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




#include "gc_hal.h"
#include "gc_hal_internal.h"
#include "gc_hal_kernel.h"

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/uaccess.h>

#define _GC_OBJ_ZONE    gcvZONE_KERNEL

/*******************************************************************************
***** Version Signature *******************************************************/

#define _gcmTXT2STR(t) #t
#define gcmTXT2STR(t) _gcmTXT2STR(t)
const char * _VERSION = "\n\0$VERSION$"
                        gcmTXT2STR(gcvVERSION_MAJOR) "."
                        gcmTXT2STR(gcvVERSION_MINOR) "."
                        gcmTXT2STR(gcvVERSION_PATCH) ":"
                        gcmTXT2STR(gcvVERSION_BUILD) "$\n";

/******************************************************************************\
******************************* gckKERNEL API Code ******************************
\******************************************************************************/

#if gcmIS_DEBUG(gcdDEBUG_TRACE)
#define gcmDEFINE2TEXT(d) #d
const char *_DispatchText[] =
{
    gcmDEFINE2TEXT(gcvHAL_QUERY_VIDEO_MEMORY),
    gcmDEFINE2TEXT(gcvHAL_QUERY_CHIP_IDENTITY),
    gcmDEFINE2TEXT(gcvHAL_ALLOCATE_NON_PAGED_MEMORY),
    gcmDEFINE2TEXT(gcvHAL_FREE_NON_PAGED_MEMORY),
    gcmDEFINE2TEXT(gcvHAL_ALLOCATE_CONTIGUOUS_MEMORY),
    gcmDEFINE2TEXT(gcvHAL_FREE_CONTIGUOUS_MEMORY),
    gcmDEFINE2TEXT(gcvHAL_ALLOCATE_VIDEO_MEMORY),
    gcmDEFINE2TEXT(gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY),
    gcmDEFINE2TEXT(gcvHAL_FREE_VIDEO_MEMORY),
    gcmDEFINE2TEXT(gcvHAL_MAP_MEMORY),
    gcmDEFINE2TEXT(gcvHAL_UNMAP_MEMORY),
    gcmDEFINE2TEXT(gcvHAL_MAP_USER_MEMORY),
    gcmDEFINE2TEXT(gcvHAL_UNMAP_USER_MEMORY),
    gcmDEFINE2TEXT(gcvHAL_LOCK_VIDEO_MEMORY),
    gcmDEFINE2TEXT(gcvHAL_UNLOCK_VIDEO_MEMORY),
    gcmDEFINE2TEXT(gcvHAL_EVENT_COMMIT),
    gcmDEFINE2TEXT(gcvHAL_USER_SIGNAL),
    gcmDEFINE2TEXT(gcvHAL_SIGNAL),
    gcmDEFINE2TEXT(gcvHAL_WRITE_DATA),
    gcmDEFINE2TEXT(gcvHAL_COMMIT),
    gcmDEFINE2TEXT(gcvHAL_STALL),
    gcmDEFINE2TEXT(gcvHAL_READ_REGISTER),
    gcmDEFINE2TEXT(gcvHAL_WRITE_REGISTER),
    gcmDEFINE2TEXT(gcvHAL_GET_PROFILE_SETTING),
    gcmDEFINE2TEXT(gcvHAL_SET_PROFILE_SETTING),
    gcmDEFINE2TEXT(gcvHAL_READ_ALL_PROFILE_REGISTERS),
    gcmDEFINE2TEXT(gcvHAL_PROFILE_REGISTERS_2D),
    gcmDEFINE2TEXT(gcvHAL_SET_POWER_MANAGEMENT_STATE),
    gcmDEFINE2TEXT(gcvHAL_QUERY_POWER_MANAGEMENT_STATE),
    gcmDEFINE2TEXT(gcvHAL_GET_BASE_ADDRESS),
    gcmDEFINE2TEXT(gcvHAL_SET_IDLE),
    gcmDEFINE2TEXT(gcvHAL_QUERY_KERNEL_SETTINGS),
    gcmDEFINE2TEXT(gcvHAL_RESET),
    gcmDEFINE2TEXT(gcvHAL_MAP_PHYSICAL),
    gcmDEFINE2TEXT(gcvHAL_DEBUG),
    gcmDEFINE2TEXT(gcvHAL_CACHE),
    gcmDEFINE2TEXT(gcvHAL_TIMESTAMP),
    gcmDEFINE2TEXT(gcvHAL_DATABASE),
    gcmDEFINE2TEXT(gcvHAL_VERSION),
    gcmDEFINE2TEXT(gcvHAL_CHIP_INFO),
    gcmDEFINE2TEXT(gcvHAL_ATTACH),
    gcmDEFINE2TEXT(gcvHAL_DETACH)
};
#endif

static void
gckKERNEL_SetTimeOut(
    IN gckKERNEL Kernel,
    IN u32 timeOut
    )
{
    gcmkHEADER_ARG("Kernel=0x%x timeOut=%d", Kernel, timeOut);
#if gcdGPU_TIMEOUT
    Kernel->timeOut = timeOut;
#endif
    gcmkFOOTER_NO();
}

/*******************************************************************************
**
**  gckKERNEL_Construct
**
**  Construct a new gckKERNEL object.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gceCORE Core
**          Specified core.
**
**      IN void *Context
**          Pointer to a driver defined context.
**
**      IN gckDB SharedDB,
**          Pointer to a shared DB.
**
**  OUTPUT:
**
**      gckKERNEL * Kernel
**          Pointer to a variable that will hold the pointer to the gckKERNEL
**          object.
*/

gceSTATUS
gckKERNEL_Construct(
    IN gckOS Os,
    IN gceCORE Core,
    IN void *Context,
    IN gckDB SharedDB,
    OUT gckKERNEL * Kernel
    )
{
    gckKERNEL kernel = NULL;
    gceSTATUS status;
    size_t i;
    void *pointer = NULL;

    gcmkHEADER_ARG("Os=0x%x Context=0x%x", Os, Context);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Kernel != NULL);

    /* Allocate the gckKERNEL object. */
    gcmkONERROR(gckOS_Allocate(Os,
                               sizeof(struct _gckKERNEL),
                               &pointer));

    kernel = pointer;

    /* Zero the object pointers. */
    kernel->hardware     = NULL;
    kernel->command      = NULL;
    kernel->eventObj     = NULL;
    kernel->mmu          = NULL;

    if (SharedDB == NULL)
    {
        gcmkONERROR(gckOS_Allocate(Os,
                                   sizeof(struct _gckDB),
                                   &pointer));

        kernel->db               = pointer;
        kernel->dbCreated        = gcvTRUE;
        kernel->db->freeDatabase = NULL;
        kernel->db->freeRecord   = NULL;
        kernel->db->dbMutex      = NULL;
        kernel->db->lastDatabase = NULL;
        kernel->db->idleTime     = 0;
        kernel->db->lastIdle     = 0;
        kernel->db->lastSlowdown = 0;

        for (i = 0; i < ARRAY_SIZE(kernel->db->db); ++i)
        {
            kernel->db->db[i] = NULL;
        }

        /* Construct a database mutex. */
        gcmkONERROR(gckOS_CreateMutex(Os, &kernel->db->dbMutex));
    }
    else
    {
        kernel->db               = SharedDB;
        kernel->dbCreated        = gcvFALSE;
    }

    for (i = 0; i < ARRAY_SIZE(kernel->timers); ++i)
    {
        kernel->timers[i].startTime = 0;
        kernel->timers[i].stopTime = 0;
    }

    kernel->timeOut      = gcdGPU_TIMEOUT;

    /* Initialize the gckKERNEL object. */
    kernel->object.type = gcvOBJ_KERNEL;
    kernel->os          = Os;
    kernel->core        = Core;

    /* Save context. */
    kernel->context = Context;

    /* Construct atom holding number of clients. */
    kernel->atomClients = NULL;
    gcmkONERROR(gckOS_AtomConstruct(Os, &kernel->atomClients));

    /* Construct the gckHARDWARE object. */
    gcmkONERROR(
        gckHARDWARE_Construct(Os, kernel->core, &kernel->hardware));

    /* Set pointer to gckKERNEL object in gckHARDWARE object. */
    kernel->hardware->kernel = kernel;

    /* Initialize the hardware. */
    gcmkONERROR(
        gckHARDWARE_InitializeHardware(kernel->hardware));

    /* Construct the gckCOMMAND object. */
    gcmkONERROR(
        gckCOMMAND_Construct(kernel, &kernel->command));

    /* Construct the gckEVENT object. */
    gcmkONERROR(
        gckEVENT_Construct(kernel, &kernel->eventObj));

    /* Construct the gckMMU object. */
    gcmkONERROR(
        gckMMU_Construct(kernel, gcdMMU_SIZE, &kernel->mmu));

    /* Return pointer to the gckKERNEL object. */
    *Kernel = kernel;

    /* Success. */
    gcmkFOOTER_ARG("*Kernel=0x%x", *Kernel);
    return gcvSTATUS_OK;

