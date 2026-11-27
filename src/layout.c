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

/* layout.c - column layout engine. */
#include "util.h"
#include "layout.h"
#include "marker.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

int layout_show_marks;
int layout_show_dots;
int layout_hide_shy;

const unsigned short cp437_uni[256] = {
    0x0020, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022, 0x25D8,
    0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C, 0x25BA, 0x25C4,
    0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8, 0x2191, 0x2193, 0x2192,
    0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC, 0x0020, 0x0021, 0x0022, 0x0023,
    0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002A, 0x002B, 0x002C,
    0x002D, 0x002E, 0x002F, 0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035,
    0x0036, 0x0037, 0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E,
    0x003F, 0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F, 0x0050,
    0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059,
    0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F, 0x0060, 0x0061, 0x0062,
    0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B,
    0x006C, 0x006D, 0x006E, 0x006F, 0x0070, 0x0071, 0x0072, 0x0073, 0x0074,
    0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D,
    0x007E, 0x2302, 0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5,
    0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF,
    0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1, 0x00ED,
    0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC,
    0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x2591, 0x2592, 0x2593, 0x2502,
    0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D,
    0x255C, 0x255B, 0x2510, 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C,
    0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C,
    0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x03B1,
    0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398,
    0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229, 0x2261, 0x00B1, 0x2265,
    0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A,
    0x207F, 0x00B2, 0x25A0, 0x00A0};


/*
 * The ASCII look-alike of each cell of the CP437 screen set, by the number
 * of the character (the table cp437_uni of layout.c has the Unicode ones):
 * the box and block characters, the arrows, the triangles, the accented
 * letters (the letter without the accent) and the Greek and math signs.
 * Sixteen characters to a row; the plain ASCII rows are the characters.
 */
const char cp437_ascii[256 + 1] =
    " oO*****#o#MFdD*" /* 00 */
    "><|!PS-|^v><L-^v" /* 10 */
    " !\"#$%&'()*+,-./" /* 20 */
    "0123456789:;<=>?" /* 30 */
    "@ABCDEFGHIJKLMNO" /* 40 */
    "PQRSTUVWXYZ[\\]^_" /* 50 */
    "`abcdefghijklmno" /* 60 */
    "pqrstuvwxyz{|}~^" /* 70 */
    "CueaaaaceeeiiiAA" /* 80 */
    "EaAooouuyOUcLYPf" /* 90 */
    "aiounNao?--//!<>" /* A0 */
    "::#|++++++|+++++" /* B0 */
    "++++-++++++++=++" /* C0 */
    "+++++++++++#####" /* D0 */
    "aBGpSoutFOOd8oen" /* E0 */
    "=+><||/~o..vn2# "; /* F0 */


/* The ASCII character for the Unicode character cp of a cell. */
char
layout_ascii_of(unsigned cp)
{
    if (cp >= 0x20 && cp < 0x7F) {
        return chr(iun(cp));
    }

    for (int i = 1; i < 256; i++) {
        if (cp437_uni[i] == cp) {
            return cp437_ascii[i];
        }
    }

    return cp == 0 ? ' ' : '?';
}


/*
 * The length of the well formed UTF-8 character at s (2 to 4 bytes), or 0
 * when the bytes there are not one: a plain byte, a stray byte, a character
 * that is cut short or one that is written in more bytes than it needs.
 */
static size_t
utf8_char_length(const char *s, unsigned *cp)
{
    unsigned c = u8(s[0]);
    size_t need;
    unsigned v;
    unsigned least;

    if (c >= 0xC2 && c < 0xE0) {
        need = 1;
        v = c & 0x1Fu;
        least = 0x80;
    } else if (c >= 0xE0 && c < 0xF0) {
        need = 2;
        v = c & 0x0Fu;
        least = 0x800;
    } else if (c >= 0xF0 && c < 0xF5) {
        need = 3;
        v = c & 0x07u;
        least = 0x10000;
    } else {
        return 0;
    }

    for (size_t i = 1; i <= need; i++) {
        if ((u8(s[i]) & 0xC0) != 0x80) {
            return 0;
        }

        v = (v << 6) | (u8(s[i]) & 0x3Fu);
    }

    if (v < least || v > 0x10FFFF) {
        return 0;
    }

    *cp = v;

    return need + 1;
}


int
utf8_cell(const char *s, size_t *used)
{
    unsigned cp = 0;
    size_t n = utf8_char_length(s, &cp);

    if (n == 0) {
        *used = 1;

        return u8(s[0]);
    }

    *used = n;

    for (int k = 0x80; k < 256; k++) {
        if (cp437_uni[k] == cp) {
            return k;
        }
    }

    return '?';
}


int
swidth(const char *s)
{
    int w = 0;

    while (*s != '\0') {
        size_t n;

        utf8_cell(s, &n);
        s += n;
        w++;
    }

    return w;
}


void
name_to_cells(const char *name, char *out, size_t cap)
{
    size_t o = 0;

    while (*name != '\0' && o + 1 < cap) {
        size_t n;

        out[o++] = chr(utf8_cell(name, &n));
        name += n;
    }

    if (cap > 0) {
        out[o] = '\0';
    }
}


void
name_from_cells(const char *cells, char *out, size_t cap)
{
    size_t o = 0;

    for (; *cells != '\0'; cells++) {
        char b[4];
        int n = u8(*cells) < 0x80 ? 1 : utf8_encode(cp437_uni[u8(*cells)], b);

        if (o + usz(n) + 1 > cap) {
            break;
        }

        if (n == 1) {
            out[o++] = *cells;
        } else {
            memcpy(out + o, b, usz(n));
            o += usz(n);
        }
    }

    if (cap > 0) {
        out[o] = '\0';
    }
}


