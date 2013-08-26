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




#include "gc_hal_kernel_precomp.h"

#define _GC_OBJ_ZONE    gcvZONE_VIDMEM

//####modified for marvell-bg2
#if USE_GALOIS_SHM
#include "shm_api.h"

/*cszhu: add for cache operationwhile alloc shared memory*/
#include <linux/dma-mapping.h>
#include <asm/cacheflush.h>
/*cszhu*/

typedef struct _GFX_SHM_STAT{
    unsigned int total_malloc;
    unsigned int current_malloc;
    unsigned int malloc_count;
    unsigned int free_count;
    unsigned int total_free;
}GFX_SHM_STAT;
static GFX_SHM_STAT gfx_shm_statistic;
extern int debugSHM;
extern int maxSHMSize;
#else

/******************************************************************************\
******************************* Private Functions ******************************
\******************************************************************************/

/*******************************************************************************
**
**  _Split
**
**  Split a node on the required byte boundary.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to the node to split.
**
**      gctSIZE_T Bytes
**          Number of bytes to keep in the node.
**
**  OUTPUT:
**
**      Nothing.
**
**  RETURNS:
**
**      gctBOOL
**          gcvTRUE if the node was split successfully, or gcvFALSE if there is an
**          error.
**
*/
static gctBOOL
_Split(
    IN gckOS Os,
    IN gcuVIDMEM_NODE_PTR Node,
    IN gctSIZE_T Bytes
    )
{
    gcuVIDMEM_NODE_PTR node;
    gctPOINTER pointer = gcvNULL;

    /* Make sure the byte boundary makes sense. */
    if ((Bytes <= 0) || (Bytes > Node->VidMem.bytes))
    {
        return gcvFALSE;
    }

    /* Allocate a new gcuVIDMEM_NODE object. */
    if (gcmIS_ERROR(gckOS_Allocate(Os,
                                   gcmSIZEOF(gcuVIDMEM_NODE),
                                   &pointer)))
    {
        /* Error. */
        return gcvFALSE;
    }

    node = pointer;

    /* Initialize gcuVIDMEM_NODE structure. */
    node->VidMem.offset    = Node->VidMem.offset + Bytes;
    node->VidMem.bytes     = Node->VidMem.bytes  - Bytes;
    node->VidMem.alignment = 0;
    node->VidMem.locked    = 0;
    node->VidMem.memory    = Node->VidMem.memory;
    node->VidMem.pool      = Node->VidMem.pool;
    node->VidMem.physical  = Node->VidMem.physical;
#ifdef __QNXNTO__
#if gcdUSE_VIDMEM_PER_PID
    gcmkASSERT(Node->VidMem.physical != 0);
    gcmkASSERT(Node->VidMem.logical != gcvNULL);
    node->VidMem.processID = Node->VidMem.processID;
    node->VidMem.physical  = Node->VidMem.physical + Bytes;
    node->VidMem.logical   = Node->VidMem.logical + Bytes;
#else
    node->VidMem.processID = 0;
    node->VidMem.logical   = gcvNULL;
#endif
#endif

    /* Insert node behind specified node. */
    node->VidMem.next = Node->VidMem.next;
    node->VidMem.prev = Node;
    Node->VidMem.next = node->VidMem.next->VidMem.prev = node;

    /* Insert free node behind specified node. */
    node->VidMem.nextFree = Node->VidMem.nextFree;
    node->VidMem.prevFree = Node;
    Node->VidMem.nextFree = node->VidMem.nextFree->VidMem.prevFree = node;

    /* Adjust size of specified node. */
    Node->VidMem.bytes = Bytes;

    /* Success. */
    return gcvTRUE;
}

/*******************************************************************************
**
**  _Merge
**
**  Merge two adjacent nodes together.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to the first of the two nodes to merge.
**
**  OUTPUT:
**
**      Nothing.
**
*/
static gceSTATUS
_Merge(
    IN gckOS Os,
    IN gcuVIDMEM_NODE_PTR Node
    )
{
    gcuVIDMEM_NODE_PTR node;
    gceSTATUS status;

    /* Save pointer to next node. */
    node = Node->VidMem.next;
#if gcdUSE_VIDMEM_PER_PID
    /* Check if the nodes are adjacent physically. */
    if ( ((Node->VidMem.physical + Node->VidMem.bytes) != node->VidMem.physical) ||
          ((Node->VidMem.logical + Node->VidMem.bytes) != node->VidMem.logical) )
    {
        /* Can't merge. */
        return gcvSTATUS_OK;
    }
#else

    /* This is a good time to make sure the heap is not corrupted. */
    if (Node->VidMem.offset + Node->VidMem.bytes != node->VidMem.offset)
    {
        /* Corrupted heap. */
        gcmkASSERT(
            Node->VidMem.offset + Node->VidMem.bytes == node->VidMem.offset);
        return gcvSTATUS_HEAP_CORRUPTED;
    }
#endif

    /* Adjust byte count. */
    Node->VidMem.bytes += node->VidMem.bytes;

    /* Unlink next node from linked list. */
    Node->VidMem.next     = node->VidMem.next;
    Node->VidMem.nextFree = node->VidMem.nextFree;

    Node->VidMem.next->VidMem.prev         =
    Node->VidMem.nextFree->VidMem.prevFree = Node;

    /* Free next node. */
    status = gcmkOS_SAFE_FREE(Os, node);
    return status;
}
#endif
//####end for marvell-bg2
/******************************************************************************\
******************************* gckVIDMEM API Code ******************************
\******************************************************************************/

/*******************************************************************************
**
**  gckVIDMEM_ConstructVirtual
**
**  Construct a new gcuVIDMEM_NODE union for virtual memory.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gctSIZE_T Bytes
**          Number of byte to allocate.
**
**  OUTPUT:
**
**      gcuVIDMEM_NODE_PTR * Node
**          Pointer to a variable that receives the gcuVIDMEM_NODE union pointer.
*/
gceSTATUS
gckVIDMEM_ConstructVirtual(
    IN gckKERNEL Kernel,
    IN gctBOOL Contiguous,
    IN gctSIZE_T Bytes,
    OUT gcuVIDMEM_NODE_PTR * Node
    )
{
    gckOS os;
    gceSTATUS status;
    gcuVIDMEM_NODE_PTR node = gcvNULL;
    gctPOINTER pointer = gcvNULL;
    gctINT i;

    gcmkHEADER_ARG("Kernel=0x%x Contiguous=%d Bytes=%lu", Kernel, Contiguous, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Node != gcvNULL);

    /* Extract the gckOS object pointer. */
    os = Kernel->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Allocate an gcuVIDMEM_NODE union. */
    gcmkONERROR(gckOS_Allocate(os, gcmSIZEOF(gcuVIDMEM_NODE), &pointer));

    node = pointer;

    /* Initialize gcuVIDMEM_NODE union for virtual memory. */
    node->Virtual.kernel        = Kernel;
    node->Virtual.contiguous    = Contiguous;
    node->Virtual.logical       = gcvNULL;

    for (i = 0; i < gcdCORE_COUNT; i++)
    {
        node->Virtual.lockeds[i]        = 0;
        node->Virtual.pageTables[i]     = gcvNULL;
        node->Virtual.lockKernels[i]    = gcvNULL;
    }

    node->Virtual.mutex         = gcvNULL;

    gcmkONERROR(gckOS_GetProcessID(&node->Virtual.processID));

#ifdef __QNXNTO__
    node->Virtual.next          = gcvNULL;
    node->Virtual.freePending   = gcvFALSE;
    for (i = 0; i < gcdCORE_COUNT; i++)
    {
        node->Virtual.unlockPendings[i] = gcvFALSE;
    }
#endif

    node->Virtual.freed         = gcvFALSE;

    gcmkONERROR(gckOS_ZeroMemory(&node->Virtual.sharedInfo, gcmSIZEOF(gcsVIDMEM_NODE_SHARED_INFO)));

    /* Create the mutex. */
    gcmkONERROR(
        gckOS_CreateMutex(os, &node->Virtual.mutex));

    /* Allocate the virtual memory. */
    gcmkONERROR(
        gckOS_AllocatePagedMemoryEx(os,
                                    node->Virtual.contiguous,
                                    node->Virtual.bytes = Bytes,
                                    &node->Virtual.physical));

#ifdef __QNXNTO__
    /* Register. */
    gckMMU_InsertNode(Kernel->mmu, node);
#endif

    /* Return pointer to the gcuVIDMEM_NODE union. */
    *Node = node;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                   "Created virtual node 0x%x for %u bytes @ 0x%x",
                   node, Bytes, node->Virtual.physical);

    /* Success. */
    gcmkFOOTER_ARG("*Node=0x%x", *Node);
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (node != gcvNULL)
    {
        if (node->Virtual.mutex != gcvNULL)
        {
            /* Destroy the mutex. */
            gcmkVERIFY_OK(gckOS_DeleteMutex(os, node->Virtual.mutex));
        }

        /* Free the structure. */
        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(os, node));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckVIDMEM_DestroyVirtual
