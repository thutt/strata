#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# Strata is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along
# with Strata.  If not, see <https://www.gnu.org/licenses/>.
# Copyright (c) 2026 Logic Magicians Software

"""Makes the sample document "How Strata was made" from the text sources in
samples/story: story.src (the title, the order of the chapters) and the
chapter files that it includes.  The sample is a structured document that a
user can learn structured documents from, and it is not a template.

The sources use the markup of the generator of the manual (manual/mkmanual.py:
the directives .h1 to .h4, .pic, .table, .code, .note, .ix, and the commands
{k:..}, {fn:..}, {eq:..}, {ref:..}, {fig:..}); this file adds what the manual
does not use:

    .quote ... .end        A quotation: lines that are joined into one block
                           of the named format Quote.
    .tabcap LABEL TEXT     The caption of the table that follows.  {tab:LABEL}
                           in the text of the document is "table N" (with the
                           chapter: "table 6.2").

The numbers of the measurements are not in the sources.  A name between two at
signs (@words_per_page@) is replaced by its value in tests/storydata.py, and
a line .datarows NAME inside a table is replaced by the rows of that table
from the same file.  The words TeX, LaTeX and the names that end in them are
typeset with the logos of the Printer (the LaTeX instruction marker) in the
text of the paragraphs and the quotations, not in the headlines.

The output has the title page, the contents, the chapters, the index, the list
of figures and the list of tables, all as the records of the manual
(docs/DOC-FORMAT.txt), the named formats Heading 1 to Heading 4, Body, Quote
and Listing, and a header with the title of the section and a footer with the
page number.  Nothing in it changes from one run to the next: the date of the
title page is text.

    python3 -I -B tests/mkstory.py FOLDER

writes FOLDER/strata-story.doc and the pictures FOLDER/pictures/*.png (the
pictures are made by tests/mkstorypics.py).
"""
import importlib.util
import os
import re
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SOURCE = os.path.join(ROOT, "samples", "story", "story.src")
NAME = "strata-story.doc"
TITLE = "How Strata Was Made"
DATE = "19 November 2026"
AUTHOR = "The project maintainer and Claude"

# The line numbering of the sample: mode 1 (the count starts again on every
# page, as the lines of a legal document are numbered) and a number on every
# fifth line.  Side, size,
# font, format and distance keep their defaults, so no record is written.
LINENUMBERING = [b"FZM1", b"FZE5"]


def load(name, path):
    """Load a module of the project from its file."""
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


mm = load("mkmanual", os.path.join(ROOT, "manual", "mkmanual.py"))
# The story sets the names TeX and LaTeX with the logos by its own rule (the
# text of the paragraphs and the quotations, not the headlines): the rule of
# the manual, which would mark them everywhere, is switched off.
mm.NAME_MARKS = ()
sd = load("storydata", os.path.join(HERE, "storydata.py"))

CRLF = mm.CRLF
EOL = mm.EOL

# The named formats: the records of each (docs/DOC-FORMAT.txt).  The text
# of the document is even; a heading is bold; a quotation is indented and in
# italics; a listing is flush left.
FORMATS = {
    "Body": [b"BFJ3"],
    "Heading 1": [b"BFA1", b"BSB200"],
    "Heading 2": [b"BFA1", b"BSB150"],
    "Heading 3": [b"BFA1"],
    "Heading 4": [b"BFA1"],
    "Listing": [b"BFJ0", b"BL3"],
    "Quote": [b"BFA4", b"BL6", b"BFJ3"],
}


# The line numbering records are written once, with the global records: see
# LINENUMBERING above.
LINE_NUMBERING = []

# The Keep marker: Start (fixed) and End (the record 1 with the value 2).
KEEP_START = mm.marker(4, [])
KEEP_END = mm.marker(4, [b"12"])


def named(name):
    """The record that gives a block a named format."""
    return b"BG" + name.encode("ascii") + b"\x00"


TEXWORD = re.compile(r"\b(pdf|Xe|Lua)?(LaTeX|TeX)\b")


