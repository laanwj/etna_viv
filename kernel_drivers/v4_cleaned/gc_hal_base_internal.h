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

#ifndef __gc_hal_base_internal_h_
#define __gc_hal_base_internal_h_

#include "gc_hal.h"
#include "gc_hal_types_internal.h"

#include "gc_hal_version.h"

#include "gc_hal_options_internal.h"

/******************************************************************************\
****************************** Object Declarations *****************************
\******************************************************************************/

typedef struct _gckOS *                 gckOS;
typedef struct _gcoHAL *                gcoHAL;
typedef struct _gcoOS *                 gcoOS;

typedef struct _gcsPOINT *              gcsPOINT_PTR;
typedef struct _gcsSIZE *               gcsSIZE_PTR;
typedef struct _gcsRECT *               gcsRECT_PTR;

#define gcmkOS_SAFE_FREE(os, mem) \
    gckOS_Free(os, mem); \
	mem = NULL

/*----------------------------------------------------------------------------*/
/*----- Profile --------------------------------------------------------------*/

gceSTATUS
gckOS_GetProfileTick(
    OUT u64 *Tick
    );

u32
gckOS_ProfileToMS(
    IN u64 Ticks
    );

#define _gcmPROFILE_INIT(prefix, freq, start) \
    do { \
        prefix ## OS_QueryProfileTickRate(&(freq)); \
        prefix ## OS_GetProfileTick(&(start)); \
    } while (gcvFALSE)

#define _gcmPROFILE_QUERY(prefix, start, ticks) \
    do { \
        prefix ## OS_GetProfileTick(&(ticks)); \
        (ticks) = ((ticks) > (start)) ? ((ticks) - (start)) \
                                      : (~0ull - (start) + (ticks) + 1); \
    } while (gcvFALSE)

#if gcdENABLE_PROFILING
#   define gcmkPROFILE_INIT(freq, start)    _gcmPROFILE_INIT(gck, freq, start)
#   define gcmkPROFILE_QUERY(start, ticks)  _gcmPROFILE_QUERY(gck, start, ticks)
#   define gcmPROFILE_INIT(freq, start)     _gcmPROFILE_INIT(gco, freq, start)
#   define gcmPROFILE_QUERY(start, ticks)   _gcmPROFILE_QUERY(gco, start, ticks)
#   define gcmPROFILE_ONLY(x)               x
#   define gcmPROFILE_ELSE(x)               do { } while (gcvFALSE)
#   define gcmPROFILE_DECLARE_ONLY(x)       x
#   define gcmPROFILE_DECLARE_ELSE(x)       typedef x
#else
#   define gcmkPROFILE_INIT(start, freq)    do { } while (gcvFALSE)
#   define gcmkPROFILE_QUERY(start, ticks)  do { } while (gcvFALSE)
#   define gcmPROFILE_INIT(start, freq)     do { } while (gcvFALSE)
#   define gcmPROFILE_QUERY(start, ticks)   do { } while (gcvFALSE)
#   define gcmPROFILE_ONLY(x)               do { } while (gcvFALSE)
#   define gcmPROFILE_ELSE(x)               x
#   define gcmPROFILE_DECLARE_ONLY(x)       typedef x
#   define gcmPROFILE_DECLARE_ELSE(x)       x
#endif

static inline int
gckMATH_ModuloInt(
    IN int X,
    IN int Y
    )
{
    if(Y ==0) {return 0;}
    else {return X % Y;}
}

/******************************************************************************\
**************************** Coordinate Structures *****************************
\******************************************************************************/

typedef struct _gcsPOINT
{
    s32                         x;
    s32                         y;
}
gcsPOINT;

typedef struct _gcsSIZE
{
    s32                         width;
    s32                         height;
}
gcsSIZE;

typedef struct _gcsRECT
{
    s32                         left;
    s32                         top;
    s32                         right;
    s32                         bottom;
}
gcsRECT;

typedef struct _gcsVIDMEM_NODE_SHARED_INFO
{
    int                         tileStatusDisabled;
    gcsPOINT                    SrcOrigin;
    gcsPOINT                    DestOrigin;
    gcsSIZE                     RectSize;
    u32                         clearValue;
}
gcsVIDMEM_NODE_SHARED_INFO;

