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
from etnaviv.dump_cmdstream_util import int_as_float, fixp_as_float
from etnaviv.rnn_domain_visitor import DomainVisitor

# (gdb) print ((struct etna_pipe_context*)((struct gl_context*)_glapi_Context)->st->cso_context->pipe)
# pipe-> gpu3d           has current GPU state
# pipe->ctx (etna_ctx)   has command buffers

# Ideas:
# - print changes in current GPU state highlighted

# - can we hook etna_flush and display the current command buffer contents?
#   set breakpoint that invokes python code? is that possible?
# (see gdb.Breakpoint)

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
    gl_context_type = gdb.lookup_type('struct gl_context').pointer()
    etna_pipe_context_type = gdb.lookup_type('struct etna_pipe_context').pointer()
    etna_screen_type = gdb.lookup_type('struct etna_screen').pointer()

    glapi_context = glapi_context_sym.value()
    glapi_context = glapi_context.cast(gl_context_type)
    pipe = glapi_context['st']['cso_context']['pipe']
    screen = pipe['screen']
    # case to specific types
    pipe = pipe.cast(etna_pipe_context_type)
    screen = pipe.cast(etna_screen_type)
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

    def __init__ (self):
        super(GPUState, self).__init__ ("gpu-state", gdb.COMMAND_USER)
        self.state_xml = parse_rng_file(rnndb_path('state.xml'))
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
        (pipe, screen) = lookup_etna_state()
        gpu3d = pipe['gpu3d']
        
        # Parse arguments
        if arg == 'uniforms':
            self.print_uniforms(pipe)
            return
        else: # register prefix
            prefix = arg.lower()

        for key in gpu3d.type.keys():
            if key in {'VS_UNIFORMS', 'PS_UNIFORMS'} or not key.lower().startswith(prefix):
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
 
    def __init__ (self):
        super(GPUDisassemble, self).__init__ ("gpu-disassemble", gdb.COMMAND_USER)
        self.isa = parse_rng_file(rnndb_path('isa.xml'))
 
    def invoke(self, arg, from_tty):
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

GPUState()
GPUDisassemble() 

