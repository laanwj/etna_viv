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
#ifndef ETNA_SCREEN_H_
#define ETNA_SCREEN_H_

#include "etna_internal.h"

#include "pipe/p_screen.h"
#include "os/os_thread.h"

struct viv_conn;
struct etna_bo;

#define ETNA_SCREEN_NAME_LEN (64)
/* Gallium screen structure for etna driver.
 */
struct etna_screen {
    struct pipe_screen base;
    char name[ETNA_SCREEN_NAME_LEN];
    struct viv_conn *dev;
    struct etna_pipe_specs specs;
};

/* Resolve target.
 * Used by etna_screen_flush_frontbuffer
 */
struct etna_rs_target
{
   unsigned rs_format;
   bool swap_rb;
   unsigned width, height;
   struct etna_bo *bo;
   size_t stride;
   struct pipe_fence_handle *fence;
};

static INLINE struct etna_screen *
etna_screen(struct pipe_screen *pscreen)
{
    return (struct etna_screen *)pscreen;
}

struct pipe_screen *
etna_screen_create(struct viv_conn *dev);

#endif

