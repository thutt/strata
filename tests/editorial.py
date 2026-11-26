#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This test checks the editorial rules of the Reference Manual
# (manual/checkeditorial.py, the terms in docs/MANUAL-TERMS.txt):
#
#   - the sources of the manual pass the check;
#   - every rule fails on a source that breaks it, and the report names
#     the rule: E1 (filler words), E2 (narration), E3 (stale and future
#     words), E4 (hedges), E5 (contractions), E6 (units and numbers), E7
#     (spaces), E8 (terms and spelling, in the text, a headline and an
#     index entry), E9 (a typed cross reference), E10 (the case of a
#     headline), E11 (a list whose items end differently), E12 (a sentence
#     that begins with This and a verb), E13 (a sentence said twice), E14
#     (a section that announces itself), E15 (a column of a table whose
#     cells end differently), E16 (a table row that is the rest of the
#     row before it), E17 (a command that goes over the lines of a table)
#     E18 (a reference with a title inside parentheses), E19 (the word
#     Appendix typed in front of a reference), E23 (the words See also),
#     E24 (a row of a table of messages out of its place), E25 (a name of
#     the typesetting system that the generator would leave plain) and E26
#     (the spelling of the names of systems and formats) E27 (the widths
#     of a table that the line cannot hold), E29 (a table of steps with no
#     header row), E30 (software that acts like a person: a panel or a
#     program that says, asks or decides), E31 (the word bug) and E32 (the
#     pronoun it or they for software, with a verb of a person or of
#     intention);
#   - a source that follows the rules, and the places that are not checked
#     (the program font, a quoted message, a keycap, a code block, a
#     comment, a phrase of the allow file), pass;
#   - the same cases give no failure from a checker that finds nothing, so
#     the cases would notice a check that was switched off;
#   - check-style of build.sh runs the check, and the section sign of a
#     cross reference to a number is made by the generator ({n:label}).
#
#     python3 -I -B tests/editorial.py

import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
TOOL = os.path.join(ROOT, "manual", "checkeditorial.py")

fails = 0


def check(name, ok, detail=""):
    """Print one result and count a failure."""
    global fails
    if ok:
        print("ok   %s" % name)
    else:
        fails += 1
        print("FAIL %s" % name)
        if detail != "":
            print("       " + detail.replace("\n", "\n       "))


def run(tool, text, name="x.src"):
    """Run the checker on a source made of text; the exit status and the
    output."""
    d = tempfile.mkdtemp()
    try:
        path = os.path.join(d, name)
        with open(path, "w") as f:
            f.write(text)
        p = subprocess.run([sys.executable, "-I", "-B", tool, path],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           universal_newlines=True)
        return p.returncode, p.stdout
    finally:
        shutil.rmtree(d)


LONG = ("The Editor keeps the text of the document in memory until the "
        "document is saved to a file.")

TABLE_PERIOD = (".table 10 40\nA | B\nx | The first one is whole.\n"
                "y | The second one is whole.\nz | The third has no end\n"
                ".end\n")

