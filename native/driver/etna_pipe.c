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

#include <etnaviv/common.xml.h>
#include <etnaviv/state.xml.h>
#include <etnaviv/state_3d.xml.h>
#include <etnaviv/cmdstream.xml.h>
#include <etnaviv/viv.h>
#include <etnaviv/etna.h>
#include <etnaviv/etna_mem.h>
#include <etnaviv/etna_util.h>
#include <etnaviv/etna_tex.h>
#include <etnaviv/etna_fb.h>
#include <etnaviv/etna_rs.h>

#include "etna_shader.h"
#include "etna_debug.h"
#include "etna_fence.h"
#include "etna_transfer.h"

#include "pipe/p_defines.h"
#include "pipe/p_format.h"
#include "pipe/p_shader_tokens.h"
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "util/u_format.h"
#include "util/u_memory.h"
#include "util/u_transfer.h"
#include "util/u_surface.h"
#include "util/u_blitter.h"

/*********************************************************************/
/* Macros to define state */
#define SET_STATE(addr, value) cs->addr = (value)
#define SET_STATE_FIXP(addr, value) cs->addr = (value)
#define SET_STATE_F32(addr, value) cs->addr = etna_f32_to_u32(value)

/* Create bit field that specifies which samplers are active and thus need to be programmed
 * 32 bits is enough for 32 samplers. As far as I know this is the upper bound supported on any Vivante hw
 * up to GC4000.
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

/* Link vs and fs together: fill in shader_state from vs and fs
 * as this function is called every time a new fs or vs is bound, the goal is to do little code as possible here,
 * and to precompute as much as possible in the vs/fs shader_object.
 */
static void etna_link_shaders(struct pipe_context *pipe,
                              struct compiled_shader_state *cs, 
                              const struct etna_shader_object *vs, const struct etna_shader_object *fs)
{
    assert(vs->processor == TGSI_PROCESSOR_VERTEX);
    assert(fs->processor == TGSI_PROCESSOR_FRAGMENT);
#ifdef DEBUG
    etna_dump_shader_object(vs);
    etna_dump_shader_object(fs);
#endif

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
    SET_STATE(GL_VARYING_TOTAL_COMPONENTS, VIVS_GL_VARYING_TOTAL_COMPONENTS_NUM(align(total_components, 2)));
    SET_STATE(GL_VARYING_NUM_COMPONENTS, num_components);
    SET_STATE(GL_VARYING_COMPONENT_USE[0], component_use[0]);
    SET_STATE(GL_VARYING_COMPONENT_USE[1], component_use[1]);
    
    /* reference instruction memory */
#if 0
    {
        int fd=creat("/tmp/shader_vs.bin", 0777);
        write(fd, vs->code, vs->code_size*4);
        close(fd);
        fd=creat("/tmp/shader_ps.bin", 0777);
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
    
/* Weave state for draw operation. This function merges all the compiled state blocks under 
 * the context into one device register state. Parts of this state that are changed since 
 * last call (dirty) will be uploaded as state changes in the command buffer.
 */
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
     * - caching, don't re-emit cached state (use struct etna_3d_state as a write-through cache?)
     * - group consecutive states into one LOAD_STATE command stream
     * - update context: libetnaviv needs to provide interface for this
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
        /*0064C*/ EMIT_STATE(FE_VERTEX_STREAM_BASE_ADDR, FE_VERTEX_STREAM_BASE_ADDR, e->vertex_buffer[0].FE_VERTEX_STREAM_BASE_ADDR);
        /*00650*/ EMIT_STATE(FE_VERTEX_STREAM_CONTROL, FE_VERTEX_STREAM_CONTROL, e->vertex_buffer[0].FE_VERTEX_STREAM_CONTROL);
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
        /* XXX affected by supersampling as well (supersampling adds a PS input, and thus a temp when that makes #inputs>#temps) */
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

        if (ctx->conn->chip.pixel_pipes == 1)
        {
            /*01410*/ EMIT_STATE(PE_DEPTH_ADDR, PE_DEPTH_ADDR, e->framebuffer.PE_DEPTH_ADDR);
        }
        else if (ctx->conn->chip.pixel_pipes == 2)
        {
            /*01480*/ EMIT_STATE(PE_PIPE_DEPTH_ADDR(0), PE_PIPE_0_DEPTH_ADDR, e->framebuffer.PE_PIPE_DEPTH_ADDR[0]);
            /*01484*/ EMIT_STATE(PE_PIPE_DEPTH_ADDR(1), PE_PIPE_1_DEPTH_ADDR, e->framebuffer.PE_PIPE_DEPTH_ADDR[1]);
        }

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
        if (ctx->conn->chip.pixel_pipes == 1)
        {
            /*01430*/ EMIT_STATE(PE_COLOR_ADDR, PE_COLOR_ADDR, e->framebuffer.PE_COLOR_ADDR);
        }
        else if (ctx->conn->chip.pixel_pipes == 2)
        {
            /*01460*/ EMIT_STATE(PE_PIPE_COLOR_ADDR(0), PE_PIPE_0_COLOR_ADDR, e->framebuffer.PE_PIPE_COLOR_ADDR[0]);
            /*01464*/ EMIT_STATE(PE_PIPE_COLOR_ADDR(1), PE_PIPE_1_COLOR_ADDR, e->framebuffer.PE_PIPE_COLOR_ADDR[1]);
        }

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
        /* wait rasterizer until RS (PE) finished configuration */
        etna_stall(ctx, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);
    }

    e->dirty_bits = 0;
}
/*********************************************************************/
static void etna_pipe_destroy(struct pipe_context *pipe)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    if (priv->blitter)
        util_blitter_destroy(priv->blitter);

    etna_free(priv->ctx);
    FREE(priv);
    FREE(pipe);
}

