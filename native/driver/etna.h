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

/* Low-level command buffer building and submission, abstracts away specific kernel interface 
 * as much as practically possible. */
#ifndef H_ETNA
#define H_ETNA

#include "gc_hal_base.h"
#include "gc_hal.h"
#include "gc_hal_driver.h"
#include "gc_hal_user_context.h"
#include "gc_hal_types.h"

#include "common.xml.h"
#include "cmdstream.xml.h"

#include <stdint.h>
#include <stdlib.h>
#ifdef DEBUG
#include <stdio.h>
#endif

/* Number of command buffers, to be used in a circular fashion.
 */
#define NUM_COMMAND_BUFFERS 5

/* Number of bytes in one command buffer */
#define COMMAND_BUFFER_SIZE (0x8000)

/* Constraints to command buffer layout:
 *
 * - Keep 8 words (32 bytes) at beginning of commit (for kernel to add optional PIPE switch)
 * - Keep 6 words (24 bytes) between end of one commit and beginning of next, or at the end of a buffer (for kernel to add LINK)
 * These reserved areas can be left uninitialized, as they are written by the kernel.
 *
 * Synchronization:
 *
 * - Create N command buffers, with a signal for each buffer
 * - Before starting to write to a buffer, make sure it is free by waiting for buffer's sync signal
 * - After a buffer is full, queue the buffer's sync signal and switch to next buffer
 *
 */
#define BEGIN_COMMIT_CLEARANCE 32
#define END_COMMIT_CLEARANCE 24

#define ETNA_NEW(type) calloc(1, sizeof(type))
#define ETNA_FREE(ptr) free(ptr)

/** Structure definitions */

/* Etna error (return) codes */
typedef enum _etna_status {
    ETNA_OK,
    ETNA_INVALID_ADDR,
    ETNA_INVALID_VALUE,
    ETNA_OUT_OF_MEMORY,
    ETNA_INTERNAL_ERROR,
    ETNA_ALREADY_LOCKED
} etna_status;

/* HW pipes.
 * Used by GPU to tell front-end what back-end modules to synchronize operations with. 
 */
typedef enum _etna_pipe {
    ETNA_PIPE_3D = 0,
    ETNA_PIPE_2D = 1
} etna_pipe;

struct etna_ctx {
    /* Driver connection */
    struct viv_conn *conn;
    /* Keep track of current command buffer and writing location.
     * The offset is kept here instead of in cmdbuf[cur_buf].offset to save an level of indirection
     * when building the buffer. It is only copied to the command buffer before submission to the kernel
     * in etna_flush().
     * Also, this offset is in terms of 32 bit words, instead of in bytes, so it can be directly used to index
     * into buf.
     */
    uint32_t *buf;
    uint32_t offset;
    /* Current buffer id (index into cmdbuf) */
    int cur_buf;
    /* Synchronization signal for finish() */
    int sig_id;
    /* Status for updating context */
    int num_vertex_elements; /* number of active vertex elements */
    /* Structures for kernel */
    struct _gcoCMDBUF cmdbuf[NUM_COMMAND_BUFFERS];
    int cmdbuf_sig[NUM_COMMAND_BUFFERS]; /* sync signals for command buffers */
    struct _gcoCONTEXT ctx;
};

/** Convenience macros for command buffer building, remember to reserve enough space before using them */
/* Queue load state command header (queues one word) */
#define ETNA_EMIT_LOAD_STATE(ctx, ofs, count, fixp) \
    (ctx)->buf[(ctx)->offset++] = \
    (VIV_FE_LOAD_STATE_HEADER_OP_LOAD_STATE | ((fixp)?VIV_FE_LOAD_STATE_HEADER_FIXP:0) | \
     VIV_FE_LOAD_STATE_HEADER_OFFSET(ofs) | \
     (VIV_FE_LOAD_STATE_HEADER_COUNT(count) & VIV_FE_LOAD_STATE_HEADER_COUNT__MASK))

/* Queues one value (1 word) */
#define ETNA_EMIT(ctx, value) \
    (ctx)->buf[(ctx)->offset++] = (value)

/* Draw array primitives (queues 4 words) */
#define ETNA_EMIT_DRAW_PRIMITIVES(ctx, cmd, start, count) \
    do { (ctx)->buf[(ctx)->offset++] = VIV_FE_DRAW_PRIMITIVES_HEADER_OP_DRAW_PRIMITIVES; \
      (ctx)->buf[(ctx)->offset++] = cmd; \
      (ctx)->buf[(ctx)->offset++] = start; \
      (ctx)->buf[(ctx)->offset++] = count; } while(0)

