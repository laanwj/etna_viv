/****************************************************************************
*
*    Copyright (C) 2005 - 2010 by Vivante Corp.
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
#include "gc_hal_user_context.h"

#define _GC_OBJ_ZONE	gcvZONE_EVENT

#define gcdEVENT_ALLOCATION_COUNT		(4096 / gcmSIZEOF(gcsHAL_INTERFACE))
#define gcdEVENT_MIN_THRESHOLD			4

/******************************************************************************\
********************************* Support Code *********************************
\******************************************************************************/

/*******************************************************************************
**
**	_GetEvent
**
**	Get an empty event ID.
**
**	INPUT:
**
**		gckEVENT Event
**			Pointer to an gckEVENT object.
**
**	OUTPUT:
**
**		gctUINT8 * EventID
**			Pointer to a variable that receives an empty event ID.
*/
static gceSTATUS
_GetEvent(
    IN gckEVENT Event,
    OUT gctUINT8 * EventID,
    IN gceKERNEL_WHERE Source
    )
{
    gctINT i, id;
	gceSTATUS status;
	gctBOOL acquired = gcvFALSE;

	gcmkHEADER_ARG("Event=0x%x Source=%d", Event, Source);

	/* Grab the queue mutex. */
	gcmkONERROR(gckOS_AcquireMutex(Event->os,
	        	   				   Event->mutexQueue,
    				    		   gcvINFINITE));
	acquired = gcvTRUE;

	/* Walk through all events. */
    id = Event->lastID;
    for (i = 0; i < gcmCOUNTOF(Event->queues); ++i)
    {
        if (Event->queues[id].head == gcvNULL)
        {
            *EventID = (gctUINT8) id;

            Event->lastID = (id + 1) % gcmCOUNTOF(Event->queues);

            /* Save time stamp of event. */
            Event->queues[id].stamp  = ++(Event->stamp);
            Event->queues[id].source = Source;

            /* Release the queue mutex. */
            gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->mutexQueue));

            /* Success. */
            gcmkFOOTER_ARG("*EventID=%u", *EventID);
            return gcvSTATUS_OK;
        }

        id = (id + 1) % gcmCOUNTOF(Event->queues);
    }

    /* Release the queue mutex. */
    gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->mutexQueue));
    acquired = gcvFALSE;

    /* Out of resources. */
    gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);

OnError:
	if (acquired)
	{
		/* Release the queue mutex. */
		gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->mutexQueue));
	}

	/* Return the status. */
	gcmkFOOTER();
	return status;
}
#if VIVANTE_POWER_MANAGE
static gceSTATUS
_IsEmpty(
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
    status = gckOS_AcquireMutex(Event->os, Event->mutexQueue, 0);
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
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->mutexQueue));
    }

    /* Success. */
    gcmkFOOTER_ARG("*IsEmpty=%d", gcmOPT_VALUE(IsEmpty));
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif
/******************************************************************************\
******************************* gckEVENT API Code *******************************
\******************************************************************************/

/*******************************************************************************
**
**	gckEVENT_Construct
**
**	Construct a new gckEVENT object.
**
**	INPUT:
**
**		gckKERNEL Kernel
**			Pointer to an gckKERNEL object.
**
**	OUTPUT:
**
**		gckEVENT * Event
**			Pointer to a variable that receives the gckEVENT object pointer.
*/
gceSTATUS
gckEVENT_Construct(
	IN gckKERNEL Kernel,
	OUT gckEVENT * Event
	)
{
	gckOS os;
	gceSTATUS status;
	gckEVENT event = gcvNULL;
	int i;
	gcsEVENT_PTR record;

	gcmkHEADER_ARG("Kernel=0x%x", Kernel);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
	gcmkVERIFY_ARGUMENT(Event != gcvNULL);

	/* Extract the pointer to the gckOS object. */
	os = Kernel->os;
	gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

	/* Allocate the gckEVENT object. */
	gcmkONERROR(
		gckOS_Allocate(os,
					   gcmSIZEOF(struct _gckEVENT),
					   (gctPOINTER *) &event));

	/* Initialize the gckEVENT object. */
	event->object.type = gcvOBJ_EVENT;
	event->kernel      = Kernel;
	event->os          = os;
	event->mutexQueue  = gcvNULL;
	event->freeList    = gcvNULL;
	event->freeCount   = 0;
	event->freeMutex   = gcvNULL;
	event->list.head   = gcvNULL;
	event->list.tail   = gcvNULL;
	event->listMutex   = gcvNULL;
	event->lastID      = 0;

    /* Construct atom holding number of event counts. */
    event->atomEventRef= gcvNULL;
    gcmkONERROR(
        gckOS_AtomConstruct(os, &event->atomEventRef));
    
	/* Create the mutexes. */
	gcmkONERROR(
		gckOS_CreateMutex(os, &event->mutexQueue));

	gcmkONERROR(
		gckOS_CreateMutex(os, &event->freeMutex));

	gcmkONERROR(
		gckOS_CreateMutex(os, &event->listMutex));

	/* Create a bunch of event reccords. */
	for (i = 0; i < gcdEVENT_ALLOCATION_COUNT; ++i)
	{
		/* Allocate an event record. */
		gcmkONERROR(
			gckOS_Allocate(os, gcmSIZEOF(gcsEVENT), (gctPOINTER *) &record));

		/* Push it on the free list. */
		record->next      = event->freeList;
		event->freeList   = record;
		event->freeCount += 1;
	}

	/* Zero out the entire event queue. */
	for (i = 0; i < gcmCOUNTOF(event->queues); ++i)
	{
		event->queues[i].head = gcvNULL;
	}

	/* Zero out the time stamp. */
	event->stamp = 0;

	/* No events to handle. */
	event->pending = 0;

	/* Return pointer to the gckEVENT object. */
	*Event = event;

	/* Success. */
	gcmkFOOTER_ARG("*Event=0x%x", *Event);
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_STATUS();
	/* Roll back. */
	if (event != gcvNULL)
	{
        if (event->atomEventRef != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_AtomDestroy(os, event->atomEventRef));
            event->atomEventRef = gcvNULL;
        }

		if (event->mutexQueue != gcvNULL)
		{
			gcmkVERIFY_OK(gckOS_DeleteMutex(os, event->mutexQueue));
            event->mutexQueue = gcvNULL;
		}

		if (event->freeMutex != gcvNULL)
		{
			gcmkVERIFY_OK(gckOS_DeleteMutex(os, event->freeMutex));
            event->freeMutex = gcvNULL;
		}

		if (event->listMutex != gcvNULL)
		{
			gcmkVERIFY_OK(gckOS_DeleteMutex(os, event->listMutex));
            event->listMutex = gcvNULL;
		}

		while (event->freeList != gcvNULL)
		{
			record          = event->freeList;
			event->freeList = record->next;

			gcmkVERIFY_OK(gckOS_Free(os, record));
		}
        event->freeList = gcvNULL;
        
		gcmkVERIFY_OK(gckOS_Free(os, event));
        event = gcvNULL;
	}

	/* Return the status. */
	gcmkFOOTER();
	return status;
}

