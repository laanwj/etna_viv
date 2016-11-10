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

/* TGSI->Vivante shader ISA conversion */

/* What does the compiler return (see etna_shader_object)?
 *  1) instruction data
 *  2) input-to-temporary mapping (fixed for ps)
 *      *) in case of ps, semantic -> varying id mapping
 *      *) for each varying: number of components used (r, rg, rgb, rgba)
 *  3) temporary-to-output mapping (in case of vs, fixed for ps)
 *  4) for each input/output: possible semantic (position, color, glpointcoord, ...)
 *  5) immediates base offset, immediates data
 *  6) used texture units (and possibly the TGSI_TEXTURE_* type); not needed to configure the hw, but useful
 *       for error checking
 *  7) enough information to add the z=(z+w)/2.0 necessary for older chips (output reg id is enough)
 *
 *  Empty shaders are not allowed, should always at least generate a NOP. Also if there is a label
 *  at the end of the shader, an extra NOP should be generated as jump target.
 *
 * TODO
 * * Allow loops
 * * Use an instruction scheduler
 * * Indirect access to uniforms / temporaries using amode
 */
#include "etna_compiler.h"
#include "etna_asm.h"
#include "etna_internal.h"
#include "etna_debug.h"

#include "tgsi/tgsi_iterate.h"
#include "tgsi/tgsi_strings.h"
#include "tgsi/tgsi_util.h"
#include "tgsi/tgsi_info.h"
#include "pipe/p_shader_tokens.h"
#include "util/u_memory.h"
#include "util/u_math.h"

#include <etnaviv/etna.h>
#include <etnaviv/etna_util.h>
#include <etnaviv/isa.xml.h>
#include <etnaviv/state_3d.xml.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Native register description structure */
struct etna_native_reg
{
    unsigned valid:1;
    unsigned is_tex:1; /* is texture unit, overrides rgroup */
    unsigned rgroup:3;
    unsigned id:9;
};

/* Register description */
struct etna_reg_desc
{
    enum tgsi_file_type file; /* IN, OUT, TEMP, ... */
    int idx; /* index into file */
    bool active; /* used in program */
    int first_use; /* instruction id of first use (scope begin) */
    int last_use;  /* instruction id of last use (scope end, inclusive) */

    struct etna_native_reg native; /* native register to map to */
    unsigned usage_mask:4; /* usage, per channel */
    bool has_semantic; /* register has associated TGSI semantic */
    struct tgsi_declaration_semantic semantic; /* TGSI semantic */
    struct tgsi_declaration_interp interp; /* Interpolation type */
};

/* Label information structure */
struct etna_compile_label
{
    int inst_idx; /* Instruction id that label points to */
};

enum etna_compile_frame_type {
    ETNA_COMPILE_FRAME_IF, /* IF/ELSE/ENDIF */
};

/* nesting scope frame (LOOP, IF, ...) during compilation
 */
struct etna_compile_frame
{
    enum etna_compile_frame_type type;
    struct etna_compile_label *lbl_else;
    struct etna_compile_label *lbl_endif;
};

/* scratch area for compiling shader, freed after compilation finishes */
struct etna_compile_data
{
    uint processor; /* TGSI_PROCESSOR_... */

    /* Register descriptions, per TGSI file, per register index */
    struct etna_reg_desc *file[TGSI_FILE_COUNT];

    /* Number of registers in each TGSI file (max register+1) */
    uint file_size[TGSI_FILE_COUNT];

    /* Keep track of TGSI register declarations */
    struct etna_reg_desc decl[ETNA_MAX_DECL];
    uint total_decls;

    /* Bitmap of dead instructions which are removed in a separate pass */
    bool dead_inst[ETNA_MAX_TOKENS];

    /* Immediate data */
    uint32_t imm_data[ETNA_MAX_IMM];
    uint32_t imm_base; /* base of immediates (in 32 bit units) */
    uint32_t imm_size; /* size of immediates (in 32 bit units) */

    /* Next free native register, for register allocation */
    uint32_t next_free_native;

    /* Temporary register for use within translated TGSI instruction,
     * only allocated when needed.
     */
    int inner_temps; /* number of inner temps used; only up to one available at this point */
    struct etna_native_reg inner_temp;

    /* Fields for handling nested conditionals */
    struct etna_compile_frame frame_stack[ETNA_MAX_DEPTH];
    int frame_sp;
    struct etna_compile_label *lbl_usage[ETNA_MAX_INSTRUCTIONS]; /* label usage reference, per instruction */
    struct etna_compile_label labels[ETNA_MAX_LABELS]; /* XXX use subheap allocation */
    int num_labels;

    /* Code generation */
    int inst_ptr; /* current instruction pointer */
    uint32_t code[ETNA_MAX_INSTRUCTIONS*ETNA_INST_SIZE];

    /* I/O */

    /* Number of varyings (PS only) */
    int num_varyings;

    /* GPU hardware specs */
    const struct etna_pipe_specs *specs;
};

/** Register allocation **/
enum reg_sort_order
{
    FIRST_USE_ASC,
    FIRST_USE_DESC,
    LAST_USE_ASC,
    LAST_USE_DESC
};

/* Augmented register description for sorting */
struct sort_rec
{
    struct etna_reg_desc *ptr;
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
            sorted[ptr].ptr = &regs[idx];
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
    /* sort index by key */
    qsort(sorted, ptr, sizeof(struct sort_rec), (int (*) (const void *, const void *))sort_rec_compar);
    return ptr;
}

/* Allocate a new, unused, native temp register */
static struct etna_native_reg alloc_new_native_reg(struct etna_compile_data *cd)
{
    assert(cd->next_free_native < ETNA_MAX_TEMPS);
    int rv = cd->next_free_native;
    cd->next_free_native++;
    return (struct etna_native_reg){ .valid=1, .rgroup=INST_RGROUP_TEMP, .id=rv };
}

/* assign TEMPs to native registers */
static void assign_temporaries_to_native(struct etna_compile_data *cd, struct etna_reg_desc *temps, int num_temps)
{
    for(int idx=0; idx<num_temps; ++idx)
    {
        temps[idx].native = alloc_new_native_reg(cd);
    }
}

/* assign inputs and outputs to temporaries
 * Gallium assumes that the hardware has separate registers for taking input and output,
 * however Vivante GPUs use temporaries both for passing in inputs and passing back outputs.
 * Try to re-use temporary registers where possible.
 */
static void assign_inouts_to_temporaries(struct etna_compile_data *cd, uint file)
{
    bool mode_inputs = (file == TGSI_FILE_INPUT);
    int inout_ptr = 0, num_inouts;
    int temp_ptr = 0, num_temps;
    struct sort_rec inout_order[ETNA_MAX_TEMPS];
    struct sort_rec temps_order[ETNA_MAX_TEMPS];
    num_inouts = sort_registers(inout_order,
            cd->file[file], cd->file_size[file],
            mode_inputs ? LAST_USE_ASC : FIRST_USE_ASC);
    num_temps = sort_registers(temps_order,
            cd->file[TGSI_FILE_TEMPORARY], cd->file_size[TGSI_FILE_TEMPORARY],
            mode_inputs ? FIRST_USE_ASC : LAST_USE_ASC);

    while(inout_ptr < num_inouts && temp_ptr < num_temps)
    {
        struct etna_reg_desc *inout = inout_order[inout_ptr].ptr;
        struct etna_reg_desc *temp = temps_order[temp_ptr].ptr;
        if(!inout->active || inout->native.valid) /* Skip if already a native register assigned */
        {
            inout_ptr++;
            continue;
        }
        /* last usage of this input is before or in same instruction of first use of temporary? */
        if(mode_inputs ? (inout->last_use <= temp->first_use) :
                         (inout->first_use >= temp->last_use))
        {
            /* assign it and advance to next input */
            inout->native = temp->native;
            inout_ptr++;
        }
        temp_ptr++;
    }
    /* if we couldn't reuse current ones, allocate new temporaries
     */
    for(inout_ptr=0; inout_ptr<num_inouts; ++inout_ptr)
    {
        struct etna_reg_desc *inout = inout_order[inout_ptr].ptr;
        if(inout->active && !inout->native.valid)
        {
            inout->native = alloc_new_native_reg(cd);
        }
    }
}

/* Allocate an immediate with a certain value and return the index. If
 * there is already an immediate with that value, return that.
 */
