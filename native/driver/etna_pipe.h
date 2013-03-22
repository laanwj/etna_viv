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
#include "etna_internal.h"
#include "etna_mem.h"
#include "etna_rs.h"
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

#define ETNA_NUM_INPUTS (16)
#define ETNA_NUM_VARYINGS (16)
#define ETNA_NUM_LOD (14)
#define ETNA_NUM_LAYERS (6)

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


struct etna_shader_program 
{
    unsigned num_inputs;
    struct etna_shader_input inputs[ETNA_NUM_INPUTS];
    unsigned num_varyings;
    struct etna_shader_varying varyings[ETNA_NUM_VARYINGS]; 
    
    unsigned vs_code_size; /* Vertex shader code size in words */ 
    uint32_t *vs_code;
    unsigned vs_pos_out_reg; /* VS position output */
    unsigned vs_pointsize_out_reg; /* VS point size output */
    unsigned vs_load_balancing;
    unsigned vs_num_temps; /* number of temporaries, can never be less than num_varyings+1 */
    unsigned vs_uniforms_size; /* Size of uniforms (in words) */
    uint32_t *vs_uniforms; /* Initial values for VS uniforms */

    unsigned ps_code_size; /* Pixel shader code size in words */
    uint32_t *ps_code;
    unsigned ps_color_out_reg; /* color output register */
    unsigned ps_num_temps; /* number of temporaries, can never be less than num_varyings+1 */;
    unsigned ps_uniforms_size; /* Size of uniforms (in words) */
    uint32_t *ps_uniforms; /* Initial values for VS uniforms */
};

struct etna_resource_level
{
   unsigned width, padded_width;
   unsigned height, padded_height;
   unsigned offset; /* offset into memory area */
   unsigned size; /* size of memory area */

   uint32_t address; /* cached GPU pointers to LODs */
   void *logical; /* cached CPU pointer */
   uint32_t ts_address;
   uint32_t ts_size;
   uint32_t stride; /* VIVS_PE_(COLOR|DEPTH)_STRIDE */
   uint32_t layer_stride;
};

struct etna_resource
{
    struct pipe_resource base;

    /* only lod 0 used for non-texture buffers */
    enum etna_surface_layout layout;
    etna_vidmem *surface; /* Surface video memory */
    etna_vidmem *ts; /* Tile status video memory */

    struct etna_resource_level levels[ETNA_NUM_LOD];
    /* XXX uint32_t clear_value; */
};

struct etna_surface
{
    struct pipe_surface base;
   
    enum etna_surface_layout layout;
    struct etna_resource_level surf;
    uint32_t clear_value; // XXX remember depth/stencil clear value from ->clear
    struct compiled_rs_state clear_command;
};

struct etna_sampler_view
{
    struct pipe_sampler_view base;

    struct compiled_sampler_view *internal;
};

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

struct pipe_context *etna_new_pipe_context(etna_ctx *ctx);

/* Allocate 2D texture or render target resource 
 */
struct pipe_resource *etna_pipe_create_2d(struct pipe_context *pipe, unsigned flags, unsigned format, unsigned width, unsigned height, unsigned max_mip_level);

/* Allocate buffer resource.
 * Like with gallium, a buffer is essentially an 1D texture of width size, and format PIPE_FORMAT_R8_UNORM, and target
 * PIPE_BUFFER (see u_inlines.h in mesa tree).
 */
struct pipe_resource *etna_pipe_create_buffer(struct pipe_context *pipe, unsigned flags, unsigned size);

/* Free previously allocated resource */
void etna_pipe_destroy_resource(struct pipe_context *pipe, struct pipe_resource *resource);

/* Temporary entry point to get access to the memory behind a resource.
 * Eventually we should use pipe transfers and lock/unlock like gallium,
 * to enable sane handling of cache etc, but for now at least it's better than directly accessing the internal structure
 * XXX tiling textures need currently be done manually using etna_texture_tile
 */
void *etna_pipe_get_resource_ptr(struct pipe_context *pipe, struct pipe_resource *resource, unsigned layer, unsigned level);

/** 
 * One-shot write to texture or buffer, similar to gallium transfer_inline_write but somewhat more limited right now.
 * Does tiling if needed.
 * XXX no stride parameter, assumes that data is tightly packed.
 */
void etna_pipe_inline_write(struct pipe_context *pipe, struct pipe_resource *resource, unsigned layer, unsigned level, void *data, size_t size);

#endif

