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

"""Nothing that the tests, the manual tools or the scripts do may depend on
the sample files of the original product (the directory lotus-manuscript/
sample at the top of the repository).  The documents of the tests are the
ones in tests/samples, which tests/mksamples.py makes.  This test proves it
in two ways:

  1. A text guard: no file under tests/, manual/ (except the generated
     documents), scripts/ or the Makefile names a file of the original
     samples (the list of the names is kept here, so that the guard works
     after the directory has been removed) and none names the directory.
  2. A hidden directory: when the original directory exists and bwrap is
     available, the quick tools (roundtrip, tabcheck, loadtest and the unit
     test of the editor) run in a mount namespace where the original
     directory is an empty tmpfs; they must still pass.

    python3 -I -B tests/samplesguard.py [BINDIR]

BINDIR is the build directory of the test programs; without it the second
part is skipped.  docs/SAMPLES.txt says how to remove the originals."""
import sys as _s; _s.dont_write_bytecode = True
import os
import re
import shutil
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
repo = os.path.dirname(root)
orig = os.path.join(repo, 'lotus-manuscript', 'sample')

# The files of the original samples, by name.
NAMES = """ages.pic cd2.gmf cns10pp.doc cns123dt.wk1 cns123gr.pic cns2memo.doc
cns3memo.doc cns5drft.doc cns6drft.doc cns7prop.doc cns8let.doc cns9let.doc
cnsflpgr.gmf cnsmerge.doc comita.lvf comrom.lvf duprom.lvf envelope.set
gdstroke.dat guide.lib guide.mac laptop.bat laptop.key memo.set musbar.gmf
musdata.wk1 music.doc music1.doc music2.doc music3.doc music4.doc outline.set
pdupdate.doc proposal.set readme.exe sample1.doc sample10.doc sample2.doc
sample3.doc sample3a.gmf sample3b.gmf sample4.doc sample4.pic sample5.doc
sample5.wk1 sample6.doc sample7.doc sample7.sct scr.doc simrom.lvf temp.val
triita.lvf trirom.lvf vidpc.gmf""".split()

# Names that mean something else in the product: memo.set is the name of a
# template in the example of the manual about templates, and the name is
# generic.  They are not looked for.
GENERIC = {'memo.set', 'outline.set', 'envelope.set', 'temp.val'}

SCAN_DIRS = ['tests', 'manual', '../scripts']
SKIP_DIRS = {'doc', 'samples', 'data', 'pictures', 'shadowbin', '__pycache__'}
SKIP_FILES = {'samplesguard.py', 'SAMPLES.txt'}
SCAN_EXT = {'.py', '.c', '.h', '.sh', '.txt', '.src', '.allow', '.mk', ''}
fails = 0


def check(name, ok, detail=''):
    global fails

    if ok:
        print('ok   %s' % name)
    else:
        fails += 1
        print('FAIL %s' % name)

        if detail:
            print(detail)


def files():
    paths = [os.path.join(root, 'Makefile')]

    for d in SCAN_DIRS:
        top = os.path.normpath(os.path.join(root, d))

        for dp, dns, fns in os.walk(top):
            dns[:] = [x for x in dns if x not in SKIP_DIRS]

            for fn in fns:
                if fn in SKIP_FILES:
                    continue

                if os.path.splitext(fn)[1] in SCAN_EXT:
                    paths.append(os.path.join(dp, fn))

    return paths


def text_guard():
    pats = [(n, re.compile(r'(?<![\w.-])' + re.escape(n) + r'(?![\w-])'))
            for n in NAMES if n not in GENERIC]
    dirpat = re.compile(r"lotus-manuscript[/'\", ]+\s*'?sample(?![\w.])")
    hits = []

    for path in files():
        try:
            text = open(path, encoding='utf-8', errors='replace').read()
        except OSError:
            continue

        for n, pat in pats:
            if pat.search(text):
                hits.append('%s names %s' % (os.path.relpath(path, root), n))

        if dirpat.search(text):
            hits.append('%s names the original directory' %
                        os.path.relpath(path, root))

    return hits


def hidden_run(bindir):
    if not os.path.isdir(orig):
        print('skip the hidden directory run: %s does not exist' % orig)
        return

    if shutil.which('bwrap') is None:
        print('skip the hidden directory run: bwrap is not installed')
        return

    base = ['bwrap', '--bind', '/', '/', '--dev', '/dev', '--proc', '/proc',
            '--tmpfs', orig]
    probe = subprocess.run(base + ['/bin/true'], capture_output=True)

    if probe.returncode != 0:
        print('skip the hidden directory run: bwrap cannot make the view')
        return

    check('the original directory is empty in the private view',
          subprocess.run(base + ['/bin/sh', '-c', 'test -z "$(ls -A "%s")"'
                                 % orig]).returncode == 0)
    samples = os.path.join(here, 'samples')
    docs = sorted(os.path.join(samples, f) for f in os.listdir(samples)
                  if f.endswith('.doc'))
    runs = [
        ('roundtrip', [os.path.join(bindir, 'roundtrip')] + docs),
        ('tabcheck', [os.path.join(bindir, 'tabcheck')] + docs),
        ('loadtest', [os.path.join(bindir, 'loadtest'),
                      os.path.join(here, 'data'), samples]),
        ('edtest', [os.path.join(bindir, 'edtest'), samples,
                    os.path.join(bindir, 'edtest_hidden.doc'),
                    os.path.join(here, 'data')]),
    ]

    for name, cmd in runs:
        if not os.path.exists(cmd[0]):
            print('skip %s: %s is not built' % (name, cmd[0]))
            continue

        r = subprocess.run(base + cmd, cwd=root, capture_output=True,
                           text=True)
        check('%s runs with the original directory hidden' % name,
              r.returncode == 0, (r.stdout + r.stderr)[-600:])


hits = text_guard()
check('no test, tool or script names a file or the directory of the '
      'original samples', not hits, '\n'.join(hits))
check('the new sample directory has the documents', os.path.isdir(
      os.path.join(here, 'samples')) and len([f for f in os.listdir(
      os.path.join(here, 'samples')) if f.endswith('.doc')]) >= 8)

if len(sys.argv) > 1:
    hidden_run(sys.argv[1])

print('samplesguard: %s' % ('all passed' if not fails else
                            '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
