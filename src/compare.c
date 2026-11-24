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

/* compare.c - the comparison of two documents, see compare.h. */
#include "util.h"
#include "log.h"
#include "msdoc.h"
#include "compare.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEG (-1)
#define NUMCAP 64
#define SHOW_TEXT 300
#define SHOW_TITLE 60


/* ------------------------------------------------------------------ util */
static void *
cmp_alloc(void *p, size_t n)
{
    void *q = realloc(p, n != 0 ? n : 1);

    if (q == NULL) {
        log_fatal(1, "compare: out of memory allocating %zu bytes", n);
    }

    return q;
}


static uint32_t
hash_bytes(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p;

    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }

    return h;
}


typedef struct {
    char *s;
    size_t n;
    size_t cap;
} Buf;


static void
buf_put(Buf *b, char c)
{
    if (b->n + 2 > b->cap) {
        b->cap = b->cap != 0 ? b->cap * 2 : 128;
        b->s = cmp_alloc(b->s, b->cap);
    }

    b->s[b->n++] = c;
    b->s[b->n] = '\0';
}


static void
buf_puts(Buf *b, const char *s)
{
    for (; *s != '\0'; s++) {
        buf_put(b, *s);
    }
}


/* ------------------------------------------------------------- the diff */

/* Whether item i of the first sequence is the same as item j of the second. */
typedef int (*EqFn)(const void *ctx, int i, int j);


/*
 * Chooses how the path reaches diagonal k in round d.  xl is the furthest
 * point of diagonal k - 1 and xr that of diagonal k + 1 in the round before
 * (NEG when there is none).  Returns 0 when the diagonal cannot be reached,
 * 1 for an insertion (from k + 1) and 2 for a deletion (from k - 1), and the
 * x of the point before the diagonal that follows in *x.
 */
static int
choose(int xl, int xr, int k, int na, int nb, int *x)
{
    int del_ok = xl != NEG && xl + 1 <= na;
    int ins_ok = xr != NEG && xr - k <= nb;

    if (del_ok != 0 && ins_ok != 0) {
        if (xr >= xl + 1) {
            *x = xr;

            return 1;
        }

        *x = xl + 1;

        return 2;
    }

    if (ins_ok != 0) {
        *x = xr;

        return 1;
    }

    if (del_ok != 0) {
        *x = xl + 1;

        return 2;
    }

    return 0;
}


/*
 * The shortest edit script of the two sequences, by the method of Myers: the
 * steps are written to ops as 'e' (the same), 'd' (only in the first) and 'i'
 * (only in the second).  Returns the number of steps, or -1 when more than
 * dmax differences are needed.  The first item of the first sequence is item
 * offa for eq, and the first of the second is offb.
 */
static int
myers(const void *ctx, EqFn eq, int offa, int na, int offb, int nb, int dmax,
      char *ops)
{
    int off = dmax + 1;
    int *v = cmp_alloc(NULL, usz(2 * dmax + 3) * sizeof *v);
    int **tr = cmp_alloc(NULL, usz(dmax + 1) * sizeof *tr);
    int found = -1;
    int nops = 0;

    for (int d = 0; d <= dmax; d++) {
        tr[d] = NULL;
    }

    for (int i = 0; i < 2 * dmax + 3; i++) {
        v[i] = NEG;
    }

    for (int d = 0; d <= dmax && found < 0; d++) {
        tr[d] = cmp_alloc(NULL, usz(d + 1) * sizeof(int));

        for (int k = -d; k <= d; k += 2) {
            int xl = k > -d ? v[off + k - 1] : NEG;
            int xr = k < d ? v[off + k + 1] : NEG;
            int x = 0;
            int mode;

            if (d == 0) {
                mode = 1;
                x = 0;
            } else {
                mode = choose(xl, xr, k, na, nb, &x);
            }

            if (mode == 0) {
                v[off + k] = NEG;
                tr[d][(k + d) / 2] = NEG;
                continue;
            }

            int y = x - k;

            while (x < na && y < nb && eq(ctx, offa + x, offb + y) != 0) {
                x++;
                y++;
            }

            v[off + k] = x;
            tr[d][(k + d) / 2] = x;

            if (x == na && y == nb) {
                found = d;
                break;
            }
        }
    }

    if (found >= 0) {
        int x = na;
        int y = nb;

        for (int d = found; d >= 1; d--) {
            int k = x - y;
            int xl = NEG;
            int xr = NEG;
            int ex = 0;
            int px;
            int py;
            int mode;

            if (k > -d && k - 1 >= -(d - 1) && k - 1 <= d - 1) {
                xl = tr[d - 1][(k - 1 + d - 1) / 2];
            }

            if (k < d && k + 1 >= -(d - 1) && k + 1 <= d - 1) {
                xr = tr[d - 1][(k + 1 + d - 1) / 2];
            }

            mode = choose(xl, xr, k, na, nb, &ex);

            if (mode == 0) {
                break;
            }

            /* The point before the edit: an insertion came from diagonal
               k + 1 and a deletion from diagonal k - 1. */
            if (mode == 1) {
                px = ex;
                py = ex - (k + 1);
            } else {
                px = ex - 1;
                py = ex - k;
            }

            while (x > ex) {
                ops[nops++] = 'e';
                x--;
                y--;
            }

            ops[nops++] = mode == 1 ? 'i' : 'd';
            x = px;
            y = py;
        }

        while (x > 0 && y > 0) {
            ops[nops++] = 'e';
            x--;
            y--;
        }

        for (int a = 0, b = nops - 1; a < b; a++, b--) {
            char t = ops[a];

            ops[a] = ops[b];
            ops[b] = t;
        }
    } else {
        nops = -1;
    }

    for (int d = 0; d <= dmax; d++) {
        free(tr[d]);
    }

    free(tr);
    free(v);

    return nops;
}


