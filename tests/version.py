#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The version of Strata is the name of the branch that is built.

    python3 -I -B tests/version.py [BUILD_DIR]

scripts/strata-version derives the version in a source root with git; the
rules are in docs/VERSIONING.txt.  This test makes scratch repositories and
checks the derivation: a release or a defect branch gives its number, any
other branch gives its name and the short SHA, a checkout of a release tag
gives the number of the tag, and everything else (no git repository, a tree
that is only a part of a repository, a detached HEAD without a release tag)
is UNSUPPORTED BUILD CONFIGURATION.  The option --manual gives the number or
the word development, without a SHA, for the title page of the manuals.

With BUILD_DIR (the build directory of the build that the unit tests use) it
also checks that the string reaches the programs: --version of stred, str and
strprint, and the file BUILD_DIR/version that the install script copies.  The
title page of the committed Getting Started document is checked against the
version that the generator makes.

The checks of the derivation are made again on copies of the script that have
been spoiled, one rule at a time, and every copy has to fail a check.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
modernization = os.path.dirname(here)
root = os.path.dirname(modernization)
script = os.path.join(root, "scripts", "strata-version")
UNSUPPORTED = "UNSUPPORTED BUILD CONFIGURATION"

fails = 0


def check(label, ok, detail=""):
    global fails
    if ok:
        print("ok   " + label)
    else:
        fails += 1
        print("FAIL " + label)
        if detail != "":
            print("     " + str(detail))


def git(cwd, *args):
    """Run git in cwd; return its output without the final newline."""
    env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t",
               GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@t",
               GIT_CONFIG_GLOBAL="/dev/null", GIT_CONFIG_SYSTEM="/dev/null")
    r = subprocess.run(["git", "-C", cwd] + list(args),
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       env=env)

    return r.stdout.decode("ascii", "replace").strip()


def derive(tool, tree, manual=False):
    """The version that the script tool derives for the source root tree."""
    cmd = [tool] + (["--manual"] if manual else []) + [tree]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    return r.stdout.decode("ascii", "replace").strip()


def make_repo(base):
    """A repository with one commit; returns its path and the short SHA."""
    repo = os.path.join(base, "repo")
    os.makedirs(repo)
    git(repo, "init", "-q", "-b", "start")
    with open(os.path.join(repo, "a.txt"), "w") as f:
        f.write("a\n")
    git(repo, "add", "a.txt")
    git(repo, "commit", "-q", "-m", "first")

    return repo, git(repo, "rev-parse", "--short", "HEAD")


def cases(tool, base, tag):
    """The failures of the derivation of tool on the scratch repositories."""
    bad = []
    repo, sha = make_repo(base)

    def expect(label, want, manual=False):
        got = derive(tool, repo, manual)
        if got != want:
            bad.append("%s: want %r, got %r" % (label, want, got))

    for branch, want, manual_want in (
            ("1.2.0", "1.2.0", "1.2.0"),
            ("1.2.57", "1.2.57", "1.2.57"),
            ("development-1", "development-1 " + sha, "development"),
            ("port-to-c", "port-to-c " + sha, "development"),
            ("feature/x", "feature/x " + sha, "development")):
        git(repo, "checkout", "-q", "-B", branch)
        expect("branch " + branch, want)
        expect("branch " + branch + " (--manual)", manual_want, True)

    git(repo, "checkout", "-q", "-B", "1.2.0")
    git(repo, "tag", "v1.2.0")
    git(repo, "checkout", "-q", "--detach", "v1.2.0")
    expect("detached at the tag v1.2.0", "1.2.0")
    expect("detached at the tag v1.2.0 (--manual)", "1.2.0", True)

    git(repo, "checkout", "-q", "-B", "port-to-c")
    with open(os.path.join(repo, "a.txt"), "a") as f:
        f.write("b\n")
    git(repo, "commit", "-q", "-a", "-m", "second")
    git(repo, "checkout", "-q", "--detach", "HEAD")
    expect("detached at an untagged SHA", UNSUPPORTED)
    git(repo, "tag", "v1.2.1")
    expect("detached at a tag that is not v<x>.<y>.0", UNSUPPORTED)

    plain = os.path.join(base, "plain")
    os.makedirs(plain)
    with open(os.path.join(plain, "a.txt"), "w") as f:
        f.write("a\n")
    got = derive(tool, plain)
    if got != UNSUPPORTED:
        bad.append("a tree without a repository: %r" % got)

    git(repo, "checkout", "-q", "-B", "1.2.0")
    sub = os.path.join(repo, "sub")
    os.makedirs(sub, exist_ok=True)
    got = derive(tool, sub)
    if got != UNSUPPORTED:
        bad.append("a directory inside a repository: %r" % got)

    return bad


