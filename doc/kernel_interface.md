Devices
=======================

At startup, the application connects to galcore device using open with the device

- `/dev/galcore`, or
- `/dev/graphics/galcore`

Then it figures out what the base and size of the contigous memory is using the "query video memory" call.

After that it mmaps the complete contiguous memory (128MB in my case) from device into memory space.

Allocations
------------
Two types of memory are allocated:

Contiguous memory
  Used for command buffers
  Reserved system memory that is contiguous (not fragmented by MMU) and mapped into GPU memory
  It looks like the blob driver also allocates a signal for each contigous memory block, how does this get used?

Linear video memory
  Used for render targets, textures, surfaces, vertex buffers, bitmaps (see `gcvSURF_*`) ...

  Device memory, from one of the pools (default, local, unified or contiguous system memory)
  The available pools depend on the hardware; many of the devices have no local memory, and simply 
  use a part of system memory as video memory.

`LOCK_VIDEO_MEMORY` locks the memory both into the GPU memory so that it can be used by the GPU as
into CPU memory so that the application can read/write. It is interesting that these are done by
the same call.

User signal API
----------------
Commands with op `USER_SIGNAL` are used for signaling between the kernel and userspace
driver.
The subcommands are:

- `USER_SIGNAL_CREATE` Create a new signal
  Inputs: 
     - manualReset
     If set to gcvTRUE, the `SIGNAL` command must be used with state false to
     reset the signal. If set to gcvFALSE, the signal automatically resets
     after waiting for it with `WAIT`.
  Outputs: id

- `USER_SIGNAL_DESTROY` Destroy the signal
  Inputs: id
  Outputs: N/A

- `USER_SIGNAL_SIGNAL` Signal the signal
  Inputs: id, state
    - id    Signal id to signal
    - state If gcvTRUE, the signal will be set to signaled state, if gcvFALSE
             the signal will be set to nonsignaled state.
  Outputs: N/A

- `USER_SIGNAL_WAIT` Wait on the signal (block current thread)
  Inputs: 
    - id     Signal id to wait for
    - wait   Maximum duration to wait (in milliseconds)
  Outputs: N/A

- `USER_SIGNAL_MAP` Map the signal
  Inputs: id
  Outputs: N/A

- `USER_SIGNAL_UNMAP` Same as destroy
  Inputs: id
  Outputs: N/A

This is used to synchronize GPU and CPU
Signals can be scheduled to be signalled/unsignalled when the GPU finished a certain operation (using an Event).
They are also used for inter-thread synchronization by the EGL driver.

The event queue schedules kernel operations to happen in the future.

Events queues are sent to the kernel using `gcvHAL_EVENT_COMMIT`. Types of interfaces that can be sent using an event are:

- `FREE_NON_PAGED_MEMORY`
- `FREE_CONTIGUOUS_MEMORY`
- `FREE_VIDEO_MEMORY`
- `WRITE_DATA`
- `UNLOCK_VIDEO_MEMORY`
- `SIGNAL`
- `UNMAP_USER_MEMORY`

User can then wait for them using `gcvHAL_USER_EVENT`.

Anatomy of a small rendering test
----------------------------------

See `native/replay` examples.

- Get GPU base address
- Get chip identity
- Create user signals for synchronization
- Query video memory
- Allocate contiguous memory A of 0x8000 bytes, physical cdd30b40 logical 484ab000
  -> Command buffer queue
- Allocate contiguous memory B of 0x8000 bytes, physical cde41e40 logical 484f0000
  -> Spare command buffer queue?
- Allocate contiguous memory C of 0x8000 bytes, physical ce699d80 logical 4854b000
  -> Spare command buffer queue?
- Allocate contiguous memory D of 0x8000 bytes, physical cdd30440 logical 485a4000
  -> Spare command buffer queue?
- Allocate linear vidmem E of 0x70000 bytes, type `RENDER_TARGET`, node cf85a2e0
    Main render target
