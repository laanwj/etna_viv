#include "etna_fence.h"
#include "etna_debug.h"
#include "etna_screen.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_string.h"

#include <etnaviv/viv.h>
#include <etnaviv/etna.h>
#include <etnaviv/etna_queue.h>

/* Possible optimization: keep a pool of signals in etna_screen,
 * instead of creating and destroying them all the time.
 */

int etna_fence_new(struct etna_ctx *ctx, struct pipe_fence_handle **fence_p)
{
    struct etna_fence *fence = CALLOC_STRUCT(etna_fence);
    int rv;
    /* XXX we do not release the fence_p reference here -- neither do the other drivers,
     * and clients don't seem to rely on this. */
    if(fence_p == NULL)
        return ETNA_INVALID_ADDR;
    assert(*fence_p == NULL);
    /* Create signal with manual reset; we never actually reset the signal,
     * but want to be able to probe it or wait for it without resetting it.
     */
    if((rv = viv_user_signal_create(ctx->conn, /* manualReset */ true, &fence->signal)) != VIV_STATUS_OK)
    {
        FREE(fence);
        return rv;
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

void etna_screen_fence_reference( struct pipe_screen *screen_h,
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
        if(viv_user_signal_destroy(screen->dev, old_fence->signal) != VIV_STATUS_OK)
        {
            printf("%s: cannot destroy signal %i\n", __func__, fence->signal);
        }
        FREE(old_fence);
    }
    *ptr_h = fence_h;
}

boolean etna_screen_fence_signalled( struct pipe_screen *screen_h,
                           struct pipe_fence_handle *fence_h )
{
    return etna_screen_fence_finish(screen_h, fence_h, 0);
}

boolean etna_screen_fence_finish( struct pipe_screen *screen_h,
                        struct pipe_fence_handle *fence_h,
                        uint64_t timeout )
{
    struct etna_screen *screen = etna_screen(screen_h);
    struct etna_fence *fence = etna_fence(fence_h);
    int rv;
    /* nanoseconds to milliseconds */
    rv = viv_user_signal_wait(screen->dev, fence->signal, 
            timeout == PIPE_TIMEOUT_INFINITE ? VIV_WAIT_INDEFINITE : (timeout / 1000000ULL));
    if(rv != VIV_STATUS_OK && rv != VIV_STATUS_TIMEOUT)
    {
        printf("%s: error waiting for signal %i", __func__, fence->signal);
    }
    return rv != VIV_STATUS_TIMEOUT;
}


