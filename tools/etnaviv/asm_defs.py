# Copyright (c) 2017 Wladimir J. van der Laan
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
class Model:
    '''GPU model definition'''
    GC2000 = 0 # or older
    GC3000 = 1

    by_name = {
        'GC2000':GC2000,
        'GC3000':GC3000,
    }
    by_idx = {
        GC2000:'GC2000',
        GC3000:'GC3000',
    }

class Flags:
    '''GPU dialect flags'''
    NONE = 0
    DUAL16 = 1

    by_name = {
        'DUAL16':DUAL16,
    }
    by_idx = {
        DUAL16:'DUAL16',
    }

    @classmethod
    def from_str(cls, s):
        if not s:
            return 0
        rv = 0
        for atom in s.split(','):
            rv |= cls.by_name[atom.upper()]
        return rv

    @classmethod
    def available(cls):
        return ','.join(cls.by_name.keys())

class Dialect:
    '''ISA dialect'''
    def __init__(self, model, flags):
        self.model = model
        self.flags = flags
