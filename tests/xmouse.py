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

"""The mouse in the X11 Editor (docs/MOUSE.txt, stages 1 and 2): the wheel
scrolls the view, a click places the cursor, the highlight end follows a
click, and a menu, a list's selection and the status line behave as the study
says.  The Editor runs under Xvfb, the buttons come from xdotool (real pointer
events, so that Ctrl is in the state of the event) and the screen is the text
that the program writes to $STRATA_X11_DUMP.  The checks read the cursor cell
and the Pos and Sect of the status line, not pixels.  No printer is used."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, sys, time, shutil, struct, subprocess, tempfile
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bindir = os.environ.get('XBIN', os.path.join(os.environ.get('STRATA_BUILD_DIR',
    ''), 'x11'))
sample = os.environ.get('DOCSRC', os.path.join(root, 'tests', 'samples'))
helpf = os.environ.get('STRATA_HELP', os.path.join(
    os.environ.get('STRATA_BUILD_DIR', ''), 'strata.hlp'))
for tool in ('Xvfb', 'xdotool', 'xkbcomp'):
    if not shutil.which(tool): print('SKIP: %s not '
        'installed' % tool); sys.exit(0)
if not os.path.exists(os.path.join(bindir, 'stred')): print('SKIP: run `make '
    'x11` first'); sys.exit(0)

# Xvfb picks a display number that is free (-displayfd).  A number taken
# from the process id could belong to an Xvfb that is still running, and
# then Xvfb fails to start and the tests meet the windows of that server.
dfd_r, dfd_w = os.pipe()
# The server is not reset when the last client leaves (-noreset): a reset
# would bring back the key map that one_level_function_keys changes.
xvfb = subprocess.Popen(['Xvfb', '-noreset', '-displayfd', str(dfd_w),
    '-screen', '0', '1280x800x24'], stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL, pass_fds=(dfd_w,))
os.close(dfd_w)
num = b''
while not num.endswith(b'\n'):
    chunk = os.read(dfd_r, 16)
    if not chunk:
        print('FAIL: Xvfb did not start'); sys.exit(1)
    num += chunk
os.close(dfd_r)
disp = ':' + num.decode().strip()

# The key map of Xvfb gives each of the keys F1 to F12 the symbols
# [F1, F1, F1, F1, XF86Switch_VT_1] on the type CTRL+ALT.  To type F1, xdotool
# 3.20160805 finds that F1 is on a level that needs Alt, and so it presses
# Alt_L around the key: the program is then sent Alt-F1, Alt-F12 and
# Ctrl-Alt-F12 (Ctrl-Alt-F12 is even the keysym XF86Switch_VT_12) although
# the test asked for F1, F12 and Ctrl-F12.  The program is right to take the
# Alt state of the events it gets.  Give the function keys one level, as the
# keyboard of a person who has no virtual consoles to switch has.
def one_level_function_keys(display):
    km = subprocess.run(['xkbcomp', '-xkb', display, '-'],
        capture_output=True, text=True).stdout
    km = re.sub(r'(key <FK\d\d> \{\s*)type= "CTRL\+ALT",\s*'
        r'symbols\[Group1\]= \[\s*(F\d+),[^\]]*\]',
        r'\1type= "ONE_LEVEL", symbols[Group1]= [ \2 ]', km)
    subprocess.run(['xkbcomp', '-w0', '-', display], input=km, text=True,
        capture_output=True)
one_level_function_keys(disp)
tmp = tempfile.mkdtemp(); dump = os.path.join(tmp, 'dump')
# No test may start a real program that opens a PDF: xdg-open, gio and open
# are guards (tests/shadowbin) at the front of PATH.
shadowbin = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         'shadowbin')
env = dict(os.environ, DISPLAY=disp, STRATA_X11_DUMP=dump,
    PATH=shadowbin + ':' + os.environ.get('PATH', ''),
    XDG_CONFIG_HOME=os.path.join(tmp, 'cfg'),
    STRATA_HOME=os.path.join(tmp, 'strata-home'), SHELL='/bin/true')
fails = 0; procs = []

def xdo(*a):
    return subprocess.run(['xdotool'] + list(a), env=env, capture_output=True,
        text=True).stdout.strip()
def start(args, cwd=None):
    if os.path.exists(dump): os.remove(dump)
    p = subprocess.Popen(args, env=env, cwd=cwd, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL); procs.append(p)
    wait(lambda s: len(s) > 0); return p
def flat(s):
    # The text of a screen on one line: the frames of boxes are dropped and
    # a message that a box wraps (a long path) is joined again.
    return ' '.join(re.sub('[\u2551\u2554\u2557\u255a\u255d\u2550]', ' ',
        s).split())
def screen():
    try: return open(dump, encoding='utf-8').read()
    except OSError: return ''
def wait(pred, t=6.0):
    end = time.time() + t; s = ''
    while time.time() < end:
        s = screen()
        if s and pred(s): return s
        time.sleep(0.1)
    return s
def win(name):
    for _ in range(40):
        w = xdo('search', '--name', '^%s$' % name)
        if w: return w.split()[0]
        time.sleep(0.1)
    return None
def key(w, *keys): subprocess.run(['xdotool', 'key', '--window', w, '--delay',
    '70'] + list(keys), env=env, capture_output=True)
def typ(w, s): subprocess.run(['xdotool', 'type', '--window', w, '--delay',
    '40', s], env=env, capture_output=True)
def pixel(w, x, y):
    """The color 0xRRGGBB of the pixel (x, y) of the window, as the X
    server shows it (xwd writes the window, with a header of 25 numbers)."""
    data = subprocess.run(['xwd', '-id', w, '-silent'], env=env,
        capture_output=True).stdout
    h = struct.unpack('>25I', data[:100])
    base = h[0] + h[19] * 12
    if y < 0:
        y += h[5]
    bpp = h[11] // 8
    raw = data[base + y * h[12] + x * bpp:base + y * h[12] + (x + 1) * bpp]
    v = int.from_bytes(raw, 'little' if h[7] == 0 else 'big')
    rgb = 0
    for mask in (h[14], h[15], h[16]):
        shift = (mask & -mask).bit_length() - 1
        rgb = (rgb << 8) | (((v & mask) >> shift) * 255 // (mask >> shift))
    return rgb
def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra))); fails += 0 if cond else 1


import zlib
CRLF = b'\r\n'
EOL = b'\x1d'
HEADER = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
          b'P1C33' + CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF +
          b'DP0,8500,11000' + CRLF)
BEFORE = 30
AFTER = 30


def png(path, w, h):
    """A picture of w by h pixels, a gradient."""
    raw = b''
    for y in range(h):
        raw += b'\x00' + b''.join(bytes([x * 255 // w, y * 255 // h, 128])
                                  for x in range(w))

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)

    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' +
        chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
        chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def document(name=b'pics/p.png', structured=False):
    pic = b'\x1325(1' + name + b'\x00' + CRLF + b'22000' + CRLF + b'\x13)'
    if structured:
        out = (HEADER + b'*B1' + CRLF + b'S' + CRLF + b'+' + CRLF + b'S' +
               CRLF + b'THeadline of the section' + EOL + b'\x00' + CRLF)
        first = False
    else:
        out = HEADER + b'*B0' + CRLF + b'S' + CRLF
        first = True
    blocks = [b'Line %02d before the picture.' % i + EOL
              for i in range(BEFORE)]
    blocks.append(pic + EOL)
    blocks += [b'Line %02d after the picture.' % i + EOL
               for i in range(AFTER)]

    for b in blocks:
        out += (b'' if first else b'B' + CRLF) + b'T' + b + b'\x00' + CRLF
        first = False

    return out + b'*E' + CRLF



def rows_of(s):
    # The screen rows (without the lines that begin with the mark \x01).
    return [l for l in s.split('\n') if not l.startswith('\x01')]
def info(s, what):
    m = re.search('\x01' + what + r' (.*)', s)
    return [int(v) for v in m.group(1).split()] if m else None
def cur(s):
    c = info(s, 'cursor'); return (c[0], c[1]) if c else None
def pos(s):
    m = re.search(r'Pos:\s*(\d+)', rows_of(s)[0]); return int(m.group(1)) if m else None
def sect(s):
    m = re.search(r'Sect:\s*([\d.]+)', rows_of(s)[0]); return m.group(1) if m else None
def shift(old, new, lo, hi):
    """How many rows the text moved up: the d for which new[y] is old[y+d]."""
    a = [l.rstrip() for l in rows_of(old)]; b = [l.rstrip() for l in rows_of(new)]
    for d in range(-60, 61):
        if d == 0: continue
        if all(0 <= y + d < len(a) and 0 <= y < len(b) and b[y] == a[y + d]
               for y in range(lo, hi)):
            return d
    return None
def settle(t=0.7): time.sleep(t)
def move(w, col, row):
    xdo('mousemove', '--window', w, str(col * cw + cw // 2),
        str(row * chh + chh // 2))
def click(w, col, row, button='1', ctrl=False):
    move(w, col, row); settle(0.15)
    if ctrl:
        xdo('keydown', 'ctrl')
    xdo('click', button)
    if ctrl:
        xdo('keyup', 'ctrl')
    settle()
def wheel(w, n, down=True, ctrl=False, col=40, row=10):
    move(w, col, row); settle(0.15)
    if ctrl:
        xdo('keydown', 'ctrl')
    for _ in range(n):
        xdo('click', '5' if down else '4'); time.sleep(0.12)
    if ctrl:
        xdo('keyup', 'ctrl')
    settle()
def find(s, text):
    for y, l in enumerate(rows_of(s)):
        x = l.find(text)
        if x >= 0: return x, y
    return None

try:
    # ---- a long document: the wheel -----------------------------------
    doc = os.path.join(tmp, 'a.doc')
    shutil.copy(os.path.join(sample, 'brief-long.doc'), doc)
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc])
    w = win('stred')
    xdo('windowfocus', w); time.sleep(0.3)
    s0 = wait(lambda s: 'Aims and Approach' in s); settle()
    s0 = screen()
    g = info(s0, 'grid'); R = g[1]; cw = g[2]; chh = g[3]
    last = R - 3        # the last text row: two rows of keys follow
    pos0 = pos(s0)
    wheel(w, 1)
    s1 = screen()
    check('wheel: one step down moves the text up by three rows',
          shift(s0, s1, 2, last - 6) == 3, (shift(s0, s1, 2, last - 6),))
    check('wheel: the cursor is kept inside the view',
          cur(s1) is not None and 2 <= cur(s1)[1] <= last, cur(s1))
    wheel(w, 1, down=False)
    s2 = screen()
    a0 = [l.rstrip() for l in rows_of(s0)[2:last]]
    a2 = [l.rstrip() for l in rows_of(s2)[2:last]]
    check('wheel: one step up brings the text back', a0 == a2,
          [(i, x[:30], y[:30]) for i, (x, y) in enumerate(zip(a0, a2))
           if x != y][:3])
    wheel(w, 1, ctrl=True)
    s3 = screen()
    half = (R - 3) // 2
    check('wheel: Ctrl and one step moves by half a screen (%d rows)' % half,
          shift(s2, s3, 2, last - half - 2) == half,
          (shift(s2, s3, 2, last - half - 2),))
    check('wheel: the cursor is inside the view after a half screen',
          2 <= cur(s3)[1] <= last, cur(s3))
    wheel(w, 1, down=False, ctrl=True)
    s4 = screen()
    check('wheel: Ctrl and one step up returns by half a screen',
          [l.rstrip() for l in rows_of(s2)[2:last]] ==
          [l.rstrip() for l in rows_of(s4)[2:last]])
    wheel(w, 8)
    s5 = screen()
    check('wheel: eight steps down: the cursor follows into the view and '
          'the position changed', 2 <= cur(s5)[1] <= last and
          pos(s5) != pos0 and sect(s5) != sect(s0), (cur(s5), sect(s5)))
    wheel(w, 70)
    s6 = screen()
    wheel(w, 5)
    s6b = screen()
    check('wheel: the end of the document stops the view (more steps change '
          'nothing)', [l.rstrip() for l in rows_of(s6)[2:last]] ==
          [l.rstrip() for l in rows_of(s6b)[2:last]] and
          any(l.strip() for l in rows_of(s6)[2:last]))
    wheel(w, 60, down=False)
    s7 = screen()
    check('wheel: back at the top the view is the first screen again',
          [l.rstrip() for l in rows_of(s7)[2:last]] ==
          [l.rstrip() for l in rows_of(s0)[2:last]])
    # ---- clicks in plain text, wrapped lines, sections ------------------
    s = screen(); x1, y1 = find(s, 'orchard on Heron')
    click(w, x1 + 2, y1); sa = screen()
    check('click: the cursor is put on the cell that was clicked',
          cur(sa) == (x1 + 2, y1), (cur(sa), x1 + 2, y1))
    click(w, x1 + 8, y1); sb = screen()
    check('click: six cells to the right is six characters on',
          cur(sb) == (x1 + 8, y1) and pos(sb) == pos(sa) + 6,
          (cur(sb), pos(sa), pos(sb)))
    click(w, 5, y1 + 1); sc = screen()
    check('click: a row of a wrapped line is found',
          cur(sc) == (5, y1 + 1) and pos(sc) == 6, (cur(sc), pos(sc)))
    short = rows_of(s)[y1 + 2].rstrip()
    click(w, 70, y1 + 2); sd = screen()
    check('click: after the end of a row the cursor is at the end of the row',
          cur(sd) == (len(short) - 1, y1 + 2), (cur(sd), len(short)))
    xt, yt = find(s, 'A trial pit')
    click(w, 0, yt); se = screen()
    check('click: the margin of an indented row gives the start of the row '
          'and the section of the status line is that of the row',
          cur(se) == (xt, yt) and sect(se) == '1.1.1', (cur(se), sect(se)))
    rule = [i for i, l in enumerate(rows_of(s)) if i > 1 and l.strip() and
            set(l.strip()) == {'\u2500'}][0]
    click(w, 30, rule)
    click(w, 5, 0)
    sf = screen()
    check('click: a rule line and the status line do nothing',
          cur(sf) == cur(se) and pos(sf) == pos(se), (cur(sf), cur(se)))
    p.terminate(); p.wait(5)

    # ---- a table, columns of tabs, the highlight, menus -------------------
    shutil.copy(os.path.join(sample, 'plan-title-table.doc'), doc)
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc])
    w = win('stred')
    xdo('windowfocus', w); time.sleep(0.3)
    s = wait(lambda s: 'Orchard Plan' in s); settle(); s = screen()
    xm, ym = find(s, 'Mara Ellison')
    click(w, xm, ym); sm = screen()
    click(w, xm - 4, ym); sg = screen()
    check('click: a column of a tab line: the character under the pointer',
          cur(sm) == (xm, ym) and pos(sm) > 0, (cur(sm), xm, ym))
    xtab = rows_of(s)[ym].find('\u25ba')
    check('click: a cell inside a tab goes to the tab',
          cur(sg) == (xtab, ym), (cur(sg), xtab))
    xr, yr = find(s, 'Response')
    click(w, xr + 3, yr); st1 = screen()
    check('click: a cell of a table', cur(st1) == (xr + 3, yr), cur(st1))
    xw, yw = find(s, 'before nine')
    click(w, xw + 2, yw); st2 = screen()
    check('click: the second row of a wrapped cell',
          cur(st2) == (xw + 2, yw), cur(st2))
    xc, yc = find(s, 'Noise from visitors')
    click(w, 5, yc + 1); st3 = screen()
    end = len('Noise from visitors')
    check('click: a column with fewer rows gives the end of its text',
          cur(st3) == (xc + end, yc), (cur(st3), xc + end, yc))
    click(w, xc + 25 + 1, yc); st4 = screen()
    check('click: the separator of the columns goes to the nearest column',
          cur(st4)[1] == yc, cur(st4))
    # The highlight: a click moves its end.
    click(w, 5, 22); sh0 = screen()
    key(w, 'F3'); settle(); key(w, 'Return'); settle()
    click(w, 12, 24); sh1 = screen()
    check('click: in the highlight of a text the end follows the click',
          cur(sh1) == (12, 24) and pos(sh1) != pos(sh0), (cur(sh1),))
    click(w, 5, 40); sh2 = screen()
    check('click: in the highlight of a text another block is not taken',
          cur(sh2) == cur(sh1), (cur(sh2), cur(sh1)))
    key(w, 'Escape'); settle()
    click(w, 5, 22)
    key(w, 'F3'); settle(); key(w, 'Down'); key(w, 'Down'); key(w, 'Return')
    settle()
    click(w, 5, 40); sh3 = screen()
    check('click: in the highlight of a block the end goes to another block',
          cur(sh3) == (5, 40), cur(sh3))
    key(w, 'Escape'); settle()
    # A menu: the mouse does nothing.  The first item is run with Enter once
    # without the mouse and once after clicks and the wheel: the same panel.
    sp = screen()
    key(w, 'F12'); settle(); sm0 = screen()
    key(w, 'Return'); settle(1.2); r0 = rows_of(screen())
    key(w, 'Escape'); key(w, 'Escape'); key(w, 'Escape'); settle()
    key(w, 'F12'); settle()
    click(w, 12, 24); wheel(w, 3); sm1 = screen()
    check('menu: a click and the wheel change nothing',
          rows_of(sm1) == rows_of(sm0))
    key(w, 'Return'); settle(1.2); r1 = rows_of(screen())
    check('menu: the item that is run after them is the same', r1 == r0 and
          r0 != rows_of(sp), (r1 == r0, r0 == rows_of(sp)))
    key(w, 'Escape'); key(w, 'Escape'); key(w, 'Escape'); settle()
    click(w, 5, 0)
    check('click: nothing when the menu is closed and the click is on the '
          'status line', cur(screen()) == cur(sp))
    p.terminate(); p.wait(5)

    # ---- a click on a row of a picture; the wheel in a list --------------
    os.makedirs(os.path.join(tmp, 'pics'))
    png(os.path.join(tmp, 'pics', 'p.png'), 240, 160)
    pdoc = os.path.join(tmp, 'pic.doc')
    open(pdoc, 'wb').write(document())
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, pdoc], cwd=tmp)
    w = win('stred')
    xdo('windowfocus', w); time.sleep(0.3)
    s = wait(lambda s: 'Line 00 before' in s); settle()
    wheel(w, 9)
    s = screen()
    prow = [i for i, l in enumerate(rows_of(s)) if '\ue000' in l]
    check('picture: the wheel brings the picture into view', len(prow) > 3,
          prow)
    if len(prow) > 3:
        r = prow[len(prow) // 2]
        click(w, 20, r); sp1 = screen()
        check('click: on a row of a picture the cursor is on that row',
              cur(sp1) is not None and cur(sp1)[1] == r, (cur(sp1), r))
        key(w, 'Down'); settle(); sp2 = screen()
        cy = cur(sp2)[1]
        check('click: the cursor stands on the picture, one Down leaves it',
              'Line 00 after' in rows_of(sp2)[cy], rows_of(sp2)[cy])
    p.terminate(); p.wait(5)
    # The wheel in a list: a step is three Down keys; Ctrl a Page Down.
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc])
    w = win('stred')
    xdo('windowfocus', w); time.sleep(0.3)
    wait(lambda s: 'Orchard Plan' in s); settle()
    # The unit list of Copy: Text is first; the wheel (three rows) goes to the
    # end of the list, Block or Section, which a click may take to another
    # block, and Text does not.
    click(w, 5, 22)
    key(w, 'F3'); settle()
    wheel(w, 1, col=12, row=12)
    key(w, 'Return'); settle()
    click(w, 5, 40); wl = screen()
    check('list: the wheel moves the selection of the list of units '
          '(a click in another block is then taken)', cur(wl) == (5, 40),
          cur(wl))
    key(w, 'Escape'); settle()
    click(w, 5, 22)
    key(w, 'F3'); settle(); key(w, 'Return'); settle()
    click(w, 5, 40); wl2 = screen()
    check('list: without the wheel the first unit, Text, takes no click in '
          'another block', cur(wl2) == (5, 22), cur(wl2))
    key(w, 'Escape'); settle()
    p.terminate(); p.wait(5)
finally:
    for q in procs:
        if q.poll() is None:
            q.terminate()
    xvfb.terminate()
    shutil.rmtree(tmp, ignore_errors=True)
print('xmouse: %d failure(s)' % fails)
sys.exit(1 if fails else 0)
