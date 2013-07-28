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


#include "gc_hal_kernel_precomp.h"
#include "gc_hal_kernel_buffer.h"

#ifdef __QNXNTO__
#include <atomic.h>
#include "gc_hal_kernel_qnx.h"
#endif

#define _GC_OBJ_ZONE                    gcvZONE_EVENT

#define gcdEVENT_ALLOCATION_COUNT       (4096 / gcmSIZEOF(gcsHAL_INTERFACE))
#define gcdEVENT_MIN_THRESHOLD          4

/******************************************************************************\
********************************* Support Code *********************************
\******************************************************************************/

static gceSTATUS
gckEVENT_AllocateQueue(
    IN gckEVENT Event,
    OUT gcsEVENT_QUEUE_PTR * Queue
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Event=0x%x", Event);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
    gcmkVERIFY_ARGUMENT(Queue != gcvNULL);

    /* Do we have free queues? */
    if (Event->freeList == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
    }

    /* Move one free queue from the free list. */
    * Queue = Event->freeList;
    Event->freeList = Event->freeList->next;

    /* Success. */
    gcmkFOOTER_ARG("*Queue=0x%x", gcmOPT_POINTER(Queue));
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

static gceSTATUS
gckEVENT_FreeQueue(
    IN gckEVENT Event,
    OUT gcsEVENT_QUEUE_PTR Queue
    )
{
    gceSTATUS status = gcvSTATUS_OK;

    gcmkHEADER_ARG("Event=0x%x", Event);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
    gcmkVERIFY_ARGUMENT(Queue != gcvNULL);

    /* Move one free queue from the free list. */
    Queue->next = Event->freeList;
    Event->freeList = Queue;

    /* Success. */
    gcmkFOOTER();
    return status;
}

static gceSTATUS
gckEVENT_FreeRecord(
    IN gckEVENT Event,
    IN gcsEVENT_PTR Record
    )
{
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Event=0x%x Record=0x%x", Event, Record);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
    gcmkVERIFY_ARGUMENT(Record != gcvNULL);

    /* Acquire the mutex. */
    gcmkONERROR(gckOS_AcquireMutex(Event->os,
                                   Event->freeEventMutex,
                                   gcvINFINITE));
    acquired = gcvTRUE;

    /* Push the record on the free list. */
    Record->next           = Event->freeEventList;
    Event->freeEventList   = Record;
    Event->freeEventCount += 1;

    /* Release the mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Event->os, Event->freeEventMutex));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (acquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->freeEventMutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return gcvSTATUS_OK;
}

static gceSTATUS
gckEVENT_IsEmpty(
    IN gckEVENT Event,
    OUT gctBOOL_PTR IsEmpty
    )
{
    gceSTATUS status;
    gctSIZE_T i;

    gcmkHEADER_ARG("Event=0x%x", Event);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
    gcmkVERIFY_ARGUMENT(IsEmpty != gcvNULL);

    /* Assume the event queue is empty. */
    *IsEmpty = gcvTRUE;

    /* Walk the event queue. */
    for (i = 0; i < gcmCOUNTOF(Event->queues); ++i)
    {
        /* Check whether this event is in use. */
        if (Event->queues[i].head != gcvNULL)
        {
            /* The event is in use, hence the queue is not empty. */
            *IsEmpty = gcvFALSE;
            break;
        }
    }

    /* Try acquiring the mutex. */
    status = gckOS_AcquireMutex(Event->os, Event->eventQueueMutex, 0);
    if (status == gcvSTATUS_TIMEOUT)
    {
        /* Timeout - queue is no longer empty. */
        *IsEmpty = gcvFALSE;
    }
    else
    {
        /* Bail out on error. */
        gcmkONERROR(status);

        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->eventQueueMutex));
    }

    /* Success. */
    gcmkFOOTER_ARG("*IsEmpty=%d", gcmOPT_VALUE(IsEmpty));
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

static gceSTATUS
_TryToIdleGPU(
    IN gckEVENT Event
)
{
    gceSTATUS status;
    gctBOOL empty = gcvFALSE, idle = gcvFALSE;
    gctUINT32 process, thread;
    gctBOOL powerLocked = gcvFALSE;
    gckHARDWARE hardware;


    gcmkHEADER_ARG("Event=0x%x", Event);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);

    /* Grab gckHARDWARE object. */
    hardware = Event->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);


    /* Check whether the event queue is empty. */
    gcmkONERROR(gckEVENT_IsEmpty(Event, &empty));

    if (empty)
    {
        status = gckOS_AcquireMutex(hardware->os, hardware->powerMutex, 0);
        if (status == gcvSTATUS_TIMEOUT)
        {
            gcmkONERROR(gckOS_GetProcessID(&process));
            gcmkONERROR(gckOS_GetThreadID(&thread));

            /* Just return to prevent deadlock. */
            if ((hardware->powerProcess != process)
            ||  (hardware->powerThread  != thread))
            {
                gcmkFOOTER_NO();
                return gcvSTATUS_OK;
            }
        }
        else
        {
            powerLocked = gcvTRUE;
        }

        /* Query whether the hardware is idle. */
        gcmkONERROR(gckHARDWARE_QueryIdle(Event->kernel->hardware, &idle));

        if (powerLocked)
        {
            gcmkONERROR(gckOS_ReleaseMutex(hardware->os, hardware->powerMutex));
            powerLocked = gcvFALSE;
        }

        if (idle)
        {
            /* Inform the system of idle GPU. */
            gcmkONERROR(gckOS_Broadcast(Event->os,
                                        Event->kernel->hardware,
                                        gcvBROADCAST_GPU_IDLE));
        }
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:

    if (powerLocked)
    {
        gcmkONERROR(gckOS_ReleaseMutex(hardware->os, hardware->powerMutex));
        powerLocked = gcvFALSE;
    }

    gcmkFOOTER();
    return status;
}

