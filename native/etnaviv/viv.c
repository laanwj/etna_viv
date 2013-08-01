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
#include <etnaviv/viv.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "gc_abi.h"
#include "gc_hal_base.h"
#include "gc_hal.h"
#include "gc_hal_driver.h"
#ifdef GCABI_HAS_CONTEXT
#include "gc_hal_user_context.h"
#else
#include "gc_hal_kernel_context.h"
#endif
#include "gc_hal_types.h"
#include "etna_enum_convert.h"

#ifdef ETNAVIV_HOOK
/* If set, command stream will be logged to environment variable ETNAVIV_FDR */
#include "viv_hook.h"
#define open my_open
#define mmap my_mmap
#define munmap my_munmap
#define ioctl my_ioctl
#endif
//#define DEBUG

const char *galcore_device[] = {"/dev/galcore", "/dev/graphics/galcore", NULL};
#define INTERFACE_SIZE (sizeof(gcsHAL_INTERFACE))

/* Call ioctl interface with structure cmd as input and output.
 * @returns status (gcvSTATUS_xxx)
 */
int viv_invoke(struct viv_conn *conn, gcsHAL_INTERFACE *cmd)
{
    vivante_ioctl_data_t ic = {
        .in_buf = cmd,
        .in_buf_size = INTERFACE_SIZE,
        .out_buf = cmd,
        .out_buf_size = INTERFACE_SIZE
    };
    if(ioctl(conn->fd, IOCTL_GCHAL_INTERFACE, &ic) < 0)
        return -1;
#ifdef DEBUG
    if(cmd->status != 0)
    {
        printf("Command %i failed with status %i\n", cmd->command, cmd->status);
    }
#endif
    return cmd->status;
}

int viv_close(struct viv_conn *conn)
{
    if(conn->fd < 0)
        return -1;
    close(conn->fd);
    free(conn);
#ifdef ETNAVIV_HOOK
    close_hook();
#endif
    return 0;
}

/* convert specs to kernel-independent format */
static void convert_chip_specs(struct viv_specs *out, const struct _gcsHAL_QUERY_CHIP_IDENTITY *in)
{
    out->chip_model = in->chipModel;
    out->chip_revision = in->chipRevision;
    out->chip_features[0] = in->chipFeatures;
    out->chip_features[1] = in->chipMinorFeatures;
    out->chip_features[2] = in->chipMinorFeatures1;
#ifdef GCABI_HAS_MINOR_FEATURES_2
    out->chip_features[3] = in->chipMinorFeatures2;
#else
    out->chip_features[3] = 0;
#endif
#ifdef GCABI_HAS_MINOR_FEATURES_3
    out->chip_features[4] = in->chipMinorFeatures3;
#else
    out->chip_features[4] = 0;
#endif
    out->stream_count = in->streamCount;
    out->register_max = in->registerMax;
    out->thread_count = in->threadCount;
    out->shader_core_count = in->shaderCoreCount;
    out->vertex_cache_size = in->vertexCacheSize;
    out->vertex_output_buffer_size = in->vertexOutputBufferSize;
#ifdef GCABI_CHIPIDENTITY_EXT
    out->pixel_pipes = in->pixelPipes;
    out->instruction_count = in->instructionCount;
    out->num_constants = in->numConstants;
    out->buffer_size = in->bufferSize;
#else
    out->pixel_pipes = 1;
    out->instruction_count = 256;
    out->num_constants = 0; /* =default (depends on hw) */
    out->buffer_size = 0; /* =default (depends on hw) */
#endif
#ifdef GCABI_CHIPIDENTITY_VARYINGS
    out->varyings_count = in->varyingsCount;
#else
    out->varyings_count = 8;
#endif
}

