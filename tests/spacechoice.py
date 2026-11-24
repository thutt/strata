#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The space bar changes a field that has a fixed set of values (a choice):
it takes the next value and goes round from the last to the first
(docs/SPACE-CHOICE-AUDIT.txt).  Driven with tests/tui.py:

  - TeX Font Association of the Document Manager: Family (roman, sans, mono)
    and Install Now (No, Yes), which were typed text before;
  - Setup of the Document Manager: Autosave, a Yes or No field;
  - the Print panel of the Editor: Print Double Sided? (Printer default,
    Yes, No), Error Log? (Yes, No) and the Printer field (a list);
  - a text field (Output File, Copies) takes the space as a typed character;
  - a choice field never takes a typed space into its value.

    STRED=... STR=... STRATA_HELP=... python3 -I -B tests/spacechoice.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strmgr = os.environ.get('STR', os.path.join(build, 'term', 'str'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
sample = os.path.join(root, 'manual', 'samples', 'chars.doc')

if not (os.path.exists(stred) and os.path.exists(strmgr)):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='spacech')
doc = os.path.join(tmp, 'chars.doc')
shutil.copy(sample, doc)
SPACE = 'text: '


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
          + str(extra)))
    fails += 0 if cond else 1


def home():
    h = tempfile.mkdtemp(prefix='h', dir=tmp)
    os.chmod(h, 0o700)
    return h


def env():
    return {'STRATA_HOME': home(), 'LANG': 'en_US.UTF-8', 'LC_ALL': None,
            'TMUX': None, 'STRATA_LPSTAT': os.path.join(here,
            'fakelpstat.sh'), 'FAKELPSTAT_QUEUES': 'alpha bravo',
            'FAKELPSTAT_DEFAULT': 'alpha'}


def value(screen, label):
    """What a field shows after its label: a choice in brackets, or a text
    up to the dots that fill the field."""
    for l in screen.split('\n'):
        if label in l:
            t = l.split(label, 1)[1].lstrip(': ')
            m = re.match(r'\[([^\]]*)\]', t)
            if m:
                return m.group(1)
            return re.split('·|║| {3,}', t)[0].strip()
    return None


def cycle(screens, label, first):
    """The values that a field showed in screens: they must start at first,
    change at each press and end at first again."""
    vals = [value(s, label) for s in screens]
    return vals


# ---- TeX Font Association: Family and Install Now -------------------------
FONT = ['DOWN'] * 4 + ['ENTER'] + ['wait'] * 14 + ['DOWN'] * 19 + ['ENTER',
        'wait', 'DOWN', 'DOWN', 'DOWN', 'DOWN']
d = tui.session([strmgr, '-H', helpf], FONT + ['show'] + [SPACE, 'show'] * 3,
                env=env(), rows=30)
fam = [value(x, 'Family (roman, sans or mono)') for x in d]
check('Family: Space steps roman, sans, mono and goes round to roman',
      fam == ['roman', 'sans', 'mono', 'roman'], fam)
d = tui.session([strmgr, '-H', helpf], FONT + ['DOWN', 'DOWN', 'DOWN', 'DOWN', 'show']
                + [SPACE, 'show'] * 2, env=env(), rows=30)
inst = [value(x, 'Install Now (Yes or No)') for x in d]
check('Install Now: Space gives Yes, then No again (it did nothing before)',
      inst == ['No', 'Yes', 'No'], inst)
d = tui.session([strmgr, '-H', helpf], FONT + ['DOWN', 'DOWN', 'DOWN', 'DOWN', 'show',
                'F2', 'show'], env=env(), rows=30)
check('Install Now: the keys that worked still work: F2 lists No and Yes',
      'Yes' in d[1] and 'No' in d[1] and d[1] != d[0], d[1])
d = tui.session([strmgr, '-H', helpf], FONT + ['DOWN', 'DOWN', 'DOWN', 'DOWN', 'RIGHT',
                'show', 'LEFT', 'show'], env=env(), rows=30)
inst = [value(x, 'Install Now (Yes or No)') for x in d]
check('Install Now: Right and Left still choose', inst == ['Yes', 'No'], inst)
d = tui.session([strmgr, '-H', helpf], FONT + ['DOWN', 'text:Nm', SPACE,
                'text:x', 'show'], env=env(), rows=30)
check('a text field of the same panel keeps the space as typed',
      'Nm x' in d[0], d[0])

# ---- Setup: Autosave (Yes, No) ---------------------------------------------
SETUP = tui.OPEN_SETUP + ['DOWN'] * 2
d = tui.session([strmgr, '-H', helpf], SETUP + ['show'] + [SPACE, 'show'] * 2,
                env=env(), rows=40)
au = [value(x, 'Autosave:') for x in d]
check('Setup: Autosave is a Yes or No field and Space goes round: %s' % au,
      au[0] in ('Yes', 'No') and au[1] != au[0] and au[2] == au[0], au)

# ---- the Print panel of the Editor ------------------------------------------
e = env()
FINAL = tui.OPEN_PRINT_FINAL
# Output File, Printer, Copies, Print Double Sided?
SIDES = ['DOWN'] * 3
d = tui.session([stred, doc, '-H', helpf], FINAL + SIDES + ['show'] +
                [SPACE, 'show'] * 3, env=e, rows=40)
sd = [value(x, 'Print Double Sided?') for x in d]
check('Print Double Sided?: Space steps Printer default, Yes, No and goes '
      'round', sd == ['Printer default', 'Yes', 'No', 'Printer default'], sd)
d = tui.session([stred, doc, '-H', helpf], FINAL + ['DOWN'] * 12 + ['show'] +
                [SPACE, 'show'] * 2, env=e, rows=40)
el = [value(x, 'Error Log?') for x in d]
check('Error Log?: a Yes or No field: Space gives No, then Yes: %s' % el,
      el == ['Yes', 'No', 'Yes'], el)
d = tui.session([stred, doc, '-H', helpf], FINAL + ['DOWN', 'wait', 'wait',
                'wait', 'show'] + [SPACE, 'show'] * 3, env=e, rows=40)
pr = [value(x, 'Printer:') for x in d]
check('Printer: Space steps File only, PDF viewer, the queues: %s' % pr,
      pr[:3] == ['File only', 'PDF viewer', 'alpha'], pr)
check('a choice field never takes the typed space into its value',
      all(v is not None and v.strip() != '' for v in pr + sd + el),
      (pr, sd, el))
# A text field takes the space as typed.
d = tui.session([stred, doc, '-H', helpf], FINAL + ['DOWN', 'DOWN',
                'text:1', SPACE, 'text:2', 'show'], env=e, rows=40)
check('Copies, a text field: the space is typed', '1 2' in
      (value(d[0], 'Copies') or ''), d[0])

shutil.rmtree(tmp, ignore_errors=True)
print('spacechoice: %d failure(s)' % fails)
sys.exit(1 if fails else 0)
