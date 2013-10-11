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

/* Buffer building and submission, abstracts away specific kernel interface
 * as much as practically possible.
 */
#ifndef H_ETNA
#define H_ETNA

#include <etnaviv/common.xml.h>
#include <etnaviv/cmdstream.xml.h>
#include <etnaviv/etna_util.h>
#include <etnaviv/viv.h>

#include <stdint.h>
#include <stdlib.h>
#ifdef DEBUG
#include <stdio.h>
#endif
#include <string.h> /* for memcpy */

/* Number of command buffers, to be used in a circular fashion.
 */
#define NUM_COMMAND_BUFFERS 5

/* Special command buffer ids */
#define ETNA_NO_BUFFER (-1)
#define ETNA_CTX_BUFFER (-2)

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

/** Structure definitions */

/* Etna error (return) codes */
enum etna_status {
    ETNA_OK             = 0,    /* = VIV_STATUS_OK */
    ETNA_INVALID_ADDR   = 1000, /* Don't overlap with VIV_STATUS_* */
    ETNA_INVALID_VALUE  = 1001,
    ETNA_OUT_OF_MEMORY  = 1002,
    ETNA_INTERNAL_ERROR = 1003,
    ETNA_ALREADY_LOCKED = 1004
};

/* HW pipes.
 * Used by GPU to tell front-end what back-end modules to synchronize operations with.
 */
enum etna_pipe {
    ETNA_PIPE_3D = 0,
    ETNA_PIPE_2D = 1
};

struct _gcoCMDBUF;
struct etna_queue;
struct etna_ctx;
struct etna_bo;

struct etna_context_info {
    size_t bytes;
    viv_addr_t physical;
    void *logical;
};

typedef int (*etna_context_snapshot_cb_t)(void *data, struct etna_ctx *ctx,
        enum etna_pipe *initial_pipe, enum etna_pipe *final_pipe);

struct etna_cmdbuf {
    /* sync signal for command buffer */
    int sig_id;
    struct etna_bo *bo;
};

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
    /* Stored current buffer id when building context */
    int stored_buf;
    /* Synchronization signal for finish() */
    int sig_id;
    /* Structures for kernel */
    struct _gcoCMDBUF *cmdbuf[NUM_COMMAND_BUFFERS];
    /* Extra information per command buffer */
    struct etna_cmdbuf cmdbufi[NUM_COMMAND_BUFFERS];
    /* number of unsignalled flushes (used to work around kernel bug) */
    int flushes;
    /* context */
    viv_context_t ctx;
    struct etna_bo *ctx_bo;
    etna_context_snapshot_cb_t ctx_cb;
    void *ctx_cb_data;
    /* command queue */
    struct etna_queue *queue;
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

/* Draw indexed primitives (queues 6 words) */
#define ETNA_EMIT_DRAW_INDEXED_PRIMITIVES(ctx, cmd, start, count, offset) \
    do { (ctx)->buf[(ctx)->offset++] = VIV_FE_DRAW_INDEXED_PRIMITIVES_HEADER_OP_DRAW_INDEXED_PRIMITIVES; \
      (ctx)->buf[(ctx)->offset++] = cmd; \
      (ctx)->buf[(ctx)->offset++] = start; \
      (ctx)->buf[(ctx)->offset++] = count; \
      (ctx)->buf[(ctx)->offset++] = offset; \
      (ctx)->offset++; } while(0)

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
    if(ctx->cur_buf != ETNA_NO_BUFFER)
    {
#ifdef CMD_DEBUG
        printf("etna_reserve: %i at offset %i\n", (int)n, (int)ctx->offset);
#endif
        ETNA_ALIGN(ctx);

        if(((ctx->offset + n)*4 + END_COMMIT_CLEARANCE) <= COMMAND_BUFFER_SIZE) /* enough bytes free in buffer */
        {
            return ETNA_OK;
        }
    }
    return _etna_reserve_internal(ctx, n);
}

/* Set GPU pipe (ETNA_PIPE_2D, ETNA_PIPE_3D).
 */
int etna_set_pipe(struct etna_ctx *ctx, enum etna_pipe pipe);

/* Send currently queued commands to kernel.
 * @return OK on success, error code otherwise
 */
int etna_flush(struct etna_ctx *ctx, uint32_t *fence_out);

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

/** Set callback for building context before flush.
 * Any etna state update commands called inside this callback function will be
 * part of context.
 * @note This callback is never called if the kernel driver does not use contexts.
 * @return OK on success, error code otherwise
 */
int etna_set_context_cb(struct etna_ctx *ctx, etna_context_snapshot_cb_t snapshot_cb, void *data);

/* print command buffer for debugging */
void etna_dump_cmd_buffer(struct etna_ctx *ctx);

/**
 * Direct state setting functions; these can be used for convenience. When absolute performance
 * is required while updating big blocks of state at once, it is recommended to use the
 * ETNA_EMIT_* macros and etna_reserve directly.
 */
static inline void etna_set_state(struct etna_ctx *cmdbuf, uint32_t address, uint32_t value)
{
    etna_reserve(cmdbuf, 2);
    ETNA_EMIT_LOAD_STATE(cmdbuf, address >> 2, 1, 0);
    ETNA_EMIT(cmdbuf, value);
}

