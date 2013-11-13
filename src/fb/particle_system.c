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
/* Vertex-shader based particle system 
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
#include "util/u_inlines.h"
#include "write_bmp.h"
#include "state_tracker/graw.h"
#include "fbdemos.h"

#include "esTransform.h"
#include "esUtil.h"
#include "dds.h"

/*********************************************************************/
#define VERTEX_BUFFER_SIZE 0x60000
#define NUM_PARTICLES	1000
#define PARTICLE_SIZE   7

static const char particle_system_vert[] = 
"VERT\n"
"DCL IN[0]\n"
"DCL IN[1]\n"
"DCL IN[2]\n"
"DCL OUT[0], POSITION\n"
"DCL OUT[1], PSIZE\n"
"DCL OUT[2], GENERIC[0]\n"
"DCL CONST[0..1]\n"
"DCL TEMP[0..2], LOCAL\n"
"IMM[0] FLT32 {    1.0000, -1000.0000,     0.0000,    40.0000}\n"
"  0: SGE TEMP[0].x, IN[0].xxxx, CONST[1].xxxx\n"
"  1: IF TEMP[0].xxxx :0\n"
"  2:   MAD TEMP[0].xyz, CONST[1].xxxx, IN[2].xyzz, IN[1].xyzz\n"
"  3:   ADD TEMP[0].xyz, TEMP[0].xyzz, CONST[0].xyzz\n"
"  4:   MOV TEMP[0].w, IMM[0].xxxx\n"
"  5: ELSE :0\n"
"  6:   MOV TEMP[0], IMM[0].yyzz\n"
"  7: ENDIF\n"
"  8: RCP TEMP[1].x, IN[0].xxxx\n"
"  9: MUL TEMP[1].x, CONST[1].xxxx, TEMP[1].xxxx\n"
" 10: ADD_SAT TEMP[1].x, IMM[0].xxxx, -TEMP[1].xxxx\n"
" 11: MUL TEMP[2].x, TEMP[1].xxxx, TEMP[1].xxxx\n"
" 12: MUL TEMP[2].x, TEMP[2].xxxx, IMM[0].wwww\n"
" 13: MOV TEMP[1].x, TEMP[1].xxxx\n"
" 14: MOV OUT[2], TEMP[1]\n"
" 15: MOV OUT[1], TEMP[2].xxxx\n"
" 16: MOV OUT[0], TEMP[0]\n"
" 17: END\n";

static const char particle_system_frag[] = 
"FRAG\n"
"PROPERTY FS_COLOR0_WRITES_ALL_CBUFS 1\n"
"DCL IN[0], PCOORD, LINEAR\n"
"DCL IN[1], GENERIC[0], PERSPECTIVE\n"
"DCL OUT[0], COLOR\n"
"DCL SAMP[0]\n"
"DCL CONST[1]\n"
"DCL TEMP[0..1], LOCAL\n"
"  0: TEX TEMP[0], IN[0].xyyy, SAMP[0], 2D\n"
"  1: MUL TEMP[0], CONST[1], TEMP[0]\n"
"  2: MOV TEMP[1].xyz, TEMP[0].xyzx\n"
"  3: MUL TEMP[0].x, TEMP[0].wwww, IN[1].xxxx\n"
"  4: MOV TEMP[1].w, TEMP[0].xxxx\n"
"  5: MOV OUT[0], TEMP[1]\n"
"  6: END\n";

