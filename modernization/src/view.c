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

/* view.c - build the laid-out view of the document and draw it. */
#include "util.h"
#include "editor.h"
#include "marker.h"
#include "border.h"
#include "term.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


static void
bview_free(BView *bv)
{
    if (bv->cl != NULL) {
        for (int c = 0; c < bv->ncl; c++) {
            if (bv->cl[c].rows != NULL) {
                layout_free(&bv->cl[c]);
            }
        }

        free(bv->cl);
        free(bv->colx); /* Also holds colw and sepx. */
    }

    memset(bv, 0, sizeof *bv);
}


static void
view_free_contents(View *v)
{
    v->n = 0;

    for (int i = 0; i < v->nbv; i++) {
        bview_free(&v->bv[i]);
    }
}


void
ed_invalidate(Editor *e)
{
    e->view.valid = 0;
    e->view.all_stale = 1;
    e->view.markers_ok = 0;
    e->view.partial = 0;
}


void
ed_invalidate_block(Editor *e)
{
    View *v = &e->view;
    const Doc *d = e->doc;

    v->valid = 0;

    /* The text of a cross-reference may be the headline that is changed. */
    if (marker_prepared_none(d) == 0 && e->cur.item > 0 &&
        e->cur.item < d->nitems && d->items[e->cur.item - 1].type == IT_SECT) {
        v->all_stale = 1;

        return;
    }

    v->markers_ok = marker_prepared_none(d);
    v->partial = 1;

    if (e->cur.item >= 0 && e->cur.item < v->nbv) {
        v->bv[e->cur.item].stale = 1;
    } else {
        v->all_stale = 1;
    }
}


/* The block at item i, if there is one, must be laid out again. */
static void
mark_stale(View *v, int i)
{
    if (i >= 0 && i < v->nbv) {
        v->bv[i].stale = 1;
    }
}


void
ed_view_inserted(Editor *e, int at, int n)
{
    View *v = &e->view;
    const Doc *d = e->doc;

    if (v->bv == NULL || v->nbv != d->nitems - n || at < 0 || at > v->nbv ||
        marker_prepared_none(d) == 0) {
        ed_invalidate(e);

        return;
    }

    v->bv = realloc(v->bv, usz(d->nitems + 1) * sizeof(BView));
    memmove(&v->bv[at + n], &v->bv[at], usz(v->nbv - at) * sizeof(BView));
    memset(&v->bv[at], 0, usz(n) * sizeof(BView));
    v->nbv = d->nitems;

    /* The blocks around the new ones may have become a headline, or have
       been one. */
    mark_stale(v, at - 1);
    mark_stale(v, at + n);
    mark_stale(v, e->cur.item);
    v->valid = 0;
    v->markers_ok = 1;
    v->partial = 1;
}


void
ed_view_deleted(Editor *e, int at, int n)
{
    View *v = &e->view;
    const Doc *d = e->doc;

    if (v->bv == NULL || v->nbv != d->nitems + n || at < 0 ||
        at + n > v->nbv || marker_prepared_none(d) == 0) {
        ed_invalidate(e);

        return;
    }

    for (int i = at; i < at + n; i++) {
        bview_free(&v->bv[i]);
    }

    memmove(&v->bv[at], &v->bv[at + n], usz(v->nbv - at - n) * sizeof(BView));
    v->nbv = d->nitems;
    memset(&v->bv[v->nbv], 0, usz(n) * sizeof(BView));
    mark_stale(v, at - 1);
    mark_stale(v, at);
    mark_stale(v, e->cur.item);
    v->valid = 0;
    v->markers_ok = 1;
    v->partial = 1;
}


void
ed_view_free(Editor *e)
{
    view_free_contents(&e->view);
    free(e->view.bv);
    free(e->view.lines);
    memset(&e->view, 0, sizeof e->view);
}


static VLine *
push_line(View *v, int kind, int item, int row)
{
    if (v->n == v->cap) {
        v->cap = v->cap != 0 ? v->cap * 2 : 256;
        v->lines = realloc(v->lines, usz(v->cap) * sizeof(VLine));
    }

    VLine *l = &v->lines[v->n++];

    l->kind = kind;
    l->item = item;
    l->row = row;

    return l;
}


int
view_line_nseg(const Editor *e, int li)
{
    const VLine *l = &e->view.lines[li];

    return l->kind == 0 ? e->view.bv[l->item].ncl : 0;
}


void
view_line_seg(const Editor *e, int li, int s, int *col, int *x0, int *x1)
{
    const VLine *l = &e->view.lines[li];
    const BView *bv = &e->view.bv[l->item];

    *col = s;
    *x0 = bv->colx[s];
    *x1 = bv->colx[s] + bv->colw[s];
}


/* ------------------------------------------------------ the borders */
/*
 * The borders of the columns (record CB, see border.h) are drawn into the
 * cells that the layout leaves free, so that no text moves: the left and the
 * right side in the gap cell at each side of a column, the top and the bottom
 * on the separator row above and below the block.  Single is a thin line,
 * double a double line, wide a double line in bold (the code page has no
 * heavier line).
 */
static int
col_border(const Editor *e, int item, int c)
{
    const Block *b;
    int flag;

    if (item < 0 || item >= e->doc->nitems ||
        e->doc->items[item].type != IT_BLOCK) {
        return 0;
    }

    b = e->doc->items[item].blk;

    return c < b->ncols ? border_parse(props_get(&b->cols[c].props, "CB", 0),
                                       &flag) :
                          0;
}


/* A cell of a line of weight w; wide is bold. */
static void
bput(Cell *cell, int ch, int w)
{
    cell->ch = (uint8_t)ch;
    cell->attr = w == BW_WIDE ? DA_BOLD : 0;
}


static int
vchar(int w)
{
    return w == BW_SINGLE ? 0xB3 : 0xBA;
}


/*
 * The character where lines meet: which of the four directions have a line
 * (bits JU, JD, JL and JR), the kind of the vertical lines (1 single, 2
 * double) and the kind of the horizontal ones.  Code page 437 has the
 * characters for every combination of presence when the vertical lines are
 * of one kind and the horizontal lines are of one kind: single and single,
 * double and double, single horizontal with double vertical, and double
 * horizontal with single vertical.  When the two ends of an axis are of
 * different kinds (a single line joined to a double one) the code page has
 * no character, and the heavier kind is taken for the whole axis.
 */
