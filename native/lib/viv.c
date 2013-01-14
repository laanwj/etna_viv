/*
 * Copyright (c) 2012-2013 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "viv.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <stdio.h>

#define GALCORE_DEVICE "/dev/galcore"
#define INTERFACE_SIZE (64)

int viv_fd = -1;
viv_addr_t viv_base_address = 0;
void *viv_mem = NULL;
viv_addr_t viv_mem_base = 0;
gctHANDLE viv_process;
struct _gcsHAL_QUERY_CHIP_IDENTITY viv_chip;

/* IOCTL structure for IOCTL_GCHAL_INTERFACE */
typedef struct 
{
    void *in_buf;
    uint32_t in_buf_size;
    void *out_buf;
    uint32_t out_buf_size;
} vivante_ioctl_data_t;

int viv_invoke(gcsHAL_INTERFACE *cmd)
{
    vivante_ioctl_data_t ic = {
        .in_buf = cmd,
        .in_buf_size = INTERFACE_SIZE,
        .out_buf = cmd,
        .out_buf_size = INTERFACE_SIZE
    };
    if(ioctl(viv_fd, IOCTL_GCHAL_INTERFACE, &ic) < 0)
        return -1;
    return cmd->status;
}

int viv_close(void)
{
    if(viv_fd < 0)
        return -1;
    return close(viv_fd);
}

int viv_open(void)
{
    gcsHAL_INTERFACE id = {};
    viv_fd = open(GALCORE_DEVICE, O_RDWR);
    if(viv_fd < 0)
        return viv_fd;
    
    /* Determine base address */
    id.command = gcvHAL_GET_BASE_ADDRESS;
    if(viv_invoke(&id) != gcvSTATUS_OK)
        return -1;
    viv_base_address = id.u.GetBaseAddress.baseAddress;
    printf("Physical address of internal memory: %08x\n", viv_base_address);

    /* Get chip identity */
    id.command = gcvHAL_QUERY_CHIP_IDENTITY;
    if(viv_invoke(&id) != gcvSTATUS_OK)
        return -1;
    viv_chip = id.u.QueryChipIdentity;

    /* Map contiguous memory */
    id.command = gcvHAL_QUERY_VIDEO_MEMORY;
    if(viv_invoke(&id) != gcvSTATUS_OK)
        return -1;
    printf("* Video memory:\n");
    printf("  Internal physical: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.internalPhysical);
    printf("  Internal size: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.internalSize);
    printf("  External physical: %08x\n", (uint32_t)id.u.QueryVideoMemory.externalPhysical);
    printf("  External size: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.externalSize);
    printf("  Contiguous physical: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.contiguousPhysical);
    printf("  Contiguous size: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.contiguousSize);

    viv_mem_base = (viv_addr_t)id.u.QueryVideoMemory.contiguousPhysical;
    viv_mem = mmap(NULL, id.u.QueryVideoMemory.contiguousSize, PROT_READ|PROT_WRITE, MAP_SHARED, viv_fd, viv_mem_base);
    if(viv_mem == NULL)
        return -1;

    viv_process = (gctHANDLE)getpid(); /* value passed as .process to commands */

    return gcvSTATUS_OK;
}

int viv_alloc_contiguous(size_t bytes, viv_addr_t *physical, void **logical, size_t *bytes_out)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_ALLOCATE_CONTIGUOUS_MEMORY,
        .u = {
            .AllocateContiguousMemory = {
                .bytes = bytes
            }
        }
    };
    int rv = viv_invoke(&id);
    if(rv != gcvSTATUS_OK)
    {
        *physical = 0;
        *logical = 0;
        return rv;
    }
    *physical = (viv_addr_t) id.u.AllocateContiguousMemory.physical;
    *logical = id.u.AllocateContiguousMemory.logical;
    if(bytes_out)
        *bytes_out = id.u.AllocateContiguousMemory.bytes;
    return gcvSTATUS_OK;
}

int viv_alloc_linear_vidmem(size_t bytes, size_t alignment, gceSURF_TYPE type, gcePOOL pool, gcuVIDMEM_NODE_PTR *node)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY,
        .u = {
            .AllocateLinearVideoMemory = {
                .bytes = bytes,
                .alignment = alignment,
                .type = type,
                .pool = pool
            }
        }
    };
    int rv = viv_invoke(&id);
    if(rv != gcvSTATUS_OK)
    {
        *node = 0;
        return rv;
    }
    *node = id.u.AllocateLinearVideoMemory.node;
    return gcvSTATUS_OK; 
}

int viv_lock_vidmem(gcuVIDMEM_NODE_PTR node, viv_addr_t *physical, void **logical)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_LOCK_VIDEO_MEMORY,
        .u = {
            .LockVideoMemory = {
                .node = node,
            }
        }
    };
    int rv = viv_invoke(&id);
    if(rv != gcvSTATUS_OK)
    {
        *physical = 0;
        *logical = 0;
        return rv;
    }
    *physical = id.u.LockVideoMemory.address;
    *logical = id.u.LockVideoMemory.memory;
    return gcvSTATUS_OK; 
}

