# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# The pictures of the Math command (Ctrl-F2), the Border Layout panel
# (Alt-F8), the Winding Column Layout panel and a page layout with a header
# and a footer.  The example documents are made by scenes/mkpicdocs.py.  The
# Global menu is the third entry of the menu bar, so F12 Right Return opens
# it and Return again opens the entry Format; Prior and Next are the PgUp and
# PgDn keys.

G = "F12 Right Return "
NUM = ["@/numbers.doc"]
BRD = ["@/border.doc"]
TBL = ["@/table.doc"]
HF = ["@/headfoot.doc"]

SCENES = [
    # The box that asks what to calculate.
    ("edit-c-math-unit", "stred", NUM, "ctrl+F2 wait:1", (0, 0, 800, 560)),
    # Block, the four lines of figures highlighted, and the result on the
    # message line, which is the foot of the window.
    ("edit-c-math-result", "stred", NUM,
     "ctrl+F2 Down Down Return Down Down Down Down Return wait:1",
     (0, 906, 800, 950)),
    # The panel of Alt-F8 with the top and the bottom double.
    ("edit-b-border-panel", "stred", BRD,
     "alt+F8 Return Down Down Right Right Down Down Down Down Right Right"
     " wait:1", (0, 0, 800, 760)),
    # The table of three columns and five rows before any border, then the
    # highlight over the three columns of the first two rows of the table
    # (the rectangle that Border works on), and the table after the two applications of the example.
    ("edit-c-border-table", "stred", TBL, "wait:1", (0, 0, 800, 460)),
    ("edit-c-border-highlight", "stred", TBL,
     "Down alt+F8 shift+Right shift+Right Down wait:1",
     (0, 0, 800, 460)),
    ("edit-c-border-example", "stred", TBL,
     "Down alt+F8 shift+Right shift+Right Return"
     " Right Right Down Down Down Down Down Down Right Right Insert"
     " shift+Left shift+Left Down"
     " alt+F8 shift+Right shift+Right Down Down Down Return"
     " Down Down Down Right Down Right Down Right Down Right Insert"
     " wait:1", (0, 0, 800, 460)),
    # F2 on Winding Columns, the last field of the second Global Format
    # panel, opens the layout of the columns.
    ("format-global-winding", "stred", NUM,
     G + "Return Prior" + " Down" * 13 + " F2 text:2 Down text:0.3 wait:1",
     (0, 0, 800, 560)),
    # The Global Page Layout of a document that has a header and a footer.
    ("format-page-headfoot", "stred", HF, G + "Right Return",
     (0, 0, 800, 740)),
]
