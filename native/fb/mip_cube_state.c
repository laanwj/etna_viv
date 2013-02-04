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
/* Gallium state experiments -- WIP
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
#include "etna_tex.h"

#include "esTransform.h"
#include "dds.h"
#include "minigallium.h"

#define RCPLOG2 (1.4426950408889634f)

/* print command buffer for debugging */
void dump_cmd_buffer(etna_ctx *ctx)
{
    uint32_t start_offset = ctx->cmdbuf[ctx->cur_buf].startOffset/4 + 8;
    uint32_t *buf = &ctx->buf[start_offset]; 
    size_t size = ctx->offset - start_offset;
    printf("cmdbuf:\n");
    for(unsigned idx=0; idx<size; ++idx)
    {
        printf(":%08x ", buf[idx]);
        printf("\n");
    }
}

#define VIV_MAX_VERTEX_STREAMS (8)
struct rs_state
{
    uint8_t source_format; // RS_FORMAT_XXX
    uint8_t downsample_x; // Downsample in x direction
    uint8_t downsample_y; // Downsample in y direction
    uint8_t source_tiling; // ETNA_LAYOUT_XXX
    uint8_t dest_tiling;   // ETNA_LAYOUT_XXX
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

/* Define state */
#define SET_STATE(addr, value) cs->addr = (value)
#define SET_STATE_FIXP(addr, value) cs->addr = (value)
#define SET_STATE_F32(addr, value) cs->addr = f32_to_u32(value)

struct compiled_rs_state
{
    uint32_t RS_CONFIG;
    uint32_t RS_SOURCE_ADDR;
    uint32_t RS_SOURCE_STRIDE;
    uint32_t RS_DEST_ADDR;
    uint32_t RS_DEST_STRIDE;
    uint32_t RS_WINDOW_SIZE;
    uint32_t RS_DITHER[2];
    uint32_t RS_CLEAR_CONTROL;
    uint32_t RS_FILL_VALUE[4];
    uint32_t RS_EXTRA_CONFIG;
};

/* compile RS state struct to state packet - will always write 14 words */
void compile_rs_state(struct compiled_rs_state *cs, const struct rs_state *rs)
{
    SET_STATE(RS_CONFIG, VIVS_RS_CONFIG_SOURCE_FORMAT(rs->source_format) |
                            (rs->downsample_x?VIVS_RS_CONFIG_DOWNSAMPLE_X:0) |
                            (rs->downsample_y?VIVS_RS_CONFIG_DOWNSAMPLE_Y:0) |
                            ((rs->source_tiling&1)?VIVS_RS_CONFIG_SOURCE_TILED:0) |
                            VIVS_RS_CONFIG_DEST_FORMAT(rs->dest_format) |
                            ((rs->dest_tiling&1)?VIVS_RS_CONFIG_DEST_TILED:0) |
                            ((rs->swap_rb)?VIVS_RS_CONFIG_SWAP_RB:0) |
                            ((rs->flip)?VIVS_RS_CONFIG_FLIP:0));
    SET_STATE(RS_SOURCE_ADDR, rs->source_addr);
    SET_STATE(RS_SOURCE_STRIDE, rs->source_stride | ((rs->source_tiling&2)?VIVS_RS_SOURCE_STRIDE_TILING:0));
    SET_STATE(RS_DEST_ADDR, rs->dest_addr);
    SET_STATE(RS_DEST_STRIDE, rs->dest_stride | ((rs->dest_tiling&2)?VIVS_RS_DEST_STRIDE_TILING:0));
    SET_STATE(RS_WINDOW_SIZE, VIVS_RS_WINDOW_SIZE_WIDTH(rs->width) | VIVS_RS_WINDOW_SIZE_HEIGHT(rs->height));
    SET_STATE(RS_DITHER[0], rs->dither[0]);
    SET_STATE(RS_DITHER[1], rs->dither[1]);
    SET_STATE(RS_CLEAR_CONTROL, VIVS_RS_CLEAR_CONTROL_BITS(rs->clear_bits) | rs->clear_mode);
    SET_STATE(RS_FILL_VALUE[0], rs->clear_value[0]);
    SET_STATE(RS_FILL_VALUE[1], rs->clear_value[1]);
    SET_STATE(RS_FILL_VALUE[2], rs->clear_value[2]);
    SET_STATE(RS_FILL_VALUE[3], rs->clear_value[3]);
    SET_STATE(RS_EXTRA_CONFIG, VIVS_RS_EXTRA_CONFIG_AA(rs->aa) | VIVS_RS_EXTRA_CONFIG_ENDIAN(rs->endian_mode));
}

/*********************************************************************/
/** Gallium state translation, WIP */

/* [0.0 .. 1.0] -> [0 .. 255] */
static inline uint8_t cfloat_to_uint8(float f)
{
    if(f<=0.0f) return 0;
    if(f>=1.0f) return 255;
    return f * 256.0f;
}

/* float to fixp 5.5 */
static inline uint32_t float_to_fixp55(float f)
{
    if(f >= 15.953125f) return 511;
    if(f < -16.0f) return 512;
    return (uint32_t) (f * 32.0f + 0.5f);
}

/* texture size to log2 in fixp 5.5 format */
static inline uint32_t log2_fixp55(unsigned width)
{
    return float_to_fixp55(logf((float)width) * RCPLOG2);
}

static inline uint32_t f32_to_u32(float value)
{
    union {
        uint32_t u32;
        float f32;
    } x = { .f32 = value };
    return x.u32;
}

static inline uint32_t translate_cull_face(unsigned cull_face, unsigned front_ccw)
{
    switch(cull_face) /* XXX verify this is the right way around */
    {
    case PIPE_FACE_NONE: return VIVS_PA_CONFIG_CULL_FACE_MODE_OFF;
    case PIPE_FACE_FRONT: return front_ccw ? VIVS_PA_CONFIG_CULL_FACE_MODE_CW : VIVS_PA_CONFIG_CULL_FACE_MODE_CCW;
    case PIPE_FACE_BACK: return front_ccw ? VIVS_PA_CONFIG_CULL_FACE_MODE_CCW : VIVS_PA_CONFIG_CULL_FACE_MODE_CW;
    default: printf("Unhandled cull face mode %i\n", cull_face); return 0;
    }
}

static inline uint32_t translate_polygon_mode(unsigned polygon_mode)
{
    switch(polygon_mode)
    {
    case PIPE_POLYGON_MODE_FILL: return VIVS_PA_CONFIG_FILL_MODE_SOLID;
    case PIPE_POLYGON_MODE_LINE: return VIVS_PA_CONFIG_FILL_MODE_WIREFRAME;
    case PIPE_POLYGON_MODE_POINT: return VIVS_PA_CONFIG_FILL_MODE_POINT;
    default: printf("Unhandled polygon mode %i\n", polygon_mode); return 0;
    }
}

static inline uint32_t translate_stencil_mode(bool enable_0, bool enable_1)
{
    if(enable_0)
    {
        return enable_1 ? VIVS_PE_STENCIL_CONFIG_MODE_TWO_SIDED : 
                          VIVS_PE_STENCIL_CONFIG_MODE_ONE_SIDED;
    } else {
        return VIVS_PE_STENCIL_CONFIG_MODE_DISABLED;
    }
}

static inline uint32_t translate_stencil_op(unsigned stencil_op)
{
    switch(stencil_op)
    {
    case PIPE_STENCIL_OP_KEEP:    return STENCIL_OP_KEEP;
    case PIPE_STENCIL_OP_ZERO:    return STENCIL_OP_ZERO;
    case PIPE_STENCIL_OP_REPLACE: return STENCIL_OP_REPLACE;
    case PIPE_STENCIL_OP_INCR:    return STENCIL_OP_INCR;
    case PIPE_STENCIL_OP_DECR:    return STENCIL_OP_DECR;
    case PIPE_STENCIL_OP_INCR_WRAP: return STENCIL_OP_INCR_WRAP;
    case PIPE_STENCIL_OP_DECR_WRAP: return STENCIL_OP_DECR_WRAP;
    case PIPE_STENCIL_OP_INVERT:  return STENCIL_OP_INVERT;
    default: printf("Unhandled stencil op: %i\n", stencil_op); return 0;
    }
}

static inline uint32_t translate_blend(unsigned blend)
{
    switch(blend)
    {
    case PIPE_BLEND_ADD: return BLEND_EQ_ADD;
    case PIPE_BLEND_SUBTRACT: return BLEND_EQ_SUBTRACT;
    case PIPE_BLEND_REVERSE_SUBTRACT: return BLEND_EQ_REVERSE_SUBTRACT;
    case PIPE_BLEND_MIN: return BLEND_EQ_MIN;
    case PIPE_BLEND_MAX: return BLEND_EQ_MAX;
    default: printf("Unhandled blend: %i\n", blend); return 0;
    }
}

static inline uint32_t translate_blend_factor(unsigned blend_factor)
{
    switch(blend_factor)
    {
    case PIPE_BLENDFACTOR_ONE:         return BLEND_FUNC_ONE;
    case PIPE_BLENDFACTOR_SRC_COLOR:   return BLEND_FUNC_SRC_COLOR;
    case PIPE_BLENDFACTOR_SRC_ALPHA:   return BLEND_FUNC_SRC_ALPHA;
    case PIPE_BLENDFACTOR_DST_ALPHA:   return BLEND_FUNC_DST_ALPHA;
    case PIPE_BLENDFACTOR_DST_COLOR:   return BLEND_FUNC_DST_COLOR;
    case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE: return BLEND_FUNC_SRC_ALPHA_SATURATE;
    case PIPE_BLENDFACTOR_CONST_COLOR: return BLEND_FUNC_CONSTANT_COLOR;
    case PIPE_BLENDFACTOR_CONST_ALPHA: return BLEND_FUNC_CONSTANT_ALPHA;
    case PIPE_BLENDFACTOR_ZERO:        return BLEND_FUNC_ZERO;
    case PIPE_BLENDFACTOR_INV_SRC_COLOR: return BLEND_FUNC_ONE_MINUS_SRC_COLOR;
    case PIPE_BLENDFACTOR_INV_SRC_ALPHA: return BLEND_FUNC_ONE_MINUS_SRC_ALPHA;
    case PIPE_BLENDFACTOR_INV_DST_ALPHA: return BLEND_FUNC_ONE_MINUS_DST_ALPHA;
    case PIPE_BLENDFACTOR_INV_DST_COLOR: return BLEND_FUNC_ONE_MINUS_DST_COLOR;
    case PIPE_BLENDFACTOR_INV_CONST_COLOR: return BLEND_FUNC_ONE_MINUS_CONSTANT_COLOR;
    case PIPE_BLENDFACTOR_INV_CONST_ALPHA: return BLEND_FUNC_ONE_MINUS_CONSTANT_ALPHA;
    case PIPE_BLENDFACTOR_SRC1_COLOR: 
    case PIPE_BLENDFACTOR_SRC1_ALPHA:
    case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
    case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:
    default: printf("Unhandled blend factor: %i\n", blend_factor); return 0;
    }
}

static inline uint32_t translate_texture_wrapmode(unsigned wrap)
{
    switch(wrap)
    {
    case PIPE_TEX_WRAP_REPEAT:          return TEXTURE_WRAPMODE_REPEAT;
    case PIPE_TEX_WRAP_CLAMP:           return TEXTURE_WRAPMODE_CLAMP_TO_EDGE;
    case PIPE_TEX_WRAP_CLAMP_TO_EDGE:   return TEXTURE_WRAPMODE_CLAMP_TO_EDGE;
    case PIPE_TEX_WRAP_CLAMP_TO_BORDER: return TEXTURE_WRAPMODE_CLAMP_TO_EDGE; /* XXX */
    case PIPE_TEX_WRAP_MIRROR_REPEAT:   return TEXTURE_WRAPMODE_MIRRORED_REPEAT;
    case PIPE_TEX_WRAP_MIRROR_CLAMP:    return TEXTURE_WRAPMODE_MIRRORED_REPEAT; /* XXX */
    case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE:   return TEXTURE_WRAPMODE_MIRRORED_REPEAT; /* XXX */
    case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER: return TEXTURE_WRAPMODE_MIRRORED_REPEAT; /* XXX */
    default: printf("Unhandled texture wrapmode: %i\n", wrap); return 0;
    }
}

static inline uint32_t translate_texture_mipfilter(unsigned filter)
{
    switch(filter)
    {
    case PIPE_TEX_MIPFILTER_NEAREST: return TEXTURE_FILTER_NEAREST;
    case PIPE_TEX_MIPFILTER_LINEAR:  return TEXTURE_FILTER_LINEAR;
    case PIPE_TEX_MIPFILTER_NONE:    return TEXTURE_FILTER_NONE;
    default: printf("Unhandled texture mipfilter: %i\n", filter); return 0;
    }
}

static inline uint32_t translate_texture_filter(unsigned filter)
{
    switch(filter)
    {
    case PIPE_TEX_FILTER_NEAREST: return TEXTURE_FILTER_NEAREST;
    case PIPE_TEX_FILTER_LINEAR:  return TEXTURE_FILTER_LINEAR;
    default: printf("Unhandled texture filter: %i\n", filter); return 0;
    }
}

static inline uint32_t translate_texture_format(enum pipe_format fmt)
{
    /* XXX these are all reversed - does it matter? */
    switch(fmt) /* XXX with TEXTURE_FORMAT_EXT and swizzle on newer chips we can support much more */
    {
    case PIPE_FORMAT_A8_UNORM: return TEXTURE_FORMAT_A8;
    case PIPE_FORMAT_L8_UNORM: return TEXTURE_FORMAT_L8;
    case PIPE_FORMAT_I8_UNORM: return TEXTURE_FORMAT_I8;
    case PIPE_FORMAT_L8A8_UNORM: return TEXTURE_FORMAT_A8L8;
    case PIPE_FORMAT_B4G4R4A4_UNORM: return TEXTURE_FORMAT_A4R4G4B4;
    case PIPE_FORMAT_B4G4R4X4_UNORM: return TEXTURE_FORMAT_X4R4G4B4;
    case PIPE_FORMAT_B8G8R8A8_UNORM: return TEXTURE_FORMAT_A8R8G8B8;
    case PIPE_FORMAT_B8G8R8X8_UNORM: return TEXTURE_FORMAT_X8R8G8B8;
    case PIPE_FORMAT_R8G8B8A8_UNORM: return TEXTURE_FORMAT_A8B8G8R8;
    case PIPE_FORMAT_R8G8B8X8_UNORM: return TEXTURE_FORMAT_X8B8G8R8;
    case PIPE_FORMAT_B5G6R5_UNORM: return TEXTURE_FORMAT_R5G6B5;
    case PIPE_FORMAT_B5G5R5A1_UNORM: return TEXTURE_FORMAT_A1R5G5B5;
    case PIPE_FORMAT_B5G5R5X1_UNORM: return TEXTURE_FORMAT_X1R5G5B5;
    case PIPE_FORMAT_YUYV: return TEXTURE_FORMAT_YUY2;
    case PIPE_FORMAT_UYVY: return TEXTURE_FORMAT_UYVY;
    case PIPE_FORMAT_Z16_UNORM: return TEXTURE_FORMAT_D16;
    case PIPE_FORMAT_Z24X8_UNORM: return TEXTURE_FORMAT_D24S8;
    case PIPE_FORMAT_Z24_UNORM_S8_UINT: return TEXTURE_FORMAT_D24S8;
    case PIPE_FORMAT_DXT1_RGB:  return TEXTURE_FORMAT_DXT1;
    case PIPE_FORMAT_DXT1_RGBA: return TEXTURE_FORMAT_DXT1;
    case PIPE_FORMAT_DXT3_RGBA: return TEXTURE_FORMAT_DXT2_DXT3;
    case PIPE_FORMAT_DXT5_RGBA: return TEXTURE_FORMAT_DXT4_DXT5;
    case PIPE_FORMAT_ETC1_RGB8: return TEXTURE_FORMAT_ETC1;
    default: printf("Unhandled texture format: %i\n", fmt); return 0;
    }
}

/* render target format */
static inline uint32_t translate_rt_format(enum pipe_format fmt)
{
    /* XXX these are all reversed - does it matter? */
    /* XXX RS: swap rb flag */
    switch(fmt) 
    {
    case PIPE_FORMAT_B4G4R4X4_UNORM: return RS_FORMAT_X4R4G4B4;
    case PIPE_FORMAT_B4G4R4A4_UNORM: return RS_FORMAT_A4R4G4B4;
    case PIPE_FORMAT_B5G5R5X1_UNORM: return RS_FORMAT_X1R5G5B5;
    case PIPE_FORMAT_B5G5R5A1_UNORM: return RS_FORMAT_A1R5G5B5;
    case PIPE_FORMAT_B5G6R5_UNORM: return RS_FORMAT_R5G6B5;
    case PIPE_FORMAT_B8G8R8X8_UNORM: return RS_FORMAT_X8R8G8B8;
    case PIPE_FORMAT_B8G8R8A8_UNORM: return RS_FORMAT_A8R8G8B8;
    case PIPE_FORMAT_YUYV: return RS_FORMAT_YUY2;
    default: printf("Unhandled rs surface format: %i\n", fmt); return 0;
    }
}

static inline uint32_t translate_depth_format(enum pipe_format fmt)
{
    switch(fmt) 
    {
    case PIPE_FORMAT_Z16_UNORM: return VIVS_PE_DEPTH_CONFIG_DEPTH_FORMAT_D16;
    case PIPE_FORMAT_Z24X8_UNORM: return VIVS_PE_DEPTH_CONFIG_DEPTH_FORMAT_D24S8;
    case PIPE_FORMAT_Z24_UNORM_S8_UINT: return VIVS_PE_DEPTH_CONFIG_DEPTH_FORMAT_D24S8;
    default: printf("Unhandled depth format: %i\n", fmt); return 0;
    }
}

/* render target format for MSAA */
static inline uint32_t translate_msaa_format(enum pipe_format fmt)
{
    switch(fmt) 
    {
    case PIPE_FORMAT_B4G4R4X4_UNORM: return VIVS_TS_MEM_CONFIG_MSAA_FORMAT_A4R4G4B4;
    case PIPE_FORMAT_B4G4R4A4_UNORM: return VIVS_TS_MEM_CONFIG_MSAA_FORMAT_A4R4G4B4;
    case PIPE_FORMAT_B5G5R5X1_UNORM: return VIVS_TS_MEM_CONFIG_MSAA_FORMAT_A1R5G5B5;
    case PIPE_FORMAT_B5G5R5A1_UNORM: return VIVS_TS_MEM_CONFIG_MSAA_FORMAT_A1R5G5B5;
    case PIPE_FORMAT_B5G6R5_UNORM:   return VIVS_TS_MEM_CONFIG_MSAA_FORMAT_R5G6B5;
    case PIPE_FORMAT_X8R8G8B8_UNORM: return VIVS_TS_MEM_CONFIG_MSAA_FORMAT_X8R8G8B8;
    case PIPE_FORMAT_A8R8G8B8_UNORM: return VIVS_TS_MEM_CONFIG_MSAA_FORMAT_A8R8G8B8;
    /* MSAA with YUYV not supported */
    default: printf("Unhandled msaa surface format: %i\n", fmt); return 0;
    }
}

static inline uint32_t translate_texture_target(enum pipe_texture_target tgt)
{
    switch(tgt)
    {
    case PIPE_TEXTURE_2D: return TEXTURE_TYPE_2D;
    case PIPE_TEXTURE_CUBE: return TEXTURE_TYPE_CUBE_MAP;
    default: printf("Unhandled texture target: %i\n", tgt); return 0;
    }
}

/* IMPORTANT when adding a vertex element format be sure to add the format to
 * all four of translate_vertex_format_type, translate_vertex_format_num,
 *   translate_vertex_format_normalize and vertex_element_size
 */
/* Return type flags for vertex element format */
static inline uint32_t translate_vertex_format_type(enum pipe_format fmt)
{
    switch(fmt)
    {
    case PIPE_FORMAT_R8_UNORM:
    case PIPE_FORMAT_R8G8_UNORM:
    case PIPE_FORMAT_R8G8B8_UNORM:
    case PIPE_FORMAT_R8G8B8A8_UNORM:
    case PIPE_FORMAT_R8_UINT:
    case PIPE_FORMAT_R8G8_UINT:
    case PIPE_FORMAT_R8G8B8_UINT:
    case PIPE_FORMAT_R8G8B8A8_UINT:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_UNSIGNED_BYTE;
    case PIPE_FORMAT_R8_SNORM:
    case PIPE_FORMAT_R8G8_SNORM:
    case PIPE_FORMAT_R8G8B8_SNORM:
    case PIPE_FORMAT_R8G8B8A8_SNORM:
    case PIPE_FORMAT_R8_SINT:
    case PIPE_FORMAT_R8G8_SINT:
    case PIPE_FORMAT_R8G8B8_SINT:
    case PIPE_FORMAT_R8G8B8A8_SINT:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_BYTE;
    case PIPE_FORMAT_R16_UNORM:
    case PIPE_FORMAT_R16G16_UNORM:
    case PIPE_FORMAT_R16G16B16_UNORM:
    case PIPE_FORMAT_R16G16B16A16_UNORM:
    case PIPE_FORMAT_R16_UINT:
    case PIPE_FORMAT_R16G16_UINT:
    case PIPE_FORMAT_R16G16B16_UINT:
    case PIPE_FORMAT_R16G16B16A16_UINT:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_UNSIGNED_SHORT;
    case PIPE_FORMAT_R16_SNORM:
    case PIPE_FORMAT_R16G16_SNORM:
    case PIPE_FORMAT_R16G16B16_SNORM:
    case PIPE_FORMAT_R16G16B16A16_SNORM:
    case PIPE_FORMAT_R16_SINT:
    case PIPE_FORMAT_R16G16_SINT:
    case PIPE_FORMAT_R16G16B16_SINT:
    case PIPE_FORMAT_R16G16B16A16_SINT:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_SHORT;
    case PIPE_FORMAT_R32_UNORM:
    case PIPE_FORMAT_R32G32_UNORM:
    case PIPE_FORMAT_R32G32B32_UNORM:
    case PIPE_FORMAT_R32G32B32A32_UNORM:
    case PIPE_FORMAT_R32_UINT:
    case PIPE_FORMAT_R32G32_UINT:
    case PIPE_FORMAT_R32G32B32_UINT:
    case PIPE_FORMAT_R32G32B32A32_UINT:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_UNSIGNED_INT;
    case PIPE_FORMAT_R32_SNORM:
    case PIPE_FORMAT_R32G32_SNORM:
    case PIPE_FORMAT_R32G32B32_SNORM:
    case PIPE_FORMAT_R32G32B32A32_SNORM:
    case PIPE_FORMAT_R32_SINT:
    case PIPE_FORMAT_R32G32_SINT:
    case PIPE_FORMAT_R32G32B32_SINT:
    case PIPE_FORMAT_R32G32B32A32_SINT:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_INT;
    case PIPE_FORMAT_R16_FLOAT:
    case PIPE_FORMAT_R16G16_FLOAT:
    case PIPE_FORMAT_R16G16B16_FLOAT:
    case PIPE_FORMAT_R16G16B16A16_FLOAT:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_HALF_FLOAT;
    case PIPE_FORMAT_R32_FLOAT:
    case PIPE_FORMAT_R32G32_FLOAT:
    case PIPE_FORMAT_R32G32B32_FLOAT:
    case PIPE_FORMAT_R32G32B32A32_FLOAT:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_FLOAT;
    case PIPE_FORMAT_R32_FIXED:
    case PIPE_FORMAT_R32G32_FIXED:
    case PIPE_FORMAT_R32G32B32_FIXED:
    case PIPE_FORMAT_R32G32B32A32_FIXED:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_FIXED;
    case PIPE_FORMAT_R10G10B10A2_UNORM:
    case PIPE_FORMAT_R10G10B10A2_USCALED:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_UNSIGNED_INT_10_10_10_2;
    case PIPE_FORMAT_R10G10B10A2_SNORM:
    case PIPE_FORMAT_R10G10B10A2_SSCALED:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_INT_10_10_10_2;
    default: printf("Unhandled vertex format: %i", fmt); return 0;
    }
}

/* Return normalization flag for vertex element format */
static inline uint32_t translate_vertex_format_normalize(enum pipe_format fmt)
{
    switch(fmt)
    {
    case PIPE_FORMAT_R8_UNORM:
    case PIPE_FORMAT_R8G8_UNORM:
    case PIPE_FORMAT_R8G8B8_UNORM:
    case PIPE_FORMAT_R8G8B8A8_UNORM:
    case PIPE_FORMAT_R8_SNORM:
    case PIPE_FORMAT_R8G8_SNORM:
    case PIPE_FORMAT_R8G8B8_SNORM:
    case PIPE_FORMAT_R8G8B8A8_SNORM:
    case PIPE_FORMAT_R16_UNORM:
    case PIPE_FORMAT_R16G16_UNORM:
    case PIPE_FORMAT_R16G16B16_UNORM:
    case PIPE_FORMAT_R16G16B16A16_UNORM:
    case PIPE_FORMAT_R16_SNORM:
    case PIPE_FORMAT_R16G16_SNORM:
    case PIPE_FORMAT_R16G16B16_SNORM:
    case PIPE_FORMAT_R16G16B16A16_SNORM:
    case PIPE_FORMAT_R32_UNORM:
    case PIPE_FORMAT_R32G32_UNORM:
    case PIPE_FORMAT_R32G32B32_UNORM:
    case PIPE_FORMAT_R32G32B32A32_UNORM:
    case PIPE_FORMAT_R32_SNORM:
    case PIPE_FORMAT_R32G32_SNORM:
    case PIPE_FORMAT_R32G32B32_SNORM:
    case PIPE_FORMAT_R32G32B32A32_SNORM:
    case PIPE_FORMAT_R10G10B10A2_UNORM:
    case PIPE_FORMAT_R10G10B10A2_SNORM:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_ON;
    case PIPE_FORMAT_R8_UINT:
    case PIPE_FORMAT_R8G8_UINT:
    case PIPE_FORMAT_R8G8B8_UINT:
    case PIPE_FORMAT_R8G8B8A8_UINT:
    case PIPE_FORMAT_R8_SINT:
    case PIPE_FORMAT_R8G8_SINT:
    case PIPE_FORMAT_R8G8B8_SINT:
    case PIPE_FORMAT_R8G8B8A8_SINT:
    case PIPE_FORMAT_R16_UINT:
    case PIPE_FORMAT_R16G16_UINT:
    case PIPE_FORMAT_R16G16B16_UINT:
    case PIPE_FORMAT_R16G16B16A16_UINT:
    case PIPE_FORMAT_R16_SINT:
    case PIPE_FORMAT_R16G16_SINT:
    case PIPE_FORMAT_R16G16B16_SINT:
    case PIPE_FORMAT_R16G16B16A16_SINT:
    case PIPE_FORMAT_R32_UINT:
    case PIPE_FORMAT_R32G32_UINT:
    case PIPE_FORMAT_R32G32B32_UINT:
    case PIPE_FORMAT_R32G32B32A32_UINT:
    case PIPE_FORMAT_R32_SINT:
    case PIPE_FORMAT_R32G32_SINT:
    case PIPE_FORMAT_R32G32B32_SINT:
    case PIPE_FORMAT_R32G32B32A32_SINT:
    case PIPE_FORMAT_R16_FLOAT:
    case PIPE_FORMAT_R16G16_FLOAT:
    case PIPE_FORMAT_R16G16B16_FLOAT:
    case PIPE_FORMAT_R16G16B16A16_FLOAT:
    case PIPE_FORMAT_R32_FLOAT:
    case PIPE_FORMAT_R32G32_FLOAT:
    case PIPE_FORMAT_R32G32B32_FLOAT:
    case PIPE_FORMAT_R32G32B32A32_FLOAT:
    case PIPE_FORMAT_R32_FIXED:
    case PIPE_FORMAT_R32G32_FIXED:
    case PIPE_FORMAT_R32G32B32_FIXED:
    case PIPE_FORMAT_R32G32B32A32_FIXED:
    case PIPE_FORMAT_R10G10B10A2_USCALED:
    case PIPE_FORMAT_R10G10B10A2_SSCALED:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF;
    default: printf("Unhandled vertex format: %i", fmt); return 0;
    }
}

/* Return number of components for vertex element format */
static inline uint32_t vertex_format_num(enum pipe_format fmt)
{
    switch(fmt)
    {
    case PIPE_FORMAT_R8_UNORM:
    case PIPE_FORMAT_R8_UINT:
    case PIPE_FORMAT_R8_SNORM:
    case PIPE_FORMAT_R8_SINT:
    case PIPE_FORMAT_R16_UNORM:
    case PIPE_FORMAT_R16_UINT:
    case PIPE_FORMAT_R16_SNORM:
    case PIPE_FORMAT_R16_SINT:
    case PIPE_FORMAT_R32_UNORM:
    case PIPE_FORMAT_R32_UINT:
    case PIPE_FORMAT_R32_SNORM:
    case PIPE_FORMAT_R32_SINT:
    case PIPE_FORMAT_R16_FLOAT:
    case PIPE_FORMAT_R32_FLOAT:
    case PIPE_FORMAT_R32_FIXED:
        return 1;
    case PIPE_FORMAT_R8G8_UNORM:
    case PIPE_FORMAT_R8G8_UINT:
    case PIPE_FORMAT_R8G8_SNORM:
    case PIPE_FORMAT_R8G8_SINT:
    case PIPE_FORMAT_R16G16_UNORM:
    case PIPE_FORMAT_R16G16_UINT:
    case PIPE_FORMAT_R16G16_SNORM:
    case PIPE_FORMAT_R16G16_SINT:
    case PIPE_FORMAT_R32G32_UNORM:
    case PIPE_FORMAT_R32G32_UINT:
    case PIPE_FORMAT_R32G32_SNORM:
    case PIPE_FORMAT_R32G32_SINT:
    case PIPE_FORMAT_R16G16_FLOAT:
    case PIPE_FORMAT_R32G32_FLOAT:
    case PIPE_FORMAT_R32G32_FIXED:
        return 2;
    case PIPE_FORMAT_R8G8B8_UNORM:
    case PIPE_FORMAT_R8G8B8_UINT:
    case PIPE_FORMAT_R8G8B8_SNORM:
    case PIPE_FORMAT_R8G8B8_SINT:
    case PIPE_FORMAT_R16G16B16_UNORM:
    case PIPE_FORMAT_R16G16B16_UINT:
    case PIPE_FORMAT_R16G16B16_SNORM:
    case PIPE_FORMAT_R16G16B16_SINT:
    case PIPE_FORMAT_R32G32B32_UNORM:
    case PIPE_FORMAT_R32G32B32_UINT:
    case PIPE_FORMAT_R32G32B32_SNORM:
    case PIPE_FORMAT_R32G32B32_SINT:
    case PIPE_FORMAT_R16G16B16_FLOAT:
    case PIPE_FORMAT_R32G32B32_FLOAT:
    case PIPE_FORMAT_R32G32B32_FIXED:
        return 3;
    case PIPE_FORMAT_R8G8B8A8_UNORM:
    case PIPE_FORMAT_R8G8B8A8_UINT:
    case PIPE_FORMAT_R8G8B8A8_SNORM:
    case PIPE_FORMAT_R8G8B8A8_SINT:
    case PIPE_FORMAT_R16G16B16A16_UNORM:
    case PIPE_FORMAT_R16G16B16A16_UINT:
    case PIPE_FORMAT_R16G16B16A16_SNORM:
    case PIPE_FORMAT_R16G16B16A16_SINT:
    case PIPE_FORMAT_R32G32B32A32_UNORM:
    case PIPE_FORMAT_R32G32B32A32_UINT:
    case PIPE_FORMAT_R32G32B32A32_SNORM:
    case PIPE_FORMAT_R32G32B32A32_SINT:
    case PIPE_FORMAT_R16G16B16A16_FLOAT:
    case PIPE_FORMAT_R32G32B32A32_FLOAT:
    case PIPE_FORMAT_R32G32B32A32_FIXED:
    case PIPE_FORMAT_R10G10B10A2_UNORM:
    case PIPE_FORMAT_R10G10B10A2_USCALED:
    case PIPE_FORMAT_R10G10B10A2_SNORM:
    case PIPE_FORMAT_R10G10B10A2_SSCALED:
        return 4;
    default: printf("Unhandled vertex format: %i", fmt); return 0;
    }
}

/* Return size in bytes for one vertex element */
static inline uint32_t vertex_element_size(enum pipe_format fmt)
{
    switch(fmt)
    {
    case PIPE_FORMAT_R8_UNORM:
    case PIPE_FORMAT_R8_UINT:
    case PIPE_FORMAT_R8_SNORM:
    case PIPE_FORMAT_R8_SINT:
        return 1;
    case PIPE_FORMAT_R8G8_UNORM:
    case PIPE_FORMAT_R8G8_UINT:
    case PIPE_FORMAT_R8G8_SNORM:
    case PIPE_FORMAT_R8G8_SINT:
    case PIPE_FORMAT_R16_UNORM:
    case PIPE_FORMAT_R16_UINT:
    case PIPE_FORMAT_R16_SNORM:
    case PIPE_FORMAT_R16_SINT:
    case PIPE_FORMAT_R16_FLOAT:
        return 2;
    case PIPE_FORMAT_R8G8B8_UNORM:
    case PIPE_FORMAT_R8G8B8_UINT:
    case PIPE_FORMAT_R8G8B8_SNORM:
    case PIPE_FORMAT_R8G8B8_SINT:
        return 3;
    case PIPE_FORMAT_R8G8B8A8_UNORM:
    case PIPE_FORMAT_R8G8B8A8_UINT:
    case PIPE_FORMAT_R8G8B8A8_SNORM:
    case PIPE_FORMAT_R8G8B8A8_SINT:
    case PIPE_FORMAT_R16G16_UNORM:
    case PIPE_FORMAT_R16G16_UINT:
    case PIPE_FORMAT_R16G16_SNORM:
    case PIPE_FORMAT_R16G16_SINT:
    case PIPE_FORMAT_R32_UNORM:
    case PIPE_FORMAT_R32_UINT:
    case PIPE_FORMAT_R32_SNORM:
    case PIPE_FORMAT_R32_SINT:
    case PIPE_FORMAT_R16G16_FLOAT:
    case PIPE_FORMAT_R32_FLOAT:
    case PIPE_FORMAT_R32_FIXED:
    case PIPE_FORMAT_R10G10B10A2_UNORM:
    case PIPE_FORMAT_R10G10B10A2_USCALED:
    case PIPE_FORMAT_R10G10B10A2_SNORM:
    case PIPE_FORMAT_R10G10B10A2_SSCALED:
        return 4;
    case PIPE_FORMAT_R16G16B16_UNORM:
    case PIPE_FORMAT_R16G16B16_UINT:
    case PIPE_FORMAT_R16G16B16_SNORM:
    case PIPE_FORMAT_R16G16B16_SINT:
    case PIPE_FORMAT_R16G16B16_FLOAT:
        return 6;
    case PIPE_FORMAT_R16G16B16A16_UNORM:
    case PIPE_FORMAT_R16G16B16A16_UINT:
    case PIPE_FORMAT_R16G16B16A16_SNORM:
    case PIPE_FORMAT_R16G16B16A16_SINT:
    case PIPE_FORMAT_R32G32_UNORM:
    case PIPE_FORMAT_R32G32_UINT:
    case PIPE_FORMAT_R32G32_SNORM:
    case PIPE_FORMAT_R32G32_SINT:
    case PIPE_FORMAT_R16G16B16A16_FLOAT:
    case PIPE_FORMAT_R32G32_FLOAT:
    case PIPE_FORMAT_R32G32_FIXED:
        return 8;
    case PIPE_FORMAT_R32G32B32_UNORM:
    case PIPE_FORMAT_R32G32B32_UINT:
    case PIPE_FORMAT_R32G32B32_SNORM:
    case PIPE_FORMAT_R32G32B32_SINT:
    case PIPE_FORMAT_R32G32B32_FLOAT:
    case PIPE_FORMAT_R32G32B32_FIXED:
        return 12;
    case PIPE_FORMAT_R32G32B32A32_UNORM:
    case PIPE_FORMAT_R32G32B32A32_UINT:
    case PIPE_FORMAT_R32G32B32A32_SNORM:
    case PIPE_FORMAT_R32G32B32A32_SINT:
    case PIPE_FORMAT_R32G32B32A32_FLOAT:
    case PIPE_FORMAT_R32G32B32A32_FIXED:
        return 16;
    default: printf("Unhandled vertex format: %i", fmt); return 0;
    }
}

static inline uint32_t translate_index_size(unsigned index_size)
{
    switch(index_size)
    {
    case 1: return VIVS_FE_INDEX_STREAM_CONTROL_TYPE_UNSIGNED_CHAR;
    case 2: return VIVS_FE_INDEX_STREAM_CONTROL_TYPE_UNSIGNED_SHORT;
    case 4: return VIVS_FE_INDEX_STREAM_CONTROL_TYPE_UNSIGNED_INT;
    default: printf("Unhandled index size %i\n", index_size); return 0;
    }
}

/*********************************************************************/
/** Gallium state compilation, WIP */

struct compiled_rasterizer_state
{
    uint32_t PA_CONFIG;
    uint32_t PA_LINE_WIDTH;
    uint32_t PA_POINT_SIZE;
    uint32_t SE_DEPTH_SCALE;
    uint32_t SE_DEPTH_BIAS;
    uint32_t SE_CONFIG;
    bool scissor;
};

void compile_rasterizer_state(struct compiled_rasterizer_state *cs, const struct pipe_rasterizer_state *rs)
{
    if(rs->fill_front != rs->fill_back)
    {
        printf("Different front and back fill mode not supported\n");
    }
    SET_STATE(PA_CONFIG, 
            (rs->flatshade ? VIVS_PA_CONFIG_SHADE_MODEL_FLAT : VIVS_PA_CONFIG_SHADE_MODEL_SMOOTH) | 
            translate_cull_face(rs->cull_face, rs->front_ccw) |
            translate_polygon_mode(rs->fill_front) |
            (rs->point_quad_rasterization ? VIVS_PA_CONFIG_POINT_SPRITE_ENABLE : 0) |
            (rs->point_size_per_vertex ? VIVS_PA_CONFIG_POINT_SIZE_ENABLE : 0));
    SET_STATE_F32(PA_LINE_WIDTH, rs->line_width);
    SET_STATE_F32(PA_POINT_SIZE, rs->point_size);
    SET_STATE_F32(SE_DEPTH_SCALE, rs->offset_scale);
    SET_STATE_F32(SE_DEPTH_BIAS, rs->offset_units);
    SET_STATE(SE_CONFIG, 
            (rs->line_last_pixel ? VIVS_SE_CONFIG_LAST_PIXEL_ENABLE : 0) 
            /* XXX anything else? */
            );
    /* XXX rs->gl_rasterization_rules is likely one of the bits in VIVS_PA_SYSTEM_MODE */
    /* rs->scissor overrides the scissor, defaulting to the whole framebuffer, with the scissor state */
    cs->scissor = rs->scissor;
}

struct compiled_depth_stencil_alpha_state
{
    uint32_t PE_DEPTH_CONFIG;
    uint32_t PE_ALPHA_OP;
    uint32_t PE_STENCIL_OP;
    uint32_t PE_STENCIL_CONFIG;
};

void compile_depth_stencil_alpha_state(struct compiled_depth_stencil_alpha_state *cs, const struct pipe_depth_stencil_alpha_state *dsa)
{
    /* XXX does stencil[0] / stencil[1] order depend on rs->front_ccw? */
    /* compare funcs have 1 to 1 mapping */
    SET_STATE(PE_DEPTH_CONFIG, 
            VIVS_PE_DEPTH_CONFIG_DEPTH_FUNC(dsa->depth.enabled ? dsa->depth.func : PIPE_FUNC_ALWAYS) |
            (dsa->depth.writemask ? VIVS_PE_DEPTH_CONFIG_WRITE_ENABLE : 0)
            /* XXX EARLY_Z */
            );
    SET_STATE(PE_ALPHA_OP, 
            (dsa->alpha.enabled ? VIVS_PE_ALPHA_OP_ALPHA_TEST : 0) |
            VIVS_PE_ALPHA_OP_ALPHA_FUNC(dsa->alpha.func) |
            VIVS_PE_ALPHA_OP_ALPHA_REF(cfloat_to_uint8(dsa->alpha.ref_value)));
    SET_STATE(PE_STENCIL_OP, 
            VIVS_PE_STENCIL_OP_FUNC_FRONT(dsa->stencil[0].func) |
            VIVS_PE_STENCIL_OP_FUNC_BACK(dsa->stencil[1].func) |
            VIVS_PE_STENCIL_OP_FAIL_FRONT(translate_stencil_op(dsa->stencil[0].fail_op)) | 
            VIVS_PE_STENCIL_OP_FAIL_BACK(translate_stencil_op(dsa->stencil[1].fail_op)) |
            VIVS_PE_STENCIL_OP_DEPTH_FAIL_FRONT(translate_stencil_op(dsa->stencil[0].zfail_op)) |
            VIVS_PE_STENCIL_OP_DEPTH_FAIL_BACK(translate_stencil_op(dsa->stencil[1].zfail_op)) |
            VIVS_PE_STENCIL_OP_PASS_FRONT(translate_stencil_op(dsa->stencil[0].zpass_op)) |
            VIVS_PE_STENCIL_OP_PASS_BACK(translate_stencil_op(dsa->stencil[1].zpass_op)));
    SET_STATE(PE_STENCIL_CONFIG, 
            translate_stencil_mode(dsa->stencil[0].enabled, dsa->stencil[1].enabled) |
            VIVS_PE_STENCIL_CONFIG_MASK_FRONT(dsa->stencil[0].valuemask) | 
            VIVS_PE_STENCIL_CONFIG_WRITE_MASK(dsa->stencil[0].writemask) 
            /* XXX back masks in VIVS_PE_DEPTH_CONFIG_EXT? */
            /* XXX VIVS_PE_STENCIL_CONFIG_REF_FRONT comes from pipe_stencil_ref */
            );
    /* XXX does alpha/stencil test affect PE_COLOR_FORMAT_PARTIAL? */
}

struct compiled_blend_state
{
    uint32_t PE_ALPHA_CONFIG;
    uint32_t PE_COLOR_FORMAT;
    uint32_t PE_LOGIC_OP;
    uint32_t PE_DITHER[2];
};

void compile_blend_state(struct compiled_blend_state *cs, const struct pipe_blend_state *bs)
{
    const struct pipe_rt_blend_state *rt0 = &bs->rt[0];
    bool enable = rt0->blend_enable && !(rt0->rgb_src_factor == PIPE_BLENDFACTOR_ONE && rt0->rgb_dst_factor == PIPE_BLENDFACTOR_ZERO &&
                                         rt0->alpha_src_factor == PIPE_BLENDFACTOR_ONE && rt0->alpha_dst_factor == PIPE_BLENDFACTOR_ZERO);
    bool separate_alpha = enable && !(rt0->rgb_src_factor == rt0->alpha_src_factor &&
                                      rt0->rgb_dst_factor == rt0->alpha_dst_factor);
    bool partial = (rt0->colormask != 15) || enable;
    SET_STATE(PE_ALPHA_CONFIG, 
            (enable ? VIVS_PE_ALPHA_CONFIG_BLEND_ENABLE_COLOR : 0) | 
            (separate_alpha ? VIVS_PE_ALPHA_CONFIG_BLEND_SEPARATE_ALPHA : 0) |
            VIVS_PE_ALPHA_CONFIG_SRC_FUNC_COLOR(translate_blend_factor(rt0->rgb_src_factor)) |
            VIVS_PE_ALPHA_CONFIG_SRC_FUNC_ALPHA(translate_blend_factor(rt0->alpha_src_factor)) |
            VIVS_PE_ALPHA_CONFIG_DST_FUNC_COLOR(translate_blend_factor(rt0->rgb_dst_factor)) |
            VIVS_PE_ALPHA_CONFIG_DST_FUNC_ALPHA(translate_blend_factor(rt0->alpha_dst_factor)) |
            VIVS_PE_ALPHA_CONFIG_EQ_COLOR(translate_blend(rt0->rgb_func)) |
            VIVS_PE_ALPHA_CONFIG_EQ_ALPHA(translate_blend(rt0->alpha_func))
            );
    SET_STATE(PE_COLOR_FORMAT, 
            VIVS_PE_COLOR_FORMAT_COMPONENTS(rt0->colormask) |
            (partial ? VIVS_PE_COLOR_FORMAT_PARTIAL : 0)
            );
    SET_STATE(PE_LOGIC_OP, 
            VIVS_PE_LOGIC_OP_OP(bs->logicop_enable ? bs->logicop_func : LOGIC_OP_COPY) /* 1-to-1 mapping */ |
            0x000E4000 /* ??? */
            );
    /* independent_blend_enable not needed: only one rt supported */
    /* XXX alpha_to_coverage / alpha_to_one? */
    /* XXX dither? VIVS_PE_DITHER(...) and/or VIVS_RS_DITHER(...) on resolve */
}

struct compiled_blend_color
{
    uint32_t PE_ALPHA_BLEND_COLOR;
};

void compile_blend_color(struct compiled_blend_color *cs, const struct pipe_blend_color *bc)
{
    SET_STATE(PE_ALPHA_BLEND_COLOR, 
            VIVS_PE_ALPHA_BLEND_COLOR_R(cfloat_to_uint8(bc->color[0])) |
            VIVS_PE_ALPHA_BLEND_COLOR_G(cfloat_to_uint8(bc->color[1])) |
            VIVS_PE_ALPHA_BLEND_COLOR_B(cfloat_to_uint8(bc->color[2])) |
            VIVS_PE_ALPHA_BLEND_COLOR_A(cfloat_to_uint8(bc->color[3]))
            );
}

struct compiled_stencil_ref
{
    uint32_t PE_STENCIL_CONFIG;
    uint32_t PE_STENCIL_CONFIG_EXT;
};

void compile_stencil_ref(struct compiled_stencil_ref *cs, const struct pipe_stencil_ref *sr)
{
    SET_STATE(PE_STENCIL_CONFIG, 
            VIVS_PE_STENCIL_CONFIG_REF_FRONT(sr->ref_value[0]) 
            /* XXX rest comes from depth_stencil_alpha, need to merge in */
            );
    SET_STATE(PE_STENCIL_CONFIG_EXT, 
            VIVS_PE_STENCIL_CONFIG_EXT_REF_BACK(sr->ref_value[0]) 
            );
}

struct compiled_scissor_state
{
    uint32_t SE_SCISSOR_LEFT; // fixp
    uint32_t SE_SCISSOR_TOP; // fixp
    uint32_t SE_SCISSOR_RIGHT; // fixp
    uint32_t SE_SCISSOR_BOTTOM; // fixp
};

void compile_scissor_state(struct compiled_scissor_state *cs, const struct pipe_scissor_state *ss)
{
    SET_STATE_FIXP(SE_SCISSOR_LEFT, (ss->minx << 16));
    SET_STATE_FIXP(SE_SCISSOR_TOP, (ss->miny << 16));
    SET_STATE_FIXP(SE_SCISSOR_RIGHT, (ss->maxx << 16)-1);
    SET_STATE_FIXP(SE_SCISSOR_BOTTOM, (ss->maxy << 16)-1);
    /* XXX note that this state is only used when rasterizer_state->scissor is on */
}

struct compiled_viewport_state
{
    uint32_t PA_VIEWPORT_SCALE_X;
    uint32_t PA_VIEWPORT_SCALE_Y;
    uint32_t PA_VIEWPORT_SCALE_Z;
    uint32_t PA_VIEWPORT_OFFSET_X;
    uint32_t PA_VIEWPORT_OFFSET_Y;
    uint32_t PA_VIEWPORT_OFFSET_Z;
    uint32_t PE_DEPTH_NEAR;
    uint32_t PE_DEPTH_FAR;
};

void compile_viewport_state(struct compiled_viewport_state *cs, const struct pipe_viewport_state *vs)
{
    /**
     * For Vivante GPU, viewport z transformation is 0..1 to 0..1 instead of -1..1 to 0..1.
     * scaling and translation to 0..1 already happened, so remove that
     *
     * z' = (z * 2 - 1) * scale + translate
     *    = z * (2 * scale) + (translate - scale)
     *
     * scale' = 2 * scale
     * translate' = translate - scale
     */
    SET_STATE_F32(PA_VIEWPORT_SCALE_X, vs->scale[0]); /* XXX must this be fixp? */
    SET_STATE_F32(PA_VIEWPORT_SCALE_Y, vs->scale[1]); /* XXX must this be fixp? */
    SET_STATE_F32(PA_VIEWPORT_SCALE_Z, vs->scale[2] * 2.0f);
    SET_STATE_F32(PA_VIEWPORT_OFFSET_X, vs->translate[0]); /* XXX must this be fixp? */
    SET_STATE_F32(PA_VIEWPORT_OFFSET_Y, vs->translate[1]); /* XXX must this be fixp? */
    SET_STATE_F32(PA_VIEWPORT_OFFSET_Z, vs->translate[2] - vs->scale[2]);

    SET_STATE_F32(PE_DEPTH_NEAR, 0.0); /* not affected if depth mode is Z (as in GL) */
    SET_STATE_F32(PE_DEPTH_FAR, 1.0);
}

struct compiled_sample_mask
{
    uint32_t GL_MULTI_SAMPLE_CONFIG;
};

void compile_sample_mask(struct compiled_sample_mask *cs, unsigned sample_mask)
{
    SET_STATE(GL_MULTI_SAMPLE_CONFIG, 
            /* to be merged with render target state */
            VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_ENABLES(sample_mask));
}

struct compiled_sampler_state
{
    /* sampler offset +4*sampler, interleave when committing state */
    uint32_t TE_SAMPLER_CONFIG0;
    uint32_t TE_SAMPLER_LOD_CONFIG;
    unsigned min_lod, max_lod;
};

void compile_sampler_state(struct compiled_sampler_state *cs, const struct pipe_sampler_state *ss)
{
    SET_STATE(TE_SAMPLER_CONFIG0, 
                /* XXX get from sampler view: VIVS_TE_SAMPLER_CONFIG0_TYPE(TEXTURE_TYPE_2D)| */
                VIVS_TE_SAMPLER_CONFIG0_UWRAP(translate_texture_wrapmode(ss->wrap_s))|
                VIVS_TE_SAMPLER_CONFIG0_VWRAP(translate_texture_wrapmode(ss->wrap_t))|
                VIVS_TE_SAMPLER_CONFIG0_MIN(translate_texture_filter(ss->min_img_filter))|
                VIVS_TE_SAMPLER_CONFIG0_MIP(translate_texture_mipfilter(ss->min_mip_filter))|
                VIVS_TE_SAMPLER_CONFIG0_MAG(translate_texture_filter(ss->mag_img_filter))
                /* XXX get from sampler view: VIVS_TE_SAMPLER_CONFIG0_FORMAT(tex_format) */
            );
    /* VIVS_TE_SAMPLER_CONFIG1 (swizzle, extended format) fully determined by sampler view */
    SET_STATE(TE_SAMPLER_LOD_CONFIG,
            (ss->lod_bias != 0.0 ? VIVS_TE_SAMPLER_LOD_CONFIG_BIAS_ENABLE : 0) | 
            VIVS_TE_SAMPLER_LOD_CONFIG_BIAS(float_to_fixp55(ss->lod_bias))
            );
    cs->min_lod = float_to_fixp55(ss->min_lod);
    cs->max_lod = float_to_fixp55(ss->max_lod);
}

struct compiled_sampler_view
{
    /* sampler offset +4*sampler, interleave when committing state */
    uint32_t TE_SAMPLER_CONFIG0;
    uint32_t TE_SAMPLER_SIZE;
    uint32_t TE_SAMPLER_LOG_SIZE;
    uint32_t TE_SAMPLER_LOD_ADDR[VIVS_TE_SAMPLER_LOD_ADDR__LEN];
    unsigned min_lod, max_lod; /* 5.5 fixp */
};

void compile_sampler_view(struct compiled_sampler_view *cs, const struct pipe_sampler_view *sv)
{
    assert(sv->texture);
    struct pipe_resource *res = sv->texture;
    assert(res != NULL);

    SET_STATE(TE_SAMPLER_CONFIG0, 
                VIVS_TE_SAMPLER_CONFIG0_TYPE(translate_texture_target(res->target)) |
                VIVS_TE_SAMPLER_CONFIG0_FORMAT(translate_texture_format(sv->format)) 
                /* XXX merged with sampler state */
            );
    /* XXX VIVS_TE_SAMPLER_CONFIG1 (swizzle, extended format), swizzle_(r|g|b|a) */
    SET_STATE(TE_SAMPLER_SIZE, 
            VIVS_TE_SAMPLER_SIZE_WIDTH(res->width0)|
            VIVS_TE_SAMPLER_SIZE_HEIGHT(res->height0));
    SET_STATE(TE_SAMPLER_LOG_SIZE, 
            VIVS_TE_SAMPLER_LOG_SIZE_WIDTH(log2_fixp55(res->width0)) |
            VIVS_TE_SAMPLER_LOG_SIZE_HEIGHT(log2_fixp55(res->height0)));
    /* XXX in principle we only have to define lods sv->first_level .. sv->last_level */
    for(int lod=0; lod<=res->last_level; ++lod)
    {
        SET_STATE(TE_SAMPLER_LOD_ADDR[lod], res->levels[lod].address);
    }
    cs->min_lod = sv->u.tex.first_level << 5;
    cs->max_lod = etna_umin(sv->u.tex.last_level, res->last_level) << 5;
}

struct compiled_framebuffer_state
{
    uint32_t GL_MULTI_SAMPLE_CONFIG;
    uint32_t PE_COLOR_FORMAT;
    uint32_t PE_DEPTH_CONFIG;
    uint32_t PE_DEPTH_ADDR;
    uint32_t PE_DEPTH_STRIDE;
    uint32_t PE_HDEPTH_CONTROL;
    uint32_t PE_DEPTH_NORMALIZE;
    uint32_t PE_COLOR_ADDR;
    uint32_t PE_COLOR_STRIDE;
    uint32_t SE_SCISSOR_LEFT; // fixp, restricted by scissor state *if* enabled in rasterizer state
    uint32_t SE_SCISSOR_TOP; // fixp
    uint32_t SE_SCISSOR_RIGHT; // fixp
    uint32_t SE_SCISSOR_BOTTOM; // fixp
    uint32_t TS_MEM_CONFIG; 
    uint32_t TS_DEPTH_CLEAR_VALUE;
    uint32_t TS_DEPTH_STATUS_BASE;
    uint32_t TS_DEPTH_SURFACE_BASE;
    uint32_t TS_COLOR_CLEAR_VALUE;
    uint32_t TS_COLOR_STATUS_BASE;
    uint32_t TS_COLOR_SURFACE_BASE;
};

void compile_framebuffer_state(struct compiled_framebuffer_state *cs, const struct pipe_framebuffer_state *sv)
{
    struct pipe_surface *cbuf = (sv->nr_cbufs > 0) ? sv->cbufs[0] : NULL;
    struct pipe_surface *zsbuf = sv->zsbuf;
    /* XXX rendering with only color or only depth should be possible */
    assert(cbuf != NULL && zsbuf != NULL);
    uint32_t depth_format = translate_depth_format(zsbuf->format);
    unsigned depth_bits = depth_format == VIVS_PE_DEPTH_CONFIG_DEPTH_FORMAT_D16 ? 16 : 24; 
    assert((cbuf->layout & 1) && (zsbuf->layout & 1)); /* color and depth buffer must be at least tiled */
    bool color_supertiled = (cbuf->layout & 2)!=0;
    bool depth_supertiled = (zsbuf->layout & 2)!=0;

    /* XXX support multisample 2X/4X, take care that required width/height is doubled */
    SET_STATE(GL_MULTI_SAMPLE_CONFIG, 
            VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES_NONE
            /* VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_ENABLES(0xf)
            VIVS_GL_MULTI_SAMPLE_CONFIG_UNK12 |
            VIVS_GL_MULTI_SAMPLE_CONFIG_UNK16 */
            );  /* merged with sample_mask */
    SET_STATE(PE_COLOR_FORMAT, 
            VIVS_PE_COLOR_FORMAT_FORMAT(translate_rt_format(cbuf->format)) |
            (color_supertiled ? VIVS_PE_COLOR_FORMAT_SUPER_TILED : 0) /* XXX depends on layout */
            /* XXX VIVS_PE_COLOR_FORMAT_PARTIAL and the rest comes from depth_stencil_alpha */
            ); /* merged with depth_stencil_alpha */
    SET_STATE(PE_DEPTH_CONFIG, 
            depth_format |
            (depth_supertiled ? VIVS_PE_DEPTH_CONFIG_SUPER_TILED : 0) | /* XXX depends on layout */
            VIVS_PE_DEPTH_CONFIG_EARLY_Z |
            VIVS_PE_DEPTH_CONFIG_DEPTH_MODE_Z /* XXX set to NONE if no Z buffer? */
            /* VIVS_PE_DEPTH_CONFIG_ONLY_DEPTH */
            ); /* merged with depth_stencil_alpha */

    SET_STATE(PE_DEPTH_ADDR, zsbuf->surf.address);
    SET_STATE(PE_DEPTH_STRIDE, zsbuf->surf.stride);
    SET_STATE(PE_HDEPTH_CONTROL, VIVS_PE_HDEPTH_CONTROL_FORMAT_DISABLED);
    SET_STATE_F32(PE_DEPTH_NORMALIZE, exp2f(depth_bits) - 1.0f);
    SET_STATE(PE_COLOR_ADDR, cbuf->surf.address);
    SET_STATE(PE_COLOR_STRIDE, cbuf->surf.stride);
    
    SET_STATE_FIXP(SE_SCISSOR_LEFT, 0); /* affected by rasterizer and scissor state as well */
    SET_STATE_FIXP(SE_SCISSOR_TOP, 0);
    SET_STATE_FIXP(SE_SCISSOR_RIGHT, (sv->width << 16)-1);
    SET_STATE_FIXP(SE_SCISSOR_BOTTOM, (sv->height << 16)-1);

    /* Set up TS as well. Warning: this is shared with RS */
    SET_STATE(TS_MEM_CONFIG, 
            VIVS_TS_MEM_CONFIG_DEPTH_FAST_CLEAR |
            VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR |
            (depth_bits == 16 ? VIVS_TS_MEM_CONFIG_DEPTH_16BPP : 0) | 
            VIVS_TS_MEM_CONFIG_DEPTH_COMPRESSION /* |
            VIVS_TS_MEM_CONFIG_MSAA | 
            translate_msaa_format(cbuf->format) */);
    SET_STATE(TS_DEPTH_CLEAR_VALUE, zsbuf->clear_value);
    SET_STATE(TS_DEPTH_STATUS_BASE, zsbuf->surf.ts_address);
    SET_STATE(TS_DEPTH_SURFACE_BASE, zsbuf->surf.address);
    SET_STATE(TS_COLOR_CLEAR_VALUE, cbuf->clear_value);
    SET_STATE(TS_COLOR_STATUS_BASE, cbuf->surf.ts_address);
    SET_STATE(TS_COLOR_SURFACE_BASE, cbuf->surf.address);
}

struct compiled_vertex_elements_state
{
    unsigned num_elements;
    uint32_t FE_VERTEX_ELEMENT_CONFIG[VIVS_FE_VERTEX_ELEMENT_CONFIG__LEN];
};

void compile_vertex_elements_state(struct compiled_vertex_elements_state *cs, unsigned num_elements, const struct pipe_vertex_element * elements)
{
    /* VERTEX_ELEMENT_STRIDE is in pipe_vertex_buffer */
    cs->num_elements = num_elements;
    for(unsigned idx=0; idx<num_elements; ++idx)
    {
        unsigned element_size = vertex_element_size(elements[idx].src_format);
        unsigned end_offset = elements[idx].src_offset + element_size;
        assert(element_size != 0 && end_offset <= 256);
        /* check whether next element is consecutive to this one */
        bool nonconsecutive = (idx == (num_elements-1)) || 
                    elements[idx+1].vertex_buffer_index != elements[idx].vertex_buffer_index ||
                    end_offset != elements[idx+1].src_offset;
        SET_STATE(FE_VERTEX_ELEMENT_CONFIG[idx], 
                (nonconsecutive ? VIVS_FE_VERTEX_ELEMENT_CONFIG_NONCONSECUTIVE : 0) |
                translate_vertex_format_type(elements[idx].src_format) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_NUM(vertex_format_num(elements[idx].src_format)) |
                translate_vertex_format_normalize(elements[idx].src_format) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_ENDIAN(ENDIAN_MODE_NO_SWAP) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_STREAM(elements[idx].vertex_buffer_index) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_START(elements[idx].src_offset) |
                VIVS_FE_VERTEX_ELEMENT_CONFIG_END(end_offset));
    }
}

struct compiled_set_vertex_buffer
{
    unsigned num_buffers;
    uint32_t FE_VERTEX_STREAM_CONTROL;
    uint32_t FE_VERTEX_STREAM_BASE_ADDR;
};

void compile_set_vertex_buffer(struct compiled_set_vertex_buffer *cs, unsigned num_buffers, const struct pipe_vertex_buffer * vb)
{
    /* XXX figure out multi-stream VIS_FE_VERTEX_STREAMS(..) */
    cs->num_buffers = num_buffers;
    if(num_buffers > 0)
    {
        assert(vb[0].buffer); /* XXX user_buffer */
        SET_STATE(FE_VERTEX_STREAM_CONTROL, 
                VIVS_FE_VERTEX_STREAM_CONTROL_VERTEX_STRIDE(vb[0].stride));
        SET_STATE(FE_VERTEX_STREAM_BASE_ADDR, vb[0].buffer->levels[0].address + vb[0].buffer_offset);
    } 
}

struct compiled_set_index_buffer
{
    uint32_t FE_INDEX_STREAM_CONTROL;
    uint32_t FE_INDEX_STREAM_BASE_ADDR;
};

void compile_set_index_buffer(struct compiled_set_index_buffer *cs, const struct pipe_index_buffer * ib)
{
    if(ib == NULL)
    {
        SET_STATE(FE_INDEX_STREAM_CONTROL, 0);
        SET_STATE(FE_INDEX_STREAM_BASE_ADDR, 0);
    } else
    {
        assert(ib->buffer); /* XXX user_buffer */
        SET_STATE(FE_INDEX_STREAM_CONTROL, 
                translate_index_size(ib->index_size));
        SET_STATE(FE_INDEX_STREAM_BASE_ADDR, ib->buffer->levels[0].address + ib->offset);
    }
}
/*********************************************************************/
/* group all current CSOs */
enum
{
    ETNA_STATE_BLEND = 0x1,
    ETNA_STATE_SAMPLERS = 0x2,
    ETNA_STATE_RASTERIZER = 0x4,
    ETNA_STATE_DSA = 0x8,
    ETNA_STATE_VERTEX_ELEMENTS = 0x10,
    ETNA_STATE_BLEND_COLOR = 0x20,
    ETNA_STATE_STENCIL_REF = 0x40,
    ETNA_STATE_SAMPLE_MASK = 0x80,
    ETNA_STATE_VIEWPORT = 0x100,
    ETNA_STATE_FRAMEBUFFER = 0x200,
    ETNA_STATE_SCISSOR = 0x400,
    ETNA_STATE_SAMPLER_VIEWS = 0x800,
    ETNA_STATE_VERTEX_BUFFERS = 0x1000,
    ETNA_STATE_INDEX_BUFFER = 0x2000,
    ETNA_STATE_TS = 0x4000 /* RS blit operations affect TS */
};
struct etna_3d_state
{
    unsigned num_vertex_elements; /* number of elements in FE_VERTEX_ELEMENT_CONFIG */
    
