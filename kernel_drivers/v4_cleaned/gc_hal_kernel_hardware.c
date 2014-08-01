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



#ifdef CONFIG_MACH_JZ4770
#include <linux/sched.h>
#endif

#include "gc_hal.h"
#include "gc_hal_internal.h"
#include "gc_hal_kernel.h"

#include <linux/kernel.h>

#define _GC_OBJ_ZONE    gcvZONE_HARDWARE

/******************************************************************************\
********************************* Support Code *********************************
\******************************************************************************/
static gceSTATUS
_ResetGPU(
    IN gckHARDWARE Hardware,
    IN gckOS Os,
    IN gceCORE Core
    );

static gceSTATUS
_IdentifyHardware(
    IN gckOS Os,
    IN gceCORE Core,
    OUT struct _gcsHAL_QUERY_CHIP_IDENTITY *Identity
    )
{
    gceSTATUS status;

    u32 chipIdentity;

    u32 streamCount = 0;
    u32 registerMax = 0;
    u32 threadCount = 0;
    u32 shaderCoreCount = 0;
    u32 vertexCacheSize = 0;
    u32 vertexOutputBufferSize = 0;
    u32 pixelPipes = 0;
    u32 instructionCount = 0;
    u32 numConstants = 0;
    u32 bufferSize = 0;

    gcmkHEADER_ARG("Os=0x%x", Os);

    /***************************************************************************
    ** Get chip ID and revision.
    */

    /* Read chip identity register. */
    gcmkONERROR(
        gckOS_ReadRegisterEx(Os, Core,
                             0x00018,
                             &chipIdentity));

    /* Special case for older graphic cores. */
    if (gcmVERIFYFIELDVALUE(chipIdentity, 31:24, 0x01 ))
    {
        Identity->chipModel    = gcv500;
        Identity->chipRevision = gcmGETFIELD(chipIdentity, 15:12);
    }

    else
    {
        /* Read chip identity register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x00020,
                                 (u32 *) &Identity->chipModel));

        /* !!!! HACK ALERT !!!! */
        /* Because people change device IDs without letting software know
        ** about it - here is the hack to make it all look the same.  Only
        ** for GC400 family.  Next time - TELL ME!!! */
        if (((Identity->chipModel & 0xFF00) == 0x0400)
          && (Identity->chipModel != 0x0420))
        {
            Identity->chipModel = (gceCHIPMODEL) (Identity->chipModel & 0x0400);
        }

        /* Read CHIP_REV register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x00024,
                                 &Identity->chipRevision));

        if ((Identity->chipModel    == gcv300)
        &&  (Identity->chipRevision == 0x2201)
        )
        {
            u32 chipDate;
            u32 chipTime;

            /* Read date and time registers. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x00028,
                                     &chipDate));

            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x0002C,
                                     &chipTime));

            if ((chipDate == 0x20080814) && (chipTime == 0x12051100))
            {
                /* This IP has an ECO; put the correct revision in it. */
                Identity->chipRevision = 0x1051;
            }
        }
    }

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipModel=%X",
                   Identity->chipModel);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipRevision=%X",
                   Identity->chipRevision);


    /***************************************************************************
    ** Get chip features.
    */

    /* Read chip feature register. */
    gcmkONERROR(
        gckOS_ReadRegisterEx(Os, Core,
                             0x0001C,
                             &Identity->chipFeatures));

#if !VIVANTE_NO_3D
    /* Disable fast clear on GC700. */
    if (Identity->chipModel == gcv700)
    {
        Identity->chipFeatures
            = gcmSETFIELD(Identity->chipFeatures, 0:0, 0x0 );
    }
#endif

    if (((Identity->chipModel == gcv500) && (Identity->chipRevision < 2))
    ||  ((Identity->chipModel == gcv300) && (Identity->chipRevision < 0x2000))
    )
    {
        /* GC500 rev 1.x and GC300 rev < 2.0 doesn't have these registers. */
        Identity->chipMinorFeatures  = 0;
        Identity->chipMinorFeatures1 = 0;
        Identity->chipMinorFeatures2 = 0;
        Identity->chipMinorFeatures3 = 0;
    }
    else
    {
        /* Read chip minor feature register #0. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x00034,
                                 &Identity->chipMinorFeatures));

        if (gcmVERIFYFIELDVALUE(Identity->chipMinorFeatures, 21:21, 0x1 )
        )
        {
            /* Read chip minor featuress register #1. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x00074,
                                     &Identity->chipMinorFeatures1));

            /* Read chip minor featuress register #2. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x00084,
                                     &Identity->chipMinorFeatures2));

            /*Identity->chipMinorFeatures2 &= ~(0x1 << 3);*/

            /* Read chip minor featuress register #1. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Os, Core,
                                     0x00088,
                                     &Identity->chipMinorFeatures3));
        }
        else
        {
            /* Chip doesn't has minor features register #1 or 2 or 3. */
            Identity->chipMinorFeatures1 = 0;
            Identity->chipMinorFeatures2 = 0;
            Identity->chipMinorFeatures3 = 0;
        }
    }

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipFeatures=0x%08X",
                   Identity->chipFeatures);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipMinorFeatures=0x%08X",
                   Identity->chipMinorFeatures);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipMinorFeatures1=0x%08X",
                   Identity->chipMinorFeatures1);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipMinorFeatures2=0x%08X",
                   Identity->chipMinorFeatures2);

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Identity: chipMinorFeatures3=0x%08X",
                   Identity->chipMinorFeatures3);

    /***************************************************************************
    ** Get chip specs.
    */

    if (gcmVERIFYFIELDVALUE(Identity->chipMinorFeatures, 21:21, 0x1 ))
    {
        u32 specs, specs2;

        /* Read gcChipSpecs register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x00048,
                                 &specs));

        /* Extract the fields. */
        streamCount            = gcmGETFIELD(specs, 3:0);
        registerMax            = gcmGETFIELD(specs, 7:4);
        threadCount            = gcmGETFIELD(specs, 11:8);
        shaderCoreCount        = gcmGETFIELD(specs, 24:20);
        vertexCacheSize        = gcmGETFIELD(specs, 16:12);
        vertexOutputBufferSize = gcmGETFIELD(specs, 31:28);
        pixelPipes             = gcmGETFIELD(specs, 27:25);

        /* Read gcChipSpecs2 register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Os, Core,
                                 0x00080,
                                 &specs2));

        instructionCount       = gcmGETFIELD(specs2, 15:8);
        numConstants           = gcmGETFIELD(specs2, 31:16);
        bufferSize             = gcmGETFIELD(specs2, 7:0);
    }

    /* Get the number of pixel pipes. */
    Identity->pixelPipes = max(pixelPipes, 1u);

    /* Get the stream count. */
    Identity->streamCount = (streamCount != 0)
                          ? streamCount
                          : (Identity->chipModel >= gcv1000) ? 4 : 1;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: streamCount=%u%s",
                   Identity->streamCount,
                   (streamCount == 0) ? " (default)" : "");

    /* Get the vertex output buffer size. */
    Identity->vertexOutputBufferSize = (vertexOutputBufferSize != 0)
                                     ? 1 << vertexOutputBufferSize
                                     : (Identity->chipModel == gcv400)
                                       ? (Identity->chipRevision < 0x4000) ? 512
                                       : (Identity->chipRevision < 0x4200) ? 256
                                       : 128
                                     : (Identity->chipModel == gcv530)
                                       ? (Identity->chipRevision < 0x4200) ? 512
                                       : 128
                                     : 512;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: vertexOutputBufferSize=%u%s",
                   Identity->vertexOutputBufferSize,
                   (vertexOutputBufferSize == 0) ? " (default)" : "");

    /* Get the maximum number of threads. */
    Identity->threadCount = (threadCount != 0)
                          ? 1 << threadCount
                          : (Identity->chipModel == gcv400) ? 64
                          : (Identity->chipModel == gcv500) ? 128
                          : (Identity->chipModel == gcv530) ? 128
                          : 256;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: threadCount=%u%s",
                   Identity->threadCount,
                   (threadCount == 0) ? " (default)" : "");

    /* Get the number of shader cores. */
    Identity->shaderCoreCount = (shaderCoreCount != 0)
                              ? shaderCoreCount
                              : (Identity->chipModel >= gcv1000) ? 2
                              : 1;
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: shaderCoreCount=%u%s",
                   Identity->shaderCoreCount,
                   (shaderCoreCount == 0) ? " (default)" : "");

    /* Get the vertex cache size. */
    Identity->vertexCacheSize = (vertexCacheSize != 0)
                              ? vertexCacheSize
                              : 8;
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: vertexCacheSize=%u%s",
                   Identity->vertexCacheSize,
                   (vertexCacheSize == 0) ? " (default)" : "");

    /* Get the maximum number of temporary registers. */
    Identity->registerMax = (registerMax != 0)
        /* Maximum of registerMax/4 registers are accessible to 1 shader */
                          ? 1 << registerMax
                          : (Identity->chipModel == gcv400) ? 32
                          : 64;
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: registerMax=%u%s",
                   Identity->registerMax,
                   (registerMax == 0) ? " (default)" : "");

    /* Get the instruction count. */
    Identity->instructionCount = (instructionCount == 0) ? 256
                               : (instructionCount == 1) ? 1024
                               : (instructionCount == 2) ? 2048
                               : 256;

    if (Identity->chipModel == gcv2000 && Identity->chipRevision == 0x5108)
    {
        Identity->instructionCount = 512;
    }

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: instructionCount=%u%s",
                   Identity->instructionCount,
                   (instructionCount == 0) ? " (default)" : "");

    /* Get the number of constants. */
    Identity->numConstants = numConstants;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: numConstants=%u%s",
                   Identity->numConstants,
                   (numConstants == 0) ? " (default)" : "");

    /* Get the buffer size. */
    Identity->bufferSize = bufferSize;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Specs: bufferSize=%u%s",
                   Identity->bufferSize,
                   (bufferSize == 0) ? " (default)" : "");

    /* Success. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if gcdPOWEROFF_TIMEOUT
static void
_PowerTimerFunction(
    void *Data
    )
{
    gckHARDWARE hardware = (gckHARDWARE)Data;
    gcmkVERIFY_OK(
        gckHARDWARE_SetPowerManagementState(hardware, gcvPOWER_OFF_TIMEOUT));
}
#endif

/******************************************************************************\
****************************** gckHARDWARE API code *****************************
\******************************************************************************/

/*******************************************************************************
**
**  gckHARDWARE_Construct
**
**  Construct a new gckHARDWARE object.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an initialized gckOS object.
**
**      gceCORE Core
**          Specified core.
**
**  OUTPUT:
**
**      gckHARDWARE * Hardware
**          Pointer to a variable that will hold the pointer to the gckHARDWARE
**          object.
*/
gceSTATUS
gckHARDWARE_Construct(
    IN gckOS Os,
    IN gceCORE Core,
    OUT gckHARDWARE * Hardware
    )
{
    gceSTATUS status;
    gckHARDWARE hardware = NULL;
    u16 data = 0xff00;
    void *pointer = NULL;

    gcmkHEADER_ARG("Os=0x%x", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Hardware != NULL);

    /* Enable the GPU. */
    gcmkONERROR(gckOS_SetGPUPower(Os, gcvTRUE, gcvTRUE));
    gcmkONERROR(gckOS_WriteRegisterEx(Os, Core, 0x00000, 0));

    /* Allocate the gckHARDWARE object. */
    gcmkONERROR(gckOS_Allocate(Os,
                               sizeof(struct _gckHARDWARE),
                               &pointer));

    hardware = (gckHARDWARE) pointer;

    /* Initialize the gckHARDWARE object. */
    hardware->object.type = gcvOBJ_HARDWARE;
    hardware->os          = Os;
    hardware->core        = Core;

    /* Identify the hardware. */
    gcmkONERROR(_IdentifyHardware(Os, Core, &hardware->identity));

    /* Determine the hardware type */
    switch (hardware->identity.chipModel)
    {
    case gcv350:
    case gcv355:
        hardware->type = gcvHARDWARE_VG;
        break;

    case gcv300:
    case gcv320:
        hardware->type = gcvHARDWARE_2D;
        break;

    default:
        hardware->type = gcvHARDWARE_3D;

        if (gcmGETFIELD(hardware->identity.chipFeatures, 9:9))
        {
            hardware->type = (gceHARDWARE_TYPE) (hardware->type | gcvHARDWARE_2D);
        }
    }

    hardware->powerBaseAddress
        = ((hardware->identity.chipModel   == gcv300)
        && (hardware->identity.chipRevision < 0x2000))
            ? 0x0100
            : 0x0000;

    /* _ResetGPU need powerBaseAddress. */
    status = _ResetGPU(hardware, Os, Core);

    if (status != gcvSTATUS_OK)
    {
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
            "_ResetGPU failed: status=%d\n", status);
    }

    hardware->powerMutex = NULL;

    hardware->mmuVersion
        = gcmGETFIELD(hardware->identity.chipMinorFeatures1, 28:28);

    /* Determine whether bug fixes #1 are present. */
    hardware->extraEventStates = gcmVERIFYFIELDVALUE(hardware->identity.chipMinorFeatures1, 3:3, 0x0 );

    /* Check if big endian */
    hardware->bigEndian = (*(u8 *)&data == 0xff);

    /* Initialize the fast clear. */
    gcmkONERROR(gckHARDWARE_SetFastClear(hardware, -1, -1));

