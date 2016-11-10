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
#include <etnaviv/etna_util.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "gc_abi.h"
#include "viv_internal.h"

#ifdef ETNAVIV_HOOK
/* If set, command stream will be logged to environment variable ETNAVIV_FDR */
#include "viv_hook.h"
#define open my_open
#define mmap my_mmap
#define munmap my_munmap
#define ioctl my_ioctl
#endif
//#define DEBUG

union rmk_gcabi_header {
    uint32_t padding[16];
    struct {
        uint32_t zero;
#ifdef GCABI_HAS_HARDWARE_TYPE
        uint32_t hwtype;
#endif
        uint32_t status;
    } v4;
};

/* rmk's extension for importing dmabufs */
struct viv_dmabuf_map {
    union rmk_gcabi_header hdr;
    uint64_t info;
    uint64_t address;
    int32_t fd;
    uint32_t prot;
};
#define IOC_GDMABUF_MAP _IOWR('_', 0, struct viv_dmabuf_map)

/* rmk's extension for mapping read-only memory (X shmem buffers) */
struct viv_membuf_map {
    union rmk_gcabi_header hdr;
    uint64_t info;
    uint64_t address;
    uint64_t virt;
    uint32_t size;
    uint32_t prot;
};
#define IOC_GMEMBUF_MAP _IOWR('_', 1, struct viv_membuf_map)

const char *galcore_device[] = {"/dev/gal3d", "/dev/galcore", "/dev/graphics/galcore", NULL};
#define INTERFACE_SIZE (sizeof(gcsHAL_INTERFACE))

/* Allocate signals for fences */
static int viv_allocate_signals(struct viv_conn *conn)
{
    int rv;
    if(pthread_mutex_init(&conn->fence_mutex, NULL))
        return VIV_STATUS_OUT_OF_MEMORY;
    for(int x=0; x<VIV_NUM_FENCE_SIGNALS; ++x)
    {
        /* Create signal with manual reset; we want to be able to probe it
         * or wait for it without resetting it.
         */
        if((rv = viv_user_signal_create(conn, /* manualReset */ false, &conn->fence_signals[x])) != VIV_STATUS_OK)
        {
            return rv;
        }
    }
    conn->next_fence_id = 0;
    conn->last_fence_id = -1; /* far enough into the past */
    return VIV_STATUS_OK;
}

/* Free signals for fences */
static int viv_deallocate_signals(struct viv_conn *conn)
{
    int rv;
    for(int x=0; x<VIV_NUM_FENCE_SIGNALS; ++x)
    {
        if((rv = viv_user_signal_destroy(conn, conn->fence_signals[x])) != VIV_STATUS_OK)
            return rv;
    }
    if(pthread_mutex_destroy(&conn->fence_mutex))
        return VIV_STATUS_OUT_OF_MEMORY;
    return VIV_STATUS_OK;
}

/* Get signal id # for a fence */
static int signal_for_fence(struct viv_conn *conn, uint32_t fence)
{
    if((conn->next_fence_id - fence) >= VIV_NUM_FENCE_SIGNALS)
        return -1; /* too old */
    return conn->fence_signals[fence % VIV_NUM_FENCE_SIGNALS];
}

/* Almost raw ioctl interface.  This provides an interface similar to
 * gcoOS_DeviceControl.
 * @returns standard ioctl semantics
 */
static int viv_ioctl(struct viv_conn *conn, int request, void *data, size_t size)
{
    vivante_ioctl_data_t ic = {
#ifdef GCABI_UINT64_IOCTL_DATA
        .in_buf = PTR_TO_VIV(data),
        .in_buf_size = size,
        .out_buf = PTR_TO_VIV(data),
        .out_buf_size = size
#else
        .in_buf = data,
        .in_buf_size = size,
        .out_buf = data,
        .out_buf_size = size
#endif
    };
    int ret, old_errno = errno;

    do {
        errno = 0;
        ret = ioctl(conn->fd, request, &ic);
    } while (ret == -1 && errno == EINTR);

    /* if there was no error, restore the old errno for proper errno handling */
    if(errno == 0)
        errno = old_errno;

    return ret;
}

/* Call ioctl interface with structure cmd as input and output.
 * @returns status (VIV_STATUS_xxx)
 */
