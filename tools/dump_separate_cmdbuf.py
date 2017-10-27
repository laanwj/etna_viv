#!/usr/bin/python
'''
Parse separate command buffer (outside of fdr), used for processing debug output.
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
import argparse
import os, sys, struct
import json
from collections import defaultdict, namedtuple

from binascii import b2a_hex

# Parse rules-ng-ng format for state space
from etnaviv.util import rnndb_path
from etnaviv.parse_rng import parse_rng_file, format_path, BitSet, Domain
from etnaviv.dump_cmdstream_util import int_as_float, fixp_as_float
from etnaviv.parse_command_buffer import parse_command_buffer,CmdStreamInfo
from etnaviv.target_arch import bytes_to_words
from etnaviv.rng_describe_c import dump_command_buffer_c, dump_command_buffer_c_raw

DEBUG = False

COMPS = 'xyzw'
def format_state(pos, value, fixp, state_map):
    try:
        path = state_map.lookup_address(pos)
        path_str = format_path(path) #+ ('(0x%05X)' % pos)
    except KeyError:
        path = None
        path_str = '0x%05X' % pos
    desc = '  ' + path_str 
    if fixp:
        desc += ' = %f' % fixp_as_float(value)
    else:
        # For uniforms, show float value
        if (pos >= 0x05000 and pos < 0x06000) or (pos >= 0x07000 and pos < 0x08000):
            num = pos & 0xFFF
            spec = 'u%i.%s' % (num//16, COMPS[(num//4)%4])
            desc += ' := %f (%s)' % (int_as_float(value), spec)
        elif path is not None:
            register = path[-1][0]
            desc += ' := '
            desc += register.describe(value)
    return desc

def dump_command_buffer(f, recs, depth, state_map, addrs, tgtaddrs):
    '''
    Dump Vivante command buffer contents in human-readable
    format.
    '''
    indent = '    ' * len(depth)
    f.write('{\n')
    ptr = 0
    for rec in recs:
        hide = False
        if addrs is not None:
            if addrs[ptr] in tgtaddrs:
                f.write('\x1b[1;31m')
            f.write('[0x0%08x]' % addrs[ptr])
            if addrs[ptr] in tgtaddrs:
                f.write('*')
            else:
                f.write(' ')
        if rec.op == 1 and rec.payload_ofs == -1:
            if options.hide_load_state:
                hide = True

        if rec.state_info is not None:
            desc = format_state(rec.state_info.pos, rec.value, rec.state_info.format, state_map)
        else:
            desc = rec.desc

        if not hide:
            f.write(indent + '    0x%08x' % rec.value)
            f.write(", /* %s */\n" % desc)
        f.write('\x1b[0m')
        ptr += 1
    f.write(indent + '}')

def parse_arguments():
    parser = argparse.ArgumentParser(description='Parse execution data log stream.')
    parser.add_argument('input_file', metavar='INFILE', type=str, 
            help='FDR file')
    parser.add_argument('--rules-file', metavar='RULESFILE', type=str, 
            help='State map definition file (rules-ng-ng)',
            default=rnndb_path('state.xml'))
    parser.add_argument('--cmdstream-file', metavar='CMDSTREAMFILE', type=str, 
            help='Command stream definition file (rules-ng-ng)',
            default=rnndb_path('cmdstream.xml'))
    parser.add_argument('-l', '--hide-load-state', dest='hide_load_state',
            default=False, action='store_const', const=True,
            help='Hide "LOAD_STATE" entries, this can make command stream a bit easier to read')
    parser.add_argument('-b', '--binary', dest='binary',
            default=False, action='store_const', const=True,
            help='Input is in binary')
    parser.add_argument('-g', '--galcore', dest='galcore',
            default=False, action='store_const', const=True,
            help='Input is in galcore dmesg format')
    parser.add_argument('--output-c', dest='output_c',
            default=False, action='store_const', const=True,
            help='Print command buffer emission in C format')
    parser.add_argument('--output-c-raw', dest='output_c_raw',
            default=False, action='store_const', const=True,
            help='Print command buffer emission in C raw command stream emit format')
    return parser.parse_args()        

shader_num = 0

def main():
    args = parse_arguments()
    state_xml = parse_rng_file(args.rules_file)
    state_map = state_xml.lookup_domain('VIVS')

    cmdstream_xml = parse_rng_file(args.cmdstream_file)
    fe_opcode = cmdstream_xml.lookup_type('FE_OPCODE')
    cmdstream_map = cmdstream_xml.lookup_domain('VIV_FE')
    cmdstream_info = CmdStreamInfo(fe_opcode, cmdstream_map)

    global options
    options = args
    import re

    if args.binary:
        # Binary format
        with open(args.input_file,'rb') as f:
            data = f.read()
        assert((len(data) % 8)==0)
        values = bytes_to_words(data)
        addrs = None
        tgtaddrs = None
    elif args.galcore:
        # Vivante kernel format
        values = []
        addrs = []
        tgtaddrs = set()
        with open(args.input_file,'r') as f:
            for line in f:
                #value = line.strip()
                #if value.startswith(':'):
                #    value = int(value[1:9], 16)
                #    values.append(value)
                m = re.search('DMA Address 0x([0-9A-F]{8})', line)
                if m:
                    tgtaddrs.add(int(m.group(1), 16))
                m = re.search('([0-9A-F]{8}) : (([0-9A-F]{8} )*[0-9A-F]{8})$', line)
                # [  309.029521] 3FD84000 : 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
                if m:
                    addr = int(m.group(1), 16)
                    for i,d in enumerate(m.group(2).split(' ')):
                        addrs.append(addr + i*4)
                        values.append(int(d, 16))
    else:
        # old etnaviv ASCII format
        values = []
        addrs = None
        tgtaddrs = None
        with open(args.input_file,'r') as f:
            for line in f:
                value = line.strip()
                if value.startswith(':'):
                    value = int(value[1:9], 16)
                    values.append(value)

    recs = parse_command_buffer(values, cmdstream_info, initial_padding=0)
    if args.output_c_raw:
        dump_command_buffer_c_raw(sys.stdout, recs, state_map)
    elif args.output_c:
        dump_command_buffer_c(sys.stdout, recs, state_map)
    else:
        dump_command_buffer(sys.stdout, recs, [], state_map, addrs, tgtaddrs)
    sys.stdout.write('\n')

if __name__ == '__main__':
    main()