#if !gcdENABLE_128B_MERGE && 1 && 1

    if (gcmVERIFYFIELDVALUE(hardware->identity.chipMinorFeatures2, 21:21, 0x1  ))
    {
        /* 128B merge is turned on by default. Disable it. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os, Core, 0x00558, 0));
    }

#endif

    /* Set power state to ON. */
    hardware->chipPowerState  = gcvPOWER_ON;
    hardware->clockState      = gcvTRUE;
    hardware->powerState      = gcvTRUE;
    hardware->lastWaitLink    = ~0U;
    hardware->globalSemaphore = NULL;

    gcmkONERROR(gckOS_CreateMutex(Os, &hardware->powerMutex));
    gcmkONERROR(gckOS_CreateSemaphore(Os, &hardware->globalSemaphore));

#if gcdPOWEROFF_TIMEOUT
    hardware->powerOffTimeout = gcdPOWEROFF_TIMEOUT;

    gcmkVERIFY_OK(gckOS_CreateTimer(Os,
                                    (void *)_PowerTimerFunction,
                                    (void *)hardware,
                                    &hardware->powerOffTimer));
#endif

    gcmkONERROR(gckOS_AtomConstruct(Os, &hardware->pageTableDirty));

    /* Return pointer to the gckHARDWARE object. */
    *Hardware = hardware;

    /* Success. */
    gcmkFOOTER_ARG("*Hardware=0x%x", *Hardware);
    return gcvSTATUS_OK;

OnError:
    /* Roll back. */
    if (hardware != NULL)
    {
        /* Turn off the power. */
        gcmkVERIFY_OK(gckOS_SetGPUPower(Os, gcvFALSE, gcvFALSE));

        if (hardware->globalSemaphore != NULL)
        {
            /* Destroy the global semaphore. */
            gcmkVERIFY_OK(gckOS_DestroySemaphore(Os,
                                                 hardware->globalSemaphore));
        }

        if (hardware->powerMutex != NULL)
        {
            /* Destroy the power mutex. */
            gcmkVERIFY_OK(gckOS_DeleteMutex(Os, hardware->powerMutex));
        }

#if gcdPOWEROFF_TIMEOUT
        if (hardware->powerOffTimer != NULL)
        {
            gcmkVERIFY_OK(gckOS_StopTimer(Os, hardware->powerOffTimer));
            gcmkVERIFY_OK(gckOS_DestoryTimer(Os, hardware->powerOffTimer));
        }
#endif

        if (hardware->pageTableDirty != NULL)
        {
            gcmkVERIFY_OK(gckOS_AtomDestroy(Os, hardware->pageTableDirty));
        }

        gcmkVERIFY_OK(gcmkOS_SAFE_FREE(Os, hardware));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_Destroy
**
**  Destroy an gckHARDWARE object.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object that needs to be destroyed.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_Destroy(
    IN gckHARDWARE Hardware
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Turn off the power. */
    gcmkVERIFY_OK(gckOS_SetGPUPower(Hardware->os, gcvFALSE, gcvFALSE));

    /* Destroy the power semaphore. */
    gcmkVERIFY_OK(gckOS_DestroySemaphore(Hardware->os,
                                         Hardware->globalSemaphore));

    /* Destroy the power mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Hardware->os, Hardware->powerMutex));

#if gcdPOWEROFF_TIMEOUT
    gcmkVERIFY_OK(gckOS_StopTimer(Hardware->os, Hardware->powerOffTimer));
    gcmkVERIFY_OK(gckOS_DestoryTimer(Hardware->os, Hardware->powerOffTimer));
#endif

    gcmkVERIFY_OK(gckOS_AtomDestroy(Hardware->os, Hardware->pageTableDirty));

    /* Mark the object as unknown. */
    Hardware->object.type = gcvOBJ_UNKNOWN;

    /* Free the object. */
    gcmkONERROR(gcmkOS_SAFE_FREE(Hardware->os, Hardware));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_GetType
**
**  Get the hardware type.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**  OUTPUT:
**
**      gceHARDWARE_TYPE * Type
**          Pointer to a variable that receives the type of hardware object.
*/
gceSTATUS
gckHARDWARE_GetType(
    IN gckHARDWARE Hardware,
    OUT gceHARDWARE_TYPE * Type
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);
    gcmkVERIFY_ARGUMENT(Type != NULL);

    *Type = Hardware->type;

    gcmkFOOTER_ARG("*Type=%d", *Type);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_InitializeHardware
**
**  Initialize the hardware.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_InitializeHardware(
    IN gckHARDWARE Hardware
    )
{
    gceSTATUS status;
    u32 baseAddress;
    u32 chipRev;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Read the chip revision register. */
    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00024,
                                     &chipRev));

    if (chipRev != Hardware->identity.chipRevision)
    {
        /* Chip is not there! */
        gcmkONERROR(gcvSTATUS_CONTEXT_LOSSED);
    }

    /* Disable isolate GPU bit. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00000,
                                      gcmSETFIELD(0x00000100, 19:19, 0)));

    /* Reset memory counters. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0003C,
                                      ~0U));

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0003C,
                                      0));

    /* Get the system's physical base address. */
    gcmkONERROR(gckOS_GetBaseAddress(Hardware->os, &baseAddress));

    /* Program the base addesses. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0041C,
                                      baseAddress));

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00418,
                                      baseAddress));

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00428,
                                      baseAddress));

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00420,
                                      baseAddress));

    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00424,
                                      baseAddress));

#if !VIVANTE_PROFILER && 1
    {
        u32 data;

        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         Hardware->powerBaseAddress +
                                         0x00100,
                                         &data));

        /* Enable clock gating. */
        data = gcmSETFIELD(data, 0:0, 1);

        if ((Hardware->identity.chipRevision == 0x4301)
        ||  (Hardware->identity.chipRevision == 0x4302)
        )
        {
            /* Disable stall module level clock gating for 4.3.0.1 and 4.3.0.2
            ** revisions. */
            data = gcmSETFIELD(data, 1:1, 1);
        }

        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          Hardware->powerBaseAddress
                                          + 0x00100,
                                          data));

#if !VIVANTE_NO_3D
        /* Disable PE clock gating on revs < 5.0 when HZ is present without a
        ** bug fix. */
        if ((Hardware->identity.chipRevision < 0x5000)
        &&  gcmVERIFYFIELDVALUE(Hardware->identity.chipMinorFeatures1, 9:9, 0x0 )
        &&  gcmVERIFYFIELDVALUE(Hardware->identity.chipMinorFeatures, 27:27, 0x1 )
        )
        {
            gcmkONERROR(
                gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     Hardware->powerBaseAddress
                                     + 0x00104,
                                     &data));

            /* Disable PE clock gating. */
            data = gcmSETFIELD(data, 2:2, 1);

            gcmkONERROR(
                gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      Hardware->powerBaseAddress
                                      + 0x00104,
                                      data));
        }

#endif
    }
#endif

    /* Special workaround for this core
    ** Make sure pulse eater kicks in only when SH is idle */
    if (Hardware->identity.chipModel == gcv4000 &&
        Hardware->identity.chipRevision == 0x5208)
    {
		gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x0010C,
                                  gcmSETFIELD(0x01590880, 23:23, 1)));
    }

    /* Special workaround for this core
    ** Make sure FE and TX are on different buses */
    if ((Hardware->identity.chipModel == gcv2000)
    &&  (Hardware->identity.chipRevision  == 0x5108))
    {
        u32 data;

        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00480,
                                 &data));

        /* Set FE bus to one, TX bus to zero */
        data = gcmSETFIELD(data, 3:3, 1);
        data = gcmSETFIELD(data, 7:7, 0);

        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x00480,
                                  data));
    }

    /* Test if MMU is initialized. */
    if ((Hardware->kernel      != NULL)
    &&  (Hardware->kernel->mmu != NULL)
    )
    {
        /* Reset MMU. */
        if (Hardware->mmuVersion == 0)
        {
            gcmkONERROR(
                    gckHARDWARE_SetMMU(Hardware,
                        Hardware->kernel->mmu->pageTableLogical));
        }
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the error. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_QueryMemory
**
**  Query the amount of memory available on the hardware.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**  OUTPUT:
**
**      size_t * InternalSize
**          Pointer to a variable that will hold the size of the internal video
**          memory in bytes.  If 'InternalSize' is NULL, no information of the
**          internal memory will be returned.
**
**      u32 * InternalBaseAddress
**          Pointer to a variable that will hold the hardware's base address for
**          the internal video memory.  This pointer cannot be NULL if
**          'InternalSize' is also non-NULL.
**
**      u32 * InternalAlignment
**          Pointer to a variable that will hold the hardware's base address for
**          the internal video memory.  This pointer cannot be NULL if
**          'InternalSize' is also non-NULL.
**
**      size_t * ExternalSize
**          Pointer to a variable that will hold the size of the external video
**          memory in bytes.  If 'ExternalSize' is NULL, no information of the
**          external memory will be returned.
**
**      u32 * ExternalBaseAddress
**          Pointer to a variable that will hold the hardware's base address for
**          the external video memory.  This pointer cannot be NULL if
**          'ExternalSize' is also non-NULL.
**
**      u32 * ExternalAlignment
**          Pointer to a variable that will hold the hardware's base address for
**          the external video memory.  This pointer cannot be NULL if
**          'ExternalSize' is also non-NULL.
**
**      u32 * HorizontalTileSize
**          Number of horizontal pixels per tile.  If 'HorizontalTileSize' is
**          NULL, no horizontal pixel per tile will be returned.
**
**      u32 * VerticalTileSize
**          Number of vertical pixels per tile.  If 'VerticalTileSize' is
**          NULL, no vertical pixel per tile will be returned.
*/
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
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    if (InternalSize != NULL)
    {
        /* No internal memory. */
        *InternalSize = 0;
    }

    if (ExternalSize != NULL)
    {
        /* No external memory. */
        *ExternalSize = 0;
    }

    if (HorizontalTileSize != NULL)
    {
        /* 4x4 tiles. */
        *HorizontalTileSize = 4;
    }

    if (VerticalTileSize != NULL)
    {
        /* 4x4 tiles. */
        *VerticalTileSize = 4;
    }

    /* Success. */
    gcmkFOOTER_ARG("*InternalSize=%lu *InternalBaseAddress=0x%08x "
                   "*InternalAlignment=0x%08x *ExternalSize=%lu "
                   "*ExternalBaseAddress=0x%08x *ExtenalAlignment=0x%08x "
                   "*HorizontalTileSize=%u *VerticalTileSize=%u",
                   gcmOPT_VALUE(InternalSize),
                   gcmOPT_VALUE(InternalBaseAddress),
                   gcmOPT_VALUE(InternalAlignment),
                   gcmOPT_VALUE(ExternalSize),
                   gcmOPT_VALUE(ExternalBaseAddress),
                   gcmOPT_VALUE(ExternalAlignment),
                   gcmOPT_VALUE(HorizontalTileSize),
                   gcmOPT_VALUE(VerticalTileSize));
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_QueryChipIdentity
**
**  Query the identity of the hardware.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**  OUTPUT:
**
**      struct _gcsHAL_QUERY_CHIP_IDENTITY *Identity
**          Pointer to the identity structure.
**
*/
gceSTATUS
gckHARDWARE_QueryChipIdentity(
    IN gckHARDWARE Hardware,
    OUT struct _gcsHAL_QUERY_CHIP_IDENTITY *Identity
    )
{
    u32 features;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Identity != NULL);

    /* Return chip model and revision. */
    Identity->chipModel = Hardware->identity.chipModel;
    Identity->chipRevision = Hardware->identity.chipRevision;

    /* Return feature set. */
    features = Hardware->identity.chipFeatures;

    if (gcmGETFIELD(features, 0:0))
    {
        /* Override fast clear by command line. */
        features = gcmSETFIELD(features, 0:0, Hardware->allowFastClear);
    }

    if (gcmGETFIELD(features, 5:5))
    {
        /* Override compression by command line. */
        features = gcmSETFIELD(features, 5:5, Hardware->allowCompression);
    }

    /* Mark 2D pipe as available for GC500.0 through GC500.2 and GC300,
    ** since they did not have this bit. */
    if (((Hardware->identity.chipModel == gcv500) && (Hardware->identity.chipRevision <= 2))
    ||   (Hardware->identity.chipModel == gcv300)
    )
    {
        features = gcmSETFIELD(features, 9:9, 0x1 );
    }

    Identity->chipFeatures = features;

    /* Return minor features. */
    Identity->chipMinorFeatures  = Hardware->identity.chipMinorFeatures;
    Identity->chipMinorFeatures1 = Hardware->identity.chipMinorFeatures1;
    Identity->chipMinorFeatures2 = Hardware->identity.chipMinorFeatures2;
    Identity->chipMinorFeatures3 = Hardware->identity.chipMinorFeatures3;

    /* Return chip specs. */
    Identity->streamCount            = Hardware->identity.streamCount;
    Identity->registerMax            = Hardware->identity.registerMax;
    Identity->threadCount            = Hardware->identity.threadCount;
    Identity->shaderCoreCount        = Hardware->identity.shaderCoreCount;
    Identity->vertexCacheSize        = Hardware->identity.vertexCacheSize;
    Identity->vertexOutputBufferSize = Hardware->identity.vertexOutputBufferSize;
    Identity->pixelPipes             = Hardware->identity.pixelPipes;
    Identity->instructionCount       = Hardware->identity.instructionCount;
    Identity->numConstants           = Hardware->identity.numConstants;
    Identity->bufferSize             = Hardware->identity.bufferSize;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_SplitMemory
**
**  Split a hardware specific memory address into a pool and offset.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**      u32 Address
**          Address in hardware specific format.
**
**  OUTPUT:
**
**      gcePOOL * Pool
**          Pointer to a variable that will hold the pool type for the address.
**
**      u32 * Offset
**          Pointer to a variable that will hold the offset for the address.
*/
gceSTATUS
gckHARDWARE_SplitMemory(
    IN gckHARDWARE Hardware,
    IN u32 Address,
    OUT gcePOOL * Pool,
    OUT u32 * Offset
    )
{
    gcmkHEADER_ARG("Hardware=0x%x Addres=0x%08x", Hardware, Address);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Pool != NULL);
    gcmkVERIFY_ARGUMENT(Offset != NULL);

    /* Dispatch on memory type. */
    switch (gcmGETFIELD(Address, 31:31))
    {
    case 0x0:
        /* System memory. */
        *Pool = gcvPOOL_SYSTEM;
        break;

    case 0x1:
        /* Virtual memory. */
        *Pool = gcvPOOL_VIRTUAL;
        break;

    default:
        /* Invalid memory type. */
        gcmkFOOTER_ARG("status=%d", gcvSTATUS_INVALID_ARGUMENT);
        return gcvSTATUS_INVALID_ARGUMENT;
    }

    /* Return offset of address. */
    *Offset = gcmGETFIELD(Address, 30:0);

    /* Success. */
    gcmkFOOTER_ARG("*Pool=%d *Offset=0x%08x", *Pool, *Offset);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_Execute
**
**  Kickstart the hardware's command processor with an initialized command
**  buffer.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**      void *Logical
**          Logical address of command buffer.
**
**      size_t Bytes
**          Number of bytes for the prefetch unit (until after the first LINK).
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_Execute(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN size_t Bytes
    )
{
    gceSTATUS status;
    u32 address = 0, control;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Bytes=%lu",
                   Hardware, Logical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Logical != NULL);

    /* Convert logical into hardware specific address. */
    gcmkONERROR(
        gckHARDWARE_ConvertLogical(Hardware, Logical, &address));

    /* Enable all events. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00014, ~0U));

    /* Write address register. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00654, address));

    /* Build control register. */
    control = gcmSETFIELD(0, 16:16, 0x1 )
            | gcmSETFIELD(0, 15:0, (Bytes + 7) >> 3);

    /* Set big endian */
    if (Hardware->bigEndian)
    {
        control |= gcmSETFIELD(0, 21:20, 0x2 );
    }

    /* Write control register. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00658, control));

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                  "Started command buffer @ 0x%08x",
                  address);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_WaitLink
**
**  Append a WAIT/LINK command sequence at the specified location in the command
**  queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      void *Logical
**          Pointer to the current location inside the command queue to append
**          WAIT/LINK command sequence at or NULL just to query the size of the
**          WAIT/LINK command sequence.
**
**      u32 Offset
**          Offset into command buffer required for alignment.
**
**      size_t * Bytes
**          Pointer to the number of bytes available for the WAIT/LINK command
**          sequence.  If 'Logical' is NULL, this argument will be ignored.
**
**  OUTPUT:
**
**      size_t * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          by the WAIT/LINK command sequence.  If 'Bytes' is NULL, nothing will
**          be returned.
**
**      u32 * WaitOffset
**          Pointer to a variable that will receive the offset of the WAIT command
**          from the specified logcial pointer.
**          If 'WaitOffset' is NULL nothing will be returned.
**
**      size_t * WaitSize
**          Pointer to a variable that will receive the number of bytes used by
**          the WAIT command.  If 'LinkSize' is NULL nothing will be returned.
*/
gceSTATUS
gckHARDWARE_WaitLink(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN u32 Offset,
    IN OUT size_t * Bytes,
    OUT u32 * WaitOffset,
    OUT size_t * WaitSize
    )
{
    static const unsigned int waitCount = 200;

    gceSTATUS status;
    u32 address;
    u32 *logical;
    size_t bytes;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Offset=0x%08x *Bytes=%lu",
                   Hardware, Logical, Offset, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical != NULL) || (Bytes != NULL));

    /* Compute number of bytes required. */
