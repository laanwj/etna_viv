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




#ifndef __gc_hal_types_h_
#define __gc_hal_types_h_

#include <linux/types.h>

typedef void *                  gctPHYS_ADDR;
typedef void *                  gctHANDLE;
typedef void *                  gctSIGNAL;

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

/* Macro to combine four characters into a Charcater Code. */
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

/******************************************************************************\
****************************** Function Parameters *****************************
\******************************************************************************/

#define IN
#define OUT
#define OPTIONAL

/******************************************************************************\
********************************* Status Codes *********************************
\******************************************************************************/

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

/*******************************************************************************
***** Database ****************************************************************/

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

/*******************************************************************************
***** Frame database **********************************************************/

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

#endif /* __gc_hal_types_h_ */
