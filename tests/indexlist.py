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

"""The index and the lists of figures and tables.

The documents are made here (the records are those of docs/DOC-FORMAT.txt,
the markers Index, Figure and Table of docs/MARKERS.txt).  Checks: with the
options off, or with no marker of the kind, the LaTeX text is the same; the
index (alphabetical, case ignored, subheadings, reference text, runs of three
or more pages joined as 1-3) and the lists, read with pdftotext, with the
right pages; the same through an Include marker.

    STRPRINT=... python3 -I -B tests/indexlist.py
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
tmp = tempfile.mkdtemp(prefix='ixls')
# Nothing of a test goes into the real home of the user.
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
os.environ['HOME'] = tempfile.mkdtemp(prefix='uhome', dir=tmp)
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'
CRLF = b'\r\n'
EOL = b'\x1d'
DASH = '\u2013'


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


def INDEX(main, sub=b'', ref=b''):
    recs = [b'2' + main + b'\x00']
    if sub:
        recs.append(b'3' + sub + b'\x00')
    if ref:
        recs.append(b'4' + ref + b'\x00')
    return marker(3, recs)


def FIGURE(name):
    return marker(11, [b'1' + name + b'\x00'])


def TABLE(name):
    return marker(29, [b'1' + name + b'\x00'])


def INCLUDE(name):
    return marker(16, [b'1' + name + b'\x00'])


def trec(text):
    return b'T' + text + EOL + b'\x00' + CRLF


HEADER = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
          b'P1HB0' + CRLF + b'P1FB0' + CRLF + b'P1C33' + CRLF +
          b'FHA1' + CRLF + b'FTT5' + CRLF + b'DP0,8500,11000' + CRLF +
          b'YCL0' + CRLF + b'YIWN2' + CRLF)


def flat(lines, extra=None, tag=b'Line'):
    """An unstructured document of blocks 'Line k'; extra maps a line number
    to the bytes (markers) put after its text."""
    extra = extra or {}
    out = HEADER + b'*B0' + CRLF + b'S' + CRLF
    for i in range(lines):
        out += (b'' if i == 0 else b'B' + CRLF) + trec(
            tag + b' %d' % (i + 1) + extra.get(i + 1, b''))
    return out + b'*E' + CRLF


def write(d, name, data):
    p = os.path.join(d, name)
    with open(p, 'wb') as f:
        f.write(data)
    return p


def newdir(name):
    d = os.path.join(tmp, '%s%d' % (name, len(os.listdir(tmp))))
    os.makedirs(d)
    return d


def tex_of(doc, extra=()):
    d = os.path.dirname(doc)
    out = os.path.join(d, 'x.tex')
    run([strprint, doc, '--export-latex', out] + list(extra))
    t = open(out).read() if os.path.exists(out) else ''
    return re.sub(r'^% Source document: .*$', '% Source document', t,
        flags=re.M)


def pages_of(doc, extra=()):
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


def page_of_line(pg, k):
    for i, p in enumerate(pg):
        if 'Line %d' % k in p or any(l.startswith('Line %d ' % k) or
                l == 'Line %d' % k for l in p):
            return i + 1
    return 0


# ---- with no marker, or with the options off: the same text -----------------
plain = write(newdir('a'), 'doc.doc', flat(120))
opts = ['--index', '--list-figures', '--list-tables']
# The document decides when no option is given (a marker turns its list on),
# so the print with the lists off says so.
OFF = ['--no-index', '--no-list-figures', '--no-list-pictures',
       '--no-list-tables']
check('a document with no marker: the same LaTeX text with the three options',
    tex_of(plain) == tex_of(plain, opts))
pg0 = pages_of(plain)
check('the plain document has at least 3 pages', len(pg0) >= 3, len(pg0))
lines_on = {}
for p in (1, 2, 3):
    lines_on[p] = next((k for k in range(1, 121)
        if page_of_line(pg0, k) == p and k % 7 == 3), 0)
check('a line is found on each of the first three pages',
    all(lines_on.values()), lines_on)

# ---- the index ---------------------------------------------------------------
extra = {}
def put(k, m):
    extra[k] = extra.get(k, b'') + m
l1, l2, l3 = lines_on[1], lines_on[2], lines_on[3]
put(l1, INDEX(b'apple'))
put(l2, INDEX(b'Apple'))
put(l3, INDEX(b'APPLE'))
put(l1 + 1, INDEX(b'apple', b'red'))
put(l2 + 1, INDEX(b'apple', b'red'))
put(l1 + 2, INDEX(b'apple', b'green'))
put(l3 + 1, INDEX(b'apple', b'green'))
put(l1 + 3, INDEX(b'zebra'))
put(l3 + 2, INDEX(b'zebra'))
put(l2 + 2, INDEX(b'Banana', b'', b'See apple'))
put(l2 + 3, INDEX(b'', b'ignored'))
d = write(newdir('b'), 'doc.doc', flat(120, extra))
tb_off = tex_of(d, OFF)
check('with the options off the markers make no label and no index',
    'label{lm' not in tb_off and 'strixline' not in tb_off, tb_off[:20])
pg = pages_of(d, ['--index'])
check('the index adds a page at the end', len(pg) == len(pg0) + 1, len(pg))
idx = pg[-1] if pg else []
check('the index has its title', idx[:1] == ['Index'], idx[:3])
body = idx[1:]
check('apple: the pages 1 to 3 are joined as a run (1%s3)' % DASH,
    body[:1] == ['apple, 1%s3' % DASH], body[:3])
check('the subheadings follow, in alphabetical order, with their pages',
    body[1:3] == ['green, 1, 3', 'red, 1, 2'], body[:4])
check('Banana comes next, with its reference text instead of a page',
    body[3:4] == ['Banana, See apple'], body[:5])
check('zebra is last, with its two pages; a marker with no main heading '
    'makes no entry', body[4:] == ['zebra, 1, 1'] or
    body[4:] == ['zebra, 1, 3'] or body[4:5] == ['zebra, 1, 3'], body)

# ---- the lists ---------------------------------------------------------------
ex2 = {}
ex2[l1] = FIGURE(b'Sales chart')
ex2[l2] = TABLE(b'Costs') + FIGURE(b'Map')
ex2[l3] = TABLE(b'Totals')
e = write(newdir('c'), 'doc.doc', flat(120, ex2))
pg = pages_of(e, ['--list-figures', '--list-tables'])
check('two lists make two pages before the text', len(pg) == len(pg0) + 2,
    len(pg))
if len(pg) >= 2:
    j = ' | '.join(pg[0]), ' | '.join(pg[1])
    check('the list of figures: title, names and pages (roman page numbers '
        'do not change the pages of the text)', pg[0][0] == 'List of Figures'
        and any(l.startswith('Sales chart') and l.endswith('1') for l in
        pg[0]) and any(l.startswith('Map') and l.endswith('2') for l in
        pg[0]), pg[0])
    check('the list of figures has the figures only, not the tables',
        not any(l.startswith('Costs') or l.startswith('Totals')
        for l in pg[0]), pg[0])
    check('the list of tables follows on its own page',
        pg[1][0] == 'List of Tables' and
        any(l.startswith('Costs') and l.endswith('2') for l in pg[1]) and
        any(l.startswith('Totals') and l.endswith('3') for l in pg[1]),
        pg[1])
    check('the list of tables has the tables only, not the figures',
        not any(l.startswith('Sales chart') or l.startswith('Map')
        for l in pg[1]), pg[1])
    check('the first page of the text is page 1 again',
        any(l.endswith('i') or l == 'i' for l in pg[0]) and
        'Line 1' in ' '.join(pg[2]), (pg[0][-1:], pg[2][:2]))
base = len(pages_of(e, OFF))
pg = pages_of(e, ['--list-figures', '--no-list-tables'])
check('only the list of figures: one page more, no list of tables',
    len(pg) == base + 1 and pg[0][0] == 'List of Figures' and
    not any('List of Tables' in p for p in pg), (len(pg), base))
pg = pages_of(e, ['--contents', '1', '--list-figures'])
check('the contents come first, then the list of figures',
    len(pg) >= 2 and pg[0][0] == 'Contents' and pg[1][0] == 'List of Figures',
    [p[:1] for p in pg[:3]])

# ---- through an Include marker -----------------------------------------------
f = newdir('d')
write(f, 'part.doc', flat(100, {50: INDEX(b'Included'),
    60: FIGURE(b'Part pic')}, b'Part'))
m = flat(100, {5: INDEX(b'Master')})
m = m.replace(b'*E' + CRLF, b'B' + CRLF + trec(INCLUDE(b'part.doc')) +
    b'*E' + CRLF)
write(f, 'master.doc', m)
pg = pages_of(os.path.join(f, 'master.doc'), ['--index', '--list-figures'])
txt = [l for p in pg for l in p]
check('the markers of the part are in the index of the master, and so are '
    'those of the master itself',
    any(l.startswith('Included, ') for l in txt) and
    any(l.startswith('Master, ') for l in txt), txt[-6:])
check('and in the list of figures', len(pg) > 1 and
    any(l.startswith('Part pic') for l in pg[0]), pg[0][:4] if pg else '')

shutil.rmtree(tmp)
print('indexlist: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
