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
#include "etna_asm.h"
#include "etna_debug.h"

/** An instruction can only read from one distinct uniform.
 * This function verifies this property and returns true if the instruction
 * is deemed correct and false otherwise.
 */
static bool check_uniforms(const struct etna_inst *inst)
{
    unsigned uni_rgroup = -1;
    unsigned uni_reg = -1;
    bool conflict = false;
    for(int src=0; src<ETNA_NUM_SRC; ++src)
    {
        if(etna_rgroup_is_uniform(inst->src[src].rgroup))
        {
            if(uni_reg == -1) /* first uniform used */
            {
                uni_rgroup = inst->src[src].rgroup;
                uni_reg = inst->src[src].reg;
            } else { /* second or later; check that it is a re-use */
                if(uni_rgroup != inst->src[src].rgroup ||
                   uni_reg != inst->src[src].reg)
                {
                    conflict = true;
                }
            }
        }
    }
    return !conflict;
}

int etna_assemble(uint32_t *out, const struct etna_inst *inst)
{
    if(inst->imm && inst->src[2].use)
        return 1; /* cannot have both src2 and imm */

    if(!check_uniforms(inst))
    {
        BUG("error: generating instruction that accesses two different uniforms");
    }

    out[0] = VIV_ISA_WORD_0_OPCODE(inst->opcode) |
             VIV_ISA_WORD_0_COND(inst->cond) |
             (inst->sat ? VIV_ISA_WORD_0_SAT : 0) |
             (inst->dst.use ? VIV_ISA_WORD_0_DST_USE : 0) |
             VIV_ISA_WORD_0_DST_AMODE(inst->dst.amode) |
             VIV_ISA_WORD_0_DST_REG(inst->dst.reg) |
             VIV_ISA_WORD_0_DST_COMPS(inst->dst.comps) |
             VIV_ISA_WORD_0_TEX_ID(inst->tex.id);
    out[1] = VIV_ISA_WORD_1_TEX_AMODE(inst->tex.amode) |
             VIV_ISA_WORD_1_TEX_SWIZ(inst->tex.swiz) |
             (inst->src[0].use ? VIV_ISA_WORD_1_SRC0_USE : 0) |
             VIV_ISA_WORD_1_SRC0_REG(inst->src[0].reg) |
             VIV_ISA_WORD_1_SRC0_SWIZ(inst->src[0].swiz) |
             (inst->src[0].neg ? VIV_ISA_WORD_1_SRC0_NEG : 0) |
             (inst->src[0].abs ? VIV_ISA_WORD_1_SRC0_ABS : 0);
    out[2] = VIV_ISA_WORD_2_SRC0_AMODE(inst->src[0].amode) |
             VIV_ISA_WORD_2_SRC0_RGROUP(inst->src[0].rgroup) |
             (inst->src[1].use ? VIV_ISA_WORD_2_SRC1_USE : 0) |
             VIV_ISA_WORD_2_SRC1_REG(inst->src[1].reg) |
             VIV_ISA_WORD_2_SRC1_SWIZ(inst->src[1].swiz) |
             (inst->src[1].neg ? VIV_ISA_WORD_2_SRC1_NEG : 0) |
             (inst->src[1].abs ? VIV_ISA_WORD_2_SRC1_ABS : 0) |
             VIV_ISA_WORD_2_SRC1_AMODE(inst->src[1].amode);
    out[3] = VIV_ISA_WORD_3_SRC1_RGROUP(inst->src[1].rgroup) |
             (inst->src[2].use ? VIV_ISA_WORD_3_SRC2_USE : 0) |
             VIV_ISA_WORD_3_SRC2_REG(inst->src[2].reg) |
             VIV_ISA_WORD_3_SRC2_SWIZ(inst->src[2].swiz) |
             (inst->src[2].neg ? VIV_ISA_WORD_3_SRC2_NEG : 0) |
             (inst->src[2].abs ? VIV_ISA_WORD_3_SRC2_ABS : 0) |
             VIV_ISA_WORD_3_SRC2_AMODE(inst->src[2].amode) |
             VIV_ISA_WORD_3_SRC2_RGROUP(inst->src[2].rgroup);
    out[3] |= VIV_ISA_WORD_3_SRC2_IMM(inst->imm);
    return 0;
}

int etna_assemble_set_imm(uint32_t *out, uint32_t imm)
{
    out[3] |= VIV_ISA_WORD_3_SRC2_IMM(imm);
    return 0;
}

