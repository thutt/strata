#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program writes the test documents of the print-pdflatex task into the
# directory that is its argument: print_orient.doc, print_whitespace.doc,
# print_keycap.doc and print_chars.doc.  The documents are plain Manuscript
# files (docs/DOC-FORMAT.txt), so the files in tests/data can be checked in
# and this program only shows how they were made.
#
#     python3 -I tests/mkprintdata.py tests/data

import sys

CRLF = b"\r\n"
EOL = b"\x1d"

# The common header of a document, with the fonts that a test may name.
HEADER = (b"*R3" + CRLF + b"MR1001" + CRLF + b"PP\\1\\\x00" + CRLF +
          b"P1C33" + CRLF + b"FHA1" + CRLF + b"FTT5" + CRLF +
          b"DP0,8500,11000" + CRLF)


def marker(mtype, records):
    """A marker with its records, as text bytes."""
    body = b"".join(r + CRLF for r in records)
    return b"\x13" + str(mtype).encode() + b"(" + body + b"\x13)"


def block(text, first=False):
    """One block of one column; text is bytes, lines end with EOL."""
    return (b"" if first else b"B" + CRLF) + b"T" + text + b"\x00" + CRLF


def document(blocks, header=HEADER):
    """A whole document of unstructured blocks."""
    out = header + b"*B0" + CRLF + b"S" + CRLF
    for i, b in enumerate(blocks):
        out += block(b, i == 0)
    return out + b"*E" + CRLF


FILLER = (b"The quick brown fox jumps over the lazy dog, and then it "
          b"jumps back again to see whether the dog has moved. ") * 12


def orient():
    land = marker(36, [b"12"])
    port = marker(36, [b"11"])
    blocks = [b"PORTRAIT ONE. " + FILLER + EOL,
              land + EOL,
              b"LANDSCAPE ONE. " + FILLER * 3 + EOL,
              b"Second landscape block. " + FILLER * 2 + EOL,
              port + EOL,
              b"PORTRAIT TWO. " + FILLER + EOL,
              b"A block with a marker in the middle: before it " + FILLER +
              land + b" and after it, LANDSCAPE TWO. " + FILLER * 2 + EOL,
              b"This landscape page is open at the end of the document." +
              EOL]
    return document(blocks)


def whitespace():
    units = [b"in", b"cm", b"mm", b"pt", b"bp", b"pc", b"dd", b"cc", b"sp",
             b"em", b"ex"]
    blocks = [b"Whitespace of 1.500 in the default unit follows." + EOL]
    blocks.append(marker(31, [b"11500"]) + b"Line after 1.500 in." + EOL)
    for i, u in enumerate(units):
        if i == 0:
            continue
        size = b"4000000" if u == b"sp" else b"1500"
        recs = [b"1" + size, b"U" + str(i).encode()]
        blocks.append(b"Before " + u + b"." + EOL)
        blocks.append(marker(31, recs) + b"After 1.500 " + u + b"." + EOL)
    blocks.append(marker(31, [b"110"]) + b"After 0.010 in." + EOL)
    blocks.append(marker(31, [b"110", b"U3"]) + b"After 0.010 pt." + EOL)
    blocks.append(marker(31, [b"11250", b"U9"]) + b"Whitespace of 1.25 em" +
                  b" at the top of a page, then the end." + EOL)
    blocks.append(marker(31, [b"10"]) + b"A size of 0 prints nothing." + EOL)
    return document(blocks)


def keycap():
    # The fonts 4 to 8 are given names that tell their family.
    names = {4: b"Courier mono", 5: b"Helvetica sans", 6: b"Times",
             7: b"Program listing", 8: b"Arial Black"}
    header = HEADER
    for n in sorted(names):
        header += b"DF" + str(n).encode() + b"N" + names[n] + CRLF
    lines = []
    for f in [1, 2, 3, 4, 5, 6, 7, 8, 9, 20]:
        recs = [b"1Key" + str(f).encode() + b"\x00"]
        if f != 2:
            recs.append(b"5" + str(f).encode())
        lines.append(b"Font " + str(f).encode() + b": " +
                     marker(37, recs) + b" and a gray one: " +
                     marker(37, [b"1Esc\x00", b"2300", b"3800",
                                 b"4750", b"5" + str(f).encode()]) + EOL)
    return document([b"".join(lines)], header)