**
**  Destroy an gcuVIDMEM_NODE union for virtual memory.
**
**  INPUT:
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a gcuVIDMEM_NODE union.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckVIDMEM_DestroyVirtual(
    IN gcuVIDMEM_NODE_PTR Node
    )
{
    gckOS os;
    gctINT i;

    gcmkHEADER_ARG("Node=0x%x", Node);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Node->Virtual.kernel, gcvOBJ_KERNEL);

    /* Extact the gckOS object pointer. */
    os = Node->Virtual.kernel->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

#ifdef __QNXNTO__
    /* Unregister. */
    gcmkVERIFY_OK(
            gckMMU_RemoveNode(Node->Virtual.kernel->mmu, Node));
#endif

    /* Delete the mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(os, Node->Virtual.mutex));

    for (i = 0; i < gcdCORE_COUNT; i++)
    {
        if (Node->Virtual.pageTables[i] != gcvNULL)
        {
#if gcdENABLE_VG
            if (i == gcvCORE_VG)
            {
                /* Free the pages. */
                gcmkVERIFY_OK(gckVGMMU_FreePages(Node->Virtual.lockKernels[i]->vg->mmu,
                                               Node->Virtual.pageTables[i],
                                               Node->Virtual.pageCount));
            }
            else
#endif
            {
                /* Free the pages. */
                gcmkVERIFY_OK(gckMMU_FreePages(Node->Virtual.lockKernels[i]->mmu,
                                               Node->Virtual.pageTables[i],
                                               Node->Virtual.pageCount));
            }
        }
    }

    /* Delete the gcuVIDMEM_NODE union. */
    gcmkVERIFY_OK(gcmkOS_SAFE_FREE(os, Node));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVIDMEM_Construct
**
**  Construct a new gckVIDMEM object.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 BaseAddress
**          Base address for the video memory heap.
**
**      gctSIZE_T Bytes
**          Number of bytes in the video memory heap.
**
**      gctSIZE_T Threshold
**          Minimum number of bytes beyond am allocation before the node is
**          split.  Can be used as a minimum alignment requirement.
**
**      gctSIZE_T BankSize
**          Number of bytes per physical memory bank.  Used by bank
**          optimization.
**
**  OUTPUT:
**
**      gckVIDMEM * Memory
**          Pointer to a variable that will hold the pointer to the gckVIDMEM
**          object.
*/
gceSTATUS
gckVIDMEM_Construct(
    IN gckOS Os,
    IN gctUINT32 BaseAddress,
    IN gctSIZE_T Bytes,
    IN gctSIZE_T Threshold,
    IN gctSIZE_T BankSize,
    OUT gckVIDMEM * Memory
    )
{
    gckVIDMEM memory = gcvNULL;
    gceSTATUS status;
    gcuVIDMEM_NODE_PTR node;
    gctINT i, banks = 0;
    gctPOINTER pointer = gcvNULL;

    gcmkHEADER_ARG("Os=0x%x BaseAddress=%08x Bytes=%lu Threshold=%lu "
                   "BankSize=%lu",
                   Os, BaseAddress, Bytes, Threshold, BankSize);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);

    /* Allocate the gckVIDMEM object. */
    gcmkONERROR(gckOS_Allocate(Os, gcmSIZEOF(struct _gckVIDMEM), &pointer));

    memory = pointer;

    /* Initialize the gckVIDMEM object. */
    memory->object.type = gcvOBJ_VIDMEM;
    memory->os          = Os;

    /* Set video memory heap information. */
    memory->baseAddress = BaseAddress;
    memory->bytes       = Bytes;
    memory->freeBytes   = Bytes;
    memory->threshold   = Threshold;
    memory->mutex       = gcvNULL;
#if gcdUSE_VIDMEM_PER_PID
    gcmkONERROR(gckOS_GetProcessID(&memory->pid));
#endif

    BaseAddress = 0;

    /* Walk all possible banks. */
    for (i = 0; i < gcmCOUNTOF(memory->sentinel); ++i)
    {
        gctSIZE_T bytes;

        if (BankSize == 0)
        {
            /* Use all bytes for the first bank. */
            bytes = Bytes;
        }
        else
        {
            /* Compute number of bytes for this bank. */
            bytes = gcmALIGN(BaseAddress + 1, BankSize) - BaseAddress;

            if (bytes > Bytes)
            {
                /* Make sure we don't exceed the total number of bytes. */
                bytes = Bytes;
            }
        }

        if (bytes == 0)
        {
            /* Mark heap is not used. */
            memory->sentinel[i].VidMem.next     =
            memory->sentinel[i].VidMem.prev     =
            memory->sentinel[i].VidMem.nextFree =
            memory->sentinel[i].VidMem.prevFree = gcvNULL;
            continue;
        }

        /* Allocate one gcuVIDMEM_NODE union. */
        gcmkONERROR(gckOS_Allocate(Os, gcmSIZEOF(gcuVIDMEM_NODE), &pointer));

        node = pointer;

        /* Initialize gcuVIDMEM_NODE union. */
        node->VidMem.memory    = memory;

        node->VidMem.next      =
        node->VidMem.prev      =
        node->VidMem.nextFree  =
        node->VidMem.prevFree  = &memory->sentinel[i];

        node->VidMem.offset    = BaseAddress;
        node->VidMem.bytes     = bytes;
        node->VidMem.alignment = 0;
        node->VidMem.physical  = 0;
        node->VidMem.pool      = gcvPOOL_UNKNOWN;

        node->VidMem.locked    = 0;

        gcmkONERROR(gckOS_ZeroMemory(&node->VidMem.sharedInfo, gcmSIZEOF(gcsVIDMEM_NODE_SHARED_INFO)));

#ifdef __QNXNTO__
#if gcdUSE_VIDMEM_PER_PID
        node->VidMem.processID = memory->pid;
        node->VidMem.physical  = memory->baseAddress + BaseAddress;
        gcmkONERROR(gckOS_GetLogicalAddressProcess(Os,
                    node->VidMem.processID,
                    node->VidMem.physical,
                    &node->VidMem.logical));
#else
        node->VidMem.processID = 0;
        node->VidMem.logical   = gcvNULL;
#endif
#endif

        /* Initialize the linked list of nodes. */
        memory->sentinel[i].VidMem.next     =
        memory->sentinel[i].VidMem.prev     =
        memory->sentinel[i].VidMem.nextFree =
        memory->sentinel[i].VidMem.prevFree = node;

        /* Mark sentinel. */
        memory->sentinel[i].VidMem.bytes = 0;

        /* Adjust address for next bank. */
        BaseAddress += bytes;
        Bytes       -= bytes;
        banks       ++;
    }

    /* Assign all the bank mappings. */
    memory->mapping[gcvSURF_RENDER_TARGET]      = banks - 1;
    memory->mapping[gcvSURF_BITMAP]             = banks - 1;
    if (banks > 1) --banks;
    memory->mapping[gcvSURF_DEPTH]              = banks - 1;
    memory->mapping[gcvSURF_HIERARCHICAL_DEPTH] = banks - 1;
    if (banks > 1) --banks;
    memory->mapping[gcvSURF_TEXTURE]            = banks - 1;
    if (banks > 1) --banks;
    memory->mapping[gcvSURF_VERTEX]             = banks - 1;
    if (banks > 1) --banks;
    memory->mapping[gcvSURF_INDEX]              = banks - 1;
    if (banks > 1) --banks;
    memory->mapping[gcvSURF_TILE_STATUS]        = banks - 1;
    if (banks > 1) --banks;
    memory->mapping[gcvSURF_TYPE_UNKNOWN]       = 0;

#if gcdENABLE_VG
    memory->mapping[gcvSURF_IMAGE]   = 0;
    memory->mapping[gcvSURF_MASK]    = 0;
    memory->mapping[gcvSURF_SCISSOR] = 0;
#endif

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                  "[GALCORE] INDEX:         bank %d",
                  memory->mapping[gcvSURF_INDEX]);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                  "[GALCORE] VERTEX:        bank %d",
                  memory->mapping[gcvSURF_VERTEX]);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                  "[GALCORE] TEXTURE:       bank %d",
                  memory->mapping[gcvSURF_TEXTURE]);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                  "[GALCORE] RENDER_TARGET: bank %d",
                  memory->mapping[gcvSURF_RENDER_TARGET]);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                  "[GALCORE] DEPTH:         bank %d",
                  memory->mapping[gcvSURF_DEPTH]);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                  "[GALCORE] TILE_STATUS:   bank %d",
                  memory->mapping[gcvSURF_TILE_STATUS]);

    /* Allocate the mutex. */
    gcmkONERROR(gckOS_CreateMutex(Os, &memory->mutex));

    /* Return pointer to the gckVIDMEM object. */
    *Memory = memory;

    /* Success. */
    gcmkFOOTER_ARG("*Memory=0x%x", *Memory);
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (memory != gcvNULL)
    {
        if (memory->mutex != gcvNULL)
        {
            /* Delete the mutex. */
            gcmkVERIFY_OK(gckOS_DeleteMutex(Os, memory->mutex));
        }

        for (i = 0; i < banks; ++i)
        {
            /* Free the heap. */
            gcmkASSERT(memory->sentinel[i].VidMem.next != gcvNULL);
            gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Os, memory->sentinel[i].VidMem.next));
        }

        /* Free the object. */
        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Os, memory));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckVIDMEM_Destroy
