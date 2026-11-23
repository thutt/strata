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
 * Fonts: TFM metrics, Type 1 shapes through FreeType, PK fallback,
 * virtual fonts, and the cache of rendered glyphs.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "dviview.h"

#define HASH_SIZE 8192

typedef struct Entry {
    int font;
    long ch;
    long size64;
    long q[4];
    Glyph *g;
    struct Entry *next;
} Entry;

static FT_Library ft_lib;
static int ft_ok;
static DFont **fonts;
static long nfonts;
static Entry *buckets[HASH_SIZE];
static double g_unit = 1.0;
static long g_mag = 1000;


void
font_init(double unit, long mag)
{
    g_unit = unit;
    g_mag = mag;

    if (!ft_ok && FT_Init_FreeType(&ft_lib) == 0) {
        ft_ok = 1;
    }
}


DFont *
font_get(const char *name, long size, long design)
{
    long i;
    DFont *f;

    for (i = 0; i < nfonts; i++) {
        if (fonts[i]->size == size && strcmp(fonts[i]->name, name) == 0) {
            return fonts[i];
        }
    }

    f = xcalloc(1, sizeof *f);
    f->name = xstrdup(name);
    f->size = size;
    f->design = design;
    f->id = to_int(nfonts);
    f->cur_size64 = -1;
    fonts = xrealloc(fonts, to_size(nfonts + 1) * sizeof *fonts);
    fonts[nfonts++] = f;

    return f;
}


double
font_width(const DFont *f, long ch)
{
    if (f->tfm == NULL || ch < 0 || ch > 255) {
        return 0.5 * dbl(f->size);
    }

    return f->tfm->wd[ch] * dbl(f->size);
}


static void
vf_parse(DFont *f, const char *path)
{
    size_t len;
    unsigned char *d = read_file(path, &len);
    Vf *vf;
    long pos;
    long cap = 0;
    long fcap = 0;

    if (d == NULL || len < 11 || d[0] != 247 || d[1] != 202) {
        free(d);
        return;
    }

    vf = xcalloc(1, sizeof *vf);
    vf->data = d;
    pos = 3 + d[2] + 8;

    while (pos < to_long(len)) {
        long op = d[pos];

        if (op >= 243 && op <= 246) {
            int kb = to_int(op - 242);
            long k = get_u(d + pos + 1, kb);
            const unsigned char *q = d + pos + 1 + kb;
            long s = get_u(q + 4, 4);
            long dd = get_u(q + 8, 4);
            long a = q[12];
            long l = q[13];
            char name[256];
            long nl = a + l;

            if (nl >= to_long(sizeof name)) {
                nl = to_long(sizeof name) - 1;
            }

            memcpy(name, q + 14, to_size(nl));
            name[nl] = '\0';

            if (vf->ft.n >= fcap) {
                fcap = fcap * 2 + 8;
                vf->ft.f = xrealloc(vf->ft.f,
                                    to_size(fcap) * sizeof *vf->ft.f);
                vf->ft.num = xrealloc(vf->ft.num,
                                      to_size(fcap) * sizeof *vf->ft.num);
            }

            /* The scale is a fix_word relative to the size of this font. */
            vf->ft.f[vf->ft.n] = font_get(name, (s * f->size) >> 20, dd);
            vf->ft.num[vf->ft.n] = k;
            vf->ft.n++;
            pos += 1 + kb + 14 + a + l;
            continue;
        }

        if (op <= 242) {
            long pl;
            long cc;
            long tw;
            long hl;

            if (op == 242) {
                pl = get_u(d + pos + 1, 4);
                cc = get_u(d + pos + 5, 4);
                tw = get_s(d + pos + 9, 4);
                hl = 13;
            } else {
                pl = op;
                cc = d[pos + 1];
                tw = get_s(d + pos + 2, 3);
                hl = 5;
            }

            if (vf->nchars >= cap) {
                cap = cap * 2 + 64;
                vf->chars = xrealloc(vf->chars,
                                     to_size(cap) * sizeof *vf->chars);
            }

            vf->chars[vf->nchars].ch = cc;
            vf->chars[vf->nchars].tfmw = dbl(tw) / 1048576.0;
            vf->chars[vf->nchars].code = d + pos + hl;
            vf->chars[vf->nchars].len = pl;
            vf->nchars++;
            pos += hl + pl;
            continue;
        }

        break;
    }

    f->vf = vf;
}


