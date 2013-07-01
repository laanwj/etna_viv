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
/* Functions dealing with fences */
#ifndef ETNA_FENCE_H_
#define ETNA_FENCE_H_

#include "pipe/p_state.h"

struct pipe_screen;
struct pipe_fence_handle;
struct etna_ctx;

struct etna_fence
{
    struct pipe_reference reference;
    int signal;
};

static INLINE struct etna_fence *
etna_fence(struct pipe_fence_handle *pfence)
{
    return (struct etna_fence *)pfence;
}

int etna_fence_new(struct etna_ctx *ctx, struct pipe_fence_handle **fence);

void etna_screen_fence_reference( struct pipe_screen *screen,
                        struct pipe_fence_handle **ptr,
                        struct pipe_fence_handle *fence );

boolean etna_screen_fence_signalled( struct pipe_screen *screen,
                           struct pipe_fence_handle *fence );

boolean etna_screen_fence_finish( struct pipe_screen *screen,
                        struct pipe_fence_handle *fence,
                        uint64_t timeout );
#endif

