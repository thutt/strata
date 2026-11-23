#!/usr/bin/env python3
# Copyright (c) 2026 Logic Magicians Software
#
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# Getting Started is one document: what a system needs, the install, what
# Strata is, starting and setting up, and the Tutorial.  This test checks the
# facts that the text states against the programs and the documents that they
# come from, so that the text cannot go out of date:
#
#   - the document is made of the five parts, in the order need, install,
#     about, start, tutorial, and the prerequisites come first; the separate
#     Prerequisites and Tutorial documents are not in the build, the
#     published documents or the installed files;
#   - the options of the install script that the text lists are the options
#     of its help (the option --build-dir belongs to the build and is not in
#     the text), and the commands that the text tells to run exist;
#   - the text never sends a user to the build script, has no section about
#     building, shows no sample document and no list of the files of an
#     install, and does not mention the File Format documents;
#   - every row of the table of the software that Strata needs says Required
#     or Optional, the LaTeX and thesaurus rows say what they need and what is
#     lost without them, and the text says that the programs work without
#     LaTeX;
#   - the section about WYSIWYG names Print and Preview and lists the reasons
#     for Preview;
#   - the text has no word bug, and none of the phrases in which software
#     acts like a person (software is a mechanical device);
#   - the fields of Setup that the text names are fields of the panel;
#   - the memory, the times, the print times, the disk sizes and the size of
#     the prerequisites are the numbers of docs/MEASUREMENTS.txt;
#
# Every check is run again on a copy that is made wrong in one way (a number
# changed, an option left out, two parts in the wrong order, an option that
# the script does not have) and must fail there.
#
#     python3 -I -B tests/gsdoc.py

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = os.path.dirname(HERE)
PROJECT = os.path.dirname(TREE)
SRC = os.path.join(TREE, "manual", "src")
PARTS = ["gs-need.src", "gs-install.src", "gs-about.src", "gs-start.src",
         "gs-tutorial.src"]

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


def read(*path):
    with open(os.path.join(*path), encoding="ascii") as f:
        return f.read()