/*******************************************************************************
**
**  gcmFATAL
**
**      Print a message to the debugger and execute a break point.
**
**  ARGUMENTS:
**
**      message Message.
**      ...     Optional arguments.
*/

void
gckOS_DebugFatal(
    IN const char *Message,
    ...
    );

#if gcmIS_DEBUG(gcdDEBUG_FATAL)
#   define gcmFATAL             gcoOS_DebugFatal
#   define gcmkFATAL            gckOS_DebugFatal
#else
#   define gcmFATAL(...)
#   define gcmkFATAL(...)
#endif

/*******************************************************************************
**
**  gcmTRACE
**
**      Print a message to the debugfer if the correct level has been set.  In
**      retail mode this macro does nothing.
**
**  ARGUMENTS:
**
**      level   Level of message.
**      message Message.
**      ...     Optional arguments.
*/
#define gcvLEVEL_NONE           -1
#define gcvLEVEL_ERROR          0
#define gcvLEVEL_WARNING        1
#define gcvLEVEL_INFO           2
#define gcvLEVEL_VERBOSE        3

void
gckOS_DebugTrace(
    IN u32 Level,
    IN const char *Message,
    ...
    );

void
gckOS_DebugTraceN(
    IN u32 Level,
    IN unsigned int ArgumentSize,
    IN const char *Message,
    ...
    );

#if gcmIS_DEBUG(gcdDEBUG_TRACE)
#   define gcmkTRACE            gckOS_DebugTrace
#   define gcmkTRACE_N          gckOS_DebugTraceN
#else
#   define gcmkTRACE(...)
#   define gcmkTRACE_N(...)
#endif

/* Zones common for kernel and user. */
#define gcvZONE_OS              (1 << 0)
#define gcvZONE_HARDWARE        (1 << 1)
#define gcvZONE_HEAP            (1 << 2)
#define gcvZONE_SIGNAL          (1 << 27)

/* Kernel zones. */
#define gcvZONE_KERNEL          (1 << 3)
#define gcvZONE_VIDMEM          (1 << 4)
#define gcvZONE_COMMAND         (1 << 5)
#define gcvZONE_DRIVER          (1 << 6)
#define gcvZONE_CMODEL          (1 << 7)
#define gcvZONE_MMU             (1 << 8)
#define gcvZONE_EVENT           (1 << 9)
#define gcvZONE_DEVICE          (1 << 10)
#define gcvZONE_DATABASE        (1 << 11)
#define gcvZONE_INTERRUPT       (1 << 12)


/* Handy zones. */
#define gcvZONE_NONE            0
#define gcvZONE_ALL             0x0FFFFFFF

/*******************************************************************************
**
**  gcmTRACE_ZONE
**
**      Print a message to the debugger if the correct level and zone has been
**      set.  In retail mode this macro does nothing.
**
**  ARGUMENTS:
**
**      Level   Level of message.
**      Zone    Zone of message.
**      Message Message.
**      ...     Optional arguments.
*/

void
gckOS_DebugTraceZone(
    IN u32 Level,
    IN u32 Zone,
    IN const char *Message,
    ...
    );

void
gckOS_DebugTraceZoneN(
    IN u32 Level,
    IN u32 Zone,
    IN unsigned int ArgumentSize,
    IN const char *Message,
    ...
    );

#if gcmIS_DEBUG(gcdDEBUG_TRACE)
#   define gcmkTRACE_ZONE           gckOS_DebugTraceZone
#   define gcmkTRACE_ZONE_N         gckOS_DebugTraceZoneN
#else
#   define gcmkTRACE_ZONE(...)
#   define gcmkTRACE_ZONE_N(...)
#endif

/*******************************************************************************
**
**  gcmDEBUG_ONLY
**
**      Execute a statement or function only in DEBUG mode.
**
**  ARGUMENTS:
**
**      f       Statement or function to execute.
*/
#if gcmIS_DEBUG(gcdDEBUG_CODE)
#   define gcmDEBUG_ONLY(f)         f
#else
#   define gcmDEBUG_ONLY(f)
#endif

