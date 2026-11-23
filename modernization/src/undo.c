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

/* undo.c - Undo and Redo: see undo.h for the design. */
#include "undo.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

/* One operation: the swap of a range of items, and of the header. */
typedef struct {
    int lo; /* the first item of the range */
    int n_live; /* how many items of the document the range has now */
    Item *items; /* the items of the other side (owned) */
    int n_other;
    int has_hdr; /* the header and the structured flag are swapped too */
    Lines hdr;
    int structured;
    Pos cur_before; /* where the cursor was before the operation */
    Pos cur_after; /* where it was after it */
    char label[32];
    int typing; /* the class of the key of a grouped run of typing, or 0 */
    int first; /* the first patch of an operation; more may follow it */
    size_t bytes;
} Patch;

typedef struct Undo Undo;

/*
 * An item of the shadow.  The shadow is a copy of the whole document, and a
 * document of 4000 pages has 12 million tokens of 4 bytes: the text of a
 * column that holds no marker is therefore kept packed, at one byte for a
 * plain character (see pack_text), and unpacked only when the item goes into
 * a patch.  A packed column has text.v NULL in it.blk and its bytes in pk.
 */
typedef struct {
    uint8_t *s;
    size_t n;
} Packed;

typedef struct {
    Item it;
    Packed *pk; /* one for each column of it.blk; s NULL: not packed */
} ShItem;

struct Undo {
    ShItem *sh; /* the shadow: the document as it was after the last key */
    int shn;
    int shcap;
    Lines shhdr;
    int shstruct;
    Patch *p;
    int np; /* the patches: [0, pos) can be undone, [pos, np) redone */
    int pcap;
    int pos;
    int saved_pos; /* the position of the saved state, -1 when it is lost */
    unsigned gen; /* the generation of the document at the last look */
    int last_typing; /* the last key was grouped typing */
    int break_after; /* the next typing starts a new operation */
    size_t bytes; /* the bytes of the history */
};

static int levels_set = UNDO_DEFAULT_LEVELS;


int
undo_clamp_levels(int n)
{
    if (n < 0) {
        return 0;
    }

    return n > UNDO_MAX_LEVELS ? UNDO_MAX_LEVELS : n;
}


void
undo_set_levels(int n)
{
    levels_set = undo_clamp_levels(n);
}


int
undo_get_levels(void)
{
    return levels_set;
}


/* ---- copies and comparisons -------------------------------------------- */
static int
lines_eq(const Lines *a, const Lines *b)
{
    if (a->n != b->n) {
        return 0;
    }

    for (int i = 0; i < a->n; i++) {
        if (a->v[i].n != b->v[i].n ||
            memcmp(a->v[i].s, b->v[i].s, usz(a->v[i].n)) != 0) {
            return 0;
        }
    }

    return 1;
}


static void
lines_dup(Lines *dst, const Lines *src)
{
    memset(dst, 0, sizeof *dst);

    for (int i = 0; i < src->n; i++) {
        lines_add(dst, src->v[i].s, usz(src->v[i].n));
    }
}


static int
tok_eq(const Tok *a, const Tok *b)
{
    if (a->kind != b->kind) {
        return 0;
    }

    if (a->kind == TK_MARKER) {
        size_t n = tok_mlen(a);

        return tok_mtype(a) == tok_mtype(b) && n == tok_mlen(b) &&
               memcmp(tok_mdata(a), tok_mdata(b), n) == 0;
    }

    return a->esc == b->esc && a->a == b->a && a->b == b->b;
}


/*
 * The packed form of a text: a plain character (kind TK_CHAR, no esc, b 0,
 * a not PACK_ESC) is its byte; any other token that is no marker is PACK_ESC
 * and its four bytes.  A text with a marker is not packed (a marker token is
 * an entry of a table, and tok_eq compares what it holds).
 */
#define PACK_ESC 0xFF

static int
pack_plain(const Tok *k)
{
    return k->kind == TK_CHAR && k->esc == 0 && k->b == 0 &&
           k->a != PACK_ESC;
}