OnError:
    if (kernel != NULL)
    {
        if (kernel->eventObj != NULL)
        {
            gcmkVERIFY_OK(gckEVENT_Destroy(kernel->eventObj));
        }

        if (kernel->command != NULL)
        {
        gcmkVERIFY_OK(gckCOMMAND_Destroy(kernel->command));
        }

        if (kernel->hardware != NULL)
        {
            gcmkVERIFY_OK(gckHARDWARE_Destroy(kernel->hardware));
        }

        if (kernel->atomClients != NULL)
        {
            gcmkVERIFY_OK(gckOS_AtomDestroy(Os, kernel->atomClients));
        }

        if (kernel->dbCreated && kernel->db != NULL)
        {
            if (kernel->db->dbMutex != NULL)
            {
                /* Destroy the database mutex. */
                gcmkVERIFY_OK(gckOS_DeleteMutex(Os, kernel->db->dbMutex));
            }

            gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Os, kernel->db));
        }

        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Os, kernel));
    }

    /* Return the error. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckKERNEL_Destroy
**
**  Destroy an gckKERNEL object.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object to destroy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckKERNEL_Destroy(
    IN gckKERNEL Kernel
    )
{
    size_t i;
    gcsDATABASE_PTR database, databaseNext;
    gcsDATABASE_RECORD_PTR record, recordNext;

    gcmkHEADER_ARG("Kernel=0x%x", Kernel);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    /* Destroy the database. */
    if (Kernel->dbCreated)
    {
        for (i = 0; i < ARRAY_SIZE(Kernel->db->db); ++i)
        {
            if (Kernel->db->db[i] != NULL)
            {
                gcmkVERIFY_OK(
                    gckKERNEL_DestroyProcessDB(Kernel, Kernel->db->db[i]->processID));
            }
        }

        /* Free all databases. */
        for (database = Kernel->db->freeDatabase;
             database != NULL;
             database = databaseNext)
        {
            databaseNext = database->next;
            gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Kernel->os, database));
        }

        if (Kernel->db->lastDatabase != NULL)
        {
            gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Kernel->os, Kernel->db->lastDatabase));
        }

        /* Free all database records. */
        for (record = Kernel->db->freeRecord; record != NULL; record = recordNext)
        {
            recordNext = record->next;
            gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Kernel->os, record));
        }

        /* Destroy the database mutex. */
        gcmkVERIFY_OK(gckOS_DeleteMutex(Kernel->os, Kernel->db->dbMutex));
    }

    /* Destroy the gckMMU object. */
    gcmkVERIFY_OK(gckMMU_Destroy(Kernel->mmu));

    /* Destroy the gckCOMMNAND object. */
    gcmkVERIFY_OK(gckCOMMAND_Destroy(Kernel->command));

    /* Destroy the gckEVENT object. */
    gcmkVERIFY_OK(gckEVENT_Destroy(Kernel->eventObj));

    /* Destroy the gckHARDWARE object. */
    gcmkVERIFY_OK(gckHARDWARE_Destroy(Kernel->hardware));

    /* Detsroy the client atom. */
    gcmkVERIFY_OK(gckOS_AtomDestroy(Kernel->os, Kernel->atomClients));

    /* Mark the gckKERNEL object as unknown. */
    Kernel->object.type = gcvOBJ_UNKNOWN;

    /* Free the gckKERNEL object. */
    gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Kernel->os, Kernel));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}


/*******************************************************************************
**
**  _AllocateMemory
**
**  Private function to walk all required memory pools to allocate the requested
**  amount of video memory.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      gcsHAL_INTERFACE * Interface
**          Pointer to a gcsHAL_INTERFACE structure that defines the command to
**          be dispatched.
**
**  OUTPUT:
**
**      gcsHAL_INTERFACE * Interface
**          Pointer to a gcsHAL_INTERFACE structure that receives any data to be
**          returned.
*/
static gceSTATUS
_AllocateMemory(
    IN gckKERNEL Kernel,
    IN OUT gcePOOL * Pool,
    IN size_t Bytes,
    IN size_t Alignment,
    IN gceSURF_TYPE Type,
    OUT gcuVIDMEM_NODE_PTR * Node
    )
{
    gcePOOL pool;
    gceSTATUS status;
    gckVIDMEM videoMemory;
    int loopCount;
    gcuVIDMEM_NODE_PTR node = NULL;
    int tileStatusInVirtual;

    gcmkHEADER_ARG("Kernel=0x%x *Pool=%d Bytes=%lu Alignment=%lu Type=%d",
                   Kernel, *Pool, Bytes, Alignment, Type);

    gcmkVERIFY_ARGUMENT(Pool != NULL);
    gcmkVERIFY_ARGUMENT(Bytes != 0);

    /* Get initial pool. */
    switch (pool = *Pool)
    {
    case gcvPOOL_DEFAULT:
    case gcvPOOL_LOCAL:
        pool      = gcvPOOL_LOCAL_INTERNAL;
        loopCount = (int) gcvPOOL_NUMBER_OF_POOLS;
        break;

    case gcvPOOL_UNIFIED:
        pool      = gcvPOOL_SYSTEM;
        loopCount = (int) gcvPOOL_NUMBER_OF_POOLS;
        break;

    case gcvPOOL_CONTIGUOUS:
        loopCount = (int) gcvPOOL_NUMBER_OF_POOLS;
        break;

    default:
        loopCount = 1;
        break;
    }

    while (loopCount-- > 0)
    {
        if (pool == gcvPOOL_VIRTUAL)
        {
            /* Create a gcuVIDMEM_NODE for virtual memory. */
            gcmkONERROR(
                gckVIDMEM_ConstructVirtual(Kernel, gcvFALSE, Bytes, &node));

            /* Success. */
            break;
        }

        else
        if (pool == gcvPOOL_CONTIGUOUS)
        {
            /* Create a gcuVIDMEM_NODE for contiguous memory. */
            status = gckVIDMEM_ConstructVirtual(Kernel, gcvTRUE, Bytes, &node);
            if (gcmIS_SUCCESS(status))
            {
                /* Memory allocated. */
                break;
            }
        }

        else
        {
            /* Get pointer to gckVIDMEM object for pool. */
#if gcdUSE_VIDMEM_PER_PID
            u32 pid;
            pid = task_tgid_vnr(current);

            status = gckKERNEL_GetVideoMemoryPoolPid(Kernel, pool, pid, &videoMemory);
            if (status == gcvSTATUS_NOT_FOUND)
            {
                /* Create VidMem pool for this process. */
                status = gckKERNEL_CreateVideoMemoryPoolPid(Kernel, pool, pid, &videoMemory);
            }
#else
            status = gckKERNEL_GetVideoMemoryPool(Kernel, pool, &videoMemory);
#endif

            if (gcmIS_SUCCESS(status))
            {
                /* Allocate memory. */
                status = gckVIDMEM_AllocateLinear(videoMemory,
                                                  Bytes,
                                                  Alignment,
                                                  Type,
                                                  &node);

                if (gcmIS_SUCCESS(status))
                {
                    /* Memory allocated. */
                    node->VidMem.pool = pool;
                    break;
                }
            }
        }

        if (pool == gcvPOOL_LOCAL_INTERNAL)
        {
            /* Advance to external memory. */
            pool = gcvPOOL_LOCAL_EXTERNAL;
        }

        else
        if (pool == gcvPOOL_LOCAL_EXTERNAL)
        {
            /* Advance to contiguous system memory. */
            pool = gcvPOOL_SYSTEM;
        }

        else
        if (pool == gcvPOOL_SYSTEM)
        {
            /* Advance to contiguous memory. */
#ifdef CONFIG_MACH_JZ4770
            pool = gcvPOOL_VIRTUAL;
            // Wolfgang@ingenic.cn, modify, 2011-0
            // do not use __get_free_page when system running,
            // it may cause kernel  hanging.
#else
            pool = gcvPOOL_CONTIGUOUS;
#endif
        }

        else
        if (pool == gcvPOOL_CONTIGUOUS)
        {
            tileStatusInVirtual =
                gckHARDWARE_IsFeatureAvailable(Kernel->hardware,
                                               gcvFEATURE_MC20);

            if (Type == gcvSURF_TILE_STATUS && tileStatusInVirtual != gcvTRUE)
            {
                gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
            }

            /* Advance to virtual memory. */
            pool = gcvPOOL_VIRTUAL;
        }

        else
        {
            /* Out of pools. */
            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }
    }

    if (node == NULL)
    {
        /* Nothing allocated. */
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }


    /* Return node and pool used for allocation. */
    *Node = node;
    *Pool = pool;

    /* Return status. */
    gcmkFOOTER_ARG("*Pool=%d *Node=0x%x", *Pool, *Node);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckKERNEL_Dispatch
**
**  Dispatch a command received from the user HAL layer.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      int FromUser
**          whether the call is from the user space.
**
**      gcsHAL_INTERFACE * Interface
**          Pointer to a gcsHAL_INTERFACE structure that defines the command to
**          be dispatched.
**
**  OUTPUT:
**
**      gcsHAL_INTERFACE * Interface
**          Pointer to a gcsHAL_INTERFACE structure that receives any data to be
**          returned.
*/

gceSTATUS
gckKERNEL_Dispatch(
    IN gckKERNEL Kernel,
    IN int FromUser,
    IN OUT gcsHAL_INTERFACE * Interface
    )
{
    gceSTATUS status = gcvSTATUS_OK;
    size_t bytes;
    gcuVIDMEM_NODE_PTR node;
    int locked = gcvFALSE;
    gctPHYS_ADDR physical = NULL;
    u32 address;
    u32 processID;
#if gcdSECURE_USER
    gcskSECURE_CACHE_PTR cache;
    void *logical;
#endif
    int asynchronous;
    void *paddr = NULL;
#if !USE_NEW_LINUX_SIGNAL
    gctSIGNAL   signal;
#endif

    gcsDATABASE_RECORD record;
    void *   data;

    gcmkHEADER_ARG("Kernel=0x%x FromUser=%d Interface=0x%x",
                   Kernel, FromUser, Interface);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Interface != NULL);

#if gcmIS_DEBUG(gcdDEBUG_TRACE)
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_KERNEL,
                   "Dispatching command %d (%s)",
                   Interface->command, _DispatchText[Interface->command]);
#endif

    /* Get the current process ID. */
    processID = task_tgid_vnr(current);

#if gcdSECURE_USER
    gcmkONERROR(gckKERNEL_GetProcessDBCache(Kernel, processID, &cache));
