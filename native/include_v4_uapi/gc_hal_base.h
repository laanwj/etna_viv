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




#ifndef __gc_hal_base_h_
#define __gc_hal_base_h_

#include "gc_hal_options.h"


/******************************************************************************\
****************************** Object Declarations *****************************
\******************************************************************************/

typedef union  _gcuVIDMEM_NODE *        gcuVIDMEM_NODE_PTR;

/******************************************************************************\
********************************* Enumerations *********************************
\******************************************************************************/

typedef enum _gcePLS_VALUE
{
  gcePLS_VALUE_EGL_DISPLAY_INFO,
  gcePLS_VALUE_EGL_SURFACE_INFO
}
gcePLS_VALUE;

/* Video memory pool type. */
typedef enum _gcePOOL
{
    gcvPOOL_UNKNOWN = 0,
    gcvPOOL_DEFAULT,
    gcvPOOL_LOCAL,
    gcvPOOL_LOCAL_INTERNAL,
    gcvPOOL_LOCAL_EXTERNAL,
    gcvPOOL_UNIFIED,
    gcvPOOL_SYSTEM,
    gcvPOOL_VIRTUAL,
    gcvPOOL_USER,
    gcvPOOL_CONTIGUOUS,

    gcvPOOL_NUMBER_OF_POOLS
}
gcePOOL;

#if !VIVANTE_NO_3D
/* Blending functions. */
typedef enum _gceBLEND_FUNCTION
{
    gcvBLEND_ZERO,
    gcvBLEND_ONE,
    gcvBLEND_SOURCE_COLOR,
    gcvBLEND_INV_SOURCE_COLOR,
    gcvBLEND_SOURCE_ALPHA,
    gcvBLEND_INV_SOURCE_ALPHA,
    gcvBLEND_TARGET_COLOR,
    gcvBLEND_INV_TARGET_COLOR,
    gcvBLEND_TARGET_ALPHA,
    gcvBLEND_INV_TARGET_ALPHA,
    gcvBLEND_SOURCE_ALPHA_SATURATE,
    gcvBLEND_CONST_COLOR,
    gcvBLEND_INV_CONST_COLOR,
    gcvBLEND_CONST_ALPHA,
    gcvBLEND_INV_CONST_ALPHA,
}
gceBLEND_FUNCTION;

/* Blending modes. */
typedef enum _gceBLEND_MODE
{
    gcvBLEND_ADD,
    gcvBLEND_SUBTRACT,
    gcvBLEND_REVERSE_SUBTRACT,
    gcvBLEND_MIN,
    gcvBLEND_MAX,
}
gceBLEND_MODE;

/* API flags. */
typedef enum _gceAPI
{
    gcvAPI_D3D                  = 0x1,
    gcvAPI_OPENGL               = 0x2,
    gcvAPI_OPENVG               = 0x3,
    gcvAPI_OPENCL               = 0x4,
}
gceAPI;

/* Depth modes. */
typedef enum _gceDEPTH_MODE
{
    gcvDEPTH_NONE,
    gcvDEPTH_Z,
    gcvDEPTH_W,
}
gceDEPTH_MODE;
#endif /* VIVANTE_NO_3D */

typedef enum _gceWHERE
{
    gcvWHERE_COMMAND,
    gcvWHERE_RASTER,
    gcvWHERE_PIXEL,
}
gceWHERE;

typedef enum _gceHOW
{
    gcvHOW_SEMAPHORE            = 0x1,
    gcvHOW_STALL                = 0x2,
    gcvHOW_SEMAPHORE_STALL      = 0x3,
}
gceHOW;

typedef enum _gceSignalHandlerType
{
    gcvHANDLE_SIGFPE_WHEN_SIGNAL_CODE_IS_0        = 0x1,
}
gceSignalHandlerType;

#endif /* __gc_hal_base_h_ */
