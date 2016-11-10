Directory contents
===================

Old etnaviv repository contents, most have not been touched in a long time, and 
are outdated.

- `egl`: egl demos, to check GL functionality

- `replay`: original replay command stream tests (very low level)

- `etnaviv`: libetnaviv low-level command buffer handling library and register definition headers

- `fb`: attempts at rendering to framebuffer using `etna_pipe` (high level gallium-like interface)

- `fb_rawshader`: same as `fb`, but using manually assembled shaders. &lt;GC1000 only.

- `fb_old`: attempts at rendering to framebuffer using raw state queueing (lower level interface)

- `lib`: C files shared between demos and tests, generic math and GL context utilities etc

- `driver`: `etna_pipe` driver and generated hardware header files

- `resources`: meshes, textures used in demos

- `cl`: OpenCL test (like egl, for command stream interception)

- `test2d`: 2D engine tests

- `util`: Various utilities for developing, debugging and profiling for Vivante GPUS

