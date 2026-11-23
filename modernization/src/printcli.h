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
 * printcli.h - "stred --print": the same print as F12, Print, Final, without
 * the editor.
 *
 *     stred --print FILE.doc [--printer QUEUE] [--copies N]
 *           [--sides one|two] [--sort yes|no] [-o OUT.pdf]
 *           [--from SECTION] [--to SECTION] [--from-page N] [--to-page N]
 *           [--no-title] [--double-spaced] [--contents LEVEL] [--draft]
 *           [-v]
 *
 * The document is typeset to a PDF with strprint and, with --printer, sent
 * to that CUPS queue with lp (-v tells the pages on the standard error as
 * they are made).  The result is written as one line of text:
 * on the standard output when the PDF is made (and sent, when that was
 * asked), on the standard error otherwise.  The exit status is 0 for
 * success, 1 when no PDF was made, 2 for a wrong option or value, 3 when
 * the PDF was made but not sent, 4 when the print was stopped, and 5 when
 * the PDF was made (and sent, when that was asked) but the document has
 * errors or warnings: they follow on the standard error with the place of
 * each in the document.  --no-error-log leaves the Error Log out of the PDF;
 * the errors are told all the same.
 */
#ifndef PRINTCLI_H
#define PRINTCLI_H
#include "msdoc.h"

/*
 * The part of a document that a print covers: the section of the cursor
 * (current_only, with cur_item the item of the cursor), a range of sections
 * (from and to, either may be empty), or the whole document with the title
 * page (title != 0) or without it.  The part is released with
 * doc_part_free.  Returns 0, or -1 with a message in err.
 */
int
print_part(Doc *d, int cur_item, int current_only, int title,
           const char *from, const char *to, Doc *out, char *err,
           size_t errlen);


/* Run the mode.  argv[0] is the program; the options may be in any order. */
int
print_cli(int argc, char **argv);
#endif
