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

"""The tool scripts/strata-release makes the tag of a release and its tarball.

    python3 -I -B tests/release.py

The logic of the tool is tested in a scratch git repository that holds copies
of scripts/strata-release, scripts/strata-version and scripts/setup and small
stand-ins for strata-build and strata-install-tarball, so that no build of
Strata is needed.  The tests check that:

  - a version that is not X.Y.0, a changed tracked file, an existing tag of
    either kind (also with the option --dry-run), a missing commit and a
    build directory inside the repository are refused and nothing is changed;
  - the existing tag is the same object afterwards and no worktree and no
    build directory was made;
  - the option --dry-run changes nothing;
  - a release makes the annotated tag with its message on the right commit,
    the files strata-VERSION.tar.xz and strata-VERSION.tar.xz.sha256, removes
    its scratch worktree, pushes nothing, and puts the files in the directory
    release of the build root of the environment (there is no option for it);
  - a failure after the tag was made removes the tag, the worktree and the
    build directory, and removes no tag that it did not make, also when the
    tag appears between the check and the creation;
  - the manuals are made between the build and the tarball (the stand-in of
    the build logs the order: build, manual, install), the PDF files are
    named in the output, a failure of the manual build is rolled back like
    any other, and a machine without the tools of the manual (LaTeX) is
    refused at the start with nothing changed;
  - a tagged checkout whose version is not the version is an error;
  - the option --rebuild makes the tarball and its checksum file again from
    an existing annotated tag: a missing tag, a tag that is not annotated and
    the option --commit with it are refused and nothing is changed; --dry-run
    with it changes nothing; the real run makes the files in the directory
    release of the build root, leaves the tag the same object, makes no other
    tag, removes its scratch worktree and says that the bytes differ from a
    release made elsewhere; a failed step removes the tarball and the
    worktree and never the tag;
  - the tool run with --dry-run in the real repository changes nothing.

The checks are made again on copies of the tool that have been spoiled, one
rule at a time, and every copy has to fail a check.
"""

import os
import shutil
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
tree = os.path.dirname(here)
project = tree
scripts = os.path.join(project, 'scripts')

fails = 0
quiet = False
quiet_fails = 0


def check(name, ok, detail=''):
    global fails, quiet_fails
    if quiet:
        # The checks of a spoiled copy are counted, not shown: a spoiled copy
        # has to fail one of them.
        if not ok:
            quiet_fails += 1
        return
    if ok:
        print('ok   %s' % name)
    else:
        fails += 1
        print('FAIL %s' % name)
        if detail != '':
            print('     %s' % str(detail)[-400:])


def read(path):
    with open(path) as f:
        return f.read()


def write(path, text, mode=0o644):
    with open(path, 'w') as f:
        f.write(text)
    os.chmod(path, mode)


tmp = tempfile.mkdtemp(prefix='rel.', dir=os.environ.get('TMPDIR') or '/tmp')
env = {'PATH': os.environ['PATH'], 'LANG': 'C', 'HOME': tmp,
       'TMPDIR': os.path.join(tmp, 'tmp'), 'GIT_CONFIG_GLOBAL': '/dev/null',
       'GIT_CONFIG_SYSTEM': '/dev/null', 'GIT_AUTHOR_NAME': 't',
       'GIT_AUTHOR_EMAIL': 't@t', 'GIT_COMMITTER_NAME': 't',
       'GIT_COMMITTER_EMAIL': 't@t'}
os.makedirs(env['TMPDIR'])


def run(cmd, cwd, e=None):
    """Run a command; return (status, output of both streams)."""
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       cwd=cwd, env=e if e is not None else env,
                       timeout=300)
    return r.returncode, r.stdout.decode('utf-8', 'replace')


def git(repo, *args):
    return run(['git', '-C', repo] + list(args), repo)[1].strip()


