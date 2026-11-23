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

/* tabcheck.c - verify that recomputed tab widths equal the stored ones.
 * Validates the layout model (stop - x cells; stored = cells - 1). */
#include "../src/util.h"
#include "../src/layout.h"
#include <stdio.h>
#include <stdlib.h>


int
main(int argc, char **argv)
{
    int tot = 0;
    int bad = 0;

    for (int a = 1; a < argc; a++) {
        Doc *d = doc_load(argv[a], NULL, 0);

        if (d == NULL) {
            continue;
        }

        for (int i = 0; i < d->nitems; i++) {
            if (d->items[i].type != IT_BLOCK) {
                continue;
            }

            Block *b = d->items[i].blk;

            for (int c = 0; c < b->ncols; c++) {
                Text *t = &b->cols[c].text;
                uint8_t *save = malloc(usz(t->n + 1));

                for (int k = 0; k < t->n; k++) {
                    save[k] = t->v[k].b;
                }

                Fmt f;

                fmt_resolve(d, i, c, &f);
                int w = b->cols[c].width != 0 ? b->cols[c].width : 78;
                ColLayout l;

                layout_column(&f, t, w, &l, 1);
                layout_free(&l);

                for (int k = 0; k < t->n; k++) {
                    if (t->v[k].kind == TK_TAB) {
                        tot++;

                        if (save[k] != t->v[k].b) {
                            if (bad < 30) {
                                int px = 0;

                                for (int q = k - 1;
                                     q >= 0 && t->v[q].kind != TK_NL; q--) {
                                    if (t->v[q].kind == TK_CHAR) {
                                        px++;
                                    }
                                }

                                printf("%s item "
                                       "%d col %d/%d w=%d prefix=%d tok %d: "
                                       "stored %d "
                                       "computed %d (tabs:",
                                       argv[a] + 30, i, c, b->ncols,
                                       b->cols[c].width, px, k, save[k],
                                       t->v[k].b);
                            }

                            if (bad < 30) {
                                for (int s = 0; s < f.ntab; s++) {
                                    printf(" %d%c", f.tabpos[s], f.tabtype[s]);
                                }

                                printf(" ind=%d "
                                       "bl=%d)\n",
                                       f.indent, f.bl);
                            }

                            bad++;
                        }
                    }
                }

                free(save);
            }
        }

        doc_free(d);
    }

    printf("tabs checked: %d, mismatches: %d\n", tot, bad);
    /* The sample documents of tests/samples have no mismatch: every tab
       marker holds the number of fill characters that the Editor
       computes. */

    return bad > 0;
}
