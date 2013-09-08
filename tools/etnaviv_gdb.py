# GDB plugin for etnaviv driver debugging.
# This needs gdb 7.5+ to work.
#
# usage (from gdb):
#    source /path/to/etnaviv_gdb.py
#
# Commands:
#    gpu-state (prefix|uniforms)
#        Show full GPU state (default) or only registers with a certain prefix.
#        The special prefix 'uniforms' shows only the shader uniforms.
#    gpu-dis
#        Disassemble the current shaders.
#
from __future__ import print_function, division, unicode_literals
import sys,os
from collections import namedtuple
# Add script directory to python path (seems that gdb does not do this automatically)
# need this to import etnaviv.*
if not os.path.dirname(__file__) in sys.path:
    sys.path.append(os.path.dirname(__file__))

from etnaviv.util import rnndb_path
from etnaviv.parse_rng import parse_rng_file, format_path, BitSet, Domain, Stripe, Register, Array, BaseType
from etnaviv.dump_cmdstream_util import int_as_float, fixp_as_float, offset_to_uniform
from etnaviv.rnn_domain_visitor import DomainVisitor
from etnaviv.parse_command_buffer import parse_command_buffer

# (gdb) print ((struct etna_pipe_context*)((struct gl_context*)_glapi_Context)->st->cso_context->pipe)
# pipe-> gpu3d           has current GPU state
# pipe->ctx (etna_ctx)   has command buffers

# Ideas:
# - print changes in current GPU state highlighted
# - can we hook etna_flush and display the current command buffer contents?
#   set breakpoint that invokes python code? is that possible?
# (see gdb.Breakpoint)
# - dump current command buffers

RegStride = namedtuple('RegStride', ['stride', 'length'])
RegInfo = namedtuple('RegInfo', ['reg', 'offset', 'strides'])

class StateCollector(DomainVisitor):
    '''
    Walk rnndb domain, collect all register names,
    build a dictionary of register name to object.
    '''
    def __init__(self):
        self.registers = {}
        self.path = [(0, [], [])]

    def extend_path(self, node):
        offset, name, strides = self.path[-1]
        offset = offset + node.offset
        if node.name is not None: # don't append, we want a copy
            name = name + [node.name]
        if node.length != 1: # don't append, we want a copy
            strides = strides + [RegStride(node.stride, node.length)]
        self.path.append((offset, name, strides))

    def visit_stripe(self, node):
        self.extend_path(node)
        DomainVisitor.visit_stripe(self, node)
        self.path.pop()
    
    def visit_array(self, node):
        self.extend_path(node)
        DomainVisitor.visit_array(self, node)
        self.path.pop()

    def visit_register(self, node):
        self.extend_path(node)
        offset, name, strides = self.path[-1]
        name = '_'.join(name)
        # sort strides by decreasing size to make sure child addresses are in increasing order
        strides.sort(key=lambda x:-x.stride)
        self.registers[name] = RegInfo(node, offset, strides)
        self.path.pop()

def build_registers_dict(domain):
    '''
    Build dictionary of GPU registers from their C name (_ delimited)
    to a RegisterInfo named tuple with (register description, offset, strides).
    '''
    col = StateCollector()
    col.visit(domain)
    return col.registers

def lookup_etna_state():
    '''
    Return etna pipe and screen from current Mesa GL context.
    @returns a tuple (pipe, screen)
    '''
    glapi_context_sym,_ = gdb.lookup_symbol('_glapi_Context') 
    fbs_sym,_ = gdb.lookup_symbol('_fbs')
    etna_pipe_context_type = gdb.lookup_type('struct etna_pipe_context').pointer()
    etna_screen_type = gdb.lookup_type('struct etna_screen').pointer()
    if glapi_context_sym is not None: # Mesa
        gl_context_type = gdb.lookup_type('struct gl_context').pointer()

        glapi_context = glapi_context_sym.value()
        glapi_context = glapi_context.cast(gl_context_type)
        pipe = glapi_context['st']['cso_context']['pipe']
        screen = pipe['screen']
    elif fbs_sym is not None: # fbs scaffold
        fbs_sym = fbs_sym.value()
        pipe = fbs_sym['pipe']
        screen = fbs_sym['screen']
    else:
        print("Unable to find etna context")
        return (None, None)
    # cast to specific types
    pipe = pipe.cast(etna_pipe_context_type)
    screen = screen.cast(etna_screen_type)
    return (pipe, screen)

