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
#include "gc_hal_kernel_context.h"

#include <linux/sched.h>
#include <asm/uaccess.h>

#define _GC_OBJ_ZONE            gcvZONE_COMMAND

/* When enabled, extra messages needed by the dump parser are left out. */
#define gcdSIMPLE_COMMAND_DUMP  1

/******************************************************************************\
********************************* Support Code *********************************
\******************************************************************************/

/*******************************************************************************
**
**  _NewQueue
**
**  Allocate a new command queue.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object.
**
**  OUTPUT:
**
**      gckCOMMAND Command
**          gckCOMMAND object has been updated with a new command queue.
*/
static gceSTATUS
_NewQueue(
    IN OUT gckCOMMAND Command
    )
{
    gceSTATUS status;
    int currentIndex, newIndex;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Switch to the next command buffer. */
    currentIndex = Command->index;
    newIndex     = (currentIndex + 1) % gcdCOMMAND_QUEUES;

    /* Wait for availability. */
#if gcdDUMP_COMMAND && !gcdSIMPLE_COMMAND_DUMP
    gcmkPRINT("@[kernel.waitsignal]");
#endif

    gcmkONERROR(gckOS_WaitSignal(
        Command->os,
        Command->queues[newIndex].signal,
        gcvINFINITE
        ));

#if gcmIS_DEBUG(gcdDEBUG_TRACE)
    if (newIndex < currentIndex)
    {
        Command->wrapCount += 1;

        gcmkTRACE_ZONE_N(
            gcvLEVEL_INFO, gcvZONE_COMMAND,
            2 * 4,
            "%s(%d): queue array wrapped around.\n",
            __FUNCTION__, __LINE__
            );
    }

    gcmkTRACE_ZONE_N(
        gcvLEVEL_INFO, gcvZONE_COMMAND,
        3 * 4,
        "%s(%d): total queue wrap arounds %d.\n",
        __FUNCTION__, __LINE__, Command->wrapCount
        );

    gcmkTRACE_ZONE_N(
        gcvLEVEL_INFO, gcvZONE_COMMAND,
        3 * 4,
        "%s(%d): switched to queue %d.\n",
        __FUNCTION__, __LINE__, newIndex
        );
#endif

    /* Update gckCOMMAND object with new command queue. */
    Command->index    = newIndex;
    Command->newQueue = gcvTRUE;
    Command->logical  = Command->queues[newIndex].logical;
    Command->offset   = 0;

    gcmkONERROR(
        gckOS_GetPhysicalAddress(
            Command->os,
            Command->logical,
            (u32 *) &Command->physical
            ));

    if (currentIndex != -1)
    {
        /* Mark the command queue as available. */
        gcmkONERROR(gckEVENT_Signal(
            Command->kernel->eventObj,
            Command->queues[currentIndex].signal,
            gcvKERNEL_COMMAND
            ));
    }

    /* Success. */
    gcmkFOOTER_ARG("Command->index=%d", Command->index);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

static gceSTATUS
_IncrementCommitAtom(
    IN gckCOMMAND Command,
    IN int Increment
    )
{
    gceSTATUS status;
    gckHARDWARE hardware;
    s32 atomValue;
    int powerAcquired = gcvFALSE;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Extract the gckHARDWARE and gckEVENT objects. */
    hardware = Command->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    /* Grab the power mutex. */
    gcmkONERROR(gckOS_AcquireMutex(
        Command->os, hardware->powerMutex, gcvINFINITE
        ));
    powerAcquired = gcvTRUE;

    /* Increment the commit atom. */
    if (Increment)
    {
        gcmkONERROR(gckOS_AtomIncrement(
            Command->os, Command->atomCommit, &atomValue
            ));
    }
    else
    {
        gcmkONERROR(gckOS_AtomDecrement(
            Command->os, Command->atomCommit, &atomValue
            ));
    }

    /* Release the power mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(
        Command->os, hardware->powerMutex
        ));
    powerAcquired = gcvFALSE;

    /* Success. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    if (powerAcquired)
    {
        /* Release the power mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(
            Command->os, hardware->powerMutex
            ));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if gcdSECURE_USER
static gceSTATUS
_ProcessHints(
    IN gckCOMMAND Command,
    IN u32 ProcessID,
    IN gcoCMDBUF CommandBuffer
    )
{
    gceSTATUS status = gcvSTATUS_OK;
    gckKERNEL kernel;
    gcskSECURE_CACHE_PTR cache;
    u8 *commandBufferLogical;
    u8 *hintedData;
    u32 *hintArray;
    unsigned int i, hintCount;

    gcmkHEADER_ARG(
        "Command=0x%08X ProcessID=%d CommandBuffer=0x%08X",
        Command, ProcessID, CommandBuffer
        );

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Reset state array pointer. */
    hintArray = NULL;

    /* Get the kernel object. */
    kernel = Command->kernel;

    /* Get the cache form the database. */
    gcmkONERROR(gckKERNEL_GetProcessDBCache(kernel, ProcessID, &cache));

    /* Determine the start of the command buffer. */
    commandBufferLogical
        = (u8 *) CommandBuffer->logical
        +                CommandBuffer->startOffset;

    /* Determine the number of records in the state array. */
    hintCount = CommandBuffer->hintArrayTail - CommandBuffer->hintArray;

    /* Get access to the state array. */
    if (NO_USER_DIRECT_ACCESS_FROM_KERNEL)
    {
        unsigned int copySize;

        if (Command->hintArrayAllocated &&
            (Command->hintArraySize < CommandBuffer->hintArraySize))
        {
            gcmkONERROR(gcmkOS_SAFE_FREE(Command->os, Command->hintArray));
            Command->hintArraySize = gcvFALSE;
        }

        if (!Command->hintArrayAllocated)
        {
            void *pointer = NULL;

            gcmkONERROR(gckOS_Allocate(
                Command->os,
                CommandBuffer->hintArraySize,
                &pointer
                ));

            Command->hintArray          = pointer;
            Command->hintArrayAllocated = gcvTRUE;
            Command->hintArraySize      = CommandBuffer->hintArraySize;
        }

        hintArray = Command->hintArray;
        copySize   = hintCount * sizeof(u32);

	if (copy_from_user(hintArray, CommandBuffer->hintArray, copySize) != 0)
        {
            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
        }
    }
    else
    {
        void *pointer = NULL;

        gcmkONERROR(gckOS_MapUserPointer(
            Command->os,
            CommandBuffer->hintArray,
            CommandBuffer->hintArraySize,
            &pointer
            ));

        hintArray = pointer;
    }

    /* Scan through the buffer. */
    for (i = 0; i < hintCount; i += 1)
    {
        /* Determine the location of the hinted data. */
        hintedData = commandBufferLogical + hintArray[i];

        /* Map handle into physical address. */
        gcmkONERROR(gckKERNEL_MapLogicalToPhysical(
            kernel, cache, (void *) hintedData
            ));
    }

OnError:
    /* Get access to the state array. */
    if (!NO_USER_DIRECT_ACCESS_FROM_KERNEL && (hintArray != NULL))
    {
        gcmkVERIFY_OK(gckOS_UnmapUserPointer(
            Command->os,
            CommandBuffer->hintArray,
            CommandBuffer->hintArraySize,
            hintArray
            ));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif

static gceSTATUS
_FlushMMU(
    IN gckCOMMAND Command
    )
{
    gceSTATUS status;
    u32 oldValue;
    gckHARDWARE hardware = Command->kernel->hardware;

    gcmkONERROR(gckOS_AtomicExchange(Command->os,
                                     hardware->pageTableDirty,
                                     0,
                                     &oldValue));

    if (oldValue)
    {
        /* Page Table is upated, flush mmu before commit. */
        gcmkONERROR(gckHARDWARE_FlushMMU(hardware));
    }

    return gcvSTATUS_OK;
OnError:
    return status;
}

/******************************************************************************\
****************************** gckCOMMAND API Code ******************************
\******************************************************************************/

/* Size of kernel GPU command queue */
#define COMMAND_QUEUE_SIZE (PAGE_SIZE)

/*******************************************************************************
**
**  gckCOMMAND_Construct
**
**  Construct a new gckCOMMAND object.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**  OUTPUT:
**
**      gckCOMMAND * Command
**          Pointer to a variable that will hold the pointer to the gckCOMMAND
**          object.
*/
gceSTATUS
gckCOMMAND_Construct(
    IN gckKERNEL Kernel,
    OUT gckCOMMAND * Command
    )
{
    gckOS os;
    gckCOMMAND command = NULL;
    gceSTATUS status;
    int i;
    void *pointer = NULL;

    gcmkHEADER_ARG("Kernel=0x%x", Kernel);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Command != NULL);

    /* Extract the gckOS object. */
    os = Kernel->os;

    /* Allocate the gckCOMMAND structure. */
    gcmkONERROR(gckOS_Allocate(os, sizeof(struct _gckCOMMAND), &pointer));
    command = pointer;

    /* Reset the entire object. */
    gcmkONERROR(gckOS_ZeroMemory(command, sizeof(struct _gckCOMMAND)));

    /* Initialize the gckCOMMAND object.*/
    command->object.type    = gcvOBJ_COMMAND;
    command->kernel         = Kernel;
    command->os             = os;

    /* Get the command buffer requirements. */
    gcmkONERROR(gckHARDWARE_QueryCommandBuffer(
        Kernel->hardware,
        &command->alignment,
        &command->reservedHead,
        &command->reservedTail
        ));

    /* Create the command queue mutex. */
    gcmkONERROR(gckOS_CreateMutex(os, &command->mutexQueue));

    /* Create the context switching mutex. */
    gcmkONERROR(gckOS_CreateMutex(os, &command->mutexContext));

    /* Create the power management semaphore. */
    gcmkONERROR(gckOS_CreateSemaphore(os, &command->powerSemaphore));

    /* Create the commit atom. */
    gcmkONERROR(gckOS_AtomConstruct(os, &command->atomCommit));

    command->kernelProcessID = task_tgid_vnr(current);

    /* Set hardware to pipe 0. */
    command->pipeSelect = gcvPIPE_INVALID;

    /* Pre-allocate the command queues. */
    for (i = 0; i < gcdCOMMAND_QUEUES; ++i)
    {
	size_t bytes = COMMAND_QUEUE_SIZE;
        gcmkONERROR(gckOS_AllocateNonPagedMemory(
            os,
            gcvFALSE,
            &bytes,
            &command->queues[i].physical,
            &command->queues[i].logical
            ));

        gcmkONERROR(gckOS_CreateSignal(
            os, gcvFALSE, &command->queues[i].signal
            ));

        gcmkONERROR(gckOS_Signal(
            os, command->queues[i].signal, gcvTRUE
            ));
    }

    /* No command queue in use yet. */
    command->index    = -1;
    command->logical  = NULL;
    command->newQueue = gcvFALSE;

    /* Command is not yet running. */
    command->running = gcvFALSE;

    /* Command queue is idle. */
    command->idle = gcvTRUE;

    /* Commit stamp is zero. */
    command->commitStamp = 0;

    /* END event signal not created. */
    command->endEventSignal = NULL;

    /* Return pointer to the gckCOMMAND object. */
    *Command = command;

    /* Success. */
    gcmkFOOTER_ARG("*Command=0x%x", *Command);
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (command != NULL)
    {
        if (command->atomCommit != NULL)
        {
            gcmkVERIFY_OK(gckOS_AtomDestroy(os, command->atomCommit));
        }

        if (command->powerSemaphore != NULL)
        {
            gcmkVERIFY_OK(gckOS_DestroySemaphore(os, command->powerSemaphore));
        }

        if (command->mutexContext != NULL)
        {
            gcmkVERIFY_OK(gckOS_DeleteMutex(os, command->mutexContext));
        }

        if (command->mutexQueue != NULL)
        {
            gcmkVERIFY_OK(gckOS_DeleteMutex(os, command->mutexQueue));
        }

        for (i = 0; i < gcdCOMMAND_QUEUES; ++i)
        {
            if (command->queues[i].signal != NULL)
            {
                gcmkVERIFY_OK(gckOS_DestroySignal(
                    os, command->queues[i].signal
                    ));
            }

            if (command->queues[i].logical != NULL)
            {
                gcmkVERIFY_OK(gckOS_FreeNonPagedMemory(
                    os,
                    COMMAND_QUEUE_SIZE,
                    command->queues[i].physical,
                    command->queues[i].logical
                    ));
            }
        }

        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(os, command));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_Destroy
**
**  Destroy an gckCOMMAND object.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object to destroy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Destroy(
    IN gckCOMMAND Command
    )
{
    int i;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Stop the command queue. */
    gcmkVERIFY_OK(gckCOMMAND_Stop(Command, gcvFALSE));

    for (i = 0; i < gcdCOMMAND_QUEUES; ++i)
    {
        gcmkASSERT(Command->queues[i].signal != NULL);
        gcmkVERIFY_OK(gckOS_DestroySignal(
            Command->os, Command->queues[i].signal
            ));

        gcmkASSERT(Command->queues[i].logical != NULL);
        gcmkVERIFY_OK(gckOS_FreeNonPagedMemory(
            Command->os,
            COMMAND_QUEUE_SIZE,
            Command->queues[i].physical,
            Command->queues[i].logical
            ));
    }

    /* END event signal. */
    if (Command->endEventSignal != NULL)
    {
        gcmkVERIFY_OK(gckOS_DestroySignal(
            Command->os, Command->endEventSignal
            ));
    }

    /* Delete the context switching mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Command->os, Command->mutexContext));

    /* Delete the command queue mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Command->os, Command->mutexQueue));

    /* Destroy the power management semaphore. */
    gcmkVERIFY_OK(gckOS_DestroySemaphore(Command->os, Command->powerSemaphore));

    /* Destroy the commit atom. */
    gcmkVERIFY_OK(gckOS_AtomDestroy(Command->os, Command->atomCommit));

#if gcdSECURE_USER
    /* Free state array. */
    if (Command->hintArrayAllocated)
    {
        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Command->os, Command->hintArray));
        Command->hintArrayAllocated = gcvFALSE;
    }
#endif

    /* Mark object as unknown. */
    Command->object.type = gcvOBJ_UNKNOWN;

    /* Free the gckCOMMAND object. */
    gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Command->os, Command));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckCOMMAND_EnterCommit
