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
/* Animated rotating "weighted companion cube", using array or indexed rendering
 * Exercised in this demo:
 * - Array and indexed rendering of arbitrary mesh
 * - Video memory allocation
 * - Setting up render state
 * - Depth buffer
 * - Vertex / fragment shader
 * - Texturing
 * - Double-buffered rendering to framebuffer
 * - Mipmapping
 */
#define INDEXED

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
#include "dds.h"
#include "companion.h"

/*********************************************************************/
#define INDEXED /* Used indexed rendering */
#define INDEX_BUFFER_SIZE 0x8000
#define VERTEX_BUFFER_SIZE 0x60000

static const char cube_companion_vert[] = 
"VERT\n"
"DCL IN[0]\n"
"DCL IN[1]\n"
"DCL IN[2]\n"
"DCL OUT[0], POSITION\n"
"DCL OUT[1], GENERIC[0]\n"
"DCL OUT[2], GENERIC[1]\n"
"DCL CONST[0..10]\n"
"DCL TEMP[0..4], LOCAL\n"
"IMM[0] FLT32 {    2.0000,    20.0000,     1.0000,     0.0000}\n"
"  0: MUL TEMP[0], CONST[3], IN[0].xxxx\n"
"  1: MAD TEMP[0], CONST[4], IN[0].yyyy, TEMP[0]\n"
"  2: MAD TEMP[0], CONST[5], IN[0].zzzz, TEMP[0]\n"
"  3: MAD TEMP[0], CONST[6], IN[0].wwww, TEMP[0]\n"
"  4: MUL TEMP[1], CONST[7], IN[0].xxxx\n"
"  5: MAD TEMP[1], CONST[8], IN[0].yyyy, TEMP[1]\n"
"  6: MAD TEMP[1], CONST[9], IN[0].zzzz, TEMP[1]\n"
"  7: MAD TEMP[1], CONST[10], IN[0].wwww, TEMP[1]\n"
"  8: RCP TEMP[2].x, TEMP[1].wwww\n"
"  9: MUL TEMP[1].xyz, TEMP[1].xyzz, TEMP[2].xxxx\n"
" 10: ADD TEMP[1].xyz, IMM[0].xxyy, -TEMP[1].xyzz\n"
" 11: MOV TEMP[2].w, IMM[0].zzzz\n"
" 12: MUL TEMP[3].xyz, CONST[0].xyzz, IN[1].xxxx\n"
" 13: MAD TEMP[3].xyz, CONST[1].xyzz, IN[1].yyyy, TEMP[3].xyzz\n"
" 14: MAD TEMP[3].xyz, CONST[2].xyzz, IN[1].zzzz, TEMP[3].xyzz\n"
" 15: DP3 TEMP[4].x, TEMP[1].xyzz, TEMP[1].xyzz\n"
" 16: RSQ TEMP[4].x, TEMP[4].xxxx\n"
" 17: MUL TEMP[1].xyz, TEMP[1].xyzz, TEMP[4].xxxx\n"
" 18: DP3 TEMP[1].x, TEMP[3].xyzz, TEMP[1].xyzz\n"
" 19: MAX TEMP[1].x, IMM[0].wwww, TEMP[1].xxxx\n"
" 20: MOV TEMP[2].xyz, TEMP[1].xxxx\n"
" 21: MOV TEMP[1].xy, IN[2].xyxx\n"
" 22: MOV OUT[1], TEMP[2]\n"
" 23: MOV OUT[0], TEMP[0]\n"
" 24: MOV OUT[2], TEMP[1]\n"
" 25: END\n";

static const char cube_companion_frag[] = 
"FRAG\n"
"PROPERTY FS_COLOR0_WRITES_ALL_CBUFS 1\n"
"DCL IN[0], GENERIC[0], PERSPECTIVE\n"
"DCL IN[1], GENERIC[1], PERSPECTIVE\n"
"DCL OUT[0], COLOR\n"
"DCL SAMP[0]\n"
"DCL TEMP[0..1], LOCAL\n"
"  1: TEX TEMP[1], IN[1].xyyy, SAMP[0], 2D\n"
"  2: MUL TEMP[0], IN[0], TEMP[1]\n"
"  3: MOV OUT[0], TEMP[0]\n"
"  4: END\n";

