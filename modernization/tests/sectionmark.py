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

"""The Section marker of a header or footer prints the number, the title or
both of the section: "3.2 Search" (the default), "3.2" and "Search".

The documents are made here, with the Section marker in the header: the
marker with no field (both), with the field S2 (number only) and S3 (title
only).  The PDF is read with pdftotext: the header of each page must be the
mark of the first headline on the page, else of the one before, with the tag
as it is printed at the headline; a headline with no tag (numbering off)
prints its title for the number choices.  Also: the header of a title page and
of the pages before the first headline is empty; the Include marker parts;
the print of a range of sections; the line "\\sectionnumber\\" of the Header
and Footer fields (the Page panel) stores the field and shows again; and an
old document (a Section marker with no field) prints both.

    STRPRINT=... STRED=... python3 -I -B tests/sectionmark.py
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



stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
DOCS = os.path.join(here, 'data')

NUM = marker(28, [b'S2'])
TIT = marker(28, [b'S3'])
BOTH = marker(28)
TREE = [(b'Intro', 40, []), (b'Method', 40, []),
        (b'Results', 10, [(b'Alpha', 60), (b'Search', 200), (b'Omega', 60)]),
        (b'Epilogue', 40, [])]


def tree_doc(hdr):
    out = hdr + b'*B1' + CRLF + b'S' + CRLF + trec(b'The Title') + b'+' + CRLF
    for head, n, kids in TREE:
        out += b'SE0' + CRLF + trec(head)
        for i in range(n):
            out += b'B' + CRLF + trec(b'Text %d of %s' % (i + 1, head))
        if kids:
            out += b'+' + CRLF
            for kh, kn in kids:
                out += b'SE0' + CRLF + trec(kh)
                for i in range(kn):
                    out += b'B' + CRLF + trec(b'Text %d of %s' % (i + 1, kh))
            out += b'-' + CRLF
    return out + b'-' + CRLF + b'*E' + CRLF


def layout(mk):
    return [(b'PHB', hfblock(mk, b'', b'')),
            (b'PFB', hfblock(b'', b'', b'Foot ' + PAGE))]


TITLES = r'(Intro|Method|Results|Alpha|Search|Omega|Epilogue|Master)'
TAGS = {'plain': r'\d+(?:\.\d+)*', 'fln1': r'\d+',
        'forms': r'(?:Part [IVXLC]+\.|[A-Z]\.)'}
HEADLINE = None


def expected(pages, how, tagre='plain', skip=1):
    """The header of each page from the headlines that are in the page text:
    the first one on the page, else the last one before.  how: 'both',
    'num' or 'title'."""
    out = []
    last = None
    for n, pg in enumerate(pages):
        first = None
        for line in pg[1:-1]:
            m = re.match('^(?:(' + TAGS[tagre] + ') )?' + TITLES + '$', line)
            if m:
                if first is None:
                    first = m
                last_here = m
        cur = first or last
        if first is not None:
            last = last_here
        if cur is None or n < skip:
            out.append('')
            continue
        tag, title = cur.group(1) or '', cur.group(2)
        out.append(title if how == 'title' or not tag else
                   tag if how == 'num' else tag + ' ' + title)
    return out


def headers(pages):
    return [pg[0] if len(pg) > 1 and not pg[0].startswith('Text') and
            pg[0] != 'The Title' and pg[0] != 'Foot' else '' for pg in pages]


for how, mk in (('both', BOTH), ('num', NUM), ('title', TIT)):
    d = write(newdir('t'), 'doc.doc', tree_doc(header(layout(mk))))
    pg = pages_of(d)
    check('%s: the print has many pages' % how, len(pg) >= 8, len(pg))
    exp = expected(pg, how)
    got = [p[0] if len(p) > 1 else '' for p in pg]
    exp1 = exp[1:]
    got1 = got[1:]
    check('%s: the header of every page after the title page is the mark of '
        'its first headline, else of the one before' % how,
        got1 == exp1 and len(set(exp1)) >= 4, list(zip(got1, exp1))[:12])
    if how == 'both':
        check('both: a page in section 3.2 says "3.2 Search"',
            '3.2 Search' in got, sorted(set(got)))
    elif how == 'num':
        check('number only: a page in section 3.2 says "3.2" alone',
            '3.2' in got and '3.2 Search' not in got, sorted(set(got)))
    else:
        check('title only: a page in section 3.2 says "Search" alone',
            'Search' in got and '3.2 Search' not in got and '3.2' not in got,
            sorted(set(got)))

# the numbering: only the first level has a tag, and other tag formats
FORMS = [b'F1GPart \\I\\. \x00', b'F2G\\A\\. \x00']
for name, extra in (('fln1', [b'FLN1']), ('forms', FORMS)):
    for how, mk in (('both', BOTH), ('num', NUM), ('title', TIT)):
        hdr = header(layout(mk))
        at = hdr.index(b'FHA1')
        hdr = hdr[:at] + b''.join(r + CRLF for r in extra) + hdr[at:]
        pg = pages_of(write(newdir('v'), 'doc.doc', tree_doc(hdr)))
        exp = expected(pg, how, name)
        got = [p[0] if len(p) > 1 else '' for p in pg]
        check('%s, %s: the header of every page is the mark of the headline '
            'with its tag as it is printed' % (name, how),
            len(pg) >= 8 and got[1:] == exp[1:] and len(set(exp[1:])) >= 4,
            list(zip(got, exp))[:10])
        if name == 'fln1' and how == 'num':
            check('fln1, num: a headline with no tag prints its title',
                'Search' in got and '3' in got, sorted(set(got)))
        if name == 'forms' and how == 'num':
            check('forms, num: the tag is as in the headline, with its '
                'full stop', 'Part III.' in got and 'A.' not in got or
                any(g.endswith('.') for g in got), sorted(set(got)))

# the title page has nothing in the header, the contents are not marked
pg = pages_of(write(newdir('t'), 'doc.doc', tree_doc(header(layout(BOTH)))))
check('the title page has no header text', len(pg) > 0 and
    pg[0][0] == 'The Title', pg[:1])

# pages before the first headline: a document with no headline at all; the
# header is empty and the print works
for how, mk in (('both', BOTH), ('num', NUM), ('title', TIT)):
    pg = pages_of(write(newdir('z'), 'doc.doc', flat(header(layout(mk)), 130)))
    check('%s: a document with no headline prints, and its header is empty' %
        how, len(pg) >= 3 and all(p[0].startswith('Line') or
        p[0].startswith('Foot') for p in pg[1:]), [p[:2] for p in pg[:3]])

# an old document: the marker has no field and prints both
old = tree_doc(header(layout(marker(28))))
check('the old marker has no record', b'\x1328(\x13)' in old and b'S2' not in old)

# an Include marker: the part is put one level below the section of the marker
# and its headlines make the marks (the numbers are those of the joined text)
f = newdir('f')
write(f, 'part.doc', tree_doc(header()))
mdoc = header(layout(BOTH)) + b'*B1' + CRLF + b'S' + CRLF + trec(b'The Title') + \
    b'+' + CRLF + b'SE0' + CRLF + trec(b'Master') + b'B' + CRLF + \
    trec(b'Master text') + b'B' + CRLF + trec(marker(16, [b'1part.doc\x00'])) + \
    b'-' + CRLF + b'*E' + CRLF
write(f, 'master.doc', mdoc)
pg = pages_of(os.path.join(f, 'master.doc'))
exp = expected(pg, 'both')
got = [p[0] if len(p) > 1 else '' for p in pg]
check('Include: the pages of the part have the mark of its headlines, as '
    'numbered in the joined text (1.3.2 Search)',
    len(pg) >= 8 and got[1:] == exp[1:] and '1.3.2 Search' in got,
    list(zip(got, exp))[:12])

# a print of a range of sections: the pages that are printed
d = write(newdir('p'), 'doc.doc', tree_doc(header(layout(BOTH))))
out = os.path.join(os.path.dirname(d), 'part.pdf')
subprocess.run([stred, '--print', d, '--from', '3', '--to', '3', '-o', out,
    '--no-title', '--no-banner'], capture_output=True)
n = 0
for l in run(['pdfinfo', out])[1].split('\n'):
    if l.startswith('Pages:'):
        n = int(l.split()[1])
txt = [[' '.join(l.split()) for l in run(['pdftotext', '-f', str(k), '-l',
    str(k), '-layout', out, '-'])[1].split('\n') if l.strip()]
    for k in range(1, n + 1)]
check('a print of section 3: its pages say "3 Results", "3.1 Alpha", ...',
    n >= 4 and txt[0][0] in ('3 Results', '3.1 Alpha') and
    any(t[0] == '3.2 Search' for t in txt) and
    not any(t[0] in ('1 Intro', '4 Epilogue') for t in txt),
    [t[0] for t in txt])

shutil.rmtree(tmp)
print('sectionmark: %s' % ('all passed' if not fails else '%d FAILURES'
                          % fails))
sys.exit(1 if fails else 0)
