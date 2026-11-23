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

/* fileops.c - what the File menu does to a document. */
#include "util.h"
#include "fileops.h"
#include "backup.h"
#include "marker.h"
#include "ui.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* -------------------------------------------------------------- copying */
Doc *
doc_clone(const Doc *d)
{
    Doc *c = calloc(1, sizeof *c);

    c->structured = d->structured;

    for (int i = 0; i < d->header.n; i++) {
        lines_add(&c->header, d->header.v[i].s, usz(d->header.v[i].n));
    }

    c->tail_len = d->tail_len;
    c->tail = malloc(d->tail_len + 1);

    if (d->tail_len > 0) {
        memcpy(c->tail, d->tail, d->tail_len);
    }

    for (int i = 0; i < d->nitems; i++) {
        Block *b = d->items[i].type == IT_BLOCK ?
                       block_clone_full(d->items[i].blk) :
                       NULL;

        doc_insert_item(c, c->nitems, d->items[i].type, b);
        c->items[c->nitems - 1].fold = d->items[i].fold;
        c->items[c->nitems - 1].se = d->items[i].se;
    }

    c->modified = 0;

    return c;
}


/* ------------------------------------------------------------- retrieve */
static bool
is_code(const Str *r, const char *code)
{
    return r->n >= 2 && r->s[0] == u8(code[0]) && r->s[1] == u8(code[1]);
}


static const Str *
find_rec(const Lines *l, const char *code)
{
    for (int i = 0; i < l->n; i++) {
        if (is_code(&l->v[i], code)) {
            return &l->v[i];
        }
    }

    return NULL;
}


static void
add_rec(Lines *l, const Str *r)
{
    lines_add(l, r->s, usz(r->n));
}


/* The settings of dst become those of src, except the revision. */
static void
retrieve_global(Doc *dst, const Doc *src)
{
    Lines nl = {0};
    const Str *mr = find_rec(&dst->header, "MR");

    for (int i = 0; i < dst->header.n; i++) {
        if (is_code(&dst->header.v[i], "*R")) {
            add_rec(&nl, &dst->header.v[i]);
        }
    }

    for (int i = 0; i < src->header.n; i++) {
        const Str *r = &src->header.v[i];

        if (is_code(r, "*R") || is_code(r, "*B")) {
            continue;
        }

        if (is_code(r, "MR")) {
            if (mr != NULL) {
                add_rec(&nl, mr);
            }

            continue;
        }

        add_rec(&nl, r);
    }

    if (mr != NULL && find_rec(&nl, "MR") == NULL) {
        add_rec(&nl, mr);
    }

    for (int i = 0; i < dst->header.n; i++) {
        if (is_code(&dst->header.v[i], "*B")) {
            add_rec(&nl, &dst->header.v[i]);
        }
    }

    lines_free(&dst->header);
    dst->header = nl;
}


/* The typeface settings are the DF and DP records. */
static bool
is_typeface(const Str *r)
{
    return is_code(r, "DF") || is_code(r, "DP");
}


static void
retrieve_typeface(Doc *dst, const Doc *src)
{
    Lines nl = {0};
    int first = -1;

    for (int i = 0; i < dst->header.n; i++) {
        const Str *r = &dst->header.v[i];

        if (is_typeface(r)) {
            if (first < 0) {
                first = nl.n;
            }

            continue;
        }

        add_rec(&nl, r);
    }

    if (first < 0) {
        first = nl.n;

        for (int i = 0; i < nl.n; i++) {
            if (is_code(&nl.v[i], "YC") || is_code(&nl.v[i], "*B")) {
                first = i;
                break;
            }
        }
    }

    Lines add = {0};

    for (int i = 0; i < src->header.n; i++) {
        if (is_typeface(&src->header.v[i])) {
            add_rec(&add, &src->header.v[i]);
        }
    }

    Lines out = {0};

    for (int i = 0; i < nl.n; i++) {
        if (i == first) {
            for (int k = 0; k < add.n; k++) {
                add_rec(&out, &add.v[k]);
            }
        }

        add_rec(&out, &nl.v[i]);
    }

    if (first >= nl.n) {
        for (int k = 0; k < add.n; k++) {
            add_rec(&out, &add.v[k]);
        }
    }

    lines_free(&nl);
    lines_free(&add);
    lines_free(&dst->header);
    dst->header = out;
}


