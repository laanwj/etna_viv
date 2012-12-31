#!/bin/bash
# Build, upload, execute egl2 on device and fetch data log
make cube_replay
adb push cube_replay /data/mine
adb shell '/data/mine/cube_replay'
#adb pull /mnt/sdcard/egl2.fdr .
adb pull /mnt/sdcard/cube_replay.bmp .
