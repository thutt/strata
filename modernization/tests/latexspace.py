#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The check manual/checklatexspace.py: a LaTeX marker that holds vertical
space fails it, a Whitespace marker around a LaTeX marker that holds only the
logo passes it, and the documents of the manual and the story sample pass it.
A mutation (the title page generators with the space back in the LaTeX
marker) is the first case: it must fail.

    python3 -I -B tests/latexspace.py"""
import glob
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CHECK = os.path.join(ROOT, "manual", "checklatexspace.py")
CRLF = b"\r\n"
fails = 0
tmp = tempfile.mkdtemp(prefix="lspace")


def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok:
        fails += 1
        if detail != "":
            print("       " + str(detail)[:300])


def doc(*blocks):
    body = b"".join(b"S" + CRLF + b"T" + b + b"\x1d\x00" + CRLF
                    for b in blocks)
    return (b"*R3" + CRLF + b"MR1001" + CRLF + b"*B0" + CRLF + body + b"*E" +
            CRLF)


def latex(text):
    return b"\x1341(S" + CRLF + b"T" + text + b"\x00" + CRLF + b"\x13)"


def whitespace(size, unit):
    return b"\x1331(" + size + CRLF + unit + CRLF + b"\x13)"


def run(data, name):
    path = os.path.join(tmp, name + ".doc")
    with open(path, "wb") as f:
        f.write(data)
    p = subprocess.run([sys.executable, "-I", "-B", CHECK, path],
                       capture_output=True, text=True)
    return p.returncode, p.stdout


old = doc(latex(b"\\vspace{3ex}\\stratalogo[3]\\vspace{3ex}"))
code, out = run(old, "old")
check("a LaTeX marker with \\vspace around the logo fails the check "
      "and the check names the text", code == 1 and "vspace" in out, out)
for word in (b"\\vfill", b"\\bigskip", b"\\medskip", b"\\smallskip",
             b"\\vskip 2pt", b"\\addvspace{1ex}", b"\\vspace*{1ex}"):
    code, out = run(doc(latex(b"\\TeX{}" + word)), "w")
    check("a LaTeX marker with %s fails the check" % word.decode(),
          code == 1, out)
new = doc(whitespace(b"17300", b"U3"), latex(b"\\stratalogo[3]"),
          whitespace(b"17300", b"U3"))
code, out = run(new, "new")
check("Whitespace markers around a LaTeX marker with only the logo pass the "
      "check", code == 0, out)
code, out = run(doc(latex(b"\\strata"), latex(b"$\\vspacex$")), "other")
check("a command whose name only begins like a space command passes",
      code == 0, out)
p = subprocess.run([sys.executable, "-I", "-B", CHECK],
                   capture_output=True, text=True)
check("the documents of the manual and the story sample pass the check "
      "(the cards are not looked at)", p.returncode == 0, p.stdout[-300:])
print("latexspace: %d failures" % fails)
sys.exit(1 if fails else 0)