/*******************************************************************************
**
**	gckEVENT_Destroy
**
**	Destroy an gckEVENT object.
**
**	INPUT:
**
**		gckEVENT Event
**			Pointer to an gckEVENT object.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gckEVENT_Destroy(
	IN gckEVENT Event
	)
{
	gcsEVENT_PTR record;

	gcmkHEADER_ARG("Event=0x%x", Event);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);

    /* Detsroy the event counts atom. */
    if (Event->atomEventRef != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_AtomDestroy(Event->os, Event->atomEventRef));
        Event->atomEventRef = gcvNULL;
    }

    /* Delete the queue mutex. */
	if (Event->mutexQueue != gcvNULL)
	{
		gcmkVERIFY_OK(gckOS_DeleteMutex(Event->os, Event->mutexQueue));
        Event->mutexQueue = gcvNULL;
	}

	/* Free all free events. */
	while (Event->freeList != gcvNULL)
	{
		record          = Event->freeList;
		Event->freeList = record->next;

		gcmkVERIFY_OK(gckOS_Free(Event->os, record));
	}

    Event->freeList = gcvNULL;
    

	/* Delete the free mutex. */
    if(Event->freeMutex != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_DeleteMutex(Event->os, Event->freeMutex));
        Event->freeMutex = gcvNULL;
    }

	/* Free all pending events. */
	while (Event->list.head != gcvNULL)
	{
		record           = Event->list.head;
		Event->list.head = record->next;

		gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_EVENT,
					   "Event record 0x%x is still pending for %d.",
					   record, Event->list.source);
		gcmkVERIFY_OK(gckOS_Free(Event->os, record));
	}

    Event->list.head =  gcvNULL;

	/* Delete the list mutex. */
    if(Event->listMutex != gcvNULL)
	{
	    gcmkVERIFY_OK(gckOS_DeleteMutex(Event->os, Event->listMutex));
        Event->listMutex = gcvNULL;
    }
    
	/* Mark the gckEVENT object as unknown. */
	Event->object.type = gcvOBJ_UNKNOWN;

	/* Free the gckEVENT object. */
	gcmkVERIFY_OK(gckOS_Free(Event->os, Event));

    Event = gcvNULL;

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;
}