static struct etna_inst_src alloc_imm_u32(struct etna_compile_data *cd, uint32_t value)
{
    int idx;
    /* Could use a hash table to speed this up */
    for(idx = 0; idx<cd->imm_size; ++idx)
    {
        if(cd->imm_data[idx] == value)
            break;
    }
    if(idx == cd->imm_size) /* allocate new immediate */
    {
        assert(cd->imm_size < ETNA_MAX_IMM);
        idx = cd->imm_size++;
        cd->imm_data[idx] = value;
    }

    /* swizzle so that component with value is returned in all components */
    idx += cd->imm_base;
    struct etna_inst_src imm_src = {
        .use = 1,
        .rgroup = INST_RGROUP_UNIFORM_0,
        .reg = idx/4,
        .swiz = INST_SWIZ_BROADCAST(idx & 3)
    };
    return imm_src;
}

/* Allocate immediate with a certain float value. If there is already an
 * immediate with that value, return that.
 */
static struct etna_inst_src alloc_imm_f32(struct etna_compile_data *cd, float value)
{
    return alloc_imm_u32(cd, etna_f32_to_u32(value));
}

/* Pass -- check register file declarations and immediates */
static void etna_compile_parse_declarations(struct etna_compile_data *cd, const struct tgsi_token *tokens)
{
    struct tgsi_parse_context ctx = {};
    unsigned status = TGSI_PARSE_OK;
    status = tgsi_parse_init(&ctx, tokens);
    assert(status == TGSI_PARSE_OK);

    cd->processor = ctx.FullHeader.Processor.Processor;

    while(!tgsi_parse_end_of_tokens(&ctx))
    {
        tgsi_parse_token(&ctx);
        switch(ctx.FullToken.Token.Type)
        {
        case TGSI_TOKEN_TYPE_DECLARATION: {
            const struct tgsi_full_declaration *decl = &ctx.FullToken.FullDeclaration;
            /* Extend size of register file to encompass entire declaration */
            cd->file_size[decl->Declaration.File] = MAX2(cd->file_size[decl->Declaration.File], decl->Range.Last+1);
            } break;
        case TGSI_TOKEN_TYPE_IMMEDIATE: { /* immediates are handled differently from other files; they are not declared
                                           explicitly, and always add four components */
            const struct tgsi_full_immediate *imm = &ctx.FullToken.FullImmediate;
            assert(cd->imm_size <= (ETNA_MAX_IMM-4));
            for(int i=0; i<4; ++i)
            {
                cd->imm_data[cd->imm_size++] = imm->u[i].Uint;
            }
            cd->file_size[TGSI_FILE_IMMEDIATE] = cd->imm_size / 4;
            } break;
        }
    }
    tgsi_parse_free(&ctx);
}

/* Allocate register declarations for the registers in all register files */
static void etna_allocate_decls(struct etna_compile_data *cd)
{
    uint idx=0;
    for(int x=0; x<TGSI_FILE_COUNT; ++x)
    {
        cd->file[x] = &cd->decl[idx];
        for(int sub=0; sub<cd->file_size[x]; ++sub)
        {
            cd->decl[idx].file = x;
            cd->decl[idx].idx = sub;
            idx++;
        }
    }
    cd->total_decls = idx;
}

/* Pass -- check and record usage of temporaries, inputs, outputs */
static void etna_compile_pass_check_usage(struct etna_compile_data *cd, const struct tgsi_token *tokens)
{
    struct tgsi_parse_context ctx = {};
    unsigned status = TGSI_PARSE_OK;
    status = tgsi_parse_init(&ctx, tokens);
    assert(status == TGSI_PARSE_OK);

    for(int idx=0; idx<cd->total_decls; ++idx)
    {
        cd->decl[idx].active = false;
        cd->decl[idx].first_use = cd->decl[idx].last_use = -1;
    }

    int inst_idx = 0;
    while(!tgsi_parse_end_of_tokens(&ctx))
    {
        tgsi_parse_token(&ctx);
        /* find out max register #s used
         * For every register mark first and last instruction index where it's
         * used this allows finding ranges where the temporary can be borrowed
         * as input and/or output register
         *
         * XXX in the case of loops this needs special care, or even be completely disabled, as
         * the last usage of a register inside a loop means it can still be used on next loop
         * iteration (execution is no longer * chronological). The register can only be
         * declared "free" after the loop finishes.
         *
         * Same for inputs: the first usage of a register inside a loop doesn't mean that the register
         * won't have been overwritten in previous iteration. The register can only be declared free before the loop
         * starts.
         * The proper way would be to do full dominator / post-dominator analysis (especially with more complicated
         * control flow such as direct branch instructions) but not for now...
         */
        switch(ctx.FullToken.Token.Type)
        {
        case TGSI_TOKEN_TYPE_DECLARATION: {
            /* Declaration: fill in file details */
            const struct tgsi_full_declaration *decl = &ctx.FullToken.FullDeclaration;
            for(int idx=decl->Range.First; idx<=decl->Range.Last; ++idx)
            {
                cd->file[decl->Declaration.File][idx].usage_mask = 0; // we'll compute this ourselves
                cd->file[decl->Declaration.File][idx].has_semantic = decl->Declaration.Semantic;
                cd->file[decl->Declaration.File][idx].semantic = decl->Semantic;
                cd->file[decl->Declaration.File][idx].interp = decl->Interp;
            }
            } break;
        case TGSI_TOKEN_TYPE_INSTRUCTION: {
            /* Instruction: iterate over operands of instruction */
            const struct tgsi_full_instruction *inst = &ctx.FullToken.FullInstruction;
            /* iterate over destination registers */
            for(int idx=0; idx<inst->Instruction.NumDstRegs; ++idx)
            {
                struct etna_reg_desc *reg_desc = &cd->file[inst->Dst[idx].Register.File][inst->Dst[idx].Register.Index];
                if(reg_desc->first_use == -1)
                    reg_desc->first_use = inst_idx;
                reg_desc->last_use = inst_idx;
                reg_desc->active = true;
            }
            /* iterate over source registers */
            for(int idx=0; idx<inst->Instruction.NumSrcRegs; ++idx)
            {
                struct etna_reg_desc *reg_desc = &cd->file[inst->Src[idx].Register.File][inst->Src[idx].Register.Index];
                if(reg_desc->first_use == -1)
                    reg_desc->first_use = inst_idx;
                reg_desc->last_use = inst_idx;
                reg_desc->active = true;
                /* accumulate usage mask for register, this is used to determine how many slots for varyings
                 * should be allocated */
                reg_desc->usage_mask |= tgsi_util_get_inst_usage_mask(inst, idx);
            }
            inst_idx += 1;
            } break;
        default:
            break;
        }
    }
    tgsi_parse_free(&ctx);
}

/* assign inputs that need to be assigned to specific registers */
static void assign_special_inputs(struct etna_compile_data *cd)
{
    if(cd->processor == TGSI_PROCESSOR_FRAGMENT)
    {
        /* never assign t0 as it is the position output, start assigning at t1 */
        cd->next_free_native = 1;
        /* hardwire TGSI_SEMANTIC_POSITION (input and output) to t0 */
        for(int idx=0; idx<cd->total_decls; ++idx)
        {
            struct etna_reg_desc *reg = &cd->decl[idx];
            if(reg->active && reg->semantic.Name == TGSI_SEMANTIC_POSITION)
            {
                reg->native.valid = 1;
                reg->native.rgroup = INST_RGROUP_TEMP;
                reg->native.id = 0;
            }
        }
    }
}

/* Check that a move instruction does not swizzle any of the components
 * that it writes.
 */
static bool etna_mov_check_no_swizzle(const struct tgsi_dst_register dst, const struct tgsi_src_register src)
{
    return (!(dst.WriteMask & TGSI_WRITEMASK_X) || src.SwizzleX == TGSI_SWIZZLE_X) &&
           (!(dst.WriteMask & TGSI_WRITEMASK_Y) || src.SwizzleY == TGSI_SWIZZLE_Y) &&
           (!(dst.WriteMask & TGSI_WRITEMASK_Z) || src.SwizzleZ == TGSI_SWIZZLE_Z) &&
           (!(dst.WriteMask & TGSI_WRITEMASK_W) || src.SwizzleW == TGSI_SWIZZLE_W);

}

