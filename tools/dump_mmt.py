#!/usr/bin/python3
# Parse mmt log
import sys
import struct
import argparse
from collections import namedtuple
from etnaviv.mmt import parse_mmt_file, LogMessage, Open, Mmap, StoreInfo, Store, Commit, ProcessMap
from etnaviv.parse_command_buffer import annotate_command_buffer
from etnaviv.parse_proc_map import extract_mem_ranges
from etnaviv.textutil import pad_right
import json

class ATTRS:
    state = '\x1b[93m'
    header = '\x1b[92m'
    th = '\x1b[96m'
    end = '\x1b[0m'
    top = '\x1b[97m'

REGION_COMMIT = -1
REGION_MEMRANGES = -2

def mmt_file_states(f):
    '''
    Get writes to command buffer from raw file into format suitable for command
    buffer parsing.
    '''
    info = None
    lastpos = None
    for rec in parse_mmt_file(f):
        if isinstance(rec, StoreInfo):
            info = rec.msg
        elif isinstance(rec, Store):
            if len(rec.data) == 4:
                yield (rec.region_id,rec.offset, rec.data, info)
            elif len(rec.data) == 8:
                yield (rec.region_id,rec.offset, rec.data[0:4], info)
                yield (rec.region_id,rec.offset+4, rec.data[4:8], info)
            else:
                assert(0)
        elif isinstance(rec, Commit):
            yield (REGION_COMMIT, 0, b'', b'')
        elif isinstance(rec, ProcessMap):
            #print('{a.header}Process map{a.end}'.format(a=ATTRS))
            #print(rec.msg.decode('utf8'))
            yield (REGION_MEMRANGES, 0, b'', extract_mem_ranges(rec.msg))

def reconstruct_cmdbuf(states):
    minofs = min(s[0] for s in states)
    maxofs = max(s[0] for s in states) + 1
    cmdbuf = [(None, None)] * (maxofs-minofs)
    for (ptr, value, info) in states:
        if cmdbuf[ptr - minofs][0] is not None:
            print('Warning: Duplicate state at %08x' % ptr)
        cmdbuf[ptr - minofs] = (value, info)
    return cmdbuf

def update_mappings(states, state_to_info, cmd_to_info):
    '''
    Create mapping from state to address, and from cmd,ofs to address
    respectively.
    '''
    cmdbuf = reconstruct_cmdbuf(states)
    for (ann, (value,info)) in zip(annotate_command_buffer(s[0] for s in cmdbuf), cmdbuf):
        if ann.cat == 'op':
            cmd_to_info.add((ann.op, ann.ofs, info))
        elif ann.cat == 'state':
            state_to_info.add((ann.ofs, info))

def dump_states(states, show_depth):
    cmdbuf = reconstruct_cmdbuf(states)
    ptr = 0 
    for (ann, (value,info)) in zip(annotate_command_buffer(s[0] for s in cmdbuf), cmdbuf):
        if ann.cat == 'op':
            desc = str(ann.op)
        elif ann.cat == 'state':
            desc = '->[{a.state}{:05X}{a.end}]'.format(ann.ofs,a=ATTRS)
        elif ann.cat == 'pad':
            desc = 'PAD'

        value_str = 'xxxxxxxx'
        if value is not None:
            value_str = '%08x' % value
        print('{:08x} {} {} {}'.format(ptr, value_str, pad_right(desc,10), format_loc(info[0]) if info else None))
        if show_depth and info is not None:
            for line in info[1:]:
                print('                             {}'.format(format_loc(line),a=ATTRS))
        ptr += 1

LocInfo = namedtuple('LocInfo', ['basename', 'offset', 'func', 'meta'])
def parse_loc_info(info, memranges):
    items = []
    for line in info.split(b'\n'):
        (addr, _, meta) = line.partition(b':')
        addr = int(addr, 0)
        meta = meta.lstrip().decode()
        func, _, meta = meta.partition(' ')
        r = memranges.lookup(addr) if memranges is not None else None
        if r:
            locspec = LocInfo(r.basename.decode(), addr - r.addrfrom, func, meta)
        else:
            locspec = LocInfo(None, addr, func, meta)
        items.append(locspec)
    return tuple(items)

def mmt_state_blocks(fstate_iter):
    # Group state blocks (command buffers) from
    # mmt file. These may be out of order, use
    # reconstruct_cmdbuf to re-sort them.
    prev_region = None
    states = []
    memranges = None

    for (region, ptr, data, info) in fstate_iter:
        if region == REGION_COMMIT:
            if states:
                yield(states)
            states = []
        elif region == REGION_MEMRANGES:
            memranges = info
        else:
            if prev_region != None and region != prev_region:
                if states:
                    yield states
                states = []

            ptr >>= 2
            info = parse_loc_info(info, memranges)
            value = struct.unpack('<I', data)[0]
            states.append((ptr, value, info))
        prev_region = region

def uninteresting_loc(info):
    for loc in info:
        if loc.func.startswith('gcoBUFFER_EndTEMPCMDBUF'):
            return True
    return False

def format_loc(loc):
    return '{loc.basename}+0x{loc.offset:x}: {loc.func}'.format(loc=loc,a=ATTRS)

def dump_mmt_file(f, verbose, show_depth, as_json):
    state_to_info = set()
    cmd_to_info = set()
    for states in mmt_state_blocks(mmt_file_states(f)):
        if verbose:
            print()
            dump_states(states, show_depth)
        update_mappings(states, state_to_info, cmd_to_info)

    if not as_json:
        print('{a.header}States{a.end}'.format(a=ATTRS))
    per_state = []
    for addr,info in sorted(state_to_info):
        if uninteresting_loc(info): # not interested in this, usually
            # if this happens and there is no other source that would indicate a bug
            continue
        if not as_json:
            print('[{a.state}{:05X}{a.end}] {a.top}{}{a.end}'.format(addr,format_loc(info[0]),a=ATTRS))
        if show_depth:
            for line in info[1:]:
                print('        {}'.format(format_loc(line),a=ATTRS))
        per_state.append({'state':addr, 'loc':[i._asdict() for i in info]})
    if not as_json:
        print()
        print('{a.header}Commands{a.end}'.format(a=ATTRS))
        print('{a.th}cmd   ofs  addr{a.end}'.format(a=ATTRS))
    per_command = []
    for cmd,ofs,info in sorted(cmd_to_info):
        if cmd == 1:
            continue # State loading is above
        if not as_json:
            print('0x%02x +0x%02x %s' % (cmd, ofs, format_loc(info[0])))
        per_command.append({'cmd':cmd,'ofs':ofs,'loc':[i._asdict() for i in info]})
    if as_json:
        json.dump({'states':per_state,'commands':per_command}, sys.stdout, indent=4)

def dump_mmt(filename, verbose, show_depth, as_json):
    with open(filename, 'rb') as f:
        dump_mmt_file(f, verbose, show_depth, as_json)

def parse_arguments():
    parser = argparse.ArgumentParser(description='Dump mmt')
    parser.add_argument('-d','--show-depth', dest='show_depth',
            default=False, action='store_const', const=True,
            help='Show stack traces')
    parser.add_argument('-v','--verbose', dest='verbose',
            default=False, action='store_const', const=True,
            help='Show verbose output while processing')
    parser.add_argument('-j','--json', dest='json',
            default=False, action='store_const', const=True,
            help='Export as JSON format')
    parser.add_argument('input', metavar='INFILE', type=str,
            help='Input mmt file')
    return parser.parse_args()

def main():
    args = parse_arguments()
    dump_mmt(args.input, args.verbose, args.show_depth, args.json)

if __name__ == '__main__':
    main()
