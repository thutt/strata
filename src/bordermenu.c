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
 * bordermenu.c - Border (Alt-F8): the Border Layout panel and the records of
 * the borders of the highlighted columns (see border.h):
 *
 *   CB<mask>,<flag>  on each column: the sides, and Connect Borders (1 or 0);
 *   CL1, CR1         the gap at the left and the right of a column that has a
 *                    border on that side;
 *   BT<n>, BB<n>     on each block: the Top and the Bottom Gutter, in
 *                    thousandths of an inch;
 *   BH<n>            on each block: the Shading, in percent.
 *
 * The highlight is the columns from the one where it began to the one where
 * the cursor is, in the blocks between the two.
 */
#include "util.h"
#include "bordermenu.h"
#include "border.h"
#include "marker.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    F_ALL,
    F_OUTSIDE,
    F_TOP,
    F_LEFT,
    F_INSIDE,
    F_RIGHT,
    F_BOTTOM,
    F_TGUT,
    F_BGUT,
    F_CONNECT,
    F_SHADE,
    F_HINT1,
    F_HINT2,
    F_COUNT
};

/*
 * The choice n/a comes first, so that the cursor keys reach None, Single,
 * Double and Wide from a field that shows no one value.  The weight of a
 * choice is its index less one.
 */
#define NA 0

static const char *const weights[] = {"n/a", "None", "Single", "Double",
                                      "Wide", NULL};
static const char *const yes_no[] = {"Yes", "No", NULL};
static const char *const yes_no_mixed[] = {"Yes", "No", "n/a", NULL};


/* The text of a record of a block: thousandths as 0.250. */
static void
gutter_text(const Block *b, const char *code, char *out, size_t cap)
{
    const char *v = props_get(&b->props, code, 0);

    marker_fmt_num(v != NULL ? atoi(v) : 0, 3, 0, out, cap);
}


static void
set_gutter(Block *b, const char *code, const char *text)
{
    int milli = 0;

    marker_parse_num(text, 3, &milli);

    if (milli <= 0) {
        props_remove(&b->props, code);
    } else {
        char rec[24];

        snprintf(rec, sizeof rec, "%d", milli);
        props_set(&b->props, code, 0, rec);
    }
}


/* Set a record of a column or take it away when val is NULL. */
static void
col_record(Column *c, const char *code, const char *val)
{
    if (val == NULL) {
        props_remove(&c->props, code);
    } else {
        props_set(&c->props, code, 0, val);
    }
}


/*
 * Merge value v into the running value *cur of a field over several blocks:
 * *n counts the values seen, and a value that differs from the first makes
 * the field mixed (*mixed is set to 1).
 */
static void
merge_int(int *cur, int *n, int *mixed, int v)
{
    if (*n == 0) {
        *cur = v;
    } else if (*cur != v) {
        *mixed = 1;
    }

    (*n)++;
}


/* The record text of a block as a number, 0 when the block has none. */
static int
block_num(const Block *b, const char *code)
{
    const char *v = props_get(&b->props, code, 0);

    return v != NULL ? atoi(v) : 0;
}


