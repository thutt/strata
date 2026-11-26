# Copyright (c) 2026 Logic Magicians Software
#
# Checks README.md, the Markdown file at the root of the repository that
# GitHub shows.  Usage:
#
#     python3 -I -B tests/readmemd.py
#
# The file is ASCII, has no line over 80 columns, has the sections in the
# order that the owner set, names no release that does not exist, names no
# original program, links only to files that exist (the one outside link is
# the license page), and says no more than the sources support.  The checks
# are made again on texts that break one rule each, and each such text has
# to fail a check.
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
BAD_WORDS = [r'\blotus\b', r'\bmanuscript\b', r'\boriginal\b', r'\bbugs?\b',
             r'\bpowerful\b', r'\bblazing\b', r'\bseamless', r'\bbest\b']


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

    for pat in BAD_WORDS:
        if re.search(pat, text, re.IGNORECASE) is not None:
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

    if re.search(r'^Status: the first release is in preparation',
                 text, re.MULTILINE) is None:
        found.append('the status line is missing')

    if re.search(r'GNU General Public License, version 3 or later',
                 text) is None:
        found.append('the license is not named')

    if len(tags) != 0 and 'v1.0.0' in tags:
        found.append('a release tag exists: the status is stale')

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
    progs = [ln for ln in section(text, 'What is in it').split('\n')
             if ln.startswith('| `strprint`')]

    return {'the first paragraph': first,
            'the programs list': ''.join(progs),
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

    check('no release tag exists', tags == [], tags)

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
            names.add(line.split()[1][:-4])

    for name in sorted(names):
        check('README.md names the manual ' + name, name in text)

    for name in ['stred', 'str', 'strprint', 'dviview']:
        check('README.md names the program ' + name,
              '`' + name + '`' in text)

    # LaTeX is named in the first paragraph, in the programs list and in
    # the requirements.
    for label, where in latex_places(text).items():
        check('README.md names LaTeX in ' + label, where)

    for label in ['the first paragraph', 'the programs list',
                  'the requirements']:
        broken = text_without_latex(text, label)
        check('a mutation fails: no LaTeX in ' + label,
              latex_places(broken)[label] is False)

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
        ('no status line',
         text.replace('Status: the first release is in preparation',
                      'Status: ready')),
    ]

    for label, mutated in mutations:
        check('a mutation fails: ' + label,
              problems(mutated, files, []) != [])

    check('a mutation fails: a release tag exists',
          problems(text, files, ['v1.0.0']) != [])

    if fails != 0:
        print('%d check(s) failed' % fails)
        sys.exit(1)

    print('all checks passed')


main()
