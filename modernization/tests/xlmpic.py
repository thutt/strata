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

"""LaTeX markers drawn as pictures in the X11 Editor (docs/LATEXMARKER-
PICTURES.txt).  Under Xvfb: a table of marker texts of both kinds and what
the Editor draws for each (a picture on the line, or the marker by its
display mode); a marker with the TeX logo is a picture whose pixels are the
picture of the cache and are not the pixels of the marker box, and the
picture matches a reference render of the same text; a marker with an error
is drawn by its mode, the Error Log (the log of the run) names its place and
the first line of the message of LaTeX, and the message line shows why when
the cursor is on it; with LaTeX missing (STRATA_PROGRAM_DIRS with a folder
that holds no program) every marker is drawn by its mode; the cache is used
on the second draw (the compiles are counted by a stand-in for pdflatex that
runs the real one); a slow stand-in that sleeps 20 seconds never holds a key
for more than 0.3 seconds and is stopped at 5 seconds; a mode of the marker
type other than Inline (Short, Name, Long, Diamond, Hidden) draws every marker
by that mode and starts no job; and the terminal Editor is unchanged.  With
--table the table of the document is printed.  With LMPIC_SHOTS set to a
folder the pictures of the screen are kept there as PNG files.

    STRATA_BUILD_DIR=... python3 -I -B tests/xlmpic.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, socket, struct, subprocess, sys, tempfile, time, zlib
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
sys.path.insert(0, here)

# The table: the text of the marker and what is drawn: P picture, S settings.
TABLE = [
    # LaTeX gives ink: a picture.
    ('P', '\\TeX{}'),
    ('P', '\\LaTeX{}'),
    ('P', '\\strata'),
    ('P', '$\\alpha+\\beta$'),
    ('P', '{\\bfseries Bold}'),
    ('P', '\\emph{emphasised}'),
    ('P', '$\\frac{a}{b}$'),
    ('P', '\\fbox{box}'),
    ('P', '\\rule{1cm}{2pt}'),
    ('P', '\\textsc{Caps}'),
    ('P', '\\textcolor{red}{red}'),
    ('P', '\\bfseries Hello'),
    ('P', '\\parbox{2cm}{a b c d e f g h i j k l m n o p q r s t u v w x}'),
    ('P', '\\newcommand{\\zz}{y}\\TeX{}'),
    ('P', '\\setcounter{page}{5}\\TeX{}'),
    ('P', '$$x$$'),
    # Vertical space with ink around it: a picture (the title page logo).
    ('P', '\\vspace{3ex}\\strata\\vspace{3ex}'),
    ('P', '\\strata\\bigskip'),
    ('P', '\\stratalogo[0.5]'),
    ('P', '\\vspace{3ex}\\stratalogo[0.5]\\vspace{3ex}'),
    # LaTeX gives an error: the marker settings.
    ('S', '\\hrule'),
    ('S', '\\section{Intro}'),
    ('S', '\\begin{verbatim}'),
    ('S', '\\end{verbatim}'),
    ('S', '{'),
    ('S', '}'),
    ('S', '{\\bfseries unclosed'),
    ('S', '\\usepackage[utf8]{inputenc}'),
    ('S', '\\begin{figure}'),
    ('S', '\\begin{itemize}'),
    ('S', '\\begin{tabular}{ll}'),
    ('S', '\\ifx a b'),
    ('S', '\\undefinedmacroxyz'),
    ('S', '\\input{/etc/hostname}'),
    ('S', '\\immediate\\write18{echo hi}'),
    # LaTeX gives no ink: the marker settings.
    ('S', '\\newpage'),
    ('S', '\\vspace{2ex}'),
    ('S', '\\newcommand{\\x}{y}'),
    ('S', '\\setcounter{page}{5}'),
    ('S', '\\bfseries'),
    ('S', '\\rule{0pt}{0pt}'),
    ('S', '\\hspace{1cm}'),
    ('S', '\\hphantom{ink}'),
    ('S', '\\par'),
    ('S', '\\vfill'),
]

if '--table' in sys.argv:
    for kind, text in TABLE:
        print('  %s  %s' % (kind, text))
    sys.exit(0)

import shutil as _sh
bindir = os.environ.get('XBIN', os.path.join(os.environ.get(
    'STRATA_BUILD_DIR', ''), 'x11'))
termdir = os.environ.get('TBIN', os.path.join(os.environ.get(
    'STRATA_BUILD_DIR', ''), 'term'))
for tool in ('Xvfb', 'xdotool', 'xwd', 'pdflatex', 'pdftoppm', 'pdfinfo'):
    if not _sh.which(tool):
        print('SKIP: %s not installed' % tool)
        sys.exit(0)
if not os.path.exists(os.path.join(bindir, 'stred')):
    print('SKIP: run `make x11` first')
    sys.exit(0)

sys.path.insert(0, os.path.join(root, 'manual'))
import shots
import tui

REAL = _sh.which('pdflatex')
# The waits of a run in which pictures are not expected to come (the
# mutations of the code are run with LMPIC_WAIT=0.25) are shorter.
WAITF = float(os.environ.get('LMPIC_WAIT', '1'))
tmp = tempfile.mkdtemp(prefix='lmpic')
shotdir = os.environ.get('LMPIC_SHOTS')
fails = 0
procs = []
CRLF = b'\r\n'
EOL = b'\x1d'
HEADER = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
          b'P1C33' + CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF +
          b'DP0,8500,11000' + CRLF)


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


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


def marker(text):
    return (b'\x1341(S' + CRLF + b'T' + text.encode() + b'\x00' + CRLF +
            b'\x13)')


def document(lines, extra=b'', first=0):
    """One block; each line is a tag, a space, the marker and 'end'."""
    body = b''
    for i, text in enumerate(lines):
        body += b'%02d ' % (first + i) + marker(text) + b' end' + EOL
    return (HEADER + extra + b'*B0' + CRLF + b'S' + CRLF + b'T' + body +
            b'\x00' + CRLF + b'*E' + CRLF)


def fake_program(path, body):
    with open(path, 'w') as f:
        f.write('#!/bin/sh\n' + body)
    os.chmod(path, 0o755)


class Run:
    """The Editor in a window of the display, on a document."""

    def __init__(self, name, doc, size=12, dirs=None, logsock=None,
                 cache=None, cwd=None, config=None):
        self.dir = os.path.join(tmp, name)
        os.makedirs(self.dir, exist_ok=True)
        if config is not None:
            home = os.path.join(self.dir, 'home')
            os.makedirs(home, mode=0o700, exist_ok=True)
            with open(os.path.join(home, 'config'), 'w') as f:
                f.write(config)
            os.chmod(os.path.join(home, 'config'), 0o600)
        self.dump = os.path.join(self.dir, 'dump')
        self.path = os.path.join(self.dir, 't.doc')
        open(self.path, 'wb').write(doc)
        self.env = dict(os.environ, DISPLAY=disp, STRATA_X11_DUMP=self.dump,
            STRATA_X11_SIZE=str(size), STRATA_HOME=os.path.join(self.dir,
            'home'), XDG_CONFIG_HOME=os.path.join(self.dir, 'cfg'),
            XDG_CACHE_HOME=cache or os.path.join(self.dir, 'cache'),
            SHELL='/bin/true')
        if dirs:
            self.env['STRATA_PROGRAM_DIRS'] = ':'.join(dirs)
        if logsock:
            self.env['STRATA_LOG_SOCKET'] = logsock
        self.proc = subprocess.Popen([os.path.join(bindir, 'stred'),
            self.path], env=self.env, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, cwd=cwd)
        procs.append(self.proc)
        self.win = None
        for _ in range(100):
            w = subprocess.run(['xdotool', 'search', '--name', '^stred$'],
                env=self.env, capture_output=True, text=True).stdout.split()
            if w:
                self.win = w[-1]
                break
            time.sleep(0.1)

    def screen(self):
        try:
            return open(self.dump, encoding='utf-8').read()
        except OSError:
            return ''

    def rows(self):
        return self.screen().split('\x01')[0].split('\n')

    def draws(self):
        m = re.search(r'draws (\d+)', self.screen())
        return int(m.group(1)) if m else 0

    def grid(self):
        m = re.search(r'grid (\d+) (\d+) (\d+) (\d+)', self.screen())
        return tuple(int(v) for v in m.groups()) if m else None

    def key(self, name):
        subprocess.run(['xdotool', 'key', '--window', self.win, name],
            env=self.env, capture_output=True)

    def wait(self, pred, secs=120):
        end = time.time() + secs * WAITF
        while time.time() < end:
            if pred(self):
                return True
            time.sleep(0.2)
        return False

    def shot(self, name=None):
        data = subprocess.run(['xwd', '-silent', '-id', self.win],
            env=self.env, capture_output=True).stdout
        if shotdir and name:
            os.makedirs(shotdir, exist_ok=True)
            shots.xwd_to_png(data, os.path.join(shotdir, name + '.png'))
        return data

    def stop(self, others=False):
        if self.proc.poll() is None:
            self.proc.terminate()
            self.proc.wait()

        # The window goes with the program; a window that stays would
        # cover the next one.  (Not when another Editor is open.)
        if others:
            return

        end = time.time() + 5
        while True:
            left = subprocess.run(['xdotool', 'search', '--name', '^stred$'],
                env=self.env, capture_output=True, text=True).stdout.split()
            if not left or time.time() > end:
                break
            time.sleep(0.1)
        check('the window of %s goes away with the program' %
              os.path.basename(self.dir), not left, left)


def picture_rows(run):
    """For each tag of the text: does its line hold image cells?  A tall
    picture is a block on rows of its own after the row of the tag, so the
    rows that follow a tag row belong to it until the next tag row."""
    out = {}
    cur = None
    for r in run.rows():
        m = re.match(r'(\d\d)(?: |$)', r)
        if m:
            cur = int(m.group(1))
            out[cur] = '\ue000' in r
        elif cur is not None and '\ue000' in r:
            out[cur] = True
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
        # The pixel is stored as blue, green, red and a byte that is not
        # used.
        return (self.data[i + 2], self.data[i + 1], self.data[i])


def png_read(path):
    """A PNG file of 8 bits per color: width, height, a list of rows of RGB
    tuples."""
    d = open(path, 'rb').read()
    pos = 8
    idat = b''
    while pos < len(d):
        n, kind = struct.unpack('>I4s', d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    assert depth == 8 and ctype in (0, 2, 6), (depth, ctype)
    bpp = {0: 1, 2: 3, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows = []
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        rows.append([tuple(line[x * bpp:x * bpp + 3]) if bpp > 1 else
                     (line[x],) * 3 for x in range(w)])
        prev = line
    return w, h, rows


def ink_box(rows, bg, tol=60):
    """The box of the pixels that differ from the background color."""
    xs, ys = [], []
    for y, row in enumerate(rows):
        for x, p in enumerate(row):
            if sum(abs(p[i] - bg[i]) for i in range(3)) > tol:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return min(xs), min(ys), max(xs), max(ys)


def cache_pngs(cache):
    d = os.path.join(cache, 'strata', 'eqn')
    return sorted(os.path.join(d, f) for f in os.listdir(d)
                  if f.endswith('.png')) if os.path.isdir(d) else []


def log_server(name):
    """A datagram socket like the one the Document Manager makes.  The
    name is relative to the folder of the test, because the path of the
    folder is too long for a socket: the Editor is started in that folder."""
    os.chdir(tmp)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    s.bind(name)
    s.setblocking(False)
    return s, name


def log_lines(s):
    out = []
    while True:
        try:
            out.append(s.recv(4096).decode('utf-8', 'replace'))
        except BlockingIOError:
            return out


try:
    # ---- The table ---------------------------------------------------------
    # The table is shown in parts, so that every part is on the screen.
    got = {}
    for first in range(0, len(TABLE), 10):
        part = TABLE[first:first + 10]
        npic = sum(1 for k, t in part if k == 'P')
        r = Run('table%d' % first, document([t for k, t in part],
                first=first))

        def settled(run):
            return sum(1 for v in picture_rows(run).values() if v) >= npic

        r.wait(settled, 240)
        # The jobs run one after another: wait until no picture has come
        # for a while, so that a marker that is wrongly made a picture shows.
        same = 0
        last = -1
        end = time.time() + 120 * WAITF
        while same < 8 and time.time() < end:
            n = sum(1 for v in picture_rows(r).values() if v)
            same = same + 1 if n == last else 0
            last = n
            time.sleep(1.0)
        rows_now = picture_rows(r)
        got.update(rows_now)
        check('the table: every marker of the part is on the screen',
              all(first + i in rows_now for i in range(len(part))),
              r.screen())
        r.shot('table%d' % first)
        r.stop()
    for i, (kind, text) in enumerate(TABLE):
        check('table: %s is drawn as %s' % (text, 'a picture' if kind == 'P'
              else 'the marker settings'), got.get(i) == (kind == 'P'), i)

    # ---- The picture: pixels, the cache and a reference render --------------
    cache = os.path.join(tmp, 'cache-pix')
    r = Run('pix', document(['\\TeX{}']), cache=cache)
    r.wait(lambda run: '\ue000' in run.screen(), 120)
    time.sleep(1.0)
    cw, chh = r.grid()[2], r.grid()[3]
    rows = r.rows()
    yy = next((y for y, row in enumerate(rows) if '\ue000' in row), None)
    xs = ([x for x, c in enumerate(rows[yy]) if c == '\ue000']
          if yy is not None else [])
    pngs = cache_pngs(cache)
    check('the TeX logo is a picture in its line', yy is not None and
          len(xs) > 0, r.screen())
    check('the picture is made once and kept in the cache', len(pngs) == 1,
          pngs)
    if yy is not None and len(pngs) == 1:
        pw, ph, prow = png_read(pngs[0])
        bg = prow[0][0]
        pic = Pic(r.shot('tex-logo'))
        ink = [(x, y, prow[y][x]) for y in range(ph) for x in range(pw)
               if sum(abs(prow[y][x][i] - bg[i]) for i in range(3)) > 120]
        sample = ink[::max(1, len(ink) // 150)]
        best = (0, None)
        for oy in range(yy * chh - ph - chh, yy * chh + chh):
            for ox in range(xs[0] * cw - 3, xs[0] * cw + cw):
                n = 0
                for x, y, p in sample:
                    if 0 <= ox + x < pic.w and 0 <= oy + y < pic.h and \
                            pic.px(ox + x, oy + y) == p:
                        n += 1
                if n > best[0]:
                    best = (n, (ox, oy))
        check('the pixels of the window are the pixels of the picture of the '
              'cache (%d of %d ink pixels)' % (best[0], len(sample)),
              len(sample) > 20 and best[0] >= len(sample) - 1, best)
        # The marker box of the same marker, drawn with the mode Short.
        r0 = Run('pix0', document(['\\TeX{}'],
            b'OM41,2' + CRLF), cache=os.path.join(tmp, 'cache-pix0'))
        r0.wait(lambda run: '[LaTeX instructions]' in run.screen(), 60)
        time.sleep(1.0)
        pic0 = Pic(r0.shot('tex-label'))
        ox, oy = best[1]
        same = sum(1 for x, y, p in sample if pic0.px(ox + x, oy + y) == p)
        check('the pixels of the picture differ from the pixels of the '
              'marker box (%d of %d the same)' % (same, len(sample)),
              same < len(sample) // 2, same)
        r0.stop(others=True)
        # A reference render: the same text with the same fonts and size.
        eff = (chh * 7) * 10 // 11
        ref = os.path.join(tmp, 'ref')
        os.makedirs(ref, exist_ok=True)
        open(os.path.join(ref, 'r.tex'), 'w').write(
            '\\documentclass[11pt]{extarticle}\\usepackage[T1]{fontenc}'
            '\\usepackage{lmodern}\\pagestyle{empty}\\begin{document}'
            '\\TeX{}\\end{document}\n')
        subprocess.run([REAL, '-no-shell-escape', '-interaction=nonstopmode',
            'r.tex'], cwd=ref, capture_output=True)
        subprocess.run(['pdftoppm', '-png', '-r', str(eff), '-singlefile',
            '-x', '0', '-y', '0', '-W', '400', '-H', '200', 'r.pdf', 'r'],
            cwd=ref, capture_output=True)
        rw, rh, rrows = png_read(os.path.join(ref, 'r.png'))
        rb = ink_box(rrows, (255, 255, 255))
        pb = ink_box(prow, bg)
        check('the picture has the size of a reference render of the same '
              'text (ink %s, reference %s)' % (
              (pb[2] - pb[0] + 1, pb[3] - pb[1] + 1),
              (rb[2] - rb[0] + 1, rb[3] - rb[1] + 1)),
              abs((pb[2] - pb[0]) - (rb[2] - rb[0])) <= 2 and
              abs((pb[3] - pb[1]) - (rb[3] - rb[1])) <= 2)
    r.stop()

    # ---- An error: drawn by settings, in the log, in the message line -------
    sock, sockpath = log_server('log1.sock')
    r = Run('err', document(['\\undefinedmacroxyz']), logsock=sockpath,
            cwd=tmp)
    r.wait(lambda run: '[LaTeX instructions]' in run.screen(), 60)
    got_log = []
    end = time.time() + 60
    while time.time() < end and not any('Undefined' in l for l in got_log):
        got_log += log_lines(sock)
        time.sleep(0.3)
    msg = [l for l in got_log if 'Undefined control sequence' in l]
    check('an error: the marker is drawn by its mode, no picture',
          '\ue000' not in r.screen() and
          '[LaTeX instructions]' in r.screen(), r.screen())
    check('an error: the log names the marker\'s place and the first line of '
          'the message of LaTeX (a warning)', len(msg) == 1 and
          msg[0].startswith('<4>') and 'block 1' in msg[0] and
          'line 1' in msg[0] and 'Undefined control sequence' in msg[0],
          got_log)
    for _ in range(3):
        r.key('Right')
    ok = r.wait(lambda run: 'LaTeX reported: Undefined control sequence' in
                run.screen(), 20)
    check('an error: the message line shows why when the cursor is on the '
          'marker', ok, r.screen())
    r.stop()

    # ---- LaTeX missing: every marker is drawn by settings -------------------
    empty = os.path.join(tmp, 'empty')
    os.makedirs(empty, exist_ok=True)
    r = Run('nolatex', document(['\\TeX{}', '{\\bfseries Bold}', '\\newpage']),
            dirs=[empty])
    r.wait(lambda run: '[LaTeX instructions]' in run.screen(), 60)
    time.sleep(3.0)
    s = r.screen()
    check('LaTeX missing: all three markers are drawn by settings',
          s.count('[LaTeX instructions]') == 3 and '\ue000' not in s, s)
    for _ in range(3):
        r.key('Right')
    check('LaTeX missing: the message line shows that LaTeX is not '
          'installed', r.wait(lambda run: 'drawn as Short: LaTeX is not '
          'installed' in run.screen(), 20), r.screen())
    r.stop()

    # ---- The cache and the compiles -----------------------------------------
    fdir = os.path.join(tmp, 'fakecount')
    os.makedirs(fdir, exist_ok=True)
    count = os.path.join(tmp, 'count.txt')
    open(count, 'w').close()
    fake_program(os.path.join(fdir, 'pdflatex'),
                 'echo x >> %s\nexec %s "$@"\n' % (count, REAL))
    dirs = [fdir, '/usr/bin', '/bin']

    def compiles():
        return len(open(count).read().split())

    cache = os.path.join(tmp, 'cache-count')
    doc = document(['\\TeX{}', '\\LaTeX{}', '\\TeX{}'])
    r = Run('count', doc, dirs=dirs, cache=cache)
    r.wait(lambda run: sum(1 for v in picture_rows(run).values() if v) == 3,
           120)
    time.sleep(2.0)
    check('the cache: three markers with two different texts are two '
          'compiles', compiles() == 2, compiles())
    n0 = compiles()
    for k in ('Next', 'Prior', 'Down', 'Up'):
        r.key(k)
        time.sleep(0.4)
    time.sleep(2.0)
    check('the cache: drawing the window again makes no compile',
          compiles() == n0 and sum(1 for v in picture_rows(r).values()
          if v) == 3, (compiles(), n0))
    r.stop()
    r = Run('count2', doc, dirs=dirs, cache=cache)
    r.wait(lambda run: sum(1 for v in picture_rows(run).values() if v) == 3,
           60)
    time.sleep(1.0)
    check('the cache: a document opened again shows its pictures with no '
          'compile', compiles() == n0 and
          sum(1 for v in picture_rows(r).values() if v) == 3,
          (compiles(), n0))
    r.stop()
    r = Run('count3', document(['\\TeX{} x', '\\TeX{}\\LaTeX{}']),
            dirs=dirs, cache=cache)
    r.wait(lambda run: sum(1 for v in picture_rows(run).values() if v) == 2,
           60)
    time.sleep(1.0)
    check('the cache: another text is a new compile', compiles() > n0,
          (compiles(), n0))
    r.stop()

    # ---- A slow stand-in: keys do not wait, the job stops at 5 seconds ------
    sdir = os.path.join(tmp, 'fakeslow')
    os.makedirs(sdir, exist_ok=True)
    slog = os.path.join(tmp, 'slow.txt')
    open(slog, 'w').close()
    fake_program(os.path.join(sdir, 'pdflatex'),
                 'echo "$$ $(date +%%s.%%N)" >> %s\nsleep 20\n' % slog)
    r = Run('slow', document(['\\TeX{}'] * 9),
            dirs=[sdir, '/usr/bin', '/bin'])
    r.wait(lambda run: '[LaTeX instructions]' in run.screen(), 60)
    r.wait(lambda run: open(slog).read().strip() != '', 30)
    lat = []
    for k in ('Down', 'Down', 'Right', 'Left', 'Up', 'Down', 'Right', 'Left'):
        before = r.draws()
        t0 = time.time()
        r.key(k)
        while r.draws() == before and time.time() - t0 < 5:
            time.sleep(0.005)
        lat.append(time.time() - t0)
        time.sleep(0.1)
    check('a slow latex: no key waited more than 0.3 seconds (longest '
          '%.3f)' % max(lat), max(lat) < 0.3, lat)
    started = open(slog).read().split()
    pid = int(started[0])
    t_start = float(started[1])

    def dead():
        try:
            st = open('/proc/%d/stat' % pid).read().split(')')[-1].split()[0]
        except OSError:
            return True
        return st == 'Z'

    while not dead() and time.time() - t_start < 12:
        time.sleep(0.05)
    took = time.time() - t_start
    check('a slow latex: the job is stopped at the limit of 5 seconds '
          '(%.2f)' % took, 4.5 <= took <= 6.5, took)
    check('a slow latex: the marker stays drawn by its mode',
          '\ue000' not in r.screen() and '[LaTeX instructions]' in
          r.screen(), r.screen())
    r.stop()

    # ---- An Editor that ends while a job runs leaves no window --------------
    open(slog, 'w').close()
    r = Run('slow2', document(['\\TeX{}']), dirs=[sdir, '/usr/bin', '/bin'])
    r.wait(lambda run: open(slog).read().strip() != '', 60)
    r.stop()

    # ---- The baseline: a letter lowered by half an em -----------------------
    r = Run('base', document(['\\raisebox{-0.5em}{x}']))
    r.wait(lambda run: '\ue000' in run.screen(), 120)
    time.sleep(1.0)
    cw, chh = r.grid()[2], r.grid()[3]
    rows = r.rows()
    yy = next((y for y, row in enumerate(rows) if '\ue000' in row), None)
    xs = ([x for x, c in enumerate(rows[yy]) if c == '\ue000']
          if yy is not None else [])
    check('a lowered letter is a picture in its line', yy is not None,
          r.screen())
    if yy is not None:
        pic = Pic(r.shot('baseline'))
        bgc = pic.px(cw * 60, chh * 20)

        def lowest(x0, x1):
            low = None
            for y in range(yy * chh - chh, yy * chh + 2 * chh):
                for x in range(x0, x1):
                    p = pic.px(x, y)
                    if sum(abs(p[i] - bgc[i]) for i in range(3)) > 150:
                        low = y
                        break
            return low

        # The second digit: the first has the cursor on it.
        digits = lowest(cw, 2 * cw)
        letter = lowest(xs[0] * cw, (xs[-1] + 1) * cw)
        em = 11 * ((chh * 7) * 10 // 11) / 72.27 / 2
        check('the picture stands on the baseline: the lowered letter is '
              'half an em (%.1f pixels) under the digits of the line (%s '
              'pixels)' % (em, None if digits is None or letter is None
              else letter - digits), digits is not None and letter is not None
              and em - 3 <= letter - digits <= em + 3,
              (digits, letter, yy, chh, cw, xs, bgc, pic.w, pic.h))
    r.stop()

    # ---- The Printer setting that forbids LaTeX markers ---------------------
    r = Run('forbid', document(['\\TeX{}']), config='latex_markers=no\n')
    r.wait(lambda run: '[LaTeX instructions]' in run.screen(), 60)
    time.sleep(3.0)
    for _ in range(3):
        r.key('Right')
    check('latex_markers=no in the settings: the marker is drawn by '
          'settings and the message line shows so', '\ue000' not in
          r.screen() and r.wait(lambda run: 'LaTeX markers are not allowed '
          'in this setup' in run.screen(), 20), r.screen())
    r.stop()

    # ---- The mode Short -----------------------------------------------------
    open(count, 'w').close()
    r = Run('off', document(['\\TeX{}', '{\\bfseries Bold}'],
            b'OM41,2' + CRLF), dirs=dirs)
    r.wait(lambda run: '[LaTeX instructions]' in run.screen(), 60)
    time.sleep(3.0)
    s = r.screen()
    check('the mode Short: both markers are drawn Short and nothing '
          'is compiled', s.count('[LaTeX instructions]') == 2 and
          '\ue000' not in s and compiles() == 0, (s, compiles()))
    r.stop()

    # ---- The other modes: no job, drawn by the mode -------------------------
    for mode, name, label in ((0, 'Diamond', '\u2666'), (1, 'Name', '[LaTeX]'),
                              (3, 'Long', '[LaTeX instructions]'),
                              (4, 'Hidden', None)):
        open(count, 'w').close()
        r = Run('mode%d' % mode, document(['\\TeX{}'],
                b'OM41,%d' % mode + CRLF), dirs=dirs)
        r.wait(lambda run: 'end' in run.screen(), 60)
        time.sleep(3.0)
        s = r.screen()
        check('the mode %s: the marker is drawn by the mode and nothing is '
              'compiled' % name, '\ue000' not in s and compiles() == 0 and
              (label is None or label in s), (s, compiles()))
        r.stop()

    # ---- A marker that LaTeX cannot typeset in a box: the Short form ---------
    open(count, 'w').close()
    sock5, sp5 = log_server('struct.sock')
    r = Run('struct', document(['\\newpage']), dirs=dirs, logsock=sp5,
            cwd=tmp)
    r.wait(lambda run: '[LaTeX instructions]' in run.screen(), 60)
    time.sleep(3.0)
    lg = [l for l in log_lines(sock5) if '<4>' in l]
    check('a page break in the mode Inline is given to LaTeX, which reports '
          'an error: the Short form and one entry in the Error Log with the '
          'first line of the error', '\ue000' not in r.screen() and
          compiles() >= 1 and len(lg) == 1 and 'prevdepth' in lg[0],
          (compiles(), lg))
    for _ in range(3):
        r.key('Right')
    check('the message line shows what LaTeX said', r.wait(
          lambda run: 'LaTeX marker drawn as Short: LaTeX reported: ' in
          run.screen(), 20), r.screen())
    r.stop()

    # ---- A tall picture is a block on rows of its own -----------------------
    def only_picture_rows(run):
        """The longest run of rows that hold image cells and no tag: the
        rows of a block picture (an inline picture that is a little taller
        than its line has one or two such rows above its line)."""
        best, cur = [], []
        for y, row in enumerate(run.rows()):
            if '\ue000' in row and re.match(r'\d\d(?: |$)', row) is None:
                cur.append(y)
                if len(cur) > len(best):
                    best = list(cur)
            else:
                cur = []
        return best

    r = Run('tall', document(['\\TeX{}', '\\stratalogo[1]', '\\TeX{}']))
    ok = r.wait(lambda run: len(only_picture_rows(run)) >= 4, 240)
    time.sleep(1.0)
    prow = only_picture_rows(r)
    rows = r.rows()
    tags = {int(m.group(1)): y for y, row in enumerate(rows)
            for m in [re.match(r'(\d\d)(?: |$)', row)] if m}
    check('a LaTeX picture that is taller than two lines is shown on rows of '
          'its own, between the line before it and the line after it',
          ok and len(prow) >= 4 and tags.get(0) is not None and
          tags.get(1) is not None and tags.get(2) is not None and
          tags[1] < prow[0] and prow[-1] < tags[2], (prow, tags,
          r.screen()))
    check('the rows of a tall picture do not run over the lines around it: '
          'the picture rows follow one another, the two small pictures are '
          'on the rows of their own tags, and the text after the tall one is '
          'on the row after its picture', len(prow) >= 4 and
          prow == list(range(prow[0], prow[-1] + 1)) and
          '' in rows[tags[0]] and '' in rows[tags[2]] and
          prow[-1] + 1 <= tags[2] - 1 and 'end' in rows[prow[-1] + 1],
          (prow, tags, r.screen()))
    if len(prow) >= 4:
        cw, chh = r.grid()[2], r.grid()[3]
        pic = Pic(r.shot('tall-logo'))
        y0 = prow[0] * chh
        y1 = (prow[-1] + 1) * chh
        bgp = pic.px(1, y0 + 1)
        inkn = sum(1 for y in range(y0, y1, 2) for x in range(0, pic.w, 2)
                   if sum(abs(pic.px(x, y)[i] - bgp[i]) for i in range(3)) >
                   120)
        check('the tall picture has ink on the rows that it takes (%d '
              'sampled pixels)' % inkn, inkn > 500, inkn)
    else:
        check('the tall picture has ink on the rows that it takes', False)

    def cursor_row(run):
        m = re.search(r'\x01cursor (\d+) (\d+)', run.screen())
        return int(m.group(2)) if m else -1

    presses = 0
    while presses < 12:
        cr = cursor_row(r)
        if cr >= 0 and cr < len(rows) and re.match(r'02(?: |$)', rows[cr]) is not None:
            break
        r.key('Down')
        presses += 1
        time.sleep(0.4)
    check('the cursor passes the tall picture in a few keys, not one key '
          'for each of its %d rows' % len(prow), presses <= 5 and
          len(prow) >= 5, presses)
    r.stop()

    # ---- The title page of the story sample --------------------------------
    sdoc = os.path.join(root, 'samples', 'story', 'strata-story.doc')

    if os.path.exists(sdoc):
        r = Run('story', open(sdoc, 'rb').read())
        ok = r.wait(lambda run: len(only_picture_rows(run)) >= 8, 240)
        time.sleep(1.0)
        prow = only_picture_rows(r)
        check('the logo of the title page of the story sample is a picture '
              'of several rows (%d rows)' % len(prow), ok and len(prow) >= 8,
              r.screen())
        r.shot('story-title')
        r.stop()

    # ---- A marker that makes nothing visible: the reason is told ------------
    for text in ('\\vspace{2ex}', '\\hphantom{ink}'):
        r = Run('nothing', document([text]), cwd=tmp)
        r.wait(lambda run: '[LaTeX instructions]' in run.screen(), 60)
        time.sleep(3.0)
        for _ in range(3):
            r.key('Right')
        check('%s: no picture, and the message line shows that it makes '
              'nothing visible' % text,
              r.wait(lambda run: 'LaTeX marker drawn as Short: it makes '
                     'nothing visible' in run.screen(), 20) and
              '\ue000' not in r.screen(), r.screen())
        r.stop()

    # ---- The terminal Editor is unchanged -----------------------------------
    tdoc = os.path.join(tmp, 'term.doc')
    open(tdoc, 'wb').write(document(['\\TeX{}', '{\\bfseries Bold}']))
    open(count, 'w').close()
    saved = {k: os.environ.get(k) for k in ('STRATA_HOME',
             'STRATA_PROGRAM_DIRS')}
    os.environ['STRATA_HOME'] = os.path.join(tmp, 'thome')
    os.environ['STRATA_PROGRAM_DIRS'] = ':'.join(dirs)
    try:
        helpf = os.environ.get('STRATA_HELP', os.path.join(os.environ.get(
            'STRATA_BUILD_DIR', ''), 'strata.hlp'))
        d = tui.session([os.path.join(termdir, 'stred'), '-H', helpf, tdoc],
                        ['wait'] * 6 + ['show'])
    finally:
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
    check('the terminal Editor draws both markers as before and compiles '
          'nothing', d[0].count('[LaTeX instructions]') == 2 and
          compiles() == 0, (d[0], compiles()))
finally:
    for p in procs:
        if p.poll() is None:
            p.terminate()
    xvfb.terminate()
    shutil.rmtree(tmp, ignore_errors=True)

print('xlmpic: %d failures' % fails)
sys.exit(1 if fails else 0)