**
**  Destroy an gckVIDMEM object.
**
**  INPUT:
**
**      gckVIDMEM Memory
**          Pointer to an gckVIDMEM object to destroy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckVIDMEM_Destroy(
    IN gckVIDMEM Memory
    )
{
    gcuVIDMEM_NODE_PTR node, next;
    gctINT i;

    gcmkHEADER_ARG("Memory=0x%x", Memory);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Memory, gcvOBJ_VIDMEM);

    /* Walk all sentinels. */
    for (i = 0; i < gcmCOUNTOF(Memory->sentinel); ++i)
    {
        /* Bail out of the heap is not used. */
        if (Memory->sentinel[i].VidMem.next == gcvNULL)
        {
            break;
        }

        /* Walk all the nodes until we reach the sentinel. */
        for (node = Memory->sentinel[i].VidMem.next;
             node->VidMem.bytes != 0;
             node = next)
        {
            /* Save pointer to the next node. */
            next = node->VidMem.next;

            /* Free the node. */
            gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Memory->os, node));
        }
    }

    /* Free the mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Memory->os, Memory->mutex));

    /* Mark the object as unknown. */
    Memory->object.type = gcvOBJ_UNKNOWN;

    /* Free the gckVIDMEM object. */
    gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Memory->os, Memory));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVIDMEM_Allocate
**
**  Allocate rectangular memory from the gckVIDMEM object.
**
**  INPUT:
**
**      gckVIDMEM Memory
**          Pointer to an gckVIDMEM object.
**
**      gctUINT Width
**          Width of rectangle to allocate.  Make sure the width is properly
**          aligned.
**
**      gctUINT Height
**          Height of rectangle to allocate.  Make sure the height is properly
**          aligned.
**
**      gctUINT Depth
**          Depth of rectangle to allocate.  This equals to the number of
**          rectangles to allocate contiguously (i.e., for cubic maps and volume
**          textures).
**
**      gctUINT BytesPerPixel
**          Number of bytes per pixel.
**
**      gctUINT32 Alignment
**          Byte alignment for allocation.
**
**      gceSURF_TYPE Type
**          Type of surface to allocate (use by bank optimization).
**
**  OUTPUT:
**
**      gcuVIDMEM_NODE_PTR * Node
**          Pointer to a variable that will hold the allocated memory node.
*/
gceSTATUS
gckVIDMEM_Allocate(
    IN gckVIDMEM Memory,
    IN gctUINT Width,
    IN gctUINT Height,
    IN gctUINT Depth,
    IN gctUINT BytesPerPixel,
    IN gctUINT32 Alignment,
    IN gceSURF_TYPE Type,
    OUT gcuVIDMEM_NODE_PTR * Node
    )
{
    gctSIZE_T bytes;
    gceSTATUS status;

    gcmkHEADER_ARG("Memory=0x%x Width=%u Height=%u Depth=%u BytesPerPixel=%u "
                   "Alignment=%u Type=%d",
                   Memory, Width, Height, Depth, BytesPerPixel, Alignment,
                   Type);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Memory, gcvOBJ_VIDMEM);
    gcmkVERIFY_ARGUMENT(Width > 0);
    gcmkVERIFY_ARGUMENT(Height > 0);
    gcmkVERIFY_ARGUMENT(Depth > 0);
    gcmkVERIFY_ARGUMENT(BytesPerPixel > 0);
    gcmkVERIFY_ARGUMENT(Node != gcvNULL);

    /* Compute linear size. */
    bytes = Width * Height * Depth * BytesPerPixel;

    /* Allocate through linear function. */
    gcmkONERROR(
        gckVIDMEM_AllocateLinear(Memory, bytes, Alignment, Type, Node));

    /* Success. */
    gcmkFOOTER_ARG("*Node=0x%x", *Node);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if gcdENABLE_BANK_ALIGNMENT

#if !gcdBANK_BIT_START
#error gcdBANK_BIT_START not defined.
#endif

#if !gcdBANK_BIT_END
#error gcdBANK_BIT_END not defined.
#endif
/*******************************************************************************
**  _GetSurfaceBankAlignment
**
**  Return the required offset alignment required to the make BaseAddress
**  aligned properly.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to gcoOS object.
**
**      gceSURF_TYPE Type
**          Type of allocation.
**
**      gctUINT32 BaseAddress
**          Base address of current video memory node.
**
**  OUTPUT:
**
**      gctUINT32_PTR AlignmentOffset
**          Pointer to a variable that will hold the number of bytes to skip in
**          the current video memory node in order to make the alignment bank
**          aligned.
*/
static gceSTATUS
_GetSurfaceBankAlignment(
    IN gceSURF_TYPE Type,
    IN gctUINT32 BaseAddress,
    OUT gctUINT32_PTR AlignmentOffset
    )
{
    gctUINT32 bank;
    /* To retrieve the bank. */
    static const gctUINT32 bankMask = (0xFFFFFFFF << gcdBANK_BIT_START)
                                    ^ (0xFFFFFFFF << (gcdBANK_BIT_END + 1));

    /* To retrieve the bank and all the lower bytes. */
    static const gctUINT32 byteMask = ~(0xFFFFFFFF << (gcdBANK_BIT_END + 1));

    gcmkHEADER_ARG("Type=%d BaseAddress=0x%x ", Type, BaseAddress);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(AlignmentOffset != gcvNULL);

    switch (Type)
    {
    case gcvSURF_RENDER_TARGET:
        bank = (BaseAddress & bankMask) >> (gcdBANK_BIT_START);

        /* Align to the first bank. */
        *AlignmentOffset = (bank == 0) ?
            0 :
            ((1 << (gcdBANK_BIT_END + 1)) + 0) -  (BaseAddress & byteMask);
        break;

    case gcvSURF_DEPTH:
        bank = (BaseAddress & bankMask) >> (gcdBANK_BIT_START);

        /* Align to the third bank. */
        *AlignmentOffset = (bank == 2) ?
            0 :
            ((1 << (gcdBANK_BIT_END + 1)) + (2 << gcdBANK_BIT_START)) -  (BaseAddress & byteMask);

        /* Add a channel offset at the channel bit. */
        *AlignmentOffset += (1 << gcdBANK_CHANNEL_BIT);
        break;

    default:
        /* no alignment needed. */
        *AlignmentOffset = 0;
    }

    /* Return the status. */
    gcmkFOOTER_ARG("*AlignmentOffset=%u", *AlignmentOffset);
    return gcvSTATUS_OK;
}
#endif

