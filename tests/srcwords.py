#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The sources do not name the earlier product: no file of src or tools has
the word Manuscript, and no comment or name has the word original (a message
that the user reads may say "the original is not changed" of a file).  Run on
its own:

    python3 -I -B tests/srcwords.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
fails = 0
checked = 0

STRING = re.compile(r'"(?:[^"\\\n]|\\.)*"')
MANUSCRIPT = re.compile(r'manuscript', re.I)
ORIGINAL = re.compile(r'\boriginal', re.I)


def files(folder):
    for name in sorted(os.listdir(os.path.join(root, folder))):
        path = os.path.join(root, folder, name)

        if os.path.isfile(path) and name.endswith(
                ('.c', '.h', '.py', '.sh', '.txt')):
            yield folder + '/' + name, path


def words_of(text, strip_strings):
    """The lines of a file with the strings of C removed when asked."""
    for n, line in enumerate(text.split('\n'), 1):
        yield n, STRING.sub('""', line) if strip_strings else line


for rel, path in list(files('src')) + list(files('tools')):
    with open(path, encoding='utf-8', errors='replace') as f:
        text = f.read()

    checked += 1
    c_source = rel.endswith(('.c', '.h'))

    for n, line in words_of(text, False):
        if MANUSCRIPT.search(line):
            fails += 1
            print('FAIL %s:%d names the earlier product: %s' %
                  (rel, n, line.strip()))

    for n, line in words_of(text, c_source):
        if ORIGINAL.search(line):
            fails += 1
            print('FAIL %s:%d says original: %s' % (rel, n, line.strip()))

print('srcwords: %d files, %d failures' % (checked, fails))
sys.exit(1 if fails else 0)
