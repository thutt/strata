# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# The pictures of chapter 4 (Format).  Each scene is a file name, a
# program, its arguments, the keys to type and the part of the window to
# keep.  Prior and Next are the PgUp and PgDn keys of the keyboard.  The
# Global menu is the third entry of the menu bar (File, Edit, Global ...)
# so that F12 Right selects it; Return opens it and Return again opens
# the entry Format.

G = "F12 Right Return "
CROP = (0, 0, 800, 740)
TALL = (0, 0, 800, 880)
MEMO = ["@/memo.doc"]
R3 = "Right Right Right "

SCENES = [
    ("format-global-menu", "stred", MEMO, "F12 Right Return", CROP),
    ("format-global-format", "stred", MEMO, G + "Return", CROP),
    ("format-global-spacing", "stred", MEMO, G + "Return Prior",
     (0, 0, 800, 760)),
    ("format-global-linenum", "stred", MEMO, G + "Return Prior Prior", CROP),
    ("format-global-lists", "stred", MEMO, G + "Return Prior Prior Prior",
     CROP),
    ("format-level1", "stred", MEMO, G + "Return Prior Prior Prior Prior",
     CROP),
    ("format-level1-spacing", "stred", MEMO,
     G + "Return Prior Prior Prior Prior Prior", CROP),
    ("format-page-global", "stred", MEMO, G + "Right Return", CROP),
    ("format-page-title", "stred", MEMO, G + "Right Return Prior", CROP),
    ("format-typeface", "stred", MEMO, G + "Right Right Return", TALL),
    ("format-marker", "stred", MEMO, G + R3 + "Return", CROP),
    ("format-marker-modes", "stred", MEMO, G + R3 + "Return Prior", CROP),
    ("format-keycap", "stred", MEMO, G + R3 + "Return Prior Prior Prior Prior",
     CROP),
    ("format-footnote", "stred", MEMO,
     G + R3 + "Return Prior Prior Prior Prior Prior Prior Prior", CROP),
    ("format-counters", "stred", MEMO, G + R3 + "Return Next", TALL),
    ("format-options", "stred", MEMO, G + R3 + "Right Return", CROP),
    ("format-named", "stred", MEMO, G + R3 + "Right Right Return Return",
     CROP),
    # F2 on Winding Columns of the Named Block Format: the layout of the
    # columns of a block, two columns, a gutter of 0.5 inch and a single rule.
    ("format-named-winding", "stred", MEMO,
     G + R3 + "Right Right Return Return" + " Down" * 12 +
     " F2 text:2 Down text:0.5 Down Right Right wait:1", CROP),
    ("format-expanded-status", "stred", MEMO,
     G + R3 + "Right Return Right Insert Down Down F6 Return Return Return"
     " Return text:8,20R Return", CROP),
]
