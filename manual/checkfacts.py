#!/usr/bin/env python3
# Copyright (c) 2026 Logic Magicians Software
#
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This program compares what the programs have with what the Reference
# Manual says.  The truth is taken from the sources of the programs and from
# the Editor itself, never from the manual:
#
#   F1  Every message that the Editor puts on its message line or in a
#       box (the first words of the text of ed_msg and popup_msg calls)
#       is in the Messages chapter of the manual.
#   F2  Every entry of every menu that the Editor prints with --dump-menus
#       is named in the manual.
#   F3  Every option that a program reads from its command line
#       (strcmp(argv[i], "--name") in the sources of the programs) is
#       in the manual.
#   F4  Every marker type has a section of the manual under its name.
#   F6  Every sample file or directory that the manual names (samples/NAME)
#       is in the samples of the source tree.
#   F5  Every label of a field of a panel (the fields of the markers and the
#       labels that end with a colon in the sources of the programs) is
#       named in the manual.
#
#     python3 -I -B manual/checkfacts.py [--list] [--self-test] [--reasons]
#
# STRED names the Editor program (the menus come from it); without it F2 is
# left out and the run says so.  A hit is a thing that the program has and
# the manual does not name.  A thing that the manual leaves out on purpose
# goes into manual/checkfacts.allow: one line, the check (F1 to F6), a bar
# and the text that is left out, and a reason after another bar.
# --list prints every hit; with no flag the run fails when there is a hit.
# --self-test shows that each check can fail: it takes a word out of the
# text of the manual and the check must find it.
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = os.path.dirname(HERE)
SRC = os.path.join(TREE, "src")
MAN = os.path.join(HERE, "src")


def read(path):
    """The text of a file."""
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def expand(path, files):
    """Add the source PATH and the parts that it includes to FILES."""
    if path in files or not os.path.exists(path):
        return
    files.append(path)
    for line in read(path).splitlines():
        if line.startswith(".include "):
            expand(os.path.join(MAN, line.split(None, 1)[1].strip()), files)


def master_files():
    """The sources of the Reference Manual: the chapters of the master and
    the parts that they include."""
    files = []
    for line in read(os.path.join(MAN, "master.txt")).splitlines():
        parts = line.split()
        if len(parts) >= 2 and not line.startswith("#") and parts[0] != "-":
            expand(os.path.join(MAN, parts[1] + ".src"), files)
    return files


COMMAND = re.compile(r"\{[a-zA-Z]+:([^{}]*)\}")


def flat_manual(files, only=None):
    """The text of the manual with the commands taken off, the table bars
    and the line breaks made one space, and the case folded."""
    out = []
    for path in files:
        if only is not None and only not in os.path.basename(path):
            continue
        text = read(path)
        while True:
            new = COMMAND.sub(r"\1", text)
            if new == text:
                break
            text = new
        out.append(text)
    text = "\n".join(out)
    text = re.sub(r"\n \| ", " ", text)
    text = text.replace(" | ", " ").replace("\n", " ")
    return " ".join(text.split()).lower()


def load_allow():
    """The things that are left out on purpose: (check, text) pairs."""
    allow = set()
    path = os.path.join(HERE, "checkfacts.allow")
    if os.path.exists(path):
        for line in read(path).splitlines():
            line = line.strip()
            if line == "" or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split("|")]
            if len(parts) >= 2:
                allow.add((parts[0], parts[1].lower()))
    return allow


STRING = re.compile(r'"((?:[^"\\]|\\.)*)"')
CALL = re.compile(r"\b(ed_msg|popup_msg)\(")
# The reasons that a panel gives for a refused value are written into a
# buffer called err or why: snprintf(err, cap, "...") or str_copy(err, cap,
# "...").
REASON = re.compile(r"\b(?:snprintf|str_copy)\((?:err|why)\s*,[^,;]+,\s*")


def literal_after(text, pos):
    """The joined string literals that begin at or after POS (adjacent
    literals are one string); it stops at the first thing that is not a
    literal or a comma after the arguments."""
    out = []
    depth = 0
    i = pos
    seen = False
    while i < len(text):
        c = text[i]
        if c == '"':
            m = STRING.match(text, i)
            if m is None:
                break
            out.append(m.group(1))
            seen = True
            i = m.end()
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            if depth == 0:
                break
            depth -= 1
        elif c == "," and seen and depth == 0:
            break
        elif c == ";":
            break
        elif c in " \t\n" or seen is False:
            pass
        elif not c.isspace():
            if seen:
                break
        i += 1
    return "".join(out)