/* Pass -- optimize outputs
 * Mesa tends to generate code like this at the end if their shaders
 *   MOV OUT[1], TEMP[2]
 *   MOV OUT[0], TEMP[0]
 *   MOV OUT[2], TEMP[1]
 * Recognize if
 * a) there is only a single assignment to an output register and
 * b) the temporary is not used after that
 * Also recognize direct assignment of IN to OUT (passthrough)
 **/
static void etna_compile_pass_optimize_outputs(struct etna_compile_data *cd, const struct tgsi_token *tokens)
{
    struct tgsi_parse_context ctx = {};
    unsigned status = TGSI_PARSE_OK;
    status = tgsi_parse_init(&ctx, tokens);
    assert(status == TGSI_PARSE_OK);

    int inst_idx = 0;
    while(!tgsi_parse_end_of_tokens(&ctx))
    {
        tgsi_parse_token(&ctx);
        switch(ctx.FullToken.Token.Type)
        {
        case TGSI_TOKEN_TYPE_INSTRUCTION: {
            const struct tgsi_full_instruction *inst = &ctx.FullToken.FullInstruction;
            /* iterate over operands */
            switch(inst->Instruction.Opcode)
            {
            case TGSI_OPCODE_MOV: {
                uint out_idx = inst->Dst[0].Register.Index;
                uint in_idx = inst->Src[0].Register.Index;
                /* assignment of temporary to output --
                 * and the output doesn't yet have a native register assigned
                 * and the last use of the temporary is this instruction
                 * and the MOV does not do a swizzle
                 */
                if(inst->Dst[0].Register.File == TGSI_FILE_OUTPUT &&
                   inst->Src[0].Register.File == TGSI_FILE_TEMPORARY &&
                   !cd->file[TGSI_FILE_OUTPUT][out_idx].native.valid &&
                   cd->file[TGSI_FILE_TEMPORARY][in_idx].last_use == inst_idx &&
                   etna_mov_check_no_swizzle(inst->Dst[0].Register, inst->Src[0].Register))
                {
                    cd->file[TGSI_FILE_OUTPUT][out_idx].native = cd->file[TGSI_FILE_TEMPORARY][in_idx].native;
                    /* prevent temp from being re-used for the rest of the shader */
                    cd->file[TGSI_FILE_TEMPORARY][in_idx].last_use = ETNA_MAX_TOKENS;
                    /* mark this MOV instruction as a no-op */
                    cd->dead_inst[inst_idx] = true;
                }
                /* direct assignment of input to output --
                 * and the input or output doesn't yet have a native register assigned
                 * and the output is only used in this instruction,
                 * allocate a new register, and associate both input and output to it
                 * and the MOV does not do a swizzle
                 */
                if(inst->Dst[0].Register.File == TGSI_FILE_OUTPUT &&
                   inst->Src[0].Register.File == TGSI_FILE_INPUT &&
                   !cd->file[TGSI_FILE_INPUT][in_idx].native.valid &&
                   !cd->file[TGSI_FILE_OUTPUT][out_idx].native.valid &&
                   cd->file[TGSI_FILE_OUTPUT][out_idx].last_use == inst_idx &&
                   cd->file[TGSI_FILE_OUTPUT][out_idx].first_use == inst_idx &&
                   etna_mov_check_no_swizzle(inst->Dst[0].Register, inst->Src[0].Register))
                {
                    cd->file[TGSI_FILE_OUTPUT][out_idx].native =
                        cd->file[TGSI_FILE_INPUT][in_idx].native =
                            alloc_new_native_reg(cd);
                    /* mark this MOV instruction as a no-op */
                    cd->dead_inst[inst_idx] = true;
                }
                } break;
            default: ;
            }
            inst_idx += 1;
            } break;
        }
    }
    tgsi_parse_free(&ctx);
}

/* Get a temporary to be used within one TGSI instruction.
 * The first time that this function is called the temporary will be allocated.
 * Each call to this function will return the same temporary.
 */
static struct etna_native_reg etna_compile_get_inner_temp(struct etna_compile_data *cd)
{
    if(cd->inner_temps)
        BUG("Multiple inner temporaries (%i) requested in one instruction", cd->inner_temps + 1);
    if(!cd->inner_temp.valid)
        cd->inner_temp = alloc_new_native_reg(cd);
    cd->inner_temps += 1;
    return cd->inner_temp;
}

/* Emit instruction and append it to program */
static void emit_inst(struct etna_compile_data *cd, struct etna_inst *inst)
{
    assert(cd->inst_ptr <= ETNA_MAX_INSTRUCTIONS);

    /* Check for uniform conflicts (each instruction can only access one uniform),
     * if detected, use an intermediate temporary */
    unsigned uni_rgroup = -1;
    unsigned uni_reg = -1;

    for(int src=0; src<ETNA_NUM_SRC; ++src)
    {
        if(etna_rgroup_is_uniform(inst->src[src].rgroup))
        {
            if(uni_reg == -1) /* first unique uniform used */
            {
                uni_rgroup = inst->src[src].rgroup;
                uni_reg = inst->src[src].reg;
            } else { /* second or later; check that it is a re-use */
                if(uni_rgroup != inst->src[src].rgroup ||
                   uni_reg != inst->src[src].reg)
                {
                    DBG_F(ETNA_DBG_COMPILER_MSGS,
                            "perf warning: instruction that accesses different uniforms, need to generate extra MOV");
                    struct etna_native_reg inner_temp = etna_compile_get_inner_temp(cd);

                    /* Generate move instruction to temporary */
                    etna_assemble(&cd->code[cd->inst_ptr*4], &(struct etna_inst) {
                        .opcode = INST_OPCODE_MOV,
                        .dst.use = 1,
                        .dst.comps = INST_COMPS_X | INST_COMPS_Y | INST_COMPS_Z | INST_COMPS_W,
                        .dst.reg = inner_temp.id,
                        .src[2] = inst->src[src]
                        });
                    cd->inst_ptr ++;

                    /* Modify instruction to use temp register instead of uniform */
                    inst->src[src].use = 1;
                    inst->src[src].rgroup = INST_RGROUP_TEMP;
                    inst->src[src].reg = inner_temp.id;
                    inst->src[src].swiz = INST_SWIZ_IDENTITY; /* swizzling happens on MOV */
                    inst->src[src].neg = 0; /* negation happens on MOV */
                    inst->src[src].abs = 0; /* abs happens on MOV */
                    inst->src[src].amode = 0; /* amode effects happen on MOV */
                }
            }
        }
    }

    /* Finally assemble the actual instruction */
    etna_assemble(&cd->code[cd->inst_ptr*4], inst);
    cd->inst_ptr ++;
}

/* convert destination operand */
static struct etna_inst_dst convert_dst(struct etna_compile_data *cd, const struct tgsi_full_dst_register *in)
{
    struct etna_inst_dst rv = {
        /// XXX .amode
        .use = 1,
        .comps = in->Register.WriteMask,
    };
    struct etna_native_reg native_reg = cd->file[in->Register.File][in->Register.Index].native;
    assert(native_reg.valid && !native_reg.is_tex && native_reg.rgroup == INST_RGROUP_TEMP); /* can only assign to temporaries */
    rv.reg = native_reg.id;
    return rv;
}

/* convert texture operand */
static struct etna_inst_tex convert_tex(struct etna_compile_data *cd, const struct tgsi_full_src_register *in, const struct tgsi_instruction_texture *tex)
{
    struct etna_inst_tex rv = {
        // XXX .amode (to allow for an array of samplers?)
        .swiz = INST_SWIZ_IDENTITY
    };
    struct etna_native_reg native_reg = cd->file[in->Register.File][in->Register.Index].native;
    assert(native_reg.is_tex && native_reg.valid);
    rv.id = native_reg.id;
    return rv;
}

/* convert source operand */
static struct etna_inst_src convert_src(struct etna_compile_data *cd, const struct tgsi_full_src_register *in, uint32_t swizzle)
{
    struct etna_inst_src rv = {
        .use = 1,
        .swiz = inst_swiz_compose(
                    INST_SWIZ(in->Register.SwizzleX, in->Register.SwizzleY, in->Register.SwizzleZ, in->Register.SwizzleW),
                    swizzle),
        .neg = in->Register.Negate,
        .abs = in->Register.Absolute,
        // XXX .amode
    };
    struct etna_native_reg native_reg = cd->file[in->Register.File][in->Register.Index].native;
    assert(native_reg.valid && !native_reg.is_tex);
    rv.rgroup = native_reg.rgroup;
    rv.reg = native_reg.id;
    return rv;
}

