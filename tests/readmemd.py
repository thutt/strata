# Copyright (c) 2026 Logic Magicians Software
#
# Checks README.md, the Markdown file at the root of the repository that
# GitHub shows.  Usage:
#
#     python3 -I -B tests/readmemd.py
#
# The file is ASCII, has no line over 80 columns, has the sections in the
# order that the owner set, says nothing about a release or a branch, names
# only the program str and none of its parts, names no original program,
# links only to files that exist (the one outside link is the license page),
# and says no more than the sources support.  The checks are made again on
# texts that break one rule each, and each such text has to fail a check.
#
"""Check README.md."""

import os
import re
import subprocess
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fails = 0

SECTIONS = ['What is in it', 'What it does', 'Install',
            'Build from the source', 'Documentation', 'Known issues',
            'Repository']
# The README says nothing about a release (the build type in the command
# 'source scripts/setup -b release' is the one place for the word) and names
# none of the parts of str, and it names no branch.
BAD_WORDS = [r'\bbranch', r'\bdevelopment-\d', r'\brelease', r'\bstatus\b',
             r'\bin preparation\b', r'\bstred\b',
             r'\bstrprint\b', r'\bdviview\b', r'\bEditor\b',
             r'\bPrinter\b', r'\bViewer\b', r'\bDocument Manager\b',
             r'\blotus\b', r'\bmanuscript\b', r'\boriginal\b', r'\bbugs?\b',
             r'\bpowerful\b', r'\bblazing\b', r'\bseamless', r'\bbest\b']


# The four things that the owner asked for: the layout of the source, the
# layout of the install, how to start str and open Getting Started, and where
# the PDF files are.  Each is a text that has to be in README.md.
REQUIRED = [('the layout of the install', '### Where the files are installed'),
            ('the install layout names the PDF folder',
             'PREFIX/share/strata/pdf/'),
            ('the install layout names the folder of the documents',
             'PREFIX/share/strata/doc/'),
            ('how to start Strata and open Getting Started',
             '### Starting Strata and opening Getting Started'),
            ('the name of the Getting Started document',
             'share/strata/doc/GettingStarted.doc'),
            ('the programs str and str-x11 are named for the start',
             '`str-x11`'),
            ('the PDF file of Getting Started is named',
             'share/strata/pdf/GettingStarted.pdf'),
            ('the PDF file of the Reference Manual is named',
             'share/strata/pdf/strata-reference-manual.pdf'),
            ('the layout of the source tree', 'The top of the source tree:'),
            ('the layout below manual', 'Below `manual`:'),
            ('the mouse in the X11 window',
             'the wheel of the mouse scrolls the text')]


def check(label, ok, detail=''):
    global fails
    if ok:
        print('ok   ' + label)
    else:
        fails += 1
        print('FAIL ' + label)
        if detail != '':
            print('     ' + str(detail))


def problems(text, files, tags):
    """What is wrong with a README text: a list of strings."""
    found = []
    lines = text.split('\n')

    for n, line in enumerate(lines, 1):
        if len(line) > 80:
            found.append('line %d is over 80 columns' % n)

        for ch in line:
            if ord(ch) > 126 or (ord(ch) < 32 and ch != '\t'):
                found.append('line %d has a character that is not ASCII' % n)
                break

    if len(lines) == 0 or not lines[0].startswith('# Strata'):
        found.append('the first line is not the title')

    heads = re.findall(r'^## (.*)$', text, re.MULTILINE)

    if heads != SECTIONS:
        found.append('the sections are %s' % heads)

    allowed = text.replace('scripts/setup -b release', 'scripts/setup -b X')

    for pat in BAD_WORDS:
        flags = 0 if pat.startswith(r'\b') and pat[2:3].isupper() else \
            re.IGNORECASE

        if re.search(pat, allowed, flags) is not None:
            found.append('the text has a word that is not allowed: ' + pat)

    for m in re.finditer(r'\]\(([^)]*)\)', text):
        target = m.group(1)

        if target.startswith('http'):
            continue

        if target.rstrip('/') not in files:
            found.append('the link %s names no file' % target)

    for m in re.finditer(r'https?://[^\s)>]+', text):
        if m.group(0) != 'https://www.gnu.org/licenses/':
            found.append('the outside link ' + m.group(0))

    for m in re.finditer(r'v?\b\d+\.\d+\.\d+\b', text):
        found.append('the text names a version: ' + m.group(0))

    for m in re.finditer(r'strata-([A-Za-z0-9.]+)\.tar', text):
        if m.group(1) != 'X.Y.0':
            found.append('the tarball is named strata-' + m.group(1))

    if re.search(r'!\[|<img|badge|shields\.io', text) is not None:
        found.append('the text has an image or a badge')

    for label, needle in REQUIRED:
        if needle not in text:
            found.append('the text lacks ' + label)

    if re.search(r'GNU General Public License, version 3 or later',
                 text) is None:
        found.append('the license is not named')

    return found


