/****************************************************************************
*
*    Copyright (C) 2005 - 2010 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/


#include <linux/device.h>
#include "gc_hal_kernel_linux.h"
#include "gc_hal_driver.h"
#include "gc_hal_user_context.h"

#ifdef ENABLE_GPU_CLOCK_BY_DRIVER
#undef ENABLE_GPU_CLOCK_BY_DRIVER
#endif

#if (defined CONFIG_DOVE_GPU)
#define ENABLE_GPU_CLOCK_BY_DRIVER	0
#else
#define ENABLE_GPU_CLOCK_BY_DRIVER	1
#endif

/* You can comment below line to use legacy driver model */
#define USE_PLATFORM_DRIVER         1

#if USE_PLATFORM_DRIVER
#include <linux/platform_device.h>
#endif

#if MRVL_PLATFORM_MMP2
#include <mach/cputype.h>
#endif

#if MRVL_CONFIG_ENABLE_DVFM
#include <mach/dvfm.h>
#endif

MODULE_DESCRIPTION("Vivante Graphics Driver");
MODULE_LICENSE("GPL");

struct class *gpuClass;

static gckGALDEVICE galDevice;

static int major = 199;
module_param(major, int, 0644);

#ifdef CONFIG_MACH_CUBOX
    int irqLine = 42;
    long registerMemBase = 0xf1840000;
    ulong contiguousBase = 0x8000000;
#else
    int irqLine = 8;
    long registerMemBase = 0xc0400000;
    ulong contiguousBase = 0;
#endif

module_param(irqLine, int, 0644);
module_param(registerMemBase, long, 0644);

ulong registerMemSize = 256 << 10;
module_param(registerMemSize, ulong, 0644);


long contiguousSize = 32 << 20;
module_param(contiguousSize, long, 0644);
module_param(contiguousBase, ulong, 0644);

long bankSize = 32 << 20;
module_param(bankSize, long, 0644);

int fastClear = -1;
module_param(fastClear, int, 0644);

int compression = -1;
module_param(compression, int, 0644);

int signal = 48;
module_param(signal, int, 0644);

ulong baseAddress = 0;
module_param(baseAddress, ulong, 0644);

int showArgs = 1;
module_param(showArgs, int, 0644);

ulong gpu_frequency = 312;
module_param(gpu_frequency, ulong, 0644);

/******************************************************************************\
* Create a data entry system using proc for GC
\******************************************************************************/
#define MRVL_CONFIG_PROC

#ifdef MRVL_CONFIG_PROC
#include <linux/proc_fs.h>

#define GC_PROC_FILE    "driver/gc"
#define _GC_OBJ_ZONE	gcvZONE_DRIVER

static struct proc_dir_entry * gc_proc_file;

/* cat /proc/driver/gc will print gc related msg */
static ssize_t gc_proc_read(struct file *file,
    char __user *buffer, size_t count, loff_t *offset)
{
    gceSTATUS status;
	ssize_t len = 0;
	char buf[1000];
    gctUINT32 idle;
    gctBOOL   isIdle;
    gctUINT32 clockControl;

    len += sprintf(buf+len, "%s(%s)\n", _VENDOR_STRING_, _GC_VERSION_STRING_);
#ifdef _DEBUG
    len += sprintf(buf+len, "DEBUG VERSION\n");
#else
    len += sprintf(buf+len, "RELEASE VERSION\n");
#endif

    gcmkONERROR(gckHARDWARE_GetIdle(galDevice->kernel->hardware, gcvFALSE, &idle));
    gcmkONERROR(gckHARDWARE_QueryIdle(galDevice->kernel->hardware, &isIdle));
    len += sprintf(buf+len, "idle register: 0x%02x, hardware is %s\n", idle, (gcvTRUE == isIdle)?"idle":"busy");

    gcmkONERROR(gckOS_ReadRegister(galDevice->os, 0x00000, &clockControl));
    len += sprintf(buf+len, "clockControl register: 0x%02x\n", clockControl);

	len += sprintf(buf+len, "print mode:\tPid(%d) Reset(%d) DumpCmdBuf(%d)\n",
                    galDevice->printPID, galDevice->silentReset, galDevice->kernel->command->dumpCmdBuf);

	len += sprintf(buf+len, "GC memory usage profile:\n");

	len += sprintf(buf+len, "Total reserved video memory: %ld KB\n", galDevice->reservedMem/1024);

	len += sprintf(buf+len, "Used video mem: %d KB\tcontiguous: %d KB\tvirtual: %d KB\n", galDevice->vidMemUsage/1024,
							galDevice->contiguousMemUsage/1024, galDevice->virtualMemUsage/1024);

	if (galDevice->kernel->mmu)
		len += sprintf(buf+len, "MMU Entries usage(PageCount): Total(%d), Used(%d)\n", galDevice->kernel->mmu->pageTableEntries,galDevice->kernel->mmu->pageTableUsedEntries);

	return simple_read_from_buffer(buffer, count, offset, buf, len);

OnError:
    return 0;
}