def tex_logo(text):
    """Put the LaTeX instruction marker with the logo macros in place of the
    words TeX and LaTeX (and pdfLaTeX, XeLaTeX, pdfTeX ...) in the text of a
    paragraph.  What is inside a command that is written between braces,
    such as the program font or a footnote, is left as it is."""
    out = []
    depth = 0
    run = ""
    i = 0

    def flush():
        nonlocal run
        out.append(TEXWORD.sub(replace, run))
        run = ""

    while i < len(text):
        if text.startswith("{{", i) or text.startswith("}}", i):
            run += text[i:i + 2]
            i += 2
        elif text[i] == "{" and depth == 0 and \
                re.match(r"\{[a-zA-Z]+:", text[i:]):
            flush()
            j = i
            level = 0
            while j < len(text):
                if text.startswith("}}", j) or text.startswith("{{", j):
                    j += 2
                    continue
                if text[j] == "{":
                    level += 1
                elif text[j] == "}":
                    level -= 1
                    if level == 0:
                        break
                j += 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            run += text[i]
            i += 1
    flush()
    return "".join(out)


def replace(m):
    """The marker of one word."""
    prefix = m.group(1) or ""
    macro = "\\LaTeX" if m.group(2) == "LaTeX" else "\\TeX"
    return "{latex:%s%s{{}}}" % (prefix, macro)


def expand(line):
    """Replace the names between at signs by their values."""
    def one(m):
        name = m.group(1)
        if name == "VERSION":
            return m.group(0)
        if name not in sd.SCALARS:
            raise mm.SourceError("no number is called @%s@" % name)
        return sd.SCALARS[name]
    return re.sub(r"@([a-z0-9_A-Z]+)@", one, line)


class StoryBuilder(mm.Builder):
    """The builder of the manual with the named formats, the tables with a
    caption and the header and the footer of the sample."""

    def __init__(self):
        mm.Builder.__init__(self)
        self.tabno = 0
        self.tablabels = {}
        self.tabrecs = []

    def section(self, level, title, label):
        """Start a section; its headline has the format of its level."""
        mm.Builder.section(self, level, title, label)
        head = self.cur.blocks[0]
        head.props = [named("Heading %d" % min(level, 4))] + head.props

    def resolve(self, data):
        """Resolve the references to tables, then the others."""
        def one(m):
            word = "Table " if m.group(1) == b"Tab" else "table "
            name = m.group(2).decode("ascii")
            if name not in self.tablabels:
                raise mm.SourceError("no table has the label %s" % name)
            chapter, number = self.tablabels[name]
            text = "%d" % number
            if chapter is not None:
                text = "%s.%d" % (chapter, number)
            return mm.styled(word + text)
        data = re.sub(rb"\x01REF:(tab|Tab):([^\x01]*)\x01", one, data)
        return mm.Builder.resolve(self, data)

    def write(self, path):
        """Write the document, then add what the manual does not write: the
        author, the page layouts with their header and footer, and the
        named formats."""
        mm.Builder.write(self, path)
        with open(path, "rb") as f:
            data = f.read()
        data = data.replace(b"MR1000" + CRLF, b"MA" + mm.cp(AUTHOR) +
                            b"\x00" + CRLF + b"MR1000" + CRLF, 1)
        data = data.replace(b"P1C33" + CRLF, b"P1C33" + CRLF + layouts(), 1)
        # The line numbering of the global scope, after the other global
        # records: the first line of a section starts at 1 again, and only
        # every fifth line carries its number.
        data = data.replace(b"FU2" + CRLF, b"FU2" + CRLF + b"".join(
            rec + CRLF for rec in LINENUMBERING), 1)
        formats = b""
        for name in sorted(FORMATS):
            formats += b"FN" + mm.cp(name) + b"\x00" + CRLF
            for rec in FORMATS[name]:
                formats += rec + CRLF
        formats += b"FN\x00" + CRLF
        numbering = b"".join(rec + CRLF for rec in LINE_NUMBERING)
        data = re.sub(rb"YCL\d+" + CRLF, lambda m: numbering + formats +
                      m.group(0), data, count=1)
        with open(path, "wb") as f:
            f.write(data)