#if gcd6000_SUPPORT
    bytes = gcmALIGN(Offset + 96, 8) - Offset;
#else
    bytes = gcmALIGN(Offset + 16, 8) - Offset;
#endif

    /* Cast the input pointer. */
    logical = (u32 *) Logical;

    if (logical != NULL)
    {
        /* Not enough space? */
        if (*Bytes < bytes)
        {
            /* Command queue too small. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }

        /* Convert logical into hardware specific address. */
        gcmkONERROR(gckHARDWARE_ConvertLogical(Hardware, logical, &address));

        /* Store the WAIT/LINK address. */
        Hardware->lastWaitLink = address;

        /* Append WAIT(count). */
        logical[0]
            = gcmSETFIELD(0, 31:27, 0x07 )
            | gcmSETFIELD(0, 15:0, waitCount);

#if gcd6000_SUPPORT
        /* Send FE-PE sempahore token. */
        logical[2]
            = gcmSETFIELD(0, 31:27, 0x01 )
            | gcmSETFIELD(0, 25:16, 1)
            | gcmSETFIELD(0, 15:0, 0x0E02);

        logical[3]
            = gcmSETFIELD(0, 4:0, 0x01 )
            | gcmSETFIELD(0, 12:8, 0x07 );

        /* Send FE-PE stall token. */
        logical[4]
            = gcmSETFIELD(0, 31:27, 0x01 )
            | gcmSETFIELD(0, 25:16, 1)
            | gcmSETFIELD(0, 15:0, 0x0F00);

        logical[5]
            = gcmSETFIELD(0, 4:0, 0x01 )
            | gcmSETFIELD(0, 12:8, 0x07 );

        /*************************************************************/
        /* Enable chip ID 0. */
        logical[6] =
            gcmSETFIELD(0, 31:27, 0x0D )
            | (1 << 0);

        /* Send semaphore from FE to ChipID 1. */
        logical[8] =
              gcmSETFIELD(0, 31:27, 0x01 )
            | gcmSETFIELD(0, 25:16, 1)
            | gcmSETFIELD(0, 15:0, 0x0E02);

        logical[9] =
              gcmSETFIELD(0, 4:0, 0x01 )
            | gcmSETFIELD(0, 12:8, 0x0F )
            | gcmSETFIELD(0, 27:24, 1);

        /* Send semaphore from FE to ChipID 1. */
        logical[10] =
              gcmSETFIELD(0, 31:27, 0x09 );

        logical[11] =
              gcmSETFIELD(0, 4:0, 0x01 )
            | gcmSETFIELD(0, 12:8, 0x0F )
            | gcmSETFIELD(0, 27:24, 0);

        /*************************************************************/
        /* Enable chip ID 1. */
        logical[12] =
            gcmSETFIELD(0, 31:27, 0x0D )
            | (1 << 1);

        /* Send semaphore from FE to ChipID 1. */
        logical[14] =
              gcmSETFIELD(0, 31:27, 0x01 )
            | gcmSETFIELD(0, 25:16, 1)
            | gcmSETFIELD(0, 15:0, 0x0E02);

        logical[15] =
              gcmSETFIELD(0, 4:0, 0x01 )
            | gcmSETFIELD(0, 12:8, 0x0F )
            | gcmSETFIELD(0, 27:24, 0);

        /* Wait for semaphore from ChipID 0. */
        logical[16] =
              gcmSETFIELD(0, 31:27, 0x09 );

        logical[17] =
              gcmSETFIELD(0, 4:0, 0x01 )
            | gcmSETFIELD(0, 12:8, 0x0F )
            | gcmSETFIELD(0, 27:24, 1);

        /*************************************************************/
        /* Enable all chips. */
        logical[18] =
            gcmSETFIELD(0, 31:27, 0x0D )
            | (0xFFFF);

        /* LoadState(AQFlush, 1), flush. */
        logical[20]
            = gcmSETFIELD(0, 31:27, 0x01 )
            | gcmSETFIELD(0, 15:0, 0x0E03)
            | gcmSETFIELD(0, 25:16, 1);

        logical[21]
            = gcmSETFIELD(0, 6:6, 0x1 );

        /* Append LINK(2, address). */
        logical[22]
            = gcmSETFIELD(0, 31:27, 0x08 )
            | gcmSETFIELD(0, 15:0, bytes >> 3);

        logical[23] = address;
#else
        /* Append LINK(2, address). */
        logical[2]
            = gcmSETFIELD(0, 31:27, 0x08 )
            | gcmSETFIELD(0, 15:0, bytes >> 3);

        logical[3] = address;

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_HARDWARE,
            "0x%08x: WAIT %u", address, waitCount
            );

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_HARDWARE,
            "0x%08x: LINK 0x%08x, #%lu",
            address + 8, address, bytes
            );
#endif

        if (WaitOffset != NULL)
        {
            /* Return the offset pointer to WAIT command. */
            *WaitOffset = 0;
        }

        if (WaitSize != NULL)
        {
            /* Return number of bytes used by the WAIT command. */
            *WaitSize = 8;
        }
    }

    if (Bytes != NULL)
    {
        /* Return number of bytes required by the WAIT/LINK command
        ** sequence. */
        *Bytes = bytes;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu *WaitOffset=0x%x *WaitSize=%lu",
                   gcmOPT_VALUE(Bytes), gcmOPT_VALUE(WaitOffset),
                   gcmOPT_VALUE(WaitSize));
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_End
**
**  Append an END command at the specified location in the command queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      void *Logical
**          Pointer to the current location inside the command queue to append
**          END command at or NULL just to query the size of the END command.
**
**      size_t * Bytes
**          Pointer to the number of bytes available for the END command.  If
**          'Logical' is NULL, this argument will be ignored.
**
**  OUTPUT:
**
**      size_t * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the END command.  If 'Bytes' is NULL, nothing will be returned.
*/
gceSTATUS
gckHARDWARE_End(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN OUT size_t * Bytes
    )
{
    u32 *logical = (u32 *) Logical;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x *Bytes=%lu",
                   Hardware, Logical, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical == NULL) || (Bytes != NULL));

    if (Logical != NULL)
    {
        if (*Bytes < 8)
        {
            /* Command queue too small. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }

        /* Append END. */
       logical[0] =
            gcmSETFIELD(0, 31:27, 0x02 );

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE, "0x%x: END", Logical);

        /* Make sure the CPU writes out the data to memory. */
        gcmkONERROR(
            gckOS_MemoryBarrier(Hardware->os, Logical));
    }

    if (Bytes != NULL)
    {
        /* Return number of bytes required by the END command. */
        *Bytes = 8;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu", gcmOPT_VALUE(Bytes));
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_Nop
**
**  Append a NOP command at the specified location in the command queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      void *Logical
**          Pointer to the current location inside the command queue to append
**          NOP command at or NULL just to query the size of the NOP command.
**
**      size_t * Bytes
**          Pointer to the number of bytes available for the NOP command.  If
**          'Logical' is NULL, this argument will be ignored.
**
**  OUTPUT:
**
**      size_t * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the NOP command.  If 'Bytes' is NULL, nothing will be returned.
*/
gceSTATUS
gckHARDWARE_Nop(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN OUT size_t * Bytes
    )
{
    u32 *logical = (u32 *) Logical;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x *Bytes=%lu",
                   Hardware, Logical, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical == NULL) || (Bytes != NULL));

    if (Logical != NULL)
    {
        if (*Bytes < 8)
        {
            /* Command queue too small. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }

        /* Append NOP. */
        logical[0] = gcmSETFIELD(0, 31:27, 0x03 );

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE, "0x%x: NOP", Logical);
    }

    if (Bytes != NULL)
    {
        /* Return number of bytes required by the NOP command. */
        *Bytes = 8;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu", gcmOPT_VALUE(Bytes));
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_Event
**
**  Append an EVENT command at the specified location in the command queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      void *Logical
**          Pointer to the current location inside the command queue to append
**          the EVENT command at or NULL just to query the size of the EVENT
**          command.
**
**      u8 Event
**          Event ID to program.
**
**      gceKERNEL_WHERE FromWhere
**          Location of the pipe to send the event.
**
**      size_t * Bytes
**          Pointer to the number of bytes available for the EVENT command.  If
**          'Logical' is NULL, this argument will be ignored.
**
**  OUTPUT:
**
**      size_t * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the EVENT command.  If 'Bytes' is NULL, nothing will be
**          returned.
*/
gceSTATUS
gckHARDWARE_Event(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN u8 Event,
    IN gceKERNEL_WHERE FromWhere,
    IN OUT size_t * Bytes
    )
{
    unsigned int size;
    u32 destination = 0;
    u32 *logical = (u32 *) Logical;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Event=%u FromWhere=%d *Bytes=%lu",
                   Hardware, Logical, Event, FromWhere, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical == NULL) || (Bytes != NULL));
    gcmkVERIFY_ARGUMENT(Event < 32);

    /* Determine the size of the command. */

#if gcdUSE_OPENCL
	/* Temporary workaround for lost events */
    size = gcmALIGN(8 + (1 + 5) * 4 * 20, 8); /* EVENT + 100 STATES */
#else
    size = (Hardware->extraEventStates && (FromWhere == gcvKERNEL_PIXEL))
         ? gcmALIGN(8 + (1 + 5) * 4, 8) /* EVENT + 5 STATES */
         : 8;
#endif

    if (Logical != NULL)
    {
        if (*Bytes < size)
        {
            /* Command queue too small. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }

        switch (FromWhere)
        {
        case gcvKERNEL_COMMAND:
            /* From command processor. */
#if gcdUSE_OPENCL
            /* Send all events via PE */
            destination = gcmSETFIELD(0, 6:6, 0x1 );
#else
            destination = gcmSETFIELD(0, 5:5, 0x1 );
#endif
            break;

        case gcvKERNEL_PIXEL:
            /* From pixel engine. */
            destination = gcmSETFIELD(0, 6:6, 0x1 );
            break;

        default:
            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }

        /* Append EVENT(Event, destiantion). */
        logical[0] = gcmSETFIELD(0, 31:27, 0x01 )
                   | gcmSETFIELD(0, 15:0, 0x0E01)
                   | gcmSETFIELD(0, 25:16, 1);

        logical[1] = gcmSETFIELD(destination, 4:0, Event);

        /* Make sure the event ID gets written out before GPU can access it. */
        gcmkONERROR(
            gckOS_MemoryBarrier(Hardware->os, logical + 1));

#if gcmIS_DEBUG(gcdDEBUG_TRACE)
        {
            u32 phys;
            gckOS_GetPhysicalAddress(Hardware->os, Logical, &phys);
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                           "0x%08x: EVENT %d", phys, Event);
        }
