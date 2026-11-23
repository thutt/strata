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

/* editor.c - editing operations on the document model. */
#include "util.h"
#include "term.h"
#include "perr.h"
#include "editor.h"
#include "undo.h"
#include "marker.h"
#include "calc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>


/* ----------------------------------------------------------- lifecycle */
Editor *
ed_new(Doc *d)
{
    Editor *e = calloc(1, sizeof *e);

    /* the defaults */
    e->opt_expanded = 0;
    e->opt_ruler = 1;
    e->opt_keys = 1;
    e->text_width = 80;
    e->only_s = -1;

    /* Search ignores the case; Replace keeps it, as it always did. */
    e->ropt.match_case = 1;
    e->rep_ask = 1;
    ed_set_doc(e, d);

    return e;
}


void
clip_free(Clip *c)
{
    for (int i = 0; i < c->n; i++) {
        block_free(c->items[i].blk);
    }

    free(c->items);
    text_free(&c->text);
    memset(c, 0, sizeof *c);
}


void
ed_set_doc(Editor *e, Doc *d)
{
    e->doc = d;
    undo_reset(e);
    e->sel.kind = SEL_NONE;
    e->top = 0;
    memset(&e->cur, 0, sizeof e->cur);
    ed_invalidate(e);
    ed_fix_cursor(e);
}


void
ed_free(Editor *e)
{
    if (e == NULL) {
        return;
    }

    clip_free(&e->clip);
    ed_view_free(e);
    undo_free(e);
    free(e);
}


