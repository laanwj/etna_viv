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
/* Resolve tests
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>

#include <errno.h>

#include "etnaviv/common.xml.h"
#include "etnaviv/state.xml.h"
#include "etnaviv/state_3d.xml.h"
#include "etnaviv/cmdstream.xml.h"

#include "write_bmp.h"
#include "fbdemos.h"
#include "etnaviv/viv.h"
#include "etnaviv/etna.h"
#include "etnaviv/etna_fb.h"
#include "etnaviv/etna_util.h"
#include "etnaviv/etna_rs.h"
#include "etnaviv/etna_bo.h"

#include "etna_pipe.h"
#include "esTransform.h"

int main(int argc, char **argv)
{
    int rv;
    int width = 256;
    int height = 256;
    int padded_width, padded_height;

    struct fbdemos_scaffold *fbs = 0;
    fbdemo_init(&fbs);
    struct viv_conn *conn = fbs->conn;

    padded_width = etna_align_up(width, 64);
    padded_height = etna_align_up(height, 64);

    printf("padded_width %i padded_height %i\n", padded_width, padded_height);

    struct etna_bo *rt1 = 0; /* main render target */
    struct etna_bo *rt2 = 0; /* main render target */
    struct etna_bo *rt_ts = 0; /* tile status for main render target */
    struct etna_bo *z = 0; /* depth for main render target */
    struct etna_bo *z_ts = 0; /* depth ts for main render target */

    size_t rt_size = padded_width * padded_height * 4;
    size_t rt_ts_size = etna_align_up((padded_width * padded_height * 4)/0x100, 0x100);
    size_t z_size = padded_width * padded_height * 2;
    size_t z_ts_size = etna_align_up((padded_width * padded_height * 2)/0x100, 0x100);

    if((rt1=etna_bo_new(conn, rt_size, DRM_ETNA_GEM_TYPE_RT))==NULL ||
       (rt2=etna_bo_new(conn, rt_size, DRM_ETNA_GEM_TYPE_RT))==NULL ||
       (rt_ts=etna_bo_new(conn, rt_ts_size, DRM_ETNA_GEM_TYPE_TS))==NULL ||
       (z=etna_bo_new(conn, z_size, DRM_ETNA_GEM_TYPE_ZS))==NULL ||
       (z_ts=etna_bo_new(conn, z_ts_size, DRM_ETNA_GEM_TYPE_TS))==NULL
       )
    {
        fprintf(stderr, "Error allocating video memory\n");
        exit(1);
    }

    struct etna_ctx *ctx = 0;
    if(etna_create(conn, &ctx) != ETNA_OK)
    {
        printf("Unable to create context\n");
        exit(1);
    }

    memset(etna_bo_map(rt_ts), 0x55, rt_ts->size);  // Pattern: cleared
    //memset(etna_bo_map(rt_ts), 0xAA, rt_ts->size);  // Pattern: weird pattern fill
    //memset(etna_bo_map(rt_ts), 0x00, rt_ts->size);  // Pattern: filled in (nothing to do)
    //memset(etna_bo_map(rt_ts), 0xFF, rt_ts->size);  // Pattern: weird pattern fill
    //
    /* pattern in:
     * <32b> [<32b>] <32b> [<32b>] ...  delete odd groups of 32 bytes
     *  ->
     * <32b> <32b> ...
     */
    {
        int src_height = 16;
        for(int x=0; x<(src_height/4)*64; ++x)
        {
            *(uint16_t*)(rt1->logical + 2*x) = x;
        }
        printf("In:\n");
        for(int x=0; x<(src_height/4); ++x) /* print values, per tile */
        {
            printf(" ");
            for(int y=0; y<16; ++y)
                printf("%i ", *(uint16_t*)(rt1->logical + 64*x + 2*y));
            printf("\n");
        }
        printf("\n");
        memset(rt2->logical, 0, rt_size);
        //int format = RS_FORMAT_R5G6B5;
        //int format = 0x9;
        int format = 0x6;
        bool tiled1 = true;
        bool tiled2 = false;
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG, 0);
        etna_set_state(ctx, VIVS_RS_CONFIG,
                VIVS_RS_CONFIG_SOURCE_FORMAT(format) |
                (tiled1?VIVS_RS_CONFIG_SOURCE_TILED:0) |
                VIVS_RS_CONFIG_DEST_FORMAT(format) |
                (tiled2?VIVS_RS_CONFIG_DEST_TILED:0));
        etna_set_state(ctx, VIVS_RS_SOURCE_STRIDE, 64);
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, 64);
        etna_set_state(ctx, VIVS_RS_DITHER(0), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_DITHER(1), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 0); /* no AA, no endian switch */
        etna_set_state(ctx, VIVS_RS_SOURCE_ADDR, rt1->address);
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, rt2->address);
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE,
                VIVS_RS_WINDOW_SIZE_HEIGHT(2) | /* height in 4x4 tiles */
                VIVS_RS_WINDOW_SIZE_WIDTH(16)); /* width spans one 16bpp 4x4 tile = 32 bytes */
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);

        etna_finish(ctx);

        printf("Out:\n");
        for(int x=0; x<(src_height/4); ++x) /* print values, per tile */
        {
            printf(" ");
            for(int y=0; y<16; ++y)
                printf("%i ", *(uint16_t*)(rt2->logical + 32*x + 2*y));
            printf("\n");
        }
        printf("\n");
    }
    /* tile->nontiled swizzle ("rotation")
     * (width 16)
     * <8b_0> <8b_1> <8b_2> <8b_3> <8b_4> <8b_5> <8b_6> <8b_7> <8b_8> <8b_9> <8b_10> <8b_11> <8b_12> <8b_13> <8b_14> <8b_15>
     *  ->
     * row0 <8b_0> <8b_4> <8b_8> <8b_12>
     * row1 <8b_1> <8b_5> <8b_9> <8b_13>
     * row2 <8b_2> <8b_6> <8b_10> <8b_14>
     * row3 <8b_3> <8b_7> <8b_11> <8b_15>
     *
     * Swizzle other way around
     *
     * row0 <8b_0> <8b_1> <8b_2> <8b_3> <8b_4> <8b_5> <8b_6> <8b_7> <8b_8> <8b_9> <8b_10> <8b_11> <8b_12> <8b_13> <8b_14> <8b_15>
     * row1 <8b_16> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...>
     * row2 <8b_32> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...>
     * row3 <8b_48> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...> <...>
     * ->
     * tile0 <8b_0> <8b_16> <8b_32> <8b_48>
     * tile1 <8b_1> <8b_17> <8b_33> <8b_49>
     * tile2 <8b_2> <8b_18> <8b_34> <8b_50>
     * tile3 <8b_3> <8b_19> <8b_35> <8b_51>
     * ...
     */
    /* Pass 1
     *
     * <16b_0> <16b_1> [<16b_2> <16b_3>] <16b_4> <16b_5> [<16b_6> <16b_7>] <16b_8> <16b_9> [<16b_10> <16b_11>] <16b_12> <16b_13> [<16b_14> <16b_15>]
     *  ->
     * row0  <16b_0> <16b_4> <16b_8> <16b_12>
     * row1  <16b_1> <16b_5> <16b_9> <16b_13>
     * row2 [<16b_2> <16b_6> <16b_10> <16b_14>]
     * row3 [<16b_3> <16b_7> <16b_11> <16b_15>]
     *
     * drop row2 and row3
     * how to put it back?
     *
     *     <16b_0> <16b_2> <16b_4> <16b_6> <16b_1> <16b_3> <16b_5> <16b_7>
     *     ->
     *     <16b_0> <16b_1> <16b_2> <16b_3> <16b_4> <16b_5> <16b_6> <16b_7>
     *
     */

