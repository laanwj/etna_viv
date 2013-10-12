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
/* Experimentation with non-supertiled, 4-bit-per-tile rendering on cubox.
 * This succesfully renders the cube on the dove gc600!
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

#include <errno.h>

#include <etnaviv/common.xml.h>
#include <etnaviv/state.xml.h>
#include <etnaviv/state_3d.xml.h>
#include <etnaviv/cmdstream.xml.h>
#include <etnaviv/viv.h>
#include <etnaviv/etna.h>
#include <etnaviv/etna_bo.h>
#include <etnaviv/etna_util.h>
#include <etnaviv/etna_rs.h>

#include "write_bmp.h"

#include "esTransform.h"

//INTERLEAVED - mix vertices, colors, and normals
#define INTERLEAVED

#define VERTEX_BUFFER_SIZE 0x100000

float vVertices[] = {
  // front
  -1.0f, -1.0f, +1.0f, // point blue
  +1.0f, -1.0f, +1.0f, // point magenta
  -1.0f, +1.0f, +1.0f, // point cyan
  +1.0f, +1.0f, +1.0f, // point white
  // back
  +1.0f, -1.0f, -1.0f, // point red
  -1.0f, -1.0f, -1.0f, // point black
  +1.0f, +1.0f, -1.0f, // point yellow
  -1.0f, +1.0f, -1.0f, // point green
  // right
  +1.0f, -1.0f, +1.0f, // point magenta
  +1.0f, -1.0f, -1.0f, // point red
  +1.0f, +1.0f, +1.0f, // point white
  +1.0f, +1.0f, -1.0f, // point yellow
  // left
  -1.0f, -1.0f, -1.0f, // point black
  -1.0f, -1.0f, +1.0f, // point blue
  -1.0f, +1.0f, -1.0f, // point green
  -1.0f, +1.0f, +1.0f, // point cyan
  // top
  -1.0f, +1.0f, +1.0f, // point cyan
  +1.0f, +1.0f, +1.0f, // point white
  -1.0f, +1.0f, -1.0f, // point green
  +1.0f, +1.0f, -1.0f, // point yellow
  // bottom
  -1.0f, -1.0f, -1.0f, // point black
  +1.0f, -1.0f, -1.0f, // point red
  -1.0f, -1.0f, +1.0f, // point blue
  +1.0f, -1.0f, +1.0f  // point magenta
};

float vColors[] = {
  // front
  0.0f,  0.0f,  1.0f, // blue
  1.0f,  0.0f,  1.0f, // magenta
  0.0f,  1.0f,  1.0f, // cyan
  1.0f,  1.0f,  1.0f, // white
  // back
  1.0f,  0.0f,  0.0f, // red
  0.0f,  0.0f,  0.0f, // black
  1.0f,  1.0f,  0.0f, // yellow
  0.0f,  1.0f,  0.0f, // green
  // right
  1.0f,  0.0f,  1.0f, // magenta
  1.0f,  0.0f,  0.0f, // red
  1.0f,  1.0f,  1.0f, // white
  1.0f,  1.0f,  0.0f, // yellow
  // left
  0.0f,  0.0f,  0.0f, // black
  0.0f,  0.0f,  1.0f, // blue
  0.0f,  1.0f,  0.0f, // green
  0.0f,  1.0f,  1.0f, // cyan
  // top
  0.0f,  1.0f,  1.0f, // cyan
  1.0f,  1.0f,  1.0f, // white
  0.0f,  1.0f,  0.0f, // green
  1.0f,  1.0f,  0.0f, // yellow
  // bottom
  0.0f,  0.0f,  0.0f, // black
  1.0f,  0.0f,  0.0f, // red
  0.0f,  0.0f,  1.0f, // blue
  1.0f,  0.0f,  1.0f  // magenta
};

float vNormals[] = {
  // front
  +0.0f, +0.0f, +1.0f, // forward
  +0.0f, +0.0f, +1.0f, // forward
  +0.0f, +0.0f, +1.0f, // forward
  +0.0f, +0.0f, +1.0f, // forward
  // back
  +0.0f, +0.0f, -1.0f, // backbard
  +0.0f, +0.0f, -1.0f, // backbard
  +0.0f, +0.0f, -1.0f, // backbard
  +0.0f, +0.0f, -1.0f, // backbard
  // right
  +1.0f, +0.0f, +0.0f, // right
  +1.0f, +0.0f, +0.0f, // right
  +1.0f, +0.0f, +0.0f, // right
  +1.0f, +0.0f, +0.0f, // right
  // left
  -1.0f, +0.0f, +0.0f, // left
  -1.0f, +0.0f, +0.0f, // left
  -1.0f, +0.0f, +0.0f, // left
  -1.0f, +0.0f, +0.0f, // left
  // top
  +0.0f, +1.0f, +0.0f, // up
  +0.0f, +1.0f, +0.0f, // up
  +0.0f, +1.0f, +0.0f, // up
  +0.0f, +1.0f, +0.0f, // up
  // bottom
  +0.0f, -1.0f, +0.0f, // down
  +0.0f, -1.0f, +0.0f, // down
  +0.0f, -1.0f, +0.0f, // down
  +0.0f, -1.0f, +0.0f  // down
};
#define COMPONENTS_PER_VERTEX (3)
#define NUM_VERTICES (6*4)
#define VERTICES_PER_DRAW 4
#define DRAW_COUNT 6

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
    0x0080108f, 0x3fc06800, 0x00000050, 0x203fc068,
    0x03801003, 0x00000800, 0x01480140, 0x00000000,
    0x04001009, 0x00000000, 0x00000000, 0x200000b8,
};
uint32_t ps[] = {
    0x00000000, 0x00000000, 0x00000000, 0x00000000
};
size_t vs_size = sizeof(vs);
size_t ps_size = sizeof(ps);

int main(int argc, char **argv)
{
    int rv;
    int width = 400;
    int height = 240;
    int padded_width = etna_align_up(width, 64);
    int padded_height = etna_align_up(height, 64);
    
    printf("padded_width %i padded_height %i\n", padded_width, padded_height);
    struct viv_conn *conn = 0;
    rv = viv_open(VIV_HW_3D, &conn);
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    printf("Succesfully opened device\n");
    
    bool supertiled = VIV_FEATURE(conn, chipMinorFeatures0,SUPER_TILED);
    unsigned bits_per_tile = VIV_FEATURE(conn, chipMinorFeatures0,2BITPERTILE)?2:4;

    printf("Supertile: %i, bits per tile: %i\n", supertiled, bits_per_tile);

    struct etna_bo *rt = 0; /* main render target */
    struct etna_bo *rt_ts = 0; /* tile status for main render target */
    struct etna_bo *z = 0; /* depth for main render target */
    struct etna_bo *z_ts = 0; /* depth ts for main render target */
    struct etna_bo *vtx = 0; /* vertex buffer */
    struct etna_bo *aux_rt = 0; /* auxilary render target */
    struct etna_bo *bmp = 0; /* bitmap */

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
       (aux_rt=etna_bo_new(conn, rt_size, DRM_ETNA_GEM_TYPE_BMP))==NULL ||
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
#ifdef INTERLEAVED
    for(int vert=0; vert<NUM_VERTICES; ++vert)
    {
        int src_idx = vert * COMPONENTS_PER_VERTEX;
        int dest_idx = vert * COMPONENTS_PER_VERTEX * 3;
        for(int comp=0; comp<COMPONENTS_PER_VERTEX; ++comp)
        {
            ((float*)etna_bo_map(vtx))[dest_idx+comp+0] = vVertices[src_idx + comp]; /* 0 */
            ((float*)etna_bo_map(vtx))[dest_idx+comp+3] = vNormals[src_idx + comp]; /* 1 */
            ((float*)etna_bo_map(vtx))[dest_idx+comp+6] = vColors[src_idx + comp]; /* 2 */
        }
    }
