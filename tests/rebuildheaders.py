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

"""A header that is installed after a build is used by the next build.

    python3 -I -B tests/rebuildheaders.py

A person builds with the default target on a machine that lacks the Xft
headers, installs the packages that the closing lines name, and builds again
in the same build directory.  The second build has to make the X11 programs
again, with Xft, and has to print no warning about the packages that are now
installed.  The test does the same without changing the system: the first
build and the last one get the make variable XFT_H= (the answer "no Xft
headers"), the second one is made with the real answer.  A third build with
the same answer makes nothing again.  When the machine has no Xft headers
the test is skipped: there is no answer to change.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
fails = 0
WARNING = 'WARNING: the X11 version was built without Xft'
NOTE = 'x11: NOTE: built without Xft'


def check(label, ok, detail=''):
    global fails
    if ok:
        print('ok   ' + label)
    else:
        fails += 1
        print('FAIL ' + label)
        if detail != '':
            print('     ' + str(detail)[-500:])


def build(words, build_dir):
    """Run strata-build with the words in the build directory: the status and
    the output of both streams."""
    script = ('cd "%s" && source scripts/setup -b debug "%s" >/dev/null 2>&1 '
              '&& scripts/strata-build -j 8 %s' % (root, build_dir, words))
    r = subprocess.run(['bash', '-c', script], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, universal_newlines=True)

    return r.returncode, r.stdout


def has_xft(program):
    """Whether the program is linked with the library of Xft."""
    r = subprocess.run(['ldd', program], stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL, universal_newlines=True)

    return 'libXft' in r.stdout


def made(out):
    """The objects and programs that a build made: the words after CC or LD."""
    return re.findall(r'^  (?:CC|LD)\s+(\S+)', out, re.MULTILINE)


if not os.path.exists('/usr/include/X11/Xft/Xft.h'):
    print('SKIP: this machine has no Xft headers: there is no answer to '
          'change')
    print('rebuildheaders: all passed')
    sys.exit(0)

tmp = tempfile.mkdtemp(prefix='rbh.')
b = os.path.join(tmp, 'b')
stred = os.path.join(b, 'debug', 'x11', 'stred')
stamp = os.path.join(b, 'debug', 'x11', 'xft-answer')

# 1. The default build on a machine that lacks the Xft headers.
st, out = build('all XFT_H=', b)
check('the first default build succeeds', st == 0, out)
check('the closing lines warn that the X11 version has no Xft',
      WARNING in out, out)
check('the X11 program is built without Xft', os.path.isfile(stred) and
      not has_xft(stred))

# 2. The packages are installed: the same build directory, the real answer.
st, out = build('all', b)
check('the second default build succeeds', st == 0, out)
objs = made(out)
check('the second build makes the X11 frontend again',
      any(o.endswith('/x11/term_x11.o') for o in objs), out)
check('the second build links the X11 programs again',
      any(o.endswith('/x11/stred') for o in objs) and
      any(o.endswith('/x11/str') for o in objs), out)
check('the X11 program is now linked with Xft', has_xft(stred))
check('the second build prints no warning about Xft', WARNING not in out and
      NOTE not in out, out)
lacking = subprocess.run([os.path.join(root, 'scripts', 'check-tools'),
                          '--build', '--quiet'], stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL).returncode != 0
check('the second build prints a note about missing packages only when the '
      'machine lacks some now',
      ('something that the build needs is missing' in out) == lacking, out)
answer = ''
if os.path.isfile(stamp):
    with open(stamp) as f:
        answer = f.read().strip()
check('the answer in the build directory is xft', answer == 'xft', answer)

# 3. Nothing changed: nothing is made again.
st, out = build('all', b)
check('the third default build succeeds', st == 0, out)
check('the third build makes nothing again', made(out) == [], made(out))
check('the third build prints no warning about Xft', WARNING not in out,
      out)

# 4. The headers go away again: the program is made without Xft.
st, out = build('all XFT_H=', b)
check('a build after the headers went away succeeds', st == 0, out)
check('it makes the X11 frontend again',
      any(o.endswith('/x11/term_x11.o') for o in made(out)), out)
check('the X11 program is no longer linked with Xft', not has_xft(stred))
check('the closing lines warn again', WARNING in out, out)

shutil.rmtree(tmp, ignore_errors=True)
print('rebuildheaders: %s' % ('all passed' if fails == 0 else
                              '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
