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
 * library.h - the library: named, saved cut buffers that can be pasted into
 * any document.  The panels of F12, Tools, Library and of Lib Copy, Lib Cut
 * and Lib Paste (Ctrl-F3, Ctrl-F4 and Ctrl-F5).
 */
#ifndef LIBRARY_H
#define LIBRARY_H
#include "editor.h"
#include <stddef.h>
#include <stdint.h>

/*
 * The text editor of the panels that edit a library entry's contents: the
 * body records go in and the edited ones come out (see ui.c).  Returns 1
 * when the changes are kept.
 */
typedef int (*LibraryEdit)(const char *title, const uint8_t *in, size_t inlen,
                           uint8_t **out, size_t *outlen);


/* Tools, Library, Edit and Delete. */
void
library_edit(Editor *e, LibraryEdit edit);


void
library_delete(Editor *e);


/* Tools, Library, Save and Retrieve. */
void
library_save(Editor *e);


void
library_retrieve(Editor *e);


/*
 * The library of the setting initial_library, loaded when the Editor starts
 * (it replaces the entries that are there).  Searched as initial_file_path
 * (macro.h) says.  Returns the number of entries, or -1 with the reason in
 * why.
 */
int
library_load_initial(const char *dir, const char *name, char *why, size_t cap);


/*
 * Lib Copy and Lib Cut: the region has been copied (or cut) to the
 * clipboard of the editor; ask for the name and make an entry of it.
 */
void
library_add_clip(Editor *e);


/* Lib Paste. */
void
library_paste(Editor *e);


/* The number of entries, for the tests and the status line. */
int
library_count(void);
#endif
