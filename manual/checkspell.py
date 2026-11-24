#!/usr/bin/env python3
# Copyright (c) 2026 Logic Magicians Software
#
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This program checks the spelling of the prose of the Reference Manual with
# aspell.  The text that is checked is the running text of the sources of
# the manual (the master and the parts that it includes): the commands
# {c:...}, {k:...}, {eq:...}, {x:...} and {n:...}, the directives that name
# things (.pic, .ix, a label) and the comment lines are taken out first, so
# that the words of the program font, the keycaps and the equations are not
# looked at.  A word that aspell does not know and that is not in
# manual/checkspell.allow (one word to a line, compared without regard to
# the case, the technical words of the manual) is a hit.
#
#     python3 -I -B manual/checkspell.py [--list] [--self-test] [--words]
#
# --words prints the unknown words with the files that hold them, for
# building the allow file.  --self-test puts a misspelled word into the text
# and the check must find it.  Without aspell the run says so and passes.
import os
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import checkfacts as CF  # noqa: E402

DROP = re.compile(r"\{(?:c|k|eq|x|X|n|ref|pic|tex|m):[^{}]*\}")


def prose(text):
    """The running text of a source: one string, one line per line."""
    out = []
    for line in text.split("\n"):
        if line.startswith(("#", ".pic", ".ix")):
            continue
        line = re.sub(r"^\.[a-z0-9]+ ?(\[[^\]]*\])?", "", line)
        for _ in range(3):
            line = DROP.sub(" ", line)
        line = re.sub(r"\{[a-zA-Z]+:([^{}]*)\}", r"\1", line)
        out.append(line)
    return "\n".join(out)


def load_allow():
    """The words that are spelled as they are on purpose."""
    path = os.path.join(HERE, "checkspell.allow")
    words = set()
    if os.path.exists(path):
        for line in CF.read(path).splitlines():
            line = line.strip()
            if line != "" and not line.startswith("#"):
                words.add(line.lower())
    return words


def unknown(texts):
    """The words that aspell does not know: word -> list of file names."""
    found = {}
    for name, text in texts:
        run = subprocess.run(["aspell", "-l", "en", "list"], input=text,
                             capture_output=True, text=True)
        for word in run.stdout.split():
            found.setdefault(word, [])
            if name not in found[word]:
                found[word].append(name)
    return found


def texts_of_manual(extra=None):
    """The prose of every source: a list of (file name, text)."""
    texts = []
    for path in CF.master_files():
        texts.append((os.path.basename(path), prose(CF.read(path))))
    if extra is not None:
        texts.append(("test", extra))
    return texts


def main(argv):
    if shutil.which("aspell") is None:
        print("checkspell: aspell is not installed: the spelling is not "
              "checked")
        return 0
    allow = load_allow()
    if "--self-test" in argv:
        found = unknown(texts_of_manual("The quik brwn fox jumpd.\n"))
        bad = [w for w in found if w.lower() not in allow]
        ok = "quik" in bad and "brwn" in bad
        print("%s a misspelled word is found" % ("ok  " if ok else "FAIL"))
        return 0 if ok else 1
    found = unknown(texts_of_manual())
    if "--words" in argv:
        for word in sorted(found, key=str.lower):
            print(word, " ".join(found[word][:3]))
        return 0
    hits = {w: f for w, f in found.items() if w.lower() not in allow}
    for word in sorted(hits, key=str.lower):
        print("%s: %s" % (hits[word][0], word))
    print("checkspell: %d word(s) not in the dictionary" % len(hits))
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
