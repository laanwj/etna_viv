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
/* Displacement mapping using vertex texture.
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
#include "fbdemos.h"

#include "esTransform.h"
#include "esShapes.h"
#include "dds.h"

/*********************************************************************/
#ifndef M_PI
# define M_PI		3.14159265358979323846	/* pi */
#endif

#define VERTEX_BUFFER_SIZE 0x60000

/* displacement_vs.asm */
const uint32_t vs[] = {
0x01831009,0x00000000,0x00000000,0x203fc048,
0x02031009,0x00000000,0x00000000,0x203fc058,
0x47841018,0x15000f20,0x00000000,0x00000000,
0x00841003,0x00004800,0x015405c0,0x00000002,
0x03841003,0x00004800,0x01480140,0x00000000,
0x04041009,0x00000000,0x00000000,0x203fc068,
0x07841001,0x39001800,0x00000000,0x00390048,
0x07851003,0x39000800,0x00000250,0x00000000,
0x07851002,0x39001800,0x00aa0250,0x00390058,
0x07851002,0x39002800,0x01540250,0x00390058,
0x07841002,0x39003800,0x01fe0250,0x00390058,
0x03851003,0x29004800,0x00000150,0x00000000,
0x03851002,0x29005800,0x00aa0150,0x00290058,
0x03821002,0x29006800,0x01540150,0x00290058,
0x07851003,0x39007800,0x000000d0,0x00000000,
0x07851002,0x39008800,0x00aa00d0,0x00390058,
0x07851002,0x39009800,0x015400d0,0x00390058,
0x07811002,0x3900a800,0x01fe00d0,0x00390058,
0x0402100c,0x00000000,0x00000000,0x003fc018,
0x03811002,0x69001800,0x01fe0140,0x00290038,
0x03831005,0x29001800,0x014800c0,0x00000000,
0x0383100d,0x00000000,0x00000000,0x00000038,
0x03811003,0x29001800,0x014801c0,0x00000000,
0x00811005,0x29002800,0x014800c0,0x00000000,
0x0081108f,0x3fc06800,0x000000d0,0x203fc068,
0x03011009,0x00000000,0x00000000,0x203fc068,
0x04011009,0x00000000,0x00000000,0x200000b8,
0x02041001,0x2a804800,0x00000000,0x003fc048,
0x02041003,0x2a804800,0x00aa05c0,0x00000002,
};
/* displacement_ps.asm */
const uint32_t ps[] = {
0x00000000,0x00000000,0x00000000,0x00000000,
};

const struct etna_shader_program shader = {
    .num_inputs = 3,
    .inputs = {{.vs_reg=1},{.vs_reg=2},{.vs_reg=0}},
    .num_varyings = 1,
    .varyings = {
        {.num_components=4, .special=ETNA_VARYING_VSOUT, .pa_attributes=0x200, .vs_reg=1}
    }, 
    .vs_code_size = sizeof(vs)/4,
    .vs_code = (uint32_t*)vs,
    .vs_pos_out_reg = 4, // t4 out
    .vs_load_balancing = 0xf3f0582,
    .vs_num_temps = 6,
    .vs_uniforms_size = 12*4,
    .vs_uniforms = (uint32_t*)(const float[12*4]){
        [19] = 2.0f, /* u4.w */
        [23] = 20.0f, /* u5.w */
        [27] = 0.0f, /* u6.w */
        [44] = 1.0f, /* u11.x */
        [45] = 0.5f, /* u11.y */
        [46] = 0.2f, /* u11.z -- scaling factor applied to displacement */
    },
    .ps_code_size = sizeof(ps)/4,
    .ps_code = (uint32_t*)ps,
    .ps_color_out_reg = 1, // t1 out
    .ps_num_temps = 2,
    .ps_uniforms_size = 0,
    .ps_uniforms = NULL,
};

#define TEX_WIDTH (32)
#define TEX_HEIGHT (32)
static struct pipe_resource *createSimpleTexture(struct pipe_screen *screen, struct pipe_context *pipe)
{
    struct pipe_resource *tex_resource = fbdemo_create_2d(screen, PIPE_BIND_SAMPLER_VIEW, PIPE_FORMAT_L8_UNORM, TEX_WIDTH, TEX_HEIGHT, 0);
    uint8_t pixels[TEX_HEIGHT][TEX_WIDTH];

