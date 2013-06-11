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
#include <etnaviv/etna.h>
#include <etnaviv/viv.h>
#include <etnaviv/state.xml.h>

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "etna_context_cmd.h"

//#define DEBUG
//#define DEBUG_CMDBUF

/* TODO: don't forget to handle FE.VERTEX_ELEMENT_CONFIG (0x0600....0x0063c) specially;
 * fields need to be written in the right order, and only as many should be written as there are
 * used vertex elements.
 * header: contextbuf[contextbuf_addr[i].index - 1] where contextbuf_addr[i].address == 0x600
 */

/* Initialize kernel GPU context and state map */
#ifdef GCABI_HAS_CONTEXT
static int initialize_gpu_context(struct viv_conn *conn, gcoCONTEXT vctx)
{
    /* First build context state map from compressed representation */
    size_t contextbuf_addr_size = sizeof(contextbuf_addr)/sizeof(address_index_t);
    size_t state_count = contextbuf_addr[contextbuf_addr_size - 1].address / 4 + 1;
    uint32_t *context_map = ETNA_MALLOC(state_count * 4);
    if(context_map == NULL)
    {
        return ETNA_OUT_OF_MEMORY;
    }
    memset(context_map, 0, state_count*4);
    for(int idx=0; idx<contextbuf_addr_size; ++idx)
    {
        context_map[contextbuf_addr[idx].address / 4] = contextbuf_addr[idx].index;
    }
#ifdef DEBUG
    printf("Initialized state map (%x)\n", state_count);
#endif
    /* fill in context */
    vctx->object.type = gcvOBJ_CONTEXT;
    vctx->id = 0x0; // Actual ID will be returned here by kernel
    vctx->map = context_map;
    vctx->stateCount = state_count;
    vctx->buffer = ETNA_MALLOC(sizeof(contextbuf));
    memcpy(vctx->buffer, contextbuf, sizeof(contextbuf)); /* copy over hardcoded context command buffer */
    vctx->pipe3DIndex = 0x2d6; // XXX should not be hardcoded
    vctx->pipe2DIndex = 0x106e; // XXX should not be hardcoded
    vctx->linkIndex = 0x1076; // XXX should not be hardcoded
    vctx->inUseIndex = 0x1078; // XXX should not be hardcoded
    vctx->bufferSize = sizeof(contextbuf);
#ifdef GCABI_CONTEXT_HAS_PHYSICAL /* Are these used by the kernel on other platforms? otherwise better to remove them completely */
    vctx->bytes = 0x0; // Number of bytes at actually allocated for physical, logical
    vctx->physical = (void*)0x0;
#endif
    vctx->logical = (void*)0x0;
    vctx->link = (void*)0x0; // Logical address of link (within consecutive array)
    vctx->initialPipe = ETNA_PIPE_2D;
    vctx->entryPipe = ETNA_PIPE_3D;
    vctx->currentPipe = ETNA_PIPE_3D; // not used by kernel
    vctx->postCommit = 1; // not used by kernel
    vctx->inUse = (int*)0x0; // Logical address of inUse (within consecutive array)

    viv_addr_t cbuf0_physical = 0;
    void *cbuf0_logical = 0;
    size_t cbuf0_bytes = 0;
    if(viv_alloc_contiguous(conn, vctx->bufferSize, &cbuf0_physical, &cbuf0_logical, &cbuf0_bytes)!=0)
    {
#ifdef DEBUG
        fprintf(stderr, "Error allocating contiguous host memory for context\n");
#endif
        ETNA_FREE(context_map);
        return ETNA_OUT_OF_MEMORY;
    }
#ifdef DEBUG
    printf("Allocated buffer (size 0x%x) for context: phys=%08x log=%08x\n", (int)cbuf0_bytes, (int)cbuf0_physical, (int)cbuf0_logical);
#endif

#ifdef GCABI_HAS_PHYSICAL
    vctx->bytes = cbuf0_bytes; /* actual size of buffer */
    vctx->physical = (void*)cbuf0_physical;
#endif
    vctx->logical = cbuf0_logical;
    vctx->link = ((uint32_t*)cbuf0_logical) + vctx->linkIndex;
    vctx->inUse = (gctBOOL*)(((uint32_t*)cbuf0_logical) + vctx->inUseIndex);

    /* copy over context buffer to contiguous memory, clear in-use flag */
    memcpy(vctx->logical, vctx->buffer, vctx->bufferSize);
    *vctx->inUse = 0;
    return ETNA_OK;
}
#else
static int initialize_gpu_context(struct viv_conn *conn, gckCONTEXT *vctx)
{
    /* attach to GPU */
    int err;
    gcsHAL_INTERFACE id = {};
    id.command = gcvHAL_ATTACH;
    if((err=viv_invoke(conn, &id)) != gcvSTATUS_OK)
    {
#ifdef DEBUG
        fprintf(stderr, "Error attaching to GPU\n");
#endif
        return ETNA_INTERNAL_ERROR;
    }

#ifdef DEBUG
    printf("Context 0x%08x\n", (int)id.u.Attach.context);
#endif

    *vctx = id.u.Attach.context;
    return ETNA_OK;
}
#endif

