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

#ifndef __gc_hal_kernel_h_
#define __gc_hal_kernel_h_

#include "gc_hal.h"
#include "gc_hal_internal.h"
#include "gc_hal_kernel_hardware.h"
#include "gc_hal_options_internal.h"


/*******************************************************************************
***** New MMU Defination *******************************************************/
#define gcdMMU_MTLB_SHIFT           22
#define gcdMMU_STLB_4K_SHIFT        12
#define gcdMMU_STLB_64K_SHIFT       16

#define gcdMMU_MTLB_BITS            (32 - gcdMMU_MTLB_SHIFT)
#define gcdMMU_PAGE_4K_BITS         gcdMMU_STLB_4K_SHIFT
#define gcdMMU_STLB_4K_BITS         (32 - gcdMMU_MTLB_BITS - gcdMMU_PAGE_4K_BITS)
#define gcdMMU_PAGE_64K_BITS        gcdMMU_STLB_64K_SHIFT
#define gcdMMU_STLB_64K_BITS        (32 - gcdMMU_MTLB_BITS - gcdMMU_PAGE_64K_BITS)

#define gcdMMU_MTLB_ENTRY_NUM       (1 << gcdMMU_MTLB_BITS)
#define gcdMMU_MTLB_SIZE            (gcdMMU_MTLB_ENTRY_NUM << 2)
#define gcdMMU_STLB_4K_ENTRY_NUM    (1 << gcdMMU_STLB_4K_BITS)
#define gcdMMU_STLB_4K_SIZE         (gcdMMU_STLB_4K_ENTRY_NUM << 2)
#define gcdMMU_PAGE_4K_SIZE         (1 << gcdMMU_STLB_4K_SHIFT)
#define gcdMMU_STLB_64K_ENTRY_NUM   (1 << gcdMMU_STLB_64K_BITS)
#define gcdMMU_STLB_64K_SIZE        (gcdMMU_STLB_64K_ENTRY_NUM << 2)
#define gcdMMU_PAGE_64K_SIZE        (1 << gcdMMU_STLB_64K_SHIFT)

#define gcdMMU_MTLB_MASK            (~((1U << gcdMMU_MTLB_SHIFT)-1))
#define gcdMMU_STLB_4K_MASK         ((~0U << gcdMMU_STLB_4K_SHIFT) ^ gcdMMU_MTLB_MASK)
#define gcdMMU_PAGE_4K_MASK         (gcdMMU_PAGE_4K_SIZE - 1)
#define gcdMMU_STLB_64K_MASK        ((~((1U << gcdMMU_STLB_64K_SHIFT)-1)) ^ gcdMMU_MTLB_MASK)
#define gcdMMU_PAGE_64K_MASK        (gcdMMU_PAGE_64K_SIZE - 1)

/*******************************************************************************
***** Process Secure Cache ****************************************************/

#define gcdSECURE_CACHE_LRU         1
#define gcdSECURE_CACHE_LINEAR      2
#define gcdSECURE_CACHE_HASH        3
#define gcdSECURE_CACHE_TABLE       4

typedef struct _gcskLOGICAL_CACHE * gcskLOGICAL_CACHE_PTR;
typedef struct _gcskLOGICAL_CACHE   gcskLOGICAL_CACHE;
struct _gcskLOGICAL_CACHE
{
    /* Logical address. */
    void *                          logical;

    /* DMAable address. */
    u32                             dma;

#if gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_HASH
    /* Pointer to the previous and next hash tables. */
    gcskLOGICAL_CACHE_PTR           nextHash;
    gcskLOGICAL_CACHE_PTR           prevHash;
#endif

#if gcdSECURE_CACHE_METHOD != gcdSECURE_CACHE_TABLE
    /* Pointer to the previous and next slot. */
    gcskLOGICAL_CACHE_PTR           next;
    gcskLOGICAL_CACHE_PTR           prev;
#endif

#if gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_LINEAR
    /* Time stamp. */
    u64                             stamp;
#endif
};

