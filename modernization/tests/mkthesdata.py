#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program writes the small test thesaurus in the MyThes format: the two
# files th_test.idx and th_test.dat go into the directory that is its
# argument.  Nothing of the real data of mythes-en-us is used; the words and
# senses are made up for the tests (tests/data/README.txt says what each one
# is for).  The offsets in the index are the byte positions of the entries in
# the data file, as in the real files.
#
#     python3 -I tests/mkthesdata.py tests/data

import sys

# Every entry is the headword and its senses.  A sense is the part of speech and
# its words; a word may carry a relation in parentheses, which is how MyThes
# marks the terms that are not synonyms.
ENTRIES = {
    "abbey": [("noun", ["monastery", "convent (related term)"])],
    "abbot": [("noun", ["monk (generic term)", "superior"])],
    "abdicate": [("verb", ["resign", "renounce", "step down"])],
    "abduct": [("verb", ["kidnap", "seize"])],
    "abet": [("verb", ["aid", "assist", "help"])],
    "abide": [("verb", ["stay", "remain", "endure"])],
    "area": [("noun", ["region", "zone", "district"])],
    "center": [("noun", ["core", "heart", "middle"]),
               ("verb", ["focus", "concentrate"])],
    "core": [("noun", ["center", "heart", "kernel"])],
    "fast": [("adj", ["quick", "rapid", "swift"]),
             ("adv", ["quickly", "rapidly"])],
    "happy": [("adj", ["glad", "joyful", "content", "cheerful"]),
              ("adj", ["fortunate", "lucky", "sad (antonym)"])],
    "headline": [("noun", ["title", "heading", "caption"])],
    "heart": [("noun", ["center", "core", "spirit"])],
    "next day": [("noun", ["tomorrow"])],
    "passage": [("noun", ["excerpt", "citation", "quotation"])],
    "piece of writing": [("noun", ["composition", "article"])],
    "quick": [("noun", ["center", "core", "next day", "quick (related term)"]),
              ("adj", ["fast", "rapid", "swift", "speedy",
                       "slow (antonym)", "brisk (similar term)",
                       "nimble (similar term)", "movement (generic term)"]),
              ("adv", ["quickly", "promptly"]),
              ("adj", ["animal (generic term)"])],
    "rapid": [("adj", ["fast", "quick", "swift"])],
    "run": [("verb", ["sprint", "dash", "jog"]),
            ("noun", ["sprint", "race"])],
    "section": [("noun", ["part", "segment", "division", "piece of writing"]),
                ("noun", ["area", "region"]),
                ("verb", ["divide", "split"])],
    "sector": [("noun", ["area", "zone", "section"])],
    "set": [("noun", ["group", "collection", "kit"]),
            ("verb", ["place", "put", "lay"])],
    "slow": [("adj", ["sluggish", "unhurried"])],
    "speedy": [("adj", ["fast", "quick", "swift"])],
    "swift": [("adj", ["fast", "rapid", "speedy"])],
    "zeal": [("noun", ["passion", "fervor"])],
    "zebra": [("noun", ["equine (generic term)"])],
    "zone": [("noun", ["area", "region", "belt"])],
    "zoom": [("verb", ["speed", "rush", "zip"])],
    "zygote": [("noun", ["egg", "fertilized egg"])],
}


def build(path):
    names = sorted(ENTRIES, key=lambda w: w.encode("ascii"))
    dat = "UTF-8\n"
    idx = []
    for w in names:
        idx.append((w, len(dat.encode("ascii"))))
        senses = ENTRIES[w]
        dat += "%s|%d\n" % (w, len(senses))
        for pos, words in senses:
            dat += "(%s)|%s\n" % (pos, "|".join(words))
    text = "UTF-8\n%d\n" % len(idx)
    text += "".join("%s|%d\n" % e for e in idx)
    open(path + ".idx", "w", newline="\n").write(text)
    open(path + ".dat", "w", newline="\n").write(dat)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: mkthesdata.py DIRECTORY")
    build(sys.argv[1] + "/th_test")


if __name__ == "__main__":
    main()