/* convert destination to source operand (for operation in place)
 * i.e,
 *    MUL dst0.x__w, src0.xyzw, 2/PI
 *    SIN dst0.x__w, dst0.xyzw
 */
static struct etna_inst_src convert_dst_to_src(struct etna_compile_data *cd,  const struct tgsi_full_dst_register *in)
{
    struct etna_inst_src rv = {
        .use = 1,
        .swiz = INST_SWIZ_IDENTITY, /* no swizzle needed, destination does selection */
        .neg = 0,
        .abs = 0,
    };
    struct etna_native_reg native_reg = cd->file[in->Register.File][in->Register.Index].native;
    assert(native_reg.valid && !native_reg.is_tex);
    rv.rgroup = native_reg.rgroup;
    rv.reg = native_reg.id;
    return rv;
}

/* create a new label */
static struct etna_compile_label *alloc_new_label(struct etna_compile_data *cd)
{
    assert(cd->num_labels < ETNA_MAX_LABELS);
    struct etna_compile_label *rv = &cd->labels[cd->num_labels++];
    rv->inst_idx = -1; /* start by point to no specific instruction */
    return rv;
}

/* place label at current instruction pointer */
static void label_place(struct etna_compile_data *cd, struct etna_compile_label *label)
{
    label->inst_idx = cd->inst_ptr;
}

/* mark label use at current instruction.
 * target of the label will be filled in in the marked instruction's src2.imm slot as soon
 * as the value becomes known.
 */
static void label_mark_use(struct etna_compile_data *cd, struct etna_compile_label *label)
{
    assert(cd->inst_ptr < ETNA_MAX_INSTRUCTIONS);
    cd->lbl_usage[cd->inst_ptr] = label;
}