int etna_create(struct viv_conn *conn, struct etna_ctx **ctx_out)
{
    if(ctx_out == NULL) return ETNA_INVALID_ADDR;
    struct etna_ctx *ctx = ETNA_CALLOC_STRUCT(etna_ctx);
    if(ctx == NULL) return ETNA_OUT_OF_MEMORY;
    ctx->conn = conn;

    if(initialize_gpu_context(conn, &ctx->ctx) != ETNA_OK)
    {
        ETNA_FREE(ctx);
        return ETNA_INTERNAL_ERROR;
    }

    /* Create synchronization signal */
    if(viv_user_signal_create(conn, 0, &ctx->sig_id) != 0) /* automatic resetting signal */
    {
#ifdef DEBUG
        fprintf(stderr, "Cannot create user signal\n");
#endif
        return ETNA_INTERNAL_ERROR;
    }
#ifdef DEBUG
    printf("Created user signal %i\n", ctx->sig_id);
#endif

    /* Allocate command buffers, and create a synchronization signal for each.
     * Also signal the synchronization signal for the buffers to tell that the buffers are ready for use.
     */
    for(int x=0; x<NUM_COMMAND_BUFFERS; ++x)
    {
        viv_addr_t buf0_physical = 0;
        void *buf0_logical = 0;
        size_t buf0_bytes = 0;
        if(viv_alloc_contiguous(conn, COMMAND_BUFFER_SIZE, &buf0_physical, &buf0_logical, &buf0_bytes)!=0)
        {
#ifdef DEBUG
            fprintf(stderr, "Error allocating host memory\n");
#endif
            return ETNA_OUT_OF_MEMORY;
        }
        ctx->cmdbuf[x].object.type = gcvOBJ_COMMANDBUFFER;
        ctx->cmdbuf[x].physical = (void*)buf0_physical;
        ctx->cmdbuf[x].logical = (void*)buf0_logical;
        ctx->cmdbuf[x].bytes = buf0_bytes;

        if(viv_user_signal_create(conn, 0, &ctx->cmdbuf_sig[x]) != 0 ||
           viv_user_signal_signal(conn, ctx->cmdbuf_sig[x], 1) != 0)
        {
#ifdef DEBUG
            fprintf(stderr, "Cannot create user signal\n");
#endif
            return ETNA_INTERNAL_ERROR;
        }
#ifdef DEBUG
        printf("Allocated buffer %i: phys=%08x log=%08x bytes=%08x [signal %i]\n", x, (uint32_t)buf0_physical, (uint32_t)buf0_logical,
                buf0_bytes, ctx->cmdbuf_sig[x]);
#endif
    }
    /* Set current buffer to -1, to signify that we need to switch to buffer 0 before
     * queueing of commands can be started.
     */
    ctx->cur_buf = -1;
    *ctx_out = ctx;

    return ETNA_OK;
}

/* Clear a command buffer */
static void clear_buffer(gcoCMDBUF cmdbuf)
{
    /* Prepare command buffer for use */
    cmdbuf->startOffset = 0x0;
    cmdbuf->offset = BEGIN_COMMIT_CLEARANCE;
    cmdbuf->free = cmdbuf->bytes - END_COMMIT_CLEARANCE; /* keep space for end-of-commit XXX we don't use this field at all */
}

/* Switch to next buffer, optionally wait for it to be available */
static int switch_next_buffer(struct etna_ctx *ctx)
{
#ifdef DEBUG
    printf("Switching to new buffer\n");
#endif
    int next_buf_id = (ctx->cur_buf + 1) % NUM_COMMAND_BUFFERS;
    if(viv_user_signal_wait(ctx->conn, ctx->cmdbuf_sig[next_buf_id], SIG_WAIT_INDEFINITE) != 0)
    {
#ifdef DEBUG
        printf("Error waiting for command buffer sync signal\n");
#endif
        return ETNA_INTERNAL_ERROR;
    }
    clear_buffer(&ctx->cmdbuf[next_buf_id]);
    ctx->cur_buf = next_buf_id;
    ctx->buf = ctx->cmdbuf[next_buf_id].logical;
    ctx->offset = ctx->cmdbuf[next_buf_id].offset / 4;
#ifdef DEBUG
    printf("Switched to command buffer %i\n", ctx->cur_buf);
#endif
    return ETNA_OK;
}

