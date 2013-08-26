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




#include "gc_hal_kernel_linux.h"

#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <asm/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23)
#include <linux/math64.h>
#endif

#define _GC_OBJ_ZONE    gcvZONE_OS

/*******************************************************************************
***** Version Signature *******************************************************/

#ifdef ANDROID
const char * _PLATFORM = "\n\0$PLATFORM$Android$\n";
#else
const char * _PLATFORM = "\n\0$PLATFORM$Linux$\n";
#endif

#define USER_SIGNAL_TABLE_LEN_INIT  64

#define MEMORY_LOCK(os) \
    gcmkVERIFY_OK(gckOS_AcquireMutex( \
                                (os), \
                                (os)->memoryLock, \
                                gcvINFINITE))

#define MEMORY_UNLOCK(os) \
    gcmkVERIFY_OK(gckOS_ReleaseMutex((os), (os)->memoryLock))

#define MEMORY_MAP_LOCK(os) \
    gcmkVERIFY_OK(gckOS_AcquireMutex( \
                                (os), \
                                (os)->memoryMapLock, \
                                gcvINFINITE))

#define MEMORY_MAP_UNLOCK(os) \
    gcmkVERIFY_OK(gckOS_ReleaseMutex((os), (os)->memoryMapLock))

/* Protection bit when mapping memroy to user sapce */
#define gcmkPAGED_MEMROY_PROT(x)    pgprot_writecombine(x)

#if gcdNONPAGED_MEMORY_BUFFERABLE
#define gcmkIOREMAP                 ioremap_wc
#define gcmkNONPAGED_MEMROY_PROT(x) pgprot_writecombine(x)
#elif !gcdNONPAGED_MEMORY_CACHEABLE
#define gcmkIOREMAP                 ioremap_nocache
#define gcmkNONPAGED_MEMROY_PROT(x) pgprot_noncached(x)
#endif

#define gcdINFINITE_TIMEOUT     (60 * 1000)
#define gcdDETECT_TIMEOUT       0
#define gcdDETECT_DMA_ADDRESS   1
#define gcdDETECT_DMA_STATE     1

#define gcdUSE_NON_PAGED_MEMORY_CACHE 32
//####modified for marvell-bg2
#define NON_PAGED_MEMORY_CACHE_THRESHOLD 3		//order=3
//####end for marvell-bg2

/******************************************************************************\
********************************** Structures **********************************
\******************************************************************************/
#if gcdUSE_NON_PAGED_MEMORY_CACHE
typedef struct _gcsNonPagedMemoryCache
{
#ifndef NO_DMA_COHERENT
    gctINT                           size;
    gctSTRING                        addr;
    dma_addr_t                       dmaHandle;
#else
    long                             order;
    struct page *                    page;
#endif

    struct _gcsNonPagedMemoryCache * prev;
    struct _gcsNonPagedMemoryCache * next;
}
gcsNonPagedMemoryCache;
#endif /* gcdUSE_NON_PAGED_MEMORY_CACHE */

typedef struct _gcsUSER_MAPPING * gcsUSER_MAPPING_PTR;
typedef struct _gcsUSER_MAPPING
{
    /* Pointer to next mapping structure. */
    gcsUSER_MAPPING_PTR         next;

    /* Physical address of this mapping. */
    gctUINT32                   physical;

    /* Logical address of this mapping. */
    gctPOINTER                  logical;

    /* Number of bytes of this mapping. */
    gctSIZE_T                   bytes;

    /* Starting address of this mapping. */
    gctINT8_PTR                 start;

    /* Ending address of this mapping. */
    gctINT8_PTR                 end;
}
gcsUSER_MAPPING;

struct _gckOS
{
    /* Object. */
    gcsOBJECT                   object;

    /* Heap. */
    gckHEAP                     heap;

    /* Pointer to device */
    gckGALDEVICE                device;

    /* Memory management */
    gctPOINTER                  memoryLock;
    gctPOINTER                  memoryMapLock;

    struct _LINUX_MDL           *mdlHead;
    struct _LINUX_MDL           *mdlTail;

    /* Kernel process ID. */
    gctUINT32                   kernelProcessID;

    /* Signal management. */
    struct _signal
    {
        /* Unused signal ID number. */
        gctINT                  unused;

        /* The pointer to the table. */
        gctPOINTER *            table;

        /* Signal table length. */
        gctINT                  tableLen;

        /* The current unused signal ID. */
        gctINT                  currentID;

        /* Lock. */
        gctPOINTER              lock;
    }
    signal;

    gcsUSER_MAPPING_PTR         userMap;
    gctPOINTER                  debugLock;

#if gcdUSE_NON_PAGED_MEMORY_CACHE
    gctUINT                      cacheSize;
    gcsNonPagedMemoryCache *     cacheHead;
    gcsNonPagedMemoryCache *     cacheTail;
#endif
};

typedef struct _gcsSIGNAL * gcsSIGNAL_PTR;
typedef struct _gcsSIGNAL
{
    /* Kernel sync primitive. */
    struct completion obj;

    /* Manual reset flag. */
    gctBOOL manualReset;

    /* The reference counter. */
    atomic_t ref;

    /* The owner of the signal. */
    gctHANDLE process;
}
gcsSIGNAL;

typedef struct _gcsPageInfo * gcsPageInfo_PTR;
typedef struct _gcsPageInfo
{
    struct page **pages;
    gctUINT32_PTR pageTable;
}
gcsPageInfo;

typedef struct _gcsiDEBUG_REGISTERS * gcsiDEBUG_REGISTERS_PTR;
typedef struct _gcsiDEBUG_REGISTERS
{
    gctSTRING       module;
    gctUINT         index;
    gctUINT         shift;
    gctUINT         data;
    gctUINT         count;
    gctUINT32       signature;
}
gcsiDEBUG_REGISTERS;


/******************************************************************************\
******************************* Private Functions ******************************
\******************************************************************************/

static gceSTATUS
_VerifyDMA(
    IN gckOS Os,
    IN gceCORE Core,
    gctUINT32_PTR Address1,
    gctUINT32_PTR Address2,
    gctUINT32_PTR State1,
    gctUINT32_PTR State2
    )
{
    gceSTATUS status;
    gctUINT32 i;

    gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x660, State1));
    gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x664, Address1));

    for (i = 0; i < 500; i += 1)
    {
        gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x660, State2));
        gcmkONERROR(gckOS_ReadRegisterEx(Os, Core, 0x664, Address2));

        if (*Address1 != *Address2)
        {
            break;
        }

#if gcdDETECT_DMA_STATE
        if (*State1 != *State2)
        {
            break;
        }
#endif
    }

OnError:
    return status;
}

static gceSTATUS
_DumpDebugRegisters(
    IN gckOS Os,
    IN gcsiDEBUG_REGISTERS_PTR Descriptor
    )
{
    gceSTATUS status;
    gctUINT32 select;
    gctUINT32 data;
    gctUINT i;

    gcmkHEADER_ARG("Os=0x%X Descriptor=0x%X", Os, Descriptor);

    gcmkPRINT_N(4, "  %s debug registers:\n", Descriptor->module);

    select = 0xF << Descriptor->shift;

    for (i = 0; i < 500; i += 1)
    {
        gcmkONERROR(gckOS_WriteRegister(Os, Descriptor->index, select));
#if !gcdENABLE_RECOVERY
        gcmkONERROR(gckOS_Delay(Os, 1000));
#endif
        gcmkONERROR(gckOS_ReadRegister(Os, Descriptor->data, &data));

        if (data == Descriptor->signature)
        {
            break;
        }
    }

    if (i == 500)
    {
        gcmkPRINT_N(4, "    failed to obtain the signature (read 0x%08X).\n", data);
    }
    else
    {
        gcmkPRINT_N(8, "    signature = 0x%08X (%d read attempt(s))\n", data, i + 1);
    }

    for (i = 0; i < Descriptor->count; i += 1)
    {
        select = i << Descriptor->shift;

        gcmkONERROR(gckOS_WriteRegister(Os, Descriptor->index, select));
#if !gcdENABLE_RECOVERY
        gcmkONERROR(gckOS_Delay(Os, 1000));
#endif
        gcmkONERROR(gckOS_ReadRegister(Os, Descriptor->data, &data));

        gcmkPRINT_N(12, "    [0x%02X] 0x%08X\n", i, data);
    }

OnError:
    /* Return the error. */
    gcmkFOOTER();
    return status;
}

static gceSTATUS
_DumpGPUState(
    IN gckOS Os,
    IN gceCORE Core
    )
{
    static gctCONST_STRING _cmdState[] =
    {
        "PAR_IDLE_ST", "PAR_DEC_ST", "PAR_ADR0_ST", "PAR_LOAD0_ST",
        "PAR_ADR1_ST", "PAR_LOAD1_ST", "PAR_3DADR_ST", "PAR_3DCMD_ST",
        "PAR_3DCNTL_ST", "PAR_3DIDXCNTL_ST", "PAR_INITREQDMA_ST",
        "PAR_DRAWIDX_ST", "PAR_DRAW_ST", "PAR_2DRECT0_ST", "PAR_2DRECT1_ST",
        "PAR_2DDATA0_ST", "PAR_2DDATA1_ST", "PAR_WAITFIFO_ST", "PAR_WAIT_ST",
        "PAR_LINK_ST", "PAR_END_ST", "PAR_STALL_ST"
    };

    static gctCONST_STRING _cmdDmaState[] =
    {
        "CMD_IDLE_ST", "CMD_START_ST", "CMD_REQ_ST", "CMD_END_ST"
    };

    static gctCONST_STRING _cmdFetState[] =
    {
        "FET_IDLE_ST", "FET_RAMVALID_ST", "FET_VALID_ST"
    };

    static gctCONST_STRING _reqDmaState[] =
    {
        "REQ_IDLE_ST", "REQ_WAITIDX_ST", "REQ_CAL_ST"
    };

    static gctCONST_STRING _calState[] =
    {
        "CAL_IDLE_ST", "CAL_LDADR_ST", "CAL_IDXCALC_ST"
    };

    static gctCONST_STRING _veReqState[] =
    {
        "VER_IDLE_ST", "VER_CKCACHE_ST", "VER_MISS_ST"
    };

    static gcsiDEBUG_REGISTERS _dbgRegs[] =
    {
        { "RA", 0x474, 16, 0x448, 16, 0x12344321 },
        { "TX", 0x474, 24, 0x44C, 16, 0x12211221 },
        { "FE", 0x470,  0, 0x450, 16, 0xBABEF00D },
        { "PE", 0x470, 16, 0x454, 16, 0xBABEF00D },
        { "DE", 0x470,  8, 0x458, 16, 0xBABEF00D },
        { "SH", 0x470, 24, 0x45C, 16, 0xDEADBEEF },
        { "PA", 0x474,  0, 0x460, 16, 0x0000AAAA },
        { "SE", 0x474,  8, 0x464, 16, 0x5E5E5E5E },
        { "MC", 0x478,  0, 0x468, 16, 0x12345678 },
        { "HI", 0x478,  8, 0x46C, 16, 0xAAAAAAAA }
    };

    static gctUINT32 _otherRegs[] =
    {
        0x040, 0x044, 0x04C, 0x050, 0x054, 0x058, 0x05C, 0x060,
        0x43c, 0x440, 0x444, 0x414,
    };

    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;
    gckGALDEVICE device;
    gckKERNEL kernel;
    gctUINT32 idle, axi;
    gctUINT32 dmaAddress1, dmaAddress2;
    gctUINT32 dmaState1, dmaState2;
    gctUINT32 dmaLow, dmaHigh;
    gctUINT32 cmdState, cmdDmaState, cmdFetState;
    gctUINT32 dmaReqState, calState, veReqState;
    gctUINT i;

    gcmkHEADER_ARG("Os=0x%X, Core=%d", Os, Core);

    gcmkONERROR(gckOS_AcquireMutex(Os, Os->debugLock, gcvINFINITE));
    acquired = gcvTRUE;

    /* Extract the pointer to the gckGALDEVICE class. */
    device = (gckGALDEVICE) Os->device;

    /* TODO: Kernel shortcut. */
    kernel = device->kernels[Core];
    gcmkPRINT_N(4, "Core = 0x%d\n",Core);

    if (kernel == gcvNULL)
    {
        gcmkFOOTER();
        return gcvSTATUS_OK;
    }

    /* Reset register values. */
    idle        = axi         =
    dmaState1   = dmaState2   =
    dmaAddress1 = dmaAddress2 =
    dmaLow      = dmaHigh     = 0;

    /* Verify whether DMA is running. */
    gcmkONERROR(_VerifyDMA(
        Os, kernel->core, &dmaAddress1, &dmaAddress2, &dmaState1, &dmaState2
        ));

    cmdState    =  dmaState2        & 0x1F;
    cmdDmaState = (dmaState2 >>  8) & 0x03;
    cmdFetState = (dmaState2 >> 10) & 0x03;
    dmaReqState = (dmaState2 >> 12) & 0x03;
    calState    = (dmaState2 >> 14) & 0x03;
    veReqState  = (dmaState2 >> 16) & 0x03;

    gcmkONERROR(gckOS_ReadRegisterEx(Os, kernel->core, 0x004, &idle));
    gcmkONERROR(gckOS_ReadRegisterEx(Os, kernel->core, 0x00C, &axi));
    gcmkONERROR(gckOS_ReadRegisterEx(Os, kernel->core, 0x668, &dmaLow));
    gcmkONERROR(gckOS_ReadRegisterEx(Os, kernel->core, 0x66C, &dmaHigh));

    gcmkPRINT_N(0, "**************************\n");
    gcmkPRINT_N(0, "***   GPU STATE DUMP   ***\n");
    gcmkPRINT_N(0, "**************************\n");

    gcmkPRINT_N(4, "  axi      = 0x%08X\n", axi);

    gcmkPRINT_N(4, "  idle     = 0x%08X\n", idle);
    if ((idle & 0x00000001) == 0) gcmkPRINT_N(0, "    FE not idle\n");
    if ((idle & 0x00000002) == 0) gcmkPRINT_N(0, "    DE not idle\n");
    if ((idle & 0x00000004) == 0) gcmkPRINT_N(0, "    PE not idle\n");
    if ((idle & 0x00000008) == 0) gcmkPRINT_N(0, "    SH not idle\n");
    if ((idle & 0x00000010) == 0) gcmkPRINT_N(0, "    PA not idle\n");
    if ((idle & 0x00000020) == 0) gcmkPRINT_N(0, "    SE not idle\n");
    if ((idle & 0x00000040) == 0) gcmkPRINT_N(0, "    RA not idle\n");
    if ((idle & 0x00000080) == 0) gcmkPRINT_N(0, "    TX not idle\n");
    if ((idle & 0x00000100) == 0) gcmkPRINT_N(0, "    VG not idle\n");
    if ((idle & 0x00000200) == 0) gcmkPRINT_N(0, "    IM not idle\n");
    if ((idle & 0x00000400) == 0) gcmkPRINT_N(0, "    FP not idle\n");
    if ((idle & 0x00000800) == 0) gcmkPRINT_N(0, "    TS not idle\n");
    if ((idle & 0x80000000) != 0) gcmkPRINT_N(0, "    AXI low power mode\n");

    if (
        (dmaAddress1 == dmaAddress2)

#if gcdDETECT_DMA_STATE
     && (dmaState1 == dmaState2)
#endif
    )
    {
        gcmkPRINT_N(0, "  DMA appears to be stuck at this address:\n");
        gcmkPRINT_N(4, "    0x%08X\n", dmaAddress1);
    }
    else
    {
        if (dmaAddress1 == dmaAddress2)
        {
            gcmkPRINT_N(0, "  DMA address is constant, but state is changing:\n");
            gcmkPRINT_N(4, "    0x%08X\n", dmaState1);
            gcmkPRINT_N(4, "    0x%08X\n", dmaState2);
        }
        else
        {
            gcmkPRINT_N(0, "  DMA is running; known addresses are:\n");
            gcmkPRINT_N(4, "    0x%08X\n", dmaAddress1);
            gcmkPRINT_N(4, "    0x%08X\n", dmaAddress2);
        }
    }

    gcmkPRINT_N(4, "  dmaLow   = 0x%08X\n", dmaLow);
    gcmkPRINT_N(4, "  dmaHigh  = 0x%08X\n", dmaHigh);
    gcmkPRINT_N(4, "  dmaState = 0x%08X\n", dmaState2);
    gcmkPRINT_N(8, "    command state       = %d (%s)\n", cmdState,    _cmdState   [cmdState]);
    gcmkPRINT_N(8, "    command DMA state   = %d (%s)\n", cmdDmaState, _cmdDmaState[cmdDmaState]);
    gcmkPRINT_N(8, "    command fetch state = %d (%s)\n", cmdFetState, _cmdFetState[cmdFetState]);
    gcmkPRINT_N(8, "    DMA request state   = %d (%s)\n", dmaReqState, _reqDmaState[dmaReqState]);
    gcmkPRINT_N(8, "    cal state           = %d (%s)\n", calState,    _calState   [calState]);
    gcmkPRINT_N(8, "    VE request state    = %d (%s)\n", veReqState,  _veReqState [veReqState]);

    for (i = 0; i < gcmCOUNTOF(_dbgRegs); i += 1)
    {
        gcmkONERROR(_DumpDebugRegisters(Os, &_dbgRegs[i]));
    }

    if (kernel->hardware->identity.chipFeatures & (1 << 4))
    {
        gctUINT32 read0, read1, write;

        read0 = read1 = write = 0;

        gcmkONERROR(gckOS_ReadRegisterEx(Os, kernel->core, 0x43C, &read0));
        gcmkONERROR(gckOS_ReadRegisterEx(Os, kernel->core, 0x440, &read1));
        gcmkONERROR(gckOS_ReadRegisterEx(Os, kernel->core, 0x444, &write));

        gcmkPRINT_N(4, "  read0    = 0x%08X\n", read0);
        gcmkPRINT_N(4, "  read1    = 0x%08X\n", read1);
        gcmkPRINT_N(4, "  write    = 0x%08X\n", write);
    }

    gcmkPRINT_N(0, "  Other Registers:\n");
    for (i = 0; i < gcmCOUNTOF(_otherRegs); i += 1)
    {
        gctUINT32 read;
        gcmkONERROR(gckOS_ReadRegisterEx(Os, kernel->core, _otherRegs[i], &read));
        gcmkPRINT_N(12, "    [0x%04X] 0x%08X\n", _otherRegs[i], read);
    }

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->debugLock));
    }

    /* Return the error. */
    gcmkFOOTER();
    return status;
}

static gctINT
_GetProcessID(
    void
    )
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
    return task_tgid_vnr(current);
#else
    return current->tgid;
#endif
}

static gctINT
_GetThreadID(
    void
    )
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
    return task_pid_vnr(current);
#else
    return current->pid;
#endif
}

static PLINUX_MDL
_CreateMdl(
    IN gctINT ProcessID
    )
{
    PLINUX_MDL  mdl;

    gcmkHEADER_ARG("ProcessID=%d", ProcessID);

    mdl = (PLINUX_MDL)kmalloc(sizeof(struct _LINUX_MDL), GFP_KERNEL | __GFP_NOWARN);
    if (mdl == gcvNULL)
    {
        gcmkFOOTER_NO();
        return gcvNULL;
    }

    mdl->pid    = ProcessID;
    mdl->maps   = gcvNULL;
    mdl->prev   = gcvNULL;
    mdl->next   = gcvNULL;

    gcmkFOOTER_ARG("0x%X", mdl);
    return mdl;
}

static gceSTATUS
_DestroyMdlMap(
    IN PLINUX_MDL Mdl,
    IN PLINUX_MDL_MAP MdlMap
    );

static gceSTATUS
_DestroyMdl(
    IN PLINUX_MDL Mdl
    )
{
    PLINUX_MDL_MAP mdlMap, next;

    gcmkHEADER_ARG("Mdl=0x%X", Mdl);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Mdl != gcvNULL);

    mdlMap = Mdl->maps;

    while (mdlMap != gcvNULL)
    {
        next = mdlMap->next;

        gcmkVERIFY_OK(_DestroyMdlMap(Mdl, mdlMap));

        mdlMap = next;
    }

    kfree(Mdl);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

static PLINUX_MDL_MAP
_CreateMdlMap(
    IN PLINUX_MDL Mdl,
    IN gctINT ProcessID
    )
{
    PLINUX_MDL_MAP  mdlMap;

    gcmkHEADER_ARG("Mdl=0x%X ProcessID=%d", Mdl, ProcessID);

    mdlMap = (PLINUX_MDL_MAP)kmalloc(sizeof(struct _LINUX_MDL_MAP), GFP_KERNEL | __GFP_NOWARN);
    if (mdlMap == gcvNULL)
    {
        gcmkFOOTER_NO();
        return gcvNULL;
    }

    mdlMap->pid     = ProcessID;
    mdlMap->vmaAddr = gcvNULL;
    mdlMap->vma     = gcvNULL;

    mdlMap->next    = Mdl->maps;
    Mdl->maps       = mdlMap;

    gcmkFOOTER_ARG("0x%X", mdlMap);
    return mdlMap;
}

