#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program fails when the sources of the Reference Manual break the
# editorial rules that make it a reference (checkstyle.py has the rules of
# the program font, the shell variables and the commands, and checknames.py
# the names of the programs):
#
#   E1  Filler words: very, really, quite, just, simply, basically, easily,
#       obviously, of course, actually and their kind.  A reference states
#       the fact.
#   E2  Narration: we, our, let us, you will, imagine, as you can see and
#       the like.  The text states what the program does; a procedure is
#       in the imperative.
#   E3  Stale and future words: not yet, planned, future, TODO, FIXME,
#       soon, eventually, new in, used to, earlier version.  The manual
#       describes the program as it is.
#   E4  Hedges: might, perhaps, probably, possibly, somewhat, seems.  A
#       statement is a fact or it is not in the manual.
#   E5  Contractions (don't, it's).
#   E6  Units and numbers: a unit follows its number after a space (7 mm,
#       not 7mm); no thousands separator in a number (1000, not 1,000).
#   E7  Spacing in running text: two spaces after the period that ends a
#       sentence and one space everywhere else; no run of three spaces.
#   E8  Terms: a word or a spelling that docs/MANUAL-TERMS.txt lists as
#       wrong, with the right one named in the report.
#   E9  A cross reference that is typed (chapter 5, section 2.1, Appendix
#       B) instead of made by the generator ({x:label}, the section sign),
#       which would go stale when a section is renumbered.
#   E10 Headlines are in sentence case: the first word and the names of
#       Strata (a panel, a field, a command, a marker) begin with a capital
#       letter, an ordinary word does not.
#   E11 The items of one list end alike: all with a period or none.
#   E12 A sentence that begins with This or It and a verb has no noun to
#       point at: name the thing.
#   E13 The same long sentence in two places of the manual: say it once and
#       refer to it.
#   E14 The first sentence under a headline states the thing; it does not
#       announce the section ("This section describes ...") or begin with
#       Here or Now.
#   E15 One column of a table has cells that end with a period and cells
#       that do not: all the cells of a sentence column end with one.
#   E16 A table row that continues the row before it (its text goes on in
#       lower case in the one cell that it fills) begins with a space.  A
#       line that begins in the first column is a new row, and the table
#       shows a row that ends in the middle of a cell.
#   E17 A command ({c:...}) that is opened in one line of a table and closed
#       in a later line that begins with a bar: the bar is printed as text.
#   E18 A reference with a title ({x:label}) inside parentheses: the title
#       would nest inside the parentheses.  Use {n:label}, the sign and the
#       number.
#   E19 The word Appendix (or Appendices) typed in front of a reference
#       ({x:label}, {X:label}, {n:label}): the generator prints the word
#       itself for a reference to an appendix, once for a run of them
#       (Appendices B and C), and the section sign for every other
#       reference, so the typed word would print twice.
#   E20 The entry of a marker (a second-level section of the markers chapter
#       whose label begins with marker-) has the third-level sections On the
#       screen, When printed and Edges.
#   E21 The entry of a command of the File menu (Save, Retrieve, New,
#       Delete, Import, Export) has a section for its panel, a section for its
#       edges and, last, a section called See.
#   E22 A word before a reference that the reference makes redundant: the,
#       a, an, section, chapter or appendix in front of {x:label} or
#       {n:label}, which print the sign and the number.
#   E23 The words See also.  The manual points to another place with See
#       (the form that most of its references use), in the text, in the
#       headlines and in the index.
#   E24 A row of a table of messages that is not in its place: in the files
#       of the Messages chapter (messages1.src and the others) the rows of a
#       table whose first column is headed Message, Text, Title and text or
#       Message of TeX are sorted by the text of the first cell, ignoring
#       capital letters, marks and punctuation.
#
#   E26 The names of systems, formats and programs have one spelling
#       (the table NAMES below): LaTeX, TeX, pdfTeX, XeTeX, PDF, DVI, ASCII,
#       Unicode, UTF-8, X11, PostScript, Ghostscript, PNG, CUPS, Linux,
#       Debian, Ubuntu, Latin-1, macOS, TrueType.  A command or a file name
#       is a different thing: it stays in the program font, where this rule
#       does not look.
#   E25 The name LaTeX or TeX in the running text that the generator would
#       leave as plain text (manual/mkmanual.py makes each of them a LaTeX
#       marker with the logo of the name): a name that sits against a
#       slash, a tilde, a backslash or a letter that makes it look like part
#       of a path or of another word.  The only one is e-TeX (the allow
#       file).  The documents that are made are checked by checkstrata.py.
#   E27 The widths of a table that add up to more than the line holds
#       (MK.table_room: 61 for two columns, 60 for three, 58 for four and
#       56 for five): the last column of such a table gets less than its
#       source says.  The generator would fit the widths all the same; the
#       source must say what is printed.
#   E29 A table whose first row begins with a number: the generator sets the
#       first row of every table as a header row, in bold, so a table of
#       numbered steps needs a header row of its own (Step | What to do).
#   E30 Software that acts like a person: a program, a panel, a box, a bar,
#       a message line, TeX or a terminal that says, asks, tells, wants,
#       knows, tries, decides, refuses, understands, remembers, forgets,
#       thinks, expects, assumes, prefers, likes, helps, sees, notices,
#       guesses, hopes, insists or learns, and the pronoun it with one of
#       these verbs.  Software is a mechanical device: a panel shows or
#       prompts for, a check rejects, a rule determines, and a sentence that
#       would need a person as its subject is written in the passive voice
#       ("If any is missing, nothing is installed", not "it installs
#       nothing").  This rule and E31 look at every source of the product,
#       the guides and the cards too, not only at the Reference Manual.
#   E32 The pronoun it or they for software, with a verb of a person or of
#       intention: it or they, in a sentence or in the one after a
#       sentence that has a program, a panel, a box, a bar, a message line,
#       TeX or a terminal for its subject, with a bare or a modal verb of
#       E30 ("it would not help", "they say") or with allows, lets, offers,
#       ignores, recognizes, warns, complains, cares, chooses, picks or
#       insists; and software with a modal and such a verb ("a terminal
#       cannot see", "LaTeX would refuse").  Software is a mechanical
#       device: the text names the thing ("the keys cannot be told apart")
#       or uses the passive voice.  Like E30 and E31, this rule looks at
#       every source of the product.
#   E31 The word bug, bugs or buggy anywhere in a source.  A defect of the
#       software is called a defect or an error.
#
#     python3 -I manual/checkeditorial.py [FILE.src ...]
#
# With no argument the sources of the Reference Manual are checked: every
# .src file of the master except the cards, the guides and the Strata File
# Formats.  Text that is not plain text (the program font, keycaps,
# equations, LaTeX markers, labels, cross references and quoted words, which
# are the messages of the programs as they appear) is not checked.  The
# file checkeditorial.allow, next to this program, holds the allowed
# places: one line per place, a rule name (E1 ...), a blank and a phrase
# that is matched, without regard to case, in the 40 characters around the
# hit; a line that begins with a number sign is a comment.  The exit status
# is 1 when a hit is left.

