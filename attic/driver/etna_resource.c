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
/* Resource handling.
 */
#include "etna_resource.h"

#include "etna_pipe.h"
#include "etna_screen.h"
#include "etna_debug.h"
#include "etna_translate.h"

#include "util/u_format.h"
#include "util/u_memory.h"
#include "util/u_math.h"
#include "util/u_inlines.h"
#include "util/u_transfer.h" /* u_default_resource_get_handle */

/* Associate an resource with this context when it is bound in any way
 * (vertex buffer, index buffer, texture, surface, blit).
 */
void etna_resource_touch(struct pipe_context *pipe, struct pipe_resource *resource_)
{
    struct etna_pipe_context *ectx = etna_pipe_context(pipe);
    struct etna_resource *resource = etna_resource(resource_);
    if(resource == NULL)
        return;
    resource->last_ctx = ectx;
}

bool etna_screen_resource_alloc_ts(struct pipe_screen *screen, struct etna_resource *resource)
{
    struct etna_screen *priv = etna_screen(screen);
    size_t rt_ts_size;
    assert(!resource->ts_bo);
    /* TS only for level 0 -- XXX is this formula correct? */
    rt_ts_size = align(resource->levels[0].size*priv->specs.bits_per_tile/0x80, 0x100);
    if(rt_ts_size == 0)
        return true;

    DBG_F(ETNA_DBG_RESOURCE_MSGS, "%p: Allocating tile status of size %i", resource, rt_ts_size);
    struct etna_bo *rt_ts = 0;
    if(unlikely((rt_ts = etna_bo_new(priv->dev, rt_ts_size, DRM_ETNA_GEM_TYPE_TS)) == NULL))
    {
        BUG("Problem allocating tile status for resource");
        return false;
    }
    resource->ts_bo = rt_ts;
    resource->levels[0].ts_offset = 0;
    resource->levels[0].ts_size = etna_bo_size(resource->ts_bo);
    /* It is important to initialize the TS, as random pattern
     * can result in crashes. Do this on the CPU as this only happens once
     * per surface anyway and it's a small area, so it may not be worth
     * queuing this to the GPU.
     */
    void *ts_map = etna_bo_map(rt_ts);
    memset(ts_map, priv->specs.ts_clear_value, rt_ts_size);
    return true;
}


static boolean etna_screen_can_create_resource(struct pipe_screen *pscreen,
                              const struct pipe_resource *templat)
{
    struct etna_screen *screen = etna_screen(pscreen);
    if(!translate_samples_to_xyscale(templat->nr_samples, NULL, NULL, NULL))
        return false;
    if(templat->bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DEPTH_STENCIL | PIPE_BIND_SAMPLER_VIEW))
    {
        uint max_size = (templat->bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DEPTH_STENCIL)) ?
                            screen->specs.max_rendertarget_size :
                            screen->specs.max_texture_size;
        if(templat->width0 > max_size || templat->height0 > max_size)
            return false;
    }
    return true;
}

static struct pipe_resource * etna_screen_resource_from_handle(struct pipe_screen *screen,
                                              const struct pipe_resource *templat,
                                              struct winsys_handle *handle)
{
    DBG("unimplemented etna_screen_resource_from_handle");
    return NULL;
}

/* Allocate 2D texture or render target resource
 */