/******************************************************************************\
******************************** Logging Macros ********************************
\******************************************************************************/

#define gcdHEADER_LEVEL             gcvLEVEL_VERBOSE

#define gcmkHEADER() \
    s8 __kernel__ = 1; \
    s8 *__kernel_ptr__ = &__kernel__; \
    gcmkTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                   "++%s(%d)", __FUNCTION__, __LINE__)

#define gcmkHEADER_ARG(Text, ...) \
    s8 __kernel__ = 1; \
    s8 *__kernel_ptr__ = &__kernel__; \
    gcmkTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                   "++%s(%d): " Text, __FUNCTION__, __LINE__, __VA_ARGS__)

#define gcmkFOOTER() \
    gcmkTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                   "--%s(%d): status=%d(%s)", \
                   __FUNCTION__, __LINE__, status, gckOS_DebugStatus2Name(status)); \
    *__kernel_ptr__ -= 1

#define gcmkFOOTER_NO() \
    gcmkTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                   "--%s(%d)", __FUNCTION__, __LINE__); \
    *__kernel_ptr__ -= 1

#define gcmkFOOTER_ARG(Text, ...) \
    gcmkTRACE_ZONE(gcdHEADER_LEVEL, _GC_OBJ_ZONE, \
                   "--%s(%d): " Text, \
                   __FUNCTION__, __LINE__, __VA_ARGS__); \
    *__kernel_ptr__ -= 1

#define gcmOPT_VALUE(ptr)           (((ptr) == NULL) ? 0 : *(ptr))
#define gcmOPT_POINTER(ptr)         (((ptr) == NULL) ? NULL : *(ptr))

void
gckOS_Print(
    IN const char *Message,
    ...
    );

void
gckOS_PrintN(
    IN unsigned int ArgumentSize,
    IN const char *Message,
    ...
    );

void
gckOS_CopyPrint(
    IN const char *Message,
    ...
    );

#define gcmkPRINT               gckOS_Print
#define gcmkPRINT_N             gckOS_PrintN

#if gcdPRINT_VERSION
#   define gcmPRINT_VERSION()       do { \
                                        _gcmPRINT_VERSION(gcm); \
                                        gcmSTACK_DUMP(); \
                                    } while (0)
#   define gcmkPRINT_VERSION()      _gcmPRINT_VERSION(gcmk)
#   define _gcmPRINT_VERSION(prefix) \
        prefix##TRACE(gcvLEVEL_ERROR, \
                      "Vivante HAL version %d.%d.%d build %d  %s  %s", \
                      gcvVERSION_MAJOR, gcvVERSION_MINOR, gcvVERSION_PATCH, \
                      gcvVERSION_BUILD, gcvVERSION_DATE, gcvVERSION_TIME )
#else
#   define gcmPRINT_VERSION()       do { gcmSTACK_DUMP(); } while (gcvFALSE)
#   define gcmkPRINT_VERSION()      do { } while (gcvFALSE)
#endif

typedef enum _gceDUMP_BUFFER
{
    gceDUMP_BUFFER_CONTEXT,
    gceDUMP_BUFFER_USER,
    gceDUMP_BUFFER_KERNEL,
    gceDUMP_BUFFER_LINK,
    gceDUMP_BUFFER_WAITLINK,
    gceDUMP_BUFFER_FROM_USER,
}
gceDUMP_BUFFER;

void
gckOS_DumpBuffer(
    IN gckOS Os,
    IN void *Buffer,
    IN unsigned int Size,
    IN gceDUMP_BUFFER Type,
    IN int CopyMessage
    );

#define gcmkDUMPBUFFER          gckOS_DumpBuffer

#if gcdDUMP_COMMAND
#   define gcmkDUMPCOMMAND(Os, Buffer, Size, Type, CopyMessage) \
        gcmkDUMPBUFFER(Os, Buffer, Size, Type, CopyMessage)
#else
#   define gcmkDUMPCOMMAND(Os, Buffer, Size, Type, CopyMessage)
#endif

