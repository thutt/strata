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

/* diag.c - the diagnostics of a print or a preview (see diag.h). */
#include "util.h"
#include "diag.h"
#include "marker.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void
diag_list_init(DiagList *l)
{
    memset(l, 0, sizeof *l);
}


void
diag_list_free(DiagList *l)
{
    free(l->v);
    memset(l, 0, sizeof *l);
}


void
diag_list_clear(DiagList *l)
{
    l->n = 0;
    l->dropped = 0;
}


void
diag_list_set(DiagList *l, const Diag *v, int n, int dropped)
{
    diag_list_clear(l);

    for (int i = 0; i < n; i++) {
        diag_push(l, &v[i]);
    }

    l->dropped += dropped;
}


void
diag_list_copy(DiagList *dst, const DiagList *src)
{
    diag_list_clear(dst);

    for (int i = 0; i < src->n; i++) {
        diag_push(dst, &src->v[i]);
    }

    dst->dropped += src->dropped;
}


/* Make room for one more entry; returns 0, or -1 when the list is full. */
static int
room(DiagList *l)
{
    if (l->n >= DIAG_MAX) {
        l->dropped++;

        return -1;
    }

    if (l->n == l->cap) {
        Diag *nv;
        int cap = l->cap != 0 ? l->cap * 2 : 16;

        nv = realloc(l->v, usz(cap) * sizeof *nv);

        if (nv == NULL) {
            l->dropped++;

            return -1;
        }

        l->v = nv;
        l->cap = cap;
    }

    return 0;
}


Diag *
diag_add(DiagList *l, int kind, const char *code, const char *fmt, ...)
{
    Diag *g;
    va_list ap;

    if (room(l) != 0) {
        return NULL;
    }

    g = &l->v[l->n];
    memset(g, 0, sizeof *g);
    g->kind = kind;
    g->item = DIAG_NOITEM;
    g->off = -1;
    g->seq = l->n;
    str_copy(g->code, sizeof g->code, code);
    va_start(ap, fmt);
    vsnprintf(g->msg, sizeof g->msg, fmt, ap);
    va_end(ap);
    l->n++;

    return g;
}


int
diag_push(DiagList *l, const Diag *g)
{
    if (room(l) != 0) {
        return -1;
    }

    l->v[l->n] = *g;
    l->v[l->n].seq = l->n;
    l->n++;

    return 0;
}


void
diag_counts(const DiagList *l, int *errors, int *warnings)
{
    *errors = 0;
    *warnings = 0;

    for (int i = 0; i < l->n; i++) {
        if (l->v[i].kind == DIAG_ERROR) {
            (*errors)++;
        } else {
            (*warnings)++;
        }
    }
}


/* Compare two diagnostics for the order of the document. */
static int
diag_cmp(const Diag *a, const Diag *b)
{
    int ka = a->item < 0 ? 1 : 0;
    int kb = b->item < 0 ? 1 : 0;

    if (ka != kb) {
        return ka - kb;
    }

    if (a->item != b->item) {
        return a->item < b->item ? -1 : 1;
    }

    if (a->col != b->col) {
        return a->col < b->col ? -1 : 1;
    }

    if (a->line != b->line) {
        return a->line < b->line ? -1 : 1;
    }

    return a->seq - b->seq;
}


void
diag_sort(DiagList *l)
{
    /* An insertion sort is stable and the lists are short. */
    for (int i = 1; i < l->n; i++) {
        Diag g = l->v[i];
        int j = i;

        while (j > 0 && diag_cmp(&l->v[j - 1], &g) > 0) {
            l->v[j] = l->v[j - 1];
            j--;
        }

        l->v[j] = g;
    }
}


/* The S item of the section that holds the item, or -1. */
static int
section_of(const Doc *d, int item)
{
    int k = item;

    while (k >= 0 && k < d->nitems && d->items[k].type == IT_BLOCK) {
        k--;
    }

    return k >= 0 && d->items[k].type == IT_SECT ? k : -1;
}


