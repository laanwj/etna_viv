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




#ifndef __gc_hal_driver_h_
#define __gc_hal_driver_h_

#include "gc_hal_enum.h"
#include "gc_hal_types.h"

#if gcdENABLE_VG
#include "gc_hal_driver_vg.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*####modified for marvell-bg2*/
#ifdef CONFIG_DOVE_GPU
#define DRV_NAME "dove_gpu"
#else

#ifndef DRV_NAME
#define DRV_NAME "gal3d"
#endif

#endif

/*#ifndef USE_GALOIS_SHM
#define USE_GALOIS_SHM  1
#endif*/

#define DRV_MSG "[" DRV_NAME "]"
#ifndef gcvHAL_CLASS
#define gcvHAL_CLASS                    DRV_NAME
#endif
/*####end for marvell-bg2*/

/******************************************************************************\
******************************* I/O Control Codes ******************************
\******************************************************************************/
/*####modified for marvell-bg2
//#define gcvHAL_CLASS                    "galcore"
####end for marvell-bg2*/
#define IOCTL_GCHAL_INTERFACE           30000
#define IOCTL_GCHAL_KERNEL_INTERFACE    30001
#define IOCTL_GCHAL_TERMINATE           30002

/******************************************************************************\
********************************* Command Codes ********************************
\******************************************************************************/

typedef enum _gceHAL_COMMAND_CODES
{
    /* Generic query. */
    gcvHAL_QUERY_VIDEO_MEMORY,
    gcvHAL_QUERY_CHIP_IDENTITY,

    /* Contiguous memory. */
    gcvHAL_ALLOCATE_NON_PAGED_MEMORY,
    gcvHAL_FREE_NON_PAGED_MEMORY,
    gcvHAL_ALLOCATE_CONTIGUOUS_MEMORY,
    gcvHAL_FREE_CONTIGUOUS_MEMORY,

    /* Video memory allocation. */
    gcvHAL_ALLOCATE_VIDEO_MEMORY,           /* Enforced alignment. */
    gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY,    /* No alignment. */
    gcvHAL_FREE_VIDEO_MEMORY,

    /* Physical-to-logical mapping. */
    gcvHAL_MAP_MEMORY,
    gcvHAL_UNMAP_MEMORY,

    /* Logical-to-physical mapping. */
    gcvHAL_MAP_USER_MEMORY,
    gcvHAL_UNMAP_USER_MEMORY,

    /* Surface lock/unlock. */
    gcvHAL_LOCK_VIDEO_MEMORY,
    gcvHAL_UNLOCK_VIDEO_MEMORY,

    /* Event queue. */
    gcvHAL_EVENT_COMMIT,

    gcvHAL_USER_SIGNAL,
    gcvHAL_SIGNAL,
    gcvHAL_WRITE_DATA,

    gcvHAL_COMMIT,
    gcvHAL_STALL,

    gcvHAL_READ_REGISTER,
    gcvHAL_WRITE_REGISTER,

    gcvHAL_GET_PROFILE_SETTING,
    gcvHAL_SET_PROFILE_SETTING,

    gcvHAL_READ_ALL_PROFILE_REGISTERS,
    gcvHAL_PROFILE_REGISTERS_2D,

    /* Power management. */
    gcvHAL_SET_POWER_MANAGEMENT_STATE,
    gcvHAL_QUERY_POWER_MANAGEMENT_STATE,

    gcvHAL_GET_BASE_ADDRESS,

    gcvHAL_SET_IDLE, /* reserved */

    /* Queries. */
    gcvHAL_QUERY_KERNEL_SETTINGS,

    /* Reset. */
    gcvHAL_RESET,

    /* Map physical address into handle. */
    gcvHAL_MAP_PHYSICAL,

    /* Debugger stuff. */
    gcvHAL_DEBUG,

    /* Cache stuff. */
    gcvHAL_CACHE,

    /* TimeStamp */
    gcvHAL_TIMESTAMP,

    /* Database. */
    gcvHAL_DATABASE,

    /* Version. */
    gcvHAL_VERSION,

    /* Chip info */
    gcvHAL_CHIP_INFO,

    /* Process attaching/detaching. */
    gcvHAL_ATTACH,
    gcvHAL_DETACH,

    /* Composition. */
    gcvHAL_COMPOSE,

    /* Set timeOut value */
    gcvHAL_SET_TIMEOUT,

    /* Frame database. */
    gcvHAL_GET_FRAME_INFO,

    /* Shared info for each process */
    gcvHAL_GET_SHARED_INFO,
    gcvHAL_SET_SHARED_INFO,
    gcvHAL_QUERY_COMMAND_BUFFER,

    gcvHAL_COMMIT_DONE,

    /* GPU and event dump */
    gcvHAL_DUMP_GPU_STATE,
    gcvHAL_DUMP_EVENT
}
gceHAL_COMMAND_CODES;

