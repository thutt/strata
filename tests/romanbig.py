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

"""Roman numerals above 3999.  A roman numeral shows the numbers 1 to 3999;
a number that the style cannot show is written in arabic digits.  The
counters and the tags of the sections do it in the Editor and in the print
(tests/edtest.c tests the function); this script tests the two places that
the Printer numbers itself: the numbers of the lines (Global Format, Line
Numbering, Number Format Roman or Capital Roman) and the page numbers when
the page number tag is a roman numeral.  A document cannot have 4000 pages
in a test, so the exported LaTeX is changed to start the counter near 4000
(the numbers of the pages and of the lines are those of TeX), and the PDF is
made from it: 3999 is MMMCMXCIX and the next number is 4000."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
data = os.path.join(here, 'data')
build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
    'strprint'))
for tool in ('pdflatex', 'pdftotext'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='romanbig')
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


def variant(src, name, records, swap=None):
    """A copy of a test document with global F records added after the
    revision record, and the bytes swap[0] replaced by swap[1] if given."""
    raw = open(os.path.join(data, src), 'rb').read()
    if swap is not None:
        assert swap[0] in raw, swap
        raw = raw.replace(swap[0], swap[1], 1)
    m = re.search(rb'MR[0-9]+\r\n', raw)
    at = m.end() if m else raw.index(b'\r\n') + 2
    extra = b''.join(b'F' + r.encode() + b'\r\n' for r in records)
    path = os.path.join(tmp, name + '.doc')
    open(path, 'wb').write(raw[:at] + extra + raw[at:])
    return path


def tex_of(doc, name):
    out = os.path.join(tmp, name + '.tex')
    run([strprint, doc, '--tex-only', '-o', out, '--docdir', data])
    return open(out).read() if os.path.exists(out) else ''


def pdf_text(tex, name):
    """Make the PDF of LaTeX text in its own directory and return the text
    of every page."""
    d = os.path.join(tmp, name)
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, 'doc.tex')
    open(path, 'w').write(tex)
    for i in range(2):
        run(['pdflatex', '-interaction=nonstopmode', '-halt-on-error',
             'doc.tex'], cwd=d)
    pdf = os.path.join(d, 'doc.pdf')
    if not os.path.exists(pdf):
        return None
    rc, out, err = run(['pdftotext', '-layout', pdf, '-'])
    return out.split('\f')


# The numbers of the lines: Running (ZM2) with the format Capital Roman (ZF2)
# and Roman (ZF1).  The lines are numbered from 3997 on.
for fmt, name, small in ((2, 'capital', 'MMMCMXCIX'),
                         (1, 'lower', 'mmmcmxcix')):
    doc = variant('print_lineno.doc', 'ln' + name, ['ZM2', 'ZF%d' % fmt])
    tex = tex_of(doc, 'ln' + name)
    check('line numbers, format %s: the number is made arabic above 3999 in '
        'the LaTeX' % name, '\\ifnum\\value{linenumber}>3999 '
        '\\arabic{linenumber}' in tex, tex[-400:])
    check('line numbers, format %s: a document without the setting has no '
        'such text' % name,
        '\\ifnum\\value{linenumber}>3999' not in tex_of(
            os.path.join(data, 'print_lineno.doc'), 'plain' + name))
    start = tex.replace('\\linenumbers',
        '\\setcounter{linenumber}{3996}\\linenumbers', 1)
    pages = pdf_text(start, 'lnpdf' + name)
    allt = ' '.join(pages) if pages else ''
    words = re.findall(r'[A-Za-z]+|\d+', allt)
    check('line numbers, format %s: line 3999 is %s and line 4000 is 4000'
        % (name, small), small in words and '4000' in words and
        '4001' in words, words[:30])
    check('line numbers, format %s: no roman numeral above 3999 (no MMMM)'
        % name, not any(w.lower().startswith('mmmm') for w in words), words)


# The page numbers: the page number tag \I\ (PP) makes them roman.
doc = variant('plain_text.doc', 'pgroman', [], swap=(b'PP\\1\\', b'PP\\I\\'))
tex = tex_of(doc, 'pgroman')
check('page numbers: the LaTeX makes the number arabic above 3999',
    '\\ifnum\\value{page}>3999 \\arabic{page}\\else\\Roman{page}\\fi' in tex,
    tex[:200])
plain = tex_of(os.path.join(data, 'plain_text.doc'), 'pgplain')
check('page numbers: a document with arabic page numbers has no such text',
    '\\ifnum\\value{page}>3999' not in plain)
m = re.search(r'\\pagenumbering\{Roman\}\n(\\renewcommand[^\n]*\n)', tex)
check('page numbers: the numbering of the body is Roman, then the rule',
    m is not None, tex[tex.find('\\begin{document}'):][:300])
if m is not None:
    start = tex.replace(m.group(0), m.group(0) + '\\setcounter{page}{3999}\n',
        1)
    start = start.replace('\\end{document}', '\\newpage\\null\\newpage\\null\n'
        '\\end{document}', 1)
    pages = pdf_text(start, 'pgpdf')
    words = []
    for p in pages or []:
        words += re.findall(r'[A-Za-z]{4,}|\d{4}', p)
    check('page numbers: page 3999 is MMMCMXCIX and page 4000 is 4000 in '
        'the PDF', 'MMMCMXCIX' in words and '4000' in words and
        '4001' in words, words[:20])
    check('page numbers: no page number is MMMM', not any(
        w.startswith('MMMM') for w in words), words)

print('romanbig: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
