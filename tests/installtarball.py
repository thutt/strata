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

"""The install tarball is a build artifact made into the build directory.

    python3 -I -B tests/installtarball.py

The variable STRATA_BUILD_DIR names a built build directory and
STRATA_BUILD_TYPE its type (debug when it is not set).  The test runs the
script scripts/strata-install-tarball and checks that:

  - the tarball strata-NAME.tar.xz and its checksum file are made in the
    build directory, NAME is the version of the build made safe for a file
    name (with the build type when it is not release), and the checksum
    file passes sha256sum -c;
  - the tarball has one top-level directory, strata-NAME, that holds the
    install script, the payload and INSTALL.txt, and holds no manifest, no
    source and no specification of the file formats;
  - two runs give the same bytes, also when a second passes between them;
  - the tarball is unpacked and installed as an ordinary user with a private
    home directory, the installed program reports the version in the name,
    and the uninstall removes everything;
  - starting the install script with sh gives a message about bash and the
    status 1, and the test for bash comes before the set lines;
  - INSTALL.txt names the tarball, tells the reader to use bash and never
    sh, and has only ASCII characters and lines of at most 80 columns;
  - a build with no supported version makes no tarball and is no failure.

The checks are made again on copies of the scripts that have been spoiled,
one rule at a time, and every copy has to fail a check."""
import sys as _s; _s.dont_write_bytecode = True
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time

here = os.path.dirname(os.path.abspath(__file__))
tree = os.path.dirname(here)
project = tree
scripts = os.path.join(project, 'scripts')
build = os.environ.get('STRATA_BUILD_DIR', '')
btype = os.environ.get('STRATA_BUILD_TYPE', 'debug')

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


def run(cmd, env, cwd, timeout=600):
    """Run a command; return (status, output of both streams)."""
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       env=env, cwd=cwd, timeout=timeout)
    return r.returncode, r.stdout.decode('utf-8', 'replace')


def sha_of(path):
    st, out = run(['sha256sum', path], os.environ, '/')
    return out.split()[0] if st == 0 else ''


if os.getuid() == 0:
    print('installtarball: skipped: the test must run as an ordinary user')
    sys.exit(0)

if not build or not os.path.isdir(build):
    print('installtarball: STRATA_BUILD_DIR must name the build directory')
    sys.exit(2)

tmp = tempfile.mkdtemp(prefix='tb.', dir=os.environ.get('TMPDIR') or '/tmp')
home = os.path.join(tmp, 'home')
work = os.path.join(tmp, 'work')
os.makedirs(home)
os.makedirs(work)
os.makedirs(os.path.join(tmp, 'tmp'))
env = {'PATH': os.environ['PATH'], 'HOME': home, 'LANG': 'C',
       'TMPDIR': os.path.join(tmp, 'tmp'),
       'SOURCE_DATE_EPOCH': '1700000000'}

# The list of the manuals that a release delivers: the one list of the install
# script.
st, out = run([os.path.join(scripts, 'strata-install'), '--list-manuals'],
              env, work)
listed_docs = [ln.split()[1] for ln in out.split('\n')
               if ln.startswith('document ')]
listed_pdfs = [ln.split()[1] for ln in out.split('\n')
               if ln.startswith('pdf ')]
FAKE_PDF = '%PDF-1.4\n% a stand-in for a manual\n%%EOF\n'


def fake_build(path, pdfs=None, manual=True):
    """A build directory of stand-ins: the programs, the help file and the
    version of the real build (symbolic links), and the PDF files of the
    manuals that pdfs names (all of them by default) as files of a few bytes.
    The real build directory is not changed, so that the manual that a
    developer has made there stays as it is."""
    os.makedirs(path)
    for n in ('term', 'x11', 'strata.hlp', 'version'):
        if os.path.exists(os.path.join(build, n)):
            os.symlink(os.path.join(build, n), os.path.join(path, n))
    if manual:
        os.makedirs(os.path.join(path, 'manual', 'pdf'))
        for n in (listed_pdfs if pdfs is None else pdfs):
            with open(os.path.join(path, 'manual', 'pdf', n), 'w') as f:
                f.write(FAKE_PDF)

    return path


