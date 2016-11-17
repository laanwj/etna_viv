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
from etnaviv.disasm import disasm_format

def parse_arguments():
    parser = argparse.ArgumentParser(description='Disassemble shader')
    parser.add_argument('--isa-file', metavar='ISAFILE', type=str, 
            help='Shader ISA definition file (rules-ng-ng)',
            default=rnndb_path('isa.xml'))
    parser.add_argument('input', metavar='INFILE', type=str, 
            help='Binary shader file')
    parser.add_argument('-a', dest='addr',
            default=True, action='store_const', const=False,
            help='Show no address with instructions')
    parser.add_argument('-r', dest='raw',
            default=False, action='store_const', const=True,
            help='Show raw data with instructions')
    parser.add_argument('-c', dest='cfmt',
            default=False, action='store_const', const=True,
            help='Output in format suitable for inclusion in C source')
    return parser.parse_args()

def main():
    args = parse_arguments()
    out = sys.stdout
    isa = parse_rng_file(args.isa_file)

    with open(args.input, 'rb') as f:
        data = f.read()
        if len(data)%16:
            print('Size of code must be multiple of 16.', file=sys.stderr)
            exit(1)
        disasm_format(out, isa, data, args.addr, args.raw, args.cfmt)

if __name__ == '__main__':
    main()


