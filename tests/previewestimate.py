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

"""The page estimate of the preview against the real page count.

The preview says "Page 1 of about N" before LaTeX has run.  The estimate
(latex_estimate_pages, shown by the program pvest) has to count the pages
that the print makes of the document's own settings besides the text, that
is the contents, the lists and the index, and the room of the pictures and
the tables.  The test prints four documents and checks that the estimate is
within 10 percent of the pages of the PDF: a letter, the structured sample,
a generated document of about a hundred pages (tests/mkbig.py) and the
Reference Manual.  It also checks that the estimate of a document with its
contents and lists is larger than the estimate of the same document when
the lists are switched off in the document.

The programs are named by the variables STRPRINT, PVEST and
STRATA_BUILD_DIR, as for the other scripts.
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
                                                    'strprint'))
pvest = os.environ.get('PVEST', os.path.join(os.path.dirname(strprint),
                                             'pvest'))

for tool in ('pdflatex', 'latex', 'dvipdfmx', 'pdfinfo'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)

if not os.path.exists(strprint) or not os.path.exists(pvest):
    print('SKIP: build the programs first (strata-build test makes pvest)')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='pest')
os.environ['TMPDIR'] = tmp
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
                                                    + str(extra)))
    fails += 0 if cond else 1


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    return r.returncode, r.stdout, r.stderr


RELEASE = ['--special', '.STRATA_BUILD_TYPE', 'release']


def real_pages(doc, extra=()):
    out = os.path.join(tmp, 'r.pdf')
    run([strprint, doc, '-o', out, '--no-banner'] + list(extra))
    rc, so, se = run(['pdfinfo', out])
    m = re.search(r'Pages:\s+(\d+)', so)
    return int(m.group(1)) if m else 0


def estimate(doc, extra=()):
    rc, so, se = run([pvest] + list(extra) + [doc])
    w = so.split()
    return int(w[1]) if len(w) > 1 else 0


def compare(label, doc, extra=()):
    real = real_pages(doc, extra)
    est = estimate(doc, extra)
    err = abs(est - real) * 100.0 / real if real > 0 else 100.0
    check('%s: the estimate %d is within 10 percent of the %d real pages '
          '(%.1f percent)' % (label, est, real, err), real > 0 and err <= 10.0)
    return real, est


compare('a letter', os.path.join(root, 'samples', 'letters',
                                  'complaint-to-utility-us.doc'))
story = os.path.join(root, 'samples', 'story', 'strata-story.doc')
real, est = compare('the structured sample', story)
# The estimate follows the edition: the story prints some sections only in
# a developer build (If markers on .STRATA_BUILD_TYPE), and the estimate
# counts the branches that the variables of the process choose, as the print
# does.
real_rel, est_rel = compare('the structured sample, release edition', story,
                            RELEASE)
check('the release edition of the structured sample has fewer pages, real '
      '(%d against %d) and estimated (%d against %d)' % (real_rel, real,
                                                       est_rel, est),
      real_rel < real and est_rel < est and real - real_rel >= 3 and
      est - est_rel >= 3)

# The same document with every generated part switched off in the document:
# the contents level 0 and the four lists off.  The estimate must be smaller,
# by the pages that these parts take.
raw = open(story, 'rb').read()
m = re.search(rb'MR[0-9]+\r\n', raw)
at = m.end() if m else raw.index(b'\r\n') + 2
raw = re.sub(rb'\r\nYCL[0-9]+(?=\r\n)', b'', raw)
raw = re.sub(rb'\r\nFZL[FPTI][01](?=\r\n)', b'', raw)
extra = b'YCL0\r\nFZLF0\r\nFZLP0\r\nFZLT0\r\nFZLI0\r\n'
off = os.path.join(tmp, 'off.doc')
open(off, 'wb').write(raw[:at] + extra + raw[at:])
est_off = estimate(off)
check('the structured sample with the contents and lists switched off has '
      'fewer estimated pages (%d against %d, at least 5 fewer)' % (est_off,
                                                                    est),
      est - est_off >= 5)

# A list that has no named marker prints no page, whether it is on or off:
# the Picture markers of the story have no names, so the list of pictures
# adds nothing to the print and nothing to the estimate, switched on
# (FZLP1), off (FZLP0) or automatic (the document as it is).  The entries
# counted are the named markers, not the counter of the markers.
# The variants stand in the folder of the test: the pictures of the story
# are copied next to them, so that they print as the story does.
shutil.copytree(os.path.join(root, 'samples', 'story', 'pictures'),
                os.path.join(tmp, 'pictures'), dirs_exist_ok=True)
raw = open(story, 'rb').read()
m = re.search(rb'MR[0-9]+\r\n', raw)
at = m.end() if m else raw.index(b'\r\n') + 2
raw = re.sub(rb'\r\nFZLP[01](?=\r\n)', b'', raw)
for state in (b'1', b'0'):
    var = os.path.join(tmp, 'pic%s.doc' % state.decode())
    open(var, 'wb').write(raw[:at] + b'FZLP' + state + b'\r\n' + raw[at:])
    real_v = real_pages(var)
    est_v = estimate(var)
    check('the list of pictures set to %s with no named Picture marker '
          'adds no page: real %d against %d, estimate %d against %d' %
          ('on' if state == b'1' else 'off', real_v, real, est_v, est),
          real_v == real and est_v == est)

big = os.path.join(tmp, 'big')
os.makedirs(big)
rc, so, se = run([sys.executable, '-I', '-B', os.path.join(here, 'mkbig.py'),
                  big, '4'])
bigdoc = os.path.join(big, 'big.doc')
check('a generated document of about a hundred pages is made',
      rc == 0 and os.path.exists(bigdoc), se[-300:])

if os.path.exists(bigdoc):
    compare('a generated document of about a hundred pages', bigdoc)

manual = os.path.join(root, 'manual', 'doc', 'strata-reference-manual.doc')

if os.path.exists(manual):
    compare('the Reference Manual', manual)
else:
    print('SKIP: the Reference Manual is not made (build.sh single)')

shutil.rmtree(tmp, ignore_errors=True)
print('previewestimate: %s' % ('all passed' if fails == 0 else
                               '%d FAILURES' % fails))
sys.exit(1 if fails != 0 else 0)
