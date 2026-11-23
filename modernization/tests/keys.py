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

"""The keys of Undo and Redo, the Ctrl-F list of the commands that have a
function key, and the Konsole form of a modified F1 to F4, through the real
Editor in a pty.

  - Undo is Ctrl-/ (the byte 1F in a terminal), Ctrl-F10 and Ctrl-Z; Redo is
    Alt-/ (ESC /) and Ctrl-F11; the menu hints show Ctrl-/ and Alt-/;
  - the Ctrl-F list has every command of the table of function keys, with its
    key (the table is printed by stred --fkey-table, the same table that runs
    the keys), a command that has no key is not in it, Enter runs the command;
  - ESC O m P to S (m 2 to 8) are the keys of ESC [ 1 ; m P to S.

    STRED=... python3 -I -B tests/keys.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
stred = os.environ.get('STRED', os.path.join(os.environ.get('STRATA_BUILD_DIR',
    ''), 'term', 'stred'))
sample = os.environ.get('DOCSRC', os.path.join(root, 'tests', 'samples'))
helpf = os.environ.get('STRATA_HELP', os.path.join(root, '..',
    'lotus-manuscript', 'utility_3', 'strata.hlp'))
fails = 0
tmp = tempfile.mkdtemp()


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def session(*args, **kw):
    os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home')
    return tui.session(*args, **kw)


def fresh(name):
    path = os.path.join(tmp, name)
    shutil.copy(os.path.join(sample, 'plan-title-table.doc'), path)
    return path


def fm(keys, name='d.doc'):
    return session([stred, '-H', helpf, fresh(name)], keys)


# ---- Undo and Redo -----------------------------------------------------------
for key, name in (('C-SLASH', 'Ctrl-/'), ('C-F10', 'Ctrl-F10'),
        ('C-z', 'Ctrl-Z')):
    d = fm(['text:abc', 'show', key, 'wait', 'show'])
    check('%s is Undo: the typing is taken back and the message shows so' %
        name, 'abcOrchard' in d[0] and 'abc' not in d[1] and
        'Undone: Typing' in d[1], d[1])
for key, name in (('A-SLASH', 'Alt-/'), ('C-F11', 'Ctrl-F11')):
    d = fm(['text:abc', 'C-SLASH', 'wait', key, 'wait', 'show'])
    check('%s is Redo: the typing is done again' % name,
        'abcOrchard' in d[0] and 'Redone: Typing' in d[0], d[0])
d = fm(['text:abc', 'C-SLASH', 'C-SLASH', 'show'])
check('Ctrl-/ with nothing left to undo says so',
    'Nothing to undo' in d[0], d[0])
d = fm(['A-SLASH', 'show'])
check('Alt-/ with nothing to redo says so', 'Nothing to redo' in d[0], d[0])

# The menu hints.
d = fm(['F12', 'text:T', 'END', 'show', 'LEFT', 'show'])
check('the hint of Redo names Alt-/', 'Do again what was undone [Alt-/]' in
    d[0], d[0])
check('the hint of Undo names Ctrl-/', 'Undo the last change [Ctrl-/]' in
    d[1], d[1])

# ---- The Ctrl-F list --------------------------------------------------------
table = [l.split('\t') for l in subprocess.run([stred, '--fkey-table'],
    capture_output=True, text=True).stdout.split('\n') if l]
check('the table has 31 commands with a function key', len(table) == 31 and
    all(len(t) == 2 for t in table), table)
EXPECT = ('Help F1;Split F2;Copy F3;Cut F4;Paste F5;Format F6;Move F7;Table F8;'
    'Search F9;Menu F12;Learn Ctrl-F1;Math Ctrl-F2;Lib Copy Ctrl-F3;Lib Cut '
    'Ctrl-F4;Lib Paste Ctrl-F5;Case Ctrl-F6;Prev Error Ctrl-F7;Next Error '
    'Ctrl-F8;Search Again Ctrl-F9;Undo Ctrl-F10;Redo Ctrl-F11;Marker '
    'Ctrl-F12;Append Alt-F2;Join Alt-F3;Delete Alt-F4;Overtype Alt-F5;Set '
    'Attribute Alt-F6;Go To Alt-F7;Border Alt-F8;Replace Alt-F9;Marker '
    'Alt-F12').split(';')
check('the table is the table of the keys of the manual (every command, '
    'every key)', ['%s %s' % (n, k) for n, k in table] == EXPECT, table)
labels = ['%s (%s)' % (n, k) for n, k in table]
other = ['Promote section (Alt-[)', 'Demote section (Alt-])',
    'Soft hyphen (Alt-_)']
shots = fm(['C-f', 'show'] + ['DOWN'] * 25 + ['show'] +
    ['DOWN'] * 8 + ['show'])
seen = set()
for s in shots:
    for l in s.split('\n'):
        seen.add(' '.join(l.replace('║', ' ').split()))
missing = [l for l in labels + other if not any(l in x for x in seen)]
check('the list shows every command of the table with its key, and the three '
    'commands of other keys', not missing, missing)
check('the commands that have no function key are not in it (Compose has '
    'no label and no command)',
    not any('Compose' in x for x in seen), '')
check('the list is headed Commands', 'Commands' in shots[0], shots[0])
check('the list fits the screen: the first page ends before the last entry',
    not any('Soft hyphen' in l for l in shots[0].split('\n')) and
    len(shots[0].split('\n')) <= 25, shots[0])

# Enter runs the command.
idx = {l: i for i, l in enumerate(labels)}
for _k in ('Undo (Ctrl-F10)', 'Overtype (Alt-F5)', 'Search Again (Ctrl-F9)',
        'Border (Alt-F8)'):
    idx.setdefault(_k, 0)
d = fm(['text:abc', 'C-f'] + ['DOWN'] * idx['Undo (Ctrl-F10)'] + ['ENTER',
    'wait', 'show'])
check('Enter on Undo undoes', 'Undone: Typing' in d[0] and 'abcOrchard' not in
    d[0], d[0])
d = fm(['C-f'] + ['DOWN'] * idx['Overtype (Alt-F5)'] + ['ENTER', 'wait',
    'show'])
check('Enter on Overtype turns overtype on', 'Overtype on' in d[0], d[0])
d = fm(['C-f'] + ['DOWN'] * idx['Search Again (Ctrl-F9)'] + ['ENTER', 'wait',
    'show'])
check('Enter on Search Again runs it (it has nothing to search for)',
    'Search' in d[0] and 'Commands' not in d[0], d[0])
d = fm(['C-f'] + ['DOWN'] * idx['Border (Alt-F8)'] + ['ENTER', 'wait',
    'show'])
check('Enter on Border (a command that has a key and no other route) runs it',
    'highlight column' in d[0], d[0])
d = fm(['A-F8', 'wait', 'show'])
check('Alt-F8 is Border', 'highlight column' in d[0], d[0])
d = fm(['C-f', 'ESC', 'show'])
check('Esc leaves the list', 'Commands' not in d[0], d[0])
d = fm(['C-f'] + ['DOWN'] * 31 + ['ENTER', 'wait', 'show'])
check('the last three entries run Promote, Demote and Soft hyphen',
    'Commands' not in d[0], d[0])

# ---- The Konsole form of Shift, Ctrl and Alt with F1 to F4 -------------------
def stable(screen):
    return '\n'.join(l for l in screen.split('\n') if 'p.doc' not in l and
        'd.doc' not in l)


for n, name in ((2, 'Append'), (3, 'Join'), (4, 'Delete')):
    d1 = fm(['K3-F%d' % n, 'wait', 'show', 'ESC'])
    d2 = fm(['A-F%d' % n, 'wait', 'show', 'ESC'])
    check('Alt-F%d (%s) from ESC O 3 %s is the key of ESC [ 1 ; 3 %s' %
        (n, name, 'PQRS'[n - 1], 'PQRS'[n - 1]),
        stable(d1[0]) == stable(d2[0]) and 'Orchard' in d1[0] and
        d1[0] != fm(['show'])[0], (d1[0], d2[0]))
d1 = fm(['K5-F2', 'wait', 'show', 'ESC'])
check('Ctrl-F2 (Math) from ESC O 5 Q opens the Math box',
    'Math' in d1[0] and d1[0] != fm(['show'])[0], d1[0])
d1 = fm(['text:abc', 'K5-F1', 'wait', 'show', 'K5-F1', 'wait', 'show'])
d2 = fm(['text:abc', 'C-F1', 'wait', 'show', 'C-F1', 'wait', 'show'])
check('Ctrl-F1 (Learn) from ESC O 5 P is the same key as ESC [ 1 ; 5 P',
    stable(d1[0]) == stable(d2[0]) and stable(d1[1]) == stable(d2[1]),
    (d1, d2))
d = fm(['K2-F3', 'wait', 'show'])
check('Shift-F3 from ESC O 2 R has no command, as the other form',
    'Copy' not in d[0].replace('F3: Copy', ''), d[0])
d = fm(['bytes:1b4f3950', 'text:x', 'show'])
check('a form with m = 9 is no key and does not eat the next one',
    'xOrchard' in d[0], d[0])
d = fm(['text:ab', 'K3-F2', 'wait', 'ESC', 'text:c', 'show'])
check('after the key the next one is read', 'abcOrchard' in d[0], d[0])

print('keys: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
