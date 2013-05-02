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

#include <etnaviv/state.xml.h>
#include <etnaviv/state_3d.xml.h>

#define ETNA_NUM_INPUTS (16)
#define ETNA_NUM_VARYINGS (16)
#define ETNA_NUM_LOD (14)
#define ETNA_NUM_LAYERS (6)
#define ETNA_MAX_UNIFORMS (256)

enum etna_surface_layout
{
    ETNA_LAYOUT_LINEAR = 0,
    ETNA_LAYOUT_TILED = 1,
    ETNA_LAYOUT_SUPERTILED = 3 /* 1|2, both tiling and supertiling bit enabled */
};

/* GPU chip 3D specs */
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

/** Compiled Gallium state. All the different compiled state atoms are woven together and uploaded
 * only when it is necessary to synchronize the state, for example before rendering. */

/* Registers that don't fit into any other category but that are needed for setup */
struct compiled_base_setup_state
{
    uint32_t PA_W_CLIP_LIMIT;
    uint32_t GL_VERTEX_ELEMENT_CONFIG;
};

/* Compiled pipe_rasterizer_state */
struct compiled_rasterizer_state
{
    uint32_t PA_CONFIG;
    uint32_t PA_LINE_WIDTH;
    uint32_t PA_POINT_SIZE;
    uint32_t PA_SYSTEM_MODE;
    uint32_t SE_DEPTH_SCALE;
    uint32_t SE_DEPTH_BIAS;
    uint32_t SE_CONFIG;
    uint32_t VS_OUTPUT_COUNT; /* # outs added by rasterizer -- 0 or 1 */
    bool scissor;
};

/* Compiled pipe_depth_stencil_alpha_state */
struct compiled_depth_stencil_alpha_state
{
    uint32_t PE_DEPTH_CONFIG;
    uint32_t PE_ALPHA_OP;
    uint32_t PE_STENCIL_OP;
    uint32_t PE_STENCIL_CONFIG;
};

/* Compiled pipe_blend_state */
struct compiled_blend_state
{
    uint32_t PE_ALPHA_CONFIG;
    uint32_t PE_COLOR_FORMAT;
    uint32_t PE_LOGIC_OP;
    uint32_t PE_DITHER[2];
};

/* Compiled pipe_blend_color */
struct compiled_blend_color
{
    uint32_t PE_ALPHA_BLEND_COLOR;
};

/* Compiled pipe_stencil_ref */
struct compiled_stencil_ref
{
    uint32_t PE_STENCIL_CONFIG;
    uint32_t PE_STENCIL_CONFIG_EXT;
};

/* Compiled pipe_scissor_state */
struct compiled_scissor_state
{
    uint32_t SE_SCISSOR_LEFT; // fixp
    uint32_t SE_SCISSOR_TOP; // fixp
    uint32_t SE_SCISSOR_RIGHT; // fixp
    uint32_t SE_SCISSOR_BOTTOM; // fixp
};

/* Compiled pipe_viewport_state */
struct compiled_viewport_state
{
    uint32_t PA_VIEWPORT_SCALE_X;
    uint32_t PA_VIEWPORT_SCALE_Y;
    uint32_t PA_VIEWPORT_SCALE_Z;
    uint32_t PA_VIEWPORT_OFFSET_X;
    uint32_t PA_VIEWPORT_OFFSET_Y;
    uint32_t PA_VIEWPORT_OFFSET_Z;
    uint32_t PE_DEPTH_NEAR;
    uint32_t PE_DEPTH_FAR;
};

/* Compiled sample mask (context->set_sample_mask) */
struct compiled_sample_mask
{
    uint32_t GL_MULTI_SAMPLE_CONFIG;
};

/* Compiled pipe_sampler_state */
struct compiled_sampler_state
{
    /* sampler offset +4*sampler, interleave when committing state */
    uint32_t TE_SAMPLER_CONFIG0;
    uint32_t TE_SAMPLER_LOD_CONFIG;
    unsigned min_lod, max_lod;
};

/* Compiled pipe_sampler_view */
struct compiled_sampler_view
{
    /* sampler offset +4*sampler, interleave when committing state */
    uint32_t TE_SAMPLER_CONFIG0;
    uint32_t TE_SAMPLER_SIZE;
    uint32_t TE_SAMPLER_LOG_SIZE;
    uint32_t TE_SAMPLER_LOD_ADDR[VIVS_TE_SAMPLER_LOD_ADDR__LEN];
    unsigned min_lod, max_lod; /* 5.5 fixp */
};

