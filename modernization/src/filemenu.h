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
 * filemenu.h - the panels of the File menu: Save, Retrieve, New, Delete,
 * Import and Export, with the fields of their panels.
 */
#ifndef FILEMENU_H
#define FILEMENU_H
#include "editor.h"

/* The Save File panel (F12, File, Save). */
void
filemenu_save(Editor *e);


/*
 * Ctrl-S: save at once, under the name the document has.  A document
 * without a name gets the Save File panel.  Returns 0 when it was saved.
 */
int
filemenu_quick_save(Editor *e);


/* The same for an Autosave: it makes no numbered backup. */
int
filemenu_autosave(Editor *e);


void
filemenu_retrieve(Editor *e);


void
filemenu_new(Editor *e);


void
filemenu_delete(Editor *e);


void
filemenu_import(Editor *e);


void
filemenu_export(Editor *e);


/*
 * Does the document have changes that were not saved, and if so does the
 * user want to ignore them?  Returns 1 to go on.
 */
int
filemenu_ignore_changes(Editor *e);

/*
 * The working directory that the file panels share: the directory of the
 * document at first.  The panels of the macros and of the library use it
 * too.
 */
const char *
filemenu_workdir(const Editor *e);


void
filemenu_set_workdir(const char *dir);
#endif