void
ed_msg(Editor *e, const char *fmt, ...)
{
    char full[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(full, sizeof full, fmt, ap);
    va_end(ap);

    /* A long path is shortened before the message is cut to the room. */
    int room = term_cols() - 2;

    if (room > 159) {
        room = 159;
    }

    fit_message(full, room, e->msg, sizeof e->msg);
}


/* ------------------------------------------------------ structure helpers */
int
is_edit_block(const Doc *d, int i)
{
    return i >= 0 && i < d->nitems && d->items[i].type == IT_BLOCK &&
           d->items[i].blk->ncols > 0;
}


/* Does the block say that the document has a title page? */
static int
is_title_flag(const Block *b)
{
    return b->ncols == 0 && b->props.n > 0 && b->props.v[0].n > 0 &&
           b->props.v[0].s[0] == '=';
}


int
doc_title_range(const Doc *d, int *lo, int *hi)
{
    int s0 = 0;

    while (s0 < d->nitems && d->items[s0].type != IT_SECT) {
        s0++;
    }

    if (s0 == 0 || s0 >= d->nitems || d->items[s0 - 1].type != IT_BLOCK ||
        is_title_flag(d->items[s0 - 1].blk) == 0) {
        return 0;
    }

    *lo = s0 + 1;
    *hi = *lo;

    while (*hi < d->nitems && d->items[*hi].type == IT_BLOCK) {
        (*hi)++;
    }

    return 1;
}


/* ------------------------------------------- structured or unstructured */
/* Rewrite the *B record of the header. */
static void
set_body_record(Doc *d, int structured)
{
    for (int i = 0; i < d->header.n; i++) {
        if (d->header.v[i].n >= 2 && d->header.v[i].s[0] == '*' &&
            d->header.v[i].s[1] == 'B') {
            d->header.v[i].s[2] = structured != 0 ? '1' : '0';
        }
    }
}


/* The S item that starts the text, after the title page if there is one. */
static int
body_root(const Doc *d)
{
    int lo;
    int hi;
    int i = 0;

    if (doc_title_range(d, &lo, &hi) != 0) {
        return hi;
    }

    while (i < d->nitems && d->items[i].type != IT_SECT) {
        i++;
    }

    return i;
}


int
doc_can_unstructure(const Doc *d)
{
    int r = body_root(d);

    if (d->structured == 0 || r + 2 >= d->nitems ||
        d->items[r + 1].type != IT_PLUS || d->items[r + 2].type != IT_SECT) {
        return 0;
    }

    int k = r + 3;

    while (k < d->nitems && d->items[k].type == IT_BLOCK) {
        k++;
    }

    return k + 1 == d->nitems && d->items[k].type == IT_MINUS;
}


int
ed_set_structured(Editor *e, int structured)
{
    Doc *d = e->doc;
    int r = body_root(d);

    if (structured == d->structured) {
        return 0;
    }

    if (structured != 0) {
        /* The text becomes one section: S + S text -. */
        doc_insert_item(d, r + 1, IT_PLUS, NULL);
        doc_insert_item(d, r + 2, IT_SECT, NULL);
        doc_insert_item(d, d->nitems, IT_MINUS, NULL);
    } else {
        if (doc_can_unstructure(d) == 0) {
            ed_msg(e, "A document with several sections cannot be made "
                      "unstructured");

            return -1;
        }

        doc_delete_items(d, d->nitems - 1, 1);
        doc_delete_items(d, r + 1, 2);
    }

    d->structured = structured;
    set_body_record(d, structured);
    doc_touch(d);
    e->only_s = -1;
    memset(&e->cur, 0, sizeof e->cur);
    ed_invalidate(e);
    ed_fix_cursor(e);

    return 0;
}


int
sect_start(const Doc *d, int item)
{
    int k = item;

    if (k < 0 || k >= d->nitems) {
        return -1;
    }

    if (d->items[k].type == IT_SECT) {
        return k;
    }

    while (k >= 0 && d->items[k].type == IT_BLOCK) {
        k--;
    }

    return (k >= 0 && d->items[k].type == IT_SECT) ? k : -1;
}


int
sect_blocks_end(const Doc *d, int s)
{
    int k = s + 1;

    while (k < d->nitems && d->items[k].type == IT_BLOCK) {
        k++;
    }

    return k;
}


static int
match_minus(const Doc *d, int plus)
{
    int depth = 0;

    for (int m = plus; m < d->nitems; m++) {
        if (d->items[m].type == IT_PLUS) {
            depth++;
        } else if (d->items[m].type == IT_MINUS && --depth == 0) {
            return m;
        }
    }

    return d->nitems - 1;
}


int
sect_end(const Doc *d, int s)
{
    int k = sect_blocks_end(d, s);

    if (k < d->nitems && d->items[k].type == IT_PLUS) {
        return match_minus(d, k) + 1;
    }

    return k;
}


/* section depth ("level") of S item s */
static int
sect_level(const Doc *d, int s)
{
    return doc_item_level(d, s);
}


int
sect_find(const Doc *d, const char *num)
{
    int cnt[SECT_MAXDEPTH + 1] = {0};
    int depth = 0;
    int from = d->structured != 0 ? 1 : 0;

    for (int i = 0; i + 1 < d->nitems; i++) {
        int t = d->items[i].type;
        char sn[SECT_NUMLEN];
        int o = 0;

        if (t == IT_PLUS) {
            depth++;

            if (depth <= SECT_MAXDEPTH) {
                cnt[depth] = 0;
            }

            continue;
        }

        if (t == IT_MINUS) {
            depth--;
            continue;
        }

        if (t != IT_SECT) {
            continue;
        }

        if (depth >= 0 && depth <= SECT_MAXDEPTH) {
            cnt[depth]++;
        }

        if (is_edit_block(d, i + 1) == 0) {
            continue;
        }

        /*
         * The title page is not a section of the document: the first
         * section is 1, as the numbers of the cross-references say.
         */
        if (depth < from) {
            continue;
        }

        sn[0] = '\0';

        for (int l = from; l <= depth && l <= SECT_MAXDEPTH &&
                           o < isz(sizeof sn) - 12; l++) {
            o += snprintf(sn + o, sizeof sn - usz(o), l == from ? "%d" : ".%d",
                          cnt[l]);
        }

        if (strcmp(sn, num) == 0) {
            return i + 1;
        }
    }

    return -1;
}


int
sect_number(const Doc *d, int item, char *buf, int cap)
{
    int s = sect_start(d, item);

    if (s < 0) {
        snprintf(buf, usz(cap), "-");
        return 0;
    }

    int cnt[SECT_MAXDEPTH + 1] = {0};
    int depth = 0;

    for (int i = 0; i <= s; i++) {
        if (d->items[i].type == IT_PLUS) {
            depth++;

            if (depth <= SECT_MAXDEPTH) {
                cnt[depth] = 0;
            }
        } else if (d->items[i].type == IT_MINUS) {
            depth--;
        } else if (d->items[i].type == IT_SECT && depth >= 0 &&
                   depth <= SECT_MAXDEPTH) {
            cnt[depth]++;
        }
    }

    int o = 0;

    buf[0] = 0;
    int from = d->structured != 0 ? 1 : 0;

    if (depth < from) {
        snprintf(buf, usz(cap), "1");
        return 1;
    }

    for (int l = from; l <= depth && l <= SECT_MAXDEPTH && o < cap - 8; l++) {
        o += snprintf(buf + o, usz(cap - o), l == from ? "%d" : ".%d", cnt[l]);
    }

    return 1;
}


static int
sect_count(const Doc *d, int **idx, int **lvl)
{
    int n = 0;
    int cap = 64;
    int depth = 0;

    *idx = malloc(usz(cap) * sizeof(int));
    *lvl = malloc(usz(cap) * sizeof(int));
    for (int i = 0; i < d->nitems; i++) {
        int t = d->items[i].type;

        if (t == IT_PLUS) {
            depth++;
        } else if (t == IT_MINUS) {
            depth--;
        } else if (t == IT_SECT) {
            if (n == cap) {
                cap *= 2;
                *idx = realloc(*idx, usz(cap) * sizeof(int));
                *lvl = realloc(*lvl, usz(cap) * sizeof(int));
            }

            (*idx)[n] = i;
            (*lvl)[n] = depth;
            n++;
        }
    }

    return n;
}


/* --------------------------------------------------------------- cloning */
Tok
tok_dup(const Tok *k)
{
    Tok c = *k;

    if (k->kind == TK_MARKER) {
        c = tok_marker(tok_mtype(k), tok_mdata(k), tok_mlen(k));
    }

    return c;
}


Block *
block_clone_full(const Block *s)
{
    Block *b = block_new(0);

    for (int i = 0; i < s->props.n; i++) {
        lines_add(&b->props, s->props.v[i].s, usz(s->props.v[i].n));
    }

    for (int c = 0; c < s->ncols; c++) {
        Column *d = block_add_column(b);

        d->width = s->cols[c].width;

        for (int i = 0; i < s->cols[c].props.n; i++) {
            lines_add(&d->props, s->cols[c].props.v[i].s,
                      usz(s->cols[c].props.v[i].n));
        }

        /* The room that the copy needs, and no more: a text that grows by
           doubling keeps up to half of its room free. */
        int n = s->cols[c].text.n;

        d->text.v = malloc(usz(n > 0 ? n : 1) * sizeof *d->text.v);
        d->text.cap = n > 0 ? n : 1;

        for (int i = 0; i < n; i++) {
            d->text.v[i] = tok_dup(&s->cols[c].text.v[i]);
        }

        d->text.n = n;
    }

    b->dirty = s->dirty;
    b->sid = s->sid;

    return b;
}


void
clip_copy(Clip *dst, const Clip *src)
{
    memset(dst, 0, sizeof *dst);
    dst->kind = src->kind;
    dst->n = src->n;

    if (src->n > 0) {
        dst->items = calloc(usz(src->n), sizeof(Item));

        for (int i = 0; i < src->n; i++) {
            dst->items[i].type = src->items[i].type;
            dst->items[i].fold = src->items[i].fold;
            dst->items[i].se = src->items[i].se;
            dst->items[i].blk = src->items[i].blk != NULL ?
                                    block_clone_full(src->items[i].blk) :
                                    NULL;
        }
    }

    for (int i = 0; i < src->text.n; i++) {
        text_insert(&dst->text, dst->text.n, tok_dup(&src->text.v[i]));
    }
}



static Block *
block_empty(int ncols)
{
    Block *b = block_new(ncols);
    Tok nl;

    memset(&nl, 0, sizeof nl);
    nl.kind = TK_NL;

    for (int c = 0; c < b->ncols; c++) {
        text_insert(&b->cols[c].text, 0, nl);
    }

    return b;
}


/* ------------------------------------------------------------- cursor fix */
Text *
ed_text(Editor *e)
{
    Doc *d = e->doc;

    if (is_edit_block(d, e->cur.item) == 0) {
        return NULL;
    }

    Block *b = d->items[e->cur.item].blk;

    if (e->cur.col < 0 || e->cur.col >= b->ncols) {
        return NULL;
    }

    return &b->cols[e->cur.col].text;
}


static int
text_last(const Text *t)
{
    if (t->n == 0) {
        return 0;
    }

    return t->v[t->n - 1].kind == TK_NL ? t->n - 1 : t->n;
}


/* Is item i a block the cursor can be in? */
static int
cursor_ok(const Editor *e, int i)
{
    int lo;
    int hi;

    if (is_edit_block(e->doc, i) == 0) {
        return 0;
    }

    if (e->hide_title != 0 && doc_title_range(e->doc, &lo, &hi) != 0 &&
        i >= lo && i < hi) {
        return 0;
    }

    if (e->only_s >= 0 && e->only_s < e->doc->nitems &&
        (i < e->only_s || i >= sect_end(e->doc, e->only_s))) {
        return 0;
    }

    return 1;
}


void
ed_fix_cursor(Editor *e)
{
    Doc *d = e->doc;

    if (cursor_ok(e, e->cur.item) == 0) {
        int f = -1;

        for (int i = e->cur.item; i < d->nitems && f < 0; i++) {
            if (cursor_ok(e, i) != 0) {
                f = i;
            }
        }

        for (int i = e->cur.item < d->nitems ? e->cur.item : d->nitems - 1;
             i >= 0 && f < 0; i--) {
            if (cursor_ok(e, i) != 0) {
                f = i;
            }
        }

        /* Only hidden blocks are left: go to one of those, not to a new one. */
        for (int i = 0; i < d->nitems && f < 0; i++) {
            if (is_edit_block(d, i) != 0) {
                f = i;
            }
        }

        if (f < 0) { /* no editable block at all: rebuild a valid skeleton */
            doc_delete_items(d, 0, d->nitems);

            if (d->structured != 0) {
                doc_insert_item(d, 0, IT_SECT, NULL);
                doc_insert_item(d, 1, IT_PLUS, NULL);
                doc_insert_item(d, 2, IT_SECT, NULL);
                doc_insert_item(d, 3, IT_BLOCK, block_empty(1));
                doc_insert_item(d, 4, IT_MINUS, NULL);
                f = 3;
            } else {
                doc_insert_item(d, 0, IT_SECT, NULL);
                doc_insert_item(d, 1, IT_BLOCK, block_empty(1));
                f = 1;
            }

            ed_invalidate(e);
        }

        e->cur.item = f;
        e->cur.col = 0;
        e->cur.tok = 0;
    }

    Block *b = d->items[e->cur.item].blk;

    if (e->cur.col >= b->ncols) {
        e->cur.col = b->ncols - 1;
    }

    if (e->cur.col < 0) {
        e->cur.col = 0;
    }

    int last = text_last(&b->cols[e->cur.col].text);

    if (e->cur.tok > last) {
        e->cur.tok = last;
    }

    if (e->cur.tok < 0) {
        e->cur.tok = 0;
    }
}


/* The first item that the cursor can be on, where a document opens. */
static int
first_cursor_item(Editor *e)
{
    for (int i = 0; i < e->doc->nitems; i++) {
        if (cursor_ok(e, i) != 0) {
            return i;
        }
    }

    return 0;
}


void
ed_remember_position(Editor *e)
{
    Doc *d = e->doc;

    ed_layout(e);

    int line = ed_cursor_screen(e, NULL, NULL);
    int row = line >= 0 ? line - e->top : 0;

    if (e->cur.item == first_cursor_item(e) && e->cur.col == 0 &&
        e->cur.tok == 0 && e->top == 0) {
        doc_clear_view_pos(d);

        return;
    }

    doc_set_view_pos(d, e->cur.item, e->cur.col, e->cur.tok, row);
}


void
ed_restore_position(Editor *e)
{
    Doc *d = e->doc;
    int item;
    int col;
    int tok;
    int row;

    if (doc_get_view_pos(d, &item, &col, &tok, &row) == 0 || d->nitems < 1) {
        return;
    }

    if (item > d->nitems - 1) {
        item = d->nitems - 1;
    }

    /* ed_goto unfolds what hides the place and keeps the cursor valid. */
    ed_goto(e, item, col, tok);
    ed_fix_cursor(e);

    int line = ed_cursor_screen(e, NULL, NULL);

    if (line >= 0) {
        e->top = line - row;

        if (e->top < 0) {
            e->top = 0;
        }

        if (e->top > e->view.n - 1) {
            e->top = e->view.n > 0 ? e->view.n - 1 : 0;
        }
    }
}


/*
 * True for a token that other blocks depend on: a counter or a
 * cross-reference.  Every other token changes the layout of its own block
 * only.
 */
static int
tok_global(const Tok *k)
{
    return k->kind == TK_MARKER && (tok_mtype(k) == 2 || tok_mtype(k) == 17);
}


/* True if text t holds a token that other blocks depend on, in [lo, hi). */
static int
range_global(const Text *t, int lo, int hi)
{
    for (int i = lo; i < hi && i < t->n; i++) {
        if (tok_global(&t->v[i]) != 0) {
            return 1;
        }
    }

    return 0;
}


/*
 * True if the items [at, at + n) of d are blocks, and none holds a token
 * that other blocks depend on.  Such items can be inserted or deleted
 * without laying out the whole document again.
 */
static int
items_local(const Doc *d, int at, int n)
{
    for (int i = at; i < at + n && i < d->nitems; i++) {
        if (d->items[i].type != IT_BLOCK) {
            return 0;
        }

        const Block *b = d->items[i].blk;

        for (int c = 0; c < b->ncols; c++) {
            if (range_global(&b->cols[c].text, 0, b->cols[c].text.n) != 0) {
                return 0;
            }
        }
    }

    return 1;
}


/* The items [at, at + n) were inserted; see ed_view_inserted. */
static void
view_inserted(Editor *e, int at, int n)
{
    if (items_local(e->doc, at, n) != 0) {
        ed_view_inserted(e, at, n);
    } else {
        ed_invalidate(e);
    }
}


/* The items [at, at + n) were deleted; local says what items_local said. */
static void
view_deleted(Editor *e, int at, int n, int local)
{
    if (local != 0) {
        ed_view_deleted(e, at, n);
    } else {
        ed_invalidate(e);
    }
}


/*
 * The text of the block of the cursor changed by one character: only the
 * layout of that block is made again.  Other tokens may change other
 * blocks (a counter, say), and then touch is used.
 */
static void
touch_char(Editor *e)
{
    Doc *d = e->doc;

    if (is_edit_block(d, e->cur.item) != 0) {
        d->items[e->cur.item].blk->dirty = 1;
    }

    doc_touch(d);
    ed_invalidate_block(e);
}


static void
touch(Editor *e)
{
    Doc *d = e->doc;

    if (is_edit_block(d, e->cur.item) != 0) {
        d->items[e->cur.item].blk->dirty = 1;
    }

    doc_touch(d);
    ed_invalidate(e);
}


/*
 * The edits of the text of a block of the document count the markers that
 * they remove and add, for the lists of the printed document (see
 * docs/AUTOLISTS.txt): only the removed tokens, or the one token that is
 * added, are looked at, never the document.
 */
static void
tdel(Doc *d, Text *t, int pos, int n)
{
    doc_count_text(d, t, pos, pos + n, -1);
    text_delete(t, pos, n);
}


static void
tins(Doc *d, Text *t, int pos, Tok k)
{
    if (text_insert(t, pos, k) == 0 && k.kind == TK_MARKER) {
        doc_count_marker(d, tok_mtype(&k), 1);
    }
}


/* ---------------------------------------------------------------- editing */
static void
ed_insert_tok(Editor *e, Tok k)
{
    Text *t = ed_text(e);

    if (t == NULL) {
        return;
    }

    tins(e->doc, t, e->cur.tok, k);
    e->cur.tok++;

    if (tok_global(&k) == 0) {
        touch_char(e);
    } else {
        touch(e);
    }
}


void
ed_insert_char(Editor *e, uint8_t c)
{
    Text *t = ed_text(e);

    if (t == NULL) {
        return;
    }

    Tok k = tok_char(c);

    if (c == NBSP_BYTE) {
        k = tok_nbsp();
    } else if (c == SHY_CTRL) {
        k = tok_shy();
    } else if (c < 0x20) {
        k.esc = 1;
    }

    if (e->overtype != 0 && e->cur.tok < t->n &&
        t->v[e->cur.tok].kind == TK_CHAR) {
        t->v[e->cur.tok] = k;
        e->cur.tok++;
        touch_char(e);
        return;
    }

    ed_insert_tok(e, k);
}


void
ed_insert_text_indent(Editor *e)
{
    Tok k;

    memset(&k, 0, sizeof k);
    k.kind = TK_CTRL;
    k.a = 0x1A;
    ed_insert_tok(e, k);
}


void
ed_insert_nl(Editor *e)
{
    Tok k;

    memset(&k, 0, sizeof k);
    k.kind = TK_NL;
    ed_insert_tok(e, k);
}


void
ed_insert_tab(Editor *e, uint8_t fill)
{
    Tok k;

    memset(&k, 0, sizeof k);
    k.kind = TK_TAB;
    k.a = fill;
    ed_insert_tok(e, k);
}


void
ed_insert_marker(Editor *e, int type, const uint8_t *data, size_t len)
{
    /* Only perr_apply makes a Print Error: the user cannot. */
    if (type == MT_PRINT_ERROR) {
        ed_msg(e, "A Print Error cannot be made by hand");

        return;
    }

    ed_insert_tok(e, tok_marker(type, data, len));
}


void
ed_replace_marker(Editor *e, int tok, const uint8_t *data, size_t len)
{
    Text *t = ed_text(e);

    if (t == NULL || tok < 0 || tok >= t->n || t->v[tok].kind != TK_MARKER) {
        return;
    }

    int was = tok_mtype(&t->v[tok]);

    tok_marker_set(&t->v[tok], data, len);

    /* The panel changes the fields of a marker, not its type; if the type
       is ever changed here, one count moves from the old kind to the new. */
    if (tok_mtype(&t->v[tok]) != was) {
        doc_count_marker(e->doc, was, -1);
        doc_count_marker(e->doc, tok_mtype(&t->v[tok]), 1);
    }

    touch(e);
}


int
ed_attr_at(Editor *e)
{
    Text *t = ed_text(e);
    int a = 0;

    if (t == NULL) {
        return 0;
    }

    for (int i = 0; i < e->cur.tok && i < t->n; i++) {
        if (t->v[i].kind == TK_ATTR) {
            a = t->v[i].a;
        }
    }

    return a;
}


/* The attributes in effect at the cursor as the screen draws them: the
   attributes of the format of the block (a headline is usually bold) joined
   with the attributes that the text itself has set before the cursor. */
int
ed_attr_effective(Editor *e)
{
    Fmt f;

    fmt_resolve(e->doc, e->cur.item, e->cur.col, &f);

    return f.attr | ed_attr_at(e);
}


int
ed_flags_at(Editor *e)
{
    Text *t = ed_text(e);
    int f = 0;

    if (t == NULL) {
        return 0;
    }

    for (int i = 0; i < e->cur.tok && i < t->n; i++) {
        if (t->v[i].kind == TK_ATTR) {
            f = t->v[i].b;
        }
    }

    return f;
}


void
ed_toggle_attr(Editor *e, int mask)
{
    Tok k;

    memset(&k, 0, sizeof k);
    k.kind = TK_ATTR;
    int a = ed_attr_at(e);

    /* A change of the mask keeps the flags, except that Normal clears all. */
    k.a = (mask == 0) ? 0 : u8(a ^ mask);
    k.b = mask == 0 ? 0 : u8(ed_flags_at(e) & ~FLAG_STATE);
    ed_insert_tok(e, k);
}


static void
delete_block_at(Editor *e, int item);


void
ed_backspace(Editor *e)
{
    Text *t = ed_text(e);

    if (t == NULL) {
        return;
    }

    if (e->cur.tok > 0) {
        int plain = tok_global(&t->v[e->cur.tok - 1]) == 0;

        tdel(e->doc, t, e->cur.tok - 1, 1);
        e->cur.tok--;

        if (plain != 0) {
            touch_char(e);
        } else {
            touch(e);
        }

        return;
    }

    Doc *d = e->doc;

    if (e->cur.col == 0 && is_edit_block(d, e->cur.item - 1) != 0 &&
        d->items[e->cur.item - 1].blk->ncols == 1 &&
        d->items[e->cur.item].blk->ncols == 1) {
        e->cur.item--;
        e->cur.col = 0;
        Text *pt = ed_text(e);

        e->cur.tok = text_last(pt);
        ed_join(e, 1);
    }
}


void
ed_delete(Editor *e)
{
    Text *t = ed_text(e);

    if (t == NULL) {
        return;
    }

    if (e->cur.tok < text_last(t)) {
        int plain = tok_global(&t->v[e->cur.tok]) == 0;

        tdel(e->doc, t, e->cur.tok, 1);

        if (plain != 0) {
            touch_char(e);
        } else {
            touch(e);
        }

        return;
    }

    /* on the final end-of-line character */
    Doc *d = e->doc;
    Block *b = d->items[e->cur.item].blk;
    int empty = (t->n == 1);

    if (empty != 0 && b->ncols == 1) {
        delete_block_at(e, e->cur.item);
        return;
    }

    if (e->cur.col == b->ncols - 1) {
        if (ed_join(e, 1) != 0) {
            ed_msg(e, "Nothing to "
                      "join here");
        }
    }
}


/* is token i a word character? */
static bool
word_at(const Text *t, int i)
{
    return i >= 0 && i < t->n && t->v[i].kind == TK_CHAR && is_alnum(t->v[i].a);
}


void
ed_delete_word(Editor *e, int fwd)
{
    Text *t = ed_text(e);

    if (t == NULL) {
        return;
    }

    int p = e->cur.tok;

    if (fwd != 0) {
        int q = p;

        while (q < text_last(t) && word_at(t, q)) {
            q++;
        }

        while (q < text_last(t) && !word_at(t, q) && t->v[q].kind != TK_NL) {
            q++;
        }

        if (q == p && p < text_last(t)) {
            q++;
        }

        tdel(e->doc, t, p, q - p);
    } else {
        int q = p;

        while (q > 0 && !word_at(t, q - 1)) {
            q--;
        }

        while (q > 0 && word_at(t, q - 1)) {
            q--;
        }

        tdel(e->doc, t, q, p - q);
        e->cur.tok = q;
    }

    touch(e);
}


/* ------------------------------------------------------ word and replace */
/* A letter of the text: a letter of ASCII or of CP437 (the accented ones). */
static int
tok_letter(const Text *t, int i)
{
    if (i < 0 || i >= t->n || t->v[i].kind != TK_CHAR) {
        return 0;
    }

    int c = t->v[i].a;

    return is_alpha(c) != 0 || (c >= 0x80 && c <= 0xA5);
}


/* An apostrophe or a hyphen that has a letter on both sides. */
static int
tok_joiner(const Text *t, int i)
{
    if (i <= 0 || i + 1 >= t->n || t->v[i].kind != TK_CHAR) {
        return 0;
    }

    return (t->v[i].a == '\'' || t->v[i].a == '-') &&
           tok_letter(t, i - 1) != 0 && tok_letter(t, i + 1) != 0;
}


static int
tok_in_word(const Text *t, int i)
{
    return tok_letter(t, i) != 0 || tok_joiner(t, i) != 0;
}


int
ed_word_at(Editor *e, int *from, int *to, char *buf, int cap)
{
    Text *t = ed_text(e);

    if (t == NULL) {
        return 0;
    }

    int p = e->cur.tok;

    if (tok_letter(t, p) == 0) {
        /* Look back over what is not a letter, on this line only. */
        p--;

        while (p >= 0 && tok_letter(t, p) == 0 && t->v[p].kind != TK_NL) {
            p--;
        }

        if (p < 0 || t->v[p].kind == TK_NL) {
            return 0;
        }
    }

    int a = p;
    int b = p + 1;

    while (a > 0 && tok_in_word(t, a - 1) != 0) {
        a--;
    }

    while (b < t->n && tok_in_word(t, b) != 0) {
        b++;
    }

    if (b - a > THES_MAXWORD) {
        b = a + THES_MAXWORD;
    }

    if (b - a >= cap) {
        return 0;
    }

    for (int i = a; i < b; i++) {
        buf[i - a] = chr(t->v[i].a);
    }

    buf[b - a] = '\0';
    *from = a;
    *to = b;

    return 1;
}


void
ed_replace_range(Editor *e, int from, int to, const uint8_t *text, int n)
{
    Text *t = ed_text(e);

    if (t == NULL || from < 0 || to < from || to > t->n) {
        return;
    }

    tdel(e->doc, t, from, to - from);

    for (int i = 0; i < n; i++) {
        text_insert(t, from + i,
                    text[i] == NBSP_BYTE ? tok_nbsp() : tok_char(text[i]));
    }

    e->cur.tok = from + n;
    touch(e);
}


/* ------------------------------------------------------------- structure */
static Tok
nl_tok(void)
{
    Tok k;

    memset(&k, 0, sizeof k);
    k.kind = TK_NL;

    return k;
}


void
ed_append_block(Editor *e, int global)
{
    Doc *d = e->doc;
    int is_head = e->cur.item > 0 && d->items[e->cur.item - 1].type == IT_SECT;
    Block *nb =
        (global != 0 || is_head != 0 || is_edit_block(d, e->cur.item) == 0) ?
            block_empty(1) :
            block_clone_format(d->items[e->cur.item].blk);

    doc_insert_item(d, e->cur.item + 1, IT_BLOCK, nb);
    e->cur.item++;
    e->cur.col = 0;
    e->cur.tok = 0;
    view_inserted(e, e->cur.item, 1);
}


void
ed_append_column(Editor *e)
{
    Doc *d = e->doc;

    if (is_edit_block(d, e->cur.item) == 0) {
        return;
    }

    Block *b = d->items[e->cur.item].blk;

    Column *last = &b->cols[b->ncols - 1];

    if (last->width == 0) { /* last column gains an explicit width */
        char v[16];

        last->width = e->text_width / (b->ncols + 1);
        snprintf(v, sizeof v, "%d", last->width);
        props_set(&last->props, "CW", 0, v);
    }

    Column *c = block_add_column(b);

    c->width = 0;
    text_insert(&c->text, 0, nl_tok());
    e->cur.col = b->ncols - 1;
    e->cur.tok = 0;
    b->dirty = 1;
    doc_touch(d);
    ed_invalidate_block(e);
}


void
ed_append_section(Editor *e, int sub)
{
    Doc *d = e->doc;

    if (d->structured == 0) {
        ed_append_block(e, 1);
        return;
    }

    int s = sect_start(d, e->cur.item);

    if (s < 0) {
        ed_msg(e, "No section here");
        return;
    }

    Block *nb = block_empty(1);
    int at;

    if (sub == 0) {
        at = sect_end(d, s);
        doc_insert_item(d, at, IT_SECT, NULL);
        doc_insert_item(d, at + 1, IT_BLOCK, nb);
        e->cur.item = at + 1;
    } else {
        int be = sect_blocks_end(d, s);

        if (be < d->nitems && d->items[be].type == IT_PLUS) {
            int m = match_minus(d, be);

            doc_insert_item(d, m, IT_SECT, NULL);
            doc_insert_item(d, m + 1, IT_BLOCK, nb);
            e->cur.item = m + 1;
        } else {
            doc_insert_item(d, be, IT_PLUS, NULL);
            doc_insert_item(d, be + 1, IT_SECT, NULL);
            doc_insert_item(d, be + 2, IT_BLOCK, nb);
            doc_insert_item(d, be + 3, IT_MINUS, NULL);
            e->cur.item = be + 2;
        }
    }

    e->cur.col = 0;
    e->cur.tok = 0;
    ed_invalidate(e);
}


void
ed_split(Editor *e, int what)
{
    Doc *d = e->doc;
    Text *t = ed_text(e);

    if (t == NULL) {
        return;
    }

    Block *b = d->items[e->cur.item].blk;

    if (what == 0) {
        Column *c0 = &b->cols[e->cur.col];
        int w = c0->width != 0 ? c0->width : e->text_width;

        block_add_column(b); /* may realloc: re-fetch */
        c0 = &b->cols[e->cur.col];

        /* shift columns right to make room after cur.col */
        for (int i = b->ncols - 1; i > e->cur.col + 1; i--) {
            b->cols[i] = b->cols[i - 1];
        }

        Column *nc = &b->cols[e->cur.col + 1];

        memset(nc, 0, sizeof *nc);

        for (int i = e->cur.tok; i < c0->text.n; i++) {
            Tok k = tok_dup(&c0->text.v[i]);

            text_insert(&nc->text, nc->text.n, k);
        }

        text_delete(&c0->text, e->cur.tok, c0->text.n - e->cur.tok);
        text_insert(&c0->text, c0->text.n, nl_tok());
        nc->width = c0->width != 0 ? c0->width / 2 : 0;
        char v[16];

        c0->width = w / 2;

        if (c0->width < 1) {
            c0->width = 1;
        }

        snprintf(v, sizeof v, "%d", c0->width);
        props_set(&c0->props, "CW", 0, v);

        if (nc->width == 0 && e->cur.col + 1 < b->ncols - 1) {
            nc->width = c0->width;
        }

        for (int i = 0; i < c0->props.n; i++) {
            if (strncmp(cstr(c0->props.v[i].s), "CW", 2) != 0) {
                lines_add(&nc->props, c0->props.v[i].s, usz(c0->props.v[i].n));
            }
        }

        e->cur.col++;
        e->cur.tok = 0;
        b->dirty = 1;
        doc_touch(d);
        ed_invalidate_block(e);
        return;
    }

    if (b->ncols != 1) {
        ed_msg(e, "Split a block of several columns by "
                  "column");
        return;
    }

    Block *nb = block_clone_format(b);

    text_delete(&nb->cols[0].text, 0, nb->cols[0].text.n);

    for (int i = e->cur.tok; i < t->n; i++) {
        Tok k = tok_dup(&t->v[i]);

        text_insert(&nb->cols[0].text, nb->cols[0].text.n, k);
    }

    tdel(d, t, e->cur.tok, t->n - e->cur.tok);
    text_insert(t, t->n, nl_tok());
    b->dirty = nb->dirty = 1;
    int at = e->cur.item + 1;

    if (what == 2 && d->structured != 0) {
        doc_insert_item(d, at++, IT_SECT, NULL);
    }

    doc_insert_item(d, at, IT_BLOCK, nb);
    e->cur.item = at;
    e->cur.tok = 0;
    e->cur.col = 0;
    doc_touch(d);

    if (what == 1) {
        view_inserted(e, at, 1);
    } else {
        ed_invalidate(e);
    }
}


int
ed_join(Editor *e, int what)
{
    Doc *d = e->doc;

    if (what == 2) {
        if (d->structured == 0) {
            return -1;
        }

        int s = sect_start(d, e->cur.item);

        if (s < 0) {
            return -1;
        }

        int be = sect_blocks_end(d, s);

        if (be < d->nitems && d->items[be].type == IT_PLUS) {
            ed_msg(e, "Cannot join a section that has subsections");
            return -1;
        }

        if (be >= d->nitems || d->items[be].type != IT_SECT) {
            ed_msg(e, "No "
                      "following peer section");
            return -1;
        }

        doc_delete_items(d, be, 1);
        ed_invalidate(e);
        return 0;
    }

    int nx = e->cur.item + 1;

    if (is_edit_block(d, nx) == 0) {
        return -1;
    }

    Block *a = d->items[e->cur.item].blk;
    Block *b = d->items[nx].blk;

    if (a->ncols != 1 || b->ncols != 1) {
        ed_msg(e, "Join works on "
                  "single-column blocks");
        return -1;
    }

    Text *ta = &a->cols[0].text;
    Text *tb = &b->cols[0].text;

    if (ta->n != 0 && ta->v[ta->n - 1].kind == TK_NL) {
        text_delete(ta, ta->n - 1, 1);
    }

    int at = ta->n;

    for (int i = 0; i < tb->n; i++) {
        Tok k = tok_dup(&tb->v[i]);

        tins(d, ta, ta->n, k);
    }

    a->dirty = 1;

    int local = items_local(d, nx, 1);

    doc_delete_items(d, nx, 1);
    e->cur.col = 0;
    e->cur.tok = at;
    view_deleted(e, nx, 1, local);
    ed_fix_cursor(e);

    return 0;
}


static void
delete_block_at(Editor *e, int item)
{
    Doc *d = e->doc;
    int s = sect_start(d, item);
    int be = s >= 0 ? sect_blocks_end(d, s) : item + 1;
    int nblocks = s >= 0 ? be - (s + 1) : 2;

    if (nblocks <= 1) {
        /* the only block of a section: delete the whole (empty) section, if
           allowed */
        int has_sub = be < d->nitems && d->items[be].type == IT_PLUS;

        if (d->structured == 0 || has_sub != 0 || s < 0) {
            ed_msg(e, "Cannot delete the "
                      "only block");
            return;
        }

        int nsec = 0;

        for (int i = 0; i < d->nitems; i++) {
            if (d->items[i].type == IT_SECT) {
                nsec++;
            }
        }

        if (nsec <= 2) {
            ed_msg(e, "Cannot delete the only section");
            return;
        }

        doc_delete_items(d, s, 2);
        e->cur.item = s > 0 ? s - 1 : 0;
        ed_invalidate(e);
    } else {
        int local = items_local(d, item, 1);

        doc_delete_items(d, item, 1);

        if (e->cur.item >= d->nitems) {
            e->cur.item = d->nitems - 1;
        }

        view_deleted(e, item, 1, local);
    }

    e->cur.col = 0;
    e->cur.tok = 0;
    ed_fix_cursor(e);
}


/* ---------------------------------------------------------------- folding */
static void
apply_visibility_fix(Editor *e)
{
    ed_invalidate(e);
    ed_layout(e);
    Doc *d = e->doc;

    if (e->view.bv[e->cur.item].visible == 0) {
        /* move to nearest visible block before */
        for (int i = e->cur.item; i >= 0; i--) {
            if (is_edit_block(d, i) != 0 && e->view.bv[i].visible != 0) {
                e->cur.item = i;
                break;
            }
        }

        e->cur.col = 0;
        e->cur.tok = 0;
    }

    ed_fix_cursor(e);
}


static int
fold_is_level_state(const Doc *d, int n, int expanded, int *idx, int *lvl,
                    int ns)
{
    for (int i = 0; i < ns; i++) {
        int L = lvl[i];

        if (L < 1) {
            continue;
        }

        int want;

        if (L < n) {
            want = expanded != 0 ? 0 : 1;
        } else if (L == n) {
            want = expanded != 0 ? 3 : 2;
        } else {
            continue;
        }

        if (d->items[idx[i]].fold != want) {
            return 0;
        }
    }

    return 1;
}


void
ed_fold(Editor *e, int cmd, int arg)
{
    Doc *d = e->doc;

    if (d->structured == 0) {
        ed_msg(e, "Collapse/expand works in structured "
                  "documents");
        return;
    }

    int s = sect_start(d, e->cur.item);
    int *idx;
    int *lvl;
    int ns = sect_count(d, &idx, &lvl);
    uint8_t *before = malloc(usz(d->nitems + 1));

    for (int i = 0; i < d->nitems; i++) {
        before[i] = d->items[i].fold;
    }

    if (cmd == 0 && s >= 0) {
        d->items[s].fold = 2;
    } else if (cmd == 1 && s >= 0) {
        Item *it = &d->items[s];

        if (it->fold != '\0') {
            it->fold = 0;

            for (int i = 0; i < ns; i++) {
                if (idx[i] > s && idx[i] < sect_end(d, s) &&
                    lvl[i] == sect_level(d, s) + 1) {
                    d->items[idx[i]].fold = 2;
                }
            }
        } else { /* already open: open the next level too */
            for (int i = 0; i < ns; i++) {
                if (idx[i] > s && idx[i] < sect_end(d, s) &&
                    lvl[i] == sect_level(d, s) + 1) {
                    d->items[idx[i]].fold = 0;

                    for (int j = 0; j < ns; j++) {
                        if (idx[j] > idx[i] && idx[j] < sect_end(d, idx[i]) &&
                            lvl[j] == lvl[i] + 1) {
                            d->items[idx[j]].fold = 2;
                        }
                    }
                }
            }
        }
    } else if (cmd == 2 && s >= 0) {
        d->items[s].fold = d->items[s].fold == 0 ? 1 : 0;
    } else if (cmd == 3) {
        /* currently outline-collapsed to this level? */
        int expanded = fold_is_level_state(d, arg, 0, idx, lvl, ns);

        for (int i = 0; i < ns; i++) {
            int L = lvl[i];
            Item *it = &d->items[idx[i]];

            if (L < 1) {
                continue;
            }

            if (L < arg) {
                it->fold = expanded != 0 ? 0 : 1;
            } else if (L == arg) {
                it->fold = expanded != 0 ? 3 : 2;
            } else {
                it->fold = 0;
            }
        }
    } else if (cmd == 4) {
        int all1 = 1;

        for (int i = 0; i < ns; i++) {
            if (lvl[i] >= 1 && d->items[idx[i]].fold != 1) {
                all1 = 0;
            }
        }

        for (int i = 0; i < ns; i++) {
            if (lvl[i] >= 1) {
                d->items[idx[i]].fold = all1 != 0 ? 0 : 1;
            }
        }
    }

    free(idx);
    free(lvl);

    /* The state of the outline is part of the file:
       changing it changes the document. */
    for (int i = 0; i < d->nitems; i++) {
        if (before[i] != d->items[i].fold) {
            doc_touch(d);
            break;
        }
    }

    free(before);
    apply_visibility_fix(e);
}


static void
unfold_all(Editor *e)
{
    for (int i = 0; i < e->doc->nitems; i++) {
        if (e->doc->items[i].fold != 0) {
            e->doc->items[i].fold = 0;
            doc_touch(e->doc);
        }
    }

    ed_invalidate(e);
}


/* ---------------------------------------------------- selection/clipboard */
void
ed_sel_begin(Editor *e, int kind)
{
    e->sel.kind = kind;
    e->sel.anchor = e->cur;
}


void
ed_sel_cancel(Editor *e)
{
    e->sel.kind = SEL_NONE;
}


void
ed_sel_range(Editor *e, int *lo, int *hi)
{
    Doc *d = e->doc;
    int a = e->sel.anchor.item;
    int c = e->cur.item;

    if (e->sel.kind == SEL_BLOCK) {
        *lo = a < c ? a : c;
        *hi = a < c ? c : a;
    } else if (e->sel.kind == SEL_SECTION) {
        int sa = sect_start(d, a);
        int sc = sect_start(d, c);

        if (sa < 0) {
            sa = 0;
        }

        if (sc < 0) {
            sc = 0;
        }

        *lo = sa < sc ? sa : sc;
        *hi = sect_end(d, sa < sc ? sc : sa) - 1;
    } else {
        *lo = *hi = c;
    }
}


static int
range_balanced(const Doc *d, int lo, int hi)
{
    int depth = 0;

    for (int i = lo; i <= hi; i++) {
        if (d->items[i].type == IT_PLUS) {
            depth++;
        } else if (d->items[i].type == IT_MINUS && --depth < 0) {
            return 0;
        }
    }

    return depth == 0;
}


/* ------------------------------------------------------------ autosort */
/*
 * The weight of a character in the sort order.  Letters count without case.
 * Of the punctuation seen in documents, a hyphen comes before an
 * underscore and an underscore before a space; all of them before digits.
 */
static int
sort_weight(unsigned char c)
{
    if (c == '-') {
        return 1;
    }

    if (c == '_') {
        return 2;
    }

    if (c == ' ') {
        return 3;
    }

    return to_lower(c) + 10;
}


/* The order of the format: no case, then a lower case letter first. */
static int
sort_compare(const char *a, const char *b)
{
    size_t i = 0;

    for (; a[i] != '\0' && b[i] != '\0'; i++) {
        int d = sort_weight(u8(a[i])) - sort_weight(u8(b[i]));

        if (d != 0) {
            return d;
        }
    }

    if (a[i] != b[i]) {
        return a[i] == '\0' ? -1 : 1;
    }

    for (i = 0; a[i] != '\0'; i++) {
        if (a[i] != b[i]) {
            return a[i] != to_lower(a[i]) ? 1 : -1;
        }
    }

    return 0;
}


/* An item range to sort, with the text it is sorted by. */
typedef struct {
    int first;
    int last; /* one past the end */
    char key[128];
    int order; /* the position before sorting, so equal keys keep theirs */
} SortRange;

static int sort_down;


static int
sort_range_cmp(const void *pa, const void *pb)
{
    const SortRange *a = pa;
    const SortRange *b = pb;
    int d = sort_compare(a->key, b->key);

    if (d == 0) {
        return a->order - b->order;
    }

    return sort_down != 0 ? -d : d;
}


static void
sort_key_of(const Doc *d, int item, char *key, int cap)
{
    key[0] = '\0';

    if (is_edit_block(d, item) != 0 && d->items[item].blk->ncols > 0) {
        text_plain(&d->items[item].blk->cols[0].text, key, cap);
    }

    /* The end-of-block mark is not part of the text that is compared. */
    for (int n = slen(key); n > 0 && key[n - 1] == ' '; n--) {
        key[n - 1] = '\0';
    }
}


/* Put the items of the ranges back in sorted order, from item at. */
static void
sort_apply(Editor *e, SortRange *r, int n, int at, int end, int desc)
{
    Doc *d = e->doc;
    Item *tmp = malloc(usz(end - at + 1) * sizeof *tmp);
    int k = 0;

    sort_down = desc;
    qsort(r, usz(n), sizeof *r, sort_range_cmp);

    for (int i = 0; i < n; i++) {
        for (int j = r[i].first; j < r[i].last; j++) {
            tmp[k++] = d->items[j];
        }
    }

    memcpy(&d->items[at], tmp, usz(k) * sizeof *tmp);
    free(tmp);
    doc_touch(d);
    ed_invalidate(e);
    ed_fix_cursor(e);
}


static int
sort_blocks(Editor *e, int lo, int hi)
{
    Doc *d = e->doc;
    int n = hi - lo + 1;
    SortRange *r = malloc(usz(n > 1 ? n : 1) * sizeof *r);

    for (int i = 0; i < n; i++) {
        r[i].first = lo + i;
        r[i].last = lo + i + 1;
        r[i].order = i;
        sort_key_of(d, lo + i, r[i].key, sizeof r[i].key);
    }

    sort_apply(e, r, n, lo, hi + 1, e->sort_descending);
    free(r);
    e->cur.item = lo;
    e->cur.col = 0;
    e->cur.tok = 0;
    ed_fix_cursor(e);

    return n;
}


int
ed_sort_sections(Editor *e, int descending)
{
    Doc *d = e->doc;

    if (d->structured == 0) {
        ed_msg(e, "Sections exist only in structured documents");

        return -1;
    }

    int s = sect_start(d, e->cur.item);

    if (s < 0) {
        ed_msg(e, "There is no current section");

        return -1;
    }

    int p = sect_blocks_end(d, s);

    if (p >= d->nitems || d->items[p].type != IT_PLUS) {
        ed_msg(e, "The section has no subsections to sort");

        return -1;
    }

    int q = sect_end(d, s) - 1;
    int cap = 8;
    int n = 0;
    SortRange *r = malloc(usz(cap) * sizeof *r);

    for (int i = p + 1; i < q;) {
        int end = sect_end(d, i);

        if (n == cap) {
            cap *= 2;
            r = realloc(r, usz(cap) * sizeof *r);
        }

        r[n].first = i;
        r[n].last = end;
        r[n].order = n;
        sort_key_of(d, sect_blocks_end(d, i) > i + 1 ? i + 1 : i, r[n].key,
                    sizeof r[n].key);
        n++;
        i = end;
    }

    if (n < 2) {
        free(r);
        ed_msg(e, "Nothing to sort");

        return -1;
    }

    sort_apply(e, r, n, p + 1, q, descending);
    free(r);
    ed_msg(e, "Sorted %d sections", n);

    return n;
}


void
ed_sel_section_edge(Editor *e, int end)
{
    Doc *d = e->doc;
    int s = sect_start(d, e->cur.item);

    if (s < 0) {
        return;
    }

    int last = sect_blocks_end(d, s) - 1;
    int target = end != 0 ? last : s + 1;

    if (is_edit_block(d, target) != 0) {
        e->cur.item = target;
        e->cur.col = 0;
        e->cur.tok = 0;
        ed_fix_cursor(e);
    }
}


/* The pairs of lower case and upper case letters of the code page 437. */
static const struct {
    uint8_t lower;
    uint8_t upper;
} case_pairs[] = {{0x81, 0x9A}, {0x82, 0x90}, {0x84, 0x8E}, {0x86, 0x8F},
                  {0x87, 0x80}, {0x91, 0x92}, {0x94, 0x99}, {0xA4, 0xA5}};


/* The upper case of the letter c, or c. */
static int
case_up(int c)
{
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 'A';
    }

    for (size_t i = 0; i < sizeof case_pairs / sizeof case_pairs[0]; i++) {
        if (c == case_pairs[i].lower) {
            return case_pairs[i].upper;
        }
    }

    return c;
}


