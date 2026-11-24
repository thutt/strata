#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program writes the test documents of the section numbers into the
# directory that is its argument: secnum_deep.doc, secnum_deeper.doc,
# secnum_fln.doc,
# secnum_fla0.doc, secnum_forms.doc, secnum_jump.doc, secnum_contract.doc,
# secnum_start2.doc, secnum_title.doc, secnum_heads.doc,
# secnum_chars.doc and secnum_xref.doc.  The documents
# are plain files of the format that docs/DOC-FORMAT.txt describes, so the
# files in tests/data can be checked in and this program only shows how they
# were made.  Every headline is one word, such as Hd007x, so that
# tests/secnum.py finds it in the text of the PDF; the word does not occur
# anywhere else.
#
#     python3 -I tests/mksecdata.py tests/data

import sys

CRLF = b"\r\n"
EOL = b"\x1d"

# The common header of a document.  Records are added after it.
BASE = [b"*R3", b"MR1001", b"PP\\1\\\x00", b"P1C33", b"FHA1", b"FTT5",
        b"DP0,8500,11000"]


class Counter:
    """Hands out the headwords Hd001x, Hd002x and so on."""

    def __init__(self):
        self.n = 0

    def word(self):
        self.n += 1
        return b"Hd%03dx" % self.n


def sect(word, kids=(), body=True, fold=None, head=None):
    """A section: its headline word, a body, and its subsections."""
    return {"word": word, "kids": list(kids), "body": body, "fold": fold,
            "head": head}


def emit(sections, out, depth_open):
    """Writes the records of a list of sections as one group."""
    for s in sections:
        out.append(b"S" if s["fold"] is None else b"SE%d" % s["fold"])
        head = s["head"] if s["head"] is not None else s["word"] + EOL
        out.append(b"T" + head + b"\x00")

        if s["body"]:
            out.append(b"T" + b"The body of a section." + EOL + b"\x00")

        if s["kids"]:
            out.append(b"+")
            emit(s["kids"], out, depth_open + 1)
            out.append(b"-")


def document(header_extra, sections, title=None, extra_open=0):
    """The text of a structured document; title is the text of the title
    page, and extra_open adds groups that hold no section (a document that
    starts at a deeper level)."""
    # The documents have no table of contents: a print makes one from the
    # level of the record YCL, and these are not about it.
    out = list(BASE) + list(header_extra) + [b"YCL0", b"*B1"]
    out.append(b"S")

    if title is not None:
        out.append(b"T" + title + EOL + b"\x00")

    out.append(b"S")
    out.append(b"+")

    for _ in range(extra_open):
        out.append(b"+")

    emit(sections, out, 1)

    for _ in range(extra_open):
        out.append(b"-")

    out.append(b"-")
    out.append(b"*E")

    return CRLF.join(out) + CRLF


def chain(c, depth, width):
    """A tree: width siblings at every level, and a chain down to depth
    levels below the first sibling, so that the counters are above one."""
    def level(n):
        sibs = []

        for i in range(width):
            kids = level(n + 1) if (i == 0 and n < depth) else []
            sibs.append(sect(c.word(), kids))

        return sibs

    return level(1)


def deep():
    c = Counter()
    return document([], chain(c, 26, 2))


def deeper():
    """Sections nested 120 levels deep, two at every level: the numbers of
    the levels up to 99 have a tag; the original gives no tag to a section
    of level 100 or deeper."""
    c = Counter()
    return document([], chain(c, 120, 2))


def fln():
    c = Counter()
    return document([b"FLN3"], chain(c, 6, 2))


def fla0():
    c = Counter()
    return document([b"FLA0"], chain(c, 4, 2))


def forms():
    c = Counter()
    fmts = [b"F1GPart \\I\\. \x00", b"F2G\\A\\. \x00", b"F3G(\\a\\) \x00",
            b"F4G\\i\\. \x00", b"F5G\x01\x07 \x00"]
    return document(fmts, chain(c, 8, 3))


def jump():
    """A level 3 section follows a level 1 section with no level 2."""
    c = Counter()
    a = sect(c.word())
    b = sect(c.word(), [sect(c.word(), [sect(c.word()), sect(c.word())])])
    d = sect(c.word())
    return document([], [a, b, d])


