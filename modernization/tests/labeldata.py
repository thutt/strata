#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The data of the label sheets: the one place where the numbers live.

docs/LABELS.txt tells where every number was found (the source is marked
with a letter), tests/mklabels.py makes the templates from this table, the
manual table is made from it, and tests/labels.py compares all of them.

A sheet is a dict:

  num     the product number that names the template (the name of the file
          is label-<paper>-<num>, lower case, a dot written as a dash)
  also    the other numbers of the same sheet that the source shows (each
          gets a template of its own with the same layout)
  kind    what the sheet is for
  shape   rect, round or cd (the position of a round sticker is the box
          that holds it: the width and the height are the diameter)
  w, h    the size of one sticker (for a round one, r is the radius)
  nx, ny  the number of stickers across and down
  x0, y0  the margins: the left edge of the first sticker, the top edge
  dx, dy  the pitch: from the left edge of a sticker to the left edge of the
          next one, and from the top edge to the top edge of the next row
  unit    in, pt or mm: the unit of all the lengths of the sheet, the unit
          of the source
  src     U, I: the file of the source (see docs/LABELS.txt)
  kb      the pitch of the second source (K) in inches, where it has the
          sheet, to be compared; None otherwise
  hole    the radius of the hole of a disc, in the unit (a disc only)
