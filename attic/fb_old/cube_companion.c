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
 * - Anti-aliasing (MSAA)
 *
 * Still TODO:
 * - Mipmapping (hw generation, if possible)
 * - Cube map textures
 * - Decrease verbosity by grouping often-used commands
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

#include <errno.h>

#include <etnaviv/common.xml.h>
#include <etnaviv/state.xml.h>
#include <etnaviv/state_3d.xml.h>
#include <etnaviv/cmdstream.xml.h>

#include <etnaviv/viv.h>
#include <etnaviv/etna.h>
#include <etnaviv/etna_rs.h>
#include <etnaviv/etna_bo.h>
#include <etnaviv/etna_util.h>

#include "write_bmp.h"

#include "etna_pipe.h"
#include "fbdemos.h"

#include "esTransform.h"

#include "companion.h"

//#define TEST_PATTERN /* Upload test pattern instead of texture */
//#define EXTRA_DELAYS /* Insert extra delays (that blob does but appear unnecessary on my hw for this demo) */
#define INDEXED /* Used indexed rendering */
#define INDEX_BUFFER_SIZE 0x8000
#define VERTEX_BUFFER_SIZE 0x60000
uint32_t vs[] = {
    0x01831009, 0x00000000, 0x00000000, 0x203fc048,
    0x02031009, 0x00000000, 0x00000000, 0x203fc058,
    0x07841003, 0x39000800, 0x00000050, 0x00000000,
    0x07841002, 0x39001800, 0x00aa0050, 0x00390048,
    0x07841002, 0x39002800, 0x01540050, 0x00390048,
    0x07841002, 0x39003800, 0x01fe0050, 0x00390048,
    0x03851003, 0x29004800, 0x000000d0, 0x00000000,
    0x03851002, 0x29005800, 0x00aa00d0, 0x00290058,
    0x03811002, 0x29006800, 0x015400d0, 0x00290058,
    0x07851003, 0x39007800, 0x00000050, 0x00000000,
    0x07851002, 0x39008800, 0x00aa0050, 0x00390058,
    0x07851002, 0x39009800, 0x01540050, 0x00390058,
    0x07801002, 0x3900a800, 0x01fe0050, 0x00390058,
    0x0401100c, 0x00000000, 0x00000000, 0x003fc008,
    0x03801002, 0x69000800, 0x01fe00c0, 0x00290038,
    0x03831005, 0x29000800, 0x01480040, 0x00000000,
    0x0383100d, 0x00000000, 0x00000000, 0x00000038,
    0x03801003, 0x29000800, 0x014801c0, 0x00000000,
    0x00801005, 0x29001800, 0x01480040, 0x00000000,
    0x0380108f, 0x3fc06800, 0x00000050, 0x203fc068,
    0x04001009, 0x00000000, 0x00000000, 0x200000b8,
    0x01811009, 0x00000000, 0x00000000, 0x00150028,
    0x02041001, 0x2a804800, 0x00000000, 0x003fc048,
    0x02041003, 0x2a804800, 0x00aa05c0, 0x00000002,
};
#if 1
uint32_t ps[] = { /* texture sampling */
    0x07811003, 0x00000800, 0x01c800d0, 0x00000000,
    0x07821018, 0x15002f20, 0x00000000, 0x00000000,
    0x07811003, 0x39001800, 0x01c80140, 0x00000000,
};
#else
uint32_t ps[] = { /* passthrough */
    0x00000000, 0x00000000, 0x00000000, 0x00000000
};
#endif

size_t vs_size = sizeof(vs);
size_t ps_size = sizeof(ps);

