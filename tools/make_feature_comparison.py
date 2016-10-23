#!/usr/bin/python
'''
Create overview comparison table beween different GCxxx chips on different platforms.

The input data is specified in JSON format, the output is in HTML.
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
import argparse
import json, cgi
from etnaviv.util import rnndb_path
from etnaviv.parse_rng import parse_rng_file, format_path, BitSet, Domain
import sys

GPUS_FILE = 'data/gpus.json'
STATE_MAP = rnndb_path('state.xml')

class Cell(object):
    def __init__(self, row, column, value, rowspan=1, colspan=1, cls=None):
        self.row = row
        self.column = column
        self.rowspan = rowspan
        self.colspan = colspan
        self.value = value
        self.cls = cls

def main():
    parser = argparse.ArgumentParser(description='Compare GCxxx chips.')
    parser.add_argument('-i', dest='gpus_file', metavar='GPUSFILE', type=str, 
            help='gpus.json file', default=GPUS_FILE)
    parser.add_argument('-s', dest='state_xml_file', metavar='STATEXML', type=str, 
            help='State map definition file (rules-ng-ng)', default=STATE_MAP)
    args = parser.parse_args()

    with open(args.gpus_file, 'r') as f:
        gpus = json.load(f)
    state_xml = parse_rng_file(args.state_xml_file)
    state_map = state_xml.lookup_domain('VIVS')

    feature_fields = ["chipFeatures", "chipMinorFeatures", "chipMinorFeatures1", "chipMinorFeatures2", "chipMinorFeatures3", "chipMinorFeatures4", "chipMinorFeatures5"]
    all_features = []
    for field in feature_fields:
        if field == 'chipMinorFeatures':
            set_desc = state_xml.types['chipMinorFeatures0']
        else:
            set_desc = state_xml.types[field]
        all_features.extend([(field,bit) for bit in set_desc.bitfields])

    table = []
    
    cur_row = 0
    table.append(Cell(cur_row, 0, 'Platform', cls='header firstrow'))
    cur_col = 1
    for platform in gpus:
        table.append(Cell(cur_row, cur_col, platform['platform'], colspan=len(platform['chips']), cls='firstrow'))
        cur_col += len(platform['chips'])
    full_width = cur_col

    cur_row += 1
    table.append(Cell(cur_row, 0, 'Type', cls='header'))
    cur_col = 1
    for platform in gpus:
        for idx,chip in enumerate(platform['chips']):
            table.append(Cell(cur_row, cur_col, chip['type']))
            cur_col += 1
    
    cur_row += 1
    table.append(Cell(cur_row, 0, 'Revision', cls='header'))
    cur_col = 1
    for platform in gpus:
        for idx,chip in enumerate(platform['chips']):
            if 'chipRevision' in chip:
                revision_str = '0x%04x' % int(chip['chipRevision'],0)
            else:
                revision_str = '?'
            table.append(Cell(cur_row, cur_col, revision_str))
            cur_col += 1

    cur_row += 1
    table.append(Cell(cur_row, 0, 'Specs', colspan=full_width, cls='category'))
    spec_fields = ["streamCount", "registerMax", "threadCount", "shaderCoreCount",  "vertexCacheSize", "vertexOutputBufferSize",
              "pixelPipes", "instructionCount", "numConstants", "bufferSize", "numVaryings", "superTileLayout"]
    for (field) in spec_fields:
        cur_row += 1
        table.append(Cell(cur_row, 0, field, cls='subheader'))
        cur_col = 1
        for platform in gpus:
            for chip in platform['chips']:
                if field in chip:
                    value = int(chip[field], 0)
                else:
                    value = '?'
                table.append(Cell(cur_row, cur_col, value))
                cur_col += 1

    cur_row += 1
    table.append(Cell(cur_row, 0, 'Features', colspan=full_width, cls='category'))
    for (field, bit) in all_features:
        cur_row += 1
        table.append(Cell(cur_row, 0, bit.name, cls='subheader featurename'))
        cur_col = 1
        for platform in gpus:
            for chip in platform['chips']:
                value = int(chip.get(field,'0'), 0)
                active_feat = bit.extract(value)
                if active_feat:
                    active_feat = '+'
                    cls = 'plus'
                else:
                    active_feat = '-'
                    cls = 'minus'
                table.append(Cell(cur_row, cur_col, active_feat, cls=cls))
                cur_col += 1
    
    layout = {}
    rows = 0
    columns = 0
    for cell in table:
        layout[cell.row,cell.column] = cell
        rows = max(cell.row+1, rows)
        columns = max(cell.column+1, columns)

    out = sys.stdout 
    out.write('<html>\n')
    out.write('<head><!-- Auto-generated by make_feature_comparison.py from gpus.json -->\n')
    out.write('<title>Vivante GPU feature bits comparison</title>\n')
    out.write("""<style>
body { background-color: white; }
table.gpu-comparison { table-layout: fixed; word-wrap:break-all; }
table.gpu-comparison td { width: 80px; text-align: center; fixed; word-wrap:break-word; word-break:break-all; }
table.gpu-comparison tr:nth-child(odd) td {
    background-color: #e0e0ff;
}
table.gpu-comparison tr:nth-child(even) td {
    background-color: #d0d0ff;
}
table.gpu-comparison tr td.firstrow { text-align: left; background-color: #F0F0F0; }
table.gpu-comparison tr td.header { text-align: left; width: 15em; }
table.gpu-comparison tr td.subheader { text-align: left; }
table.gpu-comparison tr td.category { text-align: left; font-style: italic; background-color: #F0F0F0; }
table.gpu-comparison td.minus { color: #808080; }
table.gpu-comparison td.plus { }
table.gpu-comparison .featurename { font-family:monospace; font-size: 10px; }
</style>
""")
    out.write('</head>\n')
    out.write('<body>\n')
    out.write('<table class="gpu-comparison">\n')
    for row in xrange(rows):
        out.write('<tr>')
        for column in xrange(columns):
            try:
                cell = layout[row, column]
            except KeyError:
                pass #out.write('<td></td>')
            else:
                args = ''
                if cell.colspan != 1:
                    args += ' colspan="%i"' % cell.colspan
                if cell.cls is not None:
                    args += ' class="%s"' % cell.cls
                out.write('<td%s>%s</td>' %(args,cgi.escape(str(cell.value))))
        out.write('</tr>\n')
    out.write('</table>\n')
    out.write('</body>\n')
    out.write('</html>\n')

if __name__ == '__main__':
    main()
