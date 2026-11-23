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
 * sysprog.h - finding the programs of the system that Strata runs.
 *
 * Every program that the Editor, the Document Manager, the Printer or the
 * Viewer starts (the TeX engines, ghostscript, lp, the spell checker, the
 * PDF viewer and the others) is looked for in a fixed list of system
 * folders and started by its full path with an argument vector.  The
 * environment variable PATH is never read, so a folder that another user
 * can write to, or a "." in PATH, cannot put a program of the same name in
 * the place of the real one.  See docs/EXEC-PATHS.txt.
 */
#ifndef SYSPROG_H
#define SYSPROG_H
#include <stddef.h>

/*
 * What a file is as a program to start: 1 for a regular file that the user
 * may run, that the user or root owns, that is not writable by its group or
 * by others and whose folder is not writable by others; -1 for a program
 * that is not safe to start; 0 for no program.
 */
int
sp_state(const char *path);

/*
 * Find the program name (a name without a slash) in the fixed folders:
 * /usr/bin, /bin, /usr/local/bin, /usr/sbin and the folders
 * /usr/local/texlive/YEAR/bin/SYSTEM of a TeX Live installation, the newest
 * year first.  Returns 0 and the full path in out, or -1 and an empty out.
 * *unsafe, when it is not NULL, is set to 1 when a program of that name was
 * found that is not safe to start (see sp_state).
 */
int
sp_find(const char *name, char *out, size_t cap, int *unsafe);

/*
 * Like sp_find, but a variable of the environment may name the program: when
 * envname is not NULL and the variable is set and not empty, its value is the
 * program.  It must be a full path of a regular file that can be run; a value
 * that is not is refused (-1), and the folders are not looked at.  The
 * variables are an explicit choice of the user, like the settings.
 */
int
sp_find_env(const char *envname, const char *name, char *out, size_t cap);

/* The folders in which sp_find looks, as one line for a message. */
void
sp_tried(char *out, size_t cap);

/*
 * Replace the list of folders (a NULL ended list of full paths), for the
 * tests; NULL puts the system list back.  The list is not copied.  A debug
 * or asan build also reads the variable STRATA_PROGRAM_DIRS (full paths
 * separated by colons) when no list was set; a release build never reads it.
 */
void
sp_dirs_set(const char *const *dirs);

/*
 * To be called in a child process just before the program is started: PATH
 * is set to the folder of the program followed by the system folders (the
 * programs that TeX runs, such as mktexpk, are found there and nowhere else)
 * and the variables that load another library into the program are removed.
 */
void
sp_child_env(const char *prog);

#endif