def hyphens():
    # Keycaps and text with the pairs of characters that the fonts of TeX
    # join into one ligature: two hyphens (an en dash), three (an em dash),
    # two backticks, two apostrophes, two commas, two less-than and two
    # greater-than signs, an exclamation mark or a question mark and a
    # backtick, and the f ligatures.  Each must be printed as typed.
    def cap(text):
        return marker(37, [b"1" + text + b"\x00"])

    keys = [b"Alt--", b"Alt-+", b"Ctrl--", b"Shift--", b"Off", b"Fill"]
    line1 = b"Keys:"
    for k in keys:
        line1 += b" " + cap(k)
    line1 += EOL
    line2 = b"Text: a -- b --- c and x-y." + EOL
    line3 = b"Quotes: ``a'' ,, << >> !` ?` ok." + EOL
    line4 = b"Words: office fish waffle." + EOL
    return document([line1 + line2 + line3 + line4])


def nbsp():
    # Paragraphs with a no-break space between two words (the character 1E,
    # and the byte FF of code page 437) at every place that a line can end,
    # and the same with a plain space as the control.  Each paragraph is
    # a run of words, a word of k letters that moves the pair, and the pair.
    blocks = [b"Tied: Mr." + b"\x1e" + b"Smith, 10" + b"\xff" + b"km." + EOL]
    for k in range(80):
        lead = b"word " * 30 + b"w" * (k + 1) + b" "
        for a, sep, b in ((b"alpha", b"\x1e", b"beta"),
                          (b"iota", b"\xff", b"kappa"),
                          (b"gamma", b" ", b"delta")):
            blocks.append(lead + a + sep + b + b" and more words follow." +
                          EOL)
    return document(blocks)


def chars():
    skip = {0x00, 0x0A, 0x0D, 0x13, 0x1A, 0x1D, 0x1E}
    lines = []
    for row in range(0, 256, 16):
        text = b"".join(bytes([c]) for c in range(row, row + 16)
                        if c not in skip)
        lines.append(("%02X: " % row).encode() + text + EOL)
    # A placeholder character that is used twice is named once.
    lines.append(b"Twice: \xf4 and \xf4 and \xf4." + EOL)
    return document([b"".join(lines), b"Ordinary text # $ % & _ { } ~ ^ \\ "
                     b"[ ]." + EOL])


def panels():
    # A landscape page with a block of three columns, a footnote that is
    # followed by a space, and a picture in a directory below the document.
    land = marker(36, [b"12"])
    cols = b""
    for i, w in enumerate((20, 20, 0)):
        if w != 0:
            cols += b"CW" + str(w).encode() + CRLF
        cols += (b"T" + b"Panel " + str(i + 1).encode() + EOL + b"\x00" +
                 CRLF)
    note = (b"S" + CRLF + b"T" + b"The note text." + EOL + b"\x00" + CRLF)
    text = (b"Before" + marker(1, [note.rstrip(CRLF)]) + b" after the note." +
            EOL)
    pic = marker(25, [b"1pics/p.png\x00", b"22000"])
    out = HEADER + b"*B0" + CRLF + b"S" + CRLF
    out += block(land + b"Panels" + EOL, True)
    out += b"BM15" + CRLF + cols
    out += block(text)
    out += block(pic + EOL)
    return out + b"*E" + CRLF


