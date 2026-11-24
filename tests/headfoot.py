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

"""Headers and footers: the text that the page layouts hold is printed.

The documents are made here (the records are those of docs/DOC-FORMAT.txt:
P<n>HB<k> and P<n>FB<k> followed by a block of columns).  Checks: the LaTeX
text of a document with no header or footer text is the same with or without
the empty records; the text of one that has them (fancyhdr, left, centre and
right); and the PDF read with pdftotext: the First, Even and Odd layouts on
the right pages, the title page with none, the Section marker, and the
numbers that go on through the parts of an Include marker.

    STRPRINT=... python3 -I -B tests/headfoot.py
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


# ---- the text: no header or footer text ------------------------------------
a = write(newdir('a'), 'doc.doc', flat(header(), 5))
empty = [(b'PHB', hfblock()), (b'PFB', hfblock()),
         (b'P3FB1', hfblock()), (b'P4HB1', hfblock())]
b = write(newdir('b'), 'doc.doc', flat(header(empty), 5))
ta, tb = tex_of(a), tex_of(b)
check('no text in any header or footer: no fancyhdr, no page style',
    ta != '' and 'fancyhdr' not in ta and 'strsecmark' not in ta and
    'headsep' not in ta)
check('empty header and footer records: the same LaTeX text, byte for byte',
    ta == tb, [l for l in difflib.unified_diff(ta.split('\n'), tb.split('\n'),
    lineterm='', n=0)][:8])

# ---- the text: three parts --------------------------------------------------
c = write(newdir('c'), 'doc.doc', flat(header(
    [(b'PHB', hfblock(b'LEFTH', b'MIDH', b'RIGHTH')),
     (b'PFB', hfblock(b'', b'', b'Page ' + PAGE))]), 5))
tc = tex_of(c)
check('the header has its three parts (fancyhdr)',
    'usepackage{fancyhdr}' in tc and '\\fancyhead[L]{LEFTH}' in tc and
    '\\fancyhead[C]{MIDH}' in tc and '\\fancyhead[R]{RIGHTH}' in tc, tc[:50])
check('the footer: the Page Number marker is the page number, at the right',
    '\\fancyfoot[R]{Page \\thepage{}}' in tc)
check('the page style is set for the whole document',
    '\\pagestyle{strn}' in tc and '\\thispagestyle{strf}' in tc)

# ---- the pages: first, even and odd -----------------------------------------
lay = [(b'PHB', hfblock(b'', b'RUNHEAD', b'')),
       (b'P3FB1', hfblock(b'', b'FIRST ' + PAGE, b'')),
       (b'P4FB1', hfblock(b'EVEN ' + PAGE, b'', b'')),
       (b'P5FB1', hfblock(b'', b'', b'ODD ' + PAGE))]
d = write(newdir('d'), 'doc.doc', flat(header(lay), 230))
pg = pages_of(d)
check('the document has at least 4 pages', len(pg) >= 4, len(pg))
if len(pg) >= 4:
    check('page 1: the First layout (footer), the global header',
        pg[0][0] == 'RUNHEAD' and pg[0][-1] == 'FIRST 1', (pg[0][0],
        pg[0][-1]))
    check('page 2 is even: the Even footer on the left, the global header',
        pg[1][0] == 'RUNHEAD' and pg[1][-1] == 'EVEN 2', (pg[1][0],
        pg[1][-1]))
    check('page 3 is odd: the Odd footer on the right',
        pg[2][0] == 'RUNHEAD' and pg[2][-1] == 'ODD 3', (pg[2][0],
        pg[2][-1]))
    check('page 4 is even again', pg[3][-1] == 'EVEN 4', pg[3][-1])

# ---- the title page and the Section marker ----------------------------------
lay2 = [(b'PHB', hfblock(SECTION, b'', b'')),
        (b'PFB', hfblock(b'', b'', b'Foot ' + PAGE))]
e = write(newdir('e'), 'doc.doc', sections(header(lay2),
    [(b'Alpha', 150), (b'Beta', 150)]))
pg = pages_of(e)
check('structured document: title page and several pages', len(pg) >= 5,
    len(pg))
if len(pg) >= 5:
    check('the title page has no header and no footer (P1 records are empty)',
        pg[0][0] == 'The Title' and pg[0][-1] == 'The Title', pg[0])
    check('page 2: the Section marker prints the number and the headline',
        pg[1][0] == '1 Alpha' and pg[1][-1].startswith('Foot'), (pg[1][0],
        pg[1][-1]))
    first_beta = next((i for i, p in enumerate(pg) if '2 Beta' in p), None)
    check('the first page with the headline Beta has Beta in its header',
        first_beta is not None and pg[first_beta][0] == '2 Beta',
        [p[:3] for p in pg])
    check('the page before it still has 1 Alpha',
        first_beta is not None and pg[first_beta - 1][0] == '1 Alpha')
    check('the last page has 2 Beta in its header', pg[-1][0] == '2 Beta',
        pg[-1][0])

# ---- an Include marker: the numbers go on through the parts -----------------
f = newdir('f')
part = write(f, 'part.doc', flat(header(), 120, b'Part'))
master = write(f, 'master.doc', flat(header(
    [(b'PFB', hfblock(b'', b'Page ' + PAGE, b''))]), 3))
data_ = open(master, 'rb').read()
data_ = data_.replace(b'*E' + CRLF, b'B' + CRLF + trec(marker(16,
    [b'1part.doc\x00'])) + b'*E' + CRLF)
write(f, 'master.doc', data_)
pg = pages_of(os.path.join(f, 'master.doc'))
check('the master with a part: more than one page', len(pg) >= 2, len(pg))
feet = [p[-1] for p in pg]
check('every page of the master and its part has the foot, in order',
    feet == ['Page %d' % (i + 1) for i in range(len(pg))], feet)
check('the text of the part is on the pages',
    any('Part 120' in ' '.join(p) for p in pg))

shutil.rmtree(tmp)
print('headfoot: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
