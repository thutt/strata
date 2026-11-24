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

"""Tests of F12, Print, Preview (the LaTeX preview): stred runs in a pty, a
fake viewer (STRATA_DVIVIEW) records what it is given, and TMPDIR is a
private directory that must be empty at the end of every run.

The real latex is used for the normal runs; a fake latex (tests/fakelatex.sh,
first in PATH) makes the failures and the long runs that can be cancelled.
"""
import sys as _s; _s.dont_write_bytecode = True
import os, pty, shutil, signal, stat, struct, subprocess, sys, tempfile, time
import fcntl, termios, select
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
bdir = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(bdir, 'term', 'stred'))
sample = os.environ.get('DOCSRC', os.path.join(root, 'tests', 'samples'))
twain = os.environ.get('TWAIN', '/home/claude/lotus-manuscript/sample.docs/'
    'twain.doc')
helpf = os.environ.get('STRATA_HELP', os.path.join(
    os.environ.get('STRATA_BUILD_DIR', ''), 'strata.hlp'))
if not shutil.which('latex'):
    print('SKIP: latex is not installed')
    sys.exit(0)
fails = 0
tmp = tempfile.mkdtemp(prefix='pvt')
fakeview = os.path.join(here, 'fakeview.sh')


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def newdir(name):
    d = os.path.join(tmp, name)
    os.makedirs(d)
    return d


def read(path, mode='rb'):
    try:
        with open(path, mode) as f:
            return f.read()
    except OSError:
        return b'' if 'b' in mode else ''


def fake_bin():
    """A directory with the fake latex under the name latex."""
    d = newdir('fakebin%d' % len(os.listdir(tmp)))
    # A copy that only the user can write: a program that others can write
    # is not started.
    shutil.copy(os.path.join(here, 'fakelatex.sh'), os.path.join(d, 'latex'))
    os.chmod(os.path.join(d, 'latex'), 0o755)
    return d


def setup(name, **extra):
    """The environment of a run: a private TMPDIR and a recording directory."""
    run = newdir(name)
    t = os.path.join(run, 'tmp')
    rec = os.path.join(run, 'rec')
    os.makedirs(t)
    os.makedirs(rec)
    env = {'TMPDIR': t, 'PVREC': rec, 'STRATA_DVIVIEW': fakeview,
        'STRATA_HOME': os.path.join(run, 'home')}
    env.update(extra)
    return run, t, rec, env


def apply_env(env):
    # The programs are not looked for in PATH: the folders of the PATH of
    # the test are the folders of the programs (a debug build reads them from
    # STRATA_PROGRAM_DIRS), so a fake program first in PATH is still run.
    env = dict(env)
    if 'PATH' in env and 'STRATA_PROGRAM_DIRS' not in env:
        env['STRATA_PROGRAM_DIRS'] = env['PATH']
    saved = {}
    for k, v in env.items():
        saved[k] = os.environ.get(k)
        if v is None:
            os.environ.pop(k, None)
        else:
            os.environ[k] = v
    return saved


def restore_env(saved):
    for k, v in saved.items():
        if v is None:
            os.environ.pop(k, None)
        else:
            os.environ[k] = v


def fresh(run, src, name='work.doc'):
    path = os.path.join(run, name)
    shutil.copy(src, path)
    return path


def session(env, path, keys, cols=80):
    saved = apply_env(env)
    try:
        return tui.session([stred, '-H', helpf, path], keys, cols=cols)
    finally:
        restore_env(saved)


DOCUMENT = ['F12', 'text:P', 'text:P', 'ENTER', 'INS']
BLOCK = ['F12', 'text:P', 'text:P', 'RIGHT', 'ENTER']
SECTION = ['F12', 'text:P', 'text:P', 'RIGHT', 'RIGHT', 'ENTER']
cns = os.path.join(sample, 'plan-title-table.doc')
sdoc = os.path.join(here, 'data', 'sections_se.doc')


def left_over(t):
    return sorted(os.listdir(t))


# ---- strprint --progress (what stred reads through the pipe) -------------
strprint = os.path.join(os.path.dirname(stred), 'strprint')
pvest = os.path.join(os.path.dirname(stred), 'pvest')


