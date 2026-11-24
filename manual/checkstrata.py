#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program fails when a document of the manual that the generator made
# has the word Strata, or the name LaTeX or TeX, as plain text.  In every
# document the name is a LaTeX marker with the command \strata (\LaTeX{},
# \TeX{}), so that a print shows it as the logo sets it: in the text, in the
# headlines, in the contents, in the list of figures and in the titles that a
# cross-reference quotes.  (The text of an index entry is a plain record of
# the index marker, which cannot hold a marker: it is not looked at.)  The
# names LaTeX and TeX are found in the exact spelling only: a word in
# another case is a fault of the names table of checkeditorial.py.  Plain
# text is allowed in
#
#   - the header of the document, which holds the description (the title
#     that the Document Manager lists), before the first block;
#   - text in the program font (the words of a message that the program
#     writes, a file name, a code block);
#   - the markers that are not page text: keycaps, equations, picture
#     names, expressions, and the text of the LaTeX marker itself;
#   - a word that is only part of a name or a path (.strata, STRATA_HOME,
#     Stratagem), because the word must stand alone;
#   - the name LaTeX or TeX inside double quotes: the words of a message of
#     a program, as they appear;
#   - the text of a note (a footnote): a note holds plain text, because the
#     Editor and the Printer read its text up to the first end of a text
#     record, so that a marker inside it would be shown and printed as its
#     raw bytes; the name stays plain there;
#   - a phrase that the file checkstrata.allow lists.
#
# The program also fails when a marker stands inside the text of a note or a
# comment (a marker nested in a marker that holds text).
#
#     python3 -I manual/checkstrata.py [FILE.doc ...]
#
# The word is found in any case (STRATA too).  With no argument every .doc
# file in the directories doc and samples, next to this program, is
# checked.  The file checkstrata.allow holds the allowed phrases: one phrase
# per line, matched without regard to case against the text around the word
# (the 40 characters on each side); a line that begins with a number sign is
# a comment.  The exit status is 1 when a hit is left.

import glob
import os
import re
import sys

WORD = b"Strata"
# The names that are looked for: (the word, whether the case counts).  Strata
# is found in any case (STRATA too).
NAMES = ((b"Strata", False), (b"LaTeX", True), (b"TeX", True))
MARK = b"\x13"
# The markers whose text is page text, so that it is looked at: none.  The
# text of a note is plain (see above) and is skipped with the marker.
TEXT_MARKERS = set()
# The markers that hold plain text only: the note (1) and the comment (6).
PLAIN_TEXT_MARKERS = {b"1", b"6"}
FONT_MARKER = b"15"
OPEN = re.compile(rb"\x13(\d+)\(")


def allowed_phrases(here):
    """The phrases of checkstrata.allow, as lower case bytes."""
    phrases = []
    path = os.path.join(here, "checkstrata.allow")
    if os.path.exists(path):
        with open(path, encoding="ascii") as f:
            for line in f:
                line = line.rstrip("\n")
                if line != "" and not line.startswith("#"):
                    phrases.append(line.lower().encode("ascii"))
    return phrases


def word_at(data, pos, word=WORD):
    """Whether the word stands alone at pos: no letter, digit, underscore,
    dot, tilde, slash or backslash before it and no letter, digit or
    underscore after."""
    before = data[pos - 1:pos] if pos > 0 else b" "
    if data[pos - 2:pos] == b"\nT":
        # The letter T that begins the record of a text is not text.
        before = b" "
    end = pos + len(word)
    after = data[end:end + 1] if end < len(data) else b" "
    if before.isalnum() or before in (b"_", b".", b"~", b"/", b"\\"):
        return False
    return not (after.isalnum() or after == b"_")


def name_here(data, pos):
    """The name (a word of NAMES) that stands alone at pos, or None."""
    for word, exact in NAMES:
        piece = data[pos:pos + len(word)]
        if (piece == word if exact else piece.lower() == word.lower()) and \
                word_at(data, pos, word):
            return word
    return None


