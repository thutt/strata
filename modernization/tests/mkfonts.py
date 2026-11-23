#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# Strata is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along
# with Strata.  If not, see <https://www.gnu.org/licenses/>.
# Copyright (c) 2026 Logic Magicians Software

"""Makes the sample document of the standard fonts and sizes.

The sample is a structured document with no title page: it begins with the
first font.  It has one section of the first level for each of the 20 fonts
of the list of Strata (the TeX Font Association, docs/FONTS.txt): the three
fonts of the roles of a document (1 Headline, 2 Text, 3 Header/Footer), the
fifteen standard fonts, Lato (19, which has to be installed first) and the
free number 20.  The headline of a section is the name of the font, and the
number in front of it is made by the document: the Section Tag of the first
level is "Font \\1\\: " (Global Format, Level 1), so that the first level
counts the fonts and the headline of the font 4 is printed as Font 4: Times.
The counter is the number of the font because there is one section for each
font, in the order of the list.  A font that has nothing configured (the
number 20) has the line nothing configured. and no blocks.
The page break level is 0: the fonts follow one another on the pages, and a
Keep region holds each font, so that it is not split between two pages.
Below the headline there is one block for each of five sizes (8, 10, 12, 14
and 20 points).  A block has two columns: the narrow one on the left holds the
size, the wide one on the right the specimen text, set in that font and that
size.

The font and the size of the specimen text are chosen by one LaTeX instruction
marker at its start: \\fontfamily{...}\\fontsize{...}{...}\\selectfont.  The
Font marker of Strata cannot do it: it chooses only one of three families
(roman, sans serif and typewriter), and no marker or format sets a size.
The marker ends with the column, so the next block is set as usual.

The specimen text is not in this file.  It is read from the plain text file
samples/fonts/specimen.txt: one line of the text on each line of the file,
at most ten lines of at most 80 ASCII characters.

    python3 -I -B tests/mkfonts.py [--specimen FILE] [FOLDER]

writes FOLDER/fonts-sample.doc (the default FOLDER is samples/fonts, and the
default specimen text is samples/fonts/specimen.txt).  The output is the
same bytes on every run.
"""
import os
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
FOLDER = os.path.join(ROOT, "samples", "fonts")
SPECIMEN = os.path.join(FOLDER, "specimen.txt")
NAME = "fonts-sample.doc"

CRLF = b"\r\n"
EOL = b"\x1d"
MARK = b"\x13"
ATTR = b"\x12"

TITLE = "Type Fonts and Sizes"
DESCRIPTION = "Sample document: the standard fonts of Strata at the " \
              "standard sizes"
AUTHOR = "The Strata project"

# The fonts of the list of Strata (docs/FONTS.txt, src/fonts.c, numbers 1 to
# 20): the label that the panels show for the number, the name of the LaTeX
# font family that sets it, and its number in the list.  A family of None
# means that nothing is configured for the number.  The families are those of
# the standard collections of TeX Live (psnfss, tex-gyre, lm and the base),
# except Lato, which comes with the full TeX Live and is installed by the
# Document Manager (the TeX Font Association, Install Now).  A family that is
# not installed is replaced by the font of the text, with a warning of LaTeX:
# the print does not fail.
FONTS = [
    ("Headline (Latin Modern Sans)", "lmss", 1),
    ("Text (Latin Modern Roman)", "lmr", 2),
    ("Header/Footer (Latin Modern Roman)", "lmr", 3),
    ("Times", "ptm", 4),
    ("Helvetica", "phv", 5),
    ("Courier", "pcr", 6),
    ("Palatino", "ppl", 7),
    ("Bookman", "pbk", 8),
    ("New Century Schoolbook", "pnc", 9),
    ("Avant Garde", "pag", 10),
    ("Zapf Chancery", "pzc", 11),
    ("Charter", "bch", 12),
    ("Utopia", "put", 13),
    ("TeX Gyre Termes", "qtm", 14),
    ("TeX Gyre Heros", "qhv", 15),
    ("TeX Gyre Pagella", "qpl", 16),
    ("TeX Gyre Cursor", "qcr", 17),
    ("Computer Modern Roman", "cmr", 18),
    ("Lato", "lato-TLF", 19),
    ("(not set)", None, 20),
]

# The section tag of the first level (the record F1G, which ends with a
# NUL): the word Font, the number of the section among the sections of its
# level in arabic digits, a colon and a blank.
SECTION_TAG = b"F1GFont \\1\\: \x00"

NOTHING = "nothing configured."

# The sizes that a document can be set in (the record PS of the file
# format: docs/DOC-FORMAT.txt) and the distance between the lines that goes
# with each of them, in points.
SIZES = [(8, 9.6), (10, 12.0), (12, 14.4), (14, 16.8), (20, 24.0)]

# The width of the left column in character cells.
SIZE_COLUMN = 10

MAX_LINES = 10
MAX_COLUMNS = 80


class FontsError(Exception):
    """A mistake in the specimen text file."""