int main(int argc, char **argv)
{
    int width, height;
    int width_s, height_s;
    int padded_width, padded_height;
    int backbuffer = 0;
    int supersample_x = 2; // 1 or 2
    int supersample_y = 2; // 1 or 2
    int extra_ps_inputs = 0;

    struct fbdemos_scaffold *fbs = 0;
    fbdemo_init(&fbs);
    width = fbs->width;
    height = fbs->height;
    struct viv_conn *conn = fbs->conn;

    bool supertiled = VIV_FEATURE(conn, chipMinorFeatures0,SUPER_TILED);
    unsigned bits_per_tile = VIV_FEATURE(conn, chipMinorFeatures0,2BITPERTILE)?2:4;
    printf("supertiled: %i bits per tile: %i\n", supertiled, bits_per_tile);

    width_s = width * supersample_x;
    height_s = height * supersample_y;
    padded_width = etna_align_up(width_s, 64);
    padded_height = etna_align_up(height_s, 64);
    extra_ps_inputs = (supersample_x > 1 || supersample_y > 1); // in case of supersample, there is an extra input to PS

    printf("padded_width %i padded_height %i\n", padded_width, padded_height);

    struct etna_bo *rt = 0; /* main render target */
    struct etna_bo *rt_ts = 0; /* tile status for main render target */
    struct etna_bo *z = 0; /* depth for main render target */
    struct etna_bo *z_ts = 0; /* depth ts for main render target */
    struct etna_bo *vtx = 0; /* vertex buffer */
    struct etna_bo *idx = 0; /* index buffer */
    struct etna_bo *aux_rt = 0; /* auxilary render target */
    struct etna_bo *aux_rt_ts = 0; /* tile status for auxilary render target */
    struct etna_bo *tex = 0; /* texture */
    struct etna_bo *bmp = 0; /* bitmap */

    /* TODO: anti aliasing (doubles width/height) */
    size_t rt_size = padded_width * padded_height * 4;
    size_t rt_ts_size = etna_align_up((padded_width * padded_height * 4)*bits_per_tile/0x80, 0x100);
    size_t z_size = padded_width * padded_height * 2;
    size_t z_ts_size = etna_align_up((padded_width * padded_height * 2)*bits_per_tile/0x80, 0x100);
    size_t bmp_size = width * height * 4;

    if((rt=etna_bo_new(conn, rt_size, DRM_ETNA_GEM_TYPE_RT))==NULL ||
       (rt_ts=etna_bo_new(conn, rt_ts_size, DRM_ETNA_GEM_TYPE_TS))==NULL ||
       (z=etna_bo_new(conn, z_size, DRM_ETNA_GEM_TYPE_ZS))==NULL ||
       (z_ts=etna_bo_new(conn, z_ts_size, DRM_ETNA_GEM_TYPE_TS))==NULL ||
       (vtx=etna_bo_new(conn, VERTEX_BUFFER_SIZE, DRM_ETNA_GEM_TYPE_VTX))==NULL ||
       (idx=etna_bo_new(conn, INDEX_BUFFER_SIZE, DRM_ETNA_GEM_TYPE_IDX))==NULL ||
       (aux_rt=etna_bo_new(conn, 0x4000, DRM_ETNA_GEM_TYPE_RT))==NULL ||
       (aux_rt_ts=etna_bo_new(conn, 0x100, DRM_ETNA_GEM_TYPE_TS))==NULL ||
       (tex=etna_bo_new(conn, 0x100000, DRM_ETNA_GEM_TYPE_TEX))==NULL ||
       (bmp=etna_bo_new(conn, bmp_size, DRM_ETNA_GEM_TYPE_BMP))==NULL
       )
    {
        fprintf(stderr, "Error allocating video memory\n");
        exit(1);
    }

    /* Phew, now we got all the memory we need.
     * Write interleaved attribute vertex stream.
     * Unlike the GL example we only do this once, not every time glDrawArrays is called, the same would be accomplished
     * from GL by using a vertex buffer object.
     */
    memset(etna_bo_map(vtx), 0, VERTEX_BUFFER_SIZE);
#ifndef INDEXED
    printf("Interleaving vertices...\n");
    float *vertices_array = companion_vertices_array();
    float *texture_coordinates_array =
            companion_texture_coordinates_array();
    float *normals_array = companion_normals_array();
    assert(COMPANION_ARRAY_COUNT*(3+3+2)*sizeof(float) < VERTEX_BUFFER_SIZE);
    float *vtx_map = (float*)etna_bo_map(vtx);
    for(int vert=0; vert<COMPANION_ARRAY_COUNT; ++vert)
    {
        int dest_idx = vert * (3 + 3 + 2);
        for(int comp=0; comp<3; ++comp)
            vtx_map[dest_idx+comp+0] = vertices_array[vert*3 + comp]; /* 0 */
        for(int comp=0; comp<3; ++comp)
            vtx_map[dest_idx+comp+3] = normals_array[vert*3 + comp]; /* 1 */
        for(int comp=0; comp<2; ++comp)
            vtx_map[dest_idx+comp+6] = texture_coordinates_array[vert*2 + comp]; /* 2 */
    }
#else
    printf("Interleaving vertices and copying index buffer...\n");
    assert(COMPANION_VERTEX_COUNT*(3+3+2)*sizeof(float) < VERTEX_BUFFER_SIZE);
    float *vtx_map = (float*)etna_bo_map(vtx);
    for(int vert=0; vert<COMPANION_VERTEX_COUNT; ++vert)
    {
        int dest_idx = vert * (3 + 3 + 2);
        for(int comp=0; comp<3; ++comp)
            vtx_map[dest_idx+comp+0] = companion_vertices[vert][comp]; /* 0 */
        for(int comp=0; comp<3; ++comp)
            vtx_map[dest_idx+comp+3] = companion_normals[vert][comp]; /* 1 */
        for(int comp=0; comp<2; ++comp)
            vtx_map[dest_idx+comp+6] = companion_texture_coordinates[vert][comp]; /* 2 */
    }
    assert(COMPANION_TRIANGLE_COUNT*3*sizeof(unsigned short) < INDEX_BUFFER_SIZE);
    memcpy(etna_bo_map(idx), &companion_triangles[0][0], COMPANION_TRIANGLE_COUNT*3*sizeof(unsigned short));
#endif
    /* Fill in texture (convert from RGB linear to tiled) */
#define TILE_WIDTH (4)
#define TILE_HEIGHT (4)
#define TILE_WORDS (TILE_WIDTH*TILE_HEIGHT)
    unsigned ytiles = COMPANION_TEXTURE_HEIGHT / TILE_HEIGHT;
    unsigned xtiles = COMPANION_TEXTURE_WIDTH / TILE_WIDTH;
    unsigned dst_stride = xtiles * TILE_WORDS;
    uint32_t *tex_map = (uint32_t*)etna_bo_map(tex);

    for(unsigned ty=0; ty<ytiles; ++ty)
    {
        for(unsigned tx=0; tx<xtiles; ++tx)
        {
            unsigned ofs = ty * dst_stride + tx * TILE_WORDS;
            for(unsigned y=0; y<TILE_HEIGHT; ++y)
            {
                for(unsigned x=0; x<TILE_WIDTH; ++x)
                {
                    unsigned srcy = ty*TILE_HEIGHT + y;
                    unsigned srcx = tx*TILE_WIDTH + x;
                    unsigned src_ofs = (srcy*COMPANION_TEXTURE_WIDTH+srcx)*3;
                    unsigned r,g,b,a;
#ifndef TEST_PATTERN /* actual texture */
                    r = ((uint8_t*)companion_texture)[src_ofs+0];
                    g = ((uint8_t*)companion_texture)[src_ofs+1];
                    b = ((uint8_t*)companion_texture)[src_ofs+2];
#else /* test pattern */
                    r = srcx; g = srcy; b = 0;
#endif
                    a = 255;

                    tex_map[ofs] = ((a&0xFF) << 24) | ((b&0xFF) << 16) | ((g&0xFF) << 8) | (r&0xFF);
                    ofs += 1;
                }
            }
        }
    }

    struct etna_ctx *ctx = 0;
    if(etna_create(conn, &ctx) != ETNA_OK)
    {
        printf("Unable to create context\n");
        exit(1);
    }

    for(int frame=0; frame<1000; ++frame)
    {
        printf("*** FRAME %i ****\n", frame);
        /* XXX part of this can be put outside the loop, but until we have usable context management
         * this is safest.
         */
        etna_set_state(ctx, VIVS_GL_VERTEX_ELEMENT_CONFIG, 0x1);
        etna_set_state(ctx, VIVS_RA_CONTROL, 0x1);
        etna_set_state(ctx, VIVS_PA_W_CLIP_LIMIT, 0x34000001);
        etna_set_state(ctx, VIVS_PA_SYSTEM_MODE, 0x11);
        etna_set_state(ctx, VIVS_PA_CONFIG, ETNA_MASKED_BIT(VIVS_PA_CONFIG_UNK22, 0));
        etna_set_state(ctx, VIVS_SE_CONFIG, 0x0);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);

        etna_set_state(ctx, VIVS_PE_ALPHA_CONFIG,
                ETNA_MASKED_BIT(VIVS_PE_ALPHA_CONFIG_BLEND_ENABLE_COLOR, 0) &
                ETNA_MASKED_BIT(VIVS_PE_ALPHA_CONFIG_BLEND_SEPARATE_ALPHA, 0) &
                ETNA_MASKED(VIVS_PE_ALPHA_CONFIG_SRC_FUNC_COLOR, BLEND_FUNC_ONE) &
                ETNA_MASKED(VIVS_PE_ALPHA_CONFIG_SRC_FUNC_ALPHA, BLEND_FUNC_ONE) &
                ETNA_MASKED(VIVS_PE_ALPHA_CONFIG_DST_FUNC_COLOR, BLEND_FUNC_ZERO) &
                ETNA_MASKED(VIVS_PE_ALPHA_CONFIG_DST_FUNC_ALPHA, BLEND_FUNC_ZERO) &
                ETNA_MASKED(VIVS_PE_ALPHA_CONFIG_EQ_COLOR, BLEND_EQ_ADD) &
                ETNA_MASKED(VIVS_PE_ALPHA_CONFIG_EQ_ALPHA, BLEND_EQ_ADD));
        etna_set_state(ctx, VIVS_PE_ALPHA_BLEND_COLOR,
                VIVS_PE_ALPHA_BLEND_COLOR_B(0) |
                VIVS_PE_ALPHA_BLEND_COLOR_G(0) |
                VIVS_PE_ALPHA_BLEND_COLOR_R(0) |
                VIVS_PE_ALPHA_BLEND_COLOR_A(0));
        etna_set_state(ctx, VIVS_PE_ALPHA_OP, ETNA_MASKED_BIT(VIVS_PE_ALPHA_OP_ALPHA_TEST, 0));
        etna_set_state(ctx, VIVS_PA_CONFIG, ETNA_MASKED_INL(VIVS_PA_CONFIG_CULL_FACE_MODE, CCW));
        etna_set_state(ctx, VIVS_PE_STENCIL_CONFIG, ETNA_MASKED(VIVS_PE_STENCIL_CONFIG_REF_FRONT, 0) &
                                                    ETNA_MASKED(VIVS_PE_STENCIL_CONFIG_MASK_FRONT, 0xff) &
                                                    ETNA_MASKED(VIVS_PE_STENCIL_CONFIG_WRITE_MASK, 0xff) &
                                                    ETNA_MASKED_INL(VIVS_PE_STENCIL_CONFIG_MODE, DISABLED));
        etna_set_state(ctx, VIVS_PE_STENCIL_OP, ETNA_MASKED(VIVS_PE_STENCIL_OP_FUNC_FRONT, COMPARE_FUNC_ALWAYS) &
                                                ETNA_MASKED(VIVS_PE_STENCIL_OP_FUNC_BACK, COMPARE_FUNC_ALWAYS) &
                                                ETNA_MASKED(VIVS_PE_STENCIL_OP_FAIL_FRONT, STENCIL_OP_KEEP) &
                                                ETNA_MASKED(VIVS_PE_STENCIL_OP_FAIL_BACK, STENCIL_OP_KEEP) &
                                                ETNA_MASKED(VIVS_PE_STENCIL_OP_DEPTH_FAIL_FRONT, STENCIL_OP_KEEP) &
                                                ETNA_MASKED(VIVS_PE_STENCIL_OP_DEPTH_FAIL_BACK, STENCIL_OP_KEEP) &
                                                ETNA_MASKED(VIVS_PE_STENCIL_OP_PASS_FRONT, STENCIL_OP_KEEP) &
                                                ETNA_MASKED(VIVS_PE_STENCIL_OP_PASS_BACK, STENCIL_OP_KEEP));

        etna_set_state(ctx, VIVS_SE_DEPTH_SCALE, 0x0);
        etna_set_state(ctx, VIVS_SE_DEPTH_BIAS, 0x0);

        etna_set_state(ctx, VIVS_PA_CONFIG, ETNA_MASKED_INL(VIVS_PA_CONFIG_FILL_MODE, SOLID) &
                                            ETNA_MASKED_INL(VIVS_PA_CONFIG_SHADE_MODEL, SMOOTH));

        etna_set_state(ctx, VIVS_PE_COLOR_FORMAT,
                ETNA_MASKED_BIT(VIVS_PE_COLOR_FORMAT_OVERWRITE, 0) &
                ETNA_MASKED(VIVS_PE_COLOR_FORMAT_COMPONENTS, 0xf) &
                ETNA_MASKED(VIVS_PE_COLOR_FORMAT_FORMAT, RS_FORMAT_X8R8G8B8) &
                ETNA_MASKED_BIT(VIVS_PE_COLOR_FORMAT_SUPER_TILED, supertiled));

        etna_set_state(ctx, VIVS_PE_COLOR_ADDR, etna_bo_gpu_address(rt));
        etna_set_state(ctx, VIVS_PE_COLOR_STRIDE, padded_width * 4);

        uint32_t ts_msaa_config;
        if(supersample_x == 2 && supersample_y == 2)
        {
            // 4X MSAA
            etna_set_state(ctx, VIVS_GL_MULTI_SAMPLE_CONFIG,
                    ETNA_MASKED_INL(VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES, 4X) &
                    ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_ENABLES, 0xf) &
                    ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_UNK12, 0x0) &
                    ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_UNK16, 0x0)
                    );
            etna_set_state(ctx, VIVS_RA_MULTISAMPLE_UNK00E04, 0x0);
            etna_set_state(ctx, VIVS_RA_MULTISAMPLE_UNK00E10(2), 0xaaa22a22);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(8), 0x262a2288);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(9), 0x886688a2);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(10), 0x888866aa);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(11), 0x668888a6);
            etna_set_state(ctx, VIVS_RA_MULTISAMPLE_UNK00E10(1), 0xe6ae622a);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(4), 0x46622a88);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(5), 0x888888ae);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(6), 0x888888e6);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(7), 0x888888ca);
            etna_set_state(ctx, VIVS_RA_MULTISAMPLE_UNK00E10(0), 0xeaa26e26);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(0), 0x4a6e2688);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(1), 0x888888a2);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(2), 0x888888ea);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(3), 0x888888c6);
            ts_msaa_config = VIVS_TS_MEM_CONFIG_MSAA | VIVS_TS_MEM_CONFIG_MSAA_FORMAT_A8R8G8B8;
        } else if(supersample_x == 2 && supersample_y == 1)
        {
            // 2X MSAA
            etna_set_state(ctx, VIVS_GL_MULTI_SAMPLE_CONFIG,
                    ETNA_MASKED_INL(VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES, 2X) &
                    ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_ENABLES, 0xf) &
                    ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_UNK12, 0x0) &
                    ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_UNK16, 0x0)
                    );
            etna_set_state(ctx, VIVS_RA_MULTISAMPLE_UNK00E04, 0x0);
            etna_set_state(ctx, VIVS_RA_MULTISAMPLE_UNK00E10(0), 0xaa22);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(0), 0x66aa2288);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(1), 0x88558800);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(2), 0x88881100);
            etna_set_state(ctx, VIVS_RA_CENTROID_TABLE(3), 0x33888800);
            ts_msaa_config = VIVS_TS_MEM_CONFIG_MSAA | VIVS_TS_MEM_CONFIG_MSAA_FORMAT_A8R8G8B8;
        } else { // No multisampling
            etna_set_state(ctx, VIVS_GL_MULTI_SAMPLE_CONFIG,
                    ETNA_MASKED_INL(VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES, NONE) &
                    ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_ENABLES, 0xf) &
                    ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_UNK12, 0x0) &
                    ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_UNK16, 0x0)
                    );
            ts_msaa_config = 0;
        }

        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);

        etna_set_state(ctx, VIVS_TS_COLOR_CLEAR_VALUE, 0);
        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, etna_bo_gpu_address(rt_ts));
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, etna_bo_gpu_address(rt));

        etna_set_state(ctx, VIVS_PE_DEPTH_CONFIG,
                ETNA_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_WRITE_ENABLE, 0) &
                ETNA_MASKED_INL(VIVS_PE_DEPTH_CONFIG_DEPTH_FORMAT, D16) &
                ETNA_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_SUPER_TILED, supertiled) &
                ETNA_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_EARLY_Z, 1));
        etna_set_state(ctx, VIVS_PE_DEPTH_ADDR, etna_bo_gpu_address(z));
        etna_set_state(ctx, VIVS_PE_DEPTH_STRIDE, padded_width * 2);
        etna_set_state(ctx, VIVS_PE_HDEPTH_CONTROL, VIVS_PE_HDEPTH_CONTROL_FORMAT_DISABLED);
        etna_set_state_f32(ctx, VIVS_PE_DEPTH_NORMALIZE, 65535.0);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_DEPTH);

        etna_set_state(ctx, VIVS_TS_DEPTH_CLEAR_VALUE, 0xffffffff);
        etna_set_state(ctx, VIVS_TS_DEPTH_STATUS_BASE, etna_bo_gpu_address(z_ts));
        etna_set_state(ctx, VIVS_TS_DEPTH_SURFACE_BASE, etna_bo_gpu_address(z));
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG,
                VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
                VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR |
                VIVS_TS_MEM_CONFIG_DEPTH_16BPP |
                VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION |
                ts_msaa_config);

