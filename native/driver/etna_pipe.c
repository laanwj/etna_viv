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
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>

#include <errno.h>

#include "etna_pipe.h"
#include "etna_translate.h"

#include "common.xml.h"
#include "state.xml.h"
#include "state_3d.xml.h"
#include "cmdstream.xml.h"

#include "write_bmp.h"
#include "viv.h"
#include "etna.h"
#include "etna_state.h"
#include "etna_rs.h"
#include "etna_fb.h"
#include "etna_mem.h"
#include "etna_bswap.h"
#include "etna_tex.h"
#include "etna_util.h"
#include "etna_shader.h"

#include "minigallium.h"

/* Define state */
#define SET_STATE(addr, value) cs->addr = (value)
#define SET_STATE_FIXP(addr, value) cs->addr = (value)
#define SET_STATE_F32(addr, value) cs->addr = etna_f32_to_u32(value)

/*********************************************************************/
/** Gallium state compilation */
struct compiled_base_setup_state
{
    uint32_t PA_W_CLIP_LIMIT;
    uint32_t GL_VERTEX_ELEMENT_CONFIG;
};

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

struct compiled_depth_stencil_alpha_state
{
    uint32_t PE_DEPTH_CONFIG;
    uint32_t PE_ALPHA_OP;
    uint32_t PE_STENCIL_OP;
    uint32_t PE_STENCIL_CONFIG;
};

struct compiled_blend_state
{
    uint32_t PE_ALPHA_CONFIG;
    uint32_t PE_COLOR_FORMAT;
    uint32_t PE_LOGIC_OP;
    uint32_t PE_DITHER[2];
};

struct compiled_blend_color
{
    uint32_t PE_ALPHA_BLEND_COLOR;
};

struct compiled_stencil_ref
{
    uint32_t PE_STENCIL_CONFIG;
    uint32_t PE_STENCIL_CONFIG_EXT;
};

struct compiled_scissor_state
{
    uint32_t SE_SCISSOR_LEFT; // fixp
    uint32_t SE_SCISSOR_TOP; // fixp
    uint32_t SE_SCISSOR_RIGHT; // fixp
    uint32_t SE_SCISSOR_BOTTOM; // fixp
};

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

struct compiled_sample_mask
{
    uint32_t GL_MULTI_SAMPLE_CONFIG;
};

struct compiled_sampler_state
{
    /* sampler offset +4*sampler, interleave when committing state */
    uint32_t TE_SAMPLER_CONFIG0;
    uint32_t TE_SAMPLER_LOD_CONFIG;
    unsigned min_lod, max_lod;
};

struct compiled_sampler_view
{
    /* sampler offset +4*sampler, interleave when committing state */
    uint32_t TE_SAMPLER_CONFIG0;
    uint32_t TE_SAMPLER_SIZE;
    uint32_t TE_SAMPLER_LOG_SIZE;
    uint32_t TE_SAMPLER_LOD_ADDR[VIVS_TE_SAMPLER_LOD_ADDR__LEN];
    unsigned min_lod, max_lod; /* 5.5 fixp */
};

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

struct compiled_vertex_elements_state
{
    unsigned num_elements;
    uint32_t FE_VERTEX_ELEMENT_CONFIG[VIVS_FE_VERTEX_ELEMENT_CONFIG__LEN];
};

struct compiled_set_vertex_buffer
{
    uint32_t FE_VERTEX_STREAM_CONTROL;
    uint32_t FE_VERTEX_STREAM_BASE_ADDR;
};

struct compiled_set_index_buffer
{
    uint32_t FE_INDEX_STREAM_CONTROL;
    uint32_t FE_INDEX_STREAM_BASE_ADDR;
};

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

/* group all current CSOs, for dirty bits */
enum
{
    ETNA_STATE_BASE_SETUP = (1<<0), /* basic openGL setup */
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
    ETNA_STATE_TS = (1<<18) /* set after clear and when RS blit operations from other surface affect TS */
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

/* private opaque context structure */
struct etna_pipe_context_priv
{
    struct viv_conn *conn;
    struct etna_ctx *ctx;
    unsigned dirty_bits;
    struct pipe_framebuffer_state framebuffer_s;
    struct etna_pipe_specs specs;

    /* constant */
    struct compiled_base_setup_state base_setup;

    /* bindable state */
    struct compiled_blend_state *blend;
    unsigned num_vertex_samplers;
    unsigned num_fragment_samplers;
    struct compiled_sampler_state *sampler[PIPE_MAX_SAMPLERS];
    struct compiled_rasterizer_state *rasterizer;
    struct compiled_depth_stencil_alpha_state *depth_stencil_alpha;
    struct compiled_vertex_elements_state *vertex_elements;
    struct compiled_shader_state shader_state;
    struct etna_shader_object *vs;
    struct etna_shader_object *fs;

    /* parameter-like state */
    struct compiled_blend_color blend_color;
    struct compiled_stencil_ref stencil_ref;
    struct compiled_sample_mask sample_mask;
    struct compiled_framebuffer_state framebuffer;
    struct compiled_scissor_state scissor;
    struct compiled_viewport_state viewport;
    unsigned num_fragment_sampler_views;
    unsigned num_vertex_sampler_views;
    struct compiled_sampler_view sampler_view[PIPE_MAX_SAMPLERS];
    struct compiled_set_vertex_buffer vertex_buffer;
    struct compiled_set_index_buffer index_buffer;