static int
pack_text(const Text *t, Packed *out)
{
    size_t n = 0;
    size_t j = 0;

    out->s = NULL;
    out->n = 0;

    for (int i = 0; i < t->n; i++) {
        if (t->v[i].kind == TK_MARKER) {
            return 0;
        }

        n += pack_plain(&t->v[i]) != 0 ? 1 : 5;
    }

    out->s = malloc(n > 0 ? n : 1);

    if (out->s == NULL) {
        return 0;
    }

    for (int i = 0; i < t->n; i++) {
        const Tok *k = &t->v[i];

        if (pack_plain(k) != 0) {
            out->s[j++] = k->a;
        } else {
            out->s[j++] = PACK_ESC;
            out->s[j++] = k->kind;
            out->s[j++] = k->esc;
            out->s[j++] = k->a;
            out->s[j++] = k->b;
        }
    }

    out->n = n;

    return 1;
}


/* Does the text of the document hold what the packed bytes hold? */
static int
packed_eq(const Text *t, const Packed *p)
{
    size_t j = 0;

    for (int i = 0; i < t->n; i++) {
        const Tok *k = &t->v[i];

        if (k->kind == TK_MARKER) {
            return 0;
        }

        if (pack_plain(k) != 0) {
            if (j >= p->n || p->s[j] != k->a) {
                return 0;
            }

            j++;
        } else {
            if (j + 5 > p->n || p->s[j] != PACK_ESC || p->s[j + 1] != k->kind ||
                p->s[j + 2] != k->esc || p->s[j + 3] != k->a ||
                p->s[j + 4] != k->b) {
                return 0;
            }

            j += 5;
        }
    }

    return j == p->n;
}


/* The tokens of a packed text, in the exact room that they need. */
static void
unpack_text(const Packed *p, Text *t)
{
    size_t j = 0;
    int n = t->n;

    t->v = malloc(usz(n > 0 ? n : 1) * sizeof *t->v);
    t->cap = n > 0 ? n : 1;

    for (int i = 0; i < n; i++) {
        Tok k;

        memset(&k, 0, sizeof k);

        if (p->s[j] != PACK_ESC) {
            k.kind = TK_CHAR;
            k.a = p->s[j++];
        } else {
            k.kind = p->s[j + 1];
            k.esc = p->s[j + 2];
            k.a = p->s[j + 3];
            k.b = p->s[j + 4];
            j += 5;
        }

        t->v[i] = k;
    }
}


/* The shadow item for an item of the document. */
static ShItem
sh_make(const Item *s)
{
    ShItem d;

    memset(&d, 0, sizeof d);
    d.it = *s;

    if (s->blk == NULL) {
        return d;
    }

    d.it.blk = block_new(0);
    d.pk = calloc(usz(s->blk->ncols > 0 ? s->blk->ncols : 1), sizeof *d.pk);

    for (int i = 0; i < s->blk->props.n; i++) {
        lines_add(&d.it.blk->props, s->blk->props.v[i].s,
                  usz(s->blk->props.v[i].n));
    }

    for (int c = 0; c < s->blk->ncols; c++) {
        const Column *sc = &s->blk->cols[c];
        Column *dc = block_add_column(d.it.blk);

        dc->width = sc->width;

        for (int i = 0; i < sc->props.n; i++) {
            lines_add(&dc->props, sc->props.v[i].s, usz(sc->props.v[i].n));
        }

        if (pack_text(&sc->text, &d.pk[c]) != 0) {
            dc->text.n = sc->text.n;
        } else {
            dc->text.v = malloc(usz(sc->text.n > 0 ? sc->text.n : 1) *
                                sizeof *dc->text.v);
            dc->text.cap = sc->text.n > 0 ? sc->text.n : 1;

            for (int i = 0; i < sc->text.n; i++) {
                dc->text.v[i] = tok_dup(&sc->text.v[i]);
            }

            dc->text.n = sc->text.n;
        }
    }

    d.it.blk->dirty = s->blk->dirty;
    d.it.blk->sid = s->blk->sid;

    return d;
}


