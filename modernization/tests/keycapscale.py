#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The panel of the Keycap marker refuses a Foreground or a Background out
of 0.0 to 100.0 and a Scale out of 0.001 to 10, with a message, and keeps
the values that are in range.  Run on its own:

    python3 -I -B tests/keycapscale.py
"""
import sys as _s; _s.dont_write_bytecode = True
import atexit, os, shutil, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))

if not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='keycapscale')
atexit.register(shutil.rmtree, tmp, True)
os.environ['STRATA_HOME'] = os.path.join(tmp, 'home')
os.makedirs(os.environ['STRATA_HOME'], mode=0o700)
CRLF = b'\r\n'
DOC = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
       b'P1C33' + CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF +
       b'DP0,8500,11000' + CRLF + b'*B0' + CRLF + b'S' + CRLF +
       b'Tx\x1d\x00' + CRLF + b'*E' + CRLF)
count = 0


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
          + str(extra)))
    fails += 0 if cond else 1


def panel(downs, value):
    """Open the panel of a Keycap marker in a new document, type the Legend
    Esc, go DOWNS fields down, type VALUE, press Ins, and return the screen
    and the bytes of the document after a save."""
    global count
    count += 1
    path = os.path.join(tmp, 'k%d.doc' % count)

    with open(path, 'wb') as f:
        f.write(DOC)

    d = tui.session([stred, '-H', helpf, path],
                    ['DOWN', 'A-F12', 'text:K', 'text:K', 'INS', 'wait',
                     'text:Esc'] + ['DOWN'] * downs +
                    ['text:' + value, 'INS', 'wait', 'show', 'C-s', 'wait'])

    return d[0], open(path, 'rb').read()


for downs, label, bad, good in ((1, 'Foreground', '100.1', '100.0'),
                                (2, 'Background', '100.1', '0.0'),
                                (3, 'Scale', '10.001', '10')):
    screen, data = panel(downs, bad)
    check('%s %s: refused with a message' % (label, bad),
          '%s must be' % label in screen and b'Esc' not in data, screen)
    screen, data = panel(downs, good)
    check('%s %s: accepted' % (label, good),
          'must be' not in screen and b'Esc' in data, screen)

screen, data = panel(3, '0')
check('Scale 0: refused, the least is 0.001',
      'Scale must be 0.001 to 10.000' in screen and b'Esc' not in data,
      screen)
screen, data = panel(3, '0.001')
check('Scale 0.001: accepted', 'must be' not in screen and b'Esc' in data,
      screen)
screen, data = panel(3, '-1')
check('Scale -1: refused, a scale has no sign',
      'Not a valid number (Scale)' in screen and b'Esc' not in data, screen)
screen, data = panel(3, 'abc')
check('Scale abc: not a number', 'Not a valid number (Scale)' in screen,
      screen)

print('keycapscale: %d failures' % fails)
sys.exit(1 if fails else 0)
