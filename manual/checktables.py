#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program checks the borders of the tables of the generated documents.
#
#     python3 -I manual/checktables.py DOC...     check the documents
#     python3 -I manual/checktables.py --self-test  show that it can fail
#
# The rule that every table follows (it is written in one place, in the
# generator manual/mkmanual.py):
#
#   - The header row has a single line at its top, its left, its right and
#     between its columns, a double line at its bottom, and bold text.
#   - Every other row has single lines at its left, its right, between its
#     columns and at its bottom, and none at its top.
#   - A table with no header row starts with a single line at the top of its
#     first row, and its text is not made bold.
#
# A line between two columns is written once, as the right side of the left
# column, so the records of a row of N columns are, as border masks:
#
#   header row                89 and then 73 for every other column
#   other rows                84 and then 68 for every other column
#   first row, no header      85 and then 69 for every other column
#
# with the flag 1 (Connect Borders), the record CL1 on the first column only
# and the record CR1 on every column.  A table is a run of blocks whose
# columns all have the column format T1 (the panels and the notes do not).

import os
import sys
import tempfile

ATTR = b"\x12"
BOLD = 1

# A row that was split in two by a continuation line that does not begin with
# a space shows in two ways.  The first cell of the first half ends in a word
# that no entry can end with (an article, a conjunction, "of"): that is a
# fault by itself.  Or it ends in a preposition, which an entry may end
# with ("Search for"), and the first cell of the next row begins with a
# small letter: that is a fault too.
STRONG = (b"the", b"a", b"an", b"and", b"or", b"that", b"than", b"its",
          b"their", b"if", b"but")
WEAK = (b"of", b"into", b"to", b"in", b"for", b"with", b"on", b"at", b"by",
        b"is", b"are", b"from", b"as", b"this", b"these", b"those",
        b"which")

HEADER = (89, 73)
BODY = (84, 68)
HEADLESS = (85, 69)


def records(path):
    """The records of a document, as a list of bytes."""
    with open(path, "rb") as f:
        return f.read().split(b"\r\n")


def blocks(path):
    """The blocks of the document as lists of columns; a column is a pair of
    its property records and its text.  A column starts at its first property
    record that follows text, and its text may be several T records (a marker
    inside the text splits it)."""
    out = []
    cur = None
    col = None

    def close():
        nonlocal col
        if col is not None and cur is not None:
            cur.append((col[0], b"".join(col[1])))
        col = None

    for rec in records(path):
        if rec in (b"S", b"+", b"-", b"*E"):
            close()
            cur = None
        elif rec[:1] == b"B" or rec[:2] == b"*B":
            close()
            if cur is None or cur:
                cur = []
                out.append(cur)
        elif rec[:1] == b"C":
            if col is not None and col[1]:
                close()
            if cur is None:
                cur = []
                out.append(cur)
            if col is None:
                col = ([], [])
            col[0].append(rec)
        elif rec[:1] == b"T":
            if cur is None:
                cur = []
                out.append(cur)
            if col is None:
                col = ([], [])
            col[1].append(rec[1:])
    close()
    return out


def is_table_block(block):
    """True when the block has columns and every one has the column format
    T1."""
    return len(block) >= 1 and all(b"CFT1" in p for p, _ in block)


def bold_cell(text):
    """True when the whole text of a header cell is bold: it starts with a
    change of the attributes that sets bold, and no later change of the
    attributes clears bold, except the last one, which ends the run."""
    masks = []
    pos = 0
    while True:
        i = text.find(ATTR, pos)
        if i < 0 or i + 2 >= len(text):
            break
        masks.append((i, text[i + 1]))
        pos = i + 3
    if not masks or masks[0][0] != 0 or masks[0][1] & BOLD == 0:
        return False
    for i, m in masks[1:-1]:
        if m & BOLD == 0:
            return False
    last = masks[-1]
    return len(masks) == 1 or last[1] == 0 or last[1] & BOLD != 0


def small_start(text):
    """True when the text of a cell begins with a small letter."""
    pos = 0
    while pos < len(text) and text[pos:pos + 1] == ATTR:
        pos += 3
    return pos < len(text) and text[pos:pos + 1].isalpha() and \
        text[pos:pos + 1].islower()


def plain_tail(text):
    """The last word of the text of a cell when the cell ends in plain text
    (no change of the attributes and no marker after the word), else b""."""
    out = bytearray()
    pos = 0
    while pos < len(text):
        if text[pos:pos + 1] == ATTR and pos + 2 < len(text):
            pos += 3
            continue
        out.append(text[pos])
        pos += 1
    data = bytes(out).rstrip(b"\x00\x1d \r\n")
    if data == b"" or data[-1:] in b".:;!?)\"'":
        return b""
    words = data.split()
    if len(words) < 2:
        return b""
    word = words[-1]
    if not word.isalpha() or not word.islower():
        return b""
    return word