/* The lower case of the letter c, or c. */
static int
case_down(int c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 'a';
    }

    for (size_t i = 0; i < sizeof case_pairs / sizeof case_pairs[0]; i++) {
        if (c == case_pairs[i].upper) {
            return case_pairs[i].lower;
        }
    }

    return c;
}


/* Is c a letter (it has an upper or a lower case, or is a letter without)? */
static int
case_letter(int c)
{
    /* The accented letters of the code page 437 that have one case only,
       and the sharp s. */
    static const uint8_t lone[] = {0x83, 0x85, 0x88, 0x89, 0x8A, 0x8B, 0x8C,
                                   0x8D, 0x93, 0x95, 0x96, 0x97, 0x98, 0xA0,
                                   0xA1, 0xA2, 0xA3, 0xE1};

    if (case_up(c) != c || case_down(c) != c) {
        return 1;
    }

    for (size_t i = 0; i < sizeof lone; i++) {
        if (c == lone[i]) {
            return 1;
        }
    }

    return 0;
}


int
ed_case_char(int c, int mode, int *in_word)
{
    int letter = case_letter(c);
    int r = c;

    if (mode == CASE_UPPER) {
        r = case_up(c);
    } else if (mode == CASE_LOWER) {
        r = case_down(c);
    } else if (letter != 0) {
        r = *in_word == 0 ? case_up(c) : case_down(c);
    }

    /* A word is letters, digits and apostrophes; the rest ends it. */
    if (letter != 0 || (c >= '0' && c <= '9') || c == '\'') {
        *in_word = 1;
    } else {
        *in_word = 0;
    }

    return r;
}


/* Does the token continue a word (a letter, a digit or an apostrophe)? */
static int
case_tok_in_word(const Tok *k)
{
    int c = k->a;

    return k->kind == TK_CHAR && (case_letter(c) != 0 ||
                                  (c >= '0' && c <= '9') || c == '\'');
}


/* Change the case of the characters a to b - 1 of t; returns how many
   characters changed. */
static int
case_range(Text *t, int a, int b, int mode)
{
    int in_word = 0;
    int changed = 0;

    for (int i = a; i < b; i++) {
        Tok *k = &t->v[i];

        if (k->kind == TK_ATTR) {
            continue; /* an attribute does not end a word */
        }

        if (k->kind != TK_CHAR) {
            in_word = 0;

            continue;
        }

        int r = ed_case_char(k->a, mode, &in_word);

        if (r != k->a) {
            k->a = u8(r);
            changed++;
        }
    }

    return changed;
}


int
ed_case_apply(Editor *e, int mode)
{
    Doc *d = e->doc;
    int kind = e->sel.kind;
    int changed = 0;

    e->sel.kind = SEL_NONE;

    if (kind == SEL_TEXT || kind == SEL_COLUMN) {
        Text *t = ed_text(e);

        if (t == NULL) {
            return -1;
        }

        int a = 0;
        int b = text_last(t);

        if (kind == SEL_TEXT) {
            if (e->sel.anchor.item != e->cur.item ||
                e->sel.anchor.col != e->cur.col) {
                ed_msg(e, "Text selection must stay within one column");

                return -1;
            }

            a = e->sel.anchor.tok < e->cur.tok ? e->sel.anchor.tok :
                                                  e->cur.tok;
            b = e->sel.anchor.tok < e->cur.tok ? e->cur.tok :
                                                  e->sel.anchor.tok;

            if (a == b) {
                /*
                 * Nothing is highlighted: the word under the cursor.  An
                 * attribute does not end a word, and the cursor may stand
                 * before the attribute that begins it.
                 */
                if ((a == 0 || case_tok_in_word(&t->v[a - 1]) == 0) &&
                    (b >= t->n || case_tok_in_word(&t->v[b]) == 0)) {
                    /* Before the first letter of a line there may be
                       tokens that are not characters (an attribute). */
                    int s = b;

                    while (s < t->n && t->v[s].kind != TK_CHAR &&
                           t->v[s].kind != TK_NL) {
                        s++;
                    }

                    if (s < t->n && case_tok_in_word(&t->v[s]) != 0) {
                        a = b = s;
                    }
                }

                while (a > 0 && (case_tok_in_word(&t->v[a - 1]) != 0 ||
                                 t->v[a - 1].kind == TK_ATTR)) {
                    a--;
                }

                while (b < t->n && (case_tok_in_word(&t->v[b]) != 0 ||
                                    t->v[b].kind == TK_ATTR)) {
                    b++;
                }

                int letters = 0;

                for (int i = a; i < b; i++) {
                    letters += case_tok_in_word(&t->v[i]);
                }

                if (letters == 0) {
                    ed_msg(e, "No word under the cursor");

                    return -1;
                }
            }
        }

        changed = case_range(t, a, b, mode);

        if (changed != 0) {
            d->items[e->cur.item].blk->dirty = 1;
        }
    } else {
        int lo;
        int hi;

        e->sel.kind = kind;
        ed_sel_range(e, &lo, &hi);
        e->sel.kind = SEL_NONE;

        for (int i = lo; i <= hi; i++) {
            if (d->items[i].type != IT_BLOCK) {
                continue;
            }

            Block *blk = d->items[i].blk;
            int here = 0;

            for (int c = 0; c < blk->ncols; c++) {
                Text *t = &blk->cols[c].text;

                here += case_range(t, 0, text_last(t), mode);
            }

            if (here != 0) {
                blk->dirty = 1;
                changed += here;
            }
        }
    }

    if (changed == 0) {
        ed_msg(e, "No letter was changed");

        return 0;
    }

    doc_touch(d);
    ed_invalidate(e);
    ed_msg(e, "%s: %d character%s changed",
           mode == CASE_UPPER ? "Upper case" :
           mode == CASE_LOWER ? "Lower case" :
                                "Capitalized",
           changed, changed == 1 ? "" : "s");

    return 0;
}


