#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This test checks that the names LaTeX and TeX of the documents of the
# manual are LaTeX markers with the logo (manual/mkmanual.py, the constants
# LATEX_WORD and TEX_WORD) and that the check of the documents finds a name
# that is not (manual/checkstrata.py):
#
#   - the generator makes a marker \LaTeX{} or \TeX{} of the name in a
#     paragraph, a headline, a table cell, a list item, a note, a bold word
#     and a caption, and of the name beside a hyphen or a parenthesis;
#   - it leaves the name plain in the text of a footnote, because the text
#     of a footnote holds no marker (the Editor and the Printer would show
#     and print the marker as its raw bytes and cut the footnote off), and
#     the check fails for a marker inside a footnote;
#   - it leaves the name plain in the program font, a code block, a quoted
#     message, a word that is part of another word (pdfTeX,
#     TeXbook, LaTeX2e), a command (\LaTeX) and a path;
#   - the check passes the document that the generator makes, and fails
#     (naming the word) when the conversion is switched off, when the name
#     is plain in each of those places, and passes the places that stay
#     plain;
#   - the documents of the whole manual have no plain name (when the
#     documents are in the build directory).
#
#     python3 -I -B tests/latexname.py

import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
MKMANUAL = os.path.join(ROOT, "manual", "mkmanual.py")
CHECK = os.path.join(ROOT, "manual", "checkstrata.py")
fails = 0
tmp = tempfile.mkdtemp(prefix="lname")


def check(name, ok, detail=""):
    global fails
    if ok:
        print("ok   %s" % name)
    else:
        fails += 1
        print("FAIL %s" % name)
        if detail != "":
            print("       " + str(detail).replace("\n", "\n       ")[:400])


SOURCE = """.doc A description with LaTeX in it
.h1 The LaTeX name [a]
.ix LaTeX index entry
LaTeX in a paragraph, and TeX too; (LaTeX) and (e-TeX) and LaTeX-style.
- A list item about LaTeX.
- Another about TeX.
.table 20 40
Name | Text
LaTeX | the name of TeX
.end
{b:LaTeX bold} and a note{fn:LaTeX in a footnote} here.
Plain: {c:LaTeX and TeX} and "LaTeX reports: MESSAGE." and pdfTeX and TeXbook
and LaTeX2e and /usr/share/LaTeX and \\LaTeX.
.code
LaTeX code and TeX code
.end
"""


def make(src, off=False, name="d"):
    """Make the document of a source, with the conversion on or off; the
    bytes of the document, or None."""
    path = os.path.join(tmp, name + ".src")
    out = os.path.join(tmp, name + ".doc")
    with open(path, "w") as f:
        f.write(src)
    if off:
        code = ("import sys; sys.argv=['x', %r, %r]; sys.path.insert(0, %r);"
                "import mkmanual; mkmanual.NAME_MARKS = (); "
                "sys.exit(mkmanual.main(sys.argv))" % (
                    path, out, os.path.dirname(MKMANUAL)))
        p = subprocess.run([sys.executable, "-I", "-B", "-c", code],
                           capture_output=True, text=True)
    else:
        p = subprocess.run([sys.executable, "-I", "-B", MKMANUAL, path, out],
                           capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stderr)
        return None
    with open(out, "rb") as f:
        return f.read()


def run_check(data, name="c"):
    path = os.path.join(tmp, name + ".doc")
    with open(path, "wb") as f:
        f.write(data)
    p = subprocess.run([sys.executable, "-I", "-B", CHECK, path],
                       capture_output=True, text=True)
    return p.returncode, p.stdout


good = make(SOURCE)
check("the generator makes the document of the source", good is not None)
good = good or b""
body = good[good.find(b"*B"):]
n_latex = body.count(b"\\LaTeX{}")
n_tex = body.count(b"\\TeX{}")
# The marks: the headline, the paragraph (LaTeX, (LaTeX), LaTeX-style), a
# list item, a table cell and a bold word: seven.  The footnote has none.
check("the name LaTeX is a marker with \\LaTeX{} in the text, the headline, "
      "a list, a table and a bold word (7 places)",
      n_latex == 7, n_latex)
check("the name TeX is a marker with \\TeX{} in the text, a list and a table, "
      "and in e-TeX (4 places)", n_tex == 4, n_tex)
