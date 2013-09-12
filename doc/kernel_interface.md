Kernel driver
==============

Module parameters
------------------

The exact module parameters available depend on the kernel driver version
(see respective `gc_hal_kernel_driver.c`). Important initialisation parameters are,
along with the values on a RK2918 device:

    baseAddress     0           Physical memory base address
    signal          48          Realtime signal to use for kernel-user communication (only used if USE_NEW_LINUX_SIGNAL)
    bankSize        16777216    Bank size for video memory allocation (16777216 is the usual value)
    contiguousBase  0x78000000  Start physical memory address for "contiguous memory" (unified gpu-cpu memory)
    contiguousSize  0x08000000  Size of "contiguous memory" in bytes. This will be exclusive reserved for the driver from the memory available on the device!
    registerMemSize 16384       Size of MMIO area
    registerMemBase 0x10120000  Base address of MMIO area
    irqLine         41          IRQ line used for signals from GPU
    major           199         Major device node for /dev/galcore

Most important to get right are registerMemSize, registerMemBase and irqLine as these allow the driver to find and
communicate with the GPU hardware. They depend on the board, not on the GPU. For example, on a CuBox these settings are:

    irqLine         42
    registerMemBase 0xf1840000
    contiguousBase  0x08000000

The `dove` (cubox) driver also has a `gpu_frequency` parameter that sets the AXICLK/GCCLK clock at startup,
if compiled with `ENABLE_GPU_CLOCK_BY_DRIVER`. Some devices may need this, although not the CuBox itself (it is disabled in the makefile).
In that case your GPU will have an entry `GC` in `/proc/clocks`.

On a Freescale i.MX6 (GK802) device the parameters are:

    irqLine           41
    irqLine2D         42
    irqLineVG         43
    registerMemBase   0x00130000
    registerMemBase2D 0x00134000
    registerMemBaseVG 0x02204000
    registerMemSize   16384
    registerMemSize2D 16384
    registerMemSizeVG 16384
    contiguousBase    0x34000000
    contiguousSize    0x0c000000  (192 MB)
    coreClock         156000000
    signal            48
    baseAddress       0

Diagnostics
==============

There are various ways to get information about the current status of the GPU from user space.
One of these is the file /proc/driver/gc, which has the following contents (on dove):

    Marvell Technology Group Ltd(GC Ver0.8.0.3184-1)
    DEBUG VERSION
    idle register: 0xfe, hardware is busy
    clockControl register: 0x100
    print mode:     Pid(0) Reset(1) DumpCmdBuf(0)
    GC memory usage profile:
    Total reserved video memory: 65535 KB
    Used video mem: 0 KB    contiguous: 0 KB        virtual: 0 KB
    MMU Entries usage(PageCount): Total(32768), Used(0)

This shows the value of the idle register (`IDLE_STATE`, 0x0004), along with the clock
control register (`CLOCK_CONTROL`, 0x0000), various debug/print flags,
and memory usage information.

/proc/driver/gc can also be used to control the driver, with various commands (`gc_hal_kernel_driver`).

    echo xx > /proc/driver/gc

    printPID

Toggle print PID status.

    powerDebug

Toggle power debug status.

    profile <step> <timeSlice> <tailTimeSlice> <idleThreshold>

Set profiling settings.

    hang

Toggle hang status.

    reset2

Reset GPU.

    memFail <0xFFFFFFFF>

Set memory random fail rate.

    irq <0|1>

Enable or disable GC interrupt line.

    log <0|1|2|3>

Set logging verbosity:

- `0` print nothing
- `1` print error log only
- `2` print warning log only
- `3` print error and warning info

    silentReset <0|1>

Enable (1) or disable (0) silent reset.

    dumpCmdBuf <0|1>

Enable (1) or disable (0) dump command buffer.

    dumpall

Dump all command buffers (only if kernel compiled with `MRVL_PRINT_CMD_BUFFER`).

    offidle