#else
    int dest_idx = 0;
    int v_src_idx = 0;
    int n_src_idx = 0;
    int c_src_idx = 0;
    for(int jj=0; jj<DRAW_COUNT; jj++)
    {
        for(int vert=0; vert<VERTICES_PER_DRAW*3; ++vert)
        {
            ((float*)etna_bo_map(vtx))[dest_idx] = vVertices[v_src_idx];
            dest_idx++;
            v_src_idx++;
        }
        for(int vert=0; vert<VERTICES_PER_DRAW*3; ++vert)
        {
            ((float*)etna_bo_map(vtx))[dest_idx] = vNormals[n_src_idx];
            dest_idx++;
            n_src_idx++;
        }
        for(int vert=0; vert<VERTICES_PER_DRAW*3; ++vert)
        {
            ((float*)etna_bo_map(vtx))[dest_idx] = vColors[c_src_idx];
            dest_idx++;
            c_src_idx++;
        }
    }
#endif //INTERLEAVED

    struct etna_ctx *ctx = 0;
    if(etna_create(conn, &ctx) != ETNA_OK)
    {
        printf("Unable to create context\n");
        exit(1);
    }

    /* XXX how important is the ordering? I suppose we could group states (except the flushes, kickers, semaphores etc)
     * and simply submit them at once. Especially for consecutive states and masked stated this could be a big win
     * in DMA command buffer size. */
    for(int frame=0; frame<1; ++frame)
    {
        printf("*** FRAME %i ****\n", frame);
        /* XXX part of this can be put outside the loop, but until we have usable context management
         * this is safest.
         */

        etna_set_state(ctx, VIVS_GL_VERTEX_ELEMENT_CONFIG, 0x1);
        etna_set_state(ctx, VIVS_RA_CONTROL, 0x1);
        
        etna_set_state(ctx, VIVS_PA_W_CLIP_LIMIT, 0x34000001);
        etna_set_state(ctx, VIVS_PA_SYSTEM_MODE, 0x11);    
        etna_set_state(ctx, VIVS_GL_API_MODE, VIVS_GL_API_MODE_OPENGL);

        etna_set_state(ctx, VIVS_SE_CONFIG, 0x0); //LAST_PIXEL_ENABLE=0
        etna_set_state(ctx, VIVS_SE_DEPTH_SCALE, 0x0);
        etna_set_state(ctx, VIVS_SE_DEPTH_BIAS, 0x0);

        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
        //etna_set_state(ctx, VIVS_DUMMY_DUMMY, 0);
        etna_set_state(ctx, VIVS_TS_DEPTH_AUTO_DISABLE_COUNT, 0x1cc0);
        etna_set_state_multi(ctx, VIVS_TS_COLOR_STATUS_BASE, 3,
                       (uint32_t[]){etna_bo_gpu_address(rt_ts), etna_bo_gpu_address(rt), 0});
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG,
                       VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR |
                       VIVS_TS_MEM_CONFIG_COLOR_AUTO_DISABLE);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_DEPTH);
        //etna_set_state(ctx, VIVS_DUMMY_DUMMY, 0);
        etna_set_state_multi(ctx, VIVS_TS_DEPTH_STATUS_BASE, 3,
                       (uint32_t[]){etna_bo_gpu_address(z_ts), etna_bo_gpu_address(z), 0xffffffff});
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG,
                       VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
                       VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR |
                       VIVS_TS_MEM_CONFIG_DEPTH_16BPP |
                       VIVS_TS_MEM_CONFIG_COLOR_AUTO_DISABLE |
                       VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION);
        etna_set_state(ctx, VIVS_SE_DEPTH_SCALE, 0);
        etna_set_state(ctx, VIVS_SE_DEPTH_BIAS, 0);
        
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_DEPTH | VIVS_GL_FLUSH_CACHE_COLOR);
        etna_set_state(ctx, VIVS_TS_FLUSH_CACHE, VIVS_TS_FLUSH_CACHE_FLUSH);

        
        /* semaphore time */
        etna_semaphore(ctx, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);
        etna_stall(ctx, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);
        
        /* Set up the resolve to clear tile status for main render target 
         * Regard the TS as an image of width 16 with 4 bytes per pixel (64 bytes per row)
         * XXX need to clear the depth ts too.
         * */
        etna_set_state(ctx, VIVS_RS_CONFIG,
                       (RS_FORMAT_A8R8G8B8 << VIVS_RS_CONFIG_SOURCE_FORMAT__SHIFT) |
                       (RS_FORMAT_A8R8G8B8 << VIVS_RS_CONFIG_DEST_FORMAT__SHIFT));
        etna_set_state_multi(ctx, VIVS_RS_DITHER(0), 2, (uint32_t[]){0xffffffff, 0xffffffff});
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, etna_bo_gpu_address(rt_ts)); /* ADDR_B */
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, 0x200);
        etna_set_state(ctx, VIVS_RS_FILL_VALUE(0), 0x55555555);
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, 
                       VIVS_RS_CLEAR_CONTROL_MODE_ENABLED1 |
                       (0xffff << VIVS_RS_CLEAR_CONTROL_BITS__SHIFT));
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 
                       0); /* no AA, no endian switch */
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE, 
                       ((rt_ts_size/0x200) << VIVS_RS_WINDOW_SIZE_HEIGHT__SHIFT) |
                       (128 << VIVS_RS_WINDOW_SIZE_WIDTH__SHIFT));
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbadabeeb);
        
        etna_set_state(ctx, VIVS_TS_COLOR_CLEAR_VALUE, 0xff7f7f7f);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
        //etna_set_state(ctx, VIVS_DUMMY_DUMMY, 0);
        etna_set_state(ctx, VIVS_TS_DEPTH_AUTO_DISABLE_COUNT, 0x1cc0);
        etna_set_state_multi(ctx, VIVS_TS_COLOR_STATUS_BASE, 3,
                       (uint32_t[]){etna_bo_gpu_address(rt_ts), etna_bo_gpu_address(rt), 0xff7f7f7f});
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG,
                       VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
                       VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR |
                       VIVS_TS_MEM_CONFIG_DEPTH_16BPP |
                       VIVS_TS_MEM_CONFIG_COLOR_AUTO_DISABLE |
                       VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION);
        
        /*   Compute transform matrices in the same way as cube egl demo */ 
        ESMatrix modelview;
        esMatrixLoadIdentity(&modelview);
        esTranslate(&modelview, 0.0f, 0.0f, -8.0f);
        esRotate(&modelview, 45.0f, 1.0f, 0.0f, 0.0f);
        esRotate(&modelview, 45.0f, 0.0f, 1.0f, 0.0f);
        esRotate(&modelview, frame*0.5f, 0.0f, 0.0f, 1.0f);

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

        etna_set_state_fixp_multi(ctx, VIVS_PA_VIEWPORT_SCALE_X, 2, (uint32_t[]){width << 15, height << 15});
        etna_set_state_fixp_multi(ctx, VIVS_PA_VIEWPORT_OFFSET_X, 2, (uint32_t[]){width << 15, height << 15});
        etna_set_state/*_f32*/(ctx, VIVS_PA_VIEWPORT_UNK00A80, 0x37c81905); //0.000024f but loads 37c9539c not 37c81905
        etna_set_state_fixp(ctx, VIVS_PA_VIEWPORT_UNK00A84, 8192<<16);
        etna_set_state/*_f32*/(ctx, VIVS_PA_VIEWPORT_UNK00A8C, 0x38000000); //0.000031f but load 380205ff not 38000000
        etna_set_state_fixp_multi(ctx, VIVS_SE_SCISSOR_LEFT, 4, (uint32_t[]){0, 0, (width << 16)|0x1000, (height << 16)|0x1000});//0x1000=1/16
        etna_set_state_fixp(ctx, VIVS_SE_CLIP_RIGHT, (width << 16)|0xffff);
        etna_set_state_fixp(ctx, VIVS_SE_CLIP_BOTTOM, (height << 16)|0xffff);
        etna_set_state_multi(ctx, VIVS_PE_ALPHA_OP, 3, (uint32_t[]){0, 0,
                       VIVS_PE_ALPHA_CONFIG_SRC_FUNC_COLOR(1) |
                       VIVS_PE_ALPHA_CONFIG_SRC_FUNC_ALPHA(1)});
        etna_set_state(ctx, VIVS_PE_STENCIL_CONFIG_EXT, 0x0000fdff);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
        etna_set_state(ctx, VIVS_PE_COLOR_FORMAT, RS_FORMAT_A8R8G8B8 |
        VIVS_PE_COLOR_FORMAT_COMPONENTS(0xf) |
        VIVS_PE_COLOR_FORMAT_OVERWRITE |
        VIVS_PE_COLOR_FORMAT_SUPER_TILED);
        etna_set_state(ctx, VIVS_PE_PIPE_COLOR_ADDR(0), etna_bo_gpu_address(rt));
        etna_set_state(ctx, VIVS_PE_COLOR_ADDR, etna_bo_gpu_address(rt));
        etna_set_state(ctx, VIVS_PE_COLOR_STRIDE, padded_width * 4);
        etna_set_state_multi(ctx, VIVS_PE_DEPTH_CONFIG, 4, (uint32_t[]){
                       VIVS_PE_DEPTH_CONFIG_DEPTH_MODE_Z |
                       VIVS_PE_DEPTH_CONFIG_DEPTH_FUNC(COMPARE_FUNC_ALWAYS) |
                       VIVS_PE_DEPTH_CONFIG_EARLY_Z |
                       VIVS_PE_DEPTH_CONFIG_SUPER_TILED |
                       VIVS_PE_DEPTH_CONFIG_DISABLE_ZS,
                       0x00000000/*0.0*/, 0x3f800000/*1.0*/, 0x477fff00/*65535.0*/});
        etna_set_state(ctx, VIVS_PE_PIPE_DEPTH_ADDR(0), etna_bo_gpu_address(z));
        etna_set_state(ctx, VIVS_PE_DEPTH_ADDR, etna_bo_gpu_address(z));
        etna_set_state(ctx, VIVS_PE_DEPTH_STRIDE, padded_width * 2);
        etna_set_state(ctx, VIVS_PE_HDEPTH_CONTROL, 0);
        etna_set_state_f32(ctx, VIVS_PA_VIEWPORT_SCALE_Z, 1.0);
        etna_set_state_f32(ctx, VIVS_PA_VIEWPORT_OFFSET_Z, 0.0);
        etna_set_state(ctx, VIVS_PE_STENCIL_CONFIG, 0);
        etna_set_state(ctx, VIVS_GL_MULTI_SAMPLE_CONFIG, 0);
        etna_set_state_f32(ctx, VIVS_PA_LINE_WIDTH, 0.5);
        etna_set_state_multi(ctx, VIVS_PA_CONFIG, 3, (uint32_t[]){
                       VIVS_PA_CONFIG_CULL_FACE_MODE_CCW |
                       VIVS_PA_CONFIG_FILL_MODE_SOLID |
                       VIVS_PA_CONFIG_SHADE_MODEL_SMOOTH |
                       VIVS_PA_CONFIG_UNK22, 
                       0x3f000000/*0.5*/, 0x3f000000/*0.5*/
        });
        etna_semaphore(ctx, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);
        etna_stall(ctx, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);
        
        /* shader setup */
        etna_set_state_multi(ctx, VIVS_VS_INPUT_COUNT, 3, (uint32_t[]){
                       VIVS_VS_INPUT_COUNT_COUNT(3) | VIVS_VS_INPUT_COUNT_UNK8(1),
                       VIVS_VS_TEMP_REGISTER_CONTROL_NUM_TEMPS(6),
                       VIVS_VS_OUTPUT_O0(4) | VIVS_VS_OUTPUT_O1(0) |
                       VIVS_VS_OUTPUT_O2(0) | VIVS_VS_OUTPUT_O3(0)});

        int vertexProgStart = 0; //0C000 + vertexProgStart*16
        etna_set_state(ctx, VIVS_VS_RANGE, VIVS_VS_RANGE_LOW(vertexProgStart) | VIVS_VS_RANGE_HIGH(vertexProgStart + 0x15));
        etna_set_state(ctx, VIVS_VS_START_PC, 0);
        etna_set_state(ctx, VIVS_VS_END_PC, 0x16);
        
        //vec4 lightSource = vec4(2.0, 2.0, 20.0, 0.0);
        //VS.UNIFORMS[14] = 2.0, VS.UNIFORMS[19]=2.0, VS.UNIFORMS[23]=20.0 VS.UNIFORMS[27]=0.0 u[3-6].w
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(44), 1.0);
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(27), 0.0);
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(23), 20.0);
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(19), 2.0);
        
        etna_set_state_multi(ctx, VIVS_SH_INST_MEM(vertexProgStart*4), sizeof(vs)/4, vs);
        etna_set_state(ctx, VIVS_RA_CONTROL, 0x1);
        etna_set_state(ctx, VIVS_PS_OUTPUT_REG, 0x1);
        etna_set_state(ctx, VIVS_PA_SHADER_ATTRIBUTES(0), 0x200);
        etna_set_state(ctx, VIVS_GL_VARYING_NUM_COMPONENTS,  /* one varying, with four components */
                       (4 << VIVS_GL_VARYING_NUM_COMPONENTS_VAR0__SHIFT)
        );
        etna_set_state(ctx, VIVS_GL_UNK03834, 0x40000);
        etna_set_state_multi(ctx, VIVS_GL_VARYING_COMPONENT_USE(0), 2, (uint32_t[]){
                       VIVS_GL_VARYING_COMPONENT_USE_COMP0(1) |
                       VIVS_GL_VARYING_COMPONENT_USE_COMP1(1) |
                       VIVS_GL_VARYING_COMPONENT_USE_COMP2(1) |
                       VIVS_GL_VARYING_COMPONENT_USE_COMP3(1),
                       0});
        int pixelProgStart = 0x100;
        etna_set_state(ctx, VIVS_GL_UNK03838, 0x0);
        etna_set_state(ctx, VIVS_PS_RANGE, VIVS_PS_RANGE_LOW(pixelProgStart) | VIVS_PS_RANGE_HIGH(pixelProgStart));
        etna_set_state(ctx, VIVS_PS_START_PC, 0x0);
        etna_set_state(ctx, VIVS_PS_END_PC, 0x1);
        
        
        etna_set_state_multi(ctx, VIVS_SH_INST_MEM(pixelProgStart*4), sizeof(ps)/4, ps);
        etna_set_state_multi(ctx, VIVS_PS_INPUT_COUNT, 3, (uint32_t[]){
                       (31<<8)|2,
                       (2 << VIVS_PS_TEMP_REGISTER_CONTROL_NUM_TEMPS__SHIFT),
                       VIVS_PS_CONTROL_UNK1});
        
        etna_set_state(ctx, VIVS_PA_ATTRIBUTE_ELEMENT_COUNT, 0x100);
        etna_set_state(ctx, VIVS_GL_VARYING_TOTAL_COMPONENTS,  /* one varying, with four components */
                       (4 << VIVS_GL_VARYING_TOTAL_COMPONENTS_NUM__SHIFT)
        );
        etna_set_state(ctx, VIVS_VS_LOAD_BALANCING, 0xf3f0582);
        etna_set_state(ctx, VIVS_VS_OUTPUT_COUNT, 2);
        
        etna_semaphore(ctx, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);
        etna_stall(ctx, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);
