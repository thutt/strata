# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# The pictures of three features that were added late: the panel of the
# Section marker with its field Show, the list that New shows when there are
# templates, and the Description field of the Save File panel.  The example of
# the Section marker is made by scenes/mkpicdocs.py.  The list of the
# templates is made from the folder templates of the source tree by the
# launcher templates-stred.sh.

SEC = ["@/secmark.doc"]
MEMO = ["@/memo.doc"]
TEMPLATES = HERE + "/scenes/templates-stred.sh"

NEW = "F12 Return Right Right Return "
# The list of the templates is in the order of their names after the two
# built-in settings; four Down keys choose the German window envelope, and
# End and Up choose the first letter (the two letters are the last two
# templates: the label sheets come after the envelopes).
LETTER = NEW + "End Up Return "

SCENES = [
    # Ctrl-F12 on the marker at the start of the line opens its panel.
    ("format-section-marker", "stred", SEC, "ctrl+F12 wait:1",
     (0, 0, 800, 520)),
    ("file-new-list", TEMPLATES, MEMO, NEW + "Down Down Down Down wait:1",
     (0, 0, 800, 1000)),
    # The label sheet 5160 found by typing its number.
    ("file-new-labels", TEMPLATES, MEMO, NEW + "text:5160 wait:1",
     (0, 0, 800, 1000)),
    # The Description field of the New File panel, with the template of the
    # list and a description typed.
    ("file-new-description", TEMPLATES, MEMO,
     LETTER + "Down text:harbour-letter Down Down "
     "text:A_letter_to_the_harbour_master. wait:1", (0, 0, 800, 620)),
]
