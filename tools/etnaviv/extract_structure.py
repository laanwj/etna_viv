#!/usr/bin/python
'''
Extract nested structure or fixed-size value from virtual memory map
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
import struct
from collections import namedtuple, OrderedDict

import target_arch as arch
from parse_fdr import FDRLoader, Event

# struct character for specific bytesize/encoding combos
STRUCT_ENCODING_CHAR = {
    (1,'unsigned_char'):b'B',
    (1,'signed_char'):b'b',
    (1,'unsigned'):b'B',
    (1,'signed'):b'b',
    (1,None):b'B',
    (2,'unsigned'):b'H',
    (2,'signed'):b'h',
    (2,None):b'H',
    (4,'unsigned'):b'I',
    (4,'signed'):b'i',
    (4,'float'):b'f',
    (4,None):b'I',
    (8,'unsigned'):b'Q',
    (8,'signed'):b'q',
    (8,'float'):b'd',
    (8,None):b'Q',
}

VOID = object()
UNRESOLVED = object()
Enumerator = namedtuple('Enumerator', ['name', 'value'])
Value = namedtuple('Value', ['type', 'value'])
Struct = namedtuple('Struct', ['type', 'members', 'parent'])
Union = namedtuple('Union', ['type', 'members', 'parent'])
Pointer = namedtuple('Pointer', ['type', 'addr', 'indirection'])
Array = namedtuple('Array', ['contents', 'parent'])

class ResolverBase(object):
    def filter_fields(self, s, fields_in):
        '''
        Choose the correct member(s) of a struct or union based on application-specific context.
        This can be used to filter out structure fields that are unnecessary for a certain command type.
        '''
        return fields_in
    def array_length(self, a):
        '''Determine length of an array from context'''
        return None

def extract_structure(mem, addr, defs, root, parent=None, resolver=ResolverBase()):
    '''Extract nested structure or fixed-size value from memory map'''
    if root == 'void' or addr == 0:
        return VOID
    root = defs[root]
    if root['kind'] in ['structure_type','union_type']:
        # if union, need application-specific callback to determine alternative from
        # enclosing scope
        if root['kind'] == 'union_type':
            s = Union(root, OrderedDict(), parent) 
        else:
            s = Struct(root, OrderedDict(), parent)
        choice = resolver.filter_fields(s, {m['name'] for m in root['members']})
        for member in root['members']:
            if member['name'] not in choice:
                # If a specific field was chosen, return only that field
                continue
            offset = addr + member['offset']
            if member['indirection'] == 0:
                value = extract_structure(mem, offset, defs, member['type'], s, resolver)
            else:
                try:
                    xaddr = arch.ADDR_SPEC.unpack(mem[offset:offset + arch.ADDR_SPEC.size])[0]
                    value = Pointer(member['type'], xaddr, member['indirection'])
                except IndexError:
                    value = UNRESOLVED
            s.members[member['name']] = value
        return s
    elif root['kind'] in ['base_type','enumeration_type']:
        byte_size = root['byte_size']
        encoding = root.get('encoding', 'signed') # enumerations are signed in DWARF
        try:
            data = mem[addr:addr+byte_size]
        except IndexError:
            return UNRESOLVED
        try:
            char = arch.ENDIAN+STRUCT_ENCODING_CHAR[byte_size,encoding]
        except KeyError:
            try:
                char = arch.ENDIAN+STRUCT_BASE_CHAR[byte_size,None]
            except KeyError:
                return None
        value = struct.unpack(char, data)[0] 

        if root['kind'] == 'enumeration_type':
            # look name belonging to value, if not found, just show value
            s = '%i' % value
            for e in root['enumerators']:
                if e['value'] == value:
                    return Enumerator(e['name'], value)
            return Enumerator(None, value)
        else:
            return Value(root, value)
    elif root['kind'] in ['array_type']:
        # XXX array_type
        length = root['length']
        a = Array([], parent)
        if length is None:
            length = resolver.array_length(a)
        if length is None:
            return UNRESOLVED
        # TODO: determine size of inner type to determine memory offsets
    return None