# From here on the tarball is made from a build directory of stand-ins; the
# plain build, which writes into the real build directory, is checked there.
realbuild = build
fbroot = os.path.join(tmp, 'fbroot')
build = fake_build(os.path.join(fbroot, btype))

version = open(os.path.join(build, 'version')).readline().strip()
safe = re.sub(r'[^A-Za-z0-9._-]+', '-', version)
if btype != 'release':
    safe += '-' + btype
name = 'strata-' + safe
tarball = os.path.join(build, name + '.tar.xz')
sumfile = tarball + '.sha256'


def make(script_dir=scripts, e=None):
    """Make the tarball with the script of script_dir; return the status,
    the output and the checksum of the tarball."""
    st, out = run([os.path.join(script_dir, 'strata-install-tarball'), build,
                   btype], e if e is not None else env, work)
    return st, out, sha_of(tarball) if os.path.exists(tarball) else ''


# ---- the tarball and its checksum file -------------------------------------
for p in (tarball, sumfile):
    if os.path.exists(p):
        os.remove(p)
st, out, first = make()
check('make: the tarball is made in the build directory (%s)' %
      os.path.basename(tarball), st == 0 and os.path.isfile(tarball), out)
check('make: the checksum file is made next to it and sha256sum -c passes '
      'with the file names relative to the build directory',
      os.path.isfile(sumfile) and
      run(['sha256sum', '-c', os.path.basename(sumfile)], env, build)[0] == 0,
      open(sumfile).read() if os.path.exists(sumfile) else out)
check('make: no signature file is made by the build',
      not os.path.exists(tarball + '.asc'))
check('make: nothing was written below the project folder',
      not [n for n in os.listdir(scripts) if n.endswith('.xz')])

# ---- the tarball is made only on request -----------------------------------
# The environment of the sourced setup script is needed by strata-build.
benv = dict(os.environ)
build_script = os.path.join(scripts, 'strata-build')


def tarballs(directory=None):
    d = directory or build
    return sorted(n for n in os.listdir(d) if n.endswith('.tar.xz'))


# Every tarball of an earlier run (another commit has another name) goes
# first: the plain build must leave none.
for n in os.listdir(realbuild):
    if n.startswith('strata-') and (n.endswith('.tar.xz') or
                                    n.endswith('.tar.xz.sha256')):
        os.remove(os.path.join(realbuild, n))
st, out = run([build_script], benv, work)
check('request: a plain strata-build (the target all) leaves no tarball in '
      'the build directory', st == 0 and tarballs(realbuild) == [],
      (st, tarballs(realbuild)))

# The build writes the file version again when the commit has changed since
# the programs were built; the name of the tarball follows the file.
version = open(os.path.join(realbuild, 'version')).readline().strip()
safe = re.sub(r'[^A-Za-z0-9._-]+', '-', version)
if btype != 'release':
    safe += '-' + btype
name = 'strata-' + safe
tarball = os.path.join(build, name + '.tar.xz')
sumfile = tarball + '.sha256'

# The commands that make the tarball use the build directory of stand-ins.
renv = dict(benv)
benv = dict(benv, STRATA_BUILD_ROOT=fbroot, STRATA_BUILD_DIR=build)
st, out = run([build_script, 'install-tarball'], benv, work)
check('request: strata-build install-tarball makes the tarball and its '
      'checksum file', st == 0 and os.path.isfile(tarball) and
      os.path.isfile(sumfile), out)
# 'strata-build install' makes the same tarball and installs nothing.
for n in os.listdir(build):
    if n.startswith('strata-') and (n.endswith('.tar.xz') or
                                    n.endswith('.tar.xz.sha256')):
        os.remove(os.path.join(build, n))
ienv = dict(benv, HOME=home)
st, out = run([build_script, 'install'], ienv, work)
check('request: strata-build install makes the tarball and its checksum '
      'file in the build directory', st == 0 and os.path.isfile(tarball) and
      os.path.isfile(sumfile), out)
