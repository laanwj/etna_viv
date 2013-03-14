/*
 * Copyright (c) 2012-2013 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* Beginnings of TGSI->Vivante conversion -- WIP */
/* usage: ./test tgsi_testdata/cube_companion_frag.tgsi */
#include "tgsi/tgsi_text.h"
#include "tgsi/tgsi_iterate.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_parse.h"
#include "pipe/p_shader_tokens.h"
#include "util/u_memory.h"

#include "etna_util.h"
#include "isa.xml.h"

#include <stdio.h>

static char *
load_text_file(const char *file_name)
{
    char *text = NULL;
    size_t size;
    size_t total_read = 0;
    FILE *fp = fopen(file_name, "rb");

    if (!fp) {
        return NULL;
    }

    fseek(fp, 0L, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    text = (char *) malloc(size + 1);
    if (text != NULL) {
        do {
            size_t bytes = fread(text + total_read,
                         1, size - total_read, fp);
            if (bytes < size - total_read) {
                free(text);
                text = NULL;
                break;
            }

            if (bytes == 0) {
                break;
            }

            total_read += bytes;
        } while (total_read < size);

        text[total_read] = '\0';
    }

    fclose(fp);

    return text;
}

#define ETNA_MAX_TEMPS (64)
#define ETNA_MAX_TOKENS (1024)
#define ETNA_MAX_IMM (1024)

struct etna_reg_desc
{
    int idx;
    bool active;
    bool declared;
    int first_use; /* instruction id of first use */
    int last_use;  /* instruction id of last use */
    int temp; /* temporary to map to */
};

/* scratch area for compiling shader */
struct etna_compile_data
{
    struct etna_reg_desc temps[ETNA_MAX_TEMPS];
    struct etna_reg_desc inputs[ETNA_MAX_TEMPS];
    struct etna_reg_desc outputs[ETNA_MAX_TEMPS];
    uint32_t imm_data[ETNA_MAX_IMM];
    uint32_t imm_size; /* size of immediates (in 32 bit units) */
    uint32_t const_size; /* size of constants (in 32 bit units) */
};

enum reg_sort_order
{
    FIRST_USE_ASC,
    FIRST_USE_DESC,
    LAST_USE_ASC,
    LAST_USE_DESC
};

struct sort_rec
{
    int idx;
    int key;
};

static int sort_rec_compar(const struct sort_rec *a, const struct sort_rec *b)
{
    if(a->key < b->key) return -1;
    if(a->key > b->key) return 1;
    return 0;
}

/* create an index on a register set based on certain criteria. */
static int sort_registers(
        struct sort_rec *sorted,
        struct etna_reg_desc *regs,
        int count,
        enum reg_sort_order so)
{
    /* pre-populate keys from active registers */
    int ptr = 0;
    for(int idx=0; idx<count; ++idx)
    {
        /* only interested in active registers now; will only assign inactive ones if no
         * space in active ones */
        if(regs[idx].active)
        {
            sorted[ptr].idx = idx;
            switch(so)
            {
            case FIRST_USE_ASC:  sorted[ptr].key = regs[idx].first_use; break;
            case LAST_USE_ASC:   sorted[ptr].key = regs[idx].last_use; break;
            case FIRST_USE_DESC: sorted[ptr].key = -regs[idx].first_use; break;
            case LAST_USE_DESC:  sorted[ptr].key = -regs[idx].last_use; break;
            }
            ptr++;
        }
    }

    qsort(sorted, ptr, sizeof(struct sort_rec), (int (*) (const void *, const void *))sort_rec_compar);
    return ptr;
}

/* assign inputs and outputs to temporaries 
 * Gallium assumes that the hardware has separate registers for taking input and output,
 * however Vivante GPUs use temporaries both for passing in inputs and passing back outputs.
 */
static void assign_inputs_to_temporaries(
        struct etna_reg_desc *inouts, int num_inouts,
        struct etna_reg_desc *temps, int num_temps)
{
    int inout_ptr = 0;
    int temp_ptr = 0;
    struct sort_rec inout_order[ETNA_MAX_TEMPS];
    struct sort_rec temps_order[ETNA_MAX_TEMPS];
    num_inouts = sort_registers(inout_order, inouts, num_inouts, LAST_USE_ASC);
    num_temps = sort_registers(temps_order, temps, num_temps, FIRST_USE_ASC);

    while(inout_ptr < num_inouts && temp_ptr < num_temps)
    {
        struct etna_reg_desc *inout = &inouts[inout_order[inout_ptr].idx];
        struct etna_reg_desc *temp = &temps[temps_order[temp_ptr].idx];
        /* last usage of this input is before or in same instruction of first use of temporary? */
        if(inout->last_use <= temp->first_use)
        {
            /* assign it and advance to next input */
            inout->temp = temp->idx;
            inout_ptr++;
        }
        temp_ptr++;
    }
}

static void assign_outputs_to_temporaries(
        struct etna_reg_desc *inouts, int num_inouts,
        struct etna_reg_desc *temps, int num_temps)
{
    int inout_ptr = 0;
    int temp_ptr = 0;
    struct sort_rec inout_order[ETNA_MAX_TEMPS];
    struct sort_rec temps_order[ETNA_MAX_TEMPS];
    num_inouts = sort_registers(inout_order, inouts, num_inouts, FIRST_USE_ASC);
    num_temps = sort_registers(temps_order, temps, num_temps, LAST_USE_ASC);

    while(inout_ptr < num_inouts && temp_ptr < num_temps)
    {
        struct etna_reg_desc *inout = &inouts[inout_order[inout_ptr].idx];
        struct etna_reg_desc *temp = &temps[temps_order[temp_ptr].idx];
        /* first usage of this output is after or in same instruction of last use of temporary? */
        if(inout->first_use >= temp->first_use)
        {
            /* assign it and advance to next output */
            inout->temp = temp->idx;
            inout_ptr++;
        }
        temp_ptr++;
    }
}

/* Allocate a new, unused, temporary */
static int alloc_new_temp(struct etna_compile_data *cd)
{
    for(int idx=0; idx<ETNA_MAX_TEMPS; ++idx)
    {
        if(!cd->temps[idx].active)
        {
            cd->temps[idx].active = true;
            return idx;
        }
    }
    return -1;
}

/* Allocate an immediate with a certain value. If there is already one,
 * return that.
 */
static int alloc_immediate_u32(struct etna_compile_data *cd, uint32_t value)
{
    for(int idx=0; idx<cd->imm_size; ++idx)
    {
        if(cd->imm_data[idx] == value)
            return idx;
    }
    assert(cd->imm_size < ETNA_MAX_IMM);
    cd->imm_data[cd->imm_size++] = value;
    return cd->imm_size - 1;

}

static int alloc_immediate_f32(struct etna_compile_data *cd, uint32_t value)
{
    return alloc_immediate_u32(cd, etna_f32_to_u32(value));
}

/* Pass one -- check usage of temporaries, inputs, outputs */
static void etna_compile_pass1(struct etna_compile_data *cd, const struct tgsi_token *tokens)
{
    struct tgsi_parse_context ctx = {};
    unsigned status = TGSI_PARSE_OK;
    status = tgsi_parse_init(&ctx, tokens);
    assert(status == TGSI_PARSE_OK);

    for(int idx=0; idx<ETNA_MAX_TEMPS; ++idx)
    {
        cd->temps[idx].idx = idx;
        cd->temps[idx].active = false;
        cd->temps[idx].first_use = cd->temps[idx].last_use = -1;
        cd->temps[idx].temp = -1;
    }
    for(int idx=0; idx<ETNA_MAX_TEMPS; ++idx)
    {
        cd->inputs[idx].idx = idx;
        cd->inputs[idx].active = false;
        cd->inputs[idx].first_use = cd->inputs[idx].last_use = -1;
        cd->inputs[idx].temp = -1;
    }
    for(int idx=0; idx<ETNA_MAX_TEMPS; ++idx)
    {
        cd->outputs[idx].idx = idx;
        cd->outputs[idx].active = false;
        cd->outputs[idx].first_use = cd->outputs[idx].last_use = -1;
        cd->outputs[idx].temp = -1;
    }
#define MARK_USAGE(file) \
            if((file)[reg_idx].first_use == -1) \
                (file)[reg_idx].first_use = inst_idx; \
            (file)[reg_idx].last_use = inst_idx; \
            (file)[reg_idx].active = true

    int inst_idx = 0;
    while(!tgsi_parse_end_of_tokens(&ctx))
    {
        tgsi_parse_token(&ctx);
        /* declaration / immediate / instruction / property */
        //printf("Parsed token! %i\n", ctx.FullToken.Token.Type);
        //
        /* find out max register # used 
         * for every register mark first and last instruction id where it's used
         * this allows finding slots that can be used as input and output registers
         *
         * XXX in the case of loops this needs special care, as the last usage of a register
         * inside a loop means it can still be used on next loop iteration. The register can
         * only be declared "free" after the loop finishes.
         * Same for inputs: the first usage of a register inside a loop doesn't mean that the register
         * won't have been overwritten in previous iteration. The register can only be declared free before the loop
         * starts.
         * An better alternative may be to do full dominator / post-dominator analysis (especially with more complicated
         * control flow such as direct branch instructions) but not for now...
         */
        const struct tgsi_full_declaration *decl = 0;
        const struct tgsi_full_instruction *inst = 0;
        switch(ctx.FullToken.Token.Type)
        {
        case TGSI_TOKEN_TYPE_DECLARATION:
            decl = &ctx.FullToken.FullDeclaration;
            switch(decl->Declaration.File)
            {
            case TGSI_FILE_TEMPORARY:
                assert(decl->Declaration.Local); /* what are non-local temps? */
                for(int idx=decl->Range.First; idx<=decl->Range.Last; ++idx)
                {
                    cd->temps[idx].declared = true;
                }
                break;
            case TGSI_FILE_INPUT:
                for(int idx=decl->Range.First; idx<=decl->Range.Last; ++idx)
                {
                    cd->inputs[idx].declared = true;
                }
                break;
            case TGSI_FILE_OUTPUT:
                for(int idx=decl->Range.First; idx<=decl->Range.Last; ++idx)
                {
                    cd->outputs[idx].declared = true;
                }
                break;
            }
            break;
        case TGSI_TOKEN_TYPE_INSTRUCTION:
            /* iterate over operands */
            inst = &ctx.FullToken.FullInstruction;
            //printf("instruction: opcode=%i num_src=%i num_dest=%i\n", inst->Instruction.Opcode,
            //        inst->Instruction.NumSrcRegs, inst->Instruction.NumDstRegs);
            /* iterate over destination registers */
            for(int idx=0; idx<inst->Instruction.NumDstRegs; ++idx)
            {
                int reg_idx = inst->Dst[idx].Register.Index;
                switch(inst->Dst[idx].Register.File)
                {
                case TGSI_FILE_TEMPORARY: MARK_USAGE(cd->temps); break;
                case TGSI_FILE_INPUT: MARK_USAGE(cd->inputs); break;
                case TGSI_FILE_OUTPUT: MARK_USAGE(cd->outputs); break;
                }
            }
            /* iterate over source registers */
            for(int idx=0; idx<inst->Instruction.NumSrcRegs; ++idx)
            {
                int reg_idx = inst->Src[idx].Register.Index;
                switch(inst->Src[idx].Register.File)
                {
                case TGSI_FILE_TEMPORARY: MARK_USAGE(cd->temps); break;
                case TGSI_FILE_INPUT: MARK_USAGE(cd->inputs); break;
                case TGSI_FILE_OUTPUT: MARK_USAGE(cd->outputs); break;
                }
            }
            inst_idx += 1;
            break;
        default:
            break;
        }
    }
    tgsi_parse_free(&ctx);
}

/* Pass two -- check usage of immediates and constants */
static void etna_compile_pass2(struct etna_compile_data *cd, const struct tgsi_token *tokens)
{
    struct tgsi_parse_context ctx = {};
    unsigned status = TGSI_PARSE_OK;
    status = tgsi_parse_init(&ctx, tokens);
    assert(status == TGSI_PARSE_OK);

    while(!tgsi_parse_end_of_tokens(&ctx))
    {
        tgsi_parse_token(&ctx);
        const struct tgsi_full_declaration *decl = 0;
        const struct tgsi_full_immediate *imm = 0;
        switch(ctx.FullToken.Token.Type)
        {
        case TGSI_TOKEN_TYPE_DECLARATION:
            decl = &ctx.FullToken.FullDeclaration;
            switch(decl->Declaration.File)
            {
            case TGSI_FILE_CONSTANT:
                if((decl->Range.Last+1)*4 > cd->const_size)
                    cd->const_size = (decl->Range.Last+1)*4;
                break;
            }
            break;
        case TGSI_TOKEN_TYPE_IMMEDIATE: /* always adds four components */
            imm = &ctx.FullToken.FullImmediate;
            assert(cd->imm_size <= (ETNA_MAX_IMM-4));
            for(int i=0; i<4; ++i)
            {
                cd->imm_data[cd->imm_size++] = imm->u[i].Uint;
            }
            break;
        }
    }
    tgsi_parse_free(&ctx);
}

/* Pass three -- compile instructions */
static void etna_compile_pass3(struct etna_compile_data *cd, const struct tgsi_token *tokens)
{
    struct tgsi_parse_context ctx = {};
    unsigned status = TGSI_PARSE_OK;
    status = tgsi_parse_init(&ctx, tokens);
    assert(status == TGSI_PARSE_OK);

    int inst_idx = 0;
    while(!tgsi_parse_end_of_tokens(&ctx))
    {
        tgsi_parse_token(&ctx);
        const struct tgsi_full_instruction *inst = 0;
        switch(ctx.FullToken.Token.Type)
        {
        case TGSI_TOKEN_TYPE_INSTRUCTION:
            /* iterate over operands */
            inst = &ctx.FullToken.FullInstruction;
            /*
            struct tgsi_instruction             Instruction;
            struct tgsi_instruction_predicate   Predicate;
            struct tgsi_instruction_label       Label;
            struct tgsi_instruction_texture     Texture;
            struct tgsi_full_dst_register       Dst[TGSI_FULL_MAX_DST_REGISTERS];
            struct tgsi_full_src_register       Src[TGSI_FULL_MAX_SRC_REGISTERS];
            struct tgsi_texture_offset          TexOffsets[TGSI_FULL_MAX_TEX_OFFSETS];
            */
            inst_idx += 1;
            break;
        }
    }
    tgsi_parse_free(&ctx);
}

void etna_compile(struct etna_compile_data *cd, const struct tgsi_token *tokens)
{
    /* Pass one -- check usage of temporaries, inputs, outputs */
    etna_compile_pass1(cd, tokens);

    /* assign inputs: last usage of input should be <= first usage of temp */
    /*   potential optimization case:
     *     if single MOV TEMP[y], IN[x]   before which temp y is not used
     *     temp[y] can be used as input register as-is
     */
    /*   sort temporaries by first use
     *   sort inputs by last usage
     *   iterate over inputs, temporaries
     *     if last usage of input <= first usage of temp:
     *       assign input to temp
     *       advance input, temporary pointer
     *     else
     *       advance temporary pointer
     *
     *   potential problem: instruction with multiple inputs of which one is the temp and the other is the input;
     *      however, as the temp is not used before this, how would this make sense? uninitialized temporaries have an undefined
     *      value, so this would be ok
     */
    assign_inputs_to_temporaries(cd->inputs, ETNA_MAX_TEMPS, cd->temps, ETNA_MAX_TEMPS);

    /* assign outputs: first usage of output should be >= last usage of temp */
    /*   potential optimization case: 
     *      if single MOV OUT[x], TEMP[y]  after which temp y is no longer used
     *      temp[y] can be used as output register as-is 
     *
     *   potential problem: instruction with multiple outputs of which one is the temp and the other is the output;
     *      however, as the temp is not used after this, how would this make sense? could just discard the output value
     */
    /*   sort temporaries by last use
     *   sort outputs by first usage
     *   iterate over outputs, temporaries
     *     if first usage of output >= last usage of temp:
     *       assign output to temp
     *       advance output, temporary pointer
     *     else
     *       advance temporary pointer
     */
    assign_outputs_to_temporaries(cd->outputs, ETNA_MAX_TEMPS, cd->temps, ETNA_MAX_TEMPS);
   
    /* if we couldn't reuse current ones, allocate new temporaries */
    /* possible optimization: what if unallocated inputs and output could use the same register? */
    for(int idx=0; idx<ETNA_MAX_TEMPS; ++idx)
    {
        if(cd->inputs[idx].active && cd->inputs[idx].temp == -1)
        {
            cd->inputs[idx].temp = alloc_new_temp(cd);
        }
    }

    for(int idx=0; idx<ETNA_MAX_TEMPS; ++idx)
    {
        if(cd->outputs[idx].active && cd->outputs[idx].temp == -1)
        {
            cd->outputs[idx].temp = alloc_new_temp(cd);
        }
    }
    
    printf("Active temps:\n");
    for(int idx=0; idx<ETNA_MAX_TEMPS; ++idx)
    {
        if(cd->temps[idx].active)
        {
            printf("    %i first_use=%i last_use=%i\n", idx, cd->temps[idx].first_use, cd->temps[idx].last_use);
        }
    }
    printf("Active inputs:\n");
    for(int idx=0; idx<ETNA_MAX_TEMPS; ++idx)
    {
        if(cd->inputs[idx].active)
        {
            printf("    %i first_use=%i last_use=%i temp=%i\n", idx, cd->inputs[idx].first_use, cd->inputs[idx].last_use,
                    cd->inputs[idx].temp);
        }
    }
    printf("Active outputs:\n");
    for(int idx=0; idx<ETNA_MAX_TEMPS; ++idx)
    {
        if(cd->outputs[idx].active)
        {
            printf("    %i first_use=%i last_use=%i temp=%i\n", idx, cd->outputs[idx].first_use, cd->outputs[idx].last_use,
                    cd->outputs[idx].temp);
        }
    }
    /* pass 2: assign uniform positions to constants and immediates
     *
     * Let's place the immediates (IMM) first, then CONST.
     * For now, naively allocate FLOAT4s. There is not much space for optimizing here as 
     * 1) the GLSL/TGSI compiler did constant packing for us and 
     * 2) the Gallium API passes the entire const buf consecutively assuming we do not reorder anything
     */
    etna_compile_pass2(cd, tokens);
    /* XXX it is possible for the compilation process itself to generate extra immediates for
     * constants such as pi, one, zero. We need to be careful to not add immediates that already
     * exist.
     */
    printf("const size: %i\n", cd->const_size);
    printf("imm size: %i\n", cd->imm_size);

    /* pass 3: compile instructions
     */
    etna_compile_pass3(cd, tokens);
}

int main(int argc, char **argv)
{
    if(argc < 2)
    {
        fprintf(stderr, "Must pass shader source name as argument\n");
        exit(1);
    }
    const char *text = load_text_file(argv[1]);
    if(!text)
    {
        fprintf(stderr, "Unable to open %s\n", argv[1]);
        exit(1);
    }
    struct tgsi_token tokens[ETNA_MAX_TOKENS];
    if(tgsi_text_translate(text, tokens, Elements(tokens)))
    {
        /* tgsi_dump(tokens, 0); */
#if 0
        union {
            struct tgsi_header h;
            struct tgsi_token t;
        } hdr;

        hdr.t = tokens[0];
        printf("Header size: %i\n", hdr.h.HeaderSize);
        printf("Body size: %i\n", hdr.h.BodySize);
        int totalSize = hdr.h.HeaderSize + hdr.h.BodySize;

        for(int i=0; i<totalSize; ++i)
        {
            printf("%08x ", *((uint32_t*)&tokens[i]));
        }
        printf("\n");
#endif
        struct etna_compile_data cdata = {};
        etna_compile(&cdata, tokens);

    } else {
        fprintf(stderr, "Unable to parse %s\n", argv[1]);
    }

    return 0;
}