# The stand-in for strata-build: the plain build writes the file version, as
# the real one does, and install makes the stand-in tarball.  FAILING makes
# the install fail.
STUB_BUILD = '''#!/bin/bash
set -e
while [ "${1:-}" = "-j" ]; do shift 2; done
log() { echo "${1}" >> "${STRATA_BUILD_DIR}/order.log"; }
if [ "${#}" -eq 0 ]; then
    mkdir -p "${STRATA_BUILD_DIR}"
    "${STRATA_ROOT}/scripts/strata-version" > "${STRATA_BUILD_DIR}/version"
    VERSION_LINE
    log build
    exit 0
fi
if [ "${1}" = "manual" ]; then
    MANUAL_FAILING
    log manual
    mkdir -p "${STRATA_BUILD_DIR}/manual/pdf"
    for n in GettingStarted cheat strata-reference-manual; do
        echo "%PDF-1.4" > "${STRATA_BUILD_DIR}/manual/pdf/${n}.pdf"
    done
    exit 0
fi
if [ "${1}" = "install" ]; then
    log install
    FAILING
    exec "${STRATA_ROOT}/scripts/strata-install-tarball" \\
        "${STRATA_BUILD_DIR}" release
fi
exit 2
'''

STUB_TARBALL = '''#!/bin/bash
set -e
build="${1}"
version="$(head -n 1 "${build}/version")"
name="strata-${version}"
mkdir -p "${build}/stage/${name}/payload/share/strata/pdf"
echo "payload" > "${build}/stage/${name}/payload/file"
cp "${build}"/manual/pdf/*.pdf \\
    "${build}/stage/${name}/payload/share/strata/pdf/" 2>/dev/null || true
tar -C "${build}/stage" -cJf "${build}/${name}.tar.xz" "${name}"
(cd "${build}" && sha256sum "${name}.tar.xz" > "${name}.tar.xz.sha256")
'''


def stub_build(failing=False, version_line='', manual_failing=False):
    return (STUB_BUILD.replace('MANUAL_FAILING',
                               'exit 4' if manual_failing else ':')
            .replace('FAILING', 'exit 3' if failing else ':')
            .replace('VERSION_LINE', version_line or ':'))


STUB_CHECK_TOOLS = '#!/bin/bash\nexit 0\n'


