#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# Strata is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along
# with Strata.  If not, see <https://www.gnu.org/licenses/>.
# Copyright (c) 2026 Logic Magicians Software

"""The charts of the sample document "How Strata was made", drawn in colour
from the numbers of tests/storydata.py (the measurements), with the standard library only (zlib and struct write the
PNG files).  Every picture is made from DATA below, so two runs make the same
bytes.  The test tests/story.py reads the documents of the measurements
again and compares every number with storydata.py and with the tables of
the sample.

    python3 -I -B tests/mkstorypics.py [OUTPUT_FOLDER]

The default folder is samples/story/pictures.  The labels are drawn with a
small font of 5 by 7 dots that is part of this file, in capital letters.
"""
import os
import struct
import sys
import zlib

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))

# The numbers come from tests/storydata.py, the one file that holds them for
# the text of the sample, the charts and the test.
sys.path.insert(0, HERE)
import storydata as sd

PAGES = sd.PAGES

# name: (where the numbers come from, series of numbers).  The series of the
# memory and the time tables hold one value for each size of PAGES.
DATA = {
    "memory-save": (
        "the measurements of the memory of the terminal Editor, the column "
        "save, before / after (megabytes)",
        {"before": [row[3][0] for row in sd.MEMORY],
         "after": [row[3][1] for row in sd.MEMORY]}),
    "print-time": (
        "the measurements of prints that need several passes, seconds for "
        "documents of %s pages" % ", ".join(str(p) for p in sd.PASS_PAGES),
        {"pages": list(sd.PASS_PAGES),
         "seconds": list(sd.PASS_SECONDS),
         "passes": list(sd.PASSES)}),
}

WHITE = (255, 255, 255)
BLACK = (30, 30, 30)
GREY = (200, 200, 200)
BLUE = (31, 95, 168)
ORANGE = (230, 126, 34)
TEAL = (22, 160, 133)
RED = (192, 57, 43)