def run_strprint(doc, env_extra=None):
    """Run strprint --progress; returns (status, output, dvi exists)."""
    run = newdir('sp%d' % len(os.listdir(tmp)))
    t = os.path.join(run, 'tmp')
    os.makedirs(t)
    env = dict(os.environ, TMPDIR=t)
    env.update(env_extra or {})
    out = os.path.join(run, 'o.dvi')
    # The documents of these tests have errors (strprint says so with status
    # 4); the Error Log is left out here, it has its own tests (errlog.py).
    p = subprocess.run([strprint, doc, '--dvi', '-o', out, '--progress',
        '--no-error-log'],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env)
    return (p.returncode, p.stdout.decode('latin-1'), os.path.exists(out),
        os.listdir(t))


code, text, have, left = run_strprint(os.path.join(here, 'data',
    'print_features.doc'))
passes = [l for l in text.split('\n') if l.startswith('@@strprint pass')]
check('strprint: a document with page references runs a second pass',
    code in (0, 4) and have and passes[-1] == '@@strprint pass 2', passes)
check('strprint: pictures no longer make the first pass fail',
    passes[:2] == ['@@strprint pass 1', '@@strprint pass 2'], passes)
check('strprint: the stages and position marks are in the output',
    text.startswith('\n@@strprint stage writing') and
    '@@strprint pos 0\n' in text, text[:80])
check('strprint: its own directory is removed', left == [], left)

code2, text2, have2, left2 = run_strprint(os.path.join(here, 'data',
    'print_badpic.doc'))
passes2 = [l for l in text2.split('\n') if l.startswith('@@strprint pass')]
check('strprint: a pass that failed because of a piece starts again at 1',
    code2 in (0, 4) and have2 and passes2[:2] ==
    ['@@strprint pass 1', '@@strprint pass 1'], (code2, passes2))

# A PNG picture of the DVI route: latex needs its size in a file, which
# strprint makes with extractbb, so the picture is in the DVI file.
run = tempfile.mkdtemp(prefix='pic')
dvi = os.path.join(run, 'o.dvi')
p = subprocess.run([strprint, os.path.join(here, 'data', 'print_panels.doc'),
    '--dvi', '-o', dvi], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    env=dict(os.environ, TMPDIR=run))
check('strprint: a PNG picture is in the DVI file (extractbb)',
    p.returncode == 0 and b'pdf:image' in read(dvi), p.returncode)
check('strprint: the picture is copied next to the DVI file',
    any(n.startswith('pic') and n.endswith('.png') for n in os.listdir(run)),
    os.listdir(run))

# A file of the same name that is different is not replaced.
run2 = tempfile.mkdtemp(prefix='pic')
mine = os.path.join(run2, 'pic1.png')
open(mine, 'wb').write(b'not a picture of strata')
p = subprocess.run([strprint, os.path.join(here, 'data', 'print_panels.doc'),
    '--dvi', '-o', os.path.join(run2, 'o.dvi')], stdout=subprocess.DEVNULL,
    stderr=subprocess.PIPE, env=dict(os.environ, TMPDIR=run2))
check('strprint: a different file of the same name is left alone',
    read(mine) == b'not a picture of strata' and b'differs' in p.stderr,
    p.stderr[-200:])
rd = subprocess.run([pvest, '-s'], input=text.encode('latin-1'),
    stdout=subprocess.PIPE).stdout.decode().split('\n')
check('strprint: the reader counts the pages of the last pass',
    # Four pages of text and the list of the pictures that the document
    # has captions for (its Picture markers turn that list on).
    'end 5' in rd, rd[-3:])

fb0 = fake_bin()
dvi0 = os.path.join(tmp, 'template0.dvi')
open(dvi0, 'wb').write(b'\xf7\x02' + b'\x00' * 200)
penv = {'PATH': fb0 + ':' + os.environ.get('PATH', ''), 'PVDVI': dvi0,
        'STRATA_PROGRAM_DIRS': fb0 + ':' + os.environ.get('PATH', '')}
code, text, have, left = run_strprint(cns, dict(penv, PVFAKE='error'))
check('strprint: a failed latex gives status 1, no DVI and the first error',
    code == 1 and not have and '@@strprint error ! Fake error.' in text
    and '@@strprint error2 l.1 the fake line' in text, (code, text))