def lastcol():
    """Tables whose last column has no width: the Printer works out the width
    of that column from the room that is left on the line.  The first page
    has a paragraph, which shows the right margin, and each of the next pages
    holds one table of 2, 3 or 4 columns.  The first word of every cell is
    T<columns><letter of the column>, then the cell is filled with words so
    that its lines reach the right edge of the column."""
    fill = b"ab cd ef gh ij kl mn op qr st uv wx yz " * 14
    para = (b"The quick brown fox jumps over the lazy dog, and then it jumps "
            b"back again to see whether the dog has moved. ") * 8
    pagebreak = marker(23, [])
    out = HEADER + b"*B0" + CRLF + b"S" + CRLF
    out += block(b"PARAGRAPH " + para + EOL, True)
    for widths in ((30, 0), (20, 20, 0), (10, 20, 20, 0)):
        n = len(widths)
        out += block(pagebreak + EOL)
        out += b"BM15" + CRLF
        for i, w in enumerate(widths):
            if w != 0:
                out += b"CW" + str(w).encode() + CRLF
            cell = ("T%d%s " % (n, chr(65 + i))).encode() + fill
            out += b"T" + cell + EOL + b"\x00" + CRLF
    return out + b"*E" + CRLF


def badpic():
    # A picture that looks like a PDF to the writer but that nothing can read:
    # the first run fails at it and strprint runs again without it.
    pic = marker(25, [b"1pics/bad.pdf\x00", b"22000"])
    return document([b"Before the picture." + EOL, pic + EOL,
                     b"After the picture." + EOL])


def equation(text):
    """An equation marker whose nested text is text (bytes)."""
    return b"\x139(S" + CRLF + b"T" + text + EOL + b"\x00" + CRLF + b"\x13)"


def latex_marker(text):
    """A LaTeX marker (type 41) whose nested text is text (bytes); the
    lines of the text are separated by a line feed in text."""
    body = EOL.join(text.split(b"\n")) + EOL
    return b"\x1341(S" + CRLF + b"T" + body + b"\x00" + CRLF + b"\x13)"


def latex():
    # LaTeX markers: a rule in a block of its own, one inside a sentence,
    # a table on several lines, a marker with a code page 437 byte above
    # 7F, and the special characters of TeX.
    rule = latex_marker(b"\\begin{center}\\rule{6cm}{0.8pt}\\end{center}")
    inline = latex_marker(b"\\textbf{raw bold} \\& 100\\% of it")
    table = latex_marker(b"\\begin{tabular}{|l|r|}\\hline\n"
                         b"A\\ B & 1 \\\\ \\hline\n"
                         b"C & 2 \\\\ \\hline\n"
                         b"\\end{tabular}")
    high = latex_marker(b"caf\x82 and \\textsc{small caps}")
    blocks = [b"Before the markers." + EOL,
              rule + EOL,
              b"A sentence with " + inline + b" in the middle." + EOL,
              table + EOL,
              b"The text " + high + b" ends the document." + EOL]
    return document(blocks)


def latex_error():
    # A LaTeX marker with a macro that does not exist, in the second line
    # of the second block, for the Error Log.
    bad = latex_marker(b"\\strataundefinedmacro{x}")
    return document([b"First block." + EOL,
                     b"Second block, line one." + EOL + b"Line two: " + bad +
                     b" here." + EOL,
                     b"Third block." + EOL])


def latex_security():
    # LaTeX markers that try to run a program and to write a file outside
    # the directory of the print: neither may succeed.
    run = latex_marker(b"\\immediate\\write18{touch strata-write18-done}")
    out = latex_marker(b"\\newwrite\\sw\\immediate\\openout\\sw="
                       b"../strata-openout-done\n"
                       b"\\immediate\\write\\sw{x}")
    return document([b"Security test." + EOL, run + EOL, out + EOL,
                     b"After." + EOL])


