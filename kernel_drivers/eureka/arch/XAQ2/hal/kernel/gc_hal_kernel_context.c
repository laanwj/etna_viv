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






#include "gc_hal.h"
#include "gc_hal_kernel.h"
#include "gc_hal_kernel_context.h"
#include "gc_hal_kernel_buffer.h"

/******************************************************************************\
******************************** Debugging Macro *******************************
\******************************************************************************/

/* Zone used for header/footer. */
#define _GC_OBJ_ZONE    gcvZONE_HARDWARE


/******************************************************************************\
************************** Context State Buffer Helpers ************************
\******************************************************************************/

#define _STATE(reg)                                                            \
    _State(\
        Context, index, \
        reg ## _Address >> 2, \
        reg ## _ResetValue, \
        reg ## _Count, \
        gcvFALSE, gcvFALSE                                                     \
        )

#define _STATE_COUNT(reg, count)                                               \
    _State(\
        Context, index, \
        reg ## _Address >> 2, \
        reg ## _ResetValue, \
        count, \
        gcvFALSE, gcvFALSE                                                     \
        )

#define _STATE_COUNT_OFFSET(reg, offset, count)                                \
    _State(\
        Context, index, \
        (reg ## _Address >> 2) + offset, \
        reg ## _ResetValue, \
        count, \
        gcvFALSE, gcvFALSE                                                     \
        )

#define _STATE_MIRROR_COUNT(reg, mirror, count)                                \
    _StateMirror(\
        Context, \
        reg ## _Address >> 2, \
        count, \
        mirror ## _Address >> 2                                                \
        )

#define _STATE_HINT(reg)                                                       \
    _State(\
        Context, index, \
        reg ## _Address >> 2, \
        reg ## _ResetValue, \
        reg ## _Count, \
        gcvFALSE, gcvTRUE                                                      \
        )

#define _STATE_HINT_BLOCK(reg, block, count)                                   \
    _State(\
        Context, index, \
        (reg ## _Address >> 2) + (block << reg ## _BLK), \
        reg ## _ResetValue, \
        count, \
        gcvFALSE, gcvTRUE                                                      \
        )

#define _STATE_X(reg)                                                          \
    _State(\
        Context, index, \
        reg ## _Address >> 2, \
        reg ## _ResetValue, \
        reg ## _Count, \
        gcvTRUE, gcvFALSE                                                      \
        )

#define _CLOSE_RANGE()                                                         \
    _TerminateStateBlock(Context, index)

#define _ENABLE(reg, field)                                                    \
    do                                                                         \
    {                                                                          \
        if (gcmVERIFYFIELDVALUE(data, reg, MASK_ ## field, ENABLED))           \
        {                                                                      \
            enable |= gcmFIELDMASK(reg, field);                                \
        }                                                                      \
    }                                                                          \
    while (gcvFALSE)

#define _BLOCK_COUNT(reg)                                                      \
    ((reg ## _Count) >> (reg ## _BLK))


/******************************************************************************\
*********************** Support Functions and Definitions **********************
\******************************************************************************/

#define gcdSTATE_MASK \
    (((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x03 | 0xC0FFEE & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))))

#if !defined(VIVANTE_NO_3D)
static gctSIZE_T
_TerminateStateBlock(
    IN gckCONTEXT Context,
    IN gctSIZE_T Index
    )
{
    gctUINT32_PTR buffer;
    gctSIZE_T align;

    /* Determine if we need alignment. */
    align = (Index & 1) ? 1 : 0;

    /* Address correct index. */
    buffer = (Context->buffer == gcvNULL)
        ? gcvNULL
        : Context->buffer->logical;

    /* Flush the current state block; make sure no pairing with the states
       to follow happens. */
    if (align && (buffer != gcvNULL))
    {
        buffer[Index] = 0xDEADDEAD;
    }

    /* Reset last address. */
    Context->lastAddress = ~0U;

    /* Return alignment requirement. */
    return align;
}
#endif


static gctSIZE_T
_FlushPipe(
    IN gckCONTEXT Context,
    IN gctSIZE_T Index,
    IN gcePIPE_SELECT Pipe
    )
{
    if (Context->buffer != gcvNULL)
    {
        gctUINT32_PTR buffer;

        /* Address correct index. */
        buffer = Context->buffer->logical + Index;

        /* Flush the current pipe. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E03) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = (Pipe == gcvPIPE_2D)
                ?   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 3:3) - (0 ? 3:3) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 3:3) - (0 ? 3:3) + 1))))))) << (0 ? 3:3)))
                :   ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 0:0) - (0 ? 0:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 0:0) - (0 ? 0:0) + 1))))))) << (0 ? 0:0)))
                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 1:1) - (0 ? 1:1) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 1:1) - (0 ? 1:1) + 1))))))) << (0 ? 1:1)))
                  | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 2:2) - (0 ? 2:2) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 2:2) - (0 ? 2:2) + 1))))))) << (0 ? 2:2)));

        /* Semaphore from FE to PE. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* Stall from FE to PE. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        *buffer
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));
    }

    /* Flushing 3D pipe takes 6 slots. */
    return 6;
}

static gctSIZE_T
_SemaphoreStall(
    IN gckCONTEXT Context,
    IN gctSIZE_T Index
    )
{
    if (Context->buffer != gcvNULL)
    {
        gctUINT32_PTR buffer;

        /* Address correct index. */
        buffer = Context->buffer->logical + Index;

        /* Semaphore from FE to PE. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E02) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));

        /* Stall from FE to PE. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x09 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

        *buffer
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 4:0) - (0 ? 4:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:0) - (0 ? 4:0) + 1))))))) << (0 ? 4:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8))) | (((gctUINT32) (0x07 & ((gctUINT32) ((((1 ? 12:8) - (0 ? 12:8) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 12:8) - (0 ? 12:8) + 1))))))) << (0 ? 12:8)));
    }

    /* Semaphore/stall takes 4 slots. */
    return 4;
}

static gctSIZE_T
_SwitchPipe(
    IN gckCONTEXT Context,
    IN gctSIZE_T Index,
    IN gcePIPE_SELECT Pipe
    )
{
    if (Context->buffer != gcvNULL)
    {
        gctUINT32_PTR buffer;

        /* Address correct index. */
        buffer = Context->buffer->logical + Index;

        /* LoadState(AQPipeSelect, 1), pipe. */
        *buffer++
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0x0E00) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (1) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        *buffer
            = (Pipe == gcvPIPE_2D)
                ? 0x1
                : 0x0;
    }

    return 2;
}

#if !defined(VIVANTE_NO_3D)
static gctSIZE_T
_State(
    IN gckCONTEXT Context,
    IN gctSIZE_T Index,
    IN gctUINT32 Address,
    IN gctUINT32 Value,
    IN gctSIZE_T Size,
    IN gctBOOL FixedPoint,
    IN gctBOOL Hinted
    )
{
    gctUINT32_PTR buffer;
    gctSIZE_T align, i;

    /* Determine if we need alignment. */
    align = (Index & 1) ? 1 : 0;

    /* Address correct index. */
    buffer = (Context->buffer == gcvNULL)
        ? gcvNULL
        : Context->buffer->logical;

    if ((buffer == gcvNULL) && (Address + Size > Context->stateCount))
    {
        /* Determine maximum state. */
        Context->stateCount = Address + Size;
    }

    /* Do we need a new entry? */
    if ((Address != Context->lastAddress) || (FixedPoint != Context->lastFixed))
    {
        if (buffer != gcvNULL)
        {
            if (align)
            {
                /* Add filler. */
                buffer[Index++] = 0xDEADDEAD;
            }

            /* LoadState(Address, Count). */
            gcmkASSERT((Index & 1) == 0);

            if (FixedPoint)
            {
                buffer[Index]
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 26:26) - (0 ? 26:26) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 26:26) - (0 ? 26:26) + 1))))))) << (0 ? 26:26))) | (((gctUINT32) (0x1 & ((gctUINT32) ((((1 ? 26:26) - (0 ? 26:26) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 26:26) - (0 ? 26:26) + 1))))))) << (0 ? 26:26)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (Size) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (Address) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));
            }
            else
            {
                buffer[Index]
                    = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x01 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 26:26) - (0 ? 26:26) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 26:26) - (0 ? 26:26) + 1))))))) << (0 ? 26:26))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 26:26) - (0 ? 26:26) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 26:26) - (0 ? 26:26) + 1))))))) << (0 ? 26:26)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (Size) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)))
                    | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (Address) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));
            }

            /* Walk all the states. */
            for (i = 0; i < Size; i += 1)
            {
                /* Set state to uninitialized value. */
                buffer[Index + 1 + i] = Value;

                /* Set index in state mapping table. */
                Context->map[Address + i].index = Index + 1 + i;

#if gcdSECURE_USER
                /* Save hint. */
                if (Context->hint != gcvNULL)
                {
                    Context->hint[Address + i] = Hinted;
                }
#endif
            }
        }

        /* Save information for this LoadState. */
        Context->lastIndex   = Index;
        Context->lastAddress = Address + Size;
        Context->lastSize    = Size;
        Context->lastFixed   = FixedPoint;

        /* Return size for load state. */
        return align + 1 + Size;
    }

    /* Append this state to the previous one. */
    if (buffer != gcvNULL)
    {
        /* Update last load state. */
        buffer[Context->lastIndex] =
            ((((gctUINT32) (buffer[Context->lastIndex])) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (Context->lastSize + Size) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

        /* Walk all the states. */
        for (i = 0; i < Size; i += 1)
        {
            /* Set state to uninitialized value. */
            buffer[Index + i] = Value;

            /* Set index in state mapping table. */
            Context->map[Address + i].index = Index + i;

#if gcdSECURE_USER
            /* Save hint. */
            if (Context->hint != gcvNULL)
            {
                Context->hint[Address + i] = Hinted;
            }
#endif
        }
    }

    /* Update last address and size. */
    Context->lastAddress += Size;
    Context->lastSize    += Size;

    /* Return number of slots required. */
    return Size;
}

static gctSIZE_T
_StateMirror(
    IN gckCONTEXT Context,
    IN gctUINT32 Address,
    IN gctSIZE_T Size,
    IN gctUINT32 AddressMirror
    )
{
    gctSIZE_T i;

    /* Process when buffer is set. */
    if (Context->buffer != gcvNULL)
    {
        /* Walk all states. */
        for (i = 0; i < Size; i++)
        {
            /* Copy the mapping address. */
            Context->map[Address + i].index =
                Context->map[AddressMirror + i].index;
        }
    }

    /* Return the number of required maps. */
    return Size;
}
#endif

static gceSTATUS
_InitializeContextBuffer(
    IN gckCONTEXT Context
    )
{
    gctUINT32_PTR buffer;
    gctSIZE_T index;

#if !defined(VIVANTE_NO_3D)
    gctUINT i;
    gctUINT vertexUniforms, fragmentUniforms;
    gctUINT fe2vsCount;
#endif

    /* Reset the buffer index. */
    index = 0;

    /* Reset the last state address. */
    Context->lastAddress = ~0U;

    /* Get the buffer pointer. */
    buffer = (Context->buffer == gcvNULL)
        ? gcvNULL
        : Context->buffer->logical;


    /**************************************************************************/
    /* Build 2D states. *******************************************************/


#if !defined(VIVANTE_NO_3D)
    /**************************************************************************/
    /* Build 3D states. *******************************************************/

    /* Query shader support. */
    gcmkVERIFY_OK(gckHARDWARE_QueryShaderCaps(
        Context->hardware, &vertexUniforms, &fragmentUniforms, gcvNULL));

    /* Store the 3D entry index. */
    Context->entryOffset3D = index * gcmSIZEOF(gctUINT32);

    /* Flush 2D pipe. */
    index += _FlushPipe(Context, index, gcvPIPE_2D);

    /* Switch to 3D pipe. */
    index += _SwitchPipe(Context, index, gcvPIPE_3D);

    /* Current context pointer. */
#if gcdDEBUG && 1
    index += _State(Context, index, 0x03850 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
#endif

    /* Global states. */
    index += _State(Context, index, 0x03814 >> 2, 0x00000001, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x03818 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x0381C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x03820 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x03828 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x0382C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x03834 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x03838 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x0384C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);

    /* Front End states. */
	fe2vsCount = 12;
	if ((((((gctUINT32) (Context->hardware->identity.chipMinorFeatures1)) >> (0 ? 23:23)) & ((gctUINT32) ((((1 ? 23:23) - (0 ? 23:23) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:23) - (0 ? 23:23) + 1)))))) ))
	{
		fe2vsCount = 16;
	}
    index += _State(Context, index, 0x00600 >> 2, 0x00000000, fe2vsCount, gcvFALSE, gcvFALSE);
    index += _CLOSE_RANGE();

    index += _State(Context, index, 0x00644 >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x00648 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x0064C >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x00650 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00680 >> 2, 0x00000000, 8, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x006A0 >> 2, 0x00000000, 8, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00670 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00678 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x0067C >> 2, 0xFFFFFFFF, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x006C0 >> 2, 0x00000000, 16, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00700 >> 2, 0x00000000, 16, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00740 >> 2, 0x00000000, 16, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00780 >> 2, 0x3F800000, 16, gcvFALSE, gcvFALSE);

    /* Vertex Shader states. */
    index += _State(Context, index, 0x00800 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00804 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00808 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x0080C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00810 >> 2, 0x00000000, 4, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00820 >> 2, 0x00000000, 4, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00830 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00838 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    if (Context->hardware->identity.instructionCount <= 256)
    {
        index += _State(Context, index, 0x04000 >> 2, 0x00000000, 1024, gcvFALSE, gcvFALSE);
    }

    index += _CLOSE_RANGE();
    index += _State(Context, index, 0x05000 >> 2, 0x00000000, vertexUniforms * 4, gcvFALSE, gcvFALSE);

    /* Primitive Assembly states. */
    index += _State(Context, index, 0x00A00 >> 2, 0x00000000, 1, gcvTRUE, gcvFALSE);
    index += _State(Context, index, 0x00A04 >> 2, 0x00000000, 1, gcvTRUE, gcvFALSE);
    index += _State(Context, index, 0x00A08 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00A0C >> 2, 0x00000000, 1, gcvTRUE, gcvFALSE);
    index += _State(Context, index, 0x00A10 >> 2, 0x00000000, 1, gcvTRUE, gcvFALSE);
    index += _State(Context, index, 0x00A14 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00A18 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00A1C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00A28 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00A2C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00A30 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00A40 >> 2, 0x00000000, 10, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00A34 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00A38 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00A3C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00A80 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00A84 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);

    /* Setup states. */
    index += _State(Context, index, 0x00C00 >> 2, 0x00000000, 1, gcvTRUE, gcvFALSE);
    index += _State(Context, index, 0x00C04 >> 2, 0x00000000, 1, gcvTRUE, gcvFALSE);
    index += _State(Context, index, 0x00C08 >> 2, 0x45000000, 1, gcvTRUE, gcvFALSE);
    index += _State(Context, index, 0x00C0C >> 2, 0x45000000, 1, gcvTRUE, gcvFALSE);
    index += _State(Context, index, 0x00C10 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00C14 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00C18 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00C1C >> 2, 0x42000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00C20 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00C24 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);

    /* Raster states. */
    index += _State(Context, index, 0x00E00 >> 2, 0x00000001, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00E10 >> 2, 0x00000000, 4, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00E04 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00E40 >> 2, 0x00000000, 16, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00E08 >> 2, 0x00000031, 1, gcvFALSE, gcvFALSE);

    /* Pixel Shader states. */
    index += _State(Context, index, 0x01000 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01004 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01008 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x0100C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01010 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01018 >> 2, 0x01000000, 1, gcvFALSE, gcvFALSE);
    if (Context->hardware->identity.instructionCount <= 256)
    {
        index += _State(Context, index, 0x06000 >> 2, 0x00000000, 1024, gcvFALSE, gcvFALSE);
    }

    index += _CLOSE_RANGE();
    index += _State(Context, index, 0x07000 >> 2, 0x00000000, fragmentUniforms * 4, gcvFALSE, gcvFALSE);

    /* Texture states. */
    index += _State(Context, index, 0x02000 >> 2, 0x00000000, 12, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x02040 >> 2, 0x00000000, 12, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x02080 >> 2, 0x00000000, 12, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x020C0 >> 2, 0x00000000, 12, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x02100 >> 2, 0x00000000, 12, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x02140 >> 2, 0x00000000, 12, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x02180 >> 2, 0x00000000, 12, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x021C0 >> 2, 0x00321000, 12, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x02200 >> 2, 0x00000000, 12, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x02240 >> 2, 0x00000000, 12, gcvFALSE, gcvFALSE);
    index += _State(Context, index, (0x02400 >> 2) + (0 << 4), 0x00000000, 12, gcvFALSE, gcvTRUE);
    index += _State(Context, index, (0x02440 >> 2) + (0 << 4), 0x00000000, 12, gcvFALSE, gcvTRUE);
    index += _State(Context, index, (0x02480 >> 2) + (0 << 4), 0x00000000, 12, gcvFALSE, gcvTRUE);
    index += _State(Context, index, (0x024C0 >> 2) + (0 << 4), 0x00000000, 12, gcvFALSE, gcvTRUE);
    index += _State(Context, index, (0x02500 >> 2) + (0 << 4), 0x00000000, 12, gcvFALSE, gcvTRUE);
    index += _State(Context, index, (0x02540 >> 2) + (0 << 4), 0x00000000, 12, gcvFALSE, gcvTRUE);
    index += _State(Context, index, (0x02580 >> 2) + (0 << 4), 0x00000000, 12, gcvFALSE, gcvTRUE);
    index += _State(Context, index, (0x025C0 >> 2) + (0 << 4), 0x00000000, 12, gcvFALSE, gcvTRUE);
    index += _State(Context, index, (0x02600 >> 2) + (0 << 4), 0x00000000, 12, gcvFALSE, gcvTRUE);
    index += _State(Context, index, (0x02640 >> 2) + (0 << 4), 0x00000000, 12, gcvFALSE, gcvTRUE);
    index += _State(Context, index, (0x02680 >> 2) + (0 << 4), 0x00000000, 12, gcvFALSE, gcvTRUE);
    index += _State(Context, index, (0x026C0 >> 2) + (0 << 4), 0x00000000, 12, gcvFALSE, gcvTRUE);
    index += _State(Context, index, (0x02700 >> 2) + (0 << 4), 0x00000000, 12, gcvFALSE, gcvTRUE);
    index += _State(Context, index, (0x02740 >> 2) + (0 << 4), 0x00000000, 12, gcvFALSE, gcvTRUE);
    index += _CLOSE_RANGE();

    if ((((((gctUINT32) (Context->hardware->identity.chipMinorFeatures2)) >> (0 ? 11:11)) & ((gctUINT32) ((((1 ? 11:11) - (0 ? 11:11) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 11:11) - (0 ? 11:11) + 1)))))) ))
    {
        gctUINT texBlockCount;

        /* New texture block. */
        index += _State(Context, index, 0x10000 >> 2, 0x00000000, 32, gcvFALSE, gcvFALSE);
        index += _State(Context, index, 0x10080 >> 2, 0x00000000, 32, gcvFALSE, gcvFALSE);
        index += _State(Context, index, 0x10100 >> 2, 0x00000000, 32, gcvFALSE, gcvFALSE);
        index += _State(Context, index, 0x10180 >> 2, 0x00000000, 32, gcvFALSE, gcvFALSE);
        index += _State(Context, index, 0x10200 >> 2, 0x00000000, 32, gcvFALSE, gcvFALSE);
        index += _State(Context, index, 0x10280 >> 2, 0x00000000, 32, gcvFALSE, gcvFALSE);
        index += _State(Context, index, 0x10300 >> 2, 0x00000000, 32, gcvFALSE, gcvFALSE);
        index += _State(Context, index, 0x10380 >> 2, 0x00321000, 32, gcvFALSE, gcvFALSE);
        index += _State(Context, index, 0x10400 >> 2, 0x00000000, 32, gcvFALSE, gcvFALSE);
        index += _State(Context, index, 0x10480 >> 2, 0x00000000, 32, gcvFALSE, gcvFALSE);

        if ((((((gctUINT32) (Context->hardware->identity.chipMinorFeatures2)) >> (0 ? 15:15)) & ((gctUINT32) ((((1 ? 15:15) - (0 ? 15:15) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:15) - (0 ? 15:15) + 1)))))) ))
        {
            index += _State(Context, index, 0x12000 >> 2, 0x00000000, 256, gcvFALSE, gcvFALSE);
            index += _State(Context, index, 0x12400 >> 2, 0x00000000, 256, gcvFALSE, gcvFALSE);
        }

        if ((Context->hardware->identity.chipModel == gcv2000)
         && (Context->hardware->identity.chipRevision == 0x5108))
        {
            texBlockCount = 12;
        }
        else
        {
            texBlockCount = ((512) >> (4));
        }
        for (i = 0; i < texBlockCount; i += 1)
        {
            index += _State(Context, index, (0x10800 >> 2) + (i << 4), 0x00000000, 14, gcvFALSE, gcvTRUE);
        }
    }

    /* YUV. */
    index += _State(Context, index, 0x01678 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x0167C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01680 >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x01684 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01688 >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x0168C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01690 >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x01694 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01698 >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x0169C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _CLOSE_RANGE();

    /* Thread walker states. */
    index += _State(Context, index, 0x00900 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00904 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00908 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x0090C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00910 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00914 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00918 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x0091C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x00924 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _CLOSE_RANGE();

	if (Context->hardware->identity.instructionCount > 1024)
	{
		/* New Shader instruction memory. */
		index += _State(Context, index, 0x0085C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
		index += _State(Context, index, 0x0101C >> 2, 0x00000100, 1, gcvFALSE, gcvFALSE);
		index += _State(Context, index, 0x00860 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
		index += _CLOSE_RANGE();

		for (i = 0;
		     i < Context->hardware->identity.instructionCount << 2;
		     i += 256 << 2
		     )
		{
			index += _State(Context, index, (0x20000 >> 2) + i, 0x00000000, 256 << 2, gcvFALSE, gcvFALSE);
			index += _CLOSE_RANGE();
		}
	}
	else if (Context->hardware->identity.instructionCount > 256)
	{
		/* VX instruction memory. */
		for (i = 0;
		     i < Context->hardware->identity.instructionCount << 2;
		     i += 256 << 2
		     )
		{
			index += _State(Context, index, (0x0C000 >> 2) + i, 0x00000000, 256 << 2, gcvFALSE, gcvFALSE);
			index += _CLOSE_RANGE();
		}

		_StateMirror(Context, 0x08000 >> 2, Context->hardware->identity.instructionCount << 2 , 0x0C000 >> 2);
	}

    /* Store the index of the "XD" entry. */
    Context->entryOffsetXDFrom3D = index * gcmSIZEOF(gctUINT32);

    index += _FlushPipe(Context, index, gcvPIPE_3D);

    /* Pixel Engine states. */
    index += _State(Context, index, 0x01400 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01404 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01408 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x0140C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01414 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01418 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x0141C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01420 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01424 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01428 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x0142C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01434 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01454 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01458 >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x0145C >> 2, 0x00000010, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x014A0 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x014A8 >> 2, 0xFFFFFFFF, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x014AC >> 2, 0xFFFFFFFF, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x014B0 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x014B4 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x014A4 >> 2, 0x000E400C, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01580 >> 2, 0x00000000, 3, gcvFALSE, gcvFALSE);

    /* Composition states. */
    index += _State(Context, index, 0x03008 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);

    if (Context->hardware->identity.pixelPipes == 1)
    {
        index += _State(Context, index, 0x01430 >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
        index += _State(Context, index, 0x01410 >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    }
    else
    {
        index += _State(Context, index, (0x01460 >> 2) + (0 << 3), 0x00000000, Context->hardware->identity.pixelPipes, gcvFALSE, gcvTRUE);

        index += _State(Context, index, (0x01480 >> 2) + (0 << 3), 0x00000000, Context->hardware->identity.pixelPipes, gcvFALSE, gcvTRUE);

        for (i = 0; i < 2; i++)
        {
            index += _State(Context, index, (0x01500 >> 2) + (i << 3), 0x00000000, Context->hardware->identity.pixelPipes, gcvFALSE, gcvTRUE);
        }
    }

    /* Resolve states. */
    index += _State(Context, index, 0x01604 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01608 >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x0160C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01610 >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x01614 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01620 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01630 >> 2, 0x00000000, 2, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01640 >> 2, 0x00000000, 4, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x0163C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x016A0 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x016B4 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _CLOSE_RANGE();

    if (Context->hardware->identity.pixelPipes > 1)
    {
        index += _State(Context, index, (0x016C0 >> 2) + (0 << 3), 0x00000000, Context->hardware->identity.pixelPipes, gcvFALSE, gcvTRUE);

        index += _State(Context, index, (0x016E0 >> 2) + (0 << 3), 0x00000000, Context->hardware->identity.pixelPipes, gcvFALSE, gcvTRUE);

        index += _State(Context, index, 0x01700 >> 2, 0x00000000, Context->hardware->identity.pixelPipes, gcvFALSE, gcvFALSE);
    }

    /* Tile status. */
    index += _State(Context, index, 0x01654 >> 2, 0x00200000, 1, gcvFALSE, gcvFALSE);

    index += _CLOSE_RANGE();
    index += _State(Context, index, 0x01658 >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x0165C >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x01660 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01664 >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x01668 >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x0166C >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01670 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01674 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x016A4 >> 2, 0x00000000, 1, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x016A8 >> 2, 0x00000000, 1, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01720 >> 2, 0x00000000, 8, gcvFALSE, gcvFALSE);
    index += _State(Context, index, 0x01740 >> 2, 0x00000000, 8, gcvFALSE, gcvTRUE);
    index += _State(Context, index, 0x01760 >> 2, 0x00000000, 8, gcvFALSE, gcvFALSE);
    index += _CLOSE_RANGE();

    /* Semaphore/stall. */
    index += _SemaphoreStall(Context, index);
#endif

    /**************************************************************************/
    /* Link to another address. ***********************************************/

    Context->linkIndex3D = index;

    if (buffer != gcvNULL)
    {
        buffer[index + 0]
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        buffer[index + 1]
            = 0;
    }

    index += 2;

    /* Store the end of the context buffer. */
    Context->bufferSize = index * gcmSIZEOF(gctUINT32);


    /**************************************************************************/
    /* Pipe switch for the case where neither 2D nor 3D are used. *************/

    /* Store the 3D entry index. */
    Context->entryOffsetXDFrom2D = index * gcmSIZEOF(gctUINT32);

    /* Flush 2D pipe. */
    index += _FlushPipe(Context, index, gcvPIPE_2D);

    /* Switch to 3D pipe. */
    index += _SwitchPipe(Context, index, gcvPIPE_3D);

    /* Store the location of the link. */
    Context->linkIndexXD = index;

    if (buffer != gcvNULL)
    {
        buffer[index + 0]
            = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x08 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)))
            | ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0))) | (((gctUINT32) ((gctUINT32) (0) & ((gctUINT32) ((((1 ? 15:0) - (0 ? 15:0) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 15:0) - (0 ? 15:0) + 1))))))) << (0 ? 15:0)));

        buffer[index + 1]
            = 0;
    }

    index += 2;


    /**************************************************************************/
    /* Save size for buffer. **************************************************/

    Context->totalSize = index * gcmSIZEOF(gctUINT32);


    /* Success. */
    return gcvSTATUS_OK;
}

