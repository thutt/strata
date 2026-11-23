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

"""The name of the help file of the earlier product, in capital letters or
as the name of the file, and the environment variable of that name must not
be in the code, the tests, the tools, the documents or the manual of Strata,
in any build type.  The notes hold the text of the earlier product as it was
decoded and are not looked at."""
import sys as _s; _s.dont_write_bytecode = True
import os, sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
# The notes hold the text of the earlier product as it was decoded.
skip = {'notes', 'build', 'results'}
# The words are written in two pieces so that this file does not hold them.
needle = 'MS' + 'HELP'
oldfile = 'ms' + 'help.hlp'
hits = []
for d, dirs, files in os.walk(root):
    dirs[:] = [x for x in dirs if x not in skip and not x.startswith('.')]
    for f in files:
        p = os.path.join(d, f)
        if f.endswith(('.pyc', '.o', '.png', '.doc', '.pdf', '.dvi')):
            continue
        try:
            text = open(p, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        for n, line in enumerate(text.split('\n'), 1):
            if needle in line or oldfile in line:
                hits.append('%s:%d: %s' % (os.path.relpath(p, root), n,
                                           line.strip()[:80]))
for h in hits:
    print('FAIL', h)
print('nomshelp: %d hit(s)' % len(hits))
sys.exit(1 if hits else 0)
