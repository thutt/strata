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
 * logo.h - the large logo of Strata, the command \stratalogo, and the marks
 * of Logic Magicians Software, the commands \lms and \lmslogo.  Every LaTeX
 * file that the Printer makes defines the commands (logo_def_lines and
 * lms_def_lines).  The logo is made of letters, rules and one drawing made
 * with TikZ, so a file that uses it needs no other file.  The lines are in
 * logodata.c, which tests/logodata.py makes from the files
 * logo/stratalogo.def and logo/lmslogo.def.
 */
#ifndef LOGO_H
#define LOGO_H

#include <stddef.h>

/*
 * The definition of \stratalogo, one string per line (without the end of
 * the line), ends with NULL.
 */
extern const char *const logo_def_lines[];


/*
 * The definition of \lms and \lmslogo, one string per line (without the
 * end of the line), ends with NULL.
 */
extern const char *const lms_def_lines[];

#endif