typedef struct _gcskSECURE_CACHE * gcskSECURE_CACHE_PTR;
typedef struct _gcskSECURE_CACHE
{
    /* Cache memory. */
    gcskLOGICAL_CACHE               cache[1 + gcdSECURE_CACHE_SLOTS];

    /* Last known index for LINEAR mode. */
    gcskLOGICAL_CACHE_PTR           cacheIndex;

    /* Current free slot for LINEAR mode. */
    u32                             cacheFree;

    /* Time stamp for LINEAR mode. */
    u64                             cacheStamp;

#if gcdSECURE_CACHE_METHOD == gcdSECURE_CACHE_HASH
    /* Hash table for HASH mode. */
    gcskLOGICAL_CACHE              hash[256];
#endif
}
gcskSECURE_CACHE;

/*******************************************************************************
***** Process Database Management *********************************************/

typedef enum _gceDATABASE_TYPE
{
    gcvDB_VIDEO_MEMORY = 1,             /* Video memory created. */
    gcvDB_NON_PAGED,                    /* Non paged memory. */
    gcvDB_CONTIGUOUS,                   /* Contiguous memory. */
    gcvDB_SIGNAL,                       /* Signal. */
    gcvDB_VIDEO_MEMORY_LOCKED,          /* Video memory locked. */
    gcvDB_CONTEXT,                      /* Context */
    gcvDB_IDLE,                         /* GPU idle. */
    gcvDB_MAP_MEMORY,                   /* Map memory */
    gcvDB_SHARED_INFO,                 /* Private data */
    gcvDB_MAP_USER_MEMORY               /* Map user memory */
}
gceDATABASE_TYPE;

typedef struct _gcsDATABASE_RECORD *    gcsDATABASE_RECORD_PTR;
typedef struct _gcsDATABASE_RECORD
{
    /* Pointer to kernel. */
    gckKERNEL                           kernel;

    /* Pointer to next database record. */
    gcsDATABASE_RECORD_PTR              next;

    /* Type of record. */
    gceDATABASE_TYPE                    type;

    /* Data for record. */
    void *                              data;
    gctPHYS_ADDR                        physical;
    size_t                              bytes;
}
gcsDATABASE_RECORD;

typedef struct _gcsDATABASE *           gcsDATABASE_PTR;
typedef struct _gcsDATABASE
{
    /* Pointer to next entry is hash list. */
    gcsDATABASE_PTR                     next;
    size_t                              slot;

    /* Process ID. */
    u32                                 processID;

    /* Sizes to query. */
    gcsDATABASE_COUNTERS                vidMem;
    gcsDATABASE_COUNTERS                nonPaged;
    gcsDATABASE_COUNTERS                contiguous;
    gcsDATABASE_COUNTERS                mapUserMemory;
    gcsDATABASE_COUNTERS                mapMemory;

    /* Idle time management. */
    u64                                 lastIdle;
    u64                                 idle;

    /* Pointer to database. */
    gcsDATABASE_RECORD_PTR              list;

#if gcdSECURE_USER
    /* Secure cache. */
    gcskSECURE_CACHE                    cache;
#endif
}
gcsDATABASE;

/* Create a process database that will contain all its allocations. */
gceSTATUS
gckKERNEL_CreateProcessDB(
    IN gckKERNEL Kernel,
    IN u32 ProcessID
    );

/* Add a record to the process database. */
gceSTATUS
gckKERNEL_AddProcessDB(
    IN gckKERNEL Kernel,
    IN u32 ProcessID,
    IN gceDATABASE_TYPE Type,
    IN void *Pointer,
    IN gctPHYS_ADDR Physical,
    IN size_t Size
    );

/* Remove a record to the process database. */
gceSTATUS
gckKERNEL_RemoveProcessDB(
    IN gckKERNEL Kernel,
    IN u32 ProcessID,
    IN gceDATABASE_TYPE Type,
    IN void *Pointer
    );

/* Destroy the process database. */
gceSTATUS
gckKERNEL_DestroyProcessDB(
    IN gckKERNEL Kernel,
    IN u32 ProcessID
    );

/* Find a record to the process database. */
gceSTATUS
gckKERNEL_FindProcessDB(
    IN gckKERNEL Kernel,
    IN u32 ProcessID,
    IN u32 ThreadID,
    IN gceDATABASE_TYPE Type,
    IN void *Pointer,
    OUT gcsDATABASE_RECORD_PTR Record
    );

