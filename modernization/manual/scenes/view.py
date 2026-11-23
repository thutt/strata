# Scenes of chapter 1 (A View of Strata) and of the appendices D and E.
# Each scene is: name, program, arguments, keys, and the part to keep.  The
# help file is given with an absolute name because the programs do not find
# it by themselves when the environment variable is the only hint.

import os

# The help file of the environment variable STRATA_HELP, if it is set.
HELP = os.environ.get("STRATA_HELP") or os.path.join(
    os.environ.get("STRATA_BUILD_DIR", ""), "strata.hlp")

SCENES = [
    ("view-structured", "stred", ["-H", HELP, "@/report.doc"], "",
     None),
    ("view-expanded", "stred", ["-H", HELP, "@/report.doc"],
     "F12 Right Return o space Insert", None),
    ("view-menu-top", "stred", ["-H", HELP, "@/report.doc"], "F12",
     None),
    ("view-menu-global", "stred", ["-H", HELP, "@/report.doc"],
     "F12 Right Return", None),
    ("view-marker-grid", "stred", ["-H", HELP, "@/showcase.doc"],
     "alt+F12", None),
    ("view-altmenu", "stred", ["-H", HELP, "@/report.doc"], "ctrl+f",
     None),
    ("view-unit", "stred", ["-H", HELP, "@/report.doc"], "F3",
     None),
    ("view-highlight", "stred", ["-H", HELP, "@/report.doc"],
     "Down Down Down F3 Return Right Right Right Right Right Right Right "
     "Right Right Right Right Right Right Right Right Right Right",
     None),
    ("view-outline", "stred", ["-H", HELP, "@/report.doc"], "alt+2",
     None),
    ("view-section-edit", "stred", ["-H", HELP, "@/report.doc"],
     "Down Down Down Down Down Down Down Down ctrl+asciicircum",
     None),
    ("view-help", "stred", ["-H", HELP, "@/report.doc"], "F1",
     None),
    ("view-docmgr", "stred", ["-H", HELP], "", None),
    ("view-str", HERE + "/scenes/neutral-str.sh", [], "", None),
    ("view-save-quit", "stred", ["-H", HELP, "-u", "flat.doc"],
     "text:abc ctrl+q", None),
    ("view-keycap", "stred", ["-H", HELP, "@/showcase.doc"], "wait:6",
     None),
    ("view-pick", "stred", ["-H", HELP, "@/report.doc"], "F12 Return Return F2",
     None),
    ("view-dviview", "stred", ["@/showcase.doc"],
     "F12 Right Right Right Return Right Right Return Return Insert wait:25 "
     "win:preview.dvi wait:1"),
]
