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




#include "gc_hal_kernel_linux.h"
#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>
#define _GC_OBJ_ZONE	gcvZONE_DEVICE

#ifdef FLAREON
static struct dove_gpio_irq_handler gc500_handle;
#endif

/******************************************************************************\
******************************** gckGALDEVICE Code *******************************
\******************************************************************************/

gceSTATUS
gckGALDEVICE_AllocateMemory(
	IN gckGALDEVICE Device,
	IN gctSIZE_T Bytes,
    OUT gctPOINTER *Logical,
    OUT gctPHYS_ADDR *Physical,
    OUT gctUINT32 *PhysAddr
	)
{
	gceSTATUS status;

	gcmkVERIFY_ARGUMENT(Device != gcvNULL);
	gcmkVERIFY_ARGUMENT(Logical != gcvNULL);
	gcmkVERIFY_ARGUMENT(Physical != gcvNULL);
	gcmkVERIFY_ARGUMENT(PhysAddr != gcvNULL);

	status = gckOS_AllocateContiguous(Device->os,
					  gcvFALSE,
					  &Bytes,
					  Physical,
					  Logical);

	if (gcmIS_ERROR(status))
	{
        gcmkLOG_ERROR_ARGS("status=%d, allocate memory error", status);
		gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    				   "gckGALDEVICE_AllocateMemory: error status->0x%x",
    				   status);

		return status;
	}

	*PhysAddr = ((PLINUX_MDL)*Physical)->dmaHandle - Device->baseAddress;
	gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
    			   "gckGALDEVICE_AllocateMemory: phys_addr->0x%x phsical->0x%x Logical->0x%x",
               	   (gctUINT32)*Physical,
				   (gctUINT32)*PhysAddr,
				   (gctUINT32)*Logical);

    /* Success. */
    return gcvSTATUS_OK;
}

gceSTATUS
gckGALDEVICE_FreeMemory(
	IN gckGALDEVICE Device,
	IN gctPOINTER Logical,
	IN gctPHYS_ADDR Physical)
{
	gcmkVERIFY_ARGUMENT(Device != gcvNULL);

    return gckOS_FreeContiguous(Device->os,
					Physical,
					Logical,
					((PLINUX_MDL)Physical)->numPages*PAGE_SIZE);
}

irqreturn_t isrRoutine(int irq, void *ctxt)
{
    gckGALDEVICE device = (gckGALDEVICE) ctxt;
    int handled = 0;

    /* Call kernel interrupt notification. */
    if (gckKERNEL_Notify(device->kernel,
						gcvNOTIFY_INTERRUPT,
						gcvTRUE) == gcvSTATUS_OK)
    {
		device->dataReady = gcvTRUE;

		up(&device->sema);

		handled = 1;
    }

    return IRQ_RETVAL(handled);
}

int threadRoutine(void *ctxt)
{
    gckGALDEVICE device = (gckGALDEVICE) ctxt;

	gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
				   "Starting isr Thread with extension->0x%p\n",
				   device);

	while (1)
	{

		if(down_interruptible(&device->sema) == 0)
		{
		device->dataReady = gcvFALSE;

		if (device->killThread == gcvTRUE)
		{
			/* The daemon exits. */
			while (!kthread_should_stop())
			{
				gckOS_Delay(device->os, 1);
			}

			return 0;
		}
		else
		{
			gckKERNEL_Notify(device->kernel, gcvNOTIFY_INTERRUPT, gcvFALSE);
		}
    }
    }

    return 0;
}


#if MRVL_TIMER

int timer_thread(void *ctxt)
{
	gceSTATUS status;
    gckGALDEVICE device = (gckGALDEVICE) ctxt;
    gctUINT32 idle;

	while (1)
	{
		if(down_interruptible(&device->timersema) == 0)
		{	
			gcmkONERROR(gckOS_AcquireRecMutex(device->os,
							   device->kernel->hardware->recMutexPower,
							   gcvINFINITE));
            
            gcmkONERROR(gckHARDWARE_GetIdle(device->kernel->hardware,
        									gcvFALSE,
        									&idle));

            gcmkPRINT("idle = %x\n", idle);
            gcmkONERROR(gckOS_ReleaseRecMutex(device->os,
				   device->kernel->hardware->recMutexPower));
        }
    }

    return 0;
    
OnError:
    gcmkPRINT("ERROR: %s has error \n",__func__);
    return status;
}

