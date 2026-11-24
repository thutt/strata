#!/usr/bin/env python3
# Copyright (c) 2026 Logic Magicians Software
#
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This program lists the lines of the sources of the manual that may
# describe how Strata is built instead of what it does: words that belong
# to the code (byte, record, struct, function, buffer, pointer, ...),
# hexadecimal codes, paths, source and document file names of the
# project, "src/" and "docs/".  The manual describes what the user sees
# and does; a sentence about how the program does it is deleted.
#
#     python3 -I manual/checkimpl.py [FILE.src ...]
#
# With no argument every .src file next to this program is checked.  The
# file checkimpl.allow, next to this program, holds the allowed phrases:
# one phrase per line, matched without regard to case ("function key" is
# one); a line that begins with a number sign is a comment.  A phrase is
# removed from a line before the line is checked.  A line "skip: NAME.src"
# in that file leaves a whole source out of the check (the appendix that
# describes the file format is one, until the owner decides where it
# belongs).  Labels and the file names of pictures are not checked.  Every
# line that has a hit is listed under the name of its source; the exit status
# is 1 when a hit is left.
#
# Text between ".if .STRATA_BUILD_TYPE # release" and its ".else" or
# ".endif" is for developers: the release edition leaves it out, so this
# check does not look at it.  Besides the words above the check looks for
# the kinds of sentence that tell how a feature is built: a file type that
# only the programs use (the DVI file), the modes of files and directories,
# the commands that the writer of the typeset file uses, a note that one
# program leaves for another, and the order in which the programs read
# their files, the way a program reads or writes a whole file, the place of
# the cache, and a thing that is "made by LaTeX" instead of what it is for.

import glob
import os
import re
import sys

# The word "variable" is not in the list: the variables of the If markers
# (conditional text) are a feature that the user gives and reads about.
SUSPECT = [
    ("word", re.compile(
        r"\b(bytes?|records?|structs?|structures?|functions?|buffers?"
        r"|pointers?|offsets?|hex|hexadecimal|opcodes?"
        r"|algorithms?|pipes?|escape\s+sequences?|key\s+codes?"
        r"|status\s+codes?|temporary\s+(files?|directory|directories)"
        r"|passes|routes?|routines?|data\s+structures?|source\s+code"
        r"|the\s+code|the\s+layout\s+code|implementation|implemented"
        r"|checksum|malloc)\b",
        re.IGNORECASE)),
    ("hex", re.compile(
        r"(\b0x[0-9A-Fa-f]+\b|\b[0-9A-F]{2}\s*\(hex\)|\b[0-9A-F]{2}h\b"
        r"|\\x[0-9A-Fa-f]{2}|\bcode\s+[0-9A-F]{2}\b)")),
    ("path", re.compile(
        r"(\bsrc/|\bdocs/|\btests/|\btools/|\bmanual/|\./|~/|/usr/|/etc/"
        r"|/tmp/|/var/|/home/|/dev/)")),
    ("file", re.compile(
        r"\b[A-Za-z0-9_-]+\.(c|h|py|sh|src|txt|hlp|meta)\b"
        r"|\b(Makefile|mkmanual|build\.sh|DOC-FORMAT|PRINT-LATEX)\b")),
    ("mechanism", re.compile(
        r"(\bDVI\s+files?\b"
        r"|\b(mode|permissions?)\s+0?[0-7]{3,4}\b"
        r"|\\(rm|sf|tt)family\b|\\textascii\w+|\\textbackslash"
        r"|\\usepackage|\\documentclass"
        r"|\b(writes?|puts?|leaves?)\s+a\s+note\s+(on|in|at)\b"
        r"|\bnote\s+on\s+the\s+first\s+page\b"
        r"|\b(read|reads)\s+the\s+(file|settings)\s+again\b"
        r"|\bwhat\s+is\s+read\s+at\s+start\b"
        r"|\bWritten\s+to\s+TeX\b"
        r"|\bsets\s+each\s+character\s+in\s+the\s+typeset\s+file\b"
        r"|\bin\s+the\s+typeset\s+file\b"
        # Second pass: the way a program reads, writes or keeps a thing.
        r"|\b(reads|writes)\s+the\s+whole\s+file\b"
        r"|\btemporary\s+(copy|copies)\b"
        r"|\bdocument\s+(it\s+reads|in\s+memory)\b"
        r"|\b(drawn|made|typeset|set)\s+by\s+(La)?TeX\b"
        r"|\$XDG_CACHE_HOME"
        r"|\bhands?\s+over\s+the\s+terminal\b"
        r"|\bas\s+[0-9A-F]{2}\s+in\s+(a|the)\b"
        r"|\bcode\s+from\s+[0-9]+\s+to\s+[0-9]+\b"
        r"|\bgives?\s+them\s+to\s+the\s+programs\s+that\s+it\s+starts\b"
        r"|\bis\s+kept\s+with\s+the\s+picture\s+in\s+the\s+cache\b)",
        re.IGNORECASE)),
]

