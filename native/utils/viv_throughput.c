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
/* Measure fillrate.
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
#include <getopt.h>
#ifdef HAVE_CLOCK
#include <time.h>
#else
#include <sys/time.h>
#endif

#include "etna_pipe.h"
#include "etna_debug.h"
#include "util/u_inlines.h"
#include "util/u_format.h"
#include "write_bmp.h"
#include "state_tracker/graw.h"
#include "fbdemos.h"

#include "esTransform.h"
#include "dds.h"

#include <etnaviv/viv_profile.h>

const char *simple_vs =
    "VERT\n"
    "DCL IN[0]\n"
    "DCL IN[1]\n"
    "DCL OUT[0], POSITION\n"
    "DCL OUT[1], GENERIC[0]\n"
    "  0: MOV OUT[0], IN[0]\n"
    "  1: MOV OUT[1], IN[1]\n"
    "  2: END\n";

const char *simple_fs =
    "FRAG\n"
    "PROPERTY FS_COLOR0_WRITES_ALL_CBUFS 1\n"
    "DCL IN[0], GENERIC[0]\n"
    "DCL OUT[0], COLOR\n"
    "  0: MOV OUT[0], IN[0]\n"
    "  1: END\n";

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

/* Get time in microseconds */
static unsigned long gettime(void)
{
#ifdef HAVE_CLOCK
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_nsec/1000 + (t.tv_sec * 1000000));
#else
    struct timeval t;
    gettimeofday(&t, NULL);
    return (t.tv_usec + (t.tv_sec * 1000000));
#endif
}

int main(int argc, char **argv)
{
    int width = 1920;
    int height = 1080;
    bool do_clear = false;
    bool super_tiled = false;
    bool enable_ts = true;
    bool early_z = false;
    int num_frames = 2000;
    unsigned fmt_rt = PIPE_FORMAT_B8G8R8X8_UNORM;
    unsigned fmt_zs = PIPE_FORMAT_S8_UINT_Z24_UNORM;

    int opt;
    int error = 0;
    while ((opt = getopt(argc, argv, "w:h:l:s:t:e:f:d:c:")) != -1) {
        switch(opt)
        {
        case 'w': width = atoi(optarg); break;
        case 'h': height = atoi(optarg); break;
        case 'l': do_clear = atoi(optarg); break;
        case 's': super_tiled = atoi(optarg); break;
        case 't': enable_ts = atoi(optarg); break;
        case 'e': early_z = atoi(optarg); break;
        case 'f': num_frames = atoi(optarg); break;
        case 'd':
            switch(atoi(optarg))
            {
            case 0: fmt_zs = PIPE_FORMAT_NONE; break;
            case 16: fmt_zs = PIPE_FORMAT_Z16_UNORM; break;
            case 32: fmt_zs = PIPE_FORMAT_S8_UINT_Z24_UNORM; break;
            default:
                printf("Invalid depth stencil surface depth %s\n", optarg);
                error = 1;
            }
            break;
        case 'c':
            switch(atoi(optarg))
            {
            case 0: fmt_rt = PIPE_FORMAT_NONE; break;
            case 16: fmt_rt = PIPE_FORMAT_B5G6R5_UNORM; break;
            case 32: fmt_rt = PIPE_FORMAT_B8G8R8X8_UNORM; break;
            default:
                printf("Invalid color surface depth %s\n", optarg);
                error = 1;
            }
            break;
        default:
            printf("Unknown argument %c\n", opt);
            error = 1;
        }
    }
    if(error)
    {
        printf("Usage:\n");
        printf("  %s [-w <width>] [-h <height>] [-l <0/1>] [-s <0/1>] [-t <0/1>] [-e <0/1>] [-f <frames>] [-d <0/16/32>] [-c <16/32>]\n", argv[0]);
        printf("\n");
        printf("  -w <width>    Width of surface (default is 1920)\n");
        printf("  -h <height>   Height of surface (default is 1080)\n");
        printf("  -l <0/1>      Clear surface every frame (0=no, 1=yes, default is 0)\n");
        printf("  -s <0/1>      Use supertile layout (0=no, 1=yes, default is 0)\n");
        printf("  -t <0/1>      Enable TS (0=no, 1=yes, default is 1)\n");
        printf("  -e <0/1>      Enable early Z (0=no, 1=yes, default is 0)\n");
        printf("  -f <frames>   Number of frames to render (default is 2000)\n");
        printf("  -d <0/16/32>  Depth/stencil surface depth\n");
        printf("  -c <16/32>    Color surface depth\n");
        exit(1);
    }

    struct fbdemos_scaffold *fbs = 0;
    fbdemo_init(&fbs);
    struct pipe_context *pipe = fbs->pipe;
    /* resources */
    struct pipe_resource *rt_resource = NULL;
    struct pipe_resource *z_resource = NULL;
    if(!super_tiled)
        etna_mesa_debug |= ETNA_DBG_NO_SUPERTILE;
    if(!enable_ts)
        etna_mesa_debug |= ETNA_DBG_NO_TS;
    if(!early_z)
        etna_mesa_debug |= ETNA_DBG_NO_EARLY_Z;

    if(fmt_rt != PIPE_FORMAT_NONE)
        rt_resource = fbdemo_create_2d(fbs->screen, PIPE_BIND_RENDER_TARGET, fmt_rt, width, height, 0);
    if(fmt_zs != PIPE_FORMAT_NONE)
        z_resource = fbdemo_create_2d(fbs->screen, PIPE_BIND_RENDER_TARGET, fmt_zs, width, height, 0);
    struct pipe_resource *vtx_resource = pipe_buffer_create(fbs->screen, PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_IMMUTABLE, sizeof(vVertices));

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

    struct pipe_surface *cbuf = NULL;
    struct pipe_surface *zsbuf = NULL;
    if(rt_resource != NULL)
    {
        cbuf = pipe->create_surface(pipe, rt_resource, &(struct pipe_surface){
            .texture = rt_resource,
            .format = rt_resource->format,
            .u.tex.level = 0
            });
    }
    if(z_resource != NULL)
    {
        zsbuf = pipe->create_surface(pipe, z_resource, &(struct pipe_surface){
            .texture = z_resource,
            .format = z_resource->format,
            .u.tex.level = 0
            });
    }

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
            .nr_cbufs = (cbuf == NULL) ? 0 : 1,
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

    void *vtx_shader = graw_parse_vertex_shader(pipe, simple_vs);
    if(vtx_shader == NULL)
    {
        printf("Could not parse vertex shader\n");
        exit(1);
    }
    void *frag_shader = graw_parse_fragment_shader(pipe, simple_fs);
    if(frag_shader == NULL)
    {
        printf("Could not parse fragment shader\n");
        exit(1);
    }
    pipe->bind_vs_state(pipe, vtx_shader);
    pipe->bind_fs_state(pipe, frag_shader);

    void *dsa_bigquad = pipe->create_depth_stencil_alpha_state(pipe, &(struct pipe_depth_stencil_alpha_state){
            .depth = {
                .enabled = 0,
                .writemask = (fmt_zs != PIPE_FORMAT_NONE), /* write only to depth if enabled */
                .func = PIPE_FUNC_LESS /* GL default */
            },
            .stencil[0] = { .enabled = 0 },
            .stencil[1] = { .enabled = 0 },
            .alpha = { .enabled = 0 }
            });

    pipe->bind_depth_stencil_alpha_state(pipe, dsa_bigquad);

    /* Read out and reset profile counters */
    int num_profile_counters = viv_get_num_profile_counters();
    bool *reset_after_read = calloc(num_profile_counters, sizeof(bool));
    uint32_t *counter_data = calloc(num_profile_counters, 4);
    uint32_t *counter_data_initial = calloc(num_profile_counters, 4);
    if(viv_read_profile_counters_3d(fbs->conn, counter_data_initial) != 0)
    {
        fprintf(stderr, "Error querying counters (probably unsupported with this kernel, or not built into libetnaviv)\n");
    }
    viv_get_counters_reset_after_read(fbs->conn, reset_after_read);

    uint64_t start_time = gettime();
    printf("Timing...\n");
    for(int frame=0; frame<num_frames; ++frame)
    {
        if(do_clear)
        {
            /* Clear render target */
            pipe->clear(pipe, PIPE_CLEAR_COLOR | PIPE_CLEAR_DEPTHSTENCIL, &(const union pipe_color_union) {
                    .f = {0.2, 0.2, 0.2, 1.0}
                    }, 0.75f, 0x01);
        }

        pipe->draw_vbo(pipe, &(struct pipe_draw_info){
                .indexed = 0,
                .mode = PIPE_PRIM_TRIANGLE_FAN,
                .start = 0,
                .count = 4 });
        /* flush cache to make sure we're really writing to memory */
        etna_set_state(fbs->ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_stall(fbs->ctx, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);
    }
    etna_finish(fbs->ctx);
    if(viv_read_profile_counters_3d(fbs->conn, counter_data) != 0)
    {
        fprintf(stderr, "Error querying counters (probably unsupported with this kernel, or not built into libetnaviv)\n");
    }
    uint64_t end_time = gettime();
    uint64_t diff_time = end_time - start_time;
    uint32_t screen_size = 0;
    if(fmt_rt != PIPE_FORMAT_NONE)
        screen_size += width * height * util_format_get_blocksize(fmt_rt);
    if(fmt_zs != PIPE_FORMAT_NONE)
        screen_size += width * height * util_format_get_blocksize(fmt_zs);
    printf("Input\n");
    printf("  Frame: %i x %i\n", width, height);
    printf("  Color format: %s\n", util_format_name(fmt_rt));
    printf("  Depth format: %s\n", util_format_name(fmt_zs));
    printf("  Supertiled: %i\n", super_tiled);
    printf("  Enable TS: %i\n", enable_ts);
    printf("  Early z: %i\n", early_z);
    printf("  Do clear: %i\n", do_clear);
    printf("  Num frames: %i\n", num_frames);
    printf("  Frame size: %.1f MB\n", (double)screen_size / 1e6);
    printf("Statistics:\n");
    printf("  Elapsed time: %.2fs\n", (double)diff_time * 1e-6);
    printf("  FPS: %.1f\n", (double)num_frames / ((double)diff_time * 1e-6));
    printf("  Fillrate: %.1f MB/s\n", (double)num_frames * (double)screen_size / (double)diff_time);
    printf("  Vertices rendered: %i\n", counter_data[VIV_PROF_RENDERED_VERTICE_COUNTER]);
    printf("  Pixels rendered: %i\n", counter_data[VIV_PROF_RENDERED_PIXEL_COUNTER]);
    printf("  VS instructions: %i\n", counter_data[VIV_PROF_VS_INST_COUNTER]);
    printf("  PS instructions: %i\n", counter_data[VIV_PROF_PS_INST_COUNTER]);
    printf("  Read: %.1f MB/frame\n", (double)counter_data[VIV_PROF_GPU_TOTAL_READ_64_BIT] * 8 / 1e6 / num_frames);
    printf("  Written: %.1f MB/frame\n", (double)counter_data[VIV_PROF_GPU_TOTAL_WRITE_64_BIT] * 8 / 1e6 / num_frames);
    printf("  Stalls on read: %.1fM/frame\n", counter_data[VIV_PROF_HI_AXI_CYCLES_READ_REQUEST_STALLED] / 1e6 / num_frames);
    printf("  Stalls on write request: %.1fM/frame\n", counter_data[VIV_PROF_HI_AXI_CYCLES_WRITE_REQUEST_STALLED] / 1e6 / num_frames);
    printf("  Stalls on write data: %.1fM/frame\n", counter_data[VIV_PROF_HI_AXI_CYCLES_WRITE_DATA_STALLED] / 1e6 / num_frames);

    fbdemo_free(fbs);
    return 0;
}