#ifdef EXTRA_DELAYS
        /* Warm up RS on aux render target (is this needed?) */
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_warm_up_rs(ctx, etna_bo_gpu_address(aux_rt), etna_bo_gpu_address(aux_rt_ts));

        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, rt_ts_physical);
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, rt_physical);

        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_warm_up_rs(ctx, aux_rt_physical, aux_rt_ts_physical);

        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, rt_ts_physical);
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, rt_physical);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);

        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_warm_up_rs(ctx, aux_rt_physical, aux_rt_ts_physical);
        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, rt_ts_physical);
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, rt_physical);
#endif
        /* sync rasterizer to pixel engine after changes to PE config */
        etna_stall(ctx, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);

        /* Set up the resolve to clear tile status for main render target and depth
         * Regard the TS as an image of width 16 with 4 bytes per pixel (64 bytes per row)
         * */
        etna_set_state(ctx, VIVS_RS_CONFIG,
                VIVS_RS_CONFIG_SOURCE_FORMAT(RS_FORMAT_A8R8G8B8) |
                VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_A8R8G8B8)
                );
        etna_set_state_multi(ctx, VIVS_RS_DITHER(0), 2, (uint32_t[]){0xffffffff, 0xffffffff});
        etna_set_state(ctx, VIVS_RS_FILL_VALUE(0), (bits_per_tile==4)?0x11111111:0x55555555);
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL,
                VIVS_RS_CLEAR_CONTROL_MODE_ENABLED1 |
                (0xffff << VIVS_RS_CLEAR_CONTROL_BITS__SHIFT));
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 0);
        /*    clear color ts */
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, etna_bo_gpu_address(rt_ts));
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, 0x40);
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE,
                ((rt_ts_size/0x40) << VIVS_RS_WINDOW_SIZE_HEIGHT__SHIFT) |
                (16 << VIVS_RS_WINDOW_SIZE_WIDTH__SHIFT));
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);
        /*    clear depth ts */
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, etna_bo_gpu_address(z_ts));
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, 0x40);
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE,
                ((z_ts_size/0x40) << VIVS_RS_WINDOW_SIZE_HEIGHT__SHIFT) |
                (16 << VIVS_RS_WINDOW_SIZE_WIDTH__SHIFT));
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);
        /** Done */
       
        etna_set_state(ctx, VIVS_TS_COLOR_CLEAR_VALUE, 0xff7f7f7f);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);

        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, etna_bo_gpu_address(rt_ts));
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, etna_bo_gpu_address(rt));
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);

        /* depth setup */
        etna_set_state(ctx, VIVS_PE_DEPTH_CONFIG, ETNA_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_WRITE_ENABLE, 1) &
                                                  ETNA_MASKED(VIVS_PE_DEPTH_CONFIG_DEPTH_FUNC, COMPARE_FUNC_LESS) &
                                                  ETNA_MASKED_INL(VIVS_PE_DEPTH_CONFIG_DEPTH_MODE, Z) &
                                                  ETNA_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_ONLY_DEPTH, 0));
        etna_set_state_f32(ctx, VIVS_PE_DEPTH_NEAR, 0.0);
        etna_set_state_f32(ctx, VIVS_PE_DEPTH_FAR, 1.0);
        etna_set_state_f32(ctx, VIVS_PE_DEPTH_NORMALIZE, 65535.0);

        /* set up primitive assembly and setup engine */
        etna_set_state_f32(ctx, VIVS_PA_VIEWPORT_OFFSET_Z, 0.0);
        etna_set_state_f32(ctx, VIVS_PA_VIEWPORT_SCALE_Z, 1.0);
        etna_set_state_fixp(ctx, VIVS_PA_VIEWPORT_OFFSET_X, width << 15);
        etna_set_state_fixp(ctx, VIVS_PA_VIEWPORT_OFFSET_Y, height << 15);
        etna_set_state_fixp(ctx, VIVS_PA_VIEWPORT_SCALE_X, width << 15);
        etna_set_state_fixp(ctx, VIVS_PA_VIEWPORT_SCALE_Y, height << 15);
        etna_set_state_fixp(ctx, VIVS_SE_SCISSOR_LEFT, 0);
        etna_set_state_fixp(ctx, VIVS_SE_SCISSOR_TOP, 0);
        etna_set_state_fixp(ctx, VIVS_SE_SCISSOR_RIGHT, (width << 16) | 5);
        etna_set_state_fixp(ctx, VIVS_SE_SCISSOR_BOTTOM, (height << 16) | 5);

        /* set up texture unit */
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_TEXTURE);
        etna_set_state(ctx, VIVS_TE_SAMPLER_SIZE(0),
                VIVS_TE_SAMPLER_SIZE_WIDTH(512)|VIVS_TE_SAMPLER_SIZE_HEIGHT(512));
        etna_set_state(ctx, VIVS_TE_SAMPLER_LOG_SIZE(0),
                VIVS_TE_SAMPLER_LOG_SIZE_WIDTH(9<<5) |
                VIVS_TE_SAMPLER_LOG_SIZE_HEIGHT(9<<5));
        etna_set_state(ctx, VIVS_TE_SAMPLER_LOD_ADDR(0,0), etna_bo_gpu_address(tex));
        etna_set_state(ctx, VIVS_TE_SAMPLER_CONFIG0(0),
                VIVS_TE_SAMPLER_CONFIG0_TYPE(TEXTURE_TYPE_2D)|
                VIVS_TE_SAMPLER_CONFIG0_UWRAP(TEXTURE_WRAPMODE_CLAMP_TO_EDGE)|
                VIVS_TE_SAMPLER_CONFIG0_VWRAP(TEXTURE_WRAPMODE_CLAMP_TO_EDGE)|
                VIVS_TE_SAMPLER_CONFIG0_MIN(TEXTURE_FILTER_LINEAR)|
                VIVS_TE_SAMPLER_CONFIG0_MIP(TEXTURE_FILTER_NONE)|
                VIVS_TE_SAMPLER_CONFIG0_MAG(TEXTURE_FILTER_LINEAR)|
                VIVS_TE_SAMPLER_CONFIG0_FORMAT(TEXTURE_FORMAT_X8R8G8B8));
        etna_set_state(ctx, VIVS_TE_SAMPLER_LOD_CONFIG(0), 0x00000000); /*   TE.SAMPLER[0].LOD_CONFIG := 0x0 */

        /* shader setup */
        etna_set_state(ctx, VIVS_VS_END_PC, vs_size/16);
        etna_set_state_multi(ctx, VIVS_VS_INPUT_COUNT, 3, (uint32_t[]){
                /* VIVS_VS_INPUT_COUNT */ VIVS_VS_INPUT_COUNT_UNK8(1) | VIVS_VS_INPUT_COUNT_COUNT(3),
                /* VIVS_VS_TEMP_REGISTER_CONTROL */ VIVS_VS_TEMP_REGISTER_CONTROL_NUM_TEMPS(6),
                /* VIVS_VS_OUTPUT(0) */ 0x10004});
        etna_set_state(ctx, VIVS_VS_START_PC, 0x0);
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(45), 0.5); /* u11.y */
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(44), 1.0); /* u11.x */
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(27), 0.0); /* u6.w */
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(23), 20.0); /* u5.w */
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(19), 2.0); /* u4.w */

        /* Now load the shader itself */
        etna_set_state_multi(ctx, VIVS_VS_INST_MEM(0), vs_size/4, vs);
        etna_set_state(ctx, VIVS_RA_CONTROL, 0x3);
        etna_set_state_f32(ctx, VIVS_PS_UNIFORMS(0), 1.0); /* u0.x */
        etna_set_state_multi(ctx, VIVS_PS_END_PC, 2, (uint32_t[]){
                /* VIVS_PS_END_PC */ ps_size/16,
                /* VIVS_PS_OUTPUT_REG */ 0x1});
        etna_set_state(ctx, VIVS_PS_START_PC, 0x0);
        etna_set_state(ctx, VIVS_PA_ATTRIBUTE_ELEMENT_COUNT, 0x200);
        etna_set_state(ctx, VIVS_PA_SHADER_ATTRIBUTES(0), 0x200);
        etna_set_state(ctx, VIVS_PA_SHADER_ATTRIBUTES(1), 0x200);
        etna_set_state(ctx, VIVS_GL_VARYING_NUM_COMPONENTS, 
                VIVS_GL_VARYING_NUM_COMPONENTS_VAR0(4)| /* position */
                VIVS_GL_VARYING_NUM_COMPONENTS_VAR1(2)  /* texture coordinate */
                );
        etna_set_state_multi(ctx, VIVS_GL_VARYING_COMPONENT_USE(0), 2, (uint32_t[]){
                VIVS_GL_VARYING_COMPONENT_USE_COMP0(VARYING_COMPONENT_USE_USED) |
                VIVS_GL_VARYING_COMPONENT_USE_COMP1(VARYING_COMPONENT_USE_USED) |
                VIVS_GL_VARYING_COMPONENT_USE_COMP2(VARYING_COMPONENT_USE_USED) |
                VIVS_GL_VARYING_COMPONENT_USE_COMP3(VARYING_COMPONENT_USE_USED) |
                VIVS_GL_VARYING_COMPONENT_USE_COMP4(VARYING_COMPONENT_USE_USED) |
                VIVS_GL_VARYING_COMPONENT_USE_COMP5(VARYING_COMPONENT_USE_USED)
                , 0
                });
        etna_set_state_multi(ctx, VIVS_PS_INST_MEM(0), ps_size/4, ps);
        etna_set_state(ctx, VIVS_PS_INPUT_COUNT,
                VIVS_PS_INPUT_COUNT_UNK8(31)| VIVS_PS_INPUT_COUNT_COUNT(3 + extra_ps_inputs));
        etna_set_state(ctx, VIVS_PS_TEMP_REGISTER_CONTROL,
                VIVS_PS_TEMP_REGISTER_CONTROL_NUM_TEMPS(3 + extra_ps_inputs));
        etna_set_state(ctx, VIVS_PS_CONTROL, VIVS_PS_CONTROL_UNK1);
        etna_set_state(ctx, VIVS_GL_VARYING_TOTAL_COMPONENTS,
                VIVS_GL_VARYING_TOTAL_COMPONENTS_NUM(6)); /* 4+2=6 total varying components */
        etna_set_state(ctx, VIVS_VS_LOAD_BALANCING, 0xf3f0542); /* depends on number of inputs/outputs/varyings? XXX how exactly */
        etna_set_state(ctx, VIVS_VS_OUTPUT_COUNT, 3);
        etna_set_state(ctx, VIVS_PA_CONFIG, ETNA_MASKED_BIT(VIVS_PA_CONFIG_POINT_SIZE_ENABLE, 0));

        /*   Compute transform matrices in the same way as cube egl demo */
        ESMatrix modelview;
        esMatrixLoadIdentity(&modelview);
        esTranslate(&modelview, 0.0f, 0.0f, -9.0f);
        esRotate(&modelview, 45.0f, 1.0f, 0.0f, 0.0f);
        esRotate(&modelview, 45.0f, 0.0f, 1.0f, 0.0f);
        esRotate(&modelview, frame*0.5f, 0.0f, 0.0f, 1.0f);
	esScale(&modelview, 0.475f, 0.475f, 0.475f);
        float aspect = (float)(height) / (float)(width);
        ESMatrix projection;
        esMatrixLoadIdentity(&projection);
        esFrustum(&projection, -2.8f, +2.8f, -2.8f * aspect, +2.8f * aspect, 6.0f, 10.0f);
        ESMatrix modelviewprojection;
        esMatrixLoadIdentity(&modelviewprojection);
        esMatrixMultiply(&modelviewprojection, &modelview, &projection);
        ESMatrix inverse, normal; /* compute inverse transpose normal transformation matrix */
        esMatrixInverse3x3(&inverse, &modelview);
        esMatrixTranspose(&normal, &inverse);
       
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(0), 16, (uint32_t*)&modelviewprojection.m[0][0]);
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(16), 3, (uint32_t*)&normal.m[0][0]); /* u4.xyz */
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(20), 3, (uint32_t*)&normal.m[1][0]); /* u5.xyz */
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(24), 3, (uint32_t*)&normal.m[2][0]); /* u6.xyz */
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(28), 16, (uint32_t*)&modelview.m[0][0]);
#ifdef INDEXED
        etna_set_state(ctx, VIVS_FE_INDEX_STREAM_BASE_ADDR, etna_bo_gpu_address(idx));
        etna_set_state(ctx, VIVS_FE_INDEX_STREAM_CONTROL, VIVS_FE_INDEX_STREAM_CONTROL_TYPE_UNSIGNED_SHORT);