def section(text, head):
    """The text of one section, between its heading and the next one."""
    m = re.search(r'^#+ ' + re.escape(head) + r'$(.*?)(?=^#+ |\Z)', text,
                  re.MULTILINE | re.DOTALL)

    if m is None:
        return ''

    return m.group(1)


def places(text):
    """The three texts that have to name LaTeX, by their labels."""
    first = text.split('\n## ')[0]

    return {'the first paragraph': first,
            'the program section': section(text, 'What is in it'),
            'the requirements': section(text, 'Requirements')}


def latex_places(text):
    return {k: 'LaTeX' in v for k, v in places(text).items()}


def text_without_latex(text, label):
    """The text with the word LaTeX taken out of one of the places."""
    old = places(text)[label]

    return text.replace(old, old.replace('LaTeX', 'TeX'))


def tracked():
    out = subprocess.run(['git', '-C', root, 'ls-files'],
                         stdout=subprocess.PIPE, universal_newlines=True)
    names = set(out.stdout.split('\n'))
    dirs = set()

    for n in names:
        parts = n.split('/')

        for i in range(1, len(parts)):
            dirs.add('/'.join(parts[:i]))

    return names | dirs


def main():
    with open(os.path.join(root, 'README.md'), encoding='utf-8') as f:
        text = f.read()

    files = tracked()
    tags = subprocess.run(['git', '-C', root, 'tag'],
                          stdout=subprocess.PIPE,
                          universal_newlines=True).stdout.split()
    found = problems(text, files, [])
    check('README.md breaks no rule', found == [], found)

    for needle in ['sha256sum -c', 'tar -xJf', 'bash strata-install',
                   'scripts/strata-build test', 'scripts/check-tools',
                   'source scripts/setup']:
        check('README.md has the command ' + needle, needle in text)

    manuals = subprocess.run(
        ['bash', os.path.join(root, 'scripts', 'strata-install'),
         '--list-manuals'], stdout=subprocess.PIPE,
        universal_newlines=True).stdout.split('\n')
    names = set()

    for line in manuals:
        if line.startswith('document '):
            names.add(os.path.basename(line.split()[1])[:-4])

    for name in sorted(names):
        check('README.md names the manual ' + name, name in text)

    check('README.md names the program str', '`str`' in text)

    # LaTeX is named in the first paragraph, in the program section and in
    # the requirements.
    for label, where in latex_places(text).items():
        check('README.md names LaTeX in ' + label, where)

    for label in ['the first paragraph', 'the program section',
                  'the requirements']:
        broken = text_without_latex(text, label)
        check('a mutation fails: no LaTeX in ' + label,
              latex_places(broken)[label] is False)

    for label, needle in REQUIRED:
        check('README.md has ' + label, needle in text)
        check('a mutation fails: no ' + label,
              problems(text.replace(needle, 'x'), files, []) != [])

    # Mutations: each text breaks one rule and has to fail.
    mutations = [
        ('a long line', text + 'x' * 81 + '\n'),
        ('a non-ASCII character', text + 'café\n'),
        ('a section out of order',
         text.replace('## Documentation', '## Docs')),
        ('the old program', text + 'Lotus\n'),
        ('a dead link', text + '[x](nothing/here.txt)\n'),
        ('an outside link', text + '<https://example.org/>\n'),
        ('a version', text + 'Version 1.0.0 is out.\n'),
        ('a badge', text + '![x](a.png)\n'),
        ('a hype word', text + 'A powerful editor.\n'),
        ('a status line', text + 'Status: in preparation.\n'),
        ('a branch', text + 'The branch development-1 holds the work.\n'),
        ('a release', text + 'The first release is out.\n'),
        ('a part of str', text + 'The `stred` program is the Editor.\n'),
    ]

    for label, mutated in mutations:
        check('a mutation fails: ' + label,
              problems(mutated, files, []) != [])

    if fails != 0:
        print('%d check(s) failed' % fails)
        sys.exit(1)

    print('all checks passed')


main()
