#include "etna.h"
#include "viv.h"
#include "context_cmd.h"
#include "etna/state.xml.h"

#include <stdlib.h>
#include <stdbool.h>
#define DEBUG
#ifdef DEBUG
#include <stdio.h>
#endif


/* TODO: don't forget to handle FE.VERTEX_ELEMENT_CONFIG (0x0600....0x0063c) specially;
 * fields need to be written in the right order, and only as many should be written as there are
 * used vertex elements.
 * header: contextbuf[contextbuf_addr[i].index - 1] where contextbuf_addr[i].address == 0x600
 */

/* Initialize kernel GPU context and state map */
static int initialize_gpu_context(gcoCONTEXT vctx)
{
    /* First build context state map from compressed representation */
    size_t contextbuf_addr_size = sizeof(contextbuf_addr)/sizeof(address_index_t);
    size_t state_count = contextbuf_addr[contextbuf_addr_size - 1].address / 4 + 1;
    uint32_t *context_map = malloc(state_count * 4);
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
    vctx->buffer = malloc(sizeof(contextbuf));
    memcpy(vctx->buffer, contextbuf, sizeof(contextbuf)); /* copy over hardcoded context command buffer */
    vctx->pipe3DIndex = 0x2d6; // XXX should not be hardcoded
    vctx->pipe2DIndex = 0x106e; // XXX should not be hardcoded
    vctx->linkIndex = 0x1076; // XXX should not be hardcoded
    vctx->inUseIndex = 0x1078; // XXX should not be hardcoded
    vctx->bufferSize = sizeof(contextbuf);
    vctx->bytes = 0x0; // Number of bytes at actually allocated for physical, logical
    vctx->physical = (void*)0x0;
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
    if(viv_alloc_contiguous(vctx->bufferSize, &cbuf0_physical, &cbuf0_logical, &cbuf0_bytes)!=0)
    {
#ifdef DEBUG
        fprintf(stderr, "Error allocating contiguous host memory for context\n");
#endif
        free(context_map);
        return ETNA_OUT_OF_MEMORY;
    }
#ifdef DEBUG
    printf("Allocated buffer (size 0x%x) for context: phys=%08x log=%08x\n", (int)cbuf0_bytes, (int)cbuf0_physical, (int)cbuf0_logical);
#endif

    vctx->bytes = cbuf0_bytes; /* actual size of buffer */
    vctx->physical = (void*)cbuf0_physical;
    vctx->logical = cbuf0_logical;
    vctx->link = ((uint32_t*)cbuf0_logical) + vctx->linkIndex;
    vctx->inUse = (gctBOOL*)(((uint32_t*)cbuf0_logical) + vctx->inUseIndex);

    /* copy over context buffer to contiguous memory, clear in-use flag */
    memcpy(vctx->logical, vctx->buffer, vctx->bufferSize);
    *vctx->inUse = 0;
    return ETNA_OK;
}

