#!/usr/bin/env python3
'''
De-tile an RGBX image.
'''
# Copyright (c) 2012-2017 Wladimir J. van der Laan
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sub license,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the
# next paragraph) shall be included in all copies or substantial portions
# of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
from __future__ import print_function, division, unicode_literals
import argparse,struct
from binascii import b2a_hex
from PIL import Image

# This is one of the Vivante supertiling layouts. Every number is a tile number in the
# supertile for the tile at that x,y.
supertile_layout = [
    [  0,   1,   8,   9,  16,  17,  24,  25,  32,  33,  40,  41,  48,  49,  56,  57],
    [  2,   3,  10,  11,  18,  19,  26,  27,  34,  35,  42,  43,  50,  51,  58,  59],
    [  4,   5,  12,  13,  20,  21,  28,  29,  36,  37,  44,  45,  52,  53,  60,  61],
    [  6,   7,  14,  15,  22,  23,  30,  31,  38,  39,  46,  47,  54,  55,  62,  63],
    [ 64,  65,  72,  73,  80,  81,  88,  89,  96,  97, 104, 105, 112, 113, 120, 121],
    [ 66,  67,  74,  75,  82,  83,  90,  91,  98,  99, 106, 107, 114, 115, 122, 123],
    [ 68,  69,  76,  77,  84,  85,  92,  93, 100, 101, 108, 109, 116, 117, 124, 125],
    [ 70,  71,  78,  79,  86,  87,  94,  95, 102, 103, 110, 111, 118, 119, 126, 127],
    [128, 129, 136, 137, 144, 145, 152, 153, 160, 161, 168, 169, 176, 177, 184, 185],
    [130, 131, 138, 139, 146, 147, 154, 155, 162, 163, 170, 171, 178, 179, 186, 187],
    [132, 133, 140, 141, 148, 149, 156, 157, 164, 165, 172, 173, 180, 181, 188, 189],
    [134, 135, 142, 143, 150, 151, 158, 159, 166, 167, 174, 175, 182, 183, 190, 191],
    [192, 193, 200, 201, 208, 209, 216, 217, 224, 225, 232, 233, 240, 241, 248, 249],
    [194, 195, 202, 203, 210, 211, 218, 219, 226, 227, 234, 235, 242, 243, 250, 251],
    [196, 197, 204, 205, 212, 213, 220, 221, 228, 229, 236, 237, 244, 245, 252, 253],
    [198, 199, 206, 207, 214, 215, 222, 223, 230, 231, 238, 239, 246, 247, 254, 255],
]

def parse_arguments():
    parser = argparse.ArgumentParser(description='"Slice" memory data from execution data log stream.')
    parser.add_argument('input', metavar='INFILE', type=str, 
            help='Texture raw file')
    parser.add_argument('output', metavar='OUTFILE', type=str, 
            help='Output image')
    parser.add_argument('-w', dest='img_width', type=int,
            help='Width of image to export')
    parser.add_argument('-r', '--raw', dest='raw',
            default=False, action='store_true',
            help='Raw input')
    parser.add_argument('-t', '--tile', dest='tile',
            default=False, action='store_true',
            help='Tile instead of detile')
    parser.add_argument('-s', '--supertiled', dest='supertiled',
            default=False, action='store_true',
            help='Supertiled mode')
    parser.add_argument('--tile-width', dest='tile_width', type=int,
            default=4, help='Width of a tile')
    parser.add_argument('--tile-height', dest='tile_height', type=int,
            default=4, help='Height of a tile')
    return parser.parse_args()        

def rgb_to_rgbx_raw(data_in):
    out = bytearray(len(data_in)*4)
    for i,x in enumerate(data_in):
        out[i*4+0] = x[0]
        out[i*4+1] = x[1]
        out[i*4+2] = x[2]
        out[i*4+3] = 255
    return out

def do_tile(tile, out, data, src_sup, basedx, basedy, dwidth, TILE_WIDTH, TILE_HEIGHT, PIXEL_SIZE):
    for y in range(0, TILE_HEIGHT):
        for x in range(0, TILE_WIDTH):
            dst = ((basedy + y) * dwidth + (basedx + x)) * PIXEL_SIZE
            src = src_sup + (y * TILE_WIDTH + x) * PIXEL_SIZE
            if tile: # tile
                out[src:src+4] = data[dst:dst+4] 
            else: # untile
                out[dst:dst+4] = data[src:src+4] 

def main():
    args = parse_arguments()
    PIXEL_SIZE = 4

    if args.raw:
        with open(args.input, 'rb') as f:
            data = f.read()
        
        if args.img_width is None:
            print('Specify width of image with -w')
            exit(1)
        width = args.img_width
        height = len(data)//(width*4)
    else:
        img = Image.open(args.input)
        data = rgb_to_rgbx_raw(img.getdata())
        if args.img_width is None:
            width = img.width
            height = img.height
        else:
            width = args.img_width
            height = len(data)//(width*PIXEL_SIZE)

    TILE_WIDTH = args.tile_width
    TILE_HEIGHT = args.tile_height
    TILE_BYTES = TILE_WIDTH * TILE_HEIGHT * PIXEL_SIZE

    out = bytearray(len(data))
    TILES_X = width // TILE_WIDTH
    TILES_Y = height // TILE_HEIGHT
    TILES_STRIDE = TILES_X * TILE_BYTES
    print('%dx%d %dx%d tiles' % (TILES_X,TILES_Y,TILE_WIDTH,TILE_HEIGHT))

    if args.supertiled:
        TILES_PER_SUPERTILE_W = len(supertile_layout[0])
        TILES_PER_SUPERTILE_H = len(supertile_layout)
        SUPERTILE_WIDTH = TILES_PER_SUPERTILE_W * TILE_WIDTH
        SUPERTILE_HEIGHT = TILES_PER_SUPERTILE_H * TILE_HEIGHT
        SUPERTILES_X = width // SUPERTILE_WIDTH
        SUPERTILES_Y = height // SUPERTILE_HEIGHT
        SUPERTILE_BYTES = SUPERTILE_WIDTH * SUPERTILE_HEIGHT * PIXEL_SIZE
        print('%dx%d %dx%d supertiles' % (SUPERTILES_X,SUPERTILES_Y, SUPERTILE_WIDTH, SUPERTILE_HEIGHT))
        for sy in range(0,SUPERTILES_Y):
            for sx in range(0,SUPERTILES_X):
                inofs_st = (sy * SUPERTILES_X + sx) * SUPERTILE_BYTES
                for ty in range(0, TILES_PER_SUPERTILE_H):
                    for tx in range(0, TILES_PER_SUPERTILE_W):
                        do_tile(args.tile, out, data,
                                inofs_st + supertile_layout[ty][tx] * TILE_BYTES,
                                sx * SUPERTILE_WIDTH + tx * TILE_WIDTH,
                                sy * SUPERTILE_HEIGHT + ty * TILE_HEIGHT,
                                width, TILE_WIDTH, TILE_HEIGHT, PIXEL_SIZE)

    else:
        for ty in range(0,TILES_Y):
            for tx in range(0,TILES_X):
                do_tile(args.tile, out, data,
                        ty * TILES_STRIDE + tx * TILE_BYTES,
                        tx * TILE_WIDTH,
                        ty * TILE_HEIGHT,
                        width, TILE_WIDTH, TILE_HEIGHT, PIXEL_SIZE)

    img = Image.frombytes("RGBX", (width, height), bytes(out))
    img = img.convert("RGB")
    img.save(args.output)

if __name__ == '__main__':
    main()

