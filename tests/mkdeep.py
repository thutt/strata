#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""Makes a structured document whose sections are nested deeply.

    python3 -I -B tests/mkdeep.py DEPTH FILE.doc

The document is a chain of DEPTH sections, each nested in the one before it.
The headline of the section of level N is "Level N" and its text is "Text of
Level N".  tests/deepsect.py uses it, and it is the generator of the
documents that probed the depth of the original (docs/SECTION-DEPTH.txt).
"""
import sys as _s; _s.dont_write_bytecode = True
import sys


def make(depth):
    """The bytes of the document."""
    out = [b'*R3\r\n', b'MDDeep test\x00\r\n', b'MR1000\r\n',
           b'PP\\1\\\x00\r\n',
           b'FTJ3\r\nFSH150\r\nFST150\r\nFSS200\r\nFSC150\r\nFI3\r\nFU2\r\n',
           b'DP0,8500,11000\r\n', b'*B1\r\n', b'S\r\n+\r\n']

    for level in range(1, depth + 1):
        title = ('Level %d' % level).encode()

        if level > 1:
            out.append(b'+\r\n')

        out.append(b'S\r\nT' + title + b'\x1d\x00\r\n')
        out.append(b'B\r\nTText of ' + title + b'\x1d\x00\r\n')

    out.append(b'-\r\n' * depth)
    out.append(b'*E\r\n')

    return b''.join(out)


def main(argv):
    if len(argv) != 3:
        print('usage: mkdeep.py DEPTH FILE.doc')

        return 2

    with open(argv[2], 'wb') as f:
        f.write(make(int(argv[1])))

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