#ifdef INTERLEAVED
        unsigned fe_vert_elem_conf_base = 
        VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_FLOAT |
        (ENDIAN_MODE_NO_SWAP << VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN__SHIFT) |
        (0 << VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM__SHIFT) |
        (3 << VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM__SHIFT) |
        VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF;
        etna_set_state_multi(ctx, VIVS_FE_VERTEX_ELEMENT_CONFIG(0), 3, (uint32_t[]){
                       fe_vert_elem_conf_base | (0x0 << VIVS_FE_VERTEX_ELEMENT_CONFIG_START__SHIFT) | (0xc << VIVS_FE_VERTEX_ELEMENT_CONFIG_END__SHIFT),
                       fe_vert_elem_conf_base | (0xc << VIVS_FE_VERTEX_ELEMENT_CONFIG_START__SHIFT) | (0x18 << VIVS_FE_VERTEX_ELEMENT_CONFIG_END__SHIFT),
                       fe_vert_elem_conf_base | (0x18 << VIVS_FE_VERTEX_ELEMENT_CONFIG_START__SHIFT) | (0x24 << VIVS_FE_VERTEX_ELEMENT_CONFIG_END__SHIFT) | VIVS_FE_VERTEX_ELEMENT_CONFIG_NONCONSECUTIVE
        });
        etna_set_state(ctx, VIVS_VS_INPUT(0), VIVS_VS_INPUT_I0(0) |
        VIVS_VS_INPUT_I1(1) | VIVS_VS_INPUT_I2(2) | VIVS_VS_INPUT_I3(0));
        
        etna_set_state(ctx, VIVS_FE_VERTEX_STREAMS_CONTROL(0), 0x24);
        etna_set_state(ctx, VIVS_PA_W_CLIP_LIMIT, 0);
        etna_set_state(ctx, VIVS_PA_CONFIG,
                       VIVS_PA_CONFIG_CULL_FACE_MODE_CCW |
                       VIVS_PA_CONFIG_FILL_MODE_SOLID |
                       VIVS_PA_CONFIG_SHADE_MODEL_SMOOTH |
                       VIVS_PA_CONFIG_UNK22);
        etna_set_state(ctx, VIVS_FE_VERTEX_STREAMS_BASE_ADDR(0), etna_bo_gpu_address(vtx));
        
        for(int drawNr = 0; drawNr<6; drawNr++)
        {   
            etna_draw_primitives(ctx, PRIMITIVE_TYPE_TRIANGLE_STRIP, drawNr*4, 2);
        }