static gceSTATUS
_DestroyContext(
    IN gckCONTEXT Context
    )
{
    gceSTATUS status = gcvSTATUS_OK;

    if (Context != gcvNULL)
    {
        gcsCONTEXT_PTR bufferHead;

        /* Free context buffers. */
        for (bufferHead = Context->buffer; Context->buffer != gcvNULL;)
        {
            /* Get a shortcut to the current buffer. */
            gcsCONTEXT_PTR buffer = Context->buffer;

            /* Get the next buffer. */
            gcsCONTEXT_PTR next = buffer->next;

            /* Last item? */
            if (next == bufferHead)
            {
                next = gcvNULL;
            }

            /* Destroy the signal. */
            if (buffer->signal != gcvNULL)
            {
                gcmkONERROR(gckOS_DestroySignal(
                    Context->os, buffer->signal
                    ));

                buffer->signal = gcvNULL;
            }

            /* Free state delta map. */
            if (buffer->logical != gcvNULL)
            {
                gcmkONERROR(gckOS_FreeContiguous(
                    Context->os,
                    buffer->physical,
                    buffer->logical,
                    Context->totalSize
                    ));

                buffer->logical = gcvNULL;
            }

            /* Free context buffer. */
            gcmkONERROR(gcmkOS_SAFE_FREE(Context->os, buffer));

            /* Remove from the list. */
            Context->buffer = next;
        }

#if gcdSECURE_USER
        /* Free the hint array. */
        if (Context->hint != gcvNULL)
        {
            gcmkONERROR(gcmkOS_SAFE_FREE(Context->os, Context->hint));
        }
#endif
        /* Free record array copy. */
        if (Context->recordArray != gcvNULL)
        {
            gcmkONERROR(gcmkOS_SAFE_FREE(Context->os, Context->recordArray));
        }

        /* Free the state mapping. */
        if (Context->map != gcvNULL)
        {
            gcmkONERROR(gcmkOS_SAFE_FREE(Context->os, Context->map));
        }

        /* Mark the gckCONTEXT object as unknown. */
        Context->object.type = gcvOBJ_UNKNOWN;

        /* Free the gckCONTEXT object. */
        gcmkONERROR(gcmkOS_SAFE_FREE(Context->os, Context));
    }

OnError:
    return status;
}