#endif

    /* Dispatch on command. */
    switch (Interface->command)
    {
    case gcvHAL_GET_BASE_ADDRESS:
        /* Get base address. */
        gcmkONERROR(
            gckOS_GetBaseAddress(Kernel->os,
                                 &Interface->u.GetBaseAddress.baseAddress));
        break;

    case gcvHAL_QUERY_VIDEO_MEMORY:
        /* Query video memory size. */
        gcmkONERROR(gckKERNEL_QueryVideoMemory(Kernel, Interface));
        break;

    case gcvHAL_QUERY_CHIP_IDENTITY:
        /* Query chip identity. */
        gcmkONERROR(
            gckHARDWARE_QueryChipIdentity(
                Kernel->hardware,
                &Interface->u.QueryChipIdentity));
        break;

    case gcvHAL_MAP_MEMORY:
        physical = Interface->u.MapMemory.physical;

        /* Map memory. */
        gcmkONERROR(
            gckKERNEL_MapMemory(Kernel,
                                physical,
                                Interface->u.MapMemory.bytes,
                                &Interface->u.MapMemory.logical));
        gcmkVERIFY_OK(
            gckKERNEL_AddProcessDB(Kernel,
                                   processID, gcvDB_MAP_MEMORY,
                                   Interface->u.MapMemory.logical,
                                   physical,
                                   Interface->u.MapMemory.bytes));
        break;

    case gcvHAL_UNMAP_MEMORY:
        physical = Interface->u.UnmapMemory.physical;

        /* Unmap memory. */
        gcmkONERROR(
            gckKERNEL_UnmapMemory(Kernel,
                                  physical,
                                  Interface->u.UnmapMemory.bytes,
                                  Interface->u.UnmapMemory.logical));
        gcmkVERIFY_OK(
            gckKERNEL_RemoveProcessDB(Kernel,
                                      processID, gcvDB_MAP_MEMORY,
                                      Interface->u.UnmapMemory.logical));
        break;

    case gcvHAL_ALLOCATE_NON_PAGED_MEMORY:
        /* Allocate non-paged memory. */
        gcmkONERROR(
            gckOS_AllocateNonPagedMemory(
                Kernel->os,
                FromUser,
                &Interface->u.AllocateNonPagedMemory.bytes,
                &Interface->u.AllocateNonPagedMemory.physical,
                &Interface->u.AllocateNonPagedMemory.logical));

        gcmkVERIFY_OK(
            gckKERNEL_AddProcessDB(Kernel,
                                   processID, gcvDB_NON_PAGED,
                                   Interface->u.AllocateNonPagedMemory.logical,
                                   Interface->u.AllocateNonPagedMemory.physical,
                                   Interface->u.AllocateNonPagedMemory.bytes));
        break;

    case gcvHAL_FREE_NON_PAGED_MEMORY:
        physical = Interface->u.FreeNonPagedMemory.physical;

        /* Free non-paged memory. */
        gcmkONERROR(
            gckOS_FreeNonPagedMemory(Kernel->os,
                                     Interface->u.FreeNonPagedMemory.bytes,
                                     physical,
                                     Interface->u.FreeNonPagedMemory.logical));

        gcmkVERIFY_OK(
            gckKERNEL_RemoveProcessDB(Kernel,
                                      processID, gcvDB_NON_PAGED,
                                      Interface->u.FreeNonPagedMemory.logical));

#if gcdSECURE_USER
        gcmkVERIFY_OK(gckKERNEL_FlushTranslationCache(
            Kernel,
            cache,
            Interface->u.FreeNonPagedMemory.logical,
            Interface->u.FreeNonPagedMemory.bytes));
#endif
        break;

    case gcvHAL_ALLOCATE_CONTIGUOUS_MEMORY:
        /* Allocate contiguous memory. */
        gcmkONERROR(gckOS_AllocateContiguous(
            Kernel->os,
            FromUser,
            &Interface->u.AllocateContiguousMemory.bytes,
            &Interface->u.AllocateContiguousMemory.physical,
            &Interface->u.AllocateContiguousMemory.logical));

        gcmkONERROR(gckHARDWARE_ConvertLogical(
            Kernel->hardware,
            Interface->u.AllocateContiguousMemory.logical,
            &Interface->u.AllocateContiguousMemory.address));

        gcmkVERIFY_OK(gckKERNEL_AddProcessDB(
            Kernel,
            processID, gcvDB_CONTIGUOUS,
            Interface->u.AllocateContiguousMemory.logical,
            Interface->u.AllocateContiguousMemory.physical,
            Interface->u.AllocateContiguousMemory.bytes));
        break;

    case gcvHAL_FREE_CONTIGUOUS_MEMORY:
        physical = Interface->u.FreeContiguousMemory.physical;

        /* Free contiguous memory. */
        gcmkONERROR(
            gckOS_FreeContiguous(Kernel->os,
                                 physical,
                                 Interface->u.FreeContiguousMemory.logical,
                                 Interface->u.FreeContiguousMemory.bytes));

        gcmkVERIFY_OK(
            gckKERNEL_RemoveProcessDB(Kernel,
                                      processID, gcvDB_CONTIGUOUS,
                                      Interface->u.FreeNonPagedMemory.logical));

#if gcdSECURE_USER
        gcmkVERIFY_OK(gckKERNEL_FlushTranslationCache(
            Kernel,
            cache,
            Interface->u.FreeContiguousMemory.logical,
            Interface->u.FreeContiguousMemory.bytes));
#endif
        break;

    case gcvHAL_ALLOCATE_VIDEO_MEMORY:

        gcmkONERROR(gcvSTATUS_NOT_SUPPORTED);

        break;

    case gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY:
        /* Allocate memory. */
        gcmkONERROR(
            _AllocateMemory(Kernel,
                            &Interface->u.AllocateLinearVideoMemory.pool,
                            Interface->u.AllocateLinearVideoMemory.bytes,
                            Interface->u.AllocateLinearVideoMemory.alignment,
                            Interface->u.AllocateLinearVideoMemory.type,
                            &Interface->u.AllocateLinearVideoMemory.node));

        /* Get actual size of node. */
        node = Interface->u.AllocateLinearVideoMemory.node;
        if (node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
        {
            bytes = node->VidMem.bytes;
        }
        else
        {
            bytes = node->Virtual.bytes;
        }

        gcmkONERROR(
            gckKERNEL_AddProcessDB(Kernel,
                                   processID, gcvDB_VIDEO_MEMORY,
                                   Interface->u.AllocateLinearVideoMemory.node,
                                   NULL,
                                   bytes));
        break;

    case gcvHAL_FREE_VIDEO_MEMORY:
        /* Free video memory. */
        gcmkONERROR(
            gckVIDMEM_Free(Interface->u.FreeVideoMemory.node));

        gcmkONERROR(
            gckKERNEL_RemoveProcessDB(Kernel,
                                      processID, gcvDB_VIDEO_MEMORY,
                                      Interface->u.FreeVideoMemory.node));
        break;

    case gcvHAL_LOCK_VIDEO_MEMORY:
        /* Lock video memory. */
        gcmkONERROR(
            gckVIDMEM_Lock(Kernel,
                           Interface->u.LockVideoMemory.node,
                           Interface->u.LockVideoMemory.cacheable,
                           &Interface->u.LockVideoMemory.address));

        locked = gcvTRUE;

        node = Interface->u.LockVideoMemory.node;
        if (node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
        {
            /* Map video memory address into user space. */
            gcmkONERROR(
                gckKERNEL_MapVideoMemoryEx(Kernel,
                                           gcvCORE_MAJOR,
                                           FromUser,
                                           Interface->u.LockVideoMemory.address,
                                           &Interface->u.LockVideoMemory.memory));
        }
        else
        {
            Interface->u.LockVideoMemory.memory = node->Virtual.logical;

            /* Success. */
            status = gcvSTATUS_OK;
        }

#if gcdSECURE_USER
        /* Return logical address as physical address. */
        Interface->u.LockVideoMemory.address =
            gcmPTR2INT(Interface->u.LockVideoMemory.memory);
#endif
        gcmkONERROR(
            gckKERNEL_AddProcessDB(Kernel,
                                   processID, gcvDB_VIDEO_MEMORY_LOCKED,
                                   Interface->u.LockVideoMemory.node,
                                   NULL,
                                   0));

        break;

    case gcvHAL_UNLOCK_VIDEO_MEMORY:
        /* Unlock video memory. */
        node = Interface->u.UnlockVideoMemory.node;

#if gcdSECURE_USER
        /* Save node information before it disappears. */
        if (node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
        {
            logical = NULL;
            bytes   = 0;
        }
        else
        {
            logical = node->Virtual.logical;
            bytes   = node->Virtual.bytes;
        }
#endif

        /* Unlock video memory. */
        gcmkONERROR(
            gckVIDMEM_Unlock(Kernel,
                             node,
                             Interface->u.UnlockVideoMemory.type,
                             &Interface->u.UnlockVideoMemory.asynchroneous));

#if gcdSECURE_USER
        /* Flush the translation cache for virtual surfaces. */
        if (logical != NULL)
        {
            gcmkVERIFY_OK(gckKERNEL_FlushTranslationCache(Kernel,
                                                          cache,
                                                          logical,
                                                          bytes));
        }
#endif
        if (Interface->u.UnlockVideoMemory.asynchroneous == gcvFALSE)
        {
            /* There isn't a event to unlock this node, remove record now */
            gcmkONERROR(
                gckKERNEL_RemoveProcessDB(Kernel,
                                          processID, gcvDB_VIDEO_MEMORY_LOCKED,
                                          Interface->u.UnlockVideoMemory.node));
        }

        break;

    case gcvHAL_EVENT_COMMIT:
        /* Commit an event queue. */
        gcmkONERROR(
            gckEVENT_Commit(Kernel->eventObj,
                            Interface->u.Event.queue));
        break;

    case gcvHAL_COMMIT:
        /* Commit a command and context buffer. */
        gcmkONERROR(
            gckCOMMAND_Commit(Kernel->command,
                              Interface->u.Commit.context,
                              Interface->u.Commit.commandBuffer,
                              Interface->u.Commit.delta,
                              Interface->u.Commit.queue,
                              processID));
        break;

    case gcvHAL_STALL:
        /* Stall the command queue. */
        gcmkONERROR(gckCOMMAND_Stall(Kernel->command, gcvFALSE));
        break;

    case gcvHAL_MAP_USER_MEMORY:
        /* Map user memory to DMA. */
        gcmkONERROR(
            gckOS_MapUserMemoryEx(Kernel->os,
                                  Kernel->core,
                                  Interface->u.MapUserMemory.memory,
                                  Interface->u.MapUserMemory.size,
                                  &Interface->u.MapUserMemory.info,
                                  &Interface->u.MapUserMemory.address));
        gcmkVERIFY_OK(
            gckKERNEL_AddProcessDB(Kernel,
                                   processID, gcvDB_MAP_USER_MEMORY,
                                   Interface->u.MapUserMemory.memory,
                                   Interface->u.MapUserMemory.info,
                                   Interface->u.MapUserMemory.size));
        break;

    case gcvHAL_UNMAP_USER_MEMORY:
        address = Interface->u.MapUserMemory.address;

        /* Unmap user memory. */
        gcmkONERROR(
            gckOS_UnmapUserMemoryEx(Kernel->os,
                                    Kernel->core,
                                    Interface->u.UnmapUserMemory.memory,
                                    Interface->u.UnmapUserMemory.size,
                                    Interface->u.UnmapUserMemory.info,
                                    address));

#if gcdSECURE_USER
        gcmkVERIFY_OK(gckKERNEL_FlushTranslationCache(
            Kernel,
            cache,
            Interface->u.UnmapUserMemory.memory,
            Interface->u.UnmapUserMemory.size));
#endif
        gcmkVERIFY_OK(
            gckKERNEL_RemoveProcessDB(Kernel,
                                      processID, gcvDB_MAP_USER_MEMORY,
                                      Interface->u.UnmapUserMemory.memory));
        break;

#if !USE_NEW_LINUX_SIGNAL
    case gcvHAL_USER_SIGNAL:
        /* Dispatch depends on the user signal subcommands. */
        switch(Interface->u.UserSignal.command)
        {
        case gcvUSER_SIGNAL_CREATE:
            /* Create a signal used in the user space. */
            gcmkONERROR(
                gckOS_CreateUserSignal(Kernel->os,
                                       Interface->u.UserSignal.manualReset,
                                       &Interface->u.UserSignal.id));

            gcmkVERIFY_OK(
                gckKERNEL_AddProcessDB(Kernel,
                                       processID, gcvDB_SIGNAL,
                                       gcmINT2PTR(Interface->u.UserSignal.id),
                                       NULL,
                                       0));
            break;

        case gcvUSER_SIGNAL_DESTROY:
            /* Destroy the signal. */
            gcmkONERROR(
                gckOS_DestroyUserSignal(Kernel->os,
                                        Interface->u.UserSignal.id));

            gcmkVERIFY_OK(gckKERNEL_RemoveProcessDB(
                Kernel,
                processID, gcvDB_SIGNAL,
                gcmINT2PTR(Interface->u.UserSignal.id)));
            break;

        case gcvUSER_SIGNAL_SIGNAL:
            /* Signal the signal. */
            gcmkONERROR(
                gckOS_SignalUserSignal(Kernel->os,
                                       Interface->u.UserSignal.id,
                                       Interface->u.UserSignal.state));
            break;

        case gcvUSER_SIGNAL_WAIT:
            /* Wait on the signal. */
            status = gckOS_WaitUserSignal(Kernel->os,
                                          Interface->u.UserSignal.id,
                                          Interface->u.UserSignal.wait);
            break;

        case gcvUSER_SIGNAL_MAP:
            gcmkONERROR(
                gckOS_MapSignal(Kernel->os,
                               (gctSIGNAL)Interface->u.UserSignal.id,
                               (gctHANDLE)processID,
                               &signal));

            gcmkVERIFY_OK(
                gckKERNEL_AddProcessDB(Kernel,
                                       processID, gcvDB_SIGNAL,
                                       gcmINT2PTR(Interface->u.UserSignal.id),
                                       NULL,
                                       0));
            break;

        case gcvUSER_SIGNAL_UNMAP:
            /* Destroy the signal. */
            gcmkONERROR(
                gckOS_DestroyUserSignal(Kernel->os,
                                        Interface->u.UserSignal.id));

            gcmkVERIFY_OK(gckKERNEL_RemoveProcessDB(
                Kernel,
                processID, gcvDB_SIGNAL,
                gcmINT2PTR(Interface->u.UserSignal.id)));
            break;

        default:
            /* Invalid user signal command. */
            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }
        break;
#endif

    case gcvHAL_SET_POWER_MANAGEMENT_STATE:
        /* Set the power management state. */
        gcmkONERROR(
            gckHARDWARE_SetPowerManagementState(
                Kernel->hardware,
                Interface->u.SetPowerManagement.state));
        break;

    case gcvHAL_QUERY_POWER_MANAGEMENT_STATE:
        /* Chip is not idle. */
        Interface->u.QueryPowerManagement.isIdle = gcvFALSE;

        /* Query the power management state. */
        gcmkONERROR(gckHARDWARE_QueryPowerManagementState(
            Kernel->hardware,
            &Interface->u.QueryPowerManagement.state));

        /* Query the idle state. */
        gcmkONERROR(
            gckHARDWARE_QueryIdle(Kernel->hardware,
                                  &Interface->u.QueryPowerManagement.isIdle));
        break;

    case gcvHAL_READ_REGISTER:
#if gcdREGISTER_ACCESS_FROM_USER
        {
            gceCHIPPOWERSTATE power;
            gcmkONERROR(gckHARDWARE_QueryPowerManagementState(Kernel->hardware,
                                                              &power));

            if (power == gcvPOWER_ON)
            {
                /* Read a register. */
                gcmkONERROR(gckOS_ReadRegisterEx(
                    Kernel->os,
                    Kernel->core,
                    Interface->u.ReadRegisterData.address,
                    &Interface->u.ReadRegisterData.data));
            }
            else
            {
                /* Chip is in power-state. */
                Interface->u.ReadRegisterData.data = 0;
                status = gcvSTATUS_CHIP_NOT_READY;
            }
        }
#else
        /* No access from user land to read registers. */
        Interface->u.ReadRegisterData.data = 0;
        status = gcvSTATUS_NOT_SUPPORTED;
#endif
        break;

    case gcvHAL_WRITE_REGISTER:
#if gcdREGISTER_ACCESS_FROM_USER
        /* Write a register. */
        gcmkONERROR(
            gckOS_WriteRegisterEx(Kernel->os,
                                  Kernel->core,
                                  Interface->u.WriteRegisterData.address,
                                  Interface->u.WriteRegisterData.data));
#else
        /* No access from user land to write registers. */
        status = gcvSTATUS_NOT_SUPPORTED;
#endif
        break;

    case gcvHAL_READ_ALL_PROFILE_REGISTERS:
#if VIVANTE_PROFILER
        /* Read all 3D profile registers. */
        gcmkONERROR(
            gckHARDWARE_QueryProfileRegisters(
                Kernel->hardware,
                &Interface->u.RegisterProfileData.counters));
#else
        status = gcvSTATUS_OK;
#endif
        break;

    case gcvHAL_PROFILE_REGISTERS_2D:
#if VIVANTE_PROFILER
        /* Read all 2D profile registers. */
        gcmkONERROR(
            gckHARDWARE_ProfileEngine2D(
                Kernel->hardware,
                Interface->u.RegisterProfileData2D.hwProfile2D));
#else
        status = gcvSTATUS_OK;
#endif
        break;

    case gcvHAL_GET_PROFILE_SETTING:
        status = gcvSTATUS_OK;
        break;

    case gcvHAL_SET_PROFILE_SETTING:
        status = gcvSTATUS_OK;
        break;

    case gcvHAL_QUERY_KERNEL_SETTINGS:
        /* Get kernel settings. */
        gcmkONERROR(
            gckKERNEL_QuerySettings(Kernel,
                                    &Interface->u.QueryKernelSettings.settings));
        break;

    case gcvHAL_RESET:
        /* Reset the hardware. */
        gckKERNEL_Recovery(Kernel);
        break;

    case gcvHAL_DEBUG:
        /* Set debug level and zones. */
        if (Interface->u.Debug.set)
        {
            gckOS_SetDebugLevel(Interface->u.Debug.level);
            gckOS_SetDebugZones(Interface->u.Debug.zones,
                                Interface->u.Debug.enable);
        }

        if (Interface->u.Debug.message[0] != '\0')
        {
            /* Print a message to the debugger. */
            if (Interface->u.Debug.type == gcvMESSAGE_TEXT)
            {
               gckOS_CopyPrint(Interface->u.Debug.message);
            }
            else
            {
               gckOS_DumpBuffer(Kernel->os,
                                Interface->u.Debug.message,
                                Interface->u.Debug.messageSize,
                                gceDUMP_BUFFER_FROM_USER,
                                gcvTRUE);
            }
        }
        status = gcvSTATUS_OK;
        break;

    case gcvHAL_DUMP_GPU_STATE:
        /* Dump GPU state */
        {
            gceCHIPPOWERSTATE power;
            gcmkONERROR(gckHARDWARE_QueryPowerManagementState(Kernel->hardware,
                                                              &power));
            if (power == gcvPOWER_ON)
            {
                Interface->u.ReadRegisterData.data = 1;
                gcmkVERIFY_OK(
                    gckOS_DumpGPUState(Kernel->os, Kernel->core));
            }
            else
            {
                Interface->u.ReadRegisterData.data = 0;
                status = gcvSTATUS_CHIP_NOT_READY;
            }
        }
        break;

    case gcvHAL_DUMP_EVENT:
        /* Dump GPU event */
        gcmkVERIFY_OK(
            gckEVENT_Dump(Kernel->eventObj));
        break;

    case gcvHAL_CACHE:
        if (Interface->u.Cache.node == NULL)
        {
            /* FIXME Surface wrap some memory which is not allocated by us,
            ** So we don't have physical address to handle outer cache, ignore it*/
            status = gcvSTATUS_OK;
            break;
        }
        else if (Interface->u.Cache.node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
        {
            /* Video memory has no physical handles. */
            physical = NULL;
        }
        else
        {
            /* Grab physical handle. */
            physical = Interface->u.Cache.node->Virtual.physical;
        }

        switch(Interface->u.Cache.operation)
        {
        case gcvCACHE_FLUSH:
            /* Clean and invalidate the cache. */
            status = gckOS_CacheFlush(Kernel->os,
                                      processID,
                                      physical,
                                      paddr,
                                      Interface->u.Cache.logical,
                                      Interface->u.Cache.bytes);
            break;
        case gcvCACHE_CLEAN:
            /* Clean the cache. */
            status = gckOS_CacheClean(Kernel->os,
                                      processID,
                                      physical,
                                      paddr,
                                      Interface->u.Cache.logical,
                                      Interface->u.Cache.bytes);
            break;
        case gcvCACHE_INVALIDATE:
            /* Invalidate the cache. */
            status = gckOS_CacheInvalidate(Kernel->os,
                                           processID,
                                           physical,
                                           paddr,
                                           Interface->u.Cache.logical,
                                           Interface->u.Cache.bytes);
            break;

	case gcvCACHE_MEMORY_BARRIER:
	   status = gckOS_MemoryBarrier(Kernel->os,
                                        Interface->u.Cache.logical);
	   break;
        default:
            status = gcvSTATUS_INVALID_ARGUMENT;
            break;
        }
        break;

    case gcvHAL_TIMESTAMP:
        /* Check for invalid timer. */
        if ((Interface->u.TimeStamp.timer >= ARRAY_SIZE(Kernel->timers))
        ||  (Interface->u.TimeStamp.request != 2))
        {
            Interface->u.TimeStamp.timeDelta = 0;
            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }

        /* Return timer results and reset timer. */
        {
            gcsTIMER_PTR timer = &(Kernel->timers[Interface->u.TimeStamp.timer]);
            u64 timeDelta = 0;

            if (timer->stopTime < timer->startTime )
            {
                Interface->u.TimeStamp.timeDelta = 0;
                gcmkONERROR(gcvSTATUS_TIMER_OVERFLOW);
            }

            timeDelta = timer->stopTime - timer->startTime;

            /* Check truncation overflow. */
            Interface->u.TimeStamp.timeDelta = (s32) timeDelta;
			/*bit0~bit30 is available*/
            if (timeDelta>>31)
            {
                Interface->u.TimeStamp.timeDelta = 0;
                gcmkONERROR(gcvSTATUS_TIMER_OVERFLOW);
            }

            status = gcvSTATUS_OK;
        }
        break;

    case gcvHAL_DATABASE:
        /* Query video memory. */
        gcmkONERROR(
            gckKERNEL_QueryProcessDB(Kernel,
                                     Interface->u.Database.processID,
                                     !Interface->u.Database.validProcessID,
                                     gcvDB_VIDEO_MEMORY,
                                     &Interface->u.Database.vidMem));

        /* Query non-paged memory. */
        gcmkONERROR(
            gckKERNEL_QueryProcessDB(Kernel,
                                     Interface->u.Database.processID,
                                     !Interface->u.Database.validProcessID,
                                     gcvDB_NON_PAGED,
                                     &Interface->u.Database.nonPaged));

        /* Query contiguous memory. */
        gcmkONERROR(
            gckKERNEL_QueryProcessDB(Kernel,
                                     Interface->u.Database.processID,
                                     !Interface->u.Database.validProcessID,
                                     gcvDB_CONTIGUOUS,
                                     &Interface->u.Database.contiguous));

        /* Query GPU idle time. */
        gcmkONERROR(
            gckKERNEL_QueryProcessDB(Kernel,
                                     Interface->u.Database.processID,
                                     !Interface->u.Database.validProcessID,
                                     gcvDB_IDLE,
                                     &Interface->u.Database.gpuIdle));
        break;

    case gcvHAL_VERSION:
        Interface->u.Version.major = gcvVERSION_MAJOR;
        Interface->u.Version.minor = gcvVERSION_MINOR;
        Interface->u.Version.patch = gcvVERSION_PATCH;
        Interface->u.Version.build = gcvVERSION_BUILD;
#if gcmIS_DEBUG(gcdDEBUG_TRACE)
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_KERNEL,
                       "KERNEL version %d.%d.%d build %u %s %s",
                       gcvVERSION_MAJOR, gcvVERSION_MINOR, gcvVERSION_PATCH,
                       gcvVERSION_BUILD, gcvVERSION_DATE, gcvVERSION_TIME);
#endif
        break;

    case gcvHAL_CHIP_INFO:
        /* Only if not support multi-core */
        Interface->u.ChipInfo.count = 1;
        Interface->u.ChipInfo.types[0] = Kernel->hardware->type;
        break;

    case gcvHAL_ATTACH:
        /* Attach user process. */
        gcmkONERROR(
            gckCOMMAND_Attach(Kernel->command,
                              &Interface->u.Attach.context,
                              &Interface->u.Attach.stateCount,
                              processID));

        gcmkVERIFY_OK(
            gckKERNEL_AddProcessDB(Kernel,
                                   processID, gcvDB_CONTEXT,
                                   Interface->u.Attach.context,
                                   NULL,
                                   0));
        break;

    case gcvHAL_DETACH:
        /* Detach user process. */
        gcmkONERROR(
            gckCOMMAND_Detach(Kernel->command,
                              Interface->u.Detach.context));

        gcmkVERIFY_OK(
            gckKERNEL_RemoveProcessDB(Kernel,
                              processID, gcvDB_CONTEXT,
                              Interface->u.Detach.context));
        break;

    case gcvHAL_COMPOSE:
        /* Start composition. */
        gcmkONERROR(
            gckEVENT_Compose(Kernel->eventObj,
                             &Interface->u.Compose));
        break;

    case gcvHAL_SET_TIMEOUT:
         /* set timeOut value from user */
         gckKERNEL_SetTimeOut(Kernel, Interface->u.SetTimeOut.timeOut);
        break;

#if gcdFRAME_DB
    case gcvHAL_GET_FRAME_INFO:
        gcmkONERROR(gckHARDWARE_GetFrameInfo(
            Kernel->hardware,
            Interface->u.GetFrameInfo.frameInfo));
        break;
#endif

    case gcvHAL_GET_SHARED_INFO:
        if (Interface->u.GetSharedInfo.dataId != 0)
        {
            gcmkONERROR(gckKERNEL_FindProcessDB(Kernel,
                        Interface->u.GetSharedInfo.pid,
                        0,
                        gcvDB_SHARED_INFO,
                        gcmINT2PTR(Interface->u.GetSharedInfo.dataId),
                        &record));

            /* find a record in db, check size */
            if (record.bytes != Interface->u.GetSharedInfo.size)
            {
                /* Size change is not allowed */
                gcmkONERROR(gcvSTATUS_INVALID_DATA);
            }

            /* fetch data */
            if (copy_to_user(Interface->u.GetSharedInfo.data, record.physical, Interface->u.GetSharedInfo.size) != 0)
            {
                gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
            }
        }

        if ((node = Interface->u.GetSharedInfo.node) != NULL)
        {
            switch (Interface->u.GetSharedInfo.infoType)
                {
                case gcvVIDMEM_INFO_GENERIC:
                    { /* Generic data stored */
                        if (node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
                        {
                            data = &node->VidMem.sharedInfo;

                        }
                        else
                        {
                            data = &node->Virtual.sharedInfo;
                        }

                        if (copy_to_user(Interface->u.GetSharedInfo.nodeData, data, sizeof(gcsVIDMEM_NODE_SHARED_INFO)) != 0)
                        {
                            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
                        }
                    }
                    break;

                case gcvVIDMEM_INFO_DIRTY_RECTANGLE:
                    { /* Dirty rectangle stored */
                        gcsVIDMEM_NODE_SHARED_INFO *storedSharedInfo;
                        gcsVIDMEM_NODE_SHARED_INFO alignedSharedInfo;

                        if (node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
                        {
                            storedSharedInfo = &node->VidMem.sharedInfo;
                        }
                        else
                        {
                            storedSharedInfo = &node->Virtual.sharedInfo;
                        }

                        /* Stored shared info holds the unaligned dirty rectangle.
                           Align it first.                                         */

                        /* Hardware requires 64-byte aligned address, and 16x4 pixel aligned rectsize.
                           We simply align to 32 pixels which covers both 16- and 32-bpp formats. */

                        /* Make sure we have a legit rectangle. */
                        gcmkASSERT((storedSharedInfo->RectSize.width != 0) && (storedSharedInfo->RectSize.height != 0));

                        alignedSharedInfo.SrcOrigin.x = gcmALIGN_BASE(storedSharedInfo->SrcOrigin.x, 32);
                        alignedSharedInfo.RectSize.width = gcmALIGN((storedSharedInfo->RectSize.width + (storedSharedInfo->SrcOrigin.x - alignedSharedInfo.SrcOrigin.x)), 16);

                        alignedSharedInfo.SrcOrigin.y = gcmALIGN_BASE(storedSharedInfo->SrcOrigin.y, 4);
                        alignedSharedInfo.RectSize.height = gcmALIGN((storedSharedInfo->RectSize.height + (storedSharedInfo->SrcOrigin.y - alignedSharedInfo.SrcOrigin.y)), 4);

                        if (copy_to_user(Interface->u.GetSharedInfo.nodeData, &alignedSharedInfo, sizeof(gcsVIDMEM_NODE_SHARED_INFO)) != 0)
                        {
                            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
                        }

                        gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_KERNEL,
                                        "Node = %p, unaligned rectangle (l=%d, t=%d, w=%d, h=%d) aligned to (l=%d, t=%d, w=%d, h=%d)", node,
                                        storedSharedInfo->SrcOrigin.x, storedSharedInfo->SrcOrigin.y,
                                        storedSharedInfo->RectSize.width, storedSharedInfo->RectSize.height,
                                        alignedSharedInfo.SrcOrigin.x, alignedSharedInfo.SrcOrigin.y,
                                        alignedSharedInfo.RectSize.width, alignedSharedInfo.RectSize.height);

                        /* Rectangle */
                        storedSharedInfo->SrcOrigin.x =
                        storedSharedInfo->SrcOrigin.y =
                        storedSharedInfo->RectSize.width =
                        storedSharedInfo->RectSize.height = 0;
                    }
                    break;
                }
        }
        break;

    case gcvHAL_SET_SHARED_INFO:
        if (Interface->u.SetSharedInfo.dataId != 0)
        {
            status = gckKERNEL_FindProcessDB(Kernel, processID, 0,
                        gcvDB_SHARED_INFO,
                        gcmINT2PTR(Interface->u.SetSharedInfo.dataId),
                        &record);

            if (status == gcvSTATUS_INVALID_DATA)
            {
                /* private data has not been created yet */
                /* Note: we count on DestoryProcessDB to free it */
                gcmkONERROR(gckOS_AllocateMemory(
                    Kernel->os,
                    Interface->u.SetSharedInfo.size,
                    &data
                    ));

                gcmkONERROR(
                    gckKERNEL_AddProcessDB(Kernel, processID,
                        gcvDB_SHARED_INFO,
                        gcmINT2PTR(Interface->u.SetSharedInfo.dataId),
                        data,
                        Interface->u.SetSharedInfo.size
                        ));
            }
            else
            {
                /* bail on other errors */
                gcmkONERROR(status);

                /* find a record in db, check size */
                if (record.bytes != Interface->u.SetSharedInfo.size)
                {
                    /* Size change is not allowed */
                    gcmkONERROR(gcvSTATUS_INVALID_DATA);
                }

                /* get storage address */
                data = record.physical;
            }

            if (copy_from_user(data, Interface->u.SetSharedInfo.data, Interface->u.SetSharedInfo.size) != 0)
            {
                gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
            }
        }

        if ((node = Interface->u.SetSharedInfo.node) != NULL)
        {
            switch (Interface->u.SetSharedInfo.infoType)
                {
                case gcvVIDMEM_INFO_GENERIC:
                    { /* Generic data stored */
                        if (node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
                        {
                            data = &node->VidMem.sharedInfo;
                        }
                        else
                        {
                            data = &node->Virtual.sharedInfo;
                        }

                        if (copy_from_user(data, Interface->u.SetSharedInfo.nodeData, sizeof(gcsVIDMEM_NODE_SHARED_INFO)) != 0)
                        {
                            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
                        }
                    }
                    break;

                case gcvVIDMEM_INFO_DIRTY_RECTANGLE:
                    { /* Dirty rectangle stored */
                        gcsVIDMEM_NODE_SHARED_INFO newSharedInfo;
                        gcsVIDMEM_NODE_SHARED_INFO *currentSharedInfo;
                        int dirtyX, dirtyY, right, bottom;

                        /* Expand the dirty rectangle stored in the node to include the rectangle passed in. */
                        if (copy_from_user(&newSharedInfo, Interface->u.SetSharedInfo.nodeData, sizeof(gcsVIDMEM_NODE_SHARED_INFO)) != 0)
                        {
                            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
                        }

                        if (node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
                        {
                            currentSharedInfo = &node->VidMem.sharedInfo;
                        }
                        else
                        {
                            currentSharedInfo = &node->Virtual.sharedInfo;
                        }

                        gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_KERNEL, "Node = %p Stored rectangle (l=%d, t=%d, w=%d, h=%d)", node,
                                        currentSharedInfo->SrcOrigin.x, currentSharedInfo->SrcOrigin.y,
                                        currentSharedInfo->RectSize.width, currentSharedInfo->RectSize.height);

                        gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_KERNEL, "To combine with (l=%d, t=%d, w=%d, h=%d)",
                                        newSharedInfo.SrcOrigin.x, newSharedInfo.SrcOrigin.y,
                                        newSharedInfo.RectSize.width, newSharedInfo.RectSize.height);

                        if ((currentSharedInfo->RectSize.width == 0) || (currentSharedInfo->RectSize.height == 0))
                        { /* Setting it for the first time */
                            currentSharedInfo->SrcOrigin.x = newSharedInfo.SrcOrigin.x;
                            currentSharedInfo->SrcOrigin.y = newSharedInfo.SrcOrigin.y;
                            currentSharedInfo->RectSize.width = newSharedInfo.RectSize.width;
                            currentSharedInfo->RectSize.height = newSharedInfo.RectSize.height;
                        }
                        else
                        {
                            /* Expand the stored rectangle to include newly locked rectangle */
                            dirtyX = (newSharedInfo.SrcOrigin.x < currentSharedInfo->SrcOrigin.x) ? newSharedInfo.SrcOrigin.x : currentSharedInfo->SrcOrigin.x;
                            right = max(currentSharedInfo->SrcOrigin.x + currentSharedInfo->RectSize.width, newSharedInfo.SrcOrigin.x + newSharedInfo.RectSize.width);
                            currentSharedInfo->RectSize.width = right - dirtyX;
                            currentSharedInfo->SrcOrigin.x = dirtyX;

                            dirtyY = (newSharedInfo.SrcOrigin.y < currentSharedInfo->SrcOrigin.y) ? newSharedInfo.SrcOrigin.y : currentSharedInfo->SrcOrigin.y;
                            bottom = max(currentSharedInfo->SrcOrigin.y + currentSharedInfo->RectSize.height, newSharedInfo.SrcOrigin.y + newSharedInfo.RectSize.height);
                            currentSharedInfo->RectSize.height = bottom - dirtyY;
                            currentSharedInfo->SrcOrigin.y = dirtyY;
                        }

                        gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_KERNEL, "Combined rectangle (l=%d, t=%d, w=%d, h=%d)",
                                       currentSharedInfo->SrcOrigin.x, currentSharedInfo->SrcOrigin.y,
                                       currentSharedInfo->RectSize.width, currentSharedInfo->RectSize.height);
                    }
                    break;
                }
        }

        break;

    default:
        /* Invalid command. */
        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

OnError:
    /* Save status. */
    Interface->status = status;

    if (gcmIS_ERROR(status))
    {
        if (locked)
        {
            /* Roll back the lock. */
            gcmkVERIFY_OK(
                gckVIDMEM_Unlock(Kernel,
                                 Interface->u.LockVideoMemory.node,
                                 gcvSURF_TYPE_UNKNOWN,
                                 &asynchronous));

            if (gcvTRUE == asynchronous)
            {
                /* Bottom Half */
                gcmkVERIFY_OK(
                    gckVIDMEM_Unlock(Kernel,
                                     Interface->u.LockVideoMemory.node,
                                     gcvSURF_TYPE_UNKNOWN,
                                     NULL));
            }
        }
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**  gckKERNEL_AttachProcess
**
**  Attach or detach a process.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      int Attach
**          gcvTRUE if a new process gets attached or gcFALSE when a process
**          gets detatched.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckKERNEL_AttachProcess(
    IN gckKERNEL Kernel,
    IN int Attach
    )
{
    gceSTATUS status;
    u32 processID;

    gcmkHEADER_ARG("Kernel=0x%x Attach=%d", Kernel, Attach);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    /* Get current process ID. */
    processID = task_tgid_vnr(current);

    gcmkONERROR(gckKERNEL_AttachProcessEx(Kernel, Attach, processID));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**  gckKERNEL_AttachProcessEx
**
**  Attach or detach a process with the given PID. Can be paired with gckKERNEL_AttachProcess
**     provided the programmer is aware of the consequences.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      int Attach
**          gcvTRUE if a new process gets attached or gcFALSE when a process
**          gets detatched.
**
**      u32 PID
**          PID of the process to attach or detach.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckKERNEL_AttachProcessEx(
    IN gckKERNEL Kernel,
    IN int Attach,
    IN u32 PID
    )
{
    gceSTATUS status;
    s32 old;

    gcmkHEADER_ARG("Kernel=0x%x Attach=%d PID=%d", Kernel, Attach, PID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    if (Attach)
    {
        /* Increment the number of clients attached. */
        gcmkONERROR(
            gckOS_AtomIncrement(Kernel->os, Kernel->atomClients, &old));

        if (old == 0)
        {
            gcmkONERROR(gckOS_Broadcast(Kernel->os,
                                        Kernel->hardware,
                                        gcvBROADCAST_FIRST_PROCESS));
        }

        if (Kernel->dbCreated)
        {
            /* Create the process database. */
            gcmkONERROR(gckKERNEL_CreateProcessDB(Kernel, PID));
        }
    }
    else
    {
        if (Kernel->dbCreated)
        {
            /* Clean up the process database. */
            gcmkONERROR(gckKERNEL_DestroyProcessDB(Kernel, PID));

            /* Save the last know process ID. */
            Kernel->db->lastProcessID = PID;
        }

        /* Decrement the number of clients attached. */
        gcmkONERROR(
            gckOS_AtomDecrement(Kernel->os, Kernel->atomClients, &old));

        if (old == 1)
        {
            /* Last client detached, switch to SUSPEND power state. */
            gcmkONERROR(gckOS_Broadcast(Kernel->os,
                                        Kernel->hardware,
                                        gcvBROADCAST_LAST_PROCESS));

            /* Flush the debug cache. */
            gcmkDEBUGFLUSH(~0U);
        }
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if gcdSECURE_USER
gceSTATUS
gckKERNEL_MapLogicalToPhysical(
    IN gckKERNEL Kernel,
    IN gcskSECURE_CACHE_PTR Cache,
    IN OUT void **Data
    )
{
    gceSTATUS status;
    static int baseAddressValid = gcvFALSE;
    static u32 baseAddress;
    int needBase;
    gcskLOGICAL_CACHE_PTR slot;

    gcmkHEADER_ARG("Kernel=0x%x Cache=0x%x *Data=0x%x",
                   Kernel, Cache, gcmOPT_POINTER(Data));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    if (!baseAddressValid)
    {
        /* Get base address. */
        gcmkONERROR(gckHARDWARE_GetBaseAddress(Kernel->hardware, &baseAddress));

        baseAddressValid = gcvTRUE;
    }

    /* Does this state load need a base address? */
    gcmkONERROR(gckHARDWARE_NeedBaseAddress(Kernel->hardware,
                                            ((u32 *) Data)[-1],
                                            &needBase));

#if gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_LRU
    {
        gcskLOGICAL_CACHE_PTR next;
        int i;

        /* Walk all used cache slots. */
        for (i = 1, slot = Cache->cache[0].next, next = NULL;
             (i <= gcdSECURE_CACHE_SLOTS) && (slot->logical != NULL);
             ++i, slot = slot->next
        )
        {
            if (slot->logical == *Data)
            {
                /* Bail out. */
                next = slot;
                break;
            }
        }

        /* See if we had a miss. */
        if (next == NULL)
        {
            /* Use the tail of the cache. */
            slot = Cache->cache[0].prev;

            /* Initialize the cache line. */
            slot->logical = *Data;

            /* Map the logical address to a DMA address. */
            gcmkONERROR(
                gckOS_GetPhysicalAddress(Kernel->os, *Data, &slot->dma));
        }

        /* Move slot to head of list. */
        if (slot != Cache->cache[0].next)
        {
            /* Unlink. */
            slot->prev->next = slot->next;
            slot->next->prev = slot->prev;

            /* Move to head of chain. */
            slot->prev       = &Cache->cache[0];
            slot->next       = Cache->cache[0].next;
            slot->prev->next = slot;
            slot->next->prev = slot;
        }
    }
#elif gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_LINEAR
    {
        int i;
        gcskLOGICAL_CACHE_PTR next = NULL;
        gcskLOGICAL_CACHE_PTR oldestSlot = NULL;
        slot = NULL;

        if (Cache->cacheIndex != NULL)
        {
            /* Walk the cache forwards. */
            for (i = 1, slot = Cache->cacheIndex;
                 (i <= gcdSECURE_CACHE_SLOTS) && (slot->logical != NULL);
                 ++i, slot = slot->next)
            {
                if (slot->logical == *Data)
                {
                    /* Bail out. */
                    next = slot;
                    break;
                }

                /* Determine age of this slot. */
                if ((oldestSlot       == NULL)
                ||  (oldestSlot->stamp > slot->stamp)
                )
                {
                    oldestSlot = slot;
                }
            }

            if (next == NULL)
            {
                /* Walk the cache backwards. */
                for (slot = Cache->cacheIndex->prev;
                     (i <= gcdSECURE_CACHE_SLOTS) && (slot->logical != NULL);
                     ++i, slot = slot->prev)
                {
                    if (slot->logical == *Data)
                    {
                        /* Bail out. */
                        next = slot;
                        break;
                    }

                    /* Determine age of this slot. */
                    if ((oldestSlot       == NULL)
                    ||  (oldestSlot->stamp > slot->stamp)
                    )
                    {
                        oldestSlot = slot;
                    }
                }
            }
        }

        /* See if we had a miss. */
        if (next == NULL)
        {
            if (Cache->cacheFree != 0)
            {
                slot = &Cache->cache[Cache->cacheFree];
                gcmkASSERT(slot->logical == NULL);

                ++ Cache->cacheFree;
                if (Cache->cacheFree >= ARRAY_SIZE(Cache->cache))
                {
                    Cache->cacheFree = 0;
                }
            }
            else
            {
                /* Use the oldest cache slot. */
                gcmkASSERT(oldestSlot != NULL);
                slot = oldestSlot;

                /* Unlink from the chain. */
                slot->prev->next = slot->next;
                slot->next->prev = slot->prev;

                /* Append to the end. */
                slot->prev       = Cache->cache[0].prev;
                slot->next       = &Cache->cache[0];
                slot->prev->next = slot;
                slot->next->prev = slot;
            }

            /* Initialize the cache line. */
            slot->logical = *Data;

            /* Map the logical address to a DMA address. */
            gcmkONERROR(
                gckOS_GetPhysicalAddress(Kernel->os, *Data, &slot->dma));
        }

        /* Save time stamp. */
        slot->stamp = ++ Cache->cacheStamp;

        /* Save current slot for next lookup. */
        Cache->cacheIndex = slot;
    }
#elif gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_HASH
    {
        int i;
        u32 data = gcmPTR2INT(*Data);
        u32 key, index;
        gcskLOGICAL_CACHE_PTR hash;

        /* Generate a hash key. */
        key   = (data >> 24) + (data >> 16) + (data >> 8) + data;
        index = key % ARRAY_SIZE(Cache->hash);

        /* Get the hash entry. */
        hash = &Cache->hash[index];

        for (slot = hash->nextHash, i = 0;
             (slot != NULL) && (i < gcdSECURE_CACHE_SLOTS);
             slot = slot->nextHash, ++i
        )
        {
            if (slot->logical == (*Data))
            {
                break;
            }
        }

        if (slot == NULL)
        {
            /* Grab from the tail of the cache. */
            slot = Cache->cache[0].prev;

            /* Unlink slot from any hash table it is part of. */
            if (slot->prevHash != NULL)
            {
                slot->prevHash->nextHash = slot->nextHash;
            }
            if (slot->nextHash != NULL)
            {
                slot->nextHash->prevHash = slot->prevHash;
            }

            /* Initialize the cache line. */
            slot->logical = *Data;

            /* Map the logical address to a DMA address. */
            gcmkONERROR(
                gckOS_GetPhysicalAddress(Kernel->os, *Data, &slot->dma));

            if (hash->nextHash != NULL)
            {
                gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_KERNEL,
                               "Hash Collision: logical=0x%x key=0x%08x",
                               *Data, key);
            }

            /* Insert the slot at the head of the hash list. */
            slot->nextHash     = hash->nextHash;
            if (slot->nextHash != NULL)
            {
                slot->nextHash->prevHash = slot;
            }
            slot->prevHash     = hash;
            hash->nextHash     = slot;
        }

        /* Move slot to head of list. */
        if (slot != Cache->cache[0].next)
        {
            /* Unlink. */
            slot->prev->next = slot->next;
            slot->next->prev = slot->prev;

            /* Move to head of chain. */
            slot->prev       = &Cache->cache[0];
            slot->next       = Cache->cache[0].next;
            slot->prev->next = slot;
            slot->next->prev = slot;
        }
    }
#elif gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_TABLE
    {
        u32 index = (gcmPTR2INT(*Data) % gcdSECURE_CACHE_SLOTS) + 1;

        /* Get cache slot. */
        slot = &Cache->cache[index];

        /* Check for cache miss. */
        if (slot->logical != *Data)
        {
            /* Initialize the cache line. */
            slot->logical = *Data;

            /* Map the logical address to a DMA address. */
            gcmkONERROR(
                gckOS_GetPhysicalAddress(Kernel->os, *Data, &slot->dma));
        }
    }
#endif

    /* Return DMA address. */
    *Data = gcmINT2PTR(slot->dma + (needBase ? baseAddress : 0));

    /* Success. */
    gcmkFOOTER_ARG("*Data=0x%08x", *Data);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckKERNEL_FlushTranslationCache(
    IN gckKERNEL Kernel,
    IN gcskSECURE_CACHE_PTR Cache,
    IN void *Logical,
    IN size_t Bytes
    )
{
    int i;
    gcskLOGICAL_CACHE_PTR slot;
    u8 *ptr;

    gcmkHEADER_ARG("Kernel=0x%x Cache=0x%x Logical=0x%x Bytes=%lu",
                   Kernel, Cache, Logical, Bytes);

    /* Do we need to flush the entire cache? */
    if (Logical == NULL)
    {
        /* Clear all cache slots. */
        for (i = 1; i <= gcdSECURE_CACHE_SLOTS; ++i)
        {
            Cache->cache[i].logical  = NULL;

#if gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_HASH
            Cache->cache[i].nextHash = NULL;
            Cache->cache[i].prevHash = NULL;
#endif
}

#if gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_HASH
        /* Zero the hash table. */
        for (i = 0; i < ARRAY_SIZE(Cache->hash); ++i)
        {
            Cache->hash[i].nextHash = NULL;
        }
#endif

        /* Reset the cache functionality. */
        Cache->cacheIndex = NULL;
        Cache->cacheFree  = 1;
        Cache->cacheStamp = 0;
    }

    else
    {
        u8 *low  = (u8 *) Logical;
        u8 *high = low + Bytes;

#if gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_LRU
        gcskLOGICAL_CACHE_PTR next;

        /* Walk all used cache slots. */
        for (i = 1, slot = Cache->cache[0].next;
             (i <= gcdSECURE_CACHE_SLOTS) && (slot->logical != NULL);
             ++i, slot = next
        )
        {
            /* Save pointer to next slot. */
            next = slot->next;

            /* Test if this slot falls within the range to flush. */
            ptr = (u8 *) slot->logical;
            if ((ptr >= low) && (ptr < high))
            {
                /* Unlink slot. */
                slot->prev->next = slot->next;
                slot->next->prev = slot->prev;

                /* Append slot to tail of cache. */
                slot->prev       = Cache->cache[0].prev;
                slot->next       = &Cache->cache[0];
                slot->prev->next = slot;
                slot->next->prev = slot;

                /* Mark slot as empty. */
                slot->logical = NULL;
            }
        }

#elif gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_LINEAR
        gcskLOGICAL_CACHE_PTR next;

        for (i = 1, slot = Cache->cache[0].next;
             (i <= gcdSECURE_CACHE_SLOTS) && (slot->logical != NULL);
             ++i, slot = next)
        {
            /* Save pointer to next slot. */
            next = slot->next;

            /* Test if this slot falls within the range to flush. */
            ptr = (u8 *) slot->logical;
            if ((ptr >= low) && (ptr < high))
            {
                /* Test if this slot is the current slot. */
                if (slot == Cache->cacheIndex)
                {
                    /* Move to next or previous slot. */
                    Cache->cacheIndex = (slot->next->logical != NULL)
                                      ? slot->next
                                      : (slot->prev->logical != NULL)
                                      ? slot->prev
                                      : NULL;
                }

                /* Unlink slot from cache. */
                slot->prev->next = slot->next;
                slot->next->prev = slot->prev;

                /* Insert slot to head of cache. */
                slot->prev       = &Cache->cache[0];
                slot->next       = Cache->cache[0].next;
                slot->prev->next = slot;
                slot->next->prev = slot;

                /* Mark slot as empty. */
                slot->logical = NULL;
                slot->stamp   = 0;
            }
        }

#elif gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_HASH
        int j;
        gcskLOGICAL_CACHE_PTR hash, next;

        /* Walk all hash tables. */
        for (i = 0, hash = Cache->hash;
             i < ARRAY_SIZE(Cache->hash);
             ++i, ++hash)
        {
            /* Walk all slots in the hash. */
            for (j = 0, slot = hash->nextHash;
                 (j < gcdSECURE_CACHE_SLOTS) && (slot != NULL);
                 ++j, slot = next)
            {
                /* Save pointer to next slot. */
                next = slot->next;

                /* Test if this slot falls within the range to flush. */
                ptr = (u8 *) slot->logical;
                if ((ptr >= low) && (ptr < high))
                {
                    /* Unlink slot from hash table. */
                    if (slot->prevHash == hash)
                    {
                        hash->nextHash = slot->nextHash;
                    }
                    else
                    {
                        slot->prevHash->nextHash = slot->nextHash;
                    }

                    if (slot->nextHash != NULL)
                    {
                        slot->nextHash->prevHash = slot->prevHash;
                    }

                    /* Unlink slot from cache. */
                    slot->prev->next = slot->next;
                    slot->next->prev = slot->prev;

                    /* Append slot to tail of cache. */
                    slot->prev       = Cache->cache[0].prev;
                    slot->next       = &Cache->cache[0];
                    slot->prev->next = slot;
                    slot->next->prev = slot;

                    /* Mark slot as empty. */
                    slot->logical  = NULL;
                    slot->prevHash = NULL;
                    slot->nextHash = NULL;
                }
            }
        }

#elif gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_TABLE
        u32 index;

        /* Loop while inside the range. */
        for (i = 1; (low < high) && (i <= gcdSECURE_CACHE_SLOTS); ++i)
        {
            /* Get index into cache for this range. */
            index = (gcmPTR2INT(low) % gcdSECURE_CACHE_SLOTS) + 1;
            slot  = &Cache->cache[index];

            /* Test if this slot falls within the range to flush. */
            ptr = (u8 *) slot->logical;
            if ((ptr >= low) && (ptr < high))
            {
                /* Remove entry from cache. */
                slot->logical = NULL;
            }

            /* Next block. */
            low += gcdSECURE_CACHE_SLOTS;
        }
#endif
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}
#endif

/*******************************************************************************
**
**  gckKERNEL_Recovery
**
**  Try to recover the GPU from a fatal error.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckKERNEL_Recovery(
    IN gckKERNEL Kernel
    )
{
#if gcdENABLE_RECOVERY
    gceSTATUS status;
    gckEVENT eventObj;
    gckHARDWARE hardware;
#if gcdSECURE_USER
    u32 processID;
    gcskSECURE_CACHE_PTR cache;
#endif

    gcmkHEADER_ARG("Kernel=0x%x", Kernel);

    /* Validate the arguemnts. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);

    /* Grab gckEVENT object. */
    eventObj = Kernel->eventObj;
    gcmkVERIFY_OBJECT(eventObj, gcvOBJ_EVENT);

    /* Grab gckHARDWARE object. */
    hardware = Kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    /* Handle all outstanding events now. */
#ifdef CONFIG_SMP
    gcmkONERROR(gckOS_AtomSet(Kernel->os, eventObj->pending, ~0U));
#else
    eventObj->pending = ~0U;
#endif
    gcmkONERROR(gckEVENT_Notify(eventObj, 1));

    /* Again in case more events got submitted. */
#ifdef CONFIG_SMP
    gcmkONERROR(gckOS_AtomSet(Kernel->os, eventObj->pending, ~0U));
#else
    eventObj->pending = ~0U;
#endif
    gcmkONERROR(gckEVENT_Notify(eventObj, 2));

#if gcdSECURE_USER
    /* Flush the secure mapping cache. */
    processID = task_tgid_vnr(current);
    gcmkONERROR(gckKERNEL_GetProcessDBCache(Kernel, processID, &cache));
    gcmkONERROR(gckKERNEL_FlushTranslationCache(Kernel, cache, NULL, 0));
#endif

    /* Try issuing a soft reset for the GPU. */
    status = gckHARDWARE_Reset(hardware);
    if (status == gcvSTATUS_NOT_SUPPORTED)
    {
        /* Switch to OFF power.  The next submit should return the GPU to ON
        ** state. */
        gcmkONERROR(
            gckHARDWARE_SetPowerManagementState(hardware,
                                                gcvPOWER_OFF_RECOVERY));
    }
    else
    {
        /* Bail out on reset error. */
        gcmkONERROR(status);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
#else
    return gcvSTATUS_OK;
#endif
}

/*******************************************************************************
**
**  gckKERNEL_OpenUserData
**
**  Get access to the user data.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      int NeedCopy
**          The flag indicating whether or not the data should be copied.
**
**      void *StaticStorage
**          Pointer to the kernel storage where the data is to be copied if
**          NeedCopy is gcvTRUE.
**
**      void *UserPointer
**          User pointer to the data.
**
**      size_t Size
**          Size of the data.
**
**  OUTPUT:
**
**      void ** KernelPointer
**          Pointer to the kernel pointer that will be pointing to the data.
*/
gceSTATUS
gckKERNEL_OpenUserData(
    IN gckKERNEL Kernel,
    IN int NeedCopy,
    IN void *StaticStorage,
    IN void *UserPointer,
    IN size_t Size,
    OUT void **KernelPointer
    )
{
    gceSTATUS status = gcvSTATUS_OK;

    gcmkHEADER_ARG(
        "Kernel=0x%08X NeedCopy=%d StaticStorage=0x%08X "
        "UserPointer=0x%08X Size=%lu KernelPointer=0x%08X",
        Kernel, NeedCopy, StaticStorage, UserPointer, Size, KernelPointer
        );

    /* Validate the arguemnts. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(!NeedCopy || (StaticStorage != NULL));
    gcmkVERIFY_ARGUMENT(UserPointer != NULL);
    gcmkVERIFY_ARGUMENT(KernelPointer != NULL);
    gcmkVERIFY_ARGUMENT(Size > 0);

    if (NeedCopy)
    {
        /* Copy the user data to the static storage. */
        if (copy_from_user(StaticStorage, UserPointer, Size) != 0)
        {
            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
        }

        /* Set the kernel pointer. */
        * KernelPointer = StaticStorage;
    }
    else
    {
        void *pointer = NULL;

        /* Map the user pointer. */
        gcmkONERROR(gckOS_MapUserPointer(
            Kernel->os, UserPointer, Size, &pointer
            ));

        /* Set the kernel pointer. */
        * KernelPointer = pointer;
    }

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckKERNEL_CloseUserData
**
**  Release resources associated with the user data connection opened by
**  gckKERNEL_OpenUserData.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**      int NeedCopy
**          The flag indicating whether or not the data should be copied.
**
**      int FlushData
**          If gcvTRUE, the data is written back to the user.
**
**      void *UserPointer
**          User pointer to the data.
**
**      size_t Size
**          Size of the data.
**
**  OUTPUT:
**
**      void ** KernelPointer
**          Kernel pointer to the data.
*/
gceSTATUS
gckKERNEL_CloseUserData(
    IN gckKERNEL Kernel,
    IN int NeedCopy,
    IN int FlushData,
    IN void *UserPointer,
    IN size_t Size,
    OUT void **KernelPointer
    )
{
    gceSTATUS status = gcvSTATUS_OK;
    void *pointer;

    gcmkHEADER_ARG(
        "Kernel=0x%08X NeedCopy=%d FlushData=%d "
        "UserPointer=0x%08X Size=%lu KernelPointer=0x%08X",
        Kernel, NeedCopy, FlushData, UserPointer, Size, KernelPointer
        );

    /* Validate the arguemnts. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(UserPointer != NULL);
    gcmkVERIFY_ARGUMENT(KernelPointer != NULL);
    gcmkVERIFY_ARGUMENT(Size > 0);

    /* Get a shortcut to the kernel pointer. */
    pointer = * KernelPointer;

    if (pointer != NULL)
    {
        if (NeedCopy)
        {
            if (FlushData)
            {
                if (copy_to_user(UserPointer, * KernelPointer, Size) != 0)
                {
                    gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
                }
            }
        }
        else
        {
            /* Unmap record from kernel memory. */
            gcmkONERROR(gckOS_UnmapUserPointer(
                Kernel->os,
                UserPointer,
                Size,
                * KernelPointer
                ));
        }

        /* Reset the kernel pointer. */
        * KernelPointer = NULL;
    }

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}



/*******************************************************************************
***** Test Code ****************************************************************
*******************************************************************************/
