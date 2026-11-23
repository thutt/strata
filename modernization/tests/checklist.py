#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The checklist of the Retrieve panel reads every entry of a directory and
sorts all of them: in a directory of 600 files the list starts with the first
names by name and ends with the last.  Run on its own:

    python3 -I -B tests/checklist.py
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
tmp = tempfile.mkdtemp(prefix='checklist')
atexit.register(shutil.rmtree, tmp, True)
os.environ['STRATA_HOME'] = os.path.join(tmp, 'home')
os.makedirs(os.environ['STRATA_HOME'], mode=0o700)
CRLF = b'\r\n'
DOC = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
       b'P1C33' + CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF +
       b'DP0,8500,11000' + CRLF + b'*B0' + CRLF + b'S' + CRLF +
       b'Tx\x1d\x00' + CRLF + b'*E' + CRLF)
FILE = ['F12', 'ENTER']
RETRIEVE = FILE + ['RIGHT', 'ENTER', 'wait']


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
          + str(extra)))
    fails += 0 if cond else 1


def listing(n_files, n_dirs=0):
    """A directory of n_files documents and n_dirs directories, and the
    screens of the checklist of its files: at the top and at the bottom."""
    d = os.path.join(tmp, 'd%d_%d' % (n_files, n_dirs))
    os.makedirs(d)

    for i in range(n_files):
        with open(os.path.join(d, 'f%04d.doc' % i), 'wb') as f:
            f.write(DOC)

    for i in range(n_dirs):
        os.makedirs(os.path.join(d, 'a%04d' % i))

    with open(os.path.join(d, 'g.doc'), 'wb') as f:
        f.write(DOC)

    return tui.session([stred, '-H', helpf, os.path.join(d, 'g.doc')],
                       RETRIEVE + ['DOWN', 'A-+', 'wait', 'show', 'END',
                                   'wait', 'show'], rows=40, cols=100)


top, bottom = listing(600)
check('600 files: the list starts with the first names',
      all('f%04d.doc' % i in top for i in range(0, 5)), top)
check('600 files: the list ends with the last names',
      'f0599.doc' in bottom and 'g.doc' in bottom and
      'f0595.doc' in bottom, bottom)

top, bottom = listing(2000, 600)
check('600 directories and 2000 files: the directories come first, by name',
      'a0000/' in top and 'a0004/' in top, top)
check('600 directories and 2000 files: the last file is the last name',
      'f1999.doc' in bottom and 'g.doc' in bottom, bottom)

print('checklist: %d failures' % fails)
sys.exit(1 if fails else 0)
