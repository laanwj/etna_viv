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
/* Vivante specific surface tiling */
#ifndef H_ETNA_TEX
#define H_ETNA_TEX

#include <stdint.h>

/* texture or surface layout */
enum etna_surface_layout
{
    ETNA_LAYOUT_LINEAR = 0,
    ETNA_LAYOUT_TILED = 1,
    ETNA_LAYOUT_SUPER_TILED = 1|2, /* both tiling and supertiling bit enabled */
    ETNA_LAYOUT_MULTI_TILED = 4|1, /* multi pipe tiled */
    ETNA_LAYOUT_MULTI_SUPERTILED = 4|1|2 /* multi pipe supertiled */
};

void etna_texture_tile(void *dest, void *src, unsigned basex, unsigned basey, unsigned dst_stride, unsigned width, unsigned height, unsigned src_stride, unsigned elmtsize);
void etna_texture_untile(void *dest, void *src, unsigned basex, unsigned basey, unsigned src_stride, unsigned width, unsigned height, unsigned dst_stride, unsigned elmtsize);

/* XXX from/to supertiling (can have different layouts, may be better to leave to RS) */

#endif