/* Keep the structure flag of the header in step with the document. */
static void
set_structure(Doc *d, int structured)
{
    d->structured = structured;

    for (int i = 0; i < d->header.n; i++) {
        Str *r = &d->header.v[i];

        if (is_code(r, "*B") && r->n >= 3) {
            r->s[2] = structured != 0 ? '1' : '0';
        }
    }
}


static void
clone_items(Doc *dst, int at, const Doc *src)
{
    for (int i = 0; i < src->nitems; i++) {
        Block *b = src->items[i].type == IT_BLOCK ?
                       block_clone_full(src->items[i].blk) :
                       NULL;

        doc_insert_item(dst, at + i, src->items[i].type, b);
        dst->items[at + i].fold = src->items[i].fold;
        dst->items[at + i].se = src->items[i].se;
    }
}


int
doc_retrieve_into(Doc *dst, const Doc *src, RetrieveMode mode, int cur_item,
                  char *err, size_t errlen)
{
    switch (mode) {
        case RET_BODY:
            doc_delete_items(dst, 0, dst->nitems);
            clone_items(dst, 0, src);
            set_structure(dst, src->structured);
            break;

        case RET_GLOBAL:
            retrieve_global(dst, src);
            break;

        case RET_TYPEFACE:
            retrieve_typeface(dst, src);
            break;

        case RET_INSERT: {
            int at = dst->nitems;

            if (dst->structured != 0) {
                int s = sect_start(dst, cur_item);

                at = s >= 0 ? sect_end(dst, s) : dst->nitems;
            } else if (cur_item >= 0 && cur_item < dst->nitems) {
                at = cur_item + 1;
            }

            clone_items(dst, at, src);
            break;
        }

        case RET_NAMED:
            if (doc_named_copy(dst, src) == 0) {
                snprintf(err, errlen, "The file has no named block settings");

                return -1;
            }

            break;

        case RET_ENTIRE:
        default:
            snprintf(err, errlen, "Nothing to apply");

            return -1;
    }

    doc_touch(dst);

    return 0;
}


/* --------------------------------------------------------------- save */
static int
write_bytes(const char *path, const uint8_t *b, size_t n, char *err,
            size_t errlen)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL) {
        snprintf(err, errlen, "cannot write %s", path);

        return -1;
    }

    int ok = fwrite(b, 1, n, f) == n;

    ok = (fclose(f) == 0) && ok != 0;

    if (ok == 0) {
        snprintf(err, errlen, "write error");

        return -1;
    }

    return 0;
}


/*
 * A document that holds a part of d: the global settings with no text, or
 * the current section (with its subsections), or the current block, as the
 * format writes them.  The items are the ones of d, so it is only for
 * reading; free it with part_free.
 */
static int
part_make(Doc *d, SaveMode mode, int cur_item, Doc *tmp)
{
    *tmp = *d;
    doc_clear_view_pos(tmp);
    doc_count_forget(tmp);
    tmp->items = NULL;
    tmp->nitems = 0;

    if (mode == SAVE_GLOBAL) {
        return 0;
    }

    int first;
    int last;

    if (d->structured != 0) {
        first = sect_start(d, cur_item);

        if (first < 0) {
            return -1;
        }

        last = sect_end(d, first);
    } else {
        if (is_edit_block(d, cur_item) == 0) {
            return -1;
        }

        first = cur_item;
        last = cur_item + 1;
    }

    int n = last - first + (d->structured != 0 ? 3 : 1);

    tmp->items = malloc(usz(n) * sizeof *tmp->items);

    int k = 0;

    memset(&tmp->items[k], 0, sizeof tmp->items[k]);
    tmp->items[k++].type = IT_SECT;

    if (d->structured != 0) {
        memset(&tmp->items[k], 0, sizeof tmp->items[k]);
        tmp->items[k++].type = IT_PLUS;
    }

    for (int i = first; i < last; i++) {
        tmp->items[k++] = d->items[i];
    }

    if (d->structured != 0) {
        memset(&tmp->items[k], 0, sizeof tmp->items[k]);
        tmp->items[k++].type = IT_MINUS;
    }

    tmp->nitems = k;

    return 0;
}


