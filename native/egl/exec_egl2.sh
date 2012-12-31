#!/bin/bash
# Build, upload, execute egl2 on device and fetch data log
make cube
adb push cube /data/mine
adb shell 'export LD_LIBRARY_PATH="/system/lib/egl";/data/mine/cube'
adb pull /mnt/sdcard/egl2.fdr .
adb pull /mnt/sdcard/egl2.bmp .
