#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""Tests of the table of contents of a print.

The documents toc_*.doc of tests/data (tests/mktocdata.py makes them; every
headline starts with a word that occurs nowhere else, Tc001x and so on) are
printed with strprint (and, for a part, with stred --print), and the PDF is
read with pdfinfo and pdftotext: which entries the contents page has, with
which tags and which page numbers, and where the body begins.

    STRPRINT=... STRED=... python3 -I -B tests/tocpdf.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
stred = os.environ.get('STRED', os.path.join(bdir, 'term', 'stred'))

for tool in ('pdflatex', 'pdftotext', 'pdfinfo'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)

data = os.path.join(here, 'data')
tmp = tempfile.mkdtemp(prefix='tocpdf')
fails = 0
checks = 0

# What the ten headlines of toc_ten.doc are: the word, the tag the screen
# shows and the page of the body on which the headline is (FLP1: each
# chapter starts a page).
TEN = [('Tc001x', '1', 1), ('Tc002x', '1.1', 1), ('Tc003x', '1.2', 1),
       ('Tc004x', '2', 2), ('Tc005x', '2.1', 2), ('Tc006x', '3', 3),
       ('Tc007x', '3.1', 3), ('Tc008x', '3.2', 3), ('Tc009x', '4', 4),
       ('Tc010x', '4.1', 4)]


def check(what, ok, detail=''):
    global fails, checks
    checks += 1

    if ok:
        print('ok   %s' % what)
    else:
        fails += 1
        print('FAIL %s\n%s' % (what, detail))


def pdf_pages(pdf):
    """The text of each page of a PDF."""
    info = subprocess.run(['pdfinfo', pdf], capture_output=True,
                          text=True).stdout
    n = int(re.search(r'Pages:\s+(\d+)', info).group(1))
    out = []

    for k in range(1, n + 1):
        out.append(subprocess.run(['pdftotext', '-f', str(k), '-l', str(k),
                                   '-layout', pdf, '-'],
                                  capture_output=True, text=True).stdout)

    return out


def printed(name, args=()):
    """The pages of the print of a test document with strprint."""
    work = tempfile.mkdtemp(prefix='run', dir=tmp)
    src = os.path.join(work, name)
    shutil.copy(os.path.join(data, name), src)
    pdf = os.path.join(work, 'out.pdf')
    p = subprocess.run([strprint, src, '-o', pdf] + list(args),
                       capture_output=True, text=True, cwd=work, timeout=600)

    if p.returncode != 0 or not os.path.exists(pdf):
        return None

    return pdf_pages(pdf)


def entries(page):
    """The lines of a contents page: (tag, word, page number) of each."""
    out = []

    for ln in page.split('\n'):
        m = re.match(r'\s*([0-9.]*)\s+(T[cdf]\d{3}x)[ .]*\s(\d+)\s*$', ln)

        if m:
            out.append((m.group(2), m.group(1), int(m.group(3))))

    return out


# The default is the document's: toc_ten.doc has no record YCL, which is the
# level 99, so the contents is made with no option (a page and the four of
# the body); --contents 0 makes the same four pages as before.
dd = printed('toc_ten.doc')
check('contents: not asked, the document says (a page more, with Contents)',
      dd is not None and len(dd) == 5 and 'Contents' in dd[0],
      dd and len(dd))
d0 = printed('toc_ten.doc', ['--contents', '0'])
check('contents: --contents 0, the body only (four pages, no Contents)',
      d0 is not None and len(d0) == 4 and
      all('Contents' not in p for p in d0), d0 and len(d0))

# Ten headlines over four pages, with their tags and pages.
d3 = printed('toc_ten.doc', ['--contents', '3'])
check('contents: a page more, and the body still has four',
      d3 is not None and len(d3) == 5, d3 and len(d3))

