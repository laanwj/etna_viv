#!/usr/bin/python
'''
Generate 'ETC1' texture with mipmaps in dds format from an image file.
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
import struct, sys, PIL, os
from PIL import Image
import argparse, tempfile
from subprocess import call

DDS_MAGIC = 0x20534444
DDS_UINT32 = b'<I'
FOURCC_ETC1 = struct.unpack(DDS_UINT32, b'ETC1')[0] # custom FOURCC
ETC1TOOL = 'etc1tool' # Android ETC1 tool, needs to be in PATH

def main():
    parser = argparse.ArgumentParser(description='Generate mipmaps from image and compress to ETC1 dds.')
    parser.add_argument('input', metavar='INFILE', type=str, 
            help='Input file (usually .png or .jpg)')
    parser.add_argument('output', metavar='OUTFILE', type=str, 
            help='Output file (.dds)')
    args = parser.parse_args()

    img = Image.open(args.input)
    orig_width = width = img.size[0]
    orig_height = height = img.size[1]
    assert(width > 0 and height > 0)

    (fd, tmpout) = tempfile.mkstemp(suffix='.png')
    os.close(fd) # won't be using the already-opened fd
    (fd, tmpout2) = tempfile.mkstemp(suffix='.pkm')
    os.close(fd)
    mip = 0
    mips = []
    while True:
        print('Generating mip %i: %ix%i' % (mip, width, height))
        img.save(tmpout)
        call([ETC1TOOL, '--encodeNoHeader', tmpout, '-o', tmpout2])

        with open(tmpout2, 'rb') as f:
            data = f.read()
        print('  compressed size: %i' % len(data))
        mips.append(data)
      
        # Go to next mip level
        if width == 1 and height == 1: # 1x1 reached
            break
        width = (width+1) >> 1
        height = (height+1) >> 1
        mip += 1
        img = img.resize((width, height), PIL.Image.ANTIALIAS)

    with open(args.output, 'wb') as f:
        # Write dds header
        hdrdata = [0] * (128//4)
        hdrdata[0] = DDS_MAGIC # dwMagic
        hdrdata[1] = 124 # dwSize
        hdrdata[2] = 0x000A1007 # dwFlags (CAPS, WIDTH, HEIGHT, PIXELFORMAT, MIPMAPCOUNT, LINEARSIZE)
        hdrdata[3] = orig_height # dwHeight
        hdrdata[4] = orig_width # dwWidth
        hdrdata[5] = len(mips[0]) # dwPitchOrLinearSize
        hdrdata[6] = 0 # dwDepth
        hdrdata[7] = len(mips) # dwMipMapCount
        hdrdata[19] = 32 # sPixelFormat.dwSize
        hdrdata[20] = 4 # sPixelFormat.dwFlags = DDPF_FOURCC
        hdrdata[21] = FOURCC_ETC1 # sPixelFormat.dwFourCC
        hdrdata[27] = 0x00401008 # sCaps.dwCaps1 (COMPLEX, TEXTURE, MIPMAP)
        f.write(struct.pack(b'<32I', *hdrdata))
        for mip in mips:
            f.write(mip)

if __name__ == '__main__':
    main()