    /* cached state */
    struct etna_3d_state gpu3d;
};

/*********************************************************************/
#define ETNA_PIPE(pipe) ((struct etna_pipe_context_priv*)(pipe)->priv)

/* Create bit field of which samplers are active and thus need to be programmed
 * 32 bits is enough for 32 samplers. 
 */
static uint32_t active_samplers_bits(struct pipe_context *pipe)
{
    struct etna_pipe_context_priv *restrict e = ETNA_PIPE(pipe);
    unsigned num_fragment_samplers = etna_umin(e->num_fragment_samplers, e->num_fragment_sampler_views);
    unsigned num_vertex_samplers = etna_umin(e->num_vertex_samplers, e->num_vertex_sampler_views);
    uint32_t active_samplers = etna_bits_ones(num_fragment_samplers) |
                               etna_bits_ones(num_vertex_samplers) << e->specs.vertex_sampler_offset;
    return active_samplers;
}

/* fill in shader_state from vs and fs
 * as this function is called every time a new fs or vs is bound, the goal is to do little code as possible here,
 * and to precompute as much as possible per vs/fs shader_object.
 */
static void etna_link_shaders(struct pipe_context *pipe,
                              struct compiled_shader_state *cs, 
                              const struct etna_shader_object *vs, const struct etna_shader_object *fs)
{
    assert(vs->processor == TGSI_PROCESSOR_VERTEX);
    assert(fs->processor == TGSI_PROCESSOR_FRAGMENT);
    etna_dump_shader_object(vs);
    etna_dump_shader_object(fs);

    /* set last_varying_2x flag if the last varying has 1 or 2 components */
    bool last_varying_2x = false;
    if(fs->num_inputs>0 && fs->inputs[fs->num_inputs-1].num_components <= 2)
        last_varying_2x = true;

    SET_STATE(RA_CONTROL, VIVS_RA_CONTROL_UNK0 |
                          (last_varying_2x ? VIVS_RA_CONTROL_LAST_VARYING_2X : 0));

    SET_STATE(PA_ATTRIBUTE_ELEMENT_COUNT, VIVS_PA_ATTRIBUTE_ELEMENT_COUNT_COUNT(fs->num_inputs));
    for(int idx=0; idx<fs->num_inputs; ++idx)
        SET_STATE(PA_SHADER_ATTRIBUTES[idx], fs->inputs[idx].pa_attributes);

    SET_STATE(VS_END_PC, vs->code_size / 4);
    SET_STATE(VS_OUTPUT_COUNT, fs->num_inputs + 1); /* position + varyings */
    /* Number of vertex elements determines number of VS inputs. Otherwise, the GPU crashes */
    SET_STATE(VS_INPUT_COUNT, VIVS_VS_INPUT_COUNT_UNK8(vs->input_count_unk8));
    SET_STATE(VS_TEMP_REGISTER_CONTROL,
                              VIVS_VS_TEMP_REGISTER_CONTROL_NUM_TEMPS(vs->num_temps));

    /* link vs outputs to fs inputs */
    struct etna_shader_link_info link = {};
    if(etna_link_shader_objects(&link, vs, fs))
    {
        assert(0); /* linking failed: some fs inputs do not have corresponding vs outputs */
    }
    printf("link result:\n");
    for(int idx=0; idx<fs->num_inputs; ++idx)
    {
        printf("  %i -> %i\n", link.varyings_vs_reg[idx], idx+1);
    }

    /* vs outputs (varyings) */ 
    uint32_t vs_output[16] = {0};
    int varid = 0;
    vs_output[varid++] = vs->vs_pos_out_reg;
    for(int idx=0; idx<fs->num_inputs; ++idx)
        vs_output[varid++] = link.varyings_vs_reg[idx];
    if(vs->vs_pointsize_out_reg >= 0)
        vs_output[varid++] = vs->vs_pointsize_out_reg; /* pointsize is last */

    for(int idx=0; idx<4; ++idx)
    {
        SET_STATE(VS_OUTPUT[idx],(vs_output[idx*4+0] << 0)  | (vs_output[idx*4+1] << 8) | 
                                 (vs_output[idx*4+2] << 16) | (vs_output[idx*4+3] << 24));
    }
    
    /* vs inputs (attributes) */
    uint32_t vs_input[4] = {0};
    for(int idx=0; idx<vs->num_inputs; ++idx)
        vs_input[idx/4] |= vs->inputs[idx].reg << ((idx%4)*8);
    for(int idx=0; idx<4; ++idx)
        SET_STATE(VS_INPUT[idx], vs_input[idx]);

    SET_STATE(VS_LOAD_BALANCING, vs->vs_load_balancing);
    SET_STATE(VS_START_PC, 0);

    SET_STATE(PS_END_PC, fs->code_size / 4);
    SET_STATE(PS_OUTPUT_REG, fs->ps_color_out_reg);
    SET_STATE(PS_INPUT_COUNT, VIVS_PS_INPUT_COUNT_COUNT(fs->num_inputs + 1) |  /* XXX MSAA adds another input */
                              VIVS_PS_INPUT_COUNT_UNK8(fs->input_count_unk8));
    SET_STATE(PS_TEMP_REGISTER_CONTROL,
                              VIVS_PS_TEMP_REGISTER_CONTROL_NUM_TEMPS(fs->num_temps));
    SET_STATE(PS_CONTROL, VIVS_PS_CONTROL_UNK1);
    SET_STATE(PS_START_PC, 0);

    uint32_t total_components = 0;
    uint32_t num_components = 0;
    uint32_t component_use[2] = {0};
    for(int idx=0; idx<fs->num_inputs; ++idx)
    {
        num_components |= fs->inputs[idx].num_components << ((idx%8)*4);
        for(int comp=0; comp<fs->inputs[idx].num_components; ++comp)
        {
            unsigned use = VARYING_COMPONENT_USE_USED;
            if(fs->inputs[idx].semantic.Name == TGSI_SEMANTIC_PCOORD)
            {
                if(comp == 0)
                    use = VARYING_COMPONENT_USE_POINTCOORD_X;
                else if(comp == 1)
                    use = VARYING_COMPONENT_USE_POINTCOORD_Y;
            }
            /* 16 components per uint32 */
            component_use[total_components/16] |= use << ((total_components%16)*2);
            total_components += 1;
        }
    }
    SET_STATE(GL_VARYING_TOTAL_COMPONENTS, VIVS_GL_VARYING_TOTAL_COMPONENTS_NUM(etna_align_up(total_components, 2)));
    SET_STATE(GL_VARYING_NUM_COMPONENTS, num_components);
    SET_STATE(GL_VARYING_COMPONENT_USE[0], component_use[0]);
    SET_STATE(GL_VARYING_COMPONENT_USE[1], component_use[1]);
    
    /* reference instruction memory */
#if 0
    {
        int fd=creat("/mnt/sdcard/shader_vs.bin", 0777);
        write(fd, vs->code, vs->code_size*4);
        close(fd);
        fd=creat("/mnt/sdcard/shader_ps.bin", 0777);
        write(fd, fs->code, fs->code_size*4);
        close(fd);
    }
#endif
    cs->vs_inst_mem_size = vs->code_size;
    cs->VS_INST_MEM = vs->code;
    cs->ps_inst_mem_size = fs->code_size;
    cs->PS_INST_MEM = fs->code;

    /* uniforms layout -- first constants, then immediates */
    cs->vs_uniforms_size = vs->const_size + vs->imm_size;
    memcpy(&cs->VS_UNIFORMS[vs->imm_base], vs->imm_data, vs->imm_size*4);

    cs->ps_uniforms_size = fs->const_size + fs->imm_size;
    memcpy(&cs->PS_UNIFORMS[fs->imm_base], fs->imm_data, fs->imm_size*4);
}
    
/* Weave state */
static void sync_context(struct pipe_context *pipe)
{
    struct etna_pipe_context_priv *restrict e = ETNA_PIPE(pipe);
    struct etna_ctx *restrict ctx = e->ctx;
    uint32_t active_samplers = active_samplers_bits(pipe);

    uint32_t dirty = e->dirty_bits;
    e->gpu3d.num_vertex_elements = e->vertex_elements->num_elements;

    /* state must be bound */
    assert(e->blend && e->rasterizer && e->depth_stencil_alpha && e->vertex_elements);

    if((dirty & ETNA_STATE_SHADER) && e->vs && e->fs)
    {
        /* re-link vs and fs if needed 
         * XXX this contains intermediate copy steps that could be avoided
         */
        etna_link_shaders(pipe, &e->shader_state, e->vs, e->fs);
    }

    /* XXX todo: 
     * - caching, don't re-emit cached state ?
     * - group consecutive states into one LOAD_STATE command stream
     * - update context
     * - flush texture? 
     * - flush depth/color on depth/color framebuffer change
     */
#define EMIT_STATE(state_name, dest_field, src_value) etna_set_state(ctx, VIVS_##state_name, (src_value)); 
#define EMIT_STATE_FIXP(state_name, dest_field, src_value) etna_set_state_fixp(ctx, VIVS_##state_name, (src_value)); 
    /* The following code is mostly generated by gen_merge_state.py, to emit state in sorted order by address */
    /* manual changes involve combining state if the operation is not simply bitwise or: 
     * - scissor fixp
     * - num vertex elements
     * - scissor handling
     * - num samplers
     * - texture lod 
     * - ETNA_STATE_TS
     */
    if(dirty & (ETNA_STATE_VERTEX_ELEMENTS))
    {
        for(int x=0; x<e->vertex_elements->num_elements; ++x)
        {
            /*00600*/ EMIT_STATE(FE_VERTEX_ELEMENT_CONFIG(x), FE_VERTEX_ELEMENT_CONFIG[x], e->vertex_elements->FE_VERTEX_ELEMENT_CONFIG[x]);
        }
    }
    if(dirty & (ETNA_STATE_INDEX_BUFFER))
    {
        /*00644*/ EMIT_STATE(FE_INDEX_STREAM_BASE_ADDR, FE_INDEX_STREAM_BASE_ADDR, e->index_buffer.FE_INDEX_STREAM_BASE_ADDR);
        /*00648*/ EMIT_STATE(FE_INDEX_STREAM_CONTROL, FE_INDEX_STREAM_CONTROL, e->index_buffer.FE_INDEX_STREAM_CONTROL);
    }
    if(dirty & (ETNA_STATE_VERTEX_BUFFERS))
    {
        /*0064C*/ EMIT_STATE(FE_VERTEX_STREAM_BASE_ADDR, FE_VERTEX_STREAM_BASE_ADDR, e->vertex_buffer.FE_VERTEX_STREAM_BASE_ADDR);
        /*00650*/ EMIT_STATE(FE_VERTEX_STREAM_CONTROL, FE_VERTEX_STREAM_CONTROL, e->vertex_buffer.FE_VERTEX_STREAM_CONTROL);
    }
    if(dirty & (ETNA_STATE_SHADER))
    {
        /*00800*/ EMIT_STATE(VS_END_PC, VS_END_PC, e->shader_state.VS_END_PC);
    }
    if(dirty & (ETNA_STATE_SHADER | ETNA_STATE_RASTERIZER))
    {
        /*00804*/ EMIT_STATE(VS_OUTPUT_COUNT, VS_OUTPUT_COUNT, e->shader_state.VS_OUTPUT_COUNT + e->rasterizer->VS_OUTPUT_COUNT);
    }
    if(dirty & (ETNA_STATE_VERTEX_ELEMENTS | ETNA_STATE_SHADER))
    {
        /*00808*/ EMIT_STATE(VS_INPUT_COUNT, VS_INPUT_COUNT, VIVS_VS_INPUT_COUNT_COUNT(e->vertex_elements->num_elements) | e->shader_state.VS_INPUT_COUNT);
    }
    if(dirty & (ETNA_STATE_SHADER))
    {
        /*0080C*/ EMIT_STATE(VS_TEMP_REGISTER_CONTROL, VS_TEMP_REGISTER_CONTROL, e->shader_state.VS_TEMP_REGISTER_CONTROL);
        for(int x=0; x<4; ++x)
        {
            /*00810*/ EMIT_STATE(VS_OUTPUT(x), VS_OUTPUT[x], e->shader_state.VS_OUTPUT[x]);
        }
        for(int x=0; x<4; ++x)
        {
            /*00820*/ EMIT_STATE(VS_INPUT(x), VS_INPUT[x], e->shader_state.VS_INPUT[x]);
        }
        /*00830*/ EMIT_STATE(VS_LOAD_BALANCING, VS_LOAD_BALANCING, e->shader_state.VS_LOAD_BALANCING);
        /*00838*/ EMIT_STATE(VS_START_PC, VS_START_PC, e->shader_state.VS_START_PC);
    }
    if(dirty & (ETNA_STATE_VIEWPORT))
    {
        /*00A00*/ EMIT_STATE(PA_VIEWPORT_SCALE_X, PA_VIEWPORT_SCALE_X, e->viewport.PA_VIEWPORT_SCALE_X);
        /*00A04*/ EMIT_STATE(PA_VIEWPORT_SCALE_Y, PA_VIEWPORT_SCALE_Y, e->viewport.PA_VIEWPORT_SCALE_Y);
        /*00A08*/ EMIT_STATE(PA_VIEWPORT_SCALE_Z, PA_VIEWPORT_SCALE_Z, e->viewport.PA_VIEWPORT_SCALE_Z);
        /*00A0C*/ EMIT_STATE(PA_VIEWPORT_OFFSET_X, PA_VIEWPORT_OFFSET_X, e->viewport.PA_VIEWPORT_OFFSET_X);
        /*00A10*/ EMIT_STATE(PA_VIEWPORT_OFFSET_Y, PA_VIEWPORT_OFFSET_Y, e->viewport.PA_VIEWPORT_OFFSET_Y);
        /*00A14*/ EMIT_STATE(PA_VIEWPORT_OFFSET_Z, PA_VIEWPORT_OFFSET_Z, e->viewport.PA_VIEWPORT_OFFSET_Z);
    }
    if(dirty & (ETNA_STATE_RASTERIZER))
    {
        /*00A18*/ EMIT_STATE(PA_LINE_WIDTH, PA_LINE_WIDTH, e->rasterizer->PA_LINE_WIDTH);
        /*00A1C*/ EMIT_STATE(PA_POINT_SIZE, PA_POINT_SIZE, e->rasterizer->PA_POINT_SIZE);
        /*00A28*/ EMIT_STATE(PA_SYSTEM_MODE, PA_SYSTEM_MODE, e->rasterizer->PA_SYSTEM_MODE);
    }
    if(dirty & (ETNA_STATE_BASE_SETUP))
    {
        /*00A2C*/ EMIT_STATE(PA_W_CLIP_LIMIT, PA_W_CLIP_LIMIT, e->base_setup.PA_W_CLIP_LIMIT);
    }
    if(dirty & (ETNA_STATE_SHADER))
    {
        /*00A30*/ EMIT_STATE(PA_ATTRIBUTE_ELEMENT_COUNT, PA_ATTRIBUTE_ELEMENT_COUNT, e->shader_state.PA_ATTRIBUTE_ELEMENT_COUNT);
    }
    if(dirty & (ETNA_STATE_RASTERIZER))
    {
        /*00A34*/ EMIT_STATE(PA_CONFIG, PA_CONFIG, e->rasterizer->PA_CONFIG);
    }
    if(dirty & (ETNA_STATE_SHADER))
    {
        for(int x=0; x<10; ++x)
        {
            /*00A40*/ EMIT_STATE(PA_SHADER_ATTRIBUTES(x), PA_SHADER_ATTRIBUTES[x], e->shader_state.PA_SHADER_ATTRIBUTES[x]);
        }
    }
    if(dirty & (ETNA_STATE_SCISSOR | ETNA_STATE_FRAMEBUFFER | ETNA_STATE_RASTERIZER))
    {
        /* this is a bit of a mess: rasterizer->scissor determines whether to use only the
         * framebuffer scissor, or specific scissor state, so the logic spans three CSOs 
         */
        uint32_t scissor_left = e->framebuffer.SE_SCISSOR_LEFT;
        uint32_t scissor_top = e->framebuffer.SE_SCISSOR_TOP;
        uint32_t scissor_right = e->framebuffer.SE_SCISSOR_RIGHT;
        uint32_t scissor_bottom = e->framebuffer.SE_SCISSOR_BOTTOM;
        if(e->rasterizer->scissor)
        {
            scissor_left = etna_umax(e->scissor.SE_SCISSOR_LEFT, scissor_left);
            scissor_top = etna_umax(e->scissor.SE_SCISSOR_TOP, scissor_top);
            scissor_right = etna_umax(e->scissor.SE_SCISSOR_RIGHT, scissor_right);
            scissor_bottom = etna_umax(e->scissor.SE_SCISSOR_RIGHT, scissor_bottom);
        }
        /*00C00*/ EMIT_STATE_FIXP(SE_SCISSOR_LEFT, SE_SCISSOR_LEFT, scissor_left);
        /*00C04*/ EMIT_STATE_FIXP(SE_SCISSOR_TOP, SE_SCISSOR_TOP, scissor_top);
        /*00C08*/ EMIT_STATE_FIXP(SE_SCISSOR_RIGHT, SE_SCISSOR_RIGHT, scissor_right);
        /*00C0C*/ EMIT_STATE_FIXP(SE_SCISSOR_BOTTOM, SE_SCISSOR_BOTTOM, scissor_bottom);
    }
    if(dirty & (ETNA_STATE_RASTERIZER))
    {
        /*00C10*/ EMIT_STATE(SE_DEPTH_SCALE, SE_DEPTH_SCALE, e->rasterizer->SE_DEPTH_SCALE);
        /*00C14*/ EMIT_STATE(SE_DEPTH_BIAS, SE_DEPTH_BIAS, e->rasterizer->SE_DEPTH_BIAS);
        /*00C18*/ EMIT_STATE(SE_CONFIG, SE_CONFIG, e->rasterizer->SE_CONFIG);
    }
    if(dirty & (ETNA_STATE_SHADER))
    {
        /*00E00*/ EMIT_STATE(RA_CONTROL, RA_CONTROL, e->shader_state.RA_CONTROL);
        /*01000*/ EMIT_STATE(PS_END_PC, PS_END_PC, e->shader_state.PS_END_PC);
        /*01004*/ EMIT_STATE(PS_OUTPUT_REG, PS_OUTPUT_REG, e->shader_state.PS_OUTPUT_REG);
        /* XXX affected by supersampling as well (supersampling adds an input, and possible temp) */
        /*01008*/ EMIT_STATE(PS_INPUT_COUNT, PS_INPUT_COUNT, e->shader_state.PS_INPUT_COUNT);
        /*0100C*/ EMIT_STATE(PS_TEMP_REGISTER_CONTROL, PS_TEMP_REGISTER_CONTROL, e->shader_state.PS_TEMP_REGISTER_CONTROL);
        /*01010*/ EMIT_STATE(PS_CONTROL, PS_CONTROL, e->shader_state.PS_CONTROL);
        /*01018*/ EMIT_STATE(PS_START_PC, PS_START_PC, e->shader_state.PS_START_PC);
    }
    if(dirty & (ETNA_STATE_DSA | ETNA_STATE_FRAMEBUFFER))
    {
        /*01400*/ EMIT_STATE(PE_DEPTH_CONFIG, PE_DEPTH_CONFIG, e->depth_stencil_alpha->PE_DEPTH_CONFIG | e->framebuffer.PE_DEPTH_CONFIG);
    }
    if(dirty & (ETNA_STATE_VIEWPORT))
    {
        /*01404*/ EMIT_STATE(PE_DEPTH_NEAR, PE_DEPTH_NEAR, e->viewport.PE_DEPTH_NEAR);
        /*01408*/ EMIT_STATE(PE_DEPTH_FAR, PE_DEPTH_FAR, e->viewport.PE_DEPTH_FAR);
    }
    if(dirty & (ETNA_STATE_FRAMEBUFFER))
    {
        /*0140C*/ EMIT_STATE(PE_DEPTH_NORMALIZE, PE_DEPTH_NORMALIZE, e->framebuffer.PE_DEPTH_NORMALIZE);
        /*01410*/ EMIT_STATE(PE_DEPTH_ADDR, PE_DEPTH_ADDR, e->framebuffer.PE_DEPTH_ADDR);
        /*01414*/ EMIT_STATE(PE_DEPTH_STRIDE, PE_DEPTH_STRIDE, e->framebuffer.PE_DEPTH_STRIDE);
    }
    if(dirty & (ETNA_STATE_DSA))
    {
        /*01418*/ EMIT_STATE(PE_STENCIL_OP, PE_STENCIL_OP, e->depth_stencil_alpha->PE_STENCIL_OP);
    }
    if(dirty & (ETNA_STATE_DSA | ETNA_STATE_STENCIL_REF))
    {
        /*0141C*/ EMIT_STATE(PE_STENCIL_CONFIG, PE_STENCIL_CONFIG, e->depth_stencil_alpha->PE_STENCIL_CONFIG | e->stencil_ref.PE_STENCIL_CONFIG);
    }
    if(dirty & (ETNA_STATE_DSA))
    {
        /*01420*/ EMIT_STATE(PE_ALPHA_OP, PE_ALPHA_OP, e->depth_stencil_alpha->PE_ALPHA_OP);
    }
    if(dirty & (ETNA_STATE_BLEND_COLOR))
    {
        /*01424*/ EMIT_STATE(PE_ALPHA_BLEND_COLOR, PE_ALPHA_BLEND_COLOR, e->blend_color.PE_ALPHA_BLEND_COLOR);
    }
    if(dirty & (ETNA_STATE_BLEND))
    {
        /*01428*/ EMIT_STATE(PE_ALPHA_CONFIG, PE_ALPHA_CONFIG, e->blend->PE_ALPHA_CONFIG);
    }
    if(dirty & (ETNA_STATE_BLEND | ETNA_STATE_FRAMEBUFFER))
    {
        /*0142C*/ EMIT_STATE(PE_COLOR_FORMAT, PE_COLOR_FORMAT, e->blend->PE_COLOR_FORMAT | e->framebuffer.PE_COLOR_FORMAT);
    }
    if(dirty & (ETNA_STATE_FRAMEBUFFER))
    {
        /*01430*/ EMIT_STATE(PE_COLOR_ADDR, PE_COLOR_ADDR, e->framebuffer.PE_COLOR_ADDR);
        /*01434*/ EMIT_STATE(PE_COLOR_STRIDE, PE_COLOR_STRIDE, e->framebuffer.PE_COLOR_STRIDE);
        /*01454*/ EMIT_STATE(PE_HDEPTH_CONTROL, PE_HDEPTH_CONTROL, e->framebuffer.PE_HDEPTH_CONTROL);
    }
    if(dirty & (ETNA_STATE_STENCIL_REF))
    {
        /*014A0*/ EMIT_STATE(PE_STENCIL_CONFIG_EXT, PE_STENCIL_CONFIG_EXT, e->stencil_ref.PE_STENCIL_CONFIG_EXT);
    }
    if(dirty & (ETNA_STATE_BLEND))
    {
        /*014A4*/ EMIT_STATE(PE_LOGIC_OP, PE_LOGIC_OP, e->blend->PE_LOGIC_OP);
        for(int x=0; x<2; ++x)
        {
            /*014A8*/ EMIT_STATE(PE_DITHER(x), PE_DITHER[x], e->blend->PE_DITHER[x]);
        }
    }
    if(dirty & (ETNA_STATE_FRAMEBUFFER | ETNA_STATE_TS))
    {
        /*01654*/ EMIT_STATE(TS_MEM_CONFIG, TS_MEM_CONFIG, e->framebuffer.TS_MEM_CONFIG);
        /*01658*/ EMIT_STATE(TS_COLOR_STATUS_BASE, TS_COLOR_STATUS_BASE, e->framebuffer.TS_COLOR_STATUS_BASE);
        /*0165C*/ EMIT_STATE(TS_COLOR_SURFACE_BASE, TS_COLOR_SURFACE_BASE, e->framebuffer.TS_COLOR_SURFACE_BASE);
        /*01660*/ EMIT_STATE(TS_COLOR_CLEAR_VALUE, TS_COLOR_CLEAR_VALUE, e->framebuffer.TS_COLOR_CLEAR_VALUE);
        /*01664*/ EMIT_STATE(TS_DEPTH_STATUS_BASE, TS_DEPTH_STATUS_BASE, e->framebuffer.TS_DEPTH_STATUS_BASE);
        /*01668*/ EMIT_STATE(TS_DEPTH_SURFACE_BASE, TS_DEPTH_SURFACE_BASE, e->framebuffer.TS_DEPTH_SURFACE_BASE);
        /*0166C*/ EMIT_STATE(TS_DEPTH_CLEAR_VALUE, TS_DEPTH_CLEAR_VALUE, e->framebuffer.TS_DEPTH_CLEAR_VALUE);
    }
    if(dirty & (ETNA_STATE_SAMPLER_VIEWS | ETNA_STATE_SAMPLERS))
    {
        for(int x=0; x<VIVS_TE_SAMPLER__LEN; ++x)
        {
            /* set active samplers to their configuration value (determined by both the sampler state and sampler view),
             * set inactive sampler config to 0 */
            /*02000*/ EMIT_STATE(TE_SAMPLER_CONFIG0(x), TE_SAMPLER_CONFIG0[x], 
                    ((1<<x) & active_samplers)?(e->sampler[x]->TE_SAMPLER_CONFIG0 | e->sampler_view[x].TE_SAMPLER_CONFIG0):0);
        }
    }
    if(dirty & (ETNA_STATE_SAMPLER_VIEWS))
    {
        for(int x=0; x<VIVS_TE_SAMPLER__LEN; ++x)
        {
            if((1<<x) & active_samplers)
            {
                /*02040*/ EMIT_STATE(TE_SAMPLER_SIZE(x), TE_SAMPLER_SIZE[x], e->sampler_view[x].TE_SAMPLER_SIZE);
            }
        }
        for(int x=0; x<VIVS_TE_SAMPLER__LEN; ++x)
        {
            if((1<<x) & active_samplers)
            {
                /*02080*/ EMIT_STATE(TE_SAMPLER_LOG_SIZE(x), TE_SAMPLER_LOG_SIZE[x], e->sampler_view[x].TE_SAMPLER_LOG_SIZE);
            }
        }
    }
    if(dirty & (ETNA_STATE_SAMPLER_VIEWS | ETNA_STATE_SAMPLERS))
    {
        for(int x=0; x<VIVS_TE_SAMPLER__LEN; ++x)
        {
            if((1<<x) & active_samplers)
            {
                /* min and max lod is determined both by the sampler and the view */
                /*020C0*/ EMIT_STATE(TE_SAMPLER_LOD_CONFIG(x), TE_SAMPLER_LOD_CONFIG[x], 
                        e->sampler[x]->TE_SAMPLER_LOD_CONFIG | 
                        VIVS_TE_SAMPLER_LOD_CONFIG_MAX(etna_umin(e->sampler[x]->max_lod, e->sampler_view[x].max_lod)) | 
                        VIVS_TE_SAMPLER_LOD_CONFIG_MIN(etna_umax(e->sampler[x]->min_lod, e->sampler_view[x].min_lod))); 
            }
        }
    }
    if(dirty & (ETNA_STATE_SAMPLER_VIEWS))
    {
        for(int y=0; y<VIVS_TE_SAMPLER_LOD_ADDR__LEN; ++y)
        {
            for(int x=0; x<VIVS_TE_SAMPLER__LEN; ++x)
            {
                if((1<<x) & active_samplers)
                {
                    /*02400*/ EMIT_STATE(TE_SAMPLER_LOD_ADDR(x, y), TE_SAMPLER_LOD_ADDR[y][x], e->sampler_view[x].TE_SAMPLER_LOD_ADDR[y]);
                }
            }
        }
    }
    if(dirty & (ETNA_STATE_BASE_SETUP))
    {
        /*03814*/ EMIT_STATE(GL_VERTEX_ELEMENT_CONFIG, GL_VERTEX_ELEMENT_CONFIG, e->base_setup.GL_VERTEX_ELEMENT_CONFIG);
    }
    if(dirty & (ETNA_STATE_FRAMEBUFFER | ETNA_STATE_SAMPLE_MASK))
    {
        /*03818*/ EMIT_STATE(GL_MULTI_SAMPLE_CONFIG, GL_MULTI_SAMPLE_CONFIG, e->sample_mask.GL_MULTI_SAMPLE_CONFIG | e->framebuffer.GL_MULTI_SAMPLE_CONFIG);
    }
    if(dirty & (ETNA_STATE_SHADER))
    {
        /*0381C*/ EMIT_STATE(GL_VARYING_TOTAL_COMPONENTS, GL_VARYING_TOTAL_COMPONENTS, e->shader_state.GL_VARYING_TOTAL_COMPONENTS);
        /*03820*/ EMIT_STATE(GL_VARYING_NUM_COMPONENTS, GL_VARYING_NUM_COMPONENTS, e->shader_state.GL_VARYING_NUM_COMPONENTS);
        for(int x=0; x<2; ++x)
        {
            /*03828*/ EMIT_STATE(GL_VARYING_COMPONENT_USE(x), GL_VARYING_COMPONENT_USE[x], e->shader_state.GL_VARYING_COMPONENT_USE[x]);
        }
        for(int x=0; x<e->shader_state.vs_inst_mem_size; ++x)
        {
            /*04000*/ EMIT_STATE(VS_INST_MEM(x), VS_INST_MEM[x], e->shader_state.VS_INST_MEM[x]);
        }
    }
    if(dirty & (ETNA_STATE_VS_UNIFORMS))
    {
        for(int x=0; x<e->shader_state.vs_uniforms_size; ++x)
        {
            /*05000*/ EMIT_STATE(VS_UNIFORMS(x), VS_UNIFORMS[x], e->shader_state.VS_UNIFORMS[x]);
        }
    } 
    if(dirty & (ETNA_STATE_SHADER))
    {
        for(int x=0; x<e->shader_state.ps_inst_mem_size; ++x)
        {
            /*06000*/ EMIT_STATE(PS_INST_MEM(x), PS_INST_MEM[x], e->shader_state.PS_INST_MEM[x]);
        }
    }    
    if(dirty & (ETNA_STATE_PS_UNIFORMS))
    {
        for(int x=0; x<e->shader_state.ps_uniforms_size; ++x)
        {
            /*07000*/ EMIT_STATE(PS_UNIFORMS(x), PS_UNIFORMS[x], e->shader_state.PS_UNIFORMS[x]);
        }
    }
#undef EMIT_STATE
#if 0
    etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_TEXTURE | VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
    etna_set_state(ctx, VIVS_RS_FLUSH_CACHE, VIVS_RS_FLUSH_CACHE_FLUSH);
#endif
    if(dirty & (ETNA_STATE_SAMPLER_VIEWS | ETNA_STATE_SAMPLERS))
    {
        /* clear texture cache (both fragment and vertex) */
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_TEXTURE | VIVS_GL_FLUSH_CACHE_TEXTUREVS);
    }
    if(dirty & (ETNA_STATE_FRAMEBUFFER | ETNA_STATE_TS))
    {
        /* wait rasterizer until PE finished configuration */
        etna_stall(ctx, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);
    }

