# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# The picture of a sample envelope in the Editor: the German sample, opened
# from the folder samples/envelopes of the source tree (%E in shots.py).  The
# sample has a made-up sender line, a gap that is a whitespace marker, and the
# address.

SCENES = [
    ("edit-envelope-sample", "stred",
     ["%E/sample-envelope-de-din5008-dl.doc"], "wait:1",
     (0, 0, 800, 330)),
    ("view-envelope-preview", "stred", ["%E/sample-envelope-us-ca-10.doc"],
     "F12 Right Right Right Return Right Right Return Return Insert wait:25 "
     "win:preview.dvi wait:1"),
]
