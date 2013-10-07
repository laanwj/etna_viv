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
#ifndef __gc_hal_h_
#define __gc_hal_h_

#include <linux/types.h>

/* Mark dummy fields, this can be defined to empty to remove unnecessary
 * fields, but that will lose compatibilty with the blob */
#define VIV_DUMMY(type, name) type name##_dummy;
/* Documentation-only defines */
#define IN
#define OUT
#define OPTIONAL

/******************************************************************************\
*********************************** Options  ***********************************
\******************************************************************************/

/* The number of context buffers per user. */
#define gcdCONTEXT_BUFFER_COUNT 2

/* Length of profile file name */
#define gcdMAX_PROFILE_FILE_NAME    128

/*
    VIVANTE_PROFILER

        This define enables the profiler.
*/
#ifndef VIVANTE_PROFILER
#   define VIVANTE_PROFILER                     0
#endif

/*
    gcdSECURE_USER

        Use logical addresses instead of physical addresses in user land.  In
        this case a hint table is created for both command buffers and context
        buffers, and that hint table will be used to patch up those buffers in
        the kernel when they are ready to submit.
*/
#ifndef gcdSECURE_USER
#   define gcdSECURE_USER                       0
#endif

/******************************************************************************\
****************************** Object Declarations *****************************
\******************************************************************************/

typedef union  _gcuVIDMEM_NODE *gcuVIDMEM_NODE_PTR;
typedef void *gctPHYS_ADDR;
typedef void *gctHANDLE;
typedef void *gctSIGNAL;
typedef struct _gckCONTEXT *gckCONTEXT;
typedef struct _gcoCMDBUF *gcoCMDBUF;

/******************************************************************************\
******************************* I/O Control Codes ******************************
\******************************************************************************/

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
********************************* Enumerations *********************************
\******************************************************************************/

/* Status codes */
typedef enum _gceSTATUS
{
    gcvSTATUS_OK                    =   0,
    gcvSTATUS_FALSE                 =   0,
    gcvSTATUS_TRUE                  =   1,
    gcvSTATUS_NO_MORE_DATA          =   2,
    gcvSTATUS_CACHED                =   3,
    gcvSTATUS_MIPMAP_TOO_LARGE      =   4,
    gcvSTATUS_NAME_NOT_FOUND        =   5,
    gcvSTATUS_NOT_OUR_INTERRUPT     =   6,
    gcvSTATUS_MISMATCH              =   7,
    gcvSTATUS_MIPMAP_TOO_SMALL      =   8,
    gcvSTATUS_LARGER                =   9,
    gcvSTATUS_SMALLER               =   10,
    gcvSTATUS_CHIP_NOT_READY        =   11,
    gcvSTATUS_NEED_CONVERSION       =   12,
    gcvSTATUS_SKIP                  =   13,
    gcvSTATUS_DATA_TOO_LARGE        =   14,
    gcvSTATUS_INVALID_CONFIG        =   15,
    gcvSTATUS_CHANGED               =   16,
    gcvSTATUS_NOT_SUPPORT_DITHER    =   17,
    gcvSTATUS_EXECUTED              =   18,
    gcvSTATUS_TERMINATE             =   19,

    gcvSTATUS_INVALID_ARGUMENT      =   -1,
    gcvSTATUS_INVALID_OBJECT        =   -2,
    gcvSTATUS_OUT_OF_MEMORY         =   -3,
    gcvSTATUS_MEMORY_LOCKED         =   -4,
    gcvSTATUS_MEMORY_UNLOCKED       =   -5,
    gcvSTATUS_HEAP_CORRUPTED        =   -6,
    gcvSTATUS_GENERIC_IO            =   -7,
    gcvSTATUS_INVALID_ADDRESS       =   -8,
    gcvSTATUS_CONTEXT_LOSSED        =   -9,
    gcvSTATUS_TOO_COMPLEX           =   -10,
    gcvSTATUS_BUFFER_TOO_SMALL      =   -11,
    gcvSTATUS_INTERFACE_ERROR       =   -12,
    gcvSTATUS_NOT_SUPPORTED         =   -13,
    gcvSTATUS_MORE_DATA             =   -14,
    gcvSTATUS_TIMEOUT               =   -15,
    gcvSTATUS_OUT_OF_RESOURCES      =   -16,
    gcvSTATUS_INVALID_DATA          =   -17,
    gcvSTATUS_INVALID_MIPMAP        =   -18,
    gcvSTATUS_NOT_FOUND             =   -19,
    gcvSTATUS_NOT_ALIGNED           =   -20,
    gcvSTATUS_INVALID_REQUEST       =   -21,
    gcvSTATUS_GPU_NOT_RESPONDING    =   -22,
    gcvSTATUS_TIMER_OVERFLOW        =   -23,
    gcvSTATUS_VERSION_MISMATCH      =   -24,
    gcvSTATUS_LOCKED                =   -25,
    gcvSTATUS_INTERRUPTED           =   -26,
    gcvSTATUS_DEVICE                =   -27,

    /* Linker errors. */
    gcvSTATUS_GLOBAL_TYPE_MISMATCH  =   -1000,
    gcvSTATUS_TOO_MANY_ATTRIBUTES   =   -1001,
    gcvSTATUS_TOO_MANY_UNIFORMS     =   -1002,
    gcvSTATUS_TOO_MANY_VARYINGS     =   -1003,
    gcvSTATUS_UNDECLARED_VARYING    =   -1004,
    gcvSTATUS_VARYING_TYPE_MISMATCH =   -1005,
    gcvSTATUS_MISSING_MAIN          =   -1006,
    gcvSTATUS_NAME_MISMATCH         =   -1007,
    gcvSTATUS_INVALID_INDEX         =   -1008,
    gcvSTATUS_UNIFORM_TYPE_MISMATCH =   -1009,
}
gceSTATUS;