int viv_invoke(struct viv_conn *conn, struct _gcsHAL_INTERFACE *cmd)
{
#ifdef GCABI_HAS_HARDWARE_TYPE
    cmd->hardwareType = (gceHARDWARE_TYPE)conn->hw_type;
#endif
    if(viv_ioctl(conn, IOCTL_GCHAL_INTERFACE, cmd, INTERFACE_SIZE) < 0)
        return -1;
#ifdef DEBUG
    if(cmd->status != 0)
    {
        fprintf(stderr, "Command %i failed with status %i\n", cmd->command, cmd->status);
    }
#endif
    return cmd->status;
}

int viv_close(struct viv_conn *conn)
{
    if(conn->fd < 0)
        return -1;

    (void) viv_deallocate_signals(conn);

    munmap(conn->mem, conn->mem_length);

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
    struct viv_conn *conn = ETNA_CALLOC_STRUCT(viv_conn);
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
        conn->fd = open(*pname, O_RDWR | O_CLOEXEC);
    }
    if((err=conn->fd) < 0)
        goto error;

#ifdef GCABI_HAS_STATE_DELTAS
    /* Determine version */
    id.command = gcvHAL_VERSION;
    if((err=viv_invoke(conn, &id)) != gcvSTATUS_OK)
        goto error;
    conn->kernel_driver.major = id.u.Version.major;
    conn->kernel_driver.minor = id.u.Version.minor;
    conn->kernel_driver.patch = id.u.Version.patch;
    conn->kernel_driver.build = id.u.Version.build;
#else
    /* <=v2 drivers don't have an always available mechanism for getting the driver version,
     * although in some (like dove) a /proc file is available that gives the version.
     */
    conn->kernel_driver.major = 2;
    conn->kernel_driver.minor = 0;
    conn->kernel_driver.patch = 0;
    conn->kernel_driver.build = 0;