/*
 * The edit script of two sequences with the same ends cut off first.  Returns
 * the number of steps in ops (at most na + nb).  *trunc is raised when the
 * middle needed more than dmax differences: it is then all deleted and all
 * inserted.
 */
static int
diff_all(const void *ctx, EqFn eq, int na, int nb, int dmax, char *ops,
         int *trunc)
{
    int p = 0;
    int s = 0;
    int n = 0;

    while (p < na && p < nb && eq(ctx, p, p) != 0) {
        p++;
    }

    while (s < na - p && s < nb - p &&
           eq(ctx, na - 1 - s, nb - 1 - s) != 0) {
        s++;
    }

    for (int i = 0; i < p; i++) {
        ops[n++] = 'e';
    }

    int ma = na - p - s;
    int mb = nb - p - s;

    if (ma == 0) {
        for (int i = 0; i < mb; i++) {
            ops[n++] = 'i';
        }
    } else if (mb == 0) {
        for (int i = 0; i < ma; i++) {
            ops[n++] = 'd';
        }
    } else {
        int lim = dmax < ma + mb ? dmax : ma + mb;
        int got = myers(ctx, eq, p, ma, p, mb, lim, ops + n);

        if (got < 0) {
            *trunc = 1;

            for (int i = 0; i < ma; i++) {
                ops[n++] = 'd';
            }

            for (int i = 0; i < mb; i++) {
                ops[n++] = 'i';
            }
        } else {
            n += got;
        }
    }

    for (int i = 0; i < s; i++) {
        ops[n++] = 'e';
    }

    return n;
}


/* --------------------------------------------------------------- the units */
typedef struct {
    char *text; /* the words of the block, with the markers left out */
    uint32_t hash; /* of the text */
    uint32_t fmt; /* of the format records of the block and its columns */
    uint32_t mrk; /* of the markers and attributes in the text, in order */
    int level; /* more than 0 for a headline: its depth */
    int sect; /* the section (index) that the block is in, or -1 */
} Unit;


typedef struct {
    char num[NUMCAP];
    int unit; /* the headline */
} Sect;


typedef struct {
    Unit *u;
    int n;
    int cap;
    Sect *s;
    int ns;
    int caps;
    int structured;
} Units;


static void
block_text(const Block *b, Buf *out, uint32_t *mrk)
{
    uint32_t h = 2166136261u;

    for (int ci = 0; ci < b->ncols; ci++) {
        const Text *t = &b->cols[ci].text;

        if (ci > 0) {
            buf_puts(out, " | ");
        }

        for (int i = 0; i < t->n; i++) {
            const Tok *k = &t->v[i];

            if (k->kind == TK_CHAR) {
                int c = k->a;

                buf_put(out, c < 0x20 || c == 0x7F ? ' ' : chr(c));
            } else if (k->kind == TK_TAB || k->kind == TK_NL ||
                       k->kind == TK_CTRL) {
                buf_put(out, ' ');
            } else if (k->kind == TK_ATTR) {
                uint8_t rec[3] = {k->kind, k->a, k->b};

                h = hash_bytes(h, rec, sizeof rec);
            } else if (k->kind == TK_MARKER) {
                int mt = tok_mtype(k);

                h = hash_bytes(h, &mt, sizeof mt);
                h = hash_bytes(h, tok_mdata(k), tok_mlen(k));
            }
        }
    }

    while (out->n > 0 && out->s[out->n - 1] == ' ') {
        out->s[--out->n] = '\0';
    }

    if (out->n > 0 && out->s[0] == ' ') {
        size_t lead = 0;

        while (out->s[lead] == ' ') {
            lead++;
        }

        memmove(out->s, out->s + lead, out->n - lead + 1);
        out->n -= lead;
    }

    *mrk = h;
}


static uint32_t
block_format(const Block *b)
{
    uint32_t h = 2166136261u;
    const char sep = '\0';

    for (int i = 0; i < b->props.n; i++) {
        h = hash_bytes(h, b->props.v[i].s, usz(b->props.v[i].n));
        h = hash_bytes(h, &sep, 1);
    }

    for (int ci = 0; ci < b->ncols; ci++) {
        const Column *c = &b->cols[ci];

        for (int i = 0; i < c->props.n; i++) {
            h = hash_bytes(h, c->props.v[i].s, usz(c->props.v[i].n));
            h = hash_bytes(h, &sep, 1);
        }

        h = hash_bytes(h, &c->width, sizeof c->width);
    }

    return h;
}


