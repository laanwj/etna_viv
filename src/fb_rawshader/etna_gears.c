/*
 * Copyright (C) 1999-2001  Brian Paul   All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Ported to GLES2.
 * Kristian Høgsberg <krh@bitplanet.net>
 * May 3, 2010
 * 
 * Improve GLES2 port:
 *   * Refactor gear drawing.
 *   * Use correct normals for surfaces.
 *   * Improve shader.
 *   * Use perspective projection transformation.
 *   * Add FPS count.
 *   * Add comments.
 * Alexandros Frantzis <alexandros.frantzis@linaro.org>
 * Jul 13, 2010
 */
/* Ported to Etna by Wladimir van der Laan */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

#include "etna_pipe.h"
#include "etna_rawshader.h"
#include "util/u_inlines.h"

#include "write_bmp.h"
#include "fbdemos.h"

#include "esTransform.h"
#include "esShapes.h"
#include "esUtil.h"

#ifndef M_PI
# define M_PI		3.14159265358979323846	/* pi */
#endif

#define STRIPS_PER_TOOTH 7
#define VERTICES_PER_TOOTH 34
#define GEAR_VERTEX_STRIDE 6

/**
 * Struct describing the vertices in triangle strip
 */
struct vertex_strip {
   /** The first vertex in the strip */
   int first;
   /** The number of consecutive vertices in the strip after the first */
   int count;
};

/* Each vertex consist of GEAR_VERTEX_STRIDE float attributes */
typedef float GearVertex[GEAR_VERTEX_STRIDE];

/**
 * Struct representing a gear.
 */
struct gear {
   /** The array of vertices comprising the gear */
   GearVertex *vertices;
   /** The number of vertices comprising the gear */
   int nvertices;
   /** The array of triangle strips comprising the gear */
   struct vertex_strip *strips;
   /** The number of triangle strips comprising the gear */
   int nstrips;
   /** The Vertex Buffer Object holding the vertices in the graphics card */
   struct pipe_resource *vtx_resource;
   void *vertex_elements;
   struct pipe_vertex_buffer vertex_buffer;
};

/** The view rotation [x, y, z] */
static float view_rot[3] = { 20.0, 30.0, 0.0 };
/** The gears */
static struct gear *gear1, *gear2, *gear3;
/** The current gear rotation angle */
static float angle = 0.0;
/** The projection matrix */
static ESMatrix ProjectionMatrix;
/** The direction of the directional light for the scene */
static const float LightSourcePosition[4] = { 5.0, 5.0, 10.0, 1.0};

/** 
 * Fills a gear vertex.
 * 
 * @param v the vertex to fill
 * @param x the x coordinate
 * @param y the y coordinate
 * @param z the z coortinate
 * @param n pointer to the normal table 
 * 
 * @return the operation error code
 */
static GearVertex *
vert(GearVertex *v, float x, float y, float z, float n[3])
{
   v[0][0] = x;
   v[0][1] = y;
   v[0][2] = z;
   v[0][3] = n[0];
   v[0][4] = n[1];
   v[0][5] = n[2];

   return v + 1;
}

static void sincos_(double x, double *s, double *c)
{
    *s = sin(x);
    *c = cos(x);
}

/**
 *  Create a gear wheel.
 * 
 *  @param inner_radius radius of hole at center
 *  @param outer_radius radius at center of teeth
 *  @param width width of gear
 *  @param teeth number of teeth
 *  @param tooth_depth depth of tooth
 *  
 *  @return pointer to the constructed struct gear
 */
