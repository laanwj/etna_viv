#!/usr/bin/python
'''
Etna shader disassembler/assembler common utils.
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
RGROUPS = ['t', 'i', 'u', 'v', '?4?', '?5?', '?6?', '?7?']
# Addressing modes
AMODES = ['', 'a.x', 'a.y', 'a.z', 'a.w', '?5?', '?6?', '?7?']
# components
COMPS = 'xyzw'

def format_swiz(swiz):
    swiz = [(swiz >> x)&3 for x in [0,2,4,6]]
    return ''.join([COMPS[c] for c in swiz])
def format_comps(comps):
    return ''.join([['_',COMPS[c]][(comps >> c)&1] for c in range(4)])

DstOperand = namedtuple('DstOperand', ['use', 'amode', 'reg', 'comps'])
DstOperandAReg = namedtuple('DstOperandAReg', ['reg', 'comps'])
SrcOperand = namedtuple('SrcOperand', ['use', 'reg', 'swiz', 'neg', 'abs', 'amode', 'rgroup'])
TexOperand = namedtuple('TexOperand', ['id', 'amode', 'swiz'])
AddrOperand = namedtuple('AddrOperand', ['addr'])
Instruction = namedtuple('Instruction', ['op', 'cond', 'sat', 'tex', 'dst', 'src', 'addr', 'unknowns', 'linenr'])

def disassemble(isa, inst, warnings):
    '''Parse four 32-bit instruction words into Instruction object'''
    # Extract bit fields using ISA
    domain = isa.lookup_domain('VIV_ISA')
    fields = {}
    for word in [0,1,2,3]:
        mask = 0
        bitset = domain.lookup_address(word*4)[-1][0].type
        for field in bitset.bitfields:
            fields[field.name] = field.extract(inst[word])
            mask |= field.mask
        if mask != 0xffffffff:
            warnings.append('isa for word %i incomplete' % word)
    op = fields['OPCODE']

    if op in [0x0A, 0x0B]: # Move to address register
        dst = DstOperandAReg(
            reg = fields['DST_REG'], # reg nr
            comps = fields['DST_COMPS'] # xyzw
        )
        if fields['DST_AMODE'] != 0 or fields['DST_USE'] != 0:
            warnings.append('use and amode bitfields are nonzero for areg')
    else:
        dst = DstOperand(
            use = fields['DST_USE'], # destination used
            amode = fields['DST_AMODE'], # addressing mode
            reg = fields['DST_REG'], # reg nr
            comps = fields['DST_COMPS'] # xyzw
        )
        if not dst.use:
            if dst.amode != 0 or dst.reg != 0 or dst.comps != 0:
                warnings.append('dst not used but fields non-zero')
            dst = None

    tex = TexOperand(
        id = fields['TEX_ID'], # texture sampler id
        amode = fields['TEX_AMODE'],
        swiz = fields['TEX_SWIZ']
    )
    if op not in [0x18, 0x19, 0x1A, 0x1B, 0x1C]: # tex op
        if tex.id != 0 or tex.amode != 0 or tex.swiz != 0:
            warnings.append('tex not used but fields non-zero')
        tex = None

    if op in [0x14, 0x16]: # CALL, BRANCH
        # Address (immediate) operand takes the place of src2
        addr = AddrOperand(fields['SRC2_IMM'])
    else:
        addr = None

    # Determine number of source operands
    num_src = 3
    if addr is not None: # src2 is invalid when address operand used
        num_src = 2

    src = []
    for idx in xrange(num_src):
        operand = SrcOperand(
            use = fields['SRC%i_USE' % idx], reg = fields['SRC%i_REG' % idx],
            swiz = fields['SRC%i_SWIZ' % idx], neg = fields['SRC%i_NEG' % idx],
            abs = fields['SRC%i_ABS' % idx], amode = fields['SRC%i_AMODE' % idx],
            rgroup = fields['SRC%i_RGROUP' % idx]
        )
        if not operand.use:
            if operand.reg != 0 or operand.swiz != 0 or operand.neg != 0 or operand.abs != 0 or operand.amode != 0 or operand.rgroup != 0:
                warnings.append('src%i not used but fields non-zero' % idx)
            operand = None

        src.append(operand)

    # Unknown fields -- will warn if these are not 0
    unknowns = [
        ('bit_1_21', fields['UNK1_21']), ('bit_2_16', fields['UNK2_16']),
        ('bit_2_30', fields['UNK2_30']), ('bit_3_24', fields['UNK3_24']),
        ('bit_3_31', fields['UNK3_31'])
    ]
    if addr is None: # bit13 may be set if immediate operand 2
        unknowns.append(('bit_3_13', fields['UNK3_13']))
    # verify that all bits in unknown are 0
    for (name,value) in unknowns:
        if value != 0:
            warnings.append('!%s=%i!' % (name,value))
    return Instruction(op=op,
            cond=fields['COND'],sat=fields['SAT'],
            tex=tex,dst=dst,src=src,addr=addr,unknowns=unknowns,linenr=None)

def format_dst(isa, dst):
    '''Format destination operand'''
    if dst is not None:
        # actually, target register group depends on the instruction, but usually it's a temporary...
        arg = 't%i' % (dst.reg)
        if dst.amode != 0:
            arg += '[%s]' % amodes[dst.amode]
        if dst.comps != 15: # if not all comps selected
            arg += '.' + format_comps(dst.comps)
    else:
        arg = 'void' # unused argument

    return arg

def format_dst_areg(isa, dst):
    '''Format destination operand'''
    arg = 'a%i' % (dst.reg)
    if dst.comps != 15: # if not all comps selected
        arg += '.' + format_comps(dst.comps)

    return arg

def format_src(isa, src):
    '''Format source operand'''
    if src is not None:
        if src.rgroup == 3: # map vX to uniform u(X+128)
            rgroup = 2
            reg = 128 + src.reg
        else:
            rgroup = src.rgroup
            reg = src.reg
        arg = '%s%i' % (RGROUPS[rgroup], reg)
        if src.amode != 0:
            arg += '[%s]' % AMODES[src.amode]
        if src.swiz != 0xe4: # if not null swizzle
            arg += '.' + format_swiz(src.swiz)
        if src.abs:
            return '|' + arg + '|'
        if src.neg:
            return '-' + arg
    else:
        arg = 'void' # unused argument
    return arg

def format_tex(isa, tex):
    '''Format texture operand'''
    arg = 'tex%i' % (tex.id)
    if tex.amode != 0:
        arg += '[%i]' % amodes[tex.amode]
    if tex.swiz != 0xe4: # if not null swizzle
        arg += '.' + format_swiz(tex.swiz)

    return arg

def format_addr(isa, addr):
    return 'label_%x' % (addr.addr)

def format_instruction(isa, inst):
    '''
    Format instruction as text.
    '''
    atoms = []
    args = []
    atoms.append(isa.types['INST_OPCODE'].describe(inst.op))
    if inst.cond:
        atoms.append(isa.types['INST_CONDITION'].describe(inst.cond))
    if inst.sat:
        atoms.append('SAT')
    opcode = '.'.join(atoms)

    if isinstance(inst.dst, DstOperandAReg):
        args.append(format_dst_areg(isa, inst.dst))
    else:
        args.append(format_dst(isa, inst.dst))

    if inst.tex is not None:
        args.append(format_tex(isa, inst.tex))

    for src in inst.src:
        args.append(format_src(isa, src))

    if inst.addr is not None:
        args.append(format_addr(isa, inst.addr))

    return opcode+' '+(', '.join(args))

