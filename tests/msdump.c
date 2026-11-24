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

/* msdump.c - print the structure of a .DOC file */
#include "../src/util.h"
#include "../src/msdoc.h"
#include <stdio.h>
#include <string.h>


int
main(int argc, char **argv)
{
    char err[128];

    if (argc < 2) {
        fprintf(stderr, "usage: msdump file.doc\n");
        return 2;
    }

    Doc *d = doc_load(argv[1], err, sizeof err);

    if (d == NULL) {
        fprintf(stderr, "%s: %s\n", argv[1], err);
        return 1;
    }

    printf("header records: %d, structured=%d, items=%d\n", d->header.n,
           d->structured, d->nitems);
    int lvl = 0;

    for (int i = 0; i < d->nitems; i++) {
        Item *it = &d->items[i];

        if (it->type == IT_PLUS) {
            lvl++;
            printf("%*s+\n", lvl * 2, "");
            continue;
        }

        if (it->type == IT_MINUS) {
            printf("%*s-\n", lvl * 2, "");
            lvl--;
            continue;
        }

        if (it->type == IT_SECT) {
            printf("%*sSECTION\n", lvl * 2 + 1, "");
            continue;
        }

        Block *b = it->blk;

        printf("%*s[block cols=%d", lvl * 2 + 3, "", b->ncols);

        for (int p = 0; p < b->props.n; p++) {
            printf(" %s", b->props.v[p].s);
        }

        printf("]");

        for (int c = 0; c < b->ncols; c++) {
            char buf[60];

            text_plain(&b->cols[c].text, buf, sizeof buf);
            printf(" |%s|", buf);
        }

        printf("\n");
    }

    return 0;
}
