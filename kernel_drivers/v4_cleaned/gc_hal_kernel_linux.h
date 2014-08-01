/****************************************************************************
*
*    Copyright (C) 2005 - 2012 by Vivante Corp.
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

#ifndef __gc_hal_kernel_linux_h_
#define __gc_hal_kernel_linux_h_

#include "gc_hal_options_internal.h"

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/signal.h>
#ifdef FLAREON
#   include <asm/arch-realview/dove_gpio_irq.h>
#endif
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>

#ifdef MODVERSIONS
#  include <linux/modversions.h>
#endif
#include <asm/io.h>
#include <asm/uaccess.h>

#include <linux/clk.h>

#include "gc_hal.h"
#include "gc_hal_internal.h"
#include "gc_hal_kernel.h"
#include "gc_hal_kernel_device.h"
#include "gc_hal_kernel_os.h"

#define DRV_NAME          			"galcore"

#define GetPageCount(size, offset) 	((((size) + ((offset) & ~PAGE_CACHE_MASK)) + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT)

static inline int
GetOrder(
	IN int numPages
	)
{
    int order = 0;

	while ((1 << order) <  numPages) order++;

	return order;
}

#endif /* __gc_hal_kernel_linux_h_ */
