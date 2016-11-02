#!/usr/bin/python
'''
Utilities for parsing execution data log stream.
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

def int_as_float(i):
    '''Return float with binary representation of unsigned int i'''
    return struct.unpack(b'f', struct.pack(b'I', i))[0]

def fixp_as_float(i):
    '''Return float from 16.16 fixed-point value of i'''
    if i > 0x80000000:
        return (i - 0x100000000) / 65536.0
    else:
        return i / 65536.0

COMPS = 'xyzw'
def offset_to_uniform(num):
    '''
    Register offset to u0.x.
    '''
    return 'u%i.%s' % (num//16, COMPS[(num//4)%4])

