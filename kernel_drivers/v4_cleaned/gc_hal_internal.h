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

#ifndef __gc_hal_internal_h_
#define __gc_hal_internal_h_

#include "gc_hal.h"
#include "gc_hal_base_internal.h"

/******************************************************************************\
******************************* Alignment Macros *******************************
\******************************************************************************/

#define gcmALIGN(n, align) \
( \
    ((n) + ((align) - 1)) & ~((align) - 1) \
)

#define gcmALIGN_BASE(n, align) \
( \
    (n) & ~((align) - 1) \
)

/******************************************************************************\
***************************** Element Count Macro *****************************
\******************************************************************************/

typedef struct _gckHARDWARE *       gckHARDWARE;

/* CORE flags. */
typedef enum _gceCORE
{
    gcvCORE_MAJOR       = 0x0,
    gcvCORE_2D          = 0x1,
}
gceCORE;

#define gcdCORE_COUNT               2

/*******************************************************************************
**
**  gcmVERIFY_OBJECT
**
**      Assert if an object is invalid or is not of the specified type.  If the
**      object is invalid or not of the specified type, gcvSTATUS_INVALID_OBJECT
**      will be returned from the current function.  In retail mode this macro
**      does nothing.
**
**  ARGUMENTS:
**
**      obj     Object to test.
**      t       Expected type of the object.
*/
#if gcmIS_DEBUG(gcdDEBUG_TRACE)
#define _gcmVERIFY_OBJECT(prefix, obj, t) \
    if ((obj) == NULL) \
    { \
        prefix##TRACE(gcvLEVEL_ERROR, \
                      #prefix "VERIFY_OBJECT failed: NULL"); \
        prefix##TRACE(gcvLEVEL_ERROR, "  expected: %c%c%c%c", \
                      gcmCC_PRINT(t)); \
        prefix##ASSERT((obj) != NULL); \
        prefix##FOOTER_ARG("status=%d", gcvSTATUS_INVALID_OBJECT); \
        return gcvSTATUS_INVALID_OBJECT; \
    } \
    else if (((gcsOBJECT*) (obj))->type != t) \
    { \
        prefix##TRACE(gcvLEVEL_ERROR, \
                      #prefix "VERIFY_OBJECT failed: %c%c%c%c", \
                      gcmCC_PRINT(((gcsOBJECT*) (obj))->type)); \
        prefix##TRACE(gcvLEVEL_ERROR, "  expected: %c%c%c%c", \
                      gcmCC_PRINT(t)); \
        prefix##ASSERT(((gcsOBJECT*)(obj))->type == t); \
        prefix##FOOTER_ARG("status=%d", gcvSTATUS_INVALID_OBJECT); \
        return gcvSTATUS_INVALID_OBJECT; \
    }

#   define gcmVERIFY_OBJECT(obj, t)     _gcmVERIFY_OBJECT(gcm, obj, t)
#   define gcmkVERIFY_OBJECT(obj, t)    _gcmVERIFY_OBJECT(gcmk, obj, t)
#else
#   define gcmVERIFY_OBJECT(obj, t)     do {} while (gcvFALSE)
#   define gcmkVERIFY_OBJECT(obj, t)    do {} while (gcvFALSE)
#endif

/******************************************************************************\
********************************** gckOS Object *********************************
\******************************************************************************/

/* Construct a new gckOS object. */
gceSTATUS
gckOS_Construct(
    IN void *Context,
    OUT gckOS * Os
    );

/* Destroy an gckOS object. */
gceSTATUS
gckOS_Destroy(
    IN gckOS Os
    );

/* Allocate memory from the heap. */
gceSTATUS
gckOS_Allocate(
    IN gckOS Os,
    IN size_t Bytes,
    OUT void **Memory
    );

/* Free allocated memory. */
gceSTATUS
gckOS_Free(
    IN gckOS Os,
    IN void *Memory
    );

/* Wrapper for allocation memory.. */
gceSTATUS
gckOS_AllocateMemory(
    IN gckOS Os,
    IN size_t Bytes,
    OUT void **Memory
    );

/* Wrapper for freeing memory. */
gceSTATUS
gckOS_FreeMemory(
    IN gckOS Os,
    IN void *Memory
    );

/* Allocate paged memory. */
gceSTATUS
gckOS_AllocatePagedMemoryEx(
    IN gckOS Os,
    IN int Contiguous,
    IN size_t Bytes,
    OUT gctPHYS_ADDR * Physical
    );

/* Lock pages. */
gceSTATUS
gckOS_LockPages(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN size_t Bytes,
    IN int Cacheable,
    OUT void **Logical,
    OUT size_t * PageCount
    );

/* Map pages. */
gceSTATUS
gckOS_MapPagesEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctPHYS_ADDR Physical,
    IN size_t PageCount,
    IN void *PageTable
    );

/* Unlock pages. */
gceSTATUS
gckOS_UnlockPages(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN size_t Bytes,
    IN void *Logical
    );

/* Free paged memory. */
gceSTATUS
gckOS_FreePagedMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN size_t Bytes
    );

/* Allocate non-paged memory. */
gceSTATUS
gckOS_AllocateNonPagedMemory(
    IN gckOS Os,
    IN int InUserSpace,
    IN OUT size_t * Bytes,
    OUT gctPHYS_ADDR * Physical,
    OUT void **Logical
    );

