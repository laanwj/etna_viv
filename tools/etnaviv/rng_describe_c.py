from etnaviv.parse_rng import BitField, BaseType, Enum, BitSet, Domain

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
        terms = (describe_c_inner(prefix, field, field.extract(value)) for field in typ.bitfields)
        terms = [t for t in terms if t is not None]
        if terms:
            return ' | '.join(terms)
        else:
            return '0'
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

def describe_c(path, value):
    '''Describe state value as C expression.'''
    prefix = format_path_c(path, True)
    return describe_c_inner(prefix, path[-1][0].type, value)

