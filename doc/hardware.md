GCxxx hardware
===============

Major optional blocks. Each of these can be present or not depending on the specific chip.

- 2D engine
- Composition engine (multi source blit)
- 3D engine

Feature bits
=================

Variants are somewhat different from NV; what features are supported is not so much determined by the model number 
(which only determines the performance), but determined by various properties that can be found in
read-only registers in the hardware:

 1) Chip features and minor feature flags
 2) Chip specs (number of instructions, pipelines, ...)
 3) Chip model (GC800, GC2000, ...)
 4) Chip revision of the form 0x1234

Generally the chip feature flags are used to distinguish functionality, as well as the specs, and not do much the model 
and revision. Unlike NV, which parametrizes everything on the model and revision, for GC this is left for bugfixes 
(but even these sometimes have their own feature bit).

For an overview of the feature bits see the enumerations in `state.xml`.

Modules
==============
(from Vivante SoCIP 2011 presentation [1])

            ------------------
            | Host Interface |
            ------------------
                    |
         ----------------------
         |  Memory controller |
         ----------------------
          |         |    |    |
          |        \/   \|    |
          |       ---- -----  |
          |       |3D| |Tex|  ----
         \/         Shader--->|  |
       ----        ----       |PE|
       |FE|------->|DE|------>|  |
       ----        ----       ----

Functional blocks, indicated by two-letter abbreviations:

- FE Graphics Pipeline Front End (also: DMA engine, Fetch Engine)
- PE Pixel Engine (can be version 1.0 / 2.0)
- SH SHader (up to 256 threads per shader)
- PA Primitive Assembly (clipping, perspective division, viewport transformation)
- SE Setup Engine (depth offset, scissor, clipping)
- RA RAsterizer (multisampling, clipping, culling, varying interpolation, generate fragments)
- TX Texture
- VG Vector Graphics (not available)
- IM ? (unknown bit in idle state, may group a few other modules)
- FP Fragment Processor (not available, probably was present in older GLES1 HW)
- MC Memory Controller
- HI Host Interface
- DE 2D drawing and scaling engine
- RS Resolve (resolves rendered image to memory, this is a copy and fill engine)
  - VR Video raster (YUV tiler)
  - TS Tile Status

These abbreviations are used in `state.xml` where appropriate.

[1] http://www.socip.org/socip/speech/pdf/2-Vivante-SoCIP%202011%20Presentation.pdf

Operations
-----------

Modules are programmed and kicked off using state updates, queued through the FE.

The GC320 technical manual [1] described quite a few operations, but only for the 2D part (DE).

Hands-on Workshop: Graphics Development on the i.MX 6 Series [2] has some tips specific to programming Vivante 3D hardware,
including OpenCL, but is very high level.

Thread walker = Rectangle walker? (seems to have to do with OpenCL)

[1] http://www.vivantecorp.com/Vivante_GC320_Technical_Reference_Manual_V1.0_A.pdf
[2] http://2012ftf.ccidnet.com/pdf/0049.pdf

Connections 
-------------
Follows the OpenGL pipeline design [3].

- FE2VS (FE-VS) fetch engine to vertex shader: attributes
- RA2SH (RA-PS) rasterizer to shader engine: varyings
- SH2PE (PS-PE) shader to pixel engine: color output

Overall:

    FE -> VS -> PA -> SE -> RA -> PS -> PE -> RS

How does PA/SE fit in this picture? Connection seems to be VS -> PA -> SE -> RA [1]

- PA assembles 3D primitives from vertices, culls based on trivial rejection and clips based on near Z-plane
- PA transforms from 3D view frustum into 2D screen space
- SE determines rasterization starting point for each primitive, and also culls based on trivial rejection
- RA performs per-tile, per-subtile, per-quad and per-pixel clipping

  [1] METHOD FOR DISTRIBUTED CLIPPING OUTSIDE OF VIEW VOLUME 
    http://www.freepatentsonline.com/y2010/0271370.html
  [2] Efficient tile-based rasterization
    http://www.google.com/patents/US8009169
  [3] OpenGL ES2 pipeline structure
    http://www.khronos.org/opengles/2_X/