#endif
        etna_set_state(ctx, VIVS_FE_VERTEX_STREAM_BASE_ADDR, etna_bo_gpu_address(vtx));
        etna_set_state(ctx, VIVS_FE_VERTEX_STREAM_CONTROL,
                FE_VERTEX_STREAM_CONTROL_VERTEX_STRIDE((3 + 3 + 2)*4));
        etna_set_state(ctx, VIVS_FE_VERTEX_ELEMENT_CONFIG(0),
                VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_FLOAT |
                (ENDIAN_MODE_NO_SWAP << VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN__SHIFT) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM(0) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM(3) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_START(0x0) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_END(0xc));
        etna_set_state(ctx, VIVS_FE_VERTEX_ELEMENT_CONFIG(1),
                VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_FLOAT |
                (ENDIAN_MODE_NO_SWAP << VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN__SHIFT) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM(0) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM(3) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_START(0xc) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_END(0x18));
        etna_set_state(ctx, VIVS_FE_VERTEX_ELEMENT_CONFIG(2),
                VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_FLOAT |
                (ENDIAN_MODE_NO_SWAP << VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN__SHIFT) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NONCONSECUTIVE |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM(0) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM(2) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_START(0x18) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_END(0x20));
        etna_set_state(ctx, VIVS_VS_INPUT(0), 0x20100);
        etna_set_state(ctx, VIVS_PA_CONFIG, ETNA_MASKED_BIT(VIVS_PA_CONFIG_POINT_SPRITE_ENABLE, 0));