static void
sh_free(ShItem *s)
{
    if (s->it.blk != NULL) {
        for (int c = 0; c < s->it.blk->ncols; c++) {
            free(s->pk[c].s);

            /* A packed column has no tokens to give back. */
            if (s->pk[c].s != NULL) {
                s->it.blk->cols[c].text.n = 0;
            }
        }

        block_free(s->it.blk);
    }

    free(s->pk);
    memset(s, 0, sizeof *s);
}


/* The item of the shadow as an item that has its tokens (for a patch). */
static Item
sh_unpack(ShItem *s)
{
    Item it = s->it;

    if (it.blk != NULL) {
        for (int c = 0; c < it.blk->ncols; c++) {
            if (s->pk[c].s != NULL) {
                unpack_text(&s->pk[c], &it.blk->cols[c].text);
                free(s->pk[c].s);
            }
        }
    }

    free(s->pk);
    memset(s, 0, sizeof *s);

    return it;
}


/* Is the item of the document what the shadow item holds? */
static int
sh_eq(const Item *a, const ShItem *s)
{
    const Block *x = a->blk;
    const Block *y = s->it.blk;

    if (a->type != s->it.type || a->fold != s->it.fold || a->se != s->it.se) {
        return 0;
    }

    if (x == NULL || y == NULL) {
        return x == y;
    }

    if (x->ncols != y->ncols || lines_eq(&x->props, &y->props) == 0) {
        return 0;
    }

    for (int c = 0; c < x->ncols; c++) {
        const Column *p = &x->cols[c];
        const Column *q = &y->cols[c];

        if (p->width != q->width || p->text.n != q->text.n ||
            lines_eq(&p->props, &q->props) == 0) {
            return 0;
        }

        if (s->pk[c].s != NULL) {
            if (packed_eq(&p->text, &s->pk[c]) == 0) {
                return 0;
            }
        } else {
            for (int i = 0; i < p->text.n; i++) {
                if (tok_eq(&p->text.v[i], &q->text.v[i]) == 0) {
                    return 0;
                }
            }
        }
    }

    return 1;
}


static size_t
lines_bytes(const Lines *l)
{
    size_t n = sizeof *l;

    for (int i = 0; i < l->n; i++) {
        n += usz(l->v[i].n) + sizeof(Str);
    }

    return n;
}


static size_t
item_bytes(const Item *it)
{
    size_t n = sizeof *it;

    if (it->blk == NULL) {
        return n;
    }

    n += sizeof *it->blk + lines_bytes(&it->blk->props);

    for (int c = 0; c < it->blk->ncols; c++) {
        n += sizeof(Column) + lines_bytes(&it->blk->cols[c].props) +
             usz(it->blk->cols[c].text.cap) * sizeof(Tok);
    }

    return n;
}


static size_t
items_bytes(const Item *v, int n)
{
    size_t t = 0;

    for (int i = 0; i < n; i++) {
        t += item_bytes(&v[i]);
    }

    return t;
}


static void
items_free(Item *v, int n)
{
    for (int i = 0; i < n; i++) {
        block_free(v[i].blk);
        v[i].blk = NULL;
    }
}


/* ---- the shadow --------------------------------------------------------- */
static void
sh_reserve(Undo *u, int n)
{
    if (n > u->shcap) {
        int cap = n + n / 4 + 16;

        u->sh = realloc(u->sh, usz(cap) * sizeof *u->sh);
        u->shcap = cap;
    }
}


/*
 * Replace nold items of the shadow, from lo, by copies of nnew items of the
 * document.  The old items are moved to out when out is not NULL, and freed
 * when it is.
 */
static void
sh_replace(Undo *u, int lo, int nold, const Item *live, int nnew, Item *out)
{
    int tail = u->shn - lo - nold;

    for (int i = 0; i < nold; i++) {
        if (out != NULL) {
            out[i] = sh_unpack(&u->sh[lo + i]);
        } else {
            sh_free(&u->sh[lo + i]);
        }
    }

    sh_reserve(u, u->shn - nold + nnew);
    memmove(u->sh + lo + nnew, u->sh + lo + nold, usz(tail) * sizeof *u->sh);

    for (int i = 0; i < nnew; i++) {
        u->sh[lo + i] = sh_make(&live[i]);
    }

    u->shn = u->shn - nold + nnew;
}