"""

# The paper: width and height in thousandths of an inch (the record DP), and
# the paper code that the document format gives it.
PAPER = {"us": (0, 8500, 11000, "US Letter"),
         "a4": (8, 8268, 11693, "A4")}

# The sources.  The number is the date the page was read: 2026-11-21.
SOURCES = {
    "U": "https://raw.githubusercontent.com/jimevins/glabels-qt/master/"
         "templates/avery-us-templates.xml",
    "I": "https://raw.githubusercontent.com/jimevins/glabels-qt/master/"
         "templates/avery-iso-templates.xml",
    "K": "https://techshelps.github.io/MSDN/KB/Source/word/q146892.htm",
}


def S(num, kind, w, h, nx, ny, x0, y0, dx, dy, unit="in", src="U", also=(),
      shape="rect", kb=None, hole=None, paper=None):
    """One sheet; the paper follows from the source unless it is given."""
    return {"num": num, "kind": kind, "w": w, "h": h, "nx": nx, "ny": ny,
            "x0": x0, "y0": y0, "dx": dx, "dy": dy, "unit": unit,
            "src": src, "also": list(also), "shape": shape, "kb": kb,
            "hole": hole, "paper": paper or ("us" if src == "U" else "a4")}


def R(num, kind, r, nx, ny, x0, y0, dx, dy, unit="in", src="U", also=(),
      shape="round", kb=None, hole=None):
    """A sheet of round stickers of the radius r."""
    s = S(num, kind, 2 * r, 2 * r, nx, ny, x0, y0, dx, dy, unit, src, also,
          shape, kb, hole)
    s["r"] = r
    return s


# US Letter, 8.5 x 11 in, the file avery-us-templates.xml (source U).  The
# tenths of a thousandth are the exact thirds of an inch of the source.
THIRD = 4.0 / 3.0
SHEETS = [
    S("5160", "address", 2.625, 1, 3, 10, .15625, .5, 2.78125, 1,
      also=("5260", "8160"), kb=(2.75, 1.0)),
    S("5161", "address", 4, 1, 2, 10, .15625, .5, 4.1875, 1,
      also=("5261", "8161"), kb=(4.188, 1.0)),
    S("5162", "address", 4, THIRD, 2, 7, .15625, 5.0 / 6.0, 4.1875, THIRD,
      also=("5262", "8162", "5962"), kb=(4.188, 1.333)),
    S("5159", "address", 4, 1.5, 2, 7, .15625, .25, 4.1875, 1.5),
    S("5197", "address", 4, 1.5, 2, 6, .1875, 1, 4.125, 1.5,
      kb=(4.188, 1.5)),
    S("5163", "shipping", 4, 2, 2, 5, .1625, .5, 4.1875, 2,
      also=("5263", "8163", "8463", "8663"), kb=(4.188, 2.0)),
    S("5164", "shipping", 4, 10.0 / 3.0, 2, 3, .15625, .5, 4.1875,
      10.0 / 3.0, also=("5264", "8164"), kb=(4.188, 3.333)),
    S("5168", "shipping", 3.5, 5, 2, 2, .5, .5, 4, 5),
    S("6873", "shipping", 3.75, 2, 2, 4, .375, 1.125, 4, 2.25),
    S("6874", "shipping", 3.75, 3, 2, 3, .375, .625, 4, 3.375),
    S("6573", "shipping", 8.12992, 5, 1, 2, .18504, .5, 0, 5),
    S("5126", "shipping", 8.5, 5.5, 1, 2, 0, 0, 0, 5.5),
    S("5165", "full page", 8.5, 11, 1, 1, 0, 0, 0, 0,
      also=("8165", "5265"), kb=(0.0, 0.0)),
    S("5167", "return address", 1.75, .5, 4, 20, .28125, .5, 2.0625, .5,
      also=("5267", "8167"), kb=(2.063, 0.5)),
    S("8195", "return address", 1.75, .666, 4, 15, .297, .531, 2.031,
      .666),
    S("5366", "file folder", 3.4375, 2.0 / 3.0, 2, 15, .53125, .5, 4,
      2.0 / 3.0, also=("5066", "5166", "5266", "8066", "8166", "8366"),
      kb=(4.0, 0.667)),
    S("5026", "file folder", 3.4375, .9375, 2, 9, .49755, .51125, 4.0674,
      1.13),
    R("5195", "round", 1.65625, 2, 3, .6406, .4844, 3.9063, 3.3594,
      also=("5295",), kb=(3.937, 3.333)),
    R("5193", "round", .8125, 4, 6, .4157, .549, 2.0145, 1.6554,
      also=("5293",), kb=(2.0, 1.667)),
    R("5194", "round", 1.25, 3, 4, .21, .425, 2.75, 2.515,
      also=("5294",), kb=(2.75, 2.5)),
    R("3274.2", "round", .75, 4, 5, .5, .75, 2, 2, also=("3112", "8293")),
    R("3274.3", "round", 1.25, 3, 3, .3125, 1.25, 2.6875, 3),
    S("5395", "name badge", 3.375, 7.0 / 3.0, 2, 4, .6875, 7.0 / 12.0,
      3.75, 2.5, also=("5095", "5895", "8395"), kb=(3.75, 2.5)),
    S("5196", "diskette", 2.75, 2.75, 3, 3, .125, .5, 2.75, 3,
      kb=(2.75, 3.0)),
    R("5931", "CD or DVD", 2.3125, 1, 2, 1.9375, .6875, 0, 5, shape="cd",
      hole=.8125),
    R("5824", "CD or DVD", 166.5, 1, 2, 144, 36, 351, 396, unit="pt",
      shape="cd"),
    R("8960", "CD or DVD", 166.5, 1, 2, 138.898, 50.1732, 351.142, 360,
      unit="pt", shape="cd"),
    # A4, 210 x 297 mm, the file avery-iso-templates.xml (source I).  Where
    # the file gives no unit the numbers are points.
    S("L7160", "address", 181.4, 108.0, 3, 7, 21.2, 43.9, 187.2, 108.0,
      unit="pt", src="I", also=("J8160",)),
    S("L7161", "address", 180.2, 132.6, 3, 6, 21, 23, 186.9, 132.5,
      unit="pt", src="I", also=("J8161",)),
    S("L7162", "address", 280.9, 96.1, 2, 8, 11.3, 36.8, 290.5, 96.1,
      unit="pt", src="I"),
    S("L7163", "address", 280.9, 108, 2, 7, 9.5, 43, 292, 108, unit="pt",
      src="I"),
    S("L7164", "address", 180, 204.038, 3, 4, 20.84, 10, 187.08, 204.038,
      unit="pt", src="I", also=("J8164",)),
    S("L7165", "address", 280.8, 191.991, 2, 4, 13.2378, 36.9638, 288.113,
      191.991, unit="pt", src="I", also=("J8165",)),
    S("L7169", "shipping", 280.9, 394, 2, 2, 14.2, 20, 287.7, 394,
      unit="pt", src="I"),
    S("L4785", "name badge", 80, 50, 2, 5, 18, 13, 95, 55, unit="mm",
      src="I", also=("L4784", "L4786", "L4787")),
    S("L4732", "mini address", 35.6, 16.9, 5, 16, 11, 13, 38.1, 16.9,
      unit="mm", src="I"),
    S("L4770", "mini address", 45.7, 25.4, 4, 10, 10, 22, 48.5, 25.4,
      unit="mm", src="I", also=("J8654", "L6140", "L6145", "L7654")),
    S("J8157", "address", 64, 24.3, 3, 11, 6, 14, 66.5, 24.3, unit="mm",
      src="I"),
    S("8651", "mini address", 108, 60.09, 5, 13, 13.32, 30.90, 115.09,
      60.09, unit="pt", src="I",
      also=("L8651", "J8651", "J8551", "L7651", "L7551")),
    S("8658", "ID", 25.4, 10, 7, 27, 5, 13, 28.5, 10.14, unit="mm",
      src="I", also=("J8658", "L8658")),
    S("L7664", "diskette", 70, 71.9, 2, 4, 17, 5, 104.5, 72, unit="mm",
      src="I"),
    S("8666", "diskette", 198.4, 147.4, 2, 5, 66, 53, 264, 147, unit="pt",
      src="I", also=("J8666",)),
    R("L6043", "CD or DVD", 58.7, 1, 2, 46, 21.8, 123.75, 137, unit="mm",
      src="I", shape="cd", hole=20.6),
    R("L7676", "CD or DVD", 165.827, 1, 2, 131.811, 62.3622, 342.992,
      388.346, unit="pt", src="I", shape="cd", hole=24.9449),
    R("8676", "CD or DVD", 166.5, 1, 2, 130, 59.5, 0, 388, unit="pt",
      src="I", also=("J8676",), shape="cd", hole=17),
    R("L6015", "CD or DVD", 165.827, 1, 2, 131.811, 60.6614, 349.795, 392.4,
      unit="pt", src="I", shape="cd", hole=58.1102),
    S("38931", "address", 70, 25, 3, 11, 0, 10, 70, 25, unit="mm", src="I",
      also=("DL33", "CR33")),
    S("18036", "address", 198, 91, 3, 9, 0, 13, 198, 91, unit="pt",
      src="I"),
]

# Sheets of the sources that have no template, with the reason.
SKIPPED = {
    "L4730": "ten stickers across: a block winds in nine columns at most",
}

# Numbers of the request and of Avery's lists that no source of the three
# gave (docs/LABELS.txt, section Missing).
MISSING = ["5390", "5392", "L7159", "L7106", "L7418", "6240", "5931 spine"]

# The text sizes (points) and the pitch of their lines in points, as the
# printer sets them (measured in a PDF; tests/labels.py measures again),
# and the distance from the top of a line to the top of the glyph box that
# pdftotext reports, in points.
LINE_PT = {8: 9.465, 9: 10.909, 10: 11.955, 11: 13.549, 12: 14.446}
GLYPH_TOP_PT = {8: 3.4746, 9: 2.7882, 10: 3.0988, 11: 3.4426, 12: 3.7179}

UNIT_MM = {"in": 25.4, "pt": 25.4 / 72.0, "mm": 1.0}


def mm(sheet, key):
    """A length of the sheet in millimetres."""
    return sheet[key] * UNIT_MM[sheet["unit"]]


def numbers(sheet):
    """Every product number of the sheet: its own, then the others."""
    return [sheet["num"]] + sheet["also"]


def key(num, sheet):
    """The name of a template (the file without the extension)."""
    return "label-%s-%s" % (sheet["paper"], num.lower().replace(".", "-"))


def count(sheet):
    return sheet["nx"] * sheet["ny"]


def every():
    """All templates: (name, number, sheet), in the order of the names."""
    out = []

    for s in SHEETS:
        for n in numbers(s):
            out.append((key(n, s), n, s))

    return sorted(out)


def describe(num, sheet):
    """The description of a template: number, kind, count and paper."""
    kind = sheet["kind"]
    word = {"address": "address labels", "shipping": "shipping labels",
            "return address": "return address labels",
            "name badge": "name badge labels", "ID": "ID labels",
            "file folder": "file folder labels", "round": "round labels",
            "CD or DVD": "CD or DVD labels", "diskette": "diskette labels",
            "mini address": "mini address labels",
            "full page": "full page labels"}[kind]
    n = count(sheet)

    return "Avery %s %s, %d per sheet, %s" % (
        num, word, n, PAPER[sheet["paper"]][3])
