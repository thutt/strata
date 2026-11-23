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

"""The label of a LaTeX marker in the X11 version: the words LaTeX
instructions with the name LaTeX drawn as TeX draws it, the A smaller and
raised and the E lowered.  The test opens a document with LaTeX markers under
Xvfb, takes a picture of the window and measures the ink of the letters, at
several sizes of the screen font, with the cursor on the marker, and with a
label that wraps onto a second row.  The screen text of the dump must still
read LaTeX instructions.  With LOGO_SHOTS set to a directory the pictures
are kept there as PNG files, so that a person can look at them."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, sys, time, shutil, struct, subprocess, tempfile
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bindir = os.environ.get('XBIN', os.path.join(os.environ.get('STRATA_BUILD_DIR',
    ''), 'x11'))
for tool in ('Xvfb', 'xdotool', 'xwd'):
    if not shutil.which(tool):
        print('SKIP: %s not installed' % tool)
        sys.exit(0)
if not os.path.exists(os.path.join(bindir, 'stred')):
    print('SKIP: run `make x11` first')
    sys.exit(0)

sys.path.insert(0, os.path.join(root, 'manual'))
import shots

dfd_r, dfd_w = os.pipe()
xvfb = subprocess.Popen(['Xvfb', '-noreset', '-displayfd', str(dfd_w),
    '-screen', '0', '1280x1024x24'], stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL, pass_fds=(dfd_w,))
os.close(dfd_w)
num = b''
while not num.endswith(b'\n'):
    chunk = os.read(dfd_r, 16)
    if not chunk:
        print('FAIL: Xvfb did not start')
        sys.exit(1)
    num += chunk
os.close(dfd_r)
disp = ':' + num.decode().strip()
tmp = tempfile.mkdtemp()
dump = os.path.join(tmp, 'dump')
shotdir = os.environ.get('LOGO_SHOTS')
fails = 0
procs = []


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def xenv(size):
    return dict(os.environ, DISPLAY=disp, STRATA_X11_DUMP=dump,
        STRATA_X11_SIZE=str(size), STRATA_HOME=os.path.join(tmp, 'home'),
        XDG_CONFIG_HOME=os.path.join(tmp, 'cfg'), SHELL='/bin/true')


def screen():
    try:
        return open(dump, encoding='utf-8').read()
    except OSError:
        return ''


def wait(pred, t=6.0):
    end = time.time() + t
    s = ''
    while time.time() < end:
        s = screen()
        if s and pred(s):
            return s
        time.sleep(0.1)
    return s


def win(env):
    for _ in range(50):
        w = subprocess.run(['xdotool', 'search', '--name', '^stred$'],
            env=env, capture_output=True, text=True).stdout.split()
        if w:
            return w[0]
        time.sleep(0.1)
    return None


def run(doc, size, keys=()):
    """Start the Editor on doc, press the keys, return the process, the
    window, the environment and the screen text."""
    if os.path.exists(dump):
        os.remove(dump)
    env = xenv(size)
    p = subprocess.Popen([os.path.join(bindir, 'stred'), doc], env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    procs.append(p)
    w = win(env)
    s = wait(lambda s: 'Before the markers' in s)
    for k in keys:
        subprocess.run(['xdotool', 'key', '--window', w, '--delay', '70', k],
            env=env, capture_output=True)
        time.sleep(0.3)
    time.sleep(0.6)
    return p, w, env, screen()


def quit_editor(p, w, env):
    subprocess.run(['xdotool', 'key', '--window', w, 'ctrl+q'], env=env,
        capture_output=True)
    time.sleep(0.4)
    p.terminate()
    p.wait()


def shot(w, env, name):
    data = subprocess.run(['xwd', '-silent', '-id', w], env=env,
        capture_output=True).stdout
    if shotdir:
        os.makedirs(shotdir, exist_ok=True)
        shots.xwd_to_png(data, os.path.join(shotdir, name + '.png'))
    return data


class Pic:
    """The pixels of an xwd picture."""

    def __init__(self, data):
        h = struct.unpack('>25I', data[:100])
        self.off = h[0] + h[19] * 12
        self.w, self.h, self.bpl = h[4], h[5], h[12]
        self.data = data

    def px(self, x, y):
        i = self.off + y * self.bpl + x * 4
        return self.data[i:i + 3]


def ink_box(pic, x0, y0, x1, y1):
    """The first and last row (relative to y0) with ink in the rectangle,
    where ink is a pixel that is far from the color most of the rectangle
    has; None when there is no ink."""
    counts = {}
    pix = {}
    for y in range(y0, y1):
        for x in range(x0, x1):
            c = pic.px(x, y)
            pix[(x, y)] = c
            counts[c] = counts.get(c, 0) + 1
    bg = max(counts, key=counts.get)
    top = bot = None
    for y in range(y0, y1):
        for x in range(x0, x1):
            c = pix[(x, y)]
            if sum(abs(c[i] - bg[i]) for i in range(3)) > 150:
                top = y - y0 if top is None else top
                bot = y - y0
    return None if top is None else (top, bot)


def rows_of(s):
    return s.split('\x01')[0].split('\n')


def measure(pic, s, row, col):
    """The boxes of the five cells of LaTeX that starts at the cell col of
    the row: [L, A, T, E, X]; the size of a cell is in the screen text."""
    m = re.search(r'\x01grid (\d+) (\d+) (\d+) (\d+)', s)
    cw, chh = int(m.group(3)), int(m.group(4))
    return [ink_box(pic, (col + i) * cw, row * chh, (col + i + 1) * cw,
        (row + 1) * chh) for i in range(5)], cw, chh


def logo_checks(tag, boxes):
    L, A, T, E, X = boxes
    check(tag + ': every letter of LaTeX has ink', None not in boxes, boxes)
    if None in boxes:
        return
    hL, hA, hT, hE = (L[1] - L[0], A[1] - A[0], T[1] - T[0], E[1] - E[0])
    check(tag + ': the A sits higher than the baseline of L (%d, %d)' %
        (A[1], L[1]), A[1] <= L[1] - 2, boxes)
    check(tag + ': the A is smaller than the L (%d, %d rows)' % (hA, hL),
        hA < hL, boxes)
    check(tag + ': the E sits lower than the baseline of T (%d, %d)' %
        (E[1], T[1]), E[1] >= T[1] + 1, boxes)
    check(tag + ': the E is as tall as the T (%d, %d rows)' % (hE, hT),
        abs(hE - hT) <= 1, boxes)
    check(tag + ': the letters L, T and X share the baseline',
        L[1] == T[1] == X[1], boxes)


# The documents: print_latex.doc of the tests, and the same with a long
# beginning of the sentence so that the label of the marker in it wraps.
src = os.path.join(root, 'tests', 'data', 'print_latex.doc')
base = open(src, 'rb').read()
doc1 = os.path.join(tmp, 'l.doc')
# The markers of print_latex.doc may be drawn as pictures: LaTeX determines, and
# some of them make something to see.  The option record OM41,2 (the mode
# Short for the LaTeX marker) makes the label of every marker be drawn, which
# is what this check is about.
open(doc1, 'wb').write(base.replace(b'\r\n*B0', b'\r\nOM41,2\r\n*B0', 1))
# 62 characters of words and a space: the label then starts so that
# [LaTeX fits in the row and instructions] does not.
filler = (b'abcdefg ' * 10)[:62] + b' '
doc2 = os.path.join(tmp, 'w.doc')
# The marker after the filler holds text that LaTeX can typeset on a line,
# so with the default display mode (Inline) it is drawn as a picture and has
# no label.  The option record OM41,2 (the mode Short for the LaTeX marker)
# makes the label of every marker be drawn, which is what this check is about.
wrapped = base.replace(b'A sentence with ', filler, 1)
wrapped = wrapped.replace(b'\r\n*B0', b'\r\nOM41,2\r\n*B0', 1)
open(doc2, 'wb').write(wrapped)

try:
    for size in (12, 16, 24):
        p, w, env, s = run(doc1, size)
        rows = rows_of(s)
        at = [i for i, r in enumerate(rows) if 'LaTeX instructions' in r]
        check('size %d: the screen text reads LaTeX instructions' % size,
            len(at) >= 2, s)
        if at:
            row = at[0]
            col = rows[row].index('LaTeX')
            pic = Pic(shot(w, env, 'latex-marker-%d' % size))
            boxes, cw, chh = measure(pic, s, row, col)
            logo_checks('size %d' % size, boxes)
        quit_editor(p, w, env)

    # The cursor on the marker: the label is in reverse video.
    p, w, env, s = run(doc1, 16, ['Down', 'Down'])
    rows = rows_of(s)
    cur = re.search(r'\x01cursor (\d+) (\d+) (\d+)', s)
    at = [i for i, r in enumerate(rows) if 'LaTeX instructions' in r]
    if at:
        row = at[0]
        col = rows[row].index('LaTeX')
        pic = Pic(shot(w, env, 'latex-marker-cursor'))
        boxes, cw, chh = measure(pic, s, row, col)
        logo_checks('cursor on the marker', boxes)
    else:
        check('cursor on the marker: the marker is on the screen', False, s)
    quit_editor(p, w, env)

    # A label over two rows: LaTeX is in the first one.
    p, w, env, s = run(doc2, 16)
    rows = rows_of(s)
    at = [i for i, r in enumerate(rows) if 'LaTeX' in r and
        'instructions' not in r.replace('LaTeX', '')]
    check('wrapped label: the display mode of the LaTeX marker is Short in '
        'the test document', b'\r\nOM41,2\r\n' in wrapped, '')
    check('wrapped label: the words LaTeX and instructions are on two rows',
        len(at) >= 1 and any('instructions]' in rows[i + 1] for i in at
        if i + 1 < len(rows)), s)
    if at:
        row = at[0]
        col = rows[row].index('LaTeX')
        pic = Pic(shot(w, env, 'latex-marker-wrapped'))
        boxes, cw, chh = measure(pic, s, row, col)
        logo_checks('wrapped label', boxes)
    quit_editor(p, w, env)
finally:
    for p in procs:
        if p.poll() is None:
            p.terminate()
    xvfb.terminate()
    shutil.rmtree(tmp, ignore_errors=True)

print('xlogo: %d failures' % fails)
sys.exit(1 if fails else 0)
