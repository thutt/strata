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

"""The developer's check of the tools (scripts/check-tools, also run as
'strata-build check-tools') reports which programs, headers and libraries
that building, testing and running need are present, names the Debian package
of each one that is missing, installs nothing, and ends with status 0 only
when nothing required is missing.

    python3 -I -B tests/checktools.py

The programs of a machine are made by a directory of small programs that is
the whole PATH, so that the test does not depend on what this machine has
(the headers and the libraries it cannot hide are only listed)."""
import sys as _s; _s.dont_write_bytecode = True
import os
import shutil
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
project = os.path.dirname(here)
# The variable STRATA_CHECK_TOOLS names another copy of the script (a file
# of the scripts folder), which is how the mutations of the test are tried.
script = os.path.join(project, 'scripts', os.environ.get(
    'STRATA_CHECK_TOOLS', 'check-tools'))
build_script = os.path.join(project, 'scripts', 'strata-build')

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


def run(cmd, env):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       env=env, timeout=120)
    return r.returncode, r.stdout.decode('utf-8', 'replace')


# The programs that the script needs itself; the tools it looks for are not
# among them.
CORE = ('bash sh env dirname basename cat grep sed awk tr sort uniq printf '
        'date ls true false test head tail wc cut id uname').split()
TEST_TOOLS = {'Xvfb': 'xvfb', 'xdotool': 'xdotool', 'xwd': 'x11-apps',
              'bwrap': 'bubblewrap', 'pdftotext': 'poppler-utils',
              'pdfinfo': 'poppler-utils', 'pdftoppm': 'poppler-utils',
              'pdffonts': 'poppler-utils'}
BUILD_TOOLS = {'make': 'make', 'python3': 'python3'}

