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
"""Tests of the first row of a table in the printing path: a table whose
first row is a header row never has that row alone at the foot of a page.
A document of lines of text and a table of four rows is typeset many times,
with a different number of lines before the table each time, so that the
first row falls at the foot of the page in one of the runs.  The text of
each page is read with pdftotext.  Needs pdflatex and pdftotext; without
them the test is skipped."""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import mkkeepdata

bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
for tool in ('pdflatex', 'pdftotext'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.access(strprint, os.X_OK):
    print('SKIP: no strprint at %s' % strprint)
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='tablehead')
EOL = mkkeepdata.EOL


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
                                                   + str(extra)))
    fails += 0 if cond else 1


def pages_of(doc, name):
    """The text of each page of a document (bytes), or None."""
    src = os.path.join(tmp, name + '.doc')
    out = os.path.join(tmp, name + '.pdf')
    with open(src, 'wb') as f:
        f.write(doc)
    r = subprocess.run([strprint, src, '-o', out], capture_output=True,
                       text=True, timeout=300)
    if r.returncode != 0:
        return None
    t = subprocess.run(['pdftotext', '-layout', out, '-'],
                       capture_output=True, text=True).stdout
    pages = t.split('\f')
    if pages and pages[-1].strip() == '':
        pages.pop()
    return pages


def document(before):
    """BEFORE lines of text, then a table of a header row and three rows."""
    rows = [(b'HEADL' + EOL, b'HEADR' + EOL),
            (b'ROWAL' + EOL, b'ROWAR' + EOL),
            (b'ROWBL' + EOL, b'ROWBR' + EOL),
            (b'ROWCL' + EOL, b'ROWCR' + EOL)]
    return mkkeepdata.with_table([mkkeepdata.lines('L', before)], rows,
                                 [mkkeepdata.lines('M', 3)])


alone = []
split = []
for before in range(38, 50):
    pages = pages_of(document(before), 'doc%d' % before)
    if pages is None:
        check('strprint succeeds for %d lines before the table' % before,
              False)
        continue
    head = [i for i, p in enumerate(pages) if 'HEADL' in p]
    first = [i for i, p in enumerate(pages) if 'ROWAL' in p]
    if head != first:
        alone.append(before)
    if len(pages) > 1 and 'ROWBL' in pages[-1] and 'HEADL' in pages[0]:
        split.append(before)

check('the header row is never alone at the foot of a page (%d runs)' % 12,
      alone == [], 'header alone with %s lines before the table' % alone)
check('the runs include one whose table is split between two pages',
      len(split) > 0, 'no run split the table: the test does not reach '
      'the foot of the page')

print('tablehead: %d failures' % fails)
sys.exit(1 if fails else 0)
