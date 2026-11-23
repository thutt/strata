#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This test checks tools/hyc.py, the reader of a custom hyphenation
# dictionary of the original (a .HYC file, docs/DICTIONARIES.txt section 3).
# tests/data/custom.hyc was made by the original from a document of six
# words with soft hyphens; the words below are what the original wrote
# back when it was asked to turn the dictionary into a document.
#
#     python3 -I -B tests/hyc.py

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))

import hyc

failures = 0


def check(ok, what):
    global failures

    if ok:
        print("ok   " + what)
    else:
        print("FAIL " + what)
        failures += 1


with open(os.path.join(HERE, "data", "custom.hyc"), "rb") as f:
    data = f.read()

EXPECT = ["ab-cd", "extra-ordi-nary-califra-gilistic", "num-ber1-23",
          "pre-fix-ed", "x-y", "zor-ba-flux"]

count, words = hyc.decode(data)
check(count == 6, "the count of the words is 6: %d" % count)
check(words == EXPECT, "the words and their hyphens are read: %s" % words)

# A bit of the mask of zor-ba-flux (hyphens after the letters 3 and 5, 14
# hex) moved: the reader must show the other place, so that the test cannot
# pass by reading the words only.
at = data.find(b"zorbaflux") + len(b"zorbaflux")
changed = bytearray(data)
check(changed[at:at + 4] == b"\x00\x00\x00\x14",
      "the mask of zorbaflux is 00 00 00 14 in the file")
changed[at + 3] = 0x24
count2, words2 = hyc.decode(bytes(changed))
check("zor-baf-lux" in words2 and "zor-ba-flux" not in words2,
      "a changed mask gives the other hyphens: %s" % words2)

# A damaged file is refused and not read as far as it goes.
for what, bad in (("cut to a part of a page", data[:1000]),
                  ("a wrong count of the words",
                   data[:508] + b"\x00\x00\x00\x07" + data[512:])):
    try:
        hyc.decode(bad)
        check(False, "%s is refused" % what)
    except ValueError:
        check(True, "%s is refused" % what)

print("hyc: %d failures" % failures)
sys.exit(1 if failures != 0 else 0)
