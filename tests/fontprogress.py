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

"""The feedback of Install Now in the Document Manager.

After Ins is pressed on the panel of a font with Install Now set to Yes, the
Document Manager draws a box, Installing font, that names the step that runs:
Downloading NAME... with the size so far, Unpacking NAME..., the file list of
TeX and the map file.  Esc stops the download (and says so); the quick steps
that follow it cannot be stopped.  The test runs the Document Manager in a
terminal with slow fake programs (STRATA_PROGRAM_DIRS, a debug build) and
looks at the screen while each step runs:

  (a) the box appears with the font name and the step Downloading, and the
      size grows while the fake curl writes;
  (b) the steps Unpacking, the file list and the map file follow in turn,
      and the result is the message Installed as before;
  (c) Esc during the download stops it: the box goes away, the status line
      shows Installation cancelled, no fake program is left running, no
      partial archive is left in the temporary directory and nothing is
      installed;
  (d) Esc during a quick step does not stop it;
  (e) a step that fails still shows the pop-up Install failed.

Every check fails when its feature is turned off; the mutations that were
run are in docs/FONTS.txt.

    STRATA_BUILD_DIR=... python3 -I -B tests/fontprogress.py
"""
import sys as _s; _s.dont_write_bytecode = True
import atexit, os, shutil, stat, subprocess, sys, tempfile, time, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
build = os.environ.get('STRATA_BUILD_DIR', '')
strprog = os.environ.get('STR', os.path.join(build, 'term', 'str'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))

if not os.path.exists(strprog) or shutil.which('kpsewhich') is None or \
        shutil.which('unzip') is None:
    print('SKIP: build the programs first; kpsewhich and unzip are needed')
    sys.exit(0)

fails = 0


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


tmp = tempfile.mkdtemp(prefix='fprog')
os.chmod(tmp, 0o700)
atexit.register(shutil.rmtree, tmp, True)
ctan = os.path.join(tmp, 'ctan')
os.mkdir(ctan)
with zipfile.ZipFile(os.path.join(ctan, 'strfake.tds.zip'), 'w') as z:
    z.writestr('tex/latex/strfake/strfake.sty', '\\ProvidesPackage{strfake}\n')
tmpdir = os.path.join(tmp, 'tmpdir')
os.mkdir(tmpdir)
bindir = os.path.join(tmp, 'bin')
os.mkdir(bindir)
pidlog = os.path.join(tmp, 'pids.log')


def fake(name, body):
    path = os.path.join(bindir, name)
    with open(path, 'w') as f:
        f.write('#!/bin/sh\n' + body)
    os.chmod(path, 0o755)


REAL_UNZIP = shutil.which('unzip')
REAL_KPSE = shutil.which('kpsewhich')
# The fake curl writes part of the archive at once, waits, and writes the
# rest, so that the size on the screen grows while the step runs.  Every
# fake notes its process number, so that the test can see that none is left.
CURL = ('echo $$ >> %s\n'
        'out=""\n'
        'while [ $# -gt 0 ]; do\n'
        '  if [ "$1" = "-o" ]; then out="$2"; fi\n'
        '  shift\n'
        'done\n'
        'head -c 1500000 /dev/zero > "$out"\n'
        'sleep %s\n'
        'head -c 1500000 /dev/zero >> "$out"\n'
        'sleep %s\n'
        'cp "%s" "$out"\n')


def setup_fakes(download=2, unpack=2, lsr=2, updmap=2, curl_fail=False,
                updmap_fail=False):
    fake('curl', 'exit 22\n' if curl_fail else CURL % (pidlog, download,
         download, os.path.join(ctan, 'strfake.tds.zip')))
    fake('unzip', 'echo $$ >> %s\nsleep %s\nexec %s "$@"\n' % (pidlog, unpack,
         REAL_UNZIP))
    fake('mktexlsr', 'echo $$ >> %s\nsleep %s\nexit 0\n' % (pidlog, lsr))
    fake('updmap-user', 'echo $$ >> %s\nsleep %s\nexit %d\n' % (pidlog,
         updmap, 1 if updmap_fail else 0))
    fake('kpsewhich', 'exec %s "$@"\n' % REAL_KPSE)


# The keys that open the panel of font 20, give it the package, the archive
# and the map file of the fake, and set Install Now to Yes with the space bar.
OPEN_FONT = ['DOWN'] * 4 + ['ENTER', 'wait'] + ['DOWN'] * 19 + ['ENTER',
    'wait', 'DOWN', 'text:Fake', 'DOWN', 'text:strfake', 'DOWN', 'DOWN',
    'DOWN', 'text:strfake', 'DOWN', 'text:strfake.map', 'DOWN', 'DOWN',
    'text: ']