static void
sh_set_header(Undo *u, const Doc *d)
{
    lines_free(&u->shhdr);
    lines_dup(&u->shhdr, &d->header);
    u->shstruct = d->structured;
}


static Undo *
undo_make(Editor *e)
{
    Undo *u = calloc(1, sizeof *u);
    const Doc *d = e->doc;

    sh_reserve(u, d->nitems);

    for (int i = 0; i < d->nitems; i++) {
        u->sh[i] = sh_make(&d->items[i]);
    }

    u->shn = d->nitems;
    sh_set_header(u, d);
    u->gen = d->gen;
    u->saved_pos = d->modified != 0 ? -1 : 0;

    return u;
}


static void
patch_free(Patch *p)
{
    items_free(p->items, p->n_other);
    free(p->items);
    lines_free(&p->hdr);
    memset(p, 0, sizeof *p);
}


static void
undo_drop(Undo *u)
{
    if (u == NULL) {
        return;
    }

    for (int i = 0; i < u->np; i++) {
        patch_free(&u->p[i]);
    }

    for (int i = 0; i < u->shn; i++) {
        sh_free(&u->sh[i]);
    }

    free(u->sh);
    lines_free(&u->shhdr);
    free(u->p);
    free(u);
}


void
undo_free(Editor *e)
{
    undo_drop(e->undo);
    e->undo = NULL;
}


void
undo_reset(Editor *e)
{
    undo_free(e);
}


/* ---- the history -------------------------------------------------------- */
static void
patch_size(Undo *u, Patch *p)
{
    size_t b = items_bytes(p->items, p->n_other) + lines_bytes(&p->hdr);

    u->bytes = u->bytes - p->bytes + b;
    p->bytes = b;
}


/* The number of operations in the patches [from, to). */
static int
count_ops(const Undo *u, int from, int to)
{
    int n = 0;

    for (int i = from; i < to; i++) {
        n += u->p[i].first != 0;
    }

    return n;
}


/* Forget the oldest operation: its patches are the first ones. */
static void
drop_oldest(Undo *u)
{
    int k = 1;

    while (k < u->pos && u->p[k].first == 0) {
        k++;
    }

    for (int i = 0; i < k; i++) {
        u->bytes -= u->p[i].bytes;
        patch_free(&u->p[i]);
    }

    memmove(&u->p[0], &u->p[k], usz(u->np - k) * sizeof u->p[0]);
    u->np -= k;
    u->pos -= k;
    u->saved_pos = u->saved_pos >= k ? u->saved_pos - k : -1;
}


/* Keep at most the number of levels, and at most the memory limit. */
static void
trim_history(Undo *u)
{
    for (;;) {
        int ops = count_ops(u, 0, u->pos);

        if (u->pos == 0 ||
            (ops <= levels_set && (u->bytes <= UNDO_MAX_BYTES || ops <= 1))) {
            break;
        }

        drop_oldest(u);
    }
}


static void
clear_redo(Undo *u)
{
    for (int i = u->pos; i < u->np; i++) {
        u->bytes -= u->p[i].bytes;
        patch_free(&u->p[i]);
    }

    u->np = u->pos;

    if (u->saved_pos > u->pos) {
        u->saved_pos = -1;
    }
}


static Patch *
push_patch(Undo *u)
{
    if (u->np + 1 > u->pcap) {
        u->pcap = u->pcap * 2 + 8;
        u->p = realloc(u->p, usz(u->pcap) * sizeof *u->p);
    }

    memset(&u->p[u->np], 0, sizeof u->p[u->np]);
    u->np++;
    u->pos++;

    return &u->p[u->np - 1];
}


/* ---- comparing the document with the shadow ----------------------------- */
/*
 * Find the range that changed.  With fast set, the items before lo and the
 * items after hi are known to be the same, so that a typed character costs
 * the look at the items next to the cursor only.
 */
