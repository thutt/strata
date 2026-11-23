#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# Strata is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along
# with Strata.  If not, see <https://www.gnu.org/licenses/>.
# Copyright (c) 2026 Logic Magicians Software

"""Make a synthetic structured document of more than a thousand pages.

usage: mkbig.py OUTDIR [CHAPTERS]

The script writes chapter sources in the form of manual/src and a master
file into OUTDIR, and runs manual/mkmanual.py --single on them, so that the
document has what the Reference Manual has: a title page, contents with page
numbers, chapters of three levels of sections with paragraphs, bullets,
tables, boxed notes, footnotes, keycaps, cross-references, index entries and
an index.  The result is OUTDIR/big.doc.  A word that occurs only once, in
the last chapter, is written to OUTDIR/needle.txt.  The text is made by a
generator with a fixed seed, so every run makes the same document.
"""
import sys as _s; _s.dont_write_bytecode = True
import os
import random
import subprocess
import sys

WORDS = ("the editor document section block column marker table index "
         "chapter page margin header footer format print preview search "
         "replace outline level number reference contents figure note "
         "keycap panel menu field value default option setting command "
         "screen cursor line text word paragraph heading numbering style "
         "layout font size width height left right top bottom center "
         "copy paste delete insert move split join sort save load open "
         "close quit file name path disk memory buffer limit count total "
         "first last next previous before after between inside outside "
         "above below around through during while until when where which "
         "because although however therefore otherwise meanwhile").split()


def sentence(rng, n):
    words = [rng.choice(WORDS) for _ in range(n)]
    words[0] = words[0].capitalize()
    return " ".join(words) + "."


def paragraph(rng, ref):
    parts = [sentence(rng, rng.randint(8, 18))
             for _ in range(rng.randint(4, 7))]
    if ref is not None:
        parts.insert(2, "See {x:%s} and {ref:%s}." % (ref, ref))
    if rng.random() < 0.3:
        parts.insert(1, "Press {k:Ctrl-S} to {b:save} the {i:document}"
                     "{fn:%s}." % sentence(rng, 8)[:-1])
    return " ".join(parts)


def chapter(rng, n, nchap, last):
    lines = [".doc Chapter %d" % n, ".chapter %d" % n,
             ".h1 Chapter %d about %s [c%d]" % (n, rng.choice(WORDS), n),
             ".ix Chapter %d" % n]
    lines.append(paragraph(rng, None))
    lines.append("")
    for s in range(1, 21):
        lines.append(".h2 Section %d.%d on %s [c%ds%d]" % (
            n, s, rng.choice(WORDS), n, s))
        lines.append(".ix term%d-%d" % (n, s))
        lines.append(paragraph(rng, "c%ds%d" % (rng.randint(1, nchap),
                                               rng.randint(1, 20))))
        lines.append("")
        lines.append("- " + sentence(rng, 9))
        lines.append("- " + sentence(rng, 12))
        lines.append("")
        lines.append(".table 14 20 30")
        lines.append("Name | Kind | Meaning")
        for r in range(4):
            lines.append("%s | %s | %s" % (rng.choice(WORDS),
                                           rng.choice(WORDS),
                                           sentence(rng, 7)))
        lines.append(".end")
        lines.append(".note " + sentence(rng, 14))
        for t in range(1, 3):
            lines.append(".h3 Part %d.%d.%d [c%ds%dp%d]" % (
                n, s, t, n, s, t))
            lines.append(paragraph(rng, None))
            lines.append("")
            lines.append(paragraph(rng, None))
            lines.append("")
    if last:
        lines.append("The word Zyzzyvaquux occurs once in this document.")
    return "\n".join(lines) + "\n"


def main():
    out = sys.argv[1]
    nchap = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    rng = random.Random(1987)
    os.makedirs(out, exist_ok=True)
    master = ["# A synthetic manual."]
    for n in range(1, nchap + 1):
        name = "ch%d" % n
        with open(os.path.join(out, name + ".src"), "w") as f:
            f.write(chapter(rng, n, nchap, n == nchap))
        master.append("%d %s Chapter %d" % (n, name, n))
    with open(os.path.join(out, "master.txt"), "w") as f:
        f.write("\n".join(master) + "\n")
    with open(os.path.join(out, "needle.txt"), "w") as f:
        f.write("Zyzzyvaquux\n")
    here = os.path.dirname(os.path.abspath(__file__))
    gen = os.path.join(here, "..", "manual", "mkmanual.py")
    r = subprocess.run([sys.executable, "-I", "-B", gen, "--single",
                        os.path.join(out, "master.txt"),
                        os.path.join(out, "big.doc")])
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