    uint32_t /*00600*/ FE_VERTEX_ELEMENT_CONFIG[VIVS_FE_VERTEX_ELEMENT_CONFIG__LEN];
    uint32_t /*00644*/ FE_INDEX_STREAM_BASE_ADDR;
    uint32_t /*00648*/ FE_INDEX_STREAM_CONTROL;
    uint32_t /*0064C*/ FE_VERTEX_STREAM_BASE_ADDR;
    uint32_t /*00650*/ FE_VERTEX_STREAM_CONTROL;
    
    uint32_t /*00A00*/ PA_VIEWPORT_SCALE_X;
    uint32_t /*00A04*/ PA_VIEWPORT_SCALE_Y;
    uint32_t /*00A08*/ PA_VIEWPORT_SCALE_Z;
    uint32_t /*00A0C*/ PA_VIEWPORT_OFFSET_X;
    uint32_t /*00A10*/ PA_VIEWPORT_OFFSET_Y;
    uint32_t /*00A14*/ PA_VIEWPORT_OFFSET_Z;
    uint32_t /*00A18*/ PA_LINE_WIDTH;
    uint32_t /*00A1C*/ PA_POINT_SIZE;
    uint32_t /*00A34*/ PA_CONFIG;

    uint32_t /*00C00*/ SE_SCISSOR_LEFT; // fixp
    uint32_t /*00C04*/ SE_SCISSOR_TOP; // fixp
    uint32_t /*00C08*/ SE_SCISSOR_RIGHT; // fixp
    uint32_t /*00C0C*/ SE_SCISSOR_BOTTOM; // fixp
    uint32_t /*00C10*/ SE_DEPTH_SCALE;
    uint32_t /*00C14*/ SE_DEPTH_BIAS;
    uint32_t /*00C18*/ SE_CONFIG;

