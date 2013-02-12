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
/* Mip cube, but in terms of minigallium pipe
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

#include "etna/common.xml.h"
#include "etna/state.xml.h"
#include "etna/state_3d.xml.h"
#include "etna/cmdstream.xml.h"

#include "write_bmp.h"
#include "viv.h"
#include "etna.h"
#include "etna_state.h"
#include "etna_rs.h"
#include "etna_fb.h"
#include "etna_bswap.h"
#include "etna_tex.h"

#include "esTransform.h"
#include "esShapes.h"
#include "dds.h"

#define ETNA_NUM_INPUTS 16
#define ETNA_NUM_VARYINGS 16

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
    unsigned ra_control; // unknown 1 or 3
    unsigned num_inputs;
    struct etna_shader_input inputs[ETNA_NUM_INPUTS];
    unsigned num_varyings;
    struct etna_shader_varying varyings[ETNA_NUM_VARYINGS]; 
    
    unsigned vs_code_size; /* Vertex shader code size in words */ 
    uint32_t *vs_code;
    unsigned vs_pos_out_reg; /* VS position output */
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
    uint32_t *VS_UNIFORMS;
    uint32_t *PS_INST_MEM;
    uint32_t *PS_UNIFORMS;
};

#define SET_STATE(addr, value) cs->addr = (value)
#define SET_STATE_FIXP(addr, value) cs->addr = (value)
#define SET_STATE_F32(addr, value) cs->addr = f32_to_u32(value)

/* strdup for string of 32-bit words */
static inline uint32_t *copy32(uint32_t *data, unsigned size)
{
    uint32_t *retval = malloc(size * 4);
    if(retval)
        memcpy(retval, data, size * 4);
    return retval;
}

static void compile_shader_state(struct compiled_shader_state *cs, const struct etna_shader_program *rs)
{
    SET_STATE(RA_CONTROL, rs->ra_control);

    SET_STATE(PA_ATTRIBUTE_ELEMENT_COUNT, VIVS_PA_ATTRIBUTE_ELEMENT_COUNT_COUNT(rs->num_varyings));
    for(int idx=0; idx<rs->num_varyings; ++idx)
        SET_STATE(PA_SHADER_ATTRIBUTES[idx], rs->varyings[idx].pa_attributes);

    SET_STATE(VS_END_PC, rs->vs_code_size / 4);
    SET_STATE(VS_OUTPUT_COUNT, rs->num_varyings + 1); /* position + varyings */
    SET_STATE(VS_INPUT_COUNT, VIVS_VS_INPUT_COUNT_COUNT(rs->num_inputs) |
                              VIVS_VS_INPUT_COUNT_UNK8(1));
    SET_STATE(VS_TEMP_REGISTER_CONTROL,
                              VIVS_VS_TEMP_REGISTER_CONTROL_NUM_TEMPS(rs->vs_num_temps));
    /* vs outputs (varyings) */ 
    uint32_t vs_output[4] = {0};
    vs_output[0] = rs->vs_pos_out_reg;
    for(int idx=0; idx<rs->num_varyings; ++idx)
        vs_output[idx/4] |= rs->varyings[idx].vs_reg << (((idx+1)%4)*8);
    for(int idx=0; idx<4; ++idx)
        SET_STATE(VS_OUTPUT[idx], vs_output[idx]);
    
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
    SET_STATE(GL_VARYING_TOTAL_COMPONENTS, VIVS_GL_VARYING_TOTAL_COMPONENTS_NUM(total_components));
    SET_STATE(GL_VARYING_NUM_COMPONENTS, num_components);
    SET_STATE(GL_VARYING_COMPONENT_USE[0], component_use[0]);
    SET_STATE(GL_VARYING_COMPONENT_USE[1], component_use[1]);

    cs->vs_inst_mem_size = rs->vs_code_size;
    cs->vs_uniforms_size = rs->vs_uniforms_size;
    cs->ps_inst_mem_size = rs->ps_code_size;
    cs->ps_uniforms_size = rs->ps_uniforms_size;
    cs->VS_INST_MEM = copy32(rs->vs_code, rs->vs_code_size);
    cs->VS_UNIFORMS = copy32(rs->vs_uniforms, rs->vs_uniforms_size);
    cs->PS_INST_MEM = copy32(rs->ps_code, rs->ps_code_size);
    cs->PS_UNIFORMS = copy32(rs->ps_uniforms, rs->ps_uniforms_size);
}

