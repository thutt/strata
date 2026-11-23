#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program fails when a document that the generator made has a LaTeX
# marker (type 41) that holds vertical space: \vspace, \vspace*, \vfill,
# \vfil, \vss, \vskip, \bigskip, \medskip, \smallskip or \addvspace.  A
# LaTeX marker is for a piece of text of a line.  Vertical space belongs in
# a Whitespace marker (type 31), which has its own size and unit; the title
# page of the Reference Manual and of the story sample have the space
# around the large logo in Whitespace markers, and the LaTeX marker holds
# only \stratalogo[3].
#
#     python3 -I manual/checklatexspace.py [FILE.doc ...]
#
# With no argument every .doc file of the directory doc and the story sample
# is checked, except the cards (cheat, cheat-short, mathcheat, terminal-keys):
# a card is a page for the printer, laid out by its own generator with
# \vspace between its parts, and is not a document that is read in the
# Editor.  The exit status is 1 when a marker is left.

import glob
import os
import re
import sys

SPACE = re.compile(rb"\\(?:vspace\*?|vfill|vfil|vss|vskip|bigskip|medskip|"
                   rb"smallskip|addvspace)(?![A-Za-z])")
MARKER = re.compile(rb"\x1341\(\r?\n?(?:[^\x13]*?)\x13\)", re.S)
TEXT = re.compile(rb"\x1341\(([^\x13]*)\x13\)")


def space_hits(data):
    """The texts of the LaTeX markers of a document that hold vertical
    space."""
    hits = []
    for m in TEXT.finditer(data):
        if SPACE.search(m.group(1)) is not None:
            hits.append(m.group(1).replace(b"\r\n", b" ").strip())
    return hits


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    cards = ("cheat.doc", "cheat-short.doc", "mathcheat.doc",
             "terminal-keys.doc")
    files = argv[1:] or sorted(
        f for f in glob.glob(os.path.join(here, "doc", "*.doc")) +
        glob.glob(os.path.join(here, "..", "samples", "story", "*.doc"))
        if os.path.basename(f) not in cards)
    total = 0
    for path in files:
        with open(path, "rb") as f:
            data = f.read()
        for text in space_hits(data)[:20]:
            total += 1
            print("%s: LaTeX marker with vertical space: %s" % (
                os.path.basename(path), text.decode("ascii", "replace")))
    print("checklatexspace: %d marker(s) with vertical space in %d file(s)"
          % (total, len(files)))
    return 1 if total > 0 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