def load():
    """The texts that the facts come from, by name."""
    t = {"main": read(SRC, "GettingStarted.src")}
    for p in PARTS:
        t[p] = read(SRC, p)
    t["appterm"] = read(SRC, "appterm.src")
    t["termkeys"] = read(SRC, "terminal-keys.src")
    t["allsrc"] = "".join(read(SRC, n) for n in sorted(os.listdir(SRC))
                          if n.endswith(".src"))
    t["measure"] = read(TREE, "docs", "MEASUREMENTS.txt")
    t["build"] = read(TREE, "manual", "build.sh")
    t["help"] = subprocess.run(
        ["bash", os.path.join(PROJECT, "scripts", "strata-install"),
         "--help"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        universal_newlines=True).stdout
    t["setup"] = read(TREE, "src", "str.c")
    t["gs"] = "".join(t[p] for p in PARTS)
    return t


def headline_labels(text):
    """The labels of the headlines of a source, in order."""
    return re.findall(r"^\.h[1-5] .*\[([A-Za-z0-9_.-]+)\]\s*$", text,
                      re.MULTILINE)


# ---- The checks: each takes the texts and returns (ok, detail). ----

def check_parts(t):
    """The parts are included in the order of the document."""
    inc = re.findall(r"^\.include (\S+)$", t["main"], re.MULTILINE)
    if inc != PARTS:
        return False, "the parts are %s" % inc
    first = headline_labels(t["gs-need.src"])[:1]
    return first == ["need"], "the first headline is %s" % first


def check_old_documents(t):
    """The Prerequisites and Tutorial documents are not made any more."""
    names = re.search(r'^cardnames="([^"]*)"', t["build"], re.MULTILINE)
    cards = names.group(1).split() + re.findall(
        r'^cardnames="\$\{cardnames\} ([^"]*)"', t["build"], re.MULTILINE)
    bad = [n for n in ("tutorial", "prereq") if n in " ".join(cards)]
    bad += [f for f in ("prereq.src", "tutorial.src", "prereq.doc",
                        "tutorial.doc")
            if os.path.exists(os.path.join(TREE, "manual", "src", f)) or
            os.path.exists(os.path.join(TREE, "manual", "doc", f))]
    return bad == [], "left: %s" % bad


def table_rows(text, first_cell):
    """The cells of the rows of the table of a source whose first row starts
    with the cell first_cell; continuation lines are joined."""
    lines = text.split("\n")
    rows = []
    for i, line in enumerate(lines):
        if line.startswith(".table") and lines[i + 1].split(" | ")[0] \
                .strip() == first_cell:
            j = i + 2
            while not lines[j].startswith(".end"):
                rows.append([c.strip() for c in lines[j].split("|")])
                j += 1
            return rows
    return []


def doc_options(t):
    """The options of the table of the install options."""
    return sorted(set(re.findall(r"^\{c:(--[a-z-]+)", t["gs-install.src"],
                                 re.MULTILINE)))


def help_options(t):
    """The options of the help of the install script, without --help."""
    return sorted(set(re.findall(r"^\s+(?:-[a-z], )?(--[a-z-]+)", t["help"],
                                 re.MULTILINE)) - {"--help", "--build-dir"})


def check_options(t):
    """The options of the text are those of the help of the script."""
    a, b = doc_options(t), help_options(t)
    return a == b and len(a) >= 10, "text %s\nhelp %s" % (a, b)


def check_commands(t):
    """The commands that the text tells to run name things that exist, and
    none of them is a command of the build."""
    ok = True
    why = []
    if not os.path.exists(os.path.join(PROJECT, "scripts", "strata-install")):
        ok = False
        why.append("scripts/strata-install")
    for part, cmd in (("gs-need.src", "scripts/strata-install --list-prereqs"),
                      ("gs-install.src", "sudo scripts/strata-install\n"),
                      ("gs-install.src", "\nscripts/strata-install\n"),
                      ("gs-install.src",
                       "scripts/strata-install --uninstall --prefix")):
        if cmd not in t[part]:
            ok = False
            why.append("%s does not give %s" % (part, cmd.strip()))
    for part in PARTS:
        if "strata-build" in t[part]:
            ok = False
            why.append("%s sends the reader to the build script" % part)
    return ok, "; ".join(why)


def setup_fields(t):
    """The names of the fields in the paragraph that lists the fields of
    Setup, in gs-start.src."""
    m = re.search(r"The fields stand in four groups\. (.*?)\.\s+A field that holds",
                  re.sub(r"\n", " ", t["gs-start.src"]))
    return re.findall(r"\{b:([^}]*)\}", m.group(1)) if m else []


def check_setup(t):
    """Every field that the text names is in the Setup panel."""
    names = setup_fields(t)
    low = t["setup"].lower()
    missing = [n for n in names if n.lower() not in low]
    return len(names) >= 15 and missing == [], \
        "%d names, missing %s" % (len(names), missing)


def measured(t):
    """The numbers of docs/MEASUREMENTS.txt that the text uses, by table."""
    m = t["measure"]
    start = m.index("AFTER THE MEMORY REDUCTIONS")
    end = m.index("PRINTING WITH SEVERAL PASSES")
    seg = m[start:end]
    mem = {}
    tim = {}
    section = None
    for line in seg.split("\n"):
        if line.startswith("Memory (peak resident"):
            section = mem
        elif line.startswith("Time in milliseconds"):
            section = tim
        elif line.startswith("X11 Editor"):
            section = None
        elif section is not None and re.match(r"^\s+\d+ +[\d.]+ */", line):
            pages = int(line.split()[0])
            pairs = re.findall(r"([\d.]+) */ *([\d.]+)",
                               line[line.index(str(pages)) + len(str(pages)):])
            section[pages] = [after for _, after in pairs]
    mp = {}
    seg2 = m[end:m.index("DISK\n----")]
    for line in seg2.split("\n"):
        mm = re.match(r"^\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([\d.]+)\s+.*?"
                      r"([\d.]+)\s*$", line)
        if mm:
            mp[int(mm.group(1))] = [mm.group(3), mm.group(4),
                                    mm.group(5), mm.group(6)]
    return mem, tim, mp


def table_head(lines, i):
    """The header row of the table that starts at line i, with the lines
    that go on with it, and the index of the line after them."""
    head = lines[i + 1]
    k = i + 2
    while lines[k].startswith(" "):
        head += lines[k]
        k += 1
    return head, k


def number(s):
    """A cell of a table as text without the thousands commas."""
    return s.replace(",", "").strip()


def check_numbers(t):
    """The memory, time and print tables are those of the measurements."""
    mem, tim, mp = measured(t)
    need = t["gs-need.src"]
    why = []
    # The memory table has five columns (pages, words, open, end, save), the
    # time table four (pages, open, search, save), the print table five.
    memrows = {}
    timrows = {}
    prows = {}
    lines = need.split("\n")
    for i, line in enumerate(lines):
        if not line.startswith(".table"):
            continue
        head, j = table_head(lines, i)
        rows = []
        while not lines[j].startswith(".end"):
            rows.append([c.strip() for c in lines[j].split("|")])
            j += 1
        if head.startswith("Pages | Words"):
            memrows = {int(number(r[0])): r for r in rows}
        elif head.startswith("Pages | Time to open"):
            timrows = {int(number(r[0])): r for r in rows}
        elif head.startswith("Pages | PDF pages"):
            prows = {int(number(r[0])): r for r in rows}
    for pages, r in memrows.items():
        want = ["%s MB" % x.strip() for x in
                (mem[pages][0], mem[pages][1], mem[pages][3])]
        got = [r[2], r[3], r[4]]
        if [x.strip() for x in got] != [w.strip() for w in want]:
            why.append("memory %d pages: %s, measured %s" % (pages, got,
                                                              want))
    for pages, r in timrows.items():
        want = ["%.3f s" % (float(x) / 1000.0)
                for x in (tim[pages][0], tim[pages][2], tim[pages][3])]
        got = [r[1], r[2], r[3]]
        if got != want:
            why.append("time %d pages: %s, measured %s" % (pages, got, want))
    for pages, r in prows.items():
        want = mp.get(pages)
        if want is not None:
            want = [want[0], want[1], "%s s" % want[2], "%s MB" % want[3]]
        got = [r[1], r[2], r[3], r[4]]
        if want is None or got != want:
            why.append("print %d pages: %s, measured %s" % (pages, got,
                                                            want))
    if len(memrows) < 5 or len(timrows) < 5 or len(prows) < 5:
        why.append("tables too short: %d %d %d" % (len(memrows),
                                                   len(timrows),
                                                   len(prows)))
    return why == [], "\n".join(why)


def check_units(t):
    """Every number of the memory, time and print tables has its unit, the
    headers say what is measured and in what unit, and no time is given in
    milliseconds."""
    need = t["gs-need.src"]
    lines = need.split("\n")
    why = []
    heads = {}
    for i, line in enumerate(lines):
        if line.startswith(".table"):
            first = lines[i + 1].split(" | ")
            heads[first[0] + "|" + first[1]] = i
    for key, unit, cols in (("Pages|Words", " MB", (2, 3, 4)),
                            ("Pages|Time to open the document", " s",
                             (1, 2, 3)),
                            ("Pages|PDF pages", None, (3, 4))):
        if key not in heads:
            why.append("no table %s" % key)
            continue
        i = heads[key]
        text, j = table_head(lines, i)
        head = [c.strip() for c in text.split("|")]
        while not lines[j].startswith(".end"):
            cells = [c.strip() for c in lines[j].split("|")]
            for col in cols:
                u = unit
                if u is None:
                    u = " s" if col == 3 else " MB"
                if not cells[col].endswith(u):
                    why.append("%s: the cell %r has no %r"
                               % (key, cells[col], u))
            j += 1
        for col in cols:
            h = head[col]
            if key == "Pages|Words" and "(MB)" not in h:
                why.append("memory header %r has no unit" % h)
            if key.startswith("Pages|Time") and "Time to" not in h:
                why.append("time header %r does not say what" % h)
            if key == "Pages|PDF pages" and col == 3 and \
               not h.startswith("Time to print"):
                why.append("print header %r does not say what" % h)
    if "illisecond" in need or re.search(r"\d ms\b", need):
        why.append("the chapter gives a time in milliseconds")
    return why == [], "; ".join(why)


# Terminals, desktops and systems that were never run with Strata.  The manual
# does not name them, so that no reader takes a name for a promise.
UNTESTED = ("GNOME", "KDE", "Xfce ", "Xfce\n", "Plasma", "KWin", "Konsole",
            "kitty", "alacritty", "WezTerm", "iTerm", "PuTTY", "Tilix",
            "Terminator", "rxvt", "Guake", "Yakuake", "Cinnamon", "LXQt",
            "Openbox", "fvwm", "Windows Terminal", "macOS", "Linux console",
            "virtual console", "Windows 10", "GNU screen")
HEAD3 = ("Key the terminal takes | Strata command | "
         "Alternative Strata keybinding")


def check_names(t):
    """The chapter, the terminal appendix and its table name only the
    desktops and terminals that were run, say that Windows 11 through WSL2
    works, and have a subsection for each of them."""
    why = []
    for part in ("gs-need.src", "appterm", "termkeys", "allsrc"):
        text = t[part]
        for name in UNTESTED:
            if name in text:
                why.append("%s names %r" % (part, name.strip()))
    need = t["gs-need.src"]
    if re.search(r"not supported|have not been tested|has not been tested "
                 r"and", need):
        why.append("the chapter says that something is not supported")
    labels = headline_labels(need)
    for label in ("desk-windowmaker", "desk-windows", "desk-x11",
                  "term-xterm", "term-tmux", "term-xfce4", "term-others",
                  "keys-conflict"):
        if label not in labels:
            why.append("no subsection %s" % label)
    for part in PARTS:
        for m in re.finditer(r"sample-envelope|sample-label|complaint-|"
                             r"chapter-0|strata-story\.doc|"
                             r"share/strata/(?:samples|templates|tutorial)/"
                             r"\S+\.(?:doc|png)", t[part]):
            why.append("%s names the sample file %r" % (part, m.group(0)))
    if "{c:xterm}" not in need or "WSL2" not in need:
        why.append("the chapter does not describe xterm and WSL2")
    lines = need.split("\n")
    tables = 0
    for i, line in enumerate(lines):
        if line.startswith(".table") and "Key the" in lines[i + 1]:
            tables += 1
            if lines[i + 1].strip() != HEAD3:
                why.append("a table has the header %r" % lines[i + 1])
    if tables < 2:
        why.append("the terminal tables are %d" % tables)
    desk = need[need.index("[desktops]"):need.index("[terminal-needs]")]
    if ".table" in desk:
        why.append("the desktops part has a table")
    return why == [], "; ".join(why)


def check_disk(t):
    """The disk sizes and the size of the prerequisites are measured."""
    need = t["gs-need.src"]
    m = t["measure"]
    why = []
    for text, where in (("whole prefix", "5,452"),
                        ("126 packages", "126 packages"),
                        ("191 MB", "191 MB"), ("661 MB", "661 MB")):
        if where not in m:
            why.append("the measurements lack %s" % text)
    for want in ("5.4 MB", "3.7 MB", "9 MB", "126 packages",
                 "191 MB", "661 MB", "3.0 MB", "2.2 MB", "32 KB",
                 "256 MB", "700 MB"):
        if want not in need:
            why.append("the text lacks %s" % want)
    return why == [], "; ".join(why)


def flat(text):
    """The text with the lines of a paragraph joined."""
    return re.sub(r"\n(?! )", " ", text)


def software_rows(t):
    """The rows of the table of the software that Strata needs, with the
    lines that go on with a cell joined to it: a list of cell lists."""
    lines = t["gs-need.src"].split("\n")
    for i, line in enumerate(lines):
        if line.startswith(".table") and lines[i + 1].startswith("Software |"):
            rows = []
            j = i + 1
            while not lines[j].startswith(".end"):
                if lines[j].startswith(" ") and rows:
                    rows[-1][-1] += " " + lines[j].strip()
                else:
                    rows.append([c.strip() for c in lines[j].split(" | ")])
                j += 1
            return rows
    return []


def check_software(t):
    """Every row of the table of the software says Required or Optional,
    and the rows of LaTeX and of the thesaurus data say what they need and
    what is lost without them."""
    rows = software_rows(t)
    why = []
    if len(rows) < 10:
        return False, "the table has %d rows" % len(rows)
    if rows[0][:2] != ["Software", "Required or optional"]:
        why.append("the header is %s" % rows[0])
    body = {r[0]: r for r in rows[1:]}
    for r in rows[1:]:
        if len(r) != 3 or r[1] not in ("Required", "Optional"):
            why.append("the row %r does not say Required or Optional" % r[0])
    required = [r[0] for r in rows[1:] if len(r) > 1 and r[1] == "Required"]
    if len(required) != 2:
        why.append("the required rows are %s" % required)
    latex = body.get("LaTeX", ["", "", ""])
    for want in ("Printing", "Preview", "equations", "LaTeX markers",
                 "Without LaTeX"):
        if want not in latex[-1]:
            why.append("the LaTeX row lacks %r" % want)
    if latex[1:2] != ["Optional"]:
        why.append("the LaTeX row is not Optional")
    thes = body.get("Thesaurus data", ["", "", ""])
    for want in ("MyThes", ".dat", ".idx", "mythes-en-us", "Without the data"):
        if want not in thes[-1]:
            why.append("the thesaurus row lacks %r" % want)
    if thes[1:2] != ["Optional"]:
        why.append("the thesaurus row is not Optional")
    if not re.search(r"programs work without LaTeX; without it, nothing can "
                     r"be printed", flat(t["gs-need.src"])):
        why.append("the text does not say that the programs work without "
                   "LaTeX")
    return why == [], "; ".join(why)


def check_install_text(t):
    """The install chapter has no building section, no sample documents and
    no list of files, uses the install tool to remove Strata, and no part of
    Getting Started mentions the File Format documents."""
    why = []
    inst = t["gs-install.src"]
    if re.search(r"Building from the source|\[building\]", inst):
        why.append("a section about building is there")
    for part in PARTS:
        if part in ("gs-install.src", "gs-need.src") and \
           re.search(r"\bsamples?\b", t[part], re.I):
            why.append("%s shows sample documents" % part)
        if re.search(r"The File Formats|File Format documents|"
                     r"strata-file-format", t[part]):
            why.append("%s mentions the File Format documents" % part)
    if ".include gs-files" in inst or \
       os.path.exists(os.path.join(SRC, "gs-files.src")):
        why.append("a list of the installed files is there")
    if "Directory | What the directory is for" not in inst:
        why.append("the layout table of the directories is gone")
    if re.search(r"^\{c:share/[^}]*\.(?:doc|pdf|png)\}", inst, re.M):
        why.append("the layout table names a file")
    return why == [], "; ".join(why)


def check_optional_install(t):
    """The install chapter says that a missing optional program never stops
    the install of an ordinary user, that the warning is the last block of
    the output and begins with a fixed line, and no part of the text says
    that the install stops or ends with the exit status 5."""
    inst = " ".join(t["gs-install.src"].split())
    why = []
    for want in ("never stops the install", "Optional programs are missing:",
                 "the last block of the output", "the exit status is 0",
                 "lists each missing package"):
        if want not in inst:
            why.append("the install chapter lacks %r" % want)
    for part in PARTS:
        text = " ".join(t[part].split())
        if re.search(r"exit status 5|the install stops when one is missing|"
                     r"install stops when|nothing is installed: no file",
                     text):
            why.append("%s says that the install stops" % part)
    return why == [], "; ".join(why)


def check_wysiwyg(t):
    """The section about WYSIWYG names Print and Preview as ways to see the
    layout and lists the reasons for Preview."""
    s = t["gs-about.src"]
    a = s.index("[wysiwyg]")
    b = s.index(".h2", a)
    body = s[a:b]
    items = re.findall(r"^- ", body, re.M)
    why = []
    for want in ("{b:Preview}", "{b:Print}", "PDF file", "better choice"):
        if want not in body:
            why.append("the section lacks %r" % want)
    if len(items) < 3:
        why.append("the reasons are %d items" % len(items))
    return why == [], "; ".join(why)


# The phrases in which software acts like a person.  Software is a
# mechanical device: a program does not ask, say, know, want, try, decide or
# refuse, and "it installs nothing" becomes "nothing is installed".
ANTHRO = [
    r"\bit installs\b",
    r"\bit (?:asks|says|knows|wants|tries|decides|refuses|prefers|thinks|"
    r"understands|tells|warns|offers|expects|remembers|forgets|notices|"
    r"learns|guesses|suggests|finds|looks|names)\b",
    r"\b(?:Strata|the Editor|the Document Manager|the Printer|the Viewer|"
    r"the program|the programs|the script|the install script|the panel|"
    r"xterm|tmux|the terminal) (?:asks|says|knows|wants|tries|decides|"
    r"refuses|prefers|thinks|understands|tells|warns|offers|expects|"
    r"remembers|forgets|notices|learns|guesses|suggests|helps|looks|finds)\b",
]


def check_style(t):
    """No word bug, and no phrase in which software acts like a person."""
    why = []
    for part in PARTS:
        text = flat(t[part])
        if re.search(r"\bbugs?\b", text, re.I):
            why.append("%s has the word bug" % part)
        for rx in ANTHRO:
            for m in re.finditer(rx, text, re.I):
                why.append("%s: %r" % (part, text[max(0, m.start() - 20):
                                                 m.end() + 20]))
    return why == [], "; ".join(why[:6])


# ---- The same checks on copies that are made wrong. ----

def wrong(t, key, old, new):
    """A copy of the texts in which the text old of one part is new."""
    u = dict(t)
    assert old in u[key], (key, old)
    u[key] = u[key].replace(old, new, 1)
    return u


def main():
    t = load()
    for name, fn in (("the five parts are included in the order of the "
                      "document and the prerequisites come first",
                      check_parts),
                     ("the separate Prerequisites and Tutorial documents "
                      "are not made", check_old_documents),
                     ("the options of the text are the options of the "
                      "install script", check_options),
                     ("the commands of the text exist", check_commands),
                     ("the fields of Setup in the text are fields of the "
                      "panel", check_setup),
                     ("the memory, time and print tables are those of the "
                      "measurements", check_numbers),
                     ("every number of the memory, time and print tables "
                      "has its unit and the headers say what is measured",
                      check_units),
                     ("the chapter names only the desktops and terminals that "
                      "were run, and has a subsection for each",
                      check_names),
                     ("the disk sizes are those of the measurements",
                      check_disk),
                     ("every row of the software table says Required or "
                      "Optional, and LaTeX and the thesaurus say what they "
                      "need", check_software),
                     ("a missing optional program never stops the install: "
                      "the warning is the last block of the output",
                      check_optional_install),
                     ("the install chapter has no building section, no "
                      "sample document, no list of files and no File "
                      "Format document", check_install_text),
                     ("the WYSIWYG section names Print and Preview and "
                      "lists the reasons for Preview", check_wysiwyg),
                     ("the text has no bug and no software that acts like "
                      "a person", check_style)):
        ok, why = fn(t)
        check(name, ok, why)

    # Each check must fail on a text that is wrong in one way.
    cases = [
        ("two parts in the wrong order", check_parts,
         wrong(t, "main", ".include gs-need.src\n.include gs-install.src",
               ".include gs-install.src\n.include gs-need.src")),
        ("a part left out", check_parts,
         wrong(t, "main", ".include gs-about.src\n", "")),
        ("an option that the script has not", check_options,
         wrong(t, "gs-install.src", "{c:--quiet}", "{c:--quieter}")),
        ("an option left out of the table", check_options,
         wrong(t, "gs-install.src", "{c:--dry-run}", "{c:--no-such}")),
        ("a field of Setup that does not exist", check_setup,
         wrong(t, "gs-start.src", "{b:Backups Kept}", "{b:Backups Lept}")),
        ("a memory number changed", check_numbers,
         wrong(t, "gs-need.src",
               "3625 | 1,812,586 | 65.9 MB | 87.5 MB | 100.4 MB",
               "3625 | 1,812,586 | 65.9 MB | 87.5 MB | 101.4 MB")),
        ("a time changed", check_numbers,
         wrong(t, "gs-need.src", "1000 | 0.193 s | 0.174 s | 0.199 s",
               "1000 | 0.193 s | 0.175 s | 0.199 s")),
        ("a print time changed", check_numbers,
         wrong(t, "gs-need.src", "3600 | 2535 | 3 | 47.8 s | 58.8 MB",
               "3600 | 2535 | 3 | 47.9 s | 58.8 MB")),
        ("a time cell without its unit", check_units,
         wrong(t, "gs-need.src", "0.045 s", "0.045")),
        ("a memory cell without its unit", check_units,
         wrong(t, "gs-need.src", "4.6 MB", "4.6")),
        ("a memory header without its unit", check_units,
         wrong(t, "gs-need.src", "Memory after a save (MB)",
               "After a save")),
        ("a time header that says only Open", check_units,
         wrong(t, "gs-need.src", "Time to open the document", "Open")),
        ("a time in milliseconds", check_units,
         wrong(t, "gs-need.src", "The times are in seconds (s)",
               "The times are in milliseconds")),
        ("a desktop that was never run is named", check_names,
         wrong(t, "gs-need.src", "WindowMaker takes none",
               "GNOME takes none")),
        ("the Linux console is named in a part of the manual",
         check_names, wrong(t, "allsrc", "The Editor checks it when it",
                            "The Linux console is checked when it")),
        ("a terminal of the appendix that was never run is named",
         check_names, wrong(t, "termkeys", "{c:tmux} |", "Konsole |")),
        ("the old sentence about WSL2 is back", check_names,
         wrong(t, "gs-need.src", "and it also runs on Windows 11",
               "and Windows (through WSL2) is not supported, and it also "
               "runs on Windows 11")),
        ("a subsection of a terminal is missing", check_names,
         wrong(t, "gs-need.src", "[term-tmux]", "[term-tmuxx]")),
        ("a table with another header", check_names,
         wrong(t, "gs-need.src", HEAD3, "Key | Command | Other")),
        ("a table of desktops is back", check_names,
         wrong(t, "gs-need.src", ".h3 WindowMaker [desk-windowmaker]",
               ".table 20 20\nA | B\n.end\n.h3 WindowMaker "
               "[desk-windowmaker]")),
        ("a disk size changed", check_disk,
         wrong(t, "gs-need.src", "661 MB", "660 MB")),
        ("the measurements changed under the text", check_numbers,
         wrong(t, "measure", "131.1/ 87.5", "131.1/ 88.5")),
        ("the text sends the reader to the build script", check_commands,
         wrong(t, "gs-install.src",
               "scripts/strata-install --uninstall --prefix",
               "scripts/strata-build uninstall --prefix")),
        ("a row of the software table does not say Required or Optional",
         check_software,
         wrong(t, "gs-need.src", "ghostscript | Optional |",
               "ghostscript | Maybe |")),
        ("the LaTeX row says Required", check_software,
         wrong(t, "gs-need.src", "LaTeX | Optional |", "LaTeX | Required |")),
        ("the thesaurus row has no format", check_software,
         wrong(t, "gs-need.src", "MyThes", "a")),
        ("the statement that the programs work without LaTeX is gone",
         check_software,
         wrong(t, "gs-need.src", "programs work\nwithout LaTeX",
               "programs need LaTeX")),
        ("a section about building is back", check_install_text,
         wrong(t, "gs-install.src",
               ".h2 The programs that Strata needs [prereqs]",
               ".h2 Building from the source [building]\nx\n"
               ".h2 The programs that Strata needs [prereqs]")),
        ("a sample document is shown", check_install_text,
         wrong(t, "gs-install.src",
               "{c:share/strata/templates} | The templates of the New "
               "command.", "{c:share/strata/samples} | The sample "
               "documents.")),
        ("the layout table names a file", check_install_text,
         wrong(t, "gs-install.src",
               "{c:share/strata/templates} | The templates of the New "
               "command.", "{c:share/strata/x.doc} | A file.")),
        ("the File Format documents are mentioned", check_install_text,
         wrong(t, "gs-about.src", "All of these documents are written",
               "- {b:The File Formats} describes the files.\n"
               "All of these documents are written")),
        ("Print is not named as a way to see the layout", check_wysiwyg,
         wrong(t, "gs-about.src", "{b:Print} to a PDF file",
               "Printing to a PDF file")),
        ("the reasons for Preview are gone", check_wysiwyg,
         wrong(wrong(wrong(t, "gs-about.src", "\n- Preview writes", "\nPreview"
                           " writes"),
                     "gs-about.src", "\n- The pages appear",
                     "\nThe pages appear"),
               "gs-about.src", "\n- Preview can show",
               "\nPreview can show")),
        ("the word bug is used", check_style,
         wrong(t, "gs-start.src", "Start the Document Manager, in its",
               "A bug: start the Document Manager, in its")),
        ("software installs nothing like a person", check_style,
         wrong(t, "gs-install.src", "never stops the install: Strata is",
               "it installs nothing at all: Strata is")),
        ("the install is said to stop for a missing program",
         check_optional_install,
         wrong(t, "gs-install.src", "never stops the install",
               "stops the install, exit status 5,")),
        ("the warning is not said to be the last block",
         check_optional_install,
         wrong(t, "gs-install.src", "and the last block of the",
               "and a line of the")),
        ("Strata asks like a person", check_style,
         wrong(t, "gs-tutorial.src", "a question follows whether",
               "Strata asks whether")),
    ]
    for name, fn, u in cases:
        ok, _ = fn(u)
        check("the check fails when %s" % name, not ok)
    if fails != 0:
        print("gsdoc: %d check(s) failed" % fails)
        return 1
    print("gsdoc: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