/******************************************************************************\
**************************** Context Management API ****************************
\******************************************************************************/

/******************************************************************************\
**
**  gckCONTEXT_Construct
**
**  Construct a new gckCONTEXT object.
**
**  INPUT:
**
**      gckOS Os
**          Pointer to gckOS object.
**
**      gctUINT32 ProcessID
**          Current process ID.
**
**      gckHARDWARE Hardware
**          Pointer to gckHARDWARE object.
**
**  OUTPUT:
**
**      gckCONTEXT * Context
**          Pointer to a variable thet will receive the gckCONTEXT object
**          pointer.
*/
gceSTATUS
gckCONTEXT_Construct(
    IN gckOS Os,
    IN gckHARDWARE Hardware,
    IN gctUINT32 ProcessID,
    OUT gckCONTEXT * Context
    )
{
    gceSTATUS status;
    gckCONTEXT context = gcvNULL;
    gctSIZE_T allocationSize;
    gctUINT i;
    gctPOINTER pointer = gcvNULL;

    gcmkHEADER_ARG("Os=0x%08X Hardware=0x%08X", Os, Hardware);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Os, gcvOBJ_OS);
    gcmkVERIFY_ARGUMENT(Context != gcvNULL);


    /**************************************************************************/
    /* Allocate and initialize basic fields of gckCONTEXT. ********************/

    /* The context object size. */
    allocationSize = gcmSIZEOF(struct _gckCONTEXT);

    /* Allocate the object. */
    gcmkONERROR(gckOS_Allocate(
        Os, allocationSize, &pointer
        ));

    context = pointer;

    /* Reset the entire object. */
    gcmkONERROR(gckOS_ZeroMemory(context, allocationSize));

    /* Initialize the gckCONTEXT object. */
    context->object.type = gcvOBJ_CONTEXT;
    context->os          = Os;
    context->hardware    = Hardware;


