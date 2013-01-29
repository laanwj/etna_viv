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
/* Rotating, mipmapped cube. Load mipmaps from .dds file.
 * Automatic state management.
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
#include "etna_mem.h"
#include "etna_bswap.h"

#include "esTransform.h"
#include "dds.h"

#define RCPLOG2 (1.4426950408889634f)
#define VERTEX_BUFFER_SIZE 0x60000

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

float vTexCoords[] = {
  // front
  0.0f, 0.0f,
  1.0f, 0.0f,
  0.0f, 1.0f,
  1.0f, 1.0f,
  // back
  0.0f, 0.0f,
  1.0f, 0.0f,
  0.0f, 1.0f,
  1.0f, 1.0f,
  // right
  0.0f, 0.0f,
  1.0f, 0.0f,
  0.0f, 1.0f,
  1.0f, 1.0f,
  // left
  0.0f, 0.0f,
  1.0f, 0.0f,
  0.0f, 1.0f,
  1.0f, 1.0f,
  // top
  0.0f, 0.0f,
  1.0f, 0.0f,
  0.0f, 1.0f,
  1.0f, 1.0f,
  // bottom
  0.0f, 0.0f,
  1.0f, 0.0f,
  0.0f, 1.0f,
  1.0f, 1.0f,
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
uint32_t ps[] = { /* texture sampling */
    0x07811003, 0x00000800, 0x01c800d0, 0x00000000,
    0x07821018, 0x15002f20, 0x00000000, 0x00000000,
    0x07811003, 0x39001800, 0x01c80140, 0x00000000,
};
size_t vs_size = sizeof(vs);
size_t ps_size = sizeof(ps);

void etna_texture_tile(void *dest, void *src, unsigned width, unsigned height, unsigned src_stride, unsigned elmtsize)
{
#define TEX_TILE_WIDTH (4)
#define TEX_TILE_HEIGHT (4)
#define TEX_TILE_WORDS (TEX_TILE_WIDTH*TEX_TILE_HEIGHT)
    unsigned ytiles = height / TEX_TILE_HEIGHT;
    unsigned xtiles = width / TEX_TILE_WIDTH;
    unsigned dst_stride = xtiles * TEX_TILE_WORDS;
    if(elmtsize == 4)
    {
        src_stride >>= 2;

        for(unsigned ty=0; ty<ytiles; ++ty)
        {
            for(unsigned tx=0; tx<xtiles; ++tx)
            {
                unsigned ofs = ty * dst_stride + tx * TEX_TILE_WORDS;
                for(unsigned y=0; y<TEX_TILE_HEIGHT; ++y)
                {
                    for(unsigned x=0; x<TEX_TILE_WIDTH; ++x)
                    {
                        unsigned srcy = ty*TEX_TILE_HEIGHT + y;
                        unsigned srcx = tx*TEX_TILE_WIDTH + x;
                        ((uint32_t*)dest)[ofs] = ((uint32_t*)src)[srcy*src_stride+srcx];
                        ofs += 1;
                    }
                }
            }
        }
    }
}

enum etna_surface_tiling
{
    ETNA_TILING_LINEAR = 0,
    ETNA_TILING_TILED = 1,
    ETNA_TILING_SUPERTILED = 3
};

#define GL_NUM_VARYINGS 8
#define GL_NUM_VARYING_COMPONENTS 32
struct gl_state
{
    uint32_t vertex_element_config;
    uint32_t msaa_samples; /* 0, 2 or 4 */
    uint32_t msaa_bits;
    uint32_t varying_total_components;
    uint32_t varying_num_components[GL_NUM_VARYINGS];
    uint32_t varying_use[GL_NUM_VARYING_COMPONENTS];
};
struct rs_state
{
    uint8_t source_format; // RS_FORMAT_XXX
    uint8_t downsample_x; // Downsample in x direction
    uint8_t downsample_y; // Downsample in y direction
    uint8_t source_tiling; // ETNA_TILING_XXX
    uint8_t dest_tiling;   // ETNA_TILING_XXX
    uint8_t dest_format;  // RS_FORMAT_XXX
    uint8_t swap_rb;
    uint8_t flip;
    uint32_t source_addr;
    uint32_t source_stride;
    uint32_t dest_addr;
    uint32_t dest_stride;
    uint16_t width;
    uint16_t height;
    uint32_t dither[2];
    uint32_t clear_bits;
    uint32_t clear_mode; // VIVS_RS_CLEAR_CONTROL_MODE_XXX
    uint32_t clear_value[4];
    uint8_t aa;
    uint8_t endian_mode; // ENDIAN_MODE_XXX
};
#define VS_NUM_INPUTS 16
#define VS_NUM_OUTPUTS 16
struct vs_state
{
    uint32_t end_pc;
    uint32_t output_count;
    uint32_t input_count;
    uint32_t input_count_unk8;
    uint32_t num_temps;
    uint8_t outputs[VS_NUM_OUTPUTS];
    uint8_t inputs[VS_NUM_INPUTS];
    uint32_t load_balancing;
    uint32_t start_pc;
};
struct ps_state
{
    uint32_t end_pc;
    uint32_t output_reg;
    uint32_t input_count;
    uint32_t input_count_unk8;
    uint32_t num_temps;
    uint32_t start_pc;
};
#define PA_NUM_SHADER_ATTRIBUTES 10
struct pa_state
{
    float viewport_scale_x;
    float viewport_scale_y;
    float viewport_scale_z;
    float viewport_offset_x;
    float viewport_offset_y;
    float viewport_offset_z;
    float line_width;
    float point_size;
    int attribute_element_count;
    int attribute_element_count_unk0;
    bool point_size_enable;
    bool point_sprite_enable;
    uint32_t cull_face_mode; // PA_CONFIG_CULL_FACE_MODE_*
    uint32_t fill_mode; // PA_CONFIG_FILL_MODE_*
    uint32_t shade_model; // PA_CONFIG_SHADE_MODEL_*
    uint32_t shader_attributes[PA_NUM_SHADER_ATTRIBUTES];
};
struct se_state
{
    float scissor_left;
    float scissor_top;
    float scissor_right;
    float scissor_bottom;
    float depth_scale;
    float depth_bias;
    bool last_pixel_enable;
    float clip_right;
    float clip_bottom;
};