- Allocate linear vidmem F of 0x700 bytes, type `TILE_STATUS`, node d09ab6a8
    looks like the tile status is an auxilary structure, of render target size /0x100 rounded up to 0x100
- Lock vidmem E, address 7f4f4100, memory 477e2100
- Lock vidmem F, address 7a003300, memory 422f1300
- Allocate linear vidmem G  of 0x38000 bytes, type `DEPTH`, node cf8571b0
    Depth surface of main render target
- Allocate linear vidmem H  of 0x400 bytes, type `TILE_STATUS`, node cf8633a8
    Tile status of depth surface
- Lock vidmem G, address 7e468000, memory 46756000
- Lock vidmem H, address 7a002900, memory 422f0900
- Allocate linear vidmem I  of 0x60000 bytes, type `VERTEX`, node cf85f830
    Vertex buffer
- Lock vidmem I, address 7c061d80, memory 4434fd80
- Allocate linear vidmem J  of 0x4000 bytes, type `RENDER_TARGET`, node cf8633e0 (pool SYSTEM)
    What is this? (64x64 aux render target?)
- Allocate linear vidmem K  of 0x100 bytes, type `TILE_STATUS`, node d09a4250
    Tile status of J aux render target
- Lock vidmem J, address 7f284000, memory 47572000
- Lock vidmem K, address 7a002f00, memory 422f0f00
- Build and commit the command buffer

Command buffers
==================
Command buffers are built in user space by the driver in a `gcoCMDBUF` structure, then submitted to the kernel with the 
`gcvHAL_COMMIT` command. 

The following structure fields of `gcoCMDBUF` are used by the kernel:

- object: marks the type of object (`gcvOBJ_COMMANDBUFFER`)
- physical: physical address of command buffer
- logical: logical (user space) address of command buffer
- bytes: size of command buffer memory block in bytes
- startOffset: offset at which to start sending command buffer (in bytes)
- offset: end offset (in bytes)
- free: number of free bytes in command buffer

Context switching
==================
Clients manage their own context, which is passed to COMMIT in case a context switch is needed.

It appears that context switching is manual. Every process has to keep its own context structure for 
context switching, and pass this to COMMIT. In case this is needed the kernel will then load the state
from the context buffer.

The context contains a copy of all state that should be preserved when the context has been switched
(when multiple programs are using the GPU).

This has the form of a giant command stream buffer, accompanied by a state map (an array of offsets
into the command stream buffer for every known state), and the address where to put a link
to the main command buffer.

The state `FE.VERTEX_ELEMENT_CONFIG` is handled specially: write only the elements that are used, starting from 0x00600

Used fields in `_gcoCONTEXT` from the kernel:

- id is used
    [in] This id is used to determine wether to switch context
    [out] A unique id for the context is generated the first time a COMMIT is done, with context->id==0
- `hint*` only used when SECURE_USER
- logical and bufferSize are used
- pipe2DIndex is used
    if this is set, "we have to check pipes", and the pipe is set to initialPipe if needed
- entryPipe is used
    this is the pipe that has to be active on entering the passed command buffer
    (and that holds at the end of the context buffer)
- initialPipe is used 
    this is the pipe that has to be active on entering the context command buffer
- currentPipe is used
    this is the pipe that is active after the passed command buffer
- inUse is used
    value at this address is set to gcvTRUE, to mark the context as used. The context is "used" when
    a context switch happened.

All command buffers are padded with 4 NOPs at the beginning to make place for a PIPE command if needed.
At the end of the command buffer must be place for a LINK (1 NOP + padding).

What are these used for? Seems they are the last parameters of a 'LOAD_STATE' command so that it
  can be extended, but why? Was this only used for building or does the kernel also use it?
- lastAddress
- lastSize
- lastIndex
- lastFixed
- postCommit
- buffer (userspace buffer for what is put into logical)
   same as logical, except that the PIPE2D command at the end is nopped out
   including accompanying semaphore and stall
   (probably because we're using the 3D pipe)

At least in the v2 kernel driver they are not used. They are used for building the buffer from the 
userspace driver, but not for using it.


