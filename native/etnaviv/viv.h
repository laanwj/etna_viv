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

#define VIV_WAIT_INDEFINITE (0xffffffff)

/* one of the features words */
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
typedef void* viv_handle_t;

/* Memory node handle */
typedef void* viv_node_t;

/* GPU context handle */
typedef void* viv_context_t;

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
};

/* connection to driver */
struct viv_conn {
    int fd;
    enum viv_hw_type hw_type;
    
    viv_addr_t base_address;
    void *mem;
    viv_addr_t mem_base;
    viv_handle_t process;
    struct viv_specs chip;
};

/* Predefines for some kernel structures */
struct _gcsHAL_INTERFACE;
struct _gcoCMDBUF;
struct _gcsQUEUE;

/* Open connection to GPU driver.
 */
int viv_open(enum viv_hw_type hw_type, struct viv_conn **out);

/* Call ioctl interface with structure cmd as input and output.
 * @returns status (gcvSTATUS_xxx)
 */
int viv_invoke(struct viv_conn *conn, struct _gcsHAL_INTERFACE *cmd);

/* Close connection to GPU driver.
 */
int viv_close(struct viv_conn *conn);

/** Allocate contiguous GPU-mapped memory */
int viv_alloc_contiguous(struct viv_conn *conn, size_t bytes, viv_addr_t *physical, void **logical, size_t *bytes_out);

/** Allocate linear video memory.
  @returns a handle. To get the GPU and CPU address of the memory, use lock_vidmem
 */
int viv_alloc_linear_vidmem(struct viv_conn *conn, size_t bytes, size_t alignment, enum viv_surf_type type, enum viv_pool pool, viv_node_t *node, size_t *bytes_out);

/** Lock (map) video memory node to GPU and CPU memory.
 */
int viv_lock_vidmem(struct viv_conn *conn, viv_node_t node, viv_addr_t *physical, void **logical);

/** Commit GPU command buffer and context.
 */
int viv_commit(struct viv_conn *conn, struct _gcoCMDBUF *commandBuffer, viv_context_t context, struct _gcsQUEUE *queue);

/**  Unlock (unmap) video memory node from GPU and CPU memory.
 */
int viv_unlock_vidmem(struct viv_conn *conn, viv_node_t node, enum viv_surf_type type, int async);

/**  Free block of video memory previously allocated with viv_alloc_linear_vidmem.
 */
int viv_free_vidmem(struct viv_conn *conn, viv_node_t node);

/**  Free block of contiguous memory previously allocated with viv_alloc_contiguous.
 */
int viv_free_contiguous(struct viv_conn *conn, size_t bytes, viv_addr_t physical, void *logical);

/**  Map user memory to GPU memory.
 */
int viv_map_user_memory(struct viv_conn *conn, void *memory, size_t size, void **info, viv_addr_t *address);

/**  Unmap user memory from GPU memory.
 */
int viv_unmap_user_memory(struct viv_conn *conn, void *memory, size_t size, void *info, viv_addr_t address);

/** Commit event queue.
 */
int viv_event_commit(struct viv_conn *conn, struct _gcsQUEUE *queue);

/** Create a new user signal.
 *  if manualReset=0 automatic reset on WAIT
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
 */
int viv_reset(struct viv_conn *conn);

/** Convenience macro to probe features from state.xml.h: 
 * VIV_FEATURE(chipFeatures, FAST_CLEAR) 
 * VIV_FEATURE(chipMinorFeatures1, AUTO_DISABLE) 
 */
#define VIV_FEATURE(conn, word, feature) ((conn->chip.chip_features[viv_ ## word] & (word ## _ ## feature))!=0)

#endif