check('request: strata-build install installs nothing: no directory usr '
      'appears in the home directory',
      not os.path.exists(os.path.join(home, 'usr')))
# Options before the command do not change what the command is: the line
# 'strata-build -j 4 install' has been the line that installed under the home
# directory, when the script looked only at the first word.
for opts in (['-j', '4'], ['-j4'], ['--jobs=4']):
    for n in os.listdir(build):
        if n.startswith('strata-') and (n.endswith('.tar.xz') or
                                        n.endswith('.tar.xz.sha256')):
            os.remove(os.path.join(build, n))
    st, out = run([build_script] + opts + ['install'], ienv, work)
    check('request: strata-build %s install makes the tarball and installs '
          'nothing' % ' '.join(opts), st == 0 and os.path.isfile(tarball) and
          not os.path.exists(os.path.join(home, 'usr')), out[-300:])
st, out = run([build_script, '-j', 'x', 'install'], ienv, work)
check('request: a number of jobs that is not a number is refused',
      st != 0 and 'number of jobs' in out, out[-200:])
st, out = run(['make', '-C', project, 'install'],
              dict(ienv), work)
check('request: make install makes the tarball too and installs nothing',
      st == 0 and os.path.isfile(tarball) and
      not os.path.exists(os.path.join(home, 'usr')), out[-300:])
st, out = run([build_script, 'install', '--prefix', '/nonexistent/x'], ienv,
              work)
check('request: strata-build install takes no options and says where the '
      'install script is', st != 0 and 'scripts/strata-install' in out, out)
st, out = run([build_script, 'tarball'], benv, work)
check('request: strata-build tarball is not a target any more: make stops '
      'with its error for an unknown target', st != 0 and
      'No rule to make target' in out, out[-200:])
help_out = run([build_script, 'help'], benv, work)[1]
check('request: strata-build help lists the target install-tarball and says '
      'that it '
      'is made only on request', 'install-tarball' in help_out and
      'only when asked' in help_out, help_out[-300:])

# The tarball of the content checks is the one with the fixed time.
st, out, first = make()

# ---- what the tarball holds ------------------------------------------------
members = []
try:
    with tarfile.open(tarball, 'r:xz') as tf:
        members = [(m.name, m.uid, m.gid, m.mtime) for m in tf.getmembers()]
except (OSError, tarfile.TarError) as e:
    check('content: the tarball can be read', False, str(e))
names = [m[0] for m in members]
tops = sorted(set(n.split('/')[0] for n in names))
check('content: one top-level directory, %s' % name, tops == [name], tops)
check('content: every manual document and the PDF file of every manual are in '
      'the payload',
      listed_docs != [] and listed_pdfs != [] and
      all((name + '/payload/share/strata/doc/' + d) in names
          for d in listed_docs) and
      all((name + '/payload/share/strata/pdf/' + d) in names
          for d in listed_pdfs),
      [n for n in names if '/doc/' in n or '/pdf/' in n][:12])
check('content: the install script, the payload and INSTALL.txt are in it',
      all((name + '/' + n) in names for n in
          ('strata-install', 'payload/bin/stred', 'payload/bin/str',
           'payload/share/strata/strata.hlp', 'INSTALL.txt')), names[:8])
bad = [n for n in names if re.search(
    r'(/manifest$|strata-file-format|\.c$|\.h$|Makefile|/\.git|'
    r'strata-build|strata-release|strata-version|/setup$)', n)]
check('content: no manifest, no source, no Makefile, no specification of '
      'the file formats and none of the tools of the developers '
      '(strata-build, strata-release, strata-version, setup)', not bad,
      bad[:5])
check('content: every entry belongs to root and has the time of the source '
      '(SOURCE_DATE_EPOCH)', all(m[1] == 0 and m[2] == 0 and
                                  m[3] == 1700000000 for m in members),
      [m for m in members if m[3] != 1700000000][:3])
check('content: the names are sorted', names == sorted(names))

# ---- the same bytes every time ---------------------------------------------
time.sleep(2)
st, out, second = make()
check('same: a second run, two seconds later, gives the same bytes',
      st == 0 and first != '' and first == second, (first, second))

