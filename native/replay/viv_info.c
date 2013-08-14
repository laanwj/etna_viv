/* Get info about vivante device */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include "gc_hal_base.h"
#include "gc_hal.h"
#include "gc_hal_driver.h"
#include "gc_abi.h"

const char *galcore_device[] = {"/dev/galcore", "/dev/graphics/galcore", NULL};

const char *vivante_chipFeatures[32] = {
    /*0 */ "FAST_CLEAR",
    /*1 */ "SPECIAL_ANTI_ALIASING",
    /*2 */ "PIPE_3D",
    /*3 */ "DXT_TEXTURE_COMPRESSION",
    /*4 */ "DEBUG_MODE",
    /*5 */ "Z_COMPRESSION",
    /*6 */ "YUV420_SCALER",
    /*7 */ "MSAA",
    /*8 */ "DC",
    /*9 */ "PIPE_2D",
    /*10*/ "ETC1_TEXTURE_COMPRESSION",
    /*11*/ "FAST_SCALER",
    /*12*/ "HIGH_DYNAMIC_RANGE",
    /*13*/ "YUV420_TILER",
    /*14*/ "MODULE_CG",
    /*15*/ "MIN_AREA",
    /*16*/ "NO_EARLY_Z",
    /*17*/ "NO_422_TEXTURE",
    /*18*/ "BUFFER_INTERLEAVING",
    /*19*/ "BYTE_WRITE_2D",
    /*20*/ "NO_SCALER",
    /*21*/ "YUY2_AVERAGING",
    /*22*/ "HALF_PE_CACHE",
    /*23*/ "HALF_TX_CACHE",
    /*24*/ "YUY2_RENDER_TARGET",
    /*25*/ "MEM32",
    /*26*/ "PIPE_VG",
    /*27*/ "VGTS",
    /*28*/ "FE20",
    /*29*/ "BYTE_WRITE_3D",
    /*30*/ "RS_YUV_TARGET",
    /*31*/ "32_BIT_INDICES"
};

const char *vivante_chipMinorFeatures0[32] = {
    /*0 */ "FLIP_Y",
    /*1 */ "DUAL_RETURN_BUS",
    /*2 */ "ENDIANNESS_CONFIG",
    /*3 */ "TEXTURE_8K",
    /*4 */ "CORRECT_TEXTURE_CONVERTER",
    /*5 */ "SPECIAL_MSAA_LOD",
    /*6 */ "FAST_CLEAR_FLUSH",
    /*7 */ "2DPE20",
    /*8 */ "CORRECT_AUTO_DISABLE",
    /*9 */ "RENDERTARGET_8K",
    /*10*/ "2BITPERTILE",
    /*11*/ "SEPARATE_TILE_STATUS_WHEN_INTERLEAVED",
    /*12*/ "SUPER_TILED",
    /*13*/ "VG_20",
    /*14*/ "TS_EXTENDED_COMMANDS",
    /*15*/ "COMPRESSION_FIFO_FIXED",
    /*16*/ "HAS_SIGN_FLOOR_CEIL",
    /*17*/ "VG_FILTER",
    /*18*/ "VG_21",
    /*19*/ "SHADER_HAS_W",
    /*20*/ "HAS_SQRT_TRIG",
    /*21*/ "MORE_MINOR_FEATURES",
    /*22*/ "MC20",
    /*23*/ "MSAA_SIDEBAND",
    /*24*/ "BUG_FIXES0",
    /*25*/ "VAA",
    /*26*/ "BYPASS_IN_MSAA",
    /*27*/ "HZ",
    /*28*/ "NEW_TEXTURE",
    /*29*/ "2D_A8_TARGET",
    /*30*/ "CORRECT_STENCIL",
    /*31*/ "ENHANCE_VR"
};