# What breaks a rule: (name, source, the rule that must be named).
BAD = [
    ("a filler word", "It is very fast.\n", "E1"),
    ("the filler just", "It just works.\n", "E1"),
    ("narration with we", "We think that it works.\n", "E2"),
    ("narration with you will", "Then you will see the panel.\n", "E2"),
    ("a future word", "This is planned for later.\n", "E3"),
    ("the future tense", "The panel will open.\n", "E3"),
    ("a stale phrase", "That is not yet done.\n", "E3"),
    ("a hedge", "The key might work.\n", "E4"),
    ("a contraction", "The key doesn't work.\n", "E5"),
    ("a unit glued to its number", "The margin is 7mm wide.\n", "E6"),
    ("a thousands separator", "A text of 65,000 characters.\n", "E6"),
    ("one space after a sentence", "It ends here. Another one starts.\n",
     "E7"),
    ("three spaces", "Two words   apart.\n", "E7"),
    ("two spaces inside a sentence", "Two words  apart.\n", "E7"),
    ("British colour", "The colour is red.\n", "E8"),
    ("British centre", "Put it in the centre.\n", "E8"),
    ("British behaviour", "The behaviour is fixed.\n", "E8"),
    ("neighbour", "Look at the neighbour.\n", "E8"),
    ("the word dialogue", "A dialogue opens.\n", "E8"),
    ("the word folder", "Open the folder.\n", "E8"),
    ("the word popup", "A popup opens.\n", "E8"),
    ("a British spelling in an index entry", ".h1 A [a]\n.ix Colours\n",
     "E8"),
    ("a British spelling in a headline", ".h2 The colour [c]\n", "E8"),
    ("a typed chapter", "See chapter 5 for it.\n", "E9"),
    ("a typed appendix", "See Appendix B for it.\n", "E9"),
    ("a headline in title case", ".h2 The Text Area [a]\n", "E10"),
    ("a list that ends differently", "- One item.\n- Another item\n", "E11"),
    ("a sentence that begins with This is",
     "The cursor moves.  This is the cause.\n", "E12"),
    ("a sentence said twice", LONG + "\n\nSome other text.\n\n" + LONG + "\n",
     "E13"),
    ("a section that announces itself",
     ".h2 A [a]\nThis section describes the thing.\n", "E14"),
    ("a column whose cells end differently", TABLE_PERIOD, "E15"),
    ("a row that is the rest of the row before",
     ".table 20 40\nName | Text\nthe first | The first part of it that\n"
     "was cut |\n.end\n", "E16"),
    ("a reference with a title inside parentheses",
     "The key (see {x:file-save}) works.\n", "E18"),
    ("the word Appendix typed in front of a reference",
     "The keys are in Appendix {n:appterm}.\n", "E19"),
    ("the word Appendices typed in front of a reference",
     "See Appendices {x:appterm} and {x:appx11}.\n", "E19"),
    ("the words See also", ".h3 See also\nThe key.  See also {x:file-save}.\n",
     "E23"),
    ("a table of messages that is not sorted",
     ".table 25 30\nMessage | Meaning\nBeta | The second.\nAlpha | The first.\n"
     ".end\n", "E24", "messages9.src"),
    ("a marker entry without Edges",
     ".h2 Thing [marker-thing]\nThe text.\n.h3 On the screen\nA.\n"
     ".h3 When printed\nB.\n", "E20"),
    ("the word section before a reference",
     "Read the section {x:file-save} first.\n", "E22"),
    ("a File command without its See section",
     ".h2 Save [file-save]\nThe text.\n.h3 The Save panel\nA.\n"
     ".h3 Save at the edges\nB.\n", "E21"),
    ("a command over the lines of a table",
     ".table 20 40\nName | Text\nkey | See {c:one two\n | three} now.\n"
     ".end\n", "E17"),
    ("a table wider than the line",
     ".table 40 40\nName | Text\nkey | The text.\n.end\n", "E27"),
    ("a table of steps with no header row",
     ".table 8 40\n1 | Press the key.\n2 | Type the name.\n.end\n", "E29"),
    ("the plural appendixes", "The appendixes are listed.\n", "E8"),
    ("the name LaTeX in lower case", "The latex system runs.\n", "E26"),
    ("the name LaTeX as Latex", "The Latex system runs.\n", "E26"),
    ("the name TeX in lower case", "The tex system runs.\n", "E26"),
    ("the format PDF in lower case", "A pdf file is made.\n", "E26"),
    ("the character set utf8 in plain text", "It is set to utf8 now.\n",
     "E26"),
    ("the name PostScript as Postscript", "A Postscript file.\n", "E26"),
    ("a wrong name in a headline", ".h2 The latex system [a]\n", "E26"),
    ("a name that the generator would leave plain",
     "The TeX/LaTeX pair is used.\n", "E25"),
    ("software that says something",
     "The Editor says that the work is done.\n", "E30"),
    ("a panel that asks", "The panel asks for a name.\n", "E30"),
    ("a message line that tells", "The message line tells what happened.\n",
     "E30"),
    ("the pronoun it with a verb of a person",
     "If the key is missing, it decides at once.\n", "E30"),
    ("software that wants something", "TeX wants a package.\n", "E30"),
    ("the rule E30 in a source that is not the Reference Manual",
     "The panel asks for a name.\n", "E30", "cheat-x.src"),
    ("the word bug", "A bug is fixed.\n", "E31"),
    ("the word bug in the program font", "Run {c:bug} now.\n", "E31"),
    ("the word bugs", "Many bugs are known.\n", "E31"),
    ("the rule E31 in a source that is not the Reference Manual",
     "A bug is fixed.\n", "E31", "cheat-x.src"),
    ("the pronoun it with a modal verb of intention",
     "The Editor reads the file and it would refuse a bad one.\n", "E32"),
    ("the pronoun they with a verb of speech for software",
     "The Viewer starts, and they say nothing.\n", "E32"),
    ("a terminal that cannot see", "A terminal cannot see a modifier key.\n",
     "E32"),
    ("software that would refuse", "LaTeX would refuse the equation.\n",
     "E32"),
    ("the rule E32 in a source that is not the Reference Manual",
     "A terminal cannot see a modifier key.\n", "E32", "cheat-x.src"),
]

