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

"""The Printer field is a choice field like the others (docs/CHOICE-FIELDS.
txt).  In the Print panel and in the Draft Print panel of the Editor the field
takes no typed text and no plus: the space bar steps forward through File
only, PDF viewer, each queue that the system reports (a fake lpstat with three
queues), the System default printer and, last, Search for printers..., and
wraps to the first; Left and Right step as in every choice field; a value
that the settings have and the list has not is kept and shown; a slow lpstat
(it sleeps two seconds) does not hold the panel and the queues are entries as
soon as they are known; the entry Search for printers... runs the search
(a fake lpinfo) when Enter is pressed on the field or Ins accepts the panel,
and the field goes back to the entry that was chosen before; the PDF is not
printed to the search.  The Setup page of the Document Manager has the same
keys for Print To and Printer Queue.  Nothing is sent to a printer: lpstat,
lpinfo and lp are fake programs.

    STRED=... STR=... STRATA_HELP=... python3 -I -B tests/printchoice.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strmgr = os.environ.get('STR', os.path.join(build, 'term', 'str'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
sample = os.path.join(root, 'manual', 'samples', 'chars.doc')
if not (os.path.exists(stred) and os.path.exists(strmgr) and
        os.path.exists(sample)):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='pchoice')
doc = os.path.join(tmp, 'chars.doc')
shutil.copy(sample, doc)
QUEUES = ('alpha', 'bravo', 'charlie')
ENTRIES = tui.printer_entries(QUEUES)
FINAL = tui.OPEN_PRINT_FINAL
DRAFT = tui.OPEN_PRINT_DRAFT
SPACE = 'text: '


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
          + str(extra)))
    fails += 0 if cond else 1


def fake(name, body):
    path = os.path.join(tmp, name)
    with open(path, 'w') as f:
        f.write('#!/bin/bash\n' + body)
    os.chmod(path, 0o755)
    return path


LPSTAT = os.path.join(here, 'fakelpstat.sh')
SLOW = fake('lpstat-slow', 'sleep 2\nexec %s "$@"\n' % LPSTAT)
DEVICES = fake('lpinfo-devices',
               'echo "network socket://192.168.1.201:9100"\n')
LPLOG = os.path.join(tmp, 'lp.log')
LP = fake('lp', 'echo "$@" >> %s\necho "request id is x-1 (1 file(s))"\n'
          % LPLOG)


def home(config=''):
    h = tempfile.mkdtemp(prefix='h', dir=tmp)
    os.chmod(h, 0o700)
    if config:
        open(os.path.join(h, 'config'), 'w').write(config)
        os.chmod(os.path.join(h, 'config'), 0o600)
    return h


def run(steps, lpstat=LPSTAT, config='', prog=None, rows=40):
    env = {'STRATA_HOME': home(config), 'LANG': 'en_US.UTF-8', 'LC_ALL': None,
           'STRATA_ASCII': '0', 'TMUX': None, 'STRATA_LPSTAT': lpstat,
           'STRATA_LPINFO': DEVICES, 'FAKELPSTAT_QUEUES': ' '.join(QUEUES),
           'FAKELPSTAT_DEFAULT': 'bravo'}
    args = [stred, doc, '-H', helpf] if prog is None else prog
    return tui.session(args, steps, env=env, rows=rows)


def field(screen, label):
    """The entry that a choice field shows, between its brackets."""
    for l in screen.split('\n'):
        m = re.search(label + r':\s*\[([^\]]*)\]', l)
        if m:
            return m.group(1)
    return None


def printer(screen):
    return field(screen, 'Printer')


def setup_value(screen, label):
    """What a field of the Setup page shows: the text after its label up to
    the dots that fill the field or the word default."""
    for l in screen.split('\n'):
        if label + ':' in l:
            t = l.split(label + ':', 1)[1]
            t = re.split('\u00b7|default:|\u2551', t)[0]
            return t.strip()
    return None


def values(steps_after, opener=FINAL, to_printer=True, **kw):
    """Open a Print panel, go to the Printer field, press the keys that
    follow and show the panel after each: the entries that were shown.  When
    the panel opens on a queue, the Output File is inactive and the cursor
    starts on the Printer field already: to_printer is then False."""
    keys = list(opener) + (['DOWN'] if to_printer else []) + ['show']
    for k in steps_after:
        keys += [k, 'show']
    return [printer(d) for d in run(keys, **kw)]


for name, opener in (('Print', FINAL), ('Draft Print', DRAFT)):
    # ---- the space bar steps forward and wraps -----------------------------
    got = values([SPACE] * len(ENTRIES), opener)
    check('%s: the panel starts on File only' % name, got[0] == 'File only',
          got)
    check('%s: the space bar steps File only, PDF viewer, each queue, the '
          'System default printer, Search for printers... and wraps to File '
          'only' % name, got == ENTRIES + ['File only'], got)

    # ---- Left and Right step as in the other choice fields -----------------
    got = values([SPACE] * 3 + ['LEFT'] * 4 + ['RIGHT'], opener)
    check('%s: Left steps back and stops at the first entry, Right steps '
          'forward' % name, got[3:] == ['bravo', 'alpha', 'PDF viewer',
          'File only', 'File only', 'PDF viewer'], got)

    # ---- typed text and the plus change nothing ----------------------------
    got = values(['text:a', 'text:xyz', 'text:+', 'text:-x', 'BKSP'], opener)
    check('%s: typed letters, a plus and Backspace change nothing' % name,
          set(got) == {'File only'}, got)

# ---- the list that F2 and the keypad plus show is a convenience -------------
d = run(FINAL + ['DOWN', 'F2', 'waitfor:charlie', 'show', 'DOWN', 'DOWN',
                 'ENTER', 'show'])
check('F2 shows the entries and Enter chooses one', 'File only' in d[0] and
      'alpha' in d[0] and 'charlie' in d[0] and printer(d[1]) == 'alpha', d)
d = run(FINAL + ['DOWN', 'A-+', 'waitfor:charlie', 'show', 'ESC', 'show'])
check('Alt-plus shows the entries too; Esc keeps the entry',
      'charlie' in d[0] and printer(d[1]) == 'File only', d)

# ---- a value of the settings that the list has not ------------------------
CONF = 'print_to=printer\nprint_queue=ghostq\n'
got = values([SPACE, SPACE], config=CONF, to_printer=False)
check('a queue of the settings that the system does not report is kept as '
      'the value and shown first', got[0] == 'ghostq', got)
check('stepping goes on from it: Search for printers..., then File only',
      got[1:] == ['Search for printers...', 'File only'], got)
got = values(['LEFT'], config=CONF, to_printer=False)
check('Left from it goes to the System default printer',
      got[1] == 'System default printer', got)
got = values([SPACE], config='print_to=viewer\n')
check('the PDF viewer of the settings is the first entry shown and Space '
      'goes on to alpha', got == ['PDF viewer', 'alpha'], got)

# ---- a slow lpstat does not hold the panel; the entries fill in ------------
t0 = time.time()
d = run(FINAL + ['DOWN', 'show'], lpstat=SLOW)
took = time.time() - t0
check('a slow lpstat (2 seconds): the panel is on the screen at once (%.1f '
      's for the whole run)' % took, took < 2.0 and printer(d[0]) ==
      'File only', (took, d))
d = run(FINAL + ['DOWN', 'show'] + ['wait'] * 30 + [SPACE, 'show', SPACE,
        'show', SPACE, 'show'], lpstat=SLOW)
check('with the slow lpstat the queues become entries when they are known: '
      'Space goes File only, PDF viewer, alpha, bravo after the answer',
      [printer(x) for x in d] == ['File only', 'PDF viewer', 'alpha',
      'bravo'], [printer(x) for x in d])

# ---- the search entry runs the search, then the field goes back ------------
steps = [SPACE] * (len(ENTRIES) - 1)
d = run(FINAL + ['DOWN'] + steps + ['show', 'ENTER', 'waitfor:Printers found',
        'show', 'ESC', 'waitfor:Print Document', 'show'])
check('Search for printers... is the entry before Enter', printer(d[0]) ==
      'Search for printers...', d[0])
check('Enter on the field runs the search and shows its list',
      'socket://192.168.1.201:9100' in d[1], d[1])
check('after the list the field has the entry chosen before the search, '
      'not Search for printers...', printer(d[2]) == 'System default printer',
      d[2])
d = run(FINAL + ['DOWN'] + steps + ['INS', 'waitfor:Printers found', 'ESC',
        'waitfor:Print Document', 'show'])
check('Ins while the search is the entry runs the search, and the panel '
      'stays open with the entry before', 'Print Document' in d[0] and
      printer(d[0]) == 'System default printer', d[0])
d = run(FINAL + ['DOWN'] + steps + [SPACE, 'show'])
check('Space moves on from the search entry to File only, with no search',
      printer(d[0]) == 'File only' and 'Printers found' not in d[0], d[0])

# ---- the search is never the value that is printed to ---------------------
rc = subprocess.run([stred, '--print', doc, '--printer',
                     'Search for printers...', '-o', os.path.join(tmp,
                     'x.pdf')], capture_output=True, text=True,
                    env=dict(os.environ, STRATA_HOME=home(),
                             STRATA_LPSTAT=LPSTAT))
check('print to Search for printers... is refused on the command line',
      rc.returncode != 0 and 'not the name of a printer queue' in
      (rc.stdout + rc.stderr) and not os.path.exists(os.path.join(tmp,
      'x.pdf')), (rc.stdout, rc.stderr))

# ---- the Setup page of the Document Manager --------------------------------
SETUP = tui.OPEN_SETUP
PT = ['DOWN'] * 4
TO = PT
Q = PT + ['DOWN']


def setup(steps, config=''):
    return run(SETUP + steps, config=config, prog=[strmgr, '-H', helpf])


d = setup(Q + ['show', SPACE, 'show', SPACE, 'show', SPACE, 'show', SPACE,
               'show'])
vals = [setup_value(x, 'Printer Queue') for x in d]
check('Setup: Printer Queue starts on System default; Space steps through '
      'the queues and wraps', vals == ['System default', 'alpha', 'bravo',
      'charlie', 'System default'], vals)
d = setup(Q + ['text:x', 'text:Z', 'show', 'RIGHT', 'RIGHT', 'LEFT',
               'show'])
check('Setup: typed letters change nothing; Right and Left step '
      'as in the Print panel', setup_value(d[0], 'Printer Queue') ==
      'System default' and setup_value(d[1], 'Printer Queue') == 'alpha',
      [setup_value(x, 'Printer Queue') for x in d])
d = setup(TO + ['show', SPACE, 'show', SPACE, 'show', SPACE, 'show'])
vals = [setup_value(x, 'Print To') for x in d]
check('Setup: Print To is stepped with Space too and wraps (%s)' % vals,
      len(set(vals)) == 3 and vals[0] == vals[3], vals)
d = setup(Q + ['show'], config='print_to=printer\nprint_queue=ghostq\n')
check('Setup: a queue of the settings that is not reported is kept and '
      'shown', 'ghostq' in (setup_value(d[0], 'Printer Queue') or ''), d[0])

print('printchoice: %d failures' % fails)
shutil.rmtree(tmp, ignore_errors=True)
sys.exit(1 if fails else 0)
