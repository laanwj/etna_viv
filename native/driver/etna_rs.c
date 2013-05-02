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
#include "etna_rs.h"
#include "etna_translate.h"

#include <etnaviv/etna.h>
#include <etnaviv/state.xml.h>
#include <etnaviv/state_3d.xml.h>

/* Some kind of RS flush, used in the older drivers */
void etna_warm_up_rs(struct etna_ctx *cmdbuf, viv_addr_t aux_rt_physical, viv_addr_t aux_rt_ts_physical)
{
    etna_set_state(cmdbuf, VIVS_TS_COLOR_STATUS_BASE, aux_rt_ts_physical); /* ADDR_G */
    etna_set_state(cmdbuf, VIVS_TS_COLOR_SURFACE_BASE, aux_rt_physical); /* ADDR_F */
    etna_set_state(cmdbuf, VIVS_RS_FLUSH_CACHE, VIVS_RS_FLUSH_CACHE_FLUSH);
    etna_set_state(cmdbuf, VIVS_RS_CONFIG,  /* wut? */
            VIVS_RS_CONFIG_SOURCE_FORMAT(RS_FORMAT_A8R8G8B8) |
            VIVS_RS_CONFIG_SOURCE_TILED |
            VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_R5G6B5) |
            VIVS_RS_CONFIG_DEST_TILED);
    etna_set_state(cmdbuf, VIVS_RS_SOURCE_ADDR, aux_rt_physical); /* ADDR_F */
    etna_set_state(cmdbuf, VIVS_RS_SOURCE_STRIDE, 0x400);
    etna_set_state(cmdbuf, VIVS_RS_DEST_ADDR, aux_rt_physical); /* ADDR_F */
    etna_set_state(cmdbuf, VIVS_RS_DEST_STRIDE, 0x400);
    etna_set_state(cmdbuf, VIVS_RS_WINDOW_SIZE,
            VIVS_RS_WINDOW_SIZE_HEIGHT(4) |
            VIVS_RS_WINDOW_SIZE_WIDTH(16));
    etna_set_state(cmdbuf, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
    etna_set_state(cmdbuf, VIVS_RS_KICKER, 0xbeebbeeb);
}

/* compile RS state struct */
#define SET_STATE(addr, value) cs->addr = (value)
#define SET_STATE_FIXP(addr, value) cs->addr = (value)
#define SET_STATE_F32(addr, value) cs->addr = f32_to_u32(value)
void etna_compile_rs_state(struct compiled_rs_state *cs, const struct rs_state *rs)
{
    /* TILED and SUPERTILED layout have their strides multiplied with 4 in RS */
    unsigned source_stride_shift = (rs->source_tiling != ETNA_LAYOUT_LINEAR) ? 2 : 0;
    unsigned dest_stride_shift = (rs->dest_tiling != ETNA_LAYOUT_LINEAR) ? 2 : 0;
    /* TODO could just pre-generate command buffer, would simply submit to one memcpy */
    SET_STATE(RS_CONFIG, VIVS_RS_CONFIG_SOURCE_FORMAT(rs->source_format) |
                            (rs->downsample_x?VIVS_RS_CONFIG_DOWNSAMPLE_X:0) |
                            (rs->downsample_y?VIVS_RS_CONFIG_DOWNSAMPLE_Y:0) |
                            ((rs->source_tiling&1)?VIVS_RS_CONFIG_SOURCE_TILED:0) |
                            VIVS_RS_CONFIG_DEST_FORMAT(rs->dest_format) |
                            ((rs->dest_tiling&1)?VIVS_RS_CONFIG_DEST_TILED:0) |
                            ((rs->swap_rb)?VIVS_RS_CONFIG_SWAP_RB:0) |
                            ((rs->flip)?VIVS_RS_CONFIG_FLIP:0));
    SET_STATE(RS_SOURCE_ADDR, rs->source_addr);
    SET_STATE(RS_SOURCE_STRIDE, (rs->source_stride << source_stride_shift) | ((rs->source_tiling&2)?VIVS_RS_SOURCE_STRIDE_TILING:0));
    SET_STATE(RS_DEST_ADDR, rs->dest_addr);
    SET_STATE(RS_DEST_STRIDE, (rs->dest_stride << dest_stride_shift) | ((rs->dest_tiling&2)?VIVS_RS_DEST_STRIDE_TILING:0));
    SET_STATE(RS_WINDOW_SIZE, VIVS_RS_WINDOW_SIZE_WIDTH(rs->width) | VIVS_RS_WINDOW_SIZE_HEIGHT(rs->height));
    SET_STATE(RS_DITHER[0], rs->dither[0]);
    SET_STATE(RS_DITHER[1], rs->dither[1]);
    SET_STATE(RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_BITS(rs->clear_bits) | rs->clear_mode);
    SET_STATE(RS_FILL_VALUE[0], rs->clear_value[0]);
    SET_STATE(RS_FILL_VALUE[1], rs->clear_value[1]);
    SET_STATE(RS_FILL_VALUE[2], rs->clear_value[2]);
    SET_STATE(RS_FILL_VALUE[3], rs->clear_value[3]);
    SET_STATE(RS_EXTRA_CONFIG, VIVS_RS_EXTRA_CONFIG_AA(rs->aa) | VIVS_RS_EXTRA_CONFIG_ENDIAN(rs->endian_mode));
}

