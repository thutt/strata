# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# The pictures of the Case command (Ctrl-F6): the box that asks for the kind
# of change, and the box that asks for the unit after the first choice.  The
# document is the sample report, with the cursor on its first headline.

SCENES = [
    ("edit-c-case-menu", "stred", ["@/report.doc"], "ctrl+F6 wait:1",
     (0, 0, 800, 560)),
    ("edit-c-case-unit", "stred", ["@/report.doc"], "ctrl+F6 Return wait:1",
     (0, 0, 800, 560)),
]
