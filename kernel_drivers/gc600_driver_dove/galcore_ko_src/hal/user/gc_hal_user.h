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




#ifndef __gc_hal_user_h_
#define __gc_hal_user_h_

#include "gc_hal.h"
#include "gc_hal_driver.h"
#include "gc_hal_enum.h"
#include "gc_hal_dump.h"
#include "gc_hal_base.h"
#include "gc_hal_raster.h"

#ifndef VIVANTE_NO_3D
#include "gc_hal_engine.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if MRVL_OPTI_USE_RESERVE_MEMORY
#define RESERVE_MEMORY_NUM  3
#define RESERVE_MEMORY_SIZE 524288
#endif
/******************************************************************************\
******************************* Multicast values *******************************
\******************************************************************************/

/* Value types. */
typedef enum _gceVALUE_TYPE
{
	gcvVALUE_UINT,
	gcvVALUE_FIXED,
	gcvVALUE_FLOAT,
}
gceVALUE_TYPE;

/* Value unions. */
typedef union _gcuVALUE
{
	gctUINT						uintValue;
	gctFIXED_POINT				fixedValue;
	gctFLOAT					floatValue;
}
gcuVALUE;

/******************************************************************************\
***************************** gcsSAMPLES Structure *****************************
\******************************************************************************/

typedef struct _gcsSAMPLES
{
	gctUINT8 x;
	gctUINT8 y;
}
gcsSAMPLES;

/******************************************************************************\
****************************** Object Declarations *****************************
\******************************************************************************/

typedef struct _gcoBUFFER *				gcoBUFFER;

/******************************************************************************\
******************************* gcoHARDWARE Object ******************************
\******************************************************************************/

/*----------------------------------------------------------------------------*/
/*----------------------------- gcoHARDWARE Common ----------------------------*/

/* Construct a new gcoHARDWARE object. */
gceSTATUS
gcoHARDWARE_Construct(
	IN gcoHAL Hal,
	OUT gcoHARDWARE * Hardware
	);

/* Destroy an gcoHARDWARE object. */
gceSTATUS
gcoHARDWARE_Destroy(
	IN gcoHARDWARE Hardware
	);

/* Query the identity of the hardware. */
gceSTATUS
gcoHARDWARE_QueryChipIdentity(
    IN gcoHARDWARE Hardware,
	OUT gceCHIPMODEL* ChipModel,
	OUT gctUINT32* ChipRevision,
	OUT gctUINT32* ChipFeatures,
	OUT gctUINT32* ChipMinorFeatures,
	OUT gctUINT32* ChipMinorFeatures1
	);

/* Verify whether the specified feature is available in hardware. */
gceSTATUS
gcoHARDWARE_IsFeatureAvailable(
    IN gcoHARDWARE Hardware,
	IN gceFEATURE Feature
    );

/* Query command buffer requirements. */
gceSTATUS
gcoHARDWARE_QueryCommandBuffer(
	IN gcoHARDWARE Hardware,
	OUT gctSIZE_T * Alignment,
	OUT gctSIZE_T * ReservedHead,
	OUT gctSIZE_T * ReservedTail
	);

/* Select a graphics pipe. */
gceSTATUS
gcoHARDWARE_SelectPipe(
	IN gcoHARDWARE Hardware,
	IN gctUINT8 Pipe
	);

/* Flush the current graphics pipe. */
gceSTATUS
gcoHARDWARE_FlushPipe(
	IN gcoHARDWARE Hardware
	);

/* Send semaphore down the current pipe. */
gceSTATUS
gcoHARDWARE_Semaphore(
	IN gcoHARDWARE Hardware,
	IN gceWHERE From,
	IN gceWHERE To,
	IN gceHOW How
	);

/* Load a number of load states. */
gceSTATUS
gcoHARDWARE_LoadState(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Address,
	IN gctSIZE_T Count,
	IN gctPOINTER States
	);

/* Load a number of load states. */
gceSTATUS
gcoHARDWARE_LoadStateBuffer(
	IN gcoHARDWARE Hardware,
	IN gctCONST_POINTER StateBuffer,
	IN gctSIZE_T Bytes
	);

/* Load a number of load states in fixed-point (3D pipe). */
gceSTATUS
gcoHARDWARE_LoadStateX(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Address,
	IN gctSIZE_T Count,
	IN gctPOINTER States
	);

/* Load a number of load states in floating-point (3D pipe). */
gceSTATUS
gcoHARDWARE_LoadStateF(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Address,
	IN gctSIZE_T Count,
	IN gctPOINTER States
	);

/* Load one 32-bit load state. */
gceSTATUS
gcoHARDWARE_LoadState32(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Address,
	IN gctUINT32 Data
	);

/* Load one 32-bit load state. */
gceSTATUS
gcoHARDWARE_LoadState32x(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Address,
	IN gctFIXED_POINT Data
	);

/* Load one 64-bit load state. */
gceSTATUS
gcoHARDWARE_LoadState64(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Address,
	IN gctUINT64 Data
	);

gceSTATUS
gcoHARDWARE_LoadStateBlock(
	IN gcoHARDWARE Hardware,
	IN gctUINT32_PTR States,
	IN gctSIZE_T Count
	);

/* Preserve cmd buffer space */
gceSTATUS
gcoHARDWARE_PreserveCmdSpace(
    IN gcoHARDWARE  Hardware,
    IN gctSIZE_T    Size
    );

gceSTATUS
gcoHARDWARE_SkipContext(
    IN gcoHARDWARE  Hardware,
    IN gctBOOL      Value
    );

/* Commit the current command buffer. */
gceSTATUS
gcoHARDWARE_Commit(
	IN gcoHARDWARE Hardware
	);

/* Stall the pipe. */
gceSTATUS
gcoHARDWARE_Stall(
	IN gcoHARDWARE Hardware
	);

/* Compute the offset of the specified pixel location. */
gceSTATUS
gcoHARDWARE_ComputeOffset(
    IN gctINT32 X,
    IN gctINT32 Y,
    IN gctUINT Stride,
    IN gctINT BytesPerPixel,
    IN gceTILING Tiling,
    OUT gctUINT32_PTR Offset
    );

/* Resolve. */
gceSTATUS
gcoHARDWARE_ResolveRect(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_INFO_PTR SrcInfo,
	IN gcsSURF_INFO_PTR DestInfo,
	IN gcsPOINT_PTR SrcOrigin,
	IN gcsPOINT_PTR DestOrigin,
	IN gcsPOINT_PTR RectSize
	);

/* Resolve depth buffer. */
gceSTATUS
gcoHARDWARE_ResolveDepth(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 SrcTileStatusAddress,
	IN gcsSURF_INFO_PTR SrcInfo,
	IN gcsSURF_INFO_PTR DestInfo,
	IN gcsPOINT_PTR SrcOrigin,
	IN gcsPOINT_PTR DestOrigin,
	IN gcsPOINT_PTR RectSize
	);

/* Query the tile size of the given surface. */
gceSTATUS
gcoHARDWARE_GetSurfaceTileSize(
	IN gcsSURF_INFO_PTR Surface,
	OUT gctINT32 * TileWidth,
	OUT gctINT32 * TileHeight
	);

/* Query tile sizes. */
gceSTATUS
gcoHARDWARE_QueryTileSize(
	OUT gctINT32 * TileWidth2D,
	OUT gctINT32 * TileHeight2D,
	OUT gctINT32 * TileWidth3D,
	OUT gctINT32 * TileHeight3D,
	OUT gctUINT32 * StrideAlignment
	);

/* Get tile status sizes for a surface. */
gceSTATUS
gcoHARDWARE_QueryTileStatus(
	IN gcoHARDWARE Hardware,
	IN gctUINT Width,
	IN gctUINT Height,
	IN gctSIZE_T Bytes,
	OUT gctSIZE_T_PTR Size,
	OUT gctUINT_PTR Alignment,
	OUT gctUINT32_PTR Filler
	);

/* Enable tile status for a surface. */
gceSTATUS
gcoHARDWARE_EnableTileStatus(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_INFO_PTR Surface,
	IN gctUINT32 TileStatusAddress,
	IN gcsSURF_NODE_PTR HzTileStatus
	);

/* Disable tile status for a surface. */
gceSTATUS
gcoHARDWARE_DisableTileStatus(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_INFO_PTR Surface,
	IN gctBOOL CpuAccess
	);

/* Flush tile status cache. */
gceSTATUS
gcoHARDWARE_FlushTileStatus(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_INFO_PTR Surface,
	IN gctBOOL Decompress
	);

typedef enum _gceTILE_STATUS_CONTROL
{
	gcvTILE_STATUS_PAUSE,
	gcvTILE_STATUS_RESUME,
}
gceTILE_STATUS_CONTROL;