#if 0
    uint32_t pixelfmt = RS_FORMAT_X8R8G8B8;
    bool supertiled = true;
    bool tiled = true;
    uint32_t stride = 0;
    if(tiled)
    {
        stride = (padded_width * 4 * 4) | (supertiled?VIVS_RS_DEST_STRIDE_TILING:0);
    } else {
        stride = (padded_width * 4) | (supertiled?VIVS_RS_DEST_STRIDE_TILING:0);
    }
    {
        etna_set_state(ctx, VIVS_GL_VERTEX_ELEMENT_CONFIG, 0x1);
        etna_set_state(ctx, VIVS_RA_CONTROL, 0x1);

        etna_set_state(ctx, VIVS_PA_W_CLIP_LIMIT, 0x34000001);
        etna_set_state(ctx, VIVS_PA_SYSTEM_MODE, 0x11);
        etna_set_state(ctx, VIVS_PA_CONFIG, ETNA_MASKED_BIT(VIVS_PA_CONFIG_UNK22, 0));

        etna_set_state(ctx, VIVS_SE_CONFIG, 0x0);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);

        /* Set up resolve to self */
        etna_set_state(ctx, VIVS_TS_COLOR_CLEAR_VALUE, 0xff7f7f7f);
        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, etna_bo_gpu_address(rt_ts)); /* ADDR_B */
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, etna_bo_gpu_address(rt)); /* ADDR_A */
#if 0   /* don't care about depth, for now */
        etna_set_state(ctx, VIVS_TS_DEPTH_CLEAR_VALUE, 0xffffffff);
        etna_set_state(ctx, VIVS_TS_DEPTH_STATUS_BASE, etna_bo_gpu_address(z_ts)); /* ADDR_D */
        etna_set_state(ctx, VIVS_TS_DEPTH_SURFACE_BASE, etna_bo_gpu_address(z)); /* ADDR_C */
