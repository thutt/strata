#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The editions of the manual.  A small manual with conditional text is made
by manual/mkmanual.py (the directives .if, .else and .endif) and printed by
strprint as a release (.STRATA_BUILD_TYPE is release) and as a developer
(debug).  The text for developers is in the developer PDF only, the contents
and the index rows that point into it follow it, manual/checkedition.py
agrees, and the generator refuses what it must refuse: a headline inside
conditional text that is followed by a headline that is always printed (the
numbers would have a gap), a reference from text that is always printed to a
section that is not, an .else or an .endif without an .if, an .if that is
never closed, a bad expression.  A section that is left out and comes after
the others of its parent is allowed, and the numbers, the contents and the
index of both editions follow it.

    STRPRINT=... python3 -I -B tests/condmanual.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
    'strprint'))
mk = os.path.join(root, 'manual', 'mkmanual.py')
checker = os.path.join(root, 'manual', 'checkedition.py')
for tool in ('pdflatex', 'pdftotext'):
    if shutil.which(tool) is None:
        print('SKIP: %s is not installed' % tool)
        sys.exit(0)
if not os.path.exists(strprint):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='condman')
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace',
        timeout=240)
    return r.returncode, r.stdout, r.stderr


def text(pdf):
    rc, out, err = run(['pdftotext', '-layout', pdf, '-'])
    return ' '.join(out.split())


def source(name, body):
    d = os.path.join(tmp, name)
    os.makedirs(d)
    with open(os.path.join(d, 'a.src'), 'w') as f:
        f.write(body)
    with open(os.path.join(d, 'master.txt'), 'w') as f:
        f.write('1 a Fixture chapter\n')
    return d


def make(d):
    out = os.path.join(d, 'm.doc')
    rc, o, e = run([sys.executable, '-I', '-B', mk, '--single',
        os.path.join(d, 'master.txt'), out])
    return rc, e, out


GOOD = """.doc Fixture
.chapter 1
.h1 One [one]
Shared opening paragraph for every edition of the fixture manual.

.if .STRATA_BUILD_TYPE # release
This paragraph is written for developers and a release does not print it.
.ix Developer index term
.else
This paragraph is written for the users and a developer does not need it.
.endif

.ix Shared index term
Closing words that every edition has in its pages.

.if edition = beta
- A bullet that only the beta edition has.
.endif
"""

d = source('good', GOOD)
rc, err, doc = make(d)
check('the manual with conditional text is made', rc == 0, err)
rel = os.path.join(d, 'release.pdf')
dev = os.path.join(d, 'developer.pdf')
rc1, o1, e1 = run([strprint, doc, '--docdir', d, '--special',
    '.STRATA_BUILD_TYPE', 'release', '-o', rel])
rc2, o2, e2 = run([strprint, doc, '--docdir', d, '--special',
    '.STRATA_BUILD_TYPE', 'debug', '-o', dev])
check('both editions print', rc1 == 0 and rc2 == 0 and os.path.exists(rel)
      and os.path.exists(dev), (rc1, e1, rc2, e2))
tr = text(rel) if os.path.exists(rel) else ''
td = text(dev) if os.path.exists(dev) else ''
check('the release has the shared text and the text for the users',
      'Shared opening paragraph' in tr and 'written for the users' in tr and
      'Closing words' in tr, tr)
check('the release leaves out the text for developers',
      'written for developers' not in tr, tr)
check('the developer edition has the text for developers and not the else '
      'branch', 'written for developers' in td and
      'written for the users' not in td, td)
check('the index row of the conditional entry is in the developer edition '
      'only', 'Developer index term' in td and
      'Developer index term' not in tr, (tr, td))
check('the shared index entry is in both', 'Shared index term' in tr and
      'Shared index term' in td, (tr, td))
check('the bullet of another edition is in neither',
      'only the beta edition' not in tr and 'only the beta edition' not in td,
      (tr, td))
rc, out, err = run([sys.executable, '-I', '-B', checker,
    os.path.join(d, 'master.txt'), rel, dev])
check('checkedition agrees', rc == 0 and '0 problems' in out and
      'paragraphs for developers checked' in out, (rc, out, err))
# The check fails when the release holds what it must leave out.
rc, out, err = run([sys.executable, '-I', '-B', checker,
    os.path.join(d, 'master.txt'), dev, dev])
check('checkedition fails when the release has the developer text',
      rc != 0 and 'in the release PDF' in out, (rc, out))

