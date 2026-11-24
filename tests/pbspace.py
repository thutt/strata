#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""Tests of the Page Break Level and of the spacing of a block in a print.

The documents pb_*.doc and sp_*.doc of tests/data (tests/mkpbdata.py makes
them; every headline and every block starts with a word that occurs nowhere
else) are printed with strprint, and the PDF is read with pdfinfo and
pdftotext: on which page a headline is, how many pages a document has.

    STRPRINT=... python3 -I -B tests/pbspace.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))

for tool in ('pdflatex', 'pdftotext', 'pdfinfo'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)

data = os.path.join(here, 'data')
tmp = tempfile.mkdtemp(prefix='pbspace')
fails = 0
checks = 0


def check(what, ok, detail=''):
    global fails, checks
    checks += 1

    if ok:
        print('ok   %s' % what)
    else:
        fails += 1
        print('FAIL %s\n%s' % (what, detail))


def pages_of(name, args=(), source=None):
    """The text of each page of the print of a test document."""
    work = tempfile.mkdtemp(prefix='run', dir=tmp)
    src = os.path.join(work, name)

    if source is None:
        shutil.copy(os.path.join(data, name), src)
    else:
        with open(src, 'wb') as f:
            f.write(source)

    pdf = os.path.join(work, 'out.pdf')
    p = subprocess.run([strprint, src, '-o', pdf] + list(args),
                       capture_output=True, text=True, cwd=work, timeout=600)

    if p.returncode != 0 or not os.path.exists(pdf):
        return None

    info = subprocess.run(['pdfinfo', pdf], capture_output=True,
                          text=True).stdout
    n = int(re.search(r'Pages:\s+(\d+)', info).group(1))
    out = []

    for k in range(1, n + 1):
        out.append(subprocess.run(['pdftotext', '-f', str(k), '-l', str(k),
                                   '-layout', pdf, '-'],
                                  capture_output=True, text=True).stdout)

    return out


def words(page):
    return re.findall(r'Pb\d\d\dx', page)


# ---- Page Break Level
p0 = pages_of('pb_0.doc')
check('page break level 0: the document of nine headlines is one page',
      p0 is not None and len(p0) == 1, p0)

p1 = pages_of('pb_1.doc')
check('page break level 1: each of the three chapters starts a page',
      p1 is not None and len(p1) == 3 and
      [words(p)[0] for p in p1] == ['Pb001x', 'Pb004x', 'Pb007x'],
      [words(p)[:2] for p in p1] if p1 else p1)
check('page break level 1: the first chapter does not leave a blank page',
      p1 is not None and all(p.strip() != '' for p in p1) and
      'Pb001x' in p1[0], p1)
check('page break level 1: the sections of a chapter stay with it',
      p1 is not None and len(p1) == 3 and
      all(w in p1[0] for w in ('Pb002x', 'Pb003x')) and
      all(w in p1[1] for w in ('Pb005x', 'Pb006x')), p1)

p2 = pages_of('pb_2.doc')
check('page break level 2: every chapter and every section starts a page',
      p2 is not None and len(p2) == 9 and
      all(words(p)[0] == 'Pb%03dx' % (k + 1) for k, p in enumerate(p2)),
      [words(p)[:1] for p in p2] if p2 else p2)

pt = pages_of('pb_title.doc')
check('page break level 1 with a title page: the title page, then a page for '
      'each chapter',
      pt is not None and len(pt) == 4 and 'Title' in pt[0] and
      not words(pt[0]) and words(pt[1])[0] == 'Pb001x', pt)

pd = pages_of('pb_1.doc', ['--draft'])
check('page break level 1: a draft starts the pages too',
      pd is not None and len(pd) == 3 and
      [words(p)[0] for p in pd] == ['Pb001x', 'Pb004x', 'Pb007x'], pd)

# A Keep region is not broken by the level: a document with Keep markers and
# a Page Break Level prints without a failure.
with open(os.path.join(data, 'keep_sections.doc'), 'rb') as f:
    keep = f.read()
keep1 = keep.replace(b'\r\n*B', b'\r\nFLP1\r\n*B', 1)
k0 = pages_of('keep_sections.doc')
k1 = pages_of('keep_sections.doc', source=keep1)
check('page break level 1 in a document with Keep regions prints',
      k0 is not None and k1 is not None and len(k1) >= len(k0) and
      keep1 != keep, (k0 and len(k0), k1 and len(k1)))

# ---- the spacing of a block
s0 = pages_of('sp_0.doc')
sl = pages_of('sp_line2.doc')
sb = pages_of('sp_block3.doc')
so = pages_of('sp_override.doc')
sn = pages_of('sp_named.doc')
n0 = len(s0) if s0 else 0
check('spacing: a line spacing of 2.00 in the document makes more pages',
      sl is not None and len(sl) > n0, (n0, sl and len(sl)))
check('spacing: a block spacing of 3.00 in the document makes more pages',
      sb is not None and len(sb) > n0, (n0, sb and len(sb)))
check('spacing: blocks that set 2.00 and 3.00 themselves make more pages '
      'than a line spacing of 2.00 alone',
      so is not None and sl is not None and len(so) > len(sl),
      (so and len(so), sl and len(sl)))
check('spacing: a named format of 1.00 and 1.00 wins over the 2.00 and '
      '3.00 of the document',
      sn is not None and len(sn) == n0, (n0, sn and len(sn)))

d0 = pages_of('sp_0.doc', ['--draft'])
dov = pages_of('sp_override.doc', ['--draft'])
check('spacing: a draft is one line whatever a block says',
      d0 is not None and dov is not None and len(d0) == len(dov),
      (d0 and len(d0), dov and len(dov)))
dd = pages_of('sp_0.doc', ['--double-spaced'])
check('spacing: Print Double Spaced still doubles the lines',
      dd is not None and len(dd) > n0, (n0, dd and len(dd)))

shutil.rmtree(tmp, ignore_errors=True)
print('pbspace: %d checks, %d failures' % (checks, fails))
sys.exit(1 if fails else 0)