    uint32_t /*01400*/ PE_DEPTH_CONFIG;
    uint32_t /*01404*/ PE_DEPTH_NEAR;
    uint32_t /*01408*/ PE_DEPTH_FAR;
    uint32_t /*0140C*/ PE_DEPTH_NORMALIZE;
    uint32_t /*01410*/ PE_DEPTH_ADDR;
    uint32_t /*01414*/ PE_DEPTH_STRIDE;
    uint32_t /*01418*/ PE_STENCIL_OP;
    uint32_t /*0141C*/ PE_STENCIL_CONFIG;
    uint32_t /*01420*/ PE_ALPHA_OP;
    uint32_t /*01424*/ PE_ALPHA_BLEND_COLOR;
    uint32_t /*01428*/ PE_ALPHA_CONFIG;
    uint32_t /*0142C*/ PE_COLOR_FORMAT;
    uint32_t /*01430*/ PE_COLOR_ADDR;
    uint32_t /*01434*/ PE_COLOR_STRIDE;
    uint32_t /*01454*/ PE_HDEPTH_CONTROL;
    uint32_t /*014A0*/ PE_STENCIL_CONFIG_EXT;
    uint32_t /*014A4*/ PE_LOGIC_OP;
    uint32_t /*014A8*/ PE_DITHER[2];
    
