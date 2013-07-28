#!/bin/bash
TARGET=cubox

rsync -zv alpha_blend cube_companion cubemap_sphere displacement etna_gears mip_cube particle_system rotate_cube stencil_test viv_profile ps_sandbox $TARGET:
cd ../../../
rsync -zar --include \*/ \
    --include \*.c \
    --include \*.h \
    --include \*.py \
    --include \*.xml \
    --exclude \* etna_viv cubox:

