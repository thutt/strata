# The pictures of the first half of chapter 3 (Edit): the edit screen, the
# keys, and the commands from Append to Join.  Every scene is (name,
# program, arguments, keys, crop); see shots.py.  The two unstructured
# scenes create a new file in the sample directory ("-u" makes it flat).

WALK = "Down Down Down Down Down Down"
TYPED = ("text:Dear_all, Return text:The_first_line_is_short. Tab "
         "text:after_a_tab Return text:A_second_line. ")

SCENES = [
    ("edit-a-screen", "stred", ["@/report.doc"], "Down Down",
     (0, 0, 800, 560)),
    ("edit-a-flat", "stred", ["-u", "new1.doc"], TYPED,
     (0, 0, 800, 200)),
    ("edit-a-blocks", "stred", ["-u", "new2.doc"],
     "text:Tides_rise_at_dusk ctrl+a text:Gulls_follow_the_boats F2 text:c "
     "text:beyond_the_pier", (0, 0, 800, 200)),
    ("edit-a-append", "stred", ["@/report.doc"], WALK + " alt+F2",
     (0, 330, 800, 640)),
    ("edit-a-copy-menu", "stred", ["@/report.doc"], "Down F3",
     (0, 330, 800, 640)),
    ("edit-a-highlight", "stred", ["@/report.doc"],
     "Down F3 t Right Right Right Right Right Right Right Right Right "
     "Right Right Right", (0, 0, 800, 560)),
    ("edit-a-highlight-block", "stred", ["@/report.doc"],
     "Down Down F4 b Down Down Down", (0, 0, 800, 560)),
    ("edit-a-goto-menu", "stred", ["@/report.doc"], "alt+F7",
     (0, 330, 800, 640)),
    ("edit-a-goto-panel", "stred", ["@/report.doc"],
     "alt+F7 s text:2.1.2", (0, 330, 800, 640)),
    ("edit-a-goto-done", "stred", ["@/report.doc"],
     "alt+F7 s text:2.1.2 Return", (0, 0, 800, 560)),
    ("edit-a-join-menu", "stred", ["@/report.doc"], WALK + " alt+F3",
     (0, 330, 800, 640)),
    ("edit-a-fold-1", "stred", ["@/report.doc"], "alt+1",
     (0, 0, 800, 460)),
    ("edit-a-fold-minus", "stred", ["@/report.doc"],
     "Down Down Down Down Down Down alt+minus", (0, 0, 800, 560)),
    ("edit-a-menubar", "stred", ["@/report.doc"], "F12 Right Right",
     (0, 0, 800, 330)),
    ("edit-a-menubar-tools", "stred", ["@/report.doc"],
     "F12 Right Right Right Right Return", (0, 0, 800, 330)),
]
