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

"""The cursor goes over a picture with one key in the terminal Editor.  A
picture is one line of the text, and the terminal shows it as one row (the
name of the picture in brackets), so Down and Up take one key for each line
whether it is a picture or text: a picture that is the first or the last line
of the document, two pictures in a row, a picture between two text lines, in
an unstructured and in a structured document.  (The X11 window draws the
picture over many rows; tests/xpicscroll.py checks it there, and checks that
the window glides when the cursor goes onto a picture or off one.)  The
cursor is found by typing a letter at it and reading which line has it."""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, struct, sys, tempfile, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

bdir = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(bdir, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(bdir, 'strata.hlp'))
if not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
CRLF = b'\r\n'
EOL = b'\x1d'
HEADER = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
          b'P1C33' + CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF +
          b'DP0,8500,11000' + CRLF)
PIC = b'\x1325(1pics/p.png\x00' + CRLF + b'22000' + CRLF + b'\x13)'
# The lines of the test document, in order; a name that starts with PIC is a
# picture.
LINES = ['PIC first', 'Line A', 'Line B', 'PIC middle', 'Line C',
         'PIC one of two', 'PIC two of two', 'Line D', 'PIC last']


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def png(path):
    raw = b''.join(b'\x00' + b'\x80\x40\x20' * 16 for _ in range(8))

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)

    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' +
        chunk(b'IHDR', struct.pack('>IIBBBBB', 16, 8, 8, 2, 0, 0, 0)) +
        chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def document(structured):
    if structured:
        out = (HEADER + b'*B1' + CRLF + b'S' + CRLF + b'+' + CRLF + b'S' +
               CRLF + b'THead' + EOL + b'\x00' + CRLF)
        first = False
    else:
        out = HEADER + b'*B0' + CRLF + b'S' + CRLF
        first = True
    for name in LINES:
        body = PIC if name.startswith('PIC') else name.encode()
        out += (b'' if first else b'B' + CRLF) + b'T' + body + EOL + \
            b'\x00' + CRLF
        first = False
    return out + b'*E' + CRLF


def where(shows):
    """For each screen: the index in LINES of the text line that has the Z
    that was typed, or -2 when it is a picture row."""
    out = []
    for s in shows:
        rows = [r for r in s.split('\n') if 'Z' in r and 'Attr:' not in r]
        txt = rows[0] if rows else ''
        idx = -1
        if '[PIC' in txt:
            idx = -2
        else:
            for i, name in enumerate(LINES):
                if not name.startswith('PIC') and name in txt:
                    idx = i
                    break
        out.append(idx)
    return out


def walk(structured, key, count, start=None):
    """Press key count times from the start of the document (after the keys
    in start); return where the cursor is after each key."""
    tmp = tempfile.mkdtemp()
    try:
        os.makedirs(os.path.join(tmp, 'pics'))
        png(os.path.join(tmp, 'pics', 'p.png'))
        doc = os.path.join(tmp, 't.doc')
        open(doc, 'wb').write(document(structured))
        steps = ['wait']
        if structured:
            steps += ['DOWN']
        steps += list(start or [])
        for _ in range(count):
            steps += [key, 'text:Z', 'show', 'BKSP']
        env = {'STRATA_HOME': os.path.join(tmp, 'home')}
        os.chdir(tmp)
        shows = tui.session([stred, '-H', helpf, doc], steps, rows=40,
                            env=env)
        return where(shows), shows
    finally:
        os.chdir('/')
        shutil.rmtree(tmp, ignore_errors=True)


# Down from the first line (a picture): LINES index or -2 for a picture.
EXPECT = [1, 2, -2, 4, -2, -2, 7, -2]
# Up from the last line (a picture).
BACK = [7, -2, -2, 4, -2, 2, 1, -2]
for structured in (False, True):
    label = 'structured' if structured else 'unstructured'
    got, shows = walk(structured, 'DOWN', len(EXPECT))
    check('%s: Down goes line by line, one key for each, a picture too (%s)'
        % (label, got), got == EXPECT, '\n'.join(shows[:2]))
    start = ['DOWN'] * (len(LINES) - 1)
    got, shows = walk(structured, 'UP', len(BACK), start)
    check('%s: Up goes back line by line, one key for each (%s)'
        % (label, got), got == BACK, '\n'.join(shows[:2]))

print('picturecursor: %d failures' % fails)
sys.exit(1 if fails else 0)