#if MRVL_PRINT_CMD_BUFFER
extern gceSTATUS
_PrintAllCmdBuffer(
	gckCOMMAND Command
	);
#endif

/* echo xx > /proc/driver/gc set ... */
static ssize_t gc_proc_write(struct file *file,
		const char *buff, size_t len, loff_t *off)
{
    char messages[256];

	if(len > 256)
		len = 256;

	if(copy_from_user(messages, buff, len))
		return -EFAULT;

    gcmkPRINT("\n");
    if(strncmp(messages, "printPID", 8) == 0)
    {
        galDevice->printPID = galDevice->printPID ? gcvFALSE : gcvTRUE;
        gcmkPRINT("==>Change printPID to %s\n", galDevice->printPID ? "gcvTRUE" : "gcvFALSE");
    }
    else if(strncmp(messages, "powerDebug", 10) == 0)
    {
        galDevice->powerDebug= galDevice->powerDebug ? gcvFALSE : gcvTRUE;
    }
    else if(strncmp(messages, "profile", 7) == 0)
    {
        sscanf(messages+7,"%d %d %d %d",
            &galDevice->profileStep,&galDevice->profileTimeSlice,&galDevice->profileTailTimeSlice,&galDevice->idleThreshold);
        gcmkPRINT("==>Change profling [step timeSlice tailTimeSlice threshold] to be [%d %d %d %d]\n",
            galDevice->profileStep,galDevice->profileTimeSlice,galDevice->profileTailTimeSlice,galDevice->idleThreshold);
    }
    else if(strncmp(messages, "hang", 4) == 0)
    {
		galDevice->kernel->hardware->hang = galDevice->kernel->hardware->hang ? gcvFALSE : gcvTRUE;
    }
    else if(strncmp(messages, "reset2", 6) == 0)
    {
        gckOS_Reset(galDevice->os);
    }
    else if(strncmp(messages, "memFail", 7) == 0)
    {
        gctUINT32 para = 0xFFFFFFFF;
        sscanf(messages+7, "%d", &para);
        galDevice->memRandomFailRate = para;
        gcmkPRINT("==>Change memory random fail rate to %d%\n", galDevice->memRandomFailRate);
    }
    else if(strncmp(messages, "irq", 3) == 0)
    {
        gctUINT32 enable  = ~0U;

        sscanf(messages+3, "%d", &enable);

        switch (enable) {
            case 0:
                /* disable GC interrupt line */
                gckOS_SuspendInterrupt(galDevice->os);
                break;
            case 1:
                /* enable GC interrupt line */
                gckOS_ResumeInterrupt(galDevice->os);
                break;
            default:
                gcmkPRINT("[galcore] Usage: echo irq [0|1] > /proc/driver/gc");
        }
    }
    else if(strncmp(messages, "log", 3) == 0)
    {
        gctUINT32 filter = _GFX_LOG_NONE_;
        gctUINT32 level  = _GFX_LOG_NONE_;
        /*
        @Description
            Only deal with the lowest two bits of input value
            so level 5(0x101) is functional equivalent to level 1(0x001)
        @level  Val  Hex    Description
                0   0x00    print nothing
                1   0x01    print error log only
                2   0x10    print warning log only
                3   0x11    print error and warning info
        @Sample
            echo log 0 > /proc/driver/gc    # Disable error log print
            echo log 3 > /proc/driver/gc    # Enable error & warning log print
        */
        sscanf(messages+3, "%d", &level);

        if ((level & _GFX_LOG_ERROR_) != _GFX_LOG_NONE_)
            filter |= _GFX_LOG_ERROR_;
        if ((level & _GFX_LOG_WARNING_) != _GFX_LOG_NONE_)
            filter |= _GFX_LOG_WARNING_;
        gckOS_SetLogFilter(filter);
        gcmkPRINT("==>Change log level to %d", filter);
    }
    else if(strncmp(messages, "silentReset", 11) == 0)
    {
        gctUINT32 para = 0xFFFFFFFF;

        sscanf(messages+11, "%d", &para);

        switch(para)
        {
        case 0:
            galDevice->silentReset = gcvFALSE;
            gcmkPRINT("==>Change silentReset to %s\n", galDevice->silentReset ? "gcvTRUE" : "gcvFALSE");
            break;
        case 1:
            galDevice->silentReset = gcvTRUE;
            gcmkPRINT("==>Change silentReset to %s\n", galDevice->silentReset ? "gcvTRUE" : "gcvFALSE");
            break;
        default:
            gcmkPRINT("usage:  \n \
            to enable silent reset:     #echo silentReset 1 > /proc/driver/gc \n \
            to disable silent reset:    #echo silentReset 0 > /proc/driver/gc \n");
            break;
        }
    }
    else if(strncmp(messages, "dumpCmdBuf", 10) == 0)
    {
        gctUINT32 para = 0xFFFFFFFF;

        sscanf(messages+10, "%d", &para);

        switch(para)
        {
        case 0:
            galDevice->kernel->command->dumpCmdBuf = gcvFALSE;
            gcmkPRINT("==>Change dumpCmdBuf to %s\n", galDevice->kernel->command->dumpCmdBuf ? "gcvTRUE" : "gcvFALSE");
            break;
        case 1:
            galDevice->kernel->command->dumpCmdBuf = gcvTRUE;
            gcmkPRINT("==>Change dumpCmdBuf to %s\n", galDevice->kernel->command->dumpCmdBuf ? "gcvTRUE" : "gcvFALSE");
            break;
        default:
            gcmkPRINT("usage:  \n \
            to enable dump cmd buffer   #echo dumpCmdBuf 1 > /proc/driver/gc \n \
            to disable dump cmd buffer  #echo dumpCmdBuf 0 > /proc/driver/gc \n");
            break;
        }
    }
#if MRVL_PRINT_CMD_BUFFER
    else if(strncmp(messages, "dumpall", 7) == 0)
    {
        _PrintAllCmdBuffer(galDevice->kernel->command);
    }
#endif
    else if(strncmp(messages, "clkoffonly", 10) == 0)
    {
        galDevice->clkOffOnly= galDevice->clkOffOnly? gcvFALSE : gcvTRUE;
        gcmkPRINT("==>Change clkOffOnly to %s\n", galDevice->clkOffOnly ? "gcvTRUE" : "gcvFALSE");
    }
    else if(strncmp(messages, "offidle", 7) == 0)
    {
        galDevice->powerOffWhenIdle = galDevice->powerOffWhenIdle? gcvFALSE : gcvTRUE;
        gcmkPRINT("==>Change powerOffWhenIdle to %s\n", galDevice->powerOffWhenIdle ? "gcvTRUE" : "gcvFALSE");
    }
    else if(strncmp(messages, "su", 2) == 0)
    {
        /* gckOS_PowerOff(galDevice->os); */
    }
    else if(strncmp(messages, "re", 2) == 0)
    {
        /* gckOS_PowerOn(galDevice->os); */
    }
    else if(strncmp(messages, "stress", 6) == 0)
    {
        int i;
        static int count = 0;

        sscanf(messages+6,"%d", &count);
        /* struct vmalloc_info vmi; */

        /* {get_vmalloc_info(&vmi);gcmkPRINT("%s,%d,VmallocUsed: %8lu kB\n",__func__,__LINE__,vmi.used >> 10); } */

#ifdef _DEBUG
    	gckOS_SetDebugLevel(gcvLEVEL_VERBOSE);
    	gckOS_SetDebugZone(1023);
#endif

        for(i=0;i<count;i++)
        {
            static int count = 0;

            gcmkPRINT("count:%d\n",count++);
            gcmkPRINT("!!!\t");
            /* gckOS_PowerOff(galDevice->os); */
            gcmkPRINT("@@@\t");
            /* gckOS_PowerOn(galDevice->os); */
            gcmkPRINT("###\n");
        }

    }
    else if(strncmp(messages, "debug", 5) == 0)
    {
#ifdef _DEBUG
        static int count = 0;
        gctINT debugLevel = gcvLEVEL_NONE;
        gctINT debugZone = 0;

        sscanf(messages+5,"%d %d", &debugLevel,&debugZone);


        /*
            #define gcvLEVEL_NONE           -1
            #define gcvLEVEL_ERROR          0
            #define gcvLEVEL_WARNING        1
            #define gcvLEVEL_INFO           2
            #define gcvLEVEL_VERBOSE        3
        */

        /*
            #define gcvZONE_OS              (1 << 0)
            #define gcvZONE_HARDWARE        (1 << 1)
            #define gcvZONE_HEAP            (1 << 2)

            #define gcvZONE_KERNEL          (1 << 3)
            #define gcvZONE_VIDMEM          (1 << 4)
            #define gcvZONE_COMMAND         (1 << 5)
            #define gcvZONE_DRIVER          (1 << 6)
            #define gcvZONE_CMODEL          (1 << 7)
            #define gcvZONE_MMU             (1 << 8)
            #define gcvZONE_EVENT           (1 << 9)
            #define gcvZONE_DEVICE          (1 << 10)
        */

        count++;
        gckOS_SetDebugLevel(debugLevel);
        gckOS_SetDebugZone(debugZone);
        gcmkPRINT("==>Change Debuglevel to %s, DebugZone to %d, Count:%d\n",
            (debugLevel == gcvLEVEL_VERBOSE) ? "gcvLEVEL_VERBOSE" : "gcvLEVEL_NONE",
            debugZone,
            count);
#endif
    }
    else if(strncmp(messages, "16", 2) == 0)
    {
		gcmkPRINT("frequency change to 1/16\n");
        /* frequency change to 1/16 */
        gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x210));
        /* Loading the frequency scaler. */
    	gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x010));

    }
    else if(strncmp(messages, "32", 2) == 0)
    {
		gcmkPRINT("frequency change to 1/32\n");
        /* frequency change to 1/32*/
        gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x208));
        /* Loading the frequency scaler. */
    	gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x008));

    }
	else if(strncmp(messages, "64", 2) == 0)
    {
		gcmkPRINT("frequency change to 1/64\n");
        /* frequency change to 1/64 */
        gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x204));
        /* Loading the frequency scaler. */
    	gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x004));

    }
    else if('1' == messages[0])
    {
        gcmkPRINT("frequency change to full speed\n");
        /* frequency change to full speed */
        gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x300));
        /* Loading the frequency scaler. */
    	gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x100));

    }
    else if('2' == messages[0])
    {
        gcmkPRINT("frequency change to 1/2\n");
        /* frequency change to 1/2 */
        gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x280));
        /* Loading the frequency scaler. */
    	gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x080));

    }
    else if('4' == messages[0])
    {
        gcmkPRINT("frequency change to 1/4\n");
        /* frequency change to 1/4 */
        gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x240));
        /* Loading the frequency scaler. */
    	gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x040));

    }
    else if('8' == messages[0])
    {
        gcmkPRINT("frequency change to 1/8\n");
        /* frequency change to 1/8 */
        gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x220));
        /* Loading the frequency scaler. */
    	gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x020));

    }
    else
    {
        gcmkPRINT("unknown echo\n");
    }

    return len;
}

