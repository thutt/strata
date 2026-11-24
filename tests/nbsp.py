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

"""The no-break space through the real Editor in a pty: Ctrl-Shift-Space types
it (a terminal that reports modified keys sends CSI 32;6u or CSI 27;6;32~; a
terminal that sends only the byte 00 for Ctrl-Space, Ctrl-Shift-Space and
Ctrl-@ gets it too), the screen shows a cap (EF) that no line is
broken at, the saved document holds the character 1E, a macro can record the
key, and Export writes the character of the code page chosen."""
import sys as _s; _s.dont_write_bytecode = True
import os, sys, shutil, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
stred = os.environ.get('STRED', os.path.join(os.environ.get('STRATA_BUILD_DIR',
    ''), 'term', 'stred'))
sample = os.environ.get('DOCSRC', os.path.join(root, 'tests', 'samples'))
helpf = os.environ.get('STRATA_HELP', os.path.join(
    os.environ.get('STRATA_BUILD_DIR', ''), 'strata.hlp'))
fails = 0
tmp = tempfile.mkdtemp()


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def session(*args, **kw):
    os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='home')
    return tui.session(*args, **kw)


def fresh(name):
    path = os.path.join(tmp, name)
    shutil.copy(os.path.join(sample, 'plan-title-table.doc'), path)
    return path


def fm(path, keys):
    return session([stred, '-H', helpf, path], keys)


def read(path):
    return open(path, 'rb').read() if os.path.exists(path) else b''


def first_line(d):
    for line in d.split('\n'):
        if 'Orchard Plan' in line:
            return line
    return ''


# ---- The key ---------------------------------------------------------------
doc = fresh('key1.doc')
d = fm(doc, ['text:x', 'CS-SPACE', 'text:y', 'show', 'C-s', 'wait'])
check('Ctrl-Shift-Space shows a cap (EF) between x and y, not a blank',
    'x\u2229yOrchard Plan' in first_line(d[0]), d[0])
check('a plain space of the text is still a blank on the screen',
    'Orchard Plan' in first_line(d[0]) and
    first_line(d[0]).count('\u2229') == 1, first_line(d[0]))
check('Ctrl-Shift-Space is saved as the character 1E',
    b'x\x1ey\x1cOrchard' in read(doc), repr(read(doc)[300:400]))
doc = fresh('key2.doc')
fm(doc, ['text:a', 'CS-SPACE', 'CS-SPACE', 'text:b', 'C-s', 'wait'])
check('two of them in a row are two characters',
    b'a\x1e\x1eb\x1cOrchard' in read(doc), repr(read(doc)[300:400]))
doc = fresh('key3.doc')
fm(doc, ['text:ab', 'LEFT', 'CS-SPACE', 'C-s', 'wait'])
check('Ctrl-Shift-Space in the middle of a word splits it',
    b'a\x1eb\x1cOrchard' in read(doc), repr(read(doc)[300:400]))


# The other forms of the key.  The byte 00 (what most terminals send for
# Ctrl-Shift-Space, Ctrl-Space and Ctrl-@) and the sequence "27;6;32~" type it;
# the sequences of Ctrl-Space alone with a modifier do nothing.
doc = fresh('key4.doc')
fm(doc, ['text:a', 'C-SPACE', 'text:b', 'CS-SPACE-MOK', 'text:c', 'C-s',
         'wait'])
check('the byte 00 and the sequence 27;6;32~ type it',
    b'a\x1eb\x1ec\x1cOrchard' in read(doc), repr(read(doc)[300:400]))
doc = fresh('key5.doc')
fm(doc, ['text:a', 'C-SPACE-U', 'text:b', 'C-SPACE-MOK', 'text:c', 'C-s',
         'wait'])
check('Ctrl-Space reported as a modified key (CSI 32;5u, 27;5;32~) does '
      'nothing', b'abc\x1cOrchard' in read(doc), repr(read(doc)[300:400]))


# ---- The screen: no line ends at a no-break space ---------------------------
# A paragraph is words, a word of k letters that moves the pair, and the
# pair.  Over many k the pair meets the end of a line; with a plain space the
# line ends after the first word at least once, with Ctrl-Shift-Space never.
def paragraphs(sep):
    keys = []
    for k in range(24):
        keys += ['text:' + 'word ' * 13 + 'w' * k + ' alpha']
        keys += ['CS-SPACE'] if sep == 'nbsp' else ['text: ']
        keys += ['text:beta and some more words follow here.', 'show',
                 'ENTER']
    return keys


def row_ends(dumps, word):
    n = 0
    for d in dumps:
        for line in d.split('\n'):
            if line.rstrip().endswith(' ' + word):
                n += 1
    return n


doc = fresh('wrap1.doc')
tied = fm(doc, paragraphs('nbsp'))
doc = fresh('wrap2.doc')
plain = fm(doc, paragraphs('space'))
check('screen: with a plain space a line ends after the first word',
    row_ends(plain, 'alpha') > 0, plain[0])
check('screen: with a no-break space no line ends after the first word',
    row_ends(tied, 'alpha') == 0 and
    any('alpha\u2229beta' in d for d in tied), row_ends(tied, 'alpha'))
check('screen: the pair of the plain space is drawn with a blank, no cap',
    any('alpha beta' in d for d in plain) and
    not any('\u2229' in d for d in plain), plain[0])

# ---- A macro can record the key --------------------------------------------
MACM = ['F12', 'text:T', 'text:M']
LEARN = MACM + ['text:L', 'wait', 'text:n', 'DOWN', 'text:nbsp key', 'DOWN',
    'RIGHT', 'INS', 'wait', 'text:a', 'CS-SPACE', 'C-F1', 'wait']
doc = fresh('mac1.doc')
fm(doc, LEARN + ['A-n', 'wait', 'C-s', 'wait'])
check('a macro that was learned with Ctrl-Shift-Space types it when it runs',
    b'a\x1ea\x1e\x1cOrchard' in read(doc), repr(read(doc)[300:400]))

# ---- Export ---------------------------------------------------------------
FILE = ['F12', 'ENTER']


def export(name, sets):
    doc = fresh(name + '.doc')
    out = os.path.join(tmp, name + '.txt')
    fm(doc, ['text:x', 'CS-SPACE', 'text:y'] + FILE + ['RIGHT'] * 5 +
       ['ENTER', 'wait', 'DOWN', 'text:' + out, 'DOWN', 'RIGHT', 'DOWN'] +
       ['RIGHT'] * sets + ['INS', 'wait'])
    return read(out)


b = export('xu8', 5)
check('Export UTF-8 writes U+00A0 for it', b.startswith(
    'x yOrchard'.encode('utf-8')), b[:30])
b = export('xl1', 6)
check('Export Latin-1 writes the byte A0 for it',
    b.startswith(b'x\xa0yOrchard'), b[:30])

print('nbsp: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