# ---- INSTALL.txt -----------------------------------------------------------
with tarfile.open(tarball, 'r:xz') as tf:
    f = tf.extractfile(name + '/INSTALL.txt')
    install_txt = f.read() if f is not None else b''
text = install_txt.decode('latin-1')
check('install text: the names of the tarball and of its checksum file are in '
      'it and no place holder is left', (name + '.tar.xz') in text and
      (name + '.tar.xz.sha256') in text and '@NAME@' not in text)
check('install text: the step to run is bash strata-install, and sh '
      'strata-install is never named', 'bash strata-install' in text and
      re.search(r'(?<![a-z])sh strata-install', text) is None)
template = open(os.path.join(scripts, 'INSTALL-USER.txt')).read()
shown = template.replace('@NAME@', 'development-1-230cb783')
check('install text: only ASCII characters, and no line over 80 columns '
      'with the name of a release such as development-1-230cb783',
      all(b < 128 for b in install_txt) and
      all(len(ln) <= 80 for ln in shown.split('\n')),
      [ln for ln in shown.split('\n') if len(ln) > 80][:2])

# ---- unpack and install as an ordinary user --------------------------------
unpack = os.path.join(tmp, 'unpack')
os.makedirs(unpack)
st, out = run(['tar', '-xJf', tarball, '-C', unpack], env, work)
check('install: the tarball unpacks with tar -xJf', st == 0, out)
image = os.path.join(unpack, name)
prefix = os.path.join(home, 'usr')
st, out = run(['bash', os.path.join(image, 'strata-install'),
               '--no-prereqs'], env, work)
check('install: bash strata-install installs as an ordinary user from the '
      'unpacked tarball', st == 0 and os.path.isdir(prefix), out)
st, out = run([os.path.join(prefix, 'bin', 'stred'), '--version'], env, work)
check('install: the installed Editor reports the version of the name (%s)' %
      version, st == 0 and out.split('\n')[0].endswith(version), out)
st, out = run(['bash', os.path.join(image, 'strata-install'), '--uninstall'],
              env, work)
left = []
for d, dirs, files in os.walk(prefix):
    left += [os.path.join(d, n) for n in files]
check('install: the uninstall removes every file', st == 0 and not left,
      left[:5] or out)

# ---- sh is refused with a message ------------------------------------------
st, out = run(['sh', os.path.join(image, 'strata-install'), '--help'], env,
              work)
check('shell: sh strata-install prints the message about bash and the '
      'status is 1', st == 1 and 'run this script with bash' in out and
      len(out.strip().split('\n')) == 1, (st, out))

# The test for bash comes before the set lines, so that a shell that stops at
# the line with set -o pipefail still prints the message.
script_text = open(os.path.join(scripts, 'strata-install')).read()
check('shell: the test for bash is placed before the line with set -o '
      'pipefail', 0 <= script_text.find('${BASH_VERSION:-}') <
      script_text.find('set -o pipefail;'),
      (script_text.find('${BASH_VERSION:-}'),
       script_text.find('set -o pipefail;')))

# ---- a build with no supported version makes no tarball --------------------
fake = os.path.join(tmp, 'fake-build')
os.makedirs(fake)
with open(os.path.join(fake, 'version'), 'w') as f:
    f.write('UNSUPPORTED BUILD CONFIGURATION\n')
st, out = run([os.path.join(scripts, 'strata-install-tarball'), fake,
               'release'], env, work)
check('version: a build with no supported version makes no tarball and the '
      'status is 0 (one line says so)', st == 0 and
      not [n for n in os.listdir(fake) if n.endswith('.xz')] and
      len(out.strip().split('\n')) == 1, out)


# ---- the manuals are required ----------------------------------------------
tree_docs = sorted(n[:-4] for n in os.listdir(os.path.join(tree, 'manual',
                                                           'doc'))
                   if n.endswith('.doc') and 'strata-file-format' not in n)
# The document of the challenge is in a folder of its own, with its pictures.
tree_docs.append('challenge/challenge')
check('manuals: the list is the documents of manual/doc that the install '
      'delivers (all but the specification of the file formats), and the '
      'document of the challenge in its own folder',
      sorted(d[:-4] for d in listed_docs) == sorted(tree_docs),
      (listed_docs, tree_docs))