/* XXX autogenerate this 
 * XXX also need fixp flag somewhere 
 */
uint16_t rs_states[] = {
    /* 01604-01617 */ VIVS_RS_CONFIG>>2, 5,        /* SOURCE_ADDR, SOURCE_STRIDE, DEST_ADDR, DEST_STRIDE */
    /* 01620-01623 */ VIVS_RS_WINDOW_SIZE>>2, 1,   /* WINDOW_SIZE */
    /* 01630-01637 */ VIVS_RS_DITHER(0)>>2, 2,     /* DITHER(0,1) */
    /* 0163C-0164F */ VIVS_RS_CLEAR_CONTROL>>2, 5, /* CLEAR_CONTROL, FILL_VALUE(0..3) */
    /* 016A0-016A3 */ VIVS_RS_EXTRA_CONFIG>>2, 1,  /* EXTRA_CONFIG */
};

/* state packet description / metadata */
struct state_packet_desc
{
    int map_len;
    uint16_t *map;
    int values_len;
};

/* state packet values */
/* A state packet is a subset of the GPU state, can be regarded as a list of tuples
 * (addr, value), sorted by address. 
 */
struct state_packet
{
    bool invalidate;
    int values_len;
    uint32_t *values;
};

struct state_packet_desc rs_state_packet_desc = {
    .map_len = (sizeof(rs_states) / sizeof(uint16_t)),
    .map = rs_states,
    .values_len = 14
};

int create_state_packet(struct state_packet_desc *desc, struct state_packet **out)
{
    struct state_packet *rv = ETNA_NEW(struct state_packet);
    rv->values_len = desc->values_len;
    rv->values = malloc(desc->values_len * 4);
    *out = rv;
    return ETNA_OK;
}

/* compile RS state struct to state packet - will always write 14 words */
void compile_rs_state(struct state_packet *pkt, const struct rs_state *rs)
{
    uint32_t *state = pkt->values;
    int ptr = 0;
    /* VIVS_RS_CONFIG */
    state[ptr++] = VIVS_RS_CONFIG_SOURCE_FORMAT(rs->source_format) |
                            (rs->downsample_x?VIVS_RS_CONFIG_DOWNSAMPLE_X:0) |
                            (rs->downsample_y?VIVS_RS_CONFIG_DOWNSAMPLE_Y:0) |
                            ((rs->source_tiling&1)?VIVS_RS_CONFIG_SOURCE_TILED:0) |
                            VIVS_RS_CONFIG_DEST_FORMAT(rs->dest_format) |
                            ((rs->dest_tiling&1)?VIVS_RS_CONFIG_DEST_TILED:0) |
                            ((rs->swap_rb)?VIVS_RS_CONFIG_SWAP_RB:0) |
                            ((rs->flip)?VIVS_RS_CONFIG_FLIP:0);
    /* VIVS_RS_SOURCE_ADDR */
    state[ptr++] = rs->source_addr;
    /* VIVS_RS_SOURCE_STRIDE */
    state[ptr++] = rs->source_stride | ((rs->source_tiling&2)?VIVS_RS_SOURCE_STRIDE_TILING:0);
    /* VIVS_RS_DST_ADDR */
    state[ptr++] = rs->dest_addr;
    /* VIVS_RS_DEST_STRIDE */
    state[ptr++] = rs->dest_stride | ((rs->dest_tiling&2)?VIVS_RS_DEST_STRIDE_TILING:0);
    /* VIVS_RS_WINDOW_SIZE */
    state[ptr++] = VIVS_RS_WINDOW_SIZE_WIDTH(rs->width) | VIVS_RS_WINDOW_SIZE_HEIGHT(rs->height);
    /* VIVS_RS_DITHER(0..1) */
    state[ptr++] = rs->dither[0];
    state[ptr++] = rs->dither[1];
    /* VIVS_RS_CLEAR_CONTROL */
    state[ptr++] = VIVS_RS_CLEAR_CONTROL_BITS(rs->clear_bits) | rs->clear_mode;
    /* VIVS_RS_FILL_VALUE(0..3) */
    state[ptr++] = rs->clear_value[0];
    state[ptr++] = rs->clear_value[1];
    state[ptr++] = rs->clear_value[2];
    state[ptr++] = rs->clear_value[3];
    /* VIVS_RS_EXTRA_CONFIG */
    state[ptr++] = VIVS_RS_EXTRA_CONFIG_AA(rs->aa) | VIVS_RS_EXTRA_CONFIG_ENDIAN(rs->endian_mode);
}

