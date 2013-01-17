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

#if defined(__QNXNTO__)
#include <sys/slog.h>
#endif

#if MRVL_LOW_POWER_MODE_DEBUG
#include <linux/module.h>
#endif

#define _GC_OBJ_ZONE    gcvZONE_COMMAND

/******************************************************************************\
********************************* Support Code *********************************
\******************************************************************************/

#if MRVL_PRINT_CMD_BUFFER

typedef struct _gcsRECORD_INFO * gcsRECORD_INFO_PTR;
typedef struct _gcsRECORD_INFO
{
	gctUINT		    count;
	gctUINT		    index;
	gctUINT		    tail;
}
gcsRECORD_INFO;

typedef enum _gceDUMPLOCATION
{
    gcvDUMP_NEW_QUEUE,
    gcvDUMP_COMMIT,
    gcvDUMP_EXECUTE,
    gcvDUMP_STALL,
    gcvDUMP_CMD,
    gcvDUMP_EVENT,
}
gceDUMPLOCATION;

typedef struct _gcsCMDBUF_RECORD * gcsCMDBUF_RECORD_PTR;
typedef struct _gcsCMDBUF_RECORD
{
	gctPOINTER      logical;
	gctUINT32		address;
	gctSIZE_T		size;
	gceDUMPLOCATION	location;
}
gcsCMDBUF_RECORD;

typedef struct _gcsLINK_RECORD * gcsLINK_RECORD_PTR;
typedef struct _gcsLINK_RECORD
{
	gctUINT32_PTR	fromLogical;
	gctUINT32	    fromAddress;
	gctUINT32_PTR	toLogical;
	gctUINT32	    toAddress;
}
gcsLINK_RECORD;

#define gcdRECORD_COUNT 100

static gctUINT _cmdQueueCount;

static gcsRECORD_INFO _cmdInfo = { 0, ~0, 0 };
static gcsRECORD_INFO _lnkInfo = { 0, ~0, 0 };

static gcsCMDBUF_RECORD _cmdRecord[gcdRECORD_COUNT];
static gcsLINK_RECORD   _lnkRecord[gcdRECORD_COUNT];

static void _AdvanceRecord(
	gcsRECORD_INFO_PTR Record
	)
{
	Record->index = (Record->index + 1) % gcdRECORD_COUNT;

	if (Record->count < gcdRECORD_COUNT)
	{
		Record->count += 1;
	}
	else
	{
		Record->tail = (Record->tail + 1) % gcdRECORD_COUNT;
	}
}

static void _AddCmdBuffer(
	gckCOMMAND Command,
	gctUINT8_PTR Logical,
	gctSIZE_T Size,
	gceDUMPLOCATION Location
	)
{
	gctUINT address;
	gcsCMDBUF_RECORD_PTR record;

    if(!Command->dumpCmdBuf)
        return;

	_AdvanceRecord(&_cmdInfo);

	if (Location == gcvDUMP_NEW_QUEUE)
	{
		_cmdQueueCount += 1;
	}

    gckHARDWARE_ConvertLogical(
        Command->kernel->hardware, Logical, &address
        );

    if (Logical == gcvNULL)
    {
        address = ~0x00000000;
    }

	record = &_cmdRecord[_cmdInfo.index];

	record->address = address;
	record->size    = Size;
	record->location= Location;
}

static void _AddLink(
	gckCOMMAND Command,
	gctUINT32_PTR From,
	gctUINT32_PTR To
	)
{
	gctUINT from, to;
	gcsLINK_RECORD_PTR record;

    if(!Command->dumpCmdBuf)
        return;
    
	_AdvanceRecord(&_lnkInfo);

    gckHARDWARE_ConvertLogical(
        Command->kernel->hardware, From, &from
        );

    if (From == gcvNULL)
    {
        from = ~0x00000000;
    }

    gckHARDWARE_ConvertLogical(
        Command->kernel->hardware, To, &to
        );
    
    if (To == gcvNULL)
    {
        to = ~0x00000000;
    }

	record = &_lnkRecord[_lnkInfo.index];

	record->fromLogical = From;
	record->fromAddress = from;
	record->toLogical   = To;
	record->toAddress   = to;
}

static gctUINT _FindCmdBuffer(
	gctUINT Address
	)
{
	gctUINT i, j, first, last;

	i = (_cmdInfo.tail - 1 + _cmdInfo.count) % gcdRECORD_COUNT;

	for (j = 0; j < _cmdInfo.count; j += 1)
	{
		first = _cmdRecord[i].address;
		last  = first + _cmdRecord[i].size;

		if ((Address >= first) && (Address < last))
		{
			return i;
		}

		i = (i - 1 + gcdRECORD_COUNT) % gcdRECORD_COUNT;
	}

	return gcdRECORD_COUNT;
}

static void
_PrintBuffer(
    IN gckCOMMAND Command,
    IN gctPOINTER Pointer,
    IN gctSIZE_T Bytes,
    IN gctUINT Address
    )
{
    gctUINT32_PTR data = (gctUINT32_PTR) Pointer;
    gctUINT32 address;
    gctUINT32 printAddr;

    if(Pointer == gcvNULL)
    {
        gcmkPRINT("@[kernel.command NULL pointer]");
        return;
    }
    
    gckOS_GetPhysicalAddress(Command->os, Pointer, &address);

    gcmkPRINT("@[kernel.command %08X %08X", address, Bytes);

    printAddr = address;
    
    while (Bytes >= 8*4)
    {
        if( (Address >= printAddr) && (Address < printAddr + 32))
        {
            gctINT i,j;
            
            i = (Address - printAddr)/4;
            j = (Address - printAddr)%4;
            gcmkPRINT("  GPU stop @ below data[%d], bytes[%d]", i, j);
        }
        
        gcmkPRINT("  [%08X-%08X]: %08X %08X %08X %08X %08X %08X %08X %08X", 
                    printAddr, printAddr + 32,
                    data[0], data[1], data[2], data[3], data[4], data[5], data[6],
                    data[7]);
        
        data        += 8;
        Bytes       -= 32;
        printAddr   += 32;
    }

    if( (Address >= printAddr) && (Address < printAddr + 32))
    {
        gctINT i,j;
        
        i = (Address - printAddr)/4;
        j = (Address - printAddr)%4;
        gcmkPRINT("  GPU stop @ below data[%d], bytes[%d]", i, j);
    }
            
    switch (Bytes)
    {
    case 7*4:
        gcmkPRINT("  [%08X-        ]: %08X %08X %08X %08X %08X %08X %08X", printAddr,
                  data[0], data[1], data[2], data[3], data[4], data[5], data[6]);
        break;

    case 6*4:
        gcmkPRINT("  [%08X-        ]: %08X %08X %08X %08X %08X %08X", printAddr,
                  data[0], data[1], data[2], data[3], data[4], data[5]);
        break;

    case 5*4:
        gcmkPRINT("  [%08X-        ]: %08X %08X %08X %08X %08X", printAddr,
                  data[0], data[1], data[2], data[3], data[4]);
        break;

    case 4*4:
        gcmkPRINT("  [%08X-        ]: %08X %08X %08X %08X", printAddr, data[0], data[1], data[2], data[3]);
        break;

    case 3*4:
        gcmkPRINT("  [%08X-        ]: %08X %08X %08X", printAddr, data[0], data[1], data[2]);
        break;

    case 2*4:
        gcmkPRINT("  [%08X-        ]: %08X %08X", printAddr, data[0], data[1]);
        break;

    case 1*4:
        gcmkPRINT("  [%08X-        ]: %08X", printAddr, data[0]);
        break;

    default:
        break;
    }

    gcmkPRINT("] -- command");
}