static void
units_add(Units *U, const Block *b, int level, int sect)
{
    Buf text = {NULL, 0, 0};
    Unit *u;
    uint32_t mrk;

    if (U->n == U->cap) {
        U->cap = U->cap != 0 ? U->cap * 2 : 256;
        U->u = cmp_alloc(U->u, usz(U->cap) * sizeof *U->u);
    }

    block_text(b, &text, &mrk);

    if (text.s == NULL) {
        buf_put(&text, ' ');
        text.s[--text.n] = '\0';
    }

    u = &U->u[U->n++];
    u->text = text.s;
    u->hash = hash_bytes(2166136261u, text.s, text.n);
    u->fmt = block_format(b);
    u->mrk = mrk;
    u->level = level;
    u->sect = sect;
}


static void
sect_number(char *num, size_t cap, const int *cnt, int level)
{
    size_t o = 0;

    num[0] = '\0';

    for (int l = 1; l <= level && l < SECT_MAXDEPTH; l++) {
        int w = snprintf(num + o, cap - o, l > 1 ? ".%d" : "%d", cnt[l]);

        if (w < 0 || usz(w) >= cap - o) {
            break;
        }

        o += usz(w);
    }
}


/*
 * The blocks of a document in order, with the number of the section that each
 * one is in.  In a structured document the first block after a section on
 * the first level or deeper is its headline; the text before the first
 * section is not a headline.
 */
static void
units_load(Units *U, const Doc *d)
{
    int cnt[SECT_MAXDEPTH + 1];
    int depth = 0;
    int level = 0;
    int cur = -1;
    int want_head = 0;

    memset(U, 0, sizeof *U);
    memset(cnt, 0, sizeof cnt);
    U->structured = d->structured;

    for (int i = 0; i < d->nitems; i++) {
        const Item *it = &d->items[i];

        if (it->type == IT_PLUS) {
            depth++;
        } else if (it->type == IT_MINUS) {
            depth = depth > 0 ? depth - 1 : 0;
        } else if (it->type == IT_SECT) {
            if (d->structured != 0 && depth >= 1) {
                level = depth < SECT_MAXDEPTH ? depth : SECT_MAXDEPTH;
                cnt[level]++;

                for (int l = level + 1; l <= SECT_MAXDEPTH; l++) {
                    cnt[l] = 0;
                }

                if (U->ns == U->caps) {
                    U->caps = U->caps != 0 ? U->caps * 2 : 64;
                    U->s = cmp_alloc(U->s, usz(U->caps) * sizeof *U->s);
                }

                sect_number(U->s[U->ns].num, NUMCAP, cnt, level);
                U->s[U->ns].unit = -1;
                cur = U->ns++;
                want_head = 1;
            } else {
                cur = -1;
                want_head = 0;
            }
        } else if (it->type == IT_BLOCK && it->blk != NULL) {
            if (want_head != 0) {
                U->s[cur].unit = U->n;
                units_add(U, it->blk, level, cur);
                want_head = 0;
            } else {
                units_add(U, it->blk, 0, cur);
            }
        }
    }
}


static void
units_free(Units *U)
{
    for (int i = 0; i < U->n; i++) {
        free(U->u[i].text);
    }

    free(U->u);
    free(U->s);
    memset(U, 0, sizeof *U);
}


typedef struct {
    const Units *a;
    const Units *b;
} UnitPair;


static int
unit_eq(const void *ctx, int i, int j)
{
    const UnitPair *p = ctx;
    const Unit *x = &p->a->u[i];
    const Unit *y = &p->b->u[j];

    return x->hash == y->hash && strcmp(x->text, y->text) == 0;
}


/* ---------------------------------------------------------------- words */
typedef struct {
    const char *text;
    int *start;
    int *len;
    int n;
} Words;


static void
words_split(Words *w, const char *text)
{
    int cap = 16;
    size_t i = 0;

    w->text = text;
    w->n = 0;
    w->start = cmp_alloc(NULL, usz(cap) * sizeof(int));
    w->len = cmp_alloc(NULL, usz(cap) * sizeof(int));

    while (text[i] != '\0') {
        while (text[i] == ' ') {
            i++;
        }

        if (text[i] == '\0') {
            break;
        }

        size_t s = i;

        while (text[i] != '\0' && text[i] != ' ') {
            i++;
        }

        if (w->n == cap) {
            cap *= 2;
            w->start = cmp_alloc(w->start, usz(cap) * sizeof(int));
            w->len = cmp_alloc(w->len, usz(cap) * sizeof(int));
        }

        w->start[w->n] = isz(s);
        w->len[w->n] = isz(i - s);
        w->n++;
    }
}


static void
words_free(Words *w)
{
    free(w->start);
    free(w->len);
}


typedef struct {
    const Words *a;
    const Words *b;
} WordPair;


static int
word_eq(const void *ctx, int i, int j)
{
    const WordPair *p = ctx;

    return p->a->len[i] == p->b->len[j] &&
           memcmp(p->a->text + p->a->start[i], p->b->text + p->b->start[j],
                  usz(p->a->len[i])) == 0;
}


/*
 * The words of two texts compared.  Returns the percentage of the words that
 * are the same (0 when the texts are too different for the comparison), and
 * the text with the differences marked in *merged when it is not NULL.
 */