static struct file_operations gc_proc_ops = {
	.read = gc_proc_read,
	.write = gc_proc_write,
};

static void create_gc_proc_file(void)
{
	gc_proc_file = create_proc_entry(GC_PROC_FILE, 0644, gcvNULL);
	if (gc_proc_file) {
		gc_proc_file->proc_fops = &gc_proc_ops;
	} else
		gcmkPRINT("[galcore] proc file create failed!\n");
}

static void remove_gc_proc_file(void)
{
	remove_proc_entry(GC_PROC_FILE, gcvNULL);
}

#endif

/******************************************************************************\
* Driver operations definition
\******************************************************************************/
static int drv_open(struct inode *inode, struct file *filp);
static int drv_release(struct inode *inode, struct file *filp);
static long drv_ioctl(struct file *filp,
                     unsigned int ioctlCode, unsigned long arg);
static int drv_mmap(struct file * filp, struct vm_area_struct * vma);

struct file_operations driver_fops =
{
    .open   	= drv_open,
    .release	= drv_release,
    .unlocked_ioctl  	= drv_ioctl,
    .mmap   	= drv_mmap,
};

int drv_open(struct inode *inode, struct file* filp)
{
    gcsHAL_PRIVATE_DATA_PTR	private;

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
    	    	  "Entering drv_open\n");

    private = kmalloc(sizeof(gcsHAL_PRIVATE_DATA), GFP_KERNEL);

    if (private == gcvNULL)
    {
    	return -ENOTTY;
    }

    private->device				= galDevice;
    private->mappedMemory		= gcvNULL;
	private->contiguousLogical	= gcvNULL;