const char *vivante_chipMinorFeatures1[32] = {
    /*0 */ "RSUV_SWIZZLE",
    /*1 */ "V2_COMPRESSION",
    /*2 */ "VG_DOUBLE_BUFFER",
    /*3 */ "EXTRA_EVENT_STATES",
    /*4 */ "NO_STRIPING_NEEDED",
    /*5 */ "TEXTURE_STRIDE",
    /*6 */ "BUG_FIXES3",
    /*7 */ "AUTO_DISABLE",
    /*8 */ "AUTO_RESTART_TS",
    /*9 */ "DISABLE_PE_GATING",
    /*10*/ "L2_WINDOWING",
    /*11*/ "HALF_FLOAT",
    /*12*/ "PIXEL_DITHER",
    /*13*/ "TWO_STENCIL_REFERENCE",
    /*14*/ "EXTENDED_PIXEL_FORMAT",
    /*15*/ "CORRECT_MIN_MAX_DEPTH",
    /*16*/ "2D_DITHER",
    /*17*/ "BUG_FIXES5",
    /*18*/ "NEW_2D",
    /*19*/ "NEW_FP",
    /*20*/ "TEXTURE_ALIGN_4",
    /*21*/ "NON_POWER_OF_TWO",
    /*22*/ "LINEAR_TEXTURE_SUPPORT",
    /*23*/ "HALTI0",
    /*24*/ "CORRECT_OVERFLOW_VG",
    /*25*/ "NEGATIVE_LOG_FIX",
    /*26*/ "RESOLVE_OFFSET",
    /*27*/ "OK_TO_GATE_AXI_CLOCK",
    /*28*/ "MMU_VERSION",
    /*29*/ "WIDE_LINE",
    /*30*/ "UBUG_FIXES6",
    /*31*/ "FC_FLUSH_STALL"
};

const char *vivante_chipMinorFeatures2[32] = {
    /*0 */ "LINE_LOOP",
    /*1 */ "LOGIC_OP",
    /*2 */ "UNKNOWN_2",
    /*3 */ "SUPERTILED_TEXTURE",
    /*4 */ "UNKNOWN_4",
    /*5 */ "RECT_PRIMITIVE",
    /*6 */ "COMPOSITION",
    /*7 */ "CORRECT_AUTO_DISABLE_COUNT",
    /*8 */ "UNKNOWN_8",
    /*9 */ "UNKNOWN_9",
    /*10*/ "UNKNOWN_10",
    /*11*/ "SAMPLERBASE_16",
    /*12*/ "UNKNOWN_12",
    /*13*/ "UNKNOWN_13",
    /*14*/ "UNKNOWN_14",
    /*15*/ "EXTRA_TEXTURE_STATE",
    /*16*/ "FULL_DIRECTFB",
    /*17*/ "2D_OPF",
    /*18*/ "THREAD_WALKER_IN_PS",
    /*19*/ "TILE_FILLER",
    /*20*/ "UNKNOWN_20",
    /*21*/ "2D_MULTI_SOURCE_BLIT",
    /*22*/ "UNKNOWN_22",
    /*23*/ "UNKNOWN_23",
    /*24*/ "UNKNOWN_24",
    /*25*/ "MIXED_STREAMS",
    /*26*/ "2D_420_L2CACHE",
    /*27*/ "UNKNOWN_27",
    /*28*/ "2D_NO_INDEX8_BRUSH",
    /*29*/ "TEXTURE_TILED_READ",
    /*30*/ "UNKNOWN_30",
    /*31*/ "UNKNOWN_31"
};

#if 0
const char *vivante_power_state(gceCHIPPOWERSTATE state)
{
    switch(state)
    {
    case gcvPOWER_ON: return "ON";
    case gcvPOWER_OFF: return "OFF";
    case gcvPOWER_IDLE: return "IDLE";
    case gcvPOWER_SUSPEND: return "SUSPEND";
#ifndef GCABI_dove_old
#ifndef GCABI_dove
    case gcvPOWER_ON_BROADCAST: return "ON_BROADCAST";
#endif
    case gcvPOWER_SUSPEND_ATPOWERON: return "SUSPEND_ATPOWERON";
    case gcvPOWER_OFF_ATPOWERON: return "OFF_ATPOWERON";
    case gcvPOWER_IDLE_BROADCAST: return "IDLE_BROADCAST";
    case gcvPOWER_SUSPEND_BROADCAST: return "SUSPEND_BROADCAST";
    case gcvPOWER_OFF_BROADCAST: return "OFF_BROADCAST";
    case gcvPOWER_OFF_RECOVERY: return "OFF_RECOVERY";
#endif
    default:
        return "UNKNOWN";
    }
}
#endif