/* Video memory pool type. */
typedef enum _gcePOOL
{
    gcvPOOL_UNKNOWN = 0,
    gcvPOOL_DEFAULT,
    gcvPOOL_LOCAL,
    gcvPOOL_LOCAL_INTERNAL,
    gcvPOOL_LOCAL_EXTERNAL,
    gcvPOOL_UNIFIED,
    gcvPOOL_SYSTEM,
    gcvPOOL_VIRTUAL,
    gcvPOOL_USER,
    gcvPOOL_CONTIGUOUS,

    gcvPOOL_NUMBER_OF_POOLS
}
gcePOOL;

/* Chip models. */
typedef enum _gceCHIPMODEL
{
    gcv300  = 0x0300,
    gcv320  = 0x0320,
    gcv350  = 0x0350,
    gcv355  = 0x0355,
    gcv400  = 0x0400,
    gcv410  = 0x0410,
    gcv420  = 0x0420,
    gcv450  = 0x0450,
    gcv500  = 0x0500,
    gcv530  = 0x0530,
    gcv600  = 0x0600,
    gcv700  = 0x0700,
    gcv800  = 0x0800,
    gcv860  = 0x0860,
    gcv880  = 0x0880,
    gcv1000 = 0x1000,
    gcv2000 = 0x2000,
    gcv2100 = 0x2100,
    gcv4000 = 0x4000,
}
gceCHIPMODEL;

/* Chip Power Status. */
typedef enum _gceCHIPPOWERSTATE
{
    gcvPOWER_ON = 0,
    gcvPOWER_OFF,
    gcvPOWER_IDLE,
    gcvPOWER_SUSPEND,
    gcvPOWER_SUSPEND_ATPOWERON,
    gcvPOWER_OFF_ATPOWERON,
    gcvPOWER_IDLE_BROADCAST,
    gcvPOWER_SUSPEND_BROADCAST,
    gcvPOWER_OFF_BROADCAST,
    gcvPOWER_OFF_RECOVERY,
    gcvPOWER_OFF_TIMEOUT,
    gcvPOWER_ON_AUTO
}
gceCHIPPOWERSTATE;

/* CPU cache operations */
typedef enum _gceCACHEOPERATION
{
    gcvCACHE_CLEAN      = 0x01,
    gcvCACHE_INVALIDATE = 0x02,
    gcvCACHE_FLUSH      = gcvCACHE_CLEAN  | gcvCACHE_INVALIDATE,
    gcvCACHE_MEMORY_BARRIER = 0x04
}
gceCACHEOPERATION;

typedef enum _gceVIDMEM_NODE_SHARED_INFO_TYPE
{
    gcvVIDMEM_INFO_GENERIC,
    gcvVIDMEM_INFO_DIRTY_RECTANGLE
}
gceVIDMEM_NODE_SHARED_INFO_TYPE;

/* Surface types. */
typedef enum _gceSURF_TYPE
{
    gcvSURF_TYPE_UNKNOWN = 0,
    gcvSURF_INDEX,
    gcvSURF_VERTEX,
    gcvSURF_TEXTURE,
    gcvSURF_RENDER_TARGET,
    gcvSURF_DEPTH,
    gcvSURF_BITMAP,
    gcvSURF_TILE_STATUS,
    gcvSURF_IMAGE,
    gcvSURF_MASK,
    gcvSURF_SCISSOR,
    gcvSURF_HIERARCHICAL_DEPTH,
    gcvSURF_NUM_TYPES, /* Make sure this is the last one! */

    /* Combinations. */
    gcvSURF_NO_TILE_STATUS = 0x100,
    gcvSURF_NO_VIDMEM      = 0x200, /* Used to allocate surfaces with no underlying vidmem node.
                                       In Android, vidmem node is allocated by another process. */
    gcvSURF_CACHEABLE      = 0x400, /* Used to allocate a cacheable surface */

    gcvSURF_RENDER_TARGET_NO_TILE_STATUS = gcvSURF_RENDER_TARGET
                                         | gcvSURF_NO_TILE_STATUS,

    gcvSURF_DEPTH_NO_TILE_STATUS         = gcvSURF_DEPTH
                                         | gcvSURF_NO_TILE_STATUS,

    /* Supported surface types with no vidmem node. */
    gcvSURF_BITMAP_NO_VIDMEM             = gcvSURF_BITMAP
                                         | gcvSURF_NO_VIDMEM,

    gcvSURF_TEXTURE_NO_VIDMEM            = gcvSURF_TEXTURE
                                         | gcvSURF_NO_VIDMEM,

    /* Cacheable surface types with no vidmem node. */
    gcvSURF_CACHEABLE_BITMAP_NO_VIDMEM   = gcvSURF_BITMAP_NO_VIDMEM
                                         | gcvSURF_CACHEABLE,

    gcvSURF_CACHEABLE_BITMAP             = gcvSURF_BITMAP
                                         | gcvSURF_CACHEABLE,
}
gceSURF_TYPE;