/* The plain text of the first line of a block, cut to cap - 1 bytes. */
static void
block_title(const Block *b, char *out, size_t cap)
{
    size_t n = 0;

    out[0] = '\0';

    if (b == NULL || b->ncols < 1) {
        return;
    }

    for (int i = 0; i < b->cols[0].text.n && n + 1 < cap; i++) {
        const Tok *k = &b->cols[0].text.v[i];

        if (k->kind == TK_NL) {
            break;
        }

        if (k->kind == TK_CHAR) {
            out[n++] = k->a >= 0x20 && k->a < 0x7F ? chr(k->a) : '?';
        } else if (k->kind == TK_TAB) {
            out[n++] = ' ';
        }
    }

    out[n] = '\0';

    /* Leading and trailing spaces do not belong to the title. */
    while (n > 0 && out[n - 1] == ' ') {
        out[--n] = '\0';
    }

    if (out[0] == ' ') {
        size_t s = 0;

        while (out[s] == ' ') {
            s++;
        }

        memmove(out, out + s, strlen(out + s) + 1);
    }
}


void
diag_locate(const Doc *d, Diag *g)
{
    int s;

    g->sect_ord = 0;
    g->sect_level = 0;
    g->sect_tag[0] = '\0';
    g->sect_title[0] = '\0';
    g->block = 0;

    if (g->item < 0 || g->item >= d->nitems ||
        d->items[g->item].type != IT_BLOCK) {
        return;
    }

    if (d->structured == 0) {
        for (int i = 0; i <= g->item; i++) {
            if (d->items[i].type == IT_BLOCK) {
                g->block++;
            }
        }

        return;
    }

    s = section_of(d, g->item);

    if (s < 0) {
        return;
    }

    {
        int cnt[SECT_MAXDEPTH + 1] = {0};
        int depth = 0;
        int o = 0;

        for (int i = 0; i <= s; i++) {
            if (d->items[i].type == IT_PLUS) {
                depth++;

                if (depth <= SECT_MAXDEPTH) {
                    cnt[depth] = 0;
                }
            } else if (d->items[i].type == IT_MINUS) {
                depth--;
            } else if (d->items[i].type == IT_SECT) {
                g->sect_ord++;

                if (depth >= 0 && depth <= SECT_MAXDEPTH) {
                    cnt[depth]++;
                }
            }
        }

        g->sect_level = depth < 0 ? 0 : depth;

        for (int l = 1; l <= depth && l <= SECT_MAXDEPTH &&
                        o < isz(sizeof g->sect_tag) - 12; l++) {
            o += snprintf(g->sect_tag + o, sizeof g->sect_tag - usz(o),
                          l == 1 ? "%d" : ".%d", cnt[l]);
        }
    }

    g->block = g->item - s;

    if (s + 1 < d->nitems && d->items[s + 1].type == IT_BLOCK) {
        block_title(d->items[s + 1].blk, g->sect_title,
                    sizeof g->sect_title);
    }
}