/******************************************************************************\
****************************** Interface Structure *****************************
\******************************************************************************/

#define gcdMAX_PROFILE_FILE_NAME    128

/* Kernel settings. */
typedef struct _gcsKERNEL_SETTINGS
{
    /* Used RealTime signal between kernel and user. */
    gctINT signal;
}
gcsKERNEL_SETTINGS;


/* gcvHAL_QUERY_CHIP_IDENTITY */
typedef struct _gcsHAL_QUERY_CHIP_IDENTITY * gcsHAL_QUERY_CHIP_IDENTITY_PTR;
typedef struct _gcsHAL_QUERY_CHIP_IDENTITY
{

    /* Chip model. */
    gceCHIPMODEL                chipModel;

    /* Revision value.*/
    gctUINT32                   chipRevision;

    /* Supported feature fields. */
    gctUINT32                   chipFeatures;

    /* Supported minor feature fields. */
    gctUINT32                   chipMinorFeatures;

    /* Supported minor feature 1 fields. */
    gctUINT32                   chipMinorFeatures1;

    /* Supported minor feature 2 fields. */
    gctUINT32                   chipMinorFeatures2;

    /* Supported minor feature 3 fields. */
    gctUINT32                   chipMinorFeatures3;

    /* Number of streams supported. */
    gctUINT32                   streamCount;

    /* Total number of temporary registers per thread. */
    gctUINT32                   registerMax;

    /* Maximum number of threads. */
    gctUINT32                   threadCount;

    /* Number of shader cores. */
    gctUINT32                   shaderCoreCount;

    /* Size of the vertex cache. */
    gctUINT32                   vertexCacheSize;

    /* Number of entries in the vertex output buffer. */
    gctUINT32                   vertexOutputBufferSize;

    /* Number of pixel pipes. */
    gctUINT32                   pixelPipes;

    /* Number of instructions. */
	gctUINT32                   instructionCount;

    /* Number of constants. */
	gctUINT32                   numConstants;

	/* Buffer size */
	gctUINT32                   bufferSize;

}
gcsHAL_QUERY_CHIP_IDENTITY;

/* gcvHAL_COMPOSE. */
typedef struct _gcsHAL_COMPOSE * gcsHAL_COMPOSE_PTR;
typedef struct _gcsHAL_COMPOSE
{
    /* Composition state buffer. */
    gctPHYS_ADDR                physical;
    gctPOINTER                  logical;
    gctSIZE_T                   offset;
    gctSIZE_T                   size;

    /* Composition end signal. */
    gctHANDLE                   process;
    gctSIGNAL                   signal;

    /* User signals. */
    gctHANDLE                   userProcess;
    gctSIGNAL                   userSignal1;
    gctSIGNAL                   userSignal2;

#if defined(__QNXNTO__)
    /* Client pulse side-channel connection ID. */
    gctINT32                    coid;

    /* Set by server. */
    gctINT32                    rcvid;
#endif
}
gcsHAL_COMPOSE;