void
name_tail(const char *name, int width, char *out, size_t cap)
{
    int total = swidth(name);
    const char *at = name;

    if (width < 4) {
        width = 4;
    }

    if (total > width) {
        int drop = total - (width - 3);

        for (int i = 0; i < drop && *at != '\0'; i++) {
            size_t n;

            utf8_cell(at, &n);
            at += n;
        }

        snprintf(out, cap, "...%s", at);

        return;
    }

    str_copy(out, cap, name);
}


int
name_prev(const char *s, int pos)
{
    int p = pos - 1;
    unsigned cp;

    if (pos <= 0) {
        return 0;
    }

    for (int back = 0; back < 3 && p > 0 && (u8(s[p]) & 0xC0) == 0x80;
         back++) {
        p--;
    }

    return isz(utf8_char_length(s + p, &cp)) == pos - p ? p : pos - 1;
}


int
name_next(const char *s, int pos)
{
    unsigned cp;
    size_t n = utf8_char_length(s + pos, &cp);

    return pos + (n != 0 ? isz(n) : 1);
}


int
key_utf8(int k, char *out)
{
    char b[4];
    int n;

    if (k < 0x80) {
        out[0] = chr(k);

        return 1;
    }

    n = utf8_encode(cp437_uni[k & 0xFF], b);
    memcpy(out, b, usz(n));

    return n;
}


void
field_cells(const char *val, int pos, char *cells, size_t cap, int *cellpos)
{
    int at = 0;
    int cell = 0;

    while (at < pos && val[at] != '\0') {
        at = name_next(val, at);
        cell++;
    }

    name_to_cells(val, cells, cap);
    *cellpos = cell;
}

/* ---------------------------------------------------------- properties */


/* Named block formats are defined in the header as
 *     FN<name>\0   B? records ...   FN\0
 * and referenced from a block by a "BG<name>\0" record. */
static const char *
named_get(const Doc *d, const Block *b, const char *code, char sub)
{
    if (d == NULL || b == NULL) {
        return NULL;
    }

    const char *nm = props_get(&b->props, "BG", 0);

    if (nm == NULL) {
        return NULL;
    }

    int inside = 0;

    for (int i = 0; i < d->header.n; i++) {
        const Str *r = &d->header.v[i];

        if (r->n >= 2 && r->s[0] == 'F' && r->s[1] == 'N') {
            /* end of definition */
            if (inside != 0) {
                return NULL;
            }

            if (r->n >= 3 && strcmp(cstr(r->s) + 2, nm) == 0) {
                inside = 1;
            }
        } else if (inside != 0 && r->n >= 2 && r->s[0] == u8(code[0]) &&
                   r->s[1] == u8(code[1])) {
            if (sub == 0) {
                return cstr(r->s) + 2;
            }

            if (r->n >= 3 && r->s[2] == u8(sub)) {
                return cstr(r->s) + 3;
            }
        }
    }

    return NULL;
}

/* global formats in the header: F<level><H|T><sub><value> or
   F<H|T><sub><value> */


static const char *
hdr_fmt(const Doc *d, int level, char kind, char sub)
{
    for (int i = 0; i < d->header.n; i++) {
        const Str *r = &d->header.v[i];

        if (r->n < 3 || r->s[0] != 'F') {
            continue;
        }

        if (level > 0) {
            if (r->n >= 4 && r->s[1] == u8('0' + level) &&
                r->s[2] == u8(kind) && r->s[3] == u8(sub)) {
                return cstr(r->s) + 4;
            }
        } else if (r->s[1] == u8(kind) && r->s[2] == u8(sub)) {
            return cstr(r->s) + 3;
        }
    }

    return NULL;
}


/* look up property `sub` through column -> block -> global chain */
static const char *
chain(const Doc *d, const Block *b, const Column *c, char sub, int headline,
      int level)
{
    const char *v = NULL;

    if (c != NULL) {
        v = props_get(&c->props, "CF", sub);
    }

    if (v == NULL && b != NULL) {
        v = props_get(&b->props, "BF", sub);
    }

    if (v == NULL) {
        v = named_get(d, b, "BF", sub);
    }

    if (v == NULL && d != NULL) {
        const char kind = headline != 0 ? 'H' : 'T';

        if (level > 0) {
            v = hdr_fmt(d, level, kind, sub);
        }

        if (v == NULL) {
            v = hdr_fmt(d, 0, kind, sub);
        }
    }

    return v;
}


static void
parse_tabs(const char *s, Fmt *f)
{
    f->ntab = 0;

    if (s == NULL) {
        return;
    }

    while (*s != '\0' && f->ntab < MAXTABS) {
        if (!is_digit(*s)) {
            s++;
            continue;
        }

        int v = 0;

        while (is_digit(*s)) {
            v = v * 10 + (*s++ - '0');
        }

        char t = 'L';

        if (*s != '\0' && !is_digit(*s) && *s != ',') {
            t = *s++;
        }

        if (v > 0) {
            f->tabpos[f->ntab] = v;
            f->tabtype[f->ntab] = t;
            f->ntab++;
        }

        if (*s == ',') {
            s++;
        }
    }
}


/* ----------------------------------------------------------- section tags */
static void
fmt_counter(char style, uint32_t n, char *out, int cap)
{
    /*
     * The number is made by the function that the counter markers use, so
     * that a tag on the screen and in the print have the same text.
     */
    marker_number_text(style == 'i' ? 2 : style == 'I' ? 3 : style == 'A' ? 4 :
                       style == 'a' ? 5 : 1, n < 1 ? 1 : n, out, usz(cap));
}


/*
 * The style code of a Section Tag format: the letter between the first pair
 * of backslashes ('A' for "Appendix \A\: "), or '1' when the format has none.
 */
static char
run_style_of(const char *fmt)
{
    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p == '\\' && p[1] != '\0' && p[2] == '\\') {
            return p[1];
        }
    }

    return '1';
}