void timer_fn(unsigned long arg)
{
    gckGALDEVICE device = (gckGALDEVICE) arg;

    up(&device->timersema);

    /* triggle per second */
    mod_timer(&device->timer, jiffies + HZ);
}
#endif

#if MRVL_PROFILE_THREAD
int profile_thread(void *ctxt)
{
	gceSTATUS status;
    gckGALDEVICE device = (gckGALDEVICE) ctxt;
    gctUINT32 delayTime = 300; /* should be the max time of once draw */
    
	while (1)
	{
        delayTime = device->profileStep; 

	    if((device->os != gcvNULL)
         && (device->kernel != gcvNULL)
         && (device->kernel->command != gcvNULL)
         && (device->kernel->atomClients > 0))
	    {
            gckCOMMAND command = device->kernel->command;
            
            /* hold profile thread if GC is off */	
			gcmkONERROR(gckOS_AcquireSemaphore(device->os, command->powerSemaphore));
            gcmkVERIFY_OK(gckOS_ReleaseSemaphore(device->os, command->powerSemaphore));

            
            /* Grab the mutex. */
		    gcmkONERROR(gckOS_AcquireRecMutex(device->os, device->kernel->hardware->recMutexPower, gcvINFINITE));

            /* 
                FIXME:
                current idle event has bug, which may not set GC to be idle 
                after early suspend. NotifyIdle here can temporarily solve this issue.
            */
            if(device->clkEnabled && (device->currentPMode != gcvPM_NORMAL))
            {
                gctBOOL isIdle;
                gcmkONERROR(gckHARDWARE_QueryIdle(device->kernel->hardware, &isIdle));

                if(isIdle)
                {
                    if(device->kernel->command->idle == gcvFALSE)
                    {
        				device->kernel->command->idle = gcvTRUE;
        				gcmkONERROR(gckOS_NotifyIdle(device->os, gcvTRUE));
                    }
                }
            }
            
            /* Release the mutex. */
    		gcmkVERIFY_OK(gckOS_ReleaseRecMutex(device->os, device->kernel->hardware->recMutexPower));
            
            /* power off GC when idle */
    	    if(device->powerOffWhenIdle)
            {
                gckOS_PowerOffWhenIdle(device->os, gcvTRUE);
            }

            /* */
            gcmkONERROR(gckOS_Delay(device->os, delayTime));
	    }
        else
        {
            gcmkONERROR(gckOS_Delay(device->os, 10000));
            /* schedule_timeout_interruptible(500); */
        }
    }

    return 0;
    
OnError:
    gcmkPRINT("ERROR: %s has error \n",__func__);
    return status;
}
#endif

#if MRVL_GUARD_THREAD

#if MRVL_PRINT_CMD_BUFFER
extern void _PrintCmdBuffer(
	gckCOMMAND Command,
	gctUINT Address
	);
#endif