import glob
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# The sources that are not the Reference Manual.
OTHER = ("gs-", "cheat", "mathcheat", "GettingStarted",
         "strata-file-format")

# The sources that no document of the product is made from (the Strata File
# Formats are not shipped): no rule looks at them.
NOT_SHIPPED = ("strata-file-format",)


def load_checkstyle():
    """The module checkstyle.py, which knows where the plain text of a
    source is."""
    spec = importlib.util.spec_from_file_location(
        "checkstyle", os.path.join(HERE, "checkstyle.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


CS = load_checkstyle()


def in_scope(path):
    """True when a source is part of the Reference Manual."""
    return not os.path.basename(path).startswith(OTHER)


def default_files():
    """The sources of the Reference Manual, and then the other sources of
    the product, which only the rules E30 and E31 look at."""
    every = sorted(glob.glob(os.path.join(HERE, "src", "*.src")))
    return [p for p in every if in_scope(p)] + \
        [p for p in every if not in_scope(p) and
         not os.path.basename(p).startswith(NOT_SHIPPED)]


# ----------------------------------------------------------------- rules

FILLER = re.compile(
    r"\b(?:very|really|quite|simply|basically|easily|obviously|of course|"
    r"actually|certainly|clearly|merely|totally|extremely|fairly|rather|"
    r"pretty|surely|naturally|literally|essentially|virtually|just)\b",
    re.IGNORECASE)
# Uses of just that state a position or a degree and are not filler.
JUST_OK = re.compile(
    r"\bjust\s+(?:before|after|below|above|beyond|past|under|over|inside|"
    r"outside|as|enough|like|the same|one|two|three|a|an|once|now|then|"
    r"in case|to the|left|right|behind|next)\b", re.IGNORECASE)

# E30: software that acts like a person.  The subjects are the programs and
# the parts of a screen that the manual names as if they acted; the verbs are
# those of speech, thought and intention.  A document, a table or a field
# that "says" what it holds is not software and is not looked at.
PERSON_VERBS = (r"(?:says|asks|tells|wants|knows|tries|decides|refuses|"
                r"understands|remembers|forgets|thinks|expects|assumes|"
                r"prefers|likes|helps|sees|notices|guesses|hopes|insists|"
                r"learns)")
SOFT_SUBJECT = (r"(?:Strata|Editor|Document Manager|Printer|Viewer|"
                r"programs?|panel|box|bar|install script|script|system|"
                r"machine|TeX|LaTeX|terminal|checker|spell check|thesaurus|"
                r"server|ghostscript|lp|(?:message|status|hint|bottom|top|"
                r"first|second) line|pop-up|footer|notices?|messages?|"
                r"banner|Setup|Preview|Error Log|Quit|Macro|Retrieve|"
                r"Delete|Save|Replace|Search)")
PERSON = re.compile(
    r"\b" + SOFT_SUBJECT + r"(?: titled [A-Z][A-Za-z ]+?)?\s+"
    r"(?:also\s+|then\s+|only\s+|never\s+|always\s+|first\s+)?" +
    PERSON_VERBS + r"\b|"
    r"\b[Ii]t\s+(?:also\s+|then\s+|only\s+|never\s+|always\s+)?" +
    PERSON_VERBS + r"\b")
# E31: the word bug.
BUG = re.compile(r"\b(?:bugs?|buggy)\b", re.IGNORECASE)

# E32: the pronoun for software with a verb of a person or of intention.  The
# base forms follow a modal verb ("it would not help"); the third person
# forms of E30 follow they too ("they say").
PERSON_BASE = (r"(?:ask|say|tell|want|know|try|decide|refuse|understand|"
               r"remember|forget|think|expect|assume|prefer|like|help|see|"
               r"notice|guess|hope|insist|learn|care|warn|complain|choose|"
               r"pick|ignore|recognize)")
PERSON_EXTRA = (r"(?:allows|lets|offers|ignores|recognizes|warns|"
                r"complains|cares|chooses|picks|insists)")
MODAL = (r"(?:would|will|can|cannot|may|might|could|must|should|"
         r"does not|do not|did not)")
ADVERB = r"(?:also|then|only|never|always|first|still)"
PRONOUN_PERSON = re.compile(
    r"\b(?:[Ii]t|[Tt]hey)\s+(?:" + ADVERB + r"\s+)*(?:" + MODAL +
    r"\s+)?(?:" + ADVERB + r"\s+)*(?:" + PERSON_BASE + r"|" +
    PERSON_VERBS + r"|" + PERSON_EXTRA + r")\b|"
    r"\b[Tt]hey\s+(?:" + ADVERB + r"\s+)*" + PERSON_BASE + r"\b")
SOFT_MODAL_PERSON = re.compile(
    r"\b" + SOFT_SUBJECT + r"s?\s+" + MODAL + r"\s+" + PERSON_BASE + r"\b")
SOFT_BEFORE = re.compile(r"\b" + SOFT_SUBJECT + r"\b")


CLAUSE_WORDS = ("and", "but", "so", "if", "when", "because", "that", "then",
                "while", "as", "until", "once", "where", "which", "though",
                "although", "since", "before", "after", "or", "nor", "unless",
                "whether", "than")


def is_subject(masked, pos):
    """True when the pronoun at pos begins a clause, so that it is the subject
    of the verb after it and not the object of a verb before it ("remove it
    like any character")."""
    before = masked[max(0, pos - 12):pos].rstrip(" \t")
    if before == "" or before[-1] in ".!?:;,(-|\n":
        return True
    word = re.search(r"([A-Za-z]+)$", before)
    return word is not None and word.group(1).lower() in CLAUSE_WORDS


def soft_before(masked, pos):
    """True when the sentence of pos, or the one before it, has software for
    its subject (a name of E30 in it): the antecedent of a pronoun."""
    window = masked[max(0, pos - 400):pos]
    cuts = [m.end() for m in re.finditer(r"[.!?:](?:\s+|$)|\n\n", window)]
    if len(cuts) >= 2:
        window = window[cuts[-2]:]
    return SOFT_BEFORE.search(window) is not None

NARRATION = re.compile(
    r"\b(?:we|our|ours|let us|let's|you will|you'll|you would|imagine|"
    r"suppose|as you can see|you may notice|"
    r"remember that|keep in mind|please|feel free|don't forget|by the way|"
    r"incidentally|as mentioned|as we)\b", re.IGNORECASE)

NOTICE = re.compile(r"(?<![Aa] )(?<![Tt]he )(?<![Aa]n )(?<![Tt]his )"
                    r"\b(?:notice that|note that)\b", re.IGNORECASE)

STALE = re.compile(
    r"\b(?:not yet|planned|future|todo|fixme|formerly|earlier versions?|"
    r"later versions?|old program|old version of strata|will be|will not|"
    r"will)\b", re.IGNORECASE)
SOON = re.compile(r"(?<![Aa]s\s)\bsoon\b", re.IGNORECASE)
USED_TO = re.compile(r"(?<!\bis )(?<!\bare )(?<!\bbe )(?<!\bwas )"
                     r"(?<!\bwere )(?<!\bbeen )(?<!\bnot )\bused to\b",
                     re.IGNORECASE)
NEW_IN = re.compile(r"\bnew in\b")

HEDGE = re.compile(
    r"\b(?:might|perhaps|probably|possibly|somewhat|seems?|apparently|"
    r"presumably|tends? to|more or less)\b", re.IGNORECASE)

# E19: the manual points to other places with See, never with See also.
SEE_ALSO = re.compile(r"\bsee also\b", re.IGNORECASE)

CONTRACTION = re.compile(r"\b[A-Za-z]+n't\b|\b(?:it|that|there|here|what|"
                         r"who|he|she)'s\b|\b[A-Za-z]+'(?:ll|re|ve|d)\b",
                         re.IGNORECASE)

UNIT_GLUED = re.compile(
    r"(?<![\w.{\\])\d+(?:\.\d+)?(?:mm|cm|pt|px|in|dpi|KB|MB|GB|ms|s|%)"
    r"(?![\w])")
THOUSANDS = re.compile(r"(?<![\w.,])\d{1,3}(?:,\d{3})+(?![\d,])")

TYPED_REF = re.compile(
    r"\b(?:[Cc]hapters?|[Aa]ppendix|[Aa]ppendices)\s+(?:\d+|[A-F]\b)")

WORD_BEFORE_REF = re.compile(
    r"\b[Aa]ppendi(?:x|xes|ces)[ \t]+\{[xXn]:")

THIS_IT = re.compile(
    r"(?:^|(?<=[.?!:])\s+)This\s+(?:is|are|was|means|does|do|has|"
    r"have|can|may|must|makes|lets|gives|shows|writes|reads|works|"
    r"happens|should|would|could|will|means|allows|causes|keeps|uses|"
    r"takes|needs|also|only|then)\b")

ANNOUNCE = re.compile(
    r"^(?:This|The following|In this)\s+(?:section|part)\b|"
    r"^(?:Here|Now)\b")

ABBREV = ("e.g", "i.e", "etc", "vs", "No", "Fig", "Dr", "Mr", "cf", "al",
          "approx", "min", "max", "resp", "incl", "esp")


# E26: the names of systems, formats and programs: the right spelling, and
# the regular expression of the spellings that are wrong in plain text.  A
# word that is part of a file name or a path (.pdf, pictures/x.png) is not a
# name of the format: the patterns do not match after a dot, a slash or a
# hyphen.
NAME_BEFORE = r"(?<![\w./~-])"
NAME_AFTER = r"(?![\w/-]|\.\w)"
NAMES = (
    ("LaTeX", r"\b(?:latex|Latex|LATEX|LaTex|La-TeX)\b"),
    ("TeX", r"\b(?:tex|Tex|TEX)\b"),
    ("pdfTeX", r"\b(?:pdftex|PdfTeX|pdfTex|PDFTeX|pdfTEX)\b"),
    ("XeTeX", r"\b(?:xetex|Xetex|XeTex|XETEX)\b"),
    ("PDF", NAME_BEFORE + r"(?:pdf|Pdf)\b"),
    ("DVI", NAME_BEFORE + r"(?:dvi|Dvi)\b"),
    ("ASCII", r"\b(?:ascii|Ascii)\b"),
    ("Unicode", r"\b(?:unicode|UNICODE)\b"),
    ("UTF-8", r"\b(?:utf8|UTF8|utf-8|Utf-8|UTF 8)\b"),
    ("X11", r"\b(?:x11|X-11|X 11)\b"),
    ("PostScript", r"\b(?:Postscript|postscript|POSTSCRIPT|Post-Script)\b"),
    ("Ghostscript", r"\b(?:GhostScript|ghostscript|Ghost Script)\b"),
    ("PNG", NAME_BEFORE + r"(?:png|Png)\b"),
    ("CUPS", r"\b(?:cups|Cups)\b"),
    ("Linux", r"\b(?:linux|LINUX)\b"),
    ("Debian", r"\b(?:debian|DEBIAN)\b"),
    ("Ubuntu", r"\b(?:ubuntu|UBUNTU)\b"),
    ("Latin-1", r"\b(?:latin-1|LATIN-1|Latin1|latin1|Latin 1)\b"),
    ("macOS", r"\b(?:MacOS|Mac OS|mac os|MAC OS|OS X)\b"),
    ("TrueType", r"\b(?:Truetype|truetype|TRUETYPE|True Type)\b"),
)
# A name inside a longer name is another thing: a message label such as
# latex-marker-off, a package such as texlive-latex-recommended, a file such
# as pdftex.map, an option such as --tex-only.
NAME_RULES = [(re.compile(NAME_BEFORE + "(?:" + rx.replace(NAME_BEFORE, "")
                          .replace(r"\b", "") + ")" + NAME_AFTER), right)
              for right, rx in NAMES]


def load_mkmanual():
    """The module mkmanual.py, whose rule says which occurrence of LaTeX or
    TeX the generator turns into a marker."""
    spec = importlib.util.spec_from_file_location(
        "mkmanual", os.path.join(HERE, "mkmanual.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MK = load_mkmanual()
WORD_NAME = re.compile(r"(?<![A-Za-z])(?:La)?TeX(?![A-Za-z])")


def mask_text(text, start, end):
    """The text between two offsets with every part that is not plain text
    (the commands of the generator that are not text and the quoted words)
    replaced by x, so that the offsets and the line breaks stay."""
    out = list(text[start:end])
    for m in CS.PROTECTED.finditer(text, start, end):
        for i in range(m.start() - start, m.end() - start):
            if out[i] != "\n":
                out[i] = "x"
    return "".join(out)


def load_allow():
    """The allowed places of checkeditorial.allow: (rule, phrase)."""
    path = os.path.join(HERE, "checkeditorial.allow")
    out = []
    if os.path.exists(path):
        with open(path, encoding="ascii") as f:
            for line in f:
                line = line.rstrip("\n")
                if line == "" or line.startswith("#"):
                    continue
                rule, _, phrase = line.partition(" ")
                out.append((rule, phrase.lower()))
    return out


def load_terms():
    """The wrong terms of docs/MANUAL-TERMS.txt: (regex, right, reason)."""
    path = os.path.join(HERE, "..", "docs", "MANUAL-TERMS.txt")
    out = []
    if not os.path.exists(path):
        return out
    with open(path, encoding="ascii") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line.startswith("WRONG "):
                continue
            parts = [p.strip() for p in line[6:].split(" => ")]
            if len(parts) < 2:
                continue
            reason = parts[2] if len(parts) > 2 else ""
            out.append((re.compile(parts[0]), parts[1], reason))
    return out


def allowed(allow, rule, ctx):
    """True when an allowed place covers the context of a hit."""
    low = ctx.lower()
    return any(r == rule and p in low for r, p in allow)


def sentences(masked):
    """The (offset, text) of the sentences of a masked region."""
    out = []
    pos = 0
    for m in re.finditer(r"(?<=[a-z0-9)}x\"][.?!])\s+(?=[A-Z])|\n\s*\n",
                         masked):
        out.append((pos, masked[pos:m.start()]))
        pos = m.end()
    out.append((pos, masked[pos:]))
    return out


def heading_lines(lines):
    """The (line number, text) of the headlines of a source."""
    out = []
    for i, line in enumerate(lines):
        m = re.match(r"\.h([1-4])\s+(.*?)(?:\s+\[[A-Za-z0-9._-]+\])?\s*$",
                     line)
        if m is not None:
            out.append((i + 1, m.group(2), int(m.group(1))))
    return out


def check_lists(lines, base):
    """E11: the lists of a source whose items end differently."""
    hits = []
    in_code = False
    items = []
    cur = None

    def flush():
        nonlocal items
        if len(items) >= 2:
            ends = [t.rstrip().endswith((".", "?", "!")) for _, t in items]
            semi = [t.rstrip().endswith((";", ",")) for _, t in items[:-1]]
            if len(semi) > 0 and all(semi) and ends[-1]:
                ends = [True] * len(ends)
            if any(ends) and not all(ends):
                hits.append((items[0][0], "E11", "list",
                             lines[items[0][0] - 1]))
        items = []

    for i, line in enumerate(lines):
        if line.startswith(".code"):
            in_code = True
        elif line.startswith(".end"):
            in_code = False
        if in_code or line.startswith("#"):
            continue
        m = re.match(r"(?:- |\d+\. )(.*)", line)
        if m is not None:
            if cur is not None:
                items.append(cur)
            cur = [i + 1, m.group(1)]
            cur = (cur[0], cur[1])
        elif cur is not None and line.strip() and \
                re.match(r"[ a-z{(\"]", line) is not None:
            cur = (cur[0], cur[1] + " " + line.strip())
        else:
            if cur is not None:
                items.append(cur)
                cur = None
            flush()
    if cur is not None:
        items.append(cur)
    flush()
    return hits


def split_cells_pos(line):
    """The (text, start, end) of the cells of a table row: the split of the
    generator, at the bars that are not inside a command."""
    out = []
    depth = 0
    start = 0
    i = 0
    while i < len(line):
        two = line[i:i + 2]
        if two in ("{{", "}}"):
            i += 2
            continue
        if line[i] == "{":
            depth += 1
        elif line[i] == "}":
            depth -= 1
        elif line[i] == "|" and depth <= 0:
            out.append((line[start:i], start, i))
            start = i + 1
        i += 1
    out.append((line[start:], start, len(line)))
    return out


def piece(j, text, start, end):
    """A cell: its text, and the line and the end offset of its last
    piece."""
    lead = len(text) - len(text.lstrip())
    body = text.strip()
    return [body, j, start + lead + len(body)]


def table_rows(lines):
    """The tables of a source, with the rows made as the generator makes
    them: a list of (first line number, rows), each row a list of cells,
    each cell a list [text, line index, end offset of the last piece of
    the cell in that line]."""
    out = []
    i = 0
    while i < len(lines):
        m = re.match(r"\.(?:table|rows)((?:\s+\d+)+)\s*$", lines[i])
        if m is not None:
            ncols = len(m.group(1).split())
            first = i + 2
            rows = []
            j = i + 1
            while j < len(lines) and not lines[j].startswith(".end"):
                line = lines[j]
                j += 1
                if line.strip() == "" or line.startswith("#"):
                    continue
                pieces = split_cells_pos(line)
                if line[:1] == " " and rows:
                    row = rows[-1]
                    plain = " | ".join(c[0] for c in row) + " " + \
                        line.strip()
                    if len(pieces) > 1 and \
                            len(split_cells_pos(plain)) == ncols:
                        c = row[-1]
                        text = pieces[0]
                        if text[0].strip() != "":
                            c[0] = (c[0] + " " + text[0].strip()).strip()
                            c[1] = j - 1
                            c[2] = piece(j - 1, *text)[2]
                        for text in pieces[1:]:
                            row.append(piece(j - 1, *text))
                    elif len(pieces) > 1:
                        for k, text in enumerate(pieces):
                            if text[0].strip() == "":
                                continue
                            while k >= len(row):
                                row.append(["", j - 1, 0])
                            cell = row[k]
                            body = piece(j - 1, *text)
                            cell[0] = (cell[0] + " " + body[0]).strip()
                            cell[1] = body[1]
                            cell[2] = body[2]
                    else:
                        c = row[-1]
                        body = piece(j - 1, *pieces[0])
                        c[0] = (c[0] + " " + body[0]).strip()
                        c[1] = body[1]
                        c[2] = body[2]
                else:
                    rows.append([piece(j - 1, *p) for p in pieces])
            out.append((first, rows))
            i = j
        i += 1
    return out


def sentence_like(cell):
    """True when a table cell is a sentence: it ends with a period or has
    two sentences."""
    c = re.sub(r"\{[a-zA-Z]+:(?:[^{}]|\{\{|\}\})*\}", "x", cell).strip()
    return c.endswith((".", "?", "!")) or re.search(r"[a-z]\.\s+[A-Z]", c)


def cell_status(text):
    """"ok" for a cell that ends a sentence, "short" for a cell that is a
    value (two words or fewer), "open" for a cell that has no final
    period."""
    c = text.strip()
    if c.endswith((".", "?", "!", ".)", ".\"", ":")):
        return "ok"
    plain = re.sub(r"\{[a-zA-Z]+:(?:[^{}]|\{\{|\}\})*\}", "x", c)
    if len(plain.split()) <= 2:
        return "short"
    return "open"


def table_hits(lines):
    """The (line index, end offset) of every table cell that lacks the
    final period that the other cells of its column have: the column has
    at least two cells that end a sentence and at least as many as the
    cells that do not."""
    out = []
    for first, rows in table_rows(lines):
        width = max(len(r) for r in rows) if rows else 0
        for col in range(1, width):
            cells = [r[col] for r in rows[1:] if len(r) > col and
                     r[col][0] != ""]
            status = [cell_status(c[0]) for c in cells]
            oks = status.count("ok")
            opens = status.count("open")
            if oks >= 2 and oks >= opens and opens > 0:
                for c, s in zip(cells, status):
                    if s == "open":
                        out.append((c[1], c[2], c[0][-30:]))
    return out


def check_tables(lines):
    """E15: the cells of a table that lack the final period of their
    column."""
    return [(ln + 1, "E15", "no period", lines[ln])
            for ln, end, text in table_hits(lines)]


def check_widths(lines):
    """E27: a .table whose widths add up to more than MK.table_room."""
    hits = []
    for ln, line in enumerate(lines):
        m = re.match(r"\.table((?:\s+\d+)+)\s*$", line)
        if m is None:
            continue
        widths = [int(w) for w in m.group(1).split()]
        room = MK.table_room(len(widths))
        if sum(widths) > room:
            hits.append((ln + 1, "E27",
                         "the widths add up to %d, the room is %d"
                         % (sum(widths), room), line))
    return hits


def check_stepheads(lines):
    """E29: a .table whose first row begins with a number alone."""
    hits = []
    for ln, line in enumerate(lines):
        if re.match(r"\.table\s", line) is None:
            continue
        j = ln + 1
        while j < len(lines) and (lines[j].strip() == "" or
                                  lines[j].startswith("#")):
            j += 1
        if j < len(lines) and re.match(r"\s*[0-9]+\s*\|", lines[j]):
            hits.append((j + 1, "E29", "the first row is a header row",
                         lines[j]))
    return hits


def check_rows(lines):
    """E16: the rows of a table that are the rest of the row before."""
    hits = []
    for first, rows in table_rows(lines):
        for ri in range(1, len(rows)):
            row = rows[ri]
            prev = rows[ri - 1]
            full = [c for c in row if c[0] != ""]
            if len(full) != 1 or len(row) < 2:
                continue
            k = row.index(full[0])
            text = full[0][0]
            if k >= len(prev) or prev[k][0] == "" or \
                    re.match(r"[a-z(]", text) is None:
                continue
            if prev[k][0].endswith((".", "?", "!", ":", ";", ")", "\"",
                                    "}")):
                continue
            hits.append((full[0][1] + 1, "E16", "row", lines[full[0][1]]))
    return hits


# E24: the order of the rows of the tables of messages.  The Messages chapter
# promises that a message is found by the first words that the screen showed,
# so the rows of its tables are sorted by the text of the first cell.
SORTED_HEADS = ("message", "text", "title and text", "message of tex")


def sort_key(cell):
    """The text of a first cell as the order of the rows sees it."""
    cell = re.sub(r"\{[a-z]:([^{}]*)\}", r"\1", cell)
    cell = cell.replace("{{", "{").replace("}}", "}")
    cell = re.sub(r"[^a-z0-9<> ]+", "", cell.lower())
    return re.sub(r"\s+", " ", cell).strip()


def check_sorted(lines, base):
    """E24: the rows of a table of messages that come before the row that
    is before them in the order of the text."""
    if not base.startswith("messages") or base == "messages.src":
        return []
    hits = []
    for first, rows in table_rows(lines):
        if len(rows) < 3 or rows[0][0][0].lower() not in SORTED_HEADS:
            continue
        prev = None
        for row in rows[1:]:
            key = sort_key(row[0][0])
            if prev is not None and key < prev:
                hits.append((row[0][1] + 1, "E24", "not sorted",
                             lines[row[0][1]]))
            prev = key
    return hits


# E20: the pattern of the entry of a marker.  Every marker has a second-level
# section whose label begins with marker- and whose third-level sections
# say what the marker shows on the screen, what it prints and where it
# meets an edge of the editor.  The sections of the conditional marker and of
# the Include marker are written as procedures and are not entries.
ENTRY_PARTS = ("On the screen", "When printed", "Edges")
ENTRY_EXEMPT = ("marker-if", "marker-include", "marker-figure",
                "marker-index", "marker-merge", "marker-variables",
                "marker-special", "marker-compare", "marker-latex-picture")


def check_entries(lines, base):
    """E20: the sections of a marker that lack a part of the pattern."""
    hits = []
    entry = None
    parts = []

    def close():
        if entry is not None:
            for need in ENTRY_PARTS:
                if need not in parts:
                    hits.append((entry[0], "E20",
                                 "no section " + need, lines[entry[0] - 1]))

    for i, line in enumerate(lines, 1):
        m = re.match(r"\.h([23]) (.*?)(?: \[([^\]]+)\])?$", line)
        if m is None:
            continue
        if m.group(1) == "2":
            close()
            label = m.group(3) or ""
            if label.startswith("marker-") and \
                    not label.startswith(ENTRY_EXEMPT):
                entry = (i, label)
            else:
                entry = None
            parts = []
        elif entry is not None:
            parts.append(m.group(2).strip())
    close()
    return hits


# E21: the pattern of the entry of a command of the File menu.  The six
# commands have a section for the panel, a section for the edges and, last, a
# section that is called See and lists the other places.
FILE_COMMANDS = ("file-save", "file-retrieve", "file-new", "file-delete",
                 "file-import", "file-export")


def check_commands(lines):
    """E21: the commands of the File menu that lack a part of the pattern."""
    hits = []
    entry = None
    titles = []

    def close():
        if entry is None:
            return
        text = " ".join(titles).lower()
        for need, word in (("a section for the panel", "panel"),
                           ("a section for the edges", "edges")):
            if word not in text:
                hits.append((entry, "E21", "no " + need, lines[entry - 1]))
        if not titles or titles[-1] != "See":
            hits.append((entry, "E21", "the last section is not See",
                         lines[entry - 1]))

    for i, line in enumerate(lines, 1):
        m = re.match(r"\.h([23]) (.*?)(?: \[([^\]]+)\])?$", line)
        if m is None:
            continue
        if m.group(1) == "2":
            close()
            entry = i if (m.group(3) or "") in FILE_COMMANDS else None
            titles = []
        elif entry is not None:
            titles.append(m.group(2).strip())
    close()
    return hits


def check_spans(lines):
    """E17: a command that stays open at the end of a table line whose next
    line begins with a bar."""
    hits = []
    in_table = False
    for i, line in enumerate(lines):
        if re.match(r"\.(?:table|rows)\b", line):
            in_table = True
            continue
        if line.startswith(".end"):
            in_table = False
        if not in_table or line.startswith("#"):
            continue
        depth = 0
        k = 0
        while k < len(line):
            if line[k:k + 2] in ("{{", "}}"):
                k += 2
                continue
            if line[k] == "{":
                depth += 1
            elif line[k] == "}":
                depth -= 1
            k += 1
        nxt = lines[i + 1] if i + 1 < len(lines) else ""
        if depth > 0 and nxt[:1] == " " and nxt.lstrip().startswith("|"):
            hits.append((i + 1, "E17", "open command", line))
    return hits


XREF = re.compile(r"\{[xX]:[^{}]*\}")


def xref_spans(text, start, end):
    """The (start, end) offsets of the {x:...} references in a region that
    stand inside parentheses."""
    out = []
    masked = mask_text(text, start, end)
    for m in XREF.finditer(text, start, end):
        before = masked[:m.start() - start]
        # The parentheses of earlier references are masked, so the count
        # is of the parentheses of the plain text and of the program font
        # that is not text.
        depth = before.count("(") - before.count(")")
        if depth > 0 and "\n\n" not in before[before.rfind("("):]:
            out.append((m.start(), m.end()))
    return out


def check_file(path, allow, terms, sentence_index, capital_ok):
    """The list of (line number, rule, hit, line) of one file."""
    with open(path, encoding="ascii", errors="replace") as f:
        text = f.read()
    base = os.path.basename(path)
    lines = text.split("\n")
    hits = []
    full = in_scope(path)

    def add(num, rule, word, ctx):
        if not allowed(allow, rule, ctx):
            hits.append((num, rule, word, lines[num - 1]))

    for start, end, kind in CS.regions(text, "text.src"):
        if kind != "text":
            continue
        masked = mask_text(text, start, end)
        for m in PERSON.finditer(masked):
            add(CS.line_of(text, start + m.start()), "E30", m.group(0),
                masked[max(0, m.start() - 40):m.end() + 40])
        for m in PRONOUN_PERSON.finditer(masked):
            if PERSON.match(masked, m.start()) is not None:
                continue
            if is_subject(masked, m.start()) and \
                    soft_before(masked, m.start()):
                add(CS.line_of(text, start + m.start()), "E32", m.group(0),
                    masked[max(0, m.start() - 40):m.end() + 40])
        for m in SOFT_MODAL_PERSON.finditer(masked):
            add(CS.line_of(text, start + m.start()), "E32", m.group(0),
                masked[max(0, m.start() - 40):m.end() + 40])
        if not full:
            continue
        for rx, rule in ((FILLER, "E1"), (NARRATION, "E2"), (NOTICE, "E2"),
                         (STALE, "E3"), (SOON, "E3"),
                         (USED_TO, "E3"), (NEW_IN, "E3"), (HEDGE, "E4"),
                         (CONTRACTION, "E5"), (SEE_ALSO, "E23")):
            for m in rx.finditer(masked):
                ctx = masked[max(0, m.start() - 40):m.end() + 40]
                if rule == "E1" and m.group(0).lower() == "just" and \
                        JUST_OK.match(masked, m.start()) is not None:
                    continue
                add(CS.line_of(text, start + m.start()), rule,
                    m.group(0), ctx)
        for m in UNIT_GLUED.finditer(masked):
            add(CS.line_of(text, start + m.start()), "E6", m.group(0),
                masked[max(0, m.start() - 40):m.end() + 40])
        for m in THOUSANDS.finditer(masked):
            add(CS.line_of(text, start + m.start()), "E6", m.group(0),
                masked[max(0, m.start() - 40):m.end() + 40])
        for rx, right, reason in terms:
            for m in rx.finditer(masked):
                add(CS.line_of(text, start + m.start()), "E8",
                    "%s (use %s)" % (m.group(0), right),
                    masked[max(0, m.start() - 40):m.end() + 40])
        for rx, right in NAME_RULES:
            for m in rx.finditer(masked):
                add(CS.line_of(text, start + m.start()), "E26",
                    "%s (write %s)" % (m.group(0), right),
                    masked[max(0, m.start() - 40):m.end() + 40])
        for m in WORD_NAME.finditer(masked):
            if MK.name_at(masked, m.start()) is None:
                add(CS.line_of(text, start + m.start()), "E25",
                    "%s is not made a marker" % m.group(0),
                    masked[max(0, m.start() - 40):m.end() + 40])
        for m in TYPED_REF.finditer(masked):
            add(CS.line_of(text, start + m.start()), "E9", m.group(0),
                masked[max(0, m.start() - 40):m.end() + 40])
        for a, b in xref_spans(text, start, end):
            add(CS.line_of(text, a), "E18", text[a:b],
                text[max(0, a - 40):b + 40])
        for m in WORD_BEFORE_REF.finditer(text, start, end):
            add(CS.line_of(text, m.start()), "E19", m.group(0),
                text[max(0, m.start() - 40):m.end() + 40])
        for m in re.finditer(r"\b(?:the|a|an|of the|section|chapter|appendix)\s+"
                             r"(?=\{[nx]:)", text[start:end], re.IGNORECASE):
            add(CS.line_of(text, start + m.start()), "E22", m.group(0),
                text[max(0, start + m.start() - 20):start + m.end() + 30])
        # E7: the spaces after a period and elsewhere.
        flat = masked
        for m in re.finditer(r"[^\s|]( {3,})[^\s|]", flat):
            add(CS.line_of(text, start + m.start()), "E7", "3 spaces",
                flat[max(0, m.start() - 40):m.end() + 40])
        for m in re.finditer(r"([a-z0-9)\"x]{2}[.?!]) (?=[A-Z][a-z])",
                             flat):
            word = flat[:m.end()].rsplit(None, 1)[-1].rstrip(".?!")
            if word in ABBREV or word.endswith(ABBREV):
                continue
            add(CS.line_of(text, start + m.start()), "E7",
                "one space after a period",
                flat[max(0, m.start() - 40):m.end() + 40])
        for m in re.finditer(r"(?<![.?!:])(?<=[a-wyz,;])( {2})(?=[a-z])",
                             flat):
            add(CS.line_of(text, start + m.start()), "E7",
                "two spaces inside a sentence",
                flat[max(0, m.start() - 40):m.end() + 40])
        # E12 and E13: sentences.
        for off, sent in sentences(masked):
            s = sent.strip()
            if THIS_IT.match(s) is not None:
                add(CS.line_of(text, start + off), "E12", s[:20], s)
            flatten = " ".join(s.split())
            if len(flatten) >= 70 and "|" not in flatten and \
                    flatten.count("x") * 2 < len(flatten):
                orig = " ".join(text[start + off:start + off +
                                     len(sent)].split())
                sentence_index.setdefault(orig, []).append(
                    (base, CS.line_of(text, start + off)))
    # E31: the word bug, anywhere in the source.
    for i, line in enumerate(lines):
        for m in BUG.finditer(line):
            if line.startswith("#"):
                continue
            add(i + 1, "E31", m.group(0), line)
    if not full:
        hits.sort()
        return hits
    # E8 holds in the headlines, the index entries and the captions too.
    in_code = False
    for i, line in enumerate(lines):
        if line.startswith(".code"):
            in_code = True
        elif line.startswith(".end"):
            in_code = False
        m = re.match(r"\.(?:h[1-4]|ix|pic\s+\S+\s+\S+)\s+(.*)", line)
        if in_code or m is None:
            continue
        seg = m.group(1)
        seg = re.sub(r"\s+\[[A-Za-z0-9._-]+\]\s*$", "", seg)
        pos = m.start(1)
        masked = mask_text(line, pos, pos + len(seg))
        for rx, right, reason in terms:
            for t in rx.finditer(masked):
                add(i + 1, "E8", "%s (use %s)" % (t.group(0), right),
                    masked)
        if not line.startswith(".ix"):
            # The terms of the index are plain words, such as the names of
            # commands: only the other places are looked at.
            for rx, right in NAME_RULES:
                for t in rx.finditer(masked):
                    add(i + 1, "E26", "%s (write %s)" % (t.group(0), right),
                        masked)
    # E10: headlines.
    for num, head, level in heading_lines(lines):
        if level == 1:
            continue
        words = re.sub(r"\{[a-zA-Z]+:(?:[^{}]|\{\{|\}\})*\}", "x",
                       head).split()
        for k, w in enumerate(words):
            w = w.strip("(),:;\"'")
            if w.endswith("'s"):
                w = w[:-2]
            if k == 0 or len(w) < 2 or not w[0].isupper() or \
                    w.upper() == w or re.search(r"[0-9_+-]", w) is not None:
                continue
            if w in capital_ok:
                continue
            add(num, "E10", w, lines[num - 1])
    # E14: the first sentence under a headline.
    for i, line in enumerate(lines):
        if re.match(r"\.h[1-4]\s", line):
            j = i + 1
            while j < len(lines) and (lines[j].startswith(".ix") or
                                      lines[j].startswith("#") or
                                      lines[j].strip() == ""):
                j += 1
            if j < len(lines) and not lines[j].startswith("."):
                first = lines[j]
                if ANNOUNCE.match(first) is not None:
                    add(j + 1, "E14", first[:30], first)
    for item in check_lists(lines, base):
        if not allowed(allow, "E11", item[3]):
            hits.append(item)
    for item in check_tables(lines):
        if not allowed(allow, "E15", item[3]):
            hits.append(item)
    for item in check_widths(lines):
        if not allowed(allow, "E27", item[3]):
            hits.append(item)
    for item in check_stepheads(lines):
        if not allowed(allow, "E29", item[3]):
            hits.append(item)
    for item in check_rows(lines) + check_spans(lines) + \
            check_entries(lines, base) + check_commands(lines) + \
            check_sorted(lines, base):
        if not allowed(allow, item[1], item[3]):
            hits.append(item)
    hits.sort()
    return hits


def capital_vocabulary(files):
    """The capitalised words that may stand in a headline after the first
    word: a word that is written with a capital letter in the middle of a
    sentence of running text somewhere, or in a table, a term, a note or a
    keycap name.  An ordinary word is never written so."""
    ok = set()
    for path in files:
        with open(path, encoding="ascii", errors="replace") as f:
            text = f.read()
        base = os.path.basename(path)
        for start, end, kind in CS.regions(text, "text.src"):
            if kind != "text":
                continue
            masked = mask_text(text, start, end)
            for m in re.finditer(r"(?<![.?!:|])\s([A-Z][A-Za-z0-9-]+)\b",
                                 masked):
                prior = masked[:m.start()].rstrip()[-1:]
                if prior in ".?!:":
                    continue
                ok.add(m.group(1))
        for line in text.split("\n"):
            m = re.match(r"\.(?:ix|term)\s+(.*)", line)
            if m is not None:
                for w in re.findall(r"\b[A-Z][A-Za-z0-9-]+\b",
                                    m.group(1).split("|")[0]):
                    pass
    return ok


def main(argv):
    files = argv[1:] or default_files()
    allow = load_allow()
    terms = load_terms()
    capital_ok = capital_vocabulary(files)
    total = 0
    per_rule = {}
    per_file = {}
    sentence_index = {}
    for path in files:
        for num, rule, word, line in check_file(path, allow, terms,
                                                sentence_index, capital_ok):
            total += 1
            name = os.path.basename(path)
            per_rule[rule] = per_rule.get(rule, 0) + 1
            per_file[name] = per_file.get(name, 0) + 1
            print("%s:%d: %s [%s] %s" % (name, num, rule, word,
                                         line.strip()[:78]))
    for sent, places in sorted(sentence_index.items()):
        if len(places) >= 2:
            where = [p for p in places]
            if not any(allowed(allow, "E13", sent) for _ in [0]):
                for name, num in where[1:]:
                    total += 1
                    per_rule["E13"] = per_rule.get("E13", 0) + 1
                    per_file[name] = per_file.get(name, 0) + 1
                    print("%s:%d: E13 [same as %s:%d] %s" % (
                        name, num, where[0][0], where[0][1], sent[:60]))
    print("checkeditorial: %d hit(s) in %d file(s)" % (total, len(files)))
    if os.environ.get("CHECKEDITORIAL_COUNT"):
        for rule in sorted(per_rule):
            print("  %s %d" % (rule, per_rule[rule]))
        for name in sorted(per_file, key=lambda n: -per_file[n]):
            print("  %-26s %d" % (name, per_file[name]))
    return 1 if total > 0 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