#endif

        /* Append the extra states. These are needed for the chips that do not
        ** support back-to-back events due to the async interface. The extra
        ** states add the necessary delay to ensure that event IDs do not
        ** collide. */
        if (size > 8)
        {
#if gcdUSE_OPENCL
            unsigned int i;

            for (i = 0; i < 20; i++)
            {
                logical[i*6+2] = gcmSETFIELD(0, 31:27, 0x01 )
                               | gcmSETFIELD(0, 15:0, 0x0100)
                               | gcmSETFIELD(0, 25:16, 5);
                logical[i*6+3] = 0;
                logical[i*6+4] = 0;
                logical[i*6+5] = 0;
                logical[i*6+6] = 0;
                logical[i*6+7] = 0;
            }
#else
            logical[2] = gcmSETFIELD(0, 31:27, 0x01 )
                       | gcmSETFIELD(0, 15:0, 0x0100)
                       | gcmSETFIELD(0, 25:16, 5);
            logical[3] = 0;
            logical[4] = 0;
            logical[5] = 0;
            logical[6] = 0;
            logical[7] = 0;
#endif
        }
    }

    if (Bytes != NULL)
    {
        /* Return number of bytes required by the EVENT command. */
        *Bytes = size;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu", gcmOPT_VALUE(Bytes));
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_PipeSelect
**
**  Append a PIPESELECT command at the specified location in the command queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      void *Logical
**          Pointer to the current location inside the command queue to append
**          the PIPESELECT command at or NULL just to query the size of the
**          PIPESELECT command.
**
**      gcePIPE_SELECT Pipe
**          Pipe value to select.
**
**      size_t * Bytes
**          Pointer to the number of bytes available for the PIPESELECT command.
**          If 'Logical' is NULL, this argument will be ignored.
**
**  OUTPUT:
**
**      size_t * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the PIPESELECT command.  If 'Bytes' is NULL, nothing will be
**          returned.
*/
gceSTATUS
gckHARDWARE_PipeSelect(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN gcePIPE_SELECT Pipe,
    IN OUT size_t * Bytes
    )
{
    u32 *logical = (u32 *) Logical;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Pipe=%d *Bytes=%lu",
                   Hardware, Logical, Pipe, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical == NULL) || (Bytes != NULL));

    /* Append a PipeSelect. */
    if (Logical != NULL)
    {
        u32 flush, stall;

        if (*Bytes < 32)
        {
            /* Command queue too small. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }

        flush = (Pipe == gcvPIPE_2D)
              ? gcmSETFIELD(0, 1:1, 0x1 )
              | gcmSETFIELD(0, 0:0, 0x1 )
              : gcmSETFIELD(0, 3:3, 0x1 );

        stall = gcmSETFIELD(0, 4:0, 0x01 )
              | gcmSETFIELD(0, 12:8, 0x07 );

        /* LoadState(AQFlush, 1), flush. */
        logical[0]
            = gcmSETFIELD(0, 31:27, 0x01 )
            | gcmSETFIELD(0, 15:0, 0x0E03)
            | gcmSETFIELD(0, 25:16, 1);

        logical[1]
            = flush;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: FLUSH 0x%x", logical, flush);

        /* LoadState(AQSempahore, 1), stall. */
        logical[2]
            = gcmSETFIELD(0, 31:27, 0x01 )
            | gcmSETFIELD(0, 25:16, 1)
            | gcmSETFIELD(0, 15:0, 0x0E02);

        logical[3]
            = stall;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: SEMAPHORE 0x%x", logical + 2, stall);

        /* Stall, stall. */
        logical[4] = gcmSETFIELD(0, 31:27, 0x09 );
        logical[5] = stall;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: STALL 0x%x", logical + 4, stall);

        /* LoadState(AQPipeSelect, 1), pipe. */
        logical[6]
            = gcmSETFIELD(0, 31:27, 0x01 )
            | gcmSETFIELD(0, 15:0, 0x0E00)
            | gcmSETFIELD(0, 25:16, 1);

        logical[7] = (Pipe == gcvPIPE_2D)
            ? 0x1
            : 0x0;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "0x%x: PIPE %d", logical + 6, Pipe);
    }

    if (Bytes != NULL)
    {
        /* Return number of bytes required by the PIPESELECT command. */
        *Bytes = 32;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu", gcmOPT_VALUE(Bytes));
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_Link
**
**  Append a LINK command at the specified location in the command queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      void *Logical
**          Pointer to the current location inside the command queue to append
**          the LINK command at or NULL just to query the size of the LINK
**          command.
**
**      void *FetchAddress
**          Logical address of destination of LINK.
**
**      size_t FetchSize
**          Number of bytes in destination of LINK.
**
**      size_t * Bytes
**          Pointer to the number of bytes available for the LINK command.  If
**          'Logical' is NULL, this argument will be ignored.
**
**  OUTPUT:
**
**      size_t * Bytes
**          Pointer to a variable that will receive the number of bytes required
**          for the LINK command.  If 'Bytes' is NULL, nothing will be returned.
*/
gceSTATUS
gckHARDWARE_Link(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN void *FetchAddress,
    IN size_t FetchSize,
    IN OUT size_t * Bytes
    )
{
    gceSTATUS status;
    size_t bytes;
    u32 address;
    u32 link;
    u32 *logical = (u32 *) Logical;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x FetchAddress=0x%x FetchSize=%lu "
                   "*Bytes=%lu",
                   Hardware, Logical, FetchAddress, FetchSize,
                   gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT((Logical == NULL) || (Bytes != NULL));

    if (Logical != NULL)
    {
        if (*Bytes < 8)
        {
            /* Command queue too small. */
            gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
        }

        /* Convert logical address to hardware address. */
        gcmkONERROR(
            gckHARDWARE_ConvertLogical(Hardware, FetchAddress, &address));

        gcmkONERROR(
            gckOS_WriteMemory(Hardware->os, logical + 1, address));

        /* Make sure the address got written before the LINK command. */
        gcmkONERROR(
            gckOS_MemoryBarrier(Hardware->os, logical + 1));

        /* Compute number of 64-byte aligned bytes to fetch. */
        bytes = gcmALIGN(address + FetchSize, 64) - address;

        /* Append LINK(bytes / 8), FetchAddress. */
        link = gcmSETFIELD(0, 31:27, 0x08 )
             | gcmSETFIELD(0, 15:0, bytes >> 3);

        gcmkONERROR(
            gckOS_WriteMemory(Hardware->os, logical, link));

        /* Memory barrier. */
        gcmkONERROR(
            gckOS_MemoryBarrier(Hardware->os, logical));
    }

    if (Bytes != NULL)
    {
        /* Return number of bytes required by the LINK command. */
        *Bytes = 8;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu", gcmOPT_VALUE(Bytes));
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_UpdateQueueTail
**
**  Update the tail of the command queue.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      void *Logical
**          Logical address of the start of the command queue.
**
**      u32 Offset
**          Offset into the command queue of the tail (last command).
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_UpdateQueueTail(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    IN u32 Offset
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x Offset=0x%08x",
                   Hardware, Logical, Offset);

    /* Verify the hardware. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Force a barrier. */
    gcmkONERROR(
        gckOS_MemoryBarrier(Hardware->os, Logical));

    /* Notify gckKERNEL object of change. */
    gcmkONERROR(
        gckKERNEL_Notify(Hardware->kernel,
                         gcvNOTIFY_COMMAND_QUEUE,
                         gcvFALSE));

    if (status == gcvSTATUS_CHIP_NOT_READY)
    {
        gcmkONERROR(gcvSTATUS_GPU_NOT_RESPONDING);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_ConvertLogical
**
**  Convert a logical system address into a hardware specific address.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      void *Logical
**          Logical address to convert.
**
**      u32* Address
**          Return hardware specific address.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_ConvertLogical(
    IN gckHARDWARE Hardware,
    IN void *Logical,
    OUT u32 * Address
    )
{
    u32 address;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x", Hardware, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Logical != NULL);
    gcmkVERIFY_ARGUMENT(Address != NULL);

    /* Convert logical address into a physical address. */
    gcmkONERROR(
        gckOS_GetPhysicalAddress(Hardware->os, Logical, &address));

    /* Return hardware specific address. */
    *Address = (Hardware->mmuVersion == 0)
             ? gcmSETFIELD(0, 31:31, 0x0 )
               | gcmSETFIELD(0, 30:0, address)
             : address;

    /* Success. */
    gcmkFOOTER_ARG("*Address=0x%08x", *Address);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_Interrupt
**
**  Process an interrupt.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      int InterruptValid
**          If gcvTRUE, this function will read the interrupt acknowledge
**          register, stores the data, and return whether or not the interrupt
**          is ours or not.  If gcvFALSE, this functions will read the interrupt
**          acknowledge register and combine it with any stored value to handle
**          the event notifications.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_Interrupt(
    IN gckHARDWARE Hardware,
    IN int InterruptValid
    )
{
    gckEVENT eventObj;
    u32 data;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x InterruptValid=%d", Hardware, InterruptValid);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Extract gckEVENT object. */
    eventObj = Hardware->kernel->eventObj;
    gcmkVERIFY_OBJECT(eventObj, gcvOBJ_EVENT);

    if (InterruptValid)
    {
        /* Read AQIntrAcknowledge register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00010,
                                 &data));

        if (data & 0x80000000)
        {
            gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_HARDWARE, "AXI BUS ERROR");
        }

        if (data == 0)
        {
            /* Not our interrupt. */
            status = gcvSTATUS_NOT_OUR_INTERRUPT;
        }
        else
        {
            /* Inform gckEVENT of the interrupt. */
            status = gckEVENT_Interrupt(eventObj, data & 0x7FFFFFFF);
        }
    }
    else
    {
        /* Handle events. */
        status = gckEVENT_Notify(eventObj, 0);
    }

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_QueryCommandBuffer
**
**  Query the command buffer alignment and number of reserved bytes.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**  OUTPUT:
**
**      size_t * Alignment
**          Pointer to a variable receiving the alignment for each command.
**
**      size_t * ReservedHead
**          Pointer to a variable receiving the number of reserved bytes at the
**          head of each command buffer.
**
**      size_t * ReservedTail
**          Pointer to a variable receiving the number of bytes reserved at the
**          tail of each command buffer.
*/
gceSTATUS
gckHARDWARE_QueryCommandBuffer(
    IN gckHARDWARE Hardware,
    OUT size_t * Alignment,
    OUT size_t * ReservedHead,
    OUT size_t * ReservedTail
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    if (Alignment != NULL)
    {
        /* Align every 8 bytes. */
        *Alignment = 8;
    }

    if (ReservedHead != NULL)
    {
        /* Reserve space for SelectPipe(). */
        *ReservedHead = 32;
    }

    if (ReservedTail != NULL)
    {
        /* Reserve space for Link(). */
        *ReservedTail = 8;
    }

    /* Success. */
    gcmkFOOTER_ARG("*Alignment=%lu *ReservedHead=%lu *ReservedTail=%lu",
                   gcmOPT_VALUE(Alignment), gcmOPT_VALUE(ReservedHead),
                   gcmOPT_VALUE(ReservedTail));
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckHARDWARE_QuerySystemMemory
**
**  Query the command buffer alignment and number of reserved bytes.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**  OUTPUT:
**
**      size_t * SystemSize
**          Pointer to a variable that receives the maximum size of the system
**          memory.
**
**      u32 * SystemBaseAddress
**          Poinetr to a variable that receives the base address for system
**          memory.
*/
gceSTATUS
gckHARDWARE_QuerySystemMemory(
    IN gckHARDWARE Hardware,
    OUT size_t * SystemSize,
    OUT u32 * SystemBaseAddress
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    if (SystemSize != NULL)
    {
        /* Maximum system memory can be 2GB. */
        *SystemSize = 1U << 31;
    }

    if (SystemBaseAddress != NULL)
    {
        /* Set system memory base address. */
        *SystemBaseAddress = gcmSETFIELD(0, 31:31, 0x0 );
    }

    /* Success. */
    gcmkFOOTER_ARG("*SystemSize=%lu *SystemBaseAddress=%lu",
                   gcmOPT_VALUE(SystemSize), gcmOPT_VALUE(SystemBaseAddress));
    return gcvSTATUS_OK;
}

#if !VIVANTE_NO_3D
/*******************************************************************************
**
**  gckHARDWARE_QueryShaderCaps
**
**  Query the shader capabilities.
**
**  INPUT:
**
**      Nothing.
**
**  OUTPUT:
**
**      unsigned int * VertexUniforms
**          Pointer to a variable receiving the number of uniforms in the vertex
**          shader.
**
**      unsigned int * FragmentUniforms
**          Pointer to a variable receiving the number of uniforms in the
**          fragment shader.
**
**      unsigned int * Varyings
**          Pointer to a variable receiving the maimum number of varyings.
*/
gceSTATUS
gckHARDWARE_QueryShaderCaps(
    IN gckHARDWARE Hardware,
    OUT unsigned int * VertexUniforms,
    OUT unsigned int * FragmentUniforms,
    OUT unsigned int * Varyings
    )
{
    gcmkHEADER_ARG("Hardware=0x%x VertexUniforms=0x%x "
                   "FragmentUniforms=0x%x Varyings=0x%x",
                   Hardware, VertexUniforms,
                   FragmentUniforms, Varyings);

    if (VertexUniforms != NULL)
    {
		/* Return the vs shader const count. */
        if (Hardware->identity.chipModel < gcv4000)
        {
            *VertexUniforms = 168;
        }
        else
        {
            *VertexUniforms = 256;
        }
    }

    if (FragmentUniforms != NULL)
    {
		/* Return the ps shader const count. */
        if (Hardware->identity.chipModel < gcv4000)
        {
            *FragmentUniforms = 64;
        }
        else
        {
            *FragmentUniforms = 256;
        }
    }

    if (Varyings != NULL)
    {
		/* Return the shader varyings count. */
        if (gcmVERIFYFIELDVALUE(Hardware->identity.chipMinorFeatures1, 23:23, 0x1 ))
        {
		    *Varyings = 12;
        }
        else
        {
		    *Varyings = 8;
        }
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}
#endif

/*******************************************************************************
**
**  gckHARDWARE_SetMMU
**
**  Set the page table base address.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**      void *Logical
**          Logical address of the page table.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_SetMMU(
    IN gckHARDWARE Hardware,
    IN void *Logical
    )
{
    gceSTATUS status;
    u32 address = 0;
    u32 baseAddress;

    gcmkHEADER_ARG("Hardware=0x%x Logical=0x%x", Hardware, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Logical != NULL);

    /* Convert the logical address into an hardware address. */
    gcmkONERROR(
        gckHARDWARE_ConvertLogical(Hardware, Logical, &address));

    /* Also get the base address - we need a real physical address. */
    gcmkONERROR(
        gckOS_GetBaseAddress(Hardware->os, &baseAddress));

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Setting page table to 0x%08X",
                   address + baseAddress);

    /* Write the AQMemoryFePageTable register. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os,
                              Hardware->core,
                              0x00400,
                              address + baseAddress));

    /* Write the AQMemoryRaPageTable register. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os,
                              Hardware->core,
                              0x00410,
                              address + baseAddress));

    /* Write the AQMemoryTxPageTable register. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os,
                              Hardware->core,
                              0x00404,
                              address + baseAddress));


    /* Write the AQMemoryPePageTable register. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os,
                              Hardware->core,
                              0x00408,
                              address + baseAddress));

    /* Write the AQMemoryPezPageTable register. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os,
                              Hardware->core,
                              0x0040C,
                              address + baseAddress));

    /* Return the status. */
    gcmkFOOTER_NO();
    return status;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_FlushMMU
**
**  Flush the page table.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_FlushMMU(
    IN gckHARDWARE Hardware
    )
{
    gceSTATUS status;
    gckCOMMAND command;
    u32 *buffer;
    size_t bufferSize;
    int commitEntered = gcvFALSE;
    void *pointer = NULL;
    u32 flushSize;
    u32 count;
    u32 physical;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Verify the gckCOMMAND object pointer. */
    command = Hardware->kernel->command;

    /* Acquire the command queue. */
    gcmkONERROR(gckCOMMAND_EnterCommit(command, gcvFALSE));
    commitEntered = gcvTRUE;

    /* Flush the memory controller. */
    if (Hardware->mmuVersion == 0)
    {
        gcmkONERROR(gckCOMMAND_Reserve(
            command, 8, &pointer, &bufferSize
            ));

        buffer = (u32 *) pointer;

        buffer[0]
            = gcmSETFIELD(0, 31:27, 0x01 )
            | gcmSETFIELD(0, 15:0, 0x0E04)
            | gcmSETFIELD(0, 25:16, 1);

        buffer[1]
            = gcmSETFIELD(0, 0:0, 0x1 )
            | gcmSETFIELD(0, 1:1, 0x1 )
            | gcmSETFIELD(0, 2:2, 0x1 )
            | gcmSETFIELD(0, 3:3, 0x1 )
            | gcmSETFIELD(0, 4:4, 0x1 );

        gcmkONERROR(gckCOMMAND_Execute(command, 8));
    }
    else
    {
        flushSize =  16 * 4;

        gcmkONERROR(gckCOMMAND_Reserve(
            command, flushSize, &pointer, &bufferSize
            ));

        buffer = (u32 *) pointer;

        count = (bufferSize - flushSize + 7) >> 3;

        gcmkONERROR(gckOS_GetPhysicalAddress(command->os, buffer, &physical));

        /* Flush cache. */
        buffer[0]
            = gcmSETFIELD(0, 31:27, 0x01 )
            | gcmSETFIELD(0, 25:16, 1)
            | gcmSETFIELD(0, 15:0, 0x0E03);

        buffer[1]
            = gcmSETFIELD(0, 3:3, 0x1 )
            | gcmSETFIELD(0, 1:1, 0x1 )
            | gcmSETFIELD(0, 2:2, 0x1 )
            | gcmSETFIELD(0, 4:4, 0x1 )
            | gcmSETFIELD(0, 5:5, 0x1 )
            | gcmSETFIELD(0, 6:6, 0x1 );

        /* Arm the PE-FE Semaphore. */
        buffer[2]
            = gcmSETFIELD(0, 31:27, 0x01 )
            | gcmSETFIELD(0, 25:16, 1)
            | gcmSETFIELD(0, 15:0, 0x0E02);

        buffer[3]
            = gcmSETFIELD(0, 4:0, 0x01 )
            | gcmSETFIELD(0, 12:8, 0x07 );

        /* STALL FE until PE is done flushing. */
        buffer[4]
            = gcmSETFIELD(0, 31:27, 0x09 );

        buffer[5]
            = gcmSETFIELD(0, 4:0, 0x01 )
            | gcmSETFIELD(0, 12:8, 0x07 );

        /* LINK to next slot to flush FE FIFO. */
        buffer[6]
            = gcmSETFIELD(0, 31:27, 0x08 )
            | gcmSETFIELD(0, 15:0, 4);

        buffer[7]
            = physical + 8 * sizeof(u32);

        /* Flush MMU cache. */
        buffer[8]
            = gcmSETFIELD(0, 31:27, 0x01 )
            | gcmSETFIELD(0, 15:0, 0x0061)
            | gcmSETFIELD(0, 25:16, 1);

        buffer[9]
            = (gcmSETFIELD(~0, 4:4, 0x1 ) &  gcmSETFIELD(~0, 7:7, 0x0 ) );

        /* Arm the PE-FE Semaphore. */
        buffer[10]
            = gcmSETFIELD(0, 31:27, 0x01 )
            | gcmSETFIELD(0, 25:16, 1)
            | gcmSETFIELD(0, 15:0, 0x0E02);

        buffer[11]
            = gcmSETFIELD(0, 4:0, 0x01 )
            | gcmSETFIELD(0, 12:8, 0x07 );

        /* STALL FE until PE is done flushing. */
        buffer[12]
            = gcmSETFIELD(0, 31:27, 0x09 );

        buffer[13]
            = gcmSETFIELD(0, 4:0, 0x01 )
            | gcmSETFIELD(0, 12:8, 0x07 );

        /* LINK to next slot to flush FE FIFO. */
        buffer[14]
            = gcmSETFIELD(0, 31:27, 0x08 )
            | gcmSETFIELD(0, 15:0, count);

        buffer[15]
            = physical + flushSize;

        gcmkONERROR(gckCOMMAND_Execute(command, flushSize));
    }

    /* Release the command queue. */
    gcmkONERROR(gckCOMMAND_ExitCommit(command, gcvFALSE));
    commitEntered = gcvFALSE;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (commitEntered)
    {
        /* Release the command queue mutex. */
        gcmkVERIFY_OK(gckCOMMAND_ExitCommit(Hardware->kernel->command,
                                            gcvFALSE));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_SetMMUv2
**
**  Set the page table base address.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_SetMMUv2(
    IN gckHARDWARE Hardware,
    IN int Enable,
    IN void *MtlbAddress,
    IN gceMMU_MODE Mode,
    IN void *SafeAddress,
    IN int FromPower
    )
{
    gceSTATUS status;
    u32 config, address;
    gckCOMMAND command;
    u32 *buffer;
    size_t bufferSize;
    int commitEntered = gcvFALSE;
    void *pointer = NULL;

    gcmkHEADER_ARG("Hardware=0x%x Enable=%d", Hardware, Enable);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Convert logical address into physical address. */
    gcmkONERROR(
        gckOS_GetPhysicalAddress(Hardware->os, MtlbAddress, &config));

    gcmkONERROR(
        gckOS_GetPhysicalAddress(Hardware->os, SafeAddress, &address));

    if (address & 0x3F)
    {
        gcmkONERROR(gcvSTATUS_NOT_ALIGNED);
    }

    switch (Mode)
    {
    case gcvMMU_MODE_1K:
        if (config & 0x3FF)
        {
            gcmkONERROR(gcvSTATUS_NOT_ALIGNED);
        }

        config |= gcmSETFIELD(0, 0:0, 0x1 );

        break;

    case gcvMMU_MODE_4K:
        if (config & 0xFFF)
        {
            gcmkONERROR(gcvSTATUS_NOT_ALIGNED);
        }

        config |= gcmSETFIELD(0, 0:0, 0x0 );

        break;

    default:
        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    /* Verify the gckCOMMAND object pointer. */
    command = Hardware->kernel->command;

    /* Acquire the command queue. */
    gcmkONERROR(gckCOMMAND_EnterCommit(command, FromPower));
    commitEntered = gcvTRUE;

    gcmkONERROR(gckCOMMAND_Reserve(
        command, 16, &pointer, &bufferSize
        ));

    buffer = pointer;

    buffer[0]
        = gcmSETFIELD(0, 31:27, 0x01 )
        | gcmSETFIELD(0, 15:0, 0x0061)
        | gcmSETFIELD(0, 25:16, 1);

    buffer[1] = config;

    buffer[2]
        = gcmSETFIELD(0, 31:27, 0x01 )
        | gcmSETFIELD(0, 15:0, 0x0060)
        | gcmSETFIELD(0, 25:16, 1);

    buffer[3] = address;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
        "Setup MMU: config=%08x, Safe Address=%08x\n.", config, address);

    gcmkONERROR(gckCOMMAND_Execute(command, 16));

    /* Release the command queue. */
    gcmkONERROR(gckCOMMAND_ExitCommit(command, FromPower));
    commitEntered = gcvFALSE;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
        "call gckCOMMAND_Stall to make sure the config is done.\n ");

    gcmkONERROR(gckCOMMAND_Stall(command, FromPower));

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
        "Enable MMU through GCREG_MMU_CONTROL.");

    /* Enable MMU. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os,
                              Hardware->core,
                              0x0018C,
                              gcmSETFIELD(0, 0:0, Enable)));

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
        "call gckCOMMAND_Stall to check MMU available.\n");

    gcmkONERROR(gckCOMMAND_Stall(command, FromPower));

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
        "The MMU is available.\n");

    /* Return the status. */
    gcmkFOOTER_NO();
    return status;

OnError:
    if (commitEntered)
    {
        /* Release the command queue mutex. */
        gcmkVERIFY_OK(gckCOMMAND_ExitCommit(Hardware->kernel->command,
                                            gcvFALSE));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_BuildVirtualAddress
**
**  Build a virtual address.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**      u32 Index
**          Index into page table.
**
**      u32 Offset
**          Offset into page.
**
**  OUTPUT:
**
**      u32 * Address
**          Pointer to a variable receiving te hardware address.
*/
gceSTATUS
gckHARDWARE_BuildVirtualAddress(
    IN gckHARDWARE Hardware,
    IN u32 Index,
    IN u32 Offset,
    OUT u32 * Address
    )
{
    gcmkHEADER_ARG("Hardware=0x%x Index=%u Offset=%u", Hardware, Index, Offset);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Address != NULL);

    /* Build virtual address. */
    *Address = gcmSETFIELD(0, 31:31, 0x1 )
             | gcmSETFIELD(0, 30:0, Offset | (Index << 12));

    /* Success. */
    gcmkFOOTER_ARG("*Address=0x%08x", *Address);
    return gcvSTATUS_OK;
}

gceSTATUS
gckHARDWARE_GetIdle(
    IN gckHARDWARE Hardware,
    IN int Wait,
    OUT u32 * Data
    )
{
    gceSTATUS status;
    u32 idle = 0;
    int retry, poll, pollCount;

    gcmkHEADER_ARG("Hardware=0x%x Wait=%d", Hardware, Wait);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(Data != NULL);


    /* If we have to wait, try 100 polls per millisecond. */
    pollCount = Wait ? 100 : 1;

    /* At most, try for 1 second. */
    for (retry = 0; retry < 1000; ++retry)
    {
        /* If we have to wait, try 100 polls per millisecond. */
        for (poll = pollCount; poll > 0; --poll)
        {
            /* Read register. */
            gcmkONERROR(
                gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00004, &idle));

            /* See if we have to wait for FE idle. */
            if (gcmGETFIELD(idle, 0:0))
            {
                /* FE is idle. */
                break;
            }
        }

        /* Check if we need to wait for FE and FE is busy. */
        if (Wait && !gcmGETFIELD(idle, 0:0))
        {
            /* Wait a little. */
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                           "%s: Waiting for idle: 0x%08X",
                           __FUNCTION__, idle);
#ifdef CONFIG_MACH_JZ4770
            if (retry & 0x3)
                schedule();
#endif

            gcmkVERIFY_OK(gckOS_Delay(Hardware->os, 1));
        }
        else
        {
            break;
        }
    }

    /* Return idle to caller. */
    *Data = idle;

    /* Success. */
    gcmkFOOTER_ARG("*Data=0x%08x", *Data);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/* Flush the caches. */
gceSTATUS
gckHARDWARE_Flush(
    IN gckHARDWARE Hardware,
    IN gceKERNEL_FLUSH Flush,
    IN void *Logical,
    IN OUT size_t * Bytes
    )
{
    u32 pipe;
    u32 flush = 0;
    u32 *logical = (u32 *) Logical;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Flush=0x%x Logical=0x%x *Bytes=%lu",
                   Hardware, Flush, Logical, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Get current pipe. */
    pipe = Hardware->kernel->command->pipeSelect;

    /* Flush 3D color cache. */
    if ((Flush & gcvFLUSH_COLOR) && (pipe == 0x0))
    {
        flush |= gcmSETFIELD(0, 1:1, 0x1 );
    }

    /* Flush 3D depth cache. */
    if ((Flush & gcvFLUSH_DEPTH) && (pipe == 0x0))
    {
        flush |= gcmSETFIELD(0, 0:0, 0x1 );
    }

    /* Flush 3D texture cache. */
    if ((Flush & gcvFLUSH_TEXTURE) && (pipe == 0x0))
    {
        flush |= gcmSETFIELD(0, 2:2, 0x1 );
    }

    /* Flush 2D cache. */
    if ((Flush & gcvFLUSH_2D) && (pipe == 0x1))
    {
        flush |= gcmSETFIELD(0, 3:3, 0x1 );
    }

    /* See if there is a valid flush. */
    if (flush == 0)
    {
        if (Bytes != NULL)
        {
            /* No bytes required. */
            *Bytes = 0;
        }
    }

    else
    {
        /* Copy to command queue. */
        if (Logical != NULL)
        {
            if (*Bytes < 8)
            {
                /* Command queue too small. */
                gcmkONERROR(gcvSTATUS_BUFFER_TOO_SMALL);
            }

            /* Append LOAD_STATE to AQFlush. */
            logical[0] = gcmSETFIELD(0, 31:27, 0x01 )
                       | gcmSETFIELD(0, 15:0, 0x0E03)
                       | gcmSETFIELD(0, 25:16, 1);

            logical[1] = flush;

            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                           "0x%x: FLUSH 0x%x", logical, flush);
        }

        if (Bytes != NULL)
        {
            /* 8 bytes required. */
            *Bytes = 8;
        }
    }

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu", gcmOPT_VALUE(Bytes));
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckHARDWARE_SetFastClear(
    IN gckHARDWARE Hardware,
    IN int Enable,
    IN int Compression
    )
{
#if !VIVANTE_NO_3D
    u32 debug;
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x Enable=%d Compression=%d",
                   Hardware, Enable, Compression);

    /* Only process if fast clear is available. */
    if (gcmGETFIELD(Hardware->identity.chipFeatures, 0:0))
    {
        if (Enable == -1)
        {
            /* Determine automatic value for fast clear. */
            Enable = ((Hardware->identity.chipModel    != gcv500)
                     || (Hardware->identity.chipRevision >= 3)
                     ) ? 1 : 0;
        }

        if (Compression == -1)
        {
            /* Determine automatic value for compression. */
            Compression = Enable
                        && gcmGETFIELD(Hardware->identity.chipFeatures, 5:5)
                        && !(Hardware->identity.chipModel == gcv860 && Hardware->identity.chipRevision == 0x4621);
        }

        /* Read AQMemoryDebug register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00414, &debug));

        /* Set fast clear bypass. */
        debug = gcmSETFIELD(debug, 20:20, Enable == 0);

        /* Set compression bypass. */
        debug = gcmSETFIELD(debug, 21:21, Compression == 0);

        /* Write back AQMemoryDebug register. */
        gcmkONERROR(
            gckOS_WriteRegisterEx(Hardware->os,
                                  Hardware->core,
                                  0x00414,
                                  debug));

        /* Store fast clear and comprersison flags. */
        Hardware->allowFastClear   = Enable;
        Hardware->allowCompression = Compression;

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "FastClear=%d Compression=%d", Enable, Compression);
    }

    /* Special patch for 0x320 0x5220. */
    if (Hardware->identity.chipRevision == 0x5220 && Hardware->identity.chipModel == gcv320)
    {
        u32 debug;

        /* Read AQMemoryDebug register. */
        gcmkONERROR(
                gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00414, &debug));

        debug |= 8;

        /* Write back AQMemoryDebug register. */
        gcmkONERROR(
                gckOS_WriteRegisterEx(Hardware->os,
                    Hardware->core,
                    0x00414,
                    debug));
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
#else
    return gcvSTATUS_OK;
#endif
}

typedef enum
{
    gcvPOWER_FLAG_INITIALIZE    = 1 << 0,
    gcvPOWER_FLAG_STALL         = 1 << 1,
    gcvPOWER_FLAG_STOP          = 1 << 2,
    gcvPOWER_FLAG_START         = 1 << 3,
    gcvPOWER_FLAG_RELEASE       = 1 << 4,
    gcvPOWER_FLAG_DELAY         = 1 << 5,
    gcvPOWER_FLAG_SAVE          = 1 << 6,
    gcvPOWER_FLAG_ACQUIRE       = 1 << 7,
    gcvPOWER_FLAG_POWER_OFF     = 1 << 8,
    gcvPOWER_FLAG_CLOCK_OFF     = 1 << 9,
    gcvPOWER_FLAG_CLOCK_ON      = 1 << 10,
}
gcePOWER_FLAGS;

#if gcmIS_DEBUG(gcdDEBUG_TRACE) && gcdPOWER_MANAGEMENT
static const char *
_PowerEnum(gceCHIPPOWERSTATE State)
{
    const const char *states[] =
    {
        gcmSTRING(gcvPOWER_ON),
        gcmSTRING(gcvPOWER_OFF),
        gcmSTRING(gcvPOWER_IDLE),
        gcmSTRING(gcvPOWER_SUSPEND),
        gcmSTRING(gcvPOWER_SUSPEND_ATPOWERON),
        gcmSTRING(gcvPOWER_OFF_ATPOWERON),
        gcmSTRING(gcvPOWER_IDLE_BROADCAST),
        gcmSTRING(gcvPOWER_SUSPEND_BROADCAST),
        gcmSTRING(gcvPOWER_OFF_BROADCAST),
        gcmSTRING(gcvPOWER_OFF_RECOVERY),
        gcmSTRING(gcvPOWER_ON_AUTO)
    };

    if ((State >= gcvPOWER_ON) && (State <= gcvPOWER_ON_AUTO))
    {
        return states[State - gcvPOWER_ON];
    }

    return "unknown";
}
#endif

/*******************************************************************************
**
**  gckHARDWARE_SetPowerManagementState
**
**  Set GPU to a specified power state.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**      gceCHIPPOWERSTATE State
**          Power State.
**
*/
gceSTATUS
gckHARDWARE_SetPowerManagementState(
    IN gckHARDWARE Hardware,
    IN gceCHIPPOWERSTATE State
    )
{
#if gcdPOWER_MANAGEMENT
    gceSTATUS status;
    gckCOMMAND command = NULL;
    gckOS os;
    unsigned int flag, clock;
    void *buffer;
    size_t bytes, requested;
    int acquired = gcvFALSE;
    int mutexAcquired = gcvFALSE;
    int stall = gcvTRUE;
    int broadcast = gcvFALSE;
#if gcdPOWEROFF_TIMEOUT
    int timeout = gcvFALSE;
    int isAfter = gcvFALSE;
    u32 currentTime;
#endif
    u32 process, thread;
    int commitEntered = gcvFALSE;
#if gcdENABLE_PROFILING
    u64 time, freq, mutexTime, onTime, stallTime, stopTime, delayTime,
              initTime, offTime, startTime, totalTime;
#endif
    int global = gcvFALSE;
    int globalAcquired = gcvFALSE;
    int configMmu = gcvFALSE;

    /* State transition flags. */
    static const unsigned int flags[4][4] =
    {
        /* gcvPOWER_ON           */
        {   /* ON                */ 0,
            /* OFF               */ gcvPOWER_FLAG_ACQUIRE   |
                                    gcvPOWER_FLAG_STALL     |
                                    gcvPOWER_FLAG_STOP      |
                                    gcvPOWER_FLAG_POWER_OFF |
                                    gcvPOWER_FLAG_CLOCK_OFF,
            /* IDLE              */ gcvPOWER_FLAG_ACQUIRE   |
                                    gcvPOWER_FLAG_STALL,
            /* SUSPEND           */ gcvPOWER_FLAG_ACQUIRE   |
                                    gcvPOWER_FLAG_STALL     |
                                    gcvPOWER_FLAG_STOP      |
                                    gcvPOWER_FLAG_CLOCK_OFF,
        },

        /* gcvPOWER_OFF          */
        {   /* ON                */ gcvPOWER_FLAG_INITIALIZE |
                                    gcvPOWER_FLAG_START      |
                                    gcvPOWER_FLAG_RELEASE    |
                                    gcvPOWER_FLAG_DELAY,
            /* OFF               */ 0,
            /* IDLE              */ gcvPOWER_FLAG_INITIALIZE |
                                    gcvPOWER_FLAG_START      |
                                    gcvPOWER_FLAG_DELAY,
            /* SUSPEND           */ gcvPOWER_FLAG_INITIALIZE |
                                    gcvPOWER_FLAG_CLOCK_OFF,
        },

        /* gcvPOWER_IDLE         */
        {   /* ON                */ gcvPOWER_FLAG_RELEASE,
            /* OFF               */ gcvPOWER_FLAG_STOP      |
                                    gcvPOWER_FLAG_POWER_OFF |
                                    gcvPOWER_FLAG_CLOCK_OFF,
            /* IDLE              */ 0,
            /* SUSPEND           */ gcvPOWER_FLAG_STOP      |
                                    gcvPOWER_FLAG_CLOCK_OFF,
        },

        /* gcvPOWER_SUSPEND      */
        {   /* ON                */ gcvPOWER_FLAG_START     |
                                    gcvPOWER_FLAG_RELEASE   |
                                    gcvPOWER_FLAG_DELAY     |
                                    gcvPOWER_FLAG_CLOCK_ON,
            /* OFF               */ gcvPOWER_FLAG_SAVE      |
                                    gcvPOWER_FLAG_POWER_OFF |
                                    gcvPOWER_FLAG_CLOCK_OFF,
            /* IDLE              */ gcvPOWER_FLAG_START     |
                                    gcvPOWER_FLAG_DELAY     |
                                    gcvPOWER_FLAG_CLOCK_ON,
            /* SUSPEND           */ 0,
        },
    };

    /* Clocks. */
    static const unsigned int clocks[4] =
    {
        /* gcvPOWER_ON */
        gcmSETFIELD(0, 0:0, 0) |
        gcmSETFIELD(0, 1:1, 0) |
        gcmSETFIELD(0, 8:2, 64) |
        gcmSETFIELD(0, 9:9, 1),

        /* gcvPOWER_OFF */
        gcmSETFIELD(0, 0:0, 1) |
        gcmSETFIELD(0, 1:1, 1) |
        gcmSETFIELD(0, 8:2, 1) |
        gcmSETFIELD(0, 9:9, 1),

        /* gcvPOWER_IDLE */
        gcmSETFIELD(0, 0:0, 0) |
        gcmSETFIELD(0, 1:1, 0) |
        gcmSETFIELD(0, 8:2, 1) |
        gcmSETFIELD(0, 9:9, 1),

        /* gcvPOWER_SUSPEND */
        gcmSETFIELD(0, 0:0, 1) |
        gcmSETFIELD(0, 1:1, 1) |
        gcmSETFIELD(0, 8:2, 1) |
        gcmSETFIELD(0, 9:9, 1),
    };

    gcmkHEADER_ARG("Hardware=0x%x State=%d", Hardware, State);
#if gcmIS_DEBUG(gcdDEBUG_TRACE)
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "Switching to power state %d(%s)",
                   State, _PowerEnum(State));
#endif

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Get the gckOS object pointer. */
    os = Hardware->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Get the gckCOMMAND object pointer. */
    gcmkVERIFY_OBJECT(Hardware->kernel, gcvOBJ_KERNEL);
    command = Hardware->kernel->command;
    gcmkVERIFY_OBJECT(command, gcvOBJ_COMMAND);

    /* Start profiler. */
    gcmkPROFILE_INIT(freq, time);

    /* Convert the broadcast power state. */
    switch (State)
    {
    case gcvPOWER_SUSPEND_ATPOWERON:
        /* Convert to SUSPEND and don't wait for STALL. */
        State = gcvPOWER_SUSPEND;
        stall = gcvFALSE;
        break;

    case gcvPOWER_OFF_ATPOWERON:
        /* Convert to OFF and don't wait for STALL. */
        State = gcvPOWER_OFF;
        stall = gcvFALSE;
        break;

    case gcvPOWER_IDLE_BROADCAST:
        /* Convert to IDLE and note we are inside broadcast. */
        State     = gcvPOWER_IDLE;
        broadcast = gcvTRUE;
        break;

    case gcvPOWER_SUSPEND_BROADCAST:
        /* Convert to SUSPEND and note we are inside broadcast. */
        State     = gcvPOWER_SUSPEND;
        broadcast = gcvTRUE;
        break;

    case gcvPOWER_OFF_BROADCAST:
        /* Convert to OFF and note we are inside broadcast. */
        State     = gcvPOWER_OFF;
        broadcast = gcvTRUE;
        break;

    case gcvPOWER_OFF_RECOVERY:
        /* Convert to OFF and note we are inside recovery. */
        State     = gcvPOWER_OFF;
        stall     = gcvFALSE;
        broadcast = gcvTRUE;
        break;

    case gcvPOWER_ON_AUTO:
        /* Convert to ON and note we are inside recovery. */
        State = gcvPOWER_ON;
        break;

    case gcvPOWER_ON:
    case gcvPOWER_IDLE:
    case gcvPOWER_SUSPEND:
    case gcvPOWER_OFF:
        /* Mark as global power management. */
        global = gcvTRUE;
        break;

#if gcdPOWEROFF_TIMEOUT
    case gcvPOWER_OFF_TIMEOUT:
        /* Convert to OFF and note we are inside broadcast. */
        State     = gcvPOWER_OFF;
        broadcast = gcvTRUE;
        /* Check time out */
        timeout = gcvTRUE;
        break;
#endif

    default:
        break;
    }

    /* Get current process and thread IDs. */
    process = task_tgid_vnr(current);
    thread = task_pid_vnr(current);

    if (broadcast)
    {
        /* Try to acquire the power mutex. */
        status = gckOS_AcquireMutex(os, Hardware->powerMutex, 0);

        if (status == gcvSTATUS_TIMEOUT)
        {
            /* Check if we already own this mutex. */
            if ((Hardware->powerProcess == process)
            &&  (Hardware->powerThread  == thread)
            )
            {
                /* Bail out on recursive power management. */
                gcmkFOOTER_NO();
                return gcvSTATUS_OK;
            }
            else if (State == gcvPOWER_IDLE || State == gcvPOWER_SUSPEND)
            {
                /* Called from IST,
                ** so waiting here will cause deadlock,
                ** if lock holder call gckCOMMAND_Stall() */
                gcmkONERROR(gcvSTATUS_INVALID_REQUEST);
            }
            else
            {
                /* Acquire the power mutex. */
                gcmkONERROR(gckOS_AcquireMutex(os,
                                               Hardware->powerMutex,
                                               gcvINFINITE));
            }
        }
    }
    else
    {
        /* Acquire the power mutex. */
        gcmkONERROR(gckOS_AcquireMutex(os, Hardware->powerMutex, gcvINFINITE));
    }

    /* Get time until mtuex acquired. */
    gcmkPROFILE_QUERY(time, mutexTime);

    Hardware->powerProcess = process;
    Hardware->powerThread  = thread;
    mutexAcquired          = gcvTRUE;

    /* Grab control flags and clock. */
    flag  = flags[Hardware->chipPowerState][State];
    clock = clocks[State];

#if gcdPOWEROFF_TIMEOUT
    if (timeout)
    {
        gcmkONERROR(gckOS_GetTicks(&currentTime));

        gcmkONERROR(
            gckOS_TicksAfter(Hardware->powerOffTime, currentTime, &isAfter));

        /* powerOffTime is pushed forward, give up.*/
        if (isAfter
        /* Expect a transition start from IDLE or SUSPEND. */
        ||  (Hardware->chipPowerState == gcvPOWER_ON)
        ||  (Hardware->chipPowerState == gcvPOWER_OFF)
        )
        {
            /* Release the power mutex. */
            gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));

            /* No need to do anything. */
            gcmkFOOTER_NO();
            return gcvSTATUS_OK;
        }

        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                       "Power Off GPU[%d] at %u [supposed to be at %u]",
                       Hardware->core, currentTime, Hardware->powerOffTime);
    }
#endif

    if (flag == 0)
    {
        /* Release the power mutex. */
        gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));

        /* No need to do anything. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    /* If this is an internal power management, we have to check if we can grab
    ** the global power semaphore. If we cannot, we have to wait until the
    ** external world changes power management. */
    if (!global)
    {
        /* Try to acquire the global semaphore. */
        status = gckOS_TryAcquireSemaphore(os, Hardware->globalSemaphore);
        if (status == gcvSTATUS_TIMEOUT)
        {
            if (State == gcvPOWER_IDLE || State == gcvPOWER_SUSPEND)
            {
                /* Called from thread routine which should NEVER sleep.*/
                gcmkONERROR(gcvSTATUS_INVALID_REQUEST);
            }

            /* Release the power mutex. */
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                           "Releasing the power mutex.");
            gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));
            mutexAcquired = gcvFALSE;

            /* Wait for the semaphore. */
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                           "Waiting for global semaphore.");
            gcmkONERROR(gckOS_AcquireSemaphore(os, Hardware->globalSemaphore));
            globalAcquired = gcvTRUE;

            /* Acquire the power mutex. */
            gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                           "Reacquiring the power mutex.");
            gcmkONERROR(gckOS_AcquireMutex(os,
                                           Hardware->powerMutex,
                                           gcvINFINITE));
            mutexAcquired = gcvTRUE;

            /* chipPowerState may be changed by external world during the time
            ** we give up powerMutex, so updating flag now is necessary. */
            flag = flags[Hardware->chipPowerState][State];

            if (flag == 0)
            {
                gcmkONERROR(gckOS_ReleaseSemaphore(os, Hardware->globalSemaphore));
                globalAcquired = gcvFALSE;

                gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));
                mutexAcquired = gcvFALSE;

                gcmkFOOTER_NO();
                return gcvSTATUS_OK;
            }
        }
        else
        {
            /* Error. */
            gcmkONERROR(status);
        }

        /* Release the global semaphore again. */
        gcmkONERROR(gckOS_ReleaseSemaphore(os, Hardware->globalSemaphore));
        globalAcquired = gcvFALSE;
    }
    else
    {
        if (State == gcvPOWER_OFF || State == gcvPOWER_SUSPEND || State == gcvPOWER_IDLE)
        {
            /* Acquire the global semaphore if it has not been acquired. */
            status = gckOS_TryAcquireSemaphore(os, Hardware->globalSemaphore);
            if (status == gcvSTATUS_OK)
            {
                globalAcquired = gcvTRUE;
            }
            else if (status != gcvSTATUS_TIMEOUT)
            {
                /* Other errors. */
                gcmkONERROR(status);
            }
            /* Ignore gcvSTATUS_TIMEOUT and leave globalAcquired as gcvFALSE.
            ** gcvSTATUS_TIMEOUT means global semaphore has already
            ** been acquired before this operation, so even if we fail,
            ** we should not release it in our error handling. It should be
            ** released by the next successful global gcvPOWER_ON. */
        }

        /* Global power management can't be aborted, so sync with
        ** proceeding last commit. */
        if (flag & gcvPOWER_FLAG_ACQUIRE)
        {
            /* Acquire the power management semaphore. */
            gcmkONERROR(gckOS_AcquireSemaphore(os, command->powerSemaphore));
            acquired = gcvTRUE;

            /* avoid acquiring again. */
            flag &= ~gcvPOWER_FLAG_ACQUIRE;
        }
    }

    if (flag & (gcvPOWER_FLAG_INITIALIZE | gcvPOWER_FLAG_CLOCK_ON))
    {
        /* Turn on the power. */
        gcmkONERROR(gckOS_SetGPUPower(os, gcvTRUE, gcvTRUE));

        /* Mark clock and power as enabled. */
        Hardware->clockState = gcvTRUE;
        Hardware->powerState = gcvTRUE;
    }

    /* Get time until powered on. */
    gcmkPROFILE_QUERY(time, onTime);

    if ((flag & gcvPOWER_FLAG_STALL) && stall)
    {
        int idle;
        s32 atomValue;

        /* For global operation, all pending commits have already been
        ** blocked by globalSemaphore or powerSemaphore.*/
        if (!global)
        {
            /* Check commit atom. */
            gcmkONERROR(gckOS_AtomGet(os, command->atomCommit, &atomValue));

            if (atomValue > 0)
            {
                /* Commits are pending - abort power management. */
                status = broadcast ? gcvSTATUS_CHIP_NOT_READY
                                   : gcvSTATUS_MORE_DATA;
                goto OnError;
            }
        }

        if (broadcast)
        {
            /* Check for idle. */
            gcmkONERROR(gckHARDWARE_QueryIdle(Hardware, &idle));

            if (!idle)
            {
                status = gcvSTATUS_CHIP_NOT_READY;
                goto OnError;
            }
        }

        else
        {
            /* Acquire the command queue. */
            gcmkONERROR(gckCOMMAND_EnterCommit(command, gcvTRUE));
            commitEntered = gcvTRUE;

            /* Get the size of the flush command. */
            gcmkONERROR(gckHARDWARE_Flush(Hardware,
                                          gcvFLUSH_ALL,
                                          NULL,
                                          &requested));

            /* Reserve space in the command queue. */
            gcmkONERROR(gckCOMMAND_Reserve(command,
                                           requested,
                                           &buffer,
                                           &bytes));

            /* Append a flush. */
            gcmkONERROR(gckHARDWARE_Flush(
                Hardware, gcvFLUSH_ALL, buffer, &bytes
                ));

            /* Execute the command queue. */
            gcmkONERROR(gckCOMMAND_Execute(command, requested));

            /* Release the command queue. */
            gcmkONERROR(gckCOMMAND_ExitCommit(command, gcvTRUE));
            commitEntered = gcvFALSE;

            /* Wait to finish all commands. */
            gcmkONERROR(gckCOMMAND_Stall(command, gcvTRUE));
        }
    }

    /* Get time until stalled. */
    gcmkPROFILE_QUERY(time, stallTime);

    if (flag & gcvPOWER_FLAG_ACQUIRE)
    {
        /* Acquire the power management semaphore. */
        gcmkONERROR(gckOS_AcquireSemaphore(os, command->powerSemaphore));
        acquired = gcvTRUE;
    }

    if (flag & gcvPOWER_FLAG_STOP)
    {
        /* Stop the command parser. */
        gcmkONERROR(gckCOMMAND_Stop(command, gcvFALSE));

        /* Stop the Isr. */
        gcmkONERROR(Hardware->stopIsr(Hardware->isrContext));
    }

    /* Get time until stopped. */
    gcmkPROFILE_QUERY(time, stopTime);

    /* Only process this when hardware is enabled. */
    if (Hardware->clockState && Hardware->powerState)
    {
        if (flag & (gcvPOWER_FLAG_POWER_OFF | gcvPOWER_FLAG_CLOCK_OFF))
        {
            if (Hardware->identity.chipModel == gcv4000
            && Hardware->identity.chipRevision == 0x5208)
            {
                clock &= ~2U;
            }
        }

        /* Write the clock control register. */
        gcmkONERROR(gckOS_WriteRegisterEx(os,
                                          Hardware->core,
                                          0x00000,
                                          clock));

        /* Done loading the frequency scaler. */
        gcmkONERROR(gckOS_WriteRegisterEx(os,
                                          Hardware->core,
                                          0x00000,
                                          gcmSETFIELD(clock, 9:9, 0)));
    }

    if (flag & gcvPOWER_FLAG_DELAY)
    {
        /* Wait for the specified amount of time to settle coming back from
        ** power-off or suspend state. */
        gcmkONERROR(gckOS_Delay(os, gcdPOWER_CONTROL_DELAY));
    }

    /* Get time until delayed. */
    gcmkPROFILE_QUERY(time, delayTime);

    if (flag & gcvPOWER_FLAG_INITIALIZE)
    {
        /* Initialize hardware. */
        gcmkONERROR(gckHARDWARE_InitializeHardware(Hardware));

        gcmkONERROR(gckHARDWARE_SetFastClear(Hardware,
                                             Hardware->allowFastClear,
                                             Hardware->allowCompression));

        /* Force the command queue to reload the next context. */
        command->currContext = NULL;

        /* Need to config mmu after command start. */
        configMmu = gcvTRUE;
    }

    /* Get time until initialized. */
    gcmkPROFILE_QUERY(time, initTime);

    if (flag & (gcvPOWER_FLAG_POWER_OFF | gcvPOWER_FLAG_CLOCK_OFF))
    {
        /* Turn off the GPU power. */
        gcmkONERROR(
            gckOS_SetGPUPower(os,
                              (flag & gcvPOWER_FLAG_CLOCK_OFF) ? gcvFALSE
                                                               : gcvTRUE,
                              (flag & gcvPOWER_FLAG_POWER_OFF) ? gcvFALSE
                                                               : gcvTRUE));

        /* Save current hardware power and clock states. */
        Hardware->clockState = (flag & gcvPOWER_FLAG_CLOCK_OFF) ? gcvFALSE
                                                                : gcvTRUE;
        Hardware->powerState = (flag & gcvPOWER_FLAG_POWER_OFF) ? gcvFALSE
                                                                : gcvTRUE;
    }

    /* Get time until off. */
    gcmkPROFILE_QUERY(time, offTime);

    if (flag & gcvPOWER_FLAG_START)
    {
        /* Start the command processor. */
        gcmkONERROR(gckCOMMAND_Start(command));

        /* Start the Isr. */
        gcmkONERROR(Hardware->startIsr(Hardware->isrContext));

        /* Set NEW MMU. */
        if (Hardware->mmuVersion != 0 && configMmu)
        {
            gcmkONERROR(
                    gckHARDWARE_SetMMUv2(
                        Hardware,
                        gcvTRUE,
                        Hardware->kernel->mmu->mtlbLogical,
                        gcvMMU_MODE_4K,
                        (u8 *)Hardware->kernel->mmu->mtlbLogical + gcdMMU_MTLB_SIZE,
                        gcvTRUE
                        ));
        }
    }

    /* Get time until started. */
    gcmkPROFILE_QUERY(time, startTime);

    if (flag & gcvPOWER_FLAG_RELEASE)
    {
        /* Release the power management semaphore. */
        gcmkONERROR(gckOS_ReleaseSemaphore(os, command->powerSemaphore));
        acquired = gcvFALSE;

        if (global)
        {
            /* Verify global semaphore has been acquired already before
            ** we release it.
            ** If it was acquired, gckOS_TryAcquireSemaphore will return
            ** gcvSTATUS_TIMEOUT and we release it. Otherwise, global
            ** semaphore will be acquried now, but it still is released
            ** immediately. */
            status = gckOS_TryAcquireSemaphore(os, Hardware->globalSemaphore);
            if (status != gcvSTATUS_TIMEOUT)
            {
                gcmkONERROR(status);
            }

            /* Release the global semaphore. */
            gcmkONERROR(gckOS_ReleaseSemaphore(os, Hardware->globalSemaphore));
            globalAcquired = gcvFALSE;
        }
    }

    /* Save the new power state. */
    Hardware->chipPowerState = State;

