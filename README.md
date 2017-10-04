Introduction
=================

Project Etnaviv is an open source user-space driver for the Vivante GCxxx series of embedded GPUs.

**This repository contains reverse-engineering and debugging tools, and `rnndb` register documentation. It is
not necessary to use this repository when building the driver.**

Instead, use:

- https://github.com/etnaviv/mesa - Mesa driver (not yet upstreamed)
- https://cgit.freedesktop.org/mesa/drm/ - Libdrm (etnaviv was upstreamed)
- A recent mainline linux kernel (etnaviv was upstreamed)

SoCs with Vivante GPU
=========================

ARM-based:
- Marvell 88SV331x has a GC530
- Marvell Armada 510 has a GC600: [CuBox](http://www.solid-run.com/cubox)
- Marvell Armada 610 has a GC860: OLPC XO-1.75
- Marvell Armada 1500 has a GC1000: Google TV
- Marvell PXA2128 has a GC2000 (OLPC XO-4)
- Rockchip 2918 has a GC800: some Arnova tablets
- Freescale i.MX6 Quad and Dual have a GC2000 + GC320 + GC355
- Freescale i.MX6 QuadPlus and DualPlus have a GC3000 + GC320 + GC355
- Freescale i.MX6 DualLite and Solo have a GC880 + GC320
- Freescale i.MX6 SoloLite has GC320 + GC355 (no 3D)
- Actions Semiconductor ATM7029 has a GC1000

MIPS-based:
- Ingenic JZ4760 has a GC200 (2D only)
- Ingenic JZ4770 has a GC860: [GCW Zero](http://www.gcw-zero.com)

See also [wikipedia](https://en.wikipedia.org/wiki/Vivante_Corporation).

For the Vivante GPUs on many platforms feature bits have been determined, these can be found in
[doc/gpus_comparison.html](http://dev.visucore.com/etna_viv/gpus_comparison.html).

Contents
==========

The repository contains various tools and documentation related to figuring out how to
program Vivante GCxxx GPU cores.

Debugging support
------------------

Etnaviv comes with a GDB plugin for `etna` driver debugging. GDB 7.5+ with Python support (usually enabled
by default in distributions) is needed for it to work. This plugin adds a few custom commands.

Usage (from gdb):

    source /path/to/etnaviv_gdb.py

Commands:

- gpu-state [&lt;prefix&gt;]
  gpu-state uniforms

  Show full GPU state by default or a subset of the registers with a certain prefix.
  The special prefix 'uniforms' shows the shader uniforms.

- gpu-dis

  Disassemble the currently bound fragment and vertex shaders.

- gpu-trace

  Trace and dump all submitted command buffers. This is similar to dumping to FDR
  (using one of the hook mechanisms) and then running `dump_cmdstream`, however this
  works on the fly.

  Along with each command the physical address is printed, this should come in handy for
  searching back the physical address that the GPU is stuck on
  according to the kernel.

   Usage:
      gpu-trace <on|off>      Enable/disable cmdbuffer trace
      gpu-trace stop <on|off> Enable/disable stopping on every commit
      gpu-trace output stdout Set tracing output to stdout (default)
      gpu-trace output file <name>   Set tracing output to file

These commands automatically find the gallium pipe and screen from the current Mesa
context.

State map
----------

Map of documentation for known render state and registers. Mapped in rules-ng-ng (envytools) format:

    rnndb/state.xml     Top-level database, global state
    rnndb/state_hi.xml  Host interface registers
    rnndb/state_2d.xml  2D engine state
    rnndb/state_3d.xml  3D engine state
    rnndb/state_vg.xml  VG engine state (stub)
    rnndb/state_common.xml  Common, shared state defines

Other scattered bits of documentation about the hardware and ISA can be found in `doc/hardware.md`.

ISA documentation
------------------

Vivante has a unified, fixed-size, predictable instruction format with explicit inputs
and outputs. This does simplify code generation, compared to a weird flow
pipe system like the Mali 200/400.
Shader (both vertex and fragment) instruction set description in rules-ng-ng format can be found here:

    rnndb/isa.xml

Some written down notes, and examples of disassembled shaders can be found here:

    doc/isa.md

Assembler and disassembler
----------------------------

A basic disassembler for the shader instructions (to a custom format) can be found in the tools directory:

    tools/disasm.py <shader.bin>

This can be used to disassemble shaders extracted using `dump_cmdstream.py --dump-shaders`.

There is also an assembler, which accepts the same syntax that is produced by the disassembler:

    tools/asm.py <shader.asm> [-o <shader.bin>]

Command stream format
-----------------------

Like other modern GPUs, the primary means of programming the chip is through a command stream
interpreted by a DMA engine. This "Front End" takes care of distributing state changes through
the individual modules of the GPU, kicking off primitive rendering, synchronization,
and also supports basic flow control (branch, call, return).

Most of the relevant bits of this command stream have been deciphered.

The command stream format represented in rules-ng-ng XML format can be found here:

    rnndb/cmdstream.xml

Command stream interception
----------------------------

A significant part of reverse engineering was done by intercepting command streams while running GL demos
and examples.

Command stream interception functionality (`libvivhook` and `viv_interpose.so`) moved to the [libvivhook](https://github.com/etnaviv/libvivhook)
repository. The tools to parse and dump intercepted command streams will remain in this repository.

Command stream dumper
----------------------

Other tools live in:

    tools/

The most useful ones, aside from the assembler and disassembler mentioned before are:

- `dump_cmdstream.py` Decodes and dumps the intercepted command stream in human readable format, making use of rnndb state maps.

- `fdr_dump_mem.py` Extract areas of video memory, images, and command buffers at certain points of execution.

Vivante GPL kernel drivers
---------------------------

These have been moved to https://github.com/etnaviv/vivante_kernel_drivers

Envytools fork
---------------

[Envytools](https://github.com/pathscale/envytools) is a set of tools aimed at developers of the open source
NVIDIA driver Nouveau, however some parts such as rnndb can be applied more generally. The repository
contains a slightly modified subset of envytools for header generation from
the state / command stream / ISA rnndb files, so they can be used from the C code (etna), build with

    cd envytools
    mkdir build
    cd build
    cmake ..
    make
    cd ../..

Then generate the headers with

    rnndb/gen_headers.sh

Contact
=============

There is a [freedesktop.org mailing list](https://lists.freedesktop.org/mailman/listinfo/etnaviv) for the project.

There is also a Google group for development discussion for this project at
[etnaviv-devel](https://groups.google.com/forum/#!forum/etnaviv-devel), but we
are in process of switching to the above freedesktop mailing list.

We usually hang out in `#etnaviv` on `irc.freenode.net`. 

Authors
========

- Wladimir J. van der Laan
- Steven J. Hill (kernel driver help)
- Christian Gmeiner (beginnings of GC2000 support)
- Michał Ściubidło (GC880 support)
- Maarten ter Huurne (GCW kernel driver, `v4_uapi` interface)

Thanks
=======

- Luc Verhaegen (libv) of Lima project (basic framework, general idea)
- Nouveau developers (rnndb, envytools)

