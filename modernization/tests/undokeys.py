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

"""Ctrl-F10 is Undo and Ctrl-F11 is Redo, as their labels say.

usage: STRED=path/to/stred python3 -I -B tests/undokeys.py
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

    tmp = tempfile.mkdtemp(prefix='uk')
    path = os.path.join(tmp, 'a.doc')

    with open(path, 'wb') as f:
        f.write(b'*R3\r\nMR1000\r\nPP\\1\\\x00\r\nDP0,8500,11000\r\n*B0\r\n'
                b'S\r\nTtext\x1d\x00\r\n*E\r\n')

    out = tui.session([STRED, '-H', HELP, path],
                      ['text:qqq', 'wait', 'show', 'C-F10', 'wait', 'show',
                       'C-F11', 'wait', 'show'], rows=24)
    check('the text is typed', 'qqqtext' in out[0], out[0])
    check('Ctrl-F10 undoes the typing', 'qqq' not in out[1] and
          'Undone' in out[1], out[1])
    check('Ctrl-F11 redoes it', 'qqqtext' in out[2] and 'Redone' in out[2],
          out[2])

    print('undokeys: %d failures' % len(FAILS))

    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
