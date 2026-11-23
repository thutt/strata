#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program writes the test documents of the table of contents into the
# directory that is its argument: toc_ten.doc (four chapters of one to two
# sections each, ten headlines in all, FLP1 so that each chapter starts a
# page), toc_title.doc (the same with a title page), toc_deep.doc (three
# levels) and toc_flat.doc (a document with no structure).  The documents are
# plain files of the format that docs/DOC-FORMAT.txt describes, so the files
# in tests/data can be checked in and this program only shows how they were
# made.  Every headline and every block of text starts with a word such as
# Tc001x that occurs nowhere else, so that tests/tocpdf.py finds it in the
# text of the PDF.
#
#     python3 -I tests/mktocdata.py tests/data

import sys

CRLF = b"\r\n"
EOL = b"\x1d"

BASE = [b"*R3", b"MR1001", b"PP\\1\\\x00", b"P1C33", b"FHA1", b"FTT5",
        b"DP0,8500,11000"]

BODY = b" ".join([b"The text of the block goes on and on, and it shows very "
                  b"little."] * 2)


class Counter:
    """Hands out the words Tc001x, Tc002x and so on."""

    def __init__(self, prefix):
        self.n = 0
        self.prefix = prefix

    def word(self):
        self.n += 1
        return b"%s%03dx" % (self.prefix, self.n)


def text(word):
    """A block of one column: its text."""
    return [b"T" + word + b" " + BODY + EOL + b"\x00"]


def structured(header, tree, title=None):
    """A structured document.  tree is a list of (word, children)."""
    out = list(BASE) + list(header) + [b"*B1", b"S"]

    if title is not None:
        out += text(title)

    out += [b"S", b"+"]

    def emit(nodes):
        for word, kids in nodes:
            out.append(b"S")
            out.append(b"T" + word + EOL + b"\x00")
            out.extend(text(word + b"b"))

            if kids:
                out.append(b"+")
                emit(kids)
                out.append(b"-")

    emit(tree)
    out += [b"-", b"*E"]
    return CRLF.join(out) + CRLF


def unstructured(header, n):
    """A document of n blocks."""
    c = Counter(b"Tf")
    out = list(BASE) + list(header) + [b"*B0", b"S"]

    for _ in range(n):
        out += text(c.word())

    out.append(b"*E")
    return CRLF.join(out) + CRLF


def ten():
    """Four chapters with 2, 1, 2 and 1 sections: ten headlines."""
    c = Counter(b"Tc")
    tree = []

    for kids in (2, 1, 2, 1):
        top = c.word()
        tree.append((top, [(c.word(), []) for _ in range(kids)]))

    return tree


def deep():
    """Chapters, sections and subsections (three levels)."""
    c = Counter(b"Td")
    tree = []

    for _ in range(2):
        top = c.word()
        sec = c.word()
        tree.append((top, [(sec, [(c.word(), []), (c.word(), [])])]))

    return tree


def main(argv):
    out = argv[1]
    files = {
        "toc_ten.doc": structured([b"FLP1"], ten()),
        "toc_title.doc": structured([b"FLP1"], ten(), title=b"Tctitle"),
        "toc_deep.doc": structured([], deep()),
        "toc_flat.doc": unstructured([], 6),
    }

    for name, data in files.items():
        with open("%s/%s" % (out, name), "wb") as f:
            f.write(data)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
