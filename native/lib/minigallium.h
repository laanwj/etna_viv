/**************************************************************************
 * 
 * Copyright 2008 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright 2009-2010 VMware, Inc.
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
#ifndef H_MINIGALLIUM
#define H_MINIGALLIUM

/* XXX the following three includes are implementation details that shouldn't
 * leak outside the driver 
 */
#include "etna_mem.h"
#include "etna_rs.h"

#define ETNA_NUM_LOD (14)
#define ETNA_NUM_LAYERS (6)

#define PIPE_MAX_CLIP_PLANES       8
#define PIPE_MAX_COLOR_BUFS        8
#define PIPE_MAX_SAMPLERS         16

#include "pipe/p_defines.h"
#include "pipe/p_format.h"

/********************************************************************
 * State from gallium, for experimentation
 *******************************************************************/

struct pipe_rasterizer_state
{
   unsigned flatshade:1;
   unsigned light_twoside:1;
   unsigned clamp_vertex_color:1;
   unsigned clamp_fragment_color:1;
   unsigned front_ccw:1;
   unsigned cull_face:2;      /**< PIPE_FACE_x */
   unsigned fill_front:2;     /**< PIPE_POLYGON_MODE_x */
   unsigned fill_back:2;      /**< PIPE_POLYGON_MODE_x */
   unsigned offset_point:1;
   unsigned offset_line:1;
   unsigned offset_tri:1;
   unsigned scissor:1;
   unsigned poly_smooth:1;
   unsigned poly_stipple_enable:1;
   unsigned point_smooth:1;
   unsigned sprite_coord_mode:1;     /**< PIPE_SPRITE_COORD_ */
   unsigned point_quad_rasterization:1; /** points rasterized as quads or points */
   unsigned point_size_per_vertex:1; /**< size computed in vertex shader */
   unsigned multisample:1;         /* XXX maybe more ms state in future */
   unsigned line_smooth:1;
   unsigned line_stipple_enable:1;
   unsigned line_last_pixel:1;

   /**
    * Use the first vertex of a primitive as the provoking vertex for
    * flat shading.
    */
   unsigned flatshade_first:1;

   /**
    * When true, triangle rasterization uses (0.5, 0.5) pixel centers
    * for determining pixel ownership.
    *
    * When false, triangle rasterization uses (0,0) pixel centers for
    * determining pixel ownership.
    *
    * Triangle rasterization always uses a 'top,left' rule for pixel
    * ownership, this just alters which point we consider the pixel
    * center for that test.
    */
   unsigned gl_rasterization_rules:1;

   /**
    * When true, rasterization is disabled and no pixels are written.
    * This only makes sense with the Stream Out functionality.
    */
   unsigned rasterizer_discard:1;

   /**
    * When false, depth clipping is disabled and the depth value will be
    * clamped later at the per-pixel level before depth testing.
    * This depends on PIPE_CAP_DEPTH_CLIP_DISABLE.
    */
   unsigned depth_clip:1;

   /**
    * Enable bits for clipping half-spaces.
    * This applies to both user clip planes and shader clip distances.
    * Note that if the bound shader exports any clip distances, these
    * replace all user clip planes, and clip half-spaces enabled here
    * but not written by the shader count as disabled.
    */
   unsigned clip_plane_enable:PIPE_MAX_CLIP_PLANES;

   unsigned line_stipple_factor:8;  /**< [1..256] actually */
   unsigned line_stipple_pattern:16;

   unsigned sprite_coord_enable; /* bitfield referring to 32 GENERIC inputs */

   float line_width;
   float point_size;           /**< used when no per-vertex size */
   float offset_units;
   float offset_scale;
   float offset_clamp;
};

struct pipe_viewport_state
{
   float scale[4];
   float translate[4];
};


struct pipe_scissor_state
{
   unsigned minx:16;
   unsigned miny:16;
   unsigned maxx:16;
   unsigned maxy:16;
};
struct pipe_depth_state 
{
   unsigned enabled:1;         /**< depth test enabled? */
   unsigned writemask:1;       /**< allow depth buffer writes? */
   unsigned func:3;            /**< depth test func (PIPE_FUNC_x) */
};


