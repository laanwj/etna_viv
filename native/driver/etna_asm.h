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
/* Utilities for generating low-level ISA instructions */
#ifndef H_ETNA_ASM
#define H_ETNA_ASM
#include <stdint.h>

/* Size of instruction in 32-bit words */
#define ETNA_INST_SIZE (4)
/* Number of source operands per instruction */
#define ETNA_NUM_SRC (3)

/*** operands ***/

/* destination operand */
struct etna_inst_dst
{
    unsigned use:1;
    unsigned amode:3;
    unsigned reg:7;
    unsigned comps:4; /* INST_COMPS_* */
};

/* texture operand */
struct etna_inst_tex
{
    unsigned id:5;
    unsigned amode:3; /* INST_AMODE_* */
    unsigned swiz:8; /* INST_SWIZ */
};

/* source operand */
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

/*** instruction ***/
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

/**
 * Set field imm of already-assembled instruction.
 * This is used for filling in jump destinations in a separate pass.
 */
int etna_assemble_set_imm(uint32_t *out, uint32_t imm);

#endif