#if gcmIS_DEBUG(gcdDEBUG_CODE)

void
gckOS_DebugFlush(
    const char *CallerName,
    unsigned int LineNumber,
    u32 DmaAddress
    );

#   define gcmkDEBUGFLUSH(DmaAddress) \
        gckOS_DebugFlush(__FUNCTION__, __LINE__, DmaAddress)
#else
#   define gcmkDEBUGFLUSH(DmaAddress)
#endif

/*******************************************************************************
**
**  gcmBREAK
**
**      Break into the debugger.  In retail mode this macro does nothing.
**
**  ARGUMENTS:
**
**      None.
*/

void
gckOS_DebugBreak(
    void
    );

#if gcmIS_DEBUG(gcdDEBUG_BREAK)
#   define gcmkBREAK            gckOS_DebugBreak
#else
#   define gcmkBREAK()
#endif

/*******************************************************************************
**
**  gcmASSERT
**
**      Evaluate an expression and break into the debugger if the expression
**      evaluates to false.  In retail mode this macro does nothing.
**
**  ARGUMENTS:
**
**      exp     Expression to evaluate.
*/
#if gcmIS_DEBUG(gcdDEBUG_ASSERT)
#   define _gcmASSERT(prefix, exp) \
        do \
        { \
            if (!(exp)) \
            { \
                prefix##TRACE(gcvLEVEL_ERROR, \
                              #prefix "ASSERT at %s(%d)", \
                              __FUNCTION__, __LINE__); \
                prefix##TRACE(gcvLEVEL_ERROR, \
                              "(%s)", #exp); \
                prefix##BREAK(); \
            } \
        } \
        while (gcvFALSE)
#   define gcmASSERT(exp)           _gcmASSERT(gcm, exp)
#   define gcmkASSERT(exp)          _gcmASSERT(gcmk, exp)
#else
#   define gcmASSERT(exp)
#   define gcmkASSERT(exp)
#endif

/*******************************************************************************
**
**  gcmVERIFY
**
**      Verify if an expression returns true.  If the expression does not
**      evaluates to true, an assertion will happen in debug mode.
**
**  ARGUMENTS:
**
**      exp     Expression to evaluate.
*/
#if gcmIS_DEBUG(gcdDEBUG_ASSERT)
#   define gcmVERIFY(exp)           gcmASSERT(exp)
#   define gcmkVERIFY(exp)          gcmkASSERT(exp)
#else
#   define gcmVERIFY(exp)           exp
#   define gcmkVERIFY(exp)          exp
#endif

/*******************************************************************************
**
**  gcmVERIFY_OK
**
**      Verify a fucntion returns gcvSTATUS_OK.  If the function does not return
**      gcvSTATUS_OK, an assertion will happen in debug mode.
**
**  ARGUMENTS:
**
**      func    Function to evaluate.
*/
void
gckOS_Verify(
    IN gceSTATUS Status
    );

#if gcmIS_DEBUG(gcdDEBUG_ASSERT)
#   define gcmkVERIFY_OK(func) \
        do \
        { \
            gceSTATUS verifyStatus = func; \
            if (verifyStatus != gcvSTATUS_OK) \
            { \
                gcmkTRACE( \
                    gcvLEVEL_ERROR, \
                    "gcmkVERIFY_OK(%d): function returned %d", \
                    __LINE__, verifyStatus \
                    ); \
            } \
            gckOS_Verify(verifyStatus); \
            gcmkASSERT(verifyStatus == gcvSTATUS_OK); \
        } \
        while (gcvFALSE)
#else
#   define gcmVERIFY_OK(func)       func
#   define gcmkVERIFY_OK(func)      func
#endif

const char *
gckOS_DebugStatus2Name(
    gceSTATUS status
    );

