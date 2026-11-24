#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The version of the file format as the programs show it.

The first record of a file is *R<n>.  The programs read versions 2 and 3 and
write 3.  This test runs the built programs: --version of each program names
the file format; the Editor and the Printer report the three errors for a
file with the messages of the manual; the Editor makes the special backup
when it loads a file of version 2 (once, next to the file, never written
again, none for version 3) and writes version 3 when the file is saved, and
the backup keeps version 2.

    STRATA_BUILD_DIR=... python3 -I -B tests/fileversion.py
"""

import sys as _s; _s.dont_write_bytecode = True
import os, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
termdir = os.path.dirname(stred)

if not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='fv', dir='/tmp')

BODY = (b'MR1001\r\nPP\\1\\\x00\r\n*B0\r\nS\r\nTWords of the test.\x1d\x00\r\n'
        b'*E\r\n')

OLD = ('The file is version 4.  This software reads up to version 3.')
LOW = 'The file version is %s.  This software reads versions 2 to 3.'
NOT = 'The file is not a document of this software.'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
          + str(extra)))
    fails += 0 if cond else 1


def make(name, first, body=BODY):
    path = os.path.join(tmp, name)
    with open(path, 'wb') as f:
        f.write(first + b'\r\n' + body)
    return path


def run(argv):
    p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       stdin=subprocess.DEVNULL, timeout=60,
                       env={'PATH': '/usr/bin:/bin', 'STRATA_HELP': helpf,
                            'HOME': tmp, 'STRATA_HOME': tmp})
    return p.returncode, p.stdout.decode('latin-1'), p.stderr.decode('latin-1')


# ---- --version names the file format ---------------------------------------
for prog in ('stred', 'str', 'strprint', 'dviview'):
    path = os.path.join(termdir, prog)
    if not os.path.exists(path):
        continue
    rc, out, err = run([path, '--version'])
    check('%s --version has the line "file format 3"' % prog,
          rc == 0 and 'file format 3\n' in out, out + err)

# ---- the three errors, in the Editor (--text) and in the Printer -----------
errors = [
    ('too new', b'*R4', OLD),
    ('far too new', b'*R10', 'The file is version 10.  This software reads '
                             'up to version 3.'),
    ('below the oldest', b'*R1', LOW % '1'),
    ('zero', b'*R0', LOW % '0'),
    ('damaged', b'*Rx', LOW % '"x"'),
    ('empty', b'*R', LOW % '""'),
]
for what, first, msg in errors:
    path = make('e-%s.doc' % what.replace(' ', '-'), first)
    out = os.path.join(tmp, 'out.txt')
    rc, so, se = run([stred, '--text', path, out])
    check('the Editor reads a file that is %s: the message' % what,
          rc == 1 and msg in se, se)
    if os.path.exists(os.path.join(termdir, 'strprint')):
        rc, so, se = run([os.path.join(termdir, 'strprint'), path,
                          '--tex-only', '-o', os.path.join(tmp, 'p.tex')])
        check('the Printer reads a file that is %s: the message' % what,
              rc != 0 and msg in se + so, se + so)

path = os.path.join(tmp, 'plain.doc')
with open(path, 'wb') as f:
    f.write(b'Just some text.\r\n')
rc, so, se = run([stred, '--text', path, os.path.join(tmp, 'out.txt')])
check('a text file is not a document of this software',
      rc == 1 and NOT in se, se)

for first in (b'*R2', b'*R3'):
    path = make('ok-%s.doc' % first.decode()[2:], first)
    out = os.path.join(tmp, 'ok.txt')
    rc, so, se = run([stred, '--text', path, out])
    check('the Editor reads a %s file' % first.decode(), rc == 0, se)

# ---- the special backup of the Editor ---------------------------------------
helphome = tempfile.mkdtemp(prefix='h', dir=tmp)
os.chmod(helphome, 0o700)
ENV = {'STRATA_HOME': helphome, 'TMUX': None, 'LANG': 'en_US.UTF-8',
       'LC_ALL': None, 'STRATA_ASCII': '0', 'DISPLAY': ':99',
       'TMPDIR': tmp, 'STRATA_HELP': helpf}


def editor(doc, steps):
    return tui.session([stred, doc], ['waitfor:F1: Help'] + steps, env=ENV)


def read(path):
    with open(path, 'rb') as f:
        return f.read()


ddir = os.path.join(tmp, 'docs')
os.mkdir(ddir)
doc = os.path.join(ddir, 'memo.doc')
bak = os.path.join(ddir, 'V2-memo.doc')
original = b'*R2\r\n' + BODY
with open(doc, 'wb') as f:
    f.write(original)

editor(doc, ['show', 'C-q'])
check('loading a version 2 file makes V2-memo.doc next to it',
      os.path.exists(bak) and read(bak) == original, os.listdir(ddir))
check('loading a file does not change it', read(doc) == original)

with open(bak, 'wb') as f:
    f.write(b'kept')
editor(doc, ['show', 'C-q'])
check('a special backup that exists is not written again',
      read(bak) == b'kept')

with open(bak, 'wb') as f:
    f.write(original)
editor(doc, ['text:More words. ', 'C-s', 'wait', 'wait', 'show', 'C-q'])
saved = read(doc)
check('saving a version 2 file writes version 3 (*R3)',
      saved[:5] == b'*R3\r\n' and b'More words.' in saved, saved[:60])
check('the special backup keeps version 2 after the save',
      read(bak) == original)

doc3 = os.path.join(ddir, 'plan.doc')
with open(doc3, 'wb') as f:
    f.write(b'*R3\r\n' + BODY)
editor(doc3, ['show', 'C-q'])
check('loading a version 3 file makes no special backup',
      not os.path.exists(os.path.join(ddir, 'V3-plan.doc')) and
      not os.path.exists(os.path.join(ddir, 'V2-plan.doc')),
      os.listdir(ddir))

# A version 2 file whose name is so long that V2-<name> would be longer than
# a file name can be (253 bytes: V2- makes 256): the special backup cannot
# be written, so the load is refused and the original is not touched.
longname = os.path.join(ddir, 'x' * 249 + '.doc')
with open(longname, 'wb') as f:
    f.write(original)
d = editor(longname, ['show', 'C-q'])
# The message starts with the name of the file, which is longer than the
# screen, so the screen shows the name and no document: the Editor did not
# open the file.
check('a version 2 file whose special backup name is too long is refused',
      d[0].startswith(longname[:70]) and 'Pos:' not in d[0] and
      'Hello' not in d[0], d[0][:200])
check('the refused load leaves the file as it was and makes no backup',
      read(longname) == original and
      not [n for n in os.listdir(ddir) if n.startswith('V2-x')],
      os.listdir(ddir))

shutil.rmtree(tmp, ignore_errors=True)
print('fileversion: %s' % ('all passed' if fails == 0 else
                           '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
