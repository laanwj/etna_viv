from collections import namedtuple
CommandInfo = namedtuple('CommandInfo', ['ptr', 'value', 'op', 'payload_ofs', 'desc', 'state_info'])
StateInfo = namedtuple('StateInfo', ['pos', 'format'])

# Number of words to ignore at start of command buffer
# A PIPE3D command will be inserted here by the kernel if necessary
CMDBUF_IGNORE_INITIAL = 8

def parse_command_buffer(buffer_words):
    '''
    Parse Vivante command buffer contents, return a sequence of 
    CommandInfo records.
    '''
    state_base = 0
    state_count = 0
    state_format = 0
    next_cmd = CMDBUF_IGNORE_INITIAL
    payload_start_ptr = 0
    payload_end_ptr = 0
    op = 0
    ptr = 0
    for value in buffer_words:
        state_info = None
        if ptr >= next_cmd:
            payload_ofs = -1
            op = value >> 27
            payload_start_ptr = payload_end_ptr = ptr + 1
            if op == 1:
                state_base = (value & 0xFFFF)<<2
                state_count = (value >> 16) & 0x3FF
                if state_count == 0:
                    state_count = 0x400
                state_format = (value >> 26) & 1
                payload_end_ptr = payload_start_ptr + state_count
                desc = "LOAD_STATE (1) Base: 0x%05X Size: %i Fixp: %i" % (state_base, state_count, state_format)
            elif op == 2:
                desc = "END (2)"
            elif op == 3:
                desc = "NOP (3)"
            elif op == 4:
                desc = "DRAW_2D (4)"
            elif op == 5:
                desc = "DRAW_PRIMITIVES (5)"
                payload_end_ptr = payload_start_ptr + 3
            elif op == 6:
                desc = "DRAW_INDEXED_PRIMITIVES (6)"
                payload_end_ptr = payload_start_ptr + 4
            elif op == 7:
                desc = "WAIT (7)"
            elif op == 8:
                desc = "LINK (8)"
                payload_end_ptr = payload_start_ptr + 1
            elif op == 9:
                desc = "STALL (9)"
                payload_end_ptr = payload_start_ptr + 1
            elif op == 10:
                desc = "CALL (10)"
                payload_end_ptr = payload_start_ptr + 1
            elif op == 11:
                desc = "RETURN (11)"
            elif op == 13:
                desc = "CHIP_SELECT (13)"
            else:
                desc = "UNKNOWN (%i)" % op
            next_cmd = (payload_end_ptr + 1) & (~1)
        elif ptr < payload_end_ptr: # Parse payload 
            payload_ofs = ptr - payload_start_ptr
            desc = ""
            if op == 1:
                pos = payload_ofs*4 + state_base
                state_info = StateInfo(pos, state_format)
        else:
            desc = "PAD"
            payload_ofs = -2 # padding
        yield CommandInfo(ptr, value, op, payload_ofs, desc, state_info)
        ptr += 1

