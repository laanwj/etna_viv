/****************************************************************************
*  
*    Copyright (c) 2002 - 2008 by Vivante Corp.  All rights reserved.
*  
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed, 
*    reproduced, copied, transmitted, or used in any way for any purpose, 
*    without the express written permission of Vivante Corporation.
*  
*****************************************************************************
*  
*  
*****************************************************************************/

.
#ifndef __gl2_profile_h_
#define __gl2_profile_h_

#include <stdio.h>


#define GLVERTEX_OBJECT 10
#define GLVERTEX_OBJECT_BYTES 11

#define GLINDEX_OBJECT 20
#define GLINDEX_OBJECT_BYTES 21
  
#define GLTEXTURE_OBJECT 30
#define GLTEXTURE_OBJECT_BYTES 31
               
#define GL_SHADER_OBJECT 40

#define GL_PROGRAM_IN_USE_BEGIN 50

#if VIVANTE_PROFILER
#	define gcmPROFILE_GC(hal, _enum, value) countGCProfiler(hal, _enum, value)
#else
#	define gcmPROFILE_GC(hal, _enum, value)
#endif

void countGCProfiler( IN gcoHAL , gctUINT32 , int );

#endif