struct pipe_stencil_state
{
   unsigned enabled:1;  /**< stencil[0]: stencil enabled, stencil[1]: two-side enabled */
   unsigned func:3;     /**< PIPE_FUNC_x */
   unsigned fail_op:3;  /**< PIPE_STENCIL_OP_x */
   unsigned zpass_op:3; /**< PIPE_STENCIL_OP_x */
   unsigned zfail_op:3; /**< PIPE_STENCIL_OP_x */
   unsigned valuemask:8;
   unsigned writemask:8;
};


struct pipe_alpha_state
{
   unsigned enabled:1;
   unsigned func:3;     /**< PIPE_FUNC_x */
   float ref_value;     /**< reference value */
};


struct pipe_depth_stencil_alpha_state
{
   struct pipe_depth_state depth;
   struct pipe_stencil_state stencil[2]; /**< [0] = front, [1] = back */
   struct pipe_alpha_state alpha;
};


struct pipe_rt_blend_state
{
   unsigned blend_enable:1;

   unsigned rgb_func:3;          /**< PIPE_BLEND_x */
   unsigned rgb_src_factor:5;    /**< PIPE_BLENDFACTOR_x */
   unsigned rgb_dst_factor:5;    /**< PIPE_BLENDFACTOR_x */

   unsigned alpha_func:3;        /**< PIPE_BLEND_x */
   unsigned alpha_src_factor:5;  /**< PIPE_BLENDFACTOR_x */
   unsigned alpha_dst_factor:5;  /**< PIPE_BLENDFACTOR_x */

   unsigned colormask:4;         /**< bitmask of PIPE_MASK_R/G/B/A */
};

struct pipe_blend_state
{
   unsigned independent_blend_enable:1;
   unsigned logicop_enable:1;
   unsigned logicop_func:4;      /**< PIPE_LOGICOP_x */
   unsigned dither:1;
   unsigned alpha_to_coverage:1;
   unsigned alpha_to_one:1;
   struct pipe_rt_blend_state rt[PIPE_MAX_COLOR_BUFS];
};


struct pipe_blend_color
{
   float color[4];
};

struct pipe_stencil_ref
{
   uint8_t ref_value[2];
};

struct pipe_framebuffer_state
{
   unsigned width, height;

   /** multiple color buffers for multiple render targets */
   unsigned nr_cbufs;
   struct pipe_surface *cbufs[PIPE_MAX_COLOR_BUFS];

   struct pipe_surface *zsbuf;      /**< Z/stencil buffer */
};

/**
 * Texture sampler state.
 */
struct pipe_sampler_state
{
   unsigned wrap_s:3;            /**< PIPE_TEX_WRAP_x */
   unsigned wrap_t:3;            /**< PIPE_TEX_WRAP_x */
   unsigned wrap_r:3;            /**< PIPE_TEX_WRAP_x */
   unsigned min_img_filter:2;    /**< PIPE_TEX_FILTER_x */
   unsigned min_mip_filter:2;    /**< PIPE_TEX_MIPFILTER_x */
   unsigned mag_img_filter:2;    /**< PIPE_TEX_FILTER_x */
   unsigned compare_mode:1;      /**< PIPE_TEX_COMPARE_x */
   unsigned compare_func:3;      /**< PIPE_FUNC_x */
   unsigned normalized_coords:1; /**< Are coords normalized to [0,1]? */
   unsigned max_anisotropy:6;
   unsigned seamless_cube_map:1;
   float lod_bias;               /**< LOD/lambda bias */
   float min_lod, max_lod;       /**< LOD clamp range, after bias */
   union pipe_color_union border_color;
};

enum etna_surface_layout
{
    ETNA_LAYOUT_LINEAR = 0,
    ETNA_LAYOUT_TILED = 1,
    ETNA_LAYOUT_SUPERTILED = 3 /* 1|2, both tiling and supertiling bit enabled */
};

