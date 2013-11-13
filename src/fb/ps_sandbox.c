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
#include "util/u_inlines.h"
#include "write_bmp.h"
#include "state_tracker/graw.h"
#include "fbdemos.h"

#include "esTransform.h"
#include "dds.h"

/*
Usage: ps_sandbox vs.txt fs.txt

Example vs.txt:

    VERT
    DCL IN[0]
    DCL IN[1]
    DCL OUT[0], POSITION
    DCL OUT[1], GENERIC[0]
      0: MOV OUT[0], IN[0]
      1: MOV OUT[1], IN[1]
      2: END

Example fs.txt:

    FRAG
    PROPERTY FS_COLOR0_WRITES_ALL_CBUFS 1
    DCL IN[0], GENERIC[0]
    DCL OUT[0], COLOR
    DCL CONST[0]
    DCL TEMP[0], LOCAL
    IMM[0] FLT32 { 1.0, 0.0, 0.0, 1.0 }
      0: MOV TEMP[0].xy, IN[0].xyzw
      1: MOV TEMP[0].zw, IMM[0].xyzw
      2: MOV OUT[0], TEMP[0]
      3: END

*/

/*********************************************************************/
   
const float vVertices[] = { 
 -1.0f, -1.0f, 0.0f, 1.0f, 
  0.0f,  0.0f, 0.0f, 0.0f, 

  1.0f, -1.0f, 0.0f, 1.0f, 
  1.0f,  0.0f, 0.0f, 0.0f, 

  1.0f,  1.0f, 0.0f, 1.0f, 
  1.0f,  1.0f, 0.0f, 0.0f, 
 
 -1.0f,  1.0f, 0.0f, 1.0f, 
  0.0f,  1.0f, 0.0f, 0.0f
};

#define VERTEX_STRIDE (8)
#define NUM_VERTICES (sizeof(vVertices) / (sizeof(float)*VERTEX_STRIDE))

static char* readfile(const char* path, unsigned* size)
{
    FILE* fp = fopen(path, "rb");
    if (!fp) return NULL;

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return NULL;
    }
    long fsize = ftell(fp);
    if ((fsize <= 0)
        || (fseek(fp, 0, SEEK_SET) != 0))
    {
        fclose(fp);
        return NULL;
    }

    char* data = (char*)malloc(fsize + 1);
    if (!data)
    {
        fclose(fp);
        return NULL;
    }
    
    if (fread(data, fsize, 1, fp) != 1)
    {
        fclose(fp);
        free(data);
        return NULL;
    }
    data[fsize] = '\0';

    fclose(fp);
    if (size)
        *size = (unsigned)fsize;
    return data;
}

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
    struct pipe_resource *vtx_resource = pipe_buffer_create(fbs->screen, PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_IMMUTABLE, sizeof(vVertices));
    
    /* bind render target to framebuffer */
    etna_fb_bind_resource(fbs, rt_resource);

    /* vertex / index buffer setup */
    struct pipe_transfer *vtx_transfer = 0;
    float *vtx_logical = pipe_buffer_map(pipe, vtx_resource, PIPE_TRANSFER_WRITE | PIPE_TRANSFER_UNSYNCHRONIZED, &vtx_transfer);
    assert(vtx_logical);
    memcpy(vtx_logical, vVertices, sizeof(vVertices));
    pipe_buffer_unmap(pipe, vtx_transfer);

    struct pipe_vertex_buffer vertex_buf_desc = {
            .stride = VERTEX_STRIDE*4,
            .buffer_offset = 0,
            .buffer = vtx_resource,
            .user_buffer = 0
            };
    struct pipe_vertex_element pipe_vertex_elements[] = {
        { /* positions */
            .src_offset = 0*4,
            .instance_divisor = 0,
            .vertex_buffer_index = 0,
            .src_format = PIPE_FORMAT_R32G32B32A32_FLOAT 
        },
        { /* texcoord */
            .src_offset = 4*4,
            .instance_divisor = 0,
            .vertex_buffer_index = 0,
            .src_format = PIPE_FORMAT_R32G32B32A32_FLOAT 
        },
    };
    void *vertex_elements = pipe->create_vertex_elements_state(pipe, 
            sizeof(pipe_vertex_elements)/sizeof(pipe_vertex_elements[0]), pipe_vertex_elements);

    /* compile gallium3d states */
    void *blend = pipe->create_blend_state(pipe, &(struct pipe_blend_state) {
                .rt[0] = {
                    .blend_enable = 0,
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

    /* Load shaders */
    if(argc < 3)
    {
        printf("Usage: ps_sandbox <vs.txt> <ps.txt>\n");
        exit(1);
    }
    char *vert_text = readfile(argv[1], NULL);
    if(vert_text == NULL)
    {
        printf("Could not load vertex shader %s\n", argv[1]);
        exit(1);
    }
    char *frag_text = readfile(argv[2], NULL);
    if(frag_text == NULL)
    {
        printf("Could not load vertex shader %s\n", argv[2]);
        exit(1);
    }
    void *vtx_shader = graw_parse_vertex_shader(pipe, vert_text);
    if(vtx_shader == NULL)
    {
        printf("Could not parse vertex shader %s\n", argv[1]);
        exit(1);
    }
    void *frag_shader = graw_parse_fragment_shader(pipe, frag_text);
    if(frag_shader == NULL)
    {
        printf("Could not parse fragment shader %s\n", argv[2]);
        exit(1);
    }
    pipe->bind_vs_state(pipe, vtx_shader);
    pipe->bind_fs_state(pipe, frag_shader);
   
    void *dsa_bigquad = pipe->create_depth_stencil_alpha_state(pipe, &(struct pipe_depth_stencil_alpha_state){
            .depth = {
                .enabled = 0,
                .writemask = 1,
                .func = PIPE_FUNC_LESS /* GL default */
            },
            .stencil[0] = { .enabled = 0 },
            .stencil[1] = { .enabled = 0 },
            .alpha = { .enabled = 0 }
            });
    
    pipe->bind_depth_stencil_alpha_state(pipe, dsa_bigquad);

    for(int frame=0; frame<1000; ++frame)
    {
        if(frame%50 == 0)
            printf("*** FRAME %i ****\n", frame);

        /* Clear render target */
        pipe->clear(pipe, PIPE_CLEAR_COLOR | PIPE_CLEAR_DEPTHSTENCIL, &(const union pipe_color_union) {
                .f = {0.2, 0.2, 0.2, 1.0}
                }, 0.75f, 0x01);

        float fs_const[4] = {frame / 1000.0f, -frame / 1000.0f, 0.0, 0.0f}; /* frame, for animation */
        pipe->set_constant_buffer(pipe, PIPE_SHADER_FRAGMENT, 0, &(struct pipe_constant_buffer){
                .user_buffer = fs_const,
                .buffer_size = sizeof(fs_const)
                });
        pipe->draw_vbo(pipe, &(struct pipe_draw_info){
                .indexed = 0,
                .mode = PIPE_PRIM_TRIANGLE_FAN,
                .start = 0,
                .count = 4 });

        etna_swap_buffers(fbs->buffers);
    }
#ifdef DUMP
    bmp_dump32(etna_bo_map(fbs->fb.buffer[1-fbs->buffers->backbuffer]), width, height, false, "/mnt/sdcard/fb.bmp");
    printf("Dump complete\n");
#endif
    fbdemo_free(fbs);
    return 0;
}