check('strprint: nothing is left after a failure', left == [], left)
code, text, have, left = run_strprint(cns, dict(penv, PVFAKE='errordvi'))
check('strprint: a DVI made in spite of errors gives status 3 and the file',
    code == 3 and have and '@@strprint error ! Fake error.' in text,
    (code, text))

# ---- normal runs: the three options -------------------------------------
run, t, rec, env = setup('doc', PVHOLD='1')
path = fresh(run, cns)
before = read(path)
# The viewer holds for one second after LaTeX ends: wait long enough for both
# (three waits were not enough when LaTeX took a second and a half).
d = session(env, path, DOCUMENT + ['wait'] * 8 + ['show'])
part = read(os.path.join(rec, 'part.doc'))
check('document: the viewer was started on a DVI file',
    read(os.path.join(rec, 'arg'), 'r').strip().endswith('/preview.dvi'),
    read(os.path.join(rec, 'arg'), 'r'))
check('document: the DVI file existed while the viewer ran',
    read(os.path.join(rec, 'exists'), 'r').strip() == 'yes')
copy = read(os.path.join(rec, 'copy.dvi'))
check('document: the file is a DVI file (it begins with 247, 2)',
    copy[:2] == b'\xf7\x02' and len(copy) > 100, copy[:20])
check('document: the temporary directory is private (mode 700)',
    read(os.path.join(rec, 'mode'), 'r').strip() == '700')
check('document: the whole document was written to the part file',
    b'Orchard Plan' in part and b'Draft for Review' in part)
check('document: the files are deleted afterwards', left_over(t) == [],
    left_over(t))
check('document: the message line shows it is done',
    'Preview done' in d[0], d[0])
check('document: the file of the user is not touched', read(path) == before)
check('document: the directory of the viewer is gone',
    not os.path.exists(os.path.dirname(read(os.path.join(rec, 'arg'),
    'r').strip())))

run, t, rec, env = setup('block')
path = fresh(run, cns)
d = session(env, path, BLOCK + ['wait', 'wait', 'show'])
part = read(os.path.join(rec, 'part.doc'))
check('block: only the block of the cursor is previewed',
    b'Orchard Plan' in part and b'Draft for Review' not in part
    and b'Neighbours' not in part, part[:200])
check('block: the viewer got a DVI file', read(os.path.join(rec, 'exists'),
    'r').strip() == 'yes')
check('block: nothing is left', left_over(t) == [], left_over(t))

run, t, rec, env = setup('section')
path = fresh(run, sdoc)
d = session(env, path, ['DOWN', 'DOWN'] + SECTION + ['wait', 'wait', 'show'])
part = read(os.path.join(rec, 'part.doc'))
check('section: only the section of the cursor is previewed',
    b'Purpose body.' in part and b'Prerequisites body.' not in part,
    part[:300])
check('section: the viewer got a DVI file', read(os.path.join(rec, 'exists'),
    'r').strip() == 'yes')
check('section: nothing is left', left_over(t) == [], left_over(t))

run, t, rec, env = setup('unsaved')
path = fresh(run, cns)
before = read(path)
d = session(env, path, ['text:ZZUNSAVEDZZ'] + DOCUMENT + ['wait', 'wait',
    'show'])
part = read(os.path.join(rec, 'part.doc'))
check('unsaved: the text that is not saved is previewed',
    b'ZZUNSAVEDZZ' in part, part[:200])
check('unsaved: the file on the disk is not changed', read(path) == before)
check('unsaved: nothing is left', left_over(t) == [], left_over(t))

run, t, rec, env = setup('range')
path = fresh(run, os.path.join(here, 'data', 'sort_sections.doc'))
d = session(env, path, ['F12', 'text:P', 'text:P', 'ENTER', 'DOWN', 'text:1.2',
    'DOWN', 'text:1.2', 'INS', 'wait', 'wait', 'show'])
part = read(os.path.join(rec, 'part.doc'))
check('document: a range of sections previews those sections only',
    b'Apple body' in part and b'cherry body' not in part, part[:300])

# ---- the viewer: missing, no display -------------------------------------
run, t, rec, env = setup('noviewer', STRATA_DVIVIEW='/no/such/dviview')
path = fresh(run, cns)
d = session(env, path, DOCUMENT + ['wait', 'show'])
check('no viewer: the message line shows so', 'needs the viewer dviview'
    in d[0], d[0])
