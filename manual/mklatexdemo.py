#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program writes manual/samples/latexdemo.doc, the document of the
# picture of a LaTeX marker on the screen (chapter 5).  The text is bytes of
# the code page, which the sources of the manual cannot hold for a marker of
# this kind, so the document is made here.
#
#     python3 -I manual/mklatexdemo.py manual/samples/latexdemo.doc

import sys

CRLF = b"\r\n"
EOL = b"\x1d"
HEADER = (b"*R3" + CRLF + b"MR1001" + CRLF + b"PP\\1\\\x00" + CRLF +
          b"P1C33" + CRLF + b"FHA1" + CRLF + b"FTT5" + CRLF +
          b"DP0,8500,11000" + CRLF)


def latex_marker(text):
    """A LaTeX marker (type 41) whose nested text is text (bytes)."""
    return b"\x1341(S" + CRLF + b"T" + text + EOL + b"\x00" + CRLF + b"\x13)"


def block(text, first=False):
    """One block of one column; text is bytes, lines end with EOL."""
    return (b"" if first else b"B" + CRLF) + b"T" + text + b"\x00" + CRLF


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: mklatexdemo.py FILE")
    rule = latex_marker(b"\\begin{center}\\rule{6cm}{0.8pt}\\end{center}")
    bold = latex_marker(b"\\textbf{here}")
    logo = latex_marker(b"\\TeX{}")
    blocks = [b"A LaTeX marker passes its text to the typesetter." + EOL,
              rule + EOL,
              b"The logo " + logo + b" and the bold word " + bold +
              b" are LaTeX markers shown as pictures." + EOL,
              b"The rule above is a block: it is shown as its label." + EOL]
    out = HEADER + b"*B0" + CRLF + b"S" + CRLF
    for i, b in enumerate(blocks):
        out += block(b, i == 0)
    out += b"*E" + CRLF
    with open(sys.argv[1], "wb") as f:
        f.write(out)


main()
