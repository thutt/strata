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

"""The document Errata.

    python3 -I -B tests/errata.py

The Errata lists the known anomalies of the terminal programs.  The test
checks that its source and its document are in the tree, that its first entry
is the one about PgUp and PgDn, that its source has only ASCII characters and
lines of at most 80 columns, that every cross reference in it names a label
that exists in the sources of the manual, that the build and the master list
know it, and that the install script delivers it as a document and as a PDF
file.  The checks are made again on copies that break one rule each, and every
copy has to fail a check.
"""

import os
import re
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
src = os.path.join(root, 'manual', 'src')
fails = 0


def check(label, ok, detail=''):
    global fails
    if ok:
        print('ok   ' + label)
    else:
        fails += 1
        print('FAIL ' + label)
        if detail != '':
            print('     ' + str(detail))


def read(path):
    with open(path, encoding='utf-8') as f:
        return f.read()


def labels_of_sources():
    """Every label that a heading of the sources of the manual defines."""
    found = set()
    for name in os.listdir(src):
        if name.endswith('.src'):
            for line in read(os.path.join(src, name)).split('\n'):
                m = re.match(r'^\.h[1-6] .*\[([a-z0-9-]+)\]\s*$', line)
                if m is not None:
                    found.add(m.group(1))
    return found


def source_problems(text, labels):
    """What is wrong with the text of errata.src: a list of strings."""
    bad = []
    heads = [ln for ln in text.split('\n') if ln.startswith('.h2 ')]
    if heads == [] or 'errata-pgup' not in heads[0] or \
            'PgUp' not in heads[0] or 'PgDn' not in heads[0]:
        bad.append('the first entry is not the one about PgUp and PgDn')
    for n, line in enumerate(text.split('\n'), 1):
        if len(line) > 80:
            bad.append('line %d is over 80 columns' % n)
        if any(ord(c) > 127 for c in line):
            bad.append('line %d has a character that is not ASCII' % n)
    for ref in re.findall(r'\{[xn]:([a-z0-9-]+)\}', text):
        if ref not in labels:
            bad.append('the cross reference %s has no label' % ref)
    return bad


def list_problems(listing):
    """What is wrong with the output of strata-install --list-manuals."""
    bad = []
    lines = listing.split('\n')
    if 'document errata.doc' not in lines:
        bad.append('errata.doc is not in the list of the manuals')
    if 'pdf errata.pdf' not in lines:
        bad.append('errata.pdf is not in the list of the manuals')
    return bad


def run_list(script):
    r = subprocess.run(['bash', script, '--list-manuals'],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return r.stdout.decode('utf-8', 'replace')


errata_src = os.path.join(src, 'errata.src')
check('the source manual/src/errata.src exists', os.path.isfile(errata_src))
text = read(errata_src) if os.path.isfile(errata_src) else ''
labels = labels_of_sources()

doc = os.path.join(root, 'manual', 'doc', 'errata.doc')
check('the document manual/doc/errata.doc is in the tree and is a document',
      os.path.isfile(doc) and open(doc, 'rb').read(3) == b'*R3')

check('the source is right: the first entry is the one about PgUp and PgDn, '
      'it has ASCII only, no line over 80 columns, and no broken reference',
      source_problems(text, labels) == [], source_problems(text, labels))

master = read(os.path.join(src, 'master.txt'))
check('the master list names the document',
      re.search(r'^- errata Errata$', master, re.M) is not None)
build = read(os.path.join(root, 'manual', 'build.sh'))
check('the build makes the document with the cards',
      re.search(r'^cardnames="\$\{cardnames\} errata ', build, re.M)
      is not None)

install = os.path.join(root, 'scripts', 'strata-install')
listing = run_list(install)
check('the install script delivers errata as a document and as a PDF file',
      list_problems(listing) == [], list_problems(listing))

# ---- the checks again on copies that break one rule ------------------------
swapped = text.replace('.h2 PgUp and PgDn of the main block do nothing in '
                       'xterm [errata-pgup]\n', '', 1)
check('mutation: a source without the first entry fails',
      source_problems(swapped, labels) != [])
check('mutation: a source with a broken cross reference fails',
      source_problems(text + '\nsee {x:no-such-label}.\n', labels) != [])
check('mutation: a source with a line over 80 columns fails',
      source_problems(text + '\n' + 'x' * 81 + '\n', labels) != [])
check('mutation: a source with a character that is not ASCII fails',
      source_problems(text + '\né\n', labels) != [])

import tempfile
with tempfile.TemporaryDirectory(prefix='errata.') as d:
    spoiled = os.path.join(d, 'strata-install')
    body = read(install)
    body = body.replace('mathcheat errata\n', 'mathcheat\n')
    with open(spoiled, 'w') as f:
        f.write(body)
    check('mutation: an install script without errata in its lists fails',
          list_problems(run_list(spoiled)) != [] and body != read(install))

print('errata: %s' % ('all passed' if fails == 0 else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
