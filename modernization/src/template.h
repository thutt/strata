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
 * template.h - the templates of new documents: a list of the files in the
 * template folders, each with its name and its description (the record MD).
 */
#ifndef TEMPLATE_H
#define TEMPLATE_H
#include <limits.h>
#include <stddef.h>
#include "msdoc.h"

#define TEMPLATE_MAX 200

typedef struct {
    char name[64]; /* the name of the file without .doc or .set */
    char path[PATH_MAX];
    char desc[DOC_MAX_DESC + 1]; /* empty when the file has none */
} Template;


/*
 * The folders in which templates are looked for, in this order: the folder
 * that STRATA_TEMPLATES names, the folder templates below the folder of the
 * settings ($STRATA_HOME or ~/.strata), and the folder templates of an
 * install (../share/strata/templates from the folder of the program).  Puts
 * at most cap folders in dirs (each of PATH_MAX characters) and returns how
 * many.
 */
int
template_dirs(char (*dirs)[PATH_MAX], int cap);


/*
 * The templates of the folders dirs (n of them), sorted by name; a name that
 * is in an earlier folder hides the same name of a later one.  The files are
 * those that end in .doc or .set (any case) and begin as a document.  At most
 * cap are put in v.  Returns how many.
 */
int
template_scan(const char (*dirs)[PATH_MAX], int n, Template *v, int cap);


/* template_scan of template_dirs. */
int
template_list(Template *v, int cap);


/*
 * Ask which template a new document is a copy of: a list of the templates
 * with the description of the chosen one below it (Up and Down, Enter takes
 * it, Esc takes the built-in settings).  The built-in settings are the first
 * two entries when builtin is 1 (Structured and Unstructured).  Returns the
 * number of the entry (the templates follow the built-in ones), -1 for Esc.
 * v and n are the templates (template_list).
 */
/* Look for a template file by name (as typed, or with .doc or .set added):
   in dir, then in the places of template_dirs, then in the folder of the
   running program.  A name with a slash is a path.  Returns 1 and the path
   in out, or 0. */
int
template_find(const char *name, const char *dir, char *out, size_t cap);


int
template_ask(const Template *v, int n, int builtin);


/* The list of the field Template: the built-in settings, the templates of
   the places with their descriptions, and a way to browse for a file.  The
   chosen entry replaces val (the words Structured or Unstructured, the path
   of a template); Esc leaves it.  It is a PICK_CALL function. */
void
template_pick(char *val, size_t cap, int arg);
#endif
