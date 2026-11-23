#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This test checks manual/checkpics.py, the program that compares the
# pictures of the manual with the pictures taken again, with small pictures
# that the test makes itself (no program is started and no picture of the
# manual is read):
#
#   - a picture that is the same is not flagged;
#   - a change of the key strip (thin dark letters on a light bar) is
#     flagged, and the comparison of the light pixels alone, which is what
#     the program did before, does not see it;
#   - a change of the colour of the letters, with the same shapes, is
#     flagged, and the old comparison does not see it either;
#   - another size is flagged;
#   - the lines of the allow file: known, colour, size and ignore accept the
#     differences that they name, and a difference that changed is flagged
#     again;
#   - a mask line leaves one rectangle out of the comparison and the
#     report gives the box around the differences;
#   - --ignore-foot leaves the strip out of the comparison.
#
#     python3 -I -B tests/checkpics.py

import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "manual", "checkpics.py")
W = 120
H = 60
FOOT = 15
BLUE = (0, 0, 170)
YELLOW = (255, 255, 85)
WHITE = (255, 255, 255)
GREY = (170, 170, 170)
BLACK = (0, 0, 0)

fails = 0


def check(name, ok, detail=""):
    """Print one result and count a failure."""
    global fails
    if ok:
        print("ok   %s" % name)
    else:
        fails += 1
        print("FAIL %s" % name)
        if detail != "":
            print(detail)


def write_png(path, rows):
    """Write rows (lists of (r, g, b)) as an 8 bit RGB PNG file."""
    height = len(rows)
    width = len(rows[0])
    raw = b""
    for row in rows:
        raw += b"\x00" + bytes(c for px in row for c in px)

    def chunk(kind, body):
        crc = zlib.crc32(kind + body) & 0xFFFFFFFF
        return struct.pack(">I", len(body)) + kind + body + \
            struct.pack(">I", crc)

    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0,
                                       0))
    data += chunk(b"IDAT", zlib.compress(raw))
    data += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def picture(text=YELLOW, strip_at=4, height=H):
    """A picture like a screen of the Editor: a blue ground, six yellow
    strokes of text, and a key strip of light grey at the foot with six
    thin black strokes (the letters) that start at the column strip_at."""
    rows = [[BLUE] * W for _ in range(height)]
    for k in range(12):
        x = 6 + k * 9
        for y in range(5, 30):
            for dx in range(3):
                rows[y][x + dx] = text
    for y in range(height - FOOT, height):
        rows[y] = [GREY] * W
    for k in range(6):
        x = strip_at + k * 18
        for y in range(height - FOOT + 3, height - FOOT + 12):
            for dx in range(2):
                rows[y][x + dx] = BLACK
    return rows


def old_pixels(a, b):
    """What the program counted before: the pixels that are light in one
    picture and have no light pixel within one pixel in the other one."""
    def light(rows):
        out = []
        for row in rows:
            bits = 0
            for x, px in enumerate(row):
                if (px[0] * 3 + px[1] * 6 + px[2]) // 10 >= 128:
                    bits |= 1 << x
            out.append(bits)
        return out

    def grown(rows):
        wide = [r | (r << 1) | (r >> 1) for r in rows]
        out = []
        for y in range(len(wide)):
            r = wide[y]
            if y > 0:
                r |= wide[y - 1]
            if y + 1 < len(wide):
                r |= wide[y + 1]
            out.append(r)
        return out

    la = light(a)
    lb = light(b)
    ga = grown(la)
    gb = grown(lb)
    bad = 0
    for y in range(len(la)):
        bad += bin((la[y] & ~gb[y]) | (lb[y] & ~ga[y])).count("1")
    return bad


def run(old, new, allow=None, extra=()):
    """Run the program on two folders; return (status, output)."""
    cmd = [sys.executable, "-I", "-B", TOOL, old, new]
    if allow is not None:
        cmd += ["--allow", allow]
    cmd += list(extra)
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stdout


def lines_of(out, name):
    """The report lines that begin with the name of a picture."""
    return [ln for ln in out.split("\n") if ln.startswith(name + " ")]