    e->dirty_bits = 0;
}
/*********************************************************************/
static void etna_pipe_destroy(struct pipe_context *pipe)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    free(priv);
    free(pipe);
}

static void etna_pipe_draw_vbo(struct pipe_context *pipe,
                 const struct pipe_draw_info *info)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    /* First, sync state, then emit DRAW_PRIMITIVES or DRAW_INDEXED_PRIMITIVES */
    sync_context(pipe);
    int prims = translate_vertex_count(info->mode, info->count);
    if(prims <= 0)
    {
        printf("Invalid draw primitive\n");
        return;
    }
    if(info->indexed)
    {
        etna_draw_indexed_primitives(priv->ctx, translate_draw_mode(info->mode), 
                info->start, prims, info->index_bias);
    } else
    {
        etna_draw_primitives(priv->ctx, translate_draw_mode(info->mode), 
                info->start, prims);
    }
}

static void *etna_pipe_create_blend_state(struct pipe_context *pipe,
                            const struct pipe_blend_state *bs)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_blend_state *cs = ETNA_NEW(struct compiled_blend_state);
    const struct pipe_rt_blend_state *rt0 = &bs->rt[0];
    bool enable = rt0->blend_enable && !(rt0->rgb_src_factor == PIPE_BLENDFACTOR_ONE && rt0->rgb_dst_factor == PIPE_BLENDFACTOR_ZERO &&
                                         rt0->alpha_src_factor == PIPE_BLENDFACTOR_ONE && rt0->alpha_dst_factor == PIPE_BLENDFACTOR_ZERO);
    bool separate_alpha = enable && !(rt0->rgb_src_factor == rt0->alpha_src_factor &&
                                      rt0->rgb_dst_factor == rt0->alpha_dst_factor);
    bool full_overwrite = (rt0->colormask == 15) && !enable;
    SET_STATE(PE_ALPHA_CONFIG, 
            (enable ? VIVS_PE_ALPHA_CONFIG_BLEND_ENABLE_COLOR : 0) | 
            (separate_alpha ? VIVS_PE_ALPHA_CONFIG_BLEND_SEPARATE_ALPHA : 0) |
            VIVS_PE_ALPHA_CONFIG_SRC_FUNC_COLOR(translate_blend_factor(rt0->rgb_src_factor)) |
            VIVS_PE_ALPHA_CONFIG_SRC_FUNC_ALPHA(translate_blend_factor(rt0->alpha_src_factor)) |
            VIVS_PE_ALPHA_CONFIG_DST_FUNC_COLOR(translate_blend_factor(rt0->rgb_dst_factor)) |
            VIVS_PE_ALPHA_CONFIG_DST_FUNC_ALPHA(translate_blend_factor(rt0->alpha_dst_factor)) |
            VIVS_PE_ALPHA_CONFIG_EQ_COLOR(translate_blend(rt0->rgb_func)) |
            VIVS_PE_ALPHA_CONFIG_EQ_ALPHA(translate_blend(rt0->alpha_func))
            );
    SET_STATE(PE_COLOR_FORMAT, 
            VIVS_PE_COLOR_FORMAT_COMPONENTS(rt0->colormask) |
            (full_overwrite ? VIVS_PE_COLOR_FORMAT_OVERWRITE : 0)
            );
    SET_STATE(PE_LOGIC_OP, 
            VIVS_PE_LOGIC_OP_OP(bs->logicop_enable ? bs->logicop_func : LOGIC_OP_COPY) /* 1-to-1 mapping */ |
            0x000E4000 /* ??? */
            );
    /* independent_blend_enable not needed: only one rt supported */
    /* XXX alpha_to_coverage / alpha_to_one? */
    /* XXX dither? VIVS_PE_DITHER(...) and/or VIVS_RS_DITHER(...) on resolve */
    return cs;
}