/* Surface formats. */
typedef enum _gceSURF_FORMAT
{
    /* Unknown format. */
    gcvSURF_UNKNOWN             = 0,

    /* Palettized formats. */
    gcvSURF_INDEX1              = 100,
    gcvSURF_INDEX4,
    gcvSURF_INDEX8,

    /* RGB formats. */
    gcvSURF_A2R2G2B2            = 200,
    gcvSURF_R3G3B2,
    gcvSURF_A8R3G3B2,
    gcvSURF_X4R4G4B4,
    gcvSURF_A4R4G4B4,
    gcvSURF_R4G4B4A4,
    gcvSURF_X1R5G5B5,
    gcvSURF_A1R5G5B5,
    gcvSURF_R5G5B5A1,
    gcvSURF_R5G6B5,
    gcvSURF_R8G8B8,
    gcvSURF_X8R8G8B8,
    gcvSURF_A8R8G8B8,
    gcvSURF_R8G8B8A8,
    gcvSURF_G8R8G8B8,
    gcvSURF_R8G8B8G8,
    gcvSURF_X2R10G10B10,
    gcvSURF_A2R10G10B10,
    gcvSURF_X12R12G12B12,
    gcvSURF_A12R12G12B12,
    gcvSURF_X16R16G16B16,
    gcvSURF_A16R16G16B16,
    gcvSURF_A32R32G32B32,
    gcvSURF_R8G8B8X8,
    gcvSURF_R5G5B5X1,
    gcvSURF_R4G4B4X4,

    /* BGR formats. */
    gcvSURF_A4B4G4R4            = 300,
    gcvSURF_A1B5G5R5,
    gcvSURF_B5G6R5,
    gcvSURF_B8G8R8,
    gcvSURF_B16G16R16,
    gcvSURF_X8B8G8R8,
    gcvSURF_A8B8G8R8,
    gcvSURF_A2B10G10R10,
    gcvSURF_X16B16G16R16,
    gcvSURF_A16B16G16R16,
    gcvSURF_B32G32R32,
    gcvSURF_X32B32G32R32,
    gcvSURF_A32B32G32R32,
    gcvSURF_B4G4R4A4,
    gcvSURF_B5G5R5A1,
    gcvSURF_B8G8R8X8,
    gcvSURF_B8G8R8A8,
    gcvSURF_X4B4G4R4,
    gcvSURF_X1B5G5R5,
    gcvSURF_B4G4R4X4,
    gcvSURF_B5G5R5X1,
    gcvSURF_X2B10G10R10,

    /* Compressed formats. */
    gcvSURF_DXT1                = 400,
    gcvSURF_DXT2,
    gcvSURF_DXT3,
    gcvSURF_DXT4,
    gcvSURF_DXT5,
    gcvSURF_CXV8U8,
    gcvSURF_ETC1,

    /* YUV formats. */
    gcvSURF_YUY2                = 500,
    gcvSURF_UYVY,
    gcvSURF_YV12,
    gcvSURF_I420,
    gcvSURF_NV12,
    gcvSURF_NV21,
    gcvSURF_NV16,
    gcvSURF_NV61,
    gcvSURF_YVYU,
    gcvSURF_VYUY,

    /* Depth formats. */
    gcvSURF_D16                 = 600,
    gcvSURF_D24S8,
    gcvSURF_D32,
    gcvSURF_D24X8,

    /* Alpha formats. */
    gcvSURF_A4                  = 700,
    gcvSURF_A8,
    gcvSURF_A12,
    gcvSURF_A16,
    gcvSURF_A32,
    gcvSURF_A1,

    /* Luminance formats. */
    gcvSURF_L4                  = 800,
    gcvSURF_L8,
    gcvSURF_L12,
    gcvSURF_L16,
    gcvSURF_L32,
    gcvSURF_L1,

    /* Alpha/Luminance formats. */
    gcvSURF_A4L4                = 900,
    gcvSURF_A2L6,
    gcvSURF_A8L8,
    gcvSURF_A4L12,
    gcvSURF_A12L12,
    gcvSURF_A16L16,

    /* Bump formats. */
    gcvSURF_L6V5U5              = 1000,
    gcvSURF_V8U8,
    gcvSURF_X8L8V8U8,
    gcvSURF_Q8W8V8U8,
    gcvSURF_A2W10V10U10,
    gcvSURF_V16U16,
    gcvSURF_Q16W16V16U16,

    /* R/RG/RA formats. */
    gcvSURF_R8                  = 1100,
    gcvSURF_X8R8,
    gcvSURF_G8R8,
    gcvSURF_X8G8R8,
    gcvSURF_A8R8,
    gcvSURF_R16,
    gcvSURF_X16R16,
    gcvSURF_G16R16,
    gcvSURF_X16G16R16,
    gcvSURF_A16R16,
    gcvSURF_R32,
    gcvSURF_X32R32,
    gcvSURF_G32R32,
    gcvSURF_X32G32R32,
    gcvSURF_A32R32,
    gcvSURF_RG16,

    /* Floating point formats. */
    gcvSURF_R16F                = 1200,
    gcvSURF_X16R16F,
    gcvSURF_G16R16F,
    gcvSURF_X16G16R16F,
    gcvSURF_B16G16R16F,
    gcvSURF_X16B16G16R16F,
    gcvSURF_A16B16G16R16F,
    gcvSURF_R32F,
    gcvSURF_X32R32F,
    gcvSURF_G32R32F,
    gcvSURF_X32G32R32F,
    gcvSURF_B32G32R32F,
    gcvSURF_X32B32G32R32F,
    gcvSURF_A32B32G32R32F,
    gcvSURF_A16F,
    gcvSURF_L16F,
    gcvSURF_A16L16F,
    gcvSURF_A16R16F,
    gcvSURF_A32F,
    gcvSURF_L32F,
    gcvSURF_A32L32F,
    gcvSURF_A32R32F,

}
gceSURF_FORMAT;

