#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program writes manual/src/equations-t.src, the part of chapter 9 that
# lists the command words of the math notation one group at a time, from the
# list of formulas in eqdata.txt.  Every formula of the list was typeset by
# LaTeX with the wrapper that the editor uses (chk.py) and passes the checks
# of the printing program (okcheck.py in the same spirit).
#
#     python3 -I manual/eqwork/eqgen.py manual/eqwork/eqdata.txt \
#         manual/src/equations-t.src manual/eqwork/eqexamples.txt \
#         manual/src/equations-x.src
#
# The last two arguments are optional: eqexamples.txt (a line "title ;; text
# ;; TeX" for each worked example) is made into equations-x.src, which
# equations-d.src includes.

import re
import sys
import textwrap

WIDTH = 78

# Each group is (label, title, display, introduction, [sections], closing).
# The sections are the names of the "# SECTION name | title" lines of the
# data file; a group with several sections gets a third level headline for
# each.  A group with display set shows its results in display size.
GROUPS = [
    ("eq-greek", "Greek letters", 0,
     """The Greek letters are commands made of the English name of the letter.
A name that begins with a small letter gives the small letter and a name that
begins with a capital gives the capital.  Where a capital Greek letter looks
like a Latin capital (Alpha, Beta, Epsilon, Zeta, Eta, Iota, Kappa, Mu, Nu,
Omicron, Rho, Tau, Chi) TeX has no command for it: type the Latin letter,
which is set upright, as capitals of math are.  A small Greek letter is set
in the italic of math.  The names that begin with var give the second form
of the letter that some fonts have; the names that are marked amssymb are
there because the equation is typeset with the package amssymb.""",
     ["lowergreek", "uppergreek"],
     """A capital Greek letter is upright by default.  The forms with var
(\\varGamma and the others) are the slanted capitals; \\boldsymbol (
{x:eq-fonts}) makes any of them bold."""),
    ("eq-binary", "Binary operators", 0,
     """A binary operator is a sign that stands between two things, as the plus
sign does.  TeX puts a medium space on both sides of it, unless it stands at
the start of the formula or follows another operator or a relation, in which
case it is a sign of one operand (compare {eq:a-b} with {eq:-b}).  To have the
sign without spaces, put it in a group ({c:a{{+}}b}, {x:eq-spacing}).
The signs that are marked amssymb are less usual ones.""",
     ["binary"],
     """To make a symbol that is not an operator behave like one, use
\\mathbin ({x:eq-spacing})."""),
    ("eq-relations", "Relations", 0,
     """A relation is a sign that says how two things stand to each other, such
as the equals sign.  TeX puts a thick space on both sides of it.  The negation
of a relation is made by putting \\not in front of the command: {eq:\\not\\equiv}.
Many relations have a ready made negation (\\neq, \\notin, \\nless and others), which
is better set than \\not.  The relations that are marked amssymb are less
usual ones.""",
     ["relations"],
     """The sign < is the less than sign and > the greater than sign, typed as
they are.  Do not use the angle brackets of the keyboard for the brackets of
a vector: use \\langle and \\rangle ({x:eq-delimiters})."""),
    ("eq-arrows", "Arrows", 0,
     """The arrows are relations, so they have a thick space on both sides.
The names say the direction: left, right, up, down, and the long forms, whose
names begin with long, are longer arrows.  A name that begins with a capital
(Rightarrow) is the double arrow.  \\implies, \\impliedby and \\iff are the
long double arrows with more space round them, as used between statements.
Arrows that stretch over a label are in {x:eq-accents}
({c:\\xrightarrow}).""",
     ["arrows"],
     ""),
    ("eq-symbols", "Other symbols", 0,
     """The symbols of this group are ordinary symbols: they take no space of their
own.  The dots at the end are special in that the right one depends on the
place: {c:\\ldots} is on the base line (between commas), {c:\\cdots} is at the
middle of the line (between operators).  The dots with names that begin with
dots (the amsmath dots) pick the kind by what follows them.""",
     ["symbols"],
     """The letter h with a bar is \\hbar, and the dotless letters \\imath and
\\jmath are for putting an accent on them ({x:eq-accents})."""),
    ("eq-scripts", "Superscripts and subscripts", 1,
     """The sign ^ raises what follows it to the superscript place and the sign
_ lowers it to the subscript place.  What is taken is the next character or
the next group; to raise or lower more than one character, use braces.  Both
signs can follow one thing, in either order.  The scripts of a script are
smaller again, to three levels.  Scripts that stand before a thing are made
by putting an empty group in front.  The results of this table are set in
display size, as an equation that stands alone: the limits of a sum are above
and below it.  In a line of text the same limits go at the side; the commands
\\limits and \\nolimits change that for one operator.""",
     ["scripts"],
     """The apostrophe is a prime and stands for the superscript {c:^\\prime}; a
second apostrophe makes a double prime.  A script on something that is
already scripted (x^2^3) is an error of TeX, with the message "Double
superscript"; put one of the two in a group."""),
    ("eq-fractions", "Fractions, binomials and roots", 1,
     """\\frac takes two arguments, the numerator and the denominator, and
\\sqrt one, the radicand; with the square brackets {c:[n]} before the argument
it takes the n-th root.  The variants \\dfrac and \\tfrac force the display
and the text size of the fraction, and \\cfrac sets a continued fraction with
full sized parts.  \\binom is the binomial coefficient: two arguments, no bar,
between large parentheses.  The old TeX forms {c:{{a \\over b}}} and
{c:{{n \\choose k}}} also work: the command takes everything in the group, to
the left and to the right.  The results are set in display size.""",
     ["fractions"],
     """In a line of text a fraction is small: its parts are set in the
size of a script.  In an equation that stands alone the fraction is large.  Begin an inline
equation with {c:\\displaystyle} to get the large fraction in a line of
text."""),
    ("eq-bigops", "Large operators and functions", 1,
     """The large operators (sum, product, integral, union ...) are set larger in
a displayed equation and have their limits above and below; in an inline
equation they are small and the limits are at the side.  The limits are typed
as a subscript and a superscript.  The names of the usual functions are
commands: they set the name upright and put the right space round it, and the
ones that have limits (lim, max, min, sup, inf, det, gcd, Pr and others) take
their subscript underneath in a displayed equation.  The results of this table
are set in display size.""",
     ["bigops"],
     """For a function that has no command, use {c:\\operatorname{{name}}}
(or {c:\\operatorname*{{name}}} when it has limits underneath).  Do not
type the letters of the name as they are: they would be set as variables
({eq:sin x} against {eq:\\sin x}).  The package declaration that other
documents use for it ({c:\\DeclareMathOperator}) is refused in an equation
({x:eq-refused})."""),
    ("eq-delimiters", "Delimiters and their sizes", 0,
     """A delimiter is a bracket, a bar or a similar sign.  The keyboard
brackets are delimiters as they are.  The braces, which have a meaning of their
own in the text, are typed {c:\\{{} and {c:\\}}}.  \\left and \\right in front of a
delimiter make a pair that is as large as what is between them; every
\\left needs its \\right in the same group, and a dot ({c:\\left.}) makes one
side empty.  \\big, \\Big, \\bigg and \\Bigg give four fixed larger sizes, and
\\bigl and \\bigr (and the other l and r forms) tell the side so that the
spacing is that of an opening and of a closing sign.  \\middle puts a
delimiter between the other two that grows with them.""",
     ["delimiters"],
     """A vertical bar typed on the keyboard is the vertical line.  Use \\lvert and \\rvert for absolute
values (they have the right spacing) and \\lVert and \\rVert for norms."""),
    ("eq-accents", "Accents, lines and braces", 0,
     """An accent is a command with one argument, the letter that gets it.
Accents over several letters ({c:\\widehat}, {c:\\widetilde}, {c:\\overline},
the braces) stretch.  The arrows that stretch over text and take a label are
{c:\\xrightarrow} and {c:\\xleftarrow}, which have the label as an argument,
and a second label under the arrow in square brackets.""",
     ["accents"],
     """On an i or a j use \\imath or \\jmath, so that the dot does not collide
with the accent.  The accents of text ({c:\\'e} and the others) and letters
with accents that are typed on the keyboard cannot be used in math: TeX stops
with the message "Please use \\mathaccent for accents in math mode" (and the
printing program rejects the characters above 127 in any case).  For
accented words in a formula put them in {c:\\text}."""),
    ("eq-fonts", "Text and fonts", 0,
     """Letters in a formula are variables and are set in the math italic.  The
font commands change that for their argument.  {c:\\text} sets its argument
as text, in the font of the text round the equation and with real spaces;
it is the command for words in a formula.  The font commands for math change
the letters, digits and (for \\boldsymbol) the symbols, but do not make words
with spaces.  The size commands {c:\\displaystyle}, {c:\\textstyle},
{c:\\scriptstyle} and {c:\\scriptscriptstyle} choose the style of what follows
them in the group.""",
     ["fonts"],
     """Bold Greek letters need \\boldsymbol: \\mathbf leaves the Greek
letters as they are.  The letters of \\mathcal and of \\mathbb are meant
for capitals (the small letters of them are not the ones wanted);
\\mathfrak has both cases."""),
    ("eq-spacing", "Spacing", 0,
     """TeX makes the space of a formula from the kind of each sign: an
ordinary sign (a letter, a digit) has none, an operator has a medium space
on both sides, a relation a thick space, a comma a thin space after it.  The
spaces that you type are ignored.  To change the result there are fixed
spaces ({c:\\,}, {c:\\:}, {c:\\;}, {c:\\quad}, {c:\\qquad}), a negative space
({c:\\!}), and commands that blank an area of the size of their argument
(\\phantom, \\hphantom, \\vphantom).  \\mathbin, \\mathrel, \\mathop,
\\mathopen, \\mathclose, \\mathord, \\mathpunct and \\mathinner tell TeX what kind
of sign the argument is, and so which space it gets.  \\hspace and \\vspace are
refused in a printed equation ({x:eq-refused}).""",
     ["spacing"],
     """Between the integral and the differential use the thin space {c:\\,}:
{eq:\\int f(x)\\,dx} against {eq:\\int f(x)dx}."""),
    ("eq-structures", "Matrices, arrays and alignment", 1,
     """The environments are begun with {c:\\begin{{name}}} and ended with
{c:\\end{{name}}}; a row ends with two backslashes, and the cells of a row are
separated by the sign &.  The environments that are allowed in a printed
equation are matrix, pmatrix, bmatrix, Bmatrix, vmatrix, Vmatrix, cases,
array, aligned, gathered, split, smallmatrix and subarray; any other name
makes the equation print in the long form ({x:eq-refused}).  The
name split is on the list but is no use: it needs a displayed formula of
amsmath round it, which an equation of Strata does not have, and TeX
stops with the message "Package amsmath Error: \\begin{{split}} won't work
here"; use aligned in its place.  The
matrix environments have no column specification: every column is centred.
{c:array} has one, a letter for each column: c, l or r, and a bar for a
vertical rule.  {c:\\hline} draws a horizontal rule.  {c:aligned} aligns the
rows at the sign & (an & before the sign), and {c:gathered} centres them.
{c:cases} is a brace with a column of values and a column of conditions.
The results are set in display size.""",
     ["structures"],
     """The environments of amsmath that make a whole displayed formula with its
own number (equation, align, gather, multline, eqnarray) are not allowed:
the equation is already displayed by the program, and numbers are not
supported.  The command {c:\\tag} is not on the list of the refused commands
of the screen, but it makes TeX stop with an error, because the equation is
not in an environment that numbers."""),
]

