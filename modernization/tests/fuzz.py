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

"""Random key fuzzing of stred in a pty: the process must never crash or exit.
usage: fuzz.py [seeds] [keys-per-seed]"""
import sys as _s; _s.dont_write_bytecode = True
import os, sys, random, shutil, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tui import session
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
stred = os.environ.get('STRED', os.path.join(os.environ.get('STRATA_BUILD_DIR',
    ''), 'term', 'stred'))
sample = os.environ.get('DOCSRC', os.path.join(root, 'tests', 'samples'))
helpf = os.environ.get('STRATA_HELP', os.path.join(root, '..',
    'lotus-manuscript', 'utility_3', 'strata.hlp'))
seeds = int(sys.argv[1]) if len(sys.argv) > 1 else 12
nkeys = int(sys.argv[2]) if len(sys.argv) > 2 else 120

named = ['ENTER', 'TAB', 'ESC', 'BKSP', 'DEL', 'UP', 'DOWN', 'LEFT', 'RIGHT',
    'HOME', 'END', 'PGUP', 'PGDN', 'INS', 'S-INS',
         'S-UP', 'S-DOWN', 'S-LEFT', 'S-RIGHT', 'C-LEFT', 'C-RIGHT', 'S-HOME',
         'S-END', 'C-HOME', 'C-END', 'S-PGUP', 'S-PGDN', 'BTAB']
named += ['F%d' % i for i in range(1, 11)] + ['A-F%d' % i for i in range(2, 11)]
# no C-q: it quits
named += ['C-' + c for c in 'abcdefghklmnoprstuvwxyz']
named += ['A-' + c for c in '0123456789-+*<>,.iobx']
chars = ['text:' + t for t in ('hello', ' ', 'x', 'Parking', '5', 'A1',
    'The end.', '\u00e9')]
docs = ['plan-title-table.doc', 'brief-long.doc', 'memo-date-table.doc',
    'report-plain.doc', 'letter-tabs.doc', 'manual-numbered.doc',
    'reference-long.doc', 'article-footer.doc']

tmp = tempfile.mkdtemp(); os.chdir(tmp); os.environ['SHELL'] = '/bin/true'
os.environ['STRATA_HOME'] = os.path.join(tmp, 'strata-home')
bad = 0
for seed in range(seeds):
    rnd = random.Random(seed)
    name = docs[seed % len(docs)]; shutil.copy(os.path.join(sample, name),
        os.path.join(tmp, 'f.doc'))
    steps = [rnd.choice(named) if rnd.random() < .75 else rnd.choice(chars)
        for _ in range(nkeys)]
    # leave any open popup before the final screen check
    steps += ['ESC', 'ESC', 'ESC', 'show']
    dumps, st = session([stred, '-H', helpf, 'f.doc'], steps, rows=24
        + seed % 5, cols=70 + seed * 3 % 40, delay=0.025, status=True)
    alive = st is None
    ok = alive and ('EDIT' in dumps[0] or 'Document Manager' in dumps[0])
    print('%s seed %2d %-14s %s' % ('ok  ' if ok else 'FAIL', seed, name, ''
        if ok else ('exited status=%r' % st if not alive else 'no editor '
        'screen')))
    if not ok:
        bad += 1
        with open(os.path.join(tempfile.gettempdir(),
            'stred-fuzz-fail-%d.txt' % seed),
            'w') as f: f.write('\n'.join(steps) + '\n\n' + dumps[0])
shutil.rmtree(tmp)
print('fuzz: %s' % ('all survived' if not bad else '%d FAILURES' % bad))
sys.exit(1 if bad else 0)
