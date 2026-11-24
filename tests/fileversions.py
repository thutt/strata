#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This test reads the sources of the Reference Manual and checks the two
# places that document the versions of the file format:
#
#   - the section File versions (file0.src) says what a version is (the
#     number after the letter R in the first line, the current version 3,
#     the older version 2), what every program does with a version, shows
#     the three errors with their exact texts, and says that --version
#     writes the version of the file format;
#   - the section Backup files (file1.src) has the part on the special
#     backup: its name, that it is saved next to the file, that it is not
#     written again, that it is permanent, that a failure refuses the load
#     and that Restore Backup lists it and restores the original;
#   - the glossary, the file types and Restore Backup refer to them.
#
# Every check is run again on a copy of the sources that is made wrong in
# one way (a text taken out) and must fail there.
#
#     python3 -I -B tests/fileversions.py

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = os.path.dirname(HERE)
SRC = os.path.join(TREE, "manual", "src")

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


def read(name):
    with open(os.path.join(SRC, name), encoding="ascii") as f:
        return f.read()


def load():
    """The sources that the checks read, by name."""
    return {n: read(n) for n in ("file0.src", "file1.src", "str4.src",
                                 "glossary.src")}


def section(text, label, level):
    """The lines of the section with the label, up to the next heading of
    the same or a higher level, as one string."""
    lines = text.split("\n")
    out = []
    inside = False

    for line in lines:
        m = re.match(r"\.h([123]) .*\[([^\]]+)\]", line)

        if m is not None:
            if inside and int(m.group(1)) <= level:
                break

            if m.group(2) == label:
                inside = True

        if inside:
            out.append(line)

    return "\n".join(out)


def first_column(text):
    """The first column of every table row of the text, joined with
    spaces, and the other lines as they are; white space is collapsed."""
    parts = []

    for line in text.split("\n"):
        parts.append(line.split(" | ")[0] if " | " in line else line)

    return re.sub(r"\s+", " ", " ".join(parts))


def flat(text):
    """The text with the white space collapsed."""
    return re.sub(r"\s+", " ", text)


ERRORS = (
    "The file is version 4. This software reads up to version 3.",
    "The file version is 1. This software reads versions 2 to 3.",
    "The file is not a document of this software.",
)


def run(t):
    """All the checks on the sources t; returns the names that failed."""
    bad = []

    def ok(name, cond):
        if not cond:
            bad.append(name)

    sec = section(t["file0.src"], "file-versions", 2)
    ok("the section File versions exists",
       sec.startswith(".h2 File versions [file-versions]"))
    ok("it names the first line and the current version 3",
       "*R3" in sec and "The current version is {b:3}" in flat(sec))
    ok("the text states that version 2 is the older version and is read",
       "*R2" in sec and "version 2" in sec and "read" in sec)
    ok("the text states that only the current version is written",
       "writes only the current version" in flat(sec))
    ok("the text states that no program lowers the version",
       "lowers the version of a file" in flat(sec))
    errs = section(t["file0.src"], "file-versions-errors", 3)
    col = first_column(errs)

    for e in ERRORS:
        ok("the error text is exact: " + e, '"' + e + '"' in col)

    ok("the text states which programs report the errors",
       "Document Manager" in flat(errs) and "Printer" in errs and
       "Editor" in errs)
    ok("the text states what to do with a file that is too new",
       "Use a newer version" in flat(errs))
    soft = section(t["file0.src"], "file-versions-software", 3)
    ok("the text states that --version writes the file format version",
       "--version" in soft and "file format 3" in soft)
    ok("the text states that the notes of a release list the number",
       "notes of a release list" in flat(soft))
    ok("the file types refer to the special backup",
       "{n:file-backup-special}" in t["file0.src"])
    sp = section(t["file1.src"], "file-backup-special", 3)
    ok("the special backup has its section", sp.startswith(
        ".h3 The special backup [file-backup-special]"))
    ok("the name has the form V<old-version>-<filename>.<extension>",
       "V<old-version>-<filename>.<extension>" in sp)
    ok("the name is shown with examples V2-memo.doc and V2-standard.set",
       "V2-memo.doc" in sp and "V2-standard.set" in sp)
    ok("it is saved next to the file",
       "next to the file" in flat(sp))
    ok("it is not written again", "is not written again" in flat(sp))
    ok("it is saved by the Editor when the load is finished",
       "the Editor saves a special backup" in flat(sp) and
       "load is finished" in flat(sp))
    ok("a failure refuses the load and leaves the original",
       "the load is refused" in flat(sp) and
       "is not touched" in flat(sp))
    ok("it is permanent and Backups Kept does not remove it",
       "permanent" in sp and "Backups Kept does not count it" in flat(sp))
    ok("Restore Backup lists it and restores the original file",
       "Restore Backup lists it" in flat(sp) and
       "Restoring it writes the file back as it was" in flat(sp) and
       "whatever version number it carries" in flat(sp))
    ok("Backups Kept says that the number does not apply to it",
       "does not apply to the special backup" in flat(
           section(t["file1.src"], "file-backup-option", 3)))
    ok("Restore Backup of the Document Manager lists it",
       "A special backup, the copy that the Editor made of a file of an "
       "older version" in flat(t["str4.src"]) and
       "{n:file-backup-special}" in t["str4.src"])
    ok("the glossary has the terms File version and Special backup",
       ".term File version |" in t["glossary.src"] and
       ".term Special backup |" in t["glossary.src"])
    return bad


def main():
    t = load()
    bad = run(t)
    check("the sources document the file versions and the special backup",
          not bad, "\n".join(bad))

    # The checks again on sources that are made wrong in one way.
    mutations = []

    def mut(name, key, old, new):
        mutations.append((name, key, old, new))

    mut("the first error text changed", "file0.src",
        "reads up to version 3.", "reads up to version 9.")
    mut("the second error text changed", "file0.src",
        "reads versions 2 to 3.", "reads versions 1 to 3.")
    mut("the third error text changed", "file0.src",
        "is not a document of this", "is not a file of this")
    mut("the current version is not 3", "file0.src",
        "The current version is\n{b:3}.", "The current version is\n{b:4}.")
    mut("only the current version is written: taken out", "file0.src",
        "writes only the current version", "writes any version")
    mut("--version no longer writes the file format", "file0.src",
        "such\nas {c:file format 3}", "such\nas {c:format 3}")
    mut("the special backup name form changed", "file1.src",
        "{c:V<old-version>-<filename>.<extension>}",
        "{c:V<filename>.<extension>}")
    mut("the special backup is written again", "file1.src",
        "it is not written again", "it is written again")
    mut("the special backup is not permanent", "file1.src",
        "The special backup is permanent.", "The special backup is kept.")
    mut("the special backup is saved elsewhere", "file1.src",
        "It is saved next\nto the file", "It is saved in a directory")
    mut("a failure no longer refuses the load", "file1.src",
        "the load is refused", "the load goes on")
    mut("the glossary has no File version", "glossary.src",
        ".term File version |", ".term Version of a file |")
    mut("Restore Backup does not list the special backup", "str4.src",
        "A special\nbackup, the copy that the Editor made", "A backup")

    for name, key, old, new in mutations:
        u = dict(t)

        if old not in u[key]:
            check("mutation can be applied: " + name, False, old)
            continue

        u[key] = u[key].replace(old, new, 1)
        check("the check fails when " + name, bool(run(u)))

    print("fileversions: %s" % ("all passed" if fails == 0 else
                                "%d FAILURES" % fails))
    return 1 if fails else 0


sys.exit(main())