/* Pipes. */
typedef enum _gcePIPE_SELECT
{
    gcvPIPE_INVALID = ~0,
    gcvPIPE_3D      =  0,
    gcvPIPE_2D
}
gcePIPE_SELECT;

/* Hardware type. */
typedef enum _gceHARDWARE_TYPE
{
    gcvHARDWARE_INVALID = 0x00,
    gcvHARDWARE_3D      = 0x01,
    gcvHARDWARE_2D      = 0x02,
    gcvHARDWARE_VG      = 0x04,

    gcvHARDWARE_3D2D    = gcvHARDWARE_3D | gcvHARDWARE_2D
}
gceHARDWARE_TYPE;

#define gcdCHIP_COUNT               3

/* User signal command codes. */
typedef enum _gceUSER_SIGNAL_COMMAND_CODES
{
    gcvUSER_SIGNAL_CREATE,
    gcvUSER_SIGNAL_DESTROY,
    gcvUSER_SIGNAL_SIGNAL,
    gcvUSER_SIGNAL_WAIT,
    gcvUSER_SIGNAL_MAP,
    gcvUSER_SIGNAL_UNMAP,
}
gceUSER_SIGNAL_COMMAND_CODES;

/* Event locations. */
typedef enum _gceKERNEL_WHERE
{
    gcvKERNEL_COMMAND,
    gcvKERNEL_VERTEX,
    gcvKERNEL_TRIANGLE,
    gcvKERNEL_TEXTURE,
    gcvKERNEL_PIXEL,
}
gceKERNEL_WHERE;

/* gcdDUMP message type. */
typedef enum _gceDEBUG_MESSAGE_TYPE
{
    gcvMESSAGE_TEXT,
    gcvMESSAGE_DUMP
}
gceDEBUG_MESSAGE_TYPE;

/* Macro to combine four characters into a Character Code. */
#define gcmCC(c1, c2, c3, c4) \
( \
    (char) (c1) \
    | \
    ((char) (c2) <<  8) \
    | \
    ((char) (c3) << 16) \
    | \
    ((char) (c4) << 24) \
)

/* Type of objects. */
typedef enum _gceOBJECT_TYPE
{
    gcvOBJ_UNKNOWN              = 0,
    gcvOBJ_BUFFER               = gcmCC('B','U','F','R'),
    gcvOBJ_COMMAND              = gcmCC('C','M','D',' '),
    gcvOBJ_COMMANDBUFFER        = gcmCC('C','M','D','B'),
    gcvOBJ_CONTEXT              = gcmCC('C','T','X','T'),
    gcvOBJ_DEVICE               = gcmCC('D','E','V',' '),
    gcvOBJ_EVENT                = gcmCC('E','V','N','T'),
    gcvOBJ_HARDWARE             = gcmCC('H','A','R','D'),
    gcvOBJ_KERNEL               = gcmCC('K','E','R','N'),
    gcvOBJ_MMU                  = gcmCC('M','M','U',' '),
    gcvOBJ_OS                   = gcmCC('O','S',' ',' '),
    gcvOBJ_VIDMEM               = gcmCC('V','M','E','M'),
}
gceOBJECT_TYPE;

/******************************************************************************\
****************************** Interface Structure *****************************
\******************************************************************************/

/* Kernel settings. */
typedef struct _gcsKERNEL_SETTINGS
{
    /* Used RealTime signal between kernel and user. */
    int    signal;
}
gcsKERNEL_SETTINGS;


/* gcvHAL_QUERY_CHIP_IDENTITY */
typedef struct _gcsHAL_QUERY_CHIP_IDENTITY
{

    /* Chip model. */
    gceCHIPMODEL                chipModel;

    /* Revision value.*/
    __u32                       chipRevision;

    /* Supported feature fields. */
    __u32                       chipFeatures;

    /* Supported minor feature fields. */
    __u32                       chipMinorFeatures;

    /* Supported minor feature 1 fields. */
    __u32                       chipMinorFeatures1;

    /* Supported minor feature 2 fields. */
    __u32                       chipMinorFeatures2;

    /* Supported minor feature 3 fields. */
    __u32                       chipMinorFeatures3;

    /* Number of streams supported. */
    __u32                       streamCount;

    /* Total number of temporary registers per thread. */
    __u32                       registerMax;

    /* Maximum number of threads. */
    __u32                       threadCount;

    /* Number of shader cores. */
    __u32                       shaderCoreCount;

    /* Size of the vertex cache. */
    __u32                       vertexCacheSize;

    /* Number of entries in the vertex output buffer. */
    __u32                       vertexOutputBufferSize;

    /* Number of pixel pipes. */
    __u32                       pixelPipes;

    /* Number of instructions. */
    __u32                       instructionCount;

    /* Number of constants. */
    __u32                       numConstants;

    /* Buffer size */
    __u32                       bufferSize;

}
gcsHAL_QUERY_CHIP_IDENTITY;

