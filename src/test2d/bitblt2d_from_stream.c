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
/* Mono expansion bit blt operation from command stream.
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

/* Text data to expand, packed per 8x4 bits in 32 bit words */
static const uint32_t text_width = 128;
static const uint32_t text_height = 8;
static const uint32_t text_data[] = {
    0xa9aada89, 0x00898a88, 0xc20528c8, 0x00c82825, 0x00008080, 0x00808000,
    0x724a49f0, 0x00f24a4b, 0x27284887, 0x002728e0, 0x0808881c, 0x001c8888,
    0x80804830, 0x00304880, 0x08080000, 0x00020508, 0xa29c0000, 0x001c20be,
    0xcab10000, 0x00838083, 0x02e60002, 0x00c722c2, 0x221c0000, 0x001c2222,
    0xc8b00000, 0x00888888, 0x03000807, 0x00070800, 0x00808000, 0x00189880,
    0xa8988870, 0x007088c8
};
static const uint32_t text_size = sizeof(text_data)/4;

int main(int argc, char **argv)
{
    int rv;
    int width = 256;
    int height = 192;
    
    int padded_width = etna_align_up(width, 8);
    int padded_height = etna_align_up(height, 1);
    
    printf("padded_width %i padded_height %i\n", padded_width, padded_height);
    struct viv_conn *conn = 0;
    rv = viv_open(VIV_HW_2D, &conn);
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    printf("Succesfully opened device\n");
    
    struct etna_bo *bmp = 0; /* bitmap */
    struct etna_bo *src = 0; /* source */

    size_t bmp_size = width * height * 4;
    size_t src_size = width * height * 4;

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
    memset(etna_bo_map(src), 255, src_size);
    uint32_t *bmp_map = etna_bo_map(bmp);
    for(int i=0; i<bmp_size/4; ++i)
        bmp_map[i] = 0xff2020ff;
    for(int frame=0; frame<1; ++frame)
    {
        printf("*** FRAME %i ****\n", frame);

        etna_set_state(ctx, VIVS_DE_SRC_ADDRESS, 0); /* source address is ignored for monochrome blits */
        //etna_set_state(ctx, VIVS_DE_SRC_ADDRESS, etna_bo_gpu_address(src));
        etna_set_state(ctx, VIVS_DE_SRC_STRIDE, 4);
        etna_set_state(ctx, VIVS_DE_SRC_ROTATION_CONFIG, 0);
        etna_set_state(ctx, VIVS_DE_SRC_CONFIG, 
                VIVS_DE_SRC_CONFIG_UNK16 |
                VIVS_DE_SRC_CONFIG_SOURCE_FORMAT(DE_FORMAT_MONOCHROME) |
                VIVS_DE_SRC_CONFIG_LOCATION_STREAM |
                VIVS_DE_SRC_CONFIG_PACK_PACKED8 | /* 8 by 4 blocks */
                VIVS_DE_SRC_CONFIG_PE10_SOURCE_FORMAT(DE_FORMAT_MONOCHROME));
        etna_set_state(ctx, VIVS_DE_SRC_ORIGIN, 0);
        etna_set_state(ctx, VIVS_DE_SRC_SIZE, 0); // source size is ignored
        etna_set_state(ctx, VIVS_DE_SRC_COLOR_BG, 0xff2020ff);
        etna_set_state(ctx, VIVS_DE_SRC_COLOR_FG, 0xffffffff);
        etna_set_state(ctx, VIVS_DE_STRETCH_FACTOR_LOW, 0);
        etna_set_state(ctx, VIVS_DE_STRETCH_FACTOR_HIGH, 0);
        etna_set_state(ctx, VIVS_DE_DEST_ADDRESS, etna_bo_gpu_address(bmp));
        etna_set_state(ctx, VIVS_DE_DEST_STRIDE, width*4);
        etna_set_state(ctx, VIVS_DE_DEST_ROTATION_CONFIG, 0);
        etna_set_state(ctx, VIVS_DE_DEST_CONFIG, 
                VIVS_DE_DEST_CONFIG_FORMAT(DE_FORMAT_A8R8G8B8) |
                VIVS_DE_DEST_CONFIG_COMMAND_BIT_BLT |
                VIVS_DE_DEST_CONFIG_SWIZZLE(DE_SWIZZLE_ARGB) |
                VIVS_DE_DEST_CONFIG_TILED_DISABLE |
                VIVS_DE_DEST_CONFIG_MINOR_TILED_DISABLE
                );
        // 10101010  0xaa   destination
        // 01010101  0x55   !destination
        // 11001100  0xcc   source
        // 00110011  0x33   !source
        // 11110000  0xf0   pattern (8x8)
        // 00001111  0x0f   !pattern (8x8)
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
        etna_set_state(ctx, VIVS_DE_CONFIG, 0); /* TODO */
        etna_set_state(ctx, VIVS_DE_SRC_ORIGIN_FRACTION, 0);
        etna_set_state(ctx, VIVS_DE_ALPHA_CONTROL, 0);
        etna_set_state(ctx, VIVS_DE_ALPHA_MODES, 0);
        etna_set_state(ctx, VIVS_DE_DEST_ROTATION_HEIGHT, 0);
        etna_set_state(ctx, VIVS_DE_SRC_ROTATION_HEIGHT, 0);
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
        etna_reserve(ctx, 256*2 + 2 + text_size);
        (ctx)->buf[(ctx)->offset++] = VIV_FE_DRAW_2D_HEADER_OP_DRAW_2D |
                                      VIV_FE_DRAW_2D_HEADER_COUNT(NUM_RECTS) |
                                      VIV_FE_DRAW_2D_HEADER_DATA_COUNT(text_size);
        (ctx)->offset++; /* rectangles start aligned */
        for(int rec=0; rec<NUM_RECTS; ++rec)
        {
            int x1 = 4+16*rec;
            int y1 = 4;
            int x2 = x1 + text_width;
            int y2 = y1 + text_height;
            (ctx)->buf[(ctx)->offset++] = VIV_FE_DRAW_2D_TOP_LEFT_X(x1) |
                                          VIV_FE_DRAW_2D_TOP_LEFT_Y(y1);
            (ctx)->buf[(ctx)->offset++] = VIV_FE_DRAW_2D_BOTTOM_RIGHT_X(x2) |
                                          VIV_FE_DRAW_2D_BOTTOM_RIGHT_Y(y2);
        }
        /* data in-stream */
        ETNA_ALIGN(ctx);
        for(int ptr=0; ptr<text_size; ++ptr)
            (ctx)->buf[(ctx)->offset++] = text_data[ptr];
        ETNA_ALIGN(ctx);
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
