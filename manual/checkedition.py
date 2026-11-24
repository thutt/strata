#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program proves the editions of the manual.  The sources hold text
# between the directives .if and .endif (conditional text).  The PDF of a
# release is printed with .STRATA_BUILD_TYPE set to release, the PDF for
# developers with debug.  For every paragraph that is inside conditional text
# and that the release leaves out, the first words of it must not be in the
# text of the release PDF, and they must be in the text of the developer PDF.
#
#     python3 -I manual/checkedition.py MASTER RELEASE.pdf DEVELOPER.pdf
#
# The rules of the comparison are those of the If marker (src/cond.h):
# numbers compare as numbers when both sides are made of digits and fit in
# 32 bits, everything else compares as text.

import os
import re
import subprocess
import sys
import unicodedata

WORDS = 8
MINWORDS = 4


def compare(left, op, right):
    """The comparison of the If marker."""
    def as_int(s):
        if s.isdigit() and s.isascii() and int(s) <= 4294967295:
            return int(s)
        return None

    a, b = as_int(left), as_int(right)
    if a is not None and b is not None:
        left, right = a, b
    return {"=": left == right, "#": left != right, "<": left < right,
            ">": left > right, "<=": left <= right,
            ">=": left >= right}[op]


def holds(expr, variables):
    """Does the expression of an .if hold with the variables?"""
    m = re.match(r"^([A-Za-z.][A-Za-z0-9_.-]*)\s*(<=|>=|=|#|<|>)\s*(.+?)\s*$",
                 expr)
    if m is None:
        raise SystemExit("checkedition: bad expression: %s" % expr)
    name, op, lit = m.groups()
    value = variables.get(name, "")
    return compare(value if value != "" else "undefined", op, lit)


def lines_of(path):
    """The lines of a source with the .include files read in place."""
    out = []
    with open(path, "r", encoding="ascii") as f:
        for line in f.read().split("\n"):
            if line.startswith(".include "):
                name = os.path.join(os.path.dirname(path), line[9:].strip())
                out.extend(lines_of(name))
            else:
                out.append(line)
    return out


def plain(text):
    """The words of a paragraph without the markup of the sources."""
    text = re.sub(r"\{[a-zA-Z]+:([^}]*)\}", r"\1", text)
    text = text.replace("{{", "{").replace("}}", "}")
    return text


def chunks(text):
    """The pieces of a paragraph that are found whole in a PDF: the cells of
    a table are apart, and so are the words on either side of a
    cross-reference (it is written as the number and the title of a
    section)."""
    out = []
    for cell in re.split(r"\s+\|\s+", text):
        for piece in re.split(r"\{[xXnp]:[^}]*\}|\{ref:[^}]*\}", cell):
            piece = plain(piece)
            if len(piece.split()) >= MINWORDS:
                out.append(piece)
    return out


def paragraphs(path):
    """The paragraphs that are inside conditional text:
    (conditions, text) for each, the conditions as (expr, in_else)."""
    found = []
    stack = []
    words = []

    def end():
        if words and stack:
            found.append((tuple(stack), " ".join(words)))
        words.clear()

    in_table = False
    for line in lines_of(path):
        if line.startswith("#"):
            continue
        if in_table:
            # Only the first cell of a row is checked: a cell that wraps in
            # a column may be read by the PDF tool in pieces.
            if line.startswith(".end"):
                in_table = False
            elif line[:1] not in (" ", "") and stack:
                cell = re.split(r"\s+\|\s+", line, maxsplit=1)[0]
                first = plain(cell).split()
                if len(first) >= 2 and \
                        not re.fullmatch(r"[0-9+A-Za-z]{1,3}", cell):
                    found.append((tuple(stack), " ".join(first[:3])))
            continue
        if line.startswith(".table ") or line.startswith(".rows "):
            end()
            in_table = True
        elif line.strip() == "":
            end()
        elif line.startswith(".if "):
            end()
            stack.append((line[4:].strip(), False))
        elif line.startswith(".else"):
            end()
            stack[-1] = (stack[-1][0], True)
        elif line.startswith(".endif"):
            end()
            stack.pop()
        elif line.startswith("."):
            end()
        else:
            m = re.match(r"(- |\d+\. )(.*)", line)
            words.append(m.group(2).strip() if m else line.strip())
    end()
    return found


def visible(conds, variables):
    """Is the text printed with the variables?"""
    for expr, in_else in conds:
        if holds(expr, variables) == in_else:
            return False
    return True


def norm(text):
    """Only the letters and the digits, in small letters: a line break, a
    hyphen at the end of a line or a ligature then does not matter."""
    text = unicodedata.normalize("NFKC", text)
    return re.sub(r"[^0-9A-Za-z]", "", text).lower()


