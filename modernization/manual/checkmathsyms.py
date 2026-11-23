#!/usr/bin/env python3
# Copyright (c) 2026 Logic Magicians Software
#
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This program checks the Math Symbols card.  It does three things.
#
#   1. Every entry of the list (manual/mathsyms.txt) is typeset by the Printer
#      in an equation of a test document.  The check fails when the Printer
#      reports a fault of an equation, so that no entry that fails can be on
#      the sheet, and a new version of the packages or of Strata that breaks
#      a symbol is found.
#   2. Every symbol that the packages which Strata loads for math define is
#      on the sheet or in the list of the symbols that are left out with the
#      reason (manual/mathsyms-left-out.txt).  The report says how many
#      symbols there are in each class and how many are left out.
#   3. With a PDF of the card: it has two pages, the three panels of a side
#      are equal in width (as equal as the fold of letter paper needs), no
#      text is outside the margins of 5 mm, and no panel has text that runs
#      over the foot of the page.
#
#     python3 -I manual/checkmathsyms.py STRPRINT [CARD.pdf]
#
# STRPRINT is the Printer.  The exit status is 1 when a check fails.

import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# The files of the packages that the Printer loads for math (amsmath and
# amssymb, with the fonts and the symbols of the LaTeX kernel).
PACKAGES = [("fontmath.ltx", "base"), ("amsfonts.sty", "amsfonts"),
            ("amssymb.sty", "amsfonts"), ("amsopn.sty", "amsmath"),
            ("amsmath.sty", "amsmath")]

# What kind of definition makes a symbol of a name.
DEFINERS = [
    (re.compile(r"\\DeclareMathSymbol\s*\{\\(\w+)\}\s*\{\\(\w+)\}"), 2),
    (re.compile(r"\\(?:ams@)?DeclareMathDelimiter\s*\{\\(\w+)\}\s*"
                r"\{\\(\w+)\}"), 2),
    (re.compile(r"\\DeclareMathDelimiter\s*\{\\(\w+)\}\s*()"), 0),
    (re.compile(r"\\DeclareMathAccent\s*\{\\(\w+)\}\s*\{\\(\w+)\}"), 2),
    (re.compile(r"\\DeclareRobustCommand\s*\{?\\(\w+)\}?()"), 0),
    (re.compile(r"\\protected\\def\\(\w+)()"), 0),
    (re.compile(r"\\DeclareMathOperator\s*\*?\s*\{\\(\w+)\}()"), 0),
    (re.compile(r"\\(?:global)?\\let\\(\w+)\\(\w+)"), 0),
]


def find(name):
    """The path of a TeX file."""
    done = subprocess.run(["kpsewhich", name], capture_output=True, text=True)
    return done.stdout.strip()


def inventory():
    """The names that the packages define as symbols, with the class (the
    way a symbol takes part in a formula) that the definition gives."""
    names = {}
    for fname, _ in PACKAGES:
        path = find(fname)
        if path == "":
            continue
        with open(path, encoding="latin-1") as f:
            text = f.read()
        text = re.sub(r"(?m)^\s*%.*$", "", text)
        for rx, group in DEFINERS:
            for m in rx.finditer(text):
                name = m.group(1)
                kind = m.group(2) if group == 2 else "other"
                if "@" in name or name.startswith("ams"):
                    continue
                names.setdefault(name, kind)
    return names


def read_left_out(path):
    """The names that are left out of the sheet, with the reasons."""
    left = {}
    with open(path, encoding="ascii") as f:
        for line in f:
            line = line.rstrip("\n")
            if line == "" or line.startswith("#"):
                continue
            name, _, reason = line.partition(" | ")
            left[name.strip().lstrip("\\")] = reason.strip()
    return left


def listed_names(groups):
    """The command names that the list mentions: every word that begins with
    a backslash in the text of an entry, and the aliases that a note names."""
    names = set()
    for g in groups:
        for e in g.entries:
            for m in re.finditer(r"\\([A-Za-z]+)", e.tex + " " + e.typed):
                names.add(m.group(1))
    return names


