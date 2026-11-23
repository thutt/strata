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
 * pvest [-c CPI] [-l LPI] FILE.DOC ...  prints the number of pages that the
 * estimate of the preview gives for each document, one line "name pages"
 * each.  The options change the two constants of the model (the characters
 * per inch and the lines per inch, times 100); tests/pvmeasure.py uses them
 * to fit the model to the real page counts.  With -s no file is read: the
 * output of strprint --progress comes on the standard input and the program
 * prints a line "mark POS PAGES" for each position mark and a last line
 * "end PAGES" (the pages of the last pass), using the same reader as the
 * editor.
 */
#include "../src/util.h"
#include "../src/latex.h"
#include "../src/msdoc.h"
#include "../src/pvcore.h"
#include "../src/vars.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Read the progress of strprint from the standard input. */
static int
read_progress(void)
{
    PvProg p;
    char buf[4096];
    size_t n;
    int last = -1;
    int pages = 0;

    pv_prog_init(&p, 1);

    while ((n = fread(buf, 1, sizeof buf, stdin)) > 0) {
        size_t off = 0;

        /* One line at a time, so that every mark is seen. */
        while (off < n) {
            size_t k = off;

            while (k < n && buf[k] != '\n') {
                k++;
            }

            k = k < n ? k + 1 : k;
            pv_prog_feed(&p, buf + off, k - off);
            off = k;

            if (p.pos != last) {
                last = p.pos;

                if (p.pos >= 0) {
                    printf("mark %d %d\n", p.pos, p.pos_pages);
                }
            }

            pages = p.pages;
        }
    }

    printf("end %d\n", pages);

    return 0;
}


int
main(int argc, char **argv)
{
    int cpi = 0;
    int lpi = 0;
    int status = 0;

    for (int i = 1; i < argc; i++) {
        char err[128];
        Doc *d;

        if (strcmp(argv[i], "-s") == 0) {
            return read_progress();
        }

        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            cpi = atoi(argv[++i]);
            continue;
        }

        if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            lpi = atoi(argv[++i]);
            continue;
        }

        if (strcmp(argv[i], "--var") == 0 ||
            strcmp(argv[i], "--special") == 0) {
            /* A variable of the process, as the other programs take it. */
            if (vars_option(argc, argv, &i, err, sizeof err) != 1) {
                fprintf(stderr, "pvest: %s\n", err);

                return 2;
            }

            continue;
        }

        d = doc_load(argv[i], err, sizeof err);

        if (d == NULL) {
            fprintf(stderr, "pvest: %s: %s\n", argv[i], err);
            status = 1;
            continue;
        }

        printf("%s %d\n", argv[i],
               cpi > 0 && lpi > 0 ? latex_estimate_with(d, cpi, lpi) :
                                    latex_estimate_pages(d));
        doc_free(d);
    }

    return status;
}
