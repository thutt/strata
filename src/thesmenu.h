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
 * The thesaurus panel of F12, Tools, Thesaurus and Ctrl-T.
 */
#ifndef THESMENU_H
#define THESMENU_H
#include "editor.h"

/*
 * Look up the word of the cursor.  The panel covers the lower half of the
 * screen, or the upper half when the cursor is in the lower half; the text
 * of the editor stays visible.  cursor_row is the screen row of the cursor.
 * Enter replaces the word in the document (the case of the old word is
 * kept), Esc leaves it as it was.  Without data a message shows what to install, and
 * with no word at the cursor a message shows so.  Returns 1 when the document was
 * changed.
 */
int
thesmenu_lookup(Editor *e, int cursor_row);
#endif
