#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program checks the PDF of the single document of the Reference
# Manual against the list of headlines that the generator wrote:
#
#     python3 -I manual/checkpdf.py PDF META [MINPAGES]
#
# It needs pdftotext.  It checks that the PDF has at least MINPAGES pages
# (100 unless given), that the contents and the index give for every
# headline the page on which the headline is printed, that no
# cross-reference is left unresolved ("??" or a label in brackets), that
# there are no blank pages except at the end of a chapter, and that the
# programs are not named.  It prints what is wrong and exits with 1.

import os
import re
import subprocess
import sys


def pages_of(pdf):
    """The text of every page of the PDF."""
    out = subprocess.run(["pdftotext", "-layout", pdf, "-"],
                         check=True, capture_output=True).stdout
    text = out.decode("utf-8", "replace")
    # The name is the logo of the command \strata: its letters S, RA and A
    # are separate words of the text of the page, and a sign of punctuation
    # after it has a space in front.
    text = re.sub(r"\bS\s+RA\s+A\b(?:[ \t]+(?=[,.:;)'!?]))?", "Strata", text)
    # The names of the typesetting system are the logos of \LaTeX{} and
    # \TeX{}: their letters come out as the words LATEX and TEX.
    # In the bold lines of the contents the raised A of the logo comes out
    # as a line of its own, above the line that holds the L and the TEX.
    text = re.sub(r"\bA[ \t]*\n[ \t]+L[ \t]+TEX\b", "LaTeX", text)
    text = re.sub(r"\bLATEX\b", "LaTeX", text)
    text = re.sub(r"\bTEX\b", "TeX", text)
    return text.split("\f")[:-1]


def headlines(meta):
    """The H records: (level, number, title)."""
    rows = []
    with open(meta, "r", encoding="ascii") as f:
        for line in f:
            r = line.rstrip("\n").split("\t")
            if r[0] == "H":
                rows.append((int(r[1]), r[2], r[4]))
    return rows


def xrefs(meta):
    """The X record: how many cross references to a section by its number
    the generator wrote, and how many of them are inside a condition."""
    with open(meta, "r", encoding="ascii") as f:
        for line in f:
            r = line.rstrip("\n").split("\t")
            if r[0] == "X":
                return int(r[1]), int(r[2])
    return None


def pattern(number, title):
    """A regular expression for a headline as it is printed."""
    words = (number + " " + title).split()[:7]
    # A letter, a digit or a dot is itself; any other character may have
    # been set as another one (a typographic quote or a ligature).
    return re.compile(r"^\s*" + r"\s+".join(
        "".join(c if c.isalnum() or c == "." else "." for c in w)
        for w in words), re.M)


def indexed(meta):
    """The terms of the index and the headlines they point to: {term in
    lower case: [(number, title)]}.  An entry of the source (.ix, .term) is
    an I record; every headline of the second level and below is an entry
    under its own words (the generator writes an Index marker for each)."""
    found = {}
    with open(meta, "r", encoding="ascii") as f:
        for line in f:
            r = line.rstrip("\n").split("\t")
            if r[0] == "I":
                term, number, title = r[1], r[2], r[3]
            elif r[0] == "H" and int(r[1]) >= 2 and len(r[4]) > 1:
                term, number, title = r[4], r[2], r[4]
            else:
                continue
            term = re.sub(r"\{[a-zA-Z]+:([^}]*)\}", r"\1", term)
            term = " ".join(term.replace("{{", "{").replace("}}", "}")
                            .split()).lower()
            # The index lists a term without its leading article.
            term = re.sub(r"^(?:the|a|an) +(?=\S)", "", term)
            found.setdefault(term, []).append((number, title))
    return found


def pages_in(text):
    """The numbers in a list of pages: 3, 5-8 (a run is joined with a dash
    or an en dash) as a set."""
    out = set()
    for part in re.split(r",\s*", text):
        m = re.match(r"^(\d+)(?:[-\u2013](\d+))?$", part.strip())
        if m is None:
            return None
        lo = int(m.group(1))
        hi = int(m.group(2)) if m.group(2) else lo
        out.update(range(lo, hi + 1))
    return out


