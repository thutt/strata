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

"""Winding Columns: the text of a block is set in 1 to 9 snaking columns.

The LaTeX text (multicol, the gutter and the rule) and the print read with
pdftotext -bbox: with 2 and 3 columns the lines are about a half and a third
as wide, the text flows into the next column, a headline keeps the full width,
and a block of its own (BWN) or a level (F1WN) can set the number.

    STRPRINT=... python3 -I -B tests/winding.py
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
    r = run(['pdftotext', '-bbox', '-f', str(page), '-l', str(page), pdf, '-'])
    return [(m.group(3), float(m.group(1)), float(m.group(2))) for m in
        re.finditer(r'xMin="([0-9.]+)" yMin="[0-9.]+" xMax="([0-9.]+)" '
        r'yMax="[0-9.]+">([^<]*)</word>', r[1])]


def wdoc(records, blockrec=b''):
    h = header().replace(b'FHA1' + CRLF, b'FHA1' + CRLF + records, 1)
    out = h + b'*B1' + CRLF + b'S' + CRLF + b'+' + CRLF
    out += b'SE0' + CRLF + trec(b'A headline that is quite long and goes on and on across the page')
    for p in range(3):
        long = b' '.join(b'w%d_%d' % (p, i) for i in range(90))
        out += b'B' + CRLF + (blockrec if p == 0 else b'') + (
            CRLF if blockrec and p == 0 else b'') + trec(long)
    return out + b'-' + CRLF + b'-' + CRLF + b'*E' + CRLF


def printed(data, name):
    d = write(newdir(name), 'doc.doc', data)
    pdf = d[:-4] + '.pdf'
    run([strprint, d, '-o', pdf, '--no-banner'])
    return d, pdf


def widest(pdf):
    ws = [(t, x0, x1) for t, x0, x1 in words(pdf) if t.startswith('w')]
    return max(x1 - x0 for t, x0, x1 in ws), max(x1 for t, x0, x1 in ws), ws


none, _ = None, None
d0, pdf0 = printed(wdoc(b''), 'w0')
t0 = tex_of(d0)
check('no winding columns: no multicol in the text',
    'multicol' not in t0 and t0 != '')
for n in (2, 3):
    d, pdf = printed(wdoc(b'FWN%d' % n + CRLF), 'w%d' % n)
    t = tex_of(d)
    check('%d columns: the LaTeX text has multicols{%d}' % (n, n),
        '\\begin{multicols}{%d}' % n in t and 'usepackage{multicol}' in t)
    ws = words(pdf)
    text = [w for w in ws if w[0].startswith('w')]
    head = [w for w in ws if w[0] in ('headline', 'quite', 'long', 'across',
        'page', 'goes')]
    left = [w for w in text if w[1] < 72 + 468 / n - 5]
    check('%d columns: the words fill %d columns, about 1/%d of the width '
        'each' % (n, n, n), len({round(w[1] / 20) for w in text}) >= n and
        max(w[2] - w[1] for w in text) < 468 / n, (len(text),))
    check('%d columns: the text goes on into the next column' % n,
        any(w[1] > 72 + 468 * (n - 1) / n - 8 for w in text))
    check('%d columns: the headline has the full width' % n,
        head and max(w[2] for w in head) > 72 + 468 / n + 20, head)

# a block of its own, a level, the gutter and the rule
d, pdf = printed(wdoc(b'', b'BWN2'), 'wb')
check('the number of a block (BWN2) makes columns for that block only',
    '\\begin{multicols}{2}' in tex_of(d))
d, pdf = printed(wdoc(b'F1WN2' + CRLF), 'wl')
check('the number of a level (F1WN2) makes columns for its sections',
    '\\begin{multicols}{2}' in tex_of(d))
t = tex_of(printed(wdoc(b'FWN2' + CRLF + b'FWG500' + CRLF + b'FWR1' + CRLF),
    'wg')[0])
check('the gutter (FWG500) is the column separation',
    '\\setlength{\\columnsep}{0.500in}' in t, t[:10])
check('a single rule (FWR1) is a thin rule between the columns',
    '\\setlength{\\columnseprule}{0.4pt}' in t)
t = tex_of(printed(wdoc(b'FWN2' + CRLF + b'FWR3' + CRLF), 'ww')[0])
check('a wide rule (FWR3) is a thick one',
    '\\setlength{\\columnseprule}{2pt}' in t)
t = tex_of(printed(wdoc(b'FWN2' + CRLF), 'wd')[0])
check('the defaults: a gutter of a quarter inch and no rule',
    '{\\columnsep}{0.250in}' in t and '{\\columnseprule}{0pt}' in t)

# ---- the Named Block Format: its own number, gutter and rule -----------------
def named_doc(defn, blockrec=b'BGcols\x00', glob=b''):
    """A document with a named format `cols` (the records defn) and a block
    that uses it; glob are Global Format records."""
    h = header().replace(b'FHA1' + CRLF, b'FHA1' + CRLF + glob + b'FNcols\x00'
        + CRLF + defn + b'FN\x00' + CRLF, 1)
    out = h + b'*B1' + CRLF + b'S' + CRLF + b'+' + CRLF
    out += b'SE0' + CRLF + trec(b'A headline that is quite long and goes on')
    for p in range(3):
        long = b' '.join(b'w%d_%d' % (p, i) for i in range(90))
        out += b'B' + CRLF + (blockrec + CRLF if p == 0 and blockrec else
            b'') + trec(long)
    return out + b'-' + CRLF + b'*E' + CRLF


def pixels(pdf, dpi=100):
    """The first page as grey values: (width, height, bytes)."""
    out = os.path.join(os.path.dirname(pdf), 'pg')
    run(['pdftoppm', '-r', str(dpi), '-f', '1', '-l', '1', '-gray', pdf, out])
    name = [f for f in os.listdir(os.path.dirname(pdf)) if f.startswith('pg')
        and f.endswith('.pgm')]
    if not name:
        return 0, 0, b''
    raw = open(os.path.join(os.path.dirname(pdf), name[0]), 'rb').read()
    m = re.match(rb'P5\s+(\d+)\s+(\d+)\s+255\s', raw)
    w, h = int(m.group(1)), int(m.group(2))
    return w, h, raw[m.end():]


def rule_columns(pdf):
    """The x of the vertical lines of the page: columns of pixels that are
    dark for more than 40 rows (0.4 inch) in a row; no letter has such a run."""
    w, h, px = pixels(pdf)
    found = []
    for x in range(w):
        run_len = best = 0
        for y in range(h):
            if px[y * w + x] < 140:
                run_len += 1
                best = max(best, run_len)
            else:
                run_len = 0
        if best > 40:
            found.append(x)
    return found


d, pdf = printed(named_doc(b'BWN2' + CRLF + b'BWG500' + CRLF + b'BWR1' + CRLF),
    'nm1')
t = tex_of(d)
check('named format: BWN2 makes two columns for the block that uses it',
    '\\begin{multicols}{2}' in t, t[:10])
check('named format: BWG500 is a gutter of half an inch',
    '\\setlength{\\columnsep}{0.500in}' in t)
check('named format: BWR1 is a thin rule between the columns',
    '\\setlength{\\columnseprule}{0.4pt}' in t)
ws = [w for w in words(pdf) if w[0].startswith('w0_')]
left = [w for w in ws if w[1] < 72 + 468 / 2 - 5]
right = [w for w in ws if w[1] >= 72 + 468 / 2 - 5]
check('named format: the text of the block is in two columns', left and right
    and max(w[2] for w in left) < min(w[1] for w in right), (len(left),
    len(right)))
gap = min(w[1] for w in right) - max(w[2] for w in left)
check('named format: the gutter between the columns is about half an inch '
    '(%.0f points)' % gap, gap >= 36, gap)
xs = rule_columns(pdf)
mid = [x for x in xs if max(w[2] for w in left) * 100 / 72 < x <
    min(w[1] for w in right) * 100 / 72]
check('named format: a vertical line is drawn between the columns of the '
    'page (pixels)', len(mid) >= 1, xs)
d, pdf = printed(named_doc(b'BWN2' + CRLF + b'BWG500' + CRLF), 'nm2')
check('named format: with no BWR there is no vertical line in the page',
    rule_columns(pdf) == [], rule_columns(pdf))
d, pdf = printed(named_doc(b'BWN2' + CRLF + b'BWG500' + CRLF + b'BWR0' + CRLF,
    glob=b'FWR1' + CRLF), 'nm3')
check('named format: BWR0 of a block turns the rule of the document off',
    '\\setlength{\\columnseprule}{0pt}' in tex_of(d) and
    rule_columns(pdf) == [], rule_columns(pdf))
d, pdf = printed(named_doc(b'BWN2' + CRLF + b'BWR2' + CRLF, glob=b'FWG100' +
    CRLF), 'nm4')
t = tex_of(d)
check('named format: the gutter of the document is used when the block has '
    'none, and the rule of the block is double', '{\\columnsep}{0.100in}' in t
    and '{\\columnseprule}{1.2pt}' in t, t[:10])
d, pdf = printed(wdoc(b'FWN2' + CRLF + b'FWR1' + CRLF), 'gr1')
check('the rule of the document (FWR1) is a vertical line in the page',
    len(rule_columns(pdf)) >= 1, rule_columns(pdf))
d, pdf = printed(wdoc(b'FWN2' + CRLF), 'gr0')
check('no FWR: no vertical line in the page', rule_columns(pdf) == [],
    rule_columns(pdf))
d2 = write(newdir('rt'), 'doc.doc', named_doc(b'BWN2' + CRLF + b'BWG500' + CRLF
    + b'BWR1' + CRLF))
rt = os.path.join(os.path.dirname(strprint), 'roundtrip')
if os.path.exists(rt):
    r = run([rt, d2])
    check('named format: the loader keeps BWN, BWG and BWR (round trip)',
        r[0] == 0, r)

shutil.rmtree(tmp)
print('winding: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
