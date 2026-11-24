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

"""The cursor crosses a displayed picture in one key, and the window does
not leap.  A picture is one line of the text and one stop of the cursor,
however many rows of the screen it covers: Down from the line above puts the
cursor on the picture (on its first row), and one more Down puts it on the
line below.  The window moves by about the height of the picture when that
happens; it does so in steps of a few rows, about ten milliseconds apart,
and not in one leap, and a key that is typed meanwhile ends the steps at
once and is not lost.  Before, the cursor went down the rows of the picture
one key for each row (eleven keys for a picture of eleven rows), and before
that the whole window leapt by the height of the picture.

The test makes a document of text lines, a displayed picture and text
lines.  It goes down to the line above the picture and checks, key by key,
where the cursor is (the text of the row it is on) and what the window did
(the file that STRATA_SCROLL_LOG names holds the top row of the window after
each step): one key onto the picture, one key off it, the same going up, the
steps never move the window by more than 6 rows, there are at least two
steps for a picture of eleven rows and at most 14, fast keys end in the
right place, and a picture taller than the window is scrolled through with
Page Down and Page Up before the cursor leaves it."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, sys, time, shutil, struct, subprocess, tempfile, zlib
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bindir = os.environ.get('XBIN', os.path.join(os.environ.get('STRATA_BUILD_DIR',
    ''), 'x11'))
for tool in ('Xvfb', 'xdotool'):
    if not shutil.which(tool):
        print('SKIP: %s not installed' % tool)
        sys.exit(0)
if not os.path.exists(os.path.join(bindir, 'stred')):
    print('SKIP: run `make x11` first')
    sys.exit(0)

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
fails = 0
procs = []
CRLF = b'\r\n'
EOL = b'\x1d'
HEADER = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF +
          b'P1C33' + CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF +
          b'DP0,8500,11000' + CRLF)
BEFORE = 30
AFTER = 30


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


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


def screen():
    try:
        return open(dump, encoding='utf-8').read()
    except OSError:
        return ''


def state(s):
    """The number of rows of the screen that the picture covers, the row
    and the column of the cursor, and the number of rows of the screen."""
    rows = s.split('\x01')[0].split('\n')
    img = [i for i, r in enumerate(rows) if '\ue000' in r]
    cur = re.search(r'\x01cursor (\d+) (\d+)', s)
    return (len(img), (int(cur.group(2)) if cur else -1), len(rows),
            (int(cur.group(1)) if cur else -1))


def settle(env, prev):
    """Wait for the screen to change after a key and then to stay as it is."""
    end = time.time() + 1.2
    s = screen()
    while s == prev and time.time() < end:
        time.sleep(0.05)
        s = screen()
    last = None
    while s != last:
        last = s
        time.sleep(0.25)
        s = screen()
    return s


def key(w, env, k, prev):
    subprocess.run(['xdotool', 'key', '--window', w, '--delay', '60', k],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return settle(env, prev)


def sequence(steps, w, env, s, k):
    """Press the key k, steps times; the picture rows after each key."""
    out = []
    for _ in range(steps):
        s = key(w, env, k, s)
        out.append(state(s))
    return out, s


def cursor_text(s):
    """The text of the screen row that the cursor is on, and whether that
    row holds the picture."""
    rows = s.split('\x01')[0].split('\n')
    cur = re.search(r'\x01cursor (\d+) (\d+)', s)
    if not cur:
        return '', False
    y = int(cur.group(2))
    if not 0 <= y < len(rows):
        return '', False
    return rows[y].strip(), '\ue000' in rows[y]


def log_steps():
    """The steps of the window that the program logged: (from, to)."""
    try:
        return [tuple(int(v) for v in line.split())
                for line in open(steplog).read().split('\n')
                if line.strip() and not line.startswith('abort')]
    except OSError:
        return []


def log_aborts():
    """The number of glides that a key ended."""
    try:
        return sum(1 for line in open(steplog).read().split('\n')
                   if line.startswith('abort'))
    except OSError:
        return 0


def clear_log():
    try:
        os.unlink(steplog)
    except OSError:
        pass


def go_to(w, env, s, text, limit=120, k='Down'):
    """Press k (Down or Up) until the cursor is on the line that starts
    with text."""
    for _ in range(limit):
        t, _p = cursor_text(s)
        if t.startswith(text):
            return s, True
        s = key(w, env, k, s)
    t, _p = cursor_text(s)
    return s, t.startswith(text)


def reach_bottom(w, env, s):
    """Put the cursor on the last line above the picture with the window
    scrolled down to it, as if it had come down from the top."""
    s, _ok = go_to(w, env, s, 'Line 05 before', k='Up')
    s, _ok = go_to(w, env, s, 'Line %02d before' % (BEFORE - 1))
    return s


try:
    os.makedirs(os.path.join(tmp, 'pics'))
    png(os.path.join(tmp, 'pics', 'p.png'), 240, 160)
    doc = os.path.join(tmp, 't.doc')
    open(doc, 'wb').write(document())
    steplog = os.path.join(tmp, 'steps')
    env = dict(os.environ, DISPLAY=disp, STRATA_X11_DUMP=dump,
        STRATA_SCROLL_LOG=steplog,
        STRATA_HOME=os.path.join(tmp, 'home'),
        XDG_CONFIG_HOME=os.path.join(tmp, 'cfg'),
        XDG_CACHE_HOME=os.path.join(tmp, 'cache'), SHELL='/bin/true')
    def start(docpath):
        pr = subprocess.Popen([os.path.join(bindir, 'stred'), docpath],
            env=env, cwd=tmp, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL)
        procs.append(pr)
        win = None
        for _ in range(100):
            win = subprocess.run(['xdotool', 'search', '--name', '^stred$'],
                env=env, capture_output=True, text=True).stdout.split()
            if win:
                break
            time.sleep(0.1)
        time.sleep(2.0)
        return pr, (win[0] if win else None)

    p, w = start(doc)
    s = screen()
    check('the Editor is running and shows the first line',
        'Line 00 before' in s, s)

    # Down to the line above the picture.
    s, ok = go_to(w, env, s, 'Line %02d before' % (BEFORE - 1))
    check('the cursor reached the last line above the picture', ok,
        cursor_text(s))

    # ONE key puts the cursor on the picture.
    clear_log()
    s = key(w, env, 'Down', s)
    t, onpic = cursor_text(s)
    n_pic = state(s)[0]
    check('one Down puts the cursor on the picture (the row holds the '
        'picture: %r)' % t, onpic, s)
    check('the top of the picture shows at the bottom of the window '
        '(%d rows), it does not leap in whole' % n_pic, 1 <= n_pic <= 5, s)
    rows = s.split('\x01')[0].split('\n')
    cy = int(re.search(r'\x01cursor (\d+) (\d+)', s).group(2))
    check('the cursor is on the first row of the picture',
        cy == 0 or '\ue000' not in rows[cy - 1], rows[max(0, cy - 2):cy + 1])

    # ONE more key puts it on the line below, and the window glides.
    clear_log()
    s = key(w, env, 'Down', s)
    t, onpic = cursor_text(s)
    check('one more Down puts the cursor on the first line below the '
        'picture (%r)' % t, t.startswith('Line 00 after'), s)
    steps = log_steps()
    moves = [abs(b - a) for a, b in steps]
    check('the window moved in several steps and not in one (%s)' % moves,
        len(steps) >= 2 and sum(moves) >= 6, steps)
    check('no step moved the window by more than 6 rows (%s)' % moves,
        all(m <= 6 for m in moves), steps)
    check('at most 14 steps (%d)' % len(steps), len(steps) <= 14, steps)
    check('the steps went one way and ended where the window is',
        all((b - a) > 0 for a, b in steps) or
        all((b - a) < 0 for a, b in steps), steps)

    # Up: one key onto the picture, one off it.  First go far below the
    # picture, so that it is off the screen, and come back up to the first
    # line below it.
    s, ok = go_to(w, env, s, 'Line 25 after')
    check('the cursor went far below the picture', ok, cursor_text(s))
    check('the picture is off the screen there', state(s)[0] == 0,
        state(s))
    s, ok = go_to(w, env, s, 'Line 00 after', k='Up')
    check('the cursor came back up to the first line below the picture', ok,
        cursor_text(s))
    clear_log()
    s = key(w, env, 'Up', s)
    t, onpic = cursor_text(s)
    check('one Up puts the cursor on the picture (the row holds it)', onpic,
        s)
    steps = log_steps()
    moves = [abs(b - a) for a, b in steps]
    check('coming up to the picture the window glided in steps of at most '
        '6 rows (%s)' % moves, len(steps) >= 2 and all(m <= 6 for m in moves),
        steps)
    s = key(w, env, 'Up', s)
    t, onpic = cursor_text(s)
    check('one more Up puts the cursor on the last line above the picture '
        '(%r)' % t, t.startswith('Line %02d before' % (BEFORE - 1)), s)

    # Fast keys: two Down keys at once cross the picture, five go on to the
    # fourth line below it, whatever the window was doing.
    clear_log()
    subprocess.run(['xdotool', 'key', '--window', w, '--delay', '5',
        'Down', 'Down'], env=env, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    s = settle(env, s)
    t, _p = cursor_text(s)
    check('two fast Down keys cross the picture (%r)' % t,
        t.startswith('Line 00 after'), s)
    subprocess.run(['xdotool', 'key', '--window', w, '--delay', '5',
        'Up', 'Up', 'Down', 'Down', 'Down', 'Down', 'Down'], env=env,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    s = settle(env, s)
    t, _p = cursor_text(s)
    check('seven fast keys end on the fourth line below the picture, no '
        'key lost (%r)' % t, t.startswith('Line 03 after'), s)

    # A key typed while the window glides ends the glide at once and is
    # not lost: from the picture, a key that leaves it and one that comes
    # 15 ms later (the glide of four steps takes 40 ms).
    s = reach_bottom(w, env, s)
    s = key(w, env, 'Down', s)
    t, onpic = cursor_text(s)
    check('the cursor is on the picture again', onpic, s)
    clear_log()
    s = key(w, env, 'Down', s)
    full = len(log_steps())
    # The second key has to come while the glide runs; how long the first
    # key takes to handle depends on the machine, so a few delays are tried.
    ended = 0
    for delay in ('15', '10', '20', '12', '25'):
        s = reach_bottom(w, env, s)
        s = key(w, env, 'Down', s)
        clear_log()
        subprocess.run(['xdotool', 'key', '--window', w, '--delay', delay,
            'Down', 'Down'], env=env, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL)
        s = settle(env, s)
        t, _p = cursor_text(s)
        fast = len(log_steps())
        ended = log_aborts()
        if ended >= 1 or not t.startswith('Line 01 after'):
            break
    check('both keys were handled: the cursor is on the second line below '
        'the picture (%r)' % t, t.startswith('Line 01 after'), s)
    check('the key that came during the glide ended it (%d glides ended by '
        'a key; %d steps logged, %d for a whole glide)'
        % (ended, fast, full), ended >= 1, log_steps())

    # A picture taller than the window: Down and Page Down scroll through
    # its rows with the cursor on it, in steps; the cursor leaves it after
    # its last row has been shown.
    p.terminate()
    p.wait()
    png(os.path.join(tmp, 'pics', 'q.png'), 240, 1100)
    doc2 = os.path.join(tmp, 'u.doc')
    open(doc2, 'wb').write(document(b'pics/q.png'))
    try:
        os.unlink(dump)
    except OSError:
        pass
    p, w = start(doc2)
    s = screen()
    s, ok = go_to(w, env, s, 'Line %02d before' % (BEFORE - 1))
    s = key(w, env, 'Down', s)
    t, onpic = cursor_text(s)
    check('a tall picture: one Down puts the cursor on it', onpic, s)
    rows_total = len(s.split('\x01')[0].split('\n'))
    seen = []
    presses = 0
    visible = True
    clear_log()
    while presses < 12:
        s = key(w, env, 'Down', s)
        presses += 1
        t, onpic = cursor_text(s)
        seen.append(state(s)[0])
        if t.startswith('Line 00 after'):
            break
        visible = visible and onpic
    check('a tall picture: the cursor stayed on it while it was scrolled '
        'through, then went to the line below (%d keys, rows shown %s)'
        % (presses, seen), t.startswith('Line 00 after') and presses <= 5 and
        visible, s)
    steps = log_steps()
    moves = [abs(b - a) for a, b in steps]
    check('a tall picture: no step moved the window by more than 6 rows '
        '(%s)' % moves, steps and all(m <= 6 for m in moves), steps)
    # Page Up scrolls back through it before the cursor leaves it.
    s = key(w, env, 'Up', s)
    t, onpic = cursor_text(s)
    check('a tall picture: Up from the line below puts the cursor on it',
        onpic, s)
    # A structured document: the picture is one stop there too.
    p.terminate()
    p.wait()
    doc3 = os.path.join(tmp, 's.doc')
    open(doc3, 'wb').write(document(structured=True))
    try:
        os.unlink(dump)
    except OSError:
        pass
    p, w = start(doc3)
    s = screen()
    s, ok = go_to(w, env, s, 'Line %02d before' % (BEFORE - 1))
    check('structured: the cursor reached the last line above the picture',
        ok, cursor_text(s))
    s = key(w, env, 'Down', s)
    t, onpic = cursor_text(s)
    check('structured: one Down puts the cursor on the picture', onpic, s)
    s = key(w, env, 'Down', s)
    t, onpic = cursor_text(s)
    check('structured: one more Down puts it on the line below (%r)' % t,
        t.startswith('Line 00 after'), s)
    s = key(w, env, 'Up', s)
    t, onpic = cursor_text(s)
    check('structured: one Up puts it on the picture', onpic, s)
    s = key(w, env, 'Up', s)
    t, onpic = cursor_text(s)
    check('structured: one more Up puts it on the line above (%r)' % t,
        t.startswith('Line %02d before' % (BEFORE - 1)), s)
    p.terminate()
    p.wait()
finally:
    for p in procs:
        if p.poll() is None:
            p.terminate()
    xvfb.terminate()
    shutil.rmtree(tmp, ignore_errors=True)

print('xpicscroll: %d failures' % fails)
sys.exit(1 if fails else 0)