Toggle power off when idle state.

    su

Turn off device power.

    re

Turn on device power.

    stress <count>

Stress test (enable and disable device power) count times.

    debug <level> <zone>

Change debug level. Level is one of:

- `NONE` -1
- `ERROR` 0
- `WARNING` 1
- `INFO` 2
- `VERBOSE` 3

Zone is a bitfield consisting of:

- OS              1
- HARDWARE        2
- HEAP            4
- KERNEL          8
- VIDMEM          16
- COMMAND         32
- DRIVER          64
- CMODEL          128
- MMU             256
- EVENT           512
- DEVICE          1024

The reply in dmesg will show `INFO`, `WARNING` or `ERROR` as `NONE`.

    1 / 2 / 4 / 8 / 16 / 32 / 64

Change frequency to 1/x, use `1` to change to full speed.

User to kernel interface
========================

At startup, the application connects to galcore device using `open` with the device

- `/dev/galcore`, or
- `/dev/graphics/galcore`

After connecting to the device the entire chunk of contiguous memory, after requesting its address and size,
is mapped into user space using `mmap`. The kernel will return addresses in this range when the user space driver allocates
contiguous (unified) memory used for communication with the GPU.

Ioctl
-------

Communication with the kernel driver happens through ioctl calls on the resulting file descriptor. The following request ids are defined:

- `IOCTL_GCHAL_INTERFACE` (30000)
- `IOCTL_GCHAL_KERNEL_INTERFACE` (30001)
- `IOCTL_GCHAL_TERMINATE` (30002)

`IOCTL_GCHAL_INTERFACE` is the only one of these that is actually used by the userspace blob. This ioctl is passed one argument
which is a pointer to the following structure:

    typedef struct
    {
        void *in_buf;
        uint32_t in_buf_size;
        void *out_buf;
        uint32_t out_buf_size;
    } vivante_ioctl_data_t;

When used by the blob, `in_buf` and `out_buf` point to the same memory address: a `gcsHAL_INTERFACE` structure that is
used both for input and output arguments.

Command structure
------------------
The `gcsHAL_INTERFACE` (defined in `gc_hal_driver`) is the structure used by the driver to communicate with the
kernel. It can be seen as a communication packet with a command opcode and an union with parameters.
Depending on the `command` a different field of this union is used. The same structure is used both for input and output
arguments.

For example, the command `gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY` (I will leave off the `gcvHAL_` from now on)
uses the fields in `interface->u.AllocateLinearVideoMemory` to pass in the number of bytes to allocate, but
also to pass out the number of bytes actually allocated.

What is curious about the ioctl protocol is that the communication structures contains fields that are not
used by the kernel at all. There is no good reason why these values would need
to be present in kernel-facing structures. The line is blurry sometimes.
It also appears that the structure has been designed with platform-independence in mind, and so some of the fields are not used in the Linux
drivers such as `status`, `handle`, `pid`.

A possibly worthwhile long-term goal would be to clean up the kernel driver interface. This would break compatibility with
the Vivante binary blobs, though, so maybe the effort would be better spent building a fully-fledged DRM/DRI
infrastructure driver instead.

Allocations
------------
Memory management happens in the kernel. Two types of memory are allocated:

- Contiguous memory

  Used for command buffers
  Allocated with command `ALLOCATE_CONTIGUOUS_MEMORY`

  Reserved system memory that is contiguous (not fragmented by MMU) and mapped into GPU memory
  It looks like the blob driver also allocates a signal for each contigous memory block, how does this get used?

- Linear video memory

  Used for render targets, textures, surfaces, vertex buffers, bitmaps.
  The type of usage is specified by allocating the memory (see `gceSURF_TYPE` in `gc_hal_enum.h`).
  Allocated with command `ALLOCATE_LINEAR_VIDEO_MEMORY`

  Device memory, from one of the pools (default, local, unified or contiguous system memory)
  The available pools depend on the hardware; many of the devices have no local memory, and simply
  use a part of system memory as video memory.

