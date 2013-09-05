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




#ifndef __gc_hal_h_
#define __gc_hal_h_

#include "gc_hal_types.h"


/* Type of objects. */
typedef enum _gceOBJECT_TYPE
{
    gcvOBJ_UNKNOWN              = 0,
    gcvOBJ_2D                   = gcmCC('2','D',' ',' '),
    gcvOBJ_3D                   = gcmCC('3','D',' ',' '),
    gcvOBJ_ATTRIBUTE            = gcmCC('A','T','T','R'),
    gcvOBJ_BRUSHCACHE           = gcmCC('B','R','U','$'),
    gcvOBJ_BRUSHNODE            = gcmCC('B','R','U','n'),
    gcvOBJ_BRUSH                = gcmCC('B','R','U','o'),
    gcvOBJ_BUFFER               = gcmCC('B','U','F','R'),
    gcvOBJ_COMMAND              = gcmCC('C','M','D',' '),
    gcvOBJ_COMMANDBUFFER        = gcmCC('C','M','D','B'),
    gcvOBJ_CONTEXT              = gcmCC('C','T','X','T'),
    gcvOBJ_DEVICE               = gcmCC('D','E','V',' '),
    gcvOBJ_DUMP                 = gcmCC('D','U','M','P'),
    gcvOBJ_EVENT                = gcmCC('E','V','N','T'),
    gcvOBJ_FUNCTION             = gcmCC('F','U','N','C'),
    gcvOBJ_HAL                  = gcmCC('H','A','L',' '),
    gcvOBJ_HARDWARE             = gcmCC('H','A','R','D'),
    gcvOBJ_HEAP                 = gcmCC('H','E','A','P'),
    gcvOBJ_INDEX                = gcmCC('I','N','D','X'),
    gcvOBJ_INTERRUPT            = gcmCC('I','N','T','R'),
    gcvOBJ_KERNEL               = gcmCC('K','E','R','N'),
    gcvOBJ_KERNEL_FUNCTION      = gcmCC('K','F','C','N'),
    gcvOBJ_MEMORYBUFFER         = gcmCC('M','E','M','B'),
    gcvOBJ_MMU                  = gcmCC('M','M','U',' '),
    gcvOBJ_OS                   = gcmCC('O','S',' ',' '),
    gcvOBJ_OUTPUT               = gcmCC('O','U','T','P'),
    gcvOBJ_PAINT                = gcmCC('P','N','T',' '),
    gcvOBJ_PATH                 = gcmCC('P','A','T','H'),
    gcvOBJ_QUEUE                = gcmCC('Q','U','E',' '),
    gcvOBJ_SAMPLER              = gcmCC('S','A','M','P'),
    gcvOBJ_SHADER               = gcmCC('S','H','D','R'),
    gcvOBJ_STREAM               = gcmCC('S','T','R','M'),
    gcvOBJ_SURF                 = gcmCC('S','U','R','F'),
    gcvOBJ_TEXTURE              = gcmCC('T','X','T','R'),
    gcvOBJ_UNIFORM              = gcmCC('U','N','I','F'),
    gcvOBJ_VARIABLE             = gcmCC('V','A','R','I'),
    gcvOBJ_VERTEX               = gcmCC('V','R','T','X'),
    gcvOBJ_VIDMEM               = gcmCC('V','M','E','M'),
    gcvOBJ_VG                   = gcmCC('V','G',' ',' '),
}
gceOBJECT_TYPE;

/* gcsOBJECT object defintinon. */
typedef struct _gcsOBJECT
{
    /* Type of an object. */
    gceOBJECT_TYPE              type;
}
gcsOBJECT;

#endif /* __gc_hal_h_ */