static void etna_pipe_draw_vbo(struct pipe_context *pipe,
                 const struct pipe_draw_info *info)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    if(priv->vertex_elements == NULL || priv->vertex_elements->num_elements == 0)
        return; /* Nothing to do */
    int prims = translate_vertex_count(info->mode, info->count);
    if(unlikely(prims <= 0))
    {
        DBG("Invalid draw primitive mode=%i or no primitives to be drawn\n", info->mode);
        return;
    }
    /* First, sync state, then emit DRAW_PRIMITIVES or DRAW_INDEXED_PRIMITIVES */
    sync_context(pipe);
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
    struct compiled_blend_state *cs = CALLOC_STRUCT(compiled_blend_state);
    const struct pipe_rt_blend_state *rt0 = &bs->rt[0];
    bool enable = rt0->blend_enable && !(rt0->rgb_src_factor == PIPE_BLENDFACTOR_ONE && rt0->rgb_dst_factor == PIPE_BLENDFACTOR_ZERO &&
                                         rt0->alpha_src_factor == PIPE_BLENDFACTOR_ONE && rt0->alpha_dst_factor == PIPE_BLENDFACTOR_ZERO);
    bool separate_alpha = enable && !(rt0->rgb_src_factor == rt0->alpha_src_factor &&
                                      rt0->rgb_dst_factor == rt0->alpha_dst_factor);
    bool full_overwrite = (rt0->colormask == 15) && !enable;
    if(enable)
    {
        SET_STATE(PE_ALPHA_CONFIG, 
                VIVS_PE_ALPHA_CONFIG_BLEND_ENABLE_COLOR | 
                (separate_alpha ? VIVS_PE_ALPHA_CONFIG_BLEND_SEPARATE_ALPHA : 0) |
                VIVS_PE_ALPHA_CONFIG_SRC_FUNC_COLOR(translate_blend_factor(rt0->rgb_src_factor)) |
                VIVS_PE_ALPHA_CONFIG_SRC_FUNC_ALPHA(translate_blend_factor(rt0->alpha_src_factor)) |
                VIVS_PE_ALPHA_CONFIG_DST_FUNC_COLOR(translate_blend_factor(rt0->rgb_dst_factor)) |
                VIVS_PE_ALPHA_CONFIG_DST_FUNC_ALPHA(translate_blend_factor(rt0->alpha_dst_factor)) |
                VIVS_PE_ALPHA_CONFIG_EQ_COLOR(translate_blend(rt0->rgb_func)) |
                VIVS_PE_ALPHA_CONFIG_EQ_ALPHA(translate_blend(rt0->alpha_func))
                );
    } else {
        SET_STATE(PE_ALPHA_CONFIG, 0);
    }
    /* XXX should colormask be used if enable==false? */
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
    FREE(bs);
}

static void *etna_pipe_create_sampler_state(struct pipe_context *pipe,
                              const struct pipe_sampler_state *ss)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_sampler_state *cs = CALLOC_STRUCT(compiled_sampler_state);
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
    if(ss->min_mip_filter != PIPE_TEX_MIPFILTER_NONE)
    {
        cs->min_lod = float_to_fixp55(ss->min_lod);
        cs->max_lod = float_to_fixp55(ss->max_lod);
    } else { /* when not mipmapping, we need to set max/min lod so that always lowest LOD is selected */
        cs->min_lod = cs->max_lod = float_to_fixp55(ss->min_lod);
    }
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
    FREE(ss);
}

static void *etna_pipe_create_rasterizer_state(struct pipe_context *pipe,
                                 const struct pipe_rasterizer_state *rs)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_rasterizer_state *cs = CALLOC_STRUCT(compiled_rasterizer_state);
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
    /* XXX bottom_edge_rule */
    SET_STATE(PA_SYSTEM_MODE, 
            (rs->half_pixel_center ? (VIVS_PA_SYSTEM_MODE_UNK0 | VIVS_PA_SYSTEM_MODE_UNK4) : 0));
    /* rs->scissor overrides the scissor, defaulting to the whole framebuffer, with the scissor state */
    cs->scissor = rs->scissor;
    /* point size per vertex adds a vertex shader output */
    cs->VS_OUTPUT_COUNT = rs->point_size_per_vertex;

    assert(!rs->clip_halfz); /* could be supported with shader magic, actually D3D z is default on older gc */
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
    FREE(rs);
}