/*
 * Tag of the section whose headline is block `item`: the numbers of its
 * ancestors joined by dots ("1.1.6 "), or a custom F<level>G format such as
 * "Part \I\. ", or the format of a numbering run.  Not drawn when the header
 * has FLA0 (inferred: present exactly in the samples that show no numbers).
 *
 * A numbering run (Strata) begins at a section whose headline block has the
 * record BZG<format>: that section is number 1 of its level, the sections
 * after it on the same level go on from it with the style of the format, and
 * the sections below it show the style of the run in the numbers above them
 * ("A.1.2").  The run ends at the next section of the level with a BZG record.
 */
static void
make_tag(const Doc *d, int item, int level, char *out, int cap)
{
    out[0] = 0;
    const char *fl = props_get(&d->header, "FL", 'A');

    if (fl != NULL && atoi(fl) == 0) {
        return;
    }

    uint32_t cnt[SECT_MAXDEPTH + 1] = {0};
    uint32_t base[SECT_MAXDEPTH + 2] = {0};
    const char *run_fmt[SECT_MAXDEPTH + 2] = {NULL};
    char run_style[SECT_MAXDEPTH + 2];
    int depth = 0;
    int s = item - 1;

    memset(run_style, '1', sizeof run_style);

    for (int i = 0; i <= s && i < d->nitems; i++) {
        int t = d->items[i].type;

        if (t == IT_PLUS) {
            depth++;

            if (depth <= SECT_MAXDEPTH) {
                cnt[depth] = 0;
                base[depth] = 0;
                run_fmt[depth] = NULL;
                run_style[depth] = '1';
            }
        } else if (t == IT_MINUS) {
            depth--;
        } else if (t == IT_SECT && depth >= 0 && depth <= SECT_MAXDEPTH) {
            cnt[depth]++;

            if (i + 1 < d->nitems && d->items[i + 1].type == IT_BLOCK &&
                d->items[i + 1].blk != NULL) {
                const char *rf = props_get(&d->items[i + 1].blk->props,
                                           "BZ", 'G');

                if (rf != NULL) {
                    base[depth] = cnt[depth] - 1;
                    run_fmt[depth] = rf;
                    run_style[depth] = run_style_of(rf);
                }
            }
        }
    }

    if (depth < 1 || depth > SECT_MAXDEPTH) {
        return;
    }

    const char *fmt = run_fmt[depth];

    for (int i = 0; fmt == NULL && i < d->header.n; i++) {
        const Str *r = &d->header.v[i];

        if (r->n >= 3 && r->s[0] == 'F' && r->s[1] == u8('0' + level) &&
            r->s[2] == 'G') {
            fmt = cstr(r->s) + 3;
        }
    }

    if (fmt == NULL) {
        /* Auto Numbering to Level: deeper sections have no tag. */
        const char *ln = props_get(&d->header, "FL", 'N');

        if (ln != NULL && depth > atoi(ln)) {
            return;
        }

        int o = 0;

        for (int l = 1; l <= depth && o < cap - 24; l++) {
            char c[24];

            fmt_counter(run_style[l], cnt[l] - base[l], c, sizeof c);
            o += snprintf(out + o, usz(cap - o), l == 1 ? "%s" : ".%s", c);
        }

        if (o < cap - 1) {
            out[o++] = ' ';
            out[o] = 0;
        }

        return;
    }

    int o = 0;

    for (const char *p = fmt; *p != '\0' && o < cap - 12; p++) {
        if (*p == '\\' && p[1] != '\0' && p[2] == '\\') {
            char c[24];

            fmt_counter(p[1], cnt[depth] - base[depth], c, sizeof c);
            str_copy(out + o, usz(cap - o), c);
            o += slen(out + o);
            p += 2;
        } else if (*p == '\x01' && p[1] != '\0') {
            /*
             * The character after a 0x01 byte is shown as it is,
             * and the byte itself not at all: 0x01 0x07 is a bullet.
             */
            p++;
            out[o++] = *p;
        } else {
            out[o++] = *p;
        }
    }

    out[o] = 0;
}


void
fmt_resolve(const Doc *d, int item, int col, Fmt *f)
{
    memset(f, 0, sizeof *f);
    f->defstep = 5;
    const Block *b = NULL;
    const Column *c = NULL;
    int headline = 0;
    int level = 0;

    if (d != NULL && item >= 0 && item < d->nitems &&
        d->items[item].type == IT_BLOCK) {
        b = d->items[item].blk;

        if (col >= 0 && col < b->ncols) {
            c = &b->cols[col];
        }

        if (d->structured != 0) {
            headline = (item > 0 && d->items[item - 1].type == IT_SECT);
            level = doc_item_level(d, item);
        }
    }

    f->headline = headline;
    f->level = level;

    if (headline != 0 && level > 0 && col == 0) {
        make_tag(d, item, level, f->tag, sizeof f->tag);
    }

    const char *v;

    if ((v = chain(d, b, c, 'A', headline, level)) != NULL) {
        f->attr = atoi(v);
    }

    if ((v = chain(d, b, c, 'J', headline, level)) != NULL) {
        f->just = atoi(v);
    }

    if ((v = chain(d, b, c, 'I', headline, level)) != NULL) {
        f->indent = atoi(v);
    }

    parse_tabs(chain(d, b, c, 'T', headline, level), f);

    if (b != NULL && ((v = props_get(&b->props, "BL", 0)) != NULL ||
                      (v = named_get(d, b, "BL", 0)) != NULL)) {
        f->bl = atoi(v);
    }

    if (b != NULL && ((v = props_get(&b->props, "BR", 0)) != NULL ||
                      (v = named_get(d, b, "BR", 0)) != NULL)) {
        f->br = atoi(v) > 0 ? atoi(v) : 0;
    }
}


