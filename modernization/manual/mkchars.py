#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program writes manual/samples/chars.doc, a Strata document that
# holds the whole character set of the document file (the codes 1 to 255 of
# the code page 437), one row of sixteen codes in each line, for the
# pictures of appendix C.  The text is bytes of the code page, which the
# sources of the manual cannot hold, so the document is made here with the
# classes of mkmanual.py.
#
#     python3 -I manual/mkchars.py manual/samples/chars.doc
#
# The codes 1 to 31 are written with the escape 01, as the format has it for
# the control range, so that they show as the glyphs of the screen.

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import mkmanual  # noqa: E402


def row(first):
    """The bytes of one line: the code in hex and sixteen characters."""
    out = bytearray(("%02X  " % first).encode("ascii"))
    for code in range(first, first + 16):
        if code == 0:
            out += b" "
        elif code < 0x20:
            out += b"\x01" + bytes([code])
        else:
            out += bytes([code])
        out += b" "
    return bytes(out)


def main(argv):
    """Write the document named on the command line."""
    if len(argv) != 2:
        sys.stderr.write("usage: mkchars.py OUTPUT.doc\n")
        return 2
    b = mkmanual.Builder()
    b.desc = "The character set of the document file"
    b.section(1, "Character set", None)
    font = mkmanual.marker(15, [b"1%d" % mkmanual.PROGRAM_FONT])
    lines = [row(first) for first in range(0, 256, 16)]
    data = font + mkmanual.EOL.join(lines) + mkmanual.marker(15, [])
    b.add([b"BFJ0"], data)
    b.write(argv[1])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