/* Free non-paged memory. */
gceSTATUS
gckOS_FreeNonPagedMemory(
    IN gckOS Os,
    IN size_t Bytes,
    IN gctPHYS_ADDR Physical,
    IN void *Logical
    );

/* Allocate contiguous memory. */
gceSTATUS
gckOS_AllocateContiguous(
    IN gckOS Os,
    IN int InUserSpace,
    IN OUT size_t * Bytes,
    OUT gctPHYS_ADDR * Physical,
    OUT void **Logical
    );

/* Free contiguous memory. */
gceSTATUS
gckOS_FreeContiguous(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN void *Logical,
    IN size_t Bytes
    );

/* Get the physical address of a corresponding logical address. */
gceSTATUS
gckOS_GetPhysicalAddress(
    IN gckOS Os,
    IN void *Logical,
    OUT u32 * Address
    );

/* Map physical memory. */
gceSTATUS
gckOS_MapPhysical(
    IN gckOS Os,
    IN u32 Physical,
    IN size_t Bytes,
    OUT void **Logical
    );

/* Unmap previously mapped physical memory. */
gceSTATUS
gckOS_UnmapPhysical(
    IN gckOS Os,
    IN void *Logical,
    IN size_t Bytes
    );

/* Read data from a hardware register. */
gceSTATUS
gckOS_ReadRegisterEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN u32 Address,
    OUT u32 * Data
    );

/* Write data to a hardware register. */
gceSTATUS
gckOS_WriteRegisterEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN u32 Address,
    IN u32 Data
    );

/* Write data to a 32-bit memory location. */
gceSTATUS
gckOS_WriteMemory(
    IN gckOS Os,
    IN void *Address,
    IN u32 Data
    );

/* Map physical memory into the process space. */
gceSTATUS
gckOS_MapMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN size_t Bytes,
    OUT void **Logical
    );

/* Unmap physical memory from the specified process space. */
gceSTATUS
gckOS_UnmapMemoryEx(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN size_t Bytes,
    IN void *Logical,
    IN u32 PID
    );

/* Unmap physical memory from the process space. */
gceSTATUS
gckOS_UnmapMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN size_t Bytes,
    IN void *Logical
    );

/* Create a new mutex. */
gceSTATUS
gckOS_CreateMutex(
    IN gckOS Os,
    OUT void **Mutex
    );

/* Delete a mutex. */
gceSTATUS
gckOS_DeleteMutex(
    IN gckOS Os,
    IN void *Mutex
    );

/* Acquire a mutex. */
gceSTATUS
gckOS_AcquireMutex(
    IN gckOS Os,
    IN void *Mutex,
    IN u32 Timeout
    );

/* Release a mutex. */
gceSTATUS
gckOS_ReleaseMutex(
    IN gckOS Os,
    IN void *Mutex
    );

/* Atomically exchange a pair of 32-bit values. */
gceSTATUS
gckOS_AtomicExchange(
    IN gckOS Os,
    IN OUT u32 *Target,
    IN u32 NewValue,
    OUT u32 *OldValue
    );

#ifdef CONFIG_SMP
gceSTATUS
gckOS_AtomSetMask(
    IN void *Atom,
    IN u32 Mask
    );

gceSTATUS
gckOS_AtomClearMask(
    IN void *Atom,
    IN u32 Mask
    );
#endif

gceSTATUS
gckOS_DumpGPUState(
    IN gckOS Os,
    IN gceCORE Core
    );

/*******************************************************************************
**
**  gckOS_AtomConstruct
**
**  Create an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**  OUTPUT:
**
**      void ** Atom
**          Pointer to a variable receiving the constructed atom.
*/
gceSTATUS
gckOS_AtomConstruct(
    IN gckOS Os,
    OUT void **Atom
    );

/*******************************************************************************
**
**  gckOS_AtomDestroy
**
**  Destroy an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      void *Atom
**          Pointer to the atom to destroy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomDestroy(
    IN gckOS Os,
    IN void *Atom
    );

/*******************************************************************************
**
**  gckOS_AtomGet
**
**  Get the 32-bit value protected by an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      void *Atom
**          Pointer to the atom.
**
**  OUTPUT:
**
**      s32 *Value
**          Pointer to a variable the receives the value of the atom.
*/
gceSTATUS
gckOS_AtomGet(
    IN gckOS Os,
    IN void *Atom,
    OUT s32 *Value
    );

/*******************************************************************************
**
**  gckOS_AtomSet
**
**  Set the 32-bit value protected by an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      void *Atom
**          Pointer to the atom.
**
**      s32 Value
**          The value of the atom.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomSet(
    IN gckOS Os,
    IN void *Atom,
    IN s32 Value
    );

/*******************************************************************************
**
**  gckOS_AtomIncrement
**
**  Atomically increment the 32-bit integer value inside an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      void *Atom
**          Pointer to the atom.
**
**  OUTPUT:
**
**      s32 *Value
**          Pointer to a variable the receives the original value of the atom.
*/
gceSTATUS
gckOS_AtomIncrement(
    IN gckOS Os,
    IN void *Atom,
    OUT s32 *Value
    );

/*******************************************************************************
**
**  gckOS_AtomDecrement
**
**  Atomically decrement the 32-bit integer value inside an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      void *Atom
**          Pointer to the atom.
**
**  OUTPUT:
**
**      s32 *Value
**          Pointer to a variable the receives the original value of the atom.
*/
gceSTATUS
gckOS_AtomDecrement(
    IN gckOS Os,
    IN void *Atom,
    OUT s32 *Value
    );

