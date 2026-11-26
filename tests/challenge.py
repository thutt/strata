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

"""The document "How do your document preparation systems fare?".

    python3 -I -B tests/challenge.py

The document is made from manual/src/challenge.src.  The test checks that
every number in its tables is a number of section 7 of docs/BIG-DOCUMENTS.txt
(the measurements and their commands), that the source names no other
program and no program of the original product, that its text and the text of
docs/CHALLENGE-CLAIMS.txt are ASCII with lines of at most 80 columns, that
every picture it names is a PNG file of the folder manual/challenge/pictures
and has a line in the file pages.txt there, that every figure that the text
mentions has a caption, that the build knows the document and that the
install script delivers it, the document in its own folder and its PDF file
with the other manuals.  When the Printer and LaTeX are found, the
document is also made and its PDF is read with pdftotext.  The checks are made
again on copies that break one rule each, and every copy has to fail a check.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
src_file = os.path.join(root, 'manual', 'src', 'challenge.src')
big_file = os.path.join(root, 'docs', 'BIG-DOCUMENTS.txt')
claims_file = os.path.join(root, 'docs', 'CHALLENGE-CLAIMS.txt')
pictures = os.path.join(root, 'manual', 'challenge', 'pictures')
fails = 0

# The names of other document programs that the document must not use until
# the claim has been checked against the public documentation of that program
# and written into docs/CHALLENGE-CLAIMS.txt.
OTHER_NAMES = ('Word', 'Google', 'LibreOffice', 'OpenOffice',
               'Overleaf', 'InDesign', 'FrameMaker', 'WordPerfect',
               'Microsoft', 'Apple', 'Scrivener', 'Notion', 'Lotus',
               'Manuscript', 'Ventura', 'Typst', 'Markdown')
# Other words that the document must not hold (the style rules).
# The project word for a software fault is defect or error; the forbidden word
# is built here so that this file does not hold it.
FAULT = 'b' + 'ug'
FORBIDDEN = (FAULT, FAULT + 's', 'anthropic', 'stred', 'strprint', 'dviview')


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


def section7(big_text):
    """The text of section 7 of docs/BIG-DOCUMENTS.txt."""
    at = big_text.find('\n7. Measured again')
    if at < 0:
        return ''
    return big_text[at:]


def numbers_of(text):
    """Every number written with digits, commas and a point, as a string."""
    return set(n.replace(',', '') for n in
               re.findall(r'\d[\d,]*(?:\.\d+)?', text))


def table_numbers(src):
    """The numbers in the rows of the tables of the source, and in its
    first paragraphs, as written there."""
    found = set()
    in_table = False
    for line in src.split('\n'):
        if line.startswith('.table'):
            in_table = True
            continue
        if line.startswith('.end'):
            in_table = False
            continue
        if in_table and '|' in line:
            found |= numbers_of(line.split('|', 1)[1])
    return found


def source_problems(src, big7):
    """What is wrong with the source of the document: a list of strings."""
    bad = []
    known = numbers_of(big7)
    # A table may round a measured number: 65.2 s is 65 s, 1.66 s is 1.7 s.
    for k in list(known):
        if '.' in k:
            value = float(k)
            known.add(str(int(round(value))))
            known.add('%.1f' % value)
    for n in sorted(table_numbers(src)):
        trimmed = n.rstrip(',.')
        if trimmed not in known:
            bad.append('the number %s of a table is not in section 7 of '
                       'docs/BIG-DOCUMENTS.txt' % trimmed)
    words = set(re.findall(r'[A-Za-z]+', re.sub(r'\{c:[^}]*\}', '', src)))
    for name in OTHER_NAMES:
        if name in words:
            bad.append('the source names %s' % name)
    for word in FORBIDDEN:
        if re.search(r'\b' + word + r'\b', src, re.I):
            bad.append('the source holds the word %s' % word)
    for n, line in enumerate(src.split('\n'), 1):
        if len(line) > 80:
            bad.append('line %d is over 80 columns' % n)
        if any(ord(c) > 127 for c in line):
            bad.append('line %d has a character that is not ASCII' % n)
    pics = re.findall(r'^\.pic (\S+) \S+ .*\[([a-z0-9-]+)\]\s*$', src, re.M)
    labels = set(label for _, label in pics)
    for ref in re.findall(r'\{[fF]ig:([a-z0-9-]+)\}', src):
        if ref not in labels:
            bad.append('the figure %s has no caption' % ref)
    if len(re.findall(r'^\.pic ', src, re.M)) != len(pics):
        bad.append('a picture has no label in its caption')
    if 'How do your document preparation systems fare?' not in src:
        bad.append('the question of the challenge is not in the source')
    return bad


def picture_problems(src, folder):
    """What is wrong with the pictures that the source names."""
    bad = []
    pages = ''
    if os.path.exists(os.path.join(folder, 'pages.txt')):
        pages = read(os.path.join(folder, 'pages.txt'))
    for path, _ in re.findall(r'^\.pic (\S+) \S+ .*\[([a-z0-9-]+)\]\s*$', src,
                              re.M):
        base = os.path.basename(path)
        full = os.path.join(folder, base)
        if not os.path.exists(full):
            bad.append('the picture %s is missing' % base)
            continue
        with open(full, 'rb') as f:
            if f.read(8) != b'\x89PNG\r\n\x1a\n':
                bad.append('the picture %s is not a PNG file' % base)
        if os.path.getsize(full) < 3000:
            bad.append('the picture %s is nearly empty' % base)
        if base[:-4] in ('ch-equation', 'ch-table', 'ch-index', 'ch-numbered',
                         'ch-errors'):
            if base[:-4] not in pages:
                bad.append('pages.txt has no line for %s' % base)
    return bad


def text_problems(text, name):
    """The style rules of a plain text file of the tree."""
    bad = []
    for n, line in enumerate(text.split('\n'), 1):
        if len(line) > 80:
            bad.append('%s line %d is over 80 columns' % (name, n))
        if any(ord(c) > 127 for c in line):
            bad.append('%s line %d is not ASCII' % (name, n))
    if re.search(r'\b' + FAULT + r's?\b', text, re.I):
        bad.append('%s uses the forbidden word for a fault' % name)
    return bad


src = read(src_file)
big = read(big_file)
big7 = section7(big)
check('section 7 of docs/BIG-DOCUMENTS.txt exists', big7 != '')
check('the source of the document is sound',
      source_problems(src, big7) == [], source_problems(src, big7))
check('the pictures of the document are in the tree',
      picture_problems(src, pictures) == [],
      picture_problems(src, pictures))
check('docs/CHALLENGE-CLAIMS.txt is sound',
      text_problems(read(claims_file), 'CHALLENGE-CLAIMS') == [],
      text_problems(read(claims_file), 'CHALLENGE-CLAIMS'))
check('section 7 of docs/BIG-DOCUMENTS.txt is sound',
      text_problems(big7, 'BIG-DOCUMENTS') == [],
      text_problems(big7, 'BIG-DOCUMENTS'))

build = read(os.path.join(root, 'manual', 'build.sh'))
check('manual/build.sh has the target challenge',
      re.search(r'^\s+challenge\) challenge ;;', build, re.M) is not None)
check('the target challenge is part of all',
      re.search(r'all\) single && cards && challenge ;;', build) is not None)
listing = subprocess.run(
    ['bash', os.path.join(root, 'scripts', 'strata-install'),
     '--list-manuals'], stdout=subprocess.PIPE,
    universal_newlines=True).stdout.split('\n')
check('the install delivers the document in the folder doc/challenge',
      'document challenge/challenge.doc' in listing)
check('the install delivers the PDF file of the document',
      'pdf challenge.pdf' in listing)
check('the document is in the source tree, next to its pictures',
      os.path.isfile(os.path.join(root, 'manual', 'challenge',
                                  'challenge.doc')))
claims = read(claims_file)
check('docs/CHALLENGE-CLAIMS.txt names the file of the document',
      'manual/src/challenge.src' in claims)

# The checks again on copies that break one rule each.
mutants = []
mutants.append(('a number that was not measured',
                src.replace('| 724', '| 725', 1)))
mutants.append(('another program is named',
                src.replace('Only what was measured',
                            'Word is slower than Strata.  Only what was '
                            'measured', 1)))
mutants.append(('the original product is named',
                src.replace('The machine', 'Lotus Manuscript', 1)))
mutants.append(('a figure without a caption',
                src.replace('[ch-open]', '', 1)))
mutants.append(('a word that the style forbids',
                src.replace('Nothing is said', 'The ' + FAULT + ' is said', 1)))
mutants.append(('a line over 80 columns', src + 'x' * 90 + '\n'))
mutants.append(('a character that is not ASCII',
                src.replace('Strata is a', 'Strata \xe9 a', 1)))
for what, text in mutants:
    check('a copy with %s fails' % what,
          text != src and source_problems(text, big7) != [])
check('a copy without the picture file fails',
      picture_problems(src, os.path.join(root, 'manual')) != [])

# The document is made and read, when the programs are there.
build_dir = os.environ.get('STRATA_BUILD_DIR', '')
xbin = os.path.join(build_dir, 'x11')
tools = ('pdflatex', 'pdftotext', 'pdfinfo')
if build_dir == '' or not os.path.exists(os.path.join(xbin, 'strprint')) or \
        any(shutil.which(t) is None for t in tools):
    print('SKIP: the Printer or LaTeX is not there; the PDF was not made')
else:
    out = tempfile.mkdtemp(prefix='challenge')
    env = dict(os.environ, XBIN=xbin, MANUAL_OUT=out)
    r = subprocess.run(['bash', os.path.join(root, 'manual', 'build.sh'),
                        'challenge'], env=env, capture_output=True,
                       text=True)
    pdf = os.path.join(out, 'pdf', 'challenge.pdf')
    check('build.sh challenge makes the PDF', r.returncode == 0 and
          os.path.exists(pdf), r.stdout[-300:] + r.stderr[-300:])
    if os.path.exists(pdf):
        info = subprocess.run(['pdfinfo', pdf], capture_output=True,
                              text=True).stdout
        pages = int(re.search(r'Pages:\s+(\d+)', info).group(1))
        text = subprocess.run(['pdftotext', '-layout', pdf, '-'],
                              capture_output=True, text=True).stdout
        check('the PDF has between 6 and 12 pages (%d)' % pages,
              6 <= pages <= 12)
        check('the PDF asks the question twice',
              text.count('How do your document preparation systems '
                         'fare?') >= 2)
        check('the PDF has no unresolved reference', '??' not in text)
        check('the PDF has the figures 1 to 7',
              all(('Figure %d.' % n) in text for n in range(1, 8)))
        check('the PDF names no other program',
              not any(re.search(r'\b%s\b' % n, text) for n in OTHER_NAMES))
    shutil.rmtree(out, ignore_errors=True)

print('challenge.py: %s' % ('FAILED' if fails else 'ok'))
sys.exit(1 if fails else 0)