def masks_of(block):
    """The border masks of the columns, the flags, and the problems of the
    other records of the columns."""
    masks = []
    flags = []
    problems = []
    for n, (props, _) in enumerate(block):
        cb = [p for p in props if p[:2] == b"CB"]
        if len(cb) != 1:
            problems.append("column %d has %d CB records" % (n + 1, len(cb)))
            masks.append(-1)
            flags.append(-1)
            continue
        text = cb[0][2:].decode("ascii", "replace")
        mask, _, flag = text.partition(",")
        try:
            masks.append(int(mask))
            flags.append(int(flag))
        except ValueError:
            masks.append(-1)
            flags.append(-1)
        left = b"CL1" in props
        right = b"CR1" in props
        if left != (n == 0):
            problems.append("column %d: CL1 must be on the first column "
                            "only" % (n + 1))
        if not right:
            problems.append("column %d: CR1 is missing" % (n + 1))
    return masks, flags, problems


def kind_of(masks):
    """HEADER, BODY or HEADLESS when the masks are the ones of that row, or
    None."""
    for name, rule in (("header", HEADER), ("body", BODY),
                       ("headless", HEADLESS)):
        want = [rule[0]] + [rule[1]] * (len(masks) - 1)
        if masks == want:
            return name
    return None


def check_document(path):
    """The list of faults of the tables of one document and the number of
    tables."""
    faults = []
    tables = 0
    prev = None
    rownum = 0
    before = None
    for bi, block in enumerate(blocks(path)):
        if not is_table_block(block):
            prev = None
            before = None
            continue
        if len(block) > 1 and kind_of(masks_of(block)[0]) == "body":
            first = block[0][1]
            if before is not None and before[1] in WEAK and \
                    small_start(first):
                faults.append("%s: the first cell of this row begins with a "
                              "small letter and the row before it ends in "
                              "\"%s\": a row split in two by a line that "
                              "has no space at its start" %
                              (before[0], before[1].decode("ascii")))
            tail = plain_tail(first)
            before = ("%s: row %r" % (os.path.basename(path),
                                      first[:30].decode("latin-1")), tail)
            if tail in STRONG:
                faults.append("%s: the first cell ends in \"%s\": a row "
                              "split in two by a line that has no space at "
                              "its start" % (before[0],
                                             tail.decode("ascii")))
        else:
            before = None
        masks, flags, problems = masks_of(block)
        name = os.path.basename(path)
        first = block[0][1][:30].decode("latin-1")
        where = "%s: row %r" % (name, first)
        for p in problems:
            faults.append("%s: %s" % (where, p))
        if any(f != 1 for f in flags):
            faults.append("%s: the Connect flag must be 1" % where)
        kind = kind_of(masks)
        if kind is None:
            faults.append("%s: the borders %s are not those of a header "
                          "row, a body row or a first row with no header"
                          % (where, masks))
            prev = None
            continue
        if kind in ("header", "headless"):
            if kind == "headless" and prev is not None and \
                    prev[1] == len(block):
                faults.append("%s: a row with a line at its top inside a "
                              "table" % where)
            tables += 1
            rownum = 0
            prev = (kind, len(block))
        else:
            if prev is None or prev[1] != len(block):
                faults.append("%s: a body row that follows no header row"
                              % where)
                prev = (kind, len(block))
            rownum += 1
        if kind == "header":
            for n, (_, text) in enumerate(block):
                if not bold_cell(text):
                    faults.append("%s: the text of header cell %d is not "
                                  "bold" % (where, n + 1))
    return faults, tables


def make_doc(path, rows):
    """A scratch document of table rows; each row is a list of
    (props, text) columns."""
    crlf = b"\r\n"
    out = [b"*R2", b"MR1001", b"FHA1", b"DP0,8500,11000", b"*B1", b"S", b"+",
           b"S"]
    for r, cols in enumerate(rows):
        out.append(b"BM15")
        for props, text in cols:
            out += props
            out.append(b"T" + text + b"\x00")
    out += [b"-", b"*E"]
    with open(path, "wb") as f:
        f.write(crlf.join(out) + crlf)


def col(mask, text, left=False, bold=False, flag=1, right=True):
    """One column of a scratch table."""
    props = [b"CW10", b"CB%d,%d" % (mask, flag)]
    if left:
        props.append(b"CL1")
    if right:
        props.append(b"CR1")
    props.append(b"CFT1")
    body = text
    if bold:
        body = ATTR + b"\x01\x00" + text + ATTR + b"\x00\x00"
    return props, body


def good_table():
    """The rows of a table that follows the rule."""
    return [
        [col(89, b"A", True, True), col(73, b"B", False, True)],
        [col(84, b"a", True), col(68, b"b")],
        [col(84, b"c", True), col(68, b"d")],
    ]