enum {
    JU = 1,
    JD = 2,
    JL = 4,
    JR = 8
};

/* Indexed by [vertical kind - 1][horizontal kind - 1][JU|JD|JL|JR]. */
static const uint8_t join_cp437[2][2][16] = {
    {{0, 0xB3, 0xB3, 0xB3, 0xC4, 0xD9, 0xBF, 0xB4, 0xC4, 0xC0, 0xDA, 0xC3,
      0xC4, 0xC1, 0xC2, 0xC5},
     {0, 0xB3, 0xB3, 0xB3, 0xCD, 0xBE, 0xB8, 0xB5, 0xCD, 0xD4, 0xD5, 0xC6,
      0xCD, 0xCF, 0xD1, 0xD8}},
    {{0, 0xBA, 0xBA, 0xBA, 0xC4, 0xBD, 0xB7, 0xB6, 0xC4, 0xD3, 0xD6, 0xC7,
      0xC4, 0xD0, 0xD2, 0xD7},
     {0, 0xBA, 0xBA, 0xBA, 0xCD, 0xBC, 0xBB, 0xB9, 0xCD, 0xC8, 0xC9, 0xCC,
      0xCD, 0xCA, 0xCB, 0xCE}}};


/* The weight of a line as a kind: single is 1, double and wide are 2. */
static int
kind_of(int w)
{
    return w == BW_SINGLE ? 1 : 2;
}


/* The character for the directions of a cell and the weights of its lines. */
static int
join_char(int dirs, int vw, int hw)
{
    int vk = vw == BW_NONE ? 1 : kind_of(vw);
    int hk = hw == BW_NONE ? 1 : kind_of(hw);

    return join_cp437[vk - 1][hk - 1][dirs & 15];
}


/* Does a column of the block have a border at side s? */
static int
block_has_side(const Block *b, int s)
{
    int flag;

    for (int c = 0; c < b->ncols; c++) {
        if (border_get(border_parse(props_get(&b->cols[c].props, "CB", 0),
                                    &flag), s) != BW_NONE) {
            return 1;
        }
    }

    return 0;
}


/* The sides of the columns on a text row of a block. */
static void
border_text_row(const Editor *e, int item, Cell *out)
{
    const View *v = &e->view;
    const BView *bv = &v->bv[item];

    for (int c = 0; c < bv->ncl; c++) {
        int m = col_border(e, item, c);
        int wl = border_get(m, BS_LEFT);
        int wr = border_get(m, BS_RIGHT);
        int xl = bv->colx[c] - 1;
        int xr = bv->colx[c] + bv->colw[c];

        if (wl != BW_NONE && xl >= 0 && xl < v->width) {
            bput(&out[xl], vchar(wl), wl);
        }

        if (wr != BW_NONE && xr >= 0 && xr < v->width) {
            bput(&out[xr], vchar(wr), wr);
        }
    }
}


/*
 * The lines of one block on a row of its own or a separator row.  Its top
 * (up = 1, the block is below the row) or bottom (up = 0, the block is above
 * it) border goes into hw[x], the weight of the horizontal line at x.  The
 * left and right borders of its columns go into vw[x], the weight of the
 * vertical line that comes to the row from the block.
 */
static void
border_line(const Editor *e, int item, int up, int *hw, int *vw)
{
    const View *v = &e->view;
    const BView *bv;

    if (item < 0 || item >= e->doc->nitems ||
        e->doc->items[item].type != IT_BLOCK) {
        return;
    }

    bv = &v->bv[item];

    for (int c = 0; c < bv->ncl; c++) {
        int m = col_border(e, item, c);
        int w = border_get(m, up != 0 ? BS_TOP : BS_BOTTOM);
        int wl = border_get(m, BS_LEFT);
        int wr = border_get(m, BS_RIGHT);
        int xl = bv->colx[c] - 1;
        int xr = bv->colx[c] + bv->colw[c];

        if (wl != BW_NONE && xl >= 0 && xl < v->width) {
            vw[xl] = vw[xl] > wl ? vw[xl] : wl;
        }

        if (wr != BW_NONE && xr >= 0 && xr < v->width) {
            vw[xr] = vw[xr] > wr ? vw[xr] : wr;
        }

        if (w == BW_NONE) {
            continue;
        }

        for (int x = xl < 0 ? 0 : xl; x <= xr && x < v->width; x++) {
            hw[x] = hw[x] > w ? hw[x] : w;
        }
    }
}


/*
 * The borders on a row between two blocks: the bottom of block bot_item,
 * which is above the row, and the top of block top_item, which is below it
 * (either may be -1).  Each cell takes its character from the lines that
 * meet there: the horizontal line to its left and right, and the vertical
 * lines of the sides of the block above and of the block below.  So the
 * corners, the tees and the crossings are drawn wherever lines meet.
 *
 * The row may already carry a rule that runs from the left offset loffs to
 * the right edge (rule is its weight, BW_NONE for no rule).  The rule is
 * the horizontal line of every cell that has no border line of a block, so
 * a vertical line that meets it makes a tee or a cross, and a vertical line
 * that ends at it a corner or a tee, as a border line does; where a block has
 * a border line of its own, that line is the line of the cell.  So a
 * bordered column next to one without borders makes a cross at the seam when
 * the rule goes on to the right of it, and a frame that is narrower than the
 * row ends in a tee where the rule goes on.  A cell between two bordered
 * columns stays clear.
 */