# The font: five columns and seven rows for a letter, digit or sign.  Each
# glyph is seven strings of five characters; # is a dot that is drawn.
GLYPHS = {
    "A": ["..#..", ".#.#.", "#...#", "#####", "#...#", "#...#", "#...#"],
    "B": ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
    "C": [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
    "D": ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "G": [".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."],
    "H": ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "I": [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "J": ["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."],
    "K": ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "M": ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
    "N": ["#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    "Q": [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "V": ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
    "W": ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"],
    "X": ["#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"],
    "Y": ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
    "Z": ["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
    "0": [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."],
    "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    "3": ["####.", "....#", "....#", ".###.", "....#", "....#", "####."],
    "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
    "5": ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
    "6": [".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."],
    "7": ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
    "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
    "9": [".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."],
    " ": [".....", ".....", ".....", ".....", ".....", ".....", "....."],
    ".": [".....", ".....", ".....", ".....", ".....", ".##..", ".##.."],
    ",": [".....", ".....", ".....", ".....", ".##..", "..#..", ".#..."],
    "-": [".....", ".....", ".....", "#####", ".....", ".....", "....."],
    "/": ["....#", "....#", "...#.", "..#..", ".#...", "#....", "#...."],
    "%": ["##..#", "##..#", "...#.", "..#..", ".#...", "#..##", "#..##"],
    "(": ["...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."],
    ")": [".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."],
    ":": [".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."],
    "+": [".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."],
}


class Canvas:
    """A picture of coloured dots."""

    def __init__(self, width, height, colour):
        self.width = width
        self.height = height
        self.rows = [bytearray(bytes(colour) * width) for _ in range(height)]

    def rect(self, x0, y0, x1, y1, colour):
        """Fill the rectangle from (x0, y0) up to, not including, (x1, y1)."""
        x0 = max(0, x0)
        y0 = max(0, y0)
        x1 = min(self.width, x1)
        y1 = min(self.height, y1)
        if x1 <= x0:
            return
        run = bytes(colour) * (x1 - x0)
        for y in range(y0, y1):
            self.rows[y][x0 * 3:x1 * 3] = run

    def text_width(self, text, scale):
        """The width of the text in dots."""
        return len(text) * 6 * scale - scale if text else 0

    def text(self, x, y, text, scale, colour):
        """Draw capital letters; a lower case letter is drawn as a capital.
        A sign that the font does not have is a blank."""
        for ch in text.upper():
            glyph = GLYPHS.get(ch, GLYPHS[" "])
            for r, line in enumerate(glyph):
                for c, dot in enumerate(line):
                    if dot == "#":
                        self.rect(x + c * scale, y + r * scale,
                                  x + (c + 1) * scale, y + (r + 1) * scale,
                                  colour)
            x += 6 * scale

    def centred(self, cx, y, text, scale, colour):
        """Draw the text with its middle at cx."""
        self.text(cx - self.text_width(text, scale) // 2, y, text, scale,
                  colour)

    def png(self):
        """The bytes of the picture as a PNG file."""
        raw = b"".join(b"\x00" + bytes(row) for row in self.rows)

        def chunk(kind, data):
            body = kind + data
            return (struct.pack(">I", len(data)) + body +
                    struct.pack(">I", zlib.crc32(body) & 0xffffffff))

        head = struct.pack(">IIBBBBB", self.width, self.height, 8, 2, 0, 0, 0)
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", head) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def number_text(v, decimal=False):
    """A number as the chart shows it: whole numbers without a point, the
    others with one decimal (always one with DECIMAL)."""
    if float(v) == int(v) and not decimal:
        return str(int(v))
    return "%.1f" % v


def nice_top(v):
    """A top for the axis that is a round number above v."""
    step = 1
    while step * 10 < v:
        step *= 10
    for mult in (1, 2, 2.5, 5, 10):
        if mult * step >= v:
            return mult * step
    return 10 * step


def frame(canvas, title, top, unit, left=110, right=40, upper=100,
          lower=80):
    """The title, the axes and the grid; returns the box of the bars."""
    canvas.centred(canvas.width // 2, 26, title, 3, BLACK)
    x0, y0 = left, upper
    x1, y1 = canvas.width - right, canvas.height - lower
    for k in range(6):
        value = top * k / 5.0
        y = y1 - int((y1 - y0) * k / 5.0)
        canvas.rect(x0, y, x1, y + 1, GREY)
        label = number_text(value)
        canvas.text(x0 - 10 - canvas.text_width(label, 2), y - 7, label, 2,
                    BLACK)
    canvas.rect(x0, y0, x0 + 2, y1 + 2, BLACK)
    canvas.rect(x0, y1, x1, y1 + 2, BLACK)
    canvas.text(12, y0 - 34, unit, 2, BLACK)
    return x0, y0, x1, y1


def legend(canvas, items, x, y):
    """A row of coloured squares with the names of the series."""
    for name, colour in items:
        canvas.rect(x, y, x + 18, y + 18, colour)
        canvas.text(x + 26, y + 2, name, 2, BLACK)
        x += 26 + canvas.text_width(name, 2) + 34


def grouped(title, unit, labels, series, colours):
    """Bars side by side: one group for each label, one bar for each series
    (a name and the values)."""
    canvas = Canvas(960, 490, WHITE)
    top = nice_top(max(max(v) for _, v in series))
    x0, y0, x1, y1 = frame(canvas, title, top, unit)
    legend(canvas, [(n, c) for (n, _), c in zip(series, colours)], x0 + 70,
           66)
    group = (x1 - x0) // len(labels)
    bar = int(group * 0.78 / len(series))
    for g, label in enumerate(labels):
        gx = x0 + g * group + int(group * 0.11)
        for s, (name, values) in enumerate(series):
            h = int((y1 - y0) * values[g] / float(top))
            bx = gx + s * bar
            canvas.rect(bx, y1 - h, bx + bar - 3, y1, colours[s])
            text = number_text(values[g])
            canvas.centred(bx + (bar - 3) // 2, y1 - h - 22, text, 2, BLACK)
        canvas.centred(x0 + g * group + group // 2, y1 + 18, label, 2, BLACK)
    return canvas


def pass_chart(title, unit, xs, ys, passes):
    """One bar for each size of document; the colour of a bar is the number
    of LaTeX runs that the print needed."""
    canvas = Canvas(960, 490, WHITE)
    top = nice_top(max(ys))
    x0, y0, x1, y1 = frame(canvas, title, top, unit)
    pass_colours = {2: TEAL, 3: ORANGE}
    group = (x1 - x0) // len(xs)
    bar = int(group * 0.6)
    for g, (x, y, n) in enumerate(zip(xs, ys, passes)):
        h = int((y1 - y0) * y / float(top))
        bx = x0 + g * group + (group - bar) // 2
        canvas.rect(bx, y1 - h, bx + bar, y1, pass_colours[n])
        canvas.centred(bx + bar // 2, y1 - h - 22, number_text(y), 2, BLACK)
        canvas.centred(bx + bar // 2, y1 + 18, "%d PAGES" % x, 2, BLACK)
    legend(canvas, [("2 PASSES", TEAL), ("3 PASSES", ORANGE)], x0 + 70, 66)
    return canvas


def make_all():
    """The pictures: name -> Canvas."""
    pics = {}
    labels = ["%d PAGES" % p for p in PAGES]
    pics["story-memory-save"] = grouped(
        "MEMORY AFTER A SAVE, BEFORE AND AFTER", "MEGABYTES", labels,
        list(DATA["memory-save"][1].items()), [ORANGE, BLUE])
    pt = DATA["print-time"][1]
    pics["story-print-time"] = pass_chart(
        "TIME OF A PRINT TO PDF", "SECONDS", pt["pages"], pt["seconds"],
        pt["passes"])
    return pics


def main(argv):
    """Write the pictures into the folder."""
    out = argv[1] if len(argv) > 1 else os.path.join(
        HERE, "..", "samples", "story", "pictures")
    os.makedirs(out, exist_ok=True)
    for name, canvas in sorted(make_all().items()):
        with open(os.path.join(out, name + ".png"), "wb") as f:
            f.write(canvas.png())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
