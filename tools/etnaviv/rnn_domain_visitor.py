from etnaviv.parse_rng import parse_rng_file, format_path, BitSet, Domain, Stripe, Register, Array, BaseType

class DomainVisitor(object):
    '''
    Walk a rnndb domain, visit all registers recursively.
    '''
    def __init__(self):
        pass

    def visit(self, node):
        if isinstance(node, Domain):
            self.visit_domain(node)
        elif isinstance(node, Stripe):
            self.visit_stripe(node)
        elif isinstance(node, Array):
            self.visit_array(node)
        elif isinstance(node, Register):
            self.visit_register(node)
        else:
            raise ValueError('DomainVisitor: unknown node type %s' % node.__class__.__name__)

    def visit_domain(self, node):
        for child in node.contents:
            self.visit(child)
    
    def visit_stripe(self, node):
        for child in node.contents:
            self.visit(child)
    
    def visit_register(self, node):
        for child in node.contents:
            self.visit(child)
    
    def visit_array(self, node):
        for child in node.contents:
            self.visit(child)

