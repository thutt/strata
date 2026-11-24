#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The templates of the label sheets (docs/LABELS.txt), made from the table
tests/labeldata.py:

    python3 -I -B tests/mklabels.py FOLDER           (the templates)
    python3 -I -B tests/mklabels.py --probe FOLDER   (one probe in every
                                                      sticker, for the tests)

Every template is a blank document: no text.  Its layout puts one block in
each sticker position.  The blocks wind in as many columns as the sheet has
stickers across (the columns of the Printer are of equal width, and their
gutter is given in thousandths of an inch, so they can be set at the pitch of
the stickers); the text of a block is as many empty lines as fit in the
sticker, and the space after the block makes up the rest of the pitch.  The
stickers are therefore filled down the first column, then down the second.
The margins of the page are the margins of the sheet plus the distance of
the text from the edge of its sticker.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import labeldata as D
import mktemplates as M

# The distance of the text from the edge of its sticker, in millimetres.
PAD_X = 2.0
PAD_X_SMALL = 1.0
PAD_TOP = 1.5
PAD_TOP_SMALL = 0.5
# The text size by the height of the sticker in millimetres.
SIZE_OF = ((38.0, 11), (20.0, 10), (14.0, 9), (0.0, 8))
IN = 25.4


def pads(sheet):
    """The distance of the text from the left and the top edge of the
    sticker in millimetres.  A round sticker has its text in the square that
    fits in the circle."""
    w = D.mm(sheet, "w")
    h = D.mm(sheet, "h")

    if sheet["shape"] != "rect":
        return w * (1 - 0.7071) / 2, h * (1 - 0.7071) / 2

    return (PAD_X if w >= 30 else PAD_X_SMALL,
            PAD_TOP if h >= 20 else PAD_TOP_SMALL)


def points(sheet):
    h = D.mm(sheet, "h")

    for limit, size in SIZE_OF:
        if h >= limit:
            return size

    return SIZE_OF[-1][1]


def thousandths(v):
    """Millimetres as thousandths of an inch, rounded."""
    return int(round(v / IN * 1000))


def layout(sheet):
    """The numbers that the template of a sheet needs: the size of the text,
    the number of lines in a sticker, the margins of the page, the gutter and
    the space after each block (in the order down a column)."""
    ps = points(sheet)
    line_mm = D.LINE_PT[ps] * IN / 72.0
    px, pt = pads(sheet)
    w = D.mm(sheet, "w")
    h = D.mm(sheet, "h")
    dx = D.mm(sheet, "dx") if sheet["nx"] > 1 else 0.0
    dy = D.mm(sheet, "dy") if sheet["ny"] > 1 else h
    code, pw, ph, name = D.PAPER[sheet["paper"]]
    # The width of the text of one sticker and the gutter between two.
    tw = w - 2 * px
    gut = dx - w + 2 * px
    k = int((h - 2 * pt) / line_mm + 1e-9)
    left = thousandths(D.mm(sheet, "x0") + px)
    top = thousandths(D.mm(sheet, "y0") + pt)
    # The text has the width of the stickers across and no more: the right
    # margin is what is left.
    text_w = sheet["nx"] * tw + (sheet["nx"] - 1) * gut
    right = pw - left - thousandths(text_w)
    # The bottom of the text is the bottom of the last row of stickers.
    bottom = ph - thousandths(D.mm(sheet, "y0") + sheet["ny"] * dy)
    # The space after a block: what is left of the pitch after its lines, in
    # hundredths of a line (the record BSB: a hundred is one line, which is
    # no space).  The sum is rounded, not each space, so that the rows do
    # not drift.
    gap = (dy - k * line_mm) / line_mm * 100.0
    spaces = []

    for j in range(sheet["ny"]):
        spaces.append(100 + int(round((j + 1) * gap)) - int(round(j * gap)))

    return {"ps": ps, "k": k, "line_mm": line_mm, "left": left, "top": top,
            "right": right, "bottom": bottom, "gutter": thousandths(gut),
            "spaces": spaces, "pad_x": px, "pad_top": pt, "tw": tw}


def template(num, sheet, probe=False):
    """The document of a template.  With PROBE every sticker has a probe in
    its first line: its number."""
    code, pw, ph, name = D.PAPER[sheet["paper"]]
    g = layout(sheet)
    d = M.Doc(D.describe(num, sheet), code, pw, ph,
              (g["left"], g["right"], g["top"], g["bottom"]), {},
              points=g["ps"])
    nx = sheet["nx"]
    ny = sheet["ny"]
    # The page number must not print: on a sheet that has no margin it would
    # print on a sticker.  The Printer adds the number only when the document
    # has no header or footer that has text, so the header is one space.
    d.hf = [b"PHB", M.hfblock(b" ")]

    for c in range(nx):
        for j in range(ny):
            n = c * ny + j
            recs = [b"BWN%d" % nx, b"BWG%d" % max(g["gutter"], 0),
                    b"BSB%d" % g["spaces"][j]]
            lines = [b""] * g["k"]

            if probe:
                lines[0] = b"P%d" % n

            d.block(None, M.text(*lines), recs)

    return d


def build(probe=False):
    """The documents of all label templates, by name."""
    return {name: template(num, sheet, probe)
            for name, num, sheet in D.every()}


def main(argv):
    probe = "--probe" in argv
    args = [a for a in argv[1:] if a != "--probe"]

    if len(args) != 1:
        sys.stderr.write("usage: mklabels.py [--probe] FOLDER\n")
        return 2

    os.makedirs(args[0], exist_ok=True)

    for name, d in build(probe).items():
        folder = os.path.join(args[0], M.folder_of(name))

        os.makedirs(folder, exist_ok=True)

        with open(os.path.join(folder, name + ".doc"), "wb") as f:
            f.write(d.bytes())

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