/* Add the text of a column, as the bytes that calc_eval reads, to buf. */
static void
math_text(const Text *t, int a, int b, char *buf, size_t cap, size_t *len)
{
    for (int i = a; i < b && i < t->n; i++) {
        const Tok *k = &t->v[i];
        char c = ' ';

        if (k->kind == TK_CHAR) {
            c = (char)k->a;
        }

        if (*len + 2 < cap) {
            buf[(*len)++] = c;
        }
    }

    if (*len + 2 < cap) {
        buf[(*len)++] = ' ';
    }
}


int
ed_math_apply(Editor *e)
{
    static char buf[1 << 16];
    char res[800];
    size_t len = 0;
    int kind = e->sel.kind;
    int rc;

    e->sel.kind = SEL_NONE;

    if (kind == SEL_TEXT || kind == SEL_COLUMN) {
        Text *t = ed_text(e);
        int a = 0;
        int b;

        if (t == NULL) {
            return -1;
        }

        b = text_last(t);

        if (kind == SEL_TEXT) {
            if (e->sel.anchor.item != e->cur.item ||
                e->sel.anchor.col != e->cur.col) {
                ed_msg(e, "Text selection must stay within one column");

                return -1;
            }

            a = e->sel.anchor.tok < e->cur.tok ? e->sel.anchor.tok :
                                                  e->cur.tok;
            b = e->sel.anchor.tok < e->cur.tok ? e->cur.tok :
                                                  e->sel.anchor.tok;

            if (a == b) {
                ed_msg(e, "Nothing is highlighted: highlight the numbers to "
                          "calculate");

                return -1;
            }
        }

        math_text(t, a, b, buf, sizeof buf, &len);
    } else {
        int lo;
        int hi;

        e->sel.kind = kind;
        ed_sel_range(e, &lo, &hi);
        e->sel.kind = SEL_NONE;

        for (int i = lo; i <= hi; i++) {
            const Block *blk;

            if (e->doc->items[i].type != IT_BLOCK) {
                continue;
            }

            blk = e->doc->items[i].blk;

            for (int c = 0; c < blk->ncols; c++) {
                math_text(&blk->cols[c].text, 0, blk->cols[c].text.n, buf,
                          sizeof buf, &len);
            }
        }
    }

    rc = calc_eval(buf, len, res, sizeof res);

    if (rc == CALC_DIVZERO) {
        ed_msg(e, "Cannot divide by zero");

        return -1;
    }

    if (rc == CALC_EMPTY) {
        ed_msg(e, "There is no number to calculate");

        return -1;
    }

    if (rc != CALC_OK) {
        ed_msg(e, "Cannot calculate: the expression is not good");

        return -1;
    }

    clip_free(&e->clip);
    e->clip.kind = SEL_TEXT;

    for (const char *q = res; *q != '\0'; q++) {
        Tok k;

        memset(&k, 0, sizeof k);
        k.kind = TK_CHAR;
        k.a = (uint8_t)*q;
        text_insert(&e->clip.text, e->clip.text.n, k);
    }

    ed_msg(e, "Result: %s", res);

    return 0;
}