/* Pause or resume tile status. */
gceSTATUS gcoHARDWARE_PauseTileStatus(
	IN gcoHARDWARE Hardware,
	IN gceTILE_STATUS_CONTROL Control
	);

/* Lock a surface. */
gceSTATUS
gcoHARDWARE_Lock(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_NODE_PTR Node,
	OUT gctUINT32 * Address,
	OUT gctPOINTER * Memory
	);

/* Unlock a surface. */
gceSTATUS
gcoHARDWARE_Unlock(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_NODE_PTR Node,
	IN gceSURF_TYPE Type
	);

/* Call kernel for event. */
gceSTATUS
gcoHARDWARE_CallEvent(
	IN gcoHARDWARE Hardware,
	IN OUT gcsHAL_INTERFACE * Interface
	);

/* Schedule destruction for the specified video memory node. */
gceSTATUS
gcoHARDWARE_ScheduleVideoMemory(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_NODE_PTR Node
	);

/* Allocate a temporary surface with specified parameters. */
gceSTATUS
gcoHARDWARE_AllocateTemporarySurface(
	IN gcoHARDWARE Hardware,
	IN gctUINT Width,
	IN gctUINT Height,
	IN gcsSURF_FORMAT_INFO_PTR Format,
	IN gceSURF_TYPE Type
	);

/* Free the temporary surface. */
gceSTATUS
gcoHARDWARE_FreeTemporarySurface(
	IN gcoHARDWARE Hardware,
	IN gctBOOL Synchronized
	);

/* Convert pixel format. */
gceSTATUS
gcoHARDWARE_ConvertPixel(
	IN gcoHARDWARE Hardware,
	IN gctPOINTER SrcPixel,
	OUT gctPOINTER TrgPixel,
	IN gctUINT SrcBitOffset,
	IN gctUINT TrgBitOffset,
	IN gcsSURF_FORMAT_INFO_PTR SrcFormat,
	IN gcsSURF_FORMAT_INFO_PTR TrgFormat,
	IN gcsBOUNDARY_PTR SrcBoundary,
	IN gcsBOUNDARY_PTR TrgBoundary
	);

/* Copy a rectangular area with format conversion. */
gceSTATUS
gcoHARDWARE_CopyPixels(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_INFO_PTR Source,
	IN gcsSURF_INFO_PTR Target,
	IN gctINT SourceX,
	IN gctINT SourceY,
	IN gctINT TargetX,
	IN gctINT TargetY,
	IN gctINT Width,
	IN gctINT Height
	);

/* Enable or disable anti-aliasing. */
gceSTATUS
gcoHARDWARE_SetAntiAlias(
	IN gcoHARDWARE Hardware,
	IN gctBOOL Enable
	);

/* Write data into the command buffer. */
gceSTATUS
gcoHARDWARE_WriteBuffer(
	IN gcoHARDWARE Hardware,
	IN gctCONST_POINTER Data,
	IN gctSIZE_T Bytes,
	IN gctBOOL Aligned
	);

/* Convert RGB8 color value to YUV color space. */
void gcoHARDWARE_RGB2YUV(
	gctUINT8 R,
	gctUINT8 G,
	gctUINT8 B,
	gctUINT8_PTR Y,
	gctUINT8_PTR U,
	gctUINT8_PTR V
	);

/* Convert YUV color value to RGB8 color space. */
void gcoHARDWARE_YUV2RGB(
	gctUINT8 Y,
	gctUINT8 U,
	gctUINT8 V,
	gctUINT8_PTR R,
	gctUINT8_PTR G,
	gctUINT8_PTR B
	);

/* Convert an API format. */
gceSTATUS
gcoHARDWARE_ConvertFormat(
	IN gcoHARDWARE Hardware,
	IN gceSURF_FORMAT Format,
	OUT gctUINT32 * BitsPerPixel,
	OUT gctUINT32 * BytesPerTile
	);

/* Convert face to offset */
gceSTATUS
gcoHARDWARE_ConvertFace(
    IN gcoHARDWARE Hardware,
    IN gctUINT32 Width,
    IN gctUINT32 Height,
    IN gceSURF_FORMAT Format,
    IN gctUINT Face,
    OUT gctUINT32 * Offset
    );

/* Align size to tile boundary. */
gceSTATUS
gcoHARDWARE_AlignToTile(
	IN gcoHARDWARE Hardware,
	IN gceSURF_TYPE Type,
	IN OUT gctUINT32_PTR Width,
	IN OUT gctUINT32_PTR Height,
	OUT gctBOOL_PTR SuperTiled
	);

/*----------------------------------------------------------------------------*/
/*----------------------- gcoHARDWARE Fragment Processor ---------------------*/

/* Set the fragment processor configuration. */
gceSTATUS
gcoHARDWARE_SetFragmentConfiguration(
	IN gcoHARDWARE Hardware,
	IN gctBOOL ColorFromStream,
	IN gctBOOL EnableFog,
	IN gctBOOL EnableSmoothPoint,
	IN gctUINT32 ClipPlanes
	);

/* Enable/disable texture stage operation. */
gceSTATUS
gcoHARDWARE_EnableTextureStage(
	IN gcoHARDWARE Hardware,
	IN gctINT Stage,
	IN gctBOOL Enable
	);

/* Program the channel enable masks for the color texture function. */
gceSTATUS
gcoHARDWARE_SetTextureColorMask(
	IN gcoHARDWARE Hardware,
	IN gctINT Stage,
	IN gctBOOL ColorEnabled,
	IN gctBOOL AlphaEnabled
	);

/* Program the channel enable masks for the alpha texture function. */
gceSTATUS
gcoHARDWARE_SetTextureAlphaMask(
	IN gcoHARDWARE Hardware,
	IN gctINT Stage,
	IN gctBOOL ColorEnabled,
	IN gctBOOL AlphaEnabled
	);

/* Program the constant fragment color. */
gceSTATUS
gcoHARDWARE_SetFragmentColorX(
	IN gcoHARDWARE Hardware,
	IN gctFIXED_POINT Red,
	IN gctFIXED_POINT Green,
	IN gctFIXED_POINT Blue,
	IN gctFIXED_POINT Alpha
	);

gceSTATUS
gcoHARDWARE_SetFragmentColorF(
	IN gcoHARDWARE Hardware,
	IN gctFLOAT Red,
	IN gctFLOAT Green,
	IN gctFLOAT Blue,
	IN gctFLOAT Alpha
	);

/* Program the constant fog color. */
gceSTATUS
gcoHARDWARE_SetFogColorX(
	IN gcoHARDWARE Hardware,
	IN gctFIXED_POINT Red,
	IN gctFIXED_POINT Green,
	IN gctFIXED_POINT Blue,
	IN gctFIXED_POINT Alpha
	);

gceSTATUS
gcoHARDWARE_SetFogColorF(
	IN gcoHARDWARE Hardware,
	IN gctFLOAT Red,
	IN gctFLOAT Green,
	IN gctFLOAT Blue,
	IN gctFLOAT Alpha
	);

/* Program the constant texture color. */
gceSTATUS
gcoHARDWARE_SetTetxureColorX(
	IN gcoHARDWARE Hardware,
	IN gctINT Stage,
	IN gctFIXED_POINT Red,
	IN gctFIXED_POINT Green,
	IN gctFIXED_POINT Blue,
	IN gctFIXED_POINT Alpha
	);

gceSTATUS
gcoHARDWARE_SetTetxureColorF(
	IN gcoHARDWARE Hardware,
	IN gctINT Stage,
	IN gctFLOAT Red,
	IN gctFLOAT Green,
	IN gctFLOAT Blue,
	IN gctFLOAT Alpha
	);

/* Configure color texture function. */
gceSTATUS
gcoHARDWARE_SetColorTextureFunction(
	IN gcoHARDWARE Hardware,
	IN gctINT Stage,
	IN gceTEXTURE_FUNCTION Function,
	IN gceTEXTURE_SOURCE Source0,
	IN gceTEXTURE_CHANNEL Channel0,
	IN gceTEXTURE_SOURCE Source1,
	IN gceTEXTURE_CHANNEL Channel1,
	IN gceTEXTURE_SOURCE Source2,
	IN gceTEXTURE_CHANNEL Channel2,
	IN gctINT Scale
	);

/* Configure alpha texture function. */
gceSTATUS
gcoHARDWARE_SetAlphaTextureFunction(
	IN gcoHARDWARE Hardware,
	IN gctINT Stage,
	IN gceTEXTURE_FUNCTION Function,
	IN gceTEXTURE_SOURCE Source0,
	IN gceTEXTURE_CHANNEL Channel0,
	IN gceTEXTURE_SOURCE Source1,
	IN gceTEXTURE_CHANNEL Channel1,
	IN gceTEXTURE_SOURCE Source2,
	IN gceTEXTURE_CHANNEL Channel2,
	IN gctINT Scale
	);

