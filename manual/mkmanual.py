#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program writes Strata documents (the .DOC format of docs/DOC-FORMAT.txt)
# from plain text sources, so that the manual of Strata is itself a Strata
# document that uses the features it describes.  The sources are in
# manual/src and manual/samples; the result of one source is one document.
#
#     python3 -I manual/mkmanual.py SOURCE.src OUTPUT.doc
#
# The manual describes what the user sees and does, never how the programs are
# built: a sentence about how a program does something is deleted, or put
# between ".if .STRATA_BUILD_TYPE # release" and ".endif" when a developer
# needs it (see manual/README.txt and manual/checkimpl.py).
#
# The source is plain ASCII text.  A line that begins with a dot is a
# directive; any other line belongs to a paragraph, and a blank line ends it.
# A line that begins with a number sign is a comment.
#
#   .doc TEXT          The description of the document (the title).
#   .h1 TEXT  .h2 TEXT  .h3 TEXT    A section headline of that level.  The
#                      headline may end with a label in the form [name].
#   .contents LEVEL    The level of the table of contents that the print of the
#                      document makes (0 to 99); the default is 0, none.
#   .center            The next paragraph is centred.
#   .fontmarkers MODE  The display mode of the Font markers of the document:
#                      hidden (the default: the words of the program font
#                      are plain words on a terminal, with no [FONT]) or
#                      short (the markers are shown, as in the sample
#                      documents).
#   .code ... .end     Lines that are kept as they are, in a program font.
#   .table W1 W2 ...   Rows of cells separated by a bar; the widths are in
#                      character cells.  The first row is the heading.  The
#                      table ends with .end.
#                      A line of a table or of a panel that begins with a
#                      space continues the line before it.  When it holds
#                      a bar, the cells of it continue the cells of the line
#                      before, one by one: " | more text" continues the
#                      second cell and " more | more" the first two.  But
#                      when the two lines joined by a space make exactly
#                      the cells of the table, they are simply joined.
#                      Every table has the same borders, set by the
#                      generator and not by the author: the heading row has
#                      a single line at its top, its left, its right and
#                      between its columns, a double line at its bottom and
#                      bold text; the other rows have single lines at their
#                      left, right, between the columns and bottom and none
#                      at their top.  A cell of the heading keeps the bold
#                      when it holds {i:...}, {u:...} or {b:...}.
#   .panels W1 W2 ...  One block of side by side panels of text (a leaflet);
#                      .next ends a panel and .end ends the block.  In a
#                      panel a line "= TEXT" is bold, "KEYS | TEXT" is an
#                      entry that wraps under its text, "~ TEXT" is text as
#                      it is (a bar in it is a bar), and a blank line is a
#                      blank line.
#   .mathsyms FILE     The two sides of the sheet of the math symbols (six
#                      panels), made from the list FILE of the symbols (see
#                      manual/mkmathcheat.py).
#   .margins INCHES    The four page margins; or, with four numbers, the left,
#                      right, top and bottom margins.
#   .numbering off     Do not number the section headlines.
#   .linespace S       The line spacing of the panels (1.0 is single).
#   .keyscale S        The scale of the keycaps that follow (1.0 is normal).
#   .landscape         The document is printed sideways (before the first
#                      headline).
#   .pic FILE W TEXT   A picture of W inches and its caption.  The caption
#                      may end with a label in the form [name]; {fig:name}
#                      in the text of the same document is then the words
#                      "figure N" (with the chapter: "figure 5.2"), and
#                      {Fig:name} the same with a capital F.  A sentence
#                      that only says what a picture shows is replaced by a
#                      reference to the figure.
#   .space INCHES      A vertical whitespace marker.
#   .pagebreak         A page break marker.
#   .include FILE      Read the lines of FILE (in the same directory) here, so
#                      that a large chapter can be kept in several sources.
#   .part              The document is a PART for the Include marker of another
#                      document: it has no headlines, its text and tables
#                      stand directly in the document, and it is written as
#                      a document with no structure, which the Printer puts
#                      in at the place of the marker.  It must come before
#                      any text; a headline or a cross-reference to a
#                      section is an error in a part.
#   .includedoc NAME   An Include marker for the document NAME (a file in the
#                      directory of the document that holds the marker), in
#                      a block of its own.  The Printer reads the named part
#                      in when the document is printed or previewed, so the
#                      part is kept in a document of its own that can also be
#                      used alone.  Put it in the last block of its section.
#   .chapter N         The document is chapter N of the manual (a number, or a
#                      letter for an appendix).  In the single document the
#                      headlines hold no typed number: the numbering of the
#                      product makes the tags N, N.1, N.1.1 (the chapters
#                      are one numbering run, the appendices another, see
#                      the record BZG in docs/DOC-FORMAT.txt), and the
#                      chapters must follow each other.  The documents of the
#                      docs target, one for each chapter, type the numbers
#                      into the text of the headlines.  The numbers are
#                      known to the other documents of the manual.
#   .h4 TEXT           A headline of the fourth level.
#   .if EXPR           Conditional text: the paragraphs, tables and notes up
#   .else              to .else (or .endif) are printed only when the
#   .endif             expression is true, those after .else only when it is
#                      false.  EXPR is a variable, an operator and a value,
#                      such as ".STRATA_BUILD_TYPE # release" (operators = # <
#                      > <= >=, # is not equal; the rules are in src/cond.h).
#                      The directives write the If, Else and Endif markers
#                      around the text, so that the document holds all of it
#                      and the Printer chooses what is printed (the build
#                      of the manual gives .STRATA_BUILD_TYPE).  The rows of
#                      the contents, the index and the list of figures that
#                      point into conditional text are written inside the
#                      same markers.  A headline may be inside conditional
#                      text only when no later headline of the same parent
#                      comes after it outside the same .if: the numbers of
#                      the headlines are written into their text, so a
#                      section that is left out in the middle would leave a
#                      gap.  So put the sections that a release leaves out
#                      after the other sections of their parent, and close
#                      the .if right before the next headline that is
#                      printed.  A reference ({x:...} or {ref:...}) to a
#                      section that is inside conditional text must be in
#                      text that is inside the same .if (the build stops
#                      with a message when it is not), and the same holds
#                      for the references of the cards and the guides to
#                      such a section (doc/*.doc.cond lists them).
#                      Conditions may be nested and must be closed in the
#                      file that opened them.
#   .ix TERM           An index entry that points to the current headline.
#                      Every headline is also an entry of the index, under
#                      its own words.
#   .note TEXT         A boxed paragraph that begins with the bold word Note.
#   .caution TEXT      The same with the word Caution.
#   .behavior TEXT     The same with the words Behavior of Strata.
#   .term TERM | TEXT  A glossary entry: the term in bold, then its text.  The
#                      term is also an index entry.
#   .rows W1 W2 ...    Like .table, but the first row is not a heading: it
#                      has a single line at its top instead and is not bold.
#                      (In a table a bar inside a {c:...} or {eq:...} command
#                      does not separate cells.)
#
# The generator also writes OUTPUT.meta, a list of the headlines (H), index
# entries (I) and pictures (F) of the document, one tab separated record per
# line.  Three more modes build the front and back matter from these files:
#
#     mkmanual.py --toc MASTER OUTPUT.doc       The table of contents.
#     mkmanual.py --index MASTER OUTPUT.doc     The index.
#     mkmanual.py --figures MASTER OUTPUT.doc   The list of figures.
#
# MASTER is manual/src/master.txt: a line "NUMBER BASENAME TITLE" for every
# document in order (NUMBER is the chapter number or letter, or - for none).
# The meta files are read from the directory of OUTPUT.doc.  The option
# "--labels FILE" before the source makes {x:label} work across documents:
# FILE holds the H records of all the documents.
#
# THE SINGLE DOCUMENT.  The Reference Manual is one document:
#
#     mkmanual.py --single MASTER OUTPUT.doc
#
# reads every source of MASTER that has a chapter number or letter (the
# lines whose number is a dash are the cards and guides and stay separate)
# and writes OUTPUT.doc with a title page, the contents, the chapters and
# appendices in the order of the master, the index and the list of figures.
# Every chapter is a section of the first level, so Go To section, the
# outline, the section tags and the section commands of the Document
# Manager work on it.  The contents, the index and the list of figures are
# tables of cross-reference markers of the kind "page number" (XT4): the
# page of each row is made by the Printer from the labels it puts at every
# headline, so the numbers are right in any print of the whole document and
# follow every edit.  A cross-reference {ref:label} or {x:label} is a
# reference inside the one document.  The sources are read as they are on
# the day the generator runs, and a label that no headline has stops the
# build with the list of the labels.  The file manual/src/aliases.txt can
# give a label to a headline whose source has none (a line is the label, a
# blank and the text of the headline).  The records of the headlines, the
# index entries and the figures are written to OUTPUT.doc.meta as before.
#
# How the maintainer works (the same is in manual/build.sh and in the
# README of manual/): edit a chapter source and run "build.sh single"; or,
# for a small correction, edit doc/strata-reference-manual.doc in the
# Editor, and make the same correction in the source because the next
# build makes the document from the sources again.
#
# In the text of a table of the contents the command {p:N} is a page number
# of the section with the number N of the generator (used by the generator
# itself; the sources do not need it).
#   - TEXT             A bullet item; a line that begins with N. is a
#                      numbered item (the number is typed text).  A line
#                      that begins with "+ " is a counted item: a Counter
#                      marker (counter 1) in front of the text prints the
#                      number, the first item of a run sets the counter to 1
#                      and every later item adds one, so that the numbers
#                      follow the edits of the list.  A continuation line
#                      starts with spaces.
#
# Inside text: {k:Ctrl-S} is a keycap, {b:text} bold, {i:text} italic,
# {u:text} underlined, {c:text} the program font, {fn:text} a footnote,
# {latex:TEXT} a LaTeX marker with the text,
# {eq:TeX} an equation, {ref:label} a cross-reference to a section, and
# {X:label} the same with a capital S, for the start of a sentence.
# {x:label} the number and the title of a section of any document, as text,
# {n:label} the number alone of a section of this document, after the
# section sign (for a place inside parentheses, where a title would nest).
# A reference to an appendix, or to a section of one, has the word Appendix
# in place of the section sign; two or more of them in a run ({n:a} and
# {n:b}) have the plural word once: Appendices A and B.
# {{ and }} are the braces themselves.