TITLES = {"lowergreek": "Lower case Greek letters",
          "uppergreek": "Upper case Greek letters"}


def esc(text):
    return text.replace("{", "{{").replace("}", "}}")


def read(path):
    data = {}
    order = []
    cur = None
    for ln in open(path):
        ln = ln.rstrip("\n")
        if ln.startswith("# SECTION"):
            name, _, title = ln[len("# SECTION "):].partition(" | ")
            cur = name
            order.append(name)
            data[name] = (title, [])
        elif ln.startswith("#") or not ln.strip():
            continue
        else:
            tex, _, meaning = ln.partition(" ;; ")
            data[cur][1].append((tex, meaning))
    return data, order


def autoc(text):
    """Put every backslash command of the plain text in the program font;
    the spans of the form {x:...} are left as they are."""
    out = ""
    i = 0
    while i < len(text):
        m = re.match(r"\{[a-z]+:", text[i:])
        if m is not None:
            j = i + m.end()
            while j < len(text):
                if text.startswith("}}", j) or text.startswith("{{", j):
                    j += 2
                elif text[j] == "}":
                    break
                else:
                    j += 1
            out += text[i:j + 1]
            i = j + 1
            continue
        m = re.match(r"\\[A-Za-z]+", text[i:])
        if m is not None:
            out += "{c:%s}" % m.group(0)
            i += m.end()
            continue
        out += text[i]
        i += 1
    return out


