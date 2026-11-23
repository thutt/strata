#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program checks the key leaflet against the list of keys.  Every key
# of a row of KEYS.txt must be written on the leaflet as a keycap, unless
# the key is a word (a letter, any key) or the row says that the leaflet
# gives it by a rule.
#
#     python3 -I manual/checkcheat.py KEYS.txt manual/src/cheat.src
#
# It prints the keys that are missing, and the keycaps of the leaflet that are
# not keys of the key list (a key that does nothing must not be on the
# leaflet), and exits with 1 if there are any.

import re
import sys

# Rows whose keys are words, not keycaps: they are written in words on the
# leaflet, and the check only looks for the first word.
WORDS = ("a letter", "a character", "any key", "any other key", "letter",
         "Alt-letter", "cursor keys", "editing keys", "Alt-key (terminal)",
         "A", "D", "I", "E", "G", "L", "Q")

# Keys that do nothing special, so that the leaflet need not show them.
EXEMPT = ("Keypad /", "Ctrl-[", "Ctrl-]", "Ctrl-\\", "Ctrl-_")

# Keys that the leaflet gives by a rule.  The value is the keycap that
# stands for the key; None means that the rule is written in words.
RULES = {
    "Ctrl-Down": "Ctrl-Up", "Alt-Right": "Alt-Left", "Alt-Up": "Alt-Left",
    "Alt-Down": "Alt-Left", "Alt-PgDn": "Alt-PgUp", "Alt-Home": "Alt-PgUp",
    "Alt-End": "Alt-PgUp", "Ctrl-PgUp": "Ctrl-PgUp", "Ctrl-PgDn": "Ctrl-PgDn",
    "Ctrl-Shift-Right": "Ctrl-Shift-Left", "Ctrl-Shift-End":
    "Ctrl-Shift-Home", "Alt-Shift-Home": "Ctrl-Shift-Home",
    "Alt-Shift-End": "Ctrl-Shift-Home",
    "Ctrl-Alt-F1 ... Ctrl-Alt-F12": "Ctrl-Alt-F2",
    "Shift-Alt-F1 ... Shift-Alt-F12": "Shift-Alt-F2",
    "Ctrl-Shift-F1 ... Ctrl-Shift-F12": "Ctrl-F1",
    "Keypad 0 ... Keypad 9": "Keypad 5", "Keypad .": "Keypad .",
    "Keypad Del": "Keypad Ins", "Keypad End": "Keypad Home",
    "Keypad PgUp": "Keypad Home", "Keypad PgDn": "Keypad Home",
    "Keypad Up": "Keypad Home", "Keypad Down": "Keypad Home",
    "Keypad Left": "Keypad Home", "Keypad Right": "Keypad Home",
}


def read_rows(path):
    """The keys of every row of the table, from the fixed columns."""
    rows = []
    cur = None
    body = open(path).read().split("THE MODIFIER RULES")[0]
    start = re.compile(r"^(.{26}) (ed|hl|eq|mn|gr|pn|ls|hp|pg|dm|vw|sp|th|"
                       r"all) +(both|term|x11) ")
    for line in body.split("\n"):
        m = start.match(line)
        if m is not None:
            cur = [m.group(1).rstrip(), m.group(2)]
            rows.append(cur)
        elif cur is not None and line.startswith(" " * 26) is False and \
                line[26:27] == " " and line[27:31].strip() == "" and \
                line[:26].strip() != "":
            cur[0] += " " + line[:26].strip()
        elif line.strip() == "" or line.startswith("KEY "):
            cur = None
    return rows


def leaflet_keys(path):
    """Every text that the leaflet shows as a keycap, and its plain text."""
    text = open(path).read()
    caps = set(m.group(1) for m in re.finditer(r"\{k:([^}]*)\}", text))
    return caps, text


def expand(name):
    """The keycap names that a row key asks for."""
    if " ... " in name:
        a, _, b = name.partition(" ... ")
        return [a, b]
    return [name]


def known_keys(rows):
    """Every key name that the key list gives, with the ends of its ranges
    and the keys that the leaflet gives by a rule."""
    known = set()
    for keys, _ in rows:
        for name in [k.strip() for k in keys.split(" ; ")]:
            for want in expand(name):
                known.add(want.replace("Ctrl-\\\\", "Ctrl-\\"))
    for name, cap in RULES.items():
        known.add(cap or name)
    return known


def unknown_caps(rows, caps):
    """The keycaps of the leaflet that are not keys of the key list: a key
    that the leaflet shows must be a key that does something."""
    known = known_keys(rows)
    out = []
    for cap in sorted(caps):
        if cap in known or re.match(r"^[A-Za-z0-9]$", cap):
            continue
        # A range such as Alt-1..Alt-9 or Shift-F1..Shift-F12 is one text.
        if all(part in known or re.match(r"^(Shift|Ctrl|Alt)-F\d+$", part)
               for part in cap.split("..")):
            continue
        # The function keys with modifiers are given by the strip of F keys.
        if re.match(r"^(Shift|Ctrl|Alt|Ctrl-Alt|Shift-Alt|Ctrl-Shift)-F\d+$",
                    cap):
            continue
        if cap in ("+", "-", "=", "?", "Keypad .", "Keypad 0"):
            continue
        out.append(cap)
    return out


def main(argv):
    """Entry point."""
    rows = read_rows(argv[1])
    caps, text = leaflet_keys(argv[2])
    extra = unknown_caps(rows, caps)
    for cap in extra:
        print("on the leaflet but not a key of the key list: %s" % cap)
    missing = []
    count = 0
    for keys, at in rows:
        for name in [k.strip() for k in keys.split(" ; ")]:
            count += 1
            if name in EXEMPT or name == "Ctrl-\\ xx":
                if name == "Ctrl-\\ xx" and "Ctrl-\\" not in caps:
                    missing.append((name, at))
                continue
            m = re.match(r"^(Shift|Ctrl|Alt)-F(\d+)$", name)
            if m is not None:
                # The strip of the F keys shows the four columns.
                # A key that the leaflet writes with its modifier counts too.
                if "F" + m.group(2) not in caps and name not in caps and \
                        not (m.group(2) in ("11", "12") and "F11" in caps):
                    missing.append((name, at))
                continue
            if any(name == w or name.startswith(w + " ") for w in WORDS):
                continue
            if name in RULES:
                ex = RULES[name]
                if ex is not None and ex not in caps:
                    missing.append((name, at))
                continue
            if re.match(r"^F12 T S", name):
                # A menu path is written as the keycaps of its keys.
                if "{k:F12} {k:T} {k:S}" not in text:
                    missing.append((name, at))
                continue
            for want in expand(name):
                want = want.replace("Ctrl-\\\\", "Ctrl-\\")
                if want not in caps:
                    missing.append((want, at))
    for name, at in missing:
        print("missing on the leaflet: %s (%s)" % (name, at))
    print("%d keys checked, %d missing, %d not in the key list"
          % (count, len(missing), len(extra)))
    return 1 if missing or extra else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