static void etna_pipe_bind_blend_state(struct pipe_context *pipe, void *bs)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    priv->dirty_bits |= ETNA_STATE_BLEND;
    priv->blend = bs;
}

static void etna_pipe_delete_blend_state(struct pipe_context *pipe, void *bs)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    free(bs);
}

static void *etna_pipe_create_sampler_state(struct pipe_context *pipe,
                              const struct pipe_sampler_state *ss)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_sampler_state *cs = ETNA_NEW(struct compiled_sampler_state);
    SET_STATE(TE_SAMPLER_CONFIG0, 
                /* XXX get from sampler view: VIVS_TE_SAMPLER_CONFIG0_TYPE(TEXTURE_TYPE_2D)| */
                VIVS_TE_SAMPLER_CONFIG0_UWRAP(translate_texture_wrapmode(ss->wrap_s))|
                VIVS_TE_SAMPLER_CONFIG0_VWRAP(translate_texture_wrapmode(ss->wrap_t))|
                VIVS_TE_SAMPLER_CONFIG0_MIN(translate_texture_filter(ss->min_img_filter))|
                VIVS_TE_SAMPLER_CONFIG0_MIP(translate_texture_mipfilter(ss->min_mip_filter))|
                VIVS_TE_SAMPLER_CONFIG0_MAG(translate_texture_filter(ss->mag_img_filter))
                /* XXX get from sampler view: VIVS_TE_SAMPLER_CONFIG0_FORMAT(tex_format) */
            );
    /* VIVS_TE_SAMPLER_CONFIG1 (swizzle, extended format) fully determined by sampler view */
    SET_STATE(TE_SAMPLER_LOD_CONFIG,
            (ss->lod_bias != 0.0 ? VIVS_TE_SAMPLER_LOD_CONFIG_BIAS_ENABLE : 0) | 
            VIVS_TE_SAMPLER_LOD_CONFIG_BIAS(float_to_fixp55(ss->lod_bias))
            );
    cs->min_lod = float_to_fixp55(ss->min_lod);
    cs->max_lod = float_to_fixp55(ss->max_lod);
    return cs;
}

static void etna_pipe_bind_fragment_sampler_states(struct pipe_context *pipe,
                                      unsigned num_samplers,
                                      void **samplers)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    priv->dirty_bits |= ETNA_STATE_SAMPLERS;
    priv->num_fragment_samplers = num_samplers;
    for(int idx=0; idx<num_samplers; ++idx)
        priv->sampler[idx] = samplers[idx];
}

static void etna_pipe_bind_vertex_sampler_states(struct pipe_context *pipe,
                                      unsigned num_samplers,
                                      void **samplers)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    priv->dirty_bits |= ETNA_STATE_SAMPLERS;
    priv->num_vertex_samplers = num_samplers;
    for(int idx=0; idx<num_samplers; ++idx)
        priv->sampler[priv->specs.vertex_sampler_offset + idx] = samplers[idx];
}

static void etna_pipe_delete_sampler_state(struct pipe_context *pipe, void *ss)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    free(ss);
}

static void *etna_pipe_create_rasterizer_state(struct pipe_context *pipe,
                                 const struct pipe_rasterizer_state *rs)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_rasterizer_state *cs = ETNA_NEW(struct compiled_rasterizer_state);
    if(rs->fill_front != rs->fill_back)
    {
        printf("Different front and back fill mode not supported\n");
    }
    SET_STATE(PA_CONFIG, 
            (rs->flatshade ? VIVS_PA_CONFIG_SHADE_MODEL_FLAT : VIVS_PA_CONFIG_SHADE_MODEL_SMOOTH) | 
            translate_cull_face(rs->cull_face, rs->front_ccw) |
            translate_polygon_mode(rs->fill_front) |
            (rs->point_quad_rasterization ? VIVS_PA_CONFIG_POINT_SPRITE_ENABLE : 0) |
            (rs->point_size_per_vertex ? VIVS_PA_CONFIG_POINT_SIZE_ENABLE : 0));
    SET_STATE_F32(PA_LINE_WIDTH, rs->line_width);
    SET_STATE_F32(PA_POINT_SIZE, rs->point_size);
    SET_STATE_F32(SE_DEPTH_SCALE, rs->offset_scale);
    SET_STATE_F32(SE_DEPTH_BIAS, rs->offset_units);
    SET_STATE(SE_CONFIG, 
            (rs->line_last_pixel ? VIVS_SE_CONFIG_LAST_PIXEL_ENABLE : 0) 
            /* XXX anything else? */
            );
    /* XXX rs->gl_rasterization_rules is likely one of the bits in VIVS_PA_SYSTEM_MODE */
    SET_STATE(PA_SYSTEM_MODE, 
            (rs->gl_rasterization_rules ? (VIVS_PA_SYSTEM_MODE_UNK0 | VIVS_PA_SYSTEM_MODE_UNK4) : 0));
    /* rs->scissor overrides the scissor, defaulting to the whole framebuffer, with the scissor state */
    cs->scissor = rs->scissor;
    /* point size per vertex adds a vertex shader output */
    cs->VS_OUTPUT_COUNT = rs->point_size_per_vertex;
    return cs;
}

static void etna_pipe_bind_rasterizer_state(struct pipe_context *pipe, void *rs)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    priv->dirty_bits |= ETNA_STATE_RASTERIZER;
    priv->rasterizer = rs;
}

static void etna_pipe_delete_rasterizer_state(struct pipe_context *pipe, void *rs)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    free(rs);
}

