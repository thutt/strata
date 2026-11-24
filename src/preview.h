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
 * preview.h - F12, Print, Preview through LaTeX and the DVI viewer.
 *
 * The part of the document that the caller names is written to a private
 * temporary directory, strprint makes a DVI file of it with latex while a
 * panel shows the progress, and the viewer dviview shows the file.  All the
 * files are deleted afterwards, on every error and when the program is
 * stopped.  See docs/PRINT-LATEX.txt.
 */
#ifndef PREVIEW_H
#define PREVIEW_H
#include "editor.h"
#include "pvcore.h"

/*
 * Preview part (a part of the document of the editor, made with the
 * doc_part functions).  The result is told in the message line of e.
 * error_log is 0 to leave the Error Log out of the pages.  The faults of the
 * run become Print Error markers of the document (perr.h); when faults is not
 * NULL they are put there instead, with the items of the document, and
 * nothing is marked (the Document Manager's Preview has no document to
 * mark).
 */
void
preview_run(Editor *e, Doc *part, int error_log, DiagList *faults);


/*
 * 0 leaves the PRINT FAILED banner out of the pages of the next previews
 * (the marks stay); 1 puts it in (the default).
 */
void
preview_set_banner(int on);


/*
 * Draw the wait panel of a run of strprint: the title of the box, the
 * stage (the third stage, after LaTeX, is called last), the page, the gauge
 * and the line of hints (NULL for "Esc cancels").  Print uses it too.
 */
void
preview_panel(const char *title, const char *last, const PvProg *p,
              const char *extra);


/* The directory of the document, or "." for a document with no name. */
void
preview_docdir(const Editor *e, char *out, size_t cap);
#endif
