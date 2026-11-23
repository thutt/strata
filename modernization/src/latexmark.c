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

/* latexmark.c - see latexmark.h and docs/LATEXMARKER-PICTURES.txt. */
#include "util.h"
#include "latexmark.h"
#include <string.h>


int
lmk_prepare(const char *raw, char *out, size_t cap)
{
    size_t n = 0;
    const char *p = raw;

    while (*p != '\0') {
        char c = *p;

        if (c == '\\' && p[1] != '\0') {
            /* A backslash keeps the next character: \% is not a comment. */
            if (n + 2 >= cap) {
                return -1;
            }

            out[n++] = c;
            out[n++] = p[1] == '\n' ? ' ' : p[1];
            p += 2;

            continue;
        }

        if (c == '%') {
            /* TeX ignores the rest of the line, its end and the spaces at
               the start of the next line. */
            while (*p != '\0' && *p != '\n') {
                p++;
            }

            while (*p == '\n' || *p == ' ' || *p == '\t') {
                p++;
            }

            continue;
        }

        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        }

        if (n + 1 >= cap) {
            return -1;
        }

        out[n++] = c;
        p++;
    }

    out[n] = '\0';

    while (n > 0 && out[n - 1] == ' ') {
        out[--n] = '\0';
    }

    size_t lead = strspn(out, " ");

    if (lead > 0) {
        memmove(out, out + lead, n - lead + 1);
        n -= lead;
    }

    return n > LMK_MAX ? -1 : isz(n);
}
