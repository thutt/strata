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

"""The programs that Strata runs are found in fixed folders, never in PATH.

A folder with a fake program of the name of every program that Strata starts
(the TeX engines, ghostscript, extractbb, the spell checkers, lp, lpstat, the
Editor) is put FIRST in PATH.  A fake records that it ran, and fails.  The
real programs of the system make the PDF, the spell check works, and no fake
ran: so PATH was not read.  Then a debug build is given its own list of
folders (STRATA_PROGRAM_DIRS, which a release build never reads): a folder
with a fake pdflatex is run by its full path with an argument vector, and its
PATH is the folder of the program and the system folders; an empty list gives
the message that names the folders that were looked at.

Every check fails when its feature is turned off; mutations of the programs
were run (docs/EXEC-PATHS.txt).

    STRATA_BUILD_DIR=... python3 -I -B tests/progpath.py
"""
import sys as _s; _s.dont_write_bytecode = True
import atexit, os, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
strmgr = os.environ.get('STR', os.path.join(build, 'term', 'str'))
strprint = os.environ.get('STRPRINT', os.path.join(build, 'term', 'strprint'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
if not (os.path.exists(stred) and os.path.exists(strprint)):
    print('SKIP: build the programs first')
    sys.exit(0)
SYSTEM = ('/usr/bin', '/bin', '/usr/local/bin')
if not os.path.exists('/usr/bin/pdflatex'):
    print('SKIP: /usr/bin/pdflatex is missing')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='progpath')
atexit.register(shutil.rmtree, tmp, True)
home = os.path.join(tmp, 'home')
os.makedirs(home, mode=0o700)
data = os.path.join(here, 'data')
CRLF = b'\r\n'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)[:1500]))
    fails += 0 if cond else 1


def newdir(label):
    d = os.path.join(tmp, '%s%d' % (label, len(os.listdir(tmp))))
    os.makedirs(d)
    return d


# ------------------------------------------------------- the fake programs
FAKES = ('pdflatex', 'latex', 'dvipdfmx', 'gs', 'extractbb', 'kpsewhich',
         'aspell', 'hunspell', 'dvipng', 'pdftoppm', 'pdfinfo', 'lp',
         'lpstat', 'curl', 'unzip', 'stred')
fakedir = newdir('fake')
mark = newdir('mark')
for n in FAKES:
    p = os.path.join(fakedir, n)
    with open(p, 'w') as f:
        f.write('#!/bin/bash\n'
                'echo "${0}|${*}|${PATH}" >> "${FAKE_MARK}/log"\n'
                'touch "${FAKE_MARK}/ran-$(basename "${0}")"\n'
                'exit 1\n')
    os.chmod(p, 0o755)


def ran():
    return sorted(n[4:] for n in os.listdir(mark) if n.startswith('ran-'))


def reset():
    for n in os.listdir(mark):
        os.unlink(os.path.join(mark, n))


def env_for(path_first=None, dirs=None, **extra):
    """The environment of a run: no test variable that names a program, a
    private home, and PATH with path_first in front; dirs is the list of
    folders of a debug build (None: not set)."""
    env = dict(os.environ)
    for k in ('STRATA_PROGRAM_DIRS', 'STRATA_LP', 'STRATA_LPSTAT',
              'STRATA_SPELL', 'STRED', 'STRATA_STRPRINT', 'STRATA_DVIVIEW'):
        env.pop(k, None)
    env['STRATA_HOME'] = home
    env['FAKE_MARK'] = mark
    env['STRATA_COLORS'] = 'none'
    env['STRATA_ASCII'] = '0'
    if path_first is not None:
        env['PATH'] = path_first + ':' + env.get('PATH', '')
    if dirs is not None:
        env['STRATA_PROGRAM_DIRS'] = dirs
    env.update(extra)
    return env


def tui_env(env):
    """The same environment for tui.session, which changes the variables of
    this process and does not replace them: a variable that is not in env is
    taken out (None)."""
    out = dict(env)
    for k in os.environ:
        if k not in env:
            out[k] = None
    return out