def spoiled(base):
    """Copies of the script that each break one rule, with the rule."""
    with open(script) as f:
        text = f.read()
    breaks = (
        ("the branch is not checked against x.y.z",
         "[[ \"${branch}\" =~ ^[0-9]+\\.[0-9]+\\.[0-9]+$ ]]", "false"),
        ("the SHA is left out", 'echo "${branch} ${sha}";',
         'echo "${branch}";'),
        ("a tag is not looked at", "git -C \"${root}\" tag --points-at HEAD",
         "echo"),
        ("a part of a repository is accepted",
         '[ "$(cd "${top}" && pwd -P)" = "$(cd "${root}" && pwd -P)" ] ||'
         ' return 1;', ":"),
        ("the manual shows the branch", 'echo "development";',
         'echo "${branch}";'))
    out = []
    for label, old, new in breaks:
        if old not in text:
            out.append((label, None))
            continue
        path = os.path.join(base, "bad%d" % len(out))
        with open(path, "w") as f:
            f.write(text.replace(old, new, 1))
        os.chmod(path, 0o755)
        out.append((label, path))

    return out


def run_derivation():
    base = tempfile.mkdtemp(prefix="ver")
    try:
        bad = cases(script, os.path.join(base, "good"), "good")
        check("the derivation of the version gives the number of a release or "
              "defect branch, the name and the SHA of any other branch, the "
              "number of a release tag, and UNSUPPORTED BUILD "
              "CONFIGURATION for everything else", bad == [], bad)
        for label, path in spoiled(base):
            if path is None:
                check("mutation: %s (the text to spoil is in the script)"
                      % label, False)
                continue
            work = os.path.join(base, "m" + os.path.basename(path))
            bad = cases(path, work, "bad")
            check("mutation: a script where %s fails a check" % label,
                  bad != [])
    finally:
        shutil.rmtree(base, ignore_errors=True)


def run_build(bd):
    """The version in the programs, the build and the title page."""
    want = derive(script, root)
    check("the version of this source tree is not empty", want != "", want)
    for prog in ("stred", "str", "strprint"):
        exe = os.path.join(bd, "term", prog)
        r = subprocess.run([exe, "--version"], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT)
        # The first line is the version; the next line names the file
        # format (job of the file format version).
        out = r.stdout.decode("ascii", "replace").strip().split("\n")[0]
        check("%s --version prints the version of the branch (%s)"
              % (prog, want), r.returncode == 0 and
              out == "%s (Strata) %s" % (prog, want), out)

    path = os.path.join(bd, "version")
    try:
        with open(path) as f:
            body = f.read()
    except OSError as e:
        body = str(e)
    check("the build writes its version into the file BUILD_DIR/version, "
          "which the install script copies", body == want + "\n", body)

    sys.path.insert(0, os.path.join(modernization, "manual"))
    import mkmanual
    manual_want = derive(script, root, True)
    check("the generator shows the version without a SHA (%s)" % manual_want,
          mkmanual.VERSION == manual_want and
          re.search(r"\s[0-9a-f]{7,}$", mkmanual.VERSION) is None,
          mkmanual.VERSION)

    doc = os.path.join(modernization, "manual", "doc", "GettingStarted.doc")
    with open(doc, "rb") as f:
        data = f.read()
    check("the title page of the committed Getting Started document has "
          "the same form of version (%s)" % manual_want,
          (b"TVersion " + manual_want.encode("ascii")) in data or
          (b"Version " + manual_want.encode("ascii")) in data,
          data[data.find(b"Version "):][:30])
    check("the tree has no file VERSION: the branch is the version",
          not os.path.exists(os.path.join(modernization, "VERSION")))


if __name__ == "__main__":
    run_derivation()
    if len(sys.argv) > 1:
        run_build(os.path.abspath(sys.argv[1]))
    print("version: %s" % ("all passed" if fails == 0 else
                           "%d FAILURES" % fails))
    sys.exit(1 if fails else 0)