check('manuals: every manual has a PDF file in the list, except the part '
      'terminal-keys that is a part of the Reference Manual',
      sorted(d[:-4] for d in listed_pdfs) ==
      sorted(n.split('/')[-1] for n in set(tree_docs) - {'terminal-keys'}),
      listed_pdfs)

COMMAND = 'scripts/strata-build manual'


def left_over(directory):
    """What a refused run must not leave in the build directory."""
    return [n for n in os.listdir(directory)
            if n.endswith('.tar.xz') or n.endswith('.sha256') or
            n == 'image']


def refused(tarball_script, directory, what, install=None):
    """Run the tarball script on a build directory with something missing;
    return (True, '') when it is refused as a fatal error that names what is
    missing and the command that makes the manuals, and leaves nothing."""
    e = dict(env)
    if install is not None:
        e['STRATA_INSTALL_SCRIPT'] = install
    st, out = run([tarball_script, directory, btype], e, work)
    problems = []
    if st != 1:
        problems.append('status %d' % st)
    if 'FATAL ERROR' not in out:
        problems.append('no FATAL ERROR')
    if what not in out:
        problems.append('does not name %s' % what)
    if COMMAND not in out:
        problems.append('does not give the command')
    if left_over(directory):
        problems.append('left over: %s' % left_over(directory))

    return (not problems, problems or out)


tarball_script = os.path.join(scripts, 'strata-install-tarball')
victim = 'cheat.pdf'

d = fake_build(os.path.join(tmp, 'f-missing'),
               [n for n in listed_pdfs if n != victim])
ok, why = refused(tarball_script, d, victim)
check('fatal: one PDF file missing: refused, the file is named and the '
      'command is given, and nothing is left', ok, why)

d = fake_build(os.path.join(tmp, 'f-empty'))
open(os.path.join(d, 'manual', 'pdf', victim), 'w').close()
ok, why = refused(tarball_script, d, victim)
check('fatal: an empty PDF file: refused, the file is named', ok, why)

d = fake_build(os.path.join(tmp, 'f-text'))
with open(os.path.join(d, 'manual', 'pdf', victim), 'w') as f:
    f.write('this is not a PDF file\n')
ok, why = refused(tarball_script, d, victim)
check('fatal: a file that does not begin with %PDF: refused, the file is '
      'named', ok, why)

d = fake_build(os.path.join(tmp, 'f-none'), manual=False)
ok, why = refused(tarball_script, d, 'strata-reference-manual.pdf')
check('fatal: the manual is not built at all: refused, every PDF file is '
      'named', ok and all(n in run([tarball_script, d, btype], env,
                                    work)[1] for n in listed_pdfs), why)

# A document that is missing from the tree of the sources: a scratch project
# with the scripts and a manual/doc that lacks one document.
sp = os.path.join(tmp, 'sp')
os.makedirs(os.path.join(sp, 'scripts'))
os.makedirs(os.path.join(sp, 'manual', 'doc'))
for n in ('strata-install-tarball', 'strata-install', 'INSTALL-USER.txt'):
    shutil.copy2(os.path.join(scripts, n), os.path.join(sp, 'scripts', n))
for n in listed_docs:
    if n != 'GettingStarted.doc':
        # A name with a folder (challenge/challenge.doc) is below manual.
        sub = 'manual' if '/' in n else os.path.join('manual', 'doc')
        os.makedirs(os.path.join(sp, sub, os.path.dirname(n)),
                    exist_ok=True)
        shutil.copy2(os.path.join(tree, sub, n), os.path.join(sp, sub, n))
d = fake_build(os.path.join(tmp, 'f-doc'))
ok, why = refused(os.path.join(sp, 'scripts', 'strata-install-tarball'), d,
                  'GettingStarted.doc')
check('fatal: a manual document missing from the sources: refused, the '
      'document is named', ok, why)