static void *etna_pipe_create_depth_stencil_alpha_state(struct pipe_context *pipe,
                                    const struct pipe_depth_stencil_alpha_state *dsa_p)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_depth_stencil_alpha_state *cs = CALLOC_STRUCT(compiled_depth_stencil_alpha_state);
    struct pipe_depth_stencil_alpha_state dsa = *dsa_p;
    /* XXX does stencil[0] / stencil[1] order depend on rs->front_ccw? */
    bool early_z = true;
    int i;

    /* Set operations to KEEP if write mask is 0.
     * When we don't do this, the depth buffer is written for the entire primitive instead of
     * just where the stencil condition holds (GC600 rev 0x0019, without feature CORRECT_STENCIL). 
     * Not sure if this is a hardware bug or just a strange edge case.
     */
    for(i=0; i<2; ++i)
    {
        if(dsa.stencil[i].writemask == 0)
        {
            dsa.stencil[i].fail_op = dsa.stencil[i].zfail_op = dsa.stencil[i].zpass_op = PIPE_STENCIL_OP_KEEP;
        }
    }

    /* Determine whether to enable early z reject. Don't enable it when any of the stencil functions is used. */
    if(dsa.stencil[0].enabled)
    {
        if(dsa.stencil[0].fail_op != PIPE_STENCIL_OP_KEEP || 
           dsa.stencil[0].zfail_op != PIPE_STENCIL_OP_KEEP ||
           dsa.stencil[0].zpass_op != PIPE_STENCIL_OP_KEEP)
        {
            early_z = false;
        }
        else if(dsa.stencil[1].enabled)
        {
            if(dsa.stencil[1].fail_op != PIPE_STENCIL_OP_KEEP || 
               dsa.stencil[1].zfail_op != PIPE_STENCIL_OP_KEEP ||
               dsa.stencil[1].zpass_op != PIPE_STENCIL_OP_KEEP)
            {
                early_z = false;
            }
        }
    }

    /* compare funcs have 1 to 1 mapping */
    SET_STATE(PE_DEPTH_CONFIG, 
            VIVS_PE_DEPTH_CONFIG_DEPTH_FUNC(dsa.depth.enabled ? dsa.depth.func : PIPE_FUNC_ALWAYS) |
            (dsa.depth.writemask ? VIVS_PE_DEPTH_CONFIG_WRITE_ENABLE : 0) |
            (early_z ? VIVS_PE_DEPTH_CONFIG_EARLY_Z : 0)
            );
    SET_STATE(PE_ALPHA_OP, 
            (dsa.alpha.enabled ? VIVS_PE_ALPHA_OP_ALPHA_TEST : 0) |
            VIVS_PE_ALPHA_OP_ALPHA_FUNC(dsa.alpha.func) |
            VIVS_PE_ALPHA_OP_ALPHA_REF(etna_cfloat_to_uint8(dsa.alpha.ref_value)));
    SET_STATE(PE_STENCIL_OP, 
            VIVS_PE_STENCIL_OP_FUNC_FRONT(dsa.stencil[0].func) |
            VIVS_PE_STENCIL_OP_FUNC_BACK(dsa.stencil[1].func) |
            VIVS_PE_STENCIL_OP_FAIL_FRONT(translate_stencil_op(dsa.stencil[0].fail_op)) | 
            VIVS_PE_STENCIL_OP_FAIL_BACK(translate_stencil_op(dsa.stencil[1].fail_op)) |
            VIVS_PE_STENCIL_OP_DEPTH_FAIL_FRONT(translate_stencil_op(dsa.stencil[0].zfail_op)) |
            VIVS_PE_STENCIL_OP_DEPTH_FAIL_BACK(translate_stencil_op(dsa.stencil[1].zfail_op)) |
            VIVS_PE_STENCIL_OP_PASS_FRONT(translate_stencil_op(dsa.stencil[0].zpass_op)) |
            VIVS_PE_STENCIL_OP_PASS_BACK(translate_stencil_op(dsa.stencil[1].zpass_op)));
    SET_STATE(PE_STENCIL_CONFIG, 
            translate_stencil_mode(dsa.stencil[0].enabled, dsa.stencil[1].enabled) |
            VIVS_PE_STENCIL_CONFIG_MASK_FRONT(dsa.stencil[0].valuemask) | 
            VIVS_PE_STENCIL_CONFIG_WRITE_MASK(dsa.stencil[0].writemask) 
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
    FREE(dsa);
}