int etna_free(struct etna_ctx *ctx)
{
    if(ctx == NULL)
        return ETNA_INVALID_ADDR;

    /* TODO: free context buffer */
    // viv_free_contiguous
    /* TODO: free command buffers */
    /* TODO: ETNA_FREE(ctx) */
    return ETNA_OK;
}

/* internal (non-inline) part of etna_reserve 
 * - commit current command buffer (if there is a current command buffer)
 * - signify when current command buffer becomes available using a signal
 * - switch to next command buffer 
 */
int _etna_reserve_internal(struct etna_ctx *ctx, size_t n)
{
    int status;
#ifdef DEBUG
    printf("Buffer full\n");
#endif
    if(ctx->cur_buf != -1)
    {
#ifdef DEBUG
        printf("Submitting old buffer\n");
#endif
        /* Otherwise, if there is something to be committed left in the current command buffer, commit it */
        if((status = etna_flush(ctx)) != ETNA_OK)
            return status;
        /* Queue signal to signify when buffer is available again */
        if(viv_event_queue_signal(ctx->conn, ctx->cmdbuf_sig[ctx->cur_buf], gcvKERNEL_COMMAND) != 0)
            return ETNA_INTERNAL_ERROR;
    }

    /* Move on to next buffer if not enough free in current one */
    status = switch_next_buffer(ctx);
    return status;
}

int etna_flush(struct etna_ctx *ctx)
{
    if(ctx == NULL)
        return ETNA_INVALID_ADDR;
    if(ctx->cur_buf == -1)
        return ETNA_OK; /* Nothing to do */
    gcoCMDBUF cur_buf = &ctx->cmdbuf[ctx->cur_buf];
    ETNA_ALIGN(ctx); /* make sure end of submitted command buffer end is aligned */
#ifdef DEBUG
    printf("Committing command buffer %i startOffset=%x offset=%x\n", ctx->cur_buf,
            cur_buf->startOffset, ctx->offset*4);
#endif
    if(ctx->offset*4 <= (cur_buf->startOffset + BEGIN_COMMIT_CLEARANCE))
        return ETNA_OK; /* Nothing to do */
    cur_buf->offset = ctx->offset*4; /* Copy over current ending offset into CMDBUF, for kernel */
#ifdef DEBUG_CMDBUF
    etna_dump_cmd_buffer(ctx);
    /*
    printf("    {");
    for(size_t i=cur_buf->startOffset; i<cur_buf->offset; i+=4)
    {
        printf("0x%08x,", *((uint32_t*)(((size_t)cur_buf->logical)+i)));
    }
    printf("}\n");
    */
#endif
#ifdef GCABI_HAS_CONTEXT
    int status = viv_commit(ctx->conn, cur_buf, &ctx->ctx);
#else
    int status = viv_commit(ctx->conn, cur_buf, ctx->ctx);
#endif
    if(status != 0)
    {
#ifdef DEBUG
        fprintf(stderr, "Error committing command buffer\n");
#endif
        return status;
    }
#ifdef GCABI_HAS_CONTEXT
    if(*ctx->ctx.inUse)
    {
        printf("    Warning: context buffer used, full context handling not yet supported. Rendering may be corrupted.\n");
        *ctx->ctx.inUse = 0;
    }
#endif
    /* TODO: analyze command buffer to update context */
    /* set context entryPipe to currentPipe (next commit will start with current pipe) */
#ifdef GCABI_HAS_CONTEXT
    ctx->ctx.entryPipe = ctx->ctx.currentPipe;
#endif

    /* TODO: update context, NOP out final pipe2D if entryPipe is 2D, else put in the 2D pipe switch */
    /* TODO: if context was used, queue it to be freed later, and initialize new context buffer */
    cur_buf->startOffset = cur_buf->offset + END_COMMIT_CLEARANCE;
    cur_buf->offset = cur_buf->startOffset + BEGIN_COMMIT_CLEARANCE;
    ctx->offset = cur_buf->offset / 4;
#ifdef DEBUG
#ifdef GCABI_HAS_CONTEXT
    printf("  New start offset: %x New offset: %x Contextbuffer used: %i\n", cur_buf->startOffset, cur_buf->offset, *ctx->ctx.inUse);
#else
    printf("  New start offset: %x New offset: %x\n", cur_buf->startOffset, cur_buf->offset);
#endif
#endif
    return ETNA_OK;
}