static gceSTATUS
_DestroyMdlMap(
    IN PLINUX_MDL Mdl,
    IN PLINUX_MDL_MAP MdlMap
    )
{
    PLINUX_MDL_MAP  prevMdlMap;

    gcmkHEADER_ARG("Mdl=0x%X MdlMap=0x%X", Mdl, MdlMap);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(MdlMap != gcvNULL);
    gcmkASSERT(Mdl->maps != gcvNULL);

    if (Mdl->maps == MdlMap)
    {
        Mdl->maps = MdlMap->next;
    }
    else
    {
        prevMdlMap = Mdl->maps;

        while (prevMdlMap->next != MdlMap)
        {
            prevMdlMap = prevMdlMap->next;

            gcmkASSERT(prevMdlMap != gcvNULL);
        }

        prevMdlMap->next = MdlMap->next;
    }

    kfree(MdlMap);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

extern PLINUX_MDL_MAP
FindMdlMap(
    IN PLINUX_MDL Mdl,
    IN gctINT ProcessID
    )
{
    PLINUX_MDL_MAP  mdlMap;

    gcmkHEADER_ARG("Mdl=0x%X ProcessID=%d", Mdl, ProcessID);
    if(Mdl == gcvNULL)
    {
        gcmkFOOTER_NO();
        return gcvNULL;
    }
    mdlMap = Mdl->maps;

    while (mdlMap != gcvNULL)
    {
        if (mdlMap->pid == ProcessID)
        {
            gcmkFOOTER_ARG("0x%X", mdlMap);
            return mdlMap;
        }

        mdlMap = mdlMap->next;
    }

    gcmkFOOTER_NO();
    return gcvNULL;
}

void
OnProcessExit(
    IN gckOS Os,
    IN gckKERNEL Kernel
    )
{
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
static inline int
is_vmalloc_addr(
    void *Addr
    )
{
    unsigned long addr = (unsigned long)Addr;

    return addr >= VMALLOC_START && addr < VMALLOC_END;
}
#endif

static void
_NonContiguousFree(
    IN struct page ** Pages,
    IN gctUINT32 NumPages
    )
{
    gctINT i;

    gcmkHEADER_ARG("Pages=0x%X, NumPages=%d", Pages, NumPages);

    gcmkASSERT(Pages != gcvNULL);

    for (i = 0; i < NumPages; i++)
    {
        __free_page(Pages[i]);
    }

    if (is_vmalloc_addr(Pages))
    {
        vfree(Pages);
    }
    else
    {
        kfree(Pages);
    }

    gcmkFOOTER_NO();
}

static struct page **
_NonContiguousAlloc(
    IN gctUINT32 NumPages
    )
{
    struct page ** pages;
    struct page *p;
    gctINT i, size;

    gcmkHEADER_ARG("NumPages=%lu", NumPages);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
    if (NumPages > totalram_pages)
#else
    if (NumPages > num_physpages)
#endif
    {
        gcmkFOOTER_NO();
        return gcvNULL;
    }

    size = NumPages * sizeof(struct page *);

    pages = kmalloc(size, GFP_KERNEL | __GFP_NOWARN);

    if (!pages)
    {
        pages = vmalloc(size);

        if (!pages)
        {
            gcmkFOOTER_NO();
            return gcvNULL;
        }
    }

    for (i = 0; i < NumPages; i++)
    {
        p = alloc_page(GFP_KERNEL | __GFP_HIGHMEM | __GFP_NOWARN);

        if (!p)
        {
            _NonContiguousFree(pages, i);
            gcmkFOOTER_NO();
            return gcvNULL;
        }

        pages[i] = p;
    }

    gcmkFOOTER_ARG("pages=0x%X", pages);
    return pages;
}

static inline struct page *
_NonContiguousToPage(
    IN struct page ** Pages,
    IN gctUINT32 Index
    )
{
    gcmkASSERT(Pages != gcvNULL);
    return Pages[Index];
}

static inline unsigned long
_NonContiguousToPfn(
    IN struct page ** Pages,
    IN gctUINT32 Index
    )
{
    gcmkASSERT(Pages != gcvNULL);
    return page_to_pfn(_NonContiguousToPage(Pages, Index));
}

static inline unsigned long
_NonContiguousToPhys(
    IN struct page ** Pages,
    IN gctUINT32 Index
    )
{
    gcmkASSERT(Pages != gcvNULL);
    return page_to_phys(_NonContiguousToPage(Pages, Index));
}


#if gcdUSE_NON_PAGED_MEMORY_CACHE

static gctBOOL
_AddNonPagedMemoryCache(
    gckOS Os,
#ifndef NO_DMA_COHERENT
    gctINT Size,
    gctSTRING Addr,
    dma_addr_t DmaHandle
#else
    long Order,
    struct page * Page
#endif
    )
{
    gcsNonPagedMemoryCache *cache;

    //gckOS_Print("_AddNonPagedMemoryCache: cacheSize=%d, Order=%d", Os->cacheSize, Order);

    if (Os->cacheSize >= gcdUSE_NON_PAGED_MEMORY_CACHE)
    {
        return gcvFALSE;
    }

//####modified for marvell-bg2
#ifdef NO_DMA_COHERENT
    if (Order < NON_PAGED_MEMORY_CACHE_THRESHOLD)
    {
        //if size < 32 KB, let it allocate from linux memory directly
        return gcvFALSE;
    }
#endif
//####end for marvell-bg2

    /* Allocate the cache record */
    cache = (gcsNonPagedMemoryCache *)kmalloc(sizeof(gcsNonPagedMemoryCache), GFP_ATOMIC);

    if (cache == gcvNULL) return gcvFALSE;

#ifndef NO_DMA_COHERENT
    cache->size  = Size;
    cache->addr  = Addr;
    cache->dmaHandle = DmaHandle;
#else
    cache->order = Order;
    cache->page  = Page;
#endif

    /* Add to list */
    if (Os->cacheHead == gcvNULL)
    {
        cache->prev   = gcvNULL;
        cache->next   = gcvNULL;
        Os->cacheHead =
        Os->cacheTail = cache;
    }
    else
    {
        /* Add to the tail. */
        cache->prev         = Os->cacheTail;
        cache->next         = gcvNULL;
        Os->cacheTail->next = cache;
        Os->cacheTail       = cache;
    }

    Os->cacheSize++;

    //gckOS_Print("_AddNonPagedMemoryCache: sucess, cacheSize=%d, page=%p", Os->cacheSize, Page);

    return gcvTRUE;
}

#ifndef NO_DMA_COHERENT
static gctSTRING
_GetNonPagedMemoryCache(
    gckOS Os,
    gctINT Size,
    dma_addr_t * DmaHandle
    )
#else
static struct page *
_GetNonPagedMemoryCache(
    gckOS Os,
    long Order
    )
#endif
{
    gcsNonPagedMemoryCache *cache;
#ifndef NO_DMA_COHERENT
    gctSTRING addr;
#else
    struct page * page;
#endif

    //gckOS_Print("_GetNonPagedMemoryCache: cacheSize=%d, Order=%d", Os->cacheSize, Order);

//####modified for marvell-bg2
#ifdef NO_DMA_COHERENT
    if (Order < NON_PAGED_MEMORY_CACHE_THRESHOLD)
    {
        //if size < 32 KB, let it allocate from linux memory directly
        return gcvFALSE;
    }
#endif
//####end for marvell-bg2

    if (Os->cacheHead == gcvNULL) return gcvNULL;

    /* Find the right cache */
    cache = Os->cacheHead;

    while (cache != gcvNULL)
    {
#ifndef NO_DMA_COHERENT
        if (cache->size == Size) break;
#else
        if (cache->order == Order) break;
#endif

        cache = cache->next;
    }

    if (cache == gcvNULL) return gcvNULL;

    /* Remove the cache from list */
    if (cache == Os->cacheHead)
    {
        Os->cacheHead = cache->next;

        if (Os->cacheHead == gcvNULL)
        {
            Os->cacheTail = gcvNULL;
        }
    }
    else
    {
        cache->prev->next = cache->next;

        if (cache == Os->cacheTail)
        {
            Os->cacheTail = cache->prev;
        }
        else
        {
            cache->next->prev = cache->prev;
        }
    }

    /* Destroy cache */
#ifndef NO_DMA_COHERENT
    addr       = cache->addr;
    *DmaHandle = cache->dmaHandle;
#else
    page       = cache->page;
#endif

    kfree(cache);

    Os->cacheSize--;

    //gckOS_Print("_GetNonPagedMemoryCache: sucess, cacheSize=%d, page=%p", Os->cacheSize, page);

#ifndef NO_DMA_COHERENT
    return addr;
#else
    return page;
#endif
}

static void
_FreeAllNonPagedMemoryCache(
    gckOS Os
    )
{
    gcsNonPagedMemoryCache *cache, *nextCache;

    MEMORY_LOCK(Os);

    cache = Os->cacheHead;

    while (cache != gcvNULL)
    {
        if (cache != Os->cacheTail)
        {
            nextCache = cache->next;
        }
        else
        {
            nextCache = gcvNULL;
        }

        /* Remove the cache from list */
        if (cache == Os->cacheHead)
        {
            Os->cacheHead = cache->next;

            if (Os->cacheHead == gcvNULL)
            {
                Os->cacheTail = gcvNULL;
            }
        }
        else
        {
            cache->prev->next = cache->next;

            if (cache == Os->cacheTail)
            {
                Os->cacheTail = cache->prev;
            }
            else
            {
                cache->next->prev = cache->prev;
            }
        }

#ifndef NO_DMA_COHERENT
    dma_free_coherent(gcvNULL,
                    cache->size,
                    cache->addr,
                    cache->dmaHandle);
#else
    free_pages((unsigned long)page_address(cache->page), cache->order);
#endif

        kfree(cache);

        cache = nextCache;
    }

    MEMORY_UNLOCK(Os);
}

#endif /* gcdUSE_NON_PAGED_MEMORY_CACHE */

/*******************************************************************************
**
**  gckOS_Construct
**
**  Construct a new gckOS object.
**
**  INPUT:
**
**      gctPOINTER Context
**          Pointer to the gckGALDEVICE class.
**
**  OUTPUT:
**
**      gckOS * Os
**          Pointer to a variable that will hold the pointer to the gckOS object.
*/
gceSTATUS
gckOS_Construct(
    IN gctPOINTER Context,
    OUT gckOS * Os
    )
{
    gckOS os;
    gceSTATUS status;

    gcmkHEADER_ARG("Context=0x%X", Context);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Os != gcvNULL);

    /* Allocate the gckOS object. */
    os = (gckOS) kmalloc(gcmSIZEOF(struct _gckOS), GFP_KERNEL | __GFP_NOWARN);

    if (os == gcvNULL)
    {
        /* Out of memory. */
        gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_MEMORY);
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    /* Zero the memory. */
    gckOS_ZeroMemory(os, gcmSIZEOF(struct _gckOS));

    /* Initialize the gckOS object. */
    os->object.type = gcvOBJ_OS;

    /* Set device device. */
    os->device = Context;

    /* IMPORTANT! No heap yet. */
    os->heap = gcvNULL;

    /* Initialize the memory lock. */
    gcmkONERROR(gckOS_CreateMutex(os, &os->memoryLock));
    gcmkONERROR(gckOS_CreateMutex(os, &os->memoryMapLock));

    /* Create debug lock mutex. */
    gcmkONERROR(gckOS_CreateMutex(os, &os->debugLock));


    os->mdlHead = os->mdlTail = gcvNULL;

    /* Get the kernel process ID. */
    gcmkONERROR(gckOS_GetProcessID(&os->kernelProcessID));

    /*
     * Initialize the signal manager.
     * It creates the signals to be used in
     * the user space.
     */

    /* Initialize mutex. */
    gcmkONERROR(
        gckOS_CreateMutex(os, &os->signal.lock));

    /* Initialize the signal table. */
    os->signal.table =
        kmalloc(gcmSIZEOF(gctPOINTER) * USER_SIGNAL_TABLE_LEN_INIT, GFP_KERNEL | __GFP_NOWARN);

    if (os->signal.table == gcvNULL)
    {
        /* Out of memory. */
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    gckOS_ZeroMemory(os->signal.table,
                     gcmSIZEOF(gctPOINTER) * USER_SIGNAL_TABLE_LEN_INIT);

    /* Set the signal table length. */
    os->signal.tableLen = USER_SIGNAL_TABLE_LEN_INIT;

    /* The table is empty. */
    os->signal.unused = os->signal.tableLen;

    /* Initial signal ID. */
    os->signal.currentID = 0;

#if gcdUSE_NON_PAGED_MEMORY_CACHE
    os->cacheSize = 0;
    os->cacheHead = gcvNULL;
    os->cacheTail = gcvNULL;
#endif

    /* Return pointer to the gckOS object. */
    *Os = os;

    /* Success. */
    gcmkFOOTER_ARG("*Os=0x%X", *Os);
    return gcvSTATUS_OK;

OnError:
    /* Roll back any allocation. */
    if (os->signal.table != gcvNULL)
    {
        kfree(os->signal.table);
    }

    if (os->signal.lock != gcvNULL)
    {
        gcmkVERIFY_OK(
            gckOS_DeleteMutex(os, os->signal.lock));
    }

    if (os->heap != gcvNULL)
    {
        gcmkVERIFY_OK(
            gckHEAP_Destroy(os->heap));
    }

    if (os->memoryMapLock != gcvNULL)
    {
        gcmkVERIFY_OK(
            gckOS_DeleteMutex(os, os->memoryMapLock));
    }

    if (os->memoryLock != gcvNULL)
    {
        gcmkVERIFY_OK(
            gckOS_DeleteMutex(os, os->memoryLock));
    }

    if (os->debugLock != gcvNULL)
    {
        gcmkVERIFY_OK(
            gckOS_DeleteMutex(os, os->debugLock));
    }

    kfree(os);

    /* Return the error. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_Destroy
**
**  Destroy an gckOS object.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object that needs to be destroyed.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_Destroy(
    IN gckOS Os
    )
{
    gckHEAP heap;

    gcmkHEADER_ARG("Os=0x%X", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

#if gcdUSE_NON_PAGED_MEMORY_CACHE
    _FreeAllNonPagedMemoryCache(Os);
#endif

    /*
     * Destroy the signal manager.
     */

    /* Destroy the mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Os, Os->signal.lock));

    /* Free the signal table. */
    kfree(Os->signal.table);

    if (Os->heap != gcvNULL)
    {
        /* Mark gckHEAP as gone. */
        heap     = Os->heap;
        Os->heap = gcvNULL;

        /* Destroy the gckHEAP object. */
        gcmkVERIFY_OK(gckHEAP_Destroy(heap));
    }

    /* Destroy the memory lock. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Os, Os->memoryMapLock));
    gcmkVERIFY_OK(gckOS_DeleteMutex(Os, Os->memoryLock));

    /* Destroy debug lock mutex. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Os, Os->debugLock));

    /* Flush the debug cache. */
    gcmkDEBUGFLUSH(~0U);

    /* Mark the gckOS object as unknown. */
    Os->object.type = gcvOBJ_UNKNOWN;

    /* Free the gckOS object. */
    kfree(Os);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

#ifdef NO_DMA_COHERENT
static gctSTRING
_CreateKernelVirtualMapping(
    IN struct page * Page,
    IN gctINT NumPages
    )
{
    gctSTRING addr = 0;

#if gcdNONPAGED_MEMORY_CACHEABLE
    addr = page_address(Page);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
    struct page ** pages;
    gctINT i;

    pages = kmalloc(sizeof(struct page *) * NumPages, GFP_KERNEL | __GFP_NOWARN);

    if (!pages)
    {
        return gcvNULL;
    }

    for (i = 0; i < NumPages; i++)
    {
        pages[i] = nth_page(Page, i);
    }

    /* ioremap() can't work on system memory since 2.6.38. */
    addr = vmap(pages, NumPages, 0, gcmkNONPAGED_MEMROY_PROT(PAGE_KERNEL));

    kfree(pages);
#else
    addr = gcmkIOREMAP(page_to_phys(Page), NumPages * PAGE_SIZE);
#endif

    return addr;
}

static void
_DestoryKernelVirtualMapping(
    IN gctSTRING Addr
    )
{
#if !gcdNONPAGED_MEMORY_CACHEABLE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
    vunmap(Addr);
#   else
    iounmap(Addr);
#   endif
#endif
}
#endif

/*******************************************************************************
**
**  gckOS_Allocate
**
**  Allocate memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIZE_T Bytes
**          Number of bytes to allocate.
**
**  OUTPUT:
**
**      gctPOINTER * Memory
**          Pointer to a variable that will hold the allocated memory location.
*/
gceSTATUS
gckOS_Allocate(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Memory
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Bytes=%lu", Os, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);

    /* Do we have a heap? */
    if (Os->heap != gcvNULL)
    {
        /* Allocate from the heap. */
        gcmkONERROR(gckHEAP_Allocate(Os->heap, Bytes, Memory));
    }
    else
    {
        gcmkONERROR(gckOS_AllocateMemory(Os, Bytes, Memory));
    }

    /* Success. */
    gcmkFOOTER_ARG("*Memory=0x%X", *Memory);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_Free
**
**  Free allocated memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Memory
**          Pointer to memory allocation to free.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_Free(
    IN gckOS Os,
    IN gctPOINTER Memory
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Memory=0x%X", Os, Memory);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);

    /* Do we have a heap? */
    if (Os->heap != gcvNULL)
    {
        /* Free from the heap. */
        gcmkONERROR(gckHEAP_Free(Os->heap, Memory));
    }
    else
    {
        gcmkONERROR(gckOS_FreeMemory(Os, Memory));
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
**  gckOS_AllocateMemory
**
**  Allocate memory wrapper.
**
**  INPUT:
**
**      gctSIZE_T Bytes
**          Number of bytes to allocate.
**
**  OUTPUT:
**
**      gctPOINTER * Memory
**          Pointer to a variable that will hold the allocated memory location.
*/
gceSTATUS
gckOS_AllocateMemory(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Memory
    )
{
    gctPOINTER memory;
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Bytes=%lu", Os, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);

    if (Bytes > PAGE_SIZE)
    {
        memory = (gctPOINTER) vmalloc(Bytes);
    }
    else
    {
        memory = (gctPOINTER) kmalloc(Bytes, GFP_KERNEL | __GFP_NOWARN);
    }

    if (memory == gcvNULL)
    {
        /* Out of memory. */
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Return pointer to the memory allocation. */
    *Memory = memory;

    /* Success. */
    gcmkFOOTER_ARG("*Memory=0x%X", *Memory);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_FreeMemory
**
**  Free allocated memory wrapper.
**
**  INPUT:
**
**      gctPOINTER Memory
**          Pointer to memory allocation to free.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_FreeMemory(
    IN gckOS Os,
    IN gctPOINTER Memory
    )
{
    gcmkHEADER_ARG("Memory=0x%X", Memory);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);

    /* Free the memory from the OS pool. */
    if (is_vmalloc_addr(Memory))
    {
        vfree(Memory);
    }
    else
    {
        kfree(Memory);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_MapMemory
**
**  Map physical memory into the current process.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Start of physical address memory.
**
**      gctSIZE_T Bytes
**          Number of bytes to map.
**
**  OUTPUT:
**
**      gctPOINTER * Memory
**          Pointer to a variable that will hold the logical address of the
**          mapped memory.
*/
gceSTATUS
gckOS_MapMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Logical
    )
{
    PLINUX_MDL_MAP  mdlMap;
    PLINUX_MDL      mdl = (PLINUX_MDL)Physical;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%lu", Os, Physical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != 0);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    MEMORY_LOCK(Os);

    mdlMap = FindMdlMap(mdl, _GetProcessID());

    if (mdlMap == gcvNULL)
    {
        mdlMap = _CreateMdlMap(mdl, _GetProcessID());

        if (mdlMap == gcvNULL)
        {
            MEMORY_UNLOCK(Os);

            gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_MEMORY);
            return gcvSTATUS_OUT_OF_MEMORY;
        }
    }

    if (mdlMap->vmaAddr == gcvNULL)
    {
        down_write(&current->mm->mmap_sem);

        mdlMap->vmaAddr = (char *)do_mmap_pgoff(gcvNULL,
                    0L,
                    mdl->numPages * PAGE_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    0);

        if (IS_ERR(mdlMap->vmaAddr))
        {
            gcmkTRACE(
                gcvLEVEL_ERROR,
                "%s(%d): do_mmap_pgoff error",
                __FUNCTION__, __LINE__
                );

            gcmkTRACE(
                gcvLEVEL_ERROR,
                "%s(%d): mdl->numPages: %d mdl->vmaAddr: 0x%X",
                __FUNCTION__, __LINE__,
                mdl->numPages,
                mdlMap->vmaAddr
                );

            mdlMap->vmaAddr = gcvNULL;

            up_write(&current->mm->mmap_sem);

            MEMORY_UNLOCK(Os);

            gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_MEMORY);
            return gcvSTATUS_OUT_OF_MEMORY;
        }

        mdlMap->vma = find_vma(current->mm, (unsigned long)mdlMap->vmaAddr);

        if (!mdlMap->vma)
        {
            gcmkTRACE(
                gcvLEVEL_ERROR,
                "%s(%d): find_vma error.",
                __FUNCTION__, __LINE__
                );

            mdlMap->vmaAddr = gcvNULL;

            up_write(&current->mm->mmap_sem);

            MEMORY_UNLOCK(Os);

            gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_RESOURCES);
            return gcvSTATUS_OUT_OF_RESOURCES;
        }

#ifndef NO_DMA_COHERENT
        if (dma_mmap_coherent(gcvNULL,
                    mdlMap->vma,
                    mdl->addr,
                    mdl->dmaHandle,
                    mdl->numPages * PAGE_SIZE) < 0)
        {
            up_write(&current->mm->mmap_sem);

            gcmkTRACE(
                gcvLEVEL_ERROR,
                "%s(%d): dma_mmap_coherent error.",
                __FUNCTION__, __LINE__
                );

            mdlMap->vmaAddr = gcvNULL;

            MEMORY_UNLOCK(Os);

            gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_RESOURCES);
            return gcvSTATUS_OUT_OF_RESOURCES;
        }
#else
#if !gcdPAGED_MEMORY_CACHEABLE
        mdlMap->vma->vm_page_prot = gcmkPAGED_MEMROY_PROT(mdlMap->vma->vm_page_prot);
        mdlMap->vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_RESERVED;
#   endif
        mdlMap->vma->vm_pgoff = 0;

        if (remap_pfn_range(mdlMap->vma,
                            mdlMap->vma->vm_start,
                            mdl->dmaHandle >> PAGE_SHIFT,
                            mdl->numPages*PAGE_SIZE,
                            mdlMap->vma->vm_page_prot) < 0)
        {
            up_write(&current->mm->mmap_sem);

            gcmkTRACE(
                gcvLEVEL_ERROR,
                "%s(%d): remap_pfn_range error.",
                __FUNCTION__, __LINE__
                );

            mdlMap->vmaAddr = gcvNULL;

            MEMORY_UNLOCK(Os);

            gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_RESOURCES);
            return gcvSTATUS_OUT_OF_RESOURCES;
        }
#endif

        up_write(&current->mm->mmap_sem);
    }

    MEMORY_UNLOCK(Os);

    *Logical = mdlMap->vmaAddr;

    gcmkFOOTER_ARG("*Logical=0x%X", *Logical);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_UnmapMemory
**
**  Unmap physical memory out of the current process.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Start of physical address memory.
**
**      gctSIZE_T Bytes
**          Number of bytes to unmap.
**
**      gctPOINTER Memory
**          Pointer to a previously mapped memory region.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UnmapMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical
    )
{
    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%lu Logical=0x%X",
                   Os, Physical, Bytes, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != 0);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    gckOS_UnmapMemoryEx(Os, Physical, Bytes, Logical, _GetProcessID());

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}


/*******************************************************************************
**
**  gckOS_UnmapMemoryEx
**
**  Unmap physical memory in the specified process.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Start of physical address memory.
**
**      gctSIZE_T Bytes
**          Number of bytes to unmap.
**
**      gctPOINTER Memory
**          Pointer to a previously mapped memory region.
**
**      gctUINT32 PID
**          Pid of the process that opened the device and mapped this memory.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UnmapMemoryEx(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical,
    IN gctUINT32 PID
    )
{
    PLINUX_MDL_MAP          mdlMap;
    PLINUX_MDL              mdl = (PLINUX_MDL)Physical;
    struct task_struct *    task;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%lu Logical=0x%X PID=%d",
                   Os, Physical, Bytes, Logical, PID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != 0);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(PID != 0);

    MEMORY_LOCK(Os);

    if (Logical)
    {
        mdlMap = FindMdlMap(mdl, PID);

        if (mdlMap == gcvNULL || mdlMap->vmaAddr == gcvNULL)
        {
            MEMORY_UNLOCK(Os);

            gcmkFOOTER_ARG("status=%d", gcvSTATUS_INVALID_ARGUMENT);
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        /* Get the current pointer for the task with stored pid. */
        task = FIND_TASK_BY_PID(mdlMap->pid);

        if (task != gcvNULL && task->mm != gcvNULL)
        {
            down_write(&task->mm->mmap_sem);
            do_munmap(task->mm, (unsigned long)Logical, mdl->numPages*PAGE_SIZE);
            up_write(&task->mm->mmap_sem);
        }
        else
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_INFO, gcvZONE_OS,
                "%s(%d): can't find the task with pid->%d. No unmapping",
                __FUNCTION__, __LINE__,
                mdlMap->pid
                );
        }

        gcmkVERIFY_OK(_DestroyMdlMap(mdl, mdlMap));
    }

    MEMORY_UNLOCK(Os);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AllocateNonPagedMemory