/* gcvHAL_COMPOSE. */
typedef struct _gcsHAL_COMPOSE
{
    /* Composition state buffer. */
    gctPHYS_ADDR                physical;
    void *                      logical;
    size_t                      offset;
    size_t                      size;

    /* Composition end signal. */
    gctHANDLE                   process;
    gctSIGNAL                   signal;

    /* User signals. */
    gctHANDLE                   userProcess;
    gctSIGNAL                   userSignal1;
    gctSIGNAL                   userSignal2;
}
gcsHAL_COMPOSE;

/* HW profile information. */
typedef struct _gcsPROFILER_COUNTERS
{
    /* HW static counters. */
    VIV_DUMMY(__u32, gpuClock);
    VIV_DUMMY(__u32, axiClock);
    VIV_DUMMY(__u32, shaderClock);

    /* HW vairable counters. */
    VIV_DUMMY(__u32, gpuClockStart);
    VIV_DUMMY(__u32, gpuClockEnd);

    /* HW vairable counters. */
    __u32           gpuCyclesCounter;
    __u32           gpuTotalRead64BytesPerFrame;
    __u32           gpuTotalWrite64BytesPerFrame;

    /* PE */
    __u32           pe_pixel_count_killed_by_color_pipe;
    __u32           pe_pixel_count_killed_by_depth_pipe;
    __u32           pe_pixel_count_drawn_by_color_pipe;
    __u32           pe_pixel_count_drawn_by_depth_pipe;

    /* SH */
    __u32           ps_inst_counter;
    __u32           rendered_pixel_counter;
    __u32           vs_inst_counter;
    __u32           rendered_vertice_counter;
    __u32           vtx_branch_inst_counter;
    __u32           vtx_texld_inst_counter;
    __u32           pxl_branch_inst_counter;
    __u32           pxl_texld_inst_counter;

    /* PA */
    __u32           pa_input_vtx_counter;
    __u32           pa_input_prim_counter;
    __u32           pa_output_prim_counter;
    __u32           pa_depth_clipped_counter;
    __u32           pa_trivial_rejected_counter;
    __u32           pa_culled_counter;

    /* SE */
    __u32           se_culled_triangle_count;
    __u32           se_culled_lines_count;

    /* RA */
    __u32           ra_valid_pixel_count;
    __u32           ra_total_quad_count;
    __u32           ra_valid_quad_count_after_early_z;
    __u32           ra_total_primitive_count;
    __u32           ra_pipe_cache_miss_counter;
    __u32           ra_prefetch_cache_miss_counter;
    __u32           ra_eez_culled_counter;

    /* TX */
    __u32           tx_total_bilinear_requests;
    __u32           tx_total_trilinear_requests;
    __u32           tx_total_discarded_texture_requests;
    __u32           tx_total_texture_requests;
    __u32           tx_mem_read_count;
    __u32           tx_mem_read_in_8B_count;
    __u32           tx_cache_miss_count;
    __u32           tx_cache_hit_texel_count;
    __u32           tx_cache_miss_texel_count;

    /* MC */
    __u32           mc_total_read_req_8B_from_pipeline;
    __u32           mc_total_read_req_8B_from_IP;
    __u32           mc_total_write_req_8B_from_pipeline;

    /* HI */
    __u32           hi_axi_cycles_read_request_stalled;
    __u32           hi_axi_cycles_write_request_stalled;
    __u32           hi_axi_cycles_write_data_stalled;
}
gcsPROFILER_COUNTERS;

/* 2D Engine profile. */
typedef struct _gcs2D_PROFILE
{
    /* Cycle count.
       32bit counter incremented every 2D clock cycle.
       Wraps back to 0 when the counter overflows.
    */
    __u32     cycleCount;

    /* Pixels rendered by the 2D engine.
       Resets to 0 every time it is read. */
    __u32     pixelsRendered;
}
gcs2D_PROFILE;

typedef struct _gcsDATABASE_COUNTERS
{
    /* Number of currently allocated bytes. */
    size_t                      bytes;

    /* Maximum number of bytes allocated (memory footprint). */
    size_t                      maxBytes;

    /* Total number of bytes allocated. */
    size_t                      totalBytes;
}
gcsDATABASE_COUNTERS;

typedef struct _gcuDATABASE_INFO
{
    /* Counters. */
    gcsDATABASE_COUNTERS        counters;

    /* Time value. */
    __u64                       time;
}
gcuDATABASE_INFO;

/* gcsHAL_FRAME_INFO */
typedef struct _gcsHAL_FRAME_INFO
{
    /* Current timer tick. */
    OUT __u64                   ticks;

    /* Bandwidth counters. */
    OUT unsigned int            readBytes8[8];
    OUT unsigned int            writeBytes8[8];

    /* Counters. */
    OUT unsigned int            cycles[8];
    OUT unsigned int            idleCycles[8];
    OUT unsigned int            mcCycles[8];
    OUT unsigned int            readRequests[8];
    OUT unsigned int            writeRequests[8];

    /* 3D counters. */
    OUT unsigned int            vertexCount;
    OUT unsigned int            primitiveCount;
    OUT unsigned int            rejectedPrimitives;
    OUT unsigned int            culledPrimitives;
    OUT unsigned int            clippedPrimitives;
    OUT unsigned int            outPrimitives;
    OUT unsigned int            inPrimitives;
    OUT unsigned int            culledQuadCount;
    OUT unsigned int            totalQuadCount;
    OUT unsigned int            quadCount;
    OUT unsigned int            totalPixelCount;

    /* PE counters. */
    OUT unsigned int            colorKilled[8];
    OUT unsigned int            colorDrawn[8];
    OUT unsigned int            depthKilled[8];
    OUT unsigned int            depthDrawn[8];

    /* Shader counters. */
    OUT unsigned int            shaderCycles;
    OUT unsigned int            vsInstructionCount;
    OUT unsigned int            vsTextureCount;
    OUT unsigned int            psInstructionCount;
    OUT unsigned int            psTextureCount;

    /* Texture counters. */
    OUT unsigned int            bilinearRequests;
    OUT unsigned int            trilinearRequests;
    OUT unsigned int            txBytes8;
    OUT unsigned int            txHitCount;
    OUT unsigned int            txMissCount;
}
gcsHAL_FRAME_INFO;

