#!/usr/bin/env python3
# Copyright (c) 2026 Logic Magicians Software
#
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This program measures how much text the sources of the manual share with
# another text file: the number of runs of eight consecutive words (case
# and punctuation are ignored) that occur in both.  The other text is not
# part of the repository and must never be added to it; give its path as
# the argument.  The program prints only counts and the places (file and
# line) in the sources, never the shared words.
#
#     python3 -I manual/checkcopy.py OTHER.txt [SRCDIR]
#
# The number must stay near zero.  A run of more than twelve words is
# reported by file and line.  The exit status is 1 when such a run is
# found.  Run it by hand when the manual is updated; it is not part of the
# build, because the other text is not in the repository.

import os
import re
import sys

RUN = 8
LONG = 12
WORD = re.compile(r"[a-z0-9]+(?:'[a-z]+)?")


def words_of(text):
    """Return the lower case words of a text."""
    return WORD.findall(text.lower())


def source_tokens(path):
    """Return a list of paragraphs; a paragraph is a list of (word, line)."""
    paras = []
    cur = []
    with open(path, encoding="ascii", errors="replace") as f:
        for num, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if line.startswith("#") or line.strip() == "":
                if cur:
                    paras.append(cur)
                    cur = []
                continue
            if line.startswith("."):
                parts = line.split(None, 1)
                if len(parts) < 2 or parts[0] not in (".h1", ".h2", ".h3"):
                    continue
                line = re.sub(r"\[[^\]]*\]\s*$", "", parts[1])
            line = re.sub(r"\{[a-z]:[^}]*\}", " ", line)
            for w in words_of(line):
                cur.append((w, num))
    if cur:
        paras.append(cur)
    return paras


def main(argv):
    if len(argv) < 2 or len(argv) > 3:
        sys.stderr.write("usage: checkcopy.py OTHER.txt [SRCDIR]\n")
        return 2
    here = os.path.dirname(os.path.abspath(__file__))
    srcdir = argv[2] if len(argv) == 3 else os.path.join(here, "src")
    try:
        with open(argv[1], encoding="utf-8", errors="replace") as f:
            other = words_of(f.read())
    except OSError as e:
        sys.stderr.write("checkcopy.py: %s\n" % e)
        return 2
    grams = set()
    for i in range(len(other) - RUN + 1):
        grams.add(tuple(other[i:i + RUN]))
    total = 0
    shared = 0
    long_runs = []
    rows = []
    for name in sorted(os.listdir(srcdir)):
        if not name.endswith(".src"):
            continue
        count = 0
        n = 0
        for para in source_tokens(os.path.join(srcdir, name)):
            ws = [w for w, _ in para]
            hit = [tuple(ws[i:i + RUN]) in grams
                   for i in range(len(ws) - RUN + 1)]
            n += len(hit)
            count += sum(hit)
            i = 0
            while i < len(hit):
                if not hit[i]:
                    i += 1
                    continue
                j = i
                while j < len(hit) and hit[j]:
                    j += 1
                length = j - i + RUN - 1
                if length > LONG:
                    long_runs.append((name, para[i][1], length))
                i = j
        total += n
        shared += count
        if count > 0:
            rows.append((count, name))
    print("Runs of %d words in the sources: %d" % (RUN, total))
    pct = 100.0 * shared / total if total > 0 else 0.0
    print("Runs shared with %s: %d (%.3f percent)"
          % (os.path.basename(argv[1]), shared, pct))
    for count, name in sorted(rows, reverse=True):
        print("  %-16s %d" % (name, count))
    for name, line, length in long_runs:
        print("LONG RUN of %d words: %s:%d" % (length, name, line))
    return 1 if long_runs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