int guard_thread(void *ctxt)
{
	gceSTATUS status;
    gckGALDEVICE device = (gckGALDEVICE) ctxt;
    gctUINT32 captureAddr = GC_INVALID_PHYS_ADDR;
    gctUINT32 currentAddr = GC_INVALID_PHYS_ADDR;
    gctUINT32 delayTime = 300; /* should be the max time of once draw */
    gctINT32 atomRefMark, ref;
    gctBOOL isIdle;

    while (1)
	{
        delayTime = device->profileStep; 

	    if((device->os != gcvNULL)
         && (device->kernel != gcvNULL)
         && (device->kernel->command != gcvNULL)
         && (device->kernel->atomClients > 0))
	    {
	        gckCOMMAND command = device->kernel->command;
            gctBOOL needSilentReset = gcvFALSE;
            static gctUINT count = 0;
            gctUINT32 lastWaitLink = GC_INVALID_PHYS_ADDR;
            
            /* hold guard thread if GC is off */	
			gcmkONERROR(gckOS_AcquireSemaphore(device->os, command->powerSemaphore));
            gcmkVERIFY_OK(gckOS_ReleaseSemaphore(device->os, command->powerSemaphore));

            
            /* Grab the mutex. */
		    gcmkONERROR(gckOS_AcquireRecMutex(device->os, device->kernel->hardware->recMutexPower, gcvINFINITE));

            if(device->clkEnabled)
            {
                lastWaitLink = device->kernel->hardware->lastWaitLink;

                /* Read the current FE address. */
                gcmkONERROR(gckOS_ReadRegister(device->os,
                                           0x00664,
                                           &currentAddr));

                /* Test if address is outside the last WAIT/LINK sequence. */
                if( (currentAddr == captureAddr)
                    && ((currentAddr < lastWaitLink) || (currentAddr >= lastWaitLink + 16)))
                {
                    count++;

                    if(device->powerDebug)
                    {
                        gcmkPRINT("GPU stop at 0x%x for %d ms, lastWaitLink = 0x%08x\n",currentAddr,delayTime*count, lastWaitLink);
                    }

                    if (count == 1)
                    {
                        /* Acquire current event count at first time */
                        gckOS_AtomGet(device->os, device->kernel->event->atomEventRef, &atomRefMark);
                    }
                    else
                    {
                        /* Acquire event count again at second or third time */
                        gckOS_AtomGet(device->os, device->kernel->event->atomEventRef, &ref);

                        /* Event counts changed, that means some event successfully returned between
                          these two checking points which further indicates that GC hardware is still
                          working, so we clear "count" here and the capture address remains unchanged.*/
                        if (atomRefMark != ref)
                        {
                            count = 0;
                        }
                    }
                }
                else
                {
                    captureAddr = currentAddr;
                    count = 0;
                }

                if(count >= 3)
                {
                    if(device->powerDebug)
                    {
                        gcmkPRINT("GPU may hang, [captureAddr,currentAddr,lastWaitLink] = [0x%x,0x%x,0x%x]\n",
                            captureAddr,currentAddr,device->kernel->hardware->lastWaitLink);
                    }

                    needSilentReset = gcvTRUE;
                    count = 0;
                }       

                /* GPU stop at the same address */
                if(needSilentReset)
                {
#if MRVL_PRINT_CMD_BUFFER
                    if(device->kernel->command->dumpCmdBuf)
                    {
            			gctUINT i;
                        gctUINT32 idle;
            			gctUINT32 intAck;
            			gctUINT32 prevAddr = 0;
            			gctUINT32 currAddr;
            			gctBOOL changeDetected;

            			changeDetected = gcvFALSE;

                        /* IDLE */
        			    gcmkONERROR(gckOS_ReadRegister(device->os, 0x0004, &idle));
                        
        				/* INT ACK */
        				gcmkONERROR(gckOS_ReadRegister(device->os, 0x0010, &intAck));

        				/* DMA POS */
        				for (i = 0; i < 300; i += 1)
        				{
        					gcmkONERROR(gckOS_ReadRegister(device->os, 0x0664, &currAddr));

        					if ((i > 0) && (prevAddr != currAddr))
        					{
        						changeDetected = gcvTRUE;
        					}

        					prevAddr = currAddr;
        				}

        				gcmkPRINT("\n%s(%d):\n"
                					"  idle = 0x%08X\n"
                					"  int  = 0x%08X\n"
                					"  dma  = 0x%08X (change=%d)\n",
                					__FUNCTION__, __LINE__,
                					idle,
                					intAck,
                					currAddr,
                					changeDetected
                					);
                        
        				_PrintCmdBuffer(device->kernel->command, currAddr);

                    }
#endif

                    gcmkONERROR(gckHARDWARE_QueryIdle(device->kernel->hardware, &isIdle));
                    /* gcmkPRINT("[galcore], timeout, isIdle=%d\n",isIdle); */

                    if(isIdle == gcvFALSE)
                    {
                        /* silent reset */
                        if(device->silentReset)
                        {
                            gcmkONERROR(gckOS_Reset(device->os));
                        }
                    }
                }
            }
            
            /* Release the mutex. */
    		gcmkVERIFY_OK(gckOS_ReleaseRecMutex(device->os, device->kernel->hardware->recMutexPower));
            
            /* */
            gcmkONERROR(gckOS_Delay(device->os, delayTime));
	    }
        else
        {
            gcmkONERROR(gckOS_Delay(device->os, 10000));
            /* schedule_timeout_interruptible(500); */
        }
    }

    return 0;
    
OnError:
    gcmkPRINT("ERROR: %s has error \n",__func__);
    return status;
}
#endif