/** Unlock (unmap) video memory node from GPU and CPU memory.
 */
int viv_unlock_vidmem(gcuVIDMEM_NODE_PTR node, gceSURF_TYPE type, int async)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_UNLOCK_VIDEO_MEMORY,
        .u = {
            .UnlockVideoMemory = {
                .node = node,
                .type = type, /* why does this need type? */
                .asynchroneous = async
            }
        }
    };
    return viv_invoke(&id);
}

/* TODO free contiguous memory and video memory */

int viv_commit(gcoCMDBUF commandBuffer, gcoCONTEXT contextBuffer)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_COMMIT,
        .u = {
            .Commit = {
                .commandBuffer = commandBuffer,
                .contextBuffer = contextBuffer,
                .process = viv_process
            }
        }
    };
    return viv_invoke(&id);
}

int viv_event_commit(gcsQUEUE *queue)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_EVENT_COMMIT,
        .u = {
            .Event = {
                .queue = queue,
            }
        }
    };
    return viv_invoke(&id);
}

int viv_user_signal_create(int manualReset, int *id_out)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_USER_SIGNAL,
        .u = {
            .UserSignal = {
                .command = gcvUSER_SIGNAL_CREATE,
                .manualReset = manualReset
            }
        }
    };
    int rv = viv_invoke(&id);
    if(rv != gcvSTATUS_OK)
        return rv;
    *id_out = id.u.UserSignal.id;
    return gcvSTATUS_OK;
}

int viv_user_signal_signal(int sig_id, int state)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_USER_SIGNAL,
        .u = {
            .UserSignal = {
                .command = gcvUSER_SIGNAL_SIGNAL,
                .id = sig_id,
                .state = state
            }
        }
    };
    return viv_invoke(&id);
}

int viv_user_signal_wait(int sig_id, int wait)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_USER_SIGNAL,
        .u = {
            .UserSignal = {
                .command = gcvUSER_SIGNAL_WAIT,
                .id = sig_id,
                .wait = wait
            }
        }
    };
    return viv_invoke(&id);
}

int viv_event_queue_signal(int sig_id, gceKERNEL_WHERE fromWhere)
{
    /* gcsQUEUE is copied by the kernel, so it does not need to be kept in memory
     * until the kernel processes it
     */
    gcsQUEUE id = {
        .next = NULL,
        .iface = {
            .command = gcvHAL_SIGNAL,
            .u = {
                .Signal = {
                    .signal = (void*)sig_id,
                    .auxSignal = (void*)0x0,
                    .process = viv_process,
                    .fromWhere = fromWhere
                }
            }
        }
    };
    return viv_event_commit(&id);
}

void viv_show_chip_info(void)
{
    printf("* Chip identity:\n");
    printf("  Chip model: %08x\n", viv_chip.chipModel);
    printf("  Chip revision: %08x\n", viv_chip.chipRevision);
    printf("  Chip features: 0x%08x\n", viv_chip.chipFeatures);
    printf("  Chip minor features 0: 0x%08x\n", viv_chip.chipMinorFeatures);
    printf("  Chip minor features 1: 0x%08x\n", viv_chip.chipMinorFeatures1);
    printf("  Chip minor features 2: 0x%08x\n", viv_chip.chipMinorFeatures2);
    printf("  Stream count: 0x%08x\n", viv_chip.streamCount);
    printf("  Register max: 0x%08x\n", viv_chip.registerMax);
    printf("  Thread count: 0x%08x\n", viv_chip.threadCount);
    printf("  Shader core count: 0x%08x\n", viv_chip.shaderCoreCount);
    printf("  Vertex cache size: 0x%08x\n", viv_chip.vertexCacheSize);
    printf("  Vertex output buffer size: 0x%08x\n", viv_chip.vertexOutputBufferSize);
}