#endif
    snprintf(conn->kernel_driver.name, sizeof(conn->kernel_driver.name),
            "Vivante GPL kernel driver %i.%i.%i.%i",
            conn->kernel_driver.major, conn->kernel_driver.minor,
            conn->kernel_driver.patch, conn->kernel_driver.build);
    fprintf(stderr, "Kernel: %s\n", conn->kernel_driver.name);

    /* Determine base address */
    id.command = gcvHAL_GET_BASE_ADDRESS;
    if((err=viv_invoke(conn, &id)) != gcvSTATUS_OK)
        goto error;
    conn->base_address = id.u.GetBaseAddress.baseAddress;
    fprintf(stderr, "Physical address of internal memory: %08x\n", conn->base_address);

    /* Get chip identity */
    id.command = gcvHAL_QUERY_CHIP_IDENTITY;
    if((err=viv_invoke(conn, &id)) != gcvSTATUS_OK)
        goto error;
    convert_chip_specs(&conn->chip, &id.u.QueryChipIdentity);

    /* Map contiguous memory */
    id.command = gcvHAL_QUERY_VIDEO_MEMORY;
    if((err=viv_invoke(conn, &id)) != gcvSTATUS_OK)
        goto error;
    fprintf(stderr, "* Video memory:\n");
    fprintf(stderr, "  Internal physical: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.internalPhysical);
    fprintf(stderr, "  Internal size: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.internalSize);
    fprintf(stderr, "  External physical: %08x\n", (uint32_t)id.u.QueryVideoMemory.externalPhysical);
    fprintf(stderr, "  External size: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.externalSize);
    fprintf(stderr, "  Contiguous physical: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.contiguousPhysical);
    fprintf(stderr, "  Contiguous size: 0x%08x\n", (uint32_t)id.u.QueryVideoMemory.contiguousSize);

    conn->mem_base = (viv_addr_t)id.u.QueryVideoMemory.contiguousPhysical;
    conn->mem_length = id.u.QueryVideoMemory.contiguousSize;
    conn->mem = mmap(NULL, conn->mem_length, PROT_READ|PROT_WRITE, MAP_SHARED, conn->fd, conn->mem_base);
    if(conn->mem == NULL)
    {
        err = -1;
        goto error;
    }

    conn->process = getpid(); /* value passed as .process to commands */

    if((err=viv_allocate_signals(conn)) != VIV_STATUS_OK)
        goto error;

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
    *logical = VIV_TO_PTR(id.u.AllocateContiguousMemory.logical);
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
    *node = VIV_TO_HANDLE(id.u.AllocateLinearVideoMemory.node);
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
                .node = HANDLE_TO_VIV(node),
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
    *logical = VIV_TO_PTR(id.u.LockVideoMemory.memory);
    return gcvSTATUS_OK;
}

int viv_unlock_vidmem(struct viv_conn *conn, viv_node_t node, enum viv_surf_type type, bool submit_as_event, int *async)
{
    int rv;
    if(!submit_as_event && async == NULL)
        return VIV_STATUS_INVALID_ADDRESS;
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_UNLOCK_VIDEO_MEMORY,
        .u = {
            .UnlockVideoMemory = {
                .node = HANDLE_TO_VIV(node),
                .type = convert_surf_type(type), /* why does this need type? */
                .asynchroneous = 0
            }
        }
    };
    if(submit_as_event) /* submit as event immediately */
    {
        struct _gcsQUEUE queue = {
            .next = PTR_TO_VIV(NULL),
            .iface = id
        };
        rv = viv_event_commit(conn, &queue);
    } else { /* submit as command, return async flag */
        rv = viv_invoke(conn, &id);
        *async = id.u.UnlockVideoMemory.asynchroneous;
    }
    return rv;
}

#ifdef GCABI_HAS_CONTEXT
int viv_commit(struct viv_conn *conn, struct _gcoCMDBUF *commandBuffer, viv_context_t contextBuffer, struct _gcsQUEUE *queue)
{
    int rv;
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_COMMIT,
        .u = {
            .Commit = {
                .commandBuffer = commandBuffer,
                .contextBuffer = HANDLE_TO_VIV(contextBuffer),
                .process = HANDLE_TO_VIV(conn->process)
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
int viv_commit(struct viv_conn *conn, struct _gcoCMDBUF *commandBuffer, viv_context_t context, struct _gcsQUEUE *queue)
{
    gcsSTATE_DELTA fake_delta;
    memset(&fake_delta, 0, sizeof(gcsSTATE_DELTA));

    gcsHAL_INTERFACE id = {
        .command = gcvHAL_COMMIT,
        .u = {
            .Commit = {
                .commandBuffer = PTR_TO_VIV(commandBuffer),
                .context = HANDLE_TO_VIV(context),
                .queue = PTR_TO_VIV(queue),
                .delta = PTR_TO_VIV(&fake_delta),
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
                .queue = PTR_TO_VIV(queue),
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
    fprintf(stderr, "* Chip identity:\n");
    fprintf(stderr, "  Chip model: %08x\n", conn->chip.chip_model);
    fprintf(stderr, "  Chip revision: %08x\n", conn->chip.chip_revision);
    fprintf(stderr, "  Chip features: 0x%08x\n", conn->chip.chip_features[0]);
    fprintf(stderr, "  Chip minor features 0: 0x%08x\n", conn->chip.chip_features[1]);
    fprintf(stderr, "  Chip minor features 1: 0x%08x\n", conn->chip.chip_features[2]);
    fprintf(stderr, "  Chip minor features 2: 0x%08x\n", conn->chip.chip_features[3]);
    fprintf(stderr, "  Chip minor features 3: 0x%08x\n", conn->chip.chip_features[4]);
    fprintf(stderr, "  Stream count: 0x%08x\n", conn->chip.stream_count);
    fprintf(stderr, "  Register max: 0x%08x\n", conn->chip.register_max);
    fprintf(stderr, "  Thread count: 0x%08x\n", conn->chip.thread_count);
    fprintf(stderr, "  Shader core count: 0x%08x\n", conn->chip.shader_core_count);
    fprintf(stderr, "  Vertex cache size: 0x%08x\n", conn->chip.vertex_cache_size);
    fprintf(stderr, "  Vertex output buffer size: 0x%08x\n", conn->chip.vertex_output_buffer_size);
    fprintf(stderr, "  Pixel pipes: 0x%08x\n", conn->chip.pixel_pipes);
    fprintf(stderr, "  Instruction count: 0x%08x\n", conn->chip.instruction_count);
    fprintf(stderr, "  Num constants: 0x%08x\n", conn->chip.num_constants);
    fprintf(stderr, "  Buffer size: 0x%08x\n", conn->chip.buffer_size);
}

int viv_reset(struct viv_conn *conn)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_RESET,
    };
    return viv_invoke(conn, &id);
}

int viv_free_vidmem(struct viv_conn *conn, viv_node_t node, bool submit_as_event)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_FREE_VIDEO_MEMORY,
        .u = {
            .FreeVideoMemory = {
                .node = HANDLE_TO_VIV(node)
            }
        }
    };
    if(submit_as_event) /* submit as event immediately */
    {
        struct _gcsQUEUE queue = {
            .next = PTR_TO_VIV(NULL),
            .iface = id
        };
        return viv_event_commit(conn, &queue);
    } else { /* submit as command */
        return viv_invoke(conn, &id);
    }
}

int viv_free_contiguous(struct viv_conn *conn, size_t bytes, viv_addr_t physical, void *logical)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_FREE_CONTIGUOUS_MEMORY,
        .u = {
            .FreeContiguousMemory = {
                .bytes = bytes,
                .physical = PTR_TO_VIV((gctPHYS_ADDR)physical),
                .logical = PTR_TO_VIV(logical)
            }
        }
    };
    return viv_invoke(conn, &id);
}

int viv_map_dmabuf(struct viv_conn *conn, int fd, viv_usermem_t *info, viv_addr_t *address, int prot)
{
    struct viv_dmabuf_map map = {
#ifdef GCABI_HAS_HARDWARE_TYPE
        .hdr.v4.hwtype = (gceHARDWARE_TYPE)conn->hw_type,
#endif
        .fd = fd,
        .prot = prot,
    };
    int ret = viv_ioctl(conn, IOC_GDMABUF_MAP, &map, sizeof(map));
    if(ret < 0 || map.hdr.v4.status)
        return -1;
    *info = VIV_TO_HANDLE(map.info);
    *address = map.address;
    return VIV_STATUS_OK;
}

int viv_map_user_memory_prot(struct viv_conn *conn, void *memory, size_t size, int prot, viv_usermem_t *info, viv_addr_t *address)
{
    struct viv_membuf_map map = {
#ifdef GCABI_HAS_HARDWARE_TYPE
        .hdr.v4.hwtype = (gceHARDWARE_TYPE)conn->hw_type,
#endif
        .virt = (uintptr_t)memory,
        .size = size,
        .prot = prot,
    };
    int ret = viv_ioctl(conn, IOC_GMEMBUF_MAP, &map, sizeof(map));
    if(ret < 0 || map.hdr.v4.status)
        return -1;
    *info = VIV_TO_HANDLE(map.info);
    *address = map.address;
    return VIV_STATUS_OK;
}

int viv_map_user_memory(struct viv_conn *conn, void *memory, size_t size, viv_usermem_t *info, viv_addr_t *address)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_MAP_USER_MEMORY,
        .u = {
            .MapUserMemory = {
                .memory = PTR_TO_VIV(memory),
#ifdef GCABI_MAPUSERMEMORY_HAS_PHYSICAL
                .physical = ~0UL,
#endif
                .size = size
            }
        }
    };
    int status = viv_invoke(conn, &id);
    *info = VIV_TO_HANDLE(id.u.MapUserMemory.info);
    *address = id.u.MapUserMemory.address;
    return status;
}