static void *etna_pipe_create_vertex_elements_state(struct pipe_context *pipe,
                                      unsigned num_elements,
                                      const struct pipe_vertex_element *elements)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_vertex_elements_state *cs = CALLOC_STRUCT(compiled_vertex_elements_state);
    /* XXX could minimize number of consecutive stretches here by sorting, and 
     * permuting in shader or does Mesa do this already? */

    /* Check that vertex element binding is compatible with hardware; thus
     * elements[idx].vertex_buffer_index are < stream_count. If not, the binding
     * uses more streams than is supported, and we'll have to do some reorganization
     * for compatibility. 
     */
    bool incompatible = false;
    for(unsigned idx=0; idx<num_elements; ++idx)
    {
        if(elements[idx].vertex_buffer_index >= priv->specs.stream_count ||
           elements[idx].instance_divisor > 0)
            incompatible = true;
    }
    cs->num_elements = num_elements;
    if(incompatible)
    {
        DBG("Error: more vertex buffers used than supported");
        cs->num_elements = 0;
    } else {
        unsigned start_offset = 0; /* start of current consecutive stretch */
        bool nonconsecutive = true; /* previous value of nonconsecutive */
        for(unsigned idx=0; idx<num_elements; ++idx)
        {
            unsigned element_size = util_format_get_blocksize(elements[idx].src_format);
            unsigned end_offset = elements[idx].src_offset + element_size;
            if(nonconsecutive)
                start_offset = elements[idx].src_offset;
            assert(element_size != 0 && end_offset <= 256); /* maximum vertex size is 256 bytes */
            /* check whether next element is consecutive to this one */
            nonconsecutive = (idx == (num_elements-1)) || 
                        elements[idx+1].vertex_buffer_index != elements[idx].vertex_buffer_index ||
                        end_offset != elements[idx+1].src_offset;
            SET_STATE(FE_VERTEX_ELEMENT_CONFIG[idx], 
                    (nonconsecutive ? VIVS_FE_VERTEX_ELEMENT_CONFIG_NONCONSECUTIVE : 0) |
                    translate_vertex_format_type(elements[idx].src_format, false) |
                    VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM(util_format_get_nr_components(elements[idx].src_format)) |
                    translate_vertex_format_normalize(elements[idx].src_format) |
                    VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN(ENDIAN_MODE_NO_SWAP) |
                    VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM(elements[idx].vertex_buffer_index) |
                    VIVS_FE_VERTEX_ELEMENT_CONFIG_START(elements[idx].src_offset) |
                    VIVS_FE_VERTEX_ELEMENT_CONFIG_END(end_offset - start_offset));
        }
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
    struct compiled_vertex_elements_state *cs = (struct compiled_vertex_elements_state*)ve;
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    FREE(cs);
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

    priv->stencil_ref_s = *sr;

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

    priv->sample_mask_s = sample_mask;

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

    /* XXX support multisample 2X/4X, take care that required width/height is doubled
     * for both depth and color surfaces.
     */
    SET_STATE(GL_MULTI_SAMPLE_CONFIG, 
            VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES_NONE
            /* VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_ENABLES(0xf)
            VIVS_GL_MULTI_SAMPLE_CONFIG_UNK12 |
            VIVS_GL_MULTI_SAMPLE_CONFIG_UNK16 */
            );  /* merged with sample_mask */

    /* Set up TS as well. Warning: this state is used by both the RS and PE */
    uint32_t ts_mem_config = 0;
    if(sv->nr_cbufs > 0) /* at least one color buffer? */
    {
        struct etna_surface *cbuf = etna_surface(sv->cbufs[0]);
        bool color_supertiled = (cbuf->layout & 2)!=0;
        assert(cbuf->layout & 1); /* Cannot render to linear surfaces */
        pipe_surface_reference(&cs->cbuf, &cbuf->base);
        SET_STATE(PE_COLOR_FORMAT, 
                VIVS_PE_COLOR_FORMAT_FORMAT(translate_rt_format(cbuf->base.format, false)) |
                (color_supertiled ? VIVS_PE_COLOR_FORMAT_SUPER_TILED : 0) /* XXX depends on layout */
                /* XXX VIVS_PE_COLOR_FORMAT_OVERWRITE and the rest comes from blend_state / depth_stencil_alpha */
                ); /* merged with depth_stencil_alpha */
        if (priv->ctx->conn->chip.pixel_pipes == 1)
        {
            SET_STATE(PE_COLOR_ADDR, cbuf->surf.address);
        }
        else if (priv->ctx->conn->chip.pixel_pipes == 2)
        {
            SET_STATE(PE_PIPE_COLOR_ADDR[0], cbuf->surf.address);
            SET_STATE(PE_PIPE_COLOR_ADDR[1], cbuf->surf.address);  /* TODO */
        }
        SET_STATE(PE_COLOR_STRIDE, cbuf->surf.stride);
        if(cbuf->surf.ts_address)
        {
            ts_mem_config |= VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR;
            SET_STATE(TS_COLOR_CLEAR_VALUE, cbuf->clear_value);
            SET_STATE(TS_COLOR_STATUS_BASE, cbuf->surf.ts_address);
            SET_STATE(TS_COLOR_SURFACE_BASE, cbuf->surf.address);
        }
        /* XXX ts_mem_config |= VIVS_TS_MEM_CONFIG_MSAA | translate_msaa_format(cbuf->format) */
    } else {
        pipe_surface_reference(&cs->cbuf, NULL);
        SET_STATE(PE_COLOR_FORMAT, 0); /* Is this enough to render without color? */
    }

    if(sv->zsbuf != NULL)
    {
        struct etna_surface *zsbuf = etna_surface(sv->zsbuf);
        pipe_surface_reference(&cs->zsbuf, &zsbuf->base);
        assert(zsbuf->layout & 1); /* Cannot render to linear surfaces */
        uint32_t depth_format = translate_depth_format(zsbuf->base.format, false);
        unsigned depth_bits = depth_format == VIVS_PE_DEPTH_CONFIG_DEPTH_FORMAT_D16 ? 16 : 24; 
        bool depth_supertiled = (zsbuf->layout & 2)!=0;
        SET_STATE(PE_DEPTH_CONFIG, 
                depth_format |
                (depth_supertiled ? VIVS_PE_DEPTH_CONFIG_SUPER_TILED : 0) | /* XXX depends on layout */
                VIVS_PE_DEPTH_CONFIG_DEPTH_MODE_Z /* XXX set to NONE if no Z buffer */
                /* VIVS_PE_DEPTH_CONFIG_ONLY_DEPTH */
                ); /* merged with depth_stencil_alpha */
        if (priv->ctx->conn->chip.pixel_pipes == 1)
        {
            SET_STATE(PE_DEPTH_ADDR, zsbuf->surf.address);
        }
        else if (priv->ctx->conn->chip.pixel_pipes == 2)
        {
            SET_STATE(PE_PIPE_DEPTH_ADDR[0], zsbuf->surf.address);
            SET_STATE(PE_PIPE_DEPTH_ADDR[1], zsbuf->surf.address);  /* TODO */
        }
        SET_STATE(PE_DEPTH_STRIDE, zsbuf->surf.stride);
        SET_STATE(PE_HDEPTH_CONTROL, VIVS_PE_HDEPTH_CONTROL_FORMAT_DISABLED);
        SET_STATE_F32(PE_DEPTH_NORMALIZE, exp2f(depth_bits) - 1.0f);
        if(zsbuf->surf.ts_address)
        {
            ts_mem_config |= VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
                (depth_bits == 16 ? VIVS_TS_MEM_CONFIG_DEPTH_16BPP : 0) | 
                VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION;
            SET_STATE(TS_DEPTH_CLEAR_VALUE, zsbuf->clear_value);
            SET_STATE(TS_DEPTH_STATUS_BASE, zsbuf->surf.ts_address);
            SET_STATE(TS_DEPTH_SURFACE_BASE, zsbuf->surf.address);
        }
    } else {
        pipe_surface_reference(&cs->zsbuf, NULL);
        SET_STATE(PE_DEPTH_CONFIG, VIVS_PE_DEPTH_CONFIG_DEPTH_MODE_NONE);
    }

    SET_STATE_FIXP(SE_SCISSOR_LEFT, 0); /* affected by rasterizer and scissor state as well */
    SET_STATE_FIXP(SE_SCISSOR_TOP, 0);
    SET_STATE_FIXP(SE_SCISSOR_RIGHT, (sv->width << 16)-1);
    SET_STATE_FIXP(SE_SCISSOR_BOTTOM, (sv->height << 16)-1);

    SET_STATE(TS_MEM_CONFIG, ts_mem_config);

    priv->dirty_bits |= ETNA_STATE_FRAMEBUFFER;
    priv->framebuffer_s = *sv; /* keep copy of original structure */
}

static void etna_pipe_set_scissor_state( struct pipe_context *pipe,
                          const struct pipe_scissor_state *ss)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_scissor_state *cs = &priv->scissor;
    priv->scissor_s = *ss;
    SET_STATE_FIXP(SE_SCISSOR_LEFT, (ss->minx << 16));
    SET_STATE_FIXP(SE_SCISSOR_TOP, (ss->miny << 16));
    SET_STATE_FIXP(SE_SCISSOR_RIGHT, (ss->maxx << 16)-1);
    SET_STATE_FIXP(SE_SCISSOR_BOTTOM, (ss->maxy << 16)-1);
    /* note that this state is only used when rasterizer_state->scissor is on */
    priv->dirty_bits |= ETNA_STATE_VIEWPORT;
}