void _PrintLinkChain(
	gckCOMMAND Command
	)
{
	gctUINT i, j;
    
	gcmkPRINT("\nLink chain:\n\n");

	i = _lnkInfo.tail;

	for (j = 0; j < _lnkInfo.count; j += 1)
	{
		gcmkPRINT("  LINK 0x%08X --> 0x%08X\n",
			_lnkRecord[i].fromAddress,
			_lnkRecord[i].toAddress
			);

		i = (i + 1) % gcdRECORD_COUNT;
	}
}

gceSTATUS
_PrintCmdBuffer(
	gckCOMMAND Command,
	gctUINT Address
	)
{
    gceSTATUS status;
	gctUINT i, j, first, last, bufIndex;
	gcsCMDBUF_RECORD_PTR buffer;
	gctUINT address;
    
	gcmkPRINT("\n%s(%d):\n"
		"  number of buffers stored %d;\n"
		"  buffer list:\n\n",
		__FUNCTION__, __LINE__, _cmdInfo.count
		);

	i = _cmdInfo.tail;

	for (j = 0; j < _cmdInfo.count; j += 1)
	{
		first = _cmdRecord[i].address;
		last  = first + _cmdRecord[i].size;

		gcmkPRINT("  0x%08X-0x%08X Location:%d\n",
			first,
			last,
			_cmdRecord[i].location
			);

		i = (i + 1) % gcdRECORD_COUNT;
	}

	bufIndex = _FindCmdBuffer(Address);

	if (bufIndex == gcdRECORD_COUNT)
	{
		gcmkPRINT("\n*** buffer not found for the specified location ***\n");
	}
	else
	{
        buffer = &_cmdRecord[bufIndex];
		first = buffer->address;
		last  = first + buffer->size;

		gcmkPRINT("\n%s(%d): buffer found 0x%08X-0x%08X Location:%d:\n\n",
			__FUNCTION__, __LINE__,
			first,
			last,
			buffer->location
			);
        
        gckOS_MapPhysical(Command->os, buffer->address, 0, buffer->size, &buffer->logical);
        _PrintBuffer(Command, buffer->logical, buffer->size, Address);
        
        gckOS_DumpToFile(Command->os, 
                        "/data/dumpGC_CMDBUF.bin", 
                        buffer->logical,
                        buffer->size);
        
        gckOS_UnmapPhysical(Command->os, buffer->logical, buffer->size);
	}

    gcmkONERROR(
        gckHARDWARE_ConvertLogical(Command->kernel->hardware, Command->logical, &address)
        );

	first = address;
	last  = first + Command->pageSize;

	gcmkPRINT("\nCommand queue N%d: 0x%08X-0x%08X:\n\n",
		_cmdQueueCount,
		first,
		last
		);

    _PrintBuffer(Command, Command->logical, Command->pageSize, Address);

    gckOS_DumpToFile(Command->os, 
                    "/data/dumpGC_CMDQUE.bin", 
                    Command->logical,
                    Command->pageSize);

    _PrintLinkChain(Command);

    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    return status;
}

gceSTATUS
_PrintAllCmdBuffer(
	gckCOMMAND Command
	)
{
    gceSTATUS status;
	gctUINT i, j, first, last;
	gcsCMDBUF_RECORD_PTR buffer;
	gctUINT address;
    
	gcmkPRINT("\n%s(%d):\n"
		"  number of buffers stored %d;\n"
		"  buffer list:\n\n",
		__FUNCTION__, __LINE__, _cmdInfo.count
		);

	i = _cmdInfo.tail;

	for (j = 0; j < _cmdInfo.count; j += 1)
	{
		first = _cmdRecord[i].address;
		last  = first + _cmdRecord[i].size;

		gcmkPRINT("  0x%08X-0x%08X Location:%d\n",
			first,
			last,
			_cmdRecord[i].location
			);

        {
    		buffer = &_cmdRecord[i];
            
            gckOS_MapPhysical(Command->os, buffer->address, 0, buffer->size, &buffer->logical);
            _PrintBuffer(Command, buffer->logical, buffer->size, 0xFFFFFFFF);
            gckOS_UnmapPhysical(Command->os, buffer->logical, buffer->size);
	    }

		i = (i + 1) % gcdRECORD_COUNT;
	}

    gcmkONERROR(
        gckHARDWARE_ConvertLogical(Command->kernel->hardware, Command->logical, &address)
        );

	first = address;
	last  = first + Command->pageSize;

	gcmkPRINT("\nCommand queue N%d: 0x%08X-0x%08X:\n\n",
		_cmdQueueCount,
		first,
		last
		);

    _PrintBuffer(Command, Command->logical, Command->pageSize, 0xFFFFFFFF);

    _PrintLinkChain(Command);

    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    return status;
}
#endif

#if gcdDUMP_COMMAND
static void
_DumpCommand(
    IN gckCOMMAND Command,
    IN gctPOINTER Pointer,
    IN gctSIZE_T Bytes
    )
{
    gctUINT32_PTR data = (gctUINT32_PTR) Pointer;
    gctUINT32 address;

    gckOS_GetPhysicalAddress(Command->os, Pointer, &address);

    gcmkPRINT("@[kernel.command %08X %08X %08X", Pointer, address, Bytes);
    while (Bytes >= 8*4)
    {
        gcmkPRINT("  %08X %08X %08X %08X %08X %08X %08X %08X",
                  data[0], data[1], data[2], data[3], data[4], data[5], data[6],
                  data[7]);
        data  += 8;
        Bytes -= 32;
    }

    switch (Bytes)
    {
    case 7*4:
        gcmkPRINT("  %08X %08X %08X %08X %08X %08X %08X",
                  data[0], data[1], data[2], data[3], data[4], data[5],
                  data[6]);
        break;

    case 6*4:
        gcmkPRINT("  %08X %08X %08X %08X %08X %08X",
                  data[0], data[1], data[2], data[3], data[4], data[5]);
        break;

    case 5*4:
        gcmkPRINT("  %08X %08X %08X %08X %08X",
                  data[0], data[1], data[2], data[3], data[4]);
        break;

    case 4*4:
        gcmkPRINT("  %08X %08X %08X %08X", data[0], data[1], data[2], data[3]);
        break;

    case 3*4:
        gcmkPRINT("  %08X %08X %08X", data[0], data[1], data[2]);
        break;

    case 2*4:
        gcmkPRINT("  %08X %08X", data[0], data[1]);
        break;

    case 1*4:
        gcmkPRINT("  %08X", data[0]);
        break;
    }

    gcmkPRINT("] -- command");
}
#endif

/*******************************************************************************
**
**	_NewQueue
**
**	Allocate a new command queue.
**
**	INPUT:
**
**		gckCOMMAND Command
**			Pointer to an gckCOMMAND object.
**
**	OUTPUT:
**
**		gckCOMMAND Command
**			gckCOMMAND object has been updated with a new command queue.
*/
static gceSTATUS
_NewQueue(
    IN OUT gckCOMMAND Command
    )
{
    gceSTATUS status;
	gctINT currentIndex, newIndex;

    gcmkHEADER_ARG("Command=0x%x", Command);

	/* Switch to the next command buffer. */
	currentIndex = Command->index;
	newIndex     = (currentIndex + 1) % gcdCOMMAND_QUEUES;


	/* Wait for availability. */
#if gcdDUMP_COMMAND
    gcmkPRINT("@[kernel.waitsignal]");
#endif
#if MRVL_PRINT_CMD_BUFFER
    _AddCmdBuffer(
        Command, gcvNULL, 0, gcvDUMP_NEW_QUEUE);
#endif

	gcmkONERROR(
		gckOS_WaitSignal(Command->os,
						 Command->queues[newIndex].signal,
						 gcvINFINITE));
    
    /* Update gckCOMMAND object with new command queue. */
	Command->index    = newIndex;
    Command->newQueue = gcvTRUE;
	Command->physical = Command->queues[newIndex].physical;
	Command->logical  = Command->queues[newIndex].logical;
    Command->offset   = 0;

    if (currentIndex >= 0)
    {
        /* Mark the command queue as available. */
        gcmkONERROR(gckEVENT_Signal(Command->kernel->event,
                                    Command->queues[currentIndex].signal,
                                    /*gcvKERNEL_COMMAND*/gcvKERNEL_PIXEL));
        /* Submit the event queue. */
        Command->submit = gcvTRUE;
    }

    /* Success. */
    gcmkFOOTER_ARG("Command->index=%d", Command->index);
    return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_ARGS("index=%d, curIdx=%d, submit=%d", Command->index, currentIndex, Command->submit);
	/* Return the status. */
	gcmkFOOTER();
	return status;
}