def message_words(lit):
    """The fixed words at the head of a message: up to the first format
    conversion, and no shorter than twelve characters."""
    lit = lit.replace("\\n", " ").replace('\\"', '"')
    head = re.split(r"%|\\", lit)[0]
    head = " ".join(head.split())
    return head


# With --reasons the reasons that a panel gives for a refused value are
# compared too.  They are often written in other words in the tables of the
# panels, so the check is for a reader to run and not part of the build.
WITH_REASONS = False


def messages():
    """The messages of the programs: list of (file, text)."""
    found = []
    for name in sorted(os.listdir(SRC)):
        if not name.endswith(".c"):
            continue
        text = read(os.path.join(SRC, name))
        for m in CALL.finditer(text):
            pos = m.end()
            if m.group(1) == "popup_msg":
                # The first argument of a box is its title: skip it.
                first = STRING.search(text, pos)
                comma = text.find(",", first.end()) if first else -1
                if comma < 0:
                    continue
                pos = comma + 1
            lit = literal_after(text, pos)
            words = message_words(lit)
            if len(words) >= 12:
                found.append((name, words))
        for m in (REASON.finditer(text) if WITH_REASONS else ()):
            words = message_words(literal_after(text, m.end()))
            if len(words) >= 12:
                found.append((name, words))
    return found


def check_messages(flat, allow):
    """F1: the messages that the manual does not have."""
    hits = []
    for name, words in messages():
        key = " ".join(re.sub(r"\{[^}]*\}", "", words).split()).lower()
        for prog, word in (("strprint", "<printer>"), ("stred", "<editor>"),
                           ("dviview", "<viewer>"),
                           ("system folders", "system directories")):
            key = re.sub(r"(?<![\w])" + re.escape(prog) + r"(?![\w])", word,
                         key)
        key = key.rstrip(" :-.,")
        if key in flat:
            continue
        if ("F1", key) in allow:
            continue
        hits.append(("F1", "%s: %s" % (name, words)))
    return hits


def menus():
    """The entries of the menus of the Editor: list of (route, entry)."""
    stred = os.environ.get("STRED", "")
    if stred == "" or not os.path.exists(stred):
        return None
    out = subprocess.run([stred, "--dump-menus"], capture_output=True,
                         text=True, timeout=60).stdout
    found = []
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) != 3 or parts[0] != "menu":
            continue
        for entry in parts[2].split("|"):
            route = parts[1].replace(":", ", ") if parts[1] else "F12"
            found.append((route, entry))
    return found


def check_menus(flat, allow):
    """F2: the menu entries that the manual does not name.  The manual
    writes a route as the last menu and the entry, separated by a comma
    (Print, Preview), and the first level as F12 and the entry."""
    items = menus()
    if items is None:
        return [("F2", "STRED is not set: the menus are not checked")]
    hits = []
    seen = set()
    for route, entry in items:
        parent = route.split(", ")[-1]
        want = ("%s, %s" % (parent, entry)).lower()
        if want in seen:
            continue
        seen.add(want)
        if want in flat or ("F2", want) in allow:
            continue
        hits.append(("F2", want))
    return hits


OPTION = re.compile(r'strcmp\(\s*argv\[[^\]]+\]\s*,\s*"(--?[a-zA-Z][-a-zA-Z0-9]*)"')


def options():
    """The command line options that the programs read."""
    found = []
    for name in sorted(os.listdir(SRC)):
        if name.endswith(".c"):
            for m in OPTION.finditer(read(os.path.join(SRC, name))):
                found.append((name, m.group(1)))
    return found


def check_options(flat, allow):
    """F3: the options that the manual does not name."""
    hits = []
    done = set()
    for name, opt in options():
        if opt in done:
            continue
        done.add(opt)
        if re.search(r"(?<![-\w])" + re.escape(opt.lower()) + r"(?![-\w])",
                     flat):
            continue
        if ("F3", opt) in allow:
            continue
        hits.append(("F3", "%s: %s" % (name, opt)))
    return hits


def marker_names():
    """The names of the marker types of the Editor."""
    text = read(os.path.join(SRC, "marker.c"))
    names = []
    for m in re.finditer(r'\{\s*(\d+)\s*,\s*"([A-Z][A-Za-z -]+)"', text):
        names.append(m.group(2))
    return names


