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
/* Depth stencil alpha CSOs */
#include "etna_zsa.h"

#include "etna_internal.h"
#include "etna_pipe.h"
#include "etna_translate.h"
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "util/u_memory.h"

#include <etnaviv/common.xml.h>
#include <etnaviv/state.xml.h>
#include <etnaviv/state_3d.xml.h>

static void *etna_pipe_create_depth_stencil_alpha_state(struct pipe_context *pipe,
                                    const struct pipe_depth_stencil_alpha_state *dsa_p)
{
    //struct etna_pipe_context *priv = etna_pipe_context(pipe);
    struct compiled_depth_stencil_alpha_state *cs = CALLOC_STRUCT(compiled_depth_stencil_alpha_state);
    struct pipe_depth_stencil_alpha_state dsa = *dsa_p;
    /* XXX does stencil[0] / stencil[1] order depend on rs->front_ccw? */
    bool early_z = true;
    bool disable_zs = !dsa.depth.writemask;
    int i;

    /* Set operations to KEEP if write mask is 0.
     * When we don't do this, the depth buffer is written for the entire primitive instead of
     * just where the stencil condition holds (GC600 rev 0x0019, without feature CORRECT_STENCIL).
     * Not sure if this is a hardware bug or just a strange edge case.
     */
    for(i=0; i<2; ++i)
    {
        if(dsa.stencil[i].writemask == 0)
        {
            dsa.stencil[i].fail_op = dsa.stencil[i].zfail_op = dsa.stencil[i].zpass_op = PIPE_STENCIL_OP_KEEP;
        }
    }

    /* Determine whether to enable early z reject. Don't enable it when any of
     * the stencil-modifying functions is used. */
    if(dsa.stencil[0].enabled)
    {
        if(dsa.stencil[0].fail_op != PIPE_STENCIL_OP_KEEP ||
           dsa.stencil[0].zfail_op != PIPE_STENCIL_OP_KEEP ||
           dsa.stencil[0].zpass_op != PIPE_STENCIL_OP_KEEP)
        {
            disable_zs = early_z = false;
        }
        else if(dsa.stencil[1].enabled)
        {
            if(dsa.stencil[1].fail_op != PIPE_STENCIL_OP_KEEP ||
               dsa.stencil[1].zfail_op != PIPE_STENCIL_OP_KEEP ||
               dsa.stencil[1].zpass_op != PIPE_STENCIL_OP_KEEP)
            {
                disable_zs = early_z = false;
            }
        }
    }
    /* Disable early z reject when no depth test is enabled.
     * This avoids having to sample depth even though we know it's going to succeed.
     */
    if(dsa.depth.enabled == false || dsa.depth.func == PIPE_FUNC_ALWAYS)
        early_z = false;
    if(DBG_ENABLED(ETNA_DBG_NO_EARLY_Z))
        early_z = false;
    /* compare funcs have 1 to 1 mapping */
    cs->PE_DEPTH_CONFIG =
            VIVS_PE_DEPTH_CONFIG_DEPTH_FUNC(dsa.depth.enabled ? dsa.depth.func : PIPE_FUNC_ALWAYS) |
            (dsa.depth.writemask ? VIVS_PE_DEPTH_CONFIG_WRITE_ENABLE : 0) |
            (early_z ? VIVS_PE_DEPTH_CONFIG_EARLY_Z : 0) |
            (disable_zs ? VIVS_PE_DEPTH_CONFIG_DISABLE_ZS : 0);
    cs->PE_ALPHA_OP =
            (dsa.alpha.enabled ? VIVS_PE_ALPHA_OP_ALPHA_TEST : 0) |
            VIVS_PE_ALPHA_OP_ALPHA_FUNC(dsa.alpha.func) |
            VIVS_PE_ALPHA_OP_ALPHA_REF(etna_cfloat_to_uint8(dsa.alpha.ref_value));
    cs->PE_STENCIL_OP =
            VIVS_PE_STENCIL_OP_FUNC_FRONT(dsa.stencil[0].func) |
            VIVS_PE_STENCIL_OP_FUNC_BACK(dsa.stencil[1].func) |
            VIVS_PE_STENCIL_OP_FAIL_FRONT(translate_stencil_op(dsa.stencil[0].fail_op)) |
            VIVS_PE_STENCIL_OP_FAIL_BACK(translate_stencil_op(dsa.stencil[1].fail_op)) |
            VIVS_PE_STENCIL_OP_DEPTH_FAIL_FRONT(translate_stencil_op(dsa.stencil[0].zfail_op)) |
            VIVS_PE_STENCIL_OP_DEPTH_FAIL_BACK(translate_stencil_op(dsa.stencil[1].zfail_op)) |
            VIVS_PE_STENCIL_OP_PASS_FRONT(translate_stencil_op(dsa.stencil[0].zpass_op)) |
            VIVS_PE_STENCIL_OP_PASS_BACK(translate_stencil_op(dsa.stencil[1].zpass_op));
    cs->PE_STENCIL_CONFIG =
            translate_stencil_mode(dsa.stencil[0].enabled, dsa.stencil[1].enabled) |
            VIVS_PE_STENCIL_CONFIG_MASK_FRONT(dsa.stencil[0].valuemask) |
            VIVS_PE_STENCIL_CONFIG_WRITE_MASK_FRONT(dsa.stencil[0].writemask);
            /* XXX back masks in VIVS_PE_DEPTH_CONFIG_EXT? */
            /* XXX VIVS_PE_STENCIL_CONFIG_REF_FRONT comes from pipe_stencil_ref */

    /* XXX does alpha/stencil test affect PE_COLOR_FORMAT_OVERWRITE? */
    return cs;
}

static void etna_pipe_bind_depth_stencil_alpha_state(struct pipe_context *pipe, void *dsa)
{
    struct etna_pipe_context *priv = etna_pipe_context(pipe);
    priv->dirty_bits |= ETNA_STATE_DSA;
    priv->depth_stencil_alpha_p = dsa;
    if(dsa)
       priv->depth_stencil_alpha = *(struct compiled_depth_stencil_alpha_state*)dsa;
}

static void etna_pipe_delete_depth_stencil_alpha_state(struct pipe_context *pipe, void *dsa)
{
    //struct etna_pipe_context *priv = etna_pipe_context(pipe);
    FREE(dsa);
}

void etna_pipe_zsa_init(struct pipe_context *pc)
{
    pc->create_depth_stencil_alpha_state = etna_pipe_create_depth_stencil_alpha_state;
    pc->bind_depth_stencil_alpha_state = etna_pipe_bind_depth_stencil_alpha_state;
    pc->delete_depth_stencil_alpha_state = etna_pipe_delete_depth_stencil_alpha_state;
}

