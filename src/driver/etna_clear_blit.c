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
/* Clearing and blitting functionality */
#include "etna_clear_blit.h"

#include "etna_internal.h"
#include "etna_pipe.h"
#include "etna_resource.h"
#include "etna_translate.h"
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "util/u_blitter.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_surface.h"

#include <etnaviv/common.xml.h>
#include <etnaviv/state.xml.h>
#include <etnaviv/state_3d.xml.h>

/* Save current state for blitter operation */
static void etna_pipe_blit_save_state(struct pipe_context *pipe)
{
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
    util_blitter_save_vertex_buffer_slot(priv->blitter, &priv->vertex_buffer_s[0]);
    util_blitter_save_vertex_elements(priv->blitter, priv->vertex_elements_p);
    util_blitter_save_vertex_shader(priv->blitter, priv->vs);
    util_blitter_save_rasterizer(priv->blitter, priv->rasterizer_p);
    util_blitter_save_viewport(priv->blitter, &priv->viewport_s);
    util_blitter_save_scissor(priv->blitter, &priv->scissor_s);
    util_blitter_save_fragment_shader(priv->blitter, priv->fs);
    util_blitter_save_blend(priv->blitter, priv->blend_p);
    util_blitter_save_depth_stencil_alpha(priv->blitter, priv->depth_stencil_alpha_p);
    util_blitter_save_stencil_ref(priv->blitter, &priv->stencil_ref_s);
    util_blitter_save_sample_mask(priv->blitter, priv->sample_mask_s);
    util_blitter_save_framebuffer(priv->blitter, &priv->framebuffer_s);
    util_blitter_save_fragment_sampler_states(priv->blitter,
                    priv->num_fragment_samplers,
                    (void **)priv->sampler);
    util_blitter_save_fragment_sampler_views(priv->blitter,
                    priv->num_fragment_sampler_views, priv->sampler_view_s);
}

/* Generate clear command for a surface (non-fast clear case) */
void etna_rs_gen_clear_surface(struct etna_ctx *ctx, struct compiled_rs_state *rs_state, struct etna_surface *surf, uint32_t clear_value)
{
    uint bs = util_format_get_blocksize(surf->base.format);
    uint format = 0;
    switch(bs)
    {
    case 2: format = RS_FORMAT_A1R5G5B5; break;
    case 4: format = RS_FORMAT_A8R8G8B8; break;
    default: BUG("etna_rs_gen_clear_surface: Unhandled clear blocksize: %i (fmt %i)", bs, surf->base.format);
             format = RS_FORMAT_A8R8G8B8;
             assert(0);
    }
    /* use tiled clear if width is multiple of 16 */
    bool tiled_clear = (surf->surf.padded_width & ETNA_RS_WIDTH_MASK) == 0 &&
                       (surf->surf.padded_height & ETNA_RS_HEIGHT_MASK) == 0;
    struct etna_bo *dest_bo = etna_resource(surf->base.texture)->bo;
    etna_compile_rs_state(ctx, rs_state, &(struct rs_state){
            .source_format = format,
            .dest_format = format,
            .dest_addr[0] = etna_bo_gpu_address(dest_bo) + surf->surf.offset,
            .dest_stride = surf->surf.stride,
            .dest_tiling = tiled_clear ? surf->layout : ETNA_LAYOUT_LINEAR,
            .dither = {0xffffffff, 0xffffffff},
            .width = surf->surf.padded_width, /* These must be padded to 16x4 if !LINEAR, otherwise RS will hang */
            .height = surf->surf.padded_height,
            .clear_value = {clear_value},
            .clear_mode = VIVS_RS_CLEAR_CONTROL_MODE_ENABLED1,
            .clear_bits = 0xffff
        });
}

