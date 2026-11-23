# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# The pictures of chapter 5 (Markers).  Each scene is a file name, a
# program, its arguments, the keys to type and the part of the window to
# keep.  The Marker Commands menu is a grid of 35 entries in the order of
# the menu, so that Home followed by n times Down selects the entry n.

SHOW = ["@/showcase.doc"]
CROP = (0, 0, 800, 700)
M = "alt+F12 Home "


def pick(n):
    """The keys that choose the nth entry of the Marker Commands menu."""
    return M + "Down " * n + "Return"


G = "F12 Right Return Right Right Right Return "
XREF = "Down Down Down End Left Left alt+F12"
# The scenes that show the path of the document (the marker Pathname) start
# the Editor with no folder of this machine in view.
NEUTRAL_STRED = HERE + "/scenes/neutral-stred.sh"
ALL = ["@/markers-all.doc"]

SCENES = [
    ("markers-menu", "stred", SHOW, "alt+F12", CROP),
    ("markers-counter", "stred", SHOW, pick(3) + " Return text:5 Return space "
     "space", CROP),
    ("markers-xref", "stred", SHOW, XREF, CROP),
    ("markers-date", "stred", SHOW, pick(5) + " text:Mon_D,_Year", CROP),
    ("markers-include", "stred", SHOW, pick(16) + " text:part-one.doc", CROP),
    ("markers-index", "stred", SHOW, pick(17) + " text:Strata Return "
     "text:Markers", CROP),
    ("markers-keycap", "stred", SHOW, pick(19) + " text:Ctrl-S", CROP),
    ("markers-line", "stred", SHOW, pick(21) + " text:3 Return text:0.02",
     CROP),
    ("markers-picture", "stred", SHOW, pick(28) + " text:pictures/sample.png "
     "Return Return text:2", CROP),
    ("markers-picture-missing", "stred", ["@/picmissing.doc"], "wait:6",
     (0, 0, 800, 340)),
    ("markers-whitespace", "stred", SHOW, pick(34) + " text:0.5 Return space",
     CROP),
    ("markers-note-edit", "stred", SHOW, pick(23) + " text:The text of the "
     "note.", CROP),
    ("markers-note-type", "stred", SHOW, pick(23) + " text:The text. Insert",
     CROP),
    ("markers-comment", "stred", SHOW, pick(2) + " text:Check this. Insert",
     CROP),
    ("markers-font", "stred", SHOW, pick(14) + " F2", CROP),
    ("markers-modes-panel", "stred", SHOW, G + "Prior", CROP),
    ("markers-latex-label", "stred", ["@/latexdemo.doc"], "wait:12",
     (0, 0, 800, 300)),
    ("markers-latex-edit", "stred", SHOW, pick(20) + " text:\\begin{center}"
     "\\rule{6cm}{0.8pt}\\end{center}", (0, 0, 800, 120)),
    ("markers-all-default", NEUTRAL_STRED, ALL, "", CROP),
    ("markers-all-name", NEUTRAL_STRED, ALL, G + "Prior space space Insert", CROP),
    ("markers-all-long", NEUTRAL_STRED, ALL, G + "Prior space space space space "
     "Insert", CROP),
]