int
ed_sel_apply(Editor *e, int op)
{
    Doc *d = e->doc;
    int kind = e->sel.kind;

    e->sel.kind = SEL_NONE;

    if (kind == SEL_TEXT || kind == SEL_COLUMN) {
        Text *t = ed_text(e);

        if (t == NULL) {
            return -1;
        }

        int a;
        int b;

        if (kind == SEL_COLUMN) {
            a = 0;
            b = text_last(t);
        } else {
            if (e->sel.anchor.item != e->cur.item ||
                e->sel.anchor.col != e->cur.col) {
                ed_msg(e, "Text "
                          "selection must stay within one column");
                return -1;
            }

            a = e->sel.anchor.tok;
            b = e->cur.tok;

            if (a > b) {
                int x = a;

                a = b;
                b = x;
            }
        }

        if (op != OP_DELETE) {
            clip_free(&e->clip);
            e->clip.kind = SEL_TEXT;

            for (int i = a; i < b; i++) {
                Tok k;

                /* A Print Error is not copied. */
                if (t->v[i].kind == TK_MARKER &&
                    tok_mtype(&t->v[i]) == MT_PRINT_ERROR) {
                    continue;
                }

                k = tok_dup(&t->v[i]);
                text_insert(&e->clip.text, e->clip.text.n, k);
            }
        }

        if (op != OP_COPY) {
            int global = range_global(t, a, b);

            tdel(d, t, a, b - a);
            e->cur.tok = a;

            if (global == 0) {
                touch_char(e);
            } else {
                touch(e);
            }
        }

        ed_msg(e, op == OP_COPY ? "Copied" : op == OP_CUT ? "Cut" : "Deleted");
        return 0;
    }

    int lo;
    int hi;

    e->sel.kind = kind;
    ed_sel_range(e, &lo, &hi);
    e->sel.kind = SEL_NONE;

    if (op == OP_SORT) {
        if (kind != SEL_BLOCK) {
            ed_msg(e, "Autosort sorts blocks");

            return -1;
        }

        for (int i = lo; i <= hi; i++) {
            if (d->items[i].type != IT_BLOCK) {
                ed_msg(e, "Block selection must stay within one section");

                return -1;
            }
        }

        int n = sort_blocks(e, lo, hi);

        ed_msg(e, "Sorted %d blocks", n);

        return 0;
    }

    if (kind == SEL_BLOCK) {
        for (int i = lo; i <= hi; i++) {
            if (d->items[i].type != IT_BLOCK) {
                ed_msg(e, "Block selection "
                          "must stay within one section");
                return -1;
            }
        }
    } else {
        if (d->structured == 0) {
            ed_msg(e, "Sections exist only in structured "
                      "documents");
            return -1;
        }

        if (range_balanced(d, lo, hi) == 0) {
            ed_msg(e, "Section selection must "
                      "cover peer sections");
            return -1;
        }

        /* never delete the root section */
        if (lo == 0) {
            ed_msg(e, "Cannot take the whole document as a section");
            return -1;
        }
    }

    if (op != OP_DELETE) {
        clip_free(&e->clip);
        e->clip.kind = kind;
        e->clip.n = hi - lo + 1;
        e->clip.items =
            calloc(usz(e->clip.n > 0 ? e->clip.n : 1), sizeof(Item));

        for (int i = lo; i <= hi; i++) {
            e->clip.items[i - lo].type = d->items[i].type;
            e->clip.items[i - lo].blk = d->items[i].blk != NULL ?
                                            block_clone_full(d->items[i].blk) :
                                            NULL;
        }
    }

    if (op != OP_COPY) {
        int s = sect_start(d, lo);
        int local = kind == SEL_BLOCK && items_local(d, lo, hi - lo + 1) != 0;

        doc_delete_items(d, lo, hi - lo + 1);
        e->cur.item = lo > 0 ? lo - 1 : 0;
        view_deleted(e, lo, hi - lo + 1, local);

        if (kind == SEL_BLOCK && s >= 0 && sect_blocks_end(d, s) == s + 1) {
            /* keep the section valid */
            doc_insert_item(d, s + 1, IT_BLOCK, block_empty(1));
            view_inserted(e, s + 1, 1);
        }

        e->cur.col = 0;
        e->cur.tok = 0;
        ed_fix_cursor(e);
    }

    ed_msg(e, op == OP_COPY ? "Copied" : op == OP_CUT ? "Cut" : "Deleted");

    return 0;
}


