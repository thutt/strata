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

/* Copyright (c) 2026 Logic Magicians Software */

/* perr.c - the Print Error markers: insert, remove, find. */
#include "util.h"
#include "perr.h"
#include "marker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int item;
    int col;
    int tok;
    int seq;
    int diag; /* The index of the diagnostic. */
} Spot;


static int
is_perr(const Tok *k)
{
    return k->kind == TK_MARKER && tok_mtype(k) == MT_PRINT_ERROR;
}


void
perr_text(const Tok *k, char *out, size_t cap)
{
    MPayload p;
    const char *m;
    const char *c;

    out[0] = '\0';

    if (is_perr(k) == 0) {
        return;
    }

    mp_parse(&p, marker_info(MT_PRINT_ERROR), tok_mdata(k), tok_mlen(k));
    m = mp_get(&p, "1");
    c = mp_get(&p, "2");
    snprintf(out, cap, "%s [%s]", m != NULL ? m : "", c != NULL ? c : "");
    mp_free(&p);
}


/* The run recorded in a Print Error marker, or '?'. */
static int
perr_run(const Tok *k)
{
    MPayload p;
    const char *r;
    int run = '?';

    mp_parse(&p, marker_info(MT_PRINT_ERROR), tok_mdata(k), tok_mlen(k));
    r = mp_get(&p, "3");

    if (r != NULL && r[0] != '\0') {
        run = r[0];
    }

    mp_free(&p);

    return run;
}


/* A block of the part? Every block counts when there is no part. */
static int
in_part(const Doc *part, const Block *b)
{
    if (part == NULL) {
        return 1;
    }

    for (int i = 0; i < part->nitems; i++) {
        if (part->items[i].type == IT_BLOCK && part->items[i].blk == b) {
            return 1;
        }
    }

    return 0;
}


int
perr_strip_text(Text *t)
{
    int n = 0;

    for (int i = t->n - 1; i >= 0; i--) {
        if (is_perr(&t->v[i]) != 0) {
            text_delete(t, i, 1);
            n++;
        }
    }

    return n;
}


void
perr_strip_block(Block *b)
{
    for (int c = 0; c < b->ncols; c++) {
        perr_strip_text(&b->cols[c].text);
    }
}


int
perr_count(const Doc *d)
{
    int n = 0;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type != IT_BLOCK || d->items[i].blk == NULL) {
            continue;
        }

        for (int c = 0; c < d->items[i].blk->ncols; c++) {
            const Text *t = &d->items[i].blk->cols[c].text;

            for (int k = 0; k < t->n; k++) {
                if (is_perr(&t->v[k]) != 0) {
                    n++;
                }
            }
        }
    }

    return n;
}


/* The document changed: its blocks are laid out again. */
static void
changed(Editor *e)
{
    for (int i = 0; i < e->doc->nitems; i++) {
        if (is_edit_block(e->doc, i) != 0) {
            e->doc->items[i].blk->dirty = 1;
        }
    }

    doc_touch(e->doc);
    ed_invalidate(e);
    ed_fix_cursor(e);
}


/*
 * Remove the markers of a run (run 0: of every run) from the blocks of the
 * part.  The spots that follow a removed marker in its text move back.
 */
static int
remove_markers(Editor *e, int run, const Doc *part, Spot *spots, int nspots)
{
    Doc *d = e->doc;
    int removed = 0;

    for (int i = 0; i < d->nitems; i++) {
        if (is_edit_block(d, i) == 0 || in_part(part, d->items[i].blk) == 0) {
            continue;
        }

        for (int c = 0; c < d->items[i].blk->ncols; c++) {
            Text *t = &d->items[i].blk->cols[c].text;

            for (int k = t->n - 1; k >= 0; k--) {
                if (is_perr(&t->v[k]) == 0 ||
                    (run != 0 && perr_run(&t->v[k]) != run)) {
                    continue;
                }

                text_delete(t, k, 1);
                removed++;

                for (int s = 0; s < nspots; s++) {
                    if (spots[s].item == i && spots[s].col == c &&
                        spots[s].tok > k) {
                        spots[s].tok--;
                    }
                }

                if (e->cur.item == i && e->cur.col == c && e->cur.tok > k) {
                    e->cur.tok--;
                }
            }
        }
    }

    return removed;
}


/* Order the spots so that the last place comes first, and for equal places
   the diagnostic that was found first comes first in the document. */
static int
spot_cmp(const void *a, const void *b)
{
    const Spot *x = a;
    const Spot *y = b;

    if (x->item != y->item) {
        return y->item - x->item;
    }

    if (x->col != y->col) {
        return y->col - x->col;
    }

    if (x->tok != y->tok) {
        return y->tok - x->tok;
    }

    return y->seq - x->seq;
}


/* The message of a marker: the text, and a note when the place is a guess. */
static void
marker_message(const Diag *g, char *out, size_t cap)
{
    if (g->moved != 0) {
        snprintf(out, cap,
                 "%s (The exact place was not found; this is the nearest.)",
                 g->msg);
    } else {
        snprintf(out, cap, "%s", g->msg);
    }
}