/******************************************************************************\
****************************** gckCOMMAND API Code ******************************
\******************************************************************************/

/*******************************************************************************
**
**	gckCOMMAND_Construct
**
**	Construct a new gckCOMMAND object.
**
**	INPUT:
**
**		gckKERNEL Kernel
**			Pointer to an gckKERNEL object.
**
**	OUTPUT:
**
**		gckCOMMAND * Command
**			Pointer to a variable that will hold the pointer to the gckCOMMAND
**			object.
*/
gceSTATUS
gckCOMMAND_Construct(
	IN gckKERNEL Kernel,
	OUT gckCOMMAND * Command
	)
{
    gckOS os;
	gckCOMMAND command = gcvNULL;
	gceSTATUS status;
	gctINT i;

	gcmkHEADER_ARG("Kernel=0x%x", Kernel);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
	gcmkVERIFY_ARGUMENT(Command != gcvNULL);

    /* Extract the gckOS object. */
    os = Kernel->os;

	/* Allocate the gckCOMMAND structure. */
	gcmkONERROR(
		gckOS_Allocate(os,
					   gcmSIZEOF(struct _gckCOMMAND),
					   (gctPOINTER *) &command));

	/* Initialize the gckCOMMAND object.*/
    command->object.type    = gcvOBJ_COMMAND;
    command->kernel         = Kernel;
    command->os             = os;
    command->mutexQueue     = gcvNULL;
    command->mutexContext   = gcvNULL;
    command->powerSemaphore = gcvNULL;
    command->atomCommit     = gcvNULL;

    /* No command queues created yet. */
	command->index = 0;
    
	for (i = 0; i < gcdCOMMAND_QUEUES; ++i)
	{
		command->queues[i].signal  = gcvNULL;
		command->queues[i].logical = gcvNULL;
	}

    /* Get the command buffer requirements. */
    gcmkONERROR(
    	gckHARDWARE_QueryCommandBuffer(Kernel->hardware,
                                       &command->alignment,
                                       &command->reservedHead,
                                       &command->reservedTail));

    /* No contexts available yet. */
    command->contextCounter = command->currentContext = 0;

    /* Create the command queue mutex. */
    gcmkONERROR(
    	gckOS_CreateMutex(os, &command->mutexQueue));

	/* Create the context switching mutex. */
	gcmkONERROR(
		gckOS_CreateMutex(os, &command->mutexContext));

    /* Create the power management semaphore. */
    gcmkONERROR(
        gckOS_CreateSemaphore(os, &command->powerSemaphore));

    /* Create the commit atom. */
    gcmkONERROR(gckOS_AtomConstruct(os, &command->atomCommit));

	/* Get the page size from teh OS. */
	gcmkONERROR(
		gckOS_GetPageSize(os, &command->pageSize));

	/* Set hardware to pipe 0. */
	command->pipeSelect = 0;

    /* Pre-allocate the command queues. */
    for (i = 0; i < gcdCOMMAND_QUEUES; ++i)
    {
        gcmkONERROR(
            gckOS_AllocateNonPagedMemory(os,
                                         gcvFALSE,
                                         &command->pageSize,
                                         &command->queues[i].physical,
                                         &command->queues[i].logical));
        gcmkONERROR(
            gckOS_CreateSignal(os, gcvFALSE, &command->queues[i].signal));

		gcmkONERROR(
			gckOS_Signal(os, command->queues[i].signal, gcvTRUE));
	}

	/* No command queue in use yet. */
	command->index    = -1;
	command->logical  = gcvNULL;
	command->newQueue = gcvFALSE;
    command->submit   = gcvFALSE;

	/* Command is not yet running. */
	command->running = gcvFALSE;

	/* Command queue is idle. */
	command->idle = gcvTRUE;

	/* Commit stamp is zero. */
	command->commitStamp = 0;

    /* Don't dump command buffer and link chain by default */
    command->dumpCmdBuf = gcvFALSE;

    /* Return pointer to the gckCOMMAND object. */
	*Command = command;

	/* Success. */
	gcmkFOOTER_ARG("*Command=0x%x", *Command);
	return gcvSTATUS_OK;

OnError:
	/* Roll back. */
	if (command != gcvNULL)
	{
	    /* Destroy the commit atom. */
        if (command->atomCommit != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_AtomDestroy(os, command->atomCommit));
            command->atomCommit = gcvNULL;
        }

        /* Destroy the power management semaphore. */
        if (command->powerSemaphore != gcvNULL)
        {
            gcmkVERIFY_OK(gckOS_DestroySemaphore(os, command->powerSemaphore));
            command->powerSemaphore = gcvNULL;
        }

        /* Delete the context switching mutex. */
		if (command->mutexContext != gcvNULL)
		{
			gcmkVERIFY_OK(gckOS_DeleteMutex(os, command->mutexContext));
            command->mutexContext = gcvNULL;
		}

        /* Delete the command queue mutex. */
		if (command->mutexQueue != gcvNULL)
		{
			gcmkVERIFY_OK(gckOS_DeleteMutex(os, command->mutexQueue));
            command->mutexQueue = gcvNULL;
		}

		for (i = 0; i < gcdCOMMAND_QUEUES; ++i)
		{
			if (command->queues[i].signal != gcvNULL)
			{
				gcmkVERIFY_OK(
					gckOS_DestroySignal(os, command->queues[i].signal));

                command->queues[i].signal = gcvNULL;
			}

			if (command->queues[i].physical != gcvNULL ||
                command->queues[i].logical != gcvNULL
                )
			{
				gcmkVERIFY_OK(
					gckOS_FreeNonPagedMemory(os,
											 command->pageSize,
											 command->queues[i].physical,
											 command->queues[i].logical));
                command->queues[i].physical = gcvNULL;
                command->queues[i].logical  = gcvNULL;
			}
		}

		gcmkVERIFY_OK(gckOS_Free(os, command));

        command = gcvNULL;
	}

    gcmkLOG_ERROR_STATUS();
	/* Return the status. */
	gcmkFOOTER();
	return status;
}

