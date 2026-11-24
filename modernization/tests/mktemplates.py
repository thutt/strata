#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program writes the templates that Strata ships, into the folder that
# is its argument (the committed copies are in templates/):
#
#     python3 -I tests/mktemplates.py templates
#     python3 -I tests/mktemplates.py --example FOLDER   (with example text)
#
# The templates are written into folders below FOLDER: envelopes, letters,
# labels/avery/us and labels/avery/a4 (docs/TEMPLATES.txt).
#
# Letters in the style of the LaTeX letter class (US Letter and A4) and
# envelopes for several countries, and the label sheets (tests/mklabels.py).  Every template has a description (the
# record MD, which the list of templates shows).  All text and layout are
# our own.  The positions of the address blocks of the envelopes are in
# ENVELOPES, in millimetres from the top left corner of the envelope.

import os
import sys

CRLF = b"\r\n"
EOL = b"\x1d"
# The ruler of a document has 80 cells across the text, whatever the paper:
# a cell is a eightieth of the width between the margins.  LINE is the pitch
# of the lines of the text and TOP the distance of the top of the letters of
# a line from its top, in millimetres, as measured in a print (tests/
# templates.py checks the positions again).
# The text of an envelope is 14 points (the record PS): an envelope is read
# at arm's length and addressed in larger type than a letter.
POINTS = 14
# The text sizes that differ: the return line in the window of the German
# envelope is small by design (DIN 5008) and must stay inside the field of
# 85 mm; the sender block of the European C5 must stay above 28 mm.
POINTS_OF = {"envelope-de-din5008-dl": 11, "envelope-eu-c5": 12}
# Every envelope that has a sender has the return address 7 mm from the left
# edge and 7 mm from the top edge, measured to the first glyph box of the
# first line in the PDF (the owner's decision of 2026-11-20: 5 mm at first,
# then 2 mm more after the first print on the HP LaserJet 2015dn, which showed
# 11 mm left and 15 mm top for the old half inch margins).  The left margin is
# 7 mm (0.276 inch); the top margin
# depends on the size of the type, because the glyph box of a line begins
# below the top margin by an amount that grows with the size: the margin is
# (19.843 points - that amount) / 0.072 thousandths of an inch, measured in
# the PDF (tests/templates.py and tests/envfont.py measure the result).  The
# German window envelope has its sender in the corner too: the window then
# shows only the recipient.  The Japanese envelopes have no sender block and
# keep their positions.
CORNER_LEFT = 276
CORNER_TOP_OF = {8: 228, 9: 237, 10: 228, 11: 230, 12: 224, 14: 219, 17: 204,
                 20: 195}
# The pitch of the lines in millimetres at each size (13.6, 14.5 and 17 pt).
LINE_OF = {11: 4.8, 12: 5.1, 14: 6.0}
LINE = 4.8
TOP = 1.0
LEFT = 0.3


def marker(t, recs=()):
    return b"\x13" + str(t).encode() + b"(" + b"".join(
        r + CRLF for r in recs) + b"\x13)"


def text(*lines):
    """The text of a block: the lines, each ended with the end of line."""
    return b"".join(l + EOL for l in lines)