class Repo:
    """A scratch repository with three commits: good, failing install and
    a wrong version in the file version."""

    def __init__(self, tool_text):
        self.dir = tempfile.mkdtemp(prefix='repo.', dir=tmp)
        self.bare = self.dir + '.git'
        self.tmpdir = tempfile.mkdtemp(prefix='tmp.', dir=tmp)
        self.root = os.path.join(self.tmpdir, 'bld')
        self.env = dict(env, TMPDIR=self.tmpdir, STRATA_BUILD_ROOT=self.root)
        r = self.dir
        os.makedirs(os.path.join(r, 'scripts'))
        os.makedirs(os.path.join(r, 'src'))
        write(os.path.join(r, 'src', 'keep'), 'x\n')
        for n in ('strata-version', 'setup'):
            shutil.copy(os.path.join(scripts, n), os.path.join(r, 'scripts'))
        os.chmod(os.path.join(r, 'scripts', 'strata-version'), 0o755)
        write(os.path.join(r, 'scripts', 'strata-release'), tool_text, 0o755)
        write(os.path.join(r, 'scripts', 'strata-install-tarball'),
              STUB_TARBALL, 0o755)
        write(os.path.join(r, 'scripts', 'check-tools'), STUB_CHECK_TOOLS,
              0o755)
        git(r, 'init', '-q', '-b', 'dev')
        self.commit('good', stub_build())
        self.good = git(r, 'rev-parse', 'HEAD')
        self.commit('good again', stub_build() + '# a second good commit\n')
        self.good2 = git(r, 'rev-parse', 'HEAD')
        self.commit('spare', stub_build() + '# a spare commit\n')
        self.spare = git(r, 'rev-parse', 'HEAD')
        self.commit('spare two', stub_build() + '# a second spare commit\n')
        self.spare2 = git(r, 'rev-parse', 'HEAD')
        self.commit('race', stub_build() + '# a commit for the race\n')
        self.race = git(r, 'rev-parse', 'HEAD')
        self.commit('failing install', stub_build(failing=True))
        self.failing = git(r, 'rev-parse', 'HEAD')
        self.commit('failing manual', stub_build(manual_failing=True))
        self.mfailing = git(r, 'rev-parse', 'HEAD')
        self.commit('wrong version', stub_build(version_line=
                    'echo 9.9.9 > "${STRATA_BUILD_DIR}/version"'))
        self.wrong = git(r, 'rev-parse', 'HEAD')
        git(r, 'checkout', '-q', '-B', 'dev', self.good)
        subprocess.run(['git', 'init', '-q', '--bare', self.bare],
                       env=env, check=True)
        git(r, 'remote', 'add', 'origin', self.bare)
        self.tool = os.path.join(r, 'scripts', 'strata-release')

    def commit(self, subject, build_text):
        write(os.path.join(self.dir, 'scripts', 'strata-build'), build_text,
              0o755)
        git(self.dir, 'add', '-A')
        git(self.dir, 'commit', '-q', '-m', subject)

    def release(self, *args, e=None):
        return run([self.tool] + list(args), self.dir,
                   e if e is not None else self.env)

    def tags(self):
        return git(self.dir, 'tag', '-l')

    def worktrees(self):
        return git(self.dir, 'worktree', 'list').count('\n') + 1

    def build_dir(self, version):
        """The tarball of the version in the build root of the environment."""
        return os.path.join(self.root, 'release',
                            'strata-' + version + '.tar.xz')

    def wt_dir(self, version):
        return os.path.join(self.dir, '.claude', 'worktrees',
                            'release-' + version)


