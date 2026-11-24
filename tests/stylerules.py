#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This test checks the style rules of the manual (manual/checkstyle.py) and
# the name of the Getting Started document:
#
#   - the sources of the manual pass the check;
#   - every rule fails on a source that breaks it, and the report names
#     the rule: S1 (a path, a file name, an option, an environment
#     variable, a setting key or a command of the system in plain text),
#     S2 (a shell variable without braces, in the text, in the program font
#     and in a code block), S3 (a command line in a sentence) and S4 (the
#     words rights, computer and prefix);
#   - a source that follows the rules, and the places that are not checked
#     (a code block, a quoted message, the files of the messages, an
#     equation, a phrase of the allow file), pass;
#   - the same cases give no failure from a checker that finds nothing, so
#     the cases would notice a check that was switched off;
#   - check-style is part of the target check of build.sh and has its line
#     in the help;
#   - the Getting Started document is GettingStarted.doc, made from
#     GettingStarted.src: no old name is left in the sources, the tests,
#     the scripts, the Makefile and the manual build.
#
#     python3 -I -B tests/stylerules.py

import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
TOOL = os.path.join(ROOT, "manual", "checkstyle.py")
OLD_NAME = "get" + "start"

fails = 0


def check(name, ok, detail=""):
    """Print one result and count a failure."""
    global fails
    if ok:
        print("ok   %s" % name)
    else:
        fails += 1
        print("FAIL %s" % name)
        if detail != "":
            print("       " + detail.replace("\n", "\n       "))


def run(tool, text, name="x.src"):
    """Run the checker on a source made of text; the exit status and the
    output."""
    d = tempfile.mkdtemp()
    try:
        path = os.path.join(d, name)
        with open(path, "w") as f:
            f.write(text)
        p = subprocess.run([sys.executable, "-I", "-B", tool, path],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           universal_newlines=True)
        return p.returncode, p.stdout
    finally:
        shutil.rmtree(d)


# What breaks a rule: (name, source, the rule that must be named).
BAD = [
    ("a path", "Look in /usr/share/strata for it.\n", "S1"),
    ("a path of the home", "Look in ~/.strata for it.\n", "S1"),
    ("a path with two slashes", "Run share/strata/doc now.\n", "S1"),
    ("a file name", "Open report.doc and read it.\n", "S1"),
    ("an extension", "Every file ends in .pdf here.\n", "S1"),
    ("an option", "Give the option --prefix to it.\n", "S1"),
    ("a short option", "Give the option -y to it.\n", "S1"),
    ("an environment variable", "Set STRATA_HOME to a folder.\n", "S1"),
    ("a plain environment variable", "Set DISPLAY to a server.\n", "S1"),
    ("a setting key", "The key print_to has a value.\n", "S1"),
    ("a command", "Run sudo and then the program.\n", "S1"),
    ("a command in a table", "Name | Use\naspell | Spell check\n", "S1"),
    ("a path over two lines of a command",
     "Set {c:a path that goes\nover two lines} and then /usr/bin/x now.\n",
     "S1"),
    ("a variable in the text", "Go to $HOME and look.\n", "S2"),
    ("a variable in the program font", "Go to {c:$HOME/usr} and look.\n",
     "S2"),
    ("a variable in a code block", ".code\necho $HOME\n.end\n", "S2"),
    ("a command line in a sentence",
     "Type {c:sudo apt-get install aspell} now.\n", "S3"),
    ("a script line in a sentence",
     "Type {c:scripts/strata-build install --prefix x} now.\n", "S3"),
    ("the word rights", "Run it with root rights.\n", "S4"),
    ("the word computer", "It must be on the computer.\n", "S4"),
    ("the word prefix", "Choose the prefix first.\n", "S4"),
]

# What follows the rules or is not checked: (name, source, file name).
GOOD = [
    ("the program font",
     "Give {c:--prefix} and {c:${{HOME}}/usr} to {c:scripts/strata-build}.\n",
     "x.src"),
    ("a command in the program font",
     "Install {c:aspell} and set {c:STRATA_HOME} and {c:print_to}.\n",
     "x.src"),
    ("a code block", ".code\nsudo apt-get install aspell ${HOME}\n.end\n",
     "x.src"),
    ("a code block with braces",
     ".code\necho ${HOME}/usr --prefix /usr/bin x.doc\n.end\n", "x.src"),
    ("a quoted message", 'The line "not found in /usr/bin" is shown.\n',
     "x.src"),
    ("the files of the messages", "It is /usr/bin/x with $HOME, sudo.\n",
     "messages9.src"),
    ("an equation", "See {eq:a_b = $x$ -x} here.\n", "x.src"),
    ("a keycap", "Press {k:Ctrl-S} now.\n", "x.src"),
    ("a comment and a directive",
     "# A comment with /usr/bin and $HOME.\n.if .STRATA_BUILD_TYPE # release\n"
     "Plain text.\n.endif\n", "x.src"),
    ("ordinary slashes", "Yes and/or no, and a built-in choice.\n", "x.src"),
    ("a phrase of the allow file",
     "The message Settings saved to PATH is shown.\n", "x.src"),
    ("the privileges and the prefix directory",
     "Without privileges use a prefix directory of your own system.\n",
     "x.src"),
]


