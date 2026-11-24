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

"""The S of the text \\strata is the drawn S of the logo.

The Printer defines \\strata in its preamble (src/latex.c preamble_strata).
Its S is the outline of the S of \\stratalogo, drawn once into a box and
scaled to the font size at every use.  Checks: the LaTeX text of the
preamble has the box, the global assignment and the scaling and has the
drawing once only; a page with the name at 60 pt is drawn at 300 dots per
inch and the S is measured: it is as high as the word (a letter of a font
overshoots the baseline and the capital height), as wide as the logo S (46
to 43.5 of its height) and its arm has the stroke of the stems of the Ts; the
PDF still holds the letter S in its text.  The same measurements are made on
a copy of the preamble in which the S is the letter of the font (the output
of the code before the change): every measurement must reject it, so that
none of the checks can pass when the change is undone.  A page with 400
names is made, and its time is compared with the same page with the S of
the font.
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile, time

here = os.path.dirname(os.path.abspath(__file__))
bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
for tool in ('pdflatex', 'pdftoppm', 'pdftotext'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
fails = 0
tmp = tempfile.mkdtemp(prefix='sname', dir=os.environ.get('TMPDIR'))
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def run(args):
    return subprocess.run(args, capture_output=True, text=True, cwd=tmp)


def preamble():
    """The preamble of a print of a made document, up to \\begin{document}."""
    doc = os.path.join(tmp, 'a.doc')
    shutil.copy(os.path.join(here, 'data', 'plain_text.doc'), doc)
    r = run([strprint, doc, '--export-latex', os.path.join(tmp, 'a.tex')])
    text = open(os.path.join(tmp, 'a.tex'), encoding='utf-8').read()
    if r.returncode != 0 or '\\begin{document}' not in text:
        print('FAIL the LaTeX of a print could not be made\n' + r.stderr)
        sys.exit(1)
    return text[:text.index('\\begin{document}')]


def page(pre, name, body, dpi=300):
    """Make a PDF of the body with the preamble and return its pixels as
    (width, height, rows) with 0 for ink and 255 for paper."""
    tex = os.path.join(tmp, name + '.tex')
    open(tex, 'w', encoding='utf-8').write(
        pre + '\\begin{document}\n\\thispagestyle{empty}\n' + body
        + '\\end{document}\n')
    r = run(['pdflatex', '-interaction=nonstopmode', '-halt-on-error',
             name + '.tex'])
    pdf = os.path.join(tmp, name + '.pdf')
    if not os.path.exists(pdf):
        return None, r.stdout[-600:]
    run(['pdftoppm', '-r', str(dpi), '-gray', '-f', '1', '-l', '1', pdf,
         os.path.join(tmp, name)])
    pgm = [f for f in os.listdir(tmp) if f.startswith(name + '-')
           and f.endswith('.pgm')][0]
    data = open(os.path.join(tmp, pgm), 'rb').read()
    m = re.match(rb'P5\s+(\d+)\s+(\d+)\s+(\d+)\s', data)
    w, h = int(m.group(1)), int(m.group(2))
    raw = data[m.end():]
    return (w, h, [raw[y * w:(y + 1) * w] for y in range(h)]), pdf


def measure(pix):
    """The numbers of the name: (S box, word box, aspect, arm, stem)."""
    w, h, rows = pix
    ink = [[x for x in range(w) if rows[y][x] < 128] for y in range(h)]
    ys = [y for y in range(h) if ink[y]]
    if not ys:
        return None
    cols = sorted(set(x for y in ys for x in ink[y]))
    clusters = []
    start = prev = cols[0]
    for x in cols[1:]:
        if x - prev > 3:
            clusters.append((start, prev))
            start = x
        prev = x
    clusters.append((start, prev))
    s0, s1 = clusters[0]
    sy = [y for y in ys if any(s0 <= x <= s1 for x in ink[y])]
    rest = [y for y in ys if any(x > s1 for x in ink[y])]
    wtop, wbot = min(ys), max(rest)
    sw, sh = s1 - s0 + 1, max(sy) - min(sy) + 1
    # The arm: the ink run down from the top of the S at 75 percent of its
    # width.
    xc = s0 + int(0.75 * sw)
    arm = 0
    y = min(sy)
    while y <= max(sy) and rows[y][xc] < 128:
        arm += 1
        y += 1
    # The stem of the first T: the first ink run right of the S on a row
    # just above the baseline.
    row = wbot - 4
    runs = []
    x = s1 + 1
    while x < w:
        if rows[row][x] < 128:
            a = x
            while x < w and rows[row][x] < 128:
                x += 1
            runs.append(x - a)
        else:
            x += 1
    stem = runs[0] if runs else 0
    return {'s_top': min(sy), 's_bot': max(sy), 'w_top': wtop, 'w_bot': wbot,
            'aspect': sw / sh, 'arm': arm, 'stem': stem}


def good_s(m):
    """The three measurements that a drawn S of the logo passes."""
    return (abs(m['s_top'] - m['w_top']) <= 1
            and abs(m['s_bot'] - m['w_bot']) <= 1,
            1.0 <= m['aspect'] <= 1.12,
            m['stem'] > 0 and abs(m['arm'] - m['stem']) <= 0.15 * m['stem'])


pre = preamble()

# The LaTeX text of the preamble.
check('the preamble holds the box of the S', '\\newsavebox{\\strlbS}' in pre)
check('the S is made once and kept (global assignment)',
      '\\global\\setbox\\strlbS' in pre and '\\global\\strlSdrawntrue' in pre)
check('the S is scaled to the font size at every use',
      '\\scalebox{\\strlf}{\\usebox{\\strlbS}}' in pre)
part = pre[pre.index('\\newcommand{\\strlmakeS}'):
           pre.index('\\newcommand{\\strlogo}')]
check('the drawing is in the macro that makes it, once',
      part.count('\\begin{tikzpicture}') == 1, part.count('tikzpicture'))
logo = pre[pre.index('\\newcommand{\\strlogo}'):]
logo = logo[:logo.index('\\makeatother')]
check('the name does not draw a picture itself',
      'tikzpicture' not in logo and '\\strlputS' in logo)

# The page with the name at 60 pt.
body = ('\\noindent{\\fontsize{60}{66}\\selectfont \\strata}\\par\n')
pix, pdf = page(pre, 'big', body)
check('a page with the name is made', pix is not None, pdf)
new = measure(pix) if pix else None
check('the page has ink', new is not None)
if new:
    hi, asp, stroke = good_s(new)
    check('the S is as high as the word (within one pixel at 300 dpi)', hi,
          new)
    check('the S has the width of the logo S (46 to 43.5 of its height)',
          asp, new)
    check('the arm of the S has the stroke of the stems of the Ts', stroke,
          new)

# The text of the PDF keeps the letter S before RA.
if pdf:
    txt = subprocess.run(['pdftotext', pdf, '-'], capture_output=True,
                         text=True).stdout
    check('the text of the PDF has the letter S before RA',
          re.search(r'S\s*RA\s*A', txt) is not None, txt[:80])

# The same measurements on the S of the font: every one must reject it.
old_pre = pre + '\\renewcommand{\\strlputS}{S}\n'
opix, opdf = page(old_pre, 'old', body)
old = measure(opix) if opix else None
check('the control page with the S of the font is made', old is not None,
      opdf)
if old:
    hi, asp, stroke = good_s(old)
    check('control: the S of the font fails the height test', not hi, old)
    check('control: the S of the font fails the width test', not asp, old)
    check('control: the S of the font fails the stroke test', not stroke, old)

# Many names: the time of the run against the run with the S of the font.
many = ''.join('A line with \\strata in it, and \\strata again.\\par\n'
               for _ in range(200))
t0 = time.time()
mp, _ = page(pre, 'many', many, dpi=40)
t_new = time.time() - t0
t0 = time.time()
mo, _ = page(old_pre, 'manyold', many, dpi=40)
t_old = time.time() - t0
check('a page with 400 names is made', mp is not None and mo is not None)
check('400 names cost no more than 2 times the S of the font + 2 s '
      '(new %.1f s, old %.1f s)' % (t_new, t_old), t_new <= 2 * t_old + 2)

shutil.rmtree(tmp, ignore_errors=True)
print('strataname: %s' % ('all passed' if not fails else '%d FAILURES'
                         % fails))
sys.exit(1 if fails else 0)
