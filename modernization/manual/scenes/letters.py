# The picture of the sample letters (chapter 2, New): the letter to the
# utility on the screen.  The document sample-letter.doc is a copy of the
# letter samples/letters/complaint-to-utility-us.doc, which shots.py puts into
# the folder of the scene.  The window is 800 by 950 pixels.

SCENES = [
    ("file-letter-screen", "stred", ["@/sample-letter.doc"], "wait",
     (0, 0, 800, 600)),
]
