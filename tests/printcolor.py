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

"""The colours of a print and the setting Print in Black and White.

A document of the product holds no colour (docs/PRINT-LATEX.txt): the colours
of a print are the fixed ones of the Printer (the Print Error markers and the
banner of a failed print are red and orange).  The setting turns them into
greys: strprint --black-and-white, stred --print --black-and-white and the
Print panel field Print in Black and White? (default No).  Checks: the LaTeX
text has one more line with the setting and is the same without it; a page of
a print is drawn and its red pixels counted; a document with no colour in it
prints the same pages with and without the setting.
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui

bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
stred = os.environ.get('STRED', os.path.join(bdir, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(bdir, 'strata.hlp'))
for tool in ('pdflatex', 'latex', 'dvipdfmx', 'pdftoppm'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
fails = 0
tmp = tempfile.mkdtemp(prefix='pcol')
data = os.path.join(here, 'data')
LINE = '\\PassOptionsToPackage{gray}{xcolor}\n'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def run(args, **kw):
    return subprocess.run(args, capture_output=True, text=True, cwd=tmp, **kw)


def fresh(src, name):
    path = os.path.join(tmp, name)
    shutil.copy(os.path.join(data, src), path)
    shutil.copytree(os.path.join(data, 'pics'), os.path.join(tmp, 'pics'),
                    dirs_exist_ok=True)
    shutil.copy(os.path.join(data, 'picture.png'), tmp)
    return path


def page_pixels(pdf):
    """The pixels of page 1 at 50 dpi as (r, g, b) triples."""
    stem = pdf[:-4]
    subprocess.run(['pdftoppm', '-r', '50', '-f', '1', '-l', '1', '-singlefile',
        pdf, stem], check=True)
    ppm = subprocess.run(['pdftoppm', '-r', '50', '-f', '1', '-l', '1',
        pdf], capture_output=True).stdout
    d = ppm.split(b'\n', 3)
    px = d[3]
    return [(px[i], px[i + 1], px[i + 2]) for i in range(0, len(px) - 2, 3)]


def red(px):
    return sum(1 for r, g, b in px if r > 170 and g < 90 and b < 90)


def ink(px):
    return sum(1 for r, g, b in px if r < 200 or g < 200 or b < 200)


bad = fresh('print_errors.doc', 'bad.doc')
good = fresh('print_orient.doc', 'good.doc')

# The LaTeX text.
for name, src in (('bad', bad), ('good', good)):
    # The same name in two directories: the text names its file.
    os.makedirs(os.path.join(tmp, name + '-c'))
    os.makedirs(os.path.join(tmp, name + '-g'))
    a = os.path.join(tmp, name + '-c', 'x.tex')
    b = os.path.join(tmp, name + '-g', 'x.tex')
    run([strprint, src, '--export-latex', a])
    run([strprint, src, '--export-latex', b, '--black-and-white'])
    ta = open(a).read() if os.path.exists(a) else ''
    tb = open(b).read() if os.path.exists(b) else ''
    check('%s: without the setting the text has no gray option' % name,
        ta != '' and 'PassOptionsToPackage{gray}' not in ta, ta[:200])
    check('%s: with the setting the text has the gray option for xcolor'
        % name, LINE in tb, tb[:300])
    GREY = re.compile(r'\\ifdefined\\pdfliteral\n\\newsavebox.*?'
                      r'\\makeatother\n\\fi\n', re.S)
    check('%s: with the setting the text also has the grey cover of the '
        'pictures, and not without it' % name,
        GREY.search(tb) is not None and GREY.search(ta) is None)
    check('%s: and is the same text otherwise' % name,
        GREY.sub('', tb.replace(LINE, '', 1), 1) == ta)

# The pages.
for src, name in ((bad, 'bad'), (good, 'good')):
    c = os.path.join(tmp, name + '-c.pdf')
    g = os.path.join(tmp, name + '-g.pdf')
    run([strprint, src, '-o', c])
    run([strprint, src, '-o', g, '--black-and-white'])
    pc = page_pixels(c)
    pg = page_pixels(g)
    if name == 'bad':
        check('a print with errors has red pixels on page 1 (the banner)',
            red(pc) > 20, red(pc))
        check('with Black and White the page has no red pixel and still has '
            'ink', red(pg) == 0 and ink(pg) > 100, (red(pg), ink(pg)))
        check('and its pixels are greys (the same red, green and blue)',
            all(abs(r - g_) < 12 and abs(g_ - b) < 12 for r, g_, b in pg))
    else:
        check('a document with no colour in the print: the same page with '
            'and without the setting', pc == pg and red(pc) == 0)

# stred --print passes the setting on.
out = os.path.join(tmp, 'stred.pdf')
run([stred, '--print', bad, '-o', out, '--black-and-white'])
check('stred --print --black-and-white makes a page with no red pixel',
    os.path.exists(out) and red(page_pixels(out)) == 0 and
    ink(page_pixels(out)) > 100)

# The Print panels have the field, and it starts at No.
os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home', dir=tmp)
for keys, what in ((['F12', 'text:P', 'ENTER'], 'Draft'),
                   (['F12', 'text:P', 'RIGHT', 'ENTER'], 'Final')):
    # Wait for the panel itself (up to 20 seconds): a fixed wait reads the
    # menu on a loaded machine.
    d = tui.session([stred, '-H', helpf, bad],
                    keys + ['waitfor:Print in Black and White?', 'show'],
                    rows=30)
    for fld in ('Index?', 'List of Figures?', 'List of Tables?'):
        line = [l for l in d[0].split('\n') if fld in l]
        check('the %s panel has %s, and it is No' % (what, fld),
            len(line) == 1 and '[No]' in line[0], d[0])
    line = [l for l in d[0].split('\n') if 'Print in Black and White?' in l]
    check('the %s panel has Print in Black and White?, and it is No' % what,
        len(line) == 1 and '[No]' in line[0], d[0])

shutil.rmtree(tmp)
print('printcolor: %s' % ('all passed' if not fails else '%d FAILURES'
                         % fails))
sys.exit(1 if fails else 0)