static gcuVIDMEM_NODE_PTR
_FindNode(
    IN gckVIDMEM Memory,
    IN gctINT Bank,
    IN gctSIZE_T Bytes,
    IN gceSURF_TYPE Type,
    IN OUT gctUINT32_PTR Alignment
    )
{
    gcuVIDMEM_NODE_PTR node;
    gctUINT32 alignment;
//####modified for marvell-bg2
#if USE_GALOIS_SHM
    gcuVIDMEM_NODE_PTR Node;
    size_t shm_node;
#else
#if gcdENABLE_BANK_ALIGNMENT
    gctUINT32 bankAlignment;
    gceSTATUS status;
#endif

    if (Memory->sentinel[Bank].VidMem.nextFree == gcvNULL)
    {
        /* No free nodes left. */
        return gcvNULL;
    }

#if gcdENABLE_BANK_ALIGNMENT
    /* Walk all free nodes until we have one that is big enough or we have
    ** reached the sentinel. */
    for (node = Memory->sentinel[Bank].VidMem.nextFree;
         node->VidMem.bytes != 0;
         node = node->VidMem.nextFree)
    {
        gcmkONERROR(_GetSurfaceBankAlignment(
            Type,
            node->VidMem.memory->baseAddress + node->VidMem.offset,
            &bankAlignment));

        bankAlignment = gcmALIGN(bankAlignment, *Alignment);

        /* Compute number of bytes to skip for alignment. */
        alignment = (*Alignment == 0)
                  ? 0
                  : (*Alignment - (node->VidMem.offset % *Alignment));

        if (alignment == *Alignment)
        {
            /* Node is already aligned. */
            alignment = 0;
        }

        if (node->VidMem.bytes >= Bytes + alignment + bankAlignment)
        {
            /* This node is big enough. */
            *Alignment = alignment + bankAlignment;
            return node;
        }
    }
#endif
#endif /*!USE_GALOIS_SHM*/
//####end for marvell-bg2

//####modified for marvell-bg2
#if !USE_GALOIS_SHM
    /* Walk all free nodes until we have one that is big enough or we have
       reached the sentinel. */
    for (node = Memory->sentinel[Bank].VidMem.nextFree;
         node->VidMem.bytes != 0;
         node = node->VidMem.nextFree)
    {

        gctINT modulo = gckMATH_ModuloInt(node->VidMem.offset, *Alignment);

        /* Compute number of bytes to skip for alignment. */
        alignment = (*Alignment == 0) ? 0 : (*Alignment - modulo);

        if (alignment == *Alignment)
        {
            /* Node is already aligned. */
            alignment = 0;
        }

        if (node->VidMem.bytes >= Bytes + alignment)
        {
            /* This node is big enough. */
            *Alignment = alignment;
            return node;
        }
    }
#else /*~ !USE_GALOIS_SHM*/
    Node = &Memory->sentinel[Bank];

    alignment = *Alignment;
    //if (galPrint)
    //    printk(KERN_INFO "%s: %xh:%xh.\n",__FUNCTION__, (unsigned) Bytes, alignment);

    if ((gfx_shm_statistic.current_malloc+ Bytes) >= maxSHMSize){
        shm_node = ERROR_SHM_MALLOC_FAILED;
    }
    else
        shm_node = MV_SHM_Malloc(Bytes, alignment);

    if (shm_node == ERROR_SHM_MALLOC_FAILED){
        MV_SHM_MemInfo_t info;
        MV_SHM_GetCacheMemInfo(&info);
        gckOS_Print("shm malloc failed: Bytes=%u,node=0x%x, total=%d, free=%d, usedmem=%d, maxfree=%d, maxused=%d\n",
            Bytes, node, info.m_totalmem, info.m_freemem, info.m_usedmem, info.m_max_freeblock, info.m_max_usedblock);
        gckOS_Print("gfx shm usage:total_malloc=%u,current_malloc=%u, total_free=%u, malloc_count=%u, free_count=%u\n",
            gfx_shm_statistic.total_malloc, gfx_shm_statistic.current_malloc,gfx_shm_statistic.total_free,
            gfx_shm_statistic.malloc_count, gfx_shm_statistic.free_count);
        return gcvNULL;
    }

    // Invalidate the cache and clear up the allocated shared memory
    memset(MV_SHM_GetCacheVirtAddr(shm_node), 0, Bytes);
    //MV_SHM_CleanCache(shm_node, Bytes);
    dmac_map_area(MV_SHM_GetCacheVirtAddr(shm_node), Bytes, DMA_TO_DEVICE);
    outer_clean_range((unsigned long) MV_SHM_GetCachePhysAddr(shm_node), (unsigned long) MV_SHM_GetCachePhysAddr(shm_node)+ Bytes);
    //memset(MV_SHM_GetCacheVirtAddr(shm_node), 0, Bytes);

    
    /* Allocate a new gcuVIDMEM_NODE object. */
    if (gcmIS_ERROR(gckOS_Allocate(Memory->os,
                   gcmSIZEOF(gcuVIDMEM_NODE), (gctPOINTER *) &node)))
    {
        /* Error. */
        //if (galPrint)
        //    printk(KERN_INFO "%s: Node? shm=%xh.\n",__FUNCTION__, shm_node);
        MV_SHM_Free(shm_node);
        return gcvNULL;
    }

	memset(&node->VidMem, 0, sizeof(node->VidMem));

    /* Initialize gcuVIDMEM_NODE structure. */
    node->VidMem.offset    = (gctUINT32) shm_node;
    node->VidMem.bytes     = Bytes;
    node->VidMem.alignment = 0;
    node->VidMem.locked    = 0;
    node->VidMem.pool      = Node->VidMem.pool;
    node->VidMem.physical  = (size_t) MV_SHM_GetCachePhysAddr(shm_node);
    node->VidMem.nextFree  = 0;
    node->VidMem.prevFree  = 0;
    node->VidMem.memory    = Memory;
#ifdef __QNXNTO__
    node->VidMem.logical   = MV_SHM_GetCacheVirtAddr(shm_node);
#endif

    /* Insert node behind specified node. */
    node->VidMem.next = Node->VidMem.next;
    node->VidMem.prev = Node;
    Node->VidMem.next = node->VidMem.next->VidMem.prev = node;

    gfx_shm_statistic.total_malloc+=Bytes;
    gfx_shm_statistic.current_malloc +=Bytes;
    gfx_shm_statistic.malloc_count +=1;
    if(debugSHM)
    {
        MV_SHM_MemInfo_t info;
        MV_SHM_GetCacheMemInfo(&info);
        gckOS_Print("shm malloc suc: Bytes=%u,node=0x%x, offset=0x%x,total=%d, free=%d, usedmem=%d, maxfree=%d, maxused=%d\n",
            Bytes, node, node->VidMem.offset, info.m_totalmem, info.m_freemem, info.m_usedmem, info.m_max_freeblock, info.m_max_usedblock);

        gckOS_Print("gfx shm usage:total_malloc=%u,current_malloc=%u, total_free=%u, malloc_count=%u, free_count=%u\n",
	        gfx_shm_statistic.total_malloc, gfx_shm_statistic.current_malloc,gfx_shm_statistic.total_free,
            gfx_shm_statistic.malloc_count, gfx_shm_statistic.free_count);
    }

    //memset(MV_SHM_GetCacheVirtAddr(shm_node),0,Bytes);
    /* Success. */
    return node;
#endif /*!USE_GALOIS_SHM*/
//####end for marvell-bg2

#if gcdENABLE_BANK_ALIGNMENT
OnError:
#endif
    /* Not enough memory. */
    return gcvNULL;
}

/*******************************************************************************
**
**  gckVIDMEM_AllocateLinear
**
**  Allocate linear memory from the gckVIDMEM object.
**
**  INPUT:
**
**      gckVIDMEM Memory
**          Pointer to an gckVIDMEM object.
**
**      gctSIZE_T Bytes
**          Number of bytes to allocate.
**
**      gctUINT32 Alignment
**          Byte alignment for allocation.
**
**      gceSURF_TYPE Type
**          Type of surface to allocate (use by bank optimization).
**
**  OUTPUT:
**
**      gcuVIDMEM_NODE_PTR * Node
**          Pointer to a variable that will hold the allocated memory node.
*/
gceSTATUS
gckVIDMEM_AllocateLinear(
    IN gckVIDMEM Memory,
    IN gctSIZE_T Bytes,
    IN gctUINT32 Alignment,
    IN gceSURF_TYPE Type,
    OUT gcuVIDMEM_NODE_PTR * Node
    )
{
    gceSTATUS status;
    gcuVIDMEM_NODE_PTR node;
    gctUINT32 alignment;
//####modified for marvell-bg2
#if !USE_GALOIS_SHM
    gctINT bank, i;
#else
	gctINT bank;
#endif
//####end for marvell-bg2
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Memory=0x%x Bytes=%lu Alignment=%u Type=%d",
                   Memory, Bytes, Alignment, Type);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Memory, gcvOBJ_VIDMEM);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Node != gcvNULL);
    gcmkVERIFY_ARGUMENT(Type < gcvSURF_NUM_TYPES);

    /* Acquire the mutex. */
    gcmkONERROR(gckOS_AcquireMutex(Memory->os, Memory->mutex, gcvINFINITE));

    acquired = gcvTRUE;
#if !gcdUSE_VIDMEM_PER_PID

//####modified for marvell-bg
#if !USE_GALOIS_SHM

    if (Bytes > Memory->freeBytes)
    {
        /* Not enough memory. */
        status = gcvSTATUS_OUT_OF_MEMORY;
        goto OnError;
    }
#endif
//####end for marvell-bg2

#endif

#if gcdSMALL_BLOCK_SIZE
    if ((Memory->freeBytes < (Memory->bytes/gcdRATIO_FOR_SMALL_MEMORY))
    &&  (Bytes >= gcdSMALL_BLOCK_SIZE)
    )
    {
        /* The left memory is for small memory.*/
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }
#endif

    /* Find the default bank for this surface type. */
    gcmkASSERT((gctINT) Type < gcmCOUNTOF(Memory->mapping));
    bank      = Memory->mapping[Type];
    alignment = Alignment;

