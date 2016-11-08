from etnaviv.parse_rng import BitField, BaseType, Enum, BitSet, Domain

def format_path_c(path, only_prefix):
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

def describe_c(prefix, typ, value):
    '''
    Visitor to descibe expression in C format.
    '''
    if isinstance(typ, BitField):
        if isinstance(typ.type, BaseType) and typ.type.kind == 'boolean':
            return prefix + '_' + typ.name
        elif isinstance(typ.type, Enum) and typ.type.name is None:
            return prefix + '_' + typ.name + '_' + typ.type.values_by_value[value].name
        else:
            return prefix + '_' + typ.name + '(' + describe_c(prefix, typ.type, value) + ')'
    elif isinstance(typ, BitSet):
        return ' | '.join(describe_c(prefix, field, field.extract(value)) for field in typ.bitfields)
    elif isinstance(typ, Domain):
        return '*0x%08x' % value  # address: need special handling
    elif isinstance(typ, BaseType):
        if typ.kind == 'hex':
            return '0x%x' % value
        else:
            return '%d' % value
    elif isinstance(typ, Enum):
        return (typ.name or prefix) + '_' + typ.values_by_value[value].name
    raise NotImplementedError(type(typ))


