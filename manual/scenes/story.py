# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# The picture of the structured sample on the screen: the document is opened
# from the folder samples/story of the source tree (%Y in shots.py) and the
# cursor is moved down into the contents, where the numbers of the sections
# and the page numbers are rows of cross reference markers.  The wait is long
# enough for the picture of the logo of the title page to be made.

SCENES = [
    ("edit-story-sample", "stred", ["%Y/strata-story.doc"],
     "Down " * 14 + "wait:30", (0, 0, 800, 520)),
]
