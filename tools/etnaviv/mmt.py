import struct
from collections import namedtuple

def read_1(f):
    return f.read(1)[0]
def read_2(f):
    return struct.unpack('<H', f.read(2))[0]
def read_4(f):
    return struct.unpack('<I', f.read(4))[0]
def read_8(f):
    return struct.unpack('<Q', f.read(8))[0]
def read_buffer(f):
    length = read_4(f)
    return f.read(length)
def read_str(f):
    s = read_buffer(f)
    assert(s[-1] == 0)
    return s[0:-1]

LogMessage = namedtuple('LogMessage', ['msg'])
Open = namedtuple('Open', ['flags', 'mode', 'fd', 'path'])
Mmap = namedtuple('Mmap', ['offset', 'prot', 'flags', 'fd', 'region_id', 'start', 'length'])
Munmap = namedtuple('Munmap', ['offset', 'region_id', 'start', 'length', 'unk1', 'unk2'])
StoreInfo = namedtuple('StoreInfo', ['msg'])
Store = namedtuple('Store', ['region_id', 'offset', 'data'])
ProcessMap = namedtuple('ProcessMap', ['msg'])

# etnaviv specific
Commit = namedtuple('Commit', [])

def parse_mmt_file(f):
    while True:
        ch = f.read(1)
        if ch == b'':
            return
        elif ch == b'=' or ch == b'-':  # Comment
            s = b''
            while True: # read until \n
                ch = f.read(1)
                if ch == b'\n':
                    break
                else:
                    s += ch
            yield LogMessage(s)
        elif ch == b'o': # open
            flags = read_4(f)
            mode = read_4(f)
            fd = read_4(f)
            path = read_str(f)
            assert(read_1(f) == 10)
            yield Open(flags, mode, fd, path)
        elif ch == b'M': # mmap
            offset = read_8(f)
            prot = read_4(f)
            flags = read_4(f)
            fd = read_4(f)
            region_id = read_4(f)
            start = read_8(f)
            length = read_8(f)
            assert(read_1(f) == 10)
            yield Mmap(offset, prot, flags, fd, region_id, start, length)
        elif ch == b'u': # munmap
            offset = read_8(f)
            region_id = read_4(f)
            start = read_8(f)
            length = read_8(f)
            unk1 = read_8(f)
            unk2 = read_8(f)
            assert(read_1(f) == 10)
            yield Munmap(offset, region_id, start, length, unk1, unk2)
        elif ch == b'x': # store_info
            info = read_str(f)
            assert(read_1(f) == 10)
            yield StoreInfo(info)
        elif ch == b'w': # store
            region_id = read_4(f)
            offset = read_4(f)
            length = read_1(f)
            data = f.read(length)
            assert(read_1(f) == 10)
            yield Store(region_id, offset, data)
        elif ch == b'c': # commit
            assert(read_1(f) == 10)
            yield Commit()
        elif ch == b'y': # process map
            assert(read_8(f) == 1)
            msg = read_buffer(f)
            assert(read_1(f) == 10)
            yield ProcessMap(msg)
        else:
            print('Unknown ', ch)
            exit(1)

