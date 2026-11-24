#!/usr/bin/env python3
# Copyright (c) 2026 Logic Magicians Software
#
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This module makes the panels of the Math Symbols card, the tri-fold sheet
# of the TeX math symbols, from the list of symbols in manual/mathsyms.txt.
# The list is the one place that holds the symbols; the format of the list is
# described at its head.
#
# The card is made by the generator of the manual: the source of the card,
# manual/src/mathcheat.src, has the directive ".mathsyms mathsyms.txt" and
# the generator calls this module for the panels.  Run by itself, this
# program does two things:
#
#     python3 -I manual/mkmathcheat.py            the room of every panel
#     python3 -I manual/mkmathcheat.py --report   and of every group
#     python3 -I manual/mkmathcheat.py --measure  measures every symbol with
#                                                 LaTeX and writes the sizes
#                                                 to manual/mathsyms.sizes
#
# The sheet is landscape paper (letter), printed on both sides: three panels
# of equal width on each side.  Every symbol is a real equation marker, so
# that the sheet shows what the Printer makes of it, and the text to type is
# in the program font beside it.  The entries are packed into lines by their
# measured sizes, so that no symbol runs into the text beside it.

import hashlib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LIST = os.path.join(HERE, "mathsyms.txt")
SIZES = os.path.join(HERE, "mathsyms.sizes")

# The room that a line of entries has, in points.
LINE_WIDTH = 227.0

# The width of one character of the text to type (the program font at the
# size of the sheet), and the space left between two entries.
CHAR_WIDTH = 4.4
GAP = 5.0

# The most entries in a line.
MOST_PER_LINE = 4

# The distance between two lines of the panels, and the space above a
# heading, in points.
BASELINE = 9.0
HEADING_SPACE = 3.0

# The height of a panel in points: on the first side the headline of the
# card takes some of the page.
PANEL_HEIGHT = [555.0, 585.0]

# What the cover takes (the title, the text under it and the how-to line).
COVER_HEIGHT = 105.0


class Entry:
    """One symbol or construct of the sheet."""

    def __init__(self, tex, typed, note):
        self.tex = tex
        self.typed = typed
        self.note = note
        self.width = 0.0
        self.height = 0.0

    def need(self):
        """The room of the entry in points: the symbol, the text to type
        and the note."""
        note = CHAR_WIDTH * (len(self.note) + 2) if self.note else 0.0
        return self.width + GAP + CHAR_WIDTH * len(self.typed) + note


class Group:
    """A heading and its entries."""

    def __init__(self, title, panel, again):
        self.title = title
        self.panel = panel
        self.again = again
        self.entries = []


def read_list(path, sizes=True):
    """The groups of the list, in order.  The sizes of the symbols are read
    from the file of sizes unless sizes is false."""
    groups = []
    panel = 1
    with open(path, encoding="ascii") as f:
        raw = f.read().split("\n")
    # A line that begins with a space goes on from the line before it.
    lines = []
    for line in raw:
        if line.startswith(" ") and lines and line.strip() != "":
            lines[-1] += " " + line.strip()
        else:
            lines.append(line)
    for number, line in enumerate(lines, 1):
        line = line.rstrip("\n")
        if line.strip() == "" or line.startswith("#"):
            continue
        if line.startswith("@panel "):
            panel = int(line[7:])
            continue
        if line.startswith("@group "):
            groups.append(Group(line[7:].strip(), panel, False))
            continue
        if line.strip() == "@again":
            groups[-1].again = True
            continue
        if not groups:
            raise SystemExit("%s:%d: an entry before the first group"
                             % (path, number))
        if line.startswith("> "):
            fields = [x.strip() for x in line[2:].split(" | ")]
            tex = fields[0]
            typed = tex
            note = ""
            if len(fields) == 2:
                # The text to type has a backslash; a note does not.
                if "\\" in fields[1]:
                    typed = fields[1]
                else:
                    note = fields[1]
            elif len(fields) == 3:
                typed, note = fields[1], fields[2]
            groups[-1].entries.append(Entry(tex, typed, note))
            continue
        for word in line.split():
            tex = "|" if word == "@bar" else word
            groups[-1].entries.append(Entry(tex, tex, ""))
    if sizes:
        known = read_sizes()
        for g in groups:
            for e in g.entries:
                if key(e.tex) not in known:
                    raise SystemExit("mkmathcheat: no size for %s: run "
                                     "mkmathcheat.py --measure" % e.tex)
                e.width, e.height = known[key(e.tex)]
    return groups