static void etna_pipe_set_viewport_state( struct pipe_context *pipe,
                           const struct pipe_viewport_state *vs)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_viewport_state *cs = &priv->viewport;
    priv->viewport_s = *vs;
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
    unsigned idx;
    priv->dirty_bits |= ETNA_STATE_SAMPLER_VIEWS;
    priv->num_fragment_sampler_views = num_views;
    for(idx=0; idx<num_views; ++idx)
    {
        pipe_sampler_view_reference(&priv->sampler_view_s[idx], info[idx]);
        priv->sampler_view[idx] = *etna_sampler_view(info[idx])->internal;
    }
    for(; idx<priv->specs.fragment_sampler_count; ++idx)
    {
        pipe_sampler_view_reference(&priv->sampler_view_s[idx], NULL);
    }
}

static void etna_pipe_set_vertex_sampler_views(struct pipe_context *pipe,
                                  unsigned num_views,
                                  struct pipe_sampler_view **info)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    unsigned idx;
    unsigned offset = priv->specs.vertex_sampler_offset;
    priv->dirty_bits |= ETNA_STATE_SAMPLER_VIEWS;
    priv->num_vertex_sampler_views = num_views;
    for(idx=0; idx<num_views; ++idx)
    {
        pipe_sampler_view_reference(&priv->sampler_view_s[offset + idx], info[idx]);
        priv->sampler_view[offset + idx] = *etna_sampler_view(info[idx])->internal;
    }
    for(; idx<priv->specs.vertex_sampler_count; ++idx)
    {
        pipe_sampler_view_reference(&priv->sampler_view_s[offset + idx], NULL);
    }
}

static void etna_pipe_set_vertex_buffers( struct pipe_context *pipe,
                           unsigned start_slot,
                           unsigned num_buffers,
                           const struct pipe_vertex_buffer *vb)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    assert((start_slot + num_buffers) <= PIPE_MAX_ATTRIBS);
    struct pipe_vertex_buffer zero_vb = {};
    for(unsigned idx=0; idx<num_buffers; ++idx)
    {
        unsigned slot = start_slot + idx; /* copy from vb[idx] to priv->...[slot] */
        const struct pipe_vertex_buffer *vbi = vb ? &vb[idx] : &zero_vb;
        struct compiled_set_vertex_buffer *cs = &priv->vertex_buffer[slot];
        assert(!vbi->user_buffer); /* XXX support user_buffer using etna_usermem_map */
        /* copy pipe_vertex_buffer structure and take reference */
        priv->vertex_buffer_s[slot].stride = vbi->stride;
        priv->vertex_buffer_s[slot].buffer_offset = vbi->buffer_offset;
        pipe_resource_reference(&priv->vertex_buffer_s[slot].buffer, vbi->buffer);
        priv->vertex_buffer_s[slot].user_buffer = vbi->user_buffer;
        /* determine addresses */
        viv_addr_t gpu_addr = 0;
        cs->logical = 0;
        if(vbi->buffer) /* GPU buffer */
        {
            gpu_addr = etna_resource(vbi->buffer)->levels[0].address + vbi->buffer_offset;
            cs->logical = etna_resource(vbi->buffer)->levels[0].logical + vbi->buffer_offset;
        }
        /* compiled state */
        SET_STATE(FE_VERTEX_STREAM_CONTROL, VIVS_FE_VERTEX_STREAM_CONTROL_VERTEX_STRIDE(vbi->stride));
        SET_STATE(FE_VERTEX_STREAM_BASE_ADDR, gpu_addr);
    }
    
    priv->dirty_bits |= ETNA_STATE_VERTEX_BUFFERS;
}

static void etna_pipe_set_index_buffer( struct pipe_context *pipe,
                         const struct pipe_index_buffer *ib)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct compiled_set_index_buffer *cs = &priv->index_buffer;
    if(ib == NULL)
    {
        pipe_resource_reference(&priv->index_buffer_s.buffer, NULL); /* update reference to buffer */
        cs->logical = NULL;
        SET_STATE(FE_INDEX_STREAM_CONTROL, 0);
        SET_STATE(FE_INDEX_STREAM_BASE_ADDR, 0);
    } else
    {
        assert(ib->buffer); /* XXX user_buffer using etna_usermem_map */
        pipe_resource_reference(&priv->index_buffer_s.buffer, ib->buffer); /* update reference to buffer */
        priv->index_buffer_s.index_size = ib->index_size;
        priv->index_buffer_s.offset = ib->offset;
        priv->index_buffer_s.user_buffer = ib->user_buffer;

        SET_STATE(FE_INDEX_STREAM_CONTROL, 
                translate_index_size(ib->index_size));
        SET_STATE(FE_INDEX_STREAM_BASE_ADDR, etna_resource(ib->buffer)->levels[0].address + ib->offset);
        cs->logical = etna_resource(ib->buffer)->levels[0].logical + ib->offset;
    }
    priv->dirty_bits |= ETNA_STATE_INDEX_BUFFER;
}

/* Generate clear command for a surface (non-TS case) */
static void etna_rs_gen_clear_surface(struct etna_surface *surf, uint32_t clear_value)
{
    uint bs = util_format_get_blocksize(surf->base.format);
    uint format = 0;
    switch(bs) 
    {
    case 2: format = RS_FORMAT_A1R5G5B5; break;
    case 4: format = RS_FORMAT_A8R8G8B8; break;
    default: printf("Unhandled clear blocksize: %i (fmt %i)\n", bs, surf->base.format);
             format = RS_FORMAT_A8R8G8B8;
    }
    etna_compile_rs_state(&surf->clear_command, &(struct rs_state){
            .source_format = format,
            .dest_format = format,
            .dest_addr = surf->surf.address,
            .dest_stride = surf->surf.stride,
            .dest_tiling = surf->layout,
            .dither = {0xffffffff, 0xffffffff},
            .width = surf->surf.width,
            .height = surf->surf.height,
            .clear_value = {clear_value},
            .clear_mode = VIVS_RS_CLEAR_CONTROL_MODE_ENABLED1,
            .clear_bits = 0xffff
        });
}

