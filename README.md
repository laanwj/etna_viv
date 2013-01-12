Welcome to the Etna_viv project.

Introduction
=================

Project Etnaviv is an attempt to make an open source user-space driver for the Vivante GCxxx series of embedded GPUs.

The current state of the project is experimental. It is currently only of use to developers interested
in helping develop open source drivers for the hardware, reverse engineering, or in interfacing with GPU 
hardware directly. It is NOT usable as a driver for end users.

Once the understanding of the hardware is good enough, we'd likely want to fork Mesa/Gallium and create a GL driver.

Devices with Vivante GPU
=========================

- Google TV (Marvell Armada 1500, contains a GC1000)
- OLPC (also Marvell Armada something with GC1000)
- CuBox, including pro variant (Marvell Armada 510, GC600)
- Many older tablets and such based on Rockchip 2918 SoC (GC800)
- Devices based on Freescale i.MX6 Series (GC2000, GC320, GC355)

See also https://en.wikipedia.org/wiki/Vivante_Corporation

Contents
==========

The repository contains different tools and documentation related to figuring out how to 
program Vivante GCxxx GPU chips.

State map
-----------

Known render state and registers overview. Mapped in rules-ng-ng (envytools) format:

    rnndb/state.xml

Other scattered bits of documentation about the hardware and ISA can be found in `doc/hardware.md`.

ISA documentation
------------------
    
Shader (both vertex and fragment) instruction set description in rules-ng-ng format can be found here: 

    rnndb/isa.xml

Some written down notes, and examples of disassembled shaders can be found here:

    doc/isa.md

Vivante has a unified, fixed-size, predictable instruction format with explicit inputs 
and outputs. This does simplify code generation, compared to a weird flow 
pipe system like the Mali 200/400.

Assembler and disassembler
----------------------------

A basic disassembler for the shader instructions can be found in the tools directory:

    tools/disasm.py rnn/isa.xml <shader.bin>

This can be used to disassemble shaders extracted using `dump_cmdstream.py --dump-shaders`.

There is also an assembler:

    tools/asm.py rnn/isa.xml <shader.asm> -o <shader.bin>

Command stream format
-----------------------

Like many other GPUs, the primary means of programming the chip is through a command stream 
interpreted by a DMA engine. This "Front End" takes care of distributing state changes through
the individual modules of the GPU, kicking off primitive rendering, synchronization, 
and also supports some primitive flow control (branch, call, return).

Most of the relevant bits of this command stream have been deciphered.

The command stream format represented in rules-ng-ng XML format can be found here:

    rnndb/cmdstream.xml


Command stream interception
----------------------------

`viv_hook`: A library to intercept and log the traffic between libGAL (the Vivante user space blob) and the kernel
driver / hardware.

It uses ELF hooks to intercept only system calls such as ioctl and mmap coming from the driver, not from
other parts of the application (unlike more crude hacks using `LD_PRELOAD`).

At the beginning of the program call `the_hook`, at the end of the program call `end_hook` to finalize 
and flush buffers. This should even work for native android applications that fork from the zygote.

The raw binary structures interchanged with the kernel are written to disk in a `.fdr` file, along 
with updates to video memory, to be parsed by the accompanying command stream dumper and other tools.

    native/egl/*.c


Command stream dumper
----------------------

Other tools live in:

    tools/

- `show_egl2_log.sh` (uses `dump_cmdstream.py`)

Decodes and dumps the intercepted command stream in human readable format, making use of rnndb state map.

- `fdr_dump_mem.py`

Helps extract areas of video memory, images, and command buffers at certain points of execution.

Replay test
--------------

![Example output](https://raw.github.com/laanwj/etna_viv/master/native/replay/cube_replay.png)

![Example output 2](https://raw.github.com/laanwj/etna_viv/master/native/replay/cube_companion_replay.png)

    native/replay/

Replays the command stream and ioctl commands of the EGL demos, to get the same output. 

Currently this is available for the `cube` example that renders a smoothed cube, and the `cube_companion`
example that renders a textured cube.

Command stream builder
-----------------------

A beginning has been made of a simple low-level driver that builds the command stream from scratch and submits
it to the kernel driver:

    native/lib/viv.(c|h)
    native/replay/cube_etna.c (renders the GLES2 smoothed cube)
    native/replay/ps_sandbox_etna.c (to experiment with shaders)

Vivante GPL kernel driver
--------------------------

The headers and implementation files for the Vivante GPL kernel driver are also included:

    kernel_drivers/

Both GPL kernel driver versions, v2 and v4, are provided. They are useful in understanding the kernel interface, and the 
hardware at a basic level.

The drivers are rooted in the kernel tree at `drivers/gpu/vivante`.

In addition to the `v2` and `v4` kernel drivers included in the archive there is another a kernel driver
(0.8.x) part of the Linux mach-dove architecture as used for Cubox:
https://github.com/rabeeh/linux/tree/master/arch/arm/mach-dove/gc600_driver_dove
It has another slightly different (likely predating `v2`) interface.

Envytools fork
---------------

Envytools (https://github.com/pathscale/envytools) is a set of tools aimed at developers of the open source
NVidia driver Nouveau, however some parts such as rnndb be more generally applied. The repository 
contains a slightly modified subset of envytools for header generation from 
the state / cmdstream / isa rnndb files, so they can be used from the C code, build with

    cd envytools
    mkdir build
    cd build
    cmake ..
    make
    cd ../..

Then generate the headers with

    rnndb/gen_headers.sh

Building
=========

Install the Android NDK and define a native build environment, for example like this:

    export NDK="/opt/ndk"
    export TOOLCHAIN="/opt/ndk/toolchains/arm-linux-androideabi-4.6/prebuilt/linux-x86"
    export SYSROOT="/opt/ndk/platforms/android-14/arch-arm"
    export PATH="$PATH:$TOOLCHAIN/bin"

To build the egl samples, you need to copy `libEGL_VIVANTE.so` `libGLESv2_VIVANTE.so` from the device `/system/lib/egl` to
`native/lib/egl`. This is not needed if you just want to build the replay or etna tests, which do not rely in any way on the
userspace blob.

Run make in `native/replay` and `native/egl` separately.

Compatibility
================

My primary development device is an Android tablet based on Rockchip RK2918, containing a GC800 GPU.
It has pretty ancient kernel driver 2.2.2.

I do not currently have a device with a newer chip or driver, so every statement about those devices
is based on educated guesses.

In case your kernel uses 4.x kernel driver
- use the state dumper with `gcs_hal_interface_v4.json` instead of `gcs_hal_interface_v2.json`
- provide the right kernel headers when building the `egl` samples
- define `V4` in `viv_hook.c` (context is handled with state deltas in v4)

If you have a different device, or one based on a different operating system than Android (such as bare Linux), 
some modifications to the build system may be necessary to make it compatible. Let me know if you get it to work.

The command stream on different device GCxxx variants will also likely be slightly different; the features bit system
allows for a ton of slightly different chips. When porting it, look for:

- Tile size for textures and render targets

- Number of bits per tile (2 on my hw), depends on `2BIT_PER_TILE` feature flag

- depth buffer (hierarchical or normal)

- location of shader memory in state space (will be at 0x0C000/0x08000 or 0x20000 for recent models with more than
     256 shader instructions support)

Authors
========
- Wladimir J. van der Laan

Thanks
=======
- Luc Verhaegen (libv) of Lima project (basic framework, general idea)
- Nouveau developers (rnndb, envytools)
 
