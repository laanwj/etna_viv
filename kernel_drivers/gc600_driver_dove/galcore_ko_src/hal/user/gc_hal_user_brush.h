/****************************************************************************
*
*    Copyright (c) 2005 - 2010 by Vivante Corp.  All rights reserved.
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




#ifndef __gc_hal_user_brush_h_
#define __gc_hal_user_brush_h_

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
***************************** gcoBRUSH_CACHE Object *****************************
\******************************************************************************/

/* Create an gcoBRUSH_CACHE object. */
gceSTATUS
gcoBRUSH_CACHE_Construct(
	IN gcoHAL Hal,
	gcoBRUSH_CACHE * BrushCache
	);

/* Destroy an gcoBRUSH_CACHE object. */
gceSTATUS
gcoBRUSH_CACHE_Destroy(
	IN gcoBRUSH_CACHE BrushCache
	);

/* Sets the maximum number of brushes in the cache. */
gceSTATUS
gcoBRUSH_CACHE_SetBrushLimit(
	IN gcoBRUSH_CACHE BrushCache,
	IN gctUINT MaxCount
	);

/* Compute the brush ID based on the brush data. */
gceSTATUS
gcoBRUSH_CACHE_GetBrushID(
	IN gcoBRUSH_CACHE BrushCache,
	IN gctPOINTER BrushData,
	IN gctUINT32 DataCount,
	IN OUT gctUINT32 * BrushID
	);

/* Find a matching brush by the passed brush data set and return a pointer
   to the brush. If a matching brush was found, its usage counter is
   automatically incremented. Call gcoBRUSH_CACHE_DeleteBrush
   to release the brush. */
gceSTATUS
gcoBRUSH_CACHE_GetBrush(
	IN gcoBRUSH_CACHE BrushCache,
	IN gctUINT32 BrushID,
	IN gctPOINTER BrushData,
	IN gctUINT32 DataCount,
	IN OUT gcoBRUSH * Brush
	);

/* Add a brush to the brush cache. */
gceSTATUS
gcoBRUSH_CACHE_AddBrush(
	IN gcoBRUSH_CACHE BrushCache,
	IN gcoBRUSH Brush,
	IN gctUINT32 BrushID,
	IN gctBOOL NeedMemory
	);

/* Remove a brush from the brush cache. */
gceSTATUS
gcoBRUSH_CACHE_DeleteBrush(
	IN gcoBRUSH_CACHE BrushCache,
	IN gcoBRUSH Brush
	);

/* Flush the brush. */
gceSTATUS
gcoBRUSH_CACHE_FlushBrush(
	IN gcoBRUSH_CACHE BrushCache,
	IN gcoBRUSH Brush
	);


/******************************************************************************\
******************************** gcoBRUSH Object *******************************
\******************************************************************************/

/* Frees all resources held up the specified gcoBRUSH object. */
gceSTATUS
gcoBRUSH_Delete(
	IN gcoBRUSH Brush
	);

/* Returns a buffer fileld with complete set of brush parameters. */
gceSTATUS
gcoBRUSH_GetBrushData(
	IN gcoBRUSH Brush,
	IN OUT gctPOINTER BrushData,
	IN OUT gctUINT32 * DataCount
	);

/* Flush the brush. */
gceSTATUS
gcoBRUSH_FlushBrush(
	IN gcoBRUSH Brush,
	IN gctBOOL Upload,
	IN gcsSURF_NODE_PTR Node
	);


/******************************************************************************\
********************************** gco2D Object *********************************
\******************************************************************************/

/* Return a pointer to the brush cache. */
gceSTATUS
gco2D_GetBrushCache(
	IN gco2D Hardware,
	IN OUT gcoBRUSH_CACHE * BrushCache
	);

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_user_brush_h_ */

