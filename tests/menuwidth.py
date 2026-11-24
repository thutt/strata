#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""The F12 menu fits the screen, and every command of a function key has a
route in it.

The program writes its menu tree (stred --dump-menus): for every level a
'menu' line with the names of the entries, and for every entry an 'item' line
with its hint.  The bar of a level is drawn on one or more rows; the first row
ends with the label of the level at its right end (MENU, MENU-EDIT,
MENU-TOOLS-LIBRARY), so the names must fit in the columns that the label
leaves; the line of the hint is as wide as the screen.  The checks:

  - the names of a level, one space apart, fit the first row (no name of a
    level is pushed to a second row);
  - the hint of every entry fits 80 columns;
  - the names of one level are all different;
  - every command of a function key (stred --fkey-table) has an entry in the
    menu tree whose hint names its key, so that a key that a terminal or a
    desktop takes still has a route.

Every check is run again on a copy of the dump or of the table that is made
wrong in one way, and must fail there.

    python3 -I -B tests/menuwidth.py PATH-OF-STRED
"""
import subprocess
import sys

COLS = 80

# The command of each function key that the table of the program lists, and
# the entry of the menu that runs it: the path of the level and the name.
ROUTES = {
    "Help": ("", "Help"),
    "Split": ("Edit:Parts", "Split"),
    "Copy": ("Edit:Block", "Copy"),
    "Cut": ("Edit:Block", "Cut"),
    "Paste": ("Edit:Block", "Paste"),
    "Format": ("Edit:Layout", "Format"),
    "Move": ("Edit:Block", "Move"),
    "Table": ("Edit:Layout", "Table"),
    "Search": ("Tools:Find", "Search"),
    "Menu": None,
    "Learn": ("Tools:Macros", "Learn"),
    "Math": ("Tools", "Math"),
    "Lib Copy": ("Tools:Library", "Copy"),
    "Lib Cut": ("Tools:Library", "Cut"),
    "Lib Paste": ("Tools:Library", "Paste"),
    "Case": ("Edit:Layout", "Case"),
    "Prev Error": ("Print:Errors", "Previous"),
    "Next Error": ("Print:Errors", "Next"),
    "Search Again": ("Tools:Find", "Again"),
    "Undo": ("Tools", "Undo"),
    "Redo": ("Tools", "Redo"),
    "Marker": ("Edit:Insert", "Marker"),
    "Append": ("Edit:Parts", "Append"),
    "Join": ("Edit:Parts", "Join"),
    "Delete": ("Edit:Block", "Delete"),
    "Overtype": ("Edit:Insert", "Overtype"),
    "Set Attribute": ("Edit:Layout", "Attribute"),
    "Go To": ("Tools:Find", "Go To"),
    "Border": ("Edit:Layout", "Border"),
    "Replace": ("Tools:Find", "Replace"),
}

# The levels whose names do not fit the first row: the bar would draw the
# rest on a second row.  There is none since the Print menu was split.
WRAPS = ()

# Commands whose hint names another key of the same command (Undo is also
# Ctrl-/, Redo is also Alt-/, a Marker key is also Alt-F12).
ALSO = {"Undo": ["Ctrl-/"], "Redo": ["Alt-/"], "Marker": ["Alt-F12"]}

fails = 0


def check(name, cond, extra=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name +
          ("" if cond else "\n     " + str(extra)))
    fails += 0 if cond else 1


def run(prog, arg):
    r = subprocess.run([prog, arg], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    return r.stdout.decode("utf-8", "replace")


def parse(dump):
    """The levels (path -> names) and the items (path, name -> hint)."""
    levels = []
    items = {}

    for line in dump.split("\n"):
        f = line.split("\t")

        if f[0] == "menu" and len(f) >= 3:
            levels.append((f[1], f[2].split("|")))
        elif f[0] == "item" and len(f) >= 4:
            items[(f[1], f[2])] = f[3]

    return levels, items


def label(path):
    """The label at the right end of the first row of a level."""
    if path == "":
        return "MENU"

    return "MENU-" + "-".join(p.upper() for p in path.split(":"))


def widths(levels, items):
    """The faults of the widths: a list of texts."""
    bad = []

    for path, names in levels:
        room = COLS - len(label(path)) - 1
        used = sum(len(n) for n in names) + len(names) - 1

        if used > room and path not in WRAPS:
            bad.append("level %r: names %d columns, room %d" %
                       (path, used, room))

        if len(set(names)) != len(names):
            bad.append("level %r: a name is used twice" % path)

    for (path, name), hint in items.items():
        if len(hint) > COLS:
            bad.append("hint of %s/%s: %d columns" % (path, name, len(hint)))

    return bad


def key_marks(table):
    """The commands of the table of function keys: name -> key."""
    out = []

    for line in table.split("\n"):
        f = line.split("\t")

        if len(f) == 2:
            out.append((f[0], f[1]))

    return out


def routes(levels, items, table):
    """The faults of the routes: commands without an entry in the menu."""
    bad = []
    level_names = dict(levels)
    keys = {}

    for name, key in key_marks(table):
        keys.setdefault(name, []).append(key)

    for name, ks in keys.items():
        if name not in ROUTES:
            bad.append("the command %s (%s) has no route in the test" %
                       (name, ", ".join(ks)))
            continue

        route = ROUTES[name]

        if route is None:
            continue

        path, entry = route

        if entry not in level_names.get(path, []):
            bad.append("%s: no entry %s in the level %r" % (name, entry, path))
            continue

        hint = items.get((path, entry), "")
        marks = ["[" + k + "]" for k in ks + ALSO.get(name, [])]

        if not any(m in hint or m.strip("[]") in hint for m in marks):
            bad.append("%s: the hint of %s does not name the key %s: %r" %
                       (name, entry, "/".join(ks), hint))

    return bad


def main():
    if len(sys.argv) != 2:
        print("usage: menuwidth.py PATH-OF-STRED")
        return 2

    stred = sys.argv[1]
    dump = run(stred, "--dump-menus")
    table = run(stred, "--fkey-table")
    levels, items = parse(dump)

    check("the menu tree has the levels of the bar", len(levels) >= 15,
          len(levels))
    check("the widths of the levels and of the hints are right",
          widths(levels, items) == [], widths(levels, items))
    check("every command of a function key has a route in the menu",
          routes(levels, items, table) == [], routes(levels, items, table))
    check("the table of function keys is not empty",
          len(key_marks(table)) >= 30, len(key_marks(table)))

    # The Edit row of every kind of document, and the groups, as numbers.
    edit = [names for path, names in levels if path == "Edit"]
    check("the Edit row has five kinds of document", len(edit) == 5, edit)
    tools = dict(levels).get("Tools", [])
    check("Tools has Find and Math", "Find" in tools and "Math" in tools,
          tools)
    check("the bar has Help between Tools and Quit",
          dict(levels).get("", [])[-3:] == ["Tools", "Help", "Quit"],
          dict(levels).get("", []))

    # The Print row was the one row that wrapped; it is split now.
    pr = dict(levels).get("Print", [])
    check("the Print row is Draft Final Preview Errors and fits one row",
          pr == ["Draft", "Final", "Preview", "Errors"], pr)
    check("the Errors submenu of Print has the four error commands",
          dict(levels).get("Print:Errors", []) ==
          ["Next", "Previous", "Remove", "Remove All"],
          dict(levels).get("Print:Errors"))

    # The same checks on levels and tables that are made wrong.
    wide = [(p, n if p != "Edit" else n[:-1] + ["Twentycharactername1"])
            for p, n in levels]
    check("a group name of 20 characters in Edit is a fault",
          widths(wide, items) != [], "not detected")

    old_print = ["Draft", "Final", "Preview", "Next Error", "Previous Error",
                 "Remove Error", "Remove Print Errors"]
    wide_print = [(p, n if p != "Print" else old_print) for p, n in levels]
    check("a Print row of 78 columns (the old one) is a fault",
          widths(wide_print, items) != [], "not detected")

    twice = [(p, n if p != "Tools:Find" else n + ["Search"])
             for p, n in levels]
    check("a name used twice in a level is a fault",
          widths(twice, items) != [], "not detected")

    long_hint = dict(items)
    long_hint[("", "Help")] = "x" * 81
    check("a hint of 81 columns is a fault",
          widths(levels, long_hint) != [], "not detected")

    fewer = [(p, [x for x in n if not (p == "Edit:Block" and x == "Copy")])
             for p, n in levels]
    check("a command without its entry is a fault",
          routes(fewer, items, table) != [], "not detected")

    nokey = dict(items)
    nokey[("Edit:Block", "Copy")] = "Copy text"
    check("an entry that does not name its key is a fault",
          routes(levels, nokey, table) != [], "not detected")

    extra = table + "Brand New\tCtrl-F13\n"
    check("a new command of a function key without a route is a fault",
          routes(levels, items, extra) != [], "not detected")

    print("menuwidth: %s" % ("all passed" if not fails else
                              "%d FAILURES" % fails))

    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
