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
 * The DVI interpreter: postamble, page offsets, and conversion of a page
 * into a list of glyphs and rules.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dviview.h"

#define MAX_DEPTH 8
#define MAX_KINDS 64

typedef struct {
    long h;
    long v;
    long w;
    long x;
    long y;
    long z;
} Regs;

static char *seen_kinds[MAX_KINDS];
static int nseen;

/* Where the special that is being run is: its page, position and text. */
static long sp_page;
static double sp_x;
static double sp_y;
static char sp_text[80];
static const char *sp_file = "";


/*
 * Remembers the place and the text of the special that is about to run, for
 * the message about it.  The text is cut to a sensible length and what is
 * not printable is shown as an escape, so that the line can be read.
 */
void
dvi_special_context(const Ctx *c, const unsigned char *p, long len)
{
    size_t n = 0;
    long i;

    sp_page = c->page;
    sp_x = c->g.px / 72.0;
    sp_y = c->g.py / 72.0;
    sp_file = c->d->path != NULL ? c->d->path : "";

    for (i = 0; i < len && n + 4 < sizeof sp_text - 4; i++) {
        unsigned char ch = p[i];

        if (ch >= 32 && ch < 127 && ch != '\\') {
            sp_text[n++] = to_char(ch);
        } else {
            n += to_size(snprintf(sp_text + n, sizeof sp_text - n, "\\x%02X",
                                  ch));
        }
    }

    if (i < len) {
        memcpy(sp_text + n, "...", 3);
        n += 3;
    }

    sp_text[n] = '\0';
}


/*
 * Tells the Editor, once for each kind, that a special is not supported.  A
 * person can act on the message: it shows what the special is, where it is
 * in the document, why nothing is drawn for it and what to do.
 */
void
dvi_report_special(const char *kind)
{
    int i;

    for (i = 0; i < nseen; i++) {
        if (strcmp(seen_kinds[i], kind) == 0) {
            return;
        }
    }

    if (nseen < MAX_KINDS) {
        seen_kinds[nseen++] = xstrdup(kind);
    }

    viewer_tell("note",
                "special '%s' is not supported and is ignored: first on page "
                "%ld, %.2f in from the left and %.2f in from the top, text "
                "\"%s\", file %s.  The preview may differ from the printed "
                "page at this place; the PDF is not affected.  Remove the "
                "construct of the document that made it (an equation, a "
                "picture or a marker) or check the PDF instead",
                kind, sp_page, sp_x, sp_y, sp_text, sp_file);
}


/* Adds an item with the colour, matrix and clip of the graphics state. */
void
dvi_add_item(Ctx *c, const Item *it)
{
    ItemList *l = c->out;
    GState *s = gfx_cur(&c->g);

    if (l->n >= l->cap) {
        l->cap = l->cap * 2 + 1024;
        l->v = xrealloc(l->v, to_size(l->cap) * sizeof *l->v);
    }

    l->v[l->n] = *it;
    l->v[l->n].rgb = s->fill;
    l->v[l->n].clip = it->type == IT_IMAGE || it->type == IT_PATH ? it->clip :
                      s->clip;
    l->v[l->n].xf = it->type == IT_GLYPH ? gfx_xf(&c->g) : 0;
    l->n++;
}


/* A rule under a matrix other than the identity becomes a filled path. */
static void
add_rule(Ctx *c, const Regs *r, long wd, long ht)
{
    Item it;
    double s = c->d->unit * 72.0;
    GState *st = gfx_cur(&c->g);

    if (mat_is_ident(&st->ctm)) {
        memset(&it, 0, sizeof it);
        it.type = IT_RULE;
        it.x = r->h;
        it.y = r->v;
        it.w = wd;
        it.h = ht;
        dvi_add_item(c, &it);

        return;
    }

    gfx_save(&c->g);
    path_clear(&gfx_cur(&c->g)->path);
    gfx_rect(&c->g, 72.0 + dbl(r->h) * s, 72.0 + dbl(r->v) * s - dbl(ht) * s,
             dbl(wd) * s, dbl(ht) * s);
    gfx_paint(&c->g, 1, 0, 0);
    gfx_restore(&c->g, 1);
}


static long
scaled(long v, long scale)
{
    if (scale == 0) {
        return v;
    }

    return (v * scale) >> 20;
}


static void run(Ctx *c, const unsigned char *p, const unsigned char *end,
                Regs *r, long scale, const FTab *ft, int depth);


static void
run_packet(Ctx *c, const VfChar *vc, Regs *r, const DFont *cur, int depth)
{
    run(c, vc->code, vc->code + vc->len, r, cur->size, &cur->vf->ft,
        depth + 1);
}