/* test.... */
static void submit_shader_state(etna_ctx *ctx, struct compiled_shader_state *cs)
{
    etna_set_state(ctx, VIVS_RA_CONTROL, cs->RA_CONTROL);

    etna_set_state(ctx, VIVS_PA_ATTRIBUTE_ELEMENT_COUNT, cs->PA_ATTRIBUTE_ELEMENT_COUNT);
    etna_set_state_multi(ctx, VIVS_PA_SHADER_ATTRIBUTES(0), VIVS_PA_SHADER_ATTRIBUTES__LEN, 
            cs->PA_SHADER_ATTRIBUTES);
    
    etna_set_state(ctx, VIVS_VS_END_PC, cs->VS_END_PC);
    etna_set_state(ctx, VIVS_VS_OUTPUT_COUNT, cs->VS_OUTPUT_COUNT);
    etna_set_state(ctx, VIVS_VS_INPUT_COUNT, cs->VS_INPUT_COUNT);
    etna_set_state(ctx, VIVS_VS_TEMP_REGISTER_CONTROL, cs->VS_TEMP_REGISTER_CONTROL);
    etna_set_state_multi(ctx, VIVS_VS_OUTPUT(0), 4, cs->VS_OUTPUT);
    etna_set_state_multi(ctx, VIVS_VS_INPUT(0), 4, cs->VS_INPUT);
    etna_set_state(ctx, VIVS_VS_LOAD_BALANCING, cs->VS_LOAD_BALANCING); 
    etna_set_state(ctx, VIVS_VS_START_PC, cs->VS_START_PC);

    etna_set_state(ctx, VIVS_PS_END_PC, cs->PS_END_PC);
    etna_set_state(ctx, VIVS_PS_OUTPUT_REG, cs->PS_OUTPUT_REG);
    etna_set_state(ctx, VIVS_PS_INPUT_COUNT, cs->PS_INPUT_COUNT);
    etna_set_state(ctx, VIVS_PS_TEMP_REGISTER_CONTROL, cs->PS_TEMP_REGISTER_CONTROL);
    etna_set_state(ctx, VIVS_PS_CONTROL, cs->PS_CONTROL);
    etna_set_state(ctx, VIVS_PS_START_PC, cs->PS_START_PC);

    etna_set_state(ctx, VIVS_GL_VARYING_TOTAL_COMPONENTS, cs->GL_VARYING_TOTAL_COMPONENTS);
    etna_set_state(ctx, VIVS_GL_VARYING_NUM_COMPONENTS, cs->GL_VARYING_NUM_COMPONENTS);
    etna_set_state_multi(ctx, VIVS_GL_VARYING_COMPONENT_USE(0), 2, cs->GL_VARYING_COMPONENT_USE);

    etna_set_state_multi(ctx, VIVS_VS_INST_MEM(0), cs->vs_inst_mem_size, cs->VS_INST_MEM);
    etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(0), cs->vs_uniforms_size, cs->VS_UNIFORMS);
    etna_set_state_multi(ctx, VIVS_PS_INST_MEM(0), cs->ps_inst_mem_size, cs->PS_INST_MEM);
    etna_set_state_multi(ctx, VIVS_PS_UNIFORMS(0), cs->ps_uniforms_size, cs->PS_UNIFORMS);
}

/*********************************************************************/
#define VERTEX_BUFFER_SIZE 0x60000

const uint32_t vs[] = {
    0x01831009,0x00000000,0x00000000,0x203fc048,
    0x02031009,0x00000000,0x00000000,0x203fc058,
    0x07841003,0x39000800,0x00000050,0x00000000,
    0x07841002,0x39001800,0x00aa0050,0x00390048,
    0x07841002,0x39002800,0x01540050,0x00390048,
    0x07841002,0x39003800,0x01fe0050,0x00390048,
    0x03851003,0x29004800,0x000000d0,0x00000000,
    0x03851002,0x29005800,0x00aa00d0,0x00290058,
    0x03811002,0x29006800,0x015400d0,0x00290058,
    0x07851003,0x39007800,0x00000050,0x00000000,
    0x07851002,0x39008800,0x00aa0050,0x00390058,
    0x07851002,0x39009800,0x01540050,0x00390058,
    0x07801002,0x3900a800,0x01fe0050,0x00390058,
    0x0401100c,0x00000000,0x00000000,0x003fc008,
    0x03801002,0x69000800,0x01fe00c0,0x00290038,
    0x03831005,0x29000800,0x01480040,0x00000000,
    0x0383100d,0x00000000,0x00000000,0x00000038,
    0x03801003,0x29000800,0x014801c0,0x00000000,
    0x00801005,0x29001800,0x01480040,0x00000000,
    0x0380108f,0x3fc06800,0x00000050,0x203fc068,
    0x04001009,0x00000000,0x00000000,0x200000b8,
    0x02041001,0x2a804800,0x00000000,0x003fc048,
    0x02041003,0x2a804800,0x00aa05c0,0x00000002,
};
const uint32_t ps[] = { /* texture sampling */
    0x07811003,0x00000800,0x01c800d0,0x00000000,
    0x07821018,0x29002f20,0x00000000,0x00000000,
    0x07811003,0x39001800,0x01c80140,0x00000000,
};
size_t vs_size = sizeof(vs);
size_t ps_size = sizeof(ps);

