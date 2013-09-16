#!/bin/bash
TARGET=root@10.1.1.2

rsync -zv alpha_blend cube_companion cubemap_sphere displacement etna_gears\
    mip_cube particle_system rotate_cube stencil_test downsample_test ps_sandbox \
    $TARGET:

