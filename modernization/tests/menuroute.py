#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The F12 menu runs the command of each function key.

A key that a terminal or a desktop takes still has a way: an entry of the F12
menu.  For each of the 24 commands that had a function key and no entry (and
for Help on the bar) the test starts the Editor twice from the same state:
once it presses the function key, once it opens the menu with F12, chooses the
route with letters and arrows and accepts it with Enter.  The two screens must
be the same: the same panel or the same message.

The check is run again with the key of one command against the route of
another command; the screens differ there, so the comparison can fail.

    STRATA_BUILD_DIR=... python3 -I -B tests/menuroute.py
"""
import sys as _s
_s.dont_write_bytecode = True
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
sample = os.path.join(here, 'samples', 'brief-parts.doc')

if not os.path.exists(stred) or not os.path.exists(sample):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='mr', dir='/tmp')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
          + str(extra)))
    fails += 0 if cond else 1


def run(steps):
    """Run the Editor on a copy of the sample with the keys in steps and
    return the screen that the last 'show' gives."""
    doc = os.path.join(tmp, 'a.doc')
    shutil.copy(sample, doc)
    h = tempfile.mkdtemp(prefix='h', dir=tmp)
    os.chmod(h, 0o700)
    env = {'STRATA_HOME': h, 'TMUX': None, 'LANG': 'en_US.UTF-8',
           'LC_ALL': None, 'STRATA_ASCII': '0', 'STRATA_HELP': helpf}
    out = tui.session([stred, doc], ['waitfor:F1: Help'] + steps +
                      ['wait', 'wait', 'show'], env=env)
    return out[-1]


def route(first, group, index):
    """The keys of a route: F12, the letter of the menu of the bar, the
    letter of the entry of that menu (a group opens at once), and the right
    arrow for the entry in it, then Enter."""
    return (['F12', 'text:' + first] +
            (['text:' + group] if group else []) +
            ['RIGHT'] * index + ['ENTER'])


# The command, its function key and its route in the menu.
COMMANDS = [
    ('Help', 'F1', ['F12', 'text:H']),
    ('Split', 'F2', route('E', 'P', 0)),
    ('Copy', 'F3', route('E', 'B', 0)),
    ('Cut', 'F4', route('E', 'B', 1)),
    ('Paste', 'F5', route('E', 'B', 2)),
    ('Format', 'F6', route('E', 'L', 0)),
    ('Move', 'F7', route('E', 'B', 3)),
    ('Table', 'F8', route('E', 'L', 1)),
    ('Search', 'F9', route('T', 'F', 0)),
    ('Math', 'C-F2', route('T', None, 9)),
    ('Lib Copy', 'C-F3', route('T', 'L', 4)),
    ('Lib Cut', 'C-F4', route('T', 'L', 5)),
    ('Lib Paste', 'C-F5', route('T', 'L', 6)),
    ('Case', 'C-F6', route('E', 'L', 4)),
    ('Search Again', 'C-F9', route('T', 'F', 2)),
    ('Marker', 'C-F12', route('E', 'I', 0)),
    ('Append', 'A-F2', route('E', 'P', 2)),
    ('Join', 'A-F3', route('E', 'P', 1)),
    ('Delete', 'A-F4', route('E', 'B', 4)),
    ('Overtype', 'A-F5', route('E', 'I', 1)),
    ('Set Attribute', 'A-F6', route('E', 'L', 2)),
    ('Go To', 'A-F7', route('T', 'F', 3)),
    ('Border', 'A-F8', route('E', 'L', 3)),
    ('Replace', 'A-F9', route('T', 'F', 1)),
    ('Next Error', 'C-F8', route('P', 'E', 0)),
    ('Prev Error', 'C-F7', route('P', 'E', 1)),
]

screens = {}
baseline = run([])

for name, key, steps in COMMANDS:
    by_key = run([key])
    by_menu = run(steps)
    screens[name] = (by_key, by_menu)
    check('%s: the route of the menu gives the screen of %s' % (name, key),
          by_key == by_menu,
          'key:\n%s\nmenu:\n%s' % (by_key, by_menu))
    check('%s: the command shows a panel or a message' % name,
          by_menu != baseline, by_menu)

# The two remove commands of the Errors submenu have no key: the route is
# the only way, and the message line shows what each did.  The sample has no
# Print Error, so the messages are the ones of an empty document.
remove_here = run(route('P', 'E', 2))
check('Print, Errors, Remove: the message is "not on a Print '
      'Error"', 'The cursor is not on a Print Error' in remove_here,
      remove_here)
remove_all = run(route('P', 'E', 3))
check('Print, Errors, Remove All: the message is "No print errors"',
      'No print errors' in remove_all, remove_all)
check('the two remove routes give different screens',
      remove_here != remove_all, remove_here)

# The screens of different commands differ, so the comparison above can fail.
check('the key of Copy and the route of Cut give different screens',
      screens['Copy'][0] != screens['Cut'][1], screens['Copy'][0])
check('the key of Search and the route of Replace give different screens',
      screens['Search'][0] != screens['Replace'][1], screens['Search'][0])
check('the key of Go To and the route of Math give different screens',
      screens['Go To'][0] != screens['Math'][1], screens['Go To'][0])

shutil.rmtree(tmp, ignore_errors=True)
print('menuroute: %s' % ('all passed' if not fails else
                         '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
