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

"""The marks of Logic Magicians Software, the commands \\lms and \\lmslogo.

Every print defines both commands in its preamble (logo/lmslogo.def, which
src/logodata.c holds as strings).  Documents made here use them in a LaTeX
marker: in a sentence, before punctuation, in the cell of a table and alone
in a block.  The PDF of a Final print must have the name three or more times
in an italic font with no space before a comma or a full stop, the mark must
draw a gate with its inputs, the letters LMS and a star at the scale that was
asked for, and the Draft print and the print in black and white must draw the
same mark.  A file that uses the commands in a headline, in the contents and
in a header made with the package fancyhdr must run LaTeX without an error.
Every one of these checks is run again on a control: the same print with the
two commands made empty, which every check must reject."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
for tool in ('pdflatex', 'pdftoppm', 'pdftotext', 'pdffonts'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
fails = 0
tmp = tempfile.mkdtemp(prefix='lmsm', dir=os.environ.get('TMPDIR'))
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)

CRLF = b"\r\n"
EOL = b"\x1d"
HEADER = (b"*R3" + CRLF + b"MR1001" + CRLF + b"PP\\1\\\x00" + CRLF +
          b"P1C33" + CRLF + b"FHA1" + CRLF + b"FTT5" + CRLF +
          b"DP0,8500,11000" + CRLF)
# The text that a control print puts before the document: both commands
# made empty, as they would be if the definition did not work.
EMPTY = ('\\renewcommand{\\lms}{}\n'
         '\\renewcommand{\\lmslogo}[1][1]{}\n')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def run(args, cwd=None):
    return subprocess.run(args, capture_output=True, text=True,
                          cwd=cwd or tmp)


def marker(text):
    """A LaTeX marker (type 41) with a text of one line."""
    return b"\x1341(S" + CRLF + b"T" + text + EOL + b"\x00" + CRLF + b"\x13)"


def block(text, first=False):
    return (b"" if first else b"B" + CRLF) + b"T" + text + b"\x00" + CRLF


def document(blocks):
    out = HEADER + b"*B0" + CRLF + b"S" + CRLF
    for i, b in enumerate(blocks):
        out += block(b, i == 0)
    return out + b"*E" + CRLF


def make_doc(name, blocks):
    path = os.path.join(tmp, name)
    with open(path, 'wb') as f:
        f.write(document(blocks))
    return path


lms = marker(b"\\lms")
# The text document: a sentence, the name before a comma and a full stop,
# and a table with the name and a small mark in its cells.
text_doc = make_doc('text.doc', [
    b"The company is " + lms + b" and it makes the tools." + EOL,
    lms + b", then " + lms + b"." + EOL,
    marker(b"\\begin{tabular}{|l|l|}\\hline \\lms & \\lmslogo[0.5] \\\\ "
           b"\\hline \\end{tabular}") + EOL])
# The mark alone, twice as big as the default, in a block of its own.
logo_doc = make_doc('logo.doc', [
    marker(b"\\begin{center}\\lmslogo[2]\\end{center}") + EOL])


def export_tex(doc, name):
    """The LaTeX file of a print of the document."""
    tex = os.path.join(tmp, name + '.tex')
    r = run([strprint, doc, '--export-latex', tex, '--no-banner'])
    if r.returncode != 0 or not os.path.exists(tex):
        print('FAIL the LaTeX of a print could not be made\n' + r.stderr)
        sys.exit(1)
    return open(tex, encoding='utf-8').read()


def compile_tex(text, name, passes=2):
    """Run pdflatex on the text; return (PDF path or None, log tail)."""
    tex = os.path.join(tmp, name + '.tex')
    with open(tex, 'w', encoding='utf-8') as f:
        f.write(text)
    r = None
    for _ in range(passes):
        r = run(['pdflatex', '-interaction=nonstopmode', '-halt-on-error',
                 name + '.tex'])
    pdf = os.path.join(tmp, name + '.pdf')
    return (pdf if os.path.exists(pdf) and r.returncode == 0 else None,
            r.stdout[-500:])


def control(text):
    """The LaTeX file with both commands made empty."""
    return text.replace('\\begin{document}', EMPTY + '\\begin{document}', 1)


def pdf_text(pdf):
    return run(['pdftotext', '-layout', pdf, '-']).stdout


def italic_fonts(pdf):
    out = run(['pdffonts', pdf]).stdout
    return [l.split()[0] for l in out.split('\n')[2:] if l.strip()
            and re.search(r'Italic|-It\b|CMTI|LMRoman\d+-Italic', l.split()[0])]


def ink_box(pdf, dpi=100):
    """The box and the count of the ink in the upper half of page 1:
    (left, top, right, bottom, count) or None."""
    base = os.path.join(tmp, os.path.basename(pdf)[:-4] + '-px')
    run(['pdftoppm', '-r', str(dpi), '-gray', '-f', '1', '-l', '1', pdf,
         base])
    pgm = [f for f in os.listdir(tmp) if f.startswith(
        os.path.basename(base) + '-') and f.endswith('.pgm')]
    if not pgm:
        return None
    data = open(os.path.join(tmp, pgm[0]), 'rb').read()
    m = re.match(rb'P5\s+(\d+)\s+(\d+)\s+(\d+)\s', data)
    w, h = int(m.group(1)), int(m.group(2))
    raw = data[m.end():]
    xs, ys, count = [], [], 0
    for y in range(h // 2):
        row = raw[y * w:(y + 1) * w]
        for x in range(w):
            if row[x] < 128:
                xs.append(x)
                ys.append(y)
                count += 1
    if count == 0:
        return None
    return min(xs), min(ys), max(xs), max(ys), count


def text_checks(pdf, label):
    """The checks of the text document; returns the list of results."""
    txt = pdf_text(pdf)
    flat = ' '.join(txt.split())
    name = 'Logic Magicians Software'
    return [
        ('%s: the name is in the text four times (sentence, comma, full '
         'stop, table)' % label, flat.count(name) == 4),
        ('%s: no space before a comma or a full stop' % label,
         (name + ',') in flat and (name + '.') in flat),
        ('%s: the name is in an italic font' % label,
         len(italic_fonts(pdf)) > 0),
        ('%s: the name does not break across lines' % label,
         all(name in ' '.join(l.split()) or 'Logic' not in l
             for l in txt.split('\n') if 'Magicians' in l or 'Logic' in l)),
    ]


def logo_checks(pdf, label):
    """The checks of the mark: the box of its ink at 100 dpi."""
    box = ink_box(pdf)
    out = []
    ok = box is not None
    out.append(('%s: the mark draws ink' % label, ok))
    if not ok:
        return out + [('%s: the mark has the size of scale 2' % label, False),
                      ('%s: the mark has a star at its right end' % label,
                       False)]
    left, top, right, bottom, count = box
    width, height = right - left + 1, bottom - top + 1
    # Scale 2 at 11 pt: the width is about 5 cm (197 px at 100 dpi) and the
    # height about 2.3 cm, so that the gate is wider than high.
    out.append(('%s: the mark has the size of scale 2 (width %d px, height '
                '%d px)' % (label, width, height),
                165 <= width <= 215 and 0.30 * width <= height <=
                0.60 * width))
    # The star is a solid shape at the right end: many ink pixels in the
    # last 12 percent of the width, which a plain stroke would not give.
    out.append(('%s: the ink is dense at the right end (a star)' % label,
                count > 0 and width > 0 and star_density(pdf, box) > 0.30))
    return out


def star_density(pdf, box):
    """The share of ink pixels in the last 12 percent of the box of the
    mark (the star), counted in the middle rows of it."""
    left, top, right, bottom, count = box
    base = os.path.join(tmp, os.path.basename(pdf)[:-4] + '-px')
    pgm = [f for f in os.listdir(tmp) if f.startswith(
        os.path.basename(base) + '-') and f.endswith('.pgm')][0]
    data = open(os.path.join(tmp, pgm), 'rb').read()
    m = re.match(rb'P5\s+(\d+)\s+(\d+)\s+(\d+)\s', data)
    w = int(m.group(1))
    raw = data[m.end():]
    width = right - left + 1
    x0 = right - int(0.12 * width)
    n = ink = 0
    mid = (top + bottom) // 2
    for y in range(mid - 6, mid + 7):
        for x in range(x0, right + 1):
            n += 1
            ink += 1 if raw[y * w + x] < 128 else 0
    return ink / n


def print_pdf(doc, name, extra=()):
    pdf = os.path.join(tmp, name + '.pdf')
    r = run([strprint, doc, '-o', pdf, '--no-banner'] + list(extra))
    return pdf if r.returncode == 0 and os.path.exists(pdf) else None


# ---- The preamble of every print has both commands -------------------------
pre = export_tex(text_doc, 'a')
pre = pre[:pre.index('\\begin{document}')]
check('the preamble defines \\lms as a robust command',
      '\\DeclareRobustCommand{\\lms}' in pre)
check('the preamble defines \\lmslogo as a robust command',
      '\\DeclareRobustCommand{\\lmslogo}' in pre)
mine = pre[pre.index('% The Logic Magicians Software marks'):]
check('the definition loads no package: only TikZ, which every print has',
      '\\usepackage' not in mine and '\\usepackage' in pre and
      'tikz' in pre[:pre.index('% The Logic Magicians Software marks')])

# ---- The Final print --------------------------------------------------------
tpdf = print_pdf(text_doc, 'text')
check('the text document prints to a PDF', tpdf is not None)
if tpdf:
    for name, ok in text_checks(tpdf, 'Final'):
        check(name, ok)
lpdf = print_pdf(logo_doc, 'logo')
check('the document with the mark alone prints to a PDF', lpdf is not None)
if lpdf:
    for name, ok in logo_checks(lpdf, 'Final'):
        check(name, ok)
    check('Final: the letters LMS are in the text of the PDF',
          'LMS' in pdf_text(lpdf))

# ---- Draft and the print in black and white ---------------------------------
if lpdf:
    base_box = ink_box(lpdf)
    for opt, label in (('--draft', 'Draft'),
                       ('--black-and-white', 'Black and white')):
        p = print_pdf(logo_doc, 'logo-' + label.split()[0], [opt])
        check('%s: the document with the mark prints' % label, p is not None)
        if p:
            for name, ok in logo_checks(p, label):
                check(name, ok)
            box = ink_box(p)
            check('%s: the mark is the same as in Final (same box, ink '
                  'within 5 percent)' % label,
                  box is not None and base_box is not None and
                  box[:4] == base_box[:4] and
                  abs(box[4] - base_box[4]) <= 0.05 * base_box[4],
                  (box, base_box))
if tpdf:
    p = print_pdf(text_doc, 'text-bw', ['--black-and-white'])
    check('Black and white: the name still prints in italics',
          p is not None and len(italic_fonts(p)) > 0)

# ---- A headline, the contents and a header ----------------------------------
full = export_tex(text_doc, 'c')
head = ('\\usepackage{fancyhdr}\\pagestyle{fancy}\n'
        '\\fancyhead[C]{\\lms\\ \\lmslogo[0.4]}\n')
body = ('\\tableofcontents\\section{Notes on \\lms}\n'
        'Some text with the name \\lms{} in the middle.\n')
mixed = full.replace('\\begin{document}', head + '\\begin{document}\n' + body,
                     1)
mpdf, log = compile_tex(mixed, 'mixed')
check('a file with the name in a headline, the contents and a header runs '
      'LaTeX without an error', mpdf is not None, log)
if mpdf:
    first = run(['pdftotext', '-f', '1', '-l', '1', '-layout', mpdf,
                 '-']).stdout
    flat = ' '.join(first.split())
    check('the name is in the header, in the contents and in the headline '
          '(three or more times on the first page)',
          flat.count('Logic Magicians Software') >= 3, flat[:300])

# ---- The controls: the same checks must reject the empty commands -----------
cpdf, log = compile_tex(control(full), 'ctrl-text')
check('the control print of the text document is made', cpdf is not None, log)
if cpdf:
    # The control has the same text document; its text checks must fail.
    results = text_checks(cpdf, 'control')
    check('control: the name count check rejects empty commands',
          not results[0][1])
    check('control: the punctuation check rejects empty commands',
          not results[1][1])
    check('control: the italic check rejects empty commands',
          not results[2][1])
lfull = export_tex(logo_doc, 'd')
lc, log = compile_tex(control(lfull), 'ctrl-logo')
check('the control print of the mark is made', lc is not None, log)
if lc:
    results = logo_checks(lc, 'control')
    check('control: the ink check rejects an empty mark', not results[0][1])
    check('control: the size check rejects an empty mark', not results[1][1])
    check('control: the star check rejects an empty mark', not results[2][1])
if mpdf:
    cm, log = compile_tex(control(mixed), 'ctrl-mixed')
    flat = ' '.join(pdf_text(cm).split()) if cm else ''
    check('control: the header check rejects empty commands',
          cm is not None and flat.count('Logic Magicians Software') < 3,
          flat[:200])

sys.exit(1 if fails else 0)