/* Delay a number of microseconds. */
gceSTATUS
gckOS_Delay(
    IN gckOS Os,
    IN u32 Delay
    );

/* Get time in milliseconds. */
gceSTATUS
gckOS_GetTicks(
    OUT u32 *Time
    );

/* Compare time value. */
gceSTATUS
gckOS_TicksAfter(
    IN u32 Time1,
    IN u32 Time2,
    OUT int *IsAfter
    );

/* Get time in microseconds. */
gceSTATUS
gckOS_GetTime(
    OUT u64 *Time
    );

/* Memory barrier. */
gceSTATUS
gckOS_MemoryBarrier(
    IN gckOS Os,
    IN void *Address
    );

/* Map user pointer. */
gceSTATUS
gckOS_MapUserPointer(
    IN gckOS Os,
    IN void *Pointer,
    IN size_t Size,
    OUT void **KernelPointer
    );

/* Unmap user pointer. */
gceSTATUS
gckOS_UnmapUserPointer(
    IN gckOS Os,
    IN void *Pointer,
    IN size_t Size,
    IN void *KernelPointer
    );

gceSTATUS
gckOS_SuspendInterruptEx(
    IN gckOS Os,
    IN gceCORE Core
    );

gceSTATUS
gckOS_ResumeInterruptEx(
    IN gckOS Os,
    IN gceCORE Core
    );

/* Get the base address for the physical memory. */
gceSTATUS
gckOS_GetBaseAddress(
    IN gckOS Os,
    OUT u32 *BaseAddress
    );

/* Perform a memory copy. */
gceSTATUS
gckOS_MemCopy(
    IN void *Destination,
    IN const void *Source,
    IN size_t Bytes
    );

/* Zero memory. */
gceSTATUS
gckOS_ZeroMemory(
    IN void *Memory,
    IN size_t Bytes
    );

/******************************************************************************\
********************************** Signal Object *********************************
\******************************************************************************/

/* Create a signal. */
gceSTATUS
gckOS_CreateSignal(
    IN gckOS Os,
    IN int ManualReset,
    OUT gctSIGNAL * Signal
    );

/* Destroy a signal. */
gceSTATUS
gckOS_DestroySignal(
    IN gckOS Os,
    IN gctSIGNAL Signal
    );

/* Signal a signal. */
gceSTATUS
gckOS_Signal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN int State
    );

/* Wait for a signal. */
gceSTATUS
gckOS_WaitSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN u32 Wait
    );

/* Map a user signal to the kernel space. */
gceSTATUS
gckOS_MapSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctHANDLE Process,
    OUT gctSIGNAL * MappedSignal
    );

/* Map user memory. */
gceSTATUS
gckOS_MapUserMemoryEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN void *Memory,
    IN size_t Size,
    OUT void **Info,
    OUT u32 *Address
    );

/* Unmap user memory. */
gceSTATUS
gckOS_UnmapUserMemoryEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN void *Memory,
    IN size_t Size,
    IN void *Info,
    IN u32 Address
    );

#if !USE_NEW_LINUX_SIGNAL
/* Create signal to be used in the user space. */
gceSTATUS
gckOS_CreateUserSignal(
    IN gckOS Os,
    IN int ManualReset,
    OUT int * SignalID
    );

/* Destroy signal used in the user space. */
gceSTATUS
gckOS_DestroyUserSignal(
    IN gckOS Os,
    IN int SignalID
    );

/* Wait for signal used in the user space. */
gceSTATUS
gckOS_WaitUserSignal(
    IN gckOS Os,
    IN int SignalID,
    IN u32 Wait
    );

/* Signal a signal used in the user space. */
gceSTATUS
gckOS_SignalUserSignal(
    IN gckOS Os,
    IN int SignalID,
    IN int State
    );
#endif /* USE_NEW_LINUX_SIGNAL */

/* Set a signal owned by a process. */
gceSTATUS
gckOS_UserSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctHANDLE Process
    );

/******************************************************************************\
** Cache Support
*/

gceSTATUS
gckOS_CacheClean(
    gckOS Os,
    u32 ProcessID,
    gctPHYS_ADDR Handle,
    void *Physical,
    void *Logical,
    size_t Bytes
    );

gceSTATUS
gckOS_CacheFlush(
    gckOS Os,
    u32 ProcessID,
    gctPHYS_ADDR Handle,
    void *Physical,
    void *Logical,
    size_t Bytes
    );

gceSTATUS
gckOS_CacheInvalidate(
    gckOS Os,
    u32 ProcessID,
    gctPHYS_ADDR Handle,
    void *Physical,
    void *Logical,
    size_t Bytes
    );

/******************************************************************************\
** Debug Support
*/

void
gckOS_SetDebugLevel(
    IN u32 Level
    );

void
gckOS_SetDebugZones(
    IN u32 Zones,
    IN int Enable
    );

/*******************************************************************************
** Broadcast interface.
*/

typedef enum _gceBROADCAST
{
    /* GPU might be idle. */
    gcvBROADCAST_GPU_IDLE,

    /* A commit is going to happen. */
    gcvBROADCAST_GPU_COMMIT,

    /* GPU seems to be stuck. */
    gcvBROADCAST_GPU_STUCK,

    /* First process gets attached. */
    gcvBROADCAST_FIRST_PROCESS,

    /* Last process gets detached. */
    gcvBROADCAST_LAST_PROCESS,

    /* AXI bus error. */
    gcvBROADCAST_AXI_BUS_ERROR,
}
gceBROADCAST;