struct etna_resource_level
{
   unsigned width, padded_width;
   unsigned height, padded_height;
   unsigned offset; /* offset into memory area */
   unsigned size; /* size of memory area */

   uint32_t address; /* cached GPU pointers to LODs */
   void *logical; /* cached CPU pointer */
   uint32_t ts_address;
   uint32_t ts_size;
   uint32_t stride; /* VIVS_PE_(COLOR|DEPTH)_STRIDE */
   uint32_t layer_stride;
};

/**
 * A memory object/resource such as a vertex buffer or texture.
 */
struct pipe_resource
{
   /* struct pipe_reference reference; */
   struct pipe_screen *screen; /**< screen that this texture belongs to */
   enum pipe_texture_target target; /**< PIPE_TEXTURE_x */
   enum pipe_format format;         /**< PIPE_FORMAT_x */

   unsigned width0;
   unsigned height0;
   unsigned depth0;
   unsigned array_size;

   unsigned last_level:8;    /**< Index of last mipmap level present/defined */
   unsigned nr_samples:8;    /**< for multisampled surfaces, nr of samples */
   unsigned usage:8;         /**< PIPE_USAGE_x (not a bitmask) */

   unsigned bind;            /**< bitmask of PIPE_BIND_x */
   unsigned flags;           /**< bitmask of PIPE_RESOURCE_FLAG_x */

   /* XXX etna specific. This must be moved to subclass on integration. */
   /* only lod 0 used for non-texture buffers */
   enum etna_surface_layout layout;
   etna_vidmem *surface; /* Surface video memory */
   etna_vidmem *ts; /* Tile status video memory */

   struct etna_resource_level levels[ETNA_NUM_LOD];
   /* XXX uint32_t clear_value; */
};

/**
 * A view into a texture that can be bound to a color render target /
 * depth stencil attachment point.
 */
struct pipe_surface
{
   /* struct pipe_reference reference; */
   struct pipe_resource *texture; /**< resource into which this is a view  */
   struct pipe_context *context; /**< context this surface belongs to */
   enum pipe_format format;

   /* XXX width/height should be removed */
   unsigned width;               /**< logical width in pixels */
   unsigned height;              /**< logical height in pixels */

   unsigned writable:1;          /**< writable shader resource */

   union {
      struct {
         unsigned level;
         unsigned first_layer:16;
         unsigned last_layer:16;
      } tex;
      struct {
         unsigned first_element;
         unsigned last_element;
      } buf;
   } u;

   /* XXX etna specific. This must be moved to subclass on integration. */
   enum etna_surface_layout layout;
   struct etna_resource_level surf;
   uint32_t clear_value; // XXX remember depth/stencil clear value from ->clear
   struct compiled_rs_state clear_command;
};


/**
 * A view into a texture that can be bound to a shader stage.
 */
struct pipe_sampler_view
{
   /* struct pipe_reference reference; */
   enum pipe_format format;      /**< typed PIPE_FORMAT_x */
   struct pipe_resource *texture; /**< texture into which this is a view  */
   struct pipe_context *context; /**< context this view belongs to */
   union {
      struct {
         unsigned first_layer:16;     /**< first layer to use for array textures */
         unsigned last_layer:16;      /**< last layer to use for array textures */
         unsigned first_level:8;      /**< first mipmap level to use */
         unsigned last_level:8;       /**< last mipmap level to use */
      } tex;
      struct {
         unsigned first_element;
         unsigned last_element;
      } buf;
   } u;
   unsigned swizzle_r:3;         /**< PIPE_SWIZZLE_x for red component */
   unsigned swizzle_g:3;         /**< PIPE_SWIZZLE_x for green component */
   unsigned swizzle_b:3;         /**< PIPE_SWIZZLE_x for blue component */
   unsigned swizzle_a:3;         /**< PIPE_SWIZZLE_x for alpha component */

   /* etna */
   struct compiled_sampler_view *internal;
};