static void etna_pipe_clear(struct pipe_context *pipe,
             unsigned buffers,
             const union pipe_color_union *color,
             double depth,
             unsigned stencil)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    /* No need to set up the TS here with sync_context.
     * RS clear operations (in contrary to resolve and copy) do not require the TS state. 
     */
    /* Need to update clear command in non-TS (fast clear) case *if*
     * clear value is different from previous time. 
     */
    if(buffers & PIPE_CLEAR_COLOR)
    {
        for(int idx=0; idx<priv->framebuffer_s.nr_cbufs; ++idx)
        {
            struct etna_surface *surf = etna_surface(priv->framebuffer_s.cbufs[idx]);
            uint32_t new_clear_value = translate_clear_color(surf->base.format, &color[idx]);
            if(surf->surf.ts_address) /* TS: use precompiled clear command */
            {
                priv->framebuffer.TS_COLOR_CLEAR_VALUE = new_clear_value;
                priv->dirty_bits |= ETNA_STATE_TS;
            }
            else if(unlikely(new_clear_value != surf->clear_value)) /* Queue normal RS clear for non-TS surfaces */
            {
                etna_rs_gen_clear_surface(surf, new_clear_value);
            }
            etna_submit_rs_state(priv->ctx, &surf->clear_command);
            surf->clear_value = new_clear_value; 
        }
    }
    if((buffers & PIPE_CLEAR_DEPTHSTENCIL) && priv->framebuffer_s.zsbuf != NULL)
    {
        struct etna_surface *surf = etna_surface(priv->framebuffer_s.zsbuf);
        uint32_t new_clear_value = translate_clear_depth_stencil(surf->base.format, depth, stencil);
        if(surf->surf.ts_address) /* TS: use precompiled clear command */
        {
            priv->framebuffer.TS_DEPTH_CLEAR_VALUE = new_clear_value;
            priv->dirty_bits |= ETNA_STATE_TS;
        } else if(unlikely(new_clear_value != surf->clear_value)) /* Queue normal RS clear for non-TS surfaces */
        {
            etna_rs_gen_clear_surface(surf, new_clear_value);
        }
        etna_submit_rs_state(priv->ctx, &surf->clear_command);
        surf->clear_value = new_clear_value;
    }
    /* Wait rasterizer until PE finished updating. This makes sure that it sees the updated surface.
     */
    etna_stall(priv->ctx, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);
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
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    if(fence)
    {
        if(etna_fence_new(pipe->screen, priv->ctx, fence) != ETNA_OK)
        {
            printf("etna_pipe_flush: could not create fence\n");
        }
    }
    etna_flush(priv->ctx);
}

static struct pipe_sampler_view *etna_pipe_create_sampler_view(struct pipe_context *pipe,
                                                 struct pipe_resource *texture,
                                                 const struct pipe_sampler_view *templat)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct etna_sampler_view *sv = CALLOC_STRUCT(etna_sampler_view);
    sv->base = *templat;
    sv->base.context = pipe;
    sv->base.texture = 0;
    pipe_resource_reference(&sv->base.texture, texture);
    sv->base.texture = texture;
    assert(sv->base.texture);

    struct compiled_sampler_view *cs = CALLOC_STRUCT(compiled_sampler_view);
    struct etna_resource *res = etna_resource(sv->base.texture);
    assert(res != NULL);

    SET_STATE(TE_SAMPLER_CONFIG0, 
                VIVS_TE_SAMPLER_CONFIG0_TYPE(translate_texture_target(res->base.target, false)) |
                VIVS_TE_SAMPLER_CONFIG0_FORMAT(translate_texture_format(sv->base.format, false)) 
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
    pipe_reference_init(&sv->base.reference, 1);
    return &sv->base;
}

static void etna_pipe_sampler_view_destroy(struct pipe_context *pipe,
                            struct pipe_sampler_view *view)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    pipe_resource_reference(&view->texture, NULL);
    FREE(etna_sampler_view(view)->internal);
    FREE(view);
}

static struct pipe_surface *etna_pipe_create_surface(struct pipe_context *pipe,
                                      struct pipe_resource *resource_,
                                      const struct pipe_surface *templat)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct etna_surface *surf = CALLOC_STRUCT(etna_surface);
    struct etna_resource *resource = etna_resource(resource_);
    assert(templat->u.tex.first_layer == templat->u.tex.last_layer);
    unsigned layer = templat->u.tex.first_layer;
    unsigned level = templat->u.tex.level;
    assert(layer < resource->base.array_size);
   
    surf->base.context = pipe;

    pipe_reference_init(&surf->base.reference, 1);
    pipe_resource_reference(&surf->base.texture, &resource->base);

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

    if(surf->surf.ts_address)
    {
        /* This abuses the RS as a plain buffer memset().
           Currently uses a fixed row size of 64 bytes. Some benchmarking with different sizes may be in order.
         */
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
    } else {
        etna_rs_gen_clear_surface(surf, surf->clear_value);
    }
    return &surf->base;
}

static void etna_pipe_surface_destroy(struct pipe_context *pipe, struct pipe_surface *surf)
{
    //struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    pipe_resource_reference(&surf->texture, NULL);
    FREE(surf);
}

static void etna_pipe_texture_barrier(struct pipe_context *pipe)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    /* clear texture cache */
    etna_set_state(priv->ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_TEXTURE | VIVS_GL_FLUSH_CACHE_TEXTUREVS);
}

