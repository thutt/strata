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
 * readfile.h - read a whole file into memory, safely.  The file may be
 * damaged or made by someone who wants the program to fail, so the size is
 * checked before any memory is made for it.
 */
#ifndef READFILE_H
#define READFILE_H

#include <stddef.h>
#include <stdint.h>


/*
 * Read the regular file path, which must be at most limit bytes long, into
 * memory made with malloc.  One NUL byte follows the data, which is len
 * bytes long.  The open never waits for a named pipe, and a directory, a
 * device or a pipe is refused.  On failure NULL is returned and err holds
 * a message that a person can read (it names the limit in megabytes when
 * the file is too large).
 */
uint8_t *
readfile_all(const char *path, size_t limit, size_t *len, char *err,
             size_t errlen);


/* The same, as a string of characters. */
char *
readfile_text(const char *path, size_t limit, size_t *len, char *err,
              size_t errlen);


#endif
