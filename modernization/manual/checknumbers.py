#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program checks the numbering of the documents of the manual.  The
# manual uses the numbering system of the product: the headlines of the
# generated documents hold no typed numbers, and the section tags are made
# by the product.  It has three uses.
#
#     python3 -I -B manual/checknumbers.py docs DOC...
#         Checks the generated documents: the header has no FLA0 record (the
#         automatic section tags are on) and no headline of the structured
#         part starts with a number that was typed into its text (a number or
#         a letter, points and numbers, then two blanks).  Exits with 1 when
#         a document does not hold.
#
#     python3 -I -B manual/checknumbers.py snapshot PDF META OUT
#         Writes to OUT the numbers as they are printed in the PDF of the
#         single document: for every headline of the first three levels the
#         number, the title and the page, the rows of the contents, and every
#         "section N (Title)" of the text, with its count.
#
#     python3 -I -B manual/checknumbers.py compare OLD NEW
#         Compares two snapshots line by line and prints every difference.
#         Exits with 1 when there is one.
#
# It needs pdftotext for the snapshot.

import collections
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import checkpdf  # noqa: E402

# A number typed into the text of a headline by the generator: digits or a
# letter of the appendices, then points and digits, then two blanks.
TYPED = re.compile(rb"^(\d+|[A-F])(\.\d+)*  \S")


def doc_problems(path):
    """The problems of one generated document."""
    with open(path, "rb") as f:
        data = f.read()
    lines = data.split(b"\r\n")
    bad = []
    in_header = True
    after_section = False
    for i, line in enumerate(lines):
        if in_header:
            if line == b"FLA0":
                bad.append("the header has FLA0: the section tags are off")
            if line.startswith(b"*B"):
                in_header = False
            continue
        if line == b"S":
            after_section = True
            continue
        if after_section and line[:1] == b"T":
            after_section = False
            text = line[1:].rstrip(b"\x00")
            plain = re.sub(rb"[\x00-\x1f]", b"", text)
            if TYPED.match(plain):
                bad.append("a headline has a typed number: %s"
                           % plain[:40].decode("cp437", "replace"))
        elif after_section and line[:1] not in (b"B", b"C", b"F", b"+",
                                                 b"-", b"S"):
            # A property record of the headline block; keep looking.
            continue
        elif after_section:
            if line[:1] in (b"+", b"-", b"S"):
                after_section = False
    return bad


def snapshot(pdf, meta, out):
    """Write the numbers as printed."""
    pages = checkpdf.pages_of(pdf)
    heads = checkpdf.headlines(meta)
    left_out = checkpdf.hidden_of(meta)
    heads = [h for h in heads if (h[1], h[2]) not in left_out]
    lines = []
    first = None
    chap = [h for h in heads if h[0] == 1 and h[1] != ""][0]
    head_pat = checkpdf.pattern(chap[1], chap[2])
    for i, t in enumerate(pages):
        if i < 2:
            continue
        for m in head_pat.finditer(t):
            line = t[m.start():t.find("\n", m.start())]
            if not re.search(r"\s\d+\s*$", line):
                first = i
                break
        if first is not None:
            break
    if first is None:
        first = 0
    cursor = first
    for level, number, title in heads:
        if number == "" or level > 3 or "{" in title or "--" in title:
            continue
        pat = checkpdf.pattern(number, title)
        for i in range(cursor, len(pages)):
            if pat.search(pages[i]):
                lines.append("H\t%d\t%s\t%s\t%d" % (level, number, title,
                                                    i + 1))
                cursor = i
                break
        else:
            lines.append("H\t%d\t%s\t%s\tnot found" % (level, number, title))
    for t in pages[1:first]:
        for line in t.split("\n"):
            line = " ".join(line.split())
            if line:
                lines.append("C\t" + line)
    counts = collections.Counter()
    for t in pages:
        flat = " ".join(t.split())
        for m in re.finditer(r"[Ss]ection ([A-F0-9][0-9.]*) \(([^)]*)\)", flat):
            counts[(m.group(1), m.group(2))] += 1
    for (number, title), n in sorted(counts.items()):
        lines.append("X\t%s\t%s\t%d" % (number, title, n))
    with open(out, "w", encoding="ascii", errors="replace") as f:
        f.write("\n".join(lines) + "\n")
    print("snapshot: %d lines (%d headlines, %d reference kinds)" % (
        len(lines), sum(1 for x in lines if x[0] == "H"), len(counts)))
    return 0


def compare(old, new):
    """Print the differences of two snapshots."""
    with open(old, "r", encoding="ascii", errors="replace") as f:
        a = f.read().split("\n")
    with open(new, "r", encoding="ascii", errors="replace") as f:
        b = f.read().split("\n")
    only_a = collections.Counter(a)
    only_b = collections.Counter(b)
    only_a.subtract(only_b)
    diff = 0
    for line, n in sorted(only_a.items()):
        if n > 0:
            print("only in the old: %s" % line)
            diff += 1
        elif n < 0:
            print("only in the new: %s" % line)
            diff += 1
    print("compare: %d differences (%d and %d lines)" % (diff, len(a), len(b)))
    return 1 if diff else 0


def main(argv):
    if len(argv) >= 3 and argv[1] == "docs":
        status = 0
        for path in argv[2:]:
            for p in doc_problems(path):
                print("%s: %s" % (os.path.basename(path), p))
                status = 1
        if status == 0:
            print("numbers: %d documents use the numbering of the product"
                  % len(argv[2:]))
        return status
    if len(argv) == 5 and argv[1] == "snapshot":
        return snapshot(argv[2], argv[3], argv[4])
    if len(argv) == 4 and argv[1] == "compare":
        return compare(argv[2], argv[3])
    print(__doc__ if __doc__ else "usage: see the header of the file")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
