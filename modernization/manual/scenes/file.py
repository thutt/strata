# The pictures of chapter 2 (File).  Each scene is the name of the picture,
# the program, its arguments, the keys to type and the part of the window to
# keep.  The File menu is F12 and Return; the commands are reached with
# Right.  Return in a panel moves to the next field; Insert accepts.
#
# Save is the first command, so the keys are F12 Return Return.  Retrieve
# needs one Right, New two, Delete three, Import four and Export five.

# The window is 800 by 950 pixels; the panels are in the top 640.
CROP = (0, 0, 800, 640)

RAW = [
    ("file-save", "stred", ["@/memo.doc"], "F12 Return Return"),
    ("file-save-options", "stred", ["@/memo.doc"],
     "F12 Return Return Down Down F2"),
    ("file-overwrite", "stred", ["@/memo.doc"],
     "F12 Return Return Insert"),
    ("file-retrieve", "stred", ["@/memo.doc"],
     "F12 Return Right Return"),
    ("file-retrieve-options", "stred", ["@/memo.doc"],
     "F12 Return Right Return Down Down F2"),
    ("file-checklist-files", "stred", ["@/memo.doc"],
     "F12 Return Right Return Down F2"),
    ("file-checklist-dirs", "stred", ["@/memo.doc"],
     "F12 Return Right Return F2"),
    ("file-retrieve-failed", "stred", ["@/memo.doc"],
     "F12 Return Right Return Down text:nothere Insert"),
    ("file-new", "stred", ["@/memo.doc"],
     "F12 Return Right Right Return"),
    ("file-new-type", "stred", ["@/memo.doc"],
     "F12 Return Right Right Return Down text:harbor Insert"),
    ("file-delete", "stred", ["@/memo.doc"],
     "F12 Return Right Right Right Return"),
    ("file-delete-confirm", "stred", ["@/memo.doc"],
     "F12 Return Right Right Right Return Down text:report.doc Insert",
     ),
    ("file-import", "stred", ["@/memo.doc"],
     "F12 Return Right Right Right Right Return"),
    ("file-export", "stred", ["@/memo.doc"],
     "F12 Return Right Right Right Right Right Return"),
    ("file-export-charset", "stred", ["@/memo.doc"],
     "F12 Return Right Right Right Right Right Return Down Down Down F2",
     ),
]


SCENES = [scene + (CROP,) for scene in RAW]
