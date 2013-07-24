/****************************************************************************
*
*    Copyright (C) 2005 - 2013 by Vivante Corp.
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


#ifndef __gc_hal_statistics_h_
#define __gc_hal_statistics_h_


#define VIV_STAT_ENABLE_STATISTICS              0

/*  Toal number of frames for which the frame time is accounted. We have storage
    to keep frame times for last this many frames.
*/
#define VIV_STAT_FRAME_BUFFER_SIZE              30

/*
    Total number of frames sampled for a mode. This means

    # of frames for HZ Current  : VIV_STAT_EARLY_Z_SAMPLE_FRAMES
    # of frames for HZ Switched : VIV_STAT_EARLY_Z_SAMPLE_FRAMES
  +
  --------------------------------------------------------
                                : (2 * VIV_STAT_EARLY_Z_SAMPLE_FRAMES) frames needed

    IMPORTANT: This total must be smaller than VIV_STAT_FRAME_BUFFER_SIZE
*/
#define VIV_STAT_EARLY_Z_SAMPLE_FRAMES          7
#define VIV_STAT_EARLY_Z_LATENCY_FRAMES         2

/* Multiplication factor for previous Hz off mode. Make it more than 1.0 to advertise HZ on.*/
#define VIV_STAT_EARLY_Z_FACTOR                 (1.05f)

/* Defines the statistical data keys monitored by the statistics module */
typedef enum _gceSTATISTICS
{
    gcvFRAME_FPS        =   1,
}
gceSTATISTICS;

/* HAL statistics information. */
typedef struct _gcsSTATISTICS_EARLYZ
{
    gctUINT                     switchBackCount;
    gctUINT                     nextCheckPoint;
    gctBOOL                     disabled;
}
gcsSTATISTICS_EARLYZ;


/* Defines the statistical data keys monitored by the statistics module */
typedef enum _gceSTATISTICS_Call
{
	gcvSTAT_ES11_GLDRAWELEMENTS      =   1,
}
gceSTATISTICS_Call;


/* HAL statistics information. */
typedef struct _gcsSTATISTICS
{
    gctUINT64                   frameTime[VIV_STAT_FRAME_BUFFER_SIZE];
    gctUINT64                   previousFrameTime;
    gctUINT                     frame;
    gcsSTATISTICS_EARLYZ        earlyZ;
	gctUINT						ES11_drawElementsCount;
	gctBOOL						applyRTestVAFix;
}
gcsSTATISTICS;


/* Add a frame based data into current statistics. */
void
gcfSTATISTICS_AddData(
    IN gceSTATISTICS Key,
    IN gctUINT Value
    );

/* Marks the frame end and triggers statistical calculations and decisions.*/
void
gcfSTATISTICS_MarkFrameEnd (
    void
    );

/* Sets whether the dynmaic HZ is disabled or not .*/
void
gcfSTATISTICS_DisableDynamicEarlyZ (
    IN gctBOOL Disabled
    );

/* Checks whether or not glDrawArray function call will be discarded */
gctBOOL
gcfSTATISTICS_DiscardCall(
	gceSTATISTICS_Call Function
	);


#endif /*__gc_hal_statistics_h_ */

