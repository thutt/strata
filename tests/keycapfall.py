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

"""The parts of docs/KEYCAP-FALLBACK.txt that need no window.  The terminal
Editor draws no pictures, so a keycap, a picture and an equation are drawn as
before, in every display mode (the legend in a box in the mode Inline, the
labels of the other modes).  The cause of the empty keycaps (a list of 16
pictures of a screen) is not in the sources any more, and the document of the
fix is there.  The pictures themselves are tested by tests/xfallback.py.

    STRATA_BUILD_DIR=... python3 -I -B tests/keycapfall.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
bdir = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(bdir, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(bdir, 'strata.hlp'))
if not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)
tmp = tempfile.mkdtemp(prefix='kcf')
os.environ['STRATA_HOME'] = os.path.join(tmp, 'home')
fails = 0
CRLF = b'\r\n'
EOL = b'\x1d'
HEADER = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
          b'P1C33' + CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF +
          b'DP0,8500,11000' + CRLF)


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def doc(path, mode=None):
    extra = b''
    if mode is not None:
        extra = b''.join(b'OM%d,%d' % (t, mode) + CRLF for t in (9, 25, 37))
    body = (b'k \x1337(1Ctrl-s\x00' + CRLF + b'\x13) e \x139(S' + CRLF +
            b'Tx^2\x00' + CRLF + b'\x13) end' + EOL)
    open(path, 'wb').write(HEADER + extra + b'*B0' + CRLF + b'S' + CRLF +
                           b'T' + body + b'\x00' + CRLF + b'*E' + CRLF)


def read(path):
    return open(path, encoding='utf-8', errors='replace').read()


try:
    for mode, name, want in ((None, 'Inline', ('Ctrl-s', 'x^2')),
                             (1, 'Name', ('[KEY]', '[EQN]')),
                             (2, 'Short', ('[Key: Ctrl-s]', '[EQN x^2]')),
                             (3, 'Long', ('[Key: Ctrl-s]', '[EQUATION x^2]'))):
        path = os.path.join(tmp, 'k%s.doc' % name)
        doc(path, mode)
        d = tui.session([stred, '-H', helpf, path], ['wait', 'show'])
        line = [r for r in d[0].split('\n') if r.startswith('k ')]
        check('the terminal Editor, mode %s: the keycap and the equation '
              'are drawn as before (%s)' % (name, ' and '.join(want)),
              len(line) == 1 and all(w in line[0] for w in want), d[0])
    text = read(os.path.join(root, 'docs', 'KEYCAP-FALLBACK.txt'))
    check('the document of the fix names the cause and the fix',
          'tb_img' in text and 'marker_failed_fn' in text and
          'more than 5 seconds' in text and 'one warning' in text)
    termbuf = read(os.path.join(root, 'src', 'termbuf.h'))
    view = read(os.path.join(root, 'src', 'view.c'))
    x11 = read(os.path.join(root, 'src', 'term_x11.c'))
    check('no list of pictures of a screen has a fixed size of 16 or any '
          'other (tb_img, last_img, eq_done)',
          re.search(r'tb_img\[', termbuf) is None and
          re.search(r'TB_IMAGES', termbuf + x11) is None and
          re.search(r'eq_done\[\d+\]', view) is None)
    ui = read(os.path.join(root, 'src', 'ui.c'))
    check('the Editor reports the markers which picture failed (the hook is '
          'set when the X11 Editor starts)',
          'marker_failed_fn = image_failed' in ui)
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print('keycapfall: %d failures' % fails)
sys.exit(1 if fails else 0)