int viv_open(enum viv_hw_type hw_type, struct viv_conn **out)
{
    struct viv_conn *conn = malloc(sizeof(struct viv_conn));
    int err = 0;
    if(conn == NULL)
        return -1;
#ifdef ETNAVIV_HOOK
    char *fdr_out = getenv("ETNAVIV_FDR");
    if(fdr_out)
       hook_start_logging(fdr_out);
#endif
    conn->hw_type = hw_type;
    gcsHAL_INTERFACE id = {};
    /* try galcore devices */
    conn->fd = -1;
    for(const char **pname = galcore_device; *pname && conn->fd < 0; ++pname)
    {
        conn->fd = open(*pname, O_RDWR);
    }
    if((err=conn->fd) < 0)
        goto error;
    
    /* Determine base address */
    id.command = gcvHAL_GET_BASE_ADDRESS;
    if((err=viv_invoke(conn, &id)) != gcvSTATUS_OK)
        goto error;
    conn->base_address = id.u.GetBaseAddress.baseAddress;
    printf("Physical address of internal memory: %08x\n", conn->base_address);

    /* Get chip identity */
    id.command = gcvHAL_QUERY_CHIP_IDENTITY;
    if((err=viv_invoke(conn, &id)) != gcvSTATUS_OK)
        goto error;
    convert_chip_specs(&conn->chip, &id.u.QueryChipIdentity);

    /* Map contiguous memory */
    id.command = gcvHAL_QUERY_VIDEO_MEMORY;
    if((err=viv_invoke(conn, &id)) != gcvSTATUS_OK)
        goto error;
    printf("* Video memory:\n");
    printf("  Internal physical: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.internalPhysical);
    printf("  Internal size: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.internalSize);
    printf("  External physical: %08x\n", (uint32_t)id.u.QueryVideoMemory.externalPhysical);
    printf("  External size: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.externalSize);
    printf("  Contiguous physical: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.contiguousPhysical);
    printf("  Contiguous size: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.contiguousSize);

    conn->mem_base = (viv_addr_t)id.u.QueryVideoMemory.contiguousPhysical;
    conn->mem = mmap(NULL, id.u.QueryVideoMemory.contiguousSize, PROT_READ|PROT_WRITE, MAP_SHARED, conn->fd, conn->mem_base);
    if(conn->mem == NULL)
    {
        err = -1;
        goto error;
    }

    conn->process = (gctHANDLE)getpid(); /* value passed as .process to commands */

    *out = conn;
    return gcvSTATUS_OK;
error:
    if(conn->fd >= 0)
        close(conn->fd);
    free(conn);
    return err;
}

int viv_alloc_contiguous(struct viv_conn *conn, size_t bytes, viv_addr_t *physical, void **logical, size_t *bytes_out)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_ALLOCATE_CONTIGUOUS_MEMORY,
        .u = {
            .AllocateContiguousMemory = {
                .bytes = bytes
            }
        }
    };
    int rv = viv_invoke(conn, &id);
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

int viv_alloc_linear_vidmem(struct viv_conn *conn, size_t bytes, size_t alignment, enum viv_surf_type type, enum viv_pool pool, viv_node_t *node, size_t *bytes_out)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY,
        .u = {
            .AllocateLinearVideoMemory = {
                .bytes = bytes,
                .alignment = alignment,
                .type = convert_surf_type(type),
                .pool = convert_pool(pool)
            }
        }
    };
    int rv = viv_invoke(conn, &id);
    if(rv != gcvSTATUS_OK)
    {
        *node = 0;
        if(bytes_out != NULL)
            *bytes_out = 0;
        return rv;
    }
    *node = id.u.AllocateLinearVideoMemory.node;
    if(bytes_out != NULL)
        *bytes_out = id.u.AllocateLinearVideoMemory.bytes;
    return gcvSTATUS_OK; 
}

int viv_lock_vidmem(struct viv_conn *conn, viv_node_t node, viv_addr_t *physical, void **logical)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_LOCK_VIDEO_MEMORY,
        .u = {
            .LockVideoMemory = {
                .node = node,
            }
        }
    };
    int rv = viv_invoke(conn, &id);
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
int viv_unlock_vidmem(struct viv_conn *conn, viv_node_t node, enum viv_surf_type type, int async)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_UNLOCK_VIDEO_MEMORY,
        .u = {
            .UnlockVideoMemory = {
                .node = node,
                .type = convert_surf_type(type), /* why does this need type? */
                .asynchroneous = async
            }
        }
    };
    return viv_invoke(conn, &id);
}

#ifdef GCABI_HAS_CONTEXT
int viv_commit(struct viv_conn *conn, gcoCMDBUF commandBuffer, viv_context_t contextBuffer, struct _gcsQUEUE *queue)
{
    int rv;
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_COMMIT,
        .u = {
            .Commit = {
                .commandBuffer = commandBuffer,
                .contextBuffer = (gcoCONTEXT)contextBuffer,
                .process = conn->process
            }
        }
    };
    if((rv=viv_invoke(conn, &id)) != gcvSTATUS_OK)
        return rv;
    /* commit queue after command buffer */
    if(queue != NULL && (rv=viv_event_commit(conn, queue)) != gcvSTATUS_OK)
       return rv;
    return gcvSTATUS_OK;
}
#else
int viv_commit(struct viv_conn *conn, gcoCMDBUF commandBuffer, viv_context_t context, struct _gcsQUEUE *queue)
{
    gcsSTATE_DELTA fake_delta;
    memset(&fake_delta, 0, sizeof(gcsSTATE_DELTA));

    gcsHAL_INTERFACE id = {
        .command = gcvHAL_COMMIT,
        .u = {
            .Commit = {
                .commandBuffer = commandBuffer,
                .context = (gckCONTEXT)context,
                .queue = queue,
                .delta = &fake_delta,
            }
        }
    };

    return viv_invoke(conn, &id);
}
#endif