/* Query the process database. */
gceSTATUS
gckKERNEL_QueryProcessDB(
    IN gckKERNEL Kernel,
    IN u32 ProcessID,
    IN int LastProcessID,
    IN gceDATABASE_TYPE Type,
    OUT gcuDATABASE_INFO * Info
    );

#if gcdSECURE_USER
/* Get secure cache from the process database. */
gceSTATUS
gckKERNEL_GetProcessDBCache(
    IN gckKERNEL Kernel,
    IN u32 ProcessID,
    OUT gcskSECURE_CACHE_PTR * Cache
    );
#endif

/*******************************************************************************
********* Timer Management ****************************************************/
typedef struct _gcsTIMER *           gcsTIMER_PTR;
typedef struct _gcsTIMER
{
    /* Start and Stop time holders. */
    u64                                 startTime;
    u64                                 stopTime;
}
gcsTIMER;

/******************************************************************************\
********************************** Structures **********************************
\******************************************************************************/

/* gckDB object. */
struct _gckDB
{
    /* Database management. */
    gcsDATABASE_PTR             db[16];
    void *                      dbMutex;
    gcsDATABASE_PTR             freeDatabase;
    gcsDATABASE_RECORD_PTR      freeRecord;
    gcsDATABASE_PTR             lastDatabase;
    u32                         lastProcessID;
    u64                         lastIdle;
    u64                         idleTime;
    u64                         lastSlowdown;
    u64                         lastSlowdownIdle;
};

/* gckKERNEL object. */
struct _gckKERNEL
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to gckOS object. */
    gckOS                       os;

    /* Core */
    gceCORE                     core;

    /* Pointer to gckHARDWARE object. */
    gckHARDWARE                 hardware;

    /* Pointer to gckCOMMAND object. */
    gckCOMMAND                  command;

    /* Pointer to gckEVENT object. */
    gckEVENT                    eventObj;

    /* Pointer to context. */
    void *                      context;

    /* Pointer to gckMMU object. */
    gckMMU                      mmu;

    /* Arom holding number of clients. */
    void *                      atomClients;

    /* Database management. */
    gckDB                       db;
    int                         dbCreated;

    /* Pointer to gckEVENT object. */
    gcsTIMER                    timers[8];
    u32                         timeOut;
};

/* gckCOMMAND object. */
struct _gckCOMMAND
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to required object. */
    gckKERNEL                   kernel;
    gckOS                       os;

    /* Current pipe select. */
    gcePIPE_SELECT              pipeSelect;

    /* Command queue running flag. */
    int                         running;

    /* Idle flag and commit stamp. */
    int                         idle;
    u64                         commitStamp;

    /* Command queue mutex. */
    void *                      mutexQueue;

    /* Context switching mutex. */
    void *                      mutexContext;

    /* Command queue power semaphore. */
    void *                      powerSemaphore;

    /* Current command queue. */
    struct _gcskCOMMAND_QUEUE
    {
        gctSIGNAL               signal;
        gctPHYS_ADDR            physical;
        void *                  logical;
    }
    queues[gcdCOMMAND_QUEUES];

    gctPHYS_ADDR                physical;
    void *                      logical;
    u32                         offset;
    int                         index;
#if gcmIS_DEBUG(gcdDEBUG_TRACE)
    unsigned int                wrapCount;
#endif

    /* The command queue is new. */
    int                         newQueue;

    /* Context management. */
    gckCONTEXT                  currContext;

    /* Pointer to last WAIT command. */
    gctPHYS_ADDR                waitPhysical;
    void *                      waitLogical;
    size_t                      waitSize;

    /* Command buffer alignment. */
    size_t                      alignment;
    size_t                      reservedHead;
    size_t                      reservedTail;

    /* Commit counter. */
    void *                      atomCommit;

    /* Kernel process ID. */
    u32                         kernelProcessID;

    /* End Event signal. */
    gctSIGNAL                   endEventSignal;

#if gcdSECURE_USER
    /* Hint array copy buffer. */
    int                         hintArrayAllocated;
    unsigned int                hintArraySize;
    u32 *                       hintArray;
#endif
};

typedef struct _gcsEVENT *      gcsEVENT_PTR;

