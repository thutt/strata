# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# Strata is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along
# with Strata.  If not, see <https://www.gnu.org/licenses/>.
# Copyright (c) 2026 Logic Magicians Software

"""The numbers of the sample document "How Strata Was Made", in ONE place.

The generator of the text (tests/mkstory.py) and the generator of the charts
(tests/mkstorypics.py) read this module, and so does the test of the sample
(tests/story.py).  The chapters of the document do not hold these numbers:
they hold names, written between two at signs (@words_per_page@), and the
lines .datarows NAME that stand for the rows of a table.  When the
measurements are made again, this is the only file that changes, and the
test compares it with the documents of the measurements.

WORDS_PER_PAGE is the number of words that a page of the measured documents
holds.  The text says it as a fact about the measurement.

The measurement of 20 November 2026 (task 42) uses 500 words to a page, as
the owner asked; the measurement of 18 November used 735 words to a page.  The
numbers of that first measurement are kept in BASELINE_735 below, marked as
such, and nothing quotes them.
"""
import sys

sys.dont_write_bytecode = True

# The number of words that a page of the measured documents holds.
WORDS_PER_PAGE = 500

# The sizes of the measured documents, in pages.
PAGES = [100, 500, 1000, 2000, 3625]

# The memory of the terminal Editor in megabytes, before / after, for the
# four steps: open, end, search, save.  One row for each size in PAGES.
MEMORY = [
    [(4.5, 4.6), (6.3, 5.1), (6.5, 5.2), (6.9, 5.4)],
    [(10.8, 10.9), (19.5, 13.4), (20.0, 13.8), (22.3, 15.0)],
    [(19.5, 19.5), (37.2, 24.9), (38.0, 25.8), (42.7, 28.4)],
    [(37.2, 37.3), (73.1, 48.9), (74.8, 50.6), (84.5, 55.9)],
    [(65.8, 65.9), (131.1, 87.5), (134.2, 90.6), (152.3, 100.4)],
]

# The time of the terminal Editor in milliseconds, before / after, for the
# same four steps.
TIME = [
    [(26, 45), (14, 6), (29, 28), (31, 34)],
    [(143, 141), (64, 23), (116, 116), (130, 139)],
    [(192, 193), (124, 47), (176, 174), (189, 199)],
    [(289, 301), (177, 90), (263, 259), (290, 307)],
    [(464, 460), (253, 142), (407, 396), (457, 486)],
]

# The time of a save in milliseconds of the three builds: before the memory
# work, the memory work only, the final build.
SAVE = [
    (31, 32, 34),
    (130, 130, 139),
    (189, 190, 199),
    (290, 293, 307),
    (457, 451, 486),
]

# The memory of one document of SHADOW_PAGES pages in megabytes that are
# alive after the cursor was moved to the end: (what, before, after).
SHADOW_PAGES = 1000
SHADOW = [
    ("The text of the document, four bytes a token", 12.3, 12.3),
    ("The records of the blocks", 0.3, 0.3),
    ("The Undo shadow: the text", 15.3, 2.7),
    ("The Undo shadow: lines, columns, blocks", 1.7, 1.8),
    ("The layout of the screen (rows, cells)", 3.2, 3.2),
    ("Help file, buffers of the screen, other", 0.4, 0.4),
    ("Total alive", 33.2, 20.7),
]

# The prints that need several passes: documents of PASS_PAGES pages, the
# passes, the seconds and the peak memory in megabytes.
PASS_PAGES = [10, 100, 500, 1000, 3600]
PASSES = [2, 2, 2, 2, 3]
PASS_SECONDS = [1.4, 2.2, 5.6, 9.8, 47.8]
PASS_MEMORY = [50.8, 51.0, 51.6, 52.1, 58.8]

# THE BASELINE: the measurement of 18 November 2026 with 735 words to a page,
# on the machine of the owner (AMD Ryzen 7 7840HS).  Kept for the record; the
# text, the charts and the test do not use it, and its numbers are not
# comparable with those above (another page, another machine).
BASELINE_735 = {
    "words_per_page": 735,
    "pages": [100, 500, 1000, 2000, 3625],
    "memory": [
        [(7.2, 5.2), (9.8, 5.9), (9.9, 6.0), (10.3, 6.3)],
        [(14.9, 15.0), (27.4, 18.2), (27.9, 18.8), (30.9, 20.7)],
        [(30.5, 28.5), (56.5, 36.4), (57.8, 37.8), (63.8, 41.5)],
        [(55.6, 56.7), (107.9, 73.3), (110.6, 76.0), (124.5, 84.1)],
        [(96.4, 94.5), (191.5, 125.9), (196.3, 130.9), (220.1, 144.8)],
    ],
    "time": [
        [(13, 12), (6, 3), (9, 9), (16, 17)],
        [(36, 37), (19, 7), (25, 26), (48, 46)],
        [(66, 68), (33, 11), (46, 45), (86, 82)],
        [(128, 128), (61, 21), (87, 85), (159, 153)],
        [(224, 227), (106, 34), (152, 150), (278, 267)],
    ],
    "save": [(16, 18, 17), (48, 58, 46), (86, 106, 82), (159, 200, 153),
             (278, 345, 267)],
    "pass_pages": [10, 100, 500, 1000, 3600],
    "passes": [2, 2, 3, 3, 3],
    "pass_seconds": [1.5, 2.6, 11.2, 20.3, 68.6],
    "pass_memory": [50.7, 50.7, 51.5, 52.0, 87.2],
    "shadow_saved": (19, 67),
    "end_ms": (322, 134),
    "count_ms": (88, 25),
}