static struct gear *
create_gear(struct pipe_screen *screen, struct pipe_context *pipe, float inner_radius, float outer_radius, float width,
      int teeth, float tooth_depth)
{
   float r0, r1, r2;
   float da;
   GearVertex *v;
   struct gear *gear;
   double s[5], c[5];
   float normal[3];
   int cur_strip = 0;
   int i;

   /* Allocate memory for the gear */
   gear = malloc(sizeof *gear);
   if (gear == NULL)
      return NULL;

   /* Calculate the radii used in the gear */
   r0 = inner_radius;
   r1 = outer_radius - tooth_depth / 2.0;
   r2 = outer_radius + tooth_depth / 2.0;

   da = 2.0 * M_PI / teeth / 4.0;

   /* Allocate memory for the triangle strip information */
   gear->nstrips = STRIPS_PER_TOOTH * teeth;
   gear->strips = calloc(gear->nstrips, sizeof (*gear->strips));

   /* Allocate memory for the vertices */
   gear->vertices = calloc(VERTICES_PER_TOOTH * teeth, sizeof(*gear->vertices));
   v = gear->vertices;

   for (i = 0; i < teeth; i++) {
      /* Calculate needed sin/cos for varius angles */
      sincos_(i * 2.0 * M_PI / teeth, &s[0], &c[0]);
      sincos_(i * 2.0 * M_PI / teeth + da, &s[1], &c[1]);
      sincos_(i * 2.0 * M_PI / teeth + da * 2, &s[2], &c[2]);
      sincos_(i * 2.0 * M_PI / teeth + da * 3, &s[3], &c[3]);
      sincos_(i * 2.0 * M_PI / teeth + da * 4, &s[4], &c[4]);

      /* A set of macros for making the creation of the gears easier */
#define  GEAR_POINT(r, da) { (r) * c[(da)], (r) * s[(da)] }
#define  SET_NORMAL(x, y, z) do { \
   normal[0] = (x); normal[1] = (y); normal[2] = (z); \
} while(0)

#define  GEAR_VERT(v, point, sign) vert((v), p[(point)].x, p[(point)].y, (sign) * width * 0.5, normal)

#define START_STRIP do { \
   gear->strips[cur_strip].first = v - gear->vertices; \
} while(0);

#define END_STRIP do { \
   int _tmp = (v - gear->vertices); \
   gear->strips[cur_strip].count = _tmp - gear->strips[cur_strip].first; \
   cur_strip++; \
} while (0)

#define QUAD_WITH_NORMAL(p1, p2) do { \
   SET_NORMAL((p[(p1)].y - p[(p2)].y), -(p[(p1)].x - p[(p2)].x), 0); \
   v = GEAR_VERT(v, (p1), -1); \
   v = GEAR_VERT(v, (p1), 1); \
   v = GEAR_VERT(v, (p2), -1); \
   v = GEAR_VERT(v, (p2), 1); \
} while(0)

      struct point {
         float x;
         float y;
      };

      /* Create the 7 points (only x,y coords) used to draw a tooth */
      struct point p[7] = {
         GEAR_POINT(r2, 1), // 0
         GEAR_POINT(r2, 2), // 1
         GEAR_POINT(r1, 0), // 2
         GEAR_POINT(r1, 3), // 3
         GEAR_POINT(r0, 0), // 4
         GEAR_POINT(r1, 4), // 5
         GEAR_POINT(r0, 4), // 6
      };

      /* Front face */
      START_STRIP;
      SET_NORMAL(0, 0, 1.0);
      v = GEAR_VERT(v, 0, +1);
      v = GEAR_VERT(v, 1, +1);
      v = GEAR_VERT(v, 2, +1);
      v = GEAR_VERT(v, 3, +1);
      v = GEAR_VERT(v, 4, +1);
      v = GEAR_VERT(v, 5, +1);
      v = GEAR_VERT(v, 6, +1);
      END_STRIP;

      /* Inner face */
      START_STRIP;
      QUAD_WITH_NORMAL(4, 6);
      END_STRIP;

      /* Back face */
      START_STRIP;
      SET_NORMAL(0, 0, -1.0);
      v = GEAR_VERT(v, 6, -1);
      v = GEAR_VERT(v, 5, -1);
      v = GEAR_VERT(v, 4, -1);
      v = GEAR_VERT(v, 3, -1);
      v = GEAR_VERT(v, 2, -1);
      v = GEAR_VERT(v, 1, -1);
      v = GEAR_VERT(v, 0, -1);
      END_STRIP;

      /* Outer face */
      START_STRIP;
      QUAD_WITH_NORMAL(0, 2);
      END_STRIP;

      START_STRIP;
      QUAD_WITH_NORMAL(1, 0);
      END_STRIP;

      START_STRIP;
      QUAD_WITH_NORMAL(3, 1);
      END_STRIP;

      START_STRIP;
      QUAD_WITH_NORMAL(5, 3);
      END_STRIP;
   }

   gear->nvertices = (v - gear->vertices);

    /* element layout */
    struct pipe_vertex_element pipe_vertex_elements[] = {
        { /* positions */
            .src_offset = 0x0,
            .instance_divisor = 0,
            .vertex_buffer_index = 0,
            .src_format = PIPE_FORMAT_R32G32B32_FLOAT 
        },
        { /* normals */
            .src_offset = 0xc,
            .instance_divisor = 0,
            .vertex_buffer_index = 0,
            .src_format = PIPE_FORMAT_R32G32B32_FLOAT 
        }
    };
    gear->vertex_elements = pipe->create_vertex_elements_state(pipe, 
            sizeof(pipe_vertex_elements)/sizeof(pipe_vertex_elements[0]), pipe_vertex_elements);

    /* Store the vertices in a vertex buffer object (VBO) */
    gear->vtx_resource = pipe_buffer_create(screen, PIPE_BIND_VERTEX_BUFFER, PIPE_USAGE_IMMUTABLE, gear->nvertices * sizeof(GearVertex));
    etna_pipe_inline_write(pipe, gear->vtx_resource, 0, 0, gear->vertices, gear->nvertices * sizeof(GearVertex));

    gear->vertex_buffer.stride = sizeof(GearVertex);
    gear->vertex_buffer.buffer_offset = 0;
    gear->vertex_buffer.buffer = gear->vtx_resource;
    gear->vertex_buffer.user_buffer = 0;

   return gear;
}