/* Structure holding one event to be processed. */
typedef struct _gcsEVENT
{
    /* Pointer to next event in queue. */
    gcsEVENT_PTR                next;

    /* Event information. */
    gcsHAL_INTERFACE            info;

    /* Process ID owning the event. */
    u32                         processID;
}
gcsEVENT;

/* Structure holding a list of events to be processed by an interrupt. */
typedef struct _gcsEVENT_QUEUE * gcsEVENT_QUEUE_PTR;
typedef struct _gcsEVENT_QUEUE
{
    /* Time stamp. */
    u64                         stamp;

    /* Source of the event. */
    gceKERNEL_WHERE             source;

    /* Pointer to head of event queue. */
    gcsEVENT_PTR                head;

    /* Pointer to tail of event queue. */
    gcsEVENT_PTR                tail;

    /* Next list of events. */
    gcsEVENT_QUEUE_PTR          next;
}
gcsEVENT_QUEUE;

/*
    gcdREPO_LIST_COUNT defines the maximum number of event queues with different
    hardware module sources that may coexist at the same time. Only two sources
    are supported - gcvKERNEL_COMMAND and gcvKERNEL_PIXEL. gcvKERNEL_COMMAND
    source is used only for managing the kernel command queue and is only issued
    when the current command queue gets full. Since we commit event queues every
    time we commit command buffers, in the worst case we can have up to three
    pending event queues:
        - gcvKERNEL_PIXEL
        - gcvKERNEL_COMMAND (queue overflow)
        - gcvKERNEL_PIXEL
*/
#define gcdREPO_LIST_COUNT      3

/* gckEVENT object. */
struct _gckEVENT
{
    /* The object. */
    gcsOBJECT                   object;

    /* Pointer to required objects. */
    gckOS                       os;
    gckKERNEL                   kernel;

    /* Time stamp. */
    u64                         stamp;
    u64                         lastCommitStamp;

    /* Queue mutex. */
    void *                      eventQueueMutex;

    /* Array of event queues. */
    gcsEVENT_QUEUE              queues[30];
    u8                          lastID;
    void *                      freeAtom;

    /* Pending events. */
#ifdef CONFIG_SMP
    void *                      pending;
#else
    volatile unsigned int       pending;
#endif

    /* List of free event structures and its mutex. */
    gcsEVENT_PTR                freeEventList;
    size_t                      freeEventCount;
    void *                      freeEventMutex;

    /* Event queues. */
    gcsEVENT_QUEUE_PTR          queueHead;
    gcsEVENT_QUEUE_PTR          queueTail;
    gcsEVENT_QUEUE_PTR          freeList;
    gcsEVENT_QUEUE              repoList[gcdREPO_LIST_COUNT];
    void *                      eventListMutex;
};

gceSTATUS
gckEVENT_Stop(
    IN gckEVENT Event,
    IN u32 ProcessID,
    IN gctPHYS_ADDR Handle,
    IN void *Logical,
    IN gctSIGNAL Signal,
	IN OUT size_t * waitSize
    );

/* gcuVIDMEM_NODE structure. */
typedef union _gcuVIDMEM_NODE
{
    /* Allocated from gckVIDMEM. */
    struct _gcsVIDMEM_NODE_VIDMEM
    {
        /* Owner of this node. */
        gckVIDMEM               memory;

        /* Dual-linked list of nodes. */
        gcuVIDMEM_NODE_PTR      next;
        gcuVIDMEM_NODE_PTR      prev;

        /* Dual linked list of free nodes. */
        gcuVIDMEM_NODE_PTR      nextFree;
        gcuVIDMEM_NODE_PTR      prevFree;

        /* Information for this node. */
        u32                     offset;
        size_t                  bytes;
        u32                     alignment;

        /* Locked counter. */
        s32                     locked;

        /* Memory pool. */
        gcePOOL                 pool;
        u32                     physical;

        /* Process ID owning this memory. */
        u32                     processID;

        /* Prevent compositor from freeing until client unlocks. */
        int                     freePending;

        /* */
        gcsVIDMEM_NODE_SHARED_INFO sharedInfo;
    }
    VidMem;

    /* Allocated from gckOS. */
    struct _gcsVIDMEM_NODE_VIRTUAL
    {
        /* Pointer to gckKERNEL object. */
        gckKERNEL               kernel;

        /* Information for this node. */
        /* Contiguously allocated? */
        int                     contiguous;
        /* mdl record pointer... a kmalloc address. Process agnostic. */
        gctPHYS_ADDR            physical;
        size_t                  bytes;
        /* do_mmap_pgoff address... mapped per-process. */
        void *                  logical;

        /* Page table information. */
        /* Used only when node is not contiguous */
        size_t                  pageCount;

        /* Used only when node is not contiguous */
        void *                  pageTables[gcdCORE_COUNT];
        /* Pointer to gckKERNEL object who lock this. */
        gckKERNEL               lockKernels[gcdCORE_COUNT];
        /* Actual physical address */
        u32                     addresses[gcdCORE_COUNT];

        /* Mutex. */
        void *                  mutex;

        /* Locked counter. */
        s32                     lockeds[gcdCORE_COUNT];

        /* Process ID owning this memory. */
        u32                     processID;

        /* Owner process sets freed to true
         * when it trys to free a locked
         * node */
        int                     freed;

        /* */
        gcsVIDMEM_NODE_SHARED_INFO sharedInfo;
    }
    Virtual;
}
gcuVIDMEM_NODE;