def errors():
    # A structured document with several kinds of defect: an equation in the
    # old equation language, a missing picture, a character that no font has
    # and an equation that LaTeX rejects.  The positions are used by
    # tests/errlog.py.
    header = (b"*R3" + CRLF + b"MR1001" + CRLF + b"PP\\1\\\x00" + CRLF +
              b"P1HB0" + CRLF + b"P1FB0" + CRLF + b"P1C33" + CRLF +
              b"FHA1" + CRLF + b"FTT5" + CRLF + b"DP0,8500,11000" + CRLF +
              b"YCL0" + CRLF + b"YIWN2" + CRLF)

    def blk(text):
        return b"T" + text + EOL + b"\x00" + CRLF

    body = header + b"*B1" + CRLF + b"S" + CRLF + blk(b"Error test")
    body += b"S" + CRLF
    body += b"+" + CRLF + b"SE0" + CRLF + blk(b"Purpose")
    body += blk(b"A clean block of text.")
    body += blk(b"First line is fine." + EOL + b"A bad equation: " +
                equation(b"left #R[x] => #^(a = 0)") + b" in the text.")
    body += b"SE2" + CRLF + blk(b"Definition")
    body += blk(b"Before the picture.")
    body += blk(marker(25, [b"1pics/missing.pcx\x00", b"22000"]))
    body += blk(b"A character no font has: x\xf4y and an equation LaTeX "
                b"refuses: " + equation(b"a + \\notamacro b") + b".")
    body += b"-" + CRLF
    return body + b"*E" + CRLF


def lineno():
    # A structured document for the tests of line numbering: two sections
    # with headings, long paragraphs that run over pages, blank blocks, a
    # block of two columns (a table) and a short block.  The document has no
    # setting for line numbering: tests/lineno.py adds the records.
    header = (b"*R3" + CRLF + b"MR1001" + CRLF + b"PP\\1\\\x00" + CRLF +
              b"P1HB0" + CRLF + b"P1FB0" + CRLF + b"P1C33" + CRLF +
              b"FHA1" + CRLF + b"FTT5" + CRLF + b"DP0,8500,11000" + CRLF +
              b"YCL0" + CRLF + b"YIWN2" + CRLF)

    def blk(text):
        return b"T" + text + EOL + b"\x00" + CRLF

    cols = b""

    for i, w in enumerate((20, 0)):
        if w != 0:
            cols += b"CW" + str(w).encode() + CRLF

        cols += (b"T" + b"Column " + str(i + 1).encode() + b" of the table" +
                 EOL + b"\x00" + CRLF)

    body = header + b"*B1" + CRLF + b"S" + CRLF + blk(b"Line numbering")
    body += b"S" + CRLF
    body += b"+" + CRLF + b"SE0" + CRLF + blk(b"First section")
    body += blk(b"FIRST PARAGRAPH. " + FILLER * 3)
    body += blk(EOL)
    body += blk(b"SECOND PARAGRAPH. " + FILLER * 2)
    body += b"BM15" + CRLF + cols
    body += blk(b"A short block.")
    body += b"SE2" + CRLF + blk(b"Second section")
    body += blk(b"THIRD PARAGRAPH. " + FILLER * 4)
    body += blk(b"LAST PARAGRAPH. " + FILLER)
    body += b"-" + CRLF
    return body + b"*E" + CRLF


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: mkprintdata.py DIRECTORY")
    out = sys.argv[1]
    for name, data in (("print_orient", orient()),
                       ("print_whitespace", whitespace()),
                       ("print_keycap", keycap()),
                       ("print_hyphens", hyphens()),
                       ("print_nbsp", nbsp()),
                       ("print_chars", chars()),
                       ("print_panels", panels()),
                       ("print_lastcol", lastcol()),
                       ("print_badpic", badpic()),
                       ("print_errors", errors()),
                       ("print_latex", latex()),
                       ("print_latexerr", latex_error()),
                       ("print_latexsec", latex_security()),
                       ("print_lineno", lineno())):
        with open("%s/%s.doc" % (out, name), "wb") as f:
            f.write(data)


main()