def run(cmd, env, cwd=None, timeout=240):
    r = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True,
                       timeout=timeout)
    return (r.returncode, r.stdout.decode('utf-8', 'replace'),
            r.stderr.decode('utf-8', 'replace'))


def is_pdf(path):
    return os.path.exists(path) and open(path, 'rb').read(4) == b'%PDF'


# ------------------------------------------------------------- the documents
plain = os.path.join(data, 'plain_text.doc')
work = newdir('work')
os.makedirs(os.path.join(work, 'pics'))
shutil.copy(os.path.join(data, 'pics', 'p.png'),
            os.path.join(work, 'pics', 'p.png'))
HDR = (b'*R3' + CRLF + b'MR1001' + CRLF + b'PP\\1\\\x00' + CRLF + b'P1C33' +
       CRLF + b'FHA1' + CRLF + b'FTT5' + CRLF + b'DP0,8500,11000' + CRLF)


def picdoc(name, pic):
    body = HDR + b'*B0' + CRLF + b'S' + CRLF + b'TA picture: ' + \
        b'\x1325(' + b'1' + pic + b'\x00' + CRLF + b'22000' + CRLF + \
        b'\x13)' + b'\x1d' + b'\x00' + CRLF + b'*E' + CRLF
    path = os.path.join(work, name)
    open(path, 'wb').write(body)
    return path


epsdoc = picdoc('eps.doc', b'pics/box.eps')
open(os.path.join(work, 'pics', 'box.eps'), 'wb').write(
    b'%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 100 100\n'
    b'0.2 0.4 0.8 setrgbcolor 10 10 80 80 rectfill\nshowpage\n')
pngdoc = picdoc('png.doc', b'pics/p.png')

# ------------------------------- a fake first in PATH is never run (Printer)
reset()
out = os.path.join(newdir('o1'), 'a.pdf')
rc, so, se = run([strprint, plain, '-o', out, '--no-banner'],
                 env_for(fakedir))
check('printer: pdflatex is not taken from PATH: the PDF is made by the '
      'real one', rc == 0 and is_pdf(out) and 'pdflatex' not in ran(),
      (rc, se, ran()))

reset()
out = os.path.join(newdir('o2'), 'b.pdf')
rc, so, se = run([strprint, plain, '-o', out, '--no-banner',
                  '--pdf-via-dvi'], env_for(fakedir))
check('printer: latex and dvipdfmx are not taken from PATH (the way by a '
      'DVI file)', rc == 0 and is_pdf(out) and 'latex' not in ran() and
      'dvipdfmx' not in ran(), (rc, se, ran()))

if os.path.exists('/usr/bin/gs'):
    reset()
    out = os.path.join(newdir('o3'), 'c.pdf')
    rc, so, se = run([strprint, epsdoc, '-o', out, '--no-banner',
                      '--docdir', work], env_for(fakedir), cwd=work)
    check('printer: ghostscript is not taken from PATH (a PostScript '
          'picture)', rc == 0 and is_pdf(out) and 'gs' not in ran() and
          'pdflatex' not in ran(), (rc, se, ran()))
else:
    print('skip gs: /usr/bin/gs is missing')

if os.path.exists('/usr/bin/extractbb'):
    reset()
    outd = newdir('o4')
    rc, so, se = run([strprint, pngdoc, '-o', os.path.join(outd, 'd.dvi'),
                      '--dvi', '--no-banner', '--docdir', work],
                     env_for(fakedir), cwd=work)
    check('printer: extractbb is not taken from PATH (a picture on the DVI '
          'way)', rc == 0 and os.path.exists(os.path.join(outd, 'd.dvi')) and
          'extractbb' not in ran() and 'latex' not in ran(), (rc, se, ran()))
else:
    print('skip extractbb: /usr/bin/extractbb is missing')

