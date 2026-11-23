#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

"""Check the sources of the help (help/*.txt) against the programs.

The checks are these.

  - Every line is ASCII and has at most 80 columns, and the files are text.
  - A title or a name is used by one topic only (the case is ignored).
  - Every link [[Title]] goes to a topic.
  - Every key {Name} is a key that exists, and the menu key is F12: F10 is
    named only to say that it has no command.
  - No topic names a program (the Editor, the Document Manager, the Printer
    and the Viewer have these names) or the earlier product.
  - Every place that the programs give to F1 has a topic: the title of every
    menu, panel, list or prompt that opens with popup_menu, popup_input,
    popup_form, popup_form_nav, popup_grid or popup_browse, the entries of
    the menu bar (as "Menu:Edit:Title"), the titles and the help names that
    the Document Manager gives, and the places that tests/data/
    help_contexts.txt lists (the titles that are made at run time).
  - Every message that the message line can show and that the file
    tests/data/help_messages.txt lists has a topic that answers to it.
  - Every marker of the table of src/marker.c that can be put in a document
    has a topic of its own for F1 on the marker ("Marker:Name"), and every
    text editor of a marker has a topic of its own ("Text Editor:Title"):
    no fallback to a general topic is accepted for them.

    python3 -I -B tests/helpcheck.py

The exit status is 1 when something is wrong.
"""

import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

failures = 0


def fail(text):
    global failures
    failures += 1
    print("FAIL " + text)


def ok(text):
    print("ok   " + text)


def parse(path):
    """The topics of a help file: a list of dictionaries."""
    topics = []
    cur = None
    with open(path, "rb") as f:
        raw = f.read()
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as e:
        fail("%s: not ASCII (%s)" % (os.path.basename(path), e))
        text = raw.decode("ascii", "replace")
    for n, line in enumerate(text.split("\n"), 1):
        where = "%s:%d" % (os.path.basename(path), n)
        if len(line) > 80:
            fail("%s: the line has %d columns" % (where, len(line)))
        if "\t" in line:
            fail("%s: a tab" % where)
        if line.startswith("= "):
            cur = {"title": line[2:].strip(), "names": [], "msgs": [],
                   "text": [], "where": where}
            topics.append(cur)
        elif cur is None:
            continue
        elif line.startswith("~ "):
            cur["names"].append(line[2:].strip())
        elif line.startswith("? "):
            cur["msgs"].append(line[2:].strip())
        elif line.startswith(";"):
            continue
        else:
            cur["text"].append((n, line))
    return topics


def lookup(index, name):
    """The topic that answers to a name: the title, then a name."""
    return index.get(name.lower())


def context(index, name):
    """The topic for a place, as help_find_context finds it."""
    while True:
        t = lookup(index, name)
        if t is not None:
            return t
        cut = name.find(" (")
        if cut >= 0:
            name = name[:cut]
            continue
        cut = name.rfind(":")
        if cut < 0:
            return None
        name = name[:cut]


KEY_BASES = {
    "enter", "esc", "tab", "backspace", "del", "ins", "home", "end", "pgup",
    "pgdn", "up", "down", "left", "right", "space", "keypad +", "keypad -",
    "hyphen",
}
MODIFIERS = {"ctrl", "alt", "shift"}


def key_ok(name):
    """A key is a modifier, or a base key with modifiers before it."""
    low = name.lower()
    if low in MODIFIERS:
        return True
    parts = low.split("-")
    # A base key may be the minus sign or end with a minus sign.
    while len(parts) > 1 and parts[0] in MODIFIERS:
        parts = parts[1:]
    base = "-".join(parts) if parts != [""] else "-"
    if low.endswith("--"):
        return True
    return (base in KEY_BASES or len(base) == 1 or
            re.match(r"^f([1-9]|1[0-2])$", base) is not None or
            base == "shift-space" or base == "^")


BAD_WORDS = [
    r"\bstred\b", r"\bstrprint\b", r"\bdviview\b", r"\bstr\b",
    r"\blotus\b", r"\bmanuscript\b", r"\boriginal\b", r"\bearlier product\b",
    r"\bmsed\b", r"\bmshelp\b", r"\bnot implemented\b.*\boriginal\b",
]


