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

"""File, Retrieve, Retrieve Named Block Settings Only.

A document that has no named block format retrieves the named block formats of
another document: they are in the header of the saved file, the text of the
document is not touched, a format of the same name is replaced, and a file that
has no named format is refused with a message.

usage: STRED=path/to/stred python3 -I -B tests/retnamed.py
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


def doc(named, text):
    out = [b'*R3\r\n', b'MDNamed test\x00\r\n', b'MR1000\r\n',
           b'PP\\1\\\x00\r\n', b'DP0,8500,11000\r\n']

    for name, recs in named:
        out.append(b'FN' + name.encode() + b'\x00\r\n')

        for r in recs:
            out.append(r.encode() + b'\r\n')

    if named:
        out.append(b'FN\x00\r\n')

    out.append(b'*B0\r\nS\r\nT' + text.encode() + b'\x1d\x00\r\n*E\r\n')

    return b''.join(out)


def retrieve(tmp, dst, src, mode_steps):
    dpath = os.path.join(tmp, 'dst.doc')
    spath = os.path.join(tmp, 'src.doc')

    with open(dpath, 'wb') as f:
        f.write(dst)

    with open(spath, 'wb') as f:
        f.write(src)

    steps = (['F12', 'text:F', 'text:R', 'wait', 'DOWN', 'END'] + ['BKSP'] * 12 + ['text:' + spath, 'DOWN'] +
             mode_steps + ['INS', 'wait', 'show', 'C-s', 'wait'])
    out = tui.session([STRED, '-H', HELP, dpath], steps, rows=24)

    with open(dpath, 'rb') as f:
        return out, f.read()


def main():
    if not os.path.exists(STRED):
        print('SKIP: build the programs first')

        return 0

    tmp = tempfile.mkdtemp(prefix='retn')
    named_src = doc([('Quote', ['BFA4', 'BL6']), ('Body', ['BFJ3'])],
                    'source text')
    named_dst = doc([('Body', ['BFJ0'])], 'destination text')
    space4 = ['text: '] * 4

    out, data = retrieve(tmp, doc([], 'destination text'), named_src, space4)
    check('Retrieve Named Block Settings Only gives the document the named '
          'formats of the file',
          b'FNQuote\x00\r\nBFA4\r\nBL6\r\n' in data and
          b'FNBody\x00\r\nBFJ3\r\n' in data, data[:200])
    check('the text of the document is not changed',
          b'Tdestination text' in data and b'source text' not in data, data)

    out, data = retrieve(tmp, named_dst, named_src, space4)
    check('a named format of the same name is replaced',
          b'FNBody\x00\r\nBFJ3\r\n' in data and b'BFJ0' not in data, data)

    out, data = retrieve(tmp, doc([], 'destination text'),
                         doc([], 'source text'), space4)
    check('a file with no named format is refused with a message',
          'no named block settings' in out[0] and b'FNQuote' not in data,
          out[0])

    print('retnamed: %d failures' % len(FAILS))

    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
