#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program writes the test documents of the Keep marker (docs/MARKERS.txt)
# into the directory that is its argument: keep_fixed.doc, keep_text.doc and
# so on.  The documents are plain Manuscript files (docs/DOC-FORMAT.txt), so
# the files in tests/data can be checked in and this program only shows how
# they were made.  tests/keep.py imports it to make the control documents,
# the same documents with the markers taken out.
#
#     python3 -I tests/mkkeepdata.py tests/data
#
# Every line of text is a label such as KA03, so that a test can say on which
# page a line was printed.

import os
import sys

CRLF = b"\r\n"
EOL = b"\x1d"

HEADER = (b"*R3" + CRLF + b"MR1001" + CRLF + b"PP\\1\\\x00" + CRLF +
          b"P1C33" + CRLF + b"FHA1" + CRLF + b"FTT5" + CRLF +
          b"DP0,8500,11000" + CRLF)

# The three forms of the marker: Start (Fixed), Start (Floating), End.
START = b"\x134(\x13)"
FLOAT = b"\x134(11" + CRLF + b"\x13)"
END = b"\x134(12" + CRLF + b"\x13)"

# Lines of the text of a page of the typesetter; the documents are made so
# that a region does not fit after FILL lines.
FILL = 34


def lines(prefix, n):
    """n lines of text, PREFIX01 and so on, each with its line end."""
    return b"".join(("%s%02d" % (prefix, i + 1)).encode() + EOL
                    for i in range(n))


def block(text, first=False):
    return (b"" if first else b"B" + CRLF) + b"T" + text + b"\x00" + CRLF


def document(blocks, markers=True):
    """An unstructured document; without markers the Keep markers go."""
    out = HEADER + b"*B0" + CRLF + b"S" + CRLF
    for i, b in enumerate(blocks):
        if not markers:
            for m in (START, FLOAT, END):
                b = b.replace(m, b"")
            if b == b"" or b == EOL:
                continue
        out += block(b, i == 0)
    return out + b"*E" + CRLF


def table(rows):
    """Blocks of two columns; rows is a list of (left, right) texts."""
    out = b""
    for left, right in rows:
        out += (b"B" + CRLF + b"CW30" + CRLF + b"T" + left + b"\x00" + CRLF +
                b"T" + right + b"\x00" + CRLF)
    return out


def with_table(before, rows, after, markers=True):
    out = HEADER + b"*B0" + CRLF + b"S" + CRLF
    for i, b in enumerate(before):
        out += block(b, i == 0)
    t = table(rows)
    if not markers:
        for m in (START, FLOAT, END):
            t = t.replace(m, b"")
    out += t
    for b in after:
        out += block(b)
    return out + b"*E" + CRLF


def structured(sections, markers=True):
    """Sections: a list of (headline, [block texts]) under a root."""
    out = HEADER + b"YCL0" + CRLF + b"*B1" + CRLF + b"S" + CRLF + b"+" + CRLF
    for head, blocks in sections:
        out += b"S" + CRLF + b"T" + head + EOL + b"\x00" + CRLF
        for b in blocks:
            if not markers:
                for m in (START, FLOAT, END):
                    b = b.replace(m, b"")
                if b == b"" or b == EOL:
                    continue
            out += b"B" + CRLF + b"T" + b + b"\x00" + CRLF
    return out + b"-" + CRLF + b"*E" + CRLF


# Each function makes one document; the argument says whether the markers
# are in it.

def fixed(m=True):
    return document([lines("L", FILL), START + EOL, lines("KA", 5),
                     lines("KB", 5), lines("KC", 5), END + EOL,
                     lines("M", 20)], m)


def text(m=True):
    # The markers are in the text of blocks, not in blocks of their own: a
    # region is made of whole blocks, so KA moves although it is before the
    # Start.
    return document([lines("L", FILL), lines("KA", 5) + START +
                     lines("KB", 5), lines("KC", 5) + END + lines("KD", 3),
                     lines("M", 20)], m)


def floating(m=True):
    return document([lines("L", FILL), FLOAT + EOL, lines("KA", 5),
                     lines("KB", 5), lines("KC", 5), END + EOL,
                     lines("M", 20)], m)


def unmatched(m=True):
    # A Start without an End keeps to the end of the document.
    return document([lines("L", FILL), START + EOL, lines("KA", 5),
                     lines("KB", 5), lines("M", 20)], m)


def stray(m=True):
    # An End without a Start does nothing.
    return document([lines("L", FILL), lines("KA", 5), lines("KB", 5),
                     END + EOL, lines("KC", 5), lines("M", 20)], m)


def nested(m=True):
    # The first End closes the region: it is KA and KB (10 lines) that are
    # kept, and they fit; a region that went on to the second End would
    # have KC in it and move.
    return document([lines("L", FILL), START + EOL, lines("KA", 4),
                     START + EOL, lines("KB", 4), END + EOL,
                     lines("KC", 12), END + EOL, lines("M", 20)], m)


def long(m=True):
    # A region of 80 lines does not fit on a page.
    return document([lines("L", 10), START + EOL, lines("KA", 20),
                     lines("KB", 20), lines("KC", 20), lines("KD", 20),
                     END + EOL, lines("M", 10)], m)


def tabular(m=True):
    # A caption block with the Start, four rows of a table, and an End in a
    # column of the last row.
    rows = [(START + lines("TA", 4), lines("TB", 4)),
            (lines("TC", 4), lines("TD", 4)),
            (lines("TE", 4), lines("TF", 4)),
            (lines("TG", 4), lines("TH", 4) + END)]
    return with_table([lines("L", FILL)], rows, [lines("M", 20)], m)


def sections(m=True):
    # A pair across a section boundary is kept in two parts.
    return structured([(b"SECONE", [lines("L", FILL), START + EOL,
                                    lines("KA", 5)]),
                       (b"SECTWO", [lines("KB", 8), END + EOL,
                                    lines("M", 20)])], m)


def note(m=True):
    # A footnote inside the region stays with its page.
    nt = b"S" + CRLF + b"T" + b"FOOTNOTETEXT" + EOL + b"\x00"
    foot = b"\x131(" + nt + CRLF + b"\x13)"
    return document([lines("L", FILL + 6), START + EOL,
                     b"KA01 has a note" + foot + EOL + lines("KB", 5),
                     lines("KC", 5), END + EOL, lines("M", 20)], m)


def floatend(m=True):
    # A floating region that has to wait for the next page, with no text
    # after it to go on the page: it must not be lost at the end.
    return document([lines("L", FILL), FLOAT + EOL, lines("KA", 5),
                     lines("KB", 5), lines("KC", 5), END + EOL,
                     lines("M", 3)], m)


DOCS = [("keep_fixed", fixed), ("keep_text", text),
        ("keep_float", floating), ("keep_unmatched", unmatched),
        ("keep_stray", stray), ("keep_nested", nested),
        ("keep_long", long), ("keep_table", tabular),
        ("keep_sections", sections), ("keep_notes", note),
        ("keep_floatend", floatend)]


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: mkkeepdata.py DIRECTORY")
    for name, fn in DOCS:
        with open(os.path.join(sys.argv[1], name + ".doc"), "wb") as f:
            f.write(fn(True))


if __name__ == "__main__":
    main()
