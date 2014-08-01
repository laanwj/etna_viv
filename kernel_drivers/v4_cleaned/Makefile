#
# Open Vivante driver Makefile.
#
ccflags-$(CONFIG_GPU_VIVANTE_NEWSIGNAL) += -DUSE_NEW_LINUX_SIGNAL=1
ccflags-$(CONFIG_GPU_VIVANTE_PROFILER) += -DVIVANTE_PROFILER=1
ccflags-$(CONFIG_GPU_VIVANTE_DEBUG) += -DgcdDEBUG=gcdDEBUG_ALL
ccflags-$(CONFIG_GPU_VIVANTE_NO_DMA_COHERENT) += -DNO_DMA_COHERENT

#ccflags-y += -DUSE_PLATFORM_DRIVER=1
ccflags-y += -DENUM_WORKAROUND=0
ccflags-y += -DNO_USER_DIRECT_ACCESS_FROM_KERNEL=1
ccflags-y += -DgcdPAGED_MEMORY_CACHEABLE=0
ccflags-y += -DgcdNONPAGED_MEMORY_CACHEABLE=0
ccflags-y += -DgcdNONPAGED_MEMORY_BUFFERABLE=1
ccflags-y += -DgcdCACHE_FUNCTION_UNIMPLEMENTED=0
ccflags-y += -DgcdSUPPORT_SWAP_RECTANGLE=0
ccflags-y += -DgcdENABLE_OUTER_CACHE_PATCH=0
ccflags-y += -DgcdUSE_OPENCL=0

#
# Vivante GC860 for Ingenic JZ4770 SOC.
#
ifneq ($(CONFIG_GPU_VIVANTE_GC860), )
ccflags-y += \
	-DgcdMMU_SIZE=262144 \
	-DgcdPOWER_CONTROL_DELAY=1 \
	-DgcdPOWER_MANAGEMENT=0 \
	-DUSE_PLATFORM_DRIVER=0 \
	-DNO_DMA_COHERENT
endif

#
# Include paths.
#
ccflags-y += \
	-I$(obj) \
	-Iinclude/uapi/vivante

obj-$(CONFIG_GPU_VIVANTE_V4) += galcore_v4.o

galcore_v4-objs += \
	gc_hal_kernel.o \
	gc_hal_kernel_command.o \
	gc_hal_kernel_context.o \
	gc_hal_kernel_db.o \
	gc_hal_kernel_debug.o \
	gc_hal_kernel_device.o \
	gc_hal_kernel_driver.o \
	gc_hal_kernel_event.o \
	gc_hal_kernel_hardware.o \
	gc_hal_kernel_linux.o \
	gc_hal_kernel_mmu.o \
	gc_hal_kernel_os.o \
	gc_hal_kernel_video_memory.o
