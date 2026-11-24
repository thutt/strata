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

"""The install script (scripts/strata-install) puts the programs under a
prefix and removes them again, and the installed programs run from there
without the build directory.

    python3 -I -B tests/installtest.py

The variable STRATA_BUILD_DIR names the build directory.  The test installs
into a made prefix below TMPDIR (never anywhere else, and never with sudo),
reads the manifest, runs the check, then runs the installed programs in a
private view of the file system (bwrap) in which the build directory is empty
and the prefix is mounted at another place, with none of the variables that
tell where a file is: the help file, the Editor and the Printer must all be
found next to the installed program.  Last it uninstalls and checks that
nothing of the install is left.  When the system does not let a program make
such a view, the programs are run in the prefix itself and the test says so."""
import sys as _s; _s.dont_write_bytecode = True
import os
import shutil
import stat
import subprocess
import time
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
tree = os.path.dirname(here)
project = os.path.dirname(tree)
# The variable STRATA_INSTALL_SCRIPT names another copy of the script (a
# file in the scripts folder), which is how the mutations of the tests
# are tried.
script = os.path.join(project, 'scripts', os.environ.get(
    'STRATA_INSTALL_SCRIPT', 'strata-install'))
sys.path.insert(0, here)
import tui
import mktemplates
import labeldata
import mksamples

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


def run(cmd, env=None, cwd=None, timeout=300):
    """Run a command; return (status, output of both streams)."""
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       env=env, cwd=cwd, timeout=timeout)
    return r.returncode, r.stdout.decode('utf-8', 'replace')


def files_under(top):
    """Every file and directory below top, as paths relative to top."""
    out = set()
    for d, dirs, files in os.walk(top):
        for n in dirs + files:
            out.add(os.path.relpath(os.path.join(d, n), top))
    return out


# The packages of the simulated apt: name, version, download size in bytes,
# installed size in kilobytes.  The sizes are made so that the totals can be
# worked out here and compared with the printed ones.
STUB_PACKAGES = [
    ('texlive-latex-base', '2024.1', 2000000, 5000),
    ('texlive-latex-recommended', '2024.1', 1000000, 2048),
    ('texlive-latex-extra', '2024.1', 3000000, 4096),
    ('texlive-pictures', '2024.1', 500000, 1024),
    ('texlive-plain-generic', '2024.1', 100000, 512),
    ('lmodern', '2.005', 8000000, 10240),
    ('tex-common', '6.18', 100000, 200),
    ('libkpathsea6', '2024.2', 300000, 600),
    ('dvipng', '1.17', 150000, 300),
    ('ghostscript', '10.0.0', 5000000, 20000),
    ('hunspell', '1.7.2', 80000, 300),
    ('hunspell-en-us', '1:2020.12', 600000, 900),
    ('mythes-en-us', '3.0', 400000, 1200),
]
STUB_DEPS = {'texlive-latex-base': ['tex-common', 'libkpathsea6'],
             'dvipng': ['libkpathsea6'], 'hunspell-en-us': ['hunspell']}

STUB_APT_GET = '''#!%(py)s
import os, sys
args = sys.argv[1:]
log = os.environ['STUB_LOG']
open(log, 'a').write('apt-get ' + ' '.join(args) + '\\n')
pk = %(pk)r
deps = %(deps)r
if 'update' in args:
    sys.exit(0)
if '-s' in args:
    names = args[args.index('install') + 1:]
    seen = []
    for n in names:
        for m in [n] + deps.get(n, []):
            if m not in seen:
                seen.append(m)
    for m in seen:
        v = [x[1] for x in pk if x[0] == m][0]
        print('Inst %%s (%%s Debian:stable [amd64])' %% (m, v))
    sys.exit(0)
STALE = ('Err:1 http://archive.ubuntu.com/ubuntu noble-updates/main amd64 '
         'util-linux amd64 2.39.3-9ubuntu6.4\\n  404  Not Found '
         '[IP: 91.189.92.23 80]\\nE: Failed to fetch http://archive.ubuntu.'
         'com/ubuntu/pool/main/u/util-linux/util-linux_2.39.3-9ubuntu6.4_'
         'amd64.deb  404  Not Found [IP: 91.189.92.23 80]\\nE: Unable to '
         'fetch some archives, maybe run apt-get update or try with '
         '--fix-missing?\\n')
def fail_now(kind, counter):
    n = int(open(counter).read()) if os.path.exists(counter) else 0
    open(counter, 'w').write(str(n + 1))
    mode, _, times = kind.partition(':')
    if mode and n < int(times or '1'):
        if mode == '404':
            sys.stderr.write(STALE)
        else:
            sys.stderr.write("E: You don't have enough free space in "
                             "/var/cache/apt/archives/.\\n")
        sys.exit(100)
if 'download' in args:
    fail_now(os.environ.get('STUB_DOWNLOAD_FAIL', ''), log + '.download')
    for n in args[args.index('download') + 1:]:
        v = [x[1] for x in pk if x[0] == n][0]
        open('%%s_%%s_amd64.deb' %% (n, v.replace(':', '%%3a')), 'w').write('x')
    sys.exit(0)
if 'install' in args:
    fail_now(os.environ.get('STUB_INSTALL_FAIL', ''), log + '.install')
    sys.exit(0)
sys.exit(1)
'''

STUB_APT_CACHE = '''#!%(py)s
import sys
pk = %(pk)r
spec = sys.argv[-1]
name, _, ver = spec.partition('=')
for n, v, size, inst in pk:
    if n == name and (ver == '' or ver == v):
        print('Package: %%s' %% n)
        print('Version: %%s' %% v)
        print('Installed-Size: %%d' %% inst)
        print('Size: %%d' %% size)
        print()
        sys.exit(0)
sys.exit(1)
'''

STUB_DPKG_DEB = '''#!%(py)s
import os, sys
os.makedirs(sys.argv[3] + '/usr/share/stub', exist_ok=True)
open(sys.argv[3] + '/usr/share/stub/' + os.path.basename(sys.argv[2]),
     'w').write('x')
'''