int main(int argc, char **argv)
{
    struct fbdemos_scaffold *fbs = 0;
    fbdemo_init(&fbs);
    int width = fbs->width;
    int height = fbs->height;
    struct pipe_context *pipe = fbs->pipe;

    /* Convert and upload embedded texture */
    struct pipe_resource *tex_resource = fbdemo_create_2d(fbs->screen, PIPE_BIND_SAMPLER_VIEW, PIPE_FORMAT_B8G8R8X8_UNORM, 
            COMPANION_TEXTURE_WIDTH, COMPANION_TEXTURE_HEIGHT, 0);
    void *temp = malloc(COMPANION_TEXTURE_WIDTH * COMPANION_TEXTURE_HEIGHT * 4); 
    etna_convert_r8g8b8_to_b8g8r8x8(temp, (const uint8_t*)companion_texture, COMPANION_TEXTURE_WIDTH * COMPANION_TEXTURE_HEIGHT);
    etna_pipe_inline_write(pipe, tex_resource, 0, 0, temp, COMPANION_TEXTURE_WIDTH * COMPANION_TEXTURE_HEIGHT * 4);
    free(temp);

    /* resources */
    struct pipe_resource *rt_resource = fbdemo_create_2d(fbs->screen, PIPE_BIND_RENDER_TARGET, PIPE_FORMAT_B8G8R8X8_UNORM, width, height, 0);
    struct pipe_resource *z_resource = fbdemo_create_2d(fbs->screen, PIPE_BIND_RENDER_TARGET, PIPE_FORMAT_Z16_UNORM, width, height, 0);

    /* bind render target to framebuffer */
    etna_fb_bind_resource(fbs, rt_resource);

    /* geometry */
    struct pipe_resource *vtx_resource = pipe_buffer_create(fbs->screen, PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_IMMUTABLE, VERTEX_BUFFER_SIZE);
    struct pipe_resource *idx_resource = pipe_buffer_create(fbs->screen, PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_IMMUTABLE, INDEX_BUFFER_SIZE);

    struct pipe_transfer *vtx_transfer = 0;
    float *vtx_logical = pipe_buffer_map(pipe, vtx_resource, PIPE_TRANSFER_WRITE | PIPE_TRANSFER_UNSYNCHRONIZED, &vtx_transfer);
    assert(vtx_logical);
    struct pipe_transfer *idx_transfer = 0;
    float *idx_logical = pipe_buffer_map(pipe, idx_resource, PIPE_TRANSFER_WRITE | PIPE_TRANSFER_UNSYNCHRONIZED, &idx_transfer);
    assert(idx_logical);
#ifndef INDEXED
    printf("Interleaving vertices...\n");
    float *vertices_array = companion_vertices_array();
    float *texture_coordinates_array =
            companion_texture_coordinates_array();
    float *normals_array = companion_normals_array();
    assert(COMPANION_ARRAY_COUNT*(3+3+2)*sizeof(float) < VERTEX_BUFFER_SIZE);
    for(int vert=0; vert<COMPANION_ARRAY_COUNT; ++vert)
    {
        int dest_idx = vert * (3 + 3 + 2);
        for(int comp=0; comp<3; ++comp)
            ((float*)vtx_logical)[dest_idx+comp+0] = vertices_array[vert*3 + comp]; /* 0 */
        for(int comp=0; comp<3; ++comp)
            ((float*)vtx_logical)[dest_idx+comp+3] = normals_array[vert*3 + comp]; /* 1 */
        for(int comp=0; comp<2; ++comp)
            ((float*)vtx_logical)[dest_idx+comp+6] = texture_coordinates_array[vert*2 + comp]; /* 2 */
    }
#else
    printf("Interleaving vertices and copying index buffer...\n");
    assert(COMPANION_VERTEX_COUNT*(3+3+2)*sizeof(float) < VERTEX_BUFFER_SIZE);
    for(int vert=0; vert<COMPANION_VERTEX_COUNT; ++vert)
    {
        int dest_idx = vert * (3 + 3 + 2);
        for(int comp=0; comp<3; ++comp)
            ((float*)vtx_logical)[dest_idx+comp+0] = companion_vertices[vert][comp]; /* 0 */
        for(int comp=0; comp<3; ++comp)
            ((float*)vtx_logical)[dest_idx+comp+3] = companion_normals[vert][comp]; /* 1 */
        for(int comp=0; comp<2; ++comp)
            ((float*)vtx_logical)[dest_idx+comp+6] = companion_texture_coordinates[vert][comp]; /* 2 */
    }
    assert(COMPANION_TRIANGLE_COUNT*3*sizeof(unsigned short) < INDEX_BUFFER_SIZE);
    memcpy(idx_logical, &companion_triangles[0][0], COMPANION_TRIANGLE_COUNT*3*sizeof(unsigned short));
#endif
    pipe_buffer_unmap(pipe, vtx_transfer);
    pipe_buffer_unmap(pipe, idx_transfer);

    struct pipe_vertex_buffer vertex_buffer_desc = {
            .stride = (3 + 3 + 2)*4,
            .buffer_offset = 0,
            .buffer = vtx_resource,
            .user_buffer = 0
            };
    struct pipe_index_buffer index_buffer_desc = {
            .index_size = sizeof(unsigned short),
            .offset = 0,
            .buffer = idx_resource,
            .user_buffer = 0
            };
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

    /* compile other gallium3d states */
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
                .enabled = 1,
                .writemask = 1,
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
    pipe->set_index_buffer(pipe, &index_buffer_desc);
    
    void *vtx_shader = graw_parse_vertex_shader(pipe, cube_companion_vert);
    void *frag_shader = graw_parse_fragment_shader(pipe, cube_companion_frag);
    pipe->bind_vs_state(pipe, vtx_shader);
    pipe->bind_fs_state(pipe, frag_shader);

    for(int frame=0; frame<1000; ++frame)
    {
        float vs_const[11*4];
        if(frame%50 == 0)
            printf("*** FRAME %i ****\n", frame);
        /*   Compute transform matrices in the same way as cube egl demo */ 
        ESMatrix modelview, projection, modelviewprojection;
        ESMatrix inverse, normal; 
        esMatrixLoadIdentity(&modelview);
        esTranslate(&modelview, 0.0f, 0.0f, -9.0f);
        esRotate(&modelview, 45.0f, 1.0f, 0.0f, 0.0f);
        esRotate(&modelview, 45.0f, 0.0f, 1.0f, 0.0f);
        esRotate(&modelview, frame*0.5f, 0.0f, 0.0f, 1.0f);
	esScale(&modelview, 0.475f, 0.475f, 0.475f);
        float aspect = (float)(height) / (float)(width);
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
       
        memcpy(&vs_const[0*4], &normal.m[0][0], 3*4); /* CONST[0] */
        memcpy(&vs_const[1*4], &normal.m[1][0], 3*4); /* CONST[1] */
        memcpy(&vs_const[2*4], &normal.m[2][0], 3*4); /* CONST[2] */
        memcpy(&vs_const[3*4], &modelviewprojection.m[0][0], 16*4); /* CONST[3..6] */
        memcpy(&vs_const[7*4], &modelview.m[0][0], 16*4); /* CONST[7..10] */
        pipe->set_constant_buffer(pipe, PIPE_SHADER_VERTEX, 0, &(struct pipe_constant_buffer){
                .user_buffer = vs_const,
                .buffer_size = sizeof(vs_const)
                });

        pipe->draw_vbo(pipe, &(struct pipe_draw_info){
#ifdef INDEXED
                .indexed = 1,
#else
                .indexed = 0,
#endif
                .mode = PIPE_PRIM_TRIANGLES,
                .start = 0,
                .count = COMPANION_TRIANGLE_COUNT * 3
                });

        etna_swap_buffers(fbs->buffers);
    }
#ifdef DUMP
    bmp_dump32(etna_bo_map(fbs->fb.buffer[1-fbs->buffers->backbuffer]), width, height, false, "/mnt/sdcard/fb.bmp");
    printf("Dump complete\n");
#endif
    fbdemo_free(fbs);
    return 0;
}