int viv_unmap_user_memory(struct viv_conn *conn, void *memory, size_t size, viv_usermem_t info, viv_addr_t address)
{
    gcsHAL_INTERFACE id = {
        .command = gcvHAL_UNMAP_USER_MEMORY,
        .u = {
            .UnmapUserMemory = {
                .memory = PTR_TO_VIV(memory),
                .size = size,
                .info = HANDLE_TO_VIV(info),
                .address = address
            }
        }
    };
    return viv_invoke(conn, &id);
}

int viv_read_register(struct viv_conn *conn, uint32_t address, uint32_t *data)
{
    gcsHAL_INTERFACE id;
    int rv;
    id.command = gcvHAL_READ_REGISTER;
    id.u.ReadRegisterData.address = address;
    rv = viv_invoke(conn, &id);
    *data = id.u.ReadRegisterData.data;
    return rv;
}

int viv_write_register(struct viv_conn *conn, uint32_t address, uint32_t data)
{
    gcsHAL_INTERFACE id;
    id.command = gcvHAL_WRITE_REGISTER;
    id.u.WriteRegisterData.address = address;
    id.u.WriteRegisterData.data = data;
    return viv_invoke(conn, &id);
}

/* Fence emulation */
int _viv_fence_new(struct viv_conn *conn, uint32_t *fence_out, int *signal_out)
{
    /* Request fence and queue signal */
    uint32_t fence = conn->next_fence_id++;
    int signal = signal_for_fence(conn, fence);
    int status;
    /*   First, wait for old signal before reusing it if needed */
    uint32_t oldfence = fence - VIV_NUM_FENCE_SIGNALS;
    uint32_t fence_mod_signals = (fence % VIV_NUM_FENCE_SIGNALS);
    if(conn->fences_pending & (1<<fence_mod_signals)) /* fence still pending? */
    {
#ifdef FENCE_DEBUG
        fprintf(stderr, "Waiting for old fence %08x (which is after %08x)\n", oldfence,
                conn->last_fence_id);
#endif
        if((status = viv_user_signal_wait(conn, signal, VIV_WAIT_INDEFINITE)) != VIV_STATUS_OK)
        {
            return status;
        }
        /* update last signalled fence if necessary */
        if(VIV_FENCE_BEFORE(conn->last_fence_id, oldfence))
            conn->last_fence_id = oldfence;
        conn->fences_pending &= ~(1<<fence_mod_signals);
    }
    *fence_out = fence;
    *signal_out = signal;
#ifdef FENCE_DEBUG
    fprintf(stderr, "New fence: %08x [signal %08x], pending %08x\n", fence, signal, conn->fences_pending);
#endif
    return VIV_STATUS_OK;
}

