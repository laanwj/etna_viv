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
/* Mipmap generation test
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

#include "etna_pipe.h"
#include "util/u_inlines.h"
#include "util/u_format.h"
#include "util/u_gen_mipmap.h"
#include "cso_cache/cso_context.h"
#include "write_bmp.h"
#include "state_tracker/graw.h"
#include "fbdemos.h"

#include "esTransform.h"
#include "companion.h"


int main(int argc, char **argv)
{
    struct fbdemos_scaffold *fbs = 0;
    fbdemo_init(&fbs);
    struct pipe_context *pipe = fbs->pipe;
    unsigned format = PIPE_FORMAT_B5G6R5_UNORM; /* texture format */
    //unsigned format = PIPE_FORMAT_B8G8R8X8_UNORM; /* texture format */
    const struct util_format_description *format_desc = util_format_description(format);
    unsigned bs = util_format_get_blocksize(format);
    int twidth = COMPANION_TEXTURE_WIDTH/4;
    int theight = COMPANION_TEXTURE_HEIGHT;

    /* Convert and upload embedded texture */
    struct pipe_resource *tex_resource = fbdemo_create_2d(fbs->screen,
            PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_RENDER_TARGET, format,
            twidth, theight, 1000);

    /* First convert from cumbersome RGB to RGBX */
    void *temp_rgbx8888 = malloc(COMPANION_TEXTURE_WIDTH * COMPANION_TEXTURE_HEIGHT * 4);
    etna_convert_r8g8b8_to_b8g8r8x8(temp_rgbx8888, (const uint8_t*)companion_texture, COMPANION_TEXTURE_WIDTH * COMPANION_TEXTURE_HEIGHT);

    /* Then convert to destination format */
    void *temp_fmt = malloc(twidth * theight * bs);
    format_desc->pack_rgba_8unorm(temp_fmt, twidth*bs, temp_rgbx8888, COMPANION_TEXTURE_WIDTH*4,
            twidth, theight);
    etna_pipe_inline_write(pipe, tex_resource, 0, 0, temp_fmt, twidth * theight * bs);

    free(temp_rgbx8888);
    free(temp_fmt);
    /* 0 512x512
     * 1 256x256
     * 2 128x128
     * 3  64x64
     * 4  32x32
     * 5  16x16
     * 6   8x8
     * 7   4x4
     * 8   2x2
     * 9   1x1
     */
    struct pipe_sampler_view *sampler_view = pipe->create_sampler_view(pipe, tex_resource, &(struct pipe_sampler_view){
            .format = tex_resource->format,
            .u.tex.first_level = 0,
            .u.tex.last_level = 9,
            .swizzle_r = PIPE_SWIZZLE_RED,
            .swizzle_g = PIPE_SWIZZLE_GREEN,
            .swizzle_b = PIPE_SWIZZLE_BLUE,
            .swizzle_a = PIPE_SWIZZLE_ALPHA,
            });

    struct cso_context *cso = cso_create_context(pipe);
    assert(cso);
    struct gen_mipmap_state *gen_mipmap = util_create_gen_mipmap(pipe, cso);
    assert(gen_mipmap);
    for(int frame=0; frame<1; ++frame)
    {
        if(frame%50 == 0)
            printf("*** FRAME %i ****\n", frame);
        util_gen_mipmap(gen_mipmap, sampler_view, 0 /* layer */,
                sampler_view->u.tex.first_level, 
                sampler_view->u.tex.last_level, 
                PIPE_TEX_FILTER_LINEAR);
    }
    struct pipe_fence_handle *fence = 0;
    pipe->flush(pipe, &fence, 0);
    assert(fence);
    pipe->screen->fence_finish(pipe->screen, fence, PIPE_TIMEOUT_INFINITE);

    /* OK, now read back the textures */
    int lwidth = twidth;
    int lheight = theight;
    for(int level=0; level<=sampler_view->u.tex.last_level; ++level)
    {
        struct pipe_transfer *transfer = 0;
        struct pipe_box box;
        char filename[100];
        box.x = 0;
        box.y = 0;
        box.z = 0;
        box.width = lwidth;
        box.height = lheight;
        box.depth = 1;
        void *data = pipe->transfer_map(pipe, tex_resource, level, PIPE_TRANSFER_READ, 
                &box, &transfer);

        void *temp = malloc(lwidth * lheight * 4);
        printf("%i: Transfer stride is %i\n", level, transfer->stride);
        format_desc->unpack_rgba_8unorm(temp, lwidth*4, data, transfer->stride, lwidth, lheight);
        snprintf(filename, sizeof(filename), "mip%i.bmp", level);
        bmp_dump32_ex(temp, lwidth, lheight, /*flip*/ false, /*bgra*/true, /*alpha*/false, filename);
        free(temp);

        pipe->transfer_unmap(pipe, transfer);

        lwidth = (lwidth+1)/2;
        lheight = (lheight+1)/2;
    }

    fbdemo_free(fbs);
    return 0;
}