def self_test():
    """Show that the check passes a good table and fails every way that a
    table can break the rule."""
    base = tempfile.mkdtemp(prefix="checktables-")
    cases = []
    cases.append(("a table that follows the rule", good_table(), True))
    t = good_table()
    t[1] = [col(16, b"a", True), col(16, b"b")]
    cases.append(("a body row with only a left line", t, False))
    t = good_table()
    t[0] = [col(89, b"A", True, False), col(73, b"B", False, True)]
    cases.append(("a header cell that is not bold", t, False))
    t = good_table()
    t[0] = [col(89, b"A", True, True), col(73, b"B", False, False)]
    cases.append(("a second header cell that is not bold", t, False))
    t = good_table()
    keep = ATTR + b"\x01\x00A " + ATTR + b"\x05\x00i" + ATTR + b"\x01\x00 z" + \
        ATTR + b"\x00\x00"
    t[0] = [col(89, keep, True), col(73, b"B", False, True)]
    cases.append(("a bold header cell with an italic run in it", t, True))
    t = good_table()
    lose = ATTR + b"\x01\x00A " + ATTR + b"\x00\x00i" + ATTR + b"\x01\x00 z" + \
        ATTR + b"\x00\x00"
    t[0] = [col(89, lose, True), col(73, b"B", False, True)]
    cases.append(("a header cell that loses its bold in the middle", t,
                  False))
    t = good_table()
    t[2] = [col(85, b"c", True), col(69, b"d")]
    cases.append(("a body row with a line at its top", t, False))
    t = good_table()
    t[1] = [col(84, b"a", False), col(68, b"b")]
    cases.append(("a body row without CL1", t, False))
    t = good_table()
    t[1] = [col(84, b"a", True), col(68, b"b", right=False)]
    cases.append(("a body row without CR1", t, False))
    t = good_table()
    t[1] = [col(84, b"a", True, flag=0), col(68, b"b")]
    cases.append(("a body row that does not connect", t, False))
    t = good_table()
    t[2] = [col(88, b"c", True), col(72, b"d")]
    cases.append(("a body row with a double line at its bottom", t, False))
    t = good_table()
    t[1] = [col(84, b"The commands of the", True), col(68, b"b")]
    t[2] = [col(84, b"Tools menu", True), col(68, b"c")]
    cases.append(("a row split after an article", t, False))
    t = good_table()
    t[1] = [col(84, b"The commands of the Tools menu", True), col(68, b"b")]
    cases.append(("a body row with a whole first cell", t, True))
    t = good_table()
    t[1] = [col(84, b"The file on", True), col(68, b"b")]
    t[2] = [col(84, b"disk is the same", True), col(68, b"c")]
    cases.append(("a row split after a preposition", t, False))
    t = good_table()
    t[1] = [col(84, b"Search for", True), col(68, b"b")]
    t[2] = [col(84, b"Replace with", True), col(68, b"c")]
    cases.append(("entries that end in a preposition", t, True))
    t = good_table()
    t[1] = [col(84, b"Press " + ATTR + b"\x01\x00Ctrl" + ATTR + b"\x00\x00 to",
                True), col(68, b"b")]
    t[2] = [col(84, b"Next entry", True), col(68, b"c")]
    cases.append(("a first cell that ends in to, then a capital", t, True))
    t = good_table()[1:]
    cases.append(("body rows with no header row", t, False))
    t = [[col(85, b"a", True), col(69, b"b")],
         [col(84, b"c", True), col(68, b"d")]]
    cases.append(("a table with no header row, as written for one", t, True))
    bad = 0
    for n, (what, rows, want_ok) in enumerate(cases):
        path = os.path.join(base, "t%d.doc" % n)
        make_doc(path, rows)
        faults, _ = check_document(path)
        ok = not faults
        verdict = "ok" if ok == want_ok else "WRONG"
        if ok != want_ok:
            bad += 1
        print("self-test: %-52s %s" % (what, verdict))
        os.unlink(path)
    os.rmdir(base)
    return bad


def main(argv):
    """Entry point."""
    if len(argv) >= 2 and argv[1] == "--self-test":
        bad = self_test()
        if bad != 0:
            print("checktables: %d self-test case(s) are wrong" % bad)
            return 1
        print("checktables: the self-test passed")
        return 0
    if len(argv) < 2:
        sys.stderr.write("usage: checktables.py DOC... | --self-test\n")
        return 2
    total = 0
    faults = []
    for path in argv[1:]:
        f, n = check_document(path)
        total += n
        faults += f
    for line in faults[:60]:
        print("checktables: " + line)
    if faults:
        print("checktables: %d fault(s) in %d table(s)" % (len(faults),
                                                           total))
        return 1
    print("checktables: %d table(s) follow the rule" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
