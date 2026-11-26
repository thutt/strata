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

"""The names and signs that a manual writes as LaTeX markers are plain words
on the screen of a terminal, and the Font markers of a manual are not shown.

The documents of the manual write the name Strata, the names LaTeX and TeX,
the company name, the section sign and the word Appendix as LaTeX markers,
which a window draws as pictures.  A terminal cannot draw them, and showed
the label [LaTeX instructions] in their place.  This test makes a document
with the generator of the manual and reads the screen of the terminal Editor:
the names are words, a LaTeX marker with other text keeps its label, a
document that says .fontmarkers short shows [FONT], and a document of the
manual does not.  Run on its own:
    python3 -I -B tests/logomark.py
"""
import sys as _s; _s.dont_write_bytecode = True
import atexit, os, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
gen = os.path.join(root, 'manual', 'mkmanual.py')
if not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
# The scratch folder is made where TMPDIR says, and is removed at the end.
tmp = tempfile.mkdtemp(prefix='logomark')
atexit.register(shutil.rmtree, tmp, True)
os.environ['STRATA_HOME'] = os.path.join(tmp, 'home')
os.makedirs(os.environ['STRATA_HOME'], mode=0o700)


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def make(name, text):
    """Make a document from the text of a source with the generator of the
    manual; return its path."""
    src = os.path.join(tmp, name + '.src')
    doc = os.path.join(tmp, name + '.doc')
    with open(src, 'w') as f:
        f.write(text)
    r = subprocess.run([sys.executable, '-I', '-B', gen, src, doc],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr + r.stdout
    return doc


def screen(path):
    """The first screen of the Editor on a document, 100 columns wide."""
    return tui.session([stred, '-H', helpf, path], ['show'], rows=30,
                       cols=100)[0]


# {{ and }} are the braces of the text of a LaTeX marker.
names = make('names', '.doc Names\n.h1 Names\n'
             'a {latex:\\strata} b {latex:\\LaTeX{{}}} c {latex:\\TeX{{}}} '
             'd {latex:\\lms} e\n\n'
             'f {latex:\\S{{}}}7 g {latex:\\mbox{{Appendix}}~}B '
             'h {latex:\\stratalogo[3]} i\n')
d = screen(names)
check('a name or a sign written as a LaTeX marker is a plain word on the '
      'screen of a terminal',
      'a Strata b LaTeX c TeX d Logic Magicians Software e' in d and
      'f Sec. 7 g Appendix B h Strata i' in d, d)
check('no label of a LaTeX marker is drawn in its place',
      'instructions' not in d, d)

other = make('other', '.doc Other\n.h1 Other\n'
             'a {latex:\\textbf{{raw}}} b {latex:\\strataX} c '
             '{latex:\\mbox{{Two words}}~} d\n')
d = screen(other)
check('a LaTeX marker with other text keeps its label',
      d.count('[LaTeX instructions]') == 3, d)

fonts = make('fonts', '.doc Fonts\n.h1 Fonts\n'
             'See {c:dvipng} and {c:hunspell}.\n')
d = screen(fonts)
check('the Font markers of a document of the manual are not shown',
      'See dvipng and hunspell.' in d and '[FONT]' not in d, d)

shown = make('shown', '.doc Shown\n.fontmarkers short\n.h1 Shown\n'
             'See {c:dvipng}.\n')
d = screen(shown)
check('a document that says .fontmarkers short shows the Font markers',
      'See [FONT]dvipng[FONT].' in d, d)

bad = os.path.join(tmp, 'bad.src')
with open(bad, 'w') as f:
    f.write('.doc Bad\n.fontmarkers loud\n.h1 Bad\nText.\n')
r = subprocess.run([sys.executable, '-I', '-B', gen, bad,
                    os.path.join(tmp, 'bad.doc')], capture_output=True,
                   text=True)
check('.fontmarkers with another word is an error of the source',
      r.returncode != 0 and 'hidden or short' in r.stderr + r.stdout,
      (r.returncode, r.stdout, r.stderr))

# The saved document of a manual holds the record and reads back unchanged.
data = open(fonts, 'rb').read()
check('the document of a manual holds the record OM15,4 of the Font '
      'markers', b'OM15,4\r\n' in data, data[:200])
data = open(shown, 'rb').read()
check('a document that says .fontmarkers short holds no record of it',
      b'OM15,' not in data, data[:200])

print('logomark.py: %s' % ('FAILED' if fails else 'ok'))
sys.exit(1 if fails else 0)
