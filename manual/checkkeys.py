#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program fails when a key that a user presses is not written as a
# keycap in the sources of the manual.  A key is a keycap, {k:Ctrl-S}, in
# every document; the names of the entries of a menu are plain text, and the
# letter that chooses an entry is a keycap.  It finds
#
#   - a bold, italic, underlined or program-font command whose text is a
#     key: {b:F12}, {c:Ctrl-S}, {b:Enter};
#   - plain text that looks like keys pressed one after the other, such as
#     F12 F S or Alt-F2 S: a function key followed by letters;
#   - a key with Ctrl-, Alt- or Shift-, or a function key, written as a
#     plain word in the middle of a sentence.
#
#     python3 -I manual/checkkeys.py [FILE.src ...]
#
# With no argument every .src file next to this program is checked.  The
# file checkkeys.allow, next to this program, holds the allowed phrases: one
# phrase per line, matched without regard to case; a line that begins with a
# number sign is a comment.  A phrase is removed from a line before the line
# is checked.  Code lines (between .code and .end), index lines, keycaps,
# text in the program font and labels are not checked.  The exit status is
# 1 when a hit is left.

import glob
import os
import re
import sys

KEYWORD = (r"(?:F(?:[1-9]|1[0-2])|Ctrl-[^\s}]+|Alt-[^\s}]+|Shift-[^\s}]+|"
           r"Enter|Esc|Backspace|PgUp|PgDn|Tab|Ins|Del|Home|End|"
           r"Keypad [^\s}]+)")

# A command that styles a key as text.
STYLED = re.compile(r"\{[bciu]:\s*(" + KEYWORD + r")\s*\}")
# Keys one after the other in plain text.
SEQUENCE = re.compile(r"(?<![\w{:-])(?:F1[0-2]?|F[2-9]|Alt-F[0-9]+|"
                      r"Ctrl-F[0-9]+)(?:\s+[A-Z])+(?![\w-])")
# A modified key, or a function key, as a word of the sentence.
NAMED = re.compile(r"(?<![\w{:/.-])(?:Ctrl-[A-Za-z]|Alt-[A-Za-z0-9]|"
                   r"Shift-[A-Za-z0-9]|F(?:[2-9]|1[0-2]?))(?![\w-])")


def allowed_phrases(here):
    """The phrases of checkkeys.allow, as patterns."""
    path = os.path.join(here, "checkkeys.allow")
    out = []
    if os.path.exists(path):
        with open(path, encoding="ascii") as f:
            for line in f:
                line = line.rstrip("\n")
                if line == "" or line.startswith("#"):
                    continue
                out.append(re.compile(re.escape(line), re.IGNORECASE))
    return out


def text_of(line):
    """The words of a source line, without the commands that are not text:
    keycaps, program-font text, labels and quoted words (the messages of the
    programs, which are written as they appear)."""
    line = re.sub(r'"[^"]*"', " ", line)
    line = re.sub(r"\{k:[^}]*\}", " ", line)
    line = re.sub(r"\{c:[^}]*\}", " ", line)
    line = re.sub(r"\{(?:x|X|n|ref|p):[^}]*\}", " ", line)
    line = re.sub(r"\[[a-z0-9-]+\]\s*$", " ", line)
    return line


def check(path, phrases):
    """The list of (line number, hit, line) of one file."""
    hits = []
    in_code = False
    with open(path, encoding="ascii", errors="replace") as f:
        for num, line in enumerate(f, 1):
            line = line.rstrip("\n")
            if line.startswith(".code"):
                in_code = True
                continue
            if line.startswith(".end"):
                in_code = False
                continue
            if in_code or line.startswith("#") or line.startswith(".ix") or \
                    line.startswith(".pic") or line.startswith(".doc") or \
                    re.match(r"\.h[1-6] ", line):
                continue
            styled = line
            for p in phrases:
                styled = p.sub(" ", styled)
            for m in STYLED.finditer(styled):
                hits.append((num, m.group(0), line))
            if os.path.basename(path).startswith("messages"):
                # The messages are the words of the programs.
                continue
            if line.startswith(".term "):
                # The term is a name; its definition is text.
                line = line.partition("|")[2]
            text = text_of(line)
            for p in phrases:
                text = p.sub(" ", text)
            for rx in (SEQUENCE, NAMED):
                for m in rx.finditer(text):
                    hits.append((num, m.group(0), line))
    return hits


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    files = argv[1:] or sorted(glob.glob(os.path.join(here, "src",
                                                       "*.src")))
    phrases = allowed_phrases(here)
    total = 0
    per_file = {}
    for path in files:
        for num, word, line in check(path, phrases):
            total += 1
            name = os.path.basename(path)
            per_file[name] = per_file.get(name, 0) + 1
            print("%s:%d: [%s] %s" % (name, num, word, line.strip()[:90]))
    print("checkkeys: %d key(s) not written as keycaps in %d file(s)"
          % (total, len(files)))
    if os.environ.get("CHECKKEYS_COUNT"):
        for name in sorted(per_file):
            print("  %-28s %d" % (name, per_file[name]))
    return 1 if total > 0 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
