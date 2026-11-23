#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The sample sheet of address labels, into the folder that is its argument
(the committed copy is in samples/labels/):

    python3 -I -B tests/mklabelsample.py samples/labels

It is the document that New makes from the template label-us-5160 with the
addresses of made-up people filled in: the names are first names with the
surname Example, the streets are Example streets, the towns are Sampleton and
Exampleville, and the state and the ZIP code are not real (ST, 00000).  The
output has no date and nothing else that changes from one run to the next.
tests/labels.py checks the sample again.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import labeldata as D
import mklabels as L
import mktemplates as M

NUM = "5160"
NAME = "sample-label-us-5160"
FIRST = ["Ada", "Ben", "Cora", "Dan", "Eva", "Finn", "Gwen", "Hal", "Iris",
         "Jon", "Kay", "Leo", "Mia", "Ned", "Ona", "Pat", "Quin", "Ray",
         "Sue", "Tom", "Uma", "Vic", "Wes", "Xena", "Yan", "Zoe", "Abe",
         "Bea", "Cy", "Dee"]
STREETS = ["Example Street", "Sample Road", "Test Avenue", "Muster Lane",
           "Example Court", "Sample Way"]
TOWNS = ["Sampleton", "Exampleville"]


def lines_of(n):
    """The three lines of the address on sticker n."""
    return ["%s Example" % FIRST[n % len(FIRST)],
            "%d %s" % (100 + 10 * n, STREETS[n % len(STREETS)]),
            "%s, ST 00%03d" % (TOWNS[n % len(TOWNS)], n)]


def build():
    sheet = [s for s in D.SHEETS if s["num"] == NUM][0]
    d = L.template(NUM, sheet)
    k = L.layout(sheet)["k"]
    d.desc = "A sample sheet of address labels for the Avery 5160"
    d.blocks = [(f, M.text(*([l.encode() for l in lines_of(n)] +
                              [b""] * (k - 3))), e)
                for n, (f, t, e) in enumerate(d.blocks)]

    return d


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: mklabelsample.py FOLDER\n")
        return 2

    os.makedirs(argv[1], exist_ok=True)

    with open(os.path.join(argv[1], NAME + ".doc"), "wb") as f:
        f.write(build().bytes())

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