# What follows the rules or is not checked: (name, source, file name).
GOOD = [
    ("a pronoun for software with a mechanical verb",
     "The Viewer starts and shows the first page.\n", "x.src"),
    ("a pronoun that is the object of a verb",
     "Delete and Backspace remove it like any character.\n", "x.src"),
    ("a terminal that sends a code",
     "A terminal sends the same code for both keys.\n", "x.src"),
    ("a pronoun for a thing that is not software",
     "The table holds three rows and it would not help to add more.\n",
     "x.src"),
    ("references to appendices without a typed word",
     "The keys are in {n:appterm} and {n:appx11}.\n", "x.src"),
    ("plain text that follows the rules",
     "A keycap {k:Ctrl-S} saves the document.  The margin is 7 mm.\n"
     "Press the key.\n", "x.src"),
    ("a table of messages that is sorted",
     ".table 25 30\nMessage | Meaning\nAlpha | The first.\nbeta | The second.\n"
     "Gamma | The third.\n.end\n", "messages9.src"),
    ("the program font", "Set {c:very_long_name} to {c:65,000}.\n", "x.src"),
    ("a quoted message", 'The line "It will work, we think" is shown.\n',
     "x.src"),
    ("a code block", ".code\nIt is very 7mm   wide.  We\n.end\n", "x.src"),
    ("a comment", "# It is very good, we think.\n", "x.src"),
    ("a headline in sentence case", ".h2 The text area [a]\n", "x.src"),
    ("a headline with the name of a command",
     ".h2 Using the Save command [a]\nPress Save to write the document.\n",
     "x.src"),
    ("a list that ends alike", "- One item.\n- Another item.\n", "x.src"),
    ("a list with semicolons", "- One item;\n- Another item.\n", "x.src"),
    ("a column that ends alike",
     ".table 10 40\nA | B\nx | The first one is whole.\n"
     "y | The second one is whole.\nz | The third is whole.\n.end\n",
     "x.src"),
    ("a row that continues with a space",
     ".table 20 40\nName | Text\nthe first | The first part of it that\n"
     " was cut | goes on.\n.end\n", "x.src"),
    ("a reference with the number alone inside parentheses",
     "The key (see {n:file-save}) works, as {x:file-save} says.\n",
     "x.src"),
    ("a command that is closed in its line",
     ".table 20 40\nName | Text\nkey | See {c:one two three} now.\n.end\n",
     "x.src"),
    ("an abbreviation", "Use a key, e.g. Ctrl-S, to save.  It works.\n",
     "x.src"),
    ("the allow file", "Just: | Left, Right\n", "x.src"),
    ("a name in the program font", "Run {c:latex} and {c:pdflatex} now.\n",
     "x.src"),
    ("the names in their one spelling",
     "LaTeX and TeX, pdfTeX, PDF, DVI, ASCII and UTF-8.\n", "x.src"),
    ("a message label and a package that hold a name",
     "The label latex-marker-off and the package texlive-latex-base.\n",
     "x.src"),
    ("a lower case name in an index entry", ".h1 A [a]\n.ix latex, dvipng\n",
     "x.src"),
    ("the extension e-TeX", "An extension of the system (e-TeX) is used.\n",
     "x.src"),
    ("a section that states the thing",
     ".h2 A [a]\nA marker is an object in the text.\n", "x.src"),
    ("a second sentence that begins with It",
     "The cursor moves.  It stays in the block.\n", "x.src"),
    ("a panel that prompts, and a table that says",
     "The panel prompts for a name.  The table says what it holds.\n",
     "x.src"),
    ("software that shows, reports and rejects",
     "The Editor shows a box.  The Printer reports the error.  A check "
     "rejects a name.\n", "x.src"),
    ("a quoted message with a verb of a person",
     'The line "The Editor says no" is shown.\n', "x.src"),
    ("a verb of a person in the program font",
     "Set {c:panel asks} first.\n", "x.src"),
    ("the words defect and error", "A defect of the software is an error.\n",
     "x.src"),
]


def run_cases(tool):
    """The number of cases that did not come out as expected."""
    wrong = 0
    for name, text, rule, *fname in BAD:
        code, out = run(tool, text, *(fname or ["x.src"]))
        if code != 1 or (" %s [" % rule) not in out:
            wrong += 1
    for name, text, fname in GOOD:
        code, out = run(tool, text, fname)
        if code != 0:
            wrong += 1
    return wrong


# 1. The sources of the manual pass.
p = subprocess.run([sys.executable, "-I", "-B", TOOL],
                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                   universal_newlines=True)
check("the sources of the manual follow the editorial rules",
      p.returncode == 0, "\n".join(p.stdout.split("\n")[:12]))