#if gcdUSE_VIDMEM_PER_PID
//####modified for marvell-bg2
#if !USE_GALOIS_SHM
    if (Bytes <= Memory->freeBytes)
    {
#endif
//####end for marvell-bg2
#endif
    /* Find a free node in the default bank. */
    node = _FindNode(Memory, bank, Bytes, Type, &alignment);

//####modified for marvell-bg2
#if !USE_GALOIS_SHM
    /* Out of memory? */
    if (node == gcvNULL)
    {
        /* Walk all lower banks. */
        for (i = bank - 1; i >= 0; --i)
        {
            /* Find a free node inside the current bank. */
            node = _FindNode(Memory, i, Bytes, Type, &alignment);
            if (node != gcvNULL)
            {
                break;
            }
        }
    }

    if (node == gcvNULL)
    {
        /* Walk all upper banks. */
        for (i = bank + 1; i < gcmCOUNTOF(Memory->sentinel); ++i)
        {
            if (Memory->sentinel[i].VidMem.nextFree == gcvNULL)
            {
                /* Abort when we reach unused banks. */
                break;
            }

            /* Find a free node inside the current bank. */
            node = _FindNode(Memory, i, Bytes, Type, &alignment);
            if (node != gcvNULL)
            {
                break;
            }
        }
    }
#endif
//####end for marvell-bg2

#if gcdUSE_VIDMEM_PER_PID
//####modified for marvell-bg2
#if !USE_GALOIS_SHM
    }
#endif
//####end for marvell-bg2
#endif

//####modified for marvell-bg2
#if !USE_GALOIS_SHM
    if (node == gcvNULL)
    {
        /* Out of memory. */
#if gcdUSE_VIDMEM_PER_PID
        /* Allocate more memory from shared pool. */
        gctSIZE_T bytes;
        gctPHYS_ADDR physical_temp;
        gctUINT32 physical;
        gctPOINTER logical;

        bytes = gcmALIGN(Bytes, gcdUSE_VIDMEM_PER_PID_SIZE);

        gcmkONERROR(gckOS_AllocateContiguous(Memory->os,
                gcvTRUE,
                &bytes,
                &physical_temp,
                &logical));

        /* physical address is returned as 0 for user space. workaround. */
        if (physical_temp == gcvNULL)
        {
        gcmkONERROR(gckOS_GetPhysicalAddress(Memory->os, logical, &physical));
        }

        /* Allocate one gcuVIDMEM_NODE union. */
        gcmkONERROR(
            gckOS_Allocate(Memory->os,
                           gcmSIZEOF(gcuVIDMEM_NODE),
                           (gctPOINTER *) &node));

        /* Initialize gcuVIDMEM_NODE union. */
        node->VidMem.memory    = Memory;

        node->VidMem.offset    = 0;
        node->VidMem.bytes     = bytes;
        node->VidMem.alignment = 0;
        node->VidMem.physical  = physical;
        node->VidMem.pool      = gcvPOOL_UNKNOWN;

        node->VidMem.locked    = 0;

#ifdef __QNXNTO__
        gcmkONERROR(gckOS_GetProcessID(&node->VidMem.processID));
        node->VidMem.logical   = logical;
        gcmkASSERT(logical != gcvNULL);
#endif

        /* Insert node behind sentinel node. */
        node->VidMem.next = Memory->sentinel[bank].VidMem.next;
        node->VidMem.prev = &Memory->sentinel[bank];
        Memory->sentinel[bank].VidMem.next = node->VidMem.next->VidMem.prev = node;

        /* Insert free node behind sentinel node. */
        node->VidMem.nextFree = Memory->sentinel[bank].VidMem.nextFree;
        node->VidMem.prevFree = &Memory->sentinel[bank];
        Memory->sentinel[bank].VidMem.nextFree = node->VidMem.nextFree->VidMem.prevFree = node;

        Memory->freeBytes += bytes;
#else
        status = gcvSTATUS_OUT_OF_MEMORY;
        goto OnError;
#endif
    }

    /* Do we have an alignment? */
    if (alignment > 0)
    {
        /* Split the node so it is aligned. */
        if (_Split(Memory->os, node, alignment))
        {
            /* Successful split, move to aligned node. */
            node = node->VidMem.next;

            /* Remove alignment. */
            alignment = 0;
        }
    }

    /* Do we have enough memory after the allocation to split it? */
    if (node->VidMem.bytes - Bytes > Memory->threshold)
    {
        /* Adjust the node size. */
        _Split(Memory->os, node, Bytes);
    }

    /* Remove the node from the free list. */
    node->VidMem.prevFree->VidMem.nextFree = node->VidMem.nextFree;
    node->VidMem.nextFree->VidMem.prevFree = node->VidMem.prevFree;
    node->VidMem.nextFree                  =
    node->VidMem.prevFree                  = gcvNULL;

    /* Fill in the information. */
    node->VidMem.alignment = alignment;
    node->VidMem.memory    = Memory;
#ifdef __QNXNTO__
#if !gcdUSE_VIDMEM_PER_PID
    node->VidMem.logical   = gcvNULL;
    gcmkONERROR(gckOS_GetProcessID(&node->VidMem.processID));
#else
    gcmkASSERT(node->VidMem.logical != gcvNULL);
#endif
#endif

    /* Adjust the number of free bytes. */
    Memory->freeBytes -= node->VidMem.bytes;

    node->VidMem.freePending = gcvFALSE;

#if gcdDYNAMIC_MAP_RESERVED_MEMORY && gcdENABLE_VG
    node->VidMem.kernelVirtual = gcvNULL;
#endif

#else /*~ !USE_GALOIS_SHM*/
    if (node == gcvNULL)
    {
        /* Out of memory. */
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }
	gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                   "Allocated2 %u bytes @ 0x%x [0x%08X]",
                   node->VidMem.bytes, node, node->VidMem.offset);
#endif /*!USE_GALOIS_SHM*/
//####end for marvell-bg2

    /* Release the mutex. */
    gcmkVERIFY_OK(gckOS_ReleaseMutex(Memory->os, Memory->mutex));

    /* Return the pointer to the node. */
    *Node = node;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                   "Allocated %u bytes @ 0x%x [0x%08X]",
                   node->VidMem.bytes, node, node->VidMem.offset);

    /* Success. */
    gcmkFOOTER_ARG("*Node=0x%x", *Node);
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
     /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Memory->os, Memory->mutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

//####modified for marvell-bg2
#if USE_GALOIS_SHM
gceSTATUS gckVIDMEM_PreFree(
    IN gcuVIDMEM_NODE_PTR Node
    )
{
    gceSTATUS status = gcvSTATUS_OK;

    if ((Node == gcvNULL)
    ||  (Node->VidMem.memory == gcvNULL)
    ||(Node->VidMem.memory->object.type != gcvOBJ_VIDMEM)
    )
    {
        /* Invalid object. */
        status = gcvSTATUS_INVALID_OBJECT;
    }
    else{
        Node->VidMem.offset = ERROR_SHM_MALLOC_FAILED;
    }

    return status;
}
#endif
//####end for marvell-bg2

/*******************************************************************************
**
**  gckVIDMEM_Free
**
**  Free an allocated video memory node.
**
**  INPUT:
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a gcuVIDMEM_NODE object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckVIDMEM_Free(
    IN gcuVIDMEM_NODE_PTR Node
    )
{
    gceSTATUS status;
    gckKERNEL kernel = gcvNULL;
    gckVIDMEM memory = gcvNULL;
    gcuVIDMEM_NODE_PTR node;
    gctBOOL mutexAcquired = gcvFALSE;
    gckOS os = gcvFALSE;
    gctBOOL acquired = gcvFALSE;
    gctINT32 i, totalLocked;

    gcmkHEADER_ARG("Node=0x%x", Node);

    /* Verify the arguments. */
    if ((Node == gcvNULL)
    ||  (Node->VidMem.memory == gcvNULL)
    )
    {
        /* Invalid object. */
        gcmkONERROR(gcvSTATUS_INVALID_OBJECT);
    }

    /**************************** Video Memory ********************************/

    if (Node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
    {
        if (Node->VidMem.locked > 0)
        {
            /* Client still has a lock, defer free op 'till when lock reaches 0. */
            Node->VidMem.freePending = gcvTRUE;

            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                           "Node 0x%x is locked (%d)... deferring free.",
                           Node, Node->VidMem.locked);

            gcmkFOOTER_NO();
            return gcvSTATUS_OK;
        }

        /* Extract pointer to gckVIDMEM object owning the node. */
        memory = Node->VidMem.memory;

//####modified for marvell-bg2
#if !USE_GALOIS_SHM
        /* Acquire the mutex. */
        gcmkONERROR(
            gckOS_AcquireMutex(memory->os, memory->mutex, gcvINFINITE));

        mutexAcquired = gcvTRUE;

#ifdef __QNXNTO__
#if !gcdUSE_VIDMEM_PER_PID
        /* Reset. */
        Node->VidMem.processID = 0;
        Node->VidMem.logical = gcvNULL;
#endif

        /* Don't try to re-free an already freed node. */
        if ((Node->VidMem.nextFree == gcvNULL)
        &&  (Node->VidMem.prevFree == gcvNULL)
        )
#endif
        {
#if gcdDYNAMIC_MAP_RESERVED_MEMORY && gcdENABLE_VG
            if (Node->VidMem.kernelVirtual)
            {
                gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                        "%s(%d) Unmap %x from kernel space.",
                        __FUNCTION__, __LINE__,
                        Node->VidMem.kernelVirtual);

                gcmkVERIFY_OK(
                    gckOS_UnmapPhysical(memory->os,
                                        Node->VidMem.kernelVirtual,
                                        Node->VidMem.bytes));

                Node->VidMem.kernelVirtual = gcvNULL;
            }
#endif
            /* Update the number of free bytes. */
            memory->freeBytes += Node->VidMem.bytes;

            /* Find the next free node. */
            for (node = Node->VidMem.next;
                 node != gcvNULL && node->VidMem.nextFree == gcvNULL;
                 node = node->VidMem.next) ;

            /* Insert this node in the free list. */
            Node->VidMem.nextFree = node;
            Node->VidMem.prevFree = node->VidMem.prevFree;

            Node->VidMem.prevFree->VidMem.nextFree =
            node->VidMem.prevFree                  = Node;

            /* Is the next node a free node and not the sentinel? */
            if ((Node->VidMem.next == Node->VidMem.nextFree)
            &&  (Node->VidMem.next->VidMem.bytes != 0)
            )
            {
                /* Merge this node with the next node. */
                gcmkONERROR(_Merge(memory->os, node = Node));
                gcmkASSERT(node->VidMem.nextFree != node);
                gcmkASSERT(node->VidMem.prevFree != node);
            }

            /* Is the previous node a free node and not the sentinel? */
            if ((Node->VidMem.prev == Node->VidMem.prevFree)
            &&  (Node->VidMem.prev->VidMem.bytes != 0)
            )
            {
                /* Merge this node with the previous node. */
                gcmkONERROR(_Merge(memory->os, node = Node->VidMem.prev));
                gcmkASSERT(node->VidMem.nextFree != node);
                gcmkASSERT(node->VidMem.prevFree != node);
            }
        }

        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(memory->os, memory->mutex));

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                       "Node 0x%x is freed.",
                       Node);