/* Compiled pipe_framebuffer_state */
struct compiled_framebuffer_state
{
    uint32_t GL_MULTI_SAMPLE_CONFIG;
    uint32_t PE_COLOR_FORMAT;
    uint32_t PE_DEPTH_CONFIG;
    uint32_t PE_DEPTH_ADDR;
    uint32_t PE_DEPTH_STRIDE;
    uint32_t PE_HDEPTH_CONTROL;
    uint32_t PE_DEPTH_NORMALIZE;
    uint32_t PE_COLOR_ADDR;
    uint32_t PE_COLOR_STRIDE;
    uint32_t SE_SCISSOR_LEFT; // fixp, restricted by scissor state *if* enabled in rasterizer state
    uint32_t SE_SCISSOR_TOP; // fixp
    uint32_t SE_SCISSOR_RIGHT; // fixp
    uint32_t SE_SCISSOR_BOTTOM; // fixp
    uint32_t TS_MEM_CONFIG; 
    uint32_t TS_DEPTH_CLEAR_VALUE;
    uint32_t TS_DEPTH_STATUS_BASE;
    uint32_t TS_DEPTH_SURFACE_BASE;
    uint32_t TS_COLOR_CLEAR_VALUE;
    uint32_t TS_COLOR_STATUS_BASE;
    uint32_t TS_COLOR_SURFACE_BASE;
};

/* Compiled context->create_vertex_elements_state */
struct compiled_vertex_elements_state
{
    unsigned num_elements;
    uint32_t FE_VERTEX_ELEMENT_CONFIG[VIVS_FE_VERTEX_ELEMENT_CONFIG__LEN];
};

/* Compiled context->set_vertex_buffer result */
struct compiled_set_vertex_buffer
{
    uint32_t FE_VERTEX_STREAM_CONTROL;
    uint32_t FE_VERTEX_STREAM_BASE_ADDR;
};

/* Compiled context->set_index_buffer result */
struct compiled_set_index_buffer
{
    uint32_t FE_INDEX_STREAM_CONTROL;
    uint32_t FE_INDEX_STREAM_BASE_ADDR;
};

/* Compiled linked VS+PS shader state */
struct compiled_shader_state 
{
    uint32_t RA_CONTROL;
    uint32_t PA_ATTRIBUTE_ELEMENT_COUNT;
    uint32_t PA_SHADER_ATTRIBUTES[VIVS_PA_SHADER_ATTRIBUTES__LEN];
    uint32_t VS_END_PC;
    uint32_t VS_OUTPUT_COUNT;
    uint32_t VS_INPUT_COUNT;
    uint32_t VS_TEMP_REGISTER_CONTROL;
    uint32_t VS_OUTPUT[4];
    uint32_t VS_INPUT[4];
    uint32_t VS_LOAD_BALANCING; 
    uint32_t VS_START_PC;
    uint32_t PS_END_PC;
    uint32_t PS_OUTPUT_REG;
    uint32_t PS_INPUT_COUNT;
    uint32_t PS_TEMP_REGISTER_CONTROL;
    uint32_t PS_CONTROL;
    uint32_t PS_START_PC;
    uint32_t GL_VARYING_TOTAL_COMPONENTS;
    uint32_t GL_VARYING_NUM_COMPONENTS;
    uint32_t GL_VARYING_COMPONENT_USE[2];
    unsigned vs_inst_mem_size;
    unsigned vs_uniforms_size;
    unsigned ps_inst_mem_size;
    unsigned ps_uniforms_size;
    uint32_t *VS_INST_MEM;
    uint32_t VS_UNIFORMS[ETNA_MAX_UNIFORMS*4];
    uint32_t *PS_INST_MEM;
    uint32_t PS_UNIFORMS[ETNA_MAX_UNIFORMS*4];
};

/* state of all 3d and common registers relevant to etna driver */
struct etna_3d_state
{
    unsigned num_vertex_elements; /* number of elements in FE_VERTEX_ELEMENT_CONFIG */
    
    uint32_t /*00600*/ FE_VERTEX_ELEMENT_CONFIG[VIVS_FE_VERTEX_ELEMENT_CONFIG__LEN];
    uint32_t /*00644*/ FE_INDEX_STREAM_BASE_ADDR;
    uint32_t /*00648*/ FE_INDEX_STREAM_CONTROL;
    uint32_t /*0064C*/ FE_VERTEX_STREAM_BASE_ADDR;
    uint32_t /*00650*/ FE_VERTEX_STREAM_CONTROL;
    