# The commits of each day of November 2026, from the log of the repository.
DAYS = ["08", "09", "10", "11", "12", "13", "14", "15", "16", "17", "18"]
COMMITS = [1, 26, 34, 22, 18, 22, 81, 83, 159, 162, 147]

# The size of the two editions before the rewrite of 21 November 2026: the
# pages and the words of the printed text (pdftotext, the leader dots of the
# contents and the lists not counted).  The editions after the rewrite are at
# most half of these (tests/story.py).
BEFORE = {"dev": (45, 15957), "rel": (36, 12764)}

# The commits to the end of 19 November, and those of them that were made
# between midnight and seven in the morning, from the log of the repository.
COMMITS_TO_19 = 878
COMMITS_NIGHT = 195

# The early fixes of the largest document (the commits of 11 and 12
# November): the memory after the layout in megabytes and the time to open in
# tenths of a second, before and after.
EARLY_MEMORY = (609, 92)
EARLY_OPEN_TENTHS = (39, 9)

# The counts of the checks of the last verification.
UNIT_CHECKS = 3268
SMOKE_OK = 2555
SMOKE_SECONDS = 1182
XSMOKE_OK = 150

# The sentences of the chapters that quote a number of the tables above.
# The names are the names that the chapters write between at signs.
SCALARS = {
    "words_per_page": str(WORDS_PER_PAGE),
    "pages_big": "3,625",
    "pages_whole": "5,258",
    "pdf_pages_whole": "3,625",
    "pages_mid": "1,000",
    "pages_first": "100",
    "pass_pages_last": "3,600",
    "shadow_saved_mid": "12",
    "shadow_saved_big": "44",
    "end_before_ms": "253",
    "end_after_ms": "142",
    "mem_big_before": "152.3",
    "mem_big_after": "100.4",
    "mem_ratio": "0.34",
    "mem_percent": "34",
    "mid_before": "43",
    "mid_after": "28",
    "mid_percent": "34",
    "x11_more": "about 10",
    "count_big_ms": "65",
    "count_mid_ms": "19",
    "save_slow_ms": "345",
    "save_base_ms": "278",
    "save_memory_only_ms": "451",
    "save_before_ms": "457",
    "save_final_ms": "486",
    "shadow_text_before": "15.3",
    "shadow_sum_after": "20.7",
    "end_move_before": "253",
    "end_move_after": "142",
    "text_mb_mid": "12.3",
    "total_mb_mid": "20.7",
    "token2_saving_big": "23",
    "save_buffer_big": "10",
    "max_pass_two": "1,000",
    "commits_16": str(COMMITS[DAYS.index("16")]),
    "commits_17": str(COMMITS[DAYS.index("17")]),
    "commits_18": str(COMMITS[DAYS.index("18")]),
    "commits_to_19": str(COMMITS_TO_19),
    "commits_night": str(COMMITS_NIGHT),
    "unit_checks": "3,268",
    "smoke_ok": "2,555",
    "smoke_seconds": "1,182",
    "xsmoke_ok": "150",
    "early_mem_before": "609",
    "early_mem_after": "92",
    "early_open_before": "3.9",
    "early_open_after": "0.9",
}


def num(value):
    """A measured number as the tables print it: 12.0 is 12."""
    if isinstance(value, float):
        return "%.1f" % value
    return str(value)


def pair(before, after):
    """The cell of a table that holds a number before and after."""
    return "%s / %s" % (num(before), num(after))


def rows(name):
    """The lines of the rows of a table of the document, from the numbers
    above; the first line of the table (its header) is in the source."""
    out = []
    if name == "memory" or name == "time":
        table = MEMORY if name == "memory" else TIME
        for pages, row in zip(PAGES, table):
            out.append(" | ".join([str(pages)] + [pair(b, a)
                                                  for b, a in row]))
    elif name == "save":
        for pages, row in zip(PAGES, SAVE):
            out.append(" | ".join([str(pages)] + [str(v) for v in row]))
    elif name == "shadow":
        for what, before, after in SHADOW:
            out.append("%s | %s | %s" % (what, num(before), num(after)))
    elif name == "early":
        out.append("Memory after the layout, 15.6 MB document (12 Nov) | "
                   "%d MB | %d MB" % EARLY_MEMORY)
        out.append("Time to open, debug build (11 Nov) | %.1f s | %.1f s" %
                   (EARLY_OPEN_TENTHS[0] / 10.0, EARLY_OPEN_TENTHS[1] / 10.0))
    elif name == "passes":
        for i, pages in enumerate(PASS_PAGES):
            out.append(" | ".join([str(pages), str(PASSES[i]),
                                   num(PASS_SECONDS[i]),
                                   num(PASS_MEMORY[i])]))
    else:
        raise KeyError("no table of data is called %s" % name)
    return out