def human(b):
    """The size as the script prints it: kilobytes rounded up, or megabytes
    with one decimal."""
    if b >= 1048576:
        return '%.1f MB' % (b / 1048576)
    return '%d kB' % ((b + 1023) // 1024)


def run_pty(cmd, answer, env):
    """Run a command with a terminal as its input and the answer already
    typed, as a user at a keyboard would; return (status, output)."""
    master, slave = os.openpty()
    os.write(master, answer.encode())
    r = subprocess.run(cmd, stdin=slave, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, env=env, timeout=300)
    os.close(master)
    os.close(slave)
    return r.returncode, r.stdout.decode('utf-8', 'replace')


def prereq_question_test():
    """An administrator's install shows every package with its estimated size
    and asks "Install these packages? [y/N]" before it installs anything.  A
    simulated apt (a directory of small programs first in the PATH) gives a
    fixed resolution and fixed sizes, and a simulated id says that the user
    is an administrator, so the test needs no network, no privilege and
    installs no package."""
    wk = tempfile.mkdtemp(prefix='instq.')
    try:
        fake, log, env = stub_machine(wk)
        rootbin = os.path.join(wk, 'rootbin')
        stub_program(rootbin, 'id', '#!/bin/sh\necho 0\n')
        env['PATH'] = rootbin + ':' + fake

        # The packages the script requires on a machine with none of the
        # programs; the thesaurus only when this machine has none.
        wanted = [x for x in STUB_PACKAGES
                  if not (x[0] == 'mythes-en-us' and
                          os.path.isdir('/usr/share/mythes'))]
        total_down = sum(x[2] for x in wanted)
        total_inst = sum(x[3] * 1024 for x in wanted)
        n_pk = len(wanted)

        def installed():
            return (os.path.exists(log) and
                    'apt-get install -y' in open(log).read())

        def reset(pre):
            if os.path.exists(log):
                os.unlink(log)
            shutil.rmtree(pre, ignore_errors=True)

        # ------------------------------------------------ the list alone
        pre = os.path.join(wk, 'usr', 'install')
        reset(pre)
        st, out = run([script, '--list-prereqs', '--prefix', pre], env=env)
        lines = out.split('\n')
        check('question: --list-prereqs ends with status 0', st == 0, out)
        check('question: the table says that the sizes are estimates from '
              'the package index', 'estimates from the package index' in out,
              out)
        bad = []
        for name, ver, size, inst in wanted:
            row = [l for l in lines if l.startswith(name + ' ')]
            want = [human(size), human(inst * 1024)]
            if len(row) != 1 or any(w not in row[0] for w in want) or \
               ver not in row[0]:
                bad.append((name, row, want))
        check('question: every package is on its own row with its version, '
              'its download size and its installed size', not bad, bad)
        check('question: libkpathsea6, needed by two groups, is listed once',
              len([l for l in lines if l.startswith('libkpathsea6 ')]) == 1,
              out)
        tot = [l for l in lines if l.startswith('TOTAL')]
        check('question: the total row has the number of packages and the '
              'sums of the sizes (%d, %s, %s)' % (n_pk, human(total_down),
              human(total_inst)),
              len(tot) == 1 and ('TOTAL, %d packages' % n_pk) in tot[0] and
              human(total_down) in tot[0] and human(total_inst) in tot[0],
              tot)
        check('question: --list-prereqs installs nothing',
              not installed() and not os.path.exists(pre), out)

        # ------------------------------------- no terminal and no --yes
        reset(pre)
        st, out = run([script, '--prefix', pre, '--build-dir', build,
                       '--with-prereqs'], env=env)
        check('question: without a terminal and without --yes the status '
              'is 4 and --yes is named', st == 4 and '--yes' in out, out)
        check('question: and nothing was installed',
              not installed() and not os.path.exists(pre), out)

        # ------------------------------------------------- the answers
        cmd = [script, '--prefix', pre, '--build-dir', build,
               '--with-prereqs']
        for answer, what in (('n\n', 'n'), ('\n', 'an empty answer'),
                             ('yes please\n', 'any other word')):
            reset(pre)
            st, out = run_pty(cmd, answer, env)
            check('question: %s declines: status 3, nothing installed' %
                  what, st == 3 and not installed() and
                  not os.path.exists(pre) and 'declined' in out, out)
        reset(pre)
        st, out = run_pty(cmd, 'n\n', env)
        check('question: the question is "Install these packages? [y/N]"',
              'Install these packages? [y/N]' in out, out)
        check('question: the list is shown before the question',
              0 <= out.find('PACKAGE') < out.find('[y/N]'), out)
        for answer in ('y\n', 'Y\n'):
            reset(pre)
            st, out = run_pty(cmd, answer, env)
            names = open(log).read() if os.path.exists(log) else ''
            check('question: %s goes on: apt installs every package listed '
                  'and Strata is installed, status 0' % answer.strip(),
                  st == 0 and all(x[0] in names for x in wanted) and
                  os.path.isfile(os.path.join(pre, 'bin', 'stred')),
                  out + names)

        # ---------------------------------------------------------- --yes
        reset(pre)
        st, out = run(cmd + ['--yes'], env=env)
        check('question: --yes installs without asking, status 0',
              st == 0 and installed() and '[y/N]' not in out,
              'status %d\n%s' % (st, out[-600:]))
        check('question: --yes still shows the list',
              'TOTAL, %d packages' % n_pk in out, out)
        st, out = run([script, '--prefix', pre, '--uninstall', '--quiet'],
                      env=env)
        check('question: the uninstall removes what --yes put there',
              st == 0 and not os.path.exists(pre), out)

        # --------------------------------------------- the check says so
        st, out = run([script, '--check', '--build-dir', build], env=env)
        check('question: --check tells that the question is asked and that '
              '--no-prereqs leaves it out',
              '--no-prereqs' in out and 'the question is asked' in out, out)
    finally:
        shutil.rmtree(wk, ignore_errors=True)


def stub_machine(wk):
    """A machine with none of the programs that Strata needs and a simulated
    apt (the stubs above): a directory of small programs is the whole PATH.
    Returns the directory, the log of the calls of apt and the environment."""
    fake = os.path.join(wk, 'bin')
    os.makedirs(fake)
    for n in ('uniq expr od comm fold stat getconf perl bash sh yes cp '
              'tail cat awk mawk gawk ls grep head chmod mktemp install '
              'dirname basename rm rmdir mkdir tr sed readlink sort env '
              'ldd tar gzip xz zstd uname id ln mv touch date true false '
              'test sleep wc cut find xargs printf tee').split():
        src = shutil.which(n)
        if src is not None:
            os.symlink(src, os.path.join(fake, n))
    fmt = {'py': sys.executable, 'pk': STUB_PACKAGES, 'deps': STUB_DEPS}
    for name, text in (('apt-get', STUB_APT_GET),
                       ('apt-cache', STUB_APT_CACHE),
                       ('dpkg-deb', STUB_DPKG_DEB)):
        path = os.path.join(fake, name)
        open(path, 'w').write(text % fmt)
        os.chmod(path, 0o755)
    log = os.path.join(wk, 'apt.log')
    hm = os.path.join(wk, 'home')
    os.makedirs(hm)
    env = {'PATH': fake, 'HOME': hm, 'TMPDIR': wk, 'STUB_LOG': log}
    return fake, log, env


def stub_program(directory, name, text='#!/bin/sh\nexit 0\n'):
    """Put a small program called NAME in a directory."""
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, name)
    open(path, 'w').write(text)
    os.chmod(path, 0o755)


