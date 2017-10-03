#!/usr/bin/python
'''
Dump memory buffer at certain point in execution data log stream.
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

# Parse execution data log files
from etnaviv.parse_fdr import ENDIAN, WORD_SPEC, WORD_CHAR, FDRLoader, Event

def parse_arguments():
    parser = argparse.ArgumentParser(description='"Slice" memory data from execution data log stream.')
    parser.add_argument('input', metavar='INFILE', type=str, 
            help='FDR file')
    parser.add_argument('seq', metavar='SEQ', type=str, 
            help='Event sequence #')
    parser.add_argument('addr', metavar='ADDR', type=str, 
            help='Starting address')
    parser.add_argument('size', metavar='SIZE', type=str, 
            help='Memory block size')
    parser.add_argument('addr2', metavar='ADDR2', type=str, 
            default='0', nargs='?',
            help='Starting address (physical)')
    parser.add_argument('-r', dest='raw_out', type=str,
            help='Export raw data to file')
    parser.add_argument('-i', dest='img_out', type=str,
            help='Export image to file (type is determined by extension)')
    parser.add_argument('-w', dest='img_width', type=int,
            help='Width of image to export')
    return parser.parse_args()        

def main():
    args = parse_arguments()
    args.seq = int(args.seq,0)
    args.addr = int(args.addr,0)
    args.size = int(args.size,0)
    args.addr2 = int(args.addr2,0)
    fdr = FDRLoader(args.input)
    if args.raw_out is None and args.img_out is None:
        print('%-8s %-8s %-8s  %s' % ('log', 'phys', 'int', 'float'))
    for seq,rec in enumerate(fdr):
        if isinstance(rec, Event):
            if seq == args.seq:
                data = fdr[args.addr:args.addr+args.size] # extract data
                if args.img_out is not None:
                    if args.img_width is None:
                        print('Specify width of image with -w')
                        exit(1)
                    from PIL import Image
                    width = args.img_width
                    height = len(data) // (width*4)
                    img = Image.fromstring("RGBX", (width, height), data)
                    img = img.convert("RGB")
                    img.save(args.img_out)
                elif args.raw_out is not None:
                    with open(args.raw_out, 'wb') as f:
                        f.write(data)
                else:
                    s = args.size//4
                    for x in xrange(s):
                        print('%08x %08x %08x %6.3f' % (args.addr+x*4, args.addr2+x*4, 
                            struct.unpack(b'I', data[x*4:x*4+4])[0], 
                            struct.unpack(b'f', data[x*4:x*4+4])[0]))


if __name__ == '__main__':
    main()