def para(text):
    out = []
    for block in text.strip().split("\n\n"):
        # A span must not be broken at its own spaces: the writer of the
        # paragraph joins lines with one space, so this is safe.
        out.append(textwrap.fill(" ".join(autoc(block).split()), WIDTH,
                                 break_on_hyphens=False))
        out.append("")
    return out


def wrap_row(row):
    """Break a long table row at single spaces, with continuation lines."""
    if len(row) <= WIDTH:
        return [row]
    lines = []
    cur = ""
    # A break is allowed at a space that has no space next to it.
    words = []
    i = 0
    start = 0
    while i < len(row):
        if row[i] == " " and row[i - 1] != " " and row[i + 1:i + 2] != " ":
            words.append(row[start:i])
            start = i + 1
        i += 1
    words.append(row[start:])
    for w in words:
        if cur == "":
            cur = w
        elif len(cur) + 1 + len(w) <= WIDTH:
            cur += " " + w
        else:
            lines.append(cur)
            cur = " " + w
    lines.append(cur)
    return lines


def table(entries, display, out):
    out.append(".table 30 18 22")
    out.append("Source | Result | Meaning")
    for tex, meaning in entries:
        if "   " in tex:
            continue
        shown = "\\displaystyle " + tex if display != 0 else tex
        row = "{c:%s} | {eq:%s} | %s" % (esc(tex), esc(shown), esc(meaning))
        out.extend(wrap_row(row))
    out.append(".end")
    out.append("")


