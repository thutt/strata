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

/* errlist.c - the errors of a Preview or Print as plain text. */
#include "util.h"
#include "errlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


char *
errlist_text(const Diag *v, int n, int dropped)
{
    size_t cap = usz(n) * 700 + 200;
    char *out = malloc(cap);
    size_t at = 0;
    int errors = 0;
    int warnings = 0;

    if (out == NULL) {
        return NULL;
    }

    out[0] = '\0';

    for (int i = 0; i < n; i++) {
        char line[2 * DIAG_MSG + 400];

        diag_describe(&v[i], line, sizeof line);
        at += usz(snprintf(out + at, cap - at, "  %d. %s\n", i + 1, line));

        if (v[i].kind == DIAG_ERROR) {
            errors++;
        } else {
            warnings++;
        }
    }

    snprintf(out + at, cap - at, "%s%d error%s, %d warning%s%s\n",
             errors > 0 ? "PRINT FAILED: " : "", errors,
             errors == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s",
             dropped > 0 ? " (more were found than are listed)" : "");

    return out;
}