Command stream
-------------------

Commands and data are sent to the GPU through the FE (Front End interface). The 
command stream of the front-end interface has a specific format described in this section.

Overall format

    OOOOOxxx xxxxxxxx xxxxxxxx xxxxxxxx  Command (O=Opcode, x=argument)
    arg0
    ..
    argN-1

Opcodes
    00001 Update state
    00010 End
    00011 NOP
    00100 Start DE ([15-8] rect count, 1 parameter 0xDEADDEED 2 parameter words describing target rect)
    00101 Draw primitives
    00110 Draw indexed primitives
    00111 Wait ([15-0] count)
    01000 Link ([15-0] number of bytes, arg address)
    01001 Stall (argument seems same format as state 0380C)
    01010 Call 
    01011 Return
    01101 Chip select

Arguments are always padded to 2 32-bit words. Number of arguments depends on the opcode, and 
sometimes on the first word of the command.

Commands (also see cmdstream.xml)

    00001FCC CCCCCCCC AAAAAAAA AAAAAAAA  Update state

      F    Fixed point flag
      C    Count
      A    Base address / 4

Fixed point flag: what this flag does is convert a fixed point float in the command stream
   to a floating point value in the state.


Synchronization
----------------
There are various states related to synchronization, either between different modules in the GPU
and the GPU and the CPU.

- `SEMAPHORE_TOKEN`
- `STALL_TOKEN`
- `STALL` command
(usually from PE to FE)

Semaphore is used when it is available and not in use, otherwise execution of current
thread is stalled

Resolve
-----------
The resolve module is a glorified copy and fill engine. It can copy blocks of pixels
from one GPU address to another, optionally detiling. The source and destination address
can be the same for pixel format conversions, or to fill in tiles that were not touched
during the rendering process.

Tile status (Fast clear)
-------------------------
A render target is divided in tiles, and every tile has a couple of status flags.

An auxilary buffer for each render surface keeps track of tile status flags.

One of these flags is the `clear` flag, that signifies that the tile has been cleared.
`fast clear` happens by setting the clear bit for each tile instead of clearing the actual surface
data.

Tile size is dependent on the hardware, and so is the number of bits per tile.

The tile status bits are cleared using RS, by clearing a small surface with the value
0x55555555. When clearing, only the destination address and stride needs to be set,
the source is ignored.

Shader ISA
================

Vivante GPUs have unified shader ISA, this means that vertex and pixel shaders share the same 
instruction set. See `isa.xml`.

- One operation consists of 4 32-bit words. This have a fixed format, which only differs very little per opcode. Which
instruction fields are used does differ per opcode.

- Four-component SIMD processor

- Older GPUs have floating point operations only, the newer ones have support for integer operations, in the context of compute. 
  The split is around GC1000.

- Instructions can have up to three source operands (`SRC0_*`, `SRC1_*`, `SRC2_*`), and one destination operand (`DST_`). 
   In addition to that, there is a specific operand for texture sampling (`TEX_*`).

- For every operand there are three properties: 
  - `USE`: the operand is enabled
  - `REG`: register number to read or write
  - `SWIZ`: arbitrary swizzle from four to four components (source operands only)
  - `COMPS`: which components to affect (destination operand only)
  - `AMODE`: addressing mode; this can either be direct or indexed through the X,Y,Z,W component of the address register
  - `RGROUP`: choses the register group to read from (source operands only). Register groups are the temporaries, uniforms, and
     possibly others.

- Registers:
  - N temporary registers (actual number depends on the hardware, seems to be at least 64)
  - 1 address register

Programming pecularities
=========================

- The FE can convert from 16.16 fixed point format to 32 bit float. This is enabled by the `fixp` bit
  in the `LOAD_STATE` command.



