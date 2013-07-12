#!/bin/bash
TARGET=cubox

rsync -zv alpha_blend cube_companion cubemap_sphere displacement etna_gears mip_cube particle_system rotate_cube stencil_test rotate_cube_mult $TARGET:
cd ../../../
rsync -zar --include \*/ --include \*.c --include \*.h --exclude \* etna_viv cubox:

