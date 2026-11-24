#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program lists the pages of a printed document that end with a large
# white area: pages whose last line of text ends above a given part of the
# page height and that are not the last page of a chapter (the next page
# does not begin with a chapter or an appendix) and hold no picture.  It is
# a measure for the layout of the manual, not a check that fails.
#
#     python3 -I manual/whiteareas.py PDF [FRACTION]
#
# FRACTION is the part of the page height above which the last line of text
# must end for the page to be listed; the default is 0.4.  The header and
# the footer of a page are not text of the page.

import re
import subprocess
import sys

TOP = 60.0
BOTTOM = 725.0
CHAPTER = re.compile(r"^\s*(?:\d+\s|Appendix [A-F]:|Glossary|Index|Key "
                     r"Reference|Math Symbols)")


def body_bounds(pdf):
    """For every page: the heights (top, bottom) of its text between the
    header and the footer, and the first line of that text."""
    out = subprocess.run(["pdftotext", "-bbox", pdf, "-"], check=True,
                         capture_output=True).stdout.decode("utf-8", "replace")
    pages = []
    for block in out.split("<page ")[1:]:
        height = float(re.search(r'height="([\d.]+)"', block).group(1))
        words = [(float(a), float(b), w) for a, b, w in re.findall(
            r'yMin="([\d.]+)" xMax="[\d.]+" yMax="([\d.]+)">([^<]*)<',
            block)]
        body = [(y0, y1, w) for y0, y1, w in words
                if y0 > TOP and y1 < BOTTOM]
        first = " ".join(w for y0, y1, w in body[:8])
        pages.append((height, max([y1 for y0, y1, w in body], default=0.0),
                      first))
    return pages


def pages_with_pictures(pdf):
    """The numbers of the pages that hold a picture: a page that ends in a
    picture has no text below it and is not white."""
    out = subprocess.run(["pdfimages", "-list", pdf], check=True,
                         capture_output=True).stdout.decode("utf-8", "replace")
    found = set()
    for line in out.split("\n")[2:]:
        fields = line.split()
        if fields and fields[0].isdigit():
            found.add(int(fields[0]))
    return found


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    fraction = float(argv[2]) if len(argv) > 2 else 0.4
    pages = body_bounds(argv[1])
    pictures = pages_with_pictures(argv[1])
    listed = []
    for n in range(len(pages) - 1):
        height, last, first = pages[n]
        next_first = pages[n + 1][2]
        if 0.0 < last < fraction * height and (n + 1) not in pictures and \
                CHAPTER.match(next_first) is None:
            listed.append((n + 1, last))
    for n, last in listed:
        print("page %d ends at %.0f of %.0f points" % (n, last, pages[0][0]))
    print("whiteareas: %d of %d pages end above %.0f percent of the page"
          % (len(listed), len(pages), fraction * 100))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