    uint32_t /*01604*/ RS_CONFIG;
    uint32_t /*01608*/ RS_SOURCE_ADDR;
    uint32_t /*0160C*/ RS_SOURCE_STRIDE;
    uint32_t /*01610*/ RS_DEST_ADDR;
    uint32_t /*01614*/ RS_DEST_STRIDE;
    uint32_t /*01620*/ RS_WINDOW_SIZE;
    uint32_t /*01630*/ RS_DITHER[2];
    uint32_t /*0163C*/ RS_CLEAR_CONTROL;
    uint32_t /*01640*/ RS_FILL_VALUE[4];

    uint32_t /*01654*/ TS_MEM_CONFIG; 
    uint32_t /*01658*/ TS_COLOR_STATUS_BASE;
    uint32_t /*0165C*/ TS_COLOR_SURFACE_BASE;
    uint32_t /*01660*/ TS_COLOR_CLEAR_VALUE;
    uint32_t /*01664*/ TS_DEPTH_STATUS_BASE;
    uint32_t /*01668*/ TS_DEPTH_SURFACE_BASE;
    uint32_t /*0166C*/ TS_DEPTH_CLEAR_VALUE;
    
    uint32_t /*016A0*/ RS_EXTRA_CONFIG;
    
    uint32_t /*02000*/ TE_SAMPLER_CONFIG0[VIVS_TE_SAMPLER__LEN];
    uint32_t /*02040*/ TE_SAMPLER_SIZE[VIVS_TE_SAMPLER__LEN];
    uint32_t /*02080*/ TE_SAMPLER_LOG_SIZE[VIVS_TE_SAMPLER__LEN];
    uint32_t /*020C0*/ TE_SAMPLER_LOD_CONFIG[VIVS_TE_SAMPLER__LEN];
    uint32_t /*02400*/ TE_SAMPLER_LOD_ADDR[VIVS_TE_SAMPLER_LOD_ADDR__LEN][VIVS_TE_SAMPLER__LEN];
    
