#include <etnaviv/etna_queue.h>
#include <etnaviv/etna.h>
#include <etnaviv/viv.h>

#include "gc_abi.h"
#include "gc_hal_base.h"
#include "gc_hal.h"
#include "gc_hal_driver.h"
#ifdef GCABI_HAS_CONTEXT
#include "gc_hal_user_context.h"
#else
#include "gc_hal_kernel_context.h"
#endif
#include "etna_enum_convert.h"

#include <assert.h>

/* Maximum number of kernel commands in queue */
#define ETNA_QUEUE_CAPACITY (64)

int etna_queue_create(struct etna_ctx *ctx, struct etna_queue **queue_out)
{
    struct etna_queue *queue = ETNA_CALLOC_STRUCT(etna_queue);
    if(queue == NULL)
    {
        return ETNA_OUT_OF_MEMORY;
    }
    queue->ctx = ctx;
    queue->queue = ETNA_CALLOC_STRUCT_ARRAY(ETNA_QUEUE_CAPACITY, _gcsQUEUE);
    queue->last = NULL;
    queue->count = 0;
    queue->max_count = ETNA_QUEUE_CAPACITY;

    *queue_out = queue;
    return ETNA_OK;
}

int etna_queue_clear(struct etna_queue *queue)
{
    queue->last = NULL;
    queue->count = 0;
    return ETNA_OK;
}

int etna_queue_alloc(struct etna_queue *queue, struct _gcsHAL_INTERFACE **cmd_out)
{
    int rv;
    if(queue == NULL)
        return ETNA_INVALID_ADDR;
    if(queue->count == queue->max_count)
    {
        /* Queue is full, flush context. Assert that there is a one-to-one relationship
         * between queue and etna context so that flushing the context flushes this queue.
         */
        assert(queue->ctx->queue == queue);
        if((rv = etna_flush(queue->ctx)) != ETNA_OK)
            return rv;
        assert(queue->count == 0);
    }
    struct _gcsQUEUE *cmd = &queue->queue[queue->count++];
    cmd->next = NULL;
    /* update next pointer of previous record */
    if(queue->last != NULL)
    {
        queue->last->next = cmd;
    }
    queue->last = cmd;
    *cmd_out = &cmd->iface;
    return ETNA_OK;
}

int etna_queue_signal(struct etna_queue *queue, int sig_id, enum viv_where fromWhere)
{
    struct _gcsHAL_INTERFACE *cmd = NULL;
    int rv;
    if((rv=etna_queue_alloc(queue, &cmd)) != ETNA_OK)
        return rv;
    cmd->command = gcvHAL_SIGNAL;
    cmd->u.Signal.signal = (void*)sig_id;
    cmd->u.Signal.auxSignal = (void*)0x0;
    cmd->u.Signal.process = queue->ctx->conn->process;
    cmd->u.Signal.fromWhere = convert_where(fromWhere);
    return ETNA_OK;
}

int etna_queue_free_contiguous(struct etna_queue *queue, size_t bytes, viv_addr_t physical, void *logical)
{
    struct _gcsHAL_INTERFACE *cmd = NULL;
    int rv;
    if((rv=etna_queue_alloc(queue, &cmd)) != ETNA_OK)
        return rv;
    cmd->command = gcvHAL_FREE_CONTIGUOUS_MEMORY;
    cmd->u.FreeContiguousMemory.bytes = bytes;
    cmd->u.FreeContiguousMemory.physical = (gctPHYS_ADDR)physical;
    cmd->u.FreeContiguousMemory.logical = logical;
    return ETNA_OK;
}

int etna_queue_unlock_vidmem(struct etna_queue *queue, viv_node_t node, enum viv_surf_type type, int async)
{
    struct _gcsHAL_INTERFACE *cmd = NULL;
    int rv;
    if((rv=etna_queue_alloc(queue, &cmd)) != ETNA_OK)
        return rv;
    cmd->command = gcvHAL_UNLOCK_VIDEO_MEMORY;
    cmd->u.UnlockVideoMemory.node = node;
    cmd->u.UnlockVideoMemory.type = convert_surf_type(type);
    cmd->u.UnlockVideoMemory.asynchroneous = async;
    return ETNA_OK;
}

int etna_queue_free_vidmem(struct etna_queue *queue, viv_node_t node)
{
    struct _gcsHAL_INTERFACE *cmd = NULL;
    int rv;
    if((rv=etna_queue_alloc(queue, &cmd)) != ETNA_OK)
        return rv;
    cmd->command = gcvHAL_FREE_VIDEO_MEMORY;
    cmd->u.FreeVideoMemory.node = node;
    return ETNA_OK;
}

int etna_queue_unmap_user_memory(struct etna_queue *queue, void *memory, size_t size, void *info, viv_addr_t address)
{
    struct _gcsHAL_INTERFACE *cmd = NULL;
    int rv;
    if((rv=etna_queue_alloc(queue, &cmd)) != ETNA_OK)
        return rv;
    cmd->command = gcvHAL_UNMAP_USER_MEMORY;
    cmd->u.UnmapUserMemory.memory = memory;
    cmd->u.UnmapUserMemory.size = size;
    cmd->u.UnmapUserMemory.info = info;
    cmd->u.UnmapUserMemory.address = address;
    return ETNA_OK;
}

int etna_queue_free(struct etna_queue *queue)
{
    if(queue == NULL)
        return ETNA_INVALID_ADDR;
    ETNA_FREE(queue->queue);
    ETNA_FREE(queue);
    return ETNA_OK;
}