typedef struct _gcsHAL_INTERFACE
{
    /* Command code. */
    gceHAL_COMMAND_CODES        command;

    /* Hardware type. */
    gceHARDWARE_TYPE            hardwareType;

    /* Status value. */
    gceSTATUS                   status;

    /* Handle to this interface channel. */
    gctHANDLE                   handle;

    /* Pid of the client. */
    gctUINT32                   pid;

    /* Union of command structures. */
    union _u
    {
        /* gcvHAL_GET_BASE_ADDRESS */
        struct _gcsHAL_GET_BASE_ADDRESS
        {
            /* Physical memory address of internal memory. */
            OUT gctUINT32               baseAddress;
        }
        GetBaseAddress;

        /* gcvHAL_QUERY_VIDEO_MEMORY */
        struct _gcsHAL_QUERY_VIDEO_MEMORY
        {
            /* Physical memory address of internal memory. */
            OUT gctPHYS_ADDR            internalPhysical;

            /* Size in bytes of internal memory.*/
            OUT gctSIZE_T               internalSize;

            /* Physical memory address of external memory. */
            OUT gctPHYS_ADDR            externalPhysical;

            /* Size in bytes of external memory.*/
            OUT gctSIZE_T               externalSize;

            /* Physical memory address of contiguous memory. */
            OUT gctPHYS_ADDR            contiguousPhysical;

            /* Size in bytes of contiguous memory.*/
            OUT gctSIZE_T               contiguousSize;
        }
        QueryVideoMemory;

        /* gcvHAL_QUERY_CHIP_IDENTITY */
        gcsHAL_QUERY_CHIP_IDENTITY      QueryChipIdentity;

        /* gcvHAL_MAP_MEMORY */
        struct _gcsHAL_MAP_MEMORY
        {
            /* Physical memory address to map. */
            IN gctPHYS_ADDR             physical;

            /* Number of bytes in physical memory to map. */
            IN gctSIZE_T                bytes;

            /* Address of mapped memory. */
            OUT gctPOINTER              logical;
        }
        MapMemory;

        /* gcvHAL_UNMAP_MEMORY */
        struct _gcsHAL_UNMAP_MEMORY
        {
            /* Physical memory address to unmap. */
            IN gctPHYS_ADDR             physical;

            /* Number of bytes in physical memory to unmap. */
            IN gctSIZE_T                bytes;

            /* Address of mapped memory to unmap. */
            IN gctPOINTER               logical;
        }
        UnmapMemory;

        /* gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY */
        struct _gcsHAL_ALLOCATE_LINEAR_VIDEO_MEMORY
        {
            /* Number of bytes to allocate. */
            IN OUT gctUINT              bytes;

            /* Buffer alignment. */
            IN gctUINT                  alignment;

            /* Type of allocation. */
            IN gceSURF_TYPE             type;

            /* Memory pool to allocate from. */
            IN OUT gcePOOL              pool;

            /* Allocated video memory. */
            OUT gcuVIDMEM_NODE_PTR      node;
        }
        AllocateLinearVideoMemory;

        /* gcvHAL_ALLOCATE_VIDEO_MEMORY */
        struct _gcsHAL_ALLOCATE_VIDEO_MEMORY
        {
            /* Width of rectangle to allocate. */
            IN OUT gctUINT              width;

            /* Height of rectangle to allocate. */
            IN OUT gctUINT              height;

            /* Depth of rectangle to allocate. */
            IN gctUINT                  depth;

            /* Format rectangle to allocate in gceSURF_FORMAT. */
            IN gceSURF_FORMAT           format;

            /* Type of allocation. */
            IN gceSURF_TYPE             type;

            /* Memory pool to allocate from. */
            IN OUT gcePOOL              pool;

            /* Allocated video memory. */
            OUT gcuVIDMEM_NODE_PTR      node;
        }
        AllocateVideoMemory;

        /* gcvHAL_FREE_VIDEO_MEMORY */
        struct _gcsHAL_FREE_VIDEO_MEMORY
        {
            /* Allocated video memory. */
            IN gcuVIDMEM_NODE_PTR       node;

#ifdef __QNXNTO__
/* TODO: This is part of the unlock - why is it here? */
            /* Mapped logical address to unmap in user space. */
            OUT gctPOINTER              memory;

            /* Number of bytes to allocated. */
            OUT gctSIZE_T               bytes;
#endif
        }
        FreeVideoMemory;

        /* gcvHAL_LOCK_VIDEO_MEMORY */
        struct _gcsHAL_LOCK_VIDEO_MEMORY
        {
            /* Allocated video memory. */
            IN gcuVIDMEM_NODE_PTR       node;

            /* Cache configuration. */
            /* Only gcvPOOL_CONTIGUOUS and gcvPOOL_VIRUTAL
            ** can be configured */
            IN gctBOOL                  cacheable;

            /* Hardware specific address. */
            OUT gctUINT32               address;

            /* Mapped logical address. */
            OUT gctPOINTER              memory;
        }
        LockVideoMemory;

        /* gcvHAL_UNLOCK_VIDEO_MEMORY */
        struct _gcsHAL_UNLOCK_VIDEO_MEMORY
        {
            /* Allocated video memory. */
            IN gcuVIDMEM_NODE_PTR       node;

            /* Type of surface. */
            IN gceSURF_TYPE             type;

            /* Flag to unlock surface asynchroneously. */
            IN OUT gctBOOL              asynchroneous;
        }
        UnlockVideoMemory;

        /* gcvHAL_ALLOCATE_NON_PAGED_MEMORY */
        struct _gcsHAL_ALLOCATE_NON_PAGED_MEMORY
        {
            /* Number of bytes to allocate. */
            IN OUT gctSIZE_T            bytes;

            /* Physical address of allocation. */
            OUT gctPHYS_ADDR            physical;

            /* Logical address of allocation. */
            OUT gctPOINTER              logical;
        }
        AllocateNonPagedMemory;

        /* gcvHAL_FREE_NON_PAGED_MEMORY */
        struct _gcsHAL_FREE_NON_PAGED_MEMORY
        {
            /* Number of bytes allocated. */
            IN gctSIZE_T                bytes;

            /* Physical address of allocation. */
            IN gctPHYS_ADDR             physical;

            /* Logical address of allocation. */
            IN gctPOINTER               logical;
        }
        FreeNonPagedMemory;

        /* gcvHAL_EVENT_COMMIT. */
        struct _gcsHAL_EVENT_COMMIT
        {
            /* Event queue. */
            IN gcsQUEUE_PTR             queue;
        }
        Event;

        /* gcvHAL_COMMIT */
        struct _gcsHAL_COMMIT
        {
            /* Context buffer object. */
            IN gckCONTEXT               context;

            /* Command buffer. */
            IN gcoCMDBUF                commandBuffer;

            /* State delta buffer. */
            gcsSTATE_DELTA_PTR          delta;

            /* Event queue. */
            IN gcsQUEUE_PTR             queue;
        }
        Commit;

        /* gcvHAL_MAP_USER_MEMORY */
        struct _gcsHAL_MAP_USER_MEMORY
        {
            /* Base address of user memory to map. */
            IN gctPOINTER               memory;

            /* Size of user memory in bytes to map. */
            IN gctSIZE_T                size;

            /* Info record required by gcvHAL_UNMAP_USER_MEMORY. */
            OUT gctPOINTER              info;

            /* Physical address of mapped memory. */
            OUT gctUINT32               address;
        }
        MapUserMemory;

        /* gcvHAL_UNMAP_USER_MEMORY */
        struct _gcsHAL_UNMAP_USER_MEMORY
        {
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
        UnmapUserMemory;
#if !USE_NEW_LINUX_SIGNAL
        /* gcsHAL_USER_SIGNAL  */
        struct _gcsHAL_USER_SIGNAL
        {
            /* Command. */
            gceUSER_SIGNAL_COMMAND_CODES command;

            /* Signal ID. */
            IN OUT gctINT               id;

            /* Reset mode. */
            IN gctBOOL                  manualReset;

            /* Wait timedout. */
            IN gctUINT32                wait;

            /* State. */
            IN gctBOOL                  state;
        }
        UserSignal;
#endif

        /* gcvHAL_SIGNAL. */
        struct _gcsHAL_SIGNAL
        {
            /* Signal handle to signal. */
            IN gctSIGNAL                signal;

            /* Reserved. */
            IN gctSIGNAL                auxSignal;

            /* Process owning the signal. */
            IN gctHANDLE                process;

#if defined(__QNXNTO__)
            /* Client pulse side-channel connection ID. Set by client in gcoOS_CreateSignal. */
            IN gctINT32                 coid;

            /* Set by server. */
            IN gctINT32                 rcvid;
#endif
            /* Event generated from where of pipeline */
            IN gceKERNEL_WHERE          fromWhere;
        }
        Signal;

        /* gcvHAL_WRITE_DATA. */
        struct _gcsHAL_WRITE_DATA
        {
            /* Address to write data to. */
            IN gctUINT32                address;

            /* Data to write. */
            IN gctUINT32                data;
        }
        WriteData;

        /* gcvHAL_ALLOCATE_CONTIGUOUS_MEMORY */
        struct _gcsHAL_ALLOCATE_CONTIGUOUS_MEMORY
        {
            /* Number of bytes to allocate. */
            IN OUT gctSIZE_T            bytes;

            /* Hardware address of allocation. */
            OUT gctUINT32               address;

            /* Physical address of allocation. */
            OUT gctPHYS_ADDR            physical;

            /* Logical address of allocation. */
            OUT gctPOINTER              logical;
        }
        AllocateContiguousMemory;

        /* gcvHAL_FREE_CONTIGUOUS_MEMORY */
        struct _gcsHAL_FREE_CONTIGUOUS_MEMORY
        {
            /* Number of bytes allocated. */
            IN gctSIZE_T                bytes;

            /* Physical address of allocation. */
            IN gctPHYS_ADDR             physical;

            /* Logical address of allocation. */
            IN gctPOINTER               logical;
        }
        FreeContiguousMemory;

        /* gcvHAL_READ_REGISTER */
        struct _gcsHAL_READ_REGISTER
        {
            /* Logical address of memory to write data to. */
            IN gctUINT32            address;

            /* Data read. */
            OUT gctUINT32           data;
        }
        ReadRegisterData;

        /* gcvHAL_WRITE_REGISTER */
        struct _gcsHAL_WRITE_REGISTER
        {
            /* Logical address of memory to write data to. */
            IN gctUINT32            address;

            /* Data read. */
            IN gctUINT32            data;
        }
        WriteRegisterData;

#if VIVANTE_PROFILER
        /* gcvHAL_GET_PROFILE_SETTING */
        struct _gcsHAL_GET_PROFILE_SETTING
        {
            /* Enable profiling */
            OUT gctBOOL             enable;

            /* The profile file name */
            OUT gctCHAR             fileName[gcdMAX_PROFILE_FILE_NAME];
        }
        GetProfileSetting;

        /* gcvHAL_SET_PROFILE_SETTING */
        struct _gcsHAL_SET_PROFILE_SETTING
        {
            /* Enable profiling */
            IN gctBOOL              enable;

            /* The profile file name */
            IN gctCHAR              fileName[gcdMAX_PROFILE_FILE_NAME];
        }
        SetProfileSetting;

        /* gcvHAL_READ_ALL_PROFILE_REGISTERS */
        struct _gcsHAL_READ_ALL_PROFILE_REGISTERS
        {
            /* Data read. */
            OUT gcsPROFILER_COUNTERS    counters;
        }
        RegisterProfileData;

        /* gcvHAL_PROFILE_REGISTERS_2D */
        struct _gcsHAL_PROFILE_REGISTERS_2D
        {
            /* Data read. */
            OUT gcs2D_PROFILE_PTR       hwProfile2D;
        }
        RegisterProfileData2D;
#endif
        /* Power management. */
        /* gcvHAL_SET_POWER_MANAGEMENT_STATE */
        struct _gcsHAL_SET_POWER_MANAGEMENT
        {
            /* Data read. */
            IN gceCHIPPOWERSTATE        state;
        }
        SetPowerManagement;

        /* gcvHAL_QUERY_POWER_MANAGEMENT_STATE */
        struct _gcsHAL_QUERY_POWER_MANAGEMENT
        {
            /* Data read. */
            OUT gceCHIPPOWERSTATE       state;

            /* Idle query. */
            OUT gctBOOL                 isIdle;
        }
        QueryPowerManagement;

        /* gcvHAL_QUERY_KERNEL_SETTINGS */
        struct _gcsHAL_QUERY_KERNEL_SETTINGS
        {
            /* Settings.*/
            OUT gcsKERNEL_SETTINGS      settings;
        }
        QueryKernelSettings;

        /* gcvHAL_MAP_PHYSICAL */
        struct _gcsHAL_MAP_PHYSICAL
        {
            /* gcvTRUE to map, gcvFALSE to unmap. */
            IN gctBOOL                  map;

            /* Physical address. */
            IN OUT gctPHYS_ADDR         physical;
        }
        MapPhysical;

        /* gcvHAL_DEBUG */
        struct _gcsHAL_DEBUG
        {
            /* If gcvTRUE, set the debug information. */
            IN gctBOOL                  set;
            IN gctUINT32                level;
            IN gctUINT32                zones;
            IN gctBOOL                  enable;

            IN gceDEBUG_MESSAGE_TYPE    type;
            IN gctUINT32                messageSize;

            /* Message to print if not empty. */
            IN gctCHAR                  message[80];
        }
        Debug;

        /* gcvHAL_CACHE */
        struct _gcsHAL_CACHE
        {
            IN gctBOOL                  invalidate;
            IN gceCACHEOPERATION        operation;
            IN gctHANDLE                process;
/*####modified for marvell-bg2*/
            IN gctPOINTER               physical;
/*####end for marvell-bg2*/
            IN gctPOINTER               logical;
            IN gctSIZE_T                bytes;
            IN gcuVIDMEM_NODE_PTR       node;
        }
        Cache;

        /* gcvHAL_TIMESTAMP */
        struct _gcsHAL_TIMESTAMP
        {
            /* Timer select. */
            IN gctUINT32                timer;

            /* Timer request type (0-stop, 1-start, 2-send delta). */
            IN gctUINT32                request;

            /* Result of delta time in microseconds. */
            OUT gctINT32                timeDelta;
        }
        TimeStamp;

        /* gcvHAL_DATABASE */
        struct _gcsHAL_DATABASE
        {
            /* Set to gcvTRUE if you want to query a particular process ID.
            ** Set to gcvFALSE to query the last detached process. */
            IN gctBOOL                  validProcessID;

            /* Process ID to query. */
            IN gctUINT32                processID;

            /* Information. */
            OUT gcuDATABASE_INFO        vidMem;
            OUT gcuDATABASE_INFO        nonPaged;
            OUT gcuDATABASE_INFO        contiguous;
            OUT gcuDATABASE_INFO        gpuIdle;
        }
        Database;

        /* gcvHAL_VERSION */
        struct _gcsHAL_VERSION
        {
            /* Major version: N.n.n. */
            OUT gctINT32                major;

            /* Minor version: n.N.n. */
            OUT gctINT32                minor;

            /* Patch version: n.n.N. */
            OUT gctINT32                patch;

            /* Build version. */
            OUT gctUINT32               build;
        }
        Version;

        /* gcvHAL_CHIP_INFO */
        struct _gcsHAL_CHIP_INFO
        {
            /* Chip count. */
            OUT gctINT32                count;

            /* Chip types. */
            OUT gceHARDWARE_TYPE        types[gcdCHIP_COUNT];
        }
        ChipInfo;

        /* gcvHAL_ATTACH */
        struct _gcsHAL_ATTACH
        {
            /* Context buffer object. */
            OUT gckCONTEXT              context;

            /* Number of states in the buffer. */
            OUT gctSIZE_T               stateCount;
        }
        Attach;

        /* gcvHAL_DETACH */
        struct _gcsHAL_DETACH
        {
            /* Context buffer object. */
            IN gckCONTEXT               context;
        }
        Detach;

        /* gcvHAL_COMPOSE. */
        gcsHAL_COMPOSE                  Compose;

        /* gcvHAL_GET_FRAME_INFO. */
        struct _gcsHAL_GET_FRAME_INFO
        {
            OUT gcsHAL_FRAME_INFO *     frameInfo;
        }
        GetFrameInfo;

        /* gcvHAL_SET_TIME_OUT. */
        struct _gcsHAL_SET_TIMEOUT
        {
            gctUINT32                   timeOut;
        }
        SetTimeOut;

#if gcdENABLE_VG
		/* gcvHAL_COMMIT */
		struct _gcsHAL_VGCOMMIT
		{
			/* Context buffer. */
			IN gcsVGCONTEXT_PTR			context;

			/* Command queue. */
			IN gcsVGCMDQUEUE_PTR			queue;

			/* Number of entries in the queue. */
			IN gctUINT					entryCount;

			/* Task table. */
			IN gcsTASK_MASTER_TABLE_PTR	taskTable;
		}
		VGCommit;

		/* gcvHAL_QUERY_COMMAND_BUFFER */
		struct _gcsHAL_QUERY_COMMAND_BUFFER
		{
			/* Command buffer attributes. */
			OUT gcsCOMMAND_BUFFER_INFO	information;
		}
		QueryCommandBuffer;

#endif

        struct _gcsHAL_GET_SHARED_INFO
        {
            IN gctUINT32            pid;
            IN gctUINT32            dataId;
            IN gcuVIDMEM_NODE_PTR   node;
            OUT gctUINT8_PTR        data;
            /* fix size */
            OUT gctUINT8_PTR        nodeData;
            gctSIZE_T               size;
            IN gceVIDMEM_NODE_SHARED_INFO_TYPE infoType;
        }
        GetSharedInfo;

        struct _gcsHAL_SET_SHARED_INFO
        {
            IN gctUINT32            dataId;
            IN gcuVIDMEM_NODE_PTR   node;
            IN gctUINT8_PTR         data;
            IN gctUINT8_PTR         nodeData;
            IN gctSIZE_T            size;
            IN gceVIDMEM_NODE_SHARED_INFO_TYPE infoType;
        }
        SetSharedInfo;
    }
    u;
}
gcsHAL_INTERFACE;


#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_driver_h_ */
