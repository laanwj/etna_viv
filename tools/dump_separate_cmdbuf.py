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
from collections import defaultdict

from binascii import b2a_hex

# Parse rules-ng-ng format for state space
from etnaviv.parse_rng import parse_rng_file, format_path, BitSet, Domain

DEBUG = False

# Number of words to ignore at start of command buffer
# A PIPE3D command will be inserted here by the kernel if necessary
CMDBUF_IGNORE_INITIAL = 8

def int_as_float(i):
    '''Return float with binary representation of unsigned int i'''
    return struct.unpack(b'f', struct.pack(b'I', i))[0]

def fixp_as_float(i):
    '''Return float from 16.16 fixed-point value of i'''
    return i / 65536.0

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

def dump_command_buffer(f, buf, depth, state_map):
    '''
    Dump Vivante command buffer contents in human-readable
    format.
    '''
    indent = '    ' * len(depth)
    f.write('{\n')
    state_base = 0
    state_count = 0
    state_format = 0
    next_cmd = 0 #CMDBUF_IGNORE_INITIAL
    payload_start_ptr = 0
    payload_end_ptr = 0
    op = 0
    size = len(buf)
    ptr = 0
    states = [] # list of (ptr, state_addr) tuples
    while ptr < size:
        hide = False
        value = buf[ptr]
        if ptr >= next_cmd:
            #f.write('\n')
            op = value >> 27
            payload_start_ptr = payload_end_ptr = ptr + 1
            if op == 1:
                state_base = (value & 0xFFFF)<<2
                state_count = (value >> 16) & 0x3FF
                if state_count == 0:
                    state_count = 0x400
                state_format = (value >> 26) & 1
                payload_end_ptr = payload_start_ptr + state_count
                desc = "LOAD_STATE (1) Base: 0x%05X Size: %i Fixp: %i" % (state_base, state_count, state_format)
                if options.hide_load_state:
                    hide = True
            elif op == 2:
                desc = "END (2)"
            elif op == 3:
                desc = "NOP (3)"
            elif op == 4:
                desc = "DRAW_2D (4)"
            elif op == 5:
                desc = "DRAW_PRIMITIVES (5)"
                payload_end_ptr = payload_start_ptr + 3
            elif op == 6:
                desc = "DRAW_INDEXED_PRIMITIVES (6)"
                payload_end_ptr = payload_start_ptr + 4
            elif op == 7:
                desc = "WAIT (7)"
            elif op == 8:
                desc = "LINK (8)"
                payload_end_ptr = payload_start_ptr + 1
            elif op == 9:
                desc = "STALL (9)"
                payload_end_ptr = payload_start_ptr + 1
            elif op == 10:
                desc = "CALL (10)"
                payload_end_ptr = payload_start_ptr + 1
            elif op == 11:
                desc = "RETURN (11)"
            elif op == 13:
                desc = "CHIP_SELECT (13)"
            else:
                desc = "UNKNOWN (%i)" % op
            next_cmd = (payload_end_ptr + 1) & (~1)
        elif ptr < payload_end_ptr: # Parse payload 
            if op == 1:
                pos = (ptr - payload_start_ptr)*4 + state_base
                states.append((ptr, pos, state_format, value))
                desc = format_state(pos, value, state_format, state_map)
            else:
                desc = ""
        else:
            desc = "PAD"
        if not hide:
            f.write(indent + '    0x%08x' % value)
            if ptr != (size-1):
                f.write(", /* %s */\n" % desc)
            else:
                f.write("  /* %s */\n" % desc)
        ptr += 1
    f.write(indent + '}')

def parse_arguments():
    parser = argparse.ArgumentParser(description='Parse execution data log stream.')
    parser.add_argument('input_file', metavar='INFILE', type=str, 
            help='FDR file')
    parser.add_argument('rules_file', metavar='RULESFILE', type=str, 
            help='State map definition file (rules-ng-ng)')
    parser.add_argument('-l', '--hide-load-state', dest='hide_load_state',
            default=False, action='store_const', const=True,
            help='Hide "LOAD_STATE" entries, this can make command stream a bit easier to read')
    return parser.parse_args()        

shader_num = 0

def main():
    args = parse_arguments()
    state_xml = parse_rng_file(args.rules_file)
    state_map = state_xml.lookup_domain('VIVS')
    global options
    options = args
    import re

    with open(args.input_file,'r') as f:
        # parse ascii
        values = []
        for line in f:
            value = line.strip()
            if value.startswith(':'):
                value = int(value[1:9], 16)
                values.append(value)

        dump_command_buffer(sys.stdout, values, [], state_map)
        sys.stdout.write('\n')

if __name__ == '__main__':
    main()