`LOCK_VIDEO_MEMORY` locks the video memory both
- into the GPU memory space so that it can be used by the GPU
- into CPU memory so that the application can read/write.
It is interesting that these are done by
the same call.

Command buffers
-------------------

Like many other GPUs, the primary means of programming the chip is through a command stream
interpreted by a DMA engine. This "Front End" takes care of distributing state changes through
the individual modules of the GPU, kicking off primitive rendering, synchronization,
and also supports some primitive flow control (branch, call, return).

The command stream is submitted to the kernel by means of command buffers. As most important part these
structures contain a pointer to contiguous memory (allocated with command `ALLOCATE_CONTIGUOUS_MEMORY`)
where the commands start.

Command buffers are built in user space by the driver in a `gcoCMDBUF` structure, then submitted to the kernel with the
`COMMIT` command.

The following structure fields of `gcoCMDBUF` are used by the kernel:

- `object`: marks the type of object (`gcvOBJ_COMMANDBUFFER`)
- `physical`: physical address of command buffer
- `logical`: logical (user space) address of command buffer
- `bytes`: size of command buffer memory block in bytes
- `startOffset`: offset at which to start sending command buffer (in bytes)
- `offset`: end offset (in bytes)
- `free`: number of free bytes in command buffer

User signal API
----------------
Command `USER_SIGNAL` is used for synchronization signals between the kernel and userspace driver.

Note: the contents in this section only apply as-is if the kernel was *not* compiled with `USE_NEW_LINUX_SIGNAL`. If this
flag was set, then a posix real-time signal will be used to notify the process of incoming signals, and the
`USER_SIGNAL_WAIT` is a no-op.

The subcommands are:

- `USER_SIGNAL_CREATE` Create a new signal
  Inputs:
     - manualReset
     If set to gcvTRUE, the `SIGNAL` command must be used with state false to
     reset the signal. If set to gcvFALSE, the signal automatically resets
     after waiting for it with `WAIT`.
     - signalType (more recent dove kernel only), type of signal, appearantly only used for debugging

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

This is used to synchronize GPU and CPU.
Signals can be scheduled to be signalled/unsignalled when the GPU finished a certain operation (using an Event).
They are also used for inter-thread synchronization by the EGL driver.

The event queue effectively schedules kernel operations to happen in the future, when the GPU has finished processing the currently
committed command buffers. This can be used to implement, for example, a fenced free that will release a buffer as soon as the GPU
is finished with it.

Event queues are sent to the kernel using the command `HAL_EVENT_COMMIT`. Types of interfaces that can be sent using an event are:

- `FREE_NON_PAGED_MEMORY`: free earlier allocated non paged memory
- `FREE_CONTIGUOUS_MEMORY`: free earier allocated contiguous memory
- `FREE_VIDEO_MEMORY`: free earlier allocated video memory
- `WRITE_DATA`: write data to memory using `writel`
- `UNLOCK_VIDEO_MEMORY`: unlock earlier locked video memory
- `SIGNAL`: command from the signal API described in this section
- `UNMAP_USER_MEMORY`: unmap earlier mapped user memory

Userspace can wait for the signal using `USER_SIGNAL` with subcommand `USER_SIGNAL_WAIT`.

Anatomy of a small rendering test
----------------------------------

See `native/replay` tests for details.

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


Context switching
==================
Clients manage their own context, which is passed to COMMIT preemptively in case a context switch is needed.

It appears that context switching is manual. Every process has to keep its own context structure for
context switching, and pass this to COMMIT. In case this is needed the kernel will then load the state
from the context buffer.

The context contains a copy of all state that should be preserved when the context has been switched
(when multiple programs are using the GPU).

This has the form of a giant command stream buffer, accompanied by a state map (an array of offsets
into the command stream buffer for every known state), and the address where to put a link
to the main command buffer.

