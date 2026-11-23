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

"""Cursor keys in a burst, in the terminal version of the Editor.

The Editor leaves out a drawing when another key is waiting and draws when no
key is left.  The keys must still all be handled and the last drawing must
be made: the screen after 100 Down keys that are written at once must be the
screen after the same keys written one at a time.  A drawing that is left out
and never made would show the screen of an earlier key."""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui

bdir = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(bdir, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(bdir, 'strata.hlp'))
root = os.path.dirname(here)
fails = 0
NKEYS = 100


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra))); fails += 0 if cond else 1


if not os.path.exists(stred):
    print('SKIP: no %s' % stred); sys.exit(0)
tmp = tempfile.mkdtemp()
os.environ['STRATA_HOME'] = os.path.join(tmp, 'home')
docs = [('GettingStarted.doc', os.path.join(root, 'manual', 'doc',
        'GettingStarted.doc')), ('the Reference manual', os.path.join(root,
        'manual', 'doc', 'strata-reference-manual.doc'))]
try:
    for name, src in docs:
        path = os.path.join(tmp, 'lag.doc')
        shutil.copy(src, path)
        burst = tui.session([stred, '-H', helpf, path],
            [' '.join(['DOWN'] * NKEYS), 'wait', 'wait', 'show'],
            rows=25, delay=0.0)
        top = tui.session([stred, '-H', helpf, path], ['show'], rows=25)
        slow = tui.session([stred, '-H', helpf, path],
            ['DOWN'] * NKEYS + ['wait', 'show'], rows=25, delay=0.03)
        check('termlag: %d Down keys written at once move the cursor on '
            '%s' % (NKEYS, name), burst[0] != top[0], burst[0])
        check('termlag: the screen after %d keys at once is the screen '
            'after the same keys one by one on %s' % (NKEYS, name),
            burst[0] == slow[0], (burst[0], slow[0]))
finally:
    shutil.rmtree(tmp, ignore_errors=True)
print('termlag: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
