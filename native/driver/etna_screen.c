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
#include "etna_screen.h"
#include "etna_pipe.h"
#include "etna_compiler.h"
#include "etna_translate.h"
#include "etna_debug.h"
#include "etna_fence.h"
#include "etna_resource.h"

#include <etnaviv/etna_rs.h>
#include <etnaviv/viv.h>
#include <etnaviv/etna.h>
#include <etnaviv/etna_util.h>

#include "util/u_memory.h"
#include "util/u_format.h"
#include "util/u_transfer.h"
#include "util/u_math.h"
#include "util/u_inlines.h"

#include <stdio.h>

int etna_mesa_debug = ETNA_DBG_MSGS | ETNA_RESOURCE_MSGS | ETNA_COMPILER_MSGS;  /* XXX */

static void etna_screen_destroy( struct pipe_screen *screen )
{
    struct etna_screen *priv = etna_screen(screen);
    etna_screen_destroy_fences(screen);
    pipe_mutex_destroy(priv->fence_mutex);
    FREE(screen);
}

static const char *etna_screen_get_name( struct pipe_screen *screen )
{
    return "ETNA0";
}

static const char *etna_screen_get_vendor( struct pipe_screen *screen )
{
    return "etnaviv";
}

static int etna_screen_get_param( struct pipe_screen *screen, enum pipe_cap param )
{
    struct etna_screen *priv = etna_screen(screen);
    switch (param) {
    /* Supported features (boolean caps). */
    case PIPE_CAP_TWO_SIDED_STENCIL:
    case PIPE_CAP_ANISOTROPIC_FILTER:
    case PIPE_CAP_POINT_SPRITE:
    case PIPE_CAP_TEXTURE_SHADOW_MAP:
    case PIPE_CAP_BLEND_EQUATION_SEPARATE:
    case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT: /* FS coordinates start in upper left */
    case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER: /* Pixel center on 0.5 */
    case PIPE_CAP_SM3:
    case PIPE_CAP_SEAMLESS_CUBE_MAP: /* ??? */
    case PIPE_CAP_TEXTURE_BARRIER:
    case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
    case PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY:
    case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
    case PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY:
    case PIPE_CAP_USER_CONSTANT_BUFFERS: /* constant buffers can be user buffers; they end up in command stream anyway */
    case PIPE_CAP_TGSI_TEXCOORD: /* explicit TEXCOORD and POINTCOORD semantics */
            return 1;

    /* Memory */
    case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
            return 256;
    case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
            return 4; /* XXX could easily be supported */
    case PIPE_CAP_GLSL_FEATURE_LEVEL:
            return 120;

    case PIPE_CAP_NPOT_TEXTURES: /* MUST be supported with GLES 2.0 */
            return true; /* VIV_FEATURE(priv->dev, chipMinorFeatures1, NON_POWER_OF_TWO); */

    case PIPE_CAP_MAX_VERTEX_BUFFERS:
            return priv->specs.stream_count;

    /* Unsupported features. */
    case PIPE_CAP_TEXTURE_SWIZZLE: /* XXX supported on gc2000 */
    case PIPE_CAP_COMPUTE: /* XXX supported on gc2000 */
    case PIPE_CAP_MIXED_COLORBUFFER_FORMATS: /* only one colorbuffer supported, so mixing makes no sense */
    case PIPE_CAP_PRIMITIVE_RESTART: /* primitive restart index AFAIK not supported */
    case PIPE_CAP_VERTEX_COLOR_UNCLAMPED: /* no floating point buffer support */
    case PIPE_CAP_CONDITIONAL_RENDER: /* no occlusion queries */
    case PIPE_CAP_TGSI_INSTANCEID: /* no idea, really */
    case PIPE_CAP_START_INSTANCE: /* instancing not supported AFAIK */
    case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:  /* instancing not supported AFAIK */
    case PIPE_CAP_SHADER_STENCIL_EXPORT: /* Fragment shader cannot export stencil value */
    case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS: /* no dual-source supported */
    case PIPE_CAP_TEXTURE_MULTISAMPLE: /* no texture multisample */
    case PIPE_CAP_TEXTURE_MIRROR_CLAMP: /* only mirrored repeat */
    case PIPE_CAP_INDEP_BLEND_ENABLE:
    case PIPE_CAP_INDEP_BLEND_FUNC:
    case PIPE_CAP_DEPTH_CLIP_DISABLE:
    case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
    case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
    case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
    case PIPE_CAP_SCALED_RESOLVE: /* Should be possible to support */
    case PIPE_CAP_TGSI_CAN_COMPACT_CONSTANTS: /* Don't skip strict max uniform limit check */
    case PIPE_CAP_FRAGMENT_COLOR_CLAMPED:
    case PIPE_CAP_VERTEX_COLOR_CLAMPED:
    case PIPE_CAP_USER_VERTEX_BUFFERS:
    case PIPE_CAP_USER_INDEX_BUFFERS:
    case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
            return 0;

    /* Stream output. */
    case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
    case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
    case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
    case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
            return 0;

    /* Texturing. */
    case PIPE_CAP_MAX_TEXTURE_2D_LEVELS:
    case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
            return 14;
    case PIPE_CAP_MAX_TEXTURE_3D_LEVELS: /* 3D textures not supported */
            return 0;
    case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
            return 0;
    case PIPE_CAP_MAX_COMBINED_SAMPLERS:
            return priv->specs.fragment_sampler_count + priv->specs.vertex_sampler_count;
    case PIPE_CAP_CUBE_MAP_ARRAY:
            return 0;
    case PIPE_CAP_MIN_TEXEL_OFFSET:
            return -8;
    case PIPE_CAP_MAX_TEXEL_OFFSET:
            return 7;
    case PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK:
            return 0;
    case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE:
            return 65536;

    /* Render targets. */
    case PIPE_CAP_MAX_RENDER_TARGETS:
            return 1;

    /* Viewports and scissors. */
    case PIPE_CAP_MAX_VIEWPORTS:
            return 1;

    /* Timer queries. */
    case PIPE_CAP_QUERY_TIME_ELAPSED:
    case PIPE_CAP_OCCLUSION_QUERY:
    case PIPE_CAP_QUERY_TIMESTAMP:
            return 0;
    case PIPE_CAP_QUERY_PIPELINE_STATISTICS:
            return 0;

    /* Preferences */
    case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
            return 1;

    default:
            DBG("unknown param %d", param);
            return 0;
    }
}

