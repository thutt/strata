#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The table of the label sheets of the manual, made from tests/labeldata.py:

    python3 -I -B tests/mklabelsman.py manual/src/labels-table.src

The file is read by manual/src/file3.src (.include), so that the manual cannot
disagree with the templates.  It has no comment line: the check of the manual
sources for implementation details would read the names of the files in it.  tests/labels.py compares the file with the text
that this program makes.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import labeldata as D
import mklabels as L

WIDTHS = (16, 10, 6, 8, 11, 4)
HEAD = ("Numbers", "Kind", "Paper", "Stickers", "Size in mm", "Lines")
KIND = {"address": "Address", "shipping": "Shipping",
        "return address": "Return address", "name badge": "Name badge",
        "ID": "ID", "file folder": "Filing", "round": "Round",
        "CD or DVD": "CD or DVD", "diskette": "Diskette",
        "mini address": "Mini address", "full page": "Full page"}


def mm1(v):
    return ("%.1f" % v).rstrip("0").rstrip(".") if v % 1 else "%d" % v


def size(s):
    """The size of a sticker in millimetres, for people to read."""
    if s["shape"] != "rect":
        return "diameter %s" % mm1(D.mm(s, "w"))

    return "%s x %s" % (mm1(D.mm(s, "w")), mm1(D.mm(s, "h")))


def wrap(text, width):
    """The text in lines of at most width characters, cut at spaces."""
    lines = []
    cur = ""

    for word in text.split(" "):
        if cur != "" and len(cur) + 1 + len(word) > width:
            lines.append(cur)
            cur = word
        else:
            cur = word if cur == "" else cur + " " + word

    return lines + [cur]


def rows():
    out = []

    for paper in ("us", "a4"):
        for s in D.SHEETS:
            if s["paper"] != paper:
                continue

            cells = [", ".join(D.numbers(s)), KIND[s["kind"]],
                     "Letter" if paper == "us" else "A4",
                     "%d x %d" % (s["nx"], s["ny"]), size(s),
                     "%d" % L.layout(s)["k"]]
            out.append(cells)

    return out


def text():
    lines = [".table " + " ".join(str(w) for w in WIDTHS),
             " | ".join(HEAD)]

    for cells in rows():
        # The Printer wraps the cells itself, to the widths of the table.
        parts = [wrap(cells[0], 22)] + [[c] for c in cells[1:]]

        for i in range(max(len(p) for p in parts)):
            row = " | ".join(p[i] if i < len(p) else "" for p in parts)

            # A line that begins with a space goes on with the row above.
            lines.append(("" if i == 0 else " ") + row.rstrip())

    lines.append(".end")

    return "\n".join(lines) + "\n"


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: mklabelsman.py FILE\n")
        return 2

    with open(argv[1], "w") as f:
        f.write(text())

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