/* Pass -- compile instructions */
static void etna_compile_pass_generate_code(struct etna_compile_data *cd, const struct tgsi_token *tokens)
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
        /* No inner temps used yet for this instruction, clear counter */
        cd->inner_temps = 0;

        switch(ctx.FullToken.Token.Type)
        {
        case TGSI_TOKEN_TYPE_INSTRUCTION:
            /* iterate over operands */
            inst = &ctx.FullToken.FullInstruction;
            if(cd->dead_inst[inst_idx]) /* skip dead instructions */
            {
                inst_idx++;
                continue;
            }
            assert(inst->Instruction.Saturate != TGSI_SAT_MINUS_PLUS_ONE);
            int sat = (inst->Instruction.Saturate == TGSI_SAT_ZERO_ONE);
            /* Use a naive switch statement to get up and running, later on when we have more experience with
             * Vivante instructions generation, this may be shortened greatly by using lookup in a table with patterns. */
            switch(inst->Instruction.Opcode)
            {
            case TGSI_OPCODE_MOV:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_MOV,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_LIT: {
                /*
                LOG tmp.x, void, void, src.yyyy
                MUL tmp.x, tmp.xxxx, src.wwww, void
                LITP dst, src.xxyy, src.xxxx, tmp.xxxx
                 */
                struct etna_native_reg inner_temp = etna_compile_get_inner_temp(cd);
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_LOG,
                        .sat = 0,
                        .dst.use = 1,
                        .dst.comps = INST_COMPS_X, /* tmp.x */
                        .dst.reg = inner_temp.id,
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_BROADCAST(1)), /* src.yyyy */
                        });
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_MUL,
                        .sat = 0,
                        .dst.use = 1,
                        .dst.comps = INST_COMPS_X,
                        .dst.reg = inner_temp.id,
                        .src[0].use = 1,
                        .src[0].swiz = INST_SWIZ_BROADCAST(0), /* tmp.xxxx */
                        .src[0].neg = 0,
                        .src[0].abs = 0,
                        .src[0].rgroup = inner_temp.rgroup,
                        .src[0].reg = inner_temp.id,
                        .src[1] = convert_src(cd, &inst->Src[0], INST_SWIZ_BROADCAST(3)), /* src.wwww */
                        });
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_LITP,
                        .sat = 0,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ(0,0,1,1)), /* src.xxyy */
                        .src[1] = convert_src(cd, &inst->Src[0], INST_SWIZ_BROADCAST(0)), /* src.xxxx */
                        .src[2].use = 1,
                        .src[2].swiz = INST_SWIZ_BROADCAST(0), /* tmp.xxxx */
                        .src[2].neg = 0,
                        .src[2].abs = 0,
                        .src[2].rgroup = inner_temp.rgroup,
                        .src[2].reg = inner_temp.id,
                        });
                } break;
            case TGSI_OPCODE_RCP:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_RCP,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_RSQ:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_RSQ,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_MUL:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_MUL,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        .src[1] = convert_src(cd, &inst->Src[1], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_ADD:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_ADD,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        .src[2] = convert_src(cd, &inst->Src[1], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_DP3:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_DP3,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        .src[1] = convert_src(cd, &inst->Src[1], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_DP4:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_DP4,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        .src[1] = convert_src(cd, &inst->Src[1], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_MIN:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_SELECT,
                        .cond = INST_CONDITION_GT,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        .src[1] = convert_src(cd, &inst->Src[1], INST_SWIZ_IDENTITY),
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_MAX:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_SELECT,
                        .cond = INST_CONDITION_LT,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        .src[1] = convert_src(cd, &inst->Src[1], INST_SWIZ_IDENTITY),
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_SLT:
            case TGSI_OPCODE_SGE:
            case TGSI_OPCODE_SEQ:
            case TGSI_OPCODE_SGT:
            case TGSI_OPCODE_SLE:
            case TGSI_OPCODE_SNE:
            case TGSI_OPCODE_STR: {
                uint cond = 0;
                switch(inst->Instruction.Opcode)
                {
                case TGSI_OPCODE_SLT: cond = INST_CONDITION_LT; break;
                case TGSI_OPCODE_SGE: cond = INST_CONDITION_GE; break;
                case TGSI_OPCODE_SEQ: cond = INST_CONDITION_EQ; break;
                case TGSI_OPCODE_SGT: cond = INST_CONDITION_GT; break;
                case TGSI_OPCODE_SLE: cond = INST_CONDITION_LE; break;
                case TGSI_OPCODE_SNE: cond = INST_CONDITION_NE; break;
                case TGSI_OPCODE_STR: cond = INST_CONDITION_TRUE; break;
                }
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_SET,
                        .cond = cond,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        .src[1] = convert_src(cd, &inst->Src[1], INST_SWIZ_IDENTITY),
                        });
                } break;
            case TGSI_OPCODE_MAD:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_MAD,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        .src[1] = convert_src(cd, &inst->Src[1], INST_SWIZ_IDENTITY),
                        .src[2] = convert_src(cd, &inst->Src[2], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_SUB: { /* ADD with negated SRC1 */
                struct etna_inst inst_out = {
                    .opcode = INST_OPCODE_ADD,
                    .sat = sat,
                    .dst = convert_dst(cd, &inst->Dst[0]),
                    .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                    .src[2] = convert_src(cd, &inst->Src[1], INST_SWIZ_IDENTITY),
                };
                inst_out.src[2].neg = !inst_out.src[2].neg;
                emit_inst(cd, &inst_out);
                } break;
            case TGSI_OPCODE_SQRT: /* only generated if HAS_SQRT_TRIG */
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_SQRT,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_FRC:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_FRC,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_FLR: /* XXX HAS_SIGN_FLOOR_CEIL */
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_FLOOR,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_CEIL: /* XXX HAS_SIGN_FLOOR_CEIL */
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_CEIL,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_SSG: /* XXX HAS_SIGN_FLOOR_CEIL */
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_SIGN,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_EX2:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_EXP,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_LG2:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_LOG,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_ABS: /* XXX can be propagated into uses of destination operand */
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_MOV,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        .src[2].abs = 1
                        });
                break;
            case TGSI_OPCODE_COS: /* fall through */
            case TGSI_OPCODE_SIN:
                if(cd->specs->has_sin_cos_sqrt)
                {
                    /* add divide by PI/2, re-use dest register, this works even in src=dst case
                     * because second instruction only uses output of first. */
                    emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_MUL,
                        .sat = 0,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY), /* any swizzling happens here */
                        .src[1] = alloc_imm_u32(cd, 2.0f/M_PI),
                    });
                    emit_inst(cd, &(struct etna_inst) {
                        .opcode = inst->Instruction.Opcode == TGSI_OPCODE_COS ? INST_OPCODE_COS : INST_OPCODE_SIN,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[2] = convert_dst_to_src(cd, &inst->Dst[0]),
                    });
                } else {
                    /* XXX fall back to Taylor series if not HAS_SQRT_TRIG,
                     * see i915_fragprog.c for a good example.
                     */
                    assert(0);
                }
                break;
            case TGSI_OPCODE_DDX:
            case TGSI_OPCODE_DDY:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = inst->Instruction.Opcode == TGSI_OPCODE_DDX ? INST_OPCODE_DSX : INST_OPCODE_DSY,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_KILL_IF:
                /* discard if (src.x < 0 || src.y < 0 || src.z < 0 || src.w < 0) */
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_TEXKILL,
                        .cond = INST_CONDITION_LZ,
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY)
                        });
                break;
            case TGSI_OPCODE_KILL:
                /* discard always */
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_TEXKILL,
                        .cond = INST_CONDITION_TRUE
                        });
                break;
            case TGSI_OPCODE_TEX:
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_TEXLD,
                        .sat = (inst->Instruction.Saturate == TGSI_SAT_ZERO_ONE),
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .tex = convert_tex(cd, &inst->Src[1], &inst->Texture),
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_TXP: { /* divide src.xyz by src.w */
                struct etna_native_reg temp = etna_compile_get_inner_temp(cd);
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_RCP,
                        .sat = 0,
                        .dst.use = 1,
                        .dst.comps = INST_COMPS_W, /* tmp.w */
                        .dst.reg = temp.id,
                        .src[2] = convert_src(cd, &inst->Src[0], INST_SWIZ_BROADCAST(3)),
                        });
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_MUL,
                        .sat = 0,
                        .dst.use = 1,
                        .dst.comps = INST_COMPS_X | INST_COMPS_Y | INST_COMPS_Z, /* tmp.xyz */
                        .dst.reg = temp.id,
                        .src[0].use = 1, /* tmp.wwww */
                        .src[0].swiz = INST_SWIZ_BROADCAST(3),
                        .src[0].neg = 0,
                        .src[0].abs = 0,
                        .src[0].rgroup = temp.rgroup,
                        .src[0].reg = temp.id,
                        .src[1] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY), /* src.xyzw */
                });
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_TEXLD,
                        .sat = (inst->Instruction.Saturate == TGSI_SAT_ZERO_ONE),
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .tex = convert_tex(cd, &inst->Src[1], &inst->Texture),
                        .src[0].use = 1, /* tmp.xyzw */
                        .src[0].swiz = INST_SWIZ_IDENTITY,
                        .src[0].neg = 0,
                        .src[0].abs = 0,
                        .src[0].rgroup = temp.rgroup,
                        .src[0].reg = temp.id,
                        });
                } break;
            case TGSI_OPCODE_CMP: /* componentwise dst = (src0 < 0) ? src1 : src2 */
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_SELECT,
                        .cond = INST_CONDITION_LZ,
                        .sat = sat,
                        .dst = convert_dst(cd, &inst->Dst[0]),
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        .src[1] = convert_src(cd, &inst->Src[1], INST_SWIZ_IDENTITY),
                        .src[2] = convert_src(cd, &inst->Src[2], INST_SWIZ_IDENTITY),
                        });
                break;
            case TGSI_OPCODE_IF:  {
                struct etna_compile_frame *f = &cd->frame_stack[cd->frame_sp++];
                /* push IF to stack */
                f->type = ETNA_COMPILE_FRAME_IF;
                /* create "else" label */
                f->lbl_else = alloc_new_label(cd);
                f->lbl_endif = NULL;
                /* mark position in instruction stream of label reference so that it can be filled in in next pass */
                label_mark_use(cd, f->lbl_else);
                /* create conditional branch to label if src0 EQ 0 */
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_BRANCH,
                        .cond = INST_CONDITION_EQ,
                        .src[0] = convert_src(cd, &inst->Src[0], INST_SWIZ_IDENTITY),
                        .src[1] = alloc_imm_f32(cd, 0.0f),
                        /* imm is filled in later */
                        });
                } break;
            case TGSI_OPCODE_ELSE: {
                assert(cd->frame_sp>0);
                struct etna_compile_frame *f = &cd->frame_stack[cd->frame_sp-1];
                assert(f->type == ETNA_COMPILE_FRAME_IF);
                /* create "endif" label, and branch to endif label */
                f->lbl_endif = alloc_new_label(cd);
                label_mark_use(cd, f->lbl_endif);
                emit_inst(cd, &(struct etna_inst) {
                        .opcode = INST_OPCODE_BRANCH,
                        .cond = INST_CONDITION_TRUE,
                        /* imm is filled in later */
                        });
                /* mark "else" label at this position in instruction stream */
                label_place(cd, f->lbl_else);
                } break;
            case TGSI_OPCODE_ENDIF: {
                assert(cd->frame_sp>0);
                struct etna_compile_frame *f = &cd->frame_stack[--cd->frame_sp];
                assert(f->type == ETNA_COMPILE_FRAME_IF);
                /* assign "endif" or "else" (if no ELSE) label to current position in instruction stream, pop IF */
                if(f->lbl_endif != NULL)
                    label_place(cd, f->lbl_endif);
                else
                    label_place(cd, f->lbl_else);
                } break;
            case TGSI_OPCODE_NOP: break;
            case TGSI_OPCODE_END: /* Nothing to do */ break;

            case TGSI_OPCODE_PK2H:
            case TGSI_OPCODE_PK2US:
            case TGSI_OPCODE_PK4B:
            case TGSI_OPCODE_PK4UB:
            case TGSI_OPCODE_RFL:
            case TGSI_OPCODE_RCC:
            case TGSI_OPCODE_DPH: /* src0.x * src1.x + src0.y * src1.y + src0.z * src1.z + src1.w */
            case TGSI_OPCODE_POW: /* lowered by mesa to ex2(y*lg2(x)) */
            case TGSI_OPCODE_XPD:
            case TGSI_OPCODE_ROUND:
            case TGSI_OPCODE_CLAMP:
            case TGSI_OPCODE_DP2A:
            case TGSI_OPCODE_LRP: /* lowered by mesa to (op2 * (1.0f - op0)) + (op1 * op0) */
            case TGSI_OPCODE_CND:
            case TGSI_OPCODE_SFL: /* SET to 0 */
            case TGSI_OPCODE_DST: /* XXX INST_OPCODE_DST */
            case TGSI_OPCODE_DP2: /* Either MUL+MAD or DP3 with a zeroed channel, but we don't have a 'zero' swizzle */
            case TGSI_OPCODE_EXP:
            case TGSI_OPCODE_LOG:
            case TGSI_OPCODE_TXB: /* XXX INST_OPCODE_TEXLDB */
            case TGSI_OPCODE_TXL: /* XXX INST_OPCODE_TEXLDL */
            case TGSI_OPCODE_UP2H:
            case TGSI_OPCODE_UP2US:
            case TGSI_OPCODE_UP4B:
            case TGSI_OPCODE_UP4UB:
            case TGSI_OPCODE_X2D:
            case TGSI_OPCODE_ARL: /* floor */
            case TGSI_OPCODE_ARR: /* round */
            case TGSI_OPCODE_ARA: /* to be removed according to doc */
            case TGSI_OPCODE_BRA: /* to be removed according to doc */
            case TGSI_OPCODE_CAL: /* XXX INST_OPCODE_CALL */
            case TGSI_OPCODE_RET: /* XXX INST_OPCODE_RET */
            case TGSI_OPCODE_BRK: /* break from loop */
            case TGSI_OPCODE_BGNLOOP:
            case TGSI_OPCODE_ENDLOOP:
            case TGSI_OPCODE_BGNSUB:
            case TGSI_OPCODE_ENDSUB:
            default:
                BUG("Unhandled instruction %s", tgsi_get_opcode_name(inst->Instruction.Opcode));
                assert(0);
            }
            inst_idx += 1;
            break;
        }
    }
    tgsi_parse_free(&ctx);
}

/* Look up register by semantic */
static struct etna_reg_desc *find_decl_by_semantic(struct etna_compile_data *cd, uint file, uint name, uint index)
{
    for(int idx=0; idx<cd->file_size[file]; ++idx)
    {
        struct etna_reg_desc *reg = &cd->file[file][idx];
        if(reg->semantic.Name == name && reg->semantic.Index == index)
        {
            return reg;
        }
    }
    return NULL; /* not found */
}

