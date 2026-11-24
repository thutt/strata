#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program checks the log messages of the programs, as the owner asked:
# every message is unique in the whole system, and every message has a
# variable part, so that it shows what failed, with which file or value, and
# why.  It also checks that no program calls the system log.  Run it from
# the root of the source tree:
#
#     python3 -I -B tests/logmsgs.py
#
# The sources checked are src/*.c and tools/dviview/src/*.c.  The message
# is the format string of the call of log_error, log_warn, log_note,
# log_debug or log_fatal (its second argument; the first is the exit
# status).

import glob
import os
import re
import sys

CALL = re.compile(r"\blog_(error|warn|note|debug|fatal)\s*\(")
SYSLOG = re.compile(r"\b(syslog|vsyslog|openlog|closelog)\s*\(")
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"', re.S)
# A conversion that shows a value: % followed by flags, width and a letter,
# but not the two characters %%.
CONVERSION = re.compile(r"%(?!%)[-+ #0-9.*lhzjt]*[diouxXeEfgGcspn]")
MINIMUM_LENGTH = 24


def sources(root):
    found = []
    for pattern in ("src/*.c", "tools/dviview/src/*.c"):
        found += sorted(glob.glob(os.path.join(root, pattern)))
    return found


def strip_comments(text):
    """The text without the comments of C, keeping the line breaks."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text[i] == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def arguments(text, start):
    """The arguments of the call whose '(' is just before start."""
    args = []
    depth = 1
    cur = []
    i = start
    n = len(text)
    while i < n and depth > 0:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            cur.append(text[i:j + 1])
            i = j + 1
            continue
        if c == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            cur.append(text[i:j + 1])
            i = j + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                break
        if c == "," and depth == 1:
            args.append("".join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    args.append("".join(cur))
    return args


def message_of(arg):
    """The text of adjacent string literals, or None when there is none."""
    rest = arg.strip()
    pieces = []
    while rest.startswith('"'):
        m = LITERAL.match(rest)
        if m is None:
            return None
        pieces.append(m.group(1))
        rest = rest[m.end():].lstrip()
    if rest != "" or not pieces:
        return None
    return "".join(pieces)


def main():
    root = os.getcwd()
    if len(sys.argv) > 1:
        root = sys.argv[1]
    seen = {}
    bad = []
    count = 0
    for path in sources(root):
        name = os.path.relpath(path, root)
        with open(path, "r", encoding="latin-1") as f:
            raw = f.read()
        text = strip_comments(raw)
        for m in SYSLOG.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            bad.append("%s:%d: a call of the system log (%s)" %
                       (name, line, m.group(1)))
        if os.path.basename(path) in ("log.c",):
            continue
        for m in CALL.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            prefix = text[max(0, m.start() - 20):m.start()]
            if prefix.rstrip().endswith("#define"):
                continue
            args = arguments(text, m.end())
            index = 1 if m.group(1) == "fatal" else 0
            where = "%s:%d" % (name, line)
            if index >= len(args):
                bad.append("%s: log_%s has no message" % (where, m.group(1)))
                continue
            msg = message_of(args[index])
            if msg is None:
                bad.append("%s: the message of log_%s is not a string "
                           "literal" % (where, m.group(1)))
                continue
            count += 1
            if CONVERSION.search(msg) is None:
                bad.append("%s: the message has no variable part (what "
                           "file, value or reason?): %r" % (where, msg))
            if len(msg) < MINIMUM_LENGTH:
                bad.append("%s: the message is too short to say what "
                           "failed and why: %r" % (where, msg))
            if msg in seen:
                bad.append("%s: the message is not unique, it is also at "
                           "%s: %r" % (where, seen[msg], msg))
            else:
                seen[msg] = where
    for line in bad:
        print("FAIL " + line)
    print("logmsgs: %d messages, %d problems" % (count, len(bad)))
    return 1 if bad else 0


sys.exit(main())