static gceSTATUS
gckEVENT_AllocateRecord(
	IN gckEVENT Event,
	OUT gcsEVENT_PTR * Record
	)
{
	gceSTATUS status;
	gctBOOL acquired = gcvFALSE;
	gctINT i;
	gcsEVENT_PTR record;

	gcmkHEADER_ARG("Event=0x%x", Event);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
	gcmkVERIFY_ARGUMENT(Record != gcvNULL);

	/* Test if we are below the allocation threshold. */
	if (Event->freeCount < gcdEVENT_MIN_THRESHOLD)
	{
		/* Allocate a bunch of records. */
		for (i = 0; i < gcdEVENT_ALLOCATION_COUNT; ++i)
		{
			/* Allocate an event record. */
			status = gckOS_Allocate(Event->os,
									gcmSIZEOF(gcsEVENT),
									(gctPOINTER *) &record);

			if (gcmIS_ERROR(status))
			{
                gcmkLOG_WARNING_ARGS("status=%d, i=%d, Out of memory allocating event records",
                                        status, i);
				gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_EVENT,
							   "Out of memory allocating event records.");
				break;
			}

			/* Acquire the mutex. */
			gcmkONERROR(gckOS_AcquireMutex(Event->os,
										   Event->freeMutex,
										   gcvINFINITE));
			acquired = gcvTRUE;

			/* Push it on the free list. */
			record->next      = Event->freeList;
			Event->freeList   = record;
			Event->freeCount += 1;

			/* Release the mutex. */
			gcmkONERROR(gckOS_ReleaseMutex(Event->os, Event->freeMutex));
            acquired = gcvFALSE;
		}
	}

	/* Acquire the mutex. */
	gcmkONERROR(gckOS_AcquireMutex(Event->os, Event->freeMutex, gcvINFINITE));
	acquired = gcvTRUE;

	*Record           = Event->freeList;
	Event->freeList   = Event->freeList->next;
	Event->freeCount -= 1;

	/* Release the mutex. */
	gcmkONERROR(gckOS_ReleaseMutex(Event->os, Event->freeMutex));
	acquired = gcvFALSE;

	/* Success. */
	gcmkFOOTER_ARG("*Record=0x%x", gcmOPT_POINTER(Record));
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_ARGS("status=%d, acquired=%d", status, acquired);
	/* Roll back. */
	if (acquired)
	{
		gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->freeMutex));
	}

	/* Return the status. */
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
	gcmkONERROR(gckOS_AcquireMutex(Event->os, Event->freeMutex, gcvINFINITE));
	acquired = gcvTRUE;

	/* Push the record on the free list. */
	Record->next      = Event->freeList;
	Event->freeList   = Record;
	Event->freeCount += 1;

	/* Release the mutex. */
	gcmkONERROR(gckOS_ReleaseMutex(Event->os, Event->freeMutex));

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_ARGS("status=%d, acquired=%d", status, acquired);
	/* Roll back. */
	if (acquired)
	{
		gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->freeMutex));
	}

	/* Return the status. */
	gcmkFOOTER();
	return gcvSTATUS_OK;
}

static gceSTATUS
gckEVENT_AddList(
	IN gckEVENT Event,
	IN gcsHAL_INTERFACE_PTR Interface,
    IN gceKERNEL_WHERE FromWhere
	)
{
	gceSTATUS status;
	gctBOOL acquired = gcvFALSE;
	gcsEVENT_PTR record = gcvNULL;

    gcmkHEADER_ARG("Event=0x%x Interface=0x%x FromWhere=%d",
                   Event, Interface, FromWhere);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
	gcmkVERIFY_ARGUMENT(Interface != gcvNULL);

	switch (FromWhere)
	{
	case gcvKERNEL_COMMAND:
	case gcvKERNEL_PIXEL:
		/* Check if the requested source matches the list. */
		if ((Event->list.head   != gcvNULL)
		&&  (Event->list.source != FromWhere)
		)
		{
			/* No match - auto-submit the list. */
            status = gckEVENT_Submit(Event, gcvFALSE);

			if (status == gcvSTATUS_OUT_OF_RESOURCES)
			{
				/* When we are out of resources, just convert to submit from
				** PIXEL. */
				Event->list.source = FromWhere = gcvKERNEL_PIXEL;
			}

			else
			{
				/* Check for error. */
				gcmkONERROR(status);
			}
		}
		break;

	default:
		/* Invalid argument. */
		gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
	}

	/* Allocate a free record. */
	gcmkONERROR(gckEVENT_AllocateRecord(Event, &record));

	/* Copy the event interface into the record. */
	gcmkONERROR(gckOS_MemCopy(&record->event,
							  Interface,
							  gcmSIZEOF(record->event)));

#if 0
	gcmkASSERT
		(  (Interface->command == gcvHAL_FREE_NON_PAGED_MEMORY)
		|| (Interface->command == gcvHAL_FREE_CONTIGUOUS_MEMORY)
		|| (Interface->command == gcvHAL_FREE_VIDEO_MEMORY)
		|| (Interface->command == gcvHAL_WRITE_DATA)
		|| (Interface->command == gcvHAL_UNLOCK_VIDEO_MEMORY)
		|| (Interface->command == gcvHAL_SIGNAL)
		|| (Interface->command == gcvHAL_UNMAP_USER_MEMORY)
		);
#endif

	record->next = gcvNULL;

	/* Acquire the mutex. */
	gcmkONERROR(gckOS_AcquireMutex(Event->os, Event->listMutex, gcvINFINITE));
	acquired = gcvTRUE;

	if (Event->list.head == gcvNULL)
	{
		/* List doesn't exist yet. */
		Event->list.head = record;
		Event->list.tail = record;
	}
	else
	{
		/* Append to the current list. */
		Event->list.tail->next = record;
		Event->list.tail       = record;
	}

	/* Mark the source of this event. */
	Event->list.source = FromWhere;

	/* Release the mutex. */
	gcmkONERROR(gckOS_ReleaseMutex(Event->os, Event->listMutex));

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_ARGS("status=%d, acquired=%d, record=0x%08x", status, acquired, record);
	/* Roll back. */
	if (acquired)
	{
		gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->listMutex));
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
**	gckEVENT_FreeNonPagedMemory
**
**	Schedule an event to free non-paged memory.
**
**	INPUT:
**
**		gckEVENT Event
**			Pointer to an gckEVENT object.
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
	iface.u.FreeNonPagedMemory.physical = Physical;
	iface.u.FreeNonPagedMemory.logical  = Logical;

	/* Append it to the queue. */
    gcmkONERROR(gckEVENT_AddList(Event, &iface, FromWhere));

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_STATUS();
	/* Return the status. */
	gcmkFOOTER();
	return status;
}

