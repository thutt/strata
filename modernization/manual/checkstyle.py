#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program fails when the sources of the manual break the style rules
# that every document follows (the generator has the means for each one):
#
#   S1  A command, an option, a file name, a path, an environment variable,
#       a setting key and the name of a program that the system provides
#       are set in the program font, {c:...}, in running text.  Plain text
#       that looks like one of them is a hit.
#   S2  A shell variable is written ${name}, with braces, in the text, in
#       the program font and in a code block.
#   S3  An example that the reader types is in a block of its own (.code
#       and .end), not in a sentence: a hit for the program font that holds
#       a command line (a command of the system or a path to a script,
#       followed by two or more words) inside a sentence or a table.
#   S4  The words are privileges (not rights), system (not computer) and
#       prefix directory (not prefix on its own).
#
#     python3 -I manual/checkstyle.py [FILE.src ...]
#
# With no argument every .src file next to this program is checked.  The
# file checkstyle.allow, next to this program, holds the allowed phrases:
# one phrase per line, matched without regard to case; a line that begins
# with a number sign is a comment.  A phrase is left alone together with
# the 40 characters around it when the plain text is looked at.
# Directives (except the text of .term, .note, .caution and .behavior),
# comments, headlines, index lines, keycaps, equations, LaTeX markers,
# labels and quoted words (the messages of the programs, which are written
# as they appear) are not checked for the rules about plain text, and the
# files of the messages are not checked at all; the code blocks are checked
# for S2 only.  The exit status is 1 when a hit is left.
#
# The rule of the program names and of the original is checknames.py and
# is not repeated here.

import glob
import os
import re
import sys

# The commands of the generator whose text is not plain text: keycaps,
# program-font text, equations, LaTeX markers, labels, cross references,
# footnotes, and the styled text of a bold, italic or underlined run.
PROTECTED = re.compile(
    r'"[^"\n]*"|\{(?:k|c|eq|latex|x|X|n|ref|p|fn|fig|Fig|b|i|u):'
    r'(?:[^{}]|\{\{|\}\})*\}|\[[a-z0-9-]+\]\s*$', re.MULTILINE)

# S2: a shell variable without braces.  A dollar sign, a letter or an
# underscore, and no brace after the dollar sign.
VARIABLE = re.compile(r"(?<![\\$])\$[A-Za-z_][A-Za-z0-9_]*")

# S1: plain text that looks like a path, a file name, an option, an
# environment variable, a setting key or the command of a system.
TILDE_PATH = re.compile(r"(?<![\w/.{-])~(?:/[\w./{}$-]*)?")
ABS_PATH = re.compile(
    r"(?<![\w.{-])/(?:usr|etc|bin|sbin|home|tmp|var|opt|dev|proc|lib|share|"
    r"run|sys|root)\b[\w./{}$-]*")
SLASH_PATH = re.compile(
    r"(?<![\w/.{~-])(?:[A-Za-z][\w-]*/)+[A-Za-z0-9_.-]+(?![\w/])")
FILE_NAME = re.compile(
    r"(?<![\w/.{-])[\w-]+(?:\.[\w-]+)*\.(?:doc|set|pdf|dvi|tex|sh|txt|mac|"
    r"lib|bak|cfg|conf|py|ps|png|jpg|gz|deb|bundle|log|def|dic|aff)"
    r"(?![\w])")
EXTENSION = re.compile(
    r"(?<![\w}])\.(?:doc|set|pdf|dvi|tex|sh|txt|mac|lib|bak|ps|png|jpg|log)"
    r"(?![\w])")
OPTION = re.compile(r"(?<![\w-])--?[A-Za-z][\w-]*(?![\w-])")
ENV_NAME = re.compile(
    r"(?<![\w{$-])(?:[A-Z][A-Z0-9]*_[A-Z0-9_]+|TMPDIR|HOME|PATH|DISPLAY|"
    r"DESTDIR|SHELL|LANG|TERM|XDG_[A-Z_]+)(?![\w-])")