/**
 * Information to describe a vertex attribute (position, color, etc)
 */
struct pipe_vertex_element
{
   /** Offset of this attribute, in bytes, from the start of the vertex */
   unsigned src_offset;

   /** Instance data rate divisor. 0 means this is per-vertex data,
    *  n means per-instance data used for n consecutive instances (n > 0).
    */
   unsigned instance_divisor;

   /** Which vertex_buffer (as given to pipe->set_vertex_buffer()) does
    * this attribute live in?
    */
   unsigned vertex_buffer_index;
 
   enum pipe_format src_format;
};


/**
 * An index buffer.  When an index buffer is bound, all indices to vertices
 * will be looked up in the buffer.
 */
struct pipe_index_buffer
{
   unsigned index_size;  /**< size of an index, in bytes 1/2/4 */
   unsigned offset;  /**< offset to start of data in buffer, in bytes */
   struct pipe_resource *buffer; /**< the actual buffer */
   const void *user_buffer;  /**< pointer to a user buffer if buffer == NULL */
};

/**
 * A vertex buffer.  Typically, all the vertex data/attributes for
 * drawing something will be in one buffer.  But it's also possible, for
 * example, to put colors in one buffer and texcoords in another.
 */
struct pipe_vertex_buffer
{
   unsigned stride;    /**< stride to same attrib in next vertex, in bytes */
   unsigned buffer_offset;  /**< offset to start of data in buffer, in bytes */
   struct pipe_resource *buffer;  /**< the actual buffer */
   const void *user_buffer;  /**< pointer to a user buffer if buffer == NULL */
};

/**
 * Information to describe a draw_vbo call.
 */
struct pipe_draw_info
{
   bool indexed;  /**< use index buffer */

   unsigned mode;  /**< the mode of the primitive */
   unsigned start;  /**< the index of the first vertex */
   unsigned count;  /**< number of vertices */

   unsigned start_instance; /**< first instance id */
   unsigned instance_count; /**< number of instances */

   /**
    * For indexed drawing, these fields apply after index lookup.
    */
   int index_bias; /**< a bias to be added to each index */
   unsigned min_index; /**< the min index */
   unsigned max_index; /**< the max index */

   /**
    * Primitive restart enable/index (only applies to indexed drawing)
    */
   bool primitive_restart;
   unsigned restart_index;

   /**
    * Stream output target. If not NULL, it's used to provide the 'count'
    * parameter based on the number vertices captured by the stream output
    * stage. (or generally, based on the number of bytes captured)
    *
    * Only 'mode', 'start_instance', and 'instance_count' are taken into
    * account, all the other variables from pipe_draw_info are ignored.
    *
    * 'start' is implicitly 0 and 'count' is set as discussed above.
    * The draw command is non-indexed.
    *
    * Note that this only provides the count. The vertex buffers must
    * be set via set_vertex_buffers manually.
    */
   struct pipe_stream_output_target *count_from_stream_output;
};

/**
 * Subregion of 1D/2D/3D image resource.
 */
struct pipe_box
{
   int x;
   int y;
   int z;
   int width;
   int height;
   int depth;
};

struct pipe_fence_handle;
struct etna_shader_program;
/**
 * Gallium rendering context.  Basically:
 *  - state setting functions
 *  - VBO drawing functions
 *  - surface functions
 */
struct pipe_context {
   struct pipe_screen *screen;

   void *priv;  /**< context private data (for DRI for example) */
   void *draw;  /**< private, for draw module (temporary?) */

   void (*destroy)( struct pipe_context * );

   /**
    * VBO drawing
    */
   /*@{*/
   void (*draw_vbo)( struct pipe_context *pipe,
                     const struct pipe_draw_info *info );
   /*@}*/
#if 0 /* no query support */
   /**
    * Predicate subsequent rendering on occlusion query result
    * \param query  the query predicate, or NULL if no predicate
    * \param mode  one of PIPE_RENDER_COND_x
    */
   void (*render_condition)( struct pipe_context *pipe,
                             struct pipe_query *query,
                             uint mode );
   /**
    * Query objects
    */
   /*@{*/
   struct pipe_query *(*create_query)( struct pipe_context *pipe,
                                       unsigned query_type );