/* Append a formatted text to out (of cap bytes) at *at, cut to fit. */
static void
put_text(char *out, size_t cap, size_t *at, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
static void
put_text(char *out, size_t cap, size_t *at, const char *fmt, ...)
{
    va_list ap;
    int w;

    if (*at + 1 >= cap) {
        return;
    }

    va_start(ap, fmt);
    w = vsnprintf(out + *at, cap - *at, fmt, ap);
    va_end(ap);

    if (w > 0) {
        *at += usz(w);

        if (*at >= cap) {
            *at = cap - 1;
        }
    }
}


void
diag_position(const Diag *g, char *out, size_t cap)
{
    size_t n = 0;
    const MInfo *mi = g->mtype > 0 ? marker_info(g->mtype) : NULL;

    out[0] = '\0';

    if (g->block == 0 && g->item < 0 && g->texline == 0) {
        snprintf(out, cap, "no position");

        return;
    }

    if (g->block == 0 && g->item < 0) {
        snprintf(out, cap, "line %d of the file that TeX read", g->texline);

        return;
    }

    if (g->sect_ord > 0 && g->sect_level >= 1) {
        put_text(out, cap, &n, "Section %s%s%s, block %d", g->sect_tag,
                 g->sect_title[0] != '\0' ? " " : "", g->sect_title, g->block);
    } else if (g->sect_ord > 0) {
        put_text(out, cap, &n, "Title page, block %d", g->block);
    } else if (g->block > 0) {
        put_text(out, cap, &n, "Block %d", g->block);
    } else {
        put_text(out, cap, &n, "Item %d", g->item);
    }

    if (g->col > 0) {
        put_text(out, cap, &n, ", column %d", g->col);
    }

    if (g->line > 0) {
        put_text(out, cap, &n, ", line %d", g->line);
    }

    if (mi != NULL) {
        put_text(out, cap, &n, ", %s marker", mi->name);
    }
}


/* Copy a text as plain ASCII: a byte that is not printable becomes '?'. */
static void
ascii_copy(char *out, size_t cap, const char *s)
{
    size_t n = 0;

    for (; *s != '\0' && n + 1 < cap; s++) {
        uint8_t c = u8(*s);

        out[n++] = c >= 0x20 && c < 0x7F ? chr(c) : '?';
    }

    out[n] = '\0';
}


void
diag_describe(const Diag *g, char *out, size_t cap)
{
    char pos[200];
    char msg[DIAG_MSG];
    char text[DIAG_TEXT];

    diag_position(g, pos, sizeof pos);
    ascii_copy(msg, sizeof msg, g->msg);
    ascii_copy(text, sizeof text, g->text);

    if (text[0] != '\0') {
        snprintf(out, cap, "%s: %s: %s [%s] \"%s\"",
                 g->kind == DIAG_ERROR ? "error" : "warning", pos, msg,
                 g->code, text);
    } else {
        snprintf(out, cap, "%s: %s: %s [%s]",
                 g->kind == DIAG_ERROR ? "error" : "warning", pos, msg,
                 g->code);
    }
}


/* Put a text in out, cut to DIAG_TEXT - 1 with "..." when it is longer. */
static void
cut_text(const char *s, size_t n, char *out, size_t cap)
{
    size_t m = 0;
    size_t i = 0;
    int lastsp = 1;

    /* Runs of white space are one space, and the ends are trimmed. */
    while (i < n && m + 1 < cap) {
        uint8_t c = u8(s[i++]);

        if (c == ' ' || c == '\n' || c == '\t' || c < 0x20) {
            if (lastsp == 0) {
                out[m++] = ' ';
                lastsp = 1;
            }
        } else {
            out[m++] = chr(c);
            lastsp = 0;
        }
    }

    while (m > 0 && out[m - 1] == ' ') {
        m--;
    }

    out[m] = '\0';

    if (i < n && m >= 3 && cap > 4) {
        if (m + 3 >= cap) {
            m = cap - 4;
        }

        memcpy(out + m, "...", 4);
    }
}


void
diag_marker_text(const Tok *k, char *out, size_t cap)
{
    char buf[1024];

    out[0] = '\0';

    if (tok_mtype(k) == 9) {
        int n = marker_equation_text(k, buf, sizeof buf);

        if (n > 0) {
            cut_text(buf, lsz(n), out, cap);
        }

        return;
    }

    if (tok_mtype(k) == 25) {
        MPayload p;
        const char *name;

        mp_parse(&p, marker_info(25), tok_mdata(k), tok_mlen(k));
        name = mp_get(&p, "1");

        if (name != NULL) {
            cut_text(name, strlen(name), out, cap);
        }

        mp_free(&p);

        return;
    }

    marker_long_label(k, buf, sizeof buf);
    cut_text(buf, strlen(buf), out, cap);
}


/* ------------------------------------------------------------- wire form */
static int
wire_ok(int c)
{
    return is_alnum(c) || c == '.' || c == ',' || c == ':' || c == '/' ||
           c == '-';
}


/* Append key=value (the value percent encoded); returns the new length. */
static int
put_kv(char *out, size_t cap, int n, const char *key, const char *val)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = usz(n);

    if (o != 0 && o + 1 < cap) {
        out[o++] = ' ';
    }

    for (const char *p = key; *p != '\0' && o + 1 < cap; p++) {
        out[o++] = *p;
    }

    if (o + 1 < cap) {
        out[o++] = '=';
    }

    for (const char *p = val; *p != '\0'; p++) {
        uint8_t c = u8(*p);

        if (wire_ok(c) != 0) {
            if (o + 1 >= cap) {
                return -1;
            }

            out[o++] = chr(c);
        } else {
            if (o + 3 >= cap) {
                return -1;
            }

            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }

    out[o] = '\0';

    return isz(o);
}


int
diag_encode(const Diag *g, char *out, size_t cap)
{
    char num[16];
    int n = 0;

    if (cap < 2) {
        return -1;
    }

    out[0] = '\0';
    n = put_kv(out, cap, n, "k", g->kind == DIAG_ERROR ? "E" : "W");
    snprintf(num, sizeof num, "%d", g->item);
    n = n < 0 ? n : put_kv(out, cap, n, "i", num);
    snprintf(num, sizeof num, "%d", g->col);
    n = n < 0 ? n : put_kv(out, cap, n, "o", num);
    snprintf(num, sizeof num, "%d", g->line);
    n = n < 0 ? n : put_kv(out, cap, n, "l", num);
    snprintf(num, sizeof num, "%d", g->mtype);
    n = n < 0 ? n : put_kv(out, cap, n, "t", num);
    snprintf(num, sizeof num, "%d", g->texline);
    n = n < 0 ? n : put_kv(out, cap, n, "x", num);
    snprintf(num, sizeof num, "%d", g->off);
    n = n < 0 ? n : put_kv(out, cap, n, "p", num);
    n = n < 0 ? n : put_kv(out, cap, n, "c", g->code);
    n = n < 0 ? n : put_kv(out, cap, n, "m", g->msg);
    n = n < 0 ? n : put_kv(out, cap, n, "w", g->text);

    return n;
}


static int
hexval(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    return -1;
}


/* Decode a percent encoded value of n bytes into out. */
static void
unwire(const char *s, size_t n, char *out, size_t cap)
{
    size_t o = 0;

    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        if (s[i] == '%' && i + 2 < n && hexval(s[i + 1]) >= 0 &&
            hexval(s[i + 2]) >= 0) {
            out[o++] = chr(hexval(s[i + 1]) * 16 + hexval(s[i + 2]));
            i += 2;
        } else {
            out[o++] = s[i];
        }
    }

    out[o] = '\0';
}


