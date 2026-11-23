#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This test applies the style rules of the documentation to the text that
# the code contains: the comments and the string literals of the C sources
# (src, tools, tests), the comments and the strings of the Python scripts
# and of the shell scripts, the help text of the Editor and the README
# files.  The rules are those of the rules E30 and E31 of the manual
# (manual/checkeditorial.py):
#
#   - software is a mechanical device: a program, a panel, a box, a bar, a
#     message line, TeX or a terminal does not say, ask, tell, want, know,
#     try, decide, refuse, understand, remember, forget or think; the
#     sentence is written with a verb of the machine (shows, reports,
#     rejects, requires, determines, prompts for) or in the passive voice;
#   - a defect or an error of the software is never called a bug.
#
# The pattern of the verbs finds a verb that follows a software subject or
# the bare pronoun it.  It does not find the pronoun whose noun is in an
# earlier sentence; those cases are found by reading.  A document, a table, a
# field or the header of a file that "says" what it holds is not software
# and is not looked at.
#
# The files that define or test the rules themselves hold sentences that
# break them on purpose and are skipped: manual/checkeditorial.py,
# tests/editorial.py and this file.  The test also proves, with samples
# that must fail, that every group of the pattern finds what it is for.

import io
import os
import re
import sys
import tokenize

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, '..')
sys.path.insert(0, os.path.join(root, 'manual'))
import checkeditorial as CE  # noqa: E402

# Subjects that only the code has: a part of the program that a comment
# describes as if it acted.
CODE_SUBJECT = (r"(?:lpinfo|dictionary|function|routine|parser|handler|"
                r"module|callback)")
CODE_PERSON = re.compile(r"\b" + CODE_SUBJECT + r"\s+" + CE.PERSON_VERBS +
                         r"\b")

# The files that name the forbidden words as samples of what must be caught.
SKIP = ('manual/checkeditorial.py', 'tests/editorial.py',
        'tests/codestyle.py', 'tests/gsdoc.py')

# Quoted text of other systems or of the licence, which cannot be changed:
# a pair of the path and a text that is allowed to match.  Every entry has
# its reason.  There is none at present.
ALLOW = ()

fails = 0


def check(name, ok, detail=''):
    global fails
    if ok:
        print('ok   %s' % name)
    else:
        fails += 1
        print('FAIL %s' % name)
        if detail != '':
            print('     %r' % (detail,))


def c_texts(s):
    """The comments and the string literals of C source, with line numbers."""
    out = []
    i = 0
    n = len(s)
    line = 1
    while i < n:
        c = s[i]
        if c == '\n':
            line += 1
            i += 1
        elif s.startswith('/*', i):
            j = s.find('*/', i + 2)
            j = n if j < 0 else j
            t = s[i + 2:j]
            out.append((line, t))
            line += t.count('\n')
            i = j + 2
        elif s.startswith('//', i):
            j = s.find('\n', i)
            j = n if j < 0 else j
            out.append((line, s[i + 2:j]))
            i = j
        elif c == '"':
            j = i + 1
            buf = ''
            while j < n and s[j] != '"':
                if s[j] == '\\':
                    buf += s[j:j + 2]
                    j += 2
                else:
                    buf += s[j]
                    j += 1
            out.append((line, buf))
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and s[j] != "'":
                j += 2 if s[j] == '\\' else 1
            i = j + 1
        else:
            i += 1

    return out


def py_texts(s):
    """The comments and the strings of Python source, with line numbers."""
    out = []
    try:
        for tok in tokenize.generate_tokens(io.StringIO(s).readline):
            if tok.type in (tokenize.COMMENT, tokenize.STRING):
                out.append((tok.start[0], tok.string))
    except (tokenize.TokenError, IndentationError, SyntaxError):
        pass

    return out


def line_texts(s):
    """Every line of a text: shell scripts and the help text."""
    return [(k, l) for k, l in enumerate(s.split('\n'), 1)]


def texts_of(path, s):
    if path.endswith(('.c', '.h')):
        return c_texts(s)
    if path.endswith('.py'):
        return py_texts(s)

    return line_texts(s)


def hits_of(texts):
    """The offending phrases of a list of (line, text) pairs."""
    out = []
    for ln, t in texts:
        t = ' '.join(t.split())
        for rx in (CE.PERSON, CODE_PERSON, CE.BUG):
            for m in rx.finditer(t):
                out.append((ln, m.group(0)))

    return out


def files():
    """The files of the project that carry text of the rules."""
    for top in ('src', 'tools', 'tests', 'scripts', 'manual', 'help'):
        for dp, dn, fn in os.walk(os.path.join(root, top)):
            dn[:] = [d for d in dn if d not in ('build', '__pycache__')]
            for f in fn:
                if top == 'help' or f.endswith(('.c', '.h', '.py', '.sh')):
                    yield os.path.join(dp, f)
    yield os.path.join(root, 'README.txt')


# ---- the tree is clean ------------------------------------------------------
found = []
for path in files():
    rel = os.path.relpath(path, root)
    if rel in SKIP:
        continue
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            s = f.read()
    except OSError:
        continue
    for ln, h in hits_of(texts_of(path, s)):
        if (rel, h) not in ALLOW:
            found.append('%s:%d: %s' % (rel, ln, h))
check('the comments, strings and help text follow the rules of the '
      'documentation (no software that acts like a person, no bug)',
      not found, found[:12])

# ---- every group of the pattern finds what it is for -----------------------
SAMPLES = (
    ('a message line that says', '/* The message line says why. */', 'c'),
    ('a panel that asks', '/* The panel asks for a name. */', 'c'),
    ('TeX that refuses', 'x = "TeX refuses the equation";', 'c'),
    ('a pronoun with a verb of a person', '/* It decides at once. */', 'c'),
    ('a part of the program that says', '/* The parser knows the keys. */',
     'c'),
    ('lpinfo that says', 'x = "lpinfo says:";', 'c'),
    ('the word bug in a comment', '/* A bug of the list. */', 'c'),
    ('the word bug in a Python comment', '# This is a bug.', 'py'),
    ('a shell comment that says', '# The box wants a lookup.', 'sh'),
)
for name, text, kind in SAMPLES:
    path = {'c': 'x.c', 'py': 'x.py', 'sh': 'x.sh'}[kind]
    check('a sample must fail: %s' % name,
          len(hits_of(texts_of(path, text + '\n'))) > 0, text)

CLEAN = (
    ('a message line that shows', '/* The message line shows why. */', 'c'),
    ('a panel that prompts', '/* The panel prompts for a name. */', 'c'),
    ('a table that says what it holds',
     '/* The table says which font it is. */', 'c'),
    ('the word debug', '/* A debug build reports more. */', 'c'),
    ('the passive voice', '/* Nothing is installed. */', 'c'),
)
for name, text, kind in CLEAN:
    check('a sample must pass: %s' % name,
          len(hits_of(texts_of('x.c', text + '\n'))) == 0, text)

print('codestyle: %s' % ('all passed' if fails == 0 else
                         '%d FAILURES' % fails))
sys.exit(1 if fails != 0 else 0)
