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
/* Utilities for framebuffer demos */
#ifndef H_FBDEMOS
#define H_FBDEMOS

#include <etnaviv/etna_fb.h>
#include "etna_bswap.h" 

#define ETNA_FB_MAX_BUFFERS (2) /* double buffering is enough */
struct pipe_resource;
struct etna_bo;

struct fb_info
{
    int fd;
    int num_buffers;
    struct etna_bo *buffer[ETNA_FB_MAX_BUFFERS];
    int padded_height;
    size_t stride;
    size_t buffer_stride;
    struct fb_var_screeninfo fb_var;
    struct fb_fix_screeninfo fb_fix;

    struct etna_resource *resource;
    struct compiled_rs_state copy_to_screen[ETNA_FB_MAX_BUFFERS];

    /* Resolve format (-1 if no match), and swap red/blue bit */
    int rs_format;
    bool swap_rb;
};

struct fbdemos_scaffold
{
    int width;
    int height;

    struct fb_info fb;
    struct viv_conn *conn;
    struct etna_ctx *ctx;
    struct pipe_context *pipe;
    struct etna_bswap_buffers *buffers;
    struct pipe_screen *screen;
};

void fbdemo_init(struct fbdemos_scaffold **out);
void fbdemo_free(struct fbdemos_scaffold *fbs);
struct pipe_resource *fbdemo_create_2d(struct pipe_screen *screen, unsigned flags, unsigned format, unsigned width, unsigned height, unsigned max_mip_level);
struct pipe_resource *fbdemo_create_cube(struct pipe_screen *screen, unsigned flags, unsigned format, unsigned width, unsigned height, unsigned max_mip_level);

/** 
 * One-shot write to texture or buffer, similar to gallium transfer_inline_write but somewhat more limited right now.
 * Does tiling if needed.
 * XXX no stride parameter, assumes that data is tightly packed.
 */
void etna_pipe_inline_write(struct pipe_context *pipe, struct pipe_resource *resource, unsigned layer, unsigned level, void *data, size_t size);

void etna_convert_r8g8b8_to_b8g8r8x8(uint32_t *dst, const uint8_t *src, unsigned num_pixels);

/* Open framebuffer and get information */
int fb_open(struct viv_conn *conn, int num, struct fb_info *out);

/* Set currently visible buffer id */
int fb_set_buffer(struct fb_info *fb, int buffer);

/* Close framebuffer */
int fb_close(struct fb_info *fb);

/* Bind framebuffer to render target resource */
int etna_fb_bind_resource(struct fbdemos_scaffold *fbs, struct pipe_resource *rt_resource);

/* Copy framebuffer from bound render target resource */
int etna_fb_copy_buffer(struct fb_info *fb, struct etna_ctx *ctx, int buffer);


#endif

