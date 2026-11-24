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

"""Which fixed-width fonts have every character that the screen draws?

The terminal version draws the characters of the code page 437 table
cp437_uni of src/layout.c, as UTF-8 (nothing else outside ASCII).  This
script reads that table from the source and asks fontconfig (fc-list,
fc-query) for the character set of every font with fixed width, and prints
for each family how many of the code points it lacks and which.

    python3 -I -B tests/fontcover.py [-v] [family ...]

With -v every missing code point is named.  With no family, every fixed
width family fontconfig knows is checked.  Study helper of
docs/TERMINALS.txt; not part of the smoke test.
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, '..', 'src', 'layout.c')).read()
tab = src[src.index('cp437_uni[256]'):]
tab = tab[:tab.index('};')]
NEEDED = sorted(set(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{4})',
    tab)) - set(range(0x20, 0x7f)))


def charset(path):
    out = subprocess.run(['fc-query', '--format', '%{charset}\n', path],
        capture_output=True, text=True).stdout.strip().split()
    have = set()
    for r in out:
        a, _, b = r.partition('-')
        for c in range(int(a, 16), int(b or a, 16) + 1):
            have.add(c)
    return have


def main():
    args = [a for a in sys.argv[1:] if a != '-v']
    verbose = '-v' in sys.argv
    fonts = {}
    q = subprocess.run(['fc-list', ':spacing=100', 'family', 'file',
        'style'], capture_output=True, text=True).stdout
    for line in q.splitlines():
        f, _, rest = line.partition(': ')
        fam = rest.split(':')[0].split(',')[0].strip()
        if 'style=' in line and 'Regular' not in line and 'Book' not in line \
                and 'Medium' not in line and 'Roman' not in line:
            continue
        if args and fam not in args:
            continue
        fonts.setdefault(fam, f)
    print('%d code points needed (%d outside ASCII)' % (len(NEEDED) + 95,
        len(NEEDED)))
    for fam in sorted(fonts):
        have = charset(fonts[fam])
        miss = [c for c in NEEDED if c not in have]
        print('%-28s missing %3d%s' % (fam, len(miss),
            '' if not miss or not verbose else ': ' + ' '.join(
            'U+%04X' % c for c in miss)))


main()
