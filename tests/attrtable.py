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

"""The attribute table of the original: the mask and the flags of the token
12 m f.  Mask 01 bold, 02 underscore words (Ctrl-U), 04 italics, 08
subscript, 10 superscript, 20 strikethrough, 40 printer specific 1; flags 01
printer specific 2, 02 underscore all, 04 double underscore words, 08 small
caps, 10 double underscore all, 80 a state bit.  The documents are made here:
the print LaTeX has the right command for each, the printer specific
attributes print as plain text, a document with the mask 10
before a note marker prints it raised, and Ctrl-U and the Set Attributes menu
write 12 02 00 (and not 12 10 00).

    STRPRINT=... STRED=... STRATA_HELP=... python3 -I -B tests/attrtable.py
"""
import sys as _s; _s.dont_write_bytecode = True
import difflib, os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
    'strprint'))
for tool in ('pdflatex', 'pdftotext', 'pdfinfo'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='hdft')
# Nothing of a test goes into the real home of the user.
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
os.environ['HOME'] = tempfile.mkdtemp(prefix='uhome', dir=tmp)
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'
CRLF = b'\r\n'
EOL = b'\x1d'


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


PAGE = marker(22)
SECTION = marker(28)


def trec(text):
    return b'T' + text + EOL + b'\x00' + CRLF


def hfblock(left=b'', centre=b'', right=b''):
    """The block of a header or footer: three columns."""
    return (b'S' + CRLF + b'CW26' + CRLF + b'CFJ0' + CRLF + trec(left) +
            b'CW26' + CRLF + b'CFJ2' + CRLF + trec(centre) + b'CFJ1' + CRLF +
            trec(right))


def header(layouts=(), titlefree=True):
    """The header of a document; layouts is a list of (record, blocks)."""
    h = b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF
    if titlefree:
        h += b'P1HB0' + CRLF + b'P1FB0' + CRLF + b'P1C33' + CRLF
    for rec, blocks in layouts:
        h += rec + CRLF + blocks
    return (h + b'FHA1' + CRLF + b'FTT5' + CRLF + b'DP0,8500,11000' + CRLF +
            b'YCL0' + CRLF + b'YIWN2' + CRLF)


def flat(hdr, lines, tag=b'Line'):
    out = hdr + b'*B0' + CRLF + b'S' + CRLF
    for i in range(lines):
        out += (b'' if i == 0 else b'B' + CRLF) + trec(
            tag + b' %d' % (i + 1))
    return out + b'*E' + CRLF


def sections(hdr, secs):
    out = hdr + b'*B1' + CRLF + b'S' + CRLF + trec(b'The Title') + b'+' + CRLF
    for head, n in secs:
        out += b'SE0' + CRLF + trec(head)
        for i in range(n):
            out += b'B' + CRLF + trec(b'Text %d of %s' % (i + 1, head))
    return out + b'-' + CRLF + b'*E' + CRLF


def write(d, name, data):
    p = os.path.join(d, name)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'wb') as f:
        f.write(data)
    return p


def newdir(name):
    d = os.path.join(tmp, '%s%d' % (name, len(os.listdir(tmp))))
    os.makedirs(d)
    return d


def tex_of(doc):
    d = os.path.dirname(doc)
    out = os.path.join(d, 'x.tex')
    run([strprint, doc, '--export-latex', out])
    t = open(out).read() if os.path.exists(out) else ''
    # The line that names the file is not part of the comparison.
    return re.sub(r'^% Source document: .*$', '% Source document', t,
        flags=re.M)


def pages_of(doc, extra=()):
    """The text of each page of the print, as a list of lines."""
    pdf = doc[:-4] + '.pdf'
    run([strprint, doc, '-o', pdf, '--no-banner'] + list(extra))
    if not os.path.exists(pdf):
        return []
    n = 0
    for l in run(['pdfinfo', pdf])[1].split('\n'):
        if l.startswith('Pages:'):
            n = int(l.split()[1])
    out = []
    for p in range(1, n + 1):
        t = run(['pdftotext', '-f', str(p), '-l', str(p), '-layout', pdf,
            '-'])[1]
        out.append([' '.join(l.split()) for l in t.split('\n') if l.strip()])
    return out



sys.path.insert(0, here)
import tui
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))


def attr_doc(cases):
    """A document with one block for each case (mask, flags, word)."""
    out = header() + b'*B0' + CRLF + b'S' + CRLF
    for n, (m, f, word) in enumerate(cases):
        out += (b'B' + CRLF if n else b'') + trec(
            b'pre ' + b'\x12' + bytes([m, f]) + word + b'\x12\x00\x00' +
            b' post')
    return out + b'*E' + CRLF


