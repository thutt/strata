#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program writes the test documents of the Page Break Level and of the
# spacings into the directory that is its argument: pb_0.doc, pb_1.doc,
# pb_2.doc, pb_title.doc, sp_0.doc, sp_line2.doc, sp_block3.doc,
# sp_override.doc and sp_named.doc.  The documents are plain files of the
# format that docs/DOC-FORMAT.txt describes, so the files in tests/data can
# be checked in and this program only shows how they were made.  Every
# headline and every block of text is one word, such as Pb001x, so that
# tests/pbspace.py finds it in the text of the PDF; the word does not occur
# anywhere else.
#
#     python3 -I tests/mkpbdata.py tests/data

import sys

CRLF = b"\r\n"
EOL = b"\x1d"

# The common header of a document.  Records are added after it.
BASE = [b"*R3", b"MR1001", b"PP\\1\\\x00", b"P1C33", b"FHA1", b"FTT5",
        b"DP0,8500,11000"]

BODY = b" ".join([b"The text of the block goes on and on, and it shows very "
                  b"little, so that a block is several lines long."] * 3)


class Counter:
    """Hands out the words Pb001x, Pb002x and so on."""

    def __init__(self, prefix):
        self.n = 0
        self.prefix = prefix

    def word(self):
        self.n += 1
        return b"%s%03dx" % (self.prefix, self.n)


def text(word, props=()):
    """A block of one column: its property records, then its text."""
    out = list(props)
    out.append(b"T" + word + b" " + BODY + EOL + b"\x00")
    return out


def structured(header, tree, title=None):
    """A structured document.  tree is a list of (word, children)."""
    out = list(BASE) + list(header) + [b"YCL0", b"*B1", b"S"]

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


def unstructured(header, blocks):
    """A document of blocks: each is a (word, property records)."""
    out = list(BASE) + list(header) + [b"*B0", b"S"]

    for word, props in blocks:
        out += text(word, props)

    out.append(b"*E")
    return CRLF.join(out) + CRLF


def chapters():
    """Three chapters of two sections each, so that levels 1 and 2 exist."""
    c = Counter(b"Pb")
    tree = []

    for _ in range(3):
        top = c.word()
        kids = [(c.word(), []), (c.word(), [])]
        tree.append((top, kids))

    return tree


def plain(n, prefix, props=()):
    """n blocks, each with the same property records."""
    c = Counter(prefix)
    return [(c.word(), props) for _ in range(n)]


def main(argv):
    out = argv[1]
    files = {
        "pb_0.doc": structured([], chapters()),
        "pb_1.doc": structured([b"FLP1"], chapters()),
        "pb_2.doc": structured([b"FLP2"], chapters()),
        "pb_title.doc": structured([b"FLP1"], chapters(), title=b"Title"),
        "sp_0.doc": unstructured([], plain(40, b"Sp")),
        "sp_line2.doc": unstructured([b"FSL200"], plain(40, b"Sp")),
        "sp_block3.doc": unstructured([b"FST300"], plain(40, b"Sp")),
        "sp_override.doc": unstructured(
            [], plain(40, b"Sp", [b"BSL200", b"BSB300"])),
        "sp_named.doc": unstructured(
            [b"FSL200", b"FST300", b"FNtight\x00", b"BSL100", b"BSB100",
             b"FN\x00"], plain(40, b"Sp", [b"BGtight\x00"])),
    }

    for name, data in files.items():
        with open("%s/%s" % (out, name), "wb") as f:
            f.write(data)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