static void
find_range(const Undo *u, const Doc *d, int fast, int lo, int hi, int *pp,
           int *ps)
{
    int nl = d->nitems;
    int ns = u->shn;
    int least = nl < ns ? nl : ns;
    int p = 0;
    int s = 0;

    if (fast != 0) {
        p = lo < least ? lo : least;
        s = nl - hi;

        if (s < 0 || ns - s < p) {
            p = 0;
            s = 0;
            fast = 0;
        }
    }

    while (p < least - s && sh_eq(&d->items[p], &u->sh[p]) != 0) {
        p++;
    }

    while (s < least - p &&
           sh_eq(&d->items[nl - 1 - s], &u->sh[ns - 1 - s]) != 0) {
        s++;
    }

    *pp = p;
    *ps = s;
}


static void
set_label(Patch *p, const char *label)
{
    str_copy(p->label, sizeof p->label, label != NULL ? label : "Edit");
}


/*
 * A document of many items in which the same number of items changed here and
 * there (Replace All) is kept as one patch for each stretch that changed, so
 * that the equal items between them take no memory.
 */
#define UNDO_GAP 32


/* Make one patch for the items [lo, lo + n) (n_other of the shadow). */
static Patch *
make_one(Undo *u, const Doc *d, int lo, int n_live, int n_other)
{
    Patch *pt = push_patch(u);

    pt->lo = lo;
    pt->n_live = n_live;
    pt->n_other = n_other;
    pt->items = calloc(usz(n_other > 0 ? n_other : 1), sizeof *pt->items);
    sh_replace(u, lo, n_other, d->items + lo, n_live, pt->items);

    return pt;
}


static void
make_patches(Undo *u, const Doc *d, const UndoKey *uk, const char *label,
             int p, int n_live, int n_old, int hdr_changed)
{
    Patch *first;
    int made = 0;

    if (hdr_changed == 0 && n_live == n_old && n_live > 2 * UNDO_GAP) {
        int i = p;
        int end = p + n_live;

        while (i < end) {
            int a;
            int b;
            int gap = 0;

            while (i < end && sh_eq(&d->items[i], &u->sh[i]) != 0) {
                i++;
            }

            if (i >= end) {
                break;
            }

            a = i;
            b = i;

            while (i < end && gap <= UNDO_GAP) {
                if (sh_eq(&d->items[i], &u->sh[i]) != 0) {
                    gap++;
                } else {
                    gap = 0;
                    b = i + 1;
                }

                i++;
            }

            {
                Patch *pt = make_one(u, d, a, b - a, b - a);

                pt->first = made == 0;
                pt->cur_before = uk->cur;
                pt->typing = uk->cls;
                set_label(pt, label);
                patch_size(u, pt);
                made++;
            }
        }
    }

    if (made == 0) {
        first = make_one(u, d, p, n_live, n_old);
        first->first = 1;
        first->typing = uk->cls;
        set_label(first, label);

        if (hdr_changed != 0) {
            first->has_hdr = 1;
            first->hdr = u->shhdr;
            memset(&u->shhdr, 0, sizeof u->shhdr);
            first->structured = u->shstruct;
            sh_set_header(u, d);
        }

        first->cur_before = uk->cur;
        patch_size(u, first);
    }
}


void
undo_before_key(Editor *e, UndoKey *uk, int cls, int ch)
{
    uk->cur = e->cur;
    uk->cls = cls;
    uk->ch = ch;

    if (levels_set == 0) {
        undo_free(e);
        return;
    }

    if (e->undo == NULL) {
        e->undo = undo_make(e);
    }

    trim_history(e->undo);

    /*
     * A change that was made between two keys (the markers that a Preview
     * puts in) is an operation of its own, and is looked for in the whole
     * document.
     */
    if (e->undo->gen != e->doc->gen) {
        UndoKey other;

        other.cur = e->cur;
        other.cls = 0;
        other.ch = 0;
        undo_after_key(e, &other, "Edit");
    }
}


