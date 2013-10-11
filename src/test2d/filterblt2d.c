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
/* Scale image using filter blit through video rasterizer
 * (hardware scaling using arbitrary 9-tap kernel and 5 bit subpixel precision).
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
#include <math.h>

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

#define M_PIf               3.14159265358979323846f

#define FB_ROWS_TOTAL       (32)
#define FB_ROWS_TO_STORE    (FB_ROWS_TOTAL / 2 + 1)
#define FB_NR_VALUES        (9)
#define FB_VALUES_TO_STORE  (FB_ROWS_TO_STORE * FB_NR_VALUES)
#define FB_EVEN_VALUE_COUNT ((FB_VALUES_TO_STORE + 1) & ~1)
#define FB_DWORD_COUNT      (FB_EVEN_VALUE_COUNT / 2)

static inline float sinc(float x)
{
    if(x == 0.0f)
        return 1.0f;
    else
        return sinf(x) / x;
}

int main(int argc, char **argv)
{
    int rv;
    int width = 512;
    int height = 256;

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

    size_t bmp_size = width * height * 4;
    size_t src_size = src_stride * height;

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
        bmp_map[i] = 0xff404040;
    memcpy(etna_bo_map(src), src_data, src_size);

    /* Compute lanczos filter kernel */
    uint32_t filter_kernel[FB_DWORD_COUNT] = {0};
    float kernel_in[FB_ROWS_TO_STORE][9] = {{0.0f}};
    float row_ofs = 0.5f;
    float radius = 4.0f;
    for(int row_idx=0; row_idx<FB_ROWS_TO_STORE; ++row_idx)
    {
        for(int val_idx=0; val_idx<FB_NR_VALUES; ++val_idx)
        {
            float x = val_idx - 4.0f + row_ofs;
            if(fabs(x) <= radius)
                kernel_in[row_idx][val_idx] = sinc(M_PIf * x) * sinc(M_PIf * x / radius);
        }
        row_ofs -= 1.0 / FB_ROWS_TOTAL;
    }

    /* Normalize kernel */
    for(int row_idx=0; row_idx<FB_ROWS_TO_STORE; ++row_idx)
    {
        float sum = 0.0f;
        for(int val_idx=0; val_idx<FB_NR_VALUES; ++val_idx)
        {
            sum += kernel_in[row_idx][val_idx];
        }
        if(sum != 0)
        {
            for(int val_idx=0; val_idx<FB_NR_VALUES; ++val_idx)
            {
                kernel_in[row_idx][val_idx] /= sum;
            }
        }
    }

    /* Convert filter to state vector */
    int ptr = 0;
    for(int row_idx=0; row_idx<FB_ROWS_TO_STORE; ++row_idx)
    {
        for(int val_idx=0; val_idx<FB_NR_VALUES; ++val_idx)
        {
            /* -2.0 .. 2.0 to -0x8000..0x7fff
             * (1.14 fixed point with sign bit)
             */
            float val = kernel_in[row_idx][val_idx] * (1<<14);
            uint16_t bits = 0;
            if(val <= -0x8000)
                bits = 0x8000;
            else if(val >= 0x7fff)
                bits = 0x7fff;
            else if(val >= 0)
                bits = val;
            else if(val < 0)
                bits = val + 0x10000;
            filter_kernel[ptr/2] |= bits << ((ptr&1)*16);
            ++ptr;
        }
    }

    for(int frame=0; frame<1; ++frame)
    {
        printf("*** FRAME %i ****\n", frame);
        /* Source configuration */
        etna_set_state(ctx, VIVS_DE_SRC_ADDRESS, etna_bo_gpu_address(src));
        etna_set_state(ctx, VIVS_DE_SRC_STRIDE, src_stride);
        etna_set_state(ctx, VIVS_DE_UPLANE_ADDRESS, 0);
        etna_set_state(ctx, VIVS_DE_UPLANE_STRIDE, 0);
        etna_set_state(ctx, VIVS_DE_VPLANE_ADDRESS, 0);
        etna_set_state(ctx, VIVS_DE_VPLANE_STRIDE, 0);

        /* Are these used in VR blit?
         * Likely, only the source format is.
         */
        etna_set_state(ctx, VIVS_DE_SRC_ROTATION_CONFIG, 0);
        etna_set_state(ctx, VIVS_DE_SRC_CONFIG,
                VIVS_DE_SRC_CONFIG_SOURCE_FORMAT(DE_FORMAT_A8R8G8B8) |
                VIVS_DE_SRC_CONFIG_LOCATION_MEMORY |
                VIVS_DE_SRC_CONFIG_PE10_SOURCE_FORMAT(DE_FORMAT_A8R8G8B8));
        etna_set_state(ctx, VIVS_DE_SRC_ORIGIN,
                VIVS_DE_SRC_ORIGIN_X(0) |
                VIVS_DE_SRC_ORIGIN_Y(0));
        etna_set_state(ctx, VIVS_DE_SRC_SIZE,
                VIVS_DE_SRC_SIZE_X(src_width) |
                VIVS_DE_SRC_SIZE_Y(src_height)
                ); // source size is ignored

        /* Compute stretch factors */
        etna_set_state(ctx, VIVS_DE_STRETCH_FACTOR_LOW,
                VIVS_DE_STRETCH_FACTOR_LOW_X(((src_width - 1) << 16) / (width - 1)));
        etna_set_state(ctx, VIVS_DE_STRETCH_FACTOR_HIGH,
                VIVS_DE_STRETCH_FACTOR_HIGH_Y(((src_height - 1) << 16) / (height - 1)));

        /* Destination setup */
        etna_set_state(ctx, VIVS_DE_DEST_ADDRESS, etna_bo_gpu_address(bmp));
        etna_set_state(ctx, VIVS_DE_DEST_STRIDE, width*4);
        etna_set_state(ctx, VIVS_DE_DEST_ROTATION_CONFIG, 0);
        etna_set_state(ctx, VIVS_DE_DEST_CONFIG,
                VIVS_DE_DEST_CONFIG_FORMAT(DE_FORMAT_A8R8G8B8) |
                VIVS_DE_DEST_CONFIG_COMMAND_HOR_FILTER_BLT |
                VIVS_DE_DEST_CONFIG_SWIZZLE(DE_SWIZZLE_ARGB) |
                VIVS_DE_DEST_CONFIG_TILED_DISABLE |
                VIVS_DE_DEST_CONFIG_MINOR_TILED_DISABLE
                // | VIVS_DE_DEST_CONFIG_GDI_STRE_ENABLE
                );
        etna_set_state(ctx, VIVS_DE_ROP,
                VIVS_DE_ROP_ROP_FG(0xcc) | VIVS_DE_ROP_ROP_BG(0xcc) | VIVS_DE_ROP_TYPE_ROP4);
        /* Clipping rectangle (probably not used in VR blit) */
        etna_set_state(ctx, VIVS_DE_CLIP_TOP_LEFT,
                VIVS_DE_CLIP_TOP_LEFT_X(0) |
                VIVS_DE_CLIP_TOP_LEFT_Y(0)
                );
        etna_set_state(ctx, VIVS_DE_CLIP_BOTTOM_RIGHT,
                VIVS_DE_CLIP_BOTTOM_RIGHT_X(width) |
                VIVS_DE_CLIP_BOTTOM_RIGHT_Y(height)
                );

        /* Misc DE/PE setup */
        etna_set_state(ctx, VIVS_DE_CONFIG, 0); /* TODO */
        etna_set_state(ctx, VIVS_DE_SRC_ORIGIN_FRACTION, 0);
        etna_set_state(ctx, VIVS_DE_ALPHA_CONTROL, 0);
        etna_set_state(ctx, VIVS_DE_ALPHA_MODES, 0);
        etna_set_state(ctx, VIVS_DE_DEST_ROTATION_HEIGHT, 0);
        etna_set_state(ctx, VIVS_DE_SRC_ROTATION_HEIGHT, 0);
        etna_set_state(ctx, VIVS_DE_ROT_ANGLE, 0);

        etna_set_state(ctx, VIVS_DE_DEST_COLOR_KEY, 0);
        etna_set_state(ctx, VIVS_DE_GLOBAL_SRC_COLOR, 0);
        etna_set_state(ctx, VIVS_DE_GLOBAL_DEST_COLOR, 0);
        etna_set_state(ctx, VIVS_DE_COLOR_MULTIPLY_MODES, 0);
        etna_set_state(ctx, VIVS_DE_PE_TRANSPARENCY, 0);
        etna_set_state(ctx, VIVS_DE_PE_CONTROL, 0);
        etna_set_state(ctx, VIVS_DE_PE_DITHER_LOW, 0xffffffff);
        etna_set_state(ctx, VIVS_DE_PE_DITHER_HIGH, 0xffffffff);

        /* Program video rasterizer */
        etna_set_state(ctx, VIVS_DE_VR_CONFIG_EX, 0);
        etna_set_state(ctx, VIVS_DE_VR_SOURCE_IMAGE_LOW,
                VIVS_DE_VR_SOURCE_IMAGE_LOW_LEFT(0) |
                VIVS_DE_VR_SOURCE_IMAGE_LOW_TOP(0));
        etna_set_state(ctx, VIVS_DE_VR_SOURCE_IMAGE_HIGH,
                VIVS_DE_VR_SOURCE_IMAGE_HIGH_RIGHT(src_width) |
                VIVS_DE_VR_SOURCE_IMAGE_HIGH_BOTTOM(src_height));

        etna_set_state(ctx, VIVS_DE_VR_SOURCE_ORIGIN_LOW,
                VIVS_DE_VR_SOURCE_ORIGIN_LOW_X(0));
        etna_set_state(ctx, VIVS_DE_VR_SOURCE_ORIGIN_HIGH,
                VIVS_DE_VR_SOURCE_ORIGIN_HIGH_Y(0));

        etna_set_state(ctx, VIVS_DE_VR_TARGET_WINDOW_LOW,
                VIVS_DE_VR_TARGET_WINDOW_LOW_LEFT(0) |
                VIVS_DE_VR_TARGET_WINDOW_LOW_TOP(0));
        etna_set_state(ctx, VIVS_DE_VR_TARGET_WINDOW_HIGH,
                VIVS_DE_VR_TARGET_WINDOW_HIGH_RIGHT(width) |
                VIVS_DE_VR_TARGET_WINDOW_HIGH_BOTTOM(height));

        etna_set_state_multi(ctx, VIVS_DE_FILTER_KERNEL(0), FB_DWORD_COUNT, filter_kernel);

        /* Kick off VR */
        etna_set_state(ctx, VIVS_DE_VR_CONFIG,
                VIVS_DE_VR_CONFIG_START_HORIZONTAL_BLIT);

        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_PE2D);
        etna_finish(ctx);
    }
    bmp_dump32_noflip(etna_bo_map(bmp), width, height, true, "/tmp/fb.bmp");
    printf("Dump complete\n");

    etna_free(ctx);
    viv_close(conn);
    return 0;
}