int
ed_paste(Editor *e)
{
    Doc *d = e->doc;
    Clip *c = &e->clip;

    if (c->kind == SEL_NONE) {
        ed_msg(e, "Nothing to paste");
        return -1;
    }

    if (c->kind == SEL_TEXT) {
        for (int i = 0; i < c->text.n; i++) {
            Tok k;
            Text *t = ed_text(e);

            if (t == NULL) {
                return -1;
            }

            /* A pasted Print Error is dropped. */
            if (c->text.v[i].kind == TK_MARKER &&
                tok_mtype(&c->text.v[i]) == MT_PRINT_ERROR) {
                continue;
            }

            k = tok_dup(&c->text.v[i]);

            tins(d, t, e->cur.tok, k);
            e->cur.tok++;
        }

        if (range_global(&c->text, 0, c->text.n) == 0) {
            touch_char(e);
        } else {
            touch(e);
        }

        return 0;
    }

    int at;

    if (c->kind == SEL_BLOCK) {
        at = e->cur.item + 1;
    } else {
        int s = sect_start(d, e->cur.item);

        at = s < 0 ? d->nitems : sect_end(d, s);
    }

    for (int i = 0; i < c->n; i++) {
        Block *nb = c->items[i].blk != NULL ?
                        block_clone_full(c->items[i].blk) :
                        NULL;

        /* A copy of a block has no Print Error in it. */
        if (nb != NULL) {
            perr_strip_block(nb);
        }

        doc_insert_item(d, at + i, c->items[i].type, nb);
    }

    for (int i = at; i < at + c->n; i++) {
        if (is_edit_block(d, i) != 0) {
            e->cur.item = i;
            break;
        }
    }

    e->cur.col = 0;
    e->cur.tok = 0;

    if (c->kind == SEL_BLOCK) {
        view_inserted(e, at, c->n);
    } else {
        ed_invalidate(e);
    }

    ed_fix_cursor(e);

    return 0;
}


void
ed_clip_move(Editor *from, Editor *to)
{
    if (from->clip.kind == SEL_NONE || from == to) {
        return;
    }

    clip_free(&to->clip);
    to->clip = from->clip;
    memset(&from->clip, 0, sizeof from->clip);
}


/* ------------------------------------------------------------------- move */
/* The + that opens the sections that item s belongs to, or -1. */
static int
parent_plus(const Doc *d, int s)
{
    int depth = 0;

    for (int i = s - 1; i >= 0; i--) {
        if (d->items[i].type == IT_MINUS) {
            depth++;
        } else if (d->items[i].type == IT_PLUS) {
            if (depth == 0) {
                return i;
            }

            depth--;
        }
    }

    return -1;
}


/* The section before section lo, with the same parent, or -1. */
static int
prev_peer(const Doc *d, int lo)
{
    int k = lo - 1;
    int depth = 0;

    if (k < 0 || d->items[k].type == IT_PLUS) {
        return -1;
    }

    if (d->items[k].type == IT_BLOCK) {
        return sect_start(d, k);
    }

    for (int i = k; i >= 0; i--) {
        if (d->items[i].type == IT_MINUS) {
            depth++;
        } else if (d->items[i].type == IT_PLUS && --depth == 0) {
            return i > 0 ? sect_start(d, i - 1) : -1;
        }
    }

    return -1;
}


/* Take away every + that is followed by its - at once: it has no sections. */
static void
drop_empty_levels(Doc *d, int *keep)
{
    for (int i = 0; i + 1 < d->nitems; i++) {
        if (d->items[i].type == IT_PLUS && d->items[i + 1].type == IT_MINUS) {
            doc_delete_items(d, i, 2);

            if (i < *keep) {
                *keep -= 2;
            }

            i--;
        }
    }
}


/* The place of the highlight after the items lo to hi went to newlo. */
static void
move_follow(Editor *e, int lo, int hi, int newlo)
{
    if (e->cur.item >= lo && e->cur.item <= hi) {
        e->cur.item += newlo - lo;
    }

    if (e->sel.anchor.item >= lo && e->sel.anchor.item <= hi) {
        e->sel.anchor.item += newlo - lo;
    }

    ed_invalidate(e);
    ed_fix_cursor(e);
}


/* One step of Move for the highlighted sections lo to hi (peers). */
static int
move_sections(Editor *e, int dir, int lo, int hi)
{
    Doc *d = e->doc;
    int n = hi - lo + 1;
    int pl = parent_plus(d, lo);
    int ps = pl > 0 ? sect_start(d, pl - 1) : -1;
    int lvl = doc_item_level(d, lo);
    int pp = prev_peer(d, lo);
    int np = hi + 1 < d->nitems && d->items[hi + 1].type == IT_SECT ?
                 hi + 1 :
                 -1;
    int to = -1;
    int wrap = 0;
    int newlo;

    if (pl < 0 || ps < 0) {
        ed_msg(e, "The section cannot move");

        return -1;
    }

    switch (dir) {
        case MOVE_UP:
            to = pp >= 0 ? pp : lvl >= 2 ? ps : -1;
            break;

        case MOVE_DOWN:
            to = np >= 0 ? sect_end(d, np) - n :
                 lvl >= 2 ? sect_end(d, ps) - n :
                            -1;
            break;

        case MOVE_HOME:
            to = pp >= 0 ? pl + 1 : -1;
            break;

        case MOVE_END:
            to = np >= 0 ? match_minus(d, pl) - n : -1;
            break;

        case MOVE_PGUP:
            for (int i = lo - 1; i >= 1 && to < 0; i--) {
                if (d->items[i].type == IT_SECT && doc_item_level(d, i) == lvl) {
                    to = i;
                }
            }

            break;

        case MOVE_PGDN:
            for (int i = hi + 1; i < d->nitems && to < 0; i++) {
                if (d->items[i].type == IT_SECT && doc_item_level(d, i) == lvl) {
                    to = sect_end(d, i) - n;
                }
            }

            break;

        case MOVE_LEFT:
            to = lvl >= 2 ? sect_end(d, ps) - n : -1;
            break;

        case MOVE_RIGHT:
            if (pp >= 0 && sect_blocks_end(d, pp) < d->nitems &&
                d->items[sect_blocks_end(d, pp)].type == IT_PLUS) {
                to = match_minus(d, sect_blocks_end(d, pp));
            } else if (pp >= 0) {
                to = sect_blocks_end(d, pp);
                wrap = 1;
            }

            break;

        default:
            break;
    }

    if (to < 0 || (to == lo && wrap == 0)) {
        ed_msg(e, "The section cannot move farther");

        return -1;
    }

    doc_move_items(d, lo, hi, to);
    newlo = to;

    if (wrap != 0) {
        doc_insert_item(d, newlo, IT_PLUS, NULL);
        doc_insert_item(d, newlo + 1 + n, IT_MINUS, NULL);
        newlo++;
    }

    drop_empty_levels(d, &newlo);
    move_follow(e, lo, hi, newlo);

    return 0;
}


/* One step of Move for the highlighted blocks lo to hi, in one section. */
static int
move_blocks(Editor *e, int dir, int lo, int hi)
{
    Doc *d = e->doc;
    int s = sect_start(d, lo);
    int n = hi - lo + 1;
    int first = s >= 0 ? s + 1 : 0;
    int end = s >= 0 ? sect_blocks_end(d, s) : lo;
    int to = -1;

    if (s < 0) {
        while (end < d->nitems && d->items[end].type == IT_BLOCK) {
            end++;
        }
    }

    /* The headline of a section stays the first block of it. */
    if (d->structured != 0 && s >= 0) {
        if (lo == first) {
            ed_msg(e, "The headline of a section cannot move");

            return -1;
        }

        first++;
    }

    switch (dir) {
        case MOVE_UP:
            to = lo > first ? lo - 1 : -1;
            break;

        case MOVE_DOWN:
            to = hi + 1 < end ? lo + 1 : -1;
            break;

        case MOVE_HOME:
            to = lo > first ? first : -1;
            break;

        case MOVE_END:
            to = hi + 1 < end ? end - n : -1;
            break;

        default:
            ed_msg(e, "Up, Down, Home and End move blocks");

            return -1;
    }

    if (to < 0) {
        ed_msg(e, "The block cannot move farther");

        return -1;
    }

    doc_move_items(d, lo, hi, to);
    move_follow(e, lo, hi, to);

    return 0;
}


/* One step of Move for the highlighted column: its text swaps with the next. */
static int
move_column(Editor *e, int dir)
{
    Doc *d = e->doc;
    Block *b = d->items[e->cur.item].blk;
    int c = e->cur.col;
    int to;

    if (b == NULL || b->ncols < 2) {
        ed_msg(e, "The block has one column only");

        return -1;
    }

    if (dir == MOVE_LEFT) {
        to = c - 1;
    } else if (dir == MOVE_RIGHT) {
        to = c + 1;
    } else if (dir == MOVE_HOME) {
        to = 0;
    } else if (dir == MOVE_END) {
        to = b->ncols - 1;
    } else {
        ed_msg(e, "Left, Right, Home and End move columns");

        return -1;
    }

    if (to < 0 || to >= b->ncols || to == c) {
        ed_msg(e, "The column cannot move farther");

        return -1;
    }

    /* The text goes through the columns between, one place at a time. */
    for (int i = c; i != to; i += to > c ? 1 : -1) {
        Text t = b->cols[i].text;
        int j = i + (to > c ? 1 : -1);

        b->cols[i].text = b->cols[j].text;
        b->cols[j].text = t;
    }

    b->dirty = 1;
    doc_touch(d);
    e->cur.col = to;
    e->cur.tok = 0;
    ed_invalidate(e);
    ed_fix_cursor(e);

    return 0;
}


