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

/* move.c - cursor movement. */
#include "util.h"
#include "editor.h"
#include <stdlib.h>
#include <ctype.h>


static int
text_last(const Text *t)
{
    return t->n == 0 ? 0 : (t->v[t->n - 1].kind == TK_NL ? t->n - 1 : t->n);
}


static int
prev_vis(Editor *e, int i)
{
    for (i--; i >= 0; i--) {
        if (is_edit_block(e->doc, i) != 0 && e->view.bv[i].visible != 0) {
            return i;
        }
    }

    return -1;
}


static int
next_vis(Editor *e, int i)
{
    for (i++; i < e->doc->nitems; i++) {
        if (is_edit_block(e->doc, i) != 0 && e->view.bv[i].visible != 0) {
            return i;
        }
    }

    return -1;
}


static void
to_end(Editor *e)
{
    Text *t = ed_text(e);

    e->cur.tok = t != NULL ? text_last(t) : 0;
}


static void
set_want(Editor *e)
{
    int x;

    if (ed_cursor_screen(e, &x, NULL) >= 0) {
        e->want_x = x;
    }
}


static int
isw(const Text *t, int i)
{
    return i >= 0 && i < t->n && t->v[i].kind == TK_CHAR &&
           (is_alnum(t->v[i].a) || t->v[i].a >= 0x80);
}


/* Does a row of a column have a cell of the picture of marker tk? */
static int
row_has_picture(const LRow *r, int tk)
{
    if (r->fmt == LF_TEXT) {
        return 0;
    }

    for (int i = 0; i < r->n; i++) {
        Cell c = lrow_cell(r, i, NULL);

        if ((c.attr & DA_EQ) != 0 && c.tok == tk) {
            return 1;
        }
    }

    return 0;
}


/*
 * A row under a picture is not a place for the cursor on its own.  Arriving
 * at one (row, going in direction dir), the cursor goes to the row of text
 * that goes with the picture of an inline equation, and stays on the
 * picture of a displayed one.
 */
static int
settle(const ColLayout *cl, int row, int dir)
{
    int tk = cl->rows[row].pic_tok;
    int r = row;

    if (tk < 0) {
        return row;
    }

    while (r >= 0 && r < cl->nrows && cl->rows[r].pic_tok >= 0) {
        r += dir;
    }

    if (r >= 0 && r < cl->nrows && row_has_picture(&cl->rows[r], tk)) {
        return r;
    }

    return row;
}


/*
 * The row to go to from row going one step in direction dir, or -1 when the
 * column has no such row.  The rows that only hold a picture are passed over
 * as one: Up and Down move from the text of an inline equation to the line
 * above or below, and from a displayed equation to the block before or after.
 */
static int
next_row(const ColLayout *cl, int row, int dir)
{
    int target = row + dir;

    while (target >= 0 && target < cl->nrows &&
           cl->rows[target].pic_tok >= 0) {
        int tk = cl->rows[target].pic_tok;

        if (cl->rows[row].pic_tok == tk ||
            row_has_picture(&cl->rows[row], tk)) {
            while (target >= 0 && target < cl->nrows &&
                   cl->rows[target].pic_tok >= 0) {
                target += dir;
            }
        } else {
            return settle(cl, target, dir);
        }
    }

    return target >= 0 && target < cl->nrows ? target : -1;
}


/*
 * The row that holds only the end of line mark that follows a displayed
 * picture: the picture fills its rows, so the mark of its line has a row of
 * its own below them.  It is part of the line of the picture and not a stop
 * of the cursor for Up and Down (Right and End still reach the mark).
 */
static int
pic_eol_row(const ColLayout *cl, int row)
{
    return row > 0 && row < cl->nrows && cl->rows[row].pic_tok < 0 &&
           cl->rows[row - 1].pic_tok >= 0 &&
           cl->rows[row].tok0 == cl->rows[row - 1].tok1 &&
           cl->rows[row].n <= 1;
}


/*
 * The cursor has just arrived at a place by Up or Down (dir).  A displayed
 * picture is one line of the text and one stop of the cursor, going down and
 * going up, so the cursor is shown on its first row: when it arrives going
 * up at the row of the end of line mark below the picture it is put on the
 * picture.  Page Up and Page Down may move it down the rows of a picture
 * that is taller than the window.
 */
static void
arrive(Editor *e, int dir)
{
    int lx;
    BView *bv = &e->view.bv[e->cur.item];
    ColLayout *cl = &bv->cl[e->cur.col];
    int row = layout_locate(cl, e->cur.tok, &lx);

    if (dir < 0 && row > 0 && pic_eol_row(cl, row) != 0) {
        e->cur.tok = cl->rows[row - 1].pic_tok;
        ed_fix_cursor(e);
    }

    ed_set_picture_row(e, 0);
}


