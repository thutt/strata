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

"""Lag of the cursor keys in the X11 Editor.

The Editor is started under a private Xvfb on a long document.  The test
sends a burst of Down key events as fast as xdotool can send them, and it
measures how long the Editor needs until the screen has stopped changing.
The Editor writes the text of its screen to $STRATA_X11_DUMP at every draw,
which is what the test watches.  A second run sends the same number of keys
slowly, each one after the draw of the one before it, and gives the position
that the burst must reach: no event may be lost or doubled.

With no argument the test is a check (see the bounds below) and it prints
ok and FAIL lines.  With --measure it prints one line of milliseconds for each
document, and it is the measurement that is used to look for the commit that
made the cursor lag.

Environment: XBIN (the folder with the X11 stred), STRATA_BUILD_DIR (its
parent, default as the other X11 tests), STRATA_HELP, XLAG_DOCS (the
documents, separated by colons; default the Getting Started document and the
Reference manual of manual/doc), XLAG_KEYS (the number of events, default
100), XLAG_RUNS (default 3: the median is used)."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, sys, time, shutil, subprocess, tempfile, statistics, hashlib
import signal
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bindir = os.environ.get('XBIN', os.path.join(os.environ.get(
    'STRATA_BUILD_DIR', ''), 'x11'))
helpf = os.environ.get('STRATA_HELP', os.path.join(
    os.environ.get('STRATA_BUILD_DIR', ''), 'strata.hlp'))
mandoc = os.path.join(root, 'manual', 'doc')
docs = os.environ.get('XLAG_DOCS', ':'.join([
    os.path.join(mandoc, 'GettingStarted.doc'),
    os.path.join(mandoc, 'strata-reference-manual.doc'), 'LONGBLOCK'])
    ).split(':')
nkeys = int(os.environ.get('XLAG_KEYS', '100'))
depths = [int(d) for d in os.environ.get('XLAG_DEPTHS', '0').split(',')]
nruns = int(os.environ.get('XLAG_RUNS', '3'))
measure = '--measure' in sys.argv
# The bounds of the check, in milliseconds after the last event was sent.
# The machine may be loaded by other jobs: the bound is generous.
IDLE_BOUND_MS = int(os.environ.get('XLAG_BOUND_MS', '2000'))
STABLE_MS = 300
# The drawings that a burst of nkeys events may make: a drawing that the next
# key replaces is left out, so that the Editor never works off a queue.
MAX_DRAWS = int(os.environ.get('XLAG_MAX_DRAWS', '5'))

for tool in ('Xvfb', 'xdotool'):
    if not shutil.which(tool):
        print('SKIP: %s not installed' % tool); sys.exit(0)
if not os.path.exists(os.path.join(bindir, 'stred')):
    print('SKIP: run `make x11` first'); sys.exit(0)

tmp = tempfile.mkdtemp(); dump = os.path.join(tmp, 'dump')
dfd_r, dfd_w = os.pipe()
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
shadowbin = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         'shadowbin')
env = dict(os.environ, DISPLAY=disp, STRATA_X11_DUMP=dump,
    PATH=shadowbin + ':' + os.environ.get('PATH', ''),
    XDG_CONFIG_HOME=os.path.join(tmp, 'cfg'),
    STRATA_HOME=os.path.join(tmp, 'strata-home'), SHELL='/bin/true')
# With XLAG_SLOW_LATEX=N the pictures are made by a latex that waits N seconds
# first, in an empty cache of the test: every keycap of the document is a
# picture that arrives late.  The test must still pass, because the Editor is
# not given a key until its picture jobs are done.
slow_latex = os.environ.get('XLAG_SLOW_LATEX', '')
if slow_latex != '':
    fake = os.path.join(tmp, 'slowtools')
    os.makedirs(fake)
    for prog in ('pdflatex', 'latex'):
        path = os.path.join(fake, prog)
        with open(path, 'w') as f:
            f.write('#!/bin/sh\nsleep %s\nexec %s "$@"\n' %
                    (slow_latex, shutil.which(prog)))
        os.chmod(path, 0o755)
    for prog in ('pdftoppm', 'dvipng'):
        os.symlink(shutil.which(prog), os.path.join(fake, prog))
    env['STRATA_PROGRAM_DIRS'] = fake + ':/usr/bin:/bin'
    env['XDG_CACHE_HOME'] = os.path.join(tmp, 'cache')
fails = 0


def make_long_block(path, nchars):
    """A document of ONE block: a paragraph of nchars characters of words of
    varied length, which the Editor wraps in about nchars/70 lines.  It is the
    case of a long paragraph: the work of a cursor move must not grow with
    the length of the block."""
    d = open(os.path.join(root, 'tests', 'data', 'plain_text.doc'),
        'rb').read()
    head = d[:d.index(b'*B0')]
    words = ['alpha', 'be', 'cargo', 'delta', 'eye', 'fjord', 'go', 'harbour',
        'in', 'jetty', 'keel', 'lamp', 'mast', 'net', 'oar', 'pier']
    out = []; n = 0; i = 0
    while n < nchars:
        w = words[(i * 7 + i // 3) % len(words)]
        out.append(w); n += len(w) + 1; i += 1
    text = ' '.join(out)[:nchars]
    open(path, 'wb').write(head + b'*B0\r\nS\r\nT' + text.encode('ascii')
        + b'\x1d\x00\r\n*E\r\n')


def screen():
    try:
        return open(dump, encoding='utf-8').read()
    except OSError:
        return ''


def wait_for(pred, t=30.0):
    end = time.time() + t; s = ''
    while time.time() < end:
        s = screen()
        if s and pred(s):
            return s
        time.sleep(0.05)
    return s


def pos_of(s):
    """The number after 'Pos:' on the status line, or None."""
    m = re.search(r'Pos:\s*(\d+)', s.split('\n')[0] if s else '')
    return int(m.group(1)) if m else None


def draws_of(s):
    """The number of drawings that the Editor has made (the line draws of
    the dump), or 0 for a program that does not count them."""
    m = re.search(r'\x01draws (\d+)', s or '')
    return int(m.group(1)) if m else 0


def plain(s):
    """The screen without the counter of drawings, to compare two screens."""
    return re.sub(r'\x01draws \d+\n', '', s or '')


def same_place(a, b):
    """Two screens that show the same place of the document: the status line
    is the same and so is the text of the line that holds the cursor.  The
    first row of the window may differ by a line or two, because the view
    follows the cursor in steps that depend on when it was drawn; a key that
    was lost or done twice moves the cursor to another line, which this
    finds."""
    def place(s):
        s = s or ''
        m = re.search(r'\x01cursor (\d+) (\d+)', s)
        rows = plain(s).split('\n')
        y = int(m.group(2)) if m else -1
        return rows[0], (rows[y] if 0 <= y < len(rows) else None)
    pa, pb = place(a), place(b)
    return pa == pb and pa[1] is not None


def window(name):
    for _ in range(100):
        r = subprocess.run(['xdotool', 'search', '--name', '^%s$' % name],
            env=env, capture_output=True, text=True).stdout.split()
        if r:
            return r[0]
        time.sleep(0.1)
    return None


def settle(t0, limit=60.0):
    """Wait until the dump has not changed for STABLE_MS milliseconds.  The
    result is the time from t0 to the last change, in milliseconds, and the
    last screen."""
    last = screen(); last_change = time.time(); end = t0 + limit
    while time.time() < end:
        time.sleep(0.02)
        s = screen()
        if s != last:
            last = s; last_change = time.time()
        elif (time.time() - last_change) * 1000 >= STABLE_MS:
            break
    return (last_change - t0) * 1000.0, last


def editor(doc, depth=0):
    """Start the Editor on a copy of the document: the process, the window.
    The cursor is moved down the document by depth pages (PgDn)."""
    path = os.path.join(tmp, 'lag.doc'); shutil.copy(doc, path)
    if os.path.exists(dump):
        os.remove(dump)
    p = subprocess.Popen([os.path.join(bindir, 'stred'), '-H', helpf, path],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    w = window('stred')
    wait_for(lambda s: len(s) > 0)
    time.sleep(0.5); settle(time.time())
    # With a cold file cache or a loaded machine the Editor goes on working
    # after the first screen (it loads the images of the document): wait
    # until it has been idle for a while, for up to 60 seconds, so that the
    # runs start from the same screen.
    busy_until_idle(p, time.time(), 60.0)
    wait_no_jobs(p)
    busy_until_idle(p, time.time(), 60.0)
    settle(time.time(), 60.0)
    p.jobs_ready = jobs(p)
    prewalk(p, w)
    if depth:
        subprocess.run(['xdotool', 'key', '--window', w, '--delay', '0',
            '--repeat', str(depth), 'Next'], env=env, capture_output=True)
        settle(time.time(), 120.0)
        busy_until_idle(p, time.time(), 120.0)
        busy_until_idle(p, time.time())
    return p, w


def stop(p):
    p.terminate()
    try:
        p.wait(5)
    except subprocess.TimeoutExpired:
        p.kill()


def cpu_ms(p):
    """The CPU time (user and system) that the process has used, in
    milliseconds, from /proc: a measure of the work of the Editor that does
    not depend on the speed of the X server."""
    try:
        f = open('/proc/%d/stat' % p.pid).read().rsplit(')', 1)[1].split()
        return (int(f[11]) + int(f[12])) * 1000.0 / os.sysconf('SC_CLK_TCK')
    except (OSError, IndexError, ValueError):
        return 0.0


def prewalk(p, w):
    """Walk down the document with the keys of a slow run and back up with
    the same number of keys, waiting for the jobs that make the pictures of
    every screen on the way, so that the Editor has all the pictures that the
    keys of a run pass over.  The layout of a table depends on whether the
    pictures of its cells exist, so keys that are done before a picture
    exists pass over other lines than the same keys after it: a burst, which
    draws only some screens and so asks for few pictures, would end on
    another line than the slow keys, which draw every screen.  Every Editor
    of a run does this walk, so that all of them pass over the same layout."""
    top = screen()
    for _ in range(nkeys):
        keys(w, 'Down')
        time.sleep(0.01)
    wait_no_jobs(p)
    busy_until_idle(p, time.time())
    for _ in range(nkeys):
        keys(w, 'Up')
        time.sleep(0.01)
    wait_no_jobs(p)
    busy_until_idle(p, time.time())
    settle(time.time(), 60.0)
    if plain(screen()) != plain(top):
        print('xlag: the walk down and up did not end on the first screen')


def jobs(p):
    """The number of processes below the Editor p: the jobs that make its
    pictures (latex and the programs it starts) run there.  The CPU time of
    the Editor itself does not show them."""
    parent = {}
    for d in os.listdir('/proc'):
        if not d.isdigit():
            continue
        try:
            f = open('/proc/%s/stat' % d).read().rsplit(')', 1)[1].split()
            # A process that has ended and is not yet waited for (state Z)
            # is no job: the Editor leaves such children of lpstat.
            if f[0] != 'Z':
                parent[int(d)] = int(f[1])
        except (OSError, IndexError, ValueError):
            pass
    below = {p.pid}; grew = True
    while grew:
        grew = False
        for c, pp in parent.items():
            if pp in below and c not in below:
                below.add(c); grew = True
    return len(below) - 1


def wait_no_jobs(p, limit=120.0):
    """Wait until no picture job has been running for STABLE_MS
    milliseconds, for at most limit seconds, and then until the screen has
    been quiet.  The layout of a table depends on whether the pictures of its
    cells have arrived, so a key that is sent before they have arrived is
    done on another layout than the same key after: the final screens of two
    runs can differ with no event lost.  The limit is 120 seconds because a
    loaded machine makes the 50 pictures of the document in a minute."""
    end = time.time() + limit; quiet_since = time.time()
    while time.time() < end:
        time.sleep(0.05)
        if jobs(p) != 0:
            quiet_since = time.time()
        elif (time.time() - quiet_since) * 1000 >= STABLE_MS:
            break
    settle(time.time(), 60.0)


def busy_until_idle(p, t0, limit=180.0):
    """Wait until the process has used no CPU for STABLE_MS milliseconds
    and return the time from t0 to the last moment it was busy, in
    milliseconds.  The screen may stay the same while the Editor is working
    through a queue of key events: the CPU shows it."""
    last = cpu_ms(p); last_busy = time.time(); end = t0 + limit
    while time.time() < end:
        time.sleep(0.05)
        c = cpu_ms(p)
        if c - last > 2.0:
            last_busy = time.time()
        last = c
        if (time.time() - last_busy) * 1000 >= STABLE_MS:
            break
    return (last_busy - t0) * 1000.0


def keys(w, *a):
    subprocess.run(['xdotool', 'key', '--window', w] + list(a), env=env,
        capture_output=True)


def run_once(doc, depth=0):
    """One run: the time of one move, the burst of Down events with the idle
    time after it, and the slow reference.  Each part has an Editor of its
    own, which starts at the top of the document, so that the final screens
    of the burst and of the slow keys can be compared."""
    p, w = editor(doc, depth)
    try:
        before = screen()
        t0 = time.time(); keys(w, 'Down')
        one_ms, s1 = settle(t0)
        one_ms = max(one_ms, busy_until_idle(p, t0))
        moved = s1 != before
    finally:
        stop(p)
    p, w = editor(doc, depth)
    try:
        start = screen()
        burst_jobs = p.jobs_ready
        draws0 = draws_of(start)
        c0 = cpu_ms(p); x0 = cpu_ms(xvfb)
        t0 = time.time()
        keys(w, '--delay', '0', '--repeat', str(nkeys), 'Down')
        sent_ms = (time.time() - t0) * 1000.0
        idle_ms, burst = settle(t0)
        idle_ms = max(idle_ms, busy_until_idle(p, t0))
        # The Editor may draw after the screen was quiet for a while (a loaded
        # machine): the screen to compare is the one after it went idle.
        burst = screen()
        draws = draws_of(screen()) - draws0
        cpu_per_key = (cpu_ms(p) - c0) / nkeys
        xcpu_per_key = (cpu_ms(xvfb) - x0) / nkeys
    finally:
        stop(p)
    p, w = editor(doc, depth)
    slow_jobs = p.jobs_ready
    try:
        for _ in range(nkeys):
            keys(w, 'Down')
            time.sleep(0.01)
        settle(time.time(), 120.0)
        busy_until_idle(p, time.time())
        slow = screen()
    finally:
        stop(p)
    # The frozen burst: the Editor is stopped while the keys are sent, so that
    # all of them are waiting when it goes on.  Its drawings are counted.
    p, w = editor(doc, depth)
    try:
        draws0 = draws_of(screen())
        os.kill(p.pid, signal.SIGSTOP)
        keys(w, '--delay', '0', '--repeat', str(nkeys), 'Down')
        time.sleep(0.3)
        t0 = time.time()
        os.kill(p.pid, signal.SIGCONT)
        frozen_ms, frozen = settle(t0)
        frozen_ms = max(frozen_ms, busy_until_idle(p, t0))
        frozen = screen()
        frozen_draws = draws_of(screen()) - draws0
    finally:
        try:
            os.kill(p.pid, signal.SIGCONT)
        except OSError:
            pass
        stop(p)
    if os.environ.get('XLAG_TRACE', '') != '':
        def cur(s):
            m = re.search(r'\x01cursor (\d+) (\d+)', s or '')
            rows = plain(s).split('\n')
            return rows[int(m.group(2))].strip()[:40] if m else None
        print('TRACE burst=%r slow=%r frozen=%r top_b=%r top_s=%r' % (
            cur(burst), cur(slow), cur(frozen),
            plain(burst).split('\n')[2][:30].strip(),
            plain(slow).split('\n')[2][:30].strip()))
    if not same_place(burst, slow):
        for i, (x, y) in enumerate(zip(plain(burst).split('\n'),
                                       plain(slow).split('\n'))):
            if x != y:
                print('xlag: first different row %d:\n  burst: %s\n  slow:  %s'
                    % (i, x, y))
                break
        print('xlag: picture jobs still running when the keys were sent: '
              'burst %d slow %d' % (burst_jobs, slow_jobs))
        print('xlag: the burst screen:\n%s' % plain(burst))
        print('xlag: the slow screen:\n%s' % plain(slow))
    if not same_place(frozen, slow):
        for i, (x, y) in enumerate(zip(plain(frozen).split('\n'),
                                       plain(slow).split('\n'))):
            if x != y:
                print('xlag: frozen differs at row %d:\n  frozen: %s\n'
                      '  slow:   %s' % (i, x, y))
                break
        print('xlag: the frozen screen:\n%s' % plain(frozen))
        print('xlag: the slow screen:\n%s' % plain(slow))
    return dict(moved=moved, same=same_place(burst, slow),
        frozen_same=same_place(frozen, slow),
        frozen_draws=frozen_draws, frozen_ms=frozen_ms,
        burst_moved=plain(burst) != plain(start),
        idle_ms=max(0.0, idle_ms - sent_ms), sent_ms=sent_ms, one_ms=one_ms,
        cpu_key=cpu_per_key, xcpu_key=xcpu_per_key, draws=draws,
        burst_status=burst.split('\n')[0][:70],
        slow_status=slow.split('\n')[0][:70])


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra))); fails += 0 if cond else 1


try:
    for doc in docs:
        if doc == 'LONGBLOCK':
            doc = os.path.join(tmp, 'longblock.doc')
            make_long_block(doc, int(os.environ.get('XLAG_CHARS', '20000')))
        if not os.path.exists(doc):
            print('SKIP: no document %s' % doc); continue
        for depth in depths:
            rs = [run_once(doc, depth) for _ in range(nruns)]
            idle = statistics.median(r['idle_ms'] for r in rs)
            one = statistics.median(r['one_ms'] for r in rs)
            sent = statistics.median(r['sent_ms'] for r in rs)
            cpu = statistics.median(r['cpu_key'] for r in rs)
            draws = statistics.median(r['draws'] for r in rs)
            xcpu = statistics.median(r['xcpu_key'] for r in rs)
            name = os.path.basename(doc)
            if measure:
                print('xlag %s depth=%d: idle_ms=%d cpu_ms_per_key=%.2f '
                    'xserver_cpu_ms_per_key=%.2f one_move_ms=%d '
                    'sent_ms=%d live_draws=%d frozen_draws=%d same=%s | '
                    '%s | %s' % (name, depth, idle, cpu, xcpu, one, sent,
                    draws, max(r['frozen_draws'] for r in rs),
                    all(r['same'] for r in rs), rs[0]['burst_status'],
                    rs[0]['slow_status']))
                continue
            check('xlag: the screen is idle within %d ms after %d Down events '
                'on %s (median %d ms)' % (IDLE_BOUND_MS, nkeys, name, idle),
                idle <= IDLE_BOUND_MS, rs)
            check('xlag: the burst on %s ends on the screen of the slow keys '
                '(no event lost or doubled)' % name,
                all(r['same'] for r in rs), rs)
            check('xlag: %d keys that wait together on %s are drawn at most '
                '%d times (drawn %d): a drawing that the next key replaces '
                'is left out' % (nkeys, name, MAX_DRAWS,
                max(r['frozen_draws'] for r in rs)),
                all(0 < r['frozen_draws'] <= MAX_DRAWS for r in rs), rs)
            check('xlag: the keys that waited together on %s end on the '
                'screen of the slow keys' % name,
                all(r['frozen_same'] for r in rs), rs)
            check('xlag: the keys move the cursor on %s' % name,
                all(r['moved'] and r['burst_moved'] for r in rs), rs)
finally:
    xvfb.terminate()
    try:
        xvfb.wait(5)
    except subprocess.TimeoutExpired:
        xvfb.kill()
    shutil.rmtree(tmp, ignore_errors=True)
if not measure:
    print('xlag: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
    sys.exit(1 if fails else 0)
