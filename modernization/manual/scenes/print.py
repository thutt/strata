# The pictures of chapter 6 (Print and Preview).  The Print menu is F12 and
# the letter P; its commands are chosen with the first letter of the name
# (D for Draft, F for Final, P for Preview).  Return in a panel moves to the
# next field and Insert accepts the panel.  The window is 800 by 950 pixels.

import os
import sys

CROP = (0, 0, 800, 560)
# The Draft panel has fourteen fields since the index, the lists and the
# black and white setting were added.
TALL = (0, 0, 800, 830)
# The Final panel has four more fields than the Draft panel.
FINAL = (0, 0, 800, 890)

RAW = [
    ("print-menu", "stred", ["@/report.doc"], "F12 text:p", CROP),
    ("print-draft-panel", "stred", ["@/report.doc"], "F12 text:p text:d",
     TALL),
    ("print-final-panel", "stred", ["@/report.doc"], "F12 text:p text:f",
     FINAL),
    ("print-preview-menu", "stred", ["@/report.doc"],
     "F12 text:p text:p", CROP),
    ("print-preview-doc-panel", "stred", ["@/report.doc"],
     "F12 text:p text:p text:d", (0, 0, 800, 580)),
]

SCENES = list(RAW)

SCENES += [
    ("print-viewer-first", "stred", ["@/showcase.doc"],
     "F12 text:p text:p text:d Insert wait:8 win:preview.dvi wait:1"),
]
