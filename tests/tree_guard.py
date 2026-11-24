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

"""Fail if anything under the source tree changed.
usage: tree_guard.py snapshot STATEFILE | check STATEFILE
Watches the source tree, which is the root of the repository (the directory
scripts is in it); the metadata of git and the worktrees are not watched."""
import sys as _s; _s.dont_write_bytecode = True
import os, sys, json
here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
roots = [here]
SKIP = ('.git', '.claude')
def scan():
    out = {}
    for r in roots:
        for d, dirs, files in os.walk(r):
            dirs[:] = [x for x in dirs if x not in SKIP or d != r]
            for f in files + dirs:
                p = os.path.join(d, f)
                try: st = os.lstat(p)
                except OSError: continue
                out[p] = [st.st_size, st.st_mtime_ns, st.st_mode]
    return out
cmd, state = sys.argv[1], sys.argv[2]
if cmd == 'snapshot':
    os.makedirs(os.path.dirname(os.path.abspath(state)), exist_ok=True)
    json.dump(scan(), open(state, 'w'))
elif cmd == 'check':
    old = json.load(open(state)); new = scan(); bad = []
    for p in sorted(set(old) | set(new)):
        if p not in old: bad.append('created   ' + p)
        elif p not in new: bad.append('deleted   ' + p)
        elif old[p] != new[p]: bad.append('modified  ' + p)
    for b in bad: print('TREE GUARD:', b)
    print('tree guard: %s' % ('source tree untouched'
        if not bad else '%d CHANGES' % len(bad)))
    sys.exit(1 if bad else 0)
