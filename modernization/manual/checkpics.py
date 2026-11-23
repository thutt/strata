#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program compares the pictures of the manual with the same pictures
# taken again from the programs of the tree.  A picture is taken from the
# running program on the day it is made, so a change of a menu, a panel or
# the drawing of the screen makes it wrong without any test failing.  The
# target "pics-check" of build.sh takes every picture again into a scratch
# folder and runs this program on the two folders:
#
#     python3 -I manual/checkpics.py OLDDIR NEWDIR [--log FILE]
#         [--threshold PIXELS] [--colour-threshold PIXELS]
#         [--ignore-foot ROWS] [--allow FILE]
#
# For each picture of OLDDIR that is also in NEWDIR it compares the shapes
# and the colours.
#
# The shapes: a pixel is "light" when its brightness is 128 or more and
# "dark" when it is less.  The light pixels and the dark pixels are compared
# one after the other, because text may be light on a dark ground (the text
# of the screen) or dark on a light ground (the key strip, the menu bars,
# the panels): a thin dark letter on a light bar is a hole in the light
# pixels, which the light comparison does not see.  A pixel of one picture
# differs when it is light (or dark) there and there is no light (or dark)
# pixel within one pixel of the same place in the other picture, or the
# other way round.  So the small differences that the drawing of the letters
# makes (the colours at the edge of a letter, a shift of a pixel) are not
# differences, and a letter, a line, a box or an entry of a menu that is
# there in one picture and not in the other one is.  The number of pixels
# that differ, light and dark together, is the shape difference.
#
# The colours: every channel is made one of four levels, which gives 64
# classes of colour, and the colour difference is the number of pixels that
# are in another class in the new picture than in the old one (the sum of
# the changes of the counts of the classes, halved).  So a letter that is
# yellow in one picture and white in the other one, with the same shape, is
# a difference of colour and not of shape.
#
# A picture is flagged when its shape difference is more than the threshold
# (30 pixels by default), or its colour difference is more than the colour
# threshold (300 pixels by default), or it has another size.  With
# --ignore-foot the last ROWS rows of a picture are not compared (shape and
# colour), so that the key strip at the foot of the Editor can be left out;
# the strip is compared by default.
#
# The file checkpics.allow, next to this program (or the file of --allow),
# holds what is known.  A line that begins with a number sign is a comment;
# the other lines are:
#
#     none NAME                  no scene makes the picture NAME and it is
#                                not missing (the pages that the Printer
#                                prints are made again by pics-check, from
#                                the scripts of scenes/, and compared).
#     known NAME PIXELS reason   a shape difference of PIXELS pixels was
#                                looked at and is accepted; the picture is
#                                flagged again when the difference changes
#                                by more than the threshold.
#     colour NAME PIXELS reason  the same for the colour difference.
#     size NAME WIDTHxHEIGHT reason
#                                the new picture has this size, and a
#                                picture of that size is accepted without a
#                                comparison (the size depends on the machine:
#                                the window of the Viewer); another size is
#                                flagged.
#     mask NAME X Y WIDTH HEIGHT reason
#                                the rectangle of the picture at the column
#                                X and the row Y is left out of the
#                                comparison (shape and colour) in both
#                                pictures, because it shows something that
#                                changes from run to run: the time of day,
#                                the path of the program.  The rest of the
#                                picture is compared.  A picture may have
#                                several mask lines.
#     ignore NAME reason         the picture is never compared and never
#                                flagged: it depends as a whole on something
#                                that changes (the fonts that are installed).
#                                Say why.  Prefer mask.
#
# A picture that is flagged is listed with the box (x0,y0 to x1,y1) around
# the pixels of shape that differ, to write a mask line.
#
# The pictures that only one folder has are listed, and so are the scenes
# that the log of shots.py (--log) tells were skipped or failed.  Nothing is
# changed in either folder.  The exit status is 1 when a picture is flagged
# or missing.

import collections
import glob
import os
import struct
import sys
import zlib

INK = 128
DEFAULT_THRESHOLD = 30
DEFAULT_COLOUR_THRESHOLD = 300
# Four levels of a channel: the byte divided by 64.
LEVELS = bytes(i >> 6 for i in range(256))


