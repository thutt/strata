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

"""The DVI viewer is built next to both versions of the Editor.

    python3 -I -B tests/dviviewbuild.py

The target dviview of the Makefile puts the program in the terminal directory
and in the X11 directory of the build.  It makes both directories itself, so
that the program is there whatever the order of the targets and the number of
jobs: a build in which the X11 directory does not exist yet when the viewer is
finished must still give both files.  A missing header stops the build with
the names of the packages, and no program is left over.  The checks are made
on a fresh build directory in a scratch place.
"""

import os
import shutil
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
fails = 0


def check(label, ok, detail=''):
    global fails
    if ok:
        print('ok   ' + label)
    else:
        fails += 1
        print('FAIL ' + label)
        if detail != '':
            print('     ' + str(detail)[-400:])


def build(target_words, build_dir):
    """Run strata-build with the given words in a fresh build directory; the
    status and the output of both streams."""
    script = ('cd "%s" && source scripts/setup -b debug "%s" >/dev/null 2>&1 '
              '&& scripts/strata-build %s' % (root, build_dir, target_words))
    r = subprocess.run(['bash', '-c', script], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, universal_newlines=True)

    return r.returncode, r.stdout


def same(a, b):
    with open(a, 'rb') as fa, open(b, 'rb') as fb:
        return fa.read() == fb.read()


tmp = tempfile.mkdtemp(prefix='dvb.')

# 1. A fresh build directory, so that the X11 directory does not exist when
# the viewer is finished.
b1 = os.path.join(tmp, 'one')
st, out = build('dviview', b1)
term = os.path.join(b1, 'debug', 'term', 'dviview')
x11 = os.path.join(b1, 'debug', 'x11', 'dviview')
check('strata-build dviview succeeds on a fresh build directory', st == 0, out)
check('the viewer is in the terminal directory', os.path.isfile(term), out)
check('the viewer is in the X11 directory, which did not exist before',
      os.path.isfile(x11), out)
check('the two viewers are the same program',
      os.path.isfile(term) and os.path.isfile(x11) and same(term, x11))

# 2. The same again, with the X11 directory there already: nothing breaks.
st, out = build('dviview', b1)
check('a second build with both directories there succeeds', st == 0, out)

# 3. A header that is not there: the build stops, names the package and
# leaves no program.
b3 = os.path.join(tmp, 'three')
st, out = build('dviview DVIVIEW_HEADERS=no-such-header.h:libnone-dev', b3)
check('a missing header stops the build', st != 0, out)
check('the message names the package that is missing',
      'dviview: cannot be built: install libnone-dev.' in out, out)
check('no viewer is left after the stop',
      not os.path.exists(os.path.join(b3, 'debug', 'term', 'dviview')) and
      not os.path.exists(os.path.join(b3, 'debug', 'x11', 'dviview')))

# 4. The headers that the viewer needs are named in the Makefile and in the
# list of check-tools, so that the tool names the same packages.
with open(os.path.join(root, 'Makefile')) as f:
    makefile = f.read()
with open(os.path.join(root, 'scripts', 'check-tools')) as f:
    tools = f.read()
for header, package in (('X11/Xlib.h', 'libx11-dev'),
                        ('ft2build.h', 'libfreetype-dev'),
                        ('png.h', 'libpng-dev')):
    check('the Makefile checks %s for %s' % (header, package),
          '%s:%s' % (header, package) in makefile)
    check('check-tools names %s' % package, package in tools)

shutil.rmtree(tmp, ignore_errors=True)
print('dviviewbuild: %s' % ('all passed' if fails == 0 else
                            '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
