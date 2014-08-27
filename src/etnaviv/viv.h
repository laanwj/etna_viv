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
/* Thin wrapper around Vivante ioctls */
#ifndef H_VIV
#define H_VIV

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

#define VIV_WAIT_INDEFINITE (0xffffffff)

/* Number of signals to keep for fences, max is 32 */
#define VIV_NUM_FENCE_SIGNALS 32

/* Return true if fence a was before b */
#define VIV_FENCE_BEFORE(a,b) ((int32_t)((b)-(a))>0)
/* Return true if fence a was before or equal to b */
#define VIV_FENCE_BEFORE_EQ(a,b) ((int32_t)((b)-(a))>=0)

/* Enum with indices for each of the feature words */
enum viv_features_word
{
    viv_chipFeatures = 0,
    viv_chipMinorFeatures0 = 1,
    viv_chipMinorFeatures1 = 2,
    viv_chipMinorFeatures2 = 3,
    viv_chipMinorFeatures3 = 4,
    VIV_FEATURES_WORD_COUNT /* Must be last */
};

/* hardware type */
/* matches gceHARDWARE_TYPE enums */
enum viv_hw_type
{
    VIV_HW_3D = 1,
    VIV_HW_2D = 2,
    VIV_HW_VG = 4,

    VIV_HW_2D3D = VIV_HW_3D | VIV_HW_2D
};

/* Surface types */
enum viv_surf_type
{
    VIV_SURF_UNKNOWN,
    VIV_SURF_INDEX,
    VIV_SURF_VERTEX,
    VIV_SURF_TEXTURE,
    VIV_SURF_RENDER_TARGET,
    VIV_SURF_DEPTH,
    VIV_SURF_BITMAP,
    VIV_SURF_TILE_STATUS,
    VIV_SURF_IMAGE,
    VIV_SURF_MASK,
    VIV_SURF_SCISSOR,
    VIV_SURF_HIERARCHICAL_DEPTH
};

/* Video memory pool type. */
enum viv_pool
{
    VIV_POOL_UNKNOWN,
    VIV_POOL_DEFAULT,
    VIV_POOL_LOCAL,
    VIV_POOL_LOCAL_INTERNAL,
    VIV_POOL_LOCAL_EXTERNAL,
    VIV_POOL_UNIFIED,
    VIV_POOL_SYSTEM,
    VIV_POOL_VIRTUAL,
    VIV_POOL_USER,
    VIV_POOL_CONTIGUOUS
};

/* Semaphore recipient */
enum viv_where
{
    VIV_WHERE_COMMAND,
    VIV_WHERE_PIXEL
};

/* Status code from kernel.
 * These numbers must match gcvSTATUS_*.
 */
enum viv_status
{
    VIV_STATUS_OK                    =   0,
    VIV_STATUS_TRUE                  =   1,
    VIV_STATUS_NOT_OUR_INTERRUPT     =   6,
    VIV_STATUS_CHIP_NOT_READY        =   11,
    VIV_STATUS_SKIP                  =   13,
    VIV_STATUS_EXECUTED              =   18,
    VIV_STATUS_TERMINATE             =   19,

    VIV_STATUS_INVALID_ARGUMENT      =   -1,
    VIV_STATUS_INVALID_OBJECT        =   -2,
    VIV_STATUS_OUT_OF_MEMORY         =   -3,
    VIV_STATUS_MEMORY_LOCKED         =   -4,
    VIV_STATUS_MEMORY_UNLOCKED       =   -5,
    VIV_STATUS_HEAP_CORRUPTED        =   -6,
    VIV_STATUS_GENERIC_IO            =   -7,
    VIV_STATUS_INVALID_ADDRESS       =   -8,
    VIV_STATUS_CONTEXT_LOSSED        =   -9,
    VIV_STATUS_TOO_COMPLEX           =   -10,
    VIV_STATUS_BUFFER_TOO_SMALL      =   -11,
    VIV_STATUS_INTERFACE_ERROR       =   -12,
    VIV_STATUS_NOT_SUPPORTED         =   -13,
    VIV_STATUS_MORE_DATA             =   -14,
    VIV_STATUS_TIMEOUT               =   -15,
    VIV_STATUS_OUT_OF_RESOURCES      =   -16,
    VIV_STATUS_INVALID_DATA          =   -17,
    VIV_STATUS_INVALID_MIPMAP        =   -18,
    VIV_STATUS_NOT_FOUND             =   -19,
    VIV_STATUS_NOT_ALIGNED           =   -20,
    VIV_STATUS_INVALID_REQUEST       =   -21,
    VIV_STATUS_GPU_NOT_RESPONDING    =   -22,
    VIV_STATUS_TIMER_OVERFLOW        =   -23,
    VIV_STATUS_VERSION_MISMATCH      =   -24,
    VIV_STATUS_LOCKED                =   -25,
    VIV_STATUS_INTERRUPTED           =   -26,
    VIV_STATUS_DEVICE                =   -27,
};