static void *etna_pipe_create_depth_stencil_alpha_state(struct pipe_context *pipe,
                                    const struct pipe_depth_stencil_alpha_state *dsa)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_depth_stencil_alpha_state *cs = ETNA_NEW(struct compiled_depth_stencil_alpha_state);
    /* XXX does stencil[0] / stencil[1] order depend on rs->front_ccw? */
    /* Determine whether to enable early z reject. Don't enable it when any of the stencil functions is used. */
    bool early_z = true;
    if(dsa->stencil[0].enabled)
    {
        if(dsa->stencil[0].fail_op != PIPE_STENCIL_OP_KEEP || 
           dsa->stencil[0].zfail_op != PIPE_STENCIL_OP_KEEP ||
           dsa->stencil[0].zpass_op != PIPE_STENCIL_OP_KEEP)
        {
            early_z = false;
        }
        else if(dsa->stencil[1].enabled)
        {
            if(dsa->stencil[1].fail_op != PIPE_STENCIL_OP_KEEP || 
               dsa->stencil[1].zfail_op != PIPE_STENCIL_OP_KEEP ||
               dsa->stencil[1].zpass_op != PIPE_STENCIL_OP_KEEP)
            {
                early_z = false;
            }
        }
    }
    /* compare funcs have 1 to 1 mapping */
    SET_STATE(PE_DEPTH_CONFIG, 
            VIVS_PE_DEPTH_CONFIG_DEPTH_FUNC(dsa->depth.enabled ? dsa->depth.func : PIPE_FUNC_ALWAYS) |
            (dsa->depth.writemask ? VIVS_PE_DEPTH_CONFIG_WRITE_ENABLE : 0) |
            (early_z ? VIVS_PE_DEPTH_CONFIG_EARLY_Z : 0)
            );
    SET_STATE(PE_ALPHA_OP, 
            (dsa->alpha.enabled ? VIVS_PE_ALPHA_OP_ALPHA_TEST : 0) |
            VIVS_PE_ALPHA_OP_ALPHA_FUNC(dsa->alpha.func) |
            VIVS_PE_ALPHA_OP_ALPHA_REF(etna_cfloat_to_uint8(dsa->alpha.ref_value)));
    SET_STATE(PE_STENCIL_OP, 
            VIVS_PE_STENCIL_OP_FUNC_FRONT(dsa->stencil[0].func) |
            VIVS_PE_STENCIL_OP_FUNC_BACK(dsa->stencil[1].func) |
            VIVS_PE_STENCIL_OP_FAIL_FRONT(translate_stencil_op(dsa->stencil[0].fail_op)) | 
            VIVS_PE_STENCIL_OP_FAIL_BACK(translate_stencil_op(dsa->stencil[1].fail_op)) |
            VIVS_PE_STENCIL_OP_DEPTH_FAIL_FRONT(translate_stencil_op(dsa->stencil[0].zfail_op)) |
            VIVS_PE_STENCIL_OP_DEPTH_FAIL_BACK(translate_stencil_op(dsa->stencil[1].zfail_op)) |
            VIVS_PE_STENCIL_OP_PASS_FRONT(translate_stencil_op(dsa->stencil[0].zpass_op)) |
            VIVS_PE_STENCIL_OP_PASS_BACK(translate_stencil_op(dsa->stencil[1].zpass_op)));
    SET_STATE(PE_STENCIL_CONFIG, 
            translate_stencil_mode(dsa->stencil[0].enabled, dsa->stencil[1].enabled) |
            VIVS_PE_STENCIL_CONFIG_MASK_FRONT(dsa->stencil[0].valuemask) | 
            VIVS_PE_STENCIL_CONFIG_WRITE_MASK(dsa->stencil[0].writemask) 
            /* XXX back masks in VIVS_PE_DEPTH_CONFIG_EXT? */
            /* XXX VIVS_PE_STENCIL_CONFIG_REF_FRONT comes from pipe_stencil_ref */
            );

    /* XXX does alpha/stencil test affect PE_COLOR_FORMAT_OVERWRITE? */
    return cs;
}

static void etna_pipe_bind_depth_stencil_alpha_state(struct pipe_context *pipe, void *dsa)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    priv->dirty_bits |= ETNA_STATE_DSA;
    priv->depth_stencil_alpha = dsa;
}

static void etna_pipe_delete_depth_stencil_alpha_state(struct pipe_context *pipe, void *dsa)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    free(dsa);
}

static void *etna_pipe_create_vertex_elements_state(struct pipe_context *pipe,
                                      unsigned num_elements,
                                      const struct pipe_vertex_element *elements)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_vertex_elements_state *cs = ETNA_NEW(struct compiled_vertex_elements_state);
    /* VERTEX_ELEMENT_STRIDE is in pipe_vertex_buffer */
    cs->num_elements = num_elements;
    for(unsigned idx=0; idx<num_elements; ++idx)
    {
        unsigned element_size = pipe_element_size(elements[idx].src_format);
        unsigned end_offset = elements[idx].src_offset + element_size;
        assert(element_size != 0 && end_offset <= 256);
        /* check whether next element is consecutive to this one */
        bool nonconsecutive = (idx == (num_elements-1)) || 
                    elements[idx+1].vertex_buffer_index != elements[idx].vertex_buffer_index ||
                    end_offset != elements[idx+1].src_offset;
        SET_STATE(FE_VERTEX_ELEMENT_CONFIG[idx], 
                (nonconsecutive ? VIVS_FE_VERTEX_ELEMENT_CONFIG_NONCONSECUTIVE : 0) |
                translate_vertex_format_type(elements[idx].src_format) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM(vertex_format_num(elements[idx].src_format)) |
                translate_vertex_format_normalize(elements[idx].src_format) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN(ENDIAN_MODE_NO_SWAP) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM(elements[idx].vertex_buffer_index) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_START(elements[idx].src_offset) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_END(end_offset));
    }
    return cs;
}

static void etna_pipe_bind_vertex_elements_state(struct pipe_context *pipe, void *ve)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    priv->dirty_bits |= ETNA_STATE_VERTEX_ELEMENTS;
    priv->vertex_elements = ve;
}

static void etna_pipe_delete_vertex_elements_state(struct pipe_context *pipe, void *ve)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    free(ve);
}

static void etna_pipe_set_blend_color(struct pipe_context *pipe,
                        const struct pipe_blend_color *bc)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_blend_color *cs = &priv->blend_color;
    SET_STATE(PE_ALPHA_BLEND_COLOR, 
            VIVS_PE_ALPHA_BLEND_COLOR_R(etna_cfloat_to_uint8(bc->color[0])) |
            VIVS_PE_ALPHA_BLEND_COLOR_G(etna_cfloat_to_uint8(bc->color[1])) |
            VIVS_PE_ALPHA_BLEND_COLOR_B(etna_cfloat_to_uint8(bc->color[2])) |
            VIVS_PE_ALPHA_BLEND_COLOR_A(etna_cfloat_to_uint8(bc->color[3]))
            );
    priv->dirty_bits |= ETNA_STATE_BLEND_COLOR;
}

static void etna_pipe_set_stencil_ref(struct pipe_context *pipe,
                        const struct pipe_stencil_ref *sr)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_stencil_ref *cs = &priv->stencil_ref;
    SET_STATE(PE_STENCIL_CONFIG, 
            VIVS_PE_STENCIL_CONFIG_REF_FRONT(sr->ref_value[0]) 
            /* rest of bits comes from depth_stencil_alpha, merged in */
            );
    SET_STATE(PE_STENCIL_CONFIG_EXT, 
            VIVS_PE_STENCIL_CONFIG_EXT_REF_BACK(sr->ref_value[0]) 
            );
    priv->dirty_bits |= ETNA_STATE_STENCIL_REF;
}

static void etna_pipe_set_sample_mask(struct pipe_context *pipe,
                        unsigned sample_mask)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_sample_mask *cs = &priv->sample_mask;

    SET_STATE(GL_MULTI_SAMPLE_CONFIG, 
            /* to be merged with render target state */
            VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_ENABLES(sample_mask));
    priv->dirty_bits |= ETNA_STATE_SAMPLE_MASK;
}

static void etna_pipe_set_framebuffer_state(struct pipe_context *pipe,
                              const struct pipe_framebuffer_state *sv)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_framebuffer_state *cs = &priv->framebuffer;
    struct etna_surface *cbuf = (sv->nr_cbufs > 0) ? etna_surface(sv->cbufs[0]) : NULL;
    struct etna_surface *zsbuf = etna_surface(sv->zsbuf);
    /* XXX rendering with only color or only depth should be possible */
    assert(cbuf != NULL && zsbuf != NULL);
    uint32_t depth_format = translate_depth_format(zsbuf->base.format);
    unsigned depth_bits = depth_format == VIVS_PE_DEPTH_CONFIG_DEPTH_FORMAT_D16 ? 16 : 24; 
    assert((cbuf->layout & 1) && (zsbuf->layout & 1)); /* color and depth buffer must be at least tiled */
    bool color_supertiled = (cbuf->layout & 2)!=0;
    bool depth_supertiled = (zsbuf->layout & 2)!=0;

    /* XXX support multisample 2X/4X, take care that required width/height is doubled */
    SET_STATE(GL_MULTI_SAMPLE_CONFIG, 
            VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES_NONE
            /* VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_ENABLES(0xf)
            VIVS_GL_MULTI_SAMPLE_CONFIG_UNK12 |
            VIVS_GL_MULTI_SAMPLE_CONFIG_UNK16 */
            );  /* merged with sample_mask */
    SET_STATE(PE_COLOR_FORMAT, 
            VIVS_PE_COLOR_FORMAT_FORMAT(translate_rt_format(cbuf->base.format)) |
            (color_supertiled ? VIVS_PE_COLOR_FORMAT_SUPER_TILED : 0) /* XXX depends on layout */
            /* XXX VIVS_PE_COLOR_FORMAT_OVERWRITE and the rest comes from blend_state / depth_stencil_alpha */
            ); /* merged with depth_stencil_alpha */
    SET_STATE(PE_DEPTH_CONFIG, 
            depth_format |
            (depth_supertiled ? VIVS_PE_DEPTH_CONFIG_SUPER_TILED : 0) | /* XXX depends on layout */
            VIVS_PE_DEPTH_CONFIG_DEPTH_MODE_Z /* XXX set to NONE if no Z buffer */
            /* VIVS_PE_DEPTH_CONFIG_ONLY_DEPTH */
            ); /* merged with depth_stencil_alpha */

    SET_STATE(PE_DEPTH_ADDR, zsbuf->surf.address);
    SET_STATE(PE_DEPTH_STRIDE, zsbuf->surf.stride);
    SET_STATE(PE_HDEPTH_CONTROL, VIVS_PE_HDEPTH_CONTROL_FORMAT_DISABLED);
    SET_STATE_F32(PE_DEPTH_NORMALIZE, exp2f(depth_bits) - 1.0f);
    SET_STATE(PE_COLOR_ADDR, cbuf->surf.address);
    SET_STATE(PE_COLOR_STRIDE, cbuf->surf.stride);
    
    SET_STATE_FIXP(SE_SCISSOR_LEFT, 0); /* affected by rasterizer and scissor state as well */
    SET_STATE_FIXP(SE_SCISSOR_TOP, 0);
    SET_STATE_FIXP(SE_SCISSOR_RIGHT, (sv->width << 16)-1);
    SET_STATE_FIXP(SE_SCISSOR_BOTTOM, (sv->height << 16)-1);

    /* Set up TS as well. Warning: this is shared with RS */
    SET_STATE(TS_MEM_CONFIG, 
            VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
            VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR |
            (depth_bits == 16 ? VIVS_TS_MEM_CONFIG_DEPTH_16BPP : 0) | 
            VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION /* |
            VIVS_TS_MEM_CONFIG_MSAA | 
            translate_msaa_format(cbuf->format) */);
    SET_STATE(TS_DEPTH_CLEAR_VALUE, zsbuf->clear_value);
    SET_STATE(TS_DEPTH_STATUS_BASE, zsbuf->surf.ts_address);
    SET_STATE(TS_DEPTH_SURFACE_BASE, zsbuf->surf.address);
    SET_STATE(TS_COLOR_CLEAR_VALUE, cbuf->clear_value);
    SET_STATE(TS_COLOR_STATUS_BASE, cbuf->surf.ts_address);
    SET_STATE(TS_COLOR_SURFACE_BASE, cbuf->surf.address);

    priv->dirty_bits |= ETNA_STATE_VIEWPORT;
    priv->framebuffer_s = *sv; /* keep copy of original structure */
}