/*
 * A record of a block: the block's own, else the one of its named format.
 * The result is the text after the code, or NULL.
 */
const char *
fmt_block_prop(const Doc *d, const Block *b, const char *code, char sub)
{
    const char *v;

    if (b == NULL) {
        return NULL;
    }

    v = props_get(&b->props, code, sub);

    return v != NULL ? v : named_get(d, b, code, sub);
}


/* -------------------------------------------------------------- layout */
typedef struct {
    Cell *c;
    int n;
    int cap;
} CellBuf;


static void
cb_push(CellBuf *b, Cell c)
{
    if (b->n == b->cap) {
        b->cap = b->cap != 0 ? b->cap * 2 : 128;
        b->c = realloc(b->c, usz(b->cap) * sizeof(Cell));
    }

    b->c[b->n++] = c;
}


static int
seg_width(const Text *t, int i, int dec)
{
    /* width of text following a tab until next TAB / NL; if dec, only up to
       a decimal point */
    int w = 0;

    for (; i < t->n; i++) {
        const Tok *k = &t->v[i];

        if (k->kind == TK_TAB || k->kind == TK_NL) {
            break;
        }

        if (k->kind == TK_CHAR) {
            if (dec != 0 && (k->a == '.' || k->a == ',')) {
                break;
            }

            w++;
        } else if (k->kind == TK_MARKER) {
            char b[96];

            switch (marker_render(k, b, sizeof b)) {
                case MR_GLYPH:
                    w += 1;
                    break;

                case MR_TEXT:
                case MR_VALUE:
                case MR_KEYCAP:
                    w += slen(b);
                    break;

                case MR_BRACKET:
                case MR_ERROR:
                case MR_MISSING:
                default:
                    w += slen(b) + 2;
                    break;
            }
        }
    }

    return w;
}


/*
 * An equation, a picture or a keycap: all three are drawn as images that
 * are made in the background.
 */
static int
is_image_marker(const Tok *k)
{
    return tok_mtype(k) == 9 || tok_mtype(k) == 25 || tok_mtype(k) == 37 ||
           tok_mtype(k) == MT_LATEX;
}


typedef struct {
    int x; /* cell index in the row */
    int wc; /* width in cells */
    int above; /* rows above the text row that it needs */
    int below;
    int tok;
} FlowPic;

typedef struct {
    const Fmt *f;
    const Text *text;
    ColLayout *out;
    int cap;
    CellBuf row;
    int rowi; /* rows emitted so far in this logical line */
    int x; /* current x relative to the left margin (includes indent) */
    int row_tok0;
    int last_break; /* cell index after a space */
    int tindent; /* text indent (0x1A): continuation lines align here */
    int tw;
    int ind_first;
    int ind_rest;
    /* pictures of inline equations in the row being built */
    int npic;
    FlowPic pic[8];
} Flow;


int (*layout_eq_size)(const Text *t, int tok, int display, int *wcells,
                      int *above, int *below);
const uint8_t *(*layout_eq_image)(const Text *t, int tok, int display, int *w,
                                  int *h, int *xoff, int *yoff);
void (*layout_eq_want)(const Text *t, int tok, int display);


/* The rows of a picture in a line, above which a LaTeX picture is a block. */
#define LATEX_BLOCK_ROWS 4


int
eqn_is_display(const Text *t, int tok)
{
    int a = tok;
    int b = tok + 1;

    /*
     * A LaTeX marker is a word in a line, unless its picture is a block: a
     * picture that needs more than LATEX_BLOCK_ROWS rows in a line (it is
     * taller than the two lines that the inline pictures may be) is shown
     * on rows of its own, like a displayed equation.  The picture is not
     * known until it is made; until then the marker is a word.
     */
    if (tok_mtype(&t->v[tok]) == MT_LATEX) {
        int wc;
        int ab;
        int bl;

        return layout_eq_size != NULL &&
               layout_eq_size(t, tok, 0, &wc, &ab, &bl) != 0 &&
               ab + bl + 1 > LATEX_BLOCK_ROWS;
    }

    /* A keycap is always a word in a line, and so is a picture In Text. */
    if (tok_mtype(&t->v[tok]) == 37 ||
        marker_picture_in_text(&t->v[tok]) != 0) {
        return 0;
    }

    while (a > 0 && t->v[a - 1].kind != TK_NL) {
        a--;
    }

    while (b < t->n && t->v[b].kind != TK_NL) {
        b++;
    }

    for (int i = a; i < b; i++) {
        const Tok *k = &t->v[i];

        if (i == tok) {
            continue;
        }

        if (k->kind == TK_ATTR || k->kind == TK_TAB) {
            continue;
        }

        if (k->kind == TK_CHAR && k->a == ' ') {
            continue;
        }

        if (k->kind == TK_CTRL && (k->a == NBSP_CTRL || k->a == SHY_CTRL)) {
            continue;
        }

        return 0;
    }

    return 1;
}