void
undo_after_key(Editor *e, const UndoKey *uk, const char *label)
{
    Undo *u = e->undo;
    Doc *d = e->doc;
    int hdr_changed;
    int fast;
    int p;
    int s;
    int lo = 0;
    int hi = 0;

    if (u == NULL) {
        return;
    }

    if (uk->cls == 0) {
        u->last_typing = 0;
    }

    if (d->modified == 0) {
        u->saved_pos = u->pos;
    }

    if (d->gen == u->gen) {
        return;
    }

    u->gen = d->gen;
    hdr_changed = lines_eq(&d->header, &u->shhdr) == 0 ||
                  d->structured != u->shstruct;
    fast = uk->cls != 0 && hdr_changed == 0;

    if (fast != 0) {
        int a = uk->cur.item < e->cur.item ? uk->cur.item : e->cur.item;
        int b = uk->cur.item < e->cur.item ? e->cur.item : uk->cur.item;

        lo = a > 0 ? a - 1 : 0;
        hi = b + 2 < d->nitems ? b + 2 : d->nitems;
    }

    find_range(u, d, fast, lo, hi, &p, &s);

    {
        int n_live = d->nitems - p - s;
        int n_old = u->shn - p - s;
        Patch *top = u->np > 0 && u->pos == u->np ? &u->p[u->np - 1] : NULL;

        if (n_live == 0 && n_old == 0 && hdr_changed == 0) {
            return;
        }

        /* Typing of the same kind in the same place joins the operation. */
        if (uk->cls != 0 && top != NULL && top->typing == uk->cls &&
            u->last_typing != 0 && u->break_after == 0 && hdr_changed == 0 &&
            p >= top->lo && p + n_old <= top->lo + top->n_live) {
            sh_replace(u, p, n_old, d->items + p, n_live, NULL);
            top->n_live += n_live - n_old;
            top->cur_after = e->cur;
        } else {
            int n0;

            clear_redo(u);
            n0 = u->np;
            make_patches(u, d, uk, label, p, n_live, n_old, hdr_changed);

            for (int i = n0; i < u->np; i++) {
                u->p[i].cur_before = uk->cur;
                u->p[i].cur_after = e->cur;
            }

            trim_history(u);
        }
    }

    u->last_typing = uk->cls;
    u->break_after = uk->cls == 1 && uk->ch == ' ';

    if (d->modified == 0) {
        u->saved_pos = u->pos;
    }
}


/* ---- undo and redo ------------------------------------------------------ */
/*
 * Swap the range of the document with the items of the patch, and the
 * header too when the patch has one.  The shadow follows.
 */
static void
apply(Editor *e, Patch *pt)
{
    Undo *u = e->undo;
    Doc *d = e->doc;
    int old_live = pt->n_live;
    int new_live = pt->n_other;
    Item *out = calloc(usz(old_live > 0 ? old_live : 1), sizeof *out);
    int n = d->nitems - old_live + new_live;
    int tail = d->nitems - pt->lo - old_live;

    memcpy(out, d->items + pt->lo, usz(old_live) * sizeof *out);

    /* The blocks that go out and the blocks that come in change the counts
       of the markers; only the items of the patch are looked at. */
    for (int i = 0; i < old_live; i++) {
        if (out[i].type == IT_BLOCK && out[i].blk != NULL) {
            doc_count_block(d, out[i].blk, -1);
        }
    }

    for (int i = 0; i < new_live; i++) {
        if (pt->items[i].type == IT_BLOCK && pt->items[i].blk != NULL) {
            doc_count_block(d, pt->items[i].blk, 1);
        }
    }

    if (n > d->capitems) {
        d->capitems = n + n / 4 + 16;
        d->items = realloc(d->items, usz(d->capitems) * sizeof *d->items);
    }

    memmove(d->items + pt->lo + new_live, d->items + pt->lo + old_live,
            usz(tail) * sizeof *d->items);
    memcpy(d->items + pt->lo, pt->items, usz(new_live) * sizeof *d->items);
    d->nitems = n;
    free(pt->items);
    pt->items = out;
    pt->n_other = old_live;
    pt->n_live = new_live;
    sh_replace(u, pt->lo, old_live, d->items + pt->lo, new_live, NULL);

    if (pt->has_hdr != 0) {
        Lines hdr = d->header;
        int st = d->structured;

        d->header = pt->hdr;
        d->structured = pt->structured;
        pt->hdr = hdr;
        pt->structured = st;
        sh_set_header(u, d);
    }

    patch_size(u, pt);
}


