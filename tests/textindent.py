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

"""Ctrl-G inserts the text indent character.

The character is the byte 1A in the text of the saved file; the lines that
continue the paragraph begin below the place after it, on the screen; and
the key gives no message of its own any more.

usage: STRED=path/to/stred python3 -I -B tests/textindent.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tui  # noqa: E402

BUILD = os.environ.get('STRATA_BUILD_DIR', '')
STRED = os.environ.get('STRED', os.path.join(BUILD, 'term', 'stred'))
HELP = os.environ.get('STRATA_HELP', os.path.join(BUILD, 'strata.hlp'))
FAILS = []


def check(name, ok, detail=None):
    print(('ok   ' if ok else 'FAIL ') + name)

    if not ok:
        FAILS.append(name)

        if detail is not None:
            print('     ' + str(detail))


def main():
    if not os.path.exists(STRED):
        print('SKIP: build the programs first')

        return 0

    tmp = tempfile.mkdtemp(prefix='ti')
    path = os.path.join(tmp, 't.doc')

    with open(path, 'wb') as f:
        f.write(b'*R3\r\nMR1000\r\nPP\\1\\\x00\r\nDP0,8500,11000\r\n*B0\r\n'
                b'S\r\nT\x1d\x00\r\n*E\r\n')

    words = ' '.join(['word%d' % i for i in range(1, 25)])
    steps = (['text:1.', 'C-g', 'text:' + words, 'wait', 'show', 'C-s',
              'wait'])
    out = tui.session([STRED, '-H', HELP, path], steps, rows=24)
    scr = out[0].split('\n')
    lines = [ln for ln in scr if 'word' in ln]

    with open(path, 'rb') as f:
        data = f.read()

    check('Ctrl-G puts the byte 1A in the text of the saved file',
          b'T1.\x1a' in data, data[-60:])
    check('Ctrl-G shows no message of its own',
          'not implemented' not in out[0], out[0])
    check('the text has more than one line on the screen', len(lines) >= 2,
          lines)
    # The first line begins "1." and the indent character; the lines that
    # continue it begin after them: three cells in.
    check('the first line begins with the number and the indent character',
          len(lines) >= 1 and lines[0].startswith('1.'), lines)
    check('the lines that continue the paragraph begin below the text after '
          'the indent character',
          len(lines) >= 2 and all(ln.startswith('   word') for ln in
                                  lines[1:]), lines)

    print('textindent: %d failures' % len(FAILS))

    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