def code_lines(tex):
    """The source of an example as lines for a code block, broken at spaces
    (or before a backslash) where a line would be too long."""
    lines = []
    cur = ""
    parts = []
    word = ""
    for ch in tex:
        if (ch == " " or ch == "\\") and word != "":
            if ch == " ":
                parts.append(word + " ")
                word = ""
                continue
            parts.append(word)
            word = ""
        word += ch
    parts.append(word)
    for p in parts:
        if len(esc(cur + p).rstrip()) > 66 and cur != "":
            lines.append(esc(cur.rstrip()))
            cur = ""
        cur += p
    lines.append(esc(cur.rstrip()))
    return lines


def eq_lines(text):
    """A paragraph that is one equation, broken at spaces or before a
    backslash (a space there changes nothing in math)."""
    lines = []
    cur = ""
    for p in re.findall(r"\\?[^\\ ]*[ ]?", text):
        if len(cur) + len(p) > WIDTH and cur != "":
            lines.append(cur.rstrip())
            cur = ""
        cur += p
    lines.append(cur.rstrip())
    return lines


def examples(path, outpath):
    out = ["# Chapter 9, worked examples.  Made by manual/eqwork/eqgen.py from",
           "# manual/eqwork/eqexamples.txt: change the list, not this file.",
           ""]
    n = 0
    with open(path) as f:
        for ln in f:
            ln = ln.rstrip("\n")
            if ln.startswith("#") or ln.strip() == "":
                continue
            title, text, tex = ln.split(" ;; ")
            n += 1
            out.append(".h3 %s [eq-ex-%d]" % (title, n))
            out.append(".ix Example, %s" % title.lower())
            out.extend(para(esc(text)))
            out.append("The text of the equation:")
            out.append(".code")
            out.extend(code_lines(tex))
            out.append(".end")
            out.append("The result:")
            out.append("")
            out.extend(eq_lines("{eq:%s}" % esc(tex)))
            out.append("")
    with open(outpath, "w") as f:
        f.write("\n".join(out).rstrip("\n") + "\n")


def main():
    data, order = read(sys.argv[1])
    out = ["# Chapter 9, third part: the command words of the math notation.",
           "# Made by manual/eqwork/eqgen.py from manual/eqwork/eqdata.txt:",
           "# change the generator or the list, not this file.", ""]
    used = set()
    for label, title, display, intro, sections, closing in GROUPS:
        out.append(".h2 %s [%s]" % (title, label))
        out.append(".ix %s" % title)
        out.extend(para(intro))
        if display != 0:
            out.extend(para("""In the tables of this group the result is typeset with
{c:\\displaystyle} in front of the source, which is not shown, so that it
has the form of an equation that stands alone."""))
        for name in sections:
            used.add(name)
            stitle, entries = data[name]
            if len(sections) > 1:
                out.append(".h3 %s [%s-%s]" % (stitle, label, name))
            table(entries, display, out)
        if closing != "":
            out.extend(para(closing))
    missing = [n for n in order if n not in used]
    if missing:
        sys.exit("sections not used: %s" % missing)
    with open(sys.argv[2], "w") as f:
        f.write("\n".join(out).rstrip("\n") + "\n")
    if len(sys.argv) > 4:
        examples(sys.argv[3], sys.argv[4])


main()