# --------------------------------- a fake first in PATH is never run (Editor)
if os.path.exists('/usr/bin/aspell'):
    reset()
    sp = os.path.join(newdir('sp'), 'spell.doc')
    shutil.copy(os.path.join(data, 'spell.doc'), sp)
    d = tui.session([stred, '-H', helpf, '--spell', sp], ['wait', 'show'],
                    env=tui_env(env_for(fakedir)))
    check('spell: the checker is not taken from PATH: the real aspell finds '
          'the unknown word', 'Unknown word: quikc' in d[0] and
          'aspell' not in ran() and 'hunspell' not in ran(), (d[0], ran()))
else:
    print('skip spell: /usr/bin/aspell is missing')

reset()
doc = os.path.join(newdir('lp'), 'l.doc')
shutil.copy(plain, doc)
rc, so, se = run([stred, '--print', doc, '--printer', 'zz-no-such-queue',
                  '-o', os.path.join(os.path.dirname(doc), 'l.pdf')],
                 env_for(fakedir))
check('print: lp and lpstat are not taken from PATH',
      'lp' not in ran() and 'lpstat' not in ran(), (rc, so, se, ran()))

# The Document Manager starts the Editor that is next to it, or the one of a
# fixed folder: a stred first in PATH is not run.
reset()
mdir = newdir('mgr')
mbin = os.path.join(mdir, 'bin')
os.makedirs(mbin)
shutil.copy(strmgr, os.path.join(mbin, 'str'))
shutil.copy(plain, os.path.join(mdir, 'one.doc'))
emptydir = newdir('empty')
old = os.getcwd()
os.chdir(mdir)
try:
    d = tui.session([os.path.join(mbin, 'str'), '-H', helpf],
                    ['INS', 'wait', 'text:one.doc', 'INS', 'wait', 'wait', 'show'],
                    env=tui_env(env_for(fakedir, dirs=emptydir)))
finally:
    os.chdir(old)
check('manager: the Editor is not taken from PATH: it is not run, and the '
      'message shows so', 'stred' not in ran() and
      'Cannot run the editor' in d[0], (d[0], ran()))

# ------------------------------- a list of folders of a debug build is used
# The build type decides whether the variable is read: a release build never
# reads it, so the checks below are made with a debug or an asan build only.
btype = os.environ.get('STRATA_BUILD_TYPE', 'debug')
if btype == 'release':
    print('skip hook: a release build never reads STRATA_PROGRAM_DIRS')
else:
    reset()
    out = os.path.join(newdir('o5'), 'e.pdf')
    poison = newdir('poison')
    rc, so, se = run([strprint, plain, '-o', out, '--no-banner'],
                     env_for(poison, dirs=emptydir))
    check('hook: with no folder that holds pdflatex the Printer shows that '
          'it did not run, and names the folder and that PATH is not read',
          rc != 0 and 'pdflatex did not run' in se and emptydir in se and
          'not in PATH' in se, (rc, se))

    reset()
    out = os.path.join(newdir('o6'), 'f.pdf')
    rc, so, se = run([strprint, plain, '-o', out, '--no-banner'],
                     env_for(poison, dirs=fakedir))
    log = open(os.path.join(mark, 'log')).read() if os.path.exists(
        os.path.join(mark, 'log')) else ''
    first = log.split('\n')[0].split('|') if log else ['', '', '']
    check('hook: a program of the list is run by its full path',
          first[0] == os.path.join(fakedir, 'pdflatex'), log[:300])
    check('hook: it is given its arguments as separate words',
          '-no-shell-escape' in first[1].split(' ') and
          '-interaction=nonstopmode' in first[1].split(' '), log[:300])
    check('hook: its PATH is its own folder and the system folders, not the '
          'PATH of the user', first[2] == fakedir + ':/usr/bin:/bin:'
          '/usr/local/bin' and poison not in first[2], log[:300])

print('progpath: %d failures' % fails)
sys.exit(1 if fails else 0)