/*******************************************************************************
**
**	gckEVENT_FreeContigiuousMemory
**
**	Schedule an event to free contiguous memory.
**
**	INPUT:
**
**		gckEVENT Event
**			Pointer to an gckEVENT object.
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
	iface.u.FreeContiguousMemory.physical = Physical;
	iface.u.FreeContiguousMemory.logical  = Logical;

	/* Append it to the queue. */
    gcmkONERROR(gckEVENT_AddList(Event, &iface, FromWhere));

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_STATUS();
	/* Return the status. */
	gcmkFOOTER();
	return status;
}

/*******************************************************************************
**
**	gckEVENT_FreeVideoMemory
**
**	Schedule an event to free video memory.
**
**	INPUT:
**
**		gckEVENT Event
**			Pointer to an gckEVENT object.
**
**      gcuVIDMEM_NODE_PTR VideoMemory
**          Pointer to a gcuVIDMEM_NODE object to free.
**
**      gceKERNEL_WHERE FromWhere
**          Place in the pipe where the event needs to be generated.
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
	iface.u.FreeVideoMemory.node = VideoMemory;

	/* Append it to the queue. */
    gcmkONERROR(gckEVENT_AddList(Event, &iface, FromWhere));

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_STATUS();
	/* Return the status. */
	gcmkFOOTER();
	return status;
}

/*******************************************************************************
**
**	gckEVENT_Signal
**
**	Schedule an event to trigger a signal.
**
**	INPUT:
**
**		gckEVENT Event
**			Pointer to an gckEVENT object.
**
**      gctSIGNAL Signal
**          Pointer to the signal to trigger.
**
**      gceKERNEL_WHERE FromWhere
**          Place in the pipe where the event needs to be generated.
**
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
	iface.u.Signal.signal    = Signal;
#ifdef __QNXNTO__
    iface.u.Signal.coid      = 0;
    iface.u.Signal.rcvid     = 0;
#else
	iface.u.Signal.auxSignal = gcvNULL;
	iface.u.Signal.process   = gcvNULL;
#endif

	/* Append it to the queue. */
    gcmkONERROR(gckEVENT_AddList(Event, &iface, FromWhere));

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_STATUS();
	/* Return the status. */
	gcmkFOOTER();
	return status;
}

/*******************************************************************************
**
**	gckEVENT_Unlock
**
**	Schedule an event to unlock virtual memory.
**
**	INPUT:
**
**		gckEVENT Event
**			Pointer to an gckEVENT object.
**
**      gceKERNEL_WHERE FromWhere
**          Place in the pipe where the event needs to be generated.
**
**      gcuVIDMEM_NODE_PTR Node
**          Pointer to a gcuVIDMEM_NODE union that specifies the virtual memory
**			to unlock.
**
**		gceSURF_TYPE Type
**			Type of surface to unlock.
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
	iface.u.UnlockVideoMemory.node          = Node;
	iface.u.UnlockVideoMemory.type          = Type;
	iface.u.UnlockVideoMemory.asynchroneous = 0;

	/* Append it to the queue. */
    gcmkONERROR(gckEVENT_AddList(Event, &iface, FromWhere));

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_STATUS();
	/* Return the status. */
	gcmkFOOTER();
	return status;
}

gceSTATUS
gckEVENT_Commit(
	IN gckEVENT Event,
	IN gcsQUEUE_PTR Queue
	)
{
	gceSTATUS status;
	gcsQUEUE_PTR record = gcvNULL, next;

    gcmkHEADER_ARG("Event=0x%x Queue=0x%x", Event, Queue);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
    
    if(Queue == gcvNULL)
    {
#if !VIVANTE_POWER_MANAGE
    	/* Try to set idle */
    	gcmkVERIFY_OK(gckEVENT_TryToSetIdle(Event));
    	gcmkFOOTER_NO();
        return gcvSTATUS_OK;
#endif
    }

	/* Loop while there are records in the queue. */
	while (Queue != gcvNULL)
	{
		/* Map record into kernel memory. */
		gcmkONERROR(gckOS_MapUserPointer(Event->os,
										 Queue,
										 gcmSIZEOF(gcsQUEUE),
										 (gctPOINTER *) &record));

		/* Append event record to event queue. */
        gcmkONERROR(gckEVENT_AddList(Event, &record->iface, gcvKERNEL_PIXEL));

		/* Next record in the queue. */
		next = record->next;

		/* Unmap record from kernel memory. */
		gcmkONERROR(
			gckOS_UnmapUserPointer(Event->os,
								   Queue,
								   gcmSIZEOF(gcsQUEUE),
								   (gctPOINTER *) record));
		record = gcvNULL;

		Queue = next;
	}

	/* Submit the event list. */
    gcmkONERROR(gckEVENT_Submit(Event, gcvTRUE));

	/* Success */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_ARGS("status=%d, record=0x%08x, Queue=0x%08x", status, record, Queue);
	/* Roll back. */
	if (record == gcvNULL)
	{
		gcmkVERIFY_OK(gckOS_UnmapUserPointer(Event->os,
											 Queue,
											 gcmSIZEOF(gcsQUEUE),
											 (gctPOINTER *) record));
	}

	/* Return the status. */
	gcmkFOOTER();
	return status;
}

