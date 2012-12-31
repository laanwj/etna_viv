#!/usr/bin/python
'''
Print which bits are set in a value.
'''
from __future__ import print_function, division, unicode_literals
import argparse

def intdh(s):
    if s.startswith("0x"):
        return int(s[2:], 16)
    else:
        return int(s)

parser = argparse.ArgumentParser(description='Print which bits are set.')
parser.add_argument('value', metavar='VALUE')
parser.add_argument('-i', dest='invert', default=False, action='store_const', const=True)
parser.add_argument('-b', dest='bits', default=32, type=int)
args = parser.parse_args()

value = intdh(args.value)
if args.invert:
    value ^= (1<<args.bits)-1

bit = 0
all_set = []
while value > 0:
    if value & (1<<bit):
        all_set.append('%i' % bit)
    value &= ~(1<<bit)
    bit += 1

print(' '.join(all_set))