gceSTATUS
gckOS_Broadcast(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    IN gceBROADCAST Reason
    );

gceSTATUS
gckOS_BroadcastHurry(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    IN unsigned int Urgency
    );

gceSTATUS
gckOS_BroadcastCalibrateSpeed(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    IN unsigned int Idle,
    IN unsigned int Time
    );

/*******************************************************************************
**
**  gckOS_SetGPUPower
**
**  Set the power of the GPU on or off.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.ß
**
**      int Clock
**          gcvTRUE to turn on the clock, or gcvFALSE to turn off the clock.
**
**      int Power
**          gcvTRUE to turn on the power, or gcvFALSE to turn off the power.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_SetGPUPower(
    IN gckOS Os,
    IN int Clock,
    IN int Power
    );

/*******************************************************************************
** Semaphores.
*/

/* Create a new semaphore. */
gceSTATUS
gckOS_CreateSemaphore(
    IN gckOS Os,
    OUT void **Semaphore
    );

/* Delete a semahore. */
gceSTATUS
gckOS_DestroySemaphore(
    IN gckOS Os,
    IN void *Semaphore
    );

/* Acquire a semahore. */
gceSTATUS
gckOS_AcquireSemaphore(
    IN gckOS Os,
    IN void *Semaphore
    );

/* Try to acquire a semahore. */
gceSTATUS
gckOS_TryAcquireSemaphore(
    IN gckOS Os,
    IN void *Semaphore
    );

/* Release a semahore. */
gceSTATUS
gckOS_ReleaseSemaphore(
    IN gckOS Os,
    IN void *Semaphore
    );

/*******************************************************************************
** Timer API.
*/

typedef void (*gctTIMERFUNCTION)(void *);

/* Create a timer. */
gceSTATUS
gckOS_CreateTimer(
    IN gckOS Os,
    IN gctTIMERFUNCTION Function,
    IN void *Data,
    OUT void **Timer
    );

/* Destory a timer. */
gceSTATUS
gckOS_DestoryTimer(
    IN gckOS Os,
    IN void *Timer
    );

/* Start a timer. */
gceSTATUS
gckOS_StartTimer(
    IN gckOS Os,
    IN void *Timer,
    IN u32 Delay
    );

/* Stop a timer. */
gceSTATUS
gckOS_StopTimer(
    IN gckOS Os,
    IN void *Timer
    );

/******************************************************************************\
******************************** gckVIDMEM Object ******************************
\******************************************************************************/

typedef struct _gckVIDMEM *         gckVIDMEM;
typedef struct _gckKERNEL *         gckKERNEL;
typedef struct _gckDB *             gckDB;

/* Construct a new gckVIDMEM object. */
gceSTATUS
gckVIDMEM_Construct(
    IN gckOS Os,
    IN u32 BaseAddress,
    IN size_t Bytes,
    IN size_t Threshold,
    IN size_t Banking,
    OUT gckVIDMEM * Memory
    );

/* Destroy an gckVDIMEM object. */
gceSTATUS
gckVIDMEM_Destroy(
    IN gckVIDMEM Memory
    );

/* Allocate linear memory. */
gceSTATUS
gckVIDMEM_AllocateLinear(
    IN gckVIDMEM Memory,
    IN size_t Bytes,
    IN u32 Alignment,
    IN gceSURF_TYPE Type,
    OUT gcuVIDMEM_NODE_PTR * Node
    );

/* Free memory. */
gceSTATUS
gckVIDMEM_Free(
    IN gcuVIDMEM_NODE_PTR Node
    );

/* Lock memory. */
gceSTATUS
gckVIDMEM_Lock(
    IN gckKERNEL Kernel,
    IN gcuVIDMEM_NODE_PTR Node,
    IN int Cacheable,
    OUT u32 * Address
    );

/* Unlock memory. */
gceSTATUS
gckVIDMEM_Unlock(
    IN gckKERNEL Kernel,
    IN gcuVIDMEM_NODE_PTR Node,
    IN gceSURF_TYPE Type,
    IN OUT int * Asynchroneous
    );

/* Construct a gcuVIDMEM_NODE union for virtual memory. */
gceSTATUS
gckVIDMEM_ConstructVirtual(
    IN gckKERNEL Kernel,
    IN int Contiguous,
    IN size_t Bytes,
    OUT gcuVIDMEM_NODE_PTR * Node
    );

/******************************************************************************\
******************************** gckKERNEL Object ******************************
\******************************************************************************/

struct _gcsHAL_INTERFACE;

/* Notifications. */
typedef enum _gceNOTIFY
{
    gcvNOTIFY_INTERRUPT,
    gcvNOTIFY_COMMAND_QUEUE,
}
gceNOTIFY;

/* Flush flags. */
typedef enum _gceKERNEL_FLUSH
{
    gcvFLUSH_COLOR              = 0x01,
    gcvFLUSH_DEPTH              = 0x02,
    gcvFLUSH_TEXTURE            = 0x04,
    gcvFLUSH_2D                 = 0x08,
    gcvFLUSH_ALL                = gcvFLUSH_COLOR
                                | gcvFLUSH_DEPTH
                                | gcvFLUSH_TEXTURE
                                | gcvFLUSH_2D,
}
gceKERNEL_FLUSH;

