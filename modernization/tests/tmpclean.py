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

"""A test program or a tool must make its temporary files in the directory
that the variable TMPDIR names, and only in /tmp when the variable is not
set.  This test runs the quick test programs, each one with TMPDIR set to an
empty folder, and each one with a private empty /tmp of its own (a mount
namespace with a fresh read-only tmpfs on /tmp, made with bwrap, which keeps
the user), and fails when a program cannot do its work, or leaves anything, in
that /tmp.  A private /tmp, not a
list of the real /tmp before and after, is used because other programs make
files in the real /tmp at the same time.  The private /tmp is the only place
that a program could write to by the old fixed paths, and it is gone when the
program ends.

    python3 -I -B tests/tmpclean.py BINDIR DOCSRC DATA

BINDIR holds the test programs (the build directory of the programs), DOCSRC
the sample documents and DATA the test data (tests/data).  When the system
does not let a program make a private /tmp (bwrap), the test says so and
passes: it cannot know.  The top folder of BINDIR in /tmp, if it is there, is
put back into the private /tmp, because the programs are in it.  The folder
for TMPDIR is made below BINDIR and removed at the end."""
import sys as _s; _s.dont_write_bytecode = True
import os
import shutil
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
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


def private_tmp(binds):
    """The start of a command that runs a program with an empty /tmp of its
    own; binds are the folders of /tmp that must stay (the programs)."""
    cmd = ['bwrap', '--bind', '/', '/', '--dev', '/dev', '--proc', '/proc',
           '--tmpfs', '/tmp']
    for b in binds:
        cmd += ['--bind', b, b]
    # Read only: a program that tries to make a file there, and takes it
    # away again, fails too.
    return cmd + ['--remount-ro', '/tmp', '--']


def can_private(binds):
    """Whether a program may be run with a private /tmp."""
    try:
        r = subprocess.run(private_tmp(binds) + ['sh', '-c',
                          'if touch /tmp/x 2>/dev/null; then exit 3; fi'],
                           stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
    except OSError:
        return False
    return r.returncode == 0


def run_private(cmd, tmpdir, listfile, binds, env_extra=None):
    """Run cmd with TMPDIR set and a private /tmp; the entries that it left
    in that /tmp (not the binds) are written to listfile.  Returns (status,
    output, left)."""
    script = '"$@"; rc=$?; ls -A /tmp > "$LISTFILE"; exit $rc'
    env = dict(os.environ, TMPDIR=tmpdir, LISTFILE=listfile)
    if env_extra:
        env.update(env_extra)
    r = subprocess.run(private_tmp(binds) + ['sh', '-c', script, 'sh'] + cmd,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       env=env, cwd=root, universal_newlines=True)
    keep = set(os.path.basename(b) for b in binds)
    left = []
    if os.path.exists(listfile):
        with open(listfile) as f:
            left = [ln.strip() for ln in f
                    if ln.strip() != '' and ln.strip() not in keep]
        os.unlink(listfile)
    return r.returncode, r.stdout, left


def main():
    if len(sys.argv) != 4:
        print('usage: tmpclean.py BINDIR DOCSRC DATA')
        return 2
    bindir = os.path.abspath(sys.argv[1])
    docsrc = os.path.abspath(sys.argv[2])
    data = os.path.abspath(sys.argv[3])
    py = [sys.executable, '-I', '-B']

    binds = []
    top = os.path.realpath(bindir).split(os.sep)
    if len(top) > 2 and top[1] == 'tmp':
        binds.append(os.sep + os.path.join(top[1], top[2]))
    if not can_private(binds):
        print('skip tmpclean: this system does not let a program make a '
              'private /tmp (bwrap)')
        return 0

    base = os.path.join(bindir, 'tmpclean-work')
    shutil.rmtree(base, ignore_errors=True)
    os.makedirs(base)
    listfile = os.path.join(base, 'left.txt')
    tmpdir = os.path.join(base, 'tmpdir')
    os.makedirs(tmpdir)

    progs = [
        ('lxtest', [os.path.join(bindir, 'lxtest'), data], None),
        ('pvtest', [os.path.join(bindir, 'pvtest'), data], None),
        ('pjtest', [os.path.join(bindir, 'pjtest'),
                    os.path.join(here, 'fakelpstat.sh')], None),
        ('perrtest', [os.path.join(bindir, 'perrtest'), data, base], None),
        ('backuptest', [os.path.join(bindir, 'backuptest')], None),
        ('logtest', [os.path.join(bindir, 'logtest')], None),
        ('edtest', [os.path.join(bindir, 'edtest'), docsrc,
                    os.path.join(base, 'edtest_out.doc'), data], None),
        ('checkpics.py', py + [os.path.join(here, 'checkpics.py')], None),
        ('shots.py scratch',
         py + ['-c', 'import sys; sys.path.insert(0, %r); import shots; '
               'p = shots.scratch("x"); sys.exit(0 if p == %r else 1)'
               % (os.path.join(root, 'manual'),
                  os.path.join(tmpdir, 'x'))], None),
    ]
    for name, cmd, env_extra in progs:
        if not os.path.exists(cmd[0]) and not name.endswith('.py') and \
                name != 'shots.py scratch':
            check('%s: the program is built' % name, False, cmd[0])
            continue
        rc, out, left = run_private(cmd, tmpdir, listfile, binds, env_extra)
        check('%s: it ends well' % name, rc == 0, out[-600:])
        check('%s: it leaves nothing in /tmp when TMPDIR is set' % name,
              left == [], 'left in /tmp: ' + ' '.join(left))

    shutil.rmtree(base, ignore_errors=True)
    print('tmpclean: %d failures' % fails)
    return 1 if fails else 0


sys.exit(main())
