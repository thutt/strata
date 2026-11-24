#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The texts name the F12 menu route of every command of a function key.

The commands that had a function key and no menu entry (24 of them, and
Help on the bar) have a route in the F12 menu.  This test reads the key
tables of the texts and fails when the route of one of them is missing:

  - manual/src/edit.src and view3.src: a line that has the key of the command
    and the route, written as {k:F12}, Edit, Block, Copy; manual/src/cheat.src
    (the card): a line that has the keys of the commands of a group and the
    group, written as {k:F12}, Edit, Block;
  - docs/KEY-MAP.txt: a line that has the key and the route, written as F12,
    Edit, Block, Copy;
  - help/*.txt: the help place of the entry, such as Menu:Edit:Block:Copy.

Every check is run again on a text from which the line of one command is
taken out, and must fail there.

    python3 -I -B tests/menutext.py
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# The command, its keys, the route (the names of the menu entries).
COMMANDS = [
    ("Help", ["F1"], "Help"),
    ("Split", ["F2"], "Edit, Parts, Split"),
    ("Copy", ["F3"], "Edit, Block, Copy"),
    ("Cut", ["F4"], "Edit, Block, Cut"),
    ("Paste", ["F5"], "Edit, Block, Paste"),
    ("Format", ["F6"], "Edit, Layout, Format"),
    ("Move", ["F7"], "Edit, Block, Move"),
    ("Table", ["F8"], "Edit, Layout, Table"),
    ("Search", ["F9"], "Tools, Find, Search"),
    ("Math", ["Ctrl-F2"], "Tools, Math"),
    ("Lib Copy", ["Ctrl-F3"], "Tools, Library, Copy"),
    ("Lib Cut", ["Ctrl-F4"], "Tools, Library, Cut"),
    ("Lib Paste", ["Ctrl-F5"], "Tools, Library, Paste"),
    ("Case", ["Ctrl-F6"], "Edit, Layout, Case"),
    ("Search Again", ["Ctrl-F9"], "Tools, Find, Again"),
    ("Marker", ["Ctrl-F12"], "Edit, Insert, Marker"),
    ("Append", ["Alt-F2"], "Edit, Parts, Append"),
    ("Join", ["Alt-F3"], "Edit, Parts, Join"),
    ("Delete", ["Alt-F4"], "Edit, Block, Delete"),
    ("Overtype", ["Alt-F5"], "Edit, Insert, Overtype"),
    ("Set Attribute", ["Alt-F6"], "Edit, Layout, Attribute"),
    ("Go To", ["Alt-F7"], "Tools, Find, Go To"),
    ("Border", ["Alt-F8"], "Edit, Layout, Border"),
    ("Replace", ["Alt-F9"], "Tools, Find, Replace"),
    ("Prev Error", ["Ctrl-F7"], "Print, Errors, Previous"),
    ("Next Error", ["Ctrl-F8"], "Print, Errors, Next"),
]

fails = 0


def check(name, cond, extra=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name +
          ("" if cond else "\n     " + str(extra)))
    fails += 0 if cond else 1


def read(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as f:
        return f.read()


def line_has(text, keys, route, keycap):
    """True when one line has a key of the command and the route."""
    f12 = "{k:F12}" if keycap else "F12"

    for line in text.split("\n"):
        if (f12 + ", " + route) not in line:
            continue

        for k in keys:
            mark = ("{k:%s}" % k) if keycap else k

            if re.search(r"(^|[^A-Za-z0-9-])" + re.escape(mark) +
                         r"([^A-Za-z0-9-]|$)", line):
                return True

    return False


def group_of(route):
    """The menu of a route: the card names the group, not each entry."""
    return route.rsplit(", ", 1)[0] if route.count(",") >= 2 else route


def missing_in_table(text, keycap, grouped=False):
    return [c for c, keys, route in COMMANDS
            if not line_has(text, keys,
                            group_of(route) if grouped else route, keycap)]


def missing_in_help(text):
    places = set(l[2:].strip() for l in text.split("\n")
                 if l.startswith("~ "))

    return [c for c, keys, route in COMMANDS
            if "Menu:" + route.replace(", ", ":") not in places]


def without(text, command, keycap, grouped=False):
    """The text without the lines that route the command."""
    for c, keys, route in COMMANDS:
        if c == command:
            f12 = "{k:F12}" if keycap else "F12"
            r = group_of(route) if grouped else route

            return "\n".join(l for l in text.split("\n")
                             if (f12 + ", " + r) not in l)

    raise SystemExit("no command " + command)


def main():
    tables = [("manual/src/edit.src", True), ("manual/src/view3.src", True),
              ("manual/src/cheat.src", True), ("docs/KEY-MAP.txt", False)]

    for path, keycap in tables:
        text = read(*path.split("/"))
        grouped = path.endswith("cheat.src")

        if grouped:
            # A row of the card goes on in the lines that begin with a space.
            text = text.replace("\n ", " ")

        miss = missing_in_table(text, keycap, grouped)
        check("%s has the route of the %d commands" % (path, len(COMMANDS)),
              miss == [], miss)
        check("%s: a text without the route of Border is a fault" % path,
              "Border" in missing_in_table(
                  without(text, "Border", keycap, grouped), keycap, grouped),
              "not detected")
        check("%s: a text without the route of Help is a fault" % path,
              "Help" in missing_in_table(
                  without(text, "Help", keycap, grouped), keycap, grouped),
              "not detected")

    help_text = ""

    for name in sorted(os.listdir(os.path.join(ROOT, "help"))):
        if name.endswith(".txt"):
            help_text += read("help", name) + "\n"

    miss = missing_in_help(help_text)
    check("help/*.txt has a place for every route", miss == [], miss)
    cut = help_text.replace("~ Menu:Edit:Layout:Border\n", "")
    check("a help text without the place of Border is a fault",
          "Border" in missing_in_help(cut), "not detected")

    print("menutext: %s" % ("all passed" if not fails else
                            "%d FAILURES" % fails))

    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