plain = body
plain = re.sub(rb"\x13\d+\(.*?\x13\)", b"", plain, flags=re.S)
for what, word in (("the program font", b"LaTeX and TeX"),
                   ("a quoted message", b"\"LaTeX reports: MESSAGE.\""),
                   ("pdfTeX", b"pdfTeX"), ("TeXbook", b"TeXbook"),
                   ("LaTeX2e", b"LaTeX2e"),
                   ("a path", b"/usr/share/LaTeX"),
                   ("a command", b"\\LaTeX."),
                   ("the code block", b"LaTeX code and TeX code")):
    check("the name stays plain in %s" % what, word in body)
head = good[:good.find(b"*B")]
check("the description of the document keeps the plain name",
      b"A description with LaTeX in it" in head)
# The check.
code, out = run_check(good)
check("the check passes the document that the generator makes", code == 0,
      out)
off = make(SOURCE, off=True, name="off")
code, out = run_check(off or b"", "off")
check("the check fails when the conversion is switched off, and names the "
      "word", code == 1 and "LaTeX" in out and "TeX" in out, out[:300])
for what, text in (
        ("a paragraph", "LaTeX is here.\n"),
        ("a headline", ".h1 The LaTeX name [a]\nText.\n"),
        ("a table cell", ".table 20 40\nA | B\nx | TeX here\n.end\n"),
        ("a list item", "- One TeX item.\n")):
    data = make(".h1 H [h]\n" + text, off=True, name="o2")
    code, out = run_check(data or b"", "o2")
    check("the check fails for the plain name in %s" % what,
          code == 1, out[:200])
# A plain name made by hand in a good document.
for what, old, new in (
        ("a marker replaced by the plain word",
         b"\x1341(S\r\nT\\LaTeX{}\x1d\x00\r\n\x13)", b"LaTeX"),):
    data = good.replace(old, new, 1)
    code, out = run_check(data, "o3")
    check("the check fails when one marker is the plain word", code == 1,
          out[:200])
# The text of a footnote holds no marker.
NOTE = ".h1 H [h]\nA note{fn:Strata prints LaTeX and TeX here} ends.\n"
note = make(NOTE, name="note")
check("the text of a footnote is plain: the words Strata, LaTeX and TeX "
      "are not markers in it",
      note is not None and
      b"1(S\r\nTStrata prints LaTeX and TeX here\x1d\x00\r\n\x13)" in note,
      (note or b"")[-200:])
code, out = run_check(note or b"", "note")
check("the check passes a footnote with the plain names", code == 0, out)
if note is not None:
    nested = note.replace(
        b"Strata prints",
        b"\x1341(S\r\nT\\strata\x1d\x00\r\n\x13) prints", 1)
    code, out = run_check(nested, "nested")
    check("the check fails for a marker inside a footnote and says so",
          code == 1 and "inside the text of a note" in out, out[:300])
# The print of a footnote shows its whole text at the foot of the page.
BUILD = os.environ.get("STRATA_BUILD_DIR", "")
PRINTER = os.path.join(BUILD, "term", "strprint")
if BUILD != "" and os.path.exists(PRINTER) and \
        shutil.which("pdflatex") is not None and \
        shutil.which("pdftotext") is not None:
    fn_text = ("A footnote is a marker that holds text. Strata prints it at "
               "the foot of the page.")
    data = make(".h1 H [h]\nA word{fn:%s} ends.\n" % fn_text, name="print")
    pdf = os.path.join(tmp, "print.pdf")
    subprocess.run([PRINTER, os.path.join(tmp, "print.doc"), "-o", pdf],
                   capture_output=True, text=True)
    p = subprocess.run(["pdftotext", pdf, "-"], capture_output=True,
                       text=True)
    flat = " ".join(p.stdout.split())
    check("the print has the whole text of the footnote at the foot of the "
          "page", fn_text in flat and "41(" not in flat, flat[-300:])
# The places that stay plain pass.
only = make('.h1 H [h]\n{c:LaTeX} and "TeX stopped" and pdfTeX.\n'
            '.code\nLaTeX\n.end\n', name="only")
code, out = run_check(only or b"", "only")
check("the program font, a quoted message, pdfTeX and a code block pass",
      code == 0, out)

# The documents of the whole manual.
docs = []
for pat in (os.environ.get("STRATA_BUILD_DIR", ""),):
    if pat:
        docs = glob.glob(os.path.join(pat, "manual", "doc", "*.doc"))
if docs:
    p = subprocess.run([sys.executable, "-I", "-B", CHECK] + docs,
                       capture_output=True, text=True)
    check("no document of the manual (%d) has a plain name" % len(docs),
          p.returncode == 0, p.stdout[-400:])
shutil.rmtree(tmp, ignore_errors=True)
print("latexname: %d failures" % fails)
sys.exit(1 if fails else 0)