static void etna_pipe_set_scissor_state( struct pipe_context *pipe,
                          const struct pipe_scissor_state *ss)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_scissor_state *cs = &priv->scissor;
    SET_STATE_FIXP(SE_SCISSOR_LEFT, (ss->minx << 16));
    SET_STATE_FIXP(SE_SCISSOR_TOP, (ss->miny << 16));
    SET_STATE_FIXP(SE_SCISSOR_RIGHT, (ss->maxx << 16)-1);
    SET_STATE_FIXP(SE_SCISSOR_BOTTOM, (ss->maxy << 16)-1);
    /* note that this state is only used when rasterizer_state->scissor is on */
    priv->dirty_bits |= ETNA_STATE_FRAMEBUFFER;
}

static void etna_pipe_set_viewport_state( struct pipe_context *pipe,
                           const struct pipe_viewport_state *vs)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_viewport_state *cs = &priv->viewport;
    /**
     * For Vivante GPU, viewport z transformation is 0..1 to 0..1 instead of -1..1 to 0..1.
     * scaling and translation to 0..1 already happened, so remove that
     *
     * z' = (z * 2 - 1) * scale + translate
     *    = z * (2 * scale) + (translate - scale)
     *
     * scale' = 2 * scale
     * translate' = translate - scale
     */
    SET_STATE_F32(PA_VIEWPORT_SCALE_X, vs->scale[0]);
    SET_STATE_F32(PA_VIEWPORT_SCALE_Y, vs->scale[1]);
    SET_STATE_F32(PA_VIEWPORT_SCALE_Z, vs->scale[2] * 2.0f);
    SET_STATE_F32(PA_VIEWPORT_OFFSET_X, vs->translate[0]);
    SET_STATE_F32(PA_VIEWPORT_OFFSET_Y, vs->translate[1]);
    SET_STATE_F32(PA_VIEWPORT_OFFSET_Z, vs->translate[2] - vs->scale[2]);

    SET_STATE_F32(PE_DEPTH_NEAR, 0.0); /* not affected if depth mode is Z (as in GL) */
    SET_STATE_F32(PE_DEPTH_FAR, 1.0);
    priv->dirty_bits |= ETNA_STATE_SCISSOR;
}

static void etna_pipe_set_fragment_sampler_views(struct pipe_context *pipe,
                                  unsigned num_views,
                                  struct pipe_sampler_view **info)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    priv->dirty_bits |= ETNA_STATE_SAMPLER_VIEWS;
    priv->num_fragment_sampler_views = num_views;
    for(int idx=0; idx<num_views; ++idx)
        priv->sampler_view[idx] = *etna_sampler_view(info[idx])->internal;
}

static void etna_pipe_set_vertex_sampler_views(struct pipe_context *pipe,
                                  unsigned num_views,
                                  struct pipe_sampler_view **info)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    priv->dirty_bits |= ETNA_STATE_SAMPLER_VIEWS;
    priv->num_vertex_sampler_views = num_views;
    for(int idx=0; idx<num_views; ++idx)
        priv->sampler_view[priv->specs.vertex_sampler_offset + idx] = *etna_sampler_view(info[idx])->internal;
}

static void etna_pipe_set_vertex_buffers( struct pipe_context *pipe,
                           unsigned start_slot,
                           unsigned num_buffers,
                           const struct pipe_vertex_buffer *vb)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_set_vertex_buffer *cs = &priv->vertex_buffer;

    assert(start_slot == 0 && num_buffers == 1); /* XXX TODO */
    assert(vb[0].buffer); /* XXX user_buffer */
    SET_STATE(FE_VERTEX_STREAM_CONTROL, 
            VIVS_FE_VERTEX_STREAM_CONTROL_VERTEX_STRIDE(vb[0].stride));
    SET_STATE(FE_VERTEX_STREAM_BASE_ADDR, etna_resource(vb[0].buffer)->levels[0].address + vb[0].buffer_offset);
    
    priv->dirty_bits |= ETNA_STATE_VERTEX_BUFFERS;
}

static void etna_pipe_set_index_buffer( struct pipe_context *pipe,
                         const struct pipe_index_buffer *ib)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_set_index_buffer *cs = &priv->index_buffer;
    if(ib == NULL)
    {
        SET_STATE(FE_INDEX_STREAM_CONTROL, 0);
        SET_STATE(FE_INDEX_STREAM_BASE_ADDR, 0);
    } else
    {
        assert(ib->buffer); /* XXX user_buffer */
        SET_STATE(FE_INDEX_STREAM_CONTROL, 
                translate_index_size(ib->index_size));
        SET_STATE(FE_INDEX_STREAM_BASE_ADDR, etna_resource(ib->buffer)->levels[0].address + ib->offset);
    }
    priv->dirty_bits |= ETNA_STATE_INDEX_BUFFER;
}

static void etna_pipe_clear(struct pipe_context *pipe,
             unsigned buffers,
             const union pipe_color_union *color,
             double depth,
             unsigned stencil)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    /* XXX need to update clear command in non-TS (fast clear) case *if*
     * clear value is different from previous time. 
     */
    if(buffers & PIPE_CLEAR_COLOR)
    {
        for(int idx=0; idx<priv->framebuffer_s.nr_cbufs; ++idx)
        {
            struct etna_surface *surf = etna_surface(priv->framebuffer_s.cbufs[idx]);

            surf->clear_value = translate_clear_color(surf->base.format, &color[idx]); 
            priv->framebuffer.TS_COLOR_CLEAR_VALUE = surf->clear_value;
            priv->dirty_bits |= ETNA_STATE_TS;
            etna_submit_rs_state(priv->ctx, &surf->clear_command);
        }
    }
    if((buffers & PIPE_CLEAR_DEPTHSTENCIL) && priv->framebuffer_s.zsbuf != NULL)
    {
        struct etna_surface *surf = etna_surface(priv->framebuffer_s.zsbuf);

        surf->clear_value = translate_clear_depth_stencil(surf->base.format, depth, stencil);
        priv->framebuffer.TS_DEPTH_CLEAR_VALUE = surf->clear_value;
        priv->dirty_bits |= ETNA_STATE_TS;
        etna_submit_rs_state(priv->ctx, &surf->clear_command);
    }
}

static void etna_pipe_clear_render_target(struct pipe_context *pipe,
                           struct pipe_surface *dst,
                           const union pipe_color_union *color,
                           unsigned dstx, unsigned dsty,
                           unsigned width, unsigned height)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    /* TODO fill me in */
    printf("Warning: unimplemented clear_render_target\n");
}

static void etna_pipe_clear_depth_stencil(struct pipe_context *pipe,
                           struct pipe_surface *dst,
                           unsigned clear_flags,
                           double depth,
                           unsigned stencil,
                           unsigned dstx, unsigned dsty,
                           unsigned width, unsigned height)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    /* TODO fill me in */
    printf("Warning: unimplemented clear_depth_stencil\n");
}

static void etna_pipe_flush(struct pipe_context *pipe,
             struct pipe_fence_handle **fence,
             enum pipe_flush_flags flags)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    /* TODO fill me in */
}

static struct pipe_sampler_view *etna_pipe_create_sampler_view(struct pipe_context *pipe,
                                                 struct pipe_resource *texture,
                                                 const struct pipe_sampler_view *templat)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct etna_sampler_view *sv = ETNA_NEW(struct etna_sampler_view);
    sv->base = *templat;
    sv->base.context = pipe;
    sv->base.texture = texture;
    assert(sv->base.texture);

    struct compiled_sampler_view *cs = ETNA_NEW(struct compiled_sampler_view);
    struct etna_resource *res = etna_resource(sv->base.texture);
    assert(res != NULL);

    SET_STATE(TE_SAMPLER_CONFIG0, 
                VIVS_TE_SAMPLER_CONFIG0_TYPE(translate_texture_target(res->base.target)) |
                VIVS_TE_SAMPLER_CONFIG0_FORMAT(translate_texture_format(sv->base.format)) 
                /* merged with sampler state */
            );
    /* XXX VIVS_TE_SAMPLER_CONFIG1 (swizzle, extended format), swizzle_(r|g|b|a) */
    SET_STATE(TE_SAMPLER_SIZE, 
            VIVS_TE_SAMPLER_SIZE_WIDTH(res->base.width0)|
            VIVS_TE_SAMPLER_SIZE_HEIGHT(res->base.height0));
    SET_STATE(TE_SAMPLER_LOG_SIZE, 
            VIVS_TE_SAMPLER_LOG_SIZE_WIDTH(log2_fixp55(res->base.width0)) |
            VIVS_TE_SAMPLER_LOG_SIZE_HEIGHT(log2_fixp55(res->base.height0)));
    /* XXX in principle we only have to define lods sv->first_level .. sv->last_level */
    for(int lod=0; lod<=res->base.last_level; ++lod)
    {
        SET_STATE(TE_SAMPLER_LOD_ADDR[lod], res->levels[lod].address);
    }
    cs->min_lod = sv->base.u.tex.first_level << 5;
    cs->max_lod = etna_umin(sv->base.u.tex.last_level, res->base.last_level) << 5;

    sv->internal = cs;
    return &sv->base;
}

static void etna_pipe_sampler_view_destroy(struct pipe_context *pipe,
                            struct pipe_sampler_view *view)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    free(etna_sampler_view(view)->internal);
    free(view);
}

static struct pipe_surface *etna_pipe_create_surface(struct pipe_context *pipe,
                                      struct pipe_resource *resource_,
                                      const struct pipe_surface *templat)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct etna_surface *surf = ETNA_NEW(struct etna_surface);
    struct etna_resource *resource = etna_resource(resource_);
    assert(templat->u.tex.first_layer == templat->u.tex.last_layer);
    unsigned layer = templat->u.tex.first_layer;
    unsigned level = templat->u.tex.level;
    assert(layer < resource->base.array_size);
   
    surf->base.context = pipe;
    surf->base.texture = &resource->base;
    surf->base.format = resource->base.format;
    surf->base.width = resource->levels[level].width;
    surf->base.height = resource->levels[level].height;
    surf->base.writable = templat->writable; // what is this for anyway
    surf->base.u = templat->u;

    surf->layout = resource->layout;
    surf->surf = resource->levels[level];
    surf->surf.address += layer * surf->surf.layer_stride; 
    surf->surf.logical += layer * surf->surf.layer_stride; 
    surf->clear_value = 0; /* last clear value */

    if(surf->surf.ts_address) /* XXX handle non-fast-clear case */
    {
        etna_compile_rs_state(&surf->clear_command, &(struct rs_state){
                .source_format = RS_FORMAT_X8R8G8B8,
                .dest_format = RS_FORMAT_X8R8G8B8,
                .dest_addr = surf->surf.ts_address,
                .dest_stride = 0x40,
                .dither = {0xffffffff, 0xffffffff},
                .width = 16,
                .height = surf->surf.ts_size/0x40,
                .clear_value = {priv->specs.ts_clear_value},  /* XXX should be 0x11111111 for non-2BITPERTILE GPUs */
                .clear_mode = VIVS_RS_CLEAR_CONTROL_MODE_ENABLED1,
                .clear_bits = 0xffff
            });
    }
    return &surf->base;
}

