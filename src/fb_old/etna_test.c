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
/* shader playground using etna_* functions,
 * more high-level command-buffer building with synchronization
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

#include <linux/fb.h>
#include <errno.h>

#include "common.xml.h"
#include "state.xml.h"
#include "state_3d.xml.h"
#include "cmdstream.xml.h"

#include "write_bmp.h"
#include "viv.h"
#include "etna.h"
#include "etna_rs.h"
#include "etna_fb.h"
#include "etna_bo.h"
#include "etna_util.h"

#include "esTransform.h"

GLfloat vVertices[] = {
  -1.0f, -1.0f, +0.0f,
  +1.0f, -1.0f, +0.0f,
  -1.0f, +1.0f, +0.0f,
  +1.0f, +1.0f, +0.0f,
};

GLfloat vTexCoords[] = {
  +0.0f, +0.0f,
  +1.0f, +0.0f,
  +0.0f, +1.0f,
  +1.0f, +1.0f,
};

#define VERTEX_BUFFER_SIZE 0x60000

#define NUM_VERTICES (4)

int main(int argc, char **argv)
{
    int rv;
    int width = 256;
    int height = 256;
    int padded_width, padded_height;
    int backbuffer = 0;
    
    struct fb_info fb;
    rv = fb_open(0, &fb);
    if(rv!=0)
    {
        exit(1);
    }
    width = fb.fb_var.xres;
    height = fb.fb_var.yres;
    padded_width = etna_align_up(width, 64);
    padded_height = etna_align_up(height, 64);

    printf("padded_width %i padded_height %i\n", padded_width, padded_height);
    struct viv_conn *conn = 0;
    rv = viv_open(VIV_HW_3D, &conn);
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    printf("Succesfully opened device\n");

    struct etna_bo *rt = 0; /* main render target */
    struct etna_bo *rt_ts = 0; /* tile status for main render target */
    struct etna_bo *z = 0; /* depth for main render target */
    struct etna_bo *z_ts = 0; /* depth ts for main render target */
    struct etna_bo *vtx = 0; /* vertex buffer */
    struct etna_bo *aux_rt = 0; /* auxilary render target */
    struct etna_bo *aux_rt_ts = 0; /* tile status for auxilary render target */
    struct etna_bo *bmp = 0; /* bitmap */

    size_t rt_size = padded_width * padded_height * 4;
    size_t rt_ts_size = etna_align_up((padded_width * padded_height * 4)/0x100, 0x100);
    size_t z_size = padded_width * padded_height * 2;
    size_t z_ts_size = etna_align_up((padded_width * padded_height * 2)/0x100, 0x100);
    size_t bmp_size = width * height * 4;

    if(etna_bo_new(conn, &rt, rt_size, gcvSURF_RENDER_TARGET, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_bo_new(conn, &rt_ts, rt_ts_size, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_bo_new(conn, &z, z_size, gcvSURF_DEPTH, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_bo_new(conn, &z_ts, z_ts_size, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_bo_new(conn, &vtx, VERTEX_BUFFER_SIZE, gcvSURF_VERTEX, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_bo_new(conn, &aux_rt, 0x4000, gcvSURF_RENDER_TARGET, gcvPOOL_SYSTEM, true)!=ETNA_OK ||
       etna_bo_new(conn, &aux_rt_ts, 0x100, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_bo_new(conn, &bmp, bmp_size, gcvSURF_BITMAP, gcvPOOL_DEFAULT, true)!=ETNA_OK
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
    float *vtx_map = (float*)etna_bo_map(vtx);
    for(int vert=0; vert<NUM_VERTICES; ++vert)
    {
        int dest_idx = vert * (3 + 2);
        for(int comp=0; comp<3; ++comp)
            vtx_map[dest_idx+comp+0] = vVertices[vert*3 + comp]; /* 0 */
        for(int comp=0; comp<2; ++comp)
            vtx_map[dest_idx+comp+3] = vTexCoords[vert*2 + comp]; /* 1 */
    }
    struct etna_ctx *ctx = 0;
    if(etna_create(conn, &ctx) != ETNA_OK)
    {
        printf("Unable to create context\n");
        exit(1);
    }

    /* Now load the shader itself */
    uint32_t vs[] = {
        0x02001001, 0x2a800800, 0x00000000, 0x003fc008,
        0x02001003, 0x2a800800, 0x00000040, 0x00000002,
    };
    uint32_t vs_size = sizeof(vs);
    uint32_t *ps;
    uint32_t ps_size;
    if(argc < 2)
    {
        perror("provide shader on command line");
        exit(1);
    }
    int fd = open(argv[1], O_RDONLY);
    if(fd == -1)
    {
        perror("opening shader");
        exit(1);
    }
    ps_size = lseek(fd, 0, SEEK_END);
    ps = malloc(ps_size);
    lseek(fd, 0, SEEK_SET);
    if(ps_size == 0 || ps_size>8192 || read(fd, ps, ps_size) != ps_size)
    {
        perror("empty or unreadable shader");
        exit(1);
    }
    close(fd);

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
        etna_set_state(ctx, VIVS_SE_CONFIG, 0x0);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);

        /* Set up pixel engine */
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
        etna_set_state(ctx, VIVS_PE_STENCIL_CONFIG, ETNA_MASKED(VIVS_PE_STENCIL_CONFIG_REF_FRONT, 0) &
                                                    ETNA_MASKED(VIVS_PE_STENCIL_CONFIG_MASK_FRONT, 0xff) & 
                                                    ETNA_MASKED(VIVS_PE_STENCIL_CONFIG_WRITE_MASK, 0xff));
        etna_set_state(ctx, VIVS_PE_STENCIL_OP, ETNA_MASKED(VIVS_PE_STENCIL_OP_FUNC_FRONT, COMPARE_FUNC_ALWAYS) &
                                                ETNA_MASKED(VIVS_PE_STENCIL_OP_FUNC_BACK, COMPARE_FUNC_ALWAYS) &
                                                ETNA_MASKED(VIVS_PE_STENCIL_OP_FAIL_FRONT, STENCIL_OP_KEEP) & 
                                                ETNA_MASKED(VIVS_PE_STENCIL_OP_FAIL_BACK, STENCIL_OP_KEEP) & 
                                                ETNA_MASKED(VIVS_PE_STENCIL_OP_DEPTH_FAIL_FRONT, STENCIL_OP_KEEP) & 
                                                ETNA_MASKED(VIVS_PE_STENCIL_OP_DEPTH_FAIL_BACK, STENCIL_OP_KEEP) &
                                                ETNA_MASKED(VIVS_PE_STENCIL_OP_PASS_FRONT, STENCIL_OP_KEEP) &
                                                ETNA_MASKED(VIVS_PE_STENCIL_OP_PASS_BACK, STENCIL_OP_KEEP));

        etna_set_state(ctx, VIVS_PE_DEPTH_CONFIG, ETNA_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_WRITE_ENABLE, 0));
        etna_set_state(ctx, VIVS_PE_DEPTH_CONFIG, ETNA_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_EARLY_Z, 0));

        etna_set_state(ctx, VIVS_SE_DEPTH_SCALE, 0x0);
        etna_set_state(ctx, VIVS_SE_DEPTH_BIAS, 0x0);
        
        etna_set_state(ctx, VIVS_PA_CONFIG, ETNA_MASKED_BIT(VIVS_PA_CONFIG_UNK22, 0));
        etna_set_state(ctx, VIVS_PA_CONFIG, ETNA_MASKED_INL(VIVS_PA_CONFIG_CULL_FACE_MODE, OFF));
        etna_set_state(ctx, VIVS_PA_CONFIG, ETNA_MASKED_INL(VIVS_PA_CONFIG_FILL_MODE, SOLID));
        etna_set_state(ctx, VIVS_PA_CONFIG, ETNA_MASKED_INL(VIVS_PA_CONFIG_SHADE_MODEL, SMOOTH));

        etna_set_state(ctx, VIVS_PE_COLOR_FORMAT, 
                ETNA_MASKED_BIT(VIVS_PE_COLOR_FORMAT_OVERWRITE, 0));
        etna_set_state(ctx, VIVS_PE_COLOR_FORMAT, ETNA_MASKED(VIVS_PE_COLOR_FORMAT_COMPONENTS, 0xf));
        etna_set_state(ctx, VIVS_PE_COLOR_FORMAT, 
                ETNA_MASKED(VIVS_PE_COLOR_FORMAT_FORMAT, RS_FORMAT_A8R8G8B8) &
                ETNA_MASKED_BIT(VIVS_PE_COLOR_FORMAT_SUPER_TILED, 1));

        etna_set_state(ctx, VIVS_PE_COLOR_ADDR, etna_bo_gpu_address(rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_PE_COLOR_STRIDE, padded_width * 4); 
        etna_set_state(ctx, VIVS_GL_MULTI_SAMPLE_CONFIG, 
                ETNA_MASKED_INL(VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES, NONE) &
                ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_ENABLES, 0xf) &
                ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_UNK12, 0x0) &
                ETNA_MASKED(VIVS_GL_MULTI_SAMPLE_CONFIG_UNK16, 0x0)
                ); 
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
        etna_set_state(ctx, VIVS_PE_COLOR_FORMAT, ETNA_MASKED_BIT(VIVS_PE_COLOR_FORMAT_OVERWRITE, 1));
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);

        etna_set_state(ctx, VIVS_TS_COLOR_CLEAR_VALUE, 0);
        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, etna_bo_gpu_address(rt_ts)); /* ADDR_B */
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, etna_bo_gpu_address(rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG, VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR); /* ADDR_A */

        etna_set_state(ctx, VIVS_PE_DEPTH_CONFIG, 
                ETNA_MASKED_INL(VIVS_PE_DEPTH_CONFIG_DEPTH_FORMAT, D16) &
                ETNA_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_SUPER_TILED, 1)
                );
        etna_set_state(ctx, VIVS_PE_DEPTH_ADDR, etna_bo_gpu_address(z)); /* ADDR_C */
        etna_set_state(ctx, VIVS_PE_DEPTH_STRIDE, padded_width * 2);
        etna_set_state(ctx, VIVS_PE_STENCIL_CONFIG, ETNA_MASKED_INL(VIVS_PE_STENCIL_CONFIG_MODE, DISABLED));
        etna_set_state(ctx, VIVS_PE_HDEPTH_CONTROL, VIVS_PE_HDEPTH_CONTROL_FORMAT_DISABLED);
        etna_set_state_f32(ctx, VIVS_PE_DEPTH_NORMALIZE, 65535.0);
        etna_set_state(ctx, VIVS_PE_DEPTH_CONFIG, ETNA_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_EARLY_Z, 0));
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_DEPTH);

        etna_set_state(ctx, VIVS_TS_DEPTH_CLEAR_VALUE, 0xffffffff);
        etna_set_state(ctx, VIVS_TS_DEPTH_STATUS_BASE, etna_bo_gpu_address(z_ts)); /* ADDR_D */
        etna_set_state(ctx, VIVS_TS_DEPTH_SURFACE_BASE, etna_bo_gpu_address(z)); /* ADDR_C */
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG, 
                VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
                VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR |
                VIVS_TS_MEM_CONFIG_DEPTH_16BPP | 
                VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION);
        etna_set_state(ctx, VIVS_PE_DEPTH_CONFIG, ETNA_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_EARLY_Z, 1)); /* flip-flopping once again */

        /* Warm up RS on aux render target */
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_warm_up_rs(ctx, etna_bo_gpu_address(aux_rt), etna_bo_gpu_address(aux_rt_ts));

        /* Phew, now that's one hell of a setup; the serious rendering starts now */
        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, etna_bo_gpu_address(rt_ts)); /* ADDR_B */
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, etna_bo_gpu_address(rt)); /* ADDR_A */

        /* ... or so we thought */
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_warm_up_rs(ctx, etna_bo_gpu_address(aux_rt), etna_bo_gpu_address(aux_rt_ts));

        /* maybe now? */
        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, etna_bo_gpu_address(rt_ts)); /* ADDR_B */
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, etna_bo_gpu_address(rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
       
        /* nope, not really... */ 
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_warm_up_rs(ctx, etna_bo_gpu_address(aux_rt), etna_bo_gpu_address(aux_rt_ts));
        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, etna_bo_gpu_address(rt_ts)); /* ADDR_B */
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, etna_bo_gpu_address(rt)); /* ADDR_A */

        etna_stall(ctx, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);

        /* Set up the resolve to clear tile status for main render target 
         * What the blob does is regard the TS as an image of width N, height 4, with 4 bytes per pixel
         * Looks like the height always stays the same. I don't think it matters as long as the entire memory are is covered.
         * XXX need to clear the depth ts too.
         * */
        etna_set_state(ctx, VIVS_RS_CONFIG,
                VIVS_RS_CONFIG_SOURCE_FORMAT(RS_FORMAT_A8R8G8B8) |
                VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_A8R8G8B8)
                );
        etna_set_state_multi(ctx, VIVS_RS_DITHER(0), 2, (uint32_t[]){0xffffffff, 0xffffffff});
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, etna_bo_gpu_address(rt_ts)); /* ADDR_B */
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, 0x100); /* 0x100 iso 0x40! seems it uses a width of 256 if width divisible by 256, XXX need to figure out these rules */
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE, 
                VIVS_RS_WINDOW_SIZE_HEIGHT(rt_ts_size/0x100) |
                VIVS_RS_WINDOW_SIZE_WIDTH(64));
        etna_set_state(ctx, VIVS_RS_FILL_VALUE(0), 0x55555555);
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, 
                VIVS_RS_CLEAR_CONTROL_MODE_ENABLED1 |
                VIVS_RS_CLEAR_CONTROL_BITS(0xffff));
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 
                0); /* no AA, no endian switch */
        etna_set_state(ctx, VIVS_RS_KICKER, 
                0xbeebbeeb);
        
        etna_set_state(ctx, VIVS_TS_COLOR_CLEAR_VALUE, 0xff7f7f7f);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, etna_bo_gpu_address(rt_ts)); /* ADDR_B */
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, etna_bo_gpu_address(rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG, 
                VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
                VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR |
                VIVS_TS_MEM_CONFIG_DEPTH_16BPP | 
                VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);

        etna_set_state(ctx, VIVS_PE_DEPTH_CONFIG, ETNA_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_WRITE_ENABLE, 0));
        etna_set_state(ctx, VIVS_PE_DEPTH_CONFIG, ETNA_MASKED_INL(VIVS_PE_DEPTH_CONFIG_DEPTH_MODE, NONE));
        etna_set_state(ctx, VIVS_PE_DEPTH_CONFIG, ETNA_MASKED(VIVS_PE_DEPTH_CONFIG_DEPTH_FUNC, COMPARE_FUNC_ALWAYS));
        etna_set_state(ctx, VIVS_PE_DEPTH_CONFIG, ETNA_MASKED_INL(VIVS_PE_DEPTH_CONFIG_DEPTH_MODE, Z));
        etna_set_state(ctx, VIVS_PE_DEPTH_CONFIG, ETNA_MASKED_BIT(VIVS_PE_DEPTH_CONFIG_ONLY_DEPTH, 0));
        etna_set_state_f32(ctx, VIVS_PE_DEPTH_NEAR, 0.0);
        etna_set_state_f32(ctx, VIVS_PE_DEPTH_FAR, 1.0);
        etna_set_state_f32(ctx, VIVS_PE_DEPTH_NORMALIZE, 65535.0);

        /* set up primitive assembly */
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

        /* shader setup */
        etna_set_state(ctx, VIVS_VS_END_PC, vs_size/16);
        etna_set_state_multi(ctx, VIVS_VS_INPUT_COUNT, 3, (uint32_t[]){
                /* VIVS_VS_INPUT_COUNT */ (1<<8) | 2,
                /* VIVS_VS_TEMP_REGISTER_CONTROL */ VIVS_VS_TEMP_REGISTER_CONTROL_NUM_TEMPS(2),
                /* VIVS_VS_OUTPUT(0) */ 0x100});
        etna_set_state(ctx, VIVS_VS_START_PC, 0x0);
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(0), 0.5); /* u0.x */

        etna_set_state_multi(ctx, VIVS_VS_INST_MEM(0), vs_size/4, vs);
        etna_set_state(ctx, VIVS_RA_CONTROL, 0x3); /* huh, this is 1 for the cubes */
        etna_set_state_multi(ctx, VIVS_PS_END_PC, 2, (uint32_t[]){
                /* VIVS_PS_END_PC */ ps_size/16,
                /* VIVS_PS_OUTPUT_REG */ 0x1});
        etna_set_state(ctx, VIVS_PS_START_PC, 0x0);
        etna_set_state(ctx, VIVS_PA_SHADER_ATTRIBUTES(0), 0x200);
        etna_set_state(ctx, VIVS_GL_VARYING_NUM_COMPONENTS,  /* one varying, with four components */
                VIVS_GL_VARYING_NUM_COMPONENTS_VAR0(2)
                );
        etna_set_state_multi(ctx, VIVS_GL_VARYING_COMPONENT_USE(0), 2, (uint32_t[]){ /* one varying, with four components */
                VIVS_GL_VARYING_COMPONENT_USE_COMP0(VARYING_COMPONENT_USE_USED) |
                VIVS_GL_VARYING_COMPONENT_USE_COMP1(VARYING_COMPONENT_USE_USED) |
                VIVS_GL_VARYING_COMPONENT_USE_COMP2(VARYING_COMPONENT_USE_UNUSED) |
                VIVS_GL_VARYING_COMPONENT_USE_COMP3(VARYING_COMPONENT_USE_UNUSED)
                , 0
                });
        etna_set_state_f32(ctx, VIVS_PS_UNIFORMS(0), 0.0); /* u0.x */
        etna_set_state_f32(ctx, VIVS_PS_UNIFORMS(1), 1.0); /* u0.y */
        etna_set_state_f32(ctx, VIVS_PS_UNIFORMS(2), 0.5); /* u0.z */
        etna_set_state_f32(ctx, VIVS_PS_UNIFORMS(3), 2.0); /* u0.w */
        etna_set_state_f32(ctx, VIVS_PS_UNIFORMS(4), 1/256.0); /* u1.x */
        etna_set_state_f32(ctx, VIVS_PS_UNIFORMS(5), 16.0); /* u1.y */
        etna_set_state_f32(ctx, VIVS_PS_UNIFORMS(6), 10.0); /* u1.z */
        etna_set_state_f32(ctx, VIVS_PS_UNIFORMS(8), frame); /* u2.x */

        etna_set_state_multi(ctx, VIVS_PS_INST_MEM(0), ps_size/4, ps);
        etna_set_state(ctx, VIVS_PS_INPUT_COUNT, (31<<8)|2);
        etna_set_state(ctx, VIVS_PS_TEMP_REGISTER_CONTROL, 
                VIVS_PS_TEMP_REGISTER_CONTROL_NUM_TEMPS(4));
        etna_set_state(ctx, VIVS_PS_CONTROL, 
                VIVS_PS_CONTROL_UNK1
                );
        etna_set_state(ctx, VIVS_PA_ATTRIBUTE_ELEMENT_COUNT, 0x100);
        etna_set_state(ctx, VIVS_GL_VARYING_TOTAL_COMPONENTS,  /* one varying, with two components, must be 
                                                                changed together with GL_PS_VARYING_NUM_COMPONENTS */
                VIVS_GL_VARYING_TOTAL_COMPONENTS_NUM(2)
                );
        etna_set_state(ctx, VIVS_VS_LOAD_BALANCING, 0xf3f0582);
        etna_set_state(ctx, VIVS_VS_OUTPUT_COUNT, 2);
        etna_set_state(ctx, VIVS_PA_CONFIG, ETNA_MASKED_BIT(VIVS_PA_CONFIG_POINT_SIZE_ENABLE, 0));
        
        etna_set_state(ctx, VIVS_FE_VERTEX_STREAM_BASE_ADDR, etna_bo_gpu_address(vtx)); /* ADDR_E */
        etna_set_state(ctx, VIVS_FE_VERTEX_STREAM_CONTROL, 
                VIVS_FE_VERTEX_STREAM_CONTROL_VERTEX_STRIDE(0x14));
        etna_set_state(ctx, VIVS_FE_VERTEX_ELEMENT_CONFIG(0), 
                VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_FLOAT |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN(ENDIAN_MODE_NO_SWAP) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM(0) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM(3) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_START(0x0) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_END(0xc));
        etna_set_state(ctx, VIVS_FE_VERTEX_ELEMENT_CONFIG(1), 
                VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_FLOAT |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN(ENDIAN_MODE_NO_SWAP) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NONCONSECUTIVE |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM(0) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM(2) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_START(0xc) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_END(0x14));
        etna_set_state(ctx, VIVS_VS_INPUT(0), 0x00100); /* 0x20000 in etna_cube */
        etna_set_state(ctx, VIVS_PA_CONFIG, ETNA_MASKED_BIT(VIVS_PA_CONFIG_POINT_SPRITE_ENABLE, 0));
        etna_draw_primitives(ctx, PRIMITIVE_TYPE_TRIANGLE_STRIP, 0, 2);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);

        /* Submit first command buffer */
        etna_flush(ctx, NULL);

        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_set_state(ctx, VIVS_RS_CONFIG,
                VIVS_RS_CONFIG_SOURCE_FORMAT(RS_FORMAT_A8R8G8B8) |
                VIVS_RS_CONFIG_SOURCE_TILED |
                VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_A8R8G8B8) |
                VIVS_RS_CONFIG_DEST_TILED);
        etna_set_state(ctx, VIVS_RS_SOURCE_STRIDE, (padded_width * 4 * 4) | VIVS_RS_SOURCE_STRIDE_TILING);
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, (padded_width * 4 * 4) | VIVS_RS_DEST_STRIDE_TILING);
        etna_set_state(ctx, VIVS_RS_DITHER(0), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_DITHER(1), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 0); /* no AA, no endian switch */
        etna_set_state(ctx, VIVS_RS_SOURCE_ADDR, etna_bo_gpu_address(rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, etna_bo_gpu_address(rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE, 
                VIVS_RS_WINDOW_SIZE_HEIGHT(padded_height) |
                VIVS_RS_WINDOW_SIZE_WIDTH(padded_width));
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);

        /* Submit second command buffer */
        etna_flush(ctx, NULL);

        etna_warm_up_rs(ctx, etna_bo_gpu_address(aux_rt), etna_bo_gpu_address(aux_rt_ts));

        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, etna_bo_gpu_address(rt_ts)); /* ADDR_B */
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, etna_bo_gpu_address(rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG, 
                VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
                VIVS_TS_MEM_CONFIG_DEPTH_16BPP | 
                VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
        etna_set_state(ctx, VIVS_PE_COLOR_FORMAT, 
                ETNA_MASKED_BIT(VIVS_PE_COLOR_FORMAT_OVERWRITE, 0));

        /* Submit third command buffer, wait for pixel engine to finish */
        etna_finish(ctx);


        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_set_state(ctx, VIVS_RS_CONFIG,
                VIVS_RS_CONFIG_SOURCE_FORMAT(RS_FORMAT_A8R8G8B8) |
                VIVS_RS_CONFIG_SOURCE_TILED |
                VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_A8R8G8B8) /*|
                VIVS_RS_CONFIG_SWAP_RB*/);
        etna_set_state(ctx, VIVS_RS_SOURCE_STRIDE, (padded_width * 4 * 4) | VIVS_RS_SOURCE_STRIDE_TILING);
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, fb.fb_fix.line_length);
        etna_set_state(ctx, VIVS_RS_DITHER(0), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_DITHER(1), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 
                0); /* no AA, no endian switch */
        etna_set_state(ctx, VIVS_RS_SOURCE_ADDR, etna_bo_gpu_address(rt)); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, fb.physical[backbuffer]); /* ADDR_J */
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE, 
                VIVS_RS_WINDOW_SIZE_HEIGHT(height) |
                VIVS_RS_WINDOW_SIZE_WIDTH(width));
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);
        etna_finish(ctx);
        /* switch buffers */
        fb_set_buffer(&fb, backbuffer);
        backbuffer = 1-backbuffer;
    }

    etna_free(ctx);
    viv_close(conn);
    return 0;
}
