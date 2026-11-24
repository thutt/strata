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

"""A print system that answers slowly never freezes a panel.

The Setup page of the Document Manager and the Print panels of the Editor
need the list of printers.  The programs ask lpstat once, in the background,
when they start, and keep the answer.  A panel waits for it at most 0.3
second: when lpstat has not answered, the panel opens at once and says that
the printer list is not available yet; the next opening finds the answer.

The test names a fake lpstat that sleeps two seconds before it writes
(STRATA_LPSTAT), opens the panels at once after the start, and checks the
time that the whole run takes, what the panels say, and that a second opening
costs nothing.  With the old code, which waited for lpstat every time the
panel was opened, the run takes more than four seconds.

    STRED=... STR=... STRATA_HELP=... python3 -I -B tests/lpslow.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strprog = os.environ.get('STR', os.path.join(build, 'term', 'str'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
sample = os.path.join(root, 'manual', 'samples', 'chars.doc')

if not os.path.exists(stred) or not os.path.exists(strprog) or \
        not os.path.exists(sample):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='lpslow')
doc = os.path.join(tmp, 'chars.doc')
shutil.copy(sample, doc)
SLOW = 2.0
FAKE = os.path.join(tmp, 'lpstat')

with open(FAKE, 'w') as f:
    f.write('#!/bin/sh\n'
            'sleep 2\n'
            'if [ "$1" = "-d" ]; then\n'
            '    echo "system default destination: slowq"\n'
            'else\n'
            '    echo "slowq accepting requests since Thu Nov 20 08:00"\n'
            'fi\n')

os.chmod(FAKE, 0o755)

# The same fake without the sleep: the time that the keys and the drawing
# need by themselves.
QUICK = os.path.join(tmp, 'quick', 'lpstat')
os.makedirs(os.path.dirname(QUICK))

with open(QUICK, 'w') as f:
    f.write(open(FAKE).read().replace('sleep 2', 'true'))

os.chmod(QUICK, 0o755)


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
          + str(extra)))
    fails += 0 if cond else 1


def home():
    h = tempfile.mkdtemp(prefix='h', dir=tmp)
    os.chmod(h, 0o700)
    return h


def timed(prog, start_text, open_keys, panel_text, after=(),
          fake=None):
    """Run a program with the slow lpstat: wait for the start screen, press
    the keys that open a panel and wait for the text of the panel.  Returns
    the seconds from the first key to the panel on the screen and the screen
    after the keys in after.  The seconds do not include the start of the
    program (it waits 0.8 second for the answer of the terminal) and not the
    end of the run, in which the fake lpstat may still be sleeping."""
    env = {'STRATA_LPSTAT': fake or FAKE, 'STRATA_HOME': home(),
           'LANG':
           'en_US.UTF-8', 'LC_ALL': None, 'STRATA_ASCII': '0', 'TMUX': None}
    out = tui.session(prog, ['waitfor:' + start_text, 'clock'] +
                      list(open_keys) + ['waitfor:' + panel_text, 'clock'] +
                      list(after) + ['show'], env=env)

    return float(out[1]) - float(out[0]), out[2]


# ---- the Setup page of the Document Manager ---------------------------------
# What the keys and the drawing take with a lpstat that answers at once is
# the base; the slow lpstat may add little to it.  The old code, which waited
# for lpstat every time a panel was opened, added two to four seconds.
SLACK = 0.5
DM = 'Edit / Preview / Print Document'
ED = 'F1: Help'
base, _ = timed([strprog, '-H', helpf], DM, ['END UP', 'ENTER'],
                'Backups Kept', fake=QUICK)
secs, setup = timed([strprog, '-H', helpf], DM, ['END UP', 'ENTER'],
                    'Backups Kept', ['DOWN'] * 5)
check('Setup is on the screen %.2f s after the key (%.2f s with a lpstat '
      'that answers at once) although lpstat takes %.0f s' %
      (secs, base, SLOW), 'Backups Kept' in setup and secs < base + SLACK,
      setup)
check('Setup shows that the printer list is not available yet, and not that '
      'there are no printers', 'not available yet' in setup and
      'No printers found' not in setup, setup)

# A second opening, after lpstat has answered, costs nothing and shows the
# default printer of the system.
_, again = timed([strprog, '-H', helpf], DM,
                 ['END UP', 'ENTER', 'ESC'] + ['wait'] * 6 +
                 ['END UP', 'ENTER'], 'Backups Kept', ['DOWN'] * 5)
check('after the answer the second opening shows the default printer of the '
      'system', 'slowq' in again and 'not available yet' not in again,
      again)

# ---- the Print panels of the Editor ----------------------------------------
FINAL = ['F12', 'text:P', 'RIGHT', 'ENTER']
DRAFT = ['F12', 'text:P', 'ENTER']
base, _ = timed([stred, doc, '-H', helpf], ED, FINAL, 'List of Figures',
                fake=QUICK)
secs, panel = timed([stred, doc, '-H', helpf], ED, FINAL, 'List of Figures')
check('the Print panel is on the screen %.2f s after the key (%.2f s with a '
      'lpstat that answers at once) although lpstat takes %.0f s' %
      (secs, base, SLOW), 'Print Document' in panel and
      secs < base + SLACK, panel)

secs, panel = timed([stred, doc, '-H', helpf], ED, DRAFT, 'List of Figures')
check('the Draft Print panel is on the screen %.2f s after the key' % secs,
      'Draft Print' in panel and secs < base + SLACK, panel)

# The second opening, after the answer, says nothing about the list.
_, panel = timed([stred, doc, '-H', helpf], ED,
                 FINAL + ['ESC'] + ['wait'] * 6 + FINAL, 'List of Figures')
check('the second Print panel, after the answer, says nothing about the '
      'printer list', 'Print Document' in panel and
      'not available yet' not in panel, panel)

shutil.rmtree(tmp, ignore_errors=True)
print('lpslow: %d failures' % fails)
sys.exit(1 if fails else 0)
