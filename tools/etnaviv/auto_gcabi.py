'''
Choose GCABI json file automatically, based on Vivante driver version
in FDR file
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
import os, struct

from etnaviv.util import BASEPATH
from etnaviv.parse_fdr import FDRLoader, Event, Comment
from etnaviv.target_arch import ENDIAN, WORD_SPEC, WORD_CHAR

DATAPATH=os.path.join(BASEPATH, "tools", "data")

BY_VERSION={
    (5,0,11,25762): 'gcs_hal_interface_imx6_v5_0_11_p4_25762.json',
    (5,0,11,41671): 'gcs_hal_interface_imx6_v5_0_11_p8_41671.json',
    (6,2,3,129602): 'gcs_hal_interface_imx8_v6.2.3.129602.json',
    (6,2,2,93313): 'gcs_hal_interface_imx6_v6.2.2.93313.json',

}

def guess_from_version(version):
    try:
        return os.path.join(DATAPATH, BY_VERSION[version])
    except KeyError:
        return None

def guess_from_fdr(filename):
    fdr = FDRLoader(filename)
    rec = next(iter(fdr))
    if isinstance(rec, Comment):
        t = WORD_SPEC.unpack(rec.data[0:4])[0]
        if t == 0x424f4c42: # Version marker
            v = struct.unpack(ENDIAN + WORD_CHAR * 4, rec.data[4:])
            return guess_from_version(v)
    return None
