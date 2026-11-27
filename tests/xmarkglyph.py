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

"""The end of line mark and the tab mark in the X11 version, with a font that
has no glyph for them.

    python3 -I -B tests/xmarkglyph.py

The Editor draws the end of a line with the triangle U+25C4 and a tab with
the triangle U+25BA.  A font without those glyphs draws a box for each one.
The test runs the Editor under Xvfb with a fontconfig that knows one font
only, a font that has neither triangle, takes a picture of the window and
compares the cell of each mark with the cell of the ASCII look-alike typed in
the same document ('<' and '>').  The cells must have the same ink, apart from
the bold of the marks: no box and no empty cell.  A
second run with the usual fonts checks that a font that has the glyphs still
draws the triangles, so that they differ from the look-alikes.  The test is
skipped when there is no font without the glyphs or no X tool."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, sys, time, shutil, struct, subprocess, tempfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bindir = os.environ.get('XBIN', os.path.join(os.environ.get('STRATA_BUILD_DIR',
    ''), 'x11'))
for tool in ('Xvfb', 'xdotool', 'xwd', 'fc-list'):
    if not shutil.which(tool):
        print('SKIP: %s not installed' % tool)
        sys.exit(0)
if not os.path.exists(os.path.join(bindir, 'stred')):
    print('SKIP: run `make x11` first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='xmg')
procs = []
CRLF = b'\r\n'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)[-400:]))
    fails += 0 if cond else 1


def files_with(spec):
    out = subprocess.run(['fc-list', spec, '-f', '%{file}\n'],
        capture_output=True, text=True).stdout.split('\n')
    return set(f for f in out if f != '')


def font_without_marks():
    """The file and the family of an installed outline font that has the
    letters x and the signs < > and neither triangle, or None."""
    both = files_with(':charset=25c4') | files_with(':charset=25ba')
    plain = files_with(':charset=3c') & files_with(':charset=3e') & \
        files_with(':charset=78')
    for f in sorted(plain - both):
        if not f.endswith(('.otf', '.ttf')):
            continue
        fam = subprocess.run(['fc-list', ':file=' + f, '-f', '%{family}\n'],
            capture_output=True, text=True).stdout.split('\n')[0]
        fam = fam.split(',')[0]
        if fam != '':
            return f, fam
    return None


found = font_without_marks()
if found is None:
    print('SKIP: no installed font lacks the triangles')
    sys.exit(0)
fontfile, family = found

fdir = os.path.join(tmp, 'fonts')
os.makedirs(fdir)
os.symlink(fontfile, os.path.join(fdir, os.path.basename(fontfile)))
conf = os.path.join(tmp, 'fonts.conf')
with open(conf, 'w') as f:
    f.write('<?xml version="1.0"?>\n'
            '<!DOCTYPE fontconfig SYSTEM "fonts.dtd">\n'
            '<fontconfig><dir>%s</dir><cachedir>%s</cachedir></fontconfig>\n'
            % (fdir, os.path.join(tmp, 'cache')))

# The document: a line with a tab and a line end (the ruler shows the tab
# mark, the line the end of line mark), then a line with '<' and a line with
# '>', the look-alikes.
HEADER = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
          b'P1C33' + CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF +
          b'DP0,8500,11000' + CRLF)
EOL = b'\x1d'
TAB = b'\x1a'


def doc_of(lines):
    out = HEADER + b'*B0' + CRLF
    for text in lines:
        out += b'S' + CRLF + b'T' + text + EOL + b'\x00' + CRLF
    return out + b'*E' + CRLF


doc = os.path.join(tmp, 'm.doc')
with open(doc, 'wb') as f:
    f.write(doc_of([b'x' + TAB + b'x', b'<', b'>']))

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
dump = os.path.join(tmp, 'dump')


def screen():
    try:
        return open(dump, encoding='utf-8').read()
    except OSError:
        return ''


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


def mask(pic, col, row, cw, chh):
    """The set of the pixels of a cell that are not the color that most
    pixels of the cell have."""
    cells = {}
    counts = {}
    for y in range(row * chh, (row + 1) * chh):
        for x in range(col * cw, (col + 1) * cw):
            c = pic.px(x, y)
            cells[(x - col * cw, y - row * chh)] = c
            counts[c] = counts.get(c, 0) + 1
    bg = max(counts, key=counts.get)
    return frozenset(p for p, c in cells.items() if c != bg)


def overlap(a, b):
    """How much of the ink of two cells is the same: the size of the common
    ink over the size of all the ink, 0 when both are empty."""
    return len(a & b) / len(a | b) if (a | b) else 0.0


SAME = 0.6
# The server fonts draw the bold of the marks with more ink than the typed
# sign has, so that the ink of a mark and of the sign share less.
CORE_SAME = 0.3


def core_font():
    """The name of a server font of fixed width in the Latin 1 set of
    characters, which has no glyph for the triangles, or None."""
    if not shutil.which('xlsfonts'):
        return None
    out = subprocess.run(['xlsfonts', '-fn',
        '-*-*-medium-r-normal--*-*-*-*-c-*-iso8859-1'],
        env=dict(os.environ, DISPLAY=disp), capture_output=True,
        text=True).stdout.split('\n')
    for name in out:
        m = re.match(r'-[^-]*-[^-]*-medium-r-normal--(\d+)-', name)
        if m is not None and 13 <= int(m.group(1)) <= 20:
            return name
    return None


def run(extra_env):
    """Start the Editor on the document with the environment, return the
    pictures masks of the cells: (end of line mark, '<', tab mark, '>')
    and the screen text."""
    if os.path.exists(dump):
        os.remove(dump)
    env = dict(os.environ, DISPLAY=disp, STRATA_X11_DUMP=dump,
        STRATA_X11_SIZE='24', STRATA_HOME=os.path.join(tmp, 'home'),
        XDG_CONFIG_HOME=os.path.join(tmp, 'cfg'), SHELL='/bin/true')
    env.update(extra_env)
    p = subprocess.Popen([os.path.join(bindir, 'stred'), doc], env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    procs.append(p)
    w = None
    for _ in range(80):
        w = subprocess.run(['xdotool', 'search', '--name', '^stred$'],
            env=env, capture_output=True, text=True).stdout.split()
        if w:
            w = w[0]
            break
        time.sleep(0.1)
    end = time.time() + 8
    s = ''
    while time.time() < end:
        s = screen()
        if '\u25c4' in s and '\u25ba' in s:
            break
        time.sleep(0.1)
    time.sleep(0.8)
    s = screen()
    data = subprocess.run(['xwd', '-silent', '-id', w], env=env,
        capture_output=True).stdout
    subprocess.run(['xdotool', 'key', '--window', w, 'ctrl+q'], env=env,
        capture_output=True)
    time.sleep(0.4)
    p.terminate()
    p.wait()
    m = re.search(r'\x01grid (\d+) (\d+) (\d+) (\d+)', s)
    if m is None or w is None:
        return None, s
    cw, chh = int(m.group(3)), int(m.group(4))
    rows = s.split('\x01')[0].split('\n')
    re0 = [i for i, r in enumerate(rows) if r.startswith('x') and
           '\u25c4' in r]
    rt = [i for i, r in enumerate(rows) if '\u25ba' in r]
    r1 = [i for i, r in enumerate(rows) if r.startswith('<')]
    r2 = [i for i, r in enumerate(rows) if r.startswith('>')]
    if not re0 or not rt or not r1 or not r2:
        return None, s
    pic = Pic(data)
    eol = rows[re0[0]].index('\u25c4')
    tab = rows[rt[0]].index('\u25ba')
    return ((mask(pic, eol, re0[0], cw, chh), mask(pic, 0, r1[0], cw, chh),
             mask(pic, tab, rt[0], cw, chh), mask(pic, 0, r2[0], cw, chh)), s)


try:
    # 1. The only font has neither triangle.
    got, s = run({'FONTCONFIG_FILE': conf, 'STRATA_X11_XFT': family})
    check('the Editor starts with the font %s and shows both marks' % family,
        got is not None, s)
    if got is not None:
        eol, lt, tab, gt = got
        check('the look-alike < has ink', len(lt) > 0)
        # The marks are drawn in bold, so the ink of a mark is a little more
        # than the ink of the typed sign, and not the same.
        check('the end of line mark is drawn as the look-alike <, not as a '
            'box or as nothing (%.2f)' % overlap(eol, lt),
            overlap(eol, lt) >= SAME, (len(eol), len(lt)))
        check('the tab mark is drawn as the look-alike >, not as a box or as '
            'nothing (%.2f)' % overlap(tab, gt), overlap(tab, gt) >= SAME,
            (len(tab), len(gt)))

    # 2. The fonts of the server (no Xft) with a font that has no glyph for
    # the triangles.
    core = core_font()
    if core is not None:
        got, s = run({'STRATA_X11_XFT': '0', 'STRATA_X11_FONT': core})
        check('the server font %s: the Editor shows both marks' % core,
            got is not None, s)
        if got is not None:
            eol, lt, tab, gt = got
            check('the server font: the end of line mark is the look-alike '
                '<, not a box (%.2f)' % overlap(eol, lt),
                overlap(eol, lt) >= CORE_SAME, (len(eol), len(lt)))
            check('the server font: the tab mark is the look-alike >, not a '
                'box (%.2f)' % overlap(tab, gt),
                overlap(tab, gt) >= CORE_SAME, (len(tab), len(gt)))
    else:
        print('SKIP: no server font of fixed width in Latin 1')

    # 3. The usual fonts: a font that has the glyphs draws the triangles.
    usual = [f for f in files_with(':charset=25c4') &
             files_with(':charset=25ba') if 'Mono' in f]
    if usual:
        got, s = run({})
        check('the usual fonts: the Editor shows both marks', got is not None,
            s)
        if got is not None:
            eol, lt, tab, gt = got
            check('the usual fonts: the end of line mark is the triangle, '
                'not the look-alike (%.2f)' % overlap(eol, lt),
                len(eol) > 0 and overlap(eol, lt) < SAME)
            check('the usual fonts: the tab mark is the triangle, not the '
                'look-alike (%.2f)' % overlap(tab, gt),
                len(tab) > 0 and overlap(tab, gt) < SAME)
    else:
        print('SKIP: no installed monospaced font has the triangles')
finally:
    for p in procs:
        if p.poll() is None:
            p.terminate()
    xvfb.terminate()
    shutil.rmtree(tmp, ignore_errors=True)

print('xmarkglyph: %d failures' % fails)
sys.exit(1 if fails else 0)