/* Keep the cells of the row being built in the form that takes least room. */
static void
row_pack(LRow *r, const CellBuf *b, int tok0, const Text *text)
{
    int n = b->n;
    int fmt = LF_DENSE;
    int plain = n > 0 && tok0 >= 0 && tok0 + n <= text->n;

    r->rattr = 0;

    for (int k = 0; plain != 0 && k < n; k++) {
        const Tok *tk = &text->v[tok0 + k];

        plain = b->c[k].tok == tok0 + k && tk->kind == TK_CHAR &&
                b->c[k].ch == tk->a && b->c[k].attr == b->c[0].attr;
    }

    if (plain != 0) {
        r->n = n;
        r->fmt = LF_TEXT;
        r->rattr = b->c[0].attr;
        r->cells = NULL;

        return;
    }

    for (int k = 0; k < n; k++) {
        int t = b->c[k].tok;

        if (t == tok0 + k) {
            continue;
        }

        if (t == -1 || (t >= tok0 && t - tok0 < 0xFFFF)) {
            if (fmt == LF_DENSE) {
                fmt = LF_NEAR;
            }
        } else {
            fmt = LF_WIDE;
            break;
        }
    }

    size_t es = fmt == LF_DENSE ? 0 : fmt == LF_NEAR ? 2 : 4;

    r->n = n;
    r->fmt = u8(fmt);
    r->cells = NULL;

    if (n > 0) {
        r->cells = malloc(usz(n) * (2 + es));
    }

    for (int k = 0; k < n; k++) {
        uint8_t *p = r->cells + 2 * k;
        int t = b->c[k].tok;

        p[0] = b->c[k].ch;
        p[1] = b->c[k].attr;

        if (fmt == LF_NEAR) {
            uint16_t v = 0xFFFF;

            if (t != -1) {
                v = u16(uns(t - tok0));
            }


            memcpy(r->cells + 2 * usz(n) + 2 * usz(k), &v, sizeof v);
        } else if (fmt == LF_WIDE) {
            memcpy(r->cells + 2 * usz(n) + 4 * usz(k), &t, sizeof t);
        }
    }
}


int
lrow_tok(const LRow *r, int k)
{
    if (r->fmt == LF_DENSE || r->fmt == LF_TEXT) {
        return r->tok0 + k;
    }

    if (r->fmt == LF_NEAR) {
        uint16_t v;

        memcpy(&v, r->cells + 2 * usz(r->n) + 2 * usz(k), sizeof v);

        return v == 0xFFFF ? -1 : r->tok0 + v;
    }

    int t;

    memcpy(&t, r->cells + 2 * usz(r->n) + 4 * usz(k), sizeof t);

    return t;
}


Cell
lrow_cell(const LRow *r, int k, const Text *t)
{
    Cell c;

    if (r->fmt == LF_TEXT) {
        c.tok = r->tok0 + k;
        c.ch = c.tok < t->n ? t->v[c.tok].a : ' ';
        c.attr = r->rattr;

        return c;
    }

    c.ch = r->cells[2 * k];
    c.attr = r->cells[2 * k + 1];
    c.tok = lrow_tok(r, k);

    return c;
}


/* Close the row being built: it ends just before token tok1. */
static void
emit_plain(Flow *s, int tok1)
{
    if (s->out->nrows == s->cap) {
        s->cap *= 2;
        s->out->rows = realloc(s->out->rows, usz(s->cap) * sizeof(LRow));
    }

    LRow *r = &s->out->rows[s->out->nrows++];

    row_pack(r, &s->row, s->row_tok0, s->text);

    r->tok0 = s->row_tok0;
    r->tok1 = tok1;
    r->pic_tok = -1;

    int ir = s->tindent > s->ind_rest ? s->tindent : s->ind_rest;
    int ind = (s->rowi == 0) ? s->ind_first : ir;
    int len = s->row.n;
    int tl = len;

    while (tl > 0 && s->row.c[tl - 1].ch == ' ') {
        tl--;
    }

    r->x0 = s->f->bl + ind;

    if (len == 0) {
        r->x0 = s->f->bl;
    }

    int avail = s->tw - ind;

    if (s->f->just == 1) {
        /* Right aligned text stops one cell short: the last cell of a row
           is kept for the end-of-line mark. */
        r->x0 += (avail - 1 - tl > 0 ? avail - 1 - tl : 0);
    } else if (s->f->just == 2) {
        r->x0 += (avail - tl > 0 ? (avail - tl) / 2 : 0);
    }

    s->rowi++;
    s->row.n = 0;
    s->last_break = -1;
    s->row_tok0 = tok1;
    s->x = ir;
}


/* The marker of the first picture that reaches the given line of a group. */
static int
picture_tok(const Flow *s, int line, int above)
{
    for (int p = 0; p < s->npic; p++) {
        if (line >= above - s->pic[p].above &&
            line <= above + s->pic[p].below) {
            return s->pic[p].tok;
        }
    }

    return -1;
}


/* A row of cells like the text row n cells wide, with the pictures that
   reach row j of the rows above (j >= 0) or below (j < 0 counts from 1). */
static void
fill_picture_row(Flow *s, int n, int line, int above)
{
    s->row.n = 0;

    for (int k = 0; k < n; k++) {
        Cell c = {' ', 0, -1};

        for (int p = 0; p < s->npic; p++) {
            int first = above - s->pic[p].above;
            int last = above + s->pic[p].below;

            if (line >= first && line <= last && k >= s->pic[p].x &&
                k < s->pic[p].x + s->pic[p].wc) {
                c.ch = '.';
                c.attr = DA_EQ;
                c.tok = s->pic[p].tok;
            }
        }

        cb_push(&s->row, c);
    }
}


/*
 * Close the row being built.  A row with the picture of an inline equation
 * is taller than a text row: rows of free cells come before and after the
 * text row, as many as the tallest picture needs, and the text row is the
 * one that holds the text.
 */
