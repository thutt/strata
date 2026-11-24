# The pictures of the second half of chapter 3 (Edit): Marker to Words.
# Every scene is (name, program, arguments, keys, crop); see shots.py.
# The keys of the section edit scene first walk down to a subsection.

WALK = "Down Down Down Down Down Down"

SCENES = [
    ("edit-b-marker-grid", "stred", ["@/report.doc"], "alt+F12",
     (0, 0, 800, 720)),
    ("edit-b-marker-date", "stred", ["@/report.doc"], "alt+F12 d Return",
     (0, 0, 800, 560)),
    ("edit-b-replace", "stred", ["@/report.doc"],
     "alt+F9 text:the Return", (0, 0, 800, 660)),
    ("edit-b-section-edit", "stred", ["@/report.doc"],
     WALK + " F12 Right Right Return Right Return", (0, 0, 800, 560)),
    ("edit-b-outline-2", "stred", ["@/report.doc"], "alt+2",
     (0, 0, 800, 560)),
    ("edit-b-move-section", "stred", ["@/report.doc"],
     "Down Down Down Down F7 s Down Down", (0, 0, 800, 560)),
    ("edit-b-undo", "stred", ["@/report.doc"],
     "height:380 Down Down text:Hello_world_ ctrl+z", (0, 0, 800, 380)),
    ("edit-b-overtype", "stred", ["@/report.doc"], "alt+o",
     (0, 0, 800, 560)),
    ("edit-b-options", "stred", ["@/report.doc"],
     "F12 Right Return Right Right Right Right Return", (0, 0, 800, 720)),
    ("edit-b-count", "stred", ["@/report.doc"],
     "F12 Right Right Right Right Return Right Right Right Right Right "
     "Return", (0, 0, 800, 560)),
    ("edit-b-split", "stred", ["@/report.doc"], "Down F2",
     (0, 0, 800, 560)),
    ("edit-b-quit", "stred", ["@/report.doc"], "x F12 End Return",
     (0, 0, 800, 560)),
    ("edit-b-sort-before", "stred", ["@/report.doc"],
     "Down Down Down Down Down", (0, 0, 800, 560)),
    ("edit-b-sort-after", "stred", ["@/report.doc"],
     "Down Down Down Down Down F12 Right Right Right Right Return "
     "Right Right Right Right Return Return", (0, 0, 800, 560)),
    ("edit-b-split-section", "stred", ["@/report.doc"],
     "Down " + "Right " * 22 + "F2 s", (0, 0, 800, 560)),
    ("edit-b-window-menu", "stred", ["@/report.doc"],
     "F12 Right Right Return Right Right Return", (0, 0, 800, 330)),
    ("edit-b-window-open", "stred", ["@/report.doc"],
     "F12 Right Right Return Right Right Return Return", (0, 0, 800, 560)),
    ("edit-b-move-done", "stred", ["@/report.doc"],
     "height:560 Down Down Down Down F7 s Return Down", (0, 0, 800, 560)),
]
