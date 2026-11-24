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

"""The copy of a branch that can be published.

    python3 -I -B tests/publishclone.py

scripts/strata-publish-clone makes a copy of a branch in which the original
program disks (the directory lotus-manuscript) are in no commit, no line of
a commit message names anthropic, and no author or committer address names
anthropic.  The test makes a scratch repository that has all three, runs the
script and checks the copy, that the repository itself is unchanged, that a
second run gives the same last commit, and the refusals.  The checks are made
again on copies of the script that have been spoiled, one rule at a time, and
every copy has to fail a check.
"""

import os
import shutil
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
project = os.path.dirname(here)
script = os.path.join(project, 'scripts', 'strata-publish-clone')
fails = 0
OWNER = ('Owner Name', 'owner@example.org')
TOOL = ('Claude Sonnet 5.5', 'noreply@' + 'anthropic' + '.com')


def check(label, ok, detail=''):
    global fails
    if ok:
        print('ok   ' + label)
    else:
        fails += 1
        print('FAIL ' + label)
        if detail != '':
            print('     ' + str(detail))


def run(cmd, cwd=None, env=None):
    r = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    return r.returncode, r.stdout.decode('utf-8', 'replace')


def git(repo, *args, who=OWNER, **kw):
    env = dict(os.environ, GIT_CONFIG_GLOBAL='/dev/null',
               GIT_CONFIG_SYSTEM='/dev/null',
               GIT_AUTHOR_NAME=who[0], GIT_AUTHOR_EMAIL=who[1],
               GIT_COMMITTER_NAME=OWNER[0], GIT_COMMITTER_EMAIL=OWNER[1],
               GIT_AUTHOR_DATE='2026-01-01T12:00:00',
               GIT_COMMITTER_DATE='2026-01-01T12:00:00')
    return run(['git', '-C', repo] + list(args), env=env)[1].strip()


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write(text)


def make_project(base, name):
    """A scratch project with the script and a repository of four commits."""
    repo = os.path.join(base, name)
    os.makedirs(os.path.join(repo, 'scripts'))
    shutil.copy(script, os.path.join(repo, 'scripts', 'strata-publish-clone'))
    git(repo, 'init', '-q', '-b', 'main')
    git(repo, 'config', 'user.name', OWNER[0])
    git(repo, 'config', 'user.email', OWNER[1])
    write(os.path.join(repo, 'lotus-manuscript', 'ms.exe'), 'original\n')
    write(os.path.join(repo, 'lotus-manuscript', 'sample', 'a.doc'), 'doc\n')
    git(repo, 'add', 'lotus-manuscript')
    git(repo, 'commit', '-q', '-m', 'Initial dump of the original disks')
    write(os.path.join(repo, 'tracked.txt'), 'one\n')
    write(os.path.join(repo, 'extra', 'x.txt'), 'x\n')
    git(repo, 'add', '-A')
    git(repo, 'commit', '-q', '-m',
        'Add the first file\n\nDescription:\n  What: a file.\n\n\n'
        'Testing:\n  none.\n\nCo-Authored-By: %s <%s>' % TOOL, who=TOOL)
    write(os.path.join(repo, 'tracked.txt'), 'two\n')
    git(repo, 'add', '-A')
    git(repo, 'commit', '-q', '-m',
        'Change the file\n\nWe read the anthropic notes here.\nMore text.')
    git(repo, 'checkout', '-q', '-b', 'other')
    write(os.path.join(repo, 'other.txt'), 'other\n')
    git(repo, 'add', '-A')
    git(repo, 'commit', '-q', '-m', 'On another branch', who=TOOL)
    git(repo, 'checkout', '-q', 'main')

    return repo


def publish(repo, target, *args, tool='strata-publish-clone'):
    return run([os.path.join(repo, 'scripts', tool), '--branch', 'main',
                '--name', OWNER[0], '--email', OWNER[1]] + list(args) +
               [target])