static inline void etna_set_state_multi(struct etna_ctx *cmdbuf, uint32_t base, uint32_t num, const uint32_t *values)
{
    if(num == 0) return;
    etna_reserve(cmdbuf, 1 + num + 1); /* 1 extra for potential alignment */
    ETNA_EMIT_LOAD_STATE(cmdbuf, base >> 2, num, 0);
    memcpy(&cmdbuf->buf[cmdbuf->offset], values, 4*num);
    cmdbuf->offset += num;
    ETNA_ALIGN(cmdbuf);
}

static inline void etna_set_state_f32(struct etna_ctx *cmdbuf, uint32_t address, float value)
{
    etna_set_state(cmdbuf, address, etna_f32_to_u32(value));
}
static inline void etna_set_state_fixp(struct etna_ctx *cmdbuf, uint32_t address, uint32_t value)
{
    etna_reserve(cmdbuf, 2);
    ETNA_EMIT_LOAD_STATE(cmdbuf, address >> 2, 1, 1);
    ETNA_EMIT(cmdbuf, value);
}
static inline void etna_set_state_fixp_multi(struct etna_ctx *cmdbuf, uint32_t address, uint32_t num, uint32_t *values)
{
    etna_reserve(cmdbuf, 1 + num + 1); /* 1 extra for potential alignment */
    ETNA_EMIT_LOAD_STATE(cmdbuf, address >> 2, num, 1);
    memcpy(&cmdbuf->buf[cmdbuf->offset], values, 4*num);
    cmdbuf->offset += num;
    ETNA_ALIGN(cmdbuf);
}
static inline void etna_draw_primitives(struct etna_ctx *cmdbuf, uint32_t primitive_type, uint32_t start, uint32_t count)
{
#ifdef CMD_DEBUG
    printf("draw_primitives %08x %08x %08x %08x\n",
            VIV_FE_DRAW_PRIMITIVES_HEADER_OP_DRAW_PRIMITIVES,
            primitive_type, start, count);
#endif
    etna_reserve(cmdbuf, 4);
    ETNA_EMIT_DRAW_PRIMITIVES(cmdbuf, primitive_type, start, count);
}
static inline void etna_draw_indexed_primitives(struct etna_ctx *cmdbuf, uint32_t primitive_type, uint32_t start, uint32_t count, uint32_t offset)
{
#ifdef CMD_DEBUG
    printf("draw_primitives_indexed %08x %08x %08x %08x\n",
            VIV_FE_DRAW_PRIMITIVES_HEADER_OP_DRAW_INDEXED_PRIMITIVES,
            primitive_type, start, count);
#endif
    etna_reserve(cmdbuf, 5+1);
    ETNA_EMIT_DRAW_INDEXED_PRIMITIVES(cmdbuf, primitive_type, start, count, offset);
}

/* ETNA_COALESCE
 *
 * Mechanism to emit state changes and join consecutive
 * state updates into single SET_STATE commands when possible.
 *
 * Usage:
 * - Set state words with ETNA_COALESCE_STATE_UPDATE.
 *
 * - Before starting the state update, reserve space using EMIT_STATE_OPEN(max_updates),
 *   where max_updates is the maximum number of possible updates that will be emitted between
 *   this ETNA_COALESCE_STATE_OPEN .. ETNA_COALESCE_STATE_CLOSE pair.
 *
 * - When done with updating, call ETNA_COALESCE_STATE_CLOSE.
 *
 * In the scope where these macros are used, define the variables
 *     uint32_t last_reg,  -> last register to write to
 *       last_fixp,   -> fixp conversion flag for last written register
 *       span_start   -> start of span in command buffer
 * for state tracking.
 *
 * It works by keeping track of the last register that was written to plus one,
 * thus the next register that will be written. If the register number to be written
 * matches this next register, add it to the current span. If not, close the span
 * and open a new one.
 */
#define ETNA_COALESCE_STATE_OPEN(max_updates) \
    etna_reserve(ctx, (max_updates) * 2); \
    span_start = ctx->offset; \
    last_reg = last_fixp = 0;

#define ETNA_COALESCE_STATE_CLOSE() \
    uint32_t span_size = ctx->offset - span_start; \
    if(span_size) { (ctx)->buf[span_start - 1] |= VIV_FE_LOAD_STATE_HEADER_COUNT(span_size); } \
    ETNA_ALIGN(ctx);

#define ETNA_COALESCE_STATE_UPDATE(state_name, src_value, fixp) \
    { \
        if(last_reg != (VIVS_##state_name) || (fixp) != last_fixp) \
        { \
            ETNA_COALESCE_STATE_CLOSE() \
            ETNA_EMIT_LOAD_STATE(ctx, (VIVS_##state_name) >> 2, 0, (fixp));  \
            span_start = ctx->offset; \
        } \
        ETNA_EMIT(ctx, src_value); \
        last_reg = VIVS_##state_name + 4; last_fixp = (fixp); \
    }

#endif