# 2. Every rule fails on a source that breaks it.
for name, text, rule, *fname in BAD:
    code, out = run(TOOL, text, *(fname or ["x.src"]))
    check("%s fails the rule %s" % (name, rule),
          code == 1 and (" %s [" % rule) in out, out[:200])

# 3. What follows the rules passes.
for name, text, fname in GOOD:
    code, out = run(TOOL, text, fname)
    check("%s passes" % name, code == 0, out[:200])

# 4. A checker that finds nothing fails the cases.
d = tempfile.mkdtemp()
try:
    fake = os.path.join(d, "checkeditorial.py")
    with open(fake, "w") as f:
        f.write("import sys\nprint('checkeditorial: 0 hit(s)')\n"
                "sys.exit(0)\n")
    check("a checker that finds nothing fails the cases",
          run_cases(fake) == len(BAD))
    check("the real checker has no case that fails", run_cases(TOOL) == 0)
finally:
    shutil.rmtree(d)

# 5. The terms file and the build.
terms = os.path.join(ROOT, "docs", "MANUAL-TERMS.txt")
with open(terms) as f:
    text = f.read()
wrong = [ln for ln in text.split("\n") if ln.startswith("WRONG ")]
check("the terms file has the lines of the wrong forms", len(wrong) >= 7)
check("every wrong form names the right one",
      all(len(ln[6:].split(" => ")) >= 2 for ln in wrong))
bad = []
for ln in wrong:
    try:
        re.compile(ln[6:].split(" => ")[0])
    except re.error:
        bad.append(ln)
check("every pattern of the terms file is a regular expression", bad == [],
      "\n".join(bad))
check("the terms file is plain text of at most 80 columns",
      all(len(ln) <= 80 for ln in text.split("\n")) and
      all(ord(c) < 128 for c in text))
with open(os.path.join(ROOT, "manual", "build.sh")) as f:
    build = f.read()
m = re.search(r"function check-style\n\{.*?\n\}\n", build, re.DOTALL)
check("check-style runs the editorial check",
      m is not None and "checkeditorial.py" in m.group(0))
check("the help of build.sh says that check-style has the editorial rules",
      "editorial" in build)

# 6. The cross references of the generator.
GEN = os.path.join(ROOT, "manual", "mkmanual.py")


def generate(text):
    """Make a document from a source; the exit status, the output of the
    generator and the bytes of the document."""
    d2 = tempfile.mkdtemp()
    try:
        src = os.path.join(d2, "t.src")
        out = os.path.join(d2, "t.doc")
        with open(src, "w") as f:
            f.write(text)
        p2 = subprocess.run([sys.executable, "-I", "-B", GEN, src, out],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            universal_newlines=True)
        data = b""
        if os.path.exists(out):
            with open(out, "rb") as f:
                data = f.read()
        return p2.returncode, p2.stdout, data
    finally:
        shutil.rmtree(d2)


code, out, data = generate(".doc T\n.h1 A [one]\nSee {n:one}.\n")
check("{n:label} is accepted by the generator", code == 0, out[:200])
code, out, data = generate(".doc T\n.h1 A [one]\nSee {n:none}.\n")
check("{n:label} with no such label is left as the label, as {x:label}",
      code == 0 and b"[none]" in data, out[:200])

code, out, data = generate(".doc T\n.h1 A [a]\n.table 40 40\nName | Text\n"
                           "x | y\n.end\n")
check("the generator fits the widths of a table that is wider than the line",
      code == 0 and b"CW30" in data and b"CW40" not in data, out[:200])
spec = importlib.util.spec_from_file_location("mkgen", GEN)
mkgen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mkgen)
# A counted item is a Counter marker (17) and no typed number.  The first item
# of a run sets the counter to 1, the next ones only add one, and a run that
# follows other text sets it to 1 again.
code, out, data = generate(".doc T\n.h1 A [a]\n+ one\n+ two\n+ three\n\n"
                           "Text.\n\n+ again\n")
counter = mkgen.MARK + b"17("
check("a counted item is a Counter marker: four markers, no typed number",
      code == 0 and data.count(counter) == 4 and b"1. " not in data, out[:200])
check("only the first item of each run sets the counter to 1",
      data.count(counter + b"21" + mkgen.CRLF) == 2, out[:200])
check("the text of a counted item follows the marker and a tab",
      data.count(b" \x1aone") == 1 and data.count(b" \x1aagain") == 1)
check("the index lists a term without its leading article",
      mkgen.index_term("The font list") == "Font list" and
      mkgen.index_term("An index") == "Index" and
      mkgen.index_term("Theory of fonts") == "Theory of fonts" and
      mkgen.index_term("The") == "The")

print("editorial: %s" % ("all passed" if fails == 0 else
                         "%d failed" % fails))
sys.exit(1 if fails > 0 else 0)