def prereq_default_test():
    """The install looks for the programs that Strata needs by itself: it
    shows what is present and what is missing, the packages with their
    sizes, asks, and installs them; as an administrator with apt, otherwise
    into the prefix.  --no-prereqs turns that off.  A user who cannot write
    to the prefix is told to use a prefix in the home directory.  The
    machine and apt are simulated, so no network is used."""
    wk = tempfile.mkdtemp(prefix='instd.')
    try:
        fake, log, env = stub_machine(wk)
        wanted = [x for x in STUB_PACKAGES
                  if not (x[0] == 'mythes-en-us' and
                          os.path.isdir('/usr/share/mythes'))]
        names = [x[0] for x in wanted]
        thes = 'present' if os.path.isdir('/usr/share/mythes') else 'missing'

        def reset(pre):
            if os.path.exists(log):
                os.unlink(log)
            shutil.rmtree(pre, ignore_errors=True)

        def called(word):
            return os.path.exists(log) and (' %s ' % word) in open(log).read()

        pre = os.path.join(wk, 'usr', 'install')

        rootbin = os.path.join(wk, 'rootbin')
        stub_program(rootbin, 'id', '#!/bin/sh\necho 0\n')
        env_root = dict(env)
        env_root['PATH'] = rootbin + ':' + fake

        # --------------------------------- the default looks for them
        reset(pre)
        st, out = run([script, '--prefix', pre, '--build-dir', build],
                      env=env_root)
        check('default: with no option an administrator\'s install looks '
              'for the programs that are missing: with no terminal and no '
              '--yes it stops with status 4 and names --yes',
              st == 4 and '--yes' in out, out)
        check('default: nothing was installed before the answer',
              not os.path.exists(pre) and not called('install -y'), out)
        reset(pre)
        st, out = run([script, '--prefix', pre, '--build-dir', build,
                       '--yes'], env=env_root)
        check('default: --yes installs what is missing with apt, status 0',
              st == 0 and called('install -y') and
              os.path.isfile(os.path.join(pre, 'bin', 'stred')),
              out[-600:])
        reset(pre)
        st, out = run([script, '--prefix', pre, '--build-dir', build,
                       '--no-prereqs', '--quiet'], env=env)
        check('default: --no-prereqs installs Strata alone: status 0, apt '
              'is not called and the programs are in place',
              st == 0 and not os.path.exists(log) and
              os.path.isfile(os.path.join(pre, 'bin', 'stred')), out)
        check('default: --no-prereqs leaves no tree of prerequisites',
              not os.path.exists(os.path.join(pre, 'lib', 'strata')), out)
        reset(pre)

        # ------------------------------------ present and missing
        st, out = run([script, '--list-prereqs', '--prefix', pre],
                      env=env_root)
        lines = out.split('\n')
        check('status: the list says what Strata needs to run',
              'What Strata needs to run:' in out, out)

        def row(label):
            r = [l for l in lines if label in l and l.startswith('  ')]
            return r[0] if len(r) == 1 else ''

        check('status: on a machine with none of the programs LaTeX, '
              'dvipng, ghostscript and the spell check are missing',
              all(row(x).split()[:1] == ['missing'] for x in
                  ('LaTeX', 'dvipng', 'ghostscript', 'spell check')),
              out)
        check('status: the thesaurus is %s as this machine shows' % thes,
              row('thesaurus').split()[:1] == [thes], out)
        # Ghostscript is there: it is present and is not asked for.
        gsdir = os.path.join(wk, 'gsbin')
        stub_program(gsdir, 'gs')
        env_gs = dict(env_root)
        env_gs['PATH'] = gsdir + ':' + rootbin + ':' + fake
        st, out = run([script, '--list-prereqs', '--prefix', pre],
                      env=env_gs)
        lines = out.split('\n')
        check('status: a ghostscript that is there is present',
              row('ghostscript').split()[:1] == ['present'], out)
        check('status: and its package is not in the table',
              not [l for l in lines if l.startswith('ghostscript ')] and
              'TOTAL, %d packages' % (len(wanted) - 1) in out, out)
        # LaTeX is complete: it is present and none of its packages is asked
        # for.
        fulldir = os.path.join(wk, 'fulltex')
        for n in ('pdflatex', 'latex'):
            stub_program(fulldir, n)
        stub_program(fulldir, 'kpsewhich',
                     '#!/bin/sh\nfor f in "$@"; do echo "/x/$f"; done\n'
                     'exit 0\n')
        env_full = dict(env_root)
        env_full['PATH'] = fulldir + ':' + rootbin + ':' + fake
        st, out = run([script, '--list-prereqs', '--prefix', pre],
                      env=env_full)
        lines = out.split('\n')
        check('status: LaTeX with every file of the Printer is present',
              row('LaTeX').split()[:1] == ['present'], out)
        check('status: and none of its packages is in the table',
              not [l for l in lines if l.startswith('texlive-') or
                   l.startswith('lmodern ')], out)
        # LaTeX programs are there, but not one file that the Printer requests
        # for: only the package of that file is asked for.
        texdir = os.path.join(wk, 'texbin')
        for n in ('pdflatex', 'latex'):
            stub_program(texdir, n)
        stub_program(texdir, 'kpsewhich',
                     '#!/bin/sh\nfor f in "$@"; do\n'
                     '  case "$f" in tikz.sty) ;; *) echo "/x/$f" ;; esac\n'
                     'done\nexit 0\n')
        env_tex = dict(env_root)
        env_tex['PATH'] = texdir + ':' + rootbin + ':' + fake
        st, out = run([script, '--list-prereqs', '--prefix', pre],
                      env=env_tex)
        lines = out.split('\n')
        check('status: LaTeX is missing when one file of the Printer is',
              row('LaTeX').split()[:1] == ['missing'], out)
        check('status: only the package of that file (texlive-pictures) is '
              'asked for', [l for l in lines if l.startswith('texlive-')
                            ] and all(l.startswith('texlive-pictures ')
                                      for l in lines if
                                      l.startswith('texlive-')), out)

        # ----------------------------------- as an administrator
        cmd = [script, '--prefix', pre, '--build-dir', build]
        reset(pre)
        st, out = run(cmd + ['--yes'], env=env_root)
        text = open(log).read() if os.path.exists(log) else ''
        installed = [l for l in text.split('\n')
                     if l.startswith('apt-get install -y '
                                     '--no-install-recommends ')]
        check('admin: the packages are installed with apt, status 0',
              st == 0 and len(installed) == 1 and
              all(n in installed[0] for n in names), out[-600:] + text)
        check('admin: nothing is downloaded into the prefix',
              not called('download') and
              not os.path.exists(os.path.join(pre, 'lib', 'strata')), out)
        manifest = open(os.path.join(pre, 'share', 'strata', 'manifest')
                        ).read() if os.path.exists(os.path.join(
                            pre, 'share', 'strata', 'manifest')) else ''
        check('admin: no tree of prerequisites is in the manifest',
              manifest != '' and '\nt ' not in '\n' + manifest, manifest)
        check('admin: the end says the uninstall leaves the packages',
              'installed with apt' in out and 'does not remove' in out, out)
        reset(pre)
        st, out = run_pty(cmd, 'n\n', env_root)
        check('admin: the question is "Install these packages? [y/N]", and a '
              'no installs nothing: status 3', st == 3 and
              'Install these packages? [y/N]' in out and
              not os.path.exists(pre) and
              'apt-get install -y' not in (open(log).read()
                                           if os.path.exists(log) else ''),
              out)
        late = out.partition('Optional programs are missing:')[2]
        check('admin: a no ends the output with the warning that lists the '
              'missing packages and the command of an administrator',
              'ghostscript' in late and
              out.rstrip().split('\n')[-1].strip().startswith(
                  'sudo apt-get install --no-install-recommends'), out[-400:])
        reset(pre)

        # ------------------- an old package index (the fetch fails: 404)
        def lines_of(prefix):
            text = open(log).read() if os.path.exists(log) else ''
            return [l for l in text.split('\n') if l.startswith(prefix)]

        def updates():
            return [l for l in lines_of('apt-get') if l.endswith(' update')
                    and '-o ' not in l]

        def installs():
            return lines_of('apt-get install -y')

        reset(pre)
        st, out = run(cmd + ['--yes'], env=env_root)
        check('stale: an install that works the first time never runs '
              'apt-get update', st == 0 and len(installs()) == 1 and
              updates() == [], out + open(log).read())

        env_404 = dict(env_root)
        env_404['STUB_INSTALL_FAIL'] = '404:1'
        for f in (log + '.install', log + '.download'):
            if os.path.exists(f):
                os.unlink(f)
        reset(pre)
        st, out = run(cmd + ['--yes'], env=env_404)
        check('stale: with --yes the 404 runs apt-get update without asking '
              'and tries the install once more with --fix-missing, status 0',
              st == 0 and len(updates()) == 1 and len(installs()) == 2 and
              '--fix-missing' in installs()[1] and
              '--fix-missing' not in installs()[0] and
              'Run apt-get update and try again' not in out, out)

        for f in (log + '.install', log + '.download'):
            if os.path.exists(f):
                os.unlink(f)
        reset(pre)
        st, out = run_pty(cmd, 'y\ny\n', env_404)
        check('stale: the 404 asks to run apt-get update; a y runs it once '
              'and the install is tried once more, status 0',
              st == 0 and 'The package index of this system is old' in out
              and 'Run apt-get update and try again? [y/N]' in out and
              len(updates()) == 1 and len(installs()) == 2, out)

        for f in (log + '.install', log + '.download'):
            if os.path.exists(f):
                os.unlink(f)
        reset(pre)
        st, out = run_pty(cmd, 'y\nn\n', env_404)
        by_hand = ('sudo apt-get update' in out and
                   'sudo apt-get install --no-install-recommends' in out)
        check('stale: an n stops with the two commands to run by hand, no '
              'update and one install, status 1',
              st == 1 and by_hand and updates() == [] and
              len(installs()) == 1, out)
        st_no = st

        env_disk = dict(env_root)
        env_disk['STUB_INSTALL_FAIL'] = 'disk:1'
        for f in (log + '.install', log + '.download'):
            if os.path.exists(f):
                os.unlink(f)
        reset(pre)
        st, out = run(cmd + ['--yes'], env=env_disk)
        check('stale: a failure that is not the old index (no space) does '
              'not offer the update: it is reported as it is, status 1 like '
              'the answer n', st == st_no and updates() == [] and
              len(installs()) == 1 and 'apt-get install failed' in out and
              'index of this system is old' not in out, out)

        env_twice = dict(env_root)
        env_twice['STUB_INSTALL_FAIL'] = '404:5'
        for f in (log + '.install', log + '.download'):
            if os.path.exists(f):
                os.unlink(f)
        reset(pre)
        st, out = run(cmd + ['--yes'], env=env_twice)
        check('stale: a second failure after the update stops, status 1: '
              'one update, two installs, a message that the install failed '
              'again', st == st_no and len(updates()) == 1 and
              len(installs()) == 2 and 'failed again' in out, out)

        for f in (log + '.install', log + '.download'):
            if os.path.exists(f):
                os.unlink(f)
        reset(pre)

        # ----------------------------------- who may write the prefix
        if os.geteuid() == 0:
            print('note the privilege tests are not run as an administrator')
        else:
            ro = os.path.join(wk, 'ro')
            os.makedirs(ro)
            os.chmod(ro, 0o555)
            try:
                target = os.path.join(ro, 'usr')
                st, out = run([script, '--prefix', target, '--build-dir',
                               build, '--no-prereqs'], env=env)
                check('privilege: a prefix the user cannot write stops the '
                      'install with status 1 and nothing is made',
                      st == 1 and not os.path.exists(target), out)
                check('privilege: the message names the directory, the '
                      'administrator and --prefix ${HOME}/usr',
                      ro in out and 'administrator' in out and
                      '--prefix ${HOME}/usr' in out, out)
                st, out = run([script, '--prefix', target, '--uninstall'],
                              env=env)
                check('privilege: the uninstall says the same',
                      st == 1 and 'administrator' in out, out)
            finally:
                os.chmod(ro, 0o755)
            if not os.access('/usr', os.W_OK):
                st, out = run([script, '--prefix', '/usr', '--build-dir',
                               build, '--no-prereqs'], env=env)
                check('privilege: the prefix /usr, which an ordinary user '
                      'cannot write, stops the install',
                      st == 1 and 'cannot write to /usr' in out, out)
            st, out = run([script, '--build-dir', build, '--no-prereqs',
                           '--dry-run'], env=env)
            check('privilege: a dry run needs no privilege and writes '
                  'nothing', st == 0 and 'Nothing was installed' in out,
                  out)
    finally:
        shutil.rmtree(wk, ignore_errors=True)