import os
import re
import subprocess
import sys

CRLF = b"\r\n"
EOL = b"\x1d"
MARK = b"\x13"
ATTR = b"\x12"
PROGRAM_FONT = 10
KEY_SCALE = [1000]
# While it is 1, the word Strata in the text of a paragraph, a list, a table,
# a note, a headline, the contents, the index and the list of figures is a
# LaTeX marker with the command \strata, so that a print shows the logo of
# the word.  Every document of the manual has it.  The words in the program
# font, in a code block and in the description of a document stay plain.
STRATA_MARKS = [True]
STRATA_WORD = "Strata"
# The name of the company, Logic Magicians Software, in the text of a
# paragraph is a LaTeX marker with the command \lms, which prints it in
# italics, so that every document of the manual shows the name in the same
# way.  The words in the program font and in a code block stay plain.
LMS_WORD = "Logic Magicians Software"
# The names of the typesetting system, LaTeX and TeX, are written as the
# names of Strata are: while STRATA_MARKS is 1, each occurrence as a word of
# its own in the text of a paragraph, a list, a table, a note, a headline, a
# caption and a run of bold, italic or underlined text is a LaTeX marker with
# the command \LaTeX{} or \TeX{}, which prints the logo of the name.  The
# conversion is the same as for the word Strata in every place but one: a
# word in double quotes is the text of a message of a program, as it
# appears, and stays plain.  The words in the program font, in a code block,
# in the description of a document and in the text of an index entry (a
# plain record of the index marker) stay plain; a word that is part of a
# longer word (pdfTeX, TeXbook, LaTeX2e), of a file name or of a command
# (\LaTeX) is not the name and stays plain.  manual/checkeditorial.py
# checks the sources and the generated documents (rules E24 and E25).
LATEX_WORD = "LaTeX"
TEX_WORD = "TeX"
NAME_MARKS = ((LATEX_WORD, b"\\LaTeX{}"), (TEX_WORD, b"\\TeX{}"))

HEADER = [
    b"*R3",
    b"MD%s\x00",
    b"MR1000",
    b"PML1000",
    b"PMR1000",
    b"PMT1000",
    b"PMB1000",
    b"PP\\1\\\x00",
    b"P1C33",
    b"FHA1",
    b"FTJ3",
    b"FTT5",
    b"FSH150",
    b"FST150",
    b"FSS200",
    b"FSC150",
    b"FI3",
    b"FU2",
    b"DP0,8500,11000",
    b"DF10NProgram\x00",
    b"YCL99",
    b"YIWN2",
]



def read_version():
    """The version of Strata that the title page of the manuals shows.  It
    comes from scripts/strata-version with the option --manual: the number
    for a release, and the word development for every other branch, so that
    the documents that are committed do not change with the name of a branch
    or with the SHA of a commit.  The sources write @VERSION@ where the
    version is to be shown."""
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, ".."))
    script = os.path.join(root, "scripts", "strata-version")
    try:
        out = subprocess.run([script, "--manual", root],
                             stdout=subprocess.PIPE, check=True,
                             stderr=subprocess.DEVNULL)
    except (OSError, subprocess.CalledProcessError):
        return "unknown"

    return out.stdout.decode("ascii", "replace").strip()


VERSION = read_version()

class SourceError(Exception):
    """A mistake in a source file."""


def cp(text):
    """Text as bytes; the sources are ASCII so there is nothing to map."""
    try:
        return text.encode("ascii")
    except UnicodeEncodeError as e:
        raise SourceError("the text is not ASCII: %r" % text) from e


def marker(mtype, records):
    """A marker of the given type; the records are byte strings."""
    body = b"".join(r + CRLF for r in records)
    return MARK + str(mtype).encode() + b"(" + body + MARK + b")"


def plain_text(text):
    """The words of a text that has commands: {c:word} is word, and a
    reference has no words.  An index entry and the name of a list are the
    words only."""
    out = []
    pos = 0
    while pos < len(text):
        if text.startswith("{{", pos):
            out.append("{")
            pos += 2
        elif text.startswith("}}", pos):
            out.append("}")
            pos += 2
        elif text[pos] == "{":
            m = re.match(r"\{([a-zA-Z]+):", text[pos:])
            if m is None:
                out.append(text[pos])
                pos += 1
                continue
            start = pos + m.end()
            i = start
            while i < len(text):
                if text.startswith("}}", i):
                    i += 2
                elif text[i] == "}":
                    break
                else:
                    i += 1
            arg = text[start:i].replace("{{", "{").replace("}}", "}")
            if m.group(1) not in ("ref", "x", "X", "n", "p", "tg", "hd",
                                  "fig", "Fig", "eq"):
                out.append(arg)
            pos = i + 1
        else:
            out.append(text[pos])
            pos += 1
    return " ".join("".join(out).split())


def index_term(term):
    """The main heading of an index entry for a term or a headline: its
    words, without a leading article, so that The font list is listed under
    Font list and not under T."""
    words = plain_text(term)
    m = re.match(r"(?:the|a|an)\s+(\S)(.*)$", words, re.IGNORECASE | re.DOTALL)
    return words if m is None else m.group(1).upper() + m.group(2)


def index_marker(term):
    """An Index marker with the main heading TERM: the entry of the index
    that the Printer makes, with the page of the marker.  The heading has no
    leading article (index_term)."""
    return marker(3, [b"2" + cp(index_term(term)) + b"\x00"])


def see_marker(alias, primary):
    """An Index marker for a second name of a thing: the main heading ALIAS
    and, in place of a page number, the reference text See PRIMARY."""
    return marker(3, [b"2" + cp(plain_text(alias)) + b"\x00",
                      b"4" + cp("See " + plain_text(primary)) + b"\x00"])


def read_synonyms(path):
    """The second names of things: a line of the file synonyms.txt is a
    name, a bar and the glossary term that the name stands for.  The result
    maps the term to the list of its second names."""
    found = {}
    if not os.path.exists(path):
        return found
    with open(path, "r", encoding="ascii") as f:
        for line in f:
            line = line.strip()
            if line == "" or line.startswith("#"):
                continue
            alias, bar, primary = line.partition("|")
            if bar == "":
                raise SourceError("synonyms.txt: no bar in %r" % line)
            found.setdefault(primary.strip(), []).append(alias.strip())
    return found


SYNONYMS = read_synonyms(os.path.join(os.path.dirname(
    os.path.abspath(__file__)), "src", "synonyms.txt"))


def figure_marker(name):
    """A Figure marker: the entry NAME of the list of figures."""
    return marker(11, [b"1" + cp(plain_text(name)) + b"\x00"])


def table_marker(name):
    """A Table marker: the entry NAME of the list of tables."""
    return marker(29, [b"1" + cp(plain_text(name)) + b"\x00"])


# The line of the print is 468 points wide on Letter paper.  A unit of the
# width of a column is 7.2 points and every column but the last adds 11.9
# points for its borders and its space.  The last column has no width of its
# own: it gets what is left of the line.  The widths of a table therefore
# fit when the widths of all columns together are at most the number that
# table_room gives; a table that is wider loses a part of its last column.
TABLE_LINE = 468.0
TABLE_UNIT = 7.2
TABLE_PAD = 11.9


def table_room(columns):
    """The most that the widths of a table of this many columns may add up
    to, so that the last column keeps the width that its source gives it."""
    return int((TABLE_LINE - TABLE_PAD * columns) / TABLE_UNIT)


