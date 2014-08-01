/****************************************************************************
*
*    Copyright (C) 2005 - 2012 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/

#ifndef __gc_hal_kernel_context_h_
#define __gc_hal_kernel_context_h_

#include "gc_hal.h"

typedef struct _gckEVENT *      gckEVENT;

/* Maps state locations within the context buffer. */
typedef struct _gcsSTATE_MAP * gcsSTATE_MAP_PTR;
typedef struct _gcsSTATE_MAP
{
    /* Index of the state in the context buffer. */
    unsigned int                index;

    /* State mask. */
    u32                         mask;
}
gcsSTATE_MAP;

/* Context buffer. */
typedef struct _gcsCONTEXT * gcsCONTEXT_PTR;
typedef struct _gcsCONTEXT
{
    /* For debugging: the number of context buffer in the order of creation. */
#if gcmIS_DEBUG(gcdDEBUG_CODE)
    unsigned int                num;
#endif

    /* Pointer to gckEVENT object. */
    gckEVENT                    eventObj;

    /* Context busy signal. */
    gctSIGNAL                   signal;

    /* Physical address of the context buffer. */
    gctPHYS_ADDR                physical;

    /* Logical address of the context buffer. */
    u32 *                       logical;

    /* Pointer to the LINK commands. */
    void *                      link2D;
    void *                      link3D;

    /* The number of pending state deltas. */
    unsigned int                deltaCount;

    /* Pointer to the first delta to be applied. */
    struct _gcsSTATE_DELTA *    delta;

    /* Next context buffer. */
    gcsCONTEXT_PTR              next;
}
gcsCONTEXT;

/* gckCONTEXT structure that hold the current context. */
struct _gckCONTEXT
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to gckOS object. */
    gckOS                       os;

    /* Pointer to gckHARDWARE object. */
    gckHARDWARE                 hardware;

    /* Command buffer alignment. */
    size_t                      alignment;
    size_t                      reservedHead;
    size_t                      reservedTail;

    /* Context buffer metrics. */
    size_t                      stateCount;
    size_t                      totalSize;
    size_t                      bufferSize;
    u32                         linkIndex2D;
    u32                         linkIndex3D;
    u32                         linkIndexXD;
    u32                         entryOffset3D;
    u32                         entryOffsetXDFrom2D;
    u32                         entryOffsetXDFrom3D;

    /* Dirty flags. */
    int                         dirty;
    int                         dirty2D;
    int                         dirty3D;
    gcsCONTEXT_PTR              dirtyBuffer;

    /* State mapping. */
    gcsSTATE_MAP_PTR            map;

    /* List of context buffers. */
    gcsCONTEXT_PTR              buffer;

    /* A copy of the user record array. */
    unsigned int                recordArraySize;
    struct _gcsSTATE_DELTA_RECORD *recordArray;

    /* Requested pipe select for context. */
    gcePIPE_SELECT              entryPipe;
    gcePIPE_SELECT              exitPipe;

    /* Variables used for building state buffer. */
    u32                         lastAddress;
    size_t                      lastSize;
    u32                         lastIndex;
    int                         lastFixed;

    /* Hint array. */
#if gcdSECURE_USER
    int *                       hint;
#endif
};

#endif /* __gc_hal_kernel_context_h_ */
