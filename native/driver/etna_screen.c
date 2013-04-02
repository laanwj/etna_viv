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
#include "viv.h"

int etna_mesa_debug = ETNA_DBG_MSGS;  /* XXX */

static void etna_screen_destroy( struct pipe_screen *screen )
{
    DBG("unimplemented etna_screen_destroy");
}

static const char *etna_screen_get_name( struct pipe_screen *screen )
{
    DBG("unimplemented etna_screen_get_name");
    return NULL;
}

static const char *etna_screen_get_vendor( struct pipe_screen *screen )
{
    DBG("unimplemented etna_screen_get_vendor");
    return NULL;
}

static int etna_screen_get_param( struct pipe_screen *screen, enum pipe_cap param )
{
    DBG("unimplemented etna_screen_get_param");
    return 0;
}

static float etna_screen_get_paramf( struct pipe_screen *screen, enum pipe_capf param )
{
    DBG("unimplemented etna_screen_get_paramf");
    return 0.0f;
}

static int etna_screen_get_shader_param( struct pipe_screen *screen, unsigned shader, enum pipe_shader_cap param )
{
    DBG("unimplemented etna_screen_get_shader_param");
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
    DBG("unimplemented etna_screen_context_create");
    return NULL;
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