/** Add ADD and MUL instruction to bring Z/W to 0..1 if -1..1 if needed:
 * - this is a vertex shader
 * - and this is an older GPU
 */
static void etna_compile_add_z_div_if_needed(struct etna_compile_data *cd)
{
    if(cd->processor == TGSI_PROCESSOR_VERTEX && cd->specs->vs_need_z_div)
    {
        /* find position out */
        struct etna_reg_desc *pos_reg = find_decl_by_semantic(cd, TGSI_FILE_OUTPUT, TGSI_SEMANTIC_POSITION, 0);
        if(pos_reg != NULL)
        {
            /*
             * ADD tX.__z_, tX.zzzz, void, tX.wwww
             * MUL tX.__z_, tX.zzzz, 0.5, void
            */
            emit_inst(cd, &(struct etna_inst) {
                    .opcode = INST_OPCODE_ADD,
                    .dst.use = 1,
                    .dst.reg = pos_reg->native.id,
                    .dst.comps = INST_COMPS_Z,
                    .src[0].use = 1,
                    .src[0].reg = pos_reg->native.id,
                    .src[0].swiz = INST_SWIZ_BROADCAST(INST_SWIZ_COMP_Z),
                    .src[2].use = 1,
                    .src[2].reg = pos_reg->native.id,
                    .src[2].swiz = INST_SWIZ_BROADCAST(INST_SWIZ_COMP_W),
                    });
            emit_inst(cd, &(struct etna_inst) {
                    .opcode = INST_OPCODE_MUL,
                    .dst.use = 1,
                    .dst.reg = pos_reg->native.id,
                    .dst.comps = INST_COMPS_Z,
                    .src[0].use = 1,
                    .src[0].reg = pos_reg->native.id,
                    .src[0].swiz = INST_SWIZ_BROADCAST(INST_SWIZ_COMP_Z),
                    .src[1] = alloc_imm_f32(cd, 0.5f),
                    });
        }
    }
}

/** add a NOP to the shader if
 * a) the shader is empty
 * or
 * b) there is a label at the end of the shader
 */
static void etna_compile_add_nop_if_needed(struct etna_compile_data *cd)
{
    bool label_at_last_inst = false;
    for(int idx=0; idx<cd->num_labels; ++idx)
    {
        if(cd->labels[idx].inst_idx == (cd->inst_ptr-1))
        {
            label_at_last_inst = true;
        }
    }
    if(cd->inst_ptr == 0 || label_at_last_inst)
    {
        emit_inst(cd, &(struct etna_inst) {
                .opcode = INST_OPCODE_NOP
                });
    }
}

/* Allocate CONST and IMM to native ETNA_RGROUP_UNIFORM(x).
 * CONST must be consecutive as const buffers are supposed to be consecutive, and before IMM, as this is
 * more convenient because is possible for the compilation process itself to generate extra
 * immediates for constants such as pi, one, zero.
 */
static void assign_constants_and_immediates(struct etna_compile_data *cd)
{
    for(int idx=0; idx<cd->file_size[TGSI_FILE_CONSTANT]; ++idx)
    {
        cd->file[TGSI_FILE_CONSTANT][idx].native.valid = 1;
        cd->file[TGSI_FILE_CONSTANT][idx].native.rgroup = INST_RGROUP_UNIFORM_0;
        cd->file[TGSI_FILE_CONSTANT][idx].native.id = idx;
    }
    /* immediates start after the constants */
    cd->imm_base = cd->file_size[TGSI_FILE_CONSTANT] * 4;
    for(int idx=0; idx<cd->file_size[TGSI_FILE_IMMEDIATE]; ++idx)
    {
        cd->file[TGSI_FILE_IMMEDIATE][idx].native.valid = 1;
        cd->file[TGSI_FILE_IMMEDIATE][idx].native.rgroup = INST_RGROUP_UNIFORM_0;
        cd->file[TGSI_FILE_IMMEDIATE][idx].native.id = cd->imm_base/4 + idx;
    }
    DBG_F(ETNA_DBG_COMPILER_MSGS, "imm base: %i size: %i", cd->imm_base, cd->imm_size);
}

/* Assign declared samplers to native texture units */
static void assign_texture_units(struct etna_compile_data *cd)
{
    uint tex_base = 0;
    if(cd->processor == TGSI_PROCESSOR_VERTEX)
    {
        tex_base = cd->specs->vertex_sampler_offset;
    }
    for(int idx=0; idx<cd->file_size[TGSI_FILE_SAMPLER]; ++idx)
    {
        cd->file[TGSI_FILE_SAMPLER][idx].native.valid = 1;
        cd->file[TGSI_FILE_SAMPLER][idx].native.is_tex = 1; // overrides rgroup
        cd->file[TGSI_FILE_SAMPLER][idx].native.id = tex_base + idx;
    }
}

/* Additional pass to fill in branch targets. This pass should be last
 * as no instruction reordering or removing/addition can be done anymore
 * once the branch targets are computed.
 */
static void etna_compile_fill_in_labels(struct etna_compile_data *cd)
{
    for(int idx=0; idx<cd->inst_ptr ; ++idx)
    {
        if(cd->lbl_usage[idx])
        {
            etna_assemble_set_imm(&cd->code[idx * 4], cd->lbl_usage[idx]->inst_idx);
        }
    }
}

/* compare two etna_native_reg structures, return true if equal */
static bool cmp_etna_native_reg(const struct etna_native_reg to, const struct etna_native_reg from)
{
    return to.valid == from.valid && to.is_tex == from.is_tex && to.rgroup == from.rgroup &&
           to.id == from.id;
}

/* go through all declarations and swap native registers *to* and *from* */
static void swap_native_registers(struct etna_compile_data *cd, const struct etna_native_reg to, const struct etna_native_reg from)
{
    if(cmp_etna_native_reg(from, to))
        return; /* Nothing to do */
    for(int idx=0; idx<cd->total_decls; ++idx)
    {
        if(cmp_etna_native_reg(cd->decl[idx].native, from))
        {
            cd->decl[idx].native = to;
        } else if(cmp_etna_native_reg(cd->decl[idx].native, to))
        {
            cd->decl[idx].native = from;
        }
    }
}

/* For PS we need to permute so that inputs are always in temporary 0..N-1.
 * Semantic POS is always t0. If that semantic is not used, avoid t0.
 */
static void permute_ps_inputs(struct etna_compile_data *cd)
{
    /* Special inputs:
     * gl_FragCoord  VARYING_SLOT_POS   TGSI_SEMANTIC_POSITION
     * gl_PointCoord VARYING_SLOT_PNTC  TGSI_SEMANTIC_PCOORD
     */
    uint native_idx = 1;
    for(int idx=0; idx<cd->file_size[TGSI_FILE_INPUT]; ++idx)
    {
        struct etna_reg_desc *reg = &cd->file[TGSI_FILE_INPUT][idx];
        uint input_id;
        assert(reg->has_semantic);
        if(!reg->active || reg->semantic.Name == TGSI_SEMANTIC_POSITION)
            continue;
        input_id = native_idx++;
        swap_native_registers(cd, (struct etna_native_reg) {
                .valid = 1,
                .rgroup = INST_RGROUP_TEMP,
                .id = input_id
            }, cd->file[TGSI_FILE_INPUT][idx].native);
    }
    cd->num_varyings = native_idx-1;
    if(native_idx > cd->next_free_native)
        cd->next_free_native = native_idx;
}

/* fill in ps inputs into shader object */
static void fill_in_ps_inputs(struct etna_shader_object *sobj, struct etna_compile_data *cd)
{
    sobj->num_inputs = cd->num_varyings;
    assert(sobj->num_inputs < ETNA_NUM_INPUTS);
    for(int idx=0; idx<cd->file_size[TGSI_FILE_INPUT]; ++idx)
    {
        struct etna_reg_desc *reg = &cd->file[TGSI_FILE_INPUT][idx];
        if(reg->native.id > 0)
        {
            int input_id = reg->native.id - 1;
            sobj->inputs[input_id].reg = reg->native.id;
            sobj->inputs[input_id].semantic = reg->semantic;
            if(reg->semantic.Name == TGSI_SEMANTIC_COLOR) /* colors affected by flat shading */
                sobj->inputs[input_id].pa_attributes = 0x200;
            else /* texture coord or other bypasses flat shading */
                sobj->inputs[input_id].pa_attributes = 0x2f1;
            /* convert usage mask to number of components (*=wildcard)
             *   .r    (0..1)  -> 1 component
             *   .*g   (2..3)  -> 2 component
             *   .**b  (4..7)  -> 3 components
             *   .***a (8..15) -> 4 components
             */
            sobj->inputs[input_id].num_components = util_last_bit(reg->usage_mask);
        }
    }
    sobj->input_count_unk8 = 31; /* XXX what is this */
}