etna_ctx *etna_create(void)
{
    etna_ctx *ctx = malloc(sizeof(etna_ctx));
    if(ctx == NULL)
        return NULL;
    memset(ctx, 0, sizeof(etna_ctx));

    if(initialize_gpu_context(&ctx->ctx) != ETNA_OK)
    {
        free(ctx);
        return NULL;
    }

    /* Create synchronization signal */
    if(viv_user_signal_create(0, &ctx->sig_id) != 0) /* automatic resetting signal */
    {
#ifdef DEBUG
        fprintf(stderr, "Cannot create user signal\n");
#endif
        return NULL;
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
        if(viv_alloc_contiguous(COMMAND_BUFFER_SIZE, &buf0_physical, &buf0_logical, &buf0_bytes)!=0)
        {
#ifdef DEBUG
            fprintf(stderr, "Error allocating host memory\n");
#endif
            return NULL;
        }
        ctx->cmdbuf[x].object.type = gcvOBJ_COMMANDBUFFER;
        ctx->cmdbuf[x].physical = (void*)buf0_physical;
        ctx->cmdbuf[x].logical = (void*)buf0_logical;
        ctx->cmdbuf[x].bytes = buf0_bytes;

        if(viv_user_signal_create(0, &ctx->cmdbuf_sig[x]) != 0 ||
           viv_user_signal_signal(ctx->cmdbuf_sig[x], 1) != 0)
        {
#ifdef DEBUG
            fprintf(stderr, "Cannot create user signal\n");
#endif
            return NULL;
        }
#ifdef DEBUG
        printf("Allocated buffer %i: phys=%08x log=%08x [signal %i]\n", x, (uint32_t)buf0_physical, (uint32_t)buf0_logical,
                ctx->cmdbuf_sig[x]);
#endif
    }
    /* Set current buffer to -1, to signify that we need to switch to buffer 0 before
     * queueing of commands can be started.
     */
    ctx->cur_buf = -1;

    return ctx;
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
static int switch_next_buffer(etna_ctx *ctx)
{
    int next_buf_id = (ctx->cur_buf + 1) % NUM_COMMAND_BUFFERS;
    if(viv_user_signal_wait(ctx->cmdbuf_sig[next_buf_id], SIG_WAIT_INDEFINITE) != 0)
    {
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

int etna_free(etna_ctx *ctx)
{
    if(ctx == NULL)
        return ETNA_INVALID_ADDR;

    /* TODO: free context buffer */
    // viv_free_contiguous
    /* TODO: free command buffers */
    /* TODO: free(ctx) */
    return ETNA_OK;
}

/* XXX ideally, (part of) this function should be inlined,
 * as it's a very simple but also very common operation 
 */

/* internal (non-inline) part of etna_reserve 
 * - commit current command buffer (if there is a current command buffer)
 * - signify when current command buffer becomes available using a signal
 * - switch to next command buffer 
 */
int _etna_reserve_internal(etna_ctx *ctx, size_t n)
{
    int status;
    if(ctx->cur_buf != -1)
    {
        /* Otherwise, if there is something to be committed left in the current command buffer, commit it */
        status = etna_flush(ctx);
        if(status != 0)
            return status;
        /* Queue signal to signify when buffer is available again */
        if(viv_event_queue_signal(ctx->cmdbuf_sig[ctx->cur_buf], gcvKERNEL_COMMAND) != 0)
            return ETNA_INTERNAL_ERROR;
    }

    /* Move on to next buffer if not enough free in current one */
    status = switch_next_buffer(ctx);
    return status;
}

int etna_flush(etna_ctx *ctx)
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
#ifdef DEBUG
    printf("    {");
    for(size_t i=cur_buf->startOffset; i<ctx->offset*4; i+=4)
    {
        printf("0x%08x,", *((uint32_t*)((size_t)cur_buf->logical)+i));
    }

    printf("}\n");
#endif
    cur_buf->offset = ctx->offset*4; /* Copy over current ending offset into CMDBUF, for kernel */
    int status = viv_commit(cur_buf, &ctx->ctx);
    if(status != 0)
    {
#ifdef DEBUG
        fprintf(stderr, "Error committing command buffer\n");
#endif
        return status;
    }
    /* TODO: analyze command buffer to update context */
    /* set context entryPipe to currentPipe (next commit will start with current pipe) */
    ctx->ctx.entryPipe = ctx->ctx.currentPipe;
    /* TODO: if context was used, queue it to be freed later, and initialize new context buffer */
    cur_buf->startOffset = cur_buf->offset + END_COMMIT_CLEARANCE;
    cur_buf->offset = cur_buf->startOffset + BEGIN_COMMIT_CLEARANCE;
    ctx->offset = cur_buf->offset / 4;
#ifdef DEBUG
    printf("  New start offset: %x New offset: %x Contextbuffer used: %i\n", cur_buf->startOffset, cur_buf->offset, *ctx->ctx.inUse);
#endif
    return ETNA_OK;
}

int etna_finish(etna_ctx *ctx)
{
    int status;
    if(ctx == NULL)
        return ETNA_INVALID_ADDR;
    status = etna_flush(ctx);
    if(status != ETNA_OK)
        return status;
    /* Submit event queue with SIGNAL, fromWhere=gcvKERNEL_PIXEL (wait for pixel engine to finish) */
    if(viv_event_queue_signal(ctx->sig_id, gcvKERNEL_PIXEL) != 0)
    {
        return ETNA_INTERNAL_ERROR;
    }

    /* Wait for signal */
    if(viv_user_signal_wait(ctx->sig_id, SIG_WAIT_INDEFINITE) != 0)
    {
        return ETNA_INTERNAL_ERROR;
    }

    return ETNA_OK;
}

int etna_set_pipe(etna_ctx *ctx, etna_pipe pipe)
{
    if(ctx == NULL)
        return ETNA_INVALID_ADDR;
    int status;

    if((status = etna_reserve(ctx, 2)) != ETNA_OK)
        return status;
    ETNA_EMIT_LOAD_STATE(ctx, VIVS_GL_FLUSH_CACHE, 1, 0);
    switch(pipe)
    {
    case ETNA_PIPE_2D: ETNA_EMIT(ctx, VIVS_GL_FLUSH_CACHE_PE2D); break;
    case ETNA_PIPE_3D: ETNA_EMIT(ctx, VIVS_GL_FLUSH_CACHE_DEPTH | VIVS_GL_FLUSH_CACHE_COLOR); break;
    default: return ETNA_INVALID_VALUE;
    }

    etna_stall(ctx, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);

    if((status = etna_reserve(ctx, 2)) != ETNA_OK)
        return status;
    ETNA_EMIT_LOAD_STATE(ctx, VIVS_GL_PIPE_SELECT, 1, 0);
    ETNA_EMIT(ctx, pipe);

    ctx->ctx.currentPipe = pipe;
    return ETNA_OK;
}

int etna_semaphore(etna_ctx *ctx, uint32_t from, uint32_t to)
{
    if(ctx == NULL)
        return ETNA_INVALID_ADDR;
    int status = etna_reserve(ctx, 2);
    if(status != ETNA_OK)
        return status;
    ETNA_EMIT_LOAD_STATE(ctx, VIVS_GL_SEMAPHORE_TOKEN, 1, 0);
    ETNA_EMIT(ctx, VIVS_GL_SEMAPHORE_TOKEN_FROM(from) | VIVS_GL_SEMAPHORE_TOKEN_TO(to));
    return ETNA_OK;
}

int etna_stall(etna_ctx *ctx, uint32_t from, uint32_t to)
{
    if(ctx == NULL)
        return ETNA_INVALID_ADDR;
    int status = etna_reserve(ctx, 4);
    if(status != ETNA_OK)
        return status;
    ETNA_EMIT_LOAD_STATE(ctx, VIVS_GL_SEMAPHORE_TOKEN, 1, 0);
    ETNA_EMIT(ctx, VIVS_GL_SEMAPHORE_TOKEN_FROM(from) | VIVS_GL_SEMAPHORE_TOKEN_TO(to));
    if(from == SYNC_RECIPIENT_FE)
    {
        /* if the frontend is to be stalled, queue a STALL frontend command */
        ETNA_EMIT_STALL(ctx, from, to);
    } else {
        /* otherwise, load the STALL token state */
        ETNA_EMIT_LOAD_STATE(ctx, VIVS_GL_STALL_TOKEN, 1, 0);
        ETNA_EMIT(ctx, VIVS_GL_STALL_TOKEN_FROM(from) | VIVS_GL_STALL_TOKEN_TO(to));
    }
    return ETNA_OK;
}
