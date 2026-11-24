#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program keeps and checks the table of the pictures of the manual,
# manual/pictures.txt: one line for every picture file of doc/pictures that
# says how the picture is made, by what and when it was last taken.
#
#     python3 -I manual/checkpictable.py            check the table
#     python3 -I manual/checkpictable.py --write    write the table again
#     python3 -I manual/checkpictable.py --scripts  list the script pictures
#
# The check fails when a picture file has no line, when a line has no picture
# file, when a line says it is made by a scene that no file of scenes names or
# by a program that is not the one that the scene starts, or when a line is
# not made of the six fields.  The date of the last retake is not checked
# (a commit changes it): "--write" takes it from the history of git, and the
# table is written again after pictures are retaken.
#
# A line is six fields separated by " | ":
#
#     NAME | MADE BY | PROGRAM | DOCUMENT | LAST RETAKE | HOW TO MAKE IT AGAIN
#
# MADE BY is "scene" (a scene of shots.py or of scenes/*.py: the PROGRAM is
# the program that the scene starts) or "script" (a page that the Printer
# prints, made by a script of manual/scenes: the PROGRAM is the Printer).
# The lines that begin with a number sign are comments.

import glob
import importlib.util
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PICTURES = os.path.join(HERE, "doc", "pictures")
TABLE = os.path.join(HERE, "pictures.txt")

# The pictures that are pages printed by the Printer and the script that
# makes each one: name, script, arguments after PRINTER.
SCRIPTS = {
    "appchar-print": ("appchar-pdf.sh", "manual/samples/chars.doc"),
    "print-colour": ("colour-pdf.sh", "manual/samples/colour.doc"),
    "print-errlog": ("errlog-pdf.sh", "tests/data/print_errors.doc"),
    "print-failed-page": ("failed-pdf.sh", "tests/data/print_errors.doc"),
    # The example documents of these four are made by scenes/mkpicdocs.py.
    "print-headfoot-page": ("hf-pdf.sh", "headfoot.doc"),
    "print-index-page": ("index-pdf.sh", "indexlist.doc"),
    "print-lists-page": ("lists-pdf.sh", "indexlist.doc"),
    "print-border-page": ("border-pdf.sh", "border.doc"),
    # The sample letters are made by tests/mkletters.py.
    "print-letter-page": ("letter-pdf.sh",
                          "samples/letters/complaint-to-utility-us.doc"),
    "print-envelope-sample": ("envelope-pdf.sh",
                              "samples/envelopes/"
                              "sample-envelope-us-ca-10.doc"),
    # The sample sheet of labels is made by tests/mklabelsample.py.
    "print-label-sample": ("envelope-pdf.sh",
                           "samples/labels/sample-label-us-5160.doc"),
}

HEADER = """\
# The pictures of the manual: one line for every file of doc/pictures.
# The fields: the name of the picture, what makes it (scene: a scene of
# shots.py or of manual/scenes/*.py; script: a page that the Printer prints,
# made by a script of manual/scenes), the program that the scene starts (or
# the Printer), the document that it opens (a document made for the manual,
# never one of the original product; "-" for none), the date of the last
# retake, and how to make it again.
# A scene is made again with "build.sh pics NAME" (it writes into doc/pictures
# of the source tree: the pictures are sources) and checked with "build.sh
# pics-check".  After pictures are retaken run
# "python3 -I manual/checkpictable.py --write" to bring this table up to date;
# "build.sh check-pictable" (part of "build.sh check") fails when a picture
# and a line do not belong together.
"""