def in_quotes(data, pos):
    """Whether pos is inside a pair of double quotes of its paragraph: the
    words of a message of a program, as they appear, which stay plain (the
    generator leaves them as they are).  A paragraph is the text since the
    last record that begins a text (T) or the last end of a line of it."""
    start = max(data.rfind(b"\r\nT", 0, pos), data.rfind(b"\x1d", 0, pos))
    return data.count(b'"', start + 1 if start >= 0 else 0, pos) % 2 == 1


def skip_marker(data, start):
    """The offset after the marker that begins at start, with the markers
    that it holds."""
    depth = 1
    scan = OPEN.match(data, start).end()
    while depth > 0 and scan < len(data):
        nxt = data.find(MARK, scan)
        if nxt < 0:
            return len(data)
        if data[nxt + 1:nxt + 2] == b")":
            depth -= 1
            scan = nxt + 2
        elif OPEN.match(data, nxt) is not None:
            depth += 1
            scan = OPEN.match(data, nxt).end()
        else:
            scan = nxt + 1
    return scan


def hits_of(data, phrases):
    """The (offset, context) of every plain use of the word in the page
    text of a document."""
    start = data.find(b"*B1")
    if start < 0:
        return []
    hits = []
    stack = []
    font = False
    pos = start
    while pos < len(data):
        if data[pos:pos + 1] == MARK:
            m = OPEN.match(data, pos)
            if m is not None:
                kind = m.group(1)
                if kind == FONT_MARKER:
                    # A font marker with a record starts the program font
                    # and an empty one ends it.
                    font = data[m.end():m.end() + 2] != MARK + b")"
                    pos = skip_marker(data, pos)
                elif kind in TEXT_MARKERS:
                    stack.append(kind)
                    pos = m.end()
                else:
                    # Any other marker is not page text: all of it is
                    # skipped, with the markers that it holds.
                    pos = skip_marker(data, pos)
                continue
            if data[pos + 1:pos + 2] == b")":
                if stack:
                    stack.pop()
                pos += 2
                continue
        name = None if font else name_here(data, pos)
        if name is not None and name != WORD and in_quotes(data, pos):
            name = None
        if name is not None:
            ctx = data[max(0, pos - 40):pos + len(name) + 40]
            low = ctx.lower()
            if not any(p in low for p in phrases):
                hits.append((pos, ctx.replace(b"\r\n", b" ")))
        pos += 1
    return hits


def nested_in_text(data):
    """The (offset, context) of every marker that stands inside the text of
    a marker that holds plain text only (a note or a comment)."""
    start = data.find(b"*B1")
    if start < 0:
        return []
    hits = []
    for m in OPEN.finditer(data, start):
        if m.group(1) not in PLAIN_TEXT_MARKERS:
            continue
        end = skip_marker(data, m.start())
        inner = OPEN.search(data, m.end(), end - 2)
        if inner is not None:
            ctx = data[m.start():min(end, m.start() + 80)]
            hits.append((inner.start(), ctx.replace(b"\r\n", b" ")))
    return hits


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    files = argv[1:] or sorted(
        glob.glob(os.path.join(here, "doc", "*.doc")) +
        glob.glob(os.path.join(here, "samples", "*.doc")))
    phrases = allowed_phrases(here)
    total = 0
    for path in files:
        with open(path, "rb") as f:
            data = f.read()
        for pos, ctx in hits_of(data, phrases)[:20]:
            total += 1
            print("%s: [%d] %s" % (os.path.basename(path), pos,
                                   ctx.decode("ascii", "replace")))
        for pos, ctx in nested_in_text(data)[:20]:
            total += 1
            print("%s: [%d] a marker inside the text of a note or a comment:"
                  " %s" % (os.path.basename(path), pos,
                           ctx.decode("ascii", "replace")))
    print("checkstrata: %d fault(s) in %d file(s)" % (total, len(files)))
    return 1 if total > 0 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