#if gcdkUSE_MEMORY_RECORD
	private->memoryRecordList.prev = &private->memoryRecordList;
	private->memoryRecordList.next = &private->memoryRecordList;
#endif

	/* A process gets attached. */
	gcmkVERIFY_OK(
		gckKERNEL_AttachProcess(galDevice->kernel, gcvTRUE));

    if (!galDevice->contiguousMapped)
    {
    	gcmkVERIFY_OK(gckOS_MapMemory(galDevice->os,
									galDevice->contiguousPhysical,
									galDevice->contiguousSize,
									&private->contiguousLogical));
    }

    filp->private_data = private;

    return 0;
}

int drv_release(struct inode* inode, struct file* filp)
{
    gcsHAL_PRIVATE_DATA_PTR	private;
    gckGALDEVICE			device;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
    	    	  "Entering drv_close\n");

    private = filp->private_data;
    gcmkASSERT(private != gcvNULL);

    device = private->device;

#if gcdkUSE_MEMORY_RECORD
    gcmkVERIFY_OK(gckCOMMAND_Stall(device->kernel->command));

	FreeAllMemoryRecord(galDevice->os, &private->memoryRecordList);

    gcmkVERIFY_OK(gckCOMMAND_Stall(device->kernel->command));
#endif

	if (private->contiguousLogical != gcvNULL)
	{
		gcmkVERIFY_OK(gckOS_UnmapMemory(galDevice->os,
										galDevice->contiguousPhysical,
										galDevice->contiguousSize,
										private->contiguousLogical));
	}

    /* Free some uncleared resource when unnormal exit */
    gckOS_FreeProcessResource(galDevice->os, current->tgid);

	/* Print GC memory usage after every process exits. */
	gcmkPRINT("PID=%d , name=%s exits\n", current->tgid, current->comm);
	gcmkPRINT("GC memory usage profile:\n");
	gcmkPRINT("Total reserved video memory: %ld KB\n", galDevice->reservedMem/1024);
	gcmkPRINT("Used video mem: %d KB\tcontiguous: %d KB\tvirtual: %d KB\n", galDevice->vidMemUsage/1024,
							galDevice->contiguousMemUsage/1024, galDevice->virtualMemUsage/1024);

	/* A process gets detached. */
	gcmkVERIFY_OK(
		gckKERNEL_AttachProcess(galDevice->kernel, gcvFALSE));

    kfree(private);
    filp->private_data = gcvNULL;

    return 0;
}

