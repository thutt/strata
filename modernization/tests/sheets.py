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

"""The layout of the three sheets that are folded in three: the key leaflet
(3 pages), the card of the first forty keys (1 page) and the Math Symbols card
(2 pages).  Each is made from its source (manual/src), printed by the Printer
and measured with pdftotext -bbox.  The sheet is landscape letter paper, 792
points wide.  The rules between the panels are at a third and two thirds of
the sheet (264 and 528 points), so the text of the second and the third panel
begins 6 points (the space of the column) after them, on every page; no text
goes past the right margin of 0.2 inch (a keycap at the end of a line may go
into the space of the last column); and no text is closer than 5 mm to the
edge of the paper."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
manual = os.path.join(root, 'manual')
bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
for tool in ('pdftotext', 'pdfinfo'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint):
    print('SKIP: build strprint first')
    sys.exit(0)

SHEETS = (('cheat', 3), ('cheat-short', 1), ('mathcheat', 2))
MARGIN = 0.2 * 72       # The margin of the sheets, 0.2 inch.
SPACE = 6.0             # The space of a column on each side (tabcolsep).
MM5 = 5 / 25.4 * 72 - 1.5
fails = 0


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def run(args):
    return subprocess.run(args, capture_output=True, text=True)


def pages_of(pdf):
    """The size of the page as it is shown and the words of every page."""
    info = run(['pdfinfo', pdf]).stdout
    rot = re.search(r'Page rot:\s+(\d+)', info)
    out = run(['pdftotext', '-bbox', pdf, '-']).stdout
    result = []
    pw = ph = 0.0
    for pg in out.split('<page ')[1:]:
        m = re.match(r'width="([\d.]+)" height="([\d.]+)"', pg)
        pw, ph = float(m.group(1)), float(m.group(2))
        if rot and int(rot.group(1)) in (90, 270):
            pw, ph = ph, pw
        result.append([(float(a), float(b), float(c), float(d)) for a, b, c, d
            in re.findall(r'xMin="([\d.]+)" yMin="([\d.]+)" xMax="([\d.]+)" '
            r'yMax="([\d.]+)"', pg)])
    return pw, ph, result


tmp = tempfile.mkdtemp(prefix='sheets')
for name, npages in SHEETS:
    doc = os.path.join(tmp, name + '.doc')
    pdf = os.path.join(tmp, name + '.pdf')
    r = run([sys.executable, '-I', '-B', os.path.join(manual, 'mkmanual.py'),
             os.path.join(manual, 'src', name + '.src'), doc])
    check('%s: the document is made from its source' % name,
          r.returncode == 0 and os.path.exists(doc), r.stdout + r.stderr)
    r = run([strprint, doc, '-o', pdf])
    check('%s: the Printer prints it' % name,
          r.returncode == 0 and os.path.exists(pdf), r.stderr[-800:])
    if not os.path.exists(pdf):
        continue
    pw, ph, pages = pages_of(pdf)
    check('%s: it has %d page(s)' % (name, npages), len(pages) == npages,
          len(pages))
    third = pw / 3
    for n, words in enumerate(pages, 1):
        left = min(w[0] for w in words)
        right = max(w[2] for w in words)
        top = min(w[1] for w in words)
        bottom = max(w[3] for w in words)
        check('%s, page %d: the text is at least 5 mm inside the paper' %
              (name, n), min(left, pw - right, top, ph - bottom) >= MM5,
              (left, pw - right, top, ph - bottom))
        for k in (1, 2):
            want = k * third + SPACE
            near = [w[0] for w in words if abs(w[0] - want) <= 1.5]
            check('%s, page %d: the text of panel %d begins %.0f points '
                  'after the left edge of the sheet (a third of it)' %
                  (name, n, k + 1, want), len(near) > 0,
                  sorted(set(round(w[0]) for w in words
                             if abs(w[0] - want) <= 30)))
        # The outer edge of the table is the right margin.  A keycap at the
        # end of a line may go into the space of the column, but no further.
        limit = pw - MARGIN
        check('%s, page %d: no text goes past the right margin (%.1f '
              'points)' % (name, n, limit), right <= limit + 0.6,
              (right, limit))

shutil.rmtree(tmp, ignore_errors=True)
print('sheets: %d failures' % fails)
sys.exit(1 if fails else 0)