/* Construct a new gckKERNEL object. */
gceSTATUS
gckKERNEL_Construct(
    IN gckOS Os,
    IN gceCORE Core,
    IN void *Context,
    IN gckDB SharedDB,
    OUT gckKERNEL * Kernel
    );

/* Destroy an gckKERNEL object. */
gceSTATUS
gckKERNEL_Destroy(
    IN gckKERNEL Kernel
    );

/* Dispatch a user-level command. */
gceSTATUS
gckKERNEL_Dispatch(
    IN gckKERNEL Kernel,
    IN int FromUser,
    IN OUT struct _gcsHAL_INTERFACE * Interface
    );

/* Query the video memory. */
gceSTATUS
gckKERNEL_QueryVideoMemory(
    IN gckKERNEL Kernel,
    OUT struct _gcsHAL_INTERFACE * Interface
    );

/* Lookup the gckVIDMEM object for a pool. */
gceSTATUS
gckKERNEL_GetVideoMemoryPool(
    IN gckKERNEL Kernel,
    IN gcePOOL Pool,
    OUT gckVIDMEM * VideoMemory
    );

#if gcdUSE_VIDMEM_PER_PID
gceSTATUS
gckKERNEL_GetVideoMemoryPoolPid(
    IN gckKERNEL Kernel,
    IN gcePOOL Pool,
    IN u32 Pid,
    OUT gckVIDMEM * VideoMemory
    );

gceSTATUS
gckKERNEL_CreateVideoMemoryPoolPid(
    IN gckKERNEL Kernel,
    IN gcePOOL Pool,
    IN u32 Pid,
    OUT gckVIDMEM * VideoMemory
    );
#endif

/* Map video memory. */
gceSTATUS
gckKERNEL_MapVideoMemoryEx(
    IN gckKERNEL Kernel,
    IN gceCORE Core,
    IN int InUserSpace,
    IN u32 Address,
    OUT void **Logical
    );

/* Map memory. */
gceSTATUS
gckKERNEL_MapMemory(
    IN gckKERNEL Kernel,
    IN gctPHYS_ADDR Physical,
    IN size_t Bytes,
    OUT void **Logical
    );

/* Unmap memory. */
gceSTATUS
gckKERNEL_UnmapMemory(
    IN gckKERNEL Kernel,
    IN gctPHYS_ADDR Physical,
    IN size_t Bytes,
    IN void *Logical
    );

/* Notification of events. */
gceSTATUS
gckKERNEL_Notify(
    IN gckKERNEL Kernel,
    IN gceNOTIFY Notifcation,
    IN int Data
    );

gceSTATUS
gckKERNEL_QuerySettings(
    IN gckKERNEL Kernel,
    OUT gcsKERNEL_SETTINGS * Settings
    );

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
    );

/* Get access to the user data. */
gceSTATUS
gckKERNEL_OpenUserData(
    IN gckKERNEL Kernel,
    IN int NeedCopy,
    IN void *StaticStorage,
    IN void *UserPointer,
    IN size_t Size,
    OUT void **KernelPointer
    );

/* Release resources associated with the user data connection. */
gceSTATUS
gckKERNEL_CloseUserData(
    IN gckKERNEL Kernel,
    IN int NeedCopy,
    IN int FlushData,
    IN void *UserPointer,
    IN size_t Size,
    OUT void **KernelPointer
    );

/******************************************************************************\
******************************* gckHARDWARE Object *****************************
\******************************************************************************/

/* Construct a new gckHARDWARE object. */
gceSTATUS
gckHARDWARE_Construct(
    IN gckOS Os,
    IN gceCORE Core,
    OUT gckHARDWARE * Hardware
    );

/* Destroy an gckHARDWARE object. */
gceSTATUS
gckHARDWARE_Destroy(
    IN gckHARDWARE Hardware
    );

/* Get hardware type. */
gceSTATUS
gckHARDWARE_GetType(
    IN gckHARDWARE Hardware,
    OUT gceHARDWARE_TYPE * Type
    );

/* Query system memory requirements. */
gceSTATUS
gckHARDWARE_QuerySystemMemory(
    IN gckHARDWARE Hardware,
    OUT size_t * SystemSize,
    OUT u32 * SystemBaseAddress
    );

/* Build virtual address. */
gceSTATUS
gckHARDWARE_BuildVirtualAddress(
    IN gckHARDWARE Hardware,
    IN u32 Index,
    IN u32 Offset,
    OUT u32 * Address
    );

/* Query command buffer requirements. */
gceSTATUS
gckHARDWARE_QueryCommandBuffer(
    IN gckHARDWARE Hardware,
    OUT size_t * Alignment,
    OUT size_t * ReservedHead,
    OUT size_t * ReservedTail
    );

/* Add a WAIT/LINK pair in the command queue. */
gceSTATUS
gckHARDWARE_WaitLink(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN u32 Offset,
    IN OUT size_t * Bytes,
    OUT u32 * WaitOffset,
    OUT size_t * WaitBytes
    );

/* Kickstart the command processor. */
gceSTATUS
gckHARDWARE_Execute(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN size_t Bytes
    );

/* Add an END command in the command queue. */
gceSTATUS
gckHARDWARE_End(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN OUT size_t * Bytes
    );

/* Add a NOP command in the command queue. */
gceSTATUS
gckHARDWARE_Nop(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN OUT size_t * Bytes
    );

/* Add a PIPESELECT command in the command queue. */
gceSTATUS
gckHARDWARE_PipeSelect(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN gcePIPE_SELECT Pipe,
    IN OUT size_t * Bytes
    );