/*******************************************************************************
**
**	gckGALDEVICE_Setup_ISR
**
**	Start the ISR routine.
**
**	INPUT:
**
**		gckGALDEVICE Device
**			Pointer to an gckGALDEVICE object.
**
**	OUTPUT:
**
**		Nothing.
**
**	RETURNS:
**
**		gcvSTATUS_OK
**			Setup successfully.
**		gcvSTATUS_GENERIC_IO
**			Setup failed.
*/
gceSTATUS
gckGALDEVICE_Setup_ISR(
	IN gckGALDEVICE Device
	)
{
	gctINT ret;

	gcmkVERIFY_ARGUMENT(Device != gcvNULL);

	if (Device->irqLine == 0)
	{
		return gcvSTATUS_GENERIC_IO;
	}

	/* Hook up the isr based on the irq line. */
#ifdef FLAREON
	gc500_handle.dev_name = "galcore interrupt service";
	gc500_handle.dev_id = Device;
	gc500_handle.handler = isrRoutine;
	gc500_handle.intr_gen = GPIO_INTR_LEVEL_TRIGGER;
	gc500_handle.intr_trig = GPIO_TRIG_HIGH_LEVEL;
	ret = dove_gpio_request (DOVE_GPIO0_7, &gc500_handle);
#else
	ret = request_irq(Device->irqLine,
				isrRoutine,
				IRQF_DISABLED,
				"galcore interrupt service",
				Device);
#endif


	if (ret != 0) {
		gcmkTRACE_ZONE(gcvLEVEL_INFO,
				gcvZONE_DRIVER,
            	"[galcore] gckGALDEVICE_Setup_ISR: "
				"Could not register irq line->%d\n",
				Device->irqLine);

		Device->isrInitialized = gcvFALSE;

		return gcvSTATUS_GENERIC_IO;
	}

	Device->isrInitialized = gcvTRUE;

	gcmkTRACE_ZONE(gcvLEVEL_INFO,
				gcvZONE_DRIVER,
            	"[galcore] gckGALDEVICE_Setup_ISR: "
				"Setup the irq line->%d\n",
				Device->irqLine);

	return gcvSTATUS_OK;
}

/*******************************************************************************
**
**	gckGALDEVICE_Release_ISR
**
**	Release the irq line.
**
**	INPUT:
**
**		gckGALDEVICE Device
**			Pointer to an gckGALDEVICE object.
**
**	OUTPUT:
**
**		Nothing.
**
**	RETURNS:
**
**		Nothing.
*/
gceSTATUS
gckGALDEVICE_Release_ISR(
	IN gckGALDEVICE Device
	)
{
	gcmkVERIFY_ARGUMENT(Device != gcvNULL);

	/* release the irq */
	if (Device->isrInitialized)
	{
#ifdef FLAREON
		dove_gpio_free (DOVE_GPIO0_7, "galcore interrupt service");
#else
		free_irq(Device->irqLine, Device);
#endif
        Device->isrInitialized = gcvFALSE;
	}

	return gcvSTATUS_OK;
}

/*******************************************************************************
**
**	gckGALDEVICE_Start_Thread
**
**	Start the daemon thread.
**
**	INPUT:
**
**		gckGALDEVICE Device
**			Pointer to an gckGALDEVICE object.
**
**	OUTPUT:
**
**		Nothing.
**
**	RETURNS:
**
**		gcvSTATUS_OK
**			Start successfully.
**		gcvSTATUS_GENERIC_IO
**			Start failed.
*/
gceSTATUS
gckGALDEVICE_Start_Thread(
	IN gckGALDEVICE Device
	)
{
	gcmkVERIFY_ARGUMENT(Device != gcvNULL);

	/* start the kernel thread */
    Device->threadCtxt = kthread_run(threadRoutine,
					Device,
					"galcore daemon thread");

	Device->threadInitialized = gcvTRUE;
	gcmkTRACE_ZONE(gcvLEVEL_INFO,
					gcvZONE_DRIVER,
					"[galcore] gckGALDEVICE_Start_Thread: "
					"Stat the daemon thread.\n");

	return gcvSTATUS_OK;
}

/*******************************************************************************
**
**	gckGALDEVICE_Stop_Thread
**
**	Stop the gal device, including the following actions: stop the daemon
**	thread, release the irq.
**
**	INPUT:
**
**		gckGALDEVICE Device
**			Pointer to an gckGALDEVICE object.
**
**	OUTPUT:
**
**		Nothing.
**
**	RETURNS:
**
**		Nothing.
*/
gceSTATUS
gckGALDEVICE_Stop_Thread(
	gckGALDEVICE Device
	)
{
	gcmkVERIFY_ARGUMENT(Device != gcvNULL);

	/* stop the thread */
	if (Device->threadInitialized)
	{
		Device->killThread = gcvTRUE;
		up(&Device->sema);

		kthread_stop(Device->threadCtxt);

        Device->threadInitialized = gcvFALSE;
	}

	return gcvSTATUS_OK;
}

