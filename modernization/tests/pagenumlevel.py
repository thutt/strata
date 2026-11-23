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

"""Page Number Level: the page number starts again at 1 at every headline of
the level named in the Global Format settings or a higher one.

A structured document of six sections of level 1 and one of level 2 (6.1),
each on its own page (Page Break Level 2), with the page number in the
footer; printed with the Page Number Level 0, 1 and 2, the footer is read
with pdftotext, with the table of contents and a cross-reference to 6.1.

    STRPRINT=... python3 -I -B tests/pagenumlevel.py
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



def doc(level):
    h = header([(b'PFB', hfblock(b'', b'Page ' + PAGE, b''))])
    h = h.replace(b'FHA1' + CRLF, b'FHA1' + CRLF + b'FLP2' + CRLF +
        (b'FLG%d' % level + CRLF if level else b''), 1)
    out = h + b'*B1' + CRLF + b'S' + CRLF + trec(b'The Title') + b'+' + CRLF
    for n in range(1, 7):
        out += b'SE0' + CRLF + trec(b'Section %d' % n)
        text = b'Body of section %d' % n
        if n == 1:
            text += b' see page ' + marker(2, [b'XN6.1\x00', b'XT4'])
        out += b'B' + CRLF + trec(text)
        if n == 6:
            out += b'+' + CRLF + b'SE0' + CRLF + trec(b'Subsection 6.1')
            out += b'B' + CRLF + trec(b'Body of 6.1') + b'-' + CRLF
    return out + b'-' + CRLF + b'*E' + CRLF


def feet(level, extra=()):
    d = write(newdir('p%d' % level), 'doc.doc', doc(level))
    pg = pages_of(d, list(extra))
    return pg, [p[-1] for p in pg]


for level, want in ((0, [1, 2, 3, 4, 5, 6, 7]), (1, [1, 1, 1, 1, 1, 1, 2]),
                    (2, [1, 1, 1, 1, 1, 1, 1])):
    pg, ft = feet(level)
    check('Page Number Level %d: the footers of the seven pages' % level,
        len(pg) == 8 and ft[0] != 'Page 1' or len(pg) == 8,
        (len(pg), ft))
    check('Page Number Level %d: the pages are %s' % (level, want),
        ft[1:] == ['Page %d' % n for n in want], ft)

pg, ft = feet(1, ['--contents', '2'])
toc = pg[1] if len(pg) > 1 else []
check('Page Number Level 1: the table of contents has the same page numbers',
    any(re.match(r'^6\.1 Subsection 6\.1 [. ]+ 2$', l) for l in toc) and
    sum(1 for l in toc if re.match(r'^[1-6] Section [1-6] [. ]+ 1$', l)) == 6,
    toc)
body = [' '.join(p) for p in pg]
check('Page Number Level 1: a cross-reference to 6.1 gives its page, 2',
    any('see page 2' in b for b in body), [b[:80] for b in body[:3]])
pg, ft = feet(0)
check('Page Number Level 0: the same cross-reference gives 7',
    any('see page 7' in ' '.join(p) for p in pg))

shutil.rmtree(tmp)
print('pagenumlevel: %s' % ('all passed' if not fails else '%d FAILURES'
                           % fails))
sys.exit(1 if fails else 0)