/*----------------------------------------------------------------------------*/
/*------------------------------- gcoHARDWARE 2D ------------------------------*/

/* Translate API source color format to its hardware value. */
gceSTATUS
gcoHARDWARE_TranslateSourceFormat(
	IN gcoHARDWARE Hardware,
	IN gceSURF_FORMAT APIValue,
	OUT gctUINT32* HwValue,
	OUT gctUINT32* HwSwizzleValue,
	OUT gctUINT32* HwIsYUVValue
	);

/* Translate API destination color format to its hardware value. */
gceSTATUS
gcoHARDWARE_TranslateDestinationFormat(
    IN gcoHARDWARE Hardware,
	IN gceSURF_FORMAT APIValue,
	OUT gctUINT32* HwValue,
	OUT gctUINT32* HwSwizzleValue,
	OUT gctUINT32* HwIsYUVValue
	);

/* Translate API pattern color format to its hardware value. */
gceSTATUS
gcoHARDWARE_TranslatePatternFormat(
    IN gcoHARDWARE Hardware,
	IN gceSURF_FORMAT APIValue,
	OUT gctUINT32* HwValue,
	OUT gctUINT32* HwSwizzleValue,
	OUT gctUINT32* HwIsYUVValue
	);

/* Translate API transparency mode to its hardware value. */
gceSTATUS
gcoHARDWARE_TranslateTransparency(
	IN gceSURF_TRANSPARENCY APIValue,
	OUT gctUINT32* HwValue
	);

/* Translate SURF API transparency mode to PE 2.0 transparency values. */
gceSTATUS
gcoHARDWARE_TranslateSurfTransparency(
	IN gceSURF_TRANSPARENCY APIValue,
	OUT gctUINT32* srcTransparency,
	OUT gctUINT32* dstTransparency,
	OUT gctUINT32* patTransparency
	);

/* Translate API transparency mode to its PE 1.0 hardware value. */
gceSTATUS
gcoHARDWARE_TranslateTransparencies(
	IN gcoHARDWARE	Hardware,
	IN gctUINT32	srcTransparency,
	IN gctUINT32	dstTransparency,
	IN gctUINT32	patTransparency,
	OUT gctUINT32*  HwValue
	);

/* Translate API transparency mode to its hardware value. */
gceSTATUS
gcoHARDWARE_TranslateSourceTransparency(
	IN gce2D_TRANSPARENCY APIValue,
	OUT gctUINT32 * HwValue
	);

/* Translate API transparency mode to its hardware value. */
gceSTATUS
gcoHARDWARE_TranslateDestinationTransparency(
	IN gce2D_TRANSPARENCY APIValue,
	OUT gctUINT32 * HwValue
	);

/* Translate API transparency mode to its hardware value. */
gceSTATUS
gcoHARDWARE_TranslatePatternTransparency(
	IN gce2D_TRANSPARENCY APIValue,
	OUT gctUINT32 * HwValue
	);

/* Translate API YUV Color mode to its hardware value. */
gceSTATUS
gcoHARDWARE_TranslateYUVColorMode(
	IN	gce2D_YUV_COLOR_MODE APIValue,
	OUT gctUINT32 * HwValue
	);

/* Translate API pixel color multiply mode to its hardware value. */
gceSTATUS
gcoHARDWARE_PixelColorMultiplyMode(
	IN	gce2D_PIXEL_COLOR_MULTIPLY_MODE APIValue,
	OUT gctUINT32 * HwValue
	);

/* Translate API global color multiply mode to its hardware value. */
gceSTATUS
gcoHARDWARE_GlobalColorMultiplyMode(
	IN	gce2D_GLOBAL_COLOR_MULTIPLY_MODE APIValue,
	OUT gctUINT32 * HwValue
	);

/* Translate API mono packing mode to its hardware value. */
gceSTATUS
gcoHARDWARE_TranslateMonoPack(
	IN gceSURF_MONOPACK APIValue,
	OUT gctUINT32* HwValue
	);

/* Translate API 2D command to its hardware value. */
gceSTATUS
gcoHARDWARE_TranslateCommand(
	IN gce2D_COMMAND APIValue,
	OUT gctUINT32* HwValue
	);

/* Translate API per-pixel alpha mode to its hardware value. */
gceSTATUS
gcoHARDWARE_TranslatePixelAlphaMode(
	IN gceSURF_PIXEL_ALPHA_MODE APIValue,
	OUT gctUINT32* HwValue
	);

/* Translate API global alpha mode to its hardware value. */
gceSTATUS
gcoHARDWARE_TranslateGlobalAlphaMode(
	IN gceSURF_GLOBAL_ALPHA_MODE APIValue,
	OUT gctUINT32* HwValue
	);

/* Translate API per-pixel color mode to its hardware value. */
gceSTATUS
gcoHARDWARE_TranslatePixelColorMode(
	IN gceSURF_PIXEL_COLOR_MODE APIValue,
	OUT gctUINT32* HwValue
	);

/* Translate API alpha factor mode to its hardware value. */
gceSTATUS
gcoHARDWARE_TranslateAlphaFactorMode(
	IN	gcoHARDWARE Hardware,
	IN	gceSURF_BLEND_FACTOR_MODE APIValue,
	OUT gctUINT32_PTR HwValue
	);

/* Configure monochrome source. */
gceSTATUS
gcoHARDWARE_SetMonochromeSource(
	IN gcoHARDWARE Hardware,
	IN gctUINT8 MonoTransparency,
	IN gceSURF_MONOPACK DataPack,
	IN gctBOOL CoordRelative,
	IN gctUINT32 FgColor32,
	IN gctUINT32 BgColor32
	);

/* Configure color source. */
gceSTATUS
gcoHARDWARE_SetColorSource(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_INFO_PTR Surface,
	IN gctBOOL CoordRelative
	);

/* Configure masked color source. */
gceSTATUS
gcoHARDWARE_SetMaskedSource(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_INFO_PTR Surface,
	IN gctBOOL CoordRelative,
	IN gceSURF_MONOPACK MaskPack
	);

/* Setup the source rectangle. */
gceSTATUS
gcoHARDWARE_SetSource(
	IN gcoHARDWARE Hardware,
	IN gcsRECT_PTR SrcRect
	);

/* Setup the fraction of the source origin for filter blit. */
gceSTATUS
gcoHARDWARE_SetOriginFraction(
	IN gcoHARDWARE Hardware,
	IN gctUINT16 HorFraction,
	IN gctUINT16 VerFraction
	);

/* Load 256-entry color table for INDEX8 source surfaces. */
gceSTATUS
gcoHARDWARE_LoadPalette(
	IN gcoHARDWARE Hardware,
	IN gctUINT FirstIndex,
	IN gctUINT IndexCount,
	IN gctPOINTER ColorTable,
	IN gctBOOL ColorConvert
	);

/* Setup the source pixel swizzle. */
gceSTATUS
gcoHARDWARE_SetSourceSwizzle(
	IN gcoHARDWARE Hardware,
	IN gceSURF_SWIZZLE Swizzle
	);

/* Setup the source pixel UV swizzle. */
gceSTATUS
gcoHARDWARE_SetSourceSwizzleUV(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 SwizzleUV
	);

/* Setup the source global color value in ARGB8 format. */
gceSTATUS
gcoHARDWARE_SetSourceGlobalColor(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Color
	);

/* Setup the target global color value in ARGB8 format. */
gceSTATUS
gcoHARDWARE_SetTargetGlobalColor(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Color
	);

/* Setup the source and target pixel multiply modes. */
gceSTATUS
gcoHARDWARE_SetMultiplyModes(
	IN gcoHARDWARE Hardware,
	IN gce2D_PIXEL_COLOR_MULTIPLY_MODE SrcPremultiplySrcAlpha,
	IN gce2D_PIXEL_COLOR_MULTIPLY_MODE DstPremultiplyDstAlpha,
	IN gce2D_GLOBAL_COLOR_MULTIPLY_MODE SrcPremultiplyGlobalMode,
	IN gce2D_PIXEL_COLOR_MULTIPLY_MODE DstDemultiplyDstAlpha
	);

/* Setup the source, target and pattern transparency modes. */
gceSTATUS
gcoHARDWARE_SetTransparencyModes(
	IN gcoHARDWARE Hardware,
	IN gce2D_TRANSPARENCY SrcTransparency,
	IN gce2D_TRANSPARENCY DstTransparency,
	IN gce2D_TRANSPARENCY PatTransparency
	);

