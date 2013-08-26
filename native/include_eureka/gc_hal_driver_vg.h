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






#ifndef __gc_hal_driver_vg_h_
#define __gc_hal_driver_vg_h_



#include "gc_hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
******************************* I/O Control Codes ******************************
\******************************************************************************/
#ifndef gcvHAL_CLASS
#define gcvHAL_CLASS            "gal3d"
#endif
#define IOCTL_GCHAL_INTERFACE   30000

/******************************************************************************\
********************************* Command Codes ********************************
\******************************************************************************/

/******************************************************************************\
********************* Command buffer information structure. ********************
\******************************************************************************/

typedef struct _gcsCOMMAND_BUFFER_INFO * gcsCOMMAND_BUFFER_INFO_PTR;
typedef struct _gcsCOMMAND_BUFFER_INFO
{
    /* FE command buffer interrupt ID. */
    gctINT32                    feBufferInt;

    /* TS overflow interrupt ID. */
    gctINT32                    tsOverflowInt;

    /* Alignment and mask for the buffer address. */
    gctUINT                     addressMask;
    gctSIZE_T                   addressAlignment;

    /* Alignment for each command. */
    gctSIZE_T                   commandAlignment;

    /* Number of bytes required by the STATE command. */
    gctSIZE_T                   stateCommandSize;

    /* Number of bytes required by the RESTART command. */
    gctSIZE_T                   restartCommandSize;

    /* Number of bytes required by the FETCH command. */
    gctSIZE_T                   fetchCommandSize;

    /* Number of bytes required by the CALL command. */
    gctSIZE_T                   callCommandSize;

    /* Number of bytes required by the RETURN command. */
    gctSIZE_T                   returnCommandSize;

    /* Number of bytes required by the EVENT command. */
    gctSIZE_T                   eventCommandSize;

    /* Number of bytes required by the END command. */
    gctSIZE_T                   endCommandSize;

    /* Number of bytes reserved at the tail of a static command buffer. */
    gctSIZE_T                   staticTailSize;

    /* Number of bytes reserved at the tail of a dynamic command buffer. */
    gctSIZE_T                   dynamicTailSize;
}
gcsCOMMAND_BUFFER_INFO;

/******************************************************************************\
******************************** Task Structures *******************************
\******************************************************************************/

typedef enum _gceTASK
{
    gcvTASK_LINK,
    gcvTASK_CLUSTER,
    gcvTASK_INCREMENT,
    gcvTASK_DECREMENT,
    gcvTASK_SIGNAL,
    gcvTASK_LOCKDOWN,
    gcvTASK_UNLOCK_VIDEO_MEMORY,
    gcvTASK_FREE_VIDEO_MEMORY,
    gcvTASK_FREE_CONTIGUOUS_MEMORY,
    gcvTASK_UNMAP_USER_MEMORY,
    gcvTASK_UNMAP_MEMORY
}
gceTASK;

typedef struct _gcsTASK_HEADER * gcsTASK_HEADER_PTR;
typedef struct _gcsTASK_HEADER
{
    /* Task ID. */
    IN gceTASK                  id;
}
gcsTASK_HEADER;

typedef struct _gcsTASK_LINK * gcsTASK_LINK_PTR;
typedef struct _gcsTASK_LINK
{
    /* Task ID (gcvTASK_LINK). */
    IN gceTASK                  id;

    /* Pointer to the next task container. */
    IN gctPOINTER               cotainer;

    /* Pointer to the next task from the next task container. */
    IN gcsTASK_HEADER_PTR       task;
}
gcsTASK_LINK;

typedef struct _gcsTASK_CLUSTER * gcsTASK_CLUSTER_PTR;
typedef struct _gcsTASK_CLUSTER
{
    /* Task ID (gcvTASK_CLUSTER). */
    IN gceTASK                  id;

    /* Number of tasks in the cluster. */
    IN gctUINT                  taskCount;
}
gcsTASK_CLUSTER;

typedef struct _gcsTASK_INCREMENT * gcsTASK_INCREMENT_PTR;
typedef struct _gcsTASK_INCREMENT
{
    /* Task ID (gcvTASK_INCREMENT). */
    IN gceTASK                  id;

    /* Address of the variable to increment. */
    IN gctUINT32                address;
}
gcsTASK_INCREMENT;

typedef struct _gcsTASK_DECREMENT * gcsTASK_DECREMENT_PTR;
typedef struct _gcsTASK_DECREMENT
{
    /* Task ID (gcvTASK_DECREMENT). */
    IN gceTASK                  id;

    /* Address of the variable to decrement. */
    IN gctUINT32                address;
}
gcsTASK_DECREMENT;