gceSTATUS
gckEVENT_Interrupt(
	IN gckEVENT Event,
	IN gctUINT32 Data
	)
{
	gcmkHEADER_ARG("Event=0x%x Data=%08x", Event, Data);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);

	/* Combine current interrupt status with pending flags. */
	Event->pending |= Data;

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;
}

gceSTATUS
gckEVENT_Notify(
	IN gckEVENT Event,
	IN gctBOOL IsReset
	)
{
	gceSTATUS status = gcvSTATUS_OK;
	gctINT i;
	gcsEVENT_QUEUE * queue = gcvNULL;
	gctUINT mask = 0;
	gctBOOL acquired = gcvFALSE;
    gctBOOL cmdAcquired = gcvFALSE;
#ifdef __QNXNTO__
	gcuVIDMEM_NODE_PTR node;
#endif
    gctUINT pending;
    gctBOOL suspended = gcvFALSE;
#if VIVANTE_POWER_MANAGE
    gctBOOL empty = gcvFALSE, idle = gcvFALSE;
#endif
	gcmkHEADER_ARG("Event=0x%x", Event);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);

    for (;;)
    {
        /* Suspend interrupts. */
        gcmkONERROR(gckOS_SuspendInterrupt(Event->os));
        suspended = gcvTRUE;

		/* Get current interrupts. */
        pending = Event->pending;

        /* Resume interrupts. */
        gcmkONERROR(gckOS_ResumeInterrupt(Event->os));
        suspended = gcvFALSE;

        if (pending == 0)
        {
            /* No more pending interrupts - done. */
            break;
        }

		gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_EVENT,
					   "Pending interrupts 0x%08x", pending);

		queue = gcvNULL;

#if gcdDEBUG
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
#endif

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
				}
			}
		}

		if (queue == gcvNULL)
		{
            gcmkLOG_WARNING_ARGS("Queue is null,interrupts 0x%08x are not pending", pending);
			gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_EVENT,
						   "Interrupts 0x%08x are not pending.", pending);

			break;
		}

		/* Check whether there is a missed interrupt. */
		for (i = 0; i < gcmCOUNTOF(Event->queues); ++i)
		{
			if ((Event->queues[i].head != gcvNULL)
			&&  (Event->queues[i].stamp < queue->stamp)
			&&  (Event->queues[i].source == queue->source)
			)
			{
                gcmkLOG_WARNING_ARGS("Event %d lost (stamp %llu)",
                                    i, Event->queues[i].stamp);
				gcmkTRACE(gcvLEVEL_ERROR,
						  "Event %d lost (stamp %llu)",
						  i, Event->queues[i].stamp);

				/* Use this event instead. */
				queue = &Event->queues[i];
				mask  = 0;
			}
		}

		/* Walk all events for this interrupt. */
		while (queue->head != gcvNULL)
		{
			gcsEVENT_PTR event;
#ifndef __QNXNTO__
			gctPOINTER logical;
#endif

			event       = queue->head;

            gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_EVENT,
                "event->event.command: %d", event->event.command);
			/* Dispatch on event type. */
			switch (event->event.command)
			{
			case gcvHAL_FREE_NON_PAGED_MEMORY:
				/* Free non-paged memory. */
				status = gckOS_FreeNonPagedMemory(
							Event->os,
							event->event.u.FreeNonPagedMemory.bytes,
							event->event.u.FreeNonPagedMemory.physical,
							event->event.u.FreeNonPagedMemory.logical);
				break;

			case gcvHAL_FREE_CONTIGUOUS_MEMORY:
                /* Unmap the user memory. */
				status = gckOS_FreeContiguous(
							Event->os,
							event->event.u.FreeContiguousMemory.physical,
							event->event.u.FreeContiguousMemory.logical,
							event->event.u.FreeContiguousMemory.bytes);
				break;

			case gcvHAL_FREE_VIDEO_MEMORY:
#ifdef __QNXNTO__
	            node = event->event.u.FreeVideoMemory.node;
	            if ((node->VidMem.memory->object.type == gcvOBJ_VIDMEM)
	            &&  (node->VidMem.logical != gcvNULL)
	            )
	            {
	                gcmkERR_BREAK(
	                    gckKERNEL_UnmapVideoMemory(event->kernel,
                                                   node->VidMem.logical,
                                                   event->event.pid,
                                                   node->VidMem.bytes));
	                node->VidMem.logical = gcvNULL;
	            }
#endif

				/* Free video memory. */
				status = gckVIDMEM_Free(event->event.u.FreeVideoMemory.node);
				break;

			case gcvHAL_WRITE_DATA:
#ifndef __QNXNTO__
				/* Convert physical into logical address. */
				gcmkERR_BREAK(
					gckOS_MapPhysical(Event->os,
									  event->event.u.WriteData.address,
	  								  gcmPTR2INT(event->event.u.WriteData.kernelAddress),
									  gcmSIZEOF(gctUINT32),
									  &logical));

				/* Write data. */
				gcmkERR_BREAK(
					gckOS_WriteMemory(Event->os,
									  logical,
									  event->event.u.WriteData.data));

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
										  event->event.u.WriteData.address,
									  event->event.u.WriteData.data));