/* Add a LINK command in the command queue. */
gceSTATUS
gckHARDWARE_Link(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN void *FetchAddress,
    IN size_t FetchSize,
    IN OUT size_t * Bytes
    );

/* Add an EVENT command in the command queue. */
gceSTATUS
gckHARDWARE_Event(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN u8 Event,
    IN gceKERNEL_WHERE FromWhere,
    IN OUT size_t * Bytes
    );

/* Query the available memory. */
gceSTATUS
gckHARDWARE_QueryMemory(
    IN gckHARDWARE Hardware,
    OUT size_t * InternalSize,
    OUT u32 * InternalBaseAddress,
    OUT u32 * InternalAlignment,
    OUT size_t * ExternalSize,
    OUT u32 * ExternalBaseAddress,
    OUT u32 * ExternalAlignment,
    OUT u32 * HorizontalTileSize,
    OUT u32 * VerticalTileSize
    );

/* Query the identity of the hardware. */
gceSTATUS
gckHARDWARE_QueryChipIdentity(
    IN gckHARDWARE Hardware,
    OUT struct _gcsHAL_QUERY_CHIP_IDENTITY *Identity
    );

/* Query the shader support. */
gceSTATUS
gckHARDWARE_QueryShaderCaps(
    IN gckHARDWARE Hardware,
    OUT unsigned int * VertexUniforms,
    OUT unsigned int * FragmentUniforms,
    OUT unsigned int * Varyings
    );

/* Split a harwdare specific address into API stuff. */
gceSTATUS
gckHARDWARE_SplitMemory(
    IN gckHARDWARE Hardware,
    IN u32 Address,
    OUT gcePOOL * Pool,
    OUT u32 * Offset
    );

/* Update command queue tail pointer. */
gceSTATUS
gckHARDWARE_UpdateQueueTail(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN u32 Offset
    );

/* Convert logical address to hardware specific address. */
gceSTATUS
gckHARDWARE_ConvertLogical(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    OUT u32 * Address
    );

/* Interrupt manager. */
gceSTATUS
gckHARDWARE_Interrupt(
    IN gckHARDWARE Hardware,
    IN int InterruptValid
    );

/* Program MMU. */
gceSTATUS
gckHARDWARE_SetMMU(
    IN gckHARDWARE Hardware,
    IN void *Logical
    );

/* Flush the MMU. */
gceSTATUS
gckHARDWARE_FlushMMU(
    IN gckHARDWARE Hardware
    );

typedef enum _gceMMU_MODE
{
    gcvMMU_MODE_1K,
    gcvMMU_MODE_4K,
} gceMMU_MODE;

/* Set the page table base address. */
gceSTATUS
gckHARDWARE_SetMMUv2(
    IN gckHARDWARE Hardware,
    IN int Enable,
    IN void *MtlbAddress,
    IN gceMMU_MODE Mode,
    IN void *SafeAddress,
    IN int FromPower
    );

/* Get idle register. */
gceSTATUS
gckHARDWARE_GetIdle(
    IN gckHARDWARE Hardware,
    IN int Wait,
    OUT u32 * Data
    );

/* Flush the caches. */
gceSTATUS
gckHARDWARE_Flush(
    IN gckHARDWARE Hardware,
    IN gceKERNEL_FLUSH Flush,
    IN void *Logical,
    IN OUT size_t * Bytes
    );

/* Enable/disable fast clear. */
gceSTATUS
gckHARDWARE_SetFastClear(
    IN gckHARDWARE Hardware,
    IN int Enable,
    IN int Compression
    );

/* Power management. */
gceSTATUS
gckHARDWARE_SetPowerManagementState(
    IN gckHARDWARE Hardware,
    IN gceCHIPPOWERSTATE State
    );

gceSTATUS
gckHARDWARE_QueryPowerManagementState(
    IN gckHARDWARE Hardware,
    OUT gceCHIPPOWERSTATE* State
    );

/* Profile 2D Engine. */
gceSTATUS
gckHARDWARE_ProfileEngine2D(
    IN gckHARDWARE Hardware,
    OUT struct _gcs2D_PROFILE *Profile
    );

gceSTATUS
gckHARDWARE_InitializeHardware(
    IN gckHARDWARE Hardware
    );

gceSTATUS
gckHARDWARE_Reset(
    IN gckHARDWARE Hardware
    );

typedef gceSTATUS (*gctISRMANAGERFUNC)(void *Context);

gceSTATUS
gckHARDWARE_SetIsrManager(
    IN gckHARDWARE Hardware,
    IN gctISRMANAGERFUNC StartIsr,
    IN gctISRMANAGERFUNC StopIsr,
    IN void *Context
    );

/* Start a composition. */
gceSTATUS
gckHARDWARE_Compose(
    IN gckHARDWARE Hardware,
    IN u32 ProcessID,
    IN gctPHYS_ADDR Physical,
    IN void *Logical,
    IN size_t Offset,
    IN size_t Size,
    IN u8 EventID
    );

