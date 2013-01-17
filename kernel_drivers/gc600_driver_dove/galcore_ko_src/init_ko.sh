#!/bin/sh

export MARVELL_GCC=1

if [ "$MARVELL_GCC" = "1" ];then
export CROSS_COMPILE=arm-marvell-linux-gnueabi-
export LIB_DIR=/usr/local/arm-marvell-linux-gnueabi/arm-marvell-linux-gnueabi/lib
export PATH=/usr/local/arm-marvell-linux-gnueabi/bin:$PATH
else
export CROSS_COMPILE=arm-linux-
export LIB_DIR=/usr/local/arm-linux-4.1.1/arm-iwmmxt-linux-gnueabi/lib
export PATH=/usr/local/arm-linux-4.1.1/bin:$PATH
fi

export ARCH=arm
export AQROOT=$PWD
export AQARCH=$PWD/arch/XAQ2
export TOOL_DIR=$AQROOT/../resource
export KERNEL_DIR=$TOOL_DIR/linux-2.6.28




