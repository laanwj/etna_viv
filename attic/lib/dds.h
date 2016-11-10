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
#if !defined( DDS_H )
#define DDS_H
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

/* Formats */
#define FMT_A8R8G8B8 0
#define FMT_X8R8G8B8 1
#define FMT_R8G8B8   2
#define FMT_A1R5G5B5 3
#define FMT_X1R5G5B5 4
#define FMT_R5G6B5   5
#define FMT_DXT1     6
#define FMT_DXT3     7
#define FMT_DXT5     8
#define FMT_ETC1     9
#define FMT_A8       10
#define FMT_L8       11
#define FMT_A8L8     12

typedef struct _dds_mipmap
{
    unsigned int width, height, stride, size;
    void *data;
    size_t offset;
} dds_mipmap;

typedef struct _dds_texture
{
    int fmt;
    unsigned int div_size;
    unsigned int block_bytes;
    int num_slices; /* number of faces or slices */
    int num_mipmaps; /* number of mipmaps */
    dds_mipmap **slices;
    void *data;
    size_t size;
} dds_texture;

bool dds_load(const char *filename, dds_texture **out);
bool dds_load_file(FILE *f, dds_texture **out);
void dds_free(dds_texture *tex);

#endif

