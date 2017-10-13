from collections import namedtuple
CommandInfo = namedtuple('CommandInfo', ['ptr', 'value', 'op', 'payload_ofs', 'desc', 'state_info'])
StateInfo = namedtuple('StateInfo', ['pos', 'format'])
CmdStreamInfo = namedtuple('CmdStreamInfo', ['opcodes', 'domain'])

# Number of words to ignore at start of command buffer
# A PIPE3D command will be inserted here by the kernel if necessary
CMDBUF_IGNORE_INITIAL = 8

# known command payload sizes
# could fill this in from cmdstream.xml too
CMD_PAYLOAD_SIZES = {
    1: 1,  # LOAD_STATE (per state)
    2: 1,  # END
    3: 0,  # NOP
    4: 2,  # DRAW_2D (per rectangle)
    5: 3,  # DRAW_PRIMITIVES
    6: 4,  # DRAW_INDEXED_PRIMITIVES
    7: 0,  # WAIT
    8: 1,  # LINK
    9: 1,  # STALL
    10: 3, # CALL
    11: 0, # RETURN
    12: 2, # DRAW_INSTANCED
    13: 0, # CHIP_SELECT
    15: 1, # WAIT_FENCE
    16: 1, # DRAW_INDIRECT
    19: 0, # SNAP_PAGES
}

PLO_CMD = -1
PLO_PAD = -2
PLO_INITIAL_PAD = -3

def _describe(t, v):
    return t.describe(v)

def parse_command_buffer(buffer_words, cmdstream_info, initial_padding=CMDBUF_IGNORE_INITIAL, describe=_describe):
    '''
    Parse Vivante command buffer contents, return a sequence of 
    CommandInfo records.
    '''
    # TODO: implement in terms of annotate_command_buffer to avoid code duplication
    state_base = 0
    state_count = 0
    state_format = 0
    next_cmd = initial_padding
    payload_start_ptr = 0
    payload_end_ptr = 0
    op = 0
    ptr = 0
    for value in buffer_words:
        state_info = None
        if ptr >= next_cmd:
            payload_ofs = PLO_CMD
            op = value >> 27
            payload_start_ptr = payload_end_ptr = ptr + 1
            try:
                opname = cmdstream_info.opcodes.values_by_value[op].name
            except KeyError:
                opname = None
            opinfo = None
            if opname is not None:
                try:
                    opinfo = cmdstream_info.domain.lookup_address(0,(opname,'FE_OPCODE'))
                except KeyError:
                    pass
            desc = '%s (%i)' % (opname or 'UNKNOWN', op)
            if op == 1:
                state_base = (value & 0xFFFF)<<2
                state_count = (value >> 16) & 0x3FF
                if state_count == 0:
                    state_count = 0x400
                state_format = (value >> 26) & 1
                payload_end_ptr = payload_start_ptr + state_count
                desc += " Base: 0x%05X Size: %i Fixp: %i" % (state_base, state_count, state_format)
            else:
                payload_end_ptr = payload_start_ptr + CMD_PAYLOAD_SIZES.get(op, 1)
            if op != 1 and opinfo is not None:
                desc += " " + opinfo[-1][0].describe(value)
            next_cmd = (payload_end_ptr + 1) & (~1)
        elif ptr < payload_end_ptr: # Parse payload 
            payload_ofs = ptr - payload_start_ptr
            desc = ""
            if op == 1:
                pos = payload_ofs*4 + state_base
                state_info = StateInfo(pos, state_format)
            elif opname is not None:
                try:
                    opinfo = cmdstream_info.domain.lookup_address(payload_ofs*4+4,(opname,'FE_OPCODE'))
                except KeyError:
                    pass
                else:
                    desc = '  ' + opinfo[-1][0].name + ' ' + describe(opinfo[-1][0], value)
        else:
            desc = "PAD"
            if ptr < initial_padding:
                payload_ofs = PLO_INITIAL_PAD # initial padding
            else:
                payload_ofs = PLO_PAD # padding
        yield CommandInfo(ptr, value, op, payload_ofs, desc, state_info)
        ptr += 1

CmdBufEntry = namedtuple('CmdBufferEntry', ['cat', 'op', 'ofs'])
def annotate_command_buffer(cmdbuf):
    '''Produce annotation for command buffer values'''
    next_cmd = 0
    payload_start_ptr = 0
    op = None
    prev_ptr = None
    ptr = 0
    for value in cmdbuf:
        if value is not None:
            if ptr >= next_cmd:
                op = value >> 27
                payload_start_ptr = ptr + 1
                if op == 1:
                    state_base = (value & 0xFFFF)<<2
                    state_count = (value >> 16) & 0x3FF
                    if state_count == 0:
                        state_count = 0x400
                    state_format = (value >> 26) & 1
                    payload_end_ptr = payload_start_ptr + state_count
                else:
                    payload_end_ptr = payload_start_ptr + CMD_PAYLOAD_SIZES.get(op, 1)
                next_cmd = (payload_end_ptr + 1) & (~1)
                yield CmdBufEntry('op', op, 0)
            elif ptr < payload_end_ptr: # Parse payload 
                payload_ofs = ptr - payload_start_ptr
                desc = ''
                if op == 1:
                    yield CmdBufEntry('state', None, state_base + payload_ofs*4)
                else:
                    yield CmdBufEntry('op', op, payload_ofs+4)
            else:
                yield CmdBufEntry('pad', None, None)
        else:
            yield CmdBufEntry('pad', None, None)
        ptr += 1


