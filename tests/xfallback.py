import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, socket, struct, subprocess, sys, tempfile, time, zlib
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
sys.path.insert(0, here)

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
# mutations of the code are run with XFALL_WAIT=0.25) are shorter.
WAITF = float(os.environ.get('XFALL_WAIT', '1'))
tmp = tempfile.mkdtemp(prefix='xfall')
shotdir = os.environ.get('XFALL_SHOTS')
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


def kc(legend):
    return b'\x1337(1' + legend.encode() + b'\x00' + CRLF + b'\x13)'


def pc(name):
    return b'\x1325(1' + name.encode() + b'\x00' + CRLF + b'\x13)'


def eqm(text):
    return (b'\x139(S' + CRLF + b'T' + text.encode() + b'\x00' + CRLF +
            b'\x13)')


def doc_of(blocks, extra=b''):
    """A document: each block is a list of parts (bytes)."""
    out = HEADER + extra + b'*B0' + CRLF
    for parts in blocks:
        out += b'S' + CRLF + b'T' + b''.join(parts) + EOL + b'\x00' + CRLF
    return out + b'*E' + CRLF


def png_file(path):
    w = h = 24
    raw = b''.join(b'\x00' + b'\xc8\x20\x20' * w for _ in range(h))

    def chunk(k, d):
        c = struct.pack('>I', len(d)) + k + d
        return c + struct.pack('>I', zlib.crc32(k + d) & 0xffffffff)
    open(path, 'wb').write(
        b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack(
            '>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
        chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def runs(run):
    """The pictures of the screen, as sets of cells: the runs of image cells
    of a row, and the runs of the rows below that stand under them (a keycap
    takes three rows)."""
    rows = run.rows()
    cells = {(y, x) for y, r in enumerate(rows) for x, c in enumerate(r)
             if c == '\ue000'}
    found = []
    for y in sorted({y for y, x in cells}):
        xs = sorted(x for yy, x in cells if yy == y)
        cur = [xs[0]]
        for x in xs[1:] + [None]:
            if x is not None and x == cur[-1] + 1:
                cur.append(x)
            else:
                found.append({(y, c) for c in cur})
                cur = [x]
    out = []
    for run_ in found:
        y = min(c[0] for c in run_)
        xs = {c[1] for c in run_}
        for pic in out:
            if max(c[0] for c in pic) == y - 1 and {c[1] for c in pic
                                                    if c[0] == y - 1} == xs:
                pic |= run_
                break
        else:
            out.append(set(run_))
    return out


def ink_white(run, cells, pic):
    """The pixels of a set of cells that are not the color of the window,
    and the white ones."""
    g = run.grid()
    cw, chh = g[2], g[3]
    ys = [y for y, x in cells]
    xs = [x for y, x in cells]
    bg = pic.px(2, min(ys) * chh + 1)
    ink = white = 0
    for y in range(min(ys) * chh, (max(ys) + 1) * chh):
        for x in range(min(xs) * cw, (max(xs) + 1) * cw):
            p = pic.px(x, y)
            if sum(abs(p[i] - bg[i]) for i in range(3)) > 60:
                ink += 1
            if p == (255, 255, 255):
                white += 1
    return ink, white


def text_ink(run, row, col0, col1, pic):
    """Pixels that are not the color of the window in the cells col0 to
    col1 of a row of text."""
    g = run.grid()
    cw, chh = g[2], g[3]
    bg = pic.px(2, row * chh + 1)
    n = 0
    for y in range(row * chh, (row + 1) * chh):
        for x in range(col0 * cw, col1 * cw):
            p = pic.px(x, y)
            if sum(abs(p[i] - bg[i]) for i in range(3)) > 60:
                n += 1
    return n


PASSAGE = ['Ctrl-S', 'Ctrl', 'S', 'Alt-F12', 'Alt', 'F12', 'Shift-Up',
           'Shift', 'Keypad +', 'Keypad -', 'Keypad *', 'Enter', 'Ins',
           'Insert', 'Ctrl-', 'Alt-', 'Ctrl-s', 'Ctrl-S', 'F12']
MANY = ['Ctrl-%s' % c for c in 'abcdef']


def fake_tools(name, latex_body):
    """A folder for STRATA_PROGRAM_DIRS: a fake pdflatex and latex with the
    body, and the real programs that the pictures need."""
    d = os.path.join(tmp, name)
    os.makedirs(d, exist_ok=True)
    fake_program(os.path.join(d, 'pdflatex'), latex_body)
    fake_program(os.path.join(d, 'latex'), latex_body)
    for prog in ('pdftoppm', 'dvipng'):
        os.symlink(_sh.which(prog), os.path.join(d, prog))
    return d


def marker_doc(extra=b''):
    return doc_of([[b'k ', kc('Ctrl-s'), b' p ', pc('pic.png'), b' e ',
                    eqm('x^2'), b' end']], extra)


def shot_pic(run, name=None):
    return Pic(run.shot(name))


def key_to_marker(run, n):
    for _ in range(n):
        run.key('Right')


try:
    # ---- (a) every keycap of a screen is a picture --------------------------
    text = []
    for i, leg in enumerate(PASSAGE):
        text += [b'k%02d ' % i, kc(leg), b' ']
    text2 = []
    for leg in MANY:
        text2 += [kc(leg), b' ']
    r = Run('pass', doc_of([text, text2]))
    want = len(PASSAGE) + len(MANY)
    r.wait(lambda run: len(runs(run)) >= want, 240)
    time.sleep(1.5)
    rs = runs(r)
    pic = shot_pic(r, 'passage')
    info = [ink_white(r, c, pic) for c in rs]
    check('a screen of %d keycaps: every one is a picture (%d image runs), '
          'not only the first 16' % (want, len(rs)), len(rs) >= want, len(rs))
    check('every keycap picture has ink and no white pixel',
          len(info) >= want and all(i[0] > 40 and i[1] == 0 for i in info),
          [i for i in info if i[0] <= 40 or i[1] != 0][:3])
    r.stop()

    r = Run('case', doc_of([[kc('Ctrl-s'), b' ', kc('Ctrl-S')]]))
    r.wait(lambda run: len(runs(run)) >= 2, 120)
    time.sleep(1.0)
    pngs = cache_pngs(os.path.join(r.dir, 'cache'))
    pics_ = [png_read(p) for p in pngs]
    check('keycaps whose legends differ only in case are two pictures: two '
          'files in the cache, with different pixels', len(runs(r)) >= 2 and
          len(pngs) == 2 and pics_[0][2] != pics_[1][2], (len(pngs)))
    r.stop()

    # ---- (b) a picture that fails is drawn by the settings ------------------
    png_file(os.path.join(tmp, 'pic.png'))
    failing = fake_tools('failtools', 'echo "! Fake error."\nexit 1\n')
    os.chdir(tmp)
    sock, sockpath = log_server('fail.sock')
    r = Run('fail', marker_doc(), dirs=[failing], logsock=sockpath, cwd=tmp)
    got = []
    ok = r.wait(lambda run: all(w in run.screen() for w in (
        '[Key: Ctrl-s]', '[PIC pic.png]', '[EQN x^2]')), 90)
    end = time.time() + 20
    while time.time() < end and len([l for l in got if 'cannot be drawn' in
                                     l]) < 3:
        got += log_lines(sock)
        time.sleep(0.3)
    time.sleep(2.0)
    got += log_lines(sock)
    s = r.screen()
    rows = r.rows()
    check('failed jobs: the Keycap, the Picture and the Equation are drawn by '
          'the settings (the Short form), with no image cell: no empty box',
          ok and '\ue000' not in s, rows[:4])
    pic = shot_pic(r, 'failed')
    y, _ = next(((i, 0) for i, x in enumerate(rows) if '[Key:' in x),
                (None, None))
    cols = [rows[y].index(w) for w in ('[Key: Ctrl-s]', '[PIC pic.png]',
                                       '[EQN x^2]')] if y else []
    lens = (13, 13, 14)
    inks = [text_ink(r, y, c, c + n, pic) for c, n in zip(cols, lens)] \
        if y else []
    check('failed jobs: the text of each marker has ink on the screen',
          len(inks) == 3 and all(i > 100 for i in inks), inks)
    mine = [l for l in got if 'cannot be drawn as a picture' in l]
    check('failed jobs: the log has one entry for each marker, with its '
          'place and the first line of the message',
          len(mine) == 3 and all('block 1' in l and 'line 1' in l for l in
                                 mine) and
          sum('The Keycap' in l for l in mine) == 1 and
          sum('The Picture' in l for l in mine) == 1 and
          sum('The Equation' in l for l in mine) == 1, got)
    key_to_marker(r, 2)
    check('failed jobs: the message line shows "Keycap drawn as Short" '
          'with the cause when the cursor is on the keycap',
          r.wait(lambda run: 'Keycap drawn as Short: LaTeX could not make '
                 'the keycap' in run.screen(), 20), r.screen())
    key_to_marker(r, 4)
    check('failed jobs: the message line shows it for the Picture',
          r.wait(lambda run: 'Picture drawn as Short:' in run.screen(),
                 20), r.screen())
    key_to_marker(r, 4)
    check('failed jobs: the message line shows it for the Equation',
          r.wait(lambda run: 'Equation drawn as Short: The equation did '
                 'not typeset' in run.screen(), 20), r.screen())
    r.stop()

    # ---- LaTeX is not installed ---------------------------------------------
    empty = os.path.join(tmp, 'nolatex')
    os.makedirs(empty, exist_ok=True)
    sock4, sp4 = log_server('nolatex.sock')
    r = Run('nolatex', marker_doc(), dirs=[empty], logsock=sp4, cwd=tmp)
    ok = r.wait(lambda run: all(w in run.screen() for w in (
        '[Key: Ctrl-s]', '[PIC pic.png]', '[EQN x^2]')), 60)
    time.sleep(2.0)
    got4 = [l for l in log_lines(sock4) if 'cannot be drawn' in l]
    check('LaTeX missing: the three markers are drawn in the Short form, no image '
          'cell, and the log has one entry for each', ok and
          '\ue000' not in r.screen() and len(got4) == 3 and
          all('LaTeX is not installed' in l for l in got4), (r.rows()[:3],
                                                         got4))
    key_to_marker(r, 2)
    check('LaTeX missing: the message line shows why when the cursor is on '
          'the keycap', r.wait(lambda run: 'Keycap drawn as Short: LaTeX '
                               'is not installed' in run.screen(), 20),
          r.screen())
    r.stop()

    # ---- the other display modes: the marker is drawn by its mode -----------
    for mode, name, expect in ((0, 'Diamond', '\u2666'), (1, 'Name', '[KEY]'),
                               (2, 'Short', '[Key: Ctrl-s]'),
                               (3, 'Long', '[Key: Ctrl-s]'), (4, 'Hidden', '')):
        extra = b''.join(b'OM%d,%d' % (t, mode) + CRLF for t in (9, 25, 37))
        sock2, sp2 = log_server('m%d.sock' % mode)
        r = Run('mode%d' % mode, marker_doc(extra), dirs=[failing],
                logsock=sp2, cwd=tmp)
        r.wait(lambda run: 'end' in run.screen(), 60)
        time.sleep(3.0)
        s = r.screen()
        rows = r.rows()
        line = next((x for x in rows if x.startswith('k ')), '')
        check('mode %s: the keycap is drawn by its mode (%r), no image cell, '
              'no empty box' % (name, expect or 'nothing'),
              '\ue000' not in s and expect in line and
              (expect != '' or line.startswith('k  p ')), line)
        lg = [l for l in log_lines(sock2) if 'cannot be drawn' in l]
        check('mode %s: no picture is asked for, so nothing is logged' % name,
              lg == [], lg)
        r.stop()

    # ---- a job that takes more than 5 seconds -------------------------------
    slow = fake_tools('slowtools', 'sleep 30\n')
    sock3, sp3 = log_server('slow.sock')
    t0 = time.time()
    r = Run('slow', doc_of([[b'k ', kc('Ctrl-s'), b' end']]), dirs=[slow],
            logsock=sp3, cwd=tmp)
    gone = r.wait(lambda run: '[Key: Ctrl-s]' in run.screen(), 40)
    took = time.time() - t0
    got3 = []
    end = time.time() + 10
    while time.time() < end and not any('more than 5 seconds' in l
                                        for l in got3):
        got3 += log_lines(sock3)
        time.sleep(0.3)
    check('a job over 5 seconds fails: the keycap is drawn as Short after '
          'the limit (%.1f s) and the log says why' % took,
          gone and 4.0 <= took <= 25 and any('more than 5 seconds' in l
                                             for l in got3), (took, got3))
    r.stop()
finally:
    for p in procs:
        if p.poll() is None:
            p.terminate()
    xvfb.terminate()
    shutil.rmtree(tmp, ignore_errors=True)

print('xfallback: %d failures' % fails)
sys.exit(1 if fails else 0)