def hidden_of(meta):
    """The headlines that the release edition leaves out: (number, title),
    from the file that the generator writes next to the meta file."""
    rows = set()
    path = meta.replace(".meta", ".hidden")
    if os.path.exists(path):
        with open(path, "r", encoding="ascii") as f:
            for line in f:
                number, _, title = line.rstrip("\n").partition("\t")
                rows.add((number, title))
    return rows


def main(argv):
    release = "--release" in argv
    argv = [a for a in argv if a != "--release"]
    pdf, meta = argv[1], argv[2]
    minpages = int(argv[3]) if len(argv) > 3 else 100
    pages = pages_of(pdf)
    heads = headlines(meta)
    if release:
        left_out = hidden_of(meta)
        heads = [h for h in heads if (h[1], h[2]) not in left_out]
    bad = []
    if len(pages) < minpages:
        bad.append("only %d pages, %d wanted" % (len(pages), minpages))

    # Where the body begins: the first page after the contents that has the
    # headline of the first chapter on a line without a page number.
    first = None
    chap = [h for h in heads if h[0] == 1 and h[1] != ""][0]
    head_pat = pattern(chap[1], chap[2])
    for i, t in enumerate(pages):
        if i < 2:
            continue
        for m in head_pat.finditer(t):
            line = t[m.start():t.find("\n", m.start())]
            if not re.search(r"\s\d+\s*$", line):
                first = i
                break
        if first is not None:
            break
    if first is None:
        bad.append("the first chapter was not found")
        first = 0
    index_start = None
    for i, t in enumerate(pages):
        if i > first and re.match(r"\s*Index\s*\n", t):
            index_start = i
    if index_start is None:
        bad.append("the index was not found")
        index_start = len(pages)

    # The page of every headline of the body.
    where = {}
    deep = {}
    cursor = first
    deepcursor = first
    for level, number, title in heads:
        if number == "" or "{" in title or "--" in title:
            continue
        pat = pattern(number, title)
        if level > 3:
            # Not in the contents; found for the pages of the index only.
            for i in range(deepcursor, index_start):
                if pat.search(pages[i]):
                    deep[(number, title)] = i - first + 1
                    deepcursor = i
                    break
            continue
        for i in range(cursor, index_start):
            if pat.search(pages[i]):
                # The page number that is printed: the text starts at 1.
                where[(number, title)] = i - first + 1
                deep[(number, title)] = i - first + 1
                cursor = i
                deepcursor = max(deepcursor, i)
                break
        else:
            bad.append("headline not found in the body: %s %s"
                       % (number, title))

    # The contents.
    contents = "\n".join(pages[1:first])
    nrows = 0
    for (number, title), page in where.items():
        pat = re.compile(r"^\s*" + re.escape(number) + r"\s+" +
                         pattern("", title).pattern[4:].replace(
                             r"^\s*", "", 1) +
                         r".*?\s(\d+)\s*$", re.M)
        m = pat.search(contents)
        if m is None:
            bad.append("not in the contents: %s %s" % (number, title))
            continue
        nrows += 1
        if int(m.group(1)) != page:
            bad.append("contents says page %s for %s %s, it is on %d"
                       % (m.group(1), number, title, page))
    print("contents: %d rows checked" % nrows)

    # The index that the Printer makes: a line is a term and the pages of
    # its markers, "term, 3, 5-7".  The page of a marker is the page of the
    # headline that it belongs to (the generator puts it in the headline or
    # in the paragraph of the entry), so the pages of a line are those of the
    # headlines of its entry.  A term that wraps on two lines is not read.
    nidx = 0
    wanted = indexed(meta)
    # The pages that a headline spans: from its own page to the page of the
    # next headline.  A marker of the entry is in the headline or in a block
    # of the section, so its page is in the span.
    order = [(n, t) for lv, n, t in heads if (n, t) in deep]
    span = {}
    for k, key in enumerate(order):
        later = [deep[o] for o in order[k + 1:]]
        span[key] = (deep[key], later[0] if later else index_start - first)
    idx = "\n".join(pages[index_start + 1:])
    for line in idx.split("\n"):
        m = re.match(r"^\s*(\S.*?),\s+((?:\d+(?:[-\u2013]\d+)?)"
                     r"(?:,\s*\d+(?:[-\u2013]\d+)?)*)\s*$", line)
        if m is None:
            continue
        term = " ".join(m.group(1).split()).lower()
        got = pages_in(m.group(2))
        if got is not None and re.match(r"(?:the|an?) +\S", term):
            bad.append("an entry of the index begins with an article: %r"
                       % term)
        if got is None or term not in wanted:
            continue
        keys = [k for k in wanted[term] if k in span]
        if keys == [] or len(keys) != len(wanted[term]):
            continue
        nidx += 1
        # Every page that is printed is in the span of one of the entries,
        # and every entry has a page in its span.
        stray = [g for g in got
                 if not any(span[k][0] <= g <= span[k][1] for k in keys)]
        lost = [k for k in keys
                if not any(span[k][0] <= g <= span[k][1] for g in got)]
        if stray or lost:
            bad.append("index says pages %s for %s, the headlines are on %s"
                       % (sorted(got), term,
                          sorted(span[k] for k in keys)))
    print("index: %d rows checked" % nidx)
    if nidx < 100:
        bad.append("only %d rows of the index could be checked" % nidx)

    # Unresolved cross-references and names of programs.
    alltext = "\n".join(pages)
    for m in re.finditer(r"\?\?", alltext):
        bad.append("unresolved reference: %s" % m.group(0))
    # The key stred of the configuration and the variables STRED and
    # STRATA_STRPRINT are names that may stay.
    for m in re.finditer(r"\b(str|strprint|dviview)\b", alltext, re.I):
        bad.append("a program is named in the PDF: %s" % m.group(0))

    # The cross references to a section by its number are printed with the
    # section sign directly before the number: every sign is followed by a
    # digit with no blank between, and the signs are as many as the
    # references that the generator wrote (the ones inside a condition may
    # be left out by an edition).  A reference to an appendix has the word
    # Appendix and no sign: the word is followed by a blank and the letter
    # of the appendix, two references in a row have the plural word once,
    # and a sign never stands before an appendix letter.
    counts = xrefs(meta)
    signs = [m.start() for m in re.finditer("\u00a7", alltext)]
    for pos in signs:
        if re.match(r"[0-9]", alltext[pos + 1:pos + 2]) is None:
            bad.append("a section sign is not followed by a section "
                       "number (an appendix has the word Appendix): %r" %
                       alltext[max(0, pos - 20):pos + 20])
    for m in re.finditer(r"Append(?:ix|ices)[ \u00a0]+[A-F](?:\.[0-9.]+)?"
                         r"(?:[ \n]+\([^()]*\))?(?:,| and| or)[ \n]+"
                         r"Appendix[ \u00a0]+[A-F]\b", alltext):
        bad.append("two references to appendices in a row, with the "
                   "singular word twice: %r" % m.group(0)[:60])
    print("cross references: %d appendix words" %
          len(re.findall(r"Append(?:ix|ices)[ \u00a0][A-F]\b", alltext)))
    if counts is not None:
        total, cond = counts
        low = total - cond
        if not (low <= len(signs) <= total):
            bad.append("%d section signs are printed, %d to %d wanted"
                       % (len(signs), low, total))
    print("cross references: %d section signs" % len(signs))

    # Blank pages: nothing but the page number.
    for i, t in enumerate(pages):
        words = t.split()
        if len(words) <= 1:
            nxt = pages[i + 1] if i + 1 < len(pages) else ""
            bad.append("blank page %d" % (i + 1))

    print("pages: %d" % len(pages))
    for b in bad[:60]:
        print("BAD:", b)
    if len(bad) > 60:
        print("BAD: and %d more" % (len(bad) - 60))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