#endif
				break;

			case gcvHAL_UNLOCK_VIDEO_MEMORY:
				/* Unlock. */
				status = gckVIDMEM_Unlock(event->event.u.UnlockVideoMemory.node,
										  event->event.u.UnlockVideoMemory.type,
										  gcvNULL);
				break;

			case gcvHAL_SIGNAL:
#ifdef __QNXNTO__
				if ((event->event.u.Signal.coid == 0)
				&&  (event->event.u.Signal.rcvid == 0)
				)
				{
					/* Kernel signal. */
					gcmkERR_BREAK(
						gckOS_Signal(Event->os,
									 event->event.u.Signal.signal,
									 gcvTRUE));
				}
				else
				{
					/* User signal. */
					gcmkERR_BREAK(
						gckOS_UserSignal(Event->os,
										 event->event.u.Signal.signal,
										 event->event.u.Signal.rcvid,
										 event->event.u.Signal.coid));
				}
#else
				/* Set signal. */
				if (event->event.u.Signal.process == gcvNULL)
				{
					/* Kernel signal. */
					gcmkERR_BREAK(
						gckOS_Signal(Event->os,
									 event->event.u.Signal.signal,
									 gcvTRUE));
				}
				else
				{
					/* User signal. */
					gcmkERR_BREAK(
						gckOS_UserSignal(Event->os,
										 event->event.u.Signal.signal,
										 event->event.u.Signal.process));
				}

				gcmkASSERT(event->event.u.Signal.auxSignal == gcvNULL);
#endif
                break;

			case gcvHAL_UNMAP_USER_MEMORY:
				/* Unmap the user memory. */
				status =
					gckOS_UnmapUserMemory(Event->os,
										  event->event.u.UnmapUserMemory.memory,
										  event->event.u.UnmapUserMemory.size,
										  event->event.u.UnmapUserMemory.info,
										  event->event.u.UnmapUserMemory.address);
				break;

    		case gcvHAL_SET_IDLE:
				if(!IsReset)
				{
        			/* Grab the mutex. */
        			gcmkONERROR(gckOS_AcquireRecMutex(Event->os,
        											Event->kernel->hardware->recMutexPower,
        											gcvINFINITE));
                    cmdAcquired = gcvTRUE;
                    
                    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_EVENT,
                        "lastCommitStamp=%llu  commitStamp=%llu",
                        Event->lastCommitStamp, Event->kernel->command->commitStamp);
                    
        			/* Set idle if no new commitments */
        			if (Event->lastCommitStamp == Event->kernel->command->commitStamp)
        			{
                        if(Event->kernel->command->idle == gcvFALSE)
                        {
            				Event->kernel->command->idle = gcvTRUE;
            				gcmkVERIFY_OK(gckOS_NotifyIdle(Event->os, gcvTRUE));
                        }
        			}

        			/* Release the mutex. */
        			gcmkVERIFY_OK(gckOS_ReleaseRecMutex(Event->os, Event->kernel->hardware->recMutexPower));
                    cmdAcquired = gcvFALSE;
				}
                break;

			default:
				/* Invalid argument. */
				gcmkFATAL("Unknown event type: %d", event->event.command);
				status = gcvSTATUS_INVALID_ARGUMENT;
				break;
			}

			/* Make sure there are no errors generated. */
			gcmkASSERT(gcmNO_ERROR(status));

			/* Pop the event from the event queue. */
			gcmkONERROR(
				gckOS_AcquireMutex(Event->os, Event->mutexQueue, gcvINFINITE));
			acquired = gcvTRUE;

			/* Unlink head from chain. */
			queue->head = event->next;

			gcmkONERROR(gckOS_ReleaseMutex(Event->os, Event->mutexQueue));
			acquired = gcvFALSE;

			/* Free the event. */
			gcmkVERIFY_OK(gckOS_Free(Event->os, event));
		}

		gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_EVENT,
					   "Handled interrupt 0x%08x", mask);

        /* Suspend interrupts. */
        gcmkONERROR(gckOS_SuspendInterrupt(Event->os));
        suspended = gcvTRUE;

		/* Mark pending interrupt as handled. */
		Event->pending &= ~mask;

        /* Resume interrupts. */
        gcmkONERROR(gckOS_ResumeInterrupt(Event->os));
        suspended = gcvFALSE;
	}

    if(!IsReset)
    {
#if VIVANTE_POWER_MANAGE
		/* Grab the mutex. */
		gcmkONERROR(gckOS_AcquireRecMutex(Event->os,
										Event->kernel->hardware->recMutexPower,
										gcvINFINITE));
        cmdAcquired = gcvTRUE;
        
        /* Check whether the event queue is empty. */
        gcmkONERROR(_IsEmpty(Event, &empty));

        if (empty)
        {
            /* Query whether the hardware is idle. */
            gcmkONERROR(gckHARDWARE_QueryIdle(Event->kernel->hardware, &idle));

            if (idle)
            {
                if(Event->kernel->command->idle == gcvFALSE)
                {
    				Event->kernel->command->idle = gcvTRUE;
    				gcmkVERIFY_OK(gckOS_NotifyIdle(Event->os, gcvTRUE));
                }
            }
        }
        
    	/* Release the mutex. */
		gcmkVERIFY_OK(gckOS_ReleaseRecMutex(Event->os, Event->kernel->hardware->recMutexPower));
        cmdAcquired = gcvFALSE;
#else
        {
        	/* Try to set idle */
        	gcmkVERIFY_OK(gckEVENT_TryToSetIdle(Event));
        }
#endif
    }

    {
        gctINT32 old = 0;
        /* Increment the number of event counts. */
        gcmkONERROR(
            gckOS_AtomIncrement(Event->kernel->os, Event->atomEventRef, &old));
    }

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_ARGS("status=%d, acquired=%d, suspended=%d, queue=0x%08x",
                        status, acquired, suspended, queue);
	if (acquired)
	{
		/* Release mutex. */
		gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->mutexQueue));
	}

    if (suspended)
    {
        /* Resume interrupts. */
        gcmkVERIFY_OK(gckOS_ResumeInterrupt(Event->os));
    }

    if (cmdAcquired)
    {
        /* Release the command queue mutex. */
		gcmkVERIFY_OK(gckOS_ReleaseRecMutex(Event->os, Event->kernel->hardware->recMutexPower));
    }

	/* Return the status. */
	gcmkFOOTER();
	return status;
}

