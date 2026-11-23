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

"""The programs work without LaTeX, as Getting Started says.

The Getting Started document says that LaTeX is optional: without it the
Editor works, and only printing (to a PDF file or to a printer) and Preview
cannot be done.  This test runs the terminal Editor of a debug build with a
list of program folders (STRATA_PROGRAM_DIRS, which a release build never
reads) that holds the Printer and the Viewer but no LaTeX, and checks:

  - typing and saving a document work;
  - Print, Final reports that it needs LaTeX, and writes no PDF file;
  - Preview reports that latex did not run.

A control run puts the real LaTeX in the list of folders: the same Print,
Final then writes the PDF.  So the checks above fail when the Editor needs
LaTeX for typing or saving, and the control fails when the folders are not
what the test says they are.

    STRATA_BUILD_DIR=... python3 -I -B tests/nolatex.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
sample = os.path.join(root, 'manual', 'samples', 'chars.doc')

if not os.path.exists(stred) or not os.path.exists(sample):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
# A short path: the message of a failed Preview names a file below this
# directory, and the screen is only 80 columns wide.
tmp = tempfile.mkdtemp(prefix='nl', dir='/tmp')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
          + str(extra)))
    fails += 0 if cond else 1


def folder(*programs):
    """A folder with links to the named programs, for STRATA_PROGRAM_DIRS."""
    d = tempfile.mkdtemp(prefix='dirs', dir=tmp)
    for p in programs:
        os.symlink(os.path.realpath(p), os.path.join(d, os.path.basename(p)))
    return d


def run(dirs, steps, name):
    """Run the Editor on a copy of the sample with the keys in steps."""
    doc = os.path.join(tmp, name + '.doc')
    shutil.copy(sample, doc)
    h = tempfile.mkdtemp(prefix='h', dir=tmp)
    os.chmod(h, 0o700)
    env = {'STRATA_PROGRAM_DIRS': dirs, 'STRATA_HOME': h, 'TMUX': None,
           'LANG': 'en_US.UTF-8', 'LC_ALL': None, 'STRATA_ASCII': '0',
           'DISPLAY': ':99', 'TMPDIR': tmp,
           'STRATA_HELP': helpf}
    out = tui.session([stred, doc], ['waitfor:F1: Help'] + steps, env=env)
    return doc, out


termdir = os.path.dirname(stred)
printer = os.path.join(termdir, 'strprint')
viewer = os.path.join(termdir, 'dviview')
if not os.path.exists(printer):
    print('SKIP: strprint is not built')
    sys.exit(0)

none = folder(printer, viewer)
FINAL = ['F12', 'text:P', 'RIGHT', 'ENTER']

# ---- typing and saving need no LaTeX ---------------------------------------
doc, out = run(none, ['text:Written without LaTeX. ', 'C-s', 'wait', 'wait',
                      'show', 'C-q'], 'save')
with open(doc, 'rb') as f:
    data = f.read()
check('typing and saving work without LaTeX: the text is in the file',
      b'Written without LaTeX.' in data, out[0] if out else '')

# ---- Print, Final reports the missing LaTeX and writes nothing --------------
pdf = os.path.join(tmp, 'none.pdf')
doc, out = run(none, FINAL + ['text:' + pdf, 'INS', 'waitfor:Print needs LaTeX',
                              'show', 'ESC', 'C-q'], 'print')
check('Print, Final without LaTeX shows that Print needs LaTeX',
      'Print needs LaTeX' in out[0], out[0])
check('Print, Final without LaTeX writes no PDF file',
      not os.path.exists(pdf))

# ---- Preview reports it too, and nothing is left ----------------------------
doc, out = run(none, ['F12', 'wait', 'text:P', 'wait', 'text:P', 'wait',
                      'text:D', 'wait', 'INS',
                      'waitfor:Preview failed', 'show', 'ESC', 'C-q'],
              'preview')
check('Preview without LaTeX shows that latex did not run',
      'Preview failed' in out[0] and 'latex did not run' in out[0], out[0])

# ---- The control: with LaTeX in the folders the same print writes a PDF -----
latex = shutil.which('pdflatex')
if latex is None:
    print('note the control is skipped: pdflatex is not installed')
else:
    ctrl = folder(printer, viewer, latex)
    pdf2 = os.path.join(tmp, 'ctrl.pdf')
    doc, out = run(ctrl, FINAL + ['text:' + pdf2, 'INS', 'waitfor:Wrote',
                                  'show', 'C-q'], 'control')
    check('the control with LaTeX in the folders writes the PDF',
          os.path.exists(pdf2) and open(pdf2, 'rb').read(4) == b'%PDF',
          out[0] if out else '')

shutil.rmtree(tmp, ignore_errors=True)
print('nolatex: %s' % ('all passed' if fails == 0 else
                       '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