def user_prereq_test():
    """An ordinary user installs into ${HOME}/usr by default.  The programs
    that Strata can use are optional: when any is missing the install is
    made all the same, status 0, and a warning at the end of the output names
    the missing packages and the command of an administrator.  The machines
    are simulated by a PATH of small programs (the thesaurus folder is the
    one of this machine)."""
    if os.geteuid() == 0:
        print('note the tests of an ordinary user are not run as an '
              'administrator')
        return
    wk = tempfile.mkdtemp(prefix='instu.')
    try:
        fake, log, env = stub_machine(wk)
        hm = env['HOME']
        mythes = os.path.isdir('/usr/share/mythes')
        full = os.path.join(wk, 'full')
        for n in ('pdflatex', 'latex', 'dvipdfmx', 'dvipng', 'gs', 'aspell',
                  'lp', 'lpstat', 'curl', 'unzip'):
            stub_program(full, n)
        stub_program(full, 'kpsewhich',
                     '#!/bin/sh\nfor f in "$@"; do echo "/x/$f"; done\n'
                     'exit 0\n')
        env_full = dict(env)
        env_full['PATH'] = full + ':' + fake
        # Everything but ghostscript.
        nogs = os.path.join(wk, 'nogs')
        for n in ('pdflatex', 'latex', 'dvipdfmx', 'dvipng', 'aspell', 'lp',
                  'lpstat', 'curl', 'unzip'):
            stub_program(nogs, n)
        stub_program(nogs, 'kpsewhich',
                     '#!/bin/sh\nfor f in "$@"; do echo "/x/$f"; done\n'
                     'exit 0\n')
        env_nogs = dict(env)
        env_nogs['PATH'] = nogs + ':' + fake

        def listing(top):
            out = []
            for d, dirs, files in os.walk(top):
                out += [os.path.join(d, x) for x in dirs + files]
            return sorted(out)

        def reset():
            if os.path.exists(log):
                os.unlink(log)
            shutil.rmtree(os.path.join(hm, 'usr'), ignore_errors=True)

        home_prefix = os.path.join(hm, 'usr')
        sudo = 'sudo apt-get install --no-install-recommends'

        # --------------------- a machine with none of the programs
        reset()
        st, out = run([script, '--build-dir', build], env=env)
        check('user: with no program present the install is made, status 0',
              st == 0 and os.path.isfile(os.path.join(home_prefix, 'bin',
                                                      'stred')), out[-300:])
        head = 'Optional programs are missing:'
        early, _, late = out.partition(head)
        check('user: the warning is the end of the output and names the '
              'packages and the command of an administrator',
              head in out and 'texlive-latex-base' in late and
              'ghostscript' in late and sudo in late and
              'Nothing was installed' not in out, out[-500:])
        check('user: nothing about the missing programs is printed before '
              'the warning', not [w for w in ('missing', 'is not installed',
                                              'sudo', 'ghostscript')
                                  if w in early], early[-500:])
        check('user: apt was not called to install or to download',
              not os.path.exists(log) or
              ' install -y ' not in open(log).read(), out)
        st, out = run([script, '--uninstall', '--quiet'], env=env)
        check('user: and the install is removed again', st == 0 and
              not os.path.exists(home_prefix), out)

        # ----------------------------- one program missing: ghostscript
        reset()
        st, out = run([script, '--build-dir', build], env=env_nogs)
        early, _, late = out.partition(head)
        lines = [l for l in late.split('\n') if sudo in l]
        check('user: with ghostscript missing the install is made, status '
              '0, and the warning names only that package%s' %
              ('' if mythes else ' and the thesaurus'),
              st == 0 and len(lines) == 1 and 'ghostscript' in lines[0] and
              'texlive' not in lines[0] and 'dvipng' not in lines[0] and
              os.path.isfile(os.path.join(home_prefix, 'bin', 'stred')), out)
        custom = os.path.join(wk, 'custom', 'inst')
        reset()
        st, out = run([script, '--prefix', custom, '--build-dir', build],
                      env=env_nogs)
        check('user: a prefix on the command line follows the same rule: '
              'the install is made there, status 0, with the warning',
              st == 0 and head in out and os.path.isfile(
                  os.path.join(custom, 'bin', 'stred')) and
              not os.path.exists(home_prefix), out)
        shutil.rmtree(os.path.join(wk, 'custom'), ignore_errors=True)
        reset()
        before = listing(hm)
        st, out = run([script, '--build-dir', build, '--dry-run'],
                      env=env_nogs)
        check('user: a dry run writes nothing, status 0', st == 0 and
              listing(hm) == before, out)
        st, out = run([script, '--build-dir', build, '--list-prereqs'],
                      env=env_nogs)
        check('user: --list-prereqs only reports: status 0, ghostscript '
              'missing, nothing written', st == 0 and listing(hm) == before
              and 'ghostscript' in out, out)

        # ------------------------------------------- every program there
        reset()
        st, out = run([script, '--build-dir', build], env=env_full)
        if mythes:
            check('user: with every program there the install goes into '
                  '${HOME}/usr, status 0, and apt is not called',
                  st == 0 and
                  os.path.isfile(os.path.join(home_prefix, 'bin', 'stred'))
                  and not os.path.exists(log), out)
            st, out = run([script, '--uninstall', '--quiet'], env=env_full)
            check('user: the uninstall with no prefix works on '
                  '${HOME}/usr', st == 0 and
                  not os.path.exists(home_prefix), out)
            st, out = run([script, '--prefix', custom, '--build-dir', build,
                           '--quiet'], env=env_full)
            check('user: a prefix on the command line is used when every '
                  'program is there', st == 0 and os.path.isfile(
                      os.path.join(custom, 'bin', 'stred')) and
                  not os.path.exists(home_prefix), out)
        else:
            check('user: with only the thesaurus missing the install is made, '
                  'status 0, and the warning names mythes-en-us',
                  st == 0 and 'mythes-en-us' in out.partition(
                      'Optional programs are missing:')[2], out)

        # ------------------------------------ the explicit way around
        reset()
        st, out = run([script, '--build-dir', build, '--no-prereqs',
                       '--quiet'], env=env)
        check('user: --no-prereqs installs Strata alone into ${HOME}/usr '
              'even when every program is missing, status 0', st == 0 and
              os.path.isfile(os.path.join(home_prefix, 'bin', 'stred')), out)
        st, out = run([script, '--uninstall', '--quiet'], env=env)
        check('user: and it is removed again', st == 0 and
              not os.path.exists(home_prefix), out)

        # ------------------------- a destination root is not a user's home
        reset()
        dest = os.path.join(wk, 'dest')
        st, out = run([script, '--destdir', dest, '--build-dir', build,
                       '--quiet'], env=env)
        check('user: with --destdir and no prefix the files go under '
              'DESTDIR/usr, not into the home directory', st == 0 and
              os.path.isfile(os.path.join(dest, 'usr', 'bin', 'stred')) and
              not os.path.exists(home_prefix), out)
    finally:
        shutil.rmtree(wk, ignore_errors=True)


