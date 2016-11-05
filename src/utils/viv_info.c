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

#include "gc_abi.h"

const char *galcore_device[] = {"/dev/gal3d", "/dev/galcore", "/dev/graphics/galcore", NULL};
// Unused, just to pull it into the DWARF info
struct _gcoCMDBUF tmp;

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
    id.hardwareType = (gceHARDWARE_TYPE)hwtype;
#endif

    ic.in_buf = (long long unsigned int)&id;
    ic.in_buf_size = sizeof(id);
    ic.out_buf = (long long unsigned int)&id;
    ic.out_buf_size = sizeof(id);

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
    printf("Chip minor features 0: 0x%08x\n", id.u.QueryChipIdentity.chipMinorFeatures);
    printf("Chip minor features 1: 0x%08x\n", id.u.QueryChipIdentity.chipMinorFeatures1);
#ifdef GCABI_HAS_MINOR_FEATURES_2
    printf("Chip minor features 2: 0x%08x\n", id.u.QueryChipIdentity.chipMinorFeatures2);
#endif
#ifdef GCABI_HAS_MINOR_FEATURES_3
    printf("Chip minor features 3: 0x%08x\n", id.u.QueryChipIdentity.chipMinorFeatures3);
#endif
#ifdef GCABI_HAS_MINOR_FEATURES_4
    printf("Chip minor features 4: 0x%08x\n", id.u.QueryChipIdentity.chipMinorFeatures4);
#endif
#ifdef GCABI_HAS_MINOR_FEATURES_5
    printf("Chip minor features 5: 0x%08x\n", id.u.QueryChipIdentity.chipMinorFeatures5);
#endif
#ifdef GCABI_HAS_MINOR_FEATURES_6
    printf("Chip minor features 6: 0x%08x\n", id.u.QueryChipIdentity.chipMinorFeatures6);
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
    id.hardwareType = (gceHARDWARE_TYPE)hwtype;
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

    memset((void*)&id, 0, sizeof(id));
    id.command = gcvHAL_QUERY_KERNEL_SETTINGS;
#ifdef GCABI_HAS_HARDWARE_TYPE
    id.hardwareType = (gceHARDWARE_TYPE)hwtype;
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
    id.hardwareType = (gceHARDWARE_TYPE)hwtype;
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

