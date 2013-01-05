#!/bin/bash
# Build, upload, execute egl2 on device and fetch data log
DEMO=$1
if [ -z "$DEMO" ]; then
    echo "Defaulting to cube"
    DEMO="cube"
fi
make ${DEMO}
adb push ${DEMO} /data/mine
adb shell 'export LD_LIBRARY_PATH="/system/lib/egl";/data/mine/'"${DEMO}"
adb pull /mnt/sdcard/egl2.fdr ${DEMO}.fdr
adb pull /mnt/sdcard/egl2.bmp ${DEMO}.bmp
