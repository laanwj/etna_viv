import struct
from etnaviv.asm_common import format_instruction, disassemble

def disasm_format(out, isa, dialect, data, opt_addr=False, opt_raw=False, opt_cfmt=False):
    for idx in xrange(len(data)//16):
        inst = struct.unpack(b'<IIII', data[idx*16:idx*16+16])
        if opt_addr:
            if opt_cfmt:
                out.write('/* ')
            out.write('%3i: ' % idx)
            if opt_cfmt:
                out.write('*/ ')
        if opt_cfmt:
            out.write('0x%08x, 0x%08x, 0x%08x, 0x%08x,  ' % inst)
        elif opt_raw:
            out.write('%08x %08x %08x %08x  ' % inst)
        warnings = []
        parsed = disassemble(isa, dialect, inst, warnings)
        text = format_instruction(isa, dialect, parsed)
        if opt_cfmt:
            out.write('/* ')
        out.write(text)
        if opt_cfmt:
            out.write(' */')
        if warnings:
            out.write('\t; ')
            out.write(' '.join(warnings))
        out.write('\n')



