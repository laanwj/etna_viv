from __future__ import print_function, division, unicode_literals
import struct

# from http://davidejones.com/blog/1413-python-precision-floating-point/
def float16_compress(float32):
    F16_EXPONENT_BITS = 0x1F
    F16_EXPONENT_SHIFT = 10
    F16_EXPONENT_BIAS = 15
    F16_MANTISSA_BITS = 0x3ff
    F16_MANTISSA_SHIFT =  (23 - F16_EXPONENT_SHIFT)
    F16_MAX_EXPONENT =  (F16_EXPONENT_BITS << F16_EXPONENT_SHIFT)

    a = struct.pack('>f',float32)
    b = binascii.hexlify(a)

    f32 = int(b,16)
    f16 = 0
    sign = (f32 >> 16) & 0x8000
    exponent = ((f32 >> 23) & 0xff) - 127
    mantissa = f32 & 0x007fffff
            
    if exponent == 128:
        f16 = sign | F16_MAX_EXPONENT
        if mantissa:
            f16 |= (mantissa & F16_MANTISSA_BITS)
    elif exponent > 15:
        f16 = sign | F16_MAX_EXPONENT
    elif exponent > -15:
        exponent += F16_EXPONENT_BIAS
        mantissa >>= F16_MANTISSA_SHIFT
        f16 = sign | exponent << F16_EXPONENT_SHIFT | mantissa
    else:
        f16 = sign
    return f16
    
def float16_decompress(float16):
    s = int((float16 >> 15) & 0x00000001)    # sign
    e = int((float16 >> 10) & 0x0000001f)    # exponent
    f = int(float16 & 0x000003ff)            # fraction

    if e == 0:
        if f == 0:
            return int(s << 31)
        else:
            while not (f & 0x00000400):
                f = f << 1
                e -= 1
            e += 1
            f &= ~0x00000400
    elif e == 31:
        if f == 0:
            return int((s << 31) | 0x7f800000)
        else:
            return int((s << 31) | 0x7f800000 | (f << 13))

    e = e + (127 - 15)
    f = f << 13
    return int((s << 31) | (e << 23) | f)

def int_as_float(i, size):
    '''Return float with binary representation of unsigned int i'''
    if size == 32:
        return struct.unpack(b'f', struct.pack(b'I', i))[0]
    elif size == 64:
        return struct.unpack(b'd', struct.pack(b'Q', i))[0]
    elif size == 16: # half-float
        # Future python will support this:
        # https://hg.python.org/cpython/rev/519bde9db8e0
        # return struct.unpack(b'e', struct.pack(b'H', i))[0]
        return struct.unpack(b'f', struct.pack(b'I', float16_decompress(i)))[0]

def float_as_int(f, size):
    '''Return unsigned int with binary representation of float f'''
    if size == 32:
        return struct.unpack(b'I', struct.pack(b'f', f))[0]
    elif size == 64:
        return struct.unpack(b'Q', struct.pack(b'd', f))[0]
    elif size == 16: # half-float
        # Future python will support this:
        # https://hg.python.org/cpython/rev/519bde9db8e0
        # return struct.unpack(b'H', struct.pack(b'e', i))[0]
        return float16_compress(struct.unpack(b'I', struct.pack(b'f', f))[0])