static void
border_row(const Editor *e, int bot_item, int top_item, int rule, int loffs,
           Cell *out)
{
    const View *v = &e->view;
    int w = v->width;
    int hwu[512];
    int hwb[512];
    int hh[512];
    int hz[512];
    int vu[512];
    int vd[512];

    if (w > 512) {
        return;
    }

    memset(hwu, 0, sizeof hwu);
    memset(hwb, 0, sizeof hwb);
    memset(vu, 0, sizeof vu);
    memset(vd, 0, sizeof vd);

    border_line(e, bot_item, 0, hwb, vu);
    border_line(e, top_item, 1, hwu, vd);

    for (int x = 0; x < w; x++) {
        hh[x] = hwb[x] > hwu[x] ? hwb[x] : hwu[x];
    }

    /* The horizontal line of each cell, and whether the cell is a gap. */
    for (int x = 0; x < w; x++) {
        int gap = hh[x] == BW_NONE && x > 0 && x + 1 < w &&
                  hh[x - 1] != BW_NONE && hh[x + 1] != BW_NONE;

        if (hh[x] != BW_NONE) {
            hz[x] = hh[x];
        } else if (gap) {
            hz[x] = BW_NONE;
        } else if (rule != BW_NONE &&
                   (x >= loffs ||
                    (x + 1 == loffs &&
                     (vu[x] != BW_NONE || vd[x] != BW_NONE)))) {
            /* A vertical line just left of the rule is the end of it. */
            hz[x] = rule;
        } else {
            hz[x] = BW_NONE;
        }
    }

    for (int x = 0; x < w; x++) {
        int h = hz[x];
        int lw = 0;
        int rw = 0;
        int dirs = 0;
        int vw = vu[x] > vd[x] ? vu[x] : vd[x];
        int hl;

        if (h == BW_NONE) {
            /* No horizontal line here: a vertical line goes through. */
            if (vu[x] != BW_NONE && vd[x] != BW_NONE) {
                bput(&out[x], join_char(JU | JD, vw, BW_NONE), vw);
            } else if (hh[x] == BW_NONE && x > 0 && x + 1 < w &&
                       hh[x - 1] != BW_NONE && hh[x + 1] != BW_NONE) {
                /* The cell between the lines of two columns is clear, as
                   it is above and below the block. */
                out[x].ch = ' ';
                out[x].attr = 0;
            }

            continue;
        }

        if (hh[x] == BW_NONE && vu[x] == BW_NONE && vd[x] == BW_NONE) {
            /* The plain rule goes on as it was drawn. */
            continue;
        }

        if (x > 0 && hz[x - 1] != BW_NONE) {
            lw = hz[x - 1] < h ? hz[x - 1] : h;
        }

        if (x + 1 < w && hz[x + 1] != BW_NONE) {
            rw = hz[x + 1] < h ? hz[x + 1] : h;
        }

        dirs |= vu[x] != BW_NONE ? JU : 0;
        dirs |= vd[x] != BW_NONE ? JD : 0;
        dirs |= lw != BW_NONE ? JL : 0;
        dirs |= rw != BW_NONE ? JR : 0;
        hl = lw > rw ? lw : rw;

        if (hl == BW_NONE) {
            hl = h;
        }

        bput(&out[x], join_char(dirs, vw, hl),
             hl == BW_WIDE || vw == BW_WIDE ? BW_WIDE : hl);
    }
}



void
view_line_cells(Editor *e, int li, Cell *out)
{
    View *v = &e->view;
    const VLine *l = &v->lines[li];
    const BView *bv = l->item >= 0 ? &v->bv[l->item] : NULL;
    int w = v->width;

    for (int i = 0; i < w; i++) {
        out[i].ch = ' ';
        out[i].attr = 0;
        out[i].tok = -1;
    }

    if (bv == NULL) {
        return;
    }

    if (l->kind == 3) {
        /* A row of its own for a border above the first block (row 0) or
           below the last one (row 1). */
        border_row(e, l->row == 1 ? l->item : -1, l->row == 0 ? l->item : -1,
                   BW_NONE, 0, out);

        return;
    }

    if (l->kind != 0) {
        uint8_t ch = l->kind == 2 ? 0xCD : 0xC4;
        int loffs = bv->loffs;

        for (int k = loffs; k < w; k++) {
            out[k].ch = ch;
            out[k].attr = DA_DIM;
        }

        border_row(e, li > 0 && v->lines[li - 1].kind == 0 ?
                          v->lines[li - 1].item : -1,
                   l->item, l->kind == 2 ? BW_DOUBLE : BW_SINGLE, loffs, out);

        if (l->kind == 2) {
            int fl = e->doc->items[l->item - 1].fold;

            if (fl != 0) {
                const char *t = fl == 2 ? " + " : fl == 3 ? " + " : " * ";

                for (int k = 0; t[k] != '\0'; k++) {
                    out[loffs + 2 + k].ch = u8(t[k]);
                    out[loffs + 2 + k].attr = DA_BOLD;
                }
            }
        }

        return;
    }

    for (int c = 0; c < bv->ncl; c++) {
        if (c > 0 && bv->sepx[c] >= 0 && bv->sepx[c] < w) {
            out[bv->sepx[c]].ch = ':';
            out[bv->sepx[c]].attr = DA_DIM;
        }

        if (l->row >= bv->cl[c].nrows) {
            continue;
        }

        const LRow *row = &bv->cl[c].rows[l->row];
        const Text *tx = &e->doc->items[l->item].blk->cols[c].text;

        for (int k = 0; k < row->n; k++) {
            int px = bv->colx[c] + row->x0 + k;

            if (px >= w || px >= bv->colx[c] + bv->colw[c]) {
                break;
            }

            out[px] = lrow_cell(row, k, tx);
        }
    }

    border_text_row(e, l->item, out);
}


/* Visibility according to per-section fold state. */
static void
compute_visibility(Editor *e, uint8_t *vis)
{
    Doc *d = e->doc;
    int n = d->nitems;

    memset(vis, 1, usz(n));
    int i = 0;

    while (i < n) {
        if (d->items[i].type == IT_SECT) {
            int fold = d->items[i].fold;
            int be = sect_blocks_end(d, i);

            for (int k = i + 1; k < be; k++) {
                vis[k] = (k == i + 1) || fold == 0 || fold == 3;
            }

            if (be < n && d->items[be].type == IT_PLUS &&
                (fold == 2 || fold == 3)) {
                int depth = 0;
                int m = be;

                for (; m < n; m++) {
                    vis[m] = 0;

                    if (d->items[m].type == IT_PLUS) {
                        depth++;
                    } else if (d->items[m].type == IT_MINUS && --depth == 0) {
                        break;
                    }
                }

                i = m + 1;
                continue;
            }

            i = be;
        } else {
            i++;
        }
    }

    int lo;
    int hi;

    if (e->hide_title != 0 && doc_title_range(d, &lo, &hi) != 0) {
        memset(vis + lo, 0, usz(hi - lo));
    }

    if (e->only_s >= 0 && e->only_s < n) {
        int end = sect_end(d, e->only_s);

        memset(vis, 0, usz(e->only_s));
        memset(vis + end, 0, usz(n - end));
    }
}


