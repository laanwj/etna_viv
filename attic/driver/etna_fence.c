#include "etna_fence.h"
#include "etna_debug.h"
#include "etna_screen.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_string.h"

#include <etnaviv/viv.h>
#include <etnaviv/etna.h>
#include <etnaviv/etna_queue.h>

/**
 * Reference or unreference a fence. This is pretty much a no-op.
 */
static void etna_screen_fence_reference(struct pipe_screen *screen_h,
                        struct pipe_fence_handle **ptr_h,
                        struct pipe_fence_handle *fence_h)
{
    *ptr_h = fence_h;
}

/**
 * Wait until the fence has been signalled for the specified timeout in nanoseconds,
 * or PIPE_TIMEOUT_INFINITE.
 */
static boolean etna_screen_fence_finish(struct pipe_screen *screen_h,
                        struct pipe_fence_handle *fence_h,
                        uint64_t timeout )
{
    struct etna_screen *screen = etna_screen(screen_h);
    uint32_t fence = PIPE_HANDLE_TO_ETNA_FENCE(fence_h);
    int rv;
    /* nanoseconds to milliseconds */
    rv = viv_fence_finish(screen->dev, fence,
            timeout == PIPE_TIMEOUT_INFINITE ? VIV_WAIT_INDEFINITE : (timeout / 1000000ULL));
    if(rv != VIV_STATUS_OK && rv != VIV_STATUS_TIMEOUT)
    {
        BUG("error waiting for fence %08x", fence);
    }
    return (rv != VIV_STATUS_TIMEOUT);
}

/**
 * Poll whether the fence has been signalled.
 */
static boolean etna_screen_fence_signalled(struct pipe_screen *screen_h,
                           struct pipe_fence_handle *fence_h)
{
    return etna_screen_fence_finish(screen_h, fence_h, 0);
}

void etna_screen_fence_init(struct pipe_screen *pscreen)
{
    pscreen->fence_reference = etna_screen_fence_reference;
    pscreen->fence_signalled = etna_screen_fence_signalled;
    pscreen->fence_finish = etna_screen_fence_finish;
}