check('no viewer: no file is made', left_over(t) == [], left_over(t))

run, t, rec, env = setup('nodisplay', STRATA_DVIVIEW=None, DISPLAY=None)
path = fresh(run, cns)
d = session(env, path, DOCUMENT + ['wait', 'show'])
check('no display: the message line shows so', 'DISPLAY is not set' in d[0]
    or 'needs the viewer' in d[0], d[0])
check('no display: no file is made', left_over(t) == [], left_over(t))

run, t, rec, env = setup('viewerfail', PVSTATUS='3')
path = fresh(run, cns)
d = session(env, path, DOCUMENT + ['wait'] * 8 + ['show'])
check('a viewer that ends with a status still cleans up',
    left_over(t) == [], left_over(t))

# ---- what the viewer reports (the viewer never writes to the console) ----
run, t, rec, env = setup('vnote', PVNOTE="special 'foo' is not supported "
    "and is ignored: first on page 1")
path = fresh(run, cns)
d = session(env, path, DOCUMENT + ['wait'] * 8 + ['show'])
check('viewer note: the message line shows that the viewer could not show '
    'a thing', 'could not show 1 thing' in d[0] and "special 'foo'" in d[0],
    d[0])
check('viewer note: the preview is still done', 'Preview done' in d[0], d[0])
check('viewer note: nothing is left', left_over(t) == [], left_over(t))
check('viewer note: without a run log the line has no pointer to one',
    'details in the log' not in d[0], d[0])

# With a run log (a socket that the Editor can connect to, as str makes it)
# the message line ends with the pointer to the log.
POINTER = '; details in the log of the run'


def abstract_name(prefix):
    """The name of a socket in the abstract namespace of Linux: it is not a
    file, so the length of TMPDIR does not matter (the name of a Unix
    socket that is a file has at most 107 characters).  The programs are
    given the name with an @ in front, as log.c reads it; bind takes the
    same name with a zero byte in front."""
    return '%s%d-%d' % (prefix, os.getpid(), int(time.time() * 1000) % 100000)


def bind_abstract(sock, name):
    sock.bind('\0' + name)
    return '@' + name


def log_socket(name):
    """A datagram socket like the collector of str; returns (socket, path)."""
    import socket
    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    path = bind_abstract(s, abstract_name('pv%s-' % name))
    return s, path


sk, sp = log_socket('l')
run, t, rec, env = setup('vnotelog', PVNOTE="special 'foo' is not supported "
    "and is ignored: first on page 1", STRATA_LOG_SOCKET=sp)
path = fresh(run, cns)
d = session(env, path, DOCUMENT + ['wait'] * 8 + ['show'])
check('viewer note with a run log: the line ends with the pointer to the log',
    'could not show 1 thing' in d[0] and d[0].rstrip().endswith(POINTER),
    d[0])
check('viewer note with a run log: the middle is cut, not the pointer',
    "special 'foo'" not in d[0] or '...' in d[0], d[0])
sk.close()

sk, sp = log_socket('l')
run, t, rec, env = setup('vfaillog', PVFAIL='cannot open the X display: '
    'DISPLAY is :9', PVSTATUS='1', STRATA_LOG_SOCKET=sp)
path = fresh(run, cns)
d = session(env, path, DOCUMENT + ['wait'] * 8 + ['show'])
check('viewer failure with a run log: the line says why and ends with the '
    'pointer', 'the viewer failed' in d[0] and d[0].rstrip().endswith(
    POINTER), d[0])
sk.close()

sk, sp = log_socket('l')
run, t, rec, env = setup('vfailnarrow', PVFAIL='cannot open the X display: '
    'DISPLAY is :9', PVSTATUS='1', STRATA_LOG_SOCKET=sp)
path = fresh(run, cns)
d = session(env, path, DOCUMENT + ['wait'] * 8 + ['show'], cols=40)
check('a narrow screen (40 columns) keeps the pointer to the log whole',
    d[0].rstrip().endswith(POINTER), d[0])
sk.close()

run, t, rec, env = setup('vfail', PVFAIL='cannot open the X display: DISPLAY '
    'is :9', PVSTATUS='1')
path = fresh(run, cns)
d = session(env, path, DOCUMENT + ['wait'] * 8 + ['show'])
check('viewer failure: the message line shows why the viewer failed',
    'the viewer failed' in d[0] and 'cannot open the X display' in d[0],
    d[0])