/* gcsOBJECT object defintinon. */
typedef struct _gcsOBJECT
{
    /* Type of an object. */
    gceOBJECT_TYPE              type;
}
gcsOBJECT;

/* State delta record. */
typedef struct _gcsSTATE_DELTA_RECORD
{
    /* State address. */
    unsigned int                address;

    /* State mask. */
    __u32                       mask;

    /* State data. */
    __u32                       data;
}
gcsSTATE_DELTA_RECORD;

/* State delta. */
typedef struct _gcsSTATE_DELTA
{
    /* Main state delta ID. Every time state delta structure gets reinitialized,
       main ID is incremented. If main state ID overflows, all map entry IDs get
       reinitialized to make sure there is no potential erroneous match after
       the overflow.*/
    unsigned int                id;

    /* The number of contexts pending modification by the delta. */
    int                         refCount;

    /* Vertex element count for the delta buffer. */
    unsigned int                elementCount;

    /* Number of states currently stored in the record array. */
    unsigned int                recordCount;

    /* Record array; holds all modified states. */
    struct _gcsSTATE_DELTA_RECORD *recordArray;

    VIV_DUMMY(unsigned int *, mapEntryID);
    VIV_DUMMY(unsigned int  , mapEntryIDSize);
    VIV_DUMMY(unsigned int *, mapEntryIndex);

    /* Previous and next state deltas. */
    struct _gcsSTATE_DELTA *    prev;
    struct _gcsSTATE_DELTA *    next;
}
gcsSTATE_DELTA;

/* Command buffer object. */
struct _gcoCMDBUF
{
    /* The object. */
    gcsOBJECT                   object;

    /* Command buffer entry and exit pipes. */
    gcePIPE_SELECT              entryPipe;
    gcePIPE_SELECT              exitPipe;

    /* Feature usage flags. */
    int                         using2D;
    int                         using3D;
    VIV_DUMMY(int, usingFilterBlit);
    VIV_DUMMY(int, usingPalette);

    /* Physical address of command buffer. */
    VIV_DUMMY(gctPHYS_ADDR, physical);

    /* Logical address of command buffer. */
    void *                      logical;

    /* Number of bytes in command buffer. */
    size_t                      bytes_dummy;

    /* Start offset into the command buffer. */
    __u32                       startOffset;

    /* Current offset into the command buffer. */
    __u32                       offset;

    /* Number of free bytes in command buffer. */
    VIV_DUMMY(size_t, free);
    VIV_DUMMY(void *, lastReserve);
    VIV_DUMMY(unsigned int, lastOffset);

#if gcdSECURE_USER
    /* Hint array for the current command buffer. */
    unsigned int                hintArraySize;
    __u32 *                     hintArray;
    __u32 *                     hintArrayTail;
#endif
};

/******************************************************************************\
*************************** Interface structure *******************************
\******************************************************************************/