/*******************************************************************************
**
**	gckGALDEVICE_Start
**
**	Start the gal device, including the following actions: setup the isr routine
**  and start the daemoni thread.
**
**	INPUT:
**
**		gckGALDEVICE Device
**			Pointer to an gckGALDEVICE object.
**
**	OUTPUT:
**
**		Nothing.
**
**	RETURNS:
**
**		gcvSTATUS_OK
**			Start successfully.
*/
gceSTATUS
gckGALDEVICE_Start(
	IN gckGALDEVICE Device
	)
{
	/* start the daemon thread */
	gcmkVERIFY_OK(gckGALDEVICE_Start_Thread(Device));

	/* setup the isr routine */
	gcmkVERIFY_OK(gckGALDEVICE_Setup_ISR(Device));

    /*
       gcvPOWER_SUSPEND will stop GC and disable 2D/3D clk.
       Since frequency scaling is enabled, disable 2D/3D clk will gain little
       but lead to potential issue. I prefer not change power state here.
    */
#if 0
	/* Switch to SUSPEND power state. */
    /* To simplify power state logic, temporarily use gcvPOWER_OFF instead*/
	gcmkVERIFY_OK(
		gckHARDWARE_SetPowerManagementState(Device->kernel->hardware,
                                            gcvPOWER_SUSPEND_ATPOWERON));
#endif

#if MRVL_PROFILE_THREAD
    Device->profilethread= kthread_run(profile_thread,
				Device,
				"galcore profile thread");
#endif

#if MRVL_GUARD_THREAD
    Device->guardthread= kthread_run(guard_thread,
				Device,
				"galcore guard thread");
#endif

#if MRVL_TIMER
    sema_init(&Device->timersema, 0);
    init_timer(&Device->timer);

    Device->timerthread = kthread_run(timer_thread,
					Device,
					"timer thread");

    Device->timer.data = (unsigned long)Device;
    Device->timer.function = timer_fn;
    /* triggle per second */
    Device->timer.expires = jiffies + HZ;
    add_timer(&Device->timer);
#endif

	return gcvSTATUS_OK;
}