/* Draw indexed primitives (queues 5 words) */
#define ETNA_EMIT_DRAW_INDEXED_PRIMITIVES(ctx, cmd, start, count, offset) \
    do { (ctx)->buf[(ctx)->offset++] = VIV_FE_DRAW_INDEXED_PRIMITIVES_HEADER_OP_DRAW_INDEXED_PRIMITIVES; \
      (ctx)->buf[(ctx)->offset++] = cmd; \
      (ctx)->buf[(ctx)->offset++] = start; \
      (ctx)->buf[(ctx)->offset++] = count; \
      (ctx)->buf[(ctx)->offset++] = offset; } while(0)

/* Queue a STALL command (queues 2 words) */
#define ETNA_EMIT_STALL(ctx, from, to) \
    do { (ctx)->buf[(ctx)->offset++] = VIV_FE_STALL_HEADER_OP_STALL; \
      (ctx)->buf[(ctx)->offset++] = VIV_FE_STALL_TOKEN_FROM(from) | VIV_FE_STALL_TOKEN_TO(to); } while(0)

/* Round current offset to 64-bit */
#define ETNA_ALIGN(ctx) ctx->offset = (ctx->offset + 1)&(~1);

/* macro for MASKED() (multiple can be &ed) */
#define ETNA_MASKED(NAME, VALUE) (~(NAME ## _MASK | NAME ## __MASK) | ((VALUE)<<(NAME ## __SHIFT)))
/* for boolean bits */
#define ETNA_MASKED_BIT(NAME, VALUE) (~(NAME ## _MASK | NAME) | ((VALUE) ? NAME : 0))
/* for inline enum bit fields
 */
#define ETNA_MASKED_INL(NAME, VALUE) (~(NAME ## _MASK | NAME ## __MASK) | (NAME ## _ ## VALUE))

/* Create new etna context.
 * Return error when creation fails.
 */
int etna_create(struct viv_conn *conn, struct etna_ctx **ctx);

/* Free an etna context. */
int etna_free(struct etna_ctx *ctx);

/* internal (non-inline) part of etna_reserve.
   only to be used from etna_reserve. */
int _etna_reserve_internal(struct etna_ctx *ctx, size_t n);

/* Reserve space for writing N 32-bit command words. It is allowed to reserve
 * more than is written, but not less, as this will result in a buffer overflow.
 * ctx->offset will point to the reserved area on succesful return.
 * It will always be 64-bit aligned so that a new command can be started.
 * @return OK on success, error code otherwise
 */
static inline int etna_reserve(struct etna_ctx *ctx, size_t n)
{
    if(ctx == NULL)
        return ETNA_INVALID_ADDR;
    if(ctx->cur_buf != -1)
    {
        gcoCMDBUF cur_buf = &ctx->cmdbuf[ctx->cur_buf];
#ifdef DEBUG
        printf("etna_reserve: %i at offset %i\n", (int)n, (int)ctx->offset);
#endif
        ETNA_ALIGN(ctx);

        if(((ctx->offset + n)*4 + END_COMMIT_CLEARANCE) <= cur_buf->bytes) /* enough bytes free in buffer */
        {
            return ETNA_OK;
        }
    }
    return _etna_reserve_internal(ctx, n);
}

/* Set GPU pipe (ETNA_PIPE_2D, ETNA_PIPE_3D).
 */
int etna_set_pipe(struct etna_ctx *ctx, etna_pipe pipe);

/* Send currently queued commands to kernel.
 * @return OK on success, error code otherwise
 */
int etna_flush(struct etna_ctx *ctx);

/* Send currently queued commands to kernel, then block for them to finish.
 * @return OK on success, error code otherwise
 */
int etna_finish(struct etna_ctx *ctx);

/* Queue a semaphore (but don't stall).
 * from, to are values from SYNC_RECIPIENT_*.
 * @return OK on success, error code otherwise
 */
int etna_semaphore(struct etna_ctx *ctx, uint32_t from, uint32_t to);

/* Queue a semaphore and stall.
 * from, to are values from SYNC_RECIPIENT_*.
 * @return OK on success, error code otherwise
 */
int etna_stall(struct etna_ctx *ctx, uint32_t from, uint32_t to);

/* print command buffer for debugging */
void etna_dump_cmd_buffer(struct etna_ctx *ctx);

#endif
