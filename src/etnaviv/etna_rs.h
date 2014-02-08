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
#ifndef H_ETNA_RS
#define H_ETNA_RS
#include <etnaviv/viv.h>
#include <etnaviv/etna.h>

struct rs_state
{
    uint8_t downsample_x:1; // Downsample in x direction
    uint8_t downsample_y:1; // Downsample in y direction

    uint8_t source_format; // RS_FORMAT_XXX
    uint8_t source_tiling; // ETNA_LAYOUT_XXX
    uint8_t dest_tiling;   // ETNA_LAYOUT_XXX
    uint8_t dest_format;  // RS_FORMAT_XXX
    uint8_t swap_rb;
    uint8_t flip;
    uint32_t source_addr[2];
    uint32_t source_stride;
    uint32_t dest_addr[2];
    uint32_t dest_stride;
    uint16_t width; // source width
    uint16_t height; // source height
    uint32_t dither[2];
    uint32_t clear_bits;
    uint32_t clear_mode; // VIVS_RS_CLEAR_CONTROL_MODE_XXX
    uint32_t clear_value[4];
    uint8_t aa;
    uint8_t endian_mode; // ENDIAN_MODE_XXX
};

/* treat this as opaque structure */
struct compiled_rs_state
{
    uint32_t RS_CONFIG;
    uint32_t RS_SOURCE_ADDR;
    uint32_t RS_SOURCE_STRIDE;
    uint32_t RS_DEST_ADDR;
    uint32_t RS_DEST_STRIDE;
    uint32_t RS_WINDOW_SIZE;
    uint32_t RS_DITHER[2];
    uint32_t RS_CLEAR_CONTROL;
    uint32_t RS_FILL_VALUE[4];
    uint32_t RS_EXTRA_CONFIG;
    uint32_t RS_PIPE_SOURCE_ADDR[2];    /* TODO is there hardware with more then 2 pipes? */
    uint32_t RS_PIPE_DEST_ADDR[2];      /* TODO is there hardware with more then 2 pipes? */
    uint32_t RS_PIPE_OFFSET[2];         /* TODO is there hardware with more then 2 pipes? */
};

/* Flush RS? warm up RS on aux render target */
void etna_warm_up_rs(struct etna_ctx *cmdbuf, viv_addr_t aux_rt_physical, viv_addr_t aux_rt_ts_physical);

/* compile RS state struct */
void etna_compile_rs_state(struct etna_ctx *restrict ctx, struct compiled_rs_state *cs, const struct rs_state *rs);

/* submit compiled RS state */
void etna_submit_rs_state(struct etna_ctx *restrict ctx, const struct compiled_rs_state *cs);

#endif

