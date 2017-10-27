'''
Parse rules-ng-ng XML format.

See rules-ng-ng.xsd and rules-ng-ng.xml for documentation of the format.
'''
# Copyright (c) 2012-2013 Wladimir J. van der Laan
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sub license,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the
# next paragraph) shall be included in all copies or substantial portions
# of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
from __future__ import print_function, division, unicode_literals
import os, sys, struct
from collections import OrderedDict
from lxml import etree as ET # parsing
from itertools import izip
from os import path
from etnaviv.floatutil import int_as_float

ns = "{http://nouveau.freedesktop.org/}"
XML_BOOL = {'1':True, '0':False, 'false':False, 'true':True, 'yes':True, 'no':False}
MASK_FIELD_SUFFIX = '_MASK'

# Simple interval arithmetic
# XXX move to utils package
INTERVAL_MIN = 0
INTERVAL_MAX = 1<<64
EMPTY_INTERVAL = (INTERVAL_MAX, INTERVAL_MIN)
FULL_INTERVAL = (INTERVAL_MIN, INTERVAL_MAX)
def interval_union(bounds1, bounds2):
    '''Return the union of two intervals'''
    return (min(bounds1[0], bounds2[0]), max(bounds1[1], bounds2[1]))
def interval_add(bounds, val):
    '''Shift an interval with a specified value'''
    return (max(INTERVAL_MIN, bounds[0] + val),
            min(INTERVAL_MAX, bounds[1] + val))
def interval_check(bounds, val):
    '''Check if val lies within this interval'''
    return bounds[0] <= val < bounds[1]

#-------------------------------------------------------------------------
# Helper classes

class RNNObject(object):
    '''
    Rules-ng description object, common attributes are here.
    '''
    name = None # object name (None if anonymous)
    parent = None # back-reference to parent object
    brief = '' # short documentation
    doc = '' # long documentation
    varset = ''
    variants = ''
    def __init__(self, parent, **attr):
        self.parent = parent
        self.name = attr.get('name', None)
        self.parent = attr.get('parent')
        self.varset = attr.get('varset', None)
        self.variants = attr.get('variants', None)
        # Extension to rules-ng-ng: allow brief as attribute as well as element
        self.brief = attr.get('brief', '')

    def add_child(self, child):
        return False

class TypedValue(object):
    type = None
    shr = None # Shiftright value, only makes sense for integer types
    anon_type = None # anonymous private type
    size = None
    masked = None  # does this register use masks for state groups?

    def __init__(self, **attr):
        self.type = attr.get('type', None)
        self.shr = attr.get('shr', None)
        self.masked = attr.get('masked', False)
        
    def add_child(self, child):
        # Creates an anonymous type
        if isinstance(child, BitField):
            if self.type is not None:
                raise ValueError('Register with type cannot have bitfield inside')
            if self.anon_type is None:
                self.anon_type = BitSet(self, masked=self.masked)
            self.anon_type.add_child(child)
            return True
        elif isinstance(child, EnumValue):
            if self.type is not None:
                raise ValueError('Register with type cannot have value inside')
            if self.anon_type is None:
                self.anon_type = Enum(self)
            self.anon_type.add_child(child)
            return True
        return False

class Range(object):
    '''
    Memory range that can contain registers and other memory ranges.
    '''
    contents = None # arrays, stripes and registers
    def __init__(self, **attr):
        self.contents = []
        self.contents_by_name = {}

    def lookup_address(self, addr, variants=None):
        '''Look up the specified address in this range. Return None if not found.'''
        raise KeyError
    
    def add_child(self, child):
        if isinstance(child, (Array, Stripe, Register)):
            self.contents.append(child)
            self.contents_by_name[child.name] = child
            return True
        return False

    def compute_bounds(self):
        '''
        Compute and propagate lower and upper bound and return as tuple. 
        '''
        return FULL_INTERVAL

class Type(object):
    '''
    Marker class for objects that can be used as type.
    '''
    pass

#-------------------------------------------------------------------------
#  Rules database objects: Types / values