def check_text(topics):
    for t in topics:
        for n, line in t["text"]:
            where = "%s (line %d)" % (t["title"], n)
            for m in re.finditer(r"\{([^{}]*)\}", line.replace("{{", "")):
                if not key_ok(m.group(1)):
                    fail("%s: {%s} is not a key" % (where, m.group(1)))
            if re.search(r"F10", line) and "no command" not in line:
                fail("%s: F10 is not the menu key" % where)
            for pat in BAD_WORDS:
                if re.search(pat, line, re.I):
                    fail("%s: the text names a program or the earlier "
                         "product: %s" % (where, line.strip()))


def check_links(topics, index):
    bad = 0
    for t in topics:
        for n, line in t["text"]:
            for m in re.finditer(r"\[\[([^\]]*)\]\]", line):
                name = m.group(1).split("|")[0]
                if lookup(index, name) is None:
                    # A link may be broken over two lines of a paragraph.
                    bad += 1
                    fail("%s (line %d): the link [[%s]] has no topic" %
                         (t["title"], n, name))
        text = " ".join(l for _, l in t["text"])
        for m in re.finditer(r"\[\[([^\]]*)\]\]", text):
            name = re.sub(r"\s+", " ", m.group(1).split("|")[0])
            if lookup(index, name) is None:
                bad += 1
                fail("%s: the link [[%s]] has no topic" % (t["title"], name))
    if bad == 0:
        ok("every link goes to a topic")


def check_unique(topics):
    seen = {}
    index = {}
    bad = 0
    for t in topics:
        for name in [t["title"]] + t["names"]:
            low = name.lower()
            if low in seen and seen[low] is not t:
                bad += 1
                fail("the name '%s' is used by '%s' and by '%s'" %
                     (name, seen[low]["title"], t["title"]))
            seen[low] = t
            index[low] = t
    if bad == 0:
        ok("every title and name is used by one topic only")
    return index


def c_sources():
    out = {}
    for p in sorted(glob.glob(os.path.join(ROOT, "src", "*.c"))):
        with open(p, encoding="utf-8", errors="replace") as f:
            out[os.path.basename(p)] = f.read()
    return out


POPUP_RE = re.compile(
    r"\b(?:popup_menu|popup_input|popup_form|popup_form_nav|popup_grid|"
    r"popup_browse|pager)\(\s*\"([^\"]+)\"")
def split_call(text, start):
    """The arguments of the call whose opening parenthesis is at start."""
    args = []
    depth = 0
    cur = ""
    i = start
    in_str = False
    while i < len(text):
        c = text[i]
        if in_str:
            cur += c
            if c == "\\":
                cur += text[i + 1]
                i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
            cur += c
        elif c == "(":
            depth += 1
            if depth > 1:
                cur += c
        elif c == ")":
            depth -= 1
            if depth == 0:
                args.append(cur.strip())
                return args
            cur += c
        elif c == "," and depth == 1:
            args.append(cur.strip())
            cur = ""
        else:
            cur += c
        i += 1
    return args


def literal(arg, defines):
    """The text of a string literal or of a macro that is one, or None."""
    arg = defines.get(arg, arg)
    m = re.fullmatch(r'"([^"]*)"', arg)
    return m.group(1) if m is not None else None


def str_titles(src):
    """The help names that the Document Manager gives to its panels."""
    defines = dict(re.findall(r'#define\s+(\w+)\s+("[^"]*")', src))
    names = set()
    for fn, idx in (("form", 3), ("run_menu", 4)):
        for m in re.finditer(r"\b" + fn + r"\(", src):
            args = split_call(src, m.end() - 1)
            if len(args) > idx:
                lit = literal(args[idx], defines)
                if lit is not None:
                    names.add(lit)
    return names


def menu_paths(src):
    """The paths of the entries of the menu bar of the Editor."""
    arrays = {}
    for m in re.finditer(r"static const MenuItem (\w+)\[\] = \{(.*?)\};",
                         src, re.S):
        entries = []
        for e in re.finditer(r"\b(MI|MSUB)\(\s*\"([^\"]+)\"(?:[^;]*?,\s*"
                             r"(\w+)\))?", m.group(2)):
            entries.append((e.group(1), e.group(2), e.group(3)))
        arrays[m.group(1)] = entries
    paths = []

    def walk(name, prefix):
        for kind, nm, sub in arrays.get(name, []):
            path = prefix + ":" + nm
            paths.append(path)
            if kind == "MSUB" and sub in arrays:
                walk(sub, path)

    walk("top_items", "Menu")
    return paths