def fit_widths(widths):
    """The widths of a table, made smaller in one proportion when they add up
    to more than table_room allows."""
    room = table_room(len(widths))
    total = sum(widths)
    if total <= room:
        return widths
    fitted = [max(4, int(w * room / total)) for w in widths]
    while sum(fitted) > room:
        fitted[fitted.index(max(fitted))] -= 1
    return fitted


# The number of columns of a .code line from which the block is set in the
# small size of the program font.
CODE_SMALL_COLUMNS = 80


def nested_marker(mtype, text):
    """A marker that holds text, such as a footnote or an equation."""
    body = b"S" + CRLF + b"T" + text + EOL + b"\x00" + CRLF
    return MARK + str(mtype).encode() + b"(" + body + MARK + b")"


def attr(mask):
    """A change of the attributes of the text that follows."""
    return ATTR + bytes([mask, 0])


# 1 while the cells of a header row are turned into bytes: an attribute that
# a cell sets on a run (bold, italic, underscore) then keeps the bold of the
# header, so that the whole header row stays bold.
HEADER_BOLD = [0]

# The border weights of the sides of a column (see src/border.h): the top is
# bits 0-1, the bottom bits 2-3, the left bits 4-5 and the right bits 6-7.
BORDER_SINGLE = 1
BORDER_DOUBLE = 2


def table_mask(header, first_row, column):
    """The border mask of the column COLUMN of a table row: the rule for the
    header row, for the other rows, and for the first row of a table with no
    header row (FIRST_ROW without HEADER)."""
    top = BORDER_SINGLE if header or first_row else 0
    bottom = BORDER_DOUBLE if header else BORDER_SINGLE
    left = BORDER_SINGLE if column == 0 else 0
    return top | (bottom << 2) | (left << 4) | (BORDER_SINGLE << 6)


class Block:
    """A block: its property records and its columns.

    A column is a pair of its property records and its text bytes.  The text
    may hold a cross-reference placeholder that is replaced when the document
    is written, because the number of a section is known only then."""

    def __init__(self, props, columns):
        self.props = props
        self.columns = columns
        self.cond = ()


class Section:
    """A section: a level, a headline, blocks and subsections."""

    def __init__(self, level, title, label):
        self.level = level
        self.title = title
        self.label = label
        self.blocks = []
        self.subs = []
        self.number = ""
        self.snum = ""
        self.uid = 0
        self.cond = ()


