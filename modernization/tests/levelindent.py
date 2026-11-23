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

"""The ruler unit and the indents: Indent to Next Level, Ruler and the block
indents BL and BR, measured on the print.

The words of the print are found with pdftotext -bbox (points; the left
margin of the test documents is 1 inch).  Indent to Next Level: the five cases
of docs/ORIGINAL-EDITOR-GAPS.txt (the unit is 10 per inch here, so that a unit
is 0.1 inch).  Ruler: a block with BL20 starts at 1 inch plus 20 units, in
Relative (1/80 of the text width), 10, 12 and 15 per inch.

    STRPRINT=... python3 -I -B tests/levelindent.py
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



def words(pdf, page=1):
    """The words of a page: a list of (text, xMin, xMax)."""
    r = run(['pdftotext', '-bbox', '-f', str(page), '-l', str(page), pdf, '-'])
    return [(m.group(3), float(m.group(1)), float(m.group(2))) for m in
        re.finditer(r'xMin="([0-9.]+)" yMin="[0-9.]+" xMax="([0-9.]+)" '
        r'yMax="[0-9.]+">([^<]*)</word>', r[1])]


def printed(data, name):
    d = write(newdir(name), 'doc.doc', data)
    pdf = d[:-4] + '.pdf'
    run([strprint, d, '-o', pdf, '--no-banner'])
    return pdf


def levels(records):
    h = header().replace(b'FHA1' + CRLF, b'FHA1' + CRLF + b'FU2' + CRLF +
        records, 1)
    out = h + b'*B1' + CRLF + b'S' + CRLF + b'+' + CRLF
    for lv in (1, 2, 3):
        out += b'SE0' + CRLF + trec(b'Head%d' % lv) + b'B' + CRLF + trec(
            b'Text%d' % lv)
        if lv < 3:
            out += b'+' + CRLF
    return out + b'-' + CRLF + b'-' + CRLF + b'-' + CRLF + b'*E' + CRLF


def xs(pdf):
    w = words(pdf)
    return {t: x for t, x, _ in w}


cases = [
    ('no record', b'', (0, 0)),
    ('FI9', b'FI9' + CRLF, (9, 18)),
    ('F1I5', b'F1I5' + CRLF, (5, 5)),
    ('F1I5 and F2I3', b'F1I5' + CRLF + b'F2I3' + CRLF, (5, 8)),
    ('F2I5 only', b'F2I5' + CRLF, (0, 5)),
]
for name, rec, (u2, u3) in cases:
    x = xs(printed(levels(rec), 'lv'))
    ok = ('Text2' in x and 'Text3' in x and 'Text1' in x)
    check('Indent to Next Level, %s: text of level 1 at the margin' % name,
        ok and abs(x['Text1'] - 72) < 1.5, x)
    check('Indent to Next Level, %s: level 2 is %d units (%.1f in) in' %
        (name, u2, u2 / 10.0), ok and abs(x['Text2'] - (72 + 7.2 * u2)) < 1.5,
        (x.get('Text2'), 72 + 7.2 * u2))
    check('Indent to Next Level, %s: level 3 is %d units in' % (name, u3),
        ok and abs(x['Text3'] - (72 + 7.2 * u3)) < 1.5,
        (x.get('Text3'), 72 + 7.2 * u3))

# the headline moves with its text (the tag stands at the start of the line)
x = xs(printed(levels(b'FI9' + CRLF), 'lh'))
check('the headline of level 2 is indented with its text', 'Head2' in x and
    abs(x['Head2'] - (72 + 7.2 * 9)) < 40, x)


# ---- Ruler ------------------------------------------------------------------
def blocks_doc(records, bl=b'BL20', br=b''):
    h = header().replace(b'FHA1' + CRLF, b'FHA1' + CRLF + records, 1)
    out = h + b'*B0' + CRLF + b'S' + CRLF
    out += bl + CRLF + trec(b'Marker')
    long = b' '.join(b'word%d' % i for i in range(120))
    out += b'B' + CRLF + (br + CRLF if br else b'') + trec(long)
    return out + b'*E' + CRLF


for name, rec, inch in (('Relative', b'', 6.5 / 80), ('10/inch', b'FU2' + CRLF,
        0.1), ('12/inch', b'FU3' + CRLF, 1 / 12.0),
        ('15/inch', b'FU4' + CRLF, 1 / 15.0)):
    x = xs(printed(blocks_doc(rec), 'ru'))
    want = 72 + 20 * inch * 72
    check('Ruler %s: a block with BL20 starts at %.1f points' % (name, want),
        'Marker' in x and abs(x['Marker'] - want) < 1.5,
        (x.get('Marker'), want))

# BR narrows the line by the same unit
def rightmost(records, br):
    pdf = printed(blocks_doc(records, b'BL0', br), 'br')
    return max(xm for t, x0, xm in words(pdf) if t.startswith('word'))

wide = rightmost(b'FU2' + CRLF, b'')
narrow = rightmost(b'FU2' + CRLF, b'BR10')
check('BR10 at 10/inch keeps the line one inch from the right margin',
    narrow < 72 + 468 - 72 + 2 and wide > narrow + 30, (wide, narrow))
wide = rightmost(b'FU4' + CRLF, b'')
narrow = rightmost(b'FU4' + CRLF, b'BR30')
check('BR30 at 15/inch narrows the line by two inches',
    narrow < 72 + 468 - 144 + 2 and wide > narrow + 60, (wide, narrow))

# a block indent wider than the text is limited to the text width
x = xs(printed(blocks_doc(b'FU2' + CRLF, b'BL999'), 'lim'))
check('a block indent wider than the text is limited to the text width',
    'Marker' in x and x['Marker'] <= 72 + 468 + 1, x.get('Marker'))

shutil.rmtree(tmp)
print('levelindent: %s' % ('all passed' if not fails else '%d FAILURES'
                          % fails))
sys.exit(1 if fails else 0)