#if gcdPOWEROFF_TIMEOUT
    /* Reset power off time */
    gcmkONERROR(gckOS_GetTicks(&currentTime));

    Hardware->powerOffTime = currentTime + Hardware->powerOffTimeout;

    if (State == gcvPOWER_IDLE || State == gcvPOWER_SUSPEND)
    {
        /* Start a timer to power off GPU when GPU enters IDLE or SUSPEND. */
        gcmkVERIFY_OK(gckOS_StartTimer(os,
                                       Hardware->powerOffTimer,
                                       Hardware->powerOffTimeout));
    }
    else
    {
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE, "Cancel powerOfftimer");

        /* Cancel running timer when GPU enters ON or OFF. */
        gcmkVERIFY_OK(gckOS_StopTimer(os, Hardware->powerOffTimer));
    }
#endif

    /* Release the power mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(os, Hardware->powerMutex));

    /* Get total time. */
    gcmkPROFILE_QUERY(time, totalTime);
#if gcdENABLE_PROFILING
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "PROF(%llu): mutex:%llu on:%llu stall:%llu stop:%llu",
                   freq, mutexTime, onTime, stallTime, stopTime);
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_HARDWARE,
                   "  delay:%llu init:%llu off:%llu start:%llu total:%llu",
                   delayTime, initTime, offTime, startTime, totalTime);
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (commitEntered)
    {
        /* Release the command queue mutex. */
        gcmkVERIFY_OK(gckCOMMAND_ExitCommit(command, gcvTRUE));
    }

    if (acquired)
    {
        /* Release semaphore. */
        gcmkVERIFY_OK(gckOS_ReleaseSemaphore(Hardware->os,
                                             command->powerSemaphore));
    }

    if (globalAcquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseSemaphore(Hardware->os,
                                             Hardware->globalSemaphore));
    }

    if (mutexAcquired)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Hardware->os, Hardware->powerMutex));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