class Builder:
    """Collects the sections and writes the document."""

    def __init__(self):
        self.desc = "Strata document"
        self.root = Section(0, "", None)
        self.stack = [self.root]
        self.labels = {}
        self.figlabels = {}
        self.figure = 0
        self.margin = 1000
        self.margins = None
        self.landscape = False
        self.numbering = True
        self.product_numbers = False
        self.run_kind = None
        self.run_count = 0
        self.linespace = None
        self.chapter = None
        self.counters = []
        self.meta = []
        self.syn_used = set()
        self.xrefs_total = 0
        self.xrefs_cond = 0
        self.cur = None
        self.extern = {}
        self.unresolved = set()
        self.nuid = 0
        self.byuid = {}
        self.recs = []
        self.cond = []
        self.cur_cond = ()
        self.cur_sec = None
        self.reach_errors = []
        self.extern_cond = set()
        self.flat = False
        # The level of the table of contents that the Printer makes when the
        # document is printed (the record YCL): 0 for none, which is right
        # for a card and a guide.  A document that wants its contents, its
        # index and its lists sets auto_lists: the generator then writes the
        # markers (Index at the headlines of the second level and below and
        # at every .ix and .term, Figure at every caption) and the Printer
        # makes the parts, with the page numbers, when the document is
        # printed (docs/AUTOLISTS.txt).
        self.contents = 0
        self.font_hidden = True
        self.auto_lists = False

    def cond_state(self):
        """The conditions that are open now: (expression, in the else)."""
        return tuple((e, el) for e, el in self.cond)

    def section(self, level, title, label):
        """Start a section of the given level."""
        if self.flat:
            raise SourceError("a part has no headlines")
        while self.stack[-1].level >= level:
            self.stack.pop()
        if self.stack[-1].level != level - 1:
            raise SourceError("a level %d section has no parent" % level)
        here = self.cond_state()
        # A document whose numbers are made by the product (product_numbers)
        # has no number in the text of a headline, so a section that is left
        # out in the middle of its parent leaves no gap: the Printer numbers
        # only what it prints.
        for prev in ([] if self.product_numbers else self.stack[-1].subs):
            if prev.cond != () and here[:len(prev.cond)] != prev.cond:
                raise SourceError(
                    "the section \"%s\" follows the section \"%s\", which "
                    "is inside .if text that does not hold for it: the "
                    "numbers of the headlines are part of their text, so "
                    "the section that is left out would leave a gap; put "
                    "the sections that are left out after the others of "
                    "their parent" % (title, prev.title))
        sec = Section(level, title, label)
        sec.cond = here
        self.nuid += 1
        sec.uid = self.nuid
        self.byuid[sec.uid] = sec
        self.stack[-1].subs.append(sec)
        self.stack.append(sec)
        if label is not None:
            self.labels[label] = sec
        shown = title
        run = None
        if self.chapter is not None:
            while len(self.counters) < level:
                self.counters.append(0)
            del self.counters[level:]
            self.counters[level - 1] += 1
            nums = [self.chapter] + self.counters[1:]
            sec.number = ".".join(str(n) for n in nums)
            if self.product_numbers:
                run = self.chapter_run(level)
            else:
                shown = sec.number + "  " + title
                self.numbering = False
        elif self.product_numbers:
            run = self.plain_run(level)
        sec.shown = shown
        self.cur = sec
        self.meta.append("H\t%d\t%s\t%s\t%s" % (
            level, sec.number, label or "", title))
        self.recs.append(("H", level, sec.number, title, sec.uid,
                          self.cond_state()))
        head = styled(shown)
        if self.landscape and not self.root.subs[1:] and level == 1:
            head = marker(36, [b"12"]) + head
        props = [] if run is None else [b"BZG" + run + b"\x00"]
        if self.auto_lists and level >= 2 and len(plain_text(title)) > 1 and \
                title != "See":
            head += index_marker(title)
        blk = Block(props, [([], head + EOL)])
        blk.cond = here
        sec.blocks.append(blk)

    def chapter_run(self, level):
        """The numbering run record of the headline of a chapter section
        when the product numbers the headlines.  A chapter of the first
        level starts the run of the numbers, or of the letters of the
        appendices, when its kind differs from the kind before it.  The
        number of the chapter must be the place that the product counts it
        at, so the chapters must follow each other."""
        if level != 1:
            return None
        kind = "letter" if self.chapter.isalpha() else "number"
        run = None
        if kind != self.run_kind:
            self.run_kind = kind
            self.run_count = 0
            run = b"\\A\\ " if kind == "letter" else b"\\1\\ "
        self.run_count += 1
        if kind == "letter":
            want = ord(self.chapter) - ord("A") + 1
        else:
            want = int(self.chapter)
        if want != self.run_count:
            raise SourceError(
                "the chapter %s is number %d of its kind, and the product "
                "counts it as %d: the chapters must follow each other"
                % (self.chapter, want, self.run_count))
        return run

    def plain_run(self, level):
        """The run record of a headline that has no number (the title page,
        the contents, the index, the list of figures): an empty tag format
        on the first section of a stretch of such sections, and on the first
        section below such a section."""
        if level == 1:
            if self.run_kind != "none":
                self.run_kind = "none"
                return b""
            return None
        if len(self.stack[-2].subs) == 1:
            return b""
        return None

    def add(self, props, text):
        """Add a block of one column to the current section."""
        if len(self.stack) == 1 and not self.flat:
            raise SourceError("text before the first headline")
        blk = Block(props, [([], text + EOL)])
        blk.cond = self.cond_state()
        self.stack[-1].blocks.append(blk)

    def add_block(self, block):
        """Add a ready block to the current section."""
        if len(self.stack) == 1 and not self.flat:
            raise SourceError("text before the first headline")
        block.cond = self.cond_state()
        self.stack[-1].blocks.append(block)

    def number(self, sec, prefix):
        """Number the sections as 1, 1.1 and so on.  The structural number
        (snum) is the one that identifies the section in the Editor, and the one that
        a cross-reference marker holds; the shown number (number) is the
        one in the text of the headline when a chapter number is set."""
        for i, sub in enumerate(sec.subs, 1):
            sub.snum = prefix + str(i)
            if self.chapter is None:
                sub.number = sub.snum
            self.number(sub, sub.snum + ".")

    def reach(self, label, cond):
        """A reference from text that is printed whenever the conditions
        cur_cond hold to a section that is printed only when cond holds
        would print the number of a section that is not there."""
        if cond != () and self.cur_cond[:len(cond)] != cond:
            where = self.cur_sec.title if self.cur_sec is not None else "?"
            self.reach_errors.append(
                "a reference to the section %s from text that is printed "
                "when the section is not (in the section \"%s\"): put the "
                "reference inside the same .if" % (label, where))

    def sign(self):
        """The section sign of a cross reference to a section by its
        number: a LaTeX marker holding \\S{}, which the Printer sets
        directly before the number, with no space between them.  The braces
        keep TeX from skipping a blank after the control word.  Every sign
        is counted for the check of the printed document."""
        self.xrefs_total += 1
        if self.cur_cond != ():
            self.xrefs_cond += 1
        return nested_marker(41, cp("\\S{}"))

    def lead(self, num, tag):
        """What stands before the number of a cross reference: the section
        sign for a chapter or a section, and the word Appendix for an
        appendix or a section of an appendix (a LaTeX marker that holds the
        word in a box, so that it is never hyphenated, followed by a
        no-break space, so that the word and the letter stay on one line).  The
        first reference of a run of appendices has the word and the others
        of the run have nothing: the plural word Appendices when the run
        names more than one appendix ('Appendices B and C'), and the
        singular word when all of them are sections of one appendix
        ('Appendix B.1 and B.2')."""
        if num[:1].isalpha():
            if tag == "B":
                return b""
            word = "Appendices" if tag == "A" else "Appendix"
            return nested_marker(41, cp("\\mbox{%s}~" % word))
        return self.sign()

    def number_of(self, name):
        """The number of the section that a reference names, without the
        kind of the reference ({x:..}, {X:..}, {n:..}); empty when the
        label is not known."""
        name = name[2:]
        if name in self.labels:
            return self.labels[name].number
        if name in self.extern:
            return self.extern[name][0]
        return ""

    def mark_runs(self, data):
        """Tag the references to appendices that stand in a run: two or
        more references that are joined only by a comma, and or or, and
        that all name an appendix or a section of an appendix.  The first
        gets the tag A (the word Appendices, for runs that name more than
        one appendix) or S (the word Appendix, for runs inside one
        appendix) and the others the tag B (no word), so that the print has
        'Appendices B and C' and never 'Appendix B and Appendix C'."""
        ref = rb"\x01REF:([xXn]:[^\x01]*)\x01"
        join = rb"(?:, and |, or | and | or |, )"
        run = re.compile(ref + b"(?:" + join + ref + b")+")
        item = re.compile(ref)

        def tag_run(m):
            text = m.group(0)
            members = list(item.finditer(text))
            kinds = [self.number_of(x.group(1).decode("ascii"))[:1].isalpha()
                     for x in members]
            tags = [""] * len(members)
            i = 0
            while i < len(members):
                if not kinds[i]:
                    i += 1
                    continue
                j = i
                while j + 1 < len(members) and kinds[j + 1]:
                    j += 1
                if j > i:
                    letters = {self.number_of(
                        members[k].group(1).decode("ascii"))[:1]
                        for k in range(i, j + 1)}
                    tags[i] = "A" if len(letters) > 1 else "S"
                    for k in range(i + 1, j + 1):
                        tags[k] = "B"
                i = j + 1
            out = bytearray()
            pos = 0
            for x, tg in zip(members, tags):
                out += text[pos:x.start(1)]
                if tg != "":
                    out += tg.encode() + b"|"
                pos = x.start(1)
            out += text[pos:]
            return bytes(out)
        return run.sub(tag_run, data)

    def resolve(self, data):
        """Replace the cross-reference placeholders by markers."""
        data = self.mark_runs(data)

        def one(m):
            name = m.group(1).decode("ascii")
            tag = ""
            if name[1:2] == "|":
                tag = name[0]
                name = name[2:]
            if name.startswith("p#"):
                sec = self.byuid[int(name[2:])]
                return marker(2, [b"XN" + cp(sec.snum) + b"\x00", b"XT4"])
            if name.startswith("t#") or name.startswith("h#"):
                # The tag or the headline of a section, as the product
                # prints it: a cross-reference marker, so that it follows
                # the section.
                sec = self.byuid[int(name[2:])]
                what = b"XT1" if name[0] == "t" else b"XT2"
                return marker(2, [b"XN" + cp(sec.snum) + b"\x00", what])
            if name.startswith("fig:") or name.startswith("Fig:"):
                cap = name[0] == "F"
                label = name.split(":", 1)[1]
                if label not in self.figlabels:
                    raise SourceError("no figure has the label %s" % label)
                chapter, number = self.figlabels[label]
                text = "%d" % number
                if chapter is not None:
                    text = "%s.%d" % (chapter, number)
                return styled(("Figure " if cap else "figure ") + text)
            if name.startswith("n:"):
                # The section sign and the number of a section, with no
                # title: for a place where a title in parentheses would
                # nest inside parentheses or does not fit.
                name = name[2:]
                if name not in self.labels and name in self.extern:
                    num = self.extern[name][0]
                    return self.lead(num, tag) + styled(num)
                if name not in self.labels:
                    self.unresolved.add(name)
                    return cp("[%s]" % name)
                sec = self.labels[name]
                self.reach(name, sec.cond)
                if self.product_numbers and sec.number != "":
                    return (self.lead(sec.number, tag) +
                            marker(2, [b"XN" + cp(sec.snum) + b"\x00",
                                       b"XT1"]))
                return self.lead(sec.number, tag) + styled(sec.number)
            if name.startswith("x:") or name.startswith("X:"):
                cap = name[0] == "X"
                name = name[2:]
                if name in self.labels:
                    sec = self.labels[name]
                    self.reach(name, sec.cond)
                    num, title = sec.number, sec.title
                    if self.product_numbers and sec.number != "":
                        # A section of this document: the number is a
                        # cross-reference marker, so that it is the one
                        # that the product prints and it follows the
                        # section.  The title stays text: the marker
                        # prints a headline without the logo and the keys
                        # that it may hold.
                        return (self.lead(num, tag) +
                                marker(2, [b"XN" + cp(sec.snum) + b"\x00",
                                           b"XT1"]) +
                                styled(" (%s)" % short_title(num, title)))
                elif name in self.extern:
                    if name in self.extern_cond and self.cur_cond == ():
                        self.reach_errors.append(
                            "a reference to the section %s, which is not "
                            "always printed, from text that is: put the "
                            "reference inside an .if" % name)
                    num, title = self.extern[name][:2]
                else:
                    self.unresolved.add(name)
                    return cp("[%s]" % name)
                where = ""
                if (name in self.extern and name not in self.labels and
                        len(self.extern[name]) > 2):
                    where = " of " + self.extern[name][2]
                return self.lead(num, tag) + styled(
                    "%s (%s)%s" % (num, short_title(num, title), where))
            if name not in self.labels:
                raise SourceError("no section has the label %s" % name)
            sec = self.labels[name]
            self.reach(name, sec.cond)
            return marker(2, [b"XN" + cp(sec.snum) + b"\x00", b"XT2"])
        return re.sub(rb"\x01REF:([^\x01]*)\x01", one, data)

    def emit(self, sec, out):
        """Write one section and its subsections."""
        out.append(b"S" + CRLF)
        for i, block in enumerate(sec.blocks):
            if block.props:
                for p in block.props:
                    out.append(p + CRLF)
            elif i > 0:
                out.append(b"B" + CRLF)
            self.cur_cond = block.cond
            self.cur_sec = sec
            for props, text in block.columns:
                for p in props:
                    out.append(p + CRLF)
                out.append(b"T" + self.resolve(text) + b"\x00" + CRLF)
        if sec.subs:
            out.append(b"+" + CRLF)
            for sub in sec.subs:
                self.emit(sub, out)
            out.append(b"-" + CRLF)

    def write(self, path):
        """Write the document to a file."""
        self.number(self.root, "")
        out = []
        for h in HEADER:
            if b"%s" in h:
                h = h.replace(b"%s", cp(self.desc))
            if h == b"YCL99":
                h = b"YCL%d" % self.contents
            if h[:2] == b"PM" and h[2:3] in b"LRTB":
                m = self.margin
                if self.margins is not None:
                    m = self.margins[b"LRTB".index(h[2:3])]
                h = h[:3] + b"%d" % m
            out.append(h + CRLF)
        if not self.numbering:
            out.insert(len(out) - 2, b"FLA0" + CRLF)
        # The Font markers of a document are not shown on the screen unless
        # its source says .fontmarkers short: the words of the program font
        # are plain words to a reader in a terminal (the record OM15,4: the
        # display mode of the Font marker is Hidden).  A person can show them
        # again with the display of hidden markers.
        if self.font_hidden:
            out.append(b"OM15,4" + CRLF)
        if self.flat:
            # A part: no structure, the blocks of the root only.
            out.append(b"*B0" + CRLF)
            self.emit(self.root, out)
        else:
            out.append(b"*B1" + CRLF)
            if self.root.blocks:
                # The text before the first section is the title page.
                self.emit(self.root, out)
            else:
                out.append(b"S" + CRLF)
                out.append(b"+" + CRLF)
                for sec in self.root.subs:
                    self.emit(sec, out)
                out.append(b"-" + CRLF)
        out.append(b"*E" + CRLF)
        # The counts of the markers that the lists are made from, as the
        # Editor writes them (docs/AUTOLISTS.txt): before *B, one record for
        # each kind that the document has.
        raw = b"".join(out)
        counts = []
        for code, mtype in ((b"F", 11), (b"P", 25), (b"T", 29), (b"I", 3)):
            n = len(re.findall(b"\x13" + b"%d" % mtype + b"\\(", raw))
            if n > 0:
                counts.append(b"ZN" + code + b"%d" % n + CRLF)
        at = next(i for i, piece in enumerate(out) if piece[:2] == b"*B")
        out[at:at] = counts
        with open(path, "wb") as f:
            f.write(b"".join(out))
        # The number of cross references that carry the section sign, and
        # how many of them are inside a condition (an edition may omit
        # them): the check of the printed document compares them with the
        # signs that it finds.
        self.meta.append("X\t%d\t%d" % (self.xrefs_total, self.xrefs_cond))
        with open(path + ".meta", "w", encoding="ascii") as f:
            f.write("\n".join(self.meta) + "\n")