check('viewer failure: nothing is left', left_over(t) == [], left_over(t))

# ---- errors of latex -----------------------------------------------------
fb = fake_bin()
dvi = os.path.join(tmp, 'template.dvi')
open(dvi, 'wb').write(b'\xf7\x02' + b'\x00' * 200)
path_env = fb + ':' + os.environ.get('PATH', '')
run, t, rec, env = setup('failed', PVFAKE='error', PATH=path_env, PVDVI=dvi)
path = fresh(run, cns)
d = session(env, path, DOCUMENT + ['wait'] * 8 + ['show'])
check('failed: the first error line and the line after it are shown',
    '! Fake error.' in d[0] and 'l.1 the fake line' in d[0], d[0])
check('failed: the viewer was not started',
    not os.path.exists(os.path.join(rec, 'arg')))
check('failed: nothing is kept', left_over(t) == [], left_over(t))

run, t, rec, env = setup('errdvi', PVFAKE='errordvi', PATH=path_env,
    PVDVI=dvi)
path = fresh(run, cns)
d = session(env, path, DOCUMENT + ['wait', 'wait', 'wait', 'show'])
check('errors with a DVI: the viewer is started all the same',
    read(os.path.join(rec, 'exists'), 'r').strip() == 'yes')
check('errors with a DVI: the message line names the error',
    '! Fake error.' in d[0] and 'in spite of errors' in d[0], d[0])
check('errors with a DVI: nothing is kept', left_over(t) == [], left_over(t))

# ---- the wait panel ------------------------------------------------------
run, t, rec, env = setup('slow', PVFAKE='slow', PATH=path_env, PVDVI=dvi,
    PVPAGES='30', PVHOLD='1')
path = fresh(run, cns)
d = session(env, path, DOCUMENT + ['wait', 'show', 'wait', 'wait', 'wait',
    'wait', 'wait', 'show'])
check('panel: it shows Preview, the stage, the page and a gauge',
    'Preview' in d[0] and 'Running LaTeX (pass 1)' in d[0]
    and 'Page ' in d[0] and ' of about ' in d[0] and '%' in d[0], d[0])
check('panel: the viewer starts when the pages are done',
    read(os.path.join(rec, 'exists'), 'r').strip() == 'yes')
check('panel: nothing is left', left_over(t) == [], left_over(t))

# ---- cancel with Esc -----------------------------------------------------
run, t, rec, env = setup('cancel', PVFAKE='slow', PATH=path_env, PVDVI=dvi,
    PVPAGES='300', PVHOLD='1')
path = fresh(run, cns)
d = session(env, path, DOCUMENT + ['wait', 'show', 'ESC', 'wait', 'show'])
check('cancel: the panel was up while latex ran',
    'Running LaTeX' in d[0] and 'Esc cancels' in d[0], d[0])
check('cancel: Esc returns to the editor with a message',
    'Preview cancelled' in d[1] and 'Running LaTeX' not in d[1], d[1])
check('cancel: the viewer was not started',
    not os.path.exists(os.path.join(rec, 'arg')))
check('cancel: nothing is left', left_over(t) == [], left_over(t))
procs = subprocess.run(['pgrep', '-f', t], capture_output=True, text=True)
check('cancel: no process of the run is left', procs.stdout.strip() == '',
    procs.stdout)

if os.path.exists(twain):
    run, t, rec, env = setup('cancel2')
    path = fresh(run, twain, 'twain.doc')
    d = session(env, path, ['wait', 'wait', 'wait'] + DOCUMENT + ['wait',
        'wait', 'wait', 'wait', 'wait', 'wait', 'wait', 'show', 'ESC', 'wait',
        'wait', 'show'])
    # A slow build (asan) may still be writing: then the panel is enough.
    slow_build = 'asan' in stred
    check('cancel: a big document shows pages while latex runs',
        ('Running LaTeX (pass 1)' in d[0] and 'of about' in d[0]) or
        (slow_build and 'Esc cancels' in d[0]), d[0])
    check('cancel: a big document is cancelled at once',
        'Preview cancelled' in d[1], d[1])
    check('cancel: nothing is left of it', left_over(t) == [], left_over(t))
    procs = subprocess.run(['pgrep', '-f', t], capture_output=True, text=True)
    check('cancel: no process of the big run is left',
        procs.stdout.strip() == '', procs.stdout)