#ifdef RAWSHADER
/* XXX to be removed: test entry point */
void *etna_create_shader_state(struct pipe_context *pipe, const struct etna_shader_program *rs)
{
    struct compiled_shader_state *cs = CALLOC_STRUCT(compiled_shader_state);
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
    SET_STATE(GL_VARYING_TOTAL_COMPONENTS, VIVS_GL_VARYING_TOTAL_COMPONENTS_NUM(align(total_components, 2)));
    SET_STATE(GL_VARYING_NUM_COMPONENTS, num_components);
    SET_STATE(GL_VARYING_COMPONENT_USE[0], component_use[0]);
    SET_STATE(GL_VARYING_COMPONENT_USE[1], component_use[1]);

    cs->vs_inst_mem_size = rs->vs_code_size;
    cs->vs_uniforms_size = rs->vs_uniforms_size;
    cs->ps_inst_mem_size = rs->ps_code_size;
    cs->ps_uniforms_size = rs->ps_uniforms_size;
    cs->VS_INST_MEM = mem_dup(rs->vs_code, rs->vs_code_size * 4);
    cs->PS_INST_MEM = mem_dup(rs->ps_code, rs->ps_code_size * 4);
    memcpy(cs->VS_UNIFORMS, rs->vs_uniforms, rs->vs_uniforms_size*4);
    memcpy(cs->PS_UNIFORMS, rs->ps_uniforms, rs->ps_uniforms_size*4);

    return cs;
}

/* XXX to be removed: test entry point */
void etna_bind_shader_state(struct pipe_context *pipe, void *sh)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    priv->dirty_bits |= ETNA_STATE_SHADER | ETNA_STATE_PS_UNIFORMS | ETNA_STATE_VS_UNIFORMS;
    priv->shader_state = *((struct compiled_shader_state*)sh);
}

/* XXX to be removed: test entry point */
void etna_delete_shader_state(struct pipe_context *pipe, void *sh_)
{
    struct compiled_shader_state *sh = (struct compiled_shader_state*)sh_;
    FREE(sh->VS_INST_MEM);
    FREE(sh->PS_INST_MEM);
    FREE(sh);
}

/* XXX to be removed: test entry point */
void etna_set_uniforms(struct pipe_context *pipe, unsigned type, unsigned offset, unsigned count, const uint32_t *values)
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
#endif