def cases(base):
    repo = make_project(base, 'src')
    before = git(repo, 'for-each-ref')
    target = os.path.join(base, 'copy')
    st, out = publish(repo, target)
    check('a copy is made in the directory that was named', st == 0 and
          os.path.isdir(target), out[-400:])
    msgs = git(target, 'log', '--all', '--format=%B')
    ids = git(target, 'log', '--all', '--format=%an %ae %cn %ce')
    check('no line of a commit message names anthropic',
          'anthropic' not in msgs.lower(), msgs)
    check('no author or committer names anthropic',
          'anthropic' not in ids.lower(), ids)
    check('the rest of a message stays: its text, Description and Testing',
          'Description:' in msgs and 'Testing:' in msgs and
          'More text.' in msgs and 'Change the file' in msgs, msgs)
    check('a message does not end with a blank line',
          git(target, 'log', '-1', '--skip=1', '--format=%B').rstrip('\n') ==
          git(target, 'log', '-1', '--skip=1', '--format=%B').strip('\n'))
    names = git(target, 'log', '--all', '--name-only', '--format=')
    check('no commit holds the directory lotus-manuscript',
          'lotus-manuscript' not in names, names)
    check('the commit that held only the disks is gone: 2 of 3 commits '
          'remain', git(target, 'rev-list', '--count', 'main') == '2',
          git(target, 'rev-list', '--count', 'main'))
    check('only the branch asked for is in the copy',
          git(target, 'for-each-ref', '--format=%(refname)') ==
          'refs/heads/main', git(target, 'for-each-ref'))
    check('the copy has no remote and no saved refs of the rewrite',
          git(target, 'remote') == '' and
          'refs/original' not in git(target, 'for-each-ref'))
    check('the files of the last commit are those of the branch',
          open(os.path.join(target, 'tracked.txt')).read() == 'two\n' and
          os.path.isfile(os.path.join(target, 'extra', 'x.txt')))
    check('the scratch repository is unchanged, and still has its history',
          git(repo, 'for-each-ref') == before and
          'anthropic' in git(repo, 'log', '--all', '--format=%B').lower())

    second = os.path.join(base, 'copy2')
    st, out = publish(repo, second)
    check('a second run gives the same last commit',
          st == 0 and git(target, 'rev-parse', 'main') ==
          git(second, 'rev-parse', 'main'))

    st, out = publish(repo, target)
    check('a directory that exists is refused with status 1',
          st == 1 and 'exists already' in out, out)
    st, out = run([os.path.join(repo, 'scripts', 'strata-publish-clone'),
                   '--branch', 'main', '--name', 'x',
                   '--email', 'a@' + 'anthropic' + '.com',
                   os.path.join(base, 'c3')])
    check('an identity that names anthropic is refused', st == 1 and
          not os.path.exists(os.path.join(base, 'c3')), out)
    st, out = run([os.path.join(repo, 'scripts', 'strata-publish-clone'),
                   '--branch', 'nothere', '--name', 'x', '--email', 'a@b',
                   os.path.join(base, 'c4')])
    check('a branch that does not exist is refused', st == 1, out)
    st, out = publish(repo, os.path.join(base, 'c5'), '--exclude', 'extra')
    check('--exclude removes another path as well', st == 0 and
          'extra' not in git(os.path.join(base, 'c5'), 'log', '--all',
                             '--name-only', '--format=') and
          not os.path.exists(os.path.join(base, 'c5', 'extra')), out[-300:])
    return repo


def spoiled(base):
    """Copies of the script that each break one rule, with the rule."""
    with open(script) as f:
        text = f.read()
    breaks = (
        ('the message filter is left out',
         '--msg-filter "perl \'${scratch}/message.pl\'" \\\n', ''),
        ('the disks are not removed',
         'git rm -r -q --cached --ignore-unmatch', 'true'),
        ('the addresses are not replaced',
         '--env-filter ". \'${scratch}/env.sh\'" \\\n', ''))
    out = []
    for label, old, new in breaks:
        if old not in text:
            out.append((label, None))
            continue
        out.append((label, text.replace(old, new, 1)))

    return out


def main():
    base = tempfile.mkdtemp(prefix='pubclone.')
    try:
        cases(os.path.join(base, 'good'))
        for n, (label, text) in enumerate(spoiled(base)):
            if text is None:
                check('mutation: %s (the text to spoil is in the script)'
                      % label, False)
                continue
            work = os.path.join(base, 'bad%d' % n)
            os.makedirs(work)
            repo = make_project(work, 'src')
            path = os.path.join(repo, 'scripts', 'strata-publish-clone')
            with open(path, 'w') as f:
                f.write(text)
            os.chmod(path, 0o755)
            target = os.path.join(work, 'copy')
            st, out = publish(repo, target)
            check('mutation: a script where %s fails its own checks '
                  '(status 1, no copy left)' % label,
                  st == 1 and not os.path.exists(target), out[-300:])
    finally:
        shutil.rmtree(base, ignore_errors=True)
    print('publishclone: %s' % ('all passed' if fails == 0 else
                                '%d FAILURES' % fails))
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
