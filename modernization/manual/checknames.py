#!/usr/bin/env python3
# Copyright (c) 2026 Logic Magicians Software
#
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This program fails when the sources of the reference manual or the key
# leaflet name the product that Strata replaces, describe it ("the
# original", "the old program"), or name one of the programs of Strata
# (the manual says the Editor, the Document Manager, the Printer and the
# Viewer).  It also fails when text that the RELEASE edition prints tells
# how to use a program by its command line: the options of the Editor, the
# Printer and the Viewer, the environment variables that name another
# program, that are for development or that only a site or a developer sets
# (the variables of the font and the size of the X11 window, of the address
# of the font archives and of the print commands), and the modes of the
# Editor that have no screen.  The name of the help file of the earlier
# product is not allowed anywhere.  Strata is started with the Document
# Manager, and the other programs are not a supported way to start it.
# Such text may be in the
# sources only between ".if .STRATA_BUILD_TYPE # release" and its ".else"
# or ".endif": the release leaves it out, and the developer edition has it.
#
#     python3 -I manual/checknames.py [FILE.src ...]
#     python3 -I manual/checknames.py --pdf RELEASE.pdf
#
# With --pdf the text of the PDF of the release edition is checked with the
# same patterns (the text for developers must not be in it).
#
# With no argument every .src file next to this program is checked.  The
# file checknames.allow, next to this program, holds the allowed phrases:
# one phrase per line, matched without regard to case; a line that begins
# with a number sign is a comment.  A phrase is removed from a line before
# the line is checked.  Labels ({x:name}, [name]) and the file names of
# pictures are not text and are not checked.  The exit status is 1 when a
# hit is left.

import glob
import os
import re
import sys

FORBIDDEN = re.compile(
    r"\b(lotus|manuscripts?|originals?|ms-?dos|dosbox|mshelp"
    r"|old(er)?\s+(program|product|equation|utility|utilities)"
    r"|earlier\s+(program|product)"
    r"|previous\s+(program|product)"
    r"|predecessor|legacy\s+(program|product)"
    r"|str|stred|strprint|dviview)\b",
    re.IGNORECASE)


# The command line of the programs that are not a supported way to start
# Strata.  The options -h and --usage belong to the Document Manager and are
# not listed.
SUBPROGRAM = re.compile(
    r"(--(text|print|preview|spell|auto|no-error-log|no-banner|banner"
    r"|tex-only|dvi|pdf-via-dvi|keep|draft|double-spaced|long-list|docdir"
    r"|unstructured|description|author|autosave|autosave-seconds|printer"
    r"|copies|sides|from-page|to-page|no-title|progress|var|special"
    r"|export-latex|check-latex|latex-markers|no-latex-markers|report)\b"
    r"|(?-i:(?<![A-Za-z0-9])(-o\s+OUT|-p\s+PAGE|-d\s+DPI)\b)"
    r"|(?-i:(?<![A-Za-z0-9-])-u(?![A-Za-z0-9-]))"
    r"|\bSTRATA_(STRPRINT|DVIVIEW|X11_XFT|X11_SIZE|X11_FONT"
    r"|X11_GEOM|X11_DUMP|CTAN_URL|LP|LPSTAT)\b|\bSOURCE_DATE_EPOCH\b"
    r"|(?-i:\bSTRED\b)|\b(DVIVIEW_PLAIN|DVIVIEW_TIMING)\b"
    r"|\busage: <(Editor|Printer|Viewer)>"
    r"|\b(print|preview|spell) mode\b"
    r"|\btext mode\b)",
    re.IGNORECASE)

DEVELOPER_IF = ".if .STRATA_BUILD_TYPE # release"


def allowed_phrases(here):
    """Read the allowed phrases of checknames.allow."""
    phrases = []
    path = os.path.join(here, "checknames.allow")
    if os.path.exists(path):
        with open(path, encoding="ascii") as f:
            for line in f:
                line = line.rstrip("\n")
                if line.strip() != "" and not line.startswith("#"):
                    phrases.append(re.compile(re.escape(line.strip()),
                                              re.IGNORECASE))
    return phrases


def text_of(line):
    """Return the part of a source line that is text a reader sees."""
    if line.startswith(".pic "):
        parts = line.split(None, 3)
        line = parts[3] if len(parts) > 3 else ""
    elif line.startswith("."):
        parts = line.split(None, 1)
        line = parts[1] if len(parts) > 1 else ""
    line = re.sub(r"\{[xXnc]:[^}]*\}", " ", line)
    line = re.sub(r"\s*\[[A-Za-z0-9_.-]+\]\s*$", "", line)
    return line


def code_text_of(line):
    """The text of a source line with the words in {c:...} kept: a command
    line option is in the program font and is still text that is read."""
    if line.startswith(".pic "):
        parts = line.split(None, 3)
        line = parts[3] if len(parts) > 3 else ""
    elif line.startswith("."):
        parts = line.split(None, 1)
        line = parts[1] if len(parts) > 1 else ""
    line = re.sub(r"\{[xXn]:[^}]*\}", " ", line)
    line = re.sub(r"\{c:([^}]*)\}", r" \1 ", line)
    line = re.sub(r"\s*\[[A-Za-z0-9_.-]+\]\s*$", "", line)
    return line


def check(path, phrases):
    """Return the list of (line number, word, line) hits of one file."""
    hits = []
    stack = []
    with open(path, encoding="ascii", errors="replace") as f:
        for num, line in enumerate(f, 1):
            line = line.rstrip("\n")
            # The branch of a developer condition that the release leaves
            # out may describe the command lines of the programs.
            if line.startswith(".if "):
                stack.append([line.strip() == DEVELOPER_IF, False])
                continue
            if line.startswith(".else"):
                if stack:
                    stack[-1][1] = True
                continue
            if line.startswith(".endif"):
                if stack:
                    stack.pop()
                continue
            for_developers = any(dev and not in_else
                                 for dev, in_else in stack)
            text = text_of(line)
            for p in phrases:
                text = p.sub(" ", text)
            for m in FORBIDDEN.finditer(text):
                hits.append((num, m.group(0), line))
            if not for_developers and not skip_subprogram(path):
                code = code_text_of(line)
                for p in phrases:
                    code = p.sub(" ", code)
                for m in SUBPROGRAM.finditer(code):
                    hits.append((num, m.group(0), line))
    return hits


def skip_subprogram(path):
    """The document of the file formats is for developers and is not part
    of what a user reads."""
    return os.path.basename(path).startswith("strata-file-format")


def check_pdf(path, phrases):
    """Return the hits of the text of the PDF of the release edition."""
    import subprocess
    out = subprocess.run(["pdftotext", "-enc", "UTF-8", path, "-"],
                         capture_output=True, text=True, check=True).stdout
    hits = []
    # A phrase may be broken at the end of a line of the PDF.
    flat = re.sub(r"\s+", " ", out)
    for p in phrases:
        flat = p.sub(" ", flat)
    for m in SUBPROGRAM.finditer(flat):
        hits.append((flat.count(" ", 0, m.start()), m.group(0),
                     flat[max(0, m.start() - 40):m.end() + 40]))
    return hits


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    if len(argv) == 3 and argv[1] == "--pdf":
        hits = check_pdf(argv[2], allowed_phrases(here))
        for num, word, line in hits[:40]:
            print("%s:%d: [%s] %s" % (os.path.basename(argv[2]), num, word,
                                      line.strip()[:100]))
        print("checknames: %d command line word(s) in the release PDF"
              % len(hits))
        return 1 if hits else 0
    files = argv[1:]
    if len(files) == 0:
        files = sorted(glob.glob(os.path.join(here, "src", "*.src")))
    phrases = allowed_phrases(here)
    total = 0
    for path in files:
        for num, word, line in check(path, phrases):
            total += 1
            print("%s:%d: [%s] %s" % (os.path.basename(path), num, word,
                                      line.strip()[:100]))
    print("checknames: %d forbidden word(s) in %d file(s)"
          % (total, len(files)))
    return 1 if total > 0 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