# 'strata-build install' with no manual built is fatal and gives the command.
root4 = os.path.join(tmp, 'root4')
d4 = fake_build(os.path.join(root4, btype), manual=False)
e4 = dict(benv, STRATA_BUILD_ROOT=root4, STRATA_BUILD_DIR=d4)
st, out = run([build_script, 'install'], e4, work)
check('fatal: strata-build install with no manual built is fatal, gives the '
      'command, and makes no tarball', st == 1 and 'FATAL ERROR' in out and
      COMMAND in out and not left_over(d4), out[-300:])

# strata-install itself: a note without the option, a fatal error with it.
d5 = fake_build(os.path.join(tmp, 'f-install'), manual=False)
pre5 = os.path.join(tmp, 'usr5')
st, out = run([os.path.join(scripts, 'strata-install'), '--build-dir', d5,
               '--no-prereqs', '--prefix', pre5], env, work)
check('install: without --require-manuals a missing manual is a note and the '
      'install is made', st == 0 and 'PDF files are not built' in out and
      os.path.isdir(pre5), out[-300:])
pre6 = os.path.join(tmp, 'usr6')
st, out = run([os.path.join(scripts, 'strata-install'), '--build-dir', d5,
               '--no-prereqs', '--prefix', pre6, '--require-manuals'], env,
              work)
check('install: with --require-manuals a missing manual is fatal and nothing '
      'is installed', st == 1 and 'FATAL ERROR' in out and
      not os.path.exists(pre6), out[-300:])
rel7 = os.path.join(tmp, 'rel7')
st, out = run([os.path.join(scripts, 'strata-install'), '--build-dir', d5,
               '--make-release', rel7], env, work)
check('install: a release directory made without the manuals is fatal and '
      'nothing is made', st == 1 and 'FATAL ERROR' in out and
      not os.path.exists(rel7), out[-300:])

# The check of the result: an install script that leaves a PDF file out of
# the payload (the check before the payload passes) makes a tarball that does
# not hold every manual; that is fatal, and the tarball is removed.
spoil_dir = os.path.join(tmp, 'spoiled')
os.makedirs(spoil_dir)


def spoiled_install(fname, old, new):
    """A copy of the install script with one rule spoiled, that still takes
    the documents, the templates and the samples from the real source tree;
    return its path, or None when the text of the rule is not found."""
    data = open(os.path.join(scripts, 'strata-install')).read()
    if old not in data:
        return None
    data = data.replace(old, new, 1)
    data = data.replace('tree_dir="${project_dir}"', 'tree_dir="%s"' % tree, 1)
    path = os.path.join(spoil_dir, fname)
    open(path, 'w').write(data)
    os.chmod(path, 0o755)

    return path


INSTALL_LINE = ('    install -m 755 -- "${BASH_SOURCE[0]}" '
                '"${out}/strata-install"')
drop = spoiled_install(
    'drops-a-pdf', INSTALL_LINE,
    '    rm -f -- "${out}/payload/share/strata/pdf/cheat.pdf"\n' +
    INSTALL_LINE)


def result_checked(tarball_script_path):
    """True when a tarball with a manual left out is refused and removed."""
    d = fake_build(tempfile.mkdtemp(prefix='f-', dir=tmp) + '/b')
    e = dict(env, STRATA_INSTALL_SCRIPT=drop)
    st, out = run([tarball_script_path, d, btype], e, work)

    return (st == 1 and 'does not hold every manual' in out and
            'cheat.pdf' in out and not left_over(d))


check('result: a tarball that lacks a manual is fatal and is removed',
      drop is not None and result_checked(tarball_script), drop)

# ---- the mutations ---------------------------------------------------------
mut = os.path.join(tmp, 'mut', 'scripts')
os.makedirs(mut)
for n in ('strata-install-tarball', 'strata-install', 'INSTALL-USER.txt'):
    shutil.copy2(os.path.join(scripts, n), os.path.join(mut, n))
# The spoiled tarball script uses the real install script, which needs the
# tree of the project; the spoiled install script is only started with sh.
menv = dict(env, STRATA_INSTALL_SCRIPT=os.path.join(scripts,
                                                    'strata-install'))