#else
        {
            size_t shm_node;
            shm_node = (size_t) Node->VidMem.offset;

            /* Acquire the mutex. */
            gcmkONERROR(
                gckOS_AcquireMutex(memory->os, memory->mutex, gcvINFINITE));
            mutexAcquired = gcvTRUE;

            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                "%s: free bytes=%d, phy=0x%x, virt=0x%x, offset=0x%x, node=0x%x",
                 __FUNCTION__, Node->VidMem.bytes,Node->VidMem.physical, Node->VidMem.memory, Node->VidMem.offset, Node);
            if (ERROR_SHM_MALLOC_FAILED != shm_node)
                MV_SHM_Free(shm_node);

            gfx_shm_statistic.current_malloc -= Node->VidMem.bytes;
            gfx_shm_statistic.total_free += Node->VidMem.bytes;
            gfx_shm_statistic.free_count += 1;
            if(debugSHM)
            {
                MV_SHM_MemInfo_t info;
                MV_SHM_GetCacheMemInfo(&info);
                gckOS_Print("shm free suc: Bytes=%u,node=0x%x, offset=0x%x,total=%d, free=%d, usedmem=%d, maxfree=%d, maxused=%d\n",
                    Node->VidMem.bytes, Node, Node->VidMem.offset, info.m_totalmem, info.m_freemem, info.m_usedmem, info.m_max_freeblock, info.m_max_usedblock);

                gckOS_Print("gfx shm usage:total_malloc=%u,current_malloc=%u, total_free=%u, malloc_count=%u, free_count=%u\n",
                    gfx_shm_statistic.total_malloc, gfx_shm_statistic.current_malloc,gfx_shm_statistic.total_free,
                    gfx_shm_statistic.malloc_count, gfx_shm_statistic.free_count);
            }

            node = Node->VidMem.next;
            if (node)
                node->VidMem.prev = Node->VidMem.prev;
            if (Node->VidMem.prev)
                Node->VidMem.prev->VidMem.next = node;
            gckOS_Free(memory->os, Node);

            /* Release the mutex. */
            gcmkVERIFY_OK(gckOS_ReleaseMutex(memory->os, memory->mutex));
        }
#endif /*!USE_GALOIS_SHM*/
//####end for marvell-bg2

        /* Success. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    /*************************** Virtual Memory *******************************/

    /* Get gckKERNEL object. */
    kernel = Node->Virtual.kernel;

    /* Verify the gckKERNEL object pointer. */
    gcmkVERIFY_OBJECT(kernel, gcvOBJ_KERNEL);

    /* Get the gckOS object pointer. */
    os = kernel->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Grab the mutex. */
    gcmkONERROR(
        gckOS_AcquireMutex(os, Node->Virtual.mutex, gcvINFINITE));

    acquired = gcvTRUE;

    for (i = 0, totalLocked = 0; i < gcdCORE_COUNT; i++)
    {
        totalLocked += Node->Virtual.lockeds[i];
    }

    if (totalLocked > 0)
    {
        gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_VIDMEM,
                       "gckVIDMEM_Free: Virtual node 0x%x is locked (%d)",
                       Node, totalLocked);

        /* Set Flag */
        Node->Virtual.freed = gcvTRUE;

        gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->Virtual.mutex));
    }
    else
    {
        /* Free the virtual memory. */
        gcmkVERIFY_OK(gckOS_FreePagedMemory(kernel->os,
                                            Node->Virtual.physical,
                                            Node->Virtual.bytes));

        gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->Virtual.mutex));

        /* Destroy the gcuVIDMEM_NODE union. */
        gcmkVERIFY_OK(gckVIDMEM_DestroyVirtual(Node));
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (mutexAcquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(
            memory->os, memory->mutex
            ));
    }

    if (acquired)
    {
       gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->Virtual.mutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}


#ifdef __QNXNTO__
/*******************************************************************************
**
**  gcoVIDMEM_FreeHandleMemory
**
**  Free all allocated video memory nodes for a handle.
**
**  INPUT:
**
**      gcoVIDMEM Memory
**          Pointer to an gcoVIDMEM object..
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckVIDMEM_FreeHandleMemory(
    IN gckKERNEL Kernel,
    IN gckVIDMEM Memory,
    IN gctUINT32 Pid
    )
{
    gceSTATUS status;
    gctBOOL mutex = gcvFALSE;
    gcuVIDMEM_NODE_PTR node;
    gctINT i;
    gctUINT32 nodeCount = 0, byteCount = 0;
    gctBOOL again;

    gcmkHEADER_ARG("Kernel=0x%x, Memory=0x%x Pid=0x%u", Kernel, Memory, Pid);

    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_OBJECT(Memory, gcvOBJ_VIDMEM);

    gcmkONERROR(gckOS_AcquireMutex(Memory->os, Memory->mutex, gcvINFINITE));
    mutex = gcvTRUE;

    /* Walk all sentinels. */
    for (i = 0; i < gcmCOUNTOF(Memory->sentinel); ++i)
    {
        /* Bail out of the heap if it is not used. */
        if (Memory->sentinel[i].VidMem.next == gcvNULL)
        {
            break;
        }

        do
        {
            again = gcvFALSE;

            /* Walk all the nodes until we reach the sentinel. */
            for (node = Memory->sentinel[i].VidMem.next;
                 node->VidMem.bytes != 0;
                 node = node->VidMem.next)
            {
                /* Free the node if it was allocated by Handle. */
                if (node->VidMem.processID == Pid)
                {
                    /* Unlock video memory. */
                    while (node->VidMem.locked > 0)
                    {
                        gckVIDMEM_Unlock(Kernel, node, gcvSURF_TYPE_UNKNOWN, gcvNULL);
                    }

                    nodeCount++;
                    byteCount += node->VidMem.bytes;

                    /* Free video memory. */
                    gcmkVERIFY_OK(gckVIDMEM_Free(node));

                    /*
                     * Freeing may cause a merge which will invalidate our iteration.
                     * Don't be clever, just restart.
                     */
                    again = gcvTRUE;

                    break;
                }
#if gcdUSE_VIDMEM_PER_PID
                else
                {
                    gcmkASSERT(node->VidMem.processID == Pid);
                }
#endif
            }
        }
        while (again);
    }

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Memory->os, Memory->mutex));
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    if (mutex)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Memory->os, Memory->mutex));
    }

    gcmkFOOTER();
    return status;
}
#endif

