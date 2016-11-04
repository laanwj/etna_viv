import os, bisect

class Memblock:
    pass
def parse_proc_map(m):
    for line in m.rstrip().split(b'\n'):
        # 7f44adffd000-7f44ae7fd000 rw-p 00000000 00:00 0                          [stack:14834]
        attrs = line.split()
        addrfrom,addrto = attrs[0].split(b'-')
        blk = Memblock()
        blk.addrfrom = int(addrfrom, 16)
        blk.addrto = int(addrto, 16)
        blk.size = blk.addrto - blk.addrfrom
        blk.perms = attrs[1]
        blk.offset = int(attrs[2], 16)
        blk.stime = attrs[3]
        blk.inode = int(attrs[4])
        if len(attrs)>=6:
            blk.desc = attrs[5]
        else:
            blk.desc = ''
        yield blk

class MemRangeDesc:
    def __init__(self, name, addrfrom, addrto):
        self.addrfrom = addrfrom
        self.addrto = addrto
        self.name = name
        self.basename = os.path.basename(name)
    def __repr__(self):
        return 'MemRangeDesc(0x%08x:0x%08x,name=%s)' % (self.addrfrom, self.addrto, self.name.decode('utf-8'))

class MemRanges:
    def __init__(self, ranges):
        self.ranges = ranges
        self.starts = [r.addrfrom for r in ranges]

    def lookup(self, addr):
        i = bisect.bisect(self.starts, addr) - 1
        if i>=0 and self.ranges[i].addrfrom <= addr < self.ranges[i].addrto:
            return self.ranges[i]
        else:
            return None

def extract_mem_ranges(msg):
    ranges = {}
    for rec in parse_proc_map(msg): # Memblock iter
        if not rec.desc:
            continue
        if rec.desc in ranges: # if known mapping, extend end address
            ranges[rec.desc].addrto = rec.addrto
        else: # new mapping
            r = MemRangeDesc(rec.desc, rec.addrfrom, rec.addrto)
            ranges[rec.addrfrom] = r
    ranges = list(ranges.values())
    ranges.sort(key=lambda e:e.addrfrom)
    return MemRanges(ranges)

