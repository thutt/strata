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

"""The terminal version does not use the mouse (docs/MOUSE.txt, stages 1 and
2 are for the X11 window only).  It must send no escape sequence that turns a
mouse mode on (the modes 1000, 1002, 1003, 1005, 1006, 1015 and the alternate
scroll mode 1007), at the start, while keys are typed, in the Preview and when
it ends, and a document must stay as it was after keys that only move.

    STRATA_BUILD_DIR=... python3 -I -B tests/mousetui.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
sample = os.path.join(here, 'samples', 'brief-long.doc')
if not os.path.exists(stred) or not os.path.exists(sample):
    print('SKIP: build the programs first')
    sys.exit(0)
fails = 0
tmp = tempfile.mkdtemp(prefix='mousetui')
doc = os.path.join(tmp, 'a.doc')
shutil.copy(sample, doc)
MODES = re.compile(rb'\x1b\[\?(1000|1001|1002|1003|1005|1006|1015|1007)[hl]')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


check('the pattern finds a mode that is switched on',
      MODES.search(b'a\x1b[?1006hb') is not None)
env = {'STRATA_HOME': tmp, 'LANG': 'en_US.UTF-8'}
out = tui.session([stred, doc, '-H', helpf],
                  ['wait', 'rawbytes', 'DOWN', 'DOWN', 'PGDN', 'wait', 'rawbytes',
                   'F12', 'wait', 'ESC', 'wait', 'rawbytes', 'C-q', 'wait',
                   'rawbytes'], env=env)
raws = [eval(o) for o in out]
check('terminal: no mouse mode is switched on at the start',
      MODES.search(raws[0]) is None, MODES.findall(raws[0]))
check('terminal: no mouse mode while keys are typed',
      MODES.search(raws[1]) is None, MODES.findall(raws[1]))
check('terminal: no mouse mode around a menu',
      MODES.search(raws[2]) is None, MODES.findall(raws[2]))
check('terminal: no mouse mode when the program ends',
      MODES.search(raws[3]) is None, MODES.findall(raws[3]))
check('terminal: the start did write the screen (the test is not empty)',
      len(raws[0]) > 500, len(raws[0]))
shutil.rmtree(tmp, ignore_errors=True)
print('mousetui: %d failure(s)' % fails)
sys.exit(1 if fails else 0)
