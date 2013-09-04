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

#include "pipe/p_defines.h"
#include "pipe/p_format.h"
#include "pipe/p_state.h"

#include <etnaviv/common.xml.h>
#include <etnaviv/state.xml.h>
#include <etnaviv/state_3d.xml.h>
#include <etnaviv/cmdstream.xml.h>
#include <etnaviv/etna_util.h>
#include <etnaviv/etna_tex.h>
#include "etna_internal.h"
#include "etna_debug.h"

#include "util/u_format.h"

#include <stdio.h>

/* Returned when there is no match of pipe value to etna value */
#define ETNA_NO_MATCH (~0)

static inline uint32_t translate_cull_face(unsigned cull_face, unsigned front_ccw)
{
    switch(cull_face)
    {
    case PIPE_FACE_NONE: return VIVS_PA_CONFIG_CULL_FACE_MODE_OFF;
    case PIPE_FACE_BACK: return front_ccw ? VIVS_PA_CONFIG_CULL_FACE_MODE_CW : VIVS_PA_CONFIG_CULL_FACE_MODE_CCW;
    case PIPE_FACE_FRONT: return front_ccw ? VIVS_PA_CONFIG_CULL_FACE_MODE_CCW : VIVS_PA_CONFIG_CULL_FACE_MODE_CW;
    default: DBG("Unhandled cull face mode %i\n", cull_face); return ETNA_NO_MATCH;
    }
}

static inline uint32_t translate_polygon_mode(unsigned polygon_mode)
{
    switch(polygon_mode)
    {
    case PIPE_POLYGON_MODE_FILL: return VIVS_PA_CONFIG_FILL_MODE_SOLID;
    case PIPE_POLYGON_MODE_LINE: return VIVS_PA_CONFIG_FILL_MODE_WIREFRAME;
    case PIPE_POLYGON_MODE_POINT: return VIVS_PA_CONFIG_FILL_MODE_POINT;
    default: DBG("Unhandled polygon mode %i\n", polygon_mode); return ETNA_NO_MATCH;
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
    default: DBG("Unhandled stencil op: %i\n", stencil_op); return ETNA_NO_MATCH;
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
    default: DBG("Unhandled blend: %i\n", blend); return ETNA_NO_MATCH;
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
    default: DBG("Unhandled blend factor: %i\n", blend_factor); return ETNA_NO_MATCH;
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
    default: DBG("Unhandled texture wrapmode: %i\n", wrap); return ETNA_NO_MATCH;
    }
}

static inline uint32_t translate_texture_mipfilter(unsigned filter)
{
    switch(filter)
    {
    case PIPE_TEX_MIPFILTER_NEAREST: return TEXTURE_FILTER_NEAREST;
    case PIPE_TEX_MIPFILTER_LINEAR:  return TEXTURE_FILTER_LINEAR;
    case PIPE_TEX_MIPFILTER_NONE:    return TEXTURE_FILTER_NONE;
    default: DBG("Unhandled texture mipfilter: %i\n", filter); return ETNA_NO_MATCH;
    }
}

static inline uint32_t translate_texture_filter(unsigned filter)
{
    switch(filter)
    {
    case PIPE_TEX_FILTER_NEAREST: return TEXTURE_FILTER_NEAREST;
    case PIPE_TEX_FILTER_LINEAR:  return TEXTURE_FILTER_LINEAR;
    /* What about anisotropic? */
    default: DBG("Unhandled texture filter: %i\n", filter); return ETNA_NO_MATCH;
    }
}

static inline uint32_t translate_texture_format(enum pipe_format fmt, bool silent)
{
    switch(fmt) /* XXX with TEXTURE_FORMAT_EXT and swizzle on newer chips we can support much more */
    {
    /* Note: Pipe format convention is LSB to MSB, VIVS is MSB to LSB */
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
    case PIPE_FORMAT_X8Z24_UNORM: return TEXTURE_FORMAT_D24S8;
    case PIPE_FORMAT_S8_UINT_Z24_UNORM: return TEXTURE_FORMAT_D24S8;
    case PIPE_FORMAT_DXT1_RGB:  return TEXTURE_FORMAT_DXT1;
    case PIPE_FORMAT_DXT1_RGBA: return TEXTURE_FORMAT_DXT1;
    case PIPE_FORMAT_DXT3_RGBA: return TEXTURE_FORMAT_DXT2_DXT3;
    case PIPE_FORMAT_DXT5_RGBA: return TEXTURE_FORMAT_DXT4_DXT5;
    case PIPE_FORMAT_ETC1_RGB8: return TEXTURE_FORMAT_ETC1;
    default: if(!silent) { DBG("Unhandled texture format: %i\n", fmt); } return ETNA_NO_MATCH;
    }
}

/* render target format (non-rb swapped RS-supported formats) */
static inline uint32_t translate_rt_format(enum pipe_format fmt, bool silent)
{
    switch(fmt)
    {
    /* Note: Pipe format convention is LSB to MSB, VIVS is MSB to LSB */
    case PIPE_FORMAT_B4G4R4X4_UNORM: return RS_FORMAT_X4R4G4B4;
    case PIPE_FORMAT_B4G4R4A4_UNORM: return RS_FORMAT_A4R4G4B4;
    case PIPE_FORMAT_B5G5R5X1_UNORM: return RS_FORMAT_X1R5G5B5;
    case PIPE_FORMAT_B5G5R5A1_UNORM: return RS_FORMAT_A1R5G5B5;
    case PIPE_FORMAT_B5G6R5_UNORM: return RS_FORMAT_R5G6B5;
    case PIPE_FORMAT_B8G8R8X8_UNORM: return RS_FORMAT_X8R8G8B8;
    case PIPE_FORMAT_B8G8R8A8_UNORM: return RS_FORMAT_A8R8G8B8;
    case PIPE_FORMAT_YUYV: return RS_FORMAT_YUY2;
    default: if(!silent) { DBG("Unhandled rs surface format: %i\n", fmt); } return ETNA_NO_MATCH;
    }
}

static inline uint32_t translate_depth_format(enum pipe_format fmt, bool silent)
{
    switch(fmt)
    {
    /* Note: Pipe format convention is LSB to MSB, VIVS is MSB to LSB */
    case PIPE_FORMAT_Z16_UNORM: return VIVS_PE_DEPTH_CONFIG_DEPTH_FORMAT_D16;
    case PIPE_FORMAT_X8Z24_UNORM: return VIVS_PE_DEPTH_CONFIG_DEPTH_FORMAT_D24S8;
    case PIPE_FORMAT_S8_UINT_Z24_UNORM: return VIVS_PE_DEPTH_CONFIG_DEPTH_FORMAT_D24S8;
    default: if(!silent) { DBG("Unhandled depth format: %i\n", fmt); } return ETNA_NO_MATCH;
    }
}

/* render target format for MSAA */
static inline uint32_t translate_msaa_format(enum pipe_format fmt, bool silent)
{
    switch(fmt)
    {
    /* Note: Pipe format convention is LSB to MSB, VIVS is MSB to LSB */
    case PIPE_FORMAT_B4G4R4X4_UNORM: return VIVS_TS_MEM_CONFIG_MSAA_FORMAT_A4R4G4B4;
    case PIPE_FORMAT_B4G4R4A4_UNORM: return VIVS_TS_MEM_CONFIG_MSAA_FORMAT_A4R4G4B4;
    case PIPE_FORMAT_B5G5R5X1_UNORM: return VIVS_TS_MEM_CONFIG_MSAA_FORMAT_A1R5G5B5;
    case PIPE_FORMAT_B5G5R5A1_UNORM: return VIVS_TS_MEM_CONFIG_MSAA_FORMAT_A1R5G5B5;
    case PIPE_FORMAT_B5G6R5_UNORM:   return VIVS_TS_MEM_CONFIG_MSAA_FORMAT_R5G6B5;
    case PIPE_FORMAT_B8G8R8X8_UNORM: return VIVS_TS_MEM_CONFIG_MSAA_FORMAT_X8R8G8B8;
    case PIPE_FORMAT_B8G8R8A8_UNORM: return VIVS_TS_MEM_CONFIG_MSAA_FORMAT_A8R8G8B8;
    /* MSAA with YUYV not supported */
    default: if(!silent) { DBG("Unhandled msaa surface format: %i\n", fmt); } return ETNA_NO_MATCH;
    }
}

static inline uint32_t translate_texture_target(enum pipe_texture_target tgt, bool silent)
{
    switch(tgt)
    {
    case PIPE_TEXTURE_2D: return TEXTURE_TYPE_2D;
    case PIPE_TEXTURE_CUBE: return TEXTURE_TYPE_CUBE_MAP;
    default: DBG("Unhandled texture target: %i\n", tgt); return ETNA_NO_MATCH;
    }
}

/* Return type flags for vertex element format */
static inline uint32_t translate_vertex_format_type(enum pipe_format fmt, bool silent)
{
    switch(fmt)
    {
    case PIPE_FORMAT_R8_UNORM:
    case PIPE_FORMAT_R8G8_UNORM:
    case PIPE_FORMAT_R8G8B8_UNORM:
    case PIPE_FORMAT_R8G8B8A8_UNORM:
    case PIPE_FORMAT_R8_USCALED:
    case PIPE_FORMAT_R8G8_USCALED:
    case PIPE_FORMAT_R8G8B8_USCALED:
    case PIPE_FORMAT_R8G8B8A8_USCALED:
    case PIPE_FORMAT_R8_UINT:
    case PIPE_FORMAT_R8G8_UINT:
    case PIPE_FORMAT_R8G8B8_UINT:
    case PIPE_FORMAT_R8G8B8A8_UINT:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_UNSIGNED_BYTE;
    case PIPE_FORMAT_R8_SNORM:
    case PIPE_FORMAT_R8G8_SNORM:
    case PIPE_FORMAT_R8G8B8_SNORM:
    case PIPE_FORMAT_R8G8B8A8_SNORM:
    case PIPE_FORMAT_R8_SSCALED:
    case PIPE_FORMAT_R8G8_SSCALED:
    case PIPE_FORMAT_R8G8B8_SSCALED:
    case PIPE_FORMAT_R8G8B8A8_SSCALED:
    case PIPE_FORMAT_R8_SINT:
    case PIPE_FORMAT_R8G8_SINT:
    case PIPE_FORMAT_R8G8B8_SINT:
    case PIPE_FORMAT_R8G8B8A8_SINT:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_BYTE;
    case PIPE_FORMAT_R16_UNORM:
    case PIPE_FORMAT_R16G16_UNORM:
    case PIPE_FORMAT_R16G16B16_UNORM:
    case PIPE_FORMAT_R16G16B16A16_UNORM:
    case PIPE_FORMAT_R16_USCALED:
    case PIPE_FORMAT_R16G16_USCALED:
    case PIPE_FORMAT_R16G16B16_USCALED:
    case PIPE_FORMAT_R16G16B16A16_USCALED:
    case PIPE_FORMAT_R16_UINT:
    case PIPE_FORMAT_R16G16_UINT:
    case PIPE_FORMAT_R16G16B16_UINT:
    case PIPE_FORMAT_R16G16B16A16_UINT:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_UNSIGNED_SHORT;
    case PIPE_FORMAT_R16_SNORM:
    case PIPE_FORMAT_R16G16_SNORM:
    case PIPE_FORMAT_R16G16B16_SNORM:
    case PIPE_FORMAT_R16G16B16A16_SNORM:
    case PIPE_FORMAT_R16_SSCALED:
    case PIPE_FORMAT_R16G16_SSCALED:
    case PIPE_FORMAT_R16G16B16_SSCALED:
    case PIPE_FORMAT_R16G16B16A16_SSCALED:
    case PIPE_FORMAT_R16_SINT:
    case PIPE_FORMAT_R16G16_SINT:
    case PIPE_FORMAT_R16G16B16_SINT:
    case PIPE_FORMAT_R16G16B16A16_SINT:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_SHORT;
    case PIPE_FORMAT_R32_UNORM:
    case PIPE_FORMAT_R32G32_UNORM:
    case PIPE_FORMAT_R32G32B32_UNORM:
    case PIPE_FORMAT_R32G32B32A32_UNORM:
    case PIPE_FORMAT_R32_USCALED:
    case PIPE_FORMAT_R32G32_USCALED:
    case PIPE_FORMAT_R32G32B32_USCALED:
    case PIPE_FORMAT_R32G32B32A32_USCALED:
    case PIPE_FORMAT_R32_UINT:
    case PIPE_FORMAT_R32G32_UINT:
    case PIPE_FORMAT_R32G32B32_UINT:
    case PIPE_FORMAT_R32G32B32A32_UINT:
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_TYPE_UNSIGNED_INT;
    case PIPE_FORMAT_R32_SNORM:
    case PIPE_FORMAT_R32G32_SNORM:
    case PIPE_FORMAT_R32G32B32_SNORM:
    case PIPE_FORMAT_R32G32B32A32_SNORM:
    case PIPE_FORMAT_R32_SSCALED:
    case PIPE_FORMAT_R32G32_SSCALED:
    case PIPE_FORMAT_R32G32B32_SSCALED:
    case PIPE_FORMAT_R32G32B32A32_SSCALED:
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
    default: if(!silent) { DBG("Unhandled vertex format: %i", fmt); } return ETNA_NO_MATCH;
    }
}

