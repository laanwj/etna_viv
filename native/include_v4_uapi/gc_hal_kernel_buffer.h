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




#ifndef __gc_hal_kernel_buffer_h_
#define __gc_hal_kernel_buffer_h_

/******************************************************************************\
************************ Command Buffer and Event Objects **********************
\******************************************************************************/

/* The number of context buffers per user. */
#define gcdCONTEXT_BUFFER_COUNT 2

/* State delta record. */
typedef struct _gcsSTATE_DELTA_RECORD * gcsSTATE_DELTA_RECORD_PTR;
typedef struct _gcsSTATE_DELTA_RECORD
{
    /* State address. */
    unsigned int                address;

    /* State mask. */
    __u32                       mask;

    /* State data. */
    __u32                       data;
}
gcsSTATE_DELTA_RECORD;

/* State delta. */
typedef struct _gcsSTATE_DELTA
{
    /* Main state delta ID. Every time state delta structure gets reinitialized,
       main ID is incremented. If main state ID overflows, all map entry IDs get
       reinitialized to make sure there is no potential erroneous match after
       the overflow.*/
    unsigned int                id;

    /* The number of contexts pending modification by the delta. */
    int                         refCount;

    /* Vertex element count for the delta buffer. */
    unsigned int                elementCount;

    /* Number of states currently stored in the record array. */
    unsigned int                recordCount;

    /* Record array; holds all modified states. */
    gcsSTATE_DELTA_RECORD_PTR   recordArray;

    /* Map entry ID is used for map entry validation. If map entry ID does not
       match the main state delta ID, the entry and the corresponding state are
       considered not in use. */
    unsigned int *              mapEntryID;
    unsigned int                mapEntryIDSize;

    /* If the map entry ID matches the main state delta ID, index points to
       the state record in the record array. */
    unsigned int *              mapEntryIndex;

    /* Previous and next state deltas. */
    gcsSTATE_DELTA_PTR          prev;
    gcsSTATE_DELTA_PTR          next;
}
gcsSTATE_DELTA;

/* Command buffer object. */
struct _gcoCMDBUF
{
    /* The object. */
    gcsOBJECT                   object;

    /* Command buffer entry and exit pipes. */
    gcePIPE_SELECT              entryPipe;
    gcePIPE_SELECT              exitPipe;

    /* Feature usage flags. */
    int                         using2D;
    int                         using3D;
    int                         usingFilterBlit;
    int                         usingPalette;

    /* Physical address of command buffer. */
    gctPHYS_ADDR                physical;

    /* Logical address of command buffer. */
    void *                      logical;

    /* Number of bytes in command buffer. */
    size_t                      bytes;

    /* Start offset into the command buffer. */
    __u32                       startOffset;

    /* Current offset into the command buffer. */
    __u32                       offset;

    /* Number of free bytes in command buffer. */
    size_t                      free;

    /* Location of the last reserved area. */
    void *                      lastReserve;
    unsigned int                lastOffset;

#if gcdSECURE_USER
    /* Hint array for the current command buffer. */
    unsigned int                hintArraySize;
    __u32 *                     hintArray;
    __u32 *                     hintArrayTail;
#endif
};

typedef struct _gcsQUEUE
{
    /* Pointer to next gcsQUEUE structure. */
    gcsQUEUE_PTR                next;

    /* Event information. */
    gcsHAL_INTERFACE            iface;
}
gcsQUEUE;

/* Event queue. */
struct _gcoQUEUE
{
    /* The object. */
    gcsOBJECT                   object;

    /* Pointer to current event queue. */
    gcsQUEUE_PTR                head;
    gcsQUEUE_PTR                tail;

    /* List of free records. */
    gcsQUEUE_PTR                freeList;
    #define gcdIN_QUEUE_RECORD_LIMIT 16
    /* Number of records currently in queue */
    __u32                       recordCount;
};

#endif /* __gc_hal_kernel_buffer_h_ */