int viv_event_commit(struct viv_conn *conn, struct _gcsQUEUE *queue)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_EVENT_COMMIT,
        .u = {
            .Event = {
                .queue = queue,
            }
        }
    };
    return viv_invoke(conn, &id);
}

int viv_user_signal_create(struct viv_conn *conn, int manualReset, int *id_out)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_USER_SIGNAL,
        .u = {
            .UserSignal = {
                .command = gcvUSER_SIGNAL_CREATE,
                .manualReset = manualReset,
#ifdef GCABI_USER_SIGNAL_HAS_TYPE
                .signalType = 0 /* only used for debugging and error messages inside kernel */
#endif
            }
        }
    };
    int rv = viv_invoke(conn, &id);
    if(rv != gcvSTATUS_OK)
        return rv;
    *id_out = id.u.UserSignal.id;
    return gcvSTATUS_OK;
}

int viv_user_signal_signal(struct viv_conn *conn, int sig_id, int state)
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
    return viv_invoke(conn, &id);
}

int viv_user_signal_wait(struct viv_conn *conn, int sig_id, int wait)
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
    return viv_invoke(conn, &id);
}

int viv_user_signal_destroy(struct viv_conn *conn, int sig_id)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_USER_SIGNAL,
        .u = {
            .UserSignal = {
                .command = gcvUSER_SIGNAL_DESTROY,
                .id = sig_id
            }
        }
    };
    return viv_invoke(conn, &id);
}

void viv_show_chip_info(struct viv_conn *conn)
{
    printf("* Chip identity:\n");
    printf("  Chip model: %08x\n", conn->chip.chip_model);
    printf("  Chip revision: %08x\n", conn->chip.chip_revision);
    printf("  Chip features: 0x%08x\n", conn->chip.chip_features[0]);
    printf("  Chip minor features 0: 0x%08x\n", conn->chip.chip_features[1]);
    printf("  Chip minor features 1: 0x%08x\n", conn->chip.chip_features[2]);
    printf("  Chip minor features 2: 0x%08x\n", conn->chip.chip_features[3]);
    printf("  Chip minor features 3: 0x%08x\n", conn->chip.chip_features[4]);
    printf("  Stream count: 0x%08x\n", conn->chip.stream_count);
    printf("  Register max: 0x%08x\n", conn->chip.register_max);
    printf("  Thread count: 0x%08x\n", conn->chip.thread_count);
    printf("  Shader core count: 0x%08x\n", conn->chip.shader_core_count);
    printf("  Vertex cache size: 0x%08x\n", conn->chip.vertex_cache_size);
    printf("  Vertex output buffer size: 0x%08x\n", conn->chip.vertex_output_buffer_size);
    printf("  Pixel pipes: 0x%08x\n", conn->chip.pixel_pipes);
    printf("  Instruction count: 0x%08x\n", conn->chip.instruction_count);
    printf("  Num constants: 0x%08x\n", conn->chip.num_constants);
    printf("  Buffer size: 0x%08x\n", conn->chip.buffer_size);
}

int viv_reset(struct viv_conn *conn)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_RESET,
    };
    return viv_invoke(conn, &id);
}

int viv_free_vidmem(struct viv_conn *conn, viv_node_t node)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_FREE_VIDEO_MEMORY,
        .u = {
            .FreeVideoMemory = {
                .node = node
            }
        }
    };
    return viv_invoke(conn, &id);
}

int viv_free_contiguous(struct viv_conn *conn, size_t bytes, viv_addr_t physical, void *logical)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_FREE_CONTIGUOUS_MEMORY,
        .u = {
            .FreeContiguousMemory = {
                .bytes = bytes,
                .physical = (gctPHYS_ADDR)physical,
                .logical = logical
            }
        }
    };
    return viv_invoke(conn, &id);
}

int viv_map_user_memory(struct viv_conn *conn, void *memory, size_t size, void **info, viv_addr_t *address)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_MAP_USER_MEMORY,
        .u = {
            .MapUserMemory = {
                .memory = memory,
                .size = size
            }
        }
    };
    int status = viv_invoke(conn, &id);
    *info = id.u.MapUserMemory.info;
    *address = id.u.MapUserMemory.address;
    return status;
}

int viv_unmap_user_memory(struct viv_conn *conn, void *memory, size_t size, void *info, viv_addr_t address)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_UNMAP_USER_MEMORY,
        .u = {
            .UnmapUserMemory = {
                .memory = memory,
                .size = size,
                .info = info,
                .address = address
            }
        }
    };
    return viv_invoke(conn, &id);
}