def trec(text):
    """A column of a header or a footer."""
    return b"T" + text + EOL + b"\x00" + CRLF


def hfblock(left, centre, right):
    """The block of a header or a footer: three columns."""
    return (b"S" + CRLF + b"CW26" + CRLF + b"CFJ0" + CRLF + trec(left) +
            b"CW26" + CRLF + b"CFJ2" + CRLF + trec(centre) + b"CFJ1" + CRLF +
            trec(right))


def layouts():
    """The header of every page but the title page: the name of the document
    at the left and the title of the section at the right; the footer: the
    page number in the middle.  The title page has neither."""
    head = hfblock(mm.cp(TITLE), b"", mm.marker(28, [b"S3"]))
    foot = hfblock(b"", b"Page " + mm.marker(22, []), b"")
    return (b"PHB" + CRLF + head + b"PFB" + CRLF + foot + b"P1HB0" + CRLF +
            b"P1FB0" + CRLF)


class StoryParser(mm.Parser):
    """The parser of the manual with the named formats of the sample, the
    quotation and the caption of a table."""

    keep_open = False

    def one(self, line):
        """Handle one line of the source, with the numbers filled in."""
        mm.Parser.one(self, expand(line))

    def until_end(self, what, join=False, ncols=0):
        """The lines up to the .end line, with the numbers filled in and the
        rows of the tables of the data module in place of .datarows."""
        out = []
        for line in mm.Parser.until_end(self, what, join, ncols):
            if line.startswith(".datarows "):
                out.extend(sd.rows(line[10:].strip()))
            else:
                out.append(expand(line))
        return out

    def flush(self):
        """End the paragraph that is being collected."""
        if self.kind is None:
            return
        text = tex_logo(" ".join(self.words))
        data = mm.inline(text)
        props = [named("Body")]
        if self.kind == "bullet":
            data = b"\x01\x07 \x1a" + data
        elif self.kind.startswith("num"):
            data = mm.cp(self.kind[3:] + ".") + b" \x1a" + data
        elif self.kind == "centre":
            props = [b"BFJ2"]
        self.b.add(props, data)
        self.kind = None
        self.words = []

    def code(self):
        """A listing: lines that are kept as they are, in the program font."""
        lines = self.until_end(".code")
        data = EOL.join(mm.cp(ln.replace("{{", "{").replace("}}", "}"))
                        for ln in lines)
        data = (mm.marker(15, [b"1%d" % mm.PROGRAM_FONT]) + data +
                mm.marker(15, []))
        self.b.add([named("Listing")], data)

    def quote(self):
        """A quotation."""
        lines = self.until_end(".quote")
        text = tex_logo(" ".join(ln.strip() for ln in lines))
        self.b.add([named("Quote")], mm.inline(text))

    def tabcap(self, rest):
        """The caption of a table, and its place in the list of tables."""
        label, _, caption = rest.partition(" ")
        caption = caption.strip()
        if label == "" or caption == "":
            raise mm.SourceError(".tabcap needs a label and a caption")
        if label in self.b.tablabels:
            raise mm.SourceError("the label %s of a table is used twice"
                                 % label)
        self.b.tabno += 1
        self.b.tablabels[label] = (self.b.chapter, self.b.tabno)
        number = "%s.%d" % (self.b.chapter, self.b.tabno)
        self.b.tabrecs.append(("T", number, caption, self.b.cur.number,
                               self.b.cur.uid, self.b.cond_state()))
        text = (KEEP_START + mm.attr(4) + mm.cp("Table %s. " % number) +
                mm.inline(caption) + mm.attr(0))
        text += mm.table_marker("Table %s. %s" % (number, caption))
        self.b.add([b"BFJ0"], text)
        self.keep_open = True

    def table(self, rest, heading=True):
        """A table; the Keep marker that its caption opened is closed in the
        last column of its last row, so that the caption and all the rows are
        printed on one page."""
        mm.Parser.table(self, rest, heading)
        if self.keep_open:
            block = self.b.stack[-1].blocks[-1]
            props, data = block.columns[-1]
            block.columns[-1] = (props, data[:-len(EOL)] + KEEP_END + EOL)
            self.keep_open = False

    def directive(self, line):
        """Handle a directive: the two of the sample, or one of the manual."""
        word, _, rest = line.partition(" ")
        if word == ".quote":
            self.quote()
        elif word == ".tabcap":
            self.tabcap(rest.strip())
        elif word == ".chapter":
            mm.Parser.directive(self, line)
            self.b.tabno = 0
        else:
            mm.Parser.directive(self, line)


