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
 * macro.h - macros: recorded keystrokes that run when Alt and a letter or a
 * digit are pressed.  The panels of F12, Tools, Macros.
 */
#ifndef MACRO_H
#define MACRO_H
#include "editor.h"
#include <stdint.h>

/* Tools, Macros: Edit, Delete, Learn, Save and Retrieve. */
void
macro_edit(Editor *e);


void
macro_delete(Editor *e);


void
macro_learn(Editor *e);


void
macro_save(Editor *e);


void
macro_retrieve(Editor *e);


/*
 * The macro set of the setting initial_macros, loaded when the Editor
 * starts (it replaces the macros that are there, as Retrieve with Overwrite
 * does).  name is searched as initial_file_path says, dir is the working
 * directory.  Returns the number of macros, or -1 with the reason in why.
 */
int
macro_load_initial(const char *dir, const char *name, char *why, size_t cap);


/* Where an initial macro set or library is: see macro.c. */
void
initial_file_path(const char *dir, const char *name, const char *ext,
                  char *out, size_t cap);


/*
 * Run the macro of the key k (Alt and a letter or a digit) by queueing its
 * keystrokes.  Returns 1 when there is such a macro, else 0.
 */
int
macro_play(int k);


/* The number of macros, for the status line and the tests. */
int
macro_count(void);


/*
 * The keystrokes as the bytes of a macro file, and back.  macro_encode
 * writes at most cap bytes and returns the number, macro_decode returns the
 * number of keys read.  The tests use them.
 */
int
macro_encode(const int *keys, int n, uint8_t *out, int cap);


int
macro_decode(const uint8_t *in, int n, int *keys, int cap);
#endif
