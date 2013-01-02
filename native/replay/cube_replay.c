#include "gc_hal_base.h"
#include "gc_hal.h"
#include "gc_hal_driver.h"
#include "gc_hal_user_context.h"
#include "gc_hal_types.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>

#include "write_bmp.h"
#define GALCORE_DEVICE "/dev/galcore"
#define INTERFACE_SIZE (64)

/* "relocation" */
typedef struct
{
    uint32_t index; /* index into command buffer */
    uint32_t address; /* state address */
} address_index_t;

#include "cube_cmd.h"
#include "context_cmd.h"

/* IOCTL structure for IOCTL_GCHAL_INTERFACE */
typedef struct 
{
    void *in_buf;
    uint32_t in_buf_size;
    void *out_buf;
    uint32_t out_buf_size;
} vivante_ioctl_data_t;

/* Type for GPU physical address */
typedef uint32_t viv_addr_t;

int viv_fd = -1;
viv_addr_t viv_base_address = 0;
void *viv_mem = NULL;
viv_addr_t viv_mem_base = 0;
gctHANDLE viv_process;
struct _gcsHAL_QUERY_CHIP_IDENTITY viv_chip;

/* Call ioctl interface with structure cmd as input and output.
 * @returns status (gcvSTATUS_xxx)
 */
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

/* Close connection to GPU driver.
 */
int viv_close(void)
{
    if(viv_fd < 0)
        return -1;
    return close(viv_fd);
}

/* Open connection to GPU driver.
 */
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

/** Allocate contiguous GPU-mapped memory */
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

/** Allocate linear video memory.
  @returns a handle. To get the GPU and CPU address of the memory, use lock_vidmem
 */
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

/** Lock (map) video memory node to GPU and CPU memory.
 */
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

/** Commit GPU command buffer and context.
 */
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

/** Commit event queue.
 */
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

/** Create a new user signal.
 *  if manualReset=0 automatic reset on WAIT
 *     manualReset=1 need to manually reset state to 0 using SIGNAL
 */
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

/** Set user signal state.
 */
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

#define SIG_WAIT_INDEFINITE (0xffffffff)
/** Wait for signal. Provide time to wait in milliseconds, or SIG_WAIT_INDEFINITE.
 */
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

/** Queue synchronization signal from GPU.
 */