/* -------------------------------------------------------- the title page */
uint8_t *
doc_title_bytes(Doc *d, size_t *len)
{
    int lo;
    int hi;

    *len = 0;

    if (doc_title_range(d, &lo, &hi) == 0 || hi == lo) {
        return NULL;
    }

    Doc tmp = *d;

    doc_clear_view_pos(&tmp);
    tmp.structured = 0;
    tmp.tail = NULL;
    tmp.tail_len = 0;
    tmp.nitems = hi - lo + 1;
    tmp.items = calloc(usz(tmp.nitems), sizeof *tmp.items);
    tmp.items[0].type = IT_SECT;

    for (int i = lo; i < hi; i++) {
        tmp.items[i - lo + 1] = d->items[i];
    }

    size_t all_len;
    uint8_t *all = doc_serialize(&tmp, &all_len);
    size_t start = 0;

    for (int i = 0; i < d->header.n; i++) {
        start += usz(d->header.v[i].n) + 2;
    }

    size_t n = all_len > start ? all_len - start : 0;
    uint8_t *out = malloc(n + 1);

    memcpy(out, all + start, n);
    *len = n;
    free(all);
    free(tmp.items);

    return out;
}


/* Is the block empty: one column, no text but the end-of-block mark? */
static int
block_is_blank(const Block *b)
{
    char tx[8];

    if (b->ncols != 1 || b->props.n != 0 || b->cols[0].props.n != 0) {
        return 0;
    }

    text_plain(&b->cols[0].text, tx, sizeof tx);

    for (const char *c = tx; *c != '\0'; c++) {
        if (*c != ' ') {
            return 0;
        }
    }

    return 1;
}


int
doc_set_title(Doc *d, const uint8_t *bytes, size_t len, char *err,
              size_t errlen)
{
    static const char head[] = DOC_FIRST_RECORD "\r\nMR1006\r\n*B0\r\n";
    static const char foot[] = "*E\r\n";
    size_t hl = sizeof head - 1;
    uint8_t *buf = malloc(hl + len + sizeof foot);

    memcpy(buf, head, hl);
    memcpy(buf + hl, bytes, len);
    memcpy(buf + hl + len, foot, sizeof foot - 1);

    Doc *nd = doc_load_mem(buf, hl + len + sizeof foot - 1, err, errlen);

    free(buf);

    if (nd == NULL) {
        return -1;
    }

    int nb = 0;
    int blank = 1;

    for (int i = 0; i < nd->nitems; i++) {
        if (is_edit_block(nd, i) != 0) {
            nb++;
            blank = blank != 0 && block_is_blank(nd->items[i].blk) != 0;
        }
    }

    int lo;
    int hi;
    int have = doc_title_range(d, &lo, &hi);
    int remove = nb == 0 || (nb == 1 && blank != 0);

    if (have != 0) {
        doc_delete_items(d, lo, hi - lo);
    }

    if (remove != 0) {
        if (have != 0) {
            /* The flag and the S item go too. */
            doc_delete_items(d, lo - 2, 2);
        }
    } else {
        if (have == 0) {
            int s0 = 0;

            while (s0 < d->nitems && d->items[s0].type != IT_SECT) {
                s0++;
            }

            Block *flag = block_new(0);

            lines_add(&flag->props, "=1", 2);
            doc_insert_item(d, s0, IT_BLOCK, flag);
            doc_insert_item(d, s0 + 1, IT_SECT, NULL);
            lo = s0 + 2;
        }

        int at = lo;

        for (int i = 0; i < nd->nitems; i++) {
            if (is_edit_block(nd, i) != 0) {
                doc_insert_item(d, at++, IT_BLOCK,
                                block_clone_full(nd->items[i].blk));
            }
        }
    }

    doc_touch(d);
    doc_free(nd);

    return 0;
}


/* ---------------------------------------------------------- parts of text */
int
doc_find_section(const Doc *d, const char *num)
{
    return d != NULL ? sect_find(d, num) : -1;
}


/*
 * Items first up to last of d, as a document of its own.  The part keeps the
 * whole structure of d: every section start, plus and minus item in front
 * of first stays, and only the blocks in front of first are left out.  So
 * every section in the part is on the same level and has the same number
 * and the same tag as in the whole document (a part that is a section of
 * level 2.3 prints as 2.3, not as 1), and the Printer needs no knowledge
 * of the whole document.  The groups that are still open at last are
 * closed with minus items.  A block that is not the headline of its section
 * would take the place of the headline when the headline is left out: a
 * plus and a minus item are put in front of it, which change nothing in the
 * numbers and make it a block of text.
 */
static int
part_keep(const Doc *d, int i, int first, int last)
{
    return (i >= first && i < last) || d->items[i].type != IT_BLOCK;
}


/*
 * The orientation in force before item first: 2 when the last orientation
 * marker (type 36) before it is landscape, else 0.
 */
static int
orient_before(const Doc *d, int first)
{
    int cur = 0;

    for (int i = 0; i < first && i < d->nitems; i++) {
        const Block *blk;

        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        blk = d->items[i].blk;

        for (int col = 0; col < blk->ncols; col++) {
            const Text *t = &blk->cols[col].text;

            for (int j = 0; j < t->n; j++) {
                const Tok *k = &t->v[j];
                MPayload p;
                const char *v;

                if (k->kind != TK_MARKER || tok_mtype(k) != 36) {
                    continue;
                }

                mp_parse(&p, marker_info(36), tok_mdata(k), tok_mlen(k));
                v = mp_get(&p, "1");
                cur = v != NULL && atoi(v) == 2 ? 2 : 0;
                mp_free(&p);
            }
        }
    }

    return cur;
}


static void
part_items(Doc *d, int first, int last, Doc *out)
{
    int depth = 0;
    int n = 0;
    int total = 0;

    *out = *d;
    doc_clear_view_pos(out);
    doc_count_forget(out);
    /* A landscape marker before the part still holds for the part. */
    out->part_orient = orient_before(d, first);

    if (d->structured == 0) {
        out->items = malloc(usz(last - first + 8) * sizeof *out->items);
        out->nitems = 0;

        for (int i = first; i < last; i++) {
            out->items[out->nitems++] = d->items[i];
        }

        return;
    }

    /* The number of items: the kept ones, the closing minus items and the
       two that keep a block of text from becoming a headline. */
    for (int i = 0; i < last; i++) {
        if (part_keep(d, i, first, last) != 0) {
            total++;
        }

        if (d->items[i].type == IT_PLUS) {
            depth++;
        } else if (d->items[i].type == IT_MINUS) {
            depth--;
        }
    }

    out->items = calloc(usz(total + depth + 8), sizeof *out->items);

    for (int i = 0; i < last; i++) {
        if (part_keep(d, i, first, last) == 0) {
            continue;
        }

        if (i == first && d->items[i].type == IT_BLOCK && i > 0 &&
            d->items[i - 1].type != IT_SECT && n > 0 &&
            out->items[n - 1].type == IT_SECT) {
            out->items[n++].type = IT_PLUS;
            out->items[n++].type = IT_MINUS;
        }

        out->items[n++] = d->items[i];
    }

    for (; depth > 0; depth--) {
        out->items[n++].type = IT_MINUS;
    }

    out->nitems = n;
}


int
doc_part_sections(Doc *d, const char *from, const char *to, Doc *out,
                  char *err, size_t errlen)
{
    int first = 0;
    int last = d->nitems;

    /* The title page is not a section: a range starts after it, at the
       first group of sections. */
    for (int i = 0; d->structured != 0 && i < d->nitems; i++) {
        if (d->items[i].type == IT_PLUS) {
            first = i;
            break;
        }
    }

    if (from[0] != '\0') {
        int b = doc_find_section(d, from);

        if (b < 0) {
            snprintf(err, errlen, "Cannot find section number %s", from);

            return -1;
        }

        first = sect_start(d, b);
    }

    if (to[0] != '\0') {
        int b = doc_find_section(d, to);

        if (b < 0) {
            snprintf(err, errlen, "Cannot find section number %s", to);

            return -1;
        }

        last = sect_end(d, sect_start(d, b));
    }

    if (first >= last) {
        snprintf(err, errlen, "The first section is after the last");

        return -1;
    }

    part_items(d, first, last, out);

    return 0;
}


int
doc_part_current(Doc *d, int cur_item, Doc *out)
{
    int first;
    int last;

    if (d->structured != 0) {
        first = sect_start(d, cur_item);

        if (first < 0) {
            return -1;
        }

        last = sect_end(d, first);
    } else {
        if (is_edit_block(d, cur_item) == 0) {
            return -1;
        }

        first = cur_item;
        last = cur_item + 1;
    }

    part_items(d, first, last, out);

    return 0;
}


