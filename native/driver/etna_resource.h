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
/* Resource handling.
 */
#ifndef H_ETNA_RESOURCE
#define H_ETNA_RESOURCE

#include "pipe/p_state.h"

struct pipe_screen;
struct etna_resource;

void etna_resource_touch(struct pipe_context *pipe, struct pipe_resource *resource_);

/* Allocate Tile Status for an etna resource.
 * Tile status is a cache of the clear status per tile. This means a smaller surface
 * has to be cleared which is faster. This is also called "fast clear".
 */
bool etna_screen_resource_alloc_ts(struct pipe_screen *screen, struct etna_resource *resource);

void etna_screen_resource_init(struct pipe_screen *screen);

#endif

