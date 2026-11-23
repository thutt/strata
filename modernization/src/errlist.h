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
 * errlist.h - the diagnostics of a run as plain text, for a run that has no
 * document to put Print Error markers in (stred --preview, stred --print;
 * see perr.h for the Editor).  See docs/PRINT-LATEX.txt, section 17.
 */
#ifndef ERRLIST_H
#define ERRLIST_H
#include "diag.h"

/*
 * The diagnostics as plain text, one entry after another.  The caller
 * frees the result.
 */
char *
errlist_text(const Diag *v, int n, int dropped);
#endif