/* Setup the source, target and pattern transparency modes.
   Used only for have backward compatibility.
*/
gceSTATUS
gcoHARDWARE_SetAutoTransparency(
	IN gcoHARDWARE Hardware,
	IN gctUINT8 FgRop,
	IN gctUINT8 BgRop
	);

/* Setup the source color key value in ARGB8 format. */
gceSTATUS
gcoHARDWARE_SetSourceColorKeyRange(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 ColorLow,
	IN gctUINT32 ColorHigh,
	IN gctBOOL ColorPack
	);

/* Setup the YUV color space mode. */
gceSTATUS gcoHARDWARE_YUVColorMode(
	IN gcoHARDWARE Hardware,
	IN gce2D_YUV_COLOR_MODE  Mode
	);

/* Save mono colors for later programming. */
gceSTATUS gcoHARDWARE_SaveMonoColors(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 FgColor,
	IN gctUINT32 BgColor
	);

/* Save transparency color for later programming. */
gceSTATUS gcoHARDWARE_SaveTransparencyColor(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Color32
	);

/* Set clipping rectangle. */
gceSTATUS
gcoHARDWARE_SetClipping(
	IN gcoHARDWARE Hardware,
	IN gcsRECT_PTR Rect
	);

/* Configure destination. */
gceSTATUS
gcoHARDWARE_SetTarget(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_INFO_PTR Surface
	);

/* Set the target color format. */
gceSTATUS
gcoHARDWARE_SetTargetFormat(
	IN gcoHARDWARE Hardware,
	IN gceSURF_FORMAT Format
	);

/* Setup the destination color key value in ARGB8 format. */
gceSTATUS
gcoHARDWARE_SetTargetColorKeyRange(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 ColorLow,
	IN gctUINT32 ColorHigh
	);

/* Load solid (single) color pattern. */
gceSTATUS
gcoHARDWARE_LoadSolidColorPattern(
	IN gcoHARDWARE Hardware,
	IN gctBOOL ColorConvert,
	IN gctUINT32 Color,
	IN gctUINT64 Mask
	);

/* Load monochrome pattern. */
gceSTATUS
gcoHARDWARE_LoadMonochromePattern(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 OriginX,
	IN gctUINT32 OriginY,
	IN gctBOOL ColorConvert,
	IN gctUINT32 FgColor,
	IN gctUINT32 BgColor,
	IN gctUINT64 Bits,
	IN gctUINT64 Mask
	);

/* Load color pattern. */
gceSTATUS
gcoHARDWARE_LoadColorPattern(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 OriginX,
	IN gctUINT32 OriginY,
	IN gctUINT32 Address,
	IN gceSURF_FORMAT Format,
	IN gctUINT64 Mask
	);

/* Calculate stretch factor. */
gctUINT32
gcoHARDWARE_GetStretchFactor(
	IN gctINT32 SrcSize,
	IN gctINT32 DestSize
	);

/* Calculate the stretch factors. */
gceSTATUS
gcoHARDWARE_GetStretchFactors(
	IN gcsRECT_PTR SrcRect,
	IN gcsRECT_PTR DestRect,
	OUT gctUINT32 * HorFactor,
	OUT gctUINT32 * VerFactor
	);

/* Calculate and program the stretch factors. */
gceSTATUS
gcoHARDWARE_SetStretchFactors(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 HorFactor,
	IN gctUINT32 VerFactor
	);

/* Determines the usage of 2D resources (source/pattern/destination). */
void
gcoHARDWARE_Get2DResourceUsage(
	IN gctUINT8 FgRop,
	IN gctUINT8 BgRop,
	IN gctUINT32 Transparency,
	OUT gctBOOL_PTR UseSource,
	OUT gctBOOL_PTR UsePattern,
	OUT gctBOOL_PTR UseDestination
	);

/* Set 2D clear color in A8R8G8B8 format. */
gceSTATUS
gcoHARDWARE_Set2DClearColor(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Color,
	IN gctBOOL ColorConvert
	);

/* Enable/disable 2D BitBlt mirrorring. */
gceSTATUS
gcoHARDWARE_SetBitBlitMirror(
	IN gcoHARDWARE Hardware,
	IN gctBOOL HorizontalMirror,
	IN gctBOOL VerticalMirror
	);

/* Start a DE command. */
gceSTATUS
gcoHARDWARE_StartDE(
	IN gcoHARDWARE Hardware,
	IN gce2D_COMMAND Command,
	IN gctUINT32 SrcRectCount,
	IN gcsRECT_PTR SrcRect,
	IN gctUINT32 DestRectCount,
	IN gcsRECT_PTR DestRect,
	IN gctUINT32 FgRop,
	IN gctUINT32 BgRop
	);

/* Start a DE command to draw one or more Lines,
   with a common or individual color. */
gceSTATUS
gcoHARDWARE_StartDELine(
	IN gcoHARDWARE Hardware,
	IN gce2D_COMMAND Command,
	IN gctUINT32 RectCount,
	IN gcsRECT_PTR DestRect,
	IN gctUINT32 ColorCount,
	IN gctUINT32_PTR Color32,
	IN gctUINT32 FgRop,
	IN gctUINT32 BgRop
	);

/* Start a DE command with a monochrome stream. */
gceSTATUS
gcoHARDWARE_StartDEStream(
	IN gcoHARDWARE Hardware,
	IN gcsRECT_PTR DestRect,
	IN gctUINT32 FgRop,
	IN gctUINT32 BgRop,
	IN gctUINT32 StreamSize,
	OUT gctPOINTER * StreamBits
	);

/* Set kernel size. */
gceSTATUS
gcoHARDWARE_SetKernelSize(
	IN gcoHARDWARE Hardware,
	IN gctUINT8 HorKernelSize,
	IN gctUINT8 VerKernelSize
	);

/* Set filter type. */
gceSTATUS
gcoHARDWARE_SetFilterType(
	IN gcoHARDWARE Hardware,
	IN gceFILTER_TYPE FilterType
	);

/* Set the filter kernel array by user. */
gceSTATUS gcoHARDWARE_SetUserFilterKernel(
	IN gcoHARDWARE Hardware,
	IN gceFILTER_PASS_TYPE PassType,
	IN gctUINT16_PTR KernelArray
	);

/* Select the pass(es) to be done for user defined filter. */
gceSTATUS gcoHARDWARE_EnableUserFilterPasses(
	IN gcoHARDWARE Hardware,
	IN gctBOOL HorPass,
	IN gctBOOL VerPass
	);

/* Frees the kernel weight array. */
gceSTATUS
gcoHARDWARE_FreeKernelArray(
	IN gcoHARDWARE Hardware
	);

/* Frees the temporary buffer allocated by filter blit operation. */
gceSTATUS
gcoHARDWARE_FreeFilterBuffer(
	IN gcoHARDWARE Hardware
	);

/* Filter blit. */
gceSTATUS
gcoHARDWARE_FilterBlit(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_INFO_PTR SrcSurface,
	IN gcsSURF_INFO_PTR DestSurface,
	IN gcsRECT_PTR SrcRect,
	IN gcsRECT_PTR DestRect,
	IN gcsRECT_PTR DestSubRect
	);

/* Enable alpha blending engine in the hardware and disengage the ROP engine. */
gceSTATUS
gcoHARDWARE_EnableAlphaBlend(
	IN gcoHARDWARE Hardware,
	IN gceSURF_PIXEL_ALPHA_MODE SrcAlphaMode,
	IN gceSURF_PIXEL_ALPHA_MODE DstAlphaMode,
	IN gceSURF_GLOBAL_ALPHA_MODE SrcGlobalAlphaMode,
	IN gceSURF_GLOBAL_ALPHA_MODE DstGlobalAlphaMode,
	IN gceSURF_BLEND_FACTOR_MODE SrcFactorMode,
	IN gceSURF_BLEND_FACTOR_MODE DstFactorMode,
	IN gceSURF_PIXEL_COLOR_MODE SrcColorMode,
	IN gceSURF_PIXEL_COLOR_MODE DstColorMode
	);

/* Disable alpha blending engine in the hardware and engage the ROP engine. */
gceSTATUS
gcoHARDWARE_DisableAlphaBlend(
	IN gcoHARDWARE Hardware
	);

/* Set the GPU clock cycles, after which the idle 2D engine
   will trigger a flush. */
gceSTATUS
gcoHARDWARE_SetAutoFlushCycles(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Cycles
	);

gceSTATUS
gcoHARDWARE_ColorConvertToARGB8(
	IN gceSURF_FORMAT Format,
	IN gctUINT32 NumColors,
	IN gctUINT32_PTR Color,
	OUT gctUINT32_PTR Color32
	);

