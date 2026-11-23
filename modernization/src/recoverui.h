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
 * recoverui.h - open a document, and offer the part that can be read when the
 * file is cut short or damaged (see doc_recover in msdoc.h).
 */
#ifndef RECOVERUI_H
#define RECOVERUI_H
#include "msdoc.h"

/*
 * Load path as doc_load does.  When the loader refuses a file because it is
 * cut short or damaged after a readable beginning, a box (titled title) says
 * why, how many blocks and sections and how many bytes could be read, and
 * asks "Open the part that can be read?": Y says yes, N and Esc say no
 * (Enter does not answer).  Yes returns the part as a new document that has
 * the name doc_recovered_path gives and is marked as changed, with *blocks set
 * to the number of its blocks; the file is not changed.  No, and every file
 * that cannot be recovered, return NULL with err.  *blocks is -1 for a
 * document that was loaded whole.
 */
Doc *
recoverui_load(const char *path, char *err, size_t errlen, const char *title,
               int *blocks);


/* The text of the box, for the tests: reason, counts and the question. */
void
recoverui_text(const char *path, const DocRecovery *r, char *out, size_t cap);

#endif