The state `FE.VERTEX_ELEMENT_CONFIG` is handled specially: write only the elements that are used, starting from 0x00600

Used fields in `struct _gcoCONTEXT` from the kernel:

- `id`
    [in] This id is used to determine wether to switch context
    [out] A unique id for the context is generated the first time a COMMIT is done, with context->id==0
- `hint*` only used when `SECURE_USER` is set
- `logical` and `bufferSize`  (note: `physical` is not used; the dove version of the driver doesn't even have this field in the default configuration)
- `pipe2DIndex`: if this is set, "we have to check pipes", and the pipe is set to initialPipe if needed
- `entryPipe`: this is the pipe that has to be active on entering the passed command buffer (and that holds at the end of the context buffer)
- `initialPipe`: this is the pipe that has to be active on entering the context command buffer
- `currentPipe`: this is the pipe that is active after the passed command buffer
- `inUse`: value at this address is set to gcvTRUE, to mark the context as used. The context is "used" when a context switch happened.

All command buffers are padded with 4 NOPs at the beginning to make place for a PIPE command if needed.
At the end of the command buffer must be place for a LINK (1 NOP + padding).

The other fields are not used by the kernel, only by the user-space driver internally for various purposes. This makes them
uninteresting from a viewpoint of understanding the kernel interface.

Profiling
===============

To enable profiling, the kernel most have been built with `VIVANTE_PROFILER` enabled in `gc_hal_options.h` or the appropriate
`config` file.

    USE_PROFILER                        = 1

Vivante also recommends disabling power management features while profiling,

    USE_POWER_MANAGEMENT                = 0

HW profiling registers can be read using the command `READ_ALL_PROFILE_REGISTERS`.

There are also the commands `GET_PROFILE_SETTING` and `SET_PROFILE_SETTING`, which set a flag for
logging to a file (`vprofiler.xml` by default), but this flag doesn't do anything in the kernel driver,
likely it's meant to be read out by the user space driver.

This will return a structure `gcsPROFILER_COUNTERS`, defined in `GC_HAL_PROFILER.h`, which has the following timers:

Hardware-wise, the memory controller keeps track of these counters in registers `MC_PROFILE_xx_READ`,
switched by corresponding bits in registers `MC_PROFILE_CONFIGx`.

HW static counters (clock rates). These are never filled in by the kernel, it appears, so will likely contain garbage.

    gpuClock
    axiClock
    shaderClock
    gpuClockStart
    gpuClockEnd

HW variable counters

    gpuCyclesCounter
    gpuTotalRead64BytesPerFrame
    gpuTotalWrite64BytesPerFrame

PE (Pixel engine)

    pe_pixel_count_killed_by_color_pipe
    pe_pixel_count_killed_by_depth_pipe
    pe_pixel_count_drawn_by_color_pipe
    pe_pixel_count_drawn_by_depth_pipe

SH (Shader engine)

    ps_inst_counter
    rendered_pixel_counter
    vs_inst_counter
    rendered_vertice_counter
    vtx_branch_inst_counter
    vtx_texld_inst_counter
    pxl_branch_inst_counter
    pxl_texld_inst_counter

PA (Primitive assembly)

    pa_input_vtx_counter
    pa_input_prim_counter
    pa_output_prim_counter
    pa_depth_clipped_counter
    pa_trivial_rejected_counter
    pa_culled_counter

SE (Setup engine)

    se_culled_triangle_count
    se_culled_lines_count

RA (Rasterizer)

    ra_valid_pixel_count
    ra_total_quad_count
    ra_valid_quad_count_after_early_z
    ra_total_primitive_count
    ra_pipe_cache_miss_counter
    ra_prefetch_cache_miss_counter
    ra_eez_culled_counter

TX (Texture engine)

    tx_total_bilinear_requests
    tx_total_trilinear_requests
    tx_total_discarded_texture_requests
    tx_total_texture_requests
    tx_mem_read_count
    tx_mem_read_in_8B_count
    tx_cache_miss_count
    tx_cache_hit_texel_count
    tx_cache_miss_texel_count

MC (Memory controller)

    mc_total_read_req_8B_from_pipeline
    mc_total_read_req_8B_from_IP
    mc_total_write_req_8B_from_pipeline

HI (Host interface)

    hi_axi_cycles_read_request_stalled
    hi_axi_cycles_write_request_stalled
    hi_axi_cycles_write_data_stalled

Resetting the GPU
-------------------

When the GPU gets stuck, it can be reset with the `RESET` ioctl command. This calls the `gckHARDWARE_Reset` kernel function.

Detailed overview of commands
------------------------------
From enum `gceHAL_COMMAND_CODES`.
Calls: function within the kernel that is called by the dispatcher upon receiving this command.
TODO: input/output arguments.

* `QUERY_VIDEO_MEMORY`

        Query the amount of video memory.

        Calls: gckKERNEL_QueryVideoMemory (see also gckHARDWARE_QueryMemory)

* `QUERY_CHIP_IDENTITY`

        Query chip identity.

        Calls: gckHARDWARE_QueryChipIdentity

* `ALLOCATE_NON_PAGED_MEMORY`

        Allocate non-paged memory.

        Calls: gckOS_AllocateNonPagedMemory

* `FREE_NON_PAGED_MEMORY`

        Free non-paged memory.

        Calls: gckOS_FreeNonPagedMemory

* `ALLOCATE_CONTIGUOUS_MEMORY`

        Allocate contiguous non-paged memory (used for command buffers).

        Calls: gckOS_AllocateContiguous

* `FREE_CONTIGUOUS_MEMORY`

        Free contiguous non-paged memory.

        Calls: gckOS_FreeContiguous

* `ALLOCATE_VIDEO_MEMORY`

        Same as `ALLOCATE_LINEAR_VIDEO_MEMORY`, but kernel does enforced alignment.

        Calls: gckHARDWARE_AlignToTile, gckHARDWARE_ConvertFormat, _AllocateMemory

* `ALLOCATE_LINEAR_VIDEO_MEMORY`

        Allocate video memory of a certain type. The type of memory (gcvSURF_*) is used to determine what
        memory bank to allocate in (for performance reasons).
        Walks all required memory pools to allocate the requested amount of video memory.

        gcvPOOL_VIRTUAL: Virtual memory, allocated using gckVIDMEM_ConstructVirtual
        gcvPOOL_CONTIGUOUS: Contiguous memory, allocated using gckVIDMEM_ConstructVirtual
        gcvPOOL_SYSTEM: Contiguous system memory
        gcvPOOL_LOCAL_INTERNAL: Internal memory
        gcvPOOL_LOCAL_EXTERNAL: External memory
        gcvPOOL_DEFAULT: Same as gcvPOOL_LOCAL_INTERNAL
        gcvPOOL_LOCAL: Same as gcvPOOL_LOCAL_INTERNAL
        gcvPOOL_UNIFIED: Same as gcvPOOL_SYSTEM

        If there is no available free memory in the requested pool, the pools are tried in the following order,
        starting from the requested pool type:
        - gcvPOOL_LOCAL_INTERNAL
        - gcvPOOL_LOCAL_EXTERNAL
        - gcvPOOL_SYSTEM
        - gcvPOOL_CONTIGUOUS
        - gcvPOOL_VIRTUAL

        Calls: gckKERNEL_GetVideoMemoryPool, gckVIDMEM_AllocateLinear

* `FREE_VIDEO_MEMORY`

        Calls: gckVIDMEM_Free

* `MAP_MEMORY`

        Map physical memory into the current process (Physical-to-logical mapping).

        Calls: gckKERNEL_MapMemory (gckOS_MapMemory)

* `UNMAP_MEMORY`

        Unmap memory mapped with `MAP_MEMORY`.

        Calls: gckKERNEL_UnmapMemory (gckOS_UnmapMemory)

* `MAP_USER_MEMORY`

        Lock down a user buffer and return an DMA'able address to be used by the hardware to access it.
        (Logical-to-physical mapping)

        Calls: gckOS_MapUserMemory

* `UNMAP_USER_MEMORY`

        Unlock a user buffer mapped by `MAP_USER_MEMORY`.

        Calls: gckOS_UnmapUserMemory

* `LOCK_VIDEO_MEMORY`

        Surface lock.

        Calls: gckVIDMEM_Lock

* `UNLOCK_VIDEO_MEMORY`

        Surface unlock.

        Calls: gckVIDMEM_Unlock

* `EVENT_COMMIT`

        Commit an event queue.

        Calls: gckEVENT_Commit

* `USER_SIGNAL`

        Dispatch depends on the user signal subcommands (refer to section `User signal API`).
        (if not USE_NEW_LINUX_SIGNAL defined)

        Calls: gckOS_CreateUserSignal, gckOS_DestroyUserSignal, gckOS_SignalUserSignal, gckOS_WaitUserSignal

* `SIGNAL`

        Used in submitted event queues only (refer to section `User signal API`). Not handled by ioctl dispatcher.

* `WRITE_DATA`

        Used in submitted event queues only (refer to section `User signal API`). Not handled by ioctl handler.

* `COMMIT`

        Commit a command and context buffer.

        Calls: gckCOMMAND_Commit

* `STALL`

        Stall the command queue. This is equivalent to queueing a `SIGNAL` using `EVENT_COMMIT` then waiting for it
        using `USER_SIGNAL.WAIT`.

        Calls: gckCOMMAND_Stall

* `READ_REGISTER`

        Read a GPU register. Only enabled if kernel compiled with `gcdREGISTER_ACCESS_FROM_USER` (which
        is obviously an security risk, as it allows user-space to read and write arbitrary registers).

        Calls: gckOS_ReadRegister

* `WRITE_REGISTER`

        Write a GPU register. Only enabled if kernel compiled with `gcdREGISTER_ACCESS_FROM_USER` (which
        is obviously an security risk, as it allows user-space to read and write arbitrary registers).

        Calls: gckOS_WriteRegister

* `GET_PROFILE_SETTING`

        Get profile settings. Only available if kernel compiled with `VIVANTE_PROFILER` enabled.
        Simply copies the "kernel profile filename" to the returned structure from the kernel configuration.

* `SET_PROFILE_SETTING`

        Get profile settings. Only available if kernel compiled with `VIVANTE_PROFILER` enabled.
        Simply copies the "kernel profile filename" from the passed interface structure into the kernel
        configuration.

* `READ_ALL_PROFILE_REGISTERS`

        Read all 3D profile registers. Only available if kernel compiled with `VIVANTE_PROFILER` enabled.

        Calls: gckHARDWARE_QueryProfileRegisters

* `PROFILE_REGISTERS_2D`

        Read all 2D profile registers. Only available if kernel compiled with `VIVANTE_PROFILER` enabled.

        Calls: gckHARDWARE_ProfileEngine2D

* `SET_POWER_MANAGEMENT_STATE`

        Set the power management state.

        Calls: gckHARDWARE_SetPowerManagementState

* `QUERY_POWER_MANAGEMENT_STATE`

        Get the power management state.

        Calls: gckHARDWARE_QueryPowerManagementState / gckHARDWARE_QueryIdle

* `GET_BASE_ADDRESS`

        Get physical base address.

        Out:
        - baseAddress: Physical memory address of internal memory.

        Calls: gckOS_GetBaseAddress

* `SET_IDLE`

        Reserved. Not handled by kernel.

* `QUERY_KERNEL_SETTINGS`

        Get kernel settings.

        Calls: gckKERNEL_QuerySettings

* `RESET`

        Reset the hardware.

        Calls: gckHARDWARE_Reset

* `MAP_PHYSICAL`

        Map physical address into handle.

        Not handled by the kernel on Linux.

* `DEBUG`

        Set debug level and zones.

        Calls: gckOS_SetDebugLevel / gckOS_SetDebugZones

* `CACHE`

        Flush or invalidate the cache.
        NOTE: unimplemented on Linux, and also apparently not called by the blob on Linux.

        In:
          invalidate: If FALSE, flush the cache (the GPU is going to need the data)
                      if TRUE, flush and invalidate the cache (if the GPU is going to modify the data)
          process: Process handle Logical belongs to or gcvNULL if Logical belongs to the kernel.
          logical: Logical address to flush
          bytes: Size of the address range in bytes to flush

        Calls: gckOS_CacheInvalidate / gckOS_CacheFlush

* `BROADCAST_GPU_STUCK`

        Broadcast GPU stuck.

        Calls: gckOS_Broadcast

Crash recovery
================

The GPU sometimes crashes when fed with invalid addresses or commands. In these cases it seems like
rebooting the device is the only way to get control over the GPU back. However the kernel does appear
to contain stuck detection and recovery, which will be researched in this section.
Kernel needs to be compiled with `gcdENABLE_TIMEOUT_DETECTION` enabled in `gc_hal_options.h` for this to work.

- `gckCOMMAND_Stall` broadcasts `BROADCAST_GPU_STUCK` when the stall times out.
- `gckEVENT_Submit` broadcasts GPU stuck when no event IDs are available, and the request time out.

This will print the following message:

    !!FATAL!! GPU Stuck
      idle=0x%08X axi=0x%08X cmd=0x%08X

The contents of the `IDLE_STATE`, `AXI_STATUS` and `DMA_ADDRESS` will be printed and then the function
`gckKERNEL_Recovery` is called which tries to recover the GPU from a fatal error.

- Try to do a a soft reset (`gckHARDWARE_Reset`)
- If not supported, set power management state to `gcvPOWER_OFF_RECOVERY`

XXX how to trigger from user space?

State deltas
=============

The v4 version has abandoned the user-space context approach of the v2 versions, and introduced a
new mechanism with state deltas. The kernel now maintains the current values
of all 3D states for the userspace-driver connection.

A state delta (`gcsSTATE_DELTA`) structure contains new values for a subset of all GPU state
addresses defined in the kernel context.

User space has to generate a state delta structure before (XXX or after?) every COMMIT to let the
kernel know of the changes made in the state buffer.

State deltas have a refcount that tracks the number of contexts that are pending update by the state
delta.

State deltas are not copied from user space until actually needed (due to a context switch). This
means that it is possible to keep updating the current state delta *until* the kernel increases it's
refcount.

When the refcount reaches zero they can be freed. This happens from user space as well.

State delta records form a doubly-linked list. They contains an array of modified states
(`_gcsSTATE_DELTA_RECORD`) in `recordArray` which are (address, mask, data) tuples. The mask is
normally 0xffffffff which means update the whole state, but partial updates are possible as well by
specifying a bitfield.

The `vertexElementCount` of the state delta specifies how many vertex elements (state 00800) are
used. These need to be handled specifically because they must always all be written, in consecutive
order, up to the number of elements actually used (if they are all written, all vertex elements
would be enabled).

Fields `mapEntryID`, `mapEntryIDSize` and `mapEntryIndex` are not used from kernel space.

A context has multiple buffers to prevent (de)allocation overhead; these are stored in a
doubly-linked list and used in round-robin fashion.

Pseudocode (simplified a lot):

    COMMIT(Ctx, CmdBuf, NewStateDelta)
    - If context switch needed (Ctx.id != CurCtx.id)
      - Get current context buffer CurBuf for context Ctx
      - Merge pending state deltas for context Ctx into CurBuf, and reset pending deltas
      - Append NewStateDelta to list of pending deltas of all buffers for context Ctx
      - Send commands in CurBuf to GPU
    - Send commands in CmdBuf to GPU


