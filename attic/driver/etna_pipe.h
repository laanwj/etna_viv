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
/* Gallium driver main header file
 */
#ifndef H_ETNA_PIPE
#define H_ETNA_PIPE

#include <stdint.h>
#include <etnaviv/etna.h>
#include <etnaviv/etna_bo.h>
#include <etnaviv/etna_rs.h>
#include <etnaviv/etna_tex.h>

#include "etna_internal.h"
#include "pipe/p_defines.h"
#include "pipe/p_format.h"
#include "pipe/p_shader_tokens.h"
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "util/u_slab.h"

struct pipe_screen;

struct etna_shader_input
{
    int vs_reg; /* VS input register */
};

enum etna_varying_special {
    ETNA_VARYING_VSOUT = 0, /* from VS */
    ETNA_VARYING_POINTCOORD, /* point texture coord */
};

struct etna_shader_varying
{
    int num_components;
    enum etna_varying_special special;
    int pa_attributes;
    int vs_reg; /* VS output register */
};

struct etna_resource_level
{
   unsigned width, padded_width;
   unsigned height, padded_height;
   unsigned offset; /* offset into memory area */
   uint32_t stride; /* row stride */
   uint32_t layer_stride; /* layer stride */
   unsigned size; /* total size of memory area */

   uint32_t ts_offset;
   uint32_t ts_size;
   uint32_t clear_value; /* clear value of resource level (mainly for TS) */
};

struct etna_resource
{
    struct pipe_resource base;

    /* only lod 0 used for non-texture buffers */
    /* Layout for surface (tiled, multitiled, split tiled, ...) */
    enum etna_surface_layout layout;
    /* Horizontal alignment for texture unit (TEXTURE_HALIGN_*) */
    unsigned halign;
    struct etna_bo *bo; /* Surface video memory */
    struct etna_bo *ts_bo; /* Tile status video memory */

    struct etna_resource_level levels[ETNA_NUM_LOD];
    struct etna_pipe_context *last_ctx; /* Last bound context */

    uint32_t pipe_addr[2];
};

struct etna_surface
{
    struct pipe_surface base;

    enum etna_surface_layout layout;
    struct etna_resource_level surf;
    struct compiled_rs_state clear_command;
    /* Keep pointer to resource level, for fast clear */
    struct etna_resource_level *level;
};

struct etna_sampler_view
{
    struct pipe_sampler_view base;

    struct compiled_sampler_view *internal;
};

struct etna_transfer
{
    struct pipe_transfer base;

    /* Pointer to buffer (same pointer as returned by transfer_map) */
    void *buffer;
    /* If true, transfer happens in-place. buffer is not allocated separately but
     * points into the actual resource, and thus does not need to be copied or freed.
     */
    bool in_place;
};

/* group all current CSOs, for dirty bits */
enum
{
    ETNA_STATE_BLEND = (1<<1),
    ETNA_STATE_SAMPLERS = (1<<2),
    ETNA_STATE_RASTERIZER = (1<<3),
    ETNA_STATE_DSA = (1<<4),
    ETNA_STATE_VERTEX_ELEMENTS = (1<<5),
    ETNA_STATE_BLEND_COLOR = (1<<6),
    ETNA_STATE_STENCIL_REF = (1<<7),
    ETNA_STATE_SAMPLE_MASK = (1<<8),
    ETNA_STATE_VIEWPORT = (1<<9),
    ETNA_STATE_FRAMEBUFFER = (1<<10),
    ETNA_STATE_SCISSOR = (1<<11),
    ETNA_STATE_SAMPLER_VIEWS = (1<<12),
    ETNA_STATE_VERTEX_BUFFERS = (1<<13),
    ETNA_STATE_INDEX_BUFFER = (1<<14),
    ETNA_STATE_SHADER = (1<<15),
    ETNA_STATE_VS_UNIFORMS = (1<<16),
    ETNA_STATE_PS_UNIFORMS = (1<<17),
    ETNA_STATE_TS = (1<<18), /* set after clear and when RS blit operations from other surface affect TS */
    ETNA_STATE_TEXTURE_CACHES = (1<<19) /* set when texture has been modified/uploaded */
};

