Notes about old and new bugs in Vivante GPL kernel.

Race condition
---------------

In event submission (from #cubox).

    [20:19:57] <_rmk_> so, gckEVENT_Submit claims the event listMutex... allocates an event id, drops it, and then submits the command queue...
    [20:20:02] <_rmk_> so two threads can do this...
    [20:20:15] <_rmk_> CPU0: claim listMutex
    [20:20:20] <_rmk_> CPU0: get event ID
    [20:20:25] <_rmk_> CPU0: drop listMutex
    [20:20:31] <_rmk_> CPU1: claim listMutex
    [20:20:36] <_rmk_> CPU1: get another event ID
    [20:20:41] <_rmk_> CPU1: drop listMutex
    [20:20:49] <_rmk_> CPU1: insert commands into the command queue
    [20:20:56] <_rmk_> CPU0: insert commands into the command queue
    [20:21:16] <_rmk_> and then we have the second event due to fire first, which upsets the event handling

Status: still present