gceSTATUS
gcoHARDWARE_ColorConvertFromARGB8(
	IN gceSURF_FORMAT Format,
	IN gctUINT32 NumColors,
	IN gctUINT32_PTR Color32,
	OUT gctUINT32_PTR Color
	);

gceSTATUS
gcoHARDWARE_ColorPackFromARGB8(
	IN gceSURF_FORMAT Format,
	IN gctUINT32 Color32,
	OUT gctUINT32_PTR Color
	);

#ifndef VIVANTE_NO_3D
/*----------------------------------------------------------------------------*/
/*------------------------------- gcoHARDWARE 3D ------------------------------*/

/* Query if a surface is renderable or not. */
gceSTATUS
gcoHARDWARE_IsSurfaceRenderable(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_INFO_PTR Surface
	);

/* Initialize the 3D hardware. */
gceSTATUS
gcoHARDWARE_Initialize3D(
	IN gcoHARDWARE Hardware
	);

/* Query the OpenGL ES 2.0 capabilities. */
gceSTATUS
gcoHARDWARE_QueryOpenGL2(
	OUT gctBOOL * OpenGL2
	);

/* Query the stream capabilities. */
gceSTATUS
gcoHARDWARE_QueryStreamCaps(
	IN gcoHARDWARE Hardware,
	OUT gctUINT * MaxAttributes,
	OUT gctUINT * MaxStreamSize,
	OUT gctUINT * NumberOfStreams,
	OUT gctUINT * Alignment
	);

/* Flush the evrtex caches. */
gceSTATUS
gcoHARDWARE_FlushVertex(
	IN gcoHARDWARE Hardware
	);

/* Flush the evrtex caches. */
gceSTATUS
gcoHARDWARE_FlushL2Cache(
	IN gcoHARDWARE Hardware
	);

/* Query the index capabilities. */
gceSTATUS
gcoHARDWARE_QueryIndexCaps(
	OUT gctBOOL * Index8,
	OUT gctBOOL * Index16,
	OUT gctBOOL * Index32,
	OUT gctUINT * MaxIndex
	);

/* Query the target capabilities. */
gceSTATUS
gcoHARDWARE_QueryTargetCaps(
	IN gcoHARDWARE Hardware,
	OUT gctUINT * MaxWidth,
	OUT gctUINT * MaxHeight,
	OUT gctUINT * MultiTargetCount,
	OUT gctUINT * MaxSamples
	);

/* Query the texture capabilities. */
gceSTATUS
gcoHARDWARE_QueryTextureCaps(
	OUT gctUINT * MaxWidth,
	OUT gctUINT * MaxHeight,
	OUT gctUINT * MaxDepth,
	OUT gctBOOL * Cubic,
	OUT gctBOOL * NonPowerOfTwo,
	OUT gctUINT * VertexSamplers,
	OUT gctUINT * PixelSamplers
	);

/* Query the shader support. */
gceSTATUS
gcoHARDWARE_QueryShaderCaps(
	OUT gctUINT * VertexUniforms,
	OUT gctUINT * FragmentUniforms,
	OUT gctUINT * Varyings
	);

gceSTATUS
gcoHARDWARE_GetClosestTextureFormat(
	IN gcoHARDWARE Hardware,
	IN gceSURF_FORMAT InFormat,
	OUT gceSURF_FORMAT* OutFormat
	);

/* Query the texture mipmap support. */
gceSTATUS
gcoHARDWARE_QueryTexture_MipMap(
	IN gctUINT Width,
	IN gctUINT Height
	);

/* Query the texture support. */
gceSTATUS
gcoHARDWARE_QueryTexture(
	IN gceSURF_FORMAT Format,
	IN gctUINT Level,
	IN gctUINT Width,
	IN gctUINT Height,
	IN gctUINT Depth,
	IN gctUINT Faces,
	OUT gctUINT * WidthAlignment,
	OUT gctUINT * HeightAlignment,
	OUT gctSIZE_T * SliceSize
	);

/* Upload data into a texture. */
gceSTATUS
gcoHARDWARE_UploadTexture(
	IN gcoHARDWARE Hardware,
	IN gceSURF_FORMAT TargetFormat,
	IN gctUINT32 Address,
	IN gctPOINTER Logical,
	IN gctUINT32 Offset,
	IN gctINT TargetStride,
	IN gctUINT X,
	IN gctUINT Y,
	IN gctUINT Width,
	IN gctUINT Height,
	IN gctCONST_POINTER Memory,
	IN gctINT SourceStride,
	IN gceSURF_FORMAT SourceFormat
	);

/* Flush the texture cache. */
gceSTATUS
gcoHARDWARE_FlushTexture(
	IN gcoHARDWARE Hardware
	);

/* Set the texture addressing mode. */
gceSTATUS
gcoHARDWARE_SetTextureAddressingMode(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gceTEXTURE_WHICH Which,
	IN gceTEXTURE_ADDRESSING Mode
	);

/* Set the unsigned integer texture border color. */
gceSTATUS
gcoHARDWARE_SetTextureBorderColor(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gctUINT Red,
	IN gctUINT Green,
	IN gctUINT Blue,
	IN gctUINT Alpha
	);

/* Set the fixed point texture border color. */
gceSTATUS
gcoHARDWARE_SetTextureBorderColorX(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gctFIXED_POINT Red,
	IN gctFIXED_POINT Green,
	IN gctFIXED_POINT Blue,
	IN gctFIXED_POINT Alpha
	);

/* Set the floating point texture border color. */
gceSTATUS
gcoHARDWARE_SetTextureBorderColorF(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gctFLOAT Red,
	IN gctFLOAT Green,
	IN gctFLOAT Blue,
	IN gctFLOAT Alpha
	);

/* Set the texture minification filter. */
gceSTATUS
gcoHARDWARE_SetTextureMinFilter(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gceTEXTURE_FILTER Filter
	);

/* Set the texture magnification filter. */
gceSTATUS
gcoHARDWARE_SetTextureMagFilter(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gceTEXTURE_FILTER Filter
	);

/* Set the texture mip map filter. */
gceSTATUS
gcoHARDWARE_SetTextureMipFilter(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gceTEXTURE_FILTER Filter
	);

/*Set RoundUV, add 1./64. to UV for Nearest sample*/
gceSTATUS
gcoHARDWARE_SetTextureRoundUV(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gctINT RoundEnable
	);

/* Set the fixed point bias for the level of detail. */
gceSTATUS
gcoHARDWARE_SetTextureLODBiasX(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gctFIXED_POINT Bias
	);

/* Set the floating point bias for the level of detail. */
gceSTATUS
gcoHARDWARE_SetTextureLODBiasF(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gctFLOAT Bias
	);

/* Set the fixed point minimum value for the level of detail. */
gceSTATUS
gcoHARDWARE_SetTextureLODMinX(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gctFIXED_POINT LevelOfDetail
	);

/* Set the floating point minimum value for the level of detail. */
gceSTATUS
gcoHARDWARE_SetTextureLODMinF(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gctFLOAT LevelOfDetail
	);

/* Set the fixed point maximum value for the level of detail. */
gceSTATUS
gcoHARDWARE_SetTextureLODMaxX(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gctFIXED_POINT LevelOfDetail
	);

/* Set the floating point maximum value for the level of detail. */
gceSTATUS
gcoHARDWARE_SetTextureLODMaxF(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gctFLOAT LevelOfDetail
	);

/* Set texture format. */
gceSTATUS
gcoHARDWARE_SetTextureFormat(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gceSURF_FORMAT Format,
	IN gceENDIAN_HINT EndianHint,
	IN gctUINT Width,
	IN gctUINT Height,
	IN gctUINT Depth,
	IN gctUINT Faces
	);

/* Set texture LOD address. */
gceSTATUS
gcoHARDWARE_SetTextureLOD(
	IN gcoHARDWARE Hardware,
	IN gctINT Sampler,
	IN gctINT LevelOfDetail,
	IN gctUINT32 Address,
	IN gctINT Stride
	);

/* Clear wrapper to distinguish between software and resolve(3d) clear cases. */
gceSTATUS
gcoHARDWARE_ClearRect(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Address,
	IN gctPOINTER LogicalAddress,
	IN gctUINT32 Stride,
	IN gctINT Left,
	IN gctINT Top,
	IN gctINT Right,
	IN gctINT Bottom,
	IN gceSURF_FORMAT Format,
	IN gctUINT32 ClearValue,
	IN gctUINT8 ClearMask
	);

/* Append a TILE STATUS CLEAR command to a command queue. */
gceSTATUS
gcoHARDWARE_ClearTileStatus(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_INFO_PTR Surface,
	IN gctUINT32 Address,
	IN gctSIZE_T Bytes,
	IN gceSURF_TYPE Type,
	IN gctUINT32 ClearValue,
	IN gctUINT8 ClearMask
	);