/*******************************************************************************
**
** _NeedVirtualMapping
**
**  Whether setup GPU page table for video node.
**
**  INPUT:
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a gcuVIDMEM_NODE union.
**
**      gceCORE  Core
**          Id of current GPU.
**
**  OUTPUT:
**      gctBOOL * NeedMapping
**          A pointer hold the result whether Node should be mapping.
*/
static gceSTATUS
_NeedVirtualMapping(
    IN gckKERNEL Kernel,
    IN gceCORE  Core,
    IN gcuVIDMEM_NODE_PTR Node,
    OUT gctBOOL * NeedMapping
)
{
    gceSTATUS status;
    gctUINT32 phys;
    gctUINT32 end;
    gcePOOL pool;
    gctUINT32 offset;

    gcmkHEADER_ARG("Node=0x%X", Node);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Kernel != gcvNULL);
    gcmkVERIFY_ARGUMENT(Node != gcvNULL);
    gcmkVERIFY_ARGUMENT(NeedMapping != gcvNULL);
    gcmkVERIFY_ARGUMENT(Core < gcdCORE_COUNT);

    if (Node->Virtual.contiguous)
    {
#if gcdENABLE_VG
        if (Core == gcvCORE_VG)
        {
            *NeedMapping = gcvFALSE;
        }
        else
#endif
        {
            /* For cores which can't access all physical address. */
            gcmkONERROR(gckOS_GetPhysicalAddress(Kernel->os,
                        Node->Virtual.logical,
                        &phys));

            /* If part of region is belong to gcvPOOL_VIRTUAL,
            ** whole region has to be mapped. */
            end = phys + Node->Virtual.bytes - 1;

            gcmkONERROR(gckHARDWARE_SplitMemory(
                        Kernel->hardware, end, &pool, &offset
                        ));

            *NeedMapping = (pool == gcvPOOL_VIRTUAL);
        }
    }
    else
    {
        *NeedMapping = gcvTRUE;
    }

    gcmkFOOTER_ARG("*NeedMapping=%d", *NeedMapping);
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckVIDMEM_Lock
**
**  Lock a video memory node and return its hardware specific address.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a gcuVIDMEM_NODE union.
**
**  OUTPUT:
**
**      gctUINT32 * Address
**          Pointer to a variable that will hold the hardware specific address.
*/
gceSTATUS
gckVIDMEM_Lock(
    IN gckKERNEL Kernel,
    IN gcuVIDMEM_NODE_PTR Node,
    IN gctBOOL Cacheable,
    OUT gctUINT32 * Address
    )
{
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;
    gctBOOL locked = gcvFALSE;
    gckOS os = gcvNULL;
    gctBOOL needMapping = gcvFALSE;

    gcmkHEADER_ARG("Node=0x%x", Node);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    if ((Node == gcvNULL)
    ||  (Node->VidMem.memory == gcvNULL)
    )
    {
        /* Invalid object. */
        gcmkONERROR(gcvSTATUS_INVALID_OBJECT);
    }

    /**************************** Video Memory ********************************/

    if (Node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
    {
        if (Cacheable == gcvTRUE)
        {
//####modified for marvell-bg2
#if !USE_GALOIS_SHM
            gcmkONERROR(gcvSTATUS_INVALID_REQUEST);
#endif
//####end for marvell-bg2
        }

        /* Increment the lock count. */
        Node->VidMem.locked ++;

        /* Return the address of the node. */
#if !gcdUSE_VIDMEM_PER_PID
//####modified for marvell-bg2
#if !USE_GALOIS_SHM
        *Address = Node->VidMem.memory->baseAddress
                 + Node->VidMem.offset
                 + Node->VidMem.alignment;
#else
        *Address = Node->VidMem.physical;
#endif
//####end for marvell-bg2
#else
        *Address = Node->VidMem.physical;
#endif

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                      "Locked node 0x%x (%d) @ 0x%08X",
                      Node,
                      Node->VidMem.locked,
                      *Address);
    }

    /*************************** Virtual Memory *******************************/

    else
    {
        /* Verify the gckKERNEL object pointer. */
        gcmkVERIFY_OBJECT(Node->Virtual.kernel, gcvOBJ_KERNEL);

        /* Extract the gckOS object pointer. */
        os = Node->Virtual.kernel->os;
        gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

        /* Grab the mutex. */
        gcmkONERROR(gckOS_AcquireMutex(os, Node->Virtual.mutex, gcvINFINITE));
        acquired = gcvTRUE;

        gcmkONERROR(
            gckOS_LockPages(os,
                            Node->Virtual.physical,
                            Node->Virtual.bytes,
                            Cacheable,
                            &Node->Virtual.logical,
                            &Node->Virtual.pageCount));

        /* Increment the lock count. */
        if (Node->Virtual.lockeds[Kernel->core] ++ == 0)
        {
            /* Is this node pending for a final unlock? */
#ifdef __QNXNTO__
            if (!Node->Virtual.contiguous && Node->Virtual.unlockPendings[Kernel->core])
            {
                /* Make sure we have a page table. */
                gcmkASSERT(Node->Virtual.pageTables[Kernel->core] != gcvNULL);

                /* Remove pending unlock. */
                Node->Virtual.unlockPendings[Kernel->core] = gcvFALSE;
            }

            /* First lock - create a page table. */
            gcmkASSERT(Node->Virtual.pageTables[Kernel->core] == gcvNULL);

            /* Make sure we mark our node as not flushed. */
            Node->Virtual.unlockPendings[Kernel->core] = gcvFALSE;
#endif

            locked = gcvTRUE;

            gcmkONERROR(_NeedVirtualMapping(Kernel, Kernel->core, Node, &needMapping));

            if (needMapping == gcvFALSE)
            {
                /* Get physical address directly */
                 gcmkONERROR(gckOS_GetPhysicalAddress(os,
                             Node->Virtual.logical,
                             &Node->Virtual.addresses[Kernel->core]));
            }
            else
            {
#if gcdENABLE_VG
                if (Kernel->vg != gcvNULL)
                {
                    /* Allocate pages inside the MMU. */
                    gcmkONERROR(
                        gckVGMMU_AllocatePages(Kernel->vg->mmu,
                                             Node->Virtual.pageCount,
                                             &Node->Virtual.pageTables[Kernel->core],
                                             &Node->Virtual.addresses[Kernel->core]));
                }
                else
#endif
                {
                    /* Allocate pages inside the MMU. */
                    gcmkONERROR(
                        gckMMU_AllocatePages(Kernel->mmu,
                                             Node->Virtual.pageCount,
                                             &Node->Virtual.pageTables[Kernel->core],
                                             &Node->Virtual.addresses[Kernel->core]));
                }

                Node->Virtual.lockKernels[Kernel->core] = Kernel;

                /* Map the pages. */
#ifdef __QNXNTO__
                gcmkONERROR(
                    gckOS_MapPagesEx(os,
                                     Kernel->core,
                                     Node->Virtual.physical,
                                     Node->Virtual.logical,
                                     Node->Virtual.pageCount,
                                     Node->Virtual.pageTables[Kernel->core]));
#else
                gcmkONERROR(
                    gckOS_MapPagesEx(os,
                                     Kernel->core,
                                     Node->Virtual.physical,
                                     Node->Virtual.pageCount,
                                     Node->Virtual.pageTables[Kernel->core]));
#endif

#if gcdENABLE_VG
                if (Kernel->core != gcvCORE_VG)
#endif
                {
                    gcmkONERROR(gckMMU_Flush(Kernel->mmu));
                }
            }
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                           "Mapped virtual node 0x%x to 0x%08X",
                           Node,
                           Node->Virtual.addresses[Kernel->core]);
        }

        /* Return hardware address. */
        *Address = Node->Virtual.addresses[Kernel->core];

        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->Virtual.mutex));
    }

    /* Success. */
    gcmkFOOTER_ARG("*Address=%08x", *Address);
    return gcvSTATUS_OK;

OnError:
    if (locked)
    {
        if (Node->Virtual.pageTables[Kernel->core] != gcvNULL)
        {
#if gcdENABLE_VG
            if (Kernel->vg != gcvNULL)
            {
                /* Free the pages from the MMU. */
                gcmkVERIFY_OK(
                    gckVGMMU_FreePages(Kernel->vg->mmu,
                                     Node->Virtual.pageTables[Kernel->core],
                                     Node->Virtual.pageCount));
            }
            else
#endif
            {
                /* Free the pages from the MMU. */
                gcmkVERIFY_OK(
                    gckMMU_FreePages(Kernel->mmu,
                                     Node->Virtual.pageTables[Kernel->core],
                                     Node->Virtual.pageCount));
            }
            Node->Virtual.pageTables[Kernel->core]  = gcvNULL;
            Node->Virtual.lockKernels[Kernel->core] = gcvNULL;
        }

        /* Unlock the pages. */
        gcmkVERIFY_OK(
            gckOS_UnlockPages(os,
                              Node->Virtual.physical,
                              Node->Virtual.bytes,
                              Node->Virtual.logical
                              ));

        Node->Virtual.lockeds[Kernel->core]--;
    }

    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->Virtual.mutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckVIDMEM_Unlock
