#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""Take the pictures of the document "How do your document preparation
systems fare?" from the real programs.

usage: python3 -I -B manual/challenge/shots.py XBIN MANUAL_DOC MANUAL_PDF OUT

XBIN is the build directory of the X11 version (it holds stred), MANUAL_DOC
and MANUAL_PDF are the Reference Manual as one document and the PDF that the
Printer made from it (manual/build.sh single), and OUT is the folder that
receives the pictures (the folder pictures of the challenge tree).  The
screens are taken from the Editor with the whole manual open; the pages are
pages of the PDF of the manual, drawn by pdftoppm, and a page with numbered
lines is printed from a small document by the Printer.  The programs xwd,
Xvfb, xdotool, pdftoppm, pdftotext and pdfinfo are needed.

The pictures are sources: they are kept in git and are made again only on
purpose.  The file pages.txt of OUT says, for every page picture, the
headline that it shows and the page of the PDF, so that the numbers can be
checked.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))

import shots  # noqa: E402


def screen(xbin, doc, name, keys, crop, out, cwd):
    """One picture of the Editor with the document open."""
    d = shots.Display()
    try:
        d.start([os.path.join(xbin, "stred"), doc], cwd)
        d.keys(keys)
        d.shot(os.path.join(out, name + ".png"), crop)
    finally:
        d.close()


def page_of(pdf, words, first=60):
    """The number of the first page of the PDF that holds all the words, from
    the page FIRST on (the default is past the contents)."""
    count = int(re.search(r"Pages:\s+(\d+)", subprocess.run(
        ["pdfinfo", pdf], capture_output=True, text=True).stdout).group(1))
    for page in range(first, count + 1):
        text = subprocess.run(["pdftotext", "-f", str(page), "-l", str(page),
                               pdf, "-"], capture_output=True,
                              text=True).stdout
        if all(w in text for w in words):
            return page

    return 0


def draw(pdf, page, name, out, resolution="80"):
    """The page of the PDF as a PNG picture."""
    base = os.path.join(out, name)
    subprocess.run(["pdftoppm", "-f", str(page), "-l", str(page), "-r",
                    resolution, "-png", "-singlefile", pdf, base],
                   check=True)


def numbered(xbin, out, work):
    """A page with numbered lines: a sample letter printed by the Printer
    with the numbers of the pagewise mode (the record FZM1), and its first
    page drawn."""
    root = os.path.join(HERE, "..", "..")
    data = os.path.join(root, "samples", "letters")
    with open(os.path.join(data, "complaint-to-utility-us.doc"), "rb") as f:
        raw = f.read()
    at = re.search(rb"MR[0-9]+\r\n", raw).end()
    path = os.path.join(work, "numbered.doc")
    with open(path, "wb") as f:
        f.write(raw[:at] + b"FZM1\r\n" + raw[at:])

    pdf = os.path.join(work, "numbered.pdf")
    subprocess.run([os.path.join(xbin, "strprint"), path, "--docdir", data,
                    "-o", pdf], check=True)
    draw(pdf, 1, "ch-numbered", out)


def main(argv):
    if len(argv) != 5:
        sys.stderr.write(__doc__)

        return 2

    xbin, doc, pdf, out = argv[1:]
    os.makedirs(out, exist_ok=True)
    work = tempfile.mkdtemp(prefix="strata-challenge")
    shutil.copy(doc, os.path.join(work, "manual.doc"))
    manual = os.path.join(work, "manual.doc")
    screen(xbin, manual, "ch-open", "wait:3 alt+F7 s text:9.3 Return wait:3",
           (0, 0, 800, 520), out, work)
    screen(xbin, manual, "ch-outline",
           "wait:3 alt+F7 s text:6 Return wait:2 alt+1 wait:3",
           (0, 0, 800, 700), out, work)
    log = []
    for name, words, first in (
            ("ch-equation", ["quadratic formula", "cube root"],
             60),
            ("ch-table", ["When printed", "Saving and loading"],
             60),
            ("ch-index", ["Page Number Level", "Pagination"],
             690)):
        page = page_of(pdf, words, first)
        log.append("%s | page %d of the PDF | words: %s\n"
                   % (name, page, " ".join(words)))
        if page != 0:
            draw(pdf, page, name, out)

    numbered(xbin, out, work)
    log.append("ch-numbered | page 1 of a sample letter, record FZM1\n")
    errors = os.path.join(HERE, "..", "doc", "pictures", "markers-errors.png")
    shutil.copy(errors, os.path.join(out, "ch-errors.png"))
    log.append("ch-errors | a copy of doc/pictures/markers-errors.png\n")
    with open(os.path.join(out, "pages.txt"), "w") as f:
        f.writelines(log)

    shutil.rmtree(work, ignore_errors=True)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
