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

/* roundtrip.c - load each file, serialize, compare bytes. exit 1 on any diff */
#include "../src/util.h"
#include "../src/msdoc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int
main(int argc, char **argv)
{
    int bad = 0;

    for (int i = 1; i < argc; i++) {
        char err[128] = "";
        Doc *d = doc_load(argv[i], err, sizeof err);

        if (d == NULL) {
            printf("FAIL %-40s load: %s\n", argv[i], err);
            bad++;
            continue;
        }

        {
            int dp = 0;
            int neg = 0;

            for (int k = 0; k < d->nitems; k++) {
                if (d->items[k].type == IT_PLUS) {
                    dp++;
                }

                if (d->items[k].type == IT_MINUS && --dp < 0) {
                    neg = 1;
                }
            }

            if (dp != 0 || neg != 0) {
                printf("FAIL %-40s unbalanced +/- (depth %d)\n", argv[i], dp);
                bad++;
            }
        }
        size_t len;
        uint8_t *out = doc_serialize(d, &len);
        FILE *f = fopen(argv[i], "rb");

        fseek(f, 0, SEEK_END);
        size_t sz = lsz(ftell(f));

        fseek(f, 0, SEEK_SET);
        uint8_t *orig = malloc(sz);

        if (fread(orig, 1, sz, f) != sz) {
            printf("FAIL %-40s read error\n", argv[i]);
            bad++;
        }

        fclose(f);

        /* A file of the older version 2 is written with the current one. */
        if (sz > 3 && orig[0] == '*' && orig[1] == 'R' && orig[2] == '2' &&
            (orig[3] < '0' || orig[3] > '9')) {
            orig[2] = (uint8_t)('0' + STRATA_FILE_FORMAT);
        }

        if (len != sz || memcmp(orig, out, sz) != 0) {
            size_t k = 0;

            while (k < sz && k < len && orig[k] == out[k]) {
                k++;
            }

            printf("FAIL %-40s size %zu vs %zu, first diff at %zu\n", argv[i],
                   sz, len, k);
            bad++;
        } else {
            printf("ok   %-40s %zu bytes, %d items\n", argv[i], sz,
                   d->nitems);
        }

        free(orig);
        free(out);
        doc_free(d);
    }

    return bad != 0 ? 1 : 0;
}
