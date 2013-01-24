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

#include "etna/state.xml.h"
#include "etna/cmdstream.xml.h"
#include "write_bmp.h"
#include "viv.h"
#include "etna.h"
#include "etna_state.h"
#include "etna_rs.h"
#include "etna_fb.h"
#include "etna_mem.h"

#include "esUtil.h"

int main(int argc, char **argv)
{
    int rv;
    int width = 256;
    int height = 256;
    int padded_width, padded_height;
    int backbuffer = 0;
    
    fb_info fb;
    rv = fb_open(0, &fb);
    if(rv!=0)
    {
        exit(1);
    }
    width = fb.fb_var.xres;
    height = fb.fb_var.yres;
    padded_width = etna_align_up(width, 64);
    padded_height = etna_align_up(height, 64);

    printf("padded_width %i padded_height %i\n", padded_width, padded_height);
    rv = viv_open();
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    printf("Succesfully opened device\n");

    etna_vidmem *rt = 0; /* main render target */
    etna_vidmem *rt_ts = 0; /* tile status for main render target */
    etna_vidmem *z = 0; /* depth for main render target */
    etna_vidmem *z_ts = 0; /* depth ts for main render target */
    etna_vidmem *aux_rt = 0; /* auxilary render target */
    etna_vidmem *aux_rt_ts = 0; /* tile status for auxilary render target */

    size_t rt_size = padded_width * padded_height * 4;
    size_t rt_ts_size = etna_align_up((padded_width * padded_height * 4)/0x100, 0x100);
    size_t z_size = padded_width * padded_height * 2;
    size_t z_ts_size = etna_align_up((padded_width * padded_height * 2)/0x100, 0x100);

    if(etna_vidmem_alloc_linear(&rt, rt_size, gcvSURF_RENDER_TARGET, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_vidmem_alloc_linear(&rt_ts, rt_ts_size, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_vidmem_alloc_linear(&z, z_size, gcvSURF_DEPTH, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_vidmem_alloc_linear(&z_ts, z_ts_size, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_vidmem_alloc_linear(&aux_rt, 0x4000, gcvSURF_RENDER_TARGET, gcvPOOL_SYSTEM, true)!=ETNA_OK ||
       etna_vidmem_alloc_linear(&aux_rt_ts, 0x100, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, true)!=ETNA_OK
       )
    {
        fprintf(stderr, "Error allocating video memory\n");
        exit(1);
    }

    etna_ctx *ctx = 0;
    if(etna_create(&ctx) != ETNA_OK)
    {
        printf("Unable to create context\n");
        exit(1);
    }

    memset(rt_ts->logical, 0x55, rt_ts->size);  // Pattern: cleared
    //memset(rt_ts->logical, 0xAA, rt_ts->size);  // Pattern: weird pattern fill
    //memset(rt_ts->logical, 0x00, rt_ts->size);  // Pattern: filled in (nothing to do)
    //memset(rt_ts->logical, 0xFF, rt_ts->size);  // Pattern: weird pattern fill
    
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

    for(int frame=0; frame<1; ++frame)
    {
        if(frame%50 == 0)
            printf("*** FRAME %i ****\n", frame);
        /* XXX part of this can be put outside the loop, but until we have usable context management
         * this is safest.
         */
        etna_set_state(ctx, VIVS_GL_VERTEX_ELEMENT_CONFIG, 0x1);
        etna_set_state(ctx, VIVS_RA_CONTROL, 0x1);

        etna_set_state(ctx, VIVS_PA_W_CLIP_LIMIT, 0x34000001);
        etna_set_state(ctx, VIVS_PA_SYSTEM_MODE, 0x11);
        etna_set_state(ctx, VIVS_PA_CONFIG, ETNA_MASKED_BIT(VIVS_PA_CONFIG_UNK22, 0));

        etna_set_state(ctx, VIVS_SE_LAST_PIXEL_ENABLE, 0x0);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
       
        /* Does this affect the RS? It appears not. */
        etna_set_state(ctx, VIVS_GL_MULTI_SAMPLE_CONFIG, 
                ETNA_MASKED_INL(VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES, 4X) &
                ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_ENABLES, 0xf) &
                ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_UNK12, 0x0) &
                ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_UNK16, 0x0)
                ); 

        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        
        /* Set up resolve to self */
        etna_set_state(ctx, VIVS_TS_COLOR_CLEAR_VALUE, 0xff7f7f7f);
        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, rt_ts->address); /* ADDR_B */
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, rt->address); /* ADDR_A */
#if 0   /* don't care about depth, for now */
        etna_set_state(ctx, VIVS_TS_DEPTH_CLEAR_VALUE, 0xffffffff);
        etna_set_state(ctx, VIVS_TS_DEPTH_STATUS_BASE, z_ts->address); /* ADDR_D */
        etna_set_state(ctx, VIVS_TS_DEPTH_SURFACE_BASE, z->address); /* ADDR_C */
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
        etna_set_state(ctx, VIVS_RS_SOURCE_ADDR, rt->address); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, rt->address); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE, 
                VIVS_RS_WINDOW_SIZE_HEIGHT(padded_height) |
                VIVS_RS_WINDOW_SIZE_WIDTH(padded_width));
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);

        etna_set_state(ctx, VIVS_RS_FLUSH_CACHE, VIVS_RS_FLUSH_CACHE_FLUSH | VIVS_GL_FLUSH_CACHE_COLOR);

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
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, rt->address + 64*64*4); /* Offset one entire 64*64 tile. Interesting things happen if only a partial tile is offset. */
        /* Pure FILL_VALUE(0) */
        //etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_ENABLED | VIVS_RS_CLEAR_CONTROL_BITS(0xffff));
        /* Vertical line pattern */
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_ENABLED2 | VIVS_RS_CLEAR_CONTROL_BITS(0xffff));
        /* Same as ENABLED2 */
        //etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_ENABLED3 | VIVS_RS_CLEAR_CONTROL_BITS(0xffff));

        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 0);
        etna_set_state(ctx, VIVS_RS_KICKER, 
                0xbeebbeeb);
      
        etna_finish(ctx);

        /* manually fill, to figure out tiling pattern */
        for(int x=0; x<16384/4; ++x)
        {
            int a = (x & 0x3F) << 2;
            int b = ((x >> 3) & 0x3F) << 2;
            int c = ((x >> 6) & 0x3F) << 2;
            ((uint32_t*)(rt->logical + 16384*6))[x] = (a & 0xFF) | ((b & 0xFF) << 8) | ((c & 0xFF) << 16);
            printf("%08x\n", (a & 0xFF) | ((b & 0xFF) << 8) | ((c & 0xFF) << 16));
        }

        /* Copy image to screen */
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG, 0);
        etna_set_state(ctx, VIVS_RS_CONFIG,
                VIVS_RS_CONFIG_SOURCE_FORMAT(pixelfmt) |
                (tiled?VIVS_RS_CONFIG_SOURCE_TILED:0) |
                VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_X8R8G8B8) |
                VIVS_RS_CONFIG_SWAP_RB);
        etna_set_state(ctx, VIVS_RS_SOURCE_STRIDE, stride);
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, fb.fb_fix.line_length);
        etna_set_state(ctx, VIVS_RS_DITHER(0), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_DITHER(1), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 
                0); /* no AA, no endian switch */
        etna_set_state(ctx, VIVS_RS_SOURCE_ADDR, rt->address); /* ADDR_A */
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
#ifdef DUMP
    bmp_dump32(fb.logical[1-backbuffer], width, height, false, "/mnt/sdcard/fb.bmp");
    printf("Dump complete\n");
#endif
    
    etna_free(ctx);
    viv_close();
    return 0;
}
