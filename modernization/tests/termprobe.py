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

"""Probe a terminal emulator on a virtual display (Xvfb); stdlib only.

This is a study helper (docs/TERMINALS.txt), not part of the smoke test.  It
needs a running X server (DISPLAY), xdotool and ImageMagick (import, convert).

  termprobe.py shot  --out DIR --label L --stred PATH --doc FILE
        [--term TERM|native] [--lang LANG] [--lcall LC_ALL] [--keys K,K]
        -- TERMINAL ARGS... {INNER}
    Starts the terminal with the Editor on FILE (the word {INNER} in the
    arguments is replaced by a script that sets TERM and the locale and runs
    the Editor), waits, sends the keys of --keys (xdotool names), takes
    DIR/L.png of the window and DIR/L.txt (the size of the terminal as the
    program reads it, the TERM and the locale), then ends the terminal.

  termprobe.py keys --out DIR --label L [--term TERM|native] -- TERMINAL ARGS {INNER}
    Starts the terminal with a key reader, sends every key that the Editor
    uses with xdotool, and writes DIR/L.keys: for each key the bytes that
    arrived, the key that the Editor's decoder (src/term.c) makes of them,
    and ok or BAD.

    --skip K,K leaves out keys (alacritty: NBSP starts its vi mode);
    --click clicks in the window first (for toolkits that need a click).
    --prefix CMD puts a command in front of the key reader (tmux, screen).

  termprobe.py width --out DIR --label L [--term TERM|native] -- TERMINAL
    ARGS {INNER}   writes DIR/L.width: the characters that do not take one
    cell (the cursor position is asked for after each).

  termprobe.py reader FILE      (used by the keys mode inside the terminal)
"""
import sys as _s; _s.dont_write_bytecode = True
import os, signal, subprocess, sys, time, argparse, select, termios, tty

# The keys that the Editor uses: (name for xdotool, name of the Editor key).
KEYLIST = []
for i in range(1, 13):
    KEYLIST += [('F%d' % i, 'F%d' % i), ('shift+F%d' % i, 'S-F%d' % i),
        ('ctrl+F%d' % i, 'C-F%d' % i), ('alt+F%d' % i, 'A-F%d' % i)]
for n, e in (('Up', 'UP'), ('Down', 'DOWN'), ('Left', 'LEFT'),
        ('Right', 'RIGHT'), ('Home', 'HOME'), ('End', 'END'),
        ('Prior', 'PGUP'), ('Next', 'PGDN')):
    KEYLIST += [(n, e), ('shift+' + n, 'S-' + e), ('ctrl+' + n, 'C-' + e)]
KEYLIST += [('Insert', 'INS'), ('shift+Insert', 'S-INS'), ('Delete', 'DEL'),
    ('BackSpace', 'BKSP'), ('Tab', 'TAB'), ('shift+Tab', 'BTAB'),
    ('Return', 'ENTER'), ('Escape', 'ESC'), ('ctrl+space', 'C-SPACE'),
    ('ctrl+shift+space', 'NBSP'), ('ctrl+a', 'C-a'), ('ctrl+b', 'C-b'),
    ('ctrl+e', 'C-e'), ('ctrl+k', 'C-k'), ('ctrl+u', 'C-u'),
    ('ctrl+z', 'C-z'), ('alt+f', 'A-f'), ('alt+x', 'A-x'),
    ('alt+1', 'A-1'), ('alt+BackSpace', 'A-BKSP'), ('ctrl+slash', 'C-1f')]

# The sequences that parse_csi and term_getkey_raw of src/term.c turn into
# keys (the same rules, in Python).
def decode(b):
    """The Editor key name for the bytes b (a bytes object), or '?'."""
    def mods(m):
        m -= 1
        return ('S-' if m & 1 else '') + ('A-' if m & 2 else '') + (
            'C-' if m & 4 else '')
    if b == b'\x1b':
        return 'ESC'
    if b[:1] == b'\x1b' and len(b) > 1:
        d = b[1:2]
        if d == b'[':
            return csi(b[2:], mods)
        if d == b'O':
            return {b'P': 'F1', b'Q': 'F2', b'R': 'F3', b'S': 'F4',
                b'A': 'UP', b'B': 'DOWN', b'C': 'RIGHT', b'D': 'LEFT',
                b'H': 'HOME', b'F': 'END'}.get(b[2:3], '?') if len(b) == 3 \
                else '?'
        if len(b) == 2:
            c = b[1]
            if c < 0x20 and c != 0x0d:
                return 'A-C-' + chr(c + 0x60)
            return 'A-' + ('BKSP' if c == 127 else chr(c))
        return '?'
    if len(b) == 1:
        c = b[0]
        return {0: 'C-SPACE', 9: 'TAB', 13: 'ENTER', 127: 'BKSP'}.get(c,
            'C-' + (chr(c + 0x60) if c <= 26 else '%02x' % c) if c < 0x20
            else chr(c))
    return '?'