/*******************************************************************************
**
**  gcmERR_BREAK
**
**      Executes a break statement on error.
**
**  ASSUMPTIONS:
**
**      'status' variable of gceSTATUS type must be defined.
**
**  ARGUMENTS:
**
**      func    Function to evaluate.
*/
#define _gcmkERR_BREAK(prefix, func) \
    status = func; \
    if (gcmIS_ERROR(status)) \
    { \
        prefix##PRINT_VERSION(); \
        prefix##TRACE(gcvLEVEL_ERROR, \
            #prefix "ERR_BREAK: status=%d(%s) @ %s(%d)", \
            status, gckOS_DebugStatus2Name(status), __FUNCTION__, __LINE__); \
        break; \
    } \
    do { } while (gcvFALSE)
#define gcmkERR_BREAK(func)         _gcmkERR_BREAK(gcmk, func)

/*******************************************************************************
**
**  gcmONERROR
**
**      Jump to the error handler in case there is an error.
**
**  ASSUMPTIONS:
**
**      'status' variable of gceSTATUS type must be defined.
**
**  ARGUMENTS:
**
**      func    Function to evaluate.
*/
#define _gcmkONERROR(prefix, func) \
    do \
    { \
        status = func; \
        if (gcmIS_ERROR(status)) \
        { \
            prefix##PRINT_VERSION(); \
            prefix##TRACE(gcvLEVEL_ERROR, \
                #prefix "ONERROR: status=%d(%s) @ %s(%d)", \
                status, gckOS_DebugStatus2Name(status), __FUNCTION__, __LINE__); \
            goto OnError; \
        } \
    } \
    while (gcvFALSE)
#define gcmkONERROR(func)           _gcmkONERROR(gcmk, func)

/*******************************************************************************
**
**  gcmVERIFY_ARGUMENT
**
**      Assert if an argument does not apply to the specified expression.  If
**      the argument evaluates to false, gcvSTATUS_INVALID_ARGUMENT will be
**      returned from the current function.  In retail mode this macro does
**      nothing.
**
**  ARGUMENTS:
**
**      arg     Argument to evaluate.
*/
#   define _gcmVERIFY_ARGUMENT(prefix, arg) \
       do \
       { \
           if (!(arg)) \
           { \
               prefix##TRACE(gcvLEVEL_ERROR, #prefix "VERIFY_ARGUMENT failed:"); \
               prefix##ASSERT(arg); \
               prefix##FOOTER_ARG("status=%d", gcvSTATUS_INVALID_ARGUMENT); \
               return gcvSTATUS_INVALID_ARGUMENT; \
           } \
       } \
       while (gcvFALSE)
#   define gcmkVERIFY_ARGUMENT(arg)    _gcmVERIFY_ARGUMENT(gcmk, arg)

/*******************************************************************************
**
**  gcmDEBUG_VERIFY_ARGUMENT
**
**      Works just like gcmVERIFY_ARGUMENT, but is only valid in debug mode.
**      Use this to verify arguments inside non-public API functions.
*/
#if gcdDEBUG
#   define gcmkDEBUG_VERIFY_ARGUMENT(arg)   _gcmkVERIFY_ARGUMENT(gcm, arg)
#else
#   define gcmkDEBUG_VERIFY_ARGUMENT(arg)
#endif
/*******************************************************************************
**
**  gcmVERIFY_ARGUMENT_RETURN
**
**      Assert if an argument does not apply to the specified expression.  If
**      the argument evaluates to false, gcvSTATUS_INVALID_ARGUMENT will be
**      returned from the current function.  In retail mode this macro does
**      nothing.
**
**  ARGUMENTS:
**
**      arg     Argument to evaluate.
*/
#   define _gcmVERIFY_ARGUMENT_RETURN(prefix, arg, value) \
       do \
       { \
           if (!(arg)) \
           { \
               prefix##TRACE(gcvLEVEL_ERROR, \
                             #prefix "gcmVERIFY_ARGUMENT_RETURN failed:"); \
               prefix##ASSERT(arg); \
               prefix##FOOTER_ARG("value=%d", value); \
               return value; \
           } \
       } \
       while (gcvFALSE)
#   define gcmkVERIFY_ARGUMENT_RETURN(arg, value) \
                _gcmVERIFY_ARGUMENT_RETURN(gcmk, arg, value)

#endif /* __gc_hal_base_internal_h_ */
