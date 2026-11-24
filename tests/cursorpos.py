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

"""The place of the cursor is saved with the document.

Save, Save As and Autosave of a changed document write the record
ZP<item>,<col>,<tok>,<row> before *B, and the Editor goes back to that place
and screen row when it opens the document again.  A document without the
record, or with a record that cannot be read, opens at the top; a record that
points beyond the end goes to the nearest place; moving the cursor does not
change the document; a document made from a template starts at the top;
printing ignores the record.  docs/CURSOR-POSITION.txt.

Every check fails when its feature is turned off: a mutation of the program
for each part was run (see docs/CURSOR-POSITION.txt).

    STRATA_BUILD_DIR=... python3 -I -B tests/cursorpos.py
"""
import sys as _s; _s.dont_write_bytecode = True
import atexit, os, shutil, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
                                                     'strprint'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
if not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
# The scratch folder is made where TMPDIR says, and is removed at the end.
tmp = tempfile.mkdtemp(prefix='cursorpos')
atexit.register(shutil.rmtree, tmp, True)
home = os.path.join(tmp, 'home')
os.makedirs(home, mode=0o700)
os.environ['STRATA_HOME'] = home
os.chdir(tmp)
CRLF = b'\r\n'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def trec(text):
    return b'T' + text + b'\x1d\x00' + CRLF


def header(extra=b''):
    return (b'*R3' + CRLF + b'MR1001' + CRLF + b'FHA1' + CRLF + extra)


def flat(n, extra=b''):
    """An unstructured document of n blocks: Line 1 of the text, ..."""
    out = header(extra) + b'*B0' + CRLF + b'S' + CRLF
    for i in range(n):
        out += (b'' if i == 0 else b'B' + CRLF) + trec(
            b'Line %d of the text' % (i + 1))
    return out + b'*E' + CRLF


def sections(n, extra=b''):
    """A structured document of n sections of two blocks each."""
    out = (header(extra) + b'*B1' + CRLF + b'S' + CRLF + trec(b'Title') +
           b'+' + CRLF)
    for i in range(n):
        out += b'SE0' + CRLF + trec(b'Head %d' % (i + 1))
        out += b'B' + CRLF + trec(b'First text of %d' % (i + 1))
        out += b'B' + CRLF + trec(b'Second text of %d' % (i + 1))
    return out + b'-' + CRLF + b'*E' + CRLF


def write(name, data):
    with open(os.path.join(tmp, name), 'wb') as f:
        f.write(data)
    return os.path.join(tmp, name)


def read(name):
    with open(os.path.join(tmp, name), 'rb') as f:
        return f.read()


def run(name, steps, args=(), env=None):
    return tui.session([stred, '-H', helpf] + list(args) + [name], steps,
                       env=env or {})


def body(d):
    """The text rows of a screen, without the status line and the message."""
    rows = d.split('\n')
    return '\n'.join(rows[2:-1])


def zp(data):
    """The ZP record of a document, or None."""
    for rec in data.split(CRLF):
        if rec.startswith(b'ZP'):
            return rec
    return None


# ---- a long flat document: the same screen after the reload ----------------
write('long.doc', flat(120))
d = run('long.doc', ['PGDN', 'PGDN', 'DOWN', 'DOWN', 'RIGHT', 'RIGHT',
                     'text:X', 'show', 'C-s', 'wait', 'C-q', 'wait'])
before = d[0]
rec = zp(read('long.doc'))
check('save: the record ZP is written when a change is saved',
      rec is not None and rec.startswith(b'ZP') and rec.count(b',') == 3,
      rec)
d = run('long.doc', ['show', 'text:Y', 'show'])
check('reload: the same text is on the screen as when it was saved',
      body(d[0]) == body(before), (before, d[0]))
check('reload: the cursor is at the same column of the same line: the next '
      'letter goes in at the place', 'LiXYne' in d[1], d[1])
check('reload: the document has not been changed by reading the place '
      '(no star)', '*long.doc' not in d[0] and '* long.doc' not in d[0],
      d[0].split('\n')[0])

# ---- a long document: the cursor is on the screen row it had ---------------
rows_before = [i for i, r in enumerate(before.split('\n')) if 'LiXne' in r]
rows_after = [i for i, r in enumerate(d[1].split('\n')) if 'LiXYne' in r]
check('reload: the cursor line is on the same screen row as when it was '
      'saved', rows_before and rows_before == rows_after,
      (rows_before, rows_after))

# ---- a structured document ---------------------------------------------------
write('sec.doc', sections(30))
d = run('sec.doc', ['PGDN', 'DOWN', 'DOWN', 'RIGHT', 'text:Z', 'show', 'C-s',
                    'wait', 'C-q', 'wait'])
sbefore = d[0]
d = run('sec.doc', ['show', 'text:W', 'show'])
check('structured: the document opens where it was saved',
      body(d[0]) == body(sbefore), (sbefore, d[0]))
check('structured: the cursor is at the same place in the block',
      'WZ' in d[1] or 'ZW' in d[1] or 'FWZirst' in d[1] or 'FZWirst' in d[1]
      or 'FiWZ' in d[1] or 'SWZecond' in d[1], d[1])