int
doc_part_block(Doc *d, int cur_item, Doc *out)
{
    if (is_edit_block(d, cur_item) == 0) {
        return -1;
    }

    part_items(d, cur_item, cur_item + 1, out);

    return 0;
}


int
doc_part_untitled(Doc *d, Doc *out)
{
    *out = *d;
    doc_clear_view_pos(out);
    doc_count_forget(out);
    out->items = malloc(usz(d->nitems + 1) * sizeof *out->items);
    out->nitems = 0;

    for (int i = 0; i < d->nitems; i++) {
        if (d->structured != 0 && is_edit_block(d, i) != 0 &&
            doc_item_level(d, i) < 1) {
            continue;
        }

        out->items[out->nitems++] = d->items[i];
    }

    return 0;
}


void
doc_part_free(Doc *p)
{
    free(p->items);
    p->items = NULL;
    p->nitems = 0;
}


int
doc_save_mode(Doc *d, const char *path, SaveMode mode, int cur_item, char *err,
              size_t errlen)
{
    if (backup_is_backup_file(path) != 0) {
        snprintf(err, errlen, "%s", BACKUP_REFUSED_MSG);

        return -1;
    }

    if (mode == SAVE_ENTIRE) {
        return doc_save(d, path, err, errlen);
    }

    Doc tmp;

    if (part_make(d, mode, cur_item, &tmp) != 0) {
        snprintf(err, errlen, "There is no current section");

        return -1;
    }

    size_t len;
    uint8_t *b = doc_serialize(&tmp, &len);
    int rc = write_bytes(path, b, len, err, errlen);

    free(b);
    free(tmp.items);

    return rc;
}


/* ------------------------------------------------------------ character sets
 */
static const uint8_t cp850_to437[128] = {
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B,
    0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0x3F, 0x9C, 0x3F, 0x3F, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0x3F, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0x3F, 0x3F, 0x3F, 0x3F, 0xB9, 0xBA, 0xBB,
    0xBC, 0x9B, 0x9D, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0x3F, 0x3F,
    0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0xD9, 0xDA, 0xDB, 0xDC, 0x3F, 0x3F, 0xDF,
    0x3F, 0xE1, 0x3F, 0x3F, 0x3F, 0x3F, 0xE6, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0xF1, 0x3F, 0x3F, 0x3F, 0x3F, 0xF6, 0x3F,
    0xF8, 0x3F, 0xFA, 0x3F, 0x3F, 0xFD, 0xFE, 0xFF,
};

static const uint8_t cp850_from437[128] = {
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B,
    0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0xBD, 0x9C, 0xBE, 0x3F, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0x3F, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0x3F, 0x3F, 0x3F, 0x3F, 0xB9, 0xBA, 0xBB,
    0xBC, 0x3F, 0x3F, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0x3F, 0x3F,
    0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0xD9, 0xDA, 0xDB, 0xDC, 0x3F, 0x3F, 0xDF,
    0x3F, 0xE1, 0x3F, 0x3F, 0x3F, 0x3F, 0xE6, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0xF1, 0x3F, 0x3F, 0x3F, 0x3F, 0xF6, 0x3F,
    0xF8, 0x3F, 0xFA, 0x3F, 0x3F, 0xFD, 0xFE, 0xFF,
};

static const uint8_t cp860_to437[128] = {
    0x80, 0x81, 0x82, 0x83, 0x3F, 0x85, 0x3F, 0x87, 0x88, 0x3F, 0x8A, 0x3F,
    0x3F, 0x8D, 0x3F, 0x3F, 0x90, 0x3F, 0x3F, 0x93, 0x3F, 0x95, 0x3F, 0x97,
    0x3F, 0x3F, 0x9A, 0x9B, 0x9C, 0x3F, 0x9E, 0x3F, 0xA0, 0xA1, 0xA2, 0xA3,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0x3F, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB,
    0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3,
    0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB,
    0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
    0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};