def spoil(fname, old, new):
    """Spoil one rule in the copy of a script; return False when the text of
    the rule is not found."""
    p = os.path.join(mut, fname)
    data = open(p).read()
    if old not in data:
        return False
    open(p, 'w').write(data.replace(old, new, 1))
    return True


def restore(fname):
    shutil.copy2(os.path.join(scripts, fname), os.path.join(mut, fname))


# The time of the entries is not set: two runs a second apart differ.
if spoil('strata-install-tarball', '"--mtime=@${epoch}" ', ''):
    st, out, a = make(mut, menv)
    time.sleep(2)
    st2, out2, b = make(mut, menv)
    check('mutation: a tarball made without the fixed time of the entries is '
          'not the same twice', a != '' and b != '' and a != b)
else:
    check('mutation: the text of the time option is in the script', False)
restore('strata-install-tarball')

# INSTALL.txt tells the reader to use sh.
if spoil('INSTALL-USER.txt', 'bash strata-install\n\n   An ordinary',
         'sh strata-install\n\n   An ordinary'):
    make(mut, menv)
    with tarfile.open(tarball, 'r:xz') as tf:
        f = tf.extractfile(name + '/INSTALL.txt')
        text2 = f.read().decode('latin-1') if f is not None else ''
    check('mutation: an INSTALL.txt that says sh strata-install fails the '
          'check of the text', re.search(r'(?<![a-z])sh strata-install',
                                         text2) is not None)
else:
    check('mutation: the text of the instruction is in INSTALL-USER.txt',
          False)
restore('INSTALL-USER.txt')

# The check for bash is taken out of the install script.
if spoil('strata-install', 'if [ -z "${BASH_VERSION:-}" ]; then',
         'if false; then'):
    st, out = run(['sh', os.path.join(mut, 'strata-install'), '--help'], env,
                  work)
    check('mutation: an install script without the check for bash does not '
          'print the message', 'run this script with bash' not in out)
else:
    check('mutation: the text of the check is in the install script', False)

# The default build is changed to make the tarball: the plain build leaves
# one, which the check of the request refuses.
mb = os.path.join(tmp, 'mut', 'strata-build')
shutil.copy2(build_script, mb)
data = open(mb).read()
spot = '        echo "  X11:      ${build}/x11/stred and ${build}/x11/str"\n'
if spot in data:
    open(mb, 'w').write(data.replace(
        spot, spot + '        "${STRATA_ROOT}/scripts/strata-install-tarball" '
        '"${STRATA_TEST_FAKE_BUILD}" "${STRATA_BUILD_TYPE:-debug}"\n', 1))
    # The spoiled default build makes the tarball of the build directory of
    # stand-ins, which holds the manuals, so that the tarball is made.
    for n in tarballs(build):
        os.remove(os.path.join(build, n))
    st, out = run([mb], dict(renv, STRATA_TEST_FAKE_BUILD=build), work)
    check('mutation: a build script that makes the tarball in the default '
          'build makes one, which the check of the request refuses',
          tarballs(build) != [])
    for n in os.listdir(realbuild):
        if n.startswith('strata-') and (n.endswith('.tar.xz') or
                                        n.endswith('.tar.xz.sha256')):
            os.remove(os.path.join(realbuild, n))
else:
    check('mutation: the text of the end of the default build is in the '
          'build script', False)

# The check for bash is moved after the set lines: the check of the place
# fails.
p = os.path.join(mut, 'strata-install')
restore('strata-install')
data = open(p).read()
chk = data[data.find('if [ -z "${BASH_VERSION:-}" ]; then'):]
chk = chk[:chk.find('fi\n') + 3]
moved = data.replace(chk + '\n', '', 1).replace(
    'set -o errexit;\n', 'set -o errexit;\n\n' + chk, 1)
open(p, 'w').write(moved)
mt = open(p).read()
check('mutation: an install script with the test for bash after the set '
      'lines fails the check of the place', not (0 <= mt.find(
          '${BASH_VERSION:-}') < mt.find('set -o pipefail;')))
restore('strata-install')