def run_cases(tool):
    """The number of cases that did not come out as expected."""
    wrong = 0
    for name, text, rule in BAD:
        code, out = run(tool, text)
        if code != 1 or (" %s [" % rule) not in out:
            wrong += 1
    for name, text, fname in GOOD:
        code, out = run(tool, text, fname)
        if code != 0:
            wrong += 1
    return wrong


# 1. The sources of the manual pass.
p = subprocess.run([sys.executable, "-I", "-B", TOOL],
                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                   universal_newlines=True)
check("the sources of the manual follow the style rules", p.returncode == 0,
      "\n".join(p.stdout.split("\n")[:12]))

# 2. Every rule fails on a source that breaks it.
for name, text, rule in BAD:
    code, out = run(TOOL, text)
    check("%s fails the rule %s" % (name, rule),
          code == 1 and (" %s [" % rule) in out, out[:200])

# 3. What follows the rules passes.
for name, text, fname in GOOD:
    code, out = run(TOOL, text, fname)
    check("%s passes" % name, code == 0, out[:200])

# 4. A checker that finds nothing fails the cases.
d = tempfile.mkdtemp()
try:
    fake = os.path.join(d, "checkstyle.py")
    with open(fake, "w") as f:
        f.write("import sys\nprint('checkstyle: 0 hit(s)')\nsys.exit(0)\n")
    check("a checker that finds nothing fails the cases",
          run_cases(fake) == len(BAD))
    check("the real checker has no case that fails", run_cases(TOOL) == 0)
finally:
    shutil.rmtree(d)

# 5. check-style is part of check.
with open(os.path.join(ROOT, "manual", "build.sh")) as f:
    build = f.read()
check("build.sh has the function check-style",
      "function check-style" in build)
m = re.search(r"function check\n\{.*?\n\}\n", build, re.DOTALL)
check("the target check runs check-style",
      m is not None and "\n    check-style\n" in m.group(0))
check("the help of build.sh lists check-style",
      '"  check-style  ' in build)
check("the dispatch of build.sh has check-style",
      "check-style) check-style ;;" in build)

# 6. The reference to a figure of the generator.
GEN = os.path.join(ROOT, "manual", "mkmanual.py")


def generate(text):
    """Make a document from a source; the exit status, the output of the
    generator and the bytes of the document."""
    d = tempfile.mkdtemp()
    try:
        src = os.path.join(d, "t.src")
        out = os.path.join(d, "t.doc")
        with open(src, "w") as f:
            f.write(text)
        p = subprocess.run([sys.executable, "-I", "-B", GEN, src, out],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           universal_newlines=True)
        data = b""
        if os.path.exists(out):
            with open(out, "rb") as f:
                data = f.read()
        return p.returncode, p.stdout, data
    finally:
        shutil.rmtree(d)


code, out, data = generate(".doc T\n.h1 A\nSee {fig:one} and {Fig:two}.\n"
                           ".pic x.png 2 The first. [one]\n"
                           ".pic y.png 2 The second. [two]\n")
check("a figure reference gives the number of the figure",
      code == 0 and b"figure 1" in data and b"Figure 2" in data, out[:200])
check("the label is not part of the caption", b"[one]" not in data)
code, out, data = generate(".doc T\n.h1 A\nSee {fig:none}.\n"
                           ".pic x.png 2 The first. [one]\n")
check("a reference to a figure that has no label stops the build",
      code != 0 and "no figure has the label" in out, out[:200])
code, out, data = generate(".doc T\n.h1 A\n.pic x.png 2 One. [a]\n"
                           ".pic y.png 2 Two. [a]\n")
check("a label used twice stops the build",
      code != 0 and "used twice" in out, out[:200])

# 7. The name of the Getting Started document.
check("the source is GettingStarted.src",
      os.path.exists(os.path.join(ROOT, "manual", "src",
                                  "GettingStarted.src")))
check("the document is GettingStarted.doc",
      os.path.exists(os.path.join(ROOT, "manual", "doc",
                                  "GettingStarted.doc")))
left = []
for top in ("manual", "tests", "scripts", "src", "help", "Makefile",
            "README.txt", "../scripts"):
    path = os.path.join(ROOT, top)
    files = []
    if os.path.isdir(path):
        for d2, dirs, names in os.walk(path):
            dirs[:] = [x for x in dirs if x not in ("pictures", ".git")]
            for n in names:
                files.append(os.path.join(d2, n))
    elif os.path.exists(path):
        files.append(path)
    for fn in files:
        if OLD_NAME in os.path.basename(fn).lower():
            left.append(os.path.relpath(fn, ROOT))
        if os.path.abspath(fn) == os.path.abspath(__file__):
            continue
        try:
            with open(fn, encoding="ascii", errors="ignore") as f:
                if OLD_NAME in f.read().lower():
                    left.append(os.path.relpath(fn, ROOT))
        except OSError:
            pass
check("no file or text of the build names the old document",
      left == [], "\n".join(sorted(set(left))[:10]))

print("stylerules: %s" % ("all passed" if fails == 0 else
                          "%d failed" % fails))
sys.exit(1 if fails > 0 else 0)