typedef struct _gcsHAL_INTERFACE
{
    /* Command code. */
    gceHAL_COMMAND_CODES        command;

    /* Hardware type. */
    gceHARDWARE_TYPE            hardwareType;

    /* Status value. */
    gceSTATUS                   status;

    VIV_DUMMY(gctHANDLE, handle);
    VIV_DUMMY(__u32, pid);

    /* Union of command structures. */
    union _u
    {
        /* gcvHAL_GET_BASE_ADDRESS */
        struct _gcsHAL_GET_BASE_ADDRESS
        {
            /* Physical memory address of internal memory. */
            OUT __u32                   baseAddress;
        }
        GetBaseAddress;

        /* gcvHAL_QUERY_VIDEO_MEMORY */
        struct _gcsHAL_QUERY_VIDEO_MEMORY
        {
            /* Physical memory address of internal memory. */
            OUT gctPHYS_ADDR            internalPhysical;

            /* Size in bytes of internal memory.*/
            OUT size_t                  internalSize;

            /* Physical memory address of external memory. */
            OUT gctPHYS_ADDR            externalPhysical;

            /* Size in bytes of external memory.*/
            OUT size_t                  externalSize;

            /* Physical memory address of contiguous memory. */
            OUT gctPHYS_ADDR            contiguousPhysical;

            /* Size in bytes of contiguous memory.*/
            OUT size_t                  contiguousSize;
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
            IN size_t                   bytes;

            /* Address of mapped memory. */
            OUT void *                  logical;
        }
        MapMemory;

        /* gcvHAL_UNMAP_MEMORY */
        struct _gcsHAL_UNMAP_MEMORY
        {
            /* Physical memory address to unmap. */
            IN gctPHYS_ADDR             physical;

            /* Number of bytes in physical memory to unmap. */
            IN size_t                   bytes;

            /* Address of mapped memory to unmap. */
            IN void *                   logical;
        }
        UnmapMemory;

        /* gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY */
        struct _gcsHAL_ALLOCATE_LINEAR_VIDEO_MEMORY
        {
            /* Number of bytes to allocate. */
            IN OUT unsigned int         bytes;

            /* Buffer alignment. */
            IN unsigned int             alignment;

            /* Type of allocation. */
            IN gceSURF_TYPE             type;

            /* Memory pool to allocate from. */
            IN OUT gcePOOL              pool;

            /* Allocated video memory. */
            OUT gcuVIDMEM_NODE_PTR      node;
        }
        AllocateLinearVideoMemory;

        /* gcvHAL_ALLOCATE_VIDEO_MEMORY */
        VIV_DUMMY(struct _gcsHAL_ALLOCATE_VIDEO_MEMORY
        {
            /* Width of rectangle to allocate. */
            IN OUT unsigned int         width;

            /* Height of rectangle to allocate. */
            IN OUT unsigned int         height;

            /* Depth of rectangle to allocate. */
            IN unsigned int             depth;

            /* Format rectangle to allocate in gceSURF_FORMAT. */
            IN gceSURF_FORMAT           format;

            /* Type of allocation. */
            IN gceSURF_TYPE             type;

            /* Memory pool to allocate from. */
            IN OUT gcePOOL              pool;

            /* Allocated video memory. */
            OUT gcuVIDMEM_NODE_PTR      node;
        }, AllocateVideoMemory);

        /* gcvHAL_FREE_VIDEO_MEMORY */
        struct _gcsHAL_FREE_VIDEO_MEMORY
        {
            /* Allocated video memory. */
            IN gcuVIDMEM_NODE_PTR       node;
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
            IN int                      cacheable;

            /* Hardware specific address. */
            OUT __u32                   address;

            /* Mapped logical address. */
            OUT void *                  memory;
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
            IN OUT int                  asynchroneous;
        }
        UnlockVideoMemory;

        /* gcvHAL_ALLOCATE_NON_PAGED_MEMORY */
        struct _gcsHAL_ALLOCATE_NON_PAGED_MEMORY
        {
            /* Number of bytes to allocate. */
            IN OUT size_t               bytes;

            /* Physical address of allocation. */
            OUT gctPHYS_ADDR            physical;

            /* Logical address of allocation. */
            OUT void *                  logical;
        }
        AllocateNonPagedMemory;

        /* gcvHAL_FREE_NON_PAGED_MEMORY */
        struct _gcsHAL_FREE_NON_PAGED_MEMORY
        {
            /* Number of bytes allocated. */
            IN size_t                   bytes;

            /* Physical address of allocation. */
            IN gctPHYS_ADDR             physical;

            /* Logical address of allocation. */
            IN void *                   logical;
        }
        FreeNonPagedMemory;

        /* gcvHAL_EVENT_COMMIT. */
        struct _gcsHAL_EVENT_COMMIT
        {
            /* Event queue. */
            IN struct _gcsQUEUE *       queue;
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
            struct _gcsSTATE_DELTA *    delta;

            /* Event queue. */
            IN struct _gcsQUEUE *       queue;
        }
        Commit;

        /* gcvHAL_MAP_USER_MEMORY */
        struct _gcsHAL_MAP_USER_MEMORY
        {
            /* Base address of user memory to map. */
            IN void *                   memory;

            /* Size of user memory in bytes to map. */
            IN size_t                   size;

            /* Info record required by gcvHAL_UNMAP_USER_MEMORY. */
            OUT void *                  info;

            /* Physical address of mapped memory. */
            OUT __u32                   address;
        }
        MapUserMemory;

        /* gcvHAL_UNMAP_USER_MEMORY */
        struct _gcsHAL_UNMAP_USER_MEMORY
        {
            /* Base address of user memory to unmap. */
            IN void *                   memory;

            /* Size of user memory in bytes to unmap. */
            IN size_t                   size;

            /* Info record returned by gcvHAL_MAP_USER_MEMORY. */
            IN void *                   info;

            /* Physical address of mapped memory as returned by
               gcvHAL_MAP_USER_MEMORY. */
            IN __u32                    address;
        }
        UnmapUserMemory;

        /* gcsHAL_USER_SIGNAL  */
        struct _gcsHAL_USER_SIGNAL
        {
            /* Command. */
            gceUSER_SIGNAL_COMMAND_CODES command;

            /* Signal ID. */
            IN OUT int                  id;

            /* Reset mode. */
            IN int                      manualReset;

            /* Wait timedout. */
            IN __u32                    wait;

            /* State. */
            IN int                      state;
        }
        UserSignal;

        /* gcvHAL_SIGNAL. */
        struct _gcsHAL_SIGNAL
        {
            /* Signal handle to signal. */
            IN gctSIGNAL                signal;

            /* Reserved. */
            IN gctSIGNAL                auxSignal;

            /* Process owning the signal. */
            IN gctHANDLE                process;

            /* Event generated from where of pipeline */
            IN gceKERNEL_WHERE          fromWhere;
        }
        Signal;

        /* gcvHAL_WRITE_DATA. */
        struct _gcsHAL_WRITE_DATA
        {
            /* Address to write data to. */
            IN __u32                    address;

            /* Data to write. */
            IN __u32                    data;
        }
        WriteData;

        /* gcvHAL_ALLOCATE_CONTIGUOUS_MEMORY */
        struct _gcsHAL_ALLOCATE_CONTIGUOUS_MEMORY
        {
            /* Number of bytes to allocate. */
            IN OUT size_t               bytes;

            /* Hardware address of allocation. */
            OUT __u32                   address;

            /* Physical address of allocation. */
            OUT gctPHYS_ADDR            physical;

            /* Logical address of allocation. */
            OUT void *                  logical;
        }
        AllocateContiguousMemory;

        /* gcvHAL_FREE_CONTIGUOUS_MEMORY */
        struct _gcsHAL_FREE_CONTIGUOUS_MEMORY
        {
            /* Number of bytes allocated. */
            IN size_t                   bytes;

            /* Physical address of allocation. */
            IN gctPHYS_ADDR             physical;

            /* Logical address of allocation. */
            IN void *                   logical;
        }
        FreeContiguousMemory;

        /* gcvHAL_READ_REGISTER */
        struct _gcsHAL_READ_REGISTER
        {
            /* Logical address of memory to write data to. */
            IN __u32                address;

            /* Data read. */
            OUT __u32               data;
        }
        ReadRegisterData;

        /* gcvHAL_WRITE_REGISTER */
        struct _gcsHAL_WRITE_REGISTER
        {
            /* Logical address of memory to write data to. */
            IN __u32                address;

            /* Data read. */
            IN __u32                data;
        }
        WriteRegisterData;

#if VIVANTE_PROFILER
        /* gcvHAL_GET_PROFILE_SETTING */
        VIV_DUMMY(struct _gcsHAL_GET_PROFILE_SETTING
        {
            /* Enable profiling */
            OUT int                 enable;

            /* The profile file name */
            OUT char                fileName[gcdMAX_PROFILE_FILE_NAME];
        }, GetProfileSetting);

        /* gcvHAL_SET_PROFILE_SETTING */
        VIV_DUMMY(struct _gcsHAL_SET_PROFILE_SETTING
        {
            /* Enable profiling */
            IN int                  enable;

            /* The profile file name */
            IN char                 fileName[gcdMAX_PROFILE_FILE_NAME];
        }, SetProfileSetting);

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
            OUT struct _gcs2D_PROFILE * hwProfile2D;
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
            OUT int                     isIdle;
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
        VIV_DUMMY(struct _gcsHAL_MAP_PHYSICAL
        {
            /* gcvTRUE to map, gcvFALSE to unmap. */
            IN int                      map;

            /* Physical address. */
            IN OUT gctPHYS_ADDR         physical;
        }, MapPhysical);

        /* gcvHAL_DEBUG */
        struct _gcsHAL_DEBUG
        {
            /* If gcvTRUE, set the debug information. */
            IN int                      set;
            IN __u32                    level;
            IN __u32                    zones;
            IN int                      enable;

            IN gceDEBUG_MESSAGE_TYPE    type;
            IN __u32                    messageSize;

            /* Message to print if not empty. */
            IN char                     message[80];
        }
        Debug;

        /* gcvHAL_CACHE */
        struct _gcsHAL_CACHE
        {
            IN gceCACHEOPERATION        operation;
            IN gctHANDLE                process;
            IN void *                   logical;
            IN size_t                   bytes;
            IN gcuVIDMEM_NODE_PTR       node;
        }
        Cache;

        /* gcvHAL_TIMESTAMP */
        struct _gcsHAL_TIMESTAMP
        {
            /* Timer select. */
            IN __u32                    timer;

            /* Timer request type (0-stop, 1-start, 2-send delta). */
            IN __u32                    request;

            /* Result of delta time in microseconds. */
            OUT __s32                   timeDelta;
        }
        TimeStamp;

        /* gcvHAL_DATABASE */
        struct _gcsHAL_DATABASE
        {
            /* Set to gcvTRUE if you want to query a particular process ID.
            ** Set to gcvFALSE to query the last detached process. */
            IN int                      validProcessID;

            /* Process ID to query. */
            IN __u32                    processID;

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
            OUT __s32                   major;

            /* Minor version: n.N.n. */
            OUT __s32                   minor;

            /* Patch version: n.n.N. */
            OUT __s32                   patch;

            /* Build version. */
            OUT __u32                   build;
        }
        Version;

        /* gcvHAL_CHIP_INFO */
        struct _gcsHAL_CHIP_INFO
        {
            /* Chip count. */
            OUT __s32                   count;

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
            OUT size_t                  stateCount;
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
            __u32                       timeOut;
        }
        SetTimeOut;

        struct _gcsHAL_GET_SHARED_INFO
        {
            IN __u32                pid;
            IN __u32                dataId;
            IN gcuVIDMEM_NODE_PTR   node;
            OUT __u8 *              data;
            /* fix size */
            OUT __u8 *              nodeData;
            size_t                  size;
            IN gceVIDMEM_NODE_SHARED_INFO_TYPE infoType;
        }
        GetSharedInfo;

        struct _gcsHAL_SET_SHARED_INFO
        {
            IN __u32                dataId;
            IN gcuVIDMEM_NODE_PTR   node;
            IN __u8 *               data;
            IN __u8 *               nodeData;
            IN size_t               size;
            IN gceVIDMEM_NODE_SHARED_INFO_TYPE infoType;
        }
        SetSharedInfo;
    }
    u;
}
gcsHAL_INTERFACE;

typedef struct _gcsQUEUE
{
    /* Pointer to next gcsQUEUE structure. */
    struct _gcsQUEUE *          next;

    /* Event information. */
    gcsHAL_INTERFACE            iface;
}
gcsQUEUE;

#undef VIV_DUMMY

#endif /* __gc_hal_h_ */
