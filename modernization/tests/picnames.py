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

"""Pictures whose file names have spaces, accents and TeX characters.

Two features meet here: the one lookup rule for a picture file (the folder
of the document, then the working directory), and the long file names (a name
with a space, an accented letter, an ampersand, a percent sign, a number
sign, a dollar sign and an underscore must reach the file system, the Error
Log and TeX unharmed).  The test makes a document in a folder whose name has
a space and an ampersand, with two Picture markers: one whose file is there
and one whose file is not, and runs the Editor and the Printer from another
folder.  The name of the file that is there must be found by both programs
and written into the LaTeX so that TeX reads it as it is; the name of the
file that is not there must be told in the Editor, in the Error Log and in
the red box of the page, and the LaTeX must still be made and the PDF too."""
import sys as _s; _s.dont_write_bytecode = True
import atexit, os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui

build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
                                                     'strprint'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))

if not os.path.exists(stred) or not os.path.exists(strprint):
    print('SKIP: build the programs first')
    sys.exit(0)

CRLF = b'\r\n'
EOL = b'\x1d'
HEADER = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
          b'P1C33' + CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF +
          b'DP0,8500,11000' + CRLF)

fails = 0
tmp = tempfile.mkdtemp(prefix='picnames')
atexit.register(shutil.rmtree, tmp, True)
home = os.path.join(tmp, 'home')
docs = os.path.join(tmp, 'my docs & more')
away = os.path.join(tmp, 'away')

for d in (home, docs, away):
    os.makedirs(d, mode=0o700)

os.environ['STRATA_HOME'] = home
os.chdir(away)

# The names: a space, an accented letter, ampersand, percent, number sign,
# dollar sign and underscore.  The document keeps the name in the code page
# of the document format, the file system has the UTF-8 of it.
GOOD = 'my pic \u00e4 & 5% #1 $x_y.png'
GONE = 'gone \u00e4 & 7% #2 $z_y.png'
shutil.copy(os.path.join(here, 'data', 'picture.png'),
            os.path.join(docs, GOOD))


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def doc(name, pics):
    """A document of a line, a Picture marker for each name, and a line."""
    out = HEADER + b'*B0' + CRLF + b'S' + CRLF
    blocks = [b'Before.' + EOL]
    for n in pics:
        blocks.append(b'\x1325(1' + n.encode('cp437') + b'\x00' + CRLF +
                      b'22000' + CRLF + b'\x13)' + EOL)
    blocks.append(b'After.' + EOL)
    first = True
    for b in blocks:
        out += (b'' if first else b'B' + CRLF) + b'T' + b + b'\x00' + CRLF
        first = False
    path = os.path.join(docs, name)
    with open(path, 'wb') as f:
        f.write(out + b'*E' + CRLF)
    return path


def run(path, *extra):
    return subprocess.run([strprint, path, '--no-banner'] + list(extra),
                          capture_output=True, text=True, cwd=away)


# ------------------------------------------------------- the file is there
d_good = doc('good.doc', [GOOD])
dumps = tui.session([stred, '-H', helpf, d_good],
                    ['waitfor:After.', 'show'], rows=14, cols=78,
                    env={'STRATA_HOME': home})
check('picnames: the Editor finds a picture whose name has a space, an '
      'accent and TeX characters, in the folder of the document, when it is '
      'started from another folder',
      'not found' not in dumps[0] and 'PIC' in dumps[0], dumps[0])

tex = os.path.join(tmp, 'good.tex')
p = run(d_good, '--tex-only', '-o', tex)
text = open(tex, encoding='utf-8', errors='replace').read() \
    if os.path.exists(tex) else ''
check('picnames: the Printer finds it too (status 0, no missing file '
      'told)', p.returncode == 0 and 'not found' not in p.stderr, p.stderr)
check('picnames: the pictures are copied to names of their own, so the '
      'LaTeX holds no part of the odd name',
      '\\strpic' in text and 'pic1.png' in text and '5%' not in
      text.split('% Pictures used')[0], text[-500:])