const struct etna_shader_program shader = {
    .ra_control = 1,
    .num_inputs = 3,
    .inputs = {{.vs_reg=0},{.vs_reg=1},{.vs_reg=2}},
    .num_varyings = 2,
    .varyings = {
        {.num_components=4, .special=ETNA_VARYING_VSOUT, .pa_attributes=0x200, .vs_reg=0},
        {.num_components=3, .special=ETNA_VARYING_VSOUT, .pa_attributes=0x200, .vs_reg=1}
    }, 
    .vs_code_size = sizeof(vs)/4,
    .vs_code = (uint32_t*)vs,
    .vs_pos_out_reg = 4, // t4 out
    .vs_load_balancing = 0xf3f0542,
    .vs_num_temps = 6,
    .vs_uniforms_size = 12*4,
    .vs_uniforms = (uint32_t*)(const float[12*4]){
        [19] = 2.0f, /* u4.w */
        [23] = 20.0f, /* u5.w */
        [27] = 0.0f, /* u6.w */
        [45] = 0.5f, /* u11.y */
        [44] = 1.0f, /* u11.x */
    },
    .ps_code_size = sizeof(ps)/4,
    .ps_code = (uint32_t*)ps,
    .ps_color_out_reg = 1, // t1 out
    .ps_num_temps = 3,
    .ps_uniforms_size = 2*4,
    .ps_uniforms = (uint32_t*)(const float[2*4]){
        [0] = 1.0f,
        [4] = 1.0f,
        [5] = -1.0f,
        [6] = -1.0f,
        [7] = 0.0f
    },
};