gceSTATUS
gco3D_ClearHzTileStatus(
	IN gco3D Engine,
	IN gcsSURF_INFO_PTR Surface,
	IN gcsSURF_NODE_PTR TileStatus
	);

gceSTATUS
gcoHARDWARE_SetRenderTarget(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_INFO_PTR Surface
	);

gceSTATUS
gcoHARDWARE_SetDepthBuffer(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_INFO_PTR Surface
	);

gceSTATUS
gcoHARDWARE_SetAPI(
	IN gcoHARDWARE Hardware,
	IN gceAPI Api
	);

gceSTATUS
gcoHARDWARE_SetViewport(
	IN gcoHARDWARE Hardware,
	IN gctINT32 Left,
	IN gctINT32 Top,
	IN gctINT32 Right,
	IN gctINT32 Bottom
	);

gceSTATUS
gcoHARDWARE_SetScissors(
	IN gcoHARDWARE Hardware,
	IN gctINT32 Left,
	IN gctINT32 Top,
	IN gctINT32 Right,
	IN gctINT32 Bottom
	);

gceSTATUS
gcoHARDWARE_SetShading(
	IN gcoHARDWARE Hardware,
	IN gceSHADING Shading
	);

gceSTATUS
gcoHARDWARE_SetBlendEnable(
	IN gcoHARDWARE Hardware,
	IN gctBOOL Enabled
	);

gceSTATUS
gcoHARDWARE_SetBlendFunctionSource(
	IN gcoHARDWARE Hardware,
	IN gceBLEND_FUNCTION FunctionRGB,
	IN gceBLEND_FUNCTION FunctionAlpha
	);

gceSTATUS
gcoHARDWARE_SetBlendFunctionTarget(
	IN gcoHARDWARE Hardware,
	IN gceBLEND_FUNCTION FunctionRGB,
	IN gceBLEND_FUNCTION FunctionAlpha
	);

gceSTATUS
gcoHARDWARE_SetBlendMode(
	IN gcoHARDWARE Hardware,
	IN gceBLEND_MODE ModeRGB,
	IN gceBLEND_MODE ModeAlpha
	);

gceSTATUS
gcoHARDWARE_SetBlendColor(
	IN gcoHARDWARE Hardware,
	IN gctUINT8 Red,
	IN gctUINT8 Green,
	IN gctUINT8 Blue,
	IN gctUINT8 Alpha
	);

gceSTATUS
gcoHARDWARE_SetBlendColorX(
	IN gcoHARDWARE Hardware,
	IN gctFIXED_POINT Red,
	IN gctFIXED_POINT Green,
	IN gctFIXED_POINT Blue,
	IN gctFIXED_POINT Alpha
	);

gceSTATUS
gcoHARDWARE_SetBlendColorF(
	IN gcoHARDWARE Hardware,
	IN gctFLOAT Red,
	IN gctFLOAT Green,
	IN gctFLOAT Blue,
	IN gctFLOAT Alpha
	);

gceSTATUS
gcoHARDWARE_SetCulling(
	IN gcoHARDWARE Hardware,
	IN gceCULL Mode
	);

gceSTATUS
gcoHARDWARE_SetPointSizeEnable(
	IN gcoHARDWARE Hardware,
	IN gctBOOL Enable
	);

gceSTATUS
gcoHARDWARE_SetPointSprite(
	IN gcoHARDWARE Hardware,
	IN gctBOOL Enable
	);

gceSTATUS
gcoHARDWARE_SetFill(
	IN gcoHARDWARE Hardware,
	IN gceFILL Mode
	);

gceSTATUS
gcoHARDWARE_SetDepthCompare(
	IN gcoHARDWARE Hardware,
	IN gceCOMPARE DepthCompare
	);

gceSTATUS
gcoHARDWARE_SetDepthWrite(
	IN gcoHARDWARE Hardware,
	IN gctBOOL Enable
	);

gceSTATUS
gcoHARDWARE_SetDepthMode(
	IN gcoHARDWARE Hardware,
	IN gceDEPTH_MODE DepthMode
	);

gceSTATUS
gcoHARDWARE_SetDepthRangeX(
	IN gcoHARDWARE Hardware,
	IN gceDEPTH_MODE DepthMode,
	IN gctFIXED_POINT Near,
	IN gctFIXED_POINT Far
	);

gceSTATUS
gcoHARDWARE_SetDepthRangeF(
	IN gcoHARDWARE Hardware,
	IN gceDEPTH_MODE DepthMode,
	IN gctFLOAT Near,
	IN gctFLOAT Far
	);

gceSTATUS
gcoHARDWARE_SetDepthScaleBiasX(
	IN gcoHARDWARE Hardware,
	IN gctFIXED_POINT DepthScale,
	IN gctFIXED_POINT DepthBias
	);

gceSTATUS
gcoHARDWARE_SetDepthScaleBiasF(
	IN gcoHARDWARE Hardware,
	IN gctFLOAT DepthScale,
	IN gctFLOAT DepthBias
	);

gceSTATUS
gcoHARDWARE_SetLastPixelEnable(
	IN gcoHARDWARE Hardware,
	IN gctBOOL Enable
	);

gceSTATUS
gcoHARDWARE_SetDither(
	IN gcoHARDWARE Hardware,
	IN gctBOOL Enable
	);

gceSTATUS
gcoHARDWARE_SetColorWrite(
	IN gcoHARDWARE Hardware,
	IN gctUINT8 Enable
	);

gceSTATUS
gcoHARDWARE_SetEarlyDepth(
	IN gcoHARDWARE Hardware,
	IN gctBOOL Enable
	);

gceSTATUS
gcoHARDWARE_SetStencilMode(
	IN gcoHARDWARE Hardware,
	IN gceSTENCIL_MODE Mode
	);

gceSTATUS
gcoHARDWARE_SetStencilMask(
	IN gcoHARDWARE Hardware,
	IN gctUINT8 Mask
	);

gceSTATUS
gcoHARDWARE_SetStencilWriteMask(
	IN gcoHARDWARE Hardware,
	IN gctUINT8 Mask
	);

gceSTATUS
gcoHARDWARE_SetStencilReference(
	IN gcoHARDWARE Hardware,
	IN gctUINT8 Reference
	);

gceSTATUS
gcoHARDWARE_SetStencilCompare(
	IN gcoHARDWARE Hardware,
	IN gceSTENCIL_WHERE Where,
	IN gceCOMPARE Compare
	);

gceSTATUS
gcoHARDWARE_SetStencilPass(
	IN gcoHARDWARE Hardware,
	IN gceSTENCIL_WHERE Where,
	IN gceSTENCIL_OPERATION Operation
	);

gceSTATUS
gcoHARDWARE_SetStencilFail(
	IN gcoHARDWARE Hardware,
	IN gceSTENCIL_WHERE Where,
	IN gceSTENCIL_OPERATION Operation
	);

gceSTATUS
gcoHARDWARE_SetStencilDepthFail(
	IN gcoHARDWARE Hardware,
	IN gceSTENCIL_WHERE Where,
	IN gceSTENCIL_OPERATION Operation
	);

gceSTATUS
gcoHARDWARE_SetAlphaTest(
	IN gcoHARDWARE Hardware,
	IN gctBOOL Enable
	);

gceSTATUS
gcoHARDWARE_SetAlphaCompare(
	IN gcoHARDWARE Hardware,
	IN gceCOMPARE Compare
	);

gceSTATUS
gcoHARDWARE_SetAlphaReference(
	IN gcoHARDWARE Hardware,
	IN gctUINT8 Reference
	);

gceSTATUS
gcoHARDWARE_SetAlphaReferenceX(
	IN gcoHARDWARE Hardware,
	IN gctFIXED_POINT Reference
	);

gceSTATUS
gcoHARDWARE_SetAlphaReferenceF(
	IN gcoHARDWARE Hardware,
	IN gctFLOAT Reference
	);

gceSTATUS
gcoHARDWARE_BindStream(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Address,
	IN gctINT Stride,
	IN gctUINT Number
	);

gceSTATUS
gcoHARDWARE_BindIndex(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Address,
	IN gceINDEX_TYPE IndexType
	);

gceSTATUS
gcoHARDWARE_SetAntiAliasLine(
	IN gcoHARDWARE Hardware,
	IN gctBOOL Enable
	);

gceSTATUS
gcoHARDWARE_SetAALineTexSlot(
	IN gcoHARDWARE Hardware,
	IN gctUINT TexSlot
	);

gceSTATUS
gcoHARDWARE_SetAALineWidth(
	IN gcoHARDWARE Hardware,
	IN gctFLOAT Width
	);


