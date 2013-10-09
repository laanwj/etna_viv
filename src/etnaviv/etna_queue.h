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

/* Kernel command queue. Kernel commands that should be executed after a certain command buffer
 * has been processed can be added to this queue.
 * Preallocate the entire queue at a fixed size for performance.
 * The queue is flushed to the kernel after command buffer submission.
 */
#ifndef H_ETNA_QUEUE
#define H_ETNA_QUEUE

#include <etnaviv/viv.h>

struct _gcsQUEUE;
struct _gcsHAL_INTERFACE;
struct etna_ctx;

/* command queue */
struct etna_queue {
    struct etna_ctx *ctx;
    struct _gcsQUEUE *queue;
    struct _gcsQUEUE *last;
    int count;
    int max_count;
};

/* Initialize and allocate a queue.
 */
int etna_queue_create(struct etna_ctx *ctx, struct etna_queue **queue_out);

/* Return pointer to first element in queue, or NULL if queue is empty, and
 * empty the queue so that it can be used for the next flush.
 * As the queue is submitted to the kernel as a linked list, a pointer to the first element is enough
 * to represent it.
 */
struct _gcsQUEUE *_etna_queue_first(struct etna_queue *queue);

/* Allocate a new kernel command structure, add it to the queue,
 * and return a pointer to it.
 * @note the returned structure is not zero-initialized
 */
int etna_queue_alloc(struct etna_queue *queue, struct _gcsHAL_INTERFACE **cmd_out);

/** Queue synchronization signal from GPU.
 */
int etna_queue_signal(struct etna_queue *queue, int sig_id, enum viv_where fromWhere);

/** Queue free block of contiguous memory previously allocated with viv_alloc_contiguous.
 */
int etna_queue_free_contiguous(struct etna_queue *queue, size_t bytes, viv_addr_t physical, void *logical);

/** Queue unlock (unmap) video memory node from GPU and CPU memory.
 */
int etna_queue_unlock_vidmem(struct etna_queue *queue, viv_node_t node, enum viv_surf_type type);

/** Queue free block of video memory previously allocated with viv_alloc_linear_vidmem.
 */
int etna_queue_free_vidmem(struct etna_queue *queue, viv_node_t node);

/** Queue unmap user memory from GPU memory.
 */
int etna_queue_unmap_user_memory(struct etna_queue *queue, void *memory, size_t size, viv_usermem_t info, viv_addr_t address);

/* Deallocate a queue. Flushes the queue and returns all memory.
 */
int etna_queue_free(struct etna_queue *queue);

#endif

