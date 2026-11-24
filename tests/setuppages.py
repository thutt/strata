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

"""The Setup panel of the Document Manager: pages and choice keys.

Setup is one panel of three pages (the settings, the screen colors, the
advanced settings).  PgUp goes forward and PgDn backward, round and round,
as the multi-page panels of the Editor do.  Ins keeps the changes of every
page, Esc takes them all back.  A choice field takes Right (the next, none
after the last), Left (the previous, none before the first), Space (the next,
round to the first) and F2 or Keypad + (the list); a letter does nothing.

Every check fails when its feature is turned off: a mutation of the program
for each of them was run (see docs/DM-SETUP-DESIGN.txt).

    STR=... STRATA_HELP=... python3 -I -B tests/setuppages.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
strprog = os.environ.get('STR', os.path.join(build, 'term', 'str'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
if not os.path.exists(strprog):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='setuppages')
os.chdir(tmp)
# No queues: Print To has two entries, File only and PDF viewer.
os.environ['STRATA_LPSTAT'] = '/no/such/lpstat'
SETUP = tui.OPEN_SETUP


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def home(text=None):
    h = tempfile.mkdtemp(prefix='h', dir=tmp)
    os.chmod(h, 0o700)
    if text is not None:
        open(os.path.join(h, 'config'), 'w').write(text)
        os.chmod(os.path.join(h, 'config'), 0o600)
    return h


def run(steps, h=None, config=None):
    h = h or home(config)
    os.environ['STRATA_HOME'] = h
    d = tui.session([strprog, '-H', helpf], steps,
                    env={'COLORTERM': None, 'STRATA_COLORS': '256'})
    return d, h


def conf(h):
    p = os.path.join(h, 'config')
    return open(p).read() if os.path.exists(p) else ''


def line(d, label):
    for l in d.split('\n'):
        if label in l:
            return l
    return ''


def value(d, label):
    """What the field shows, without its default."""
    return line(d, label).split('default')[0]


P1 = 'Setup (settings)'
P2 = 'Setup (screen colors)'
P3 = 'Setup (advanced settings)'

# ---- the pages and the keys that go from page to page ------------------------
d, h = run(SETUP + ['show', 'PGUP', 'show', 'PGUP', 'show', 'PGUP', 'show'])
check('pages: PgUp goes forward: settings, colors, advanced, settings again',
      P1 in d[0] and P2 in d[1] and P3 in d[2] and P1 in d[3], d)
d, h = run(SETUP + ['PGDN', 'show', 'PGDN', 'show', 'PGDN', 'show'])
check('pages: PgDn goes backward: advanced, colors, settings',
      P3 in d[0] and P2 in d[1] and P1 in d[2], d)
check('pages: the first page has no line Screen Colors and no line Advanced, '
      'and the frame names PgUp/PgDn',
      'Screen Colors:' not in d[2] and 'Advanced:' not in d[2] and
      'PgUp/PgDn: Page' in d[2], d[2])
d, h = run(SETUP + ['PGUP', 'PGDN', 'show'])
check('pages: PgDn goes back from the second page to the first',
      P1 in d[0], d[0])
d, h = run(SETUP + ['PGUP', 'PGUP', 'show', 'PGDN', 'show'])
check('pages: the third page has the macro set and the Editor program, and '
      'PgDn goes back to the colors',
      'Initial Macro Set:' in d[0] and 'Editor program:' in d[0] and
      P2 in d[1], d)
d, h = run(SETUP + ['ENTER', 'show'])
check('pages: Enter on the first page opens no sub-panel (it moves on)',
      P1 in d[0] and 'Setup: Advanced' not in d[0], d[0])
d, h = run(SETUP + ['F2', 'show'])
check('pages: F2 on the first field opens no sub-panel',
      P1 in d[0] and P3 not in d[0] and P2 not in d[0], d[0])

# ---- a bad value blocks the change of page -----------------------------------
for key, name in (('PGUP', 'PgUp'), ('PGDN', 'PgDn')):
    d, h = run(SETUP + ['DOWN'] * 3 + ['text:abc', key, 'show'])
    check('pages: %s with a wrong value stays on the page and gives the '
          'reason' % name, P1 in d[0] and 'The autosave interval is 1 to '
          '86400 seconds' in d[0], d[0])

# ---- Ins keeps the changes of every page, Esc takes them back ----------------
ALL = SETUP + ['DOWN', 'text:7', 'PGUP', 'RIGHT', 'PGUP', 'DOWN',
               'text:y.lib']
d, h = run(ALL + ['INS', 'wait'])
c = conf(h)
check('pages: Ins keeps the Backups Kept of page 1, the color of page 2 and '
      'the library of page 3', '\nbackups=7\n' in c and
      '\nterminal_color_normal_fg=white\n' in c and
      '\ninitial_library=y.lib\n' in c, c)
d, h = run(ALL + ['PGUP', 'INS', 'wait'])
c = conf(h)
check('pages: Ins on page 1 keeps the changes of pages 2 and 3 too',
      '\nbackups=7\n' in c and '\nterminal_color_normal_fg=white\n' in c and
      '\ninitial_library=y.lib\n' in c, c)
d, h = run(ALL + ['ESC', 'wait'])
check('pages: Esc on page 3 takes back the changes of all pages: no file',
      conf(h) == '', conf(h))
d, h = run(ALL + ['PGUP', 'ESC', 'wait'])
check('pages: Esc on page 1 takes back the changes of all pages: no file',
      conf(h) == '', conf(h))
d, h = run(ALL + ['ESC', 'wait', 'show'] + SETUP + ['show', 'DOWN',
    'show', 'PGUP', 'show'])
check('pages: Setup opened again shows the old values of every page',
      'Backups Kept (0 to 99):  3' in line(d[2], 'Backups Kept') and
      any('Normal text' in l and 'yellow' in l for l in d[3].split('\n')),
      d[2:])

# ---- the colors are live while Setup is open, and Esc takes them back --------
d, h = run(SETUP + ['PGUP', 'RIGHT', 'wait', 'cell:0,2', 'ESC', 'wait',
                    'cell:0,2'])
check('pages: a color chosen on page 2 is used at once',
      d[0].startswith("(' ', 231, 19,"), d[0])
check('pages: Esc puts the color back at once (the screen is yellow again)',
      d[1].startswith("(' ', 227, 19,"), d[1])
d, h = run(SETUP + ['PGUP', 'RIGHT', 'INS', 'wait', 'cell:0,2'])
check('pages: Ins keeps the chosen color on the screen',
      d[0].startswith("(' ', 231, 19,"), d[0])

# ---- the choice keys ---------------------------------------------------------
PT = ['DOWN'] * 4
d, h = run(SETUP + PT + ['show', 'text: ', 'show', 'text: ', 'show',
                         'text: ', 'show'])
w = [value(x, 'Print To:') for x in d]
check('choice: Space takes the next choice and wraps round to the first',
      'File only' in w[0] and 'PDF viewer' in w[1] and 'File only' in w[2] and
      'PDF viewer' in w[3], w)
d, h = run(SETUP + PT + ['RIGHT', 'RIGHT', 'show'])
check('choice: Right goes on to the last choice and then does nothing',
      'PDF viewer' in value(d[0], 'Print To:'), d[0])
d, h = run(SETUP + PT + ['LEFT', 'show'])
check('choice: Left at the first choice does nothing',
      'File only' in value(d[0], 'Print To:'), d[0])
d, h = run(SETUP + PT + ['RIGHT', 'LEFT', 'show'])
check('choice: Left goes back to the previous choice',
      'File only' in value(d[0], 'Print To:'), d[0])
d, h = run(SETUP + PT + ['text:p', 'text:v', 'text:P', 'show'])
check('choice: a letter does nothing (the Editor has no letter jump)',
      'File only' in value(d[0], 'Print To:'), d[0])
d, h = run(SETUP + PT + ['F2', 'wait', 'show', 'DOWN', 'ENTER', 'show'])
check('choice: F2 shows the list of the choices, and Enter takes one',
      'File only' in d[0] and 'PDF viewer' in d[0] and
      'Print To:' in d[0] and 'PDF viewer' in value(d[1], 'Print To:'), d)
d, h = run(SETUP + PT + ['A-+', 'wait', 'show'])
check('choice: Keypad + shows the list too',
      'PDF viewer' in d[0] and d[0].count('File only') >= 2, d[0])
d, h = run(SETUP + ['DOWN'] * 2 + ['text: ', 'show', 'text: ', 'show'])
check('choice: Space on Autosave takes No, and round again takes Yes',
      'No' in value(d[0], 'Autosave:') and 'Yes' in value(d[1], 'Autosave:'),
      d)
d, h = run(SETUP + ['DOWN'] * 8 + ['text: ', 'show', 'text: ', 'show'])
check('choice: Space on Paper takes A4, and round again takes Letter',
      'A4' in value(d[0], 'Paper for New Documents') and
      'Letter' in value(d[1], 'Paper for New Documents'), d)
d, h = run(SETUP + PT + ['show'])
check('choice: the foot of the screen names the keys of the choice',
      'Space Next' in d[0] and 'Left Right Choose' in d[0] and
      'F2 List' in d[0], d[0])
d, h = run(SETUP + ['show'])
check('choice: and not on a field that has no choice',
      'Space Next' not in d[0], d[0])
d, h = run(SETUP + PT + ['text: ', 'DEL', 'show'])
check('choice: Del puts the default back', 'File only' in
      value(d[0], 'Print To:'), d[0])
d, h = run(SETUP + PT + ['text: ', 'INS', 'wait'])
check('choice: Print To PDF viewer is saved as print_to=viewer, never the '
      'old words', '\nprint_to=viewer\n' in conf(h) and
      'open' not in conf(h).lower(), conf(h))
d, h = run(SETUP + ['show'], config='print_to=open\n')
check('choice: an old print_to=open is read and shown as PDF viewer',
      'PDF viewer' in value(d[0], 'Print To:'), d[0])
d, h = run(SETUP + ['INS', 'wait'], config='print_to=open\nbackup=no\n')
check('choice: and it is saved back as viewer, with no old key',
      '\nprint_to=viewer\n' in conf(h) and 'backup' not in conf(h) and
      'print_to=open' not in conf(h), conf(h))
d, h = run(SETUP + ['show'], config='printer=Open to print\n')
check('choice: the old printer=Open to print is shown as PDF viewer',
      'PDF viewer' in value(d[0], 'Print To:') and
      'Open to print' not in d[0], d[0])

# ---- the PDF viewer of an old file: a bare name is not used -------------------
d, h = run(SETUP + ['DOWN'] * 7 + ['show'], config='pdf_viewer=evince\n')
check('viewer: an old setting that is a bare name is not used: the field is '
      'empty and the foot says why',
      'The old setting evince is not a full path' in d[0] and
      'evince' not in value(d[0], 'PDF Viewer:'), d[0])
d, h = run(SETUP + ['INS', 'wait'], config='pdf_viewer=evince\nprint_to=open\n')
check('viewer: and saving writes no pdf_viewer line',
      'pdf_viewer' not in conf(h) and '\nprint_to=viewer\n' in conf(h),
      conf(h))

print('setuppages: %d failures' % fails)
sys.exit(1 if fails else 0)