def typeset(strprint, groups):
    """Typeset every entry in an equation; return the faults."""
    texts = sorted({e.tex for g in groups for e in g.entries})
    lines = [".doc Strata Math Check", ".numbering off", ".h1 Check"]
    for tex in texts:
        lines.append("{eq:%s}" % tex.replace("{", "{{").replace("}", "}}"))
        lines.append("")
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "check.src")
        doc = os.path.join(tmp, "check.doc")
        with open(src, "w", encoding="ascii") as f:
            f.write("\n".join(lines) + "\n")
        done = subprocess.run(
            [sys.executable, "-I", os.path.join(HERE, "mkmanual.py"), src,
             doc], capture_output=True, text=True)
        if done.returncode != 0:
            return ["the test document was not made: " + done.stderr[-300:]]
        done = subprocess.run(
            [strprint, doc, "-o", os.path.join(tmp, "check.pdf")],
            capture_output=True, text=True)
        faults = [ln for ln in done.stderr.split("\n")
                  if "error" in ln or "warning" in ln]
        if done.returncode != 0 and not faults:
            faults.append("the Printer ended with status %d"
                          % done.returncode)
    return faults


def check_pdf(path):
    """The layout of the PDF of the card; return the faults."""
    faults = []
    info = subprocess.run(["pdfinfo", path], capture_output=True, text=True)
    pages = int(re.search(r"Pages:\s+(\d+)", info.stdout).group(1))
    if pages != 2:
        faults.append("the card has %d pages, not 2" % pages)
    box = subprocess.run(["pdftotext", "-bbox", path, "-"],
                         capture_output=True, text=True).stdout
    # The pages are portrait pages that are shown sideways: the positions of
    # the words are those of the page as it is shown.
    size = re.search(r'<page width="([\d.]+)" height="([\d.]+)"', box)
    pw, ph = float(size.group(1)), float(size.group(2))
    rot = re.search(r"Page rot:\s+(\d+)", info.stdout)
    if rot is not None and int(rot.group(1)) in (90, 270):
        pw, ph = ph, pw
    margin = 5 / 25.4 * 72 - 1.5
    words = [(float(a), float(b), float(c), float(d)) for a, b, c, d in
             re.findall(r'<word xMin="([\d.]+)" yMin="([\d.]+)" '
                        r'xMax="([\d.]+)" yMax="([\d.]+)"', box)]
    if not words:
        return faults + ["the card has no text"]
    left = min(w[0] for w in words)
    right = max(w[2] for w in words)
    top = min(w[1] for w in words)
    bottom = max(w[3] for w in words)
    for side, value, limit, lo in (
            ("left", left, margin, True), ("right", pw - right, margin, True),
            ("top", top, margin, True), ("bottom", ph - bottom, margin, True)):
        if lo and value < limit:
            faults.append("text comes within %.1f mm of the %s edge of the "
                          "paper" % (value / 72 * 25.4, side))
    return faults


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    import mkmathcheat
    strprint = argv[1]
    groups = mkmathcheat.read_list(mkmathcheat.LIST, sizes=False)
    status = 0

    # 1. Every entry through the Printer.
    faults = typeset(strprint, groups)
    count = sum(len(g.entries) for g in groups)
    for ln in faults[:20]:
        print("FAIL", ln[:200])
    print("entries typeset: %d, faults: %d" % (count, len(faults)))
    status |= 1 if faults else 0

    # 2. What the packages define, against what is on the sheet.
    names = inventory()
    left = read_left_out(os.path.join(HERE, "mathsyms-left-out.txt"))
    listed = listed_names(groups)
    classes = {}
    missing = []
    for name, kind in sorted(names.items()):
        row = classes.setdefault(kind, [0, 0, 0])
        row[0] += 1
        if name in listed:
            row[1] += 1
        elif name in left:
            row[2] += 1
        else:
            missing.append(name)
    for kind in sorted(classes):
        row = classes[kind]
        print("%-12s %4d defined, %4d on the sheet, %4d left out"
              % (kind, row[0], row[1], row[2]))
    print("%-12s %4d defined, %4d on the sheet, %4d left out"
          % ("all", sum(r[0] for r in classes.values()),
             sum(r[1] for r in classes.values()),
             sum(r[2] for r in classes.values())))
    if missing:
        status = 1
        print("FAIL %d names are neither on the sheet nor left out: %s"
              % (len(missing), " ".join("\\" + n for n in missing[:60])))
    stale = [n for n in left if n in listed]
    if stale:
        status = 1
        print("FAIL left out but on the sheet: %s" % " ".join(stale))

    # 3. The layout of the printed card.
    if len(argv) > 2:
        faults = check_pdf(argv[2])
        for ln in faults:
            print("FAIL", ln)
        print("layout of %s: %d fault(s)" % (os.path.basename(argv[2]),
                                            len(faults)))
        status |= 1 if faults else 0
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv))
