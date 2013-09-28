Notes about old and new bugs in Vivante GPL kernel.

Race condition
---------------

In event submission (from #cubox).

    [20:19:57] <_rmk_> so, gckEVENT_Submit claims the event listMutex... allocates an event id,
    drops it, and then submits the command queue...  [20:20:02] <_rmk_> so two threads can do
    this...
    [20:20:15] <_rmk_> CPU0: claim listMutex
    [20:20:20] <_rmk_> CPU0: get event ID
    [20:20:25] <_rmk_> CPU0: drop listMutex
    [20:20:31] <_rmk_> CPU1: claim listMutex
    [20:20:36] <_rmk_> CPU1: get another event ID
    [20:20:41] <_rmk_> CPU1: drop listMutex
    [20:20:49] <_rmk_> CPU1: insert commands into the command queue
    [20:20:56] <_rmk_> CPU0: insert commands into the command queue
    [20:21:16] <_rmk_> and then we have the second event due to fire first, which upsets the event
    handling

Status: still present

Command buffer submission
--------------------------

Version: dove

Submitting a lot of distinct command buffers without queuing (or waiting for) a synchronization
signal causes the kernel to run out of signals. This causes hangs in rendering.

Status: workaround found (see `ETNA_MAX_UNSIGNALED_FLUSHES`)

Command buffer memory management on cubox
-------------------------------------------

    <_rmk_> err, this is not good.
    <_rmk_> gckCOMMAND_Start: command queue is at 0xe7846000
    <_rmk_> gckCOMMAND_Start: WL 380000c8 0cf6d19f 40000002 10281000
    <_rmk_> that's what the first 4 words should've been at the beginning (and where when the GPU was started)
    <_rmk_> they then conveniently become: 4000002C 19CE0000 AAAAAAAA AAAAAAAA
    <_rmk_> the first two change because of the wait being converted to a link
    <_rmk_> the second two...
    <_rmk_> well, 0xaaaaaaaa is the free page poisoning.
    <_rmk_> which suggests that the GPU command page was freed while still in use
    <_rmk_> ah ha, that'll be how.  get lucky with the cache behaviour and this is what happens...
    <_rmk_> page poisoning enabled.  When a lowmem page is allocated, it's memset() through its lowmem mapping, which is cacheable.
    <_rmk_> this data can sit in the CPU cache.
    <_rmk_> it is then ioremap()'d (which I've been dead against for years) which creates a *device* mapping.
    <_rmk_> we then write the wait/link through the device mapping.
    <_rmk_> at some point later, the cache lines get evicted from the _normal memory_ mapping.
    <_rmk_> thereby overwriting the original wait/link commands in the GPU stream with 0xAAAAAAAA
    <_rmk_> I wonder what that a command word with a bit pattern of 10101 in the top 5 bits tells the GPU to do...
    <_rmk_> the only thing I can say is... if people would damn well listen to me when I say "don't do this" then you wouldn't get these bugs.
    <_rmk_> this is one of the reasons why my check in ioremap.c exists to prevent system memory being ioremap'd and therefore this kind of issue cropping up

More races...

    <wumpus> IIRC the blob does a similar thing
    <_rmk_> if you ever see the kernel galcore complain about a lost event or an event not pending, that's because the kernel driver raced
    <wumpus> yes, I've had those races, especially on cubox
    <_rmk_> and its caused by three races
    <wumpus> even when using it only from one process :D
    <_rmk_> one in _GetEvent, where it finds an event slot under a lock but has no way to mark it as having been allocated
    <_rmk_> one in gckEVENT_Submit, where you can end up with two events being allocated and then inserted in the reverse order to allocation
    <wumpus> ugh
    <_rmk_> and one in gckEVENT_Notify, where the processing of pending events can screw it up (I need to write that one up properly in the commit)
    <wumpus> I guess some more correct and linuxy synchronization should be used, not some leaky abstractions
    <_rmk_> the final one - _IsEmpty - can work both ways.  This function can report that the event queues are empty when they aren't, and they aren't empty when they are.

    <_rmk_> oh yes, that's the gckEVENT_Notify race - it scans the event queues/lists without holding any locks - it was coded with the assumption that the only thing you need to protect against is writes to queue->head and apparantly reads are fine.
    <_rmk_> the problem that leads to is that you can end up with queue->head non-NULL, queue->stamp being old, and then gckEVENT_Notify() identifies it as a missed event when in actual fact the event queue is in the middle of being setup
    <_rmk_> ... and NULLs out queue->head and runs all the queued events
    <_rmk_> since I've fixed all these, I'm seriously considering getting rid of my fencing in my Xorg driver... it's just not needed anymore