int
diag_decode(Diag *g, const char *s)
{
    int have = 0;

    memset(g, 0, sizeof *g);
    g->item = DIAG_NOITEM;
    g->off = -1;
    g->kind = DIAG_ERROR;

    while (*s != '\0') {
        const char *e;
        const char *eq;
        char val[DIAG_MSG + 1];
        size_t vn;

        while (*s == ' ') {
            s++;
        }

        if (*s == '\0') {
            break;
        }

        e = strchr(s, ' ');
        e = e != NULL ? e : s + strlen(s);
        eq = memchr(s, '=', lsz(e - s));

        if (eq == NULL || eq == s) {
            return -1;
        }

        vn = lsz(e - (eq + 1));
        unwire(eq + 1, vn, val, sizeof val);

        switch (s[0]) {
            case 'k':
                g->kind = val[0] == 'W' ? DIAG_WARNING : DIAG_ERROR;
                have = 1;
                break;

            case 'i':
                g->item = atoi(val);
                break;

            case 'o':
                g->col = atoi(val);
                break;

            case 'l':
                g->line = atoi(val);
                break;

            case 't':
                g->mtype = atoi(val);
                break;

            case 'x':
                g->texline = atoi(val);
                break;

            case 'p':
                g->off = atoi(val);
                break;

            case 'c':
                str_copy(g->code, sizeof g->code, val);
                break;

            case 'm':
                str_copy(g->msg, sizeof g->msg, val);
                break;

            case 'w':
                str_copy(g->text, sizeof g->text, val);
                break;

            default:
                break;
        }

        s = e;
    }

    return have != 0 && g->msg[0] != '\0' ? 0 : -1;
}