/*
 * A displayed picture is one stop of the cursor, but one that is taller than
 * the window cannot be seen at once.  Page Up and Page Down then move the
 * cursor down and up the rows of the picture, a window at a time, until the
 * last or the first row is reached; only then do they leave the picture.
 * Returns 1 when the key was used up on the picture.
 */
static int
picture_page(Editor *e, int dir, int rows)
{
    int n = ed_picture_rows(e, NULL);
    int r = ed_picture_row(e);
    int step = rows > 2 ? rows - 1 : 1;

    if (n <= rows) {
        return 0;
    }

    if (dir > 0 && r < n - 1) {
        ed_set_picture_row(e, r + step > n - 1 ? n - 1 : r + step);
        e->glide = 1;

        return 1;
    }

    if (dir < 0 && r > 0) {
        ed_set_picture_row(e, r - step < 0 ? 0 : r - step);
        e->glide = 1;

        return 1;
    }

    return 0;
}


/*
 * Move the cursor one row up or down.  Inside a block the cursor stays in its
 * column and moves to the column's next row.  When the column has no more
 * rows the cursor goes to the next block in that direction: it takes the
 * column nearest to where the cursor was, and the first row of that column
 * going down or its last row going up.  This is how the cursor moves.
 */
static void
vertical_step(Editor *e, int dir)
{
    int x;
    int line = ed_cursor_screen(e, &x, NULL);

    if (line < 0) {
        return;
    }

    View *v = &e->view;
    BView *cb = &v->bv[e->cur.item];
    ColLayout *cl = &cb->cl[e->cur.col];
    int row = line - cb->line0;

    int target = next_row(cl, row, dir);
    int tok = -1;

    /* The mark below a picture is not a stop: go on to the next row. */
    if (dir > 0 && target >= 0 && cl->rows[row].pic_tok >= 0 &&
        pic_eol_row(cl, target) != 0) {
        target = next_row(cl, target, dir);
    }

    /*
     * A marker whose label goes over more than one row is one stop of the
     * cursor, shown where it starts.  When the row that is reached is a
     * later row of the marker that the cursor is on, the cursor would stay
     * where it is, so the next rows are tried until the cursor gets to
     * another row.
     */
    while (target >= 0) {
        int mx;
        int mrow;

        tok = layout_tok_at(cl, target, e->want_x - cb->colx[e->cur.col]);
        mrow = layout_locate(cl, tok, &mx);

        if ((dir > 0 && mrow > row) || (dir < 0 && mrow < row)) {
            break;
        }

        target = next_row(cl, target, dir);
    }

    if (target >= 0) {
        e->cur.tok = tok;
        ed_fix_cursor(e);
        arrive(e, dir);

        return;
    }

    int l = dir > 0 ? cb->line0 + cb->nrows : cb->line0 - 1;

    for (; l >= 0 && l < v->n; l += dir) {
        const VLine *vl = &v->lines[l];

        if (vl->kind != 0 || vl->item == e->cur.item) {
            continue;
        }

        const BView *bv = &v->bv[vl->item];
        int best = 0;
        int bd = 1 << 30;

        for (int c = 0; c < bv->ncl; c++) {
            int d = e->want_x < bv->colx[c] ?
                        bv->colx[c] - e->want_x :
                    e->want_x >= bv->colx[c] + bv->colw[c] ?
                        e->want_x - bv->colx[c] - bv->colw[c] + 1 :
                        0;

            if (d < bd) {
                bd = d;
                best = c;
            }
        }

        const ColLayout *tl = &bv->cl[best];
        int trow = dir > 0 ? 0 : tl->nrows - 1;

        if (trow < 0) {
            trow = 0;
        }

        trow = settle(tl, trow, dir);
        e->cur.item = vl->item;
        e->cur.col = best;
        e->cur.tok = layout_tok_at(tl, trow, e->want_x - bv->colx[best]);
        ed_fix_cursor(e);
        arrive(e, dir);

        return;
    }
}


/*
 * Move one row (see vertical_step).  A move onto a picture or off one makes
 * the window move by about the height of the picture; the drawing does that
 * in a few steps and not in one leap.
 */
static void
vertical(Editor *e, int dir)
{
    int was = ed_picture_rows(e, NULL) > 1;

    vertical_step(e, dir);

    if (was != 0 || ed_picture_rows(e, NULL) > 1) {
        e->glide = 1;
    }
}


