#!/bin/bash
# Build, upload, execute egl2 on device and fetch data log
DEMO=$1
if [ -z "$DEMO" ]; then
    DEMO="fbtest"
    echo "Defaulting to ${DEMO}"
fi
if [[ "$DEMO" == "ps_sandbox_etna" || "$DEMO" == "etna_test" ]]; then
    ARG="/data/mine/shader.bin"
    ../../tools/asm.py --isa-file ../../rnndb/isa.xml sandbox.asm -o shader.bin
    [ $? -ne 0 ] && exit
    adb push shader.bin ${ARG}
fi
if [[ "$DEMO" == "mip_cube" || "$DEMO" == "mip_cube_raw" ]]; then
    #TEX="miprgba"
    #TEX="mipdxt1"
    #TEX="test_image-dxt3"
    #TEX="mipdxt5"
    #TEX="test_image-dxt1a"
    #TEX="test_image-dxt1c"
    TEX="lavaetc1"
    adb push ../resources/${TEX}.dds /mnt/sdcard
    ARG="/mnt/sdcard/${TEX}.dds"
fi
if [[ "$DEMO" == "particle_system" ]]; then
    TEX="smoke"
    adb push ../resources/${TEX}.tga /mnt/sdcard
    ARG="/mnt/sdcard/${TEX}.tga"
fi
make ${DEMO}
[ $? -ne 0 ] && exit
adb push ${DEMO} /data/mine
adb shell "/data/mine/${DEMO} ${ARG}"
#adb pull /mnt/sdcard/egl2.fdr .
#adb pull /mnt/sdcard/replay.bmp .