/* Return normalization flag for vertex element format */
static inline uint32_t translate_vertex_format_normalize(enum pipe_format fmt)
{
    const struct util_format_description *desc = util_format_description(fmt);
    if(!desc)
        return VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF;
    /* assumes that normalization of channel 0 holds for all channels;
     * this holds for all vertex formats that we support */
    return desc->channel[0].normalized ? VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_ON :
                                         VIVS_FE_VERTEX_ELEMENT_CONFIG_NORMALIZE_OFF;
}

static inline uint32_t translate_index_size(unsigned index_size)
{
    switch(index_size)
    {
    case 1: return VIVS_FE_INDEX_STREAM_CONTROL_TYPE_UNSIGNED_CHAR;
    case 2: return VIVS_FE_INDEX_STREAM_CONTROL_TYPE_UNSIGNED_SHORT;
    case 4: return VIVS_FE_INDEX_STREAM_CONTROL_TYPE_UNSIGNED_INT;
    default: DBG("Unhandled index size %i\n", index_size); return ETNA_NO_MATCH;
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
    default: DBG("Unhandled draw mode primitive %i\n", mode); return ETNA_NO_MATCH;
    }
}

/* Get size multiple for size of texture/rendertarget with a certain layout
 * This is affected by many different parameters:
 *   -  A horizontal multiple of 16 is used when possible as in this case tile status and resolve can be used
 *       at the cost of only a little bit extra memory usage.
 *   - If the surface is a texture, and HALIGN can not be specified on thie GPU, set tex_no_halign to 1
 *       If set, an horizontal multiple of 4 will be used for tiled and linear surfaces, otherwise one of 16.
 *   - If the surface is supertiled, horizontal and vertical multiple is always 64
 *   - If the surface is multi tiled or supertiled, make sure that the vertical size
 *     is a multiple of the number of pixel pipes as well.
 * */
static inline void etna_layout_multiple(unsigned layout, unsigned pixel_pipes,
        bool tex_no_halign,
        unsigned *paddingX, unsigned *paddingY, unsigned *halign)
{
    switch(layout)
    {
    case ETNA_LAYOUT_LINEAR:
        *paddingX = tex_no_halign ? 4 : 16;
        *paddingY = 1;
        *halign = tex_no_halign ? TEXTURE_HALIGN_FOUR : TEXTURE_HALIGN_SIXTEEN;
        break;
    case ETNA_LAYOUT_TILED:
        *paddingX = tex_no_halign ? 4 : 16;
        *paddingY = 4;
        *halign = tex_no_halign ? TEXTURE_HALIGN_FOUR : TEXTURE_HALIGN_SIXTEEN;
        break;
    case ETNA_LAYOUT_SUPER_TILED:
        *paddingX = 64;
        *paddingY = 64;
        *halign = TEXTURE_HALIGN_SUPER_TILED;
        break;
    case ETNA_LAYOUT_MULTI_TILED:
        *paddingX = 16;
        *paddingY = 4 * pixel_pipes;
        *halign = TEXTURE_HALIGN_SPLIT_TILED;
        break;
    case ETNA_LAYOUT_MULTI_SUPERTILED:
        *paddingX = 64;
        *paddingY = 64 * pixel_pipes;
        *halign = TEXTURE_HALIGN_SPLIT_SUPER_TILED;
        break;
    default: DBG("Unhandled layout %i\n", layout);
    }
}

