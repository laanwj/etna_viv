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

#ifndef __gc_hal_kernel_device_h_
#define __gc_hal_kernel_device_h_

/******************************************************************************\
******************************* gckGALDEVICE Structure *******************************
\******************************************************************************/

typedef struct _gckGALDEVICE
{
    /* Objects. */
    gckOS               os;
    gckKERNEL           kernels[gcdCORE_COUNT];

    /* Attributes. */
    size_t              internalSize;
    gctPHYS_ADDR        internalPhysical;
    void *              internalLogical;
    gckVIDMEM           internalVidMem;
    size_t              externalSize;
    gctPHYS_ADDR        externalPhysical;
    void *              externalLogical;
    gckVIDMEM           externalVidMem;
    gckVIDMEM           contiguousVidMem;
    void *              contiguousBase;
    gctPHYS_ADDR        contiguousPhysical;
    size_t              contiguousSize;
    int                 contiguousMapped;
    void *              contiguousMappedUser;
    size_t              systemMemorySize;
    u32                 systemMemoryBaseAddress;
    void *              registerBases[gcdCORE_COUNT];
    size_t              registerSizes[gcdCORE_COUNT];
    u32                 baseAddress;
    u32                 requestedRegisterMemBases[gcdCORE_COUNT];
    size_t              requestedRegisterMemSizes[gcdCORE_COUNT];
    u32                 requestedContiguousBase;
    size_t              requestedContiguousSize;

    /* IRQ management. */
    int                 irqLines[gcdCORE_COUNT];
    int                 isrInitializeds[gcdCORE_COUNT];
    int                 dataReadys[gcdCORE_COUNT];

    /* Thread management. */
    struct task_struct  *threadCtxts[gcdCORE_COUNT];
    struct semaphore    semas[gcdCORE_COUNT];
    int                 threadInitializeds[gcdCORE_COUNT];
    int                 killThread;

    /* Signal management. */
    int                 signal;

    /* Core mapping */
    gceCORE             coreMapping[8];

    /* States before suspend. */
    gceCHIPPOWERSTATE   statesStored[gcdCORE_COUNT];

    /* Clock management. */
    struct clk          *clk;
    int                 clk_enabled;

    /* Device pointer for dma_alloc_coherent */
    struct device       *dev;
}
* gckGALDEVICE;

typedef struct _gcsHAL_PRIVATE_DATA
{
    gckGALDEVICE        device;
    void *              mappedMemory;
    void *              contiguousLogical;
    /* The process opening the device may not be the same as the one that closes it. */
    u32                 pidOpen;
}
gcsHAL_PRIVATE_DATA, * gcsHAL_PRIVATE_DATA_PTR;

gceSTATUS gckGALDEVICE_Start(
    IN gckGALDEVICE Device
    );

gceSTATUS gckGALDEVICE_Stop(
    gckGALDEVICE Device
    );

gceSTATUS gckGALDEVICE_Construct(
    IN int IrqLine,
    IN u32 RegisterMemBase,
    IN size_t RegisterMemSize,
    IN int IrqLine2D,
    IN u32 RegisterMemBase2D,
    IN size_t RegisterMemSize2D,
    IN u32 ContiguousBase,
    IN size_t ContiguousSize,
    IN size_t BankSize,
    IN int FastClear,
    IN int Compression,
    IN u32 PhysBaseAddr,
    IN u32 PhysSize,
    IN int Signal,
    OUT gckGALDEVICE *Device
    );

gceSTATUS gckGALDEVICE_Destroy(
    IN gckGALDEVICE Device
    );

#endif /* __gc_hal_kernel_device_h_ */