gceSTATUS
gckEVENT_Submit(
	IN gckEVENT Event,
    IN gctBOOL Wait
	)
{
	gctUINT8 id = 0xFF;
	gctSIZE_T bytes;
	gctPOINTER buffer;
	gceSTATUS status;
    gctBOOL acquired = gcvFALSE;
	gctBOOL reserved = gcvFALSE;
#if gcdGPU_TIMEOUT
    gctUINT32 timer = 0;
#endif

    gcmkHEADER_ARG("Event=0x%x Wait=%d", Event, Wait);

	/* Only process if we have events queued. */
	if (Event->list.head != gcvNULL)
	{
	    for (;;)
	    {
		    /* Allocate an event ID. */
    		status = _GetEvent(Event, &id, Event->list.source);

    		if (gcmIS_ERROR(status))
    		{
				/* Out of resources? */
                if (Wait && (status == gcvSTATUS_OUT_OF_RESOURCES))
                {
					/* Delay a while. */
                    gcmkONERROR(gckOS_Delay(Event->os, 1));

#if gcdGPU_TIMEOUT && VIVANTE_POWER_MANAGE
                    if (++timer >= gcdGPU_TIMEOUT)
                    {
                        gcmkONERROR(gckOS_Broadcast(Event->os,
                                                    Event->kernel->hardware,
                                                    gcvBROADCAST_GPU_STUCK));

                        gcmkONERROR(gcvSTATUS_GPU_NOT_RESPONDING);
                }
#endif
                }
                else
                {
                    gcmkONERROR(status);
                }
    		}
    		else
    		{
				/* Got en event ID. */
        		break;
    		}
        }

		gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_EVENT, "Using id=%d", id);

		/* Acquire the list mutex. */
		gcmkONERROR(gckOS_AcquireMutex(Event->os,
									   Event->listMutex,
									   gcvINFINITE));
		acquired = gcvTRUE;

		/* Copy event list to event ID queue. */
		Event->queues[id].source = Event->list.source;
		Event->queues[id].head   = Event->list.head;

        /* Get process ID. */
        gcmkONERROR(gckOS_GetProcessID(&Event->queues[id].processID));

		/* Mark event list as empty. */
		Event->list.head = gcvNULL;

		/* Release the list mutex. */
		gcmkONERROR(gckOS_ReleaseMutex(Event->os, Event->listMutex));
		acquired = gcvFALSE;

#if gcdNULL_DRIVER == 2
		/* Notify immediately on infinite hardware. */
		gcmkONERROR(gckEVENT_Interrupt(Event, 1 << id));

		gcmkONERROR(gckEVENT_Notify(Event, 0));
#else
		/* Get the size of the hardware event. */
		gcmkONERROR(gckHARDWARE_Event(Event->kernel->hardware,
									  gcvNULL,
									  id,
									  gcvKERNEL_PIXEL,
									  &bytes));

		/* Reserve space in the command queue. */
		gcmkONERROR(gckCOMMAND_Reserve(Event->kernel->command,
									   bytes,
									   &buffer,
									   &bytes));
		reserved = gcvTRUE;

		/* Set the hardware event in the command queue. */
		gcmkONERROR(gckHARDWARE_Event(Event->kernel->hardware,
									  buffer,
									  id,
									  Event->queues[id].source,
									  &bytes));

		/* Execute the hardware event. */
        gcmkONERROR(gckCOMMAND_Execute(Event->kernel->command, bytes));
		reserved = gcvFALSE;
#endif
	}

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_ARGS("status=%d, acquired=%d, reserved=%d, eventId=%d",
                        status, acquired, reserved, id);
	if (acquired)
	{
		/* Need to unroll the mutex acquire. */
		gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->listMutex));
	}

    if (reserved)
	{
		/* Need to release the command buffer. */
		gcmkVERIFY_OK(gckCOMMAND_Release(Event->kernel->command));
	}

	if (id != 0xFF)
	{
		/* Need to unroll the event allocation. */
		Event->queues[id].head = gcvNULL;
	}

	/* Return the status. */
	gcmkFOOTER();
	return status;
}