/*******************************************************************************
**
**	gckGALDEVICE_Stop
**
**	Stop the gal device, including the following actions: stop the daemon
**	thread, release the irq.
**
**	INPUT:
**
**		gckGALDEVICE Device
**			Pointer to an gckGALDEVICE object.
**
**	OUTPUT:
**
**		Nothing.
**
**	RETURNS:
**
**		Nothing.
*/
gceSTATUS
gckGALDEVICE_Stop(
    gckGALDEVICE Device
    )
{
    gcmkVERIFY_ARGUMENT(Device != gcvNULL);

    /*
       There is not need to change power state here
    */
#if 0
	/* Switch to ON power state. */
	gcmkVERIFY_OK(
		gckHARDWARE_SetPowerManagementState(Device->kernel->hardware,
											gcvPOWER_ON));
#endif
    if (Device->isrInitialized)
    {
    	gckGALDEVICE_Release_ISR(Device);
    }

    if (Device->threadInitialized)
    {
    	gckGALDEVICE_Stop_Thread(Device);
    }
    
#if MRVL_PROFILE_THREAD
    kthread_stop(Device->profilethread);
#endif

#if MRVL_GUARD_THREAD
    kthread_stop(Device->guardthread);
#endif

#if MRVL_TIMER
    del_timer(&Device->timer);
    kthread_stop(Device->timerthread);
#endif

    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckGALDEVICE_Construct
**
**  Constructor.
**
**  INPUT:
**
**  OUTPUT:
**
**  	gckGALDEVICE * Device
**  	    Pointer to a variable receiving the gckGALDEVICE object pointer on
**  	    success.
*/
gceSTATUS
gckGALDEVICE_Construct(
    IN gctINT IrqLine,
    IN gctUINT32 RegisterMemBase,
    IN gctSIZE_T RegisterMemSize,
    IN gctUINT32 ContiguousBase,
    IN gctSIZE_T ContiguousSize,
    IN gctSIZE_T BankSize,
    IN gctINT FastClear,
	IN gctINT Compression,
	IN gctUINT32 BaseAddress,
	IN gctINT Signal,
    OUT gckGALDEVICE *Device
    )
{
    gctUINT32 internalBaseAddress, internalAlignment;
    gctUINT32 externalBaseAddress, externalAlignment;
    gctUINT32 horizontalTileSize, verticalTileSize;
    gctUINT32 physAddr;
    gctUINT32 physical;
    gckGALDEVICE device;
    gceSTATUS status;

    gcmkTRACE(gcvLEVEL_VERBOSE, "[galcore] Enter gckGALDEVICE_Construct\n");

    gcmkPRINT("\n[galcore] registerBase =0x%08x, registerMemSize = 0x%08x, contiguousBase= 0x%08x, contiguousSize = 0x%08x\n", 
              (gctUINT32)RegisterMemBase, (gctUINT32)RegisterMemSize, (gctUINT32)ContiguousBase, (gctUINT32)ContiguousSize);
    /* Allocate device structure. */
    device = kmalloc(sizeof(struct _gckGALDEVICE), GFP_KERNEL);
    if (!device)
    {
        gcmkLOG_ERROR_ARGS("Can't allocate memory for device.");
    	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
    	    	      "[galcore] gckGALDEVICE_Construct: Can't allocate memory.\n");

    	return gcvSTATUS_OUT_OF_MEMORY;
    }
    memset(device, 0, sizeof(struct _gckGALDEVICE));

    device->clkEnabled = gcvTRUE;

    physical = RegisterMemBase;

    /* Set up register memory region */
    if (physical != 0)
    {
    	/* Request a region. */
    	request_mem_region(RegisterMemBase, RegisterMemSize, "galcore register region");
        device->registerBase = (gctPOINTER) ioremap_nocache(RegisterMemBase,
	    	    	    	    	    	    	    RegisterMemSize);
        if (!device->registerBase)
	    {
    	    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
	    	    	  "[galcore] gckGALDEVICE_Construct: Unable to map location->0x%lX for size->%ld\n",
			  RegisterMemBase,
			  RegisterMemSize);
            /* free device to avoid memory leakage*/
            kfree(device);
            device = gcvNULL;
    	    return gcvSTATUS_OUT_OF_RESOURCES;
        }

    	physical += RegisterMemSize;

		gcmkTRACE_ZONE(gcvLEVEL_INFO,
					gcvZONE_DRIVER,
        			"[galcore] gckGALDEVICE_Construct: "
					"RegisterBase after mapping Address->0x%x is 0x%x\n",
               		(gctUINT32)RegisterMemBase,
					(gctUINT32)device->registerBase);
    }

	device->baseAddress = BaseAddress;
    
 	/* construct the gckOS object */ 
    gcmkONERROR(gckOS_Construct(device, &device->os));

    /* construct the gckKERNEL object. */
    gcmkONERROR(gckKERNEL_Construct(device->os, device, &device->kernel));

    /* set fast clear. */
    gcmkONERROR(gckHARDWARE_SetFastClear(device->kernel->hardware,
    					  				  FastClear,
										  Compression));

    /* query the ceiling of the system memory */
    gcmkONERROR(gckHARDWARE_QuerySystemMemory(device->kernel->hardware,
					&device->systemMemorySize,
                    &device->systemMemoryBaseAddress));

	gcmkTRACE_ZONE(gcvLEVEL_INFO,
					gcvZONE_DRIVER,
					"[galcore] gckGALDEVICE_Construct: "
    				"Will be trying to allocate contiguous memory of 0x%x bytes\n",
           			(gctUINT32)device->systemMemoryBaseAddress);

#if COMMAND_PROCESSOR_VERSION == 1
    /* start the command queue */
    gcmkONERROR(gckCOMMAND_Start(device->kernel->command));
#endif

    /* initialize the thread daemon */
	sema_init(&device->sema, 0);

	device->threadInitialized = gcvFALSE;
	device->killThread = gcvFALSE;

	/* initialize the isr */
	device->isrInitialized = gcvFALSE;
	device->dataReady = gcvFALSE;
	device->irqLine = IrqLine;

	device->signal = Signal;

    device->printPID = gcvFALSE;
    device->silentReset = gcvTRUE;
    device->powerOffWhenIdle = gcvTRUE;
    device->profileStep = 300; /* profiling every 300 ms */
    device->profileTimeSlice = 300; /* take recent 300 ms as profiling foundation */
    /* for a 30 fps application, recent 33 ms idle means the app may be paused */
    device->profileTailTimeSlice = 33; 
    device->powerDebug = gcvFALSE;
    device->idleThreshold = 80; /* try to power off GC when idle time > 80% */
    device->needPowerOff = gcvFALSE;
    
#if SEPARATE_CLOCK_AND_POWER && KEEP_POWER_ON
    device->clkOffOnly = gcvTRUE;
#else
    device->clkOffOnly = gcvFALSE;