#else /* gcdPOWER_MANAGEMENT */
    /* Do nothing */
    return gcvSTATUS_OK;
#endif
}

/*******************************************************************************
**
**  gckHARDWARE_QueryPowerManagementState
**
**  Get GPU power state.
**
**  INPUT:
**
**      gckHARDWARE Harwdare
**          Pointer to an gckHARDWARE object.
**
**      gceCHIPPOWERSTATE* State
**          Power State.
**
*/
gceSTATUS
gckHARDWARE_QueryPowerManagementState(
    IN gckHARDWARE Hardware,
    OUT gceCHIPPOWERSTATE* State
    )
{
    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(State != NULL);

    /* Return the statue. */
    *State = Hardware->chipPowerState;

    /* Success. */
    gcmkFOOTER_ARG("*State=%d", *State);
    return gcvSTATUS_OK;
}

gceSTATUS
gckHARDWARE_QueryIdle(
    IN gckHARDWARE Hardware,
    OUT int *IsIdle
    )
{
    gceSTATUS status;
    u32 idle, address;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(IsIdle != NULL);

    /* We are idle when the power is not ON. */
    if (Hardware->chipPowerState != gcvPOWER_ON)
    {
        *IsIdle = gcvTRUE;
    }

    else
    {
        /* Read idle register. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00004, &idle));

        /* Pipe must be idle. */
        if ((gcmGETFIELD(idle, 1:1) != 1)
        ||  (gcmGETFIELD(idle, 3:3) != 1)
        ||  (gcmGETFIELD(idle, 4:4) != 1)
        ||  (gcmGETFIELD(idle, 5:5) != 1)
        ||  (gcmGETFIELD(idle, 6:6) != 1)
        ||  (gcmGETFIELD(idle, 7:7) != 1)
        ||  (gcmGETFIELD(idle, 2:2) != 1)
        )
        {
            /* Something is busy. */
            *IsIdle = gcvFALSE;
        }

        else
        {
            /* Read the current FE address. */
            gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                             Hardware->core,
                                             0x00664,
                                             &address));

            /* Test if address is inside the last WAIT/LINK sequence. */
            if ((address >= Hardware->lastWaitLink)
            &&  (address <= Hardware->lastWaitLink + 16)
            )
            {
                /* FE is in last WAIT/LINK and the pipe is idle. */
                *IsIdle = gcvTRUE;
            }
            else
            {
                /* FE is not in WAIT/LINK yet. */
                *IsIdle = gcvFALSE;
            }
        }
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
** Handy macros that will help in reading those debug registers.
*/