# With --pictures keep the file is named in the LaTeX as it is: a name with
# a percent sign and a number sign must go through \detokenize with the two
# characters made into macros, or TeX reads them as a comment and a
# parameter.  (An accented letter cannot be named there: that is told.)
KEEP = 'my pic & 5% #1 $x_y.png'
shutil.copy(os.path.join(here, 'data', 'picture.png'),
            os.path.join(docs, KEEP))
d_keep = doc('keep.doc', [KEEP])
tex_k = os.path.join(tmp, 'keep.tex')
p = run(d_keep, '--tex-only', '--pictures', 'keep', '-o', tex_k)
text_k = open(tex_k, encoding='utf-8', errors='replace').read() \
    if os.path.exists(tex_k) else ''
check('picnames: with the pictures kept where they are, the name goes into '
      'the LaTeX through \\detokenize, the percent sign and the number sign '
      'written as macros that TeX does not read as a comment or a parameter',
      '\\detokenize' in text_k and '@percentchar' in text_k and
      '\\string#' in text_k, text_k[-700:])
pdf_k = os.path.join(tmp, 'keep.pdf')
p = run(d_keep, '--pictures', 'keep', '-o', pdf_k)
check('picnames: and TeX makes the PDF of it with the picture in it',
      p.returncode == 0 and os.path.exists(pdf_k) and
      (shutil.which('pdfimages') is None or
       len(subprocess.run(['pdfimages', '-list', pdf_k],
                          capture_output=True, text=True
                          ).stdout.strip().split('\n')) > 2), p.stderr)

pdf = os.path.join(tmp, 'good.pdf')
p = run(d_good, '-o', pdf)
check('picnames: the PDF is made and holds the picture',
      p.returncode == 0 and os.path.exists(pdf) and
      (shutil.which('pdfimages') is None or
       len(subprocess.run(['pdfimages', '-list', pdf], capture_output=True,
                          text=True).stdout.strip().split('\n')) > 2),
      p.stderr)

# ---------------------------------------------------- the file is not there
d_gone = doc('gone.doc', [GONE])
dumps = tui.session([stred, '-H', helpf, d_gone],
                    ['waitfor:After.', 'show'], rows=14, cols=78,
                    env={'STRATA_HOME': home})
check('picnames: the Editor reports that the file is not found, with the name',
      'file not found' in dumps[0] and 'gone' in dumps[0], dumps[0])

errs = os.path.join(tmp, 'gone.err')
pdf2 = os.path.join(tmp, 'gone.pdf')
p = run(d_gone, '-o', pdf2, '--errors', errs)
log = open(errs, encoding='utf-8', errors='replace').read() \
    if os.path.exists(errs) else ''
check('picnames: the Printer reports the missing file on a line of its own',
      'picture file' in p.stderr and 'not found' in p.stderr, p.stderr)
import urllib.parse
mm = re.search(r' m=(\S+)', log)
msg = urllib.parse.unquote_to_bytes(mm.group(1)).decode('cp437') \
    if mm else ''
check('picnames: the Error Log names the file as written, with every '
      'special character, and the places that were looked in (the folder '
      'of the document, with its space and ampersand, first)',
      GONE in msg and 'looked in' in msg and
      (os.path.realpath(docs) in msg or docs in msg), msg)
check('picnames: the PDF is made in spite of the missing picture',
      os.path.exists(pdf2), p.stderr)
if shutil.which('pdftotext') is not None and os.path.exists(pdf2):
    words = subprocess.run(['pdftotext', pdf2, '-'], capture_output=True,
                           text=True).stdout
    # The Error Log pages follow the body and name the file too: the box is
    # what comes before them.
    flat = re.sub(r'\s+', ' ', words).split(' Error Log ')[0]
    check('picnames: the red box of the page says Picture file not found '
          'with the name, the ampersand, the percent sign, the number '
          'sign, the dollar sign and the underscore as they are',
          'Picture file not found: gone' in flat and '& 7%' in flat and
          '#2' in flat and '$z_y.png' in flat, flat[:400])

sys.exit(1 if fails else 0)
