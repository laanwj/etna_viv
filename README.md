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
- Many tablets and such based on Rockchip 2918 SoC (GC800)
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

Vivante has a unified, fixed-size, predictable instruction format with explicit inputs 
and outputs. This does simplify code generation, compared to a weird flow 
pipe system like the Mali 200/400.

Disassembler
-------------

A basic disassembler for the shader instructions can be found in the tools directory:

    tools/disass.py

This can be used to disassemble shaders extracted using `dump_cmdstream.py --dump-shaders`.

An assembler still needs to be written.

Command stream format
-----------------------
    
Command stream format represented in rules-ng-ng XML format. 

    rnndb/cmdstream.xml

Most of the relevant bits have been deciphered.

Command stream interception
----------------------------

    native/egl/*.c

Use ELF hooks to intercept ioctl and mmap calls from libGAL (the Vivante user space blob)
to the kernel driver in GLES2 examples.

At the beginning of the program call `the_hook`, at the end of the program call `end_hook` to finalize 
and flush buffers.

The raw binary structures interchanged with the kernel are written to disk in a `.fdr` file, along 
with updates to video memory, to be parsed by the accompanying tools.

Command stream dumper
----------------------

Other tools live in:

    tools/

- `show_egl2_log.sh` (uses `dump_cmdstream.py`)

Decodes and dumps the intercepted command stream in human readable format, making use of rnndb state map.

- `fdr_dump_mem.py`

Helps extract areas of video memory and command buffers at certain points of execution.

Replay test
--------------

![Example output](https://raw.github.com/laanwj/etna_viv/master/native/replay/cube_replay.png)

![Example output 2](https://raw.github.com/laanwj/etna_viv/master/native/replay/cube_companion_replay.png)

    native/replay/

Replays the command stream and ioctl commands of the EGL demos, to get the same output. 

Currently this is available for the `cube` example that renders a smoothed cube, and the `cube_companion`
example that renders a textured cube.

Vivante GPL kernel driver
--------------------------

The headers and implementation files for the Vivante GPL kernel driver are also included:

    kernel_drivers/

Both GPL kernel driver versions, v2 and v4, are provided. They are useful in understanding the kernel interface, and the 
hardware at a basic level.

The drivers are rooted in the kernel tree at `drivers/gpu/vivante`.

Envytools
----------

The repository also contains a slightly modified subset of envytools for header generation from 
the state / cmdstream / isa rnndb files, so they can be used from C code, build with

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
`native/lib/egl`. This is not needed if you just want to build the replay example.

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
 
