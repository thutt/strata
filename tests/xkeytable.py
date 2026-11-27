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

"""Every keycap of a table is drawn in the X11 version.  The table is the
one of the section xfce4-terminal of Getting Started (the sources of the
manual), which has a keycap at the start of its first column and another one
at the start of its third column in the same lines.  The window gives a
picture to the display once for each picture, and it used to tell the
pictures apart by the line and the token only, so that the keycap of the third
column, which has the same token number as the one of the first column, was
taken for the same picture and left out: the cells stayed empty, except for
the keycaps that are later in their text.  The test makes the document with
the generator of the manual, opens it under Xvfb, waits until the pictures
have been made, and looks at the pixels of every picture: a picture whose
cells are all of the color of the window is missing.  It also counts the
keycaps that are drawn as labels when LaTeX cannot be run (a debug build
only, which reads STRATA_PROGRAM_DIRS)."""
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

fails = 0
procs = []
tmp = tempfile.mkdtemp()
dump = os.path.join(tmp, 'dump')
shotdir = os.environ.get('KEYCAP_SHOTS')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def table_source():
    """The lines of the first table that follows the heading xfce4-terminal
    in the sources of the manual, as a source of its own."""
    lines = open(os.path.join(root, 'manual', 'src', 'gs-need.src'),
                 encoding='ascii').read().split('\n')
    at = lines.index('.h3 xfce4-terminal [term-xfce4]')
    start = next(i for i in range(at, len(lines))
                 if lines[i].startswith('.table '))
    end = next(i for i in range(start, len(lines)) if lines[i] == '.end')
    return '.doc Keycap table\n.h1 Keycaps\n' + '\n'.join(
        lines[start:end + 1]) + '\n'