    uint32_t /*03818*/ GL_MULTI_SAMPLE_CONFIG;
};

struct etna_pipe_context
{
    unsigned dirty_bits;
    
    /* bindable state */
    struct compiled_blend_state *blend;
    unsigned num_samplers;/*   XXX separate fragment/vertex sampler states */
    struct compiled_sampler_state *sampler[PIPE_MAX_SAMPLERS];
    struct compiled_rasterizer_state *rasterizer;
    struct compiled_depth_stencil_alpha_state *depth_stencil_alpha;
    struct compiled_vertex_elements_state *vertex_elements;

    /* parameter-like state */
    struct compiled_blend_color blend_color;
    struct compiled_stencil_ref stencil_ref;
    struct compiled_sample_mask sample_mask;
    struct compiled_framebuffer_state framebuffer;
    struct compiled_scissor_state scissor;
    struct compiled_viewport_state viewport;
    unsigned num_sampler_views; /*   XXX separate fragment/vertex sampler states */
    struct compiled_sampler_view sampler_view[PIPE_MAX_SAMPLERS];
    struct compiled_set_vertex_buffer vertex_buffer;
    struct compiled_set_index_buffer index_buffer;

    /* cached state */
    struct etna_3d_state gpu3d;
};

/*********************************************************************/
/* submit RS state, without any processing */
void submit_rs_state(etna_ctx *restrict ctx, const struct compiled_rs_state *cs)
{
    etna_reserve(ctx, 20);
    /*0 */ ETNA_EMIT_LOAD_STATE(ctx, VIVS_RS_CONFIG>>2, 5, 0);
    /*1 */ ETNA_EMIT(ctx, cs->RS_CONFIG);
    /*2 */ ETNA_EMIT(ctx, cs->RS_SOURCE_ADDR);
    /*3 */ ETNA_EMIT(ctx, cs->RS_SOURCE_STRIDE);
    /*4 */ ETNA_EMIT(ctx, cs->RS_DEST_ADDR);
    /*5 */ ETNA_EMIT(ctx, cs->RS_DEST_STRIDE);
    /*6 */ ETNA_EMIT_LOAD_STATE(ctx, VIVS_RS_WINDOW_SIZE>>2, 1, 0);
    /*7 */ ETNA_EMIT(ctx, cs->RS_WINDOW_SIZE);
    /*8 */ ETNA_EMIT_LOAD_STATE(ctx, VIVS_RS_DITHER(0)>>2, 2, 0);
    /*9 */ ETNA_EMIT(ctx, cs->RS_DITHER[0]);
    /*10*/ ETNA_EMIT(ctx, cs->RS_DITHER[1]);
    /*11*/ ETNA_EMIT(ctx, 0xbabb1e); /* pad */
    /*12*/ ETNA_EMIT_LOAD_STATE(ctx, VIVS_RS_CLEAR_CONTROL>>2, 5, 0);
    /*13*/ ETNA_EMIT(ctx, cs->RS_CLEAR_CONTROL);
    /*14*/ ETNA_EMIT(ctx, cs->RS_FILL_VALUE[0]);
    /*15*/ ETNA_EMIT(ctx, cs->RS_FILL_VALUE[1]);
    /*16*/ ETNA_EMIT(ctx, cs->RS_FILL_VALUE[2]);
    /*17*/ ETNA_EMIT(ctx, cs->RS_FILL_VALUE[3]);
    /*18*/ ETNA_EMIT_LOAD_STATE(ctx, VIVS_RS_EXTRA_CONFIG>>2, 1, 0);
    /*19*/ ETNA_EMIT(ctx, cs->RS_EXTRA_CONFIG);
}

void sync_context(etna_ctx *restrict ctx, struct etna_pipe_context *restrict e)
{
    /* weave state */
    /* Number of samplers to program */
    unsigned num_samplers = etna_umin(e->num_samplers, e->num_sampler_views);
    uint32_t dirty = e->dirty_bits;
    e->gpu3d.num_vertex_elements = e->vertex_elements->num_elements;

    /* XXX todo: 
     * - caching, don't re-emit cached state ?
     * - group consecutive states
     * - update context
     * - flush texture? etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_TEXTURE);
     * - flush depth/color on depth/color framebuffer change
     */
#define EMIT_STATE(state_name, dest_field, src_value) etna_set_state(ctx, VIVS_##state_name, (src_value)); 
#define EMIT_STATE_FIXP(state_name, dest_field, src_value) etna_set_state_fixp(ctx, VIVS_##state_name, (src_value)); 
    /* The following code is mostly generated by gen_merge_state.py, to emit state in sorted order by address */
    /* manual changes: 
     * - scissor fixp
     * - num vertex elements
     * - scissor handling
     * - num samplers
     * - texture lod 
     */
    if(dirty & (ETNA_STATE_VERTEX_ELEMENTS))
    {
        for(int x=0; x<e->vertex_elements->num_elements; ++x)
        {
            /*00600*/ EMIT_STATE(FE_VERTEX_ELEMENT_CONFIG(x), FE_VERTEX_ELEMENT_CONFIG[x], e->vertex_elements->FE_VERTEX_ELEMENT_CONFIG[x]);
        }
    }
    if(dirty & (ETNA_STATE_INDEX_BUFFER))
    {
        /*00644*/ EMIT_STATE(FE_INDEX_STREAM_BASE_ADDR, FE_INDEX_STREAM_BASE_ADDR, e->index_buffer.FE_INDEX_STREAM_BASE_ADDR);
        /*00648*/ EMIT_STATE(FE_INDEX_STREAM_CONTROL, FE_INDEX_STREAM_CONTROL, e->index_buffer.FE_INDEX_STREAM_CONTROL);
    }
    if(dirty & (ETNA_STATE_VERTEX_BUFFERS))
    {
        /*0064C*/ EMIT_STATE(FE_VERTEX_STREAM_BASE_ADDR, FE_VERTEX_STREAM_BASE_ADDR, e->vertex_buffer.FE_VERTEX_STREAM_BASE_ADDR);
        /*00650*/ EMIT_STATE(FE_VERTEX_STREAM_CONTROL, FE_VERTEX_STREAM_CONTROL, e->vertex_buffer.FE_VERTEX_STREAM_CONTROL);
    }
    if(dirty & (ETNA_STATE_VIEWPORT))
    {
        /*00A00*/ EMIT_STATE(PA_VIEWPORT_SCALE_X, PA_VIEWPORT_SCALE_X, e->viewport.PA_VIEWPORT_SCALE_X);
        /*00A04*/ EMIT_STATE(PA_VIEWPORT_SCALE_Y, PA_VIEWPORT_SCALE_Y, e->viewport.PA_VIEWPORT_SCALE_Y);
        /*00A08*/ EMIT_STATE(PA_VIEWPORT_SCALE_Z, PA_VIEWPORT_SCALE_Z, e->viewport.PA_VIEWPORT_SCALE_Z);
        /*00A0C*/ EMIT_STATE(PA_VIEWPORT_OFFSET_X, PA_VIEWPORT_OFFSET_X, e->viewport.PA_VIEWPORT_OFFSET_X);
        /*00A10*/ EMIT_STATE(PA_VIEWPORT_OFFSET_Y, PA_VIEWPORT_OFFSET_Y, e->viewport.PA_VIEWPORT_OFFSET_Y);
        /*00A14*/ EMIT_STATE(PA_VIEWPORT_OFFSET_Z, PA_VIEWPORT_OFFSET_Z, e->viewport.PA_VIEWPORT_OFFSET_Z);
    }
    if(dirty & (ETNA_STATE_RASTERIZER))
    {
        /*00A18*/ EMIT_STATE(PA_LINE_WIDTH, PA_LINE_WIDTH, e->rasterizer->PA_LINE_WIDTH);
        /*00A1C*/ EMIT_STATE(PA_POINT_SIZE, PA_POINT_SIZE, e->rasterizer->PA_POINT_SIZE);
        /*00A34*/ EMIT_STATE(PA_CONFIG, PA_CONFIG, e->rasterizer->PA_CONFIG);
    }
    if(dirty & (ETNA_STATE_SCISSOR | ETNA_STATE_FRAMEBUFFER | ETNA_STATE_RASTERIZER))
    {
        /* this is a bit of a mess: rasterizer->scissor determines whether to use only the
         * framebuffer scissor, or specific scissor state, so the logic spans three CSOs 
         */
        uint32_t scissor_left = e->framebuffer.SE_SCISSOR_LEFT;
        uint32_t scissor_top = e->framebuffer.SE_SCISSOR_TOP;
        uint32_t scissor_right = e->framebuffer.SE_SCISSOR_RIGHT;
        uint32_t scissor_bottom = e->framebuffer.SE_SCISSOR_BOTTOM;
        if(e->rasterizer->scissor)
        {
            scissor_left = etna_umax(e->scissor.SE_SCISSOR_LEFT, scissor_left);
            scissor_top = etna_umax(e->scissor.SE_SCISSOR_TOP, scissor_top);
            scissor_right = etna_umax(e->scissor.SE_SCISSOR_RIGHT, scissor_right);
            scissor_bottom = etna_umax(e->scissor.SE_SCISSOR_RIGHT, scissor_bottom);
        }
        /*00C00*/ EMIT_STATE_FIXP(SE_SCISSOR_LEFT, SE_SCISSOR_LEFT, scissor_left);
        /*00C04*/ EMIT_STATE_FIXP(SE_SCISSOR_TOP, SE_SCISSOR_TOP, scissor_top);
        /*00C08*/ EMIT_STATE_FIXP(SE_SCISSOR_RIGHT, SE_SCISSOR_RIGHT, scissor_right);
        /*00C0C*/ EMIT_STATE_FIXP(SE_SCISSOR_BOTTOM, SE_SCISSOR_BOTTOM, scissor_bottom);
    }
    if(dirty & (ETNA_STATE_RASTERIZER))
    {
        /*00C10*/ EMIT_STATE(SE_DEPTH_SCALE, SE_DEPTH_SCALE, e->rasterizer->SE_DEPTH_SCALE);
        /*00C14*/ EMIT_STATE(SE_DEPTH_BIAS, SE_DEPTH_BIAS, e->rasterizer->SE_DEPTH_BIAS);
        /*00C18*/ EMIT_STATE(SE_CONFIG, SE_CONFIG, e->rasterizer->SE_CONFIG);
    }
    if(dirty & (ETNA_STATE_DSA | ETNA_STATE_FRAMEBUFFER))
    {
        /*01400*/ EMIT_STATE(PE_DEPTH_CONFIG, PE_DEPTH_CONFIG, e->depth_stencil_alpha->PE_DEPTH_CONFIG | e->framebuffer.PE_DEPTH_CONFIG);
    }
    if(dirty & (ETNA_STATE_VIEWPORT))
    {
        /*01404*/ EMIT_STATE(PE_DEPTH_NEAR, PE_DEPTH_NEAR, e->viewport.PE_DEPTH_NEAR);
        /*01408*/ EMIT_STATE(PE_DEPTH_FAR, PE_DEPTH_FAR, e->viewport.PE_DEPTH_FAR);
    }
    if(dirty & (ETNA_STATE_FRAMEBUFFER))
    {
        /*0140C*/ EMIT_STATE(PE_DEPTH_NORMALIZE, PE_DEPTH_NORMALIZE, e->framebuffer.PE_DEPTH_NORMALIZE);
        /*01410*/ EMIT_STATE(PE_DEPTH_ADDR, PE_DEPTH_ADDR, e->framebuffer.PE_DEPTH_ADDR);
        /*01414*/ EMIT_STATE(PE_DEPTH_STRIDE, PE_DEPTH_STRIDE, e->framebuffer.PE_DEPTH_STRIDE);
    }
    if(dirty & (ETNA_STATE_DSA))
    {
        /*01418*/ EMIT_STATE(PE_STENCIL_OP, PE_STENCIL_OP, e->depth_stencil_alpha->PE_STENCIL_OP);
    }
    if(dirty & (ETNA_STATE_DSA | ETNA_STATE_STENCIL_REF))
    {
        /*0141C*/ EMIT_STATE(PE_STENCIL_CONFIG, PE_STENCIL_CONFIG, e->depth_stencil_alpha->PE_STENCIL_CONFIG | e->stencil_ref.PE_STENCIL_CONFIG);
    }
    if(dirty & (ETNA_STATE_DSA))
    {
        /*01420*/ EMIT_STATE(PE_ALPHA_OP, PE_ALPHA_OP, e->depth_stencil_alpha->PE_ALPHA_OP);
    }
    if(dirty & (ETNA_STATE_BLEND_COLOR))
    {
        /*01424*/ EMIT_STATE(PE_ALPHA_BLEND_COLOR, PE_ALPHA_BLEND_COLOR, e->blend_color.PE_ALPHA_BLEND_COLOR);
    }
    if(dirty & (ETNA_STATE_BLEND))
    {
        /*01428*/ EMIT_STATE(PE_ALPHA_CONFIG, PE_ALPHA_CONFIG, e->blend->PE_ALPHA_CONFIG);
    }
    if(dirty & (ETNA_STATE_BLEND | ETNA_STATE_FRAMEBUFFER))
    {
        /*0142C*/ EMIT_STATE(PE_COLOR_FORMAT, PE_COLOR_FORMAT, e->blend->PE_COLOR_FORMAT | e->framebuffer.PE_COLOR_FORMAT);
    }
    if(dirty & (ETNA_STATE_FRAMEBUFFER))
    {
        /*01430*/ EMIT_STATE(PE_COLOR_ADDR, PE_COLOR_ADDR, e->framebuffer.PE_COLOR_ADDR);
        /*01434*/ EMIT_STATE(PE_COLOR_STRIDE, PE_COLOR_STRIDE, e->framebuffer.PE_COLOR_STRIDE);
        /*01454*/ EMIT_STATE(PE_HDEPTH_CONTROL, PE_HDEPTH_CONTROL, e->framebuffer.PE_HDEPTH_CONTROL);
    }
    if(dirty & (ETNA_STATE_STENCIL_REF))
    {
        /*014A0*/ EMIT_STATE(PE_STENCIL_CONFIG_EXT, PE_STENCIL_CONFIG_EXT, e->stencil_ref.PE_STENCIL_CONFIG_EXT);
    }
    if(dirty & (ETNA_STATE_BLEND))
    {
        /*014A4*/ EMIT_STATE(PE_LOGIC_OP, PE_LOGIC_OP, e->blend->PE_LOGIC_OP);
        for(int x=0; x<2; ++x)
        {
            /*014A8*/ EMIT_STATE(PE_DITHER(x), PE_DITHER[x], e->blend->PE_DITHER[x]);
        }
    }
    if(dirty & (ETNA_STATE_FRAMEBUFFER | ETNA_STATE_TS))
    {
        /*01654*/ EMIT_STATE(TS_MEM_CONFIG, TS_MEM_CONFIG, e->framebuffer.TS_MEM_CONFIG);
        /*01658*/ EMIT_STATE(TS_COLOR_STATUS_BASE, TS_COLOR_STATUS_BASE, e->framebuffer.TS_COLOR_STATUS_BASE);
        /*0165C*/ EMIT_STATE(TS_COLOR_SURFACE_BASE, TS_COLOR_SURFACE_BASE, e->framebuffer.TS_COLOR_SURFACE_BASE);
        /*01660*/ EMIT_STATE(TS_COLOR_CLEAR_VALUE, TS_COLOR_CLEAR_VALUE, e->framebuffer.TS_COLOR_CLEAR_VALUE);
        /*01664*/ EMIT_STATE(TS_DEPTH_STATUS_BASE, TS_DEPTH_STATUS_BASE, e->framebuffer.TS_DEPTH_STATUS_BASE);
        /*01668*/ EMIT_STATE(TS_DEPTH_SURFACE_BASE, TS_DEPTH_SURFACE_BASE, e->framebuffer.TS_DEPTH_SURFACE_BASE);
        /*0166C*/ EMIT_STATE(TS_DEPTH_CLEAR_VALUE, TS_DEPTH_CLEAR_VALUE, e->framebuffer.TS_DEPTH_CLEAR_VALUE);
    }
    if(dirty & (ETNA_STATE_SAMPLER_VIEWS | ETNA_STATE_SAMPLERS))
    {
        for(int x=0; x<num_samplers; ++x)
        {
            /*02000*/ EMIT_STATE(TE_SAMPLER_CONFIG0(x), TE_SAMPLER_CONFIG0[x], e->sampler[x]->TE_SAMPLER_CONFIG0 | e->sampler_view[x].TE_SAMPLER_CONFIG0);
        }
        for(int x=num_samplers; x<12; ++x)
        {
            /*02000*/ EMIT_STATE(TE_SAMPLER_CONFIG0(x), TE_SAMPLER_CONFIG0[x], 0); /* set unused samplers config to 0 */
        }
    }
    if(dirty & (ETNA_STATE_SAMPLER_VIEWS))
    {
        for(int x=0; x<num_samplers; ++x)
        {
            /*02040*/ EMIT_STATE(TE_SAMPLER_SIZE(x), TE_SAMPLER_SIZE[x], e->sampler_view[x].TE_SAMPLER_SIZE);
        }
        for(int x=0; x<num_samplers; ++x)
        {
            /*02080*/ EMIT_STATE(TE_SAMPLER_LOG_SIZE(x), TE_SAMPLER_LOG_SIZE[x], e->sampler_view[x].TE_SAMPLER_LOG_SIZE);
        }
    }
    if(dirty & (ETNA_STATE_SAMPLER_VIEWS | ETNA_STATE_SAMPLERS))
    {
        for(int x=0; x<num_samplers; ++x)
        {
            /* min and max lod is determined both by the sampler and the view */
            /*020C0*/ EMIT_STATE(TE_SAMPLER_LOD_CONFIG(x), TE_SAMPLER_LOD_CONFIG[x], 
                    e->sampler[x]->TE_SAMPLER_LOD_CONFIG | 
                    VIVS_TE_SAMPLER_LOD_CONFIG_MAX(etna_umin(e->sampler[x]->max_lod, e->sampler_view[x].max_lod)) | 
                    VIVS_TE_SAMPLER_LOD_CONFIG_MIN(etna_umax(e->sampler[x]->min_lod, e->sampler_view[x].min_lod))); 
        }
    }
    if(dirty & (ETNA_STATE_SAMPLER_VIEWS))
    {
        for(int y=0; y<14; ++y)
        {
            for(int x=0; x<num_samplers; ++x)
            {
                /*02400*/ EMIT_STATE(TE_SAMPLER_LOD_ADDR(x, y), TE_SAMPLER_LOD_ADDR[y][x], e->sampler_view[x].TE_SAMPLER_LOD_ADDR[y]);
            }
        }
    }
    if(dirty & (ETNA_STATE_FRAMEBUFFER | ETNA_STATE_SAMPLE_MASK))
    {
        /*03818*/ EMIT_STATE(GL_MULTI_SAMPLE_CONFIG, GL_MULTI_SAMPLE_CONFIG, e->sample_mask.GL_MULTI_SAMPLE_CONFIG | e->framebuffer.GL_MULTI_SAMPLE_CONFIG);
    }
#undef EMIT_STATE
    e->dirty_bits = 0;
}

/*********************************************************************/
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
    printf("Loading compressed texture (format %i, %ix%i) log_width=%i log_height=%i\n", dds->fmt, tex_base_width, tex_base_height, tex_base_log_width, tex_base_log_height);
    if(dds->fmt == FMT_X8R8G8B8 || dds->fmt == FMT_A8R8G8B8)
    {
        for(int ix=0; ix<dds->num_mipmaps; ++ix)
        {
            printf("%08x: Tiling mipmap %i (%ix%i)\n", dds->slices[0][ix].offset, ix, dds->slices[0][ix].width, dds->slices[0][ix].height);
            etna_texture_tile((void*)((size_t)tex->logical + dds->slices[0][ix].offset), 
                    dds->slices[0][ix].data, dds->slices[0][ix].width, dds->slices[0][ix].height, dds->slices[0][ix].stride, 4);
        }
        tex_format = PIPE_FORMAT_B8G8R8X8_UNORM;
    } else if(dds->fmt == FMT_DXT1 || dds->fmt == FMT_DXT3 || dds->fmt == FMT_DXT5 || dds->fmt == FMT_ETC1)
    {
        printf("Uploading compressed texture\n");
        memcpy(tex->logical, dds->data, dds->size);
        switch(dds->fmt)
        {
        case FMT_DXT1: tex_format = PIPE_FORMAT_DXT1_RGB; break;
        case FMT_DXT3: tex_format = PIPE_FORMAT_DXT3_RGBA; break;
        case FMT_DXT5: tex_format = PIPE_FORMAT_DXT5_RGBA; break;
        case FMT_ETC1: tex_format = PIPE_FORMAT_ETC1_RGB8; break;
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

    /* resources */
    struct pipe_resource *rt_resource = ETNA_NEW(struct pipe_resource);
    rt_resource->target = PIPE_TEXTURE_2D;
    rt_resource->format = PIPE_FORMAT_B8G8R8X8_UNORM;
    rt_resource->width0 = width;
    rt_resource->height0 = height;
    rt_resource->depth0 = 1;
    rt_resource->array_size = 1;
    rt_resource->last_level = 0;
    rt_resource->layout = ETNA_LAYOUT_SUPERTILED;
    rt_resource->padded_width = padded_width;
    rt_resource->padded_height = padded_height;
    rt_resource->surface = rt;
    rt_resource->ts = rt_ts;
    rt_resource->levels[0].address = rt_resource->surface->address;
    rt_resource->levels[0].logical = rt_resource->surface->logical;
    rt_resource->levels[0].ts_address = rt_resource->ts->address;
    rt_resource->levels[0].stride = 4 * padded_width;
    
    struct pipe_resource *z_resource = ETNA_NEW(struct pipe_resource);
    z_resource->target = PIPE_TEXTURE_2D;
    z_resource->format = PIPE_FORMAT_Z16_UNORM;
    z_resource->width0 = width;
    z_resource->height0 = height;
    z_resource->depth0 = 1;
    z_resource->array_size = 1;
    z_resource->last_level = 0;
    z_resource->layout = ETNA_LAYOUT_SUPERTILED;
    z_resource->padded_width = padded_width;
    z_resource->padded_height = padded_height;
    z_resource->surface = z;
    z_resource->ts = z_ts;
    z_resource->levels[0].address = z_resource->surface->address;
    z_resource->levels[0].logical = z_resource->surface->logical;
    z_resource->levels[0].ts_address = z_resource->ts->address;
    z_resource->levels[0].stride = 2 * padded_width;

    struct pipe_resource *tex_resource = ETNA_NEW(struct pipe_resource);
    tex_resource->target = PIPE_TEXTURE_2D;
    tex_resource->format = tex_format;
    tex_resource->width0 = tex_base_width;
    tex_resource->height0 = tex_base_height;
    tex_resource->depth0 = 1;
    tex_resource->array_size = 1;
    tex_resource->last_level = dds->num_mipmaps - 1;
    tex_resource->layout = ETNA_LAYOUT_TILED;
    tex_resource->padded_width = tex_resource->width0;
    tex_resource->padded_height = tex_resource->height0;
    tex_resource->surface = tex;
    tex_resource->ts = 0;
    for(int ix=0; ix<dds->num_mipmaps; ++ix)
    {
        tex_resource->levels[ix].address = tex_resource->surface->address + dds->slices[0][ix].offset;
        tex_resource->levels[ix].logical = tex_resource->surface->logical + dds->slices[0][ix].offset;
        tex_resource->levels[ix].ts_address = 0;
        tex_resource->levels[ix].stride = dds->slices[0][ix].stride;
    }

    struct pipe_resource *vtx_resource = ETNA_NEW(struct pipe_resource);
    vtx_resource->target = PIPE_BUFFER;
    vtx_resource->format = PIPE_FORMAT_NONE;
    vtx_resource->width0 = 0;
    vtx_resource->height0 = 0;
    vtx_resource->depth0 = 0;
    vtx_resource->array_size = vtx->size; //?
    vtx_resource->last_level = 0;
    vtx_resource->layout = ETNA_LAYOUT_LINEAR;
    vtx_resource->padded_width = 0;
    vtx_resource->padded_height = 0;
    vtx_resource->surface = vtx;
    vtx_resource->ts = 0;
    vtx_resource->levels[0].address = vtx_resource->surface->address;
    vtx_resource->levels[0].logical = vtx_resource->surface->logical;
    vtx_resource->levels[0].ts_address = 0;
    vtx_resource->levels[0].stride = 0;

    /* pre-compile RS states, one to clear buffer, and one for each back/front buffer */
    struct compiled_rs_state clear_rt = {};
    struct compiled_rs_state copy_to_screen[ETNA_BSWAP_NUM_BUFFERS] = {};
    compile_rs_state(&clear_rt, &(struct rs_state){
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

    for(int bi=0; bi<ETNA_BSWAP_NUM_BUFFERS; ++bi)
    {
        compile_rs_state(&copy_to_screen[bi], &(struct rs_state){
                    .source_format = RS_FORMAT_X8R8G8B8,
                    .source_tiling = ETNA_LAYOUT_SUPERTILED,
                    .source_addr = rt->address,
                    .source_stride = padded_width * 4 * 4,
                    .dest_format = RS_FORMAT_X8R8G8B8,
                    .dest_tiling = ETNA_LAYOUT_LINEAR,
                    .dest_addr = fb.physical[bi],
                    .dest_stride = fb.fb_fix.line_length,
                    .swap_rb = true,
                    .dither = {0xffffffff, 0xffffffff},
                    .clear_mode = VIVS_RS_CLEAR_CONTROL_MODE_DISABLED,
                    .width = width,
                    .height = height
                });
    }

    /* compile gallium3d states */
    struct etna_pipe_context *pipe = ETNA_NEW(struct etna_pipe_context);

    struct compiled_blend_state *blend = ETNA_NEW(struct compiled_blend_state);
    compile_blend_state(blend, &(struct pipe_blend_state) {
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

    struct compiled_sampler_state *sampler = ETNA_NEW(struct compiled_sampler_state);
    compile_sampler_state(sampler, &(struct pipe_sampler_state) {
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

    struct compiled_rasterizer_state *rasterizer = ETNA_NEW(struct compiled_rasterizer_state);
    compile_rasterizer_state(rasterizer, &(struct pipe_rasterizer_state){
                .flatshade = 0,
                .light_twoside = 1,
                .clamp_vertex_color = 1,
                .clamp_fragment_color = 1,
                .front_ccw = 1,
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
                .gl_rasterization_rules = 1,
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

    struct compiled_depth_stencil_alpha_state *dsa = ETNA_NEW(struct compiled_depth_stencil_alpha_state);
    compile_depth_stencil_alpha_state(dsa, &(struct pipe_depth_stencil_alpha_state){
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

    struct compiled_vertex_elements_state  *vertex_elements = ETNA_NEW(struct compiled_vertex_elements_state);
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
    compile_vertex_elements_state(vertex_elements, sizeof(pipe_vertex_elements)/sizeof(pipe_vertex_elements[0]), pipe_vertex_elements);
    
    /* bind */
    pipe->num_samplers = 1;
    pipe->sampler[0] = sampler;
    pipe->blend = blend;
    pipe->rasterizer = rasterizer;
    pipe->depth_stencil_alpha = dsa;
    pipe->vertex_elements = vertex_elements;

    /* parameter-like state */
    compile_blend_color(&pipe->blend_color, &(struct pipe_blend_color){
            .color = {0.0f,0.0f,0.0f,1.0f}
            });
    compile_stencil_ref(&pipe->stencil_ref, &(struct pipe_stencil_ref){
            .ref_value[0] = 0xff,
            .ref_value[1] = 0xff
            });
    compile_sample_mask(&pipe->sample_mask, 0xf);
    compile_framebuffer_state(&pipe->framebuffer, &(struct pipe_framebuffer_state){
            .width = width,
            .height = height,
            .nr_cbufs = 1,
            .cbufs[0] = &(struct pipe_surface){
                .texture = rt_resource,
                .format = rt_resource->format,
                .width = rt_resource->width0,
                .height = rt_resource->height0,
                .writable = 1,
                .u.tex.level = 0,
                .layout = rt_resource->layout,
                .surf = rt_resource->levels[0],
                .clear_value = 0xff303030
                },
            .zsbuf = &(struct pipe_surface){
                .texture = z_resource,
                .format = z_resource->format,
                .width = z_resource->width0,
                .height = z_resource->height0,
                .writable = 1,
                .u.tex.level = 0,
                .layout = z_resource->layout,
                .surf = z_resource->levels[0],
                .clear_value = 0xffffffff
                }
            });
    compile_scissor_state(&pipe->scissor, &(struct pipe_scissor_state){
            .minx = 0,
            .miny = 0,
            .maxx = 65535,
            .maxy = 65535
            });
    compile_viewport_state(&pipe->viewport, &(struct pipe_viewport_state){
            .scale = {width/2.0f, height/2.0f, 0.5f, 1.0f},
            .translate = {width/2.0f, height/2.0f, 0.5f, 1.0f}
            });
    pipe->num_sampler_views = 1;
    compile_sampler_view(&pipe->sampler_view[0], &(struct pipe_sampler_view){
            .format = tex_format,
            .texture = tex_resource,
            .u.tex.first_level = 0,
            .u.tex.last_level = dds->num_mipmaps - 1,
            .swizzle_r = PIPE_SWIZZLE_RED,
            .swizzle_g = PIPE_SWIZZLE_GREEN,
            .swizzle_b = PIPE_SWIZZLE_BLUE,
            .swizzle_a = PIPE_SWIZZLE_ALPHA,
            });
    compile_set_vertex_buffer(&pipe->vertex_buffer, 1, &(struct pipe_vertex_buffer){
            .stride = (3 + 3 + 2)*4,
            .buffer_offset = 0,
            .buffer = vtx_resource,
            .user_buffer = 0
            });
    compile_set_index_buffer(&pipe->index_buffer, NULL);/*&(struct pipe_index_buffer){
            .index_size = 0,
            .offset = 0,
            .buffer = 0,
            .user_buffer = 0
            });*/ /* non-indexed rendering */
    /* everything is dirty */ 
    pipe->dirty_bits = 0xffffffff;

    for(int frame=0; frame<1000; ++frame)
    {
        if(frame%50 == 0)
            printf("*** FRAME %i ****\n", frame);
        /* everything is dirty */ 
        pipe->dirty_bits = 0xffffffff;
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
        
        etna_set_state(ctx, VIVS_PA_W_CLIP_LIMIT, 0x34000001);
        etna_set_state(ctx, VIVS_PA_SYSTEM_MODE, 0x11);
        
        /* Clear render target */
        etna_set_state(ctx, VIVS_TS_MEM_CONFIG, 0);
        submit_rs_state(ctx, &clear_rt);
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);
        
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH | VIVS_GL_FLUSH_CACHE_TEXTURE);
        etna_set_state(ctx, VIVS_RS_FLUSH_CACHE, VIVS_RS_FLUSH_CACHE_FLUSH);
        
        /* send gallium state */
        sync_context(ctx, pipe);

        /* shaders etc, not yet molded into gallium state */
        etna_set_state(ctx, VIVS_RA_CONTROL, 0x3);
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
        etna_set_state(ctx, VIVS_PA_ATTRIBUTE_ELEMENT_COUNT, 0x200);
        etna_set_state(ctx, VIVS_PA_SHADER_ATTRIBUTES(0), 0x200);
        etna_set_state(ctx, VIVS_PA_SHADER_ATTRIBUTES(1), 0x200);

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

        /* wait rasterizer until PE finished configuration */
        etna_stall(ctx, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);
        for(int prim=0; prim<6; ++prim)
        {
            etna_draw_primitives(ctx, PRIMITIVE_TYPE_TRIANGLE_STRIP, prim*4, 2);
        }
        etna_stall(ctx, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);
        
        /* copy to screen */
        etna_bswap_wait_available(buffers);
        etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
        submit_rs_state(ctx, &copy_to_screen[buffers->backbuffer]);
        etna_set_state(ctx, VIVS_RS_KICKER, 0xbeebbeeb);

        etna_bswap_queue_swap(buffers);
        
        //dump_cmd_buffer(ctx);
        //exit(0);
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