/**
 * Draws a gear.
 *
 * @param gear the gear to draw
 * @param transform the current transformation matrix
 * @param x the x position to draw the gear at
 * @param y the y position to draw the gear at
 * @param angle the rotation angle of the gear
 * @param color the color of the gear
 */
static void
draw_gear(struct pipe_context *pipe, struct gear *gear, void *shader_state, ESMatrix *transform,
      float x, float y, float angle, const float color[4])
{
    ESMatrix model_view;
    ESMatrix normal_matrix;
    ESMatrix model_view_projection;

    /* Translate and rotate the gear */
    model_view = *transform;
    esTranslate(&model_view, x, y, 0);
    esRotate(&model_view, -angle, 0, 0, 1);
    
    /* Create and set the ModelViewProjectionMatrix */
    esMatrixMultiply(&model_view_projection, &model_view, &ProjectionMatrix);

    etna_set_uniforms(pipe, PIPE_SHADER_VERTEX, 6*4, 16, (uint32_t*)&model_view_projection.m[0][0]);

    /* 
     * Create and set the NormalMatrix. It's the inverse transpose of the
     * ModelView matrix.
     */
    ESMatrix inverse_model_view;
    esMatrixInverse3x3(&inverse_model_view, &model_view);
    esMatrixTranspose(&normal_matrix, &inverse_model_view);
    etna_set_uniforms(pipe, PIPE_SHADER_VERTEX, 0*4, 16, (uint32_t*)&normal_matrix.m[0][0]);

    /* Set the gear color */
    etna_set_uniforms(pipe, PIPE_SHADER_VERTEX, 5*4, 4, (uint32_t*)color);

    /* Set up the position of the attributes in the vertex buffer object */
    pipe->bind_vertex_elements_state(pipe, gear->vertex_elements);
    pipe->set_vertex_buffers(pipe, 0, 1, &gear->vertex_buffer);
    pipe->set_index_buffer(pipe, NULL);

    /* Draw the triangle strips that comprise the gear */
    int n;
    for (n = 0; n < gear->nstrips; n++)
    {
        pipe->draw_vbo(pipe, &(struct pipe_draw_info){
                .indexed = 0,
                .mode = PIPE_PRIM_TRIANGLE_STRIP,
                .start = gear->strips[n].first,
                .count = gear->strips[n].count
                });
    }
}