static struct pipe_resource * etna_screen_resource_create(struct pipe_screen *screen,
                                         const struct pipe_resource *templat)
{
    struct etna_screen *priv = etna_screen(screen);
    assert(templat);

    /* Check input */
    if(templat->target == PIPE_TEXTURE_CUBE)
    {
        assert(templat->array_size == 6);
    } else if (templat->target == PIPE_BUFFER)
    {
        assert(templat->format == PIPE_FORMAT_R8_UNORM); /* bytes; want TYPELESS or similar */
        assert(templat->array_size == 1);
        assert(templat->height0 == 1);
        assert(templat->depth0 == 1);
        assert(templat->array_size == 1);
        assert(templat->last_level == 0);
    } else
    {
        assert(templat->array_size == 1);
    }
    assert(templat->width0 != 0);
    assert(templat->height0 != 0);
    assert(templat->depth0 != 0);
    assert(templat->array_size != 0);

    /* Figure out what tiling to use -- for now, assume that textures cannot be supertiled, and cannot be linear.
     * There is a feature flag SUPERTILED_TEXTURE (not supported on any known hw) that may allow this, as well
     * as LINEAR_TEXTURE_SUPPORT (supported on gc880 and gc2000 at least), but not sure how it works.
     * Buffers always have LINEAR layout.
     */
    unsigned layout = ETNA_LAYOUT_LINEAR;
    if(templat->target != PIPE_BUFFER)
    {
        if(!(templat->bind & PIPE_BIND_SAMPLER_VIEW) && priv->specs.can_supertile &&
                !DBG_ENABLED(ETNA_DBG_NO_SUPERTILE))
            layout = ETNA_LAYOUT_SUPER_TILED;
        else
            layout = ETNA_LAYOUT_TILED;
    }

    /* multi tiled formats */
    if ((priv->dev->chip.pixel_pipes > 1) && !(templat->bind & PIPE_BIND_SAMPLER_VIEW))
    {
        if (layout == ETNA_LAYOUT_TILED)
            layout = ETNA_LAYOUT_MULTI_TILED;
        if (layout == ETNA_LAYOUT_SUPER_TILED)
            layout = ETNA_LAYOUT_MULTI_SUPERTILED;
    }

    /* Determine scaling for antialiasing, allow override using debug flag */
    int nr_samples = templat->nr_samples;
    if((templat->bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DEPTH_STENCIL)) &&
       !(templat->bind & PIPE_BIND_SAMPLER_VIEW))
    {
        if(DBG_ENABLED(ETNA_DBG_MSAA_2X))
            nr_samples = 2;
        if(DBG_ENABLED(ETNA_DBG_MSAA_4X))
            nr_samples = 4;
    }
    int msaa_xscale = 1, msaa_yscale = 1;
    if(!translate_samples_to_xyscale(nr_samples, &msaa_xscale, &msaa_yscale, NULL))
    {
        /* Number of samples not supported */
        return NULL;
    }

    /* Determine needed padding (alignment of height/width) */
    unsigned paddingX = 0, paddingY = 0;
    unsigned halign = TEXTURE_HALIGN_FOUR;
    etna_layout_multiple(layout,
            priv->dev->chip.pixel_pipes,
            (templat->bind & PIPE_BIND_SAMPLER_VIEW) && !VIV_FEATURE(priv->dev, chipMinorFeatures1, TEXTURE_HALIGN),
            &paddingX, &paddingY, &halign);
    assert(paddingX && paddingY);

    /* compute mipmap level sizes and offsets */
    struct etna_resource *resource = CALLOC_STRUCT(etna_resource);
    int max_mip_level = templat->last_level;
    if(unlikely(max_mip_level >= ETNA_NUM_LOD)) /* max LOD supported by hw */
        max_mip_level = ETNA_NUM_LOD - 1;

    unsigned ix = 0;
    unsigned x = templat->width0, y = templat->height0;
    unsigned offset = 0;
    while(true)
    {
        struct etna_resource_level *mip = &resource->levels[ix];
        mip->width = x;
        mip->height = y;
        mip->padded_width = align(x * msaa_xscale, paddingX);
        mip->padded_height = align(y * msaa_yscale, paddingY);
        mip->stride = util_format_get_stride(templat->format, mip->padded_width);
        mip->offset = offset;
        mip->layer_stride = mip->stride * util_format_get_nblocksy(templat->format, mip->padded_height);
        mip->size = templat->array_size * mip->layer_stride;
        offset += align(mip->size, ETNA_PE_ALIGNMENT); /* align mipmaps to 64 bytes to be able to render to them */
        if(ix == max_mip_level || (x == 1 && y == 1))
            break; // stop at last level
        x = u_minify(x, 1);
        y = u_minify(y, 1);
        ix += 1;
    }

    /* determine memory type */
    uint32_t flags = 0; /* XXX DRM_ETNA_GEM_CACHE_xxx */
    enum viv_surf_type memtype = VIV_SURF_UNKNOWN;
    if(templat->bind & PIPE_BIND_SAMPLER_VIEW)
        flags |= DRM_ETNA_GEM_TYPE_TEX;
    else if(templat->bind & PIPE_BIND_RENDER_TARGET)
        flags |= DRM_ETNA_GEM_TYPE_RT;
    else if(templat->bind & PIPE_BIND_DEPTH_STENCIL)
        flags |= DRM_ETNA_GEM_TYPE_ZS;
    else if(templat->bind & PIPE_BIND_INDEX_BUFFER)
        flags |= DRM_ETNA_GEM_TYPE_IDX;
    else if(templat->bind & PIPE_BIND_VERTEX_BUFFER)
        flags |= DRM_ETNA_GEM_TYPE_VTX;
    DBG_F(ETNA_DBG_RESOURCE_MSGS, "%p: Allocate surface of %ix%i (padded to %ix%i), %i layers, of format %s, size %08x flags %08x, memtype %i",
            resource,
            templat->width0, templat->height0, resource->levels[0].padded_width, resource->levels[0].padded_height, templat->array_size, util_format_name(templat->format),
            offset, templat->bind, memtype);

    struct etna_bo *bo = 0;
    if(unlikely((bo = etna_bo_new(priv->dev, offset, flags)) == NULL))
    {
        BUG("Problem allocating video memory for resource");
        return NULL;
    }

    resource->base = *templat;
    resource->base.last_level = ix; /* real last mipmap level */
    resource->base.screen = screen;
    resource->base.nr_samples = nr_samples;
    resource->layout = layout;
    resource->halign = halign;
    resource->bo = bo;
    resource->ts_bo = 0; /* TS is only created when first bound to surface */
    pipe_reference_init(&resource->base.reference, 1);

    /* calculate pipe addresses */
    resource->pipe_addr[0] = etna_bo_gpu_address(resource->bo) + resource->levels[0].offset;
    resource->pipe_addr[1] = etna_bo_gpu_address(resource->bo) + resource->levels[0].offset + (resource->levels[0].size / 2);

    if(DBG_ENABLED(ETNA_DBG_ZERO))
    {
        void *map = etna_bo_map(bo);
        memset(map, 0, offset);
    }

    return &resource->base;
}

