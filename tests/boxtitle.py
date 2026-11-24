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

"""The box that a panel shows for a value that it rejects has the title of the
panel, not the word Marker.

usage: STRED=path/to/stred python3 -I -B tests/boxtitle.py
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

    tmp = tempfile.mkdtemp(prefix='bt')
    path = os.path.join(tmp, 'a.doc')

    with open(path, 'wb') as f:
        f.write(b'*R3\r\nMR1000\r\nPP\\1\\\x00\r\nDP0,8500,11000\r\n*B0\r\n'
                b'S\r\nTtext\x1d\x00\r\n*E\r\n')

    # Retrieve with the name field empty: the panel rejects it.
    steps = (['F12', 'text:F', 'text:R', 'wait', 'DOWN', 'END'] +
             ['BKSP'] * 12 + ['INS', 'wait', 'show'])
    out = tui.session([STRED, '-H', HELP, path], steps, rows=24)
    check('the refusal says No file name specified',
          'No file name specified' in out[0], out[0])
    check('the box of the refusal has the title of the panel, Retrieve File',
          'Retrieve File' in out[0], out[0])
    check('no box has the title Marker', 'Marker' not in out[0], out[0])

    print('boxtitle: %d failures' % len(FAILS))

    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