/* Lay out block i: the columns, their widths and their rows. */
static void
layout_block(Editor *e, int i, int vis)
{
    View *v = &e->view;
    Doc *d = e->doc;
    Block *b = d->items[i].blk;
    BView *bv = &v->bv[i];

    bview_free(bv);
    bv->visible = vis != 0 && b->ncols > 0;

    if (bv->visible == 0) {
        return;
    }

    int level = d->structured != 0 ? doc_item_level(d, i) : 0;
    /* no FI record: levels are not indented */
    int loffs = level > 1 ? doc_level_indent(d, level) : 0;

    if (loffs > v->width - 24) {
        loffs = v->width - 24 > 0 ? v->width - 24 : 0;
    }

    int avail = v->width - loffs;
    int nc = b->ncols;

    bv->loffs = loffs;
    bv->ncl = nc;
    bv->cl = calloc(usz(nc), sizeof(ColLayout));
    bv->colx = calloc(usz(nc) * 3, sizeof(int));
    bv->colw = bv->colx + nc;
    bv->sepx = bv->colx + 2 * nc;
    int slotx = loffs;

    for (int c = 0; c < nc; c++) {
        /* a column occupies CL + CW + CR cells (left gap, text, right gap);
         * a separator
         * cell follows.  Checked against sample tables. */
        const char *sl = props_get(&b->cols[c].props, "CL", 0);
        const char *sr = props_get(&b->cols[c].props, "CR", 0);
        int gl = sl != NULL ? atoi(sl) : 0;
        int gr = sr != NULL ? atoi(sr) : 0;

        if (gl < 0 || gl > 9) {
            gl = 0;
        }

        if (gr < 0 || gr > 9) {
            gr = 0;
        }

        int w;
        int slot;
        /*
         * A column with an explicit width keeps it, even when the table
         * is wider than the screen.  Such a table is clipped at the
         * right edge instead of squeezing the columns.
         */
        int fixed = c < nc - 1 && b->cols[c].width > 0;

        if (fixed != 0) {
            w = b->cols[c].width;
            slot = w + gl + gr;
        } else if (c < nc - 1) {
            slot = (avail - (nc - 1)) / nc;
            w = slot - gl - gr;
        } else {
            slot = v->width - slotx;
            w = slot - gl - gr;
        }

        if (fixed == 0 && c < nc - 1 && slotx + slot + 1 > v->width - 6) {
            slot = v->width - slotx - 7;
            w = slot - gl - gr;
        }

        if (fixed == 0 && w < 6) {
            w = 6;
            slot = w + gl + gr;
        }

        bv->sepx[c] = slotx - 1;
        bv->colx[c] = slotx + gl;
        bv->colw[c] = w;
        slotx += slot + 1;
        Fmt f;

        fmt_resolve(d, i, c, &f);
        layout_column(&f, &b->cols[c].text, w, &bv->cl[c], 0);
    }

    int nrows = 0;

    for (int c = 0; c < nc; c++) {
        if (bv->cl[c].nrows > nrows) {
            nrows = bv->cl[c].nrows;
        }
    }

    bv->nrows = nrows;
}


void
ed_layout(Editor *e)
{
    View *v = &e->view;
    Doc *d = e->doc;

    if (v->valid != 0) {
        return;
    }

    marker_set_doc(d);

    /* An edit that changed no counter and no cross-reference leaves the
       numbering as it was, and there is none to make in the document. */
    if (v->markers_ok == 0 || marker_prepared_none(d) == 0) {
        marker_prepare(d);
    }

    v->markers_ok = 0;

    if (v->nbv != d->nitems) {
        /* The items changed: the layouts do not belong to them any more. */
        view_free_contents(v);
        v->bv = realloc(v->bv, usz(d->nitems + 1) * sizeof(BView));
        memset(v->bv, 0, usz(d->nitems + 1) * sizeof(BView));
        v->nbv = d->nitems;
        v->all_stale = 1;
    }

    if (v->width != e->text_width) {
        v->width = e->text_width;
        v->all_stale = 1;
    }

    uint8_t *vis = malloc(usz(d->nitems + 1));

    compute_visibility(e, vis);
    doc_levels_begin(d);
    int first = 1;
    int last_vis = -1;

    v->n = 0;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        BView *bv = &v->bv[i];
        int want = vis[i] != '\0' && d->items[i].blk->ncols > 0;

        if (v->all_stale != 0 || bv->stale != 0 || bv->visible != want ||
            (want != 0 && bv->cl == NULL)) {
            layout_block(e, i, vis[i]);
        }

        if (bv->visible == 0) {
            continue;
        }

        if (first == 0) {
            int sect = (i > 0 && d->items[i - 1].type == IT_SECT);

            push_line(v, sect != 0 ? 2 : 1, i, 0);
        } else if (block_has_side(d->items[i].blk, BS_TOP) != 0) {
            push_line(v, 3, i, 0); /* the top border of the first block */
        }

        first = 0;
        last_vis = i;
        bv->line0 = v->n;

        for (int r = 0; r < bv->nrows; r++) {
            push_line(v, 0, i, r);
        }
    }

    if (last_vis >= 0 &&
        block_has_side(d->items[last_vis].blk, BS_BOTTOM) != 0) {
        push_line(v, 3, last_vis, 1); /* the bottom border of the last one */
    }

    doc_levels_end();
    free(vis);
    v->all_stale = 0;
    v->valid = 1;
}