    uint32_t /*00A00*/ PA_VIEWPORT_SCALE_X;
    uint32_t /*00A04*/ PA_VIEWPORT_SCALE_Y;
    uint32_t /*00A08*/ PA_VIEWPORT_SCALE_Z;
    uint32_t /*00A0C*/ PA_VIEWPORT_OFFSET_X;
    uint32_t /*00A10*/ PA_VIEWPORT_OFFSET_Y;
    uint32_t /*00A14*/ PA_VIEWPORT_OFFSET_Z;
    uint32_t /*00A18*/ PA_LINE_WIDTH;
    uint32_t /*00A1C*/ PA_POINT_SIZE;
    uint32_t /*00A34*/ PA_CONFIG;

    uint32_t /*00C00*/ SE_SCISSOR_LEFT; // fixp
    uint32_t /*00C04*/ SE_SCISSOR_TOP; // fixp
    uint32_t /*00C08*/ SE_SCISSOR_RIGHT; // fixp
    uint32_t /*00C0C*/ SE_SCISSOR_BOTTOM; // fixp
    uint32_t /*00C10*/ SE_DEPTH_SCALE;
    uint32_t /*00C14*/ SE_DEPTH_BIAS;
    uint32_t /*00C18*/ SE_CONFIG;

    uint32_t /*01400*/ PE_DEPTH_CONFIG;
    uint32_t /*01404*/ PE_DEPTH_NEAR;
    uint32_t /*01408*/ PE_DEPTH_FAR;
    uint32_t /*0140C*/ PE_DEPTH_NORMALIZE;
    uint32_t /*01410*/ PE_DEPTH_ADDR;
    uint32_t /*01414*/ PE_DEPTH_STRIDE;
    uint32_t /*01418*/ PE_STENCIL_OP;
    uint32_t /*0141C*/ PE_STENCIL_CONFIG;
    uint32_t /*01420*/ PE_ALPHA_OP;
    uint32_t /*01424*/ PE_ALPHA_BLEND_COLOR;
    uint32_t /*01428*/ PE_ALPHA_CONFIG;
    uint32_t /*0142C*/ PE_COLOR_FORMAT;
    uint32_t /*01430*/ PE_COLOR_ADDR;
    uint32_t /*01434*/ PE_COLOR_STRIDE;
    uint32_t /*01454*/ PE_HDEPTH_CONTROL;
    uint32_t /*014A0*/ PE_STENCIL_CONFIG_EXT;
    uint32_t /*014A4*/ PE_LOGIC_OP;
    uint32_t /*014A8*/ PE_DITHER[2];
    
    uint32_t /*01604*/ RS_CONFIG;
    uint32_t /*01608*/ RS_SOURCE_ADDR;
    uint32_t /*0160C*/ RS_SOURCE_STRIDE;
    uint32_t /*01610*/ RS_DEST_ADDR;
    uint32_t /*01614*/ RS_DEST_STRIDE;
    uint32_t /*01620*/ RS_WINDOW_SIZE;
    uint32_t /*01630*/ RS_DITHER[2];
    uint32_t /*0163C*/ RS_CLEAR_CONTROL;
    uint32_t /*01640*/ RS_FILL_VALUE[4];

    uint32_t /*01654*/ TS_MEM_CONFIG; 
    uint32_t /*01658*/ TS_COLOR_STATUS_BASE;
    uint32_t /*0165C*/ TS_COLOR_SURFACE_BASE;
    uint32_t /*01660*/ TS_COLOR_CLEAR_VALUE;
    uint32_t /*01664*/ TS_DEPTH_STATUS_BASE;
    uint32_t /*01668*/ TS_DEPTH_SURFACE_BASE;
    uint32_t /*0166C*/ TS_DEPTH_CLEAR_VALUE;
    
    uint32_t /*016A0*/ RS_EXTRA_CONFIG;
    
    uint32_t /*02000*/ TE_SAMPLER_CONFIG0[VIVS_TE_SAMPLER__LEN];
    uint32_t /*02040*/ TE_SAMPLER_SIZE[VIVS_TE_SAMPLER__LEN];
    uint32_t /*02080*/ TE_SAMPLER_LOG_SIZE[VIVS_TE_SAMPLER__LEN];
    uint32_t /*020C0*/ TE_SAMPLER_LOD_CONFIG[VIVS_TE_SAMPLER__LEN];
    uint32_t /*02400*/ TE_SAMPLER_LOD_ADDR[VIVS_TE_SAMPLER_LOD_ADDR__LEN][VIVS_TE_SAMPLER__LEN];
    
    uint32_t /*03818*/ GL_MULTI_SAMPLE_CONFIG;
};


#endif

