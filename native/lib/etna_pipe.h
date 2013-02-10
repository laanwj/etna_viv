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
/* Gallium state experiments -- WIP
 */
#ifndef H_ETNA_PIPE
#define H_ETNA_PIPE

#include <stdint.h>
#include "etna.h"
#include "minigallium.h"

/* etna gallium pipe resource creation flags */
enum etna_resource_flags 
{
    ETNA_IS_TEXTURE = 0x1, /* is to be used as texture */
    ETNA_IS_RENDER_TARGET = 0x2,     /* has tile status (fast clear), use if rendertarget */
    ETNA_IS_VERTEX = 0x4,  /* vertex buffer */
    ETNA_IS_INDEX = 0x8,   /* index buffer */
    ETNA_IS_CUBEMAP = 0x10 /* cubemap texture */
};

struct pipe_context *etna_new_pipe_context(etna_ctx *ctx);

/* Allocate 2D texture or render target resource 
 */
struct pipe_resource *etna_pipe_create_2d(struct pipe_context *pipe, unsigned flags, unsigned format, unsigned width, unsigned height, unsigned max_mip_level);

/* Allocate buffer resource */
struct pipe_resource *etna_pipe_create_buffer(struct pipe_context *pipe, unsigned flags, unsigned size);

/* Free previously allocated resource */
void etna_pipe_destroy_resource(struct pipe_context *pipe, struct pipe_resource *resource);

#endif