   void (*destroy_query)(struct pipe_context *pipe,
                         struct pipe_query *q);

   void (*begin_query)(struct pipe_context *pipe, struct pipe_query *q);
   void (*end_query)(struct pipe_context *pipe, struct pipe_query *q);

   /**
    * Get results of a query.
    * \param wait  if true, this query will block until the result is ready
    * \return TRUE if results are ready, FALSE otherwise
    */
   boolean (*get_query_result)(struct pipe_context *pipe,
                               struct pipe_query *q,
                               boolean wait,
                               union pipe_query_result *result);
   /*@}*/
#endif

   /**
    * State functions (create/bind/destroy state objects)
    */
   /*@{*/
   void * (*create_blend_state)(struct pipe_context *,
                                const struct pipe_blend_state *);
   void   (*bind_blend_state)(struct pipe_context *, void *);
   void   (*delete_blend_state)(struct pipe_context *, void  *);

   void * (*create_sampler_state)(struct pipe_context *,
                                  const struct pipe_sampler_state *);
   void   (*bind_fragment_sampler_states)(struct pipe_context *,
                                          unsigned num_samplers,
                                          void **samplers);
   void   (*bind_vertex_sampler_states)(struct pipe_context *,
                                        unsigned num_samplers,
                                        void **samplers);
#if 0 /* XXX todo */
   void   (*bind_geometry_sampler_states)(struct pipe_context *,
                                          unsigned num_samplers,
                                          void **samplers);
   void   (*bind_compute_sampler_states)(struct pipe_context *,
                                         unsigned start_slot,
                                         unsigned num_samplers,
                                         void **samplers);
#endif
   void   (*delete_sampler_state)(struct pipe_context *, void *);

   void * (*create_rasterizer_state)(struct pipe_context *,
                                     const struct pipe_rasterizer_state *);
   void   (*bind_rasterizer_state)(struct pipe_context *, void *);
   void   (*delete_rasterizer_state)(struct pipe_context *, void *);

   void * (*create_depth_stencil_alpha_state)(struct pipe_context *,
                                        const struct pipe_depth_stencil_alpha_state *);
   void   (*bind_depth_stencil_alpha_state)(struct pipe_context *, void *);
   void   (*delete_depth_stencil_alpha_state)(struct pipe_context *, void *);
#if 0 /* XXX TODO */
   void * (*create_fs_state)(struct pipe_context *,
                             const struct pipe_shader_state *);
   void   (*bind_fs_state)(struct pipe_context *, void *);
   void   (*delete_fs_state)(struct pipe_context *, void *);

   void * (*create_vs_state)(struct pipe_context *,
                             const struct pipe_shader_state *);
   void   (*bind_vs_state)(struct pipe_context *, void *);
   void   (*delete_vs_state)(struct pipe_context *, void *);

   void * (*create_gs_state)(struct pipe_context *,
                             const struct pipe_shader_state *);
   void   (*bind_gs_state)(struct pipe_context *, void *);
   void   (*delete_gs_state)(struct pipe_context *, void *);
#endif
   void * (*create_vertex_elements_state)(struct pipe_context *,
                                          unsigned num_elements,
                                          const struct pipe_vertex_element *);
   void   (*bind_vertex_elements_state)(struct pipe_context *, void *);
   void   (*delete_vertex_elements_state)(struct pipe_context *, void *);

   /* temp for testing until a real shader compiler */
   void * (*create_etna_shader_state)(struct pipe_context *, const struct etna_shader_program *prog);
   void   (*bind_etna_shader_state)(struct pipe_context *, void *);
   void   (*delete_etna_shader_state)(struct pipe_context *, void*);
   void   (*set_etna_uniforms)(struct pipe_context *pipe, void *sh_, unsigned type, unsigned offset, unsigned count, const uint32_t *values);

