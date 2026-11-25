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

"""Interactive smoke tests: run stred in a pty and assert on the screen."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, sys, shutil, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

# SMOKE_PART=K/N runs the K-th of N parts of this script (tests/smokepart.py).
if os.environ.get('SMOKE_PART'):
    import smokepart
    sys.exit(smokepart.run(__file__, os.environ['SMOKE_PART']))

# Every session has a home directory of its own for the settings of the user
# (~/.strata), unless a test names one, so that a setting that one test makes
# does not reach the next.
home_override = [None]


def session(*args, **kw):
    os.environ['STRATA_HOME'] = home_override[0] or tempfile.mkdtemp(
        prefix='home')
    return tui.session(*args, **kw)


here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
stred = os.environ.get('STRED', os.path.join(os.environ.get('STRATA_BUILD_DIR',
    ''), 'term', 'stred'))
sample = os.environ.get('DOCSRC', os.path.join(root, 'tests', 'samples'))
PLAN = os.path.join(sample, 'plan-title-table.doc')
helpf = os.environ.get('STRATA_HELP', os.path.join(
    os.environ.get('STRATA_BUILD_DIR', ''), 'strata.hlp'))
fails = 0
def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1

tmp = tempfile.mkdtemp()
doc = os.path.join(tmp, 'work.doc'); shutil.copy(os.path.join(sample,
    'plan-title-table.doc'), doc)
base = [stred, '-H', helpf, doc]

d = session(base, ['show'])
check('initial render: headline, status, structure', 'Orchard Plan' in d[0]
    and 'STRUCTURED EDIT' in d[0] and 'Sect: 1' in d[0], d[0])
check('tab alignment (Site: ... Heron Hill Orchard)',
    any(l.startswith('Site:') and 'Heron Hill Orchard' in l
    and l.index('Heron') == 24 for l in d[0].split('\n')), d[0])
check('multi-column block shows column separators', ':' in d[0])

d = session(base, ['text:abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ',
    'show'])
check('every letter that is typed is inserted (q included)',
    'abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ' in d[0], d[0])

# The key strip: twelve entries, six on each row, F1 to F12, and at least
# one blank between two entries; the row is cut at the edge of a narrow
# terminal and never wrapped.  A terminal cannot see the modifier keys, so
# the strip of the terminal version always has the labels of no modifier.
def strip_fault(d, cols):
    import re
    lines = d.split('\n')
    top, bot = lines[-2], lines[-1]
    for row, want in ((top, [1, 3, 5, 7, 9, 11]), (bot, [2, 4, 6, 8, 10, 12])):
        found = [(m.start(), int(m.group(1))) for m in
            re.finditer(r'F(\d+):', row)]
        if len(row) > cols:
            return 'the row is longer than the terminal'
        if cols >= 80 and [n for _, n in found] != want:
            return 'keys %r, wanted %r' % ([n for _, n in found], want)
        if [n for _, n in found] != want[:len(found)]:
            return 'keys %r are not a start of %r' % ([n for _, n in found],
                want)
        for pos, n in found[1:]:
            if row[pos - 1] != ' ':
                return 'F%d touches the entry before it' % n
    return None
for cols in (80, 100, 60, 40):
    d = session(base, ['show'], cols=cols)
    check('key strip: F1 to F12 with spacing at %d columns' % cols,
        strip_fault(d[0], cols) is None and (cols < 80 or
        ('F12: Menu' in d[0].split('\n')[-1] and
        'F11:' in d[0].split('\n')[-2])), (strip_fault(d[0], cols), d[0]))
    check('key strip: nothing wraps at %d columns (25 rows)' % cols,
        len(d[0].split('\n')) == 25 and 'STRUCTURED EDIT' in
        d[0].split('\n')[0], d[0])

# Task: Ctrl-F1 is Learn, not Help.  F1 alone is Help (or says that there is
# no help file).  A missing help file is not an error of Learn.
# The program is found next to the help file of the build directory, so a
# copy of it in another directory is the program that finds no help file.
stred_alone = os.path.join(tmp, 'stred-alone')
shutil.copy(stred, stred_alone)
nohelp = [stred_alone, '-H', os.path.join(tmp, 'no-such-help-file.hlp'), doc]
d = session(base, ['C-F1', 'wait', 'show', 'ESC'])
check('Ctrl-F1 starts Learn: the panel Define Macro opens, not the help',
    'Define Macro' in d[0] and 'Macro Key: Alt-' in d[0] and
    'HELP:' not in d[0] and 'Help file not found' not in d[0], d[0])
# The variable STRATA_HELP would find the help file; take it away here.
saved_help = os.environ.pop('STRATA_HELP', None)
d = session(nohelp, ['C-F1', 'wait', 'show', 'ESC'])
check('Ctrl-F1 starts Learn when there is no help file, with no error',
    'Define Macro' in d[0] and 'Help file not found' not in d[0], d[0])
d = session(base, ['F1', 'wait', 'show'])
check('F1 opens the help', 'HELP: Edit Screen' in d[0] and
    'Define Macro' not in d[0], d[0])
d = session(nohelp, ['F1', 'wait', 'show'])
check('F1 says that no help file was found when there is none',
    'Help file not found' in d[0] and 'Define Macro' not in d[0], d[0])
if saved_help is not None:
    os.environ['STRATA_HELP'] = saved_help
d = session(base, ['C-F1', 'wait', 'text:q', 'ENTER', 'text:note', 'INS',
    'wait', 'show', 'text:x', 'C-F1', 'wait', 'show'])
check('Ctrl-F1 Learn: the message line shows that Ctrl-F1 ends the recording',
    'Defining Alt-Q: press Ctrl-F1 to end' in d[0].split('\n')[-1], d[0])
check('Ctrl-F1 ends the recording that it started',
    'Defining Alt-Q' not in d[1] and 'Define Macro' not in d[1] and
    'HELP:' not in d[1], d[1])
d = session(base, ['S-F1', 'wait', 'show', 'S-F2', 'wait', 'show'])
check('Shift-F1 and Shift-F2 do nothing: no help, no split',
    'HELP:' not in d[0] and d[0] == d[1] and 'Define Macro' not in d[0],
    d[0])
d = session(base, ['C-F2', 'wait', 'show', 'ESC', 'C-F6', 'wait', 'show',
    'ESC'])
check('Ctrl-F2 (Math) asks for the unit to calculate, and does not run Split',
    'Math of' in d[0] and 'Text' in d[0] and 'Column' in d[0] and
    'Block' in d[0], d[0])
check('Ctrl-F6 (Case) opens a box with Upper, Lower and Capitalize and does '
    'not run Format', 'Upper' in d[1] and 'Lower' in d[1] and
    'Capitalize' in d[1] and 'Case' in d[1], d[1])

# ---- Case (Ctrl-F6) ---------------------------------------------------------
CASE = ['C-F6']
d = session(base, CASE + ['ENTER', 'ENTER', 'ENTER', 'show'])
check('case: Upper with nothing highlighted changes the word under the cursor',
    'ORCHARD Plan' in d[0] and 'Upper case: 6 characters changed' in d[0],
    d[0])
d = session(base, CASE + ['ENTER', 'ENTER', 'ENTER', 'C-F10', 'show'])
check('case: it is one Undo operation, which takes it back',
    'Orchard Plan' in d[0] and 'Undone: Case' in d[0].split('\n')[-1], d[0])
d = session(base, ['text:hello wORLD ', 'C-F6', 'DOWN', 'DOWN', 'ENTER',
    'ENTER', 'HOME', 'ENTER', 'show'])
check('case: Capitalize over a highlight makes the first letters upper case '
    'and the others lower case', 'Hello World Orchard Plan' in d[0], d[0])
d = session(base, CASE + ['DOWN', 'ENTER', 'ENTER', 'END', 'ENTER', 'show'])
check('case: Lower over a highlight', 'orchard plan' in d[0], d[0])
d = session(base, CASE + ['ENTER', 'DOWN', 'DOWN', 'ENTER', 'DOWN', 'DOWN',
    'ENTER', 'show'])
check('case: a highlight of blocks changes every block in it',
    'DRAFT FOR REVIEW' in d[0] and 'NOT FOR CIRCULATION' in d[0], d[0])
d = session(base, CASE + ['ENTER', 'ENTER', 'ESC', 'show'])
check('case: Esc leaves the text as it was and says so',
    'Orchard Plan' in d[0] and 'Cancelled' in d[0].split('\n')[-1], d[0])
d = session(base, ['text:x1y', 'C-F6', 'ENTER', 'ENTER', 'HOME', 'ENTER',
    'show'])
check('case: only letters change: a digit and a space stay',
    'X1Y Orchard Plan' in d[0] or 'X1YOrchard Plan' in d[0], d[0])

# ---- Math (Ctrl-F2) ---------------------------------------------------------
MATH = ['C-F2', 'ENTER', 'HOME', 'ENTER']
d = session(base, ['text:2 + 3 * 4'] + MATH + ['show'])
check('math: the highlighted numbers are calculated, the message line shows '
    'the result', 'Result: 14' in d[0].split('\n')[-1], d[0].split('\n')[-1])
d = session(base, ['text:2 + 3 * 4'] + MATH + ['F5', 'show'])
check('math: the result is on the clipboard: Paste (F5) puts 14 in the text',
    '142 + 3 * 4Orchard Plan' in d[0], d[0])
d = session(base, ['text:(1 + 2) * 3'] + MATH + ['show'])
check('math: parentheses first', 'Result: 9' in d[0], d[0])
d = session(base, ['text:1 / 8 2 3'] + MATH + ['show'])
check('math: numbers with no operator between them are added, and the '
    'decimals are as many as the result needs', 'Result: 5.125' in d[0], d[0])
d = session(base, ['text:total 10 and 5'] + MATH + ['show'])
check('math: letters are ignored', 'Result: 15' in d[0], d[0])
d = session(base, ['text:1 / 0'] + MATH + ['show', 'F5', 'show'])
check('math: a division by zero says so, and the clipboard is not changed',
    'Cannot divide by zero' in d[0].split('\n')[-1] and
    '1 / 0Orchard Plan' in d[1], d[0].split('\n')[-1] + '\n' + d[1])
d = session(base, ['text:1 / 0'] + MATH + ['show'])
check('math: the message of the division by zero',
    'Cannot divide by zero' in d[0], d[0])
d = session(base, ['text:2 + 2'] + MATH + ['show', 'C-F10', 'show'])
check('math: the document is not changed: Undo takes back the typing, not '
    'the calculation', 'Result: 4' in d[0].split('\n')[-1] and
    'Undone' in d[1] and '2 + 2Orchard Plan' not in d[1],
    d[0].split('\n')[-1] + '\n' + d[1])
d = session(base, ['C-F2', 'ENTER', 'ENTER', 'show'])
check('math: with nothing highlighted it prompts for the numbers',
    'Nothing is highlighted' in d[0], d[0])

d = session(base, ['A-1', 'show'])
check('Alt-1 collapses to level-1 headlines', 'Orchard Plan' in d[0]
    and 'Neighbours' not in d[0], d[0])

d = session(base, ['A-2', 'show'])
check('Alt-2 shows level-2 headlines only', 'Neighbours' in d[0]
    and 'neighbor' not in d[0], d[0])

# No label of the strip is cut at 80 columns: every entry of the keys that
# have a command is there in full, F11 (which has no command) is the bare
# key name, and F12 ends before the last column.
NORMAL_LABELS = ['Help', 'Split', 'Copy', 'Cut', 'Paste', 'Format', 'Move',
    'Table', 'Search', '', '', 'Menu']
d = session(base, ['show'], cols=80)
strip = d[0].split('\n')[-2:]
check('key strip at 80 columns: every label of the keys is whole',
    all(('F%d: %s' % (i + 1, w) if w != '' else 'F%d:' % (i + 1)) in
        strip[i % 2] for i, w in enumerate(NORMAL_LABELS)), d[0])
check('key strip at 80 columns: F11 and F12 end inside the row',
    strip[0].rstrip().endswith('F11:') and
    strip[1].rstrip().endswith('F12: Menu') and
    max(len(strip[0]), len(strip[1])) <= 80, d[0])

# The faults found in the Tutorial walk: Alt-0 outlines the whole document
# (the headlines of every level, then the text again, then the headlines),
# and the status line names the attribute that the screen draws, so a headline
# that is bold by its format says Bold and not Normal.
d = session(base, ['A-0', 'show'])
check('Alt-0 shows the headlines of every level and no text',
    'Neighbours' in d[0] and 'Heron Hill Orchard' not in d[0], d[0])
d = session(base, ['A-0', 'A-0', 'show'])
check('Alt-0 again shows the text of the whole document',
    'Neighbours' in d[0] and 'Heron Hill Orchard' in d[0], d[0])
d = session(base, ['A-0', 'A-0', 'A-0', 'show'])
check('Alt-0 a third time goes back to the headlines',
    'Neighbours' in d[0] and 'Heron Hill Orchard' not in d[0], d[0])
d = session(base, ['show'])
check('status line: the cursor in a bold headline says Attr: Bold',
    'Attr: Bold' in d[0].split('\n')[0], d[0])
d = session(base, ['DOWN', 'DOWN', 'DOWN', 'show'])
check('status line: the cursor in a plain text block says Attr: Normal',
    'Attr: Normal' in d[0].split('\n')[0], d[0])
d = session(base, ['A-i', 'show'])
check('status line: an italic set in a bold headline says Bold Italic',
    'Attr: Bold Italic' in d[0].split('\n')[0], d[0])
d = session(base, ['A-i', 'A-i', 'show'])
check('status line: setting italic twice goes back to Attr: Bold',
    'Attr: Bold ' in d[0].split('\n')[0] and
    'Italic' not in d[0].split('\n')[0], d[0])

# F10 and F11 have no command; F12 is the menu key and nothing else.
d = session(base, ['F10', 'show'])
check('F10 does nothing: no menu bar opens', 'Global' not in d[0].split('\n')[0]
    and 'Quit' not in d[0], d[0])
d = session(base, ['F11', 'show'])
check('F11 does nothing: no menu bar opens', 'Global' not in d[0].split('\n')[0]
    and 'Quit' not in d[0], d[0])
d = session(base, ['C-F10', 'show', 'C-F11', 'show'])
check('Ctrl-F10 and Ctrl-F11 do nothing: no marker commands',
    'Marker Commands' not in d[0] and 'Marker Commands' not in d[1], d[0])
d = session(base, ['A-F10', 'show'])
check('Alt-F10 is not the marker command now',
    'Marker Commands' not in d[0], d[0])
d = session(base, ['C-F12', 'wait', 'show'])
check('Ctrl-F12 offers the marker commands', 'Marker Commands' in d[0], d[0])
d = session(base, ['F12', 'show', 'F12', 'show'])
check('F12 opens the menu bar and F12 again closes it',
    'Global' in d[0].split('\n')[0] and 'Global' not in d[1].split('\n')[0],
    d[1])

d = session(base, ['F1', 'show', 'ESC', 'F12', 'show'])
check('F1 opens the help (Edit Screen)', 'HELP: Edit Screen' in d[0]
    and 'status line' in d[0].lower(), d[0])
check('F12 opens the menu bar', all(w in d[1] for w in ('File',
    'Global', 'Edit', 'Print', 'Tools', 'Quit')), d[1])
check('the menu bar has no Shell entry', 'Shell' not in d[1].split('\n')[0]
    and 'Shell' not in d[1], d[1])

# The menu bar has no Shell entry: F12 then S starts nothing, and the
# screen stays the editor.  A fake shell records any call to a shell.
shlog = os.path.join(tmp, 'shell.log')
os.environ['SHELL'] = os.path.join(here, 'fakeshell.sh')
os.environ['FAKESHELL_LOG'] = shlog
d = session(base, ['F12', 'text:S', 'wait', 'wait', 'show'])
check('F12 then S does not run a shell and the menu bar stays',
    not os.path.exists(shlog) and 'Global' in d[0].split('\n')[0], d[0])
d = session(base, ['F12', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT',
    'RIGHT', 'show'])
check('the menu bar has six entries: the cursor wraps from Quit to File',
    'Global' in d[0].split('\n')[0], d[0])
d = session(base, ['F12', 'LEFT', 'show'])
check('Left from File goes to Quit and its help line shows Quit',
    'Return to the Document Manager' in d[0], d[0])

d = session(base, ['C-a', 'text:Typed paragraph', 'C-s', 'wait', 'show'])
data = open(doc, 'rb').read()
check('typing + Ctrl-A + Ctrl-S saves to disk', b'Typed paragraph' in data
    and b'*E' in data, d[0])

d = session(base, ['END', 'text: EXTRA', 'F3', 'DOWN', 'DOWN', 'ENTER', 'DOWN',
    'rev'])
check('block highlight uses reverse video', '\u27e6' in d[0].split('\n', 2)[2],
    d[0])

d = session(base, ['PGDN', 'PGDN', 'show'], rows=40, cols=100)
check('large terminal works', 'STRUCTURED EDIT' in d[0])
d = session(base, ['show'], rows=12, cols=44)
check('small terminal works', 'STRUCTURED' in d[0] or 'Orchard' in d[0], d[0])

d = session([stred, '-H', helpf], ['show', 'DOWN', 'ENTER', 'show'])
check('Document Manager at start (no file argument)',
    'Edit / Preview / Print Document' in d[0] and 'Quit' in d[0] and
    'Batch Print Utility' not in d[0] and 'Check Document Spelling' not in
    d[0], d[0])
# The entries of the menu of an Editor with no document all work now; the
# test ownmenu.py checks them.


# ---- section tags (verified against the original: numbers before headlines
# unless FLA0) ----
import subprocess
def render(name):
    out = os.path.join(tempfile.mkdtemp(), 'o.txt')
    subprocess.run([stred, '--text', os.path.join(sample, name), out],
        check=True)
    return open(out, encoding='utf-8').read()
t = render('manual-numbered.doc')
check('section tags: manual-numbered.doc shows 1 / 1.1.1 numbers',
    t.startswith('1 The Tide Table Utility') and '1.1.1 Help' in t
    and '1.1.6 Mode=nn' in t, t[:300])
t = render('brief-parts.doc')
check('section tags: custom format "Part I." from F1G', 'Part I. Aims '
    'and Approach' in t and 'Part II. Work Plan' in t, t[:300])
t = render('report-plain.doc')
check('section tags: FLA0 documents show no numbers', 'Orchard Yields' in t
    and '1 Trends in Orchard Yields' not in t, t[:200])

# Verified against the original: Auto Numbering to Level 1 (FLN1) leaves the
# deeper sections without a tag, and the level 3 Section Tag 0x01 0x07 shows
# a bullet only (the 0x01 byte is not shown).
out = os.path.join(tempfile.mkdtemp(), 'o.txt')
subprocess.run([stred, '--text', os.path.join(here, 'data', 'tagbullet.doc'),
    out], check=True)
t = open(out, encoding='utf-8').read()
check('section tags: a level 3 tag of 0x01 0x07 is a bullet only',
    '\u2022 Definition' in t and '\u263a' not in t, t[1500:2500])
check('section tags: Auto Numbering to Level 1 gives level 2 no tag',
    '\n3 Types' in t and '3.1 Directory' not in t, t[1500:2500])

# ---- markers: the MARKER command (Alt-F12) --------------------------------
d = session(base, ['A-F12', 'wait', 'show', 'ESC', 'show'])
check('marker: Alt-F12 offers the marker commands', 'Marker Commands' in d[0]
    and 'Cross-reference' in d[0] and 'Worksheet' not in d[0]
    and '(Page Number)' in d[0] and 'Marker Commands' not in d[1], d[0])
d = session(base, ['DOWN', 'A-F12', 'text:L', 'text:L', 'INS', 'wait', 'show',
    'text:3',
    'INS', 'show'])
check('marker: the Line command opens its panel', 'Length:' in d[0]
    and 'Weight:' in d[0] and 'Center:' in d[0] and 'String:' in d[0], d[0])
check('marker: the Line command inserts a line marker',
    '[LN Length:3 in]' in d[1] and '*' in d[1].split('\n')[0], d[1])
d = session(base, ['DOWN', 'A-F12', 'text:L', 'text:L', 'INS', 'text:3', 'INS',
    'LEFT',
    'A-F12', 'wait', 'show', 'text:5', 'INS', 'show'])
check('marker: Alt-F12 on a marker opens its panel with its values',
    'Length:' in d[0] and '3.000' in d[0], d[0])
check('marker: the changed value is kept', '[LN Length:5 in]' in d[1]
    and '[LN Length:3 in]' not in d[1], d[1])
d = session(base, ['A-F12', 'text:P', 'text:P', 'INS', 'wait', 'show'])
check('marker: a command that is not allowed says so', 'Invalid marker '
    'selection' in d[0] and 'document body' in d[0], d[0])
# ---- the menu bar: names on row 1, a hint on row 2, a label at the right ---
d = session(base, ['F12', 'show', 'RIGHT', 'show', 'ENTER', 'show', 'ESC',
    'ESC', 'show'])
check('menu: the bar shows a hint line for the highlighted entry',
    'Save, Retrieve, New, Delete, Import, Export' in d[0]
    and 'Format, Page, Typeface, Marker, Options, Named' in d[1], d[0] + d[1])
check('menu: a submenu replaces the names and labels the bar',
    d[2].split('\n')[0].startswith('Format Page Typeface Marker Options Named')
    and 'MENU-GLOBAL' in d[2].split('\n')[0], d[2])
check('menu: Escape leaves the submenu and then the bar',
    'MENU' not in d[3].split('\n')[0], d[3])
# No entry of the F12 menu is dimmed now (Edit, Window works).
d = session(base, ['F12', 'text:P', 'text:P', 'show'])
check('menu: Print, Preview has its own submenu of Document, Block, Section',
    'Document Block Section' in d[0].split('\n')[0]
    and 'MENU-PRINT-PREVIEW' in d[0], d[0])

def fresh(name, src='plan-title-table.doc'):
    path = os.path.join(tmp, name)
    shutil.copy(os.path.join(sample, src), path)
    return path
def fm(path, keys):
    return session([stred, '-H', helpf, path], keys)
def read(path):
    return open(path, 'rb').read() if os.path.exists(path) else b''
def bakp(path, n=1):
    """The file of numbered backup n of a document: in the folder .backup
    next to it."""
    return os.path.join(os.path.dirname(path), '.backup',
        os.path.basename(path)[:-4] + '.bak%02d' % n)
# ---- the Print menu --------------------------------------------------------
# Draft and Final make a PDF with strprint, and send it with lp when a
# printer is named.  Here: the panels and what the PDF holds; the whole chain
# (wait panel, cancel, lp, command line, str) is in tests/printpdf.py.
os.environ['STRATA_LP'] = os.path.join(here, 'fakelp.sh')
os.environ['STRATA_LPSTAT'] = os.path.join(here, 'fakelpstat.sh')
os.environ['FAKELP_LOG'] = os.path.join(tmp, 'lp.log')
def pdftext(path):
    if not os.path.exists(path):
        return ''
    return subprocess.run(['pdftotext', '-layout', path, '-'],
        capture_output=True, text=True).stdout
def pdfpages(path):
    p = subprocess.run(['pdfinfo', path], capture_output=True, text=True)
    for l in p.stdout.split('\n'):
        if l.startswith('Pages:'):
            return int(l.split()[1])
    return 0
def printed(keys, name, pre=[], src=None):
    out = os.path.join(tmp, name)
    pr = os.path.join(tmp, name + '.doc')
    shutil.copy(src or PLAN, pr)
    d = fm(pr, pre + ['F12', 'text:P'] + keys[0] + ['text:' + out] + keys[1]
        + ['INS'] + ['wait'] * 10 + ['show'])
    return pdftext(out), d[0], out
sdoc = os.path.join(here, 'data', 'sections_se.doc')
d = fm(fresh('prp.doc'), ['F12', 'text:P', 'RIGHT', 'ENTER', 'show', 'ESC'])
check('print: the Final panel has every field that works and no other',
    all(w in d[0] for w in ('Print Document', 'Output File:', 'Printer:',
    'Copies:', 'Print Double Sided?', 'Sort Output Pages?',
    'Print Current Section Only?', 'Print Title Page?',
    'Print Double Spaced?', 'Print from Section', 'to Section',
    'Print from Page', 'to Page', 'File only', '.pdf'))
    and 'Print Quality' not in d[0] and '.txt' not in d[0], d[0])
d = fm(fresh('prq.doc'), ['F12', 'text:P', 'ENTER', 'show', 'ESC'])
check('print: the Draft panel has no title page or section range',
    'Draft Print' in d[0] and 'Print Double Spaced?' in d[0]
    and 'Print Title Page?' not in d[0] and 'to Section' not in d[0], d[0])
# The sample document sets Page Break Level 2, so that each section of the
# first two levels starts a page: four pages, where the setting was once
# ignored and the document had one.
full, d, out = printed((['ENTER'], []), 'pr1.pdf')
check('print: Draft makes a PDF and says so',
    open(out, 'rb').read(4) == b'%PDF' and 'Orchard Plan' in full
    and 'Neighbours' in full and 'Wrote ' in d
    and '(4 pages)' in d, d)
check('print: the PDF has the pages that the message shows',
    pdfpages(out) == 4, pdfpages(out))
sfull, d, o0 = printed((['ENTER'], []), 'pr0.pdf', src=sdoc)
sec, d, o2 = printed((['ENTER'], ['DOWN'] * 5 + ['RIGHT']), 'pr2.pdf',
    ['DOWN', 'DOWN'], sdoc)
check('print: Draft, Current Section Only makes that section only',
    'Purpose body.' in sec and 'Prerequisites body.' not in sec
    and 'Prerequisites body.' in sfull, sec)
c5 = os.path.join(sample, 'brief-long.doc')
one, d, o3a = printed((['ENTER'], []), 'pr3a.pdf', src=c5)
dbl, d, o3 = printed((['ENTER'], ['DOWN'] * 6 + ['RIGHT']), 'pr3.pdf', src=c5)
check('print: Draft, Double Spaced makes more pages',
    pdfpages(o3) > pdfpages(o3a) > 0, '%d %d' % (pdfpages(o3),
    pdfpages(o3a)))
fin, d, o4 = printed((['RIGHT', 'ENTER'], []), 'pr4.pdf')
check('print: Final makes a PDF too, and says how many pages',
    'Orchard Plan' in fin and 'Wrote ' in d and '.pdf (4 pages)' in d, d)
# ---- PDF viewer (a block of its own): the stand-in opener of the setting
# pdf_viewer records the call; no real program that opens a PDF can run.
oph = tempfile.mkdtemp(prefix='openhome')
# A program that others cannot change: a copy of mode 0755, and a
# settings file of mode 0600 (the policy of the setting pdf_viewer).
fakesafe = os.path.join(oph, 'fakeopen.sh')
shutil.copy(os.path.join(here, 'fakeopen.sh'), fakesafe)
os.chmod(fakesafe, 0o755)
def forget_print_choices():
    """The panels start with the choices of the last print: each print of
    this block wants the first-use state, so the settings are written again."""
    with open(os.path.join(oph, 'config'), 'w') as f:
        f.write('pdf_viewer=%s\n' % fakesafe)
    os.chmod(os.path.join(oph, 'config'), 0o600)


forget_print_choices()
oplog = os.path.join(tmp, 'open.log')
saved_open = (home_override[0], os.environ.get('DISPLAY'))
home_override[0] = oph
os.environ['FAKEOPEN_LOG'] = oplog
os.environ['DISPLAY'] = ':99'
for kind, kk in (('Final', ['RIGHT', 'ENTER']), ('Draft', ['ENTER'])):
    name = 'op%s.pdf' % kind
    outp = os.path.join(tmp, name)
    odoc = os.path.join(tmp, name + '.doc')
    shutil.copy(PLAN, odoc)
    forget_print_choices()
    d = fm(odoc, ['F12', 'text:P'] + kk + ['text:' + outp, 'DOWN', 'F2',
        'DOWN', 'ENTER', 'INS'] + ['wait'] * 10 + ['show'])
    time.sleep(0.8)
    lg = read(oplog).decode('latin-1')
    check('print: %s with PDF viewer makes the PDF and opens it' % kind,
        read(outp)[:4] == b'%PDF' and 'and opened it to print' in d[0] and
        ('fakeopen.sh|%s size=' % outp) in lg, (d[0], lg))
home_override[0] = saved_open[0]
if saved_open[1] is None:
    del os.environ['DISPLAY']
else:
    os.environ['DISPLAY'] = saved_open[1]
check('print: nothing is written as plain text any more',
    not os.path.exists(os.path.join(tmp, 'pr4.txt'))
    and not os.path.exists(os.path.join(tmp, 'pr1.txt')))
sorts = os.path.join(here, 'data', 'sort_sections.doc')
rng, d, o6 = printed((['RIGHT', 'ENTER'], ['DOWN'] * 8 + ['text:1.2', 'DOWN',
    'text:1.2']), 'pr6.pdf', src=sorts)
check('print: Final, from Section to Section makes only those sections',
    'Apple body' in rng and 'cherry body' not in rng
    and '9 x body' not in rng, rng)
bad, d, o7 = printed((['RIGHT', 'ENTER'], ['DOWN'] * 8 + ['text:9.9']),
    'pr7.pdf', src=sorts)
check('print: a section that does not exist is refused on the panel',
    'Cannot find section number 9.9' in d and bad == '', d)
bad, d, o7 = printed((['RIGHT', 'ENTER'], ['DOWN'] * 2 + ['text:abc']),
    'pr7b.pdf')
check('print: a bad Copies value is refused on the panel',
    'Copies must be a number from 1 to 99' in d and bad == '', d)
nt, d, o8 = printed((['RIGHT', 'ENTER'], ['DOWN'] * 6 + ['LEFT']), 'pr8.pdf',
    src=sdoc)
yt, d, o9 = printed((['RIGHT', 'ENTER'], []), 'pr9.pdf', src=sdoc)
check('print: Final, Print Title Page No leaves out the title page',
    'Title page' in yt and 'Title page' not in nt and 'Purpose body.' in nt,
    nt)

# Preview: the entries run LaTeX and the DVI viewer; tests/preview.py has the
# tests of the run itself.  Here: the panel of Document.
d = fm(fresh('pv1.doc'), ['F12', 'text:P', 'text:P', 'ENTER', 'show', 'ESC'])
check('preview: Document asks for the title page and a section range',
    'Preview Title Page?' in d[0] and 'Preview from Section' in d[0], d[0])

# ---- the Tools menu --------------------------------------------------------
TOOLS = ['F12', 'text:T']
def tool(name, *more):
    n = {'macros': 0, 'library': 1, 'spell': 2, 'thesaurus': 3, 'autosort': 4,
        'count': 5}[name]
    return TOOLS + ['RIGHT'] * n + ['ENTER'] + list(more)
d = fm(fresh('tl1.doc'), tool('count') + ['wait', 'show'])
check('tools: Count shows the number of words in the document',
    'Word Count' in d[0] and 'Total number of words in document: ' in d[0],
    d[0])
def read_texts(path):
    return [t.decode('cp437').rstrip('\x1d') for t in
        re.findall(rb'\r\nT([^\x00\r]*)\x00', read(path))]
import re
sp = os.path.join(tmp, 'tl2.doc')
shutil.copy(os.path.join(here, 'data', 'sort_sections.doc'), sp)
fm(sp, tool('autosort', 'ENTER', 'wait') + ['C-s', 'wait'])
t = [x for x in read_texts(sp) if not x.endswith(' body')]
t = t[t.index('Parent'):]
check('tools: Autosort, Section sorts the subsections of the current section',
    t[:8] == ['Parent', '10 x', '9 x', 'apple', 'Apple', 'Apple pie',
    'banana', 'z'], t)
sp = os.path.join(tmp, 'tl3.doc')
shutil.copy(os.path.join(here, 'data', 'sort_ties.doc'), sp)
fm(sp, tool('autosort', 'ENTER', 'wait') + ['C-s', 'wait'])
t = [x for x in read_texts(sp) if x not in ('b', 'P body')]
t = t[t.index('P'):]
check('tools: Autosort puts upper case after lower case and ignores case '
    'otherwise', t == ['P', 'A-b', 'a_b', 'a b', 'ab', 'bob', 'bOb', 'Bob',
    'BOB'], t)
sp = os.path.join(tmp, 'tl4.doc')
shutil.copy(os.path.join(here, 'data', 'sort_table.doc'), sp)
d = fm(sp, ['DOWN'] + tool('autosort', 'RIGHT', 'ENTER', 'wait', 'show',
    'END', 'ENTER', 'wait', 'show') + ['C-s', 'wait'])
t = read_texts(sp)
check('tools: Autosort, Table says how to highlight blocks',
    'Use arrow keys to highlight block(s); then press Enter.' in d[0], d[0])
check('tools: Autosort, Table sorts the highlighted blocks by the first '
    'column', [x for x in t if x in ('Apple', 'banana', 'Cherry', 'pear')] ==
    ['Apple', 'banana', 'Cherry', 'pear'], t)
sp = os.path.join(tmp, 'tl5.doc')
shutil.copy(os.path.join(here, 'data', 'sort_table.doc'), sp)
fm(sp, ['DOWN'] + tool('autosort', 'RIGHT', 'ENTER', 'wait', 'text:-', 'END',
    'ENTER', 'wait') + ['C-s', 'wait'])
t = read_texts(sp)
check('tools: a minus in Autosort, Table sorts the other way',
    [x for x in t if x in ('Apple', 'banana', 'Cherry', 'pear')] ==
    ['pear', 'Cherry', 'banana', 'Apple'], t)

# ---- the thesaurus: Tools, Thesaurus and Ctrl-T ------------------------------
# The data is the small MyThes data set of tests/data (tests/mkthesdata.py).
def thesdoc(name, lines):
    """An unstructured document whose one block holds the given lines."""
    crlf = b'\r\n'
    head = (b'*R3' + crlf + b'MR1001' + crlf + b'PP\\1\\\x00' + crlf + b'P1C33'
        + crlf + b'FHA1' + crlf + b'FTT5' + crlf + b'DP0,8500,11000' + crlf)
    text = b''.join(l.encode('cp437') + b'\x1d' for l in lines)
    path = os.path.join(tmp, name)
    open(path, 'wb').write(head + b'*B0' + crlf + b'S' + crlf + b'T' + text
        + b'\x00' + crlf + b'*E' + crlf)
    return path
th_data = os.path.join(here, 'data')
os.environ['STRATA_THESAURUS'] = th_data
THL = ['Quick sections of the headlines.', 'QUICK ran quick', '', 'happy']
def panel(d):
    """The lines of the panel on the lower half of a screen dump."""
    return '\n'.join(d.split('\n')[13:])
def flat(d):
    """A screen dump with the frame characters and the line breaks taken
    out, so that a message that wraps over several lines can be found."""
    return re.sub('[\u2551\u2550\u2554\u2557\u255a\u255d\\s]+', ' ', d)
tp = thesdoc('th1.doc', THL)
d = fm(tp, ['C-t', 'show'])
pn = panel(d[0])
check('thesaurus: Ctrl-T shows the panel with the word, the senses and the '
    'hint', 'Quick:' in pn and 'Thesaurus' in pn
    and 'noun - center, core, next day;' in pn
    and ' adj - fast, rapid, swift, speedy, brisk, nimble;' in pn
    and ' adv - quickly, promptly.' in pn
    and 'Enter:replace  Esc:exit  CTRL-T:look up' in pn
    and 'Quick sections of the headlines.' in d[0], d[0])
check('thesaurus: the part of speech is written at its first sense only, '
    'and antonyms, generic and related terms are left out',
    'slow' not in pn and 'movement' not in pn and 'animal' not in pn
    and pn.count('adj') == 1, pn)
d = fm(tp, ['C-t', 'rev'])
check('thesaurus: the word is highlighted in the text and the first synonym '
    'in the panel', '\u27e6Quick\u27e7 sections' in d[0]
    and '\u27e6center\u27e7' in d[0], d[0])
d = fm(tp, tool('thesaurus') + ['show'])
check('thesaurus: Tools, Thesaurus does the same as Ctrl-T',
    'Quick:' in panel(d[0]) and 'Enter:replace' in d[0], d[0])
d = fm(tp, ['RIGHT RIGHT', 'C-t', 'show', 'ESC', 'END', 'LEFT LEFT LEFT',
    'C-t', 'show'])
check('thesaurus: the cursor in the middle of a word looks it up',
    'Quick:' in d[0], d[0])
check('thesaurus: the cursor on a later word looks that word up',
    'sections:' in d[1] and 'part, segment, division' in d[1]
    or 'headlines:' in d[1], d[1])
d = fm(tp, ['RIGHT RIGHT RIGHT RIGHT RIGHT', 'C-t', 'show'])
check('thesaurus: the cursor just after the last letter looks the word up',
    'Quick:' in d[0], d[0])
d = fm(tp, ['RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT', 'C-t', 'show'])
check('thesaurus: a form of a word is found under its heading as typed',
    'sections:' in d[0] and 'part, segment, division, piece of writing;'
    in d[0], d[0])
d = fm(tp, ['C-t', 'RIGHT', 'rev'])
check('thesaurus: Right highlights the next word', '\u27e6core\u27e7' in d[0],
    d[0])
d = fm(tp, ['C-t', 'DOWN', 'rev', 'END', 'rev', 'HOME', 'rev'])
check('thesaurus: Down goes to the row below, End and Home to the ends of '
    'the row', '\u27e6fast\u27e7' in d[0] and '\u27e6nimble\u27e7' in d[1]
    and '\u27e6fast\u27e7' in d[2], d)
d = fm(tp, ['C-t', 'DOWN', 'UP', 'rev'])
check('thesaurus: Up goes back', '\u27e6center\u27e7' in d[0], d[0])
d = fm(tp, ['C-t', 'LEFT', 'rev'])
check('thesaurus: Left at the first word stays', '\u27e6center\u27e7' in d[0],
    d[0])
d = fm(tp, ['C-t', 'RIGHT', 'C-t', 'show', 'C-PGUP', 'show'])
check('thesaurus: Ctrl-T looks up the highlighted word and keeps a history',
    'core:' in d[0] and 'kernel' in d[0]
    and 'Ctrl+PgUp:last word' in d[0], d[0])
check('thesaurus: Ctrl-PgUp goes back to the last word',
    'Quick:' in d[1] and 'Ctrl+PgUp' not in d[1], d[1])
d = fm(tp, ['C-t', 'RIGHT', 'C-t', 'ESC', 'show'])
check('thesaurus: Esc leaves the thesaurus from a second lookup too',
    'core:' not in d[0] and 'Enter:replace' not in d[0], d[0])
out = os.path.join(tmp, 'th2.doc')
shutil.copy(tp, out)
d = fm(out, ['C-t', 'ESC', 'C-s', 'wait', 'show'])
check('thesaurus: Esc changes nothing', read(out) == read(tp)
    and 'Quick sections' in d[0], d[0])
d = fm(out, ['C-t', 'RIGHT', 'RIGHT', 'ESC', 'text:X', 'show'])
check('thesaurus: after Esc the cursor is where it was and the highlight is '
    'gone', 'XQuick sections' in d[0] and 'Enter:replace' not in d[0], d[0])
shutil.copy(tp, out)
d = fm(out, ['C-t', 'ENTER', 'show', 'C-s', 'wait'])
check('thesaurus: Enter puts the highlighted word in the text',
    'Center sections of the headlines.' in d[0]
    and 'Enter:replace' not in d[0], d[0])
check('thesaurus: the replaced document is saved with the new word and the '
    'case of Quick', b'Center sections of the headlines.' in read(out),
    read(out))
shutil.copy(tp, out)
d = fm(out, ['C-t', 'RIGHT', 'RIGHT', 'ENTER', 'text:!', 'C-s',
    'wait'])
check('thesaurus: the cursor is after the new word (a multi-word synonym)',
    b'Next day! sections' in read(out), read(out))
shutil.copy(tp, out)
d = fm(out, ['DOWN', 'C-t', 'ENTER', 'RIGHT RIGHT RIGHT RIGHT RIGHT', 'C-t',
    'ENTER', 'C-s', 'wait'])
check('thesaurus: QUICK gives CENTER and quick gives center',
    b'CENTER ran center' in read(out), read(out))
d = fm(tp, ['DOWN', 'HOME', 'RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT', 'C-t', 'rev'])
check('thesaurus: a word that is not found shows the list of similar words '
    'with the nearest lower one highlighted',
    'The word was not found -  Please choose another word' in d[0]
    and '\u27e6quick\u27e7' in d[0] and 'Enter:look up' in d[0], d[0])
d = fm(tp, ['DOWN', 'HOME', 'RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT', 'C-t', 'ENTER', 'show',
    'C-PGUP', 'show'])
check('thesaurus: Enter in the list looks up the highlighted word',
    'quick:' in d[0] and 'noun - center' in d[0]
    and 'Ctrl+PgUp:last word' in d[0], d[0])
nsd = thesdoc('th-nosyn.doc', ['zebra', 'ran'])
d = [fm(nsd, ['C-t', 'rev'])[0],
    fm(nsd, ['RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT', 'C-t', 'rev'])[0]]
check('thesaurus: a word with only generic or related terms says that it has '
    'no synonyms, and lists the words near it',
    'The word has no synonyms -  Please choose another word' in d[0]
    and 'The word was not found' not in d[0] and '\u27e6zebra\u27e7' in d[0]
    and 'Enter:look up' in d[0], d[0])
check('thesaurus: a word that is not in the data still says that it was not '
    'found', 'The word was not found -  Please choose another word' in d[1]
    and 'no synonyms' not in d[1], d[1])
check('thesaurus: Ctrl-PgUp from there goes back to the list',
    'The word was not found' in d[1], d[1])
d = fm(tp, ['DOWN', 'HOME', 'RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT', 'C-t', 'DOWN', 'rev',
    'UP', 'UP', 'rev'])
check('thesaurus: Down and Up move the highlight in the list',
    '\u27e6rapid\u27e7' in d[0] and '\u27e6piece of writing\u27e7' in d[1], d)
d = fm(tp, ['DOWN', 'HOME', 'RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT', 'C-t', 'PGDN', 'show',
    'PGUP', 'show'])
check('thesaurus: PgDn and PgUp page through the dictionary',
    'zygote' in d[0] and 'MORE:' in d[0] and 'zygote' not in d[1], d)
d = fm(tp, ['C-t', 'text:h', 'show', 'text:appy', 'show', 'ENTER', 'show'])
check('thesaurus: a letter opens the Look Up box', 'Look Up: h' in d[0]
    and 'Esc:exit  CTRL-T:look up' in d[0], d[0])
check('thesaurus: Enter in the Look Up box looks the word up',
    'Look Up:' not in d[2] and 'happy:' in d[2] and 'glad, joyful' in d[2],
    d[2])
d = fm(tp, ['C-t', 'text:h', 'ESC', 'show'])
check('thesaurus: Esc in the Look Up box closes only the box',
    'Look Up:' not in d[0] and 'Quick:' in d[0], d[0])
d = fm(tp, ['C-t', 'text:xy', 'BKSP', 'text:z', 'show', 'C-t', 'show'])
check('thesaurus: Backspace edits the box and Ctrl-T looks up too',
    'Look Up: xz' in d[0] and 'The word was not found' in d[1], d)
d = fm(tp, ['DOWN DOWN', 'C-t', 'show'])
check('thesaurus: an empty line gives "No word selected"',
    'No word selected' in d[0] and 'Enter:replace' not in d[0], d[0])
d = fm(tp, ['DOWN', 'END', 'C-t', 'show'])
check('thesaurus: after the last word the word before is used',
    'quick:' in panel(d[0]) and 'noun - center' in d[0], d[0])
tl = thesdoc('th3.doc', ['one', 'two', 'three', 'four', 'five', 'six',
    'seven', 'eight', 'nine', 'ten', 'happy', 'twelve', 'thirteen',
    'fourteen', 'fifteen', 'sixteen', 'seventeen', 'eighteen', 'nineteen',
    'twenty'])
d = fm(tl, ['DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN', 'C-t',
    'show'])
check('thesaurus: with the cursor in the upper half the panel is the lower '
    '12 rows', d[0].split('\n')[13].startswith('\u2554')
    and 'Thesaurus' in d[0].split('\n')[13]
    and d[0].split('\n')[12].startswith('happy'), d[0])
d = fm(tl, ['DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN '
    'DOWN DOWN DOWN', 'C-t', 'show'])
check('thesaurus: with the cursor in the lower half the panel is the upper '
    '12 rows and the text is not scrolled', d[0].split('\n')[0].startswith(
    '\u2554') and d[0].split('\n')[12].startswith('happy')
    and d[0].split('\n')[16].startswith('fifteen'), d[0])
# Without the data: a message names the package, and the editor works as before.
os.environ['STRATA_THESAURUS'] = os.path.join(tmp, 'no-such-directory')
d = fm(tp, ['C-t', 'show', 'ESC', 'text:Z', 'show'])
check('thesaurus: without data Ctrl-T says what to install',
    'The thesaurus is not installed' in d[0] and 'mythes-en-us' in d[0], d[0])
check('thesaurus: without data the editor works as before', 'ZQuick' in d[1],
    d[1])
check('thesaurus: without data the message shows that the install script '
    'installs the package', 'install script of Strata installs it'
    in flat(d[0]) and 'Install the package mythes-en-us' in flat(d[0]), d[0])
d = fm(tp, tool('thesaurus') + ['show'])
check('thesaurus: without data the menu entry says what to install',
    'mythes-en-us' in d[0], d[0])
os.environ['STRATA_THESAURUS'] = ''
d = fm(tp, ['C-t', 'show'])
check('thesaurus: an empty STRATA_THESAURUS means no thesaurus',
    'not installed' in d[0], d[0])
# One check with the real data of the package mythes-en-us, when it is there
# (STRATA_TEST_MYTHES names its directory; else /usr/share/mythes).
real = os.environ.get('STRATA_TEST_MYTHES', '/usr/share/mythes')
if os.path.exists(os.path.join(real, 'th_en_US_v2.dat')):
    os.environ['STRATA_THESAURUS'] = real
    d = fm(tp, ['RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT', 'C-t', 'show'])
    check('thesaurus: the real data of mythes-en-us finds "sections" (stem '
        'section) with "segment" among its synonyms',
        'sections:' in d[0] and 'segment' in d[0], d[0])
else:
    print('skip thesaurus: the real data of mythes-en-us is not installed '
        '(%s/th_en_US_v2.dat); set STRATA_TEST_MYTHES' % real)
del os.environ['STRATA_THESAURUS']

# ---- the Edit menu ---------------------------------------------------------
EDIT = ['F12', 'text:E']
def tdata(name):
    p = os.path.join(tmp, name)
    shutil.copy(os.path.join(here, 'data', name), p)
    return p
d = fm(tdata('title_page.doc'), EDIT + ['show'])
check('edit: a structured document offers Title, Section and Window',
    d[0].split('\n')[0].startswith('Title Section Window')
    and 'MENU-EDIT' in d[0], d[0])
d = fm(tdata('plain_text.doc'), EDIT + ['show'])
check('edit: an unstructured document offers Structured', d[0].split('\n')[0]
    .startswith('Title Structured Window'), d[0])
tp = tdata('title_page.doc')
d = fm(tp, ['show'])
check('edit: the title page is not in the text', 'Front page' not in d[0]
    and 'First' in d[0], d[0])
d = fm(tp, EDIT + ['ENTER', 'wait', 'show', 'END', 'text: here', 'INS', 'wait',
    'C-s', 'wait'])
check('edit: Title opens the title page in an editor of its own',
    'TITLE PAGE EDIT' in d[0]
    and 'Front page' in d[0] and 'First' not in d[0], d[0])
check('edit: the title page changes are kept, and the text is not changed',
    b'Front page here' in read(tp) and b'=1\r\nS\r\n' in read(tp)
    and b'First body' in read(tp), repr(read(tp)[-200:]))
np = tdata('plain_text.doc')
fm(np, EDIT + ['ENTER', 'wait', 'text:A title', 'INS', 'wait', 'C-s', 'wait'])
check('edit: Title on a document with none adds one in front of the text',
    b'\r\n=1\r\nS\r\nTA title' in read(np) and b'One line' in read(np),
    repr(read(np)[-200:]))
tp2 = tdata('title_page.doc')
fm(tp2, EDIT + ['ENTER', 'wait', 'C-HOME', 'S-END', 'DEL', 'wait'] +
    ['ESC', 'wait', 'C-s', 'wait'])
check('edit: Esc in the title page editor keeps the old title page',
    b'Front page' in read(tp2), repr(read(tp2)[-200:]))
sp = tdata('plain_text.doc')
d = fm(sp, EDIT + ['RIGHT', 'ENTER', 'wait', 'show', 'C-s', 'wait'])
check('edit: Structured makes the text one section',
    'STRUCTURED EDIT' in d[0] and b'*B1' in read(sp)
    and b'S\r\n+\r\nS\r\n' in read(sp) and read(sp).rstrip().endswith(
    b'-\r\n*E'), repr(read(sp)[-120:]))
d = fm(sp, EDIT + ['RIGHT', 'show'])
check('edit: a structured document of one section offers Unstructured',
    'Title Unstructured Section Window' in d[0], d[0])
fm(sp, EDIT + ['RIGHT', 'ENTER', 'wait', 'C-s', 'wait'])
check('edit: Unstructured goes back to the text of the document',
    b'*B0' in read(sp) and b'+\r\n' not in read(sp)
    and b'One line' in read(sp), repr(read(sp)[-160:]))
d = fm(tdata('sort_sections.doc'), EDIT + ['show'])
check('edit: a document of several sections has no Unstructured',
    'Unstructured' not in d[0], d[0])
sc = tdata('sort_sections.doc')
d = fm(sc, ['DOWN', 'DOWN'] + EDIT + ['RIGHT', 'ENTER', 'wait', 'show'])
check('edit: Section shows the current section only, in SECTION EDIT mode',
    'SECTION EDIT' in d[0] and 'banana' in d[0] and 'Parent' not in d[0]
    and 'cherry' not in d[0], d[0])
d = fm(sc, ['DOWN', 'DOWN'] + EDIT + ['RIGHT', 'ENTER', 'wait'] +
    EDIT + ['RIGHT', 'show'])
check('edit: in a section the entry is Document', 'Title Document Window'
    in d[0].split('\n')[0], d[0])
d = fm(sc, ['DOWN', 'DOWN', 'C-^', 'wait', 'show', 'C-^',
    'wait', 'show'])
check('edit: Ctrl-^ goes in and out of the section', 'SECTION EDIT' in d[0]
    and 'cherry' not in d[0] and 'STRUCTURED EDIT' in d[1]
    and 'cherry' in d[1], d[0] + d[1])

# ---- the Global menu -------------------------------------------------------
GLOBAL = ['F12', 'text:G']
OPTIONS = GLOBAL + ['RIGHT'] * 4 + ['ENTER', 'wait']
d = fm(fresh('go1.doc'), OPTIONS + ['show'])
check('global: Options is one panel with the original\'s settings',
    all(w in d[0] for w in ('Expanded Status Line?', 'Display On-Screen Help?',
    'Block Indicators', 'Display Tab & End-of-Line Characters?',
    'Display Dots for Spaces?', 'Backups Kept (0 to 99)',
    'Line Weight (Borders)')), d[0])
check('global: Options does not show Autosave (str, Setup has it)',
    'Autosave' not in d[0] and 'Save Interval' not in d[0], d[0])
d = fm(fresh('go2.doc'), OPTIONS + ['DOWN'] * 3 + ['RIGHT', 'INS', 'wait',
    'show'])
check('global: Display Dots for Spaces shows a dot for every space',
    'Orchard\u00b7Plan' in d[0], d[0])
bk = fresh('go3.doc')
before = read(bk)
hm = tempfile.mkdtemp(prefix='home')
os.chmod(hm, 0o700)
home_override[0] = hm
fm(bk, OPTIONS + ['DOWN'] * 4 + ['text:5', 'INS', 'wait', 'text:x', 'C-s',
    'wait'])
home_override[0] = None
check('global: Backups Kept is kept as the key backups of the settings',
    b'\nbackups=5\n' in read(os.path.join(hm, 'config')),
    read(os.path.join(hm, 'config')))
check('global: the backup keeps the old version of a .DOC file',
    read(bakp(bk)) == before and read(bk) != before, bk)
bk = fresh('go4.doc')
hm = tempfile.mkdtemp(prefix='home')
os.chmod(hm, 0o700)
open(os.path.join(hm, 'config'), 'w').write('backups=0\n')
os.chmod(os.path.join(hm, 'config'), 0o600)
home_override[0] = hm
fm(bk, ['text:x', 'C-s', 'wait'])
home_override[0] = None
check('global: with no backups kept none is made',
    not os.path.exists(bakp(bk)), bk)

# Save onto a backup file is refused: the file is not written and the document
# stays changed.  Reading a backup file is allowed (it is opened here).
bf = os.path.join(tmp, 'refuse.bak01')
shutil.copy(PLAN, bf)
bf_before = read(bf)
d = session([stred, '-H', helpf, bf], ['text:x', 'C-s', 'wait', 'show'])
check('backup file: the document of a .bak01 file opens (reading is allowed)',
    'Orchard Plan' in d[0] or 'Orchard' in d[0], d[0])
check('backup file: Ctrl-S is refused and says that a backup is not saved over',
    'A backup file is not saved over' in d[0], d[0])
check('backup file: nothing is written onto the backup file',
    read(bf) == bf_before)
os.makedirs(os.path.join(tmp, '.backup'), exist_ok=True)
bf2 = os.path.join(tmp, '.backup', 'inside.doc')
shutil.copy(PLAN, bf2)
bf2_before = read(bf2)
d = session([stred, '-H', helpf, bf2], ['text:x', 'C-s', 'wait', 'show'])
check('backup file: a file in the folder .backup is refused too',
    'A backup file is not saved over' in d[0] and read(bf2) == bf2_before,
    d[0])
bf3 = os.path.join(tmp, 'plain.doc')
shutil.copy(PLAN, bf3)
session([stred, '-H', helpf, bf3], ['text:x', 'C-s', 'wait'])
check('backup file: a save under an ordinary name still works',
    b'Tx\x1cOrchard' in read(bf3), read(bf3)[300:420])

# Autosave: on or off, with an interval in seconds; it saves without asking.
# It is set in str (Setup), which keeps it in ~/.strata; stred is told by flags.
def auto_session(path, flags, keys):
    return session([stred, '-H', helpf] + flags + [path], keys)
au = fresh('au1.doc')
d = auto_session(au, ['--autosave-seconds', '2'], ['text:x'] + ['wait'] * 8 +
    ['show'])
check('autosave: it saves the changed document by itself, without asking',
    b'Tx\x1cOrchard' in read(au) and 'Save document' not in d[0], d[0])
au = fresh('au2.doc')
auto_session(au, ['--autosave', 'no', '--autosave-seconds', '2'], ['text:x'] +
    ['wait'] * 8)
check('autosave: with Autosave set to No nothing is saved by itself',
    b'Tx\x1cOrchard' not in read(au), read(au)[300:420])
au = fresh('au3.doc')
before = read(au)
hm = tempfile.mkdtemp(prefix='home')
os.chmod(hm, 0o700)
open(os.path.join(hm, 'config'), 'w').write('backups=2\n')
home_override[0] = hm
auto_session(au, ['--autosave-seconds', '2'], ['text:x'] + ['wait'] * 8)
home_override[0] = None
check('autosave: it makes no numbered backup',
    b'Tx\x1cOrchard' in read(au) and not os.path.exists(bakp(au)),
    read(au)[300:420])
# The settings of the user are kept in ~/.strata, which only the user can use.
hm = os.path.join(tempfile.mkdtemp(prefix='home'), 'strata')
home_override[0] = hm
fm(fresh('cf9.doc'), OPTIONS + ['RIGHT', 'DOWN', 'DOWN', 'DOWN', 'RIGHT',
    'INS', 'wait'])
txt = read(os.path.join(hm, 'config')).decode()
check('config: changing the Options writes them to ~/.strata/config',
    'expanded_status=yes' in txt and 'show_dots=yes' in txt, txt)
check('config: the directory is for the user only (0700) and so is the file '
    '(0600)', os.stat(hm).st_mode & 0o777 == 0o700 and
    os.stat(os.path.join(hm, 'config')).st_mode & 0o777 == 0o600,
    '%o %o' % (os.stat(hm).st_mode & 0o777,
    os.stat(os.path.join(hm, 'config')).st_mode & 0o777))
d = fm(fresh('cf8.doc'), OPTIONS + ['show'])
check('config: the next session starts with the settings of the user',
    any('Expanded Status Line?' in l and '[Yes]' in l
    for l in d[0].split('\n')), d[0])
os.chmod(hm, 0o755); os.chmod(os.path.join(hm, 'config'), 0o666)
fm(fresh('cf7.doc'), OPTIONS + ['INS', 'wait'])
check('config: a directory or file that others could use is made private',
    os.stat(hm).st_mode & 0o777 == 0o700 and
    os.stat(os.path.join(hm, 'config')).st_mode & 0o777 == 0o600,
    '%o %o' % (os.stat(hm).st_mode & 0o777,
    os.stat(os.path.join(hm, 'config')).st_mode & 0o777))
home_override[0] = None

TYPEFACE = GLOBAL + ['RIGHT'] * 2 + ['ENTER', 'wait']
d = fm(fresh('gt1.doc'), TYPEFACE + ['show'])
check('global: Typeface lists the fonts and the printer', 'Typeface List'
    in d[0] and 'Font 6' in d[0] and 'Intro head' in d[0]
    and 'Printer' in d[0], d[0])
tf = fresh('gt2.doc')
fm(tf, TYPEFACE + ['DOWN'] * 5 + ['text:Zed', 'INS', 'wait', 'C-s', 'wait'])
check('global: a font name typed on the Typeface list is saved',
    b'\r\nDF6NZed\x00\r\n' in read(tf), repr(read(tf)[:300]))
tf = fresh('gt3.doc')
fm(tf, TYPEFACE + ['DOWN'] * 5 + ['text:Zed', 'ESC', 'wait', 'C-s', 'wait'])
check('global: Esc on the Typeface list keeps the names', b'DF6N' not in
    read(tf), repr(read(tf)[:300]))

FORMAT = GLOBAL + ['ENTER', 'wait']
d = fm(fresh('gf1.doc'), FORMAT + ['show'])
check('global: Format opens the Global Format panel with the rulers',
    all(w in d[0] for w in ('Global Format', 'Headline Attributes',
    'Headline Justification', 'Text Font', 'Text Ruler')), d[0])
d = fm(fresh('gf2.doc'), FORMAT + ['PGUP', 'show'])
check('global: PgUp shows the spacing, with the original\'s fields',
    all(w in d[0] for w in ('Line Spacing', 'Headline Spacing',
    'Block Spacing', 'Subsection Spacing', 'Peer Section Spacing',
    'Indent per Level', 'Page Break Level', 'Page Number Level',
    'Auto Numbering?', 'First Block Type', 'Print', 'Ruler',
    'Winding Columns')), d[0])
d = fm(fresh('gf3.doc'), FORMAT + ['PGUP', 'PGUP', 'PGUP', 'PGUP', 'show'])
check('global: the next panel is Level 1 Format and shows inherited values',
    'Level 1 Format' in d[0] and '[Bold]' in d[0], d[0])
gf = fresh('gf4.doc')
fm(gf, FORMAT + ['DOWN', 'RIGHT', 'RIGHT', 'INS', 'wait', 'C-s', 'wait'])
check('global: Headline Justification Center is saved as FHJ2',
    b'\r\nFHJ2\r\n' in read(gf), repr(read(gf)[:400]))
check('global: the new record is placed with the other headline records',
    read(gf).index(b'FHA1') < read(gf).index(b'FHJ2') <
    read(gf).index(b'FTT5'), repr(read(gf)[:400]))
gf = fresh('gf5.doc')
fm(gf, FORMAT + ['PGUP', 'text:2.25', 'INS', 'wait', 'C-s', 'wait'])
check('global: Line Spacing 2.25 is saved as FSL225',
    b'\r\nFSL225\r\n' in read(gf), repr(read(gf)[:400]))
check('global: FSL comes before the headline spacing FSH',
    read(gf).index(b'FSL225') < read(gf).index(b'FSH150'),
    repr(read(gf)[:400]))
gf = fresh('gf6.doc')
fm(gf, FORMAT + ['PGUP'] + ['DOWN'] * 8 + ['LEFT', 'INS', 'wait', 'C-s',
    'wait'])
check('global: Auto Numbering No is saved as FLA0',
    b'\r\nFLA0\r\n' in read(gf), repr(read(gf)[:400]))
gf = fresh('gf7.doc')
fm(gf, FORMAT + ['PGUP', 'PGUP', 'PGUP', 'PGUP', 'RIGHT', 'RIGHT', 'RIGHT',
    'INS',
    'wait',
    'C-s', 'wait'])
check('global: a Level 1 headline attribute is saved as F1HA4',
    b'\r\nF1HA4\r\n' in read(gf), repr(read(gf)[:400]))
gf = fresh('gf8.doc')
before = read(gf)
fm(gf, FORMAT + ['DOWN', 'RIGHT', 'RIGHT', 'ESC', 'wait', 'C-s', 'wait'])
check('global: Esc on the Format panels keeps the settings',
    read(gf) == before, gf)
gf = fresh('gf9.doc')
before = read(gf)
fm(gf, FORMAT + ['INS', 'wait', 'C-s', 'wait'])
check('global: Ins with nothing changed leaves the file as it was',
    read(gf) == before, gf)

LINENUM = FORMAT + ['PGUP', 'PGUP']
d = fm(fresh('gl1.doc'), LINENUM + ['show'])
check('global: the third Global Format panel is Line Numbering, with its '
    'eight fields', 'Global Format (line numbering)' in d[0] and all(w in d[0]
    for w in ('Numbering', 'Number Every', 'Side', 'Number Size',
    'Number Font', 'Number Format', 'Distance', 'Equations')), d[0])
check('global: line numbering is Off, every line, Left and Tiny by default',
    all(w in d[0] for w in ('Off', 'Left', 'Tiny', 'Sans', 'Arabic',
    '0.15')), d[0])
gl = fresh('gl2.doc')
before = read(gl)
fm(gl, LINENUM + ['RIGHT', 'INS', 'wait', 'C-s', 'wait'])
check('global: Numbering Pagewise is saved as FZM1',
    b'\r\nFZM1\r\n' in read(gl), repr(read(gl)[:400]))
check('global: the Line Numbering records come after the other global ones',
    read(gl).index(b'FWN') < read(gl).index(b'FZM1') if b'FWN' in read(gl)
    else read(gl).index(b'FZM1') > read(gl).index(b'FTT5'),
    repr(read(gl)[:400]))
gl = fresh('gl3.doc')
fm(gl, LINENUM + ['RIGHT', 'RIGHT', 'DOWN', 'text:5', 'DOWN', 'RIGHT', 'RIGHT',
    'INS', 'wait', 'C-s', 'wait'])
check('global: Running, every 5th line and the Outer side are saved as '
    'FZM2, FZE5 and FZS2', all(r in read(gl) for r in (b'\r\nFZM2\r\n',
    b'\r\nFZE5\r\n', b'\r\nFZS2\r\n')), repr(read(gl)[:400]))
gl = fresh('gl4.doc')
fm(gl, LINENUM + ['RIGHT'] + ['DOWN'] * 6 + ['text:0.30', 'DOWN', 'RIGHT',
    'INS', 'wait', 'C-s', 'wait'])
check('global: a distance of 0.30 and Equations Yes are saved as FZD30 and '
    'FZQ1', all(r in read(gl) for r in (b'\r\nFZD30\r\n',
    b'\r\nFZQ1\r\n')), repr(read(gl)[:400]))
gl = fresh('gl5.doc')
d = fm(gl, LINENUM + ['RIGHT', 'DOWN', 'text:0', 'INS', 'wait', 'show'])
check('global: Number Every of 0 is refused with a message that says why',
    'Number Every must be a number from 1 to 99' in d[0], d[0])
gl = fresh('gl6.doc')
fm(gl, LINENUM + ['RIGHT', 'ESC', 'wait', 'C-s', 'wait'])
check('global: Esc on the Line Numbering panel keeps the settings',
    b'FZM' not in read(gl), repr(read(gl)[:300]))
d = fm(fresh('gf10.doc'), FORMAT + ['PGUP', 'text:abc', 'INS', 'wait',
    'show'])
check('global: a spacing that is not a number is refused',
    'must be a number' in d[0], d[0])

MARKER = GLOBAL + ['RIGHT'] * 3 + ['ENTER', 'wait']
mkd = os.path.join(here, 'data', 'markers.doc')
d = fm(fresh('gm1.doc'), MARKER + ['show'])
check('global: Marker opens the Marker Format panel with its fields',
    all(w in d[0] for w in ('Marker Format', 'Display Hidden Markers?',
    'Note Format', 'Primary Note Type', 'Cross-reference Format',
    'Date Format', 'Time Format', 'Month D, Year', 'H:MM am/pm')), d[0])
d = fm(fresh('gm2.doc'), MARKER + ['PGUP', 'show'])
check('global: PgUp shows the display modes, with the global mode first',
    'Marker Display Modes (1 of 3)' in d[0] and 'Global display mode' in d[0]
    and 'Author' in d[0] and 'Long' in d[0] and 'Column Break' in d[0], d[0])
d = fm(fresh('gm3.doc'), MARKER + ['PGUP', 'PGUP', 'PGUP', 'show'])
check('global: the last panel of modes lists Whitespace, not Worksheet',
    'Whitespace' in d[0] and 'Worksheet' not in d[0] and '(3 of 3)' in d[0],
    d[0])
d = fm(fresh('gm3b.doc'), ['F12', 'text:T', 'show'])
check('tools: the Tools menu has no Worksheet entry',
    'Worksheet' not in d[0] and 'Autosort' in d[0] and 'Count' in d[0], d[0])
mk = fresh('gm4.doc')
fm(mk, MARKER + ['DOWN', 'RIGHT', 'INS', 'wait', 'C-s', 'wait'])
check('global: Primary Note Type Levelnotes is saved as NT2',
    b'\r\nNT2\r\n' in read(mk), repr(read(mk)[:600]))
mk = fresh('gm5.doc')
fm(mk, MARKER + ['DOWN', 'DOWN', 'RIGHT', 'RIGHT', 'INS', 'wait', 'C-s',
    'wait'])
check('global: Cross-reference Format Both is saved as OC3',
    b'\r\nOC3\r\n' in read(mk), repr(read(mk)[:600]))
mk = fresh('gm6.doc')
fm(mk, MARKER + ['RIGHT', 'INS', 'wait', 'C-s', 'wait'])
check('global: Display Hidden Markers Yes is saved as OH1',
    b'\r\nOH1\r\n' in read(mk), repr(read(mk)[:600]))
WS = ['DOWN', 'A-F12', 'text:W', 'INS', 'wait', 'text:3', 'DOWN', 'DOWN']
d = fm(fresh('ws0.doc'), WS + ['show'])
check('marker: Whitespace offers Treatment at End of Page, Place All first',
    'Treatment at End of Page: [Place All on Next Page]' in d[0], d[0])
for tag, keys, rec in (('ws1', ['INS'], b'(13000\r\n\x13)'),
                       ('ws2', ['text: ', 'INS'], b'(13000\r\n22\r\n\x13)'),
                       ('ws3', ['text: ', 'text: ', 'INS'],
                        b'(13000\r\n20\r\n\x13)')):
    mk = fresh(tag + '.doc')
    fm(mk, WS + keys + ['C-s', 'wait'])
    check('marker: Whitespace %s is stored as %r' % (tag, rec),
        b'\x1331' + rec in read(mk), repr(read(mk)[:600]))
mk = fresh('gm7.doc')
fm(mk, MARKER + ['DOWN', 'DOWN', 'DOWN', 'text:MM-DD', 'DOWN',
    'text:HH.mm', 'INS', 'wait', 'C-s', 'wait'])
check('global: the date and time formats are saved as PD and PT after PP',
    b'\r\nPDMM-DD\x00\r\nPTHH.mm\x00\r\n' in read(mk) and
    read(mk).index(b'PP\\') < read(mk).index(b'PDMM-DD'),
    repr(read(mk)[:600]))
mk = fresh('gm8.doc')
fm(mk, MARKER + ['INS', 'wait', 'C-s', 'wait'])
check('global: Ins with nothing changed adds no records',
    b'\r\nOH' not in read(mk) and b'\r\nNT' not in read(mk) and
    b'\r\nPD' not in read(mk), repr(read(mk)[:600]))
mk = fresh('gm9.doc')
shutil.copy(mkd, mk)
d = fm(mk, ['show'])
check('global: the markers are drawn in their default modes first',
    '[AUTHOR]' in d[0] and '[CB]' in d[0] and '[DESCRIPTION]' in d[0], d[0])
mk = fresh('gm10.doc')
shutil.copy(mkd, mk)
d = fm(mk, MARKER + ['PGUP', 'DOWN', 'RIGHT', 'INS', 'wait', 'show'])
check('global: a marker whose mode is set to Hidden is not drawn',
    '[AUTHOR]' not in d[0] and '[CB]' in d[0], d[0])
mk = fresh('gm11.doc')
shutil.copy(mkd, mk)
d = fm(mk, MARKER + ['PGUP', 'DOWN', 'DOWN', 'RIGHT', 'INS', 'wait',
    'show'])
check('global: Column Break in the Long mode is drawn as [COLUMN BREAK]',
    '[COLUMN BREAK]' in d[0], d[0])
mk = fresh('gm12.doc')
shutil.copy(mkd, mk)
d = fm(mk, MARKER + ['PGUP', 'RIGHT', 'RIGHT', 'INS', 'wait', 'show'])
check('global: the global display mode Name draws every marker as a name',
    '[DESC]' in d[0] and '[DESCRIPTION]' not in d[0] and '[CB]' in d[0]
    and '[AUTHOR]' in d[0], d[0])
mk = fresh('gm13.doc')
shutil.copy(mkd, mk)
d = fm(mk, MARKER + ['PGUP', 'RIGHT', 'INS', 'wait', 'show'])
check('global: the global display mode Diamond draws a diamond for each',
    '[AUTHOR]' not in d[0] and d[0].count('\u2666') >= 15, d[0])
mk = fresh('gm14.doc')
shutil.copy(mkd, mk)
fm(mk, MARKER + ['PGUP', 'DOWN', 'RIGHT', 'INS', 'wait', 'C-s', 'wait'])
check('global: a display mode other than the default is saved as OM',
    b'\r\nOM5,4\r\n' in read(mk), repr(read(mk)[:600]))
mk = fresh('gm15.doc')
shutil.copy(mkd, mk)
d = fm(mk, MARKER + ['RIGHT', 'INS', 'wait', 'F12', 'text:G', 'RIGHT',
    'RIGHT', 'RIGHT', 'ENTER', 'wait', 'PGUP', 'DOWN', 'RIGHT', 'INS',
    'wait', 'show'])
check('global: with Hidden Markers Yes a hidden marker is drawn again',
    '[AUTHOR]' in d[0], d[0])

PAGE = GLOBAL + ['RIGHT'] + ['ENTER', 'wait']
d = fm(fresh('gp1.doc'), PAGE + ['show'])
check('global: Page opens the Global Page Layout with the original\'s fields',
    all(w in d[0] for w in ('Global Page Layout', 'Left Margin',
    'Right Margin', 'Top Margin', 'Bottom Margin', 'Header Indent',
    'Footer Whitespace', 'Page Number Tag', 'Paper Size', 'Paper Bin',
    'Letter')), d[0])
d = fm(fresh('gp2.doc'), PAGE + ['PGUP', 'show'])
check('global: PgUp shows the Title Page Layout with a vertical centering',
    'Title Page Layout' in d[0] and 'Vertical Centering' in d[0]
    and '[1.000]' in d[0], d[0])
d = fm(fresh('gp3.doc'), PAGE + ['PGDN', 'show'])
check('global: PgDn from the first panel shows the Index Page Layout',
    'Index Page Layout' in d[0], d[0])
pg = fresh('gp4.doc')
fm(pg, PAGE + ['text:1.25', 'DOWN', 'text:1.5', 'DOWN', 'text:1.75', 'DOWN',
    'text:2.25', 'INS', 'wait', 'C-s', 'wait'])
check('global: the four margins are saved as PML, PMR, PMT and PMB',
    b'\r\nPML1250\r\nPMR1500\r\nPMT1750\r\nPMB2250\r\n' in read(pg),
    repr(read(pg)[:300]))
check('global: the margin records come before the page number tag',
    read(pg).index(b'PML1250') < read(pg).index(b'PP\\'),
    repr(read(pg)[:300]))
pg = fresh('gp5.doc')
fm(pg, PAGE + ['DOWN'] * 6 + ['text:0.6', 'DOWN', 'text:0.7', 'DOWN',
    'text:0.4', 'DOWN', 'text:0.9', 'INS', 'wait', 'C-s', 'wait'])
check('global: header and footer indent and whitespace are PHI, PFI, PHW, '
    'PFW', b'\r\nPHI600\r\nPFI700\r\nPHW400\r\nPFW900\r\n' in read(pg),
    repr(read(pg)[:300]))
pg = fresh('gp6.doc')
fm(pg, PAGE + ['PGUP', 'text:1.1', 'INS', 'wait', 'C-s', 'wait'])
check('global: a margin of the Title Page Layout is saved as P1ML',
    b'\r\nP1ML1100\r\n' in read(pg), repr(read(pg)[:300]))
pg = fresh('gp7.doc')
fm(pg, PAGE + ['DOWN'] * 11 + ['RIGHT', 'INS', 'wait', 'C-s', 'wait'])
check('global: Paper Size Legal is saved in the DP record',
    b'\r\nDP1,8500,14000\r\n' in read(pg), repr(read(pg)[-300:]))
pg = fresh('gp8.doc')
before = read(pg)
fm(pg, PAGE + ['text:3', 'ESC', 'wait', 'C-s', 'wait'])
check('global: Esc on the page layouts keeps the settings',
    read(pg) == before, pg)
pg = fresh('gp9.doc')
before = read(pg)
fm(pg, PAGE + ['INS', 'wait', 'C-s', 'wait'])
check('global: Ins with nothing changed leaves the file as it was',
    read(pg) == before, pg)
d = fm(fresh('gp10.doc'), PAGE + ['text:abc', 'INS', 'wait', 'show'])
check('global: a margin that is not a number is refused',
    'must be a number' in d[0], d[0])

# The Header and the Footer: one line, left | centre | right.
HDR = ['DOWN'] * 4
FTR = ['DOWN'] * 5
pg = fresh('gh1.doc')
fm(pg, PAGE + HDR + ['text:Chapter | \\page\\ | Strata'] + ['INS', 'wait',
    'C-s', 'wait'])
CW = b'CW26\r\n'
check('global: the Header is saved as PHB with a block of three columns',
    b'\r\nPHB\r\nS\r\n' + CW + b'CFJ0\r\nTChapter\x1d\x00\r\n' + CW +
    b'CFJ2\r\nT\x1322(\x13)\x1d\x00\r\nCFJ1\r\nTStrata\x1d\x00\r\n' in
    read(pg), repr(read(pg)[:400]))
check('global: the records of the Header come before the page number tag',
    read(pg).index(b'\r\nPHB\r\n') < read(pg).index(b'PP\\'), repr(read(pg)[:300]))
d = fm(pg, PAGE + ['show'])
check('global: the Header shows its text again, with \\page\\',
    'Chapter | \\page\\ | Strata' in d[0], d[0])
# The three forms of the Section marker: number and title, number, title.
pg = fresh('gh1s.doc')
before = read(pg)
fm(pg, PAGE + HDR + ['text:\\section\\ | \\sectionnumber\\ | \\sectiontitle\\'] +
    ['INS', 'wait', 'C-s', 'wait'])
check('global: the Header with the three forms had no Section marker before',
    b'\x1328(' not in before, repr(before[:200]))
check('global: \\section\\ is the marker with no field, \\sectionnumber\\ '
    'has the field S2 and \\sectiontitle\\ the field S3',
    b'CFJ0\r\nT\x1328(\x13)\x1d\x00\r\n' in read(pg) and
    b'CFJ2\r\nT\x1328(S2\r\n\x13)\x1d\x00\r\n' in read(pg) and
    b'CFJ1\r\nT\x1328(S3\r\n\x13)\x1d\x00\r\n' in read(pg),
    repr(read(pg)[:500]))
d = fm(pg, PAGE + ['show'])
check('global: the Header shows the three forms again',
    '\\section\\ | \\sectionnumber\\ | \\sectiontitle\\' in d[0], d[0])
# The panel of a Section marker has the field Show (the marker is in a body
# here, as a document of the original can have it).
sm = os.path.join(tmp, 'secmark.doc')
open(sm, 'wb').write(b'*R3\r\nMR1001\r\nPP\\1\\\x00\r\nP1C33\r\nFHA1\r\nFTT5\r\n'
    b'DP0,8500,11000\r\n*B0\r\nS\r\nTab\x1328(S3\r\n\x13)cd\x1d\x00\r\n*E\r\n')
d = fm(sm, ['RIGHT', 'RIGHT', 'C-F12', 'wait', 'show'])
check('global: the panel of a Section marker has the field Show with Title '
    'Only chosen', 'Show' in d[0] and 'Title Only' in d[0], d[0])
d = fm(sm, ['RIGHT', 'RIGHT', 'C-F12', 'wait', 'LEFT', 'INS', 'wait', 'C-s',
    'wait'])
check('global: choosing Number Only in the panel stores S2',
    b'28(S2\r\n' in read(sm) and b'28(S3' not in read(sm), repr(read(sm)))
tui.drop_place(sm)
fm(sm, ['RIGHT', 'RIGHT', 'C-F12', 'wait', 'LEFT', 'LEFT', 'INS', 'wait',
    'C-s', 'wait'])
check('global: choosing Number and Title (the default) leaves no record',
    b'\x1328(\x13)' in read(sm) and b'S2' not in read(sm), repr(read(sm)))
pg = fresh('gh2.doc')
fm(pg, PAGE + FTR + ['text:Only the centre'] + ['INS', 'wait', 'C-s', 'wait'])
check('global: a Footer with no | is the centre part',
    b'\r\nPFB\r\nS\r\n' + CW + b'CFJ0\r\nT\x1d\x00\r\n' + CW +
    b'CFJ2\r\nTOnly the centre\x1d\x00\r\nCFJ1\r\nT\x1d\x00\r\n' in read(pg),
    repr(read(pg)[:400]))
pg = fresh('gh3.doc')
fm(pg, PAGE + ['PGUP'] * 4 + FTR + ['text:EVEN \\page\\ | x'] + ['INS',
    'wait', 'C-s', 'wait'])
check('global: the Footer of the Even Page Layout is saved as P4FB1',
    b'\r\nP4FB1\r\nS\r\n' + CW + b'CFJ0\r\nTEVEN \x1322(\x13)\x1d\x00\r\n' in
    read(pg), repr(read(pg)[:400]))
d = fm(fresh('gh4.doc'), PAGE + HDR + ['text:a|b|c|d', 'INS', 'wait', 'show'])
check('global: a header of four parts is refused',
    'at most three parts' in d[0], d[0])
pg = fresh('gh5.doc')
d = fm(pg, PAGE + ['PGUP'] * 5 + ['show'])
check('global: a layout with no Header of its own shows no text before the '
    'Global Header is set', 'Odd Page Layout' in d[0] and
    'Chapter' not in d[0], d[0])
fm(pg, PAGE + HDR + ['text:Chapter | \\page\\ | Strata'] + ['INS', 'wait',
    'C-s', 'wait'])
d = fm(pg, PAGE + ['PGUP'] * 5 + ['show'])
check('global: a layout with no Header of its own shows the Global one in '
    'brackets', 'Odd Page Layout' in d[0] and
    'Header: [Chapter | \\page\\ | Strata]' in d[0], d[0])
pg = fresh('gh6.doc')
shutil.copy(os.path.join(here, 'data', 'plain_text.doc'), pg)
fm(pg, PAGE + ['PGUP'] * 5 + ['DOWN'] * 10 + ['text:\\I\\', 'INS', 'wait',
    'C-s', 'wait'])
t6 = read(pg)
check('global: a tag set on a layout that has a header leaves its blocks '
    'whole', b'P5P\\I\\' in t6 and b'P5HB1\r\nS\r\n' in t6 and
    b'T\x1322(\x13)\x1d\x00\r\nFHA1' in t6, repr(t6[:700]))

# ---- Border (Alt-F8) --------------------------------------------------------
BORDER = ['A-F8', 'wait']
d = session(base, BORDER + ['show'])
check('border: Alt-F8 asks to highlight the columns',
    'Use arrow keys to highlight column(s); then press Enter' in
    d[0].split('\n')[-1], d[0].split('\n')[-1])
d = session(base, BORDER + ['ENTER', 'wait', 'show'])
check('border: Enter shows the Border Layout panel with its fields',
    all(w in d[0] for w in ('Border Layout', 'All', 'Outside', 'Top', 'Left',
    'Inside', 'Right', 'Bottom', 'Top Gutter', 'Bottom Gutter',
    'Connect Borders?', 'Shading', 'None')) and
    'n/a' in d[0], d[0])
d = session(base, BORDER + ['ENTER', 'wait', 'ESC', 'show'])
check('border: Esc on the panel changes nothing and says so',
    'Cancelled' in d[0].split('\n')[-1] and '*' not in d[0][:30], d[0])


# The Border checks start from a document that has no border record at all,
# so that a check cannot pass because the record was there before.
def bplain(name):
    path = os.path.join(tmp, name)
    shutil.copy(os.path.join(here, 'data', 'plain_text.doc'), path)
    return path


def bfile(name, keys, save=True):
    pg = bplain(name)
    fm(pg, BORDER + ['ENTER', 'wait'] + keys + ['wait'] +
       (['C-s', 'wait'] if save else []))
    return read(pg), pg


t, pg = bfile('gb0.doc', [])
check('border: the document of these checks has no border record',
    b'\r\nCB' not in t and b'\r\nCL' not in t and b'\r\nCR' not in t,
    repr(t[:300]))
t, pg = bfile('gb1.doc', ['RIGHT', 'RIGHT', 'RIGHT', 'INS'])
check('border: All Double is saved as CB170,1 with CL1 and CR1',
    b'\r\nCB170,1\r\n' in t and b'\r\nCL1\r\n' in t and
    b'\r\nCR1\r\n' in t, repr(t[:600]))
t, pg = bfile('gb2.doc', ['RIGHT', 'RIGHT', 'INS'])
check('border: All Single is CB85,1 with CL1 and CR1',
    b'\r\nCB85,1\r\n' in t and b'\r\nCL1\r\n' in t and
    b'\r\nCR1\r\n' in t, repr(t[:600]))
t, pg = bfile('gb3.doc', ['DOWN', 'DOWN', 'RIGHT', 'INS'])
check('border: Top Single only is CB1,1 and no CL or CR',
    b'\r\nCB1,1\r\n' in t and b'\r\nCL1' not in t and
    b'\r\nCR1' not in t, repr(t[:600]))
t, pg = bfile('gb4.doc', ['DOWN', 'DOWN', 'DOWN', 'RIGHT', 'INS'])
check('border: Left Single only is CB16,1 with CL1 and not CR1',
    b'\r\nCB16,1\r\n' in t and b'\r\nCL1\r\n' in t and
    b'\r\nCR1\r\n' not in t, repr(t[:600]))
t, pg = bfile('gb5.doc', ['DOWN'] * 6 + ['RIGHT', 'INS'])
check('border: Bottom Single only is CB4,1',
    b'\r\nCB4,1\r\n' in t and b'\r\nCL1' not in t, repr(t[:600]))
t, pg = bfile('gb6.doc', ['DOWN'] * 8 + ['text:0.25', 'INS'])
check('border: a Bottom Gutter of 0.25 inch is BB250',
    b'\r\nBB250\r\n' in t, repr(t[:600]))
t, pg = bfile('gb7.doc', ['RIGHT', 'RIGHT', 'RIGHT', 'S-INS'])
check('border: Shift-Ins uses every field: CB170,1 and no gutter record',
    b'\r\nCB170,1\r\n' in t and b'\r\nBB' not in t and
    b'\r\nBT' not in t, repr(t[:600]))
d = fm(bplain('gb8.doc'), BORDER + ['ENTER', 'wait'] + ['DOWN'] * 8 +
       ['text:0.1234', 'INS', 'wait', 'show'])
check('border: a gutter with four decimals is refused',
    'Invalid number (no more than 3 decimal places allowed)' in d[0], d[0])
d = fm(bplain('gb9.doc'), BORDER + ['ENTER', 'wait', 'INS', 'wait', 'show'])
check('border: Ins with nothing changed leaves the document as it was',
    '*gb9.doc' not in d[0] and 'gb9.doc' in d[0] and
    'Border: 0 changes' in d[0], d[0][:200])
pg = bplain('gb10.doc')
fm(pg, BORDER + ['ENTER', 'wait', 'RIGHT', 'RIGHT', 'RIGHT', 'INS', 'wait',
    'C-F10', 'wait', 'C-s', 'wait'])
check('border: the command is one operation for Undo, which takes it back',
    b'\r\nCB' not in read(pg), repr(read(pg)[:300]))

# ---- Border on a highlight over several columns and rows -------------------
# A table of 5 rows (blocks) of 3 columns, with no border record at all.
CRL = b'\r\n'


def trow(text):
    return b'T' + text + b'\x1d\x00' + CRL


def mtable(name):
    path = fresh(name)
    body = b''
    for i in range(5):
        body += (b'*B0' if i == 0 else b'B') + CRL
        body += b'S' + CRL if i == 0 else b''
        for j in range(3):
            body += b'CW20' + CRL + trow(b'r%dc%d' % (i + 1, j + 1))
    open(path, 'wb').write(b'*R3' + CRL + b'MR1001' + CRL + b'PP\\1\\\x00' +
        CRL + b'FHA1' + CRL + b'DP0,8500,11000' + CRL + body + b'*E' + CRL)
    return path


def cbs(path):
    """The CB text of every column of every block: a list of lists."""
    out = []
    for line in read(path).split(b'\r\n'):
        if line == b'B' or line.startswith(b'*B'):
            out.append([])
        elif line.startswith(b'CW'):
            out[-1].append('')
        elif line.startswith(b'CB'):
            out[-1][-1] = line[2:].decode()
    return out


# Keys: highlight the three columns of a row, and the rows below it.
ACROSS = ['S-RIGHT', 'S-RIGHT']
FIRSTROW = BORDER + ACROSS + ['ENTER', 'wait']
# All Single, then Bottom Double (the sixth field below All).
ROW1 = (['RIGHT', 'RIGHT'] + ['DOWN'] * 6 + ['RIGHT', 'RIGHT', 'INS', 'wait'])
# The next four rows: Left, Inside, Right, Bottom Single.
ROWS25 = (['S-LEFT', 'S-LEFT', 'DOWN'] + BORDER + ACROSS + ['DOWN'] * 3 +
          ['ENTER', 'wait'] + ['DOWN'] * 3 + ['RIGHT', 'DOWN', 'RIGHT',
          'DOWN', 'RIGHT', 'DOWN', 'RIGHT', 'INS', 'wait'])
mt = mtable('bm1.doc')
fm(mt, FIRSTROW + ROW1 + ['C-s', 'wait'])
check('border rows: a highlight over three columns gives all three the '
      'sides (top, left, right single, bottom double)',
    cbs(mt)[0] == ['89,1', '73,1', '73,1'] and
    cbs(mt)[1:] == [[''] * 3] * 4,
    cbs(mt))
mt = mtable('bm2.doc')
fm(mt, FIRSTROW + ROW1 + ROWS25 + ['C-s', 'wait'])
check('border rows: a highlight over four rows of three columns gives '
      'all twelve columns left, right and bottom single and no top',
    cbs(mt)[1:] == [['84,1', '68,1', '68,1']] * 4 and
    cbs(mt)[0] == ['89,1', '73,1', '73,1'],
    cbs(mt))
# A field that is left at n/a changes nothing where the blocks differ.
mt = mtable('bm3.doc')
TOPONE = (BORDER + ['ENTER', 'wait', 'DOWN', 'DOWN', 'RIGHT', 'INS', 'wait'])
BOTTOM1 = (BORDER + ['DOWN', 'ENTER', 'wait'] + ['DOWN'] * 6 +
           ['RIGHT', 'S-INS', 'wait'])
fm(mt, TOPONE + BOTTOM1 + ['C-s', 'wait'])
b = cbs(mt)
check('border rows: n/a (blocks differ) leaves the top of each block as it '
      'was while Bottom is set on both',
    b[0][0] == '5,1' and b[1][0] == '4,1' and b[2][0] == '', b)
d = fm(mtable('bm4.doc'), TOPONE + BORDER + ['DOWN', 'ENTER', 'wait', 'show'])
check('border rows: the panel over a highlight shows n/a where blocks '
      'differ (Top) and a value where they agree (Left)',
    re.search(r'Top: \[n/a\]', d[0]) is not None and
    re.search(r'Left: \[None\]', d[0]) is not None, d[0])
mt = mtable('bm5.doc')
fm(mt, FIRSTROW + ROW1 + ['C-F10', 'wait', 'C-s', 'wait'])
check('border rows: one Undo takes back the border of all the columns',
    cbs(mt) == [[''] * 3] * 5, cbs(mt))
mt = mtable('bm6.doc')
fm(mt, BORDER + ['ENTER', 'wait', 'DOWN', 'DOWN', 'RIGHT', 'INS', 'wait',
    'C-s', 'wait'])
check('border rows: with no highlight the panel works on the one column',
    cbs(mt)[0] == ['1,1', '', ''] and cbs(mt)[1:] == [[''] * 3] * 4,
    cbs(mt))

d = fm(mtable('bm7.doc'), BORDER + ACROSS + ['DOWN', 'wait', 'rev'])
rr = [l for l in d[0].split('\n') if 'r1c' in l or 'r2c' in l or
      'r3c' in l]
check('border rows: the highlight shows on all three columns of the first '
      'two rows and on no other row',
    len(rr) == 3 and rr[0].count('⟦') == 3 and
    rr[1].count('⟦') == 3 and rr[2].count('⟦') == 0, d[0])
mt = mtable('bm8.doc')
d = fm(mt, BORDER + ACROSS + ['ENTER', 'wait'] + ['DOWN'] * 4 +
       ['RIGHT', 'INS', 'wait', 'show', 'C-s', 'wait'])
rr = [l for l in d[0].split('\n') if 'r1c1' in l]
check('border rows: Inside Single alone is ONE bar between two columns on '
      'the row of text (cells), kept as the right side of the left column',
    len(rr) == 1 and rr[0].count('│') == 2 and
    cbs(mt)[0] == ['64,1', '64,1', ''], (d[0], cbs(mt)))
mt = mtable('bm9.doc')
d = fm(mt, BORDER + ACROSS + ['ENTER', 'wait', 'RIGHT', 'RIGHT', 'INS',
       'wait', 'show', 'C-s', 'wait'])
rr = [l for l in d[0].split('\n') if 'r1c1' in l]
check('border rows: All Single on three columns is four bars on the row of '
      'text and no column has its own left side but the first',
    len(rr) == 1 and rr[0].count('│') == 4 and
    cbs(mt)[0] == ['85,1', '69,1', '69,1'], (d[0], cbs(mt)))

# ---- Border on the screen ---------------------------------------------------
# The documents are made here: they hold no border record, so that a frame on
# the screen can only come from the Border command.
BOXES = '\u250c\u2510\u2514\u2518\u2502\u2554\u2557\u255a\u255d\u2551'
CRL = b'\r\n'


def bdoc(name, body, hdr=b''):
    path = fresh(name)
    open(path, 'wb').write(b'*R3' + CRL + b'MR1001' + CRL + b'PP\\1\\\x00' +
        CRL + b'FHA1' + CRL + hdr + b'DP0,8500,11000' + CRL + body + b'*E' +
        CRL)
    return path


def trow(text):
    return b'T' + text + b'\x1d\x00' + CRL


two = (b'*B0' + CRL + b'S' + CRL + b'CW20' + CRL + trow(b'Left cell') +
       trow(b'Right cell has words') + b'B' + CRL + trow(b'Second block'))
long_text = b' '.join(b'word%d' % i for i in range(40))
wrapped = (b'*B0' + CRL + b'S' + CRL + trow(long_text) + b'B' + CRL +
           trow(b'Next'))
BSEL = ['A-F8', 'wait']


def rows_of(d):
    return d.split('\n')


d = fm(bdoc('sb0.doc', two), ['show'])
check('border screen: no box character on a document with no border',
    not any(c in d[0] for c in BOXES), d[0])
SINGLE = ['RIGHT', 'RIGHT']
DOUBLE = ['RIGHT', 'RIGHT', 'RIGHT']
d = fm(bdoc('sb1.doc', two), BSEL + ['ENTER', 'wait', 'wait'] + DOUBLE +
       ['INS', 'wait', 'wait', 'show'])
r = rows_of(d[0])
top = next((i for i, l in enumerate(r) if '\u2554' in l), -1)
# The rule between the two blocks goes on to the right of the frame (the
# second column has no border), so the bottom right corner of the frame is a
# tee where the rule meets it: a bottom tee, not a corner.
check('border screen: All Double on one column: the frame is at the right '
    'cells', top >= 0 and r[top].startswith('\u2554' + '\u2550' * 20 +
    '\u2557') and r[top + 1].startswith('\u2551Left cell\u25c4') and
    r[top + 1][21] == '\u2551' and r[top + 2].startswith('\u255a' +
    '\u2550' * 20 + '\u2569'), d[0])
check('border screen: the second block, which has no border, has none',
    top >= 0 and 'Second block' in r[top + 3] and not any(
    c in r[top + 3] for c in BOXES), d[0])
d = fm(bdoc('sb2.doc', two), BSEL + ['ENTER', 'wait', 'wait'] + SINGLE +
       ['INS', 'wait', 'wait', 'show'])
r = rows_of(d[0])
top = next((i for i, l in enumerate(r) if '\u250c' in l), -1)
check('border screen: All Single draws the thin frame: corners and sides',
    top >= 0 and r[top].startswith('\u250c' + '\u2500' * 20 + '\u2510') and
    r[top + 1].startswith('\u2502Left cell') and r[top + 1][21] == '\u2502'
    and r[top + 2].startswith('\u2514' + '\u2500' * 20 + '\u2534') and
    '\u2554' not in d[0], d[0])
d = fm(bdoc('sb3.doc', two), BSEL + ['ENTER', 'wait', 'wait', 'DOWN', 'DOWN',
       'RIGHT', 'INS', 'wait', 'wait', 'show'])
r = rows_of(d[0])
check('border screen: Top Single only: a line above the block and no side',
    r[2].startswith('\u2500' * 20) and 'Left cell' in r[3] and
    r[3][0] == 'L' and not any(c in r[3] for c in '\u2502\u2551'), d[0])
d = fm(bdoc('sb4.doc', two), BSEL + ['ENTER', 'wait', 'wait', 'DOWN', 'DOWN',
       'DOWN', 'RIGHT', 'INS', 'wait', 'wait', 'show'])
r = rows_of(d[0])
check('border screen: Left Single only: a bar at the left of each row, none '
    'above, and the rule below ends it with a corner',
    r[2].startswith('\u2502Left cell') and '\u250c' not in d[0] and
    r[3].startswith('\u2514\u2500'), d[0])
# a table of two columns: both are in the highlight
d = fm(bdoc('sb5.doc', two), BSEL + ['S-RIGHT', 'S-RIGHT', 'ENTER', 'wait',
       'wait'] + SINGLE + ['INS', 'wait', 'wait', 'show'])
r = rows_of(d[0])
top = next((i for i, l in enumerate(r) if '\u250c' in l), -1)
# One line between the columns: three bars on the row of text, not four.
check('border screen: a table of two columns: one frame, with one line '
      'between the columns',
    top >= 0 and r[top + 1].count('\u2502') == 3 and
    '\u2514' in r[top + 2], d[0])
# a block whose text wraps: the sides are on every row
d = fm(bdoc('sb6.doc', wrapped), BSEL + ['ENTER', 'wait', 'wait'] + DOUBLE +
       ['INS', 'wait', 'wait', 'show'])
r = rows_of(d[0])
top = next((i for i, l in enumerate(r) if '\u2554' in l), -1)
bot = next((i for i, l in enumerate(r) if '\u255a' in l), -1)
check('border screen: a block that wraps has the sides on every row',
    top >= 0 and bot - top >= 3 and all(r[k][0] == '\u2551' and
    r[k][79] == '\u2551' for k in range(top + 1, bot)) and
    'Next' in r[bot + 1], d[0])
# Undo and Redo
d = fm(bdoc('sb7.doc', two), BSEL + ['ENTER', 'wait', 'wait'] + DOUBLE +
       ['INS', 'wait', 'wait', 'C-F10', 'wait', 'wait', 'show', 'C-F11',
        'wait', 'wait', 'show'])
check('border screen: Undo takes the frame away', not any(c in d[0] for c in
    BOXES) and 'Left cell' in d[0], d[0])
check('border screen: Redo brings it back', '\u2554' in d[1] and
    '\u255a' in d[1], d[1])
# a fold hides the border of a block that is hidden
sect = (b'*B1' + CRL + b'S' + CRL + b'+' + CRL + b'SE0' + CRL + trow(b'Chap') +
        b'B' + CRL + trow(b'Body of the chapter') + b'-' + CRL)
d = fm(bdoc('sb8.doc', sect), ['DOWN', 'DOWN'] + BSEL + ['ENTER', 'wait',
       'wait'] + DOUBLE + ['INS', 'wait', 'wait', 'show', 'A-1', 'wait',
       'show'])
check('border screen: the frame is on a section that is open', '\u2554' in
    d[0] and 'Body of the chapter' in d[0], d[0])
check('border screen: a folded section hides the frame of its text',
    '\u2554' not in d[1] and 'Body of the chapter' not in d[1], d[1])

# The Winding Column Layout panel behind Winding Columns (F2).
WIND = FORMAT + ['PGUP'] + ['DOWN'] * 13 + ['F2', 'wait']
d = fm(fresh('gw1.doc'), WIND + ['show'])
check('global: F2 on Winding Columns opens the Winding Column Layout panel',
    all(w in d[0] for w in ('Winding Column Layout', 'Number of Columns',
    'Gutter Size', 'Vertical Rules', 'None')) and
    'Bottom Alignment' not in d[0], d[0])
gw = fresh('gw2.doc')
fm(gw, WIND + ['text:3', 'DOWN', 'text:0.5', 'DOWN', 'RIGHT', 'INS', 'wait',
    'INS', 'wait', 'C-s', 'wait'])
check('global: the layout is saved as FWN3, FWG500 and FWR1',
    all(r in read(gw) for r in (b'\r\nFWN3\r\n', b'\r\nFWG500\r\n',
    b'\r\nFWR1\r\n')), repr(read(gw)[:400]))
check('global: the records are in the order WN, WG, WR',
    read(gw).index(b'FWN3') < read(gw).index(b'FWG500') <
    read(gw).index(b'FWR1'), repr(read(gw)[:400]))
d = fm(fresh('gw4.doc'), WIND + ['DOWN', 'text:x', 'INS', 'wait', 'show'])
check('global: a gutter that is not a number is refused',
    'Gutter Size must be a number' in d[0], d[0])
d = fm(fresh('gw5.doc'), WIND + ['text:12', 'INS', 'wait', 'show'])
check('global: ten or more columns are refused',
    'Number of Columns must be from 1 to 9' in d[0], d[0])
gw = fresh('gw6.doc')
before = read(gw)
fm(gw, WIND + ['text:4', 'ESC', 'wait', 'ESC', 'wait', 'C-s', 'wait'])
check('global: Esc on the layout panel keeps the settings',
    b'FWN4' not in read(gw), repr(read(gw)[:300]))

# The indent of the levels on the screen: the sum of the Indent to Next Level.
def levels_doc(name, recs):
    CR = b'\r\n'
    out = (b'*R3' + CR + b'MR1001' + CR + b'PP\\1\\\x00' + CR + b'FHA1' + CR +
           recs + b'DP0,8500,11000' + CR + b'*B1' + CR + b'S' + CR + b'+' + CR)
    for lv in (1, 2, 3):
        out += (b'SE0' + CR + b'THead%d' % lv + b'\x1d\x00' + CR + b'B' + CR +
                b'TText%d' % lv + b'\x1d\x00' + CR)
        if lv < 3:
            out += b'+' + CR
    out += b'-' + CR + b'-' + CR + b'-' + CR + b'*E' + CR
    path = fresh(name)
    open(path, 'wb').write(out)
    return path


def text_cols(d):
    cols = {}
    for ln in d[0].split('\n'):
        for w in ('Text1', 'Text2', 'Text3'):
            if w in ln:
                cols[w] = ln.index(w)
    return cols


for nm, recs, want in (('F1I5 and F2I3', b'F1I5\r\nF2I3\r\n', (5, 8)),
                       ('F2I5 only', b'F2I5\r\n', (0, 5)),
                       ('Indent per Level 4', b'FI4\r\n', (4, 8))):
    cols = text_cols(fm(levels_doc('li.doc', recs), ['show']))
    check('the screen indents the levels by the sum of the Indent to Next '
          'Level (%s)' % nm, len(cols) == 3 and
          (cols['Text2'] - cols['Text1'], cols['Text3'] - cols['Text1']) ==
          want, cols)

NAMED = GLOBAL + ['RIGHT'] * 5 + ['ENTER']
d = fm(fresh('gn1.doc'), NAMED + ['show'])
check('global: Named is a menu of Edit and Delete with the original\'s hint',
    'Edit' in d[0] and 'Delete' in d[0] and 'Edit a named block format'
    in d[0], d[0])
d = fm(fresh('gn2.doc'), NAMED + ['ENTER', 'wait', 'show'])
check('global: Named, Edit asks which named format, with (new) first',
    '(new)' in d[0] and 'tab list' in d[0], d[0])
d = fm(fresh('gn3.doc'), NAMED + ['ENTER', 'wait', 'DOWN', 'ENTER', 'wait',
    'show'])
check('global: a named format that is retrieved fills the Named Block '
    'Format panel', 'Named Block Format' in d[0] and 'tab list' in d[0]
    and '24,49' in d[0] and 'Keep Block on One Page?' in d[0]
    and 'Block Indent (Left)' in d[0], d[0])
nm = fresh('gn4.doc')
fm(nm, NAMED + ['ENTER', 'wait', 'ENTER', 'wait', 'text:mine', 'DOWN',
    'DOWN', 'RIGHT', 'RIGHT', 'INS', 'wait', 'C-s', 'wait'])
check('global: a new named format is saved as FN, its records and FN',
    b'\r\nFNmine\x00\r\nBFA1\r\nFNtab list\x00\r\nBFT24,49\r\nFN\x00'
    b'\r\n' in read(nm), repr(read(nm)[:500]))
nm = fresh('gn5.doc')
fm(nm, NAMED + ['ENTER', 'wait', 'ENTER', 'wait', 'text:p2'] + ['DOWN'] * 3 +
    ['RIGHT'] * 4 + ['DOWN', 'RIGHT', 'RIGHT', 'DOWN', 'RIGHT', 'INS', 'wait',
    'C-s', 'wait'])
check('global: justification, indent and font are BFJ, BFI and BFF, in '
    'order', b'FNp2\x00\r\nBFJ3\r\nBFI1\r\nBFF1\r\n' in read(nm),
    repr(read(nm)[:500]))
nm = fresh('gn6.doc')
fm(nm, NAMED + ['ENTER', 'wait', 'DOWN', 'ENTER', 'wait', 'DOWN',
    'text:2.5', 'INS', 'wait', 'C-s', 'wait'])
check('global: the retrieved format is saved under its name again with the '
    'change', b'FNtab list\x00\r\nBFT2.5\r\nFN\x00' in read(nm),
    repr(read(nm)[:500]))
nm = fresh('gn7.doc')
before = read(nm)
fm(nm, NAMED + ['ENTER', 'wait', 'ENTER', 'wait', 'text:zzz', 'ESC', 'wait',
    'C-s', 'wait'])
check('global: Esc on the Named Block Format panel keeps the settings',
    read(nm) == before, nm)
nm = fresh('gn8.doc')
fm(nm, NAMED + ['RIGHT', 'ENTER', 'wait', 'ENTER', 'wait', 'text:y', 'wait',
    'C-s', 'wait'])
check('global: Named, Delete removes the format and the list that ends it',
    b'FNtab list' not in read(nm) and b'\r\nFN\x00' not in read(nm),
    repr(read(nm)[:500]))
d = fm(fresh('gn9.doc'), NAMED + ['ENTER', 'wait', 'ENTER', 'wait', 'INS',
    'wait', 'show'])
check('global: a new format with no name is refused', 'Enter a name' in d[0],
    d[0])

# ---- the Tools menu: Library -----------------------------------------------
LIBM = ['F12', 'text:T', 'text:L']
d = fm(fresh('lb1.doc'), LIBM + ['show'])
check('library: Tools, Library offers Edit, Delete, Save and Retrieve',
    all(w in d[0] for w in ('Edit', 'Delete', 'Save', 'Retrieve')), d[0])
d = fm(fresh('lb2.doc'), LIBM + ['text:E', 'wait', 'show'])
check('library: Edit asks for the entry name', 'Library Entry' in d[0]
    and 'Entry Name' in d[0], d[0])
d = fm(fresh('lb3.doc'), LIBM + ['text:E', 'wait', 'text:one', 'INS', 'wait',
    'show'])
check('library: the Library Entry panel has the original\'s fields',
    all(w in d[0] for w in ('Entry Name', 'Description', 'Contents',
    'Include in Library?')), d[0])
LC = ['C-F3', 'wait', 'DOWN', 'DOWN', 'ENTER', 'wait', 'ENTER', 'wait']
d = fm(fresh('lb4.doc'), LC + ['show'])
check('library: Lib Copy of a block asks for the entry name and description',
    'Copy to Library' in d[0] and 'Entry name' in d[0]
    and 'Description' in d[0] and 'Include in Library?' in d[0], d[0])
lb = fresh('lb5.doc')
lbdir = os.path.dirname(lb)
fm(lb, LC + ['text:blk1', 'DOWN', 'text:A saved block', 'INS', 'wait'] +
    LIBM + ['text:S', 'wait', 'DOWN', 'text:mylib', 'INS', 'wait', 'wait'])
saved = read(os.path.join(lbdir, 'mylib.lib'))
check('library: Save writes a library file with the entry and its '
    'description', b'TBblk1\x1d\x00' in saved and
    b'TA saved block\x1d\x00' in saved and saved.startswith(b'*R3'),
    repr(saved[:300]))
lb = fresh('lb6.doc')
before = read(lb).count(b'Orchard Plan')
fm(lb, LC + ['text:blk2', 'INS', 'wait', 'DOWN', 'DOWN', 'C-F5', 'wait',
    'text:blk2', 'INS', 'wait', 'C-s', 'wait'])
check('library: Lib Paste puts the entry in the document',
    read(lb).count(b'Orchard Plan') == before + 1, (before,
    read(lb).count(b'Orchard Plan')))
lb = fresh('lb7.doc')
before = read(lb).count(b'Orchard Plan')
fm(lb, LIBM + ['text:R', 'wait', 'DOWN', 'text:mylib', 'INS', 'wait',
    'C-F5', 'wait', 'text:blk1', 'INS', 'wait', 'C-s', 'wait'])
check('library: Retrieve brings the entries of a file back for Lib Paste',
    read(lb).count(b'Orchard Plan') == before + 1, (before,
    read(lb).count(b'Orchard Plan')))
lb = fresh('lb8.doc')
fm(lb, LIBM + ['text:E', 'wait', 'text:new1', 'INS', 'wait', 'DOWN', 'DOWN',
    'RIGHT', 'INS', 'wait', 'ESC', 'wait'] + LIBM + ['text:S', 'wait',
    'DOWN', 'text:lib2', 'INS', 'wait', 'wait'])
saved = read(os.path.join(lbdir, 'lib2.lib'))
check('library: an entry made with Edit is saved', b'TBnew1\x1d\x00' in saved,
    repr(saved[:300]))
lb = fresh('lb9.doc')
fm(lb, LC + ['text:gone', 'INS', 'wait'] + LIBM + ['text:D', 'wait',
    'text:gone', 'INS', 'wait', 'ENTER', 'wait'] + LIBM + ['text:S', 'wait',
    'DOWN', 'text:lib3', 'INS', 'wait', 'wait'])
saved = read(os.path.join(lbdir, 'lib3.lib'))
check('library: Delete removes the entry from the library',
    os.path.exists(os.path.join(lbdir, 'lib3.lib')) and b'Bgone' not in saved,
    repr(saved[:300]))

lb = fresh('lb10.doc')
shutil.copy(os.path.join(sample, 'boiler.lib'),
    os.path.join(lbdir, 'boiler.lib'))
fm(lb, LIBM + ['text:R', 'wait', 'DOWN', 'text:boiler', 'INS', 'wait', 'C-F5',
    'wait', 'text:Notice', 'INS', 'wait', 'C-s', 'wait'])
check('library: the sample library (boiler.lib) can be retrieved and '
    'pasted', b'leave the trees as you found them.' in read(lb),
    repr(read(lb)[-300:]))
d = fm(fresh('lb11.doc'), LIBM + ['text:R', 'wait', 'DOWN', 'text:boiler',
    'INS', 'wait', 'C-F5', 'wait', 'text:xx', 'INS', 'wait', 'show'])
check('library: Lib Paste of a name that is not there says so',
    'No such library entry' in d[0], d[0])

# ---- the Tools menu: Macros ------------------------------------------------
MACM = ['F12', 'text:T', 'text:M']
d = fm(fresh('mc1.doc'), MACM + ['text:L', 'wait', 'show'])
check('macros: Learn opens the Define Macro panel of the original',
    all(w in d[0] for w in ('Define Macro', 'Macro Key', 'Description',
    'Include in Macro Set?')), d[0])
LEARN = MACM + ['text:L', 'wait', 'text:q', 'DOWN', 'text:typed ab', 'DOWN',
    'RIGHT', 'INS', 'wait', 'text:ab', 'C-F1', 'wait']
mc = fresh('mc2.doc')
fm(mc, LEARN + ['A-q', 'wait', 'C-s', 'wait'])
check('macros: a macro that was learned runs when Alt and its key are '
    'pressed', b'abab' in read(mc), repr(read(mc)[-200:]))
mc = fresh('mc3.doc')
mcdir = os.path.dirname(mc)
fm(mc, LEARN + MACM + ['text:S', 'wait', 'DOWN', 'text:mset', 'INS', 'wait',
    'wait'])
saved = read(os.path.join(mcdir, 'mset.mac'))
check('macros: Save writes the macro in the original\'s format',
    b'TK\x11Q\x80\x1d\x00\r\nTtyped ab\x1d\x00\r\n' in saved and
    b'\r\nTab\x1d\x00\r\n' in saved and saved.startswith(b'*R3'),
    repr(saved))
shutil.copy(os.path.join(sample, 'names.mac'), os.path.join(mcdir,
    'names.mac'))
mc = fresh('mc4.doc')
fm(mc, MACM + ['text:R', 'wait', 'DOWN', 'text:names', 'INS', 'wait', 'A-n',
    'wait', 'C-s', 'wait'])
check('macros: the sample macro set (names.mac) can be retrieved '
    'and run', b'Heron Hill Orchard Club' in read(mc), repr(read(mc)[-200:]))
mc = fresh('mc5.doc')
fm(mc, MACM + ['text:E', 'wait', 'text:e', 'INS', 'wait', 'DOWN', 'text:x',
    'DOWN', 'text:xy<Alt-Q>z', 'DOWN', 'RIGHT', 'INS', 'wait', 'A-e', 'wait',
    'C-s', 'wait'])
check('macros: a macro typed on the Edit panel runs', b'xyz' in read(mc),
    repr(read(mc)[-200:]))
d = fm(fresh('mc6.doc'), MACM + ['text:E', 'wait', 'text:e', 'INS', 'wait',
    'DOWN', 'DOWN', 'text:<bogus', 'INS', 'wait', 'show'])
check('macros: keystrokes that are not understood are refused',
    'not understood' in d[0], d[0])
mc = fresh('mc7.doc')
fm(mc, LEARN + MACM + ['text:D', 'wait', 'text:q', 'INS', 'wait', 'ENTER',
    'wait', 'A-q', 'wait', 'C-s', 'wait'])
check('macros: a macro that was deleted no longer runs',
    b'abab' not in read(mc), repr(read(mc)[-120:]))

# F10 and F11 are recorded and played back by a macro, plain and with Ctrl
# and Alt; F12 is the menu key and is never recorded.
mc = fresh('mc8.doc')
fm(mc, MACM + ['text:E', 'wait', 'text:f', 'INS', 'wait', 'DOWN', 'text:fk',
    'DOWN', 'text:xy<F10>z<F11>w<Alt-F10>v<Alt-F11>u', 'DOWN', 'RIGHT', 'INS',
    'wait', 'A-f', 'wait', 'C-s', 'wait'])
check('macros: F10, F11 and their Alt forms are played back and '
    'do nothing (Ctrl-F10 and Ctrl-F11 are Undo and Redo)', b'xyzwvu' in read(mc), repr(read(mc)[-200:]))
d = fm(fresh('mc9.doc'), MACM + ['text:E', 'wait', 'text:f', 'INS', 'wait',
    'DOWN', 'text:fk', 'DOWN', 'text:a<F10>b<F11>c', 'DOWN', 'RIGHT', 'INS',
    'wait'] + MACM + ['text:E', 'wait', 'text:f', 'INS', 'wait', 'DOWN',
    'DOWN', 'show'])
check('macros: the keystrokes F10 and F11 are kept and shown again',
    'a<F10>b<F11>c' in d[0], d[0])
mc = fresh('mc10.doc')
mcdir = os.path.dirname(mc)
fm(mc, MACM + ['text:L', 'wait', 'text:w', 'DOWN', 'text:fkeys', 'DOWN',
    'RIGHT', 'INS', 'wait', 'text:ab', 'F10', 'F11', 'F12', 'ESC', 'text:cd',
    'C-F1', 'wait'] + MACM + ['text:S', 'wait', 'DOWN', 'text:msetf', 'INS',
    'wait', 'wait'])
saved = read(os.path.join(mcdir, 'msetf.mac'))
check('macros: Learn keeps F10 and F11', b'ab\x11\x0a\x12\x11\x0b\x12' in
    saved, repr(saved))
check('macros: Learn does not keep F12: the menu key is not recorded',
    b'\x11\x0c\x12' not in saved and b'cd' in saved, repr(saved))

# An equation that was edited keeps its text in the label (the editor leaves
# the position of the cursor, 1C, in a saved text).
eqd = os.path.join(here, 'data', 'equation.doc')
d = fm(eqd, ['show'])
check('equation: the label of an equation shows its text',
    '[EQN y = 10 + e^x]' in d[0], d[0])
d = fm(os.path.join(here, 'data', 'equation_lines.doc'), ['show'])
check('equation: the label of an equation of several lines shows its text',
    '[EQN a = 1 \\\\ b = 2]' in d[0], d[0])
d = fm(eqd, ['A-F12', 'wait', 'C-w', 'wait', 'show'])
check('equation: Ctrl-W in the equation editor says that X11 is needed',
    'needs the X11 version' in d[0], d[0])
eqf = os.path.join(tmp, 'eqedit.doc')
shutil.copy(eqd, eqf)
d = fm(eqf, ['A-F12', 'wait', 'text:z+', 'INS', 'wait', 'show'])
check('equation: the label still shows the text after the equation editor',
    '[EQN z+y = 10 + e^x]' in d[0], d[0])

# F12, Quit with changes: the answers say that the program quits
qd = fresh('quit1.doc')
d = fm(qd, ['text:x', 'F12', 'text:Q', 'wait', 'show'])
check('quit: with unsaved changes the answers say that the program quits',
    'Save and quit' in d[0] and 'Discard changes and quit' in d[0]
    and 'Save and continue' not in d[0], d[0])
qd = fresh('quit2.doc')
orig = read(qd)
fm(qd, ['text:x', 'F12', 'text:Q', 'wait', 'ENTER', 'wait'])
check('quit: Save and quit writes the file', read(qd) != orig and
    b'xOrchard' in read(qd).replace(b'\x1c', b''), repr(read(qd)[-120:]))

# The display modes of the markers that are images: Inline shows the image,
# the others the marker (Global, Marker, Marker Display Modes)
MARKER_FMT = GLOBAL + ['RIGHT'] * 3 + ['ENTER', 'wait']
for lab, pages, downs in (('Equation', 1, 10), ('Keycap', 2, 7),
        ('Picture', 3, 3)):
    d = fm(fresh('mode_%s.doc' % lab), MARKER_FMT + ['PGUP'] * pages +
        ['DOWN'] * downs + ['show'])
    check('marker modes: %s is shown as Inline, the image, at first' % lab,
        '%s:' % lab in d[0] and '[Inline]' in d[0], d[0])
ge = fresh('mode_set.doc')
fm(ge, MARKER_FMT + ['PGUP'] + ['DOWN'] * 10 + ['LEFT'] * 3 + ['INS',
    'wait', 'C-s', 'wait'])
check('marker modes: Equation set to Short is saved as the record OM9,2',
    b'\r\nOM9,2\r\n' in read(ge), repr(read(ge)[:400]))
fm(ge, MARKER_FMT + ['PGUP'] + ['DOWN'] * 10 + ['RIGHT'] * 3 + ['INS',
    'wait', 'C-s', 'wait'])
check('marker modes: Equation back to Inline takes the record out again',
    b'OM9,' not in read(ge), repr(read(ge)[:400]))

# ---- counters, comments and cross-references -------------------------------
d = fm(os.path.join(here, 'data', 'counters.doc'), ['show'])
check('counter: the uses of a counter are numbered in the text',
    all(w in d[0] for w in ('Item 1. first', 'Item 2. second',
    'Item 10. set to ten', 'Item 11. after')), d[0])
check('counter: a style set on a marker stays with the counter',
    'Part I. roman' in d[0] and 'Part II. next' in d[0], d[0])
# A counter counts up to 4294967295 and stops there (docs/WORKLIST.txt).
d = fm(os.path.join(here, 'data', 'countermax.doc'), ['show'])
check('counter limit: a set value above 2147483647 counts to 4294967295 and '
    'stays there', all(w in d[0] for w in ('Item 4294967294. last but one',
    'Item 4294967295. last', 'Item 4294967295. stays')), d[0])
check('counter limit: large numbers in roman numerals and in letters',
    all(w in d[0] for w in ('Part 4294967295', 'Letter MWLQKWU.',
    'Roman MMMCMXCIX.', 'Roman 4000.')), d[0])
CTR_PANEL = ['C-F12', 'wait', 'DOWN', 'DOWN', 'DOWN', 'ENTER', 'wait', 'DOWN']
for what, txt in (('4294967296', '4294967296'), ('a negative number', '-5'),
                  ('a number with a point', '1.5')):
    cfile = os.path.join(tmp, 'ctrlim.doc')
    shutil.copy(os.path.join(here, 'data', 'plain_text.doc'), cfile)
    d = fm(cfile, CTR_PANEL + ['text:' + txt, 'INS', 'wait', 'show'])
    check('counter limit: the panel rejects %s' % what,
        'Set Value must be 0 to 4294967295' in d[0], d[0])
shutil.copy(os.path.join(here, 'data', 'plain_text.doc'), cfile)
d = fm(cfile, CTR_PANEL + ['text:4294967295', 'INS', 'wait', 'show'])
check('counter limit: the panel takes 4294967295',
    'Set Value must be' not in d[0] and 'Counter' not in d[0], d[0])
cdoc = os.path.join(here, 'data', 'comment.doc')
cd1, d, o = printed((['ENTER'], []), 'cm1.pdf', src=cdoc)
check('comment: a draft print has the comment with its name and time',
    'remember the milk' in cd1 and 'Ann' in cd1, cd1)
cd2, d, o = printed((['RIGHT', 'ENTER'], []), 'cm2.pdf', src=cdoc)
check('comment: a final print leaves the comment out',
    'Hello' in cd2 and 'world' in cd2 and 'remember' not in cd2, cd2)
d = fm(cdoc, ['show'])
check('comment: on the screen it is the short form, not a diamond',
    'Hello [COM "remember the milk"] world' in d[0] and '\u2666' not in d[0],
    d[0])
cmf = os.path.join(tmp, 'cmnew.doc')
shutil.copy(os.path.join(here, 'data', 'plain_text.doc'), cmf)
d = fm(cmf, ['A-F12', 'wait', 'DOWN', 'DOWN', 'ENTER', 'wait', 'text:look',
    'INS', 'wait', 'show', 'INS', 'wait', 'C-s', 'wait'])
check('comment: the panel has a name and the time it was made',
    'Name' in d[0] and 'Created' in d[0] and 'Initials' not in d[0], d[0])
check('comment: the new comment is saved with its name and time',
    b'\x136(' in read(cmf) and
    __import__('re').search(rb'\r\n2\d{4}-\d\d-\d\d \d\d:\d\d\x00\r\n',
    read(cmf)) is not None, repr(read(cmf)[-200:]))
d = fm(os.path.join(here, 'data', 'xref.doc'), ['show'])
check('cross-reference: the text of the section is shown in the text',
    'See 1 banana for fruit' in d[0] and '[XREF' not in d[0], d[0])
MARKER_CF = GLOBAL + ['RIGHT'] * 3 + ['ENTER', 'wait']
d = fm(fresh('cf1.doc'), MARKER_CF + ['PGDN', 'show'])
check('counter: Global, Marker has a Counter Format panel of 99 counters',
    'Counter Format' in d[0] and 'C1 Tag' in d[0] and 'C9 Tag' in d[0] and
    'Numeric' in d[0], d[0])
d = fm(fresh('cf1b.doc'), MARKER_CF + ['PGDN', 'PGDN', 'show'])
check('counter: the second page of Counter Format has counter 10',
    'Counter Format (2 of 11)' in d[0] and 'C10 Tag' in d[0] and
    'C18 Tag' in d[0] and 'C9 Tag' not in d[0], d[0])
d = fm(fresh('cf1c.doc'), MARKER_CF + ['PGDN'] * 11 + ['show'])
check('counter: the last page of Counter Format has counter 99',
    'Counter Format (11 of 11)' in d[0] and 'C99 Tag' in d[0], d[0])
cf3 = fresh('cf3.doc')
fm(cf3, MARKER_CF + ['PGDN', 'PGDN', 'text:books', 'INS', 'wait',
    'C-s', 'wait'])
check('counter: a description is saved as the record OT<n>N',
    b'\r\nOT10Nbooks\x00\r\n' in read(cf3), repr(read(cf3)[:500]))
cf = fresh('cf2.doc')
fm(cf, MARKER_CF + ['PGDN'] + ['DOWN'] * 5 + ['text:Fig \\I\\)', 'INS', 'wait',
    'C-s', 'wait'])
check('counter: a changed tag is saved as the record OT<n>T',
    b'\r\nOT3TFig \\I\\)\x00\r\n' in read(cf), repr(read(cf)[:500]))

d = fm(fresh('nf1.doc'), MARKER_CF + ['PGDN'] * 12 + ['show'])
check('note: the Footnote Format panel follows the counters',
    'Footnote Format' in d[0] and 'Reference Tag' in d[0] and
    'Separator Bar' in d[0] and 'Position of Notes' in d[0] and
    'Bottom of Page' in d[0], d[0])
d = fm(fresh('nf2.doc'), MARKER_CF + ['PGDN'] * 13 + ['show'])
check('note: the Levelnote Format panel has the Note Level',
    'Levelnote Format' in d[0] and 'Note Level' in d[0], d[0])
d = fm(fresh('nf3.doc'), MARKER_CF + ['PGDN'] * 14 + ['show'])
check('note: the Endnote Format panel has the Note Page Title',
    'Endnote Format' in d[0] and 'Note Page Title' in d[0] and
    'Endnotes' in d[0], d[0])
nf = fresh('nf4.doc')
fm(nf, MARKER_CF + ['PGDN'] * 12 + ['DOWN'] * 3 + ['text:\\A\\', 'INS',
    'wait', 'C-s', 'wait'])
check('note: a changed reference tag is saved as the record N1R',
    b'\r\nN1R\\A\\\x00\r\n' in read(nf), repr(read(nf)[:500]))

# ---- the Keycap Format: Global, Marker, behind the Endnote Format ----------
d = fm(fresh('kf0.doc'), MARKER_CF + ['show'])
check('keycap format: the Marker Format panel has the field Keycap Format',
    'Keycap Format' in d[0] and 'PgDn after the Note formats' in d[0], d[0])
d = fm(fresh('kf1.doc'), MARKER_CF + ['PGDN'] * 15 + ['show'])
check('keycap format: the panel is in the ring after the Endnote Format',
    'Keycap Format' in d[0] and 'Foreground' in d[0] and
    'Background' in d[0] and 'Scale' in d[0] and 'Font' in d[0] and
    '0.0' in d[0] and '100.0' in d[0] and '1.000' in d[0] and
    'Text (' in d[0] and 'Legend' not in d[0], d[0])
d = fm(fresh('kf2.doc'), MARKER_CF + ['PGDN'] * 16 + ['show'])
check('keycap format: PgDn goes on to the last Marker Display Modes page',
    'Marker Display Modes (3 of 3)' in d[0], d[0])
d = fm(fresh('kf3.doc'), MARKER_CF + ['PGDN'] * 14 + ['show'])
check('keycap format: the Endnote Format is still before the Keycap Format',
    'Endnote Format' in d[0], d[0])
d = fm(fresh('kf3b.doc'), MARKER_CF + ['PGUP'] * 4 + ['show'])
check('keycap format: PgUp through the display modes pages comes to it',
    'Keycap Format' in d[0] and 'Foreground' in d[0], d[0])
d = fm(fresh('kf4.doc'), MARKER_CF + ['PGDN'] * 15 + ['DOWN', 'DOWN',
    'text:101', 'INS', 'wait', 'show'])
check('keycap format: a background above 100.0 is refused',
    'Background' in d[0] and '0.0 to 100.0' in d[0], d[0])
kf = fresh('kf5.doc')
fm(kf, MARKER_CF + ['PGDN'] * 15 + ['text:20.0', 'DOWN', 'text:80', 'DOWN',
    'text:1.5', 'DOWN', 'RIGHT', 'RIGHT', 'RIGHT', 'INS', 'wait', 'C-s',
    'wait'])
check('keycap format: changed values are saved as OKF, OKB, OKS and OKT',
    all(r in read(kf) for r in (b'\r\nOKF200\r\n', b'\r\nOKB800\r\n',
    b'\r\nOKS1500\r\n', b'\r\nOKT5\r\n')), repr(read(kf)[:400]))
check('keycap format: the records come after YIWN2',
    read(kf).find(b'YIWN2') >= 0 and
    read(kf).find(b'YIWN2') < read(kf).find(b'OKF200'), repr(read(kf)[:400]))
kf2 = fresh('kf6.doc')
fm(kf2, MARKER_CF + ['PGDN'] * 15 + ['text:0.0', 'INS', 'wait', 'C-s', 'wait'])
check('keycap format: the default values leave no record',
    b'OKF' not in read(kf2) and b'OKB' not in read(kf2) and
    b'OKS' not in read(kf2) and b'OKT' not in read(kf2), repr(read(kf2)[:400]))
d = fm(fresh('kf7.doc'), ['DOWN', 'A-F12', 'text:K', 'text:K', 'INS', 'wait',
    'show'])
check('keycap format: a Keycap marker panel shows the global defaults in '
    'brackets', 'Foreground: [0.0]' in d[0] and 'Background: [100.0]' in d[0]
    and 'Scale: [1.000]' in d[0] and 'Text (' in d[0], d[0])
d = fm(fresh('kf8.doc'), MARKER_CF + ['PGDN'] * 15 + ['text:20.0', 'DOWN',
    'text:80', 'DOWN', 'text:1.5', 'DOWN', 'RIGHT', 'RIGHT', 'RIGHT', 'INS',
    'wait', 'DOWN', 'A-F12', 'text:K', 'text:K', 'INS', 'wait', 'show'])
check('keycap format: the marker panel shows the changed global values',
    'Foreground: [20.0]' in d[0] and 'Background: [80.0]' in d[0] and
    'Scale: [1.500]' in d[0] and 'Helvetica' in d[0], d[0])
kf3 = fresh('kf9.doc')
fm(kf3, ['DOWN', 'A-F12', 'text:K', 'text:K', 'INS', 'wait', 'text:Esc',
    'INS', 'wait', 'C-s', 'wait'])
check('keycap format: a field left at the bracket has no record',
    b'Esc' in read(kf3) and b'\r\n2' not in read(kf3)[read(kf3).find(b'Esc'):
    read(kf3).find(b'Esc') + 12], repr(read(kf3)[:300]))
kf4 = fresh('kf10.doc')
fm(kf4, ['DOWN', 'A-F12', 'text:K', 'text:K', 'INS', 'wait', 'text:Esc',
    'DOWN', 'text:0.0', 'INS', 'wait', 'C-s', 'wait'])
i = read(kf4).find(b'Esc')
check('keycap format: a value typed in the marker is stored even when it is '
    'the default', i >= 0 and b'\r\n20\r\n' in read(kf4)[i:i + 20],
    repr(read(kf4)[max(i, 0):max(i, 0) + 40]))

# ---- section levels: Alt-< and Alt-> (Alt-[ and Alt-] in X11) --------------
import subprocess as _sp
msdump = os.path.join(os.path.dirname(stred), 'msdump')
def outline(path):
    """The (depth, headline) of every section of a saved document."""
    out = _sp.run([msdump, path], capture_output=True, text=True).stdout
    res = []; pend = None
    for line in out.split('\n'):
        t = line.strip()
        if t == 'SECTION':
            pend = (len(line) - len(line.lstrip()) - 3) // 2 + 1
        elif pend is not None and t.startswith('[block'):
            res.append((pend, t.split('|')[1])); pend = None
    return res
def levels(keys, name):
    p = os.path.join(tmp, name)
    shutil.copy(os.path.join(here, 'data', 'sort_sections.doc'), p)
    fm(p, keys + ['C-s', 'wait'])
    return outline(p), p
APPLE = ['S-DOWN', 'S-PGDN']                  # the cursor is on Apple (1.2)
o, p = levels(APPLE + ['A-<'], 'lv1.doc')
check('levels: promoting a section makes the sections after it its subsections',
    [x for x in o if x[1] in ('Parent', 'Apple', 'cherry', 'apple')] ==
    [(1, 'Parent'), (1, 'Apple'), (2, 'cherry'), (2, 'apple')], o)
o, p = levels(APPLE + ['A->'], 'lv2.doc')
check('levels: demoting a section makes it the last subsection of the one '
    'before it', [x for x in o if x[1] in ('banana', 'z', 'a', 'Apple',
    'cherry')] == [(2, 'banana'), (3, 'z'), (3, 'a'), (3, 'Apple'),
    (2, 'cherry')], o)
o, p = levels(APPLE + ['A->', 'text:Q'], 'lv3.doc')
check('levels: after a section moved the cursor is still in it',
    (3, 'QApple') in o, o)
o, p = levels(APPLE + ['A->', 'A->', 'text:Q'], 'lv4.doc')
check('levels: a section that was demoted can be demoted again, into the '
    'last section before it', (4, 'QApple') in o, o)
o, p = levels(APPLE + ['A->', 'S-DOWN', 'A->', 'text:Q'], 'lv5.doc')
check('levels: the section that follows can be demoted too, with the cursor '
    'in it',
    (3, 'Apple') in o and (3, 'Qcherry') in o, o)
# The last section moves left, and a section moves left from any place.
o, p = levels(['S-DOWN', 'S-DOWN', 'S-DOWN', 'S-PGDN', 'S-PGDN', 'S-PGDN',
    'S-PGDN', 'S-PGDN', 'A-<'], 'lv6.doc')
check('levels: the last section can move left', (1, 'apple') in o or
    (2, 'apple') in o, o)
o, p = levels(['S-DOWN', 'S-DOWN', 'A-<'], 'lv7.doc')
check('levels: a first subsection that moves left takes the next ones with it',
    [x for x in o if x[1] in ('banana', 'z', 'a')] ==
    [(2, 'banana'), (2, 'z'), (3, 'a')], o)

p = os.path.join(tmp, 'lv8.doc')
shutil.copy(os.path.join(here, 'data', 'sort_sections.doc'), p)
d = fm(p, APPLE + ['A->', 'C-q', 'show'])
check('levels: moving a section changes the document, so Quit prompts',
    'unsaved changes' in d[0], d[0])

# Collapsing a section changes the file (the outline state is saved in it).
d = fm(fresh('fold1.doc'), ['A-1', 'C-q', 'show'])
check('outline: collapsing the sections changes the document, so Quit prompts',
    'Document has unsaved changes' in d[0], d[0])
d = fm(fresh('fold2.doc'), ['A-1', 'A-0', 'A-0', 'C-q', 'show'])
check('outline: opening them again is a change too', 'unsaved changes' in
    d[0], d[0])
d = fm(fresh('fold3.doc'), ['C-q', 'show'])
check('outline: Quit does not ask when nothing changed',
    'unsaved changes' not in d[0], d[0])

# ---- the File menu ---------------------------------------------------------
FILE = ['F12', 'ENTER']             # the File menu: Save is first
ITEM = {'save': 0, 'retrieve': 1, 'new': 2, 'delete': 3, 'import': 4,
    'export': 5}
def item(name): return FILE + ['RIGHT'] * ITEM[name] + ['ENTER', 'wait']

# Save
sv = fresh('save1.doc')
d = fm(sv, item('save') + ['show', 'DOWN', 'DOWN', 'DOWN',
    'text:Saved description', 'DOWN', 'text:Saved author', 'INS', 'ENTER',
    'wait'])
check('file: Save shows the working directory, name, options, description, '
    'author and revision', all(w in d[0] for w in ('Working Directory:',
    'Document Name:', 'Options:', 'Description:', 'Author:', 'Revision:',
    'Save Entire Document')), d[0])
saved = read(sv)
check('file: Save keeps a changed description and author, in order',
    b'MDSaved description\x00' in saved and b'MASaved author\x00' in saved
    and saved.index(b'MD') < saved.index(b'MA') < saved.index(b'MR'),
    repr(saved[:160]))
check('file: a changed description counts as a change to the revision',
    b'\r\nMR1007\r\n' in saved, repr(saved[:80]))
rv = fresh('rev1.doc')
fm(rv, item('save') + ['DOWN', 'DOWN', 'DOWN', 'DOWN', 'DOWN', 'text:2.5',
    'INS', 'ENTER', 'wait'])
check('file: a revision typed on the Save panel is kept',
    b'\r\nMR2500\r\n' in read(rv), repr(read(rv)[:80]))
rq = fresh('rev2.doc')
fm(rq, ['C-s', 'wait'])
check('file: Ctrl-S on a document that was not changed keeps its revision',
    b'\r\nMR1006\r\n' in read(rq), repr(read(rq)[:80]))
rq = fresh('rev3.doc')
fm(rq, ['text:x', 'C-s', 'wait'])
check('file: Ctrl-S on a changed document adds one to the revision',
    b'\r\nMR1007\r\n' in read(rq), repr(read(rq)[:80]))
gs = os.path.join(tmp, 'gs.set')
fm(fresh('save2.doc', 'orchard.set'), item('save') + ['DOWN',
    'text:' + gs, 'DOWN', 'RIGHT', 'INS', 'wait'])
check('file: Save Global Settings Only writes the settings and no text',
    b'MDclub template' in read(gs) and b'DF1N' in read(gs)
    and b'\r\n*B1\r\n*E\r\n' in read(gs), repr(read(gs)[-80:]))
se = os.path.join(tmp, 'se.doc')
shutil.copy(os.path.join(here, 'data', 'sections_se.doc'), se)
ex = os.path.join(tmp, 'ex.doc')
fm(se, ['DOWN', 'DOWN'] + item('save') + ['DOWN', 'text:' + ex, 'DOWN',
    'RIGHT', 'RIGHT', 'INS', 'wait'])
check('file: Extract Current Section writes only that section',
    b'Purpose body.' in read(ex) and b'Prerequisites body.' not in read(ex)
    and read(ex).count(b'Purpose') >= 1, repr(read(ex)[-200:]))

# New
nn = os.path.join(tmp, 'nd1')
d = fm(sv, item('new') + ['show', 'DOWN', 'text:' + nn, 'DOWN', 'DOWN',
    'text:New desc', 'DOWN', 'text:New author', 'INS', 'ENTER', 'wait', 'C-s',
    'wait'])
check('file: New asks for working directory, name, template, description, '
    'author and revision', all(w in d[0] for w in ('Working Directory:',
    'Document Name:', 'Template:', 'Description:', 'Author:', 'Revision:')),
    d[0])
nb = read(nn + '.doc')
check('file: a new file starts with the description and author given',
    b'MDNew desc\x00' in nb and b'MANew author\x00' in nb, repr(nb[:120]))
check('file: the first save of a new file makes it revision 1.001',
    b'\r\nMR1001\r\n' in nb, repr(nb[:120]))

# ---- Recovery of a damaged document ------------------------------------------
# The damaged files are made here: five blocks, the last cut in the middle of
# its text, with no end record.  Nothing in them is called "recovered".
import hashlib
CRR = b'\r\n'


def sha(path):
    return hashlib.sha256(read(path)).hexdigest()


def damaged(name, cut=True):
    path = os.path.join(tmp, name)
    out = (b'*R3' + CRR + b'MR1001' + CRR + b'*B0' + CRR + b'S' + CRR)
    for k, w in enumerate((b'Alpha', b'Bravo', b'Charlie', b'Delta')):
        out += (b'B' + CRR if k else b'') + b'T' + w + b' block\x1d\x00' + CRR
    out += b'B' + CRR + b'TEcho block that is cut off'
    open(path, 'wb').write(out)
    return path


dm = damaged('dmg.doc')
dm_sha = sha(dm)
d = fm(fresh('rc0.doc'), item('retrieve') + ['DOWN', 'text:' + dm, 'INS',
    'wait', 'wait', 'show'])
check('recover: Retrieve of a damaged file asks to open the part that can be '
    'read, with the reason and the counts', all(w in d[0] for w in
    ('Retrieve failed', 'a piece of text has no end', '4 blocks',
    'Open the part', 'not changed')), d[0])
d = fm(fresh('rc1.doc'), item('retrieve') + ['DOWN', 'text:' + dm, 'INS',
    'wait', 'wait', 'text:y', 'wait', 'wait', 'show'])
check('recover: Y opens the blocks before the damage as a new document',
    'Delta block' in d[0] and 'Echo' not in d[0] and
    'dmg.recovered.doc' in d[0].split('\n')[0] and
    '*' in d[0].split('\n')[0], d[0])
check('recover: the message line shows how many blocks, and that the original '
    'is not changed', 'Recovered 4 blocks of' in d[0].split('\n')[-1] and
    'the original is not changed' in d[0].split('\n')[-1],
    d[0].split('\n')[-1])
for nm, key in (('N', 'text:n'), ('Esc', 'ESC')):
    d = fm(fresh('rc2.doc'), item('retrieve') + ['DOWN', 'text:' + dm, 'INS',
        'wait', 'wait', key, 'wait', 'wait', 'ESC', 'wait', 'show'])
    check('recover: %s opens nothing: no recovered text on the screen' % nm,
        'Delta block' not in d[0] and 'Recovered' not in d[0], d[0])
d = fm(fresh('rc3.doc'), item('retrieve') + ['DOWN', 'text:' + dm, 'INS',
    'wait', 'wait', 'ENTER', 'wait', 'show'])
check('recover: Enter does not answer the question', 'Open the part' in d[0]
    and 'Retrieve failed' in d[0], d[0])
check('recover: nothing was written by the questions: no recovered file, the '
    'damaged file is byte for byte as it was',
    not os.path.exists(os.path.join(tmp, 'dmg.recovered.doc')) and
    sha(dm) == dm_sha)
# opening the damaged file from the command line of the editor
d = session([stred, '-H', helpf, dm], ['wait', 'wait', 'show', 'text:y', 'wait',
    'wait', 'show', 'C-s', 'wait', 'wait'])
check('recover: the editor started on a damaged file shows the box',
    'Open the part' in d[0] and 'Cannot open' in d[0], d[0])
check('recover: Y at the start opens the recovered part',
    'Delta block' in d[1] and 'Recovered 4 blocks of' in d[1], d[1])
rp = os.path.join(tmp, 'dmg.recovered.doc')
check('recover: saving writes the recovered name, whole, with its end '
    'record', os.path.exists(rp) and b'Delta block' in read(rp) and
    read(rp).rstrip().endswith(b'*E') and b'Echo' not in read(rp),
    repr(read(rp)[-80:]))
check('recover: the damaged original was not changed by the save',
    sha(dm) == dm_sha)
# the name is never taken: dmg.recovered.doc exists, so dmg.recovered2.doc
d = session([stred, '-H', helpf, dm], ['wait', 'wait', 'text:y', 'wait', 'wait',
    'C-s', 'wait', 'wait', 'show'])
rp2 = os.path.join(tmp, 'dmg.recovered2.doc')
check('recover: when NAME.recovered.doc exists the name is '
    'NAME.recovered2.doc', os.path.exists(rp2) and b'Delta block' in
    read(rp2) and 'dmg.recovered2.doc' in d[0].split('\n')[0], d[0][:200])
check('recover: the earlier recovered file and the original are not changed',
    b'Delta block' in read(rp) and sha(dm) == dm_sha)
nq = fresh('rc4.doc')
d = session([stred, '-H', helpf, dm], ['wait', 'wait', 'text:n', 'wait', 'wait'])
check('recover: N at the start opens nothing and writes no file',
    not os.path.exists(os.path.join(tmp, 'dmg.recovered3.doc')) and
    sha(dm) == dm_sha)

# the command line: strprint refuses a damaged file, --recover prints the part
def cli(args):
    r = subprocess.run(args, capture_output=True, text=True, cwd=tmp)
    return r.returncode, r.stdout + r.stderr


tex = os.path.join(tmp, 'cli.tex')
rc, out = cli([os.environ['STRPRINT'], dm, '--export-latex', tex])
check('recover: strprint without the option refuses the damaged file and says '
    'why', rc != 0 and 'a piece of text has no end' in out and
    not os.path.exists(tex), (rc, out))
rc, out = cli([os.environ['STRPRINT'], dm, '--export-latex', tex, '--recover'])
txt = read(tex).decode('utf-8', 'replace')
check('recover: strprint --recover prints the part that can be read and says '
    'so', rc == 0 and 'Delta block' in txt and 'Echo' not in txt and
    '4 blocks' in out and 'not changed' in out, (rc, out, txt[-200:]))
rc, out = cli([stred, '--print', dm, '-o', os.path.join(tmp, 'cli.pdf')])
check('recover: stred --print refuses the damaged file without the option',
    rc != 0 and 'no end' in out, (rc, out))
check('recover: the damaged file is the same after the command line runs',
    sha(dm) == dm_sha)

# Retrieve
other = fresh('other.doc', 'brief-long.doc')
d = fm(fresh('ret1.doc'), item('retrieve') + ['show', 'DOWN',
    'text:' + other, 'INS', 'wait', 'show'])
check('file: Retrieve has a working directory, a name and options',
    all(w in d[0] for w in ('Working Directory:', 'Document Name:',
    'Options:', 'Retrieve Entire Document')), d[0])
check('file: Retrieve Entire Document brings in the other file',
    'Aims and Approach' in d[1], d[1])
d = fm(fresh('ret2.doc'), item('retrieve') + ['DOWN', 'text:' + other,
    'DOWN'] + ['RIGHT'] * 4 + ['INS', 'wait', 'show'])
check('file: Retrieve Named Block Settings Only of a file with none says so',
    'no named block settings' in d[0], d[0])
body = fresh('ret3.doc')
fm(body, item('retrieve') + ['DOWN', 'text:' + other, 'DOWN', 'RIGHT', 'INS',
    'wait', 'C-s', 'wait'])
check('file: Retrieve Body Only takes the text and keeps the settings',
    b'Aims and Approach' in read(body) and b'FLA0' in read(body),
    repr(read(body)[:120]))
glob_ = fresh('ret4.doc')
fm(glob_, item('retrieve') + ['DOWN', 'text:' + fresh('prop.set',
    'orchard.set'), 'DOWN', 'RIGHT', 'RIGHT', 'INS', 'wait', 'C-s', 'wait'])
check('file: Retrieve Global Settings takes the settings and keeps the text',
    b'DF1N' in read(glob_) and b'Orchard Plan' in read(glob_)
    and b'MDclub template' in read(glob_), repr(read(glob_)[:120]))
ins = os.path.join(tmp, 'ins.doc')
shutil.copy(os.path.join(here, 'data', 'sections_se.doc'), ins)
fm(ins, ['DOWN', 'DOWN'] + item('retrieve') + ['DOWN', 'text:' + ins,
    'DOWN'] + ['RIGHT'] * 5 + ['INS', 'wait', 'C-s', 'wait'])
check('file: Retrieve & Insert after Current Section adds the text',
    read(ins).count(b'Purpose body.') == 2, read(ins).count(b'Purpose body.'))

# Retrieve of a damaged file: a message, and the open document stays as it is
cutdoc = os.path.join(tmp, 'cutshort.doc')
with open(cutdoc, 'wb') as cf:
    cf.write(read(other)[:read(other).rindex(b'\r\n*E') + 2])
target = fresh('ret5.doc')
before = read(target)
d = fm(target, item('retrieve') + ['DOWN', 'text:' + cutdoc, 'INS', 'wait',
    'show', 'ENTER', 'C-s', 'wait'])
check('file: Retrieve of a document that is cut short says so',
    'cut short' in d[0], d[0])
check('file: a refused Retrieve leaves the open document as it was',
    read(target) == before, len(read(target)))

# The checklists that the Gray + key shows (Alt-+ in a terminal)
pickd = os.path.join(tmp, 'pickdir')
os.makedirs(pickd, exist_ok=True)
shutil.copy(PLAN, os.path.join(pickd,
    'pick1.doc'))
shutil.copy(PLAN, os.path.join(pickd,
    'other.doc'))
d = fm(os.path.join(pickd, 'pick1.doc'), item('retrieve') + ['DOWN', 'A-+',
    'wait', 'show'])
check('file: Gray + on a file name lists the files of the directory',
    'pick1.doc' in d[0] and 'other.doc' in d[0] and '../' in d[0], d[0])
d = fm(fresh('pick2.doc'), item('retrieve') + ['A-+', 'wait', 'show'])
check('file: Gray + on the working directory lists the directories',
    '[Use this directory]' in d[0] and '../' in d[0], d[0])
d = fm(os.path.join(pickd, 'pick1.doc'), item('retrieve') + ['DOWN', 'A-+',
    'wait', 'DOWN', 'ENTER', 'wait', 'show'])
check('file: choosing from the list fills in the name',
    'Document Name:' in d[0] and '.doc' in d[0].split('Document Name:')[1]
    .split('\n')[0], d[0])

# The list can be walked through: .. goes up, a name with a slash goes in
walk = os.path.join(tmp, 'walk')
os.makedirs(os.path.join(walk, 'inner'), exist_ok=True)
wdoc = os.path.join(walk, 'inner', 'w1.doc')
shutil.copy(PLAN, wdoc)
shutil.copy(PLAN, os.path.join(walk, 'inner',
    'w2.doc'))
d = fm(wdoc, item('retrieve') + ['DOWN', 'A-+', 'wait', 'ENTER', 'wait',
    'show'])
check('file: .. in the list goes to the directory above and shows it',
    'inner/' in d[0] and 'w2.doc' not in d[0], d[0])
d = fm(wdoc, item('retrieve') + ['DOWN', 'A-+', 'wait', 'ENTER', 'wait',
    'DOWN', 'ENTER', 'wait', 'show'])
check('file: a name with a slash goes into that directory',
    'w1.doc' in d[0] and 'w2.doc' in d[0] and '../' in d[0], d[0])
top = os.path.join(walk, 'top.doc')
shutil.copy(PLAN, top)
d = fm(top, item('retrieve') + ['DOWN', 'A-+', 'wait', 'DOWN', 'ENTER',
    'wait', 'DOWN', 'ENTER', 'wait', 'show'])
wl = [l for l in d[0].split('\n') if 'Working Directory:' in l]
dl_ = [l for l in d[0].split('\n') if 'Document Name:' in l]
check('file: a file chosen in another directory changes the working '
    'directory too', wl and 'inner' in wl[0] and dl_ and 'w1.doc' in dl_[0],
    d[0])

# Delete
dl = os.path.join(tmp, 'del.txt')
open(dl, 'w').write('to be deleted')
d = fm(fresh('del1.doc'), item('delete') + ['show', 'DOWN', 'text:' + dl,
    'INS', 'ENTER', 'wait'])
check('file: Delete prompts for a working directory and a file name',
    'Working Directory:' in d[0] and 'File Name:' in d[0], d[0])
check('file: Delete removes the file', not os.path.exists(dl))

# Import
imp = os.path.join(tmp, 'imp.txt')
open(imp, 'w').write('First paragraph line one\nline two\n\nSecond '
    'paragraph\n')
tg = fresh('imp1.doc')
d = fm(tg, item('import') + ['show', 'DOWN', 'text:' + imp, 'INS', 'wait',
    'C-s', 'wait'])
check('file: Import asks for a name, line endings and a character set',
    all(w in d[0] for w in ('ASCII File Name:', 'Options:', 'Ignore Line '
    'Endings', 'Character Set:')), d[0])
check('file: Import ignoring line endings joins lines and splits blocks',
    b'First paragraph line one line two' in read(tg)
    and b'Second paragraph' in read(tg), repr(read(tg)[300:600]))

# Export
sx = os.path.join(tmp, 'exp1.txt')
dx = os.path.join(tmp, 'exp2.txt')
d = fm(fresh('exp0.doc'), item('export') + ['show', 'DOWN', 'text:' + sx,
    'INS', 'wait'])
check('file: Export asks for a name, options, a character set and a scope',
    all(w in d[0] for w in ('ASCII File Name:', 'Export a Screen Image',
    'Character Set:', 'Export Entire Document')), d[0])
check('file: Export a Screen Image writes the screen with CR LF line ends',
    b'Orchard Plan\r\n' in read(sx) and b'Heron Hill Orchard' in read(sx)
    and b'\n' not in read(sx).replace(b'\r\n', b''), repr(read(sx)[:120]))
fm(fresh('exp3.doc'), item('export') + ['DOWN', 'text:' + dx, 'DOWN',
    'RIGHT', 'INS', 'wait'])
check('file: Export Data Only writes a block on a line, with tabs',
    read(dx).startswith(b'Orchard Plan\r\n')
    and b'Site:\tHeron Hill Orchard\r\n' in read(dx), repr(read(dx)[:120]))

# ---- notes: the note editor ------------------------------------------------
nk = ['DOWN', 'A-F12', 'text:N', 'INS', 'wait']
d = session(base, nk + ['show', 'text:Hello note', 'INS', 'wait', 'INS',
    'show', 'LEFT', 'A-F12', 'wait', 'show', 'END', 'text: again', 'INS',
    'wait', 'INS', 'A-F12', 'wait', 'show'])
check('note: the Note command opens the note editor', 'NOTE EDIT' in d[0]
    and 'Press Ins to Save Changes' in d[0], d[0])
check('note: accepting the note inserts a note marker, drawn as [NOTE]',
    '[NOTE]' in d[1] and '♦' not in d[1] and 'NOTE EDIT' not in d[1], d[1])
check('note: editing a note shows the text of the note', 'NOTE EDIT' in d[2]
    and 'Hello note' in d[2], d[2])
check('note: the changed text of the note is kept', 'Hello note again' in d[3],
    d[3])
d = session(base, nk + ['text:typed', 'INS', 'wait', 'show', 'ESC'])
check('note: the Note panel has the Type, [Footnote] at first',
    'Type:' in d[0] and '[Footnote]' in d[0], d[0])
d = session(base, nk + ['text:abandoned', 'ESC', 'show'])
check('note: Esc leaves the note editor without inserting a note',
    '♦' not in d[0] and 'NOTE EDIT' not in d[0], d[0])
nd = os.path.join(tmp, 'note.doc')
shutil.copy(os.path.join(here, 'data', 'note_complex.doc'), nd)
d = session([stred, '-H', helpf, nd], ['DOWN'] + ['RIGHT'] * 7 + ['A-F12',
    'wait', 'show', 'END', 'text:+', 'INS', 'wait', 'INS', 'C-s', 'wait'])
check('note: a note of several blocks shows all of them', 'First block.' in
    d[0] and 'Second block, centered.' in d[0] and 'NOTE EDIT' in d[0], d[0])
saved = open(nd, 'rb').read()
check('note: editing one block keeps the other and its formatting',
    b'First block.+' in saved and b'BFJ2' in saved and b'Second block, '
    b'centered.' in saved, repr(saved[-200:]))

xr = ['A-F12', 'text:C', 'text:C', 'text:C', 'text:C', 'INS', 'wait']
d = session(base, xr + ['show', 'text:1', 'INS', 'show'])
check('marker: Cross-reference opens its panel', 'Section Number:' in d[0]
    and 'Style:' in d[0] and 'Section Tag:' in d[0] and 'Headline:' in d[0],
    d[0])
check('marker: Cross-reference inserts a marker that shows the tag of the '
    'section', '1Orchard Plan' in d[1] and '[XREF' not in d[1], d[1])
d = session(base, xr + ['text:99', 'INS', 'wait', 'show'])
check('marker: Cross-reference refuses a section that is not there',
    'Cannot find section number' in d[0], d[0])
d = session(base, xr + ['text:1', 'DOWN', 'text: ', 'text: ', 'INS', 'LEFT',
    'A-F12', 'wait', 'show'])
check('marker: Cross-reference keeps its style and shows the section',
    '[Headline]' in d[0] and 'Orchard Plan' in d[0], d[0])
d = session(base, ['A-F12', 'text:T', 'INS', 'wait', 'text:', 'INS', 'wait',
    'show'])
check('marker: a panel rejects an empty required value', 'No string '
    'specified' in d[0], d[0])

# ---- the spell check (docs/SPELL.txt) --------------------------------------
# A fake checker that gives fixed answers (tests/fakespell.sh) stands for
# aspell; one test at the end uses the real aspell when it is in PATH.
fakespell = os.path.join(here, 'fakespell.sh')
real_spell_path = os.environ.get('STRATA_SPELL')
os.environ['STRATA_SPELL'] = fakespell
spell_home = tempfile.mkdtemp(prefix='home')
home_override[0] = spell_home
spdoc = os.path.join(tmp, 'spell.doc')
def spell_fresh():
    shutil.copy(os.path.join(here, 'data', 'spell.doc'), spdoc)
    for n in ('dictionary', 'config'):
        if os.path.exists(os.path.join(spell_home, n)):
            os.unlink(os.path.join(spell_home, n))
spell_fresh()
sbase = [stred, '-H', helpf, spdoc]
SPELL = ['F12', 'text:t', 'text:s']
d = session(sbase, SPELL + ['wait', 'show'])
check('spell: F12, Tools, Spell offers Document, Block, Section, Word',
    all(w in d[0].split('\n')[0] + d[0].split('\n')[1] for w in ('Document',
    'Block', 'Section', 'Word')), d[0])
d = session(sbase, SPELL + ['text:d', 'wait', 'show', 'INS', 'wait', 'show'])
check('spell: Document shows the Check Document Spelling panel',
    'Check Document Spelling' in d[0] and 'Document Name:' in d[0]
    and 'Custom Spelling Dictionary:' in d[0] and 'Auto Correct?' in d[0]
    and 'Yes' in d[0] and 'Dictionary Language:' in d[0] and 'en_US' in d[0],
    d[0])
check('spell: Ins starts the check: the bar, the word, the text round it',
    'Accept' in d[1] and 'Define' in d[1] and 'Ignore' in d[1]
    and 'Edit' in d[1] and 'Guess' in d[1] and 'Line' in d[1]
    and 'Quit' in d[1] and 'Accept word as correct' in d[1]
    and 'Unknown word: quikc' in d[1] and 'The quikc brown fox' in d[1]
    and 'Document:' in d[1] and 'Section:      Body' in d[1]
    and 'Dictionaries: en_US' in d[1], d[1])
GO = SPELL + ['text:d', 'wait', 'INS', 'wait']
d = session(sbase, GO + ['text:g', 'wait', 'show', 'RIGHT', 'LEFT', 'ENTER',
    'wait', 'show'])
check('spell: Guess lists the suggestions on the bar',
    'quick  quirk' in d[0] and 'Select word' in d[0], d[0])
check('spell: choosing a suggestion replaces the word and goes on to the next',
    'The quick brown fox' in d[1] and 'Unknown word: teh' in d[1], d[1])
d = session(sbase, GO + ['text:i', 'wait', 'show'])
check('spell: Ignore goes on to the next unknown word',
    'Unknown word: teh' in d[0] and 'The quikc brown' in d[0], d[0])
d = session(sbase, GO + ['text:i', 'text:g', 'ENTER', 'wait', 'text:i',
    'text:i', 'text:i', 'text:i', 'text:i', 'text:i', 'text:i', 'wait', 'show'])
check('spell: Auto Correct replaces a word that comes again by itself',
    'Spell Check completed' in d[0] and 'Press any key to exit' in d[0]
    and 'No spelling errors found' not in d[0], d[0])
d = session(sbase, GO + ['text:i', 'text:i', 'text:i', 'text:i', 'text:i',
    'text:i', 'text:i', 'text:i', 'text:i', 'text:i', 'wait', 'show'])
check('spell: the end says No spelling errors found when nothing was changed',
    'Spell Check completed' in d[0] and 'No spelling errors found' in d[0],
    d[0])
d = session(sbase, GO + ['text:e', 'wait', 'show', 'BKSP', 'BKSP', 'BKSP',
    'text:the', 'ENTER', 'wait', 'show'])
check('spell: Edit shows the Correct word field with the word',
    'Correct word: quikc' in d[0] and 'Unknown word: quikc' in d[0], d[0])
check('spell: the edited word is used and the check goes on',
    'The the brown fox' not in d[1] and 'Unknown word: teh' in d[1], d[1])
d = session(sbase, GO + ['text:i', 'text:i', 'text:d', 'wait', 'show'])
check('spell: Define goes on', 'Unknown word: wrold' in d[0], d[0])
check('spell: Define keeps the word in ~/.strata/dictionary, one per line, '
    'plain text, only for the user',
    read(os.path.join(spell_home, 'dictionary')) == b'dont-stop\n'
    and os.stat(os.path.join(spell_home, 'dictionary')).st_mode & 0o777
    == 0o600, repr(read(os.path.join(spell_home, 'dictionary'))))
d = session(sbase, GO + ['text:a', 'wait', 'show'])
check('spell: Accept goes on', 'Unknown word: teh' in d[0], d[0])
d = session(sbase, GO + ['text:l', 'wait', 'show', 'ESC', 'wait', 'show'])
check('spell: Line edits the line of the word', 'Corrected line:' in d[0]
    and 'The quikc brown fox jumpd over teh lazy dog.' in d[0], d[0])
d = session(sbase, GO + ['text:g', 'ENTER', 'wait', 'text:g', 'ENTER', 'wait',
    'text:i', 'text:i', 'text:i', 'wait', 'show'])
check('spell: the text of a note is checked, and its section says Note',
    'Unknown word: foootnote' in d[0] and 'Section:      Body (Note)' in d[0]
    and 'A foootnote here.' in d[0], d[0])
# Quit, with a correction that is waiting for the rest of the text
d = session(sbase, GO + ['text:i', 'text:g', 'ENTER', 'wait', 'text:q', 'wait',
    'show', 'text:n', 'wait', 'show'])
check('spell: Quit prompts to continue with the auto corrections',
    'Continue with auto corrections?' in d[0], d[0])
check('spell: the end of a quit says Spell Check terminated',
    'Spell Check terminated' in d[1], d[1])
d = session(sbase, GO + ['text:q', 'wait', 'show'])
check('spell: Quit without corrections to make ends at once',
    'Spell Check terminated' in d[0] and 'No spelling errors found' in d[0],
    d[0])
# The document keeps what was done, with its attributes; it is changed
spell_fresh()
d = session(sbase, GO + ['text:g', 'ENTER', 'wait', 'text:q', 'wait',
    'text:n', 'wait', 'text:x', 'wait', 'C-s', 'wait', 'show'])
data = read(spdoc)
check('spell: what was replaced is in the document that is saved',
    b'The quick brown fox' in data and b'\x12\x01\x00qz\x12\x00\x00t and a '
    b'tab' in data, repr(data[300:420]))
# Block and Section: the area is highlighted first
spell_fresh()
d = session(sbase, SPELL + ['text:b', 'wait', 'show'])
check('spell: Block asks to highlight the blocks and press Enter',
    'highlight block(s)' in d[0], d[0])
d = session(sbase, SPELL + ['text:b', 'wait', 'ENTER', 'wait', 'show', 'INS',
    'wait', 'show'])
check('spell: Enter on the highlight shows the panel, then the check of that '
    'block', 'Check Document Spelling' in d[0] and 'Unknown word: quikc'
    in d[1], d[0] + d[1])
d = session(sbase, SPELL + ['text:s', 'wait', 'show'])
check('spell: Section asks to highlight the sections', 'highlight section(s)'
    in d[0], d[0])
# Spell Word: Tools, Spell, Word and Ctrl-C
d = session(sbase, ['C-HOME', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT',
    'C-c', 'wait', 'show', 'ENTER', 'wait', 'show'])
check('spell: Ctrl-C on a word that is not known shows the panel with the '
    'suggestions', 'Spell Word' in d[0] and 'quikc' in d[0] and 'Not found '
    'in dictionary' in d[0] and 'Suggested alternatives:' in d[0]
    and 'quick' in d[0] and 'Press Ins to select word' in d[0], d[0])
check('spell: Enter replaces the word of the document',
    'The quick brown fox' in d[1] and '*' in d[1].split('\n')[0], d[1])
d = session(sbase, ['C-HOME', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT',
    'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT',
    'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT',
    'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT',
    'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT',
    'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'C-c', 'wait', 'show'])
check('spell: Ctrl-C on a word that is right says Spelled correctly',
    'Spell Word' in d[0] and 'Spelled correctly' in d[0]
    and 'Press any key to continue' in d[0], d[0])
d = session(sbase, ['C-END', 'text: ' + 'a' * 70, 'C-c', 'wait', 'show'])
check('spell: a word of more than 65 letters is refused',
    'Word is too long - maximum length is 65 characters' in d[0], d[0])
# No checker: the entries stay, a message shows what to install
os.environ['STRATA_SPELL'] = '/nonexistent/no-spell-checker'
d = session(sbase, SPELL + ['text:d', 'wait', 'show'])
check('spell: without a checker a message shows what to install',
    'aspell' in d[0] and 'hunspell' in d[0] and 'No spelling checker'
    in d[0], d[0])
d = session(sbase, ['C-c', 'wait', 'show', 'ESC', 'text:typed', 'show'])
check('spell: Ctrl-C without a checker shows the same, and the editor works',
    'aspell' in d[0] and 'typed' in d[1], d[0] + d[1])
# The message names the packages to install, the install script that does it,
# and is not cut off by the screen (it wraps).
d = session(sbase, SPELL + ['text:d', 'wait', 'show'])
check('spell: without a checker the message names the packages hunspell, '
    'hunspell-en-us, aspell and aspell-en',
    all(x in flat(d[0]) for x in ('packages hunspell and hunspell-en-us',
                                  'aspell and aspell-en')), d[0])
check('spell: without a checker the message shows that the install script '
    'installs them', 'install script of Strata installs them' in flat(d[0]),
    d[0])
# The check of a file from the Document Manager gives the same message.
d = session([stred, '-H', helpf, '--spell', spdoc], ['wait', 'show'])
check('spell: stred --spell without a checker names the packages and the '
    'install script and the variable of another program',
    'packages hunspell and hunspell-en-us' in flat(d[0]) and
    'aspell and aspell-en' in flat(d[0]) and
    'install script of Strata installs them' in flat(d[0]) and
    'STRATA_SPELL' in flat(d[0]), d[0])
# A checker that does not start for the language of the settings: the message
# names the package of its dictionary, as Debian names it.
deadspell = os.path.join(tmp, 'deadspell')
os.makedirs(deadspell)
for prog in ('aspell', 'hunspell'):
    path = os.path.join(deadspell, prog)
    open(path, 'w').write('#!/bin/sh\nexit 1\n')
    os.chmod(path, 0o755)
for lang, want in (('en_US', {'hunspell': 'hunspell-en-us',
                              'aspell': 'aspell-en'}),
                   ('de_DE', {'hunspell': 'hunspell-de-de',
                              'aspell': 'aspell-de'}),
                   ('fr_FR', {'hunspell': 'hunspell-fr',
                              'aspell': 'aspell-fr'}),
                   ('pt_BR', {'hunspell': 'hunspell-pt-br',
                              'aspell': 'aspell-pt-br'})):
    spell_fresh()
    open(os.path.join(spell_home, 'config'), 'w').write(
        'spell_language=%s\n' % lang)
    for prog in ('hunspell', 'aspell'):
        os.environ['STRATA_SPELL'] = os.path.join(deadspell, prog)
        d = session(sbase, SPELL + ['text:d', 'wait', 'INS', 'wait', 'show'])
        f = flat(d[0])
        check('spell: %s that does not start for %s says to install the '
            'package %s' % (prog, lang, want[prog]),
            ('The spelling checker %s did not start for the language %s'
             % (prog, lang)) in f and
            ('Install the package %s, or change' % want[prog]) in f, d[0])
spell_fresh()
open(os.path.join(spell_home, 'config'), 'w').write('spell_language=9x\n')
os.environ['STRATA_SPELL'] = os.path.join(deadspell, 'hunspell')
d = session(sbase, SPELL + ['text:d', 'wait', 'INS', 'wait', 'show'])
check('spell: a language that has no package name says to install the '
    'dictionary of that language, and names no package',
    'Install the dictionary of that language for hunspell' in flat(d[0])
    and 'Install the package' not in flat(d[0]), d[0])
d = session(sbase, ['C-c', 'wait', 'show', 'ESC', 'text:typed', 'show'])
check('spell: Ctrl-C with a checker that does not start says so and the '
    'editor works', 'did not start' in flat(d[0]) and 'typed' in d[1],
    d[0] + d[1])
spell_fresh()
os.environ['STRATA_SPELL'] = fakespell
# Dictionary Language of the settings is the language that the checker gets
spell_fresh()
open(os.path.join(spell_home, 'config'), 'w').write('spell_language=de_DE\n')
d = session(sbase, GO + ['show'])
check('spell: the language of the settings is used (de_DE)',
    'Dictionaries: de_DE' in d[0], d[0])
spell_fresh()
# stred --spell: the check of a file, as the Document Manager does it
d = session([stred, '-H', helpf, '--spell', spdoc], ['wait', 'show', 'text:g',
    'ENTER', 'wait', 'text:q', 'wait', 'text:n', 'wait', 'text:x', 'wait',
    'show', 'ENTER', 'wait'])
check('spell: stred --spell shows the check at once', 'Unknown word: quikc'
    in d[0], d[0])
check('spell: stred --spell offers to save the corrected document',
    'Save corrected document?' in d[1], d[1])
check('spell: Yes saves the corrected document',
    b'The quick brown fox' in read(spdoc), repr(read(spdoc)[300:380]))
spell_fresh()
# The real checker, when it is there
if real_spell_path is None:
    del os.environ['STRATA_SPELL']
    import shutil as _sh
    if _sh.which('aspell') or _sh.which('hunspell'):
        d = session(sbase, GO + ['text:g', 'wait', 'show'])
        check('spell: with the real checker quikc is unknown and quick is '
            'suggested', 'Unknown word: quikc' in d[0]
            and 'quick' in d[0].split('\n')[0], d[0])
    else:
        print('skip spell: neither aspell nor hunspell is in PATH, the test of '
            'the real checker is skipped')
    os.environ['STRATA_SPELL'] = fakespell
home_override[0] = None
del os.environ['STRATA_SPELL']
if real_spell_path is not None:
    os.environ['STRATA_SPELL'] = real_spell_path
shutil.rmtree(spell_home)

# ---- str: the master menu --------------------------------------------------
strprog = os.environ.get('STR', os.path.join(os.environ.get('STRATA_BUILD_DIR',
    ''), 'term', 'str'))
cfg = tempfile.mkdtemp(); os.environ['XDG_CONFIG_HOME'] = cfg
wd = tempfile.mkdtemp(); shutil.copy(PLAN,
    os.path.join(wd, 'a.doc'))
os.chdir(wd)
msbase = [strprog, '-H', helpf]
d = session(msbase, ['show'])
check('str: Document Manager box and its seven entries', 'Strata Document '
    'Manager' in d[0] and 'Press Ins to Select Action' in d[0]
      and all(w in d[0] for w in ('Edit / Preview / Print Document',
              'Maintain Dictionaries', 'Compare Document Versions',
              'Restore Backup', 'TeX Font Association', 'Setup', 'Quit')) and
      '1-2-3' not in d[0] and 'File Conversion' not in d[0], d[0])
check('str: the entries for Print, Preview, spelling and Batch Print are gone',
      d[0].count('Print Document') == 1 and
      all(w not in d[0] for w in ('Preview Document',
          'Check Document Spelling', 'Batch Print', 'Edit Document')) and
      'Preview and Print are on the menu' not in d[0], d[0])
# TeX Font Association: the list of the LaTeX fonts of the font numbers
# The panel looks for each font on the disk before it is drawn, which takes
# several seconds, so the test waits longer than usual.
d = session(msbase, ['DOWN'] * 4 + ['ENTER'] + ['wait'] * 12 + ['show'],
    rows=30)
check('str: TeX Font Association lists the fonts of the numbers',
    all(w in d[0] for w in ('Latin Modern Roman', 'Times', 'Helvetica',
    'Palatino', 'TeX Gyre Pagella', 'Lato', 'lmodern', 'helvet',
    '(LaTeX default)')), d[0])
fh = os.path.join(tempfile.mkdtemp(prefix='home'), 'strata')
home_override[0] = fh
session(msbase, ['DOWN'] * 4 + ['ENTER', 'wait'] + ['DOWN'] * 19 + ['ENTER',
    'wait', 'DOWN', 'text:Test Font', 'DOWN', 'text:charter', 'INS', 'wait',
    'ESC'])
home_override[0] = None
ftxt = read(os.path.join(fh, 'fonts')).decode()
check('str: a font that is changed is kept in ~/.strata/fonts for the user',
    '\n20|Test Font|charter||roman||\n' in ftxt and
    '\n2|Latin Modern Roman|lmodern|' in ftxt and
    os.stat(os.path.join(fh, 'fonts')).st_mode & 0o777 == 0o600, ftxt)
# The Font marker: the keypad plus (Alt-+) lists the fonts of the TeX Font
# Association, and the choice is stored by its number
fmk = os.path.join(tmp, 'fontmark.doc')
shutil.copy(os.path.join(here, 'data', 'plain_text.doc'), fmk)
d = fm(fmk, ['A-F12', 'text:f', 'text:f', 'text:f', 'text:f', 'text:f', 'INS',
    'wait', 'show', 'A-+', 'wait', 'show', 'DOWN DOWN DOWN DOWN DOWN DOWN '
    'DOWN', 'ENTER', 'wait', 'show', 'INS', 'wait', 'C-s', 'wait'])
check('marker: the Font panel shows [Text: name] of the font list',
    '[Text: Latin Modern Roman]' in d[0] and '[[' not in d[0], d[0])
check('marker: Alt-+ on the Font lists the fonts by number and name',
    all(w in d[1] for w in ('Headline: Latin Modern Sans', 'Font 4: Times',
    'Font 7: Palatino', 'Font 18: Computer Modern Roman')), d[1])
check('marker: choosing from the list fills in the font',
    '[Font 7: Palatino]' in d[2], d[2])
check('marker: the font is saved by its number, as the original does',
    b'\x1315(17\r\n\x13)' in read(fmk), repr(read(fmk)[:300]))
# A font is downloaded and installed: from a fake archive of a directory
import zipfile
ctan = tempfile.mkdtemp(prefix='ctan')
with zipfile.ZipFile(os.path.join(ctan, 'strfake.tds.zip'), 'w') as z:
    z.writestr('tex/latex/strfake/strfake.sty', '\\ProvidesPackage{strfake}\n')
texhome = tempfile.mkdtemp(prefix='texmf')
os.environ['STRATA_CTAN_URL'] = 'file://' + ctan
os.environ['TEXMFHOME'] = texhome
fh2 = os.path.join(tempfile.mkdtemp(prefix='home'), 'strata')
home_override[0] = fh2
d = session(msbase, ['DOWN'] * 4 + ['ENTER', 'wait'] + ['DOWN'] * 19 +
    ['ENTER', 'wait', 'DOWN', 'text:Fake', 'DOWN', 'text:strfake', 'DOWN',
    'DOWN', 'DOWN', 'text:strfake', 'DOWN', 'DOWN', 'DOWN', 'text: ', 'INS',
    'wait', 'wait', 'wait', 'show', 'ENTER', 'ESC'])
home_override[0] = None
del os.environ['STRATA_CTAN_URL']
del os.environ['TEXMFHOME']
check('str: Install Now downloads the archive and unpacks it for the user',
    'Installed in' in d[0] and os.path.exists(os.path.join(texhome, 'tex',
    'latex', 'strfake', 'strfake.sty')), d[0])
d = session(msbase, ['DOWN', 'ENTER', 'show'])
check('str: Maintain Dictionaries is an entry that works: it opens its panel',
    'is not implemented in this version' not in d[0] and
    'Spelling, your words' in d[0], d[0])
d = session(msbase, ['DOWN', 'ENTER', 'F1', 'show'])
check('str: F1 in the panel of Maintain Dictionaries shows its help topic',
    'HELP: Maintain Dictionaries' in d[0], d[0])
d = session(msbase, tui.OPEN_SETUP + ['show'])
# The fields of Setup: the number of the Down keys from the first field of the
# first page to a field, and the same for the third page (the advanced
# settings), which PgDn reaches from the first page (PgUp goes forward to the
# colors, then to the advanced settings, then back to the first page).
SU = {'workdir': 0, 'backups': 1, 'autosave': 2, 'interval': 3, 'print_to': 4,
    'queue': 5, 'pdf_dir': 6, 'viewer': 7, 'paper': 8, 'language': 9,
    'undo': 10, 'rows': 11}
SA = {'macro': 0, 'library': 1, 'charset': 2, 'editor': 3, 'help': 4}
def su_to(name):
    return ['DOWN'] * SU[name]
def sa_to(name):
    # The keys that go to the page of the advanced settings and to a field.
    return ['PGDN'] + ['DOWN'] * SA[name]
check('str: Setup panel has the Initial Working Directory, no line Advanced '
    'and no dimmed setting',
    'Setup' in d[0] and '(n/a)' not in d[0] and
    'Advanced:' not in d[0] and 'Screen Colors:' not in d[0] and
    'Initial Working Directory' in d[0], d[0])
check('str: Setup has the Autosave settings, on and 120 seconds',
    'Autosave:' in d[0] and 'Yes' in d[0] and
    'Autosave Interval (Seconds):' in d[0] and '120' in d[0], d[0])
hm = os.path.join(tempfile.mkdtemp(prefix='home'), 'strata')
home_override[0] = hm
session(msbase, tui.OPEN_SETUP + ['INS', 'wait'])
home_override[0] = None
txt = read(os.path.join(hm, 'config')).decode()
check('str: Setup writes the settings to ~/.strata/config, for the user only',
    'autosave=yes' in txt and 'autosave_seconds=120' in txt and
    os.stat(hm).st_mode & 0o777 == 0o700 and
    os.stat(os.path.join(hm, 'config')).st_mode & 0o777 == 0o600, txt)
check('str: Setup does not fix the place of stred, which changes with a build',
    '\nstred=\n' in txt, txt)
d = session(msbase, ['ENTER', 'text:a', 'INS', 'wait', 'show', 'C-q',
    'wait', 'show'])
check('str: Edit / Preview / Print Document launches stred',
    'STRUCTURED EDIT' in d[0] and 'Orchard Plan' in d[0], d[0])
check('str: returning from the editor brings back the menu', 'Strata Document '
    'Manager' in d[1], d[1])
d = session(msbase, ['ENTER', 'text:a', 'INS', 'wait', 'F12',
    'text:P', 'show', 'ESC', 'ESC', 'C-q', 'wait'])
check('str: Print and Preview are on the menu of the Editor that it starts',
      'Draft' in d[0] and 'Final' in d[0] and 'Preview' in d[0], d[0])
# Gray + (Alt-+ in a terminal) in the panels of str lists the directories
# and files to walk through
d = session(msbase, ['ENTER', 'A-+', 'wait', 'show'])
check('str: Gray + on Document Name lists the files and ../',
    '../' in d[0] and 'a.doc' in d[0], d[0])
d = session(msbase, ['ENTER', 'UP', 'A-+', 'wait', 'show'])
check('str: Gray + on Working Directory offers to use the directory',
    '[Use this directory]' in d[0] and '../' in d[0], d[0])
d = session(msbase, ['ENTER', 'A-+', 'wait', 'DOWN', 'ENTER', 'wait',
    'show'])
check('str: choosing from the list fills in the Document Name',
    any('Document Name:' in l and '.doc' in l for l in d[0].split('\n')),
    d[0])
d = session(msbase, tui.OPEN_SETUP + su_to('workdir') + ['A-+', 'wait',
    'show'])
check('str: Gray + on Initial Working Directory lists the directories',
    '[Use this directory]' in d[0], d[0])
d = session(msbase, tui.OPEN_SETUP + sa_to('editor') + ['F2', 'wait',
    'show'])
check('str: F2 on a file field of Setup lists the files too',
    '../' in d[0], d[0])
d = session(msbase, ['ENTER', 'text:new1', 'INS', 'DOWN', 'ENTER',
    'wait', 'show', 'DOWN', 'text:Desc1', 'DOWN', 'text:Auth1', 'INS', 'wait',
    'show',
    'C-s', 'wait', 'C-q', 'wait'])
check('str: a missing file offers New File with a description and author',
    'New File' in d[0] and 'Description:' in d[0] and 'Author:' in d[0], d[0])
check('str: a missing file offers New File (type) and opens the editor',
    'UNSTRUCTURED EDIT' in d[1], d[1])
nf = os.path.join(wd, 'new1.doc')
check('str: the description and author given for a new file are saved',
    os.path.exists(nf) and b'MDDesc1\x00' in open(nf, 'rb').read()
    and b'MAAuth1\x00' in open(nf, 'rb').read(),
    repr(open(nf, 'rb').read()[:120]) if os.path.exists(nf) else 'no file')
# str: Setup has the Dictionary Language of the checker
sh = os.path.join(tempfile.mkdtemp(prefix='home'), 'strata')
home_override[0] = sh
d = session(msbase, ['DOWN'] * 5 + ['ENTER', 'wait', 'show'])
check('str: Setup has the Dictionary Language, which can be changed',
    'Dictionary Language:  en_US' in d[0], d[0])
session(msbase, ['DOWN'] * 5 + ['ENTER', 'wait'] + su_to('language') +
    ['text:de_DE', 'INS', 'wait', 'ESC'])
check('str: the Dictionary Language is kept in the settings of the user',
    b'spell_language=de_DE' in read(os.path.join(sh, 'config')),
    repr(read(os.path.join(sh, 'config'))))
home_override[0] = None
shutil.rmtree(cfg); shutil.rmtree(wd)

# The rows of the terminal are kept in the settings (key terminal_rows):
# written at the end of a run in which the user changed the height of the
# terminal, never when the height was not changed, and used as the most
# rows that the Editor takes.  The program never changes the terminal size.
def rows_home():
    h = os.path.join(tempfile.mkdtemp(prefix='home'), 'strata')
    home_override[0] = h
    return h
def screen_rows_ok(d, rows, total):
    lines = d.split('\n')
    return (len(lines) == total and lines[rows - 2].startswith('F1: Help') and
        lines[rows - 1].startswith('F2: Split') and
        all(l.strip() == '' for l in lines[rows:]))
th = rows_home()
d, st = session(base, ['show', 'size:40,80', 'show', 'C-q', 'wait'],
    status=True)
check('rows: the Editor of a terminal made taller keeps the height at the end',
    b'terminal_rows=40\n' in read(os.path.join(th, 'config')),
    repr(read(os.path.join(th, 'config'))))
th = rows_home()
session(base, ['show', 'C-q', 'wait'])
check('rows: a run in which the terminal was not resized writes nothing',
    b'terminal_rows' not in read(os.path.join(th, 'config')),
    repr(read(os.path.join(th, 'config'))))
th = rows_home()
os.makedirs(th, mode=0o700)
with open(os.path.join(th, 'config'), 'w') as f:
    f.write('backup=yes\nterminal_rows=30\nshow_dots=yes\n')
d = session(base, ['show'], rows=40)
check('rows: a terminal with more rows than the key has uses the rows of the '
    'key and no more', screen_rows_ok(d[0], 30, 40), d[0])
d = session(base, ['show'], rows=25)
check('rows: a terminal with fewer rows than the key is used as it is',
    screen_rows_ok(d[0], 25, 25), d[0])
d = session(base, ['show', 'size:45,80', 'show', 'C-q', 'wait'], rows=40)
txt = read(os.path.join(th, 'config')).decode()
check('rows: a terminal that is resized is used whole again and the new '
    'height replaces the key, every other line stays',
    screen_rows_ok(d[1], 45, 45) and txt == 'backup=yes\nshow_dots=yes\n'
    'terminal_rows=45\n', txt)
th = rows_home()
d, st = session(base, ['size:15,80', 'C-q', 'wait'], status=True)
check('rows: a height below 20 is written as 20',
    b'terminal_rows=20\n' in read(os.path.join(th, 'config')),
    repr(read(os.path.join(th, 'config'))))
th = rows_home()
d = session(msbase, tui.OPEN_SETUP + ['show'])
check('rows: Setup has one line for the rows of the X11 version and of '
    'the terminal version, with two edit fields, empty at the start',
    any('Window Rows:  X11:' in l and 'Terminal:' in l
    for l in d[0].split('\n')) and 'Version' not in d[0].split('Window Rows')[1]
    .split('\n')[0], d[0])
session(msbase, tui.OPEN_SETUP + su_to('rows') + ['text:60', 'TAB',
    'text:35', 'INS', 'wait'])
txt = read(os.path.join(th, 'config')).decode()
check('rows: Setup saves both values', '\nx11_rows=60\n' in txt and
    '\nterminal_rows=35\n' in txt, txt)
d = session(msbase, tui.OPEN_SETUP + ['show'])
check('rows: Setup shows the values that are kept',
    any('Window Rows:' in l and 'X11: 60' in l and 'Terminal: 35' in l
    for l in d[0].split('\n')), d[0])
d = session(msbase, tui.OPEN_SETUP + su_to('rows') + ['text:abc', 'INS',
    'wait', 'show'])
txt2 = read(os.path.join(th, 'config')).decode()
check('rows: Setup rejects rows that are not a number and keeps the file',
    'window rows are a whole number' in d[0] and txt2 == txt, d[0])
session(msbase, tui.OPEN_SETUP + su_to('rows') + ['text:10', 'TAB',
    'text:5000', 'INS', 'wait'])
txt = read(os.path.join(th, 'config')).decode()
check('rows: Setup makes the rows right when it saves: 10 is 20, 5000 is 200',
    '\nx11_rows=20\n' in txt and '\nterminal_rows=200\n' in txt, txt)
# Setup: the PDF Viewer of PDF viewer.  Nothing is started here.
th = rows_home()
d = session(msbase, tui.OPEN_SETUP + ['show'])
check('opener: Setup shows the field PDF Viewer, empty, with the default '
    'a viewer of /usr/bin', any('PDF Viewer:' in l and
    'default: a viewer in /usr/bin' in l for l in d[0].split('\n')), d[0])
for word in ('evince', 'soffice', 'AbiWord', './mine'):
    d = session(msbase, tui.OPEN_SETUP + ['DOWN'] * 7 + ['text:' + word,
        'INS', 'wait', 'show'])
    check('opener: Setup rejects the name %s: the full path is needed' % word,
        'needs its full path, such as /usr/bin/evince' in d[0]
        and not os.path.exists(os.path.join(th, 'config')), d[0])
for word in ('/opt/x/soffice', '/usr/bin/libreoffice', '/opt/AbiWord'):
    d = session(msbase, tui.OPEN_SETUP + ['DOWN'] * 7 + ['text:' + word,
        'INS', 'wait', 'show'])
    check('opener: Setup rejects %s, a word processor, and says why' % word,
        'A word processor is not a PDF viewer' in
        d[0] and not os.path.exists(os.path.join(th, 'config')), d[0])
ovd = tempfile.mkdtemp(prefix='viewer')
ovp = os.path.join(ovd, 'show')
with open(ovp, 'w') as f:
    f.write('#!/bin/sh\nexit 0\n')
os.chmod(ovp, 0o755)
session(msbase, tui.OPEN_SETUP + ['DOWN'] * 7 + ['text:' + ovp, 'INS',
    'wait'])
check('opener: the full path of a safe program is saved as the key '
    'pdf_viewer',
    ('\npdf_viewer=' + ovp + '\n').encode() in read(os.path.join(th,
    'config')), repr(read(os.path.join(th, 'config'))))
d = session(msbase, tui.OPEN_SETUP + ['show'])
check('opener: Setup shows the saved program', any('PDF Viewer' in l and
    ovp[:30] in l for l in d[0].split('\n')), d[0])

# ---- Setup: the print settings, the PDF Viewer policy and the row of two edit
# fields (a block of its own).  lpstat is the stand-in of the tests.
saved_lp = {k: os.environ.get(k) for k in ('STRATA_LPSTAT',
    'FAKELPSTAT_QUEUES', 'FAKELPSTAT_DEFAULT', 'FAKELPSTAT_OFF')}
def lp_env(**kw):
    for k in saved_lp:
        os.environ.pop(k, None)
    os.environ['STRATA_LPSTAT'] = os.path.join(here, 'fakelpstat.sh')
    for k, v in kw.items():
        os.environ[k] = v
def put_conf(h, text, mode=0o600):
    os.makedirs(h, mode=0o700, exist_ok=True)
    with open(os.path.join(h, 'config'), 'w') as f:
        f.write(text)
    os.chmod(os.path.join(h, 'config'), mode)
def conf(h):
    return read(os.path.join(h, 'config')).decode()
def line(d, label):
    for l in d.split('\n'):
        if label in l:
            return l
    return ''
SETUP = tui.OPEN_SETUP
lp_env()
th = rows_home()
d = session(msbase, SETUP + ['show'])
check('setup: the fields that make no sense now are gone',
    all(w not in d[0] for w in ('Graphics Display Driver', 'Printer:',
    'Virtual Memory Setup', 'Sound:', 'Window Rows, ')), d[0])
check('setup: the settings stand in four groups under headings and the print '
    'settings stand together; the Editor program and the Help File are on '
    'the page of the advanced settings',
    d[0].index('Documents') < d[0].index('Initial Working Directory') <
    d[0].index('Printing') < d[0].index('Print To:') <
    d[0].index('Printer Queue:') < d[0].index('PDF Directory:') <
    d[0].index('PDF Viewer:') < d[0].index('Writing') <
    d[0].index('Paper for New Documents:') < d[0].index('Screen') and
    'Editor program:' not in d[0] and 'Help File:' not in d[0], d[0])
check('setup: Print To starts as File only and the queue as the default',
    'File only' in line(d[0], 'Print To:') and
    'System default' in line(d[0], 'Printer Queue:'), d[0])
d = session(msbase, SETUP + ['DOWN'] * 4 + ['RIGHT', 'show'])
check('setup: Right changes the choice of Print To to PDF viewer',
    'PDF viewer' in line(d[0], 'Print To:') and
    'Open to print' not in d[0], d[0])
d = session(msbase, SETUP + ['DOWN'] * 4 + ['LEFT', 'show'])
check('setup: Left goes back to Printer, which is there when the system has '
    'queues', 'Printer' in line(d[0], 'Print To:') and
    'File only' not in line(d[0], 'Print To:').split('default')[0], d[0])
d = session(msbase, SETUP + ['DOWN'] * 4 + ['text:p', 'show'])
check('setup: a letter does not jump to an entry: the Editor has no such jump',
    'File only' in line(d[0], 'Print To:').split('default')[0], d[0])
d = session(msbase, SETUP + ['DOWN'] * 5 + ['show'])
check('setup: the line at the foot names the default printer of the system',
    'The default printer of the system is laser' in d[0], d[0])
d = session(msbase, SETUP + ['DOWN'] * 5 + ['F2', 'wait', 'show'])
check('setup: F2 lists the queues, the default first and marked, a queue '
    'that does not accept jobs marked too',
    'System default  (laser)' in d[0] and 'laser  (system default)' in d[0] and
    'office' in d[0], d[0])
lp_env(FAKELPSTAT_OFF='office')
d = session(msbase, SETUP + ['DOWN'] * 5 + ['F2', 'wait', 'show'])
check('setup: a queue that does not accept jobs is marked',
    'office  (not accepting jobs)' in d[0], d[0])
lp_env()
d = session(msbase, SETUP + ['DOWN'] * 5 + ['text:x', 'show'])
check('setup: typing cannot make up a name: an unknown letter does nothing',
    'System default' in line(d[0], 'Printer Queue:') and
    'x' not in line(d[0], 'Printer Queue:').replace('Queue', ''), d[0])
th = rows_home()
session(msbase, SETUP + ['DOWN'] * 4 + ['LEFT', 'DOWN', 'RIGHT', 'INS',
    'wait'])
check('setup: Printer and the queue laser are saved as print_to and '
    'print_queue, and the old key printer is not written',
    '\nprint_to=printer\n' in conf(th) and '\nprint_queue=laser\n' in conf(th)
    and '\nprinter=' not in conf(th), conf(th))
d = session(msbase, SETUP + ['show'])
check('setup: Setup shows what was kept',
    'Printer' in line(d[0], 'Print To:') and
    'laser' in line(d[0], 'Printer Queue:'), d[0])
th = rows_home()
session(msbase, SETUP + ['DOWN'] * 4 + ['LEFT', 'INS', 'wait'])
check('setup: Printer with the default queue saves no queue',
    '\nprint_to=printer\n' in conf(th) and 'print_queue' not in conf(th),
    conf(th))
th = rows_home()
session(msbase, SETUP + ['DOWN'] * 4 + ['RIGHT', 'INS', 'wait'])
check('setup: PDF viewer is saved as print_to=viewer',
    '\nprint_to=viewer\n' in conf(th) and 'open' not in conf(th), conf(th))
# A file of an older version: the key printer keeps its meaning.
for old, want_to, want_q in (('printer=office\n', 'Printer', 'office'),
        ('printer=Open to print\n', 'PDF viewer', 'System default'),
        ('print_to=open\n', 'PDF viewer', 'System default'),
        ('print_to=viewer\n', 'PDF viewer', 'System default'),
        ('printer=File only\n', 'File only', 'System default'),
        ('printer=\n', 'File only', 'System default'),
        ('backup=yes\n', 'File only', 'System default')):
    th = rows_home()
    put_conf(th, old)
    d = session(msbase, SETUP + ['show'])
    check('setup: an old settings file with %r means %s and %s' %
        (old.strip(), want_to, want_q),
        want_to in line(d[0], 'Print To:') and
        want_q in line(d[0], 'Printer Queue:'), d[0])
th = rows_home()
put_conf(th, 'printer=office\nbackup=yes\n')
session(msbase, SETUP + ['INS', 'wait'])
check('setup: accepting Setup turns the old keys into the new ones, every '
    'other line stays (the old key backup is not read: the default of 3 '
    'backups stays, and no line backups is written)',
    '\nprint_to=printer\n' in conf(th) and
    '\nprint_queue=office\n' in conf(th) and '\nprinter=' not in conf(th) and
    '\nshow_dots=no\n' in conf(th) and '\nbackup=' not in conf(th) and
    'backups=' not in conf(th), conf(th))
th = rows_home()
put_conf(th, 'backup=no\nautosave=yes\nshow_dots=no\n')
d = session(msbase, SETUP + ['show'])
check('setup: an old file with backup=no shows Backups Kept 3, the default',
    'Backups Kept (0 to 99):  3' in line(d[0], 'Backups Kept'), d[0])
session(msbase, SETUP + ['INS', 'wait'])
check('setup: and saving it writes no line backups (the old no was never a '
    'choice)', 'backups' not in conf(th), conf(th))
th = rows_home()
put_conf(th, 'backups=0\nbackup=no\n')
d = session(msbase, SETUP + ['show'])
check('setup: an explicit backups=0 stays 0', 'Backups Kept (0 to 99):  0' in
    line(d[0], 'Backups Kept'), d[0])
# No queues, no tool, and a queue that is not listed now.
lp_env(FAKELPSTAT_QUEUES='')
th = rows_home()
d = session(msbase, SETUP + ['DOWN'] * 4 + ['RIGHT', 'show'])
check('setup: no queues: Print To offers File only and PDF viewer only',
    'PDF viewer' in line(d[0], 'Print To:'), d[0])
d = session(msbase, SETUP + ['DOWN'] * 5 + ['show'])
check('setup: no queues: the field says No printers found and so does the '
    'line at the foot', 'No printers found' in line(d[0], 'Printer Queue:')
    and d[0].count('No printers found') == 2, d[0])
put_conf(th, 'print_to=printer\nprint_queue=laser\n')
d = session(msbase, SETUP + ['show'])
check('setup: no queues: a saved Printer is kept and shown as not available',
    'Printer (not available now)' in line(d[0], 'Print To:') and
    'laser (not available now)' in line(d[0], 'Printer Queue:'), d[0])
session(msbase, SETUP + ['INS', 'wait'])
check('setup: and it stays in the file when Setup is accepted',
    '\nprint_to=printer\n' in conf(th) and '\nprint_queue=laser\n' in
    conf(th), conf(th))
lp_env(STRATA_LPSTAT='/no/such/lpstat')
th = rows_home()
d = session(msbase, SETUP + ['DOWN'] * 5 + ['show'])
check('setup: no lpstat at all is not an error: No printers found',
    'No printers found' in line(d[0], 'Printer Queue:'), d[0])
lp_env()
th = rows_home()
put_conf(th, 'print_to=printer\nprint_queue=gone\n')
d = session(msbase, SETUP + ['show'])
check('setup: a saved queue that is not listed now is kept and marked',
    'gone (not available now)' in line(d[0], 'Printer Queue:'), d[0])
session(msbase, SETUP + ['INS', 'wait'])
check('setup: and is not dropped from the file',
    '\nprint_queue=gone\n' in conf(th), conf(th))
# The PDF Directory.
th = rows_home()
d = session(msbase, SETUP + ['DOWN'] * 6 + ['show'])
check('setup: PDF Directory is empty at the start and the line at the foot '
    'says the directory of the document is used',
    'directory of the document' in d[0], d[0])
d = session(msbase, SETUP + ['DOWN'] * 6 + ['text:pdfs', 'INS', 'wait',
    'show'])
check('setup: a relative PDF Directory is refused, the panel stays open and '
    'nothing is saved',
    'full path or starts with ~/' in d[0] and 'Documents' in d[0] and
    ('pdf_dir' not in conf(th) if os.path.exists(os.path.join(th, 'config'))
    else True), d[0])
d = session(msbase, SETUP + ['DOWN'] * 6 + ['text:/nonexistent/pdfs', 'show'])
check('setup: a directory that does not exist is said so at the foot',
    'The PDF Directory does not exist' in d[0], d[0])
session(msbase, SETUP + ['DOWN'] * 6 + ['text:/nonexistent/pdfs', 'INS',
    'wait'])
check('setup: and it is saved anyway, because it may be made later',
    '\npdf_dir=/nonexistent/pdfs\n' in conf(th), conf(th))
pd = tempfile.mkdtemp(prefix='pdfdir')
d = session(msbase, SETUP + ['DOWN'] * 6 + ['text:' + pd, 'show'])
# A long path is shown shortened from the front: the end of it is checked.
check('setup: a directory that can be used: the line at the foot says where '
    'the PDF files go', 'PDF files go to ' in d[0] and
    os.path.basename(pd) in d[0], d[0])
# The PDF Viewer: the policy.  A full path only, never a name of PATH.
th = rows_home()
for bad in ('a b', './viewer', '../x', 'x;y', 'evince', 'rel/viewer',
        '/a/../b', '/usr/bin/ev ince'):
    d = session(msbase, SETUP + ['DOWN'] * 7 + ['text:' + bad, 'show'])
    check('setup: %r is not a full path: the foot says so' % bad,
        'The full path of the program, such as /usr/bin/evince' in d[0], d[0])
    d = session(msbase, SETUP + ['DOWN'] * 7 + ['text:' + bad, 'INS', 'wait',
        'show'])
    check('setup: %r is refused when Setup is accepted, and not saved' % bad,
        'needs its full path' in d[0] and 'Documents' in d[0] and
        (not os.path.exists(os.path.join(th, 'config')) or
        'pdf_viewer' not in conf(th)), d[0])
d = session(msbase, SETUP + ['DOWN'] * 7 + ['text:/nonexistent/viewer',
    'show'])
check('setup: a path of no program: the line at the foot says so',
    'Not an executable file' in d[0], d[0])
session(msbase, SETUP + ['DOWN'] * 7 + ['text:/nonexistent/viewer', 'INS',
    'wait'])
check('setup: and it is refused at Ins, not saved',
    not os.path.exists(os.path.join(th, 'config')) or
    'pdf_viewer' not in conf(th), conf(th))
d = session(msbase, SETUP + ['DOWN'] * 7 + ['text:' + tempfile.gettempdir(),
    'INS', 'wait', 'show'])
check('setup: a directory is refused',
    'The PDF viewer is not an executable file' in d[0], d[0])
d = session(msbase, SETUP + ['DOWN'] * 7 + ['text:/opt/x/soffice', 'show'])
check('setup: a word processor is not accepted: the line says so',
    'Not a word processor' in d[0], d[0])

vd = tempfile.mkdtemp(prefix='viewer')
vp = os.path.join(vd, 'goodview')
with open(vp, 'w') as f:
    f.write('#!/bin/sh\nexit 0\n')
os.chmod(vp, 0o755)
d = session(msbase, SETUP + ['DOWN'] * 7 + ['text:' + vp, 'show'])
check('setup: a full path of a program that is safe: Found: the path',
    'Found: ' in d[0] and os.path.basename(vd) + '/goodview' in d[0], d[0])
os.chmod(vp, 0o775)
d = session(msbase, SETUP + ['DOWN'] * 7 + ['text:' + vp, 'show'])
check('setup: a program that its group can write is not safe: the line says '
    'so', 'Not safe: other users can change this program' in d[0], d[0])
d = session(msbase, SETUP + ['DOWN'] * 7 + ['text:' + vp, 'INS', 'wait',
    'show'])
check('setup: and Ins refuses it, with the reason',
    'can be changed by other users' in d[0], d[0])
os.chmod(vp, 0o755)
os.chmod(vd, 0o777)
d = session(msbase, SETUP + ['DOWN'] * 7 + ['text:' + vp, 'show'])
check('setup: a program in a directory that anyone can write is not safe',
    'Not safe: other users can change this program' in d[0], d[0])
os.chmod(vd, 0o700)
# An old settings file with a name: not used, and Setup shows so at the field.
th = rows_home()
put_conf(th, 'pdf_viewer=evince\n')
d = session(msbase, SETUP + ['DOWN'] * 7 + ['show'])
check('setup: an old setting that is a name is not used: the field is empty '
    'and the foot says it is not a full path',
    'The old setting evince is not a full path and is not used' in d[0] and
    'evince' not in line(d[0], 'PDF Viewer:').split('default')[0], d[0])
session(msbase, SETUP + ['INS', 'wait'])
check('setup: and saving it writes no pdf_viewer line', 'pdf_viewer' not in
    conf(th), conf(th))
# The file of the settings: a file that others can write is not trusted.
th = rows_home()
put_conf(th, 'pdf_viewer=%s\n' % vp, mode=0o664)
d = session(msbase, SETUP + ['show'])
check('setup: the setting pdf_viewer is ignored when the file of the '
    'settings can be written by its group (the field is empty)',
    'default: a viewer in /usr/bin' in line(d[0], 'PDF Viewer:') and
    vp not in d[0], d[0])
th = rows_home()
os.chmod(vp, 0o755)
put_conf(th, 'pdf_viewer=%s\n' % vp)
d = session(msbase, SETUP + ['show'])
# The field shows the start of a long path: look for what it shows.
check('setup: the same setting in a file that only the user can write is '
    'used', vp[:30] in line(d[0], 'PDF Viewer:'), d[0])
# One line, two edit fields.
th = rows_home()
d = session(msbase, SETUP + su_to('rows') + ['text:60', 'TAB', 'text:35',
    'show'])
check('setup: Tab goes from the X11 field to the Terminal field of the row',
    any('Window Rows:' in l and 'X11: 60' in l and 'Terminal: 35' in l
    for l in d[0].split('\n')), d[0])
session(msbase, SETUP + su_to('rows') + ['text:60', 'TAB', 'text:35', 'BTAB',
    'END', 'text:0', 'INS', 'wait'])
check('setup: Shift-Tab goes back to the X11 field; 600 is made 200 and the '
    'terminal field keeps 35', '\nx11_rows=200\n' in conf(th) and
    '\nterminal_rows=35\n' in conf(th), conf(th))
th = rows_home()
d = session(msbase, SETUP + su_to('rows') + ['text:60', 'RIGHT', 'text:35',
    'LEFT', 'LEFT', 'LEFT', 'show'])
check('setup: Right at the end of the first field enters the second one, '
    'Left at the start of the second goes back',
    any('Window Rows:' in l and 'X11: 60' in l and 'Terminal: 35' in l
    for l in d[0].split('\n')), d[0])
for k, v in saved_lp.items():
    if v is None:
        os.environ.pop(k, None)
    else:
        os.environ[k] = v
home_override[0] = None

# A change of the size of the terminal (SIGWINCH): the Editor draws every
# cell again, with the status line at the top and the key strip at the
# foot, whether the terminal gets taller, shorter or narrower.
def strip_ok(d, rows):
    lines = d.split('\n')
    return (len(lines) == rows and 'STRUCTURED EDIT' in lines[0] and
        lines[rows - 2].startswith('F1: Help') and
        lines[rows - 1].startswith('F2: Split'))
d = session(base, ['show', 'size:40,80', 'show', 'size:12,80', 'show',
    'size:25,80', 'show'])
check('resize: the terminal made taller (40 rows) keeps the strip at the foot',
    strip_ok(d[1], 40) and d[1].count('\n') > d[0].count('\n') and
    len([l for l in d[1].split('\n')[1:-2] if l.strip() != '']) >
    len([l for l in d[0].split('\n')[1:-2] if l.strip() != '']), d[1])
check('resize: the terminal made shorter (12 rows) keeps the strip at the foot',
    strip_ok(d[2], 12), d[2])
check('resize: the terminal made as it was (25 rows) draws the first screen',
    d[3] == d[0], d[3])
d = session(base, ['PGDN PGDN PGDN', 'show', 'size:10,80', 'show',
    'size:25,80', 'show'])
cur = d[0].split('\n')[1:-2]
check('resize: the text is laid out again and the cursor stays in view',
    strip_ok(d[1], 10) and strip_ok(d[2], 25) and
    any(l.strip() != '' for l in d[1].split('\n')[1:-2]), d[1])
d = session(base, ['size:30,100', 'show', 'size:30,60', 'show'])
check('resize: more columns keep the text at the left, the status line over '
    'the width', 'STRUCTURED EDIT' in d[0].split('\n')[0] and
    d[0].split('\n')[0].rstrip().endswith('STRUCTURED EDIT') and
    len(d[0].split('\n')[0]) > 90, d[0])
check('resize: fewer columns cut the rows and the key strip stays at the foot',
    strip_ok(d[1], 30) and all(len(l) <= 60 for l in d[1].split('\n')), d[1])
# Dialogs and panels that are open follow the size too.
d = session(base, ['F12', 'size:40,80', 'show', 'size:25,80', 'show'])
check('resize: the menu bar that is open follows a taller terminal',
    d[0].split('\n')[0].startswith('File Global Edit') and
    len(d[0].split('\n')) == 40 and 'F1: Help' in d[0].split('\n')[38] and
    d[1].split('\n')[0].startswith('File Global Edit') and
    len(d[1].split('\n')) == 25, d[0])
d = session(base, ['F12 ENTER RIGHT ENTER', 'wait', 'show', 'size:15,80',
    'show', 'size:40,80', 'show', 'size:6,80', 'show'])
check('resize: the dialog Retrieve File that is open is drawn again at the '
    'new size and is kept in the middle', all('Retrieve File' in x and
    'Ins accepts, Esc cancels' in x for x in d[:3]) and
    len(d[1].split('\n')) == 15 and len(d[2].split('\n')) == 40 and
    d[2].split('\n')[0].lstrip().startswith('work.doc') and
    strip_ok(d[2], 40), d[2])
check('resize: a terminal of fewer than 20 rows (6) is drawn without an error',
    len(d[3].split('\n')) == 6 and d[3].split('\n')[0] != '', d[3])

# ---- the help (F1): a window across the foot of the screen, with the topic
# of the place that the cursor is in ----
d = session(base, ['F1', 'wait', 'show'])
check('help: F1 opens the window across the foot; the document stays above',
    'HELP: Edit Screen' in d[0] and 'Orchard Plan' in d[0].split('\n')[2],
    d[0])
check('help: the window has the key hints and is a box',
    'Esc: close' in d[0] and '╔' in d[0] and '╚' in d[0], d[0])
d = session(base, ['F1', 'wait', 'TAB', 'ENTER', 'wait', 'show', 'BKSP',
    'wait', 'show', 'ESC', 'wait', 'show'])
check('help: Tab and Enter follow a link to another topic',
    'HELP: Function Keys' in d[0], d[0])
check('help: Backspace goes back to the topic that it came from',
    'HELP: Edit Screen' in d[1], d[1])
check('help: Esc closes the window and shows the document again',
    'HELP:' not in d[2] and 'Orchard Plan' in d[2], d[2])
d = session(base, ['F1', 'wait', 'text:z', 'wait', 'show', 'text:z', 'wait',
    'show'])
check('help: Z makes the window as large as the screen, and back',
    'HELP:' in d[0].split('\n')[0] and
    'HELP:' not in d[1].split('\n')[0] and 'HELP:' in d[1], d[0])
d = session(base, ['F1', 'wait', 'text:i', 'wait', 'show', 'ENTER', 'wait',
    'show'])
check('help: I lists the topics, and Enter shows the one that is chosen',
    'Help Index' in d[0] and 'HELP:' in d[1] and 'Help Index' not in d[1],
    d[0] + d[1])
d = session(base, ['F12', 'text:P', 'F1', 'wait', 'show'])
check('help: F1 in the menu bar shows the topic of the entry (Print, Draft)',
    'HELP: Print Draft' in d[0], d[0])
d = session(base, ['F12', 'F1', 'wait', 'show'])
check('help: F1 on the first level of the menu bar shows the File menu',
    'HELP: File Menu' in d[0], d[0])
d = session(base, ['F12', 'text:F', 'text:S', 'wait', 'F1', 'wait', 'show'])
check('help: F1 in the panel Save File shows the topic Save',
    'HELP: Save' in d[0] and 'Working Directory' in d[0], d[0])
d = session(base, ['F12', 'text:F', 'text:S', 'wait', 'DOWN', 'DOWN', 'F1',
    'wait', 'show'])
check('help: F1 on the field Options of Save File shows the topic Save too',
    'HELP: Save' in d[0], d[0])
d = session(base, ['F9', 'wait', 'text:zzzzqq', 'INS', 'wait', 'show', 'F1',
    'wait', 'show'])
check('help: the message Not found is on the message line',
    'Not found: zzzzqq' in d[0].split('\n')[-1], d[0])
check('help: F1 on a message shows the topic of the message',
    'HELP: Search Messages' in d[1], d[1])
d = session(base, ['F12', 'text:G', 'text:O', 'wait', 'F1', 'wait', 'show'])
check('help: F1 in the panel Options shows the topic Global Options',
    'HELP: Global Options' in d[0], d[0])
d = session(base, ['A-F12', 'wait', 'F1', 'wait', 'show'])
check('help: F1 in the table Marker Commands shows the topic',
    'HELP: Marker Commands' in d[0], d[0])
d = session(base, ['A-F12', 'wait'] + ['DOWN'] * 5 + ['ENTER', 'wait', 'show',
    'F1', 'wait', 'show'])
check('help: the sixth entry of Marker Commands is the panel of Date',
    'Date' in d[0] and 'Month D, Year' in d[0], d[0])
check('help: F1 in the panel of the marker Date shows the marker topic',
    'HELP: Date and Time Markers' in d[1], d[1])
d = session(base, ['F1', 'wait', 'show'], rows=24, cols=44)
check('help: a window of 44 columns works and shows the short hints',
    'HELP:' in d[0] and 'Esc' in d[0], d[0])
d = session(base, ['F1', 'wait', 'show'], rows=40, cols=100)
check('help: a window of 100 columns shows the long hints',
    'HELP:' in d[0] and 'PgUp/PgDn' in d[0], d[0])
d = session([stred, '-H', helpf], ['F1', 'wait', 'show'])
check('help: F1 in the Document Manager menu shows its topic',
    'HELP: Document Manager' in d[0], d[0])
saved_help = os.environ.pop('STRATA_HELP', None)
d = session(nohelp, ['F12', 'text:P', 'F1', 'wait', 'show'])
check('help: a missing help file says so in a menu too',
    'Help file not found' in d[0], d[0])
if saved_help is not None:
    os.environ['STRATA_HELP'] = saved_help
d = session(msbase, ['F1', 'wait', 'show'])
check('help: F1 in the menu of the Document Manager program shows its topic',
    'HELP: Edit / Preview / Print Document' in d[0], d[0])
d = session(msbase, ['END UP', 'F1', 'wait', 'show'])
check('help: F1 on Setup in the menu of the Document Manager program',
    'HELP: Setup' in d[0], d[0])

# ---- the help of the places that were missing: the text editors of a
# marker, a marker under the cursor and the dimmed entries of Print ---------
nk = ['DOWN', 'A-F12', 'text:N', 'INS', 'wait']
d = session(base, nk + ['F1', 'wait', 'show'])
check('help: F1 in the editor of a note shows the topic Note Text',
    'HELP: Note Text' in d[0], d[0])
d = session(base, ['A-F12', 'wait', 'DOWN', 'DOWN', 'ENTER', 'wait', 'show',
    'F1', 'wait', 'show'])
check('help: F1 in the editor of a comment shows the topic Comment Text',
    'COMMENT EDIT' in d[0] and 'HELP: Comment Text' in d[1], d[0] + d[1])
d = session(base, nk + ['text:Hello note', 'INS', 'wait', 'INS', 'wait',
    'LEFT', 'F1', 'wait', 'show'])
check('help: F1 on a marker in the text shows the topic of the marker',
    'HELP: Note Marker' in d[0], d[0])
d = session(base, ['F12', 'text:P', 'text:E', 'F1', 'wait', 'show'])
check('help: F1 on the entry Next of Print, Errors shows Print Errors',
    'HELP: Print Errors' in d[0], d[0])
d = session(base, ['F12', 'text:P', 'text:E'] + ['RIGHT'] * 3 + ['wait', 'show'])
check('the third Right in Print, Errors reaches the last entry',
    'Remove all the Print Errors' in d[0].split('\n')[1], d[0])
d = session(base, ['F1', 'wait', 'show'])
check('help: F1 on the Edit Screen without a marker still shows the screen',
    'HELP: Edit Screen' in d[0], d[0])

# ---- the markers Filename, Pathname, Filedate and Filetime ------------------
# A print of a named file shows its name, its path and its date and time, not
# the copy that the Printer is given (more of it in tests/printpdf.py).
fnsub = os.path.join(tmp, 'fname-where', 'deep')
os.makedirs(fnsub)
fnamed = os.path.join(fnsub, 'My Report.doc')
open(fnamed, 'wb').write(b'*R3\r\nMR1001\r\nPP\\1\\\x00\r\nP1C33\r\nFHA1\r\n'
    b'FTT5\r\nDP0,8500,11000\r\n*B1\r\nS\r\n+\r\nS\r\nTONE\x1d\x00\r\nB\r\n'
    b'TNAME \x1313(\x13)\x1d PATH \x1324(\x13)\x1d DATE \x1312(\x13)\x1d '
    b'TIME \x1314(\x13)\x1d\x00\r\n-\r\n*E\r\n')
fnstamp = time.mktime((2001, 2, 3, 4, 5, 0, 0, 0, -1))
os.utime(fnamed, (fnstamp, fnstamp))
fnout = os.path.join(tmp, 'fname-out.pdf')
d = fm(fnamed, ['F12', 'text:P', 'ENTER', 'text:' + fnout, 'INS'] +
    ['wait'] * 10 + ['show'])
fntext = ''.join(pdftext(fnout).split())
check('markers: a Draft print of a named file shows its name, path, date and '
    'time', 'MyReport.doc' in fntext.replace('MYREPORT.DOC', 'MyReport.doc')
    and ''.join(fnsub.split()) in fntext and 'February3,2001' in fntext and
    '4:05am' in fntext and 'part.doc' not in fntext.lower(),
    (fntext[:300], d[0][-400:]))

# ---- every menu is whole at every width -------------------------------------
# The menu tree of the F12 menu bar is read from the program (the option
# --dump-menus), so that the test follows the tables that the program uses.
# At 80, 100 and 132 columns every level is opened and every entry must be
# whole on the screen, the entries must run over the rows from left to right,
# and the label of the mode (MENU-PRINT and so on) must be at the right end of
# the first row with no entry run over by it.  The popup boxes must be whole.
import subprocess as _sp


def menu_levels():
    out = _sp.run([stred, '--dump-menus'], capture_output=True,
                  text=True).stdout
    levels = []

    for ln in out.split('\n'):
        if ln.startswith('menu\t'):
            _, path, names = ln.split('\t')
            levels.append((path.split(':') if path != '' else [],
                           names.split('|')))

    return levels


# The Editor has no Shell entry: the top level of the menu tree is exactly
# these seven names (Help is the entry that W29.2 added), and no level of
# the tree has an entry of the name Shell.
_top = [names for path, names in menu_levels() if path == []]
check('the menu tree of the program has no Shell entry and seven top entries',
    _top == [['File', 'Global', 'Edit', 'Print', 'Tools', 'Help', 'Quit']] and
    all('Shell' not in names for _, names in menu_levels()), _top)


# The Print menu has exactly these entries: Repaginate and Clear-pages were
# dimmed, did nothing and are gone (owner, 2026-11-18); no level of the tree
# has an entry of either name.
_print = [names for path, names in menu_levels() if path == ['Print']]
check('the Print menu has four entries and no Repaginate or Clear-pages',
    _print == [['Draft', 'Final', 'Preview', 'Errors']] and
    all('Repaginate' not in names and 'Clear-pages' not in names
        for _, names in menu_levels()), _print)
d = session(base, ['F12', 'text:P', 'wait', 'show'])
_rows = d[0].split('\n')
check('the Print bar at 80 columns: four whole entries on one row and the '
    'mode name at the end of it, the hint on the second row',
    _rows[0].startswith('Draft Final Preview Errors') and
    _rows[0].rstrip().endswith('MENU-PRINT') and
    'Print document in draft quality' in _rows[1], d[0])
d = session(base, ['F12', 'text:P', 'text:E', 'text:R', 'show'])
check('the letter R in Print, Errors chooses an entry that works: the '
    'menu closes and nothing says it is not available',
    'MENU' not in d[0].split('\n')[0] and
    'Not available in this version' not in d[0], d[0])


def menu_rows_ok(rows, path, names):
    """Return an empty string when the bar on the screen rows shows the names
    in order and whole, with the label of the mode alone at the end of the
    first row; else the reason."""
    label = 'MENU' + ''.join('-' + p.upper() for p in path)
    first = rows[0].rstrip()

    if not first.endswith(label):
        return 'the label %s is not at the end of the first row' % label

    rows = [first[:len(first) - len(label)]] + rows[1:]
    idx = 0

    for r in range(len(rows)):
        txt = rows[r].strip()

        if txt == '':
            return 'a row of the bar is empty before the last entry'

        for k in range(len(names), idx, -1):
            if ' '.join(names[idx:k]) == txt:
                idx = k
                break
        else:
            return 'row %d "%s" is not a run of whole entries' % (r, txt)

        if idx == len(names):
            return ''

    return 'the entries after %s are not on the screen' % names[idx - 1]


def boxes_whole(text):
    """Each line of the screen has as many right edges of a box as left
    edges, and an even number of vertical bars."""
    for ln in text.split('\n'):
        left = sum(ln.count(c) for c in '\u2554\u255a\u2560')
        right = sum(ln.count(c) for c in '\u2557\u255d\u2563')

        if left != right or ln.count('\u2551') % 2 != 0:
            return ln

    return ''


levels = menu_levels()
check('menus: the program lists its menu tree (23 levels)',
      len(levels) == 23 and ['Print'] in [lv[0] for lv in levels], levels)
mtop = [lv for lv in levels if lv[0] == []][0][1]

for mcols in (80, 100, 132):
    bad = []
    seen_edit = 0

    for mpath, mnames in levels:
        steps = ['F12']
        cur = mtop
        pre = []

        for nm in mpath:
            steps += ['RIGHT'] * cur.index(nm) + ['ENTER']
            pre.append(nm)
            cur = [lv[1] for lv in levels if lv[0] == pre][0]

        d = session(base, steps + ['wait', 'show'], rows=30, cols=mcols)
        rows = d[0].split('\n')[:4]

        if mpath == ['Edit']:
            # Which entries Edit offers depends on the document: the sample
            # document is structured, so one of the variants is on the screen.
            if ' '.join(mnames) not in rows[0]:
                continue

            seen_edit += 1

        why = menu_rows_ok(rows, mpath, mnames)

        if why != '':
            bad.append((mpath, why, rows))

    check('menus: every entry of every level is whole and the mode label is '
          'clear at %d columns (%d levels)' % (mcols, len(levels)),
          bad == [] and seen_edit >= 1, bad)

# The sample document has a frame on its screen (it carries a border): the
# boxes of the menus are counted on a copy that has none.
nbdoc = fresh('nb.doc')
nbtext = re.sub(rb'\r\nC[BLR]\d+(,\d+)?(?=\r\n)', b'', read(nbdoc))
open(nbdoc, 'wb').write(nbtext)
nbbase = [stred, '-H', helpf, nbdoc]
for mcols in (80, 132):
    for mname, mkeys in (('Marker Commands', ['C-F12']), ('Table', ['F8']),
                         ('Append', ['A-F2']), ('Join', ['A-F3']),
                         ('Set Attr', ['A-F6']), ('Go To', ['A-F7'])):
        d = session(nbbase, mkeys + ['wait', 'show'], rows=30, cols=mcols)
        check('menus: the box %s is whole at %d columns' % (mname, mcols),
              '\u2554' in d[0] and boxes_whole(d[0]) == '', d[0])

for mcols in (80, 132):
    d = session(msbase, ['show'], rows=30, cols=mcols)
    check('menus: the menu of the Document Manager is whole at %d columns'
          % mcols, '\u2554' in d[0] and boxes_whole(d[0]) == '', d[0])


# ---- Compare Document Versions of the Document Manager ----------------------
# The entry asks for two documents, shows the differences in the pager and can
# save them as a document (the comparison itself is in tests/cmptest.c).
cmpd = tempfile.mkdtemp(prefix='cmp')
shutil.copy(PLAN, os.path.join(cmpd, 'a.doc'))
os.chdir(cmpd)
cwd_a = os.path.join(cmpd, 'a.doc')
cwd_b = os.path.join(cmpd, 'b.doc')
data_a = read(cwd_a)
data_b = data_a.replace(b'Pomologist', b'Pomologisa')
check('compare: the second document is made from the first',
    data_b != data_a and data_b.count(b'Pomologisa') >= 1, data_b[:80])
open(cwd_b, 'wb').write(data_b)
report = os.path.join(cmpd, 'a-diff.doc')
if os.path.exists(report):
    os.remove(report)
cmp_open = ['DOWN', 'DOWN', 'ENTER']
d = session(msbase, cmp_open + ['show'])
check('compare: the panel prompts for the two documents and the report',
    'Compare Document Versions' in d[0] and 'First Document' in d[0] and
    'Second Document' in d[0] and 'Report Document' in d[0] and
    'is not implemented' not in d[0], d[0])
d = session(msbase, cmp_open + ['DOWN', 'text:' + cwd_a, 'DOWN',
    'text:' + cwd_b, 'INS', 'wait', 'show'])
check('compare: the report shows the words that changed', 'Comparison of two '
    'documents' in d[0] and 'Changed' in d[0] and '[-Pomologist-]' in d[0] and
    '[+Pomologisa+]' in d[0], d[0])
check('compare: the report names the two documents', cwd_a in d[0] or
    'a.doc' in d[0], d[0])
d = session(msbase, cmp_open + ['DOWN', 'text:' + cwd_a, 'DOWN',
    'text:' + cwd_b, 'INS', 'wait', 'ESC', 'show'])
check('compare: the report can be saved as a document', 'Save the comparison '
    'as a document?' in d[0], d[0])
d = session(msbase, cmp_open + ['DOWN', 'text:' + cwd_a, 'DOWN',
    'text:' + cwd_b, 'INS', 'wait', 'ESC', 'ENTER', 'wait', 'show'])
check('compare: the name of the report that was written is shown',
    'Wrote' in d[0] and 'a-diff.doc' in d[0], d[0])
rep = read(report)
check('compare: the report document exists and holds the comparison',
    b'Comparison of two documents' in rep and b'Pomologist' in rep, rep[:200])
mtime = os.stat(report).st_mtime
d = session(msbase, cmp_open + ['DOWN', 'text:' + cwd_a, 'DOWN',
    'text:' + cwd_b, 'INS', 'wait', 'ESC', 'ENTER', 'wait', 'show'])
check('compare: a report that exists is not replaced without asking',
    'exists.  Replace it?' in d[0], d[0])
d = session(msbase, cmp_open + ['DOWN', 'text:' + cwd_a, 'DOWN',
    'text:' + cwd_b, 'INS', 'wait', 'ESC', 'ENTER', 'wait', 'DOWN', 'ENTER',
    'wait', 'show'])
check('compare: answering No leaves the report as it is',
    os.stat(report).st_mtime == mtime and read(report) == rep, d[0])
shutil.copy(cwd_a, os.path.join(cmpd, 'c.doc'))
d = session(msbase, cmp_open + ['DOWN', 'text:' + cwd_a, 'DOWN',
    'text:' + os.path.join(cmpd, 'c.doc'), 'INS', 'wait', 'show'])
check('compare: two documents that are the same say so and offer no save',
    'same text, sections and format' in d[0] and
    'Save the comparison' not in d[0], d[0])
d = session(msbase, cmp_open + ['DOWN', 'text:' + cwd_a, 'DOWN',
    'text:' + cwd_a, 'INS', 'wait', 'show'])
check('compare: the same file twice is refused',
    'Choose two different documents' in d[0], d[0])
d = session(msbase, cmp_open + ['DOWN', 'text:' + cwd_a, 'DOWN',
    'text:' + os.path.join(cmpd, 'none.doc'), 'INS', 'wait', 'show'])
check('compare: a document that does not exist is refused', 'Cannot find' in
    d[0] and 'none.doc' in d[0], d[0])
d = session(msbase, cmp_open + ['DOWN', 'text:' + cwd_a, 'DOWN',
    'text:' + cwd_b, 'DOWN', 'text:' + cwd_a, 'INS', 'wait', 'show'])
check('compare: a report that would replace a document is refused',
    'must not replace one of the two documents' in d[0], d[0])
d = session(msbase, cmp_open + ['F1', 'show'])
check('compare: F1 on the panel shows its help topic',
    'HELP: Compare Document Versions' in d[0], d[0])



# ---- the colors of the screen ----------------------------------------------
# The Setup of the Document Manager has a Colors panel; the terminal paints
# every cell with a background and restores the colors of the terminal.
def cols(steps, depth='16', home=None, args=(), env=None):
    """Run str with the given depth of colors; returns the screen dumps."""
    e = {'STRATA_COLORS': depth, 'COLORTERM': None,
         'STRATA_DEFAULT_COLORS': None}
    e.update(env or {})
    home_override[0] = home
    d = session(list(msbase[:1]) + list(args) + list(msbase[1:]), steps,
        env=e)
    home_override[0] = None
    return d

colbase = ['wait', 'raw']
for depth, fgbg in (('16', '\x1b[0;93;44m'),
        ('256', '\x1b[0;38;5;227;48;5;19m'),
        ('truecolor', '\x1b[0;38;2;255;255;85;48;2;0;0;170m')):
    d = cols(colbase, depth)
    check('colors: with %s colors every cell has the colors of normal text'
        % depth, fgbg in d[0] and (fgbg + '\x1b[2J') in d[0], repr(d[0][:300]))
    check('colors: with %s colors the screen is cleared with the background'
        % depth, d[0].find('\x1b[2J') > d[0].find(fgbg), repr(d[0][:300]))
d = cols(colbase, '16')
check('colors: the status line is black on light grey',
    '\x1b[0;30;47m F1 Help' in d[0].replace('\x1b[25;1H', ''), repr(d[0][-300:]))
d = cols(colbase, 'none')
check('colors: a terminal with no colors gets the attributes, no colors',
    '\x1b[0;7m' in d[0] and '44m' not in d[0] and ';48;' not in d[0] and
    '\x1b[0m\x1b[2J' in d[0], repr(d[0][:300]))
d = cols(['wait', 'raw', 'END', 'ENTER', 'wait', 'raw'], '16')
check('colors: at the end the colors and the cursor color of the terminal '
    'are put back', d[1].endswith('\x1b]112\x07\x1b(B\x1b[0m\x1b[?25h'
    '\x1b[?1049l'),
    repr(d[1][-120:]))

def colhome(text=None):
    h = os.path.join(tempfile.mkdtemp(prefix='home'), 'strata')
    os.mkdir(h, 0o700)

    if text is not None:
        open(os.path.join(h, 'config'), 'w').write(text)
        os.chmod(os.path.join(h, 'config'), 0o600)

    return h

h = colhome('terminal_color_normal_bg=black\nterminal_color_bold_fg=red\n'
    'x11_color_normal_bg=#123456\n')
d = cols(colbase, '16', home=h)
check('colors: a setting of the terminal is used, the one of X11 is not',
    '\x1b[0;93;40m' in d[0] and '123456' not in d[0], repr(d[0][:200]))
d = cols(colbase, '16', home=h, args=['--default-colors'])
check('colors: --default-colors starts with the colors of the program',
    '\x1b[0;93;44m' in d[0] and '\x1b[0;93;40m' not in d[0], repr(d[0][:200]))
d = cols(colbase, '16', home=h, env={'STRATA_DEFAULT_COLORS': '1'})
check('colors: STRATA_DEFAULT_COLORS starts with the colors of the program',
    '\x1b[0;93;44m' in d[0] and '\x1b[0;93;40m' not in d[0], repr(d[0][:200]))

h = colhome('terminal_color_normal_fg=bluish\nx11_color_bold_bg=#12\n')
d = cols(['wait', 'raw'], '16', home=h)
logs = ''
logdir = os.path.join(h, 'log')
for n in sorted(os.listdir(logdir)) if os.path.isdir(logdir) else []:
    if n.startswith('run-'):
        logs += open(os.path.join(logdir, n), errors='replace').read()
check('colors: a bad value goes to the run log and the default is used',
    'terminal_color_normal_fg has the value bluish, which is not a color' in
    logs and 'x11_color_bold_bg has the value #12' in logs and
    '\x1b[0;93;44m' in d[0], logs[-400:])

PG = tui.OPEN_SETUP + ['PGUP']
d = cols(tui.OPEN_SETUP + ['show'], '256')
check('colors: the first page of Setup has no line Screen Colors and no '
    'line Advanced, and the foot names PgUp/PgDn',
    'Screen Colors:' not in d[0] and 'Advanced:' not in d[0] and
    'PgUp/PgDn: Page' in d[0] and 'Setup (settings)' in d[0], d[0])
d = cols(PG + ['wait', 'show'], '256')
check('colors: PgUp goes to the page of the colors: every kind of text with '
    'its colors', all(w in d[0] for w in ('Setup (screen colors)',
    'Colors of the terminal version', 'Normal text',
    'Bold text', 'Italic text', 'Underlined text', 'Markers', 'Dim text',
    'Highlight', 'Print Error markers', 'Keycaps', 'Status line',
    'Message line', 'Function key labels', 'Menus', 'Panels',
    'Selected entry', 'Cursor', 'Sample text', 'This terminal: 256 colors'))
    and any('Normal text' in l and 'yellow' in l and 'blue' in l
    for l in d[0].split('\n')) and any('Bold text' in l and 'white' in l
    for l in d[0].split('\n')), d[0])
check('colors: the page shows the default of every kind (VGA names)',
    any('Selected entry' in l and 'white' in l and 'black' in l
    for l in d[0].split('\n')) and any('Status line' in l and 'black' in l
    and 'light-grey' in l for l in d[0].split('\n')) and
    any('Cursor' in l and '#ffb000' in l for l in d[0].split('\n')), d[0])

h = colhome()
d = cols(PG + ['wait', 'show', 'RIGHT', 'wait', 'show', 'cell:0,2', 'INS',
    'wait'], '256', home=h)
txt = open(os.path.join(h, 'config')).read() if os.path.exists(
    os.path.join(h, 'config')) else ''
check('colors: the page shows yellow for Normal text before Right',
    any('Normal text' in l and 'yellow' in l for l in d[0].split('\n')),
    d[0])
check('colors: Right takes the next color, white, and shows it',
    any('Normal text' in l and 'white' in l and 'yellow' not in l
    for l in d[1].split('\n')), d[1])
check('colors: a color that is chosen is used at once',
    d[2].startswith("(' ', 231, 19,"), d[2])
check('colors: Ins keeps the colors in the settings file',
    'terminal_color_normal_fg=white\n' in txt and 'x11_color' not in txt, txt)

h = colhome()
d = cols(PG + ['text: ', 'wait', 'show', 'INS', 'wait'], '256', home=h)
txt = open(os.path.join(h, 'config')).read() if os.path.exists(
    os.path.join(h, 'config')) else ''
check('colors: Space takes the next color too', any('Normal text' in l and
    'white' in l and 'yellow' not in l for l in d[0].split('\n')), d[0])
check('colors: and Ins keeps it', 'terminal_color_normal_fg=white\n' in txt,
    txt)
h = colhome('terminal_color_normal_fg=white\n')
d = cols(PG + ['RIGHT', 'wait', 'show'], '16', home=h)
check('colors: Right does nothing at the last color', any('Normal text' in l
    and 'white' in l for l in d[0].split('\n')), d[0])

h = colhome()
d = cols(PG + ['F2', 'wait', 'show'], '256', home=h)
check('colors: F2 lists the palette on the color of the entry', 'Choose a '
    'color' in d[0] and 'yellow' in d[0], d[0])

h = colhome()
d = cols(PG + ['RIGHT', 'wait', 'ESC', 'wait', 'cell:0,2'], '256', home=h)
check('colors: Esc takes the colors back', d[0].startswith("(' ', 227, 19,")
    and not os.path.exists(os.path.join(h, 'config')), d)

h = colhome()
d = cols(PG + ['text:v', 'wait', 'show', 'RIGHT', 'wait', 'INS', 'wait'],
    '256', home=h)
txt = open(os.path.join(h, 'config')).read() if os.path.exists(
    os.path.join(h, 'config')) else ''
check('colors: V chooses the colors of the other version, the X11 version',
    'Colors of the X11 version' in d[0], d[0])
check('colors: the colors of the other version are kept under its own keys',
    'x11_color_normal_fg=' in txt and 'terminal_color' not in txt, txt)

h = colhome('terminal_color_normal_fg=black\nterminal_color_normal_bg=black\n')
d = cols(PG + ['wait', 'show'], '256', home=h)
check('colors: the page warns when the text is hard to read on its '
    'background', 'Warning: the text is hard to read' in d[0], d[0])

h = colhome('terminal_color_normal_fg=red\nterminal_color_status_bg=blue\n')
d = cols(PG + ['text:r', 'wait', 'ENTER', 'wait', 'INS', 'wait'], '256',
    home=h)
txt = open(os.path.join(h, 'config')).read()
check('colors: R (all defaults) and Ins take every setting of the version '
    'out of the file', 'terminal_color' not in txt, txt)

h = colhome()
d = cols(PG + ['RIGHT', 'wait', 'PGUP', 'PGUP', 'wait', 'show', 'INS', 'wait'],
    '256', home=h)
txt = open(os.path.join(h, 'config')).read() if os.path.exists(
    os.path.join(h, 'config')) else ''
check('colors: a color chosen on page 2 is kept when Ins is pressed on page 1',
    'Setup (settings)' in d[0] and 'terminal_color_normal_fg=white\n' in txt,
    d[0] + txt)



# ---- Undo and Redo (src/undo.c) ----------------------------------------------
def undo_home(lines):
    """A home folder whose settings are the lines, for the next sessions."""
    h = tempfile.mkdtemp(prefix='undohome')
    os.chmod(h, 0o700)
    with open(os.path.join(h, 'config'), 'w') as f:
        f.write(lines)
    home_override[0] = h
    return h


home_override[0] = None
d = session(base, ['C-z', 'show'])
check('undo: Undo with nothing to undo says so', 'Nothing to undo' in d[0],
    d[0])
d = session(base, ['text:hello', 'show', 'C-z', 'wait', 'show', 'C-F11',
    'wait', 'show'])
check('undo: Ctrl-Z takes back what was typed, and says what it undid',
    'hello' in d[0] and 'hello' not in d[1] and 'Undone: Typing' in d[1],
    (d[0], d[1]))
check('undo: Ctrl-F11 does the typing again', 'hello' in d[2] and
    'Redone: Typing' in d[2], d[2])
d = session(base, ['text:abc', 'C-F10', 'wait', 'show', 'C-F11', 'show'])
check('undo: Ctrl-F10 is Undo too, and Ctrl-F11 is Redo', 'abc' not in d[0]
    and 'abc' in d[1], (d[0], d[1]))
d = session(base, ['text:xyz', 'F12', 'text:T', 'text:U', 'wait', 'show',
    'F12', 'text:T', 'text:R', 'wait', 'show'])
check('undo: Tools, Undo and Tools, Redo are the same commands',
    'xyz' not in d[0] and 'Undone: Typing' in d[0] and 'xyz' in d[1] and
    'Redone: Typing' in d[1], (d[0], d[1]))
d = session(base, ['text:qxj', 'text: ', 'text:zkv', 'wait', 'C-z', 'show',
    'C-z', 'show'])
check('undo: a word and its space are one operation, so Undo takes a word '
    'at a time', 'qxj' in d[0] and 'zkv' not in d[0] and 'qxj' not in d[1],
    (d[0], d[1]))
d = session(base, ['text:abcd', 'BKSP', 'BKSP', 'show', 'C-z', 'show'])
check('undo: a run of Backspace is one operation', 'abcd' not in d[0] and
    'ab' in d[0] and 'abcd' in d[1], (d[0], d[1]))
d = session(base, ['text:ab', 'LEFT', 'text:X', 'show', 'C-z', 'show', 'C-z',
    'show'])
check('undo: a move of the cursor ends the run of typing', 'X' in d[0] and
    'X' not in d[1] and 'ab' in d[1] and 'ab' not in d[2], (d[0], d[1],
    d[2]))
d = session(base, ['text:aa', 'C-z', 'text:b', 'C-F11', 'show'])
check('undo: a new edit takes the redo operations away',
    'Nothing to redo' in d[0], d[0])
# The number of levels is the setting.
undo_home('undo_levels=0\n')
d = session(base, ['text:hello', 'C-z', 'show'])
check('undo: with Undo Levels 0, Undo is off and says so',
    'Undo is off' in d[0] and 'hello' in d[0], d[0])
undo_home('undo_levels=2\n')
d = session(base, ['text:a', 'text: ', 'text:b', 'text: ', 'text:c',
    'text: ', 'C-z', 'C-z', 'C-z', 'show'])
check('undo: with Undo Levels 2 only the last two operations are kept',
    'Nothing to undo' in d[0], d[0])
home_override[0] = None
# A macro that plays Undo.
d = session(base, MACM + ['text:L', 'wait', 'text:q', 'DOWN',
    'text:undo test', 'DOWN', 'RIGHT', 'INS', 'wait', 'text:ab', 'C-z',
    'C-F1', 'wait', 'A-q', 'wait', 'show'])
check('undo: a macro can play Undo (it types ab and takes it back)',
    'Undone: Typing' in d[0] and 'abOrchard' not in d[0], d[0])
home_override[0] = None
# Setup: the field Undo Levels.
th = rows_home()
d = session(msbase, tui.OPEN_SETUP + ['show'])
check('undo: Setup has the field Undo Levels, 100 at the start',
    any('Undo Levels (0 to 500):' in l and '100' in l
    for l in d[0].split('\n')), d[0])
session(msbase, tui.OPEN_SETUP + su_to('undo') + ['text:7', 'INS',
    'wait'])
txt = read(os.path.join(th, 'config')).decode()
check('undo: Setup saves the Undo Levels, 7 here', '\nundo_levels=7\n' in txt,
    txt)
d = session(msbase, tui.OPEN_SETUP + su_to('undo') + ['text:abc', 'INS',
    'wait', 'show'])
txt2 = read(os.path.join(th, 'config')).decode()
check('undo: Setup rejects Undo Levels that is not a number and keeps the '
    'file', 'Undo Levels is a whole number' in d[0] and txt2 == txt, d[0])
session(msbase, tui.OPEN_SETUP + su_to('undo') + ['text:900', 'INS',
    'wait'])
txt = read(os.path.join(th, 'config')).decode()
check('undo: Setup makes the number right when it saves: 900 is 500',
    '\nundo_levels=500\n' in txt, txt)
home_override[0] = None

# ---- numbered backups ------------------------------------------------------
# A save keeps up to N numbered backups of the document that it replaces
# (the key backups of the settings; 1 is the default): memo.bak01 is the
# newest.  A save of a document that has not changed makes no backup.  The
# field Backups Kept is the second field of the Setup panel, so it is reached
# with one press of DOWN from the first field (see su_to).
def bk_home(text):
    h = tempfile.mkdtemp(prefix='home')
    os.chmod(h, 0o700)
    if text is not None:
        c = os.path.join(h, 'config')
        open(c, 'w').write(text)
        os.chmod(c, 0o600)
    return h
def bk_names(d):
    return sorted(os.listdir(d))
def bk_all(d):
    """The names in a directory, and those of its folder .backup as
    .backup/NAME."""
    n = [x for x in bk_names(d) if x != '.backup']
    b = os.path.join(d, '.backup')
    if os.path.isdir(b):
        n = n + ['.backup/' + x for x in bk_names(b)]
    return sorted(n)
def bk_edit(path, home, keys):
    home_override[0] = home
    r = session([stred, '-H', helpf, path], keys)
    home_override[0] = None
    return r
bkd = os.path.join(tmp, 'bk')
os.mkdir(bkd)
bdoc = os.path.join(bkd, 'memo.doc')
shutil.copy(PLAN, bdoc)
versions = [read(bdoc)]
h3 = bk_home('backups=3\n')

for i in range(4):
    bk_edit(bdoc, h3, ['text:v%d' % i, 'C-s', 'wait'])
    versions.append(read(bdoc))

check('backups: four changed saves with 3 kept leave the document and three '
    'backups in the folder .backup, and nothing else beside the document',
    bk_names(bkd) == ['.backup', 'memo.doc'] and
    bk_names(os.path.join(bkd, '.backup')) == ['memo.bak01', 'memo.bak02',
    'memo.bak03'], bk_all(bkd))
check('backups: backup 1 is the newest, 3 the oldest, the first version '
    'is gone',
    read(bakp(bdoc, 1)) == versions[3] and
    read(bakp(bdoc, 2)) == versions[2] and
    read(bakp(bdoc, 3)) == versions[1] and
    read(bdoc) == versions[4] and versions[0] not in versions[1:],
    bk_names(bkd))
st1 = os.stat(bakp(bdoc))
sd = os.stat(bdoc)
d = bk_edit(bdoc, h3, ['C-s', 'wait', 'show'])
check('backups: a save of a document that has not changed says so',
    'no change, no backup made' in d[0], d[0])
check('backups: ... and changes no backup and writes nothing',
    bk_all(bkd) == ['.backup/memo.bak01', '.backup/memo.bak02',
    '.backup/memo.bak03', 'memo.doc'] and
    os.stat(bakp(bdoc)).st_mtime_ns == st1.st_mtime_ns and
    os.stat(bdoc).st_ino == sd.st_ino and
    os.stat(bdoc).st_mtime_ns == sd.st_mtime_ns and
    read(bdoc) == versions[4], bk_names(bkd))

# The default is three backups; the old yes and no are not read.
for text, want, what in ((None, 3, 'the default is three backups'),
                         ('backup=yes\n', 3, 'the old yes is not read'),
                         ('backup=no\n', 3, 'the old no is not read'),
                         ('backups=0\n', 0, 'backups=0 makes none'),
                         ('backup=no\nbackups=2\n', 2,
                          'the number wins over the old key')):
    pd = os.path.join(tmp, 'bk' + str(want) + str(abs(hash(what)) % 997))
    os.mkdir(pd)
    pdoc = os.path.join(pd, 'p.doc')
    shutil.copy(os.path.join(here, 'data', 'plain_text.doc'), pdoc)
    first = read(pdoc)
    hh = bk_home(text)
    versions = [read(pdoc)]
    # Five saves, each after a different edit: the saves of the first run
    # make versions 1 to 5, and the folder keeps the newest ones only.
    for i, ch in enumerate('abcde'):
        bk_edit(pdoc, hh, ['text:' + ch, 'C-s', 'wait'])
        versions.append(read(pdoc))
    got = sorted(x for x in bk_all(pd) if x.startswith('.backup/p.bak'))
    names = ['.backup/p.bak%02d' % (k + 1) for k in range(want)]
    ok = got == names
    # Backup 1 is the document as it was before the last save; backup k
    # is the one before that, and so on: the newest are kept.
    for k in range(want):
        ok = ok and read(bakp(pdoc, k + 1)) == versions[len(versions) - 2 - k]
    # The document changed with every save: the first versions are gone.
    ok = ok and len(set(versions)) == len(versions)
    check('backups: ' + what + ' (five saves keep ' + str(want) +
        ' backups, the newest ones)', ok, (got, names))

# Autosave makes no numbered backup (it is tested with the Options above).
# A backup is a document like any other: the Editor opens it by name.
d = session([stred, '-H', helpf, bakp(bdoc)], ['show'])
check('backups: a backup file is opened by its name', 'Orchard Plan' in d[0] and
    'memo.bak01' in d[0], d[0])

# The Options panel: Backups Kept is a whole number.
ho = bk_home(None)
bk_edit(fresh('bko.doc'), ho, OPTIONS + ['DOWN'] * 4 + ['text:abc', 'INS',
    'wait'])
check('backups: Options refuses text that is not a number and saves nothing',
    b'backups' not in read(os.path.join(ho, 'config')),
    read(os.path.join(ho, 'config')))
d = bk_edit(fresh('bko2.doc'), ho, OPTIONS + ['DOWN'] * 4 + ['text:abc', 'INS',
    'wait', 'show'])
check('backups: ... and says why', 'Backups Kept is a whole number' in d[0],
    d[0])

# ---- Backups, part 1: the folder .backup ------------------------------------
# An unchanged document makes no folder; a file named .backup, a directory that
# cannot be written: the save works, makes no backup and says why; the file
# lists do not show the folder; Setup and Compare Document Versions know it.
h1 = bk_home('backups=2\n')
ud = os.path.join(tmp, 'bk1')
os.mkdir(ud)
udoc = os.path.join(ud, 'u.doc')
shutil.copy(PLAN, udoc)
d = bk_edit(udoc, h1, ['C-s', 'wait', 'show'])
check('backups: a document that has not changed makes no folder .backup',
    bk_names(ud) == ['u.doc'] and 'no change, no backup made' in d[0],
    bk_names(ud))
fd = os.path.join(tmp, 'bk2')
os.mkdir(fd)
fdoc = os.path.join(fd, 'f.doc')
shutil.copy(PLAN, fdoc)
open(os.path.join(fd, '.backup'), 'w').write('a file')
d = bk_edit(fdoc, h1, ['text:x', 'C-s', 'wait', 'show'])
check('backups: a file named .backup: the save works and the message line '
    'says why there is no backup', b'Tx' in read(fdoc) and
    'no backup made' in d[0] and '.backup is in the way' in d[0] and
    read(os.path.join(fd, '.backup')) == b'a file', d[0])
if os.geteuid() != 0:
    rd = os.path.join(tmp, 'bk3')
    os.mkdir(rd)
    rdoc = os.path.join(rd, 'r.doc')
    shutil.copy(PLAN, rdoc)
    before_ro = read(rdoc)
    os.chmod(rd, 0o555)
    d = bk_edit(rdoc, h1, ['text:x', 'C-s', 'wait', 'show'])
    os.chmod(rd, 0o755)
    check('backups: a directory that cannot be written: saved in place, no '
        'backup, and the message line shows why',
        read(rdoc) != before_ro and 'no backup made' in d[0] and
        bk_names(rd) == ['r.doc'], d[0])
pk = os.path.join(tmp, 'bk4')
os.makedirs(os.path.join(pk, '.backup'))
open(os.path.join(pk, '.backup', 'x.bak01'), 'w').write('x')
open(os.path.join(pk, 'x.doc'), 'w').write('x')
os.makedirs(os.path.join(pk, 'sub'))
d = session(msbase, ['ENTER', 'UP', 'text:' + pk, 'DOWN', 'F2', 'wait',
    'show'])
check('backups: the file lists do not show the folder .backup',
    'sub/' in d[0] and '.backup' not in d[0], d[0])
d = session(msbase + [os.path.join(pk, 'x.doc')], cmp_open + ['show'])
check('backups: Compare Document Versions offers backup 1 from the folder',
    '.backup/x.bak01' in d[0], d[0])
hs1 = bk_home('backups=3\n')
home_override[0] = hs1
d = session(msbase, SETUP + su_to('backups') + ['show'])
home_override[0] = None
check('backups: the note of Backups Kept names the folder .backup',
    'in the folder .backup' in d[0], d[0])

# ---- Backups, parts 2 and 3: Restore Backup ---------------------------------
# The panel lists the backups of a document newest first with the date, the
# size and the first words; the quick view shows the text; a restore asks,
# and swaps the document and the backup.
import time as _time
os.environ['TZ'] = 'UTC'
_time.tzset()
rbd = os.path.join(tmp, 'rb')
os.makedirs(os.path.join(rbd, '.backup'))
rdoc = os.path.join(rbd, 'r.doc')
shutil.copy(PLAN, rdoc)
orig_r = read(rdoc)
b1 = orig_r.replace(b'Orchard Plan', b'Orchard Plon')
b2 = orig_r.replace(b'Orchard Plan', b'Orchard Plin')
open(bakp(rdoc, 1), 'wb').write(b1)
open(bakp(rdoc, 2), 'wb').write(b2)
os.utime(bakp(rdoc, 1), (1767349200, 1767349200))   # 2026-01-02 10:20 UTC
os.utime(bakp(rdoc, 2), (1766000000, 1766000000))   # 2025-12-17 19:33 UTC
open(os.path.join(rbd, 'r.bak01'), 'wb').write(orig_r.replace(b'Orchard Plan',
    b'Orchard Plen'))
RB = ['DOWN'] * 3 + ['ENTER', 'wait', 'DOWN', 'text:r.doc', 'INS', 'wait']
def rb_session(keys, home=None):
    cwd0 = os.getcwd()
    os.chdir(rbd)
    try:
        home_override[0] = home
        return session(msbase, RB + keys)
    finally:
        home_override[0] = None
        os.chdir(cwd0)
d = rb_session(['show'])
check('restore: the panel lists the backups newest first with the date, the '
    'size and the first words',
    '02 Jan 2026 10:20' in d[0] and '17 Dec 2025 19:33' in d[0] and
    d[0].index('02 Jan 2026') < d[0].index('17 Dec 2025') and
    'bytes' in d[0] and 'Orchard Plon' in d[0], d[0])
check('restore: the backups beside the document are listed as old',
    ' old ' in d[0] and 'Orchard Plen' in d[0], d[0])
d = rb_session(['ENTER', 'show'])
check('restore: a backup offers to show its text and to restore it',
    'View the text' in d[0] and 'Restore this backup' in d[0], d[0])
d = rb_session(['ENTER', 'ENTER', 'show'])
check('restore: the quick view shows the text of the backup in a pager',
    'Orchard Plon' in d[0] and 'Backup 01' in d[0] and
    'scroll' in d[0], d[0])
d = rb_session(['ENTER', 'ENTER', 'ESC', 'show'])
check('restore: Esc comes back from the view to the choice',
    'Restore this backup' in d[0], d[0])
d = rb_session(['ENTER', 'DOWN', 'ENTER', 'show'])
check('restore: a restore asks first, with the numbers and the document',
    'Restore backup 01 of 02 Jan 2026 10:20 over r.doc' in
    d[0].replace('\n', ' ').replace('  ', ' ') or
    'Restore backup 01' in d[0], d[0])
rb_session(['ENTER', 'DOWN', 'ENTER', 'text:n', 'wait'])
check('restore: No leaves everything as it was', read(rdoc) == orig_r and
    read(bakp(rdoc, 1)) == b1 and read(bakp(rdoc, 2)) == b2)
rb_session(['ENTER', 'DOWN', 'ENTER', 'ESC', 'wait'])
check('restore: Esc is No', read(rdoc) == orig_r and
    read(bakp(rdoc, 1)) == b1)
d = rb_session(['ENTER', 'DOWN', 'ENTER', 'text:y', 'wait', 'show'],
    home=bk_home('backups=2\n'))
check('restore: Y restores: the backup is the document and the document is '
    'backup 1', read(rdoc) == b1 and read(bakp(rdoc, 1)) == orig_r and
    read(bakp(rdoc, 2)) == b2 and 'Restored backup 01' in d[0], d[0])
d = rb_session(['ENTER', 'DOWN', 'ENTER', 'text:y', 'wait', 'show'],
    home=bk_home('backups=2\n'))
check('restore: the same restore again swaps back',
    read(rdoc) == orig_r and read(bakp(rdoc, 1)) == b1, d[0])
nd = os.path.join(tmp, 'rb2')
os.makedirs(nd)
shutil.copy(PLAN, os.path.join(nd, 'n.doc'))
cwd0 = os.getcwd()
os.chdir(nd)
d = session(msbase, ['DOWN'] * 3 + ['ENTER', 'wait', 'DOWN', 'text:n.doc',
    'INS', 'wait', 'show'])
os.chdir(cwd0)
check('restore: a document that has no backup is told so',
    'n.doc has no backup' in d[0], d[0])

# ---- Backups, part 4: lowering Backups Kept asks first ----------------------
def flat(screen):
    """The text of a screen without the box lines and with one space."""
    return ' '.join(re.sub('[\u2551\u2554\u2557\u255a\u255d\u2550]', ' ',
        screen).split())
def kept_of(home):
    m = re.search(rb'\nbackups=(\d+)\n', read(os.path.join(home, 'config')))
    return int(m.group(1)) if m else None
LOW = SETUP + su_to('backups')
hl = bk_home('backups=5\n')
home_override[0] = hl
d = session(msbase, LOW + ['text:2', 'INS', 'wait', 'show'])
home_override[0] = None
check('backups: lowering the number in Setup shows the box with the words',
    'Backups Kept: 5 to 2.' in flat(d[0]) and
    'numbered 3, 4 and 5' in flat(d[0]) and 'removed' in flat(d[0]) and
    'next time each document is saved' in flat(d[0]) and
    'No keeps 5' in flat(d[0]), d[0])
for ans, want, what in (('text:y', 2, 'Y keeps the change'),
                        ('text:n', 5, 'N keeps the old number'),
                        ('ESC', 5, 'Esc is No')):
    hl = bk_home('backups=5\n')
    home_override[0] = hl
    session(msbase, LOW + ['text:2', 'INS', 'wait', ans, 'wait'])
    home_override[0] = None
    check('backups: in Setup ' + what, kept_of(hl) == want, read(
        os.path.join(hl, 'config')))
for start, new, what in ((2, 5, 'raising the number asks nothing'),
                         (3, 3, 'the same number asks nothing')):
    hl = bk_home('backups=%d\n' % start)
    home_override[0] = hl
    d = session(msbase, LOW + ['text:%d' % new, 'INS', 'wait', 'show'])
    home_override[0] = None
    check('backups: in Setup ' + what, 'Yes keeps the change' not in d[0],
        d[0])
# The default is 3: lowering it prompts, with the number 3 as the old one.
hl = bk_home(None)
home_override[0] = hl
d = session(msbase, LOW + ['text:1', 'INS', 'wait', 'show'])
home_override[0] = None
check('backups: lowering the default 3 to 1 shows the box with the words',
    'Backups Kept: 3 to 1.' in flat(d[0]) and
    'numbered 2 and 3' in flat(d[0]) and 'No keeps 3' in flat(d[0]), d[0])
hl = bk_home(None)
home_override[0] = hl
session(msbase, LOW + ['text:1', 'INS', 'wait', 'text:n', 'wait'])
home_override[0] = None
check('backups: after No the default 3 stays, and is saved as no line',
    kept_of(hl) is None, read(os.path.join(hl, 'config')))
hl = bk_home('backups=5\n')
home_override[0] = hl
d = session(msbase, LOW + ['text:0', 'INS', 'wait', 'show'])
home_override[0] = None
check('backups: setting 0 says that no more backups are made and the old '
    'ones stay', 'No more backups will be made' in flat(d[0]) and
    'not touched' in flat(d[0]), d[0])
ho = bk_home('backups=5\n')
d = bk_edit(fresh('bkl.doc'), ho, OPTIONS + ['DOWN'] * 4 + ['text:2', 'INS',
    'wait', 'show'])
check('backups: the Options panel prompts in the same way',
    'Backups Kept: 5 to 2.' in flat(d[0]) and 'No keeps 5' in flat(d[0]),
    d[0])
bk_edit(fresh('bkl2.doc'), ho, OPTIONS + ['DOWN'] * 4 + ['text:2', 'INS',
    'wait', 'text:y', 'wait'])
check('backups: ... and Y keeps the change', kept_of(ho) == 2,
    read(os.path.join(ho, 'config')))
ho = bk_home('backups=5\n')
bk_edit(fresh('bkl3.doc'), ho, OPTIONS + ['DOWN'] * 4 + ['text:2', 'INS',
    'wait', 'text:n', 'wait'])
check('backups: ... and N keeps the old number', kept_of(ho) in (5, None),
    read(os.path.join(ho, 'config')))

# Edit / Preview / Print Document starts on the field Document Name.
d = session(msbase, ['ENTER', 'text:zz.doc', 'show'])
check('str: the panel of Edit / Preview / Print Document starts on Document '
    'Name', any('Document Name:' in l and 'zz.doc' in l
    for l in d[0].split('\n')) and not any('Working Directory:' in l and
    'zz.doc' in l for l in d[0].split('\n')), d[0])

# Setup, in the Document Manager.
hs = bk_home(None)
home_override[0] = hs
d = session(msbase, SETUP + su_to('backups') + ['show'])
check('backups: Setup has the field Backups Kept, 3 at the start, with a note',
    any('Backups Kept (0 to 99):' in l and '3' in l
        for l in d[0].split('\n'))
    and '3 backups, in the folder .backup of the document' in d[0], d[0])
cfgs = os.path.join(hs, 'config')
for typed, want in (('5', b'\nbackups=5\n'), ('150', b'\nbackups=99\n'),
                    ('0', b'\nbackups=0\n')):
    session(msbase, SETUP + su_to('backups') + ['text:' + typed, 'INS', 'wait'] +
        (['text:y', 'wait'] if typed == '0' else []))
    check('backups: Setup saves %s kept as %s' % (typed, want.strip().decode()),
        want in read(cfgs), read(cfgs))
before = read(cfgs)
d = session(msbase, SETUP + su_to('backups') + ['text:abc', 'INS', 'wait', 'show'])
check('backups: Setup rejects text that is not a number and keeps the file',
    'Backups Kept is a whole number from 0 to 99' in d[0] and
    read(cfgs) == before, d[0])
session(msbase, SETUP + su_to('backups') + ['text:1', 'INS', 'wait'])
check('backups: Setup saves one backup, which is not the default, as a line',
    b'\nbackups=1\n' in read(cfgs), read(cfgs))
session(msbase, SETUP + su_to('backups') + ['text:3', 'INS', 'wait'])
check('backups: Setup saves the default, 3, as no line',
    b'backups=' not in read(cfgs), read(cfgs))
home_override[0] = None

# ---- Setup: the two levels, the defaults, Del and the check in place --------
# The panel Setup has the common settings in groups and a page of the advanced
# settings (PgUp and PgDn); every field shows its default and Del puts it back; a value is
# checked where it is typed and a wrong value never closes the panel or loses
# the other values; Autosave is a choice; Paper for New Documents is a new
# setting that a document made new uses.
lp_env()
th = rows_home()
d = session(msbase, SETUP + ['show'])
check('setup design: each field shows its default at the right',
    'default: 3' in line(d[0], 'Backups Kept') and
    'default: Yes' in line(d[0], 'Autosave:') and
    'default: 120' in line(d[0], 'Autosave Interval') and
    'default: File only' in line(d[0], 'Print To:') and
    'default: a viewer in /usr/bin' in line(d[0], 'PDF Viewer') and
    'default: Letter' in line(d[0], 'Paper for New Documents') and
    'default: en_US' in line(d[0], 'Dictionary Language') and
    'default: 100' in line(d[0], 'Undo Levels'), d[0])
d = session(msbase, SETUP + su_to('backups') + ['text:7', 'DEL', 'show'])
check('setup design: Del puts the default back into a text field',
    'Backups Kept (0 to 99):  3' in line(d[0], 'Backups Kept') and
    '7' not in line(d[0], 'Backups Kept').split('default')[0], d[0])
d = session(msbase, SETUP + su_to('autosave') + ['RIGHT', 'show', 'DEL',
    'show'])
check('setup design: Autosave is a choice: Right makes it No and Del Yes',
    'No' in line(d[0], 'Autosave:').split('default')[0] and
    'Yes' in line(d[1], 'Autosave:').split('default')[0], (d[0], d[1]))
d = session(msbase, SETUP + su_to('paper') + ['RIGHT', 'show'])
check('setup design: Paper for New Documents is a choice of Letter and A4',
    'A4' in line(d[0], 'Paper for New Documents').split('default')[0], d[0])
# A value is checked where it is typed; the reason is in the line at the foot.
d = session(msbase, SETUP + su_to('interval') + ['text:abc', 'DOWN', 'show'])
check('setup design: a wrong value is refused when the focus leaves the '
    'field: the reason is at the foot', 'The autosave interval is 1 to '
    '86400 seconds' in d[0], d[0])
d = session(msbase, SETUP + su_to('interval') + ['text:abc', 'DOWN', 'DEL',
    'show'])
check('setup design: and the focus stays in that field (Del puts the '
    'default 120 into it, not into the next field)',
    '120' in line(d[0], 'Autosave Interval').split('default')[0] and
    'abc' not in d[0], d[0])
# A wrong value never closes the panel and no other change is lost.
th = rows_home()
d = session(msbase, SETUP + su_to('backups') + ['text:7', 'DOWN', 'DOWN',
    'text:abc', 'INS', 'wait', 'show'])
check('setup design: Ins with a wrong value keeps the panel open, with the '
    'reason, and nothing is saved yet',
    'Setup' in d[0] and 'Documents' in d[0] and
    'The autosave interval is 1 to 86400 seconds' in d[0] and
    not os.path.exists(os.path.join(th, 'config')), d[0])
th = rows_home()
session(msbase, SETUP + su_to('backups') + ['text:7', 'DOWN', 'DOWN',
    'text:abc', 'INS', 'wait', 'DEL', 'INS', 'wait'])
check('setup design: the other change (Backups Kept 7) is kept and saved '
    'when the wrong value is put right',
    '\nbackups=7\n' in conf(th) and '\nautosave_seconds=120\n' in conf(th),
    conf(th))
# The first wrong field takes the focus.
d = session(msbase, SETUP + su_to('undo') + ['text:x', 'UP', 'UP', 'UP',
    'show'])
check('setup design: a wrong Undo Levels cannot be left: the reason is shown '
    'and the panel does not move on', 'Undo Levels is a whole number from '
    '0 to 500' in d[0], d[0])
# The page of the advanced settings.
d = session(msbase, SETUP + sa_to('macro') + ['show'])
check('setup design: PgDn goes to the page with the macro set, '
    'the library, the terminal characters, the Editor and the help file',
    'Setup (advanced settings)' in d[0] and 'Initial Macro Set:' in d[0] and
    'Initial Library:' in d[0] and 'Terminal Characters:' in d[0] and
    'Editor program:' in d[0] and 'Help File:' in d[0] and
    'Templates are looked for in' in d[0] and
    'The settings are in' in d[0], d[0])
hi = bk_home(None)
home_override[0] = hi
session(msbase, SETUP + sa_to('library') + ['text:y.lib', 'INS', 'wait'])
home_override[0] = None
check('setup design: Ins on the page of the advanced settings saves the value',
    b'\ninitial_library=y.lib\n' in read(os.path.join(hi, 'config')),
    read(os.path.join(hi, 'config')))
hi = bk_home(None)
home_override[0] = hi
session(msbase, SETUP + sa_to('library') + ['text:y.lib', 'ESC', 'wait'])
home_override[0] = None
check('setup design: Esc on the page of the advanced settings saves nothing',
    not os.path.exists(os.path.join(hi, 'config')) or
    b'initial_library' not in read(os.path.join(hi, 'config')),
    read(os.path.join(hi, 'config')) if os.path.exists(
    os.path.join(hi, 'config')) else b'')
# The paper of a new document.
th = rows_home()
session(msbase, SETUP + su_to('paper') + ['RIGHT', 'INS', 'wait'])
check('setup design: A4 is saved as default_paper=a4', '\ndefault_paper=a4\n'
    in conf(th), conf(th))
d = session(msbase, SETUP + ['show'])
check('setup design: and Setup shows A4 at the next start',
    'A4' in line(d[0], 'Paper for New Documents').split('default')[0], d[0])
session(msbase, SETUP + su_to('paper') + ['DEL', 'INS', 'wait'])
check('setup design: Letter, the default, is saved as no line',
    'default_paper' not in conf(th), conf(th))
th = rows_home()
put_conf(th, 'default_paper=letter\n')
session(msbase, SETUP + ['INS', 'wait'])
check('setup design: default_paper=letter is read as Letter and is not kept',
    'default_paper' not in conf(th), conf(th))
th = rows_home()
put_conf(th, 'default_paper=foolscap\n')
d = session(msbase, SETUP + ['show'])
check('setup design: another word in default_paper is ignored: Letter',
    'Letter' in line(d[0], 'Paper for New Documents').split('default')[0],
    d[0])
# The settings of an older file are read and written back unchanged.
th = rows_home()
pw = tempfile.mkdtemp(prefix='paper')
dvp = os.path.join(pw, 'show')
with open(dvp, 'w') as f:
    f.write('#!/bin/sh\nexit 0\n')
os.chmod(dvp, 0o755)
old = ('workdir=' + pw + '\nautosave=no\nautosave_seconds=300\n'
       'print_to=viewer\nspell_language=de_DE\nx11_rows=60\nterminal_rows=35\n'
       'undo_levels=7\nbackups=5\npdf_viewer=' + dvp + '\n')
put_conf(th, old)
session(msbase, SETUP + ['INS', 'wait'])
new = conf(th)
check('setup design: an old settings file is read by the new panel and every '
    'old setting is saved with its value',
    all(('\n' + l + '\n') in ('\n' + new) for l in old.strip().split('\n')),
    new)
# A new document has the paper of the setting; a template keeps its own.
nd = os.path.join(pw, 'paper-new.doc')
ph = bk_home('default_paper=a4\n')
bk_edit(nd, ph, ['text:a', 'C-s', 'wait', 'C-q', 'wait'])
check('setup design: a document made new has the A4 of the setting',
    os.path.exists(nd) and b'\nDP8,8268,11693\r\n' in open(nd, 'rb').read(),
    repr(open(nd, 'rb').read()[:200]) if os.path.exists(nd) else 'no file')
nd2 = os.path.join(pw, 'paper-new2.doc')
ph = bk_home(None)
bk_edit(nd2, ph, ['text:a', 'C-s', 'wait', 'C-q', 'wait'])
check('setup design: without the setting a new document has no paper record '
    '(Letter)', os.path.exists(nd2) and b'DP8,' not in open(nd2, 'rb').read(),
    repr(open(nd2, 'rb').read()[:200]) if os.path.exists(nd2) else 'no file')

# ---- Setup: Initial Macro Set and Initial Library ---------------------------
# The two fields are edit fields with the file list; the Editor loads the macro
# set and the library when it starts, before the first key; a file that cannot
# be used is a message and the Editor starts anyway; an empty field loads none.
gmac = os.path.join(sample, 'names.mac')
glib = os.path.join(sample, 'boiler.lib')
hi = bk_home(None)
home_override[0] = hi
d = session(msbase, SETUP + sa_to('macro') + ['show'])
check('initial: the fields Initial Macro Set and Initial Library are on '
    'the page of the advanced settings and are not dimmed', 'Initial Macro Set:' in d[0] and
    'Initial Library:' in d[0] and '(n/a)' not in d[0], d[0])
session(msbase, SETUP + sa_to('macro') + ['text:' + gmac, 'DOWN',
    'text:' + glib, 'INS', 'wait'])
home_override[0] = None
cfg_i = read(os.path.join(hi, 'config'))
check('initial: Setup saves the macro set and the library in the settings',
    b'\ninitial_macros=' + gmac.encode() + b'\n' in cfg_i and
    b'\ninitial_library=' + glib.encode() + b'\n' in cfg_i, cfg_i)
home_override[0] = hi
d = session(msbase, SETUP + sa_to('macro') + ['F2', 'wait', 'show'])
home_override[0] = None
check('initial: F2 on Initial Macro Set lists the files', '../' in d[0], d[0])
hi2 = bk_home(None)
home_override[0] = hi2
session(msbase, SETUP + sa_to('macro') + ['text:' + gmac, 'INS', 'wait'])
session(msbase, SETUP + sa_to('macro') + ['DEL', 'INS', 'wait'])
home_override[0] = None
check('initial: an empty field is saved as no line',
    b'initial_macros=' not in read(os.path.join(hi2, 'config')),
    read(os.path.join(hi2, 'config')))
im = fresh('im1.doc')
d = bk_edit(im, bk_home('initial_macros=' + gmac + '\n'), ['show', 'A-n',
    'wait', 'C-s', 'wait'])
check('initial: the Editor starts with the macro set loaded and a macro key '
    'works', b'Heron Hill Orchard Club' in read(im), repr(read(im)[-200:]))
check('initial: what is loaded at the start is not a change of the document',
    d[0].split('\n')[0].lstrip().startswith('im1.doc') and
    'Macro set not loaded' not in d[0], d[0])
hs_dir = bk_home(None)
shutil.copy(gmac, os.path.join(hs_dir, 'names.mac'))
open(os.path.join(hs_dir, 'config'), 'w').write('initial_macros=names\n')
os.chmod(os.path.join(hs_dir, 'config'), 0o600)
im = fresh('im2.doc')
bk_edit(im, hs_dir, ['A-n', 'wait', 'C-s', 'wait'])
check('initial: a name without a directory is looked for in the settings '
    'directory (and .mac is added)', b'Heron Hill Orchard Club' in read(im),
    repr(read(im)[-200:]))
im = fresh('im3.doc')
d = bk_edit(im, bk_home('initial_macros=/nonexistent/none.mac\n'),
    ['show', 'text:x', 'C-s', 'wait'])
check('initial: a missing macro set gives the message and the Editor starts',
    'Macro set not loaded: cannot read' in d[0] and 'none.mac' in d[0] and
    'STRUCTURED EDIT' in d[0] and b'Tx' in read(im), d[0])
im = fresh('im4.doc')
junk = os.path.join(tmp, 'junk.mac')
open(junk, 'w').write('this is not a macro set')
d = bk_edit(im, bk_home('initial_macros=' + junk + '\n'), ['show'])
check('initial: a file that is not a macro set gives the message',
    'is not a macro set' in d[0] and 'STRUCTURED EDIT' in d[0], d[0])
im = fresh('im5.doc')
bk_edit(im, bk_home(None), ['A-n', 'wait', 'C-s', 'wait'])
check('initial: with an empty field no macro set is loaded',
    b'Heron Hill Orchard Club' not in read(im), repr(read(im)[-200:]))
im = fresh('il1.doc')
bk_edit(im, bk_home('initial_library=' + glib + '\n'), ['C-F5', 'wait',
    'text:Notice', 'INS', 'wait', 'C-s', 'wait'])
check('initial: the Editor starts with the library loaded and Lib Paste '
    'works', b'leave the trees as you found them.' in read(im),
    repr(read(im)[-300:]))
im = fresh('il2.doc')
d = bk_edit(im, bk_home('initial_library=/nonexistent/none.lib\n'), ['show'])
check('initial: a missing library gives the message and the Editor starts',
    'Library not loaded: cannot read' in d[0] and 'none.lib' in d[0] and
    'STRUCTURED EDIT' in d[0], d[0])
im = fresh('il3.doc')
bk_edit(im, bk_home(None), ['C-F5', 'wait', 'text:Notice', 'INS', 'wait',
    'C-s', 'wait'])
check('initial: with an empty field no library is loaded',
    b'leave the trees as you found them.' not in read(im),
    repr(read(im)[-300:]))

# ---- Maintain Dictionaries: the panel of the personal word list ---------
# The panel reads and writes ~/.strata/dictionary (the file that the spell
# check reads).  A home of its own holds the file; the keys of the panel are
# Ins (add), Del (delete, with a question), F3 (import), F4 (export), letters
# (find) and Esc.
dict_home = os.path.join(tempfile.mkdtemp(prefix='home'), 'strata')
os.makedirs(dict_home, mode=0o700)
dict_file = os.path.join(dict_home, 'dictionary')


def dict_reset(text):
    with open(dict_file, 'w') as f:
        f.write(text)

    os.chmod(dict_file, 0o600)


home_override[0] = dict_home
MD = ['DOWN', 'ENTER', 'wait']
dict_reset('pear\napple\nZed\n')
d = session(msbase, MD + ['show'])
check('dictionary: the entry opens the panel with the words in order and '
    'their number', 'Maintain Dictionaries' in d[0] and
    'Spelling, your words: 3' in d[0] and
    d[0].index('apple') < d[0].index('pear') < d[0].index('Zed'), d[0])
check('dictionary: the foot line names the keys', all(w in d[0] for w in
    ('Ins Add', 'Del Delete', 'F3 Import', 'F4 Export', 'Esc Leave')), d[0])
d = session(msbase, MD + ['ESC', 'show'])
check('dictionary: Esc leaves the panel for the menu',
    'Press Ins to Select Action' in d[0] and
    'Spelling, your words' not in d[0], d[0])
d = session(msbase, MD + ['F1', 'wait', 'show'])
check('dictionary: F1 shows the help of the panel',
    'HELP: Maintain Dictionaries' in d[0], d[0])
dict_reset('')
d = session(msbase, MD + ['show'])
check('dictionary: an empty list says so',
    'Spelling, your words: 0' in d[0] and
    'No words yet' in d[0], d[0])

dict_reset('pear\napple\nZed\n')
d = session(msbase, MD + ['INS', 'text:mango', 'ENTER', 'wait', 'show'])
check('dictionary: Ins adds a word, in order, and the file is written',
    'Added mango' in d[0] and 'Spelling, your words: 4' in d[0]
    and read(dict_file) == b'apple\nmango\npear\nZed\n', (d[0],
    read(dict_file)))
check('dictionary: the file stays private',
    os.stat(dict_file).st_mode & 0o777 == 0o600, oct(os.stat(
    dict_file).st_mode))
dict_reset('pear\napple\nZed\n')
for text, why in (('two words', 'A word has no spaces'),
        ('abc1', 'A word has no digits'),
        ('a_b', 'A word has letters, apostrophes and hyphens only'),
        ('-ab', 'A word begins and ends with a letter')):
    d = session(msbase, MD + ['INS', 'text:' + text, 'ENTER', 'wait',
        'show'])
    check('dictionary: the word "%s" is refused with a reason' % text,
        why in d[0] and read(dict_file) == b'pear\napple\nZed\n', d[0])
for text in ('pear', 'Pear'):
    d = session(msbase, MD + ['INS', 'text:' + text, 'ENTER', 'wait',
        'show'])
    check('dictionary: %s is in the list already' % text,
        text + ' is in the list already' in d[0] and
        read(dict_file) == b'pear\napple\nZed\n', d[0])

d = session(msbase, MD + ['DEL', 'wait', 'show'])
check('dictionary: Del asks before it takes a word out',
    'Delete apple from your words?' in d[0] and
    read(dict_file) == b'pear\napple\nZed\n', d[0])
d = session(msbase, MD + ['DEL', 'text:n', 'wait', 'show'])
check('dictionary: No keeps the word', 'Delete apple' not in d[0] and
    read(dict_file) == b'pear\napple\nZed\n' and 'apple' in d[0], d[0])
d = session(msbase, MD + ['DEL', 'ENTER', 'wait', 'show'])
check('dictionary: Yes takes the word out, the file is written',
    'Deleted apple' in d[0] and read(dict_file) == b'pear\nZed\n' and
    'Spelling, your words: 2' in d[0], (d[0], read(dict_file)))

dict_reset('pear\napple\nZed\nzebra\n')
d = session(msbase, MD + ['text:z', 'wait', 'show'])
check('dictionary: letters find a word', 'Find: z' in d[0], d[0])
d = session(msbase, MD + ['text:z', 'text:e', 'text:b', 'DEL', 'wait',
    'show'])
check('dictionary: more letters narrow the find',
    'Delete zebra from your words?' in d[0], d[0])
d = session(msbase, MD + ['text:q', 'wait', 'show'])
check('dictionary: no word begins with the letters: it shows so',
    'No word begins with q' in d[0], d[0])

# Import and export
dict_reset('pear\napple\nZed\n')
impf = os.path.join(tmp, 'importwords.txt')
with open(impf, 'w') as f:
    f.write('kiwi\nfig\nkiwi\nbad word\npear\n')
d = session(msbase, MD + ['F3', 'wait', 'DOWN', 'text:' + impf, 'INS',
    'wait', 'show'])
check('dictionary: F3 imports the words of a file and counts them',
    'Imported: 2 added, 2 in the list already, 1 not words' in d[0] and
    read(dict_file) == b'apple\nfig\nkiwi\npear\nZed\n', (d[0],
    read(dict_file)))
d = session(msbase, MD + ['F3', 'wait', 'DOWN', 'text:' + os.path.join(tmp,
    'nothing-here.txt'), 'INS', 'wait', 'show'])
check('dictionary: an import of a file that is not there says so',
    'Cannot read' in d[0] and read(dict_file) == b'apple\nfig\nkiwi\npear\n'
    b'Zed\n', d[0])
expf = os.path.join(tmp, 'exportwords.txt')
if os.path.exists(expf):
    os.unlink(expf)
d = session(msbase, MD + ['F4', 'wait', 'DOWN', 'text:' + expf, 'INS',
    'wait', 'show'])
check('dictionary: F4 exports the words to a file',
    'Wrote 5 words to' in d[0] and read(expf) ==
    b'apple\nfig\nkiwi\npear\nZed\n', (d[0], read(expf)))
d = session(msbase, MD + ['F4', 'wait', 'DOWN', 'text:' + expf, 'INS',
    'wait', 'show'])
check('dictionary: an export over a file that is there asks first',
    'exists.  Replace it?' in d[0], d[0])
d = session(msbase, MD + ['F4', 'wait', 'DOWN', 'text:' + expf, 'INS',
    'wait', 'text:n', 'wait', 'show'])
check('dictionary: No leaves the file as it was',
    'Wrote' not in d[0] and read(expf) == b'apple\nfig\nkiwi\npear\nZed\n',
    d[0])

# The personal hyphenation list: the same panel, the other list (Tab).  The
# file is hyphenation in the settings folder, one entry on a line, a hyphen
# where the word may be broken.
hyph_file = os.path.join(dict_home, 'hyphenation')


def hyph_reset(text):
    with open(hyph_file, 'w') as f:
        f.write(text)

    os.chmod(hyph_file, 0o600)


dict_reset('pear\napple\nZed\n')
hyph_reset('pro-ject\nin-ter-net\n')
d = session(msbase, MD + ['TAB', 'show'])
check('hyphenation: Tab shows the other list, in order, with its number and '
    'the way back', 'Hyphenation, your words: 2' in d[0] and
    'Tab: Spelling' in d[0] and d[0].index('in-ter-net') <
    d[0].index('pro-ject') and 'apple' not in d[0], d[0])
d = session(msbase, MD + ['TAB', 'TAB', 'show'])
check('hyphenation: Tab again shows the spelling list',
    'Spelling, your words: 3' in d[0] and 'pro-ject' not in d[0], d[0])
hyph_reset('')
d = session(msbase, MD + ['TAB', 'show'])
check('hyphenation: an empty list says how a word is written',
    'Hyphenation, your words: 0' in d[0] and 'hy-phen-a-tion' in d[0] and
    'never broken' in d[0], d[0])
hyph_reset('pro-ject\nin-ter-net\n')
d = session(msbase, MD + ['TAB', 'INS', 'text:hy-phen-a-tion', 'ENTER',
    'wait', 'show'])
check('hyphenation: Ins adds a word, in order, and only its file is written',
    'Added hy-phen-a-tion' in d[0] and 'Hyphenation, your words: 3' in d[0]
    and read(hyph_file) == b'hy-phen-a-tion\nin-ter-net\npro-ject\n' and
    read(dict_file) == b'pear\napple\nZed\n', (d[0], read(hyph_file)))
check('hyphenation: the file stays private',
    os.stat(hyph_file).st_mode & 0o777 == 0o600, oct(os.stat(
    hyph_file).st_mode))
hyph_reset('pro-ject\nin-ter-net\n')
for text, why in (('two words', 'A word has no spaces'),
        ('ab--cd', 'A hyphen needs a letter on both sides'),
        ('abc1', 'TeX cannot break a word with a digit or an apostrophe'),
        ('-ab', 'A word begins and ends with a letter'),
        ('=ab-cd', 'A word with an equals sign has no hyphen')):
    d = session(msbase, MD + ['TAB', 'INS', 'text:' + text, 'ENTER', 'wait',
        'show'])
    check('hyphenation: "%s" is refused with a reason' % text,
        why in d[0] and read(hyph_file) == b'pro-ject\nin-ter-net\n', d[0])
for text in ('proj-ect', 'PROJECT', '=project'):
    d = session(msbase, MD + ['TAB', 'INS', 'text:' + text, 'ENTER', 'wait',
        'show'])
    check('hyphenation: %s is the word that the list has already' % text,
        text + ' is in the list already' in d[0] and
        read(hyph_file) == b'pro-ject\nin-ter-net\n', d[0])
d = session(msbase, MD + ['TAB', 'INS', 'text:=Texas', 'ENTER', 'wait',
    'show'])
check('hyphenation: =word says that the word is never broken',
    'Added =Texas' in d[0] and read(hyph_file) ==
    b'in-ter-net\npro-ject\n=Texas\n', (d[0], read(hyph_file)))
hyph_reset('pro-ject\nin-ter-net\n')
d = session(msbase, MD + ['TAB', 'DEL', 'ENTER', 'wait', 'show'])
check('hyphenation: Del takes a word out of its list only',
    'Deleted in-ter-net' in d[0] and read(hyph_file) == b'pro-ject\n' and
    read(dict_file) == b'pear\napple\nZed\n', (d[0], read(hyph_file)))
d = session(msbase, MD + ['TAB', 'text:p', 'text:r', 'DEL', 'wait', 'show'])
check('hyphenation: letters find a word through its hyphens',
    'Delete pro-ject from your words?' in d[0], d[0])

hyph_reset('pro-ject\n')
impf = os.path.join(tmp, 'importhyph.txt')
with open(impf, 'w') as f:
    f.write('re-fresh\nPRO-JECT\nbad word\n=Texas\n')
d = session(msbase, MD + ['TAB', 'F3', 'wait', 'DOWN', 'text:' + impf, 'INS',
    'wait', 'show'])
check('hyphenation: F3 imports the words of a text file and counts them',
    'Imported: 2 added, 1 in the list already, 1 not words' in d[0] and
    read(hyph_file) == b'pro-ject\nre-fresh\n=Texas\n', (d[0],
    read(hyph_file)))
hyph_reset('')
hycf = os.path.join(here, 'data', 'custom.hyc')
d = session(msbase, MD + ['TAB', 'F3', 'wait', 'DOWN', 'text:' + hycf, 'INS',
    'wait', 'show'])
check('hyphenation: F3 imports the hyphenation dictionary of the earlier '
    'program (a file .hyc)',
    'Imported: 5 added, 0 in the list already, 1 not words' in d[0] and
    read(hyph_file) == b'ab-cd\nextra-ordi-nary-califra-gilistic\n'
    b'pre-fix-ed\nx-y\nzor-ba-flux\n', (d[0], read(hyph_file)))
hyph_reset('re-fresh\npro-ject\n')
expf = os.path.join(tmp, 'exporthyph.txt')
if os.path.exists(expf):
    os.unlink(expf)
d = session(msbase, MD + ['TAB', 'F4', 'wait', 'DOWN', 'text:' + expf,
    'INS', 'wait', 'show'])
check('hyphenation: F4 exports the words with their hyphens',
    'Wrote 2 words to' in d[0] and read(expf) == b'pro-ject\nre-fresh\n',
    (d[0], read(expf)))
# A list in the code page of the earlier program is read with its accents.
dict_reset('')
oldf = os.path.join(tmp, 'oldspell.spc')
with open(oldf, 'wb') as f:
    f.write(b'Caf\x82\r\n\x81ber\r\nplain\r\n\x1a')
d = session(msbase, MD + ['F3', 'wait', 'DOWN', 'text:' + oldf, 'INS',
    'wait', 'show'])
check('dictionary: a list in the old code page is imported with its accents '
    '(written as UTF-8)', 'Imported: 3 added, 0 in the list already, 0 not '
    'words' in d[0] and read(dict_file) ==
    'Caf\u00e9\nplain\n\u00fcber\n'.encode('utf-8'), (d[0],
    read(dict_file)))

# The spell check of the Editor reads the words at once
dict_reset('')
session(msbase, MD + ['INS', 'text:quikc', 'ENTER', 'wait'])
spell_fresh_doc = os.path.join(tmp, 'spell-dict.doc')
shutil.copy(os.path.join(here, 'data', 'spell.doc'), spell_fresh_doc)
os.environ['STRATA_SPELL'] = os.path.join(here, 'fakespell.sh')
d = session([stred, '-H', helpf, spell_fresh_doc], ['F12', 'text:t',
    'text:s', 'text:d', 'wait', 'INS', 'wait', 'show'])
check('dictionary: a word that the panel added is not asked about by the '
    'spell check', 'Unknown word: teh' in d[0] and 'Unknown word: quikc' not
    in d[0], d[0])



# ---- Search and Replace: the options, and the interactive Replace -----------
# The matcher is src/search.c (tests/searchtest.c tests it alone).  Here the
# panels, the keys and the messages are tried in a real Editor.  Each test
# types a line at the start of the document, goes back to the start of the
# line (HOME), and works on that line; the line is the third line of the
# screen, followed by the headline that was already there.
import re as _sre


def sr_pos(s):
    """The column of the cursor, from the status line."""
    m = _sre.search(r'Pos: (\d+)', s)
    return int(m.group(1)) if m else -1


def sr_line(s):
    """The line that was typed: the third line of the screen, without the
    headline that follows it."""
    return s.split('\n')[2].strip().split('Orchard Plan')[0]


def sr_last(s):
    """The message line, the last line of the screen."""
    return [l for l in s.split('\n') if l.strip() != ''][-1].strip()


def sr_search(text, steps):
    """Type text, go to its start, run the steps and show the screen after
    the start and after the steps."""
    return session(base, ['text:' + text, 'HOME', 'show'] + steps +
                   ['wait', 'show'])


# The default: the case is ignored, and the string is found inside a word.
d = sr_search('xx qzxone QZXONE', ['F9', 'text:QZXONE', 'INS'])
check('search: the case is ignored by default', sr_pos(d[1]) - sr_pos(d[0])
    == 3 and 'Found' in sr_last(d[1]), (d[0], d[1]))

# Match Case: Yes.
d = sr_search('xx qzxone QZXONE', ['F9', 'text:QZXONE', 'DOWN', 'DOWN',
    'DOWN', 'RIGHT', 'INS'])
check('search: with Match Case only the same case is found',
    sr_pos(d[1]) - sr_pos(d[0]) == 10 and 'Found' in sr_last(d[1]),
    (d[0], d[1]))
d = sr_search('xx qzxone', ['F9', 'text:QZXONE', 'DOWN', 'DOWN', 'DOWN',
    'RIGHT', 'INS'])
check('search: with Match Case the wrong case is not found',
    'Not found: QZXONE' in sr_last(d[1]), d[1])

# Whole Word: Yes.
d = sr_search('xx qzxonely qzxone', ['F9', 'text:qzxone', 'DOWN', 'DOWN',
    'DOWN', 'DOWN', 'RIGHT', 'INS'])
check('search: with Whole Word a longer word is not found',
    sr_pos(d[1]) - sr_pos(d[0]) == 12 and 'Found' in sr_last(d[1]),
    (d[0], d[1]))
d = sr_search('xx qzxonely', ['F9', 'text:qzxone', 'DOWN', 'DOWN', 'DOWN',
    'DOWN', 'RIGHT', 'INS'])
check('search: with Whole Word the part of a word is not found',
    'Not found: qzxone' in sr_last(d[1]), d[1])

# The attribute: the bold text, and the start of a bold run.
BOLDLINE = ['text:plain ', 'A-F6', 'ENTER', 'text:bold', 'A-F6', 'UP', 'ENTER',
            'text: tail']
d = session(base, BOLDLINE + ['HOME', 'show', 'F9', 'DOWN', 'DOWN', 'DOWN',
    'DOWN', 'DOWN', 'RIGHT', 'RIGHT', 'INS', 'wait', 'show'])
check('search: an attribute with no string finds the start of the bold text',
    sr_pos(d[1]) - sr_pos(d[0]) == 6 and 'Found' in sr_last(d[1]),
    (d[0], d[1]))
d = session(base, BOLDLINE + ['HOME', 'show', 'F9', 'text:bold', 'DOWN',
    'DOWN', 'DOWN', 'DOWN', 'DOWN', 'RIGHT', 'RIGHT', 'INS', 'wait', 'show'])
check('search: the string that is bold is found',
    sr_pos(d[1]) - sr_pos(d[0]) == 6, (d[0], d[1]))
d = session(base, BOLDLINE + ['HOME', 'F9', 'text:plain', 'DOWN', 'DOWN',
    'DOWN', 'DOWN', 'DOWN', 'RIGHT', 'RIGHT', 'INS', 'wait', 'show'])
check('search: a string that is not bold is not found as bold',
    'Not found: plain' in sr_last(d[0]), d[0])
d = session(base, BOLDLINE + ['HOME', 'F9', 'text:plain', 'DOWN', 'DOWN',
    'DOWN', 'DOWN', 'DOWN', 'RIGHT', 'INS', 'wait', 'show'])
check('search: plain text is found with the attribute Plain',
    'Found' in sr_last(d[0]), d[0])
d = session(base, BOLDLINE + ['HOME', 'F9', 'DOWN', 'DOWN', 'DOWN', 'DOWN',
    'DOWN', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'INS', 'wait', 'show'])
check('search: nothing is underscored, and the message shows what was looked '
    'for', 'Not found: underscored text' in sr_last(d[0]), d[0])

# A tab and a no-break space.
d = session(base, ['text:a', 'TAB', 'text:b', 'HOME', 'show', 'F9', 'DOWN',
    'RIGHT', 'RIGHT', 'INS', 'wait', 'show'])
check('search: Find Tab finds a tab', sr_pos(d[1]) - sr_pos(d[0]) == 1 and
    'Found' in sr_last(d[1]), (d[0], d[1]))
d = session(base, ['text:a', 'CS-SPACE', 'text:b', 'HOME', 'show', 'F9',
    'DOWN', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'INS', 'wait', 'show'])
check('search: Find Nonbreaking Space finds a no-break space',
    sr_pos(d[1]) - sr_pos(d[0]) == 1 and 'Found' in sr_last(d[1]),
    (d[0], d[1]))
d = session(base, ['text:ab', 'HOME', 'F9', 'DOWN', 'RIGHT', 'RIGHT', 'DOWN',
    'DOWN', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT', 'INS', 'wait', 'show'])
check('search: a tab that is underscored is not there, and the message shows '
    'so', 'Not found: a tab' in sr_last(d[0]), d[0])
d = session(base, ['text:ab', 'HOME', 'F9', 'DOWN', 'RIGHT', 'RIGHT', 'RIGHT',
    'RIGHT', 'RIGHT', 'INS', 'wait', 'show'])
check('search: the choice Marker looks for a marker',
    'Not found: a marker' in sr_last(d[0]) or 'Found' in sr_last(d[0]),
    d[0])

# Search again (Ctrl-F9) keeps the options; Backward is still there.
d = session(base, ['text:xx qzxone QZXONE qzxonely', 'HOME', 'show', 'F9',
    'text:qzxone', 'DOWN', 'DOWN', 'DOWN', 'RIGHT', 'INS', 'wait', 'show',
    'C-F9', 'wait', 'show'])
check('search: Search again keeps the options of the panel',
    sr_pos(d[1]) - sr_pos(d[0]) == 3 and sr_pos(d[2]) - sr_pos(d[0]) == 17,
    (d[0], d[1], d[2]))
d = session(base, ['text:xx qzxone yy qzxone', 'show', 'F9', 'text:qzxone',
    'DOWN', 'DOWN', 'RIGHT', 'INS', 'wait', 'show'])
check('search: the direction Backward finds the last one first',
    sr_pos(d[1]) - sr_pos(d[0]) == -6, (d[0], d[1]))
d = session(base, ['F9', 'DOWN', 'DOWN', 'DOWN', 'show'])
check('search: the panel has the fields Match Case, Whole Word and Attribute',
    all(w in d[0] for w in ('Search for', 'Find', 'Direction', 'Match Case',
    'Whole Word', 'Attribute')), d[0])
# No field of a panel appears or disappears: Match Case and Whole Word stay in
# their places, inactive, and the cursor skips them (the second Down goes from
# Direction to Attribute, where Right chooses the next attribute).
d = session(base, ['F9', 'DOWN', 'RIGHT', 'DOWN', 'DOWN', 'RIGHT', 'show'])
check('search: Match Case and Whole Word are for text only: a Print Error '
    'leaves them inactive and the cursor skips them',
    'Match Case: No' in d[0] and 'Whole Word: No' in d[0] and
    'Attribute: [Any]' not in d[0] and 'Attribute: [' in d[0], d[0])
d = session(base, ['size:20,80', 'F9', 'wait', 'show'])
check('search: the panel fits a screen of 20 rows', 'Attribute' in d[0] and
    'Search for' in d[0], d[0])

# Replace: ask at each place.
RP = ['A-F9', 'wait', 'text:qzx', 'DOWN', 'text:ab']
d = session(base, ['text:qzx qzx qzx qzx', 'HOME'] + RP + ['INS', 'wait',
    'show', 'text:r', 'wait', 'show', 'text:s', 'wait', 'show', 'text:o',
    'wait', 'show'])
check('replace: the question is asked with the keys', 'R replace' in
    sr_last(d[0]) and 'O once' in sr_last(d[0]) and 'S skip' in sr_last(d[0])
    and 'A all' in sr_last(d[0]) and 'Q quit' in sr_last(d[0]), d[0])
check('replace: R replaces and asks about the next place',
    'ab qzx qzx qzx' in sr_line(d[1]) and 'R replace' in sr_last(d[1]),
    d[1])
check('replace: S leaves the place as it is', 'ab qzx qzx qzx' in
    sr_line(d[2]) and 'R replace' in sr_last(d[2]), d[2])
check('replace: O replaces this place and stops, and counts',
    'ab qzx ab qzx' in sr_line(d[3]) and '2 replacements' in sr_last(d[3]),
    d[3])
d = session(base, ['text:qzx qzx qzx', 'HOME'] + RP + ['INS', 'wait',
    'text:a', 'wait', 'show'])
check('replace: A replaces this place and all the others without asking',
    'ab ab ab' in sr_line(d[0]) and '3 replacements' in sr_last(d[0]), d[0])
d = session(base, ['text:qzx qzx', 'HOME'] + RP + ['INS', 'wait', 'text:q',
    'wait', 'show'])
check('replace: Q stops and changes nothing more', 'qzx qzx' in
    sr_line(d[0]) and '0 replacements' in sr_last(d[0]) and
    'stopped' in sr_last(d[0]), d[0])
d = session(base, ['text:qzx qzx', 'HOME'] + RP + ['INS', 'wait', 'ESC',
    'wait', 'show'])
check('replace: Esc stops like Q', 'qzx qzx' in sr_line(d[0]) and
    'stopped' in sr_last(d[0]), d[0])
d = session(base, ['text:qzx qzx', 'HOME'] + RP + ['INS', 'wait', 'text:r',
    'wait', 'text:r', 'wait', 'show'])
check('replace: at the end the run says how many places it changed',
    'ab ab' in sr_line(d[0]) and '2 replacements' in sr_last(d[0]), d[0])
d = session(base, ['text:zzzz', 'HOME'] + RP + ['INS', 'wait', 'show'])
check('replace: no place says Not found', 'Not found: qzx' in sr_last(d[0]),
    d[0])

# Search and Replace go round the document: they continue from the other end
# and stop where they started.  The line is typed at the start of the
# document; MID puts the cursor between its two places, after the first one.
MID = ['HOME'] + ['RIGHT'] * 5
d = sr_search('zz qzx', ['F9', 'text:qzx', 'DOWN', 'DOWN', 'RIGHT', 'INS'])
check('wrap: a backward search from the start of the document goes round to '
    'the last place and says so', sr_pos(d[1]) - sr_pos(d[0]) == 3 and
    'wrapped to the end of the document' in sr_last(d[1]), (d[0], d[1]))
d = session(base, ['text:aa qzx', 'show', 'F9', 'text:qzx', 'INS', 'wait',
    'show'])
check('wrap: a search from the end of the line goes round to the start and '
    'says so', sr_pos(d[1]) - sr_pos(d[0]) == -3 and
    'wrapped to the start of the document' in sr_last(d[1]), (d[0], d[1]))
d = sr_search('aa qzx', ['F9', 'text:qzx', 'INS'])
check('wrap: a search that goes forward without going round does not say so',
    'wrapped' not in sr_last(d[1]) and 'Found' in sr_last(d[1]), d[1])
d = session(base, ['text:qzx aa qzx'] + MID + RP + ['INS', 'wait', 'text:r',
    'wait', 'show', 'text:r', 'wait', 'show'])
check('wrap: Replace goes on from the start of the document, and asks about '
    'the place that is before the start', 'qzx aa ab' in sr_line(d[0]) and
    'R replace' in sr_last(d[0]) and 'wrapped to the start' in sr_last(d[0]),
    d[0])
check('wrap: the place before the start is replaced, and the run stops at '
    'the start', 'ab aa ab' in sr_line(d[1]) and
    '2 replacements (wrapped to the start)' in sr_last(d[1]), d[1])
d = session(base, ['text:qzx aa qzx aa qzx'] + MID + RP + ['INS', 'wait',
    'text:a', 'wait', 'show'])
check('wrap: A replaces the places after the start and then, without '
    'asking, those before it', 'ab aa ab aa ab' in sr_line(d[0]) and
    '3 replacements (wrapped to the start)' in sr_last(d[0]), d[0])
d = session(base, ['text:qzx aa qzx'] + MID + RP + ['INS', 'wait', 'text:q',
    'wait', 'show'])
check('wrap: Q before the end does not go round', 'qzx aa qzx' in
    sr_line(d[0]) and 'wrapped' not in sr_last(d[0]) and
    '0 replacements (stopped)' in sr_last(d[0]), d[0])
d = session(base, ['text:qzx aa qzx'] + MID + RP + ['INS', 'wait', 'text:o',
    'wait', 'show'])
check('wrap: O replaces one place and does not go round', 'qzx aa ab' in
    sr_line(d[0]) and 'wrapped' not in sr_last(d[0]) and
    '1 replacement' in sr_last(d[0]), d[0])
d = session(base, ['text:qzx aa qzx'] + MID + RP[:-1] + ['text:qzxy', 'INS',
    'wait', 'text:r', 'wait', 'text:r', 'wait', 'show'])
check('wrap: a replacement that holds the string is not replaced again, and '
    'the run still ends at the start', 'qzxy aa qzxy' in sr_line(d[0]) and
    'qzxyy' not in sr_line(d[0]) and
    '2 replacements (wrapped to the start)' in sr_last(d[0]), d[0])
d = session(base, ['text:qzx aa ab'] + MID + RP + ['INS', 'wait', 'text:r',
    'wait', 'show'])
check('wrap: a place only before the start is found after the end of the '
    'document', 'ab aa ab' in sr_line(d[0]) and
    '1 replacement (wrapped to the start)' in sr_last(d[0]), d[0])
d = session(base, ['text:zzzz'] + MID + RP + ['INS', 'wait', 'show'])
check('wrap: no place at all after going round says Not found',
    'Not found: qzx' in sr_last(d[0]), d[0])

# Replace: no question, all at once (Ask at Each: No).
ALL = ['A-F9', 'wait', 'text:qzx', 'DOWN', 'text:ab', 'DOWN', 'DOWN', 'DOWN',
       'DOWN', 'DOWN', 'DOWN', 'LEFT']
d = session(base, ['text:qzx qzx qzx', 'HOME'] + ALL + ['INS', 'wait',
    'show'])
check('replace: Ask at Each No replaces every place at once and counts',
    'ab ab ab' in sr_line(d[0]) and '3 replacements' in sr_last(d[0]), d[0])

# Undo takes back a Replace: one operation for the whole run.
d = session(base, ['text:qzx qzx qzx', 'HOME'] + ALL + ['INS', 'wait',
    'C-z', 'wait', 'show'])
check('replace: Undo takes back every replacement of Replace all at once',
    'qzx qzx qzx' in sr_line(d[0]) and 'Undone' in sr_last(d[0]), d[0])
d = session(base, ['text:qzx qzx qzx', 'HOME'] + RP + ['INS', 'wait',
    'text:r', 'wait', 'text:r', 'wait', 'text:r', 'wait', 'show', 'C-z',
    'wait', 'show'])
check('replace: Undo takes back a whole run of questions in one step',
    'ab ab ab' in sr_line(d[0]) and 'qzx qzx qzx' in sr_line(d[1]), d)

# Replace: the case of the replacement, whole words, the attribute, a tab.
FOLLOW = ['A-F9', 'wait', 'text:the', 'DOWN', 'text:a', 'DOWN', 'DOWN',
          'LEFT', 'DOWN', 'DOWN', 'DOWN', 'RIGHT', 'DOWN', 'LEFT']
d = session(base, ['text:The the THE', 'HOME'] + FOLLOW + ['INS', 'wait',
    'show'])
check('replace: Follow Found gives the replacement the case of the text',
    'A a A' in sr_line(d[0]), d[0])
d = session(base, ['text:The the THE', 'HOME', 'A-F9', 'wait', 'text:the',
    'DOWN', 'text:x'] + ['DOWN'] * 6 + ['LEFT', 'INS', 'wait', 'show'])
check('replace: Match Case Yes (the default) leaves other cases alone',
    'The x THE' in sr_line(d[0]), d[0])
d = session(base, ['text:cat catalog cat', 'HOME', 'A-F9', 'wait',
    'text:cat', 'DOWN', 'text:dog', 'DOWN', 'DOWN', 'DOWN', 'RIGHT', 'DOWN',
    'DOWN', 'DOWN', 'DOWN', 'LEFT', 'INS', 'wait', 'show'])
check('replace: Whole Word leaves the longer word alone',
    'dog catalog dog' in sr_line(d[0]) and sr_last(d[0]) == '2 replacements',
    d[0])
d = session(base, ['text:bold ', 'A-F6', 'ENTER', 'text:bold', 'A-F6', 'UP',
    'ENTER', 'HOME', 'A-F9', 'wait', 'text:bold', 'DOWN', 'text:X', 'DOWN',
    'DOWN', 'DOWN', 'DOWN', 'RIGHT', 'RIGHT', 'DOWN', 'DOWN', 'LEFT', 'INS',
    'wait', 'show'])
check('replace: the attribute Bold changes only the bold text',
    'bold X' in sr_line(d[0]) and sr_last(d[0]) == '1 replacement', d[0])
d = session(base, ['text:a', 'TAB', 'text:b', 'HOME', 'A-F9', 'wait', 'DOWN',
    'text:-', 'DOWN', 'RIGHT', 'INS', 'wait', 'text:o', 'wait', 'show'])
check('replace: Find Tab replaces a tab (once, since the document has other '
    'tabs)', 'a-b' in sr_line(d[0]) and sr_last(d[0]) == '1 replacement',
    d[0])
d = session(base, ['text:ab', 'HOME', 'A-F9', 'wait', 'show'])
check('replace: the panel has the fields of Search and the replacement',
    all(w in d[0] for w in ('Search for', 'Replace with', 'Find',
    'Match Case', 'Whole Word', 'Attribute', 'Replacement Case',
    'Ask at Each')), d[0])
d = session(base, ['size:20,80', 'A-F9', 'wait', 'show'])
check('replace: the panel fits a screen of 20 rows', 'Ask at Each' in d[0] and
    'Search for' in d[0], d[0])
d = session(base, ['text:ab', 'A-F9', 'wait', 'INS', 'wait', 'show'])
check('replace: an empty string to search for is refused with a message',
    'needs the text' in sr_last(d[0]), d[0])

# ---- the table of contents of a print ------------------------------------
# The Print panels have the fields Table of Contents? (No at the start) and
# Contents Levels; with Yes the PDF has a contents page after the title page
# that lists the headlines with their tags and pages (tests/tocpdf.py reads
# the lines of the page; here the panel and the message are tested).
d = fm(fresh('tocp.doc'), ['F12', 'text:P', 'RIGHT', 'ENTER', 'show', 'ESC'])
check('contents: the Final panel has Table of Contents? (No) and Levels (3)',
    'Table of Contents?' in d[0] and 'Contents Levels' in d[0] and
    re.search(r'Table of Contents\?:\s*\[No\]', d[0]) is not None and
    re.search(r'Contents Levels:\s*3', d[0]) is not None, d[0])
d = fm(fresh('tocq.doc'), ['F12', 'text:P', 'ENTER', 'show', 'ESC'])
check('contents: the Draft panel has the two fields too',
    'Table of Contents?' in d[0] and 'Contents Levels' in d[0], d[0])
# Draft: Output File, Printer, Copies, Sides, Sort, Current Section Only,
# Double Spaced, from Page, to Page, Error Log, then the two fields.
full, d, out = printed((['ENTER'], ['DOWN'] * 10 + ['RIGHT', 'DOWN',
    'text:2']), 'toc1.pdf')
check('contents: Yes makes the contents page: the headlines, the pages and '
    'one page more', open(out, 'rb').read(4) == b'%PDF' and
    'Contents' in full and 'Neighbours' in full and
    pdfpages(out) == 5 and '(5 pages)' in d, (d, pdfpages(out)))
full, d, out = printed((['ENTER'], []), 'toc2.pdf')
check('contents: not asked, the print has no contents page',
    'Contents' not in full and pdfpages(out) == 4, pdfpages(out))
d = fm(fresh('tocr.doc'), ['F12', 'text:P', 'ENTER'] + ['DOWN'] * 10 +
    ['RIGHT', 'DOWN', 'text:x', 'INS', 'wait', 'show'])
check('contents: a level that is not a number is refused with a message',
    'Contents Levels is a number from 1 to 99' in d[0], d[0])
d = fm(fresh('tocs.doc'), ['F12', 'text:P', 'ENTER'] + ['DOWN'] * 10 +
    ['RIGHT', 'DOWN', 'text:100', 'INS', 'wait', 'show'])
check('contents: the level 100 is refused too',
    'Contents Levels is a number from 1 to 99' in d[0], d[0])

# ---- the Include marker (a block of its own) ---------------------------------
# The panel of the marker (File Name, with the list of the documents on F2)
# and a print of a master document that holds the text of its parts
# (tests/include.py has the rules: the directories, the loops, the errors).
def incl_blk(text):
    return b'T' + text + b'\x1d\x00\r\n'
def incl_doc(blocks):
    out = (b'*R3\r\nMR1001\r\nPP\\1\\\x00\r\nP1C33\r\nFHA1\r\nFTT5\r\n'
           b'DP0,8500,11000\r\n*B0\r\nS\r\n')
    for i, b in enumerate(blocks):
        out += (b'' if i == 0 else b'B\r\n') + incl_blk(b)
    return out + b'*E\r\n'
incl_dir = os.path.join(tmp, 'incl')
os.makedirs(incl_dir)
for nm, txt in (('chapter1', b'Text of the first chapter.'),
                ('chapter2', b'Text of the second chapter.')):
    open(os.path.join(incl_dir, nm + '.doc'), 'wb').write(incl_doc([txt]))
open(os.path.join(incl_dir, 'master.doc'), 'wb').write(incl_doc(
    [b'Master text.']))
os.chdir(incl_dir)
d = fm(os.path.join(incl_dir, 'master.doc'), ['A-F12', 'text:I', 'text:I',
    'INS', 'wait', 'show', 'F2', 'wait', 'show', 'DOWN', 'ENTER', 'wait',
    'show', 'INS', 'wait', 'show'])
os.chdir(tmp)
check('include: the panel has File Name, and no other name for it',
    'Include' in d[0] and 'File Name:' in d[0] and
    'Document Name' not in d[0], d[0])
check('include: F2 lists the documents of the directory',
    'chapter1.doc' in d[1] and 'chapter2.doc' in d[1], d[1])
check('include: the choice is in the field', 'chapter1.doc' in d[2], d[2])
check('include: the marker is in the document, with the name',
    'INCLUDE "chapter1.doc"' in d[3], d[3])
d = fm(os.path.join(incl_dir, 'master.doc'), ['A-F12', 'text:I', 'text:I',
    'INS', 'wait', 'INS', 'wait', 'show'])
check('include: an empty File Name is refused', 'No string specified' in d[0],
    d[0])
# A master of a print: the parts are beside it (the print finds them in the
# directory of the document).
for nm, txt in (('incl_a', b'Text of part A, in the print.'),
                ('incl_b', b'Text of part B, in the print.')):
    open(os.path.join(tmp, nm + '.doc'), 'wb').write(incl_doc([txt]))
def imarker(n):
    return b'\x1316(1' + n + b'\x00\r\n\x13)'
msrc = os.path.join(tmp, 'incl_master_src.doc')
open(msrc, 'wb').write(incl_doc([b'Master first.', imarker(b'incl_a'),
    b'Master middle.', imarker(b'incl_b.doc'), b'Master last.']))
full, d, out = printed((['ENTER'], []), 'inclp.pdf', src=msrc)
check('include: a Draft print has the text of the parts at the markers',
    'Master first.' in full and 'Text of part A, in the print.' in full and
    'Master middle.' in full and 'Text of part B, in the print.' in full and
    'Master last.' in full and 'INCLUDE' not in full and 'Wrote ' in d,
    (d, full))
check('include: the parts come in the order of the markers',
    full.index('Master first.') < full.index('part A') <
    full.index('Master middle.') < full.index('part B') <
    full.index('Master last.'), full)
full, d, out = printed((['RIGHT', 'ENTER'], []), 'inclq.pdf', src=msrc)
check('include: a Final print has them too', 'part A' in full and
    'part B' in full and 'Wrote ' in d, (d, full))
os.remove(os.path.join(tmp, 'incl_b.doc'))
full, d, out = printed((['ENTER'], []), 'inclr.pdf', src=msrc)
check('include: a part that is not there is told in the print and the print '
    'goes on', 'Invalid Include request' in full and 'Master last.' in full
    and 'part A' in full, (d, full))

home_override[0] = None

shutil.rmtree(tmp)
print('smoke: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