typedef struct _gcsTASK_SIGNAL * gcsTASK_SIGNAL_PTR;
typedef struct _gcsTASK_SIGNAL
{
    /* Task ID (gcvTASK_SIGNAL). */
    IN gceTASK                  id;

    /* Process owning the signal. */
    IN gctHANDLE                process;

    /* Signal handle to signal. */
    IN gctSIGNAL                signal;

#if defined(__QNXNTO__)
    IN gctINT32                 coid;
    IN gctINT32                 rcvid;
#endif
}
gcsTASK_SIGNAL;

typedef struct _gcsTASK_LOCKDOWN * gcsTASK_LOCKDOWN_PTR;
typedef struct _gcsTASK_LOCKDOWN
{
    /* Task ID (gcvTASK_LOCKDOWN). */
    IN gceTASK                  id;

    /* Address of the user space counter. */
    IN gctUINT32                userCounter;

    /* Address of the kernel space counter. */
    IN gctUINT32                kernelCounter;

    /* Process owning the signal. */
    IN gctHANDLE                process;

    /* Signal handle to signal. */
    IN gctSIGNAL                signal;
}
gcsTASK_LOCKDOWN;

typedef struct _gcsTASK_UNLOCK_VIDEO_MEMORY * gcsTASK_UNLOCK_VIDEO_MEMORY_PTR;
typedef struct _gcsTASK_UNLOCK_VIDEO_MEMORY
{
    /* Task ID (gcvTASK_UNLOCK_VIDEO_MEMORY). */
    IN gceTASK                  id;

    /* Allocated video memory. */
    IN gcuVIDMEM_NODE_PTR       node;
}
gcsTASK_UNLOCK_VIDEO_MEMORY;

typedef struct _gcsTASK_FREE_VIDEO_MEMORY * gcsTASK_FREE_VIDEO_MEMORY_PTR;
typedef struct _gcsTASK_FREE_VIDEO_MEMORY
{
    /* Task ID (gcvTASK_FREE_VIDEO_MEMORY). */
    IN gceTASK                  id;

    /* Allocated video memory. */
    IN gcuVIDMEM_NODE_PTR       node;
}
gcsTASK_FREE_VIDEO_MEMORY;

typedef struct _gcsTASK_FREE_CONTIGUOUS_MEMORY * gcsTASK_FREE_CONTIGUOUS_MEMORY_PTR;
typedef struct _gcsTASK_FREE_CONTIGUOUS_MEMORY
{
    /* Task ID (gcvTASK_FREE_CONTIGUOUS_MEMORY). */
    IN gceTASK                  id;

    /* Number of bytes allocated. */
    IN gctSIZE_T                bytes;

    /* Physical address of allocation. */
    IN gctPHYS_ADDR             physical;

    /* Logical address of allocation. */
    IN gctPOINTER               logical;
}
gcsTASK_FREE_CONTIGUOUS_MEMORY;

typedef struct _gcsTASK_UNMAP_USER_MEMORY * gcsTASK_UNMAP_USER_MEMORY_PTR;
typedef struct _gcsTASK_UNMAP_USER_MEMORY
{
    /* Task ID (gcvTASK_UNMAP_USER_MEMORY). */
    IN gceTASK                  id;

    /* Base address of user memory to unmap. */
    IN gctPOINTER               memory;

    /* Size of user memory in bytes to unmap. */
    IN gctSIZE_T                size;

    /* Info record returned by gcvHAL_MAP_USER_MEMORY. */
    IN gctPOINTER               info;

    /* Physical address of mapped memory as returned by
       gcvHAL_MAP_USER_MEMORY. */
    IN gctUINT32                address;
}
gcsTASK_UNMAP_USER_MEMORY;

typedef struct _gcsTASK_UNMAP_MEMORY * gcsTASK_UNMAP_MEMORY_PTR;
typedef struct _gcsTASK_UNMAP_MEMORY
{
    /* Task ID (gcvTASK_UNMAP_MEMORY). */
    IN gceTASK                  id;

    /* Physical memory address to unmap. */
    IN gctPHYS_ADDR             physical;

    /* Number of bytes in physical memory to unmap. */
    IN gctSIZE_T                bytes;

    /* Address of mapped memory to unmap. */
    IN gctPOINTER               logical;
}
gcsTASK_UNMAP_MEMORY;

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_driver_h_ */
