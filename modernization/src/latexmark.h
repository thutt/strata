/*
 * Strata is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Strata is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Strata.  If not, see <https://www.gnu.org/licenses/>.
 */
/* Copyright (c) 2026 Logic Magicians Software */

/*
 * latexmark.h - the text of a LaTeX marker for the picture job.  The X11
 * Editor draws a LaTeX marker as the picture that LaTeX makes of its text,
 * typeset alone in a box.  There is no table of commands that are refused:
 * the text is given to LaTeX, and when LaTeX reports an error, or the job
 * takes more than 5 seconds, or the result has no ink (a marker that only
 * sets a state, breaks a page or makes space shows nothing), the marker is
 * drawn in its Short form.  See docs/LATEXMARKER-PICTURES.txt.
 */
#ifndef LATEXMARK_H
#define LATEXMARK_H
#include <stddef.h>


/* The longest text that is drawn as a picture (bytes, after preparation). */
#define LMK_MAX 900


/*
 * Make the text that is given to LaTeX from the text of a marker: the
 * comments (from a percent sign to the end of the line) are removed and
 * the lines are joined with one space, with no space at either end.
 * Returns the length, or -1 when the result does not fit in cap bytes or
 * is longer than LMK_MAX.
 */
int
lmk_prepare(const char *raw, char *out, size_t cap);
#endif
