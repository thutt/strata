#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This test checks the numbering runs: the record BZG<format> on the headline
# block of a section starts a run of numbers on its level.  The test makes
# small structured documents of its own, with chapters 1 to 3 and an
# appendix run A, B (so that the appendices are the sections 4 and 5 of the
# structure but print as A and B, with A.1 and B.1 below them), and a run in
# the middle of a list of subsections.  It calls the Editor's own function
# for the tag of every headline (the program sectags) and prints every
# document to a PDF, and compares both with the tags that are expected.  The
# same documents without the records must give the plain numbers, which
# proves that the records, and not the structure, make the letters.
#
#     python3 -I -B tests/numrun.py
#
# The programs are found like the other tests find them (STRATA_BUILD_DIR).

import os
import shutil
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
bdir = os.environ.get('STRATA_BUILD_DIR', '')
sectags = os.environ.get('SECTAGS', os.path.join(bdir, 'term', 'sectags'))
strprint = os.environ.get('STRPRINT', os.path.join(bdir, 'term', 'strprint'))

CRLF = b'\r\n'
EOL = b'\x1d'
BASE = [b'*R3', b'MR1001', b'PP\\1\\\x00', b'P1C33', b'FHA1', b'FTT5',
        b'DP0,8500,11000']

fails = 0


def check(name, cond, extra=''):
    global fails
    if cond:
        print('ok   ' + name)
    else:
        fails += 1
        print('FAIL ' + name)
        if extra != '':
            print(extra)


def sect(word, kids=(), run=None):
    return {'word': word, 'kids': list(kids), 'run': run}


def emit(sections, out, runs):
    for s in sections:
        out.append(b'S')
        if s['run'] is not None and runs:
            out.append(b'BZG' + s['run'] + b'\x00')
        out.append(b'T' + s['word'] + EOL + b'\x00')
        out.append(b'T' + b'The body of a section.' + EOL + b'\x00')
        if s['kids']:
            out.append(b'+')
            emit(s['kids'], out, runs)
            out.append(b'-')


def document(sections, runs=True):
    out = list(BASE) + [b'*B1', b'S', b'S', b'+']
    emit(sections, out, runs)
    out += [b'-', b'*E']
    return CRLF.join(out) + CRLF


def tree():
    return [
        sect(b'Alpha', [sect(b'Alpha1'), sect(b'Alpha2')]),
        sect(b'Bravo'),
        sect(b'Charlie', [sect(b'Charlie1'),
                          sect(b'Charlie2', run=b'(\\a\\) '),
                          sect(b'Charlie3')]),
        sect(b'Delta', [sect(b'Delta1'),
                        sect(b'Delta2', [sect(b'Delta2x')])], run=b'\\A\\ '),
        sect(b'Echo', [sect(b'Echo1')]),
        sect(b'Foxtrot', run=b'Part \\I\\. '),
        sect(b'Golf', [sect(b'Golf1')]),
    ]


EXPECT = [
    ('Alpha', '1'), ('Alpha1', '1.1'), ('Alpha2', '1.2'), ('Bravo', '2'),
    ('Charlie', '3'), ('Charlie1', '3.1'), ('Charlie2', '(a)'),
    ('Charlie3', '(b)'),
    ('Delta', 'A'), ('Delta1', 'A.1'), ('Delta2', 'A.2'),
    ('Delta2x', 'A.2.1'), ('Echo', 'B'), ('Echo1', 'B.1'),
    ('Foxtrot', 'Part I.'), ('Golf', 'Part II.'), ('Golf1', 'II.1'),
]

PLAIN = [
    ('Alpha', '1'), ('Alpha1', '1.1'), ('Alpha2', '1.2'), ('Bravo', '2'),
    ('Charlie', '3'), ('Charlie1', '3.1'), ('Charlie2', '3.2'),
    ('Charlie3', '3.3'),
    ('Delta', '4'), ('Delta1', '4.1'), ('Delta2', '4.2'),
    ('Delta2x', '4.2.1'), ('Echo', '5'), ('Echo1', '5.1'),
    ('Foxtrot', '6'), ('Golf', '7'), ('Golf1', '7.1'),
]


def tags_of(path):
    r = subprocess.run([sectags, path], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    if r.returncode != 0:
        return None
    rows = []
    for line in r.stdout.split(b'\n'):
        if line == b'':
            continue
        f = line.split(b'\t')
        word = f[3].decode('cp437')
        tag = ''.join(c for c in f[2].decode('cp437') if c.isprintable())
        rows.append((word, tag.strip()))
    return rows


def pdf_lines(path, out):
    subprocess.run([strprint, path, '-o', out, '--no-error-log',
                    '--no-banner'], stdout=subprocess.PIPE,
                   stderr=subprocess.PIPE)
    if not os.path.exists(out):
        return None
    r = subprocess.run(['pdftotext', '-layout', out, '-'],
                       stdout=subprocess.PIPE)
    text = r.stdout.decode('utf-8', 'replace')
    return [' '.join(x.split()) for x in text.split('\n')]


def run_case(name, tmp, runs, expect):
    path = os.path.join(tmp, name + '.doc')
    with open(path, 'wb') as f:
        f.write(document(tree(), runs))
    rows = tags_of(path)
    check('%s: the Editor gives the tags %s' % (
        name, ' '.join(t for _, t in expect[:3]) + ' ...'),
        rows == expect, repr(rows))
    lines = pdf_lines(path, os.path.join(tmp, name + '.pdf'))
    check('%s: the PDF was made' % name, lines is not None)
    if lines is None:
        return
    for word, tag in expect:
        want = tag + ' ' + word
        check('%s: the print has "%s"' % (name, want), want in lines,
              'lines with the word: %r' % [x for x in lines if word in x])


def numbers_check(path):
    """The exit status of manual/checknumbers.py docs for one document."""
    prog = os.path.join(here, '..', 'manual', 'checknumbers.py')
    r = subprocess.run([sys.executable, '-I', '-B', prog, 'docs', path],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return r.returncode, r.stdout.decode('utf-8', 'replace')


def check_program(tmp):
    """The check of the manual: a document with typed numbers in its
    headlines, or with the section tags off, is refused; one without them
    is accepted."""
    typed = os.path.join(tmp, 'typed.doc')
    good = os.path.join(tmp, 'good.doc')
    off = os.path.join(tmp, 'off.doc')
    data = document(tree(), True)
    with open(good, 'wb') as f:
        f.write(data)
    with open(typed, 'wb') as f:
        f.write(data.replace(b'Bravo', b'2  Bravo'))
    with open(off, 'wb') as f:
        f.write(data.replace(b'FHA1\r\n', b'FHA1\r\nFLA0\r\n'))
    rc, out = numbers_check(good)
    check('checknumbers accepts a document with the tags on and no typed '
          'number', rc == 0, out)
    rc, out = numbers_check(typed)
    check('checknumbers refuses a typed number in a headline',
          rc == 1 and 'typed number' in out, out)
    rc, out = numbers_check(off)
    check('checknumbers refuses a document with the tags off',
          rc == 1 and 'FLA0' in out, out)


tmp = tempfile.mkdtemp(prefix='numrun')
run_case('runs', tmp, True, EXPECT)
run_case('plain', tmp, False, PLAIN)
check_program(tmp)
shutil.rmtree(tmp)
print('numrun: %s' % ('all passed' if fails == 0 else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