int
ed_picture_rows(Editor *e, int *first)
{
    ed_layout(e);
    const Pos *p = &e->cur;

    if (p->item < 0 || p->item >= e->view.nbv) {
        return 0;
    }

    const BView *bv = &e->view.bv[p->item];

    if (bv->visible == 0 || p->col >= bv->ncl || bv->cl[p->col].rows == NULL) {
        return 0;
    }

    const ColLayout *cl = &bv->cl[p->col];
    int lx;
    int row = layout_locate(cl, p->tok, &lx);

    if (row < 0 || row >= cl->nrows) {
        return 0;
    }

    /* A row of a displayed picture holds the picture and nothing else. */
    const LRow *w = &cl->rows[row];

    if (w->pic_tok != p->tok || w->n == 0 || lrow_tok(w, 0) != p->tok) {
        return 0;
    }

    /*
     * Only a Picture marker (type 25) is walked row by row: it is as tall as
     * a screen, and the equation of a displayed block (type 9) is a few
     * rows that are passed over as one.
     */
    const Block *blk = e->doc->items[p->item].blk;

    if (blk == NULL || p->col >= blk->ncols ||
        p->tok >= blk->cols[p->col].text.n ||
        tok_mtype(&blk->cols[p->col].text.v[p->tok]) != 25) {
        return 0;
    }

    int n = 0;

    for (int r = row; r < cl->nrows && cl->rows[r].pic_tok == p->tok; r++) {
        n++;
    }

    if (first != NULL) {
        *first = row;
    }

    return n;
}


int
ed_picture_row(Editor *e)
{
    int n = ed_picture_rows(e, NULL);

    if (n < 2 || e->prow_at.item != e->cur.item ||
        e->prow_at.col != e->cur.col || e->prow_at.tok != e->cur.tok) {
        return 0;
    }

    return e->prow < 0 ? 0 : (e->prow > n - 1 ? n - 1 : e->prow);
}


void
ed_set_picture_row(Editor *e, int r)
{
    int n = ed_picture_rows(e, NULL);

    e->prow = r < 0 || n < 2 ? 0 : (r > n - 1 ? n - 1 : r);
    e->prow_at = e->cur;
}


int
ed_cursor_screen(Editor *e, int *x, int *y)
{
    ed_layout(e);
    Pos *p = &e->cur;

    if (p->item < 0 || p->item >= e->view.nbv) {
        return -1;
    }

    BView *bv = &e->view.bv[p->item];

    if (bv->visible == 0 || p->col >= bv->ncl || bv->cl[p->col].rows == NULL) {
        return -1;
    }

    int lx;
    int row = layout_locate(&bv->cl[p->col], p->tok, &lx);
    int px = bv->colx[p->col] + lx;
    int maxx = bv->colx[p->col] + bv->colw[p->col] - 1;

    if (px > maxx) {
        px = maxx;
    }

    /* A column beyond the right edge of the screen shows its cursor there. */
    if (px > e->view.width - 1) {
        px = e->view.width - 1;
    }

    if (x != NULL) {
        *x = px;
    }

    /* On a displayed picture the cursor is on one row of it. */
    row += ed_picture_row(e);

    if (y != NULL) {
        *y = bv->line0 + row;
    }

    return bv->line0 + row;
}


int
ed_pos_at(Editor *e, int line, int x, Pos *out)
{
    ed_layout(e);
    View *v = &e->view;

    if (line < 0 || line >= v->n) {
        return 0;
    }

    const VLine *l = &v->lines[line];

    if (l->kind != 0 || l->item < 0 || l->item >= v->nbv) {
        return 0;
    }

    const BView *bv = &v->bv[l->item];

    if (bv->visible == 0 || bv->ncl < 1) {
        return 0;
    }

    int best = 0;
    int bd = 1 << 30;

    for (int c = 0; c < bv->ncl; c++) {
        int d = x < bv->colx[c] ? bv->colx[c] - x :
                x >= bv->colx[c] + bv->colw[c] ?
                    x - bv->colx[c] - bv->colw[c] + 1 :
                    0;

        if (d < bd) {
            bd = d;
            best = c;
        }
    }

    const ColLayout *cl = &bv->cl[best];

    if (cl->rows == NULL || cl->nrows < 1) {
        return 0;
    }

    out->item = l->item;
    out->col = best;

    if (l->row >= cl->nrows) {
        /* Below the text of a column that has fewer rows than its block: the
           end of the text. */
        out->tok = layout_tok_at(cl, cl->nrows - 1, 1 << 20);

        return 1;
    }

    out->tok = layout_tok_at(cl, l->row, x - bv->colx[best]);

    return 1;
}


void
ed_scroll_to_cursor(Editor *e, int rows)
{
    int line = ed_cursor_screen(e, NULL, NULL);

    if (line < 0) {
        return;
    }

    if (rows < 1) {
        rows = 1;
    }

    int margin = rows > 8 ? 2 : 0;

    if (line < e->top + margin) {
        e->top = line - margin;
    }

    if (line > e->top + rows - 1 - margin) {
        e->top = line - rows + 1 + margin;
    }

    if (e->top < 0) {
        e->top = 0;
    }

    if (e->top > e->view.n - 1) {
        e->top = e->view.n > 0 ? e->view.n - 1 : 0;
    }
}


static int
sel_item_range(Editor *e, int *lo, int *hi)
{
    if (e->sel.kind == SEL_BLOCK || e->sel.kind == SEL_SECTION) {
        ed_sel_range(e, lo, hi);
        return 1;
    }

    return 0;
}


/* The pictures already given to the terminal in this drawing. */
static int (*eq_done)[2];
static int eq_ndone;
static int eq_cap;


/* The text of the column that cell x of line li belongs to, or NULL. */
static const Text *
line_text(Editor *e, int li, int x, int *colp)
{
    const VLine *l = &e->view.lines[li];

    if (l->item < 0 || l->item >= e->doc->nitems ||
        e->doc->items[l->item].blk == NULL) {
        return NULL;
    }

    for (int s = 0; s < view_line_nseg(e, li); s++) {
        int col;
        int x0;
        int x1;

        view_line_seg(e, li, s, &col, &x0, &x1);

        if (x >= x0 && x < x1) {
            if (colp != NULL) {
                *colp = col;
            }

            return &e->doc->items[l->item].blk->cols[col].text;
        }
    }

    return NULL;
}