void _viv_fence_mark_pending(struct viv_conn *conn, uint32_t fence)
{
    if((conn->next_fence_id - fence) >= VIV_NUM_FENCE_SIGNALS)
        return; /* too old */
    conn->fences_pending |= (1<<(fence % VIV_NUM_FENCE_SIGNALS));
}

int viv_fence_finish(struct viv_conn *conn, uint32_t fence, uint32_t timeout)
{
    int signal;
    int rv;
    pthread_mutex_lock(&conn->fence_mutex);
    signal = signal_for_fence(conn, fence);
    if(signal == -1)
    {
#ifdef FENCE_DEBUG
        fprintf(stderr, "Fence already expired: %08x\n", fence);
#endif
        goto unlock_and_ok; /* fence too old, it must have been signalled already */
    }
    /* If fence is older than last_fence_id which is the last signalled fence,
     * it must already have been signalled. We can make use of the fact that there is only
     * one ringbuffer inside the kernel, so commands submitted prior to this
     * fence will be executed before this fence.
     * Also check whether fence is really pending, if not simply return.
     */
    uint32_t fence_mod_signals = (fence % VIV_NUM_FENCE_SIGNALS);
    if(!(conn->fences_pending & (1<<fence_mod_signals)) ||
        VIV_FENCE_BEFORE_EQ(fence, conn->last_fence_id))
    {
#ifdef FENCE_DEBUG
        fprintf(stderr, "Fence already signaled: %08x, pending %i, newer than %08x; next fence id is %08x\n",
                fence,
                (conn->fences_pending >> fence_mod_signals)&1,
                conn->last_fence_id, conn->next_fence_id);
#endif
        goto unlock_and_ok;
    }
    pthread_mutex_unlock(&conn->fence_mutex); /* don't keep mutex while waiting */

    rv = viv_user_signal_wait(conn, signal, timeout);
    if(rv == VIV_STATUS_OK)
    {
        pthread_mutex_lock(&conn->fence_mutex);
        /* mark fence as non-pending */
        conn->fences_pending &= ~(1<<fence_mod_signals);
        /* if fence is later than last_fence_id, update last_fence_id */
        if(VIV_FENCE_BEFORE(conn->last_fence_id, fence))
        {
            conn->last_fence_id = fence;
#ifdef FENCE_DEBUG
            fprintf(stderr, "Last fence id updated to %i\n", conn->last_fence_id);
#endif
        }
        pthread_mutex_unlock(&conn->fence_mutex);
    }
    return rv;

unlock_and_ok: /* unlock mutex and return OK */
    pthread_mutex_unlock(&conn->fence_mutex);
    return VIV_STATUS_OK;
}