/* submit RS state, without any processing and no dependence on context 
 * except TS if this is a source-to-destination blit. */
void etna_submit_rs_state(struct etna_ctx *restrict ctx, const struct compiled_rs_state *cs)
{
    etna_reserve(ctx, 22);
    /*0 */ ETNA_EMIT_LOAD_STATE(ctx, VIVS_RS_CONFIG>>2, 5, 0);
    /*1 */ ETNA_EMIT(ctx, cs->RS_CONFIG);
    /*2 */ ETNA_EMIT(ctx, cs->RS_SOURCE_ADDR);
    /*3 */ ETNA_EMIT(ctx, cs->RS_SOURCE_STRIDE);
    /*4 */ ETNA_EMIT(ctx, cs->RS_DEST_ADDR);
    /*5 */ ETNA_EMIT(ctx, cs->RS_DEST_STRIDE);
    /*6 */ ETNA_EMIT_LOAD_STATE(ctx, VIVS_RS_WINDOW_SIZE>>2, 1, 0);
    /*7 */ ETNA_EMIT(ctx, cs->RS_WINDOW_SIZE);
    /*8 */ ETNA_EMIT_LOAD_STATE(ctx, VIVS_RS_DITHER(0)>>2, 2, 0);
    /*9 */ ETNA_EMIT(ctx, cs->RS_DITHER[0]);
    /*10*/ ETNA_EMIT(ctx, cs->RS_DITHER[1]);
    /*11*/ ETNA_EMIT(ctx, 0xbabb1e); /* pad */
    /*12*/ ETNA_EMIT_LOAD_STATE(ctx, VIVS_RS_CLEAR_CONTROL>>2, 5, 0);
    /*13*/ ETNA_EMIT(ctx, cs->RS_CLEAR_CONTROL);
    /*14*/ ETNA_EMIT(ctx, cs->RS_FILL_VALUE[0]);
    /*15*/ ETNA_EMIT(ctx, cs->RS_FILL_VALUE[1]);
    /*16*/ ETNA_EMIT(ctx, cs->RS_FILL_VALUE[2]);
    /*17*/ ETNA_EMIT(ctx, cs->RS_FILL_VALUE[3]);
    /*18*/ ETNA_EMIT_LOAD_STATE(ctx, VIVS_RS_EXTRA_CONFIG>>2, 1, 0);
    /*19*/ ETNA_EMIT(ctx, cs->RS_EXTRA_CONFIG);
    /*20*/ ETNA_EMIT_LOAD_STATE(ctx, VIVS_RS_KICKER>>2, 1, 0);
    /*21*/ ETNA_EMIT(ctx, 0xbeebbeeb);
}

