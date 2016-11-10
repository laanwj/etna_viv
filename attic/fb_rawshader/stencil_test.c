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
/* stencil_test implemented with etna_pipe 
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
#include "etna_rawshader.h"
#include "util/u_inlines.h"

#include "write_bmp.h"
#include "etna_bswap.h"
#include "fbdemos.h"

#include "esTransform.h"
#include "dds.h"

/*********************************************************************/
#define VERTEX_BUFFER_SIZE 0x60000
   
const float vVertices[] = { 
   -0.75f,  0.25f,  0.50f, // Quad #0
   -0.25f,  0.25f,  0.50f,
   -0.25f,  0.75f,  0.50f,
   -0.75f,  0.75f,  0.50f,
        0.25f,  0.25f,  0.90f, // Quad #1
            0.75f,  0.25f,  0.90f,
            0.75f,  0.75f,  0.90f,
            0.25f,  0.75f,  0.90f,
       -0.75f, -0.75f,  0.50f, // Quad #2
   -0.25f, -0.75f,  0.50f,
   -0.25f, -0.25f,  0.50f,
   -0.75f, -0.25f,  0.50f,
    0.25f, -0.75f,  0.50f, // Quad #3
    0.75f, -0.75f,  0.50f,
    0.75f, -0.25f,  0.50f,
    0.25f, -0.25f,  0.50f,
   -1.00f, -1.00f,  0.00f, // Big Quad
    1.00f, -1.00f,  0.00f,
    1.00f,  1.00f,  0.00f,
   -1.00f,  1.00f,  0.00f
};

const uint8_t indices[][6] = { 
   {  0,  1,  2,  0,  2,  3 }, // Quad #0
   {  4,  5,  6,  4,  6,  7 }, // Quad #1
   {  8,  9, 10,  8, 10, 11 }, // Quad #2
   { 12, 13, 14, 12, 14, 15 }, // Quad #3
   { 16, 17, 18, 16, 18, 19 }  // Big Quad
};

#define NumTests  4
const float  colors[NumTests][4] = { 
   { 1.0f, 0.0f, 0.0f, 1.0f },
   { 0.0f, 1.0f, 0.0f, 1.0f },
   { 0.0f, 0.0f, 1.0f, 1.0f },
   { 1.0f, 1.0f, 0.0f, 1.0f }
};
   
uint32_t  stencilValues[NumTests] = { 
  0x7, // Result of test 0
  0x0, // Result of test 1
  0x2, // Result of test 2
  0xff // Result of test 3.  We need to fill this
       //  value in a run-time
};

#define NUM_VERTICES (sizeof(vVertices) / (sizeof(float)*3))

/* stencil_test_vs.asm */
uint32_t vs[] = {
0x02001001,0x2a800800,0x00000000,0x003fc008,
0x02001003,0x2a800800,0x00000040,0x00000002,
};
/* stencil_test_ps.asm */
uint32_t ps[] = {
0x07811009,0x00000000,0x00000000,0x20390008,
};

const struct etna_shader_program shader = {
    .num_inputs = 1,
    .inputs = {{.vs_reg=0}},
    .num_varyings = 0,
    .varyings = {
    }, 
    .vs_code_size = sizeof(vs)/4,
    .vs_code = (uint32_t*)vs,
    .vs_pos_out_reg = 0,
    .vs_load_balancing = 0xf3f0582,  /* depends on number of inputs/outputs/varyings? XXX how exactly */
    .vs_num_temps = 1,
    .vs_uniforms_size = 1*4,
    .vs_uniforms = (uint32_t*)(const float[1*4]){
        [0] = 0.5f, /* u0.x */
    },
    .ps_code_size = sizeof(ps)/4,
    .ps_code = (uint32_t*)ps,
    .ps_color_out_reg = 1, // t1 = color out
    .ps_num_temps = 2,
    .ps_uniforms_size = 1*4,
    .ps_uniforms = (uint32_t*)(const float[1*4]){
        [0] = 0.0f,
        [1] = 0.0f,
        [2] = 0.0f,
        [3] = 0.0f,
    },
};