/* compare old and new state packet, submit difference to queue */
void diff_state_packet(etna_ctx *restrict ctx, const struct state_packet_desc *restrict pdesc, const uint32_t *restrict oldvalues, const uint32_t *restrict newvalues)
{
    int values_ptr = 0;
        
    uint32_t start_addr = 0x10000;
    uint32_t next_addr = 0x10000;
    uint32_t cur_count = 0;

    etna_reserve(ctx, pdesc->values_len*2); /* worst case */
    for(int map_ptr=0; map_ptr < pdesc->map_len; map_ptr += 2)
    {
        uint16_t addr = pdesc->map[map_ptr];
        uint16_t sub_count = pdesc->map[map_ptr+1];
        for(int idx=0; idx<sub_count; ++idx)
        {
            uint32_t newvalue = newvalues[values_ptr];
            if(oldvalues[values_ptr] != newvalue)
            {
                if(addr != next_addr) /* non-consecutive */
                {
                    if(cur_count != 0) /* emit state load */
                    {
                        ETNA_EMIT_LOAD_STATE(ctx, start_addr, cur_count, 0 /*fixp*/);
                        ctx->offset += cur_count;
                        ETNA_ALIGN(ctx);
                    }
                    cur_count = 0;
                    start_addr = addr;
                }
                ctx->buf[ctx->offset + 1 + cur_count] = newvalue;
                next_addr = addr + 1;
                cur_count += 1;
            }
            ++values_ptr;
            ++addr;
        }
    }
    if(cur_count != 0) /* close final state_load */
    {
        ETNA_EMIT_LOAD_STATE(ctx, start_addr, cur_count, 0 /*fixp*/);
        ctx->offset += cur_count;
        ETNA_ALIGN(ctx);
    }
}

/* print command buffer for debugging */
void dump_cmd_buffer(uint32_t *buf, size_t size)
{
    printf("cmdbuf:");
    for(unsigned idx=0; idx<size; ++idx)
    {
        printf(":%08x ", buf[idx]);
        printf("\n");
    }
}

/* send contents of state packet, without comparison to any current state */
void send_state_packet(etna_ctx *restrict ctx, const struct state_packet_desc *restrict pdesc, const uint32_t *restrict newvalues)
{
    int values_ptr = 0;
        
    etna_reserve(ctx, 1 + pdesc->values_len + pdesc->map_len/2 + 1); /* worst case */
    unsigned start_offset = ctx->offset;
    for(int map_ptr=0; map_ptr < pdesc->map_len; map_ptr += 2)
    {
        uint16_t addr = pdesc->map[map_ptr];
        uint16_t sub_count = pdesc->map[map_ptr+1];
        ETNA_EMIT_LOAD_STATE(ctx, addr, sub_count, 0 /*fixp*/);
        memcpy(&ctx->buf[ctx->offset], &newvalues[values_ptr], sub_count*4);
        ctx->offset += sub_count;
        ETNA_ALIGN(ctx);
        values_ptr += sub_count;
    }
    //dump_cmd_buffer(&ctx->buf[start_offset], ctx->offset - start_offset);
}

void submit_state_packet(etna_ctx *restrict ctx, const struct state_packet_desc *restrict pdesc, struct state_packet *restrict cur, const struct state_packet *restrict new)
{
    /* XXX handle invalidate */
    //diff_state_packet(ctx, pdesc, cur->values, new->values);
    send_state_packet(ctx, pdesc, new->values);
    /* XXX copy new to cur */
}