   /*@}*/

   /**
    * Parameter-like state (or properties)
    */
   /*@{*/
   void (*set_blend_color)( struct pipe_context *,
                            const struct pipe_blend_color * );

   void (*set_stencil_ref)( struct pipe_context *,
                            const struct pipe_stencil_ref * );

   void (*set_sample_mask)( struct pipe_context *,
                            unsigned sample_mask );
#if 0 /* TODO */
   void (*set_clip_state)( struct pipe_context *,
                            const struct pipe_clip_state * );

   void (*set_constant_buffer)( struct pipe_context *,
                                uint shader, uint index,
                                struct pipe_constant_buffer *buf );

#endif
   void (*set_framebuffer_state)( struct pipe_context *,
                                  const struct pipe_framebuffer_state * );
#if 0
   void (*set_polygon_stipple)( struct pipe_context *,
				const struct pipe_poly_stipple * );
#endif
   void (*set_scissor_state)( struct pipe_context *,
                              const struct pipe_scissor_state * );

   void (*set_viewport_state)( struct pipe_context *,
                               const struct pipe_viewport_state * );

   void (*set_fragment_sampler_views)(struct pipe_context *,
                                      unsigned num_views,
                                      struct pipe_sampler_view **);
   void (*set_vertex_sampler_views)(struct pipe_context *,
                                    unsigned num_views,
                                    struct pipe_sampler_view **);
#if 0 /* XXX todo */

   void (*set_geometry_sampler_views)(struct pipe_context *,
                                      unsigned num_views,
                                      struct pipe_sampler_view **);

   void (*set_compute_sampler_views)(struct pipe_context *,
                                     unsigned start_slot, unsigned num_views,
                                     struct pipe_sampler_view **);
   
   /**
    * Bind an array of shader resources that will be used by the
    * graphics pipeline.  Any resources that were previously bound to
    * the specified range will be unbound after this call.
    *
    * \param start      first resource to bind.
    * \param count      number of consecutive resources to bind.
    * \param resources  array of pointers to the resources to bind, it
    *                   should contain at least \a count elements
    *                   unless it's NULL, in which case no new
    *                   resources will be bound.
    */
   void (*set_shader_resources)(struct pipe_context *,
                                unsigned start, unsigned count,
                                struct pipe_surface **resources);
#endif
   void (*set_vertex_buffers)( struct pipe_context *,
                               unsigned start_slot,
                               unsigned num_buffers,
                               const struct pipe_vertex_buffer * );

   void (*set_index_buffer)( struct pipe_context *pipe,
                             const struct pipe_index_buffer * );

   /*@}*/
#if 0 /* unsupported? */
   /**
    * Stream output functions.
    */
   /*@{*/

   struct pipe_stream_output_target *(*create_stream_output_target)(
                        struct pipe_context *,
                        struct pipe_resource *,
                        unsigned buffer_offset,
                        unsigned buffer_size);

   void (*stream_output_target_destroy)(struct pipe_context *,
                                        struct pipe_stream_output_target *);

   void (*set_stream_output_targets)(struct pipe_context *,
                              unsigned num_targets,
                              struct pipe_stream_output_target **targets,
                              unsigned append_bitmask);

   /*@}*/
#endif

   /**
    * Resource functions for blit-like functionality
    *
    * If a driver supports multisampling, blit must implement color resolve.
    */
   /*@{*/
#if 0 /* XXX todo */

   /**
    * Copy a block of pixels from one resource to another.
    * The resource must be of the same format.
    * Resources with nr_samples > 1 are not allowed.
    */
   void (*resource_copy_region)(struct pipe_context *pipe,
                                struct pipe_resource *dst,
                                unsigned dst_level,
                                unsigned dstx, unsigned dsty, unsigned dstz,
                                struct pipe_resource *src,
                                unsigned src_level,
                                const struct pipe_box *src_box);
   /* Optimal hardware path for blitting pixels.
    * Scaling, format conversion, up- and downsampling (resolve) are allowed.
    */
   void (*blit)(struct pipe_context *pipe,
                const struct pipe_blit_info *info);
#endif
   /*@}*/

