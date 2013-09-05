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




#ifndef __gc_hal_options_h_
#define __gc_hal_options_h_

/*
    VIVANTE_NO_3D

        This define disables support for 3D rendering.
*/
#ifndef VIVANTE_NO_3D
#   define VIVANTE_NO_3D                        0
#endif

/*
    VIVANTE_PROFILER

        This define enables the profiler.
*/
#ifndef VIVANTE_PROFILER
#   define VIVANTE_PROFILER                     0
#endif

/*
    gcdSECURE_USER

        Use logical addresses instead of physical addresses in user land.  In
        this case a hint table is created for both command buffers and context
        buffers, and that hint table will be used to patch up those buffers in
        the kernel when they are ready to submit.
*/
#ifndef gcdSECURE_USER
#   define gcdSECURE_USER                       0
#endif

/******************************************************************************\
************************************* Debug ************************************
\******************************************************************************/

/* Possible debug flags. */
#define gcdDEBUG_NONE           0
#define gcdDEBUG_ALL            (1 << 0)
#define gcdDEBUG_FATAL          (1 << 1)
#define gcdDEBUG_TRACE          (1 << 2)
#define gcdDEBUG_BREAK          (1 << 3)
#define gcdDEBUG_ASSERT         (1 << 4)
#define gcdDEBUG_CODE           (1 << 5)
#define gcdDEBUG_STACK          (1 << 6)

#define gcmIS_DEBUG(flag)       ( gcdDEBUG & (flag | gcdDEBUG_ALL) )

#ifndef gcdDEBUG
#   define gcdDEBUG         gcdDEBUG_NONE
#endif

#endif /* __gc_hal_options_h_ */