def csi(rest, mods):
    s = rest.decode('latin-1')
    i = 0
    num = [0]
    while i < len(s) and (s[i].isdigit() or s[i] == ';'):
        if s[i] == ';':
            num.append(0)
        else:
            num[-1] = num[-1] * 10 + int(s[i])
        i += 1
    if i >= len(s):
        return '?'
    c, extra = s[i], s[i + 1:]
    m = mods(num[1] if len(num) > 1 and num[1] else 1) if len(num) > 1 else ''
    if c in 'ABCDHF' and not extra:
        return m + {'A': 'UP', 'B': 'DOWN', 'C': 'RIGHT', 'D': 'LEFT',
            'H': 'HOME', 'F': 'END'}[c]
    if c == 'Z' and not extra:
        return 'BTAB'
    if c in 'PQRS' and not extra:
        return m + 'F' + str('PQRS'.index(c) + 1)
    if c == '[' and len(extra) == 1 and extra in 'ABCDE':
        return 'F' + str('ABCDE'.index(extra) + 1)
    if c == 'u' and num[0] == 32 and m == 'S-C-':
        return 'NBSP'
    if c == '~' and not extra:
        n = num[0]
        t = {1: 'HOME', 7: 'HOME', 2: 'INS', 3: 'DEL', 4: 'END', 8: 'END',
            5: 'PGUP', 6: 'PGDN'}
        if n in t:
            return m + t[n]
        if 11 <= n <= 15:
            return m + 'F%d' % (n - 10)
        if 17 <= n <= 21:
            return m + 'F%d' % (n - 11)
        if n in (23, 24):
            return m + 'F%d' % (n - 12)
        if n == 27 and len(num) > 2 and num[2] == 32 and m == 'S-C-':
            return 'NBSP'
    return '?'


def canon(name):
    """The name with its modifier letters in the order S-, A-, C-."""
    mods = ''
    rest = name
    while len(rest) > 2 and rest[0] in 'SAC' and rest[1] == '-':
        mods += rest[0]
        rest = rest[2:]
    return ''.join(m + '-' for m in 'SAC' if m in mods) + rest


def reader(path):
    """Run inside the terminal: log the groups of bytes that arrive."""
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    tty.setraw(fd)
    out = open(path, 'a')
    out.write('READY %s\n' % os.environ.get('TERM', ''))
    out.flush()
    try:
        while True:
            r, _, _ = select.select([fd], [], [], 30)
            if not r:
                break
            data = os.read(fd, 64)
            time.sleep(0.15)
            while select.select([fd], [], [], 0)[0]:
                data += os.read(fd, 64)
            out.write('KEY %s\n' % data.hex())
            out.flush()
            if data == b'q' * len(data) and len(data) > 2:
                break
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)