static float etna_screen_get_paramf( struct pipe_screen *screen, enum pipe_capf param )
{
    switch (param) {
    case PIPE_CAPF_MAX_LINE_WIDTH:
    case PIPE_CAPF_MAX_LINE_WIDTH_AA:
    case PIPE_CAPF_MAX_POINT_WIDTH:
    case PIPE_CAPF_MAX_POINT_WIDTH_AA:
            return 8192.0f;
    case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
            return 16.0f;
    case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
            return 16.0f;
    case PIPE_CAPF_GUARD_BAND_LEFT:
    case PIPE_CAPF_GUARD_BAND_TOP:
    case PIPE_CAPF_GUARD_BAND_RIGHT:
    case PIPE_CAPF_GUARD_BAND_BOTTOM:
            return 0.0f;
    default:
            DBG("unknown paramf %d", param);
            return 0;
    }
}

static int etna_screen_get_shader_param( struct pipe_screen *screen, unsigned shader, enum pipe_shader_cap param )
{
    struct etna_screen *priv = etna_screen(screen);
    switch(shader)
    {
    case PIPE_SHADER_FRAGMENT:
    case PIPE_SHADER_VERTEX:
            break;
    case PIPE_SHADER_COMPUTE:
    case PIPE_SHADER_GEOMETRY:
            /* maybe we could emulate.. */
            return 0;
    default:
            DBG("unknown shader type %d", shader);
            return 0;
    }

    switch (param) {
    case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
    case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
    case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
    case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
            return ETNA_MAX_TOKENS;
    case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
            return ETNA_MAX_DEPTH; /* XXX */
    case PIPE_SHADER_CAP_MAX_INPUTS:
            return 16; /* XXX this amount is reserved */
    case PIPE_SHADER_CAP_MAX_TEMPS:
            return 64; /* Max native temporaries. */
    case PIPE_SHADER_CAP_MAX_ADDRS:
            return 1; /* Max native address registers */
    case PIPE_SHADER_CAP_MAX_CONSTS:
            /* Absolute maximum on ideal hardware is 256 (as that's how much register space is reserved); 
             * immediates are included in here, so actual space available for constants will always be less. 
             * Also the amount of registers really available depends on the hw. 
             * XXX see also: viv_specs.num_constants, if this is 0 we need to come up with some default value.
             */
            return 256; 
    case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
            return 1;
    case PIPE_SHADER_CAP_MAX_PREDS:
            return 0; /* nothing uses this */
    case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
            return 1;
    case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
    case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
    case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
    case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
            return 1;
    case PIPE_SHADER_CAP_SUBROUTINES:
            return 0;
    case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
            return VIV_FEATURE(priv->dev, chipMinorFeatures0, HAS_SQRT_TRIG);
    case PIPE_SHADER_CAP_TGSI_POW_SUPPORTED:
            return false;
    case PIPE_SHADER_CAP_TGSI_LRP_SUPPORTED:
            return false;
    case PIPE_SHADER_CAP_INTEGERS: /* XXX supported on gc2000 but not yet implemented */
            return 0;
    case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
            return shader==PIPE_SHADER_FRAGMENT ? priv->specs.fragment_sampler_count : 
                                                  priv->specs.vertex_sampler_count;
    case PIPE_SHADER_CAP_PREFERRED_IR:
            return PIPE_SHADER_IR_TGSI;
    default:
            DBG("unknown shader param %d", param);
            return 0;
    }
    return 0;
}

