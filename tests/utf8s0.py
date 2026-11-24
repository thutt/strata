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

"""UTF-8 stage 0 through the real Editor in a pty: the keyboard (a character
of four bytes is one key), and the UTF-8 and Latin-1 choices of the Import and
Export panels of the File menu."""
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


def texts(path):
    """The whole document, as bytes; the typed text is in a T record."""
    return read(path)


FILE = ['F12', 'ENTER']
ITEM = {'import': 4, 'export': 5}


def item(name):
    return FILE + ['RIGHT'] * ITEM[name] + ['ENTER', 'wait']


def valid_utf8(data):
    try:
        data.decode('utf-8')
    except UnicodeDecodeError:
        return False
    return len(data) > 0


def first_line(d):
    """The screen line that holds the text typed at the start."""
    for line in d.split('\n'):
        if 'Orchard Plan' in line:
            return line
    return ''


# ---- The keyboard: a character of four bytes is one key ---------------------
# The text is a, e acute, an emoji (four bytes), b, a Chinese character (three
# bytes, no place in CP437), c and d.  The result must be a, e acute, b, c, d.
typed = 'a\u00e9\U0001F600b\u4e2dcd'
doc = fresh('key1.doc')
d = fm(doc, ['text:' + typed, 'C-s', 'wait', 'show'])
check('keyboard: a, e acute, emoji, b, Chinese, c, d gives a e acute b c d',
    'a\u00e9bcdOrchard Plan' in first_line(d[0]), d[0])
check('keyboard: the saved text is the bytes a, 82, b, c, d and no other',
    b'a\x82bcd\x1cOrchard' in read(doc), repr(read(doc)[300:500]))
for name, ch in (('four byte character', '\U0001F600'),
                 ('four byte character of the last plane', '\U0010FFFF'),
                 ('three byte character', '\u20ac'),
                 ('Chinese character', '\u4e2d')):
    doc = fresh('key2.doc')
    fm(doc, ['text:x' + ch + 'y', 'C-s', 'wait', 'show'])
    check('keyboard: a %s is dropped as one key' % name,
        b'xy\x1cOrchard' in read(doc), repr(read(doc)[300:500]))
doc = fresh('key3.doc')
fm(doc, ['text:\u00e0\U0001F600\U0001F600\u00e8', 'C-s', 'wait', 'show'])
check('keyboard: two four byte characters in a row are two dropped keys',
    b'\x85\x8a\x1cOrchard' in read(doc), repr(read(doc)[300:500]))
doc = fresh('key4.doc')
fm(doc, ['text:x\u00a0y', 'C-s', 'wait', 'show'])
check('keyboard: a no-break space is saved as the character 1E',
    b'x\x1ey\x1cOrchard' in read(doc), repr(read(doc)[300:500]))

# ---- Import ---------------------------------------------------------------
# The message line after an Import is on the last screen line.
def imp(name, data, keys_after=(), sets=0):
    path = os.path.join(tmp, name + '.txt')
    open(path, 'wb').write(data)
    target = fresh(name + '.doc')
    d = fm(target, item('import') + ['DOWN', 'text:' + path, 'DOWN',
        'DOWN'] + ['RIGHT'] * sets + ['INS', 'wait', 'show', 'C-s', 'wait'])
    return d[0], texts(target)


# UTF-8 with a byte order mark; a Chinese word and an emoji have no place.
scr, t = imp('u8a', b'\xef\xbb\xbfCaf\xc3\xa9 \xe4\xb8\xad\xe6\x96\x87 '
    b'\xf0\x9f\x98\x80 end\n', sets=5)
check('import UTF-8: the text is read, characters with no place are ?',
    b'Caf\x82 ?? ? end' in t, "see the file")
check('import UTF-8: the byte order mark is skipped',
    b'\xef\xbb' not in t and b'\xfe' not in t, repr(read(doc)[300:500]))
check('import UTF-8: the message line counts what was not kept',
    '3 characters not kept' in scr.split('\n')[-1], scr)

# Nothing is lost: no count, the usual message.
scr, t = imp('u8b', b'Caf\xc3\xa9 \xc3\xbc\xc3\xa0 \xe2\x95\x90 '
    b'\xc2\xa0x\n', sets=5)
