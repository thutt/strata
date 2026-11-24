# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# The pictures of chapter 7 (the Document Manager, str).  Each scene is a
# file name, the program, its arguments, the keys, and the part of the
# window to keep.  The help file of the original is named by the option -H
# so that F1 shows a topic.

import os

HELP = os.environ.get("STRATA_HELP") or os.path.join(
    os.environ.get("STRATA_BUILD_DIR", ""), "strata.hlp")
H = ["-H", HELP]
# The scenes of Setup start the Document Manager with no folder of this
# machine in view.
NEUTRAL = HERE + "/scenes/neutral-str.sh"
ALL = (0, 0, 800, 950)

SCENES = [
    ("str-main", NEUTRAL, [], "", (80, 290, 720, 640)),
    ("str-edit-panel", NEUTRAL, [], "Return", (40, 330, 760, 600)),
    ("str-variables-panel", NEUTRAL, [],
     "Return Down F2 text:COLOR Down text:red Down text:region Down "
     "text:EU", (40, 250, 760, 650)),
    ("str-newfile-type", NEUTRAL, [], "Return text:harbor Insert",
     (40, 330, 760, 600)),
    ("str-newfile-panel", NEUTRAL, [],
     "Return text:harbor Insert Return", (40, 330, 760, 600)),
    # Maintain Dictionaries: the spelling words and, behind Tab, the
    # hyphenation list.
    ("str-dict-spelling", HERE + "/scenes/hyph-str.sh", [], "height:380 text:M Return",
     (0, 0, 800, 380)),
    ("str-dict-hyphenation", HERE + "/scenes/hyph-str.sh", [],
     "height:380 text:M Return Tab", (0, 0, 800, 380)),
    ("str-fonts-list", NEUTRAL, [], "End Up Up Return wait:4", (0, 200, 800, 760)),
    ("str-font-panel", NEUTRAL, [], "End Up Up Return wait:4 Return",
     (40, 300, 760, 660)),
    ("str-setup-panel", NEUTRAL, [], "End Up Return", (40, 225, 760, 700)),
    # A value that is wrong is refused where it was typed: the reason is in
    # the line at the foot and the focus stays in the field.
    ("str-setup-error", NEUTRAL, [],
     "End Up Return Down Down Down text:abc Down", (40, 225, 760, 700)),
    # Print To: the list of the choices (F2), with PDF viewer in it.
    ("str-setup-printto", NEUTRAL, [],
     "End Up Return Down Down Down Down F2 wait:1", (40, 225, 760, 700)),
    # A PDF Viewer that is only a name is refused: the full path is needed.
    ("str-setup-viewer", NEUTRAL, [],
     "End Up Return Down Down Down Down Down Down Down text:evince Insert "
     "wait:1", (40, 225, 760, 700)),
    # The third page of Setup (PgDn from the first): the settings that are seldom
    # changed.
    ("str-setup-advanced", NEUTRAL, [], "End Up Return Next wait:1",
     (40, 250, 760, 700)),
    # The Setup panel in a window of 20 rows: it scrolls.
    ("str-setup-scrolled", NEUTRAL, [], "height:380 End Up Return",
     (0, 0, 800, 380)),
    # The second page of Setup (PgUp): the colours of the screen, and its palette.
    ("str-colors-panel", NEUTRAL, [], "End Up Return Prior wait:1",
     (40, 235, 760, 700)),
    ("str-colors-palette", NEUTRAL, [],
     "End Up Return Prior wait:1 F2 wait:1", (40, 235, 760, 700)),
    # Compare Document Versions: the panel, and the report that it shows.
    ("str-compare-panel", NEUTRAL, [],
     "Down Down Return Down text:memo.doc Down text:memo-v2.doc",
     (0, 200, 800, 640)),
    ("str-compare-report", NEUTRAL, [],
     "Down Down Return Down text:memo.doc Down text:memo-v2.doc Insert "
     "wait:2", (0, 0, 800, 600)),
    # Restore Backup: the panel, the list of the backups of the memo, the
    # small menu of a backup, and the question before a restore.
    ("str-restore-panel", NEUTRAL, [],
     "Down Down Down Return Down text:memo.doc", (0, 380, 800, 550)),
    ("str-restore-list", NEUTRAL, [],
     "Down Down Down Return Down text:memo.doc Insert wait:1",
     (0, 415, 800, 540)),
    ("str-restore-menu", NEUTRAL, [],
     "Down Down Down Return Down text:memo.doc Insert wait:1 Return wait:1",
     (0, 400, 800, 530)),
    ("str-restore-box", NEUTRAL, [],
     "Down Down Down Return Down text:memo.doc Insert wait:1 Return Down "
     "Return wait:1", (0, 380, 800, 570)),
    ("str-setup-browse", NEUTRAL, [],
     "End Up Return KP_Add",
     (40, 225, 760, 700)),
]
