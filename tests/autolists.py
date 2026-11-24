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

"""The contents and the lists that the Printer makes from the document.

A document stores the level of its table of contents (the record YCL), a
setting for each list (the records FZLF, FZLP, FZLT and FZLI: 1 on, 0 off, none
is automatic) and the number of markers of each kind (ZNF, ZNP, ZNT, ZNI).  The
Printer, with no options, prints the contents of a structured document and the
lists whose setting is on, or that have no setting and whose count is not 0;
an option of the command line wins.  docs/AUTOLISTS.txt has the design; the
counters of the edits are checked in tests/listtest.c.

    STRPRINT=... STRED=... python3 -I -B tests/autolists.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
    'strprint'))
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
for tool in ('pdflatex', 'pdftotext', 'pdfinfo'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint):
    print('SKIP: build the programs first')
    sys.exit(0)
import tui

fails = 0
tmp = tempfile.mkdtemp(prefix='autol')
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
os.environ['HOME'] = tempfile.mkdtemp(prefix='uhome', dir=tmp)
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'
CRLF = b'\r\n'
EOL = b'\x1d'
data = os.path.join(here, 'data')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace',
        timeout=300)
    return r.returncode, r.stdout, r.stderr


def marker(t, recs=()):
    return b'\x13' + str(t).encode() + b'(' + b''.join(
        r + CRLF for r in recs) + b'\x13)'


def FIGURE(name):
    return marker(11, [b'1' + name + b'\x00'])


def TABLE(name):
    return marker(29, [b'1' + name + b'\x00'])


def PICTURE(name):
    return marker(25, [b'1picture.png\x00', b'6' + name + b'\x00'])


def INDEX(main):
    return marker(3, [b'2' + main + b'\x00'])


def INCLUDE(name):
    return marker(16, [b'1' + name + b'\x00'])


def trec(text):
    return b'T' + text + EOL + b'\x00' + CRLF


def header(extra=b''):
    return (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
            b'P1HB0' + CRLF + b'P1FB0' + CRLF + b'P1C33' + CRLF +
            b'FHA1' + CRLF + b'FTT5' + CRLF + b'DP0,8500,11000' + CRLF +
            b'YCL99' + CRLF + b'YIWN2' + CRLF + extra)


def structured(body_extra=None, hdr=b'', counts=b'', title=b'The Report',
               blocks=None):
    """A structured document: two chapters with two sections each.  The
    body of a section is 'Body of c.s'; body_extra maps 'c.s' to markers put
    after its text and blocks maps 'c.s' to the text of one more block."""
    body_extra = body_extra or {}
    blocks = blocks or {}
    out = header(hdr) + counts + b'*B1' + CRLF + b'S' + CRLF + trec(title)
    out += b'+' + CRLF
    for c in (1, 2):
        out += b'S' + CRLF + trec(b'Chapter %d' % c)
        out += b'B' + CRLF + trec(b'Text of chapter %d' % c)
        out += b'+' + CRLF
        for s in (1, 2):
            key = '%d.%d' % (c, s)
            out += b'S' + CRLF + trec(b'Section %d.%d' % (c, s))
            out += b'B' + CRLF + trec(b'Body of %d.%d' % (c, s) +
                                      body_extra.get(key, b''))

            if key in blocks:
                out += b'B' + CRLF + trec(blocks[key])

        out += b'-' + CRLF
    out += b'-' + CRLF + b'*E' + CRLF
    return out


def flat(lines, extra=None, hdr=b'', counts=b''):
    """An unstructured document of blocks 'Line k'."""
    extra = extra or {}
    out = header(hdr) + counts + b'*B0' + CRLF + b'S' + CRLF
    for i in range(lines):
        out += (b'' if i == 0 else b'B' + CRLF) + trec(
            b'Line %d' % (i + 1) + extra.get(i + 1, b''))
    return out + b'*E' + CRLF


def newdir(name):
    d = os.path.join(tmp, '%s%d' % (name, len(os.listdir(tmp))))
    os.makedirs(d)
    shutil.copy(os.path.join(data, 'picture.png'), d)
    return d


def write(d, name, raw):
    p = os.path.join(d, name)
    with open(p, 'wb') as f:
        f.write(raw)
    return p


def printed(raw, extra=(), name='d'):
    """The text of the PDF that the Printer makes of the document with the
    options, and the number of pages."""
    d = newdir('p')
    doc = write(d, name + '.doc', raw)
    pdf = os.path.join(d, name + '.pdf')
    rc, so, se = run([strprint, doc, '-o', pdf, '--no-banner',
                      '--no-error-log'] + list(extra))
    if not os.path.exists(pdf):
        return '', 0, se
    t = run(['pdftotext', '-layout', pdf, '-'])[1]
    n = int(re.search(r'Pages:\s+(\d+)', run(['pdfinfo', pdf])[1]).group(1))
    return t, n, se


def has(t, word):
    return re.search(r'\b' + re.escape(word) + r'\b', t) is not None


def before(t, a, b):
    ia = t.find(a)
    ib = t.find(b)
    return 0 <= ia < ib


# ---- the defaults of a structured document: the contents -------------------
t, n, se = printed(structured())
check('default: a structured document with headlines has a table of '
      'contents with no option', has(t, 'Contents') and
      has(t, 'Chapter 1') and has(t, 'Section 2.2'), t[:600] + se)
check('default: the contents is before the text',
      before(t, 'Contents', 'Text of chapter 1'), t[:800])
check('default: no list of figures, pictures or tables, and no index when '
      'the document has no marker', not any(w in t for w in (
          'List of Figures', 'List of Pictures', 'List of Tables',
          'Index')), t[:800])
t0, n0, _ = printed(structured(), ['--contents', '0'])
check('default: --contents 0 prints no contents and the document has fewer '
      'pages', not has(t0, 'Contents') and n0 < n, (n0, n))

# ---- the level of the contents (YCL) ---------------------------------------
raw1 = structured().replace(b'YCL99', b'YCL1')
t1, n1, _ = printed(raw1)
def front(t):
    """The pages before the text."""
    return t[:t.find('Text of chapter 1')]


check('YCL1: only the headlines of level 1 are listed',
      has(front(t1), 'Contents') and has(front(t1), 'Chapter 2') and
      not has(front(t1), 'Section 1.1'), t1[:600])
raw2 = structured().replace(b'YCL99', b'YCL2')
t2, n2, _ = printed(raw2)
check('YCL2: the headlines of levels 1 and 2 are listed',
      has(front(t2), 'Section 2.2') and has(front(t2), 'Section 1.1'),
      t2[:600])
raw0 = structured().replace(b'YCL99', b'YCL0')
tz, nz, _ = printed(raw0)
check('YCL0: the document has no contents', not has(tz, 'Contents') and
      nz == n0, (nz, n0))
t3, _, _ = printed(raw0, ['--contents', '2'])
check('an option wins: --contents 2 on a document with YCL0 prints it',
      has(t3, 'Contents') and has(t3, 'Chapter 1'), t3[:400])
t4, _, _ = printed(raw1, ['--contents', '99'])
check('an option wins: --contents 99 on a document with YCL1 lists all',
      has(front(t4), 'Section 1.1'), t4[:600])
tu, nu, _ = printed(flat(3))
check('an unstructured document has no contents (it has no headlines)',
      not has(tu, 'Contents'), tu[:300])
check('the record YCL is optional: a header with none is read as 99',
      has(printed(structured().replace(b'YCL99' + CRLF, b''))[0], 'Contents'))

# A structured document with no headline has nothing to list: no contents.
raw = (header() + b'*B1' + CRLF + b'S' + CRLF + trec(b'Only a title') +
       b'B' + CRLF + trec(b'Some text') + b'*E' + CRLF)
t, n, se = printed(raw)
check('a structured document with no headline has no contents page by '
      'default', 'Some text' in t and not has(t, 'Contents') and n == 1,
      (n, t[:300]))

# ---- a list turns on by itself with a marker -------------------------------
LISTS = [
    ('figures', 'List of Figures', FIGURE(b'The first figure'),
     'The first figure'),
    ('pictures', 'List of Pictures', PICTURE(b'The first picture'),
     'The first picture'),
    ('tables', 'List of Tables', TABLE(b'The first table'),
     'The first table'),
    ('index', 'Index', INDEX(b'zebra'), 'zebra'),
]
for kind, title, mk, entry in LISTS:
    t, n, se = printed(structured({'1.2': mk}))
    check('auto: one %s marker turns the %s on, with the entry and the '
          'page' % (kind, title), has(t, title) and entry in t and
          re.search(re.escape(entry) + r'.*\d', t) is not None, t[:900] + se)
    check('auto: the marker prints nothing at its place when the list is '
          'made', t.count(entry) == 1, t[:900])
    others = [x[1] for x in LISTS if x[0] != kind]
    check('auto: only the list of the kind appears (%s)' % kind,
          not any(o in t for o in others), t[:900])

# ---- an explicit setting, and the options ----------------------------------
SET = {'figures': (b'FZLF', FIGURE, b'F'), 'pictures': (b'FZLP', PICTURE, b'P'),
       'tables': (b'FZLT', TABLE, b'T'), 'index': (b'FZLI', INDEX, b'I')}
TITLE = {k: v[1] for k, v in zip([x[0] for x in LISTS], LISTS)}
for kind, title, mk, entry in LISTS:
    rec = SET[kind][0]
    t, n, se = printed(structured({'1.2': mk}, hdr=rec + b'0' + CRLF))
    check('explicit off: the setting %s0 keeps the list away although the '
          'document has a marker' % rec.decode(), not has(t, title),
          t[:900])
    t, n, se = printed(structured(hdr=rec + b'1' + CRLF))
    check('explicit on with no marker: nothing to list, so nothing is '
          'printed (no empty list)', not has(t, title), t[:900])
    t, n, se = printed(structured({'1.2': mk}, hdr=rec + b'1' + CRLF))
    check('explicit on with a marker: the list is made', has(t, title) and
          entry in t, t[:900])
off = {'figures': '--no-list-figures', 'pictures': '--no-list-pictures',
       'tables': '--no-list-tables', 'index': '--no-index'}
on = {'figures': '--list-figures', 'pictures': '--list-pictures',
      'tables': '--list-tables', 'index': '--index'}
for kind, title, mk, entry in LISTS:
    t, _, _ = printed(structured({'1.2': mk}), [off[kind]])
    check('option: %s keeps the %s away although the document turns it on' %
          (off[kind], title), not has(t, title), t[:900])
    t, _, _ = printed(structured({'1.2': mk}, hdr=SET[kind][0] + b'0' +
                                 CRLF), [on[kind]])
    check('option: %s makes the %s although the document says off' %
          (on[kind], title), has(t, title) and entry in t, t[:900])
t, _, _ = printed(structured({'1.2': FIGURE(b'Only figure')}),
                  ['--no-list-figures'])
check('a marker whose list is off is printed where it is, as text',
      'Only figure' in t, t[:900])

# ---- the order of the parts ------------------------------------------------
t, n, se = printed(structured({'1.1': FIGURE(b'Fig one'),
                               '1.2': TABLE(b'Tab one'),
                               '2.1': PICTURE(b'Pic one'),
                               '2.2': INDEX(b'quokka')}))
pos = [t.find(w) for w in ('Contents', 'List of Figures', 'List of Pictures',
                           'List of Tables', 'Text of chapter 1', 'Index')]
check('order: contents, figures, pictures, tables, the text, the index',
      all(p >= 0 for p in pos) and pos == sorted(pos), (pos, t[:1500]))
check('the lists of one document each have the entry of their marker',
      all(w in t for w in ('Fig one', 'Tab one', 'Pic one', 'quokka')), t)

# A marker that has no name makes no entry; it prints nothing either when its
# list is made by another marker (not its long form, FIGURE "").
t, n, se = printed(structured({'1.1': FIGURE(b'Named one'),
                               '1.2': FIGURE(b'')}))
check('a marker with no name prints nothing at its place when the list is '
      'made, and makes no entry', has(t, 'List of Figures') and
      'Named one' in t and 'FIGURE' not in t, t[:900])

# ---- the markers of an included document are in the print ------------------
d = newdir('inc')
write(d, 'part.doc', flat(2, {1: FIGURE(b'Included figure')}))
main = write(d, 'main.doc', structured(blocks={'1.1': INCLUDE(b'part.doc')}))
pdf = os.path.join(d, 'main.pdf')
run([strprint, main, '-o', pdf, '--no-banner', '--no-error-log'])
ti = run(['pdftotext', '-layout', pdf, '-'])[1] if os.path.exists(pdf) else ''
check('include: a Figure marker of the included document makes the list of '
      'figures of the print (it is counted on what is printed)',
      has(ti, 'List of Figures') and 'Included figure' in ti, ti[:900])

# ---- the old documents print as before when they have no marker -----------
told, nold, _ = printed(flat(3))
check('an unstructured document with no marker has no front or back part',
      not any(w in told for w in ('Contents', 'List of', 'Index')), told)

# ---- the stored counts and settings are read -------------------------------
t, _, _ = printed(structured({'1.2': FIGURE(b'Fig c')},
                             counts=b'ZNF1' + CRLF))
check('a document with its counts prints the list', has(t, 'List of '
      'Figures'), t[:600])
t, _, _ = printed(structured({'1.2': FIGURE(b'Fig d')},
                             counts=b'ZNF0' + CRLF))
check('the print counts what it prints: a wrong count of 0 in the file '
      'does not hide the list', has(t, 'List of Figures') and 'Fig d' in t,
      t[:600])

# ---- the Editor: the panel, the counts, the saved records ------------------
FORMAT = ['F12', 'text:G', 'ENTER', 'wait']
PANEL = FORMAT + ['PGUP', 'PGUP', 'PGUP']


def edit(doc, keys, rows=30):
    return tui.session([stred, '-H', helpf, doc], keys, rows=rows)


d = newdir('ed')
doc = write(d, 'a.doc', structured({'1.1': FIGURE(b'F one') + FIGURE(b'F two'),
                                    '1.2': TABLE(b'T one')},
                                   counts=b'ZNF2' + CRLF + b'ZNT1' + CRLF))
s = edit(doc, PANEL + ['show', 'ESC'])[0]
check('panel: the fourth panel of Global Format is Contents and Lists',
      'Global Format (contents and lists)' in s and all(w in s for w in (
          'Table of Contents Level', 'Table of Figures', 'Table of Pictures',
          'Table of Tables', 'Index', 'Figure Markers', 'Picture Markers',
          'Table Markers', 'Index Markers')), s)
check('panel: the contents level is 99 and every list is Automatic',
      re.search(r'Table of Contents Level\W+99', s) is not None and
      s.count('Automatic') == 4, s)
check('panel: the counts are shown (2 figures, 1 table)',
      re.search(r'Figure Markers\W+2\b', s) is not None and
      re.search(r'Table Markers\W+1\b', s) is not None and
      re.search(r'Picture Markers\W+0\b', s) is not None, s)
raw_before = open(doc, 'rb').read()
edit(doc, PANEL + ['ESC', 'wait', 'C-s', 'wait'])
check('panel: Esc changes nothing', b'FZL' not in open(doc, 'rb').read(),
      open(doc, 'rb').read()[:400])
doc2 = write(d, 'b.doc', structured())
edit(doc2, PANEL + ['text:3', 'DOWN', 'RIGHT', 'INS', 'wait', 'C-s', 'wait'])
r = open(doc2, 'rb').read()
check('panel: contents level 3 and the figures On are saved as YCL3 and '
      'FZLF1', b'\r\nYCL3\r\n' in r and b'\r\nFZLF1\r\n' in r,
      r[:400])
check('panel: the other lists stay automatic (no record)',
      b'FZLP' not in r and b'FZLT' not in r and b'FZLI' not in r, r[:400])
edit(doc2, PANEL + ['DOWN', 'LEFT', 'INS', 'wait', 'C-s', 'wait'])
r = open(doc2, 'rb').read()
check('panel: back to Automatic removes the record', b'FZLF' not in r,
      r[:400])
doc3 = write(d, 'c.doc', structured())
edit(doc3, PANEL + ['text:0', 'DOWN', 'DOWN', 'DOWN', 'RIGHT', 'RIGHT',
                    'INS', 'wait', 'C-s', 'wait'])
r = open(doc3, 'rb').read()
check('panel: level 0 and the tables Off are saved as YCL0 and FZLT0',
      b'\r\nYCL0\r\n' in r and b'\r\nFZLT0\r\n' in r, r[:400])
doc4 = write(d, 'e.doc', structured())
s = edit(doc4, PANEL + ['text:100', 'INS', 'wait', 'show', 'ESC'])
check('panel: a level over 99 is refused with a message that says why',
      'is a number from 0 to 99' in s[0], s[0])

# The counts follow the edits of the Editor and are saved with the document.
d = newdir('cnt')
doc = write(d, 'f.doc', flat(2, counts=b'ZNF1' + CRLF).replace(
    b'Line 1', FIGURE(b'Gone') + b'Line 1', 1))
edit(doc, ['DEL', 'wait', 'C-s', 'wait'])
r = open(doc, 'rb').read()
check('counts: Delete on the only Figure marker takes it out and the saved '
      'document has no count of figures any more', b'\x132' not in r and
      b'\x1311(' not in r and b'ZNF' not in r, r[:500])
d2 = newdir('cnt2')
doc = write(d2, 'h.doc', flat(2, counts=b'ZNF1' + CRLF).replace(
    b'Line 1', FIGURE(b'Kept') + b'Line 1', 1))
edit(doc, ['DOWN', 'C-s', 'wait'])
r = open(doc, 'rb').read()
check('counts: a save with the marker in the document writes ZNF1',
      b'ZNF1' in r and b'\x1311(' in r, r[:500])

# The Print panel gives the Printer an option only for a field that was
# changed: a stand-in for strprint logs its arguments and runs the real one.
d4 = newdir('args')
wrapper = os.path.join(d4, 'strprint-log')
with open(wrapper, 'w') as f:
    f.write('#!/bin/sh\nprintf "%%s\\n" "$*" >> %s/args.log\nexec %s "$@"\n'
            % (d4, strprint))
os.chmod(wrapper, 0o755)
doc = write(d4, 'k.doc', structured({'1.1': FIGURE(b'Panel fig')}))
out = os.path.join(d4, 'k.pdf')
os.environ['STRATA_STRPRINT'] = wrapper
FINAL = tui.OPEN_PRINT_FINAL
edit(doc, FINAL + ['text:' + out, 'INS', 'wait', 'wait', 'wait', 'wait',
                   'wait', 'wait'])
log = open(os.path.join(d4, 'args.log')).read() if os.path.exists(
    os.path.join(d4, 'args.log')) else ''
check('print panel: a print with no field changed gives the Printer no '
      'option about the contents or the lists (the document decides)',
      log != '' and not re.search(r'--(index|list-|no-index|no-list-|'
                                  r'contents)', log), log)
os.remove(os.path.join(d4, 'args.log'))
if os.path.exists(out):
    os.remove(out)
edit(doc, FINAL + ['text:' + out] + ['DOWN'] * 18 +
     ['RIGHT', 'INS', 'wait', 'wait', 'wait', 'wait', 'wait', 'wait'])
log = open(os.path.join(d4, 'args.log')).read() if os.path.exists(
    os.path.join(d4, 'args.log')) else ''
check('print panel: List of Tables? changed to Yes gives --list-tables and '
      'nothing else about the lists', '--list-tables' in log and
      not re.search(r'--(index|list-figures|list-pictures|no-)', log), log)
os.environ.pop('STRATA_STRPRINT', None)

# The Print panel starts with what the document says.
PRINT = tui.OPEN_PRINT_DRAFT
d3 = newdir('pp')
doc = write(d3, 'i.doc', structured({'1.1': FIGURE(b'Panel fig')}))
s = edit(doc, PRINT + ['wait', 'show', 'ESC'], rows=34)[0]
check('print panel: Table of Contents? and List of Figures? start at Yes '
      'for a document that has them, the other lists at No',
      re.search(r'Table of Contents\?\W+Yes', s) is not None and
      re.search(r'List of Figures\?\W+Yes', s) is not None and
      re.search(r'List of Tables\?\W+No', s) is not None and
      re.search(r'List of Pictures\?\W+No', s) is not None and
      re.search(r'Index\?\W+No', s) is not None, s)
doc = write(d3, 'j.doc', flat(2))
s = edit(doc, PRINT + ['wait', 'show', 'ESC'], rows=34)[0]
check('print panel: an unstructured document with no marker starts with '
      'Table of Contents? No', re.search(r'Table of Contents\?\W+No', s)
      is not None, s)

shutil.rmtree(tmp, ignore_errors=True)
print('autolists: %d failures' % fails)
sys.exit(1 if fails else 0)