class Doc:
    """A document of blocks that use named formats."""

    def __init__(self, desc, paper, width, height, margins, formats,
                 points=None):
        self.points = points            # the text size, the record PS
        self.desc = desc
        self.paper = (paper, width, height)
        self.margins = margins          # left, right, top, bottom in 1/1000 in
        self.formats = formats          # name -> list of B records
        self.hf = []
        self.blocks = []
        self.cursor = None              # the record ZP: where the cursor is

    def block(self, fmt, data, extra=()):
        self.blocks.append((fmt, data, list(extra)))

    def bytes(self):
        ml, mr, mt, mb = self.margins
        out = [b"*R3", b"MD" + self.desc.encode("ascii") + b"\x00",
               b"MA" + b"Strata" + b"\x00", b"MR1000",
               b"PML%d" % ml, b"PMR%d" % mr, b"PMT%d" % mt, b"PMB%d" % mb,
               b"PP\\1\\\x00", b"P1C33"]
        out += self.hf

        if self.points is not None:
            out.append(b"PS%d" % self.points)

        out += [b"FHA1", b"FTT5",
                b"DP%d,%d,%d" % self.paper]
        for name in sorted(self.formats):
            out.append(b"FN" + name.encode() + b"\x00")
            out += self.formats[name]
        if self.formats:
            out.append(b"FN\x00")
        if self.cursor is not None:
            out.append(b"ZP%d,%d,%d,%d" % self.cursor)
        out += [b"*B0", b"S"]
        for i, (fmt, data, extra) in enumerate(self.blocks):
            if i:
                out.append(b"B")
            if fmt:
                out.append(b"BG" + fmt.encode() + b"\x00")
            out += extra
            out.append(b"T" + data + b"\x00")
        out.append(b"*E")
        return CRLF.join(out) + CRLF


def hfblock(centre):
    """The block of a header: three columns, the centre one has the text."""
    def trec(t):
        return b"T" + t + EOL + b"\x00"
    return CRLF.join([b"S", b"CW26", b"CFJ0", trec(b""), b"CW26", b"CFJ2",
                      trec(centre), b"CFJ1", trec(b"")])


def letter_formats(indent, date_space=200):
    """The named formats of a letter; indent is the left indent of the
    sender, date, closing and signature in cells (3.5 inches); date_space
    is the space before the date in hundredths of a line (the recipient
    follows the date, so it moves the recipient down)."""
    bl = b"BL%d" % indent
    return {
    "Sender": [bl],
    "Date": [bl, b"BSB%d" % date_space],
    "Recipient": [b"BSB300"],
    "Salutation": [b"BSB200"],
    "Body": [b"BSB200", b"BFJ3"],
    "Closing": [bl, b"BSB200"],
    "Signature": [bl, b"BSB200"],
    "Enclosure": [b"BSB300"],
    "Copy": [b"BSB100"],
    "Postscript": [b"BSB200", b"BFJ3"],
    }


def letter(desc, width, height, date_space=200, example=False):
    d = Doc(desc, 0 if width == 8500 else 8, width, height,
            (1000, 1000, 1000, 1000),
            letter_formats(round(3.5 / ((width - 2000) / 1000.0) * 80),
                           date_space))
    # The first page has no head; the other pages have the continuation head
    # "Recipient - date - page".
    # The head carries the recipient only in the example copy.
    who = b"Ms. Jane Example - " if example else b""
    head = hfblock(who + marker(7) + b" - Page " + marker(22))
    d.hf = [b"PHB", head, b"P3HB0", b"P3FB0"]

    def lines(*example_lines):
        """The lines of a block: the example lines, or one empty line."""
        return text(*example_lines) if example else text(b"")

    d.block("Sender", lines(b"Strata Sample Company", b"100 Main Street",
                            b"Springfield, ST 12345"))
    d.block("Date", marker(7) + EOL)
    d.block("Recipient", lines(b"Ms. Jane Example", b"Example Industries",
                               b"200 Oak Avenue", b"Shelbyville, ST 54321"))
    d.block("Salutation", lines(b"Dear Ms. Example,"))
    d.block("Body", lines(b"Write the first paragraph of the letter here.  "
                          b"The paragraphs have no indent and a blank line "
                          b"between them, as in the letter class of LaTeX."))
    d.block("Body", lines(b"Write the second paragraph here."))
    d.block("Closing", lines(b"Sincerely,"))
    # The space for the signature is three empty lines in both copies.
    d.block("Signature", text(b"", b"", b"", b"John Q. Writer",
                              b"Sales Manager") if example else
            text(b"", b"", b"", b""))
    d.block("Copy", lines(b"cc: Mr. Sample Copy"))
    d.block("Enclosure", lines(b"encl: Price list"))
    d.block("Postscript", lines(b"P.S. A postscript goes here."))
    return d