### gpu-state ###
# state formatting
def hex_and_float(x):
    return '%08x (%f)' % (x, int_as_float(x))
def hex_and_float_fixp(x):
    return '%08x (%f)' % (x, fixp_as_float(x))

special_format = {
    'VS_UNIFORMS': hex_and_float,
    'PS_UNIFORMS': hex_and_float,
    'SE_SCISSOR_LEFT': hex_and_float_fixp,
    'SE_SCISSOR_RIGHT': hex_and_float_fixp,
    'SE_SCISSOR_TOP': hex_and_float_fixp,
    'SE_SCISSOR_BOTTOM': hex_and_float_fixp
}

def format_state(reg, key, val):
    if key in special_format:
        return special_format[key](int(val))
    else:
        return reg.describe(int(val))

class GPUState(gdb.Command):
    """Etnaviv: show GPU state."""

    def __init__ (self, state_xml):
        super(GPUState, self).__init__ ("gpu-state", gdb.COMMAND_USER)
        self.state_xml = state_xml
        self.state_map = self.state_xml.lookup_domain('VIVS')
        self.registers = build_registers_dict(self.state_map)

    def print_uniforms_for(self, out, stype, uniforms, count):
        out.write('[%s uniforms]:\n' % stype)
        base = 0
        comps = 'xyzw'
        while base < count:
            sub = []
            for idx in xrange(0, 4): # last uniform can be partial
                if (base + idx)<count:
                    sub.append(int(uniforms[base + idx]))
            subf = (int_as_float(x) for x in sub)
            subfs = ', '.join((str(x) for x in subf))
            out.write('    u%i.%s = (%s)\n' % (base//4, comps[0:len(sub)], subfs))
            base += 4

    def print_uniforms(self, pipe):
        gpu3d = pipe['gpu3d']
        self.print_uniforms_for(sys.stdout, 'vs', gpu3d['VS_UNIFORMS'], int(pipe['shader_state']['vs_uniforms_size']))
        self.print_uniforms_for(sys.stdout, 'ps', gpu3d['PS_UNIFORMS'], int(pipe['shader_state']['ps_uniforms_size']))

    def invoke(self, arg, from_tty):
        self.dont_repeat()
        (pipe, screen) = lookup_etna_state()
        gpu3d = pipe['gpu3d']
        
        # Parse arguments
        if arg == 'uniforms':
            self.print_uniforms(pipe)
            return
        else: # register prefix
            prefix = arg.lower()

        for key in gpu3d.type.keys():
            if key in {'VS_UNIFORMS', 'PS_UNIFORMS', 'VS_INST_MEM', 'PS_INST_MEM'} or not key.lower().startswith(prefix):
                # handled separately
                continue
            val = gpu3d[key]
            typ = val.type.strip_typedefs()
            if key in self.registers:
                reg_desc = self.registers[key]
            else:
                print('Warning: no such register %s' % key)
                continue
            if typ.code == gdb.TYPE_CODE_INT:
                print("  %s: %s" % (key, format_state(reg_desc.reg,key,val)))
            elif typ.code == gdb.TYPE_CODE_ARRAY:
                size = typ.range()[1]+1
                print("  %s:" % key)
                for x in xrange(0, size):
                    subval = val[x]
                    subtyp = subval.type.strip_typedefs()
                    subaddr = reg_desc.offset + x * reg_desc.strides[0].stride
                    if subtyp.code == gdb.TYPE_CODE_INT:
                        print("    [%d] %s" % (x, format_state(reg_desc.reg,key,subval)))
        if not prefix:
            self.print_uniforms(pipe)

### gpu-dis(assemble) ###
from etnaviv.asm_common import format_instruction, disassemble
class GPUDisassemble(gdb.Command):
    """Etnaviv: disassemble shaders"""
 
    def __init__ (self, isa_xml):
        super(GPUDisassemble, self).__init__ ("gpu-disassemble", gdb.COMMAND_USER)
        self.isa = isa_xml
 
    def invoke(self, arg, from_tty):
        self.dont_repeat()
        (pipe, screen) = lookup_etna_state()
        shader_state = pipe['shader_state']
        vs_inst_size = int(shader_state['vs_inst_mem_size'])
        ps_inst_size = int(shader_state['ps_inst_mem_size'])
        vs_inst_mem = shader_state['VS_INST_MEM']
        ps_inst_mem = shader_state['PS_INST_MEM']
        self.disassemble(sys.stdout, 'vs', vs_inst_mem, vs_inst_size//4)
        self.disassemble(sys.stdout, 'ps', ps_inst_mem, ps_inst_size//4)

    def disassemble(self, out, stype, mem, count):
        out.write('[%s code]:\n' % stype)
        for idx in xrange(count):
            inst = (int(mem[idx*4+0]), int(mem[idx*4+1]), int(mem[idx*4+2]), int(mem[idx*4+3]))
            out.write('  %3x: ' % idx)
            out.write('%08x %08x %08x %08x  ' % inst)
            warnings = []
            parsed = disassemble(self.isa, inst, warnings)
            text = format_instruction(self.isa, parsed)
            out.write(text)
            if warnings:
                out.write(' ; ')
                out.write(' '.join(warnings))
            # XXX show current values when loading from uniforms
            out.write('\n')

### gpu-trace ###
from etnaviv.asm_common import format_instruction, disassemble

def indirect_memcpy(start_addr, end_addr):
    '''
    Indirect memory copy operation from inferior.

    Allocate memory in the inferior process, and copy over
    a range of memory using memcpy in the inferior.
    Subsequently this memory is read by the gdb process.
    This two-step process is needed because gdb refuses
    to access the memory mapped by the Vivante kernel
    driver.
    '''
    inferior = gdb.selected_inferior()
    size_t_type = gdb.lookup_type('size_t') # same size as pointer
    # These don't have debug symbols, so we need to be very careful
    # about the types passed in. Cast them all to size_t.
    malloc = gdb.parse_and_eval('malloc')
    free = gdb.parse_and_eval('free')
    memcpy = gdb.parse_and_eval('memcpy')

    start_addr = gdb.Value(start_addr).cast(size_t_type)
    size = gdb.Value(end_addr - start_addr).cast(size_t_type)

    try:
        # allocate memory and cast result to size_t
        ptr = malloc(size).cast(size_t_type)
        # copy from client memory to client memory
        memcpy(ptr, start_addr, size)
        # read from client memory
        return inferior.read_memory(ptr, size)
    finally: # ensure free of temp buffer to prevent memory leak
        free(ptr)

import struct
class CommitBreakpoint(gdb.Breakpoint):
    def __init__(self, state_map, do_stop, output):
        super(CommitBreakpoint, self).__init__('viv_commit')
        self.state_map = state_map
        self.size_t_type = gdb.lookup_type('size_t') # must have same size as pointer
        self.uint32_t_ptr_type = gdb.lookup_type('uint32_t').pointer() # command words
        self.do_stop = do_stop
        self.output = output

    def memory_iterator(self, start_addr, end_addr):
        addr = start_addr
        while addr < end_addr:
            yield int(self.viv_read_u32(addr))
            addr += 4
    
    def format_state(self, pos, value, fixp):
        try:
            path = self.state_map.lookup_address(pos)
            path_str = format_path(path)
        except KeyError:
            path = None
            path_str = '0x%05X' % pos
        desc = '  ' + path_str 
        if fixp:
            desc += ' = %f' % fixp_as_float(value)
        else:
            # For uniforms, show float value
            if (pos >= 0x05000 and pos < 0x06000) or (pos >= 0x07000 and pos < 0x08000):
                num = pos & 0xFFF
                desc += ' := %f (%s)' % (int_as_float(value), offset_to_uniform(num))
            elif path is not None:
                register = path[-1][0]
                desc += ' := ' + register.describe(value)
        return desc

    def stop(self):
        commandBuffer_sym,_ = gdb.lookup_symbol('commandBuffer') 
        commandBuffer = commandBuffer_sym.value(gdb.newest_frame())
        # offset, startOffset
        # physical
        offset = int(commandBuffer['offset'])
        startOffset = int(commandBuffer['startOffset'])
        physical = int(commandBuffer['physical'].cast(self.size_t_type)) # GPU address
        logical = int(commandBuffer['logical'].cast(self.size_t_type)) # CPU address

        buffer = indirect_memcpy(logical + startOffset, logical + offset)
        data = struct.unpack_from(b'%dI' % (len(buffer)/4), buffer)

        # "cast" byte-based buffer to uint32_t
        # iterate over buffer, one 32 bit word at a time
        f = sys.stdout if self.output is None else self.output
        f.write('viv_commit:\n')
        for rec in parse_command_buffer(data):
            if rec.state_info is not None:
                desc = self.format_state(rec.state_info.pos, rec.value, rec.state_info.format)
            else:
                desc = rec.desc
            f.write('  [%08x] %08x %s\n' % (physical + startOffset + rec.ptr*4, rec.value, desc))
        f.flush()
        return self.do_stop

class GPUTrace(gdb.Command):
    """Etnaviv: trace submitted command buffers
    Usage: 
      gpu-trace <on|off>      Enable/disable cmdbuffer trace
      gpu-trace stop <on|off> Enable/disable stopping on commit
      gpu-trace output stdout Set tracing output to stdout (default)
      gpu-trace output file <name>   Set tracing output to file
    """
 
    def __init__ (self, state_xml):
        super(GPUTrace, self).__init__ ("gpu-trace", gdb.COMMAND_USER)
        self.enabled = False
        self.state_xml = state_xml
        self.state_map = self.state_xml.lookup_domain('VIVS')
        self.bp = None
        self.stop_on_commit = False
        self.output = None
 
    def invoke(self, arg, from_tty):
        self.dont_repeat()
        arg = gdb.string_to_argv(arg)
        if not arg:
            pass # just check status
        elif arg[0] == 'off': # disable
            if not self.enabled:
                print("GPU tracing is not enabled")
                return
            self.enabled = False
            self.bp.delete()
            self.bp = None
        elif arg[0] == 'on': # enable
            if self.enabled:
                print("GPU tracing is already enabled")
                return
            self.enabled = True
            self.bp = CommitBreakpoint(self.state_map, self.stop_on_commit, self.output)
        elif arg[0] == 'stop':
            if arg[1] == 'off':
                self.stop_on_commit = False
            elif arg[1] == 'on':
                self.stop_on_commit = True
            else:
                print("Unrecognized stop mode %s" % arg[1])
            if self.bp: # if breakpoint currently exists, change parameter on the fly
                self.bp.do_stop = self.stop_on_commit
        elif arg[0].startswith('out'): # output
            new_output = self.output
            if arg[1] == 'file':
                new_output = open(arg[2],'w')
            elif arg[1] == 'stdout':
                new_output = None
            else:
                print("Unrecognized output mode %s" % arg[1])
            if new_output is not self.output:
                if self.output is not None: # close old output file
                    self.output.close()
                self.output = new_output
                if self.bp: # if breakpoint currently exists, change parameter on the fly
                    self.bp.output = self.output
        else:
            print("Unrecognized tracing mode %s" % arg[0])
            return
        print("Etnaviv command buffer tracing %s (stop %s, output to %s)" % (
            ['disabled','enabled'][self.enabled],
            ['disabled','enabled'][self.stop_on_commit],
            self.output or '<stdout>'))

### gpu-inspect ###
class GPUInspect(gdb.Command):
    """Etnaviv: inspect etna resource
    Usage:
      gpu-inspect <resource>
    """

    def __init__ (self):
        super(GPUInspect, self).__init__ ("gpu-inspect", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        self.dont_repeat()
        arg = gdb.string_to_argv(arg)
        arg[0] = gdb.parse_and_eval(arg[0])
        etna_resource_type = gdb.lookup_type('struct etna_resource').pointer()
        res = arg[0].cast(etna_resource_type)
        # this is very, very primitive now
        # dump first 128 bytes of level 0 by default, as floats
        # XXX make this more flexible
        logical = res['levels'][0]['logical']
        size = 128
        buffer = indirect_memcpy(logical, logical+size)
        data = struct.unpack_from(b'%df' % (len(buffer)/4), buffer)
        print(data)

state_xml = parse_rng_file(rnndb_path('state.xml'))
isa_xml = parse_rng_file(rnndb_path('isa.xml'))

GPUState(state_xml)
GPUDisassemble(isa_xml) 
GPUTrace(state_xml)
GPUInspect()

