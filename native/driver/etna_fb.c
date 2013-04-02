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
#include "etna_fb.h"
#include "etna_translate.h"
#include "etna_pipe.h"
#include "state.xml.h"
#include "state_3d.xml.h"

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
#include <assert.h>

#ifdef ANDROID
#define FBDEV_DEV "/dev/graphics/fb%i"
#else
#define FBDEV_DEV "/dev/fb%i"
#endif

/* XXX:
 * If you get "Error: failed to run ioctl to pan display", use
 *     fbset -vyres 2160
 * replace 2160 by 2* y resolution.
 */

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

    assert(out->num_buffers >= 2); /* if you hit this assert, there is not enough fb memory for double-buffering, */
                                   /* either fix single-buffering in etna or modify your kernel fb driver settings */
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
            printf("Warning: failed to run ioctl to change virtual height: %s\n", strerror(errno));
        }
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
    close(fb->fd);
    return 0;
}

/* Structure to convert framebuffer format to RS destination conf */
struct etna_fb_format_desc
{
    unsigned bits_per_pixel;
    unsigned red_offset;
    unsigned red_length;
    unsigned green_offset;
    unsigned green_length;
    unsigned blue_offset;
    unsigned blue_length;
    unsigned rs_format;
    bool swap_rb;
};

static const struct etna_fb_format_desc etna_fb_formats[] = {
    {32, 16, 8, 8, 8, 0 , 8, RS_FORMAT_X8R8G8B8, false}, /* X8R8G8B8 */
    {32, 0 , 8, 8, 8, 16, 8, RS_FORMAT_X8R8G8B8, true},  /* X8B8G8R8 */
    {16, 8 , 4, 4, 4, 0,  4, RS_FORMAT_X4R4G4B4, false}, /* X4R4G4B4 */
    {16, 0 , 4, 4, 4, 8,  4, RS_FORMAT_X4R4G4B4, true},  /* X4B4G4R4 */
    {16, 10, 5, 5, 5, 0,  5, RS_FORMAT_X1R5G5B5, false}, /* X1R5G5B5 */
    {16, 0,  5, 5, 5, 10, 5, RS_FORMAT_X1R5G5B5, true},  /* X1B5G5R5 */
    {16, 11, 5, 5, 6, 0,  5, RS_FORMAT_R5G6B5, false},   /* R5G6B5 */
    {16, 0,  5, 5, 6, 11, 5, RS_FORMAT_R5G6B5, true},    /* B5G6R5 */
    /* I guess we could support YUV outputs as well... */
};
#define NUM_FB_FORMATS (sizeof(etna_fb_formats) / sizeof(etna_fb_formats[0]))

int etna_fb_bind_resource(struct fb_info *fb, struct pipe_resource *rt_resource_)
{
    struct etna_resource *rt_resource = etna_resource(rt_resource_);
    int fmt_idx = 0;
    fb->resource = rt_resource;
    
    for(fmt_idx=0; fmt_idx<NUM_FB_FORMATS; ++fmt_idx)
    {
        const struct etna_fb_format_desc *desc = &etna_fb_formats[fmt_idx];
        if(desc->red_offset == fb->fb_var.red.offset &&
            desc->red_length == fb->fb_var.red.length &&
            desc->green_offset == fb->fb_var.green.offset &&
            desc->green_length == fb->fb_var.green.length &&
            desc->blue_offset == fb->fb_var.blue.offset &&
            desc->blue_length == fb->fb_var.blue.length)
        {
            break;
        }
    }
    if(fmt_idx == NUM_FB_FORMATS)
    {
        printf("Unsupported framebuffer format: red_offset=%i red_length=%i green_offset=%i green_length=%i blue_offset=%i blue_length=%i\n",
                (int)fb->fb_var.red.offset, (int)fb->fb_var.red.length,
                (int)fb->fb_var.green.offset, (int)fb->fb_var.green.length,
                (int)fb->fb_var.blue.offset, (int)fb->fb_var.blue.length);
        fmt_idx = 0; // XXX
    } else {
        printf("Framebuffer format: %i, flip_rb=%i\n", 
                etna_fb_formats[fmt_idx].rs_format, 
                etna_fb_formats[fmt_idx].swap_rb);
    }
    for(int bi=0; bi<ETNA_FB_MAX_BUFFERS; ++bi)
    {
        etna_compile_rs_state(&fb->copy_to_screen[bi], &(struct rs_state){
                    .source_format = translate_rt_format(rt_resource->base.format),
                    .source_tiling = rt_resource->layout,
                    .source_addr = rt_resource->levels[0].address,
                    .source_stride = rt_resource->levels[0].stride,
                    .dest_format = etna_fb_formats[fmt_idx].rs_format,
                    .dest_tiling = ETNA_LAYOUT_LINEAR,
                    .dest_addr = fb->physical[bi],
                    .dest_stride = fb->fb_fix.line_length,
                    .swap_rb = etna_fb_formats[fmt_idx].swap_rb,
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
    assert(fb->resource);
    /*  XXX assumes TS is still set up correctly */
    etna_submit_rs_state(ctx, &fb->copy_to_screen[buffer]);
    return 0;
}