# ---- the mutations of the rule about the manuals ---------------------------
layer1 = ('"${install_script}" --build-dir "${build_dir}" --check-manuals || '
          'exit 1\n')
restore('strata-install-tarball')
data = open(os.path.join(mut, 'strata-install-tarball')).read()
start = data.find('if [ -n "${absent}" ]; then')
end = data.find('\nfi\n', start) + 4
layer3_gone = data[:start] + data[end:]
layer1_gone = data.replace(layer1, '', 1)
check('mutation: the texts of the checks are in the tarball script',
      layer1 in data and start > 0 and end > start)


def copy_tarball_script(text, fname):
    path = os.path.join(spoil_dir, fname)
    open(path, 'w').write(text)
    os.chmod(path, 0o755)
    shutil.copy2(os.path.join(scripts, 'INSTALL-USER.txt'),
                 os.path.join(spoil_dir, 'INSTALL-USER.txt'))

    return path


# The check of the result removed: the tarball with a manual left out stays.
no3 = copy_tarball_script(layer3_gone, 'strata-install-tarball')
check('mutation: a tarball script without the check of the result makes the '
      'tarball that lacks a manual, so the test of the result fails',
      not result_checked(no3))

# The checks before the payload removed, and the option not passed: only the
# check of the result is left, and it does not give the command.
no1 = copy_tarball_script(layer1_gone, 'strata-install-tarball')
no2 = spoiled_install('without-option',
                      '--no-prereqs --quiet --require-manuals',
                      '--no-prereqs --quiet')
d = fake_build(os.path.join(tmp, 'f-mut-a'),
               [n for n in listed_pdfs if n != victim])
check('mutation: a tarball script without the check before the payload, and '
      'an install script that does not pass --require-manuals, still stop '
      'but not with the message that gives the command, so the test fails',
      no2 is not None and not refused(no1, d, victim, no2)[0])

# The option not passed: a release directory is made without the manuals.
d = fake_build(os.path.join(tmp, 'f-mut-b'),
               [n for n in listed_pdfs if n != victim])
rel8 = os.path.join(tmp, 'rel8')
st, out = run([no2, '--build-dir', d, '--make-release', rel8], env, work)
check('mutation: an install script that does not pass --require-manuals makes '
      'a release directory without a PDF file, so the check fails',
      no2 is not None and st == 0 and os.path.exists(rel8))

# The documents of the manual build are installed in place of the committed
# ones: a build of a release tag has its version number on the title page,
# and a committed document says "development".
bd = fake_build(os.path.join(tmp, 'f-built'))
os.makedirs(os.path.join(bd, 'manual', 'doc'))
marker = b'built from the tree that is installed\n'
for n in ('errata.doc', 'challenge.doc'):
    with open(os.path.join(bd, 'manual', 'doc', n), 'wb') as f:
        f.write(marker)
dest = os.path.join(tmp, 'f-built-dest')
st, out = run([os.path.join(scripts, 'strata-install'), '--build-dir', bd,
               '--destdir', dest, '--prefix', '/usr', '--no-prereqs',
               '--quiet'], env, work)
inst = os.path.join(dest, 'usr', 'share', 'strata', 'doc')


def same(a, b):
    with open(a, 'rb') as fa, open(b, 'rb') as fb:
        return fa.read() == fb.read()


check('install: a document of the manual build is installed in place of the '
      'committed one, in doc and in doc/challenge',
      st == 0 and
      open(os.path.join(inst, 'errata.doc'), 'rb').read() == marker and
      open(os.path.join(inst, 'challenge', 'challenge.doc'),
           'rb').read() == marker, out[-300:])
check('install: a document that the build does not have is the committed one',
      same(os.path.join(inst, 'cheat.doc'),
           os.path.join(tree, 'manual', 'doc', 'cheat.doc')))

# The tarball of the mutations is not kept: make it again as it should be,
# with the time of the commit and not the time of this test.
plain = dict(env)
del plain['SOURCE_DATE_EPOCH']
make(scripts, plain)
shutil.rmtree(tmp)
print('installtarball: %s' % ('all passed' if not fails else
                       '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