int etna_finish(struct etna_ctx *ctx)
{
    int status;
    if(ctx == NULL)
        return ETNA_INVALID_ADDR;
    if((status = etna_flush(ctx)) != ETNA_OK)
        return status;
    /* Submit event queue with SIGNAL, fromWhere=gcvKERNEL_PIXEL (wait for pixel engine to finish) */
    if(viv_event_queue_signal(ctx->conn, ctx->sig_id, gcvKERNEL_PIXEL) != 0)
    {
        return ETNA_INTERNAL_ERROR;
    }
#ifdef DEBUG
    printf("finish: Waiting for signal...\n");
#endif
    /* Wait for signal */
    if(viv_user_signal_wait(ctx->conn, ctx->sig_id, SIG_WAIT_INDEFINITE) != 0)
    {
        return ETNA_INTERNAL_ERROR;
    }

    return ETNA_OK;
}

int etna_set_pipe(struct etna_ctx *ctx, etna_pipe pipe)
{
    int status;
    if(ctx == NULL)
        return ETNA_INVALID_ADDR;

    if((status = etna_reserve(ctx, 2)) != ETNA_OK)
        return status;
    ETNA_EMIT_LOAD_STATE(ctx, VIVS_GL_FLUSH_CACHE>>2, 1, 0);
    switch(pipe)
    {
    case ETNA_PIPE_2D: ETNA_EMIT(ctx, VIVS_GL_FLUSH_CACHE_PE2D); break;
    case ETNA_PIPE_3D: ETNA_EMIT(ctx, VIVS_GL_FLUSH_CACHE_DEPTH | VIVS_GL_FLUSH_CACHE_COLOR); break;
    default: return ETNA_INVALID_VALUE;
    }

    etna_stall(ctx, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);

    if((status = etna_reserve(ctx, 2)) != ETNA_OK)
        return status;
    ETNA_EMIT_LOAD_STATE(ctx, VIVS_GL_PIPE_SELECT>>2, 1, 0);
    ETNA_EMIT(ctx, pipe);

#ifdef GCABI_HAS_CONTEXT
    ctx->ctx.currentPipe = pipe;
#endif
    return ETNA_OK;
}

int etna_semaphore(struct etna_ctx *ctx, uint32_t from, uint32_t to)
{
    int status;
    if(ctx == NULL)
        return ETNA_INVALID_ADDR;
    if((status = etna_reserve(ctx, 2)) != ETNA_OK)
        return status;
    ETNA_EMIT_LOAD_STATE(ctx, VIVS_GL_SEMAPHORE_TOKEN>>2, 1, 0);
    ETNA_EMIT(ctx, VIVS_GL_SEMAPHORE_TOKEN_FROM(from) | VIVS_GL_SEMAPHORE_TOKEN_TO(to));
    return ETNA_OK;
}

int etna_stall(struct etna_ctx *ctx, uint32_t from, uint32_t to)
{
    int status;
    if(ctx == NULL)
        return ETNA_INVALID_ADDR;
    if((status = etna_reserve(ctx, 4)) != ETNA_OK)
        return status;
    ETNA_EMIT_LOAD_STATE(ctx, VIVS_GL_SEMAPHORE_TOKEN>>2, 1, 0);
    ETNA_EMIT(ctx, VIVS_GL_SEMAPHORE_TOKEN_FROM(from) | VIVS_GL_SEMAPHORE_TOKEN_TO(to));
    if(from == SYNC_RECIPIENT_FE)
    {
        /* if the frontend is to be stalled, queue a STALL frontend command */
        ETNA_EMIT_STALL(ctx, from, to);
    } else {
        /* otherwise, load the STALL token state */
        ETNA_EMIT_LOAD_STATE(ctx, VIVS_GL_STALL_TOKEN>>2, 1, 0);
        ETNA_EMIT(ctx, VIVS_GL_STALL_TOKEN_FROM(from) | VIVS_GL_STALL_TOKEN_TO(to));
    }
    return ETNA_OK;
}

void etna_dump_cmd_buffer(struct etna_ctx *ctx)
{
    uint32_t start_offset = ctx->cmdbuf[ctx->cur_buf].startOffset/4 + 8;
    uint32_t *buf = &ctx->buf[start_offset]; 
    size_t size = ctx->offset - start_offset;
    printf("cmdbuf:\n");
    for(unsigned idx=0; idx<size; ++idx)
    {
        printf(":%08x ", buf[idx]);
        printf("\n");
    }
}