static int
word_diff(const char *ta, const char *tb, char **merged)
{
    Words wa;
    Words wb;
    WordPair p;
    int trunc = 0;
    int common = 0;
    char *ops;
    int n;
    Buf out = {NULL, 0, 0};

    words_split(&wa, ta);
    words_split(&wb, tb);
    p.a = &wa;
    p.b = &wb;
    ops = cmp_alloc(NULL, usz(wa.n + wb.n + 1));
    n = diff_all(&p, word_eq, wa.n, wb.n, CMP_WORD_DMAX, ops, &trunc);

    for (int i = 0; i < n; i++) {
        common += ops[i] == 'e';
    }

    if (merged != NULL) {
        int ia = 0;
        int ib = 0;
        int i = 0;

        while (i < n) {
            char op = ops[i];
            int first = 1;

            if (op == 'e') {
                if (out.n > 0) {
                    buf_put(&out, ' ');
                }

                for (int c = 0; c < wa.len[ia]; c++) {
                    buf_put(&out, wa.text[wa.start[ia] + c]);
                }

                ia++;
                ib++;
                i++;
            } else {
                char mark = op;

                if (out.n > 0) {
                    buf_put(&out, ' ');
                }

                buf_put(&out, mark == 'd' ? CMP_DEL_ON : CMP_INS_ON);

                while (i < n && ops[i] == mark) {
                    if (first == 0) {
                        buf_put(&out, ' ');
                    }

                    first = 0;

                    if (mark == 'd') {
                        for (int c = 0; c < wa.len[ia]; c++) {
                            buf_put(&out, wa.text[wa.start[ia] + c]);
                        }

                        ia++;
                    } else {
                        for (int c = 0; c < wb.len[ib]; c++) {
                            buf_put(&out, wb.text[wb.start[ib] + c]);
                        }

                        ib++;
                    }

                    i++;
                }

                buf_put(&out, mark == 'd' ? CMP_DEL_OFF : CMP_INS_OFF);
            }
        }

        *merged = out.s != NULL ? out.s : cmp_alloc(NULL, 1);

        if (out.s == NULL) {
            (*merged)[0] = '\0';
        }
    }

    free(ops);
    words_free(&wa);
    words_free(&wb);

    if (trunc != 0) {
        return 0;
    }

    if (wa.n + wb.n == 0) {
        return 100;
    }

    return (200 * common) / (wa.n + wb.n);
}


/* ------------------------------------------------------------- the entries */
enum {
    CE_INS, /* a block only in the second document */
    CE_DEL, /* a block only in the first document */
    CE_CHG, /* a block with other words (a, b) */
    CE_MOVED, /* a block that moved (a, b) */
    CE_FMT, /* the same words, other format or markers (a, b, flags) */
    CE_LEVEL, /* the same headline on another level (a, b) */
    CE_NONE /* taken out: it was joined with another entry */
};

#define FLAG_FORMAT 1
#define FLAG_MARKS 2


typedef struct {
    int kind;
    int a;
    int b;
    int flags;
} Entry;


typedef struct {
    Entry *v;
    int n;
    int cap;
} Entries;


static void
entry_add(Entries *E, int kind, int a, int b, int flags)
{
    if (E->n == E->cap) {
        E->cap = E->cap != 0 ? E->cap * 2 : 64;
        E->v = cmp_alloc(E->v, usz(E->cap) * sizeof *E->v);
    }

    E->v[E->n].kind = kind;
    E->v[E->n].a = a;
    E->v[E->n].b = b;
    E->v[E->n].flags = flags;
    E->n++;
}


/*
 * The blocks that are only in one document, in a run between two blocks that
 * are the same: the i-th removed block and the i-th inserted block are the
 * same block changed when enough of their words are the same.
 */
static void
flush_run(Entries *E, const Units *A, const Units *B, const int *dels, int nd,
          const int *inss, int ni)
{
    int m = nd < ni ? nd : ni;

    for (int j = 0; j < m; j++) {
        if (word_diff(A->u[dels[j]].text, B->u[inss[j]].text, NULL) >=
            CMP_SIMILAR) {
            entry_add(E, CE_CHG, dels[j], inss[j], 0);
        } else {
            entry_add(E, CE_DEL, dels[j], -1, 0);
            entry_add(E, CE_INS, -1, inss[j], 0);
        }
    }

    for (int j = m; j < nd; j++) {
        entry_add(E, CE_DEL, dels[j], -1, 0);
    }

    for (int j = m; j < ni; j++) {
        entry_add(E, CE_INS, -1, inss[j], 0);
    }
}


static const Units *sort_units;
static const Entries *sort_entries;


/* The inserted entries in the order of the words of their blocks. */
static int
by_text(const void *x, const void *y)
{
    const int *px = x;
    const int *py = y;
    const Unit *p = &sort_units->u[sort_entries->v[*px].b];
    const Unit *q = &sort_units->u[sort_entries->v[*py].b];

    if (p->hash != q->hash) {
        return p->hash < q->hash ? -1 : 1;
    }

    return strcmp(p->text, q->text);
}


/*
 * A block that was removed in one place and inserted in another one with the
 * same words has moved.  The removed entry becomes the move and the inserted
 * entry is taken out.
 */
