#!/usr/bin/python
'''
Parse execution data log stream.
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

# Parse execution data log files
from etnaviv.parse_fdr import ENDIAN, WORD_SPEC, ADDR_SPEC, ADDR_CHAR, WORD_CHAR, FDRLoader, Event
# Extract C structures from memory
from etnaviv.extract_structure import extract_structure, ResolverBase
# Print C structures
from etnaviv.dump_structure import dump_structure, print_address
# Parse rules-ng-ng format for state space
from etnaviv.parse_rng import parse_rng_file, format_path, BitSet, Domain

DEBUG = False

# Vivante ioctls (only GCHAL_INTERFACE is actually used by the driver)
IOCTL_GCHAL_INTERFACE          = 30000
IOCTL_GCHAL_KERNEL_INTERFACE   = 30001
IOCTL_GCHAL_TERMINATE          = 30002

# HAL commands without input, can hide the input arguments structure completely
CMDS_NO_INPUT = [
'gcvHAL_GET_BASE_ADDRESS',
'gcvHAL_QUERY_CHIP_IDENTITY',
'gcvHAL_QUERY_VIDEO_MEMORY',
]
# HAL commands without output, can hide the output arguments structure completely
CMDS_NO_OUTPUT = [
'gcvHAL_COMMIT',
'gcvHAL_EVENT_COMMIT'
]

# Number of words to ignore at start of command buffer
# A PIPE3D command will be inserted here by the kernel if necessary
CMDBUF_IGNORE_INITIAL = 8

class HalResolver(ResolverBase):
    '''
    Data type resolver for HAL interface commands.
    Acts as a filter for which union/struct fields to extract and show.
    '''
    def __init__(self, dir):
        self.dir = dir
    
    def filter_fields(self, s, fields_in):
        name = s.type['name']
        if name == '_u':
            enum = s.parent.members['command'].name
            if ((self.dir == 'in' and enum in CMDS_NO_INPUT) or
                (self.dir == 'out' and enum in CMDS_NO_OUTPUT)):
                return {} # no need to show input
            enum = enum[7:].split('_')
            # convert to camelcase FOO_BAR to FooBar
            field = ''.join(s[0] + s[1:].lower() for s in enum)
            if field == 'EventCommit':
                field = 'Event' # exception to the rule...
            return {field}
        elif name == '_gcsHAL_INTERFACE':
            # these fields contains uninitialized garbage
            fields_in.difference_update(['handle', 'pid'])
            if self.dir == 'in':
                # status is an output-only field
                fields_in.difference_update(['status'])
        elif name == '_gcsHAL_ALLOCATE_CONTIGUOUS_MEMORY':
            # see gckOS_AllocateNonPagedMemory
            if self.dir == 'in':
                # These fields are not used on input
                fields_in.difference_update(['physical','logical'])
            # All three fields are filled on output
            # bytes has the number of actually allocated bytes
        elif name == '_gcsHAL_ALLOCATE_LINEAR_VIDEO_MEMORY':
            # see gckVIDMEM_AllocateLinear
            if self.dir == 'in':
                # These fields are not used on input
                fields_in.difference_update(['node'])
            else:
                return {'node'}
        elif name == '_gcsHAL_LOCK_VIDEO_MEMORY':
            if self.dir == 'in':
                fields_in.difference_update(['address','memory'])
            else:
                fields_in.difference_update(['node'])
        elif name == '_gcsHAL_USER_SIGNAL':
            if self.dir == 'out': # not used as output by any subcommand
                fields_in.difference_update(['manualReset','wait','state'])

        return fields_in

class Counter(object):
    '''Count unique values'''
    def __init__(self):
        self.d = {}
        self.c = 0

    def __getitem__(self, key):
        try:
            return self.d[key]
        except KeyError:
            rv = self.c
            self.d[key] = rv
            self.c += 1
            return rv

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
            if isinstance(register.type, Domain):
                desc += format_addr(value)
            else:
                desc += register.describe(value)
    return desc

def dump_shader(f, name, states, start, end):
    '''Dump binary shader code to disk'''
    if not start in states:
        return # No shader detected
    # extract code from consecutive addresses
    pos = start
    code = []
    while pos < end and (pos in states):
        code.append(states[pos])
        pos += 4
    global shader_num
    filename = '%s_%i.bin' % (name, shader_num)
    with open(filename, 'wb') as g:
        for word in code:
            g.write(struct.pack('<I', word))
    f.write('/* [dumped %s to %s] */ ' % (name, filename))
    shader_num += 1


def dump_command_buffer(f, mem, addr, end_addr, depth, state_map):
    '''
    Dump Vivante command buffer contents in human-readable
    format.
    '''
    indent = '    ' * len(depth)
    f.write('{\n')
    state_base = 0
    state_count = 0
    state_format = 0
    next_cmd = CMDBUF_IGNORE_INITIAL
    payload_start_ptr = 0
    payload_end_ptr = 0
    op = 0
    size = (end_addr - addr)//4
    ptr = 0
    states = [] # list of (ptr, state_addr) tuples
    while ptr < size:
        hide = False
        (value,) = WORD_SPEC.unpack(mem[addr+ptr*4:addr+ptr*4+4])
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
    if options.list_address_states:
        # Print addresses; useful for making a re-play program
        #f.write('\n' + indent + 'GPU addresses {\n')
        uniqaddr = defaultdict(list)
        for (ptr, pos, state_format, value) in states:
            try:
                path = state_map.lookup_address(pos)
            except KeyError:
                continue
            type = path[-1][0].type
            if isinstance(type, Domain): # type Domain refers to another memory space
                #f.write(indent)
                addrname = format_addr(value)
                #f.write('    {0x%x,0x%05X}, /* %s = 0x%08x (%s) */\n' % (ptr, pos, format_path(path), value, addrname))
                uniqaddr[value].append(ptr)

        #f.write(indent + '},')
        f.write('\n' + indent + 'Grouped GPU addresses {\n')
        for (value, ptrs) in uniqaddr.iteritems():
            lvalues = ' = '.join([('cmdbuf[0x%x]' % ptr) for ptr in ptrs])
            f.write(indent + '    ' + lvalues + ' = ' + format_addr(value) + ('; /* 0x%x */' % value) + '\n')

        f.write(indent + '}')

    if options.dump_shaders:
        state_by_pos = {}
        for (ptr, pos, state_format, value) in states:
            state_by_pos[pos]=value
        # 0x04000 and 0x06000 contain shader instructions
        dump_shader(f, 'vs', state_by_pos, 0x04000, 0x05000)
        dump_shader(f, 'ps', state_by_pos, 0x06000, 0x07000)

def dump_context_map(f, mem, addr, end_addr, depth, state_map):
    '''
    Dump Vivante context map.
    '''
    indent = '    ' * len(depth)
    f.write('{\n')
    state_base = 0
    state_count = 0
    state_format = 0
    next_cmd = 0
    payload_start_ptr = 0
    payload_end_ptr = 0
    op = 0
    size = (end_addr - addr)//4
    ptr = 0
    while ptr < size:
        hide = False
        (value,) = WORD_SPEC.unpack(mem[addr+ptr*4:addr+ptr*4+4])
        if value != 0:
            f.write(indent + '    {0x%x, 0x%05X}' % (value, ptr*4))
            try:
                path = state_map.lookup_address(ptr*4)
                desc = format_path(path)
            except KeyError:
                desc = ''
            if ptr != (size-1):
                f.write(", /* %s */\n" % desc)
            else:
                f.write("  /* %s */\n" % desc)
        ptr += 1
    f.write(indent + '}')

def parse_arguments():
    parser = argparse.ArgumentParser(description='Parse execution data log stream.')
    parser.add_argument('input', metavar='INFILE', type=str, 
            help='FDR file')
    parser.add_argument('struct_file', metavar='STRUCTFILE', type=str, 
            help='Structures definition file')
    parser.add_argument('rules_file', metavar='RULESFILE', type=str, 
            help='State map definition file (rules-ng-ng)')
    parser.add_argument('isa_file', metavar='ISAFILE', type=str, 
            help='Shader ISA definition file (rules-ng-ng)')
    parser.add_argument('-l', '--hide-load-state', dest='hide_load_state',
            default=False, action='store_const', const=True,
            help='Hide "LOAD_STATE" entries, this can make command stream a bit easier to read')
    parser.add_argument('--show-state-map', dest='show_state_map',
            default=False, action='store_const', const=True,
            help='Expand state map from context (verbose!)')
    parser.add_argument('--show-context-commands', dest='show_context_commands',
            default=False, action='store_const', const=True,
            help='Expand context command buffer (verbose!)')
    parser.add_argument('--show-context-buffer', dest='show_context_buffer',
            default=False, action='store_const', const=True,
            help='Expand context CPU buffer (verbose!)')
    parser.add_argument('--list-address-states', dest='list_address_states',
            default=False, action='store_const', const=True,
            help='When dumping command buffer, provide list of states that contain GPU addresses')
    parser.add_argument('--dump-shaders', dest='dump_shaders',
            default=False, action='store_const', const=True,
            help='Dump shaders to file')
    return parser.parse_args()        

def load_data_definitions(struct_file):
    with open(struct_file, 'r') as f:
        return json.load(f)

vidmem_addr = Counter() # Keep track of video memories
shader_num = 0

def format_addr(value):
    '''
    Return unique identifier for an address.
    '''
    # XXX this only works if exact addresses are used; offsets into buffers
    # are not currently recognized as such but labeled as unique new addresses
    id = vidmem_addr[value]
    if id < 26:
        return 'ADDR_'+chr(65 + id)
    else:
        return 'ADDR_%i' % id

def main():
    args = parse_arguments()
    defs = load_data_definitions(args.struct_file)
    state_xml = parse_rng_file(args.rules_file)
    state_map = state_xml.lookup_domain('VIVS')
    fdr = FDRLoader(args.input)
    global options
    options = args
    global isa
    isa = parse_rng_file(args.isa_file)

    def handle_comment(f, val, depth):
        '''Annotate value with a comment'''
        if not depth:
            return None
        parent = depth[-1][0]
        field = depth[-1][1]
        parent_type = parent.type['name']
        # Show names for features and minor feature bits
        if parent_type == '_gcsHAL_QUERY_CHIP_IDENTITY':
            if field == 'chipMinorFeatures':
                field = 'chipMinorFeatures0'
            if field in state_xml.types and isinstance(state_xml.types[field], BitSet):
                feat = state_xml.types[field]
                active_feat = [bit.name for bit in feat.bitfields if bit.extract(val.value)]
                return ' '.join(active_feat)
        elif parent_type == '_gcsHAL_LOCK_VIDEO_MEMORY':
            if field == 'address': # annotate addresses with unique identifier
                return format_addr(val.value)

    def handle_pointer(f, ptr, depth):
        parent = depth[-1][0]
        field = depth[-1][1]
        if ptr.type in ['_gcoCMDBUF','_gcoCONTEXT','_gcsQUEUE']:
            s = extract_structure(fdr, ptr.addr, defs, ptr.type, resolver=resolver)
            f.write('&(%s)0x%x' % (ptr.type,ptr.addr))
            dump_structure(f, s, handle_pointer, handle_comment, depth)
            return
        elif field == 'logical' and ptr.addr != 0:
            # Command stream
            if parent.type['name'] == '_gcoCMDBUF':
                f.write('&(uint32[])0x%x' % (ptr.addr))
                dump_command_buffer(f, fdr, ptr.addr + parent.members['startOffset'].value, 
                         ptr.addr + parent.members['offset'].value,
                         depth, state_map)
                return
            if parent.type['name'] == '_gcoCONTEXT' and options.show_context_commands:
                f.write('&(uint32[])0x%x' % (ptr.addr))
                dump_command_buffer(f, fdr, ptr.addr, 
                         ptr.addr + parent.members['bufferSize'].value,
                         depth, state_map)
                return
        elif parent.type['name'] == '_gcoCONTEXT' and field == 'map' and ptr.addr != 0 and options.show_state_map:
            f.write('&(uint32[])0x%x' % (ptr.addr))
            dump_context_map(f, fdr, ptr.addr, 
                     ptr.addr + parent.members['stateCount'].value*4,
                     depth, state_map)
            return
        elif parent.type['name'] == '_gcoCONTEXT' and field == 'buffer' and ptr.addr != 0 and options.show_context_buffer:
            # Equivalent to gcoCONTEXT.map
            f.write('&(uint32[])0x%x' % (ptr.addr))
            dump_command_buffer(f, fdr, ptr.addr, 
                     ptr.addr + parent.members['bufferSize'].value,
                     depth, state_map)
            return

        print_address(f, ptr, depth)

    vivante_ioctl_data_t = struct.Struct(ENDIAN + ADDR_CHAR + WORD_CHAR + ADDR_CHAR + WORD_CHAR)
    f = sys.stdout
    thread_id = Counter()

    def thread_name(rec):
        return '[thread %i]' % thread_id[rec.parameters['thread'].value]

    for seq,rec in enumerate(fdr):
        if isinstance(rec, Event): # Print events as they appear in the fdr
            f.write(('[seq %i] ' % seq) + thread_name(rec) + ' ')
            params = rec.parameters
            if rec.event_type == 'MMAP_AFTER':
                f.write('mmap addr=0x%08x length=0x%08x prot=0x%08x flags=0x%08x offset=0x%08x = 0x%08x\n' % (
                    params['addr'].value, params['length'].value, params['prot'].value, params['flags'].value, params['offset'].value, 
                    params['ret'].value))
            elif rec.event_type == 'MUNMAP_AFTER':
                f.write('munmap addr=0x%08x length=0x%08x = 0x%08x\n' % (
                    params['addr'].value, params['length'].value, params['ret'].value))
            elif rec.event_type == 'IOCTL_BEFORE': # addr, length, prot, flags, offset
                if params['request'].value == IOCTL_GCHAL_INTERFACE:
                    ptr = params['ptr'].value
                    inout = vivante_ioctl_data_t.unpack(fdr[ptr:ptr+vivante_ioctl_data_t.size])
                    f.write('in=')
                    resolver = HalResolver('in')
                    s = extract_structure(fdr, inout[0], defs, '_gcsHAL_INTERFACE', resolver=resolver)
                    dump_structure(f, s, handle_pointer, handle_comment)
                    f.write('\n')
                else:
                    f.write('Unknown Vivante ioctl %i\n' % rec.parameters['request'].value)
            elif rec.event_type == 'IOCTL_AFTER':
                if params['request'].value == IOCTL_GCHAL_INTERFACE:
                    ptr = params['ptr'].value
                    inout = vivante_ioctl_data_t.unpack(fdr[ptr:ptr+vivante_ioctl_data_t.size])
                    f.write('out=')
                    resolver = HalResolver('out')
                    s = extract_structure(fdr, inout[2], defs, '_gcsHAL_INTERFACE', resolver=resolver)
                    dump_structure(f, s, handle_pointer, handle_comment)
                    f.write('\n')
                    f.write('/* ================================================ */\n')
                else:
                    f.write('Unknown Vivante ioctl %i\n' % rec.parameters['request'].value)
            else:
                f.write('unhandled event ' + rec.event_type + '\n')

if __name__ == '__main__':
    main()