# Words that begin a longer word and are a feature: "structured".
FALSE_WORDS = re.compile(r"\b(un)?structured\b", re.IGNORECASE)


def skipped_files(here):
    """Return the names of the sources that checkimpl.allow leaves out."""
    names = set()
    path = os.path.join(here, "checkimpl.allow")
    if os.path.exists(path):
        with open(path, encoding="ascii") as f:
            for line in f:
                if line.startswith("skip:"):
                    names.add(line[5:].strip())
    return names


def allowed_phrases(here):
    """Read the allowed phrases of checkimpl.allow."""
    phrases = []
    path = os.path.join(here, "checkimpl.allow")
    if os.path.exists(path):
        with open(path, encoding="ascii") as f:
            for line in f:
                line = line.rstrip("\n")
                if (line.strip() != "" and not line.startswith("#") and
                        not line.startswith("skip:")):
                    phrases.append(re.compile(re.escape(line.strip()),
                                              re.IGNORECASE))
    return phrases


def text_of(line):
    """Return the part of a source line that is text a reader sees."""
    if line.startswith((".include ", ".mathsyms ")):
        return ""
    if line.startswith(".pic "):
        parts = line.split(None, 3)
        line = parts[3] if len(parts) > 3 else ""
    elif line.startswith("."):
        parts = line.split(None, 1)
        line = parts[1] if len(parts) > 1 else ""
    line = re.sub(r"\{(?:[xXnc]|fig|Fig):(?:[^{}]|\{\{|\}\})*\}", " ", line)
    line = re.sub(r"\s*\[[A-Za-z0-9_.-]+\]\s*$", "", line)
    return line


DEVELOPER_IF = ".if .STRATA_BUILD_TYPE # release"


def check(path, phrases):
    """Return the list of (line number, kind, word, line) of one file."""
    hits = []
    stack = []
    with open(path, encoding="ascii", errors="replace") as f:
        for num, line in enumerate(f, 1):
            line = line.rstrip("\n")
            # The branch of a developer condition that the release leaves
            # out may say how the programs work inside.
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
            if any(dev and not in_else for dev, in_else in stack):
                continue
            text = text_of(line)
            text = FALSE_WORDS.sub(" ", text)
            for p in phrases:
                text = p.sub(" ", text)
            for kind, rx in SUSPECT:
                for m in rx.finditer(text):
                    hits.append((num, kind, m.group(0), line))
    return hits


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    files = argv[1:]
    if len(files) == 0:
        files = sorted(glob.glob(os.path.join(here, "src", "*.src")))
    phrases = allowed_phrases(here)
    skipped = skipped_files(here)
    total = 0
    for path in files:
        if os.path.basename(path) in skipped:
            continue
        seen = set()
        for num, kind, word, line in check(path, phrases):
            total += 1
            if num in seen:
                continue
            seen.add(num)
            print("%s:%d: [%s:%s] %s" % (os.path.basename(path), num, kind,
                                         word, line.strip()[:90]))
    print("checkimpl: %d suspect hit(s) in %d file(s)" % (total, len(files)))
    return 1 if total > 0 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
