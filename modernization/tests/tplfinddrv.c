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
 * tplfinddrv.c - a program for tests/tplfolders.py: it calls template_find
 * for each name of its arguments, with the working directory DIR, and prints
 * the path that was found, or the word NOTFOUND, one line for each name.
 *
 *     tplfinddrv DIR NAME...
 *
 * The test builds it with src/tplfind.c, or with a spoiled copy of that file.
 */
#include "../src/template.h"
#include <limits.h>
#include <stdio.h>


int
main(int argc, char **argv)
{
    int i;

    if (argc < 3) {
        fprintf(stderr, "usage: tplfinddrv DIR NAME...\n");

        return 2;
    }

    for (i = 2; i < argc; i++) {
        char out[PATH_MAX];

        if (template_find(argv[i], argv[1], out, sizeof out) != 0) {
            printf("%s\n", out);
        } else {
            printf("NOTFOUND\n");
        }
    }

    return 0;
}