int main(int argc, char **argv)
{
    int rv;
    int width = 256;
    int height = 256;
    int padded_width, padded_height;
    
    fb_info fb;
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
    rv = viv_open();
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device\n");
        exit(1);
    }
    printf("Succesfully opened device\n");

    /* Initialize buffers synchronization structure */
    etna_bswap_buffers *buffers = 0;
    if(etna_bswap_create(&buffers, (int (*)(void *, int))&fb_set_buffer, &fb) < 0)
    {
        fprintf(stderr, "Error creating buffer swapper\n");
        exit(1);
    }

    /* Allocate video memory */
    etna_vidmem *rt = 0; /* main render target */
    etna_vidmem *rt_ts = 0; /* tile status for main render target */
    etna_vidmem *z = 0; /* depth for main render target */
    etna_vidmem *z_ts = 0; /* depth ts for main render target */
    etna_vidmem *vtx = 0; /* vertex buffer */
    etna_vidmem *aux_rt = 0; /* auxilary render target */
    etna_vidmem *aux_rt_ts = 0; /* tile status for auxilary render target */
    etna_vidmem *tex = 0; /* texture */

    size_t rt_size = padded_width * padded_height * 4;
    size_t rt_ts_size = etna_align_up((padded_width * padded_height * 4)/0x100, 0x100);
    size_t z_size = padded_width * padded_height * 2;
    size_t z_ts_size = etna_align_up((padded_width * padded_height * 2)/0x100, 0x100);

    dds_texture *dds = 0;
    if(argc<2 || !dds_load(argv[1], &dds))
    {
        printf("Error loading texture\n");
        exit(1);
    }

    if(etna_vidmem_alloc_linear(&rt, rt_size, gcvSURF_RENDER_TARGET, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_vidmem_alloc_linear(&rt_ts, rt_ts_size, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_vidmem_alloc_linear(&z, z_size, gcvSURF_DEPTH, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_vidmem_alloc_linear(&z_ts, z_ts_size, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_vidmem_alloc_linear(&vtx, VERTEX_BUFFER_SIZE, gcvSURF_VERTEX, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_vidmem_alloc_linear(&aux_rt, 0x4000, gcvSURF_RENDER_TARGET, gcvPOOL_SYSTEM, true)!=ETNA_OK ||
       etna_vidmem_alloc_linear(&aux_rt_ts, 0x100, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, true)!=ETNA_OK ||
       etna_vidmem_alloc_linear(&tex, dds->size, gcvSURF_TEXTURE, gcvPOOL_DEFAULT, true)!=ETNA_OK
       )
    {
        fprintf(stderr, "Error allocating video memory\n");
        exit(1);
    }

    uint32_t tex_format = 0;
    uint32_t tex_base_width = dds->slices[0][0].width;
    uint32_t tex_base_height = dds->slices[0][0].height;
    uint32_t tex_base_log_width = (int)(logf(tex_base_width) * RCPLOG2 * 32.0f + 0.5f);
    uint32_t tex_base_log_height = (int)(logf(tex_base_height) * RCPLOG2 * 32.0f + 0.5f);
    printf("Loading compressed texture (format %i, %ix%i) log_width=%i log_height=%i\n", dds->fmt, width, height, tex_base_log_width, tex_base_log_height);
    if(dds->fmt == FMT_X8R8G8B8 || dds->fmt == FMT_A8R8G8B8)
    {
        for(int ix=0; ix<dds->num_mipmaps; ++ix)
        {
            printf("%08x: Tiling mipmap %i (%ix%i)\n", dds->slices[0][ix].offset, ix, dds->slices[0][ix].width, dds->slices[0][ix].height);
            etna_texture_tile((void*)((size_t)tex->logical + dds->slices[0][ix].offset), 
                    dds->slices[0][ix].data, dds->slices[0][ix].width, dds->slices[0][ix].height, dds->slices[0][ix].stride, 4);
        }
        tex_format = TEXTURE_FORMAT_X8R8G8B8;
    } else if(dds->fmt == FMT_DXT1 || dds->fmt == FMT_DXT3 || dds->fmt == FMT_DXT5 || dds->fmt == FMT_ETC1)
    {
        printf("Loading compressed texture\n");
        memcpy(tex->logical, dds->data, dds->size);
        switch(dds->fmt)
        {
        case FMT_DXT1: tex_format = TEXTURE_FORMAT_DXT1;
        case FMT_DXT3: tex_format = TEXTURE_FORMAT_DXT2_DXT3;
        case FMT_DXT5: tex_format = TEXTURE_FORMAT_DXT4_DXT5;
        case FMT_ETC1: tex_format = TEXTURE_FORMAT_ETC1;
        }
    } else
    {
        printf("Unknown texture format\n");
        exit(1);
    }

    /* Phew, now we got all the memory we need.
     * Write interleaved attribute vertex stream.
     * Unlike the GL example we only do this once, not every time glDrawArrays is called, the same would be accomplished
     * from GL by using a vertex buffer object.
     */
    for(int vert=0; vert<NUM_VERTICES; ++vert)
    {
        int dest_idx = vert * (3 + 3 + 2);
        for(int comp=0; comp<3; ++comp)
            ((float*)vtx->logical)[dest_idx+comp+0] = vVertices[vert*3 + comp]; /* 0 */
        for(int comp=0; comp<3; ++comp)
            ((float*)vtx->logical)[dest_idx+comp+3] = vNormals[vert*3 + comp]; /* 1 */
        for(int comp=0; comp<2; ++comp)
            ((float*)vtx->logical)[dest_idx+comp+6] = vTexCoords[vert*2 + comp]; /* 2 */
    }

    etna_ctx *ctx = 0;
    if(etna_create(&ctx) != ETNA_OK)
    {
        printf("Unable to create context\n");
        exit(1);
    }

    /* create RS states */
    struct state_packet *cur_rs_state=0;
    create_state_packet(&rs_state_packet_desc, &cur_rs_state);
    struct state_packet *clear_rt=0;
    struct state_packet *copy_to_screen=0;
    create_state_packet(&rs_state_packet_desc, &clear_rt);
    compile_rs_state(clear_rt, &(struct rs_state){
                .source_format = RS_FORMAT_X8R8G8B8,
                .dest_format = RS_FORMAT_X8R8G8B8,
                .dest_addr = rt_ts->address,
                .dest_stride = 0x40,
                .dither = {0xffffffff, 0xffffffff},
                .width = 16,
                .height = rt_ts_size/0x40,
                .clear_value = {0x55555555},
                .clear_mode = VIVS_RS_CLEAR_CONTROL_MODE_ENABLED1,
                .clear_bits = 0xffff
            });

    create_state_packet(&rs_state_packet_desc, &copy_to_screen);
    compile_rs_state(copy_to_screen, &(struct rs_state){
                .source_format = RS_FORMAT_X8R8G8B8,
                .source_tiling = ETNA_TILING_SUPERTILED,
                .source_addr = rt->address,
                .source_stride = padded_width * 4 * 4,
                .dest_format = RS_FORMAT_X8R8G8B8,
                .dest_tiling = ETNA_TILING_LINEAR,
                .dest_addr = fb.physical[buffers->backbuffer],
                .dest_stride = fb.fb_fix.line_length,
                .swap_rb = true,
                .dither = {0xffffffff, 0xffffffff},
                .clear_mode = VIVS_RS_CLEAR_CONTROL_MODE_DISABLED,
                .width = width,
                .height = height
            });

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

        /* XXX part of this can be put outside the loop, but until we have usable context management
         * this is safest.
         */
        etna_set_state(ctx, VIVS_RA_CONTROL, 0x3);
        
        etna_set_state(ctx, VIVS_GL_MULTI_SAMPLE_CONFIG, 
                VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES_NONE |
                VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_ENABLES(0xf) /*|
                VIVS_GL_MULTI_SAMPLE_CONFIG_UNK12 |
                VIVS_GL_MULTI_SAMPLE_CONFIG_UNK16 */
                ); 
        etna_set_state(ctx, VIVS_GL_VERTEX_ELEMENT_CONFIG, 0x1);
        etna_set_state(ctx, VIVS_GL_VARYING_NUM_COMPONENTS,  
                VIVS_GL_VARYING_NUM_COMPONENTS_VAR0(4)| /* position */
                VIVS_GL_VARYING_NUM_COMPONENTS_VAR1(2)  /* texture coordinate */
                );
        etna_set_state(ctx, VIVS_GL_VARYING_TOTAL_COMPONENTS,
                VIVS_GL_VARYING_TOTAL_COMPONENTS_NUM(4 + 2)
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

        etna_set_state(ctx, VIVS_PA_W_CLIP_LIMIT, 0x34000001);
        etna_set_state(ctx, VIVS_PA_SYSTEM_MODE, 0x11);
        etna_set_state(ctx, VIVS_PA_CONFIG, /* VIVS_PA_CONFIG_UNK22 | */
                                            VIVS_PA_CONFIG_CULL_FACE_MODE_CCW |
                                            VIVS_PA_CONFIG_FILL_MODE_SOLID |
                                            VIVS_PA_CONFIG_SHADE_MODEL_SMOOTH /* |
                                            VIVS_PA_CONFIG_POINT_SIZE_ENABLE |
                                            VIVS_PA_CONFIG_POINT_SPRITE_ENABLE*/);
        etna_set_state_f32(ctx, VIVS_PA_VIEWPORT_OFFSET_Z, 0.0);
        etna_set_state_f32(ctx, VIVS_PA_VIEWPORT_SCALE_Z, 1.0);
        etna_set_state_fixp(ctx, VIVS_PA_VIEWPORT_OFFSET_X, width << 15);
        etna_set_state_fixp(ctx, VIVS_PA_VIEWPORT_OFFSET_Y, height << 15);
        etna_set_state_fixp(ctx, VIVS_PA_VIEWPORT_SCALE_X, width << 15);
        etna_set_state_fixp(ctx, VIVS_PA_VIEWPORT_SCALE_Y, height << 15);
        etna_set_state(ctx, VIVS_PA_ATTRIBUTE_ELEMENT_COUNT, 0x200);
        etna_set_state(ctx, VIVS_PA_SHADER_ATTRIBUTES(0), 0x200);
        etna_set_state(ctx, VIVS_PA_SHADER_ATTRIBUTES(1), 0x200);

        etna_set_state(ctx, VIVS_SE_CONFIG, 0x0);
        etna_set_state(ctx, VIVS_SE_DEPTH_SCALE, 0x0);
        etna_set_state(ctx, VIVS_SE_DEPTH_BIAS, 0x0);
        etna_set_state_fixp(ctx, VIVS_SE_SCISSOR_LEFT, 0);
        etna_set_state_fixp(ctx, VIVS_SE_SCISSOR_TOP, 0);
        etna_set_state_fixp(ctx, VIVS_SE_SCISSOR_RIGHT, (width << 16) | 5);
        etna_set_state_fixp(ctx, VIVS_SE_SCISSOR_BOTTOM, (height << 16) | 5);

        etna_set_state(ctx, VIVS_PE_ALPHA_CONFIG,
                /* VIVS_PE_ALPHA_CONFIG_BLEND_ENABLE_COLOR | */
                /* VIVS_PE_ALPHA_CONFIG_BLEND_ENABLE_ALPHA | */
                VIVS_PE_ALPHA_CONFIG_SRC_FUNC_COLOR(BLEND_FUNC_ONE) |
                VIVS_PE_ALPHA_CONFIG_SRC_FUNC_ALPHA(BLEND_FUNC_ONE) |
                VIVS_PE_ALPHA_CONFIG_DST_FUNC_COLOR(BLEND_FUNC_ZERO) |
                VIVS_PE_ALPHA_CONFIG_DST_FUNC_ALPHA(BLEND_FUNC_ZERO) |
                VIVS_PE_ALPHA_CONFIG_EQ_COLOR(BLEND_EQ_ADD) |
                VIVS_PE_ALPHA_CONFIG_EQ_ALPHA(BLEND_EQ_ADD));
        etna_set_state(ctx, VIVS_PE_ALPHA_BLEND_COLOR, 
                VIVS_PE_ALPHA_BLEND_COLOR_B(0) | 
                VIVS_PE_ALPHA_BLEND_COLOR_G(0) | 
                VIVS_PE_ALPHA_BLEND_COLOR_R(0) | 
                VIVS_PE_ALPHA_BLEND_COLOR_A(0));
        etna_set_state(ctx, VIVS_PE_ALPHA_OP, /* VIVS_PE_ALPHA_OP_ALPHA_TEST */ 0);
        etna_set_state(ctx, VIVS_PE_STENCIL_CONFIG, VIVS_PE_STENCIL_CONFIG_REF_FRONT(0) |
                                                    VIVS_PE_STENCIL_CONFIG_MASK_FRONT(0xff) | 
                                                    VIVS_PE_STENCIL_CONFIG_WRITE_MASK(0xff) |
                                                    VIVS_PE_STENCIL_CONFIG_MODE_DISABLED);
        etna_set_state(ctx, VIVS_PE_STENCIL_OP, VIVS_PE_STENCIL_OP_FUNC_FRONT(COMPARE_FUNC_ALWAYS) |
                                                VIVS_PE_STENCIL_OP_FUNC_BACK(COMPARE_FUNC_ALWAYS) |
                                                VIVS_PE_STENCIL_OP_FAIL_FRONT(STENCIL_OP_KEEP) | 
                                                VIVS_PE_STENCIL_OP_FAIL_BACK(STENCIL_OP_KEEP) |
                                                VIVS_PE_STENCIL_OP_DEPTH_FAIL_FRONT(STENCIL_OP_KEEP) |
                                                VIVS_PE_STENCIL_OP_DEPTH_FAIL_BACK(STENCIL_OP_KEEP) |
                                                VIVS_PE_STENCIL_OP_PASS_FRONT(STENCIL_OP_KEEP) |
                                                VIVS_PE_STENCIL_OP_PASS_BACK(STENCIL_OP_KEEP));
        etna_set_state(ctx, VIVS_PE_COLOR_FORMAT, 
                VIVS_PE_COLOR_FORMAT_COMPONENTS(0xf) |
                VIVS_PE_COLOR_FORMAT_FORMAT(RS_FORMAT_X8R8G8B8) |
                VIVS_PE_COLOR_FORMAT_SUPER_TILED /* |
                VIVS_PE_COLOR_FORMAT_PARTIAL*/);
        etna_set_state(ctx, VIVS_PE_DEPTH_CONFIG, 
                VIVS_PE_DEPTH_CONFIG_DEPTH_FORMAT_D16 |
                VIVS_PE_DEPTH_CONFIG_SUPER_TILED |
                VIVS_PE_DEPTH_CONFIG_EARLY_Z |
                /* VIVS_PE_DEPTH_CONFIG_WRITE_ENABLE | */
                VIVS_PE_DEPTH_CONFIG_DEPTH_FUNC(COMPARE_FUNC_ALWAYS) |
                VIVS_PE_DEPTH_CONFIG_DEPTH_MODE_Z
                /* VIVS_PE_DEPTH_CONFIG_ONLY_DEPTH */
                );
        etna_set_state(ctx, VIVS_PE_DEPTH_ADDR, z->address);
        etna_set_state(ctx, VIVS_PE_DEPTH_STRIDE, padded_width * 2);
        etna_set_state(ctx, VIVS_PE_HDEPTH_CONTROL, VIVS_PE_HDEPTH_CONTROL_FORMAT_DISABLED);
        etna_set_state_f32(ctx, VIVS_PE_DEPTH_NORMALIZE, 65535.0);
        etna_set_state(ctx, VIVS_PE_COLOR_ADDR, rt->address);
        etna_set_state(ctx, VIVS_PE_COLOR_STRIDE, padded_width * 4); 
        etna_set_state_f32(ctx, VIVS_PE_DEPTH_NEAR, 0.0);
        etna_set_state_f32(ctx, VIVS_PE_DEPTH_FAR, 1.0);
        etna_set_state_f32(ctx, VIVS_PE_DEPTH_NORMALIZE, 65535.0);

        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_set_state(ctx, VIVS_RS_FLUSH_CACHE, VIVS_RS_FLUSH_CACHE_FLUSH);
        etna_stall(ctx, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);

        /* Clear render target */
        submit_state_packet(ctx, &rs_state_packet_desc, cur_rs_state, clear_rt);
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);
       
        /* Now set up TS */
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG, 
                VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
                VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR |
                VIVS_TS_MEM_CONFIG_DEPTH_16BPP | 
                VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION);
        etna_set_state(ctx, VIVS_TS_DEPTH_CLEAR_VALUE, 0xffffffff);
        etna_set_state(ctx, VIVS_TS_DEPTH_STATUS_BASE, z_ts->address);
        etna_set_state(ctx, VIVS_TS_DEPTH_SURFACE_BASE, z->address);
        etna_set_state(ctx, VIVS_TS_COLOR_CLEAR_VALUE, 0xff303030);
        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, rt_ts->address);
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, rt->address);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);

        /* set up texture unit */
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_TEXTURE);
        etna_set_state(ctx, VIVS_TE_SAMPLER_SIZE(0), 
                VIVS_TE_SAMPLER_SIZE_WIDTH(tex_base_width)|
                VIVS_TE_SAMPLER_SIZE_HEIGHT(tex_base_height));
        etna_set_state(ctx, VIVS_TE_SAMPLER_LOG_SIZE(0), 
                VIVS_TE_SAMPLER_LOG_SIZE_WIDTH(tex_base_log_width) |
                VIVS_TE_SAMPLER_LOG_SIZE_HEIGHT(tex_base_log_height));
        for(int ix=0; ix<dds->num_mipmaps; ++ix)
        {
            etna_set_state(ctx, VIVS_TE_SAMPLER_LOD_ADDR(0,ix), tex->address + dds->slices[0][ix].offset);
        }
        etna_set_state(ctx, VIVS_TE_SAMPLER_CONFIG0(0), 
                VIVS_TE_SAMPLER_CONFIG0_TYPE(TEXTURE_TYPE_2D)|
                VIVS_TE_SAMPLER_CONFIG0_UWRAP(TEXTURE_WRAPMODE_CLAMP_TO_EDGE)|
                VIVS_TE_SAMPLER_CONFIG0_VWRAP(TEXTURE_WRAPMODE_CLAMP_TO_EDGE)|
                VIVS_TE_SAMPLER_CONFIG0_MIN(TEXTURE_FILTER_LINEAR)|
                VIVS_TE_SAMPLER_CONFIG0_MIP(TEXTURE_FILTER_LINEAR)|
                VIVS_TE_SAMPLER_CONFIG0_MAG(TEXTURE_FILTER_LINEAR)|
                VIVS_TE_SAMPLER_CONFIG0_FORMAT(tex_format));
        etna_set_state(ctx, VIVS_TE_SAMPLER_LOD_CONFIG(0), 
                VIVS_TE_SAMPLER_LOD_CONFIG_MAX((dds->num_mipmaps - 1)<<5) | VIVS_TE_SAMPLER_LOD_CONFIG_MIN(0));

        /* shader setup */

        etna_set_state(ctx, VIVS_VS_START_PC, 0x0);
        etna_set_state(ctx, VIVS_VS_END_PC, vs_size/16);
        etna_set_state_multi(ctx, VIVS_VS_INPUT_COUNT, 3, (uint32_t[]){
                /* VIVS_VS_INPUT_COUNT */ VIVS_VS_INPUT_COUNT_UNK8(1) | VIVS_VS_INPUT_COUNT_COUNT(3),
                /* VIVS_VS_TEMP_REGISTER_CONTROL */ VIVS_VS_TEMP_REGISTER_CONTROL_NUM_TEMPS(6),
                /* VIVS_VS_OUTPUT(0) */ VIVS_VS_OUTPUT_O0(4) | 
                                        VIVS_VS_OUTPUT_O1(0) | 
                                        VIVS_VS_OUTPUT_O2(1)});
        etna_set_state_multi(ctx, VIVS_VS_INST_MEM(0), vs_size/4, vs);
        etna_set_state(ctx, VIVS_VS_OUTPUT_COUNT, 3);
        etna_set_state(ctx, VIVS_VS_LOAD_BALANCING, 0xf3f0542); /* depends on number of inputs/outputs/varyings? XXX how exactly */
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(0), 16, (uint32_t*)&modelviewprojection.m[0][0]);
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(16), 3, (uint32_t*)&normal.m[0][0]); /* u4.xyz */
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(20), 3, (uint32_t*)&normal.m[1][0]); /* u5.xyz */
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(24), 3, (uint32_t*)&normal.m[2][0]); /* u6.xyz */
        etna_set_state_multi(ctx, VIVS_VS_UNIFORMS(28), 16, (uint32_t*)&modelview.m[0][0]);
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(19), 2.0); /* u4.w */
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(23), 20.0); /* u5.w */
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(27), 0.0); /* u6.w */
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(45), 0.5); /* u11.y */
        etna_set_state_f32(ctx, VIVS_VS_UNIFORMS(44), 1.0); /* u11.x */
        etna_set_state(ctx, VIVS_VS_INPUT(0), VIVS_VS_INPUT_I0(0) | 
                                        VIVS_VS_INPUT_I1(1) | 
                                        VIVS_VS_INPUT_I2(2));

        etna_set_state(ctx, VIVS_PS_START_PC, 0x0);
        etna_set_state_multi(ctx, VIVS_PS_END_PC, 2, (uint32_t[]){
                /* VIVS_PS_END_PC */ ps_size/16,
                /* VIVS_PS_OUTPUT_REG */ 0x1});
        etna_set_state_multi(ctx, VIVS_PS_INST_MEM(0), ps_size/4, ps);
        etna_set_state(ctx, VIVS_PS_INPUT_COUNT, VIVS_PS_INPUT_COUNT_UNK8(31) | VIVS_PS_INPUT_COUNT_COUNT(3));
        etna_set_state(ctx, VIVS_PS_TEMP_REGISTER_CONTROL, VIVS_PS_TEMP_REGISTER_CONTROL_NUM_TEMPS(3));
        etna_set_state(ctx, VIVS_PS_CONTROL, VIVS_PS_CONTROL_UNK1);
        etna_set_state_f32(ctx, VIVS_PS_UNIFORMS(0), 1.0); /* u0.x */

        etna_set_state(ctx, VIVS_FE_VERTEX_STREAM_BASE_ADDR, vtx->address); /* ADDR_E */
        etna_set_state(ctx, VIVS_FE_VERTEX_STREAM_CONTROL, 
                VIVS_FE_VERTEX_STREAM_CONTROL_VERTEX_STRIDE((3 + 3 + 2)*4));
        etna_set_state(ctx, VIVS_FE_VERTEX_ELEMENT_CONFIG(0), 
                VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_FLOAT |
                (ENDIAN_MODE_NO_SWAP << VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN__SHIFT) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM(0) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM_3 |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_START(0x0) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_END(0xc));
        etna_set_state(ctx, VIVS_FE_VERTEX_ELEMENT_CONFIG(1), 
                VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_FLOAT |
                (ENDIAN_MODE_NO_SWAP << VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN__SHIFT) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM(0) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM_3 |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_START(0xc) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_END(0x18));
        etna_set_state(ctx, VIVS_FE_VERTEX_ELEMENT_CONFIG(2), 
                VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_FLOAT |
                (ENDIAN_MODE_NO_SWAP << VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN__SHIFT) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NONCONSECUTIVE |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM(0) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM_2 |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_START(0x18) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_END(0x20));

        for(int prim=0; prim<6; ++prim)
        {
            etna_draw_primitives(ctx, PRIMITIVE_TYPE_TRIANGLE_STRIP, prim*4, 2);
        }