COMMAND = re.compile(
    r"(?<![\w-])(?:sudo|apt|apt-get|dpkg|chmod|mkdir|pdflatex|aspell|"
    r"hunspell|xterm|lpstat|gzip|ssh|scp|grep|gcc|latex|dvipdfmx|gs|"
    r"kpsewhich|dvipng|pdftoppm|pdftotext|curl|unzip|extractbb|lp|xwd|"
    r"xdotool)(?![\w-])")
SHELL_VAR = re.compile(r"\$\{\{[A-Za-z_][A-Za-z0-9_]*\}\}")
SETTING_KEY = re.compile(r"(?<![\w-])[a-z][a-z0-9]*(?:_[a-z0-9]+)+(?![\w-])")
# Words that have a slash and are ordinary English or a name that is not
# a path.
SLASH_WORDS = {"and/or", "his/her", "he/she", "yes/no", "on/off",
               "read/write", "input/output", "either/or", "up/down",
               "left/right", "A/B", "I/O", "w/o", "n/a", "N/A", "TCP/IP",
               "km/h", "m/s", "M/D/YY", "C6/5", "D/M/YY"}

# S3: the text of the program font that holds a command line.
COMMAND_LINE = re.compile(
    r"^(?:sudo\s+)?(?:(?:apt-get|apt|dpkg|chmod|mkdir|pdflatex|latex|"
    r"aspell|hunspell|xterm|lp|gs)|(?:\./|\.\./|/|~/|\$\{)[\w./~${}-]*|"
    r"scripts/[\w./-]+)(?:\s+\S+){2,}$")

# S4: words.
BANNED = re.compile(
    r"\b(?:root|admin|administrator|administrative|sudo|superuser|"
    r"user)\s+rights\b|\b(?:your|the|a|any|this|that)\s+computers?\b|"
    r"(?<![\w-])prefix(?!\s+director)(?![\w-])",
    re.IGNORECASE)


def allowed_phrases(here):
    """The phrases of checkstyle.allow, as patterns."""
    path = os.path.join(here, "checkstyle.allow")
    out = []
    if os.path.exists(path):
        with open(path, encoding="ascii") as f:
            for line in f:
                line = line.rstrip("\n")
                if line == "" or line.startswith("#"):
                    continue
                out.append(re.compile(re.escape(line), re.IGNORECASE))
    return out


def s1_spans(text):
    """The (start, end) spans of the plain text that look like a path, a
    file name, an option, an environment variable, a setting key or a
    command of the system, with the punctuation at the end of each left
    out."""
    spans = []
    for rx in (TILDE_PATH, ABS_PATH, FILE_NAME, EXTENSION, OPTION,
               ENV_NAME, COMMAND, SETTING_KEY, SHELL_VAR):
        for m in rx.finditer(text):
            spans.append((m.start(), m.end()))
    for m in SLASH_PATH.finditer(text):
        if m.group(0) not in SLASH_WORDS and (
                m.group(0).count("/") > 1 or
                re.search(r"[0-9._-]", m.group(0)) is not None or
                re.match(r"(usr|etc|bin|share|lib|doc|docs|src|tmp|home|"
                         r"var|opt)/", m.group(0)) is not None):
            spans.append((m.start(), m.end()))
    # Leave the punctuation at the end out and merge what overlaps.
    cleaned = []
    for start, end in spans:
        while end > start and text[end - 1] in ".,;:-":
            end -= 1
        if end > start:
            cleaned.append((start, end))
    cleaned.sort()
    merged = []
    for start, end in cleaned:
        if merged and start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    return merged


