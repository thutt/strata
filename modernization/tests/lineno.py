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

"""Line numbering (the records ZM, ZE, ZS, ZZ, ZY, ZF, ZD and ZQ of the Line
Numbering panel of Global, Format).  A document that has none of them is
written as before (no lineno in the LaTeX, no number on a page); with them the
Printer numbers the lines through the package lineno: pagewise and running
numbers, a number on every n-th line, the side (left, right, outer, inner),
the format and the distance of the number.  The numbers are read from the PDF
with pdftotext -bbox: their place in the margin, their size and their font
(it is not the font of the text).  The preview (the DVI of the same
typesetting, shown by the Viewer) has the same numbers as the PDF, and an
exported LaTeX file compiles by itself to the same numbers."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
data = os.path.join(here, 'data')
build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
    'strprint'))
for tool in ('pdflatex', 'latex', 'dvipdfmx', 'pdftotext', 'pdfinfo',
             'pdffonts'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='lineno')
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def run(cmd, cwd=None):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
        errors='replace')
    return r.returncode, r.stdout, r.stderr


def variant(src, name, records):
    """A copy of a test document with global F records added after the
    revision record."""
    raw = open(os.path.join(data, src), 'rb').read()
    m = re.search(rb'MR[0-9]+\r\n', raw)
    at = m.end() if m else raw.index(b'\r\n') + 2
    extra = b''.join(b'F' + r.encode() + b'\r\n' for r in records)
    path = os.path.join(tmp, name + '.doc')
    open(path, 'wb').write(raw[:at] + extra + raw[at:])
    return path


def pdf_of(doc, name, extra=()):
    out = os.path.join(tmp, name + '.pdf')
    rc, so, se = run([strprint, doc, '-o', out, '--docdir', data] +
                     list(extra))
    return out if os.path.exists(out) else None, rc, se


def tex_of(doc, name):
    out = os.path.join(tmp, name + '.tex')
    run([strprint, doc, '--tex-only', '-o', out, '--docdir', data])
    return open(out).read() if os.path.exists(out) else ''


def pages_of(pdf):
    rc, out, err = run(['pdfinfo', pdf])
    m = re.search(r'Pages:\s+(\d+)', out)
    return int(m.group(1)) if m else -1


def margin_words(pdf):
    """For every page the words that stand in the left margin (right end
    before 72 pt) or in the right margin (left end after 540 pt), as tuples
    (left x, top y, height, text), in the order of their place."""
    rc, out, err = run(['pdftotext', '-bbox', pdf, '-'])
    result = []
    for page in out.split('<page ')[1:]:
        found = []
        for m in re.finditer(r'<word xMin="([\d.]+)" yMin="([\d.]+)" '
                             r'xMax="([\d.]+)" yMax="([\d.]+)">(.*?)</word>',
                             page):
            x, y, x2, y2 = (float(v) for v in m.groups()[:4])
            if x2 < 72 or x > 540:
                found.append((x, y, y2 - y, m.group(5)))
        result.append(sorted(found, key=lambda w: w[1]))
    return result


def body_height(pdf):
    """The height of a word of the text (the first word of the first body
    line that is not in a margin)."""
    rc, out, err = run(['pdftotext', '-bbox', pdf, '-'])
    for m in re.finditer(r'<word xMin="([\d.]+)" yMin="([\d.]+)" '
                         r'xMax="([\d.]+)" yMax="([\d.]+)">(.*?)</word>',
                         out):
        x, y, x2, y2 = (float(v) for v in m.groups()[:4])
        if 72 <= x <= 540 and m.group(5) == 'PARAGRAPH.':
            return y2 - y
    return 0.0


def numbers(pdf):
    """For every page the list of the numbers in the margins as strings."""
    return [[w[3] for w in page] for page in margin_words(pdf)]


def ints(page):
    return [int(w) for w in page if w.isdigit()]


doc0 = os.path.join(data, 'print_lineno.doc')
pdf0, rc0, err0 = pdf_of(doc0, 'plain')
pages0 = pages_of(pdf0)
tex0 = tex_of(doc0, 'plain')
check('without the setting there is no lineno in the LaTeX',
      '{lineno}' not in tex0 and '\\linenumbers' not in tex0, tex0[:300])
check('without the setting no page has a number in its margin',
      all(not p for p in numbers(pdf0)), numbers(pdf0)[:2])

# Pagewise: the count starts again at 1 on each page.
d1 = variant('print_lineno.doc', 'pagewise', ['ZM1'])
pdf1, rc1, err1 = pdf_of(d1, 'pagewise')
n1 = [ints(p) for p in numbers(pdf1)]
check('pagewise: the page count is the one of the document without',
      pages_of(pdf1) == pages0, (pages_of(pdf1), pages0))
check('pagewise: every page starts at 1 and counts up by one',
      all(p and p == list(range(1, len(p) + 1)) for p in n1), n1)
check('pagewise: the numbers are in the left margin',
      all(w[0] < 72 for p in margin_words(pdf1) for w in p),
      margin_words(pdf1)[0][:2])
tex1 = tex_of(d1, 'pagewise')
check('pagewise: the LaTeX uses lineno with the pagewise option',
      '\\usepackage[pagewise,sep=0.15in]{lineno}' in tex1 and
      '\\linenumbers' in tex1, tex1[tex1.find('lineno') - 40:][:200])
rc, fonts, err = run(['pdffonts', pdf1])
rc, fonts0, err = run(['pdffonts', pdf0])
check('pagewise: the numbers use a font that the text does not',
      len(fonts.split('\n')) > len(fonts0.split('\n')) and
      re.search(r'lmss|LMSans|LMSS', fonts) is not None, fonts)
rows = margin_words(pdf1)[0]
check('pagewise: a number is smaller than a word of the text',
      rows and rows[0][2] < 0.8 * body_height(pdf1),
      (rows[0][2] if rows else None, body_height(pdf1)))

# Running: one count through the whole text.
d2 = variant('print_lineno.doc', 'running', ['ZM2'])
pdf2, rc2, err2 = pdf_of(d2, 'running')
n2 = [ints(p) for p in numbers(pdf2)]
flat = [v for p in n2 for v in p]
check('running: the count goes on over the pages', flat ==
      list(range(1, len(flat) + 1)) and n2[1][0] == n2[0][-1] + 1, n2)
check('running: the page count is the same', pages_of(pdf2) == pages0)

# Each Section: running that starts again at 1 on each section of level 1.
d3 = variant('print_lineno.doc', 'section', ['ZM3'])
pdf3, rc3, err3 = pdf_of(d3, 'section')
n3 = [ints(p) for p in numbers(pdf3)]
starts = sum(1 for p in n3 for i, v in enumerate(p) if v == 1)
check('each section: the count starts at 1 at the start of the document and '
      'again at each of the two sections', starts == 3, n3)
check('each section: the second section starts again in the middle of a '
      'page (page 3)', 1 in n3[2][1:], n3[2])
check('each section: the first number of the first page is 1',
      n3[0][:1] == [1], n3[0][:3])

# Number Every: only the lines that are a multiple of the step.
d4 = variant('print_lineno.doc', 'modulo', ['ZM1', 'ZE5'])
pdf4, rc4, err4 = pdf_of(d4, 'modulo')
n4 = [ints(p) for p in numbers(pdf4)]
check('number every 5: only multiples of 5, in order, on the pages '
      'that have five lines', all(p == [5 * (i + 1) for i in range(len(p))]
      for p in n4) and sum(len(p) for p in n4) > 20, n4)

# Side: right, outer and inner.
d5 = variant('print_lineno.doc', 'right', ['ZM1', 'ZS1'])
pdf5, rc5, err5 = pdf_of(d5, 'right')
check('side right: the numbers are in the right margin, pagewise',
      all(w[0] > 540 for p in margin_words(pdf5) for w in p) and
      all(p[:1] == [1] for p in [ints(q) for q in numbers(pdf5)]),
      margin_words(pdf5)[0][:2])
d6 = variant('print_lineno.doc', 'outer', ['ZM1', 'ZS2'])
pdf6, rc6, err6 = pdf_of(d6, 'outer')
w6 = margin_words(pdf6)
check('side outer: odd pages on the right, even pages on the left',
      all((w[0] > 540) == (i % 2 == 0) for i, p in enumerate(w6)
          for w in p), [(i, p[0][0]) for i, p in enumerate(w6) if p])
check('side outer: the numbers still start at 1 on each page',
      all(ints(p)[:1] == [1] for p in numbers(pdf6)), numbers(pdf6)[:2])
d7 = variant('print_lineno.doc', 'inner', ['ZM1', 'ZS3'])
pdf7, rc7, err7 = pdf_of(d7, 'inner')
w7 = margin_words(pdf7)
check('side inner: odd pages on the left, even pages on the right',
      all((w[0] > 540) == (i % 2 == 1) for i, p in enumerate(w7)
          for w in p), [(i, p[0][0]) for i, p in enumerate(w7) if p])

# Format and distance.
d8 = variant('print_lineno.doc', 'roman', ['ZM1', 'ZF1'])
pdf8, rc8, err8 = pdf_of(d8, 'roman')
n8 = numbers(pdf8)[1]
check('format roman: i, ii, iii, iv, v', n8[:5] == ['i', 'ii', 'iii', 'iv',
      'v'], n8[:6])
d9 = variant('print_lineno.doc', 'capital', ['ZM1', 'ZF2'])
pdf9, rc9, err9 = pdf_of(d9, 'capital')
check('format capital roman: I, II, III', numbers(pdf9)[1][:3] ==
      ['I', 'II', 'III'], numbers(pdf9)[1][:4])
d10 = variant('print_lineno.doc', 'distance', ['ZM1', 'ZD30'])
pdf10, rc10, err10 = pdf_of(d10, 'distance')
x1 = margin_words(pdf1)[0][0][0]
x10 = margin_words(pdf10)[0][0][0]
check('distance 0.30 puts the number 0.15 inch (10.8 pt) farther from the '
      'text than the default', abs((x1 - x10) - 10.8) < 1.0, (x1, x10))
d11 = variant('print_lineno.doc', 'size', ['ZM1', 'ZZ2', 'ZY1'])
pdf11, rc11, err11 = pdf_of(d11, 'size')
h11 = margin_words(pdf11)[0][0][2]
h1 = margin_words(pdf1)[0][0][2]
check('size Footnote is larger than the default size Tiny',
      h11 > h1, (h11, h1))
rc, fonts11, err = run(['pdffonts', pdf11])
check('font Roman does not bring the sans serif font', not
      re.search(r'lmss|LMSans|LMSS', fonts11), fonts11)

# Equations: the lines of a displayed equation, with NQ.
de0 = variant('equation_lines.doc', 'eq0', ['ZM1'])
de1 = variant('equation_lines.doc', 'eq1', ['ZM1', 'ZQ1'])
pe0, rce0, ee0 = pdf_of(de0, 'eq0')
pe1, rce1, ee1 = pdf_of(de1, 'eq1')
count0 = sum(len(ints(p)) for p in numbers(pe0))
count1 = sum(len(ints(p)) for p in numbers(pe1))
check('equations: a displayed equation is numbered only with the option '
      '(the document is one equation)', pe0 is not None and
      pe1 is not None and count0 == 0 and count1 == 1, (count0, count1))
tex_e1 = tex_of(de1, 'eq1')
check('equations: the option mathlines is passed on', 'mathlines' in tex_e1,
      tex_e1[tex_e1.find('lineno') - 40:][:200])

# The rows of a table in a Keep part take no number, and the count goes on
# past the table (tests/data/keep_table.doc: thirty-four lines of text, a
# Keep part of four rows of two columns, twenty more words in a last line).
dk = variant('keep_table.doc', 'keeptable', ['ZM2'])
pk, rck, ek = pdf_of(dk, 'keeptable')
rc, xml, err = run(['pdftotext', '-bbox', pk, '-'])
placed = []
for pg, part in enumerate(xml.split('<page ')[1:]):
    for m in re.finditer(r'<word xMin="([\d.]+)" yMin="([\d.]+)" '
                         r'xMax="([\d.]+)" yMax="[\d.]+">(.*?)</word>',
                         part):
        placed.append((pg, float(m.group(2)), float(m.group(1)),
                       float(m.group(3)), m.group(4)))
placed.sort()
cells = [i for i, w in enumerate(placed) if re.match(r'^T[A-H]0[1-4]$', w[4])]
margin = [(i, int(w[4])) for i, w in enumerate(placed)
          if w[3] < 72 and w[4].isdigit()]
inside = [n for i, n in margin if cells and cells[0] < i < cells[-1]]
before = [n for i, n in margin if cells and i < cells[0]]
after = [n for i, n in margin if cells and i > cells[-1]]
check('keep table: the document prints and the rows have no number in '
      'the margin', pk is not None and len(cells) == 32 and inside == [],
      (len(cells), inside))
check('keep table: the count goes on past the table (the line after it is '
      'the next number)', before and after and
      after[0] == before[-1] + 1, (before[-3:], after[:3]))

# The Print Error marks, the banner and the Error Log keep working.
derr = variant('print_errors.doc', 'errors', ['ZM1'])
pe, rce, ee = pdf_of(derr, 'errors')
rc, text, err = run(['pdftotext', pe, '-'])
check('errors: the print fails as before (status 4) and the PDF is made',
      rce == 4 and pe is not None, (rce, ee))
check('errors: the banner is on the pages', 'PRINT FAILED' in text, text[:200])
check('errors: the Error Log page is in the PDF', 'Error Log' in text)
last = margin_words(pe)[-1]
check('errors: the Error Log page has no line numbers', not last, last)
check('errors: the marks of the errors are still before their places',
      re.search(r'E1', text) is not None, text[:300])

# The preview (the DVI of the same typesetting) has the same numbers.
dvi = os.path.join(tmp, 'preview.dvi')
rc, so, se = run([strprint, d1, '--dvi', '-o', dvi, '--docdir', data])
pdfd = os.path.join(tmp, 'preview.pdf')
run(['dvipdfmx', '-q', '-o', pdfd, dvi])
check('the preview: the DVI route has the same numbers on every page as '
      'the PDF route', os.path.exists(pdfd) and numbers(pdfd) ==
      numbers(pdf1), (numbers(pdfd)[:2] if os.path.exists(pdfd) else None))
dvi3 = os.path.join(tmp, 'preview3.dvi')
rc, so, se = run([strprint, d3, '--dvi', '-o', dvi3, '--docdir', data])
pdfd3 = os.path.join(tmp, 'preview3.pdf')
run(['dvipdfmx', '-q', '-o', pdfd3, dvi3])
check('the preview: each section numbers the same in the DVI route',
      os.path.exists(pdfd3) and numbers(pdfd3) == numbers(pdf3))

# An exported file compiles by itself and has the same numbers.
ex = os.path.join(tmp, 'exp')
os.makedirs(ex)
tex = os.path.join(ex, 'numbered.tex')
rc, so, se = run([strprint, d1, '--export-latex', tex, '--docdir', data,
                  '--source-name', 'numbered.doc'])
src = open(tex).read() if os.path.exists(tex) else ''
check('export: the header comment lists lineno among the packages',
      re.search(r'^%\s+lineno\s*$', src, re.M) is not None, src[:900])
for _ in range(2):
    rc, so, se = run(['pdflatex', '-interaction=nonstopmode',
                      '-halt-on-error', 'numbered.tex'], cwd=ex)
epdf = os.path.join(ex, 'numbered.pdf')
check('export: the file compiles by itself to the same numbers as the '
      'print', rc == 0 and os.path.exists(epdf) and
      numbers(epdf) == numbers(pdf1),
      (rc, numbers(epdf)[:1] if os.path.exists(epdf) else None))

print('lineno: %d failures' % fails)
sys.exit(1 if fails else 0)