def make_document(path):
    src = os.path.join(tmp, 'table.src')
    text = table_source()
    open(src, 'w', encoding='ascii').write(text)
    r = subprocess.run([sys.executable, '-I', '-B',
                        os.path.join(root, 'manual', 'mkmanual.py'), src, path],
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr, text.count('{k:')


def screen():
    try:
        return open(dump, encoding='utf-8').read()
    except OSError:
        return ''


def image_cells(s):
    rows = s.split('\x01')[0].split('\n')
    return {(y, x) for y, r in enumerate(rows) for x, c in enumerate(r)
            if c == ''}


def pictures(cells):
    """The cells of each picture: the runs of one row that touch a run of the
    next row (a keycap that is higher than a row) are one picture."""
    runs = []
    for y in sorted({y for y, x in cells}):
        xs = sorted(x for yy, x in cells if yy == y)
        run = [xs[0]]
        for x in xs[1:]:
            if x == run[-1] + 1:
                run.append(x)
            else:
                runs.append((y, run[0], run[-1]))
                run = [x]
        runs.append((y, run[0], run[-1]))
    parent = list(range(len(runs)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for i, (y, a, b) in enumerate(runs):
        for j, (y2, a2, b2) in enumerate(runs):
            if y2 == y + 1 and a2 <= b and a <= b2:
                parent[find(j)] = find(i)
    groups = {}
    for i, r in enumerate(runs):
        groups.setdefault(find(i), []).append(r)
    return list(groups.values())


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


def start(doc, path_dirs=None):
    dfd_r, dfd_w = os.pipe()
    xvfb = subprocess.Popen(['Xvfb', '-noreset', '-displayfd', str(dfd_w),
        '-screen', '0', '1280x1400x24'], stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, pass_fds=(dfd_w,))
    procs.append(xvfb)
    os.close(dfd_w)
    num = b''
    while not num.endswith(b'\n'):
        chunk = os.read(dfd_r, 16)
        if not chunk:
            return None, None, None
        num += chunk
    os.close(dfd_r)
    disp = ':' + num.decode().strip()
    if os.path.exists(dump):
        os.remove(dump)
    env = dict(os.environ, DISPLAY=disp, STRATA_X11_DUMP=dump,
        STRATA_HOME=os.path.join(tmp, 'home' + disp[1:]),
        XDG_CONFIG_HOME=os.path.join(tmp, 'cfg' + disp[1:]),
        XDG_CACHE_HOME=os.path.join(tmp, 'cache' + disp[1:]),
        SHELL='/bin/true')
    if path_dirs is not None:
        env['STRATA_PROGRAM_DIRS'] = path_dirs
    p = subprocess.Popen([os.path.join(bindir, 'stred'), doc], env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    procs.append(p)
    w = []
    for _ in range(100):
        w = subprocess.run(['xdotool', 'search', '--name', '^stred$'],
            env=env, capture_output=True, text=True).stdout.split()
        if w:
            break
        time.sleep(0.1)

    return p, (w[0] if w else None), env


def shot(w, env, name):
    data = subprocess.run(['xwd', '-silent', '-id', w], env=env,
        capture_output=True).stdout
    if shotdir:
        os.makedirs(shotdir, exist_ok=True)
        shots.xwd_to_png(data, os.path.join(shotdir, name + '.png'))

    return data


def blank_pictures(pic, comps, cw, chh):
    """The pictures whose cells hold no pixel that differs from the color of
    the window."""
    bg = {}
    for y in range(0, pic.h, 7):
        for x in range(0, pic.w, 7):
            c = pic.px(x, y)
            bg[c] = bg.get(c, 0) + 1
    window = max(bg, key=bg.get)
    bad = []
    for g in comps:
        y0 = min(y for y, a, b in g) * chh
        y1 = (max(y for y, a, b in g) + 1) * chh
        x0 = min(a for y, a, b in g) * cw
        x1 = (max(b for y, a, b in g) + 1) * cw
        ink = 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                if pic.px(x, y) != window:
                    ink += 1
        if ink < 40:
            bad.append((x0, y0, x1, y1, ink))

    return bad


try:
    doc = os.path.join(tmp, 'table.doc')
    code, out, nkeys = make_document(doc)
    check('the generator makes the document of the table', code == 0 and
          os.path.exists(doc) and nkeys >= 20, out)
    p, w, env = start(doc)
    check('the window opens', w is not None)
    last_bad = None
    quiet = time.time()
    end = time.time() + 240
    comps = []
    bad = []
    pic = None
    cw = chh = 0
    while w is not None and time.time() < end:
        time.sleep(2.0)
        s = screen()
        m = re.search(r'\x01grid (\d+) (\d+) (\d+) (\d+)', s)
        if not s or m is None:
            continue
        cw, chh = int(m.group(3)), int(m.group(4))
        comps = pictures(image_cells(s))
        if len(comps) < nkeys:
            continue
        pic = Pic(shot(w, env, 'keytable'))
        bad = blank_pictures(pic, comps, cw, chh)
        if not bad:
            break
        if last_bad != len(bad):
            last_bad = len(bad)
            quiet = time.time()
        elif time.time() - quiet > 25:
            break
    check('every keycap of the table is a picture on the screen (%d of %d)' %
          (len(comps), nkeys), len(comps) >= nkeys, screen())
    check('every picture of the table has pixels: no keycap is left empty '
          '(%d empty of %d)' % (len(bad), len(comps)), not bad, bad)
    if p is not None:
        p.terminate()
        p.wait()

    # LaTeX cannot be run: the keycaps are drawn as labels, every one.
    empty = os.path.join(tmp, 'empty')
    os.makedirs(empty, exist_ok=True)
    p, w, env = start(doc, empty)
    time.sleep(8)
    s = screen()
    if image_cells(s):
        print('SKIP: the program ignores STRATA_PROGRAM_DIRS (a release '
              'build): the keycaps are pictures, not labels')
    else:
        rows = s.split('\x01')[0]
        shown = rows.count('[Key:')
        check('LaTeX missing: the keycaps are labels, none is left out (%d of '
              '%d)' % (shown, nkeys), shown >= nkeys, rows)
    if p is not None:
        p.terminate()
        p.wait()
finally:
    for p in procs:
        if p.poll() is None:
            p.terminate()
    shutil.rmtree(tmp, ignore_errors=True)

print('xkeytable: %d failures' % fails)
sys.exit(1 if fails else 0)
