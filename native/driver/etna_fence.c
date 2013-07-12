#include "etna_fence.h"
#include "etna_debug.h"
#include "etna_screen.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_string.h"

#include <etnaviv/viv.h>
#include <etnaviv/etna.h>
#include <etnaviv/etna_queue.h>

int etna_fence_new(struct pipe_screen *screen_h, struct etna_ctx *ctx, struct pipe_fence_handle **fence_p)
{
    struct etna_fence *fence = NULL;
    struct etna_screen *screen = etna_screen(screen_h);
    int rv;

    /* XXX we do not release the fence_p reference here -- neither do the other drivers,
     * and clients don't seem to rely on this. */
    if(fence_p == NULL)
        return ETNA_INVALID_ADDR;
    assert(*fence_p == NULL);

    /* re-use old fence, if available, and reset it first */
    pipe_mutex_lock(screen->fence_mutex);
    if(screen->fence_freelist != NULL)
    {
        fence = screen->fence_freelist;
        screen->fence_freelist = fence->next_free;
        fence->next_free = NULL;
    }
    pipe_mutex_unlock(screen->fence_mutex);

    if(fence != NULL)
    {
        if((rv = viv_user_signal_signal(ctx->conn, fence->signal, 0)) != VIV_STATUS_OK)
        {
            printf("Error: could not reset signal %i\n", fence->signal);
            etna_screen_destroy_fence(screen_h, fence);
            return rv;
        }
        fence->signalled = false;
    } else {
        fence = CALLOC_STRUCT(etna_fence);
        /* Create signal with manual reset; we want to be able to probe it 
         * or wait for it without resetting it.
         */
        if((rv = viv_user_signal_create(ctx->conn, /* manualReset */ true, &fence->signal)) != VIV_STATUS_OK)
        {
            FREE(fence);
            return rv;
        }
    }
    if((rv = etna_queue_signal(ctx->queue, fence->signal, VIV_WHERE_PIXEL)) != ETNA_OK)
    {
        printf("%s: error queueing signal %i\n", __func__, fence->signal);
        viv_user_signal_destroy(ctx->conn, fence->signal);
        FREE(fence);
        return rv;
    }
    pipe_reference_init(&fence->reference, 1);
    *fence_p = (struct pipe_fence_handle*)fence;
    return ETNA_OK;
}

static void
debug_describe_fence(char* buf, const struct etna_fence *fence)
{
    util_sprintf(buf, "etna_fence<%i>", fence->signal);
}

void etna_screen_fence_reference(struct pipe_screen *screen_h,
                        struct pipe_fence_handle **ptr_h,
                        struct pipe_fence_handle *fence_h )
{
    struct etna_screen *screen = etna_screen(screen_h);
    struct etna_fence *fence = etna_fence(fence_h);
    struct etna_fence **ptr = (struct etna_fence **) ptr_h;
    struct etna_fence *old_fence = *ptr;
    if (pipe_reference_described(&(*ptr)->reference, &fence->reference, 
                                 (debug_reference_descriptor)debug_describe_fence))
    {
        if(etna_screen_fence_signalled(screen_h, (struct pipe_fence_handle*)old_fence))
        {
            /* If signalled, add old fence to free list, as it can be reused */
            pipe_mutex_lock(screen->fence_mutex);
            old_fence->next_free = screen->fence_freelist;
            screen->fence_freelist = old_fence;
            pipe_mutex_unlock(screen->fence_mutex);
        } else {
            /* If fence is still to be signalled, destroy it, to prevent it from being
             * reused. */
            etna_screen_destroy_fence(screen_h, old_fence);
        }
    }
    *ptr_h = fence_h;
}

boolean etna_screen_fence_signalled(struct pipe_screen *screen_h,
                           struct pipe_fence_handle *fence_h)
{
    return etna_screen_fence_finish(screen_h, fence_h, 0);
}

boolean etna_screen_fence_finish(struct pipe_screen *screen_h,
                        struct pipe_fence_handle *fence_h,
                        uint64_t timeout )
{
    struct etna_screen *screen = etna_screen(screen_h);
    struct etna_fence *fence = etna_fence(fence_h);
    int rv;
    if(fence->signalled) /* avoid a kernel roundtrip */
        return true;
    /* nanoseconds to milliseconds */
    rv = viv_user_signal_wait(screen->dev, fence->signal, 
            timeout == PIPE_TIMEOUT_INFINITE ? VIV_WAIT_INDEFINITE : (timeout / 1000000ULL));
    if(rv != VIV_STATUS_OK && rv != VIV_STATUS_TIMEOUT)
    {
        printf("%s: error waiting for signal %i", __func__, fence->signal);
    }
    fence->signalled = (rv != VIV_STATUS_TIMEOUT);
    return fence->signalled;
}

void etna_screen_destroy_fence(struct pipe_screen *screen_h, struct etna_fence *fence)
{
    struct etna_screen *screen = etna_screen(screen_h);
    if(viv_user_signal_destroy(screen->dev, fence->signal) != VIV_STATUS_OK)
    {
        printf("%s: cannot destroy signal %i\n", __func__, fence->signal);
    }
    FREE(fence);
}

void etna_screen_destroy_fences(struct pipe_screen *screen_h)
{
    struct etna_screen *screen = etna_screen(screen_h);
    struct etna_fence *fence, *next;
    pipe_mutex_lock(screen->fence_mutex);
    for(fence = screen->fence_freelist; fence != NULL; fence = next)
    {
        next = fence->next_free;
        etna_screen_destroy_fence(screen_h, fence);
    }
    screen->fence_freelist = NULL;
    pipe_mutex_unlock(screen->fence_mutex);
}

