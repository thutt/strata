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
 * spellui.h - the panels of the spell check: F12, Tools, Spell (Document,
 * Block, Section, Word), Ctrl-C and stred --spell (docs/SPELL.txt).
 */
#ifndef SPELLUI_H
#define SPELLUI_H
#include "editor.h"

/*
 * The Check Document Spelling panel and the check of the items lo to hi of
 * the document of the editor.  The panel has the settings of the check (Auto
 * Correct?) and Ins starts it.
 */
void
spellui_check(Editor *e, int lo, int hi);


/* Spell the word at the cursor (Tools, Spell, Word and Ctrl-C). */
void
spellui_word(Editor *e);


/*
 * Check a whole document file and offer to save it, as the Document Manager
 * does (stred --spell).  Returns 0, or 1 when the file could not be read.
 */
int
spellui_file(const char *path, int autocorrect);
#endif