def widths(path):
    """Run inside the terminal: how many cells does each character take?

    Every character of the Editor's set is written after a carriage return,
    then the position of the cursor is asked for (ESC [ 6 n); a character
    that does not move the cursor by one cell is written to PATH.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    src = open(os.path.join(here, '..', 'src', 'layout.c')).read()
    tab = src[src.index('cp437_uni[256]'):]
    tab = tab[:tab.index('};')]
    import re
    cps = sorted(set(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{4})',
        tab)) - set(range(0x20, 0x7f)))
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    tty.setraw(fd)
    bad = []
    try:
        for cp in cps:
            os.write(1, ('\r\x1b[K%s\x1b[6n' % chr(cp)).encode())
            buf = b''
            while not buf.endswith(b'R'):
                r, _, _ = select.select([fd], [], [], 2)
                if not r:
                    break
                buf += os.read(fd, 32)
            try:
                col = int(buf.decode().split(';')[1].rstrip('R'))
            except (IndexError, ValueError):
                col = -1
            if col != 2:
                bad.append('U+%04X=%d' % (cp, col - 1))
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
    with open(path, 'w') as f:
        f.write('WIDTH %d checked, %d not one cell: %s\n' % (len(cps),
            len(bad), ' '.join(bad)))


def start(cmd, env):
    return subprocess.Popen(cmd, env=env, start_new_session=True,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def stop(p):
    try:
        os.killpg(p.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    time.sleep(0.5)
    try:
        os.killpg(p.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    try:
        p.wait(timeout=5)
    except Exception:
        pass


# Keys are sent with XTEST by keycode (xdotool presses Alt for the function
# keys of some keymaps: it picks a shifted level of the key).  The codes are
# those of the evdev keymap of X servers on Linux.
CODES = {'shift': 50, 'ctrl': 37, 'alt': 64, 'Up': 111, 'Down': 116,
    'Left': 113, 'Right': 114, 'Home': 110, 'End': 115, 'Prior': 112,
    'Next': 117, 'Insert': 118, 'Delete': 119, 'BackSpace': 22, 'Tab': 23,
    'Return': 36, 'Escape': 9, 'space': 65, 'slash': 61, '1': 10, 'a': 38,
    'b': 56, 'e': 26, 'k': 45, 'u': 30, 'z': 52, 'f': 41, 'x': 53}
for _i in range(1, 11):
    CODES['F%d' % _i] = 66 + _i
CODES['F11'] = 95
CODES['F12'] = 96
_xlib = None


def fakekey(spec):
    """Press and release a key such as ctrl+shift+F5 with XTEST."""
    global _xlib, _dpy
    import ctypes
    if _xlib is None:
        _xlib = ctypes.CDLL('libX11.so.6')
        _xlib.XOpenDisplay.restype = ctypes.c_void_p
        _dpy = ctypes.c_void_p(_xlib.XOpenDisplay(None))
        _xlib.xtst = ctypes.CDLL('libXtst.so.6')
    parts = spec.split('+')
    codes = [CODES[p] for p in parts]
    for c in codes:
        _xlib.xtst.XTestFakeKeyEvent(_dpy, c, 1, 0)
        _xlib.XFlush(_dpy)
        time.sleep(0.02)
    for c in reversed(codes):
        _xlib.xtst.XTestFakeKeyEvent(_dpy, c, 0, 0)
        _xlib.XFlush(_dpy)
        time.sleep(0.02)


def keylines(path):
    """The KEY lines of the log of the reader."""
    if not os.path.exists(path):
        return []
    return [l for l in open(path).read().splitlines()
        if l.startswith('KEY ')]


def xdo(*a):
    return subprocess.run(['xdotool'] + list(a), capture_output=True,
        text=True)


def find_window(pid_group, tries=40):
    """The id of the biggest mapped window made after the start."""
    for _ in range(tries):
        r = xdo('search', '--onlyvisible', '--name', '.')
        best = None
        for w in r.stdout.split():
            g = xdo('getwindowgeometry', w).stdout
            if 'Geometry' not in g:
                continue
            geo = g.split('Geometry:')[1].split()[0]
            wd, ht = [int(x) for x in geo.split('x')]
            if wd > 200 and ht > 100 and (best is None or wd * ht > best[0]):
                best = (wd * ht, w)
        if best:
            return best[1]
        time.sleep(0.5)
    return None


def main():
    argv = sys.argv[1:]
    if argv and argv[0] == 'reader':
        reader(argv[1])
        return 0
    if argv and argv[0] == 'widths':
        widths(argv[1])
        return 0
    if '--' not in argv:
        print(__doc__)
        return 2
    i = argv.index('--')
    ap = argparse.ArgumentParser()
    ap.add_argument('mode')
    ap.add_argument('--out', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--stred', default='')
    ap.add_argument('--doc', default='')
    ap.add_argument('--term', default='xterm')
    ap.add_argument('--lang', default='en_US.UTF-8')
    ap.add_argument('--lcall', default='')
    ap.add_argument('--keys', default='')
    ap.add_argument('--prefix', default='')
    ap.add_argument('--skip', default='')
    ap.add_argument('--click', action='store_true')
    ap.add_argument('--wait', type=float, default=3.0)
    a = ap.parse_args(argv[:i])
    tcmd = argv[i + 1:]
    os.makedirs(a.out, exist_ok=True)
    inner = os.path.join(a.out, a.label + '.sh')
    me = os.path.abspath(__file__)
    tset = '' if a.term == 'native' else 'TERM=%s ' % a.term
    lc = ('LC_ALL=%s ' % a.lcall) if a.lcall else 'unset LC_ALL; '
    with open(inner, 'w') as f:
        f.write('#!/bin/sh\nunset LD_LIBRARY_PATH\n')
        if a.mode == 'shot':
            f.write('export %sLANG=%s; %s' % (tset, a.lang, lc))
            f.write('echo "size $(stty size) TERM=$TERM LANG=$LANG '
                'LC_ALL=$LC_ALL" > %s/%s.txt\n' % (a.out, a.label))
            f.write('exec %s %s\n' % (a.stred, a.doc))
        elif a.mode == 'width':
            f.write('export %sLANG=%s; %s' % (tset, a.lang, lc))
            f.write('%s python3 -I -B %s widths %s/%s.width\nsleep 4\n' % (
                a.prefix, me, a.out, a.label))
        else:
            f.write('export %sLANG=%s; %s' % (tset, a.lang, lc))
            f.write('exec %s python3 -I -B %s reader %s/%s.log\n' % (
                a.prefix, me, a.out, a.label))
    os.chmod(inner, 0o755)
    cmd = [inner if t == '{INNER}' else t for t in tcmd]
    logf = os.path.join(a.out, a.label + '.log')
    if os.path.exists(logf):
        os.remove(logf)
    # The terminal itself gets the locale too: xterm and others choose UTF-8
    # or not from their own locale when they start.
    env = dict(os.environ)
    env['LANG'] = a.lang
    env.pop('LC_ALL', None)
    env.pop('LC_CTYPE', None)
    if a.lcall:
        env['LC_ALL'] = a.lcall
    p = start(cmd, env)
    try:
        win = find_window(p.pid)
        if win is None:
            print('%s: no window' % a.label)
            return 1
        xdo('windowfocus', win)
        time.sleep(a.wait)
        if a.mode == 'width':
            wf = os.path.join(a.out, a.label + '.width')
            for _ in range(60):
                if os.path.exists(wf):
                    break
                time.sleep(0.5)
            print(open(wf).read().strip() if os.path.exists(wf) else
                '%s: no result' % a.label)
            return 0
        if a.mode == 'shot':
            for k in [k for k in a.keys.split(',') if k]:
                fakekey(k)
                time.sleep(0.7)
            time.sleep(0.5)
            png = os.path.join(a.out, a.label + '.png')
            subprocess.run(['import', '-window', win, png])
            return 0
        res = []
        deadline = time.time() + 20
        while time.time() < deadline and not os.path.exists(logf):
            time.sleep(0.3)
        if a.click:
            xdo('mousemove', '--window', win, '200', '200', 'click', '1')
            time.sleep(0.5)
        skip = a.skip.split(',')
        for xname, ename in KEYLIST:
            if ename in skip:
                continue
            n0 = len(keylines(logf))
            fakekey(xname)
            t0 = time.time()
            got = None
            while time.time() - t0 < 1.5:
                ls = keylines(logf)
                if len(ls) > n0:
                    got = ls[n0]
                    break
                time.sleep(0.1)
            if got is None:
                res.append((ename, xname, '-', 'nothing', 'BAD'))
                continue
            data = bytes.fromhex(got.split()[1])
            dec = decode(data)
            want = canon(ename)
            have = canon(dec) if dec != '?' else '?'
            res.append((ename, xname, data.hex(), dec,
                'ok' if have == want else 'BAD'))
        with open(os.path.join(a.out, a.label + '.keys'), 'w') as f:
            for r in res:
                f.write('%-8s %-18s %-22s %-8s %s\n' % r)
            nbad = sum(1 for r in res if r[4] != 'ok')
            f.write('TOTAL %d keys, %d BAD\n' % (len(res), nbad))
        print('%s: %d keys, %d bad' % (a.label, len(res), nbad))
        return 0
    finally:
        stop(p)


sys.exit(main())
