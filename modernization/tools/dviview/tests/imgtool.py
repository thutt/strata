# Image helper for the dviview tests: conversion, statistics, differences.
#
# Only the standard library of Python is used (no Pillow, no numpy), so the
# tests run wherever Python 3 runs.
"""
Usage:
  imgtool.py png IN.ppm OUT.png [W H]     Convert (and crop at 0,0).
  imgtool.py ink IN.png                   Print the fraction of dark pixels.
  imgtool.py same A.png B.png             Exit 0 when the images are equal.
  imgtool.py diff A.png B.png OUT.png     Write a difference picture and
                                          print the fractions that differ.
"""

import struct
import sys
import zlib


class Img:
    # An image of w by h pixels; rgb holds three bytes for every pixel.
    def __init__(self, w, h, rgb):
        self.w = w
        self.h = h
        self.rgb = rgb


def paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def unfilter(raw, stride, h, bpp):
    # Undo the PNG scanline filters; returns the h rows without filter bytes.
    rows = []
    prev = bytearray(stride)
    pos = 0
    for _ in range(h):
        ft = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if ft == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif ft == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif ft == 4:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                upleft = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + paeth(left, prev[i], upleft)) & 255
        rows.append(line)
        prev = line
    return rows


def samples(line, w, chans, depth):
    # The w * chans samples of a row, each scaled to the range 0 to 255
    # (for 1, 2 and 4 bits a palette index is returned as it is).
    n = w * chans
    if depth == 8:
        return list(line[:n])
    if depth == 16:
        return [line[2 * i] for i in range(n)]
    out = []
    per = 8 // depth
    mask = (1 << depth) - 1
    for i in range(n):
        out.append((line[i // per] >> (8 - depth * (i % per + 1))) & mask)
    return out


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("not a PNG file: %s" % path)
    pos = 8
    idat = b""
    palette = b""
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB",
                                                                body)
        elif kind == b"PLTE":
            palette = body
        elif kind == b"IDAT":
            idat += body
    if (depth not in (1, 2, 4, 8, 16) or interlace != 0 or
            ctype not in (0, 2, 3, 4, 6)):
        sys.exit("unsupported PNG: %s" % path)
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bits = chans * depth
    stride = (w * bits + 7) // 8
    bpp = max(1, bits // 8)
    rows = unfilter(zlib.decompress(idat), stride, h, bpp)
    rgb = bytearray()
    for line in rows:
        s = samples(line, w, chans, depth)
        if ctype == 3:
            for v in s:
                rgb += palette[3 * v:3 * v + 3]
            continue
        scale = 255 // ((1 << depth) - 1) if depth < 8 else 1
        if ctype == 2:
            rgb += bytes(s)
        elif ctype == 6:
            for i in range(0, len(s), 4):
                rgb += bytes(s[i:i + 3])
        elif ctype == 0:
            for v in s:
                g = v * scale
                rgb += bytes((g, g, g))
        else:
            for i in range(0, len(s), 2):
                rgb += bytes((s[i], s[i], s[i]))
    return Img(w, h, bytes(rgb))


def read_ppm(path):
    data = open(path, "rb").read()
    fields = []
    pos = 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    pos += 1
    if fields[0] != b"P6" or int(fields[3]) != 255:
        sys.exit("unsupported PPM: %s" % path)
    w = int(fields[1])
    h = int(fields[2])
    return Img(w, h, data[pos:pos + 3 * w * h])


def read_img(path):
    if open(path, "rb").read(2) == b"P6":
        return read_ppm(path)
    return read_png(path)


def write_png(path, img):
    raw = bytearray()
    stride = img.w * 3
    for y in range(img.h):
        raw.append(0)
        raw += img.rgb[y * stride:(y + 1) * stride]

    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body +
                struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", img.w, img.h, 8, 2, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    out += chunk(b"IEND", b"")
    open(path, "wb").write(out)


def crop(img, w, h):
    w = min(w, img.w)
    h = min(h, img.h)
    stride = img.w * 3
    rgb = bytearray()
    for y in range(h):
        rgb += img.rgb[y * stride:y * stride + 3 * w]
    return Img(w, h, bytes(rgb))


def gray(img):
    # The brightness of every pixel, 0 to 255 (the weights of ITU-R 601).
    p = img.rgb
    return [(299 * p[i] + 587 * p[i + 1] + 114 * p[i + 2]) // 1000
            for i in range(0, len(p), 3)]


def shifted(img, dx, dy):
    # The image moved by dx and dy pixels, the parts that leave coming back
    # on the other side.
    stride = img.w * 3
    rows = []
    for y in range(img.h):
        src = (y - dy) % img.h
        row = img.rgb[src * stride:(src + 1) * stride]
        k = (-dx * 3) % stride
        rows.append(row[k:] + row[:k])
    return Img(img.w, img.h, b"".join(rows))


def cmd_png(args):
    img = read_img(args[0])
    if len(args) >= 4:
        img = crop(img, int(args[2]), int(args[3]))
    write_png(args[1], img)


def cmd_ink(args):
    g = gray(read_img(args[0]))
    print("%.6f" % (sum(1 for v in g if v < 128) / float(len(g))))


def cmd_soft(args):
    # The number of pixels that are neither near black nor near white: the
    # soft edges of letters.  An outlined box is drawn with hard lines and
    # has none.
    g = gray(read_img(args[0]))
    print(sum(1 for v in g if 40 < v < 215))


def cmd_same(args):
    a = read_img(args[0])
    b = read_img(args[1])
    if a.w != b.w or a.h != b.h or a.rgb != b.rgb:
        sys.exit(1)


def maxdiff(a, b):
    # The largest difference of the three channels of every pixel.
    p = a.rgb
    q = b.rgb
    out = []
    for i in range(0, len(p), 3):
        d0 = abs(p[i] - q[i])
        d1 = abs(p[i + 1] - q[i + 1])
        d2 = abs(p[i + 2] - q[i + 2])
        out.append(d0 if d0 >= d1 and d0 >= d2 else (d1 if d1 >= d2 else d2))
    return out


def compare(ga, a, b):
    # The fractions of pixels that differ, and the difference of every pixel.
    m = maxdiff(a, b)
    gb = gray(b)
    ink = [1 if (x < 224 or y < 224) else 0 for x, y in zip(ga, gb)]
    ink_n = max(sum(ink), 1)
    total = float(a.w * a.h)
    rows = []
    for limit in (32, 64, 128):
        n_all = 0
        n_ink = 0
        for v, k in zip(m, ink):
            if v > limit:
                n_all += 1
                n_ink += k
        rows.append((limit, 100.0 * n_all / total, 100.0 * n_ink / ink_n))
    return ink_n, rows, m


def show(label, ink_n, rows):
    print("%s (ink pixels in either picture: %d)" % (label, ink_n))
    for limit, p_all, p_ink in rows:
        print("  differ by more than %3d/255: %.4f%% of all pixels, "
              "%.2f%% of ink pixels" % (limit, p_all, p_ink))


def cmd_diff(args):
    a = read_img(args[0])
    b = read_img(args[1])
    if a.w != b.w or a.h != b.h:
        print("size mismatch: %dx%d versus %dx%d" % (a.w, a.h, b.w, b.h))
        sys.exit(1)
    ga = gray(a)
    ink_n, rows, m = compare(ga, a, b)
    print("pixels: %d" % (a.w * a.h))
    show("as drawn, no shift", ink_n, rows)
    # Find the shift of the second picture (up to one pixel) that matches
    # best, because dvipng rounds some positions one pixel differently.
    best = (rows[1][2], 0, 0, rows, m)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dx == 0 and dy == 0:
                continue
            ink_s, rows_s, m_s = compare(ga, a, shifted(b, dx, dy))
            if rows_s[1][2] < best[0]:
                best = (rows_s[1][2], dx, dy, rows_s, m_s)
    print("best shift of the second picture: dx=%d dy=%d" % (best[1],
                                                              best[2]))
    show("after that shift", ink_n, best[3])
    # The picture shows differences as dark pixels on white.
    out = bytearray()
    for v in best[4]:
        g = 255 - min(255, v * 2)
        out += bytes((g, g, g))
    write_png(args[2], Img(a.w, a.h, bytes(out)))


def pix(img, x, y):
    i = (y * img.w + x) * 3
    return img.rgb[i], img.rgb[i + 1], img.rgb[i + 2]


def region(img, args):
    # An optional region x0 y0 x1 y1 (x1 and y1 excluded) of the image.
    if len(args) >= 4:
        return (int(args[0]), int(args[1]), min(int(args[2]), img.w),
                min(int(args[3]), img.h))
    return 0, 0, img.w, img.h


def cmd_size(args):
    img = read_img(args[0])
    print("%d %d" % (img.w, img.h))


def inked_box(img, reg):
    # The box of the pixels that are not white inside the region.
    x0, y0, x1, y1 = reg
    xs = []
    ys = []
    for y in range(y0, y1):
        for x in range(x0, x1):
            if pix(img, x, y) != (255, 255, 255):
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return min(xs), min(ys), max(xs), max(ys)


def cmd_bbox(args):
    img = read_img(args[0])
    box = inked_box(img, region(img, args[1:]))
    print("none" if box is None else "%d %d %d %d" % box)


def cmd_px(args):
    img = read_img(args[0])
    print("%d %d %d" % pix(img, int(args[1]), int(args[2])))


def cmd_count(args):
    img = read_img(args[0])
    want = (int(args[1]), int(args[2]), int(args[3]))
    tol = int(args[4])
    x0, y0, x1, y1 = region(img, args[5:])
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            p = pix(img, x, y)
            if max(abs(p[i] - want[i]) for i in range(3)) <= tol:
                n += 1
    print(n)


def cmd_frac(args):
    # The colour at a fraction of the box of the ink inside the region.
    img = read_img(args[0])
    fx = float(args[1])
    fy = float(args[2])
    box = inked_box(img, region(img, args[3:]))
    if box is None:
        sys.exit("no ink")
    x = int(box[0] + fx * (box[2] - box[0]))
    y = int(box[1] + fy * (box[3] - box[1]))
    print("%d %d %d" % pix(img, x, y))


def cmd_compare(args):
    # Pixels that differ by more than 96/255 where either picture has ink.
    # With a radius r a pixel only counts when no pixel of the other picture
    # within r pixels is close in colour: edges and one pixel shifts pass.
    a = read_img(args[0])
    b = read_img(args[1])
    rad = int(args[2]) if len(args) > 2 else 0
    w = min(a.w, b.w)
    h = min(a.h, b.h)
    bad = 0
    ink = 0
    white = (255, 255, 255)
    for y in range(h):
        for x in range(w):
            p = pix(a, x, y)
            q = pix(b, x, y)
            if p == white and q == white:
                continue
            ink += 1
            if max(abs(p[i] - q[i]) for i in range(3)) <= 96:
                continue
            if rad == 0:
                bad += 1
                continue
            ok_a = False
            ok_b = False
            for dy in range(-rad, rad + 1):
                for dx in range(-rad, rad + 1):
                    xx = x + dx
                    yy = y + dy
                    if 0 <= xx < w and 0 <= yy < h:
                        if max(abs(p[i] - pix(b, xx, yy)[i])
                               for i in range(3)) <= 96:
                            ok_a = True
                        if max(abs(q[i] - pix(a, xx, yy)[i])
                               for i in range(3)) <= 96:
                            ok_b = True
            if not (ok_a and ok_b):
                bad += 1
    print("%d %d %.4f" % (bad, ink, bad / float(max(ink, 1))))


def main():
    cmds = {"png": cmd_png, "ink": cmd_ink, "same": cmd_same,
            "diff": cmd_diff, "size": cmd_size, "bbox": cmd_bbox,
            "px": cmd_px, "count": cmd_count, "frac": cmd_frac,
            "compare": cmd_compare, "soft": cmd_soft}
    if len(sys.argv) < 2 or sys.argv[1] not in cmds:
        sys.exit("usage: imgtool.py png|ink|same|diff|size|bbox|px|count|"
                 "frac|compare|soft ARGS")
    cmds[sys.argv[1]](sys.argv[2:])


if __name__ == "__main__":
    main()