static void etna_screen_resource_destroy(struct pipe_screen *screen,
                        struct pipe_resource *resource_)
{
    struct etna_screen *priv = etna_screen(screen);
    struct etna_resource *resource = etna_resource(resource_);
    if(resource == NULL)
        return;
    struct etna_queue *queue = NULL;
    if(resource->last_ctx != NULL)
    {
        /* XXX This could fail when multiple contexts share this resource,
         * (the last one to bind it will "own" it) or fail miserably if
         * the context was since destroyed.
         * Integrate this into etna_bo_del...
         */
        DBG_F(ETNA_DBG_RESOURCE_MSGS, "%p: resource queued destroyed (%ix%ix%i)", resource, resource_->width0, resource_->height0, resource_->depth0);
        queue = resource->last_ctx->ctx->queue;
    } else {
        DBG_F(ETNA_DBG_RESOURCE_MSGS, "%p: resource destroyed (%ix%ix%i)", resource, resource_->width0, resource_->height0, resource_->depth0);
    }
    etna_bo_del(priv->dev, resource->bo, queue);
    etna_bo_del(priv->dev, resource->ts_bo, queue);
    FREE(resource);
}

void etna_screen_resource_init(struct pipe_screen *pscreen)
{
    pscreen->can_create_resource = etna_screen_can_create_resource;
    pscreen->resource_create = etna_screen_resource_create;
    pscreen->resource_from_handle = etna_screen_resource_from_handle;
    pscreen->resource_get_handle = u_default_resource_get_handle;
    pscreen->resource_destroy = etna_screen_resource_destroy;
}