#if defined(VIVANTE_NO_3D)
    context->entryPipe = gcvPIPE_2D;
    context->exitPipe  = gcvPIPE_2D;
#elif gcdCMD_NO_2D_CONTEXT
    context->entryPipe = gcvPIPE_3D;
    context->exitPipe  = gcvPIPE_3D;
#else
    context->entryPipe
        = (((((gctUINT32) (context->hardware->identity.chipFeatures)) >> (0 ? 9:9)) & ((gctUINT32) ((((1 ? 9:9) - (0 ? 9:9) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 9:9) - (0 ? 9:9) + 1)))))) )
            ? gcvPIPE_2D
            : gcvPIPE_3D;
    context->exitPipe = gcvPIPE_3D;
#endif

    /* Get the command buffer requirements. */
    gcmkONERROR(gckHARDWARE_QueryCommandBuffer(
        Hardware,
        &context->alignment,
        &context->reservedHead,
        &context->reservedTail
        ));

    /* Mark the context as dirty to force loading of the entire state table
       the first time. */
    context->dirty = gcvTRUE;


    /**************************************************************************/
    /* Get the size of the context buffer. ************************************/

    gcmkONERROR(_InitializeContextBuffer(context));


    /**************************************************************************/
    /* Compute the size of the record array. **********************************/

    context->recordArraySize
        = gcmSIZEOF(gcsSTATE_DELTA_RECORD) * context->stateCount;


    if (context->stateCount > 0)
    {
        /**************************************************************************/
        /* Allocate and reset the state mapping table. ****************************/

        /* Allocate the state mapping table. */
        gcmkONERROR(gckOS_Allocate(
            Os,
            gcmSIZEOF(gcsSTATE_MAP) * context->stateCount,
            &pointer
            ));

        context->map = pointer;

        /* Zero the state mapping table. */
        gcmkONERROR(gckOS_ZeroMemory(
            context->map, gcmSIZEOF(gcsSTATE_MAP) * context->stateCount
            ));


        /**************************************************************************/
        /* Allocate the hint array. ***********************************************/

#if gcdSECURE_USER
        /* Allocate hints. */
        gcmkONERROR(gckOS_Allocate(
            Os,
            gcmSIZEOF(gctBOOL) * context->stateCount,
            &pointer
            ));

        context->hint = pointer;
#endif
    }

    /**************************************************************************/
    /* Allocate the context and state delta buffers. **************************/

    for (i = 0; i < gcdCONTEXT_BUFFER_COUNT; i += 1)
    {
        /* Allocate a context buffer. */
        gcsCONTEXT_PTR buffer;

        /* Allocate the context buffer structure. */
        gcmkONERROR(gckOS_Allocate(
            Os,
            gcmSIZEOF(gcsCONTEXT),
            &pointer
            ));

        buffer = pointer;

        /* Reset the context buffer structure. */
        gcmkVERIFY_OK(gckOS_ZeroMemory(
            buffer, gcmSIZEOF(gcsCONTEXT)
            ));

        /* Append to the list. */
        if (context->buffer == gcvNULL)
        {
            buffer->next    = buffer;
            context->buffer = buffer;
        }
        else
        {
            buffer->next          = context->buffer->next;
            context->buffer->next = buffer;
        }

        /* Set the number of delta in the order of creation. */
#if gcmIS_DEBUG(gcdDEBUG_CODE)
        buffer->num = i;
#endif

        /* Create the busy signal. */
        gcmkONERROR(gckOS_CreateSignal(
            Os, gcvFALSE, &buffer->signal
            ));

        /* Set the signal, buffer is currently not busy. */
        gcmkONERROR(gckOS_Signal(
            Os, buffer->signal, gcvTRUE
            ));

        /* Create a new physical context buffer. */
        gcmkONERROR(gckOS_AllocateContiguous(
            Os,
            gcvFALSE,
            &context->totalSize,
            &buffer->physical,
            &pointer
            ));

        buffer->logical = pointer;

        /* Set gckEVENT object pointer. */
        buffer->eventObj = Hardware->kernel->eventObj;

        /* Set the pointers to the LINK commands. */
        if (context->linkIndex2D != 0)
        {
            buffer->link2D = &buffer->logical[context->linkIndex2D];
        }

        if (context->linkIndex3D != 0)
        {
            buffer->link3D = &buffer->logical[context->linkIndex3D];
        }

        if (context->linkIndexXD != 0)
        {
            gctPOINTER xdLink;
            gctUINT8_PTR xdEntryLogical;
            gctSIZE_T xdEntrySize;
            gctSIZE_T linkBytes;

            /* Determine LINK parameters. */
            xdLink
                = &buffer->logical[context->linkIndexXD];

            xdEntryLogical
                = (gctUINT8_PTR) buffer->logical
                + context->entryOffsetXDFrom3D;

            xdEntrySize
                = context->bufferSize
                - context->entryOffsetXDFrom3D;

            /* Query LINK size. */
            gcmkONERROR(gckHARDWARE_Link(
                Hardware, gcvNULL, gcvNULL, 0, &linkBytes
                ));

            /* Generate a LINK. */
            gcmkONERROR(gckHARDWARE_Link(
                Hardware,
                xdLink,
                xdEntryLogical,
                xdEntrySize,
                &linkBytes
                ));
        }
    }


    /**************************************************************************/
    /* Initialize the context buffers. ****************************************/

    /* Initialize the current context buffer. */
    gcmkONERROR(_InitializeContextBuffer(context));

    /* Make all created contexts equal. */
    {
        gcsCONTEXT_PTR currContext, tempContext;

        /* Set the current context buffer. */
        currContext = context->buffer;

        /* Get the next context buffer. */
        tempContext = currContext->next;

        /* Loop through all buffers. */
        while (tempContext != currContext)
        {
            if (tempContext == gcvNULL)
            {
                gcmkONERROR(gcvSTATUS_NOT_FOUND);
            }

            /* Copy the current context. */
            gcmkONERROR(gckOS_MemCopy(
                tempContext->logical,
                currContext->logical,
                context->totalSize
                ));

            /* Get the next context buffer. */
            tempContext = tempContext->next;
        }
    }

    /* Return pointer to the gckCONTEXT object. */
    *Context = context;

    /* Success. */
    gcmkFOOTER_ARG("*Context=0x%08X", *Context);
    return gcvSTATUS_OK;