def installed_list_test():
    """The install puts the manuals as PDF files and the sample documents
    in place besides the programs, and ends with the list of where
    everything went: the directory, the number of files and their names."""
    wk = tempfile.mkdtemp(prefix='instl.')
    try:
        fb = os.path.join(wk, 'build')
        os.makedirs(os.path.join(fb, 'manual', 'pdf'))
        for n in ('term', 'x11'):
            if os.path.exists(os.path.join(build, n)):
                os.symlink(os.path.join(build, n), os.path.join(fb, n))
        os.symlink(os.path.join(build, 'strata.hlp'),
                   os.path.join(fb, 'strata.hlp'))
        os.symlink(os.path.join(build, 'version'),
                   os.path.join(fb, 'version'))
        for n in ('alpha.pdf', 'beta.pdf'):
            open(os.path.join(fb, 'manual', 'pdf', n), 'w').write('%PDF-1.4\n')
        pre = os.path.join(wk, 'usr', 'install')
        hm = os.path.join(wk, 'home')
        os.makedirs(hm)
        env = dict(os.environ)
        env['HOME'] = hm
        env['TMPDIR'] = wk
        st, out = run([script, '--prefix', pre, '--build-dir', fb,
                       '--no-prereqs'], env=env)
        check('list: the install with PDF files in the build directory ends '
              'with status 0', st == 0, out)
        samples = sorted(os.listdir(os.path.join(tree, 'tests', 'samples')))
        shared = os.path.join(pre, 'share', 'strata')
        check('list: the manuals as PDF files are installed with mode 644',
              all(os.path.isfile(os.path.join(shared, 'pdf', n)) and
                  stat.S_IMODE(os.stat(os.path.join(shared, 'pdf', n)
                                       ).st_mode) == 0o644
                  for n in ('alpha.pdf', 'beta.pdf')), out)
        sdir = os.path.join(shared, 'samples')
        check('list: every sample document, macro set, library and setting '
              'of tests/samples is installed',
              os.path.isdir(sdir) and
              sorted(n for n in os.listdir(sdir)
                     if os.path.isfile(os.path.join(sdir, n))) == samples,
              out)
        letters = sorted(os.listdir(os.path.join(tree, 'samples',
                                                 'letters')))
        ldir = os.path.join(sdir, 'letters')
        check('list: the sample business letters of samples/letters are '
              'installed in samples/letters with mode 644 (%d letters)'
              % len(letters), len(letters) >= 10 and os.path.isdir(ldir) and
              sorted(os.listdir(ldir)) == letters and
              all(stat.S_IMODE(os.stat(os.path.join(ldir, n)).st_mode) ==
                  0o644 for n in letters), out)
        tdocs = sorted(n for n in os.listdir(os.path.join(tree, 'manual',
                                                          'samples'))
                       if n.endswith('.doc'))
        tpics = sorted(os.listdir(os.path.join(tree, 'manual', 'samples',
                                               'pictures')))
        tdir = os.path.join(shared, 'tutorial')
        check('list: the documents of the Tutorial and their pictures are '
              'installed (the documents of manual/samples in tutorial, the '
              'pictures in tutorial/pictures)',
              len(tdocs) >= 3 and len(tpics) >= 2 and
              os.path.isdir(tdir) and
              sorted(n for n in os.listdir(tdir) if n != 'pictures') == tdocs
              and sorted(os.listdir(os.path.join(tdir, 'pictures'))) ==
              tpics, out)
        manifest = open(os.path.join(shared, 'manifest')).read()
        check('list: the documents of the Tutorial are in the manifest',
              all(('f %s/tutorial/%s\n' % (shared, n)) in manifest
                  for n in tdocs) and
              all(('f %s/tutorial/pictures/%s\n' % (shared, n)) in manifest
                  for n in tpics), manifest[:300])
        check('list: the PDF files and the samples are in the manifest',
              all(('f %s/pdf/%s\n' % (shared, n)) in manifest
                  for n in ('alpha.pdf', 'beta.pdf')) and
              all(('f %s/samples/%s\n' % (shared, n)) in manifest
                  for n in samples) and
              all(('f %s/samples/letters/%s\n' % (shared, n)) in manifest
                  for n in letters), manifest[:300])
        docs = sorted(n for n in os.listdir(os.path.join(tree, 'manual',
                                                         'doc'))
                      if n.endswith('.doc') and
                      'strata-file-format' not in n)
        tpl_top = os.path.join(tree, 'templates')
        tpls = sorted(os.path.relpath(os.path.join(d, f), tpl_top)
                      for d, _, fs in os.walk(tpl_top)
                      for f in fs if f.endswith('.doc'))
        check('list: the end lists where everything is: the directory and '
              'the number of files of each group',
              'Installed under %s' % pre in out and
              ('Documents: %d files in %s/doc' % (len(docs), shared)) in out
              and ('Manuals as PDF files: 2 files in %s/pdf' % shared)
              in out and ('Sample documents, macros and libraries: %d files '
                          'in %s/samples' % (len(samples), shared)) in out
              and all(('%s: %d files in %s/templates/%s'
                       % (what, len([x for x in tpls if x.startswith(
                           folder + os.sep)]), shared, folder)) in out
                      for folder, what in (
                          ('letters', 'Templates of New, letters'),
                          ('envelopes', 'Templates of New, envelopes'),
                          ('labels/avery/us', 'Avery labels, US'),
                          ('labels/avery/a4', 'Avery labels, A4')))
              and ('Sample business letters: %d files in %s/samples/letters'
                   % (len(letters), shared)) in out
              and ('Documents of the Tutorial: %d files in %s/tutorial' % (
                  len(tdocs), shared)) in out, out)
        check('list: the names of the files are listed', 'alpha.pdf' in out
              and 'beta.pdf' in out and samples[0] in out and docs[0] in out
              and os.path.basename(tpls[0]) in out and 'manifest' in out, out)
        st, out = run([script, '--prefix', pre, '--uninstall', '--quiet'],
                      env=env)
        check('list: the uninstall removes the PDF files and the samples '
              'too', st == 0 and not os.path.exists(os.path.join(wk, 'usr')),
              out + str(sorted(files_under(wk))[:20]))
        # Without PDF files the install says how to make them.
        shutil.rmtree(os.path.join(fb, 'manual'))
        st, out = run([script, '--prefix', pre, '--build-dir', fb,
                       '--no-prereqs'], env=env)
        check('list: without PDF files the install goes on and says that they '
              'are not installed', st == 0 and
              'are not built in' in out and 'not installed' in out and
              not os.path.exists(os.path.join(pre, 'share', 'strata',
                                              'pdf')), out)
        run([script, '--prefix', pre, '--uninstall', '--quiet'], env=env)
    finally:
        shutil.rmtree(wk, ignore_errors=True)