static int etna_screen_get_video_param( struct pipe_screen *screen,
                       enum pipe_video_profile profile,
                       enum pipe_video_cap param )
{
    DBG("unimplemented etna_screen_get_video_param");
    return 0;
}

static int etna_screen_get_compute_param(struct pipe_screen *screen,
                        enum pipe_compute_cap param,
                        void *ret)
{
    DBG("unimplemented etna_screen_get_compute_param");
    return 0;
}

static uint64_t etna_screen_get_timestamp(struct pipe_screen *screen)
{
    DBG("unimplemented etna_screen_get_timestamp");
    return 0;
}

static struct pipe_context * etna_screen_context_create( struct pipe_screen *screen,
                                        void *priv )
{
    struct etna_screen *es = etna_screen(screen);
    struct pipe_context *ctx = etna_new_pipe_context(es->dev, &es->specs, screen, priv);
    return ctx;
}

static boolean etna_screen_is_format_supported( struct pipe_screen *screen,
                               enum pipe_format format,
                               enum pipe_texture_target target,
                               unsigned sample_count,
                               unsigned usage)
{
    struct etna_screen *priv = etna_screen(screen);
    unsigned allowed = 0;
    if ((target >= PIPE_MAX_TEXTURE_TYPES) ||
                (sample_count > 1) /* TODO add MSAA */) 
    {
        return FALSE;
    }

    if (usage & PIPE_BIND_RENDER_TARGET)
    {
        /* if render target, must be RS-supported format */
        if(translate_rt_format(format, true) != ETNA_NO_MATCH)
        {
            allowed |= PIPE_BIND_RENDER_TARGET;
        }
    }
    if (usage & PIPE_BIND_DEPTH_STENCIL)
    {
        /* must be supported depth format */
        if(translate_depth_format(format, true) != ETNA_NO_MATCH)
        {
            allowed |= PIPE_BIND_DEPTH_STENCIL;
        }
    }
    if (usage & PIPE_BIND_SAMPLER_VIEW)
    {
        /* must be supported texture format */
        if(translate_texture_format(format, true) != ETNA_NO_MATCH)
        {
            allowed |= PIPE_BIND_SAMPLER_VIEW;
        }
    }
    if (usage & PIPE_BIND_VERTEX_BUFFER)
    {
        /* must be supported vertex format */
        if(translate_vertex_format_type(format, true) == ETNA_NO_MATCH)
        {
            allowed |= PIPE_BIND_VERTEX_BUFFER;
        }
    }
    if (usage & PIPE_BIND_INDEX_BUFFER)
    {
        /* must be supported index format */
        if(format == PIPE_FORMAT_I8_UINT ||
           format == PIPE_FORMAT_I16_UINT ||
           (format == PIPE_FORMAT_I32_UINT && VIV_FEATURE(priv->dev, chipFeatures, 32_BIT_INDICES)))
        {
            allowed |= PIPE_BIND_INDEX_BUFFER;
        }
    }
    /* Always allowed */
    allowed |= usage & (PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT | 
            PIPE_BIND_SHARED | PIPE_BIND_TRANSFER_READ | PIPE_BIND_TRANSFER_WRITE);

    return usage == allowed;
}

