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
/* Interface to shader compiler */
#ifndef H_ETNA_COMPILER
#define H_ETNA_COMPILER
#include "etna_internal.h"

#include <stdint.h>
#include "pipe/p_compiler.h"
#include "pipe/p_shader_tokens.h"

/* XXX some of these such as ETNA_MAX_LABELS are pretty arbitrary limits, may be better to switch
 * to dynamic allocation at some point.
 */
#define ETNA_MAX_TEMPS (64) /* max temp register count of all Vivante hw */
#define ETNA_MAX_TOKENS (2048)
#define ETNA_MAX_IMM (1024)  /* max const+imm in 32-bit words */
#define ETNA_MAX_DECL (2048)  /* max declarations */
#define ETNA_MAX_DEPTH (32)
#define ETNA_MAX_LABELS (64)
#define ETNA_MAX_INSTRUCTIONS (2048)

struct etna_pipe_specs;

/* compiler output per input/output */
struct etna_shader_inout
{
    int reg; /* native register */
    struct tgsi_declaration_semantic semantic; /* tgsi semantic name and index */
    int num_components;
    /* varyings */
    uint32_t pa_attributes; /* PA_SHADER_ATTRIBUTES */
};

/* shader object, for linking */
struct etna_shader_object
{
    uint processor; /* TGSI_PROCESSOR_... */
    uint32_t code_size; /* code size in uint32 words */
    uint32_t *code;
    unsigned num_temps;

    uint32_t const_base; /* base of constants (in 32 bit units) */
    uint32_t const_size; /* size of constants, also base of immediates (in 32 bit units) */
    uint32_t imm_base; /* base of immediates (in 32 bit units) */
    uint32_t imm_size; /* size of immediates (in 32 bit units) */
    uint32_t *imm_data;

    /* inputs (for linking)
     *   for fs, the inputs must be in register 1..N */
    unsigned num_inputs;
    struct etna_shader_inout inputs[ETNA_NUM_INPUTS];

    /* outputs (for linking) */
    unsigned num_outputs;
    struct etna_shader_inout outputs[ETNA_NUM_INPUTS];
    /* index into outputs (for linking) */
    int output_count_per_semantic[TGSI_SEMANTIC_COUNT];
    struct etna_shader_inout **output_per_semantic_list; /* list of pointers to outputs */
    struct etna_shader_inout **output_per_semantic[TGSI_SEMANTIC_COUNT];

    /* special outputs (vs only) */
    int vs_pos_out_reg; /* VS position output */
    int vs_pointsize_out_reg; /* VS point size output */
    uint32_t vs_load_balancing;

    /* special outputs (ps only) */
    int ps_color_out_reg; /* color output register */
    int ps_depth_out_reg; /* depth output register */

    /* unknown input property (XX_INPUT_COUNT, field UNK8) */
    uint32_t input_count_unk8;
};

struct etna_shader_link_info
{
    /* each PS input is annotated with the VS output reg */
    unsigned varyings_vs_reg[ETNA_NUM_INPUTS];
};

/* Entry point to compiler.
 * Returns non-zero if compilation fails.
 */
int etna_compile_shader_object(const struct etna_pipe_specs *specs, const struct tgsi_token *tokens,
        struct etna_shader_object **out);

/* Debug dump of shader object */
void etna_dump_shader_object(const struct etna_shader_object *sobj);

/* Link two shader objects together, annotates each PS input with the VS
 * output register. Returns non-zero if the linking fails.
 */
int etna_link_shader_objects(struct etna_shader_link_info *info, const struct etna_shader_object *vs, const struct etna_shader_object *fs);

/* Destroy a previously allocated shader object */
void etna_destroy_shader_object(struct etna_shader_object *obj);

#endif

