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

"""The default display mode of six markers is Short, not Diamond.

Comment, Figure, Font, Index, Note and Table were drawn as a diamond until
a document said otherwise.  The screen now shows their short forms by
default; a Diamond that is chosen is saved and drawn; and a print is not
changed by the change of the default.  Run on its own:
    python3 -I -B tests/markerdefault.py
"""
import sys as _s; _s.dont_write_bytecode = True
import atexit, os, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
                                                     'strprint'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
if not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
# The scratch folder is made where TMPDIR says, and is removed at the end.
tmp = tempfile.mkdtemp(prefix='markerdefault')
atexit.register(shutil.rmtree, tmp, True)
os.environ['STRATA_HOME'] = os.path.join(tmp, 'home')
os.makedirs(os.environ['STRATA_HOME'], mode=0o700)
raw = open(os.path.join(here, 'data', 'markers.doc'), 'rb').read()


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def screen(path):
    """The first screen of the Editor on a document, 60 rows tall."""
    return tui.session([stred, '-H', helpf, path], ['show'], rows=60,
                       cols=100)[0]


plain = os.path.join(tmp, 'plain.doc')
open(plain, 'wb').write(raw)
d = screen(plain)
check('Comment, Figure, Font, Index, Note and Table are drawn in the short '
      'form by default, not as diamonds',
      all(t in d for t in ('[COM "', '[FIG "FigName"]', '[FONT]',
                           '[INDEX IxMain]', '[NOTE]', '[TABLE "')) and
      '\u2666' not in d, d)

six = b''.join(b'OM%d,0\r\n' % t for t in (1, 3, 6, 11, 15, 29))
dia = os.path.join(tmp, 'dia.doc')
open(dia, 'wb').write(raw.replace(b'*B0', six + b'*B0', 1))
d = screen(dia)
check('a Diamond mode that is saved for the six markers draws them as '
      'diamonds',
      d.count('\u2666') >= 6 and '[NOTE]' not in d and '[FONT]' not in d and
      '[INDEX IxMain]' not in d and '[TABLE "' not in d, d)

# The print: the same bytes with no mode saved as with the old default
# (Diamond) saved for the six.  The LaTeX names its own file in a comment, so
# both are written as same.tex, in two folders.
outs = []
for name, src in (('a', plain), ('b', dia)):
    os.makedirs(os.path.join(tmp, name))
    out = os.path.join(tmp, name, 'same.tex')
    subprocess.run([strprint, src, '--export-latex', out, '--source-name',
                    'same.doc', '--source-path', '/same'],
                   capture_output=True)
    outs.append(open(out, 'rb').read() if os.path.exists(out) else b'')
check('the print of a document with no saved modes is the same bytes as the '
      'print with the old default (Diamond) saved for the six',
      len(outs[0]) > 1000 and outs[0] == outs[1],
      (len(outs[0]), len(outs[1])))

# The width of a marker sets where a right tab puts the text that follows:
# a Note in a tabbed line must not move it when the default changes.
feat = open(os.path.join(here, 'data', 'print_features.doc'), 'rb').read()
at = b'TItem\x10 '
assert at in feat
tabbed = feat.replace(at, b'TItem\x131(\x13)\x10 ', 1)
outs = []
for name, data in (('c', tabbed),
                   ('d', tabbed.replace(b'*B0', six + b'*B0', 1))):
    os.makedirs(os.path.join(tmp, name))
    src = os.path.join(tmp, name + '.doc')
    open(src, 'wb').write(data)
    out = os.path.join(tmp, name, 'same.tex')
    subprocess.run([strprint, src, '--export-latex', out, '--source-name',
                    'same.doc', '--source-path', '/same'],
                   capture_output=True)
    outs.append(open(out, 'rb').read() if os.path.exists(out) else b'')
check('a Note before a right tab prints the same with the default and with '
      'the old default (Diamond) saved',
      len(outs[0]) > 1000 and outs[0] == outs[1],
      (len(outs[0]), len(outs[1])))

sys.exit(1 if fails else 0)