def key(tex):
    """The key of a symbol in the file of sizes: a hash of its text, so that
    the lines of the file are short."""
    return hashlib.sha1(tex.encode("ascii")).hexdigest()[:12]


def read_sizes():
    """The measured sizes: the key of the text of the equation to (width,
    height and depth together) in points."""
    known = {}
    if not os.path.exists(SIZES):
        return known
    with open(SIZES, encoding="ascii") as f:
        for line in f:
            line = line.rstrip("\n")
            if line == "" or line.startswith("#"):
                continue
            fields = line.split()
            known[fields[0]] = (float(fields[1]), float(fields[2]))
    return known


def measure(groups):
    """Measure every symbol with LaTeX and write the file of sizes."""
    texts = sorted({e.tex for g in groups for e in g.entries})
    body = []
    for i, tex in enumerate(texts):
        body.append("\\setbox0\\hbox{\\scriptsize $%s$}"
                    "\\immediate\\write\\sizes{%d|\\the\\wd0|\\the\\ht0|"
                    "\\the\\dp0}\n" % (tex, i))
    doc = ("\\documentclass[11pt]{article}\n"
           "\\usepackage[T1]{fontenc}\\usepackage{lmodern}\n"
           "\\usepackage{amsmath,amssymb}\n"
           "\\newwrite\\sizes\\immediate\\openout\\sizes=sizes.txt\n"
           "\\begin{document}\n" + "".join(body) +
           "\\immediate\\closeout\\sizes\n\\end{document}\n")
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "m.tex"), "w", encoding="ascii") as f:
            f.write(doc)
        done = subprocess.run(
            ["pdflatex", "-interaction=nonstopmode", "-halt-on-error",
             "m.tex"], cwd=tmp, capture_output=True, text=True)
        path = os.path.join(tmp, "sizes.txt")
        if not os.path.exists(path):
            raise SystemExit("mkmathcheat: LaTeX made no sizes:\n"
                             + done.stdout[-1500:])
        with open(path, encoding="ascii") as f:
            lines = f.read().split("\n")
    out = ["# The sizes in points of the symbols of mathsyms.txt: a key of "
           "the text of the",
           "# equation, the width, the height and depth together, and the "
           "start of the text.",
           "# Made by mkmathcheat.py --measure."]
    got = 0
    for line in lines:
        if line.strip() == "":
            continue
        n, wd, ht, dp = line.split("|")
        tex = texts[int(n)]
        out.append(("%s %6.1f %6.1f  %s" % (
            key(tex), float(wd[:-2]), float(ht[:-2]) + float(dp[:-2]),
            tex))[:78])
        got += 1
    if got != len(texts):
        raise SystemExit("mkmathcheat: LaTeX measured %d of %d symbols"
                         % (got, len(texts)))
    with open(SIZES, "w", encoding="ascii") as f:
        f.write("\n".join(out) + "\n")
    print("mkmathcheat: measured %d symbols" % got)


def esc(text):
    """Text for a command of the generator, where braces are doubled."""
    return text.replace("{", "{{").replace("}", "}}")


def latex(text):
    """A LaTeX marker."""
    return "{latex:" + esc(text) + "}"


def equation(text):
    """An equation marker."""
    return "{eq:" + esc(text) + "}"


def program(text):
    """Text in the program font."""
    return "{c:" + esc(text) + "}"


def entry_text(e, box, symbol):
    """The text of one entry: the symbol in a box of fixed width, and beside
    it the text to type and the note, all in a box of fixed width."""
    out = latex(r"\makebox[%.1fpt][l]{\makebox[%.1fpt][l]{" % (box, symbol))
    out += equation(e.tex) + latex("}") + program(e.typed)
    if e.note != "":
        out += "{i:  " + esc(e.note) + "}"
    return out + latex("}")


