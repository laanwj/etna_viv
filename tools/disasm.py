#!/usr/bin/python
'''
Shader disassembler.

Usage: disasm.py --isa-file ../rnndb/isa.xml (vs|ps)_x.bin
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
import sys
from binascii import b2a_hex
from collections import namedtuple 

from etnaviv.util import rnndb_path
from etnaviv.parse_rng import parse_rng_file, format_path, BitSet, Domain
from etnaviv.asm_common import format_instruction, disassemble

def parse_arguments():
    parser = argparse.ArgumentParser(description='Disassemble shader')
    parser.add_argument('--isa-file', metavar='ISAFILE', type=str, 
            help='Shader ISA definition file (rules-ng-ng)',
            default=rnndb_path('isa.xml'))
    parser.add_argument('input', metavar='INFILE', type=str, 
            help='Binary shader file')
    parser.add_argument('-a', dest='addr',
            default=False, action='store_const', const=True,
            help='Show address with instructions')
    parser.add_argument('-r', dest='raw',
            default=False, action='store_const', const=True,
            help='Show raw data with instructions')
    return parser.parse_args()        

def main():
    args = parse_arguments()
    out = sys.stdout
    isa = parse_rng_file(args.isa_file)

    with open(args.input, 'rb') as f:
        data = f.read()
        if len(data)%16:
            print >>sys.stderr,'Size of code must be multiple of 16.'
            exit(1)
        for idx in xrange(len(data)//16):
            inst = struct.unpack(b'<IIII', data[idx*16:idx*16+16])
            if args.addr:
                out.write('%3x: ' % idx)
            if args.raw:
                out.write('%08x %08x %08x %08x  ' % inst)
            warnings = []
            parsed = disassemble(isa, inst, warnings)
            text = format_instruction(isa, parsed)
            out.write(text)
            if warnings:
                out.write(' ; ')
                out.write(' '.join(warnings))
            out.write('\n')

if __name__ == '__main__':
    main()


