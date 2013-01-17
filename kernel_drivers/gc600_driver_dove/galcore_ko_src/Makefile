PWD := $(srctree)/arch/arm/mach-dove/gc600_driver_dove/galcore_ko_src

ARCH=arm
#CROSS_COMPILE ?= /usr/local/arm-marvell-linux-gnueabi/bin/arm-marvell-linux-gnueabi-
GPU_TYPE = unified
KERNEL_DIR = ${PWD}/linux

DEBUG = 0
ABI = aapcs-linux
ENABLE_ARM_L2_CACHE = 1
NO_DMA_COHERENT = 1
CONFIG_DOVE_GPU = 1
ANDROID = 0
ENABLE_GPU_CLOCK_BY_DRIVER = 0

BUILD_OPTIONS  = DEBUG=${DEBUG}
BUILD_OPTIONS += ABI=${ABI}
BUILD_OPTIONS += ENABLE_ARM_L2_CACHE=${ENABLE_ARM_L2_CACHE}
BUILD_OPTIONS += NO_DMA_COHERENT=${NO_DMA_COHERENT}
BUILD_OPTIONS += CONFIG_DOVE_GPU=${CONFIG_DOVE_GPU}
BUILD_OPTIONS += ANDROID=${ANDROID}
BUILD_OPTIONS += ENABLE_GPU_CLOCK_BY_DRIVER=${ENABLE_GPU_CLOCK_BY_DRIVER}

#AQROOT := ${PWD}
AQROOT := $(srctree)/arch/arm/mach-dove/gc600_driver_dove/galcore_ko_src
AQARCH=${PWD}/arch/${GPU_TYPE}
GCX00_COMPILE_ENV = ${BUILD_OPTIONS} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}\
	AQROOT=${PWD}  AQARCH=${PWD}/arch/${GPU_TYPE} KERNEL_DIR=${KERNEL_DIR}

# from Kbuild
include $(AQROOT)/config

DRIVER_OUT_DIR = hal/driver
KERNEL_DIR ?= $(TOOL_DIR)/kernel

OS_KERNEL_DIR   := hal/os/linux/kernel
ARCH_KERNEL_DIR := arch/$(notdir $(AQARCH))/hal/kernel
HAL_KERNEL_DIR  := hal/kernel

EXTRA_CFLAGS += -Werror

OBJS := $(OS_KERNEL_DIR)/gc_hal_kernel_debug.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_device.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_driver.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_linux.o \
        $(OS_KERNEL_DIR)/gc_hal_kernel_os.o

ifeq ($(USE_3D_VG), 1)

OBJS += $(HAL_KERNEL_DIR)/gc_hal_kernel.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_command.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_event.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_heap.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_mmu.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_video_memory.o

OBJS += $(ARCH_KERNEL_DIR)/gc_hal_kernel_hardware.o

else

OBJS += $(HAL_KERNEL_DIR)/gc_hal_kernel.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_command.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_heap.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_interrupt.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_mmu.o \
        $(HAL_KERNEL_DIR)/gc_hal_kernel_video_memory.o

OBJS += $(ARCH_KERNEL_DIR)/gc_hal_kernel_hardware.o \
        $(ARCH_KERNEL_DIR)/gc_hal_kernel_hardware_command.o

endif

ifeq ($(KERNELRELEASE), )

.PHONY: all clean install

# Define targets.
all:
	@mkdir -p $(DRIVER_OUT_DIR)
	@make V=$(V) ARCH=$(ARCH_TYPE) -C $(KERNEL_DIR) SUBDIRS=`pwd` modules

clean:
	@rm -rf $(OBJS)
	@rm -rf $(DRIVER_OUT_DIR)
	@rm -rf modules.order Module.symvers

install: all
	@mkdir -p $(SDK_DIR)/drivers
	@cp $(DRIVER_OUT_DIR)/galcore.ko $(SDK_DIR)/drivers

else


EXTRA_CFLAGS += -DLINUX -DDRIVER

ifeq ($(ENUM_WORKAROUND), 1)
EXTRA_CFLAGS += -DENUM_WORKAROUND=1
else
EXTRA_CFLAGS += -DENUM_WORKAROUND=0
endif

ifeq ($(FLAREON),1)
EXTRA_CFLAGS += -DFLAREON
endif

ifeq ($(DEBUG), 1)
EXTRA_CFLAGS += -DDBG=1 -DDEBUG -D_DEBUG
else
EXTRA_CFLAGS += -DDBG=0
endif

ifeq ($(NO_DMA_COHERENT), 1)
EXTRA_CFLAGS += -DNO_DMA_COHERENT
endif

ifeq ($(ENABLE_ARM_L2_CACHE), 1)
EXTRA_CFLAGS += -DENABLE_ARM_L2_CACHE=1
else
EXTRA_CFLAGS += -DENABLE_ARM_L2_CACHE=0
endif

ifeq ($(CONFIG_DOVE_GPU), 1)
EXTRA_CFLAGS += -DCONFIG_DOVE_GPU=1
endif

ifneq ($(USE_PLATFORM_DRIVER), 0)
EXTRA_CFLAGS += -DUSE_PLATFORM_DRIVER=1
else
EXTRA_CFLAGS += -DUSE_PLATFORM_DRIVER=0
endif

ifeq ($(USE_PROFILER), 1)
    EXTRA_CFLAGS += -DVIVANTE_PROFILER=1
else
    EXTRA_CFLAGS += -DVIVANTE_PROFILER=0
endif

ifeq ($(USE_MRVL_PROFILER),1)
    EXTRA_CFLAGS += -DMRVL_BENCH=1
else
    EXTRA_CFLAGS += -DMRVL_BENCH=0
endif


ifeq ($(ANDROID), 1)
EXTRA_CFLAGS += -DANDROID=1
endif

ifeq ($(ENABLE_GPU_CLOCK_BY_DRIVER), 1)
EXTRA_CFLAGS += -DENABLE_GPU_CLOCK_BY_DRIVER=1
else
EXTRA_CFLAGS += -DENABLE_GPU_CLOCK_BY_DRIVER=0
endif

ifeq ($(USE_NEW_LINUX_SIGNAL), 1)
EXTRA_CFLAGS += -DUSE_NEW_LINUX_SIGNAL=1
else
EXTRA_CFLAGS += -DUSE_NEW_LINUX_SIGNAL=0
endif

ifeq ($(NO_USER_DIRECT_ACCESS_FROM_KERNEL), 1)
EXTRA_CFLAGS += -DNO_USER_DIRECT_ACCESS_FROM_KERNEL=1
else
EXTRA_CFLAGS += -DNO_USER_DIRECT_ACCESS_FROM_KERNEL=0
endif

EXTRA_CFLAGS += -I$(AQROOT)/hal/inc
EXTRA_CFLAGS += -I$(AQROOT)/hal/kernel
EXTRA_CFLAGS += -I$(AQARCH)/hal/kernel
EXTRA_CFLAGS += -I$(AQARCH)/cmodel/inc
EXTRA_CFLAGS += -I$(AQROOT)/hal/user

obj-y = galcore.o
galcore-objs  = $(OBJS)

endif


#all:
#	make ${GCX00_COMPILE_ENV} -C ${PWD} -f Kbuild
#	cp $(PWD)/hal/driver/galcore.ko .

#clean:
#	make ${GCX00_COMPILE_ENV} -C ${PWD} -f Kbuild clean