int main(int argc, char **argv)
{
    int rv;
    int width = 256;
    int height = 256;
    
    fb_info fb;
    rv = fb_open(0, &fb);
    if(rv!=0)
    {
        exit(1);
    }
    width = fb.fb_var.xres;
    height = fb.fb_var.yres;

    rv = viv_open();
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    printf("Succesfully opened device\n");

    etna_ctx *ctx = 0;
    struct pipe_context *pipe = 0;
    etna_bswap_buffers *buffers = 0;
    if(etna_create(&ctx) != ETNA_OK ||
        etna_bswap_create(ctx, &buffers, (int (*)(void *, int))&fb_set_buffer, &fb) != ETNA_OK ||
        (pipe = etna_new_pipe_context(ctx)) == NULL)
    {
        printf("Unable to create etna context\n");
        exit(1);
    }

    struct pipe_resource *tex_resource = etna_pipe_create_2d(pipe, ETNA_IS_TEXTURE | ETNA_IS_CUBEMAP, FMT_X8R8G8B8, 1, 1, 0);
    
    uint32_t *tex_data[6];
    for(int layerid=0; layerid<6; ++layerid)
        tex_data[layerid] = tex_resource->levels[0].logical + tex_resource->levels[0].layer_stride * layerid;
    tex_data[0][0] = 0xffff0000;
    tex_data[1][0] = 0xff00ff00;
    tex_data[2][0] = 0xff0000ff;
    tex_data[3][0] = 0xffffff00;
    tex_data[4][0] = 0xffff00ff;
    tex_data[5][0] = 0xffffffff;

    /* resources */
    struct pipe_resource *rt_resource = etna_pipe_create_2d(pipe, ETNA_IS_RENDER_TARGET, PIPE_FORMAT_B8G8R8X8_UNORM, width, height, 0);
    struct pipe_resource *z_resource = etna_pipe_create_2d(pipe, ETNA_IS_RENDER_TARGET, PIPE_FORMAT_Z16_UNORM, width, height, 0);
    struct pipe_resource *vtx_resource = etna_pipe_create_buffer(pipe, ETNA_IS_VERTEX, VERTEX_BUFFER_SIZE);
    struct pipe_resource *idx_resource = etna_pipe_create_buffer(pipe, ETNA_IS_INDEX, VERTEX_BUFFER_SIZE);

    /* Phew, now we got all the memory we need.
     * Write interleaved attribute vertex stream.
     * Unlike the GL example we only do this once, not every time glDrawArrays is called, the same would be accomplished
     * from GL by using a vertex buffer object.
     */
    GLfloat *vVertices;
    GLfloat *vNormals;
    GLfloat *vTexCoords;
    GLushort *vIndices;
    int numVertices = 0;
    int numIndices = esGenSphere(20, 1.0f, &vVertices, &vNormals,
                                        &vTexCoords, &vIndices, &numVertices);

    float *vtx_logical = vtx_resource->levels[0].logical;
    for(int vert=0; vert<numVertices; ++vert)
    {
        int dest_idx = vert * (3 + 3 + 2);
        for(int comp=0; comp<3; ++comp)
            vtx_logical[dest_idx+comp+0] = vVertices[vert*3 + comp]; /* 0 */
        for(int comp=0; comp<3; ++comp)
            vtx_logical[dest_idx+comp+3] = vNormals[vert*3 + comp]; /* 1 */
        for(int comp=0; comp<2; ++comp)
            vtx_logical[dest_idx+comp+6] = vTexCoords[vert*2 + comp]; /* 2 */
    }
    memcpy(idx_resource->levels[0].logical, vIndices, numIndices*sizeof(GLushort));

    /* pre-compile RS states, one to clear buffer, and one for each back/front buffer */
    struct compiled_rs_state copy_to_screen[ETNA_BSWAP_NUM_BUFFERS] = {};

    for(int bi=0; bi<ETNA_BSWAP_NUM_BUFFERS; ++bi)
    {
        etna_compile_rs_state(&copy_to_screen[bi], &(struct rs_state){
                    .source_format = RS_FORMAT_X8R8G8B8,
                    .source_tiling = rt_resource->layout,
                    .source_addr = rt_resource->levels[0].address,
                    .source_stride = rt_resource->levels[0].stride,
                    .dest_format = RS_FORMAT_X8R8G8B8,
                    .dest_tiling = ETNA_LAYOUT_LINEAR,
                    .dest_addr = fb.physical[bi],
                    .dest_stride = fb.fb_fix.line_length,
                    .swap_rb = true,
                    .dither = {0xffffffff, 0xffffffff},
                    .clear_mode = VIVS_RS_CLEAR_CONTROL_MODE_DISABLED,
                    .width = width,
                    .height = height
                });
    }

    /* compile gallium3d states */
    void *blend = pipe->create_blend_state(pipe, &(struct pipe_blend_state) {
                .rt[0] = {
                    .blend_enable = 0,
                    .rgb_func = PIPE_BLEND_ADD,
                    .rgb_src_factor = PIPE_BLENDFACTOR_ONE,
                    .rgb_dst_factor = PIPE_BLENDFACTOR_ZERO,
                    .alpha_func = PIPE_BLEND_ADD,
                    .alpha_src_factor = PIPE_BLENDFACTOR_ONE,
                    .alpha_dst_factor = PIPE_BLENDFACTOR_ZERO,
                    .colormask = 0xf
                }
            });

    void *sampler = pipe->create_sampler_state(pipe, &(struct pipe_sampler_state) {
                .wrap_s = PIPE_TEX_WRAP_REPEAT,
                .wrap_t = PIPE_TEX_WRAP_REPEAT,
                .wrap_r = PIPE_TEX_WRAP_REPEAT,
                .min_img_filter = PIPE_TEX_FILTER_NEAREST,
                .min_mip_filter = PIPE_TEX_MIPFILTER_NONE,
                .mag_img_filter = PIPE_TEX_FILTER_NEAREST,
                .normalized_coords = 1,
                .lod_bias = 0.0f,
                .min_lod = 0.0f, .max_lod=1000.0f
            });

    void *rasterizer = pipe->create_rasterizer_state(pipe, &(struct pipe_rasterizer_state){
                .flatshade = 0,
                .light_twoside = 1,
                .clamp_vertex_color = 1,
                .clamp_fragment_color = 1,
                .front_ccw = 1,
                .cull_face = PIPE_FACE_BACK,      /**< PIPE_FACE_x */
                .fill_front = PIPE_POLYGON_MODE_FILL,     /**< PIPE_POLYGON_MODE_x */
                .fill_back = PIPE_POLYGON_MODE_FILL,      /**< PIPE_POLYGON_MODE_x */
                .offset_point = 0,
                .offset_line = 0,
                .offset_tri = 0,
                .scissor = 0,
                .poly_smooth = 1,
                .poly_stipple_enable = 0,
                .point_smooth = 0,
                .sprite_coord_mode = 0,     /**< PIPE_SPRITE_COORD_ */
                .point_quad_rasterization = 0, /** points rasterized as quads or points */
                .point_size_per_vertex = 0, /**< size computed in vertex shader */
                .multisample = 0,
                .line_smooth = 0,
                .line_stipple_enable = 0,
                .line_last_pixel = 0,
                .flatshade_first = 0,
                .gl_rasterization_rules = 1,
                .rasterizer_discard = 0,
                .depth_clip = 0,
                .clip_plane_enable = 0,
                .line_stipple_factor = 0,
                .line_stipple_pattern = 0,
                .sprite_coord_enable = 0,
                .line_width = 1.0f,
                .point_size = 1.0f,
                .offset_units = 0.0f,
                .offset_scale = 0.0f,
                .offset_clamp = 0.0f
            });

    void *dsa = pipe->create_depth_stencil_alpha_state(pipe, &(struct pipe_depth_stencil_alpha_state){
            .depth = {
                .enabled = 0,
                .writemask = 0,
                .func = PIPE_FUNC_LESS /* GL default */
            },
            .stencil[0] = {
                .enabled = 0
            },
            .stencil[1] = {
                .enabled = 0
            },
            .alpha = {
                .enabled = 0
            }
            });

    struct pipe_vertex_element pipe_vertex_elements[] = {
        { /* positions */
            .src_offset = 0,
            .instance_divisor = 0,
            .vertex_buffer_index = 0,
            .src_format = PIPE_FORMAT_R32G32B32_FLOAT 
        },
        { /* normals */
            .src_offset = 0xc,
            .instance_divisor = 0,
            .vertex_buffer_index = 0,
            .src_format = PIPE_FORMAT_R32G32B32_FLOAT 
        },
        { /* texture coord */
            .src_offset = 0x18,
            .instance_divisor = 0,
            .vertex_buffer_index = 0,
            .src_format = PIPE_FORMAT_R32G32_FLOAT
        }
    };
    void *vertex_elements = pipe->create_vertex_elements_state(pipe, 
            sizeof(pipe_vertex_elements)/sizeof(pipe_vertex_elements[0]), pipe_vertex_elements);
    struct pipe_sampler_view *sampler_view = pipe->create_sampler_view(pipe, tex_resource, &(struct pipe_sampler_view){
            .format = tex_resource->format,
            .u.tex.first_level = 0,
            .u.tex.last_level = 0,
            .swizzle_r = PIPE_SWIZZLE_RED,
            .swizzle_g = PIPE_SWIZZLE_GREEN,
            .swizzle_b = PIPE_SWIZZLE_BLUE,
            .swizzle_a = PIPE_SWIZZLE_ALPHA,
            });
    struct pipe_surface *cbuf = pipe->create_surface(pipe, rt_resource, &(struct pipe_surface){
        .texture = rt_resource,
        .format = rt_resource->format,
        .u.tex.level = 0
        });
    struct pipe_surface *zsbuf = pipe->create_surface(pipe, z_resource, &(struct pipe_surface){
        .texture = z_resource,
        .format = z_resource->format,
        .u.tex.level = 0
        });
    
    /* bind */
    pipe->bind_blend_state(pipe, blend);
    pipe->bind_fragment_sampler_states(pipe, 1, &sampler);
    pipe->bind_rasterizer_state(pipe, rasterizer);
    pipe->bind_depth_stencil_alpha_state(pipe, dsa);
    pipe->bind_vertex_elements_state(pipe, vertex_elements);

    pipe->set_blend_color(pipe, &(struct pipe_blend_color){
            .color = {0.0f,0.0f,0.0f,1.0f}
            });
    pipe->set_stencil_ref(pipe, &(struct pipe_stencil_ref){
            .ref_value[0] = 0xff,
            .ref_value[1] = 0xff
            });
    pipe->set_sample_mask(pipe, 0xf);
    pipe->set_framebuffer_state(pipe, &(struct pipe_framebuffer_state){
            .width = width,
            .height = height,
            .nr_cbufs = 1,
            .cbufs[0] = cbuf,
            .zsbuf = zsbuf
            });
    pipe->set_scissor_state(pipe, &(struct pipe_scissor_state){
            .minx = 0,
            .miny = 0,
            .maxx = 65535,
            .maxy = 65535
            });
    pipe->set_viewport_state(pipe, &(struct pipe_viewport_state){
            .scale = {width/2.0f, height/2.0f, 0.5f, 1.0f},
            .translate = {width/2.0f, height/2.0f, 0.5f, 1.0f}
            });
    pipe->set_fragment_sampler_views(pipe, 1, &sampler_view);
    pipe->set_vertex_buffers(pipe, 0, 1, &(struct pipe_vertex_buffer){
            .stride = (3 + 3 + 2)*4,
            .buffer_offset = 0,
            .buffer = vtx_resource,
            .user_buffer = 0
            });
    pipe->set_index_buffer(pipe, &(struct pipe_index_buffer){
            .index_size = 2,
            .offset = 0,
            .buffer = idx_resource,
            .user_buffer = 0
            }); /* non-indexed rendering */

    struct compiled_shader_state shader_state = {};
    compile_shader_state(&shader_state, &shader);
    submit_shader_state(ctx, &shader_state);
     
    for(int frame=0; frame<1000; ++frame)
    {
        if(frame%50 == 0)
            printf("*** FRAME %i ****\n", frame);
        /*   Compute transform matrices in the same way as cube egl demo */ 
        ESMatrix modelview, projection, modelviewprojection;
        ESMatrix inverse, normal; 
        esMatrixLoadIdentity(&modelview);
        esTranslate(&modelview, 0.0f, 0.0f, -8.0f);
        esRotate(&modelview, 45.0f, 1.0f, 0.0f, 0.0f);
        esRotate(&modelview, 45.0f, 0.0f, 1.0f, 0.0f);
        esRotate(&modelview, frame*0.5f, 0.0f, 0.0f, 1.0f);
        GLfloat aspect = (GLfloat)(height) / (GLfloat)(width);
        esMatrixLoadIdentity(&projection);
        esFrustum(&projection, -2.8f, +2.8f, -2.8f * aspect, +2.8f * aspect, 6.0f, 10.0f);
        esMatrixLoadIdentity(&modelviewprojection);
        esMatrixMultiply(&modelviewprojection, &modelview, &projection);
        esMatrixInverse3x3(&inverse, &modelview);
        esMatrixTranspose(&normal, &inverse);
       
        /* Clear render target */
        pipe->clear(pipe, PIPE_CLEAR_COLOR | PIPE_CLEAR_DEPTHSTENCIL, &(const union pipe_color_union) {
                .f = {0.2, 0.2, 0.2, 1.0}
                }, 1.0, 0xff);
        
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(0), 16, (uint32_t*)&modelviewprojection.m[0][0]);
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(16), 3, (uint32_t*)&normal.m[0][0]); /* u4.xyz */
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(20), 3, (uint32_t*)&normal.m[1][0]); /* u5.xyz */
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(24), 3, (uint32_t*)&normal.m[2][0]); /* u6.xyz */
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(28), 16, (uint32_t*)&modelview.m[0][0]);

        pipe->draw_vbo(pipe, &(struct pipe_draw_info){
                .indexed = 1,
                .mode = PIPE_PRIM_TRIANGLES,
                .start = 0,
                .count = numIndices
                });

#if 0
        etna_dump_cmd_buffer(ctx);
        exit(0);
#endif    
        /* copy to screen */
        etna_bswap_wait_available(buffers);
        /*  this flush is really needed, otherwise some quads will have pieces undrawn */
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        /*  assumes TS is still set up correctly */
        etna_submit_rs_state(ctx, &copy_to_screen[buffers->backbuffer]);

        etna_bswap_queue_swap(buffers);
    }
#ifdef DUMP
    bmp_dump32(fb.logical[1-backbuffer], width, height, false, "/mnt/sdcard/fb.bmp");
    printf("Dump complete\n");
#endif
    etna_bswap_free(buffers);
    etna_free(ctx);
    viv_close();
    return 0;
}
