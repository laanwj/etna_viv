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
#include "fbdemos.h"

#include "etna_pipe.h"
#include "etna_screen.h"
#include "util/u_memory.h"

#include <stdio.h>

void fbdemo_init(struct fbdemos_scaffold **out)
{
    struct fbdemos_scaffold *fbs = CALLOC_STRUCT(fbdemos_scaffold);
    int rv;
    
    rv = fb_open(0, &fbs->fb);
    if(rv!=0)
    {
        exit(1);
    }
    fbs->width = fbs->fb.fb_var.xres;
    fbs->height = fbs->fb.fb_var.yres;

    rv = viv_open(VIV_HW_3D, &fbs->conn);
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    printf("Succesfully opened device\n");

    /* Create screen */
    if((fbs->screen = etna_screen_create(fbs->conn)) == NULL)
    {
        printf("Unable to create screen context\n");
        exit(1);
    }

    if((fbs->pipe = fbs->screen->context_create(fbs->screen, NULL)) == NULL)
    {
        printf("Unable to create etna context\n");
        exit(1);
    }
    fbs->ctx = etna_pipe_get_etna_context(fbs->pipe);
    
    if(etna_bswap_create(fbs->ctx, &fbs->buffers, (etna_set_buffer_cb_t)&fb_set_buffer, (etna_copy_buffer_cb_t)&etna_fb_copy_buffer, &fbs->fb) != ETNA_OK)
    {
        printf("Unable to create buffer swapper\n");
        exit(1);
    }

    *out = fbs;
}

void fbdemo_free(struct fbdemos_scaffold *fbs)
{
    etna_bswap_free(fbs->buffers);
    etna_free(fbs->ctx);
    viv_close(fbs->conn);
    free(fbs);
}

struct pipe_resource *fbdemo_create_2d(struct pipe_screen *screen, unsigned bind, unsigned format, unsigned width, unsigned height, unsigned max_mip_level)
{
    return screen->resource_create(screen, &(struct pipe_resource){
            .target = PIPE_TEXTURE_2D,
            .format = format,
            .width0 = width,
            .height0 = height,
            .depth0 = 1,
            .array_size = 1,
            .last_level = max_mip_level,
            .nr_samples = 1,
            .usage = PIPE_USAGE_IMMUTABLE,
            .bind = bind,
            .flags = 0,
            });
}

struct pipe_resource *fbdemo_create_cube(struct pipe_screen *screen, unsigned bind, unsigned format, unsigned width, unsigned height, unsigned max_mip_level)
{
    return screen->resource_create(screen, &(struct pipe_resource){
            .target = PIPE_TEXTURE_CUBE,
            .format = format,
            .width0 = width,
            .height0 = height,
            .depth0 = 1,
            .array_size = 6,
            .last_level = max_mip_level,
            .nr_samples = 1,
            .usage = PIPE_USAGE_IMMUTABLE,
            .bind = bind,
            .flags = 0,
            });
}

void etna_pipe_inline_write(struct pipe_context *pipe, struct pipe_resource *resource, unsigned layer, unsigned level, void *data, size_t size)
{
    uint stride, layer_stride;
    struct pipe_box box;
    struct etna_resource *eresource = etna_resource(resource);

    box.x = 0;
    box.y = 0;
    box.z = layer;
    box.width = eresource->levels[level].width;
    box.height = eresource->levels[level].height;
    box.depth = 1;
    stride = eresource->levels[level].stride;
    layer_stride = eresource->levels[level].layer_stride;

    pipe->transfer_inline_write(pipe, resource, level, 
           (PIPE_TRANSFER_WRITE | PIPE_TRANSFER_UNSYNCHRONIZED), &box, data, stride, layer_stride);
}

void etna_convert_r8g8b8_to_b8g8r8x8(uint32_t *dst, const uint8_t *src, unsigned num_pixels)
{
    for(unsigned idx=0; idx<num_pixels; ++idx)
    {
        dst[idx] = ((0xFF) << 24) | (src[idx*3+0] << 16) | (src[idx*3+1] << 8) | src[idx*3+2];
    }
}