#else
        unsigned fe_vert_elem_conf_base = 
        VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_FLOAT |
        (ENDIAN_MODE_NO_SWAP << VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN__SHIFT) |
        VIVS_FE_VERTEX_ELEMENT_CONFIG_NONCONSECUTIVE |
        (3 <<VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM__SHIFT) |
        VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF |
        (0x0 << VIVS_FE_VERTEX_ELEMENT_CONFIG_START__SHIFT) |
        (0xc << VIVS_FE_VERTEX_ELEMENT_CONFIG_END__SHIFT);
        etna_set_state_multi(ctx, VIVS_FE_VERTEX_ELEMENT_CONFIG(0), 3, (uint32_t[]){
                       fe_vert_elem_conf_base | (0 << VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM__SHIFT),
                       fe_vert_elem_conf_base | (1 << VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM__SHIFT),
                       fe_vert_elem_conf_base | (2 << VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM__SHIFT)});
        etna_set_state(ctx, VIVS_VS_INPUT(0), VIVS_VS_INPUT_I0(0) |
        VIVS_VS_INPUT_I1(1) | VIVS_VS_INPUT_I2(2) | VIVS_VS_INPUT_I3(0));

        etna_set_state_multi(ctx, VIVS_FE_VERTEX_STREAMS_CONTROL(0), 3, (uint32_t[]) {0xc, 0xc, 0xc});
        etna_set_state(ctx, VIVS_PA_W_CLIP_LIMIT, 0);
        etna_set_state(ctx, VIVS_PA_CONFIG,
                    VIVS_PA_CONFIG_CULL_FACE_MODE_CCW |
                    VIVS_PA_CONFIG_FILL_MODE_SOLID |
                    VIVS_PA_CONFIG_SHADE_MODEL_SMOOTH |
                    VIVS_PA_CONFIG_UNK22);

        for(int drawNr = 0; drawNr<6; drawNr++)
        {
            etna_set_state_multi(ctx, VIVS_FE_VERTEX_STREAMS_BASE_ADDR(0), 3, (uint32_t[])
            {etna_bo_gpu_address(vtx)+(0x60*drawNr), etna_bo_gpu_address(vtx)+(0x60*drawNr)+0x30, etna_bo_gpu_address(vtx)+(0x60*drawNr)+0x60});
            
            etna_draw_primitives(ctx, PRIMITIVE_TYPE_TRIANGLE_STRIP, drawNr*4, 2);
        }
