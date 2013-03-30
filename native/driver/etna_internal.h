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
/* internal definitions */
#ifndef H_ETNA_INTERNAL
#define H_ETNA_INTERNAL

enum etna_surface_layout
{
    ETNA_LAYOUT_LINEAR = 0,
    ETNA_LAYOUT_TILED = 1,
    ETNA_LAYOUT_SUPERTILED = 3 /* 1|2, both tiling and supertiling bit enabled */
};

/* GPU chip specs */
struct etna_pipe_specs
{
    /* supports SUPERTILE (64x64) tiling? */
    bool can_supertile;
    /* number of bits per TS tile */
    unsigned bits_per_tile;
    /* clear value for TS (dependent on bits_per_tile) */
    uint32_t ts_clear_value;
    /* base of vertex texture units */
    unsigned vertex_sampler_offset;
    /* needs z=(z+w)/2, for older GCxxx */
    bool vs_need_z_div;
    /* size of vertex shader output buffer */
    unsigned vertex_output_buffer_size;
    /* size of a cached vertex (?) */
    unsigned vertex_cache_size;
    /* number of shader cores */
    unsigned shader_core_count;
};


#endif

