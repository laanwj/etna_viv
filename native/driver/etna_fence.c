#include "etna_fence.h"

#include <etnaviv/etna.h>

int etna_fence_new(struct etna_context *ctx, struct pipe_fence_handle **fence)
{
    return ETNA_OK;
}

void etna_screen_fence_reference( struct pipe_screen *screen,
                        struct pipe_fence_handle **ptr,
                        struct pipe_fence_handle *fence )
{
    DBG("unimplemented etna_screen_fence_reference");
}

boolean etna_screen_fence_signalled( struct pipe_screen *screen,
                           struct pipe_fence_handle *fence )
{
    DBG("unimplemented etna_screen_fence_signalled");
    return false;
}

boolean etna_screen_fence_finish( struct pipe_screen *screen,
                        struct pipe_fence_handle *fence,
                        uint64_t timeout )
{
    DBG("unimplemented etna_screen_fence_finish");
    return false;
}


