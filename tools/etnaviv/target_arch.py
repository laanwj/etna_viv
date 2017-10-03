'''
Basic data encoding.
'''
# Copyright (c) 2016 Wladimir J. van der Laan
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
import struct, sys

LITTLE_ENDIAN = b'<'
BIG_ENDIAN = b'>'

# target architecture description
ENDIAN = LITTLE_ENDIAN

RECTYPE_CHAR = b'B' # always 8 bit
MAGIC_CHAR = b'I' # always 32 bit
WORD_CHAR = b'I' # 32 bit
SHORT_STRING_SIZE_CHAR = b'B' # always 8 bit

# struct specifiers for decoding
WORD_SPEC = struct.Struct(ENDIAN + WORD_CHAR)
SHORT_STRING_SIZE_SPEC = struct.Struct(ENDIAN + SHORT_STRING_SIZE_CHAR)

def update_addr_size(s):
    global ADDR_CHAR,ADDR_SPEC
    if s == 32:
        ADDR_CHAR = b'I'
    elif s == 64:
        ADDR_CHAR = b'Q'
    else:
        assert(0)
    ADDR_SPEC = struct.Struct(ENDIAN + ADDR_CHAR)

update_addr_size(32)

# low-level encoding/decoding
if (ENDIAN == LITTLE_ENDIAN and sys.byteorder == 'little') or (ENDIAN == BIG_ENDIAN and sys.byteorder == 'big'):
    # endian matches native endian, find matching sizes
    import array
    _asizechars = {array.array(ch).itemsize:ch for ch in 'cHIL'}
    def bytes_to_words(data):
        return array.array(_asizechars[4], data)
else:
    def bytes_to_words(data):
        return [WORD_SPEC.unpack(data[ptr:ptr+4])[0] for ptr in range(0,len(data),4)]