def scenarios(tool_text, label):
    """The checks of the tool."""
    repo = Repo(tool_text)
    r = repo

    # ---- the version number --------------------------------------------
    for v in ('1.0', '1.0.1', 'v1.0.0', 'abc', '1.0.0-rc1', '1.x.0'):
        st, out = r.release(v)
        check('%s: the version %s is refused with a message' % (label, v),
              st == 1 and 'X.Y.0' in out and r.tags() == '', (st, out))
    st, out = r.release()
    check('%s: no version is refused with an example' % label,
          st == 1 and '1.0.0' in out, (st, out))

    # ---- a changed tracked file ----------------------------------------
    path = os.path.join(r.dir, 'src', 'keep')
    write(path, 'changed\n')
    st, out = r.release('4.0.0')
    check('%s: a modified tracked file is refused and nothing is made' %
          label, st == 1 and 'modified' in out and r.tags() == '' and
          not os.path.exists(r.wt_dir('4.0.0')), (st, out))
    git(r.dir, 'add', path)
    st, out = r.release('4.0.0')
    check('%s: a staged file is refused too' % label,
          st == 1 and r.tags() == '', (st, out))
    git(r.dir, 'reset', '-q', '--hard')
    write(os.path.join(r.dir, 'untracked.txt'), 'u\n')
    st, out = r.release('4.0.0', '--dry-run')
    check('%s: an untracked file does not count' % label,
          st == 0, (st, out))
    os.remove(os.path.join(r.dir, 'untracked.txt'))

    # ---- an existing tag -----------------------------------------------
    git(r.dir, 'tag', '-a', 'v5.0.0', '-m', 'old', r.spare)
    git(r.dir, 'tag', 'v5.1.0', r.spare2)
    for v in ('5.0.0', '5.1.0'):
        was = git(r.dir, 'rev-parse', 'refs/tags/v' + v)
        for extra in ([], ['--dry-run']):
            st, out = r.release(v, *extra)
            check('%s: the existing tag v%s is refused%s with the message '
                  'and the hint' % (label, v, ' (dry run)' if extra else ''),
                  st == 1 and ('the tag v%s exists already: nothing was '
                               'changed' % v) in out and
                  ('git tag -d v%s' % v) in out, (st, out))
        check('%s: the tag v%s is the same object afterwards' % (label, v),
              git(r.dir, 'rev-parse', 'refs/tags/v' + v) == was)
        check('%s: no worktree and no build directory for v%s' % (label, v),
              not os.path.exists(r.wt_dir(v)) and
              not os.path.exists(r.build_dir(v)) and r.worktrees() == 1)
    check('%s: only the two tags that were made exist' % label,
          r.tags().split() == ['v5.0.0', 'v5.1.0'], r.tags())

    # ---- a missing commit, a build directory in the repository ----------
    st, out = r.release('6.0.0', '--commit', 'nosuchcommit')
    check('%s: a missing commit is refused' % label,
          st == 1 and 'no commit' in out and 'v6.0.0' not in r.tags(),
          (st, out))
    st, out = r.release('6.0.0', e=dict(r.env, STRATA_BUILD_ROOT=
                                        os.path.join(r.dir, 'build')))
    check('%s: a build directory inside the repository is refused' % label,
          st == 1 and 'inside the source tree' in out and
          not os.path.exists(os.path.join(r.dir, 'build')), (st, out))

    # ---- a dry run -------------------------------------------------------
    st, out = r.release('7.0.0', '--dry-run')
    check('%s: --dry-run prints the commit and the tag and changes nothing'
          % label, st == 0 and r.good in out and 'v7.0.0' in out and
          'v7.0.0' not in r.tags() and r.worktrees() == 1 and
          not os.path.exists(os.path.join(r.dir, '.claude')) and
          not os.path.exists(r.build_dir('7.0.0')), (st, out))

    # ---- a release -------------------------------------------------------
    st, out = r.release('2.0.0')
    built = os.path.join(r.root, 'release')
    check('%s: a release succeeds' % label, st == 0, out)
    check('%s: the tag is annotated, has its message and is on the commit' %
          label, git(r.dir, 'cat-file', '-t', 'v2.0.0') == 'tag' and
          git(r.dir, 'tag', '-l', '--format=%(contents:subject)', 'v2.0.0')
          == 'Strata 2.0.0 (release 2)' and
          git(r.dir, 'rev-parse', 'v2.0.0^{commit}') == r.good)
    names = sorted(n for n in os.listdir(built) if n.startswith('strata-')
                   and (n.endswith('.xz') or n.endswith('.sha256')))
    check('%s: the files are exactly strata-2.0.0.tar.xz and its .sha256' %
          label, names == ['strata-2.0.0.tar.xz',
                           'strata-2.0.0.tar.xz.sha256'], names)
    st2, out2 = run(['sha256sum', '-c', 'strata-2.0.0.tar.xz.sha256'], built)
    check('%s: sha256sum -c passes' % label, st2 == 0, out2)
    check('%s: the output names the tarball, the checksum and the commit' %
          label, os.path.join(built, 'strata-2.0.0.tar.xz') in out and
          'SHA-256:' in out and r.good in out and 'pushes nothing' in out,
          out)
    order = read(os.path.join(built, 'order.log')).split()
    check('%s: the manuals are made between the build and the tarball: the '
          'order is build, manual, install' % label,
          order == ['build', 'manual', 'install'], order)
    check('%s: the output names the PDF files that are in the tarball' %
          label, 'manual PDF files in the tarball' in out and
          all(n + '.pdf' in out for n in ('GettingStarted', 'cheat',
                                          'strata-reference-manual')), out)
    check('%s: the scratch worktree is removed afterwards' % label,
          not os.path.exists(r.wt_dir('2.0.0')) and r.worktrees() == 1)
    check('%s: nothing is pushed: the remote has no tag' % label,
          git(r.dir, 'ls-remote', '--tags', r.bare) == '')
    check('%s: the checked out branch and the tree are as before' % label,
          git(r.dir, 'symbolic-ref', '--short', 'HEAD') == 'dev' and
          git(r.dir, 'status', '--porcelain', '--untracked-files=no') == '')

    st, out = r.release('2.0.0')
    check('%s: a second run for the same version is refused' % label,
          st == 1 and 'exists already' in out, (st, out))
    st, out = r.release('2.2.0')
    check('%s: a commit that has a release tag is not tagged again' % label,
          st == 1 and 'v2.0.0 already' in out and 'v2.2.0' not in r.tags(),
          (st, out))

    st, out = r.release('2.1.0', '--commit', r.good2)
    check('%s: a second release is made in the same build directory of the '
          'environment, and both are there' % label, st == 0 and
          os.path.isfile(os.path.join(built, 'strata-2.1.0.tar.xz')) and
          os.path.isfile(os.path.join(built, 'strata-2.1.0.tar.xz.sha256'))
          and os.path.isfile(os.path.join(built, 'strata-2.0.0.tar.xz')),
          (st, out, os.listdir(built)))
    for opt in ('--output', '--build-dir'):
        st, out = r.release('2.3.0', opt, os.path.join(r.tmpdir, 'elsewhere'))
        check('%s: there is no option %s: it is refused and nothing is made'
              % (label, opt), st == 1 and 'unknown option' in out and
              'v2.3.0' not in r.tags() and
              not os.path.exists(os.path.join(r.tmpdir, 'elsewhere')),
              (st, out))
    noroot = {k: v for k, v in r.env.items() if k != 'STRATA_BUILD_ROOT'}
    st, out = r.release('2.3.0', e=noroot)
    check('%s: without STRATA_BUILD_ROOT the tool refuses and changes nothing'
          % label, st == 1 and 'STRATA_BUILD_ROOT is not set' in out and
          'v2.3.0' not in r.tags() and r.worktrees() == 1, (st, out))
    old = os.path.join(built, 'strata-2.3.0.tar.xz')
    write(old, 'older\n')
    st, out = r.release('2.3.0', '--commit', r.race)
    check('%s: a tarball of that version that exists is not made over: the '
          'tool refuses and changes nothing' % label, st == 1 and
          'exists already' in out and 'v2.3.0' not in r.tags() and
          read(old) == 'older\n', (st, out))
    os.remove(old)

    # ---- a failure after the tag: the rollback ----------------------------
    st, out = r.release('3.0.0', '--commit', r.failing)
    check('%s: a failed build removes the tag, the worktree and the build '
          'directory and says so' % label, st == 1 and
          'v3.0.0' not in r.tags() and not os.path.exists(r.wt_dir('3.0.0'))
          and not os.path.exists(r.build_dir('3.0.0')) and r.worktrees() == 1
          and 'removed the tag v3.0.0' in out, (st, out, r.tags()))
    check('%s: the rollback left the tarballs that were in the build '
          'directory before' % label,
          os.path.isfile(os.path.join(built, 'strata-2.0.0.tar.xz')) and
          os.path.isfile(os.path.join(built, 'strata-2.1.0.tar.xz')))
    check('%s: the rollback left the tags that existed before' % label,
          'v5.0.0' in r.tags() and 'v2.0.0' in r.tags())

    # ---- the manual build fails: the rollback -------------------------------
    st, out = r.release('3.1.0', '--commit', r.mfailing)
    check('%s: a failed manual build removes the tag, the worktree and the '
          'build directory and says so' % label, st == 1 and
          'v3.1.0' not in r.tags() and not os.path.exists(r.wt_dir('3.1.0'))
          and not os.path.exists(r.build_dir('3.1.0')) and r.worktrees() == 1
          and 'removed the tag v3.1.0' in out, (st, out, r.tags()))

    # ---- the tools of the manual are missing: refused at the start ----------
    tools = os.path.join(r.dir, 'scripts', 'check-tools')
    write(tools, '#!/bin/bash\necho "missing: pdflatex"\nexit 1\n', 0o755)
    tags_before = r.tags()
    for extra in ([], ['--dry-run']):
        st, out = r.release('4.1.0', *extra)
        check('%s: missing tools of the manual%s are refused at the start, '
              'the missing tool is shown, and nothing was changed' %
              (label, ' (dry run)' if extra else ''), st == 1 and
              'pdflatex' in out and 'nothing was changed' in out and
              r.tags() == tags_before and r.worktrees() == 1 and
              not os.path.exists(r.wt_dir('4.1.0')) and
              not os.path.exists(r.build_dir('4.1.0')), (st, out))
    git(r.dir, 'checkout', '--', 'scripts/check-tools')

    # ---- a wrong version in the tagged checkout -----------------------------
    st, out = r.release('8.0.0', '--commit', r.wrong)
    check('%s: a version of the checkout that is not the version is an '
          'error and the tag is removed' % label, st == 1 and
          "'9.9.9', not '8.0.0'" in out and 'v8.0.0' not in r.tags() and
          not os.path.exists(r.wt_dir('8.0.0')), (st, out))

    # ---- --rebuild: the tarball again from an existing tag ---------------
    def other_root(name):
        return dict(r.env, STRATA_BUILD_ROOT=os.path.join(r.tmpdir, name))

    tags_before = r.tags()
    st, out = r.release('9.2.0', '--rebuild', e=other_root('rb0'))
    check('%s: --rebuild of a tag that does not exist is refused and nothing '
          'is made' % label, st == 1 and 'does not exist' in out and
          'nothing was changed' in out and r.tags() == tags_before and
          r.worktrees() == 1 and
          not os.path.exists(os.path.join(r.tmpdir, 'rb0')), (st, out))
    st, out = r.release('2.0.0', '--rebuild', '--commit', r.good2,
                        e=other_root('rb1'))
    check('%s: --rebuild with --commit is refused and nothing is made' %
          label, st == 1 and '--commit cannot be used with --rebuild' in out
          and r.tags() == tags_before and
          not os.path.exists(os.path.join(r.tmpdir, 'rb1')), (st, out))
    light = git(r.dir, 'rev-parse', 'refs/tags/v5.1.0')
    st, out = r.release('5.1.0', '--rebuild', e=other_root('rb2'))
    check('%s: --rebuild of a tag that is not annotated is refused and the '
          'tag is not changed' % label, st == 1 and
          'not an annotated tag' in out and
          git(r.dir, 'rev-parse', 'refs/tags/v5.1.0') == light and
          not os.path.exists(os.path.join(r.tmpdir, 'rb2')), (st, out))
    write(os.path.join(r.dir, 'src', 'keep'), 'changed\n')
    was = git(r.dir, 'rev-parse', 'refs/tags/v2.0.0')
    st, out = r.release('2.0.0', '--rebuild', '--dry-run',
                        e=other_root('rb3'))
    check('%s: --rebuild --dry-run names the existing tag and changes '
          'nothing (a modified tracked file does not matter: the tag is '
          'built)' % label, st == 0 and r.good in out and
          'existing tag, not changed' in out and
          'dry run: nothing was changed' in out and r.tags() == tags_before
          and git(r.dir, 'rev-parse', 'refs/tags/v2.0.0') == was and
          r.worktrees() == 1 and
          not os.path.exists(os.path.join(r.tmpdir, 'rb3')), (st, out))
    git(r.dir, 'reset', '-q', '--hard')
    st, out = r.release('2.0.0', '--rebuild', e=other_root('rb4'))
    rb = os.path.join(r.tmpdir, 'rb4', 'release')
    check('%s: --rebuild of an existing tag succeeds' % label, st == 0, out)
    names = sorted(n for n in os.listdir(rb) if n.startswith('strata-')
                   and (n.endswith('.xz') or n.endswith('.sha256'))) \
        if os.path.isdir(rb) else []
    check('%s: --rebuild makes strata-2.0.0.tar.xz and its .sha256 in the '
          'directory release of the build root' % label,
          names == ['strata-2.0.0.tar.xz', 'strata-2.0.0.tar.xz.sha256'],
          names)
    st2, out2 = run(['sha256sum', '-c', 'strata-2.0.0.tar.xz.sha256'], rb) \
        if names else (1, 'no files')
    check('%s: --rebuild: sha256sum -c passes' % label, st2 == 0, out2)
    check('%s: --rebuild leaves the tag the same object, makes no other tag '
          'and removes its scratch worktree' % label,
          git(r.dir, 'rev-parse', 'refs/tags/v2.0.0') == was and
          r.tags() == tags_before and not os.path.exists(r.wt_dir('2.0.0'))
          and r.worktrees() == 1, (r.tags(), tags_before))
    check('%s: --rebuild prints the commit of the tag, the SHA-256 and says '
          'that the bytes differ from a release made elsewhere, and does '
          'not tell to push' % label, r.good in out and 'SHA-256:' in out
          and 'differ from a release made on another machine' in out and
          'git push' not in out, out)
    check('%s: --rebuild built the tag: the order is build, manual, install'
          % label, os.path.exists(os.path.join(rb, 'order.log')) and
          read(os.path.join(rb, 'order.log')).split() ==
          ['build', 'manual', 'install'])
    git(r.dir, 'tag', '-a', 'v10.0.0', '-m', 'failing', r.failing)
    tags_with = r.tags()
    was10 = git(r.dir, 'rev-parse', 'refs/tags/v10.0.0')
    st, out = r.release('10.0.0', '--rebuild', e=other_root('rb5'))
    check('%s: a failed --rebuild removes the worktree and the build '
          'directory it made and never the tag' % label, st == 1 and
          'removed the scratch worktree' in out and
          'removed the tag' not in out and r.tags() == tags_with and
          git(r.dir, 'rev-parse', 'refs/tags/v10.0.0') == was10 and
          not os.path.exists(r.wt_dir('10.0.0')) and r.worktrees() == 1 and
          not os.path.exists(os.path.join(r.tmpdir, 'rb5')), (st, out))

    # ---- a tag that appears between the check and the creation --------------
    wrapper_dir = os.path.join(r.tmpdir, 'wrap')
    os.makedirs(wrapper_dir)
    real_git = shutil.which('git')
    write(os.path.join(wrapper_dir, 'git'),
          '#!/bin/bash\n'
          'for a in "${@}"; do\n'
          '    if [ "${a}" = "-a" ]; then\n'
          '        %s -C "%s" tag v9.1.0 %s\n'
          '    fi\n'
          'done\n'
          'exec %s "${@}"\n' % (real_git, r.dir, r.good, real_git), 0o755)
    e2 = dict(r.env, PATH=wrapper_dir + ':' + env['PATH'])
    st, out = r.release('9.1.0', '--commit', r.race, e=e2)
    check('%s: a tag made in between is refused by git and is kept' % label,
          st == 1 and 'exists already' in out and 'v9.1.0' in r.tags() and
          git(r.dir, 'cat-file', '-t', 'v9.1.0') == 'commit' and
          not os.path.exists(r.wt_dir('9.1.0')) and
          not os.path.exists(r.build_dir('9.1.0')), (st, out))