def pack(entries, start):
    """How many entries, from start, stand in the next line: the most that
    fit, each in an equal share of the line."""
    for k in range(MOST_PER_LINE, 1, -1):
        row = entries[start:start + k]
        if len(row) == k and all(x.need() <= LINE_WIDTH / k for x in row):
            return k
    return 1


SMALL = r"\scriptsize\setlength{\baselineskip}{%.1fpt}" % BASELINE


def panel_lines(groups):
    """The lines of one panel and the height that they need, in points."""
    lines = []
    height = 0.0
    first = True
    for g in groups:
        head = "{b:" + esc(g.title) + "}"
        if first:
            lines.append("~ " + latex(SMALL) + head)
            height += BASELINE
        else:
            lines.append("~ " + latex(r"\vspace{%.1fpt}" % HEADING_SPACE) +
                         head)
            height += BASELINE + HEADING_SPACE
        first = False
        entries = g.entries
        for e in entries:
            if e.need() > LINE_WIDTH:
                raise SystemExit("mkmathcheat: the entry %s needs %.0f points "
                                 "and a line has %.0f: shorten the text to "
                                 "type or the note" % (e.tex, e.need(),
                                                       LINE_WIDTH))
        i = 0
        while i < len(entries):
            k = pack(entries, i)
            row = entries[i:i + k]
            box = LINE_WIDTH / k
            symbol = max(x.width for x in row) + 2.0
            lines.append("~ " + "".join(entry_text(x, box, symbol)
                                        for x in row))
            height += max(BASELINE, max(x.height for x in row) + 1.5)
            i += k
    return lines, height


COVER = [
    "~ " + latex(r"\Large\strata"),
    "~ " + latex(r"\large") + "{b:MATH SYMBOLS}",
    "~ " + latex(SMALL) + "Every symbol and construct of TeX math "
    "that works in an equation of Strata.  Beside each symbol is the text "
    "to type; the symbol is shown as the Printer sets it.",
    "~ " + latex(r"\vspace{3pt}") + "{b:TO PUT AN EQUATION IN A DOCUMENT}",
    "~ Press {k:Alt-F12} (or {k:Ctrl-F12}), choose Equation, type the text "
    "and press {k:Ins}.  The chapter Equations of the Reference Manual says "
    "more.",
]


def pages(groups):
    """The panels of the two sides of the sheet (a list of two lists of
    three lists of lines), the height that every panel needs and the height
    that it has, in points."""
    result = []
    rooms = []
    for page in range(2):
        panels = []
        for k in range(3):
            n = page * 3 + k + 1
            mine = [g for g in groups if g.panel == n]
            lines, height = panel_lines(mine)
            if n == 3:
                # The cover: the title first, the how-to line at the foot.
                lines = COVER[:3] + [
                    ln.replace(latex(SMALL), "", 1) for ln in lines]
                lines += COVER[3:]
                height += COVER_HEIGHT
            panels.append(lines)
            rooms.append((height, PANEL_HEIGHT[page]))
        result.append(panels)
    return result, rooms


def main(argv):
    """With --report the entries of every panel and the room that they
    need are printed; the exit status is 1 when a panel has too much."""
    if "--measure" in argv:
        measure(read_list(LIST, sizes=False))
        return 0
    groups = read_list(LIST)
    result, rooms = pages(groups)
    for i, (need, have) in enumerate(rooms, 1):
        n = sum(len(g.entries) for g in groups if g.panel == i)
        print("panel %d: %d entries, %.0f of %.0f points high"
              % (i, n, need, have))
        if "--report" in argv:
            for g in groups:
                if g.panel == i:
                    print("    %5.0f  %3d  %s" % (panel_lines([g])[1],
                                                  len(g.entries), g.title))
    over = [i + 1 for i, (need, have) in enumerate(rooms) if need > have]
    if over:
        print("mkmathcheat: the panels %s need more room than they have; "
              "move groups to another panel" % over)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