static void
emit_row(Flow *s, int tok1)
{
    if (s->npic == 0) {
        emit_plain(s, tok1);

        return;
    }

    int above = 0;
    int below = 0;
    int n = s->row.n;
    int tok0 = s->row_tok0;
    int rowi = s->rowi;
    int first = s->out->nrows;
    Cell *copy = malloc(usz(n > 0 ? n : 1) * sizeof(Cell));

    for (int p = 0; p < s->npic; p++) {
        above = s->pic[p].above > above ? s->pic[p].above : above;
        below = s->pic[p].below > below ? s->pic[p].below : below;
    }

    memcpy(copy, s->row.c, usz(n) * sizeof(Cell));

    for (int j = 0; j < above; j++) {
        fill_picture_row(s, n, j, above);
        s->row_tok0 = tok0;
        emit_plain(s, tok0);
        s->out->rows[s->out->nrows - 1].pic_tok = picture_tok(s, j, above);
        s->rowi = rowi;
    }

    s->row.n = 0;

    for (int k = 0; k < n; k++) {
        cb_push(&s->row, copy[k]);
    }

    free(copy);
    emit_plain(s, tok1);

    int text_row = s->out->nrows - 1;
    int after = s->rowi;
    int ind = s->out->rows[text_row].x0;

    for (int j = 0; j < below; j++) {
        fill_picture_row(s, n, above + 1 + j, above);
        emit_plain(s, tok1);
        s->out->rows[s->out->nrows - 1].pic_tok =
            picture_tok(s, above + 1 + j, above);
        s->rowi = after;
    }

    for (int r = first; r < s->out->nrows; r++) {
        s->out->rows[r].x0 = ind;
    }

    s->npic = 0;
    s->row.n = 0;
    s->rowi = after;
}


/*
 * Put one cell on the row of a flow.  A cell that is not a space and does
 * not fit (the last cell of a row is kept for the space or the end mark that
 * follows the last word) makes the row end at the last space, and the cells
 * after it go to the next row.  The characters of the text and the
 * characters of the label of a marker are put the same way, so that a long
 * marker label wraps like text.  The tok is the token that made the cell.
 */
static void
flow_put(Flow *fl, Cell c, int is_sp, int tok)
{
    if (is_sp == 0 && fl->x >= fl->tw - 1) {
        if (fl->last_break > 0) {
            /* move cells after last_break to the next row */
            int mv = fl->row.n - fl->last_break;
            Cell *tmp =
                malloc(usz((mv > 1 ? mv : 1)) * sizeof(Cell));

            memcpy(tmp, fl->row.c + fl->last_break,
                   usz(mv) * sizeof(Cell));
            fl->row.n = fl->last_break;
            /*
             * When the row already ends at the break, nothing
             * moves and the new row starts with this token.
             */
            int first_tok = mv != 0 ? tmp[0].tok : tok;

            /*
             * A token that starts on this row and goes on in the next (the
             * label of a marker) belongs to this row, so that the cursor
             * on that token is shown where the token starts.
             */
            if (mv != 0 && fl->row.n > 0 &&
                fl->row.c[fl->row.n - 1].tok == first_tok) {
                first_tok++;
            }
            FlowPic moved[8];
            int nmoved = 0;
            int nkeep = 0;

            /* Pictures in the moved cells go to the new row. */
            for (int p = 0; p < fl->npic; p++) {
                if (fl->pic[p].x >= fl->last_break) {
                    moved[nmoved] = fl->pic[p];
                    moved[nmoved].x -= fl->last_break;
                    nmoved++;
                } else {
                    fl->pic[nkeep++] = fl->pic[p];
                }
            }

            fl->npic = nkeep;
            emit_row(fl, first_tok);

            for (int j = 0; j < mv; j++) {
                cb_push(&fl->row, tmp[j]);
            }

            memcpy(fl->pic, moved, usz(nmoved) * sizeof(FlowPic));
            fl->npic = nmoved;

            free(tmp);
            fl->x = (fl->tindent > fl->ind_rest ? fl->tindent :
                                               fl->ind_rest) +
                   mv;
        } else {
            emit_row(fl, tok);
            fl->x =
                fl->tindent > fl->ind_rest ? fl->tindent : fl->ind_rest;
        }
    }

    cb_push(&fl->row, c);
    fl->x++;

    if (is_sp != 0) {
        fl->last_break = fl->row.n;
    }
}