static void etna_pipe_surface_destroy(struct pipe_context *pipe, struct pipe_surface *surf)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    free(surf);
}

static void etna_pipe_texture_barrier(struct pipe_context *pipe)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    /* clear texture cache */
    etna_set_state(priv->ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_TEXTURE | VIVS_GL_FLUSH_CACHE_TEXTUREVS);
}
   
/* XXX to be removed: test entry point */
static void *etna_create_etna_shader_state(struct pipe_context *pipe, const struct etna_shader_program *rs)
{
    struct compiled_shader_state *cs = ETNA_NEW(struct compiled_shader_state);
    /* set last_varying_2x flag if the last varying has 1 or 2 components */
    bool last_varying_2x = false;
    if(rs->num_varyings>0 && rs->varyings[rs->num_varyings-1].num_components <= 2)
        last_varying_2x = true;

    SET_STATE(RA_CONTROL, VIVS_RA_CONTROL_UNK0 |
                          (last_varying_2x ? VIVS_RA_CONTROL_LAST_VARYING_2X : 0));

    SET_STATE(PA_ATTRIBUTE_ELEMENT_COUNT, VIVS_PA_ATTRIBUTE_ELEMENT_COUNT_COUNT(rs->num_varyings));
    for(int idx=0; idx<rs->num_varyings; ++idx)
        SET_STATE(PA_SHADER_ATTRIBUTES[idx], rs->varyings[idx].pa_attributes);

    SET_STATE(VS_END_PC, rs->vs_code_size / 4);
    SET_STATE(VS_OUTPUT_COUNT, rs->num_varyings + 1); /* position + varyings */
    SET_STATE(VS_INPUT_COUNT, VIVS_VS_INPUT_COUNT_UNK8(1)); /// XXX what is this
    SET_STATE(VS_TEMP_REGISTER_CONTROL,
                              VIVS_VS_TEMP_REGISTER_CONTROL_NUM_TEMPS(rs->vs_num_temps));
    
    /* vs outputs (varyings) */ 
    uint32_t vs_output[16] = {0};
    int varid = 0;
    vs_output[varid++] = rs->vs_pos_out_reg;
    for(int idx=0; idx<rs->num_varyings; ++idx)
        vs_output[varid++] = rs->varyings[idx].vs_reg;
    vs_output[varid++] = rs->vs_pointsize_out_reg; /* pointsize is last */

    for(int idx=0; idx<4; ++idx)
    {
        SET_STATE(VS_OUTPUT[idx], vs_output[idx*4] | (vs_output[idx*4+1] << 8) | 
                                 (vs_output[idx*4+2] << 16) | (vs_output[idx*4+3] << 24));
    }
    
    /* vs inputs (attributes) */
    uint32_t vs_input[4] = {0};
    for(int idx=0; idx<rs->num_inputs; ++idx)
        vs_input[idx/4] |= rs->inputs[idx].vs_reg << ((idx%4)*8);
    for(int idx=0; idx<4; ++idx)
        SET_STATE(VS_INPUT[idx], vs_input[idx]);

    SET_STATE(VS_LOAD_BALANCING, rs->vs_load_balancing); 
    SET_STATE(VS_START_PC, 0);

    SET_STATE(PS_END_PC, rs->ps_code_size / 4);
    SET_STATE(PS_OUTPUT_REG, rs->ps_color_out_reg);
    SET_STATE(PS_INPUT_COUNT, VIVS_PS_INPUT_COUNT_COUNT(rs->num_varyings + 1) | 
                              VIVS_PS_INPUT_COUNT_UNK8(31));
    SET_STATE(PS_TEMP_REGISTER_CONTROL,
                              VIVS_PS_TEMP_REGISTER_CONTROL_NUM_TEMPS(rs->ps_num_temps));
    SET_STATE(PS_CONTROL, VIVS_PS_CONTROL_UNK1);
    SET_STATE(PS_START_PC, 0);

    uint32_t total_components = 0;
    uint32_t num_components = 0;
    uint32_t component_use[2] = {0};
    for(int idx=0; idx<rs->num_varyings; ++idx)
    {
        num_components |= rs->varyings[idx].num_components << ((idx%8)*4);
        for(int comp=0; comp<rs->varyings[idx].num_components; ++comp)
        {
            int compid = total_components + comp;
            unsigned use = VARYING_COMPONENT_USE_USED;
            if(rs->varyings[idx].special == ETNA_VARYING_POINTCOORD)
            {
                if(comp == 0)
                    use = VARYING_COMPONENT_USE_POINTCOORD_X;
                else if(comp == 1)
                    use = VARYING_COMPONENT_USE_POINTCOORD_Y;
            }
            component_use[compid/16] |= use << ((compid%16)*2);
        }
        total_components += rs->varyings[idx].num_components;
    }
    SET_STATE(GL_VARYING_TOTAL_COMPONENTS, VIVS_GL_VARYING_TOTAL_COMPONENTS_NUM(etna_align_up(total_components, 2)));
    SET_STATE(GL_VARYING_NUM_COMPONENTS, num_components);
    SET_STATE(GL_VARYING_COMPONENT_USE[0], component_use[0]);
    SET_STATE(GL_VARYING_COMPONENT_USE[1], component_use[1]);

    cs->vs_inst_mem_size = rs->vs_code_size;
    cs->vs_uniforms_size = rs->vs_uniforms_size;
    cs->ps_inst_mem_size = rs->ps_code_size;
    cs->ps_uniforms_size = rs->ps_uniforms_size;
    cs->VS_INST_MEM = copy32(rs->vs_code, rs->vs_code_size);
    cs->PS_INST_MEM = copy32(rs->ps_code, rs->ps_code_size);
    memcpy(cs->VS_UNIFORMS, rs->vs_uniforms, rs->vs_uniforms_size*4);
    memcpy(cs->PS_UNIFORMS, rs->ps_uniforms, rs->ps_uniforms_size*4);

    return cs;
}

/* XXX to be removed: test entry point */
static void etna_bind_etna_shader_state(struct pipe_context *pipe, void *sh)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    priv->dirty_bits |= ETNA_STATE_SHADER | ETNA_STATE_PS_UNIFORMS | ETNA_STATE_VS_UNIFORMS;
    priv->shader_state = *((struct compiled_shader_state*)sh);
}

/* XXX to be removed: test entry point */
static void etna_delete_etna_shader_state(struct pipe_context *pipe, void *sh_)
{
    struct compiled_shader_state *sh = (struct compiled_shader_state*)sh_;
    free(sh->VS_INST_MEM);
    free(sh->PS_INST_MEM);
    free(sh);
}

/* XXX to be removed: test entry point, replace with proper const buf handling */
static void etna_shader_set_uniforms(struct pipe_context *pipe, void *sh_, unsigned type, unsigned offset, unsigned count, const uint32_t *values)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    assert((offset + count) <= ETNA_MAX_UNIFORMS*4);
    switch(type)
    {
    case PIPE_SHADER_VERTEX:
        memcpy(&priv->shader_state.VS_UNIFORMS[offset], values, count*4);
        priv->dirty_bits |= ETNA_STATE_VS_UNIFORMS;
        break;
    case PIPE_SHADER_FRAGMENT:
        memcpy(&priv->shader_state.PS_UNIFORMS[offset], values, count*4);
        priv->dirty_bits |= ETNA_STATE_PS_UNIFORMS;
        break;
    default: printf("Unhandled shader type %i\n", type);
    }
}

static void *etna_pipe_create_shader_state(struct pipe_context *pipe, const struct pipe_shader_state *pss)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct etna_shader_object *out = NULL;
    etna_compile_shader_object(&priv->specs, pss->tokens, &out);
    return out;
}

static void etna_pipe_delete_shader_state(struct pipe_context *pipe, void *ss)
{
    etna_destroy_shader_object((struct etna_shader_object*)ss);
}

static void etna_pipe_bind_fs_state(struct pipe_context *pipe, void *fss_)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct etna_shader_object *fss = (struct etna_shader_object*)fss_;
    priv->dirty_bits |= ETNA_STATE_SHADER | ETNA_STATE_PS_UNIFORMS;
    assert(fss->processor == TGSI_PROCESSOR_FRAGMENT);
    priv->fs = fss;
}

static void etna_pipe_bind_vs_state(struct pipe_context *pipe, void *vss_)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct etna_shader_object *vss = (struct etna_shader_object*)vss_;
    priv->dirty_bits |= ETNA_STATE_SHADER | ETNA_STATE_VS_UNIFORMS;
    assert(vss->processor == TGSI_PROCESSOR_VERTEX);
    priv->vs = vss;
}

struct pipe_context *etna_new_pipe_context(struct etna_ctx *ctx)
{
    struct pipe_context *pc = ETNA_NEW(struct pipe_context);
    if(pc == NULL)
        return NULL;

    pc->priv = ETNA_NEW(struct etna_pipe_context_priv);
    if(pc->priv == NULL)
    {
        free(pc->priv);
        return NULL;
    }
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pc);

    /* context private setup */
    priv->ctx = ctx;
    priv->dirty_bits = 0xffffffff;
    priv->conn = ctx->conn;

    priv->specs.can_supertile = VIV_FEATURE(ctx->conn, chipMinorFeatures0,SUPER_TILED);
    priv->specs.bits_per_tile = VIV_FEATURE(ctx->conn, chipMinorFeatures0,2BITPERTILE)?2:4;
    priv->specs.ts_clear_value = VIV_FEATURE(ctx->conn, chipMinorFeatures0,2BITPERTILE)?0x55555555:0x11111111;
    priv->specs.vertex_sampler_offset = 8; /* vertex and fragment samplers live in one address space */
    priv->specs.vs_need_z_div = priv->conn->chip.chip_model < 0x1000 && priv->conn->chip.chip_model != 0x880;
    priv->specs.vertex_output_buffer_size = priv->conn->chip.vertex_output_buffer_size;
    priv->specs.vertex_cache_size = priv->conn->chip.vertex_cache_size;
    priv->specs.shader_core_count = priv->conn->chip.shader_core_count;

    /* TODO set sensible defaults for the other state */
    priv->base_setup.PA_W_CLIP_LIMIT = 0x34000001;
    priv->base_setup.GL_VERTEX_ELEMENT_CONFIG = 0x1;

    /* fill in vtable entries one by one */
    pc->destroy = etna_pipe_destroy;
    pc->draw_vbo = etna_pipe_draw_vbo;
    pc->create_blend_state = etna_pipe_create_blend_state;
    pc->bind_blend_state = etna_pipe_bind_blend_state;
    pc->delete_blend_state = etna_pipe_delete_blend_state;
    pc->create_sampler_state = etna_pipe_create_sampler_state;
    pc->bind_fragment_sampler_states = etna_pipe_bind_fragment_sampler_states;
    pc->bind_vertex_sampler_states = etna_pipe_bind_vertex_sampler_states;
    pc->delete_sampler_state = etna_pipe_delete_sampler_state;
    pc->create_rasterizer_state = etna_pipe_create_rasterizer_state;
    pc->bind_rasterizer_state = etna_pipe_bind_rasterizer_state;
    pc->delete_rasterizer_state = etna_pipe_delete_rasterizer_state;
    pc->create_depth_stencil_alpha_state = etna_pipe_create_depth_stencil_alpha_state;
    pc->bind_depth_stencil_alpha_state = etna_pipe_bind_depth_stencil_alpha_state;
    pc->delete_depth_stencil_alpha_state = etna_pipe_delete_depth_stencil_alpha_state;
    pc->create_fs_state = etna_pipe_create_shader_state;
    pc->bind_fs_state = etna_pipe_bind_fs_state;
    pc->delete_fs_state = etna_pipe_delete_shader_state;
    pc->create_vs_state = etna_pipe_create_shader_state;
    pc->bind_vs_state = etna_pipe_bind_vs_state;
    pc->delete_vs_state = etna_pipe_delete_shader_state;
    pc->create_vertex_elements_state = etna_pipe_create_vertex_elements_state;
    pc->bind_vertex_elements_state = etna_pipe_bind_vertex_elements_state;
    pc->delete_vertex_elements_state = etna_pipe_delete_vertex_elements_state;
    pc->set_blend_color = etna_pipe_set_blend_color;
    pc->set_stencil_ref = etna_pipe_set_stencil_ref;
    pc->set_sample_mask = etna_pipe_set_sample_mask;
    pc->set_framebuffer_state = etna_pipe_set_framebuffer_state;
    pc->set_scissor_state = etna_pipe_set_scissor_state;
    pc->set_viewport_state = etna_pipe_set_viewport_state;
    pc->set_fragment_sampler_views = etna_pipe_set_fragment_sampler_views;
    pc->set_vertex_sampler_views = etna_pipe_set_vertex_sampler_views;
    pc->set_vertex_buffers = etna_pipe_set_vertex_buffers;
    pc->set_index_buffer = etna_pipe_set_index_buffer;
    pc->clear = etna_pipe_clear;
    pc->clear_render_target = etna_pipe_clear_render_target;
    pc->clear_depth_stencil = etna_pipe_clear_depth_stencil;
    pc->flush = etna_pipe_flush;
    pc->create_sampler_view = etna_pipe_create_sampler_view;
    pc->sampler_view_destroy = etna_pipe_sampler_view_destroy;
    pc->create_surface = etna_pipe_create_surface;
    pc->surface_destroy = etna_pipe_surface_destroy;
    pc->texture_barrier = etna_pipe_texture_barrier;
    /* temp until a real shader compiler */
    pc->create_etna_shader_state = etna_create_etna_shader_state;
    pc->bind_etna_shader_state = etna_bind_etna_shader_state;
    pc->delete_etna_shader_state = etna_delete_etna_shader_state;
    pc->set_etna_uniforms = etna_shader_set_uniforms;

    return pc;
}

