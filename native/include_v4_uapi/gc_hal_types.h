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


/******************************************************************************\
********************************** Common Types ********************************
\******************************************************************************/

typedef int                     gctBOOL;
typedef gctBOOL *               gctBOOL_PTR;

typedef int                     gctINT;
typedef signed char             gctINT8;
typedef signed short            gctINT16;
typedef signed int              gctINT32;
typedef signed long long        gctINT64;

typedef gctINT *                gctINT_PTR;
typedef gctINT8 *               gctINT8_PTR;
typedef gctINT16 *              gctINT16_PTR;
typedef gctINT32 *              gctINT32_PTR;
typedef gctINT64 *              gctINT64_PTR;

typedef unsigned int            gctUINT;
typedef unsigned char           gctUINT8;
typedef unsigned short          gctUINT16;
typedef unsigned int            gctUINT32;
typedef unsigned long long      gctUINT64;

typedef gctUINT *               gctUINT_PTR;
typedef gctUINT8 *              gctUINT8_PTR;
typedef gctUINT16 *             gctUINT16_PTR;
typedef gctUINT32 *             gctUINT32_PTR;
typedef gctUINT64 *             gctUINT64_PTR;

typedef unsigned long           gctSIZE_T;
typedef gctSIZE_T *             gctSIZE_T_PTR;

typedef void *                  gctPHYS_ADDR;
typedef void *                  gctHANDLE;
typedef void *                  gctSIGNAL;

typedef void *                  gctPOINTER;
typedef const void *            gctCONST_POINTER;

typedef char                    gctCHAR;
typedef char *                  gctSTRING;
typedef const char *            gctCONST_STRING;

/* 2D Engine profile. */
typedef struct _gcs2D_PROFILE
{
    /* Cycle count.
       32bit counter incremented every 2D clock cycle.
       Wraps back to 0 when the counter overflows.
    */
    gctUINT32 cycleCount;

    /* Pixels rendered by the 2D engine.
       Resets to 0 every time it is read. */
    gctUINT32 pixelsRendered;
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
    gctSIZE_T                   bytes;

    /* Maximum number of bytes allocated (memory footprint). */
    gctSIZE_T                   maxBytes;

    /* Total number of bytes allocated. */
    gctSIZE_T                   totalBytes;
}
gcsDATABASE_COUNTERS;

typedef struct _gcuDATABASE_INFO
{
    /* Counters. */
    gcsDATABASE_COUNTERS        counters;

    /* Time value. */
    gctUINT64                   time;
}
gcuDATABASE_INFO;

/*******************************************************************************
***** Frame database **********************************************************/

/* gcsHAL_FRAME_INFO */
typedef struct _gcsHAL_FRAME_INFO
{
    /* Current timer tick. */
    OUT gctUINT64               ticks;

    /* Bandwidth counters. */
    OUT gctUINT                 readBytes8[8];
    OUT gctUINT                 writeBytes8[8];

    /* Counters. */
    OUT gctUINT                 cycles[8];
    OUT gctUINT                 idleCycles[8];
    OUT gctUINT                 mcCycles[8];
    OUT gctUINT                 readRequests[8];
    OUT gctUINT                 writeRequests[8];

    /* 3D counters. */
    OUT gctUINT                 vertexCount;
    OUT gctUINT                 primitiveCount;
    OUT gctUINT                 rejectedPrimitives;
    OUT gctUINT                 culledPrimitives;
    OUT gctUINT                 clippedPrimitives;
    OUT gctUINT                 outPrimitives;
    OUT gctUINT                 inPrimitives;
    OUT gctUINT                 culledQuadCount;
    OUT gctUINT                 totalQuadCount;
    OUT gctUINT                 quadCount;
    OUT gctUINT                 totalPixelCount;

    /* PE counters. */
    OUT gctUINT                 colorKilled[8];
    OUT gctUINT                 colorDrawn[8];
    OUT gctUINT                 depthKilled[8];
    OUT gctUINT                 depthDrawn[8];

    /* Shader counters. */
    OUT gctUINT                 shaderCycles;
    OUT gctUINT                 vsInstructionCount;
    OUT gctUINT                 vsTextureCount;
    OUT gctUINT                 psInstructionCount;
    OUT gctUINT                 psTextureCount;

    /* Texture counters. */
    OUT gctUINT                 bilinearRequests;
    OUT gctUINT                 trilinearRequests;
    OUT gctUINT                 txBytes8;
    OUT gctUINT                 txHitCount;
    OUT gctUINT                 txMissCount;
}
gcsHAL_FRAME_INFO;

#endif /* __gc_hal_types_h_ */