/* return 32-bit clear pattern for color */
static inline uint32_t translate_clear_color(enum pipe_format format, const union pipe_color_union *color)
{
    uint32_t clear_value = 0;
    switch(format) // XXX util_pack_color
    {
    case PIPE_FORMAT_B8G8R8A8_UNORM:
    case PIPE_FORMAT_B8G8R8X8_UNORM:
        clear_value = etna_cfloat_to_uintN(color->f[2], 8) |
                (etna_cfloat_to_uintN(color->f[1], 8) << 8) |
                (etna_cfloat_to_uintN(color->f[0], 8) << 16) |
                (etna_cfloat_to_uintN(color->f[3], 8) << 24);
        break;
    case PIPE_FORMAT_B4G4R4X4_UNORM:
    case PIPE_FORMAT_B4G4R4A4_UNORM:
        clear_value = etna_cfloat_to_uintN(color->f[2], 4) |
                (etna_cfloat_to_uintN(color->f[1], 4) << 4) |
                (etna_cfloat_to_uintN(color->f[0], 4) << 8) |
                (etna_cfloat_to_uintN(color->f[3], 4) << 12);
        clear_value |= clear_value << 16;
        break;
    case PIPE_FORMAT_B5G5R5X1_UNORM:
    case PIPE_FORMAT_B5G5R5A1_UNORM:
        clear_value = etna_cfloat_to_uintN(color->f[2], 5) |
                (etna_cfloat_to_uintN(color->f[1], 5) << 5) |
                (etna_cfloat_to_uintN(color->f[0], 5) << 10) |
                (etna_cfloat_to_uintN(color->f[3], 1) << 15);
        clear_value |= clear_value << 16;
        break;
    case PIPE_FORMAT_B5G6R5_UNORM:
        clear_value = etna_cfloat_to_uintN(color->f[2], 5) |
                (etna_cfloat_to_uintN(color->f[1], 6) << 5) |
                (etna_cfloat_to_uintN(color->f[0], 5) << 11);
        clear_value |= clear_value << 16;
        break;
    default:
        DBG("Unhandled pipe format for color clear: %i\n", format);
    }
    return clear_value;
}

static inline uint32_t translate_clear_depth_stencil(enum pipe_format format, float depth, unsigned stencil)
{
    uint32_t clear_value = 0;
    switch(format) // XXX util_pack_color
    {
    case PIPE_FORMAT_Z16_UNORM:
        clear_value = etna_cfloat_to_uintN(depth, 16);
        clear_value |= clear_value << 16;
        break;
    case PIPE_FORMAT_X8Z24_UNORM:
    case PIPE_FORMAT_S8_UINT_Z24_UNORM:
        clear_value = (etna_cfloat_to_uintN(depth, 24) << 8) | (stencil & 0xFF);
        break;
    default:
        DBG("Unhandled pipe format for depth stencil clear: %i\n", format);
    }
    return clear_value;
}

/* Convert MSAA number of samples to x and y scaling factor and VIVS_GL_MULTI_SAMPLE_CONFIG value.
 * Return true if supported and false otherwise.
 */
static inline bool translate_samples_to_xyscale(int num_samples, int *xscale_out, int *yscale_out, uint32_t *config_out)
{
    int xscale, yscale;
    uint32_t config;
    switch(num_samples)
    {
    case 0:
    case 1:
        xscale = 1;
        yscale = 1;
        config = VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES_NONE;
        break;
    case 2:
        xscale = 2;
        yscale = 1;
        config = VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES_2X;
        break;
    case 4:
        xscale = 2;
        yscale = 2;
        config = VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES_4X;
        break;
    default:
        return false;
    }
    if(xscale_out)
        *xscale_out = xscale;
    if(yscale_out)
        *yscale_out = yscale;
    if(config_out)
        *config_out = config;
    return true;
}

#endif