#endif

        etna_set_state(ctx, VIVS_TS_MEM_CONFIG,
                VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR
                /*VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
                VIVS_TS_MEM_CONFIG_DEPTH_16BPP |
                VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION*/);
        etna_set_state(ctx, VIVS_RS_CONFIG,
                VIVS_RS_CONFIG_SOURCE_FORMAT(pixelfmt) |
                (tiled?VIVS_RS_CONFIG_SOURCE_TILED:0) |
                VIVS_RS_CONFIG_DEST_FORMAT(pixelfmt) |
                (tiled?VIVS_RS_CONFIG_DEST_TILED:0));
        etna_set_state(ctx, VIVS_RS_SOURCE_STRIDE, stride);
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, stride);
        etna_set_state(ctx, VIVS_RS_DITHER(0), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_DITHER(1), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 0); /* no AA, no endian switch */
        etna_set_state(ctx, VIVS_RS_SOURCE_ADDR, etna_bo_gpu_address(rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, etna_bo_gpu_address(rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE,
                VIVS_RS_WINDOW_SIZE_HEIGHT(padded_height) |
                VIVS_RS_WINDOW_SIZE_WIDTH(padded_width));
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);

        etna_set_state(ctx, VIVS_TS_FLUSH_CACHE, VIVS_TS_FLUSH_CACHE_FLUSH | VIVS_GL_FLUSH_CACHE_COLOR);

        /* Clear part using normal (not fast) clear */
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG, 0);
        etna_set_state_multi(ctx, VIVS_RS_DITHER(0), 2, (uint32_t[]){0xffffffff, 0xffffffff});
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE,
                VIVS_RS_WINDOW_SIZE_HEIGHT(0x100) |
                VIVS_RS_WINDOW_SIZE_WIDTH(0x100));
        etna_set_state(ctx, VIVS_RS_FILL_VALUE(0), 0xffff0000);
        etna_set_state(ctx, VIVS_RS_FILL_VALUE(1), 0xff00ff00);
        etna_set_state(ctx, VIVS_RS_FILL_VALUE(2), 0xff0000ff);
        etna_set_state(ctx, VIVS_RS_FILL_VALUE(3), 0xffff00ff);
        etna_set_state(ctx, VIVS_RS_SOURCE_ADDR, 0); /* fill disregards source anyway */
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, etna_bo_gpu_address(rt) + 64*64*4); /* Offset one entire 64*64 tile. Interesting things happen if only a partial tile is offset. */
        /* Pure FILL_VALUE(0) */
        //etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_ENABLED1 | VIVS_RS_CLEAR_CONTROL_BITS(0xffff));
        /* Vertical line pattern */
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_ENABLED4 | VIVS_RS_CLEAR_CONTROL_BITS(0xffff));
        /* Same as ENABLED2 */
        //etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_ENABLED4_2 | VIVS_RS_CLEAR_CONTROL_BITS(0xffff));

        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 0);
        etna_set_state(ctx, VIVS_RS_KICKER,
                0xbeebbeeb);

        etna_finish(ctx);

#if 0
        /* manually fill, to figure out tiling pattern */
        void *rt_map = etna_bo_map(rt);
        for(int x=0; x<16384/4; ++x)
        {
            int a = (x & 0x3F) << 2;
            int b = ((x >> 3) & 0x3F) << 2;
            int c = ((x >> 6) & 0x3F) << 2;
            ((uint32_t*)(rt_map + 16384*6))[x] = (a & 0xFF) | ((b & 0xFF) << 8) | ((c & 0xFF) << 16);
            printf("%08x\n", (a & 0xFF) | ((b & 0xFF) << 8) | ((c & 0xFF) << 16));
        }
#endif

        /* Copy image to screen */
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG, 0);
        etna_set_state(ctx, VIVS_RS_CONFIG,
                VIVS_RS_CONFIG_SOURCE_FORMAT(pixelfmt) |
                (tiled?VIVS_RS_CONFIG_SOURCE_TILED:0) |
                VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_X8R8G8B8) |
                VIVS_RS_CONFIG_SWAP_RB);
        etna_set_state(ctx, VIVS_RS_SOURCE_STRIDE, stride);
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, stride);
        etna_set_state(ctx, VIVS_RS_DITHER(0), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_DITHER(1), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG,
                0); /* no AA, no endian switch */
        etna_set_state(ctx, VIVS_RS_SOURCE_ADDR, etna_bo_gpu_address(rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, fb.physical[backbuffer]); /* ADDR_J */
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE,
                VIVS_RS_WINDOW_SIZE_HEIGHT(height) |
                VIVS_RS_WINDOW_SIZE_WIDTH(width));
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);
        etna_finish(ctx);

        /* switch buffers */
        fb_set_buffer(&fb, backbuffer);
        backbuffer = 1-backbuffer;
    }
#endif

    etna_free(ctx);
    viv_close(conn);
    return 0;
}
