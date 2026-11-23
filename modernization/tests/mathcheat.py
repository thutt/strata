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

"""The Math Symbols card: the card is made from the list of the symbols
(manual/mathsyms.txt), printed by the Printer, and checked.  Every entry
must be typeset by the Printer without a fault of an equation, every symbol
that the packages define must be on the card or in the list of the symbols
that are left out, and the printed card must have two pages, with the text
inside the margins of 5 mm."""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
manual = os.path.join(root, 'manual')
bdir = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))
for tool in ('pdflatex', 'pdftotext', 'pdfinfo', 'kpsewhich'):
    if not shutil.which(tool):
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
fails = 0


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def run(args, **kw):
    return subprocess.run(args, capture_output=True, text=True, **kw)


tmp = tempfile.mkdtemp(prefix='mathcheat')
doc = os.path.join(tmp, 'mathcheat.doc')
pdf = os.path.join(tmp, 'mathcheat.pdf')

r = run([sys.executable, '-I', '-B', os.path.join(manual, 'mkmathcheat.py')])
check('no panel of the card has more entries than it has room for',
      r.returncode == 0, r.stdout + r.stderr)

r = run([sys.executable, '-I', '-B', os.path.join(manual, 'mkmanual.py'),
         os.path.join(manual, 'src', 'mathcheat.src'), doc])
check('the card is made from the list', r.returncode == 0 and
      os.path.exists(doc), r.stdout + r.stderr)

r = run([strprint, doc, '-o', pdf])
check('the Printer prints the card without a fault of an equation',
      r.returncode == 0 and os.path.exists(pdf), r.stderr[-1500:])

r = run([sys.executable, '-I', '-B', os.path.join(manual, 'checkmathsyms.py'),
         strprint, pdf])
check('every entry is typeset, every symbol of the packages is accounted '
      'for and the layout of the pages is right', r.returncode == 0,
      r.stdout[-2500:] + r.stderr[-500:])
for line in r.stdout.split('\n'):
    if line.startswith(('entries typeset', 'all ', 'layout of')):
        print('     ' + line)

# The list has the sheet's main groups, and the sheet has equations, not
# pictures: the document holds equation markers (type 9).
data = open(doc, 'rb').read()
check('the card holds its symbols as equation markers',
      data.count(b'\x139(') > 500, data.count(b'\x139('))

shutil.rmtree(tmp, ignore_errors=True)
print('mathcheat: %d failures' % fails)
sys.exit(1 if fails else 0)