**
**  Acquire command queue synchronization objects.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object to destroy.
**
**      int FromPower
**          Determines whether the call originates from inside the power
**          management or not.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_EnterCommit(
    IN gckCOMMAND Command,
    IN int FromPower
    )
{
    gceSTATUS status;
    gckHARDWARE hardware;
    int atomIncremented = gcvFALSE;
    int semaAcquired = gcvFALSE;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Extract the gckHARDWARE and gckEVENT objects. */
    hardware = Command->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    if (!FromPower)
    {
        /* Increment COMMIT atom to let power management know that a commit is
        ** in progress. */
        gcmkONERROR(_IncrementCommitAtom(Command, gcvTRUE));
        atomIncremented = gcvTRUE;

        /* Notify the system the GPU has a commit. */
        gcmkONERROR(gckOS_Broadcast(Command->os,
                                    hardware,
                                    gcvBROADCAST_GPU_COMMIT));

        /* Acquire the power management semaphore. */
        gcmkONERROR(gckOS_AcquireSemaphore(Command->os,
                                           Command->powerSemaphore));
        semaAcquired = gcvTRUE;
    }

    /* Grab the conmmand queue mutex. */
    gcmkONERROR(gckOS_AcquireMutex(Command->os,
                                   Command->mutexQueue,
                                   gcvINFINITE));

    /* Success. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    if (semaAcquired)
    {
        /* Release the power management semaphore. */
        gcmkVERIFY_OK(gckOS_ReleaseSemaphore(
            Command->os, Command->powerSemaphore
            ));
    }

    if (atomIncremented)
    {
        /* Decrement the commit atom. */
        gcmkVERIFY_OK(_IncrementCommitAtom(
            Command, gcvFALSE
            ));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_ExitCommit
**
**  Release command queue synchronization objects.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object to destroy.
**
**      int FromPower
**          Determines whether the call originates from inside the power
**          management or not.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_ExitCommit(
    IN gckCOMMAND Command,
    IN int FromPower
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Release the power mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Command->os, Command->mutexQueue));

    if (!FromPower)
    {
        /* Release the power management semaphore. */
        gcmkONERROR(gckOS_ReleaseSemaphore(Command->os,
                                           Command->powerSemaphore));

        /* Decrement the commit atom. */
        gcmkONERROR(_IncrementCommitAtom(Command, gcvFALSE));
    }

    /* Success. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_Start
**
**  Start up the command queue.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object to start.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Start(
    IN gckCOMMAND Command
    )
{
    gceSTATUS status;
    gckHARDWARE hardware;
    u32 waitOffset;
    size_t waitLinkBytes;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    if (Command->running)
    {
        /* Command queue already running. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    /* Extract the gckHARDWARE object. */
    hardware = Command->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    if (Command->logical == NULL)
    {
        /* Start at beginning of a new queue. */
        gcmkONERROR(_NewQueue(Command));
    }

    /* Start at beginning of page. */
    Command->offset = 0;

    /* Set abvailable number of bytes for WAIT/LINK command sequence. */
    waitLinkBytes = COMMAND_QUEUE_SIZE;

    /* Append WAIT/LINK. */
    gcmkONERROR(gckHARDWARE_WaitLink(
        hardware,
        Command->logical,
        0,
        &waitLinkBytes,
        &waitOffset,
        &Command->waitSize
        ));

    Command->waitLogical  = (u8 *) Command->logical  + waitOffset;
    Command->waitPhysical = (u8 *) Command->physical + waitOffset;

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the cache for the wait/link. */
    gcmkONERROR(gckOS_CacheClean(
        Command->os,
        Command->kernelProcessID,
        NULL,
        Command->physical,
        Command->logical,
        waitLinkBytes
        ));