/** 
 * Draws the gears.
 */
static void
gears_draw(struct pipe_context *pipe, void *shader_state)
{
    const static float red[4] = { 0.8, 0.1, 0.0, 1.0 };
    const static float green[4] = { 0.0, 0.8, 0.2, 1.0 };
    const static float blue[4] = { 0.2, 0.2, 1.0, 1.0 };
    ESMatrix transform;
    esMatrixLoadIdentity(&transform);

    pipe->clear(pipe, PIPE_CLEAR_COLOR | PIPE_CLEAR_DEPTHSTENCIL, &(const union pipe_color_union) {
            .f = {0.2, 0.2, 0.2, 1.0}
            }, 1.0, 0xff);

    /* Translate and rotate the view */
    esTranslate(&transform, 0, 0, -15);
    esRotate(&transform, -view_rot[0], 1, 0, 0);
    esRotate(&transform, -view_rot[1], 0, 1, 0);
    esRotate(&transform, -view_rot[2], 0, 0, 1);

    /* Draw the gears */
    draw_gear(pipe, gear1, shader_state, &transform, -3.0, -2.0, angle, red);
    draw_gear(pipe, gear2, shader_state, &transform, 3.1, -2.0, -2 * angle - 9.0, green);
    draw_gear(pipe, gear3, shader_state, &transform, -3.1, 4.2, -2 * angle - 25.0, blue);
}

/** 
 * Handles a new window size or exposure.
 * 
 * @param width the window width
 * @param height the window height
 */
static void
gears_reshape(struct pipe_context *pipe, int width, int height)
{
    /* Update the projection matrix */
    esMatrixLoadIdentity(&ProjectionMatrix);
    esPerspective(&ProjectionMatrix, 60.0, width / (float)height, 1.0, 1024.0);
    
    /* Set the viewport */
    pipe->set_viewport_states(pipe, 0, 1, &(struct pipe_viewport_state){
            .scale = {width/2.0f, height/2.0f, 0.5f, 1.0f},
            .translate = {width/2.0f, height/2.0f, 0.5f, 1.0f}
            });
}

static void
gears_idle(struct etna_bswap_buffers *buffers)
{
    static int frames = 0;
    static double tRot0 = -1.0, tRate0 = -1.0;
    double dt, t = esNow();

    if (tRot0 < 0.0)
        tRot0 = t;
    dt = t - tRot0;
    tRot0 = t;

    /* advance rotation for next frame */
    angle += 70.0 * dt;  /* 70 degrees per second */
    if (angle > 3600.0)
        angle -= 3600.0;

    etna_swap_buffers(buffers);
    frames++;

    if (tRate0 < 0.0)
        tRate0 = t;
    if (t - tRate0 >= 5.0) {
        float seconds = t - tRate0;
        float fps = frames / seconds;
        printf("%d frames in %3.1f seconds = %6.3f FPS\n", frames, seconds,
              fps);
        tRate0 = t;
        frames = 0;
    }
}

/* etna_gears_vs.asm */
uint32_t vs[] = {
0x07821003,0x39000800,0x00000050,0x00000000,
0x07821002,0x39001800,0x00aa0050,0x00390028,
0x07801002,0x39002800,0x01540050,0x00390028,
0x07801001,0x39000800,0x00000000,0x20390038,
0x03821005,0x29000800,0x01480040,0x00000000,
0x0382100d,0x00000000,0x00000000,0x00000028,
0x03801003,0x29000800,0x01480140,0x00000000,
0x03821005,0x29004800,0x01480250,0x00000002,
0x0382100d,0x00000000,0x00000000,0x00000028,
0x03821003,0x29004800,0x01480150,0x00000000,
0x00801005,0x29000800,0x01480140,0x00000000,
0x0080108f,0x00000800,0x00000540,0x0000000a,
0x07801003,0x00000800,0x01c802c0,0x00000002,
0x07821003,0x39006800,0x000000d0,0x00000000,
0x07821002,0x39007800,0x00aa00d0,0x00390028,
0x07811002,0x39008800,0x015400d0,0x00390028,
0x07811001,0x39001800,0x00000000,0x20390098,
0x02011001,0x2a801800,0x00000000,0x003fc018,
0x02011003,0x2a801800,0x00aa0540,0x00000002,
};
/* etna_gears_ps.asm */
uint32_t ps[] = {
0x00000000,0x00000000,0x00000000,0x00000000,
};

const struct etna_shader_program shader = {
    .num_inputs = 2,
    .inputs = {{.vs_reg=1},{.vs_reg=0}},
    .num_varyings = 1,
    .varyings = {
        {.num_components=4, .special=ETNA_VARYING_VSOUT, .pa_attributes=0x200, .vs_reg=0},
    }, 
    .vs_code_size = sizeof(vs)/4,
    .vs_code = (uint32_t*)vs,
    .vs_pos_out_reg = 1, 
    .vs_load_balancing = 0xf3f0582,
    .vs_num_temps = 3,
    .vs_uniforms_size = 12*4,
    .vs_uniforms = (uint32_t*)(const float[12*4]){
        [40] = 0.0f, /* u10.x */
        [41] = 0.5f, /* u10.y */
    },
    .ps_code_size = sizeof(ps)/4,
    .ps_code = (uint32_t*)ps,
    .ps_color_out_reg = 1, // t1 in/out passthrough
    .ps_num_temps = 2,
    .ps_uniforms_size = 0,
    .ps_uniforms = NULL
};