def run(steps, **kw):
    texhome = tempfile.mkdtemp(prefix='texmf', dir=tmp)
    home = os.path.join(tempfile.mkdtemp(prefix='home', dir=tmp), 'strata')
    open(pidlog, 'w').close()
    env = {'STRATA_HOME': home, 'TEXMFHOME': texhome, 'TMPDIR': tmpdir,
           'STRATA_CTAN_URL': 'file://' + ctan,
           'STRATA_PROGRAM_DIRS': bindir + ':/usr/bin:/bin:/usr/local/bin'}
    d = tui.session([strprog, '-H', helpf], steps, rows=30, env=env, **kw)
    return d, texhome


def alive():
    """The fake programs that still run."""
    n = 0

    try:
        with open(pidlog) as f:
            pids = [int(w) for w in f.read().split()]
    except OSError:
        return 0

    for p in pids:
        try:
            os.kill(p, 0)
            with open('/proc/%d/stat' % p) as f:
                if f.read().split()[2] != 'Z':
                    n += 1
        except (OSError, IndexError):
            pass

    return n


# (a) and (b): the steps in turn, then the result.
setup_fakes()
d, texhome = run(OPEN_FONT + ['INS', 'waitfor:Downloading Fake', 'show',
    'wait', 'show', 'waitfor:Unpacking Fake', 'show',
    'waitfor:Making the file list', 'show', 'waitfor:Enabling the map file',
    'show', 'waitfor:Installed', 'show', 'ENTER', 'ESC'])
check('install: the box Installing font names the font and the download',
      'Installing font' in d[0] and 'Downloading Fake' in d[0] and
      'Esc stops' in d[0], d[0])
check('install: the size of the download is on the screen',
      ' MB so far' in d[0] + d[1], d[0] + d[1])
check('install: the step Unpacking names the font and cannot be stopped',
      'Unpacking Fake' in d[2] and 'Please wait' in d[2] and
      'Esc stops' not in d[2], d[2])
check('install: the step that makes the file list of TeX is shown',
      'Making the file list of TeX (mktexlsr)' in d[3], d[3])
check('install: the step that enables the map file is shown with its name',
      'Enabling the map file' in d[4] and 'updmap-user' in d[4], d[4])
check('install: the result is the message Installed in as before',
      'Installed in' in d[5] and os.path.exists(os.path.join(texhome, 'tex',
      'latex', 'strfake', 'strfake.sty')), d[5])
check('install: no fake program is left running after the install',
      alive() == 0, alive())

# (c): Esc during the download.
setup_fakes(download=6)
d, texhome = run(OPEN_FONT + ['INS', 'waitfor:Downloading Fake', 'show',
    'ESC', 'wait', 'show'])
time.sleep(0.5)
left = [n for n in os.listdir(tmpdir) if n.startswith('strata-font')]
check('install: Esc during the download closes the box and says so',
      'Installing font' not in d[1] and 'Installation cancelled' in d[1],
      d[1])
check('install: after Esc no program of the install is left running',
      alive() == 0, alive())
check('install: after Esc no partial archive or folder is left',
      left == [], left)
check('install: after Esc nothing of the font is installed',
      not os.path.exists(os.path.join(texhome, 'tex')), os.listdir(texhome))

# (d): Esc during a quick step is not an answer.
setup_fakes(download=0, unpack=4)
d, texhome = run(OPEN_FONT + ['INS', 'waitfor:Unpacking Fake', 'ESC',
    'show', 'waitfor:Installed', 'show', 'ENTER', 'ESC'])
check('install: Esc while unpacking does not stop it',
      'Unpacking Fake' in d[0] and 'Installed in' in d[1] and
      os.path.exists(os.path.join(texhome, 'tex', 'latex', 'strfake',
      'strfake.sty')), d)

# (e): a failing step still shows the pop-up.
setup_fakes(download=0, curl_fail=True)
d, texhome = run(OPEN_FONT + ['INS', 'waitfor:Install failed', 'show',
    'ENTER', 'ESC'])
check('install: a download that fails shows the pop-up Install failed',
      'Install failed' in d[0] and 'could not be downloaded' in d[0], d[0])
setup_fakes(download=0, unpack=0, lsr=0, updmap=0, updmap_fail=True)
d, texhome = run(OPEN_FONT + ['INS', 'waitfor:Install failed', 'show',
    'ENTER', 'ESC'])
check('install: a map file that cannot be enabled shows the pop-up too',
      'Install failed' in d[0] and 'updmap-user' in d[0], d[0])

print('fontprogress: %d failure(s)' % fails)
sys.exit(1 if fails else 0)
