#!/usr/bin/python
'''
Shader disassembler.

Usage: disasm.py --isa-file ../rnndb/isa.xml (vs|ps)_x.bin
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
import sys
from binascii import b2a_hex
from collections import namedtuple 

from etnaviv.util import rnndb_path
from etnaviv.parse_rng import parse_rng_file, format_path, BitSet, Domain
from etnaviv.disasm import disasm_format
from etnaviv.asm_defs import Model, Flags, Dialect

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
    parser.add_argument('-t', dest='ifmt',
            default=False, action='store_const', const=True,
            help='Input comma-separated integers instead of binary shader')
    parser.add_argument('-m', dest='model',
            type=str, default='GC2000',
            help='GPU type to disassemble for (GC2000 or GC3000, default GC3000)')
    parser.add_argument('--isa-flags', dest='isa_flags',
            type=str, default='',
            help=('ISA flags (available: %s)' % Flags.available()))
    return parser.parse_args()

def do_disasm(out, isa, dialect, args, f):
    if not args.ifmt:
        data = f.read()
    else: # 0x00841001, 0x00202800, 0x80000000, 0x00000038
        text = f.read()
        text = [x.strip() for x in text.split(b',')] # split by ',' remote whitespace
        if text and not text[-1]: # remove empty trailer
            text = text[0:-1]
        data = b''.join(struct.pack(b'<I', int(x,0)) for x in text)
    if len(data)%16:
        print('Size of code must be multiple of 16.', file=sys.stderr)
        exit(1)

    disasm_format(out, isa, dialect, data, args.addr, args.raw, args.cfmt)

def main():
    args = parse_arguments()
    out = sys.stdout
    isa = parse_rng_file(args.isa_file)
    try:
        model = Model.by_name[args.model.upper()]
    except KeyError:
        print('Unknown model identifier %s' % args.model, file=sys.stderr)
        exit(1)
    try:
        flags = Flags.from_str(args.isa_flags)
    except KeyError:
        print('Unknown ISA flag identifier %s' % args.isa_flags, file=sys.stderr)
        exit(1)
    dialect = Dialect(model, flags)

    if args.input == '-':
        do_disasm(out, isa, dialect, args, sys.stdin)
    else:
        with open(args.input, 'rb') as f:
            do_disasm(out, isa, dialect, args, f)

if __name__ == '__main__':
    main()