#if 0
        /* resolve to self */
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        etna_set_state(ctx, VIVS_RS_CONFIG,
                VIVS_RS_CONFIG_SOURCE_FORMAT(RS_FORMAT_X8R8G8B8) |
                VIVS_RS_CONFIG_SOURCE_TILED |
                VIVS_RS_CONFIG_DEST_FORMAT(RS_FORMAT_X8R8G8B8) |
                VIVS_RS_CONFIG_DEST_TILED);
        etna_set_state(ctx, VIVS_RS_SOURCE_STRIDE, (padded_width * 4 * 4) | VIVS_RS_SOURCE_STRIDE_TILING);
        etna_set_state(ctx, VIVS_RS_DEST_STRIDE, (padded_width * 4 * 4) | VIVS_RS_DEST_STRIDE_TILING);
        etna_set_state(ctx, VIVS_RS_DITHER(0), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_DITHER(1), 0xffffffff);
        etna_set_state(ctx, VIVS_RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_MODE_DISABLED);
        etna_set_state(ctx, VIVS_RS_EXTRA_CONFIG, 0); /* no AA, no endian switch */
        etna_set_state(ctx, VIVS_RS_SOURCE_ADDR, rt->address); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_DEST_ADDR, rt->address); /* ADDR_A */
        etna_set_state(ctx, VIVS_RS_WINDOW_SIZE, 
                VIVS_RS_WINDOW_SIZE_HEIGHT(padded_height) |
                VIVS_RS_WINDOW_SIZE_WIDTH(padded_width));
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);

        etna_set_state(ctx, VIVS_RS_FLUSH_CACHE, VIVS_RS_FLUSH_CACHE_FLUSH);

        etna_set_state(ctx, VIVS_TS_COLOR_STATUS_BASE, rt_ts->address); /* ADDR_B */
        etna_set_state(ctx, VIVS_TS_COLOR_SURFACE_BASE, rt->address); /* ADDR_A */
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG, 
                VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
                VIVS_TS_MEM_CONFIG_DEPTH_16BPP | 
                VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR);
#endif
        etna_stall(ctx, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);

        /* copy to screen */
        etna_bswap_wait_available(buffers);
        submit_state_packet(ctx, &rs_state_packet_desc, cur_rs_state, copy_to_screen);
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);

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
