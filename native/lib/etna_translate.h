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
/* inlined translation functions between gallium and vivante */
#ifndef H_TRANSLATE
#define H_TRANSLATE

#include "minigallium.h"
#include "etna/common.xml.h"
#include "etna/state.xml.h"
#include "etna/state_3d.xml.h"
#include "etna/cmdstream.xml.h"

#include <stdio.h>
#include <math.h>

#define RCPLOG2 (1.4426950408889634f)
/* [0.0 .. 1.0] -> [0 .. 255] */
static inline uint8_t cfloat_to_uint8(float f)
{
    if(f<=0.0f) return 0;
    if(f>=(1.0f-1.0f/256.0f)) return 255;
    return f * 256.0f;
}

static inline uint32_t cfloat_to_uintN(float f, int bits)
{
    if(f<=0.0f) return 0;
    if(f>=(1.0f-1.0f/(1<<bits))) return (1<<bits)-1;
    return f * (1<<bits);
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
 *   translate_vertex_format_normalize and pipe_element_size
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

/* Return size in bytes for one element */
static inline uint32_t pipe_element_size(enum pipe_format fmt)
{
    switch(fmt)
    {
    case PIPE_FORMAT_A8_UNORM:
    case PIPE_FORMAT_L8_UNORM:
    case PIPE_FORMAT_I8_UNORM:
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
    case PIPE_FORMAT_L8A8_UNORM:
    case PIPE_FORMAT_B4G4R4A4_UNORM:
    case PIPE_FORMAT_B4G4R4X4_UNORM:
    case PIPE_FORMAT_B5G6R5_UNORM:
    case PIPE_FORMAT_B5G5R5A1_UNORM:
    case PIPE_FORMAT_B5G5R5X1_UNORM:
    case PIPE_FORMAT_Z16_UNORM:
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
    case PIPE_FORMAT_B8G8R8A8_UNORM:
    case PIPE_FORMAT_B8G8R8X8_UNORM:
    case PIPE_FORMAT_R8G8B8X8_UNORM:
    case PIPE_FORMAT_Z24X8_UNORM:
    case PIPE_FORMAT_Z24_UNORM_S8_UINT:
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
    case PIPE_FORMAT_DXT1_RGB:
    case PIPE_FORMAT_DXT1_RGBA:
    case PIPE_FORMAT_ETC1_RGB8:
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
    case PIPE_FORMAT_DXT3_RGBA:
    case PIPE_FORMAT_DXT5_RGBA:
        return 16;
    default: printf("Unhandled pipe format: %i\n", fmt); return 0;
    }
    /// XXX YUYV, UYVY elements
}

/* return pixel size of one block */
static inline void pipe_element_divsize(enum pipe_format fmt, unsigned *divSizeX, unsigned *divSizeY)
{
    switch(fmt)
    {
    case PIPE_FORMAT_A8_UNORM:
    case PIPE_FORMAT_L8_UNORM:
    case PIPE_FORMAT_I8_UNORM:
    case PIPE_FORMAT_R8_UNORM:
    case PIPE_FORMAT_R8_UINT:
    case PIPE_FORMAT_R8_SNORM:
    case PIPE_FORMAT_R8_SINT:
    case PIPE_FORMAT_R8G8_UNORM:
    case PIPE_FORMAT_R8G8_UINT:
    case PIPE_FORMAT_R8G8_SNORM:
    case PIPE_FORMAT_R8G8_SINT:
    case PIPE_FORMAT_R16_UNORM:
    case PIPE_FORMAT_R16_UINT:
    case PIPE_FORMAT_R16_SNORM:
    case PIPE_FORMAT_R16_SINT:
    case PIPE_FORMAT_R16_FLOAT:
    case PIPE_FORMAT_L8A8_UNORM:
    case PIPE_FORMAT_B4G4R4A4_UNORM:
    case PIPE_FORMAT_B4G4R4X4_UNORM:
    case PIPE_FORMAT_B5G6R5_UNORM:
    case PIPE_FORMAT_B5G5R5A1_UNORM:
    case PIPE_FORMAT_B5G5R5X1_UNORM:
    case PIPE_FORMAT_Z16_UNORM:
    case PIPE_FORMAT_R8G8B8_UNORM:
    case PIPE_FORMAT_R8G8B8_UINT:
    case PIPE_FORMAT_R8G8B8_SNORM:
    case PIPE_FORMAT_R8G8B8_SINT:
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
    case PIPE_FORMAT_B8G8R8A8_UNORM:
    case PIPE_FORMAT_B8G8R8X8_UNORM:
    case PIPE_FORMAT_R8G8B8X8_UNORM:
    case PIPE_FORMAT_Z24X8_UNORM:
    case PIPE_FORMAT_Z24_UNORM_S8_UINT:
    case PIPE_FORMAT_R16G16B16_UNORM:
    case PIPE_FORMAT_R16G16B16_UINT:
    case PIPE_FORMAT_R16G16B16_SNORM:
    case PIPE_FORMAT_R16G16B16_SINT:
    case PIPE_FORMAT_R16G16B16_FLOAT:
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
    case PIPE_FORMAT_R32G32B32_UNORM:
    case PIPE_FORMAT_R32G32B32_UINT:
    case PIPE_FORMAT_R32G32B32_SNORM:
    case PIPE_FORMAT_R32G32B32_SINT:
    case PIPE_FORMAT_R32G32B32_FLOAT:
    case PIPE_FORMAT_R32G32B32_FIXED:
    case PIPE_FORMAT_R32G32B32A32_UNORM:
    case PIPE_FORMAT_R32G32B32A32_UINT:
    case PIPE_FORMAT_R32G32B32A32_SNORM:
    case PIPE_FORMAT_R32G32B32A32_SINT:
    case PIPE_FORMAT_R32G32B32A32_FLOAT:
    case PIPE_FORMAT_R32G32B32A32_FIXED:
        *divSizeX = 1; *divSizeY = 1;
        return;
    case PIPE_FORMAT_DXT1_RGB:
    case PIPE_FORMAT_DXT1_RGBA:
    case PIPE_FORMAT_DXT3_RGBA:
    case PIPE_FORMAT_DXT5_RGBA:
    case PIPE_FORMAT_ETC1_RGB8:
        *divSizeX = 4; *divSizeY = 4;
        return;
    default: printf("Unhandled pipe format: %i", fmt);
        *divSizeX = 0; *divSizeY = 0;
    }
    /// XXX YUYV, UYVY elements
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

static inline uint32_t translate_draw_mode(unsigned mode)
{
    switch(mode)
    {
    case PIPE_PRIM_POINTS: return PRIMITIVE_TYPE_POINTS;
    case PIPE_PRIM_LINES: return PRIMITIVE_TYPE_LINES;
    case PIPE_PRIM_LINE_LOOP: return PRIMITIVE_TYPE_LINE_LOOP;
    case PIPE_PRIM_LINE_STRIP: return PRIMITIVE_TYPE_LINE_STRIP;
    case PIPE_PRIM_TRIANGLES: return PRIMITIVE_TYPE_TRIANGLES;
    case PIPE_PRIM_TRIANGLE_STRIP: return PRIMITIVE_TYPE_TRIANGLE_STRIP;
    case PIPE_PRIM_TRIANGLE_FAN: return PRIMITIVE_TYPE_TRIANGLE_FAN;
    case PIPE_PRIM_QUADS: return PRIMITIVE_TYPE_QUADS;
    default: printf("Unhandled draw mode primitive %i\n", mode); return 0;
    }
}

/* translate vertex count to primitive count */
static inline int translate_vertex_count(unsigned mode, int vertex_count)
{
    switch(mode)
    {
    case PIPE_PRIM_POINTS: return vertex_count;
    case PIPE_PRIM_LINES: return vertex_count / 2;
    case PIPE_PRIM_LINE_LOOP: return vertex_count;
    case PIPE_PRIM_LINE_STRIP: return vertex_count - 1;
    case PIPE_PRIM_TRIANGLES: return vertex_count / 3;
    case PIPE_PRIM_TRIANGLE_STRIP: return vertex_count - 2; 
    case PIPE_PRIM_TRIANGLE_FAN: return vertex_count - 2;
    case PIPE_PRIM_QUADS: return vertex_count / 4;
    default: printf("Unhandled draw mode primitive %i\n", mode); return 0;
    }
}

/* get size multiple for size of texture/rendertarget with a certain layout */
static inline unsigned etna_layout_multiple(unsigned layout)
{
    switch(layout)
    {
    case ETNA_LAYOUT_LINEAR: return 1;
    case ETNA_LAYOUT_TILED:  return 4;
    case ETNA_LAYOUT_SUPERTILED: return 64;
    default: printf("Unhandled layout %i\n", layout); return 0;
    }
}

static inline bool pipe_format_is_depth(enum pipe_format fmt)
{
    return (fmt == PIPE_FORMAT_Z16_UNORM) || (fmt == PIPE_FORMAT_Z24X8_UNORM) || (fmt == PIPE_FORMAT_Z24_UNORM_S8_UINT);
}

/* return 32-bit clear pattern for color */
static uint32_t translate_clear_color(enum pipe_format format, const union pipe_color_union *color)
{
    uint32_t clear_value = 0;
    switch(format) // XXX util_pack_color
    {
    case PIPE_FORMAT_B8G8R8A8_UNORM:
    case PIPE_FORMAT_B8G8R8X8_UNORM:
        clear_value = cfloat_to_uintN(color->f[2], 8) |
                (cfloat_to_uintN(color->f[1], 8) << 8) |
                (cfloat_to_uintN(color->f[0], 8) << 16) |
                (cfloat_to_uintN(color->f[3], 8) << 24);
        break;
    case PIPE_FORMAT_B4G4R4X4_UNORM: 
    case PIPE_FORMAT_B4G4R4A4_UNORM: 
        clear_value = cfloat_to_uintN(color->f[2], 4) |
                (cfloat_to_uintN(color->f[1], 4) << 4) |
                (cfloat_to_uintN(color->f[0], 4) << 8) |
                (cfloat_to_uintN(color->f[3], 4) << 12);
        clear_value |= clear_value << 16;
        break;
    case PIPE_FORMAT_B5G5R5X1_UNORM: 
    case PIPE_FORMAT_B5G5R5A1_UNORM: 
        clear_value = cfloat_to_uintN(color->f[2], 5) |
                (cfloat_to_uintN(color->f[1], 5) << 5) |
                (cfloat_to_uintN(color->f[0], 5) << 10) |
                (cfloat_to_uintN(color->f[3], 1) << 15);
        clear_value |= clear_value << 16;
        break;
    case PIPE_FORMAT_B5G6R5_UNORM: 
        clear_value = cfloat_to_uintN(color->f[2], 5) |
                (cfloat_to_uintN(color->f[1], 6) << 5) |
                (cfloat_to_uintN(color->f[0], 5) << 11);
        clear_value |= clear_value << 16;
        break;
    default:
        printf("Unhandled pipe format for color clear: %i\n", format);
    }
    return clear_value;
}

static uint32_t translate_clear_depth_stencil(enum pipe_format format, float depth, unsigned stencil)
{
    uint32_t clear_value = 0;
    switch(format) // XXX util_pack_color
    {
    case PIPE_FORMAT_Z16_UNORM: 
        clear_value = cfloat_to_uintN(depth, 16);
        clear_value |= clear_value << 16;
        break;
    case PIPE_FORMAT_Z24X8_UNORM: 
    case PIPE_FORMAT_Z24_UNORM_S8_UINT: 
        clear_value = (cfloat_to_uintN(depth, 24) << 8) | (stencil & 0xFF);
        break;
    default:
        printf("Unhandled pipe format for depth stencil clear: %i\n", format);
    }
    return clear_value;
}

#endif