static gceSTATUS
__RemoveRecordFromProcessDB(
    IN gckEVENT Event,
    IN gcsEVENT_PTR Record
    )
{
    gcmkHEADER_ARG("Event=0x%x Record=0x%x", Event, Record);
    gcmkVERIFY_ARGUMENT(Record != gcvNULL);

    while (Record != gcvNULL)
    {
        if (Record->info.command == gcvHAL_SIGNAL)
        {
            /* TODO: Find a better place to bind signal to hardware.*/
            gcmkVERIFY_OK(gckOS_SignalSetHardware(Event->os,
                        gcmUINT64_TO_PTR(Record->info.u.Signal.signal),
                        Event->kernel->hardware));
        }

        if (Record->fromKernel)
        {
            /* No need to check db if event is from kernel. */
            Record = Record->next;
            continue;
        }

        switch (Record->info.command)
        {
        case gcvHAL_FREE_NON_PAGED_MEMORY:
            gcmkVERIFY_OK(gckKERNEL_RemoveProcessDB(
                Event->kernel,
                Record->processID,
                gcvDB_NON_PAGED,
                gcmUINT64_TO_PTR(Record->info.u.FreeNonPagedMemory.logical)));
            break;

        case gcvHAL_FREE_CONTIGUOUS_MEMORY:
            gcmkVERIFY_OK(gckKERNEL_RemoveProcessDB(
                Event->kernel,
                Record->processID,
                gcvDB_CONTIGUOUS,
                gcmUINT64_TO_PTR(Record->info.u.FreeContiguousMemory.logical)));
            break;

        case gcvHAL_FREE_VIDEO_MEMORY:
            gcmkVERIFY_OK(gckKERNEL_RemoveProcessDB(
                Event->kernel,
                Record->processID,
                gcvDB_VIDEO_MEMORY,
                gcmUINT64_TO_PTR(Record->info.u.FreeVideoMemory.node)));
            break;

        case gcvHAL_UNLOCK_VIDEO_MEMORY:
            gcmkVERIFY_OK(gckKERNEL_RemoveProcessDB(
                Event->kernel,
                Record->processID,
                gcvDB_VIDEO_MEMORY_LOCKED,
                gcmUINT64_TO_PTR(Record->info.u.UnlockVideoMemory.node)));
            break;

        case gcvHAL_UNMAP_USER_MEMORY:
            gcmkVERIFY_OK(gckKERNEL_RemoveProcessDB(
                Event->kernel,
                Record->processID,
                gcvDB_MAP_USER_MEMORY,
                gcmINT2PTR(Record->info.u.UnmapUserMemory.info)));
            break;

        case gcvHAL_FREE_VIRTUAL_COMMAND_BUFFER:
            gcmkVERIFY_OK(gckKERNEL_RemoveProcessDB(
                Event->kernel,
                Record->processID,
                gcvDB_COMMAND_BUFFER,
                gcmUINT64_TO_PTR(Record->info.u.FreeVirtualCommandBuffer.logical)));
            break;

        default:
            break;
        }

        Record = Record->next;
    }
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

void
_SubmitTimerFunction(
    gctPOINTER Data
    )
{
    gckEVENT event = (gckEVENT)Data;
    gcmkVERIFY_OK(gckEVENT_Submit(event, gcvTRUE, gcvFALSE));
}

/******************************************************************************\
******************************* gckEVENT API Code *******************************
\******************************************************************************/

/*******************************************************************************
**
**  gckEVENT_Construct
**
**  Construct a new gckEVENT object.
**
**  INPUT:
**
**      gckKERNEL Kernel
**          Pointer to an gckKERNEL object.
**
**  OUTPUT:
**
**      gckEVENT * Event
**          Pointer to a variable that receives the gckEVENT object pointer.
*/
gceSTATUS
gckEVENT_Construct(
    IN gckKERNEL Kernel,
    OUT gckEVENT * Event
    )
{
    gckOS os;
    gceSTATUS status;
    gckEVENT eventObj = gcvNULL;
    int i;
    gcsEVENT_PTR record;
    gctPOINTER pointer = gcvNULL;

    gcmkHEADER_ARG("Kernel=0x%x", Kernel);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(Event != gcvNULL);

    /* Extract the pointer to the gckOS object. */
    os = Kernel->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Allocate the gckEVENT object. */
    gcmkONERROR(gckOS_Allocate(os, gcmSIZEOF(struct _gckEVENT), &pointer));

    eventObj = pointer;

    /* Reset the object. */
    gcmkVERIFY_OK(gckOS_ZeroMemory(eventObj, gcmSIZEOF(struct _gckEVENT)));

    /* Initialize the gckEVENT object. */
    eventObj->object.type = gcvOBJ_EVENT;
    eventObj->kernel      = Kernel;
    eventObj->os          = os;

    /* Create the mutexes. */
    gcmkONERROR(gckOS_CreateMutex(os, &eventObj->eventQueueMutex));
    gcmkONERROR(gckOS_CreateMutex(os, &eventObj->freeEventMutex));
    gcmkONERROR(gckOS_CreateMutex(os, &eventObj->eventListMutex));

    /* Create a bunch of event reccords. */
    for (i = 0; i < gcdEVENT_ALLOCATION_COUNT; i += 1)
    {
        /* Allocate an event record. */
        gcmkONERROR(gckOS_Allocate(os, gcmSIZEOF(gcsEVENT), &pointer));

        record = pointer;

        /* Push it on the free list. */
        record->next              = eventObj->freeEventList;
        eventObj->freeEventList   = record;
        eventObj->freeEventCount += 1;
    }

    /* Initialize the free list of event queues. */
    for (i = 0; i < gcdREPO_LIST_COUNT; i += 1)
    {
        eventObj->repoList[i].next = eventObj->freeList;
        eventObj->freeList = &eventObj->repoList[i];
    }

    /* Construct the atom. */
    gcmkONERROR(gckOS_AtomConstruct(os, &eventObj->freeAtom));
    gcmkONERROR(gckOS_AtomSet(os,
                              eventObj->freeAtom,
                              gcmCOUNTOF(eventObj->queues)));

#if gcdSMP
    gcmkONERROR(gckOS_AtomConstruct(os, &eventObj->pending));
#endif

    gcmkVERIFY_OK(gckOS_CreateTimer(os,
                                    _SubmitTimerFunction,
                                    (gctPOINTER)eventObj,
                                    &eventObj->submitTimer));

    /* Return pointer to the gckEVENT object. */
    *Event = eventObj;

    /* Success. */
    gcmkFOOTER_ARG("*Event=0x%x", *Event);
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (eventObj != gcvNULL)
    {
        if (eventObj->eventQueueMutex != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_DeleteMutex(os, eventObj->eventQueueMutex));
        }

        if (eventObj->freeEventMutex != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_DeleteMutex(os, eventObj->freeEventMutex));
        }

        if (eventObj->eventListMutex != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_DeleteMutex(os, eventObj->eventListMutex));
        }

        while (eventObj->freeEventList != gcvNULL)
        {
            record = eventObj->freeEventList;
            eventObj->freeEventList = record->next;

            gcmkVERIFY_OK(gcmkOS_SAFE_FREE(os, record));
        }

        if (eventObj->freeAtom != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_AtomDestroy(os, eventObj->freeAtom));
        }

#if gcdSMP
        if (eventObj->pending != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_AtomDestroy(os, eventObj->pending));
        }
#endif
        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(os, eventObj));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckEVENT_Destroy
**
**  Destroy an gckEVENT object.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckEVENT_Destroy(
    IN gckEVENT Event
    )
{
    gcsEVENT_PTR record;
    gcsEVENT_QUEUE_PTR queue;

    gcmkHEADER_ARG("Event=0x%x", Event);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);

    if (Event->submitTimer != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_StopTimer(Event->os, Event->submitTimer));
        gcmkVERIFY_OK(gckOS_DestroyTimer(Event->os, Event->submitTimer));
    }

    /* Delete the queue mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Event->os, Event->eventQueueMutex));

    /* Free all free events. */
    while (Event->freeEventList != gcvNULL)
    {
        record = Event->freeEventList;
        Event->freeEventList = record->next;

        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Event->os, record));
    }

    /* Delete the free mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Event->os, Event->freeEventMutex));

    /* Free all pending queues. */
    while (Event->queueHead != gcvNULL)
    {
        /* Get the current queue. */
        queue = Event->queueHead;

        /* Free all pending events. */
        while (queue->head != gcvNULL)
        {
            record      = queue->head;
            queue->head = record->next;

            gcmkTRACE_ZONE_N(
                gcvLEVEL_WARNING, gcvZONE_EVENT,
                gcmSIZEOF(record) + gcmSIZEOF(queue->source),
                "Event record 0x%x is still pending for %d.",
                record, queue->source
                );

            gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Event->os, record));
        }

        /* Remove the top queue from the list. */
        if (Event->queueHead == Event->queueTail)
        {
            Event->queueHead =
            Event->queueTail = gcvNULL;
        }
        else
        {
            Event->queueHead = Event->queueHead->next;
        }

        /* Free the queue. */
        gcmkVERIFY_OK(gckEVENT_FreeQueue(Event, queue));
    }

    /* Delete the list mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Event->os, Event->eventListMutex));

    /* Delete the atom. */
    gcmkVERIFY_OK(gckOS_AtomDestroy(Event->os, Event->freeAtom));

#if gcdSMP
    gcmkVERIFY_OK(gckOS_AtomDestroy(Event->os, Event->pending));
#endif

    /* Mark the gckEVENT object as unknown. */
    Event->object.type = gcvOBJ_UNKNOWN;

    /* Free the gckEVENT object. */
    gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Event->os, Event));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckEVENT_GetEvent