def tab_command(original):
    """The inline command function of the manual with {tab:label}."""
    def command(name, arg):
        if name in ("tab", "Tab"):
            return b"\x01REF:" + name.encode("ascii") + b":" + mm.cp(arg) + \
                b"\x01"
        return original(name, arg)
    return command


def build(source, out):
    """Make the document OUT from the source file SOURCE."""
    original = mm.command
    mm.command = tab_command(original)
    try:
        b = StoryBuilder()
        b.numbering = False
        b.product_numbers = True
        b.desc = "Sample document: How Strata was made, an engineering " \
                 "history that teaches structured documents"
        # The text before the first section is the title page.  The contents
        # (the first three levels), the lists of figures and tables and the
        # index are not in the document: the Printer makes them from the
        # settings and the markers (Figure at a caption, Table at a table
        # caption, Index at a headline of the second level and below and at
        # each .ix) when the document is printed.
        b.contents = 2
        b.auto_lists = True
        for props, text in (
                ([named("Heading 1"), b"BFJ2"], mm.styled(TITLE)),
                ([b"BFJ2"], mm.marker(31, [b"1" + b"1500"])),
                # The space around the logo is made by Whitespace markers
                # of 7.3 pt (unit number 3), and the LaTeX marker holds only
                # the logo, so that the Editor draws it as a picture.  With
                # the space between two blocks the heights are those of the
                # 3 ex that the logo had on each side as one LaTeX marker.
                ([b"BFJ2"], mm.marker(31, [b"17300", b"U3"])),
                ([b"BFJ2"], mm.nested_marker(41, b"\\stratalogo[3]")),
                ([b"BFJ2"], mm.marker(31, [b"17300", b"U3"])),
                ([b"BFJ2"], mm.attr(1) + mm.cp("Method, measurements and "
                                                "open items") +
                 mm.attr(0)),
                ([b"BFJ2"], mm.cp(AUTHOR)),
                ([b"BFJ2"], mm.cp(DATE))):
            b.root.blocks.append(mm.Block(props, [([], text + EOL)]))
        parser = StoryParser(b)
        parser.run(source)
        b.chapter = "single"
        b.numbering = True
        b.write(out)
        if b.reach_errors:
            raise mm.SourceError("\n".join(sorted(set(b.reach_errors))))
        if b.unresolved:
            raise mm.SourceError("labels not found: %s"
                                 % " ".join(sorted(b.unresolved)))
    finally:
        mm.command = original
    for extra in (out + ".meta",):
        if os.path.exists(extra):
            os.remove(extra)


def main(argv):
    """Write the document and its pictures into a folder."""
    if len(argv) != 2:
        sys.stderr.write("usage: mkstory.py FOLDER\n")
        return 2
    folder = argv[1]
    os.makedirs(folder, exist_ok=True)
    try:
        build(SOURCE, os.path.join(folder, NAME))
    except mm.SourceError as e:
        sys.stderr.write("mkstory: %s\n" % e)
        return 1
    pics = load("mkstorypics", os.path.join(HERE, "mkstorypics.py"))
    pics.main(["mkstorypics", os.path.join(folder, "pictures")])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
