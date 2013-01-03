#!/usr/bin/python
'''
De-tile an RGBX image.
'''
# Copyright (c) 2012-2013 Wladimir J. van der Laan
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

def parse_arguments():
    parser = argparse.ArgumentParser(description='"Slice" memory data from execution data log stream.')
    parser.add_argument('input', metavar='INFILE', type=str, 
            help='Texture raw file')
    parser.add_argument('output', metavar='OUTFILE', type=str, 
            help='Output image')
    parser.add_argument('-w', dest='img_width', type=int,
            help='Width of image to export')
    parser.add_argument('--tile-width', dest='tile_width', type=int,
            default=4, help='Width of a tile')
    parser.add_argument('--tile-height', dest='tile_height', type=int,
            default=4, help='Height of a tile')
    return parser.parse_args()        

def main():
    args = parse_arguments()
    with open(args.input, 'rb') as f:
        data = f.read()
    
    if args.img_width is None:
        print('Specify width of image with -w')
        exit(1)
    width = args.img_width
    height = len(data)//(width*4)

    TILE_WIDTH = args.tile_width
    TILE_HEIGHT = args.tile_height
    PIXEL_SIZE = 4
    TILE_BYTES = TILE_WIDTH * TILE_HEIGHT * PIXEL_SIZE

    out = bytearray(len(data))
    TILES_X = width // TILE_WIDTH
    TILES_Y = height // TILE_HEIGHT
    TILES_STRIDE = TILES_X * TILE_BYTES
    for ty in xrange(0,TILES_Y):
        for tx in xrange(0,TILES_X):
            for y in xrange(0, TILE_HEIGHT):
                for x in xrange(0, TILE_WIDTH):
                    dx = tx * TILE_WIDTH + x
                    dy = ty * TILE_HEIGHT + y
                    dst = (dy * width + dx) * PIXEL_SIZE
                    src_sup = ty * TILES_STRIDE + tx * TILE_BYTES
                    src_sub = (y * TILE_WIDTH + x) * PIXEL_SIZE
                    src = src_sup + src_sub
                    out[dst:dst+4] = data[src:src+4] 

    from PIL import Image
    img = Image.fromstring("RGBX", (width, height), str(out))
    img = img.convert("RGB")
    img.save(args.output)

if __name__ == '__main__':
    main()