static void
ft_setup(DFont *f, const char *pfb_path)
{
    FT_Face face = NULL;
    FT_Matrix m;

    if (!ft_ok || FT_New_Face(ft_lib, pfb_path, 0, &face) != 0) {
        return;
    }

    if (f->have_map && f->map.enc != NULL) {
        f->enc = enc_load(f->map.enc);
    }

    if (f->enc == NULL) {
        if (FT_Select_Charmap(face, FT_ENCODING_ADOBE_CUSTOM) != 0) {
            FT_Select_Charmap(face, FT_ENCODING_ADOBE_STANDARD);
        }
    }

    m.xx = round_long(f->map.extend * 65536.0);
    m.xy = round_long(f->map.slant * 65536.0);
    m.yx = 0;
    m.yy = 65536;
    FT_Set_Transform(face, &m, NULL);
    f->face = face;
}


/* Finds the PK file of a font that exists; returns the resolution that it requires. */
static long
pk_setup(DFont *f)
{
    char dpi[64];
    char pkname[300];
    char *path;
    long d = f->design > 0 ? f->design : 1;
    long v = round_long(600.0 * dbl(g_mag) / 1000.0 * dbl(f->size) / dbl(d));

    snprintf(dpi, sizeof dpi, "-dpi=%ld", v);
    snprintf(pkname, sizeof pkname, "%s.pk", f->name);
    path = kpse_find("-mode=ljfour", dpi, pkname);

    if (path != NULL) {
        f->pk = pk_load(path);
        free(path);
    }

    return v;
}


void
font_load(DFont *f)
{
    char file[300];
    char *path;
    long dpi = 0;

    if (f->loaded) {
        return;
    }

    f->loaded = 1;
    snprintf(file, sizeof file, "%s.tfm", f->name);
    path = kpse_find(file, NULL, NULL);

    if (path != NULL) {
        f->tfm = tfm_load(path);
        free(path);
    }

    if (f->tfm == NULL) {
        viewer_tell("note",
                    "no TFM file for the font %s: its letters have no "
                    "widths, so the preview spaces them wrongly; the PDF is "
                    "not affected.  Check that the TeX font packages are "
                    "installed",
                    f->name);
    }

    snprintf(file, sizeof file, "%s.vf", f->name);
    path = kpse_find(file, NULL, NULL);

    if (path != NULL) {
        vf_parse(f, path);
        free(path);

        if (f->vf != NULL) {
            return;
        }
    }

    if (getenv("DVIVIEW_NOPFB") == NULL) {
        f->have_map = map_lookup(f->name, &f->map);

        if (f->have_map) {
            path = kpse_find(f->map.pfb, NULL, NULL);
        } else {
            snprintf(file, sizeof file, "%s.pfb", f->name);
            path = kpse_find(file, NULL, NULL);
            f->map.extend = 1.0;
        }

        if (path != NULL) {
            ft_setup(f, path);
            free(path);
        }
    }

    if (f->face == NULL) {
        dpi = pk_setup(f);
    }

    if (f->face == NULL && f->pk == NULL) {
        f->missing = 1;

        /* A font of METAFONT sources gets its bitmap made, as TeX does it;
           the boxes stay until the job is done (mkfont.c tells if not). */
        if (mk_request(f, dpi)) {
            return;
        }

        if (!mk_possible() && f->mk_tried == 0) {
            viewer_tell("note",
                        "font %s not found, drawing boxes where its "
                        "letters are; the PDF is not affected.  Install "
                        "the TeX package that makes bitmap fonts (the "
                        "programs mktexpk and mf) to see them",
                        f->name);
        } else {
            viewer_tell("note",
                        "font %s not found, drawing boxes where its "
                        "letters are; the PDF is not affected.  Check that "
                        "the TeX font packages are installed",
                        f->name);
        }
    }
}


/* Sets the transform of the face: slant and extend, then lin when given. */
static void
ft_transform(DFont *f, const Mat *lin)
{
    FT_Matrix m;
    double bxx = f->map.extend;
    double bxy = f->map.slant;

    if (lin == NULL) {
        m.xx = round_long(bxx * 65536.0);
        m.xy = round_long(bxy * 65536.0);
        m.yx = 0;
        m.yy = 65536;
    } else {
        /* Outlines are y up, the matrix of the page is y down. */
        double lxx = lin->a;
        double lxy = -lin->c;
        double lyx = -lin->b;
        double lyy = lin->d;

        m.xx = round_long(lxx * bxx * 65536.0);
        m.xy = round_long((lxx * bxy + lxy) * 65536.0);
        m.yx = round_long(lyx * bxx * 65536.0);
        m.yy = round_long((lyx * bxy + lyy) * 65536.0);
    }

    FT_Set_Transform(f->face, &m, NULL);
}


