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

#include "gc_abi.h"
#include "gc_hal_base.h"
#include "gc_hal.h"
#include "gc_hal_driver.h"
#include "gc_hal_user_context.h"
#include "gc_hal_types.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#define SIG_WAIT_INDEFINITE (0xffffffff)

/* one of the features words */
enum viv_features_word
{
    viv_chipFeatures,
    viv_chipMinorFeatures0,
    viv_chipMinorFeatures1,
    viv_chipMinorFeatures2,
    viv_chipMinorFeatures3
};

/* hardware type */
enum viv_hw_type
{
    VIV_HW_3D = 1,
    VIV_HW_2D = 2,
    VIV_HW_VG = 4,

    VIV_HW_2D3D = VIV_HW_3D | VIV_HW_2D
};

/* connection to driver */
struct viv_conn {
    int fd;
    enum viv_hw_type hw_type;
};

/* Type for GPU physical address */
typedef uint32_t viv_addr_t;

/* Open connection to GPU driver.
 */
int viv_open(enum viv_hw_type hw_type, struct viv_conn **out);

/* Call ioctl interface with structure cmd as input and output.
 * @returns status (gcvSTATUS_xxx)
 */
int viv_invoke(struct viv_conn *conn, gcsHAL_INTERFACE *cmd);

/* Close connection to GPU driver.
 */
int viv_close(struct viv_conn *conn);

/** Allocate contiguous GPU-mapped memory */
int viv_alloc_contiguous(struct viv_conn *conn, size_t bytes, viv_addr_t *physical, void **logical, size_t *bytes_out);

/** Allocate linear video memory.
  @returns a handle. To get the GPU and CPU address of the memory, use lock_vidmem
 */
int viv_alloc_linear_vidmem(struct viv_conn *conn, size_t bytes, size_t alignment, gceSURF_TYPE type, gcePOOL pool, gcuVIDMEM_NODE_PTR *node, size_t *bytes_out);

/** Lock (map) video memory node to GPU and CPU memory.
 */
int viv_lock_vidmem(struct viv_conn *conn, gcuVIDMEM_NODE_PTR node, viv_addr_t *physical, void **logical);

/** Commit GPU command buffer and context.
 */
int viv_commit(struct viv_conn *conn, gcoCMDBUF commandBuffer, gcoCONTEXT contextBuffer);

/**  Unlock (unmap) video memory node from GPU and CPU memory.
 */
int viv_unlock_vidmem(struct viv_conn *conn, gcuVIDMEM_NODE_PTR node, gceSURF_TYPE type, int async);

/**  Free block of video memory previously allocated with viv_alloc_linear_vidmem.
 */
int viv_free_vidmem(struct viv_conn *conn, gcuVIDMEM_NODE_PTR node);

/**  Map user memory to GPU memory.
 */
int viv_map_user_memory(struct viv_conn *conn, void *memory, size_t size, gctPOINTER *info, viv_addr_t *address);

/**  Unmap user memory from GPU memory.
 */
int viv_unmap_user_memory(struct viv_conn *conn, void *memory, size_t size, gctPOINTER info, viv_addr_t address);

/** Commit event queue.
 */
int viv_event_commit(struct viv_conn *conn, gcsQUEUE *queue);

/** Create a new user signal.
 *  if manualReset=0 automatic reset on WAIT
 *     manualReset=1 need to manually reset state to 0 using SIGNAL
 */
int viv_user_signal_create(struct viv_conn *conn, int manualReset, int *id_out);

/** Set user signal state.
 */
int viv_user_signal_signal(struct viv_conn *conn, int sig_id, int state);

/** Wait for signal. Provide time to wait in milliseconds, or SIG_WAIT_INDEFINITE.
 */
int viv_user_signal_wait(struct viv_conn *conn, int sig_id, int wait);

/** Destroy signal created with viv_user_signal_create.
 */
int viv_user_signal_destroy(struct viv_conn *conn, int sig_id);

/** Queue synchronization signal from GPU.
 */
int viv_event_queue_signal(struct viv_conn *conn, int sig_id, gceKERNEL_WHERE fromWhere);

void viv_show_chip_info(struct viv_conn *conn);

/** Send reset command to GPU.
 */
int viv_reset(struct viv_conn *conn);

/** Query for a GPU feature.
 */
bool viv_query_feature(struct viv_conn *conn, enum viv_features_word, uint32_t bits);

/** Convenience macro to probe features from state.xml.h: 
 * VIV_FEATURE(chipFeatures, FAST_CLEAR) 
 * VIV_FEATURE(chipMinorFeatures1, AUTO_DISABLE) 
 */
#define VIV_FEATURE(conn, word, feature) viv_query_feature(conn, viv_ ## word, word ## _ ## feature)

#endif