/* ------------------------------------------------------------- the items */
void
diag_remap(DiagList *l, const Doc *part, const Doc *real)
{
    diag_remap_array(l->v, l->n, part, real);
}


void
diag_remap_array(Diag *v, int n, const Doc *part, const Doc *real)
{
    for (int i = 0; i < n; i++) {
        Diag *g = &v[i];
        const Block *b = NULL;
        int found = DIAG_NOITEM;

        if (g->item >= 0 && g->item < part->nitems &&
            part->items[g->item].type == IT_BLOCK) {
            b = part->items[g->item].blk;
        }

        for (int j = 0; b != NULL && j < real->nitems; j++) {
            if (real->items[j].type == IT_BLOCK && real->items[j].blk == b) {
                found = j;
                break;
            }
        }

        g->item = found;
        diag_locate(real, g);
    }
}


static int
is_print_error(const Tok *k)
{
    return k->kind == TK_MARKER && tok_mtype(k) == MT_PRINT_ERROR;
}


/* The token that starts line n (1 is the first) of a text, or -1. */
static int
line_start(const Text *t, int n)
{
    int line = 1;

    if (n < 1) {
        return -1;
    }

    if (n == 1) {
        return 0;
    }

    for (int i = 0; i < t->n; i++) {
        if (t->v[i].kind == TK_NL && ++line == n) {
            return i + 1;
        }
    }

    return -1;
}


/* The marker of a type in tokens [s, e) whose text starts with want. */
static int
find_marker(const Text *t, int s, int e, int mtype, const char *want)
{
    int any = -1;

    for (int i = s; i < e && i < t->n; i++) {
        char txt[DIAG_TEXT];

        if (t->v[i].kind != TK_MARKER || tok_mtype(&t->v[i]) != mtype) {
            continue;
        }

        diag_marker_text(&t->v[i], txt, sizeof txt);

        if (strcmp(txt, want) == 0) {
            return i;
        }

        if (any < 0) {
            any = i;
        }
    }

    /* A marker of the type whose text was edited is the nearest place. */
    return want[0] == '\0' ? any : -(any + 2);
}


/* The headline of the section at S item s, as diag_locate reads it. */
static int
title_is(const Doc *d, int s, const char *title)
{
    char t[DIAG_TITLE];

    if (s + 1 >= d->nitems || d->items[s + 1].type != IT_BLOCK) {
        return title[0] == '\0';
    }

    block_title(d->items[s + 1].blk, t, sizeof t);

    return strcmp(t, title) == 0;
}