# ---- signals -------------------------------------------------------------
def signal_test(name, sig, hold, when):
    """Start a preview, send sig while it is in the stage 'when' (latex or
    viewer) and look at what is left."""
    run, t, rec, env = setup(name, PVFAKE='slow', PATH=path_env, PVDVI=dvi,
        PVPAGES='300' if when == 'latex' else '3', PVHOLD=hold)
    path = fresh(run, cns)
    saved = apply_env(env)
    try:
        os.environ['TERM'] = 'xterm'
        pid, fd = pty.fork()
        if pid == 0:
            os.execv(stred, [stred, '-H', helpf, path])
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 25, 80, 0, 0))
    finally:
        restore_env(saved)
    def pump(sec):
        end = time.time() + sec
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    os.read(fd, 65536)
                except OSError:
                    return
    pump(0.6)
    for k in ['\x1b[24~', 'P', 'P', '\r', '\x1b[2~']:
        os.write(fd, k.encode())
        pump(0.15)
    if when == 'viewer':
        for _ in range(60):
            pump(0.1)
            if os.path.exists(os.path.join(rec, 'pid')):
                break
    else:
        pump(1.0)
    vpid = int(read(os.path.join(rec, 'pid'), 'r') or 0)
    check('%s: the preview is running (%s)' % (name, when),
        left_over(t) != [] and (when == 'latex' or vpid > 0), left_over(t))
    os.kill(pid, sig)
    done = None
    end = time.time() + 5
    while time.time() < end:
        r, st = os.waitpid(pid, os.WNOHANG)
        if r:
            done = st
            break
        pump(0.05)
    if done is None:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
    check('%s: stred ends by the signal' % name, done is not None
        and os.WIFSIGNALED(done) and os.WTERMSIG(done) == sig, done)
    check('%s: nothing is left of the run' % name, left_over(t) == [],
        left_over(t))
    time.sleep(0.2)
    gone = True
    if vpid > 0:
        try:
            os.kill(vpid, 0)
            gone = False
        except OSError:
            pass
    check('%s: the viewer was stopped' % name, gone)
    procs = subprocess.run(['pgrep', '-f', t], capture_output=True, text=True)
    check('%s: no process of the run is left' % name,
        procs.stdout.strip() == '', procs.stdout)
    os.close(fd)


for nm, sg in (('sigterm', signal.SIGTERM), ('sighup', signal.SIGHUP),
        ('sigint', signal.SIGINT)):
    signal_test(nm + '-latex', sg, '0', 'latex')
signal_test('sigterm-viewer', signal.SIGTERM, '60', 'viewer')
signal_test('sighup-viewer', signal.SIGHUP, '60', 'viewer')

# ---- the real viewer under Xvfb (a plain terminal and an X display) --------
def real_viewer_test():
    if not (shutil.which('Xvfb') and shutil.which('xdotool')) or not \
            os.path.exists(os.path.join(os.path.dirname(stred), 'dviview')):
        print('SKIP: real viewer (Xvfb, xdotool or dviview is missing)')
        return
    dfd_r, dfd_w = os.pipe()
    xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(dfd_w), '-screen', '0',
        '1280x800x24'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        pass_fds=(dfd_w,))
    os.close(dfd_w)
    num = b''
    while not num.endswith(b'\n'):
        chunk = os.read(dfd_r, 16)
        if not chunk:
            check('real viewer: Xvfb starts', False)
            return
        num += chunk
    os.close(dfd_r)
    disp = ':' + num.decode().strip()
    try:
        run, t, rec, env = setup('real', STRATA_DVIVIEW=None, DISPLAY=disp)
        path = fresh(run, cns)
        saved = apply_env(env)
        try:
            os.environ['TERM'] = 'xterm'
            pid, fd = pty.fork()
            if pid == 0:
                os.execv(stred, [stred, '-H', helpf, path])
            fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 25, 80, 0,
                0))
        finally:
            restore_env(saved)
        scr = tui.Screen(25, 80)
        def pump(sec):
            end = time.time() + sec
            while time.time() < end:
                r, _, _ = select.select([fd], [], [], 0.05)
                if r:
                    try:
                        scr.feed(os.read(fd, 65536))
                    except OSError:
                        return
        pump(0.6)
        for k in ['\x1b[24~', 'P', 'P', '\r', '\x1b[2~']:
            os.write(fd, k.encode())
            pump(0.15)
        xenv = dict(os.environ, DISPLAY=disp)
        win = []
        for _ in range(100):
            pump(0.1)
            win = subprocess.run(['xdotool', 'search', '--name',
                'preview.dvi'], env=xenv, capture_output=True,
                text=True).stdout.split()
            if win:
                break
        name = subprocess.run(['xdotool', 'getwindowname', win[0]],
            env=xenv, capture_output=True, text=True).stdout if win else ''
        check('real viewer: dviview opens a window on the display, with the '
            'pages of the DVI file', bool(win) and 'preview.dvi' in name, name)
        check('real viewer: the files exist while it is open',
            any(n.startswith('strpv.') for n in os.listdir(t)),
            str(os.listdir(t)))
        if win:
            subprocess.run(['xdotool', 'key', '--window', win[0], 'q'],
                env=xenv, capture_output=True)
        pump(2.0)
        check('real viewer: closing the window returns to the editor',
            'Preview done' in scr.dump(), scr.dump())
        check('real viewer: nothing is left', left_over(t) == [],
            left_over(t))
        os.kill(pid, signal.SIGTERM)
        os.waitpid(pid, 0)
        os.close(fd)
    finally:
        xvfb.terminate()
        xvfb.wait()