**
**  Unlock a video memory node.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a locked gcuVIDMEM_NODE union.
**
**      gceSURF_TYPE Type
**          Type of surface to unlock.
**
**      gctBOOL * Asynchroneous
**          Pointer to a variable specifying whether the surface should be
**          unlocked asynchroneously or not.
**
**  OUTPUT:
**
**      gctBOOL * Asynchroneous
**          Pointer to a variable receiving the number of bytes used in the
**          command buffer specified by 'Commands'.  If gcvNULL, there is no
**          command buffer.
*/
gceSTATUS
gckVIDMEM_Unlock(
    IN gckKERNEL Kernel,
    IN gcuVIDMEM_NODE_PTR Node,
    IN gceSURF_TYPE Type,
    IN OUT gctBOOL * Asynchroneous
    )
{
    gceSTATUS status;
    gckHARDWARE hardware;
    gctPOINTER buffer;
    gctSIZE_T requested, bufferSize;
    gckCOMMAND command = gcvNULL;
    gceKERNEL_FLUSH flush;
    gckOS os = gcvNULL;
    gctBOOL acquired = gcvFALSE;
    gctBOOL commitEntered = gcvFALSE;
    gctINT32 i, totalLocked;

    gcmkHEADER_ARG("Node=0x%x Type=%d *Asynchroneous=%d",
                   Node, Type, gcmOPT_VALUE(Asynchroneous));

    /* Verify the arguments. */
    if ((Node == gcvNULL)
    ||  (Node->VidMem.memory == gcvNULL)
    )
    {
        /* Invalid object. */
        gcmkONERROR(gcvSTATUS_INVALID_OBJECT);
    }

    /**************************** Video Memory ********************************/

    if (Node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
    {
        if (Node->VidMem.locked <= 0)
        {
            /* The surface was not locked. */
            status = gcvSTATUS_MEMORY_UNLOCKED;
            goto OnError;
        }

        /* Decrement the lock count. */
        Node->VidMem.locked --;

        if (Asynchroneous != gcvNULL)
        {
            /* No need for any events. */
            *Asynchroneous = gcvFALSE;
        }

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                      "Unlocked node 0x%x (%d)",
                      Node,
                      Node->VidMem.locked);

#ifdef __QNXNTO__
        /* Unmap the video memory */
        if ((Node->VidMem.locked == 0) && (Node->VidMem.logical != gcvNULL))
        {
            if (Kernel->core == gcvCORE_VG)
            {
                gckKERNEL_UnmapVideoMemory(Kernel,
                                           Node->VidMem.logical,
                                           Node->VidMem.processID,
                                           Node->VidMem.bytes);
                Node->VidMem.logical = gcvNULL;
            }
        }
#endif /* __QNXNTO__ */

        if (Node->VidMem.freePending && (Node->VidMem.locked == 0))
        {
            /* Client has unlocked node previously attempted to be freed by compositor. Free now. */
            Node->VidMem.freePending = gcvFALSE;
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                           "Deferred-freeing Node 0x%x.",
                           Node);
            gcmkONERROR(gckVIDMEM_Free(Node));
        }
    }

    /*************************** Virtual Memory *******************************/

    else
    {
        /* Verify the gckHARDWARE object pointer. */
        hardware = Kernel->hardware;
        gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

        /* Verify the gckCOMMAND object pointer. */
        command = Kernel->command;
        gcmkVERIFY_OBJECT(command, gcvOBJ_COMMAND);

        /* Get the gckOS object pointer. */
        os = Kernel->os;
        gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

        /* Grab the mutex. */
        gcmkONERROR(
            gckOS_AcquireMutex(os, Node->Virtual.mutex, gcvINFINITE));

        acquired = gcvTRUE;

        if (Asynchroneous == gcvNULL)
        {
            if (Node->Virtual.lockeds[Kernel->core] == 0)
            {
                status = gcvSTATUS_MEMORY_UNLOCKED;
                goto OnError;
            }

            /* Decrement lock count. */
            -- Node->Virtual.lockeds[Kernel->core];

            /* See if we can unlock the resources. */
            if (Node->Virtual.lockeds[Kernel->core] == 0)
            {
                /* Free the page table. */
                if (Node->Virtual.pageTables[Kernel->core] != gcvNULL)
                {
#if gcdENABLE_VG
                    if (Kernel->vg != gcvNULL)
                    {
                        gcmkONERROR(
                            gckVGMMU_FreePages(Kernel->vg->mmu,
                                             Node->Virtual.pageTables[Kernel->core],
                                             Node->Virtual.pageCount));
                    }
                    else
#endif
                    {
                        gcmkONERROR(
                            gckMMU_FreePages(Kernel->mmu,
                                             Node->Virtual.pageTables[Kernel->core],
                                             Node->Virtual.pageCount));
                    }
                    /* Mark page table as freed. */
                    Node->Virtual.pageTables[Kernel->core] = gcvNULL;
                    Node->Virtual.lockKernels[Kernel->core] = gcvNULL;
                }

#ifdef __QNXNTO__
                /* Mark node as unlocked. */
                Node->Virtual.unlockPendings[Kernel->core] = gcvFALSE;
#endif
            }

            for (i = 0, totalLocked = 0; i < gcdCORE_COUNT; i++)
            {
                totalLocked += Node->Virtual.lockeds[i];
            }

            if (totalLocked == 0)
            {
                /* Owner have already freed this node
                ** and we are the last one to unlock, do
                ** real free */
                if (Node->Virtual.freed)
                {
                    /* Free the virtual memory. */
                    gcmkVERIFY_OK(gckOS_FreePagedMemory(Kernel->os,
                                                        Node->Virtual.physical,
                                                        Node->Virtual.bytes));

                    /* Release mutex before node is destroyed */
                    gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->Virtual.mutex));

                    acquired = gcvFALSE;

                    /* Destroy the gcuVIDMEM_NODE union. */
                    gcmkVERIFY_OK(gckVIDMEM_DestroyVirtual(Node));

                    /* Node has been destroyed, so we should not touch it any more */
                    gcmkFOOTER();
                    return gcvSTATUS_OK;
                }
            }

            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                           "Unmapped virtual node 0x%x from 0x%08X",
                           Node, Node->Virtual.addresses[Kernel->core]);

        }

        else
        {
            /* If we need to unlock a node from virtual memory we have to be
            ** very carefull.  If the node is still inside the caches we
            ** might get a bus error later if the cache line needs to be
            ** replaced.  So - we have to flush the caches before we do
            ** anything. */

            /* gckCommand_EnterCommit() can't be called in interrupt handler because
            ** of a dead lock situation:
            ** process call Command_Commit(), and acquire Command->mutexQueue in
            ** gckCOMMAND_EnterCommit(). Then it will wait for a signal which depends
            ** on interrupt handler to generate, if interrupt handler enter
            ** gckCommand_EnterCommit(), process will never get the signal. */

            /* So, flush cache when we still in process context, and then ask caller to
            ** schedule a event. */

            gcmkONERROR(
                gckOS_UnlockPages(os,
                              Node->Virtual.physical,
                              Node->Virtual.bytes,
                              Node->Virtual.logical));

            if (!Node->Virtual.contiguous
            &&  (Node->Virtual.lockeds[Kernel->core] == 1)
            )
            {
                if (Type == gcvSURF_BITMAP)
                {
                    /* Flush 2D cache. */
                    flush = gcvFLUSH_2D;
                }
                else if (Type == gcvSURF_RENDER_TARGET)
                {
                    /* Flush color cache. */
                    flush = gcvFLUSH_COLOR;
                }
                else if (Type == gcvSURF_DEPTH)
                {
                    /* Flush depth cache. */
                    flush = gcvFLUSH_DEPTH;
                }
                else
                {
                    /* No flush required. */
                    flush = (gceKERNEL_FLUSH) 0;
                }

                gcmkONERROR(
                    gckHARDWARE_Flush(hardware, flush, gcvNULL, &requested));

                if (requested != 0)
                {
                    /* Acquire the command queue. */
                    gcmkONERROR(gckCOMMAND_EnterCommit(command, gcvFALSE));
                    commitEntered = gcvTRUE;

                    gcmkONERROR(gckCOMMAND_Reserve(
                        command, requested, &buffer, &bufferSize
                        ));

                    gcmkONERROR(gckHARDWARE_Flush(
                        hardware, flush, buffer, &bufferSize
                        ));

                    /* Mark node as pending. */
#ifdef __QNXNTO__
                    Node->Virtual.unlockPendings[Kernel->core] = gcvTRUE;
#endif

                    gcmkONERROR(gckCOMMAND_Execute(command, requested));

                    /* Release the command queue. */
                    gcmkONERROR(gckCOMMAND_ExitCommit(command, gcvFALSE));
                    commitEntered = gcvFALSE;
                }
            }

            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_VIDMEM,
                           "Scheduled unlock for virtual node 0x%x",
                           Node);

            /* Schedule the surface to be unlocked. */
            *Asynchroneous = gcvTRUE;
        }

        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->Virtual.mutex));

        acquired = gcvFALSE;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Asynchroneous=%d", gcmOPT_VALUE(Asynchroneous));
    return gcvSTATUS_OK;

OnError:
    if (commitEntered)
    {
        /* Release the command queue mutex. */
        gcmkVERIFY_OK(gckCOMMAND_ExitCommit(command, gcvFALSE));
    }

    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(os, Node->Virtual.mutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}