long drv_ioctl(struct file *filp,
	      unsigned int ioctlCode,
	      unsigned long arg)
{
    gcsHAL_INTERFACE iface;
    gctUINT32 copyLen;
    DRIVER_ARGS drvArgs;
    gckGALDEVICE device;
    gceSTATUS status;
    gcsHAL_PRIVATE_DATA_PTR private;

    private = filp->private_data;

    if (private == gcvNULL)
    {
        gcmkLOG_WARNING_ARGS("private data is null");
    	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    	    	      "[galcore] drv_ioctl: private_data is gcvNULL\n");

    	return -ENOTTY;
    }

    device = private->device;

    if (device == gcvNULL)
    {
    	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    	    	      "[galcore] drv_ioctl: device is gcvNULL\n");

    	return -ENOTTY;
    }

    if (ioctlCode != IOCTL_GCHAL_INTERFACE
		&& ioctlCode != IOCTL_GCHAL_KERNEL_INTERFACE)
    {
        /* Unknown command. Fail the I/O. */
        return -ENOTTY;
    }

    /* Get the drvArgs to begin with. */
    copyLen = copy_from_user(&drvArgs,
    	    	    	     (void *) arg,
			     sizeof(DRIVER_ARGS));

    if (copyLen != 0)
    {
    	/* The input buffer is not big enough. So fail the I/O. */
        return -ENOTTY;
    }

    /* Now bring in the gcsHAL_INTERFACE structure. */
    if ((drvArgs.InputBufferSize  != sizeof(gcsHAL_INTERFACE))
    ||  (drvArgs.OutputBufferSize != sizeof(gcsHAL_INTERFACE))
    )
    {
        gcmkPRINT("\n [galcore] data structure size in kernel and user do not match !\n");
    	return -ENOTTY;
    }

    copyLen = copy_from_user(&iface,
    	    	    	     drvArgs.InputBuffer,
			     sizeof(gcsHAL_INTERFACE));

    if (copyLen != 0)
    {
        /* The input buffer is not big enough. So fail the I/O. */
        return -ENOTTY;
    }
    if(galDevice->printPID)
    {
        gcmkPRINT("--->pid=%d\tname=%s\tiface.command=%d.\n", current->pid, current->comm, iface.command);
    }
#if gcdkUSE_MEMORY_RECORD
	if (iface.command == gcvHAL_EVENT_COMMIT)
	{
		MEMORY_RECORD_PTR mr;
		gcsQUEUE_PTR queue = iface.u.Event.queue;

		while (queue != gcvNULL)
		{
			gcsQUEUE_PTR record, next;

			/* Map record into kernel memory. */
			gcmkERR_BREAK(gckOS_MapUserPointer(device->os,
											  queue,
											  gcmSIZEOF(gcsQUEUE),
											  (gctPOINTER *) &record));

			switch (record->iface.command)
			{
			case gcvHAL_FREE_VIDEO_MEMORY:
				mr = FindMemoryRecord(device->os,
									&private->memoryRecordList,
									record->iface.u.FreeVideoMemory.node);

				if (mr != gcvNULL)
				{
					DestoryMemoryRecord(device->os, mr);
				}
				else
				{
					gcmkPRINT("*ERROR* Invalid video memory (0x%p) for free\n",
						record->iface.u.FreeVideoMemory.node);
				}
                break;

			default:
				break;
			}

			/* Next record in the queue. */
			next = record->next;

			/* Unmap record from kernel memory. */
			gcmkERR_BREAK(gckOS_UnmapUserPointer(device->os,
												queue,
												gcmSIZEOF(gcsQUEUE),
												(gctPOINTER *) record));
			queue = next;
		}
	}
#endif

    status = gckKERNEL_Dispatch(device->kernel,
		(ioctlCode == IOCTL_GCHAL_INTERFACE) , &iface);

    if (gcmIS_ERROR(status))
    {
    	gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DRIVER,
	    	      "[galcore] gckKERNEL_Dispatch returned %d.\n",
		      status);
    }

    else if (gcmIS_ERROR(iface.status))
    {
    	gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DRIVER,
	    	      "[galcore] IOCTL %d returned %d.\n",
		      iface.command,
		      iface.status);
    }

    /* See if this was a LOCK_VIDEO_MEMORY command. */
    else if (iface.command == gcvHAL_LOCK_VIDEO_MEMORY)
    {
    	/* Special case for mapped memory. */
    	if (private->mappedMemory != gcvNULL
			&& iface.u.LockVideoMemory.node->VidMem.memory->object.type
				== gcvOBJ_VIDMEM)
		{
	   		/* Compute offset into mapped memory. */
	    	gctUINT32 offset = (gctUINT8 *) iface.u.LockVideoMemory.memory
	    	    	     	- (gctUINT8 *) device->contiguousBase;

    	    /* Compute offset into user-mapped region. */
    	    iface.u.LockVideoMemory.memory =
	    	(gctUINT8 *)  private->mappedMemory + offset;
		}
    }
