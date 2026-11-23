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

"""The display mode of the LaTeX marker (docs/LATEXMARKER-PICTURES.txt), through
the real terminal Editor.  The LaTeX marker has the display mode Inline like
the Equation, the Picture and the Keycap: it is the default, so the Marker
Display Modes panel shows Inline for LaTeX and nothing is saved for it; any
other mode is saved as the record OM41,N in the header; the old setting "Draw
LaTeX Markers As Pictures?" and its record OLP are gone: a document that holds
OLP is read, the record is ignored and it is not written again.  The terminal
Editor never draws pictures, so a LaTeX marker is drawn by the Short form in
the mode Inline.

    STRATA_BUILD_DIR=... python3 -I -B tests/lmsetting.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
bdir = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(bdir, 'term', 'stred'))
rtrip = os.environ.get('RT', os.path.join(os.path.dirname(stred),
    'roundtrip'))
helpf = os.environ.get('STRATA_HELP', os.path.join(bdir, 'strata.hlp'))
sample = os.environ.get('DOCSRC', os.path.join(here, 'samples'))
if not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)
tmp = tempfile.mkdtemp(prefix='lms')
os.environ['STRATA_HOME'] = os.path.join(tmp, 'home')
fails = 0
PANEL = ['F12', 'text:G'] + ['RIGHT'] * 3 + ['ENTER', 'wait']
MODES = PANEL + ['PGUP', 'PGUP', 'wait']
LATEX = ['DOWN'] * 8


CRLF = b'\r\n'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def fresh(name):
    path = os.path.join(tmp, name)
    shutil.copy(os.path.join(sample, 'plan-title-table.doc'), path)
    return path


def read(path):
    return open(path, 'rb').read()


def latex_doc(path, extra=b''):
    """A document with a LaTeX marker in a block of its own."""
    with open(path, 'wb') as f:
        f.write(b'*R3\r\nMR1001\r\nPP\\1\\\x00\r\nP1C33\r\nFHA1\r\nFTT5\r\n'
                b'DP0,8500,11000\r\n' + extra + b'*B0\r\nS\r\nTa \x1341(S\r\n'
                b'T\\TeX{}\x00\r\n\x13) b\x1d\x00\r\n*E\r\n')


try:
    doc = fresh('a.doc')
    d = tui.session([stred, '-H', helpf, doc], PANEL + ['show'])
    check('the Marker Format panel has no field Draw LaTeX Markers As '
          'Pictures?', 'Marker Format' in d[0] and 'Draw LaTeX' not in d[0],
          d[0])
    d = tui.session([stred, '-H', helpf, doc], MODES + LATEX + ['show'])
    line = [r for r in d[0].split('\n') if 'LaTeX:' in r]
    check('the Marker Display Modes panel shows the mode Inline for LaTeX',
          len(line) == 1 and 'Inline' in line[0], d[0])
    tui.session([stred, '-H', helpf, doc], MODES + LATEX + ['LEFT', 'LEFT',
                'LEFT', 'INS', 'wait', 'C-s', 'wait'])
    saved = read(doc)
    check('the mode Short is saved as the record OM41,2 in the header, and '
          'no record OLP', b'\r\nOM41,2\r\n' in saved and b'OLP' not in saved
          and saved.index(b'OM41,2') < saved.index(b'*B'), saved[:300])
    d = tui.session([stred, '-H', helpf, doc], MODES + LATEX + ['show'])
    line = [r for r in d[0].split('\n') if 'LaTeX:' in r]
    check('a document with the record shows Short in the panel',
          len(line) == 1 and 'Short' in line[0], d[0])
    rt = subprocess.run([rtrip, doc], capture_output=True, text=True)
    check('a file with the record is read and written again byte for byte',
          rt.returncode == 0, rt.stdout)
    tui.session([stred, '-H', helpf, doc], MODES + LATEX + ['RIGHT', 'RIGHT',
                'RIGHT', 'INS', 'wait', 'C-s', 'wait'])
    check('Inline takes the record away again (it is the default)',
          b'OM41' not in read(doc) and b'OLP' not in read(doc),
          read(doc)[:300])
    doc2 = fresh('b.doc')
    tui.session([stred, '-H', helpf, doc2], PANEL + ['INS', 'wait', 'C-s',
                'wait'])
    check('accepting the panel with no change writes no record for LaTeX',
          b'OM41' not in read(doc2) and b'OLP' not in read(doc2),
          read(doc2)[:300])

    # The old record: read, ignored, not written again.
    old = os.path.join(tmp, 'old.doc')
    latex_doc(old, b'OLP0' + CRLF)
    d = tui.session([stred, '-H', helpf, old], MODES + LATEX + ['show'])
    line = [r for r in d[0].split('\n') if 'LaTeX:' in r]
    check('a document with the old record OLP0 is read, and the record is '
          'ignored: the mode of LaTeX is Inline', len(line) == 1 and
          'Inline' in line[0], d[0])
    tui.session([stred, '-H', helpf, old], ['text:x', 'C-s', 'wait'])
    check('the old record OLP is not written again', b'OLP' not in read(old)
          and b'*R3' in read(old), read(old)[:200])
    # The control: the same document without the old record has the same mode.
    new = os.path.join(tmp, 'new.doc')
    latex_doc(new)
    d = tui.session([stred, '-H', helpf, new], MODES + LATEX + ['show'])
    line = [r for r in d[0].split('\n') if 'LaTeX:' in r]
    check('the same document without OLP shows Inline too', len(line) == 1
          and 'Inline' in line[0], d[0])

    # The terminal Editor draws no pictures: Inline is the Short form.
    for extra, name in ((b'', 'Inline'), (b'OM41,2' + CRLF, 'Short'),
                        (b'OM41,3' + CRLF, 'Long'),
                        (b'OM41,1' + CRLF, 'Name')):
        p = os.path.join(tmp, 'm%s.doc' % name)
        latex_doc(p, extra)
        d = tui.session([stred, '-H', helpf, p], ['wait', 'show'])
        want = '[LaTeX]' if name == 'Name' else '[LaTeX instructions]'
        check('the terminal Editor, mode %s: the LaTeX marker is %s' %
              (name, want), want in d[0], d[0])
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print('lmsetting: %d failures' % fails)
sys.exit(1 if fails else 0)