    for(int y=0; y<TEX_HEIGHT; ++y)
    {
        for(int x=0; x<TEX_WIDTH; ++x)
        {
            float xx = (float)x / (float)(TEX_WIDTH-1) * 2.0f - 1.0f;
            float yy = (float)y / (float)(TEX_HEIGHT-1) * 2.0f - 1.0f;
            //float vv = (0.25*xx*xx*yy*yy);
            //float vv = (sin(xx*2.0*M_PI*2.0)*sin(yy*2.0*M_PI*2.0) + 1.0f) / 2.0f;
            float vv = sin(xx*2.0*M_PI*2.0)*sin(yy*2.0*M_PI*2.0);
            vv = vv * (1.0 - yy * yy); /* flatten over poles */
            pixels[y][x] = etna_cfloat_to_uint8(vv);
            printf("%3i ", pixels[y][x]);
        }
        printf("\n");
    }

    etna_pipe_inline_write(pipe, tex_resource, 0, 0, &pixels[0][0], TEX_WIDTH*TEX_HEIGHT);

    return tex_resource;
}
#undef TEX_WIDTH
#undef TEX_HEIGHT

int main(int argc, char **argv)
{
    struct fbdemos_scaffold *fbs = 0;
    fbdemo_init(&fbs);
    int width = fbs->width;
    int height = fbs->height;
    struct pipe_context *pipe = fbs->pipe;

    /* resources */
    struct pipe_resource *rt_resource = fbdemo_create_2d(fbs->screen, PIPE_BIND_RENDER_TARGET, PIPE_FORMAT_B8G8R8X8_UNORM, width, height, 0);
    struct pipe_resource *z_resource = fbdemo_create_2d(fbs->screen, PIPE_BIND_RENDER_TARGET, PIPE_FORMAT_Z16_UNORM, width, height, 0);
    struct pipe_resource *vtx_resource = pipe_buffer_create(fbs->screen, PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_IMMUTABLE, VERTEX_BUFFER_SIZE);
    struct pipe_resource *idx_resource = pipe_buffer_create(fbs->screen, PIPE_BIND_INDEX_BUFFER, PIPE_USAGE_IMMUTABLE, VERTEX_BUFFER_SIZE);
    
    /* bind render target to framebuffer */
    etna_fb_bind_resource(fbs, rt_resource);

    /* Phew, now we got all the memory we need.
     * Write interleaved attribute vertex stream.
     * Unlike the GL example we only do this once, not every time glDrawArrays is called, the same would be accomplished
     * from GL by using a vertex buffer object.
     */
    float *vVertices;
    float *vNormals;
    float *vTexCoords;
    uint16_t *vIndices;
    int numVertices = 0;
    int numIndices = esGenSphere(80, 1.0f, &vVertices, &vNormals,
                                        &vTexCoords, &vIndices, &numVertices);

    unsigned vtxStride = 3+3+2;
    assert((numVertices * vtxStride*4) < VERTEX_BUFFER_SIZE);
    struct pipe_transfer *vtx_transfer = 0;
    float *vtx_logical = pipe_buffer_map(pipe, vtx_resource, PIPE_TRANSFER_WRITE | PIPE_TRANSFER_UNSYNCHRONIZED, &vtx_transfer);
    for(int vert=0; vert<numVertices; ++vert)
    {
        int dest_idx = vert * vtxStride;
        for(int comp=0; comp<3; ++comp)
            vtx_logical[dest_idx+comp+0] = vVertices[vert*3 + comp]; /* 0 */
        for(int comp=0; comp<3; ++comp)
            vtx_logical[dest_idx+comp+3] = vNormals[vert*3 + comp]; /* 1 */
        for(int comp=0; comp<2; ++comp)
            vtx_logical[dest_idx+comp+6] = vTexCoords[vert*2 + comp]; /* 2 */
    }
    pipe_buffer_unmap(pipe, vtx_transfer);

    assert((numIndices * 2) < VERTEX_BUFFER_SIZE);
    struct pipe_transfer *idx_transfer = 0;
    void *idx_logical = pipe_buffer_map(pipe, idx_resource, PIPE_TRANSFER_WRITE | PIPE_TRANSFER_UNSYNCHRONIZED, &idx_transfer);
    memcpy(idx_logical, vIndices, numIndices*sizeof(uint16_t));
    pipe_buffer_unmap(pipe, idx_transfer);

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

    /* texture... */
    struct pipe_resource *tex_resource = createSimpleTexture(fbs->screen, pipe);
    struct pipe_sampler_view *sampler_view = pipe->create_sampler_view(pipe, tex_resource, &(struct pipe_sampler_view){
            .format = tex_resource->format,
            .u.tex.first_level = 0,
            .u.tex.last_level = 0,
            .swizzle_r = PIPE_SWIZZLE_RED,
            .swizzle_g = PIPE_SWIZZLE_GREEN,
            .swizzle_b = PIPE_SWIZZLE_BLUE,
            .swizzle_a = PIPE_SWIZZLE_ALPHA,
            });
    void *sampler = pipe->create_sampler_state(pipe, &(struct pipe_sampler_state) {
                .wrap_s = PIPE_TEX_WRAP_REPEAT,
                .wrap_t = PIPE_TEX_WRAP_REPEAT,
                .wrap_r = PIPE_TEX_WRAP_REPEAT,
                .min_img_filter = PIPE_TEX_FILTER_LINEAR,
                .min_mip_filter = PIPE_TEX_MIPFILTER_NONE,
                .mag_img_filter = PIPE_TEX_FILTER_LINEAR,
                .normalized_coords = 1,
                .lod_bias = 0.0f,
                .min_lod = 0.0f, .max_lod=1000.0f
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
    pipe->set_vertex_buffers(pipe, 0, 1, &(struct pipe_vertex_buffer){
            .stride = vtxStride*4,
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
   
    /* vertex samplers */
    pipe->bind_vertex_sampler_states(pipe, 1, &sampler);
    pipe->set_vertex_sampler_views(pipe, 1, &sampler_view);
   
    void *shader_state = etna_create_shader_state(pipe, &shader);
    etna_bind_shader_state(pipe, shader_state);

    ESMatrix projection;
    float aspect = (float)(height) / (float)(width);
    esMatrixLoadIdentity(&projection);
    esFrustum(&projection, -1.8f, +1.8f, -1.8f * aspect, +1.8f * aspect, 6.0f, 10.0f);

    for(int frame=0; frame<1000; ++frame)
    {
        if(frame%50 == 0)
            printf("*** FRAME %i ****\n", frame);
        /*   Compute transform matrices in the same way as cube egl demo */ 
        ESMatrix modelview, modelviewprojection;
        ESMatrix inverse, normal; 
        esMatrixLoadIdentity(&modelview);
        esTranslate(&modelview, 0.0f, 0.0f, -8.0f);
        esRotate(&modelview, 45.0f, 1.0f, 0.0f, 0.0f);
        esRotate(&modelview, 45.0f, 0.0f, 1.0f, 0.0f);
        esRotate(&modelview, frame*0.5f, 0.0f, 0.0f, 1.0f);
        esMatrixLoadIdentity(&modelviewprojection);
        esMatrixMultiply(&modelviewprojection, &modelview, &projection);
        esMatrixInverse3x3(&inverse, &modelview);
        esMatrixTranspose(&normal, &inverse);
       
        /* Clear render target */
        pipe->clear(pipe, PIPE_CLEAR_COLOR | PIPE_CLEAR_DEPTHSTENCIL, &(const union pipe_color_union) {
                .f = {0.2, 0.2, 0.2, 1.0}
                }, 1.0, 0xff);
        
        etna_set_uniforms(pipe, PIPE_SHADER_VERTEX, 0, 16, (uint32_t*)&modelviewprojection.m[0][0]);
        etna_set_uniforms(pipe, PIPE_SHADER_VERTEX, 16, 3, (uint32_t*)&normal.m[0][0]); /* u4.xyz */
        etna_set_uniforms(pipe, PIPE_SHADER_VERTEX, 20, 3, (uint32_t*)&normal.m[1][0]); /* u5.xyz */
        etna_set_uniforms(pipe, PIPE_SHADER_VERTEX, 24, 3, (uint32_t*)&normal.m[2][0]); /* u6.xyz */
        etna_set_uniforms(pipe, PIPE_SHADER_VERTEX, 28, 16, (uint32_t*)&modelview.m[0][0]);

        float scaling = fmodf(frame* 0.025f, 2.0f);
        if(scaling > 1.0f)
            scaling = 2.0f - scaling;  // sawtooth
        etna_set_uniforms(pipe, PIPE_SHADER_VERTEX, 46, 1, (uint32_t*)&scaling);

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
        etna_swap_buffers(fbs->buffers);
    }
#ifdef DUMP
    bmp_dump32(etna_bo_map(fbs->fb.buffer[1-fbs->buffers->backbuffer]), width, height, false, "/mnt/sdcard/fb.bmp");
    printf("Dump complete\n");
#endif
    fbdemo_free(fbs);
    return 0;
}