int
diag_refind(const Doc *d, Diag *g, int *item, int *col, int *tok)
{
    int s = -1;
    int exact = 1;
    int it;
    int c = 0;
    int t = 0;
    const Block *b;

    *item = 0;
    *col = 0;
    *tok = 0;
    g->moved = 0;

    if (d->nitems == 0) {
        g->moved = 1;

        return 0;
    }

    if (g->block == 0 && g->sect_ord == 0 && g->item < 0) {
        /* There was never a position: the start of the document. */
        g->moved = 1;

        for (int i = 0; i < d->nitems; i++) {
            if (d->items[i].type == IT_BLOCK) {
                *item = i;
                break;
            }
        }

        return 0;
    }

    if (d->structured != 0 && g->sect_ord > 0) {
        int ord = 0;
        int first_with_title = -1;
        int nth = -1;

        for (int i = 0; i < d->nitems; i++) {
            if (d->items[i].type != IT_SECT) {
                continue;
            }

            ord++;

            if (ord == g->sect_ord) {
                nth = i;
            }

            if (first_with_title < 0 && title_is(d, i, g->sect_title) != 0 &&
                (g->sect_title[0] != '\0' || ord == g->sect_ord)) {
                first_with_title = i;
            }
        }

        if (nth >= 0 && title_is(d, nth, g->sect_title) != 0) {
            s = nth;
        } else if (first_with_title >= 0) {
            s = first_with_title;
            exact = 0;
        } else {
            s = nth >= 0 ? nth : -1;
            exact = 0;
        }

        if (s < 0) {
            /* The section is gone and so is its ordinal: the last one. */
            for (int i = d->nitems - 1; i >= 0; i--) {
                if (d->items[i].type == IT_SECT) {
                    s = i;
                    break;
                }
            }

            exact = 0;
        }

        if (s < 0) {
            g->moved = 1;
            *item = 0;

            return 0;
        }

        it = s + g->block;

        if (g->block < 1 || it >= d->nitems ||
            d->items[it].type != IT_BLOCK) {
            /* The block is gone: the headline of the section. */
            it = s + 1 < d->nitems && d->items[s + 1].type == IT_BLOCK ?
                     s + 1 : s;
            exact = 0;
        }
    } else {
        int seen = 0;

        it = -1;

        for (int i = 0; i < d->nitems; i++) {
            if (d->items[i].type == IT_BLOCK && ++seen == g->block) {
                it = i;
                break;
            }
        }

        if (it < 0) {
            for (int i = d->nitems - 1; i >= 0; i--) {
                if (d->items[i].type == IT_BLOCK) {
                    it = i;
                    break;
                }
            }

            exact = 0;
        }

        if (it < 0) {
            g->moved = 1;

            return 0;
        }
    }

    *item = it;

    if (d->items[it].type != IT_BLOCK) {
        g->moved = 1;

        return 0;
    }

    b = d->items[it].blk;
    c = g->col > 0 ? g->col - 1 : 0;

    if (c >= b->ncols) {
        c = 0;
        exact = 0;
    }

    *col = c;

    if (g->line > 0) {
        int ls = line_start(&b->cols[c].text, g->line);

        if (ls < 0) {
            exact = 0;
        } else {
            t = ls;

            if (g->mtype == 0 && g->off > 0) {
                const Text *tx = &b->cols[c].text;
                int k = ls;
                int seen = 0;

                /* The Print Errors of the Editor are not in the offset. */
                while (k < tx->n && tx->v[k].kind != TK_NL &&
                       (seen < g->off || is_print_error(&tx->v[k]) != 0)) {
                    if (is_print_error(&tx->v[k]) == 0) {
                        seen++;
                    }

                    k++;
                }

                t = k;
            }
        }
    }

    if (g->mtype > 0) {
        const Text *tx = &b->cols[c].text;
        int e = tx->n;
        int m;

        if (g->line > 0 && exact != 0) {
            int ne = line_start(tx, g->line + 1);

            e = ne >= 0 ? ne : tx->n;
        } else {
            t = 0;
        }

        m = find_marker(tx, t, e, g->mtype, g->text);

        if (m >= 0) {
            t = m;
        } else {
            if (m < -1) {
                t = -(m + 2);
            }

            exact = 0;
        }
    }

    *tok = t;
    g->moved = exact != 0 ? 0 : 1;

    return exact;
}