def check_contexts(index):
    srcs = c_sources()
    names = set()
    for text in srcs.values():
        for m in POPUP_RE.finditer(text):
            names.add(m.group(1))
    names |= str_titles(srcs["str.c"])
    # Popups whose F1 is named by the caller with w_set_help.
    names.discard("Line")
    for m in re.finditer(r"(?:\"[^\"]*\"\s*,\s*)?\"(Maintain Dictionaries|"
                         r"Compare Document Versions)\"\)", srcs["str.c"]):
        names.add(m.group(1))
    for p in menu_paths(srcs["ui.c"]):
        names.add(p)
    extra = os.path.join(HERE, "data", "help_contexts.txt")
    with open(extra, encoding="ascii") as f:
        for line in f:
            line = line.strip()
            if line != "" and not line.startswith("#"):
                names.add(line)
    # The titles of the Document Manager come from its own list of names.
    bad = 0
    shared = []
    for name in sorted(names):
        if context(index, name) is None:
            bad += 1
            fail("no topic for the place '%s'" % name)
        elif lookup(index, name) is None:
            shared.append(name)
    if "--shared" in sys.argv:
        for name in shared:
            print("note the place '%s' shows the topic of its parent" % name)
    if bad == 0:
        ok("every place has a topic (%d places)" % len(names))
    return len(names)


def check_exact(index):
    """Markers and text editors have a topic of their own, not a fallback."""
    with open(os.path.join(ROOT, "src", "marker.c"), encoding="ascii") as f:
        src = f.read()
    start = src.index("static const MInfo menu[]")
    table = src[start:src.index("};", start)]
    names = re.findall(r"\bMN?0?\(\s*\d+\s*,\s*\"([^\"]+)\"", table)
    wanted = ["Marker:" + n for n in names]
    wanted += ["Text Editor:" + n for n in
               ("Note", "Comment", "Equation", "Title Page")]
    wanted += ["Text Editor", "Menu:Print:Next Error",
               "Menu:Print:Remove Print Errors"]
    bad = 0
    for name in wanted:
        if lookup(index, name) is None:
            bad += 1
            fail("no topic of its own for '%s'" % name)
    if len(names) < 20:
        bad += 1
        fail("the marker table was not read (%d names)" % len(names))
    if bad == 0:
        ok("markers and text editors have topics of their own (%d)" %
           len(wanted))


def check_messages(topics):
    path = os.path.join(HERE, "data", "help_messages.txt")
    pats = []
    for t in topics:
        for p in t["msgs"]:
            pats.append((p, t))
    bad = 0
    with open(path, encoding="ascii") as f:
        for line in f:
            line = line.rstrip("\n")
            if line == "" or line.startswith("#"):
                continue
            best = None
            for p, t in pats:
                text = line
                pat = p
                if pat.startswith("N "):
                    m = re.match(r"^[0-9]+ ", line)
                    if m is None:
                        continue
                    pat = pat[2:]
                    text = line[m.end():]
                if text.startswith(pat) and (best is None or
                                             len(pat) > len(best[0])):
                    best = (pat, t)
            if best is None:
                bad += 1
                fail("no topic answers to the message '%s'" % line)
    if bad == 0:
        ok("every listed message has a topic")


def main():
    topics = []
    for p in sorted(glob.glob(os.path.join(ROOT, "help", "*.txt"))):
        topics.extend(parse(p))
    if len(topics) < 40:
        fail("the help has only %d topics" % len(topics))
    else:
        ok("the help has %d topics" % len(topics))
    index = check_unique(topics)
    check_links(topics, index)
    check_text(topics)
    n = check_contexts(index)
    check_exact(index)
    check_messages(topics)
    print("helpcheck: %d failure(s), %d topics, %d places" %
          (failures, len(topics), n))
    return 1 if failures != 0 else 0


if __name__ == "__main__":
    sys.exit(main())
