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
 * border.c - the record CB of a column, see border.h.
 */
#include "border.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int
border_get(int mask, int s)
{
    return (mask >> (s * 2)) & 3;
}


int
border_put(int mask, int s, int w)
{
    return (mask & ~(3 << (s * 2))) | ((w & 3) << (s * 2));
}


int
border_parse(const char *rec, int *flag)
{
    const char *comma;

    *flag = 1;

    if (rec == NULL) {
        return 0;
    }

    comma = strchr(rec, ',');

    if (comma != NULL) {
        *flag = atoi(comma + 1);
    }

    return atoi(rec) & 255;
}


void
border_format(int mask, int flag, char *out, size_t cap)
{
    snprintf(out, cap, "%d,%d", mask & 255, flag);
}


int
border_apply(int mask, const BorderSet *s, int col, int ncols)
{
    int first = col == 0;
    int last = col == ncols - 1;

    /*
     * A line between two columns is kept once, as the right side of the
     * column at its left, so that the screen and the page show one line
     * there: the left side of every column but the first is given up.
     */
    if (s->all >= 0) {
        for (int k = 0; k < 4; k++) {
            mask = border_put(mask, k, s->all);
        }

        if (first == 0) {
            mask = border_put(mask, BS_LEFT, BW_NONE);
        }
    }

    if (s->outside >= 0) {
        mask = border_put(mask, BS_TOP, s->outside);
        mask = border_put(mask, BS_BOTTOM, s->outside);

        if (first != 0) {
            mask = border_put(mask, BS_LEFT, s->outside);
        }

        if (last != 0) {
            mask = border_put(mask, BS_RIGHT, s->outside);
        }
    }

    if (s->inside >= 0) {
        if (first == 0) {
            mask = border_put(mask, BS_LEFT, BW_NONE);
        }

        if (last == 0) {
            mask = border_put(mask, BS_RIGHT, s->inside);
        }
    }

    if (s->top >= 0) {
        mask = border_put(mask, BS_TOP, s->top);
    }

    if (s->bottom >= 0) {
        mask = border_put(mask, BS_BOTTOM, s->bottom);
    }

    if (s->left >= 0 && first != 0) {
        mask = border_put(mask, BS_LEFT, s->left);
    }

    if (s->right >= 0 && last != 0) {
        mask = border_put(mask, BS_RIGHT, s->right);
    }

    return mask;
}