def paeth(a, b, c):
    """The predictor of PNG filter type 4."""
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path):
    """Return (width, height, rows) of an 8 bit RGB or RGBA PNG file; each
    row is a bytes object of RGB triples (the alpha channel is dropped)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")
    pos = 8
    idat = b""
    width = height = depth = kind = 0
    while pos < len(data):
        length, name = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if name == b"IHDR":
            width, height, depth, kind = struct.unpack(">IIBB", body[:10])
            if body[12] != 0:
                raise ValueError("an interlaced PNG file")
        elif name == b"IDAT":
            idat += body
        elif name == b"IEND":
            break
    if depth != 8 or kind not in (2, 6):
        raise ValueError("a PNG file that is not 8 bit RGB or RGBA")
    bpp = 3 if kind == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * bpp
    rows = []
    prev = bytearray(stride)
    pos = 0
    for _ in range(height):
        ftype = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if ftype == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif ftype == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif ftype == 4:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                upleft = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + paeth(left, prev[i], upleft)) & 255
        prev = line
        if bpp == 4:
            rgb = bytearray()
            for i in range(0, stride, 4):
                rgb += line[i:i + 3]
            rows.append(bytes(rgb))
        else:
            rows.append(bytes(line))
    return width, height, rows


def masks(rows):
    """The light pixels and the dark pixels of the rows, each row as a
    number whose bits are the pixels of the row; and the number of pixels of
    each of the 64 classes of colour."""
    light = []
    dark = []
    classes = collections.Counter()
    for row in rows:
        bits = 0
        for x in range(0, len(row), 3):
            if (row[x] * 3 + row[x + 1] * 6 + row[x + 2]) // 10 >= INK:
                bits |= 1 << (x // 3)
        light.append(bits)
        dark.append(~bits & ((1 << (len(row) // 3)) - 1))
        q = row.translate(LEVELS)
        classes.update(zip(q[0::3], q[1::3], q[2::3]))
    return light, dark, classes


def grown(rows):
    """The same rows with the ink made one pixel thicker in every direction."""
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


def shape_pixels(a, b):
    """The number of pixels that differ between two lists of rows of bits:
    set in one list and not set within one pixel of the same place in the
    other one; and the rows of bits of those pixels."""
    ga = grown(a)
    gb = grown(b)
    bad = 0
    rows = []
    for y in range(len(a)):
        bits = (a[y] & ~gb[y]) | (b[y] & ~ga[y])
        rows.append(bits)
        bad += bin(bits).count("1")
    return bad, rows


def colour_pixels(ca, cb):
    """The number of pixels that are in another class of colour: half of the
    sum of the changes of the counts."""
    total = 0
    for key in set(ca) | set(cb):
        total += abs(ca[key] - cb[key])
    return total // 2


def blank(rows, rects):
    """The rows with the rectangles (x, y, width, height) made black."""
    out = list(rows)
    for x, y, w, h in rects:
        for r in range(max(0, y), min(len(out), y + h)):
            row = out[r]
            left = 3 * max(0, x)
            right = min(len(row), 3 * (x + w))
            if left < right:
                out[r] = row[:left] + bytes(right - left) + row[right:]
    return out


def box_of(rows):
    """The box (x0, y0, x1, y1) around the set bits of the rows, or None."""
    ys = [y for y in range(len(rows)) if rows[y] != 0]
    if not ys:
        return None
    bits = 0
    for y in ys:
        bits |= rows[y]
    return (
        (bits & -bits).bit_length() - 1, ys[0], bits.bit_length() - 1, ys[-1])


def differing(old_rows, new_rows, width, ignore_foot, rects=()):
    """The shape difference (light and dark pixels), the colour difference,
    the number of pixels compared and the box around the shape difference.
    The rectangles are left out of both pictures."""
    count = max(0, len(old_rows) - ignore_foot)
    old_rows = blank(old_rows[:count], rects)
    new_rows = blank(new_rows[:count], rects)
    la, da, ca = masks(old_rows)
    lb, db, cb = masks(new_rows)
    n1, r1 = shape_pixels(la, lb)
    n2, r2 = shape_pixels(da, db)
    both = [x | y for x, y in zip(r1, r2)]
    return n1 + n2, colour_pixels(ca, cb), count * width, box_of(both)


def skipped_scenes(log):
    """The scenes that shots.py skipped, from its output."""
    found = []
    if log and os.path.exists(log):
        with open(log, "r", encoding="ascii", errors="replace") as f:
            for line in f:
                if line.startswith("shots:") and (
                        "skipped" in line or "failed" in line):
                    found.append(line.strip())
    return found


def read_allow(path):
    """What is known: the pictures that no scene makes, the known shape and
    colour differences, the accepted sizes, the rectangles to leave out and
    the pictures to ignore."""
    none = set()
    known = {}
    colour = {}
    sizes = {}
    rects = {}
    ignore = {}
    if os.path.exists(path):
        with open(path, "r", encoding="ascii") as f:
            for line in f:
                words = line.split()
                if len(words) < 2 or words[0].startswith("#"):
                    continue
                kind = words[0]
                if kind == "none":
                    none.add(words[1])
                elif kind == "known" and len(words) >= 3:
                    known[words[1]] = int(words[2])
                elif kind == "colour" and len(words) >= 3:
                    colour[words[1]] = int(words[2])
                elif kind == "size" and len(words) >= 3:
                    w, _, h = words[2].partition("x")
                    sizes[words[1]] = (int(w), int(h))
                elif kind == "mask" and len(words) >= 6:
                    rects.setdefault(words[1], []).append(
                        tuple(int(v) for v in words[2:6]))
                elif kind == "ignore":
                    ignore[words[1]] = " ".join(words[2:])
    return none, known, colour, sizes, rects, ignore


def names(folder):
    """The picture names of a folder, without the extension."""
    return {os.path.splitext(os.path.basename(p))[0]: p for p in
            glob.glob(os.path.join(folder, "*.png"))}


def main(argv):
    """Entry point."""
    args = argv[1:]
    log = None
    allow = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "checkpics.allow")
    threshold = DEFAULT_THRESHOLD
    cthreshold = DEFAULT_COLOUR_THRESHOLD
    foot = 0
    folders = []
    while args:
        a = args.pop(0)
        if a == "--log" and args:
            log = args.pop(0)
        elif a == "--allow" and args:
            allow = args.pop(0)
        elif a == "--threshold" and args:
            threshold = int(args.pop(0))
        elif a == "--colour-threshold" and args:
            cthreshold = int(args.pop(0))
        elif a == "--ignore-foot" and args:
            foot = int(args.pop(0))
        else:
            folders.append(a)
    if len(folders) != 2:
        sys.stderr.write("usage: checkpics.py OLDDIR NEWDIR [--log FILE] "
                         "[--threshold PIXELS] [--colour-threshold PIXELS] "
                         "[--ignore-foot ROWS] [--allow FILE]\n")
        return 2
    old = names(folders[0])
    new = names(folders[1])
    none, known, colour, sizes, rects, ignore = read_allow(allow)
    flagged = 0
    same = 0
    accepted = 0
    ignored = 0
    print("%-34s %-11s %-11s %s" % ("picture", "old size", "new size",
                                    "pixels that differ: shape and colour"))
    for name in sorted(old):
        if name not in new:
            continue
        if name in ignore:
            ignored += 1
            continue
        try:
            ow, oh, orows = read_png(old[name])
            nw, nh, nrows = read_png(new[name])
        except (ValueError, zlib.error, struct.error) as e:
            print("%-34s cannot be read: %s" % (name, e))
            flagged += 1
            continue
        if (ow, oh) != (nw, nh):
            if sizes.get(name) == (nw, nh):
                accepted += 1
            else:
                print("%-34s %-11s %-11s SIZE" % (
                    name, "%dx%d" % (ow, oh), "%dx%d" % (nw, nh)))
                flagged += 1
            continue
        shape, cdiff, total, box = differing(orows, nrows, ow, foot,
                                             rects.get(name, ()))
        percent = 100.0 * shape / max(1, total)
        shape_ok = (abs(shape - known[name]) <= threshold if name in known
                    else shape <= threshold)
        colour_ok = (abs(cdiff - colour[name]) <= cthreshold
                     if name in colour else cdiff <= cthreshold)
        if shape_ok and colour_ok:
            if name in known or name in colour:
                accepted += 1
            else:
                same += 1
        else:
            what = []
            if not shape_ok:
                what.append("SHAPE")
            if not colour_ok:
                what.append("COLOUR")
            print("%-34s %-11s %-11s %6.2f%% %6d shape %6d colour  %s%s" % (
                name, "%dx%d" % (ow, oh), "%dx%d" % (nw, nh), percent,
                shape, cdiff, " ".join(what),
                "" if box is None else "  box %d,%d to %d,%d" % box))
            flagged += 1
    only_old = sorted(set(old) - set(new) - none - set(ignore))
    only_new = sorted(set(new) - set(old))
    for name in only_old:
        print("only in the old pictures: %s" % name)
    for name in only_new:
        print("only in the new pictures: %s" % name)
    skipped = skipped_scenes(log)
    for line in skipped:
        print(line)
    print("checkpics: %d picture(s) compared, %d the same, %d known "
          "difference(s), %d flagged, %d ignored, %d missing from the new "
          "pictures, %d new, %d scene(s) skipped, %d picture(s) that no "
          "scene makes"
          % (same + accepted + flagged, same, accepted, flagged, ignored,
             len(only_old), len(only_new), len(skipped), len(none)))
    return 1 if flagged or only_old or only_new or skipped else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
