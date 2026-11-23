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

"""The keycap images of the X11 version have no bar.  The picture of a
keycap is made by LaTeX and pdftoppm; a page that ends inside a pixel left
one pixel column (and sometimes one row) in the paper color, white, at the
right of the key: a thin bar after the key.  The test opens a document with
keycaps of different widths under Xvfb, at several sizes of the screen font
(the sizes change the fractions of a pixel), takes a picture of the window
and looks for white pixels inside the cells of every image.  The window
colors have no white: the text is yellow on blue, and the cursor is on the
first character.  With KEYCAP_SHOTS set to a directory the pictures are kept
there as PNG files, so that a person can look at them."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, sys, time, shutil, struct, subprocess, tempfile
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bindir = os.environ.get('XBIN', os.path.join(os.environ.get('STRATA_BUILD_DIR',
    ''), 'x11'))
for tool in ('Xvfb', 'xdotool', 'xwd', 'pdflatex', 'pdftoppm'):
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
shotdir = os.environ.get('KEYCAP_SHOTS')
fails = 0
procs = []
CRLF = b'\r\n'
EOL = b'\x1d'
HEADER = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
          b'P1C33' + CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF +
          b'DP0,8500,11000' + CRLF)
LEGENDS = ['Ctrl-S', 'F12', 'Right', 'Esc', 'Alt-Home', 'Ctrl-Shift-Z']


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def keycap(legend):
    return b'\x1337(1' + legend.encode() + b'\x00' + CRLF + b'\x13)'


def document():
    """One block, one line per keycap, the first character is plain text."""
    text = b''
    for leg in LEGENDS:
        text += (b'Press ' + keycap(leg) + b' to continue, then ' +
                 keycap(leg) + b' again.' + EOL)
    return (HEADER + b'*B0' + CRLF + b'S' + CRLF + b'T' + text + b'\x00' +
            CRLF + b'*E' + CRLF)


def screen():
    try:
        return open(dump, encoding='utf-8').read()
    except OSError:
        return ''


def image_cells(s):
    """The cells of the screen text that an image covers."""
    rows = s.split('\x01')[0].split('\n')
    return {(y, x) for y, r in enumerate(rows) for x, c in enumerate(r)
            if c == '\ue000'}


def components(cells):
    """The runs of image cells of one row, as sets of cells: an image is
    one run in every row that it covers, and the images of two lines that
    touch must not be mixed up."""
    out = []
    for y in sorted({y for y, x in cells}):
        xs = sorted(x for yy, x in cells if yy == y)
        run = [xs[0]]
        for x in xs[1:]:
            if x == run[-1] + 1:
                run.append(x)
            else:
                out.append({(y, c) for c in run})
                run = [x]
        out.append({(y, c) for c in run})
    return out


class Pic:
    """The pixels of an xwd picture."""

    def __init__(self, data):
        h = struct.unpack('>25I', data[:100])
        self.off = h[0] + h[19] * 12
        self.w, self.h, self.bpl = h[4], h[5], h[12]
        self.data = data

    def px(self, x, y):
        i = self.off + y * self.bpl + x * 4
        return tuple(self.data[i:i + 3])


def run(size):
    """Start the Editor on the document and wait until every keycap image is
    on the screen; return the process, the window and the screen text."""
    if os.path.exists(dump):
        os.remove(dump)
    doc = os.path.join(tmp, 'k.doc')
    open(doc, 'wb').write(document())
    env = dict(os.environ, DISPLAY=disp, STRATA_X11_DUMP=dump,
        STRATA_X11_SIZE=str(size), STRATA_HOME=os.path.join(tmp, 'home'),
        XDG_CONFIG_HOME=os.path.join(tmp, 'cfg'),
        XDG_CACHE_HOME=os.path.join(tmp, 'cache' + str(size)),
        SHELL='/bin/true')
    p = subprocess.Popen([os.path.join(bindir, 'stred'), doc], env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    procs.append(p)
    w = None
    for _ in range(100):
        w = subprocess.run(['xdotool', 'search', '--name', '^stred$'],
            env=env, capture_output=True, text=True).stdout.split()
        if w:
            break
        time.sleep(0.1)
    end = time.time() + 240
    s = ''
    want = 2 * len(LEGENDS)  # runs: at least one for every keycap
    while time.time() < end:
        s = screen()
        if s and len(components(image_cells(s))) >= want:
            break
        time.sleep(0.3)
    time.sleep(1.0)
    return p, (w[0] if w else None), env, screen()


def shot(w, env, name):
    data = subprocess.run(['xwd', '-silent', '-id', w], env=env,
        capture_output=True).stdout
    if shotdir:
        os.makedirs(shotdir, exist_ok=True)
        shots.xwd_to_png(data, os.path.join(shotdir, name + '.png'))
    return data


try:
    for size in (12, 16, 20):
        p, w, env, s = run(size)
        comps = components(image_cells(s))
        check('size %d: every keycap is an image on the screen (%d runs)' %
            (size, len(comps)), len(comps) >= 2 * len(LEGENDS), s)
        m = re.search(r'\x01grid (\d+) (\d+) (\d+) (\d+)', s)
        cw, chh = int(m.group(3)), int(m.group(4))
        pic = Pic(shot(w, env, 'keycap-%d' % size))
        bad = []
        for g in comps:
            ys = [y for y, x in g]
            xs = [x for y, x in g]
            x0, x1 = min(xs) * cw, (max(xs) + 1) * cw
            y0, y1 = min(ys) * chh, (max(ys) + 1) * chh
            white = [(x, y) for y in range(y0, y1) for x in range(x0, x1)
                     if pic.px(x, y) == (255, 255, 255)]
            if white:
                bad.append((x0, y0, x1, y1, white[:3], len(white)))
        check('size %d: no white pixel in the cells of any keycap image' %
            size, not bad, bad)
        p.terminate()
        p.wait()
finally:
    for p in procs:
        if p.poll() is None:
            p.terminate()
    xvfb.terminate()
    shutil.rmtree(tmp, ignore_errors=True)

print('xkeycap: %d failures' % fails)
sys.exit(1 if fails else 0)