/* Draw a number of primitives. */
gceSTATUS
gcoHARDWARE_DrawPrimitives(
	IN gcoHARDWARE Hardware,
	IN gcePRIMITIVE Type,
	IN gctINT StartVertex,
	IN gctSIZE_T PrimitiveCount
	);

/* Draw a number of primitives using offsets. */
gceSTATUS
gcoHARDWARE_DrawPrimitivesOffset(
	IN gcoHARDWARE Hardware,
	IN gcePRIMITIVE Type,
	IN gctINT32 StartOffset,
	IN gctSIZE_T PrimitiveCount
	);

/* Draw a number of indexed primitives. */
gceSTATUS
gcoHARDWARE_DrawIndexedPrimitives(
	IN gcoHARDWARE Hardware,
	IN gcePRIMITIVE Type,
	IN gctINT BaseVertex,
	IN gctINT StartIndex,
	IN gctSIZE_T PrimitiveCount
	);

/* Draw a number of indexed primitives using offsets. */
gceSTATUS
gcoHARDWARE_DrawIndexedPrimitivesOffset(
	IN gcoHARDWARE Hardware,
	IN gcePRIMITIVE Type,
	IN gctINT32 BaseOffset,
	IN gctINT32 StartOffset,
	IN gctSIZE_T PrimitiveCount
	);

/* Copy data into video memory. */
gceSTATUS
gcoHARDWARE_CopyData(
	IN gcoHARDWARE Hardware,
	IN gcsSURF_NODE_PTR Memory,
	IN gctUINT32 Offset,
	IN gctCONST_POINTER Buffer,
	IN gctSIZE_T Bytes
	);

gceSTATUS
gcoHARDWARE_SetStream(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Index,
	IN gctUINT32 Address,
	IN gctUINT32 Stride
	);

gceSTATUS
gcoHARDWARE_SetAttributes(
	IN gcoHARDWARE Hardware,
	IN gcsVERTEX_ATTRIBUTES_PTR Attributes,
	IN gctUINT32 AttributeCount
	);

gceSTATUS
gcoHARDWARE_QuerySamplerBase(
	IN gcoHARDWARE Hardware,
	OUT gctSIZE_T * VertexCount,
	OUT gctINT_PTR VertexBase,
	OUT gctSIZE_T * FragmentCount,
	OUT gctINT_PTR FragmentBase
	);

gceSTATUS
gcoHARDWARE_SetDepthOnly(
	IN gcoHARDWARE Hardware,
	IN gctBOOL Enable
	);

gceSTATUS
gcoHARDWARE_SetCentroids(
	IN gcoHARDWARE Hardware,
	IN gctUINT32	Index,
	IN gctPOINTER	Centroids
	);
#endif /* VIVANTE_NO_3D */

/* Append a CLEAR command to a command queue. */
gceSTATUS
gcoHARDWARE_Clear(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 Address,
	IN gctUINT32 Stride,
	IN gctINT32 Left,
	IN gctINT32 Top,
	IN gctINT32 Right,
	IN gctINT32 Bottom,
	IN gceSURF_FORMAT Format,
	IN gctUINT32 ClearValue,
	IN gctUINT8 ClearMask
	);

/* Software clear. */
gceSTATUS
gcoHARDWARE_ClearSoftware(
	IN gcoHARDWARE Hardware,
	IN gctPOINTER LogicalAddress,
	IN gctUINT32 Stride,
	IN gctINT32 Left,
	IN gctINT32 Top,
	IN gctINT32 Right,
	IN gctINT32 Bottom,
	IN gceSURF_FORMAT Format,
	IN gctUINT32 ClearValue,
	IN gctUINT8 ClearMask
	);

/* Verifies whether 2D engine is available. */
gceSTATUS
gcoHARDWARE_Is2DAvailable(
    IN gcoHARDWARE Hardware
    );

/* Sets the software 2D renderer force flag. */
gceSTATUS
gcoHARDWARE_UseSoftware2D(
    IN gcoHARDWARE Hardware,
	IN gctBOOL Enable
    );

/* Sets the maximum number of brushes in the cache. */
gceSTATUS
gcoHARDWARE_SetBrushLimit(
	IN gcoHARDWARE Hardware,
	IN gctUINT MaxCount
	);

/* Return a pointer to the brush cache. */
gceSTATUS
gcoHARDWARE_GetBrushCache(
	IN gcoHARDWARE Hardware,
	IN OUT gcoBRUSH_CACHE * BrushCache
	);

/* Program the brush. */
gceSTATUS
gcoHARDWARE_FlushBrush(
	IN gcoHARDWARE Hardware,
	IN gcoBRUSH Brush
	);

/* Clear one or more rectangular areas. */
gceSTATUS
gcoHARDWARE_Clear2D(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 RectCount,
	IN gcsRECT_PTR Rect,
	IN gctUINT32 Color,
	IN gctBOOL ColorConvert,
	IN gctUINT8 FgRop,
	IN gctUINT8 BgRop
	);

/* Draw one or more Bresenham lines using a brush. */
gceSTATUS
gcoHARDWARE_Line2D(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 LineCount,
	IN gcsRECT_PTR Position,
	IN gcoBRUSH Brush,
	IN gctUINT8 FgRop,
	IN gctUINT8 BgRop
	);

/* Draw one or more Bresenham lines using solid color(s). */
gceSTATUS
gcoHARDWARE_Line2DEx(
	IN gcoHARDWARE Hardware,
	IN gctUINT32 LineCount,
	IN gcsRECT_PTR Position,
	IN gctUINT32 ColorCount,
	IN gctUINT32_PTR Color32,
	IN gctUINT8 FgRop,
	IN gctUINT8 BgRop
	);

/* Monochrome blit. */
gceSTATUS
gcoHARDWARE_MonoBlit(
	IN gcoHARDWARE Hardware,
	IN gctPOINTER StreamBits,
	IN gcsPOINT_PTR StreamSize,
	IN gcsRECT_PTR StreamRect,
	IN gceSURF_MONOPACK SrcStreamPack,
	IN gceSURF_MONOPACK DestStreamPack,
	IN gcsRECT_PTR DestRect,
	IN gctUINT32 FgRop,
	IN gctUINT32 BgRop
	);

/* Get Hw status*/
gceSTATUS
gcoHARDWARE_GetHWStatus(
	IN gcoHARDWARE Hardware,
	OUT gctBOOL_PTR* Idle,
	OUT gctINT_PTR Count,
	OUT gctINT_PTR CurrentCmdIndex
	);

/******************************************************************************\
******************************** gcoBUFFER Object *******************************
\******************************************************************************/

/* Construct a new gcoBUFFER object. */
gceSTATUS
gcoBUFFER_Construct(
	IN gcoHAL Hal,
	IN gcoHARDWARE Hardware,
	IN gctSIZE_T MaxSize,
	OUT gcoBUFFER * Buffer
	);

/* Destroy an gcoBUFFER object. */
gceSTATUS
gcoBUFFER_Destroy(
	IN gcoBUFFER Buffer
	);

/* Reserve space in a command buffer. */
gceSTATUS
gcoBUFFER_Reserve(
	IN gcoBUFFER Buffer,
	IN gctSIZE_T Bytes,
	IN gctBOOL Aligned,
	IN gctBOOL_PTR AddressHints,
	OUT gctPOINTER * Memory
	);

/* Preserve space in a command buffer. */
gceSTATUS
gcoBUFFER_Preserve(
	IN gcoBUFFER Buffer,
	IN gctSIZE_T Bytes,
	IN gctBOOL Aligned
	);

/* Write data into the command buffer. */
gceSTATUS
gcoBUFFER_Write(
	IN gcoBUFFER Buffer,
	IN gctCONST_POINTER Data,
	IN gctSIZE_T Bytes,
	IN gctBOOL Aligned
	);

/* Write 32-bit data into the command buffer. */
gceSTATUS
gcoBUFFER_Write32(
	IN gcoBUFFER Buffer,
	IN gctUINT32 Data,
	IN gctBOOL Aligned
	);

/* Write 64-bit data into the command buffer. */
gceSTATUS
gcoBUFFER_Write64(
	IN gcoBUFFER Buffer,
	IN gctUINT64 Data,
	IN gctBOOL Aligned
	);

/* Commit the command buffer. */
gceSTATUS
gcoBUFFER_Commit(
	IN gcoBUFFER Buffer,
	IN gcoCONTEXT Context,
	IN gcoQUEUE Queue
	);

/*get the status of command buffer*/
gceSTATUS
gcoBUFFER_Status(
	IN    gcoBUFFER Buffer,
	OUT gctBOOL_PTR* Idle,
	OUT gctINT_PTR Count,
	OUT gctINT_PTR CurrentCmdIndex
	);
	