# ---- the arguments that the Editor gives the viewer ----------------------
# The list ends after the variables of the run.  An unterminated list made
# execv fail with EFAULT and the Editor report that it could not run the
# viewer, on about one run in two, whatever the load of the machine.
def viewer_args(name, extra):
    run, t, rec, env = setup(name)
    path = fresh(run, cns)
    saved = apply_env(env)
    try:
        d = tui.session([stred, '-H', helpf] + extra + [path],
                        DOCUMENT + ['wait', 'wait', 'wait', 'show'])
    finally:
        restore_env(saved)
    args = read(os.path.join(rec, 'args'), 'r').split('\n')
    argc = read(os.path.join(rec, 'argc'), 'r').strip()
    return d, args[:-1] if args[-1:] == [''] else args, argc


d, args, argc = viewer_args('argsnone', [])
check('viewer arguments: with no variable they are the file, --report and '
      'its file', argc == '3' and len(args) == 3 and args[1] == '--report',
      (argc, args))
check('viewer arguments: with no variable the viewer is started',
      'cannot run the viewer' not in d[0], d[0])

d, args, argc = viewer_args('argsvars', ['--var', 'ONE', '1', '--special',
                                         '.STRATA_TWO', '2'])
check('viewer arguments: the variables of the run follow the report option '
      'and the list ends after them',
      argc == '9' and args[1:2] == ['--report'] and
      args[3:] == ['--var', 'ONE', '1', '--special', '.STRATA_TWO', '2'],
      (argc, args))
check('viewer arguments: with variables the viewer is started',
      'cannot run the viewer' not in d[0], d[0])


# ---- stred --preview: the preview mode of the Editor ---------------------
def preview_only(env, path, keys):
    saved = apply_env(env)
    try:
        return tui.session([stred, '-H', helpf, '--preview', path], keys)
    finally:
        restore_env(saved)


run, t, rec, env = setup('only')
path = fresh(run, os.path.join(here, 'data', 'sort_sections.doc'))
d = preview_only(env, path, ['wait', 'wait', 'show'])
part = read(os.path.join(rec, 'part.doc'))
check('--preview: the whole document is typeset and shown in the viewer',
    b'Apple body' in part and b'cherry body' in part, part[:200])
check('--preview: nothing is left', left_over(t) == [], left_over(t))

run, t, rec, env = setup('onlynoviewer', STRATA_DVIVIEW='/no/such/dviview')
path = fresh(run, cns)
d = preview_only(env, path, ['wait', 'show'])
check('--preview: a failure is shown in a popup',
    'needs the viewer' in d[0], d[0])
check('--preview: after a failure nothing is left', left_over(t) == [],
    left_over(t))


real_viewer_test()
shutil.rmtree(tmp, ignore_errors=True)
print('preview: %d failures' % fails)
sys.exit(1 if fails else 0)