/* Type for GPU physical address */
typedef uint32_t viv_addr_t;

/* General process handle */
typedef uint64_t viv_handle_t;

/* Memory node handle */
typedef uint64_t viv_node_t;

/* GPU context handle */
typedef uint64_t viv_context_t;

/* User memory info handle */
typedef uint64_t viv_usermem_t;

/* kernel-interface independent chip specs structure, this is much easier to use
 * than checking GCABI defines all the time.
 */
struct viv_specs {
    uint32_t chip_model;
    uint32_t chip_revision;
    uint32_t chip_features[VIV_FEATURES_WORD_COUNT];
    uint32_t stream_count;
    uint32_t register_max;
    uint32_t thread_count;
    uint32_t shader_core_count;
    uint32_t vertex_cache_size;
    uint32_t vertex_output_buffer_size;
    uint32_t pixel_pipes;
    uint32_t instruction_count;
    uint32_t num_constants;
    uint32_t buffer_size;
    uint32_t varyings_count;
};

struct viv_kernel_driver_version {
    char name[40];
    int major, minor, patch, build;
};

/* Structure encompassing a connection to kernel driver */
struct viv_conn {
    int fd;
    enum viv_hw_type hw_type;

    viv_addr_t base_address;
    void *mem;
    size_t mem_length;
    viv_addr_t mem_base;
    viv_handle_t process;
    struct viv_specs chip;
    struct viv_kernel_driver_version kernel_driver;
    /* signals for fences */
    int fence_signals[VIV_NUM_FENCE_SIGNALS];
    /* guard these with a mutex, so
     * that no races happen and command buffers are submitted
     * in the same order as the fence ids.
     */
    pthread_mutex_t fence_mutex;
    uint32_t next_fence_id; /* Next fence number to be dealt */
    uint32_t fences_pending; /* Bitmask of fences signalled but not yet waited for */
    uint32_t last_fence_id; /* Most recent signalled fence */
};

/* Predefines for some kernel structures */
struct _gcsHAL_INTERFACE;
struct _gcoCMDBUF;
struct _gcsQUEUE;

/* Open a new connection to the GPU driver.
 */
int viv_open(enum viv_hw_type hw_type, struct viv_conn **out);

/* Call ioctl interface with structure cmd as input and output.
 * @returns status (gcvSTATUS_xxx)
 */
int viv_invoke(struct viv_conn *conn, struct _gcsHAL_INTERFACE *cmd);

/* Close connection to GPU driver and free temporary structures
 * and mapping.
 */
int viv_close(struct viv_conn *conn);

/** Allocate a span of physical contiguous GPU-mapped memory */
int viv_alloc_contiguous(struct viv_conn *conn, size_t bytes, viv_addr_t *physical, void **logical, size_t *bytes_out);

/** Allocate linear video memory.
  @returns a handle. To get the GPU and CPU address of the memory, use lock_vidmem
 */
int viv_alloc_linear_vidmem(struct viv_conn *conn, size_t bytes, size_t alignment, enum viv_surf_type type, enum viv_pool pool, viv_node_t *node, size_t *bytes_out);

/** Lock (map) video memory node to GPU and CPU memory.
 * Video memory needs to be locked to be used by either the CPU or GPU.
 */
int viv_lock_vidmem(struct viv_conn *conn, viv_node_t node, viv_addr_t *physical, void **logical);

/** Commit GPU command buffer and context. This submits a batch of commands.
 */
int viv_commit(struct viv_conn *conn, struct _gcoCMDBUF *commandBuffer, viv_context_t context, struct _gcsQUEUE *queue);

/** Unlock (unmap) video memory node from GPU and CPU memory.
 *
 * If submit_as_event is set, submit the unlock as an event immediately, otherwise send it as command.
 * If submit_as_event is not set, the function will return 0 or 1 in *async depending on whether a second
 * unlock stage must be submitted as event (either through this function with submit_as_event=true
 * or through etna_queue_unlock_vidmem).
 */
