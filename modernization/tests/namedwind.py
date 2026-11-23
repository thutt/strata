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

"""The Winding Column Layout panel of the Named Block Format, and the borders
in File, Export, Export a Screen Image.

The panel (F2 on Winding Columns of the Named Block Format) has Number of
Columns, Gutter Size and Vertical Rules; they are saved as BWN, BWG and BWR in
the named format, shown again when the format is retrieved, checked at the
field, and left alone by Esc.  An export of a screen image has the frame of a
border where the screen has it: the line above the first block, between two
blocks and below the last, and the sides of every row (the file is in CP 437:
the box characters are the bytes of the Editor's set).

    STRED=... STRATA_HELP=... python3 -I -B tests/namedwind.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
stred = os.environ.get('STRED', os.path.join(os.environ.get('STRATA_BUILD_DIR',
    ''), 'term', 'stred'))
sample = os.environ.get('DOCSRC', os.path.join(root, 'tests', 'samples'))
helpf = os.environ.get('STRATA_HELP', os.path.join(
    os.environ.get('STRATA_BUILD_DIR', ''), 'strata.hlp'))
if not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)
fails = 0
tmp = tempfile.mkdtemp(prefix='nwind')
CRL = b'\r\n'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def session(*args, **kw):
    os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
    return tui.session(*args, **kw)


def fresh(name, src='plan-title-table.doc'):
    path = os.path.join(tmp, name)
    shutil.copy(os.path.join(sample, src), path)
    return path


def fm(path, keys):
    return session([stred, '-H', helpf, path], keys)


def read(path):
    return open(path, 'rb').read() if os.path.exists(path) else b''


def trow(text):
    return b'T' + text + b'\x1d\x00' + CRL


def bdoc(name, body):
    path = os.path.join(tmp, name)
    open(path, 'wb').write(b'*R3' + CRL + b'MR1001' + CRL + b'PP\\1\\\x00' +
        CRL + b'FHA1' + CRL + b'DP0,8500,11000' + CRL + body + b'*E' + CRL)
    return path


GLOBAL = ['F12', 'text:G']
NAMED = GLOBAL + ['RIGHT'] * 5 + ['ENTER']
FILE = ['F12', 'ENTER']


def item_export():
    return FILE + ['RIGHT'] * 5 + ['ENTER', 'wait']


# ---- the Winding Column Layout panel of the Named Block Format -------------
NWIND = NAMED + ['ENTER', 'wait', 'ENTER', 'wait', 'text:cols'] + \
    ['DOWN'] * 12 + ['F2', 'wait']
d = fm(fresh('gn10.doc'), NWIND + ['show'])
check('named: F2 on Winding Columns opens the Winding Column Layout panel',
    all(w in d[0] for w in ('Winding Column Layout', 'Number of Columns',
    'Gutter Size', 'Vertical Rules')) and 'Bottom Alignment' not in d[0],
    d[0])
nm = fresh('gn11.doc')
fm(nm, NWIND + ['text:2', 'DOWN', 'text:0.5', 'DOWN', 'RIGHT', 'RIGHT', 'INS',
    'wait', 'INS', 'wait', 'C-s', 'wait'])
check('named: the layout is saved as BWN2, BWG500 and BWR1 in the format',
    b'\r\nFNcols\x00\r\nBWN2\r\nBWG500\r\nBWR1\r\nFNtab list' in read(nm),
    repr(read(nm)[:500]))
d = fm(nm, NAMED + ['ENTER', 'wait', 'DOWN', 'ENTER', 'wait'] +
    ['DOWN'] * 12 + ['F2', 'wait', 'show'])
check('named: a retrieved format shows its number, gutter and rule in the '
    'layout panel', 'Winding Column Layout' in d[0] and
    any('Number of Columns' in l and '2' in l for l in d[0].split('\n')) and
    any('Gutter Size' in l and '0.500' in l for l in d[0].split('\n')) and
    any('Vertical Rules' in l and 'Single' in l for l in d[0].split('\n')),
    d[0])
d = fm(fresh('gn12.doc'), NWIND + ['DOWN', 'text:x', 'INS', 'wait', 'show'])
check('named: a gutter that is not a number is refused',
    'Gutter Size must be a number' in d[0], d[0])
d = fm(fresh('gn13.doc'), NWIND + ['text:12', 'INS', 'wait', 'show'])
check('named: ten or more columns are refused',
    'Number of Columns must be from 1 to 9' in d[0], d[0])
nm = fresh('gn14.doc')
fm(nm, NWIND + ['text:4', 'ESC', 'wait', 'INS', 'wait', 'C-s', 'wait'])
check('named: Esc on the layout panel keeps the settings (no BWN4)',
    b'BWN4' not in read(nm), repr(read(nm)[:500]))
nm = fresh('gn15.doc')
fm(nm, NWIND + ['text:3', 'INS', 'wait', 'INS', 'wait', 'C-s', 'wait'])
check('named: only the number: no gutter and no rule record is written',
    b'\r\nBWN3\r\n' in read(nm) and b'BWG' not in read(nm) and
    b'BWR' not in read(nm), repr(read(nm)[:500]))

# ---- Export a Screen Image draws the borders as the screen does ------------
CB1 = b'CB85,1' + CRL + b'CL1' + CRL + b'CR1' + CRL
CB2 = b'CB170,1' + CRL + b'CL1' + CRL + b'CR1' + CRL
two = (b'*B0' + CRL + b'S' + CRL + b'CW20' + CRL + trow(b'Left cell') +
       trow(b'Right cell has words') + b'B' + CRL + trow(b'Second block'))
bq1 = (b'*B0' + CRL + b'S' + CRL + CB1 + trow(b'Left cell') + b'B' + CRL +
       trow(b'Second block'))
bq2 = (b'*B0' + CRL + b'S' + CRL + CB2 + trow(b'Left cell') + b'B' + CRL +
       trow(b'Second block'))
bq3 = (b'*B0' + CRL + b'S' + CRL + b'CW20' + CRL + CB1 + trow(b'Left cell') +
       CB1 + trow(b'Right cell') + b'B' + CRL + trow(b'Second block'))


def screen_rows(doc):
    d = fm(doc, ['show'])
    return [l.replace('◄', ' ').rstrip() for l in d[0].split('\n')]


def exported_rows(doc, name):
    out = os.path.join(tmp, name)
    fm(doc, item_export() + ['DOWN', 'text:' + out, 'INS', 'wait'])
    return [l.decode('cp437').rstrip() for l in read(out).split(b'\r\n')], \
        read(out)


def same_as_screen(doc, name, n):
    ex, raw = exported_rows(doc, name)
    sc = screen_rows(doc)
    k = next((i for i, l in enumerate(sc) if ex[0] != '' and l == ex[0]), -1)
    return k >= 0 and sc[k:k + n] == ex[:n], ex, sc


ok, ex, sc = same_as_screen(bdoc('xb1.doc', bq1), 'xb1.txt', 4)
raw = read(os.path.join(tmp, 'xb1.txt'))
check('export: a block with a single border: the frame is in the file, '
    'top, sides and bottom, as the screen has it', raw.startswith(
    b'\xda' + b'\xc4' * 78 + b'\xbf\r\n\xb3Left cell') and
    b'\xc0' + b'\xc4' * 78 + b'\xd9\r\n' in raw and ok, (ex[:5], sc[:8]))
ok, ex, sc = same_as_screen(bdoc('xb2.doc', bq2), 'xb2.txt', 4)
raw = read(os.path.join(tmp, 'xb2.txt'))
check('export: a double border has the double corners and bars of the screen',
    raw.startswith(b'\xc9' + b'\xcd' * 78 + b'\xbb\r\n\xbaLeft cell') and
    b'\xc8' + b'\xcd' * 78 + b'\xbc\r\n' in raw and ok, (ex[:5], sc[:8]))
ex, raw = exported_rows(bdoc('xb3.doc', bq3), 'xb3.txt')
r3 = raw.split(b'\r\n')
check('export: a table of two columns has a frame round each column, with '
    'the columns where the screen has them', r3[0].count(b'\xda') == 2 and
    r3[0].count(b'\xbf') == 2 and r3[1].count(b'\xb3') == 4 and
    r3[2].count(b'\xc0') == 2 and r3[0].index(b'\xda', 1) == 23 and
    r3[3].startswith(b'Second block'), ex[:5])
ex, raw = exported_rows(bdoc('xb4.doc', two), 'xb4.txt')
check('export: a document with no border has no box character and no extra '
    'row (the first line is the text)', not any(c in raw for c in range(0xb0,
    0xe0)) and ex[0].startswith('Left cell'), ex[:4])
ex, raw = exported_rows(bdoc('xb5.doc', bq1), 'xb5.txt')
check('export: the second block follows the frame directly', ex[3].startswith(
    'Second block') and ex[:3] != ['', '', ''], ex[:5])

shutil.rmtree(tmp, ignore_errors=True)
print('namedwind: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