# name: (description, paper code, width, height in 1/1000 inch, margins,
# sender (x, y) and recipient (x, y) in mm from the top left corner of the
# envelope, the lines of the sender and of the recipient)
ENVELOPES = {
    # North America.  The sizes are the American sizes (Wikipedia, Envelope);
    # the positions keep the address inside the reading area of the United
    # States Postal Service (Domestic Mail Manual 202).
    "envelope-us-ca-10": (
        "Number 10 envelope (4 1/8 x 9 1/2 in) for the United States and "
        "Canada: sender at the top left, recipient below the middle.",
        13, 9500, 4125, (12.7, 12.7), (101.6, 52.0),
        ["Strata Sample Company", "100 Main Street",
         "Springfield, ST 12345"],
        ["Ms. Jane Example", "Example Industries", "200 Oak Avenue",
         "SHELBYVILLE ST 54321"]),
    "envelope-us-ca-9": (
        "Number 9 envelope (3 7/8 x 8 7/8 in) for the United States and "
        "Canada: sender at the top left, recipient below the middle.",
        16, 8875, 3875, (12.7, 12.7), (95.0, 46.0),
        ["Strata Sample Company", "100 Main Street",
         "Springfield, ST 12345"],
        ["Ms. Jane Example", "Example Industries", "200 Oak Avenue",
         "SHELBYVILLE ST 54321"]),
    "envelope-us-ca-monarch": (
        "Monarch envelope, number 7 3/4 (3 7/8 x 7 1/2 in) for the United "
        "States and Canada: sender at the top left, recipient below the "
        "middle.",
        12, 7500, 3875, (12.7, 12.7), (88.9, 46.0),
        ["Strata Sample Company", "100 Main Street",
         "Springfield, ST 12345"],
        ["Ms. Jane Example", "Example Industries", "200 Oak Avenue",
         "SHELBYVILLE ST 54321"]),
    # The United Kingdom.  The positions meet the rules of Royal Mail (Guide
    # for Clear Addressing, 2026).
    "envelope-uk-c5": (
        "C5 envelope (162 x 229 mm) for the United Kingdom: sender at the "
        "top left, recipient lower right, postcode on the last line.",
        15, 9016, 6378, (12.0, 12.0), (110.0, 70.0),
        ["Strata Sample Company", "10 High Street", "London"],
        ["Ms. Jane Example", "Example Industries", "20 Church Road",
         "BRISTOL", "BS1 1AA"]),
    "envelope-uk-dl": (
        "DL envelope (110 x 220 mm) for the United Kingdom: sender at the "
        "top left, recipient lower right, postcode on the last line.",
        14, 8661, 4331, (10.0, 10.0), (100.0, 45.0),
        ["Strata Sample Company", "10 High Street", "London"],
        ["Ms. Jane Example", "20 Church Road", "BRISTOL", "BS1 1AA"]),
    "envelope-uk-c6": (
        "C6 envelope (114 x 162 mm) for the United Kingdom: sender at the "
        "top left, recipient lower right, postcode on the last line.",
        16, 6378, 4488, (10.0, 10.0), (70.0, 48.0),
        ["Strata Sample Company", "10 High Street", "London"],
        ["Ms. Jane Example", "20 Church Road", "BRISTOL", "BS1 1AA"]),
    # Germany: the address field of DIN 5008 form B is 85 by 45 mm, 20 mm from
    # the left and 45 mm from the top edge of the letter.  In the DL window
    # envelope (DIN 680: a window of 90 by 45 mm, 20 mm from the left and 15
    # mm from the bottom edge) the window begins 110 - 15 - 45 = 50 mm from the
    # top edge of the envelope, so the field of a letter that rests on the
    # bottom of the envelope (105 mm folded, 5 mm of play) is in the window.
    # The letter has 10 mm of play across (210 in 220) and is taken to lie in
    # the middle, so its field, 20 mm from its left edge, is 25 mm from the
    # left edge of the envelope.  The return line is 3 mm or more inside the
    # window, and the recipient is in the address zone, 17.7 mm below the top
    # of the field (DIN 5008: five lines of 8 point above it).
    "envelope-de-din5008-dl": (
        "DL window envelope (110 x 220 mm) for Germany, DIN 5008 form B: "
        "sender in the corner, recipient in the window (90 x 45 mm).",
        14, 8661, 4331, (25.0, 54.0), (25.0, 67.7),
        ["Strata GmbH - Hauptstr. 1 - 12345 Musterstadt"],
        ["Frau", "Erika Mustermann", "Beispielweg 2", "54321 Beispielstadt"]),
    "envelope-fr-dl": (
        "DL envelope (110 x 220 mm) for France: sender top left, address "
        "in the right half, 20 mm from the right and bottom edges (La "
        "Poste).",
        14, 8661, 4331, (10.0, 10.0), (112.5, 50.0),
        ["Strata Exemple SARL", "1 rue Principale", "75001 PARIS"],
        ["MME ERIKA EXEMPLE", "2 RUE DES FLEURS", "69001 LYON"]),
    # The ISO sizes of the other countries of Europe, of Latin America and of
    # Australia.  The size is the ISO size; the place of the address is the
    # layout of the United Kingdom, which meets the rules that were found
    # (docs/ENVELOPES.txt, "Not verified").
    "envelope-eu-dl": (
        "DL envelope (110 x 220 mm) for continental Europe and Russia, A4 "
        "folded in three; Royal Mail margins (no national rule found).",
        14, 8661, 4331, (10.0, 10.0), (100.0, 45.0),
        ["Strata Sample Company", "Example Street 1", "1000 Example City"],
        ["Ms. Jane Example", "Example Street 2", "2000 Sample Town"]),
    "envelope-eu-c5": (
        "C5 envelope (162 x 229 mm) for continental Europe and Russia, A4 "
        "folded once; Royal Mail margins (no national rule found).",
        15, 9016, 6378, (12.0, 12.0), (110.0, 70.0),
        ["Strata Sample Company", "Example Street 1", "1000 Example City"],
        ["Ms. Jane Example", "Example Industries", "Example Street 2",
         "2000 Sample Town"]),
    "envelope-eu-c65": (
        "C6/5 envelope (114 x 229 mm) for Switzerland and other European "
        "countries, A4 folded in three; Royal Mail margins.",
        16, 9016, 4488, (10.0, 10.0), (100.0, 45.0),
        ["Strata Sample Company", "Example Street 1", "1000 Example City"],
        ["Ms. Jane Example", "Example Street 2", "2000 Sample Town"]),
    "envelope-latam-dl": (
        "DL envelope (110 x 220 mm, sobre americano) for Latin America, A4 "
        "folded in three; Royal Mail margins (no national rule found).",
        14, 8661, 4331, (10.0, 10.0), (100.0, 45.0),
        ["Empresa Strata Ejemplo", "Calle Principal 1", "Ciudad Ejemplo"],
        ["Sra. Ana Ejemplo", "Avenida Central 2", "Ciudad Muestra"]),
    "envelope-au-dl": (
        "DL envelope (110 x 220 mm) for Australia: sender top left, "
        "recipient lower right; meets the address zone of Australia Post.",
        14, 8661, 4331, (10.0, 10.0), (100.0, 45.0),
        ["Strata Sample Pty Ltd", "10 High Street", "SYDNEY NSW 2000"],
        ["Ms Jane Example", "20 Church Road", "MELBOURNE VIC 3000"]),
    "envelope-au-c5": (
        "C5 envelope (162 x 229 mm) for Australia: sender top left, "
        "recipient lower right; meets the address zone of Australia Post.",
        15, 9016, 6378, (12.0, 12.0), (110.0, 70.0),
        ["Strata Sample Pty Ltd", "10 High Street", "SYDNEY NSW 2000"],
        ["Ms Jane Example", "Example Industries", "20 Church Road",
         "MELBOURNE VIC 3000"]),
    # Japan, Western-style envelopes (yougata) addressed in horizontal
    # writing.  The sender goes on the back, as Japan Post asks (the lower
    # third of the back): no sender block here.  The postal code is the first
    # line, top left.  Yougata 4: the address is in the left window of JIS S
    # 5502 (90 by 45 mm, 22 mm from the left and 12 mm from the top edge,
    # from the japanlco files of KOMA-Script); Yougata 2 has no window and
    # the address starts two characters from the left edge (Japan Post).
    "envelope-jp-yougata-4": (
        "Japan, Western-style envelope Yougata 4 (235 x 105 mm), A4 folded "
        "in three: horizontal address in the JIS window; sender on the "
        "back.",
        16, 9252, 4134, None, (28.0, 15.0), [],
        ["123-4567", "Tokyo-to Chiyoda-ku Example 1-2-3", "Example Building 4F",
         "Taro Yamada sama"]),
    "envelope-jp-yougata-2": (
        "Japan, Western-style envelope Yougata 2 (162 x 114 mm), A5 or A4 "
        "folded: horizontal address, postal code first; sender on the "
        "back.",
        16, 6378, 4488, None, (10.0, 15.0), [],
        ["123-4567", "Tokyo-to Chiyoda-ku Example 1-2-3", "Example Building 4F",
         "Taro Yamada sama"]),
}

