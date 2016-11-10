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
/* Blending CSOs */
#include "etna_blend.h"

#include "etna_internal.h"
#include "etna_pipe.h"
#include "etna_translate.h"
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "util/u_memory.h"

#include <etnaviv/common.xml.h>
#include <etnaviv/state.xml.h>
#include <etnaviv/state_3d.xml.h>

static void *etna_pipe_create_blend_state(struct pipe_context *pipe,
                            const struct pipe_blend_state *bs)
{
    //struct etna_pipe_context *priv = etna_pipe_context(pipe);
    struct compiled_blend_state *cs = CALLOC_STRUCT(compiled_blend_state);
    const struct pipe_rt_blend_state *rt0 = &bs->rt[0];
    /* Enable blending if
     * - blend enabled in blend state
     * - NOT source factor is ONE and destination factor ZERO for both rgb and
     *   alpha (which would mean that blending is effectively disabled)
     */
    bool enable = rt0->blend_enable &&
            !(rt0->rgb_src_factor == PIPE_BLENDFACTOR_ONE && rt0->rgb_dst_factor == PIPE_BLENDFACTOR_ZERO &&
              rt0->alpha_src_factor == PIPE_BLENDFACTOR_ONE && rt0->alpha_dst_factor == PIPE_BLENDFACTOR_ZERO);
    /* Enable separate alpha if
     * - Blending enabled (see above)
     * - NOT source factor is equal to destination factor for both rgb abd
     *   alpha (which would effectively that mean alpha is not separate)
     */
    bool separate_alpha = enable && !(rt0->rgb_src_factor == rt0->alpha_src_factor &&
                                      rt0->rgb_dst_factor == rt0->alpha_dst_factor);
    /* If the complete render target is written, set full_overwrite:
     * - The color mask is 1111
     * - No blending is used
     */
    bool full_overwrite = (rt0->colormask == 15) && !enable;

    if(enable)
    {
        cs->PE_ALPHA_CONFIG =
                VIVS_PE_ALPHA_CONFIG_BLEND_ENABLE_COLOR |
                (separate_alpha ? VIVS_PE_ALPHA_CONFIG_BLEND_SEPARATE_ALPHA : 0) |
                VIVS_PE_ALPHA_CONFIG_SRC_FUNC_COLOR(translate_blend_factor(rt0->rgb_src_factor)) |
                VIVS_PE_ALPHA_CONFIG_SRC_FUNC_ALPHA(translate_blend_factor(rt0->alpha_src_factor)) |
                VIVS_PE_ALPHA_CONFIG_DST_FUNC_COLOR(translate_blend_factor(rt0->rgb_dst_factor)) |
                VIVS_PE_ALPHA_CONFIG_DST_FUNC_ALPHA(translate_blend_factor(rt0->alpha_dst_factor)) |
                VIVS_PE_ALPHA_CONFIG_EQ_COLOR(translate_blend(rt0->rgb_func)) |
                VIVS_PE_ALPHA_CONFIG_EQ_ALPHA(translate_blend(rt0->alpha_func));
    } else {
        cs->PE_ALPHA_CONFIG = 0;
    }
    cs->PE_COLOR_FORMAT =
            VIVS_PE_COLOR_FORMAT_COMPONENTS(rt0->colormask) |
            (full_overwrite ? VIVS_PE_COLOR_FORMAT_OVERWRITE : 0);
    cs->PE_LOGIC_OP =
            VIVS_PE_LOGIC_OP_OP(bs->logicop_enable ? bs->logicop_func : LOGIC_OP_COPY) /* 1-to-1 mapping */ |
            0x000E4000 /* ??? */;
    /* independent_blend_enable not needed: only one rt supported */
    /* XXX alpha_to_coverage / alpha_to_one? */
    /* Set dither registers based on dither status. These registers set the dither pattern,
     * for now, set the same values as the blob.
     */
    if(bs->dither)
    {
        cs->PE_DITHER[0] = 0x6e4ca280;
        cs->PE_DITHER[1] = 0x5d7f91b3;
    } else {
        cs->PE_DITHER[0] = 0xffffffff;
        cs->PE_DITHER[1] = 0xffffffff;
    }
    return cs;
}

static void etna_pipe_bind_blend_state(struct pipe_context *pipe, void *bs)
{
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
    priv->dirty_bits |= ETNA_STATE_BLEND;
    priv->blend_p = bs;
    if(bs)
        priv->blend = *(struct compiled_blend_state*)bs;
}

static void etna_pipe_delete_blend_state(struct pipe_context *pipe, void *bs)
{
    //struct etna_pipe_context *priv = etna_pipe_context(pipe);
    FREE(bs);
}

void etna_pipe_blend_init(struct pipe_context *pc)
{
    pc->create_blend_state = etna_pipe_create_blend_state;
    pc->bind_blend_state = etna_pipe_bind_blend_state;
    pc->delete_blend_state = etna_pipe_delete_blend_state;
}