int
main(int argc, char *argv[])
{
    struct fbdemos_scaffold *fbs = 0;
    fbdemo_init(&fbs);
    int width = fbs->width;
    int height = fbs->height;
    struct pipe_context *pipe = fbs->pipe;

    /* resources */
    struct pipe_resource *rt_resource = fbdemo_create_2d(fbs->screen, PIPE_BIND_RENDER_TARGET, PIPE_FORMAT_B8G8R8X8_UNORM, width, height, 0);
    struct pipe_resource *z_resource = fbdemo_create_2d(fbs->screen, PIPE_BIND_RENDER_TARGET, PIPE_FORMAT_Z16_UNORM, width, height, 0);
    
    struct pipe_surface *cbuf = pipe->create_surface(pipe, rt_resource, &(struct pipe_surface){
        .texture = rt_resource,
        .format = rt_resource->format,
        .u.tex.level = 0
        });
    struct pipe_surface *zsbuf = pipe->create_surface(pipe, z_resource, &(struct pipe_surface){
        .texture = z_resource,
        .format = z_resource->format,
        .u.tex.level = 0
        });
    
    /* bind render target to framebuffer */
    etna_fb_bind_resource(fbs, rt_resource);

    /* compile gallium3d states */
    void *blend = pipe->create_blend_state(pipe, &(struct pipe_blend_state) {
                .rt[0] = {
                    .blend_enable = 0,
                    .rgb_func = PIPE_BLEND_ADD,
                    .rgb_src_factor = PIPE_BLENDFACTOR_ONE,
                    .rgb_dst_factor = PIPE_BLENDFACTOR_ZERO,
                    .alpha_func = PIPE_BLEND_ADD,
                    .alpha_src_factor = PIPE_BLENDFACTOR_ONE,
                    .alpha_dst_factor = PIPE_BLENDFACTOR_ZERO,
                    .colormask = 0xf
                }
            });

    void *rasterizer = pipe->create_rasterizer_state(pipe, &(struct pipe_rasterizer_state){
                .flatshade = 0,
                .light_twoside = 1,
                .clamp_vertex_color = 1,
                .clamp_fragment_color = 1,
                .front_ccw = 0,
                .cull_face = PIPE_FACE_BACK,      /**< PIPE_FACE_x */
                .fill_front = PIPE_POLYGON_MODE_FILL,     /**< PIPE_POLYGON_MODE_x */
                .fill_back = PIPE_POLYGON_MODE_FILL,      /**< PIPE_POLYGON_MODE_x */
                .offset_point = 0,
                .offset_line = 0,
                .offset_tri = 0,
                .scissor = 0,
                .poly_smooth = 1,
                .poly_stipple_enable = 0,
                .point_smooth = 0,
                .sprite_coord_mode = 0,     /**< PIPE_SPRITE_COORD_ */
                .point_quad_rasterization = 0, /** points rasterized as quads or points */
                .point_size_per_vertex = 0, /**< size computed in vertex shader */
                .multisample = 0,
                .line_smooth = 0,
                .line_stipple_enable = 0,
                .line_last_pixel = 0,
                .flatshade_first = 0,
                .half_pixel_center = 1,
                .rasterizer_discard = 0,
                .depth_clip = 0,
                .clip_plane_enable = 0,
                .line_stipple_factor = 0,
                .line_stipple_pattern = 0,
                .sprite_coord_enable = 0,
                .line_width = 1.0f,
                .point_size = 1.0f,
                .offset_units = 0.0f,
                .offset_scale = 0.0f,
                .offset_clamp = 0.0f
            });

    void *dsa = pipe->create_depth_stencil_alpha_state(pipe, &(struct pipe_depth_stencil_alpha_state){
            .depth = {
                .enabled = 1,
                .writemask = 1,
                .func = PIPE_FUNC_LESS /* GL default */
            },
            .stencil[0] = {
                .enabled = 0
            },
            .stencil[1] = {
                .enabled = 0
            },
            .alpha = {
                .enabled = 0
            }
            });

    pipe->bind_blend_state(pipe, blend);
    pipe->bind_rasterizer_state(pipe, rasterizer);
    pipe->bind_depth_stencil_alpha_state(pipe, dsa);

    pipe->set_blend_color(pipe, &(struct pipe_blend_color){
            .color = {0.0f,0.0f,0.0f,1.0f}
            });
    pipe->set_stencil_ref(pipe, &(struct pipe_stencil_ref){
            .ref_value[0] = 0xff,
            .ref_value[1] = 0xff
            });
    pipe->set_sample_mask(pipe, 0xf);
    pipe->set_framebuffer_state(pipe, &(struct pipe_framebuffer_state){
            .width = width,
            .height = height,
            .nr_cbufs = 1,
            .cbufs[0] = cbuf,
            .zsbuf = zsbuf
            });
    pipe->set_scissor_states(pipe, 0, 1, &(struct pipe_scissor_state){
            .minx = 0,
            .miny = 0,
            .maxx = 65535,
            .maxy = 65535
            });
    
    void *shader_state = etna_create_shader_state(pipe, &shader);
    etna_bind_shader_state(pipe, shader_state);
    
    /* Set the LightSourcePosition uniform which is constant throught the program */
    etna_set_uniforms(pipe, PIPE_SHADER_VERTEX, 4*4, 4, (uint32_t*)LightSourcePosition);

    /* make the gears */
    gear1 = create_gear(fbs->screen, pipe, 1.0, 4.0, 1.0, 20, 0.7);
    gear2 = create_gear(fbs->screen, pipe, 0.5, 2.0, 2.0, 10, 0.7);
    gear3 = create_gear(fbs->screen, pipe, 1.3, 2.0, 0.5, 10, 0.7);

    gears_reshape(pipe, width, height);

    int frame = 0;
    while(true)
    {
        if(frame%50 == 0)
            printf("*** FRAME %i ****\n", frame);
        gears_draw(pipe, shader_state);
#if 0
        etna_dump_cmd_buffer(ctx);
        exit(0);
#endif    
        gears_idle(fbs->buffers);
        frame++;
    }
    fbdemo_free(fbs);

    return 0;
}