wk = tempfile.mkdtemp(prefix='chkt.')
try:
    fake = os.path.join(wk, 'bin')
    os.makedirs(fake)
    for n in CORE:
        src = shutil.which(n)
        if src is not None:
            os.symlink(src, os.path.join(fake, n))
    env = {'PATH': fake, 'HOME': wk, 'TMPDIR': wk}

    def with_tools(names):
        """A PATH of the core programs and the named programs, each a small
        program that does nothing."""
        d = tempfile.mkdtemp(dir=wk)
        for n in names:
            p = os.path.join(d, n)
            open(p, 'w').write('#!/bin/sh\nexit 0\n')
            os.chmod(p, 0o755)
        e = dict(env)
        e['PATH'] = d + ':' + fake
        return e

    # --------------------------------------------------------- the help
    st, out = run([script, '--help'], env)
    check('help: --help ends with status 0 and names the groups',
          st == 0 and '--build' in out and '--tests' in out and
          '--run' in out and '--quiet' in out, out)
    st, out = run([script, '--nonsense'], env)
    check('help: an unknown option ends with status 2', st == 2, out)

    # --------------------------------- a machine with none of the tools
    st, out = run([script, '--tests'], env)
    check('tests: with none of the tools the status is 1', st == 1, out)
    bad = []
    for tool, package in TEST_TOOLS.items():
        want = 'MISSING   %s (package %s)' % (tool, package)
        if want not in out:
            bad.append(want)
    check('tests: every missing tool is named with its Debian package',
          not bad, '\n'.join(bad) + '\n' + out)
    last = [l for l in out.split('\n')
            if l.strip().startswith('sudo apt-get install')]
    check('tests: the end gives one apt-get command with each package once',
          len(last) == 1 and
          sorted(last[0].split()[4:]) == sorted(set(TEST_TOOLS.values())),
          out)
    check('tests: the command is apt-get, without the recommended packages, '
          'and not apt', len(last) == 1 and
          '--no-install-recommends' in last[0].split()[:4] and
          not any(l.strip().startswith('sudo apt install')
                  for l in out.split('\n')), out)
    check('tests: one note says that a 404 means an old index and what to '
          'run', 'If it fails with 404 Not Found, the package index is old'
          in out and 'sudo apt-get update' in out and
          'repeat the command' in out, out)
    check('tests: apt-get update is not part of the printed command',
          len(last) == 1 and 'update' not in last[0], out)
    check('tests: nothing outside the group is checked',
          'pdflatex' not in out and 'make' not in out, out)

    # ------------------------------------------- a machine with them all
    full = with_tools(TEST_TOOLS)
    st, out = run([script, '--tests'], full)
    check('tests: with every tool present the status is 0',
          st == 0 and 'Nothing required is missing' in out and
          'MISSING' not in out, out)
    for tool in TEST_TOOLS:
        without = with_tools([t for t in TEST_TOOLS if t != tool])
        st, out = run([script, '--tests'], without)
        check('tests: without %s alone the status is 1 and it is named '
              'with its package' % tool,
              st == 1 and ('MISSING   %s (package %s)' % (
                  tool, TEST_TOOLS[tool])) in out and
              ('sudo apt-get install --no-install-recommends %s' %
               TEST_TOOLS[tool]) in out, out)
        if tool == 'xdotool':
            break
    # --------------------------------------------------------- --quiet
    st, out = run([script, '--tests', '--quiet'], full)
    check('quiet: with --quiet and nothing missing only the end is printed',
          st == 0 and '  ok ' not in out and
          'Nothing required is missing' in out, out)
    st, out = run([script, '--tests', '--quiet'], env)
    check('quiet: with --quiet the missing tools are still printed',
          st == 1 and 'MISSING   Xvfb (package xvfb)' in out and
          '  ok ' not in out, out)

    # ------------------------------------------------ the other groups
    st, out = run([script, '--build'], env)
    check('build: with no compiler, make or python3 they are named with '
          'their packages and the status is 1',
          st == 1 and 'MISSING   cc or gcc (package build-essential)' in out
          and 'MISSING   make (package make)' in out and
          'MISSING   python3 (package python3)' in out, out)
    check('build: the headers of X11, Xft, FreeType and fontconfig are '
          'checked', all(h in out for h in (
              'X11/Xlib.h', 'X11/Xft/Xft.h', 'freetype2/ft2build.h',
              'fontconfig/fontconfig.h')), out)
    check('build: the group does not look at the tests or at LaTeX',
          'Xvfb' not in out and 'pdflatex' not in out, out)
    st, out = run([script, '--run'], env)
    check('run: with no programs LaTeX, dvipng, ghostscript, the spell '
          'check and the thesaurus are named with their packages',
          st == 1 and 'MISSING   pdflatex (package texlive-latex-base)' in
          out and 'MISSING   dvipng (package dvipng)' in out and
          'MISSING   gs (package ghostscript)' in out and
          'MISSING   aspell or hunspell (package hunspell hunspell-en-us)'
          in out, out)
    check('run: the LaTeX files of the Printer are checked with the '
          'package of each', all(x in out for x in (
              'tikz.sty (package texlive-pictures)',
              'adjustbox.sty (package texlive-latex-extra)',
              'ulem.sty (package texlive-plain-generic)',
              'fancyhdr.sty (package texlive-latex-base)')), out)
    check('run: the optional programs do not change the status and are '
          'marked optional', 'optional  lp (package cups-client)' in out,
          out)
    # Everything required is there; the optional programs (lp, curl, unzip)
    # are not, and that must not change the status.  The thesaurus folder
    # and the libraries are those of this machine: without them the part is
    # not tried.
    rundir = tempfile.mkdtemp(dir=wk)
    for n in ('pdflatex', 'latex', 'dvipdfmx', 'dvipng', 'gs', 'hunspell'):
        p_ = os.path.join(rundir, n)
        open(p_, 'w').write('#!/bin/sh\nexit 0\n')
        os.chmod(p_, 0o755)
    p_ = os.path.join(rundir, 'kpsewhich')
    open(p_, 'w').write('#!/bin/sh\nfor f in "$@"; do echo "/x/$f"; done\n')
    os.chmod(p_, 0o755)
    env_run = dict(env)
    env_run['PATH'] = rundir + ':' + fake
    st, out = run([script, '--run'], env_run)
    if 'MISSING' in out:
        print('note the part on optional programs is not tried: this machine '
              'lacks a thesaurus folder or a library\n%s' % out)
    else:
        check('run: with every required item present and the optional '
              'programs absent the status is 0',
              st == 0 and 'optional  lp (package cups-client)' in out and
              'Nothing required is missing (3 optional' in out, out)
    st, out = run([script, '--run', '--tests'], env)
    check('run: two groups give both lists',
          'MISSING   pdflatex' in out and 'MISSING   Xvfb' in out, out)
    apt = [l for l in out.split('\n')
           if l.strip().startswith('sudo apt-get install')]
    check('run: the apt command has every package once',
          len(apt) == 1 and apt[0].split().count('poppler-utils') == 1 and
          apt[0].split().count('texlive-latex-base') == 1, out)
    st, out = run([script], full)
    check('all: with no option every group is checked',
          'To BUILD' in out and 'To run the TESTS' in out and
          'To RUN' in out, out)

    # -------------------------------- the tool installs nothing at all
    before = set(os.listdir(wk))
    run([script], env)
    check('nothing: the check makes no file',
          set(os.listdir(wk)) == before, sorted(os.listdir(wk)))

    # -------------------------------- strata-build check-tools
    clean = dict(os.environ)
    for k in list(clean):
        if k.startswith('STRATA_'):
            del clean[k]
    st1, out1 = run([script, '--tests', '--quiet'], clean)
    st2, out2 = run([build_script, 'check-tools', '--tests', '--quiet'],
                    clean)
    check('strata-build: check-tools needs no build directory and gives the '
          'same report and status as the script', st1 == st2 and
          out1 == out2 and 'build directory' not in out2, out2)
    st, out = run([build_script, 'check-tools', '--tests'], full)
    check('strata-build: the options reach the script (a machine with every '
          'tool of the tests ends with status 0)', st == 0, out)
finally:
    shutil.rmtree(wk, ignore_errors=True)

print('checktools: %s' % ('%d FAILURES' % fails if fails else 'all passed'))
sys.exit(1 if fails else 0)
