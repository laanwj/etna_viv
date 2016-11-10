#!/bin/bash
# Build, upload, execute egl2 on device and fetch data log
DEMO=$1
if [ -z "$DEMO" ]; then
    echo "Defaulting to cube"
    DEMO="cube"
fi
IMAGE_OUT="/mnt/sdcard/replay.bmp"
if [[ "$DEMO" == "ps_sandbox_etna" || "$DEMO" == "etna_test" ]]; then
    ARG="/data/mine/shader.bin"
    ../../tools/asm.py --isa-file ../../rnndb/isa.xml sandbox.asm -o shader.bin
    [ $? -ne 0 ] && exit
    adb push shader.bin ${ARG}
fi
make ${DEMO}
[ $? -ne 0 ] && exit

adb push ${DEMO} /data/mine
adb shell "/data/mine/${DEMO} ${ARG} ${IMAGE_OUT}"
#adb pull /mnt/sdcard/egl2.fdr .
adb pull ${IMAGE_OUT} .