#endif

    device->currentPMode = gcvPM_NORMAL;

    device->enableLowPowerMode = gcvFALSE;
    device->enableDVFM = gcvTRUE;

    memset(device->profNode, 0, NUM_PROFILE_NODES*sizeof(struct _gckProfNode));
    device->lastNodeIndex = 0;

    device->memRandomFailRate   = 0;

	/* query the amount of video memory */
    gcmkONERROR(gckHARDWARE_QueryMemory(device->kernel->hardware,
                    &device->internalSize,
                    &internalBaseAddress,
                    &internalAlignment,
                    &device->externalSize,
                    &externalBaseAddress,
                    &externalAlignment,
                    &horizontalTileSize,
                    &verticalTileSize));

	/* set up the internal memory region */
    if (device->internalSize > 0)
    {
        gceSTATUS status = gckVIDMEM_Construct(device->os,
                    internalBaseAddress,
                    device->internalSize,
                    internalAlignment,
                    0,
                    &device->internalVidMem);

        if (gcmIS_ERROR(status))
        {
            /* error, remove internal heap */
            device->internalSize = 0;
        }
        else
        {
            /* map internal memory */
            device->internalPhysical  = (gctPHYS_ADDR)physical;
            device->internalLogical   = (gctPOINTER)ioremap_nocache(
					physical, device->internalSize);

            gcmkASSERT(device->internalLogical != gcvNULL);

			physical += device->internalSize;
        }
    }

    if (device->externalSize > 0)
    {
        /* create the external memory heap */
        gceSTATUS status = gckVIDMEM_Construct(device->os,
					externalBaseAddress,
					device->externalSize,
					externalAlignment,
					0,
					&device->externalVidMem);

        if (gcmIS_ERROR(status))
        {
            /* error, remove internal heap */
            device->externalSize = 0;
        }
        else
        {
            /* map internal memory */
            device->externalPhysical = (gctPHYS_ADDR)physical;
            device->externalLogical = (gctPOINTER)ioremap_nocache(
					physical, device->externalSize);

			gcmkASSERT(device->externalLogical != gcvNULL);

			physical += device->externalSize;
        }
    }

	/* set up the contiguous memory */
    device->contiguousSize = ContiguousSize;

	if (ContiguousBase == 0)
	{
		status = gcvSTATUS_OUT_OF_MEMORY;

		while (device->contiguousSize > 0)
		{
			gcmkTRACE_ZONE(
				gcvLEVEL_INFO, gcvZONE_DRIVER,
				"[galcore] gckGALDEVICE_Construct: Will be trying to allocate contiguous memory of %ld bytes\n",
				device->contiguousSize
				);

			/* allocate contiguous memory */
			status = gckGALDEVICE_AllocateMemory(
				device,
				device->contiguousSize,
				&device->contiguousBase,
				&device->contiguousPhysical,
				&physAddr
				);

			if (gcmIS_SUCCESS(status))
			{
	    		gcmkTRACE_ZONE(
					gcvLEVEL_INFO, gcvZONE_DRIVER,
					"[galcore] gckGALDEVICE_Construct: Contiguous allocated size->0x%08X Virt->0x%08lX physAddr->0x%08X\n",
					device->contiguousSize,
					device->contiguousBase,
					physAddr
					);

				status = gckVIDMEM_Construct(
					device->os,
					physAddr | device->systemMemoryBaseAddress,
					device->contiguousSize,
					64,
					BankSize,
					&device->contiguousVidMem
					);

				if (gcmIS_SUCCESS(status))
				{
					device->contiguousMapped = gcvFALSE;

					/* success, abort loop */
					gcmkTRACE_ZONE(
						gcvLEVEL_INFO, gcvZONE_DRIVER,
						"Using %u bytes of contiguous memory.\n",
						device->contiguousSize
						);

					break;
				}

				gcmkONERROR(gckGALDEVICE_FreeMemory(
					device,
					device->contiguousBase,
					device->contiguousPhysical
					));

				device->contiguousBase = gcvNULL;
			}

			if (device->contiguousSize <= (4 << 20))
			{
				device->contiguousSize = 0;
			}
			else
			{
				device->contiguousSize -= (4 << 20);
			}
		}
	}
	else
	{
		/* Create the contiguous memory heap. */
		status = gckVIDMEM_Construct(
			device->os,
			(ContiguousBase - device->baseAddress) | device->systemMemoryBaseAddress,
			ContiguousSize,
			64,
			BankSize,
			&device->contiguousVidMem
			);

		if (gcmIS_ERROR(status))
		{
			/* Error, roll back. */
			device->contiguousVidMem = gcvNULL;
			device->contiguousSize   = 0;
		}
		else
		{
			/* Map the contiguous memory. */
			request_mem_region(
				ContiguousBase,
				ContiguousSize,
				"galcore managed memory"
				);

			device->contiguousPhysical = (gctPHYS_ADDR) ContiguousBase;
			device->contiguousSize     = ContiguousSize;
//			device->contiguousBase     = (gctPOINTER) ioremap_nocache(ContiguousBase, ContiguousSize);
			device->contiguousBase     = (gctPOINTER)ContiguousBase;
			device->contiguousMapped   = gcvTRUE;

			if (device->contiguousBase == gcvNULL)
			{
    			/* Error, roll back. */
				gcmkVERIFY_OK(gckVIDMEM_Destroy(device->contiguousVidMem));
				device->contiguousVidMem = gcvNULL;
				device->contiguousSize   = 0;

				status = gcvSTATUS_OUT_OF_RESOURCES;
			}
		}
	}

    *Device = device;

    gcmkPRINT("\n[galcore] real contiguouSize = 0x%08x \n",  (gctUINT32)(device->contiguousSize));
    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
    	    	  "[galcore] gckGALDEVICE_Construct: Initialized device->0x%p contiguous->%lu @ 0x%p (0x%08X)\n",
		  device,
		  device->contiguousSize,
		  device->contiguousBase,
		  device->contiguousPhysical);

	/* Init GC memory profile. */
	device->reservedMem = ContiguousSize;
	device->vidMemUsage = 0;
	device->contiguousMemUsage = 0;
	device->virtualMemUsage = 0;

    return gcvSTATUS_OK;
    
