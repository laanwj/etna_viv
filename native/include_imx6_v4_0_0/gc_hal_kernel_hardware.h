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


#ifndef __gc_hal_kernel_hardware_h_
#define __gc_hal_kernel_hardware_h_

#if gcdENABLE_VG
#include "gc_hal_kernel_hardware_vg.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* gckHARDWARE object. */
struct _gckHARDWARE
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to gctKERNEL object. */
    gckKERNEL                   kernel;

    /* Pointer to gctOS object. */
    gckOS                       os;

    /* Core */
    gceCORE                     core;

    /* Chip characteristics. */
    gcsHAL_QUERY_CHIP_IDENTITY  identity;
    gctBOOL                     allowFastClear;
    gctBOOL                     allowCompression;
    gctUINT32                   powerBaseAddress;
    gctBOOL                     extraEventStates;

    /* Big endian */
    gctBOOL                     bigEndian;

    /* Chip status */
    gctPOINTER                  powerMutex;
    gctUINT32                   powerProcess;
    gctUINT32                   powerThread;
    gceCHIPPOWERSTATE           chipPowerState;
    gctUINT32                   lastWaitLink;
    gctBOOL                     clockState;
    gctBOOL                     powerState;
    gctPOINTER                  globalSemaphore;

    gctISRMANAGERFUNC           startIsr;
    gctISRMANAGERFUNC           stopIsr;
    gctPOINTER                  isrContext;

    gctUINT32                   mmuVersion;

    /* Type */
    gceHARDWARE_TYPE            type;

#if gcdPOWEROFF_TIMEOUT
    gctUINT32                   powerOffTime;
    gctUINT32                   powerOffTimeout;
    gctPOINTER                  powerOffTimer;
#endif

    gctPOINTER                  pageTableDirty;

#if gcdENABLE_FSCALE_VAL_ADJUST
    /* FSCALE_VAL when gcvPOWER_ON. */
    gctUINT32                   powerOnFscaleVal;
#endif

#if gcdLINK_QUEUE_SIZE
    struct _gckLINKQUEUE        linkQueue;
#endif
};

gceSTATUS
gckHARDWARE_GetBaseAddress(
    IN gckHARDWARE Hardware,
    OUT gctUINT32_PTR BaseAddress
    );

gceSTATUS
gckHARDWARE_NeedBaseAddress(
    IN gckHARDWARE Hardware,
    IN gctUINT32 State,
    OUT gctBOOL_PTR NeedBase
    );

gceSTATUS
gckHARDWARE_GetFrameInfo(
    IN gckHARDWARE Hardware,
    OUT gcsHAL_FRAME_INFO * FrameInfo
    );

gceSTATUS
gckHARDWARE_SetFscaleValue(
    IN gckHARDWARE Hardware,
    IN gctUINT32   FscaleValue
    );

gceSTATUS
gckHARDWARE_GetFscaleValue(
    IN gckHARDWARE Hardware,
    IN gctUINT * FscaleValue,
    IN gctUINT * MinFscaleValue,
    IN gctUINT * MaxFscaleValue
    );

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_kernel_hardware_h_ */

