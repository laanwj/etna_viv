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
/* Pipe memory transfer
 */
#include "etna_transfer.h"
#include "etna_pipe.h"

#include "pipe/p_defines.h"
#include "pipe/p_format.h"
#include "pipe/p_state.h"
#include "util/u_format.h"
#include "util/u_memory.h"
#include "util/u_surface.h"

/* Compute offset into a 1D/2D/3D buffer of a certain box.
 * This box must be aligned to the block width and height of the underlying format.
 */
static inline size_t etna_compute_offset(enum pipe_format format, const struct pipe_box *box,
        size_t stride, size_t layer_stride)
{
    return box->z * layer_stride +
           box->y / util_format_get_blockheight(format) * stride +
           box->x / util_format_get_blockwidth(format) * util_format_get_blocksize(format);
}

void *etna_pipe_transfer_map(struct pipe_context *pipe,
                         struct pipe_resource *resource,
                         unsigned level,
                         unsigned usage,  /* a combination of PIPE_TRANSFER_x */
                         const struct pipe_box *box,
                         struct pipe_transfer **out_transfer)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct etna_transfer *ptrans = util_slab_alloc(&priv->transfer_pool);
    struct etna_resource *resource_priv = etna_resource(resource);
    enum pipe_format format = resource->format;
    if (!ptrans)
        return NULL;
    assert(level <= resource->last_level);

    /* XXX we don't handle PIPE_TRANSFER_READ; this needs to be handled separately, and always
     * requires a sync. */
    /* XXX we don't handle PIPE_TRANSFER_FLUSH_EXPLICIT; this flag can be ignored when mapping in-place,
     * but when not in place we need to fire off the copy operation in transfer_flush_region (currently
     * a no-op) instead of unmap. Need to handle this to support ARB_map_buffer_range extension at least.
     */ 
    /* XXX we don't take care of current operations on the resource; which can be, at some point in the pipeline
       which is not yet executed:
      
       - bound as surface
       - bound through vertex buffer
       - bound through index buffer
       - bound in sampler view
       - used in clear_render_target / clear_depth_stencil operation
       - used in blit
       - used in resource_copy_region

       How do other drivers record this information over course of the rendering pipeline?
       Is it necessary at all? Only in case we want to provide a fast path and map the resource directly
       (and for PIPE_TRANSFER_MAP_DIRECTLY) and we don't want to force a sync.
       We also need to know whether the resource is in use to determine if a sync is needed (or just do it
       always, but that comes at the expense of performance).

       A conservative approximation without too much overhead would be to mark all resources that have 
       been bound at some point as busy. A drawback would be that accessing resources that have 
       been bound but are no longer in use for a while still carry a performance penalty. On the other hand,
       the program could be using PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE or PIPE_TRANSFER_UNSYNCHRONIZED to 
       avoid this in the first place...
       
       A) We use an in-pipe copy engine, and queue the copy operation after unmap so that the copy 
          will be performed when all current commands have been executed.
          Using the RS is possible, not sure if always efficient. This can also do any kind of tiling for us.
          Only possible when PIPE_TRANSFER_DISCARD_RANGE is set.
       B) We discard the entire resource (or at least, the mipmap level) and allocate new memory for it.
          Only possible when mapping the entire resource or PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE is set.
     */

    /* No need to allocate a buffer for copying if the resource is not in use,
     * and no tiling is needed, can just return a direct pointer.
     */
    ptrans->in_place = resource_priv->layout == ETNA_LAYOUT_LINEAR ||
                       (resource_priv->layout == ETNA_LAYOUT_TILED && util_format_is_compressed(resource->format));
    ptrans->base.resource = resource;
    ptrans->base.level = level;
    ptrans->base.usage = usage;
    ptrans->base.box = *box;

    if(likely(ptrans->in_place))
    {
        struct etna_resource_level *res_level = &resource_priv->levels[level];
        ptrans->base.stride = res_level->stride;
        ptrans->base.layer_stride = res_level->layer_stride;
        ptrans->buffer = res_level->logical + etna_compute_offset(resource->format, box, res_level->stride, res_level->layer_stride);
    } else {
        unsigned divSizeX = util_format_get_blockwidth(format);
        unsigned divSizeY = util_format_get_blockheight(format);
        if(usage & PIPE_TRANSFER_MAP_DIRECTLY)
        {
            /* No in-place transfer possible */
            util_slab_free(&priv->transfer_pool, ptrans);
            return NULL;
        }

        ptrans->base.stride = align(box->width, divSizeX) * util_format_get_blocksize(format); /* row stride in bytes */
        ptrans->base.layer_stride = align(box->height, divSizeY) * ptrans->base.stride;
        size_t size = ptrans->base.layer_stride * box->depth;
        ptrans->buffer = MALLOC(size);
    }

    *out_transfer = &ptrans->base;
    return ptrans->buffer;
}
   
void etna_pipe_transfer_flush_region(struct pipe_context *pipe,
				  struct pipe_transfer *transfer_,
				  const struct pipe_box *box)
{
    /* NOOP for now */
}

void etna_pipe_transfer_unmap(struct pipe_context *pipe,
                      struct pipe_transfer *transfer_)
{
    struct etna_pipe_context_priv *priv = ETNA_PIPE(pipe);
    struct etna_transfer *ptrans = etna_transfer(transfer_);

    /* XXX 
     * When writing to a resource that is already in use, replace the resource with a completely new buffer
     * and free the old one using a fenced free.
     * The most tricky case to implement will be: tiled or supertiled surface, partial write, target not aligned to 4/64
     */
    struct etna_resource *resource = etna_resource(ptrans->base.resource);
    assert(ptrans->base.level <= resource->base.last_level);
    struct etna_resource_level *level = &resource->levels[ptrans->base.level];

    if(unlikely(!ptrans->in_place))
    {
        if(resource->layout == ETNA_LAYOUT_LINEAR || resource->layout == ETNA_LAYOUT_TILED)
        {
            if(resource->layout == ETNA_LAYOUT_TILED && !util_format_is_compressed(resource->base.format))
            {
                uint bpe = util_format_get_blocksize(resource->base.format);
                /* XXX currently only handles multiples of the tile size */
                void *ptr = level->logical + etna_compute_offset(resource->base.format, &ptrans->base.box, level->stride, level->layer_stride);
                /* XXX pipe_linear_to_tile */
                etna_texture_tile(ptr, ptrans->buffer, ptrans->base.box.width, ptrans->base.box.height, 
                        ptrans->base.stride, bpe);
            } else { /* non-tiled or compressed format */
                util_copy_box(level->logical,
                  resource->base.format,
                  level->stride, level->layer_stride,
                  ptrans->base.box.x, ptrans->base.box.y, ptrans->base.box.z,
                  ptrans->base.box.width, ptrans->base.box.height, ptrans->base.box.depth,
                  ptrans->buffer,
                  ptrans->base.stride, ptrans->base.layer_stride,
                  0, 0, 0);
            }
        } else
        {
            printf("etna_pipe_transfer_unmap: unsupported tiling %i\n", resource->layout);
        }
        FREE(ptrans->buffer);
    }
    util_slab_free(&priv->transfer_pool, ptrans);
}