/* private opaque context structure */
struct etna_pipe_context
{
    struct pipe_context base;
    struct viv_conn *conn;
    struct etna_ctx *ctx;
    unsigned dirty_bits;
    struct etna_pipe_specs specs;
    struct util_slab_mempool transfer_pool;
    struct blitter_context *blitter;

    /* compiled bindable state */
    struct compiled_blend_state blend;
    unsigned num_vertex_samplers;
    unsigned num_fragment_samplers;
    struct compiled_sampler_state sampler[PIPE_MAX_SAMPLERS];
    struct compiled_rasterizer_state rasterizer;
    struct compiled_depth_stencil_alpha_state depth_stencil_alpha;
    struct compiled_vertex_elements_state vertex_elements;
    struct compiled_shader_state shader_state;

    /* compiled parameter-like state */
    struct compiled_blend_color blend_color;
    struct compiled_stencil_ref stencil_ref;
    struct compiled_sample_mask sample_mask;
    struct compiled_framebuffer_state framebuffer;
    struct compiled_scissor_state scissor;
    struct compiled_viewport_state viewport;
    unsigned num_fragment_sampler_views;
    unsigned num_vertex_sampler_views;
    struct compiled_sampler_view sampler_view[PIPE_MAX_SAMPLERS];
    struct compiled_set_vertex_buffer vertex_buffer[PIPE_MAX_ATTRIBS];
    struct compiled_set_index_buffer index_buffer;

    /* pointers to the bound state. these are mainly kept around for the blitter. */
    struct compiled_blend_state *blend_p;
    struct compiled_sampler_state *sampler_p[PIPE_MAX_SAMPLERS];
    struct compiled_rasterizer_state *rasterizer_p;
    struct compiled_depth_stencil_alpha_state *depth_stencil_alpha_p;
    struct compiled_vertex_elements_state *vertex_elements_p;
    struct etna_shader_object *vs;
    struct etna_shader_object *fs;

    /* saved parameter-like state. these are mainly kept around for the blitter. */
    struct pipe_framebuffer_state framebuffer_s;
    unsigned sample_mask_s;
    struct pipe_stencil_ref stencil_ref_s;
    struct pipe_viewport_state viewport_s;
    struct pipe_scissor_state scissor_s;
    struct pipe_sampler_view *sampler_view_s[PIPE_MAX_SAMPLERS];
    struct pipe_vertex_buffer vertex_buffer_s[PIPE_MAX_ATTRIBS];
    struct pipe_index_buffer index_buffer_s;
    struct pipe_constant_buffer vs_cbuf_s;
    struct pipe_constant_buffer fs_cbuf_s;

    /* cached state of entire GPU */
    struct etna_3d_state gpu3d;
};

static INLINE struct etna_pipe_context *
etna_pipe_context(struct pipe_context *p)
{
    return (struct etna_pipe_context *)p;
}

static INLINE struct etna_resource *
etna_resource(struct pipe_resource *p)
{
    return (struct etna_resource *)p;
}

static INLINE struct etna_surface *
etna_surface(struct pipe_surface *p)
{
    return (struct etna_surface *)p;
}

static INLINE struct etna_sampler_view *
etna_sampler_view(struct pipe_sampler_view *p)
{
    return (struct etna_sampler_view *)p;
}

static INLINE struct etna_transfer *
etna_transfer(struct pipe_transfer *p)
{
    return (struct etna_transfer *)p;
}

struct pipe_context *etna_new_pipe_context(struct viv_conn *dev, const struct etna_pipe_specs *specs, struct pipe_screen *scr, void *priv);

#endif