/*
 * A cell under the picture of an equation: find the first cell of the
 * picture (the picture may begin above the top of the window) and give it
 * to the terminal, once per picture.
 */
static void
draw_equation(Editor *e, int li, const Cell *cells, int x, int x0, int y0)
{
    View *v = &e->view;
    int tok = cells[x].tok;
    int top = li;
    int left = x;
    Cell *up = malloc(usz(v->width) * sizeof(Cell));

    while (top > 0 && v->lines[top - 1].kind == 0) {
        view_line_cells(e, top - 1, up);

        if (x >= v->width || (up[x].attr & DA_EQ) == 0 || up[x].tok != tok) {
            break;
        }

        top--;
    }

    free(up);

    while (left > 0 && (cells[left - 1].attr & DA_EQ) != 0 &&
           cells[left - 1].tok == tok) {
        left--;
    }

    for (int i = 0; i < eq_ndone; i++) {
        if (eq_done[i][0] == top && eq_done[i][1] == tok) {
            return;
        }
    }

    if (eq_ndone == eq_cap) {
        int cap = eq_cap == 0 ? 64 : eq_cap * 2;
        int(*grown)[2] = realloc(eq_done, usz(cap) * sizeof *grown);

        if (grown == NULL) {
            return;
        }

        eq_done = grown;
        eq_cap = cap;
    }

    eq_done[eq_ndone][0] = top;
    eq_done[eq_ndone][1] = tok;
    eq_ndone++;

    int col = -1;
    const Text *t = line_text(e, li, x, &col);

    if (t == NULL || layout_eq_image == NULL) {
        return;
    }

    int iw;
    int ih;
    int xo;
    int yo;
    const uint8_t *rgb = layout_eq_image(t, tok, eqn_is_display(t, tok), &iw,
                                         &ih, &xo, &yo);

    if (rgb != NULL) {
        term_image(x0 + left, y0 + top - e->top, xo, yo, rgb, iw, ih);

        /* The cursor surrounds the whole picture of the equation it is on. */
        if (v->lines[li].item == e->cur.item && col == e->cur.col &&
            tok == e->cur.tok) {
            term_cursor_box(x0 + left, y0 + top - e->top, xo, yo, iw, ih);
        }
    }
}



/*
 * Is the cell x of a row one of the two letters of the name LaTeX that the
 * X11 version draws raised or lowered?  1 for the A (the label has the
 * letters L, a, T, e and X one after another, all cells of one marker), 2
 * for the E, 0 for any other cell.
 */
static int
logo_letter(const Cell *cells, int x, int w)
{
    const Cell *c = &cells[x];

    if (c->ch == 'a' && x > 0 && x + 2 < w && cells[x - 1].ch == 'L' &&
        cells[x + 1].ch == 'T' && cells[x + 2].ch == 'e' &&
        cells[x - 1].tok == c->tok && cells[x + 1].tok == c->tok &&
        cells[x + 2].tok == c->tok) {
        return 1;
    }

    if (c->ch == 'e' && x > 2 && x + 1 < w && cells[x - 1].ch == 'T' &&
        cells[x - 2].ch == 'a' && cells[x - 3].ch == 'L' &&
        cells[x + 1].ch == 'X' && cells[x - 3].tok == c->tok &&
        cells[x + 1].tok == c->tok) {
        return 2;
    }

    return 0;
}


void
ed_draw(Editor *e, int x0, int y0, int w, int h)
{
    ed_layout(e);
    eq_ndone = 0;
    View *v = &e->view;
    int lo = 0;
    int hi = -1;
    int range = sel_item_range(e, &lo, &hi);
    int ta = 0;
    int tb = -1;

    int rlo = 0;
    int rhi = -1;
    int rc0 = 0;
    int rc1 = -1;

    if (e->sel.kind == SEL_COLUMN) {
        /*
         * The highlight of a column is the rectangle from the place where
         * it began to the place of the cursor: the columns between the two,
         * in the blocks between the two.
         */
        rlo = e->sel.anchor.item < e->cur.item ? e->sel.anchor.item :
                                                 e->cur.item;
        rhi = e->sel.anchor.item < e->cur.item ? e->cur.item :
                                                 e->sel.anchor.item;
        rc0 = e->sel.anchor.col < e->cur.col ? e->sel.anchor.col :
                                               e->cur.col;
        rc1 = e->sel.anchor.col < e->cur.col ? e->cur.col :
                                               e->sel.anchor.col;
    } else if (e->sel.kind == SEL_TEXT && e->sel.anchor.item == e->cur.item &&
               e->sel.anchor.col == e->cur.col) {
        ta = e->sel.anchor.tok;
        tb = e->cur.tok;

        if (ta > tb) {
            int t = ta;

            ta = tb;
            tb = t;
        }

        tb--; /* inclusive */
    }

    Cell *cells = malloc(usz(v->width) * sizeof(Cell));

    for (int y = 0; y < h; y++) {
        int li = e->top + y;

        if (li >= v->n) {
            continue;
        }

        const VLine *l = &v->lines[li];
        int inrange = range != 0 && l->item >= lo && l->item <= hi;

        view_line_cells(e, li, cells);

        for (int x = 0; x < w && x < v->width; x++) {
            const Cell *c = &cells[x];
            int attr = c->attr;

            if (inrange != 0 && l->kind == 0) {
                attr |= TA_REVERSE;
            }

            if (l->kind == 0 && ta <= tb && l->item == e->cur.item &&
                c->tok >= ta && c->tok <= tb) {
                for (int s = 0; s < view_line_nseg(e, li); s++) {
                    int col;
                    int sx0;
                    int sx1;

                    view_line_seg(e, li, s, &col, &sx0, &sx1);

                    if (col == e->cur.col && x >= sx0 && x < sx1) {
                        attr |= TA_REVERSE;
                    }
                }
            }

            if (l->kind == 0 && l->item >= rlo && l->item <= rhi) {
                for (int s = 0; s < view_line_nseg(e, li); s++) {
                    int col;
                    int sx0;
                    int sx1;

                    view_line_seg(e, li, s, &col, &sx0, &sx1);

                    if (col >= rc0 && col <= rc1 && x >= sx0 && x < sx1) {
                        attr |= TA_REVERSE;
                    }
                }
            }

            if ((c->attr & DA_EQ) != 0 && l->kind == 0) {
                term_putu(x0 + x, y0 + y, TERM_IMAGE_CELL, 0);
                draw_equation(e, li, cells, x, x0, y0);
                continue;
            }

            if (l->kind == 0 && (c->attr & (DA_MARKER | DA_KEY)) != 0 &&
                c->tok >= 0 &&
                layout_eq_want != NULL) {
                const Text *t = line_text(e, li, x, NULL);

                if (t != NULL && c->tok < t->n &&
                    t->v[c->tok].kind == TK_MARKER) {
                    int mt = tok_mtype(&t->v[c->tok]);

                    if (mt == 9 || mt == 25 || mt == 37 || mt == MT_LATEX) {
                        layout_eq_want(t, c->tok,
                                       eqn_is_display(t, c->tok));
                    }
                }
            }

            /* The X11 version draws the name LaTeX in the label of a LaTeX
               marker as TeX does: a smaller raised A and a lowered E. */
            if (l->kind == 0 && (c->attr & DA_MARKER) != 0 && c->tok >= 0 &&
                term_is_gui() != 0) {
                int lg = logo_letter(cells, x, v->width);

                if (lg != 0) {
                    const Text *t = line_text(e, li, x, NULL);

                    if (t != NULL && c->tok < t->n &&
                        t->v[c->tok].kind == TK_MARKER &&
                        tok_mtype(&t->v[c->tok]) == MT_LATEX) {
                        term_putu(x0 + x, y0 + y,
                                  lg == 1 ? TERM_LOGO_A : TERM_LOGO_E, attr);

                        continue;
                    }
                }
            }

            term_put(x0 + x, y0 + y, c->ch, attr);
        }
    }

    free(cells);
}