build = os.environ.get('STRATA_BUILD_DIR', '')
if build == '' or not os.path.isdir(os.path.join(build, 'term')):
    print('FAIL STRATA_BUILD_DIR does not name a build directory')
    sys.exit(1)
build = os.path.realpath(build)

work = tempfile.mkdtemp(prefix='instt.')
# The prefix is made by the install: it must not exist beforehand, so that
# the test sees the directories that the install makes and removes.
prefix = os.path.join(work, 'usr', 'install')
home = os.path.join(work, 'home')
os.makedirs(home)
doc = os.path.join(work, 'memo.doc')
shutil.copy(os.path.join(tree, 'tests', 'data', 'comment.doc'), doc)
base_env = dict(os.environ)
base_env['HOME'] = home
base_env['TMPDIR'] = work

try:
    # ------------------------------------------------------------ install
    st, out = run([script, '--prefix', prefix, '--build-dir', build,
                   '--no-prereqs', '--quiet'], env=base_env)
    check('install: the script ends with status 0', st == 0, out)

    manifest = os.path.join(prefix, 'share', 'strata', 'manifest')
    check('install: the manifest is installed', os.path.isfile(manifest))
    listed_files, listed_dirs = set(), set()
    if os.path.isfile(manifest):
        for line in open(manifest, encoding='ascii'):
            kind, _, path = line.rstrip('\n').partition(' ')
            if kind == 'f':
                listed_files.add(path)
            elif kind == 'd':
                listed_dirs.add(path)
    on_disk = {os.path.join(prefix, p) for p in files_under(prefix)}
    on_disk.add(prefix)
    on_disk.add(os.path.dirname(prefix))
    check('install: every file of the manifest is there',
          all(os.path.isfile(p) for p in listed_files),
          [p for p in sorted(listed_files) if not os.path.isfile(p)])
    check('install: every directory of the manifest is there',
          all(os.path.isdir(p) for p in listed_dirs))
    check('install: nothing is installed that the manifest does not list',
          on_disk == listed_files | listed_dirs,
          sorted(on_disk - (listed_files | listed_dirs))[:10])
    check('install: the prefix itself is in the manifest', prefix in
          listed_dirs)

    for prog in ('str', 'stred', 'strprint'):
        p = os.path.join(prefix, 'bin', prog)
        check('install: bin/%s is an executable file' % prog,
              os.path.isfile(p) and os.access(p, os.X_OK))
    tpl_dir = os.path.join(prefix, 'share', 'strata', 'templates')
    def tree_names(top):
        """The paths of the .doc files below top, relative to it."""
        return sorted(os.path.relpath(os.path.join(d, f), top)
                      for d, _, fs in os.walk(top) for f in fs
                      if f.endswith('.doc'))

    shipped_names = tree_names(os.path.join(tree, 'templates'))
    all_names = sorted(mktemplates.relpath(n) for n in (
        ['letter-us', 'letter-a4'] + list(mktemplates.ENVELOPES) +
        [n for n, num, s in labeldata.every()]))
    check('install: the templates are installed with their folders, with '
          'mode 644 and listed in the manifest, and they are the complete '
          'set of the generator (%d templates)' % len(all_names),
          shipped_names == all_names and
          tree_names(tpl_dir) == all_names and
          all(
              os.path.isfile(os.path.join(tpl_dir, n)) and
              os.path.join(tpl_dir, n) in listed_files and
              stat.S_IMODE(os.stat(os.path.join(tpl_dir, n)).st_mode) == 0o644
              for n in shipped_names), (shipped_names, tpl_dir))
    tpl_folders = ['envelopes', 'labels', 'labels/avery', 'labels/avery/a4',
                   'labels/avery/us', 'letters']
    check('install: the folders envelopes, labels, labels/avery, '
          'labels/avery/a4, labels/avery/us and letters are made, listed in '
          'the manifest as directories that the install made, and no other '
          'folder is below the templates',
          all(os.path.join(tpl_dir, f) in listed_dirs and
              os.path.isdir(os.path.join(tpl_dir, f)) for f in tpl_folders)
          and sorted(os.path.relpath(os.path.join(d, x), tpl_dir)
                     for d, ds, _ in os.walk(tpl_dir) for x in ds) ==
          tpl_folders, sorted(os.listdir(tpl_dir)))
    env_dir = os.path.join(prefix, 'share', 'strata', 'samples', 'envelopes')
    env_names = sorted(mksamples.sample_name(t) + '.doc'
                       for t in mktemplates.ENVELOPES)
    env_tree = os.path.join(tree, 'samples', 'envelopes')
    check('install: the sample envelopes are installed with mode 644, listed '
          'in the manifest, and the same bytes as the tree (%d samples)'
          % len(env_names), os.path.isdir(env_dir) and
          sorted(os.listdir(env_dir)) == env_names and
          all(os.path.join(env_dir, n) in listed_files and
              stat.S_IMODE(os.stat(os.path.join(env_dir, n)).st_mode) ==
              0o644 and
              open(os.path.join(env_dir, n), 'rb').read() ==
              open(os.path.join(env_tree, n), 'rb').read()
              for n in env_names if os.path.isfile(os.path.join(env_dir, n))
              and os.path.isfile(os.path.join(env_tree, n))), env_dir)
    story_dir = os.path.join(prefix, 'share', 'strata', 'samples', 'story')
    story_tree = os.path.join(tree, 'samples', 'story')
    story_files = ['strata-story.doc'] + [
        'pictures/' + n for n in sorted(os.listdir(os.path.join(
            story_tree, 'pictures')))]
    check('install: the sample document of the story and its pictures are '
          'installed with mode 644, listed in the manifest, and the same '
          'bytes as the tree (%d files)' % len(story_files),
          len(story_files) > 1 and all(
              os.path.join(story_dir, n) in listed_files and
              stat.S_IMODE(os.stat(os.path.join(story_dir, n)).st_mode) ==
              0o644 and
              open(os.path.join(story_dir, n), 'rb').read() ==
              open(os.path.join(story_tree, n), 'rb').read()
              for n in story_files
              if os.path.isfile(os.path.join(story_dir, n)) and
              os.path.isfile(os.path.join(story_tree, n))) and
          all(os.path.isfile(os.path.join(story_dir, n))
              for n in story_files), story_dir)
    fonts_dir = os.path.join(prefix, 'share', 'strata', 'samples', 'fonts')
    fonts_tree = os.path.join(tree, 'samples', 'fonts', 'fonts-sample.doc')
    fonts_made = os.path.join(fonts_dir, 'fonts-sample.doc')
    check('install: the sample of the fonts and sizes is installed with mode '
          '644, listed in the manifest, and the same bytes as the tree',
          os.path.isfile(fonts_made) and fonts_made in listed_files and
          stat.S_IMODE(os.stat(fonts_made).st_mode) == 0o644 and
          open(fonts_made, 'rb').read() == open(fonts_tree, 'rb').read(),
          fonts_dir)
    check('install: the help file and the version are installed',
          os.path.isfile(os.path.join(prefix, 'share', 'strata',
                                      'strata.hlp')) and
          os.path.isfile(os.path.join(prefix, 'share', 'strata', 'VERSION')))
    known = os.path.join(prefix, 'share', 'doc', 'strata', 'KNOWN-ISSUES.txt')
    check('install: the document of the known issues is installed and listed',
          os.path.isfile(known) and known in listed_files and
          open(known, 'rb').read() ==
          open(os.path.join(tree, 'docs', 'KNOWN-ISSUES.txt'), 'rb').read(),
          known)
    mode = stat.S_IMODE(os.stat(os.path.join(prefix, 'bin', 'stred')).st_mode)
    check('install: programs have mode 755 (install -m)', mode == 0o755,
          oct(mode))

    st, out = run([os.path.join(os.path.dirname(tree), 'scripts',
                                'strata-version'), os.path.dirname(tree)])
    version = out.strip()
    st, out = run([os.path.join(prefix, 'bin', 'stred'), '--version'])
    check('version: stred --version says the version of the branch',
          st == 0 and out.splitlines()[0] == 'stred (Strata) ' + version and
          'file format 3' in out.splitlines(), out)
    for prog in ('str', 'strprint'):
        st, out = run([os.path.join(prefix, 'bin', prog), '--version'])
        check('version: %s --version says %s' % (prog, version),
              st == 0 and version in out, out)

    # -------------------------------------------------------------- check
    st, out = run([script, '--check', '--build-dir', build], env=base_env)
    check('check: --check ends with status 0 and says what it checked',
          st == 0 and 'Checking what the installed Strata needs' in out, out)
    check('check: a missing optional program names its Debian package',
          'optional' not in out or 'package' in out, out)
    # A PATH that has the utilities that the script itself uses and no
    # program that Strata needs: every optional one must be named.
    fake = os.path.join(work, 'nopath')
    os.makedirs(fake)
    for name in ('bash', 'dirname', 'basename', 'mktemp', 'install', 'tr',
                 'cat', 'rm', 'rmdir', 'mkdir', 'readlink', 'ls', 'ldd',
                 'sed', 'grep', 'env', 'sort'):
        src = shutil.which(name)
        if src is not None:
            os.symlink(src, os.path.join(fake, name))
    env2 = dict(base_env)
    env2['PATH'] = fake
    st, out = run([os.path.join(fake, 'bash'), script, '--check',
                   '--build-dir', build], env=env2)
    check('check: with no program in the PATH the optional ones are named '
          'with their packages and the status is still 0',
          st == 0 and 'optional  pdflatex is not installed (package '
          'texlive-latex-base)' in out, out)

    # ------------------------------------------------ the installed programs
    # The programs run where nothing of the build directory can be reached:
    # the build directory is hidden, and the prefix is mounted at another
    # place than where it was installed.
    mount = '/mnt/strata'
    view = [shutil.which('bwrap') or 'bwrap', '--bind', '/', '/',
            '--dev', '/dev', '--proc', '/proc',
            '--tmpfs', '/mnt', '--bind', prefix, mount,
            '--tmpfs', build, '--']
    try:
        ok_view = subprocess.run(
            view + ['sh', '-c', 'test -x %s/bin/stred && test -z "$(ls %s)"'
                    % (mount, build)], stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL).returncode == 0
    except OSError:
        ok_view = False
    if ok_view:
        where = mount
        print('note the programs run at %s with the build directory '
              'hidden' % mount)
    else:
        view = []
        where = prefix
        print('note bwrap is not usable here: the programs run in the '
              'prefix, the build directory is not hidden')

    # No variable may tell where a file is.
    clean = dict(base_env)
    for v in ('STRATA_HELP', 'STRED', 'STRATA_STRPRINT', 'STRATA_DVIVIEW',
              'STRATA_TEMPLATES', 'STRATA_HOME', 'STRATA_BUILD_DIR',
              'STRATA_DIR', 'STRATA_ROOT'):
        clean.pop(v, None)
    clean['PATH'] = '/usr/bin:/bin'
    clean['HOME'] = home

    st, out = run(view + [where + '/bin/stred', '--version'], env=clean)
    check('run: the installed stred starts without the build directory',
          st == 0 and out.strip().startswith('stred (Strata)'), out)
    st, out = run(view + [where + '/bin/strata-run', 'str', '--version'],
                  env=clean)
    check('run: strata-run starts a program of the prefix by its name',
          st == 0 and out.strip().startswith('str (Strata)'), out)

    # The help file is found next to the program.
    text = 'Strata is'
    d = tui.session(view + [where + '/bin/stred', doc],
                    ['F1', 'wait', 'show'], env=clean)
    check('run: stred finds the help file of the installation (F1 shows '
          'the help, not a message that there is none)',
          len(d) == 1 and 'no help' not in d[0].lower() and
          'not found' not in d[0].lower() and
          'F1' not in d[0].split('\n')[0], d[0] if d else '')

    # The templates are found next to the program, with no variable: the list
    # of New shows the whole set that is installed, and the description of the
    # chosen one (the last one, letter-us, a business letter).
    names = ['letter-us', 'letter-a4'] + sorted(mktemplates.ENVELOPES)
    labels = [n for n, num, s in labeldata.every()]
    d = tui.session(view + [where + '/bin/stred', doc],
                    ['F12', 'ENTER', 'RIGHT', 'RIGHT', 'ENTER', 'wait',
                     'wait', 'END', 'ENTER', 'wait', 'show'], env=clean,
                    rows=34)
    check('run: the installed stred lists the installed templates with '
          'their descriptions in New (the end of the list is the folder '
          'letters: it holds both letters)', len(d) == 1 and
          'Templates: letters' in d[0] and
          'letter-us' in d[0] and 'letter-a4' in d[0] and
          'business letter' in d[0], d[0] if d else '')
    d = tui.session(view + [where + '/bin/stred', doc],
                    ['F12', 'ENTER', 'RIGHT', 'RIGHT', 'ENTER', 'wait',
                     'wait', 'text:5160', 'wait', 'show'], env=clean,
                    rows=34)
    check('run: the installed stred finds a label sheet by its number (all '
          '%d templates of the list are installed)' % (len(names) +
                                                       len(labels)),
          len(d) == 1 and 'Find: 5160' in d[0] and
          'Avery 5160 address labels' in d[0], d[0] if d else '')

    # The Document Manager finds the installed templates too, with no variable.
    check('run: no variable names the templates for the programs',
          'STRATA_TEMPLATES' not in clean and 'STRATA_HOME' not in clean,
          sorted(clean))
    d = tui.session(view + [where + '/bin/str'],
                    ['ENTER', 'wait', 'text:newmemo', 'INS', 'wait', 'wait',
                     'show'], env=clean, rows=34)
    check('run: the installed str lists the installed templates when a new '
          'document is asked for (the three subdirectories of the list)',
          len(d) == 1 and 'Templates' in d[0] and
          all(n in d[0] for n in ('envelopes/', 'labels/', 'letters/')) and
          'Structured' in d[0],
          d[0] if d else '')

    # str starts the stred that is next to it: choose Edit, give the name of
    # a document, and the Editor shows it.
    d = tui.session(view + [where + '/bin/str'],
                    ['ENTER', 'wait', 'text:' + doc, 'INS', 'wait', 'wait',
                     'show'], env=clean)
    check('run: the installed str starts the stred beside it on a document '
          '(the Editor shows its status line and the key strip)',
          len(d) == 1 and 'memo.doc' in d[0] and 'F12: Menu' in d[0] and
          'Strata Document Manager' not in d[0], d[0] if d else '')

    # The Printer makes a PDF; it needs LaTeX, which is not part of Strata.
    if shutil.which('pdflatex') is not None and os.path.exists(doc):
        pdf = os.path.join(work, 'out.pdf')
        st, out = run(view + [where + '/bin/strprint', doc, '-o', pdf],
                      env=clean, cwd=work)
        check('run: the installed strprint makes a PDF',
              st == 0 and os.path.exists(pdf) and
              open(pdf, 'rb').read(5) == b'%PDF-', out)
        # The marks of the company are in the Printer itself: a document
        # that uses them prints with the installed program alone.
        crlf = b'\r\n'
        lmsdoc = os.path.join(work, 'lms.doc')
        with open(lmsdoc, 'wb') as f:
            f.write(b'*R3' + crlf + b'MR1001' + crlf + b'PP\\1\\\x00' + crlf +
                    b'P1C33' + crlf + b'FHA1' + crlf + b'FTT5' + crlf +
                    b'DP0,8500,11000' + crlf + b'*B0' + crlf + b'S' + crlf +
                    b'TThe maker is \x1341(S' + crlf + b'T\\lms\x1d\x00' +
                    crlf + b'\x13) and its mark is \x1341(S' + crlf +
                    b'T\\lmslogo[2]\x1d\x00' + crlf + b'\x13).\x1d\x00' +
                    crlf + b'*E' + crlf)
        lpdf = os.path.join(work, 'lms.pdf')
        st, out = run(view + [where + '/bin/strprint', lmsdoc, '-o', lpdf,
                              '--no-banner'], env=clean, cwd=work)
        txt = ''
        if st == 0 and os.path.exists(lpdf) and shutil.which('pdftotext'):
            txt = subprocess.run(['pdftotext', lpdf, '-'],
                                 capture_output=True, text=True).stdout
        check('run: the installed strprint prints \\lms and \\lmslogo (the '
              'name and the letters LMS are in the PDF)',
              'Logic Magicians Software' in ' '.join(txt.split()) and
              'LMS' in txt, out + txt[:200])
    else:
        print('note no pdflatex or no test document: the Printer is not run')

    if os.path.exists(os.path.join(prefix, 'bin', 'stred-x11')):
        st, out = run(view + [where + '/bin/stred-x11', '--version'],
                      env=clean)
        check('run: the installed X11 Editor starts (--version)',
              st == 0 and 'stred (Strata)' in out, out)
        st, out = run(view + [where + '/bin/str-x11', '--version'],
                      env=clean)
        check('run: the installed X11 Document Manager starts (--version)',
              st == 0 and 'str (Strata)' in out, out)

    # The X11 Editor opens its window on a virtual display and keeps running
    # (it found its help file and its files, and did not stop at start).
    if os.path.exists(os.path.join(prefix, 'bin', 'stred-x11')) and \
            shutil.which('Xvfb') is not None:
        xr, xw = os.pipe()
        xv = subprocess.Popen(['Xvfb', '-displayfd', str(xw), '-screen', '0',
                               '800x600x24', '-nolisten', 'tcp'],
                              pass_fds=(xw,), stderr=subprocess.DEVNULL)
        os.close(xw)
        num = os.read(xr, 16).decode().strip()
        os.close(xr)
        try:
            xenv = dict(clean)
            xenv['DISPLAY'] = ':' + num
            def window_tree():
                """The windows of the display: a new one is the Editor."""
                try:
                    return subprocess.run(
                        ['xwininfo', '-display', ':' + num, '-root', '-tree'],
                        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                        timeout=10).stdout
                except (OSError, subprocess.TimeoutExpired):
                    return b''

            before = window_tree()
            xp = subprocess.Popen(view + [where + '/bin/stred-x11', doc],
                                  env=xenv, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL)
            # A loaded machine may need many seconds: poll for the window, up
            # to 20 seconds, and give up at once when the program has ended.
            shown = False
            for _ in range(80):
                if xp.poll() is not None:
                    break
                if window_tree() != before:
                    shown = True
                    break
                time.sleep(0.25)
            if shown:
                time.sleep(1)
            check('run: the installed X11 Editor opens its window and keeps '
                  'running on a virtual display',
                  shown and xp.poll() is None, window_tree()[-300:])
            xp.terminate()
            xp.wait()
        finally:
            xv.terminate()
            xv.wait()

    # ---------------------------------------------------------- uninstall
    st, out = run([script, '--prefix', prefix, '--uninstall', '--quiet'],
                  env=base_env)
    check('uninstall: the script ends with status 0', st == 0, out)
    check('uninstall: nothing of the install is left (the directories that '
          'the install made are gone too)', not os.path.exists(
              os.path.join(work, 'usr')),
          sorted(files_under(os.path.join(work, 'usr')))
          if os.path.exists(os.path.join(work, 'usr')) else '')
    st, out = run([script, '--prefix', prefix, '--uninstall', '--quiet'],
                  env=base_env)
    check('uninstall: a second uninstall says that there is nothing to remove',
          st != 0 and 'nothing to remove' in out, out)

    # A file that was not installed by the script is never removed.
    st, out = run([script, '--prefix', prefix, '--build-dir', build,
                   '--no-prereqs', '--quiet'], env=base_env)
    mine = os.path.join(prefix, 'share', 'strata', 'mine.txt')
    open(mine, 'w').write('mine\n')
    st, out = run([script, '--prefix', prefix, '--uninstall', '--quiet'],
                  env=base_env)
    check('uninstall: a file of the user in the prefix stays, and so do the '
          'directories above it', os.path.exists(mine) and
          not os.path.exists(os.path.join(prefix, 'bin', 'stred')))
    os.unlink(mine)
    # What the uninstall had to keep is removed by hand for the next test.
    shutil.rmtree(os.path.join(work, 'usr'), ignore_errors=True)

    # An install over an install keeps what the first one made, so that the
    # uninstall still removes every directory.
    for _ in (1, 2):
        st, out = run([script, '--prefix', prefix, '--build-dir', build,
                       '--no-prereqs', '--quiet'], env=base_env)
    check('twice: a second install over the first ends with status 0',
          st == 0, out)
    st, out = run([script, '--prefix', prefix, '--uninstall', '--quiet'],
                  env=base_env)
    check('twice: one uninstall removes everything of both installs',
          st == 0 and not os.path.exists(os.path.join(work, 'usr')),
          sorted(files_under(os.path.join(work, 'usr')))
          if os.path.exists(os.path.join(work, 'usr')) else out)

    # ------------------------------------------------------------ destdir
    dest = os.path.join(work, 'pkg')
    st, out = run([script, '--prefix', '/usr/local', '--destdir', dest,
                   '--build-dir', build, '--quiet'], env=base_env)
    check('destdir: the files go under DESTDIR/PREFIX',
          st == 0 and os.path.isfile(os.path.join(dest, 'usr', 'local', 'bin',
                                                  'stred')), out)
    st, out = run([script, '--prefix', '/usr/local', '--destdir', dest,
                   '--uninstall', '--quiet'], env=base_env)
    check('destdir: the uninstall under DESTDIR removes them again',
          st == 0 and not os.path.exists(dest + '/usr/local/bin/stred'), out)

    prereq_question_test()
    prereq_default_test()
    user_prereq_test()
    installed_list_test()

finally:
    shutil.rmtree(work, ignore_errors=True)

print('installtest: %s' % ('%d FAILURES' % fails if fails else 'all passed'))
sys.exit(1 if fails else 0)