static const uint8_t cp860_from437[128] = {
    0x80, 0x81, 0x82, 0x83, 0x3F, 0x85, 0x3F, 0x87, 0x88, 0x3F, 0x8A, 0x3F,
    0x3F, 0x8D, 0x3F, 0x3F, 0x90, 0x3F, 0x3F, 0x93, 0x3F, 0x95, 0x3F, 0x97,
    0x3F, 0x3F, 0x9A, 0x9B, 0x9C, 0x3F, 0x9E, 0x3F, 0xA0, 0xA1, 0xA2, 0xA3,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0x3F, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB,
    0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3,
    0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB,
    0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
    0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};

static const uint8_t cp865_to437[128] = {
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B,
    0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0x3F, 0x9C, 0x3F, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0x3F,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB,
    0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3,
    0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB,
    0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
    0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};

static const uint8_t cp865_from437[128] = {
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B,
    0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0x3F, 0x9C, 0x3F, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0x3F,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB,
    0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3,
    0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB,
    0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
    0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};

static const uint8_t cp437_to_ascii7[128] = {
    0x43, 0x75, 0x65, 0x61, 0x61, 0x61, 0x61, 0x63, 0x65, 0x65, 0x65, 0x69,
    0x69, 0x69, 0x41, 0x41, 0x45, 0x3F, 0x3F, 0x6F, 0x6F, 0x6F, 0x75, 0x75,
    0x79, 0x4F, 0x55, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x61, 0x69, 0x6F, 0x75,
    0x6E, 0x4E, 0x61, 0x6F, 0x3F, 0x3F, 0x3F, 0x31, 0x31, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
    0x3F, 0x3F, 0x3F, 0x3F, 0x6E, 0x32, 0x3F, 0x20,
};


int
cp437_of_unicode(unsigned cp)
{
    for (int i = 0x20; i < 256; i++) {
        if (cp437_uni[i] == cp) {
            return i;
        }
    }

    return -1;
}


uint8_t
charset_to_437(Charset cs, uint8_t b)
{
    if (b < 0x80) {
        return b;
    }

    switch (cs) {
        case CS_ASCII7:
            return b & 0x7F;

        case CS_LATIN1: {
            int k = cp437_of_unicode(b);

            return k < 0 ? '?' : u8(k);
        }

        case CS_UTF8:
            return '?';

        case CS_850:
            return cp850_to437[b - 0x80];

        case CS_860:
            return cp860_to437[b - 0x80];

        case CS_865:
            return cp865_to437[b - 0x80];

        case CS_437:
        default:
            return b;
    }
}


uint8_t
charset_from_437(Charset cs, uint8_t b)
{
    /* The bullet of a list is written as an asterisk, as the format does. */
    if (b == 0x07) {
        return '*';
    }

    if (b == 0x7F && cs == CS_LATIN1) {
        /* The house glyph of the Editor is not in Latin-1. */
        return '?';
    }

    if (b < 0x80) {
        return b;
    }

    switch (cs) {
        case CS_LATIN1:
            return cp437_uni[b] < 0x100 ? u8(cp437_uni[b]) : '?';

        case CS_UTF8:
            return '?';

        case CS_ASCII7:
            return cp437_to_ascii7[b - 0x80];

        case CS_850:
            return cp850_from437[b - 0x80];

        case CS_860:
            return cp860_from437[b - 0x80];

        case CS_865:
            return cp865_from437[b - 0x80];

        case CS_437:
        default:
            return b;
    }
}


void
charset_put(FILE *f, Charset cs, uint8_t b)
{
    if (cs != CS_UTF8) {
        fputc(charset_from_437(cs, b), f);

        return;
    }

    if (b == 0x07) {
        /* The bullet of a list is an asterisk in every character set. */
        fputc('*', f);
    } else if (b < 0x20) {
        /* A control byte is kept as it is, as the other sets do. */
        fputc(b, f);
    } else {
        utf8_put(f, cp437_uni[b]);
    }
}


size_t
utf8_decode_strict(const uint8_t *s, size_t left, unsigned *cp)
{
    size_t need;
    unsigned v;
    unsigned lo;

    if (left == 0) {
        return 0;
    }

    if (s[0] < 0x80) {
        *cp = s[0];

        return 1;
    }

    if (s[0] >= 0xC2 && s[0] <= 0xDF) {
        need = 1;
        v = s[0] & 0x1FU;
        lo = 0x80;
    } else if (s[0] >= 0xE0 && s[0] <= 0xEF) {
        need = 2;
        v = s[0] & 0x0FU;
        lo = 0x800;
    } else if (s[0] >= 0xF0 && s[0] <= 0xF4) {
        need = 3;
        v = s[0] & 0x07U;
        lo = 0x10000;
    } else {
        return 0;
    }

    if (left < need + 1) {
        return 0;
    }

    for (size_t i = 1; i <= need; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            return 0;
        }

        v = (v << 6) | (s[i] & 0x3FU);
    }

    if (v < lo || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) {
        return 0;
    }

    *cp = v;

    return need + 1;
}


