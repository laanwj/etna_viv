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
#include "etna.h"
#include "etna_util.h"
#include "etna_pipe.h"
#include "viv.h"

int etna_mesa_debug = ETNA_DBG_MSGS;  /* XXX */

static void etna_screen_destroy( struct pipe_screen *screen )
{
    DBG("unimplemented etna_screen_destroy");
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
    switch (param) {
    /* Supported features (boolean caps). */
    case PIPE_CAP_TWO_SIDED_STENCIL:
    case PIPE_CAP_ANISOTROPIC_FILTER:
    case PIPE_CAP_POINT_SPRITE:
    case PIPE_CAP_TEXTURE_SHADOW_MAP:
    case PIPE_CAP_BLEND_EQUATION_SEPARATE:
    case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
    case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
    case PIPE_CAP_SM3:
    case PIPE_CAP_SEAMLESS_CUBE_MAP:
    case PIPE_CAP_TEXTURE_BARRIER:
    case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
    case PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY:
    case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
    case PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY:
    case PIPE_CAP_USER_CONSTANT_BUFFERS: /* constant buffers can be user buffers; they end up in command stream anyway */
            return 1;
    case PIPE_CAP_TGSI_TEXCOORD:
            return 0;

    case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
            return 256;

    case PIPE_CAP_GLSL_FEATURE_LEVEL:
            return 120;

    /* Unsupported features. */
    case PIPE_CAP_NPOT_TEXTURES: /* XXX supported on gc2000 */
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
    case PIPE_CAP_TGSI_CAN_COMPACT_VARYINGS: /* Don't skip strict max varying limit check */
    case PIPE_CAP_TGSI_CAN_COMPACT_CONSTANTS: /* Don't skip strict max uniform limit check */
    case PIPE_CAP_FRAGMENT_COLOR_CLAMPED:
    case PIPE_CAP_VERTEX_COLOR_CLAMPED:
    case PIPE_CAP_USER_VERTEX_BUFFERS:
    case PIPE_CAP_USER_INDEX_BUFFERS:
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
            return 12; /* XXX depends on caps */

    /* Render targets. */
    case PIPE_CAP_MAX_RENDER_TARGETS:
            return 1;

    /* Timer queries. */
    case PIPE_CAP_QUERY_TIME_ELAPSED:
    case PIPE_CAP_OCCLUSION_QUERY:
    case PIPE_CAP_QUERY_TIMESTAMP:
            return 0;

    case PIPE_CAP_MIN_TEXEL_OFFSET:
            return -8;

    case PIPE_CAP_MAX_TEXEL_OFFSET:
            return 7;

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
            return 16384;
    case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
            return 8; /* XXX */
    case PIPE_SHADER_CAP_MAX_INPUTS:
            return 32;
    case PIPE_SHADER_CAP_MAX_TEMPS:
            return 256; /* Max native temporaries. */
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
            return 1; /* Depends on HAS_SQRT_TRIG cap */
    case PIPE_SHADER_CAP_INTEGERS: /* XXX supported on gc2000 */
            return 0;
    case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
            return shader==PIPE_SHADER_FRAGMENT ? 16 : 8;
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
    return etna_new_pipe_context(es->dev);
}

static boolean etna_screen_is_format_supported( struct pipe_screen *screen,
                               enum pipe_format format,
                               enum pipe_texture_target target,
                               unsigned sample_count,
                               unsigned bindings )
{
    DBG("unimplemented etna_screen_is_format_supported");
    return false;
}

static boolean etna_screen_is_video_format_supported( struct pipe_screen *screen,
                                     enum pipe_format format,
                                     enum pipe_video_profile profile )
{
    DBG("unimplemented etna_screen_is_video_format_supported");
    return false;
}

static boolean etna_screen_can_create_resource(struct pipe_screen *screen,
                              const struct pipe_resource *templat)
{
    DBG("unimplemented etna_screen_can_create_resource");
    return false;
}
                           
static struct pipe_resource * etna_screen_resource_create(struct pipe_screen *screen,
                                         const struct pipe_resource *templat)
{
    DBG("unimplemented etna_screen_resource_create");
    return NULL;
}

static struct pipe_resource * etna_screen_resource_from_handle(struct pipe_screen *screen,
                                              const struct pipe_resource *templat,
                                              struct winsys_handle *handle)
{
    DBG("unimplemented etna_screen_resource_from_handle");
    return NULL;
}

static boolean etna_screen_resource_get_handle(struct pipe_screen *screen,
                              struct pipe_resource *tex,
                              struct winsys_handle *handle)
{
    DBG("unimplemented etna_screen_resource_get_handle");
    return false;
}

static void etna_screen_resource_destroy(struct pipe_screen *screen,
                        struct pipe_resource *pt)
{
    DBG("unimplemented etna_screen_resource_destroy");
}

static void etna_screen_flush_frontbuffer( struct pipe_screen *screen,
                          struct pipe_resource *resource,
                          unsigned level, unsigned layer,
                          void *winsys_drawable_handle )
{
    DBG("unimplemented etna_screen_flush_frontbuffer");
}

static void etna_screen_fence_reference( struct pipe_screen *screen,
                        struct pipe_fence_handle **ptr,
                        struct pipe_fence_handle *fence )
{
    DBG("unimplemented etna_screen_fence_reference");
}

static boolean etna_screen_fence_signalled( struct pipe_screen *screen,
                           struct pipe_fence_handle *fence )
{
    DBG("unimplemented etna_screen_fence_signalled");
    return false;
}

static boolean etna_screen_fence_finish( struct pipe_screen *screen,
                        struct pipe_fence_handle *fence,
                        uint64_t timeout )
{
    DBG("unimplemented etna_screen_fence_finish");
    return false;
}

struct pipe_screen *
fd_screen_create(struct viv_conn *dev)
{
    struct etna_screen *screen = ETNA_NEW(struct etna_screen);
    struct pipe_screen *pscreen = &screen->base;
    screen->dev = dev;

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
    pscreen->can_create_resource = etna_screen_can_create_resource;
    pscreen->resource_create = etna_screen_resource_create;
    pscreen->resource_from_handle = etna_screen_resource_from_handle;
    pscreen->resource_get_handle = etna_screen_resource_get_handle;
    pscreen->resource_destroy = etna_screen_resource_destroy;
    pscreen->flush_frontbuffer = etna_screen_flush_frontbuffer;
    pscreen->fence_reference = etna_screen_fence_reference;
    pscreen->fence_signalled = etna_screen_fence_signalled;
    pscreen->fence_finish = etna_screen_fence_finish;

    return pscreen;
}