**
**  Allocate a number of pages from non-paged memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctBOOL InUserSpace
**          gcvTRUE if the pages need to be mapped into user space.
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that holds the number of bytes to allocate.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that hold the number of bytes allocated.
**
**      gctPHYS_ADDR * Physical
**          Pointer to a variable that will hold the physical address of the
**          allocation.
**
**      gctPOINTER * Logical
**          Pointer to a variable that will hold the logical address of the
**          allocation.
*/
gceSTATUS
gckOS_AllocateNonPagedMemory(
    IN gckOS Os,
    IN gctBOOL InUserSpace,
    IN OUT gctSIZE_T * Bytes,
    OUT gctPHYS_ADDR * Physical,
    OUT gctPOINTER * Logical
    )
{
    gctSIZE_T bytes;
    gctINT numPages;
    PLINUX_MDL mdl = gcvNULL;
    PLINUX_MDL_MAP mdlMap = gcvNULL;
    gctSTRING addr;
#ifdef NO_DMA_COHERENT
    struct page * page;
    long size, order;
    gctPOINTER vaddr;
#endif
    gctBOOL locked = gcvFALSE;
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X InUserSpace=%d *Bytes=%lu",
                   Os, InUserSpace, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes != gcvNULL);
    gcmkVERIFY_ARGUMENT(*Bytes > 0);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    /* Align number of bytes to page size. */
    bytes = gcmALIGN(*Bytes, PAGE_SIZE);

    /* Get total number of pages.. */
    numPages = GetPageCount(bytes, 0);

    /* Allocate mdl+vector structure */
    mdl = _CreateMdl(_GetProcessID());
    if (mdl == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    mdl->pagedMem = 0;
    mdl->numPages = numPages;

    MEMORY_LOCK(Os);
    locked = gcvTRUE;

#ifndef NO_DMA_COHERENT
#if gcdUSE_NON_PAGED_MEMORY_CACHE
    addr = _GetNonPagedMemoryCache(Os,
                mdl->numPages * PAGE_SIZE,
                &mdl->dmaHandle);

    if (addr == gcvNULL)
#endif
    {
        addr = dma_alloc_coherent(gcvNULL,
                mdl->numPages * PAGE_SIZE,
                &mdl->dmaHandle,
                GFP_KERNEL | __GFP_NOWARN);
    }
#else
    size    = mdl->numPages * PAGE_SIZE;
    order   = get_order(size);
#if gcdUSE_NON_PAGED_MEMORY_CACHE
    page = _GetNonPagedMemoryCache(Os, order);

    if (page == gcvNULL)
#endif
    {
        page = alloc_pages(GFP_KERNEL | __GFP_NOWARN, order);
    }

    if (page == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    vaddr           = (gctPOINTER)page_address(page);
    addr            = _CreateKernelVirtualMapping(page, mdl->numPages);
    mdl->dmaHandle  = virt_to_phys(vaddr);
    mdl->kaddr      = vaddr;
    mdl->u.contiguousPages = page;

    /* Cache invalidate. */
    dma_sync_single_for_device(
                gcvNULL,
                page_to_phys(page),
                bytes,
                DMA_FROM_DEVICE);

    while (size > 0)
    {
        SetPageReserved(virt_to_page(vaddr));

        vaddr   += PAGE_SIZE;
        size    -= PAGE_SIZE;
    }
#endif

    if (addr == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    if ((Os->device->baseAddress & 0x80000000) != (mdl->dmaHandle & 0x80000000))
    {
        mdl->dmaHandle = (mdl->dmaHandle & ~0x80000000)
                       | (Os->device->baseAddress & 0x80000000);
    }

    mdl->addr = addr;

    /* Return allocated memory. */
    *Bytes = bytes;
    *Physical = (gctPHYS_ADDR) mdl;

    if (InUserSpace)
    {
        mdlMap = _CreateMdlMap(mdl, _GetProcessID());

        if (mdlMap == gcvNULL)
        {
            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }

        /* Only after mmap this will be valid. */

        /* We need to map this to user space. */
        down_write(&current->mm->mmap_sem);

        mdlMap->vmaAddr = (gctSTRING) do_mmap_pgoff(gcvNULL,
                0L,
                mdl->numPages * PAGE_SIZE,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                0);

        if (IS_ERR(mdlMap->vmaAddr))
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_WARNING, gcvZONE_OS,
                "%s(%d): do_mmap_pgoff error",
                __FUNCTION__, __LINE__
                );

            mdlMap->vmaAddr = gcvNULL;

            up_write(&current->mm->mmap_sem);

            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }

        mdlMap->vma = find_vma(current->mm, (unsigned long)mdlMap->vmaAddr);

        if (mdlMap->vma == gcvNULL)
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_WARNING, gcvZONE_OS,
                "%s(%d): find_vma error",
                __FUNCTION__, __LINE__
                );

            up_write(&current->mm->mmap_sem);

            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
        }

#ifndef NO_DMA_COHERENT
        if (dma_mmap_coherent(gcvNULL,
                mdlMap->vma,
                mdl->addr,
                mdl->dmaHandle,
                mdl->numPages * PAGE_SIZE) < 0)
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_WARNING, gcvZONE_OS,
                "%s(%d): dma_mmap_coherent error",
                __FUNCTION__, __LINE__
                );

            up_write(&current->mm->mmap_sem);

            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
        }
#else
        mdlMap->vma->vm_page_prot = gcmkNONPAGED_MEMROY_PROT(mdlMap->vma->vm_page_prot);
        mdlMap->vma->vm_flags |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_RESERVED;
        mdlMap->vma->vm_pgoff = 0;

        if (remap_pfn_range(mdlMap->vma,
                            mdlMap->vma->vm_start,
                            mdl->dmaHandle >> PAGE_SHIFT,
                            mdl->numPages * PAGE_SIZE,
                            mdlMap->vma->vm_page_prot))
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_WARNING, gcvZONE_OS,
                "%s(%d): remap_pfn_range error",
                __FUNCTION__, __LINE__
                );

            up_write(&current->mm->mmap_sem);

            gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
        }
#endif /* NO_DMA_COHERENT */

        up_write(&current->mm->mmap_sem);

        *Logical = mdlMap->vmaAddr;
    }
    else
    {
        *Logical = (gctPOINTER)mdl->addr;
    }

    /*
     * Add this to a global list.
     * Will be used by get physical address
     * and mapuser pointer functions.
     */

    if (!Os->mdlHead)
    {
        /* Initialize the queue. */
        Os->mdlHead = Os->mdlTail = mdl;
    }
    else
    {
        /* Add to the tail. */
        mdl->prev = Os->mdlTail;
        Os->mdlTail->next = mdl;
        Os->mdlTail = mdl;
    }

    MEMORY_UNLOCK(Os);

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu *Physical=0x%X *Logical=0x%X",
                   *Bytes, *Physical, *Logical);
    return gcvSTATUS_OK;