#if gcdkUSE_MEMORY_RECORD
	else if (iface.command == gcvHAL_ALLOCATE_VIDEO_MEMORY)
	{
		CreateMemoryRecord(device->os,
							&private->memoryRecordList,
							iface.u.AllocateVideoMemory.node);
	}
	else if (iface.command == gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY)
	{
		CreateMemoryRecord(device->os,
							&private->memoryRecordList,
							iface.u.AllocateLinearVideoMemory.node);
	}
	else if (iface.command == gcvHAL_FREE_VIDEO_MEMORY)
	{
		MEMORY_RECORD_PTR mr;

		mr = FindMemoryRecord(device->os,
							&private->memoryRecordList,
							iface.u.FreeVideoMemory.node);

		if (mr != gcvNULL)
		{
			DestoryMemoryRecord(device->os, mr);
		}
		else
		{
			gcmkPRINT("*ERROR* Invalid video memory for free\n");
		}
	}
#endif

    /* Copy data back to the user. */
    copyLen = copy_to_user(drvArgs.OutputBuffer,
    	    	    	   &iface,
			   sizeof(gcsHAL_INTERFACE));

    if (copyLen != 0)
    {
    	/* The output buffer is not big enough. So fail the I/O. */
        return -ENOTTY;
    }
    return 0;
}

static int drv_mmap(struct file * filp, struct vm_area_struct * vma)
{
    gcsHAL_PRIVATE_DATA_PTR private = filp->private_data;
    gckGALDEVICE device;
    int ret;
    unsigned long size = vma->vm_end - vma->vm_start;

    if (private == gcvNULL)
    {
    	return -ENOTTY;
    }

    device = private->device;

    if (device == gcvNULL)
    {
        return -ENOTTY;
    }

#ifdef CONFIG_MACH_CUBOX
    vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
#else
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#endif
    vma->vm_flags    |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND;
    vma->vm_pgoff     = 0;

    if (device->contiguousMapped)
    {
    	ret = io_remap_pfn_range(vma,
	    	    	    	 vma->vm_start,
    	    	    	    	 (gctUINT32) device->contiguousPhysical >> PAGE_SHIFT,
				 size,
				 vma->vm_page_prot);

    	private->mappedMemory = (ret == 0) ? (gctPOINTER) vma->vm_start : gcvNULL;

    	return ret;
    }
    else
    {
    	return -ENOTTY;
    }
}

/******************************************************************************\
* Driver initialization - cleanup and power management functions
\******************************************************************************/

#if !USE_PLATFORM_DRIVER
static int __init drv_init(void)
#else
static int drv_init(void)
#endif
{
    int ret;
    gckGALDEVICE device;

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
    	    	  "Entering drv_init\n");
	printk("\n[galcore] GC Version: %s\n", _GC_VERSION_STRING_);

#if ENABLE_GPU_CLOCK_BY_DRIVER && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
    gckOS_ClockOn(gcvNULL, gcvTRUE, gcvTRUE, gpu_frequency);
#endif

	if (showArgs)
	{
		printk("galcore options:\n");
		printk("  irqLine         = %d\n",      irqLine);
		printk("  registerMemBase = 0x%08lX\n", registerMemBase);
		printk("  contiguousSize  = %ld\n",     contiguousSize);
		printk("  contiguousBase  = 0x%08lX\n", contiguousBase);
		printk("  bankSize        = 0x%08lX\n", bankSize);
		printk("  fastClear       = %d\n",      fastClear);
		printk("  compression     = %d\n",      compression);
		printk("  signal          = %d\n",      signal);
		printk("  baseAddress     = 0x%08lX\n", baseAddress);
	}

    /* Create the GAL device. */
    gcmkVERIFY_OK(gckGALDEVICE_Construct(irqLine,
    	    	    	    	    	registerMemBase,
					registerMemSize,
					contiguousBase,
					contiguousSize,
					bankSize,
					fastClear,
					compression,
					baseAddress,
					signal,
					&device));
    gcmkPRINT("\n[galcore] chipModel=0x%x,chipRevision=0x%x,chipFeatures=0x%x,chipMinorFeatures=0x%x\n",
        device->kernel->hardware->chipModel, device->kernel->hardware->chipRevision,
        device->kernel->hardware->chipFeatures, device->kernel->hardware->chipMinorFeatures0);

#if MRVL_CONFIG_ENABLE_DVFM
    /* register galcore as a dvfm device*/
    if(dvfm_register("Galcore", &device->dvfm_dev_index))
    {
        gcmkPRINT("\n[galcore] fail to do dvfm_register\n");
    }
#endif
	gckOS_SetConstraint(device->os, gcvTRUE, gcvTRUE);

    /* Start the GAL device. */
    if (gcmIS_ERROR(gckGALDEVICE_Start(device)))
    {
    	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    	    	      "[galcore] Can't start the gal device.\n");

    	/* Roll back. */
    	gckGALDEVICE_Stop(device);
    	gckGALDEVICE_Destroy(device);

    	return -1;
    }

    /* Register the character device. */
    ret = register_chrdev(major, DRV_NAME, &driver_fops);
    if (ret < 0)
    {
    	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    	    	      "[galcore] Could not allocate major number for mmap.\n");

    	/* Roll back. */
    	gckGALDEVICE_Stop(device);
    	gckGALDEVICE_Destroy(device);

    	return -1;
    }
    else
    {
    	if (major == 0)
    	{
    	    major = ret;
    	}
    }

    galDevice = device;

	gpuClass = class_create(THIS_MODULE, "v_graphics_class");
	if (IS_ERR(gpuClass)) {
    	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
					  "Failed to create the class.\n");
		return -1;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	device_create(gpuClass, gcvNULL, MKDEV(major, 0), gcvNULL, "galcore");
#else
	device_create(gpuClass, gcvNULL, MKDEV(major, 0), "galcore");
#endif

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
    	    	  "[galcore] irqLine->%ld, contiguousSize->%lu, memBase->0x%lX\n",
		  irqLine,
		  contiguousSize,
		  registerMemBase);

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
    	    	  "[galcore] driver registered successfully.\n");

    BSP_IDLE_PROFILE_INIT;
    return 0;
}

#if !USE_PLATFORM_DRIVER
static void __exit drv_exit(void)
#else
static void drv_exit(void)
#endif
{
    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
    	    	  "[galcore] Entering drv_exit\n");

	device_destroy(gpuClass, MKDEV(major, 0));
	class_destroy(gpuClass);

    unregister_chrdev(major, DRV_NAME);

    gckGALDEVICE_Stop(galDevice);

    gckOS_UnSetConstraint(galDevice->os, gcvTRUE, gcvTRUE);

#if MRVL_CONFIG_ENABLE_DVFM
    if(dvfm_unregister("Galcore", &galDevice->dvfm_dev_index))
    {
        gcmkPRINT("\n[galcore] fail to do dvfm_unregister\n");
    }
#endif

    gckGALDEVICE_Destroy(galDevice);

#if ENABLE_GPU_CLOCK_BY_DRIVER && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
    gckOS_ClockOff(gcvNULL, gcvTRUE, gcvTRUE);
#endif
}

#if !USE_PLATFORM_DRIVER
module_init(drv_init);
module_exit(drv_exit);
#else /* USE_PLATFORM_DRIVER -- start */

#define DEVICE_NAME "galcore"

#if MRVL_CONFIG_ENABLE_EARLYSUSPEND
static void gpu_early_suspend(struct early_suspend *h)
{
    gcmkPRINT("\n");
    gcmkPRINT("[galcore]: %s, %d\n",__func__, __LINE__);

    if(galDevice->printPID)
    {
    }
    else
    {
#if MRVL_PLATFORM_MMP2
        if (!cpu_is_mmp2_z0() && !cpu_is_mmp2_z1())
#endif
            gckHARDWARE_SetPowerManagementState(galDevice->kernel->hardware, gcvPOWER_OFF);
        BSP_IDLE_PROFILE_CALC_IDLE_TIME;
    }

    galDevice->currentPMode = gcvPM_EARLY_SUSPEND;

    gcmkPRINT("[galcore]: %s, %d\n\n",__func__, __LINE__);
    gcmkPRINT("\n");

    return;
}

static void gpu_late_resume(struct early_suspend *h)
{
    gcmkPRINT("\n");
    gcmkPRINT("[galcore]: %s, %d\n",__func__, __LINE__);

    galDevice->currentPMode = gcvPM_NORMAL;

    if(galDevice->printPID)
    {
    }
    else
    {
        /* NO need to add the time during early-suspend to idle-time */
        BSP_IDLE_PROFILE_INIT;
    }

    gcmkPRINT("[galcore]: %s, %d\n\n",__func__, __LINE__);
    gcmkPRINT("\n");

    return;
}

static struct early_suspend gpu_early_suspend_desc = {
    .level = EARLY_SUSPEND_LEVEL_STOP_DRAWING + 200,  /*  make sure GC early_suspend after surfaceflinger stop drawing */
	.suspend = gpu_early_suspend,
	.resume = gpu_late_resume,
};
#endif /* MRVL_CONFIG_ENABLE_EARLYSUSPEND -- end */

