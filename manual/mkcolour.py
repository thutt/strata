#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program writes the sample of the colour picture of the manual:
# the drawing samples/pictures/colour-chart.png (a bar chart in six colours,
# drawn here with no library) and the document samples/colour.doc that
# holds the drawing in a Picture marker.  The Printer prints the document
# for the picture print-colour of the manual.
#
#     python3 -I manual/mkcolour.py manual/samples

import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import mkmanual  # noqa: E402

W = 480
H = 270
BARS = [(214, 39, 40), (255, 127, 14), (230, 200, 20), (44, 160, 44),
        (31, 119, 180), (148, 103, 189)]


def drawing():
    """The pixels of the bar chart: rows of (r, g, b)."""
    px = [[(250, 250, 245)] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            if y % 45 == 20 and 40 <= x < W - 10:
                px[y][x] = (210, 210, 205)
    base = 240
    for i, color in enumerate(BARS):
        top = base - (60 + 28 * ((i * 5) % 7))
        left = 55 + i * 68
        for y in range(top, base):
            for x in range(left, left + 50):
                shade = 0.82 + 0.18 * (x - left) / 50.0
                px[y][x] = tuple(int(c * shade) for c in color)
    for x in range(40, W - 10):
        px[base][x] = (60, 60, 60)
    for y in range(10, base + 1):
        px[y][40] = (60, 60, 60)
    return px


def png(px):
    """The bytes of an 8 bit RGB PNG file."""
    raw = b"".join(b"\x00" + b"".join(bytes(p) for p in row) for row in px)

    def chunk(kind, data):
        c = struct.pack(">I", len(data)) + kind + data
        return c + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main(argv):
    """Write the drawing and the document into the folder."""
    if len(argv) != 2:
        sys.stderr.write("usage: mkcolour.py FOLDER\n")
        return 2
    os.makedirs(os.path.join(argv[1], "pictures"), exist_ok=True)
    with open(os.path.join(argv[1], "pictures", "colour-chart.png"),
              "wb") as f:
        f.write(png(drawing()))
    b = mkmanual.Builder()
    b.desc = "A picture in colour"
    b.section(1, "A picture in colour", None)
    pic = mkmanual.marker(25, [b"1pictures/colour-chart.png\x00", b"23600"])
    b.add([b"BFJ2"], pic)
    b.add([b"BFJ2"], b"Pictures keep their colours when they are printed.")
    b.write(os.path.join(argv[1], "colour.doc"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
