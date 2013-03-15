#include "etna_asm.h"

#include "isa.xml.h"

int etna_assemble(uint32_t *out, const struct etna_inst *inst)
{
    if(inst->imm && inst->src[2].use)
        return 1; /* cannot have both src2 and imm */
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