static void
finish(Editor *e, const Pos *cur, const char *what, const char *label)
{
    Undo *u = e->undo;
    Doc *d = e->doc;
    int item = cur->item < d->nitems ? cur->item : d->nitems - 1;

    e->sel.kind = SEL_NONE;
    e->only_s = -1;
    ed_invalidate(e);
    doc_touch(d);
    d->modified = u->pos != u->saved_pos;
    u->gen = d->gen;
    u->last_typing = 0;
    ed_goto(e, item < 0 ? 0 : item, cur->col, cur->tok);
    ed_msg(e, "%s: %s", what, label);
}


int
undo_undo(Editor *e)
{
    Undo *u = e->undo;
    char label[32];
    Pos cur;
    int k;

    if (levels_set == 0) {
        ed_msg(e, "Undo is off: Undo Levels is 0");
        return -1;
    }

    if (u == NULL || u->pos == 0) {
        ed_msg(e, "Nothing to undo");
        return -1;
    }

    /* The patches of one operation, from the last to the first. */
    k = u->pos - 1;

    while (k > 0 && u->p[k].first == 0) {
        k--;
    }

    str_copy(label, sizeof label, u->p[k].label);
    cur = u->p[k].cur_before;

    while (u->pos > k) {
        u->pos--;
        apply(e, &u->p[u->pos]);
    }

    finish(e, &cur, "Undone", label);

    return 0;
}


int
undo_redo(Editor *e)
{
    Undo *u = e->undo;
    char label[32];
    Pos cur;

    if (levels_set == 0) {
        ed_msg(e, "Undo is off: Undo Levels is 0");
        return -1;
    }

    if (u == NULL || u->pos == u->np) {
        ed_msg(e, "Nothing to redo");
        return -1;
    }

    str_copy(label, sizeof label, u->p[u->pos].label);

    do {
        apply(e, &u->p[u->pos]);
        cur = u->p[u->pos].cur_after;
        u->pos++;
    } while (u->pos < u->np && u->p[u->pos].first == 0);

    finish(e, &cur, "Redone", label);

    return 0;
}


/* ---- for the tests ------------------------------------------------------ */
void
undo_counts(const Editor *e, int *undo, int *redo)
{
    const Undo *u = e->undo;

    *undo = u != NULL ? count_ops(u, 0, u->pos) : 0;
    *redo = u != NULL ? count_ops(u, u->pos, u->np) : 0;
}


size_t
undo_history_bytes(const Editor *e)
{
    return e->undo != NULL ? e->undo->bytes : 0;
}


size_t
undo_shadow_bytes(const Editor *e)
{
    const Undo *u = e->undo;

    size_t n = 0;

    if (u == NULL) {
        return 0;
    }

    for (int i = 0; i < u->shn; i++) {
        const ShItem *s = &u->sh[i];

        n += sizeof *s;

        if (s->it.blk == NULL) {
            continue;
        }

        n += sizeof *s->it.blk + lines_bytes(&s->it.blk->props);

        for (int c = 0; c < s->it.blk->ncols; c++) {
            const Column *col = &s->it.blk->cols[c];

            n += sizeof(Column) + sizeof(Packed) + lines_bytes(&col->props) +
                 (s->pk[c].s != NULL ? s->pk[c].n :
                                       usz(col->text.cap) * sizeof(Tok));
        }
    }

    return n + lines_bytes(&u->shhdr);
}


int
undo_verify(const Editor *e)
{
    const Undo *u = e->undo;
    const Doc *d = e->doc;

    if (u == NULL) {
        return 0;
    }

    if (u->shn != d->nitems || lines_eq(&u->shhdr, &d->header) == 0 ||
        u->shstruct != d->structured) {
        return 1;
    }

    for (int i = 0; i < d->nitems; i++) {
        if (sh_eq(&d->items[i], &u->sh[i]) == 0) {
            return 1;
        }
    }

    return 0;
}