/* Puts the character ch of font cur on the page (or runs its VF packet). */
static void
show_char(Ctx *c, DFont *cur, long ch, const Regs *r, int depth)
{
    Item it;

    if (cur->vf != NULL) {
        long i;

        if (depth >= MAX_DEPTH) {
            return;
        }

        for (i = 0; i < cur->vf->nchars; i++) {
            const VfChar *vc = &cur->vf->chars[i];

            if (vc->ch == ch) {
                Regs r2 = *r;

                r2.w = 0;
                r2.x = 0;
                r2.y = 0;
                r2.z = 0;
                run_packet(c, vc, &r2, cur, depth);
                break;
            }
        }

        return;
    }

    memset(&it, 0, sizeof it);
    it.x = r->h;
    it.y = r->v;

    if (cur->missing) {
        if (cur->tfm != NULL && cur->tfm->wd[ch] > 0.0) {
            it.type = IT_BOX;
            it.w = round_long(font_width(cur, ch));
            it.h = round_long(cur->tfm->ht[ch] * dbl(cur->size));

            /* The depth of a box travels in the field ch. */
            it.ch = round_long(cur->tfm->dp[ch] * dbl(cur->size));
            dvi_add_item(c, &it);
        }

        return;
    }

    it.type = IT_GLYPH;
    it.font = cur;
    it.ch = ch;
    dvi_add_item(c, &it);
}


/* Runs a DVI command string; scale is non-zero inside a virtual font. */
static void
run(Ctx *c, const unsigned char *p, const unsigned char *end, Regs *r,
    long scale, const FTab *ft, int depth)
{
    Regs *stack = NULL;
    long sp = 0;
    long scap = 0;
    DFont *cur = ft->n > 0 && scale != 0 ? ft->f[0] : NULL;

    while (p < end) {
        long op = *p++;
        long ch = -1;
        int put = 0;
        long fnum = -1;

        if (op < 128) {
            ch = op;
        } else if (op <= 131) {
            int n = to_int(op - 127);

            ch = get_u(p, n);
            p += n;
        } else if (op == 132 || op == 137) {
            long ht = scaled(get_s(p, 4), scale);
            long wd = scaled(get_s(p + 4, 4), scale);

            p += 8;

            if (ht > 0 && wd > 0) {
                add_rule(c, r, wd, ht);
            }

            if (op == 132) {
                r->h += wd;
            }
        } else if (op >= 133 && op <= 136) {
            int n = to_int(op - 132);

            ch = get_u(p, n);
            p += n;
            put = 1;
        } else if (op >= 143 && op <= 146) {
            int n = to_int(op - 142);

            r->h += scaled(get_s(p, n), scale);
            p += n;
        } else if (op >= 148 && op <= 151) {
            int n = to_int(op - 147);

            r->w = scaled(get_s(p, n), scale);
            p += n;
            r->h += r->w;
        } else if (op >= 153 && op <= 156) {
            int n = to_int(op - 152);

            r->x = scaled(get_s(p, n), scale);
            p += n;
            r->h += r->x;
        } else if (op >= 157 && op <= 160) {
            int n = to_int(op - 156);

            r->v += scaled(get_s(p, n), scale);
            p += n;
        } else if (op >= 162 && op <= 165) {
            int n = to_int(op - 161);

            r->y = scaled(get_s(p, n), scale);
            p += n;
            r->v += r->y;
        } else if (op >= 167 && op <= 170) {
            int n = to_int(op - 166);

            r->z = scaled(get_s(p, n), scale);
            p += n;
            r->v += r->z;
        } else if (op >= 171 && op <= 234) {
            fnum = op - 171;
        } else if (op >= 235 && op <= 238) {
            int n = to_int(op - 234);

            fnum = get_u(p, n);
            p += n;
        } else if (op >= 239 && op <= 242) {
            int n = to_int(op - 238);
            long l = get_u(p, n);

            p += n;

            if (l > end - p) {
                l = end - p;
            }

            gfx_set_pos(&c->g, 72.0 + dbl(r->h) * c->d->unit * 72.0,
                        72.0 + dbl(r->v) * c->d->unit * 72.0);
            special_run(c, p, l);
            p += l;
        } else if (op >= 243 && op <= 246) {
            /* A font definition inside a page: skip the numbers and name. */
            int n = to_int(op - 242);

            p += n + 12;
            p += p[0] + p[1] + 2;
        } else {
            switch (op) {
            case 141:
                if (sp >= scap) {
                    scap = scap * 2 + 32;
                    stack = xrealloc(stack, to_size(scap) * sizeof *stack);
                }

                stack[sp++] = *r;
                break;
            case 142:
                if (sp > 0) {
                    *r = stack[--sp];
                }

                break;
            case 147:
                r->h += r->w;
                break;
            case 152:
                r->h += r->x;
                break;
            case 161:
                r->v += r->y;
                break;
            case 166:
                r->v += r->z;
                break;
            case 139:
                p += 44;
                break;
            case 140:
                free(stack);

                return;
            default:
                break;
            }
        }

        if (fnum >= 0) {
            long i;

            for (i = 0; i < ft->n; i++) {
                if (ft->num[i] == fnum) {
                    cur = ft->f[i];
                    break;
                }
            }
        }

        if (ch >= 0 && cur != NULL) {
            font_load(cur);
            show_char(c, cur, ch, r, depth);

            if (!put) {
                r->h += round_long(font_width(cur, ch));
            }
        }
    }

    free(stack);
}


