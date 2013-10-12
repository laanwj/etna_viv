/*
 * Copyright (c) 2012-2013 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <etnaviv/etna_fb.h>
#include <etnaviv/state.xml.h>
#include <etnaviv/state_3d.xml.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <string.h>

#include <errno.h>
#include <assert.h>

#include <linux/videodev2.h>

/* Structure to convert framebuffer format to RS destination conf */
struct etna_fb_format_desc
{
    unsigned bits_per_pixel;
    unsigned red_offset;
    unsigned red_length;
    unsigned green_offset;
    unsigned green_length;
    unsigned blue_offset;
    unsigned blue_length;
    unsigned alpha_offset;
    unsigned alpha_length;
    unsigned grayscale;
    unsigned rs_format;
    bool swap_rb;
};

static const struct etna_fb_format_desc etna_fb_formats[] = {
 /* bpp  ro  rl go gl bo  bl ao  al gs rs_format           swap_rb */
    {32, 16, 8, 8, 8, 0 , 8, 0,  0, 0, RS_FORMAT_X8R8G8B8, false},
    {32, 0 , 8, 8, 8, 16, 8, 0,  0, 0, RS_FORMAT_X8R8G8B8, true},
    {32, 16, 8, 8, 8, 0 , 8, 24, 8, 0, RS_FORMAT_A8R8G8B8, false},
    {32, 0 , 8, 8, 8, 16, 8, 24, 8, 0, RS_FORMAT_A8R8G8B8, true},
    {16, 8 , 4, 4, 4, 0,  4, 0,  0, 0, RS_FORMAT_X4R4G4B4, false},
    {16, 0 , 4, 4, 4, 8,  4, 0,  0, 0, RS_FORMAT_X4R4G4B4, true},
    {16, 8 , 4, 4, 4, 0,  4, 12, 4, 0, RS_FORMAT_A4R4G4B4, false},
    {16, 0 , 4, 4, 4, 8,  4, 12, 4, 0, RS_FORMAT_A4R4G4B4, true},
    {16, 10, 5, 5, 5, 0,  5, 0,  0, 0, RS_FORMAT_X1R5G5B5, false},
    {16, 0,  5, 5, 5, 10, 5, 0,  0, 0, RS_FORMAT_X1R5G5B5, true},
    {16, 10, 5, 5, 5, 0,  5, 15, 1, 0, RS_FORMAT_A1R5G5B5, false},
    {16, 0,  5, 5, 5, 10, 5, 15, 1, 0, RS_FORMAT_A1R5G5B5, true},
    {16, 11, 5, 5, 6, 0,  5, 0,  0, 0, RS_FORMAT_R5G6B5, false},
    {16, 0,  5, 5, 6, 11, 5, 0,  0, 0, RS_FORMAT_R5G6B5, true},
    {16, 0,  0, 0, 0, 0 , 0, 0,  0, V4L2_PIX_FMT_YUYV, RS_FORMAT_YUY2, false},
};

#define NUM_FB_FORMATS (sizeof(etna_fb_formats) / sizeof(etna_fb_formats[0]))

/* Get resolve format and swap red/blue format based on report on red/green/blue
 * bit positions from kernel.
 */
bool etna_fb_get_format(const struct fb_var_screeninfo *fb_var, unsigned *rs_format, bool *swap_rb)
{
    int fmt_idx=0;
    /* linear scan of table to find matching format */
    for(fmt_idx=0; fmt_idx<NUM_FB_FORMATS; ++fmt_idx)
    {
        const struct etna_fb_format_desc *desc = &etna_fb_formats[fmt_idx];
        if(desc->red_offset == fb_var->red.offset &&
            desc->red_length == fb_var->red.length &&
            desc->green_offset == fb_var->green.offset &&
            desc->green_length == fb_var->green.length &&
            desc->blue_offset == fb_var->blue.offset &&
            desc->blue_length == fb_var->blue.length &&
            (desc->alpha_offset == fb_var->transp.offset || desc->alpha_length == 0) &&
            desc->alpha_length == fb_var->transp.length &&
            desc->grayscale == fb_var->grayscale)
        {
            break;
        }
    }
    if(fmt_idx == NUM_FB_FORMATS)
    {
        printf("Unsupported framebuffer format: red_offset=%i red_length=%i green_offset=%i green_length=%i blue_offset=%i blue_length=%i trans_offset=%i transp_length=%i grayscale=%i\n",
                (int)fb_var->red.offset, (int)fb_var->red.length,
                (int)fb_var->green.offset, (int)fb_var->green.length,
                (int)fb_var->blue.offset, (int)fb_var->blue.length,
                (int)fb_var->transp.offset, (int)fb_var->transp.length,
                (int)fb_var->grayscale);
        return false;
    } else {
        printf("Framebuffer format: %i, flip_rb=%i\n",
                etna_fb_formats[fmt_idx].rs_format,
                etna_fb_formats[fmt_idx].swap_rb);
        *rs_format = etna_fb_formats[fmt_idx].rs_format;
        *swap_rb = etna_fb_formats[fmt_idx].swap_rb;
        return true;
    }
}