void
layout_column(const Fmt *f, Text *t, int width, ColLayout *out, int fix)
{
    int stop1 = f->ntab != 0 ? f->tabpos[0] : 5;
    int ind_first = (f->indent == 1 || f->indent == 3) ? stop1 : 0;
    int ind_rest = (f->indent == 2 || f->indent == 3) ? stop1 : 0;
    Flow fl = {0};

    fl.f = f;
    fl.text = t;
    fl.out = out;
    fl.tw = width - f->bl;

    if (fl.tw < 8) {
        fl.tw = 8;
    }

    fl.cap = 8;
    out->rows = malloc(usz(fl.cap) * sizeof(LRow));
    out->nrows = 0;
    out->width = width;
    fl.ind_first = ind_first;
    fl.ind_rest = ind_rest;
    fl.last_break = -1; /* cell index after a space */
    int cur_attr = 0;
    int cur_flags = 0;

    fl.x = fl.ind_first;

    /* section tag: part of the first row, not of the text */
    if (f->tag[0] != '\0') {
        int tda = 0;

        if ((f->attr & 1) != 0) {
            tda |= DA_BOLD;
        }

        if ((f->attr & 4) != 0) {
            tda |= DA_ITALIC;
        }

        for (const char *p = f->tag; *p != '\0'; p++) {
            Cell c = {u8(*p), u8(tda), -1};

            cb_push(&fl.row, c);
            fl.x++;

            if (*p == ' ') {
                fl.last_break = fl.row.n;
            }
        }
    }

    for (int i = 0; i < t->n; i++) {
        Tok *k = &t->v[i];
        int base = f->attr;
        int da = 0;

        {
            int m = base | cur_attr;

            if ((m & 1) != 0) {
                da |= DA_BOLD;
            }

            if ((m & 4) != 0) {
                da |= DA_ITALIC;
            }

            /* Underscore Words, Underscore All and the double ones; 10 is
               superscript and 08 subscript, which a cell cannot show. */
            if ((m & ATTR_UNDERW) != 0 ||
                (cur_flags & (FLAG_UNDER_ALL | FLAG_DUNDER_W |
                              FLAG_DUNDER_ALL)) != 0) {
                da |= DA_UNDER;
            }
        }

        switch (k->kind) {
            case TK_ATTR:
                cur_attr = k->a;
                cur_flags = k->b;
                break;

            case TK_CTRL:
                if (k->a == NBSP_CTRL) { /* A no-break space. */
                    Cell c = {NBSP_SHOWN, u8(da), i};

                    cb_push(&fl.row, c);
                    fl.x++;
                } else if (k->a == SHY_CTRL) { /* A soft hyphen. */
                    if (layout_hide_shy == 0) {
                        Cell c = {SHY_SHOWN, u8(da | DA_DIM), i};

                        cb_push(&fl.row, c);
                        fl.x++;
                    }
                } else if (k->a == 0x1a) { /* text indent marker */
                    Cell c = {0x1A, u8(da | DA_DIM), i};

                    cb_push(&fl.row, c);
                    fl.x++;
                    fl.tindent = fl.x;
                }
                break;

            case TK_NL: {
                if (layout_show_marks != 0) {
                    Cell c = {0x11, DA_DIM | DA_BOLD, i};

                    cb_push(&fl.row, c);
                }

                emit_row(&fl, i + 1);
                fl.rowi = 0;
                fl.tindent = 0;
                fl.x = fl.ind_first;
                break;
            }

            case TK_TAB: {
                int stop = -1;
                char ty = 'L';

                for (int s = 0; s < f->ntab; s++) {
                    if (f->tabpos[s] > fl.x) {
                        stop = f->tabpos[s];
                        ty = f->tabtype[s];
                        break;
                    }
                }

                if (stop < 0 && f->ntab == 0) {
                    stop = ((fl.x / f->defstep) + 1) * f->defstep;
                }

                int total = (stop < 0) ? 1 : stop - fl.x;

                if (total < 1) {
                    total = 1;
                }

                if (ty == 'R' || ty == 'C' || ty == 'D' || ty == ',' ||
                    ty == 'N') {
                    int sw = seg_width(t, i + 1, ty == 'D' || ty == ',');

                    if (ty == 'R' || ty == 'N') {
                        total -= sw;
                    } else if (ty == 'C') {
                        total -= sw / 2;
                    } else {
                        total -= sw;
                    }

                    if (total < 1) {
                        total = 1;
                    }
                }

                if (fix != 0) {
                    k->b = u8(total - 1 > 255 ? 255 : total - 1);
                }

                for (int j = 0; j < total; j++) {
                    uint8_t fc = k->a != '\0' ? k->a : ' ';

                    if (j == 0 && layout_show_marks != 0 && fc == ' ') {
                        fc = 0x10;
                    }

                    Cell c = {fc, u8(j == 0 && fc == 0x10 ? (da | DA_DIM) : da),
                              i};

                    cb_push(&fl.row, c);
                }

                fl.x += total;
                break;
            }

            case TK_MARKER: {
                char lb[96];
                int wc;
                int ab;
                int bl;

                if (is_image_marker(k) != 0 && layout_eq_size != NULL &&
                    eqn_is_display(t, i) == 0 &&
                    layout_eq_size(t, i, 0, &wc, &ab, &bl) != 0 &&
                    fl.npic < 8) {
                    /* An inline equation: cells in the line for its picture
                       and rows above and below when it is taller.  A
                       picture that does not fit in what is left of the line
                       starts the next line. */
                    if (fl.row.n > 0 && fl.x + wc > fl.tw) {
                        emit_row(&fl, i);
                        fl.x = fl.tindent > fl.ind_rest ? fl.tindent :
                                                          fl.ind_rest;
                    }

                    int at = fl.npic++;

                    fl.pic[at].x = fl.row.n;
                    fl.pic[at].wc = wc;
                    fl.pic[at].above = ab;
                    fl.pic[at].below = bl;
                    fl.pic[at].tok = i;

                    for (int c = 0; c < wc; c++) {
                        Cell ce = {'.', DA_EQ, i};

                        cb_push(&fl.row, ce);
                    }

                    fl.x += wc;
                    break;
                }

                int hr;

                if (is_image_marker(k) != 0 && layout_eq_size != NULL &&
                    eqn_is_display(t, i) != 0 &&
                    layout_eq_size(t, i, 1, &wc, &ab, &bl) != 0) {
                    hr = ab + bl + 1;
                    /* The picture of the equation: rows of free cells. */
                    Fmt centred = *f;
                    int only_blank = 1;

                    for (int c = 0; c < fl.row.n; c++) {
                        if (fl.row.c[c].ch != ' ') {
                            only_blank = 0;
                        }
                    }

                    if (only_blank != 0) {
                        fl.row.n = 0;
                    } else {
                        emit_row(&fl, i);
                    }

                    centred.just = 2;
                    fl.f = &centred;

                    for (int r = 0; r < hr; r++) {
                        for (int c = 0; c < wc; c++) {
                            Cell ce = {'.', DA_EQ, i};

                            cb_push(&fl.row, ce);
                        }

                        emit_row(&fl, i + 1);
                        fl.out->rows[fl.out->nrows - 1].pic_tok = i;
                    }

                    fl.f = f;
                    fl.rowi = 0;
                    fl.x = fl.ind_first;
                    break;
                }

                MRender mr = marker_render(k, lb, sizeof lb);

                if (mr == MR_GLYPH) {
                    Cell cg = {0x04, DA_MARKER, i};

                    cb_push(&fl.row, cg);
                    fl.x++;
                    break;
                }

                if (mr == MR_KEYCAP) {
                    for (char *p = lb; *p != '\0'; p++) {
                        Cell ck = {u8(*p), DA_KEY, i};

                        flow_put(&fl, ck, *p == ' ', i);
                    }

                    break;
                }

                if (mr == MR_VALUE) {
                    for (char *p = lb; *p != '\0'; p++) {
                        Cell cv = {u8(*p), DA_MARKER, i};

                        flow_put(&fl, cv, *p == ' ', i);
                    }

                    break;
                }

                if (mr == MR_TEXT) {
                    for (char *p = lb; *p != '\0'; p++) {
                        Cell cs = {u8(*p), 0, i};

                        flow_put(&fl, cs, *p == ' ', i);
                    }

                    break;
                }

                if (mr == MR_MISSING) {
                    Cell cm0 = {'[', DA_MARKER | DA_DIM, i};

                    flow_put(&fl, cm0, 0, i);

                    for (char *p = lb; *p != '\0'; p++) {
                        Cell cm1 = {u8(*p), DA_MARKER | DA_DIM, i};

                        flow_put(&fl, cm1, *p == ' ', i);
                    }

                    Cell cm2 = {']', DA_MARKER | DA_DIM, i};

                    flow_put(&fl, cm2, 0, i);
                    break;
                }

                if (mr == MR_ERROR) {
                    Cell ce0 = {'[', DA_MARKER | DA_BOLD | DA_UNDER, i};

                    flow_put(&fl, ce0, 0, i);

                    for (char *p = lb; *p != '\0'; p++) {
                        Cell ce1 = {u8(*p), DA_MARKER | DA_BOLD | DA_UNDER, i};

                        flow_put(&fl, ce1, *p == ' ', i);
                    }

                    Cell ce2 = {']', DA_MARKER | DA_BOLD | DA_UNDER, i};

                    flow_put(&fl, ce2, 0, i);
                    break;
                }

                Cell c = {'[', DA_MARKER, i};

                flow_put(&fl, c, 0, i);

                for (char *p = lb; *p != '\0'; p++) {
                    Cell cc = {u8(*p), DA_MARKER, i};

                    flow_put(&fl, cc, *p == ' ', i);
                }

                Cell ce = {']', DA_MARKER, i};

                flow_put(&fl, ce, 0, i);
                break;
            }

            case TK_CHAR: {
                uint8_t ch = k->a;
                int is_sp = (ch == ' ');

                /*
                 * The last cell of a row is kept for the space or the
                 * end-of-line mark that follows the last word, so a word
                 * is never placed in it.
                 */
                Cell c = {
                    is_sp != 0 && layout_show_dots != 0 ? 0xFA : ch, u8(da),
                    i};

                flow_put(&fl, c, is_sp, i);

                break;
            }

            default:
                break;
        }
    }

    if (fl.row.n != 0 || out->nrows == 0 ||
        (t->n != 0 && t->v[t->n - 1].kind != TK_NL)) {
        emit_row(&fl, t->n);
    }

    free(fl.row.c);

    /* The rows were made by doubling: give back what is not used. */
    if (out->nrows < fl.cap) {
        out->rows = realloc(out->rows, usz(out->nrows) * sizeof(LRow));
    }
}


