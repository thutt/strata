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

"""Tests of the Keep marker in the printing path: strprint typesets the
documents tests/data/keep_*.doc (made by tests/mkkeepdata.py), and the
pages of the PDF are read with pdftotext.  Every line of those documents is
a label such as KA03, so a test says on which page a line fell.  The
control of each test is the same document with the markers taken out.

Needs latex (or pdflatex), dvipdfmx and pdftotext; without them the tests
are skipped."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
sys.path.insert(0, here)
import mkkeepdata

bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
data = os.path.join(root, 'tests', 'data')

for tool in ('pdflatex', 'pdftotext'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.access(strprint, os.X_OK):
    print('SKIP: no strprint at %s' % strprint)
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='keept')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def typeset(doc, name, opts=()):
    """Typeset a document (bytes); returns the text of each page."""
    src = os.path.join(tmp, name + '.doc')
    out = os.path.join(tmp, name + '.pdf')
    with open(src, 'wb') as f:
        f.write(doc)
    r = subprocess.run([strprint, src, '-o', out] + list(opts),
                       capture_output=True, text=True, timeout=300)
    if r.returncode != 0:
        return None, r.stderr
    t = subprocess.run(['pdftotext', '-layout', out, '-'],
                       capture_output=True, text=True).stdout
    pages = t.split('\f')
    if pages and pages[-1].strip() == '':
        pages.pop()
    return pages, ''


LABEL = re.compile(r'\b(?:L|K[A-D]|M|T[A-H])\d\d\b')


def page_of(pages):
    """Maps every label to its page number (1 based)."""
    where = {}
    for i, p in enumerate(pages):
        for lab in LABEL.findall(p):
            where.setdefault(lab, []).append(i + 1)
    return where


def mk(fn, markers=True):
    return fn(markers)


def run(name, fn):
    """The document with and without markers: (pages, where, controls)."""
    with open(os.path.join(data, name + '.doc'), 'rb') as f:
        real = f.read()
    check(name + ': tests/data is what mkkeepdata makes', real == fn(True))
    pg, err = typeset(real, name)
    check(name + ': strprint succeeds', pg is not None, err)
    cg, err = typeset(fn(False), name + '_control')
    check(name + ': the control typesets', cg is not None, err)
    if pg is None or cg is None:
        sys.exit(1)
    check(name + ': no text of a Keep marker is printed',
          not any('Keep' in p for p in pg))
    w = page_of(pg)
    cw = page_of(cg)
    check(name + ': no line is lost or printed twice',
          all(len(v) == 1 for v in w.values()) and
          sorted(w) == sorted(cw), (sorted(w), sorted(cw)))
    return pg, w, cw


def same_page(w, labels):
    return len({w[x][0] for x in labels}) == 1


def span(prefix, n):
    return ['%s%02d' % (prefix, i + 1) for i in range(n)]


# A pair near a page end: the whole region goes to the next page.
pg, w, cw = run('keep_fixed', mkkeepdata.fixed)
reg = span('KA', 5) + span('KB', 5) + span('KC', 5)
check('fixed: without the markers the region is broken by the page',
      not same_page(cw, reg))
check('fixed: the region is on one page', same_page(w, reg))
check('fixed: it is on page 2, the filler stays on page 1',
      w['KA01'] == [2] and w['L34'] == [1])
check('fixed: the text after the End follows the region',
      w['M01'] == [2] and w['M20'] == [2])

# Markers in the text of blocks: the blocks that hold them are the region.
pg, w, cw = run('keep_text', mkkeepdata.text)
reg = span('KA', 5) + span('KB', 5) + span('KC', 5) + span('KD', 3)
check('text: without the markers the blocks are broken by the page',
      not same_page(cw, reg))
check('text: whole blocks are kept, the part before the Start too',
      same_page(w, reg) and w['KA01'] == [2])

# Floating: the text after the region moves up in front of it.
pg, w, cw = run('keep_float', mkkeepdata.floating)
reg = span('KA', 5) + span('KB', 5) + span('KC', 5)
check('float: the region is on one page (page 2)',
      same_page(w, reg) and w['KA01'] == [2])
check('float: the text after the End fills page 1',
      w['M01'] == [1] and w['M20'] == [2])
check('float: the text keeps its order', w['M01'] == [1] and
      w['M14'] == [1] and w['M15'] == [2])

# A floating region at the very end waits for the next page; it is not lost.
pg, w, cw = run('keep_floatend', mkkeepdata.floatend)
check('floatend: the region is on page 2, the text before it on page 1',
      same_page(w, span('KA', 5) + span('KB', 5) + span('KC', 5)) and
      w['KA01'] == [2] and w['M03'] == [1])

# A Start without an End keeps to the end of the document.
pg, w, cw = run('keep_unmatched', mkkeepdata.unmatched)
check('unmatched start: from the Start on it is one page',
      same_page(w, span('KA', 5) + span('KB', 5) + span('M', 20)) and
      w['KA01'] == [2] and w['L34'] == [1])

# An End without a Start does nothing.
pg, w, cw = run('keep_stray', mkkeepdata.stray)
check('stray end: the pages are those of the control', w == cw)

# Nested pairs: the first End closes the region, a second Start is ignored.
pg, w, cw = run('keep_nested', mkkeepdata.nested)
check('nested: the region KA..KB fits and stays on page 1',
      same_page(w, span('KA', 4) + span('KB', 4)) and w['KA01'] == [1])
check('nested: the text after the first End is not kept (KC breaks)',
      w['KC01'] == [1] and w['KC12'] == [2])
check('nested: the pages are those of the control', w == cw)

# A region longer than a page starts on a new page and then breaks.
pg, w, cw = run('keep_long', mkkeepdata.long)
check('long: the region starts a new page', w['KA01'] == [2] and
      w['L10'] == [1])
check('long: it runs over the pages, nothing is lost',
      len(pg) >= 3 and w['KD20'][0] > w['KA01'][0] and w['M10'][0] >= 3)

# Columns of a table, the Start and the End in different blocks.
pg, w, cw = run('keep_table', mkkeepdata.tabular)
cells = [p + q for p in 'TA TC TE TG'.split() for q in ('01', '04')]
cells += [p + q for p in 'TB TD TF TH'.split() for q in ('01', '04')]
check('table: without the markers the rows are broken by the page',
      not same_page(cw, cells))
check('table: all the rows are on one page (page 2)',
      same_page(w, cells) and w['TA01'] == [2])

# A pair across a section boundary is kept in two parts.
pg, w, cw = run('keep_sections', mkkeepdata.sections)
check('sections: the part in the first section stays (it fits)',
      w['KA01'] == [1])
check('sections: the headline moves with the kept block of the second',
      same_page(w, span('KB', 8)) and 'SECTWO' in pg[w['KB01'][0] - 1])

# A footnote in a region is printed on the page of its mark.
pg, w, cw = run('keep_notes', mkkeepdata.note)
page = w['KA01'][0]
check('notes: the footnote is on the page of its reference',
      'FOOTNOTETEXT' in pg[page - 1] and
      sum(p.count('FOOTNOTETEXT') for p in pg) == 1)
check('notes: the region was moved to page 2', page == 2)

# The draft print and the route through a DVI file give the same pages.
src = open(os.path.join(data, 'keep_float.doc'), 'rb').read()
dp, err = typeset(src, 'draft', ['--draft'])
check('draft print: no label of a Keep marker',
      dp is not None and not any('Keep' in p for p in dp), err)
vp, err = typeset(src, 'viadvi', ['--pdf-via-dvi'])
check('through a DVI file: the same pages as pdflatex',
      vp is not None and page_of(vp) == page_of(
          typeset(src, 'plain')[0]), err)

shutil.rmtree(tmp)
print('keep: %d failures' % fails)
sys.exit(1 if fails else 0)
