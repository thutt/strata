#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# Strata is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along
# with Strata.  If not, see <https://www.gnu.org/licenses/>.
# Copyright (c) 2026 Logic Magicians Software

"""The end of a table that has lines, at the foot of a page.

A table with lines ends with a row of its own that holds the closing line.
When the last row of the table ended exactly at the foot of a page, that line
was set alone at the top of the next page: a page with a line and no text.
The document here has a number of lines of text and then a table of three
rows with a line under every row.  The number of lines is made 30 to 60, so
that the last row falls at every place of the page, and no page of any print
may be without text.

    STRPRINT=... python3 -I -B tests/tableedge.py
"""
import sys as _s; _s.dont_write_bytecode = True
import atexit, os, shutil, subprocess, sys, tempfile

for tool in ('pdflatex', 'latex', 'dvipdfmx', 'pdftotext'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
if not os.path.exists(strprint):
    print('SKIP: build the programs first')
    sys.exit(0)
fails = 0
CR = b'\r\n'
tmp = tempfile.mkdtemp(prefix='tedge')
atexit.register(shutil.rmtree, tmp, True)
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
os.environ['HOME'] = tempfile.mkdtemp(prefix='uhome', dir=tmp)


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def trow(t):
    return b'T' + t + b'\x1d\x00' + CR


def document(lines):
    """The text lines and then a table of three rows of three columns, each
    cell with a line at the left and the right and under it."""
    h = (b'*R3' + CR + b'MR1001' + CR + b'PP\\1\\\x00' + CR + b'FHA1' + CR +
         b'DP0,8500,11000' + CR)
    out = h + b'*B0' + CR + b'S' + CR + trow(b'First line')
    for k in range(lines - 1):
        out += b'B' + CR + trow(('Line %d of the text' % (k + 2)).encode())
    for r in range(3):
        out += b'B' + CR
        for c in range(3):
            out += ((b'CW20' + CR if c < 2 else b'') + b'CB84,1' + CR +
                    b'CL1' + CR + b'CR1' + CR +
                    trow(('Row %d column %d' % (r + 1, c + 1)).encode()))
    out += b'B' + CR + trow(b'The text after the table')
    return out + b'*E' + CR


def print_pages(lines):
    d = os.path.join(tmp, 'd%d' % lines)
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, 'doc.doc')
    open(path, 'wb').write(document(lines))
    pdf = os.path.join(d, 'doc.pdf')
    subprocess.run([strprint, path, '-o', pdf, '--no-banner',
                    '--no-error-log'], capture_output=True)
    if not os.path.exists(pdf):
        return None
    t = subprocess.run(['pdftotext', '-layout', pdf, '-'],
                       capture_output=True, text=True).stdout
    return t.split('\f')[:-1] if t.endswith('\f') else t.split('\f')


blank = []
tables_found = 0
for lines in range(30, 61):
    pages = print_pages(lines)
    if pages is None:
        blank.append((lines, 'no PDF'))
        continue
    tables_found += any('Row 3 column 3' in p for p in pages)
    for i, p in enumerate(pages):
        if len(p.split()) == 0:
            blank.append((lines, i + 1))

check('every print has the table: 31 documents with 30 to 60 lines of text',
      tables_found == 31, tables_found)
check('no page of any of the 31 prints is without text (the closing line '
      'of the table is not alone on a page)', blank == [], blank)

# The star form is in the LaTeX of the first row (it keeps the header with
# the first body row) and of the last row (it keeps the closing line).
path = os.path.join(tmp, 'x.doc')
open(path, 'wb').write(document(40))
tex = os.path.join(tmp, 'x.tex')
subprocess.run([strprint, path, '--export-latex', tex],
               capture_output=True)
t = open(tex).read() if os.path.exists(tex) else ''
check('the first and the last row of the table end with the star form of '
      'the row end', t.count(' \\\\*\n') == 2, t.count(' \\\\*\n'))

sys.exit(1 if fails else 0)