def read_specimen(path):
    """The lines of the specimen text file, checked."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        raise FontsError("cannot read %s: %s" % (path, e.strerror)) from e

    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as e:
        raise FontsError("%s: the text is not ASCII" % path) from e

    lines = text.split("\n")

    if lines and lines[-1] == "":
        lines.pop()

    if not lines:
        raise FontsError("%s: the text has no line" % path)

    if len(lines) > MAX_LINES:
        raise FontsError("%s: the text has %d lines, at most %d"
                         % (path, len(lines), MAX_LINES))

    for i, line in enumerate(lines, 1):
        if any(ord(c) < 32 or ord(c) > 126 for c in line):
            raise FontsError("%s:%d: a character that is not printable "
                             "ASCII" % (path, i))

        if len(line) > MAX_COLUMNS:
            raise FontsError("%s:%d: the line has %d columns, at most %d"
                             % (path, i, len(line), MAX_COLUMNS))

        if line.strip() == "":
            raise FontsError("%s:%d: the line is empty" % (path, i))

    return lines


def text(s):
    """Text as bytes: printable ASCII only."""
    return s.encode("ascii")


def trec(data):
    """A T record: the text, the end of the text and the end of the line."""
    return b"T" + data + b"\x00" + CRLF


def marker(mtype, records):
    """A marker; the records are byte strings."""
    body = b"".join(r + CRLF for r in records)
    return MARK + str(mtype).encode() + b"(" + body + MARK + b")"


def headline(name):
    """The headline of a font: its name.  The Section Tag of the first level
    puts Font and the number of the font in front of it when it is printed
    and shown."""
    return name


def size_text(points):
    """The size as it is shown in the left column."""
    return "%d point" % points


def marker_text(mtype, cmd):
    """A LaTeX instruction marker: its text is one T record."""
    body = b"S" + CRLF + b"T" + text(cmd) + EOL + b"\x00" + CRLF
    return MARK + str(mtype).encode() + b"(" + body + MARK + b")"


def header(title):
    """The records of the header of the document."""
    page = b"".join([
        b"PHB" + CRLF,
        b"S" + CRLF,
        b"CW26" + CRLF, b"CFJ0" + CRLF, trec(text(title) + EOL),
        b"CW26" + CRLF, b"CFJ2" + CRLF, trec(EOL),
        b"CFJ1" + CRLF, trec(EOL),
        b"PFB" + CRLF,
        b"S" + CRLF,
        b"CW26" + CRLF, b"CFJ0" + CRLF, trec(EOL),
        b"CW26" + CRLF, b"CFJ2" + CRLF,
        trec(b"Page " + marker(22, []) + EOL),
        b"CFJ1" + CRLF, trec(EOL),
    ])
    recs = [
        b"*R3",
        b"MD" + text(DESCRIPTION) + b"\x00",
        b"MA" + text(AUTHOR) + b"\x00",
        b"MR1000",
        b"PML1000", b"PMR1000", b"PMT1000", b"PMB1000",
        b"PP\\1\\\x00",
        b"P1C33",
    ]
    out = CRLF.join(recs) + CRLF + page
    tail = [
        b"P1HB0", b"P1FB0",
        b"FHA1",
        b"FTT5",
        SECTION_TAG,
        b"DP0,8500,11000",
        b"DF10NProgram\x00",
        b"YCL0",
        b"YIWN2",
    ]
    return out + CRLF.join(tail) + CRLF


def block(left, right):
    """A block of two columns: the size and the specimen text."""
    return (b"B" + CRLF + b"CW%d" % SIZE_COLUMN + CRLF + b"CFJ0" + CRLF +
            trec(text(left) + EOL) + b"CFJ0" + CRLF + trec(right))


def specimen_data(lines, family, points, leading):
    """The text of the right column: the marker that sets the font and the
    size, the lines, and a marker that ends the last paragraph inside the
    group of the size (without it the last line would be set with the
    distance between the lines of the text around the column)."""
    data = marker_text(
        41, "\\fontfamily{%s}\\fontsize{%d}{%.1f}\\selectfont "
        % (family, points, leading))
    data += EOL.join(text(line) for line in lines)
    data += marker_text(41, "\\par\\vspace{2pt} ")

    return data + EOL


def make(lines):
    """The bytes of the document."""
    out = header(TITLE)
    out += b"*B1" + CRLF
    # The first section is on the first level at once: nothing stands
    # before it (no title page).
    out += b"+" + CRLF
    keep_start = b"B" + CRLF + trec(marker(4, []) + EOL)
    keep_end = b"B" + CRLF + trec(marker(4, [b"12"]) + EOL)

    for index, (name, family, number) in enumerate(FONTS):
        out += b"S" + CRLF + trec(text(headline(name)) + EOL)

        # A Keep marker of the type Start (Fixed) opens a region that holds
        # a font, and a Keep marker of the type End closes it, so that a
        # font is never split between two pages.  The Start marker of a font
        # is the last block of the section of the font before it, so that
        # the region holds the headline too and the headline is never left
        # alone at the foot of a page.  The first font needs none: it is at
        # the top of the first page.
        if index == 0:
            out += keep_start

        if family is None:
            out += b"B" + CRLF + trec(text(NOTHING) + EOL)
        else:
            for points, leading in SIZES:
                out += block(size_text(points),
                             specimen_data(lines, family, points, leading))

        out += keep_end

        if index + 1 < len(FONTS):
            out += keep_start

    out += b"-" + CRLF + b"*E" + CRLF
    return out


def main(argv):
    folder = FOLDER
    specimen = SPECIMEN
    args = argv[1:]

    while args:
        if args[0] == "--specimen" and len(args) >= 2:
            specimen = args[1]
            args = args[2:]
        elif not args[0].startswith("-"):
            folder = args[0]
            args = args[1:]
        else:
            sys.stderr.write("usage: mkfonts.py [--specimen FILE] [FOLDER]\n")
            return 2

    try:
        lines = read_specimen(specimen)
    except FontsError as e:
        sys.stderr.write("mkfonts.py: %s\n" % e)
        return 1

    data = make(lines)
    os.makedirs(folder, exist_ok=True)

    with open(os.path.join(folder, NAME), "wb") as f:
        f.write(data)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
