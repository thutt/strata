#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The table of the sheets in docs/LABELS.txt, made from tests/labeldata.py
(the text between the lines BEGIN TABLE and END TABLE):

    python3 -I -B tests/mklabelsdoc.py docs/LABELS.txt     (writes it)
    python3 -I -B tests/mklabelsdoc.py --check docs/LABELS.txt

tests/labels.py makes the same text and compares it with the document.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import labeldata as D

BEGIN = "BEGIN TABLE (made by tests/mklabelsdoc.py from tests/labeldata.py)"
END = "END TABLE"


def num(v):
    """A number as the source gave it: no zeros at the end."""
    t = ("%.6f" % v).rstrip("0").rstrip(".")

    return t if t != "" else "0"


def sheet_text(s):
    """The lines of one sheet."""
    u = s["unit"]
    cd = "  hole=%s" % num(s["hole"]) if s["hole"] is not None else ""
    out = []
    head = "%s  %s, %s, %d per sheet" % (
        s["num"], s["kind"], D.PAPER[s["paper"]][3], D.count(s))

    if s["also"]:
        head += "; also " + " ".join(s["also"])

    out.append(head)

    if "r" in s:
        size = "r=%s" % num(s["r"])
    else:
        size = "w=%s h=%s" % (num(s["w"]), num(s["h"]))

    out.append("    size: %s across=%d down=%d%s shape=%s" % (
        size, s["nx"], s["ny"], cd, s["shape"]))
    out.append("    place: x0=%s y0=%s dx=%s dy=%s unit=%s src=%s" % (
        num(s["x0"]), num(s["y0"]), num(s["dx"]), num(s["dy"]), u,
        s["src"]))

    if s["kb"] is not None:
        kx, ky = s["kb"]
        ex = abs(kx * 25.4 - D.mm(s, "dx"))
        ey = abs(ky * 25.4 - D.mm(s, "dy"))
        out.append("    check: pitch %s x %s in (K)" % (num(kx), num(ky)))

        if max(ex, ey) > 0.5:
            out.append("    CONFLICT: K differs by %.2f mm across, %.2f mm "
                       "down; the pitch of %s is used" % (ex, ey, s["src"]))

    return out


def table_text():
    lines = [BEGIN, ""]

    for paper in ("us", "a4"):
        lines.append("%s sheets" % D.PAPER[paper][3])
        lines.append("")

        for s in D.SHEETS:
            if s["paper"] == paper:
                lines += sheet_text(s)
                lines.append("")

    lines.append("Sheets of the sources with no template")
    lines.append("")

    for n, why in sorted(D.SKIPPED.items()):
        lines.append("%s  %s" % (n, why))

    lines.append("")
    lines.append("Not found in any source (nothing is used)")
    lines.append("")
    lines.append("  " + " ".join(D.MISSING))
    lines += ["", END]

    return "\n".join(lines) + "\n"


def splice(doc):
    """The document with its table replaced by the made one."""
    i = doc.index(BEGIN)
    j = doc.index(END) + len(END) + 1

    return doc[:i] + table_text() + doc[j:]


def main(argv):
    check = "--check" in argv
    args = [a for a in argv[1:] if a != "--check"]

    if len(args) != 1:
        sys.stderr.write("usage: mklabelsdoc.py [--check] LABELS.txt\n")
        return 2

    doc = open(args[0]).read()
    new = splice(doc)

    if check:
        return 0 if new == doc else 1

    open(args[0], "w").write(new)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