# The owner's decision of 2026-11-20: the return address of every envelope
# that has one is 7 mm from the left edge and 7 mm from the top edge, whatever
# the country (the table above holds the study of each country's own place
# for it, which is not used any more; the recipient keeps its place).
CORNER_MM = 7.0
for _key, _val in list(ENVELOPES.items()):
    if _val[4] is not None:
        ENVELOPES[_key] = _val[:4] + ((CORNER_MM, CORNER_MM),) + _val[5:]


def envelope(key, desc=None, sender_lines=None, rcpt_lines=None,
             example=False):
    """The envelope document of a template.  The three optional arguments
    make a sample from the template (tests/mksamples.py): the description,
    the lines of the sender and the lines of the recipient; the places of the
    blocks and the paper are those of the template.  The template itself has
    empty address blocks; example=True fills them with the example lines of
    ENVELOPES (the tests measure that copy), and a sample gives its own."""
    (tdesc, code, w, h, sender, rcpt, tsender_lines, trcpt_lines) = \
        ENVELOPES[key]
    desc = tdesc if desc is None else desc
    filled = example or sender_lines is not None or rcpt_lines is not None
    # The gap between the blocks is made for the number of lines that the
    # block has when the address is written, so that the recipient is where
    # the address zone is whether the block is empty or not.
    nsender = len(tsender_lines if sender_lines is None else sender_lines)
    sender_lines = tsender_lines if sender_lines is None else sender_lines
    rcpt_lines = trcpt_lines if rcpt_lines is None else rcpt_lines

    def block_text(lines):
        """The text of an address block: the lines of a document that has
        an address, one empty line for a template."""
        return text(*[l.encode() for l in lines]) if filled else text(b"")

    # No side margins: 80 cells across the width.  An envelope with a sender
    # has a left margin of 7 mm, so its cell is smaller.
    corner = sender is not None
    left_mm = CORNER_LEFT / 1000.0 * 25.4 if corner else 0.0
    cell = (w * 25.4 / 1000.0 - left_mm) / 80
    points = POINTS_OF.get(key, POINTS)
    line = LINE_OF[points]
    # The top block of the envelope (the sender, or the recipient of an
    # envelope without a sender) is the first block of the document, so that
    # a new document from the template starts the cursor in it.  Its place
    # down the envelope is the top margin of the page; the recipient follows
    # at the distance of a Whitespace marker, in millimetres.
    first = (sender or rcpt)[1]
    top_margin = round(max(first - TOP, 0.5) / 25.4 * 1000)
    margins = (0, 0, top_margin, 0)
    sender_indent = round(((sender or (0, 0))[0] - LEFT) / cell)

    if corner:
        margins = (CORNER_LEFT, 0, CORNER_TOP_OF[points], 0)
        sender_indent = 0
        # The place of the sender in the model of this file (the top of the
        # letters, TOP below the top margin), so that the gap to the recipient
        # keeps the recipient where it was.
        sender = (sender[0], CORNER_TOP_OF[points] / 1000.0 * 25.4 + TOP)

    # The indent of the recipient in cells; the nearest cell, but never more
    # than half a millimetre left of the place that the study gives.
    rcpt_indent = round((rcpt[0] - LEFT - left_mm) / cell)

    if corner and left_mm + LEFT + rcpt_indent * cell < rcpt[0] - 0.5:
        rcpt_indent += 1

    d = Doc(desc, code, w, h, margins, {
        "Sender": [b"BL%d" % sender_indent],
        "Recipient": [b"BL%d" % rcpt_indent]},
        points=points)

    if sender is None:
        d.block("Recipient", block_text(rcpt_lines))

        return d

    d.block("Sender", block_text(sender_lines))
    gap = rcpt[1] - sender[1] - line * nsender
    d.block(None, marker(31, [b"1%d" % round(max(gap, 1.0) * 1000),
                              b"U2"]) + EOL)
    d.block("Recipient", block_text(rcpt_lines))

    return d