static Glyph *
ft_glyph(DFont *f, long ch, double px, const Mat *lin)
{
    FT_Face face = f->face;
    FT_UInt idx;
    long size64 = round_long(px * 64.0);
    Glyph *g = xcalloc(1, sizeof *g);
    FT_Bitmap *bm;
    long y;

    if (size64 < 64) {
        size64 = 64;
    }

    if (size64 != f->cur_size64) {
        FT_Set_Char_Size(face, 0, size64, 72, 72);
        f->cur_size64 = size64;
    }

    if (f->enc != NULL) {
        const char *nm = ch >= 0 && ch < 256 ? f->enc[ch] : NULL;

        idx = nm != NULL ? FT_Get_Name_Index(face, nm) : 0;
    } else {
        idx = FT_Get_Char_Index(face, to_uint(ch));
    }

    if (lin != NULL) {
        ft_transform(f, lin);
    }

    if (idx == 0 ||
        FT_Load_Glyph(face, idx, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0 ||
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
        if (lin != NULL) {
            ft_transform(f, NULL);
        }

        return g;
    }

    if (lin != NULL) {
        ft_transform(f, NULL);
    }

    bm = &face->glyph->bitmap;
    g->left = face->glyph->bitmap_left;
    g->top = face->glyph->bitmap_top;
    g->w = to_long(bm->width);
    g->h = to_long(bm->rows);

    if (g->w > 0 && g->h > 0) {
        g->a = xcalloc(to_size(g->w * g->h), 1);

        for (y = 0; y < g->h; y++) {
            memcpy(g->a + y * g->w, bm->buffer + y * bm->pitch,
                   to_size(g->w));
        }
    }

    return g;
}


static Glyph *
glyph_lookup(DFont *f, long ch, double px_per_unit, const Mat *lin)
{
    long size64 = round_long(px_per_unit * dbl(f->size) * 64.0);
    long q[4];
    unsigned h;
    Entry *e;

    q[0] = lin != NULL ? round_long(lin->a * 256.0) : 256;
    q[1] = lin != NULL ? round_long(lin->b * 256.0) : 0;
    q[2] = lin != NULL ? round_long(lin->c * 256.0) : 0;
    q[3] = lin != NULL ? round_long(lin->d * 256.0) : 256;
    h = to_uint(f->id * 31 + ch * 7 + size64 + q[0] * 3 + q[1] * 5 +
                q[2] * 11 + q[3] * 13) % HASH_SIZE;

    for (e = buckets[h]; e != NULL; e = e->next) {
        if (e->font == f->id && e->ch == ch && e->size64 == size64 &&
            e->q[0] == q[0] && e->q[1] == q[1] && e->q[2] == q[2] &&
            e->q[3] == q[3]) {
            return e->g;
        }
    }

    font_load(f);
    e = xcalloc(1, sizeof *e);
    e->font = f->id;
    e->ch = ch;
    e->size64 = size64;
    memcpy(e->q, q, sizeof q);

    if (f->face != NULL) {
        e->g = ft_glyph(f, ch, px_per_unit * dbl(f->size), lin);
    } else if (f->pk != NULL) {
        double ratio = px_per_unit / (g_unit * 600.0 * dbl(g_mag) / 1000.0);

        e->g = pk_glyph(f->pk, ch, ratio);

        if (e->g == NULL) {
            e->g = xcalloc(1, sizeof *e->g);
        }
    } else {
        e->g = xcalloc(1, sizeof *e->g);
    }

    e->next = buckets[h];
    buckets[h] = e;

    return e->g;
}


Glyph *
glyph_get(DFont *f, long ch, double px_per_unit)
{
    return glyph_lookup(f, ch, px_per_unit, NULL);
}


/* A glyph under a linear map (rotation, scale); lin is a, b, c, d of it. */
Glyph *
glyph_get_xf(DFont *f, long ch, double px_per_unit, const Mat *lin)
{
    return glyph_lookup(f, ch, px_per_unit, lin);
}


void
glyph_flush(void)
{
    long i;

    for (i = 0; i < HASH_SIZE; i++) {
        Entry *e = buckets[i];

        while (e != NULL) {
            Entry *n = e->next;

            free(e->g->a);
            free(e->g);
            free(e);
            e = n;
        }

        buckets[i] = NULL;
    }
}
