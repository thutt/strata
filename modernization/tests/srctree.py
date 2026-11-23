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

"""A build must never write a product into the source tree.  This test
records what git says about the files of the source tree (the changed files,
the new files and the ignored files), runs the build of the manual and of
the checks, records it again, and fails when the two lists differ.  The
quick form (the default) runs the generator of the documents, the samples,
the card with the symbols and the checks of the sources; with --full it runs
every target of manual/build.sh that does not write into the tree on
purpose (the targets publish and pics do, and are not run), for which the
programs of XBIN are needed.  It also checks that the Makefile of the Viewer
puts its objects, its program and its results below STRATA_BUILD_DIR.

    python3 -I -B tests/srctree.py [--full]

XBIN names the x11 build directory (or STRPRINT the Printer) for --full.  A
tree that is not a git checkout is skipped.  Nothing is written in the
tree; the products go to a folder that is made for the test and removed."""
import sys as _s; _s.dont_write_bytecode = True
import os
import shutil
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
manual = os.path.join(root, 'manual')
full = '--full' in sys.argv
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


def run(cmd, env=None, cwd=None):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       env=env, cwd=cwd, universal_newlines=True)
    return r.returncode, r.stdout


def tree_state():
    """The set of lines of git status for the source tree: the changed,
    the new and the ignored files, each file on its own."""
    rc, out = run(['git', 'status', '--porcelain', '--ignored',
                   '--untracked-files=all', '--', '.'], cwd=root)
    if rc != 0:
        return None
    return set(out.split('\n')) - {''}


def stat_state():
    """The size and the time of the files that are tracked by git, so
    that a build that rewrites a committed document is seen as well."""
    rc, out = run(['git', 'ls-files', '--', '.'], cwd=root)
    state = {}
    for name in out.split('\n'):
        path = os.path.join(root, name)
        if name != '' and os.path.isfile(path):
            st = os.stat(path)
            state[name] = (st.st_size, st.st_mtime_ns)
    return state


before = tree_state()
if before is None:
    print('srctree: not a git checkout: skipped')
    sys.exit(0)

before_files = stat_state()
tmp = tempfile.mkdtemp(prefix='srct')
out_dir = os.path.join(tmp, 'out')
env = dict(os.environ)
env['MANUAL_OUT'] = out_dir
env['STRATA_BUILD_DIR'] = tmp
xbin = os.environ.get('XBIN')
if xbin is None and os.environ.get('STRPRINT') is not None:
    xbin = os.path.dirname(os.environ['STRPRINT'])
if xbin is not None:
    env['XBIN'] = xbin
    env['RT'] = os.path.join(os.path.dirname(xbin), 'term', 'roundtrip')
    if not os.path.exists(env['RT']):
        env['RT'] = os.path.join(xbin, 'roundtrip')
env.pop('PDFDIR', None)
# SRCTREE_BUILD_SH names another script (used to try the test itself).
build = os.environ.get('SRCTREE_BUILD_SH', os.path.join(manual, 'build.sh'))
py = [sys.executable, '-I', '-B']

# The generator of the documents and the checks of the sources.
steps = [('samples', ['bash', build, 'samples']),
         ('docs', ['bash', build, 'docs']),
         ('check-names', ['bash', build, 'check-names']),
         ('check-keys', ['bash', build, 'check-keys']),
         ('check-impl', ['bash', build, 'check-impl']),
         ('the card of the symbols',
          py + [os.path.join(manual, 'mkmanual.py'),
                os.path.join(manual, 'src', 'mathcheat.src'),
                os.path.join(tmp, 'mathcheat.doc')])]
if full:
    if xbin is None:
        print('srctree: --full needs XBIN or STRPRINT')
        sys.exit(2)
    for t in ('single', 'cards', 'developer', 'check', 'check-edition',
              'pdf'):
        steps.append((t, ['bash', build, t]))
for name, cmd in steps:
    rc, text = run(cmd, env=env, cwd=root)
    check('build.sh %s ends well' % name if cmd[1] == build else
          'the generator makes %s' % name, rc == 0, text[-600:])

# The products are where they were told to be.
docs_made = []
if os.path.isdir(os.path.join(out_dir, 'doc')):
    docs_made = [f for f in os.listdir(os.path.join(out_dir, 'doc'))
                 if f.endswith('.doc')]
check('the documents are in the products folder', len(docs_made) > 10,
      out_dir)
check('the sample documents are in the products folder',
      os.path.isdir(os.path.join(out_dir, 'samples')) and
      len(os.listdir(os.path.join(out_dir, 'samples'))) > 0, out_dir)

# Every python3 that a test or the build starts has -B: the option -I
# ignores PYTHONDONTWRITEBYTECODE, and one call without it would leave a
# bytecode folder in the tree (that happened with the tests of the cards).
import re
missing = []
patterns = [(re.compile(r"""['"]-I['"](?!\s*,\s*['"]-B['"])"""),
             os.path.join(root, 'tests'), '.py'),
            (re.compile(r"python3 -I (?!-B)"), manual, '.sh'),
            (re.compile(r"python3 -I (?!-B)"), root, 'Makefile')]
for rx, top, ext in patterns:
    for d, dirs, files in os.walk(top):
        dirs[:] = [x for x in dirs if not x.startswith('.')]
        for f in files:
            if not (f.endswith(ext) or f == ext):
                continue
            path = os.path.join(d, f)
            if os.path.abspath(path) == os.path.abspath(__file__):
                continue
            with open(path, encoding='utf-8', errors='replace') as fh:
                for n, line in enumerate(fh, 1):
                    if rx.search(line):
                        missing.append('%s:%d' % (os.path.relpath(path, root),
                                                  n))
check('every python3 -I of the tests and of the build also has -B',
      not missing, '\n'.join('  ' + x for x in sorted(missing)[:20]))

# The Viewer: the Makefile must use the build directory.  A dry run of the
# build and of the test lists every file that it would write.
viewer_dir = os.path.join(root, 'tools', 'dviview')
outs = []
for goal in ('all', 'test'):
    rc, text = run(['make', '-C', viewer_dir, '-n', goal], env=env)
    for line in text.split('\n'):
        words = line.split()
        for i, w in enumerate(words):
            if w == '-o' and i + 1 < len(words):
                outs.append(words[i + 1])
            if w == '-p' and i > 0 and words[i - 1] == 'mkdir':
                outs.append(words[i + 1])
        for w in words:
            if w.startswith('DVIEW_RESULTS='):
                outs.append(w.split('=', 1)[1])
bad = [x for x in outs if not x.startswith(tmp)]
check('the Viewer writes its objects, program and results in the build '
      'directory', len(outs) > 3 and not bad,
      '\n'.join('  not in the build directory: %s' % x for x in bad) or
      '  no output found in the dry run')

after = tree_state()
after_files = stat_state()
new = sorted(after - before)
gone = sorted(before - after)
check('the build left no new or changed file in the source tree',
      not new and not gone,
      '\n'.join(['  new: %s' % x for x in new] +
                ['  gone: %s' % x for x in gone]))
changed = sorted(n for n in before_files
                 if after_files.get(n) != before_files[n])
check('the build did not rewrite a file that git keeps', not changed,
      '\n'.join('  rewritten: %s' % x for x in changed))

shutil.rmtree(tmp, ignore_errors=True)
print('srctree: %d failures' % fails)
sys.exit(1 if fails else 0)