def short_title(num, title):
    """The title of an appendix without its own words 'Appendix X: ', which
    the reference prints in front of the number."""
    prefix = "Appendix %s: " % num
    return title[len(prefix):] if title.startswith(prefix) else title


def styled(text):
    """The text of a bold, italic or underlined run: plain text, in which
    the word Strata is a marker when the reference manual asks for it."""
    out = bytearray()
    pos = 0
    quoted = quoted_spans(text)
    while pos < len(text):
        if STRATA_MARKS[0] and strata_at(text, pos):
            out += nested_marker(41, b"\\strata")
            pos += len(STRATA_WORD)
        elif STRATA_MARKS[0] and text.startswith(LMS_WORD, pos):
            out += nested_marker(41, b"\\lms")
            pos += len(LMS_WORD)
        elif STRATA_MARKS[0] and name_at(text, pos, quoted) is not None:
            word, cmd = name_at(text, pos, quoted)
            out += nested_marker(41, cmd)
            pos += len(word)
        else:
            out += cp(text[pos])
            pos += 1
    return bytes(out)


def command(name, arg):
    """The bytes of one inline command."""
    if name == "k":
        records = [b"1" + cp(arg) + b"\x00"]
        if KEY_SCALE[0] != 1000:
            records.append(b"4%d" % KEY_SCALE[0])
        return marker(37, records)
    if name == "b":
        return attr(1 | HEADER_BOLD[0]) + styled(arg) + attr(HEADER_BOLD[0])
    if name == "i":
        return attr(4 | HEADER_BOLD[0]) + styled(arg) + attr(HEADER_BOLD[0])
    if name == "u":
        return attr(16 | HEADER_BOLD[0]) + styled(arg) + attr(HEADER_BOLD[0])
    if name == "c":
        return (marker(15, [b"1%d" % PROGRAM_FONT]) + cp(arg) +
                marker(15, []))
    if name == "fn":
        # The text of a footnote is plain text: the Editor and the Printer
        # read it up to the first end of a text record, so a marker inside
        # it (the logo of the word Strata) would be shown and printed as
        # its raw bytes and would cut the rest of the footnote off.
        return nested_marker(1, cp(arg))
    if name == "latex":
        return nested_marker(41, cp(arg))
    if name == "eq":
        return nested_marker(9, cp(arg))
    if name == "ref":
        return b"\x01REF:" + cp(arg) + b"\x01"
    if name == "p":
        return b"\x01REF:p#" + cp(arg) + b"\x01"
    if name == "tg":
        return b"\x01REF:t#" + cp(arg) + b"\x01"
    if name == "hd":
        return b"\x01REF:h#" + cp(arg) + b"\x01"
    if name == "fig" or name == "Fig":
        return b"\x01REF:" + name.encode("ascii") + b":" + cp(arg) + b"\x01"
    if name == "x":
        return b"\x01REF:x:" + cp(arg) + b"\x01"
    if name == "X":
        return b"\x01REF:X:" + cp(arg) + b"\x01"
    if name == "n":
        return b"\x01REF:n:" + cp(arg) + b"\x01"
    raise SourceError("unknown command {%s:}" % name)


def strata_at(text, pos):
    """Whether the word Strata, as a word of its own, starts at pos."""
    if not text.startswith(STRATA_WORD, pos):
        return False
    before = text[pos - 1] if pos > 0 else " "
    end = pos + len(STRATA_WORD)
    after = text[end] if end < len(text) else " "
    return not (before.isalnum() or before in "_.~/" or after.isalnum() or
                after == "_")


def name_at(text, pos, quoted=()):
    """The (word, command) of the name LaTeX or TeX, as a word of its own,
    that starts at pos and is not inside a quoted span, or None."""
    for word, cmd in NAME_MARKS:
        if not text.startswith(word, pos):
            continue
        before = text[pos - 1] if pos > 0 else " "
        end = pos + len(word)
        after = text[end] if end < len(text) else " "
        if before.isalnum() or before in "_~/\\" or after.isalnum() or \
                after == "_":
            continue
        if any(a <= pos < b for a, b in quoted):
            continue
        return word, cmd
    return None


def quoted_spans(text):
    """The spans of the text in double quotes: the words of a message."""
    return [m.span() for m in re.finditer(r'"[^"\n]*"', text)]


def inline(text):
    """Turn the text of a paragraph into bytes with markers."""
    out = bytearray()
    pos = 0
    quoted = quoted_spans(text)
    while pos < len(text):
        if text.startswith("{{", pos):
            out += b"{"
            pos += 2
        elif text.startswith("}}", pos):
            out += b"}"
            pos += 2
        elif text[pos] == "{":
            m = re.match(r"\{([a-zA-Z]+):", text[pos:])
            if m is None:
                raise SourceError("a brace that starts nothing: %r" % text)
            start = pos + m.end()
            i = start
            while i < len(text):
                if text.startswith("}}", i):
                    i += 2
                elif text[i] == "}":
                    break
                else:
                    i += 1
            if i >= len(text):
                raise SourceError("a brace is not closed: %r" % text)
            arg = text[start:i].replace("{{", "{").replace("}}", "}")
            if m.group(1) in ("ref", "x", "X", "n", "p", "tg", "hd", "fig",
                              "Fig"):
                # A label has no spaces; a line break inside it is joined.
                arg = "".join(arg.split())
            out += command(m.group(1), arg)
            pos = i + 1
        elif STRATA_MARKS[0] and strata_at(text, pos):
            out += nested_marker(41, b"\\strata")
            pos += len(STRATA_WORD)
        elif STRATA_MARKS[0] and text.startswith(LMS_WORD, pos):
            out += nested_marker(41, b"\\lms")
            pos += len(LMS_WORD)
        elif STRATA_MARKS[0] and name_at(text, pos, quoted) is not None:
            word, cmd = name_at(text, pos, quoted)
            out += nested_marker(41, cmd)
            pos += len(word)
        else:
            out += cp(text[pos])
            pos += 1
    return bytes(out)


def split_cells(line):
    """Split a table row at the bars that are not inside a {x:...} command."""
    cells = []
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
            cells.append(line[start:i])
            start = i + 1
        i += 1
    cells.append(line[start:])
    return cells


def merge_cells(first, more):
    """Continue the cells of the row FIRST with those of the line MORE,
    which begins with a bar: the text of cell k of MORE is added to the text
    of cell k of FIRST (an empty cell adds nothing)."""
    a = split_cells(first)
    b = split_cells(more)
    for k, text in enumerate(b):
        text = text.strip()
        if text == "":
            continue
        while k >= len(a):
            a.append("")
        a[k] = (a[k].rstrip() + " " + text) if a[k].strip() else text
    return " | ".join(c.strip() for c in a)


