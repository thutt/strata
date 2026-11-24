#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""Tests of sections that are nested deeply (docs/SECTION-DEPTH.txt).

The original loads documents nested 3000 levels deep, numbers the levels 1
to 99 and gives no number to a section deeper than that.  Documents made by
tests/mkdeep.py are loaded by the programs and printed:

  - sectags (the tags as the Editor draws them) gives the level N the tag
    1.1.1... of N numbers up to level 99 and no tag to level 100 or deeper;
  - the Printer makes a PDF of every one of them, with every headline in it
    and the table of contents of the levels 1 to 99, whose entries have the
    tags of the screen (a tag of more than 30 characters is broken after a
    point in its column);
  - the document of 3000 levels is read and written again unchanged.

    STRATA_BUILD_DIR=... python3 -I tests/deepsect.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import mkdeep

bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
stred = os.environ.get('STRED', os.path.join(bdir, 'term', 'stred'))
sectags = os.environ.get('SECTAGS', os.path.join(bdir, 'term', 'sectags'))
roundtrip = os.path.join(bdir, 'term', 'roundtrip')

if not os.path.exists(strprint) or not os.path.exists(sectags):
    print('SKIP: build the programs first')
    sys.exit(0)

for tool in ('pdflatex', 'pdftotext'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)

NUMDEPTH = 99
tmp = tempfile.mkdtemp(prefix='deepsect')
fails = 0


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
          + str(extra)[:600]))
    fails += 0 if cond else 1


def squeeze(text):
    return ''.join(text.split())


def tag_of(level):
    return '.'.join(['1'] * level)


def document(depth):
    path = os.path.join(tmp, 'deep%d.doc' % depth)

    with open(path, 'wb') as f:
        f.write(mkdeep.make(depth))

    return path


def tags(path):
    """(level, tag) of every headline, as the Editor makes them."""
    r = subprocess.run([sectags, path], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=300)
    rows = []

    for line in r.stdout.split(b'\n'):
        if line != b'':
            f = line.split(b'\t')
            rows.append((int(f[1]), f[2].decode('cp437').strip()))

    return r.returncode, rows


def pdf_text(pdf, first=None, last=None):
    cmd = ['pdftotext', '-layout']

    if first is not None:
        cmd += ['-f', str(first), '-l', str(last)]

    r = subprocess.run(cmd + [pdf, '-'], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=120)
    return r.stdout.decode('utf-8', 'replace').split('\n')


def contents_tag(lines, level):
    """The tag of the contents entry of "Level N": the text before the title
    on its line and the lines under it that hold only the rest of the tag."""
    at = None

    for k, ln in enumerate(lines):
        if re.search(r'(^|\s)Level %d\s+\.' % level, ln):
            at = k
            break

    if at is None:
        return None

    text = lines[at].split('Level %d' % level)[0]
    j = at + 1

    while j < len(lines) and lines[j].strip() != '' and \
            re.fullmatch(r'[0-9.\s]+', lines[j]) is not None:
        text += lines[j]
        j += 1

    return squeeze(text)


def main():
    for depth in (33, 100, 300):
        path = document(depth)
        rc, rows = tags(path)
        check('depth %d: sectags lists the %d headlines' % (depth, depth),
              rc == 0 and len(rows) == depth, (rc, len(rows)))
        bad = [(lv, tg) for lv, tg in rows
               if tg != (tag_of(lv) if lv <= NUMDEPTH else '')]
        check('depth %d: the levels 1 to %d have the tag of their place and '
              'the deeper ones none' % (depth, NUMDEPTH), bad == [], bad[:3])

    # The print of a document nested 100 levels: all the headlines, the
    # table of contents with the tags of the screen.
    path = document(100)
    pdf = os.path.join(tmp, 'deep100.pdf')
    r = subprocess.run([strprint, path, '--contents', '99', '-o', pdf,
                        '--no-banner', '--no-error-log'],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=300)
    check('depth 100: the Printer makes a PDF', r.returncode == 0 and
          os.path.exists(pdf), r.stderr)
    lines = pdf_text(pdf) if os.path.exists(pdf) else []
    alltext = '\n'.join(lines)
    missing = [k for k in range(1, 101)
               if 'Text of Level %d' % k not in alltext]
    check('depth 100: the text of all 100 sections is printed',
          missing == [], missing[:5])
    head = pdf_text(pdf, 1, 14) if os.path.exists(pdf) else []
    bad = []

    for level in range(1, NUMDEPTH + 1):
        got = contents_tag(head, level)

        if got != tag_of(level):
            bad.append((level, got))

    check('depth 100: the contents list the levels 1 to 99 with the tags of '
          'the screen', bad == [], bad[:3])
    check('depth 100: the contents of the levels 1 to 99 leave level 100 out',
          contents_tag(head, 100) is None, contents_tag(head, 100))

    # A level that --contents does not reach is left out as well.
    pdf2 = os.path.join(tmp, 'deep100b.pdf')
    subprocess.run([strprint, path, '--contents', '40', '-o', pdf2,
                    '--no-banner', '--no-error-log'],
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                   timeout=300)
    head2 = pdf_text(pdf2, 1, 8) if os.path.exists(pdf2) else []
    check('depth 100: --contents 40 lists level 40 and not level 41',
          contents_tag(head2, 40) == tag_of(40) and
          contents_tag(head2, 41) is None,
          (contents_tag(head2, 40), contents_tag(head2, 41)))

    # The Editor's own print of the part from the section of level 60.
    pdf3 = os.path.join(tmp, 'part60.pdf')
    env = dict(os.environ, STRATA_STRPRINT=strprint)
    r = subprocess.run([stred, '--print', path, '--from', tag_of(60), '--to',
                        tag_of(60), '-o', pdf3, '--no-error-log'], env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=300)
    t3 = '\n'.join(pdf_text(pdf3)) if os.path.exists(pdf3) else ''
    check('depth 100: the print of the section 1.1...1 (60 levels) holds the '
          'sections of the levels 60 to 100 and not those above',
          r.returncode == 0 and 'Text of Level 60' in t3 and
          'Text of Level 100' in t3 and 'Text of Level 59' not in t3,
          r.stderr)

    # A document of 3000 levels: loaded, the tags, written again unchanged.
    path = document(3000)
    rc, rows = tags(path)
    check('depth 3000: sectags lists the 3000 headlines, 99 of them numbered',
          rc == 0 and len(rows) == 3000 and
          len([1 for lv, tg in rows if tg != '']) == NUMDEPTH,
          (rc, len(rows)))

    if os.path.exists(roundtrip):
        r = subprocess.run([roundtrip, path], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=300)
        check('depth 3000: the document is read and written again unchanged',
              r.returncode == 0, r.stdout[-200:] + r.stderr[-200:])

    r = subprocess.run([strprint, path, '--tex-only', '-o',
                        os.path.join(tmp, 'deep3000.tex'), '--no-banner'],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=300)
    check('depth 3000: the Printer writes the LaTeX', r.returncode == 0,
          r.stderr)


try:
    main()
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print('deepsect: %d failures' % fails)
sys.exit(1 if fails else 0)