check('import UTF-8: letters, a box character and the no-break space kept',
    b'Caf\x82 \x81\x85 \xcd \x1ex' in t, "see the file")
check('import UTF-8: nothing lost gives the usual message',
    'Imported ' in scr and 'not kept' not in scr, scr)

# Bytes that are not UTF-8 are Latin-1 characters.
scr, t = imp('u8c', b'ab\xe9cd \xfe\xc3( \x80\n', sets=5)
check('import UTF-8: a byte that is not UTF-8 is taken as Latin-1',
    b'ab\x82cd ??( ?' in t, "see the file")
check('import UTF-8: such a byte with no place is counted', '3 characters '
    'not kept' in scr, scr)

# A cut sequence at the end of the file.
scr, t = imp('u8d', b'end\xe4\xb8\n', sets=5)
check('import UTF-8: a sequence cut at the end is taken as Latin-1 bytes',
    b'end\x84?\x1cOrchard' in t and '1 character not kept' in scr, scr)

# Latin-1.
scr, t = imp('l1', b'caf\xe9 \xa0x \x80 \xd7\n', sets=6)
check('import Latin-1: letters, the no-break space; the rest is ?',
    b'caf\x82 \x1ex ? ?' in t, "see the file")
check('import Latin-1: the message line counts what was not kept',
    '2 characters not kept' in scr, scr)

# The old sets are as they were: a byte of CP437 is the same byte, no count.
scr, t = imp('c437', b'caf\x82 \xe0\n', sets=0)
check('import CP437: unchanged, and no count', b'caf\x82 \xe0' in t
    and 'not kept' not in scr and 'Imported ' in scr, ("see the file", scr))

# One character lost is said in the singular.
scr, t = imp('one', b'a\xe4\xb8\xadb\n', sets=5)
check('import UTF-8: one character is said in the singular',
    '1 character not kept' in scr and 'characters not kept' not in scr, scr)


# ---- Export ---------------------------------------------------------------
# Typed at the start of the first block: a, e acute, space, alpha, space, a
# box character, space, y diaeresis, a no-break space.  Export Data Only
# writes the block as a line that begins with the typed text.
typed = 'a\u00e9 \u03b1 \u2500 \u00ff\u00a0'


def exp(name, sets):
    doc = fresh(name + '.doc')
    out = os.path.join(tmp, name + '.txt')
    fm(doc, ['text:' + typed] + item('export') + ['DOWN', 'text:' + out,
        'DOWN', 'RIGHT', 'DOWN'] + ['RIGHT'] * sets + ['INS', 'wait'])
    return read(out)


b = exp('xu8', 5)
check('export UTF-8: every character is written as UTF-8',
    b.startswith('a\u00e9 \u03b1 \u2500 \u00ff\u00a0Orchard'.encode('utf-8')),
    b[:40])
check('export UTF-8: the file is valid UTF-8 with CR LF line ends',
    valid_utf8(b) and b.endswith(b'\r\n'), b[:40])
b = exp('xl1', 6)
check('export Latin-1: letters kept, alpha and the box character are ?',
    b.startswith(b'a\xe9 ? ? \xff\xa0Orchard'), b[:40])
b = exp('x437', 0)
check('export CP437: unchanged',
    b.startswith(b'a\x82 \xe0 \xc4 \x98\xffOrchard'), b[:40])
b = exp('xa7', 1)
check('export 7 bit ASCII: unchanged', b.startswith(b'ae ? ? y Orchard'),
    b[:40])

# Export of a Screen Image with UTF-8 writes the screen, with UTF-8 text.
doc = fresh('xs.doc')
out = os.path.join(tmp, 'xs.txt')
fm(doc, ['text:\u00e9'] + item('export') + ['DOWN', 'text:' + out, 'DOWN',
    'DOWN'] + ['RIGHT'] * 5 + ['INS', 'wait'])
check('export UTF-8: a screen image has the UTF-8 character',
    '\u00e9Orchard Plan\r\n'.encode('utf-8') in read(out)
    and b'\x82' not in read(out).split(b'\r\n')[0], read(out)[:60])

shutil.rmtree(tmp)
print('utf8s0: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
