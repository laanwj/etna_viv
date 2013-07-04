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
#include "etna_translate.h"
#include "etna_screen.h"
#include "util/u_memory.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <string.h>

#include <errno.h>

#ifdef ANDROID
#define FBDEV_DEV "/dev/graphics/fb%i"
#else
#define FBDEV_DEV "/dev/fb%i"
#endif

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
    
    if(etna_bswap_create(fbs->ctx, &fbs->buffers, fbs->fb.num_buffers, (etna_set_buffer_cb_t)&fb_set_buffer, (etna_copy_buffer_cb_t)&etna_fb_copy_buffer, &fbs->fb) != ETNA_OK)
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

/* Open framebuffer and get information */
int fb_open(int num, struct fb_info *out)
{
    char devname[256];
    memset(out, 0, sizeof(struct fb_info));

    snprintf(devname, 256, FBDEV_DEV, num);
	
    int fd = open(devname, O_RDWR);
    if (fd == -1) {
        printf("Error: failed to open %s: %s\n",
                devname, strerror(errno));
        return errno;
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &out->fb_var) ||
        ioctl(fd, FBIOGET_FSCREENINFO, &out->fb_fix)) {
            printf("Error: failed to run ioctl on %s: %s\n",
                    devname, strerror(errno));
        close(fd);
        return errno;
    }

    printf("fix smem_start %08x\n", (unsigned)out->fb_fix.smem_start);
    printf("    smem_len %08x\n", (unsigned)out->fb_fix.smem_len);
    printf("    line_length %08x\n", (unsigned)out->fb_fix.line_length);
    printf("\n");
    printf("var x_res %i\n", (unsigned)out->fb_var.xres);
    printf("    y_res %i\n", (unsigned)out->fb_var.yres);
    printf("    x_res_virtual %i\n", (unsigned)out->fb_var.xres_virtual);
    printf("    y_res_virtual %i\n", (unsigned)out->fb_var.yres_virtual);
    printf("    bits_per_pixel %i\n", (unsigned)out->fb_var.bits_per_pixel);
    printf("    red.offset %i\n", (unsigned)out->fb_var.red.offset);
    printf("    red.length %i\n", (unsigned)out->fb_var.red.length);
    printf("    green.offset %i\n", (unsigned)out->fb_var.green.offset);
    printf("    green.length %i\n", (unsigned)out->fb_var.green.length);
    printf("    blue.offset %i\n", (unsigned)out->fb_var.blue.offset);
    printf("    blue.length %i\n", (unsigned)out->fb_var.blue.length);
    printf("    transp.offset %i\n", (unsigned)out->fb_var.transp.offset);
    printf("    transp.length %i\n", (unsigned)out->fb_var.transp.length);
    
    out->fd = fd;
    out->stride = out->fb_fix.line_length;
    out->buffer_stride = out->stride * out->fb_var.yres;
    out->num_buffers = out->fb_fix.smem_len / out->buffer_stride;
    out->map = mmap(NULL, out->fb_fix.smem_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    printf("    mmap: %p\n", out->map);

    if(out->num_buffers > ETNA_FB_MAX_BUFFERS)
        out->num_buffers = ETNA_FB_MAX_BUFFERS;

    for(int idx=0; idx<out->num_buffers; ++idx)
    {
        out->physical[idx] = out->fb_fix.smem_start + idx * out->buffer_stride;
        out->logical[idx] = (void*)((size_t)out->map + idx * out->buffer_stride);
    }
    printf("number of fb buffers: %i\n", out->num_buffers);
    int req_virth = (out->num_buffers * out->fb_var.yres);
    if(out->fb_var.yres_virtual < req_virth)
    {
        printf("required virtual h is %i, current virtual h is %i: requesting change",
                req_virth, out->fb_var.yres_virtual);
        out->fb_var.yres_virtual = req_virth;
        if (ioctl(out->fd, FBIOPUT_VSCREENINFO, &out->fb_var))
        {
            printf("Warning: failed to run ioctl to change virtual height for buffering: %s. Rendering may fail.\n", strerror(errno));
        }
    }

    /* determine resolve format */
    if(!etna_fb_get_format(&out->fb_var, (unsigned*)&out->rs_format, &out->swap_rb))
    {
        /* no match */
        out->rs_format = -1;
        out->swap_rb = false;
    }

    return 0;
}

/* Set currently visible buffer id */
int fb_set_buffer(struct fb_info *fb, int buffer)
{
    fb->fb_var.yoffset = buffer * fb->fb_var.yres;
    if (ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->fb_var))
    {
        printf("Error: failed to run ioctl to pan display: %s\n", strerror(errno));
        return errno;
    }
    return 0;
}

int fb_close(struct fb_info *fb)
{
    if(fb->map)
        munmap(fb->map, fb->fb_fix.smem_len);
    close(fb->fd);
    return 0;
}


int etna_fb_bind_resource(struct fb_info *fb, struct pipe_resource *rt_resource_)
{
    struct etna_resource *rt_resource = etna_resource(rt_resource_);
    fb->resource = rt_resource;
    assert(rt_resource->base.width0 <= fb->fb_var.xres && rt_resource->base.height0 <= fb->fb_var.yres);
    for(int bi=0; bi<ETNA_FB_MAX_BUFFERS; ++bi)
    {
        etna_compile_rs_state(&fb->copy_to_screen[bi], &(struct rs_state){
                    .source_format = translate_rt_format(rt_resource->base.format, false),
                    .source_tiling = rt_resource->layout,
                    .source_addr = rt_resource->levels[0].address,
                    .source_stride = rt_resource->levels[0].stride,
                    .dest_format = fb->rs_format,
                    .dest_tiling = ETNA_LAYOUT_LINEAR,
                    .dest_addr = fb->physical[bi],
                    .dest_stride = fb->fb_fix.line_length,
                    .swap_rb = fb->swap_rb,
                    .dither = {0xffffffff, 0xffffffff}, // XXX dither when going from 24 to 16 bit?
                    .clear_mode = VIVS_RS_CLEAR_CONTROL_MODE_DISABLED,
                    .width = fb->fb_var.xres,
                    .height = fb->fb_var.yres
                });
    }
    return 0;
}

int etna_fb_copy_buffer(struct fb_info *fb, struct etna_ctx *ctx, int buffer)
{
    assert(fb->resource && fb->rs_format != -1);
    /*  XXX assumes TS is still set up correctly for the resource to be copied
     *  from. Currently this is not the case when another render target has been selected
     *  before calling this function.
     */
    etna_submit_rs_state(ctx, &fb->copy_to_screen[buffer]);
    /* Flush RS */
    etna_set_state(ctx, VIVS_RS_FLUSH_CACHE, VIVS_RS_FLUSH_CACHE_FLUSH);

    return 0;
}