/* The length in bytes of the DVI command at p, for skipping it. */
static long
op_length(const unsigned char *p)
{
    long op = p[0];
    long n;

    if (op < 128 || op == 138 || op == 141 || op == 142 || op == 147 ||
        op == 152 || op == 161 || op == 166 || (op >= 171 && op <= 234)) {
        return 1;
    }

    if ((op >= 128 && op <= 131) || (op >= 133 && op <= 136)) {
        return 1 + (op >= 133 ? op - 132 : op - 127);
    }

    if (op == 132 || op == 137) {
        return 9;
    }

    if (op == 139) {
        return 45;
    }

    if ((op >= 143 && op <= 146) || (op >= 148 && op <= 151) ||
        (op >= 153 && op <= 156) || (op >= 157 && op <= 160) ||
        (op >= 162 && op <= 165) || (op >= 167 && op <= 170)) {
        n = (op - 143) % 5 + 1;

        return 1 + n;
    }

    if (op >= 235 && op <= 238) {
        return 1 + (op - 234);
    }

    if (op >= 239 && op <= 242) {
        n = op - 238;

        return 1 + n + get_u(p + 1, to_int(n));
    }

    if (op >= 243 && op <= 246) {
        n = op - 242;

        return 1 + n + 14 + p[1 + n + 12] + p[1 + n + 13];
    }

    return 1;
}


/*
 * The header lines of dvips ("!" specials, such as the procedures of the pgf
 * dvips driver) may stand on any page; they are run once, in file order, the
 * first time a PostScript special is met.
 */
void
dvi_ps_prologue(Dvi *d)
{
    ItemList scratch;
    Gfx g;
    long pg;

    if (d->prologue_done) {
        return;
    }

    d->prologue_done = 1;
    d->ps = psi_new();
    memset(&scratch, 0, sizeof scratch);
    gfx_init(&g, &scratch, d->paper_h * 72.0);

    for (pg = 0; pg < d->npages; pg++) {
        const unsigned char *p = d->data + d->offs[pg];
        const unsigned char *end = d->data + d->len;

        while (end - p > 16 && *p != 140) {
            long n = op_length(p);

            if (*p >= 239 && *p <= 242) {
                int nb = to_int(*p - 238);
                long len = get_u(p + 1, nb);

                if (len <= end - (p + 1 + nb)) {
                    special_prologue(d, &g, p + 1 + nb, len);
                }
            }

            p += n;
        }
    }

    gfx_done(&g);
    itemlist_reset(&scratch);
    free(scratch.v);
}


/*
 * The status special of the Printer: "strata:print-failed N" on the first
 * page says that the print had N errors.  It is read when the file is
 * opened, so that the status area is there before the first page is drawn.
 */
static void
dvi_scan_status(Dvi *d)
{
    static const char key[] = "strata:print-failed";
    const unsigned char *p = d->data + d->offs[0];
    const unsigned char *end = d->data + d->len;

    while (end - p > 16 && *p != 140) {
        long n = op_length(p);

        if (*p >= 239 && *p <= 242) {
            int nb = to_int(*p - 238);
            long len = get_u(p + 1, nb);
            const unsigned char *s = p + 1 + nb;

            if (len <= end - s && len > to_long(sizeof key) &&
                memcmp(s, key, sizeof key - 1) == 0) {
                char num[16];
                long k = len - to_long(sizeof key);

                if (k >= to_long(sizeof num)) {
                    k = to_long(sizeof num) - 1;
                }

                memcpy(num, s + sizeof key, to_size(k));
                num[k] = '\0';
                d->failed = atol(num);

                if (d->failed < 1) {
                    d->failed = 1;
                }

                return;
            }
        }

        p += n;
    }
}