#ifdef INDEXED
        etna_draw_indexed_primitives(ctx, PRIMITIVE_TYPE_TRIANGLES, 0, COMPANION_TRIANGLE_COUNT, 0);
#else
        etna_draw_primitives(ctx, PRIMITIVE_TYPE_TRIANGLES, 0, COMPANION_TRIANGLE_COUNT);
#endif
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
#ifdef EXTRA_DELAYS
        etna_flush(ctx, NULL);
#endif
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_set_state(ctx, VIVS_RS_CONFIG,
                VIVS_RS_CONFIG_SOURCE_FORMAT(RS_FORMAT_A8R8G8B8) |
                VIVS_RS_CONFIG_SOURCE_TILED |
                VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_A8R8G8B8) |
                VIVS_RS_CONFIG_DEST_TILED);
        etna_set_state(ctx, VIVS_RS_SOURCE_STRIDE, (padded_width * 4 * 4) | (supertiled?VIVS_RS_SOURCE_STRIDE_TILING:0));
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, (padded_width * 4 * 4) | (supertiled?VIVS_RS_SOURCE_STRIDE_TILING:0));
        etna_set_state(ctx, VIVS_RS_DITHER(0), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_DITHER(1), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 0);
        etna_set_state(ctx, VIVS_RS_SOURCE_ADDR, etna_bo_gpu_address(rt));
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, etna_bo_gpu_address(rt));
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE,
                VIVS_RS_WINDOW_SIZE_HEIGHT(padded_height) |
                VIVS_RS_WINDOW_SIZE_WIDTH(padded_width));
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);
#ifdef EXTRA_DELAYS
        etna_flush(ctx, NULL);

        etna_warm_up_rs(ctx, etna_bo_gpu_address(aux_rt), etna_bo_gpu_address(aux_rt_ts));