# ---- moving the cursor alone changes nothing -----------------------------------
write('move.doc', flat(120))
orig = read('move.doc')
d = run('move.doc', ['PGDN', 'DOWN', 'show', 'C-s', 'wait', 'C-q', 'wait'])
check('move: moving the cursor does not mark the document as changed',
      '*move.doc' not in d[0].split('\n')[0] and
      '* move.doc' not in d[0].split('\n')[0], d[0].split('\n')[0])
check('move: Save of a document that was not changed writes nothing (the '
      'file is the same bytes)', read('move.doc') == orig)
d = run('move.doc', ['show'])
check('move: a document saved without a change opens at the top again',
      'Line 1 of the text' in d[0].split('\n')[2], d[0])

# ---- a document with no record, and records that cannot be read ----------------
write('none.doc', flat(120))
d = run('none.doc', ['text:Y', 'show'])
check('none: a document without the record opens at the top',
      'YLine 1 of the text' in d[0], d[0])
for name, extra in (('bad1.doc', b'ZPabc'), ('bad2.doc', b'ZP1,2,3'),
                    ('bad3.doc', b'ZP99999999999,1,1,1'),
                    ('bad4.doc', b'ZP1,2,3,4,5'), ('bad5.doc', b'ZP-3,0,0,0'),
                    ('bad6.doc', b'ZP')):
    write(name, flat(120, extra + CRLF))
    d = run(name, ['text:Y', 'show'])
    check('bad record %r: the document loads and opens at the top' % extra,
          'YLine 1 of the text' in d[0], d[0])

# ---- a record beyond the end of a shorter document ---------------------------
write('short.doc', flat(5, b'ZP60,0,0,3' + CRLF))
d = run('short.doc', ['text:Y', 'show'])
check('short: a place beyond the end goes to the last place that there is',
      'YLine 5 of the text' in d[0], d[0])
write('short2.doc', flat(5, b'ZP2,0,500,1' + CRLF))
d = run('short2.doc', ['text:Y', 'show'])
check('short: a token beyond the end of the block goes to the end of the '
      'block', 'Line 2 of the textY' in d[0].replace('◄', ''), d[0])

# ---- a document made from a template starts at the top -----------------------
write('tpl.doc', flat(120, b'ZP60,0,3,3' + CRLF))
d = run('fromtpl.doc', ['text:Y', 'show'], args=['--template',
                                              os.path.join(tmp, 'tpl.doc')])
check('template: a new document made from a template starts at the top '
      'even if the template holds a place',
      'YLine 1 of the text' in d[0], d[0])

# ---- Autosave writes the place ---------------------------------------------
write('auto.doc', flat(120))
d = run('auto.doc', ['PGDN', 'PGDN', 'text:A', 'wait', 'wait', 'wait', 'wait',
                     'wait', 'wait'], args=['--autosave-seconds', '1'])
a = zp(read('auto.doc'))
check('autosave: the record is written by an Autosave of a changed document',
      a is not None and b'A' in read('auto.doc'), a)

# ---- backups carry the place -------------------------------------------------
os.makedirs(home, exist_ok=True)
with open(os.path.join(home, 'config'), 'w') as f:
    f.write('backups=3\n')
os.chmod(os.path.join(home, 'config'), 0o600)
write('bak.doc', flat(120))
run('bak.doc', ['PGDN', 'text:B', 'C-s', 'wait', 'C-q', 'wait'])
first = zp(read('bak.doc'))
run('bak.doc', ['PGDN', 'PGDN', 'text:C', 'C-s', 'wait', 'C-q', 'wait'])
bakfile = os.path.join(tmp, '.backup', 'bak.bak01')
second = zp(read('bak.doc'))
check('backup: the first save holds a place', first is not None, first)
check('backup: the older version in the backup keeps its place and the '
      'new version has another',
      os.path.exists(bakfile) and zp(open(bakfile, 'rb').read()) == first and
      second is not None and second != first,
      (first, second, os.listdir(os.path.join(tmp, '.backup'))
       if os.path.isdir(os.path.join(tmp, '.backup')) else None))
with open(os.path.join(home, 'config'), 'w') as f:
    f.write('')

# ---- opening a backup file (reading is allowed) restores its place -----------
if os.path.exists(bakfile):
    d = run(bakfile, ['show'])
    check('backup: the Editor opens a backup file at the place it holds',
          'Line 1 of the text' not in d[0].split('\n')[2], d[0])

# ---- printing ignores the record ---------------------------------------------
if os.path.exists(strprint):
    data = read('long.doc')
    stripped = CRLF.join(r for r in data.split(CRLF) if not r.startswith(b'ZP'))
    write('long-nozp.doc', stripped)
    p = []
    for k, name in enumerate(('long.doc', 'long-nozp.doc')):
        os.makedirs(os.path.join(tmp, 'tex%d' % k), exist_ok=True)
        out = os.path.join(tmp, 'tex%d' % k, 'same.tex')
        subprocess.run([strprint, os.path.join(tmp, name), '--tex-only', '-o',
                        out, '--no-banner', '--source-name', 'same.doc',
                        '--source-path', '/same'], capture_output=True)
        p.append(open(out, 'rb').read() if os.path.exists(out) else None)
    check('print: the document holds a place (the test is meant)',
          zp(data) is not None and zp(stripped) is None)
    check('print: the LaTeX is the same bytes with the place and without it',
          p[0] is not None and p[0] == p[1])

sys.exit(1 if fails else 0)