#define gcmkREAD_DEBUG_REGISTER(control, block, index, data) \
    gcmkONERROR(\
        gckOS_WriteRegisterEx(Hardware->os, \
                              Hardware->core, \
                              GC_DEBUG_CONTROL##control##_Address, \
                              gcmSETFIELD(0, \
                                          GC_DEBUG_CONTROL##control, \
                                          block, \
                                          index))); \
    gcmkONERROR(\
        gckOS_ReadRegisterEx(Hardware->os, \
                             Hardware->core, \
                             GC_DEBUG_SIGNALS_##block##_Address, \
                             &profiler->data))

#define gcmkRESET_DEBUG_REGISTER(control, block) \
    gcmkONERROR(\
        gckOS_WriteRegisterEx(Hardware->os, \
                              Hardware->core, \
                              GC_DEBUG_CONTROL##control##_Address, \
                              gcmSETFIELD(0, \
                                          GC_DEBUG_CONTROL##control, \
                                          block, \
                                          15))); \
    gcmkONERROR(\
        gckOS_WriteRegisterEx(Hardware->os, \
                              Hardware->core, \
                              GC_DEBUG_CONTROL##control##_Address, \
                              gcmSETFIELD(0, \
                                          GC_DEBUG_CONTROL##control, \
                                          block, \
                                          0)))

/*******************************************************************************
**
**  gckHARDWARE_ProfileEngine2D
**
**  Read the profile registers available in the 2D engine and sets them in the
**  profile.  The function will also reset the pixelsRendered counter every time.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      OPTIONAL struct _gcs2D_PROFILE *Profile
**          Pointer to a gcs2D_Profile structure.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_ProfileEngine2D(
    IN gckHARDWARE Hardware,
    OPTIONAL struct _gcs2D_PROFILE *Profile
    )
{
    gceSTATUS status;
    struct _gcs2D_PROFILE *profiler = Profile;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    if (Profile != NULL)
    {
        /* Read the cycle count. */
        gcmkONERROR(
            gckOS_ReadRegisterEx(Hardware->os,
                                 Hardware->core,
                                 0x00438,
                                 &Profile->cycleCount));

        /* Read pixels rendered by 2D engine. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 19:16, 11) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &profiler->pixelsRendered));

        /* Reset counter. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 19:16, 15) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 19:16, 0)
));
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if VIVANTE_PROFILER
gceSTATUS
gckHARDWARE_QueryProfileRegisters(
    IN gckHARDWARE Hardware,
    OUT gcsPROFILER_COUNTERS * Counters
    )
{
    gceSTATUS status;
    gcsPROFILER_COUNTERS * profiler = Counters;

    gcmkHEADER_ARG("Hardware=0x%x Counters=0x%x", Hardware, Counters);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Read the counters. */
    gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x00040,
                             &profiler->gpuTotalRead64BytesPerFrame));
    gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x00044,
                             &profiler->gpuTotalWrite64BytesPerFrame));
    gcmkONERROR(
        gckOS_ReadRegisterEx(Hardware->os,
                             Hardware->core,
                             0x00438,
                             &profiler->gpuCyclesCounter));

    /* Reset counters. */
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x0003C, 1));
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x0003C, 0));
    gcmkONERROR(
        gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00438, 0));

    /* PE */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 19:16, 0) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &profiler->pe_pixel_count_killed_by_color_pipe));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 19:16, 1) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &profiler->pe_pixel_count_killed_by_depth_pipe));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 19:16, 2) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &profiler->pe_pixel_count_drawn_by_color_pipe));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 19:16, 3) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00454, &profiler->pe_pixel_count_drawn_by_depth_pipe));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 19:16, 15) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 19:16, 0)
));

    /* SH */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 27:24, 7) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->ps_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 27:24, 8) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->rendered_pixel_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 27:24, 9) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vs_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 27:24, 10) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->rendered_vertice_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 27:24, 11) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vtx_branch_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 27:24, 12) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->vtx_texld_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 27:24, 13) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->pxl_branch_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 27:24, 14) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0045C, &profiler->pxl_texld_inst_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 27:24, 15) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00470,   gcmSETFIELD(0, 27:24, 0)
));

    /* PA */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 3:0, 3) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_input_vtx_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 3:0, 4) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_input_prim_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 3:0, 5) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_output_prim_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 3:0, 6) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_depth_clipped_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 3:0, 7) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_trivial_rejected_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 3:0, 8) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00460, &profiler->pa_culled_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 3:0, 15) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 3:0, 0)
));

    /* SE */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 11:8, 0) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_culled_triangle_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 11:8, 1) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00464, &profiler->se_culled_lines_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 11:8, 15) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 11:8, 0)
));

    /* RA */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 19:16, 0) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_valid_pixel_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 19:16, 1) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_total_quad_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 19:16, 2) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_valid_quad_count_after_early_z));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 19:16, 3) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_total_primitive_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 19:16, 9) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_pipe_cache_miss_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 19:16, 10) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_prefetch_cache_miss_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 19:16, 11) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00448, &profiler->ra_eez_culled_counter));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 19:16, 15) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 19:16, 0)
));

    /* TX */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 27:24, 0) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_bilinear_requests));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 27:24, 1) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_trilinear_requests));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 27:24, 2) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_discarded_texture_requests));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 27:24, 3) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_total_texture_requests));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 27:24, 5) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_mem_read_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 27:24, 6) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_mem_read_in_8B_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 27:24, 7) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_miss_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 27:24, 8) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_hit_texel_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 27:24, 9) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0044C, &profiler->tx_cache_miss_texel_count));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 27:24, 15) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00474,   gcmSETFIELD(0, 27:24, 0)
));

    /* MC */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   gcmSETFIELD(0, 3:0, 1) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_read_req_8B_from_pipeline));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   gcmSETFIELD(0, 3:0, 2) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_read_req_8B_from_IP));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   gcmSETFIELD(0, 3:0, 3) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x00468, &profiler->mc_total_write_req_8B_from_pipeline));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   gcmSETFIELD(0, 3:0, 15) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   gcmSETFIELD(0, 3:0, 0)
));

    /* HI */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   gcmSETFIELD(0, 11:8, 0) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_read_request_stalled));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   gcmSETFIELD(0, 11:8, 1) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_write_request_stalled));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   gcmSETFIELD(0, 11:8, 2) ));
gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os, Hardware->core, 0x0046C, &profiler->hi_axi_cycles_write_data_stalled));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   gcmSETFIELD(0, 11:8, 15) ));
gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, 0x00478,   gcmSETFIELD(0, 11:8, 0)
));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif

static gceSTATUS
_ResetGPU(
    IN gckHARDWARE Hardware,
    IN gckOS Os,
    IN gceCORE Core
    )
{
    u32 control, idle;
    gceSTATUS status;

    for (;;)
    {
        /* Disable clock gating. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    Hardware->powerBaseAddress +
                    0x00104,
                    0x00000000));

        control = gcmSETFIELD(0x01590880, 17:17, 1);

        /* Disable pulse-eater. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x0010C,
                    control));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x0010C,
                    gcmSETFIELD(control, 0:0, 1)));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x0010C,
                    control));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x00000,
                    gcmSETFIELD(0x00000100, 9:9, 1)));

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                    Core,
                    0x00000,
                    0x00000100));

        /* Wait for clock being stable. */
        gcmkONERROR(gckOS_Delay(Os, 1));

        /* Isolate the GPU. */
        control = gcmSETFIELD(0x00000100, 19:19, 1);

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          Core,
                                          0x00000,
                                          control));

        /* Set soft reset. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          Core,
                                          0x00000,
                                          gcmSETFIELD(control, 12:12, 1)));

        /* Wait for reset. */
        gcmkONERROR(gckOS_Delay(Os, 1));

        /* Reset soft reset bit. */
        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          Core,
                                          0x00000,
                                          gcmSETFIELD(control, 12:12, 0)));

        /* Reset GPU isolation. */
        control = gcmSETFIELD(control, 19:19, 0);

        gcmkONERROR(gckOS_WriteRegisterEx(Os,
                                          Core,
                                          0x00000,
                                          control));

        /* Read idle register. */
        gcmkONERROR(gckOS_ReadRegisterEx(Os,
                                         Core,
                                         0x00004,
                                         &idle));

        if (gcmGETFIELD(idle, 0:0) == 0)
        {
            continue;
        }

        /* Read reset register. */
        gcmkONERROR(gckOS_ReadRegisterEx(Os,
                                         Core,
                                         0x00000,
                                         &control));

        if ((gcmGETFIELD(control, 16:16) == 0)
        ||  (gcmGETFIELD(control, 17:17) == 0)
        )
        {
            continue;
        }

        /* GPU is idle. */
        break;
    }

    /* Success. */
    return gcvSTATUS_OK;

OnError:

    /* Return the error. */
    return status;
}

gceSTATUS
gckHARDWARE_Reset(
    IN gckHARDWARE Hardware
    )
{
    gceSTATUS status;
    gckCOMMAND command;
    int acquired = gcvFALSE;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_OBJECT(Hardware->kernel, gcvOBJ_KERNEL);
    command = Hardware->kernel->command;
    gcmkVERIFY_OBJECT(command, gcvOBJ_COMMAND);

    if (Hardware->identity.chipRevision < 0x4600)
    {
        /* Not supported - we need the isolation bit. */
        gcmkONERROR(gcvSTATUS_NOT_SUPPORTED);
    }

    if (Hardware->chipPowerState == gcvPOWER_ON)
    {
        /* Acquire the power management semaphore. */
        gcmkONERROR(
            gckOS_AcquireSemaphore(Hardware->os, command->powerSemaphore));
        acquired = gcvTRUE;
    }

    if ((Hardware->chipPowerState == gcvPOWER_ON)
    ||  (Hardware->chipPowerState == gcvPOWER_IDLE)
    )
    {
        /* Stop the command processor. */
        gcmkONERROR(gckCOMMAND_Stop(command, gcvTRUE));
    }

    /* Stop isr, we will start it again when power on GPU. */
    gcmkONERROR(Hardware->stopIsr(Hardware->isrContext));

    gcmkONERROR(_ResetGPU(Hardware, Hardware->os, Hardware->core));

    /* Force an OFF to ON power switch. */
    Hardware->chipPowerState = gcvPOWER_OFF;
    gcmkONERROR(gckHARDWARE_SetPowerManagementState(Hardware, gcvPOWER_ON));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the power management semaphore. */
        gcmkVERIFY_OK(
            gckOS_ReleaseSemaphore(Hardware->os, command->powerSemaphore));
    }

    /* Return the error. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckHARDWARE_GetBaseAddress(
    IN gckHARDWARE Hardware,
    OUT u32 *BaseAddress
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(BaseAddress != NULL);

    /* Test if we have a new Memory Controller. */
    if (gcmVERIFYFIELDVALUE(Hardware->identity.chipMinorFeatures, 22:22, 0x1 ))
    {
        /* No base address required. */
        *BaseAddress = 0;
    }
    else
    {
        /* Get the base address from the OS. */
        gcmkONERROR(gckOS_GetBaseAddress(Hardware->os, BaseAddress));
    }

    /* Success. */
    gcmkFOOTER_ARG("*BaseAddress=0x%08x", *BaseAddress);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckHARDWARE_NeedBaseAddress(
    IN gckHARDWARE Hardware,
    IN u32 State,
    OUT int *NeedBase
    )
{
    int need = gcvFALSE;

    gcmkHEADER_ARG("Hardware=0x%x State=0x%08x", Hardware, State);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(NeedBase != NULL);

    /* Make sure this is a load state. */
    if (gcmVERIFYFIELDVALUE(State, 31:27, 0x01 ))
    {
#if !VIVANTE_NO_3D
        /* Get the state address. */
        switch (gcmGETFIELD(State, 15:0))
        {
        case 0x0596:
        case 0x0597:
        case 0x0599:
        case 0x059A:
        case 0x05A9:
            /* These states need a TRUE physical address. */
            need = gcvTRUE;
            break;
        }
#else
        /* 2D addresses don't need a base address. */
#endif
    }

    /* Return the flag. */
    *NeedBase = need;

    /* Success. */
    gcmkFOOTER_ARG("*NeedBase=%d", *NeedBase);
    return gcvSTATUS_OK;
}

gceSTATUS
gckHARDWARE_SetIsrManager(
   IN gckHARDWARE Hardware,
   IN gctISRMANAGERFUNC StartIsr,
   IN gctISRMANAGERFUNC StopIsr,
   IN void *Context
   )
{
    gceSTATUS status = gcvSTATUS_OK;

    gcmkHEADER_ARG("Hardware=0x%x, StartIsr=0x%x, StopIsr=0x%x, Context=0x%x",
                   Hardware, StartIsr, StopIsr, Context);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    if (StartIsr == NULL ||
        StopIsr == NULL ||
        Context == NULL)
    {
        status = gcvSTATUS_INVALID_ARGUMENT;

        gcmkFOOTER();
        return status;
    }

    Hardware->startIsr = StartIsr;
    Hardware->stopIsr = StopIsr;
    Hardware->isrContext = Context;

    /* Success. */
    gcmkFOOTER();

    return status;
}

/*******************************************************************************
**
**  gckHARDWARE_Compose
**
**  Start a composition.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckHARDWARE_Compose(
    IN gckHARDWARE Hardware,
    IN u32 ProcessID,
    IN gctPHYS_ADDR Physical,
    IN void *Logical,
    IN size_t Offset,
    IN size_t Size,
    IN u8 EventID
    )
{
#if !VIVANTE_NO_3D
    gceSTATUS status;
    u32 *triggerState;

    gcmkHEADER_ARG("Hardware=0x%x Physical=0x%x Logical=0x%x"
                   " Offset=%d Size=%d EventID=%d",
                   Hardware, Physical, Logical, Offset, Size, EventID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);
    gcmkVERIFY_ARGUMENT(((Size + 8) & 63) == 0);
    gcmkVERIFY_ARGUMENT(Logical != NULL);

    /* Program the trigger state. */
    triggerState = (u32 *) ((u8 *) Logical + Offset + Size);
    triggerState[0] = 0x0C03;
    triggerState[1]
        = gcmSETFIELD(0, 1:0, 0x1 )
        | gcmSETFIELD(0, 5:4, 0x3 )
        | gcmSETFIELD(0, 8:8, 1)
        | gcmSETFIELD(0, 24:24, 1)
        | gcmSETFIELD(0, 12:12, 1)
        | gcmSETFIELD(0, 20:16, EventID)
        ;

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Flush the cache for the wait/link. */
    gcmkONERROR(gckOS_CacheClean(
        Hardware->os, ProcessID, NULL,
        Physical, Logical, Offset + Size
        ));
#endif

    /* Start composition. */
    gcmkONERROR(gckOS_WriteRegisterEx(
        Hardware->os, Hardware->core, 0x00554,
        gcmSETFIELD(0, 1:0, 0x3 )
        ));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
#else
    /* Return the status. */
    return gcvSTATUS_NOT_SUPPORTED;
#endif
}

/*******************************************************************************
**
**  gckHARDWARE_IsFeatureAvailable
**
**  Verifies whether the specified feature is available in hardware.
**
**  INPUT:
**
**      gckHARDWARE Hardware
**          Pointer to an gckHARDWARE object.
**
**      gceFEATURE Feature
**          Feature to be verified.
*/
gceSTATUS
gckHARDWARE_IsFeatureAvailable(
    IN gckHARDWARE Hardware,
    IN gceFEATURE Feature
    )
{
    int available;

    gcmkHEADER_ARG("Hardware=0x%x Feature=%d", Hardware, Feature);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    /* Only features needed by common kernel logic added here. */
    switch (Feature)
    {
    case gcvFEATURE_END_EVENT:
        /*available = gcmVERIFYFIELDVALUE(Hardware->identity.chipMinorFeatures2,
            GC_MINOR_FEATURES2, END_EVENT, AVAILABLE
            );*/
        available = gcvFALSE;
        break;
    case gcvFEATURE_MC20:
        available = gcmVERIFYFIELDVALUE(Hardware->identity.chipMinorFeatures, 22:22, 0x1  );
        break;

    default:
        gcmkFATAL("Invalid feature has been requested.");
        available = gcvFALSE;
    }

    /* Return result. */
    gcmkFOOTER_ARG("%d", available ? gcvSTATUS_TRUE : gcvSTATUS_OK);
    return available ? gcvSTATUS_TRUE : gcvSTATUS_OK;
}

#if gcdFRAME_DB
static gceSTATUS
gckHARDWARE_ReadPerformanceRegister(
    IN gckHARDWARE Hardware,
    IN unsigned int PerformanceAddress,
    IN unsigned int IndexAddress,
    IN unsigned int IndexShift,
    IN unsigned int Index,
    OUT u32 *Value
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Hardware=0x%x PerformanceAddress=0x%x IndexAddress=0x%x "
                   "IndexShift=%u Index=%u",
                   Hardware, PerformanceAddress, IndexAddress, IndexShift,
                   Index);

    /* Write the index. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      IndexAddress,
                                      Index << IndexShift));

    /* Read the register. */
    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     PerformanceAddress,
                                     Value));

    /* Test for reset. */
    if (Index == 15)
    {
        /* Index another register to get out of reset. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os, Hardware->core, IndexAddress, 0));
    }

    /* Success. */
    gcmkFOOTER_ARG("*Value=0x%x", *Value);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckHARDWARE_GetFrameInfo(
    IN gckHARDWARE Hardware,
    OUT gcsHAL_FRAME_INFO * FrameInfo
    )
{
    gceSTATUS status;
    unsigned int i, clock;
    gcsHAL_FRAME_INFO info;
#if gcdFRAME_DB_RESET
	unsigned int reset;
#endif

    gcmkHEADER_ARG("Hardware=0x%x", Hardware);

    /* Get profile tick. */
    gcmkONERROR(gckOS_GetProfileTick(&info.ticks));

    /* Read SH counters and reset them. */
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0045C,
        0x00470,
        24,
        4,
        &info.shaderCycles));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0045C,
        0x00470,
        24,
        9,
        &info.vsInstructionCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0045C,
        0x00470,
        24,
        12,
        &info.vsTextureCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0045C,
        0x00470,
        24,
        7,
        &info.psInstructionCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0045C,
        0x00470,
        24,
        14,
        &info.psTextureCount));
#if gcdFRAME_DB_RESET
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0045C,
        0x00470,
        24,
        15,
        &reset));
#endif

    /* Read PA counters and reset them. */
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00460,
        0x00474,
        0,
        3,
        &info.vertexCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00460,
        0x00474,
        0,
        4,
        &info.primitiveCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00460,
        0x00474,
        0,
        7,
        &info.rejectedPrimitives));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00460,
        0x00474,
        0,
        8,
        &info.culledPrimitives));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00460,
        0x00474,
        0,
        6,
        &info.clippedPrimitives));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00460,
        0x00474,
        0,
        5,
        &info.outPrimitives));
#if gcdFRAME_DB_RESET
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00460,
        0x00474,
        0,
        15,
        &reset));
#endif

    /* Read RA counters and reset them. */
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00448,
        0x00474,
        16,
        3,
        &info.inPrimitives));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00448,
        0x00474,
        16,
        11,
        &info.culledQuadCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00448,
        0x00474,
        16,
        1,
        &info.totalQuadCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00448,
        0x00474,
        16,
        2,
        &info.quadCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00448,
        0x00474,
        16,
        0,
        &info.totalPixelCount));
#if gcdFRAME_DB_RESET
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00448,
        0x00474,
        16,
        15,
        &reset));
#endif

    /* Read TX counters and reset them. */
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0044C,
        0x00474,
        24,
        0,
        &info.bilinearRequests));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0044C,
        0x00474,
        24,
        1,
        &info.trilinearRequests));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0044C,
        0x00474,
        24,
        8,
        &info.txHitCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0044C,
        0x00474,
        24,
        9,
        &info.txMissCount));
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0044C,
        0x00474,
        24,
        6,
        &info.txBytes8));
#if gcdFRAME_DB_RESET
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x0044C,
        0x00474,
        24,
        15,
        &reset));
#endif

    /* Read clock control register. */
    gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                     Hardware->core,
                                     0x00000,
                                     &clock));

    /* Walk through all avaiable pixel pipes. */
    for (i = 0; i < Hardware->identity.pixelPipes; ++i)
    {
        /* Select proper pipe. */
        gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                          Hardware->core,
                                          0x00000,
                                          gcmSETFIELD(clock, 23:20, i)));

        /* Read cycle registers. */
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00078,
                                         &info.cycles[i]));
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x0007C,
                                         &info.idleCycles[i]));
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00438,
                                         &info.mcCycles[i]));

        /* Read bandwidth registers. */
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x0005C,
                                         &info.readRequests[i]));
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00040,
                                         &info.readBytes8[i]));
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00050,
                                         &info.writeRequests[i]));
        gcmkONERROR(gckOS_ReadRegisterEx(Hardware->os,
                                         Hardware->core,
                                         0x00044,
                                         &info.writeBytes8[i]));

        /* Read PE counters. */
        gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
            Hardware,
            0x00454,
            0x00470,
            16,
            0,
            &info.colorKilled[i]));
        gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
            Hardware,
            0x00454,
            0x00470,
            16,
            2,
            &info.colorDrawn[i]));
        gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
            Hardware,
            0x00454,
            0x00470,
            16,
            1,
            &info.depthKilled[i]));
        gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
            Hardware,
            0x00454,
            0x00470,
            16,
            3,
            &info.depthDrawn[i]));
    }

    /* Zero out remaning reserved counters. */
    for (; i < 8; ++i)
    {
        info.readBytes8[i]    = 0;
        info.writeBytes8[i]   = 0;
        info.cycles[i]        = 0;
        info.idleCycles[i]    = 0;
        info.mcCycles[i]      = 0;
        info.readRequests[i]  = 0;
        info.writeRequests[i] = 0;
        info.colorKilled[i]   = 0;
        info.colorDrawn[i]    = 0;
        info.depthKilled[i]   = 0;
        info.depthDrawn[i]    = 0;
    }

    /* Reset clock control register. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00000,
                                      clock));

    /* Reset cycle and bandwidth counters. */
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0003C,
                                      1));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x0003C,
                                      0));
    gcmkONERROR(gckOS_WriteRegisterEx(Hardware->os,
                                      Hardware->core,
                                      0x00078,
                                      0));

#if gcdFRAME_DB_RESET
    /* Reset PE counters. */
    gcmkONERROR(gckHARDWARE_ReadPerformanceRegister(
        Hardware,
        0x00454,
        0x00470,
        16,
        15,
        &reset));
#endif

    /* Copy to user. */
    if (copy_to_user(FrameInfo, &info, sizeof(info)) != 0)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif
