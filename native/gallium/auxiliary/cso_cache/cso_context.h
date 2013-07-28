/**************************************************************************
 *
 * Copyright 2007-2008 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#ifndef CSO_CONTEXT_H
#define CSO_CONTEXT_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "pipe/p_defines.h"


#ifdef	__cplusplus
extern "C" {
#endif

struct cso_context;
struct u_vbuf;

struct cso_context *cso_create_context( struct pipe_context *pipe );

void cso_release_all( struct cso_context *ctx );

void cso_destroy_context( struct cso_context *cso );



enum pipe_error cso_set_blend( struct cso_context *cso,
                               const struct pipe_blend_state *blend );
void cso_save_blend(struct cso_context *cso);
void cso_restore_blend(struct cso_context *cso);



enum pipe_error cso_set_depth_stencil_alpha( struct cso_context *cso,
                                             const struct pipe_depth_stencil_alpha_state *dsa );
void cso_save_depth_stencil_alpha(struct cso_context *cso);
void cso_restore_depth_stencil_alpha(struct cso_context *cso);



enum pipe_error cso_set_rasterizer( struct cso_context *cso,
                                    const struct pipe_rasterizer_state *rasterizer );
void cso_save_rasterizer(struct cso_context *cso);
void cso_restore_rasterizer(struct cso_context *cso);


enum pipe_error
cso_set_samplers(struct cso_context *cso,
                 unsigned shader_stage,
                 unsigned count,
                 const struct pipe_sampler_state **states);

void
cso_save_samplers(struct cso_context *cso, unsigned shader_stage);

void
cso_restore_samplers(struct cso_context *cso, unsigned shader_stage);

/* Alternate interface to support state trackers that like to modify
 * samplers one at a time:
 */
enum pipe_error
cso_single_sampler(struct cso_context *cso,
                   unsigned shader_stage,
                   unsigned count,
                   const struct pipe_sampler_state *states);

void
cso_single_sampler_done(struct cso_context *cso, unsigned shader_stage);


enum pipe_error cso_set_vertex_elements(struct cso_context *ctx,
                                        unsigned count,
                                        const struct pipe_vertex_element *states);
void cso_save_vertex_elements(struct cso_context *ctx);
void cso_restore_vertex_elements(struct cso_context *ctx);


void cso_set_vertex_buffers(struct cso_context *ctx,
                            unsigned start_slot, unsigned count,
                            const struct pipe_vertex_buffer *buffers);

/* One vertex buffer slot is provided with the save/restore functionality.
 * cso_context chooses the slot, it can be non-zero. */
void cso_save_aux_vertex_buffer_slot(struct cso_context *ctx);
void cso_restore_aux_vertex_buffer_slot(struct cso_context *ctx);
unsigned cso_get_aux_vertex_buffer_slot(struct cso_context *ctx);


void cso_set_stream_outputs(struct cso_context *ctx,
                            unsigned num_targets,
                            struct pipe_stream_output_target **targets,
                            unsigned append_bitmask);
void cso_save_stream_outputs(struct cso_context *ctx);
void cso_restore_stream_outputs(struct cso_context *ctx);


/*
 * We don't provide shader caching in CSO.  Most of the time the api provides
 * object semantics for shaders anyway, and the cases where it doesn't
 * (eg mesa's internally-generated texenv programs), it will be up to
 * the state tracker to implement their own specialized caching.
 */

void cso_set_fragment_shader_handle(struct cso_context *ctx, void *handle);
void cso_delete_fragment_shader(struct cso_context *ctx, void *handle );
void cso_save_fragment_shader(struct cso_context *cso);
void cso_restore_fragment_shader(struct cso_context *cso);


void cso_set_vertex_shader_handle(struct cso_context *ctx, void *handle);
void cso_delete_vertex_shader(struct cso_context *ctx, void *handle );
void cso_save_vertex_shader(struct cso_context *cso);
void cso_restore_vertex_shader(struct cso_context *cso);


void cso_set_geometry_shader_handle(struct cso_context *ctx, void *handle);
void cso_delete_geometry_shader(struct cso_context *ctx, void *handle);
void cso_save_geometry_shader(struct cso_context *cso);
void cso_restore_geometry_shader(struct cso_context *cso);


void cso_set_framebuffer(struct cso_context *cso,
                         const struct pipe_framebuffer_state *fb);
void cso_save_framebuffer(struct cso_context *cso);
void cso_restore_framebuffer(struct cso_context *cso);


void cso_set_viewport(struct cso_context *cso,
                      const struct pipe_viewport_state *vp);
void cso_save_viewport(struct cso_context *cso);
void cso_restore_viewport(struct cso_context *cso);


void cso_set_blend_color(struct cso_context *cso,
                         const struct pipe_blend_color *bc);

void cso_set_sample_mask(struct cso_context *cso, unsigned stencil_mask);
void cso_save_sample_mask(struct cso_context *ctx);
void cso_restore_sample_mask(struct cso_context *ctx);

void cso_set_stencil_ref(struct cso_context *cso,
                         const struct pipe_stencil_ref *sr);
void cso_save_stencil_ref(struct cso_context *cso);
void cso_restore_stencil_ref(struct cso_context *cso);

void cso_set_render_condition(struct cso_context *cso,
                              struct pipe_query *query,
                              boolean condition, uint mode);
void cso_save_render_condition(struct cso_context *cso);
void cso_restore_render_condition(struct cso_context *cso);


/* clip state */

void
cso_set_clip(struct cso_context *cso,
             const struct pipe_clip_state *clip);

void
cso_save_clip(struct cso_context *cso);

void
cso_restore_clip(struct cso_context *cso);


/* sampler view state */

void
cso_set_sampler_views(struct cso_context *cso,
                      unsigned shader_stage,
                      unsigned count,
                      struct pipe_sampler_view **views);

void
cso_save_sampler_views(struct cso_context *cso, unsigned shader_stage);

void
cso_restore_sampler_views(struct cso_context *cso, unsigned shader_stage);


/* constant buffers */

void cso_set_constant_buffer(struct cso_context *cso, unsigned shader_stage,
                             unsigned index, struct pipe_constant_buffer *cb);
void cso_set_constant_buffer_resource(struct cso_context *cso,
                                      unsigned shader_stage,
                                      unsigned index,
                                      struct pipe_resource *buffer);
void cso_save_constant_buffer_slot0(struct cso_context *cso,
                                    unsigned shader_stage);
void cso_restore_constant_buffer_slot0(struct cso_context *cso,
                                       unsigned shader_stage);


/* drawing */

void
cso_set_index_buffer(struct cso_context *cso,
                     const struct pipe_index_buffer *ib);

void
cso_draw_vbo(struct cso_context *cso,
             const struct pipe_draw_info *info);

/* helper drawing function */
void
cso_draw_arrays(struct cso_context *cso, uint mode, uint start, uint count);

#ifdef	__cplusplus
}
#endif

#endif
