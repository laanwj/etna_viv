/****************************************************************************
*
*    Copyright (C) 2005 - 2013 by Vivante Corp.
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


#ifndef __gc_hal_kernel_hardware_command_vg_h_
#define __gc_hal_kernel_hardware_command_vg_h_

/******************************************************************************\
******************* Task and Interrupt Management Structures. ******************
\******************************************************************************/

/* Task storage header. */
typedef struct _gcsTASK_STORAGE * gcsTASK_STORAGE_PTR;
typedef struct _gcsTASK_STORAGE
{
    /* Next allocated storage buffer. */
    gcsTASK_STORAGE_PTR         next;
}
gcsTASK_STORAGE;

/* Task container header. */
typedef struct _gcsTASK_CONTAINER * gcsTASK_CONTAINER_PTR;
typedef struct _gcsTASK_CONTAINER
{
    /* The number of tasks left to be processed in the container. */
    gctINT                      referenceCount;

    /* Size of the buffer. */
    gctUINT                     size;

    /* Link to the previous and the next allocated containers. */
    gcsTASK_CONTAINER_PTR       allocPrev;
    gcsTASK_CONTAINER_PTR       allocNext;

    /* Link to the previous and the next containers in the free list. */
    gcsTASK_CONTAINER_PTR       freePrev;
    gcsTASK_CONTAINER_PTR       freeNext;
}
gcsTASK_CONTAINER;

/* Kernel space task master table entry. */
typedef struct _gcsBLOCK_TASK_ENTRY * gcsBLOCK_TASK_ENTRY_PTR;
typedef struct _gcsBLOCK_TASK_ENTRY
{
    /* Pointer to the current task container for the block. */
    gcsTASK_CONTAINER_PTR       container;

    /* Pointer to the current task data within the container. */
    gcsTASK_HEADER_PTR          task;

    /* Pointer to the last link task within the container. */
    gcsTASK_LINK_PTR            link;

    /* Number of interrupts allocated for this block. */
    gctUINT                     interruptCount;

    /* The index of the current interrupt. */
    gctUINT                     interruptIndex;

    /* Interrupt semaphore. */
    gctSEMAPHORE                interruptSemaphore;

    /* Interrupt value array. */
    gctINT32                    interruptArray[32];
}
gcsBLOCK_TASK_ENTRY;


/******************************************************************************\
********************* Command Queue Management Structures. *********************
\******************************************************************************/

/* Command queue kernel element pointer. */
typedef struct _gcsKERNEL_CMDQUEUE * gcsKERNEL_CMDQUEUE_PTR;

/* Command queue object handler function type. */
typedef gceSTATUS (* gctOBJECT_HANDLER) (
    gckVGKERNEL Kernel,
    gcsKERNEL_CMDQUEUE_PTR Entry
    );

/* Command queue kernel element. */
typedef struct _gcsKERNEL_CMDQUEUE
{
    /* The number of buffers in the queue. */
    gcsCMDBUFFER_PTR            commandBuffer;

    /* Pointer to the object handler function. */
    gctOBJECT_HANDLER           handler;
}
gcsKERNEL_CMDQUEUE;

/* Command queue header. */
typedef struct _gcsKERNEL_QUEUE_HEADER * gcsKERNEL_QUEUE_HEADER_PTR;
typedef struct _gcsKERNEL_QUEUE_HEADER
{
    /* The size of the buffer in bytes. */
    gctUINT                     size;

    /* The number of pending entries to be processed. */
    volatile gctUINT            pending;

    /* The current command queue entry. */
    gcsKERNEL_CMDQUEUE_PTR      currentEntry;

    /* Next buffer. */
    gcsKERNEL_QUEUE_HEADER_PTR  next;
}
gcsKERNEL_QUEUE_HEADER;


/******************************************************************************\
******************************* gckVGCOMMAND Object *******************************
\******************************************************************************/

/* gckVGCOMMAND object. */
struct _gckVGCOMMAND
{
    /***************************************************************************
    ** Object data and pointers.
    */

    gcsOBJECT                   object;
    gckVGKERNEL                 kernel;
    gckOS                       os;
    gckVGHARDWARE                   hardware;

    /* Features. */
    gctBOOL                     fe20;
    gctBOOL                     vg20;
    gctBOOL                     vg21;


    /***************************************************************************
    ** Enable command queue dumping.
    */

    gctBOOL                     enableDumping;


    /***************************************************************************
    ** Bus Error interrupt.
    */

    gctINT32                    busErrorInt;


    /***************************************************************************
    ** Command buffer information.
    */

    gcsCOMMAND_BUFFER_INFO      info;


