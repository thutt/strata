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

/* Copyright (c) 2026 Logic Magicians Software */

/*
 * perr.h - the PRINT ERROR markers of the Editor.  After a Preview or a
 * Print the Editor reads the error file of the Printer (diag.h) and puts a
 * Print Error marker (marker type 40, see marker.h) into the document at
 * each position.  The user cannot make such a marker: only perr_apply does.
 * See docs/MARKERS.txt, section "Print Error", and docs/KEYSTROKES.txt.
 */
#ifndef PERR_H
#define PERR_H
#include "editor.h"

/* The run that found an error, as kept in the marker (record 3). */
#define PERR_RUN_PRINT 'P'
#define PERR_RUN_PREVIEW 'V'

/*
 * Replace the Print Error markers of a run by those of its diagnostics.  The
 * old markers of the same run, in the blocks of part (the whole document
 * when part is NULL), are removed first; the others are not touched.  The
 * items of the diagnostics are those of part and are made items of the
 * document of e.  The change is one change of the document: it marks the
 * document as changed.  Returns the number of markers inserted.
 */
int
perr_apply(Editor *e, int run, const Diag *v, int n, const Doc *part);


/* The number of Print Error markers in the document. */
int
perr_count(const Doc *d);


/* Remove every Print Error marker; returns how many were removed. */
int
perr_remove_all(Editor *e);


/* Remove the one under the cursor; returns 1, or 0 when there is none. */
int
perr_remove_here(Editor *e);


/*
 * Find the next (forward is 1) or the previous Print Error from the cursor
 * and move the cursor onto it.  The search wraps with a message at the end
 * (or at the start).  The message line shows the error text; "no print
 * errors" is told when there is none.  Returns 1 when the cursor moved.
 */
int
perr_find(Editor *e, int forward);


/* The Print Error under the cursor, or NULL when the cursor is not on one. */
const Tok *
perr_here(Editor *e);


/* The text of a Print Error marker: the message and the code. */
void
perr_text(const Tok *k, char *out, size_t cap);


/* Take the Print Error markers out of a block (a copy of it, say). */
void
perr_strip_block(Block *b);


/* Take the Print Error markers out of a text; returns how many. */
int
perr_strip_text(Text *t);
#endif