void
layout_free(ColLayout *l)
{
    for (int i = 0; i < l->nrows; i++) {
        free(l->rows[i].cells);
    }

    free(l->rows);
    l->rows = NULL;
    l->nrows = 0;
}


int
layout_locate(const ColLayout *l, int tok, int *x)
{
    for (int r = 0; r < l->nrows; r++) {
        const LRow *w = &l->rows[r];
        int last = (r == l->nrows - 1);

        if ((tok >= w->tok0 && tok < w->tok1) ||
            (last != 0 && tok >= w->tok0)) {
            int xx = w->x0 + w->n; /* default: end of row */

            for (int i = 0; i < w->n; i++) {
                if (lrow_tok(w, i) >= tok) {
                    xx = w->x0 + i;
                    break;
                }
            }

            *x = xx;
            return r;
        }
    }

    *x = 0;
    return 0;
}


int
layout_tok_at(const ColLayout *l, int row, int x)
{
    if (row < 0) {
        row = 0;
    }

    if (row >= l->nrows) {
        row = l->nrows - 1;
    }

    const LRow *w = &l->rows[row];
    int i = x - w->x0;

    /* A row of a displayed equation is the equation, wherever x is. */
    if (w->pic_tok >= 0 && w->n > 0 && lrow_tok(w, 0) == w->pic_tok) {
        return w->pic_tok;
    }

    if (i < 0) {
        return w->tok0;
    }

    if (i >= w->n) {
        /* past the end: the last token of the row (the NL / the wrapped-off
           space) */
        int t = w->tok1 - 1;

        if (t < w->tok0) {
            t = w->tok0;
        }

        int lt = w->n != 0 ? lrow_tok(w, w->n - 1) : -1;

        return (w->n != 0 && lt >= t) ? t : (w->n != 0 ? lt + 1 : w->tok0);
    }

    return lrow_tok(w, i);
}


/* recompute stored tab widths for every column of a block (used on save) */
void
block_fix_tabs(Block *b)
{
    Doc tmp;

    memset(&tmp, 0, sizeof tmp);

    for (int c = 0; c < b->ncols; c++) {
        Fmt f;

        memset(&f, 0, sizeof f);
        f.defstep = 5;
        const char *v;

        if ((v = props_get(&b->cols[c].props, "CF", 'T')) != NULL ||
            (v = props_get(&b->props, "BF", 'T')) != NULL) {
            parse_tabs(v, &f);
        }

        if ((v = props_get(&b->cols[c].props, "CF", 'I')) != NULL ||
            (v = props_get(&b->props, "BF", 'I')) != NULL) {
            f.indent = atoi(v);
        }

        if ((v = props_get(&b->props, "BL", 0)) != NULL) {
            f.bl = atoi(v);
        }

        ColLayout l;

        layout_column(&f, &b->cols[c].text,
                      b->cols[c].width != 0 ? b->cols[c].width : 80, &l, 1);
        layout_free(&l);
    }
}