def check_markers(flat, allow):
    """F4: the marker types that the manual has no section for."""
    hits = []
    for name in marker_names():
        if name.lower() + " marker" in flat or name.lower() in flat:
            continue
        if ("F4", name.lower()) in allow:
            continue
        hits.append(("F4", name))
    return hits


LABEL = re.compile(r'"([A-Z][A-Za-z0-9]*(?: [A-Za-z0-9()/?\'-]+){0,5}\??):"')
FIELD = re.compile(r'\{"[0-9A-Za-z]", "([A-Z][^"]+)", MF_')


def field_labels():
    """The labels of the fields of the panels: the fields of the markers
    (the table of marker.c) and the labels that end with a colon."""
    found = {}
    for name in sorted(os.listdir(SRC)):
        if not name.endswith(".c"):
            continue
        text = read(os.path.join(SRC, name))
        for rx in (FIELD, LABEL):
            for m in rx.finditer(text):
                found.setdefault(m.group(1), name)
    return found


def check_fields(flat, allow):
    """F5: the labels of fields that the manual does not name."""
    hits = []
    for label, name in sorted(field_labels().items()):
        if label.lower() in flat or ("F5", label.lower()) in allow:
            continue
        hits.append(("F5", "%s: %s" % (name, label)))
    return hits


SAMPLE = re.compile(r"(?<![\w/.])samples/([A-Za-z0-9._/-]+[A-Za-z0-9_])")


def check_samples(files, allow):
    """F6: a sample file or directory that the manual names and that is not
    in the samples of the source tree."""
    hits = []
    seen = set()
    for path in files:
        for m in SAMPLE.finditer(read(path)):
            name = m.group(1)
            if name in seen:
                continue
            seen.add(name)
            if os.path.exists(os.path.join(TREE, "samples", name)) or \
                    os.path.exists(os.path.join(HERE, "samples", name)):
                continue
            if ("F6", name.lower()) in allow:
                continue
            hits.append(("F6", "%s: samples/%s" % (
                os.path.basename(path), name)))
    return hits


def run(files, allow, flat=None):
    """All the hits."""
    flat = flat if flat is not None else flat_manual(files)
    hits = []
    hits += check_messages(flat, allow)
    hits += check_menus(flat, allow)
    hits += check_options(flat, allow)
    hits += check_markers(flat, allow)
    hits += check_fields(flat, allow)
    hits += check_samples(files, allow)
    return hits


def self_test(files, allow):
    """Each check must find a thing that is taken out of the manual."""
    flat = flat_manual(files)
    base = {h for h in run(files, allow, flat)}
    ok = True
    items = menus()
    first = "%s, %s" % (items[0][0].split(", ")[-1], items[0][1]) \
        if items else ""
    probes = {
        "F1": messages()[0][1] if messages() else "",
        "F2": first.lower(),
        "F3": options()[0][1] if options() else "",
        "F4": marker_names()[0].lower() if marker_names() else "",
        "F5": sorted(field_labels())[0].lower() if field_labels() else "",
    }
    for rule, word in probes.items():
        if word == "":
            print("FAIL %s: the program has nothing to probe" % rule)
            ok = False
            continue
        key = " ".join(word.split()).lower().rstrip(" :-.,")
        cut = flat.replace(key, "#" * len(key))
        hits = {h for h in run(files, allow, cut)} - base
        found = any(h[0] == rule for h in hits)
        print("%s %s: a missing word is %s" % (
            "ok  " if found else "FAIL", rule,
            "found" if found else "not found"))
        ok = ok and found
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        fake = os.path.join(tmp, "fake.src")
        with open(fake, "w") as f:
            f.write("See samples/no-such-sample.doc for it.\n")
        found = check_samples([fake], set())
        print("%s F6: a sample that is not there is %s" % (
            "ok  " if found else "FAIL", "found" if found else "not found"))
        ok = ok and bool(found)
    return ok


def main(argv):
    global WITH_REASONS
    WITH_REASONS = "--reasons" in argv
    files = master_files()
    allow = load_allow()
    if "--self-test" in argv:
        return 0 if self_test(files, allow) else 1
    hits = run(files, allow)
    if "--list" in argv or hits:
        for rule, text in hits:
            print("%s %s" % (rule, text))
    print("checkfacts: %d hits" % len(hits))
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