OnError:
    if (mdlMap != gcvNULL)
    {
        /* Free LINUX_MDL_MAP. */
        gcmkVERIFY_OK(_DestroyMdlMap(mdl, mdlMap));
    }

    if (mdl != gcvNULL)
    {
        /* Free LINUX_MDL. */
        gcmkVERIFY_OK(_DestroyMdl(mdl));
    }

    if (locked)
    {
        /* Unlock memory. */
        MEMORY_UNLOCK(Os);
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_FreeNonPagedMemory
**
**  Free previously allocated and mapped pages from non-paged memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIZE_T Bytes
**          Number of bytes allocated.
**
**      gctPHYS_ADDR Physical
**          Physical address of the allocated memory.
**
**      gctPOINTER Logical
**          Logical address of the allocated memory.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS gckOS_FreeNonPagedMemory(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical
    )
{
    PLINUX_MDL mdl;
    PLINUX_MDL_MAP mdlMap;
    struct task_struct * task;
#ifdef NO_DMA_COHERENT
    unsigned size;
    gctPOINTER vaddr;
#endif /* NO_DMA_COHERENT */

    gcmkHEADER_ARG("Os=0x%X Bytes=%lu Physical=0x%X Logical=0x%X",
                   Os, Bytes, Physical, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Physical != 0);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    /* Convert physical address into a pointer to a MDL. */
    mdl = (PLINUX_MDL) Physical;

    MEMORY_LOCK(Os);

#ifndef NO_DMA_COHERENT
#if gcdUSE_NON_PAGED_MEMORY_CACHE
    if (!_AddNonPagedMemoryCache(Os,
                                 mdl->numPages * PAGE_SIZE,
                                 mdl->addr,
                                 mdl->dmaHandle))
#endif
    {
        dma_free_coherent(gcvNULL,
                mdl->numPages * PAGE_SIZE,
                mdl->addr,
                mdl->dmaHandle);
    }
#else
    size    = mdl->numPages * PAGE_SIZE;
    vaddr   = mdl->kaddr;

    while (size > 0)
    {
        ClearPageReserved(virt_to_page(vaddr));

        vaddr   += PAGE_SIZE;
        size    -= PAGE_SIZE;
    }

#if gcdUSE_NON_PAGED_MEMORY_CACHE
    if (!_AddNonPagedMemoryCache(Os,
                                 get_order(mdl->numPages * PAGE_SIZE),
                                 virt_to_page(mdl->kaddr)))
#endif
    {
        free_pages((unsigned long)mdl->kaddr, get_order(mdl->numPages * PAGE_SIZE));
    }

    _DestoryKernelVirtualMapping(mdl->addr);
#endif /* NO_DMA_COHERENT */

    mdlMap = mdl->maps;

    while (mdlMap != gcvNULL)
    {
        if (mdlMap->vmaAddr != gcvNULL)
        {
            /* Get the current pointer for the task with stored pid. */
            task = FIND_TASK_BY_PID(mdlMap->pid);

            if (task != gcvNULL && task->mm != gcvNULL)
            {
                down_write(&task->mm->mmap_sem);

                if (do_munmap(task->mm,
                              (unsigned long)mdlMap->vmaAddr,
                              mdl->numPages * PAGE_SIZE) < 0)
                {
                    gcmkTRACE_ZONE(
                        gcvLEVEL_WARNING, gcvZONE_OS,
                        "%s(%d): do_munmap failed",
                        __FUNCTION__, __LINE__
                        );
                }

                up_write(&task->mm->mmap_sem);
            }

            mdlMap->vmaAddr = gcvNULL;
        }

        mdlMap = mdlMap->next;
    }

    /* Remove the node from global list.. */
    if (mdl == Os->mdlHead)
    {
        if ((Os->mdlHead = mdl->next) == gcvNULL)
        {
            Os->mdlTail = gcvNULL;
        }
    }
    else
    {
        mdl->prev->next = mdl->next;
        if (mdl == Os->mdlTail)
        {
            Os->mdlTail = mdl->prev;
        }
        else
        {
            mdl->next->prev = mdl->prev;
        }
    }

    MEMORY_UNLOCK(Os);

    gcmkVERIFY_OK(_DestroyMdl(mdl));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_ReadRegister
**
**  Read data from a register.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 Address
**          Address of register.
**
**  OUTPUT:
**
**      gctUINT32 * Data
**          Pointer to a variable that receives the data read from the register.
*/
gceSTATUS
gckOS_ReadRegister(
    IN gckOS Os,
    IN gctUINT32 Address,
    OUT gctUINT32 * Data
    )
{
    return gckOS_ReadRegisterEx(Os, gcvCORE_MAJOR, Address, Data);
}

gceSTATUS
gckOS_ReadRegisterEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctUINT32 Address,
    OUT gctUINT32 * Data
    )
{
    gcmkHEADER_ARG("Os=0x%X Core=%d Address=0x%X", Os, Core, Address);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Data != gcvNULL);

    *Data = readl((gctUINT8 *)Os->device->registerBases[Core] + Address);

    /* Success. */
    gcmkFOOTER_ARG("*Data=0x%08x", *Data);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_WriteRegister
**
**  Write data to a register.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 Address
**          Address of register.
**
**      gctUINT32 Data
**          Data for register.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_WriteRegister(
    IN gckOS Os,
    IN gctUINT32 Address,
    IN gctUINT32 Data
    )
{
    return gckOS_WriteRegisterEx(Os, gcvCORE_MAJOR, Address, Data);
}

gceSTATUS
gckOS_WriteRegisterEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctUINT32 Address,
    IN gctUINT32 Data
    )
{
    gcmkHEADER_ARG("Os=0x%X Core=%d Address=0x%X Data=0x%08x", Os, Core, Address, Data);

    writel(Data, (gctUINT8 *)Os->device->registerBases[Core] + Address);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_GetPageSize
**
**  Get the system's page size.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**  OUTPUT:
**
**      gctSIZE_T * PageSize
**          Pointer to a variable that will receive the system's page size.
*/
gceSTATUS gckOS_GetPageSize(
    IN gckOS Os,
    OUT gctSIZE_T * PageSize
    )
{
    gcmkHEADER_ARG("Os=0x%X", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(PageSize != gcvNULL);

    /* Return the page size. */
    *PageSize = (gctSIZE_T) PAGE_SIZE;

    /* Success. */
    gcmkFOOTER_ARG("*PageSize", *PageSize);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_GetPhysicalAddress
**
**  Get the physical system address of a corresponding virtual address.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Logical
**          Logical address.
**
**  OUTPUT:
**
**      gctUINT32 * Address
**          Poinetr to a variable that receives the 32-bit physical adress.
*/
gceSTATUS
gckOS_GetPhysicalAddress(
    IN gckOS Os,
    IN gctPOINTER Logical,
    OUT gctUINT32 * Address
    )
{
    gceSTATUS status;
    gctUINT32 processID;

    gcmkHEADER_ARG("Os=0x%X Logical=0x%X", Os, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    /* Get current process ID. */
    processID = _GetProcessID();

    /* Route through other function. */
    gcmkONERROR(
        gckOS_GetPhysicalAddressProcess(Os, Logical, processID, Address));

    /* Success. */
    gcmkFOOTER_ARG("*Address=0x%08x", *Address);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if gcdSECURE_USER
static gceSTATUS
gckOS_AddMapping(
    IN gckOS Os,
    IN gctUINT32 Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    gceSTATUS status;
    gcsUSER_MAPPING_PTR map;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Logical=0x%X Bytes=%lu",
                   Os, Physical, Logical, Bytes);

    gcmkONERROR(gckOS_Allocate(Os,
                               gcmSIZEOF(gcsUSER_MAPPING),
                               (gctPOINTER *) &map));

    map->next     = Os->userMap;
    map->physical = Physical - Os->device->baseAddress;
    map->logical  = Logical;
    map->bytes    = Bytes;
    map->start    = (gctINT8_PTR) Logical;
    map->end      = map->start + Bytes;

    Os->userMap = map;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

static gceSTATUS
gckOS_RemoveMapping(
    IN gckOS Os,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    gceSTATUS status;
    gcsUSER_MAPPING_PTR map, prev;

    gcmkHEADER_ARG("Os=0x%X Logical=0x%X Bytes=%lu", Os, Logical, Bytes);

    for (map = Os->userMap, prev = gcvNULL; map != gcvNULL; map = map->next)
    {
        if ((map->logical == Logical)
        &&  (map->bytes   == Bytes)
        )
        {
            break;
        }

        prev = map;
    }

    if (map == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_INVALID_ADDRESS);
    }

    if (prev == gcvNULL)
    {
        Os->userMap = map->next;
    }
    else
    {
        prev->next = map->next;
    }

    gcmkONERROR(gcmkOS_SAFE_FREE(Os, map));

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}
#endif

static gceSTATUS
_ConvertLogical2Physical(
    IN gckOS Os,
    IN gctPOINTER Logical,
    IN gctUINT32 ProcessID,
    IN PLINUX_MDL Mdl,
    OUT gctUINT32_PTR Physical
    )
{
    gctINT8_PTR base, vBase;
    gctUINT32 offset;
    PLINUX_MDL_MAP map;
    gcsUSER_MAPPING_PTR userMap;

    base = (Mdl == gcvNULL) ? gcvNULL : (gctINT8_PTR) Mdl->addr;

    /* Check for the logical address match. */
    if ((base != gcvNULL)
    &&  ((gctINT8_PTR) Logical >= base)
    &&  ((gctINT8_PTR) Logical <  base + Mdl->numPages * PAGE_SIZE)
    )
    {
        offset = (gctINT8_PTR) Logical - base;

        if (Mdl->dmaHandle != 0)
        {
            /* The memory was from coherent area. */
            *Physical = (gctUINT32) Mdl->dmaHandle + offset;
        }
        else if (Mdl->pagedMem && !Mdl->contiguous)
        {
            /* paged memory is not mapped to kernel space. */
            return gcvSTATUS_INVALID_ADDRESS;
        }
        else
        {
            *Physical = gcmPTR2INT(virt_to_phys(base)) + offset;
        }

        return gcvSTATUS_OK;
    }

    /* Walk user maps. */
    for (userMap = Os->userMap; userMap != gcvNULL; userMap = userMap->next)
    {
        if (((gctINT8_PTR) Logical >= userMap->start)
        &&  ((gctINT8_PTR) Logical <  userMap->end)
        )
        {
            *Physical = userMap->physical
                      + (gctUINT32) ((gctINT8_PTR) Logical - userMap->start);

            return gcvSTATUS_OK;
        }
    }

    if (ProcessID != Os->kernelProcessID)
    {
        map   = FindMdlMap(Mdl, (gctINT) ProcessID);
        vBase = (map == gcvNULL) ? gcvNULL : (gctINT8_PTR) map->vmaAddr;

        /* Is the given address within that range. */
        if ((vBase != gcvNULL)
        &&  ((gctINT8_PTR) Logical >= vBase)
        &&  ((gctINT8_PTR) Logical <  vBase + Mdl->numPages * PAGE_SIZE)
        )
        {
            offset = (gctINT8_PTR) Logical - vBase;

            if (Mdl->dmaHandle != 0)
            {
                /* The memory was from coherent area. */
                *Physical = (gctUINT32) Mdl->dmaHandle + offset;
            }
            else if (Mdl->pagedMem && !Mdl->contiguous)
            {
                *Physical = _NonContiguousToPhys(Mdl->u.nonContiguousPages, offset/PAGE_SIZE);
            }
            else
            {
                *Physical = page_to_phys(Mdl->u.contiguousPages) + offset;
            }

            return gcvSTATUS_OK;
        }
    }

    /* Address not yet found. */
    return gcvSTATUS_INVALID_ADDRESS;
}

/*******************************************************************************
**
**  gckOS_GetPhysicalAddressProcess
**
**  Get the physical system address of a corresponding virtual address for a
**  given process.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to gckOS object.
**
**      gctPOINTER Logical
**          Logical address.
**
**      gctUINT32 ProcessID
**          Process ID.
**
**  OUTPUT:
**
**      gctUINT32 * Address
**          Poinetr to a variable that receives the 32-bit physical adress.
*/
gceSTATUS
gckOS_GetPhysicalAddressProcess(
    IN gckOS Os,
    IN gctPOINTER Logical,
    IN gctUINT32 ProcessID,
    OUT gctUINT32 * Address
    )
{
    PLINUX_MDL mdl;
    gctINT8_PTR base;
    gceSTATUS status = gcvSTATUS_INVALID_ADDRESS;

    gcmkHEADER_ARG("Os=0x%X Logical=0x%X ProcessID=%d", Os, Logical, ProcessID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    MEMORY_LOCK(Os);

    /* First try the contiguous memory pool. */
    if (Os->device->contiguousMapped)
    {
        base = (gctINT8_PTR) Os->device->contiguousBase;

        if (((gctINT8_PTR) Logical >= base)
        &&  ((gctINT8_PTR) Logical <  base + Os->device->contiguousSize)
        )
        {
            /* Convert logical address into physical. */
            *Address = Os->device->contiguousVidMem->baseAddress
                     + (gctINT8_PTR) Logical - base;
            status   = gcvSTATUS_OK;
        }
    }
    else
    {
        /* Try the contiguous memory pool. */
        mdl = (PLINUX_MDL) Os->device->contiguousPhysical;
        status = _ConvertLogical2Physical(Os,
                                          Logical,
                                          ProcessID,
                                          mdl,
                                          Address);
    }

    if (gcmIS_ERROR(status))
    {
        /* Walk all MDLs. */
        for (mdl = Os->mdlHead; mdl != gcvNULL; mdl = mdl->next)
        {
            /* Try this MDL. */
            status = _ConvertLogical2Physical(Os,
                                              Logical,
                                              ProcessID,
                                              mdl,
                                              Address);
            if (gcmIS_SUCCESS(status))
            {
                break;
            }
        }
    }

    MEMORY_UNLOCK(Os);

    gcmkONERROR(status);

    if (Os->device->baseAddress != 0)
    {
        /* Subtract base address to get a GPU physical address. */
        gcmkASSERT(*Address >= Os->device->baseAddress);
        *Address -= Os->device->baseAddress;
    }

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
**  gckOS_MapPhysical
**
**  Map a physical address into kernel space.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 Physical
**          Physical address of the memory to map.
**
**      gctSIZE_T Bytes
**          Number of bytes to map.
**
**  OUTPUT:
**
**      gctPOINTER * Logical
**          Pointer to a variable that receives the base address of the mapped
**          memory.
*/
gceSTATUS
gckOS_MapPhysical(
    IN gckOS Os,
    IN gctUINT32 Physical,
    IN gctSIZE_T Bytes,
    OUT gctPOINTER * Logical
    )
{
    gctPOINTER logical;
    PLINUX_MDL mdl;
    gctUINT32 physical;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%lu", Os, Physical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    MEMORY_LOCK(Os);

    /* Compute true physical address (before subtraction of the baseAddress). */
    physical = Physical + Os->device->baseAddress;

    /* Go through our mapping to see if we know this physical address already. */
    mdl = Os->mdlHead;

    while (mdl != gcvNULL)
    {
        if (mdl->dmaHandle != 0)
        {
            if ((physical >= mdl->dmaHandle)
            &&  (physical < mdl->dmaHandle + mdl->numPages * PAGE_SIZE)
            )
            {
                *Logical = mdl->addr + (physical - mdl->dmaHandle);
                break;
            }
        }

        mdl = mdl->next;
    }

    if (mdl == gcvNULL)
    {
        /* Map memory as cached memory. */
        request_mem_region(physical, Bytes, "MapRegion");
        logical = (gctPOINTER) ioremap_nocache(physical, Bytes);

        if (logical == gcvNULL)
        {
            gcmkTRACE_ZONE(
                gcvLEVEL_INFO, gcvZONE_OS,
                "%s(%d): Failed to ioremap",
                __FUNCTION__, __LINE__
                );

            MEMORY_UNLOCK(Os);

            /* Out of resources. */
            gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_RESOURCES);
            return gcvSTATUS_OUT_OF_RESOURCES;
        }

        /* Return pointer to mapped memory. */
        *Logical = logical;
    }

    MEMORY_UNLOCK(Os);

    /* Success. */
    gcmkFOOTER_ARG("*Logical=0x%X", *Logical);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_UnmapPhysical
**
**  Unmap a previously mapped memory region from kernel memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Logical
**          Pointer to the base address of the memory to unmap.
**
**      gctSIZE_T Bytes
**          Number of bytes to unmap.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UnmapPhysical(
    IN gckOS Os,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    PLINUX_MDL  mdl;

    gcmkHEADER_ARG("Os=0x%X Logical=0x%X Bytes=%lu", Os, Logical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    MEMORY_LOCK(Os);

    mdl = Os->mdlHead;

    while (mdl != gcvNULL)
    {
        if (mdl->addr != gcvNULL)
        {
            if (Logical >= (gctPOINTER)mdl->addr
                    && Logical < (gctPOINTER)((gctSTRING)mdl->addr + mdl->numPages * PAGE_SIZE))
            {
                break;
            }
        }

        mdl = mdl->next;
    }

    if (mdl == gcvNULL)
    {
        /* Unmap the memory. */
        iounmap(Logical);
    }

    MEMORY_UNLOCK(Os);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_CreateMutex
**
**  Create a new mutex.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**  OUTPUT:
**
**      gctPOINTER * Mutex
**          Pointer to a variable that will hold a pointer to the mutex.
*/
gceSTATUS
gckOS_CreateMutex(
    IN gckOS Os,
    OUT gctPOINTER * Mutex
    )
{
    gcmkHEADER_ARG("Os=0x%X", Os);

    /* Validate the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Mutex != gcvNULL);

    /* Allocate a FAST_MUTEX structure. */
    *Mutex = (gctPOINTER)kmalloc(sizeof(struct semaphore), GFP_KERNEL | __GFP_NOWARN);

    if (*Mutex == gcvNULL)
    {
        gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_MEMORY);
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    /* Initialize the semaphore.. Come up in unlocked state. */
    sema_init(*Mutex, 1);

    /* Return status. */
    gcmkFOOTER_ARG("*Mutex=0x%X", *Mutex);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_DeleteMutex
**
**  Delete a mutex.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Mutex
**          Pointer to the mute to be deleted.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_DeleteMutex(
    IN gckOS Os,
    IN gctPOINTER Mutex
    )
{
    gcmkHEADER_ARG("Os=0x%X Mutex=0x%X", Os, Mutex);

    /* Validate the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Mutex != gcvNULL);

    /* Delete the fast mutex. */
    kfree(Mutex);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AcquireMutex
**
**  Acquire a mutex.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Mutex
**          Pointer to the mutex to be acquired.
**
**      gctUINT32 Timeout
**          Timeout value specified in milliseconds.
**          Specify the value of gcvINFINITE to keep the thread suspended
**          until the mutex has been acquired.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AcquireMutex(
    IN gckOS Os,
    IN gctPOINTER Mutex,
    IN gctUINT32 Timeout
    )
{
#if gcdDETECT_TIMEOUT
    gctUINT32 timeout;
#endif

    gcmkHEADER_ARG("Os=0x%X Mutex=0x%0x Timeout=%u", Os, Mutex, Timeout);

    /* Validate the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Mutex != gcvNULL);

#if gcdDETECT_TIMEOUT
    timeout = 0;

    for (;;)
    {
        /* Try to acquire the mutex. */
        if (!down_trylock((struct semaphore *) Mutex))
        {
            /* Success. */
            gcmkFOOTER_NO();
            return gcvSTATUS_OK;
        }

        /* Advance the timeout. */
        timeout += 1;

        if (Timeout == gcvINFINITE)
        {
            if (timeout == gcdINFINITE_TIMEOUT)
            {
                gctUINT32 dmaAddress1, dmaAddress2;
                gctUINT32 dmaState1, dmaState2;

                dmaState1   = dmaState2   =
                dmaAddress1 = dmaAddress2 = 0;

                /* Verify whether DMA is running. */
                gcmkVERIFY_OK(_VerifyDMA(
                    Os, &dmaAddress1, &dmaAddress2, &dmaState1, &dmaState2
                    ));

#if gcdDETECT_DMA_ADDRESS
                /* Dump only if DMA appears stuck. */
                if (
                    (dmaAddress1 == dmaAddress2)
#if gcdDETECT_DMA_STATE
                 && (dmaState1   == dmaState2)
#      endif
                )
#   endif
                {
                    gcmkVERIFY_OK(_DumpGPUState(Os, gcvCORE_MAJOR));

                    gcmkPRINT(
                        "%s(%d): mutex 0x%X; forced message flush.",
                        __FUNCTION__, __LINE__, Mutex
                        );

                    /* Flush the debug cache. */
                    gcmkDEBUGFLUSH(dmaAddress2);
                }

                timeout = 0;
            }
        }
        else
        {
            /* Timedout? */
            if (timeout >= Timeout)
            {
                break;
            }
        }

        /* Wait for 1 millisecond. */
        gcmkVERIFY_OK(gckOS_Delay(Os, 1));
    }
#else
    if (Timeout == gcvINFINITE)
    {
        down((struct semaphore *) Mutex);

        /* Success. */
        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    for (;;)
    {
        /* Try to acquire the mutex. */
        if (!down_trylock((struct semaphore *) Mutex))
        {
            /* Success. */
            gcmkFOOTER_NO();
            return gcvSTATUS_OK;
        }

        if (Timeout-- == 0)
        {
            break;
        }

        /* Wait for 1 millisecond. */
        gcmkVERIFY_OK(gckOS_Delay(Os, 1));
    }
#endif

    /* Timeout. */
    gcmkFOOTER_ARG("status=%d", gcvSTATUS_TIMEOUT);
    return gcvSTATUS_TIMEOUT;
}

/*******************************************************************************
**
**  gckOS_ReleaseMutex
**
**  Release an acquired mutex.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Mutex
**          Pointer to the mutex to be released.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_ReleaseMutex(
    IN gckOS Os,
    IN gctPOINTER Mutex
    )
{
    gcmkHEADER_ARG("Os=0x%X Mutex=0x%0x", Os, Mutex);

    /* Validate the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Mutex != gcvNULL);

    /* Release the fast mutex. */
    up((struct semaphore *) Mutex);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AtomicExchange
**
**  Atomically exchange a pair of 32-bit values.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      IN OUT gctINT32_PTR Target
**          Pointer to the 32-bit value to exchange.
**
**      IN gctINT32 NewValue
**          Specifies a new value for the 32-bit value pointed to by Target.
**
**      OUT gctINT32_PTR OldValue
**          The old value of the 32-bit value pointed to by Target.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomicExchange(
    IN gckOS Os,
    IN OUT gctUINT32_PTR Target,
    IN gctUINT32 NewValue,
    OUT gctUINT32_PTR OldValue
    )
{
    gcmkHEADER_ARG("Os=0x%X Target=0x%X NewValue=%u", Os, Target, NewValue);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    /* Exchange the pair of 32-bit values. */
    *OldValue = (gctUINT32) atomic_xchg((atomic_t *) Target, (int) NewValue);

    /* Success. */
    gcmkFOOTER_ARG("*OldValue=%u", *OldValue);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AtomicExchangePtr
**
**  Atomically exchange a pair of pointers.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      IN OUT gctPOINTER * Target
**          Pointer to the 32-bit value to exchange.
**
**      IN gctPOINTER NewValue
**          Specifies a new value for the pointer pointed to by Target.
**
**      OUT gctPOINTER * OldValue
**          The old value of the pointer pointed to by Target.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomicExchangePtr(
    IN gckOS Os,
    IN OUT gctPOINTER * Target,
    IN gctPOINTER NewValue,
    OUT gctPOINTER * OldValue
    )
{
    gcmkHEADER_ARG("Os=0x%X Target=0x%X NewValue=0x%X", Os, Target, NewValue);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    /* Exchange the pair of pointers. */
    *OldValue = (gctPOINTER) atomic_xchg((atomic_t *) Target, (int) NewValue);

    /* Success. */
    gcmkFOOTER_ARG("*OldValue=0x%X", *OldValue);
    return gcvSTATUS_OK;
}

#if gcdSMP
/*******************************************************************************
**
**  gckOS_AtomicSetMask
**
**  Atomically set mask to Atom
**
**  INPUT:
**      IN OUT gctPOINTER Atom
**          Pointer to the atom to set.
**
**      IN gctUINT32 Mask
**          Mask to set.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomSetMask(
    IN gctPOINTER Atom,
    IN gctUINT32 Mask
    )
{
    gctUINT32 oval, nval;

    gcmkHEADER_ARG("Atom=0x%0x", Atom);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    do
    {
        oval = atomic_read((atomic_t *) Atom);
        nval = oval | Mask;
    } while (atomic_cmpxchg((atomic_t *) Atom, oval, nval) != oval);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AtomClearMask
**
**  Atomically clear mask from Atom
**
**  INPUT:
**      IN OUT gctPOINTER Atom
**          Pointer to the atom to clear.
**
**      IN gctUINT32 Mask
**          Mask to clear.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomClearMask(
    IN gctPOINTER Atom,
    IN gctUINT32 Mask
    )
{
    gctUINT32 oval, nval;

    gcmkHEADER_ARG("Atom=0x%0x", Atom);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    do
    {
        oval = atomic_read((atomic_t *) Atom);
        nval = oval & ~Mask;
    } while (atomic_cmpxchg((atomic_t *) Atom, oval, nval) != oval);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}
#endif

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
**      gctPOINTER * Atom
**          Pointer to a variable receiving the constructed atom.
*/
gceSTATUS
gckOS_AtomConstruct(
    IN gckOS Os,
    OUT gctPOINTER * Atom
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    /* Allocate the atom. */
    gcmkONERROR(gckOS_Allocate(Os, gcmSIZEOF(atomic_t), Atom));

    /* Initialize the atom. */
    atomic_set((atomic_t *) *Atom, 0);

    /* Success. */
    gcmkFOOTER_ARG("*Atom=0x%X", *Atom);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

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
**      gctPOINTER Atom
**          Pointer to the atom to destroy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomDestroy(
    IN gckOS Os,
    OUT gctPOINTER Atom
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Atom=0x%0x", Os, Atom);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    /* Free the atom. */
    gcmkONERROR(gcmkOS_SAFE_FREE(Os, Atom));

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
**  gckOS_AtomGet
**
**  Get the 32-bit value protected by an atom.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gctPOINTER Atom
**          Pointer to the atom.
**
**  OUTPUT:
**
**      gctINT32_PTR Value
**          Pointer to a variable the receives the value of the atom.
*/
gceSTATUS
gckOS_AtomGet(
    IN gckOS Os,
    IN gctPOINTER Atom,
    OUT gctINT32_PTR Value
    )
{
    gcmkHEADER_ARG("Os=0x%X Atom=0x%0x", Os, Atom);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    /* Return the current value of atom. */
    *Value = atomic_read((atomic_t *) Atom);

    /* Success. */
    gcmkFOOTER_ARG("*Value=%d", *Value);
    return gcvSTATUS_OK;
}

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
**      gctPOINTER Atom
**          Pointer to the atom.
**
**      gctINT32 Value
**          The value of the atom.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AtomSet(
    IN gckOS Os,
    IN gctPOINTER Atom,
    IN gctINT32 Value
    )
{
    gcmkHEADER_ARG("Os=0x%X Atom=0x%0x Value=%d", Os, Atom);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    /* Set the current value of atom. */
    atomic_set((atomic_t *) Atom, Value);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

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
**      gctPOINTER Atom
**          Pointer to the atom.
**
**  OUTPUT:
**
**      gctINT32_PTR Value
**          Pointer to a variable that receives the original value of the atom.
*/
gceSTATUS
gckOS_AtomIncrement(
    IN gckOS Os,
    IN gctPOINTER Atom,
    OUT gctINT32_PTR Value
    )
{
    gcmkHEADER_ARG("Os=0x%X Atom=0x%0x", Os, Atom);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    /* Increment the atom. */
    *Value = atomic_inc_return((atomic_t *) Atom) - 1;

    /* Success. */
    gcmkFOOTER_ARG("*Value=%d", *Value);
    return gcvSTATUS_OK;
}

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
**      gctPOINTER Atom
**          Pointer to the atom.
**
**  OUTPUT:
**
**      gctINT32_PTR Value
**          Pointer to a variable that receives the original value of the atom.
*/
gceSTATUS
gckOS_AtomDecrement(
    IN gckOS Os,
    IN gctPOINTER Atom,
    OUT gctINT32_PTR Value
    )
{
    gcmkHEADER_ARG("Os=0x%X Atom=0x%0x", Os, Atom);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Atom != gcvNULL);

    /* Decrement the atom. */
    *Value = atomic_dec_return((atomic_t *) Atom) + 1;

    /* Success. */
    gcmkFOOTER_ARG("*Value=%d", *Value);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_Delay
**
**  Delay execution of the current thread for a number of milliseconds.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 Delay
**          Delay to sleep, specified in milliseconds.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_Delay(
    IN gckOS Os,
    IN gctUINT32 Delay
    )
{
    struct timeval now;
    unsigned long jiffies;

    gcmkHEADER_ARG("Os=0x%X Delay=%u", Os, Delay);

    if (Delay > 0)
    {
        /* Convert milliseconds into seconds and microseconds. */
        now.tv_sec  = Delay / 1000;
        now.tv_usec = (Delay % 1000) * 1000;

        /* Convert timeval to jiffies. */
        jiffies = timeval_to_jiffies(&now);

        /* Schedule timeout. */
        schedule_timeout_interruptible(jiffies);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_GetTicks
**
**  Get the number of milliseconds since the system started.
**
**  INPUT:
**
**  OUTPUT:
**
**      gctUINT32_PTR Time
**          Pointer to a variable to get time.
**
*/
gceSTATUS
gckOS_GetTicks(
    OUT gctUINT32_PTR Time
    )
{
     gcmkHEADER();

    *Time = jiffies * 1000 / HZ;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_TicksAfter
**
**  Compare time values got from gckOS_GetTicks.
**
**  INPUT:
**      gctUINT32 Time1
**          First time value to be compared.
**
**      gctUINT32 Time2
**          Second time value to be compared.
**
**  OUTPUT:
**
**      gctBOOL_PTR IsAfter
**          Pointer to a variable to result.
**
*/
gceSTATUS
gckOS_TicksAfter(
    IN gctUINT32 Time1,
    IN gctUINT32 Time2,
    OUT gctBOOL_PTR IsAfter
    )
{
    gcmkHEADER();

    *IsAfter = time_after((unsigned long)Time1, (unsigned long)Time2);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_GetTime
**
**  Get the number of microseconds since the system started.
**
**  INPUT:
**
**  OUTPUT:
**
**      gctUINT64_PTR Time
**          Pointer to a variable to get time.
**
*/
gceSTATUS
gckOS_GetTime(
    OUT gctUINT64_PTR Time
    )
{
    gcmkHEADER();

    *Time = 0;

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_MemoryBarrier
**
**  Make sure the CPU has executed everything up to this point and the data got
**  written to the specified pointer.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Address
**          Address of memory that needs to be barriered.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_MemoryBarrier(
    IN gckOS Os,
    IN gctPOINTER Address
    )
{
    gcmkHEADER_ARG("Os=0x%X Address=0x%X", Os, Address);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

#if gcdNONPAGED_MEMORY_BUFFERABLE \
    && defined (CONFIG_ARM) \
    && (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34))
    /* drain write buffer */
    dsb();

    /* drain outer cache's write buffer? */
#else
    mb();
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_AllocatePagedMemory
**
**  Allocate memory from the paged pool.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIZE_T Bytes
**          Number of bytes to allocate.
**
**  OUTPUT:
**
**      gctPHYS_ADDR * Physical
**          Pointer to a variable that receives the physical address of the
**          memory allocation.
*/
gceSTATUS
gckOS_AllocatePagedMemory(
    IN gckOS Os,
    IN gctSIZE_T Bytes,
    OUT gctPHYS_ADDR * Physical
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Bytes=%lu", Os, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);

    /* Allocate the memory. */
    gcmkONERROR(gckOS_AllocatePagedMemoryEx(Os, gcvFALSE, Bytes, Physical));

    /* Success. */
    gcmkFOOTER_ARG("*Physical=0x%X", *Physical);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_AllocatePagedMemoryEx
**
**  Allocate memory from the paged pool.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctBOOL Contiguous
**          Need contiguous memory or not.
**
**      gctSIZE_T Bytes
**          Number of bytes to allocate.
**
**  OUTPUT:
**
**      gctPHYS_ADDR * Physical
**          Pointer to a variable that receives the physical address of the
**          memory allocation.
*/
gceSTATUS
gckOS_AllocatePagedMemoryEx(
    IN gckOS Os,
    IN gctBOOL Contiguous,
    IN gctSIZE_T Bytes,
    OUT gctPHYS_ADDR * Physical
    )
{
    gctINT numPages;
    gctINT i;
    PLINUX_MDL mdl = gcvNULL;
    gctSIZE_T bytes;
    gctBOOL locked = gcvFALSE;
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Contiguous=%d Bytes=%lu", Os, Contiguous, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes > 0);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);

    bytes = gcmALIGN(Bytes, PAGE_SIZE);

    numPages = GetPageCount(bytes, 0);

    MEMORY_LOCK(Os);
    locked = gcvTRUE;

    mdl = _CreateMdl(_GetProcessID());
    if (mdl == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    if (Contiguous)
    {
        /* Get contiguous pages, and suppress warning (stack dump) from kernel when
           we run out of memory. */
        mdl->u.contiguousPages =
            alloc_pages(GFP_KERNEL | __GFP_NOWARN, GetOrder(numPages));

        if (mdl->u.contiguousPages == gcvNULL)
        {
            mdl->u.contiguousPages =
                alloc_pages(GFP_KERNEL | __GFP_HIGHMEM | __GFP_NOWARN, GetOrder(numPages));
        }
    }
    else
    {
        mdl->u.nonContiguousPages = _NonContiguousAlloc(numPages);
    }

    if (mdl->u.contiguousPages == gcvNULL && mdl->u.nonContiguousPages == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    mdl->dmaHandle  = 0;
    mdl->addr       = 0;
    mdl->numPages   = numPages;
    mdl->pagedMem   = 1;
    mdl->contiguous = Contiguous;

    for (i = 0; i < mdl->numPages; i++)
    {
        struct page *page;

        if (mdl->contiguous)
        {
            page = nth_page(mdl->u.contiguousPages, i);
        }
        else
        {
            page = _NonContiguousToPage(mdl->u.nonContiguousPages, i);
        }

        SetPageReserved(page);

        if (!PageHighMem(page) && page_to_phys(page))
        {
            gcmkVERIFY_OK(
                gckOS_CacheFlush(Os, _GetProcessID(), gcvNULL,
                                 (gctPOINTER)page_to_phys(page),
                                 page_address(page),
                                 PAGE_SIZE));
        }
    }

    /* Return physical address. */
    *Physical = (gctPHYS_ADDR) mdl;

    /*
     * Add this to a global list.
     * Will be used by get physical address
     * and mapuser pointer functions.
     */
    if (!Os->mdlHead)
    {
        /* Initialize the queue. */
        Os->mdlHead = Os->mdlTail = mdl;
    }
    else
    {
        /* Add to tail. */
        mdl->prev           = Os->mdlTail;
        Os->mdlTail->next   = mdl;
        Os->mdlTail         = mdl;
    }

    MEMORY_UNLOCK(Os);

    /* Success. */
    gcmkFOOTER_ARG("*Physical=0x%X", *Physical);
    return gcvSTATUS_OK;

OnError:
    if (mdl != gcvNULL)
    {
        /* Free the memory. */
        _DestroyMdl(mdl);
    }

    if (locked)
    {
        /* Unlock the memory. */
        MEMORY_UNLOCK(Os);
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_FreePagedMemory
**
**  Free memory allocated from the paged pool.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Physical address of the allocation.
**
**      gctSIZE_T Bytes
**          Number of bytes of the allocation.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_FreePagedMemory(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes
    )
{
    PLINUX_MDL mdl = (PLINUX_MDL) Physical;
    gctINT i;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%lu", Os, Physical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    /*addr = mdl->addr;*/

    MEMORY_LOCK(Os);

    for (i = 0; i < mdl->numPages; i++)
    {
        if (mdl->contiguous)
        {
            ClearPageReserved(nth_page(mdl->u.contiguousPages, i));
        }
        else
        {
            ClearPageReserved(_NonContiguousToPage(mdl->u.nonContiguousPages, i));
        }
    }

    if (mdl->contiguous)
    {
        __free_pages(mdl->u.contiguousPages, GetOrder(mdl->numPages));
    }
    else
    {
        _NonContiguousFree(mdl->u.nonContiguousPages, mdl->numPages);
    }

    /* Remove the node from global list. */
    if (mdl == Os->mdlHead)
    {
        if ((Os->mdlHead = mdl->next) == gcvNULL)
        {
            Os->mdlTail = gcvNULL;
        }
    }
    else
    {
        mdl->prev->next = mdl->next;

        if (mdl == Os->mdlTail)
        {
            Os->mdlTail = mdl->prev;
        }
        else
        {
            mdl->next->prev = mdl->prev;
        }
    }

    MEMORY_UNLOCK(Os);

    /* Free the structure... */
    gcmkVERIFY_OK(_DestroyMdl(mdl));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_LockPages
**
**  Lock memory allocated from the paged pool.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Physical address of the allocation.
**
**      gctSIZE_T Bytes
**          Number of bytes of the allocation.
**
**      gctBOOL Cacheable
**          Cache mode of mapping.
**
**  OUTPUT:
**
**      gctPOINTER * Logical
**          Pointer to a variable that receives the address of the mapped
**          memory.
**
**      gctSIZE_T * PageCount
**          Pointer to a variable that receives the number of pages required for
**          the page table according to the GPU page size.
*/
gceSTATUS
gckOS_LockPages(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctBOOL Cacheable,
    OUT gctPOINTER * Logical,
    OUT gctSIZE_T * PageCount
    )
{
    PLINUX_MDL      mdl;
    PLINUX_MDL_MAP  mdlMap;
    gctSTRING       addr;
    unsigned long   start;
    unsigned long   pfn;
    gctINT          i;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%lu", Os, Physical, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(PageCount != gcvNULL);

    mdl = (PLINUX_MDL) Physical;

    MEMORY_LOCK(Os);

    mdlMap = FindMdlMap(mdl, _GetProcessID());

    if (mdlMap == gcvNULL)
    {
        mdlMap = _CreateMdlMap(mdl, _GetProcessID());

        if (mdlMap == gcvNULL)
        {
            MEMORY_UNLOCK(Os);

            gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
            return gcvSTATUS_OUT_OF_MEMORY;
        }
    }

    if (mdlMap->vmaAddr == gcvNULL)
    {
        down_write(&current->mm->mmap_sem);

        mdlMap->vmaAddr = (gctSTRING)do_mmap_pgoff(gcvNULL,
                        0L,
                        mdl->numPages * PAGE_SIZE,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED,
                        0);

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_OS,
            "%s(%d): vmaAddr->0x%X for phys_addr->0x%X",
            __FUNCTION__, __LINE__,
            (gctUINT32) mdlMap->vmaAddr,
            (gctUINT32) mdl
            );

        if (IS_ERR(mdlMap->vmaAddr))
        {
            up_write(&current->mm->mmap_sem);

            gcmkTRACE_ZONE(
                gcvLEVEL_INFO, gcvZONE_OS,
                "%s(%d): do_mmap_pgoff error",
                __FUNCTION__, __LINE__
                );

            mdlMap->vmaAddr = gcvNULL;

            MEMORY_UNLOCK(Os);

            gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
            return gcvSTATUS_OUT_OF_MEMORY;
        }

        mdlMap->vma = find_vma(current->mm, (unsigned long)mdlMap->vmaAddr);

        if (mdlMap->vma == gcvNULL)
        {
            up_write(&current->mm->mmap_sem);

            gcmkTRACE_ZONE(
                gcvLEVEL_INFO, gcvZONE_OS,
                "%s(%d): find_vma error",
                __FUNCTION__, __LINE__
                );

            mdlMap->vmaAddr = gcvNULL;

            MEMORY_UNLOCK(Os);

            gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_RESOURCES);
            return gcvSTATUS_OUT_OF_RESOURCES;
        }

        mdlMap->vma->vm_flags |= VM_RESERVED;
#if !gcdPAGED_MEMORY_CACHEABLE
        if (Cacheable == gcvFALSE)
        {
            /* Make this mapping non-cached. */
            mdlMap->vma->vm_page_prot = gcmkPAGED_MEMROY_PROT(mdlMap->vma->vm_page_prot);
        }
#endif
        addr = mdl->addr;

        /* Now map all the vmalloc pages to this user address. */
        if (mdl->contiguous)
        {
            /* map kernel memory to user space.. */
            if (remap_pfn_range(mdlMap->vma,
                                mdlMap->vma->vm_start,
                                page_to_pfn(mdl->u.contiguousPages),
                                mdlMap->vma->vm_end - mdlMap->vma->vm_start,
                                mdlMap->vma->vm_page_prot) < 0)
            {
                up_write(&current->mm->mmap_sem);

                gcmkTRACE_ZONE(
                    gcvLEVEL_INFO, gcvZONE_OS,
                    "%s(%d): unable to mmap ret",
                    __FUNCTION__, __LINE__
                    );

                mdlMap->vmaAddr = gcvNULL;

                MEMORY_UNLOCK(Os);

                gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
                return gcvSTATUS_OUT_OF_MEMORY;
            }
        }
        else
        {
            start = mdlMap->vma->vm_start;

            for (i = 0; i < mdl->numPages; i++)
            {
                pfn = _NonContiguousToPfn(mdl->u.nonContiguousPages, i);

                if (remap_pfn_range(mdlMap->vma,
                                    start,
                                    pfn,
                                    PAGE_SIZE,
                                    mdlMap->vma->vm_page_prot) < 0)
                {
                    up_write(&current->mm->mmap_sem);

                    gcmkTRACE_ZONE(
                        gcvLEVEL_INFO, gcvZONE_OS,
                        "%s(%d): gctPHYS_ADDR->0x%X Logical->0x%X Unable to map addr->0x%X to start->0x%X",
                        __FUNCTION__, __LINE__,
                        (gctUINT32) Physical,
                        (gctUINT32) *Logical,
                        (gctUINT32) addr,
                        (gctUINT32) start
                        );

                    mdlMap->vmaAddr = gcvNULL;

                    MEMORY_UNLOCK(Os);

                    gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
                    return gcvSTATUS_OUT_OF_MEMORY;
                }

                start += PAGE_SIZE;
                addr += PAGE_SIZE;
            }
        }

        up_write(&current->mm->mmap_sem);
    }
    else
    {
        /* mdlMap->vmaAddr != gcvNULL means current process has already locked this node. */
        MEMORY_UNLOCK(Os);

        gcmkFOOTER_ARG("*status=%d, mdlMap->vmaAddr=%x", gcvSTATUS_MEMORY_LOCKED, mdlMap->vmaAddr);
        return gcvSTATUS_MEMORY_LOCKED;
    }

    /* Convert pointer to MDL. */
    *Logical = mdlMap->vmaAddr;

    /* Return the page number according to the GPU page size. */
    gcmkASSERT((PAGE_SIZE % 4096) == 0);
    gcmkASSERT((PAGE_SIZE / 4096) >= 1);

    *PageCount = mdl->numPages * (PAGE_SIZE / 4096);

    MEMORY_UNLOCK(Os);

    /* Success. */
    gcmkFOOTER_ARG("*Logical=0x%X *PageCount=%lu", *Logical, *PageCount);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_MapPages
**
**  Map paged memory into a page table.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Physical address of the allocation.
**
**      gctSIZE_T PageCount
**          Number of pages required for the physical address.
**
**      gctPOINTER PageTable
**          Pointer to the page table to fill in.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_MapPages(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T PageCount,
    IN gctPOINTER PageTable
    )
{
    return gckOS_MapPagesEx(Os,
                            gcvCORE_MAJOR,
                            Physical,
                            PageCount,
                            PageTable);
}

gceSTATUS
gckOS_MapPagesEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T PageCount,
    IN gctPOINTER PageTable
    )
{
    gceSTATUS status = gcvSTATUS_OK;
    PLINUX_MDL  mdl;
    gctUINT32*  table;
    gctUINT32   offset;
#if gcdNONPAGED_MEMORY_CACHEABLE
    gckMMU      mmu;
    PLINUX_MDL  mmuMdl;
    gctUINT32   bytes;
    gctPHYS_ADDR pageTablePhysical;
#endif

    gcmkHEADER_ARG("Os=0x%X Core=%d Physical=0x%X PageCount=%u PageTable=0x%X",
                   Os, Core, Physical, PageCount, PageTable);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(PageCount > 0);
    gcmkVERIFY_ARGUMENT(PageTable != gcvNULL);

    /* Convert pointer to MDL. */
    mdl = (PLINUX_MDL)Physical;

    gcmkTRACE_ZONE(
        gcvLEVEL_INFO, gcvZONE_OS,
        "%s(%d): Physical->0x%X PageCount->0x%X PagedMemory->?%d",
        __FUNCTION__, __LINE__,
        (gctUINT32) Physical,
        (gctUINT32) PageCount,
        mdl->pagedMem
        );

    MEMORY_LOCK(Os);

    table = (gctUINT32 *)PageTable;
#if gcdNONPAGED_MEMORY_CACHEABLE
    mmu = Os->device->kernels[Core]->mmu;
    bytes = PageCount * sizeof(*table);
    mmuMdl = (PLINUX_MDL)mmu->pageTablePhysical;
#endif

     /* Get all the physical addresses and store them in the page table. */

    offset = 0;

    if (mdl->pagedMem)
    {
        /* Try to get the user pages so DMA can happen. */
        while (PageCount-- > 0)
        {
#if gcdENABLE_VG
            if (Core == gcvCORE_VG)
            {
                if (mdl->contiguous)
                {
                    gcmkONERROR(
                        gckVGMMU_SetPage(Os->device->kernels[Core]->vg->mmu,
                             page_to_phys(nth_page(mdl->u.contiguousPages, offset)),
                             table));
                }
                else
                {
                    gcmkONERROR(
                        gckVGMMU_SetPage(Os->device->kernels[Core]->vg->mmu,
                             _NonContiguousToPhys(mdl->u.nonContiguousPages, offset),
                             table));
                }
            }
            else
#endif
            {
                if (mdl->contiguous)
                {
                    gcmkONERROR(
                        gckMMU_SetPage(Os->device->kernels[Core]->mmu,
                             page_to_phys(nth_page(mdl->u.contiguousPages, offset)),
                             table));
                }
                else
                {
                    gcmkONERROR(
                        gckMMU_SetPage(Os->device->kernels[Core]->mmu,
                             _NonContiguousToPhys(mdl->u.nonContiguousPages, offset),
                             table));
                }
            }

            table++;
            offset += 1;
        }
    }
    else
    {
        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_OS,
            "%s(%d): we should not get this call for Non Paged Memory!",
            __FUNCTION__, __LINE__
            );

        while (PageCount-- > 0)
        {
#if gcdENABLE_VG
            if (Core == gcvCORE_VG)
            {
                gcmkONERROR(
                        gckVGMMU_SetPage(Os->device->kernels[Core]->vg->mmu,
                                         page_to_phys(nth_page(mdl->u.contiguousPages, offset)),
                                         table));
            }
            else
#endif
            {
                gcmkONERROR(
                        gckMMU_SetPage(Os->device->kernels[Core]->mmu,
                                         page_to_phys(nth_page(mdl->u.contiguousPages, offset)),
                                         table));
            }
            table++;
            offset += 1;
        }
    }

#if gcdNONPAGED_MEMORY_CACHEABLE
    /* Get physical address of pageTable */
    pageTablePhysical = (gctPHYS_ADDR)(mmuMdl->dmaHandle +
                        ((gctUINT32 *)PageTable - mmu->pageTableLogical));

    /* Flush the mmu page table cache. */
    gcmkONERROR(gckOS_CacheClean(
        Os,
        _GetProcessID(),
        gcvNULL,
        pageTablePhysical,
        PageTable,
        bytes
        ));
#endif

OnError:

    MEMORY_UNLOCK(Os);

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_UnlockPages
**
**  Unlock memory allocated from the paged pool.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Physical address of the allocation.
**
**      gctSIZE_T Bytes
**          Number of bytes of the allocation.
**
**      gctPOINTER Logical
**          Address of the mapped memory.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UnlockPages(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctSIZE_T Bytes,
    IN gctPOINTER Logical
    )
{
    PLINUX_MDL_MAP          mdlMap;
    PLINUX_MDL              mdl = (PLINUX_MDL)Physical;
    struct task_struct *    task;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Bytes=%u Logical=0x%X",
                   Os, Physical, Bytes, Logical);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    /* Make sure there is already a mapping...*/
    gcmkVERIFY_ARGUMENT(mdl->u.nonContiguousPages != gcvNULL
                       || mdl->u.contiguousPages != gcvNULL);

    MEMORY_LOCK(Os);

    mdlMap = mdl->maps;

    while (mdlMap != gcvNULL)
    {
        if ((mdlMap->vmaAddr != gcvNULL) && (_GetProcessID() == mdlMap->pid))
        {
            /* Get the current pointer for the task with stored pid. */
            task = FIND_TASK_BY_PID(mdlMap->pid);

            if (task != gcvNULL && task->mm != gcvNULL)
            {
                down_write(&task->mm->mmap_sem);
                do_munmap(task->mm, (unsigned long)mdlMap->vmaAddr, mdl->numPages * PAGE_SIZE);
                up_write(&task->mm->mmap_sem);
            }

            mdlMap->vmaAddr = gcvNULL;
        }

        mdlMap = mdlMap->next;
    }

    MEMORY_UNLOCK(Os);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}


/*******************************************************************************
**
**  gckOS_AllocateContiguous
**
**  Allocate memory from the contiguous pool.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctBOOL InUserSpace
**          gcvTRUE if the pages need to be mapped into user space.
**
**      gctSIZE_T * Bytes
**          Pointer to the number of bytes to allocate.
**
**  OUTPUT:
**
**      gctSIZE_T * Bytes
**          Pointer to a variable that receives the number of bytes allocated.
**
**      gctPHYS_ADDR * Physical
**          Pointer to a variable that receives the physical address of the
**          memory allocation.
**
**      gctPOINTER * Logical
**          Pointer to a variable that receives the logical address of the
**          memory allocation.
*/
gceSTATUS
gckOS_AllocateContiguous(
    IN gckOS Os,
    IN gctBOOL InUserSpace,
    IN OUT gctSIZE_T * Bytes,
    OUT gctPHYS_ADDR * Physical,
    OUT gctPOINTER * Logical
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X InUserSpace=%d *Bytes=%lu",
                   Os, InUserSpace, gcmOPT_VALUE(Bytes));

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Bytes != gcvNULL);
    gcmkVERIFY_ARGUMENT(*Bytes > 0);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);

    /* Same as non-paged memory for now. */
    gcmkONERROR(gckOS_AllocateNonPagedMemory(Os,
                                             InUserSpace,
                                             Bytes,
                                             Physical,
                                             Logical));

    /* Success. */
    gcmkFOOTER_ARG("*Bytes=%lu *Physical=0x%X *Logical=0x%X",
                   *Bytes, *Physical, *Logical);
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_FreeContiguous
**
**  Free memory allocated from the contiguous pool.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPHYS_ADDR Physical
**          Physical address of the allocation.
**
**      gctPOINTER Logical
**          Logicval address of the allocation.
**
**      gctSIZE_T Bytes
**          Number of bytes of the allocation.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_FreeContiguous(
    IN gckOS Os,
    IN gctPHYS_ADDR Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Physical=0x%X Logical=0x%X Bytes=%lu",
                   Os, Physical, Logical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    /* Same of non-paged memory for now. */
    gcmkONERROR(gckOS_FreeNonPagedMemory(Os, Bytes, Physical, Logical));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

#if gcdENABLE_VG
/******************************************************************************
**
**  gckOS_GetKernelLogical
**
**  Return the kernel logical pointer that corresponods to the specified
**  hardware address.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 Address
**          Hardware physical address.
**
**  OUTPUT:
**
**      gctPOINTER * KernelPointer
**          Pointer to a variable receiving the pointer in kernel address space.
*/
gceSTATUS
gckOS_GetKernelLogical(
    IN gckOS Os,
    IN gctUINT32 Address,
    OUT gctPOINTER * KernelPointer
    )
{
    return gckOS_GetKernelLogicalEx(Os, gcvCORE_MAJOR, Address, KernelPointer);
}

gceSTATUS
gckOS_GetKernelLogicalEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctUINT32 Address,
    OUT gctPOINTER * KernelPointer
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Core=%d Address=0x%08x", Os, Core, Address);

    do
    {
        gckGALDEVICE device;
        gckKERNEL kernel;
        gcePOOL pool;
        gctUINT32 offset;
        gctPOINTER logical;

        /* Extract the pointer to the gckGALDEVICE class. */
        device = (gckGALDEVICE) Os->device;

        /* Kernel shortcut. */
        kernel = device->kernels[Core];
#if gcdENABLE_VG
       if (Core == gcvCORE_VG)
       {
           gcmkERR_BREAK(gckVGHARDWARE_SplitMemory(
                kernel->vg->hardware, Address, &pool, &offset
                ));
       }
       else
#endif
       {
        /* Split the memory address into a pool type and offset. */
            gcmkERR_BREAK(gckHARDWARE_SplitMemory(
                kernel->hardware, Address, &pool, &offset
                ));
       }

        /* Dispatch on pool. */
        switch (pool)
        {
        case gcvPOOL_LOCAL_INTERNAL:
            /* Internal memory. */
            logical = device->internalLogical;
            break;

        case gcvPOOL_LOCAL_EXTERNAL:
            /* External memory. */
            logical = device->externalLogical;
            break;

        case gcvPOOL_SYSTEM:
            /* System memory. */
            logical = device->contiguousBase;
            break;

        default:
            /* Invalid memory pool. */
            gcmkFOOTER();
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        /* Build logical address of specified address. */
        * KernelPointer = ((gctUINT8_PTR) logical) + offset;

        /* Success. */
        gcmkFOOTER_ARG("*KernelPointer=0x%X", *KernelPointer);
        return gcvSTATUS_OK;
    }
    while (gcvFALSE);

    /* Return status. */
    gcmkFOOTER();
    return status;
}
#endif

/*******************************************************************************
**
**  gckOS_MapUserPointer
**
**  Map a pointer from the user process into the kernel address space.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Pointer
**          Pointer in user process space that needs to be mapped.
**
**      gctSIZE_T Size
**          Number of bytes that need to be mapped.
**
**  OUTPUT:
**
**      gctPOINTER * KernelPointer
**          Pointer to a variable receiving the mapped pointer in kernel address
**          space.
*/
gceSTATUS
gckOS_MapUserPointer(
    IN gckOS Os,
    IN gctPOINTER Pointer,
    IN gctSIZE_T Size,
    OUT gctPOINTER * KernelPointer
    )
{
    gcmkHEADER_ARG("Os=0x%X Pointer=0x%X Size=%lu", Os, Pointer, Size);

#if NO_USER_DIRECT_ACCESS_FROM_KERNEL
{
    gctPOINTER buf = gcvNULL;
    gctUINT32 len;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Pointer != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);
    gcmkVERIFY_ARGUMENT(KernelPointer != gcvNULL);

    buf = kmalloc(Size, GFP_KERNEL | __GFP_NOWARN);
    if (buf == gcvNULL)
    {
        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): Failed to allocate memory.",
            __FUNCTION__, __LINE__
            );

        gcmkFOOTER_ARG("*status=%d", gcvSTATUS_OUT_OF_MEMORY);
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    len = copy_from_user(buf, Pointer, Size);
    if (len != 0)
    {
        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): Failed to copy data from user.",
            __FUNCTION__, __LINE__
            );

        if (buf != gcvNULL)
        {
            kfree(buf);
        }

        gcmkFOOTER_ARG("*status=%d", gcvSTATUS_GENERIC_IO);
        return gcvSTATUS_GENERIC_IO;
    }

    *KernelPointer = buf;
}
#else
    *KernelPointer = Pointer;
#endif /* NO_USER_DIRECT_ACCESS_FROM_KERNEL */

    gcmkFOOTER_ARG("*KernelPointer=0x%X", *KernelPointer);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_UnmapUserPointer
**
**  Unmap a user process pointer from the kernel address space.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Pointer
**          Pointer in user process space that needs to be unmapped.
**
**      gctSIZE_T Size
**          Number of bytes that need to be unmapped.
**
**      gctPOINTER KernelPointer
**          Pointer in kernel address space that needs to be unmapped.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UnmapUserPointer(
    IN gckOS Os,
    IN gctPOINTER Pointer,
    IN gctSIZE_T Size,
    IN gctPOINTER KernelPointer
    )
{
    gcmkHEADER_ARG("Os=0x%X Pointer=0x%X Size=%lu KernelPointer=0x%X",
                   Os, Pointer, Size, KernelPointer);

#if NO_USER_DIRECT_ACCESS_FROM_KERNEL
{
    gctUINT32 len;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Pointer != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);
    gcmkVERIFY_ARGUMENT(KernelPointer != gcvNULL);

    len = copy_to_user(Pointer, KernelPointer, Size);

    kfree(KernelPointer);

    if (len != 0)
    {
        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): Failed to copy data to user.",
            __FUNCTION__, __LINE__
            );

        gcmkFOOTER_ARG("status=%d", gcvSTATUS_GENERIC_IO);
        return gcvSTATUS_GENERIC_IO;
    }
}
#endif /* NO_USER_DIRECT_ACCESS_FROM_KERNEL */

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_QueryNeedCopy
**
**  Query whether the memory can be accessed or mapped directly or it has to be
**  copied.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctUINT32 ProcessID
**          Process ID of the current process.
**
**  OUTPUT:
**
**      gctBOOL_PTR NeedCopy
**          Pointer to a boolean receiving gcvTRUE if the memory needs a copy or
**          gcvFALSE if the memory can be accessed or mapped dircetly.
*/
gceSTATUS
gckOS_QueryNeedCopy(
    IN gckOS Os,
    IN gctUINT32 ProcessID,
    OUT gctBOOL_PTR NeedCopy
    )
{
    gcmkHEADER_ARG("Os=0x%X ProcessID=%d", Os, ProcessID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(NeedCopy != gcvNULL);

#if NO_USER_DIRECT_ACCESS_FROM_KERNEL
    /* We need to copy data. */
    *NeedCopy = gcvTRUE;
#else
    /* No need to copy data. */
    *NeedCopy = gcvFALSE;
#endif

    /* Success. */
    gcmkFOOTER_ARG("*NeedCopy=%d", *NeedCopy);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_CopyFromUserData
**
**  Copy data from user to kernel memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER KernelPointer
**          Pointer to kernel memory.
**
**      gctPOINTER Pointer
**          Pointer to user memory.
**
**      gctSIZE_T Size
**          Number of bytes to copy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_CopyFromUserData(
    IN gckOS Os,
    IN gctPOINTER KernelPointer,
    IN gctPOINTER Pointer,
    IN gctSIZE_T Size
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X KernelPointer=0x%X Pointer=0x%X Size=%lu",
                   Os, KernelPointer, Pointer, Size);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(KernelPointer != gcvNULL);
    gcmkVERIFY_ARGUMENT(Pointer != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);

    /* Copy data from user. */
    if (copy_from_user(KernelPointer, Pointer, Size) != 0)
    {
        /* Could not copy all the bytes. */
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

/*******************************************************************************
**
**  gckOS_CopyToUserData
**
**  Copy data from kernel to user memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER KernelPointer
**          Pointer to kernel memory.
**
**      gctPOINTER Pointer
**          Pointer to user memory.
**
**      gctSIZE_T Size
**          Number of bytes to copy.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_CopyToUserData(
    IN gckOS Os,
    IN gctPOINTER KernelPointer,
    IN gctPOINTER Pointer,
    IN gctSIZE_T Size
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X KernelPointer=0x%X Pointer=0x%X Size=%lu",
                   Os, KernelPointer, Pointer, Size);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(KernelPointer != gcvNULL);
    gcmkVERIFY_ARGUMENT(Pointer != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);

    /* Copy data to user. */
    if (copy_to_user(Pointer, KernelPointer, Size) != 0)
    {
        /* Could not copy all the bytes. */
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

/*******************************************************************************
**
**  gckOS_WriteMemory
**
**  Write data to a memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctPOINTER Address
**          Address of the memory to write to.
**
**      gctUINT32 Data
**          Data for register.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_WriteMemory(
    IN gckOS Os,
    IN gctPOINTER Address,
    IN gctUINT32 Data
    )
{
    gceSTATUS status;
    gcmkHEADER_ARG("Os=0x%X Address=0x%X Data=%u", Os, Address, Data);

    /* Verify the arguments. */
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    /* Write memory. */
#if NO_USER_DIRECT_ACCESS_FROM_KERNEL
    if (access_ok(VERIFY_WRITE, Address, 4))
    {
        /* User address. */
        if(put_user(Data, (gctUINT32*)Address))
        {
            gcmkONERROR(gcvSTATUS_INVALID_ADDRESS);
        }
    }
    else
#endif
    {
        /* Kernel address. */
        *(gctUINT32 *)Address = Data;
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_MapUserMemory
**
**  Lock down a user buffer and return an DMA'able address to be used by the
**  hardware to access it.
**
**  INPUT:
**
**      gctPOINTER Memory
**          Pointer to memory to lock down.
**
**      gctSIZE_T Size
**          Size in bytes of the memory to lock down.
**
**  OUTPUT:
**
**      gctPOINTER * Info
**          Pointer to variable receiving the information record required by
**          gckOS_UnmapUserMemory.
**
**      gctUINT32_PTR Address
**          Pointer to a variable that will receive the address DMA'able by the
**          hardware.
*/
gceSTATUS
gckOS_MapUserMemory(
    IN gckOS Os,
    IN gctPOINTER Memory,
    IN gctSIZE_T Size,
    OUT gctPOINTER * Info,
    OUT gctUINT32_PTR Address
    )
{
    return gckOS_MapUserMemoryEx(Os, gcvCORE_MAJOR, Memory, Size, Info, Address);
}

gceSTATUS
gckOS_MapUserMemoryEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctPOINTER Memory,
    IN gctSIZE_T Size,
    OUT gctPOINTER * Info,
    OUT gctUINT32_PTR Address
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%x Core=%d Memory=0x%x Size=%lu", Os, Core, Memory, Size);

#if gcdSECURE_USER
    gcmkONERROR(gckOS_AddMapping(Os, *Address, Memory, Size));

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
#else
{
    gctSIZE_T pageCount, i, j;
    gctUINT32_PTR pageTable;
    gctUINT32 address = 0, physical = ~0U;
    gctUINT32 start, end, memory;
    gctINT result = 0;

    gcsPageInfo_PTR info = gcvNULL;
    struct page **pages = gcvNULL;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);
    gcmkVERIFY_ARGUMENT(Info != gcvNULL);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    do
    {
        memory = (gctUINT32) Memory;

        /* Get the number of required pages. */
        end = (memory + Size + PAGE_SIZE - 1) >> PAGE_SHIFT;
        start = memory >> PAGE_SHIFT;
        pageCount = end - start;

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_OS,
            "%s(%d): pageCount: %d.",
            __FUNCTION__, __LINE__,
            pageCount
            );

        /* Invalid argument. */
        if (pageCount == 0)
        {
            gcmkFOOTER_ARG("status=%d", gcvSTATUS_INVALID_ARGUMENT);
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        /* Overflow. */
        if ((memory + Size) < memory)
        {
            gcmkFOOTER_ARG("status=%d", gcvSTATUS_INVALID_ARGUMENT);
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        MEMORY_MAP_LOCK(Os);

        /* Allocate the Info struct. */
        info = (gcsPageInfo_PTR)kmalloc(sizeof(gcsPageInfo), GFP_KERNEL | __GFP_NOWARN);

        if (info == gcvNULL)
        {
            status = gcvSTATUS_OUT_OF_MEMORY;
            break;
        }

        /* Allocate the array of page addresses. */
        pages = (struct page **)kmalloc(pageCount * sizeof(struct page *), GFP_KERNEL | __GFP_NOWARN);

        if (pages == gcvNULL)
        {
            status = gcvSTATUS_OUT_OF_MEMORY;
            break;
        }

        /* Get the user pages. */
        down_read(&current->mm->mmap_sem);
        result = get_user_pages(current,
                    current->mm,
                    memory & PAGE_MASK,
                    pageCount,
                    1,
                    0,
                    pages,
                    gcvNULL
                    );
        up_read(&current->mm->mmap_sem);

        if (result <=0 || result < pageCount)
        {
            struct vm_area_struct *vma;

            /* Free the page table. */
            if (pages != gcvNULL)
            {
                /* Release the pages if any. */
                if (result > 0)
                {
                    for (i = 0; i < result; i++)
                    {
                        if (pages[i] == gcvNULL)
                        {
                            break;
                        }

                        page_cache_release(pages[i]);
                    }
                }

                kfree(pages);
                pages = gcvNULL;
            }

            vma = find_vma(current->mm, memory);

            if (vma && (vma->vm_flags & VM_PFNMAP) )
            {
                pte_t       * pte;
                spinlock_t  * ptl;
                unsigned long pfn;

                pgd_t * pgd = pgd_offset(current->mm, memory);
                pud_t * pud = pud_offset(pgd, memory);
                if (pud)
                {
                    pmd_t * pmd = pmd_offset(pud, memory);
                    pte = pte_offset_map_lock(current->mm, pmd, memory, &ptl);
                    if (!pte)
                    {
                        gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
                    }
                }
                else
                {
                    gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
                }

                pfn      = pte_pfn(*pte);

                physical = (pfn << PAGE_SHIFT) | (memory & ~PAGE_MASK);

                pte_unmap_unlock(pte, ptl);

                if ((Os->device->kernels[Core]->hardware->mmuVersion == 0)
                    && !((physical - Os->device->baseAddress) & 0x80000000))
                {
                    /* Release page info struct. */
                    if (info != gcvNULL)
                    {
                        /* Free the page info struct. */
                        kfree(info);
                    }

                    MEMORY_MAP_UNLOCK(Os);

                    *Address = physical - Os->device->baseAddress;
                    *Info    = gcvNULL;

                    gcmkFOOTER_ARG("*Info=0x%X *Address=0x%08x",
                                   *Info, *Address);

                    return gcvSTATUS_OK;
                }
            }
            else
            {
                gcmkONERROR(gcvSTATUS_OUT_OF_RESOURCES);
            }
        }

        if (pages)
        {
            for (i = 0; i < pageCount; i++)
            {
                /* Flush(clean) the data cache. */
                gcmkONERROR(gckOS_CacheFlush(Os, _GetProcessID(), gcvNULL,
                                 (gctPOINTER)page_to_phys(pages[i]),
                                 (gctPOINTER)(memory & PAGE_MASK) + i*PAGE_SIZE,
                                 PAGE_SIZE));
            }
        }
        else
        {
            /* Flush(clean) the data cache. */
            gcmkONERROR(gckOS_CacheFlush(Os, _GetProcessID(), gcvNULL,
                             (gctPOINTER)(physical & PAGE_MASK),
                             (gctPOINTER)(memory & PAGE_MASK),
                             PAGE_SIZE * pageCount));

        }

#if gcdENABLE_VG
        if (Core == gcvCORE_VG)
        {
            /* Allocate pages inside the page table. */
            gcmkERR_BREAK(gckVGMMU_AllocatePages(Os->device->kernels[Core]->vg->mmu,
                                              pageCount * (PAGE_SIZE/4096),
                                              (gctPOINTER *) &pageTable,
                                              &address));
        }
        else
#endif
        {
            /* Allocate pages inside the page table. */
            gcmkERR_BREAK(gckMMU_AllocatePages(Os->device->kernels[Core]->mmu,
                                              pageCount * (PAGE_SIZE/4096),
                                              (gctPOINTER *) &pageTable,
                                              &address));
        }
        /* Fill the page table. */
        for (i = 0; i < pageCount; i++)
        {
            gctUINT32 phys;
            gctUINT32_PTR tab = pageTable + i * (PAGE_SIZE/4096);

            if (pages)
            {
                phys = page_to_phys(pages[i]);
            }
            else
            {
                phys = (physical & PAGE_MASK) + i * PAGE_SIZE;
            }

#if gcdENABLE_VG
            if (Core == gcvCORE_VG)
            {
                /* Get the physical address from page struct. */
                gcmkONERROR(
                    gckVGMMU_SetPage(Os->device->kernels[Core]->vg->mmu,
                                   phys,
                                   tab));
            }
            else
#endif
            {
                /* Get the physical address from page struct. */
                gcmkONERROR(
                    gckMMU_SetPage(Os->device->kernels[Core]->mmu,
                                   phys,
                                   tab));
            }

            for (j = 1; j < (PAGE_SIZE/4096); j++)
            {
                pageTable[i * (PAGE_SIZE/4096) + j] = pageTable[i * (PAGE_SIZE/4096)] + 4096 * j;
            }

            gcmkTRACE_ZONE(
                gcvLEVEL_INFO, gcvZONE_OS,
                "%s(%d): pageTable[%d]: 0x%X 0x%X.",
                __FUNCTION__, __LINE__,
                i, phys, pageTable[i]);
        }

#if gcdENABLE_VG
        if (Core != gcvCORE_VG)
#endif
        {
            gcmkONERROR(gckMMU_Flush(Os->device->kernels[Core]->mmu));
        }

        /* Save pointer to page table. */
        info->pageTable = pageTable;
        info->pages = pages;

        *Info = (gctPOINTER) info;

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_OS,
            "%s(%d): info->pages: 0x%X, info->pageTable: 0x%X, info: 0x%X.",
            __FUNCTION__, __LINE__,
            info->pages,
            info->pageTable,
            info
            );

        /* Return address. */
        *Address = address + (memory & ~PAGE_MASK);

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_OS,
            "%s(%d): Address: 0x%X.",
            __FUNCTION__, __LINE__,
            *Address
            );

        /* Success. */
        status = gcvSTATUS_OK;
    }
    while (gcvFALSE);

OnError:

    if (gcmIS_ERROR(status))
    {
        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): error occured: %d.",
            __FUNCTION__, __LINE__,
            status
            );

        /* Release page array. */
        if (result > 0 && pages != gcvNULL)
        {
            gcmkTRACE(
                gcvLEVEL_ERROR,
                "%s(%d): error: page table is freed.",
                __FUNCTION__, __LINE__
                );

            for (i = 0; i < result; i++)
            {
                if (pages[i] == gcvNULL)
                {
                    break;
                }
                page_cache_release(pages[i]);
            }
        }

        if (info!= gcvNULL && pages != gcvNULL)
        {
            gcmkTRACE(
                gcvLEVEL_ERROR,
                "%s(%d): error: pages is freed.",
                __FUNCTION__, __LINE__
                );

            /* Free the page table. */
            kfree(pages);
            info->pages = gcvNULL;
        }

        /* Release page info struct. */
        if (info != gcvNULL)
        {
            gcmkTRACE(
                gcvLEVEL_ERROR,
                "%s(%d): error: info is freed.",
                __FUNCTION__, __LINE__
                );

            /* Free the page info struct. */
            kfree(info);
            *Info = gcvNULL;
        }
    }

    MEMORY_MAP_UNLOCK(Os);

    /* Return the status. */
    if (gcmIS_SUCCESS(status))
    {
        gcmkFOOTER_ARG("*Info=0x%X *Address=0x%08x", *Info, *Address);
    }
    else
    {
        gcmkFOOTER();
    }

    return status;
}
#endif
}

/*******************************************************************************
**
**  gckOS_UnmapUserMemory
**
**  Unlock a user buffer and that was previously locked down by
**  gckOS_MapUserMemory.
**
**  INPUT:
**
**      gctPOINTER Memory
**          Pointer to memory to unlock.
**
**      gctSIZE_T Size
**          Size in bytes of the memory to unlock.
**
**      gctPOINTER Info
**          Information record returned by gckOS_MapUserMemory.
**
**      gctUINT32_PTR Address
**          The address returned by gckOS_MapUserMemory.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UnmapUserMemory(
    IN gckOS Os,
    IN gctPOINTER Memory,
    IN gctSIZE_T Size,
    IN gctPOINTER Info,
    IN gctUINT32 Address
    )
{
    return gckOS_UnmapUserMemoryEx(Os, gcvCORE_MAJOR, Memory, Size, Info, Address);
}

gceSTATUS
gckOS_UnmapUserMemoryEx(
    IN gckOS Os,
    IN gceCORE Core,
    IN gctPOINTER Memory,
    IN gctSIZE_T Size,
    IN gctPOINTER Info,
    IN gctUINT32 Address
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Core=%d Memory=0x%X Size=%lu Info=0x%X Address0x%08x",
                   Os, Core, Memory, Size, Info, Address);

#if gcdSECURE_USER
    gcmkONERROR(gckOS_RemoveMapping(Os, Memory, Size));

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    gcmkFOOTER();
    return status;
#else
{
    gctUINT32 memory, start, end;
    gcsPageInfo_PTR info;
    gctSIZE_T pageCount, i;
    struct page **pages;

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);
    gcmkVERIFY_ARGUMENT(Size > 0);
    gcmkVERIFY_ARGUMENT(Info != gcvNULL);

    do
    {
        /*gctUINT32 physical = ~0U;*/

        info = (gcsPageInfo_PTR) Info;

        pages = info->pages;

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_OS,
            "%s(%d): info=0x%X, pages=0x%X.",
            __FUNCTION__, __LINE__,
            info, pages
            );

        /* Invalid page array. */
        if (pages == gcvNULL)
        {
            if (info->pageTable == gcvNULL)
            {
                kfree(info);

                gcmkFOOTER_ARG("status=%d", gcvSTATUS_INVALID_ARGUMENT);
                return gcvSTATUS_INVALID_ARGUMENT;
            }
            else
            {
                /*physical = (*info->pageTable) & PAGE_MASK;*/
            }
        }

        memory = (gctUINT32) Memory;
        end = (memory + Size + PAGE_SIZE - 1) >> PAGE_SHIFT;
        start = memory >> PAGE_SHIFT;
        pageCount = end - start;

        /* Overflow. */
        if ((memory + Size) < memory)
        {
            gcmkFOOTER_ARG("status=%d", gcvSTATUS_INVALID_ARGUMENT);
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        /* Invalid argument. */
        if (pageCount == 0)
        {
            gcmkFOOTER_ARG("status=%d", gcvSTATUS_INVALID_ARGUMENT);
            return gcvSTATUS_INVALID_ARGUMENT;
        }

        gcmkTRACE_ZONE(
            gcvLEVEL_INFO, gcvZONE_OS,
            "%s(%d): memory: 0x%X, pageCount: %d, pageTable: 0x%X.",
            __FUNCTION__, __LINE__,
            memory, pageCount, info->pageTable
            );

        MEMORY_MAP_LOCK(Os);

#if gcdENABLE_VG
        if (Core == gcvCORE_VG)
        {
            /* Free the pages from the MMU. */
            gcmkERR_BREAK(gckVGMMU_FreePages(Os->device->kernels[Core]->vg->mmu,
                                          info->pageTable,
                                          pageCount * (PAGE_SIZE/4096)
                                          ));
        }
        else
#endif
        {
            /* Free the pages from the MMU. */
            gcmkERR_BREAK(gckMMU_FreePages(Os->device->kernels[Core]->mmu,
                                          info->pageTable,
                                          pageCount * (PAGE_SIZE/4096)
                                          ));
        }

        /* Release the page cache. */
        if (pages)
        {
            for (i = 0; i < pageCount; i++)
            {
                gcmkTRACE_ZONE(
                    gcvLEVEL_INFO, gcvZONE_OS,
                    "%s(%d): pages[%d]: 0x%X.",
                    __FUNCTION__, __LINE__,
                    i, pages[i]
                    );

                if (!PageReserved(pages[i]))
                {
                     SetPageDirty(pages[i]);
                }

                page_cache_release(pages[i]);
            }
        }

        /* Success. */
        status = gcvSTATUS_OK;
    }
    while (gcvFALSE);

    if (info != gcvNULL)
    {
        /* Free the page array. */
        if (info->pages != gcvNULL)
        {
            kfree(info->pages);
        }

        kfree(info);
    }

    MEMORY_MAP_UNLOCK(Os);

    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif
}

/*******************************************************************************
**
**  gckOS_GetBaseAddress
**
**  Get the base address for the physical memory.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**  OUTPUT:
**
**      gctUINT32_PTR BaseAddress
**          Pointer to a variable that will receive the base address.
*/
gceSTATUS
gckOS_GetBaseAddress(
    IN gckOS Os,
    OUT gctUINT32_PTR BaseAddress
    )
{
    gcmkHEADER_ARG("Os=0x%X", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(BaseAddress != gcvNULL);

    /* Return base address. */
    *BaseAddress = Os->device->baseAddress;

    /* Success. */
    gcmkFOOTER_ARG("*BaseAddress=0x%08x", *BaseAddress);
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_SuspendInterrupt(
    IN gckOS Os
    )
{
    return gckOS_SuspendInterruptEx(Os, gcvCORE_MAJOR);
}

gceSTATUS
gckOS_SuspendInterruptEx(
    IN gckOS Os,
    IN gceCORE Core
    )
{
    gcmkHEADER_ARG("Os=0x%X Core=%d", Os, Core);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    disable_irq(Os->device->irqLines[Core]);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_ResumeInterrupt(
    IN gckOS Os
    )
{
    return gckOS_ResumeInterruptEx(Os, gcvCORE_MAJOR);
}

gceSTATUS
gckOS_ResumeInterruptEx(
    IN gckOS Os,
    IN gceCORE Core
    )
{
    gcmkHEADER_ARG("Os=0x%X Core=%d", Os, Core);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    enable_irq(Os->device->irqLines[Core]);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_MemCopy(
    IN gctPOINTER Destination,
    IN gctCONST_POINTER Source,
    IN gctSIZE_T Bytes
    )
{
    gcmkHEADER_ARG("Destination=0x%X Source=0x%X Bytes=%lu",
                   Destination, Source, Bytes);

    gcmkVERIFY_ARGUMENT(Destination != gcvNULL);
    gcmkVERIFY_ARGUMENT(Source != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    memcpy(Destination, Source, Bytes);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_ZeroMemory(
    IN gctPOINTER Memory,
    IN gctSIZE_T Bytes
    )
{
    gcmkHEADER_ARG("Memory=0x%X Bytes=%lu", Memory, Bytes);

    gcmkVERIFY_ARGUMENT(Memory != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

    memset(Memory, 0, Bytes);

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
********************************* Cache Control ********************************
*******************************************************************************/

#if !gcdCACHE_FUNCTION_UNIMPLEMENTED && defined(CONFIG_OUTER_CACHE)
static inline gceSTATUS
outer_func(
    gceCACHEOPERATION Type,
    unsigned long Start,
    unsigned long End
    )
{
    switch (Type)
    {
        case gcvCACHE_CLEAN:
            outer_clean_range(Start, End);
            break;
        case gcvCACHE_INVALIDATE:
            outer_inv_range(Start, End);
            break;
        case gcvCACHE_FLUSH:
            outer_flush_range(Start, End);
            break;
        default:
            return gcvSTATUS_INVALID_ARGUMENT;
            break;
    }
    return gcvSTATUS_OK;
}

#if gcdENABLE_OUTER_CACHE_PATCH
/*******************************************************************************
**  _HandleOuterCache
**
**  Handle the outer cache for the specified addresses.
**
**  ARGUMENTS:
**
**      gckOS Os
**          Pointer to gckOS object.
**
**      gctUINT32 ProcessID
**          Process ID Logical belongs.
**
**      gctPHYS_ADDR Handle
**          Physical address handle.  If gcvNULL it is video memory.
**
**      gctPOINTER Physical
**          Physical address to flush.
**
**      gctPOINTER Logical
**          Logical address to flush.
**
**      gctSIZE_T Bytes
**          Size of the address range in bytes to flush.
**
**      gceOUTERCACHE_OPERATION Type
**          Operation need to be execute.
*/
static gceSTATUS
_HandleOuterCache(
    IN gckOS Os,
    IN gctUINT32 ProcessID,
    IN gctPHYS_ADDR Handle,
    IN gctPOINTER Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes,
    IN gceCACHEOPERATION Type
    )
{
    gceSTATUS status;
    gctUINT32 i, pageNum;
    unsigned long paddr;
    gctPOINTER vaddr;

    gcmkHEADER_ARG("Os=0x%X ProcessID=%d Handle=0x%X Logical=0x%X Bytes=%lu",
                   Os, ProcessID, Handle, Logical, Bytes);

    if (Physical != gcvNULL)
    {
        /* Non paged memory or gcvPOOL_USER surface */
        paddr = (unsigned long) Physical;
        gcmkONERROR(outer_func(Type, paddr, paddr + Bytes));
    }
    else if ((Handle == gcvNULL)
    || (Handle != gcvNULL && ((PLINUX_MDL)Handle)->contiguous)
    )
    {
        /* Video Memory or contiguous virtual memory */
        gcmkONERROR(gckOS_GetPhysicalAddress(Os, Logical, (gctUINT32*)&paddr));
        gcmkONERROR(outer_func(Type, paddr, paddr + Bytes));
    }
    else
    {
        /* Non contiguous virtual memory */
        vaddr = (gctPOINTER)gcmALIGN_BASE((gctUINT32)Logical, PAGE_SIZE);
        pageNum = GetPageCount(Bytes, 0);

        for (i = 0; i < pageNum; i += 1)
        {
            gcmkONERROR(_ConvertLogical2Physical(
                Os,
                vaddr + PAGE_SIZE * i,
                ProcessID,
                (PLINUX_MDL)Handle,
                (gctUINT32*)&paddr
                ));

            gcmkONERROR(outer_func(Type, paddr, paddr + PAGE_SIZE));
        }
    }

    mb();

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}
#endif
#endif

/*******************************************************************************
**  gckOS_CacheClean
**
**  Clean the cache for the specified addresses.  The GPU is going to need the
**  data.  If the system is allocating memory as non-cachable, this function can
**  be ignored.
**
**  ARGUMENTS:
**
**      gckOS Os
**          Pointer to gckOS object.
**
**      gctUINT32 ProcessID
**          Process ID Logical belongs.
**
**      gctPHYS_ADDR Handle
**          Physical address handle.  If gcvNULL it is video memory.
**
**      gctPOINTER Physical
**          Physical address to flush.
**
**      gctPOINTER Logical
**          Logical address to flush.
**
**      gctSIZE_T Bytes
**          Size of the address range in bytes to flush.
*/
gceSTATUS
gckOS_CacheClean(
    IN gckOS Os,
    IN gctUINT32 ProcessID,
    IN gctPHYS_ADDR Handle,
    IN gctPOINTER Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    gcmkHEADER_ARG("Os=0x%X ProcessID=%d Handle=0x%X Logical=0x%X Bytes=%lu",
                   Os, ProcessID, Handle, Logical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

#if !gcdCACHE_FUNCTION_UNIMPLEMENTED
#ifdef CONFIG_ARM

    /* Inner cache. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)
    dmac_map_area(Logical, Bytes, DMA_TO_DEVICE);
#      else
    dmac_clean_range(Logical, Logical + Bytes);
#      endif

#if defined(CONFIG_OUTER_CACHE)
    /* Outer cache. */
#if gcdENABLE_OUTER_CACHE_PATCH
    _HandleOuterCache(Os, ProcessID, Handle, Physical, Logical, Bytes, gcvCACHE_CLEAN);
#else
    outer_clean_range((unsigned long) Handle, (unsigned long) Handle + Bytes);
#endif
#endif

#elif defined(CONFIG_MIPS)

    dma_cache_wback((unsigned long) Logical, Bytes);

#else
    dma_sync_single_for_device(
              gcvNULL,
              Physical,
              Bytes,
              DMA_TO_DEVICE);
#endif
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**  gckOS_CacheInvalidate
**
**  Invalidate the cache for the specified addresses. The GPU is going to need
**  data.  If the system is allocating memory as non-cachable, this function can
**  be ignored.
**
**  ARGUMENTS:
**
**      gckOS Os
**          Pointer to gckOS object.
**
**      gctUINT32 ProcessID
**          Process ID Logical belongs.
**
**      gctPHYS_ADDR Handle
**          Physical address handle.  If gcvNULL it is video memory.
**
**      gctPOINTER Logical
**          Logical address to flush.
**
**      gctSIZE_T Bytes
**          Size of the address range in bytes to flush.
*/
gceSTATUS
gckOS_CacheInvalidate(
    IN gckOS Os,
    IN gctUINT32 ProcessID,
    IN gctPHYS_ADDR Handle,
    IN gctPOINTER Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    gcmkHEADER_ARG("Os=0x%X ProcessID=%d Handle=0x%X Logical=0x%X Bytes=%lu",
                   Os, ProcessID, Handle, Logical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

#if !gcdCACHE_FUNCTION_UNIMPLEMENTED
#ifdef CONFIG_ARM

    /* Inner cache. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)
    dmac_map_area(Logical, Bytes, DMA_FROM_DEVICE);
#      else
    dmac_inv_range(Logical, Logical + Bytes);
#      endif

#if defined(CONFIG_OUTER_CACHE)
    /* Outer cache. */
#if gcdENABLE_OUTER_CACHE_PATCH
    _HandleOuterCache(Os, ProcessID, Handle, Physical, Logical, Bytes, gcvCACHE_INVALIDATE);
#else
    outer_inv_range((unsigned long) Handle, (unsigned long) Handle + Bytes);
#endif
#endif

#elif defined(CONFIG_MIPS)
    dma_cache_inv((unsigned long) Logical, Bytes);
#else
    dma_sync_single_for_device(
              gcvNULL,
              Physical,
              Bytes,
              DMA_FROM_DEVICE);
#endif
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**  gckOS_CacheFlush
**
**  Clean the cache for the specified addresses and invalidate the lines as
**  well.  The GPU is going to need and modify the data.  If the system is
**  allocating memory as non-cachable, this function can be ignored.
**
**  ARGUMENTS:
**
**      gckOS Os
**          Pointer to gckOS object.
**
**      gctUINT32 ProcessID
**          Process ID Logical belongs.
**
**      gctPHYS_ADDR Handle
**          Physical address handle.  If gcvNULL it is video memory.
**
**      gctPOINTER Logical
**          Logical address to flush.
**
**      gctSIZE_T Bytes
**          Size of the address range in bytes to flush.
*/
gceSTATUS
gckOS_CacheFlush(
    IN gckOS Os,
    IN gctUINT32 ProcessID,
    IN gctPHYS_ADDR Handle,
    IN gctPOINTER Physical,
    IN gctPOINTER Logical,
    IN gctSIZE_T Bytes
    )
{
    gcmkHEADER_ARG("Os=0x%X ProcessID=%d Handle=0x%X Logical=0x%X Bytes=%lu",
                   Os, ProcessID, Handle, Logical, Bytes);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
    gcmkVERIFY_ARGUMENT(Bytes > 0);

#if !gcdCACHE_FUNCTION_UNIMPLEMENTED
#ifdef CONFIG_ARM
    /* Inner cache. */
    dmac_flush_range(Logical, Logical + Bytes);

#if defined(CONFIG_OUTER_CACHE)
    /* Outer cache. */
#if gcdENABLE_OUTER_CACHE_PATCH
    _HandleOuterCache(Os, ProcessID, Handle, Physical, Logical, Bytes, gcvCACHE_FLUSH);
#else
    outer_flush_range((unsigned long) Handle, (unsigned long) Handle + Bytes);
#endif
#endif

#elif defined(CONFIG_MIPS)
    dma_cache_wback_inv((unsigned long) Logical, Bytes);
#else
    dma_sync_single_for_device(
              gcvNULL,
              Physical,
              Bytes,
              DMA_BIDIRECTIONAL);
#endif
#endif

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
********************************* Broadcasting *********************************
*******************************************************************************/

/*******************************************************************************
**
**  gckOS_Broadcast
**
**  System hook for broadcast events from the kernel driver.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**      gceBROADCAST Reason
**          Reason for the broadcast.  Can be one of the following values:
**
**              gcvBROADCAST_GPU_IDLE
**                  Broadcasted when the kernel driver thinks the GPU might be
**                  idle.  This can be used to handle power management.
**
**              gcvBROADCAST_GPU_COMMIT
**                  Broadcasted when any client process commits a command
**                  buffer.  This can be used to handle power management.
**
**              gcvBROADCAST_GPU_STUCK
**                  Broadcasted when the kernel driver hits the timeout waiting
**                  for the GPU.
**
**              gcvBROADCAST_FIRST_PROCESS
**                  First process is trying to connect to the kernel.
**
**              gcvBROADCAST_LAST_PROCESS
**                  Last process has detached from the kernel.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_Broadcast(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    IN gceBROADCAST Reason
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%X Hardware=0x%X Reason=%d", Os, Hardware, Reason);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_OBJECT(Hardware, gcvOBJ_HARDWARE);

    switch (Reason)
    {
    case gcvBROADCAST_FIRST_PROCESS:
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS, "First process has attached");
        break;

    case gcvBROADCAST_LAST_PROCESS:
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS, "Last process has detached");

        /* Put GPU OFF. */
        gcmkONERROR(
            gckHARDWARE_SetPowerManagementState(Hardware,
                                                gcvPOWER_OFF_BROADCAST));
        break;

    case gcvBROADCAST_GPU_IDLE:
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS, "GPU idle.");

        /* Put GPU IDLE. */
        gcmkONERROR(
            gckHARDWARE_SetPowerManagementState(Hardware,
                                                gcvPOWER_IDLE_BROADCAST));

        /* Add idle process DB. */
        gcmkONERROR(gckKERNEL_AddProcessDB(Hardware->kernel,
                                           1,
                                           gcvDB_IDLE,
                                           gcvNULL, gcvNULL, 0));
        break;

    case gcvBROADCAST_GPU_COMMIT:
        gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_OS, "COMMIT has arrived.");

        /* Add busy process DB. */
        gcmkONERROR(gckKERNEL_AddProcessDB(Hardware->kernel,
                                           0,
                                           gcvDB_IDLE,
                                           gcvNULL, gcvNULL, 0));

        /* Put GPU ON. */
        gcmkONERROR(
            gckHARDWARE_SetPowerManagementState(Hardware, gcvPOWER_ON_AUTO));
        break;

    case gcvBROADCAST_GPU_STUCK:
        gcmkTRACE_N(gcvLEVEL_ERROR, 0, "gcvBROADCAST_GPU_STUCK\n");
        gcmkONERROR(_DumpGPUState(Os, gcvCORE_MAJOR));
        gcmkONERROR(gckKERNEL_Recovery(Hardware->kernel));
        break;

    case gcvBROADCAST_AXI_BUS_ERROR:
        gcmkTRACE_N(gcvLEVEL_ERROR, 0, "gcvBROADCAST_AXI_BUS_ERROR\n");
        gcmkONERROR(_DumpGPUState(Os, gcvCORE_MAJOR));
        gcmkONERROR(gckKERNEL_Recovery(Hardware->kernel));
        break;
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
**  gckOS_BroadcastHurry
**
**  The GPU is running too slow.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**      gctUINT Urgency
**          The higher the number, the higher the urgency to speed up the GPU.
**          The maximum value is defined by the gcdDYNAMIC_EVENT_THRESHOLD.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_BroadcastHurry(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    IN gctUINT Urgency
    )
{
    gcmkHEADER_ARG("Os=0x%x Hardware=0x%x Urgency=%u", Os, Hardware, Urgency);

    /* Do whatever you need to do to speed up the GPU now. */

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_BroadcastCalibrateSpeed
**
**  Calibrate the speed of the GPU.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gckHARDWARE Hardware
**          Pointer to the gckHARDWARE object.
**
**      gctUINT Idle, Time
**          Idle/Time will give the percentage the GPU is idle, so you can use
**          this to calibrate the working point of the GPU.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_BroadcastCalibrateSpeed(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    IN gctUINT Idle,
    IN gctUINT Time
    )
{
    gcmkHEADER_ARG("Os=0x%x Hardware=0x%x Idle=%u Time=%u",
                   Os, Hardware, Idle, Time);

    /* Do whatever you need to do to callibrate the GPU speed. */

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
********************************** Semaphores **********************************
*******************************************************************************/

/*******************************************************************************
**
**  gckOS_CreateSemaphore
**
**  Create a semaphore.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**  OUTPUT:
**
**      gctPOINTER * Semaphore
**          Pointer to the variable that will receive the created semaphore.
*/
gceSTATUS
gckOS_CreateSemaphore(
    IN gckOS Os,
    OUT gctPOINTER * Semaphore
    )
{
    gceSTATUS status;
    struct semaphore *sem = gcvNULL;

    gcmkHEADER_ARG("Os=0x%X", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Allocate the semaphore structure. */
    sem = (struct semaphore *)kmalloc(gcmSIZEOF(struct semaphore), GFP_KERNEL | __GFP_NOWARN);
    if (sem == gcvNULL)
    {
        gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
    }

    /* Initialize the semaphore. */
    sema_init(sem, 1);

    /* Return to caller. */
    *Semaphore = (gctPOINTER) sem;

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
**  gckOS_AcquireSemaphore
**
**  Acquire a semaphore.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gctPOINTER Semaphore
**          Pointer to the semaphore thet needs to be acquired.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_AcquireSemaphore(
    IN gckOS Os,
    IN gctPOINTER Semaphore
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%08X Semaphore=0x%08X", Os, Semaphore);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Acquire the semaphore. */
    if (down_interruptible((struct semaphore *) Semaphore))
    {
        gcmkONERROR(gcvSTATUS_TIMEOUT);
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
**  gckOS_TryAcquireSemaphore
**
**  Try to acquire a semaphore.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gctPOINTER Semaphore
**          Pointer to the semaphore thet needs to be acquired.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_TryAcquireSemaphore(
    IN gckOS Os,
    IN gctPOINTER Semaphore
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Os=0x%x", Os);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Acquire the semaphore. */
    if (down_trylock((struct semaphore *) Semaphore))
    {
        /* Timeout. */
        status = gcvSTATUS_TIMEOUT;
        gcmkFOOTER();
        return status;
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_ReleaseSemaphore
**
**  Release a previously acquired semaphore.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gctPOINTER Semaphore
**          Pointer to the semaphore thet needs to be released.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_ReleaseSemaphore(
    IN gckOS Os,
    IN gctPOINTER Semaphore
    )
{
    gcmkHEADER_ARG("Os=0x%X Semaphore=0x%X", Os, Semaphore);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Release the semaphore. */
    up((struct semaphore *) Semaphore);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_DestroySemaphore
**
**  Destroy a semaphore.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gctPOINTER Semaphore
**          Pointer to the semaphore thet needs to be destroyed.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_DestroySemaphore(
    IN gckOS Os,
    IN gctPOINTER Semaphore
    )
{
    gcmkHEADER_ARG("Os=0x%X Semaphore=0x%X", Os, Semaphore);

     /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Free the sempahore structure. */
    kfree(Semaphore);

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_GetProcessID
**
**  Get current process ID.
**
**  INPUT:
**
**      Nothing.
**
**  OUTPUT:
**
**      gctUINT32_PTR ProcessID
**          Pointer to the variable that receives the process ID.
*/
gceSTATUS
gckOS_GetProcessID(
    OUT gctUINT32_PTR ProcessID
    )
{
    /* Get process ID. */
    if (ProcessID != gcvNULL)
    {
        *ProcessID = _GetProcessID();
    }

    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_GetThreadID
**
**  Get current thread ID.
**
**  INPUT:
**
**      Nothing.
**
**  OUTPUT:
**
**      gctUINT32_PTR ThreadID
**          Pointer to the variable that receives the thread ID.
*/
gceSTATUS
gckOS_GetThreadID(
    OUT gctUINT32_PTR ThreadID
    )
{
    /* Get thread ID. */
    if (ThreadID != gcvNULL)
    {
        *ThreadID = _GetThreadID();
    }

    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_SetGPUPower
**
**  Set the power of the GPU on or off.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to a gckOS object.
**
**      gctBOOL Clock
**          gcvTRUE to turn on the clock, or gcvFALSE to turn off the clock.
**
**      gctBOOL Power
**          gcvTRUE to turn on the power, or gcvFALSE to turn off the power.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_SetGPUPower(
    IN gckOS Os,
    IN gctBOOL Clock,
    IN gctBOOL Power
    )
{
    gcmkHEADER_ARG("Os=0x%X Clock=%d Power=%d", Os, Clock, Power);

    /* TODO: Put your code here. */

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*----------------------------------------------------------------------------*/
/*----- Profile --------------------------------------------------------------*/

gceSTATUS
gckOS_GetProfileTick(
    OUT gctUINT64_PTR Tick
    )
{
    struct timespec time;

    ktime_get_ts(&time);

    *Tick = time.tv_nsec + time.tv_sec * 1000000000ULL;

    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_QueryProfileTickRate(
    OUT gctUINT64_PTR TickRate
    )
{
    struct timespec res;

    hrtimer_get_res(CLOCK_MONOTONIC, &res);

    *TickRate = res.tv_nsec + res.tv_sec * 1000000000ULL;

    return gcvSTATUS_OK;
}

gctUINT32
gckOS_ProfileToMS(
    IN gctUINT64 Ticks
    )
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23)
    return div_u64(Ticks, 1000000);
#else
    gctUINT64 rem = Ticks;
    gctUINT64 b = 1000000;
    gctUINT64 res, d = 1;
    gctUINT32 high = rem >> 32;

    /* Reduce the thing a bit first */
    res = 0;
    if (high >= 1000000)
    {
        high /= 1000000;
        res   = (gctUINT64) high << 32;
        rem  -= (gctUINT64) (high * 1000000) << 32;
    }

    while (((gctINT64) b > 0) && (b < rem))
    {
        b <<= 1;
        d <<= 1;
    }

    do
    {
        if (rem >= b)
        {
            rem -= b;
            res += d;
        }

        b >>= 1;
        d >>= 1;
    }
    while (d);

    return (gctUINT32) res;
#endif
}

/******************************************************************************\
******************************* Signal Management ******************************
\******************************************************************************/

#undef _GC_OBJ_ZONE
#define _GC_OBJ_ZONE    gcvZONE_SIGNAL

/*******************************************************************************
**
**  gckOS_CreateSignal
**
**  Create a new signal.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctBOOL ManualReset
**          If set to gcvTRUE, gckOS_Signal with gcvFALSE must be called in
**          order to set the signal to nonsignaled state.
**          If set to gcvFALSE, the signal will automatically be set to
**          nonsignaled state by gckOS_WaitSignal function.
**
**  OUTPUT:
**
**      gctSIGNAL * Signal
**          Pointer to a variable receiving the created gctSIGNAL.
*/
gceSTATUS
gckOS_CreateSignal(
    IN gckOS Os,
    IN gctBOOL ManualReset,
    OUT gctSIGNAL * Signal
    )
{
    gcsSIGNAL_PTR signal;

    gcmkHEADER_ARG("Os=0x%X ManualReset=%d", Os, ManualReset);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    /* Create an event structure. */
    signal = (gcsSIGNAL_PTR) kmalloc(sizeof(gcsSIGNAL), GFP_KERNEL | __GFP_NOWARN);

    if (signal == gcvNULL)
    {
        gcmkFOOTER_ARG("status=%d", gcvSTATUS_OUT_OF_MEMORY);
        return gcvSTATUS_OUT_OF_MEMORY;
    }

    signal->manualReset = ManualReset;
    init_completion(&signal->obj);
    atomic_set(&signal->ref, 1);

    *Signal = (gctSIGNAL) signal;

    gcmkFOOTER_ARG("*Signal=0x%X", *Signal);
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_DestroySignal
**
**  Destroy a signal.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIGNAL Signal
**          Pointer to the gctSIGNAL.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_DestroySignal(
    IN gckOS Os,
    IN gctSIGNAL Signal
    )
{
    gcsSIGNAL_PTR signal;

    gcmkHEADER_ARG("Os=0x%X Signal=0x%X", Os, Signal);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    signal = (gcsSIGNAL_PTR) Signal;

    if (atomic_dec_and_test(&signal->ref))
    {
         /* Free the sgianl. */
        kfree(Signal);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckOS_Signal
**
**  Set a state of the specified signal.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIGNAL Signal
**          Pointer to the gctSIGNAL.
**
**      gctBOOL State
**          If gcvTRUE, the signal will be set to signaled state.
**          If gcvFALSE, the signal will be set to nonsignaled state.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_Signal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctBOOL State
    )
{
    gcsSIGNAL_PTR signal;

    gcmkHEADER_ARG("Os=0x%X Signal=0x%X State=%d", Os, Signal, State);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    signal = (gcsSIGNAL_PTR) Signal;

    if (State)
    {
        /* Set the event to a signaled state. */
        complete(&signal->obj);
    }
    else
    {
        /* Set the event to an unsignaled state. */
        INIT_COMPLETION(signal->obj);
    }

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

#if gcdENABLE_VG
gceSTATUS
gckOS_SetSignalVG(
    IN gckOS Os,
    IN gctHANDLE Process,
    IN gctSIGNAL Signal
    )
{
    gceSTATUS status;
    gctINT result;
    struct task_struct * userTask;
    struct siginfo info;

    userTask = FIND_TASK_BY_PID((pid_t) Process);

    if (userTask != gcvNULL)
    {
        info.si_signo = 48;
        info.si_code  = __SI_CODE(__SI_RT, SI_KERNEL);
        info.si_pid   = 0;
        info.si_uid   = 0;
        info.si_ptr   = (gctPOINTER) Signal;

        /* Signals with numbers between 32 and 63 are real-time,
           send a real-time signal to the user process. */
        result = send_sig_info(48, &info, userTask);

        printk("gckOS_SetSignalVG:0x%x\n", result);
        /* Error? */
        if (result < 0)
        {
            status = gcvSTATUS_GENERIC_IO;

            gcmkTRACE(
                gcvLEVEL_ERROR,
                "%s(%d): an error has occurred.\n",
                __FUNCTION__, __LINE__
                );
        }
        else
        {
            status = gcvSTATUS_OK;
        }
    }
    else
    {
        status = gcvSTATUS_GENERIC_IO;

        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): an error has occurred.\n",
            __FUNCTION__, __LINE__
            );
    }

    /* Return status. */
    return status;
}
#endif

/*******************************************************************************
**
**  gckOS_UserSignal
**
**  Set the specified signal which is owned by a process to signaled state.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIGNAL Signal
**          Pointer to the gctSIGNAL.
**
**      gctHANDLE Process
**          Handle of process owning the signal.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_UserSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctHANDLE Process
    )
{
    gceSTATUS status;
    gctSIGNAL signal;

    gcmkHEADER_ARG("Os=0x%X Signal=0x%X Process=%d",
                   Os, Signal, (gctINT32) Process);

    /* Map the signal into kernel space. */
    gcmkONERROR(gckOS_MapSignal(Os, Signal, Process, &signal));

    /* Signal. */
    status = gckOS_Signal(Os, signal, gcvTRUE);

    /* Unmap the signal */
    gcmkVERIFY_OK(gckOS_UnmapSignal(Os, Signal));

    gcmkFOOTER();
    return status;

OnError:
    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_WaitSignal
**
**  Wait for a signal to become signaled.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIGNAL Signal
**          Pointer to the gctSIGNAL.
**
**      gctUINT32 Wait
**          Number of milliseconds to wait.
**          Pass the value of gcvINFINITE for an infinite wait.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_WaitSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctUINT32 Wait
    )
{
    gceSTATUS status = gcvSTATUS_OK;
    gcsSIGNAL_PTR signal;

    gcmkHEADER_ARG("Os=0x%X Signal=0x%X Wait=0x%08X", Os, Signal, Wait);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    signal = (gcsSIGNAL_PTR) Signal;

    might_sleep();

    spin_lock_irq(&signal->obj.wait.lock);

    if (signal->obj.done)
    {
        if (!signal->manualReset)
        {
            signal->obj.done = 0;
        }

        status = gcvSTATUS_OK;
    }
    else if (Wait == 0)
    {
        status = gcvSTATUS_TIMEOUT;
    }
    else
    {
        /* Convert wait to milliseconds. */
#if gcdDETECT_TIMEOUT
        gctINT timeout = (Wait == gcvINFINITE)
            ? gcdINFINITE_TIMEOUT * HZ / 1000
            : Wait * HZ / 1000;

        gctUINT complained = 0;
#else
        gctINT timeout = (Wait == gcvINFINITE)
            ? MAX_SCHEDULE_TIMEOUT
            : Wait * HZ / 1000;
#endif

        DECLARE_WAITQUEUE(wait, current);
        wait.flags |= WQ_FLAG_EXCLUSIVE;
        __add_wait_queue_tail(&signal->obj.wait, &wait);

        while (gcvTRUE)
        {
            if (signal_pending(current))
            {
                /* Interrupt received. */
                status = gcvSTATUS_INTERRUPTED;
                break;
            }

            __set_current_state(TASK_INTERRUPTIBLE);
            spin_unlock_irq(&signal->obj.wait.lock);
            timeout = schedule_timeout(timeout);
            spin_lock_irq(&signal->obj.wait.lock);

            if (signal->obj.done)
            {
                if (!signal->manualReset)
                {
                    signal->obj.done = 0;
                }

                status = gcvSTATUS_OK;
                break;
            }

#if gcdDETECT_TIMEOUT
            if ((Wait == gcvINFINITE) && (timeout == 0))
            {
                gctUINT32 dmaAddress1, dmaAddress2;
                gctUINT32 dmaState1, dmaState2;

                dmaState1   = dmaState2   =
                dmaAddress1 = dmaAddress2 = 0;

                /* Verify whether DMA is running. */
                gcmkVERIFY_OK(_VerifyDMA(
                    Os, &dmaAddress1, &dmaAddress2, &dmaState1, &dmaState2
                    ));

#if gcdDETECT_DMA_ADDRESS
                /* Dump only if DMA appears stuck. */
                if (
                    (dmaAddress1 == dmaAddress2)
#if gcdDETECT_DMA_STATE
                 && (dmaState1   == dmaState2)
#endif
                )
#endif
                {
                    /* Increment complain count. */
                    complained += 1;

                    gcmkVERIFY_OK(_DumpGPUState(Os, gcvCORE_MAJOR));

                    gcmkPRINT(
                        "%s(%d): signal 0x%X; forced message flush (%d).",
                        __FUNCTION__, __LINE__, Signal, complained
                        );

                    /* Flush the debug cache. */
                    gcmkDEBUGFLUSH(dmaAddress2);
                }

                /* Reset timeout. */
                timeout = gcdINFINITE_TIMEOUT * HZ / 1000;
            }
#endif

            if (timeout == 0)
            {

                status = gcvSTATUS_TIMEOUT;
                break;
            }
        }

        __remove_wait_queue(&signal->obj.wait, &wait);

#if gcdDETECT_TIMEOUT
        if (complained)
        {
            gcmkPRINT(
                "%s(%d): signal=0x%X; waiting done; status=%d",
                __FUNCTION__, __LINE__, Signal, status
                );
        }
#endif
    }

    spin_unlock_irq(&signal->obj.wait.lock);

    /* Return status. */
    gcmkFOOTER_ARG("Signal=0x%X status=%d", Signal, status);
    return status;
}

/*******************************************************************************
**
**  gckOS_MapSignal
**
**  Map a signal in to the current process space.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctSIGNAL Signal
**          Pointer to tha gctSIGNAL to map.
**
**      gctHANDLE Process
**          Handle of process owning the signal.
**
**  OUTPUT:
**
**      gctSIGNAL * MappedSignal
**          Pointer to a variable receiving the mapped gctSIGNAL.
*/
gceSTATUS
gckOS_MapSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal,
    IN gctHANDLE Process,
    OUT gctSIGNAL * MappedSignal
    )
{
    gctINT signalID;
    gcsSIGNAL_PTR signal;
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%X Signal=0x%X Process=0x%X", Os, Signal, Process);

    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);
    gcmkVERIFY_ARGUMENT(MappedSignal != gcvNULL);

    signalID = (gctINT) Signal - 1;

    gcmkONERROR(gckOS_AcquireMutex(Os, Os->signal.lock, gcvINFINITE));
    acquired = gcvTRUE;

    if (signalID >= 0 && signalID < Os->signal.tableLen)
    {
        /* It is a user space signal. */
        signal = Os->signal.table[signalID];

        if (signal == gcvNULL)
        {
            gcmkONERROR(gcvSTATUS_NOT_FOUND);
        }
    }
    else
    {
        /* It is a kernel space signal structure. */
        signal = (gcsSIGNAL_PTR) Signal;
    }

    if (atomic_inc_return(&signal->ref) <= 1)
    {
        /* The previous value is 0, it has been deleted. */
        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    /* Release the mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Os, Os->signal.lock));

    *MappedSignal = (gctSIGNAL) signal;

    /* Success. */
    gcmkFOOTER_ARG("*MappedSignal=0x%X", *MappedSignal);
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->signal.lock));
    }

    /* Return the staus. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**	gckOS_UnmapSignal
**
**	Unmap a signal .
**
**	INPUT:
**
**		gckOS Os
**			Pointer to an gckOS object.
**
**		gctSIGNAL Signal
**			Pointer to that gctSIGNAL mapped.
*/
gceSTATUS
gckOS_UnmapSignal(
    IN gckOS Os,
    IN gctSIGNAL Signal
    )
{
    gctINT signalID;
    gcsSIGNAL_PTR signal;
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%X Signal=0x%X ", Os, Signal);

    gcmkVERIFY_ARGUMENT(Signal != gcvNULL);

    signalID = (gctINT) Signal - 1;

    gcmkONERROR(gckOS_AcquireMutex(Os, Os->signal.lock, gcvINFINITE));
    acquired = gcvTRUE;

    if (signalID >= 0 && signalID < Os->signal.tableLen)
    {
        /* It is a user space signal. */
        signal = Os->signal.table[signalID];

        if (signal == gcvNULL)
        {
            gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }

        if (atomic_read(&signal->ref) == 1)
        {
            /* Update the table. */
            Os->signal.table[signalID] = gcvNULL;

            if (Os->signal.unused++ == 0)
            {
                Os->signal.currentID = signalID;
            }
        }

        gcmkONERROR(gckOS_DestroySignal(Os, signal));
    }
    else
    {
        /* It is a kernel space signal structure. */
        signal = (gcsSIGNAL_PTR) Signal;

        gcmkONERROR(gckOS_DestroySignal(Os, signal));
    }

    /* Release the mutex. */
    gcmkONERROR(gckOS_ReleaseMutex(Os, Os->signal.lock));

    /* Success. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->signal.lock));
    }

    /* Return the staus. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_CreateUserSignal
**
**  Create a new signal to be used in the user space.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctBOOL ManualReset
**          If set to gcvTRUE, gckOS_Signal with gcvFALSE must be called in
**          order to set the signal to nonsignaled state.
**          If set to gcvFALSE, the signal will automatically be set to
**          nonsignaled state by gckOS_WaitSignal function.
**
**  OUTPUT:
**
**      gctINT * SignalID
**          Pointer to a variable receiving the created signal's ID.
*/
gceSTATUS
gckOS_CreateUserSignal(
    IN gckOS Os,
    IN gctBOOL ManualReset,
    OUT gctINT * SignalID
    )
{
    gcsSIGNAL_PTR signal = gcvNULL;
    gctINT unused, currentID, tableLen;
    gctPOINTER * table;
    gctINT i;
    gceSTATUS status;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%0x ManualReset=%d", Os, ManualReset);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(SignalID != gcvNULL);

    /* Lock the table. */
    gcmkONERROR(gckOS_AcquireMutex(Os, Os->signal.lock, gcvINFINITE));

    acquired = gcvTRUE;

    if (Os->signal.unused < 1)
    {
        /* Enlarge the table. */
        table = (gctPOINTER *) kmalloc(
                    sizeof(gctPOINTER) * (Os->signal.tableLen + USER_SIGNAL_TABLE_LEN_INIT),
                    GFP_KERNEL | __GFP_NOWARN);

        if (table == gcvNULL)
        {
            /* Out of memory. */
            gcmkONERROR(gcvSTATUS_OUT_OF_MEMORY);
        }

        memset(table + Os->signal.tableLen, 0, sizeof(gctPOINTER) * USER_SIGNAL_TABLE_LEN_INIT);
        memcpy(table, Os->signal.table, sizeof(gctPOINTER) * Os->signal.tableLen);

        /* Release the old table. */
        kfree(Os->signal.table);

        /* Update the table. */
        Os->signal.table = table;
        Os->signal.currentID = Os->signal.tableLen;
        Os->signal.tableLen += USER_SIGNAL_TABLE_LEN_INIT;
        Os->signal.unused += USER_SIGNAL_TABLE_LEN_INIT;
    }

    table = Os->signal.table;
    currentID = Os->signal.currentID;
    tableLen = Os->signal.tableLen;
    unused = Os->signal.unused;

    /* Create a new signal. */
    gcmkONERROR(
        gckOS_CreateSignal(Os, ManualReset, (gctSIGNAL *) &signal));

    /* Save the process ID. */
    signal->process = (gctHANDLE) _GetProcessID();

    table[currentID] = signal;

    /* Plus 1 to avoid gcvNULL claims. */
    *SignalID = currentID + 1;

    /* Update the currentID. */
    if (--unused > 0)
    {
        for (i = 0; i < tableLen; i++)
        {
            if (++currentID >= tableLen)
            {
                /* Wrap to the begin. */
                currentID = 0;
            }

            if (table[currentID] == gcvNULL)
            {
                break;
            }
        }
    }

    Os->signal.table = table;
    Os->signal.currentID = currentID;
    Os->signal.tableLen = tableLen;
    Os->signal.unused = unused;

    gcmkONERROR(
        gckOS_ReleaseMutex(Os, Os->signal.lock));

    gcmkFOOTER_ARG("*SignalID=%d", gcmOPT_VALUE(SignalID));
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkONERROR(
            gckOS_ReleaseMutex(Os, Os->signal.lock));
    }

    /* Return the staus. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_DestroyUserSignal
**
**  Destroy a signal to be used in the user space.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctINT SignalID
**          The signal's ID.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_DestroyUserSignal(
    IN gckOS Os,
    IN gctINT SignalID
    )
{
    gceSTATUS status;
    gcsSIGNAL_PTR signal;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%X SignalID=%d", Os, SignalID);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    gcmkONERROR(
        gckOS_AcquireMutex(Os, Os->signal.lock, gcvINFINITE));

    acquired = gcvTRUE;

    if (SignalID < 1 || SignalID > Os->signal.tableLen)
    {
        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): invalid signal->%d.",
            __FUNCTION__, __LINE__,
            (gctINT) SignalID
            );

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    SignalID -= 1;

    signal = Os->signal.table[SignalID];

    if (signal == gcvNULL)
    {
        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): signal is gcvNULL.",
            __FUNCTION__, __LINE__
            );

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }


    if (atomic_read(&signal->ref) == 1)
    {
        /* Update the table. */
        Os->signal.table[SignalID] = gcvNULL;

        if (Os->signal.unused++ == 0)
        {
            Os->signal.currentID = SignalID;
        }
    }

    gcmkONERROR(
        gckOS_DestroySignal(Os, signal));

    gcmkVERIFY_OK(
        gckOS_ReleaseMutex(Os, Os->signal.lock));

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(
            gckOS_ReleaseMutex(Os, Os->signal.lock));
    }

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_WaitUserSignal
**
**  Wait for a signal used in the user mode to become signaled.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctINT SignalID
**          Signal ID.
**
**      gctUINT32 Wait
**          Number of milliseconds to wait.
**          Pass the value of gcvINFINITE for an infinite wait.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_WaitUserSignal(
    IN gckOS Os,
    IN gctINT SignalID,
    IN gctUINT32 Wait
    )
{
    gceSTATUS status;
    gcsSIGNAL_PTR signal;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%X SignalID=%d Wait=%u", Os, SignalID, Wait);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    gcmkONERROR(gckOS_AcquireMutex(Os, Os->signal.lock, gcvINFINITE));
    acquired = gcvTRUE;

    if (SignalID < 1 || SignalID > Os->signal.tableLen)
    {
        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): invalid signal %d",
            __FUNCTION__, __LINE__,
            SignalID
            );

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    SignalID -= 1;

    signal = Os->signal.table[SignalID];

    gcmkONERROR(gckOS_ReleaseMutex(Os, Os->signal.lock));
    acquired = gcvFALSE;

    if (signal == gcvNULL)
    {
        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): signal is gcvNULL.",
            __FUNCTION__, __LINE__
            );

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }


    status = gckOS_WaitSignal(Os, signal, Wait);

    /* Return the status. */
    gcmkFOOTER();
    return status;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Os, Os->signal.lock));
    }

    /* Return the staus. */
    gcmkFOOTER();
    return status;
}

/*******************************************************************************
**
**  gckOS_SignalUserSignal
**
**  Set a state of the specified signal to be used in the user space.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to an gckOS object.
**
**      gctINT SignalID
**          SignalID.
**
**      gctBOOL State
**          If gcvTRUE, the signal will be set to signaled state.
**          If gcvFALSE, the signal will be set to nonsignaled state.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_SignalUserSignal(
    IN gckOS Os,
    IN gctINT SignalID,
    IN gctBOOL State
    )
{
    gceSTATUS status;
    gcsSIGNAL_PTR signal;
    gctBOOL acquired = gcvFALSE;

    gcmkHEADER_ARG("Os=0x%X SignalID=%d State=%d", Os, SignalID, State);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    gcmkONERROR(gckOS_AcquireMutex(Os, Os->signal.lock, gcvINFINITE));
    acquired = gcvTRUE;

    if ((SignalID < 1)
    ||  (SignalID > Os->signal.tableLen)
    )
    {
        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): invalid signal->%d.",
            __FUNCTION__, __LINE__,
            SignalID
            );

        gcmkONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    SignalID -= 1;

    signal = Os->signal.table[SignalID];

    gcmkONERROR(gckOS_ReleaseMutex(Os, Os->signal.lock));
    acquired = gcvFALSE;

    if (signal == gcvNULL)
    {
        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): signal is gcvNULL.",
            __FUNCTION__, __LINE__
            );

        gcmkONERROR(gcvSTATUS_INVALID_REQUEST);
    }


    status = gckOS_Signal(Os, signal, State);

    /* Success. */
    gcmkFOOTER();
    return status;

OnError:
    if (acquired)
    {
        /* Release the mutex. */
        gcmkVERIFY_OK(
            gckOS_ReleaseMutex(Os, Os->signal.lock));
    }

    /* Return the staus. */
    gcmkFOOTER();
    return status;
}

gceSTATUS
gckOS_CleanProcessSignal(
    gckOS Os,
    gctHANDLE Process
    )
{
    gctINT signal;

    gcmkHEADER_ARG("Os=0x%X Process=%d", Os, Process);

    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    gcmkVERIFY_OK(gckOS_AcquireMutex(Os,
        Os->signal.lock,
        gcvINFINITE
        ));

    if (Os->signal.unused == Os->signal.tableLen)
    {
        gcmkVERIFY_OK(gckOS_ReleaseMutex(Os,
            Os->signal.lock
            ));

        gcmkFOOTER_NO();
        return gcvSTATUS_OK;
    }

    for (signal = 0; signal < Os->signal.tableLen; signal++)
    {
        if (Os->signal.table[signal] != gcvNULL &&
            ((gcsSIGNAL_PTR)Os->signal.table[signal])->process == Process)
        {
            gckOS_DestroySignal(Os, Os->signal.table[signal]);

            /* Update the signal table. */
            Os->signal.table[signal] = gcvNULL;
            if (Os->signal.unused++ == 0)
            {
                Os->signal.currentID = signal;
            }
        }
    }

    gcmkVERIFY_OK(gckOS_ReleaseMutex(Os,
        Os->signal.lock
        ));

    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

#if gcdENABLE_VG
gceSTATUS
gckOS_CreateSemaphoreVG(
    IN gckOS Os,
    OUT gctSEMAPHORE * Semaphore
    )
{
    gceSTATUS status;
    struct semaphore * newSemaphore;

    gcmkHEADER_ARG("Os=0x%X Semaphore=0x%x", Os, Semaphore);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    do
    {
        /* Allocate the semaphore structure. */
        newSemaphore = (struct semaphore *)kmalloc(gcmSIZEOF(struct semaphore), GFP_KERNEL | __GFP_NOWARN);
        if (newSemaphore == gcvNULL)
        {
            gcmkERR_BREAK(gcvSTATUS_OUT_OF_MEMORY);
        }

        /* Initialize the semaphore. */
        sema_init(newSemaphore, 0);

        /* Set the handle. */
        * Semaphore = (gctSEMAPHORE) newSemaphore;

        /* Success. */
        status = gcvSTATUS_OK;
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return the status. */
    return status;
}


gceSTATUS
gckOS_IncrementSemaphore(
    IN gckOS Os,
    IN gctSEMAPHORE Semaphore
    )
{
    gcmkHEADER_ARG("Os=0x%X Semaphore=0x%x", Os, Semaphore);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    /* Increment the semaphore's count. */
    up((struct semaphore *) Semaphore);

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_DecrementSemaphore(
    IN gckOS Os,
    IN gctSEMAPHORE Semaphore
    )
{
    gceSTATUS status;
    gctINT result;

    gcmkHEADER_ARG("Os=0x%X Semaphore=0x%x", Os, Semaphore);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Semaphore != gcvNULL);

    do
    {
        /* Decrement the semaphore's count. If the count is zero, wait
           until it gets incremented. */
        result = down_interruptible((struct semaphore *) Semaphore);

        /* Signal received? */
        if (result != 0)
        {
            status = gcvSTATUS_TERMINATE;
            break;
        }

        /* Success. */
        status = gcvSTATUS_OK;
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return the status. */
    return status;
}

/*******************************************************************************
**
**  gckOS_SetSignal
**
**  Set the specified signal to signaled state.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gctHANDLE Process
**          Handle of process owning the signal.
**
**      gctSIGNAL Signal
**          Pointer to the gctSIGNAL.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_SetSignal(
    IN gckOS Os,
    IN gctHANDLE Process,
    IN gctSIGNAL Signal
    )
{
    gceSTATUS status;
    gctINT result;
    struct task_struct * userTask;
    struct siginfo info;

    userTask = FIND_TASK_BY_PID((pid_t) Process);

    if (userTask != gcvNULL)
    {
        info.si_signo = 48;
        info.si_code  = __SI_CODE(__SI_RT, SI_KERNEL);
        info.si_pid   = 0;
        info.si_uid   = 0;
        info.si_ptr   = (gctPOINTER) Signal;

        /* Signals with numbers between 32 and 63 are real-time,
           send a real-time signal to the user process. */
        result = send_sig_info(48, &info, userTask);

        /* Error? */
        if (result < 0)
        {
            status = gcvSTATUS_GENERIC_IO;

            gcmkTRACE(
                gcvLEVEL_ERROR,
                "%s(%d): an error has occurred.\n",
                __FUNCTION__, __LINE__
                );
        }
        else
        {
            status = gcvSTATUS_OK;
        }
    }
    else
    {
        status = gcvSTATUS_GENERIC_IO;

        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): an error has occurred.\n",
            __FUNCTION__, __LINE__
            );
    }

    /* Return status. */
    return status;
}

/******************************************************************************\
******************************** Thread Object *********************************
\******************************************************************************/

gceSTATUS
gckOS_StartThread(
    IN gckOS Os,
    IN gctTHREADFUNC ThreadFunction,
    IN gctPOINTER ThreadParameter,
    OUT gctTHREAD * Thread
    )
{
    gceSTATUS status;
    struct task_struct * thread;

    gcmkHEADER_ARG("Os=0x%X ", Os);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(ThreadFunction != gcvNULL);
    gcmkVERIFY_ARGUMENT(Thread != gcvNULL);

    do
    {
        /* Create the thread. */
        thread = kthread_create(
            ThreadFunction,
            ThreadParameter,
            "Vivante Kernel Thread"
            );

        /* Failed? */
        if (IS_ERR(thread))
        {
            status = gcvSTATUS_GENERIC_IO;
            break;
        }

        /* Start the thread. */
        wake_up_process(thread);

        /* Set the thread handle. */
        * Thread = (gctTHREAD) thread;

        /* Success. */
        status = gcvSTATUS_OK;
    }
    while (gcvFALSE);

    gcmkFOOTER();
    /* Return the status. */
    return status;
}

gceSTATUS
gckOS_StopThread(
    IN gckOS Os,
    IN gctTHREAD Thread
    )
{
    gcmkHEADER_ARG("Os=0x%X Thread=0x%x", Os, Thread);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Thread != gcvNULL);

    /* Thread should have already been enabled to terminate. */
    kthread_stop((struct task_struct *) Thread);

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

gceSTATUS
gckOS_VerifyThread(
    IN gckOS Os,
    IN gctTHREAD Thread
    )
{
    gcmkHEADER_ARG("Os=0x%X Thread=0x%x", Os, Thread);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Thread != gcvNULL);

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}
#endif

/*******************************************************************************
**
**  gckOS_DumpGPUState
**
**  Dump GPU state.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to the gckOS object.
**
**      gceCORE Core
**          The core type of kernel.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckOS_DumpGPUState(
    IN gckOS Os,
    IN gceCORE Core
    )
{
    gcmkHEADER_ARG("Os=0x%X Core=%d", Os, Core);
    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);

    _DumpGPUState(Os, Core);

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}
