#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""A table in a sublevel of a structured document is indented with the text of
its level and ends at the right margin, as the original prints it
(docs/TABLES-IN-SUBLEVELS-PROBE.txt).  The probe document docs/probe/
tabprobe.src has a table in each of the levels 1, 1.1 and 1.1.1; it is made
with manual/mkmanual.py, printed with strprint, and the left and right edge
of every table are read from a picture of the page.

    STRPRINT=... python3 -I -B tests/tabindent.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
DPI = 110
STEP = 0.3 * DPI

for tool in ('pdflatex', 'pdftotext', 'pdftoppm'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)

tmp = tempfile.mkdtemp(prefix='tabindent')
fails = 0


def check(what, ok, detail=''):
    global fails

    if ok:
        print('ok   %s' % what)
    else:
        fails += 1
        print('FAIL %s\n%s' % (what, detail))


def words(pdf):
    box = subprocess.run(['pdftotext', '-bbox', pdf, '-'],
                         capture_output=True, text=True).stdout

    return [(m.group(5), float(m.group(1)), float(m.group(2)),
             float(m.group(4)))
            for m in re.finditer(r'xMin="([\d.]+)" yMin="([\d.]+)" '
                                 r'xMax="([\d.]+)" yMax="([\d.]+)">([^<]*)<',
                                 box)]


def image(pdf):
    """The first page as rows of brightness, from pdftoppm -gray."""
    pre = os.path.join(tmp, 'page')
    subprocess.run(['pdftoppm', '-r', str(DPI), '-gray', '-f', '1', '-l', '1',
                    pdf, pre], check=True)
    name = [f for f in os.listdir(tmp) if f.endswith('.pgm')][0]
    data = open(os.path.join(tmp, name), 'rb').read()
    m = re.match(rb'P5\s+(\d+)\s+(\d+)\s+255\s', data)
    w, h = int(m.group(1)), int(m.group(2))
    pix = data[m.end():]

    return w, [pix[y * w:(y + 1) * w] for y in range(h)]


def edges(rows, y):
    """The leftmost and the rightmost dark pixel in the pixel row y."""
    dark = [x for x, v in enumerate(rows[y]) if v < 128]

    return (dark[0], dark[-1]) if dark else (None, None)


def run():
    doc = os.path.join(tmp, 'tabprobe.doc')
    pdf = os.path.join(tmp, 'tabprobe.pdf')
    p = subprocess.run([sys.executable, '-I', '-B',
                        os.path.join(root, 'manual', 'mkmanual.py'),
                        os.path.join(root, 'docs', 'probe', 'tabprobe.src'),
                        doc], capture_output=True, text=True)
    check('the probe document is made', p.returncode == 0, p.stderr)
    p = subprocess.run([strprint, doc, '-o', pdf], capture_output=True,
                       text=True, cwd=tmp, timeout=600)
    check('the probe document is printed', p.returncode == 0 and
          os.path.exists(pdf), p.stdout + p.stderr)

    if fails:
        return

    ws = words(pdf)
    texts = [w for w in ws if w[0] == 'Text']
    heads = [w for w in ws if w[0] == 'Col']
    # The first "Col" of each table is the first cell of its header row.
    check('three tables and four texts are found',
          len(texts) == 4 and len(heads) // 3 == 3,
          '%d Text, %d Col' % (len(texts), len(heads)))

    if fails:
        return

    _, rows = image(pdf)
    left = []
    right = []

    for k in range(3):
        col = [h for h in heads if h[0] == 'Col'][3 * k]
        y = int(round((col[2] + col[3]) / 2 * DPI / 72))
        l, r = edges(rows, y)
        left.append(l)
        right.append(r)

    margin = texts[0][1] * DPI / 72
    check('the table of level 1 starts at the left margin',
          abs(left[0] - margin) <= 2, 'left %s, margin %.1f' % (left, margin))

    for k in (1, 2):
        check('the table of level %d starts %d steps in from the margin'
              % (k + 1, k), abs(left[k] - left[0] - k * STEP) <= 2,
              'left edges %s' % left)
        check('the table of level %d starts where the text of its level does'
              % (k + 1), abs(left[k] - texts[k][1] * DPI / 72) <= 3,
              'table %s, text %.1f' % (left[k], texts[k][1] * DPI / 72))
        check('the table of level %d ends at the right margin as level 1'
              % (k + 1), abs(right[k] - right[0]) <= 2,
              'right edges %s' % right)


run()
shutil.rmtree(tmp, ignore_errors=True)
print('tabindent: %d failure(s)' % fails)
sys.exit(1 if fails else 0)