def regions(text, base):
    """The (start, end, kind) regions of a source that are looked at: kind
    is "code" for a code block and "text" for lines of running text, joined
    so that a command that goes over lines is whole."""
    out = []
    if base.startswith("messages"):
        return out
    pos = 0
    in_code = False
    run = None
    for line in text.split("\n"):
        end = pos + len(line)
        kind = None
        if line.startswith(".code"):
            in_code = True
        elif line.startswith(".end"):
            in_code = False
        elif in_code:
            kind = "code"
        elif line.startswith("#"):
            kind = None
        elif line.startswith("."):
            m = re.match(r"\.(term|note|caution|behavior) ", line)
            if m is not None:
                if m.group(1) == "term":
                    bar = line.find("|")
                    if bar >= 0:
                        out.append((pos + bar + 1, end, "text"))
                else:
                    out.append((pos + m.end(), end, "text"))
        else:
            kind = "text"
        if kind is None:
            if run is not None:
                out.append(run)
                run = None
        elif run is not None and run[2] == kind:
            run = (run[0], end, kind)
        else:
            if run is not None:
                out.append(run)
            run = (pos, end, kind)
        pos = end + 1
    if run is not None:
        out.append(run)
    return out


def plain_segments(text, start, end):
    """The (start, end) spans of text[start:end] that are plain text: not a
    command of the generator that is not text, and not a quoted word."""
    segs = []
    pos = start
    for m in PROTECTED.finditer(text, start, end):
        if m.start() > pos:
            segs.append((pos, m.start()))
        pos = max(pos, m.end())
    if pos < end:
        segs.append((pos, end))
    return segs


def line_of(text, offset):
    """The line number of an offset."""
    return text.count("\n", 0, offset) + 1


def check(path, phrases):
    """The list of (line number, rule, hit, line) of one file."""
    with open(path, encoding="ascii", errors="replace") as f:
        text = f.read()
    base = os.path.basename(path)
    lines = text.split("\n")
    hits = []
    for start, end, kind in regions(text, base):
        if kind == "code":
            for m in VARIABLE.finditer(text, start, end):
                num = line_of(text, m.start())
                hits.append((num, "S2", m.group(0), lines[num - 1]))
            continue
        # S2 holds in the program font too: look at all but the equations.
        masked = re.sub(r"\{(?:eq|latex):(?:[^{}]|\{\{|\}\})*\}",
                        lambda m: " " * len(m.group(0)), text[start:end])
        for m in VARIABLE.finditer(masked):
            num = line_of(text, start + m.start())
            hits.append((num, "S2", m.group(0), lines[num - 1]))
        # S3: the program font that holds a command line.
        for m in re.finditer(r"\{c:((?:[^{}]|\{\{|\}\})*)\}",
                             text[start:end]):
            body = m.group(1).replace("{{", "{").replace("}}", "}")
            if COMMAND_LINE.match(" ".join(body.split())) is not None:
                num = line_of(text, start + m.start())
                hits.append((num, "S3", m.group(0)[:40], lines[num - 1]))
        for a, b in plain_segments(text, start, end):
            seg = text[a:b]
            for st, en in s1_spans(seg):
                ctx = seg[max(0, st - 40):en + 40]
                if any(p.search(ctx) for p in phrases):
                    continue
                num = line_of(text, a + st)
                hits.append((num, "S1", seg[st:en], lines[num - 1]))
            for m in BANNED.finditer(seg):
                ctx = seg[max(0, m.start() - 40):m.end() + 40]
                if any(p.search(ctx) for p in phrases):
                    continue
                num = line_of(text, a + m.start())
                hits.append((num, "S4", m.group(0), lines[num - 1]))
    hits.sort()
    return hits


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    files = argv[1:] or sorted(glob.glob(os.path.join(here, "src",
                                                       "*.src")))
    phrases = allowed_phrases(here)
    total = 0
    per_file = {}
    per_rule = {}
    for path in files:
        for num, rule, word, line in check(path, phrases):
            total += 1
            name = os.path.basename(path)
            per_file[name] = per_file.get(name, 0) + 1
            per_rule[rule] = per_rule.get(rule, 0) + 1
            print("%s:%d: %s [%s] %s" % (name, num, rule, word,
                                         line.strip()[:80]))
    print("checkstyle: %d hit(s) in %d file(s)" % (total, len(files)))
    if os.environ.get("CHECKSTYLE_COUNT"):
        for name in sorted(per_file):
            print("  %-28s %d" % (name, per_file[name]))
        for rule in sorted(per_rule):
            print("  %s %d" % (rule, per_rule[rule]))
    return 1 if total > 0 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
