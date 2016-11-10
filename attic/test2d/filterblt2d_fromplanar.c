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
/* Filter blit through video rasterizer.
 * Source from planar YV12 surface.
 * Use a horizontal and vertical pass to scale image in both directions using a
 * sinc filter.
 * Newer GPUs can do a "one pass blit" as well, but will leave that for another time.
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
    /* target width and height */
    int width = 176*2;
    int height = 144*2;

    int padded_width = etna_align_up(width, 8);
    int padded_height = etna_align_up(height, 1);

    int dest_stride = padded_width * 4;

    printf("padded_width %i padded_height %i\n", padded_width, padded_height);
    struct viv_conn *conn = 0;
    rv = viv_open(VIV_HW_2D, &conn);
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    printf("Succesfully opened device\n");

    /* Allocate target bitmap */
    struct etna_bo *bmp = 0;
    size_t bmp_size = dest_stride * height;
    if((bmp=etna_bo_new(conn, bmp_size, DRM_ETNA_GEM_TYPE_BMP))==NULL)
    {
        fprintf(stderr, "Error allocating video memory\n");
        exit(1);
    }

    /* Allocate and read test YV12 image */
    struct etna_bo *src[3] = {0}; /* source */
    int src_width[3], src_height[3], src_stride[3];
    size_t src_size[3];
    src_width[0] = 284;
    src_height[0] = 160;
    src_stride[0] = etna_align_up(src_width[0], 8);
    src_width[1] = src_width[0] / 2;
    src_height[1] = src_height[0] / 2;
    src_stride[1] = etna_align_up(src_width[1], 8);
    src_width[2] = src_width[0] / 2;
    src_height[2] = src_height[0] / 2;
    src_stride[2] = etna_align_up(src_width[2], 8);

    for(int plane=0; plane<3; ++plane)
    {
        src_size[plane] = src_stride[plane] * src_height[plane];
        if((src[plane]=etna_bo_new(conn, src_size[plane], DRM_ETNA_GEM_TYPE_BMP))==NULL)
        {
            fprintf(stderr, "Error allocating video memory\n");
            exit(1);
        }
    }

    FILE *f = fopen("bigbuckbunny_yv12.yuv","rb");
    if(f == NULL)
    {
        printf("Cannot open test image bigbuckbunny_yv12.yuv\n");
        exit(1);
    }
    for(int plane=0; plane<3; ++plane)
    {
        for(int line=0; line<src_height[plane]; ++line)
            fread(etna_bo_map(src[plane]) + src_stride[plane]*line, etna_align_up(src_width[plane], 4), 1, f);
    }
    fclose(f);
    printf("Succesfully loaded test image\n");
    // Debug: uncomment to disable planes
    //memset(etna_bo_map(src[0]), 0, src_stride[0]*src_height[0]);
    //memset(etna_bo_map(src[1]), 0, src_stride[1]*src_height[1]);
    //memset(etna_bo_map(src[2]), 0, src_stride[2]*src_height[2]);

    /* Allocate temporary surface for scaling */
    struct etna_bo *temp = 0;
    size_t temp_width = width; // horizontal pass first
    size_t temp_height = src_height[0];
    size_t temp_stride = etna_align_up(temp_width, 8) * 4; // always align to 8 pixels
    size_t temp_size = temp_stride * temp_height;
    if((temp=etna_bo_new(conn, temp_size, DRM_ETNA_GEM_TYPE_BMP))==NULL)
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
    for(int i=0; i<bmp_size/4; ++i)
        ((uint32_t*)etna_bo_map(bmp))[i] = 0xff404040;

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
        printf("[%ix%i] -> [%ix%i] -> [%ix%i]\n",
                src_width[0], src_height[0],
                temp_width, temp_height,
                width, height);
        /*((( Horizontal pass )))*/

        /* Source configuration */
        etna_set_state(ctx, VIVS_DE_SRC_ADDRESS, etna_bo_gpu_address(src[0]));
        etna_set_state(ctx, VIVS_DE_SRC_STRIDE, src_stride[0]);
        etna_set_state(ctx, VIVS_DE_UPLANE_ADDRESS, etna_bo_gpu_address(src[1]));
        etna_set_state(ctx, VIVS_DE_UPLANE_STRIDE, src_stride[1]);
        etna_set_state(ctx, VIVS_DE_VPLANE_ADDRESS, etna_bo_gpu_address(src[2]));
        etna_set_state(ctx, VIVS_DE_VPLANE_STRIDE, src_stride[2]);

        /* Are these used in VR blit?
         * Likely, only the source format is.
         */
        etna_set_state(ctx, VIVS_DE_SRC_ROTATION_CONFIG, 0);
        etna_set_state(ctx, VIVS_DE_SRC_CONFIG,
                VIVS_DE_SRC_CONFIG_SOURCE_FORMAT(DE_FORMAT_YV12) |
                VIVS_DE_SRC_CONFIG_LOCATION_MEMORY |
                VIVS_DE_SRC_CONFIG_PE10_SOURCE_FORMAT(DE_FORMAT_YV12));

        /* Compute stretch factors */
        etna_set_state(ctx, VIVS_DE_STRETCH_FACTOR_LOW,
                VIVS_DE_STRETCH_FACTOR_LOW_X(((src_width[0] - 1) << 16) / (width - 1)));
        etna_set_state(ctx, VIVS_DE_STRETCH_FACTOR_HIGH,
                VIVS_DE_STRETCH_FACTOR_HIGH_Y(((src_height[0] - 1) << 16) / (height - 1)));

        /* Destination setup */
        etna_set_state(ctx, VIVS_DE_DEST_ADDRESS, etna_bo_gpu_address(temp));
        etna_set_state(ctx, VIVS_DE_DEST_STRIDE, temp_stride);
        etna_set_state(ctx, VIVS_DE_DEST_ROTATION_CONFIG, 0);
        etna_set_state(ctx, VIVS_DE_DEST_CONFIG,
                VIVS_DE_DEST_CONFIG_FORMAT(DE_FORMAT_A8R8G8B8) |
                VIVS_DE_DEST_CONFIG_SWIZZLE(DE_SWIZZLE_ARGB) |
                VIVS_DE_DEST_CONFIG_TILED_DISABLE |
                VIVS_DE_DEST_CONFIG_MINOR_TILED_DISABLE
                // | VIVS_DE_DEST_CONFIG_GDI_STRE_ENABLE
                );
        etna_set_state(ctx, VIVS_DE_ROP,
                VIVS_DE_ROP_ROP_FG(0xcc) | VIVS_DE_ROP_ROP_BG(0xcc) | VIVS_DE_ROP_TYPE_ROP4);

        /* Clipping rectangle (not used in VR blit) */
        etna_set_state(ctx, VIVS_DE_CLIP_TOP_LEFT, 0);
        etna_set_state(ctx, VIVS_DE_CLIP_BOTTOM_RIGHT, 0);
        /* Source size and origin not used in VR blit */
        etna_set_state(ctx, VIVS_DE_SRC_SIZE, 0);
        etna_set_state(ctx, VIVS_DE_SRC_ORIGIN, 0);

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
                VIVS_DE_VR_SOURCE_IMAGE_HIGH_RIGHT(src_width[0]) |
                VIVS_DE_VR_SOURCE_IMAGE_HIGH_BOTTOM(src_height[0]));

        etna_set_state(ctx, VIVS_DE_VR_SOURCE_ORIGIN_LOW,
                VIVS_DE_VR_SOURCE_ORIGIN_LOW_X(0));
        etna_set_state(ctx, VIVS_DE_VR_SOURCE_ORIGIN_HIGH,
                VIVS_DE_VR_SOURCE_ORIGIN_HIGH_Y(0));

        etna_set_state(ctx, VIVS_DE_VR_TARGET_WINDOW_LOW,
                VIVS_DE_VR_TARGET_WINDOW_LOW_LEFT(0) |
                VIVS_DE_VR_TARGET_WINDOW_LOW_TOP(0));
        etna_set_state(ctx, VIVS_DE_VR_TARGET_WINDOW_HIGH,
                VIVS_DE_VR_TARGET_WINDOW_HIGH_RIGHT(temp_width) |
                VIVS_DE_VR_TARGET_WINDOW_HIGH_BOTTOM(temp_height));

        etna_set_state_multi(ctx, VIVS_DE_FILTER_KERNEL(0), FB_DWORD_COUNT, filter_kernel);

        /* Kick off VR */
        etna_set_state(ctx, VIVS_DE_VR_CONFIG,
                VIVS_DE_VR_CONFIG_START_HORIZONTAL_BLIT);

        /* (((Vertical pass))) */
        etna_set_state(ctx, VIVS_DE_SRC_ADDRESS, etna_bo_gpu_address(temp));
        etna_set_state(ctx, VIVS_DE_SRC_STRIDE, temp_stride);
        etna_set_state(ctx, VIVS_DE_UPLANE_ADDRESS, 0);
        etna_set_state(ctx, VIVS_DE_UPLANE_STRIDE, 0);
        etna_set_state(ctx, VIVS_DE_VPLANE_ADDRESS, 0);
        etna_set_state(ctx, VIVS_DE_VPLANE_STRIDE, 0);
        etna_set_state(ctx, VIVS_DE_SRC_CONFIG,
                VIVS_DE_SRC_CONFIG_SOURCE_FORMAT(DE_FORMAT_A8R8G8B8) |
                VIVS_DE_SRC_CONFIG_LOCATION_MEMORY |
                VIVS_DE_SRC_CONFIG_PE10_SOURCE_FORMAT(DE_FORMAT_A8R8G8B8));
        etna_set_state(ctx, VIVS_DE_VR_SOURCE_IMAGE_LOW,
                VIVS_DE_VR_SOURCE_IMAGE_LOW_LEFT(0) |
                VIVS_DE_VR_SOURCE_IMAGE_LOW_TOP(0));
        etna_set_state(ctx, VIVS_DE_VR_SOURCE_IMAGE_HIGH,
                VIVS_DE_VR_SOURCE_IMAGE_HIGH_RIGHT(temp_width) |
                VIVS_DE_VR_SOURCE_IMAGE_HIGH_BOTTOM(temp_height));

        etna_set_state(ctx, VIVS_DE_DEST_ADDRESS, etna_bo_gpu_address(bmp));
        etna_set_state(ctx, VIVS_DE_DEST_STRIDE, dest_stride);

        etna_set_state(ctx, VIVS_DE_VR_TARGET_WINDOW_LOW,
                VIVS_DE_VR_TARGET_WINDOW_LOW_LEFT(0) |
                VIVS_DE_VR_TARGET_WINDOW_LOW_TOP(0));
        etna_set_state(ctx, VIVS_DE_VR_TARGET_WINDOW_HIGH,
                VIVS_DE_VR_TARGET_WINDOW_HIGH_RIGHT(width) |
                VIVS_DE_VR_TARGET_WINDOW_HIGH_BOTTOM(height));

        /* Kick off VR */
        etna_set_state(ctx, VIVS_DE_VR_CONFIG,
                VIVS_DE_VR_CONFIG_START_VERTICAL_BLIT);

        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_PE2D);
        etna_finish(ctx);
    }
    bmp_dump32_noflip(etna_bo_map(bmp), width, height, true, "/tmp/fb.bmp");
    printf("Dump complete\n");

    etna_free(ctx);
    viv_close(conn);
    return 0;
}

