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
/* Raw shader state handling (for testing and debugging).
 */
#include "etna_rawshader.h"

#include "etna_pipe.h"
#include "etna_compiler.h"
#include "etna_debug.h"

#include "util/u_memory.h"
#include "util/u_math.h"

#include <etnaviv/state_3d.xml.h>

#define SET_STATE(addr, value) cs->addr = (value)
#define SET_STATE_FIXP(addr, value) cs->addr = (value)
#define SET_STATE_F32(addr, value) cs->addr = etna_f32_to_u32(value)

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

void etna_bind_shader_state(struct pipe_context *pipe, void *sh)
{
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
    priv->dirty_bits |= ETNA_STATE_SHADER | ETNA_STATE_PS_UNIFORMS | ETNA_STATE_VS_UNIFORMS;
    priv->shader_state = *((struct compiled_shader_state*)sh);
}

void etna_delete_shader_state(struct pipe_context *pipe, void *sh_)
{
    struct compiled_shader_state *sh = (struct compiled_shader_state*)sh_;
    FREE(sh->VS_INST_MEM);
    FREE(sh->PS_INST_MEM);
    FREE(sh);
}

void etna_set_uniforms(struct pipe_context *pipe, unsigned type, unsigned offset, unsigned count, const uint32_t *values)
{
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
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