int viv_unlock_vidmem(struct viv_conn *conn, viv_node_t node, enum viv_surf_type type, bool submit_as_event, int *async);

/** Free a block of video memory previously allocated with viv_alloc_linear_vidmem.
 */
int viv_free_vidmem(struct viv_conn *conn, viv_node_t node, bool submit_as_event);

/** Free block of contiguous memory previously allocated with viv_alloc_contiguous.
 */
int viv_free_contiguous(struct viv_conn *conn, size_t bytes, viv_addr_t physical, void *logical);

/** Map a dmabuf to GPU memory.
 */
int viv_map_dmabuf(struct viv_conn *conn, int fd, viv_usermem_t *info, viv_addr_t *address, int prot);

/** Map user memory to GPU memory, allowing for read/write protections.
 * Note: GPU is not protected against reads/writes.
 */
int viv_map_user_memory_prot(struct viv_conn *conn, void *memory, size_t size, int prot, viv_usermem_t *info, viv_addr_t *address);

/** Map user memory to GPU memory.
 */
int viv_map_user_memory(struct viv_conn *conn, void *memory, size_t size, viv_usermem_t *info, viv_addr_t *address);

/** Unmap user memory from GPU memory.
 */
int viv_unmap_user_memory(struct viv_conn *conn, void *memory, size_t size, viv_usermem_t info, viv_addr_t address);

/** Commit event queue.
 */
int viv_event_commit(struct viv_conn *conn, struct _gcsQUEUE *queue);

/** Create a new user signal.
 *  if manualReset=0 automatic reset on completion of signal_wait
 *     manualReset=1 need to manually reset state to 0 using SIGNAL
 */
int viv_user_signal_create(struct viv_conn *conn, int manualReset, int *id_out);

/** Set user signal state.
 */
int viv_user_signal_signal(struct viv_conn *conn, int sig_id, int state);

/** Wait for signal.
 * @param[in] wait Provide time to wait in milliseconds, or VIV_WAIT_INDEFINITE.
 */
int viv_user_signal_wait(struct viv_conn *conn, int sig_id, int wait);

/** Destroy signal created with viv_user_signal_create.
 */
int viv_user_signal_destroy(struct viv_conn *conn, int sig_id);

void viv_show_chip_info(struct viv_conn *conn);

/** Send reset command to GPU.
 * This is supposed to fix a hung state, but its effectiveness depends on the SoC.
 */
int viv_reset(struct viv_conn *conn);

/** Read register from GPU.
 * @note Needs kernel module compiled with user space register access
 * (gcdREGISTER_ACCESS_FROM_USER=1)
 */
int viv_read_register(struct viv_conn *conn, uint32_t address, uint32_t *data);

/** Write register to GPU.
 * @note Needs kernel module compiled with user space register access
 * (gcdREGISTER_ACCESS_FROM_USER=1)
 */
int viv_write_register(struct viv_conn *conn, uint32_t address, uint32_t data);

/** Internal: Request a new fence handle and return it. Also return signal id
 * associated with the fence, to add to queue.
 * @note must be called with fence_mutex held.
 */
int _viv_fence_new(struct viv_conn *conn, uint32_t *fence_out, int *signal_out);

/** Internal: Mark a fence as pending.
 * Call this only after submitting the signal to the kernel.
 * @note must be called with fence_mutex held.
 */
void _viv_fence_mark_pending(struct viv_conn *conn, uint32_t fence);

/** Wait for fence or poll status.
 * Timeout is in milliseconds.
 * Pass a timeout of 0 to poll fence status, or VIV_WAIT_INDEFINITE to wait forever.
 * @return VIV_STATUS_OK if fence finished
 *         VIV_STATUS_TIMEOUT if timeout expired first
 *         other if an error occured
 */
int viv_fence_finish(struct viv_conn *conn, uint32_t fence, uint32_t timeout);

/** Convenience macro to probe features from state.xml.h:
 * VIV_FEATURE(chipFeatures, FAST_CLEAR)
 * VIV_FEATURE(chipMinorFeatures1, AUTO_DISABLE)
 */
#define VIV_FEATURE(conn, word, feature) ((conn->chip.chip_features[viv_ ## word] & (word ## _ ## feature))!=0)

#endif

