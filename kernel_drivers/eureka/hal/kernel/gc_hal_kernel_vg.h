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






#ifndef __gc_hal_kernel_vg_h_
#define __gc_hal_kernel_vg_h_

#include "gc_hal.h"
#include "gc_hal_driver.h"
#include "gc_hal_kernel_hardware.h"

/******************************************************************************\
********************************** Structures **********************************
\******************************************************************************/


/* gckKERNEL object. */
struct _gckVGKERNEL
{
    /* Object. */
    gcsOBJECT                   object;

    /* Pointer to gckOS object. */
    gckOS                       os;

    /* Pointer to gckHARDWARE object. */
    gckVGHARDWARE                   hardware;

    /* Pointer to gckINTERRUPT object. */
    gckVGINTERRUPT              interrupt;

    /* Pointer to gckCOMMAND object. */
    gckVGCOMMAND                    command;

    /* Pointer to context. */
    gctPOINTER                  context;

    /* Pointer to gckMMU object. */
    gckVGMMU                        mmu;

    gckKERNEL                   kernel;
};

/* gckMMU object. */
struct _gckVGMMU
{
    /* The object. */
    gcsOBJECT                   object;

    /* Pointer to gckOS object. */
    gckOS                       os;

    /* Pointer to gckHARDWARE object. */
    gckVGHARDWARE                   hardware;

    /* The page table mutex. */
    gctPOINTER                  mutex;

    /* Page table information. */
    gctSIZE_T                   pageTableSize;
    gctPHYS_ADDR                pageTablePhysical;
    gctPOINTER                  pageTableLogical;

    /* Allocation index. */
    gctUINT32                   entryCount;
    gctUINT32                   entry;
};

#endif /* __gc_hal_kernel_h_ */