static boolean etna_screen_is_video_format_supported( struct pipe_screen *screen,
                                     enum pipe_format format,
                                     enum pipe_video_profile profile )
{
    DBG("unimplemented etna_screen_is_video_format_supported");
    return false;
}

/* XXX this should use a blit or resource copy, when implemented, instead
 * of programming the RS directly.
 */
static void etna_screen_flush_frontbuffer( struct pipe_screen *screen,
                          struct pipe_resource *resource,
                          unsigned level, unsigned layer,
                          void *winsys_drawable_handle )
{
    struct etna_rs_target *drawable = (struct etna_rs_target *)winsys_drawable_handle;
    struct etna_resource *rt_resource = etna_resource(resource);
    struct etna_pipe_context *ectx = rt_resource->last_ctx;
    struct pipe_fence_handle **fence = 0;
    assert(level <= resource->last_level && layer < resource->array_size);
    assert(ectx);
    struct etna_ctx *ctx = ectx->ctx;

    /* release previous fence, make reference to fence if we need one */
    screen->fence_reference(screen, &drawable->fence, NULL);
    if(drawable->want_fence)
        fence = &drawable->fence;

    /* XXX set up TS */
    /* Kick off RS here */
    struct compiled_rs_state copy_to_screen;
    etna_compile_rs_state(&copy_to_screen, &(struct rs_state){
                .source_format = translate_rt_format(rt_resource->base.format, false),
                .source_tiling = rt_resource->layout,
                .source_addr = rt_resource->levels[level].address,
                .source_stride = rt_resource->levels[level].stride,
                .dest_format = drawable->rs_format,
                .dest_tiling = ETNA_LAYOUT_LINEAR,
                .dest_addr = drawable->addr,
                .dest_stride = drawable->stride,
                .swap_rb = drawable->swap_rb,
                .dither = {0xffffffff, 0xffffffff}, // XXX dither when going from 24 to 16 bit?
                .clear_mode = VIVS_RS_CLEAR_CONTROL_MODE_DISABLED,
                .width = drawable->width,
                .height = drawable->height
            });
    etna_set_state(ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
    etna_submit_rs_state(ctx, &copy_to_screen);
    /* Flush RS */
    etna_set_state(ctx, VIVS_RS_FLUSH_CACHE, VIVS_RS_FLUSH_CACHE_FLUSH);
    DBG_F(ETNA_FRAME_MSGS,
            "Queued RS command to flush screen from %08x to %08x stride=%08x width=%i height=%i, ctx %p",
            rt_resource->levels[0].address,
            drawable->addr, drawable->stride,
            drawable->width, drawable->height, ctx);
    ectx->base.flush(&ectx->base, fence, 0);
}

bool etna_screen_resource_alloc_ts(struct pipe_screen *screen, struct etna_resource *resource)
{
    struct etna_screen *priv = etna_screen(screen);
    size_t rt_ts_size;
    assert(!resource->ts);
    /* TS only for level 0 -- XXX is this formula correct? */
    rt_ts_size = align(resource->levels[0].size*priv->specs.bits_per_tile/0x80, 0x100);
    if(rt_ts_size == 0)
        return true;

    DBG_F(ETNA_RESOURCE_MSGS, "%p: Allocating tile status of size %i", resource, rt_ts_size);
    struct etna_vidmem *rt_ts = 0;
    if(unlikely(etna_vidmem_alloc_linear(priv->dev, &rt_ts, rt_ts_size, VIV_SURF_TILE_STATUS, VIV_POOL_DEFAULT, true)!=ETNA_OK))
    {
        printf("Problem allocating tile status for resource\n");
        return false;
    }
    resource->ts = rt_ts;
    resource->levels[0].ts_address = resource->ts->address;
    resource->levels[0].ts_size = resource->ts->size;
    return true;
}

struct pipe_screen *
etna_screen_create(struct viv_conn *dev)
{
    struct etna_screen *screen = CALLOC_STRUCT(etna_screen);
    struct pipe_screen *pscreen = &screen->base;
    screen->dev = dev;

    /* Determine specs for device */
    screen->specs.can_supertile = VIV_FEATURE(dev, chipMinorFeatures0, SUPER_TILED);
    screen->specs.bits_per_tile = VIV_FEATURE(dev, chipMinorFeatures0, 2BITPERTILE)?2:4;
    screen->specs.ts_clear_value = VIV_FEATURE(dev, chipMinorFeatures0, 2BITPERTILE)?0x55555555:0x11111111;
    screen->specs.vertex_sampler_offset = 8; /* vertex and fragment samplers live in one address space, with vertex shaders at this offset */
    screen->specs.fragment_sampler_count = 8;
    screen->specs.vertex_sampler_count = 4;
    screen->specs.vs_need_z_div = dev->chip.chip_model < 0x1000 && dev->chip.chip_model != 0x880;
    screen->specs.vertex_output_buffer_size = dev->chip.vertex_output_buffer_size;
    screen->specs.vertex_cache_size = dev->chip.vertex_cache_size;
    screen->specs.shader_core_count = dev->chip.shader_core_count;
    screen->specs.stream_count = dev->chip.stream_count;
    screen->specs.has_sin_cos_sqrt = VIV_FEATURE(dev, chipMinorFeatures0, HAS_SQRT_TRIG);
    screen->specs.has_shader_range_registers = dev->chip.chip_model >= 0x1000 || dev->chip.chip_model == 0x880;    
    if (dev->chip.chip_model < 0x1000 && dev->chip.chip_model != 0x880)
    {
        screen->specs.vs_offset = 0x4000;
        screen->specs.ps_offset = 0x6000;
    }
    else
    {
        screen->specs.vs_offset = 0xC000;
        screen->specs.ps_offset = 0xD000; //like vivante driver
    }

    /* Initialize vtable */
    pscreen->destroy = etna_screen_destroy;
    pscreen->get_name = etna_screen_get_name;
    pscreen->get_vendor = etna_screen_get_vendor;
    pscreen->get_param = etna_screen_get_param;
    pscreen->get_paramf = etna_screen_get_paramf;
    pscreen->get_shader_param = etna_screen_get_shader_param;
    pscreen->get_video_param = etna_screen_get_video_param;
    pscreen->get_compute_param = etna_screen_get_compute_param;
    pscreen->get_timestamp = etna_screen_get_timestamp;
    pscreen->context_create = etna_screen_context_create;
    pscreen->is_format_supported = etna_screen_is_format_supported;
    pscreen->is_video_format_supported = etna_screen_is_video_format_supported;
    pscreen->flush_frontbuffer = etna_screen_flush_frontbuffer;

    etna_screen_fence_init(pscreen);
    etna_screen_resource_init(pscreen);

    pipe_mutex_init(screen->fence_mutex);

    return pscreen;
}