int main(int argc, char **argv)
{
    struct fbdemos_scaffold *fbs = 0;
    fbdemo_init(&fbs);
    int width = fbs->width;
    int height = fbs->height;
    struct pipe_context *pipe = fbs->pipe;

    /* texture */
    int tex_base_width = 0;
    int tex_base_height = 0;
    if(argc<2)
    {
        printf("Pass path to smoke.tga on command line\n");
        exit(1);
    }
    uint8_t *tex_buffer = (uint8_t*)esLoadTGA(argv[1], &tex_base_width, &tex_base_height );
    if(!tex_buffer)
    {
        printf("Could not load texture\n");
        exit(1);
    }
    struct pipe_resource *tex_resource = fbdemo_create_2d(fbs->screen, PIPE_BIND_SAMPLER_VIEW, PIPE_FORMAT_B8G8R8X8_UNORM, 
            tex_base_width, tex_base_height, 0);
    printf("Uploading texture (%ix%i)\n", tex_base_width, tex_base_height);
    uint32_t *temp = malloc(tex_base_width * tex_base_height * 4);
    etna_convert_r8g8b8_to_b8g8r8x8(temp, tex_buffer, tex_base_width * tex_base_height);
    etna_pipe_inline_write(pipe, tex_resource, 0, 0, temp, tex_base_width * tex_base_height * 4);
    free(temp);

    /* render target resources and surfaces */
    struct pipe_resource *rt_resource = fbdemo_create_2d(fbs->screen, PIPE_BIND_RENDER_TARGET, PIPE_FORMAT_B8G8R8X8_UNORM, width, height, 0);
    struct pipe_resource *z_resource = fbdemo_create_2d(fbs->screen, PIPE_BIND_RENDER_TARGET, PIPE_FORMAT_Z16_UNORM, width, height, 0);

    /* bind render target to framebuffer */
    etna_fb_bind_resource(fbs, rt_resource);
   
    /* surfaces */
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

    /* compile gallium3d states */
    void *blend = pipe->create_blend_state(pipe, &(struct pipe_blend_state) {
                .rt[0] = {
                    .blend_enable = 1,
                    .rgb_func = PIPE_BLEND_ADD,
                    .rgb_src_factor = PIPE_BLENDFACTOR_SRC_ALPHA,
                    .rgb_dst_factor = PIPE_BLENDFACTOR_ONE,
                    .alpha_func = PIPE_BLEND_ADD,
                    .alpha_src_factor = PIPE_BLENDFACTOR_SRC_ALPHA,
                    .alpha_dst_factor = PIPE_BLENDFACTOR_ONE,
                    .colormask = 0xf
                }
            });

    void *sampler = pipe->create_sampler_state(pipe, &(struct pipe_sampler_state) {
                .wrap_s = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
                .wrap_t = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
                .wrap_r = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
                .min_img_filter = PIPE_TEX_FILTER_LINEAR,
                .min_mip_filter = PIPE_TEX_MIPFILTER_NONE,
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
                .point_quad_rasterization = 1, /** points rasterized as quads or points */
                .point_size_per_vertex = 1, /**< size computed in vertex shader */
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

    /* particles */
    struct pipe_resource *vtx_resource = pipe_buffer_create(fbs->screen, PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_IMMUTABLE, VERTEX_BUFFER_SIZE);
    struct pipe_vertex_buffer vertex_buffer_desc = {
            .stride = PARTICLE_SIZE*4,
            .buffer_offset = 0,
            .buffer = vtx_resource,
            .user_buffer = 0
            };
    struct pipe_vertex_element pipe_vertex_elements[] = {
        { /* positions */
            .src_offset = 0x0,
            .instance_divisor = 0,
            .vertex_buffer_index = 0,
            .src_format = PIPE_FORMAT_R32_FLOAT 
        },
        { /* normals */
            .src_offset = 0x4,
            .instance_divisor = 0,
            .vertex_buffer_index = 0,
            .src_format = PIPE_FORMAT_R32G32B32_FLOAT 
        },
        { /* texture coord */
            .src_offset = 0x10,
            .instance_divisor = 0,
            .vertex_buffer_index = 0,
            .src_format = PIPE_FORMAT_R32G32B32_FLOAT
        }
    };
    void *vertex_elements = pipe->create_vertex_elements_state(pipe, 
            sizeof(pipe_vertex_elements)/sizeof(pipe_vertex_elements[0]), pipe_vertex_elements);

    /* texture and render target surfaces */
    struct pipe_sampler_view *sampler_view = pipe->create_sampler_view(pipe, tex_resource, &(struct pipe_sampler_view){
            .format = tex_resource->format,
            .u.tex.first_level = 0,
            .u.tex.last_level = 0,
            .swizzle_r = PIPE_SWIZZLE_RED,
            .swizzle_g = PIPE_SWIZZLE_GREEN,
            .swizzle_b = PIPE_SWIZZLE_BLUE,
            .swizzle_a = PIPE_SWIZZLE_ALPHA,
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
    pipe->set_fragment_sampler_views(pipe, 1, &sampler_view);
    pipe->set_vertex_buffers(pipe, 0, 1, &vertex_buffer_desc);
    pipe->set_index_buffer(pipe, NULL);
    
    void *vtx_shader = graw_parse_vertex_shader(pipe, particle_system_vert);
    void *frag_shader = graw_parse_fragment_shader(pipe, particle_system_frag);
    pipe->bind_vs_state(pipe, vtx_shader);
    pipe->bind_fs_state(pipe, frag_shader);
    
    /* Fill in particle data array */
    struct pipe_transfer *vtx_transfer = 0;
    float *vtx_logical = pipe_buffer_map(pipe, vtx_resource, PIPE_TRANSFER_WRITE | PIPE_TRANSFER_UNSYNCHRONIZED, &vtx_transfer);
    srand(0);
    for(int i = 0; i < NUM_PARTICLES; i++)
    {
       float *particleData = &vtx_logical[i * PARTICLE_SIZE];
   
       // Lifetime of particle
       (*particleData++) = ( (float)(rand() % 10000) / 10000.0f );

       // End position of particle
       (*particleData++) = ( (float)(rand() % 10000) / 5000.0f ) - 1.0f;
       (*particleData++) = ( (float)(rand() % 10000) / 5000.0f ) - 1.0f;
       (*particleData++) = ( (float)(rand() % 10000) / 5000.0f ) - 1.0f;

       // Start position of particle
       (*particleData++) = ( (float)(rand() % 10000) / 40000.0f ) - 0.125f;
       (*particleData++) = ( (float)(rand() % 10000) / 40000.0f ) - 0.125f;
       (*particleData++) = ( (float)(rand() % 10000) / 40000.0f ) - 0.125f;
    }
    pipe_buffer_unmap(pipe, vtx_transfer);

    double prevTime = esNow();
    float time = 1.0f;
    for(int frame=0; frame<1000; ++frame)
    {
        float vs_const[2*4];
        float fs_const[2*4];
        if(frame%50 == 0)
            printf("*** FRAME %i ****\n", frame);
       
        /* Clear render target */
        pipe->clear(pipe, PIPE_CLEAR_COLOR | PIPE_CLEAR_DEPTHSTENCIL, &(const union pipe_color_union) {
                .f = {0.2, 0.2, 0.2, 1.0}
                }, 1.0, 0xff);
       
        double newTime = esNow();
        time += newTime - prevTime;
        prevTime = newTime;
        if ( time >= 1.0f )
        {
            float centerPos[3];
            float color[4];

            time = 0.0f;

            // Pick a new start location and color
            centerPos[0] = ( (float)(rand() % 10000) / 10000.0f ) - 0.5f;
            centerPos[1] = ( (float)(rand() % 10000) / 10000.0f ) - 0.5f;
            centerPos[2] = ( (float)(rand() % 10000) / 10000.0f ) - 0.5f;

            memcpy(&vs_const[0*4], &centerPos[0], 3*4);
          
            // Random color
            color[0] = ( (float)(rand() % 10000) / 20000.0f ) + 0.5f;
            color[1] = ( (float)(rand() % 10000) / 20000.0f ) + 0.5f;
            color[2] = ( (float)(rand() % 10000) / 20000.0f ) + 0.5f;
            color[3] = 0.5;

            memcpy(&fs_const[1*4], &color[0], 4*4);
        }
        vs_const[4] = time;
        
        pipe->set_constant_buffer(pipe, PIPE_SHADER_VERTEX, 0, &(struct pipe_constant_buffer){
                .user_buffer = vs_const,
                .buffer_size = sizeof(vs_const)
                });
        pipe->set_constant_buffer(pipe, PIPE_SHADER_FRAGMENT, 0, &(struct pipe_constant_buffer){
                .user_buffer = fs_const,
                .buffer_size = sizeof(fs_const)
                });

        pipe->draw_vbo(pipe, &(struct pipe_draw_info){
                .indexed = 0,
                .mode = PIPE_PRIM_POINTS,
                .start = 0,
                .count = NUM_PARTICLES
                });

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