   /**
    * Clear the specified set of currently bound buffers to specified values.
    * The entire buffers are cleared (no scissor, no colormask, etc).
    *
    * \param buffers  bitfield of PIPE_CLEAR_* values.
    * \param color  pointer to a union of fiu array for each of r, g, b, a.
    * \param depth  depth clear value in [0,1].
    * \param stencil  stencil clear value
    */
   void (*clear)(struct pipe_context *pipe,
                 unsigned buffers,
                 const union pipe_color_union *color,
                 double depth,
                 unsigned stencil);

   /**
    * Clear a color rendertarget surface.
    * \param color  pointer to an union of fiu array for each of r, g, b, a.
    */
   void (*clear_render_target)(struct pipe_context *pipe,
                               struct pipe_surface *dst,
                               const union pipe_color_union *color,
                               unsigned dstx, unsigned dsty,
                               unsigned width, unsigned height);

   /**
    * Clear a depth-stencil surface.
    * \param clear_flags  bitfield of PIPE_CLEAR_DEPTH/STENCIL values.
    * \param depth  depth clear value in [0,1].
    * \param stencil  stencil clear value
    */
   void (*clear_depth_stencil)(struct pipe_context *pipe,
                               struct pipe_surface *dst,
                               unsigned clear_flags,
                               double depth,
                               unsigned stencil,
                               unsigned dstx, unsigned dsty,
                               unsigned width, unsigned height);

   /** Flush draw commands
    */
   void (*flush)(struct pipe_context *pipe,
                 struct pipe_fence_handle **fence,
                 enum pipe_flush_flags flags);

   /**
    * Create a view on a texture to be used by a shader stage.
    */
   struct pipe_sampler_view * (*create_sampler_view)(struct pipe_context *ctx,
                                                     struct pipe_resource *texture,
                                                     const struct pipe_sampler_view *templat);

   void (*sampler_view_destroy)(struct pipe_context *ctx,
                                struct pipe_sampler_view *view);


   /**
    * Get a surface which is a "view" into a resource, used by
    * render target / depth stencil stages.
    */
   struct pipe_surface *(*create_surface)(struct pipe_context *ctx,
                                          struct pipe_resource *resource,
                                          const struct pipe_surface *templat);

   void (*surface_destroy)(struct pipe_context *ctx,
                           struct pipe_surface *);
#if 0 /* XXX todo */
   /**
    * Map a resource.
    *
    * Transfers are (by default) context-private and allow uploads to be
    * interleaved with rendering.
    *
    * out_transfer will contain the transfer object that must be passed
    * to all the other transfer functions. It also contains useful
    * information (like texture strides).
    */
   void *(*transfer_map)(struct pipe_context *,
                         struct pipe_resource *resource,
                         unsigned level,
                         unsigned usage,  /* a combination of PIPE_TRANSFER_x */
                         const struct pipe_box *,
                         struct pipe_transfer **out_transfer);

   /* If transfer was created with WRITE|FLUSH_EXPLICIT, only the
    * regions specified with this call are guaranteed to be written to
    * the resource.
    */
   void (*transfer_flush_region)( struct pipe_context *,
				  struct pipe_transfer *transfer,
				  const struct pipe_box *);

   void (*transfer_unmap)(struct pipe_context *,
                          struct pipe_transfer *transfer);