int
dvi_page(Dvi *d, long page, ItemList *out)
{
    Ctx c;
    Regs r;

    if (page < 1 || page > d->npages) {
        return -1;
    }

    memset(&c, 0, sizeof c);
    memset(&r, 0, sizeof r);
    c.d = d;
    c.out = out;
    c.page = page;
    itemlist_reset(out);
    gfx_init(&c.g, out, d->paper_h * 72.0);
    run(&c, d->data + d->offs[page - 1], d->data + d->len, &r, 0, &d->ft, 0);
    out->paper_w = d->paper_w;
    out->paper_h = d->paper_h;
    gfx_done(&c.g);

    return 0;
}


Dvi *
dvi_open(const char *path)
{
    size_t len;
    unsigned char *b = read_file(path, &len);
    Dvi *d;
    long i;
    long post;
    long pos;
    long last;
    long fcap = 0;
    long n;
    long num;
    long den;

    if (b == NULL) {
        viewer_tell("fail",
                    "cannot read the file %s: the Viewer needs a DVI file "
                    "that exists and can be read",
                    path);
        return NULL;
    }

    i = to_long(len) - 1;

    while (i >= 0 && b[i] == 223) {
        i--;
    }

    if (len < 30 || b[0] != 247 || i < 10 || b[i] != 2) {
        viewer_tell("fail",
                    "%s is not a DVI file (version 2): it was not made by "
                    "the Printer, or it was cut short",
                    path);
        free(b);

        return NULL;
    }

    post = get_u(b + i - 4, 4);

    if (post < 0 || post + 29 > to_long(len) || b[post] != 248) {
        viewer_tell("fail",
                    "%s has a damaged postamble: the file was cut short, "
                    "so print the document again",
                    path);
        free(b);

        return NULL;
    }

    d = xcalloc(1, sizeof *d);
    d->path = xstrdup(path);
    d->data = b;
    d->len = len;
    {
        const char *slash = strrchr(path, '/');
        size_t dl = slash != NULL ? to_size(slash - path) : 0;

        d->dir = xcalloc(dl + 2, 1);

        if (dl > 0) {
            memcpy(d->dir, path, dl);
        } else {
            d->dir[0] = '.';
        }

        image_set_dir(d->dir);
    }
    d->mag = get_u(b + post + 13, 4);
    num = get_u(b + post + 5, 4);
    den = get_u(b + post + 9, 4);
    d->unit = dbl(num) / dbl(den) * 1e-7 / 0.0254 * dbl(d->mag) / 1000.0;
    d->npages = get_u(b + post + 27, 2);
    d->paper_w = 8.5;
    d->paper_h = 11.0;
    last = get_u(b + post + 1, 4);
    d->offs = xcalloc(to_size(d->npages + 1), sizeof *d->offs);
    n = d->npages;

    while (n > 0 && last >= 0 && last + 45 <= to_long(len) && b[last] == 139) {
        d->offs[--n] = last;
        last = get_s(b + last + 41, 4);
    }

    if (n != 0) {
        viewer_tell("fail",
                    "%s: the chain of pages is damaged: the file was cut "
                    "short, so print the document again",
                    path);
        free(d->offs);
        free(b);
        free(d);

        return NULL;
    }

    font_init(d->unit, d->mag);
    pos = post + 29;

    while (pos < to_long(len) && b[pos] >= 243 && b[pos] <= 246) {
        int kb = to_int(b[pos] - 242);
        long k = get_u(b + pos + 1, kb);
        const unsigned char *q = b + pos + 1 + kb;
        long a = q[12];
        long l = q[13];
        char name[256];
        long nl = a + l;

        if (nl >= to_long(sizeof name)) {
            nl = to_long(sizeof name) - 1;
        }

        memcpy(name, q + 14, to_size(nl));
        name[nl] = '\0';

        if (d->ft.n >= fcap) {
            fcap = fcap * 2 + 16;
            d->ft.f = xrealloc(d->ft.f, to_size(fcap) * sizeof *d->ft.f);
            d->ft.num = xrealloc(d->ft.num, to_size(fcap) * sizeof *d->ft.num);
        }

        d->ft.f[d->ft.n] = font_get(name, get_u(q + 4, 4), get_u(q + 8, 4));
        d->ft.num[d->ft.n] = k;
        d->ft.n++;
        pos += 1 + kb + 14 + a + l;
    }

    if (d->npages > 0) {
        dvi_scan_status(d);
    }

    return d;
}
