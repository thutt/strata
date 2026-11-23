# Pictures of the Error Log, the Print Error markers and the Search panel.
# The document is the test document tests/data/print_errors.doc, which
# shots.py copies into the directory of the samples before each picture: it
# has a bad equation, a missing picture and a missing character.  The
# programs are the ones of the build that XBIN names.

ED = "stred"
DOC = "@/print_errors.doc"

# The Print command writes print_errors.pdf next to the document; the
# directory of the samples is made again before each picture, so the PDF is
# not there for the next one.
PRINT = "F12 text:p text:f Insert wait:6 wait:2 "

SCENES = [
    ("edit-c-search-panel", ED, [DOC], "F9 Down space wait:1",
     (0, 0, 800, 600)),
    ("markers-errors", ED, [DOC], PRINT + "ctrl+F8 ctrl+F8 wait:2",
     (0, 0, 800, 440)),
    ("print-failed-message", ED, [DOC], PRINT + "wait:1", (0, 905, 800, 952)),
    ("print-viewer-failed", ED, [DOC],
     "F12 text:p text:p text:d Insert wait:12 win:preview.dvi wait:2 n "
     "wait:1", (0, 0, 850, 1024)),
    ("markers-errors-box", ED, [DOC],
     PRINT + "ctrl+F8 ctrl+F8 ctrl+F8 wait:1 hold:alt+F12 wait:1",
     (0, 0, 800, 600)),
]