class Parser:
    """Reads a source file into a Builder."""

    def __init__(self, builder):
        self.b = builder
        self.lines = []
        self.pos = 0
        self.kind = None
        self.words = []
        self.centre = False
        self.counted = False

    def run(self, path):
        """Parse a whole file."""
        self.path = path
        with open(path, "r", encoding="ascii") as f:
            self.lines = f.read().replace("@VERSION@", VERSION).split("\n")
        open_before = len(self.b.cond)
        try:
            while self.pos < len(self.lines):
                line = self.lines[self.pos]
                self.pos += 1
                self.one(line)
            self.flush()
            if len(self.b.cond) != open_before:
                raise SourceError("an .if has no .endif in this file")
        except SourceError as e:
            raise SourceError("line %d: %s" % (self.pos, e)) from e

    def flush(self):
        """End the paragraph that is being collected."""
        if self.kind is None:
            return
        text = " ".join(self.words)
        data = inline(text)
        props = []
        if self.kind == "bullet":
            data = b"\x01\x07 \x1a" + data
        elif self.kind.startswith("num"):
            data = cp(self.kind[3:] + ".") + b" \x1a" + data
        elif self.kind == "count":
            # The first item of a run sets the counter to 1, the others add
            # one to it; the counter number is left out (counter 1).
            first = [] if self.counted else [b"21"]
            data = marker(17, first) + b" \x1a" + data
        elif self.kind == "centre":
            props = [b"BFJ2"]
        self.b.add(props, data)
        self.counted = self.kind == "count"
        self.kind = None
        self.words = []

    def start(self, kind, text):
        """Start a paragraph of a kind."""
        self.flush()
        self.kind = kind
        self.words = [text]

    def one(self, line):
        """Handle one line of the source."""
        if line.startswith("#"):
            return
        if line.strip() == "":
            self.flush()
            return
        if line.startswith(".include "):
            self.flush()
            name = os.path.join(os.path.dirname(self.path), line[9:].strip())
            with open(name, "r", encoding="ascii") as f:
                more = f.read().split("\n")
            self.lines[self.pos:self.pos] = more
            return
        if line.startswith("."):
            self.flush()
            self.counted = False
            self.directive(line)
            return
        m = re.match(r"- (.*)", line)
        if m:
            self.start("bullet", m.group(1))
            return
        m = re.match(r"(\d+)\. (.*)", line)
        if m:
            self.start("num" + m.group(1), m.group(2))
            return
        m = re.match(r"\+ (.*)", line)
        if m:
            self.start("count", m.group(1))
            return
        if line.startswith(" ") and self.kind is not None:
            self.words.append(line.strip())
            return
        if self.kind is None:
            self.start("centre" if self.centre else "plain", line.strip())
            self.centre = False
        else:
            self.words.append(line.strip())

    def directive(self, line):
        """Handle a directive."""
        word, _, rest = line.partition(" ")
        rest = rest.strip()
        if word == ".doc":
            self.b.desc = rest
        elif word in (".h1", ".h2", ".h3", ".h4"):
            label = None
            m = re.search(r"\s*\[([a-z0-9-]+)\]$", rest)
            if m:
                label = m.group(1)
                rest = rest[:m.start()]
            self.b.section(int(word[2]), rest, label)
        elif word == ".fontmarkers":
            # The display mode that the document gives to its Font markers:
            # hidden (the default of a document of the manual) or short.
            if rest not in ("hidden", "short"):
                raise SourceError(".fontmarkers needs hidden or short")
            self.b.font_hidden = rest == "hidden"
        elif word == ".contents":
            # The level of the table of contents that a print of the
            # document makes (the record YCL): a document of the manual has
            # none unless its source says so.
            if not rest.strip().isdigit() or int(rest) > 99:
                raise SourceError(".contents needs a level from 0 to 99")
            self.b.contents = int(rest)
        elif word == ".chapter":
            self.b.chapter = rest
            self.b.figure = 0
            self.b.counters = []
        elif word == ".ix":
            if self.b.cur is None:
                raise SourceError(".ix before the first headline")
            self.b.meta.append("I\t%s\t%s\t%s" % (
                rest, self.b.cur.number, self.b.cur.title))
            self.b.recs.append(("I", rest, self.b.cur.number,
                                self.b.cur.title, self.b.cur.uid,
                                self.b.cond_state()))
            if self.b.auto_lists:
                # The entry points to the current headline, so its marker is
                # at the end of the headline.  Under an open condition it is
                # a block of its own (it prints nothing), which the condition
                # covers as it covers the text.
                if self.b.cond_state() == ():
                    head = self.b.cur.blocks[0]
                    props, data = head.columns[0]
                    head.columns[0] = (props, data[:-len(EOL)] +
                                       index_marker(rest) + EOL)
                else:
                    self.b.add([], index_marker(rest))
        elif word in (".if", ".else", ".endif"):
            self.condition(word, rest)
        elif word in (".note", ".caution", ".behavior"):
            self.note(word[1:], rest)
        elif word == ".term":
            term, _, text = rest.partition("|")
            term = term.strip()
            if self.b.cur is not None:
                self.b.meta.append("I\t%s\t%s\t%s" % (
                    term, self.b.cur.number, self.b.cur.title))
                self.b.recs.append(("I", term, self.b.cur.number,
                                    self.b.cur.title, self.b.cur.uid,
                                    self.b.cond_state()))
            data = attr(1) + inline(term) + attr(0) + b" \x1a" + \
                inline(text.strip())
            if self.b.auto_lists and self.b.cur is not None:
                data += index_marker(term)
            for alias in SYNONYMS.get(term, []):
                self.b.syn_used.add(term)
                self.b.meta.append("S\t%s\t%s" % (alias, term))
                if self.b.auto_lists and self.b.cur is not None:
                    data += see_marker(alias, term)
            self.b.add([], data)
        elif word == ".rows":
            self.table(rest, False)
        elif word == ".center":
            self.centre = True
        elif word == ".margins":
            sizes = [int(round(float(x) * 1000)) for x in rest.split()]
            if len(sizes) == 4:
                self.b.margins = sizes
            else:
                self.b.margin = sizes[0]
        elif word == ".numbering":
            self.b.numbering = rest != "off"
        elif word == ".linespace":
            self.b.linespace = int(round(float(rest) * 100))
        elif word == ".keyscale":
            KEY_SCALE[0] = int(round(float(rest) * 1000))
        elif word == ".landscape":
            self.b.landscape = True
        elif word == ".panels":
            self.panels(rest)
        elif word == ".mathsyms":
            self.mathsyms(rest)
        elif word == ".code":
            self.code()
        elif word == ".table":
            self.table(rest)
        elif word == ".pic":
            self.picture(rest)
        elif word == ".space":
            size = int(round(float(rest) * 1000))
            self.b.add([], marker(31, [b"1%d" % size]))
        elif word == ".pagebreak":
            self.b.add([], marker(23, []))
        elif word == ".part":
            if self.b.root.subs or self.b.root.blocks:
                raise SourceError(".part must come before any text")
            self.b.flat = True
        elif word == ".includedoc":
            name = rest.strip()
            if name == "" or '"' in name or " " in name:
                raise SourceError(".includedoc needs one file name")
            self.b.add([], marker(16, [b"1" + cp(name) + b"\x00"]))
        else:
            raise SourceError("unknown directive %s" % word)

    def condition(self, word, rest):
        """The directives .if, .else and .endif: a block of its own that
        holds the marker (the text between them stays in its own blocks)."""
        if word == ".if":
            check_expression(rest)
            self.b.cond.append([rest, False])
            self.b.add([], marker(33, [b"1" + cp(rest) + b"\x00"]))
        elif word == ".else":
            if not self.b.cond or self.b.cond[-1][1]:
                raise SourceError(".else without an open .if")
            self.b.cond[-1][1] = True
            self.b.add([], marker(34, []))
        else:
            if not self.b.cond:
                raise SourceError(".endif without an open .if")
            self.b.cond.pop()
            self.b.add([], marker(35, []))

    def until_end(self, what, join=False, ncols=0):
        """The lines up to the .end line.  With join, a line that begins
        with a space continues the line before it."""
        lines = []
        while self.pos < len(self.lines):
            line = self.lines[self.pos]
            self.pos += 1
            if line.startswith(".end"):
                return lines
            if join and line[:1] == " " and lines and lines[-1] != "":
                plain = lines[-1] + " " + line.strip()
                if len(split_cells(line)) > 1 and ncols and \
                        len(split_cells(plain)) == ncols:
                    lines[-1] = plain
                elif len(split_cells(line)) > 1:
                    lines[-1] = merge_cells(lines[-1], line)
                else:
                    lines[-1] = lines[-1] + " " + line.strip()
            else:
                lines.append(line)
        raise SourceError("%s has no .end" % what)

    def code(self):
        """A block of lines that are kept as they are."""
        lines = self.until_end(".code")
        data = EOL.join(cp(ln.replace("{{", "{").replace("}}", "}"))
                        for ln in lines)
        data = (marker(15, [b"1%d" % PROGRAM_FONT]) + data +
                marker(15, []))

        # A line of the width of a screen (80 columns) in the program font
        # at the normal size is wider than the text of a section below the
        # first level, so the block is set in the small size.
        if max(len(ln) for ln in lines) >= CODE_SMALL_COLUMNS:
            data = (nested_marker(41, b"\\begingroup\\footnotesize{}") +
                    data + nested_marker(41, b"\\endgroup{}"))

        self.b.add([b"BFJ0"], data)

    def note(self, kind, text):
        """A boxed paragraph with a bold lead word."""
        lead = {"note": "Note.", "caution": "Caution.",
                "behavior": "Behavior of Strata."}[kind]
        words = [text]
        while self.pos < len(self.lines) and \
                self.lines[self.pos].startswith("  "):
            words.append(self.lines[self.pos].strip())
            self.pos += 1
        data = attr(1) + styled(lead) + attr(0) + b" " + inline(" ".join(words))
        col = ([b"CB85,1"], data + EOL)
        self.b.add_block(Block([b"BM15"], [col]))

    def table(self, rest, heading=True):
        """Rows of cells; every row is a block of columns.

        The borders of every table follow one rule, here and nowhere else.
        A header row has a single line at its top, its left, its right and
        between its columns and a double line at its bottom, and its text is
        bold.  Every other row has single lines at its left, its right,
        between its columns and at its bottom, and none at its top, because
        the line above it already closes it.  A table with no header row
        starts with a single line at the top of its first row instead.  A
        line between two columns is written once, as the right side of the
        left column, the way the Border panel writes it.

        The widths are fitted to the line by fit_widths.  The Printer sets
        the first row of a table on the page of the second row, so that a
        header row never stands alone at the foot of a page."""
        widths = fit_widths([int(w) for w in rest.split()])
        rows = []
        for line in self.until_end(".table", True, len(widths)):
            if line.strip() == "" or line.startswith("#"):
                continue
            cells = [c.strip() for c in split_cells(line)]
            if len(cells) != len(widths):
                raise SourceError("a row has %d cells, not %d"
                                  % (len(cells), len(widths)))
            rows.append(cells)
        for r, cells in enumerate(rows):
            header = r == 0 and heading
            columns = []
            for i, cell in enumerate(cells):
                if header:
                    HEADER_BOLD[0] = 1
                try:
                    data = inline(cell)
                finally:
                    HEADER_BOLD[0] = 0
                if header:
                    data = attr(1) + data + attr(0)
                props = []
                if i < len(cells) - 1:
                    props.append(b"CW%d" % widths[i])
                props.append(b"CB%d,1" % table_mask(header, r == 0, i))
                if i == 0:
                    props.append(b"CL1")
                props += [b"CR1", b"CFT1"]
                columns.append((props, data + EOL))
            self.b.add_block(Block([b"BM15"], columns))

    def panel_line(self, line):
        """One line of a panel, as bytes without the end of line."""
        if line.startswith("= "):
            return attr(1) + inline(line[2:]) + attr(0)
        if line.startswith("~ "):
            return inline(line[2:])
        if "|" in line:
            keys, _, text = line.partition("|")
            return inline(keys.strip()) + b" \x1a" + inline(text.strip())
        return inline(line)

    def panels(self, rest):
        """One block of side by side panels."""
        widths = [int(w) for w in rest.split()]
        panels = [[]]
        for line in self.until_end(".panels", True):
            if line.startswith("#"):
                continue
            if line.startswith(".next"):
                panels.append([])
            else:
                panels[-1].append(line)
        self.panel_block(widths, panels)

    def mathsyms(self, rest):
        """The sheet of the math symbols: the two sides of it, made from the
        list of symbols by manual/mkmathcheat.py."""
        import importlib.util
        here = os.path.dirname(os.path.abspath(__file__))
        spec = importlib.util.spec_from_file_location(
            "mkmathcheat", os.path.join(here, "mkmathcheat.py"))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        groups = mod.read_list(os.path.join(here, rest.strip()))
        sides, rooms = mod.pages(groups)
        for n, (need, have) in enumerate(rooms, 1):
            if need > have:
                raise SourceError("the panel %d of the sheet of the math "
                                  "symbols needs %.0f points of the %.0f "
                                  "that it has" % (n, need, have))
        # The panels fold in equal thirds of the page: the first and the last
        # lose the margin of the page, so the middle one is wider.  The last
        # width is not used (the last panel takes what is left).
        for k, side in enumerate(sides):
            self.panel_block([33, 35, 33], side)
            if k == 0:
                self.b.add([], marker(23, []))

    def panel_block(self, widths, panels):
        """The block of the panels, given as lists of lines."""
        if len(panels) != len(widths):
            raise SourceError(".panels has %d panels for %d widths"
                              % (len(panels), len(widths)))
        columns = []
        for i, lines in enumerate(panels):
            text = EOL.join(self.panel_line(ln) for ln in lines)
            props = [b"CW%d" % widths[i]] if i < len(panels) - 1 else []
            if i > 0:
                props.append(b"CB16,1")
            columns.append((props, text + EOL))
        # The text of a panel is short lines: it is set flush left, not even,
        # so that a wrapped line has no wide gaps.
        bprops = [b"BM15", b"BFJ0"]
        if self.b.linespace is not None:
            bprops.append(b"BSL%d" % self.b.linespace)
        self.b.add_block(Block(bprops, columns))

    def picture(self, rest):
        """A picture and its caption."""
        parts = rest.split(None, 2)
        if len(parts) < 3:
            raise SourceError(".pic needs a file, a width and a caption")
        name, width, caption = parts
        m = re.search(r"\s*\[([a-z0-9-]+)\]\s*$", caption)
        figlabel = None
        if m is not None:
            figlabel = m.group(1)
            caption = caption[:m.start()]
        self.b.figure += 1
        if figlabel is not None:
            if figlabel in self.b.figlabels:
                raise SourceError("the label %s of a figure is used twice"
                                  % figlabel)
            self.b.figlabels[figlabel] = (self.b.chapter, self.b.figure)
        self.b.meta.append("F\t%d\t%s\t%s" % (
            self.b.figure, self.b.cur.number, caption))
        self.b.recs.append(("F", "%s.%d" % (self.b.chapter, self.b.figure),
                            caption, self.b.cur.number, self.b.cur.uid,
                            self.b.cond_state()))
        size = int(round(float(width) * 1000))
        pic = marker(25, [b"1" + cp(name) + b"\x00", b"2%d" % size])
        self.b.add([b"BFJ2"], pic)
        label = "Figure %d. " % self.b.figure
        if self.b.chapter is not None:
            label = "Figure %s.%d. " % (self.b.chapter, self.b.figure)
        text = (attr(4) + cp(label) +
                inline(caption) + attr(0))
        if self.b.auto_lists:
            text += figure_marker(label + caption)
        self.b.add([b"BFJ2"], text)