def contract():
    """Contracted sections count in the numbering."""
    c = Counter()
    secs = [sect(c.word()),
            sect(c.word(), [sect(c.word()), sect(c.word())], fold=2),
            sect(c.word(), [sect(c.word(), [sect(c.word())], fold=2)]),
            sect(c.word())]
    return document([], secs)


def start2():
    """The first section is at level 2."""
    c = Counter()
    secs = [sect(c.word()), sect(c.word(), [sect(c.word())]), sect(c.word())]
    return document([], secs, extra_open=1)


def title():
    c = Counter()
    secs = [sect(c.word(), [sect(c.word()), sect(c.word())]),
            sect(c.word()), sect(c.word(), [sect(c.word())])]
    return document([b"F1GPart \\I\\. \x00"], secs, title=b"The title page")


def heads():
    """Headlines that are not one plain word: bold text, an empty
    headline, several lines, a marker (a Keycap), a note and an equation."""
    c = Counter()
    w = [c.word() for _ in range(9)]
    keycap = b"\x13" + b"37(" + b"1Q\x00" + CRLF + b"\x13)"
    equation = b"\x13" + b"9(" + b"S" + CRLF + b"Tx^2" + EOL + b"\x00" + \
        CRLF + b"\x13)"
    note = (b"\x13" + b"1(" + b"S" + CRLF + b"TA note." + EOL + b"\x00" +
            CRLF + b"\x13)")
    secs = [
        sect(w[0], head=b"\x12\x01\x00" + w[0] + b"\x12\x00\x00" + EOL),
        sect(w[1], head=EOL),
        sect(w[2], head=w[2] + b" and a second" + EOL + b"line" + EOL),
        sect(w[3], head=w[3] + b" with " + keycap + EOL),
        sect(w[4], head=w[4] + b" with" + note + b" a note" + EOL),
        sect(w[5], [sect(w[6], head=w[6] + b" " + b"x" * 200 + EOL),
                    sect(w[7])]),
        sect(w[8], head=w[8] + b" is " + equation + EOL),
    ]
    return document([], secs)


def chars():
    """Tags with characters that TeX treats in a special way and with
    characters of the code page of the document."""
    c = Counter()
    fmts = [b"F1G\x15%&#_$~^ \\I\\. \x00", b"F2G\xb3\xc4 \\A\\. \x00",
            b"F3G\x01\x01\x01\x02 \\a\\) \x00", b"F4G<\\i\\> \x00",
            b"F5G{\\1\\} \x00"]
    return document(fmts, chain(c, 5, 2))


def xrefs():
    """Cross-references to every section, one that prints the tag of the
    section; the sections have custom tags and a number that is not
    the tag.  The reference r1 is to the first section, r2 to the second
    one and so on."""
    c = Counter()
    nums = [b"1", b"1.1", b"1.2", b"2", b"2.1", b"3"]
    head = [c.word() for _ in nums]
    body = b"".join(b"Ref r%d " % (k + 1) + b"\x13" + b"2(" + b"XN" + nums[k] +
                    b"\x00" + CRLF + b"XT1" + CRLF + b"\x13)" + EOL
                    for k in range(len(nums)))
    third = sect(head[5])
    third["body"] = False
    secs = [sect(head[0], [sect(head[1]), sect(head[2])]),
            sect(head[3], [sect(head[4])]), third]
    out = document([b"F1GPart \\I\\. \x00", b"F2G\\A\\. \x00"], secs)
    # The references are in a block of text after the headline of the last
    # section: put them in front of the last "S" group end.
    marker = b"T" + head[5] + EOL + b"\x00"
    at = out.find(marker) + len(marker)
    return out[:at] + CRLF + b"T" + body + b"\x00" + out[at:]


DOCS = [("secnum_deep.doc", deep), ("secnum_deeper.doc", deeper),
        ("secnum_fln.doc", fln),
        ("secnum_fla0.doc", fla0), ("secnum_forms.doc", forms),
        ("secnum_jump.doc", jump), ("secnum_contract.doc", contract),
        ("secnum_start2.doc", start2), ("secnum_title.doc", title),
        ("secnum_heads.doc", heads), ("secnum_chars.doc", chars),
        ("secnum_xref.doc", xrefs)]


def main(argv):
    if len(argv) != 2:
        print("usage: mksecdata.py DIRECTORY")

        return 2

    for name, make in DOCS:
        with open("%s/%s" % (argv[1], name), "wb") as f:
            f.write(make())

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
