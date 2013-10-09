#include <etnaviv/etna_queue.h>
#include <etnaviv/etna.h>
#include <etnaviv/viv.h>

#include "gc_abi.h"
#include "viv_internal.h"

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

struct _gcsQUEUE *_etna_queue_first(struct etna_queue *queue)
{
    struct _gcsQUEUE *rv = (queue->count == 0) ? NULL : queue->queue;
    queue->last = NULL;
    queue->count = 0;
    return rv;
}

int etna_queue_alloc(struct etna_queue *queue, struct _gcsHAL_INTERFACE **cmd_out)
{
    if(queue == NULL)
        return ETNA_INVALID_ADDR;
    if(queue->count == queue->max_count)
    {
        int rv;
        /* Queue is full, flush context. Assert that there is a one-to-one relationship
         * between queue and etna context so that flushing the context flushes this queue.
         *
         * Don't request a fence to prevent an infinite loop.
         */
        assert(queue->ctx->queue == queue);
        if((rv = etna_flush(queue->ctx, NULL)) != ETNA_OK)
            return rv;
        assert(queue->count == 0);
    }
    struct _gcsQUEUE *cmd = &queue->queue[queue->count++];
    cmd->next = PTR_TO_VIV(NULL);
    /* update next pointer of previous record */
    if(queue->last != NULL)
    {
        queue->last->next = PTR_TO_VIV(cmd);
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
    cmd->u.Signal.signal = PTR_TO_VIV((void*)sig_id);
    cmd->u.Signal.auxSignal = PTR_TO_VIV((void*)0x0);
    cmd->u.Signal.process = HANDLE_TO_VIV(queue->ctx->conn->process);
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
    cmd->u.FreeContiguousMemory.physical = PTR_TO_VIV((gctPHYS_ADDR)physical);
    cmd->u.FreeContiguousMemory.logical = PTR_TO_VIV(logical);
    return ETNA_OK;
}

int etna_queue_unlock_vidmem(struct etna_queue *queue, viv_node_t node, enum viv_surf_type type)
{
    struct _gcsHAL_INTERFACE *cmd = NULL;
    int rv;
    if((rv=etna_queue_alloc(queue, &cmd)) != ETNA_OK)
        return rv;
    cmd->command = gcvHAL_UNLOCK_VIDEO_MEMORY;
    cmd->u.UnlockVideoMemory.node = HANDLE_TO_VIV(node);
    cmd->u.UnlockVideoMemory.type = convert_surf_type(type);
    return ETNA_OK;
}

int etna_queue_free_vidmem(struct etna_queue *queue, viv_node_t node)
{
    struct _gcsHAL_INTERFACE *cmd = NULL;
    int rv;
    if((rv=etna_queue_alloc(queue, &cmd)) != ETNA_OK)
        return rv;
    cmd->command = gcvHAL_FREE_VIDEO_MEMORY;
    cmd->u.FreeVideoMemory.node = HANDLE_TO_VIV(node);
    return ETNA_OK;
}

int etna_queue_unmap_user_memory(struct etna_queue *queue, void *memory, size_t size, viv_usermem_t info, viv_addr_t address)
{
    struct _gcsHAL_INTERFACE *cmd = NULL;
    int rv;
    if((rv=etna_queue_alloc(queue, &cmd)) != ETNA_OK)
        return rv;
    cmd->command = gcvHAL_UNMAP_USER_MEMORY;
    cmd->u.UnmapUserMemory.memory = PTR_TO_VIV(memory);
    cmd->u.UnmapUserMemory.size = size;
    cmd->u.UnmapUserMemory.info = HANDLE_TO_VIV(info);
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