def load_shots():
    """The module shots.py, so that its scenes can be listed."""
    spec = importlib.util.spec_from_file_location(
        "shots", os.path.join(HERE, "shots.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def program_of(name):
    """The program that a scene starts, as the name of the program."""
    base = os.path.basename(name)
    if base in ("neutral-str.sh",):
        return "str"
    if base in ("wrapstred.sh",):
        return "stred"
    return base


def document_of(args):
    """The document that a scene opens: the first argument that names a
    document, without its folder; "-" when the scene opens none (the
    Document Manager shows the folder of the samples)."""
    for a in args:
        if a.endswith(".doc"):
            return os.path.basename(a)
    return "-"


def scene_programs():
    """The table of the scenes: picture name to the program that starts and
    the document that it opens."""
    shots = load_shots()
    found = {}
    for scene in shots.SCENES + shots.extra_scenes():
        found[scene[0]] = (program_of(scene[1]), document_of(scene[2]))
    return found


def picture_names():
    """The names of the picture files."""
    names = []
    for path in sorted(glob.glob(os.path.join(PICTURES, "*.png"))):
        names.append(os.path.basename(path)[:-4])
    return names


def last_taken(name):
    """The date of the last commit that changed the picture file."""
    path = os.path.join(PICTURES, name + ".png")
    try:
        out = subprocess.run(
            ["git", "-C", HERE, "log", "-1", "--format=%cs", "--", path],
            capture_output=True, text=True, timeout=30).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        out = ""
    return out if out != "" else "unknown"


def how(name, made):
    """The words that say how to make the picture again."""
    if made == "script":
        script, doc = SCRIPTS[name]
        return ("manual/scenes/%s PRINTER %s OUT.png" % (script, doc))
    return "build.sh pics %s" % name


def write_table():
    """Write manual/pictures.txt from the files and the scenes."""
    scenes = scene_programs()
    lines = []
    for name in picture_names():
        if name in SCRIPTS:
            made, program = "script", "strprint"
            document = os.path.basename(SCRIPTS[name][1])
        elif name in scenes:
            made = "scene"
            program, document = scenes[name]
        else:
            made, program, document = "unknown", "unknown", "unknown"
        lines.append("%s | %s | %s | %s | %s | %s" % (
            name, made, program, document, last_taken(name),
            how(name, made) if made != "unknown" else "none"))
    with open(TABLE, "w", encoding="ascii") as f:
        f.write(HEADER)
        f.write("\n".join(lines) + "\n")
    print("pictures.txt: %d lines written" % len(lines))
    return 0


def check_table():
    """Check manual/pictures.txt against the files and the scenes."""
    faults = []
    try:
        with open(TABLE, "r", encoding="ascii") as f:
            text = f.read().splitlines()
    except OSError as e:
        print("checkpictable: cannot read %s: %s" % (TABLE, e))
        return 1
    scenes = scene_programs()
    listed = {}
    for number, line in enumerate(text, 1):
        if line.startswith("#") or line.strip() == "":
            continue
        fields = [x.strip() for x in line.split(" | ")]
        if len(fields) != 6:
            faults.append("line %d: %d fields, six wanted" %
                          (number, len(fields)))
            continue
        name, made, program, document, taken, again = fields
        if name in listed:
            faults.append("line %d: %s is listed twice" % (number, name))
        listed[name] = number
        if made == "scene":
            if name not in scenes:
                faults.append("line %d: no scene makes %s" % (number, name))
            elif scenes[name][0] != program:
                faults.append("line %d: the scene of %s starts %s, not %s" %
                              (number, name, scenes[name][0], program))
            elif scenes[name][1] != document:
                faults.append("line %d: the scene of %s opens %s, not %s" %
                              (number, name, scenes[name][1], document))
        elif made == "script":
            if name not in SCRIPTS:
                faults.append("line %d: no script makes %s" % (number, name))
            elif not os.path.exists(os.path.join(HERE, "scenes",
                                                 SCRIPTS[name][0])):
                faults.append("line %d: the script of %s is missing" %
                              (number, name))
        else:
            faults.append("line %d: %s is made by %s" % (number, name, made))
        if len(taken) != 10 or taken[4] != "-" or taken[7] != "-":
            if taken != "unknown":
                faults.append("line %d: the date %s is not YYYY-MM-DD" %
                              (number, taken))
    files = set(picture_names())
    for name in sorted(files - set(listed)):
        faults.append("the picture %s has no line" % name)
    for name in sorted(set(listed) - files):
        faults.append("line %d: the picture %s has no file" %
                      (listed[name], name))
    for name in sorted(scenes):
        if name not in files:
            faults.append("the scene %s has no picture file" % name)
    for fault in faults:
        print("checkpictable: " + fault)
    print("checkpictable: %d pictures, %d lines, %d faults" %
          (len(files), len(listed), len(faults)))
    return 1 if faults else 0


def list_scripts():
    """Print one line for every picture that a script makes: the name of the
    picture, the script and the document, separated by one blank.  The
    target pics-check of build.sh makes these pictures again from this
    list, so that they are compared like the pictures of the scenes."""
    for name in sorted(SCRIPTS):
        script, document = SCRIPTS[name]
        print("%s %s %s" % (name, script, document))
    return 0


def main(argv):
    """Entry point."""
    if len(argv) > 1 and argv[1] == "--write":
        return write_table()
    if len(argv) > 1 and argv[1] == "--scripts":
        return list_scripts()
    return check_table()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