/* Chip features. */
typedef enum _gceFEATURE
{
    gcvFEATURE_PIPE_2D = 0,
    gcvFEATURE_PIPE_3D,
    gcvFEATURE_PIPE_VG,
    gcvFEATURE_DC,
    gcvFEATURE_HIGH_DYNAMIC_RANGE,
    gcvFEATURE_MODULE_CG,
    gcvFEATURE_MIN_AREA,
    gcvFEATURE_BUFFER_INTERLEAVING,
    gcvFEATURE_BYTE_WRITE_2D,
    gcvFEATURE_ENDIANNESS_CONFIG,
    gcvFEATURE_DUAL_RETURN_BUS,
    gcvFEATURE_DEBUG_MODE,
    gcvFEATURE_YUY2_RENDER_TARGET,
    gcvFEATURE_FRAGMENT_PROCESSOR,
    gcvFEATURE_2DPE20,
    gcvFEATURE_FAST_CLEAR,
    gcvFEATURE_YUV420_TILER,
    gcvFEATURE_YUY2_AVERAGING,
    gcvFEATURE_FLIP_Y,
    gcvFEATURE_EARLY_Z,
    gcvFEATURE_Z_COMPRESSION,
    gcvFEATURE_MSAA,
    gcvFEATURE_SPECIAL_ANTI_ALIASING,
    gcvFEATURE_SPECIAL_MSAA_LOD,
    gcvFEATURE_422_TEXTURE_COMPRESSION,
    gcvFEATURE_DXT_TEXTURE_COMPRESSION,
    gcvFEATURE_ETC1_TEXTURE_COMPRESSION,
    gcvFEATURE_CORRECT_TEXTURE_CONVERTER,
    gcvFEATURE_TEXTURE_8K,
    gcvFEATURE_SCALER,
    gcvFEATURE_YUV420_SCALER,
    gcvFEATURE_SHADER_HAS_W,
    gcvFEATURE_SHADER_HAS_SIGN,
    gcvFEATURE_SHADER_HAS_FLOOR,
    gcvFEATURE_SHADER_HAS_CEIL,
    gcvFEATURE_SHADER_HAS_SQRT,
    gcvFEATURE_SHADER_HAS_TRIG,
    gcvFEATURE_VAA,
    gcvFEATURE_HZ,
    gcvFEATURE_CORRECT_STENCIL,
    gcvFEATURE_VG20,
    gcvFEATURE_VG_FILTER,
    gcvFEATURE_VG21,
    gcvFEATURE_VG_DOUBLE_BUFFER,
    gcvFEATURE_MC20,
    gcvFEATURE_SUPER_TILED,
    gcvFEATURE_2D_FILTERBLIT_PLUS_ALPHABLEND,
    gcvFEATURE_2D_DITHER,
    gcvFEATURE_2D_A8_TARGET,
    gcvFEATURE_2D_FILTERBLIT_FULLROTATION,
    gcvFEATURE_2D_BITBLIT_FULLROTATION,
    gcvFEATURE_WIDE_LINE,
    gcvFEATURE_FC_FLUSH_STALL,
    gcvFEATURE_FULL_DIRECTFB,
    gcvFEATURE_HALF_FLOAT_PIPE,
    gcvFEATURE_LINE_LOOP,
    gcvFEATURE_2D_YUV_BLIT,
    gcvFEATURE_2D_TILING,
    gcvFEATURE_NON_POWER_OF_TWO,
    gcvFEATURE_3D_TEXTURE,
    gcvFEATURE_TEXTURE_ARRAY,
    gcvFEATURE_TILE_FILLER,
    gcvFEATURE_LOGIC_OP,
    gcvFEATURE_COMPOSITION,
    gcvFEATURE_MIXED_STREAMS,
    gcvFEATURE_2D_MULTI_SOURCE_BLT,
    gcvFEATURE_END_EVENT,
    gcvFEATURE_VERTEX_10_10_10_2,
    gcvFEATURE_TEXTURE_10_10_10_2,
    gcvFEATURE_TEXTURE_ANISOTROPIC_FILTERING,
    gcvFEATURE_TEXTURE_FLOAT_HALF_FLOAT,
    gcvFEATURE_2D_ROTATION_STALL_FIX,
    gcvFEATURE_2D_MULTI_SOURCE_BLT_EX,
    gcvFEATURE_BUG_FIXES10,
    gcvFEATURE_2D_MINOR_TILING,
    /* Supertiled compressed textures are supported. */
    gcvFEATURE_TEX_COMPRRESSION_SUPERTILED,
    gcvFEATURE_FAST_MSAA,
    gcvFEATURE_BUG_FIXED_INDEXED_TRIANGLE_STRIP,
    gcvFEATURE_TEXTURE_TILED_READ,
    gcvFEATURE_DEPTH_BIAS_FIX,
    gcvFEATURE_RECT_PRIMITIVE,
    gcvFEATURE_BUG_FIXES11,
    gcvFEATURE_SUPERTILED_TEXTURE,
    gcvFEATURE_2D_NO_COLORBRUSH_INDEX8
}
gceFEATURE;

/* Check for Hardware features. */
gceSTATUS
gckHARDWARE_IsFeatureAvailable(
    IN gckHARDWARE Hardware,
    IN gceFEATURE Feature
    );

/******************************************************************************\
******************************** gckEVENT Object *******************************
\******************************************************************************/

typedef struct _gckEVENT *      gckEVENT;

/* Construct a new gckEVENT object. */
gceSTATUS
gckEVENT_Construct(
    IN gckKERNEL Kernel,
    OUT gckEVENT * Event
    );

/* Destroy an gckEVENT object. */
gceSTATUS
gckEVENT_Destroy(
    IN gckEVENT Event
    );

/* Add a new event to the list of events. */
gceSTATUS
gckEVENT_AddList(
    IN gckEVENT Event,
    IN struct _gcsHAL_INTERFACE *Interface,
    IN gceKERNEL_WHERE FromWhere,
    IN int AllocateAllowed
    );

