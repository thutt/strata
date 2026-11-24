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

"""No generated PDF file is committed.

    python3 -I -B tests/pdfguard.py

The PDF files of the manuals and of the samples are products of the build:
they are made into the build directory and are never committed.  The one PDF
file in the repository is tests/data/pics/bad.pdf, a file of a few bytes that
is not a PDF file on purpose, which a picture test reads to see the error.
The test checks that .gitignore has the rule *.pdf and the exception for that
file, and that no other file with the suffix .pdf is tracked.  The checks are
made again on scratch repositories that break one rule each, and every one has
to fail a check.
"""

import os
import shutil
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
ALLOWED = ['tests/data/pics/bad.pdf']
RULES = ['*.pdf', '!tests/data/pics/bad.pdf']
fails = 0


def check(label, ok, detail=''):
    global fails
    if ok:
        print('ok   ' + label)
    else:
        fails += 1
        print('FAIL ' + label)
        if detail != '':
            print('     ' + str(detail))


def git(repo, *args):
    env = dict(os.environ, GIT_CONFIG_GLOBAL='/dev/null',
               GIT_CONFIG_SYSTEM='/dev/null')
    r = subprocess.run(['git', '-C', repo] + list(args), env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    return r.returncode, r.stdout.decode('utf-8', 'replace')


def problems(repo):
    """What is wrong in the repository: a missing rule or a tracked PDF."""
    out = []
    try:
        with open(os.path.join(repo, '.gitignore')) as f:
            lines = [ln.strip() for ln in f.read().split('\n')]
    except OSError:
        lines = []

    for rule in RULES:
        if rule not in lines:
            out.append('.gitignore has no line %s' % rule)

    status, listing = git(repo, 'ls-files', '--', '*.pdf')
    if status != 0:
        out.append('git ls-files failed')

    for name in listing.split('\n'):
        if name != '' and name not in ALLOWED:
            out.append('a PDF file is tracked: %s' % name)

    return out


def scratch(base, name, gitignore, extra_pdf):
    """A scratch repository with the allowed file and the chosen faults."""
    repo = os.path.join(base, name)
    os.makedirs(os.path.join(repo, 'tests', 'data', 'pics'))
    git(repo, 'init', '-q')
    with open(os.path.join(repo, 'tests', 'data', 'pics', 'bad.pdf'),
              'w') as f:
        f.write('not a pdf\n')
    with open(os.path.join(repo, '.gitignore'), 'w') as f:
        f.write(gitignore)
    if extra_pdf:
        with open(os.path.join(repo, 'manual.pdf'), 'w') as f:
            f.write('%PDF-1.4\n')
    git(repo, 'add', '-A', '-f', '--', '.gitignore',
        'tests/data/pics/bad.pdf')
    if extra_pdf:
        git(repo, 'add', '-f', 'manual.pdf')

    return repo


status = git(root, 'rev-parse', '--git-dir')[0]
if status == 0:
    found = problems(root)
    check('.gitignore has the rule *.pdf and the exception for the test '
          'fixture, and no other PDF file is tracked', found == [], found)
else:
    print('skip the repository itself: not a git checkout')

base = tempfile.mkdtemp(prefix='pdfguard.')
try:
    good = '\n'.join(RULES) + '\n'
    check('a scratch repository with the rules and only the fixture passes',
          problems(scratch(base, 'a', good, False)) == [])
    found = problems(scratch(base, 'b', good, True))
    check('mutation: a second tracked PDF file fails a check',
          any('is tracked' in p for p in found), found)
    found = problems(scratch(base, 'c', '*.pyc\n', False))
    check('mutation: a .gitignore without the rules fails a check',
          any('has no line' in p for p in found), found)
    found = problems(scratch(base, 'd', '*.pdf\n', False))
    check('mutation: a .gitignore without the exception fails a check',
          any('!tests/data/pics/bad.pdf' in p for p in found), found)
finally:
    shutil.rmtree(base, ignore_errors=True)

print('pdfguard: %s' % ('all passed' if fails == 0 else
                        '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
