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
/* Shader state handling.
 */
#include "etna_shader.h"

#include "etna_pipe.h"
#include "etna_compiler.h"
#include "etna_debug.h"

#include "util/u_memory.h"
#include "util/u_math.h"

#include <etnaviv/state_3d.xml.h>

#define SET_STATE(addr, value) cs->addr = (value)
#define SET_STATE_FIXP(addr, value) cs->addr = (value)
#define SET_STATE_F32(addr, value) cs->addr = etna_f32_to_u32(value)

/* Link vs and fs together: fill in shader_state from vs and fs
 * as this function is called every time a new fs or vs is bound, the goal is to do 
 * little processing as possible here, and to precompute as much as possible in the 
 * vs/fs shader_object.
 * XXX we could cache the link result for a certain set of VS/PS; usually a pair
 * of VS and PS will be used together anyway.
 */
void etna_link_shaders(struct pipe_context *pipe,
                              struct compiled_shader_state *cs, 
                              const struct etna_shader_object *vs, const struct etna_shader_object *fs)
{
    assert(vs->processor == TGSI_PROCESSOR_VERTEX);
    assert(fs->processor == TGSI_PROCESSOR_FRAGMENT);
#ifdef DEBUG
    if(DBG_ENABLED(ETNA_DUMP_SHADERS))
    {
        etna_dump_shader_object(vs);
        etna_dump_shader_object(fs);
    }
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
    DBG_F(ETNA_LINKER_MSGS, "link result:");
    for(int idx=0; idx<fs->num_inputs; ++idx)
    {
        DBG_F(ETNA_LINKER_MSGS,"  %i -> %i", link.varyings_vs_reg[idx], idx+1);
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
    SET_STATE(PS_CONTROL, VIVS_PS_CONTROL_UNK1); /* XXX when can we set BYPASS? */
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
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
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
#endif

static void etna_set_constant_buffer(struct pipe_context *pipe,
                                uint shader, uint index,
                                struct pipe_constant_buffer *buf)
{
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
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
            memcpy(priv->shader_state.VS_UNIFORMS, buf->user_buffer, MIN2(buf->buffer_size, priv->vs->const_size * 4));
            priv->dirty_bits |= ETNA_STATE_VS_UNIFORMS;
            break;
        case PIPE_SHADER_FRAGMENT:
            memcpy(priv->shader_state.PS_UNIFORMS, buf->user_buffer, MIN2(buf->buffer_size, priv->fs->const_size * 4));
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
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
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
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
    struct etna_shader_object *fss = (struct etna_shader_object*)fss_;
    priv->dirty_bits |= ETNA_STATE_SHADER | ETNA_STATE_PS_UNIFORMS;
    assert(fss == NULL || fss->processor == TGSI_PROCESSOR_FRAGMENT);
    priv->fs = fss;
}

static void etna_pipe_bind_vs_state(struct pipe_context *pipe, void *vss_)
{
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
    struct etna_shader_object *vss = (struct etna_shader_object*)vss_;
    priv->dirty_bits |= ETNA_STATE_SHADER | ETNA_STATE_VS_UNIFORMS;
    assert(vss == NULL || vss->processor == TGSI_PROCESSOR_VERTEX);
    priv->vs = vss;
}

void etna_pipe_shader_init(struct pipe_context *pc)
{
    pc->create_fs_state = etna_pipe_create_shader_state;
    pc->bind_fs_state = etna_pipe_bind_fs_state;
    pc->delete_fs_state = etna_pipe_delete_shader_state;
    pc->create_vs_state = etna_pipe_create_shader_state;
    pc->bind_vs_state = etna_pipe_bind_vs_state;
    pc->delete_vs_state = etna_pipe_delete_shader_state;
    pc->set_constant_buffer = etna_set_constant_buffer;
}