/*******************************************************************************
**
**	gckEVENT_SetIdle
**
**	Schedule an event to set idle status.
**
**	INPUT:
**
**		gcoEVENT Event
**			Pointer to an gcoEVENT object.
**
**      gctPOINTER CommandBuffer
**          Pointer to the command buffer to append the event.
**
**      gctSIZE_T * CommandSize
**          Pointer to the number of bytes available in the command buffer.
**
**      gceKERNEL_WHERE FromWhere
**          Place in the pipe where the event needs to be generated.
**
**		gctBOOL Wait
**			Wait flag.  Should be set to gcvFALSE for any event that needs to be
**			copied into a command buffer since we might exhaust the number of
**			events.
**
**	OUTPUT:
**
**		gctSIZE_T * CommandSize
**          Number of bytes used for the event.
*/
gceSTATUS
gckEVENT_SetIdle(
	IN gckEVENT Event,
	IN gctPOINTER CommandBuffer,
	IN OUT gctSIZE_T * CommandSize,
	IN gceKERNEL_WHERE FromWhere,
	IN gctBOOL Wait
	)
{
    gceSTATUS status;
	gcsHAL_INTERFACE iface;

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
	gcmkHEADER_ARG("Event=0x%x CommandBuffer=0x%x CommandSize=0x%x FromWhere=%d Wait=%d",
                    Event, CommandBuffer, CommandSize, FromWhere, Wait);

	/* Create an event. */
	iface.command = gcvHAL_SET_IDLE;

	/* Append it to the queue. */
	gcmkONERROR(gckEVENT_AddList(Event, &iface, FromWhere));

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_STATUS();
	/* Return the status. */
	gcmkFOOTER();
	return status;
}

#define ENABLE_COMMITSTAMP_CHECK    0
gceSTATUS
gckEVENT_TryToSetIdle(
	IN gckEVENT Event
	)
{
	gctINT i = -1;
	gctBOOL setIdle;

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Event, gcvOBJ_EVENT);
	gcmkHEADER_ARG("Event=0x%x", Event);

	if (!Event->kernel->notifyIdle)
    {
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

	/* Initialize the flag */
	setIdle = gcvFALSE;

	/* Grab the queue mutex. */
	gcmkVERIFY_OK(gckOS_AcquireMutex(Event->os,
									 Event->mutexQueue,
									 gcvINFINITE));

	/* Suspend interrupt */
	gcmkVERIFY_OK(gckOS_SuspendInterrupt(Event->os));

    /*
        FIXME: this check may cause system enter into idle status every 120 seconds in home screen scenario on TD,
               Temp solution -- disable this.
    */
#if ENABLE_COMMITSTAMP_CHECK
	if (Event->lastCommitStamp != Event->kernel->command->commitStamp)
#endif
	{
		setIdle = gcvTRUE;

		/* Check if no pending events */
		for (i = 0; i < gcmCOUNTOF(Event->queues); ++i)
		{
			if (Event->queues[i].head != gcvNULL)
			{
				setIdle = gcvFALSE;
				break;
			}
		}
	}
    
	/* Resume interrupt */
	gcmkVERIFY_OK(gckOS_ResumeInterrupt(Event->os));

	/* Release the queue mutex. */
	gcmkVERIFY_OK(gckOS_ReleaseMutex(Event->os, Event->mutexQueue));

	/* Issue an event to set idle if necessary */
	if (setIdle)
	{
        /* Grab the conmmand queue mutex. */
        gcmkVERIFY_OK(
        	gckOS_AcquireMutex(Event->kernel->command->os,
    						   Event->kernel->command->mutexQueue,
    						   gcvINFINITE)); 

		/* Append the EVENT command to write data into the boolean. */
		gcmkVERIFY_OK(gckEVENT_SetIdle(Event,
									gcvNULL,
									0,
									gcvKERNEL_PIXEL,
									gcvTRUE));
        
		Event->lastCommitStamp = Event->kernel->command->commitStamp + 1;
        
        /* Release the command queue mutex. */
        gcmkVERIFY_OK(
        	gckOS_ReleaseMutex(Event->kernel->command->os, Event->kernel->command->mutexQueue));
        
	}

	/* Success. */
	gcmkFOOTER_ARG("i=%d setIdle=%d", i, setIdle);
	return gcvSTATUS_OK;
}

