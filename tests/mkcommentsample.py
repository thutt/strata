#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The sample document with Comment markers, into the folder that is its
argument (the committed copy is in samples/comments/):

    python3 -I -B tests/mkcommentsample.py samples/comments

It is a short invented memo about a garden fair, with four comments by two
made-up people (Ada Example and Ben Example).  A Final print leaves the
comments out; a Draft print (the option --draft) prints each one in
brackets, with its author, its time and its text.  The output has nothing
that changes from one run to the next.  tests/printfix.py prints it both ways
and compares the two.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mktemplates as M

NAME = "sample-comments"
CRLF = b"\r\n"
EOL = b"\x1d"


def comment(author, when, text):
    """A Comment marker: its text, the author and the time."""
    return (b"\x136(S" + CRLF + b"T" + text + EOL + b"\x00" + CRLF +
            b"1" + author + b"\x00" + CRLF + b"2" + when + b"\x00" + CRLF +
            b"\x13)")


ADA = b"Ada Example"
BEN = b"Ben Example"

PARAGRAPHS = [
    b"Garden fair: plan for the day" + EOL,
    b"The fair opens at nine in the morning on the first Saturday of June, "
    b"in the meadow behind the Sample Road hall." +
    comment(ADA, b"2026-05-02 09:15", b"Check that the meadow is free "
            b"that day.") + b" Plants are sold at the long tables by the "
    b"gate." + EOL,
    b"Two people take the money at the gate and four help with the plants.  "
    b"Each helper has a badge and a list of the prices." +
    comment(BEN, b"2026-05-02 10:40", b"Four helpers is too few at "
            b"noon; ask two more.") + EOL,
    b"The cake stall is in the hall, and the tea is served from half past "
    b"ten.  " + comment(ADA, b"2026-05-03 08:05", b"Ask the baker for "
                       b"the list of cakes.") + comment(
        BEN, b"2026-05-03 08:30", b"Tea and cake: one price, as last "
        b"year.") + b"Children have a table of their own with seeds to "
    b"plant in small pots." + EOL,
    b"The fair closes at four.  The helpers put the tables away and the "
    b"meadow is cleaned before dark." + EOL,
]


def build():
    d = M.Doc("A sample memo with comments, for the Draft print", 0,
              8500, 11000, (1000, 1000, 1000, 1000), {})
    d.hf = []

    for p in PARAGRAPHS:
        d.block(None, p)

    return d


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: mkcommentsample.py FOLDER\n")
        return 2

    os.makedirs(argv[1], exist_ok=True)

    with open(os.path.join(argv[1], NAME + ".doc"), "wb") as f:
        f.write(build().bytes())

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