static void etna_set_constant_buffer(struct pipe_context *pipe,
                                uint shader, uint index,
                                struct pipe_constant_buffer *buf)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    if(buf == NULL) /* Unbinding constant buffer is a no-op as we don't keep a pointer */
        return;
    assert(buf->buffer == NULL && buf->user_buffer != NULL); 
    /* support only user buffer for now */
    assert(priv->vs && priv->fs);
    if(likely(index == 0))
    {
        /* copy only up to shader-specific constant size; never overwrite immediates */
        switch(shader)
        {
        case PIPE_SHADER_VERTEX:
            memcpy(priv->shader_state.VS_UNIFORMS, buf->user_buffer, etna_umin(buf->buffer_size, priv->vs->const_size * 4));
            priv->dirty_bits |= ETNA_STATE_VS_UNIFORMS;
            break;
        case PIPE_SHADER_FRAGMENT:
            memcpy(priv->shader_state.PS_UNIFORMS, buf->user_buffer, etna_umin(buf->buffer_size, priv->fs->const_size * 4));
            priv->dirty_bits |= ETNA_STATE_PS_UNIFORMS;
            break;
        default: printf("Unhandled shader type %i\n", shader);
        }
    } else {
        printf("Unhandled buffer index %i\n", index);
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

static void etna_pipe_set_clip_state(struct pipe_context *pipe, const struct pipe_clip_state *pcs)
{
    /* NOOP */
}

static void etna_pipe_resource_copy_region(struct pipe_context *pipe,
                            struct pipe_resource *dst,
                            unsigned dst_level,
                            unsigned dstx, unsigned dsty, unsigned dstz,
                            struct pipe_resource *src,
                            unsigned src_level,
                            const struct pipe_box *src_box)
{
    /* The resource must be of the same format. */
    assert(src->format == dst->format);
    /* Resources with nr_samples > 1 are not allowed. */
    assert(src->nr_samples == 1 && dst->nr_samples == 1);
    /* XXX we can use the RS as a literal copy engine here 
     * the only complexity is tiling; the size of the boxes needs to be aligned to the tile size 
     * how to handle the case where a resource is copied from/to a non-aligned position?
     * from non-aligned: can fall back to rendering-based copy?
     * to non-aligned: can fall back to rendering-based copy?
     */
}

static void etna_pipe_blit(struct pipe_context *pipe, const struct pipe_blit_info *blit_info)
{
    /* This is a more extended version of resource_copy_region */
    /* TODO Some cases can be handled by RS; if not, fall back to rendering */
    /* copy block of pixels from info->src to info->dst (resource, level, box, format);
     * function is used for scaling, flipping in x and y direction (negative width/height), format conversion, mask and filter
     * and even a scissor rectangle
     *
     * What can the RS do for us:
     *   convert between tiling formats (layouts)
     *   downsample 2x in x and y
     *   convert between a limited number of pixel formats
     *
     * For the rest, fall back to util_blitter
     */
    struct pipe_blit_info info = *blit_info;
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    if (info.src.resource->nr_samples > 1 &&
                info.dst.resource->nr_samples <= 1 &&
                !util_format_is_depth_or_stencil(info.src.resource->format) &&
                !util_format_is_pure_integer(info.src.resource->format)) {
        DBG("color resolve unimplemented");
        return;
    }
#if 0 /* remove once implemented */
    if (util_try_blit_via_copy_region(pctx, &info)) {
        return; /* done */
    }
#endif
    if (info.mask & PIPE_MASK_S) {
        DBG("cannot blit stencil, skipping");
        info.mask &= ~PIPE_MASK_S;
    }

    if (!util_blitter_is_blit_supported(priv->blitter, &info)) {
        DBG("blit unsupported %s -> %s",
                        util_format_short_name(info.src.resource->format),
                        util_format_short_name(info.dst.resource->format));
        return;
    }

    /* save current state */
    util_blitter_save_vertex_buffer_slot(priv->blitter, &priv->vertex_buffer_s[0]);
    util_blitter_save_vertex_elements(priv->blitter, priv->vertex_elements);
    util_blitter_save_vertex_shader(priv->blitter, priv->vs);
    util_blitter_save_rasterizer(priv->blitter, priv->rasterizer);
    util_blitter_save_viewport(priv->blitter, &priv->viewport_s);
    util_blitter_save_scissor(priv->blitter, &priv->scissor_s);
    util_blitter_save_fragment_shader(priv->blitter, priv->fs);
    util_blitter_save_blend(priv->blitter, priv->blend);
    util_blitter_save_depth_stencil_alpha(priv->blitter, priv->depth_stencil_alpha);
    util_blitter_save_stencil_ref(priv->blitter, &priv->stencil_ref_s);
    util_blitter_save_sample_mask(priv->blitter, priv->sample_mask_s);
    util_blitter_save_framebuffer(priv->blitter, &priv->framebuffer_s);
    util_blitter_save_fragment_sampler_states(priv->blitter,
                    priv->num_fragment_samplers,
                    (void **)priv->sampler);
    util_blitter_save_fragment_sampler_views(priv->blitter,
                    priv->num_fragment_sampler_views, priv->sampler_view_s);

    util_blitter_blit(priv->blitter, &info);

}

static void etna_pipe_set_polygon_stipple(struct pipe_context *pctx,
		const struct pipe_poly_stipple *stipple)
{
    /* NOP */
}

struct pipe_context *etna_new_pipe_context(struct viv_conn *dev, const struct etna_pipe_specs *specs, struct pipe_screen *screen)
{
    struct pipe_context *pc = CALLOC_STRUCT(pipe_context);
    if(pc == NULL)
        return NULL;

    pc->priv = CALLOC_STRUCT(etna_pipe_context_priv);
    if(pc->priv == NULL)
    {
        FREE(pc);
        return NULL;
    }
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pc);

    pc->screen = screen;

    if(etna_create(dev, &priv->ctx) < 0)
    {
        FREE(pc->priv);
        FREE(pc);
        return NULL;
    }

    /* context private setup */
    priv->dirty_bits = 0xffffffff;
    priv->conn = dev;
    priv->specs = *specs;
    util_slab_create(&priv->transfer_pool, sizeof(struct etna_transfer),
                     16, UTIL_SLAB_SINGLETHREADED);

    /* TODO set sensible defaults for the other state */
    priv->base_setup.PA_W_CLIP_LIMIT = 0x34000001;
    priv->base_setup.GL_VERTEX_ELEMENT_CONFIG = 0x1;

    /* fill in vtable entries one by one */
    pc->destroy = etna_pipe_destroy;
    pc->draw_vbo = etna_pipe_draw_vbo;
    /* XXX render_condition */
    /* XXX create_query */
    /* XXX destroy_query */
    /* XXX begin_query */
    /* XXX end_query */
    /* XXX get_query_result */
    pc->create_blend_state = etna_pipe_create_blend_state;
    pc->bind_blend_state = etna_pipe_bind_blend_state;
    pc->delete_blend_state = etna_pipe_delete_blend_state;
    pc->create_sampler_state = etna_pipe_create_sampler_state;
    pc->bind_fragment_sampler_states = etna_pipe_bind_fragment_sampler_states;
    pc->bind_vertex_sampler_states = etna_pipe_bind_vertex_sampler_states;
    /* XXX bind_geometry_sampler_states */
    /* XXX bind_compute_sampler_states */
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
    /* XXX create_gs_state */
    /* XXX bind_gs_state */
    /* XXX delete_gs_state */
    pc->create_vertex_elements_state = etna_pipe_create_vertex_elements_state;
    pc->bind_vertex_elements_state = etna_pipe_bind_vertex_elements_state;
    pc->delete_vertex_elements_state = etna_pipe_delete_vertex_elements_state;
    pc->set_blend_color = etna_pipe_set_blend_color;
    pc->set_stencil_ref = etna_pipe_set_stencil_ref;
    pc->set_sample_mask = etna_pipe_set_sample_mask;
    pc->set_clip_state = etna_pipe_set_clip_state;
    pc->set_constant_buffer = etna_set_constant_buffer;
    pc->set_framebuffer_state = etna_pipe_set_framebuffer_state;
    pc->set_polygon_stipple = etna_pipe_set_polygon_stipple;
    pc->set_scissor_state = etna_pipe_set_scissor_state;
    pc->set_viewport_state = etna_pipe_set_viewport_state;
    pc->set_fragment_sampler_views = etna_pipe_set_fragment_sampler_views;
    pc->set_vertex_sampler_views = etna_pipe_set_vertex_sampler_views;
    /* XXX set_geometry_sampler_views */
    /* XXX set_compute_sampler_views */
    /* XXX set_shader_resources */
    pc->set_vertex_buffers = etna_pipe_set_vertex_buffers;
    pc->set_index_buffer = etna_pipe_set_index_buffer;
    /* XXX create_stream_output_target */
    /* XXX stream_output_target_destroy */
    /* XXX set_stream_output_targets */
    pc->resource_copy_region = etna_pipe_resource_copy_region;
    pc->blit = etna_pipe_blit;
    pc->clear = etna_pipe_clear;
    pc->clear_render_target = etna_pipe_clear_render_target;
    pc->clear_depth_stencil = etna_pipe_clear_depth_stencil;
    pc->flush = etna_pipe_flush;
    pc->create_sampler_view = etna_pipe_create_sampler_view;
    pc->sampler_view_destroy = etna_pipe_sampler_view_destroy;
    pc->create_surface = etna_pipe_create_surface;
    pc->surface_destroy = etna_pipe_surface_destroy;
    pc->transfer_map = etna_pipe_transfer_map;
    pc->transfer_flush_region = etna_pipe_transfer_flush_region;
    pc->transfer_unmap = etna_pipe_transfer_unmap;
    pc->transfer_inline_write = u_default_transfer_inline_write;
    pc->texture_barrier = etna_pipe_texture_barrier;
    /* XXX create_video_decoder */
    /* XXX create_video_buffer */
    /* XXX create_compute_state */
    /* XXX bind_compute_state */
    /* XXX delete_compute_state */
    /* XXX set_compute_resources */
    /* XXX set_global_binding */
    /* XXX launch_grid */

    priv->blitter = util_blitter_create(pc);
    return pc;
}

struct etna_ctx *etna_pipe_get_etna_context(struct pipe_context *pipe)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    return priv->ctx;
}