/* Allocate 2D texture or render target resource 
 */
struct pipe_resource *etna_pipe_create_2d(struct pipe_context *pipe, unsigned flags, unsigned format, unsigned width, unsigned height, unsigned max_mip_level)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    unsigned element_size = pipe_element_size(format);
    if(!element_size)
        return NULL;
    
    /* Figure out what tiling to use -- for now, assume that textures cannot be supertiled, and cannot be linear.
     * There is a feature flag SUPERTILED_TEXTURE that may allow this, as well as TEXTURE_LINEAR, but not sure how it works. */
    unsigned layout = (!(flags & ETNA_IS_TEXTURE) && priv->specs.can_supertile) ? ETNA_LAYOUT_SUPERTILED : ETNA_LAYOUT_TILED;
    unsigned padding = etna_layout_multiple(layout);
    unsigned num_layers = 1;
    
    /* determine mipmap levels */
    struct etna_resource *resource = ETNA_NEW(struct etna_resource);
    if(flags & ETNA_IS_TEXTURE)
    {
        if(max_mip_level >= ETNA_NUM_LOD) /* max LOD supported by hw */
            max_mip_level = ETNA_NUM_LOD - 1;
        if(flags & ETNA_IS_CUBEMAP)
            num_layers = 6;
    } else
    {
        max_mip_level = 0;
    }

    /* take care about DXTx formats, which have a divSize of non-1x1
     * also: lower mipmaps are still 4x4 due to tiling. In as sense, compressed formats are already tiled.
     * XXX UYVY formats?
     */
    unsigned divSizeX = 0, divSizeY = 0;
    unsigned ix = 0;
    unsigned x = width, y = height;
    unsigned offset = 0;
    pipe_element_divsize(format, &divSizeX, &divSizeY);
    assert(divSizeX && divSizeY);
    while(true)
    {
        struct etna_resource_level *mip = &resource->levels[ix];
        mip->width = x;
        mip->height = y;
        mip->padded_width = etna_align_up(x, padding);
        mip->padded_height = etna_align_up(y, padding);
        mip->stride = etna_align_up(resource->levels[ix].padded_width, divSizeX)/divSizeX * element_size;
        mip->offset = offset;
        mip->layer_stride = etna_align_up(mip->padded_width, divSizeX)/divSizeX * 
                      etna_align_up(mip->padded_height, divSizeY)/divSizeY * element_size;
        mip->size = num_layers * mip->layer_stride;
        offset += mip->size;
        if(ix == max_mip_level || (x == 1 && y == 1))
            break; // stop at last level
        x = (x+1)>>1;
        y = (y+1)>>1;
        ix += 1;
    }
    resource->base.last_level = ix; /* real last mipmap level */

    /* Determine memory size, and whether to create a tile status */
    size_t rt_size = offset;
    size_t rt_ts_size = 0;
    if(flags & ETNA_IS_RENDER_TARGET) /* TS only for level 0 -- XXX is this formula correct? */
        rt_ts_size = etna_align_up(resource->levels[0].size*priv->specs.bits_per_tile/0x80, 0x100);
    
    /* determine memory type */
    gceSURF_TYPE memtype = gcvSURF_RENDER_TARGET;
    if(flags & ETNA_IS_TEXTURE)
        memtype = gcvSURF_TEXTURE;
    else if(pipe_format_is_depth(format)) /* if not a texture, and has a depth format */
        memtype = gcvSURF_DEPTH;

    printf("Allocate 2D surface of %ix%i (padded to %ix%i) of format %i (%i bpe), size %08x ts_size %08x, flags %08x\n",
            width, height, resource->levels[0].padded_width, resource->levels[0].padded_height, format, element_size, rt_size, rt_ts_size, flags);

    struct etna_vidmem *rt = 0;
    if(etna_vidmem_alloc_linear(priv->conn, &rt, rt_size, memtype, gcvPOOL_DEFAULT, true) != ETNA_OK)
    {
        printf("Problem allocating video memory for 2d resource\n");
        return NULL;
    }
   
    /* XXX allocate TS for rendertextures? if so, for each level or only the top? */
    struct etna_vidmem *rt_ts = 0;
    if(rt_ts_size && etna_vidmem_alloc_linear(priv->conn, &rt_ts, rt_ts_size, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, true)!=ETNA_OK)
    {
        printf("Problem allocating tile status for 2d resource\n");
        return NULL;
    }

    resource->base.target = (flags & ETNA_IS_CUBEMAP)?PIPE_TEXTURE_CUBE:PIPE_TEXTURE_2D;
    resource->base.format = format;
    resource->base.width0 = width;
    resource->base.height0 = height;
    resource->base.depth0 = 1;
    resource->base.array_size = num_layers;
    resource->layout = layout;
    resource->surface = rt;
    resource->ts = rt_ts;
    for(unsigned ix=0; ix<=resource->base.last_level; ++ix)
    {
        struct etna_resource_level *mip = &resource->levels[ix];
        mip->address = resource->surface->address + mip->offset;
        mip->logical = resource->surface->logical + mip->offset;
        printf("  %08x level %i: %ix%i (%i) stride=%i layer_stride=%i\n", 
                (int)mip->address, ix, (int)mip->width, (int)mip->height, (int)mip->size,
                (int)mip->stride, (int)mip->layer_stride);
        memset(mip->logical, 0, mip->size);
    }
    if(resource->ts) /* TS, if requested, only for level 0 */
    {
        resource->levels[0].ts_address = resource->ts->address;
        resource->levels[0].ts_size = resource->ts->size;
    }

    return &resource->base;
}

/* Allocate buffer resource 
 * Analogous to pipe_buffer_create in gallium tree
 */
struct pipe_resource *etna_pipe_create_buffer(struct pipe_context *pipe, unsigned flags, unsigned size)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct etna_vidmem *vtx = 0;

    if(etna_vidmem_alloc_linear(priv->conn, &vtx, size, 
           (flags & ETNA_IS_INDEX) ? gcvSURF_INDEX : gcvSURF_VERTEX, gcvPOOL_DEFAULT, true)!=ETNA_OK)
    {
        printf("Problem allocating video memory for buffer resource\n");
        return NULL;
    }
    printf("Allocate buffer surface of %i bytes (padded to %i), flags %08x\n",
            size, vtx->size, flags);

    struct etna_resource *resource = ETNA_NEW(struct etna_resource);
    resource->base.target = PIPE_BUFFER;
    resource->base.format = PIPE_FORMAT_R8_UNORM; /* want TYPELESS or similar */
    resource->base.width0 = vtx->size;
    resource->base.height0 = 0;
    resource->base.depth0 = 0;
    resource->base.array_size = 1;
    resource->base.last_level = 0;
    resource->layout = ETNA_LAYOUT_LINEAR;
    resource->surface = vtx;
    resource->ts = 0;
    resource->levels[0].address = resource->surface->address;
    resource->levels[0].logical = resource->surface->logical;
    resource->levels[0].ts_address = 0;
    resource->levels[0].stride = resource->levels[0].layer_stride = vtx->size;

    return &resource->base;
}

void etna_pipe_destroy_resource(struct pipe_context *pipe, struct pipe_resource *resource_)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct etna_resource *resource = etna_resource(resource_);
    if(resource == NULL)
        return;
    etna_vidmem_free(priv->conn, resource->surface);
    etna_vidmem_free(priv->conn, resource->ts);
    free(resource);
}

void *etna_pipe_get_resource_ptr(struct pipe_context *pipe, struct pipe_resource *resource_, unsigned layer, unsigned level)
{
    struct etna_resource *resource = etna_resource(resource_);
    if(layer >= resource->base.array_size || level > resource->base.last_level)
        return NULL;
    /// XXX size of returned area is resource->levels[lod].layer_stride 
    return resource->levels[level].logical + resource->levels[level].layer_stride * layer;
}

void etna_pipe_inline_write(struct pipe_context *pipe, struct pipe_resource *resource_, unsigned layer, unsigned level, void *data, size_t size)
{
    struct etna_resource *resource = etna_resource(resource_);
    if(layer >= resource->base.array_size || level > resource->base.last_level)
        return; /// XXX
    /// XXX size of returned area is resource->levels[lod].layer_stride 
    void *ptr = etna_pipe_get_resource_ptr(pipe, resource_, layer, level);
    if(resource->layout == ETNA_LAYOUT_LINEAR)
    {
        assert(size <= resource->levels[level].layer_stride);
        memcpy(ptr, data, size);
    } else if(resource->layout == ETNA_LAYOUT_TILED)
    {
        unsigned divx=0, divy=0;
        pipe_element_divsize(resource->base.format, &divx, &divy);
        assert(divx && divy);
        assert(size <= resource->levels[level].layer_stride);
        if(divx == 1 && divy == 1)
        {
            etna_texture_tile(ptr, data, resource->levels[level].width, resource->levels[level].height, 
                    resource->levels[level].stride, pipe_element_size(resource->base.format));
        } else { /* compressed format */
            memcpy(ptr, data, size);
        }
    } else
    {
        printf("etna_pipe_inline_write: unsupported tiling %i\n", resource->layout);
    }
}

