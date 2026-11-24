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

"""The soft hyphen: the place where a word may be broken.

Through the real Editor in a pty: Alt-Hyphen (Alt-_, ESC _ in a terminal) and
the entry Soft hyphen of the Ctrl-F menu type it, the screen shows a dim
triangle where it is, the saved document holds the character 1F as the
original did, Export writes nothing for it, and the Printer writes \\- and so
a word is broken there at the end of a line and is whole elsewhere (pdftotext
of a printed document).

    STRED=... STRPRINT=... python3 -I -B tests/shy.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term',
    'strprint'))
sample = os.environ.get('DOCSRC', os.path.join(root, 'tests', 'samples'))
helpf = os.environ.get('STRATA_HELP', os.path.join(root, '..',
    'lotus-manuscript', 'utility_3', 'strata.hlp'))
fails = 0
tmp = tempfile.mkdtemp()
os.environ['SOURCE_DATE_EPOCH'] = '1700000000'


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


# ---- The key and the menu ----------------------------------------------------
doc = fresh('key1.doc')
d = fm(doc, ['text:hy', 'A-HYPHEN', 'text:phen', 'show', 'C-s', 'wait'])
check('Alt-Hyphen is saved as the character 1F',
    b'hy\x1fphen\x1cOrchard' in read(doc), repr(read(doc)[300:400]))
check('the screen shows a triangle (the mark of a soft hyphen) in the word',
    'hy\u25bcphenOrchard Plan' in first_line(d[0]), d[0])
doc = fresh('key2.doc')
fm(doc, ['text:a', 'A-HYPHEN', 'A-HYPHEN', 'text:b', 'C-s', 'wait'])
check('two in a row are two characters',
    b'a\x1f\x1fb\x1cOrchard' in read(doc), repr(read(doc)[300:400]))
doc = fresh('key3.doc')
fm(doc, ['text:ab', 'LEFT', 'A-HYPHEN', 'C-s', 'wait'])
check('in the middle of a word it does not split the word on the screen',
    b'a\x1fb\x1cOrchard' in read(doc), repr(read(doc)[300:400]))

# Ctrl-/ (the byte 1F, also what Ctrl-Hyphen sends) is Undo and no longer a
# soft hyphen; Alt-minus is still the collapse of a section.
doc = fresh('key4.doc')
fm(doc, ['text:ab', 'C-SLASH', 'text:c', 'C-s', 'wait'])
check('the byte 1F is Undo: it types no soft hyphen',
    b'\x1f' not in read(doc)[100:] and b'c\x1cOrchard' in read(doc),
    repr(read(doc)[300:400]))

# The menu route: Ctrl-F opens the list of the Alt commands; Soft hyphen is
# its last entry.
doc = fresh('menu1.doc')
fm(doc, ['text:co', 'C-f'] + ['DOWN'] * 33 + ['ENTER', 'text:ver', 'C-s',
   'wait'])
check('Ctrl-F, Soft hyphen types it where the key does not reach the program',
    b'co\x1fver\x1cOrchard' in read(doc), repr(read(doc)[300:400]))

# ---- The file --------------------------------------------------------------
doc = fresh('rt1.doc')
fm(doc, ['text:q', 'A-HYPHEN', 'C-s', 'wait'])
once = read(doc)
fm(doc, ['C-s', 'wait'])
check('a document with soft hyphens is saved again as it was',
    read(doc) == once and b'q\x1f\x1cOrchard' in once, repr(once[300:400]))
tui.drop_place(doc)
fm(doc, ['text:z', 'C-s', 'wait'])
check('and a change elsewhere keeps the soft hyphen',
    b'zq\x1f\x1cOrchard' in read(doc), repr(read(doc)[300:400]))

# ---- Export ----------------------------------------------------------------
FILE = ['F12', 'ENTER']


def export(name, sets):
    doc = fresh(name + '.doc')
    out = os.path.join(tmp, name + '.txt')
    fm(doc, ['text:x', 'A-HYPHEN', 'text:y'] + FILE + ['RIGHT'] * 5 +
       ['ENTER', 'wait', 'DOWN', 'text:' + out, 'DOWN', 'RIGHT', 'DOWN'] +
       ['RIGHT'] * sets + ['INS', 'wait'])
    return read(out)


b = export('xu8', 5)
check('Export UTF-8 writes nothing for it', b.startswith(
    'xyOrchard'.encode('utf-8')), b[:30])
check('and no triangle', '\u25bc'.encode('utf-8') not in b and b'\x1f' not in b,
    b[:60])

# Export a Screen Image: the rows of the screen, without the mark.
doc = fresh('xs1.doc')
out = os.path.join(tmp, 'xs1.txt')
fm(doc, ['text:x', 'A-HYPHEN', 'text:y'] + FILE + ['RIGHT'] * 5 + ['ENTER',
   'wait', 'DOWN', 'text:' + out, 'INS', 'wait'])
b = read(out)
check('Export a Screen Image has the word whole and no mark',
    b'xyOrchard' in b[:80] and b'\x1f' not in b and
    '\u25bc'.encode('utf-8') not in b, b[:60])

# ---- The print -------------------------------------------------------------
CRLF = b'\r\n'
EOL = b'\x1d'


def trec(text):
    return b'T' + text + EOL + b'\x00' + CRLF


HDR = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF + b'P1HB0' +
       CRLF + b'P1FB0' + CRLF + b'P1C33' + CRLF + b'FHA1' + CRLF +
       b'FTT5' + CRLF + b'DP0,8500,11000' + CRLF + b'YCL0' + CRLF)


def shydoc(soft, word=b'Zqvxkjwbxzpl\x1fqrstvwnzghkb\x1fmncxzlkjhgfd'):
    """A document of 40 paragraphs: k short words, then a word that TeX does
    not know how to break (so only a soft hyphen can), then more text."""
    if not soft:
        word = word.replace(b'\x1f', b'')
    out = HDR + b'*B0' + CRLF + b'S' + CRLF
    for k in range(40):
        t = b'ab ' * (k % 40) + word + b' and some words to end it.'
        out += (b'' if k == 0 else b'B' + CRLF) + trec(t)
    return out + b'*E' + CRLF


def prt(name, data):
    d = os.path.join(tmp, name)
    os.makedirs(d, exist_ok=True)
    p = os.path.join(d, 'doc.doc')
    open(p, 'wb').write(data)
    tex = os.path.join(d, 'x.tex')
    pdf = os.path.join(d, 'doc.pdf')
    subprocess.run([strprint, p, '--export-latex', tex],
        capture_output=True)
    subprocess.run([strprint, p, '-o', pdf, '--no-banner'],
        capture_output=True, timeout=300)
    text = ''
    if os.path.exists(pdf):
        text = subprocess.run(['pdftotext', '-layout', pdf, '-'],
            capture_output=True, text=True, errors='replace').stdout
    return (open(tex).read() if os.path.exists(tex) else ''), text


if shutil.which('pdflatex') and shutil.which('pdftotext'):
    tex, text = prt('soft', shydoc(True))
    check('the Printer writes \\- for it, and for nothing else',
        tex.count('Zqvxkjwbxzpl\\-qrstvwnzghkb\\-mncxzlkjhgfd') == 40 and
        tex.count('\\-') == 80, tex[-400:])
    lines = [' '.join(l.split()) for l in text.split('\n')]
    broke = [l for l in lines if re.search(r'Zqvxkjwbxzpl-$|qrstvwnzghkb-$',
        l)]
    whole = [l for l in lines if 'Zqvxkjwbxzplqrstvwnzghkbmncxzlkjhgfd' in l]
    check('a word breaks at its soft hyphen where the line ends (%d times)' %
        len(broke), len(broke) >= 1, text[:600])
    check('the word is whole where it does not (%d lines)' % len(whole),
        len(whole) >= 15, text[:600])
    check('a broken word has its rest at the start of the next line',
        all(i + 1 < len(lines) and re.match(r'(qrstvwnzghkb|mncxzlkjhgfd)',
            lines[i + 1]) for i, l in enumerate(lines)
            if re.search(r'Zqvxkjwbxzpl-$|qrstvwnzghkb-$', l)), text[:600])
    tex, text = prt('plain', shydoc(False))
    lines = [' '.join(l.split()) for l in text.split('\n')]
    check('the same word without it is not broken at those places (the '
        'control)', '\\-' not in tex and not any(re.search(
            r'Zqvxkjwbxzpl-$|qrstvwnzghkb-$', l) for l in lines), text[:300])
else:
    print('SKIP: pdflatex or pdftotext is not installed')

print('shy: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
