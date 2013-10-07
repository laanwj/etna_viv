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
/* Internal header: convert from etnaviv-specific values to kernel driver values */
#ifndef VIV_INTERNAL_H
#define VIV_INTERNAL_H

/* Convert VIV_SURF_* to kernel specific gcvSURF_* */
static inline gceSURF_TYPE convert_surf_type(enum viv_surf_type type)
{
    switch(type)
    {
    case VIV_SURF_INDEX: return gcvSURF_INDEX;
    case VIV_SURF_VERTEX: return gcvSURF_VERTEX;
    case VIV_SURF_TEXTURE: return gcvSURF_TEXTURE;
    case VIV_SURF_RENDER_TARGET: return gcvSURF_RENDER_TARGET;
    case VIV_SURF_DEPTH: return gcvSURF_DEPTH;
    case VIV_SURF_BITMAP: return gcvSURF_BITMAP;
    case VIV_SURF_TILE_STATUS: return gcvSURF_TILE_STATUS;
#ifndef GCABI_HAS_CONTEXT
    case VIV_SURF_IMAGE: return gcvSURF_IMAGE;
#endif
    case VIV_SURF_MASK: return gcvSURF_MASK;
    case VIV_SURF_SCISSOR: return gcvSURF_SCISSOR;
    case VIV_SURF_HIERARCHICAL_DEPTH: return gcvSURF_HIERARCHICAL_DEPTH;
    default: return gcvSURF_TYPE_UNKNOWN;
    }
}

/* Convert video memory pool */
static inline gcePOOL convert_pool(enum viv_pool pool)
{
    switch(pool)
    {
    case VIV_POOL_DEFAULT: return gcvPOOL_DEFAULT;
    case VIV_POOL_LOCAL: return gcvPOOL_LOCAL;
    case VIV_POOL_LOCAL_INTERNAL: return gcvPOOL_LOCAL_INTERNAL;
    case VIV_POOL_LOCAL_EXTERNAL: return gcvPOOL_LOCAL_EXTERNAL;
    case VIV_POOL_UNIFIED: return gcvPOOL_UNIFIED;
    case VIV_POOL_SYSTEM: return gcvPOOL_SYSTEM;
    case VIV_POOL_VIRTUAL: return gcvPOOL_VIRTUAL;
    case VIV_POOL_USER: return gcvPOOL_USER;
    case VIV_POOL_CONTIGUOUS: return gcvPOOL_CONTIGUOUS;
    default: return gcvPOOL_UNKNOWN;
    }
};

/* Convert semaphore recipient */
static inline gceKERNEL_WHERE convert_where(enum viv_where where)
{
    switch(where)
    {
    case VIV_WHERE_COMMAND: return gcvKERNEL_COMMAND;
    case VIV_WHERE_PIXEL: return gcvKERNEL_PIXEL;
    default: return gcvKERNEL_PIXEL; /* unknown default */
    }
}

#ifdef GCABI_UINT64_POINTERS
/* imx6 BSP 4.x Vivante driver casts all pointers to 64 bit integers
 * provide macros to cast back and forth. */
#define PTR_TO_VIV(x) ((uint64_t)((intptr_t)(x)))
#define VIV_TO_PTR(x) ((void*)((intptr_t)(x)))
#define HANDLE_TO_VIV(x) (x)
#define VIV_TO_HANDLE(x) (x)
#else
#define PTR_TO_VIV(x) (x)
#define VIV_TO_PTR(x) (x)
#define HANDLE_TO_VIV(x) ((void*)((intptr_t)(x)))
#define VIV_TO_HANDLE(x) ((uint64_t)(intptr_t)(x))
#endif

#endif

