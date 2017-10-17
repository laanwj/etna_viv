#!/usr/bin/python
'''
Etna shader disassembler/assembler common utils.
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

from etnaviv.parse_rng import parse_rng_file, format_path, BitSet, Domain
from etnaviv.floatutil import int_as_float, float_as_int
from etnaviv.asm_defs import Model, Flags

# Register groups
# t temporary
# u uniform 0..127
# v uniform 127..255 (this is rewritten to u in format_src)
# th temporary (high precision), used in DUAL16 mode
#  others are unknown
# rgroup 7 is used for immediate values on gc3000
RGROUPS = ['t', 'i', 'u', 'v', 'th', '?5?', '?6?', '?7?']
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
DstOperandMem = namedtuple('DstOperandMem', ['comps'])
SrcOperand = namedtuple('SrcOperand', ['use', 'reg', 'swiz', 'neg', 'abs', 'amode', 'rgroup'])
# immediate source operand: only GC3000+
SrcOperandImm = namedtuple('SrcOperandImm', ['use', 'imm'])
TexOperand = namedtuple('TexOperand', ['id', 'amode', 'swiz'])
# address operand to CALL/BRANCH: only GC2000
AddrOperand = namedtuple('AddrOperand', ['addr'])
Instruction = namedtuple('Instruction', ['op', 'cond', 'sat', 'tex', 'dst', 'src', 'addr', 'sel', 'unknowns', 'linenr', 'type'])

def extract_imm(fields, idx):
    '''
    Extract GC3000+ immediate operand field.
    '''
    if fields['SRC%i_RGROUP' % idx] != 7:
        return None
    rawval = (fields['SRC%i_REG' % idx] |
              (fields['SRC%i_SWIZ' % idx] << 9) |
              (fields['SRC%i_NEG' % idx] << 17) |
              (fields['SRC%i_ABS' % idx] << 18) |
              ((fields['SRC%i_AMODE' % idx] & 1) << 19))
    conv = fields['SRC%i_AMODE' % idx] >> 1
    if conv == 0: # f32
        return int_as_float(rawval << 12, 32)
    elif conv == 1: # s32
        if rawval < 0x80000:
            return rawval
        else:
            return rawval - 0x100000
    elif conv == 2: # u32
        return rawval
    else:
        return '0x%05' % rawval

def set_imm(fields, idx, value):
    '''
    Set GC3000+ immediate operand field.
    '''
    if isinstance(value, float):
        conv = 0 # f32
        rawval = float_as_int(value, 32) >> 12 # TODO rounding?
    elif isinstance(value, int):
        if value < 0: # s32
            conv = 1
            rawval = (value + 0x100000) & 0xfffff
        else: # u32
            conv = 2
            rawval = value
    else:
        raise ValueError('Immediate value must be float or int')
    assert(rawval >= 0 and rawval < 0x100000)
    fields['SRC%i_RGROUP' % idx] = 7
    fields['SRC%i_REG' % idx] = rawval & 0x1ff
    fields['SRC%i_SWIZ' % idx] = (rawval >> 9) & 0xff
    fields['SRC%i_NEG' % idx] = (rawval >> 17) & 1
    fields['SRC%i_ABS' % idx] = (rawval >> 18) & 1
    fields['SRC%i_AMODE' % idx] = (rawval >> 19) | (conv << 1)

def disassemble(isa, dialect, inst, warnings):
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
    op = fields['OPCODE'] | (fields['OPCODE_BIT6'] << 6)

    if op in [0x0A, 0x0B]: # Move to address register
        dst = DstOperandAReg(
            reg = fields['DST_REG'], # reg nr
            comps = fields['DST_COMPS'] # xyzw
        )
        if fields['DST_AMODE'] != 0 or fields['DST_USE'] != 0:
            warnings.append('use and amode bitfields are nonzero for areg (amode=%d,use=%d)' % (fields['DST_AMODE'], fields['DST_USE']))
    elif op in [0x33]: # Store
        dst = DstOperandMem(
            comps = fields['DST_COMPS'] # xyzw
        )
        if fields['DST_AMODE'] != 0 or fields['DST_USE'] != 0 or fields['DST_REG'] != 0:
            warnings.append('use or amode or reg bitfields are nonzero for store (amode=%d,use=%d,reg=%d)' % (fields['DST_AMODE'], fields['DST_USE'], fields['DST_REG']))
    else:
        dst = DstOperand(
            use = fields['DST_USE'], # destination used
            amode = fields['DST_AMODE'], # addressing mode
            reg = fields['DST_REG'], # reg nr
            comps = fields['DST_COMPS'] # xyzw
        )
        if not dst.use:
            if dst.amode != 0 or dst.reg != 0 or dst.comps != 0:
                warnings.append('dst not used but fields non-zero (amode=%d,reg=%d,comps=%d)' % (dst.amode,dst.reg,dst.comps))
            dst = None

    tex = TexOperand(
        id = fields['TEX_ID'], # texture sampler id
        amode = fields['TEX_AMODE'],
        swiz = fields['TEX_SWIZ']
    )
    if op not in [0x18, 0x19, 0x1A, 0x1B, 0x1C]: # tex op
        if tex.id != 0 or tex.amode != 0 or tex.swiz != 0:
            warnings.append('tex not used but fields non-zero (id=%d,amode=%d,swiz=%d)' % (tex.id,tex.amode,tex.swiz))
        tex = None

    if dialect.model <= Model.GC2000 and op in [0x14, 0x16]: # CALL, BRANCH
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
        if fields['SRC%i_RGROUP' % idx] == 7: # immediate argument
            operand = SrcOperandImm(
                use = fields['SRC%i_USE' % idx],
                imm = extract_imm(fields, idx)
            )
            if not operand.use and operand.imm != 0:
                warnings.append('src%i not used but fields non-zero (rgroup=%d,imm=%s)' % (idx, 7, operand.imm))
                operand = None
        else: # register argument
            operand = SrcOperand(
                use = fields['SRC%i_USE' % idx], reg = fields['SRC%i_REG' % idx],
                swiz = fields['SRC%i_SWIZ' % idx], neg = fields['SRC%i_NEG' % idx],
                abs = fields['SRC%i_ABS' % idx], amode = fields['SRC%i_AMODE' % idx],
                rgroup = fields['SRC%i_RGROUP' % idx]
            )
            if not operand.use:
                if operand.reg != 0 or operand.swiz != 0 or operand.neg != 0 or operand.abs != 0 or operand.amode != 0 or operand.rgroup != 0:
                    warnings.append('src%i not used but fields non-zero (reg=%d,swiz=%d,neg=%d,abs=%d,amode=%d,rgroup=%d)' % (idx, operand.reg, operand.swiz, operand.neg, operand.abs, operand.amode, operand.rgroup))
                operand = None

        src.append(operand)

    # Type
    type_ = (fields['TYPE_BIT2'] << 2) | fields['TYPE_BIT01']

    # Thread selector
    if dialect.flags & Flags.DUAL16:
        sel = (fields['SEL_BIT1']<<1) | fields['SEL_BIT0']
    else:
        sel = None

    # Unknown fields -- will warn if these are not 0
    unknowns = [
        ('bit_3_31', fields['UNK3_31'])
    ]

    if not (dialect.flags & Flags.DUAL16):
        unknowns.append(('bit_3_24', fields['SEL_BIT1']))
        if addr is None: # bit13 may be set if addr (old style immediate) operand 2
            unknowns.append(('bit_3_13', fields['SEL_BIT0']))

    # verify that all bits in unknown are 0
    for (name,value) in unknowns:
        if value != 0:
            warnings.append('!%s=%i!' % (name,value))
    return Instruction(op=op,
            cond=fields['COND'],sat=fields['SAT'],type=type_,
            tex=tex,dst=dst,src=src,addr=addr,sel=sel,unknowns=unknowns,linenr=None)

def format_dst(isa, dst):
    '''Format destination operand'''
    if dst is not None:
        if isinstance(dst, DstOperand):
            arg = 't%i' % (dst.reg)
            if dst.amode != 0:
                arg += '[%s]' % AMODES[dst.amode]
            if dst.comps != 15: # if not all comps selected
                arg += '.' + format_comps(dst.comps)
        elif isinstance(dst, DstOperandAReg):
            arg = 'a%i' % (dst.reg)
            if dst.comps != 15: # if not all comps selected
                arg += '.' + format_comps(dst.comps)
        elif isinstance(dst, DstOperandMem):
            arg = '%s.%s' % ('mem', format_comps(dst.comps))
        else:
            raise NotImplementedError
    else:
        arg = 'void' # unused argument

    return arg

def format_src(isa, src):
    '''Format source operand'''
    if isinstance(src, SrcOperand):
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
    elif isinstance(src, SrcOperandImm): # gc3000 immediate
        return str(src.imm)
    else:
        arg = 'void' # unused argument
    return arg

def format_tex(isa, tex):
    '''Format texture operand'''
    arg = 'tex%i' % (tex.id)
    if tex.amode != 0:
        arg += '[%i]' % AMODES[tex.amode]
    if tex.swiz != 0xe4: # if not null swizzle
        arg += '.' + format_swiz(tex.swiz)

    return arg

def format_addr(isa, addr):
    return '%i' % (addr.addr)

def format_instruction(isa, dialect, inst):
    '''
    Format instruction as text.
    '''
    atoms = []
    args = []
    atoms.append(isa.types['INST_OPCODE'].describe(inst.op))
    if inst.cond:
        atoms.append(isa.types['INST_CONDITION'].describe(inst.cond))
    if inst.type:
        atoms.append(isa.types['INST_TYPE'].describe(inst.type))
    if inst.sat:
        atoms.append('SAT')
    if inst.sel: # Thread selector
        atoms.append('S%d' % (inst.sel - 1))
    opcode = '.'.join(atoms)

    args.append(format_dst(isa, inst.dst))

    if inst.tex is not None:
        args.append(format_tex(isa, inst.tex))

    for src in inst.src:
        args.append(format_src(isa, src))

    if inst.addr is not None:
        args.append(format_addr(isa, inst.addr))

    return opcode.lower()+'\t'+(', '.join(args))