/* Schedule a signal event. */
gceSTATUS
gckEVENT_Signal(
    IN gckEVENT Event,
    IN gctSIGNAL Signal,
    IN gceKERNEL_WHERE FromWhere
    );

gceSTATUS
gckEVENT_CommitDone(
    IN gckEVENT Event,
    IN gceKERNEL_WHERE FromWhere
    );

gceSTATUS
gckEVENT_Submit(
    IN gckEVENT Event,
    IN int Wait,
    IN int FromPower
    );

/* Commit an event queue. */
gceSTATUS
gckEVENT_Commit(
    IN gckEVENT Event,
    IN struct _gcsQUEUE *Queue
    );

/* Schedule a composition event. */
gceSTATUS
gckEVENT_Compose(
    IN gckEVENT Event,
    IN struct _gcsHAL_COMPOSE *Info
    );

/* Event callback routine. */
gceSTATUS
gckEVENT_Notify(
    IN gckEVENT Event,
    IN u32 IDs
    );

/* Event callback routine. */
gceSTATUS
gckEVENT_Interrupt(
    IN gckEVENT Event,
    IN u32 IDs
    );

gceSTATUS
gckEVENT_Dump(
    IN gckEVENT Event
    );
/******************************************************************************\
******************************* gckCOMMAND Object ******************************
\******************************************************************************/

typedef struct _gckCOMMAND *        gckCOMMAND;

/* Construct a new gckCOMMAND object. */
gceSTATUS
gckCOMMAND_Construct(
    IN gckKERNEL Kernel,
    OUT gckCOMMAND * Command
    );

/* Destroy an gckCOMMAND object. */
gceSTATUS
gckCOMMAND_Destroy(
    IN gckCOMMAND Command
    );

/* Acquire command queue synchronization objects. */
gceSTATUS
gckCOMMAND_EnterCommit(
    IN gckCOMMAND Command,
    IN int FromPower
    );

/* Release command queue synchronization objects. */
gceSTATUS
gckCOMMAND_ExitCommit(
    IN gckCOMMAND Command,
    IN int FromPower
    );

/* Start the command queue. */
gceSTATUS
gckCOMMAND_Start(
    IN gckCOMMAND Command
    );

/* Stop the command queue. */
gceSTATUS
gckCOMMAND_Stop(
    IN gckCOMMAND Command,
    IN int FromRecovery
    );

/* Commit a buffer to the command queue. */
gceSTATUS
gckCOMMAND_Commit(
    IN gckCOMMAND Command,
    IN gckCONTEXT Context,
    IN gcoCMDBUF CommandBuffer,
    IN struct _gcsSTATE_DELTA *StateDelta,
    IN struct _gcsQUEUE *EventQueue,
    IN u32 ProcessID
    );

/* Reserve space in the command buffer. */
gceSTATUS
gckCOMMAND_Reserve(
    IN gckCOMMAND Command,
    IN size_t RequestedBytes,
    OUT void **Buffer,
    OUT size_t * BufferSize
    );

/* Execute reserved space in the command buffer. */
gceSTATUS
gckCOMMAND_Execute(
    IN gckCOMMAND Command,
    IN size_t RequstedBytes
    );

/* Stall the command queue. */
gceSTATUS
gckCOMMAND_Stall(
    IN gckCOMMAND Command,
    IN int FromPower
    );

/* Attach user process. */
gceSTATUS
gckCOMMAND_Attach(
    IN gckCOMMAND Command,
    OUT gckCONTEXT * Context,
    OUT size_t * StateCount,
    IN u32 ProcessID
    );

/* Detach user process. */
gceSTATUS
gckCOMMAND_Detach(
    IN gckCOMMAND Command,
    IN gckCONTEXT Context
    );

/******************************************************************************\
********************************* gckMMU Object ********************************
\******************************************************************************/

typedef struct _gckMMU *            gckMMU;

/* Construct a new gckMMU object. */
gceSTATUS
gckMMU_Construct(
    IN gckKERNEL Kernel,
    IN size_t MmuSize,
    OUT gckMMU * Mmu
    );

/* Destroy an gckMMU object. */
gceSTATUS
gckMMU_Destroy(
    IN gckMMU Mmu
    );

/* Enable the MMU. */
gceSTATUS
gckMMU_Enable(
    IN gckMMU Mmu,
    IN u32 PhysBaseAddr,
    IN u32 PhysSize
    );

/* Allocate pages inside the MMU. */
gceSTATUS
gckMMU_AllocatePages(
    IN gckMMU Mmu,
    IN size_t PageCount,
    OUT void **PageTable,
    OUT u32 * Address
    );

/* Remove a page table from the MMU. */
gceSTATUS
gckMMU_FreePages(
    IN gckMMU Mmu,
    IN void *PageTable,
    IN size_t PageCount
    );

/* Set the MMU page with info. */
gceSTATUS
gckMMU_SetPage(
   IN gckMMU Mmu,
   IN u32 PageAddress,
   IN u32 *PageEntry
   );

gceSTATUS
gckMMU_Flush(
    IN gckMMU Mmu
    );


#if VIVANTE_PROFILER
gceSTATUS
gckHARDWARE_QueryProfileRegisters(
    IN gckHARDWARE Hardware,
    OUT gcsPROFILER_COUNTERS * Counters
    );
#endif

#endif /* __gc_hal_internal_h_ */
