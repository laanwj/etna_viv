#
# Makefile for Vivante GPU driver

debug     = 0
profiler  = 0
newsignal = 0
pm        = 0

bank_bit_start = 0
bank_bit_end = 0
bank_channel_bit = 0

ifeq ($(CONFIG_GPU_VIVANTE_DEBUG),y)
  debug = 1
endif

ifeq ($(CONFIG_GPU_VIVANTE_PROFILER),y)
  profiler = 1
endif

ifeq ($(CONFIG_GPU_VIVANTE_NEWSIGNAL),y)
  newsignal = 1
endif

ifeq ($(CONFIG_GPU_VIVANTE_POWER_MANAGEMENT),y)
  pm = 1
endif

ccflags-y += -DENUM_WORKAROUND=0

ccflags-y                           += -DDBG=$(debug)
ccflags-$(CONFIG_GPU_VIVANTE_DEBUG) += -DDEBUG -D_DEBUG

ifeq ($(CONFIG_GPU_VIVANTE_NO_DMA_COHERENT),y)
  ccflags-y += -DNO_DMA_COHERENT
endif

ccflags-y += -DgcdPOWER_MANAGEMENT=$(pm)
ccflags-y += -DUSE_PLATFORM_DRIVER=1
ccflags-y += -DVIVANTE_PROFILER=$(profiler)

ifeq ($(CONFIG_GPU_VIVANTE_ANDROID),y)
  ccflags-y += -DANDROID=1
endif

ccflags-y += -DUSE_NEW_LINUX_SIGNAL=$(newsignal)
ccflags-y += -DNO_USER_DIRECT_ACCESS_FROM_KERNEL=1
ccflags-y += -DgcdPAGED_MEMORY_CACHEABLE=0
ccflags-y += -DgcdNONPAGED_MEMORY_CACHEABLE=0
ccflags-y += -DgcdNONPAGED_MEMORY_BUFFERABLE=1
ccflags-y += -DgcdCACHE_FUNCTION_UNIMPLEMENTED=0
ccflags-y += -DgcdENABLE_VG=0
ccflags-y += -DgcdSUPPORT_SWAP_RECTANGLE=0
ccflags-y += -DgcdENABLE_OUTER_CACHE_PATCH=0
ccflags-y += -DgcdUSE_OPENCL=0

ifeq ($(CONFIG_SMP), y)
  ccflags-y += -DgcdSMP=1
else
  ccflags-y += -DgcdSMP=0
endif


ifeq ($(CONFIG_GPU_VIVANTE_USE_BANK_ALIGNMENT), y)
    ccflags-y  += -DgcdENABLE_BANK_ALIGNMENT=1
    ifneq ($(bank_bit_start), 0)
	        ifneq ($(bank_bit_end), 0)
	            ccflags-y  += -DgcdBANK_BIT_START=$(bank_bit_start)
	            ccflags-y  += -DgcdBANK_BIT_END=$(bank_bit_end)
	        endif
    endif

    ifneq ($(bank_channel_bit), 0)
        ccflags-y  += -DgcdBANK_CHANNEL_BIT=$(bank_channel_bit)
    endif
endif


# setup includes
ccflags-y += \
	-I$(obj)/hal/user \
	-I$(obj)/hal/kernel \
	-I$(obj)/hal/kernel/inc \
	-I$(obj)/hal/os/linux/kernel \
	-I$(obj)/arch/XAQ2/hal/kernel

obj-$(CONFIG_GPU_VIVANTE_V4) += galcore.o

galcore-objs += \
	hal/kernel/gc_hal_kernel.o \
	hal/kernel/gc_hal_kernel_command.o \
	hal/kernel/gc_hal_kernel_db.o \
	hal/kernel/gc_hal_kernel_debug.o \
	hal/kernel/gc_hal_kernel_event.o \
	hal/kernel/gc_hal_kernel_heap.o \
	hal/kernel/gc_hal_kernel_mmu.o \
	hal/kernel/gc_hal_kernel_video_memory.o \
	hal/os/linux/kernel/gc_hal_kernel_device.o \
	hal/os/linux/kernel/gc_hal_kernel_driver.o \
	hal/os/linux/kernel/gc_hal_kernel_linux.o \
	hal/os/linux/kernel/gc_hal_kernel_math.o \
	hal/os/linux/kernel/gc_hal_kernel_os.o \
	arch/XAQ2/hal/kernel/gc_hal_kernel_context.o \
	arch/XAQ2/hal/kernel/gc_hal_kernel_hardware.o