def build(example=False):
    """The documents of all the templates, by name; EXAMPLE gives the copies
    that hold the example text (the tests measure those)."""
    docs = {
        "letter-us": letter("A business letter on US Letter paper in the "
                            "style of the LaTeX letter class: sender, date, "
                            "recipient, closing, signature, cc, encl, P.S.",
                            8500, 11000, example=example),
        # On A4 the recipient is in the address zone of DIN 5008 form B: 45
        # mm (the field) + 17.7 mm from the top of the sheet.
        "letter-a4": letter("The same business letter on A4 paper "
                            "(210 x 297 mm); the recipient is where the "
                            "window of a DL envelope shows it (DIN 5008).",
                            8268, 11693, 460, example=example),
    }

    for key in ENVELOPES:
        docs[key] = envelope(key, example=example)

    # The label sheets are blank in both copies: a template has no text.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import mklabels
    docs.update(mklabels.build())

    return docs


def folder_of(name):
    """The folder below the templates folder that holds the template name:
    envelopes, letters, or labels/avery/us and labels/avery/a4 for the label
    sheets of Avery (every label sheet of the table is an Avery sheet)."""
    if name.startswith("envelope-"):
        return "envelopes"

    if name.startswith("letter-"):
        return "letters"

    if name.startswith("label-us-"):
        return "labels/avery/us"

    if name.startswith("label-a4-"):
        return "labels/avery/a4"

    raise ValueError("no folder for the template " + name)


def relpath(name):
    """The path of the file of the template name below the templates folder,
    folders separated by a slash (labels/avery/us/label-us-5160.doc)."""
    return folder_of(name) + "/" + name + ".doc"


def main(argv):
    example = "--example" in argv
    args = [a for a in argv[1:] if a != "--example"]

    if len(args) != 1:
        sys.stderr.write("usage: mktemplates.py [--example] FOLDER\n")
        return 2

    os.makedirs(args[0], exist_ok=True)

    for name, d in build(example).items():
        folder = os.path.join(args[0], folder_of(name))

        os.makedirs(folder, exist_ok=True)

        with open(os.path.join(folder, name + ".doc"), "wb") as f:
            f.write(d.bytes())

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