int main(int argc, char **argv)
{
    struct fbdemos_scaffold *fbs = 0;
    fbdemo_init(&fbs);
    int width = fbs->width;
    int height = fbs->height;
    struct pipe_context *pipe = fbs->pipe;

    /* resources */
    struct pipe_resource *rt_resource = fbdemo_create_2d(fbs->screen, PIPE_BIND_RENDER_TARGET, PIPE_FORMAT_B8G8R8X8_UNORM, width, height, 0);
    struct pipe_resource *z_resource = fbdemo_create_2d(fbs->screen, PIPE_BIND_RENDER_TARGET, PIPE_FORMAT_S8_UINT_Z24_UNORM, width, height, 0);
    struct pipe_resource *vtx_resource = pipe_buffer_create(fbs->screen, PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_IMMUTABLE, VERTEX_BUFFER_SIZE);
    struct pipe_resource *idx_resource = pipe_buffer_create(fbs->screen, PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_IMMUTABLE, VERTEX_BUFFER_SIZE);
    
    /* bind render target to framebuffer */
    etna_fb_bind_resource(fbs, rt_resource);

    /* vertex / index buffer setup */
    struct pipe_transfer *vtx_transfer = 0;
    float *vtx_logical = pipe_buffer_map(pipe, vtx_resource, PIPE_TRANSFER_WRITE | PIPE_TRANSFER_UNSYNCHRONIZED, &vtx_transfer);
    assert(vtx_logical);
    for(int vert=0; vert<NUM_VERTICES; ++vert)
    {
        int dest_idx = vert * 3;
        for(int comp=0; comp<3; ++comp)
            vtx_logical[dest_idx+comp+0] = vVertices[vert*3 + comp]; /* 0 */
    }
    pipe_buffer_unmap(pipe, vtx_transfer);

    struct pipe_transfer *idx_transfer = 0;
    void *idx_logical = pipe_buffer_map(pipe, idx_resource, PIPE_TRANSFER_WRITE | PIPE_TRANSFER_UNSYNCHRONIZED, &idx_transfer);
    assert(idx_logical);
    memcpy(idx_logical, indices, sizeof(indices));
    pipe_buffer_unmap(pipe, idx_transfer);

    struct pipe_vertex_buffer vertex_buf_desc = {
            .stride = (3)*4,
            .buffer_offset = 0,
            .buffer = vtx_resource,
            .user_buffer = 0
            };
    struct pipe_vertex_element pipe_vertex_elements[] = {
        { /* positions */
            .src_offset = 0,
            .instance_divisor = 0,
            .vertex_buffer_index = 0,
            .src_format = PIPE_FORMAT_R32G32B32_FLOAT 
        },
    };
    void *vertex_elements = pipe->create_vertex_elements_state(pipe, 
            sizeof(pipe_vertex_elements)/sizeof(pipe_vertex_elements[0]), pipe_vertex_elements);
    struct pipe_index_buffer index_buf_desc = {
            .index_size = 1,
            .offset = 0,
            .buffer = idx_resource,
            .user_buffer = 0
            };

    /* compile gallium3d states */
    void *blend = pipe->create_blend_state(pipe, &(struct pipe_blend_state) {
                .rt[0] = {
                    .blend_enable = 1,
                    .rgb_func = PIPE_BLEND_ADD,
                    .rgb_src_factor = PIPE_BLENDFACTOR_SRC_ALPHA,
                    .rgb_dst_factor = PIPE_BLENDFACTOR_INV_SRC_ALPHA,
                    .alpha_func = PIPE_BLEND_ADD,
                    .alpha_src_factor = PIPE_BLENDFACTOR_SRC_ALPHA,
                    .alpha_dst_factor = PIPE_BLENDFACTOR_INV_SRC_ALPHA,
                    .colormask = 0xf
                }
            });

    void *sampler = pipe->create_sampler_state(pipe, &(struct pipe_sampler_state) {
                .wrap_s = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
                .wrap_t = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
                .wrap_r = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
                .min_img_filter = PIPE_TEX_FILTER_LINEAR,
                .min_mip_filter = PIPE_TEX_MIPFILTER_LINEAR,
                .mag_img_filter = PIPE_TEX_FILTER_LINEAR,
                .normalized_coords = 1,
                .lod_bias = 0.0f,
                .min_lod = 0.0f, .max_lod=1000.0f
            });

    void *rasterizer = pipe->create_rasterizer_state(pipe, &(struct pipe_rasterizer_state){
                .flatshade = 0,
                .light_twoside = 1,
                .clamp_vertex_color = 1,
                .clamp_fragment_color = 1,
                .front_ccw = 0,
                .cull_face = PIPE_FACE_NONE,      /**< PIPE_FACE_x */
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
                .half_pixel_center = 1,
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
    
    pipe->bind_blend_state(pipe, blend);
    pipe->bind_fragment_sampler_states(pipe, 1, &sampler);
    pipe->bind_rasterizer_state(pipe, rasterizer);
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
    pipe->set_scissor_states(pipe, 0, 1, &(struct pipe_scissor_state){
            .minx = 0,
            .miny = 0,
            .maxx = 65535,
            .maxy = 65535
            });
    pipe->set_viewport_states(pipe, 0, 1, &(struct pipe_viewport_state){
            .scale = {width/2.0f, height/2.0f, 0.5f, 1.0f},
            .translate = {width/2.0f, height/2.0f, 0.5f, 1.0f}
            });

    pipe->set_vertex_buffers(pipe, 0, 1, &vertex_buf_desc);
    pipe->set_index_buffer(pipe, &index_buf_desc);    

    void *shader_state = etna_create_shader_state(pipe, &shader);
    etna_bind_shader_state(pipe, shader_state);
   
    /* create depth stencil alpha states for the different test phases.
     * except the stencil_ref, which is set separately 
     */
    void *dsa_test0 = pipe->create_depth_stencil_alpha_state(pipe, &(struct pipe_depth_stencil_alpha_state){
            .depth = {
                .enabled = 1,
                .writemask = 1,
                .func = PIPE_FUNC_LESS /* GL default */
            },
            .stencil[0] = { /* single-sided stencil */
                .enabled = 1,
                .func = PIPE_FUNC_LESS,
                .fail_op = PIPE_STENCIL_OP_REPLACE,
                .zfail_op = PIPE_STENCIL_OP_DECR,
                .zpass_op = PIPE_STENCIL_OP_DECR,
                .valuemask = 0x03,
                .writemask = 0xff
            },
            .stencil[1] = { .enabled = 0 },
            .alpha = { .enabled = 0 }
            });
    void *dsa_test1 = pipe->create_depth_stencil_alpha_state(pipe, &(struct pipe_depth_stencil_alpha_state){
            .depth = {
                .enabled = 1,
                .writemask = 1,
                .func = PIPE_FUNC_LESS /* GL default */
            },
            .stencil[0] = { /* single-sided stencil */
                .enabled = 1,
                .func = PIPE_FUNC_GREATER,
                .fail_op = PIPE_STENCIL_OP_KEEP,
                .zfail_op = PIPE_STENCIL_OP_DECR,
                .zpass_op = PIPE_STENCIL_OP_KEEP,
                .valuemask = 0x03,
                .writemask = 0xff
            },
            .stencil[1] = { .enabled = 0 },
            .alpha = { .enabled = 0 }
            });
    void *dsa_test2 = pipe->create_depth_stencil_alpha_state(pipe, &(struct pipe_depth_stencil_alpha_state){
            .depth = {
                .enabled = 1,
                .writemask = 1,
                .func = PIPE_FUNC_LESS /* GL default */
            },
            .stencil[0] = { /* single-sided stencil */
                .enabled = 1,
                .func = PIPE_FUNC_EQUAL,
                .fail_op = PIPE_STENCIL_OP_KEEP,
                .zfail_op = PIPE_STENCIL_OP_INCR,
                .zpass_op = PIPE_STENCIL_OP_INCR,
                .valuemask = 0x03,
                .writemask = 0xff
            },
            .stencil[1] = { .enabled = 0 },
            .alpha = { .enabled = 0 }
            });
    void *dsa_test3 = pipe->create_depth_stencil_alpha_state(pipe, &(struct pipe_depth_stencil_alpha_state){
            .depth = {
                .enabled = 1,
                .writemask = 1,
                .func = PIPE_FUNC_LESS /* GL default */
            },
            .stencil[0] = { /* single-sided stencil */
                .enabled = 1,
                .func = PIPE_FUNC_EQUAL,
                .fail_op = PIPE_STENCIL_OP_INVERT,
                .zfail_op = PIPE_STENCIL_OP_KEEP,
                .zpass_op = PIPE_STENCIL_OP_KEEP,
                .valuemask = 0x01,
                .writemask = 0xff
            },
            .stencil[1] = { .enabled = 0 },
            .alpha = { .enabled = 0 }
            });
    void *dsa_bigquad = pipe->create_depth_stencil_alpha_state(pipe, &(struct pipe_depth_stencil_alpha_state){
            .depth = {
                .enabled = 1,
                .writemask = 1,
                .func = PIPE_FUNC_LESS /* GL default */
            },
            .stencil[0] = { /* single-sided stencil */
                .enabled = 1,
                .func = PIPE_FUNC_EQUAL,
                .fail_op = PIPE_STENCIL_OP_KEEP,
                .zfail_op = PIPE_STENCIL_OP_KEEP,
                .zpass_op = PIPE_STENCIL_OP_KEEP,
                .valuemask = 0xff,
                .writemask = 0x00
            },
            .stencil[1] = { .enabled = 0 },
            .alpha = { .enabled = 0 }
            });
    
    pipe->bind_depth_stencil_alpha_state(pipe, dsa_test0);

    for(int frame=0; frame<1000; ++frame)
    {
        if(frame%50 == 0)
            printf("*** FRAME %i ****\n", frame);

        /* Clear render target */
        pipe->clear(pipe, PIPE_CLEAR_COLOR | PIPE_CLEAR_DEPTHSTENCIL, &(const union pipe_color_union) {
                .f = {0.2, 0.2, 0.2, 1.0}
                }, 0.75f, 0x01);

        // Test 0:
        //
        // Initialize upper-left region.  In this case, the
        //   stencil-buffer values will be replaced because the
        //   stencil test for the rendered pixels will fail the
        //   stencil test, which is
        //
        //        ref   mask   stencil  mask
        //      ( 0x7 & 0x3 ) < ( 0x1 & 0x3 )
        //
        //   The value in the stencil buffer for these pixels will
        //   be 0x7.
        //
        pipe->bind_depth_stencil_alpha_state(pipe, dsa_test0);
        pipe->set_stencil_ref(pipe, &(struct pipe_stencil_ref){
                .ref_value[0] = 0x07,
                .ref_value[1] = 0x07});
        pipe->draw_vbo(pipe, &(struct pipe_draw_info){
                .indexed = 1,
                .mode = PIPE_PRIM_TRIANGLES,
                .start = 0*6,
                .count = 6 }); 

        // Test 1:
        //
        // Initialize the upper-right region.  Here, we'll decrement
        //   the stencil-buffer values where the stencil test passes
        //   but the depth test fails.  The stencil test is
        //
        //        ref  mask    stencil  mask
        //      ( 0x3 & 0x3 ) > ( 0x1 & 0x3 )
        //
        //    but where the geometry fails the depth test.  The
        //    stencil values for these pixels will be 0x0.
        //
        pipe->bind_depth_stencil_alpha_state(pipe, dsa_test1);
        pipe->set_stencil_ref(pipe, &(struct pipe_stencil_ref){
                .ref_value[0] = 0x03,
                .ref_value[1] = 0x03});
        pipe->draw_vbo(pipe, &(struct pipe_draw_info){
                .indexed = 1,
                .mode = PIPE_PRIM_TRIANGLES,
                .start = 1*6,
                .count = 6 });

        // Test 2:
        //
        // Initialize the lower-left region.  Here we'll increment 
        //   (with saturation) the stencil value where both the
        //   stencil and depth tests pass.  The stencil test for
        //   these pixels will be
        //
        //        ref  mask     stencil  mask
        //      ( 0x1 & 0x3 ) == ( 0x1 & 0x3 )
        //
        //   The stencil values for these pixels will be 0x2.
        //
        pipe->bind_depth_stencil_alpha_state(pipe, dsa_test2);
        pipe->set_stencil_ref(pipe, &(struct pipe_stencil_ref){
                .ref_value[0] = 0x01,
                .ref_value[1] = 0x01});
        pipe->draw_vbo(pipe, &(struct pipe_draw_info){
                .indexed = 1,
                .mode = PIPE_PRIM_TRIANGLES,
                .start = 2*6,
                .count = 6 });

        // Test 3:
        //
        // Finally, initialize the lower-right region.  We'll invert
        //   the stencil value where the stencil tests fails.  The
        //   stencil test for these pixels will be
        //
        //        ref   mask    stencil  mask
        //      ( 0x2 & 0x1 ) == ( 0x1 & 0x1 )
        //
        //   The stencil value here will be set to ~((2^s-1) & 0x1),
        //   (with the 0x1 being from the stencil clear value),
        //   where 's' is the number of bits in the stencil buffer
        //
        pipe->bind_depth_stencil_alpha_state(pipe, dsa_test3);
        pipe->set_stencil_ref(pipe, &(struct pipe_stencil_ref){
                .ref_value[0] = 0x02,
                .ref_value[1] = 0x02});
        pipe->draw_vbo(pipe, &(struct pipe_draw_info){
                .indexed = 1,
                .mode = PIPE_PRIM_TRIANGLES,
                .start = 3*6,
                .count = 6 });

        stencilValues[3] = ~(((1 << 8) - 1) & 0x1) & 0xff;

        /* Bind depth stencil state for big quad */
        pipe->bind_depth_stencil_alpha_state(pipe, dsa_bigquad);
        for(int idx=0; idx<NumTests; ++idx)
        {
            /* material color */
            etna_set_uniforms(pipe, PIPE_SHADER_FRAGMENT, 0, 4, 
                    (uint32_t*)colors[idx]);
            pipe->set_stencil_ref(pipe, &(struct pipe_stencil_ref){
                    .ref_value[0] = stencilValues[idx],
                    .ref_value[1] = stencilValues[idx]});
            pipe->draw_vbo(pipe, &(struct pipe_draw_info){
                    .indexed = 1,
                    .mode = PIPE_PRIM_TRIANGLES,
                    .start = 4*6,
                    .count = 6 });
        }
#if 0
        etna_dump_cmd_buffer(ctx);
        exit(0);
#endif    
        etna_swap_buffers(fbs->buffers);
    }
#ifdef DUMP
    bmp_dump32(etna_bo_map(fbs->fb.buffer[1-fbs->buffers->backbuffer]), width, height, false, "/mnt/sdcard/fb.bmp");
    printf("Dump complete\n");
#endif
    fbdemo_free(fbs);
    return 0;
}