static const char *
attr_name(int a)
{
    static char b[96];

    b[0] = 0;

    if (a == 0 || a == (FLAG_STATE << 8)) {
        return "Normal";
    }

    if ((a & 1) != 0) {
        strcat(b, "Bold ");
    }

    if ((a & 4) != 0) {
        strcat(b, "Italic ");
    }

    /* The flags of the token are in the bits from 0x100 up. */
    if ((a & ATTR_UNDERW) != 0 ||
        ((a >> 8) & (FLAG_UNDER_ALL | FLAG_DUNDER_W | FLAG_DUNDER_ALL)) != 0) {
        strcat(b, "Under ");
    }

    if ((a & ATTR_SUPER) != 0) {
        strcat(b, "Super ");
    }

    if ((a & ATTR_SUB) != 0) {
        strcat(b, "Sub ");
    }

    if (b[0] == '\0') {
        strcpy(b, "Special");
    }

    return b;
}


static const char *
just_name(int j)
{
    static const char *n[] = {"Left", "Right", "Center", "Even"};

    return n[j & 3];
}


static const char *
ind_name(int i)
{
    static const char *n[] = {"None", "First", "All but First", "All"};

    return n[i & 3];
}


/* The number of entries in a row of the key strip. */
#define KEY_STRIP_COLS 6

/*
 * The labels of the function keys F1 to F12 in the four modes.  An empty
 * label stands for a key that has no command.
 */
static const char *const key_labels_normal[12] = {
    "Help", "Split", "Copy", "Cut", "Paste", "Format",
    "Move", "Table", "Search", "", "", "Menu"};
static const char *const key_labels_ctrl[12] = {
    "Learn", "Math", "Lib Copy", "Lib Cut", "Lib Paste", "Case",
    "Prev Error", "Next Error", "Again", "Undo", "Redo", "Marker"};
static const char *const key_labels_alt[12] = {
    "", "Append", "Join", "Delete", "Overtype", "Set Attr",
    "Go To", "Border", "Replace", "", "", "Marker"};
static const char *const key_labels_none[12] = {"", "", "", "", "", "",
                                                "", "", "", "", "", ""};


/*
 * Work out the first column of each of the six entries of a row.  The
 * entry of a column is "Fn: label" for the two keys that share the column,
 * in all the modes given; its width is the widest of them.  The entries are
 * separated by one blank at least; when 80 columns leave room, the room is
 * shared, one blank at a time, from the left.
 */
static void
key_strip_columns(const char *const *const *modes, int nmodes,
                  int xs[KEY_STRIP_COLS])
{
    int width[KEY_STRIP_COLS];
    int total = 0;

    for (int i = 0; i < KEY_STRIP_COLS; i++) {
        width[i] = 0;

        for (int m = 0; m < nmodes; m++) {
            for (int k = 0; k < 2; k++) {
                int f = 2 * i + k;
                int n = snprintf(NULL, 0, "F%d: %s", f + 1, modes[m][f]);

                if (n > width[i]) {
                    width[i] = n;
                }
            }
        }

        total += width[i];
    }

    int spare = 80 - total - (KEY_STRIP_COLS - 1);
    int x = 0;

    for (int i = 0; i < KEY_STRIP_COLS; i++) {
        xs[i] = x;
        x += width[i] + 1;

        if (i < spare) {
            x++;
        }
    }
}


