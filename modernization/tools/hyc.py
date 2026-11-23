#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program reads a custom hyphenation dictionary of the old format (a file
# with the extension .HYC) and prints its words, one on a line, with a hyphen
# where a word may be broken.  The layout of the file is described in
# docs/DICTIONARIES.txt (section 3).  It also reads the sorted count and
# checks the structure, and says so when the file is damaged.
#
#     python3 -I -B tools/hyc.py FILE.HYC

import sys

PAGE = 512


def decode(data):
    """Return (count, words) of the bytes of a .HYC file."""
    if len(data) % PAGE != 0 or len(data) < 2 * PAGE:
        raise ValueError("not a multiple of 512 bytes, or no data page")

    count = int.from_bytes(data[PAGE - 4:PAGE], "big")
    words = []

    for p in range(1, len(data) // PAGE):
        page = data[p * PAGE:(p + 1) * PAGE]
        n = page[-1]

        if n + 1 > PAGE:
            raise ValueError("page %d: bad count" % p)

        lens = list(page[PAGE - 1 - n:PAGE - 1])
        off = 0

        for length in lens:
            entry = page[off:off + length]
            off += length

            if length < 5:
                raise ValueError("page %d: entry too short" % p)

            word = entry[:-4].decode("latin-1")
            mask = int.from_bytes(entry[-4:], "big")
            text = ""

            for i, ch in enumerate(word):
                text += ch

                if (mask >> i) & 1 and i + 1 < len(word):
                    text += "-"

            words.append(text)

    if count != len(words):
        raise ValueError("count %d but %d words" % (count, len(words)))

    return count, words


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: hyc.py FILE.HYC\n")
        return 2

    with open(argv[1], "rb") as f:
        data = f.read()

    try:
        count, words = decode(data)
    except ValueError as e:
        sys.stderr.write("hyc.py: %s\n" % e)
        return 1

    for w in words:
        print(w)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
