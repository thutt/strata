#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program makes the example documents of the pictures of the running
# heads, the index, the lists and the border.  The documents are written
# as Strata documents (the records of docs/DOC-FORMAT.txt) with plain
# example text of our own.
#
#     python3 -I manual/scenes/mkpicdocs.py DIRECTORY
#
# It writes numbers.doc, headfoot.doc, indexlist.doc, border.doc, table.doc,
# secmark.doc and picmissing.doc into DIRECTORY.

import os
import sys

CRLF = b"\r\n"
EOL = b"\x1d"


def trec(text):
    """The text record of a block."""
    return b"T" + text + EOL + b"\x00" + CRLF


def marker(kind, records=()):
    """A marker of the given type with its field records."""
    return (b"\x13" + str(kind).encode() + b"(" +
            b"".join(r + CRLF for r in records) + b"\x13)")


PAGE = marker(22)
SECTION = marker(28)
SECTION_TITLE = marker(28, [b"S3"])


def hfblock(left=b"", centre=b"", right=b""):
    """The block of a header or footer: three columns."""
    return (b"S" + CRLF + b"CW26" + CRLF + b"CFJ0" + CRLF + trec(left) +
            b"CW26" + CRLF + b"CFJ2" + CRLF + trec(centre) + b"CFJ1" + CRLF +
            trec(right))


def header(layouts=(), size=b"8500,11000"):
    """The header of a document; layouts is a list of (record, blocks)."""
    head = b"*R3" + CRLF + b"MR1001" + CRLF + b"PP\\1\\\x00" + CRLF
    head += b"P1HB0" + CRLF + b"P1FB0" + CRLF + b"P1C33" + CRLF
    for rec, blocks in layouts:
        head += rec + CRLF + blocks
    return (head + b"FHA1" + CRLF + b"FTT5" + CRLF + b"DP0," + size +
            CRLF + b"YCL99" + CRLF + b"YIWN2" + CRLF)


def flat(head, blocks):
    """An unstructured document; blocks is a list of (properties, text)
    where the properties are record lines put before the text."""
    out = head + b"*B0" + CRLF + b"S" + CRLF
    for i, (props, text) in enumerate(blocks):
        out += b"" if i == 0 else b"B" + CRLF
        out += b"".join(p + CRLF for p in props) + trec(text)
    return out + b"*E" + CRLF


def sections(head, title, secs):
    """A structured document: a title and sections (headline, paragraphs)."""
    out = head + b"*B1" + CRLF + b"S" + CRLF + trec(title) + b"+" + CRLF
    for headline, paragraphs in secs:
        out += b"SE0" + CRLF + trec(headline)
        for text in paragraphs:
            out += b"B" + CRLF + trec(text)
    return out + b"-" + CRLF + b"*E" + CRLF


FILLER = (b"The tide table gives the hour of high water for each harbour of "
          b"the coast.  The boats leave on the ebb and come back on the "
          b"flood, so the table is the plan of the day.  ")


def numbers():
    """The document of the Math pictures: a short list of prices."""
    rows = [b"Order of the harbour office", b"Rope  12.50", b"Paint  8.25",
            b"Lamps  21.00", b"Chain  3.75"]
    return flat(header(), [([], r) for r in rows])


def headfoot():
    """Three pages of text with a running head and a page number foot."""
    lay = [(b"PHB", hfblock(b"Harbour guide", b"", b"Tide tables")),
           (b"PFB", hfblock(b"", b"Page " + PAGE, b""))]
    secs = []
    for name in (b"Spring", b"Summer", b"Autumn", b"Winter"):
        secs.append((name + b" tides", [FILLER * 6] * 3))
    return sections(header(lay, b"5500,3600"), b"Harbour guide", secs)


def indexlist():
    """Sections with index entries, figures and tables."""
    def entry(main, sub=b""):
        recs = [b"2" + main + b"\x00"]
        if sub:
            recs.append(b"3" + sub + b"\x00")
        return marker(3, recs)

    def figure(name):
        return marker(11, [b"1" + name + b"\x00"])

    def table(name):
        return marker(29, [b"1" + name + b"\x00"])

    secs = [
        (b"Boats", [FILLER * 20 + entry(b"Boats") + figure(b"The old quay"),
                    FILLER * 15 + entry(b"Boats", b"sailing") +
                    table(b"Boat sizes")]),
        (b"Harbours", [FILLER * 25 + entry(b"Harbours") +
                       figure(b"The sea wall"),
                       FILLER * 20 + entry(b"Tides") +
                       table(b"High water")]),
        (b"Lights", [FILLER * 25 + entry(b"Lights") + entry(b"Tides", b"neap"),
                     FILLER * 15 + entry(b"Boats") +
                     figure(b"The north light")]),
        (b"Charts", [FILLER * 25 + entry(b"Charts") + entry(b"Harbours")]),
    ]
    return sections(header(), b"The coast", secs)


def border():
    """A notice and a small table whose cells have a double border."""
    head = header()
    out = head + b"*B0" + CRLF + b"S" + CRLF + trec(b"Notice to the crews")
    for left, right in ((b"Sunday", b"Office closed"),
                        (b"Monday", b"Office open at eight")):
        out += (b"B" + CRLF + b"CW14" + CRLF + b"CB170,1" + CRLF +
                trec(left) + b"CW26" + CRLF + b"CB138,1" + CRLF +
                trec(right))
    out += b"B" + CRLF + trec(b"Signed by the harbour master.")
    return out + b"*E" + CRLF


def table():
    """A table of three columns and five rows, with no border."""
    out = header() + b"*B0" + CRLF + b"S" + CRLF + trec(b"Tide table")
    for k, row in enumerate(((b"Day", b"Low", b"High"),
                             (b"Mon", b"05:10", b"11:32"),
                             (b"Tue", b"05:48", b"12:09"),
                             (b"Wed", b"06:27", b"12:47"),
                             (b"Thu", b"07:08", b"13:26"))):
        out += b"B" + CRLF
        for cell in row:
            out += b"CW12" + CRLF + trec(cell)
    return out + b"*E" + CRLF


def secmark():
    """A line that holds a Section marker with the choice Title Only."""
    text = (SECTION_TITLE + b"  is what the running head of this page shows.")
    return flat(header(), [([], text)])


def picmissing():
    """A picture that is not there and one that is, in the folder of the
    document: the first marker is alone on its line, so the cursor is on it
    when the document is opened."""
    gone = marker(25, [b"1pictures/old-chart.png\x00", b"22000"])
    here = marker(25, [b"1pictures/sample.png\x00", b"22000"])
    return flat(header(), [([], gone),
                           ([], b"The chart above is not in the folder of "
                                b"this document."),
                           ([], here),
                           ([], b"The chart above is.")])


def main(argv):
    """Write the documents into the directory named on the command line."""
    if len(argv) != 2:
        sys.stderr.write("usage: mkpicdocs.py DIRECTORY\n")
        return 2
    os.makedirs(argv[1], exist_ok=True)
    for name, data in (("numbers.doc", numbers()),
                       ("headfoot.doc", headfoot()),
                       ("indexlist.doc", indexlist()),
                       ("border.doc", border()),
                       ("table.doc", table()),
                       ("secmark.doc", secmark()),
                       ("picmissing.doc", picmissing())):
        with open(os.path.join(argv[1], name), "wb") as f:
            f.write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