OnError:
	/* Return the status. */
	gcmkFOOTER();
	return status;
}

/*******************************************************************************
**
**	gckGALDEVICE_Destroy
**
**	Class destructor.
**
**	INPUT:
**
**		Nothing.
**
**	OUTPUT:
**
**		Nothing.
**
**	RETURNS:
**
**		Nothing.
*/
gceSTATUS
gckGALDEVICE_Destroy(
	gckGALDEVICE Device)
{
	gcmkVERIFY_ARGUMENT(Device != gcvNULL);

    gcmkTRACE(gcvLEVEL_VERBOSE, "[ENTER] gckGALDEVICE_Destroy\n");

    /* Destroy the gckKERNEL object. */
    gcmkVERIFY_OK(gckKERNEL_Destroy(Device->kernel));
    Device->kernel = gcvNULL;

    if (Device->internalVidMem != gcvNULL)
    {
        /* destroy the internal heap */
        gcmkVERIFY_OK(gckVIDMEM_Destroy(Device->internalVidMem));

        Device->internalVidMem = gcvNULL;
		/* unmap the internal memory */
		iounmap(Device->internalLogical);
    }

    if (Device->externalVidMem != gcvNULL)
    {
        /* destroy the internal heap */
        gcmkVERIFY_OK(gckVIDMEM_Destroy(Device->externalVidMem));

        Device->externalVidMem = gcvNULL;
        /* unmap the external memory */
		iounmap(Device->externalLogical);
    }

    if (Device->contiguousVidMem != gcvNULL)
    {
        /* Destroy the contiguous heap */
        gcmkVERIFY_OK(gckVIDMEM_Destroy(Device->contiguousVidMem));
        Device->contiguousVidMem = gcvNULL;

    	if (Device->contiguousMapped)
    	{
        	gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
    	    	  "[galcore] gckGALDEVICE_Destroy: "
    		  "Unmapping contiguous memory->0x%08lX\n",
    		  Device->contiguousBase);

    	    iounmap(Device->contiguousBase);
    	}
    	else
    	{
        	gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
    	    	  "[galcore] gckGALDEVICE_Destroy: "
    		  "Freeing contiguous memory->0x%08lX\n",
    		  Device->contiguousBase);

        	gcmkVERIFY_OK(gckGALDEVICE_FreeMemory(Device,
    					 Device->contiguousBase,
    					 Device->contiguousPhysical));
    	}
    }

    if (Device->registerBase)
    {
        iounmap(Device->registerBase);
    }

    /* Destroy the gckOS object. */
    gcmkVERIFY_OK(gckOS_Destroy(Device->os));
    Device->os = gcvNULL;

    kfree(Device);
    Device = gcvNULL;

    gcmkTRACE(gcvLEVEL_VERBOSE, "[galcore] Leave gckGALDEVICE_Destroy\n");

    return gcvSTATUS_OK;
}