/* fill in output mapping for ps into shader object */
static void fill_in_ps_outputs(struct etna_shader_object *sobj, struct etna_compile_data *cd)
{
    sobj->num_outputs = 0;
    for(int idx=0; idx<cd->file_size[TGSI_FILE_OUTPUT]; ++idx)
    {
        struct etna_reg_desc *reg = &cd->file[TGSI_FILE_OUTPUT][idx];
        switch(reg->semantic.Name)
        {
        case TGSI_SEMANTIC_COLOR: /* FRAG_RESULT_COLOR */
            sobj->ps_color_out_reg = reg->native.id;
            break;
        case TGSI_SEMANTIC_POSITION: /* FRAG_RESULT_DEPTH */
            sobj->ps_depth_out_reg = reg->native.id; /* =always native reg 0, only z component should be assigned */
            break;
        default:
            assert(0); /* only outputs supported are COLOR and POSITION at the moment */
        }
    }
}

/* fill in inputs for vs into shader object */
static void fill_in_vs_inputs(struct etna_shader_object *sobj, struct etna_compile_data *cd)
{
    sobj->num_inputs = 0;
    for(int idx=0; idx<cd->file_size[TGSI_FILE_INPUT]; ++idx)
    {
        struct etna_reg_desc *reg = &cd->file[TGSI_FILE_INPUT][idx];
        assert(sobj->num_inputs < ETNA_NUM_INPUTS);
        /* XXX exclude inputs with special semantics such as gl_frontFacing */
        sobj->inputs[sobj->num_inputs].reg = reg->native.id;
        sobj->inputs[sobj->num_inputs].semantic = reg->semantic;
        sobj->inputs[sobj->num_inputs].num_components = util_last_bit(reg->usage_mask);
        sobj->num_inputs++;
    }
    sobj->input_count_unk8 = (sobj->num_inputs + 19)/16; /* XXX what is this */
}

/* build two-level output index [Semantic][Index] for fast linking */
static void build_output_index(struct etna_shader_object *sobj)
{
    int total = 0;
    int offset = 0;
    for(int name=0; name<TGSI_SEMANTIC_COUNT; ++name)
    {
        total += sobj->output_count_per_semantic[name];
    }
    sobj->output_per_semantic_list = CALLOC(total, sizeof(struct etna_shader_inout *));
    for(int name=0; name<TGSI_SEMANTIC_COUNT; ++name)
    {
        sobj->output_per_semantic[name] = &sobj->output_per_semantic_list[offset];
        offset += sobj->output_count_per_semantic[name];
    }
    for(int idx=0; idx<sobj->num_outputs; ++idx)
    {
        sobj->output_per_semantic[sobj->outputs[idx].semantic.Name]
                                 [sobj->outputs[idx].semantic.Index] = &sobj->outputs[idx];
    }
}

/* fill in outputs for vs into shader object */
static void fill_in_vs_outputs(struct etna_shader_object *sobj, struct etna_compile_data *cd)
{
    sobj->num_outputs = 0;
    for(int idx=0; idx<cd->file_size[TGSI_FILE_OUTPUT]; ++idx)
    {
        struct etna_reg_desc *reg = &cd->file[TGSI_FILE_OUTPUT][idx];
        assert(sobj->num_inputs < ETNA_NUM_INPUTS);
        switch(reg->semantic.Name)
        {
        case TGSI_SEMANTIC_POSITION:
            sobj->vs_pos_out_reg = reg->native.id;
            break;
        case TGSI_SEMANTIC_PSIZE:
            sobj->vs_pointsize_out_reg = reg->native.id;
            break;
        default:
            sobj->outputs[sobj->num_outputs].reg = reg->native.id;
            sobj->outputs[sobj->num_outputs].semantic = reg->semantic;
            sobj->outputs[sobj->num_outputs].num_components = 4; // XXX reg->num_components;
            sobj->num_outputs++;
            sobj->output_count_per_semantic[reg->semantic.Name] = MAX2(
                    reg->semantic.Index + 1,
                    sobj->output_count_per_semantic[reg->semantic.Name]);
        }
    }
    /* build two-level index for linking */
    build_output_index(sobj);

    /* fill in "mystery meat" load balancing value. This value determines how work is scheduled between VS and PS
     * in the unified shader architecture. More precisely, it is determined from the number of VS outputs, as well as chip-specific
     * vertex output buffer size, vertex cache size, and the number of shader cores.
     *
     * XXX this is a conservative estimate, the "optimal" value is only known for sure at link time because some
     * outputs may be unused and thus unmapped. Then again, in the general use case with GLSL the vertex and fragment
     * shaders are linked already before submitting to Gallium, thus all outputs are used.
     */
    int half_out = (cd->file_size[TGSI_FILE_OUTPUT] + 1) / 2;
    assert(half_out);
    uint32_t b = ((20480/(cd->specs->vertex_output_buffer_size-2*half_out*cd->specs->vertex_cache_size))+9)/10;
    uint32_t a = (b+256/(cd->specs->shader_core_count*half_out))/2;
    sobj->vs_load_balancing = VIVS_VS_LOAD_BALANCING_A(MIN2(a,255)) |
                              VIVS_VS_LOAD_BALANCING_B(MIN2(b,255)) |
                              VIVS_VS_LOAD_BALANCING_C(0x3f) |
                              VIVS_VS_LOAD_BALANCING_D(0x0f);
}

static bool etna_compile_check_limits(struct etna_compile_data *cd)
{
    int max_uniforms = (cd->processor == TGSI_PROCESSOR_VERTEX) ?
                        cd->specs->max_vs_uniforms :
                        cd->specs->max_ps_uniforms;
    /* round up number of uniforms, including immediates, in units of four */
    int num_uniforms = cd->imm_base/4 + (cd->imm_size+3)/4;
    if(cd->inst_ptr > cd->specs->max_instructions)
    {
        DBG("Number of instructions (%d) exceeds maximum %d", cd->inst_ptr, cd->specs->max_instructions);
        return false;
    }
    if(cd->next_free_native > cd->specs->max_registers)
    {
        DBG("Number of registers (%d) exceeds maximum %d", cd->next_free_native, cd->specs->max_registers);
        return false;
    }
    if(num_uniforms > max_uniforms)
    {
        DBG("Number of uniforms (%d) exceeds maximum %d", num_uniforms, max_uniforms);
        return false;
    }
    if(cd->num_varyings > cd->specs->max_varyings)
    {
        DBG("Number of varyings (%d) exceeds maximum %d", cd->num_varyings, cd->specs->max_varyings);
        return false;
    }
    return true;
}

int etna_compile_shader_object(const struct etna_pipe_specs *specs, const struct tgsi_token *tokens,
        struct etna_shader_object **out)
{
    /* Create scratch space that may be too large to fit on stack
     * XXX don't forget to free this on all exit paths.
     */
    struct etna_compile_data *cd = CALLOC_STRUCT(etna_compile_data);
    cd->specs = specs;

    /* Build a map from gallium register to native registers for files
     * CONST, SAMP, IMM, OUT, IN, TEMP.
     * SAMP will map as-is for fragment shaders, there will be a +8 offset for vertex shaders.
     */
    /* Pass one -- check register file declarations and immediates */
    etna_compile_parse_declarations(cd, tokens);

    etna_allocate_decls(cd);

    /* Pass two -- check usage of temporaries, inputs, outputs */
    etna_compile_pass_check_usage(cd, tokens);

    assign_special_inputs(cd);

    /* Assign native temp register to TEMPs */
    assign_temporaries_to_native(cd, cd->file[TGSI_FILE_TEMPORARY], cd->file_size[TGSI_FILE_TEMPORARY]);

    /* optimize outputs */
    etna_compile_pass_optimize_outputs(cd, tokens);

    /* XXX assign special inputs: gl_FrontFacing (VARYING_SLOT_FACE)
     *     this is part of RGROUP_INTERNAL
     */

