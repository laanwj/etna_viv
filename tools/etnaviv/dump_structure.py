#!/usr/bin/python
'''
Print memory contents as C99 structure initializer
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
from extract_structure import extract_structure, VOID, UNRESOLVED, Enumerator, Value, Struct, Union, Pointer, Array, ResolverBase

def print_address(f, s, depth):
    '''
    Print address and type of pointer.
    This is the default implementation of printing a pointer.
    '''
    f.write('(%s%s)0x%x' % (s.type, '*'*s.indirection, s.addr))

def no_comment(f, s, depth):
    '''Dummy comment'''
    return None

def dump_structure(f, s, handle_pointer=print_address, handle_comment=no_comment, depth=[]):
    '''Print memory contents as C99 structure initializer
        = {.ice_structure = 'nine', ...}
    '''
    if s is VOID:
        f.write('void')
        return
    indent = '    ' * len(depth)
    if isinstance(s, Pointer):
        # pointer: need application-specific callback to resolve this
        #   (either print address, print one pointee, or print array @ pointer)
        handle_pointer(f, s, depth)
    elif isinstance(s, Struct) or isinstance(s, Union):
        # here there's dragons...
        f.write('{')
        first = True
        for name,value in s.members.iteritems():
            if not first:
                f.write(',')
            f.write('\n')
            f.write(indent+'    .%s = ' % name)
            dump_structure(f, value, handle_pointer, handle_comment, depth + [(s,name)])
            first = False
        f.write('\n'+indent+'}')
    elif isinstance(s, Value):
        if s.type['encoding'] == 'unsigned':
            f.write('0x%x' % s.value)
        else:
            f.write('%i' % s.value)
    elif isinstance(s, Enumerator):
        # look name belonging to value, if not found, just show value
        if s.name is None:
            f.write('%i' % s.value)
        else:
            f.write(s.name)
    elif isinstance(s, Array):
        f.write('{\n')
        first = True
        for idx,value in enumerate(s.contents):
            if not first:
                f.write(',')
            dump_structure(f, value, handle_pointer, handle_comment, depth + [(s,idx)])
            f.write('\n')
            first = False
        f.write(indent+'}')
    comment = handle_comment(f, s, depth)
    if comment is not None:
        f.write(' /* ' + comment + ' */')