void
ed_status_at(Editor *e, int w, int y0, int strip)
{
    Doc *d = e->doc;
    int R = term_rows();
    char line[512];
    char num[40];

    sect_number(d, e->cur.item, num, sizeof num);
    const char *nm = d->path != NULL ? d->path : "(new)";
    const char *base = strrchr(nm, '/');

    base = base != NULL ? base + 1 : nm;
    int sx = 0;

    ed_cursor_screen(e, &sx, NULL);
    int pos = 1;

    if (is_edit_block(d, e->cur.item) != 0) {
        ed_layout(e);
        BView *bv = &e->view.bv[e->cur.item];

        if (bv->visible != 0) {
            pos = sx - bv->colx[e->cur.col] + 1;

            if (pos < 1) {
                pos = 1;
            }
        }
    }

    /*
     * A long name is shown by its end, with the extension, so that the
     * fields after it keep their place; a name is padded to twelve cells
     * and is counted in cells, not in bytes.
     */
    char shown[300];
    char pad[16] = "            ";
    const char *mode = e->mode != NULL    ? e->mode :
                       e->only_s >= 0     ? "SECTION EDIT" :
                       d->structured != 0 ? "STRUCTURED EDIT" :
                                            "UNSTRUCTURED EDIT";

    /*
     * The room of the name is what the line has left: the width, less the
     * mode at the right edge and a blank before it, less the fields that
     * follow the name (the star, Attr, Pos, the section number and the
     * overtype flag).  A name is shown whole when it fits in that room.
     */
    int fixed = 2 + 7 + 12 + 6 + 3 + 1;

    if (d->structured != 0) {
        int nlen = isz(strlen(num));

        fixed += 6 + (nlen > 6 ? nlen : 6);
    }

    if (e->overtype != 0) {
        fixed += 4;
    }

    int room = w - slen(mode) - 2 - fixed;

    if (room < 12) {
        room = 12;
    }

    name_tail(base, room < 60 ? room : 60, shown, sizeof shown);
    pad[swidth(shown) < 12 ? 12 - swidth(shown) : 0] = '\0';

    snprintf(line, sizeof line, " %s%s%s Attr: %-12s Pos: %-3d %s%-6s %s%s",
             d->modified != 0 ? "*" : " ", shown, pad,
             attr_name(ed_attr_effective(e) | (ed_flags_at(e) << 8)), pos,
             d->structured != 0 ? "Sect: " : "", d->structured != 0 ? num : "",
             e->overtype != 0 ? "OVR " : "", "");
    int attr = TS_STATUS;
    char msgfit[256];

    /* A long path in the message is shortened, not the end of the message. */
    fit_message(e->msg, w - 2, msgfit, sizeof msgfit);
    term_fill(0, y0, w, 1, ' ', attr);
    term_puts(0, y0, line, attr);
    term_puts(w - slen(mode) - 1, y0, mode, attr);
    /* Layout: status line, (optional expanded
     * line), ruler directly
     * under it, text, and a two-row panel of key labels at the bottom. */
    int row = y0 + 1;

    if (e->opt_expanded != 0) {
        Fmt f;

        fmt_resolve(d, e->cur.item, e->cur.col, &f);
        snprintf(line, sizeof line, " Just: %-7s Indent: %-14s L: %-3d %s",
                 just_name(f.just), ind_name(f.indent), f.bl,
                 f.headline != 0 ? "Headline" : "");
        term_fill(0, row, w, 1, ' ', attr);
        term_puts(0, row, line, attr);
        row++;
    }

    if (e->opt_ruler != 0) {
        Fmt f;

        fmt_resolve(d, e->cur.item, e->cur.col, &f);
        ed_layout(e);
        int lo = 0;

        if (is_edit_block(d, e->cur.item) != 0 &&
            e->view.bv[e->cur.item].visible != 0) {
            lo = e->view.bv[e->cur.item].colx[e->cur.col];
        }

        for (int x = 0; x < w; x++) {
            term_put(x, row, 0xFA, TA_DIM);
        }

        for (int x = lo; x < lo + f.bl && x < w; x++) {
            term_put(x, row, 0xFE, 0);
        }

        for (int t = 0; t < f.ntab; t++) {
            int x = lo + f.bl + f.tabpos[t];
            uint8_t ch = f.tabtype[t] == 'R' ? 0x11 :
                         f.tabtype[t] == 'C' ? '^' :
                         f.tabtype[t] == 'D' ? '.' :
                         f.tabtype[t] == ',' ? ',' :
                         f.tabtype[t] == 'N' ? '#' :
                                               0x10;

            if (x < w) {
                term_put(x, row, ch, TA_BOLD);
            }
        }
    }

    if (strip != 0 && e->opt_keys != 0) {
        /*
         * The labels of the twelve function keys, six on each row: F1, F3,
         * F5, F7, F9 and F11 on the first row, F2, F4, F6, F8, F10 and F12
         * on the second.  A key that has no command has an empty label
         * (F10 and F11 without Ctrl, Alt-F1, Alt-F10 and Alt-F11, and every
         * key with Shift, which has no functions).  Alt shows the Alt functions, Ctrl the Ctrl
         * functions and Shift empty labels; Alt wins over Ctrl, and Ctrl
         * over Shift, when several are held.  The column of an entry is
         * the same in all four modes, so that the strip does not jump.
         */
        const char *const *const modes[4] = {key_labels_normal,
                                             key_labels_ctrl, key_labels_alt,
                                             key_labels_none};
        int xs[KEY_STRIP_COLS];
        int mods = term_modifiers();
        const char *const *labels = (mods & KM_ALT) != 0   ? key_labels_alt :
                                    (mods & KM_CTRL) != 0  ? key_labels_ctrl :
                                    (mods & KM_SHIFT) != 0 ? key_labels_none :
                                                             key_labels_normal;

        key_strip_columns(modes, 4, xs);

        for (int k = 0; k < 2; k++) {
            int y = R - 2 + k;

            term_fill(0, y, w, 1, ' ', TS_KEYS);

            for (int i = 0; i < KEY_STRIP_COLS; i++) {
                int f = 2 * i + k;
                char text[40];

                snprintf(text, sizeof text, "F%d: %s", f + 1, labels[f]);
                term_puts(xs[i], y, text, TS_KEYS);
            }
        }

        if (e->msg[0] != '\0') {
            term_fill(0, R - 1, w, 1, ' ', TS_MESSAGE);
            term_puts(1, R - 1, msgfit, TS_MESSAGE | TA_BOLD);
        }
    } else if (strip != 0 && e->msg[0] != '\0') {
        term_fill(0, R - 1, w, 1, ' ', TS_MESSAGE);
        term_puts(1, R - 1, msgfit, TS_MESSAGE | TA_BOLD);
    }
}


void
ed_status(Editor *e, int w)
{
    ed_status_at(e, w, 0, 1);
}
