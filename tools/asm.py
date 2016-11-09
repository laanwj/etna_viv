#!/usr/bin/python
'''
Shader assembler.

Usage: asm.py --isa-file ../rnndb/isa.xml in.asm out.bin
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
import re

from etnaviv.util import rnndb_path
from etnaviv.parse_rng import parse_rng_file, format_path, BitSet, Domain
from etnaviv.asm_common import DstOperand, DstOperandAReg, DstOperandMem, SrcOperand, TexOperand, AddrOperand, Instruction, AMODES, COMPS, RGROUPS
from etnaviv.asm_common import disassemble, format_instruction

reg_re = re.compile('^(i|t|u|a|tex|\?4\?|\?5\?|\?6\?|\?7\?)(\d+)(\[.*?\])?(\.[\_xyzw]{1,4})?$')
mem_re = re.compile('^mem(\.[\_xyzw]{1,4})?$')
label_re = re.compile('^[0-9a-zA-Z\-\_]+$')

def parse_amode(amode):
    if not amode:
        return 0
    return AMODES.index(amode[1:-1])

def parse_comps(comps):
    if not comps:
        return 15
    return ((('x' in comps)<<0)|(('y' in comps)<<1)|(('z' in comps)<<2)|(('w' in comps)<<3))

def parse_swiz(swiz):
    if not swiz:
        return 0xe4
    swiz = swiz[1:] # drop .
    rv = 0
    for idx in xrange(4):
        if idx < len(swiz):
            comp = COMPS.index(swiz[idx])
        rv |= comp << (idx * 2)
    return rv

def assemble(isa, inst, warnings):
    fields = {}
    fields['OPCODE'] = inst.op & 0x3F
    fields['OPCODE_BIT6'] = (inst.op >> 6) & 0x01
    fields['COND'] = inst.cond
    fields['SAT'] = inst.sat
    fields['TYPE_BIT2'] = inst.type >> 2
    fields['TYPE_BIT01'] = inst.type & 3
   
    if isinstance(inst.dst, DstOperandAReg):
        # XXX validate that this instruction accepts
        # address destination arguments
        fields['DST_REG'] = inst.dst.reg
        fields['DST_COMPS'] = inst.dst.comps
    elif isinstance(inst.dst, DstOperandMem):
        fields['DST_COMPS'] = inst.dst.comps
    elif isinstance(inst.dst, DstOperand):
        fields['DST_USE'] = inst.dst.use
        fields['DST_AMODE'] = inst.dst.amode
        fields['DST_REG'] = inst.dst.reg
        fields['DST_COMPS'] = inst.dst.comps
    elif inst.dst is None:
        fields['DST_USE'] = 0
    else:
        warnings.append('Invalid destination argument')

    if inst.tex is not None:
        fields['TEX_ID'] = inst.tex.id
        fields['TEX_AMODE'] = inst.tex.amode
        fields['TEX_SWIZ'] = inst.tex.swiz

    if inst.addr is not None:
        fields['SRC2_IMM'] = inst.addr.addr

    for (idx, src) in enumerate(inst.src):
        if src is not None:
            fields['SRC%i_USE' % idx] = src.use
            fields['SRC%i_REG' % idx] = src.reg
            fields['SRC%i_SWIZ' % idx] = src.swiz
            fields['SRC%i_NEG' % idx] = src.neg
            fields['SRC%i_ABS' % idx] = src.abs
            fields['SRC%i_AMODE' % idx] = src.amode
            fields['SRC%i_RGROUP' % idx] = src.rgroup

    # XXX check for colliding fields
    domain = isa.lookup_domain('VIV_ISA')
    rv = [0,0,0,0]
    for word in [0,1,2,3]:
        mask = 0
        bitset = domain.lookup_address(word*4)[-1][0].type
        for field in bitset.bitfields:
            if field.name in fields:
                try:
                    rv[word] |= field.fill(fields[field.name])
                    del fields[field.name]
                except ValueError,e:
                    warnings.append(str(e))
    for field in fields.iterkeys(): # warn if fields are not used, that's probably a typo
        warnings.append('Field %s not used' % field)
    return rv

class Assembler(object):
    '''
    Instruction assembler context.
    '''
    labels = {}
    linenr = 0
    instructions = None
    source = None
    def __init__(self, isa):
        self.isa = isa
        self.errors = []
        self.instructions = []
        self.source = []

    def parse(self, line):
        # remove comment
        self.source.append(line)
        self.linenr += 1

        (line, _, _) = line.partition(';') # drop comments

        (label, _, line) = line.rpartition(':') # handle optional labels
        if label:
            label = label.strip()
            if not label_re.match(label):
                self.errors.append((self.linenr, 'Invalid label: %s' % label))
            self.labels[label] = len(self.instructions)

        line = line.strip()
        if not line: # empty line
            return None
       
        (inst, _, operands) = line.partition(' ')
        m = re.match('\s*([a-zA-Z0-9\.]+)\s*(.*?)\s*$', line)
        if not m:
            self.errors.append((self.linenr, 'Cannot parse line: %s' % line))
            return None
        inst = m.group(1)
        operands = m.group(2)

        # uppercase, split into atoms
        inst = inst.upper().split('.')
        try:
            op = self.isa.types['INST_OPCODE'].values_by_name[inst[0]].value
        except KeyError:
            self.errors.append((self.linenr, 'Unknown instruction %s' % inst[0]))
            return None

        cond = 0
        sat = False
        conditions = self.isa.types['INST_CONDITION'].values_by_name
        types = self.isa.types['INST_TYPE'].values_by_name
        type_ = 0
        for atom in inst[1:]:
            if atom in conditions:
                cond = conditions[atom].value
            elif atom in types:
                type_ = types[atom].value
            elif atom == 'SAT':
                sat = True
            else:
                self.errors.append((self.linenr, 'Unknown atom %s' % atom))
                return None

        operands = operands.split(',')
        src = []
        dst = None
        tex = None
        addr = None
        for idx,operand in enumerate(operands):
            operand = operand.strip()
            neg = False
            abs = False
            if operand.startswith('-'):
                neg = True
                operand = operand[1:]
            if operand.startswith('|'):
                if not operand.endswith('|'):
                    self.errors.append((self.linenr, 'Unterminated |'))
                abs = True
                operand = operand[1:-1]

            # check kind of operand
            # (t|u|a)XXX[.xyzw] (address)register
            match_reg = reg_re.match(operand)
            match_mem = mem_re.match(operand)
            if match_reg:
                (regtype, regid, amode, swiz) = match_reg.groups()
                regid = int(regid)
                try:
                    amode = parse_amode(amode)
                except LookupError:
                    self.errors.append((self.linenr, 'Unknown amode %s' % amode))
                    amode = 0
                if idx == 0: # destination operand
                    comps = parse_comps(swiz)
                    if regtype == 't':
                        dst = DstOperand(use=1, amode=amode, reg=regid, comps=comps)
                    elif regtype == 'a':
                        dst = DstOperandAReg(reg=regid, comps=comps)
                    else:
                        self.errors.append((self.linenr, 'Cannot have texture or uniform as destination argument'))
                else: # source operand
                    try:
                        swiz = parse_swiz(swiz)
                    except LookupError:
                        self.errors.append((self.linenr, 'Unparseable swizzle %s' % swiz))
                        swiz = 0
                    if regtype in RGROUPS: # register group
                        if regtype == 'u':
                            if regid < 128:
                                rgroup = 2
                            else:
                                rgroup = 3
                                regid -= 128
                        else:
                            rgroup = RGROUPS.index(regtype)
                        src.append(SrcOperand(use=1, reg=regid, swiz=swiz, neg=neg, abs=abs, amode=amode, rgroup=rgroup))
                    elif regtype == 'a':
                        src.append(DstOperandAReg(reg=regid, comps=comps))
                    elif regtype == 'tex':
                        tex = TexOperand(id=regid, amode=amode, swiz=swiz)
                    else:
                        self.errors.append((self.linenr, 'Unparseable register type %s' % regtype))
                arg_obj = None
            elif match_mem:
                if idx == 0: # destination operand
                    comps = parse_comps(match_mem.group(1))
                    dst = DstOperandMem(comps=comps)
                else:
                    self.errors.append((self.linenr, 'Cannot have mem as source argument'))
            elif operand == 'void':
                #print('void')
                if idx == 0: # destination operand
                    dst = None
                else:
                    src.append(None)
            elif label_re.match(operand):
                #print('label ', operand)
                addr = AddrOperand(addr = operand) # will resolve labels later
            else:
                self.errors.append((self.linenr, 'Unparseable operand ' + operand))

        num_operands = 1 + len(src) + (addr is not None)
        if num_operands != 4:
            self.errors.append((self.linenr, 'Invalid number of operands (%i)' % num_operands))
        inst_out = Instruction(op=op,
            cond=cond,sat=sat,type=type_,
            tex=tex,dst=dst,src=src,addr=addr,unknowns={},linenr=self.linenr)
        self.instructions.append(inst_out)
        return inst_out

    def generate_code(self):
        rv = []
        for inst in self.instructions:
            warnings = []
            if inst.addr is not None: # fill in labels
                try:
                    addr = AddrOperand(self.labels[inst.addr.addr])
                except LookupError:
                    self.errors.append((inst.linenr, 'Unknown label ' + inst.addr.addr))
                    addr = AddrOperand(0) # dummy
                inst = inst._replace(addr=addr)
            inst_out = assemble(self.isa, inst, warnings)
            rv.append(inst_out)
            # sanity check: disassemble and see if the instruction matches
            dis_i = disassemble(self.isa, inst_out, warnings)
            if not compare_inst(inst, dis_i, warnings):
                # Assembly did not match disassembly, print details
                warnings.append('%08x %08x %08x %08x %s' % (
                    inst_out[0], inst_out[1], inst_out[2], inst_out[3], format_instruction(self.isa, dis_i))) 
                warnings.append('                             orig : %s' % (self.source[inst.linenr-1]))
            
            for warning in warnings:
                self.errors.append((inst.linenr, warning))
        if self.errors:
            return None
        return rv

def compare_inst(a,b,warnings):
    match = True
    for attr in ['op', 'cond', 'sat', 'tex', 'dst', 'src', 'addr', 'type']:
        if getattr(a, attr) != getattr(b, attr):
            warnings.append('Assembly/disassembly mismatch: %s %s %s' % (attr, getattr(a, attr), getattr(b, attr)))
            match = False
    return match

def parse_arguments():
    parser = argparse.ArgumentParser(description='Disassemble shader')
    parser.add_argument('--isa-file', metavar='ISAFILE', type=str, 
            help='Shader ISA definition file (rules-ng-ng)',
            default=rnndb_path('isa.xml'))
    parser.add_argument('input', metavar='INFILE', type=str, 
            help='Shader assembly file')
    #parser.add_argument('output', metavar='OUTFILE', type=str, 
    #        help='Binary shader file')
    #parser.add_argument('-a', dest='addr',
    #        default=False, action='store_const', const=True,
    #        help='Show address data with instructions')
    parser.add_argument('-o', dest='bin_out', type=str,
            help='Write binary shader to output file')
    return parser.parse_args()        

def main():
    args = parse_arguments()
    out = sys.stdout
    isa = parse_rng_file(args.isa_file)

    with open(args.input, 'rb') as f:
        asm = Assembler(isa)
        errors = []
        for linenr, line in enumerate(f):
            line = line.rstrip('\n')
            asm.parse(line)
       
        if not asm.errors:
            code = asm.generate_code()
        else: 
            code = None

        for line, error in asm.errors:
            print('Line %i: %s' % (line, error))

        if code is not None:
            if args.bin_out is not None:
                with open(args.bin_out, 'wb') as f:
                    for inst in code:
                        f.write(struct.pack(b'<IIII', *inst))
            else: # no binary output, print as ascii
                for inst in code:
                    print('0x%08x,0x%08x,0x%08x,0x%08x,' % tuple(inst))
        else:
            exit(1)



if __name__ == '__main__':
    main()


