#!/bin/bash
# Build, upload, execute egl2 on device and fetch data log
DEMO=$1
if [ -z "$DEMO" ]; then
    DEMO="fbtest"
    echo "Defaulting to ${DEMO}"
fi
make ${DEMO}
adb push ${DEMO} /data/mine
adb shell "/data/mine/${DEMO}"
#adb pull /mnt/sdcard/egl2.fdr .
#adb pull /mnt/sdcard/replay.bmp .