#endif

        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, etna_bo_gpu_address(rt_ts));
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, etna_bo_gpu_address(rt));
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG,
                VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
                VIVS_TS_MEM_CONFIG_DEPTH_16BPP |
                VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);

        /* Wait for pixel engine to finish */
#ifdef EXTRA_DELAYS
        etna_finish(ctx);
#else
        etna_stall(ctx, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);
#endif

        /* Copy result to framebuffer */
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_set_state(ctx, VIVS_RS_CONFIG,
                VIVS_RS_CONFIG_SOURCE_FORMAT(RS_FORMAT_A8R8G8B8) |
                VIVS_RS_CONFIG_SOURCE_TILED |
                ((supersample_x>1)?VIVS_RS_CONFIG_DOWNSAMPLE_X:0) |
                ((supersample_y>1)?VIVS_RS_CONFIG_DOWNSAMPLE_Y:0) |
                VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_A8R8G8B8) |
                VIVS_RS_CONFIG_SWAP_RB);
        etna_set_state(ctx, VIVS_RS_SOURCE_STRIDE, (padded_width * 4 * 4)  | (supertiled?VIVS_RS_SOURCE_STRIDE_TILING:0));
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, fbs->fb.fb_fix.line_length);
        etna_set_state(ctx, VIVS_RS_DITHER(0), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_DITHER(1), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 0);
        etna_set_state(ctx, VIVS_RS_SOURCE_ADDR, etna_bo_gpu_address(rt));
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, etna_bo_gpu_address(fbs->fb.buffer[backbuffer]));
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE,
                VIVS_RS_WINDOW_SIZE_HEIGHT(height * supersample_y) |
                VIVS_RS_WINDOW_SIZE_WIDTH(width * supersample_x));
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);
        etna_finish(ctx);

        /* switch buffers */
        fb_set_buffer(&fbs->fb, backbuffer);
        backbuffer = 1-backbuffer;
    }

    etna_free(ctx);
    viv_close(conn);
    return 0;
}