/* gckVIDMEM object. */
struct _gckVIDMEM
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to gckOS object. */
    gckOS                       os;

    /* Information for this video memory heap. */
    u32                         baseAddress;
    size_t                      bytes;
    size_t                      freeBytes;

    /* Mapping for each type of surface. */
    int                         mapping[gcvSURF_NUM_TYPES];

    /* Sentinel nodes for up to 8 banks. */
    gcuVIDMEM_NODE              sentinel[8];

    /* Allocation threshold. */
    size_t                      threshold;

    /* The heap mutex. */
    void *                      mutex;

#if gcdUSE_VIDMEM_PER_PID
    /* The Pid this VidMem belongs to. */
    u32                         pid;

    struct _gckVIDMEM*          next;
#endif
};

/* gckMMU object. */
struct _gckMMU
{
    /* The object. */
    gcsOBJECT                   object;

    /* Pointer to gckOS object. */
    gckOS                       os;

    /* Pointer to gckHARDWARE object. */
    gckHARDWARE                 hardware;

    /* The page table mutex. */
    void *                      pageTableMutex;

    /* Page table information. */
    size_t                      pageTableSize;
    gctPHYS_ADDR                pageTablePhysical;
    u32 *                       pageTableLogical;
    u32                         pageTableEntries;

    /* Master TLB information. */
    size_t                      mtlbSize;
    gctPHYS_ADDR                mtlbPhysical;
    u32 *                       mtlbLogical;
    u32                         mtlbEntries;

    /* Free entries. */
    u32                         heapList;
    int                         freeNodes;

    void *                      staticSTLB;
    int                         enabled;

    u32                         dynamicMappingStart;
};

gceSTATUS
gckKERNEL_AttachProcess(
    IN gckKERNEL Kernel,
    IN int Attach
    );

gceSTATUS
gckKERNEL_AttachProcessEx(
    IN gckKERNEL Kernel,
    IN int Attach,
    IN u32 PID
    );

#if gcdSECURE_USER
gceSTATUS
gckKERNEL_MapLogicalToPhysical(
    IN gckKERNEL Kernel,
    IN gcskSECURE_CACHE_PTR Cache,
    IN OUT void **Data
    );

gceSTATUS
gckKERNEL_FlushTranslationCache(
    IN gckKERNEL Kernel,
    IN gcskSECURE_CACHE_PTR Cache,
    IN void *Logical,
    IN size_t Bytes
    );
#endif

gceSTATUS
gckHARDWARE_QueryIdle(
    IN gckHARDWARE Hardware,
    OUT int *IsIdle
    );

/******************************************************************************\
******************************* gckCONTEXT Object *******************************
\******************************************************************************/

gceSTATUS
gckCONTEXT_Construct(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    IN u32 ProcessID,
    OUT gckCONTEXT * Context
    );

gceSTATUS
gckCONTEXT_Destroy(
    IN gckCONTEXT Context
    );

gceSTATUS
gckCONTEXT_Update(
    IN gckCONTEXT Context,
    IN u32 ProcessID,
    IN struct _gcsSTATE_DELTA *StateDelta
    );

#endif /* __gc_hal_kernel_h_ */