static void etna_pipe_clear(struct pipe_context *pipe,
             unsigned buffers,
             const union pipe_color_union *color,
             double depth,
             unsigned stencil)
{
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
    /* Flush color and depth cache before clearing anything.
     * This is especially important when coming from another surface, as otherwise it may clear
     * part of the old surface instead.
     */
    etna_set_state(priv->ctx, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
    etna_stall(priv->ctx, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);
    /* Preparation: Flush the TS if needed. This must be done after flushing
     * color and depth, otherwise it can result in crashes */
    bool need_ts_flush = false;
    if((buffers & PIPE_CLEAR_COLOR) && priv->framebuffer_s.nr_cbufs)
    {
        struct etna_surface *surf = etna_surface(priv->framebuffer_s.cbufs[0]);
        if(surf->surf.ts_size)
            need_ts_flush = true;
    }
    if((buffers & PIPE_CLEAR_DEPTHSTENCIL) && priv->framebuffer_s.zsbuf != NULL)
    {
        struct etna_surface *surf = etna_surface(priv->framebuffer_s.zsbuf);
        if(surf->surf.ts_size)
            need_ts_flush = true;
    }
    if(need_ts_flush)
    {
        etna_set_state(priv->ctx, VIVS_TS_FLUSH_CACHE, VIVS_TS_FLUSH_CACHE_FLUSH);
    }
    /* No need to set up the TS here as RS clear operations (in contrast to
     * resolve and copy) do not require the TS state.
     */
    if(buffers & PIPE_CLEAR_COLOR)
    {
        for(int idx=0; idx<priv->framebuffer_s.nr_cbufs; ++idx)
        {
            struct etna_surface *surf = etna_surface(priv->framebuffer_s.cbufs[idx]);
            uint32_t new_clear_value = translate_clear_color(surf->base.format, &color[idx]);
            if(surf->surf.ts_size) /* TS: use precompiled clear command */
            {
                /* Set new clear color */
                priv->framebuffer.TS_COLOR_CLEAR_VALUE = new_clear_value;
                if(!DBG_ENABLED(ETNA_DBG_NO_AUTODISABLE))
                {
                    /* Set number of color tiles to be filled */
                    etna_set_state(priv->ctx, VIVS_TS_COLOR_AUTO_DISABLE_COUNT, surf->surf.padded_width*surf->surf.padded_height/16);
                    priv->framebuffer.TS_MEM_CONFIG |= VIVS_TS_MEM_CONFIG_COLOR_AUTO_DISABLE;
                }
                priv->dirty_bits |= ETNA_STATE_TS;
            }
            else if(unlikely(new_clear_value != surf->level->clear_value)) /* Queue normal RS clear for non-TS surfaces */
            {
                /* If clear color changed, re-generate stored command */
                etna_rs_gen_clear_surface(priv->ctx, &surf->clear_command, surf, new_clear_value);
            }
            etna_submit_rs_state(priv->ctx, &surf->clear_command);
            surf->level->clear_value = new_clear_value;
        }
    }
    if((buffers & PIPE_CLEAR_DEPTHSTENCIL) && priv->framebuffer_s.zsbuf != NULL)
    {
        struct etna_surface *surf = etna_surface(priv->framebuffer_s.zsbuf);
        uint32_t new_clear_value = translate_clear_depth_stencil(surf->base.format, depth, stencil);
        if(surf->surf.ts_size) /* TS: use precompiled clear command */
        {
            /* Set new clear depth value */
            priv->framebuffer.TS_DEPTH_CLEAR_VALUE = new_clear_value;
            if(!DBG_ENABLED(ETNA_DBG_NO_AUTODISABLE))
            {
                /* Set number of depth tiles to be filled */
                etna_set_state(priv->ctx, VIVS_TS_DEPTH_AUTO_DISABLE_COUNT, surf->surf.padded_width*surf->surf.padded_height/16);
                priv->framebuffer.TS_MEM_CONFIG |= VIVS_TS_MEM_CONFIG_DEPTH_AUTO_DISABLE;
            }
            priv->dirty_bits |= ETNA_STATE_TS;
        } else if(unlikely(new_clear_value != surf->level->clear_value)) /* Queue normal RS clear for non-TS surfaces */
        {
            /* If clear depth value changed, re-generate stored command */
            etna_rs_gen_clear_surface(priv->ctx, &surf->clear_command, surf, new_clear_value);
        }
        etna_submit_rs_state(priv->ctx, &surf->clear_command);
        surf->level->clear_value = new_clear_value;
    }
    etna_stall(priv->ctx, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);
}

static void etna_pipe_clear_render_target(struct pipe_context *pipe,
                           struct pipe_surface *dst,
                           const union pipe_color_union *color,
                           unsigned dstx, unsigned dsty,
                           unsigned width, unsigned height)
{
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
    /* XXX could fall back to RS when target area is full screen / resolveable and no TS. */
    etna_pipe_blit_save_state(pipe);
    util_blitter_clear_render_target(priv->blitter, dst, color, dstx, dsty, width, height);
}

static void etna_pipe_clear_depth_stencil(struct pipe_context *pipe,
                           struct pipe_surface *dst,
                           unsigned clear_flags,
                           double depth,
                           unsigned stencil,
                           unsigned dstx, unsigned dsty,
                           unsigned width, unsigned height)
{
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
    /* XXX could fall back to RS when target area is full screen / resolveable and no TS. */
    etna_pipe_blit_save_state(pipe);
    util_blitter_clear_depth_stencil(priv->blitter, dst, clear_flags, depth, stencil, dstx, dsty, width, height);
}

static void etna_pipe_resource_copy_region(struct pipe_context *pipe,
                            struct pipe_resource *dst,
                            unsigned dst_level,
                            unsigned dstx, unsigned dsty, unsigned dstz,
                            struct pipe_resource *src,
                            unsigned src_level,
                            const struct pipe_box *src_box)
{
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
    /* The resource must be of the same format. */
    assert(src->format == dst->format);
    /* Resources with nr_samples > 1 are not allowed. */
    assert(src->nr_samples == 1 && dst->nr_samples == 1);
    /* XXX we can use the RS as a literal copy engine here
     * the only complexity is tiling; the size of the boxes needs to be aligned to the tile size
     * how to handle the case where a resource is copied from/to a non-aligned position?
     * from non-aligned: can fall back to rendering-based copy?
     * to non-aligned: can fall back to rendering-based copy?
     * XXX this goes wrong when source surface is supertiled.
     */
    etna_pipe_blit_save_state(pipe);
    util_blitter_copy_texture(priv->blitter, dst, dst_level, dstx, dsty, dstz, src, src_level, src_box,
               PIPE_MASK_RGBA, false);
    etna_resource_touch(pipe, dst);
    etna_resource_touch(pipe, src);
}

static void etna_pipe_blit(struct pipe_context *pipe, const struct pipe_blit_info *blit_info)
{
    /* This is a more extended version of resource_copy_region */
    /* TODO Some cases can be handled by RS; if not, fall back to rendering or even CPU */
    /* copy block of pixels from info->src to info->dst (resource, level, box, format);
     * function is used for scaling, flipping in x and y direction (negative width/height), format conversion, mask and filter
     * and even a scissor rectangle
     *
     * What can the RS do for us:
     *   convert between tiling formats (layouts)
     *   downsample 2x in x and y
     *   convert between a limited number of pixel formats
     *
     * For the rest, fall back to util_blitter
     * XXX this goes wrong when source surface is supertiled.
     */
    struct pipe_blit_info info = *blit_info;
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
    if (info.src.resource->nr_samples > 1 &&
                info.dst.resource->nr_samples <= 1 &&
                !util_format_is_depth_or_stencil(info.src.resource->format) &&
                !util_format_is_pure_integer(info.src.resource->format)) {
        DBG("color resolve unimplemented");
        return;
    }
    if (util_try_blit_via_copy_region(pipe, blit_info)) {
        return; /* done */
    }
    if (info.mask & PIPE_MASK_S) {
        DBG("cannot blit stencil, skipping");
        info.mask &= ~PIPE_MASK_S;
    }

    if (!util_blitter_is_blit_supported(priv->blitter, &info)) {
        DBG("blit unsupported %s -> %s",
                        util_format_short_name(info.src.resource->format),
                        util_format_short_name(info.dst.resource->format));
        return;
    }

    etna_pipe_blit_save_state(pipe);
    util_blitter_blit(priv->blitter, &info);
    etna_resource_touch(pipe, info.src.resource);
    etna_resource_touch(pipe, info.dst.resource);
}


void etna_pipe_clear_blit_init(struct pipe_context *pc)
{
    struct etna_pipe_context *priv = etna_pipe_context(pc);
    pc->clear = etna_pipe_clear;
    pc->clear_render_target = etna_pipe_clear_render_target;
    pc->clear_depth_stencil = etna_pipe_clear_depth_stencil;
    pc->resource_copy_region = etna_pipe_resource_copy_region;
    pc->blit = etna_pipe_blit;

    priv->blitter = util_blitter_create(pc);
}

void etna_pipe_clear_blit_destroy(struct pipe_context *pc)
{
    struct etna_pipe_context *priv = etna_pipe_context(pc);
    if (priv->blitter)
        util_blitter_destroy(priv->blitter);
}