/* ----------------------------------------------------------- import text */
/* Put one CP 437 byte of imported text into the text at the cursor. */
static void
import_byte(Editor *e, uint8_t b)
{
    if (b < 0x20) {
        /* A control character is shown as ^ and the letter. */
        ed_insert_char(e, '^');
        ed_insert_char(e, u8(b + 0x40));
    } else {
        ed_insert_char(e, b);
    }
}


/*
 * Put one line of a UTF-8 file into the text.  A byte that is not well
 * formed is taken as a Latin-1 character.  Returns the number of characters
 * that CP 437 has no place for.
 */
static int
import_line_utf8(Editor *e, const uint8_t *s, size_t n)
{
    int lost = 0;
    size_t i = 0;

    while (i < n) {
        unsigned cp = s[i];
        size_t k = utf8_decode_strict(s + i, n - i, &cp);

        if (k == 0) {
            k = 1;
        }

        i += k;

        if (cp == '\t') {
            ed_insert_tab(e, ' ');
        } else if (cp < 0x20) {
            import_byte(e, u8(iun(cp)));
        } else {
            int b = cp437_of_unicode(cp);

            if (b < 0) {
                lost++;
                b = '?';
            }

            ed_insert_char(e, u8(b));
        }
    }

    return lost;
}


/*
 * Put one line of the file into the text at the cursor.  Returns the number
 * of characters that could not be kept (UTF-8 and Latin-1 only).
 */
static int
import_line(Editor *e, const uint8_t *s, size_t n, Charset cs)
{
    int lost = 0;

    if (cs == CS_UTF8) {
        return import_line_utf8(e, s, n);
    }

    for (size_t i = 0; i < n; i++) {
        uint8_t c = s[i];

        if (c == '\t') {
            ed_insert_tab(e, ' ');
            continue;
        }

        uint8_t b = cs == CS_ASCII7 ? u8(c & 0x7F) : charset_to_437(cs, c);

        if (cs == CS_LATIN1 && b == '?' && c != '?') {
            lost++;
        }

        import_byte(e, b);
    }

    return lost;
}


int
ed_import_ascii(Editor *e, const uint8_t *data, size_t len, int as_document,
                Charset cs)
{
    Pos start = e->cur;
    size_t i = 0;
    int lost = 0;
    bool have = false;
    bool prev_blank = true;
    bool stopped = false;

    /* A UTF-8 byte order mark at the start of the file is not text. */
    if (cs == CS_UTF8 && len >= 3 && data[0] == 0xEF && data[1] == 0xBB &&
        data[2] == 0xBF) {
        i = 3;
    }

    while (i < len) {
        /* A document with more blocks than that could not be loaded again. */
        if (e->doc->nitems >= DOC_MAX_ITEMS - 4) {
            stopped = true;
            break;
        }

        size_t j = i;

        while (j < len && data[j] != '\n') {
            j++;
        }

        size_t n = j - i;

        if (n > 0 && data[i + n - 1] == '\r') {
            n--;
        }

        bool blank = true;

        for (size_t k = 0; k < n; k++) {
            if (is_space(data[i + k]) == false) {
                blank = false;
            }
        }

        if (as_document != 0) {
            if (blank == true) {
                /* A blank line ends a block; single line ends are ignored. */
                if (have == true && prev_blank == false) {
                    ed_split(e, 1);
                    have = false;
                }
            } else {
                if (have == true && prev_blank == false) {
                    ed_insert_char(e, ' ');
                }

                lost += import_line(e, data + i, n, cs);
                have = true;
            }

            prev_blank = blank;
        } else {
            lost += import_line(e, data + i, n, cs);

            if (j < len) {
                ed_insert_nl(e);
            }
        }

        i = j + 1;
    }

    e->cur = start;
    ed_fix_cursor(e);

    return stopped ? -1 : lost;
}