   /* One-shot transfer operation with data supplied in a user
    * pointer.  XXX: strides??
    */
   void (*transfer_inline_write)( struct pipe_context *,
                                  struct pipe_resource *,
                                  unsigned level,
                                  unsigned usage, /* a combination of PIPE_TRANSFER_x */
                                  const struct pipe_box *,
                                  const void *data,
                                  unsigned stride,
                                  unsigned layer_stride);
#endif
   /**
    * Flush any pending framebuffer writes and invalidate texture caches.
    */
   void (*texture_barrier)(struct pipe_context *);
#if 0  /* XXX todo */
   /**
    * Creates a video decoder for a specific video codec/profile
    */
   struct pipe_video_decoder *(*create_video_decoder)( struct pipe_context *context,
                                                       enum pipe_video_profile profile,
                                                       enum pipe_video_entrypoint entrypoint,
                                                       enum pipe_video_chroma_format chroma_format,
                                                       unsigned width, unsigned height, unsigned max_references,
                                                       bool expect_chunked_decode);

   /**
    * Creates a video buffer as decoding target
    */
   struct pipe_video_buffer *(*create_video_buffer)( struct pipe_context *context,
                                                     const struct pipe_video_buffer *templat );
#endif
   /**
    * Compute kernel execution
    */
   /*@{*/
#if 0 /* XXX todo */
   /**
    * Define the compute program and parameters to be used by
    * pipe_context::launch_grid.
    */
   void *(*create_compute_state)(struct pipe_context *context,
				 const struct pipe_compute_state *);
   void (*bind_compute_state)(struct pipe_context *, void *);
   void (*delete_compute_state)(struct pipe_context *, void *);
  
   /**
    * Bind an array of shader resources that will be used by the
    * compute program.  Any resources that were previously bound to
    * the specified range will be unbound after this call.
    *
    * \param start      first resource to bind.
    * \param count      number of consecutive resources to bind.
    * \param resources  array of pointers to the resources to bind, it
    *                   should contain at least \a count elements
    *                   unless it's NULL, in which case no new
    *                   resources will be bound.
    */
   void (*set_compute_resources)(struct pipe_context *,
                                 unsigned start, unsigned count,
                                 struct pipe_surface **resources);
   
   /**
    * Bind an array of buffers to be mapped into the address space of
    * the GLOBAL resource.  Any buffers that were previously bound
    * between [first, first + count - 1] are unbound after this call.
    *
    * \param first      first buffer to map.
    * \param count      number of consecutive buffers to map.
    * \param resources  array of pointers to the buffers to map, it
    *                   should contain at least \a count elements
    *                   unless it's NULL, in which case no new
    *                   resources will be bound.
    * \param handles    array of pointers to the memory locations that
    *                   will be filled with the respective base
    *                   addresses each buffer will be mapped to.  It
    *                   should contain at least \a count elements,
    *                   unless \a resources is NULL in which case \a
    *                   handles should be NULL as well.
    *
    * Note that the driver isn't required to make any guarantees about
    * the contents of the \a handles array being valid anytime except
    * during the subsequent calls to pipe_context::launch_grid.  This
    * means that the only sensible location handles[i] may point to is
    * somewhere within the INPUT buffer itself.  This is so to
    * accommodate implementations that lack virtual memory but
    * nevertheless migrate buffers on the fly, leading to resource
    * base addresses that change on each kernel invocation or are
    * unknown to the pipe driver.
    */
   void (*set_global_binding)(struct pipe_context *context,
                              unsigned first, unsigned count,
                              struct pipe_resource **resources,
                              uint32_t **handles);

   /**
    * Launch the compute kernel starting from instruction \a pc of the
    * currently bound compute program.
    *
    * \a grid_layout and \a block_layout are arrays of size \a
    * PIPE_COMPUTE_CAP_GRID_DIMENSION that determine the layout of the
    * grid (in block units) and working block (in thread units) to be
    * used, respectively.
    *
    * \a pc For drivers that use PIPE_SHADER_IR_LLVM as their prefered IR,
    * this value will be the index of the kernel in the opencl.kernels
    * metadata list.
    *
    * \a input will be used to initialize the INPUT resource, and it
    * should point to a buffer of at least
    * pipe_compute_state::req_input_mem bytes.
    */
   void (*launch_grid)(struct pipe_context *context,
                       const uint *block_layout, const uint *grid_layout,
                       uint32_t pc, const void *input);
   /*@}*/
#endif
};

#endif

