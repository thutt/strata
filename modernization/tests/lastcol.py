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

"""The width of the last column of a block.  A block whose last column has no
width takes the room that is left on the line.  LaTeX puts the space of a
column (tabcolsep, 6 pt) on both sides of every column, so the column has to
leave 12 pt for each column of the table.  tests/data/print_lastcol.doc has a
paragraph, which shows the right margin, and one table of 2, 3 and 4 columns
on a page of its own.  The text of the last column must end at the right
margin less the space of the column, whatever the number of columns, and the
three panels of the table of three columns must be equal."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
if not os.path.exists(strprint) or not shutil.which('pdftotext'):
    print('SKIP: build strprint and install pdftotext first')
    sys.exit(0)

fails = 0
TABCOLSEP = 6.0     # Points, as the file sets it.
TOLERANCE = 0.6     # Points: the rounding of the widths.
PANEL_TOLERANCE = 3.6   # Points: 0.05 inch.


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def pages_of(pdf):
    """The words of every page: (xmin, ymin, xmax, ymax, text) lists, and the
    width of the page."""
    out = subprocess.run(['pdftotext', '-bbox', pdf, '-'],
        capture_output=True, text=True).stdout
    result = []
    width = 0.0
    for pg in out.split('<page ')[1:]:
        width = float(re.match(r'width="([\d.]+)"', pg).group(1))
        result.append([(float(a), float(b), float(c), float(d), e)
            for a, b, c, d, e in re.findall(
            r'xMin="([\d.]+)" yMin="([\d.]+)" xMax="([\d.]+)" '
            r'yMax="([\d.]+)">([^<]*)<', pg)])
    return result, width


tmp = tempfile.mkdtemp(prefix='lastcol')
pdf = os.path.join(tmp, 'out.pdf')
p = subprocess.run([strprint, os.path.join(here, 'data', 'print_lastcol.doc'),
    '-o', pdf], capture_output=True, text=True)
check('the document is printed', p.returncode == 0 and os.path.exists(pdf),
      p.stderr)
pages, pagew = pages_of(pdf)
check('there are four pages: the paragraph and the three tables',
      len(pages) == 4, len(pages))

margin = pagew - 72.0
words = pages[0]
right = max(w[2] for w in words)
check('the paragraph ends at the right margin (the default of 1 inch)',
      margin - 10.0 < right <= margin + TOLERANCE, (right, margin))

limit = margin - TABCOLSEP
for n in (2, 3, 4):
    words = pages[n - 1]
    marks = {}
    for w in words:
        m = re.match(r'T%d([A-D])$' % n, w[4])
        if m:
            marks[m.group(1)] = w[0]
    check('table of %d columns: the marks of its columns are found' % n,
          sorted(marks) == [chr(65 + i) for i in range(n)], marks)
    if sorted(marks) != [chr(65 + i) for i in range(n)]:
        continue
    lastx = marks[chr(65 + n - 1)]
    cells = [w for w in words if w[0] >= lastx - 0.5]
    far = max(w[2] for w in cells)
    check('table of %d columns: the last column ends at the right margin '
          'less the space of the column (%.1f pt)' % (n, limit),
          limit - 10.0 < far <= limit + TOLERANCE, (far, limit))
    if n == 3:
        a, b, c = marks['A'], marks['B'], marks['C']
        check('table of 3 columns: the three panels are equal (the first '
              'two have the width 20, the last takes the rest)',
              abs((b - a) - (c - b)) <= PANEL_TOLERANCE,
              (a, b, c, b - a, c - b))
        # The text of a panel is the pitch of the panels less the space on
        # both sides; the last panel takes the room that is left.
        text = (b - a) - 2 * TABCOLSEP
        check('table of 3 columns: the text of the last panel is as wide as '
              'that of the first two (%.1f pt)' % text,
              text - 10.0 < far - c <= text + PANEL_TOLERANCE,
              (far - c, text))

print('lastcol: %d failures' % fails)
sys.exit(1 if fails else 0)