CASES = [
    (0x01, 0x00, b'ZBOLD', '\\textbf{ZBOLD'),
    (0x04, 0x00, b'ZITAL', '\\textit{ZITAL'),
    (0x02, 0x00, b'ZUWRD', '\\uline{ZUWRD'),
    (0x00, 0x02, b'ZUALL', '\\uline{ZUALL'),
    (0x00, 0x04, b'ZDWRD', '\\uuline{ZDWRD'),
    (0x00, 0x10, b'ZDALL', '\\uuline{ZDALL'),
    (0x08, 0x00, b'ZSUBS', '\\textsubscript{ZSUBS'),
    (0x10, 0x00, b'ZSUPR', '\\textsuperscript{ZSUPR'),
    (0x20, 0x00, b'ZSTRK', '\\sout{ZSTRK'),
    (0x00, 0x08, b'ZSMCP', '\\textsc{ZSMCP'),
    (0x00, 0x82, b'ZSTAT', '\\uline{ZSTAT'),
]
PLAIN = [(0x40, 0x00, b'ZPRN1'), (0x00, 0x01, b'ZPRN2'),
         (0x40, 0x01, b'ZPRN3'), (0x00, 0x80, b'ZSTBT')]

plain_doc = write(newdir('a'), 'doc.doc', attr_doc([(0, 0, b'ZNONE')]))
t0 = tex_of(plain_doc)
check('a document with no attribute has none of the commands',
    'ZNONE' in t0 and not any(c[3].split('{')[0] + '{Z' in t0 for c in CASES),
    t0[-300:])
d = write(newdir('b'), 'doc.doc', attr_doc([c[:3] for c in CASES]))
t = tex_of(d)
for m, f, word, want in CASES:
    check('mask %02x flags %02x: the print has %s' % (m, f, want),
        want in t, t[t.find(word.decode()) - 60:][:100])
d = write(newdir('c'), 'doc.doc', attr_doc(PLAIN))
t = tex_of(d)
check('printer specific 1, 2 and both, and the state bit: plain text',
    all(w.decode() in t and '{' + w.decode() not in t for _, _, w in PLAIN),
    t[-400:])
tsup = tex_of(write(newdir('e'), 'doc.doc', attr_doc([(0x10, 0,
    b'ZSUPR')])))
check('mask 10 is raised and not underlined', '\\textsuperscript{ZSUPR' in tsup
    and re.search(r'\\uu?line\{[^}]*ZSUPR', tsup) is None, tsup[-300:])

# A document with note references: the original writes the mask 10 (raised)
# before the marker of a note reference.  The made document of the test has
# the same bytes: the superscript token comes before the note marker.
nb = open(os.path.join(here, 'data', 'note_complex.doc'), 'rb').read()
nd = write(newdir('s'), 'doc.doc', nb.replace(b'\x13', b'\x12\x10\x00\x13'))
ts = tex_of(nd)
t_before = tex_of(write(newdir('s2'), 'doc.doc', nb))
check('a note reference after the mask 10 prints raised and not underlined',
    nb.count(b'\x13') == 2 and
    ts.count('\\textsuperscript{') > t_before.count('\\textsuperscript{') and
    '\\uline{' not in ts,
    (ts.count('\\textsuperscript{'), t_before.count('\\textsuperscript{'),
     ts.count('\\uline{')))

# the editor writes 02 for underscore words
os.environ['STRATA_BUILD_TYPE'] = os.environ.get('STRATA_BUILD_TYPE', '')
ed = newdir('k')
base = os.path.join(here, 'data', 'plain_text.doc')


def edited(name, keys):
    p = os.path.join(ed, name)
    shutil.copy(base, p)
    before = open(p, 'rb').read()
    tui.session([stred, '-H', helpf, p], keys)
    return before, open(p, 'rb').read()


b0, b1 = edited('u.doc', ['C-u', 'text:abc', 'C-s', 'wait', 'wait'])
check('Ctrl-U: the file had no attribute token before, and has 12 02 00 after',
    b'\x12\x02' not in b0 and b'\x12\x10' not in b0 and
    b'\x12\x02\x00abc' in b1, b1[:200])
check('Ctrl-U does not write 12 10 00', b'\x12\x10' not in b1)
b0, b1 = edited('m.doc', ['A-F6', 'wait', 'DOWN', 'DOWN', 'ENTER', 'wait',
    'text:xyz', 'C-s', 'wait', 'wait'])
check('Set Attributes, Underscore Words: 12 02 00',
    b'\x12\x02' not in b0 and b'\x12\x02\x00xyz' in b1 and
    b'\x12\x10' not in b1, b1[:200])

# the editor reads the table: the status line names the attribute
for m, f, word, name in ((0x10, 0, b'ZSUPR', 'Super'), (0x08, 0, b'ZSUBS',
        'Sub'), (0x02, 0, b'ZUWRD', 'Under'), (0x00, 0x02, b'ZUALL', 'Under')):
    p = os.path.join(ed, 'r%02x%02x.doc' % (m, f))
    open(p, 'wb').write(flat(header(), 1).replace(b'Line 1', b'\x12' +
        bytes([m, f]) + word))
    s = tui.session([stred, '-H', helpf, p], ['END', 'wait', 'show'])
    check('mask %02x flags %02x: the status line shows Attr: %s' % (m, f, name),
        'Attr: ' + name in s[0].split('\n')[0], s[0].split('\n')[0])
s = tui.session([stred, '-H', helpf, base], ['END', 'wait', 'show'])
check('a document with no attribute says Attr: Normal',
    'Attr: Normal' in s[0].split('\n')[0], s[0].split('\n')[0])

shutil.rmtree(tmp)
print('attrtable: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