def read_master(path):
    """The lines of the master: (number, basename, title)."""
    rows = []
    with open(path, "r", encoding="ascii") as f:
        for line in f:
            line = line.strip()
            if line == "" or line.startswith("#"):
                continue
            num, base, title = line.split(None, 2)
            rows.append((num, base, title))
    return rows


def read_meta(master, outdir):
    """For every document of the master: its number, title and records."""
    result = []
    for num, base, title in master:
        path = os.path.join(outdir, base + ".doc.meta")
        recs = []
        if os.path.exists(path):
            with open(path, "r", encoding="ascii") as f:
                recs = [ln.rstrip("\n").split("\t") for ln in f
                        if ln.strip() != ""]
        result.append((num, base, title, recs))
    return result


_EXPR = re.compile(
    r"^([A-Za-z.][A-Za-z0-9_.-]*)\s*(<=|>=|=|#|<|>)\s*(.+?)\s*$")


def check_expression(text):
    """Check the expression of an .if with the rules of the If marker
    (src/cond.h): a name, an operator and a value of printable ASCII
    without quotation marks, at most 255 characters."""
    m = _EXPR.match(text)
    if m is None:
        raise SourceError(".if needs a variable, an operator (= # < > <= >=) "
                          "and a value: %r" % text)
    rest = re.match(r"^[A-Za-z.][A-Za-z0-9_.-]*\s*(.*)$", text).group(1)
    if rest.startswith("<>") or rest.startswith("=="):
        raise SourceError(".if: the operator %s is not known: not equal is "
                          "written # and equal =" % rest[:2])
    name, _, value = m.groups()
    if len(name) > 255 or len(value) > 255:
        raise SourceError(".if: the name and the value have at most 255 "
                          "characters")
    for ch in value:
        if ch in "\"'" or not " " <= ch <= "~":
            raise SourceError(".if: the value may hold only printable ASCII "
                              "without quotation marks: %r" % value)


def cond_prefix(cond):
    """The markers that open the conditions of a row of a table, outermost
    first; a row of an else part is written inside the Else."""
    out = b""
    for expr, in_else in cond:
        out += marker(33, [b"1" + cp(expr) + b"\x00"])
        if in_else:
            out += marker(34, [])
    return out


def wrap_columns(columns, cond):
    """The columns of a row of a table written inside the conditions: the
    markers go at the start of the first column and the end of the last, so
    that a row that is left out leaves no block."""
    if not cond or not columns:
        return columns
    columns = list(columns)
    props, data = columns[0]
    columns[0] = (props, cond_prefix(cond) + data)
    props, data = columns[-1]
    columns[-1] = (props, data[:-len(EOL)] + marker(35, []) * len(cond) + EOL)
    return columns