int viv_event_queue_signal(int sig_id, gceKERNEL_WHERE fromWhere)
{
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

float vVertices[] = {
  // front
  -1.0f, -1.0f, +1.0f, // point blue
  //-0.5f, -0.6f, +0.5f, // point blue
  +1.0f, -1.0f, +1.0f, // point magenta
  -1.0f, +1.0f, +1.0f, // point cyan
  +1.0f, +1.0f, +1.0f, // point white
  // back
  +1.0f, -1.0f, -1.0f, // point red
  -1.0f, -1.0f, -1.0f, // point black
  +1.0f, +1.0f, -1.0f, // point yellow
  -1.0f, +1.0f, -1.0f, // point green
  // right
  +1.0f, -1.0f, +1.0f, // point magenta
  +1.0f, -1.0f, -1.0f, // point red
  +1.0f, +1.0f, +1.0f, // point white
  +1.0f, +1.0f, -1.0f, // point yellow
  // left
  -1.0f, -1.0f, -1.0f, // point black
  -1.0f, -1.0f, +1.0f, // point blue
  -1.0f, +1.0f, -1.0f, // point green
  -1.0f, +1.0f, +1.0f, // point cyan
  // top
  -1.0f, +1.0f, +1.0f, // point cyan
  +1.0f, +1.0f, +1.0f, // point white
  -1.0f, +1.0f, -1.0f, // point green
  +1.0f, +1.0f, -1.0f, // point yellow
  // bottom
  -1.0f, -1.0f, -1.0f, // point black
  +1.0f, -1.0f, -1.0f, // point red
  -1.0f, -1.0f, +1.0f, // point blue
  +1.0f, -1.0f, +1.0f  // point magenta
};

float vColors[] = {
  // front
  0.0f,  0.0f,  1.0f, // blue
  1.0f,  0.0f,  1.0f, // magenta
  0.0f,  1.0f,  1.0f, // cyan
  1.0f,  1.0f,  1.0f, // white
  // back
  1.0f,  0.0f,  0.0f, // red
  0.0f,  0.0f,  0.0f, // black
  1.0f,  1.0f,  0.0f, // yellow
  0.0f,  1.0f,  0.0f, // green
  // right
  1.0f,  0.0f,  1.0f, // magenta
  1.0f,  0.0f,  0.0f, // red
  1.0f,  1.0f,  1.0f, // white
  1.0f,  1.0f,  0.0f, // yellow
  // left
  0.0f,  0.0f,  0.0f, // black
  0.0f,  0.0f,  1.0f, // blue
  0.0f,  1.0f,  0.0f, // green
  0.0f,  1.0f,  1.0f, // cyan
  // top
  0.0f,  1.0f,  1.0f, // cyan
  1.0f,  1.0f,  1.0f, // white
  0.0f,  1.0f,  0.0f, // green
  1.0f,  1.0f,  0.0f, // yellow
  // bottom
  0.0f,  0.0f,  0.0f, // black
  1.0f,  0.0f,  0.0f, // red
  0.0f,  0.0f,  1.0f, // blue
  1.0f,  0.0f,  1.0f  // magenta
};

float vNormals[] = {
  // front
  +0.0f, +0.0f, +1.0f, // forward
  +0.0f, +0.0f, +1.0f, // forward
  +0.0f, +0.0f, +1.0f, // forward
  +0.0f, +0.0f, +1.0f, // forward
  // back
  +0.0f, +0.0f, -1.0f, // backbard
  +0.0f, +0.0f, -1.0f, // backbard
  +0.0f, +0.0f, -1.0f, // backbard
  +0.0f, +0.0f, -1.0f, // backbard
  // right
  +1.0f, +0.0f, +0.0f, // right
  +1.0f, +0.0f, +0.0f, // right
  +1.0f, +0.0f, +0.0f, // right
  +1.0f, +0.0f, +0.0f, // right
  // left
  -1.0f, +0.0f, +0.0f, // left
  -1.0f, +0.0f, +0.0f, // left
  -1.0f, +0.0f, +0.0f, // left
  -1.0f, +0.0f, +0.0f, // left
  // top
  +0.0f, +1.0f, +0.0f, // up
  +0.0f, +1.0f, +0.0f, // up
  +0.0f, +1.0f, +0.0f, // up
  +0.0f, +1.0f, +0.0f, // up
  // bottom
  +0.0f, -1.0f, +0.0f, // down
  +0.0f, -1.0f, +0.0f, // down
  +0.0f, -1.0f, +0.0f, // down
  +0.0f, -1.0f, +0.0f  // down
};
#define COMPONENTS_PER_VERTEX (3)
#define NUM_VERTICES (6*4)

int main(int argc, char **argv)
{
    int rv;
    rv = viv_open();
    if(rv!=0)
    {
        fprintf(stderr, "Error opening device %s\n", GALCORE_DEVICE);
        exit(1);
    }
    printf("Succesfully opened device\n");
    viv_show_chip_info();

    /* allocate command buffer (blob uses four command buffers, but we don't even fill one) */
    viv_addr_t buf0_physical = 0;
    void *buf0_logical = 0;
    if(viv_alloc_contiguous(0x8000, &buf0_physical, &buf0_logical, NULL)!=0)
    {
        fprintf(stderr, "Error allocating host memory\n");
        exit(1);
    }
    printf("Allocated buffer: phys=%08x log=%08x\n", (uint32_t)buf0_physical, (uint32_t)buf0_logical);

    /* allocate main render target */
    gcuVIDMEM_NODE_PTR rt_node = 0;
    if(viv_alloc_linear_vidmem(0x70000, 0x40, gcvSURF_RENDER_TARGET, gcvPOOL_DEFAULT, &rt_node)!=0)
    {
        fprintf(stderr, "Error allocating render target buffer memory\n");
        exit(1);
    }
    printf("Allocated render target node: node=%08x\n", (uint32_t)rt_node);
    
    viv_addr_t rt_physical = 0;
    void *rt_logical = 0;
    if(viv_lock_vidmem(rt_node, &rt_physical, &rt_logical)!=0)
    {
        fprintf(stderr, "Error locking render target memory\n");
        exit(1);
    }
    printf("Locked render target: phys=%08x log=%08x\n", (uint32_t)rt_physical, (uint32_t)rt_logical);
    memset(rt_logical, 0xff, 0x70000); /* clear previous result just in case, test that clearing works */

    /* allocate tile status for main render target */
    gcuVIDMEM_NODE_PTR rt_ts_node = 0;
    if(viv_alloc_linear_vidmem(0x700, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &rt_ts_node)!=0)
    {
        fprintf(stderr, "Error allocating render target tile status memory\n");
        exit(1);
    }
    printf("Allocated render target tile status node: node=%08x\n", (uint32_t)rt_ts_node);
    
    viv_addr_t rt_ts_physical = 0;
    void *rt_ts_logical = 0;
    if(viv_lock_vidmem(rt_ts_node, &rt_ts_physical, &rt_ts_logical)!=0)
    {
        fprintf(stderr, "Error locking render target memory\n");
        exit(1);
    }
    printf("Locked render target ts: phys=%08x log=%08x\n", (uint32_t)rt_ts_physical, (uint32_t)rt_ts_logical);

    /* allocate depth for main render target */
    gcuVIDMEM_NODE_PTR z_node = 0;
    if(viv_alloc_linear_vidmem(0x38000, 0x40, gcvSURF_DEPTH, gcvPOOL_DEFAULT, &z_node)!=0)
    {
        fprintf(stderr, "Error allocating depth memory\n");
        exit(1);
    }
    printf("Allocated depth node: node=%08x\n", (uint32_t)z_node);
    
    viv_addr_t z_physical = 0;
    void *z_logical = 0;
    if(viv_lock_vidmem(z_node, &z_physical, &z_logical)!=0)
    {
        fprintf(stderr, "Error locking depth target memory\n");
        exit(1);
    }
    printf("Locked depth target: phys=%08x log=%08x\n", (uint32_t)z_physical, (uint32_t)z_logical);

    /* allocate depth ts for main render target */
    gcuVIDMEM_NODE_PTR z_ts_node = 0;
    if(viv_alloc_linear_vidmem(0x400, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &z_ts_node)!=0)
    {
        fprintf(stderr, "Error allocating depth memory\n");
        exit(1);
    }
    printf("Allocated depth ts node: node=%08x\n", (uint32_t)z_ts_node);
    
    viv_addr_t z_ts_physical = 0;
    void *z_ts_logical = 0;
    if(viv_lock_vidmem(z_ts_node, &z_ts_physical, &z_ts_logical)!=0)
    {
        fprintf(stderr, "Error locking depth target ts memory\n");
        exit(1);
    }
    printf("Locked depth ts target: phys=%08x log=%08x\n", (uint32_t)z_ts_physical, (uint32_t)z_ts_logical);

    /* allocate vertex buffer */
    gcuVIDMEM_NODE_PTR vtx_node = 0;
    if(viv_alloc_linear_vidmem(0x60000, 0x40, gcvSURF_VERTEX, gcvPOOL_DEFAULT, &vtx_node)!=0)
    {
        fprintf(stderr, "Error allocating vertex memory\n");
        exit(1);
    }
    printf("Allocated vertex node: node=%08x\n", (uint32_t)vtx_node);
    
    viv_addr_t vtx_physical = 0;
    void *vtx_logical = 0;
    if(viv_lock_vidmem(vtx_node, &vtx_physical, &vtx_logical)!=0)
    {
        fprintf(stderr, "Error locking vertex memory\n");
        exit(1);
    }
    printf("Locked vertex memory: phys=%08x log=%08x\n", (uint32_t)vtx_physical, (uint32_t)vtx_logical);

    /* allocate aux render target */
    gcuVIDMEM_NODE_PTR aux_rt_node = 0;
    if(viv_alloc_linear_vidmem(0x4000, 0x40, gcvSURF_RENDER_TARGET, gcvPOOL_SYSTEM /*why?*/, &aux_rt_node)!=0)
    {
        fprintf(stderr, "Error allocating aux render target buffer memory\n");
        exit(1);
    }
    printf("Allocated aux render target node: node=%08x\n", (uint32_t)aux_rt_node);
    
    viv_addr_t aux_rt_physical = 0;
    void *aux_rt_logical = 0;
    if(viv_lock_vidmem(aux_rt_node, &aux_rt_physical, &aux_rt_logical)!=0)
    {
        fprintf(stderr, "Error locking aux render target memory\n");
        exit(1);
    }
    printf("Locked aux render target: phys=%08x log=%08x\n", (uint32_t)aux_rt_physical, (uint32_t)aux_rt_logical);

    /* allocate tile status for aux render target */
    gcuVIDMEM_NODE_PTR aux_rt_ts_node = 0;
    if(viv_alloc_linear_vidmem(0x100, 0x40, gcvSURF_TILE_STATUS, gcvPOOL_DEFAULT, &aux_rt_ts_node)!=0)
    {
        fprintf(stderr, "Error allocating aux render target tile status memory\n");
        exit(1);
    }
    printf("Allocated aux render target tile status node: node=%08x\n", (uint32_t)aux_rt_ts_node);
    
    viv_addr_t aux_rt_ts_physical = 0;
    void *aux_rt_ts_logical = 0;
    if(viv_lock_vidmem(aux_rt_ts_node, &aux_rt_ts_physical, &aux_rt_ts_logical)!=0)
    {
        fprintf(stderr, "Error locking aux ts render target memory\n");
        exit(1);
    }
    printf("Locked aux render target ts: phys=%08x log=%08x\n", (uint32_t)aux_rt_ts_physical, (uint32_t)aux_rt_ts_logical);

    /* Phew, now we got all the memory we need.
     * Write interleaved attribute vertex stream.
     * Unlike the GL example we only do this once, not every time glDrawArrays is called, the same would be accomplished
     * from GL by using a vertex buffer object.
     */
    for(int vert=0; vert<NUM_VERTICES; ++vert)
    {
        int src_idx = vert * COMPONENTS_PER_VERTEX;
        int dest_idx = vert * COMPONENTS_PER_VERTEX * 3;
        for(int comp=0; comp<COMPONENTS_PER_VERTEX; ++comp)
        {
            ((float*)vtx_logical)[dest_idx+comp+0] = vVertices[src_idx + comp]; /* 0 */
            ((float*)vtx_logical)[dest_idx+comp+3] = vNormals[src_idx + comp]; /* 1 */
            ((float*)vtx_logical)[dest_idx+comp+6] = vColors[src_idx + comp]; /* 2 */
        }
    }
    /*
    for(int idx=0; idx<NUM_VERTICES*3*3; ++idx)
    {
        printf("%i %f\n", idx, ((float*)vtx_logical)[idx]);
    }*/

    /* Load the command buffer and send the commit command. */
    /* First build context state map */
    size_t stateCount = 0x1d00;
    uint32_t *contextMap = malloc(stateCount * 4);
    memset(contextMap, 0, stateCount*4);
    for(int idx=0; idx<sizeof(contextbuf_addr)/sizeof(address_index_t); ++idx)
    {
        contextMap[contextbuf_addr[idx].address / 4] = contextbuf_addr[idx].index;
    }

    struct _gcoCMDBUF commandBuffer = {
        .object = {
            .type = gcvOBJ_COMMANDBUFFER
        },
        //.os = (_gcoOS*)0xbf7488,
        //.hardware = (_gcoHARDWARE*)0x402694e0,
        .physical = (void*)buf0_physical,
        .logical = (void*)buf0_logical,
        .bytes = 0x8000,
        .startOffset = 0x0,
        //.offset = 0xac0,
        //.free = 0x7520,
        //.hintTable = (unsigned int*)0x0, // Used when gcdSECURE
        //.hintIndex = (unsigned int*)0x58,  // Used when gcdSECURE
        //.hintCommit = (unsigned int*)0xffffffff // Used when gcdSECURE
    };
    struct _gcoCONTEXT contextBuffer = {
        .object = {
            .type = gcvOBJ_CONTEXT
        },
        //.os = (_gcoOS*)0xbf7488,
        //.hardware = (_gcoHARDWARE*)0x402694e0,
        .id = 0x0, // Actual ID will be returned here
        .map = contextMap,
        .stateCount = stateCount,
        //.hint = (unsigned char*)0x0, // Used when gcdSECURE
        //.hintValue = 2, // Used when gcdSECURE
        //.hintCount = 0xca, // Used when gcdSECURE
        .buffer = contextbuf,
        .pipe3DIndex = 0x2d6, // XXX should not be hardcoded
        .pipe2DIndex = 0x106e,
        .linkIndex = 0x1076,
        .inUseIndex = 0x1078,
        .bufferSize = 0x41e4,
        .bytes = 0x0, // Number of bytes at physical, logical
        .physical = (void*)0x0,
        .logical = (void*)0x0,
        .link = (void*)0x0, // Logical address of link
        .initialPipe = 0x1,
        .entryPipe = 0x0,
        .currentPipe = 0x0,
        .postCommit = 1,
        .inUse = (int*)0x0, // Logical address of inUse
        .lastAddress = 0xffffffff, // Not used by kernel
        .lastSize = 0x2, // Not used by kernel
        .lastIndex = 0x106a, // Not used by kernel
        .lastFixed = 0, // Not used by kernel
        //.hintArray = (unsigned int*)0x0, // Used when gcdSECURE
        //.hintIndex = (unsigned int*)0x0  // Used when gcdSECURE
    };
    commandBuffer.free = commandBuffer.bytes - 0x8; /* Always keep 0x8 at end of buffer for kernel driver */
    /* Set addresses in first command buffer */
    cmdbuf1[0x57] = cmdbuf1[0x67] = cmdbuf1[0x9f] = cmdbuf1[0xbb] = cmdbuf1[0xd9] = cmdbuf1[0xfb] = rt_physical;
    cmdbuf1[0x65] = cmdbuf1[0x9d] = cmdbuf1[0xb9] = cmdbuf1[0xd7] = cmdbuf1[0xe5] = cmdbuf1[0xf9] = rt_ts_physical;
    cmdbuf1[0x6d] = cmdbuf1[0x7f] = z_physical;
    cmdbuf1[0x7d] = z_ts_physical;
    cmdbuf1[0x87] = cmdbuf1[0xa3] = cmdbuf1[0xc1] = aux_rt_ts_physical;
    cmdbuf1[0x89] = cmdbuf1[0x8f] = cmdbuf1[0x93] 
        = cmdbuf1[0xa5] = cmdbuf1[0xab] = cmdbuf1[0xaf] 
        = cmdbuf1[0xc3] = cmdbuf1[0xc9] = cmdbuf1[0xcd] = aux_rt_physical;
    cmdbuf1[0x1f3] = cmdbuf1[0x215] = cmdbuf1[0x237] 
        = cmdbuf1[0x259] = cmdbuf1[0x27b] = cmdbuf1[0x29d] = vtx_physical;

    /* Submit first command buffer */
    commandBuffer.startOffset = 0;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf1, sizeof(cmdbuf1));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf1);
    commandBuffer.free -= sizeof(cmdbuf1) + 0x18;
    printf("[1] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(&commandBuffer, &contextBuffer) != 0)
    {
        fprintf(stderr, "Error committing first command buffer\n");
        exit(1);
    }

    /* After the first COMMIT, allocate contiguous memory for context and set
     * bytes, physical, logical, link, inUse */
    printf("Context assigned index: %i\n", (uint32_t)contextBuffer.id);
    viv_addr_t cbuf0_physical = 0;
    void *cbuf0_logical = 0;
    size_t cbuf0_bytes = 0;
    if(viv_alloc_contiguous(contextBuffer.bufferSize, &cbuf0_physical, &cbuf0_logical, &cbuf0_bytes)!=0)
    {
        fprintf(stderr, "Error allocating contiguous host memory for context\n");
        exit(1);
    }
    printf("Allocated buffer (size 0x%x) for context: phys=%08x log=%08x\n", (int)cbuf0_bytes, (int)cbuf0_physical, (int)cbuf0_logical);
    contextBuffer.bytes = cbuf0_bytes; /* actual size of buffer */
    contextBuffer.physical = (void*)cbuf0_physical;
    contextBuffer.logical = cbuf0_logical;
    contextBuffer.link = ((uint32_t*)cbuf0_logical) + contextBuffer.linkIndex;
    contextBuffer.inUse = (gctBOOL*)(((uint32_t*)cbuf0_logical) + contextBuffer.inUseIndex);

    /* Submit second command buffer, with updated context.
     * Second command buffer fills the background.
     */
    cmdbuf2[0x1d] = cmdbuf2[0x1f] = rt_physical;
    commandBuffer.startOffset = commandBuffer.offset + 0x18; /* Make space for LINK */
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf2, sizeof(cmdbuf2));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf2);
    commandBuffer.free -= sizeof(cmdbuf2) + 0x18;
    printf("[2] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(&commandBuffer, &contextBuffer) != 0)
    {
        fprintf(stderr, "Error committing second command buffer\n");
        exit(1);
    }

    /* Submit third command buffer, with updated context
     * Third command buffer messes up the background?!?
     **/
    cmdbuf3[0x9] = aux_rt_ts_physical;
    cmdbuf3[0xb] = cmdbuf3[0x11] = cmdbuf3[0x15] = aux_rt_physical;
    cmdbuf3[0x1f] = rt_ts_physical;
    cmdbuf3[0x21] = rt_physical;
    commandBuffer.startOffset = commandBuffer.offset + 0x18;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf3, sizeof(cmdbuf3));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf3);
    commandBuffer.free -= sizeof(cmdbuf3) + 0x18;
    printf("[3] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(&commandBuffer, &contextBuffer) != 0)
    {
        fprintf(stderr, "Error committing third command buffer\n");
        exit(1);
    }

    /* Submit event queue with SIGNAL, fromWhere=gcvKERNEL_PIXEL (wait for pixel engine to finish) */
    int sig_id = 0;
    if(viv_user_signal_create(0, &sig_id) != 0) /* automatic resetting signal */
    {
        fprintf(stderr, "Cannot create user signal\n");
        exit(1);
    }
    printf("Created user signal %i\n", sig_id);
    if(viv_event_queue_signal(sig_id, gcvKERNEL_PIXEL) != 0)
    {
        fprintf(stderr, "Cannot queue GPU signal\n");
        exit(1);
    }

    /* Wait for signal */
    if(viv_user_signal_wait(sig_id, SIG_WAIT_INDEFINITE) != 0)
    {
        fprintf(stderr, "Cannot wait for signal\n");
        exit(1);
    }

    /* Allocate video memory for BITMAP, lock */
    gcuVIDMEM_NODE_PTR bmp_node = 0;
    if(viv_alloc_linear_vidmem(0x5dc00, 0x40, gcvSURF_BITMAP, gcvPOOL_DEFAULT, &bmp_node)!=0)
    {
        fprintf(stderr, "Error allocating bitmap status memory\n");
        exit(1);
    }
    printf("Allocated bitmap node: node=%08x\n", (uint32_t)bmp_node);
    
    viv_addr_t bmp_physical = 0;
    void *bmp_logical = 0;
    if(viv_lock_vidmem(bmp_node, &bmp_physical, &bmp_logical)!=0)
    {
        fprintf(stderr, "Error locking bmp memory\n");
        exit(1);
    }
    memset(bmp_logical, 0xff, 0x5dc00); /* clear previous result */
    printf("Locked bmp: phys=%08x log=%08x\n", (uint32_t)bmp_physical, (uint32_t)bmp_logical);

    /* Submit fourth command buffer, updating context */
    cmdbuf4[0x19] = rt_physical;
    cmdbuf4[0x1b] = bmp_physical;
    commandBuffer.startOffset = commandBuffer.offset + 0x18;
    memcpy((void*)((size_t)commandBuffer.logical + commandBuffer.startOffset), cmdbuf4, sizeof(cmdbuf4));
    commandBuffer.offset = commandBuffer.startOffset + sizeof(cmdbuf4);
    commandBuffer.free -= sizeof(cmdbuf4) + 0x18;
    printf("[4] startOffset=%08x, offset=%08x, free=%08x\n", (uint32_t)commandBuffer.startOffset, (uint32_t)commandBuffer.offset, (uint32_t)commandBuffer.free);
    if(viv_commit(&commandBuffer, &contextBuffer) != 0)
    {
        fprintf(stderr, "Error committing fourth command buffer\n");
        exit(1);
    }

    /* Submit event queue with SIGNAL, fromWhere=gcvKERNEL_PIXEL */
    if(viv_event_queue_signal(sig_id, gcvKERNEL_PIXEL) != 0)
    {
        fprintf(stderr, "Cannot queue GPU signal\n");
        exit(1);
    }

    /* Wait for signal */
    if(viv_user_signal_wait(sig_id, SIG_WAIT_INDEFINITE) != 0)
    {
        fprintf(stderr, "Cannot wait for signal\n");
        exit(1);
    }
    bmp_dump32(bmp_logical, 400, 240, false, "/mnt/sdcard/cube_replay.bmp");
    /* Unlock video memory */
    if(viv_unlock_vidmem(bmp_node, gcvSURF_BITMAP, 1) != 0)
    {
        fprintf(stderr, "Cannot unlock vidmem\n");
        exit(1);
    }
    /*
    for(int x=0; x<0x700; ++x)
    {
        uint32_t value = ((uint32_t*)rt_ts_logical)[x];
        printf("Sample ts: %x %08x\n", x*4, value);
    }*/
    printf("Contextbuffer used %i\n", *contextBuffer.inUse);

    viv_close();
    return 0;
}