OnError:
    /* Roll back on error. */
    gcmkVERIFY_OK(_DestroyContext(context));

    /* Return the status. */
    gcmkFOOTER();
    return status;
}

/******************************************************************************\
**
**  gckCONTEXT_Destroy
**
**  Destroy a gckCONTEXT object.
**
**  INPUT:
**
**      gckCONTEXT Context
**          Pointer to an gckCONTEXT object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCONTEXT_Destroy(
    IN gckCONTEXT Context
    )
{
    gceSTATUS status;

    gcmkHEADER_ARG("Context=0x%08X", Context);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Context, gcvOBJ_CONTEXT);

    /* Destroy the context and all related objects. */
    status = _DestroyContext(Context);

    /* Success. */
    gcmkFOOTER_NO();
    return status;
}

/******************************************************************************\
**
**  gckCONTEXT_Update
**
**  Merge all pending state delta buffers into the current context buffer.
**
**  INPUT:
**
**      gckCONTEXT Context
**          Pointer to an gckCONTEXT object.
**
**      gctUINT32 ProcessID
**          Current process ID.
**
**      gcsSTATE_DELTA_PTR StateDelta
**          Pointer to the state delta.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS
gckCONTEXT_Update(
    IN gckCONTEXT Context,
    IN gctUINT32 ProcessID,
    IN gcsSTATE_DELTA_PTR StateDelta
    )
{
#ifndef VIVANTE_NO_3D
    gceSTATUS status = gcvSTATUS_OK;
    gcsSTATE_DELTA _stateDelta;
    gckKERNEL kernel;
    gcsCONTEXT_PTR buffer;
    gcsSTATE_MAP_PTR map;
    gctBOOL needCopy = gcvFALSE;
    gcsSTATE_DELTA_PTR nDelta;
    gcsSTATE_DELTA_PTR uDelta = gcvNULL;
    gcsSTATE_DELTA_PTR kDelta = gcvNULL;
    gcsSTATE_DELTA_RECORD_PTR record;
    gcsSTATE_DELTA_RECORD_PTR recordArray = gcvNULL;
    gctUINT elementCount;
    gctUINT address;
    gctUINT32 mask;
    gctUINT32 data;
    gctUINT index;
    gctUINT i, j;

#if gcdSECURE_USER
    gcskSECURE_CACHE_PTR cache;
#endif

    gcmkHEADER_ARG(
        "Context=0x%08X ProcessID=%d StateDelta=0x%08X",
        Context, ProcessID, StateDelta
        );

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Context, gcvOBJ_CONTEXT);

    /* Get a shortcut to the kernel object. */
    kernel = Context->hardware->kernel;

    /* Check wehther we need to copy the structures or not. */
    gcmkONERROR(gckOS_QueryNeedCopy(Context->os, ProcessID, &needCopy));

    /* Allocate the copy buffer for the user record array. */
    if (needCopy && (Context->recordArray == gcvNULL))
    {
        /* Allocate the buffer. */
        gcmkONERROR(gckOS_Allocate(
            Context->os,
            Context->recordArraySize,
            (gctPOINTER *) &Context->recordArray
            ));
    }

    /* Get the current context buffer. */
    buffer = Context->buffer;

    /* Wait until the context buffer becomes available; this will
       also reset the signal and mark the buffer as busy. */
    gcmkONERROR(gckOS_WaitSignal(
        Context->os, buffer->signal, gcvINFINITE
        ));