def real_dry_run():
    """The tool in the real repository, with --dry-run, changes nothing."""
    tool = os.path.join(scripts, 'strata-release')
    if not os.path.exists(tool):
        check('real: the tool exists', False, tool)
        return
    dirty = run(['git', '-C', project, 'status', '--porcelain',
                 '--untracked-files=no'], project)[1].strip()
    if dirty != '':
        print('ok   real: skipped: the tree has changes (the tool refuses '
              'that, as it should)')
        return
    tags = git(project, 'tag', '-l')
    trees = git(project, 'worktree', 'list')
    # The tool needs the build root of the environment, which the test has
    # not got of its own: a directory that is not made is enough for a dry run.
    real_env = dict(env, STRATA_BUILD_ROOT=os.path.join(tmp, 'real-root'))
    st, out = run([tool, '987.0.0', '--dry-run'], project, real_env)
    head = git(project, 'rev-parse', 'HEAD')
    check('real: --dry-run prints the commit and the tag', st == 0 and
          head in out and 'v987.0.0' in out, (st, out))
    check('real: --dry-run changes no tag and no worktree',
          git(project, 'tag', '-l') == tags and
          git(project, 'worktree', 'list') == trees)


def spoiled(text):
    """Copies of the tool that each break one rule, with the rule."""
    out = []
    breaks = (
        ('the check of the changed tracked files is removed',
         '    if [ -n "$(git -C "${project_dir}" status --porcelain \\\n'
         '                   --untracked-files=no)" ]; then',
         '    if false; then'),
        ('the rollback is removed', 'trap cleanup EXIT;', ':'),
        ('the check of the version of the checkout is removed',
         'if [ "${built_version}" != "${version}" ]; then', 'if false; then'),
        ('a push is added',
         'echo "strata-release: building v${version} in ${worktree} ...";',
         'git -C "${project_dir}" push origin "v${version}" >/dev/null '
         '2>&1 || true;\n'
         'echo "strata-release: building v${version} in ${worktree} ...";'),
        ('the manual build is removed',
         '       ./scripts/strata-build -j "${jobs}" manual &&\n', ''),
        ('the check of the tools of the manual is removed',
         'if ! tools_report="$("${check_tools}" --build --run --quiet '
         '2>&1)"; then', 'if false; then'),
        ('the check of the existing tag is removed',
         '    if git -C "${project_dir}" rev-parse -q --verify \\\n'
         '           "refs/tags/v${version}" >/dev/null 2>&1; then',
         '    if false; then'),
        ('the refusal of --rebuild with --commit is removed',
         'if [ "${rebuild}" = "yes" ] && [ "${commit_given}" = "yes" ]; '
         'then', 'if false; then'),
        ('the check that the tag is annotated is removed',
         '[ "$(git -C "${project_dir}" cat-file -t "${tag_object}")" = '
         '"tag" ] ||', 'false ||'),
        ('--rebuild makes the tag too',
         'if [ "${rebuild}" = "no" ]; then\n'
         '    if ! git -C "${project_dir}" tag -a',
         'if true; then\n'
         '    if ! git -C "${project_dir}" tag -a'),
        ('--rebuild takes the rollback of the tag',
         '    tag_made="yes";\nfi\n',
         '    tag_made="yes";\nfi\n'
         'if [ "${rebuild}" = "yes" ]; then tag_made="yes"; fi\n'))
    for label, old, new in breaks:
        if old not in text:
            out.append((label, None))
        else:
            out.append((label, text.replace(old, new, 1)))
    return out


def main():
    tool_text = read(os.path.join(scripts, 'strata-release'))
    scenarios(tool_text, 'tool')
    real_dry_run()
    for label, text in spoiled(tool_text):
        if text is None:
            check('mutation: %s (the text to spoil is in the script)' %
                  label, False)
            continue
        global quiet, quiet_fails
        quiet = True
        quiet_fails = 0
        try:
            scenarios(text, 'mutant')
        except (OSError, subprocess.SubprocessError, IndexError):
            # A spoiled copy can break a later step of the scenario: that is
            # a failed check as well.
            quiet_fails += 1
        quiet = False
        check('mutation: a copy of the tool where %s fails a check' % label,
              quiet_fails > 0)


if __name__ == '__main__':
    main()
    print('release: %s' % ('all passed' if fails == 0 else
                           '%d FAILURES' % fails))
    shutil.rmtree(tmp, ignore_errors=True)
    sys.exit(1 if fails else 0)