static int __devinit gpu_probe(struct platform_device *pdev)
{
	int ret = -ENODEV;
	struct resource *res;
	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ,"gpu_irq");
	if (!res) {
		gcmkPRINT(KERN_ERR "%s: No irq line supplied.\n",__FUNCTION__);
		goto gpu_probe_fail;
	}
	irqLine = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,"gpu_base");
	if (!res) {
		gcmkPRINT(KERN_ERR "%s: No register base supplied.\n",__FUNCTION__);
		goto gpu_probe_fail;
	}
	registerMemBase = res->start;
	registerMemSize = res->end - res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,"gpu_mem");
	if (!res) {
		gcmkPRINT(KERN_ERR "%s: No memory base supplied.\n",__FUNCTION__);
		goto gpu_probe_fail;
	}
	contiguousBase  = res->start;
	contiguousSize  = res->end-res->start;

	ret = drv_init();
	if(!ret) {
		platform_set_drvdata(pdev,galDevice);
#ifdef MRVL_CONFIG_PROC
    create_gc_proc_file();
#endif

#if MRVL_CONFIG_ENABLE_EARLYSUSPEND
    register_early_suspend(&gpu_early_suspend_desc);
#endif
		return ret;
	}

gpu_probe_fail:
	gcmkPRINT(KERN_INFO "Failed to register gpu driver.\n");
	return ret;
}

static int __devinit gpu_remove(struct platform_device *pdev)
{
	drv_exit();

#ifdef MRVL_CONFIG_PROC
    remove_gc_proc_file();
#endif

#if MRVL_CONFIG_ENABLE_EARLYSUSPEND
    unregister_early_suspend(&gpu_early_suspend_desc);
#endif
	return 0;
}

static int __devinit gpu_suspend(struct platform_device *dev, pm_message_t state)
{
    gceSTATUS status;
    gctUINT32 countRetry = 0;

    gcmkPRINT("\n");
    gcmkPRINT("[galcore]: %s, %d\n",__func__, __LINE__);

    while((status = gckHARDWARE_SetPowerManagementState(galDevice->kernel->hardware, gcvPOWER_OFF)) != gcvSTATUS_OK)
    {
        countRetry++;
        if(countRetry > 3)
        {
            countRetry = 0;
            gcmkPRINT("%s, GC is not correctly powered off, abort..\n",__func__);
            break;
        }
    }

    galDevice->currentPMode = gcvPM_SUSPEND;

    gcmkPRINT("[galcore]: %s, %d\n",__func__, __LINE__);
    gcmkPRINT("\n");

    return 0;
}

static int __devinit gpu_resume(struct platform_device *dev)
{
    gcmkPRINT("\n");
    gcmkPRINT("[galcore]: %s, %d",__func__, __LINE__);


#if MRVL_CONFIG_ENABLE_EARLYSUSPEND
    galDevice->currentPMode = gcvPM_EARLY_SUSPEND;
#else
    galDevice->currentPMode = gcvPM_NORMAL;
#endif

    gcmkPRINT("[galcore]: %s, %d\n",__func__, __LINE__);
    gcmkPRINT("\n");

    return 0;
}

static struct platform_driver gpu_driver = {
	.probe		= gpu_probe,
	.remove		= gpu_remove,

	.suspend	= gpu_suspend,
	.resume		= gpu_resume,

	.driver		= {
		.name	= DEVICE_NAME,
	}
};

#ifndef CONFIG_DOVE_GPU
static struct resource gpu_resources[] = {
    {
        .name   = "gpu_irq",
        .flags  = IORESOURCE_IRQ,
    },
    {
        .name   = "gpu_base",
        .flags  = IORESOURCE_MEM,
    },
    {
        .name   = "gpu_mem",
        .flags  = IORESOURCE_MEM,
    },
};

static struct platform_device * gpu_device;
#endif

static int __init gpu_init(void)
{
	int ret = 0;

#ifndef CONFIG_DOVE_GPU
	gpu_resources[0].start = gpu_resources[0].end = irqLine;

	gpu_resources[1].start = registerMemBase;
	gpu_resources[1].end   = registerMemBase + registerMemSize;

	gpu_resources[2].start = contiguousBase;
	gpu_resources[2].end   = contiguousBase + contiguousSize;

	/* Allocate device */
	gpu_device = platform_device_alloc(DEVICE_NAME, -1);
	if (!gpu_device)
	{
		gcmkPRINT(KERN_ERR "galcore: platform_device_alloc failed.\n");
		ret = -ENOMEM;
		goto out;
	}

	/* Insert resource */
	ret = platform_device_add_resources(gpu_device, gpu_resources, 3);
	if (ret)
	{
		gcmkPRINT(KERN_ERR "galcore: platform_device_add_resources failed.\n");
		goto put_dev;
	}

	/* Add device */
	ret = platform_device_add(gpu_device);
	if (ret)
	{
		gcmkPRINT(KERN_ERR "galcore: platform_device_add failed.\n");
		goto del_dev;
	}
#endif

	ret = platform_driver_register(&gpu_driver);
	if (!ret)
	{
		goto out;
	}

#ifndef CONFIG_DOVE_GPU
del_dev:
	platform_device_del(gpu_device);
put_dev:
	platform_device_put(gpu_device);
#endif

out:
	return ret;

}

static void __exit gpu_exit(void)
{
	platform_driver_unregister(&gpu_driver);
#ifndef CONFIG_DOVE_GPU
	platform_device_unregister(gpu_device);
#endif
}

module_init(gpu_init);
module_exit(gpu_exit);

#endif /* USE_PLATFORM_DRIVER -- end */