def table_doc(b, title, widths, rows, conds=None):
    """Add a table of rows (no heading) to the builder as blocks.  conds
    holds the conditions of each row (or None)."""
    for n, cells in enumerate(rows):
        columns = []
        for i, cell in enumerate(cells):
            props = []
            if i < len(cells) - 1:
                props.append(b"CW%d" % widths[i])
            columns.append((props, inline(cell) + EOL))
        if conds is not None:
            columns = wrap_columns(columns, conds[n])
        b.add_block(Block([b"BM15"], columns))


def front_matter(mode, master_path, out):
    """Write the table of contents, the index or the list of figures."""
    master = read_master(master_path)
    metas = read_meta(master, os.path.dirname(os.path.abspath(out)))
    metas = [m for m in metas if m[0] != "-"]
    b = Builder()
    b.numbering = False
    if mode == "--toc":
        b.desc = "Strata Reference: Contents"
        b.section(1, "Contents", "contents")
        for num, base, title, recs in metas:
            rows = []
            for r in recs:
                if r[0] == "H" and int(r[1]) <= 3:
                    pad = "  " * (int(r[1]) - 1)
                    rows.append([r[2], pad + r[4]])
            if rows:
                b.add([], attr(1) + styled(title) + attr(0))
                table_doc(b, title, [10], [[a, c] for a, c in rows])
    elif mode == "--figures":
        b.desc = "Strata Reference: List of Figures"
        b.section(1, "List of Figures", "figures")
        rows = []
        for num, base, title, recs in metas:
            for r in recs:
                if r[0] == "F":
                    rows.append(["%s.%s" % (num, r[1]), r[3]])
        table_doc(b, "figures", [10], rows)
    else:
        b.desc = "Strata Reference: Index"
        b.section(1, "Index", "index")
        entries = {}
        for num, base, title, recs in metas:
            for r in recs:
                if r[0] == "S":
                    term, where = r[1], "See " + r[2]
                elif r[0] == "I":
                    term, where = r[1], r[2] + " " + r[3]
                elif r[0] == "H" and int(r[1]) >= 2 and r[4] != "See":
                    term, where = r[4], r[2] + " " + r[4]
                else:
                    continue
                entries.setdefault(term, [])
                if where not in entries[term]:
                    entries[term].append(where)
        letter = None
        for term in sorted(entries, key=lambda t: (t.lower(), t)):
            first = term.lstrip("\\{}(-.[/\"'").upper()[:1] or "#"
            if not first.isalpha():
                first = "Symbols"
            if first != letter:
                letter = first
                b.section(2, letter, None)
            rows = []
            for k, where in enumerate(entries[term]):
                rows.append([term if k == 0 else "", where])
            table_doc(b, term, [34], rows)
    b.write(out)


def leading_recs(recs, kind):
    """The records of one kind."""
    return [r for r in recs if r[0] == kind]


def card_labels(master, srcdir):
    """The labels of the documents that are not part of the reference (the
    cards and the guides): label -> (number, title, document title)."""
    found = {}
    for num, base, title in master:
        if num != "-" or base in ("cheat", "cheat-short", "mathcheat"):
            continue
        path = os.path.join(srcdir, base + ".src")
        if not os.path.exists(path):
            continue
        cb = Builder()
        Parser(cb).run(path)
        cb.number(cb.root, "")
        for uid, sec in cb.byuid.items():
            if sec.label is not None:
                found[sec.label] = (sec.number, sec.title, title)
    return found


def read_aliases(path, b):
    """Labels for headlines whose source has none: a line of the file is a
    label, a blank and the exact text of one headline.  A label that the
    sources define themselves wins over the alias."""
    if not os.path.exists(path):
        return
    with open(path, "r", encoding="ascii") as f:
        for line in f:
            line = line.strip()
            if line == "" or line.startswith("#"):
                continue
            label, _, title = line.partition(" ")
            found = [s for s in b.byuid.values() if s.title == title.strip()]
            if label in b.labels:
                continue
            if len(found) != 1:
                raise SourceError("alias %s: %d headlines have the text "
                                  "%r" % (label, len(found), title))
            b.labels[label] = found[0]


def single(master_path, out):
    """Write the whole reference manual as ONE document: the title page,
    the contents, the chapters and appendices in the order of the master,
    the index and the list of figures.  Every chapter is a section of the
    first level, so the cross-references and the page numbers of the
    contents and the index are cross-references inside one document."""
    master = read_master(master_path)
    srcdir = os.path.dirname(os.path.abspath(master_path))
    b = Builder()
    b.numbering = False
    b.product_numbers = True
    b.desc = "Strata Reference Manual"
    b.extern = card_labels(master, srcdir)
    # The title page is the text of the document before its first section:
    # the Printer prints it first, and then makes the contents (the level 3)
    # and the lists and the index from the markers of the chapters.
    b.contents = 3
    b.auto_lists = True
    root = b.root
    for props, text in (
            ([b"BFJ2"], marker(31, [b"1" + b"2000"])),
            ([b"BFJ2"], attr(1) + cp("Reference Manual") + attr(0)),
            # The large logo: a LaTeX marker with only the command
            # \\stratalogo, so that the Editor draws it as a picture, and a
            # Whitespace marker of 7.3 pt (unit number 3) before and after
            # it.  The space of a Whitespace marker and the space between
            # two blocks together are as high as the 3 ex that the logo
            # had on each side when it was one LaTeX marker with \\vspace.
            ([b"BFJ2"], marker(31, [b"17300", b"U3"])),
            ([b"BFJ2"], nested_marker(41, b"\\stratalogo[3]")),
            ([b"BFJ2"], marker(31, [b"17300", b"U3"])),
            ([b"BFJ2"], attr(1) + cp("A technical document processor for "
                                     "long, structured documents") +
             attr(0)),
            ([b"BFJ2"], cp("Version " + VERSION)),
            # The name of the company and its mark, once, on the title page.
            ([b"BFJ2"], nested_marker(41, b"\\lms")),
            ([b"BFJ2"], nested_marker(41, b"\\lmslogo[0.8]"))):
        root.blocks.append(Block(props, [([], text + EOL)]))
    for num, base, title in master:
        if num == "-":
            continue
        path = os.path.join(srcdir, base + ".src")
        if not os.path.exists(path):
            raise SourceError("%s is in the master but not found" % path)
        Parser(b).run(path)
        b.add([], marker(23, []))
    read_aliases(os.path.join(srcdir, "aliases.txt"), b)
    lost = sorted(set(SYNONYMS) - b.syn_used)
    if lost and any(base == "glossary" for num, base, title in master):
        raise SourceError("synonyms.txt: no glossary term %s" %
                          ", ".join(lost))
    b.chapter = "single"
    b.desc = "Strata Reference Manual"
    b.numbering = True
    b.write(out)
    if b.reach_errors:
        raise SourceError("\n".join(sorted(set(b.reach_errors))))
    with open(out + ".hidden", "w", encoding="ascii") as f:
        for sec in b.byuid.values():
            if sec.cond != ():
                f.write("%s\t%s\n" % (sec.number, sec.title))
    with open(out + ".cond", "w", encoding="ascii") as f:
        for label in sorted(b.labels):
            if b.labels[label].cond != ():
                f.write(label + "\n")
    if b.unresolved:
        raise SourceError("labels not found: %s"
                          % " ".join(sorted(b.unresolved)))


def main(argv):
    """Entry point."""
    labels = None
    if len(argv) > 2 and argv[1] == "--labels":
        labels = argv[2]
        argv = argv[:1] + argv[3:]
    if len(argv) == 4 and argv[1] == "--single":
        try:
            single(argv[2], argv[3])
        except SourceError as e:
            sys.stderr.write("mkmanual: %s\n" % e)
            return 1
        return 0
    if len(argv) == 4 and argv[1] in ("--toc", "--index", "--figures"):
        front_matter(argv[1], argv[2], argv[3])
        return 0
    if len(argv) != 3:
        sys.stderr.write("usage: mkmanual.py [--labels FILE] SOURCE.src "
                         "OUTPUT.doc\n")
        return 2
    b = Builder()
    if labels is not None and os.path.exists(labels):
        with open(labels, "r", encoding="ascii") as f:
            for line in f:
                r = line.rstrip("\n").split("\t")
                if len(r) == 5 and r[0] == "H" and r[3] != "":
                    b.extern[r[3]] = (r[2], r[4])
        condfile = os.path.join(os.path.dirname(os.path.abspath(labels)),
                                "strata-reference-manual.doc.cond")
        if os.path.exists(condfile):
            with open(condfile, "r", encoding="ascii") as f:
                b.extern_cond = {l.strip() for l in f if l.strip() != ""}
    try:
        Parser(b).run(argv[1])
        b.write(argv[2])
        if b.reach_errors:
            raise SourceError("\n".join(sorted(set(b.reach_errors))))
    except SourceError as e:
        sys.stderr.write("mkmanual: %s: %s\n" % (argv[1], e))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
