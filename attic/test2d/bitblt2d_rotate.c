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
/* Test bit_blt operation (rotated 90 degrees).
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

#include <errno.h>

#include <etnaviv/common.xml.h>
#include <etnaviv/state.xml.h>
#include <etnaviv/state_2d.xml.h>
#include <etnaviv/cmdstream.xml.h>
#include <etnaviv/viv.h>
#include <etnaviv/etna.h>
#include <etnaviv/etna_bo.h>
#include <etnaviv/etna_util.h>
#include <etnaviv/etna_rs.h>

#include "write_bmp.h"
#include "read_png.h"

int main(int argc, char **argv)
{
    int rv;
    int width = 256;
    int height = 256;
    
    size_t dst_stride = etna_align_up(width, 8) * 4;
    
    struct viv_conn *conn = 0;
    rv = viv_open(VIV_HW_2D, &conn);
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    printf("Succesfully opened device\n");
    
    /* Read test image */
    int src_width, src_height, src_stride;
    uint32_t *src_data = 0;
    if(!read_png("amethyst256.png", 8*4, &src_stride, &src_width, &src_height, &src_data))
    {
        printf("Unable to read amethyst256.png in current directory\n");
        exit(1);
    }
    
    struct etna_bo *bmp = 0; /* bitmap */
    struct etna_bo *src = 0; /* source */

    size_t bmp_size = dst_stride * height;
    size_t src_size = src_stride * src_height;

    if((bmp=etna_bo_new(conn, bmp_size, DRM_ETNA_GEM_TYPE_BMP))==NULL ||
       (src=etna_bo_new(conn, src_size, DRM_ETNA_GEM_TYPE_BMP))==NULL)
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

    /* switch to 2D pipe */
    etna_set_pipe(ctx, ETNA_PIPE_2D);
    /* pre-clear surface. Could use the 2D engine for this,
     * but we're lazy.
     */
    uint32_t *bmp_map = etna_bo_map(bmp);
    for(int i=0; i<bmp_size/4; ++i)
        bmp_map[i] = 0xff000000;
    memcpy(etna_bo_map(src), src_data, src_height * src_stride);

    for(int frame=0; frame<1; ++frame)
    {
        printf("*** FRAME %i ****\n", frame);

        etna_set_state(ctx, VIVS_DE_SRC_ADDRESS, etna_bo_gpu_address(src));
        etna_set_state(ctx, VIVS_DE_SRC_STRIDE, src_stride);
        etna_set_state(ctx, VIVS_DE_SRC_ROTATION_CONFIG, 
                VIVS_DE_SRC_ROTATION_CONFIG_WIDTH(src_width) |
                VIVS_DE_SRC_ROTATION_CONFIG_ROTATION_ENABLE);
        etna_set_state(ctx, VIVS_DE_SRC_ROTATION_HEIGHT,
                VIVS_DE_SRC_ROTATION_HEIGHT_HEIGHT(src_height));
        etna_set_state(ctx, VIVS_DE_SRC_CONFIG, 
                VIVS_DE_SRC_CONFIG_SOURCE_FORMAT(DE_FORMAT_A8R8G8B8) |
                VIVS_DE_SRC_CONFIG_LOCATION_MEMORY |
                VIVS_DE_SRC_CONFIG_PE10_SOURCE_FORMAT(DE_FORMAT_A8R8G8B8));
        etna_set_state(ctx, VIVS_DE_SRC_ORIGIN, 
                VIVS_DE_SRC_ORIGIN_X(28) |
                VIVS_DE_SRC_ORIGIN_Y(28));
        etna_set_state(ctx, VIVS_DE_SRC_SIZE, 
                VIVS_DE_SRC_SIZE_X(width) |
                VIVS_DE_SRC_SIZE_Y(height)
                ); // source size is ignored
        etna_set_state(ctx, VIVS_DE_SRC_COLOR_BG, 0xff303030);
        etna_set_state(ctx, VIVS_DE_SRC_COLOR_FG, 0xff12ff56);
        etna_set_state(ctx, VIVS_DE_STRETCH_FACTOR_LOW, 0);
        etna_set_state(ctx, VIVS_DE_STRETCH_FACTOR_HIGH, 0);
        etna_set_state(ctx, VIVS_DE_DEST_ADDRESS, etna_bo_gpu_address(bmp));
        etna_set_state(ctx, VIVS_DE_DEST_STRIDE, dst_stride);
        etna_set_state(ctx, VIVS_DE_DEST_ROTATION_CONFIG, 
                VIVS_DE_DEST_ROTATION_CONFIG_WIDTH(width) |
                VIVS_DE_DEST_ROTATION_CONFIG_ROTATION_DISABLE);
        etna_set_state(ctx, VIVS_DE_DEST_ROTATION_HEIGHT,
                VIVS_DE_DEST_ROTATION_HEIGHT_HEIGHT(height));
        etna_set_state(ctx, VIVS_DE_DEST_CONFIG, 
                VIVS_DE_DEST_CONFIG_FORMAT(DE_FORMAT_A8R8G8B8) |
                VIVS_DE_DEST_CONFIG_COMMAND_BIT_BLT |
                VIVS_DE_DEST_CONFIG_SWIZZLE(DE_SWIZZLE_ARGB) |
                VIVS_DE_DEST_CONFIG_TILED_DISABLE |
                VIVS_DE_DEST_CONFIG_MINOR_TILED_DISABLE
                );
        etna_set_state(ctx, VIVS_DE_ROP, 
                VIVS_DE_ROP_ROP_FG(0xcc) | VIVS_DE_ROP_ROP_BG(0xcc) | VIVS_DE_ROP_TYPE_ROP4);
        etna_set_state(ctx, VIVS_DE_CLIP_TOP_LEFT, 
                VIVS_DE_CLIP_TOP_LEFT_X(0) | 
                VIVS_DE_CLIP_TOP_LEFT_Y(0)
                );
        etna_set_state(ctx, VIVS_DE_CLIP_BOTTOM_RIGHT, 
                VIVS_DE_CLIP_BOTTOM_RIGHT_X(width) | 
                VIVS_DE_CLIP_BOTTOM_RIGHT_Y(height)
                );
        etna_set_state(ctx, VIVS_DE_CONFIG,
                VIVS_DE_CONFIG_MIRROR_BLT_ENABLE_OFF |
                VIVS_DE_CONFIG_MIRROR_BLT_MODE_NORMAL
                );
        etna_set_state(ctx, VIVS_DE_SRC_ORIGIN_FRACTION, 0);
        etna_set_state(ctx, VIVS_DE_ALPHA_CONTROL, 0);
        etna_set_state(ctx, VIVS_DE_ALPHA_MODES, 0);
        etna_set_state(ctx, VIVS_DE_ROT_ANGLE, 0);

        /* Clear color PE20 */
        etna_set_state(ctx, VIVS_DE_CLEAR_PIXEL_VALUE32, 0xff40ff40);
        /* Clear color PE10 */
        etna_set_state(ctx, VIVS_DE_CLEAR_BYTE_MASK, 0xff);
        etna_set_state(ctx, VIVS_DE_CLEAR_PIXEL_VALUE_LOW, 0xff40ff40);
        etna_set_state(ctx, VIVS_DE_CLEAR_PIXEL_VALUE_HIGH, 0xff40ff40);

        etna_set_state(ctx, VIVS_DE_DEST_COLOR_KEY, 0);
        etna_set_state(ctx, VIVS_DE_GLOBAL_SRC_COLOR, 0);
        etna_set_state(ctx, VIVS_DE_GLOBAL_DEST_COLOR, 0);
        etna_set_state(ctx, VIVS_DE_COLOR_MULTIPLY_MODES, 0);
        etna_set_state(ctx, VIVS_DE_PE_TRANSPARENCY, 0);
        etna_set_state(ctx, VIVS_DE_PE_CONTROL, 0);
        etna_set_state(ctx, VIVS_DE_PE_DITHER_LOW, 0xffffffff);
        etna_set_state(ctx, VIVS_DE_PE_DITHER_HIGH, 0xffffffff);

#define NUM_RECTS (1)
        /* Queue DE command */
        etna_reserve(ctx, 256*2 + 2);
        (ctx)->buf[(ctx)->offset++] = VIV_FE_DRAW_2D_HEADER_OP_DRAW_2D |
                                      VIV_FE_DRAW_2D_HEADER_COUNT(NUM_RECTS) |
                                      VIV_FE_DRAW_2D_HEADER_DATA_COUNT(0);
        (ctx)->offset++; /* rectangles start aligned */
        for(int rec=0; rec<NUM_RECTS; ++rec)
        {
            int tgt_width = 200;
            int tgt_height = 200;
            int x1 = 28;
            int y1 = 28;
            (ctx)->buf[(ctx)->offset++] = VIV_FE_DRAW_2D_TOP_LEFT_X(x1) |
                                          VIV_FE_DRAW_2D_TOP_LEFT_Y(y1);
            (ctx)->buf[(ctx)->offset++] = VIV_FE_DRAW_2D_BOTTOM_RIGHT_X(x1 + tgt_width) |
                                          VIV_FE_DRAW_2D_BOTTOM_RIGHT_Y(y1 + tgt_height);
        }
        etna_set_state(ctx, 1, 0);
        etna_set_state(ctx, 1, 0);
        etna_set_state(ctx, 1, 0);

        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_PE2D);
        etna_finish(ctx);
    }
    bmp_dump32_noflip(etna_bo_map(bmp), width, height, true, "/tmp/fb.bmp");
    printf("Dump complete\n");

    etna_free(ctx);
    viv_close(conn);
    return 0;
}
