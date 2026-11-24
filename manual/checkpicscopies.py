#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program looks for text that the sample documents of the manual share
# with the material of the original product.  The pictures of the manual show
# the sample documents in manual/samples/src, so a run of words that is in a
# sample document and in the original manuals or the original sample
# documents is a candidate for a picture whose content copies the original.
#
#     python3 -I manual/checkpicscopies.py ORIGINAL_DIR [N]
#
# ORIGINAL_DIR holds the original material: the text of the original manuals
# (the files *.txt that pdftotext made of the PDF files) and, in a folder
# named sample, the sample documents of the original product.  N is the
# number of words of a run (5 by default).  The names that identify the
# original sample documents (the park, the city and the firm of its text)
# are looked for in the sources of the manual too.  The exit status is 1
# when a run or a name is found.  The format records of the document files
# (letters and digits such as fsh150) are not text and are left out.

import glob
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NAMES = ("centennial", "central city", "woodward", "bonner", "simons")


def words(text):
    """The words of a text, without the format records of document files."""
    found = re.findall(r"[a-z0-9][a-z0-9'.-]*", text.lower())
    return [w for w in found if re.match(r"^[a-z]{1,4}[0-9]+$", w) is None and
            re.match(r"^[0-9]+$", w) is None]


def runs(ws, n):
    """The set of the runs of n words of a list of words."""
    return {" ".join(ws[i:i + n]) for i in range(len(ws) - n + 1)}


def original_runs(folder, n):
    """The runs of the original material, by source name."""
    found = {}
    for path in sorted(glob.glob(os.path.join(folder, "*.txt"))):
        with open(path, errors="replace") as f:
            found[os.path.basename(path)] = runs(words(f.read()), n)
    for path in sorted(glob.glob(os.path.join(folder, "sample", "*"))):
        text = subprocess.run(["strings", "-n", "4", path], text=True,
                              capture_output=True).stdout
        found["sample/" + os.path.basename(path)] = runs(words(text), n)
    return found


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__.split("\n\n")[1] + "\n")
        return 2
    n = int(argv[2]) if len(argv) > 2 else 5
    original = original_runs(argv[1], n)
    hits = 0
    for path in sorted(glob.glob(os.path.join(HERE, "samples", "src",
                                              "*.src"))):
        with open(path, errors="replace") as f:
            text = f.read()
        mine = runs(words(text), n)
        for name, theirs in original.items():
            for run in sorted(mine & theirs):
                hits += 1
                print("%s: %d words in common with %s: %s" %
                      (os.path.basename(path), n, name, run))
        low = text.lower()
        for name in NAMES:
            if name in low:
                hits += 1
                print("%s: names %s" % (os.path.basename(path), name))
    print("checkpicscopies: %d hit(s) in %d original source(s)" %
          (hits, len(original)))
    return 1 if hits > 0 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