#if gcdSECURE_USER
    /* Get the cache form the database. */
    gcmkONERROR(gckKERNEL_GetProcessDBCache(kernel, ProcessID, &cache));
#endif

#if gcmIS_DEBUG(gcdDEBUG_CODE) && 1 && !defined(VIVANTE_NO_3D)
    /* Update current context token. */
    buffer->logical[Context->map[0x0E14].index]
        = gcmPTR2INT(Context);
#endif

    /* Are there any pending deltas? */
    if (buffer->deltaCount != 0)
    {
        /* Get the state map. */
        map = Context->map;

        /* Get the first delta item. */
        uDelta = buffer->delta;

        /* Reset the vertex stream count. */
        elementCount = 0;

        /* Merge all pending deltas. */
        for (i = 0; i < buffer->deltaCount; i += 1)
        {
            /* Get access to the state delta. */
            gcmkONERROR(gckKERNEL_OpenUserData(
                kernel, needCopy,
                &_stateDelta,
                uDelta, gcmSIZEOF(gcsSTATE_DELTA),
                (gctPOINTER *) &kDelta
                ));

            /* Get access to the state records. */
            gcmkONERROR(gckKERNEL_OpenUserData(
                kernel, needCopy,
                Context->recordArray,
                kDelta->recordArray, Context->recordArraySize,
                (gctPOINTER *) &recordArray
                ));

            /* Merge all pending states. */
            for (j = 0; j < kDelta->recordCount; j += 1)
            {
                if (j >= Context->stateCount)
                {
                    break;
                }

                /* Get the current state record. */
                record = &recordArray[j];

                /* Get the state address. */
                address = record->address;

                /* Make sure the state is a part of the mapping table. */
                if (address >= Context->stateCount)
                {
                    gcmkTRACE(
                        gcvLEVEL_ERROR,
                        "%s(%d): State 0x%04X is not mapped.\n",
                        __FUNCTION__, __LINE__,
                        address
                        );

                    continue;
                }

                /* Get the state index. */
                index = map[address].index;

                /* Skip the state if not mapped. */
                if (index == 0)
                {
#if gcdDEBUG
                    if ((address != 0x0594)
                     && (address != 0x0E00)
                     && (address != 0x0E03)
                        )
                    {
#endif
                        gcmkTRACE(
                            gcvLEVEL_ERROR,
                            "%s(%d): State 0x%04X is not mapped.\n",
                            __FUNCTION__, __LINE__,
                            address
                            );
#if gcdDEBUG
                    }
#endif
                    continue;
                }

                /* Get the data mask. */
                mask = record->mask;

                /* Masked states that are being completly reset or regular states. */
                if ((mask == 0) || (mask == ~0U))
                {
                    /* Get the new data value. */
                    data = record->data;

                    /* Process special states. */
                    if (address == 0x0595)
                    {
                        /* Force auto-disable to be disabled. */
                        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 5:5) - (0 ? 5:5) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 5:5) - (0 ? 5:5) + 1))))))) << (0 ? 5:5)));
                        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 4:4) - (0 ? 4:4) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 4:4) - (0 ? 4:4) + 1))))))) << (0 ? 4:4)));
                        data = ((((gctUINT32) (data)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 13:13) - (0 ? 13:13) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 13:13) - (0 ? 13:13) + 1))))))) << (0 ? 13:13))) | (((gctUINT32) (0x0 & ((gctUINT32) ((((1 ? 13:13) - (0 ? 13:13) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 13:13) - (0 ? 13:13) + 1))))))) << (0 ? 13:13)));
                    }