#endif

    /* Adjust offset. */
    Command->offset   = waitLinkBytes;
    Command->newQueue = gcvFALSE;

    /* Enable command processor. */
    gcmkONERROR(gckHARDWARE_Execute(
        hardware,
        Command->logical,
        waitLinkBytes
        ));

    /* Command queue is running. */
    Command->running = gcvTRUE;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_Stop
**
**  Stop the command queue.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object to stop.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Stop(
    IN gckCOMMAND Command,
    IN int FromRecovery
    )
{
    gckHARDWARE hardware;
    gceSTATUS status;
    u32 idle;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    if (!Command->running)
    {
        /* Command queue is not running. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    /* Extract the gckHARDWARE object. */
    hardware = Command->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    if (gckHARDWARE_IsFeatureAvailable(hardware,
                                       gcvFEATURE_END_EVENT) == gcvSTATUS_TRUE)
    {
        /* Allocate the signal. */
        if (Command->endEventSignal == NULL)
        {
            gcmkONERROR(gckOS_CreateSignal(Command->os,
                                           gcvTRUE,
                                           &Command->endEventSignal));
        }

        /* Append the END EVENT command to trigger the signal. */
        gcmkONERROR(gckEVENT_Stop(Command->kernel->eventObj,
                                  Command->kernelProcessID,
                                  Command->waitPhysical,
                                  Command->waitLogical,
                                  Command->endEventSignal,
								  &Command->waitSize));
    }
    else
    {
        /* Replace last WAIT with END. */
        gcmkONERROR(gckHARDWARE_End(
            hardware, Command->waitLogical, &Command->waitSize
            ));

        /* Update queue tail pointer. */
        gcmkONERROR(gckHARDWARE_UpdateQueueTail(Command->kernel->hardware,
                                                Command->logical,
                                                Command->offset));

#if gcdNONPAGED_MEMORY_CACHEABLE
        /* Flush the cache for the END. */
        gcmkONERROR(gckOS_CacheClean(
            Command->os,
            Command->kernelProcessID,
            NULL,
            Command->waitPhysical,
            Command->waitLogical,
            Command->waitSize
            ));
#endif

        /* Wait for idle. */
        gcmkONERROR(gckHARDWARE_GetIdle(hardware, !FromRecovery, &idle));
    }

    /* Command queue is no longer running. */
    Command->running = gcvFALSE;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_Commit
**
**  Commit a command buffer to the command queue.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to a gckCOMMAND object.
**
**      gckCONTEXT Context
**          Pointer to a gckCONTEXT object.
**
**      gcoCMDBUF CommandBuffer
**          Pointer to a gcoCMDBUF object.
**
**      struct _gcsSTATE_DELTA *StateDelta
**          Pointer to the state delta.
**
**      u32 ProcessID
**          Current process ID.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Commit(
    IN gckCOMMAND Command,
    IN gckCONTEXT Context,
    IN gcoCMDBUF CommandBuffer,
    IN struct _gcsSTATE_DELTA *StateDelta,
    IN struct _gcsQUEUE *EventQueue,
    IN u32 ProcessID
    )
{
    gceSTATUS status;
    int commitEntered = gcvFALSE;
    int contextAcquired = gcvFALSE;
    gckHARDWARE hardware;
    struct _gcsQUEUE *eventRecord = NULL;
    gcsQUEUE _eventRecord;
    struct _gcsQUEUE *nextEventRecord;
    int commandBufferMapped = gcvFALSE;
    gcoCMDBUF commandBufferObject = NULL;

#if !gcdNULL_DRIVER
    gcsCONTEXT_PTR contextBuffer;
    struct _gcoCMDBUF _commandBufferObject;
    gctPHYS_ADDR commandBufferPhysical;
    u8 *commandBufferLogical;
    u8 *commandBufferLink;
    unsigned int commandBufferSize;
    size_t nopBytes;
    size_t pipeBytes;
    size_t linkBytes;
    size_t bytes;
    u32 offset;
#if gcdNONPAGED_MEMORY_CACHEABLE
    gctPHYS_ADDR entryPhysical;
#endif
    void *entryLogical;
    size_t entryBytes;
#if gcdNONPAGED_MEMORY_CACHEABLE
    gctPHYS_ADDR exitPhysical;
#endif
    void *exitLogical;
    size_t exitBytes;
    gctPHYS_ADDR waitLinkPhysical;
    void *waitLinkLogical;
    size_t waitLinkBytes;
    gctPHYS_ADDR waitPhysical;
    void *waitLogical;
    u32 waitOffset;
    size_t waitSize;

#if gcdDUMP_COMMAND
    void *contextDumpLogical = NULL;
    size_t contextDumpBytes = 0;
    void *bufferDumpLogical = NULL;
    size_t bufferDumpBytes = 0;
# endif
#endif

    void *pointer = NULL;

    gcmkHEADER_ARG(
        "Command=0x%x CommandBuffer=0x%x ProcessID=%d",
        Command, CommandBuffer, ProcessID
        );

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    if (Command->kernel->core == gcvCORE_2D)
    {
        /* There is no context for 2D. */
        Context = NULL;
    }

    gcmkONERROR(_FlushMMU(Command));

    /* Acquire the command queue. */
    gcmkONERROR(gckCOMMAND_EnterCommit(Command, gcvFALSE));
    commitEntered = gcvTRUE;

    /* Acquire the context switching mutex. */
    gcmkONERROR(gckOS_AcquireMutex(
        Command->os, Command->mutexContext, gcvINFINITE
        ));
    contextAcquired = gcvTRUE;

    /* Extract the gckHARDWARE and gckEVENT objects. */
    hardware = Command->kernel->hardware;

#if gcdNULL_DRIVER
    /* Context switch required? */
    if ((Context != NULL) && (Command->currContext != Context))
    {
        /* Yes, merge in the deltas. */
        gckCONTEXT_Update(Context, ProcessID, StateDelta);

		/* Update the current context. */
		Command->currContext = Context;
	}
#else
    if (NO_USER_DIRECT_ACCESS_FROM_KERNEL)
    {
        commandBufferObject = &_commandBufferObject;

	if (copy_from_user(commandBufferObject, CommandBuffer, sizeof(struct _gcoCMDBUF)) != 0)
        {
            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
        }

        gcmkVERIFY_OBJECT(commandBufferObject, gcvOBJ_COMMANDBUFFER);
    }
    else
    {
        gcmkONERROR(gckOS_MapUserPointer(
            Command->os,
            CommandBuffer,
            sizeof(struct _gcoCMDBUF),
            &pointer
            ));

        commandBufferObject = pointer;

        gcmkVERIFY_OBJECT(commandBufferObject, gcvOBJ_COMMANDBUFFER);
        commandBufferMapped = gcvTRUE;
    }

    /* Query the size of NOP command. */
    gcmkONERROR(gckHARDWARE_Nop(
        hardware, NULL, &nopBytes
        ));

    /* Query the size of pipe select command sequence. */
    gcmkONERROR(gckHARDWARE_PipeSelect(
        hardware, NULL, gcvPIPE_3D, &pipeBytes
        ));

    /* Query the size of LINK command. */
    gcmkONERROR(gckHARDWARE_Link(
        hardware, NULL, NULL, 0, &linkBytes
        ));

    /* Compute the command buffer entry and the size. */
    commandBufferLogical
        = (u8 *) commandBufferObject->logical
        +                commandBufferObject->startOffset;

    gcmkONERROR(gckOS_GetPhysicalAddress(
        Command->os,
        commandBufferLogical,
        (u32 *)&commandBufferPhysical
        ));

    commandBufferSize
        = commandBufferObject->offset
        + Command->reservedTail
        - commandBufferObject->startOffset;

    /* Context switch required? */
    if (Context == NULL)
    {
        /* See if we have to switch pipes for the command buffer. */
        if (commandBufferObject->entryPipe == Command->pipeSelect)
        {
            /* Skip pipe switching sequence. */
            offset = pipeBytes;
        }
        else
        {
            /* The current hardware and the entry command buffer pipes
            ** are different, switch to the correct pipe. */
            gcmkONERROR(gckHARDWARE_PipeSelect(
                Command->kernel->hardware,
                commandBufferLogical,
                commandBufferObject->entryPipe,
                &pipeBytes
                ));

            /* Do not skip pipe switching sequence. */
            offset = 0;
        }

        /* Compute the entry. */
#if gcdNONPAGED_MEMORY_CACHEABLE
        entryPhysical = (u8 *) commandBufferPhysical + offset;
#endif
        entryLogical  =                commandBufferLogical  + offset;
        entryBytes    =                commandBufferSize     - offset;
    }
    else if (Command->currContext != Context)
    {
        /* Temporary disable context length oprimization. */
        Context->dirty = gcvTRUE;

        /* Get the current context buffer. */
        contextBuffer = Context->buffer;

        /* Yes, merge in the deltas. */
        gcmkONERROR(gckCONTEXT_Update(Context, ProcessID, StateDelta));

        /* Determine context entry and exit points. */
        if (0)
        {
            /* Reset 2D dirty flag. */
            Context->dirty2D = gcvFALSE;

            if (Context->dirty || commandBufferObject->using3D)
            {
                /***************************************************************
                ** SWITCHING CONTEXT: 2D and 3D are used.
                */

                /* Reset 3D dirty flag. */
                Context->dirty3D = gcvFALSE;

                /* Compute the entry. */
                if (Command->pipeSelect == gcvPIPE_2D)
                {
#if gcdNONPAGED_MEMORY_CACHEABLE
                    entryPhysical = (u8 *) contextBuffer->physical + pipeBytes;
#endif
                    entryLogical  = (u8 *) contextBuffer->logical  + pipeBytes;
                    entryBytes    =                Context->bufferSize     - pipeBytes;
                }
                else
                {
#if gcdNONPAGED_MEMORY_CACHEABLE
                    entryPhysical = (u8 *) contextBuffer->physical;
#endif
                    entryLogical  = (u8 *) contextBuffer->logical;
                    entryBytes    =                Context->bufferSize;
                }

                /* See if we have to switch pipes between the context
                   and command buffers. */
                if (commandBufferObject->entryPipe == gcvPIPE_3D)
                {
                    /* Skip pipe switching sequence. */
                    offset = pipeBytes;
                }
                else
                {
                    /* The current hardware and the initial context pipes are
                       different, switch to the correct pipe. */
                    gcmkONERROR(gckHARDWARE_PipeSelect(
                        Command->kernel->hardware,
                        commandBufferLogical,
                        commandBufferObject->entryPipe,
                        &pipeBytes
                        ));

                    /* Do not skip pipe switching sequence. */
                    offset = 0;
                }

                /* Ensure the NOP between 2D and 3D is in place so that the
                   execution falls through from 2D to 3D. */
                gcmkONERROR(gckHARDWARE_Nop(
                    hardware,
                    contextBuffer->link2D,
                    &nopBytes
                    ));

                /* Generate a LINK from the context buffer to
                   the command buffer. */
                gcmkONERROR(gckHARDWARE_Link(
                    hardware,
                    contextBuffer->link3D,
                    commandBufferLogical + offset,
                    commandBufferSize    - offset,
                    &linkBytes
                    ));

                /* Mark context as not dirty. */
                Context->dirty = gcvFALSE;
            }
            else
            {
                /***************************************************************
                ** SWITCHING CONTEXT: 2D only command buffer.
                */

                /* Mark 3D as dirty. */
                Context->dirty3D = gcvTRUE;

                /* Compute the entry. */
                if (Command->pipeSelect == gcvPIPE_2D)
                {
#if gcdNONPAGED_MEMORY_CACHEABLE
                    entryPhysical = (u8 *) contextBuffer->physical + pipeBytes;
#endif
                    entryLogical  = (u8 *) contextBuffer->logical  + pipeBytes;
                    entryBytes    =                Context->entryOffset3D  - pipeBytes;
                }
                else
                {
#if gcdNONPAGED_MEMORY_CACHEABLE
                    entryPhysical = (u8 *) contextBuffer->physical;
#endif
                    entryLogical  = (u8 *) contextBuffer->logical;
                    entryBytes    =                Context->entryOffset3D;
                }

                /* Store the current context buffer. */
                Context->dirtyBuffer = contextBuffer;

                /* See if we have to switch pipes between the context
                   and command buffers. */
                if (commandBufferObject->entryPipe == gcvPIPE_2D)
                {
                    /* Skip pipe switching sequence. */
                    offset = pipeBytes;
                }
                else
                {
                    /* The current hardware and the initial context pipes are
                       different, switch to the correct pipe. */
                    gcmkONERROR(gckHARDWARE_PipeSelect(
                        Command->kernel->hardware,
                        commandBufferLogical,
                        commandBufferObject->entryPipe,
                        &pipeBytes
                        ));

                    /* Do not skip pipe switching sequence. */
                    offset = 0;
                }

                /* 3D is not used, generate a LINK from the end of 2D part of
                   the context buffer to the command buffer. */
                gcmkONERROR(gckHARDWARE_Link(
                    hardware,
                    contextBuffer->link2D,
                    commandBufferLogical + offset,
                    commandBufferSize    - offset,
                    &linkBytes
                    ));
            }
        }

        /* Not using 2D. */
        else
        {
            /* Mark 2D as dirty. */
            Context->dirty2D = gcvTRUE;

            /* Store the current context buffer. */
            Context->dirtyBuffer = contextBuffer;

            if (Context->dirty || commandBufferObject->using3D)
            {
                /***************************************************************
                ** SWITCHING CONTEXT: 3D only command buffer.
                */

                /* Reset 3D dirty flag. */
                Context->dirty3D = gcvFALSE;

                /* Determine context buffer entry offset. */
                offset = (Command->pipeSelect == gcvPIPE_3D)

                    /* Skip pipe switching sequence. */
                    ? Context->entryOffset3D + pipeBytes

                    /* Do not skip pipe switching sequence. */
                    : Context->entryOffset3D;

                /* Compute the entry. */
#if gcdNONPAGED_MEMORY_CACHEABLE
                entryPhysical = (u8 *) contextBuffer->physical + offset;
#endif
                entryLogical  = (u8 *) contextBuffer->logical  + offset;
                entryBytes    =                Context->bufferSize     - offset;

                /* See if we have to switch pipes between the context
                   and command buffers. */
                if (commandBufferObject->entryPipe == gcvPIPE_3D)
                {
                    /* Skip pipe switching sequence. */
                    offset = pipeBytes;
                }
                else
                {
                    /* The current hardware and the initial context pipes are
                       different, switch to the correct pipe. */
                    gcmkONERROR(gckHARDWARE_PipeSelect(
                        Command->kernel->hardware,
                        commandBufferLogical,
                        commandBufferObject->entryPipe,
                        &pipeBytes
                        ));

                    /* Do not skip pipe switching sequence. */
                    offset = 0;
                }

                /* Generate a LINK from the context buffer to
                   the command buffer. */
                gcmkONERROR(gckHARDWARE_Link(
                    hardware,
                    contextBuffer->link3D,
                    commandBufferLogical + offset,
                    commandBufferSize    - offset,
                    &linkBytes
                    ));
            }
            else
            {
                /***************************************************************
                ** SWITCHING CONTEXT: "XD" command buffer - neither 2D nor 3D.
                */

                /* Mark 3D as dirty. */
                Context->dirty3D = gcvTRUE;

                /* Compute the entry. */
                if (Command->pipeSelect == gcvPIPE_3D)
                {
#if gcdNONPAGED_MEMORY_CACHEABLE
                    entryPhysical
                        = (u8 *) contextBuffer->physical
                        + Context->entryOffsetXDFrom3D;
#endif
                    entryLogical
                        = (u8 *) contextBuffer->logical
                        + Context->entryOffsetXDFrom3D;

                    entryBytes
                        = Context->bufferSize
                        - Context->entryOffsetXDFrom3D;
                }
                else
                {
#if gcdNONPAGED_MEMORY_CACHEABLE
                    entryPhysical
                        = (u8 *) contextBuffer->physical
                        + Context->entryOffsetXDFrom2D;
#endif
                    entryLogical
                        = (u8 *) contextBuffer->logical
                        + Context->entryOffsetXDFrom2D;

                    entryBytes
                        = Context->totalSize
                        - Context->entryOffsetXDFrom2D;
                }

                /* See if we have to switch pipes between the context
                   and command buffers. */
                if (commandBufferObject->entryPipe == gcvPIPE_3D)
                {
                    /* Skip pipe switching sequence. */
                    offset = pipeBytes;
                }
                else
                {
                    /* The current hardware and the initial context pipes are
                       different, switch to the correct pipe. */
                    gcmkONERROR(gckHARDWARE_PipeSelect(
                        Command->kernel->hardware,
                        commandBufferLogical,
                        commandBufferObject->entryPipe,
                        &pipeBytes
                        ));

                    /* Do not skip pipe switching sequence. */
                    offset = 0;
                }

                /* Generate a LINK from the context buffer to
                   the command buffer. */
                gcmkONERROR(gckHARDWARE_Link(
                    hardware,
                    contextBuffer->link3D,
                    commandBufferLogical + offset,
                    commandBufferSize    - offset,
                    &linkBytes
                    ));
            }
        }

#if gcdNONPAGED_MEMORY_CACHEABLE
        /* Flush the context buffer cache. */
        gcmkONERROR(gckOS_CacheClean(
            Command->os,
            Command->kernelProcessID,
            NULL,
            entryPhysical,
            entryLogical,
            entryBytes
            ));
#endif

        /* Update the current context. */
        Command->currContext = Context;

#if gcdDUMP_COMMAND
        contextDumpLogical = entryLogical;
        contextDumpBytes   = entryBytes;
#endif
    }

    /* Same context. */
    else
    {
        /* Determine context entry and exit points. */
        if (commandBufferObject->using2D && Context->dirty2D)
        {
            /* Reset 2D dirty flag. */
            Context->dirty2D = gcvFALSE;

            /* Get the "dirty" context buffer. */
            contextBuffer = Context->dirtyBuffer;

            if (commandBufferObject->using3D && Context->dirty3D)
            {
                /* Reset 3D dirty flag. */
                Context->dirty3D = gcvFALSE;

                /* Compute the entry. */
                if (Command->pipeSelect == gcvPIPE_2D)
                {
#if gcdNONPAGED_MEMORY_CACHEABLE
                    entryPhysical = (u8 *) contextBuffer->physical + pipeBytes;
#endif
                    entryLogical  = (u8 *) contextBuffer->logical  + pipeBytes;
                    entryBytes    =                Context->bufferSize     - pipeBytes;
                }
                else
                {
#if gcdNONPAGED_MEMORY_CACHEABLE
                    entryPhysical = (u8 *) contextBuffer->physical;
#endif
                    entryLogical  = (u8 *) contextBuffer->logical;
                    entryBytes    =                Context->bufferSize;
                }

                /* See if we have to switch pipes between the context
                   and command buffers. */
                if (commandBufferObject->entryPipe == gcvPIPE_3D)
                {
                    /* Skip pipe switching sequence. */
                    offset = pipeBytes;
                }
                else
                {
                    /* The current hardware and the initial context pipes are
                       different, switch to the correct pipe. */
                    gcmkONERROR(gckHARDWARE_PipeSelect(
                        Command->kernel->hardware,
                        commandBufferLogical,
                        commandBufferObject->entryPipe,
                        &pipeBytes
                        ));

                    /* Do not skip pipe switching sequence. */
                    offset = 0;
                }

                /* Ensure the NOP between 2D and 3D is in place so that the
                   execution falls through from 2D to 3D. */
                gcmkONERROR(gckHARDWARE_Nop(
                    hardware,
                    contextBuffer->link2D,
                    &nopBytes
                    ));

                /* Generate a LINK from the context buffer to
                   the command buffer. */
                gcmkONERROR(gckHARDWARE_Link(
                    hardware,
                    contextBuffer->link3D,
                    commandBufferLogical + offset,
                    commandBufferSize    - offset,
                    &linkBytes
                    ));
            }
            else
            {
                /* Compute the entry. */
                if (Command->pipeSelect == gcvPIPE_2D)
                {
#if gcdNONPAGED_MEMORY_CACHEABLE
                    entryPhysical = (u8 *) contextBuffer->physical + pipeBytes;
#endif
                    entryLogical  = (u8 *) contextBuffer->logical  + pipeBytes;
                    entryBytes    =                Context->entryOffset3D  - pipeBytes;
                }
                else
                {
#if gcdNONPAGED_MEMORY_CACHEABLE
                    entryPhysical = (u8 *) contextBuffer->physical;
#endif
                    entryLogical  = (u8 *) contextBuffer->logical;
                    entryBytes    =                Context->entryOffset3D;
                }

                /* See if we have to switch pipes between the context
                   and command buffers. */
                if (commandBufferObject->entryPipe == gcvPIPE_2D)
                {
                    /* Skip pipe switching sequence. */
                    offset = pipeBytes;
                }
                else
                {
                    /* The current hardware and the initial context pipes are
                       different, switch to the correct pipe. */
                    gcmkONERROR(gckHARDWARE_PipeSelect(
                        Command->kernel->hardware,
                        commandBufferLogical,
                        commandBufferObject->entryPipe,
                        &pipeBytes
                        ));

                    /* Do not skip pipe switching sequence. */
                    offset = 0;
                }

                /* 3D is not used, generate a LINK from the end of 2D part of
                   the context buffer to the command buffer. */
                gcmkONERROR(gckHARDWARE_Link(
                    hardware,
                    contextBuffer->link2D,
                    commandBufferLogical + offset,
                    commandBufferSize    - offset,
                    &linkBytes
                    ));
            }
        }
        else
        {
            if (commandBufferObject->using3D && Context->dirty3D)
            {
                /* Reset 3D dirty flag. */
                Context->dirty3D = gcvFALSE;

                /* Get the "dirty" context buffer. */
                contextBuffer = Context->dirtyBuffer;

                /* Determine context buffer entry offset. */
                offset = (Command->pipeSelect == gcvPIPE_3D)

                    /* Skip pipe switching sequence. */
                    ? Context->entryOffset3D + pipeBytes

                    /* Do not skip pipe switching sequence. */
                    : Context->entryOffset3D;

                /* Compute the entry. */
#if gcdNONPAGED_MEMORY_CACHEABLE
                entryPhysical = (u8 *) contextBuffer->physical + offset;
#endif
                entryLogical  = (u8 *) contextBuffer->logical  + offset;
                entryBytes    =                Context->bufferSize     - offset;

                /* See if we have to switch pipes between the context
                   and command buffers. */
                if (commandBufferObject->entryPipe == gcvPIPE_3D)
                {
                    /* Skip pipe switching sequence. */
                    offset = pipeBytes;
                }
                else
                {
                    /* The current hardware and the initial context pipes are
                       different, switch to the correct pipe. */
                    gcmkONERROR(gckHARDWARE_PipeSelect(
                        Command->kernel->hardware,
                        commandBufferLogical,
                        commandBufferObject->entryPipe,
                        &pipeBytes
                        ));

                    /* Do not skip pipe switching sequence. */
                    offset = 0;
                }

                /* Generate a LINK from the context buffer to
                   the command buffer. */
                gcmkONERROR(gckHARDWARE_Link(
                    hardware,
                    contextBuffer->link3D,
                    commandBufferLogical + offset,
                    commandBufferSize    - offset,
                    &linkBytes
                    ));
            }
            else
            {
                /* See if we have to switch pipes for the command buffer. */
                if (commandBufferObject->entryPipe == Command->pipeSelect)
                {
                    /* Skip pipe switching sequence. */
                    offset = pipeBytes;
                }
                else
                {
                    /* The current hardware and the entry command buffer pipes
                    ** are different, switch to the correct pipe. */
                    gcmkONERROR(gckHARDWARE_PipeSelect(
                        Command->kernel->hardware,
                        commandBufferLogical,
                        commandBufferObject->entryPipe,
                        &pipeBytes
                        ));

                    /* Do not skip pipe switching sequence. */
                    offset = 0;
                }

                /* Compute the entry. */
#if gcdNONPAGED_MEMORY_CACHEABLE
                entryPhysical = (u8 *) commandBufferPhysical + offset;
#endif
                entryLogical  =                commandBufferLogical  + offset;
                entryBytes    =                commandBufferSize     - offset;
            }
        }
    }

#if gcdDUMP_COMMAND
    bufferDumpLogical = commandBufferLogical + offset;
    bufferDumpBytes   = commandBufferSize    - offset;
#endif

#if gcdSECURE_USER
    /* Process user hints. */
    gcmkONERROR(_ProcessHints(Command, ProcessID, commandBufferObject));
#endif

    /* Get the current offset. */
    offset = Command->offset;

    /* Compute number of bytes left in current kernel command queue. */
    bytes = COMMAND_QUEUE_SIZE - offset;

    /* Query the size of WAIT/LINK command sequence. */
    gcmkONERROR(gckHARDWARE_WaitLink(
        hardware,
        NULL,
        offset,
        &waitLinkBytes,
        NULL,
        NULL
        ));

    /* Is there enough space in the current command queue? */
    if (bytes < waitLinkBytes)
    {
        /* No, create a new one. */
        gcmkONERROR(_NewQueue(Command));

        /* Get the new current offset. */
        offset = Command->offset;

        /* Recompute the number of bytes in the new kernel command queue. */
        bytes = COMMAND_QUEUE_SIZE - offset;
        gcmkASSERT(bytes >= waitLinkBytes);
    }

    /* Compute the location if WAIT/LINK command sequence. */
    waitLinkPhysical = (u8 *) Command->physical + offset;
    waitLinkLogical  = (u8 *) Command->logical  + offset;

    /* Determine the location to jump to for the command buffer being
    ** scheduled. */
    if (Command->newQueue)
    {
        /* New command queue, jump to the beginning of it. */
#if gcdNONPAGED_MEMORY_CACHEABLE
        exitPhysical = Command->physical;
#endif
        exitLogical  = Command->logical;
        exitBytes    = Command->offset + waitLinkBytes;
    }
    else
    {
        /* Still within the preexisting command queue, jump to the new
           WAIT/LINK command sequence. */
#if gcdNONPAGED_MEMORY_CACHEABLE
        exitPhysical = waitLinkPhysical;
#endif
        exitLogical  = waitLinkLogical;
        exitBytes    = waitLinkBytes;
    }

    /* Add a new WAIT/LINK command sequence. When the command buffer which is
       currently being scheduled is fully executed by the GPU, the FE will
       jump to this WAIT/LINK sequence. */
    gcmkONERROR(gckHARDWARE_WaitLink(
        hardware,
        waitLinkLogical,
        offset,
        &waitLinkBytes,
        &waitOffset,
        &waitSize
        ));

    /* Compute the location if WAIT command. */
    waitPhysical = (u8 *) waitLinkPhysical + waitOffset;
    waitLogical  = (u8 *) waitLinkLogical  + waitOffset;

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the command queue cache. */
    gcmkONERROR(gckOS_CacheClean(
        Command->os,
        Command->kernelProcessID,
        NULL,
        exitPhysical,
        exitLogical,
        exitBytes
        ));
#endif

    /* Determine the location of the LINK command in the command buffer. */
    commandBufferLink
        = (u8 *) commandBufferObject->logical
        +                commandBufferObject->offset;

    /* Generate a LINK from the end of the command buffer being scheduled
       back to the kernel command queue. */
    gcmkONERROR(gckHARDWARE_Link(
        hardware,
        commandBufferLink,
        exitLogical,
        exitBytes,
        &linkBytes
        ));

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the command buffer cache. */
    gcmkONERROR(gckOS_CacheClean(
        Command->os,
        ProcessID,
        NULL,
        commandBufferPhysical,
        commandBufferLogical,
        commandBufferSize
        ));
#endif

    /* Generate a LINK from the previous WAIT/LINK command sequence to the
       entry determined above (either the context or the command buffer).
       This LINK replaces the WAIT instruction from the previous WAIT/LINK
       pair, therefore we use WAIT metrics for generation of this LINK.
       This action will execute the entire sequence. */
    gcmkONERROR(gckHARDWARE_Link(
        hardware,
        Command->waitLogical,
        entryLogical,
        entryBytes,
        &Command->waitSize
        ));

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the cache for the link. */
    gcmkONERROR(gckOS_CacheClean(
        Command->os,
        Command->kernelProcessID,
        NULL,
        Command->waitPhysical,
        Command->waitLogical,
        Command->waitSize
        ));
#endif

    gcmkDUMPCOMMAND(
        Command->os,
        Command->waitLogical,
        Command->waitSize,
        gceDUMP_BUFFER_LINK,
        gcvFALSE
        );

    gcmkDUMPCOMMAND(
        Command->os,
        contextDumpLogical,
        contextDumpBytes,
        gceDUMP_BUFFER_CONTEXT,
        gcvFALSE
        );

    gcmkDUMPCOMMAND(
        Command->os,
        bufferDumpLogical,
        bufferDumpBytes,
        gceDUMP_BUFFER_USER,
        gcvFALSE
        );

    gcmkDUMPCOMMAND(
        Command->os,
        waitLinkLogical,
        waitLinkBytes,
        gceDUMP_BUFFER_WAITLINK,
        gcvFALSE
        );

    /* Update the current pipe. */
    Command->pipeSelect = commandBufferObject->exitPipe;

    /* Update command queue offset. */
    Command->offset  += waitLinkBytes;
    Command->newQueue = gcvFALSE;

    /* Update address of last WAIT. */
    Command->waitPhysical = waitPhysical;
    Command->waitLogical  = waitLogical;
    Command->waitSize     = waitSize;

    /* Update queue tail pointer. */
    gcmkONERROR(gckHARDWARE_UpdateQueueTail(
        hardware, Command->logical, Command->offset
        ));

#if gcdDUMP_COMMAND && !gcdSIMPLE_COMMAND_DUMP
    gcmkPRINT("@[kernel.commit]");
#endif
#endif /* gcdNULL_DRIVER */

    /* Release the context switching mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Command->os, Command->mutexContext));
    contextAcquired = gcvFALSE;

    /* Release the command queue. */
    gcmkONERROR(gckCOMMAND_ExitCommit(Command, gcvFALSE));
    commitEntered = gcvFALSE;

    /* Loop while there are records in the queue. */
    while (EventQueue != NULL)
    {
        if (NO_USER_DIRECT_ACCESS_FROM_KERNEL)
        {
            /* Point to stack record. */
            eventRecord = &_eventRecord;

            /* Copy the data from the client. */
	    if (copy_from_user(eventRecord, EventQueue, sizeof(gcsQUEUE)) != 0)
            {
                gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
            }
        }
        else
        {
            /* Map record into kernel memory. */
            gcmkONERROR(gckOS_MapUserPointer(Command->os,
                                             EventQueue,
                                             sizeof(gcsQUEUE),
                                             &pointer));

            eventRecord = pointer;
        }

        /* Append event record to event queue. */
        gcmkONERROR(gckEVENT_AddList(
            Command->kernel->eventObj, &eventRecord->iface, gcvKERNEL_PIXEL, gcvTRUE
            ));

        /* Next record in the queue. */
        nextEventRecord = eventRecord->next;

        if (!NO_USER_DIRECT_ACCESS_FROM_KERNEL)
        {
            /* Unmap record from kernel memory. */
            gcmkONERROR(gckOS_UnmapUserPointer(
                Command->os, EventQueue, sizeof(gcsQUEUE), (void **) eventRecord
                ));

            eventRecord = NULL;
        }

        EventQueue = nextEventRecord;
    }

    if (Command->kernel->eventObj->queueHead == NULL)
    {
        /* Commit done event by which work thread knows all jobs done. */
        gcmkVERIFY_OK(
            gckEVENT_CommitDone(Command->kernel->eventObj, gcvKERNEL_PIXEL));
    }

    /* Submit events. */
    gcmkONERROR(gckEVENT_Submit(Command->kernel->eventObj, gcvTRUE, gcvFALSE));

    /* Unmap the command buffer pointer. */
    if (commandBufferMapped)
    {
        gcmkONERROR(gckOS_UnmapUserPointer(
            Command->os,
            CommandBuffer,
            sizeof(struct _gcoCMDBUF),
            commandBufferObject
            ));

        commandBufferMapped = gcvFALSE;
    }

    /* Return status. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    if ((eventRecord != NULL) && !NO_USER_DIRECT_ACCESS_FROM_KERNEL)
    {
        /* Roll back. */
        gcmkVERIFY_OK(gckOS_UnmapUserPointer(
            Command->os,
            EventQueue,
            sizeof(gcsQUEUE),
            (void **) eventRecord
            ));
    }

    if (contextAcquired)
    {
        /* Release the context switching mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Command->os, Command->mutexContext));
    }

    if (commitEntered)
    {
        /* Release the command queue mutex. */
        gcmkVERIFY_OK(gckCOMMAND_ExitCommit(Command, gcvFALSE));
    }

    /* Unmap the command buffer pointer. */
    if (commandBufferMapped)
    {
        gcmkVERIFY_OK(gckOS_UnmapUserPointer(
            Command->os,
            CommandBuffer,
            sizeof(struct _gcoCMDBUF),
            commandBufferObject
            ));
    }

    /* Return status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_Reserve
**
**  Reserve space in the command queue.  Also acquire the command queue mutex.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object.
**
**      size_t RequestedBytes
**          Number of bytes previously reserved.
**
**  OUTPUT:
**
**      void ** Buffer
**          Pointer to a variable that will receive the address of the reserved
**          space.
**
**      size_t * BufferSize
**          Pointer to a variable that will receive the number of bytes
**          available in the command queue.
*/
gceSTATUS
gckCOMMAND_Reserve(
    IN gckCOMMAND Command,
    IN size_t RequestedBytes,
    OUT void **Buffer,
    OUT size_t * BufferSize
    )
{
    gceSTATUS status;
    size_t bytes;
    size_t requiredBytes;
    u32 requestedAligned;

    gcmkHEADER_ARG("Command=0x%x RequestedBytes=%lu", Command, RequestedBytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Compute aligned number of reuested bytes. */
    requestedAligned = gcmALIGN(RequestedBytes, Command->alignment);

    /* Another WAIT/LINK command sequence will have to be appended after
       the requested area being reserved. Compute the number of bytes
       required for WAIT/LINK at the location after the reserved area. */
    gcmkONERROR(gckHARDWARE_WaitLink(
        Command->kernel->hardware,
        NULL,
        Command->offset + requestedAligned,
        &requiredBytes,
        NULL,
        NULL
        ));

    /* Compute total number of bytes required. */
    requiredBytes += requestedAligned;

    /* Compute number of bytes available in command queue. */
    bytes = COMMAND_QUEUE_SIZE - Command->offset;

    /* Is there enough space in the current command queue? */
    if (bytes < requiredBytes)
    {
        /* Create a new command queue. */
        gcmkONERROR(_NewQueue(Command));

        /* Recompute the number of bytes in the new kernel command queue. */
        bytes = COMMAND_QUEUE_SIZE - Command->offset;

        /* Still not enough space? */
        if (bytes < requiredBytes)
        {
            /* Rare case, not enough room in command queue. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }
    }

    /* Return pointer to empty slot command queue. */
    *Buffer = (u8 *) Command->logical + Command->offset;

    /* Return number of bytes left in command queue. */
    *BufferSize = bytes;

    /* Success. */
    gcmkFOOTER_ARG("*Buffer=0x%x *BufferSize=%lu", *Buffer, *BufferSize);
    return gcvSTATUS_OK;

OnError:
    /* Return status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_Execute
**
**  Execute a previously reserved command queue by appending a WAIT/LINK command
**  sequence after it and modifying the last WAIT into a LINK command.  The
**  command FIFO mutex will be released whether this function succeeds or not.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object.
**
**      size_t RequestedBytes
**          Number of bytes previously reserved.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Execute(
    IN gckCOMMAND Command,
    IN size_t RequestedBytes
    )
{
    gceSTATUS status;

    gctPHYS_ADDR waitLinkPhysical;
    u8 *waitLinkLogical;
    u32 waitLinkOffset;
    size_t waitLinkBytes;

    gctPHYS_ADDR waitPhysical;
    void *waitLogical;
    u32 waitOffset;
    size_t waitBytes;

#if gcdNONPAGED_MEMORY_CACHEABLE
    gctPHYS_ADDR execPhysical;
#endif
    void *execLogical;
    size_t execBytes;

    gcmkHEADER_ARG("Command=0x%x RequestedBytes=%lu", Command, RequestedBytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Compute offset for WAIT/LINK. */
    waitLinkOffset = Command->offset + RequestedBytes;

    /* Compute number of bytes left in command queue. */
    waitLinkBytes = COMMAND_QUEUE_SIZE - waitLinkOffset;

    /* Compute the location if WAIT/LINK command sequence. */
    waitLinkPhysical = (u8 *) Command->physical + waitLinkOffset;
    waitLinkLogical  = (u8 *) Command->logical  + waitLinkOffset;

    /* Append WAIT/LINK in command queue. */
    gcmkONERROR(gckHARDWARE_WaitLink(
        Command->kernel->hardware,
        waitLinkLogical,
        waitLinkOffset,
        &waitLinkBytes,
        &waitOffset,
        &waitBytes
        ));

    /* Compute the location if WAIT command. */
    waitPhysical = (u8 *) waitLinkPhysical + waitOffset;
    waitLogical  =                waitLinkLogical  + waitOffset;

    /* Determine the location to jump to for the command buffer being
    ** scheduled. */
    if (Command->newQueue)
    {
        /* New command queue, jump to the beginning of it. */
#if gcdNONPAGED_MEMORY_CACHEABLE
        execPhysical = Command->physical;
#endif
        execLogical  = Command->logical;
        execBytes    = waitLinkOffset + waitLinkBytes;
    }
    else
    {
        /* Still within the preexisting command queue, jump directly to the
           reserved area. */
#if gcdNONPAGED_MEMORY_CACHEABLE
        execPhysical = (u8 *) Command->physical + Command->offset;
#endif
        execLogical  = (u8 *) Command->logical  + Command->offset;
        execBytes    = RequestedBytes + waitLinkBytes;
    }

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the cache. */
    gcmkONERROR(gckOS_CacheClean(
        Command->os,
        Command->kernelProcessID,
        NULL,
        execPhysical,
        execLogical,
        execBytes
        ));
#endif

    /* Convert the last WAIT into a LINK. */
    gcmkONERROR(gckHARDWARE_Link(
        Command->kernel->hardware,
        Command->waitLogical,
        execLogical,
        execBytes,
        &Command->waitSize
        ));

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the cache. */
    gcmkONERROR(gckOS_CacheClean(
        Command->os,
        Command->kernelProcessID,
        NULL,
        Command->waitPhysical,
        Command->waitLogical,
        Command->waitSize
        ));
#endif

    gcmkDUMPCOMMAND(
        Command->os,
        Command->waitLogical,
        Command->waitSize,
        gceDUMP_BUFFER_LINK,
        gcvFALSE
        );

    gcmkDUMPCOMMAND(
        Command->os,
        execLogical,
        execBytes,
        gceDUMP_BUFFER_KERNEL,
        gcvFALSE
        );

    /* Update the pointer to the last WAIT. */
    Command->waitPhysical = waitPhysical;
    Command->waitLogical  = waitLogical;
    Command->waitSize     = waitBytes;

    /* Update the command queue. */
    Command->offset  += RequestedBytes + waitLinkBytes;
    Command->newQueue = gcvFALSE;

    /* Update queue tail pointer. */
    gcmkONERROR(gckHARDWARE_UpdateQueueTail(
        Command->kernel->hardware, Command->logical, Command->offset
        ));

#if gcdDUMP_COMMAND && !gcdSIMPLE_COMMAND_DUMP
    gcmkPRINT("@[kernel.execute]");
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_Stall
**
**  The calling thread will be suspended until the command queue has been
**  completed.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to an gckCOMMAND object.
**
**      int FromPower
**          Determines whether the call originates from inside the power
**          management or not.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Stall(
    IN gckCOMMAND Command,
    IN int FromPower
    )
{
#if gcdNULL_DRIVER
    /* Do nothing with infinite hardware. */
    return gcvSTATUS_OK;
#else
    gckOS os;
    gckHARDWARE hardware;
    gckEVENT eventObject;
    gceSTATUS status;
    gctSIGNAL signal = NULL;
    unsigned int timer = 0;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Extract the gckOS object pointer. */
    os = Command->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Extract the gckHARDWARE object pointer. */
    hardware = Command->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    /* Extract the gckEVENT object pointer. */
    eventObject = Command->kernel->eventObj;
    gcmkVERIFY_OBJECT(eventObject, gcvOBJ_EVENT);

    /* Allocate the signal. */
    gcmkONERROR(gckOS_CreateSignal(os, gcvTRUE, &signal));

    /* Append the EVENT command to trigger the signal. */
    gcmkONERROR(gckEVENT_Signal(eventObject, signal, gcvKERNEL_PIXEL));

    /* Submit the event queue. */
    gcmkONERROR(gckEVENT_Submit(eventObject, gcvTRUE, FromPower));

#if gcdDUMP_COMMAND && !gcdSIMPLE_COMMAND_DUMP
    gcmkPRINT("@[kernel.stall]");
#endif

    if (status == gcvSTATUS_CHIP_NOT_READY)
    {
        /* Error. */
        goto OnError;
    }

    do
    {
        /* Wait for the signal. */
        status = gckOS_WaitSignal(os, signal, gcdGPU_ADVANCETIMER);

        if (status == gcvSTATUS_TIMEOUT)
        {
#if gcmIS_DEBUG(gcdDEBUG_CODE)
            u32 idle;

            /* Read idle register. */
            gcmkVERIFY_OK(gckHARDWARE_GetIdle(
                hardware, gcvFALSE, &idle
                ));

            gcmkTRACE(
                gcvLEVEL_ERROR,
                "%s(%d): idle=%08x",
                __FUNCTION__, __LINE__, idle
                );

            gcmkONERROR(gckOS_MemoryBarrier(os, NULL));
#endif
            /* Advance timer. */
            timer += gcdGPU_ADVANCETIMER;
        }
        else if (status == gcvSTATUS_INTERRUPTED)
        {
            gcmkONERROR(gcvSTATUS_INTERRUPTED);
        }

    }
    while (gcmIS_ERROR(status)
#if gcdGPU_TIMEOUT
           && (timer < Command->kernel->timeOut)
#endif
           );

    /* Bail out on timeout. */
    if (gcmIS_ERROR(status))
    {
        /* Broadcast the stuck GPU. */
        gcmkONERROR(gckOS_Broadcast(
            os, hardware, gcvBROADCAST_GPU_STUCK
            ));

        gcmkONERROR(gcvSTATUS_GPU_NOT_RESPONDING);
    }

    /* Delete the signal. */
    gcmkVERIFY_OK(gckOS_DestroySignal(os, signal));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (signal != NULL)
    {
        /* Free the signal. */
        gcmkVERIFY_OK(gckOS_DestroySignal(os, signal));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
#endif
}

/*******************************************************************************
**
**  gckCOMMAND_Attach
**
**  Attach user process.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to a gckCOMMAND object.
**
**      u32 ProcessID
**          Current process ID.
**
**  OUTPUT:
**
**      gckCONTEXT * Context
**          Pointer to a variable that will receive a pointer to a new
**          gckCONTEXT object.
**
**      size_t * StateCount
**          Pointer to a variable that will receive the number of states
**          in the context buffer.
*/
gceSTATUS
gckCOMMAND_Attach(
    IN gckCOMMAND Command,
    OUT gckCONTEXT * Context,
    OUT size_t * StateCount,
    IN u32 ProcessID
    )
{
    gceSTATUS status;
    int acquired = gcvFALSE;

    gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Acquire the context switching mutex. */
    gcmkONERROR(gckOS_AcquireMutex(
        Command->os, Command->mutexContext, gcvINFINITE
        ));
    acquired = gcvTRUE;

    /* Construct a gckCONTEXT object. */
    gcmkONERROR(gckCONTEXT_Construct(
        Command->os,
        Command->kernel->hardware,
        ProcessID,
        Context
        ));

    /* Return the number of states in the context. */
    * StateCount = (* Context)->stateCount;

    /* Release the context switching mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Command->os, Command->mutexContext));
    acquired = gcvFALSE;

    /* Success. */
    gcmkFOOTER_ARG("*Context=0x%x", *Context);
    return gcvSTATUS_OK;

OnError:
    /* Release mutex. */
    if (acquired)
    {
        /* Release the context switching mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Command->os, Command->mutexContext));
        acquired = gcvFALSE;
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckCOMMAND_Detach
**
**  Detach user process.
**
**  INPUT:
**
**      gckCOMMAND Command
**          Pointer to a gckCOMMAND object.
**
**      gckCONTEXT Context
**          Pointer to a gckCONTEXT object to be destroyed.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCOMMAND_Detach(
    IN gckCOMMAND Command,
    IN gckCONTEXT Context
    )
{
    gceSTATUS status;
    int acquired = gcvFALSE;

    gcmkHEADER_ARG("Command=0x%x Context=0x%x", Command, Context);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Acquire the context switching mutex. */
    gcmkONERROR(gckOS_AcquireMutex(
        Command->os, Command->mutexContext, gcvINFINITE
        ));
    acquired = gcvTRUE;

    /* Construct a gckCONTEXT object. */
    gcmkONERROR(gckCONTEXT_Destroy(Context));

    /* Release the context switching mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Command->os, Command->mutexContext));
    acquired = gcvFALSE;

    /* Return the status. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    /* Release mutex. */
    if (acquired)
    {
        /* Release the context switching mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Command->os, Command->mutexContext));
        acquired = gcvFALSE;
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}
