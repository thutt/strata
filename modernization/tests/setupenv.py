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

"""A developer who runs the programs in place from a build directory needs no
variable of his own: sourcing scripts/setup is enough.

  (a) in a clean shell, sourcing scripts/setup sets STRATA_TEMPLATES to a folder
      that exists and holds every shipped template, and STRATA_HELP;
  (b) the build directory has share/strata/templates, a link to the templates
      of the source tree, where ../share/strata/templates from a program in
      the build directory finds them, in a shell that never sourced setup (the
      link is made at every build and cannot go stale);
  (c) the Document Manager started from the build directory with only setup
      sourced lists the templates in New; and with the variable taken out
      again it lists them too, by the link.

    python3 -I -B tests/setupenv.py [BUILD_DIR]     (BUILD_DIR: .../release)
"""
import sys as _s; _s.dont_write_bytecode = True
import os, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui
import mktemplates
import labeldata

root = os.path.abspath(os.path.join(here, '..', '..'))
src = os.path.join(root, 'modernization')
shipped = os.path.join(src, 'templates')
setup = os.path.join(root, 'scripts', 'setup')
build = (sys.argv[1] if len(sys.argv) > 1 else
         os.environ.get('STRATA_BUILD_DIR', ''))
names = ['letter-us', 'letter-a4'] + sorted(mktemplates.ENVELOPES)
# The label sheets are in the folder too; the list shows the first rows.
labels = [n for n, num, sheet in labeldata.every()]

fails = 0
tmp = tempfile.mkdtemp(prefix='senv')
home = tempfile.mkdtemp(prefix='home', dir=tmp)


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def clean_env():
    return {'PATH': '/usr/bin:/bin', 'HOME': home, 'TERM': 'xterm',
            'LANG': 'C.UTF-8'}


# ---- (a) setup sets the variable in a clean shell ----------------------------
fake = os.path.join(tmp, 'build', 'release')
r = subprocess.run(['bash', '-c', 'source "%s" "%s" > /dev/null; '
    'echo "T=${STRATA_TEMPLATES}"; echo "H=${STRATA_HELP}"' % (setup, fake)],
    capture_output=True, text=True, env=clean_env())
vals = dict(l.split('=', 1) for l in r.stdout.split('\n') if '=' in l)
tvar = vals.get('T', '')
have = sorted(f[:-4] for d, _, fs in os.walk(tvar) for f in fs
              if f.endswith('.doc')) if os.path.isdir(tvar) else []
check('sourcing setup in a clean shell sets STRATA_TEMPLATES to the templates '
    'of the source tree',
    os.path.realpath(tvar) == os.path.realpath(shipped), (r.stdout, r.stderr))
check('the folder that STRATA_TEMPLATES names holds every shipped template',
    sorted(names + labels) == have, (names, have))
check('sourcing setup also sets STRATA_HELP',
    vals.get('H', '').endswith('/release/strata.hlp'), r.stdout)

# ---- (b) the build directory has the link -----------------------------------
link = os.path.join(build, 'share', 'strata', 'templates') if build else ''
if not build or not os.path.isdir(build):
    print('SKIP: no build directory: (b) and (c) need one')
    shutil.rmtree(tmp, ignore_errors=True)
    print('setupenv: %s' % ('all passed' if fails == 0 else
                            '%d failed' % fails))
    sys.exit(1 if fails else 0)
check('the build directory has share/strata/templates, a link to the source '
    'templates',
    os.path.islink(link) and
    os.path.realpath(link) == os.path.realpath(shipped), link)
check('the link shows the same templates as the source tree, so it cannot '
    'be stale',
    os.path.isdir(link) and sorted(os.listdir(link)) ==
    sorted(os.listdir(shipped)), link)

# ---- (c) the Document Manager from the build directory -----------------------
strprog = os.path.join(build, 'term', 'str')
helpf = os.path.join(build, 'strata.hlp')
if not os.path.exists(strprog):
    print('SKIP: no str in the build directory: (c) needs it')
else:
    mgr = os.path.join(tmp, 'mgr')
    os.makedirs(mgr)
    os.chdir(mgr)
    os.environ['HOME'] = home
    os.environ['STRATA_HOME'] = tempfile.mkdtemp(prefix='sh', dir=tmp)
    steps = ['ENTER', 'text:newdoc', 'INS', 'wait', 'wait', 'show']
    # only setup sourced: nothing else set by hand
    cmd = ('unset STRATA_TEMPLATES STRATA_HELP; source "%s" "%s" > /dev/null; '
           'exec "%s/term/str" -H "%s" "%s"' % (setup, build, build, helpf, mgr))
    d = tui.session(['/bin/bash', '-c', cmd], steps, rows=34)
    scr = d[0] if d else ''
    check('the Document Manager of the build directory, with only setup '
        'sourced, lists the templates in New',
        'Templates' in scr and all(n in scr for n in (
            'envelopes/', 'labels/', 'letters/')), scr)
    # the variable taken out again: the link is found by the program itself
    cmd = ('unset STRATA_TEMPLATES STRATA_HELP; exec "%s/term/str" -H "%s" '
           '"%s"' % (build, helpf, mgr))
    d = tui.session(['/bin/bash', '-c', cmd], steps, rows=34)
    scr = d[0] if d else ''
    check('without any variable the Document Manager of the build directory '
        'still lists the templates, by the link in the build directory',
        'Templates' in scr and all(n in scr for n in (
            'envelopes/', 'labels/', 'letters/')), scr)

shutil.rmtree(tmp, ignore_errors=True)
print('setupenv: %s' % ('all passed' if fails == 0 else '%d failed' % fails))
sys.exit(1 if fails else 0)