#endif //INTERLEAVED

        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);

        etna_flush(ctx, NULL);

        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_set_state(ctx, VIVS_RS_CONFIG,
                       (RS_FORMAT_A8R8G8B8 << VIVS_RS_CONFIG_SOURCE_FORMAT__SHIFT) |
                       VIVS_RS_CONFIG_SOURCE_TILED |
                       (RS_FORMAT_A8R8G8B8 << VIVS_RS_CONFIG_DEST_FORMAT__SHIFT) |
                       VIVS_RS_CONFIG_DEST_TILED);
        etna_set_state(ctx, VIVS_RS_SOURCE_STRIDE, (padded_width * 4 * 4) | (supertiled?VIVS_RS_SOURCE_STRIDE_TILING:0));
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, (padded_width * 4 * 4) | (supertiled?VIVS_RS_SOURCE_STRIDE_TILING:0));
        etna_set_state_multi(ctx, VIVS_RS_DITHER(0), 2, (uint32_t[]){0xffffffff, 0xffffffff});
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 0); /* no AA, no endian switch */
        etna_set_state(ctx, VIVS_RS_SOURCE_ADDR, etna_bo_gpu_address(rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, etna_bo_gpu_address(rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE, 
                       (padded_height << VIVS_RS_WINDOW_SIZE_HEIGHT__SHIFT) |
                       (padded_width << VIVS_RS_WINDOW_SIZE_WIDTH__SHIFT));
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbadabeeb);

        /* Submit second command buffer */
        etna_flush(ctx, NULL);

        etna_set_state(ctx, VIVS_TS_FLUSH_CACHE, VIVS_TS_FLUSH_CACHE_FLUSH);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
        //etna_set_state(ctx, VIVS_DUMMY_DUMMY, 0);
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG,
                       VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
                       VIVS_TS_MEM_CONFIG_DEPTH_16BPP | 
                       VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_set_state(ctx, VIVS_RS_CONFIG,
                       (RS_FORMAT_A8R8G8B8 << VIVS_RS_CONFIG_SOURCE_FORMAT__SHIFT) |
                       VIVS_RS_CONFIG_SOURCE_TILED |
                       (RS_FORMAT_A8R8G8B8 << VIVS_RS_CONFIG_DEST_FORMAT__SHIFT) |
                       VIVS_RS_CONFIG_SWAP_RB);
        etna_set_state(ctx, VIVS_RS_SOURCE_STRIDE, (padded_width * 4 * 4) | (supertiled?VIVS_RS_SOURCE_STRIDE_TILING:0));
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, (padded_width * 4));
        etna_set_state_multi(ctx, VIVS_RS_DITHER(0), 2, (uint32_t[]){0xffffffff, 0xffffffff});
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 0); /* no AA, no endian switch */
        etna_set_state(ctx, VIVS_RS_SOURCE_ADDR, etna_bo_gpu_address(rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, etna_bo_gpu_address(aux_rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE, 
                       (padded_height << VIVS_RS_WINDOW_SIZE_HEIGHT__SHIFT) |
                       (padded_width << VIVS_RS_WINDOW_SIZE_WIDTH__SHIFT));
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbadabeeb);

        /* Submit third command buffer, wait for pixel engine to finish */
        etna_finish(ctx);

        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_set_state(ctx, VIVS_RS_CONFIG,
                       (RS_FORMAT_A8R8G8B8 << VIVS_RS_CONFIG_SOURCE_FORMAT__SHIFT) |
                       (RS_FORMAT_A8R8G8B8 << VIVS_RS_CONFIG_DEST_FORMAT__SHIFT) /*|
                       VIVS_RS_CONFIG_SWAP_RB*/);
        etna_set_state(ctx, VIVS_RS_SOURCE_STRIDE, (padded_width * 4));
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, width * 4);
        etna_set_state(ctx, VIVS_RS_DITHER(0), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_DITHER(1), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 0); /* no AA, no endian switch */
        etna_set_state(ctx, VIVS_RS_SOURCE_ADDR, etna_bo_gpu_address(aux_rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, etna_bo_gpu_address(bmp)); /* ADDR_J */
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE, 
                       (height << VIVS_RS_WINDOW_SIZE_HEIGHT__SHIFT) |
                       (width << VIVS_RS_WINDOW_SIZE_WIDTH__SHIFT));
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);
        etna_finish(ctx);
    }
    bmp_dump32(etna_bo_map(bmp), width, height, false, "/home/linaro/fb.bmp");
    printf("Dump complete\n");
    
    etna_free(ctx);
    viv_close(conn);
    return 0;
}