class BaseType(Type):
    '''Basic (primitive) type'''
    INT = 'int'
    UINT = 'uint'
    BOOLEAN = 'boolean'
    HEX = 'hex'
    FLOAT = 'float'
    FIXEDP = 'fixedp'
    FIXEDPS = 'fixedps'

    TYPES = {INT, UINT, BOOLEAN, HEX, FLOAT, FIXEDP, FIXEDPS}

    kind = None

    def __init__(self, kind, size):
        assert(kind in self.TYPES)
        if kind == self.FLOAT and size not in [16,32,64]:
            raise ValueError('float must be size 16, 32 or 64, not %i' % size)
        self.kind = kind
        self.size = size

    def describe(self, value):
        # XXX need to propagate depth
        if self.kind == 'int':
            if value & (1<<(self.size-1)): # negative
                value -= 1<<self.size
            return '%i' % value
        elif self.kind == 'uint':
            return '%u' % value
        elif self.kind == 'boolean':
            return '%i' % value
        elif self.kind == 'hex':
            return '0x%x' % value
        elif self.kind == 'float':
            return '%f' % int_as_float(value, self.size)
        elif self.kind == 'fixedp':
            return '%f' % (value/(1<<(self.size//2)))
        elif self.kind == 'fixedps':
            if value > (1<<self.size)//2:
                value -= (1<<self.size)
            return '%f' % (value/(1<<(self.size//2)))

class EnumValue(RNNObject):
    '''Rules-ng enumeration value'''
    value = None
    def __init__(self, parent, **attr):
        RNNObject.__init__(self, parent, **attr)
        if self.name is None:
            raise ValueEror('Value must have name')
        self.value = attr['value']

class Enum(RNNObject, Type):
    '''Rules-ng enumeration'''
    values_by_value = None # ordered dict of EnumValues for fast lookup
    values_by_name = None # ordered dict of EnumValues for fast lookup
    def __init__(self, parent, **attr):
        RNNObject.__init__(self, parent, **attr)
        self.values_by_name = OrderedDict()
        self.values_by_value = OrderedDict()
    
    def add_child(self, child):
        if isinstance(child, EnumValue):
            self.values_by_value[child.value] = child
            self.values_by_name[child.name] = child
            return True
        return False

    def describe(self, value):
        '''
        Short description of the value.
        Look up value, return either the name of the enum value that has the value, or an hexadecimal value.'''
        try:
            return self.values_by_value[value].name
        except KeyError:
            return '0x%x' % value

class BitField(TypedValue, RNNObject):
    '''Rules-ng bitfield description'''
    high = None
    low = None
    def __init__(self, parent, **attr):
        RNNObject.__init__(self, parent, **attr)
        TypedValue.__init__(self, **attr)
        if 'high' in attr and 'low' in attr:
            if 'pos' in attr:
                raise ValueError('Cannot specify both low, high and pos for bitfield')
            self.high = attr['high']
            self.low = attr['low']
        elif 'pos' in attr:
            self.high = self.low = attr['pos']
        else:
            raise ValueError('bitfield has neither low/high nor pos')
        self.shr = int(attr.get('shr', '0'))
        # derived attribute
        self.size = self.high - self.low + 1

    @property
    def mask(self):
        '''Return mask for this bit field'''
        return ((1<<(self.high-self.low+1))-1) << self.low

    def extract(self, value):
        '''Extract this bit field from a value'''
        return ((value >> self.low) & ((1<<(self.high-self.low+1))-1)) << self.shr
    
    def fill(self, value):
        '''Return value filled into this bit field'''
        rv = (value << self.low)
        if rv != (rv & self.mask):
            raise ValueError('Value %i doesn\'t fit in mask %s' % (value, self.name))
        return rv

    def describe(self, value):
        return self.type.describe(self.extract(value))

class BitSet(RNNObject, Type):
    '''Rules-ng bitset description'''
    bitfields = None # List of bit fields
    masked = None # does this register use masks for state groups?
    def __init__(self, parent, **attr):
        RNNObject.__init__(self, parent, **attr)
        self.bitfields = []
        self.size = 0
        self.masked = attr.get('masked', False)
    
    def add_child(self, child):
        if isinstance(child, BitField):
            self.bitfields.append(child)
            # keep track of current size
            self.size = max(child.high+1, self.size)
            return True
        return False
    
    def describe(self, value):
        '''
        Short description of the value.
        '''
        fields = []
        if self.masked:
            # first, find out which fields are to be modified
            unmasked = set()
            mask_fields = set()
            residue = 0xffffffff
            for field in self.bitfields:
                if field.name.endswith(MASK_FIELD_SUFFIX) and field.size == 1:
                    mask_fields.add(field.name)
                    mask_fields.add(field.name[0:-len(MASK_FIELD_SUFFIX)])
                    if field.extract(value) == 0:
                        unmasked.add(field.name[0:-len(MASK_FIELD_SUFFIX)])
                        residue &= ~field.mask
            # then log fields that are unmaked
            for field in self.bitfields:
                if field.name in unmasked or (field.name not in mask_fields):
                    fields.append(field.name + '=' + field.describe(value))
                    residue &= ~field.mask
                    residue |= value & field.mask
            residue ^= value # residue are the bits that are not predicted by the masks
        else:
            residue = value
            for field in self.bitfields:
                fields.append(field.name + '=' + field.describe(value))
                residue &= ~field.mask
        rv = ','.join(fields)
        if residue != 0:
            rv += '(residue:0x%08x)' % residue
        return rv 

#-------------------------------------------------------------------------
#  Rules database objects: Memory ranges and registers

class Stripe(RNNObject, Range):
    '''Rules-ng stripe description.
    A stripe repeats its contents `length` times (defaults to 1),
    `stride` units apart (defaults to 0).
    '''
    offset = 0  # Offset into parent
    length = 1
    stride = 0
    # can contain registers, arrays and other stripes
    def __init__(self, parent, **attr):
        RNNObject.__init__(self, parent, **attr)
        Range.__init__(self, **attr)
        self.offset = attr.get('offset', 0)
        self.length = attr.get('length', 1)
        self.stride = attr.get('stride', 0)
        if self.length != 1 and self.stride == 0:
            raise ValueError('Stripe of length >1 cannot have stride 0')

    def add_child(self, child):
        return Range.add_child(self, child)

    def lookup_address(self, addr, variants=None):
        if self.length == 0:
            raise KeyError('Lookup in unknown-length stripes currently not supported')

        # For a stripe this is pretty complicated, at least to do efficiently
        # There are `length` copies of the stripe at offset `stride`
        # Will need to check all the contents at every offset 
        #   0*stride .. length*stride
        addr -= self.offset
        if addr < 0:
            raise KeyError('Address not found: 0x%x' % addr)
        # bounds -> bounds of this subelement replicated over total length of stripe
        # elem_bounds -> bounds of the first instance of this subelement
        for bounds,elem_bounds,range in izip(self.child_total_bounds, self.child_elem_bounds, self.contents):
            # Check only child ranges whose interval matches the address
            if interval_check(bounds, addr):
                # XXX this is not always necessary
                #     can determine from elem_bounds which elements this hits
                for i in xrange(0, self.length): 
                    sub_addr = addr - i*self.stride
                    pathcomp = [(self, i if self.length>1 else None)]
                    try:
                        return pathcomp + range.lookup_address(sub_addr)
                    except KeyError:
                        pass

        raise KeyError('Address not found: 0x%x' % addr)

    def compute_bounds(self):
        # for each child, compute bounds
        if self.length == 0:
            # Stripes of unknown length have unclear upper bound
            return (self.offset, INTERVAL_MAX)
        self.child_total_bounds = []
        self.child_elem_bounds = []
        for obj in self.contents:
            bounds = obj.compute_bounds()
            # now replicate bounds over stride times length
            # subtract one from length because the last stride won't span
            # the entire length
            #
            # |-----------| index 0
            #     |............|  index 1
            #          |.............|  index 2
            #
            # \______________________/
            stagger_bounds = interval_union(bounds, 
                        interval_add(bounds, self.stride * (self.length - 1)))
            self.child_total_bounds.append(stagger_bounds)
            self.child_elem_bounds.append(bounds)
            #if bounds != stagger_bounds:
            #    print('sub %x %x %x %x' % (bounds+ stagger_bounds))

        total_bounds = EMPTY_INTERVAL
        for bounds in self.child_total_bounds:
            total_bounds = interval_union(bounds, total_bounds)
        #print("total:", total_bounds)
        return interval_add(total_bounds, self.offset)

class Array(RNNObject, Range):
    '''Rules-ng array description'''
    offset = None  # Offset into parent
    length = None
    stride = None
    def __init__(self, parent, **attr):
        RNNObject.__init__(self, parent, **attr)
        Range.__init__(self, **attr)
        self.offset = attr['offset']
        self.length = attr['length']
        self.stride = attr['stride']
        if self.length < 1:
            raise ValueError('Length of array cannot be 0 or negative')
    
    def add_child(self, child):
        return Range.add_child(self, child)

    def lookup_address(self, addr, variants=None):
        addr -= self.offset
        if addr < 0:
            raise KeyError
        (r,d) = (addr % self.stride, addr // self.stride)
        if d >= self.length:
            raise KeyError

        sub_addr = addr - d*self.stride
        #print(i,self.stride,'%x'%sub_addr)
        pathcomp = [(self, d)]
        for range in self.contents:
            try:
                return pathcomp + range.lookup_address(sub_addr, variants)
            except KeyError:
                pass
        raise KeyError('Address not found: 0x%x' % addr)

    def compute_bounds(self):
        # Trivial
        return (self.offset, self.offset + self.stride * self.length)

class Register(RNNObject, Range, TypedValue):
    '''
    One rules-ng register description.
    '''
    offset = None  # Offset into parent
    length = None
    stride = None
    size = None # size, in bits
    def __init__(self, parent, **attr):
        RNNObject.__init__(self, parent, **attr)
        Range.__init__(self, **attr)
        TypedValue.__init__(self, **attr)

        self.size = attr['size']
        self.offset = attr['offset']
        self.length = attr.get('length', 1)
        self.stride = attr.get('stride', self.size // 8)
    
    def add_child(self, child):
        return TypedValue.add_child(self, child)

    def lookup_address(self, addr, variants=None):
        addr -= self.offset
        if addr < 0:
            raise KeyError
        (r,d) = (addr % self.stride, addr // self.stride)
        #print('r,d',r,d)
        if d < self.length and r < (self.size//8):
            # XXX also need byte offset in cell?
            if self.length != 1:
                num = d
            else:
                num = None
            return [(self,num)]
        else:
            raise KeyError

    def compute_bounds(self):
        # Trivial
        lower_bound = self.offset
        upper_bound = self.offset + self.stride * (self.length-1) + self.size//8
        return (lower_bound, upper_bound)

    def describe(self, value):
        return self.type.describe(value)

class Domain(RNNObject, Range, Type):
    '''Rules-ng domain description'''
    def __init__(self, parent, **attr):
        RNNObject.__init__(self, parent, **attr)
        Range.__init__(self, **attr)
        if self.name is None:
            raise ValueError('Domain must have a name')
    
    def lookup_address(self, addr, variants=None):
        '''
        Look up address within domain.
        This will return a path to the innermost register reference, or 
        raise KeyError if the address was not found.
        The path consists of a list of tuples (container, index)
        where container is the domain, stripe, array or register and index is the index
        within this object. The index will be None in case of single-element
        containers.
        '''
        if variants is None:
            variants = (None,None)
        for range in self.contents:
            if (range.variants,range.varset) != variants:
                continue
            try:
                return range.lookup_address(addr, variants)
            except KeyError:
                pass
        raise KeyError('Address not found: 0x%x' % addr)

    def add_child(self, child):
        if isinstance(child, Type): # Types can be defined here
            return True
        return Range.add_child(self, child)

    def compute_bounds(self):
        # Bounds of a domain are simply the union of all child intervals
        bounds = EMPTY_INTERVAL
        for obj in self.contents:
            bounds = interval_union(bounds, obj.compute_bounds())
        return bounds

    def describe(self, pos):
        '''DOMAIN can also be used as value'''
        try:
            path = self.lookup_address(pos)
            return format_path(path)
        except KeyError:
            return '*0x%x' % pos # prefix address with *

class Database(RNNObject):
    '''Object representing RNN database'''
    domains = None # Ordered dictionary of domains by name
    types = None # Ordered dictionary of types by name
    
    def __init__(self, parent, **attr):
        RNNObject.__init__(self, parent, **attr)
        self.domains = OrderedDict()
        self.types = OrderedDict()
    
    def lookup_domain(self, domain_name):
        return self.domains[domain_name]

    def lookup_type(self, type_name):
        return self.types[type_name]

    def add_child(self, child):
        if isinstance(child, Domain):
            self.domains[child.name] = child
            return True
        if isinstance(child, Type): # Types can be defined here
            return True
        if isinstance(child, CopyrightDummy): # Copyrights can be defined here
            return True
        return False

class CopyrightDummy(RNNObject):
    '''Dummy object representing RNN copyright info'''
    def add_child(self, child):
        if isinstance(child, CopyrightDummy): # More copyrights can be defined here
            return True
        return False

#-------------------------------------------------------------------------
# Parsing

class Tag:
    '''Rules-ng XML tags'''
    DATABASE = ns+'database'
    ENUM = ns+'enum'
    VALUE = ns+'value'
    BITSET = ns+'bitset'
    BITFIELD = ns+'bitfield'
    DOMAIN = ns+'domain'
    STRIPE = ns+'stripe'
    ARRAY = ns+'array'
    REG8 = ns+'reg8'
    REG16 = ns+'reg16'
    REG32 = ns+'reg32'
    REG64 = ns+'reg64'
    GROUP = ns+'group'
    USE_GROUP = ns+'use-group'
    BRIEF = ns+'brief'
    DOC = ns+'doc'
    IMPORT = ns+'import'
    COPYRIGHT = ns+'copyright'
    AUTHOR = ns+'author'
    LICENSE = ns+'license'
    
    REG_TO_SIZE = {REG8:8, REG16:16, REG32:32, REG64:64}

visit = {
    Tag.DATABASE: Database,
    Tag.ENUM: Enum,
    Tag.VALUE: EnumValue,
    Tag.BITSET: BitSet,
    Tag.BITFIELD: BitField,
    Tag.DOMAIN: Domain,
    Tag.STRIPE: Stripe,
    Tag.ARRAY: Array,
    Tag.REG8: Register,
    Tag.REG16: Register,
    Tag.REG32: Register,
    Tag.REG64: Register,
    Tag.COPYRIGHT: CopyrightDummy,
    Tag.AUTHOR: CopyrightDummy,
    Tag.LICENSE: CopyrightDummy,
# TODO
#    Tag.GROUP
#    Tag.USE_GROUP
}
def intdh(s):
    '''
    Parse a rules-ng integer.
    This does not recognize octal unlike int(s, 0)
    '''
    if s.startswith("0x"):
        return int(s[2:], 16)
    else:
        return int(s)

def visit_xml(syms, type_resolve_list, parent, root, imports):
    '''
    Visit an xml element, build an in-memory object for it,
    and add it to the in-memory parent object.
    '''
    # These take text and add this to the parents .brief or .doc
    if root.tag == Tag.BRIEF:
        parent.brief += root.text
        return None
    if root.tag == Tag.DOC:
        parent.doc += root.text
        return None
    if root.tag == Tag.IMPORT:
        imports.append(root.attrib['file'])
        return None

    # Pre-process attributes
    attr = {}
    for key,value in root.attrib.iteritems():
        if key in ['stride', 'offset', 'length', 'value', 'pos', 'low', 'high']:
            attr[key] = intdh(value)
        elif key in ['masked']:
            attr[key] = XML_BOOL[value]
        else:
            attr[key] = value
    if root.tag in Tag.REG_TO_SIZE:
        attr['size'] = Tag.REG_TO_SIZE[root.tag]

    # Instantiate object from tag
    if root.tag == Tag.DOMAIN and attr['name'] in syms:
        # allow re-opening a domain that was created before, to add more
        # state
        obj = syms[attr['name']]
        assert(isinstance(obj, Domain))
    else:
        obj = visit[root.tag](parent, **attr)

        # Add this object to parent object
        if parent is not None and obj is not None:
            if not parent.add_child(obj):
                raise ValueError('Cannot add child %s to %s' %
                        (obj.__class__.__name__, parent.__class__.__name__))

        # If a type, add object to symbol table
        if isinstance(obj, Type):
            if not obj.name in syms:
                syms[obj.name] = obj
            else:
                raise ValueError('Duplicate type name %s' % obj.name)

        # If it has a type attribute, add it to the type resolve list
        if isinstance(obj, TypedValue):
            type_resolve_list.append(obj)

    # Visit children
    for child in root.iterchildren(tag=ET.Element):
        visit_xml(syms, type_resolve_list, obj, child, imports)
    return obj

def parse_rng(f, import_path=''):
    '''
    Parse a rules-ng-ng XML tree from a file object.

    @returns a Database object
    '''
    # XXX Proper data structure for memory map would be an interval tree
    # After all, stripes can overlap, and arrays can be within arrays
    # Current solution is to loop over the entire domain, nesting into
    # stripes, arrays when necessary (when it is possible that it 
    # spans the provided value)
    # It is also possible to build a hash table, but this may take a lot of memory
    # and take long to build as stripes can potentially be huge, even unknown sized

    tree = ET.parse(f)
    root = tree.getroot()
    
    # build types symbol table, to be able to look up types by name
    # and patch them into the right place in the second pass
    type_table = OrderedDict()
    type_resolve_list = []
    imports = []

    retval = visit_xml(type_table, type_resolve_list, None, root, imports)
    if not isinstance(retval, Database):
        raise ValueError('Top-level element must be database')

    # Load imports
    already_imported = set()
    while imports:
        filename = imports.pop()
        if filename in already_imported:
            continue
        with open(path.join(import_path,filename), 'r') as f:
            tree = ET.parse(f)
            root = tree.getroot()
        # import, merging duplicate domains
        visit_xml(type_table, type_resolve_list, None, root, imports)

    # add types list to toplevel database
    retval.types = type_table 

    # Resolve types pass
    for obj in type_resolve_list:
        if obj.type is None:
            # The default is "bitset" if there are inline <bitfield> tags present
            # "enum" if there are inline <value> tags present
            #  (this will have created an internal anonymous type)
            # "boolean" if this value has width 1
            # otherwise "hex"
            if obj.anon_type is not None:
                obj.type = obj.anon_type
            elif obj.size == 1:
                obj.type = BaseType('boolean', 1)
            else:
                obj.type = BaseType('hex', obj.size)
        elif obj.type in BaseType.TYPES:
            obj.type = BaseType(obj.type, obj.size)
        else:
            try:
                obj.type = type_table[obj.type]
            except KeyError:
                raise ValueError('Unknown type %s' % obj.type)

    # Propagate bounds (from inside out) for stripes
    for domain in retval.domains.itervalues():
        bounds = domain.compute_bounds()
        #print(bounds)

    return retval

def parse_rng_file(filename):
    import_path = path.dirname(filename)
    with open(filename, 'r') as f:
        return parse_rng(f, import_path) 

def format_path(path):
    '''Format path into state space as string'''
    retval = []
    for obj,idx in path:
        if idx is not None:
            retval.append('%s[%i]' % (obj.name,idx))
        else:
            retval.append(obj.name)

    return '.'.join(retval)

#-------------------------------------------------------------------------
# Test
def main():
    import argparse
    parser = argparse.ArgumentParser(description='Parse rules-ng-ng test.')
    parser.add_argument('input', metavar='INFILE', type=str, 
            help='RNG file')
    args = parser.parse_args()

    r = parse_rng_file(args.input)
    d = r.lookup_domain('VIVS')

    path = d.lookup_address(0x100)
    print(format_path(path))
    path = d.lookup_address(0x604)
    print(format_path(path))
    path = d.lookup_address(0x68C)
    print(format_path(path))
    path = d.lookup_address(0x6A8)
    print(format_path(path))
    path = d.lookup_address(0x2444)
    print(format_path(path))
    path = d.lookup_address(0x10850)
    print(format_path(path))
    path = d.lookup_address(0x4100)
    print(format_path(path))

if __name__ == '__main__':
    main()