if d3 is not None and len(d3) == 5:
    e = entries(d3[0])
    check('contents: the entries are in the order of the text, with the '
          'tags of the screen and the pages of the body', e == TEN, e)
    check('contents: the page has the title and a roman page number',
          'Contents' in d3[0] and re.search(r'\n\s+i\s*\n', d3[0] + '\n'),
          d3[0][-120:])
    check('contents: no page number is left as ??',
          all('??' not in p for p in d3), '')
    check('contents: the body starts again at page 1',
          re.search(r'Tc001x', d3[1]) is not None and
          re.search(r'\n\s+1\s*\n', d3[1] + '\n') is not None, d3[1][-120:])
    check('contents: the last chapter is on page 4 of the body',
          'Tc009x' in d3[4] and re.search(r'\n\s+4\s*\n', d3[4] + '\n'),
          d3[4][-120:])

# The levels field: level 1 lists the four chapters only.
d1 = printed('toc_ten.doc', ['--contents', '1'])

if d1 is not None:
    e = entries(d1[0])
    check('contents: level 1 lists the four chapters',
          [x[0] for x in e] == ['Tc001x', 'Tc004x', 'Tc006x', 'Tc009x'] and
          [x[2] for x in e] == [1, 2, 3, 4], e)
else:
    check('contents: level 1 prints', False, '')

# A title page comes first, then the contents, then the body.
dt = printed('toc_title.doc', ['--contents', '3'])
check('contents: title page, contents page, then four pages of body',
      dt is not None and len(dt) == 6 and 'Tctitle' in dt[0] and
      'Contents' in dt[1] and 'Contents' not in dt[0], dt and len(dt))

if dt is not None and len(dt) == 6:
    check('contents: with a title page the entries are the same',
          entries(dt[1]) == TEN, entries(dt[1]))

# Three levels: level 2 lists the chapters and the sections, not the third.
dd = printed('toc_deep.doc', ['--contents', '2'])

if dd is not None:
    words = [x[0] for x in entries(dd[0])]
    check('contents: level 2 of three lists four of the six headlines',
          words == ['Td001x', 'Td002x', 'Td005x', 'Td006x'], words)
else:
    check('contents: three levels print', False, '')

# A document with no headline: one line that says so.
df = printed('toc_flat.doc', ['--contents', '3'])
check('contents: no headline, a page that says so, then the body',
      df is not None and len(df) == 2 and
      'no headlines to list' in df[0] and 'Tf001x' in df[1],
      df and df[0])

# A draft has it too.
dr = printed('toc_ten.doc', ['--contents', '3', '--draft'])
check('contents: a draft has the contents too',
      dr is not None and len(dr) == 5 and len(entries(dr[0])) == 10,
      dr and len(dr))

# A part: stred --print of sections 2 and 3 lists those with the screen's
# numbers and the pages of the part.
if os.path.exists(stred):
    work = tempfile.mkdtemp(prefix='part', dir=tmp)
    src = os.path.join(work, 'toc_ten.doc')
    shutil.copy(os.path.join(data, 'toc_ten.doc'), src)
    pdf = os.path.join(work, 'part.pdf')
    p = subprocess.run([stred, '--print', src, '--from', '2', '--to', '3',
                        '--contents', '2', '--no-title', '-o', pdf],
                       capture_output=True, text=True, cwd=work, timeout=600)
    pages = pdf_pages(pdf) if os.path.exists(pdf) else None

    if pages is not None:
        e = entries(pages[0])
        check('contents: a part lists its sections with the numbers of the '
              'screen and the pages of the part',
              e == [('Tc004x', '2', 1), ('Tc005x', '2.1', 1),
                    ('Tc006x', '3', 2), ('Tc007x', '3.1', 2),
                    ('Tc008x', '3.2', 2)], (e, p.stderr[-200:]))
    else:
        check('contents: a part prints', False, p.stderr[-300:])

    # A level that is not 0 to 99 is refused.
    p = subprocess.run([stred, '--print', src, '--contents', '100', '-o',
                        pdf + '2'], capture_output=True, text=True, cwd=work,
                       timeout=60)
    check('contents: the level 100 is refused',
          p.returncode != 0 and 'level from 0 to 99' in p.stderr, p.stderr)

shutil.rmtree(tmp, ignore_errors=True)
print('tocpdf: %d checks, %d failures' % (checks, fails))
sys.exit(1 if fails else 0)