/* ------------------------------------------------------------- TeX logs */
int
diag_tex_errors(const char *log, TexErr *v, int max)
{
    const char *p = log;
    int n = 0;

    while (*p != '\0' && n < max) {
        const char *e = strchr(p, '\n');
        size_t len = e != NULL ? lsz(e - p) : strlen(p);

        if (len >= 2 && p[0] == '!' && p[1] == ' ' &&
            strncmp(p, "! Emergency stop", 16) != 0 &&
            strncmp(p, "! ==> Fatal error", 17) != 0) {
            TexErr *x = &v[n];
            const char *q = e != NULL ? e + 1 : p + len;

            memset(x, 0, sizeof *x);
            len -= 2;
            len = len < sizeof x->msg - 1 ? len : sizeof x->msg - 1;
            memcpy(x->msg, p + 2, len);
            x->msg[len] = '\0';

            for (int k = 0; k < 14 && *q != '\0'; k++) {
                const char *qe = strchr(q, '\n');

                if (q[0] == 'l' && q[1] == '.' && is_digit(q[2])) {
                    const char *r = q + 2;
                    size_t cl;

                    x->line = atoi(r);

                    while (is_digit(*r)) {
                        r++;
                    }

                    while (*r == ' ') {
                        r++;
                    }

                    cl = qe != NULL ? lsz(qe - r) : strlen(r);
                    cl = cl < sizeof x->ctx - 1 ? cl : sizeof x->ctx - 1;
                    memcpy(x->ctx, r, cl);
                    x->ctx[cl] = '\0';

                    break;
                }

                if (qe == NULL) {
                    break;
                }

                q = qe + 1;
            }

            n++;
        }

        if (e == NULL) {
            break;
        }

        p = e + 1;
    }

    return n;
}


int
diag_tex_position(const char *tex, int texline, int *item, int *col,
                  int *line)
{
    const char *p = tex;
    int ln = 1;
    int found = 0;

    while (*p != '\0' && ln <= texline) {
        const char *e = strchr(p, '\n');

        if (p[0] == '%' && p[1] == '@' && p[2] == 'P' && p[3] == ' ') {
            int a = 0;
            int b = 0;
            int c = 0;

            if (sscanf(p + 4, "%d %d %d", &a, &b, &c) == 3) {
                *item = a;
                *col = b;
                *line = c;
                found = 1;
            }
        }

        if (e == NULL) {
            break;
        }

        p = e + 1;
        ln++;
    }

    return found;
}


/* ----------------------------------------------------------- error file */
int
diag_file_write(const char *path, const DiagList *l, int status)
{
    FILE *f = fopen(path, "w");
    int errors;
    int warnings;
    int bad = 0;

    if (f == NULL) {
        return -1;
    }

    fprintf(f, "%s\n", DIAG_FILE_MAGIC);

    for (int i = 0; i < l->n; i++) {
        char wire[4 * DIAG_MSG + 400];

        if (diag_encode(&l->v[i], wire, sizeof wire) > 0) {
            fprintf(f, "diag %s\n", wire);
        }
    }

    diag_counts(l, &errors, &warnings);
    fprintf(f, "end errors=%d warnings=%d dropped=%d status=%d\n", errors,
            warnings, l->dropped, status);
    bad = fflush(f) != 0 || ferror(f) != 0;

    if (fclose(f) != 0) {
        bad = 1;
    }

    return bad != 0 ? -1 : 0;
}


int
diag_file_read(const char *path, DiagList *l, int *complete, int *status)
{
    FILE *f = fopen(path, "r");
    char line[4 * DIAG_MSG + 600];
    int first = 1;

    *complete = 0;
    *status = -1;

    if (f == NULL) {
        return -1;
    }

    while (fgets(line, sizeof line, f) != NULL) {
        size_t n = strlen(line);

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }

        if (first != 0) {
            first = 0;

            if (strcmp(line, DIAG_FILE_MAGIC) != 0) {
                fclose(f);

                return -1;
            }
        } else if (strncmp(line, "diag ", 5) == 0) {
            Diag g;

            if (diag_decode(&g, line + 5) == 0) {
                g.seq = l->n;
                diag_push(l, &g);
            }
        } else if (strncmp(line, "end ", 4) == 0) {
            const char *p = strstr(line, "status=");
            const char *d = strstr(line, "dropped=");

            *complete = 1;
            *status = p != NULL ? atoi(p + 7) : -1;
            l->dropped = d != NULL ? atoi(d + 8) : 0;
        }
    }

    fclose(f);

    return first != 0 ? -1 : 0;
}