def main():
    tmp = tempfile.mkdtemp(prefix="cpt-")
    try:
        old = os.path.join(tmp, "old")
        new = os.path.join(tmp, "new")
        os.mkdir(old)
        os.mkdir(new)
        base = picture()
        strip = picture(strip_at=9)
        colour = picture(text=WHITE)
        tall = picture(height=H + 2)
        for name in ("a-same", "b-strip", "c-colour", "d-size"):
            write_png(os.path.join(old, name + ".png"), base)
        write_png(os.path.join(new, "a-same.png"), base)
        write_png(os.path.join(new, "b-strip.png"), strip)
        write_png(os.path.join(new, "c-colour.png"), colour)
        write_png(os.path.join(new, "d-size.png"), tall)

        # What the old comparison saw: nothing of the strip, nothing of the
        # colour.
        check("the old comparison does not see a change of the key strip",
              old_pixels(base, strip) <= 30, old_pixels(base, strip))
        check("the old comparison does not see a change of colour",
              old_pixels(base, colour) <= 30, old_pixels(base, colour))

        noallow = os.path.join(tmp, "none.allow")
        open(noallow, "w").close()
        status, out = run(old, new, noallow)
        check("a picture that is the same is not flagged",
              lines_of(out, "a-same") == [], out)
        b = lines_of(out, "b-strip")
        check("a change of the key strip is flagged as a shape difference",
              len(b) == 1 and "SHAPE" in b[0] and "COLOUR" not in b[0], out)
        c = lines_of(out, "c-colour")
        check("a change of the colour of the letters is flagged as colour "
              "only", len(c) == 1 and "COLOUR" in c[0] and
              "SHAPE" not in c[0], out)
        d = lines_of(out, "d-size")
        check("another size is flagged", len(d) == 1 and "SIZE" in d[0], out)
        check("the exit status is 1 when pictures are flagged", status == 1,
              status)

        # --ignore-foot leaves the strip out.
        status, out = run(old, new, noallow, ["--ignore-foot", str(FOOT)])
        check("--ignore-foot leaves the key strip out of the comparison",
              lines_of(out, "b-strip") == [], out)

        # The numbers, to write the allow file.
        shape = int(re.search(r"(\d+) shape", b[0]).group(1))
        cdiff = int(re.search(r"(\d+) colour", c[0]).group(1))

        allow = os.path.join(tmp, "ok.allow")
        with open(allow, "w") as f:
            f.write("# Test.\n")
            f.write("known b-strip %d the strip was looked at\n" % shape)
            f.write("colour c-colour %d the colour was looked at\n" % cdiff)
            f.write("size d-size %dx%d the size depends on the machine\n"
                    % (W, H + 2))
        status, out = run(old, new, allow)
        check("known, colour and size entries accept what they name",
              status == 0 and "0 flagged" in out and "3 known" in out, out)

        with open(allow, "w") as f:
            f.write("known b-strip %d changed\n" % (shape + 400))
            f.write("colour c-colour %d changed\n" % (cdiff + 4000))
            f.write("size d-size %dx%d changed\n" % (W, H + 9))
        status, out = run(old, new, allow)
        check("a difference that is not the one of the entry is flagged "
              "again", status == 1 and len(lines_of(out, "b-strip")) == 1 and
              len(lines_of(out, "c-colour")) == 1 and
              len(lines_of(out, "d-size")) == 1, out)

        with open(allow, "w") as f:
            f.write("ignore b-strip the time of day\n")
            f.write("ignore c-colour the fonts of the machine\n")
            f.write("ignore d-size the size of the window\n")
        status, out = run(old, new, allow)
        check("an ignore entry is never compared and never flagged",
              status == 0 and "3 ignored" in out, out)

        # The box around the pixels that differ is printed, and a mask line
        # leaves a rectangle out of the comparison and nothing else.
        m = re.search(r"box (\d+),(\d+) to (\d+),(\d+)", b[0])
        check("the report gives the box around the shape difference, in "
              "the key strip", m is not None and int(m.group(2)) >=
              H - FOOT and int(m.group(4)) < H, b[0])
        with open(allow, "w") as f:
            f.write("mask b-strip 0 %d %d %d the strip\n" % (H - FOOT, W,
                                                               FOOT))
            f.write("colour c-colour %d x\n" % cdiff)
            f.write("size d-size %dx%d x\n" % (W, H + 2))
        status, out = run(old, new, allow)
        check("a mask line leaves the rectangle out and the picture is the "
              "same", lines_of(out, "b-strip") == [] and status == 0, out)
        with open(allow, "w") as f:
            f.write("mask b-strip 0 0 %d %d the letters\n" % (W, FOOT))
            f.write("colour c-colour %d x\n" % cdiff)
            f.write("size d-size %dx%d x\n" % (W, H + 2))
        status, out = run(old, new, allow)
        check("a mask line over another place does not hide the difference",
              len(lines_of(out, "b-strip")) == 1 and status == 1, out)

        # A shift of one pixel and the colours at the edge of a letter are
        # not differences.
        shifted = picture(strip_at=5)
        write_png(os.path.join(new, "b-strip.png"), shifted)
        status, out = run(old, new, noallow)
        check("a shift of one pixel is not a difference",
              lines_of(out, "b-strip") == [], out)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("checkpics: %d failures" % fails)
    return 1 if fails != 0 else 0


if __name__ == "__main__":
    sys.exit(main())