**
**  Reserve the next available hardware event.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gctBOOL Wait
**          Set to gcvTRUE to force the function to wait if no events are
**          immediately available.
**
**      gceKERNEL_WHERE Source
**          Source of the event.
**
**  OUTPUT:
**
**      gctUINT8 * EventID
**          Reserved event ID.
*/
gceSTATUS
gckEVENT_GetEvent(
    IN gckEVENT Event,
    IN gctBOOL Wait,
    OUT gctUINT8 * EventID,
    IN gceKERNEL_WHERE Source
    )
{
    gctINT i, id;
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;
    gctINT32 free;

#if gcdGPU_TIMEOUT
    gctUINT32 timer = 0;
#endif

    gcmkHEADER_ARG("Event=0x%x Source=%d", Event, Source);

    while (gcvTRUE)
    {
        /* Grab the queue mutex. */
        gcmkONERROR(gckOS_AcquireMutex(Event->os,
                                       Event->eventQueueMutex,
                                       gcvINFINITE));
        acquired = gcvTRUE;

        /* Walk through all events. */
        id = Event->lastID;
        for (i = 0; i < gcmCOUNTOF(Event->queues); ++i)
        {
            gctINT nextID = gckMATH_ModuloInt((id + 1),
                                              gcmCOUNTOF(Event->queues));

            if (Event->queues[id].head == gcvNULL)
            {
                *EventID = (gctUINT8) id;

                Event->lastID = (gctUINT8) nextID;

                /* Save time stamp of event. */
                Event->queues[id].stamp  = ++(Event->stamp);
                Event->queues[id].source = Source;

                gcmkONERROR(gckOS_AtomDecrement(Event->os,
                                                Event->freeAtom,
                                                &free));
#if gcdDYNAMIC_SPEED
                if (free <= gcdDYNAMIC_EVENT_THRESHOLD)
                {
                    gcmkONERROR(gckOS_BroadcastHurry(
                        Event->os,
                        Event->kernel->hardware,
                        gcdDYNAMIC_EVENT_THRESHOLD - free));
                }
#endif

                /* Release the queue mutex. */
                gcmkONERROR(gckOS_ReleaseMutex(Event->os,
                                               Event->eventQueueMutex));

                /* Success. */
                gcmkTRACE_ZONE_N(
                    gcvLEVEL_INFO, gcvZONE_EVENT,
                    gcmSIZEOF(id),
                    "Using id=%d",
                    id
                    );

                gcmkFOOTER_ARG("*EventID=%u", *EventID);
                return gcvSTATUS_OK;
            }

            id = nextID;
        }

#if gcdDYNAMIC_SPEED
        /* No free events, speed up the GPU right now! */
        gcmkONERROR(gckOS_BroadcastHurry(Event->os,
                                         Event->kernel->hardware,
                                         gcdDYNAMIC_EVENT_THRESHOLD));
#endif

        /* Release the queue mutex. */
        gcmkONERROR(gckOS_ReleaseMutex(Event->os, Event->eventQueueMutex));
        acquired = gcvFALSE;

        /* Fail if wait is not requested. */
        if (!Wait)
        {
            /* Out of resources. */
            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
        }

        /* Delay a while. */
        gcmkONERROR(gckOS_Delay(Event->os, 1));

#if gcdGPU_TIMEOUT
        /* Increment the wait timer. */
        timer += 1;

        if (timer == gcdGPU_TIMEOUT)
        {
            /* Try to call any outstanding events. */
            gcmkONERROR(gckHARDWARE_Interrupt(Event->kernel->hardware,
                                              gcvTRUE));
        }
        else if (timer > gcdGPU_TIMEOUT)
        {
            gcmkTRACE_N(
                gcvLEVEL_ERROR,
                gcmSIZEOF(gctCONST_STRING) + gcmSIZEOF(gctINT),
                "%s(%d): no available events\n",
                __FUNCTION__, __LINE__
                );

            /* Bail out. */
            gcmkONERROR(gcvSTATUS_GPU_NOT_RESPONDING);
        }
#endif
    }

OnError:
    if (acquired)
    {
        /* Release the queue mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->eventQueueMutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckEVENT_AllocateRecord
**
**  Allocate a record for the new event.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gctBOOL AllocateAllowed
**          State for allocation if out of free events.
**
**  OUTPUT:
**
**      gcsEVENT_PTR * Record
**          Allocated event record.
*/
gceSTATUS
gckEVENT_AllocateRecord(
    IN gckEVENT Event,
    IN gctBOOL AllocateAllowed,
    OUT gcsEVENT_PTR * Record
    )
{
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;
    gctINT i;
    gcsEVENT_PTR record;
    gctPOINTER pointer = gcvNULL;

    gcmkHEADER_ARG("Event=0x%x AllocateAllowed=%d", Event, AllocateAllowed);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
    gcmkVERIFY_ARGUMENT(Record != gcvNULL);

    /* Acquire the mutex. */
    gcmkONERROR(gckOS_AcquireMutex(Event->os, Event->freeEventMutex, gcvINFINITE));
    acquired = gcvTRUE;

    /* Test if we are below the allocation threshold. */
    if ( (AllocateAllowed && (Event->freeEventCount < gcdEVENT_MIN_THRESHOLD)) ||
         (Event->freeEventCount == 0) )
    {
        /* Allocate a bunch of records. */
        for (i = 0; i < gcdEVENT_ALLOCATION_COUNT; i += 1)
        {
            /* Allocate an event record. */
            gcmkONERROR(gckOS_Allocate(Event->os,
                                       gcmSIZEOF(gcsEVENT),
                                       &pointer));

            record = pointer;

            /* Push it on the free list. */
            record->next           = Event->freeEventList;
            Event->freeEventList   = record;
            Event->freeEventCount += 1;
        }
    }

    *Record                = Event->freeEventList;
    Event->freeEventList   = Event->freeEventList->next;
    Event->freeEventCount -= 1;

    /* Release the mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Event->os, Event->freeEventMutex));
    acquired = gcvFALSE;

    /* Success. */
    gcmkFOOTER_ARG("*Record=0x%x", gcmOPT_POINTER(Record));
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (acquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->freeEventMutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckEVENT_AddList
**
**  Add a new event to the list of events.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gcsHAL_INTERFACE_PTR Interface
**          Pointer to the interface for the event to be added.
**
**      gceKERNEL_WHERE FromWhere
**          Place in the pipe where the event needs to be generated.
**
**      gctBOOL AllocateAllowed
**          State for allocation if out of free events.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckEVENT_AddList(
    IN gckEVENT Event,
    IN gcsHAL_INTERFACE_PTR Interface,
    IN gceKERNEL_WHERE FromWhere,
    IN gctBOOL AllocateAllowed,
    IN gctBOOL FromKernel
    )
{
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;
    gcsEVENT_PTR record = gcvNULL;
    gcsEVENT_QUEUE_PTR queue;
    gckKERNEL kernel = Event->kernel;

    gcmkHEADER_ARG("Event=0x%x Interface=0x%x",
                   Event, Interface);

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, _GC_OBJ_ZONE,
                    "FromWhere=%d AllocateAllowed=%d",
                    FromWhere, AllocateAllowed);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
    gcmkVERIFY_ARGUMENT(Interface != gcvNULL);

    /* Verify the event command. */
    gcmkASSERT
        (  (Interface->command == gcvHAL_FREE_NON_PAGED_MEMORY)
        || (Interface->command == gcvHAL_FREE_CONTIGUOUS_MEMORY)
        || (Interface->command == gcvHAL_FREE_VIDEO_MEMORY)
        || (Interface->command == gcvHAL_WRITE_DATA)
        || (Interface->command == gcvHAL_UNLOCK_VIDEO_MEMORY)
        || (Interface->command == gcvHAL_SIGNAL)
        || (Interface->command == gcvHAL_UNMAP_USER_MEMORY)
        || (Interface->command == gcvHAL_TIMESTAMP)
        || (Interface->command == gcvHAL_COMMIT_DONE)
        || (Interface->command == gcvHAL_FREE_VIRTUAL_COMMAND_BUFFER)
        );

    /* Validate the source. */
    if ((FromWhere != gcvKERNEL_COMMAND) && (FromWhere != gcvKERNEL_PIXEL))
    {
        /* Invalid argument. */
        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    /* Allocate a free record. */
    gcmkONERROR(gckEVENT_AllocateRecord(Event, AllocateAllowed, &record));

    /* Termninate the record. */
    record->next = gcvNULL;

    /* Record the committer. */
    record->fromKernel = FromKernel;

    /* Copy the event interface into the record. */
    gckOS_MemCopy(&record->info, Interface, gcmSIZEOF(record->info));

    /* Get process ID. */
    gcmkONERROR(gckOS_GetProcessID(&record->processID));

#ifdef __QNXNTO__
    record->kernel = Event->kernel;
#endif

    /* Acquire the mutex. */
    gcmkONERROR(gckOS_AcquireMutex(Event->os, Event->eventListMutex, gcvINFINITE));
    acquired = gcvTRUE;

    /* Do we need to allocate a new queue? */
    if ((Event->queueTail == gcvNULL) || (Event->queueTail->source < FromWhere))
    {
        /* Allocate a new queue. */
        gcmkONERROR(gckEVENT_AllocateQueue(Event, &queue));

        /* Initialize the queue. */
        queue->source = FromWhere;
        queue->head   = gcvNULL;
        queue->next   = gcvNULL;

        /* Attach it to the list of allocated queues. */
        if (Event->queueTail == gcvNULL)
        {
            Event->queueHead =
            Event->queueTail = queue;
        }
        else
        {
            Event->queueTail->next = queue;
            Event->queueTail       = queue;
        }
    }
    else
    {
        queue = Event->queueTail;
    }

    /* Attach the record to the queue. */
    if (queue->head == gcvNULL)
    {
        queue->head = record;
        queue->tail = record;
    }
    else
    {
        queue->tail->next = record;
        queue->tail       = record;
    }

    /* Unmap user space logical address.
     * Linux kernel does not support unmap the memory of other process any more since 3.5.
     * Let's unmap memory of self process before submit the event to gpu.
     * */
    switch(Interface->command)
    {
    case gcvHAL_FREE_NON_PAGED_MEMORY:
        gcmkONERROR(gckOS_UnmapUserLogical(
                        Event->os,
                        gcmNAME_TO_PTR(Interface->u.FreeNonPagedMemory.physical),
                        (gctSIZE_T) Interface->u.FreeNonPagedMemory.bytes,
                        gcmUINT64_TO_PTR(Interface->u.FreeNonPagedMemory.logical)));
        break;
    case gcvHAL_FREE_CONTIGUOUS_MEMORY:
        gcmkONERROR(gckOS_UnmapUserLogical(
                        Event->os,
                        gcmNAME_TO_PTR(Interface->u.FreeContiguousMemory.physical),
                        (gctSIZE_T) Interface->u.FreeContiguousMemory.bytes,
                        gcmUINT64_TO_PTR(Interface->u.FreeContiguousMemory.logical)));
        break;
    default:
        break;
    }


    /* Release the mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Event->os, Event->eventListMutex));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (acquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->eventListMutex));
    }

    if (record != gcvNULL)
    {
        gcmkVERIFY_OK(gckEVENT_FreeRecord(Event, record));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckEVENT_Unlock
**
**  Schedule an event to unlock virtual memory.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gceKERNEL_WHERE FromWhere
**          Place in the pipe where the event needs to be generated.
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a gcuVIDMEM_NODE union that specifies the virtual memory
**          to unlock.
**
**      gceSURF_TYPE Type
**          Type of surface to unlock.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckEVENT_Unlock(
    IN gckEVENT Event,
    IN gceKERNEL_WHERE FromWhere,
    IN gcuVIDMEM_NODE_PTR Node,
    IN gceSURF_TYPE Type
    )
{
    gceSTATUS status;
    gcsHAL_INTERFACE iface;

    gcmkHEADER_ARG("Event=0x%x FromWhere=%d Node=0x%x Type=%d",
                   Event, FromWhere, Node, Type);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
    gcmkVERIFY_ARGUMENT(Node != gcvNULL);

    /* Mark the event as an unlock. */
    iface.command                           = gcvHAL_UNLOCK_VIDEO_MEMORY;
    iface.u.UnlockVideoMemory.node          = gcmPTR_TO_UINT64(Node);
    iface.u.UnlockVideoMemory.type          = Type;
    iface.u.UnlockVideoMemory.asynchroneous = 0;

    /* Append it to the queue. */
    gcmkONERROR(gckEVENT_AddList(Event, &iface, FromWhere, gcvFALSE, gcvTRUE));

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
**  gckEVENT_FreeVideoMemory
**
**  Schedule an event to free video memory.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gcuVIDMEM_NODE_PTR VideoMemory
**          Pointer to a gcuVIDMEM_NODE object to free.
**
**      gceKERNEL_WHERE FromWhere
**          Place in the pipe where the event needs to be generated.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckEVENT_FreeVideoMemory(
    IN gckEVENT Event,
    IN gcuVIDMEM_NODE_PTR VideoMemory,
    IN gceKERNEL_WHERE FromWhere
    )
{
    gceSTATUS status;
    gcsHAL_INTERFACE iface;

    gcmkHEADER_ARG("Event=0x%x VideoMemory=0x%x FromWhere=%d",
                   Event, VideoMemory, FromWhere);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
    gcmkVERIFY_ARGUMENT(VideoMemory != gcvNULL);

    /* Create an event. */
    iface.command = gcvHAL_FREE_VIDEO_MEMORY;
    iface.u.FreeVideoMemory.node = gcmPTR_TO_UINT64(VideoMemory);

    /* Append it to the queue. */
    gcmkONERROR(gckEVENT_AddList(Event, &iface, FromWhere, gcvFALSE, gcvTRUE));

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
**  gckEVENT_FreeNonPagedMemory
**
**  Schedule an event to free non-paged memory.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gctSIZE_T Bytes
**          Number of bytes of non-paged memory to free.
**
**      gctPHYS_ADDR Physical
**          Physical address of non-paged memory to free.
**
**      gctPOINTER Logical
**          Logical address of non-paged memory to free.
**
**      gceKERNEL_WHERE FromWhere
**          Place in the pipe where the event needs to be generated.
*/
gceSTATUS
gckEVENT_FreeNonPagedMemory(
    IN gckEVENT Event,
    IN gctSIZE_T Bytes,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical,
    IN gceKERNEL_WHERE FromWhere
    )
{
    gceSTATUS status;
    gcsHAL_INTERFACE iface;
    gckKERNEL kernel = Event->kernel;

    gcmkHEADER_ARG("Event=0x%x Bytes=%lu Physical=0x%x Logical=0x%x "
                   "FromWhere=%d",
                   Event, Bytes, Physical, Logical, FromWhere);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    /* Create an event. */
    iface.command = gcvHAL_FREE_NON_PAGED_MEMORY;
    iface.u.FreeNonPagedMemory.bytes    = Bytes;
    iface.u.FreeNonPagedMemory.physical = gcmPTR_TO_NAME(Physical);
    iface.u.FreeNonPagedMemory.logical  = gcmPTR_TO_UINT64(Logical);

    /* Append it to the queue. */
    gcmkONERROR(gckEVENT_AddList(Event, &iface, FromWhere, gcvFALSE, gcvTRUE));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckEVENT_DestroyVirtualCommandBuffer(
    IN gckEVENT Event,
    IN gctSIZE_T Bytes,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical,
    IN gceKERNEL_WHERE FromWhere
    )
{
    gceSTATUS status;
    gcsHAL_INTERFACE iface;
    gckKERNEL kernel = Event->kernel;

    gcmkHEADER_ARG("Event=0x%x Bytes=%lu Physical=0x%x Logical=0x%x "
                   "FromWhere=%d",
                   Event, Bytes, Physical, Logical, FromWhere);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    /* Create an event. */
    iface.command = gcvHAL_FREE_VIRTUAL_COMMAND_BUFFER;
    iface.u.FreeVirtualCommandBuffer.bytes    = Bytes;
    iface.u.FreeVirtualCommandBuffer.physical = gcmPTR_TO_NAME(Physical);
    iface.u.FreeVirtualCommandBuffer.logical  = gcmPTR_TO_UINT64(Logical);

    /* Append it to the queue. */
    gcmkONERROR(gckEVENT_AddList(Event, &iface, FromWhere, gcvFALSE, gcvTRUE));

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
**  gckEVENT_FreeContigiuousMemory
**
**  Schedule an event to free contiguous memory.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gctSIZE_T Bytes
**          Number of bytes of contiguous memory to free.
**
**      gctPHYS_ADDR Physical
**          Physical address of contiguous memory to free.
**
**      gctPOINTER Logical
**          Logical address of contiguous memory to free.
**
**      gceKERNEL_WHERE FromWhere
**          Place in the pipe where the event needs to be generated.
*/
gceSTATUS
gckEVENT_FreeContiguousMemory(
    IN gckEVENT Event,
    IN gctSIZE_T Bytes,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical,
    IN gceKERNEL_WHERE FromWhere
    )
{
    gceSTATUS status;
    gcsHAL_INTERFACE iface;
    gckKERNEL kernel = Event->kernel;

    gcmkHEADER_ARG("Event=0x%x Bytes=%lu Physical=0x%x Logical=0x%x "
                   "FromWhere=%d",
                   Event, Bytes, Physical, Logical, FromWhere);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    /* Create an event. */
    iface.command = gcvHAL_FREE_CONTIGUOUS_MEMORY;
    iface.u.FreeContiguousMemory.bytes    = Bytes;
    iface.u.FreeContiguousMemory.physical = gcmPTR_TO_NAME(Physical);
    iface.u.FreeContiguousMemory.logical  = gcmPTR_TO_UINT64(Logical);

    /* Append it to the queue. */
    gcmkONERROR(gckEVENT_AddList(Event, &iface, FromWhere, gcvFALSE, gcvTRUE));

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
**  gckEVENT_Signal
**
**  Schedule an event to trigger a signal.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gctSIGNAL Signal
**          Pointer to the signal to trigger.
**
**      gceKERNEL_WHERE FromWhere
**          Place in the pipe where the event needs to be generated.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckEVENT_Signal(
    IN gckEVENT Event,
    IN gctSIGNAL Signal,
    IN gceKERNEL_WHERE FromWhere
    )
{
    gceSTATUS status;
    gcsHAL_INTERFACE iface;

    gcmkHEADER_ARG("Event=0x%x Signal=0x%x FromWhere=%d",
                   Event, Signal, FromWhere);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    /* Mark the event as a signal. */
    iface.command            = gcvHAL_SIGNAL;
    iface.u.Signal.signal    = gcmPTR_TO_UINT64(Signal);
#ifdef __QNXNTO__
    iface.u.Signal.coid      = 0;
    iface.u.Signal.rcvid     = 0;
#endif
    iface.u.Signal.auxSignal = 0;
    iface.u.Signal.process   = 0;

    /* Append it to the queue. */
    gcmkONERROR(gckEVENT_AddList(Event, &iface, FromWhere, gcvFALSE, gcvTRUE));

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
**  gckEVENT_CommitDone
**
**  Schedule an event to wake up work thread when commit is done by GPU.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gceKERNEL_WHERE FromWhere
**          Place in the pipe where the event needs to be generated.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckEVENT_CommitDone(
    IN gckEVENT Event,
    IN gceKERNEL_WHERE FromWhere
    )
{
    gceSTATUS status;
    gcsHAL_INTERFACE iface;

    gcmkHEADER_ARG("Event=0x%x FromWhere=%d", Event, FromWhere);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);

    iface.command = gcvHAL_COMMIT_DONE;

    /* Append it to the queue. */
    gcmkONERROR(gckEVENT_AddList(Event, &iface, FromWhere, gcvFALSE, gcvTRUE));

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
**  gckEVENT_Submit
**
**  Submit the current event queue to the GPU.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gctBOOL Wait
**          Submit requires one vacant event; if Wait is set to not zero,
**          and there are no vacant events at this time, the function will
**          wait until an event becomes vacant so that submission of the
**          queue is successful.
**
**      gctBOOL FromPower
**          Determines whether the call originates from inside the power
**          management or not.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckEVENT_Submit(
    IN gckEVENT Event,
    IN gctBOOL Wait,
    IN gctBOOL FromPower
    )
{
    gceSTATUS status;
    gctUINT8 id = 0xFF;
    gcsEVENT_QUEUE_PTR queue;
    gctBOOL acquired = gcvFALSE;
    gckCOMMAND command = gcvNULL;
    gctBOOL commitEntered = gcvFALSE;
#if !gcdNULL_DRIVER
    gctSIZE_T bytes;
    gctPOINTER buffer;
#endif

    gcmkHEADER_ARG("Event=0x%x Wait=%d", Event, Wait);

    /* Get gckCOMMAND object. */
    command = Event->kernel->command;

    /* Are there event queues? */
    if (Event->queueHead != gcvNULL)
    {
        /* Acquire the command queue. */
        gcmkONERROR(gckCOMMAND_EnterCommit(command, FromPower));
        commitEntered = gcvTRUE;

        /* Process all queues. */
        while (Event->queueHead != gcvNULL)
        {
            /* Acquire the list mutex. */
            gcmkONERROR(gckOS_AcquireMutex(Event->os,
                                           Event->eventListMutex,
                                           gcvINFINITE));
            acquired = gcvTRUE;

            /* Get the current queue. */
            queue = Event->queueHead;

            /* Allocate an event ID. */
            gcmkONERROR(gckEVENT_GetEvent(Event, Wait, &id, queue->source));

            /* Copy event list to event ID queue. */
            Event->queues[id].head   = queue->head;

            /* Remove the top queue from the list. */
            if (Event->queueHead == Event->queueTail)
            {
                Event->queueHead = gcvNULL;
                Event->queueTail = gcvNULL;
            }
            else
            {
                Event->queueHead = Event->queueHead->next;
            }

            /* Free the queue. */
            gcmkONERROR(gckEVENT_FreeQueue(Event, queue));

            /* Release the list mutex. */
            gcmkONERROR(gckOS_ReleaseMutex(Event->os, Event->eventListMutex));
            acquired = gcvFALSE;

            gcmkONERROR(__RemoveRecordFromProcessDB(Event,
                Event->queues[id].head));

#if gcdNULL_DRIVER
            /* Notify immediately on infinite hardware. */
            gcmkONERROR(gckEVENT_Interrupt(Event, 1 << id));

            gcmkONERROR(gckEVENT_Notify(Event, 0));
#else
            /* Get the size of the hardware event. */
            gcmkONERROR(gckHARDWARE_Event(Event->kernel->hardware,
                                          gcvNULL,
                                          id,
                                          Event->queues[id].source,
                                          &bytes));

            /* Reserve space in the command queue. */
            gcmkONERROR(gckCOMMAND_Reserve(command,
                                           bytes,
                                           &buffer,
                                           &bytes));

            /* Set the hardware event in the command queue. */
            gcmkONERROR(gckHARDWARE_Event(Event->kernel->hardware,
                                          buffer,
                                          id,
                                          Event->queues[id].source,
                                          &bytes));

            /* Execute the hardware event. */
            gcmkONERROR(gckCOMMAND_Execute(command, bytes));
#endif
        }

        /* Release the command queue. */
        gcmkONERROR(gckCOMMAND_ExitCommit(command, FromPower));
        commitEntered = gcvFALSE;

#if !gcdNULL_DRIVER
        gcmkVERIFY_OK(_TryToIdleGPU(Event));
#endif
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (commitEntered)
    {
        /* Release the command queue mutex. */
        gcmkVERIFY_OK(gckCOMMAND_ExitCommit(command, FromPower));
    }

    if (acquired)
    {
        /* Need to unroll the mutex acquire. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->eventListMutex));
    }

    if (id != 0xFF)
    {
        /* Need to unroll the event allocation. */
        Event->queues[id].head = gcvNULL;
    }

    if (status == gcvSTATUS_GPU_NOT_RESPONDING)
    {
        /* Broadcast GPU stuck. */
        status = gckOS_Broadcast(Event->os,
                                 Event->kernel->hardware,
                                 gcvBROADCAST_GPU_STUCK);
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckEVENT_Commit
**
**  Commit an event queue from the user.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gcsQUEUE_PTR Queue
**          User event queue.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckEVENT_Commit(
    IN gckEVENT Event,
    IN gcsQUEUE_PTR Queue
    )
{
    gceSTATUS status;
    gcsQUEUE_PTR record = gcvNULL, next;
    gctUINT32 processID;
    gctBOOL needCopy = gcvFALSE;

    gcmkHEADER_ARG("Event=0x%x Queue=0x%x", Event, Queue);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);

    /* Get the current process ID. */
    gcmkONERROR(gckOS_GetProcessID(&processID));

    /* Query if we need to copy the client data. */
    gcmkONERROR(gckOS_QueryNeedCopy(Event->os, processID, &needCopy));

    /* Loop while there are records in the queue. */
    while (Queue != gcvNULL)
    {
        gcsQUEUE queue;

        if (needCopy)
        {
            /* Point to stack record. */
            record = &queue;

            /* Copy the data from the client. */
            gcmkONERROR(gckOS_CopyFromUserData(Event->os,
                                               record,
                                               Queue,
                                               gcmSIZEOF(gcsQUEUE)));
        }
        else
        {
            gctPOINTER pointer = gcvNULL;

            /* Map record into kernel memory. */
            gcmkONERROR(gckOS_MapUserPointer(Event->os,
                                             Queue,
                                             gcmSIZEOF(gcsQUEUE),
                                             &pointer));

            record = pointer;
        }

        /* Append event record to event queue. */
        gcmkONERROR(
            gckEVENT_AddList(Event, &record->iface, gcvKERNEL_PIXEL, gcvTRUE, gcvFALSE));

        /* Next record in the queue. */
        next = gcmUINT64_TO_PTR(record->next);

        if (!needCopy)
        {
            /* Unmap record from kernel memory. */
            gcmkONERROR(
                gckOS_UnmapUserPointer(Event->os,
                                       Queue,
                                       gcmSIZEOF(gcsQUEUE),
                                       (gctPOINTER *) record));
            record = gcvNULL;
        }

        Queue = next;
    }

    /* Submit the event list. */
    gcmkONERROR(gckEVENT_Submit(Event, gcvTRUE, gcvFALSE));

    /* Success */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if ((record != gcvNULL) && !needCopy)
    {
        /* Roll back. */
        gcmkVERIFY_OK(gckOS_UnmapUserPointer(Event->os,
                                             Queue,
                                             gcmSIZEOF(gcsQUEUE),
                                             (gctPOINTER *) record));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckEVENT_Compose
**
**  Schedule a composition event and start a composition.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gcsHAL_COMPOSE_PTR Info
**          Pointer to the composition structure.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckEVENT_Compose(
    IN gckEVENT Event,
    IN gcsHAL_COMPOSE_PTR Info
    )
{
    gceSTATUS status;
    gcsEVENT_PTR headRecord;
    gcsEVENT_PTR tailRecord;
    gcsEVENT_PTR tempRecord;
    gctUINT8 id = 0xFF;
    gctUINT32 processID;

    gcmkHEADER_ARG("Event=0x%x Info=0x%x", Event, Info);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
    gcmkVERIFY_ARGUMENT(Info != gcvNULL);

    /* Allocate an event ID. */
    gcmkONERROR(gckEVENT_GetEvent(Event, gcvTRUE, &id, gcvKERNEL_PIXEL));

    /* Get process ID. */
    gcmkONERROR(gckOS_GetProcessID(&processID));

    /* Allocate a record. */
    gcmkONERROR(gckEVENT_AllocateRecord(Event, gcvTRUE, &tempRecord));
    headRecord = tailRecord = tempRecord;

    /* Initialize the record. */
    tempRecord->info.command            = gcvHAL_SIGNAL;
    tempRecord->info.u.Signal.process   = Info->process;
#ifdef __QNXNTO__
    tempRecord->info.u.Signal.coid      = Info->coid;
    tempRecord->info.u.Signal.rcvid     = Info->rcvid;
#endif
    tempRecord->info.u.Signal.signal    = Info->signal;
    tempRecord->info.u.Signal.auxSignal = 0;
    tempRecord->next = gcvNULL;
    tempRecord->processID = processID;

    /* Allocate another record for user signal #1. */
    if (gcmUINT64_TO_PTR(Info->userSignal1) != gcvNULL)
    {
        /* Allocate a record. */
        gcmkONERROR(gckEVENT_AllocateRecord(Event, gcvTRUE, &tempRecord));
        tailRecord->next = tempRecord;
        tailRecord = tempRecord;

        /* Initialize the record. */
        tempRecord->info.command            = gcvHAL_SIGNAL;
        tempRecord->info.u.Signal.process   = Info->userProcess;
#ifdef __QNXNTO__
        tempRecord->info.u.Signal.coid      = Info->coid;
        tempRecord->info.u.Signal.rcvid     = Info->rcvid;
#endif
        tempRecord->info.u.Signal.signal    = Info->userSignal1;
        tempRecord->info.u.Signal.auxSignal = 0;
        tempRecord->next = gcvNULL;
        tempRecord->processID = processID;
    }

    /* Allocate another record for user signal #2. */
    if (gcmUINT64_TO_PTR(Info->userSignal2) != gcvNULL)
    {
        /* Allocate a record. */
        gcmkONERROR(gckEVENT_AllocateRecord(Event, gcvTRUE, &tempRecord));
        tailRecord->next = tempRecord;
        tailRecord = tempRecord;

        /* Initialize the record. */
        tempRecord->info.command            = gcvHAL_SIGNAL;
        tempRecord->info.u.Signal.process   = Info->userProcess;
#ifdef __QNXNTO__
        tempRecord->info.u.Signal.coid      = Info->coid;
        tempRecord->info.u.Signal.rcvid     = Info->rcvid;
#endif
        tempRecord->info.u.Signal.signal    = Info->userSignal2;
        tempRecord->info.u.Signal.auxSignal = 0;
        tempRecord->next = gcvNULL;
        tempRecord->processID = processID;
    }

	/* Set the event list. */
    Event->queues[id].head = headRecord;

    /* Start composition. */
    gcmkONERROR(gckHARDWARE_Compose(
        Event->kernel->hardware, processID,
        gcmUINT64_TO_PTR(Info->physical), gcmUINT64_TO_PTR(Info->logical), Info->offset, Info->size, id
        ));

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
**  gckEVENT_Interrupt
**
**  Called by the interrupt service routine to store the triggered interrupt
**  mask to be later processed by gckEVENT_Notify.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gctUINT32 Data
**          Mask for the 32 interrupts.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckEVENT_Interrupt(
    IN gckEVENT Event,
    IN gctUINT32 Data
    )
{
    gcmkHEADER_ARG("Event=0x%x Data=0x%x", Event, Data);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);

    /* Combine current interrupt status with pending flags. */
#if gcdSMP
    gckOS_AtomSetMask(Event->pending, Data);
#elif defined(__QNXNTO__)
    atomic_set(&Event->pending, Data);
#else
    Event->pending |= Data;
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckEVENT_Notify
**
**  Process all triggered interrupts.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckEVENT_Notify(
    IN gckEVENT Event,
    IN gctUINT32 IDs
    )
{
    gceSTATUS status = gcvSTATUS_OK;
    gctINT i;
    gcsEVENT_QUEUE * queue;
    gctUINT mask = 0;
    gctBOOL acquired = gcvFALSE;
    gcuVIDMEM_NODE_PTR node;
    gctPOINTER info;
    gctSIGNAL signal;
    gctUINT pending;
    gckKERNEL kernel = Event->kernel;
#if !gcdSMP
    gctBOOL suspended = gcvFALSE;
#endif
#if gcmIS_DEBUG(gcdDEBUG_TRACE)
    gctINT eventNumber = 0;
#endif
    gctINT32 free;
#if gcdSECURE_USER
    gcskSECURE_CACHE_PTR cache;
#endif

    gcmkHEADER_ARG("Event=0x%x IDs=0x%x", Event, IDs);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);

    gcmDEBUG_ONLY(
        if (IDs != 0)
        {
            for (i = 0; i < gcmCOUNTOF(Event->queues); ++i)
            {
                if (Event->queues[i].head != gcvNULL)
                {
                    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_EVENT,
                                   "Queue(%d): stamp=%llu source=%d",
                                   i,
                                   Event->queues[i].stamp,
                                   Event->queues[i].source);
                }
            }
        }
    );

    for (;;)
    {
        gcsEVENT_PTR record;

#if gcdSMP
        /* Get current interrupts. */
        gckOS_AtomGet(Event->os, Event->pending, (gctINT32_PTR)&pending);
#else
        /* Suspend interrupts. */
        gcmkONERROR(gckOS_SuspendInterruptEx(Event->os, Event->kernel->core));
        suspended = gcvTRUE;

        /* Get current interrupts. */
        pending = Event->pending;

        /* Resume interrupts. */
        gcmkONERROR(gckOS_ResumeInterruptEx(Event->os, Event->kernel->core));
        suspended = gcvFALSE;
#endif

        if (pending == 0)
        {
            /* No more pending interrupts - done. */
            break;
        }

        if (pending & 0x80000000)
        {
            gckOS_Print("!!!!!!!!!!!!! AXI BUS ERROR !!!!!!!!!!!!!\n");
            gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_EVENT, "AXI BUS ERROR");
            pending &= 0x7FFFFFFF;
        }

        if (pending & 0x40000000)
        {
            gckHARDWARE_DumpMMUException(Event->kernel->hardware);

            pending &= 0xBFFFFFFF;
        }

        gcmkTRACE_ZONE_N(
            gcvLEVEL_INFO, gcvZONE_EVENT,
            gcmSIZEOF(pending),
            "Pending interrupts 0x%x",
            pending
            );

        queue = gcvNULL;

        gcmDEBUG_ONLY(
            if (IDs == 0)
            {
                for (i = 0; i < gcmCOUNTOF(Event->queues); ++i)
                {
                    if (Event->queues[i].head != gcvNULL)
                    {
                        gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_EVENT,
                                       "Queue(%d): stamp=%llu source=%d",
                                       i,
                                       Event->queues[i].stamp,
                                       Event->queues[i].source);
                    }
                }
            }
        );

        /* Find the oldest pending interrupt. */
        for (i = 0; i < gcmCOUNTOF(Event->queues); ++i)
        {
            if ((Event->queues[i].head != gcvNULL)
            &&  (pending & (1 << i))
            )
            {
                if ((queue == gcvNULL)
                ||  (Event->queues[i].stamp < queue->stamp)
                )
                {
                    queue = &Event->queues[i];
                    mask  = 1 << i;
#if gcmIS_DEBUG(gcdDEBUG_TRACE)
                    eventNumber = i;
#endif
                }
            }
        }

        if (queue == gcvNULL)
        {
            gcmkTRACE_ZONE_N(
                gcvLEVEL_ERROR, gcvZONE_EVENT,
                gcmSIZEOF(pending),
                "Interrupts 0x%x are not pending.",
                pending
                );

#if gcdSMP
            /* Mark pending interrupts as handled. */
            gckOS_AtomClearMask(Event->pending, pending);
#elif defined(__QNXNTO__)
            /* Mark pending interrupts as handled. */
            atomic_clr((gctUINT32_PTR)&Event->pending, pending);
#else
            /* Suspend interrupts. */
            gcmkONERROR(gckOS_SuspendInterruptEx(Event->os, Event->kernel->core));
            suspended = gcvTRUE;

            /* Mark pending interrupts as handled. */
            Event->pending &= ~pending;

            /* Resume interrupts. */
            gcmkONERROR(gckOS_ResumeInterruptEx(Event->os, Event->kernel->core));
            suspended = gcvFALSE;
#endif
            break;
        }

        /* Check whether there is a missed interrupt. */
        for (i = 0; i < gcmCOUNTOF(Event->queues); ++i)
        {
            if ((Event->queues[i].head != gcvNULL)
            &&  (Event->queues[i].stamp < queue->stamp)
            &&  (Event->queues[i].source <= queue->source)
            )
            {
                gcmkTRACE_N(
                    gcvLEVEL_ERROR,
                    gcmSIZEOF(i) + gcmSIZEOF(Event->queues[i].stamp),
                    "Event %d lost (stamp %llu)",
                    i, Event->queues[i].stamp
                    );

                /* Use this event instead. */
                queue = &Event->queues[i];
                mask  = 0;
            }
        }

        if (mask != 0)
        {
#if gcmIS_DEBUG(gcdDEBUG_TRACE)
            gcmkTRACE_ZONE_N(
                gcvLEVEL_INFO, gcvZONE_EVENT,
                gcmSIZEOF(eventNumber),
                "Processing interrupt %d",
                eventNumber
                );
#endif
        }

#if gcdSMP
        /* Mark pending interrupt as handled. */
        gckOS_AtomClearMask(Event->pending, mask);
#elif defined(__QNXNTO__)
        /* Mark pending interrupt as handled. */
        atomic_clr(&Event->pending, mask);
#else
        /* Suspend interrupts. */
        gcmkONERROR(gckOS_SuspendInterruptEx(Event->os, Event->kernel->core));
        suspended = gcvTRUE;

        /* Mark pending interrupt as handled. */
        Event->pending &= ~mask;

        /* Resume interrupts. */
        gcmkONERROR(gckOS_ResumeInterruptEx(Event->os, Event->kernel->core));
        suspended = gcvFALSE;
#endif

        /* Grab the mutex queue. */
        gcmkONERROR(gckOS_AcquireMutex(Event->os,
                                       Event->eventQueueMutex,
                                       gcvINFINITE));
        acquired = gcvTRUE;

        /* Grab the event head. */
        record = queue->head;

        /* Now quickly clear its event list. */
        queue->head = gcvNULL;

        /* Release the mutex queue. */
        gcmkONERROR(gckOS_ReleaseMutex(Event->os, Event->eventQueueMutex));
        acquired = gcvFALSE;

        /* Increase the number of free events. */
        gcmkONERROR(gckOS_AtomIncrement(Event->os, Event->freeAtom, &free));

        /* Walk all events for this interrupt. */
        while (record != gcvNULL)
        {
            gcsEVENT_PTR recordNext;
#ifndef __QNXNTO__
            gctPOINTER logical;
#endif
#if gcdSECURE_USER
            gctSIZE_T bytes;
#endif

            /* Grab next record. */
            recordNext = record->next;

#ifdef __QNXNTO__
            /* Assign record->processID as the pid for this galcore thread.
             * Used in OS calls like gckOS_UnlockMemory() which do not take a pid.
             */
            drv_thread_specific_key_assign(record->processID, 0, Event->kernel->core);
#endif

#if gcdSECURE_USER
            /* Get the cache that belongs to this process. */
            gcmkONERROR(gckKERNEL_GetProcessDBCache(Event->kernel,
                        record->processID,
                        &cache));
#endif

            gcmkTRACE_ZONE_N(
                gcvLEVEL_INFO, gcvZONE_EVENT,
                gcmSIZEOF(record->info.command),
                "Processing event type: %d",
                record->info.command
                );

            switch (record->info.command)
            {
            case gcvHAL_FREE_NON_PAGED_MEMORY:
                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_EVENT,
                               "gcvHAL_FREE_NON_PAGED_MEMORY: 0x%x",
                               gcmNAME_TO_PTR(record->info.u.FreeNonPagedMemory.physical));

                /* Free non-paged memory. */
                status = gckOS_FreeNonPagedMemory(
                            Event->os,
                            (gctSIZE_T) record->info.u.FreeNonPagedMemory.bytes,
                            gcmNAME_TO_PTR(record->info.u.FreeNonPagedMemory.physical),
                            gcmUINT64_TO_PTR(record->info.u.FreeNonPagedMemory.logical));

                if (gcmIS_SUCCESS(status))
                {
#if gcdSECURE_USER
                    gcmkVERIFY_OK(gckKERNEL_FlushTranslationCache(
                        Event->kernel,
                        cache,
                        gcmUINT64_TO_PTR(record->record.u.FreeNonPagedMemory.logical),
                        (gctSIZE_T) record->record.u.FreeNonPagedMemory.bytes));
#endif
                }
                gcmRELEASE_NAME(record->info.u.FreeNonPagedMemory.physical);
                break;

            case gcvHAL_FREE_CONTIGUOUS_MEMORY:
                gcmkTRACE_ZONE(
                    gcvLEVEL_VERBOSE, gcvZONE_EVENT,
                    "gcvHAL_FREE_CONTIGUOUS_MEMORY: 0x%x",
                    gcmNAME_TO_PTR(record->info.u.FreeContiguousMemory.physical));

                /* Unmap the user memory. */
                status = gckOS_FreeContiguous(
                            Event->os,
                            gcmNAME_TO_PTR(record->info.u.FreeContiguousMemory.physical),
                            gcmUINT64_TO_PTR(record->info.u.FreeContiguousMemory.logical),
                            (gctSIZE_T) record->info.u.FreeContiguousMemory.bytes);

                if (gcmIS_SUCCESS(status))
                {
#if gcdSECURE_USER
                    gcmkVERIFY_OK(gckKERNEL_FlushTranslationCache(
                        Event->kernel,
                        cache,
                        gcmUINT64_TO_PTR(record->record.u.FreeContiguousMemory.logical),
                        (gctSIZE_T) record->record.u.FreeContiguousMemory.bytes));
#endif
                }
                gcmRELEASE_NAME(record->info.u.FreeContiguousMemory.physical);
                break;

            case gcvHAL_FREE_VIDEO_MEMORY:
                node = gcmUINT64_TO_PTR(record->info.u.FreeVideoMemory.node);
                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_EVENT,
                               "gcvHAL_FREE_VIDEO_MEMORY: 0x%x",
                               node);
#ifdef __QNXNTO__
#if gcdUSE_VIDMEM_PER_PID
                /* Check if the VidMem object still exists. */
                if (gckKERNEL_GetVideoMemoryPoolPid(record->kernel,
                                                    gcvPOOL_SYSTEM,
                                                    record->processID,
                                                    gcvNULL) == gcvSTATUS_NOT_FOUND)
                {
                    /*printf("Vidmem not found for process:%d\n", queue->processID);*/
                    status = gcvSTATUS_OK;
                    break;
                }
#else
                if ((node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
                &&  (node->VidMem.logical != gcvNULL)
                )
                {
                    gcmkERR_BREAK(
                        gckKERNEL_UnmapVideoMemory(record->kernel,
                                                   node->VidMem.logical,
                                                   record->processID,
                                                   node->VidMem.bytes));
                    node->VidMem.logical = gcvNULL;
                }
#endif
#endif

                /* Free video memory. */
                status =
                    gckVIDMEM_Free(node);

                break;

            case gcvHAL_WRITE_DATA:
#ifndef __QNXNTO__
                /* Convert physical into logical address. */
                gcmkERR_BREAK(
                    gckOS_MapPhysical(Event->os,
                                      record->info.u.WriteData.address,
                                      gcmSIZEOF(gctUINT32),
                                      &logical));

                /* Write data. */
                gcmkERR_BREAK(
                    gckOS_WriteMemory(Event->os,
                                      logical,
                                      record->info.u.WriteData.data));

                /* Unmap the physical memory. */
                gcmkERR_BREAK(
                    gckOS_UnmapPhysical(Event->os,
                                        logical,
                                        gcmSIZEOF(gctUINT32)));
#else
                /* Write data. */
                gcmkERR_BREAK(
                    gckOS_WriteMemory(Event->os,
                                      (gctPOINTER)
                                          record->info.u.WriteData.address,
                                      record->info.u.WriteData.data));
#endif
                break;

            case gcvHAL_UNLOCK_VIDEO_MEMORY:
                node = gcmUINT64_TO_PTR(record->info.u.UnlockVideoMemory.node);

                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_EVENT,
                               "gcvHAL_UNLOCK_VIDEO_MEMORY: 0x%x",
                               node);

                /* Save node information before it disappears. */
#if gcdSECURE_USER
                if (node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
                {
                    logical = gcvNULL;
                    bytes   = 0;
                }
                else
                {
                    logical = node->Virtual.logical;
                    bytes   = node->Virtual.bytes;
                }
#endif

                /* Unlock. */
                status = gckVIDMEM_Unlock(
                    Event->kernel,
                    node,
                    record->info.u.UnlockVideoMemory.type,
                    gcvNULL);

#if gcdSECURE_USER
                if (gcmIS_SUCCESS(status) && (logical != gcvNULL))
                {
                    gcmkVERIFY_OK(gckKERNEL_FlushTranslationCache(
                        Event->kernel,
                        cache,
                        logical,
                        bytes));
                }
#endif
                break;

            case gcvHAL_SIGNAL:
                signal = gcmUINT64_TO_PTR(record->info.u.Signal.signal);
                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_EVENT,
                               "gcvHAL_SIGNAL: 0x%x",
                               signal);

#ifdef __QNXNTO__
                if ((record->info.u.Signal.coid == 0)
                &&  (record->info.u.Signal.rcvid == 0)
                )
                {
                    /* Kernel signal. */
                    gcmkERR_BREAK(
                        gckOS_Signal(Event->os,
                                     signal,
                                     gcvTRUE));
                }
                else
                {
                    /* User signal. */
                    gcmkERR_BREAK(
                        gckOS_UserSignal(Event->os,
                                         signal,
                                         record->info.u.Signal.rcvid,
                                         record->info.u.Signal.coid));
                }
#else
                /* Set signal. */
                if (gcmUINT64_TO_PTR(record->info.u.Signal.process) == gcvNULL)
                {
                    /* Kernel signal. */
                    gcmkERR_BREAK(
                        gckOS_Signal(Event->os,
                                     signal,
                                     gcvTRUE));
                }
                else
                {
                    /* User signal. */
                    gcmkERR_BREAK(
                        gckOS_UserSignal(Event->os,
                                         signal,
                                         gcmUINT64_TO_PTR(record->info.u.Signal.process)));
                }

                gcmkASSERT(record->info.u.Signal.auxSignal == 0);
#endif
                break;

            case gcvHAL_UNMAP_USER_MEMORY:
                info = gcmNAME_TO_PTR(record->info.u.UnmapUserMemory.info);
                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_EVENT,
                               "gcvHAL_UNMAP_USER_MEMORY: 0x%x",
                               info);

                /* Unmap the user memory. */
                status = gckOS_UnmapUserMemory(
                    Event->os,
                    Event->kernel->core,
                    gcmUINT64_TO_PTR(record->info.u.UnmapUserMemory.memory),
                    (gctSIZE_T) record->info.u.UnmapUserMemory.size,
                    info,
                    record->info.u.UnmapUserMemory.address);

#if gcdSECURE_USER
                if (gcmIS_SUCCESS(status))
                {
                    gcmkVERIFY_OK(gckKERNEL_FlushTranslationCache(
                        Event->kernel,
                        cache,
                        gcmUINT64_TO_PTR(record->info.u.UnmapUserMemory.memory),
                        (gctSIZE_T) record->info.u.UnmapUserMemory.size));
                }
#endif
                gcmRELEASE_NAME(record->info.u.UnmapUserMemory.info);
                break;

            case gcvHAL_TIMESTAMP:
                gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_EVENT,
                               "gcvHAL_TIMESTAMP: %d %d",
                               record->info.u.TimeStamp.timer,
                               record->info.u.TimeStamp.request);

                /* Process the timestamp. */
                switch (record->info.u.TimeStamp.request)
                {
                case 0:
                    status = gckOS_GetTime(&Event->kernel->timers[
                                           record->info.u.TimeStamp.timer].
                                           stopTime);
                    break;

                case 1:
                    status = gckOS_GetTime(&Event->kernel->timers[
                                           record->info.u.TimeStamp.timer].
                                           startTime);
                    break;

                default:
                    gcmkTRACE_ZONE_N(
                        gcvLEVEL_ERROR, gcvZONE_EVENT,
                        gcmSIZEOF(record->info.u.TimeStamp.request),
                        "Invalid timestamp request: %d",
                        record->info.u.TimeStamp.request
                        );

                    status = gcvSTATUS_INVALID_ARGUMENT;
                    break;
                }
                break;

#if gcdVIRTUAL_COMMAND_BUFFER
             case gcvHAL_FREE_VIRTUAL_COMMAND_BUFFER:
                 gcmkVERIFY_OK(
                     gckKERNEL_DestroyVirtualCommandBuffer(Event->kernel,
                         (gctSIZE_T) record->info.u.FreeVirtualCommandBuffer.bytes,
                         gcmNAME_TO_PTR(record->info.u.FreeVirtualCommandBuffer.physical),
                         gcmUINT64_TO_PTR(record->info.u.FreeVirtualCommandBuffer.logical)
                         ));
                 gcmRELEASE_NAME(record->info.u.FreeVirtualCommandBuffer.physical);
                 break;
#endif

            case gcvHAL_COMMIT_DONE:
                break;

            default:
                /* Invalid argument. */
                gcmkTRACE_ZONE_N(
                    gcvLEVEL_ERROR, gcvZONE_EVENT,
                    gcmSIZEOF(record->info.command),
                    "Unknown event type: %d",
                    record->info.command
                    );

                status = gcvSTATUS_INVALID_ARGUMENT;
                break;
            }

            /* Make sure there are no errors generated. */
            if (gcmIS_ERROR(status))
            {
                gcmkTRACE_ZONE_N(
                    gcvLEVEL_WARNING, gcvZONE_EVENT,
                    gcmSIZEOF(status),
                    "Event produced status: %d(%s)",
                    status, gckOS_DebugStatus2Name(status));
            }

            /* Free the event. */
            gcmkVERIFY_OK(gckEVENT_FreeRecord(Event, record));

            /* Advance to next record. */
            record = recordNext;
        }

        gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_EVENT,
                       "Handled interrupt 0x%x", mask);
    }

    if (IDs == 0)
    {
        gcmkONERROR(_TryToIdleGPU(Event));
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->eventQueueMutex));
    }

#if !gcdSMP
    if (suspended)
    {
        /* Resume interrupts. */
        gcmkVERIFY_OK(gckOS_ResumeInterruptEx(Event->os, Event->kernel->core));
    }
#endif

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**  gckEVENT_FreeProcess
**
**  Free all events owned by a particular process ID.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gctUINT32 ProcessID
**          Process ID of the process to be freed up.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckEVENT_FreeProcess(
    IN gckEVENT Event,
    IN gctUINT32 ProcessID
    )
{
    gctSIZE_T i;
    gctBOOL acquired = gcvFALSE;
    gcsEVENT_PTR record, next;
    gceSTATUS status;
    gcsEVENT_PTR deleteHead, deleteTail;

    gcmkHEADER_ARG("Event=0x%x ProcessID=%d", Event, ProcessID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);

    /* Walk through all queues. */
    for (i = 0; i < gcmCOUNTOF(Event->queues); ++i)
    {
        if (Event->queues[i].head != gcvNULL)
        {
            /* Grab the event queue mutex. */
            gcmkONERROR(gckOS_AcquireMutex(Event->os,
                                           Event->eventQueueMutex,
                                           gcvINFINITE));
            acquired = gcvTRUE;

            /* Grab the mutex head. */
            record                = Event->queues[i].head;
            Event->queues[i].head = gcvNULL;
            Event->queues[i].tail = gcvNULL;
            deleteHead            = gcvNULL;
            deleteTail            = gcvNULL;

            while (record != gcvNULL)
            {
                next = record->next;
                if (record->processID == ProcessID)
                {
                    if (deleteHead == gcvNULL)
                    {
                        deleteHead = record;
                    }
                    else
                    {
                        deleteTail->next = record;
                    }

                    deleteTail = record;
                }
                else
                {
                    if (Event->queues[i].head == gcvNULL)
                    {
                        Event->queues[i].head = record;
                    }
                    else
                    {
                        Event->queues[i].tail->next = record;
                    }

                    Event->queues[i].tail = record;
                }

                record->next = gcvNULL;
                record = next;
            }

            /* Release the mutex queue. */
            gcmkONERROR(gckOS_ReleaseMutex(Event->os, Event->eventQueueMutex));
            acquired = gcvFALSE;

            /* Loop through the entire list of events. */
            for (record = deleteHead; record != gcvNULL; record = next)
            {
                /* Get the next event record. */
                next = record->next;

                /* Free the event record. */
                gcmkONERROR(gckEVENT_FreeRecord(Event, record));
            }
        }
    }

    gcmkONERROR(_TryToIdleGPU(Event));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Release the event queue mutex. */
    if (acquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->eventQueueMutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**  gckEVENT_Stop
**
**  Stop the hardware using the End event mechanism.
**
**  INPUT:
**
**      gckEVENT Event
**          Pointer to an gckEVENT object.
**
**      gctUINT32 ProcessID
**          Process ID Logical belongs.
**
**      gctPHYS_ADDR Handle
**          Physical address handle.  If gcvNULL it is video memory.
**
**      gctPOINTER Logical
**          Logical address to flush.
**
**      gctSIGNAL Signal
**          Pointer to the signal to trigger.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckEVENT_Stop(
    IN gckEVENT Event,
    IN gctUINT32 ProcessID,
    IN gctPHYS_ADDR Handle,
    IN gctPOINTER Logical,
    IN gctSIGNAL Signal,
	IN OUT gctSIZE_T * waitSize
    )
{
    gceSTATUS status;
   /* gctSIZE_T waitSize;*/
    gcsEVENT_PTR record;
    gctUINT8 id = 0xFF;

    gcmkHEADER_ARG("Event=0x%x ProcessID=%u Handle=0x%x Logical=0x%x "
                   "Signal=0x%x",
                   Event, ProcessID, Handle, Logical, Signal);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);

    /* Submit the current event queue. */
    gcmkONERROR(gckEVENT_Submit(Event, gcvTRUE, gcvFALSE));

    gcmkONERROR(gckEVENT_GetEvent(Event, gcvTRUE, &id, gcvKERNEL_PIXEL));

    /* Allocate a record. */
    gcmkONERROR(gckEVENT_AllocateRecord(Event, gcvTRUE, &record));

    /* Initialize the record. */
    record->next = gcvNULL;
    record->processID               = ProcessID;
    record->info.command            = gcvHAL_SIGNAL;
    record->info.u.Signal.signal    = gcmPTR_TO_UINT64(Signal);
#ifdef __QNXNTO__
    record->info.u.Signal.coid      = 0;
    record->info.u.Signal.rcvid     = 0;
#endif
    record->info.u.Signal.auxSignal = 0;
    record->info.u.Signal.process   = 0;

    /* Append the record. */
    Event->queues[id].head      = record;

    /* Replace last WAIT with END. */
    gcmkONERROR(gckHARDWARE_End(
        Event->kernel->hardware, Logical, waitSize
        ));

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the cache for the END. */
    gcmkONERROR(gckOS_CacheClean(
        Event->os,
        ProcessID,
        gcvNULL,
        Handle,
        Logical,
        *waitSize
        ));
#endif

    /* Wait for the signal. */
    gcmkONERROR(gckOS_WaitSignal(Event->os, Signal, gcvINFINITE));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

static void
_PrintRecord(
    gcsEVENT_PTR record
    )
{
    switch (record->info.command)
    {
    case gcvHAL_FREE_NON_PAGED_MEMORY:
        gcmkPRINT("      gcvHAL_FREE_NON_PAGED_MEMORY");
            break;

    case gcvHAL_FREE_CONTIGUOUS_MEMORY:
        gcmkPRINT("      gcvHAL_FREE_CONTIGUOUS_MEMORY");
            break;

    case gcvHAL_FREE_VIDEO_MEMORY:
        gcmkPRINT("      gcvHAL_FREE_VIDEO_MEMORY");
            break;

    case gcvHAL_WRITE_DATA:
        gcmkPRINT("      gcvHAL_WRITE_DATA");
       break;

    case gcvHAL_UNLOCK_VIDEO_MEMORY:
        gcmkPRINT("      gcvHAL_UNLOCK_VIDEO_MEMORY");
        break;

    case gcvHAL_SIGNAL:
        gcmkPRINT("      gcvHAL_SIGNAL process=%d signal=0x%x",
                  record->info.u.Signal.process,
                  record->info.u.Signal.signal);
        break;

    case gcvHAL_UNMAP_USER_MEMORY:
        gcmkPRINT("      gcvHAL_UNMAP_USER_MEMORY");
       break;

    case gcvHAL_TIMESTAMP:
        gcmkPRINT("      gcvHAL_TIMESTAMP");
        break;

    case gcvHAL_COMMIT_DONE:
        gcmkPRINT("      gcvHAL_COMMIT_DONE");
        break;

    case gcvHAL_FREE_VIRTUAL_COMMAND_BUFFER:
        gcmkPRINT("      gcvHAL_FREE_VIRTUAL_COMMAND_BUFFER logical=0x%08x",
                  record->info.u.FreeVirtualCommandBuffer.logical);
        break;

    default:
        gcmkPRINT("      Illegal Event %d", record->info.command);
        break;
    }
}

/*******************************************************************************
** gckEVENT_Dump
**
** Dump record in event queue when stuck happens.
** No protection for the event queue.
**/
gceSTATUS
gckEVENT_Dump(
    IN gckEVENT Event
    )
{
    gcsEVENT_QUEUE_PTR queueHead = Event->queueHead;
    gcsEVENT_QUEUE_PTR queue;
    gcsEVENT_PTR record = gcvNULL;
    gctINT i;

    gcmkHEADER_ARG("Event=0x%x", Event);

    gcmkPRINT("**************************\n");
    gcmkPRINT("***  EVENT STATE DUMP  ***\n");
    gcmkPRINT("**************************\n");


    gcmkPRINT("  Unsumbitted Event:");
    while(queueHead)
    {
        queue = queueHead;
        record = queueHead->head;

        gcmkPRINT("    [%x]:", queue);
        while(record)
        {
            _PrintRecord(record);
            record = record->next;
        }

        if (queueHead == Event->queueTail)
        {
            queueHead = gcvNULL;
        }
        else
        {
            queueHead = queueHead->next;
        }
    }

    gcmkPRINT("  Untriggered Event:");
    for (i = 0; i < 30; i++)
    {
        queue = &Event->queues[i];
        record = queue->head;

        gcmkPRINT("    [%d]:", i);
        while(record)
        {
            _PrintRecord(record);
            record = record->next;
        }
    }

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