    /***************************************************************************
    ** Synchronization objects.
    */

    gctPOINTER                  queueMutex;
    gctPOINTER                  taskMutex;
    gctPOINTER                  commitMutex;


    /***************************************************************************
    ** Task management.
    */

    /* The head of the storage buffer linked list. */
    gcsTASK_STORAGE_PTR         taskStorage;

    /* Allocation size. */
    gctUINT                     taskStorageGranularity;
    gctUINT                     taskStorageUsable;

    /* The free container list. */
    gcsTASK_CONTAINER_PTR       taskFreeHead;
    gcsTASK_CONTAINER_PTR       taskFreeTail;

    /* Task table */
    gcsBLOCK_TASK_ENTRY         taskTable[gcvBLOCK_COUNT];


    /***************************************************************************
    ** Command queue.
    */

    /* Pointer to the allocated queue memory. */
    gcsKERNEL_QUEUE_HEADER_PTR  queue;

    /* Pointer to the current available queue from which new queue entries
       will be allocated. */
    gcsKERNEL_QUEUE_HEADER_PTR  queueHead;

    /* If different from queueHead, points to the command queue which is
       currently being executed by the hardware. */
    gcsKERNEL_QUEUE_HEADER_PTR  queueTail;

    /* Points to the queue to merge the tail with when the tail is processed. */
    gcsKERNEL_QUEUE_HEADER_PTR  mergeQueue;

    /* Queue overflow counter. */
    gctUINT                     queueOverflow;


    /***************************************************************************
    ** Context.
    */

    /* Context counter used for unique ID. */
    gctUINT64                   contextCounter;

    /* Current context ID. */
    gctUINT64                   currentContext;

    /* Command queue power semaphore. */
    gctPOINTER                  powerSemaphore;
    gctINT32                    powerStallInt;
    gcsCMDBUFFER_PTR            powerStallBuffer;
    gctSIGNAL                   powerStallSignal;

};

/******************************************************************************\
************************ gckVGCOMMAND Object Internal API. ***********************
\******************************************************************************/

/* Initialize architecture dependent command buffer information. */
gceSTATUS
gckVGCOMMAND_InitializeInfo(
    IN gckVGCOMMAND Command
    );

/* Form a STATE command at the specified location in the command buffer. */
gceSTATUS
gckVGCOMMAND_StateCommand(
    IN gckVGCOMMAND Command,
    IN gctUINT32 Pipe,
    IN gctPOINTER Logical,
    IN gctUINT32 Address,
    IN gctSIZE_T Count,
    IN OUT gctSIZE_T * Bytes
    );

/* Form a RESTART command at the specified location in the command buffer. */
gceSTATUS
gckVGCOMMAND_RestartCommand(
    IN gckVGCOMMAND Command,
    IN gctPOINTER Logical,
    IN gctUINT32 FetchAddress,
    IN gctUINT FetchCount,
    IN OUT gctSIZE_T * Bytes
    );

/* Form a FETCH command at the specified location in the command buffer. */
gceSTATUS
gckVGCOMMAND_FetchCommand(
    IN gckVGCOMMAND Command,
    IN gctPOINTER Logical,
    IN gctUINT32 FetchAddress,
    IN gctUINT FetchCount,
    IN OUT gctSIZE_T * Bytes
    );

/* Form a CALL command at the specified location in the command buffer. */
gceSTATUS
gckVGCOMMAND_CallCommand(
    IN gckVGCOMMAND Command,
    IN gctPOINTER Logical,
    IN gctUINT32 FetchAddress,
    IN gctUINT FetchCount,
    IN OUT gctSIZE_T * Bytes
    );

/* Form a RETURN command at the specified location in the command buffer. */
gceSTATUS
gckVGCOMMAND_ReturnCommand(
    IN gckVGCOMMAND Command,
    IN gctPOINTER Logical,
    IN OUT gctSIZE_T * Bytes
    );

/* Form an EVENT command at the specified location in the command buffer. */
gceSTATUS
gckVGCOMMAND_EventCommand(
    IN gckVGCOMMAND Command,
    IN gctPOINTER Logical,
    IN gceBLOCK Block,
    IN gctINT32 InterruptId,
    IN OUT gctSIZE_T * Bytes
    );

/* Form an END command at the specified location in the command buffer. */
gceSTATUS
gckVGCOMMAND_EndCommand(
    IN gckVGCOMMAND Command,
    IN gctPOINTER Logical,
    IN gctINT32 InterruptId,
    IN OUT gctSIZE_T * Bytes
    );

#endif  /* __gc_hal_kernel_hardware_command_h_ */