static void
find_moves(Entries *E, const Units *A, const Units *B)
{
    int *ins = cmp_alloc(NULL, usz(E->n + 1) * sizeof *ins);
    int nins = 0;

    for (int i = 0; i < E->n; i++) {
        if (E->v[i].kind == CE_INS && B->u[E->v[i].b].text[0] != '\0') {
            ins[nins++] = i;
        }
    }

    sort_units = B;
    sort_entries = E;
    qsort(ins, usz(nins), sizeof *ins, by_text);

    for (int i = 0; i < E->n; i++) {
        const Unit *x;
        int lo = 0;
        int hi = nins;

        if (E->v[i].kind != CE_DEL || A->u[E->v[i].a].text[0] == '\0') {
            continue;
        }

        x = &A->u[E->v[i].a];

        while (lo < hi) {
            int mid = (lo + hi) / 2;
            const Unit *y = &B->u[E->v[ins[mid]].b];
            int c;

            if (y->hash != x->hash) {
                c = y->hash < x->hash ? -1 : 1;
            } else {
                c = strcmp(y->text, x->text);
            }

            if (c < 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        for (int j = lo; j < nins; j++) {
            const Unit *y = &B->u[E->v[ins[j]].b];

            if (y->hash != x->hash || strcmp(y->text, x->text) != 0) {
                break;
            }

            if (E->v[ins[j]].kind == CE_INS) {
                E->v[i].kind = CE_MOVED;
                E->v[i].b = E->v[ins[j]].b;
                E->v[ins[j]].kind = CE_NONE;
                break;
            }
        }
    }

    free(ins);
}


/* ------------------------------------------------------------- the report */
static void
para_add(CmpReport *r, int kind, const char *s)
{
    size_t n = strlen(s);

    if (r->npara == r->cappara) {
        r->cappara = r->cappara != 0 ? r->cappara * 2 : 64;
        r->para = cmp_alloc(r->para, usz(r->cappara) * sizeof *r->para);
    }

    r->para[r->npara].s = cmp_alloc(NULL, n + 1);
    memcpy(r->para[r->npara].s, s, n + 1);
    r->para[r->npara].kind = kind;
    r->npara++;
}


static void
para_addf(CmpReport *r, int kind, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));


static void
para_addf(CmpReport *r, int kind, const char *fmt, ...)
{
    char buf[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    para_add(r, kind, buf);
}


/* The text cut to at most max characters, with three dots when it is cut. */
static void
clip(char *out, size_t cap, const char *text, size_t max)
{
    size_t n = strlen(text);

    if (max + 4 > cap) {
        max = cap - 4;
    }

    if (n <= max) {
        str_copy(out, cap, text);

        return;
    }

    memcpy(out, text, max);
    memcpy(out + max, "...", 4);
}


static const char *
plural(int n)
{
    return n == 1 ? "" : "s";
}


/* The section that a block is in, as the end of a sentence. */
static void
where(const Units *U, int u, char *buf, size_t cap)
{
    const Unit *x = &U->u[u];
    char t[SHOW_TITLE + 8];

    if (x->level > 0) {
        clip(t, sizeof t, x->text, SHOW_TITLE);
        snprintf(buf, cap, "the headline of section %s \"%s\"",
                 U->s[x->sect].num, t);
    } else if (x->sect >= 0) {
        int h = U->s[x->sect].unit;

        clip(t, sizeof t, h >= 0 ? U->u[h].text : "", SHOW_TITLE);
        snprintf(buf, cap, "in section %s \"%s\"", U->s[x->sect].num, t);
    } else {
        snprintf(buf, cap, "%s",
                 U->structured != 0 ? "before the first section" :
                                      "in the text");
    }
}


static void
section_line(CmpReport *r, const char *label, const Units *U, int u,
             const char *which)
{
    char t[SHOW_TITLE + 8];
    const Unit *x = &U->u[u];

    clip(t, sizeof t, x->text, SHOW_TITLE);
    para_addf(r, CMP_ENTRY, "%s: %s \"%s\" (%s)", label, U->s[x->sect].num, t,
              which);
}


/* The words of a block as one detail paragraph, with an optional mark. */
static void
detail(CmpReport *r, char on, const char *text, char off, size_t max)
{
    char t[SHOW_TEXT + 8];
    char buf[SHOW_TEXT + 16];

    clip(t, sizeof t, text, max);

    if (on != '\0') {
        snprintf(buf, sizeof buf, "%c%s%c", on, t, off);
    } else {
        snprintf(buf, sizeof buf, "%s", t);
    }

    para_add(r, CMP_DETAIL, buf);
}


static void
summary(CmpReport *r)
{
    char buf[512];
    size_t o = 0;
    struct {
        int n;
        const char *noun;
        const char *what;
    } part[] = {{r->inserted, "block", "inserted"},
                {r->deleted, "block", "removed"},
                {r->changed, "block", "changed"},
                {r->moved, "block", "moved"},
                {r->format_changed, "block", "with another format or markers"},
                {r->sect_added, "section", "added"},
                {r->sect_removed, "section", "removed"},
                {r->sect_moved, "section", "moved"},
                {r->sect_level, "section", "on another level"}};

    buf[0] = '\0';

    for (size_t i = 0; i < sizeof part / sizeof part[0]; i++) {
        int w;

        if (part[i].n == 0) {
            continue;
        }

        w = snprintf(buf + o, sizeof buf - o, "%s%d %s%s %s", o > 0 ? ", " : "",
                     part[i].n, part[i].noun, plural(part[i].n),
                     part[i].what);

        if (w < 0 || usz(w) >= sizeof buf - o) {
            break;
        }

        o += usz(w);
    }

    para_add(r, CMP_HEAD, "Summary");
    para_addf(r, CMP_PLAIN, "Differences: %s.", buf);

    if (r->too_large != 0) {
        para_add(r, CMP_PLAIN,
                 "Part of the documents is too different for a detailed "
                 "comparison: that part is listed as blocks removed and "
                 "blocks inserted.");
    }
}


static int
is_structure(const Entry *e, const Units *A, const Units *B)
{
    return e->kind == CE_LEVEL ||
           (e->kind == CE_INS && B->u[e->b].level > 0) ||
           (e->kind == CE_DEL && A->u[e->a].level > 0) ||
           (e->kind == CE_MOVED && A->u[e->a].level > 0 &&
            B->u[e->b].level > 0);
}


static void
describe_structure(CmpReport *r, const Entries *E, const Units *A,
                   const Units *B)
{
    para_add(r, CMP_HEAD, "Sections");

    for (int i = 0; i < E->n; i++) {
        const Entry *e = &E->v[i];
        char t[SHOW_TITLE + 8];

        if (is_structure(e, A, B) == 0) {
            continue;
        }

        if (e->kind == CE_INS) {
            section_line(r, "Section added", B, e->b, "second document");
        } else if (e->kind == CE_DEL) {
            section_line(r, "Section removed", A, e->a, "first document");
        } else if (e->kind == CE_MOVED) {
            clip(t, sizeof t, A->u[e->a].text, SHOW_TITLE);
            para_addf(r, CMP_ENTRY,
                      "Section moved: \"%s\" from %s (first document) to %s "
                      "(second document)",
                      t, A->s[A->u[e->a].sect].num,
                      B->s[B->u[e->b].sect].num);
        } else {
            clip(t, sizeof t, A->u[e->a].text, SHOW_TITLE);
            para_addf(r, CMP_ENTRY,
                      "Section on another level: \"%s\" is %s in the first "
                      "document and %s in the second",
                      t, A->s[A->u[e->a].sect].num,
                      B->s[B->u[e->b].sect].num);
        }
    }
}


static void
describe_change(CmpReport *r, const Entry *e, const Units *A, const Units *B)
{
    char w1[256];
    char w2[256];

    if (e->kind == CE_INS) {
        where(B, e->b, w2, sizeof w2);
        para_addf(r, CMP_ENTRY, "Inserted: block %d of the second document, %s",
                  e->b + 1, w2);
        detail(r, CMP_INS_ON, B->u[e->b].text, CMP_INS_OFF, SHOW_TEXT);
    } else if (e->kind == CE_DEL) {
        where(A, e->a, w1, sizeof w1);
        para_addf(r, CMP_ENTRY, "Removed: block %d of the first document, %s",
                  e->a + 1, w1);
        detail(r, CMP_DEL_ON, A->u[e->a].text, CMP_DEL_OFF, SHOW_TEXT);
    } else if (e->kind == CE_MOVED) {
        where(A, e->a, w1, sizeof w1);
        where(B, e->b, w2, sizeof w2);
        para_addf(r, CMP_ENTRY,
                  "Moved: block %d of the first document (%s) is block %d of "
                  "the second (%s)",
                  e->a + 1, w1, e->b + 1, w2);
        detail(r, '\0', A->u[e->a].text, '\0', SHOW_TEXT);
    } else {
        const char *what = "Changed";
        char *merged = NULL;

        if (e->kind == CE_FMT) {
            what = "Format and markers changed";

            if (e->flags == FLAG_FORMAT) {
                what = "Format changed";
            } else if (e->flags == FLAG_MARKS) {
                what = "Markers or attributes changed";
            }
        }

        where(A, e->a, w1, sizeof w1);
        where(B, e->b, w2, sizeof w2);

        if (strcmp(w1, w2) == 0) {
            para_addf(r, CMP_ENTRY,
                      "%s: block %d of the first document, block %d of the "
                      "second, %s%s",
                      what, e->a + 1, e->b + 1, w1,
                      e->kind == CE_FMT ? " (the words are the same)" : "");
        } else {
            para_addf(r, CMP_ENTRY,
                      "%s: block %d of the first document (%s), block %d of "
                      "the second (%s)%s",
                      what, e->a + 1, w1, e->b + 1, w2,
                      e->kind == CE_FMT ? " (the words are the same)" : "");
        }

        if (e->kind == CE_CHG) {
            word_diff(A->u[e->a].text, B->u[e->b].text, &merged);
            para_add(r, CMP_DETAIL, merged);
            free(merged);
        } else {
            detail(r, '\0', A->u[e->a].text, '\0', 120);
        }
    }
}


static void
describe(CmpReport *r, const Entries *E, const Units *A, const Units *B)
{
    int structure = 0;
    int changes = 0;

    for (int i = 0; i < E->n; i++) {
        if (E->v[i].kind == CE_NONE) {
            continue;
        }

        if (is_structure(&E->v[i], A, B) != 0) {
            structure++;
        } else {
            changes++;
        }
    }

    if (structure > 0) {
        describe_structure(r, E, A, B);
    }

    if (changes == 0) {
        return;
    }

    para_add(r, CMP_HEAD, "Changes");

    for (int i = 0; i < E->n; i++) {
        if (E->v[i].kind != CE_NONE && is_structure(&E->v[i], A, B) == 0) {
            describe_change(r, &E->v[i], A, B);
        }
    }
}


/* ------------------------------------------------------------- the lines */
static void
line_add(CmpReport *r, int *cap, const char *s, size_t n)
{
    if (r->nlines == *cap) {
        *cap = *cap != 0 ? *cap * 2 : 128;
        r->lines = cmp_alloc(r->lines, usz(*cap) * sizeof *r->lines);
    }

    r->lines[r->nlines] = cmp_alloc(NULL, n + 1);
    memcpy(r->lines[r->nlines], s, n);
    r->lines[r->nlines][n] = '\0';
    r->nlines++;
}


/* A paragraph with the marks written as brackets with plus and minus signs. */
static char *
plain_text(const char *s)
{
    Buf b = {NULL, 0, 0};

    for (; *s != '\0'; s++) {
        if (*s == CMP_INS_ON) {
            buf_puts(&b, "[+");
        } else if (*s == CMP_INS_OFF) {
            buf_puts(&b, "+]");
        } else if (*s == CMP_DEL_ON) {
            buf_puts(&b, "[-");
        } else if (*s == CMP_DEL_OFF) {
            buf_puts(&b, "-]");
        } else {
            buf_put(&b, *s);
        }
    }

    if (b.s == NULL) {
        b.s = cmp_alloc(NULL, 1);
        b.s[0] = '\0';
    }

    return b.s;
}


/*
 * A paragraph broken into lines of CMP_WRAP characters at the spaces.  The
 * lines after the first one begin with indent spaces.
 */
static void
wrap_para(CmpReport *r, int *cap, const char *s, int indent)
{
    size_t n = strlen(s);
    size_t i = 0;
    int first = 1;

    if (n == 0) {
        line_add(r, cap, "", 0);

        return;
    }

    while (i < n) {
        int pad = first != 0 ? 0 : indent;
        size_t end = i + usz(CMP_WRAP - pad);
        char line[CMP_WRAP + 8];

        if (end >= n) {
            end = n;
        } else {
            size_t cut = end;

            while (cut > i && s[cut] != ' ') {
                cut--;
            }

            end = cut > i ? cut : end;
        }

        memset(line, ' ', usz(pad));
        memcpy(line + pad, s + i, end - i);
        line_add(r, cap, line, usz(pad) + end - i);
        i = end;

        while (i < n && s[i] == ' ') {
            i++;
        }

        first = 0;
    }
}


static void
make_lines(CmpReport *r)
{
    int cap = 0;

    for (int i = 0; i < r->npara; i++) {
        const CmpPara *p = &r->para[i];
        char *t = plain_text(p->s);

        if (i > 0 && (p->kind == CMP_HEAD || p->kind == CMP_ENTRY)) {
            line_add(r, &cap, "", 0);
        }

        if (p->kind == CMP_DETAIL) {
            char *d = cmp_alloc(NULL, strlen(t) + 5);

            memcpy(d, "    ", 4);
            memcpy(d + 4, t, strlen(t) + 1);
            wrap_para(r, &cap, d, 4);
            free(d);
        } else {
            wrap_para(r, &cap, t, 4);
        }

        free(t);

        if (p->kind == CMP_TITLE) {
            line_add(r, &cap, "", 0);
        }
    }
}


/* ------------------------------------------------------------- the driver */
static void
count_entries(CmpReport *r, const Entries *E, const Units *A, const Units *B)
{
    for (int i = 0; i < E->n; i++) {
        const Entry *e = &E->v[i];

        if (e->kind == CE_INS) {
            if (B->u[e->b].level > 0) {
                r->sect_added++;
            } else {
                r->inserted++;
            }
        } else if (e->kind == CE_DEL) {
            if (A->u[e->a].level > 0) {
                r->sect_removed++;
            } else {
                r->deleted++;
            }
        } else if (e->kind == CE_CHG) {
            r->changed++;
        } else if (e->kind == CE_MOVED) {
            if (A->u[e->a].level > 0 && B->u[e->b].level > 0) {
                r->sect_moved++;
            } else {
                r->moved++;
            }
        } else if (e->kind == CE_FMT) {
            r->format_changed++;
        } else if (e->kind == CE_LEVEL) {
            r->sect_level++;
        }
    }
}


int
cmp_compare(const Doc *a, const Doc *b, const char *name_a,
            const char *name_b, CmpReport *r)
{
    Units A;
    Units B;
    UnitPair pair;
    Entries E = {NULL, 0, 0};
    char *ops;
    int *dels;
    int *inss;
    int nops;
    int nd = 0;
    int ni = 0;
    int ia = 0;
    int ib = 0;
    int trunc = 0;

    memset(r, 0, sizeof *r);
    units_load(&A, a);
    units_load(&B, b);
    pair.a = &A;
    pair.b = &B;
    ops = cmp_alloc(NULL, usz(A.n + B.n + 1));
    dels = cmp_alloc(NULL, usz(A.n + 1) * sizeof *dels);
    inss = cmp_alloc(NULL, usz(B.n + 1) * sizeof *inss);
    nops = diff_all(&pair, unit_eq, A.n, B.n, CMP_BLOCK_DMAX, ops, &trunc);
    r->too_large = trunc;

    for (int i = 0; i <= nops; i++) {
        char op = i < nops ? ops[i] : 'e';

        if (op == 'd') {
            dels[nd++] = ia++;
        } else if (op == 'i') {
            inss[ni++] = ib++;
        } else {
            flush_run(&E, &A, &B, dels, nd, inss, ni);
            nd = 0;
            ni = 0;

            if (i < nops) {
                const Unit *x = &A.u[ia];
                const Unit *y = &B.u[ib];
                int flags = (x->fmt != y->fmt ? FLAG_FORMAT : 0) |
                            (x->mrk != y->mrk ? FLAG_MARKS : 0);

                if (x->level != y->level) {
                    entry_add(&E, CE_LEVEL, ia, ib, 0);
                }

                if (flags != 0) {
                    entry_add(&E, CE_FMT, ia, ib, flags);
                }

                ia++;
                ib++;
            }
        }
    }

    find_moves(&E, &A, &B);
    r->blocks_a = A.n;
    r->blocks_b = B.n;
    r->sections_a = A.ns;
    r->sections_b = B.ns;
    count_entries(r, &E, &A, &B);
    para_add(r, CMP_TITLE, "Comparison of two documents");
    para_addf(r, CMP_PLAIN, "First document: %s", name_a);
    para_addf(r, CMP_PLAIN, "Second document: %s", name_b);
    para_addf(r, CMP_PLAIN,
              "The first document has %d block%s and %d section%s; the "
              "second has %d block%s and %d section%s.",
              A.n, plural(A.n), A.ns, plural(A.ns), B.n, plural(B.n), B.ns,
              plural(B.ns));

    if (cmp_same(r) != 0) {
        para_add(r, CMP_PLAIN,
                 "The two documents have the same text, sections and "
                 "format.");
    } else {
        summary(r);
        describe(r, &E, &A, &B);
    }

    make_lines(r);
    free(E.v);
    free(ops);
    free(dels);
    free(inss);
    units_free(&A);
    units_free(&B);

    return 0;
}


int
cmp_same(const CmpReport *r)
{
    return r->inserted + r->deleted + r->changed + r->moved +
               r->format_changed + r->sect_added + r->sect_removed +
               r->sect_moved + r->sect_level + r->too_large ==
           0;
}


void
cmp_free(CmpReport *r)
{
    for (int i = 0; i < r->npara; i++) {
        free(r->para[i].s);
    }

    for (int i = 0; i < r->nlines; i++) {
        free(r->lines[i]);
    }

    free(r->para);
    free(r->lines);
    memset(r, 0, sizeof *r);
}


/* ---------------------------------------------------------- the document */
static Tok
tok_attr(int mask)
{
    Tok k;

    memset(&k, 0, sizeof k);
    k.kind = TK_ATTR;
    k.a = u8(mask);

    return k;
}


static void
text_put(Text *t, Tok k)
{
    text_insert(t, t->n, k);
}


static void
text_puts(Text *t, const char *s)
{
    for (; *s != '\0'; s++) {
        text_put(t, tok_char(u8(*s)));
    }
}


static Block *
para_block(const char *text, int kind)
{
    Block *b = block_new(1);
    Text *t = &b->cols[0].text;
    Tok nl;

    memset(&nl, 0, sizeof nl);
    nl.kind = TK_NL;

    if (kind == CMP_DETAIL) {
        text_puts(t, "    ");
    }

    if (kind == CMP_TITLE || kind == CMP_HEAD) {
        text_put(t, tok_attr(ATTR_BOLD));
    }

    for (const char *s = text; *s != '\0'; s++) {
        if (*s == CMP_INS_ON) {
            text_puts(t, "[+");
            text_put(t, tok_attr(ATTR_BOLD));
        } else if (*s == CMP_INS_OFF) {
            text_put(t, tok_attr(0));
            text_puts(t, "+]");
        } else if (*s == CMP_DEL_ON) {
            text_puts(t, "[-");
            text_put(t, tok_attr(ATTR_UNDERW));
        } else if (*s == CMP_DEL_OFF) {
            text_put(t, tok_attr(0));
            text_puts(t, "-]");
        } else {
            text_put(t, tok_char(u8(*s)));
        }
    }

    if (kind == CMP_TITLE || kind == CMP_HEAD) {
        text_put(t, tok_attr(0));
    }

    text_put(t, nl);

    return b;
}


Doc *
cmp_report_doc(const CmpReport *r)
{
    Doc *d = doc_new(0);

    doc_delete_items(d, 1, 1);
    doc_header_set(d, "MD", "Comparison of two documents");

    for (int i = 0; i < r->npara; i++) {
        const CmpPara *p = &r->para[i];

        if (i > 0 && (p->kind == CMP_HEAD || p->kind == CMP_ENTRY)) {
            doc_insert_item(d, d->nitems, IT_BLOCK, para_block("", CMP_PLAIN));
        }

        doc_insert_item(d, d->nitems, IT_BLOCK, para_block(p->s, p->kind));
    }

    d->modified = 1;

    return d;
}