int main()
{
    gcsHAL_INTERFACE id = {};
    vivante_ioctl_data_t ic = {};
    int fd = -1;
    int rv = 0;

    for(int idx=0; fd == -1 && galcore_device[idx]; ++idx)
    {
        fd = open(galcore_device[idx], O_RDWR);
        if(fd >= 0)
        {
            printf("Succesfully opened %s\n", galcore_device[idx]);
        }
    }
    if(fd < 0)
    {
        perror("Cannot open device");
        exit(1);
    }
#ifdef GCABI_HAS_HARDWARE_TYPE
    for(int hwtype=1; hwtype<8; hwtype<<=1) 
    {
        printf("********** core: %i ***********\n", hwtype);
#endif
    memset((void*)&id, 0, sizeof(id));
    id.command = gcvHAL_QUERY_CHIP_IDENTITY;
#ifdef GCABI_HAS_HARDWARE_TYPE
    id.hardwareType = hwtype;
#endif

    ic.in_buf = &id;
    ic.in_buf_size = sizeof(id);
    ic.out_buf = &id;
    ic.out_buf_size = sizeof(id);
    printf("gcsHAL_INTERFACE size %i\n", sizeof(id));

    rv = ioctl(fd, IOCTL_GCHAL_INTERFACE, &ic);
    if(rv < 0)
    {
        perror("Ioctl error");
        exit(1);
    }
#ifdef GCABI_HAS_HARDWARE_TYPE
    if(id.status != 0) /* no such core */
        continue;
#endif
    
    printf("* Chip identity:\n");
    printf("Chip model: %08x\n", id.u.QueryChipIdentity.chipModel);
    printf("Chip revision: %08x\n", id.u.QueryChipIdentity.chipRevision);
    printf("Chip features: 0x%08x\n", id.u.QueryChipIdentity.chipFeatures);
    for(int i=0; i<32; ++i)
    {
        bool flag = id.u.QueryChipIdentity.chipFeatures & (1 << i);
        printf("  %c %s\n", flag?'+':'-', vivante_chipFeatures[i]);
    }
    printf("\n");
    printf("Chip minor features 0: 0x%08x\n", id.u.QueryChipIdentity.chipMinorFeatures);
    for(int i=0; i<32; ++i)
    {
        bool flag = id.u.QueryChipIdentity.chipMinorFeatures & (1 << i);
        printf("  %c %s\n", flag?'+':'-', vivante_chipMinorFeatures0[i]);
    }
    printf("\n");
    printf("Chip minor features 1: 0x%08x\n", id.u.QueryChipIdentity.chipMinorFeatures1);
    for(int i=0; i<32; ++i)
    {
        bool flag = id.u.QueryChipIdentity.chipMinorFeatures1 & (1 << i);
        printf("  %c %s\n", flag?'+':'-', vivante_chipMinorFeatures1[i]);
    }
    printf("\n");
#ifdef GCABI_HAS_MINOR_FEATURES_2
    printf("Chip minor features 2: 0x%08x\n", id.u.QueryChipIdentity.chipMinorFeatures2);
    for(int i=0; i<32; ++i)
    {
        bool flag = id.u.QueryChipIdentity.chipMinorFeatures2 & (1 << i);
        printf("  %c %s\n", flag?'+':'-', vivante_chipMinorFeatures2[i]);
    }
#endif
    printf("\n");
#ifdef GCABI_HAS_MINOR_FEATURES_3
    printf("Chip minor features 3: 0x%08x\n", id.u.QueryChipIdentity.chipMinorFeatures3);
#endif
    printf("Stream count: 0x%08x\n", id.u.QueryChipIdentity.streamCount);
    printf("Register max: 0x%08x\n", id.u.QueryChipIdentity.registerMax);
    printf("Thread count: 0x%08x\n", id.u.QueryChipIdentity.threadCount);
    printf("Shader core count: 0x%08x\n", id.u.QueryChipIdentity.shaderCoreCount);
    printf("Vertex cache size: 0x%08x\n", id.u.QueryChipIdentity.vertexCacheSize);
    printf("Vertex output buffer size: 0x%08x\n", id.u.QueryChipIdentity.vertexOutputBufferSize);
#ifdef GCABI_CHIPIDENTITY_EXT
    printf("Pixel pipes: 0x%08x\n", id.u.QueryChipIdentity.pixelPipes);
    printf("Instruction count: 0x%08x\n", id.u.QueryChipIdentity.instructionCount);
    printf("Num constants: 0x%08x\n", id.u.QueryChipIdentity.numConstants);
    printf("Buffer size: 0x%08x\n", id.u.QueryChipIdentity.bufferSize);
#endif
#ifdef GCABI_CHIPIDENTITY_VARYINGS
    printf("Number of varyings: 0x%08x\n", id.u.QueryChipIdentity.varyingsCount);
    printf("Supertile layout style in hardware: 0x%08x\n", id.u.QueryChipIdentity.superTileMode);
#endif
    
    printf("\n");

    memset((void*)&id, 0, sizeof(id));
    id.command = gcvHAL_QUERY_VIDEO_MEMORY;
#ifdef GCABI_HAS_HARDWARE_TYPE
    id.hardwareType = hwtype;
#endif

    rv = ioctl(fd, IOCTL_GCHAL_INTERFACE, &ic);
    if(rv < 0)
    {
        perror("Ioctl error");
        exit(1);
    }

    printf("* Video memory:\n");
    printf("Internal physical: 0x%08x\n", (unsigned)id.u.QueryVideoMemory.internalPhysical);
    printf("Internal size: 0x%08x\n", (unsigned)id.u.QueryVideoMemory.internalSize);
    printf("External physical: %08x\n", (unsigned)id.u.QueryVideoMemory.externalPhysical);
    printf("External size: 0x%08x\n", (unsigned)id.u.QueryVideoMemory.externalSize);
    printf("Contiguous physical: 0x%08x\n", (unsigned)id.u.QueryVideoMemory.contiguousPhysical);
    printf("Contiguous size: 0x%08x\n", (unsigned)id.u.QueryVideoMemory.contiguousSize);
    printf("\n");

#if 0 
    memset((void*)&id, 0, sizeof(id));
    id.command = gcvHAL_QUERY_POWER_MANAGEMENT_STATE;
#ifdef GCABI_HAS_HARDWARE_TYPE
    id.hardwareType = hwtype;
#endif
    rv = ioctl(fd, IOCTL_GCHAL_INTERFACE, &ic);
    if(rv < 0)
    {
        perror("Ioctl error");
        exit(1);
    }
    printf("* Power management:\n");
    printf("Power state: %s\n", vivante_power_state(id.u.QueryPowerManagement.state));
    printf("Is idle: %s\n", id.u.QueryPowerManagement.isIdle?"yes":"no");
    printf("\n");
#endif
    memset((void*)&id, 0, sizeof(id));
    id.command = gcvHAL_QUERY_KERNEL_SETTINGS;
#ifdef GCABI_HAS_HARDWARE_TYPE
    id.hardwareType = hwtype;
#endif
    rv = ioctl(fd, IOCTL_GCHAL_INTERFACE, &ic);
    if(rv < 0)
    {
        perror("Ioctl error");
        exit(1);
    }
    
    printf("* Kernel settings\n");
    printf("Use realtime signal: %08x\n", id.u.QueryKernelSettings.settings.signal);
    printf("\n");

    id.command = gcvHAL_GET_BASE_ADDRESS;
#ifdef GCABI_HAS_HARDWARE_TYPE
    id.hardwareType = hwtype;
#endif
    memset((void*)&id, 0, sizeof(id));
    rv = ioctl(fd, IOCTL_GCHAL_INTERFACE, &ic);
    if(rv < 0)
    {
        perror("Ioctl error");
        exit(1);
    }
    printf("* Base address\n");
    printf("Physical address of internal memory: %08x\n", id.u.GetBaseAddress.baseAddress);
    printf("\n");

#ifdef GCABI_HAS_HARDWARE_TYPE
    }
#endif

    close(fd);


    return 0;
}

