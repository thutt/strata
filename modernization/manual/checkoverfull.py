#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program reads the log of the LaTeX run that makes a document of the
# manual and fails when the log has an overfull box: a line, or the text of
# a cell of a table, that is wider than the space that it has, so that it
# runs past the border of its cell or past the edge of the text.  TeX is the
# only one that knows the width of the text, so the check reads what TeX
# says and does not estimate.  A box that is wider by no more than
# TOLERANCE points cannot be seen and is not reported.
#
#     python3 -I -B manual/checkoverfull.py LOG [LOG ...]
#     python3 -I -B manual/checkoverfull.py --self-test
#
# The program build.sh check-overfull makes the logs.

import re
import sys

TOLERANCE = 0.5
HEAD = re.compile(r"^Overfull \\hbox \(([0-9.]+)pt too wide\) in paragraph "
                  r"at lines (\d+)--(\d+)")


def overfull_boxes(text):
    """The (width in points, first line, kind, text) of every overfull box
    of a log that is wider than the tolerance.  The kind is cell when the
    box holds the border of a cell of a table."""
    lines = text.split("\n")
    boxes = []

    for i, ln in enumerate(lines):
        m = HEAD.match(ln)

        if m is None:
            continue

        width = float(m.group(1))

        if width <= TOLERANCE:
            continue

        body = " ".join(x.strip() for x in lines[i + 1:i + 3]).strip()
        kind = "cell" if "|" in body else "text"
        boxes.append((width, int(m.group(2)), kind, body[:70]))

    return boxes


def self_test():
    """The program reports an overfull cell and an overfull line, and it
    passes a log that has none and a box that is within the tolerance."""
    cell = ("Overfull \\hbox (5.4pt too wide) in paragraph at lines 7--7\n"
            "[]|\\T1/lmtt/m/n/10.95 print_queue|\n []\n")
    line = ("Overfull \\hbox (55.1pt too wide) in paragraph at lines 9--9\n"
            "[]\\T1/lmtt/m/n/10.95 ....\n []\n")
    fine = ("Overfull \\hbox (0.2pt too wide) in paragraph at lines 3--3\n"
            "[]|text|\n []\n")
    ok = True

    if [b[2] for b in overfull_boxes(cell)] != ["cell"]:
        print("FAIL an overfull cell is not reported")
        ok = False

    if [b[2] for b in overfull_boxes(line)] != ["text"]:
        print("FAIL an overfull line is not reported")
        ok = False

    if overfull_boxes(fine) != [] or overfull_boxes("") != []:
        print("FAIL a box within the tolerance, or no box, is reported")
        ok = False

    print("checkoverfull: self-test %s" % ("passed" if ok else "FAILED"))

    return 0 if ok else 1


def main(argv):
    if "--self-test" in argv:
        return self_test()

    names = [a for a in argv[1:] if not a.startswith("--")]

    if names == []:
        sys.stderr.write("usage: checkoverfull.py LOG [LOG ...]\n")
        return 2

    count = 0

    for name in names:
        with open(name, encoding="latin-1") as f:
            boxes = overfull_boxes(f.read())

        for width, first, kind, body in boxes:
            count += 1
            print("%s: line %d of the LaTeX file: a %s is %.1f points too "
                  "wide: %s" % (name, first, kind, width, body))

    print("checkoverfull: %d overfull box(es) in %d log(s)"
          % (count, len(names)))

    return 1 if count > 0 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
