#!/usr/bin/python
'''
Shader disassembler.

Usage: disasm.py ../rnndb/isa.xml (vs|ps)_x.bin
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

from etnaviv.parse_rng import parse_rng_file, format_path, BitSet, Domain

# Register groups
# t temporary
# u uniform 0..127
# v uniform 127..255 (this is rewritten to u in format_src)
#  others are unknown
rgroups = ['t', '?1?', 'u', 'v', '?4?', '?5?', '?6?', '?7?']
# Addressing modes
amodes = ['', 'a.x', 'a.y', 'a.z', 'a.w', '?5?', '?6?', '?7?']
# components
COMPS = 'xyzw'

def bitextr(val, hi, lo):
    '''Extract and return bits hi..lo from value val'''
    return (val >> lo) & ((1<<(hi-lo+1))-1)
def format_swiz(swiz):
    swiz = [(swiz >> x)&3 for x in [0,2,4,6]]
    return ''.join([COMPS[c] for c in swiz])
def format_comps(comps):
    return ''.join([COMPS[c] for c in range(4) if ((comps >> c)&1)])

DstOperand = namedtuple('DstOperand', ['use', 'amode', 'reg', 'comps'])
SrcOperand = namedtuple('SrcOperand', ['use', 'reg', 'swiz', 'neg', 'abs', 'amode', 'rgroup'])
TexOperand = namedtuple('TexOperand', ['id', 'amode', 'swiz'])
Instruction = namedtuple('Instruction', ['op', 'cond', 'sat', 'tex', 'dst', 'src', 'unknowns'])

def disassemble(isa, inst):
    '''Parse four 32-bit instruction words into Instruction object'''
    op = bitextr(inst[0], 5, 0)
    cond = bitextr(inst[0], 10, 6)
    sat = bitextr(inst[0], 11, 11) # saturate

    dst = DstOperand(
        use = bitextr(inst[0], 12, 12), # desination used
        amode = bitextr(inst[0], 15, 13), # addressing mode
        reg = bitextr(inst[0], 22, 16), # reg nr
        comps = bitextr(inst[0], 26, 23) # xyzw
    )

    tex = TexOperand(
        id = bitextr(inst[0], 31, 27), # texture sampler id
        amode = bitextr(inst[1], 2, 0),
        swiz = bitextr(inst[1], 10, 3)
    )

    src = [
        SrcOperand(
            use = bitextr(inst[1], 11, 11),
            reg = bitextr(inst[1], 20, 12),
            swiz = bitextr(inst[1], 29, 22),
            neg = bitextr(inst[1], 30, 30),
            abs = bitextr(inst[1], 31, 31),
            amode = bitextr(inst[2], 2, 0), # addressing mode
            rgroup = bitextr(inst[2], 5, 3) # reg type (0=temp, 1=?, 2=uniform, 3=uniform)
        ),
        SrcOperand(
            use = bitextr(inst[2], 6, 6),
            reg = bitextr(inst[2], 15, 7),
            swiz = bitextr(inst[2], 24, 17),
            neg = bitextr(inst[2], 25, 25),
            abs = bitextr(inst[2], 26, 26),
            amode = bitextr(inst[2], 29, 27),
            rgroup = bitextr(inst[3], 2, 0)
        ),
        SrcOperand(
            use = bitextr(inst[3], 3, 3),
            reg = bitextr(inst[3], 12, 4),
            swiz = bitextr(inst[3], 21, 14),
            neg = bitextr(inst[3], 22, 22),
            abs = bitextr(inst[3], 23, 23),
            amode = bitextr(inst[3], 27, 25),
            rgroup = bitextr(inst[3], 30, 28)
        )
    ]

    # Unknown fields -- these must be 0
    unknowns = [
        ('bit_1_21', bitextr(inst[1], 21, 21)),
        ('bit_2_16', bitextr(inst[2], 16, 16)),
        ('bit_2_28', bitextr(inst[2], 31, 28)),
        ('bit_3_16', bitextr(inst[3], 13, 13)),
        ('bit_3_24', bitextr(inst[3], 24, 24)),
        ('bit_3_31', bitextr(inst[3], 31, 31))
    ]
    return Instruction(op=op,cond=cond,sat=sat,tex=tex,dst=dst,src=src,unknowns=unknowns)

def format_dst(isa, dst, warnings):
    '''Format destination operand'''
    if dst.use:
        # actually, target register group depends on the instruction, but usually it's a temporary...
        arg = 't%i' % (dst.reg)
        if dst.amode != 0:
            arg += '[%s]' % amodes[dst.amode]
        if dst.comps != 15: # if not all comps selected
            arg += '.' + format_comps(dst.comps)
    else:
        arg = 'void' # unused argument
        if dst.amode != 0 or dst.reg != 0 or dst.comps != 0:
            warnings.append('dst not used but fields non-zero')

    return arg

def format_src(isa, src, warnings):
    '''Format source operand'''
    if src.use:
        if src.rgroup == 3: # map vX to uniform u(X+128)
            rgroup = 2
            reg = 128 + src.reg
        else:
            rgroup = src.rgroup
            reg = src.reg
        arg = '%s%i' % (rgroups[rgroup], reg)
        if src.amode != 0:
            arg += '[%s]' % amodes[src.amode]
        if src.swiz != 0xe4: # if not null swizzle
            arg += '.' + format_swiz(src.swiz)
        # XXX is the - or the | done first? In a way, -|x| is the only ordering that makes sense.
        if src.abs:
            return '|' + arg + '|'
        if src.neg:
            return '-' + arg
    else:
        arg = 'void' # unused argument
        if src.reg != 0 or src.swiz != 0 or src.neg != 0 or src.abs != 0 or src.amode != 0 or src.rgroup != 0:
            warnings.append('src not used but fields non-zero')
    return arg

def format_tex(isa, tex, warnings):
    '''Format texture operand'''
    arg = 'tex%i' % (tex.id)
    if tex.amode != 0:
        arg += '[%i]' % amodes[tex.amode]
    if tex.swiz != 0xe4: # if not null swizzle
        arg += '.' + format_swiz(tex.swiz)

    return arg

def format_instruction(isa, inst, warnings):
    '''
    Format instruction as text.
    '''
    atoms = []
    args = []
    atoms.append(isa.types['INST_OPCODE'].describe(inst.op))
    if inst.cond:
        atoms.append(isa.types['INST_CONDITION'].describe(inst.cond))
    if inst.sat:
        atoms.append(sat)
    opcode = '.'.join(atoms)

    args.append(format_dst(isa, inst.dst, warnings))
    if inst.op in [0x18, 0x19, 0x1A, 0x1B, 0x1C]:
        args.append(format_tex(isa, inst.tex, warnings))
    else:
        if inst.tex.id != 0 or inst.tex.amode != 0 or inst.tex.swiz != 0:
            warnings.append('tex not used but fields non-zero')

    for src in inst.src:
        args.append(format_src(isa, src, warnings))

    # verify that all bits in unknown are 0
    for (name,value) in inst.unknowns:
        if value != 0:
            warnings.append('!%s=%i!' % (name,value))
    return opcode+' '+(', '.join(args))

def parse_arguments():
    parser = argparse.ArgumentParser(description='Disassemble shader')
    parser.add_argument('isa_file', metavar='ISAFILE', type=str, 
            help='Shader ISA definition file (rules-ng-ng)')
    parser.add_argument('input', metavar='INFILE', type=str, 
            help='Binary shader file')
    #parser.add_argument('-r', dest='raw_out', type=str,
    #        help='Export raw data to file')
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
            parsed = disassemble(isa, inst)
            warnings = []
            text = format_instruction(isa, parsed, warnings)
            out.write(text)
            if warnings:
                out.write(' ; ')
                out.write(' '.join(warnings))
            out.write('\n')

if __name__ == '__main__':
    main()