int
ed_move_step(Editor *e, int dir)
{
    Doc *d = e->doc;
    int kind = e->sel.kind;
    int lo;
    int hi;

    if (kind == SEL_COLUMN) {
        return move_column(e, dir);
    }

    if (e->only_s >= 0) {
        ed_msg(e, "Leave Section edit before moving blocks or sections");

        return -1;
    }

    ed_sel_range(e, &lo, &hi);

    if (kind == SEL_BLOCK) {
        for (int i = lo; i <= hi; i++) {
            if (d->items[i].type != IT_BLOCK) {
                ed_msg(e, "Block selection must stay within one section");

                return -1;
            }
        }

        return move_blocks(e, dir, lo, hi);
    }

    if (kind != SEL_SECTION || d->structured == 0) {
        ed_msg(e, "Sections exist only in structured documents");

        return -1;
    }

    if (range_balanced(d, lo, hi) == 0 || lo == 0) {
        ed_msg(e, "Section selection must cover peer sections");

        return -1;
    }

    return move_sections(e, dir, lo, hi);
}


/* ----------------------------------------------------------------- search */
static int
tok_ch(const Tok *k)
{
    if (k->kind == TK_CTRL && k->a == NBSP_CTRL) {
        return NBSP_BYTE;
    }

    return k->kind == TK_CHAR ? k->a :
           k->kind == TK_TAB  ? ' ' :
           k->kind == TK_NL   ? '\n' :
                                -1;
}


/* A column of an edit block, in the order of the document. */
typedef struct {
    int item;
    int col;
} SrchCell;


/* The columns of all the edit blocks, in order; the caller frees them. */
static SrchCell *
cells_of(const Doc *d, int *n)
{
    int total = 0;
    int ncells = 1;

    for (int i = 0; i < d->nitems; i++) {
        if (is_edit_block(d, i) != 0) {
            ncells += d->items[i].blk->ncols;
        }
    }

    SrchCell *c = malloc(sizeof(SrchCell) * usz(ncells));

    for (int i = 0; i < d->nitems; i++) {
        if (is_edit_block(d, i) != 0) {
            for (int k = 0; k < d->items[i].blk->ncols; k++) {
                c[total].item = i;
                c[total].col = k;
                total++;
            }
        }
    }

    *n = total;

    return c;
}


static Text *
cell_text(const Doc *d, const SrchCell *c)
{
    return &d->items[c->item].blk->cols[c->col].text;
}


int
ed_search_spec(Editor *e, const SearchSpec *s, int forward)
{
    Doc *d = e->doc;
    int total;
    SrchCell *ic = cells_of(d, &total);
    int start = 0;
    int found = 0;

    e->search_wrapped = 0;

    if (total == 0) {
        free(ic);

        return 0;
    }

    for (int i = 0; i < total; i++) {
        if (ic[i].item == e->cur.item && ic[i].col == e->cur.col) {
            start = i;
        }
    }

    for (int step = 0; step <= total && found == 0; step++) {
        int idx = forward != 0 ? (start + step) % total :
                                 ((start - step) % total + total) % total;
        Text *t = cell_text(d, &ic[idx]);
        int from;
        int len;

        if (forward != 0) {
            from = (step == 0) ? e->cur.tok + 1 : 0;
        } else {
            from = (step == 0) ? e->cur.tok - 1 : t->n - 1;
        }

        int i = search_text_find(t, from, forward, s, &len);

        if (i >= 0) {
            e->cur.item = ic[idx].item;
            e->cur.col = ic[idx].col;
            e->cur.tok = i;
            found = 1;
            /* Past the end (forward) or the beginning (backward). */
            e->search_wrapped = forward != 0 ? start + step >= total :
                                               step > start;
        }
    }

    free(ic);

    if (found != 0) {
        ed_invalidate(e);
        ed_layout(e);

        if (e->view.bv[e->cur.item].visible == 0) {
            unfold_all(e);
            ed_layout(e);
        }
    }

    return found;
}


int
ed_search(Editor *e, const char *nd, int forward, int cf)
{
    SearchSpec s;

    if (nd[0] == '\0') {
        return 0;
    }

    memset(&s, 0, sizeof s);
    s.o.find = SF_TEXT;
    s.o.match_case = cf == 0;
    s.pattern = nd;

    return ed_search_spec(e, &s, forward);
}


int
ed_find_range(Editor *e, const SearchSpec *s, const Pos *start,
              const Pos *limit, Pos *at, int *len)
{
    Doc *d = e->doc;
    int total;
    SrchCell *ic = cells_of(d, &total);
    int found = 0;
    int first = 0;

    /* The first column that is not before the start. */
    while (first < total && (ic[first].item < start->item ||
                             (ic[first].item == start->item &&
                              ic[first].col < start->col))) {
        first++;
    }

    for (int idx = first; idx < total && found == 0; idx++) {
        Text *t = cell_text(d, &ic[idx]);
        int same = ic[idx].item == start->item && ic[idx].col == start->col;
        int past = 0;
        int i;

        /* A column after the one of the limit is out of the range. */
        if (limit != NULL && (ic[idx].item > limit->item ||
                              (ic[idx].item == limit->item &&
                               ic[idx].col > limit->col))) {
            break;
        }

        i = search_text_find(t, same != 0 ? start->tok : 0, 1, s, len);

        /* In the column of the limit only a match before it counts. */
        if (i >= 0 && limit != NULL && ic[idx].item == limit->item &&
            ic[idx].col == limit->col && i >= limit->tok) {
            past = 1;
        }

        if (i >= 0 && past == 0) {
            at->item = ic[idx].item;
            at->col = ic[idx].col;
            at->tok = i;
            found = 1;
        }

        if (past != 0) {
            break;
        }
    }

    free(ic);

    return found;
}


int
ed_find_from(Editor *e, const SearchSpec *s, const Pos *start, Pos *at,
             int *len)
{
    return ed_find_range(e, s, start, NULL, at, len);
}


void
ed_goto(Editor *e, int item, int col, int tok)
{
    Doc *d = e->doc;

    if (item < 0 || item >= d->nitems) {
        return;
    }

    e->sel.kind = SEL_NONE;

    /* Edit, Section shows one section only: leave it for a place outside. */
    if (e->only_s >= 0 &&
        (item < e->only_s || item >= sect_end(d, e->only_s))) {
        e->only_s = -1;
    }

    e->cur.item = item;
    e->cur.col = col;
    e->cur.tok = tok;
    ed_invalidate(e);
    ed_layout(e);

    if (item < e->view.nbv && e->view.bv[item].visible == 0) {
        unfold_all(e);
        ed_layout(e);
    }

    ed_fix_cursor(e);
}


/* Replace the len tokens at tok of t by the string rep; return its length. */
static int
put_replacement(Doc *d, Text *t, int tok, int len, const char *rep)
{
    int lb = slen(rep);

    tdel(d, t, tok, len);

    for (int j = 0; j < lb; j++) {
        text_insert(t, tok + j,
                    u8(rep[j]) == NBSP_BYTE ? tok_nbsp() :
                                              tok_char(u8(rep[j])));
    }

    return lb;
}


int
ed_replace_at(Editor *e, const Pos *at, int len, const char *rep, int follow,
              Pos *after)
{
    Doc *d = e->doc;
    Text *t = &d->items[at->item].blk->cols[at->col].text;
    char buf[160];

    search_replacement(t, at->tok, len, rep, follow, buf, sizeof buf);

    int lb = put_replacement(d, t, at->tok, len, buf);

    d->items[at->item].blk->dirty = 1;
    doc_touch(d);
    ed_invalidate(e);
    *after = *at;
    after->tok = at->tok + lb;

    return lb;
}


int
ed_replace_all_spec(Editor *e, const SearchSpec *s, const char *rep,
                    int follow)
{
    Doc *d = e->doc;
    int total;
    SrchCell *ic = cells_of(d, &total);
    int count = 0;

    for (int idx = 0; idx < total; idx++) {
        Text *t = cell_text(d, &ic[idx]);
        int from = 0;
        int len;
        int i;

        while ((i = search_text_find(t, from, 1, s, &len)) >= 0) {
            char buf[160];

            search_replacement(t, i, len, rep, follow, buf, sizeof buf);
            from = i + put_replacement(d, t, i, len, buf);
            count++;
            d->items[ic[idx].item].blk->dirty = 1;
            doc_touch(d);
        }
    }

    free(ic);

    if (count != 0) {
        ed_invalidate(e);
        ed_fix_cursor(e);
    }

    return count;
}


int
ed_replace_all(Editor *e, const char *a, const char *b, int cf)
{
    SearchSpec s;

    if (a[0] == '\0') {
        return 0;
    }

    memset(&s, 0, sizeof s);
    s.o.find = SF_TEXT;
    s.o.match_case = cf == 0;
    s.pattern = a;

    return ed_replace_all_spec(e, &s, b, 0);
}


/* ----------------------------------------------------------------- stats */
void
ed_doc_stats(const Doc *d, int *words, int *chars, int *blocks, int *sections)
{
    *words = *chars = *blocks = *sections = 0;
    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == IT_SECT) {
            (*sections)++;
        }

        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        (*blocks)++;

        for (int c = 0; c < d->items[i].blk->ncols; c++) {
            const Text *t = &d->items[i].blk->cols[c].text;
            int in = 0;

            for (int k = 0; k < t->n; k++) {
                int ch = tok_ch(&t->v[k]);

                /* A soft hyphen is no part of the text: a word goes on. */
                if (t->v[k].kind == TK_CTRL && t->v[k].a == SHY_CTRL) {
                    continue;
                }

                if (t->v[k].kind == TK_CHAR) {
                    (*chars)++;
                }

                if (ch >= 0 && ch != NBSP_BYTE && !is_space(ch) &&
                    ch != '\n') {
                    if (in == 0) {
                        (*words)++;
                        in = 1;
                    }
                } else {
                    in = 0;
                }
            }
        }
    }
}
