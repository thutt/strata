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

"""Edit, Structured and Edit, Unstructured do what the manual says.

The manual says: Edit, Structured makes the whole text one section, the first
block of the text is its headline, the cursor goes to the start and the
document is marked as changed; Edit, Unstructured is offered only for a
structured document that has one section and no subsections, it turns it back
into a plain document, the headline stays as the first block, the cursor goes
to the start, the title page is not touched and no text is thrown away.

The Editor is driven in a pseudo-terminal through the F12 menu (there is no
key for either command).  Every document is saved and the records of its body
are compared with the records that the text says: a structured body is S + S
blocks -, an unstructured body is S blocks.  Markers, tables and a Picture
marker go through the round trip unchanged.  Every check fails when the
feature is turned off: the mutations that were run are in docs/STRUCT-CONV.txt.

    STRATA_BUILD_DIR=... python3 -I -B tests/structconv.py
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
tmp = tempfile.mkdtemp(prefix='structconv')
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


def header(body):
    return b'*R3' + CRLF + b'MR1001' + CRLF + b'FHA1' + CRLF + body


def blocks_plain():
    """The blocks of the plain document: three lines of text."""
    return (trec(b'Alpha line') + b'B' + CRLF + trec(b'Beta line') + b'B' +
            CRLF + trec(b'Gamma line'))


def blocks_rich():
    """Text, a Picture marker, a table of two rows and a Figure marker."""
    out = trec(b'Alpha line of the text')
    out += b'B' + CRLF + trec(b'\x1325(1missing.png\x00' + CRLF +
                              b'22000' + CRLF + b'\x13)')
    for r in range(2):
        out += b'B' + CRLF
        for c in range(2):
            out += ((b'CW12' + CRLF if c < 1 else b'') + b'CB84,1' + CRLF +
                    b'CL1' + CRLF + b'CR1' + CRLF +
                    trec(b'Cell %d%d' % (r + 1, c + 1)))
    out += b'B' + CRLF + trec(b'A note \x135(1Fig one\x00' + CRLF +
                              b'\x13) after')
    return out


TITLE = b'=1' + CRLF + b'S' + CRLF + trec(b'The title')


def flat(blocks, title=b''):
    return header(b'*B0' + CRLF + title + b'S' + CRLF + blocks +
                  b'*E' + CRLF)


def one_section(blocks, title=b''):
    return header(b'*B1' + CRLF + title + b'S' + CRLF + b'+' + CRLF + b'S' +
                  CRLF + blocks + b'-' + CRLF + b'*E' + CRLF)


def two_sections():
    return header(b'*B1' + CRLF + b'S' + CRLF + b'+' + CRLF + b'S' + CRLF +
                  trec(b'Head one') + b'B' + CRLF + trec(b'Text one') +
                  b'S' + CRLF + trec(b'Head two') + b'-' + CRLF + b'*E' +
                  CRLF)


def subsection():
    return header(b'*B1' + CRLF + b'S' + CRLF + b'+' + CRLF + b'S' + CRLF +
                  trec(b'Head one') + b'+' + CRLF + b'S' + CRLF +
                  trec(b'Sub head') + b'-' + CRLF + b'-' + CRLF + b'*E' +
                  CRLF)


def write(name, data):
    with open(os.path.join(tmp, name), 'wb') as f:
        f.write(data)


def read(name):
    with open(os.path.join(tmp, name), 'rb') as f:
        return f.read()


def body(data):
    """The *B record and the records of the body, up to *E, without the Z
    records that a save adds (the place of the cursor)."""
    recs = data.split(CRLF)
    i = next(k for k, r in enumerate(recs) if r.startswith(b'*B'))
    return recs[i], [r for r in recs[i + 1:] if r != b'*E' and r != b'' and
                     not r.startswith(b'Z')]


def run(name, steps):
    return tui.session([stred, '-H', helpf, name], steps)


def status(d):
    return d.split('\n')[0]


def rows(d):
    return d.split('\n')[2:-1]


def untyped(recs):
    """The records with the letters typed by a check taken out of the text."""
    return [r.replace(b'TXXAlpha', b'TAlpha').replace(b'TXAlpha', b'TAlpha')
            for r in recs]


def convert(name, letter):
    """Open, move the cursor down two lines and right two columns, choose
    the Edit menu entry by its letter, show the screen, type X at the
    cursor, show again, save and leave.  The screens are: the menu, the
    document after the command, the document after the letter."""
    return run(name, ['DOWN', 'DOWN', 'RIGHT', 'RIGHT', 'F12', 'text:E',
                      'show', 'text:' + letter, 'wait', 'show', 'text:X',
                      'show', 'C-s', 'wait', 'C-q', 'wait'])


# ---- (a) Edit, Structured on a plain document --------------------------------
write('plain.doc', flat(blocks_plain()))
orig = body(read('plain.doc'))[1]
d = convert('plain.doc', 'S')
check('menu: the Edit menu of a plain document offers Structured and no '
      'Unstructured', 'Structured' in status(d[0]) and 'Unstructured' not in
      status(d[0]), status(d[0]))
check('structured: the status line shows STRUCTURED EDIT and Sect: 1',
      'STRUCTURED EDIT' in status(d[1]) and 'UNSTRUCTURED' not in
      status(d[1]) and 'Sect: 1' in status(d[1]), status(d[1]))
check('structured: the first block of the text is the headline: it is '
      'numbered 1, the other blocks follow it unnumbered',
      rows(d[1])[0].startswith('1 Alpha line') and
      rows(d[1])[2].startswith('Beta line') and
      rows(d[1])[4].startswith('Gamma line'), '\n'.join(rows(d[1])[:6]))
check('structured: the document is marked as changed (a star before the '
      'name)', '*plain.doc' in status(d[1]).replace(' ', ''), status(d[1]))
check('structured: the cursor is at the start of the document (the letter '
      'typed stands before the headline text)',
      'XAlpha line' in d[2], '\n'.join(rows(d[2])[:3]))
bb, recs = body(read('plain.doc'))
check('structured: the saved *B record says 1', bb == b'*B1', bb)
check('structured: the body is S + S, the blocks and -, exactly once '
      '(one section, one group)', recs[:3] == [b'S', b'+', b'S'] and
      recs[-1] == b'-' and recs.count(b'S') == 2 and recs.count(b'+') == 1
      and recs.count(b'-') == 1, recs)
check('structured: no text is lost: the records between S + S and - are '
      'those of the plain document (the typed X aside)',
      untyped(recs[3:-1]) == orig[1:], (orig, recs))

# ---- (b) Edit, Unstructured on a document of one section ---------------
write('one.doc', one_section(blocks_plain()))
d0 = run('one.doc', ['F12', 'text:E', 'show', 'ESC', 'ESC'])
check('menu: the Edit menu of a document of one section offers '
      'Unstructured and no Structured', 'Unstructured' in status(d0[0]) and
      'Structured' not in status(d0[0]).replace('Unstructured', ''),
      status(d0[0]))
d = convert('one.doc', 'U')
check('unstructured: the status line shows UNSTRUCTURED EDIT and no Sect',
      'UNSTRUCTURED EDIT' in status(d[1]) and 'Sect:' not in status(d[1]),
      status(d[1]))
check('unstructured: the headline stays as the first block, without a '
      'number', rows(d[1])[0].startswith('Alpha line') and
      not rows(d[1])[0].startswith('1'), '\n'.join(rows(d[1])[:4]))
check('unstructured: the document is marked as changed',
      '*one.doc' in status(d[1]).replace(' ', ''), status(d[1]))
check('unstructured: the cursor is at the start of the document',
      'XAlpha line' in d[2], '\n'.join(rows(d[2])[:3]))
bb, recs = body(read('one.doc'))
check('unstructured: the saved *B record says 0 and the body is S and '
      'the blocks, nothing else', bb == b'*B0' and recs[0] == b'S' and
      b'+' not in recs and b'-' not in recs and recs.count(b'S') == 1,
      (bb, recs))
check('unstructured: no text is thrown away: the records after S are '
      'those of the section (the typed X aside)',
      untyped(recs[1:]) == body(one_section(blocks_plain()))[1][3:-1],
      recs)

# ---- (c) several sections, a subsection: nothing is offered ------------
for name, doc, what in (('two.doc', two_sections(), 'two sections'),
                        ('sub.doc', subsection(), 'a subsection')):
    write(name, doc)
    d = run(name, ['F12', 'text:E', 'show', 'text:U', 'wait', 'show',
                   'ESC', 'ESC', 'show'])
    check('refused (%s): the Edit menu offers no Unstructured' % what,
          'Unstructured' not in status(d[0]) and 'Section' in status(d[0]),
          status(d[0]))
    check('refused (%s): the letter U does nothing: the document is still '
          'STRUCTURED and not changed' % what,
          'STRUCTURED EDIT' in status(d[2]) and
          'UNSTRUCTURED' not in status(d[2]) and
          '*' + name not in status(d[2]).replace(' ', ''), status(d[2]))
    check('refused (%s): the letter U gives no message' % what,
          'cannot be made unstructured' not in d[1] and
          'cannot be made unstructured' not in d[2], d[1])
    run(name, ['F12', 'text:E', 'text:U', 'wait', 'C-s', 'wait', 'C-q',
               'wait'])
    check('refused (%s): the saved body is the same as before' % what,
          body(read(name))[1] == body(doc)[1], body(read(name)))

# ---- (d) Undo ----------------------------------------------------------------
write('undo1.doc', flat(blocks_plain()))
d = run('undo1.doc', ['F12', 'text:E', 'text:S', 'wait', 'show', 'C-z',
                      'wait', 'show'])
check('undo: Undo takes Structured back in one step: the document is '
      'plain again', 'STRUCTURED EDIT' in status(d[0]) and
      'UNSTRUCTURED EDIT' in status(d[1]) and
      rows(d[1])[0].startswith('Alpha line'), (status(d[1]), rows(d[1])[:2]))
write('undo2.doc', one_section(blocks_plain()))
d = run('undo2.doc', ['F12', 'text:E', 'text:U', 'wait', 'show', 'C-z',
                      'wait', 'show'])
check('undo: Undo takes Unstructured back in one step: the section and '
      'its number are back', 'UNSTRUCTURED EDIT' in status(d[0]) and
      'STRUCTURED EDIT' in status(d[1]) and 'UNSTRUCTURED' not in
      status(d[1]) and rows(d[1])[0].startswith('1 Alpha'),
      (status(d[1]), rows(d[1])[:2]))

# ---- (e) markers, a table and a Picture marker: the round trip ---------
write('rich.doc', flat(blocks_rich()))
orig = body(read('rich.doc'))[1]
convert('rich.doc', 'S')
bb, recs = body(read('rich.doc'))
check('rich: after Structured the blocks with the markers, the Picture '
      'marker and the table (CW, CB, CL, CR records) stand unchanged '
      'between S + S and -', recs[:3] == [b'S', b'+', b'S'] and
      recs[-1] == b'-' and untyped(recs[3:-1]) == orig[1:], recs)
convert('rich.doc', 'U')
bb, recs = body(read('rich.doc'))
check('rich: after Unstructured the body is that of the first document '
      '(the typed letters aside)', bb == b'*B0' and untyped(recs) == orig,
      recs)

# ---- the title page is not touched -------------------------------------------
write('title.doc', flat(blocks_plain(), TITLE))
tb = body(read('title.doc'))[1]
convert('title.doc', 'S')
bb, recs = body(read('title.doc'))
check('title: Structured leaves the title page records before the first '
      'section and puts the section after them',
      recs[:3] == tb[:3] and recs.count(b'+') == 1 and
      recs[3:6] == [b'S', b'+', b'S'], recs)
convert('title.doc', 'U')
bb, recs = body(read('title.doc'))
check('title: Unstructured leaves the title page as it was and the text '
      'follows it', bb == b'*B0' and recs[:3] == tb[:3] and
      b'+' not in recs and untyped(recs) == tb, recs)

print('structconv: %s' % ('all passed' if fails == 0 else
                         '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
