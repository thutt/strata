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

"""The definitions of the large logo and of the marks of Logic Magicians
Software are in the program.  The files logo/stratalogo.def and
logo/lmslogo.def are the sources; src/logodata.c holds the same text as C
strings, so that the programs need no file to be installed.  Without an
option this checks that src/logodata.c is what the files make.  With --write
the file is made again."""
import sys as _s; _s.dont_write_bytecode = True
import os, sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
OUT = os.path.join(root, 'src', 'logodata.c')
WIDTH = 66

HEAD = '''/*
 * Strata is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Strata is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Strata.  If not, see <https://www.gnu.org/licenses/>.
 */
/* Copyright (c) 2026 Logic Magicians Software */

/*
 * logodata.c - the definitions of the large logo and of the marks of Logic
 * Magicians Software as C strings.  This file is made by tests/logodata.py
 * --write from logo/stratalogo.def and logo/lmslogo.def: change those files
 * and make this one again.
 */
#include "logo.h"
'''


def literal(line):
    """The C string literal of one line, in pieces that fit the width.  The
    end of the line is not in it."""
    esc = line.replace('\\', '\\\\').replace('"', '\\"')
    pieces = []
    while len(esc) > WIDTH:
        cut = WIDTH
        # Do not cut an escape sequence in two.
        k = 0
        i = 0
        while i < cut:
            i += 2 if esc[i] == '\\' else 1
        cut = i
        pieces.append(esc[:cut])
        esc = esc[cut:]
    pieces.append(esc)
    return pieces


def array(name, path):
    lines = open(path).read().split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    out = ['', 'const char *const %s[] = {' % name]
    for line in lines:
        ps = literal(line)
        for i, p in enumerate(ps):
            end = ',' if i == len(ps) - 1 else ''
            out.append('    "%s"%s' % (p, end))
    out.append('    NULL};')
    return out


def make():
    out = HEAD.split('\n')
    out += array('logo_def_lines', os.path.join(root, 'logo',
                                               'stratalogo.def'))
    out += array('lms_def_lines', os.path.join(root, 'logo', 'lmslogo.def'))
    return '\n'.join(out) + '\n'


if '--write' in sys.argv:
    open(OUT, 'w').write(make())
    print('wrote', OUT)
    sys.exit(0)

want = make()
have = open(OUT).read() if os.path.exists(OUT) else ''
bad = [l for l in want.split('\n') if len(l) > 80]
if have != want:
    print('FAIL src/logodata.c is not what logo/stratalogo.def and '
          'logo/lmslogo.def make: run tests/logodata.py --write')
    sys.exit(1)
if bad:
    print('FAIL a line of src/logodata.c is longer than 80 columns')
    sys.exit(1)
print('logodata: ok')