/*******************************************************************************
**
**	gckCOMMAND_Destroy
**
**	Destroy an gckCOMMAND object.
**
**	INPUT:
**
**		gckCOMMAND Command
**			Pointer to an gckCOMMAND object to destroy.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gckCOMMAND_Destroy(
	IN gckCOMMAND Command
	)
{
	gctINT i;
	gceSTATUS status;
	gcmkHEADER_ARG("Command=0x%x", Command);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

	/* Stop the command queue. */
	gcmkONERROR(gckCOMMAND_Stop(Command));

	for (i = 0; i < gcdCOMMAND_QUEUES; ++i)
	{
		if (Command->queues[i].signal != gcvNULL)
		{
			gcmkVERIFY_OK(
				gckOS_DestroySignal(Command->os, Command->queues[i].signal));

            Command->queues[i].signal = gcvNULL;
		}

		if (Command->queues[i].physical != gcvNULL ||
            Command->queues[i].logical != gcvNULL)
		{
			gcmkVERIFY_OK(
				gckOS_FreeNonPagedMemory(Command->os,
										 Command->pageSize,
										 Command->queues[i].physical,
										 Command->queues[i].logical));
            
            Command->queues[i].physical = gcvNULL;
            Command->queues[i].logical  = gcvNULL;
		}
	}

    /* Delete the context switching mutex. */
	if (Command->mutexContext != gcvNULL)
	{
		gcmkVERIFY_OK(gckOS_DeleteMutex(Command->os, Command->mutexContext));
        Command->mutexContext = gcvNULL;
	}

    /* Delete the Command queue mutex. */
	if (Command->mutexQueue != gcvNULL)
	{
		gcmkVERIFY_OK(gckOS_DeleteMutex(Command->os, Command->mutexQueue));
        Command->mutexQueue = gcvNULL;
	}

    /* Destroy the power management semaphore. */
    if (Command->powerSemaphore != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_DestroySemaphore(Command->os, Command->powerSemaphore));
        Command->powerSemaphore = gcvNULL;
    }

    /* Destroy the commit atom. */
    if (Command->atomCommit != gcvNULL)
    {
        gcmkVERIFY_OK(gckOS_AtomDestroy(Command->os, Command->atomCommit));
        Command->atomCommit = gcvNULL;
    }

	/* Mark object as unknown. */
	Command->object.type = gcvOBJ_UNKNOWN;

	/* Free the gckCOMMAND object. */
	gcmkVERIFY_OK(gckOS_Free(Command->os, Command));

    Command = gcvNULL;

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
**	gckCOMMAND_Start
**
**	Start up the command queue.
**
**	INPUT:
**
**		gckCOMMAND Command
**			Pointer to an gckCOMMAND object to start.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gckCOMMAND_Start(
	IN gckCOMMAND Command
	)
{
    gckHARDWARE hardware;
	gceSTATUS status;
    gctSIZE_T bytes;

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

	if (Command->logical == gcvNULL)
	{
		/* Start at beginning of a new queue. */
        gcmkONERROR(_NewQueue(Command));
	}

	/* Start at beginning of page. */
	Command->offset = 0;

	/* Append WAIT/LINK. */
    bytes = Command->pageSize;
	gcmkONERROR(
		gckHARDWARE_WaitLink(hardware,
							 Command->logical,
							 0,
							 &bytes,
							 &Command->wait,
							 &Command->waitSize));

    /* Flush the cache for the wait/link. */
    gcmkONERROR(gckOS_CacheFlush(Command->os,
                                 gcvNULL,
                                 Command->logical,
                                 bytes));

    /* Adjust offset. */
    Command->offset   = bytes;
	Command->newQueue = gcvFALSE;

	/* Enable command processor. */
#ifdef __QNXNTO__
	gcmkONERROR(
		gckHARDWARE_Execute(hardware,
							Command->logical,
							Command->physical,
							gcvTRUE,
							bytes));
#else
	gcmkONERROR(
		gckHARDWARE_Execute(hardware,
							Command->logical,
							bytes));
#endif
	/* Command queue is running. */
	Command->running = gcvTRUE;

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
**	gckCOMMAND_Stop
**
**	Stop the command queue.
**
**	INPUT:
**
**		gckCOMMAND Command
**			Pointer to an gckCOMMAND object to stop.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gckCOMMAND_Stop(
	IN gckCOMMAND Command
	)
{
    gckHARDWARE hardware;
	gceSTATUS status;
    gctUINT32 idle;

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

    /* Replace last WAIT with END. */
    gcmkONERROR(
		gckHARDWARE_End(hardware,
						Command->wait,
						&Command->waitSize));

	/* Wait for idle. */

	while (1)
	{
		/* Read register. */
		gcmkONERROR(
			gckOS_ReadRegister(hardware->os, 0x00004, &idle));

		/* Wait for FE idle. */
		if (idle & 0x1)
		{        
			break;
		}

		/* Wait a little. */
		gcmkVERIFY_OK(gckOS_Udelay(hardware->os, 1));
	}
    
	/* Command queue is no longer running. */
	Command->running = gcvFALSE;

	/* Success. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_ARGS("status=%d, Command=0x%08x", status, Command);
	/* Return the status. */
	gcmkFOOTER();
	return status;
}

typedef struct _gcsMAPPED * gcsMAPPED_PTR;
struct _gcsMAPPED
{
	gcsMAPPED_PTR next;
	gctPOINTER pointer;
	gctPOINTER kernelPointer;
	gctSIZE_T bytes;
};

static gceSTATUS
_AddMap(
	IN gckOS Os,
	IN gctPOINTER Source,
	IN gctSIZE_T Bytes,
	OUT gctPOINTER * Destination,
	IN OUT gcsMAPPED_PTR * Stack
	)
{
	gcsMAPPED_PTR map = gcvNULL;
	gceSTATUS status;

	/* Don't try to map NULL pointers. */
	if (Source == gcvNULL)
	{
		*Destination = gcvNULL;
		return gcvSTATUS_OK;
	}

	/* Allocate the gcsMAPPED structure. */
	gcmkONERROR(
		gckOS_Allocate(Os, gcmSIZEOF(*map), (gctPOINTER *) &map));

	/* Map the user pointer into kernel addressing space. */
	gcmkONERROR(
		gckOS_MapUserPointer(Os, Source, Bytes, Destination));

	/* Save mapping. */
	map->pointer       = Source;
	map->kernelPointer = *Destination;
	map->bytes         = Bytes;

	/* Push structure on top of the stack. */
	map->next = *Stack;
	*Stack    = map;

	/* Success. */
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_ARGS("status=%d, map=0x%08x", status, map);
	if (gcmIS_ERROR(status) && (map != gcvNULL))
	{
		/* Roll back on error. */
		gcmkVERIFY_OK(gckOS_Free(Os, map));
        map = gcvNULL;
	}

	/* Return the status. */
	return status;
}
#define LATE_NOTIFY_BUSY 0
/*******************************************************************************
**
**	gckCOMMAND_Commit
**
**	Commit a command buffer to the command queue.
**
**	INPUT:
**
**		gckCOMMAND Command
**			Pointer to an gckCOMMAND object.
**
**		gcoCMDBUF CommandBuffer
**			Pointer to an gcoCMDBUF object.
**
**		gcoCONTEXT Context
**			Pointer to an gcoCONTEXT object.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gckCOMMAND_Commit(
    IN gckCOMMAND Command,
    IN gcoCMDBUF CommandBuffer,
    IN gcoCONTEXT Context,
    IN gctHANDLE Process
    )
{
    gcoCMDBUF commandBuffer;
    gcoCONTEXT context;
    gckHARDWARE hardware = gcvNULL;
	gceSTATUS status;
	gctPOINTER initialLink, link;
	gctSIZE_T bytes, initialSize, lastRun;
	gcoCMDBUF buffer;
	gctPOINTER wait;
	gctSIZE_T waitSize;
	gctUINT32 offset;
	gctPOINTER fetchAddress;
	gctSIZE_T fetchSize;
	gctUINT8_PTR logical;
	gcsMAPPED_PTR stack = gcvNULL;
	gctBOOL acquired = gcvFALSE;
#if gcdSECURE_USER
	gctUINT32_PTR hint;
#endif
#if gcdDUMP_COMMAND
    gctPOINTER dataPointer;
    gctSIZE_T dataBytes;
#endif
#if MRVL_PRINT_CMD_BUFFER
    gctPOINTER dumpLogical;
    gctSIZE_T dumpSize;
#endif
    gctPOINTER flushPointer;
    gctSIZE_T flushSize;

	gcmkHEADER_ARG("Command=0x%x CommandBuffer=0x%x Context=0x%x",
				   Command, CommandBuffer, Context);

	/* Verify the arguments. */
	gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

#if gcdNULL_DRIVER == 2
	/* Do nothing with infinite hardware. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;
#endif

	gcmkONERROR(
		_AddMap(Command->os,
				CommandBuffer,
				gcmSIZEOF(struct _gcoCMDBUF),
				(gctPOINTER *) &commandBuffer,
				&stack));
	gcmkVERIFY_OBJECT(commandBuffer, gcvOBJ_COMMANDBUFFER);
	gcmkONERROR(
		_AddMap(Command->os,
				Context,
				gcmSIZEOF(struct _gcoCONTEXT),
				(gctPOINTER *) &context,
				&stack));
	gcmkVERIFY_OBJECT(context, gcvOBJ_CONTEXT);

	/* Extract the gckHARDWARE and gckEVENT objects. */
	hardware = Command->kernel->hardware;
	gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

	/* Grab the mutex. */
	gcmkONERROR(
		gckOS_AcquireRecMutex(Command->os,
						   hardware->recMutexPower,
						   gcvINFINITE));
	acquired = gcvTRUE;

#if !LATE_NOTIFY_BUSY
    gcmkONERROR(gckHARDWARE_SetPowerManagementState(hardware, gcvPOWER_ON));

	if (Command->kernel->notifyIdle)
	{
		/* Increase the commit stamp */
		Command->commitStamp++;

		/* Set busy if idle */
		if (Command->idle)
		{
			Command->idle = gcvFALSE;

			gcmkVERIFY_OK(gckOS_NotifyIdle(Command->os, gcvFALSE));
		}
	}
#endif

	/* Reserved slot in the context or command buffer. */
	gcmkONERROR(
		gckHARDWARE_PipeSelect(hardware, gcvNULL, 0, &bytes));

	/* Test if we need to switch to this context. */
	if ((context->id != 0)
	&&  (context->id != Command->currentContext)
	)
	{
		/* Map the context buffer.*/
		gcmkONERROR(
			_AddMap(Command->os,
					context->logical,
					context->bufferSize,
					(gctPOINTER *) &logical,
					&stack));

#if gcdSECURE_USER
		/* Map the hint array.*/
		gcmkONERROR(
			_AddMap(Command->os,
					context->hintArray,
					context->hintCount * gcmSIZEOF(gctUINT32),
					(gctPOINTER *) &hint,
					&stack));

        /* Loop while we have valid hints. */
        while (*hint != 0)
        {
            /* Map handle into physical address. */
            gcmkONERROR(
                gckKERNEL_MapLogicalToPhysical(
                    Command->kernel,
                    Process,
                    (gctPOINTER *) (logical + *hint)));

			/* Next hint. */
			++hint;
		}
#endif

		/* See if we have to check pipes. */
		if (context->pipe2DIndex != 0)
		{
			/* See if we are in the correct pipe. */
			if (context->initialPipe == Command->pipeSelect)
			{
				gctUINT32 reserved = bytes;
				gctUINT8_PTR nop   = logical;

				/* Already in the correct pipe, fill context buffer with NOP. */
				while (reserved > 0)
				{
					bytes = reserved;
					gcmkONERROR(
						gckHARDWARE_Nop(hardware, nop, &bytes));

					gcmkASSERT(reserved >= bytes);
					reserved -= bytes;
					nop      += bytes;
				}
			}
			else
			{
				/* Switch to the correct pipe. */
				gcmkONERROR(
					gckHARDWARE_PipeSelect(hardware,
										   logical,
										   context->initialPipe,
										   &bytes));
			}
		}

		/* Save initial link pointer. */
        initialLink = logical;
		initialSize = context->bufferSize;

        /* Save initial buffer to flush. */
        flushPointer = initialLink;
        flushSize    = initialSize;

        /* Save pointer to next link. */
        gcmkONERROR(
            _AddMap(Command->os,
                    context->link,
                    8,
                    &link,
                    &stack));

		/* Start parsing CommandBuffer. */
		buffer = commandBuffer;

		/* Mark context buffer as used. */
		if (context->inUse != gcvNULL)
		{
			gctBOOL_PTR inUse;

			gcmkONERROR(
				_AddMap(Command->os,
						(gctPOINTER) context->inUse,
						gcmSIZEOF(gctBOOL),
						(gctPOINTER *) &inUse,
						&stack));

			*inUse = gcvTRUE;
		}
	}

	else
	{
		/* Test if this is a new context. */
		if (context->id == 0)
		{
			/* Generate unique ID for the context buffer. */
			context->id = ++ Command->contextCounter;

			if (context->id == 0)
			{
				/* Context counter overflow (wow!) */
				gcmkONERROR(gcvSTATUS_TOO_COMPLEX);
			}
		}

		/* Map the command buffer. */
		gcmkONERROR(
			_AddMap(Command->os,
					commandBuffer->logical,
					commandBuffer->offset,
					(gctPOINTER *) &logical,
					&stack));

#if gcdSECURE_USER
		/* Map the hint table. */
		gcmkONERROR(
			_AddMap(Command->os,
					commandBuffer->hintCommit,
					commandBuffer->offset - commandBuffer->startOffset,
					(gctPOINTER *) &hint,
					&stack));

        /* Walk while we have valid hints. */
        while (*hint != 0)
        {
            /* Map the handle to a physical address. */
            gcmkONERROR(
                gckKERNEL_MapLogicalToPhysical(
                    Command->kernel,
                    Process,
                    (gctPOINTER *) (logical + *hint)));

			/* Next hint. */
			++hint;
		}
#endif

		if (context->entryPipe == Command->pipeSelect)
		{
			gctUINT32 reserved = Command->reservedHead;
			gctUINT8_PTR nop   = logical + commandBuffer->startOffset;

			/* Already in the correct pipe, fill context buffer with NOP. */
			while (reserved > 0)
			{
				bytes = reserved;
				gcmkONERROR(
					gckHARDWARE_Nop(hardware, nop, &bytes));

				gcmkASSERT(reserved >= bytes);
				reserved -= bytes;
				nop      += bytes;
			}
		}
		else
		{
			/* Switch to the correct pipe. */
			gcmkONERROR(
				gckHARDWARE_PipeSelect(hardware,
									   logical + commandBuffer->startOffset,
									   context->entryPipe,
									   &bytes));
		}

		/* Save initial link pointer. */
        initialLink = logical + commandBuffer->startOffset;
		initialSize = commandBuffer->offset
					- commandBuffer->startOffset
					+ Command->reservedTail;

        /* Save initial buffer to flush. */
        flushPointer = initialLink;
        flushSize    = initialSize;

        /* Save pointer to next link. */
        link = logical + commandBuffer->offset;

		/* No more data. */
		buffer = gcvNULL;
	}

#if MRVL_PRINT_CMD_BUFFER
	_AddLink(Command, Command->wait, initialLink);
#endif

#if gcdDUMP_COMMAND
    dataPointer = initialLink;
    dataBytes   = initialSize;
#endif

#if MRVL_PRINT_CMD_BUFFER
    dumpLogical  = initialLink;
    dumpSize     = initialSize;
#endif

	/* Loop through all remaining command buffers. */
	if (buffer != gcvNULL)
	{
		/* Map the command buffer. */
		gcmkONERROR(
			_AddMap(Command->os,
					buffer->logical,
					buffer->offset + Command->reservedTail,
					(gctPOINTER *) &logical,
					&stack));

#if gcdSECURE_USER
		/* Map the hint table. */
		gcmkONERROR(
			_AddMap(Command->os,
					buffer->hintCommit,
					buffer->offset - buffer->startOffset,
					(gctPOINTER *) &hint,
					&stack));

        /* Walk while we have valid hints. */
        while (*hint != 0)
        {
            /* Map the handle to a physical address. */
            gcmkONERROR(
                gckKERNEL_MapLogicalToPhysical(
                    Command->kernel,
                    Process,
                    (gctPOINTER *) (logical + *hint)));

			/* Next hint. */
			++hint;
		}
#endif

		/* First slot becomes a NOP. */
		{
			gctUINT32 reserved = Command->reservedHead;
			gctUINT8_PTR nop   = logical + buffer->startOffset;

			/* Already in the correct pipe, fill context buffer with NOP. */
			while (reserved > 0)
			{
				bytes = reserved;
				gcmkONERROR(
					gckHARDWARE_Nop(hardware, nop, &bytes));

				gcmkASSERT(reserved >= bytes);
				reserved -= bytes;
				nop      += bytes;
			}
		}

		/* Generate the LINK to this command buffer. */
		gcmkONERROR(
			gckHARDWARE_Link(hardware,
							 link,
                             logical + buffer->startOffset,
							 buffer->offset
							 - buffer->startOffset
							 + Command->reservedTail,
							 &bytes));
#if MRVL_PRINT_CMD_BUFFER
	    _AddLink(Command, link, (gctUINT32_PTR)logical);
#endif

        /* Flush the initial buffer. */
        gcmkONERROR(gckOS_CacheFlush(Command->os,
                                     Process,
                                     flushPointer,
                                     flushSize));

        /* Save new flush pointer. */
        flushPointer = logical + buffer->startOffset;
        flushSize    = buffer->offset
                     - buffer->startOffset
                     + Command->reservedTail;

#if gcdDUMP_COMMAND
        _DumpCommand(Command, dataPointer, dataBytes);
        dataPointer = logical + buffer->startOffset;
        dataBytes   = buffer->offset - buffer->startOffset
                    + Command->reservedTail;
#endif

#if MRVL_PRINT_CMD_BUFFER
		_AddCmdBuffer(
			Command, dumpLogical, dumpSize, gcvDUMP_CMD);
        dumpLogical  = logical + buffer->startOffset;
        dumpSize     = buffer->offset - buffer->startOffset
                    + Command->reservedTail;
#endif

		/* Save pointer to next link. */
        link = logical + buffer->offset;
	}

	/* Compute number of bytes required for WAIT/LINK. */
	gcmkONERROR(
		gckHARDWARE_WaitLink(hardware,
							 gcvNULL,
							 Command->offset,
							 &bytes,
							 gcvNULL,
							 gcvNULL));

	lastRun = bytes;

#if LATE_NOTIFY_BUSY
    gcmkONERROR(gckHARDWARE_SetPowerManagementState(hardware, gcvPOWER_ON));

	if (Command->kernel->notifyIdle)
	{
		/* Increase the commit stamp */
		Command->commitStamp++;

		/* Set busy if idle */
		if (Command->idle)
		{
			Command->idle = gcvFALSE;

			gcmkVERIFY_OK(gckOS_NotifyIdle(Command->os, gcvFALSE));
		}
	}
#endif

	/* Compute number of bytes left in current command queue. */
	bytes = Command->pageSize - Command->offset;

	if (bytes < lastRun)
	{
        /* Create a new command queue. */
        gcmkONERROR(_NewQueue(Command));

		/* Adjust run size with any extra commands inserted. */
		lastRun += Command->offset;
	}

	/* Get current offset. */
	offset = Command->offset;

	/* Append WAIT/LINK in command queue. */
	bytes = Command->pageSize - offset;

	gcmkONERROR(
		gckHARDWARE_WaitLink(hardware,
							 (gctUINT8 *) Command->logical + offset,
							 offset,
							 &bytes,
							 &wait,
							 &waitSize));

    /* Flush the cache for the wait/link. */
    gcmkONERROR(gckOS_CacheFlush(Command->os,
                                 gcvNULL,
                                 (gctUINT8 *) Command->logical + offset,
                                 bytes));

#if gcdDUMP_COMMAND
    _DumpCommand(Command, (gctUINT8 *) Command->logical + offset, bytes);
#endif

#if MRVL_PRINT_CMD_BUFFER
	_AddCmdBuffer(Command, (gctUINT8 *) Command->logical + offset, bytes, gcvDUMP_CMD);
#endif

	/* Adjust offset. */
	offset += bytes;

	if (Command->newQueue)
	{
		/* Compute fetch location and size for a new command queue. */
		fetchAddress = Command->logical;
		fetchSize    = offset;
	}
	else
	{
		/* Compute fetch location and size for an existing command queue. */
		fetchAddress = (gctUINT8 *) Command->logical + Command->offset;
		fetchSize    = offset - Command->offset;
	}

	bytes = 8;

	/* Link in WAIT/LINK. */
	gcmkONERROR(
		gckHARDWARE_Link(hardware,
						 link,
						 fetchAddress,
						 fetchSize,
						 &bytes));
#if MRVL_PRINT_CMD_BUFFER
	_AddLink(Command, link, fetchAddress);
#endif

    /* Flush the cache for the command buffer. */
    gcmkONERROR(gckOS_CacheFlush(Command->os,
                                 Process,
                                 flushPointer,
                                 flushSize));

#if gcdDUMP_COMMAND
    _DumpCommand(Command, dataPointer, dataBytes);
#endif

#if MRVL_PRINT_CMD_BUFFER
	_AddCmdBuffer(Command, dumpLogical, dumpSize, gcvDUMP_CMD);
#endif

	/* Execute the entire sequence. */
	gcmkONERROR(
		gckHARDWARE_Link(hardware,
						 Command->wait,
						 initialLink,
						 initialSize,
						 &Command->waitSize));

    /* Flush the cache for the link. */
    gcmkONERROR(gckOS_CacheFlush(Command->os,
                                 gcvNULL,
                                 Command->wait,
                                 Command->waitSize));

#if gcdDUMP_COMMAND
    _DumpCommand(Command, Command->wait, Command->waitSize);
#endif

#if MRVL_PRINT_CMD_BUFFER
	_AddCmdBuffer(Command, Command->wait, Command->waitSize, gcvDUMP_CMD);
#endif

	/* Update command queue offset. */
	Command->offset   = offset;
	Command->newQueue = gcvFALSE;

	/* Update address of last WAIT. */
	Command->wait     = wait;
	Command->waitSize = waitSize;

	/* Update context and pipe select. */
	Command->currentContext = context->id;
	Command->pipeSelect     = context->currentPipe;

	/* Update queue tail pointer. */
	gcmkONERROR(
		gckHARDWARE_UpdateQueueTail(hardware,
									Command->logical,
									Command->offset));

#if gcdDUMP_COMMAND
    gcmkPRINT("@[kernel.commit]");
#endif

    /* Release the mutex. */
    gcmkONERROR(gckOS_ReleaseRecMutex(Command->os, hardware->recMutexPower));
    acquired = gcvFALSE;

    /* Submit events if asked for. */
    if (Command->submit)
    {
        /* Submit events. */
        status = gckEVENT_Submit(Command->kernel->event, gcvFALSE);

        if (gcmIS_SUCCESS(status))
        {
            /* Success. */
            Command->submit = gcvFALSE;
        }
        else
        {
            gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_COMMAND,
                           "gckEVENT_Submit returned %d",
                           status);
        }
    }

    /* Success. */
    status = gcvSTATUS_OK;