/* ----------------------------------------------------------- export text */
/*
 * A character that draws a line, a corner, a tee or a crossing (B3 to DA of
 * code page 437) as the plain character that the ASCII screen of a terminal
 * shows for it ('-', '=', '|', '+'): a file of a set that has no such
 * characters (7 bit ASCII, Latin-1) gets these and not question marks.
 */
static int
box_plain(uint8_t b)
{
    if (b < 0xB3 || b > 0xDA) {
        return 0;
    }

    return b == 0xC4 ? '-' : b == 0xCD ? '=' : b == 0xB3 || b == 0xBA ? '|' :
                                                                       '+';
}


static void
put_line(FILE *f, const char *s, Charset cs)
{
    for (const char *p = s; *p != '\0'; p++) {
        int plain = cs == CS_ASCII7 || cs == CS_LATIN1 ? box_plain(u8(*p)) : 0;

        if (plain != 0) {
            fputc(plain, f);
        } else {
            charset_put(f, cs, u8(*p));
        }
    }

    fputs("\r\n", f);
}


/* The text of a column for the Export Data Only option. */
static void
export_column(FILE *f, const Text *t, Charset cs)
{
    bool at_end = true;

    for (int i = 0; i < t->n; i++) {
        const Tok *k = &t->v[i];

        if (k->kind == TK_CHAR) {
            charset_put(f, cs, k->a);
            at_end = false;
        } else if (k->kind == TK_CTRL && k->a == NBSP_CTRL) {
            charset_put(f, cs, NBSP_BYTE);
            at_end = false;
        } else if (k->kind == TK_TAB) {
            fputc('\t', f);
            at_end = false;
        } else if (k->kind == TK_NL) {
            fputs("\r\n", f);
            at_end = true;
        } else if (k->kind == TK_MARKER) {
            char lb[96];
            MRender mr = marker_render(k, lb, sizeof lb);

            if (mr == MR_TEXT || mr == MR_VALUE || mr == MR_KEYCAP) {
                for (const char *p = lb; *p != '\0'; p++) {
                    charset_put(f, cs, u8(*p));
                }

                at_end = false;
            }
        }
    }

    if (at_end == false) {
        fputs("\r\n", f);
    }
}


int
doc_export_ascii(Doc *d, const char *path, int screen_image, Charset cs,
                 int cur_item, char *err, size_t errlen)
{
    Doc tmp;
    Doc *src = d;
    int part = 0;

    if (cur_item >= 0) {
        if (part_make(d, SAVE_EXTRACT, cur_item, &tmp) != 0) {
            snprintf(err, errlen, "There is no current section");

            return -1;
        }

        src = &tmp;
        part = 1;
    }

    /*
     * The title page, which is in the sections above the first level, is
     * not part of the text and is not exported.
     */
    Doc body = *src;
    Item *keep = malloc(usz(src->nitems + 1) * sizeof *keep);
    int nkeep = 0;

    for (int i = 0; i < src->nitems; i++) {
        if (src->structured != 0 && is_edit_block(src, i) != 0 &&
            doc_item_level(src, i) < 1) {
            continue;
        }

        keep[nkeep++] = src->items[i];
    }

    body.items = keep;
    body.nitems = nkeep;
    src = &body;

    FILE *f = fopen(path, "wb");

    if (f == NULL) {
        snprintf(err, errlen, "cannot write %s", path);
        free(keep);

        if (part != 0) {
            free(tmp.items);
        }

        return -1;
    }

    if (screen_image != 0) {
        int n;
        char **lines = render_lines(src, 80, &n);

        for (int i = 0; i < n; i++) {
            put_line(f, lines[i], cs);
        }

        free_lines(lines, n);
    } else {
        for (int i = 0; i < src->nitems; i++) {
            if (is_edit_block(src, i) == 0) {
                continue;
            }

            for (int c = 0; c < src->items[i].blk->ncols; c++) {
                export_column(f, &src->items[i].blk->cols[c].text, cs);
            }
        }
    }

    int ok = fclose(f) == 0;

    free(keep);

    if (part != 0) {
        free(tmp.items);
    }

    if (ok == 0) {
        snprintf(err, errlen, "write error");

        return -1;
    }

    return 0;
}
