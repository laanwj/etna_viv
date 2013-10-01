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
/* Rasterizer CSOs */
#include "etna_rasterizer.h"

#include "etna_internal.h"
#include "etna_pipe.h"
#include "etna_translate.h"
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "util/u_memory.h"

#include <etnaviv/common.xml.h>
#include <etnaviv/state.xml.h>
#include <etnaviv/state_3d.xml.h>

static void *etna_pipe_create_rasterizer_state(struct pipe_context *pipe,
                                 const struct pipe_rasterizer_state *rs)
{
    //struct etna_pipe_context *priv = etna_pipe_context(pipe);
    struct compiled_rasterizer_state *cs = CALLOC_STRUCT(compiled_rasterizer_state);
    if(rs->fill_front != rs->fill_back)
    {
        DBG("Different front and back fill mode not supported");
    }
    cs->PA_CONFIG =
            (rs->flatshade ? VIVS_PA_CONFIG_SHADE_MODEL_FLAT : VIVS_PA_CONFIG_SHADE_MODEL_SMOOTH) |
            translate_cull_face(rs->cull_face, rs->front_ccw) |
            translate_polygon_mode(rs->fill_front) |
            (rs->point_quad_rasterization ? VIVS_PA_CONFIG_POINT_SPRITE_ENABLE : 0) |
            (rs->point_size_per_vertex ? VIVS_PA_CONFIG_POINT_SIZE_ENABLE : 0);
    cs->PA_LINE_WIDTH = etna_f32_to_u32(rs->line_width / 2.0f);
    cs->PA_POINT_SIZE = etna_f32_to_u32(rs->point_size);
    cs->SE_DEPTH_SCALE = etna_f32_to_u32(rs->offset_scale);
    cs->SE_DEPTH_BIAS = etna_f32_to_u32(rs->offset_units) / 65535.0f;
    cs->SE_CONFIG =
            (rs->line_last_pixel ? VIVS_SE_CONFIG_LAST_PIXEL_ENABLE : 0);
            /* XXX anything else? */
    /* XXX bottom_edge_rule */
    cs->PA_SYSTEM_MODE =
            (rs->half_pixel_center ? (VIVS_PA_SYSTEM_MODE_UNK0 | VIVS_PA_SYSTEM_MODE_UNK4) : 0);
    /* rs->scissor overrides the scissor, defaulting to the whole framebuffer, with the scissor state */
    cs->scissor = rs->scissor;
    /* point size per vertex adds a vertex shader output */
    cs->point_size_per_vertex = rs->point_size_per_vertex;

    assert(!rs->clip_halfz); /* could be supported with shader magic, actually D3D z is default on older gc */
    return cs;
}

static void etna_pipe_bind_rasterizer_state(struct pipe_context *pipe, void *rs)
{
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
    priv->dirty_bits |= ETNA_STATE_RASTERIZER;
    priv->rasterizer_p = rs;
    if(rs)
        priv->rasterizer = *(struct compiled_rasterizer_state*)rs;
}

static void etna_pipe_delete_rasterizer_state(struct pipe_context *pipe, void *rs)
{
    //struct etna_pipe_context *priv = etna_pipe_context(pipe);
    FREE(rs);
}

void etna_pipe_rasterizer_init(struct pipe_context *pc)
{
    pc->create_rasterizer_state = etna_pipe_create_rasterizer_state;
    pc->bind_rasterizer_state = etna_pipe_bind_rasterizer_state;
    pc->delete_rasterizer_state = etna_pipe_delete_rasterizer_state;
}