OnError:
    if (!gcmIS_SUCCESS(status))
    {
        gcmkLOG_ERROR_ARGS("status=%d, acquired=%d",
                            status, acquired);
    }
    
	if (acquired)
	{
		/* Release the mutex. */
		gcmkVERIFY_OK(
			gckOS_ReleaseRecMutex(Command->os, hardware->recMutexPower));
	}

	/* Unmap all mapped pointers. */
	while (stack != gcvNULL)
	{
		gcsMAPPED_PTR map = stack;
		stack             = map->next;

		gcmkVERIFY_OK(
			gckOS_UnmapUserPointer(Command->os,
								   map->pointer,
								   map->bytes,
								   map->kernelPointer));

		gcmkVERIFY_OK(
			gckOS_Free(Command->os, map));
	}

	/* Return status. */
	gcmkFOOTER();
	return status;
}

/*******************************************************************************
**
**	gckCOMMAND_Reserve
**
**	Reserve space in the command queue.  Also acquire the command queue mutex.
**
**	INPUT:
**
**		gckCOMMAND Command
**			Pointer to an gckCOMMAND object.
**
**		gctSIZE_T RequestedBytes
**			Number of bytes previously reserved.
**
**	OUTPUT:
**
**		gctPOINTER * Buffer
**          Pointer to a variable that will receive the address of the reserved
**          space.
**
**      gctSIZE_T * BufferSize
**          Pointer to a variable that will receive the number of bytes
**          available in the command queue.
*/
gceSTATUS
gckCOMMAND_Reserve(
    IN gckCOMMAND Command,
    IN gctSIZE_T RequestedBytes,
    OUT gctPOINTER * Buffer,
    OUT gctSIZE_T * BufferSize
    )
{
    gceSTATUS status;
    gctSIZE_T requiredBytes, bytes;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Command=0x%x RequestedBytes=%lu", Command, RequestedBytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);
    
    /* Grab the mutex. */
    gcmkONERROR(
        gckOS_AcquireRecMutex(Command->os,
                           Command->kernel->hardware->recMutexPower,
                           gcvINFINITE));
    acquired = gcvTRUE;

#if !LATE_NOTIFY_BUSY
    gcmkONERROR(gckHARDWARE_SetPowerManagementState(Command->kernel->hardware, gcvPOWER_ON)); 

    if (Command->kernel->notifyIdle)
	{
		/* Increase the commit stamp */
		Command->commitStamp++;

		/* Set busy if idle */
		if (Command->idle)
		{
			Command->idle = gcvFALSE;

			gcmkVERIFY_OK(gckOS_NotifyIdle(Command->os, gcvFALSE));
		}
	}
#endif
    
	/* Compute number of bytes required for WAIT/LINK. */
	gcmkONERROR(
		gckHARDWARE_WaitLink(Command->kernel->hardware,
							 gcvNULL,
							 Command->offset + gcmALIGN(RequestedBytes,
														Command->alignment),
							 &requiredBytes,
							 gcvNULL,
							 gcvNULL));

	/* Compute total number of bytes required. */
	requiredBytes += gcmALIGN(RequestedBytes, Command->alignment);

	/* Compute number of bytes available in command queue. */
	bytes = Command->pageSize - Command->offset;

	if (bytes < requiredBytes)
	{
        /* Create a new command queue. */
        gcmkONERROR(_NewQueue(Command));

		/* Recompute number of bytes available in command queue. */
		bytes = Command->pageSize - Command->offset;

		if (bytes < requiredBytes)
		{
			/* Rare case, not enough room in command queue. */
			gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
		}
	}

    /* Fill hole in command queue with value NOP */
    {
        gctSIZE_T alignBytes = gcmALIGN(RequestedBytes, Command->alignment) - RequestedBytes;

        if (alignBytes > 0)
        {
            gctSIZE_T reserved = alignBytes;
            gctUINT8_PTR nop   = (gctUINT8_PTR) Command->logical + Command->offset + RequestedBytes;

            while (reserved > 0)
            {
                gctSIZE_T size = sizeof(gctUINT32);
                gctUINT32_PTR ptr = (gctUINT32_PTR) nop;

                if (reserved < size)
                    break;

                *ptr = GC_NOP_COMMAND;
                reserved -= size;
                nop      += size;
            }
        }
    }

	/* Return pointer to empty slot command queue. */
	*Buffer = (gctUINT8 *) Command->logical + Command->offset;

	/* Return number of bytes left in command queue. */
	*BufferSize = bytes;

	/* Success. */
	gcmkFOOTER_ARG("*Buffer=0x%x *BufferSize=%lu", *Buffer, *BufferSize);
	return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_ARGS("status=%d, acquired=%d, requiredBytes=%d",
                        status, acquired, requiredBytes);


    if (acquired)
    {
        /* Release mutex on error. */
        gcmkVERIFY_OK(
        	gckOS_ReleaseRecMutex(Command->os, Command->kernel->hardware->recMutexPower));
    }

    /* Return status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**	gckCOMMAND_Release
**
**	Release a previously reserved command queue.  The command FIFO mutex will be
**  released.
**
**	INPUT:
**
**		gckCOMMAND Command
**			Pointer to an gckCOMMAND object.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gckCOMMAND_Release(
    IN gckCOMMAND Command
    )
{
	gceSTATUS status;

	gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

    /* Release the mutex. */
    gcmkONERROR(gckOS_ReleaseRecMutex(Command->os, Command->kernel->hardware->recMutexPower));

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
**	gckCOMMAND_Execute
**
**	Execute a previously reserved command queue by appending a WAIT/LINK command
**  sequence after it and modifying the last WAIT into a LINK command.  The
**  command FIFO mutex will be released whether this function succeeds or not.
**
**	INPUT:
**
**		gckCOMMAND Command
**			Pointer to an gckCOMMAND object.
**
**		gctSIZE_T RequestedBytes
**			Number of bytes previously reserved.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gckCOMMAND_Execute(
    IN gckCOMMAND Command,
    IN gctSIZE_T RequestedBytes
    )
{
    gctUINT32 offset;
    gctPOINTER address;
    gctSIZE_T bytes;
    gceSTATUS status;
    gctPOINTER wait;
    gctSIZE_T waitBytes;

    gctBOOL queueReleased = gcvFALSE;

    gcmkHEADER_ARG("Command=0x%x RequestedBytes=%lu", Command, RequestedBytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

#if LATE_NOTIFY_BUSY
    gcmkONERROR(gckHARDWARE_SetPowerManagementState(Command->kernel->hardware, gcvPOWER_ON)); 

	if (Command->kernel->notifyIdle)
	{
		/* Increase the commit stamp */
		Command->commitStamp++;

		/* Set busy if idle */
		if (Command->idle)
		{
			Command->idle = gcvFALSE;

			gcmkVERIFY_OK(gckOS_NotifyIdle(Command->os, gcvFALSE));
		}
	}
#endif

	/* Compute offset for WAIT/LINK. */
	offset = Command->offset + RequestedBytes;

	/* Compute number of byts left in command queue. */
	bytes = Command->pageSize - offset;

	/* Append WAIT/LINK in command queue. */
	gcmkONERROR(
		gckHARDWARE_WaitLink(Command->kernel->hardware,
							 (gctUINT8 *) Command->logical + offset,
							 offset,
							 &bytes,
							 &wait,
							 &waitBytes));

	if (Command->newQueue)
	{
		/* For a new command queue, point to the start of the command
		** queue and include both the commands inserted at the head of it
		** and the WAIT/LINK. */
		address = Command->logical;
		bytes  += offset;
	}
	else
	{
		/* For an existing command queue, point to the current offset and
		** include the WAIT/LINK. */
		address = (gctUINT8 *) Command->logical + Command->offset;
		bytes  += RequestedBytes;
	}

    /* Flush the cache. */
    gcmkONERROR(gckOS_CacheFlush(Command->os, gcvNULL, address, bytes));

#if gcdDUMP_COMMAND
    _DumpCommand(Command, address, bytes);
#endif

#if MRVL_PRINT_CMD_BUFFER
	_AddCmdBuffer(Command, address, bytes, gcvDUMP_EVENT);
#endif

    /* Convert the last WAIT into a LINK. */
    gcmkONERROR(gckHARDWARE_Link(Command->kernel->hardware,
                                 Command->wait,
                                 address,
                                 bytes,
                                 &Command->waitSize));
#if MRVL_PRINT_CMD_BUFFER
	_AddLink(Command, Command->wait, address);
#endif

    /* Flush the cache. */
    gcmkONERROR(gckOS_CacheFlush(Command->os,
                                 gcvNULL,
                                 Command->wait,
                                 Command->waitSize));

#if gcdDUMP_COMMAND
    _DumpCommand(Command, Command->wait, 8);
#endif

#if MRVL_PRINT_CMD_BUFFER
	_AddCmdBuffer(Command, Command->wait, 8, gcvDUMP_EVENT);
#endif

	/* Update the pointer to the last WAIT. */
	Command->wait     = wait;
	Command->waitSize = waitBytes;

	/* Update the command queue. */
	Command->offset  += bytes;
	Command->newQueue = gcvFALSE;

	/* Update queue tail pointer. */
	gcmkONERROR(
		gckHARDWARE_UpdateQueueTail(Command->kernel->hardware,
									Command->logical,
									Command->offset));

#if gcdDUMP_COMMAND
    gcmkPRINT("@[kernel.execute]");
#endif

    /* Release the mutex. */
    gcmkONERROR(gckOS_ReleaseRecMutex(Command->os, Command->kernel->hardware->recMutexPower));
    queueReleased = gcvTRUE;
    
    /* Submit events if asked for. */
    if (Command->submit)
    {
        /* Submit events. */
        status = gckEVENT_Submit(Command->kernel->event, gcvFALSE);

        if (gcmIS_SUCCESS(status))
        {
            /* Success. */
            Command->submit = gcvFALSE;
        }
        else
        {
            gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_COMMAND,
                           "gckEVENT_Submit returned %d",
                           status);
        }
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkLOG_ERROR_ARGS("status=%d, queueReleased=%d",
                        status, queueReleased);

    /* Release the mutex. */
    if (!queueReleased)
    {
        gcmkVERIFY_OK(
            gckOS_ReleaseRecMutex(Command->os, Command->kernel->hardware->recMutexPower));
    }
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**	gckCOMMAND_Stall
**
**	The calling thread will be suspended until the command queue has been
**  completed.
**
**	INPUT:
**
**		gckCOMMAND Command
**			Pointer to an gckCOMMAND object.
**
**	OUTPUT:
**
**		Nothing.
*/
gceSTATUS
gckCOMMAND_Stall(
    IN gckCOMMAND Command
    )
{
    gckOS os;
    gckHARDWARE hardware;
    gckEVENT event;
    gceSTATUS status;
	gctSIGNAL signal = gcvNULL;
#if VIVANTE_POWER_MANAGE
    gctUINT timer = 0;
#endif
	gcmkHEADER_ARG("Command=0x%x", Command);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Command, gcvOBJ_COMMAND);

#if gcdNULL_DRIVER == 2
	/* Do nothing with infinite hardware. */
	gcmkFOOTER_NO();
	return gcvSTATUS_OK;
#endif

    /* Extract the gckOS object pointer. */
    os = Command->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Extract the gckHARDWARE object pointer. */
    hardware = Command->kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    /* Extract the gckEVENT object pointer. */
    event = Command->kernel->event;
    gcmkVERIFY_OBJECT(event, gcvOBJ_EVENT);

    /* Allocate the signal. */
	gcmkONERROR(
		gckOS_CreateSignal(os, gcvTRUE, &signal));

    /* Append the EVENT command to trigger the signal. */
    gcmkONERROR(gckEVENT_Signal(event,
                                signal,
                                gcvKERNEL_PIXEL));

    /* Submit the event queue. */
    gcmkONERROR(gckEVENT_Submit(event, gcvTRUE));

#if gcdDUMP_COMMAND
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
		status = gckOS_WaitSignalNoInterruptible(os, signal, gcvINFINITE);

		if (status == gcvSTATUS_TIMEOUT)
		{
#if gcdDEBUG
			gctUINT32 idle;

			/* IDLE */
			gcmkONERROR(gckOS_ReadRegister(Command->os, 0x0004, &idle));
                
			gcmkTRACE(gcvLEVEL_ERROR,
					  "%s(%d): idle=%08x",
					  __FUNCTION__, __LINE__, idle);
        	    	gckOS_Log(_GFX_LOG_WARNING_, "%s : %d : idle register = 0x%08x \n", 
                            __FUNCTION__, __LINE__, idle);                
#endif 

#if MRVL_LOW_POWER_MODE_DEBUG
            	{
                	int i = 0;
                
                	gcmkPRINT(">>>>>>>>>>>>galDevice->kernel->kernelMSG\n");
                	gcmkPRINT("galDevice->kernel->msgLen=%d\n",Command->kernel->msgLen);
                
                	for(i=0;i<Command->kernel->msgLen;i+=1024)
                	{
                    		Command->kernel->kernelMSG[i+1023] = '\0';
            	    		gcmkPRINT("%s\n",(char*)Command->kernel->kernelMSG + i);
                	}
            	}
#endif
#ifdef __QNXNTO__
            gctUINT32 reg_cmdbuf_fetch;
            gctUINT32 reg_intr;

            gcmkONERROR(
                    gckOS_ReadRegister(Command->kernel->hardware->os, 0x0664, &reg_cmdbuf_fetch));

            if (idle == 0x7FFFFFFE)
            {
                /*
                 * GPU is idle so there should not be pending interrupts.
                 * Just double check.
                 *
                 * Note that reading interrupt register clears it.
                 * That's why we don't read it in all cases.
                 */
                gcmkONERROR(
                        gckOS_ReadRegister(Command->kernel->hardware->os, 0x10, &reg_intr));

                slogf(
                    _SLOG_SETCODE(1, 0),
                    _SLOG_CRITICAL,
                    "GALcore: Stall timeout (idle = 0x%X, command buffer fetch = 0x%X, interrupt = 0x%X)",
                    idle, reg_cmdbuf_fetch, reg_intr);
            }
            else
            {
                slogf(
                    _SLOG_SETCODE(1, 0),
                    _SLOG_CRITICAL,
                    "GALcore: Stall timeout (idle = 0x%X, command buffer fetch = 0x%X)",
                    idle, reg_cmdbuf_fetch);
            }
#endif
			gcmkONERROR(
				gckOS_MemoryBarrier(os, gcvNULL));
#if VIVANTE_POWER_MANAGE
            /* Advance timer. */
            timer += 250;
#endif
		}
    }
    
#if !VIVANTE_POWER_MANAGE
    while (gcmIS_ERROR(status));
#else
    while (gcmIS_ERROR(status)
#if gcdGPU_TIMEOUT
           && (timer < gcdGPU_TIMEOUT)
#endif
           );
    /* Bail out on timeout. */
    if (gcmIS_ERROR(status))
    {
        /* Broadcast the stuck GPU. */
        gcmkONERROR(gckOS_Broadcast(os,
                                    Command->kernel->hardware,
                                    gcvBROADCAST_GPU_STUCK));

        gcmkONERROR(gcvSTATUS_GPU_NOT_RESPONDING);
	}
#endif

	/* Delete the signal. */
	gcmkONERROR(gckOS_DestroySignal(os, signal));
    signal = gcvNULL;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
#if VIVANTE_POWER_MANAGE
    gcmkLOG_ERROR_ARGS("status=%d, signal=0x%08x, timer=%d", status, signal, timer);
#else
    gcmkLOG_ERROR_ARGS("status=%d, signal=0x%08x", status, signal);
#endif
    /* Free the signal. */
    if (signal != gcvNULL)
    {
    	gcmkVERIFY_OK(gckOS_DestroySignal(os, signal));

        signal = gcvNULL;
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