int
bordermenu_run(Editor *e)
{
    Doc *d = e->doc;
    int lo = e->sel.anchor.item < e->cur.item ? e->sel.anchor.item :
                                                e->cur.item;
    int hi = e->sel.anchor.item < e->cur.item ? e->cur.item :
                                                e->sel.anchor.item;
    int c0 = e->sel.anchor.col < e->cur.col ? e->sel.anchor.col : e->cur.col;
    int c1 = e->sel.anchor.col < e->cur.col ? e->cur.col : e->sel.anchor.col;
    WField f[F_COUNT];
    int init[F_COUNT];
    char init_t[F_COUNT][32];
    const Block *first = NULL;
    int ncols;
    int v_side[4] = {0, 0, 0, 0};
    int m_side[4] = {0, 0, 0, 0};
    int n_side[4] = {0, 0, 0, 0};
    int v_ins = 0;
    int m_ins = 0;
    int n_ins = 0;
    int v_conn = 0;
    int m_conn = 0;
    int n_conn = 0;
    int v_bt = 0;
    int m_bt = 0;
    int n_bt = 0;
    int v_bb = 0;
    int m_bb = 0;
    int n_bb = 0;
    int v_bh = 0;
    int m_bh = 0;
    int n_bh = 0;
    int shift;
    BorderSet s = {-1, -1, -1, -1, -1, -1, -1};
    int changed = 0;

    e->sel.kind = SEL_NONE;

    for (int i = lo; i <= hi && first == NULL; i++) {
        if (d->items[i].type == IT_BLOCK) {
            first = d->items[i].blk;
        }
    }

    if (first == NULL) {
        ed_msg(e, "There is no block to put a border on");

        return -1;
    }

    ncols = c1 - c0 + 1;

    if (c1 >= first->ncols) {
        c1 = first->ncols - 1;
        ncols = c1 - c0 + 1;
    }

    if (ncols < 1 || c0 < 0) {
        ed_msg(e, "There is no column to put a border on");

        return -1;
    }

    /*
     * Every block of the highlight shows its values: a field where the
     * blocks differ is mixed and is left at n/a (or empty), so that the
     * panel changes only what it is set to.
     */
    for (int i = lo; i <= hi; i++) {
        const Block *b;

        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        b = d->items[i].blk;

        if (c1 >= b->ncols) {
            continue;
        }

        for (int c = c0; c <= c1; c++) {
            int fl;
            int m = border_parse(props_get(&b->cols[c].props, "CB", 0), &fl);

            merge_int(&v_side[BS_TOP], &n_side[BS_TOP], &m_side[BS_TOP],
                      border_get(m, BS_TOP));
            merge_int(&v_side[BS_BOTTOM], &n_side[BS_BOTTOM],
                      &m_side[BS_BOTTOM], border_get(m, BS_BOTTOM));
            merge_int(&v_conn, &n_conn, &m_conn, fl);

            if (c == c0) {
                merge_int(&v_side[BS_LEFT], &n_side[BS_LEFT],
                          &m_side[BS_LEFT], border_get(m, BS_LEFT));
            }

            if (c == c1) {
                merge_int(&v_side[BS_RIGHT], &n_side[BS_RIGHT],
                          &m_side[BS_RIGHT], border_get(m, BS_RIGHT));
            } else {
                merge_int(&v_ins, &n_ins, &m_ins, border_get(m, BS_RIGHT));
            }
        }

        merge_int(&v_bt, &n_bt, &m_bt, block_num(b, "BT"));
        merge_int(&v_bb, &n_bb, &m_bb, block_num(b, "BB"));
        merge_int(&v_bh, &n_bh, &m_bh, block_num(b, "BH"));
    }

    memset(f, 0, sizeof f);

    for (int k = F_ALL; k <= F_BOTTOM; k++) {
        static const char *const lab[] = {"All", "Outside", "Top", "Left",
                                          "Inside", "Right", "Bottom"};

        f[k].label = lab[k];
        f[k].kind = WF_CHOICE;
        f[k].choices = weights;
        f[k].sel = NA;
    }

    f[F_TOP].sel = m_side[BS_TOP] != 0 ? NA : v_side[BS_TOP] + 1;
    f[F_BOTTOM].sel = m_side[BS_BOTTOM] != 0 ? NA : v_side[BS_BOTTOM] + 1;
    f[F_LEFT].sel = m_side[BS_LEFT] != 0 ? NA : v_side[BS_LEFT] + 1;
    f[F_RIGHT].sel = m_side[BS_RIGHT] != 0 ? NA : v_side[BS_RIGHT] + 1;

    if (c0 != c1) {
        f[F_INSIDE].sel = m_ins != 0 ? NA : v_ins + 1;
    }

    f[F_TGUT].label = "Top Gutter";
    f[F_TGUT].kind = WF_TEXT;

    if (m_bt == 0) {
        gutter_text(first, "BT", f[F_TGUT].val, sizeof f[F_TGUT].val);
    }

    f[F_BGUT].label = "Bottom Gutter";
    f[F_BGUT].kind = WF_TEXT;

    if (m_bb == 0) {
        gutter_text(first, "BB", f[F_BGUT].val, sizeof f[F_BGUT].val);
    }

    f[F_CONNECT].label = "Connect Borders?";
    f[F_CONNECT].kind = WF_CHOICE;
    f[F_CONNECT].choices = m_conn != 0 ? yes_no_mixed : yes_no;
    f[F_CONNECT].sel = m_conn != 0 ? 2 : (v_conn == 0 ? 1 : 0);
    f[F_SHADE].label = "Shading (%)";
    f[F_SHADE].kind = WF_TEXT;

    if (m_bh == 0) {
        snprintf(f[F_SHADE].val, sizeof f[F_SHADE].val, "%d", v_bh);
    }

    f[F_HINT1].label = "Ins";
    f[F_HINT1].kind = WF_INFO;
    str_copy(f[F_HINT1].val, sizeof f[F_HINT1].val,
             "uses the changed settings");
    f[F_HINT2].label = "Shift-Ins";
    f[F_HINT2].kind = WF_INFO;
    str_copy(f[F_HINT2].val, sizeof f[F_HINT2].val, "uses every setting");

    for (int k = 0; k < F_COUNT; k++) {
        init[k] = f[k].sel;
        str_copy(init_t[k], sizeof init_t[k], f[k].val);
    }

    for (;;) {
        int out;

        if (popup_form("Border Layout", f, F_COUNT, NULL) == 0) {
            ed_msg(e, "Cancelled");

            return -1;
        }

        if ((f[F_TGUT].val[0] != '\0' &&
             marker_parse_num(f[F_TGUT].val, 3, &out) != 0) ||
            (f[F_BGUT].val[0] != '\0' &&
             marker_parse_num(f[F_BGUT].val, 3, &out) != 0)) {
            popup_msg("Border", "Invalid number (no more than 3 decimal "
                                "places allowed)");
            continue;
        }

        if (atoi(f[F_SHADE].val) < 0 || atoi(f[F_SHADE].val) > 100) {
            popup_msg("Border", "Shading must be from 0 to 100");
            continue;
        }

        break;
    }

    shift = widgets_shift_ins;

    {
        int *const w[7] = {&s.all, &s.outside, &s.top, &s.left, &s.inside,
                           &s.right, &s.bottom};

        int group = 0;

        /*
         * A changed All, Outside or Inside is not undone by a side that was
         * not changed, even when Shift-Ins takes every field.
         */
        static const int groups[3] = {F_ALL, F_OUTSIDE, F_INSIDE};

        for (int g = 0; g < 3; g++) {
            int k = groups[g];

            if (f[k].sel != NA && f[k].sel != init[k]) {
                group = 1;
            }
        }

        for (int k = F_ALL; k <= F_BOTTOM; k++) {
            int side = k == F_TOP || k == F_LEFT || k == F_RIGHT ||
                       k == F_BOTTOM;
            int used = shift != 0 && (group == 0 || side == 0);

            if (f[k].sel != NA && (used != 0 || f[k].sel != init[k])) {
                *w[k] = f[k].sel - 1;
            }
        }
    }

    for (int i = lo; i <= hi; i++) {
        Block *b;
        int bflag = f[F_CONNECT].sel == 0 ? 1 : 0;
        int before = changed;

        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        b = d->items[i].blk;

        for (int c = c0; c <= c1 && c < b->ncols; c++) {
            int flag;
            int old = border_parse(props_get(&b->cols[c].props, "CB", 0),
                                   &flag);
            int mask = border_apply(old, &s, c - c0, ncols);
            char rec[24];

            if (f[F_CONNECT].sel != 2 &&
                (f[F_CONNECT].sel != init[F_CONNECT] || shift != 0)) {
                flag = bflag;
            }

            if (mask == 0) {
                col_record(&b->cols[c], "CB", NULL);
            } else {
                border_format(mask, flag, rec, sizeof rec);
                col_record(&b->cols[c], "CB", rec);
            }

            if (border_get(mask, BS_LEFT) != 0) {
                col_record(&b->cols[c], "CL", "1");
            } else if (border_get(old, BS_LEFT) != 0) {
                const char *cl = props_get(&b->cols[c].props, "CL", 0);

                if (cl != NULL && strcmp(cl, "1") == 0) {
                    col_record(&b->cols[c], "CL", NULL);
                }
            }

            if (border_get(mask, BS_RIGHT) != 0) {
                col_record(&b->cols[c], "CR", "1");
            } else if (border_get(old, BS_RIGHT) != 0) {
                const char *cr = props_get(&b->cols[c].props, "CR", 0);

                if (cr != NULL && strcmp(cr, "1") == 0) {
                    col_record(&b->cols[c], "CR", NULL);
                }
            }

            changed += mask != old;
        }

        if (f[F_TGUT].val[0] != '\0' &&
            (shift != 0 || strcmp(f[F_TGUT].val, init_t[F_TGUT]) != 0)) {
            set_gutter(b, "BT", f[F_TGUT].val);
            changed++;
        }

        if (f[F_BGUT].val[0] != '\0' &&
            (shift != 0 || strcmp(f[F_BGUT].val, init_t[F_BGUT]) != 0)) {
            set_gutter(b, "BB", f[F_BGUT].val);
            changed++;
        }

        if (f[F_SHADE].val[0] != '\0' &&
            (shift != 0 || strcmp(f[F_SHADE].val, init_t[F_SHADE]) != 0)) {
            if (atoi(f[F_SHADE].val) <= 0) {
                props_remove(&b->props, "BH");
            } else {
                props_set(&b->props, "BH", 0, f[F_SHADE].val);
            }

            changed++;
        }

        if (changed != before) {
            b->dirty = 1;
        }
    }

    if (changed != 0) {
        doc_touch(d);
        ed_invalidate(e);
    }

    ed_msg(e, "Border: %d change%s", changed, changed == 1 ? "" : "s");

    return 0;
}