    /* assign inputs: last usage of input should be <= first usage of temp */
    /*   potential optimization case:
     *     if single MOV TEMP[y], IN[x] before which temp y is not used, and after which IN[x]
     *     is not read, temp[y] can be used as input register as-is
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
    assign_inouts_to_temporaries(cd, TGSI_FILE_INPUT);

    /* assign outputs: first usage of output should be >= last usage of temp */
    /*   potential optimization case:
     *      if single MOV OUT[x], TEMP[y] (with full write mask, or at least writing all components that are used in
     *        the shader) after which temp y is no longer used temp[y] can be used as output register as-is
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
    assign_inouts_to_temporaries(cd, TGSI_FILE_OUTPUT);

    assign_constants_and_immediates(cd);
    assign_texture_units(cd);

    /* list declarations */
    for(int x=0; x<cd->total_decls; ++x)
    {
        DBG_F(ETNA_DBG_COMPILER_MSGS, "%i: %s,%d active=%i first_use=%i last_use=%i native=%i usage_mask=%x has_semantic=%i", x, tgsi_file_name(cd->decl[x].file), cd->decl[x].idx,
                cd->decl[x].active,
                cd->decl[x].first_use, cd->decl[x].last_use, cd->decl[x].native.valid?cd->decl[x].native.id:-1,
                cd->decl[x].usage_mask,
                cd->decl[x].has_semantic);
        if(cd->decl[x].has_semantic)
            DBG_F(ETNA_DBG_COMPILER_MSGS, " semantic_name=%s semantic_idx=%i",
                    tgsi_semantic_names[cd->decl[x].semantic.Name], cd->decl[x].semantic.Index);
    }
    /* XXX for PS we need to permute so that inputs are always in temporary 0..N-1.
     * There is no "switchboard" for varyings (AFAIK!). The output color, however, can be routed
     * from an arbitrary temporary.
     */
    if(cd->processor == TGSI_PROCESSOR_FRAGMENT)
    {
        permute_ps_inputs(cd);
    }

    /* list declarations */
    for(int x=0; x<cd->total_decls; ++x)
    {
        DBG_F(ETNA_DBG_COMPILER_MSGS, "%i: %s,%d active=%i first_use=%i last_use=%i native=%i usage_mask=%x has_semantic=%i", x, tgsi_file_name(cd->decl[x].file), cd->decl[x].idx,
                cd->decl[x].active,
                cd->decl[x].first_use, cd->decl[x].last_use, cd->decl[x].native.valid?cd->decl[x].native.id:-1,
                cd->decl[x].usage_mask,
                cd->decl[x].has_semantic);
        if(cd->decl[x].has_semantic)
            DBG_F(ETNA_DBG_COMPILER_MSGS, " semantic_name=%s semantic_idx=%i",
                    tgsi_semantic_names[cd->decl[x].semantic.Name], cd->decl[x].semantic.Index);
    }

    /* pass 3: generate instructions
     */
    etna_compile_pass_generate_code(cd, tokens);
    etna_compile_add_z_div_if_needed(cd);
    etna_compile_add_nop_if_needed(cd);
    etna_compile_fill_in_labels(cd);

    if(!etna_compile_check_limits(cd))
    {
        FREE(cd);
        *out = NULL;
        return -1;
    }

    /* fill in output structure */
    struct etna_shader_object *sobj = CALLOC_STRUCT(etna_shader_object);
    sobj->processor = cd->processor;
    sobj->code_size = cd->inst_ptr * 4;
    sobj->code = mem_dup(cd->code, cd->inst_ptr * 16);
    sobj->num_temps = cd->next_free_native;
    sobj->const_base = 0;
    sobj->const_size = cd->imm_base;
    sobj->imm_base = cd->imm_base;
    sobj->imm_size = cd->imm_size;
    sobj->imm_data = mem_dup(cd->imm_data, cd->imm_size * 4);
    sobj->vs_pos_out_reg = -1;
    sobj->vs_pointsize_out_reg = -1;
    sobj->ps_color_out_reg = -1;
    sobj->ps_depth_out_reg = -1;
    if(cd->processor == TGSI_PROCESSOR_VERTEX)
    {
        fill_in_vs_inputs(sobj, cd);
        fill_in_vs_outputs(sobj, cd);
    } else if(cd->processor == TGSI_PROCESSOR_FRAGMENT) {
        fill_in_ps_inputs(sobj, cd);
        fill_in_ps_outputs(sobj, cd);
    }
    *out = sobj;
    FREE(cd);
    return 0;
}

extern const char *tgsi_swizzle_names[];
void etna_dump_shader_object(const struct etna_shader_object *sobj)
{
    if(sobj->processor == TGSI_PROCESSOR_VERTEX)
    {
        printf("VERT\n");
    } else {
        printf("FRAG\n");
    }
    for(int x=0; x<sobj->code_size/4; ++x)
    {
        printf("| %08x %08x %08x %08x\n", sobj->code[x*4+0], sobj->code[x*4+1], sobj->code[x*4+2], sobj->code[x*4+3]);
    }
    printf("num temps: %i\n", sobj->num_temps);
    printf("num const: %i\n", sobj->const_size);
    printf("immediates:\n");
    for(int idx=0; idx<sobj->imm_size; ++idx)
    {
        printf(" [%i].%s = %f (0x%08x)\n", (idx+sobj->imm_base)/4, tgsi_swizzle_names[idx%4],
                *((float*)&sobj->imm_data[idx]), sobj->imm_data[idx]);
    }
    printf("inputs:\n");
    for(int idx=0; idx<sobj->num_inputs; ++idx)
    {
        printf(" [%i] name=%s index=%i pa=%08x comps=%i\n",
                sobj->inputs[idx].reg,
                tgsi_semantic_names[sobj->inputs[idx].semantic.Name], sobj->inputs[idx].semantic.Index,
                sobj->inputs[idx].pa_attributes, sobj->inputs[idx].num_components);
    }
    printf("outputs:\n");
    for(int idx=0; idx<sobj->num_outputs; ++idx)
    {
        printf(" [%i] name=%s index=%i pa=%08x comps=%i\n",
                sobj->outputs[idx].reg,
                tgsi_semantic_names[sobj->outputs[idx].semantic.Name], sobj->outputs[idx].semantic.Index,
                sobj->outputs[idx].pa_attributes, sobj->outputs[idx].num_components);
    }
    printf("special:\n");
    if(sobj->processor == TGSI_PROCESSOR_VERTEX)
    {
        printf("  vs_pos_out_reg=%i\n", sobj->vs_pos_out_reg);
        printf("  vs_pointsize_out_reg=%i\n", sobj->vs_pointsize_out_reg);
        printf("  vs_load_balancing=0x%08x\n", sobj->vs_load_balancing);
    } else {
        printf("  ps_color_out_reg=%i\n", sobj->ps_color_out_reg);
        printf("  ps_depth_out_reg=%i\n", sobj->ps_depth_out_reg);
    }
    printf("  input_count_unk8=0x%08x\n", sobj->input_count_unk8);
}

void etna_destroy_shader_object(struct etna_shader_object *sobj)
{
    if(sobj != NULL)
    {
        FREE(sobj->code);
        FREE(sobj->imm_data);
        FREE(sobj->output_per_semantic_list);
        FREE(sobj);
    }
}

int etna_link_shader_objects(struct etna_shader_link_info *info, const struct etna_shader_object *vs, const struct etna_shader_object *fs)
{
    /* For each fs input we need to find the associated ps input, which can be found by matching on
     * semantic name and index.
     * A binary search can be used because the vs outputs are sorted by semantic in fill_in_vs_outputs.
     */
    assert(fs->num_inputs < ETNA_NUM_INPUTS);
    for(int idx=0; idx<fs->num_inputs; ++idx)
    {
        struct tgsi_declaration_semantic semantic = fs->inputs[idx].semantic;
        if(semantic.Name == TGSI_SEMANTIC_PCOORD)
        {
            info->varyings_vs_reg[idx] = 0; /* replaced by point coord -- doesn't matter */
            continue;
        }
        struct etna_shader_inout *match = NULL;
        if(semantic.Index < vs->output_count_per_semantic[semantic.Name])
        {
            match = vs->output_per_semantic[semantic.Name][semantic.Index];
        }
        if(match == NULL)
            return 1; /* not found -- link error */
        info->varyings_vs_reg[idx] = match->reg;
    }
    return 0;
}