# What the generator refuses.
cases = (
    ('a hidden headline that is followed by a printed one',
     '.doc F\n.chapter 1\n.h1 A\n.if a = b\n.h2 B\ntext\n.endif\n'
     '.h2 C\ntext\n', 'leave a gap'),
    ('a reference from printed text to a section that is left out',
     '.doc F\n.chapter 1\n.h1 A\nSee {x:inner}.\n.if a = b\n.h2 B [inner]\n'
     'text\n.endif\n', 'a reference to the section inner'),
    ('an .else without an .if', '.doc F\n.chapter 1\n.h1 A\n.else\n',
     '.else without'),
    ('an .endif without an .if', '.doc F\n.chapter 1\n.h1 A\n.endif\n',
     '.endif without'),
    ('an .if that is never closed', '.doc F\n.chapter 1\n.h1 A\n.if a = b\n'
     'text\n', 'no .endif'),
    ('a second .else', '.doc F\n.chapter 1\n.h1 A\n.if a = b\n.else\n.else\n'
     '.endif\n', '.else without'),
    ('a bad operator', '.doc F\n.chapter 1\n.h1 A\n.if a <> b\n.endif\n',
     'operator'),
    ('a quote in the value', '.doc F\n.chapter 1\n.h1 A\n.if a = "b"\n'
     '.endif\n', 'quotation'),
)
for what, body, word in cases:
    dd = source('bad%d' % len(os.listdir(tmp)), body)
    rc, err, doc = make(dd)
    # A hidden headline in the middle of its parent is refused only where
    # the numbers are typed into the headlines (see the next block): the
    # single document has the numbers made by the product.
    if word == 'leave a gap':
        check('the single document, whose numbers are made by the product, '
              'accepts %s' % what, rc == 0 and word not in err, err)
        continue
    check('the generator refuses %s' % what, rc != 0 and word in err, err)

# The chapter document types the numbers into the headlines, so a section
# that is left out in the middle of its parent would leave a gap in them:
# the generator refuses it there, and accepts a section left out last.
def make_typed(d):
    out = os.path.join(d, 't.doc')
    rc, o, e = run([sys.executable, '-I', '-B', mk,
        os.path.join(d, 'a.src'), out])
    return rc, e, out


HIDDEN_MIDDLE = ('.doc F\n.chapter 1\n.h1 A\n.if a = b\n.h2 B\ntext\n.endif\n'
                 '.h2 C\ntext\n')
HIDDEN_LAST = ('.doc F\n.chapter 1\n.h1 A\n.h2 C\ntext\n.if a = b\n.h2 B\n'
               'text\n.endif\n')
dd = source('typedmid', HIDDEN_MIDDLE)
rc, err, doc = make_typed(dd)
check('the chapter document, whose numbers are typed, refuses a hidden '
      'headline that is followed by a printed one',
      rc != 0 and 'leave a gap' in err, err)
dd = source('typedlast', HIDDEN_LAST)
rc, err, doc = make_typed(dd)
check('the chapter document accepts a hidden headline that comes last',
      rc == 0, err)

# A section that a release leaves out, last in its parent.
LAST = """.doc Fixture
.chapter 1
.h1 One [one]
Shared opening paragraph for every edition of the fixture manual.
.h2 Always [always]
Text that every edition has under its own headline.
.if .STRATA_BUILD_TYPE # release
.h2 Only for developers [devsec]
.ix Developer section term
Words that only the developer edition prints under the second headline.
.h3 Deeper still
More words that only the developer edition prints, one level down.
See {x:devsec} for the rest.
.endif
"""
d = source('last', LAST)
rc, err, doc = make(d)
check('a section that is left out, last in its parent, is made', rc == 0,
      err)
rel = os.path.join(d, 'release.pdf')
dev = os.path.join(d, 'developer.pdf')
rc1, o1, e1 = run([strprint, doc, '--docdir', d, '--special',
    '.STRATA_BUILD_TYPE', 'release', '-o', rel])
rc2, o2, e2 = run([strprint, doc, '--docdir', d, '--special',
    '.STRATA_BUILD_TYPE', 'debug', '-o', dev])
tr = text(rel) if os.path.exists(rel) else ''
td = text(dev) if os.path.exists(dev) else ''
check('the release has the section that stays and not the one left out',
      'Always' in tr and 'Only for developers' not in tr and
      'Deeper still' not in tr and 'one level down' not in tr, tr)
check('the developer edition has both, numbered one after the other',
      '1.1 Always' in td and '1.2 Only for developers' in td and
      '1.2.1 Deeper still' in td, td)
check('the index row of the section that is left out follows it',
      'Developer section term' in td and 'Developer section term' not in tr,
      (tr, td))
check('the editions have no warning of a broken structure',
      'if-structure' not in e1 and 'if-structure' not in e2, (e1, e2))

print('condmanual: %d failures' % fails)
sys.exit(1 if fails else 0)