int
perr_apply(Editor *e, int run, const Diag *v, int n, const Doc *part)
{
    Doc *d = e->doc;
    DiagList l;
    Spot *spots;
    int nspots = 0;
    int removed;
    int inserted = 0;
    char runs[2] = {chr(run), '\0'};

    diag_list_init(&l);
    diag_list_set(&l, v, n, 0);

    if (part != NULL) {
        diag_remap(&l, part, d);
    } else {
        for (int i = 0; i < l.n; i++) {
            diag_locate(d, &l.v[i]);
        }
    }

    spots = calloc(usz(l.n > 0 ? l.n : 1), sizeof *spots);

    /* The places first: they are those of the document as it was printed,
       with its old markers in it. */
    for (int i = 0; i < l.n; i++) {
        Diag *g = &l.v[i];
        int item = 0;
        int col = 0;
        int tok = 0;

        if (g->item < 0 && part != NULL) {
            /* No place at all: the start of the part. */
            for (int j = 0; j < d->nitems; j++) {
                if (is_edit_block(d, j) != 0 &&
                    in_part(part, d->items[j].blk) != 0) {
                    item = j;
                    break;
                }
            }

            g->moved = 1;
        } else {
            diag_refind(d, g, &item, &col, &tok);
        }

        if (is_edit_block(d, item) == 0) {
            continue;
        }

        spots[nspots].item = item;
        spots[nspots].col = col;
        spots[nspots].tok = tok;
        spots[nspots].seq = i;
        spots[nspots].diag = i;
        nspots++;
    }

    removed = remove_markers(e, run, part, spots, nspots);
    qsort(spots, usz(nspots), sizeof *spots, spot_cmp);

    for (int s = 0; s < nspots; s++) {
        const Diag *g = &l.v[spots[s].diag];
        Text *t = &d->items[spots[s].item].blk->cols[spots[s].col].text;
        MPayload p;
        uint8_t *bytes;
        size_t blen;
        char msg[DIAG_MSG + 80];
        int at = spots[s].tok;

        if (at > t->n) {
            at = t->n;
        }

        marker_message(g, msg, sizeof msg);
        mp_parse(&p, marker_info(MT_PRINT_ERROR), NULL, 0);
        mp_set(&p, "1", msg, 0);
        mp_set(&p, "2", g->code, 0);
        mp_set(&p, "3", runs, 0);
        bytes = mp_build(&p, &blen);
        mp_free(&p);
        text_insert(t, at, tok_marker(MT_PRINT_ERROR, bytes, blen));
        free(bytes);
        inserted++;

        if (e->cur.item == spots[s].item && e->cur.col == spots[s].col &&
            e->cur.tok >= at) {
            e->cur.tok++;
        }
    }

    if (inserted > 0 || removed > 0) {
        changed(e);
    }

    free(spots);
    diag_list_free(&l);

    return inserted;
}


int
perr_remove_all(Editor *e)
{
    int n = remove_markers(e, 0, NULL, NULL, 0);

    if (n > 0) {
        changed(e);
    }

    return n;
}


const Tok *
perr_here(Editor *e)
{
    Text *t = ed_text(e);

    if (t == NULL || e->cur.tok < 0 || e->cur.tok >= t->n ||
        is_perr(&t->v[e->cur.tok]) == 0) {
        return NULL;
    }

    return &t->v[e->cur.tok];
}


int
perr_remove_here(Editor *e)
{
    Text *t = ed_text(e);

    if (t == NULL || e->cur.tok < 0 || e->cur.tok >= t->n ||
        is_perr(&t->v[e->cur.tok]) == 0) {
        return 0;
    }

    text_delete(t, e->cur.tok, 1);
    changed(e);

    return 1;
}


/* Where the Print Error markers are, in the order of the document. */
static Spot *
all_spots(const Doc *d, int *count)
{
    int total = perr_count(d);
    Spot *v = calloc(usz(total > 0 ? total : 1), sizeof *v);
    int n = 0;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type != IT_BLOCK || d->items[i].blk == NULL) {
            continue;
        }

        for (int c = 0; c < d->items[i].blk->ncols; c++) {
            const Text *t = &d->items[i].blk->cols[c].text;

            for (int k = 0; k < t->n; k++) {
                if (is_perr(&t->v[k]) != 0) {
                    v[n].item = i;
                    v[n].col = c;
                    v[n].tok = k;
                    v[n].seq = n;
                    n++;
                }
            }
        }
    }

    *count = n;

    return v;
}


/* Compare a spot with the cursor: negative before, positive after. */
static int
spot_vs_cursor(const Spot *s, const Pos *p)
{
    if (s->item != p->item) {
        return s->item - p->item;
    }

    if (s->col != p->col) {
        return s->col - p->col;
    }

    return s->tok - p->tok;
}


int
perr_find(Editor *e, int forward)
{
    int n = 0;
    Spot *v = all_spots(e->doc, &n);
    int at = -1;
    int wrapped = 0;
    char text[DIAG_MSG + 120];
    const Text *t;

    if (n == 0) {
        free(v);
        ed_msg(e, "No print errors");

        return 0;
    }

    if (forward != 0) {
        for (int i = 0; i < n && at < 0; i++) {
            if (spot_vs_cursor(&v[i], &e->cur) > 0) {
                at = i;
            }
        }

        if (at < 0) {
            at = 0;
            wrapped = 1;
        }
    } else {
        for (int i = n - 1; i >= 0 && at < 0; i--) {
            if (spot_vs_cursor(&v[i], &e->cur) < 0) {
                at = i;
            }
        }

        if (at < 0) {
            at = n - 1;
            wrapped = 1;
        }
    }

    ed_goto(e, v[at].item, v[at].col, v[at].tok);
    t = &e->doc->items[v[at].item].blk->cols[v[at].col].text;
    perr_text(&t->v[v[at].tok], text, sizeof text);
    ed_msg(e, "%sPrint error %d of %d: %s",
           wrapped == 0 ? "" :
           forward != 0 ? "Wrapped to the start.  " :
                          "Wrapped to the end.  ",
           at + 1, n, text);
    free(v);

    return 1;
}