/******************************************************************************\
******************************** gcoCMDBUF Object *******************************
\******************************************************************************/

typedef struct _gcsCOMMAND_INFO	* gcsCOMMAND_INFO_PTR;

/* Construct a new gcoCMDBUF object. */
gceSTATUS
gcoCMDBUF_Construct(
	IN gcoOS Os,
	IN gcoHARDWARE Hardware,
	IN gctSIZE_T Bytes,
	IN gcsCOMMAND_INFO_PTR Info,
	OUT gcoCMDBUF * Buffer
	);

/* Destroy an gcoCMDBUF object. */
gceSTATUS
gcoCMDBUF_Destroy(
	IN gcoCMDBUF Buffer
	);

/******************************************************************************\
******************************* gcoCONTEXT Object *******************************
\******************************************************************************/

gceSTATUS
gcoCONTEXT_Construct(
	IN gcoOS Os,
	IN gcoHARDWARE Hardware,
	OUT gcoCONTEXT * Context
	);

gceSTATUS
gcoCONTEXT_Destroy(
	IN gcoCONTEXT Context
	);

gceSTATUS
gcoCONTEXT_Buffer(
	IN gcoCONTEXT Context,
	IN gctUINT32 Address,
	IN gctSIZE_T Count,
	IN gctUINT32_PTR Data,
	OUT gctBOOL_PTR Hints
	);

gceSTATUS
gcoCONTEXT_BufferX(
	IN gcoCONTEXT Context,
	IN gctUINT32 Address,
	IN gctSIZE_T Count,
	IN gctFIXED_POINT * Data
	);

gceSTATUS
gcoCONTEXT_PreCommit(
	IN OUT gcoCONTEXT Context
	);

gceSTATUS
gcoCONTEXT_PostCommit(
	IN OUT gcoCONTEXT Context
	);

#ifdef LINUX
#define PENDING_FREED_MEMORY_SIZE_LIMIT		(4 * 1024 * 1024)
#endif

#if MRVL_PRE_ALLOCATE_CTX_BUFFER

/******************************************************************************\
******************************** gcoCTXBUF Object *******************************
\******************************************************************************/

/* Construct a new gcoCTXBUF object. */
gceSTATUS
gcoCTXBUF_Construct(
	IN gcoOS Os,
	IN gcoHARDWARE Hardware,
	IN gctSIZE_T Bytes,
	OUT gcoCTXBUF * Buffer
	);

/* Destroy an gcoCTXBUF object. */
gceSTATUS
gcoCTXBUF_Destroy(
	IN gcoCTXBUF Buffer
	);

#endif

/******************************************************************************\
********************************* gcoHAL object *********************************
\******************************************************************************/

struct _gcoHAL
{
	/* The object. */
	gcsOBJECT				object;

	/* Context passed in during creation. */
	gctPOINTER				context;

	/* Pointer to an gcoOS object. */
	gcoOS					os;

	/* Pointer to an gcoHARDWARE object. */
	gcoHARDWARE				hardware;

	/* Pointer to the gco2D and gco3D objects. */
	gco2D					engine2D;
	gcoVG					engineVG;
	gco3D					engine3D;

	/* Pointer to te gcoDUMP object. */
	gcoDUMP					dump;

#if VIVANTE_PROFILER
    gcsPROFILER             profiler;
#endif

    /* Process handle */
    gctHANDLE               process;

    /* Pointer to version string */
    gctCONST_STRING         version;

#if MRVL_BENCH
	/* timer object for bench mark */
	gcoAPIBENCH	apiBench;
#endif

#if MRVL_OPTI_USE_RESERVE_MEMORY
    gctUINT32                   reserveMemorySwitchNum;
    gctBOOL                     bReserveMemoryPending;
	gcoSTREAM					reserveMemory[RESERVE_MEMORY_NUM];
	gctUINT32					reserveMemoryIndex;
	gctSIGNAL					reserveMemorySignals[RESERVE_MEMORY_NUM];
	gctUINT32				    reserveMemoryOffset;
    gctUINT32                   reserveMemorySize[RESERVE_MEMORY_NUM];
    gctUINT32                   streamAlign;
    gctUINT32                   indexAlign;
    gctBOOL                     DisableReserveMemory;
#endif
};


/******************************************************************************\
********************************* gcoSURF object ********************************
\******************************************************************************/

typedef struct _gcsSURF_NODE
{
	/* Surface memory pool. */
	gcePOOL					pool;

	/* Lock count for the surface. */
	gctINT					lockCount;

	/* If not zero, the node is locked in the kernel. */
	gctBOOL					lockedInKernel;

	/* Number of planes in the surface for planar format support. */
	gctUINT					count;

	/* Node valid flag for the surface pointers. */
	gctBOOL					valid;

	/* The physical addresses of the surface. */
	gctUINT32				physical;
	gctUINT32				physical2;
	gctUINT32				physical3;

	/* The logical addresses of the surface. */
	gctUINT8_PTR			logical;
	gctUINT8_PTR			logical2;
	gctUINT8_PTR			logical3;

	/* Linear size and filler for tile status. */
	gctSIZE_T				size;
	gctUINT32				filler;
	gctBOOL					firstLock;

	union _gcuSURF_NODE_LIST
	{
		/* Allocated through HAL. */
		struct _gcsMEM_NODE_NORMAL
		{
			gcuVIDMEM_NODE_PTR	node;
#ifdef __QNXNTO__
			gctUINT32			bytes;
#endif
		}
		normal;

		/* Wrapped around user-allocated surface (gcvPOOL_USER). */
		struct _gcsMEM_NODE_WRAPPED
		{
			gctBOOL				logicalMapped;
			gctPOINTER			mappingInfo;
		}
		wrapped;
	}
	u;
}
gcsSURF_NODE;

typedef struct _gcsSURF_INFO
{
	/* Type usage and format of surface. */
	gceSURF_TYPE			type;
	gceSURF_FORMAT			format;

	/* Surface size. */
	gcsRECT					rect;
	gctUINT					alignedWidth;
	gctUINT					alignedHeight;
	gctBOOL					is16Bit;

	/* Rotation flag. */
	gceSURF_ROTATION		rotation;
	gceORIENTATION			orientation;

	/* Surface stride and size. */
	gctUINT					stride;
	gctUINT					size;

	/* YUV planar surface parameters. */
	gctUINT					uOffset;
	gctUINT					vOffset;
	gctUINT					uStride;
	gctUINT					vStride;

	/* Video memory node for surface. */
	gcsSURF_NODE			node;

	/* Samples. */
	gcsSAMPLES				samples;
	gctBOOL					vaa;

	/* Tile status. */
	gctBOOL					tileStatusDisabled;
	gctBOOL					superTiled;
	gctUINT32				clearValue;

	/* Hierarchical Z buffer pointer. */
	gcsSURF_NODE			hzNode;

	/* face for resolve */
	gctUINT                 face;
	
}
gcsSURF_INFO;

struct _gcoSURF
{
	/* Object. */
	gcsOBJECT				object;

	/* Pointer to an gcoHAL object. */
	gcoHAL					hal;

	/* Surface information structure. */
	struct _gcsSURF_INFO	info;

	/* Depth of the surface in planes. */
	gctUINT					depth;

#ifndef VIVANTE_NO_3D
	gctBOOL					resolvable;
#endif /* VIVANTE_NO_3D */

    /* Video memory node for tile status. */
    gcsSURF_NODE			tileStatusNode;
	gcsSURF_NODE			hzTileStatusNode;

	/* Surface color type. */
	gceSURF_COLOR_TYPE      colorType;

	/* Automatic stride calculation. */
	gctBOOL					autoStride;

	/* User pointers for the surface wrapper. */
	gctPOINTER				logical;
	gctUINT32				physical;

	/* Reference count of surface. */
	gctINT32				referenceCount;
};

/******************************************************************************\
******************************** gcoQUEUE Object *******************************
\******************************************************************************/

/* Construct a new gcoQUEUE object. */
gceSTATUS
gcoQUEUE_Construct(
	IN gcoOS Os,
	OUT gcoQUEUE * Queue
	);

/* Destroy a gcoQUEUE object. */
gceSTATUS
gcoQUEUE_Destroy(
	IN gcoQUEUE Queue
	);

/* Append an event to a gcoQUEUE object. */
gceSTATUS
gcoQUEUE_AppendEvent(
	IN gcoQUEUE Queue,
	IN gcsHAL_INTERFACE * Interface
	);

/* Commit and event queue. */
gceSTATUS
gcoQUEUE_Commit(
	IN gcoQUEUE Queue
	);

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_user_h_ */