#if gcdSECURE_USER
                    /* Do we need to convert the logical address? */
                    if (Context->hint[address])
                    {
                        /* Map handle into physical address. */
                        gcmkONERROR(gckKERNEL_MapLogicalToPhysical(
                            kernel, cache, (gctPOINTER) &data
                            ));
                    }
#endif

                    /* Set new data. */
                    buffer->logical[index] = data;
                }

                /* Masked states that are being set partially. */
                else
                {
                    buffer->logical[index]
                        = (~mask & buffer->logical[index])
                        | (mask & record->data);
                }
            }

            /* Get the element count. */
            if (kDelta->elementCount != 0)
            {
                elementCount = kDelta->elementCount;
            }

            /* Dereference delta. */
            kDelta->refCount -= 1;
            gcmkASSERT(kDelta->refCount >= 0);

            /* Get the next state delta. */
            nDelta = kDelta->next;

            /* Get access to the state records. */
            gcmkONERROR(gckKERNEL_CloseUserData(
                kernel, needCopy,
                gcvFALSE,
                kDelta->recordArray, Context->recordArraySize,
                (gctPOINTER *) &recordArray
                ));

            /* Close access to the current state delta. */
            gcmkONERROR(gckKERNEL_CloseUserData(
                kernel, needCopy,
                gcvTRUE,
                uDelta, gcmSIZEOF(gcsSTATE_DELTA),
                (gctPOINTER *) &kDelta
                ));

            /* Update the user delta pointer. */
            uDelta = nDelta;
        }

        /* Hardware disables all input streams when the stream 0 is programmed,
           it then reenables those streams that were explicitely programmed by
           the software. Because of this we cannot program the entire array of
           values, otherwise we'll get all streams reenabled, but rather program
           only those that are actully needed by the software. */
        if (elementCount != 0)
        {
            gctUINT base;
            gctUINT nopCount;
            gctUINT32_PTR nop;
            gctUINT fe2vsCount = 12;

            if ((((((gctUINT32) (Context->hardware->identity.chipMinorFeatures1)) >> (0 ? 23:23)) & ((gctUINT32) ((((1 ? 23:23) - (0 ? 23:23) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 23:23) - (0 ? 23:23) + 1)))))) ))
            {
                fe2vsCount = 16;
            }

            /* Determine the base index of the vertex stream array. */
            base = map[0x0180].index;

            /* Set the proper state count. */
            buffer->logical[base - 1]
                = ((((gctUINT32) (buffer->logical[base - 1])) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16))) | (((gctUINT32) ((gctUINT32) (elementCount ) & ((gctUINT32) ((((1 ? 25:16) - (0 ? 25:16) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 25:16) - (0 ? 25:16) + 1))))))) << (0 ? 25:16)));

            /* Determine the number of NOP commands. */
            nopCount
                = (fe2vsCount / 2)
                - (elementCount / 2);

            /* Determine the location of the first NOP. */
            nop = &buffer->logical[base + (elementCount | 1)];

            /* Fill the unused space with NOPs. */
            for (i = 0; i < nopCount; i += 1)
            {
                if (nop >= buffer->logical + Context->totalSize)
                {
                    break;
                }

                /* Generate a NOP command. */
                *nop = ((((gctUINT32) (0)) & ~(((gctUINT32) (((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27))) | (((gctUINT32) (0x03 & ((gctUINT32) ((((1 ? 31:27) - (0 ? 31:27) + 1) == 32) ? ~0 : (~(~0 << ((1 ? 31:27) - (0 ? 31:27) + 1))))))) << (0 ? 31:27)));

                /* Advance. */
                nop += 2;
            }
        }

        /* Reset pending deltas. */
        buffer->deltaCount = 0;
        buffer->delta      = gcvNULL;
    }

    /* Set state delta user pointer. */
    uDelta = StateDelta;

    /* Get access to the state delta. */
    gcmkONERROR(gckKERNEL_OpenUserData(
        kernel, needCopy,
        &_stateDelta,
        uDelta, gcmSIZEOF(gcsSTATE_DELTA),
        (gctPOINTER *) &kDelta
        ));

    /* State delta cannot be attached to anything yet. */
    if (kDelta->refCount != 0)
    {
        gcmkTRACE(
            gcvLEVEL_ERROR,
            "%s(%d): kDelta->refCount = %d (has to be 0).\n",
            __FUNCTION__, __LINE__,
            kDelta->refCount
            );
    }

    /* Attach to all contexts. */
    buffer = Context->buffer;

    do
    {
        /* Attach to the context if nothing is attached yet. If a delta
           is allready attached, all we need to do is to increment
           the number of deltas in the context. */
        if (buffer->delta == gcvNULL)
        {
            buffer->delta = uDelta;
        }

        /* Update reference count. */
        kDelta->refCount += 1;

        /* Update counters. */
        buffer->deltaCount += 1;

        /* Get the next context buffer. */
        buffer = buffer->next;

		if (buffer == gcvNULL)
		{
			gcmkONERROR(gcvSTATUS_NOT_FOUND);
		}
    }
    while (Context->buffer != buffer);

    /* Close access to the current state delta. */
    gcmkONERROR(gckKERNEL_CloseUserData(
        kernel, needCopy,
        gcvTRUE,
        uDelta, gcmSIZEOF(gcsSTATE_DELTA),
        (gctPOINTER *) &kDelta
        ));

    /* Schedule an event to mark the context buffer as available. */
    gcmkONERROR(gckEVENT_Signal(
        buffer->eventObj, buffer->signal, gcvKERNEL_PIXEL
        ));

    /* Advance to the next context buffer. */
    Context->buffer = buffer->next;

    /* Return the status. */
    gcmkFOOTER();
    return gcvSTATUS_OK;

OnError:
    /* Get access to the state records. */
	if (kDelta != gcvNULL)
	{
        gcmkVERIFY_OK(gckKERNEL_CloseUserData(
            kernel, needCopy,
            gcvFALSE,
            kDelta->recordArray, Context->recordArraySize,
            (gctPOINTER *) &recordArray
            ));
	}

    /* Close access to the current state delta. */
    gcmkVERIFY_OK(gckKERNEL_CloseUserData(
        kernel, needCopy,
        gcvTRUE,
        uDelta, gcmSIZEOF(gcsSTATE_DELTA),
        (gctPOINTER *) &kDelta
        ));

    /* Return the status. */
    gcmkFOOTER();
    return status;
#else
    return gcvSTATUS_OK;
#endif
}