def tokens(text):
    """The words of a text, in small letters, between single spaces: a
    short piece is looked up as whole words, so that it is not found inside
    another word."""
    text = unicodedata.normalize("NFKC", text)
    return " " + " ".join(re.findall(r"[0-9a-z]+", text.lower())) + " "


# The word Strata is the logo of the command \strata: the letters S, RA and A
# are separate words of the text of the PDF (the letters T and the crossbar
# have no text), and a sign of punctuation after it has a space in front.
LOGO_WORD = re.compile(r"\bS\s+RA\s+A\b(?:[ \t]+(?=[,.:;)'!?]))?")


def logo_word(text):
    """The text with the logo of the name written as the word."""
    return LOGO_WORD.sub("Strata", text)


def pdf_raw(path):
    """The text of a PDF."""
    out = subprocess.run(["pdftotext", "-enc", "UTF-8", path, "-"],
                         capture_output=True, text=True, check=True).stdout
    # The number at the foot of a page is not text of the page: a sentence
    # that is split by a page break must still be found.
    pages = []
    for page in out.split("\f"):
        lines = page.rstrip("\n").split("\n")
        if lines and lines[-1].strip().isdigit():
            lines.pop()
        pages.append("\n".join(lines))
    return logo_word("\n".join(pages))


def pdf_text(path):
    """The text of a PDF as norm makes it."""
    return norm(pdf_raw(path))


def always_printed(sources):
    """The paragraphs of the sources that no condition leaves out."""
    found = []
    stack = []
    words = []

    def end():
        if words and not any(not in_else and dev
                             for dev, in_else in stack):
            found.append(plain(" ".join(words)))
        words.clear()

    for path in sources:
        for line in lines_of(path):
            if line.startswith("#"):
                continue
            if line.strip() == "":
                end()
            elif line.startswith(".if "):
                end()
                stack.append((line.strip() == ".if .STRATA_BUILD_TYPE # release",
                              False))
            elif line.startswith(".else"):
                end()
                stack[-1] = (stack[-1][0], True)
            elif line.startswith(".endif"):
                end()
                stack.pop()
            elif line.startswith("."):
                end()
            else:
                words.append(line.strip())
        end()
    return found


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: checkedition.py MASTER RELEASE.pdf DEVELOPER.pdf")
    master, release_pdf, developer_pdf = sys.argv[1:]
    srcdir = os.path.dirname(os.path.abspath(master))
    sources = []
    with open(master, "r", encoding="ascii") as f:
        for line in f:
            line = line.strip()
            if line == "" or line.startswith("#"):
                continue
            num, base, _ = line.split(None, 2)
            if num != "-":
                sources.append(os.path.join(srcdir, base + ".src"))
    release_text = pdf_text(release_pdf)
    developer_text = pdf_text(developer_pdf)
    release_words = tokens(pdf_raw(release_pdf))
    developer_words = tokens(pdf_raw(developer_pdf))
    always_list = always_printed(sources)
    always = norm(" ".join(always_list))
    always_words = tokens(" ".join(always_list))
    rel = {".STRATA_BUILD_TYPE": "release"}
    dev = {".STRATA_BUILD_TYPE": "debug"}
    checked = 0
    bad = []
    for src in sources:
        for conds, text in paragraphs(src):
            if visible(conds, rel) or not visible(conds, dev):
                continue
            # The first cell of a table row is at most three words.
            pieces = [text] if len(text.split()) <= 3 else chunks(text)
            for piece in pieces:
                words = piece.split()
                shown = " ".join(words[:WORDS])
                if re.search(r"[<>\"]", shown):
                    # The program font may print these as other signs.
                    continue
                if len(words) <= 3 and any(w.isdigit() for w in words):
                    # A number is found in the page numbers of the contents.
                    continue
                checked += 1
                if len(words) <= 3:
                    snippet = tokens(shown)
                    in_release = snippet in release_words and \
                        snippet not in always_words
                    in_developer = snippet in developer_words
                else:
                    snippet = norm(shown)
                    in_release = snippet in release_text and \
                        snippet not in always
                    in_developer = snippet in developer_text
                if in_release:
                    bad.append("in the release PDF: %s (%s)"
                               % (shown, os.path.basename(src)))
                if not in_developer:
                    bad.append("not in the developer PDF: %s (%s)"
                               % (shown, os.path.basename(src)))
    for b in bad:
        print("checkedition: " + b)
    print("checkedition: %d paragraphs for developers checked, %d problems"
          % (checked, len(bad)))
    return 1 if bad else 0


sys.exit(main())
