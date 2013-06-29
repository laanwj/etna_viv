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
/* Low-level framebuffer query / access */
#ifndef H_ETNA_FB
#define H_ETNA_FB

#include <etnaviv/etna.h>
#include "etna_rs.h"

#include <stdint.h>
#include <linux/fb.h>
#include <unistd.h>

#define ETNA_FB_MAX_BUFFERS (2) /* double buffering is enough */
struct pipe_resource;
struct fb_info
{
    int fd;
    int num_buffers;
    /* GPU addresses of buffers */
    size_t physical[ETNA_FB_MAX_BUFFERS];
    /* CPU addresses of buffers */
    void *logical[ETNA_FB_MAX_BUFFERS];
    size_t stride;
    size_t buffer_stride;
    struct fb_var_screeninfo fb_var;
    struct fb_fix_screeninfo fb_fix;
    void *map;

    struct etna_resource *resource;
    struct compiled_rs_state copy_to_screen[ETNA_FB_MAX_BUFFERS];

    /* Resolve format (-1 if no match), and swap red/blue bit */
    int rs_format;
    bool swap_rb;
};

/* Open framebuffer and get information */
int fb_open(int num, struct fb_info *out);

/* Set currently visible buffer id */
int fb_set_buffer(struct fb_info *fb, int buffer);

/* Close framebuffer */
int fb_close(struct fb_info *fb);

/* Bind framebuffer to render target resource */
int etna_fb_bind_resource(struct fb_info *fb, struct pipe_resource *rt_resource);

/* Copy framebuffer from bound render target resource */
int etna_fb_copy_buffer(struct fb_info *fb, struct etna_ctx *ctx, int buffer);

/* Determine the framebuffer format from Linux framebuffer info structure */
bool etna_fb_get_format(const struct fb_var_screeninfo *fb_var, unsigned *rs_format, bool *swap_rb);

#endif

