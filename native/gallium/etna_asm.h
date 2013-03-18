#ifndef H_ETNA_ASM
#define H_ETNA_ASM
#include <stdint.h>

#define ETNA_INST_SIZE (4)
#define ETNA_NUM_SRC (3)

/* operands */
struct etna_inst_dst
{
    unsigned use:1;
    unsigned amode:3;
    unsigned reg:7;
    unsigned comps:4; /* INST_COMPS_* */
};

struct etna_inst_tex
{
    unsigned id:5;
    unsigned amode:3; /* INST_AMODE_* */
    unsigned swiz:8; /* INST_SWIZ */
};

struct etna_inst_src
{
    unsigned use:1;
    unsigned reg:9;
    unsigned swiz:8;   /* INST_SWIZ */
    unsigned neg:1;
    unsigned abs:1;
    unsigned amode:3;  /* INST_AMODE_* */
    unsigned rgroup:3; /* INST_RGROUP_* */
};

/* instruction */
struct etna_inst 
{
    uint8_t opcode; /* INST_OPCODE_* */
    unsigned cond:5; /* INST_CONDITION_* */
    unsigned sat:1;
    struct etna_inst_dst dst; /* destination operand */
    struct etna_inst_tex tex; /* texture operand */
    struct etna_inst_src src[ETNA_NUM_SRC]; /* source operand */
    unsigned imm;  /* takes place of src[2] for BRANCH/CALL */
};

/**
 * Build vivante instruction from structure: 
 *  opcode, cond, sat, dst_use, dst_amode, 
 *  dst_reg, dst_comps, tex_id, tex_amode, tex_swiz,
 *  src[0-2]_reg, use, swiz, neg, abs, amode, rgroup,
 *  imm
 *
 * Return 0 if succesful, and a non-zero
 * value otherwise.
 */
int etna_assemble(uint32_t *out, const struct etna_inst *inst);

/* set imm of already-assembled instruction */
int etna_assemble_set_imm(uint32_t *out, uint32_t imm);

#endif

