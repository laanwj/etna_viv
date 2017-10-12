from etnaviv.parse_rng import BitField, BaseType, Enum, BitSet, Domain
from etnaviv.parse_command_buffer import PLO_INITIAL_PAD

def format_path_c(path, only_prefix=False):
    '''Format path into state space as C string'''
    retval = []
    indices = []
    for obj,idx in path:
        if idx is not None:
            indices.append(str(idx))
        retval.append(obj.name)

    name = '_'.join(retval)
    if indices and not only_prefix:
        name += '(' + (','.join(indices)) + ')'
    return name

def describe_c_inner(prefix, typ, value):
    '''
    Visitor to descibe expression in C format.
    '''
    if isinstance(typ, BitField):
        if isinstance(typ.type, BaseType) and typ.type.kind == 'boolean':
            if value:
                return prefix + '_' + typ.name
            else:
                return None
        elif isinstance(typ.type, Enum) and typ.type.name is None:
            return prefix + '_' + typ.name + '_' + typ.type.values_by_value[value].name
        else:
            return prefix + '_' + typ.name + '(' + describe_c_inner(prefix, typ.type, value) + ')'
    elif isinstance(typ, BitSet):
        if typ.masked: # no way to handle this right now
            return '0x%08x' % value
        terms = (describe_c_inner(prefix, field, field.extract(value)) for field in typ.bitfields)
        terms = [t for t in terms if t is not None]
        if terms:
            return ' | '.join(terms)
        else:
            return '0'
    elif isinstance(typ, Domain):
        return '*0x%08x' % value  # address: need special handling
    elif isinstance(typ, BaseType):
        if typ.kind in {'hex','float','fixedp'}:
            return '0x%x' % value
        else:
            return '%d' % value
    elif isinstance(typ, Enum):
        return (typ.name or prefix) + '_' + typ.values_by_value[value].name
    raise NotImplementedError(type(typ))

def describe_c(path, value):
    '''Describe state value as C expression.'''
    prefix = format_path_c(path, True)
    return describe_c_inner(prefix, path[-1][0].type, value)

def _format_addr_default(x):
    return '*0x%08x' % x
def dump_command_buffer_c(f, recs, state_map, format_addr=_format_addr_default):
    '''Dump parsed command buffer as C'''
    for rec in recs:
        if rec.payload_ofs == PLO_INITIAL_PAD:
            continue

        if rec.state_info is not None:
            try:
                path = [(state_map,None)] + state_map.lookup_address(rec.state_info.pos)
            except KeyError:
                f.write('/* Warning: unknown state %05x */\n' % rec.state_info.pos)
            else:
                # could pipe this to clang-format to format and break up lines etc
                #f.write('etna_set_state(stream, %s, %s);\n' % (
                #    format_path_c(path),
                #    describe_c(path, rec.value)))
                if isinstance(path[-1][0].type, Domain):
                    assert(not rec.state_info.format)
                    f.write('etna_set_state_reloc(stream, %s, %s);\n' % (
                        format_path_c(path), format_addr(rec.value)))
                elif rec.state_info.format:
                    f.write('etna_set_state_fixp(stream, %s, 0x%08x);\n' % (
                        format_path_c(path), rec.value))
                else:
                    f.write('etna_set_state(stream, %s, 0x%08x);\n' % (
                        format_path_c(path), rec.value))
        else:
            # Handle other commands
            if rec.op != 1:
                f.write('etna_cmd_stream_emit(stream, 0x%08x); /* command %s */\n' % (rec.value, rec.desc))

def dump_command_buffer_c_raw(f, recs, state_map):
    '''Dump parsed command buffer as C'''
    for rec in recs:
        if rec.state_info is not None:
            try:
                path = [(state_map,None)] + state_map.lookup_address(rec.state_info.pos)
            except KeyError:
                raise
            else:
                if isinstance(path[-1][0].type, Domain):
                    f.write('etna_cmd_stream_reloc(stream, *0x%08x); /* %s */\n' % (rec.value, format_path_c(path)))
                else:
                    f.write('etna_cmd_stream_emit(stream, 0x%08x); /* %s */\n' % (rec.value, format_path_c(path)))
        else:
            f.write('etna_cmd_stream_emit(stream, 0x%08x); /* command %s */\n' % (rec.value, rec.desc))