static int *
sect_idx_list(Editor *e, int **lvl, int *n)
{
    Doc *d = e->doc;
    int cap = d->nitems + 1;
    int depth = 0;

    *n = 0;
    int *idx = malloc(usz(cap) * sizeof(int));

    *lvl = malloc(usz(cap) * sizeof(int));
    for (int i = 0; i < d->nitems; i++) {
        int t = d->items[i].type;

        if (t == IT_PLUS) {
            depth++;
        } else if (t == IT_MINUS) {
            depth--;
        } else if (t == IT_SECT && is_edit_block(d, i + 1) != 0 &&
                   e->view.bv[i + 1].visible != 0) {
            idx[*n] = i;
            (*lvl)[*n] = depth;
            (*n)++;
        }
    }

    return idx;
}


static void
goto_head(Editor *e, int s)
{
    e->cur.item = s + 1;
    e->cur.col = 0;
    e->cur.tok = 0;
    ed_fix_cursor(e);
}


void
ed_move(Editor *e, int mv, int rows)
{
    ed_layout(e);
    Doc *d = e->doc;
    /* the cursor must be in a visible block (folds can hide it): relocate
       first */

    if (is_edit_block(d, e->cur.item) != 0 &&
        e->view.bv[e->cur.item].visible == 0) {
        int f = prev_vis(e, e->cur.item);

        if (f < 0) {
            f = next_vis(e, e->cur.item);
        }

        if (f >= 0) {
            e->cur.item = f;
            e->cur.col = 0;
            e->cur.tok = 0;
        }
    }

    Text *t = ed_text(e);

    if (t == NULL) {
        ed_fix_cursor(e);
        return;
    }

    Pos *p = &e->cur;
    int keepx = 0;

    switch (mv) {
        case MV_LEFT:
            if (p->tok > 0) {
                p->tok--;
            } else if (p->col > 0) {
                p->col--;
                to_end(e);
            } else {
                int pv = prev_vis(e, p->item);

                if (pv >= 0) {
                    p->item = pv;
                    p->col = d->items[pv].blk->ncols - 1;
                    to_end(e);
                }
            }
            break;

        case MV_RIGHT:
            if (p->tok < text_last(t)) {
                p->tok++;
            } else if (p->col < d->items[p->item].blk->ncols - 1) {
                p->col++;
                p->tok = 0;
            } else {
                int nx = next_vis(e, p->item);

                if (nx >= 0) {
                    p->item = nx;
                    p->col = 0;
                    p->tok = 0;
                }
            }
            break;

        case MV_UP:
            if (picture_page(e, -1, rows) != 0) {
                return;
            }

            vertical(e, -1);
            keepx = 1;
            break;

        case MV_DOWN: {
            if (picture_page(e, 1, rows) != 0) {
                return;
            }

            Pos before = *p;
            int before_row = ed_picture_row(e);

            vertical(e, 1);

            /*
             * On the last line there is nothing below: go to its end.  The
             * cursor moved too when it went down a row of a picture.
             */
            if (p->item == before.item && p->col == before.col &&
                p->tok == before.tok && ed_picture_row(e) == before_row) {
                ed_move(e, MV_END, rows);

                return;
            }

            keepx = 1;
            break;
        }

        case MV_PGUP:
            if (picture_page(e, -1, rows) != 0) {
                return;
            }

            for (int i = 0; i < (rows > 3 ? rows / 2 : 1); i++) {
                vertical(e, -1);
            }
            keepx = 1;
            break;

        case MV_PGDN:
            if (picture_page(e, 1, rows) != 0) {
                return;
            }

            for (int i = 0; i < (rows > 3 ? rows / 2 : 1); i++) {
                vertical(e, 1);
            }
            keepx = 1;
            break;

        case MV_HOME:
        case MV_END: {
            int x;

            ed_cursor_screen(e, &x, NULL);
            BView *bv = &e->view.bv[p->item];
            int lx;
            int row = layout_locate(&bv->cl[p->col], p->tok, &lx);
            LRow *r = &bv->cl[p->col].rows[row];

            if (mv == MV_HOME) {
                p->tok = r->tok0;
            } else {
                p->tok = r->tok1 - 1;

                if (p->tok < r->tok0) {
                    p->tok = r->tok0;
                }
            }

            ed_fix_cursor(e);
            break;
        }

        case MV_WORD_LEFT:
            if (p->tok == 0) {
                ed_move(e, MV_LEFT, rows);
                return;
            }
            while (p->tok > 0 && isw(t, p->tok - 1) == 0) {
                p->tok--;
            }
            while (p->tok > 0 && isw(t, p->tok - 1) != 0) {
                p->tok--;
            }
            break;

        case MV_WORD_RIGHT:
            if (p->tok >= text_last(t)) {
                ed_move(e, MV_RIGHT, rows);
                return;
            }
            while (p->tok < text_last(t) && isw(t, p->tok) != 0) {
                p->tok++;
            }
            while (p->tok < text_last(t) && isw(t, p->tok) == 0) {
                p->tok++;
            }
            break;

        case MV_BLOCK_START:
            p->tok = 0;
            break;

        case MV_BLOCK_END:
            to_end(e);
            break;

        case MV_DOC_START:
            for (int i = 0; i < d->nitems; i++) {
                if (is_edit_block(d, i) != 0 && e->view.bv[i].visible != 0) {
                    p->item = i;
                    p->col = 0;
                    p->tok = 0;
                    break;
                }
            }
            break;

        case MV_DOC_END:
            for (int i = d->nitems - 1; i >= 0; i--) {
                if (is_edit_block(d, i) != 0 && e->view.bv[i].visible != 0) {
                    p->item = i;
                    p->col = d->items[i].blk->ncols - 1;
                    to_end(e);
                    break;
                }
            }
            break;

        case MV_COL_LEFT:
            if (p->col > 0) {
                p->col--;
                p->tok = 0;
            } else {
                ed_move(e, MV_LEVEL_UP, rows);
            }
            break;

        case MV_COL_RIGHT:
            if (p->col < d->items[p->item].blk->ncols - 1) {
                p->col++;
                p->tok = 0;
            } else {
                ed_move(e, MV_LEVEL_DOWN, rows);
            }
            break;

        case MV_SECT_HEAD: {
            int s = sect_start(d, p->item);

            /* already on the headline: previous section */
            if (s >= 0 && p->item == s + 1) {
                int *lv;
                int n;
                int *idx = sect_idx_list(e, &lv, &n);

                for (int i = 0; i < n; i++) {
                    if (idx[i] == s && i > 0) {
                        goto_head(e, idx[i - 1]);
                        break;
                    }
                }

                free(idx);
                free(lv);
            } else if (s >= 0) {
                goto_head(e, s);
            }

            break;
        }

        case MV_SECT_NEXT_HEAD: {
            int s = sect_start(d, p->item);
            int *lv;
            int n;
            int *idx = sect_idx_list(e, &lv, &n);

            for (int i = 0; i < n; i++) {
                if (idx[i] == s && i + 1 < n) {
                    goto_head(e, idx[i + 1]);
                    break;
                }
            }

            free(idx);
            free(lv);
            break;
        }

        case MV_SECT_PREV_SIB:
        case MV_SECT_NEXT_SIB: {
            int s = sect_start(d, p->item);
            int *lv;
            int n;
            int *idx = sect_idx_list(e, &lv, &n);
            int me = -1;

            for (int i = 0; i < n; i++) {
                if (idx[i] == s) {
                    me = i;
                }
            }

            if (me >= 0) {
                int dir = mv == MV_SECT_NEXT_SIB ? 1 : -1;

                for (int i = me + dir; i >= 0 && i < n; i += dir) {
                    if (lv[i] < lv[me]) {
                        break;
                    }

                    if (lv[i] == lv[me]) {
                        goto_head(e, idx[i]);
                        break;
                    }
                }
            }

            free(idx);
            free(lv);
            break;
        }

        case MV_LEVEL_UP: {
            int s = sect_start(d, p->item);
            int *lv;
            int n;
            int *idx = sect_idx_list(e, &lv, &n);

            for (int i = 0; i < n; i++) {
                if (idx[i] == s) {
                    for (int j = i - 1; j >= 0; j--) {
                        if (lv[j] < lv[i]) {
                            goto_head(e, idx[j]);
                            break;
                        }
                    }

                    break;
                }
            }

            free(idx);
            free(lv);
            break;
        }

        case MV_LEVEL_DOWN: {
            int s = sect_start(d, p->item);
            int *lv;
            int n;
            int *idx = sect_idx_list(e, &lv, &n);

            for (int i = 0; i < n; i++) {
                if (idx[i] == s) {
                    if (i + 1 < n && lv[i + 1] > lv[i]) {
                        goto_head(e, idx[i + 1]);
                    }

                    break;
                }
            }

            free(idx);
            free(lv);
            break;
        }

        default:
            break;
    }

    ed_fix_cursor(e);

    if (keepx == 0) {
        set_want(e);
    }
}
