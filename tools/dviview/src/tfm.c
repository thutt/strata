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
 * Reading of TeX font metric files.
 */
#include <stdlib.h>

#include "dviview.h"


static double
fix_at(const unsigned char *p, long idx)
{
    long v = get_s(p + 4 * idx, 4);

    return dbl(v) / 1048576.0;
}


Tfm *
tfm_load(const char *path)
{
    size_t len;
    unsigned char *d = read_file(path, &len);
    Tfm *t;
    long lh;
    long nw;
    long nh;
    long nd;
    long n;
    long i;
    const unsigned char *ci;
    const unsigned char *wt;
    const unsigned char *ht;
    const unsigned char *dt;

    if (d == NULL) {
        return NULL;
    }

    if (len < 24 || to_long(len) < get_u(d, 2) * 4) {
        free(d);
        return NULL;
    }

    lh = get_u(d + 2, 2);
    t = xcalloc(1, sizeof *t);
    t->bc = get_u(d + 4, 2);
    t->ec = get_u(d + 6, 2);
    nw = get_u(d + 8, 2);
    nh = get_u(d + 10, 2);
    nd = get_u(d + 12, 2);
    n = t->ec - t->bc + 1;

    if (n < 0 || n > 256 || t->ec > 255 ||
        to_long(len) < 24 + 4 * (lh + n + nw + nh + nd)) {
        free(d);
        free(t);
        return NULL;
    }

    t->design = fix_at(d, 7) * 16.0;
    ci = d + 24 + 4 * lh;
    wt = ci + 4 * n;
    ht = wt + 4 * nw;
    dt = ht + 4 * nh;
    t->wd = xcalloc(256, sizeof *t->wd);
    t->ht = xcalloc(256, sizeof *t->ht);
    t->dp = xcalloc(256, sizeof *t->dp);

    for (i = 0; i < n; i++) {
        long wi = ci[4 * i];
        long hi = ci[4 * i + 1] / 16;
        long di = ci[4 * i + 1] % 16;

        if (wi < nw && hi < nh && di < nd) {
            t->wd[t->bc + i] = fix_at(wt, wi);
            t->ht[t->bc + i] = fix_at(ht, hi);
            t->dp[t->bc + i] = fix_at(dt, di);
        }
    }

    free(d);

    return t;
}
