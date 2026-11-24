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
 * Drawing of a page: glyphs, rules, painted paths, clips and pictures are
 * put into the window buffer.  Only the page that is shown is ever drawn.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dviview.h"

/* The state of drawing one page. */
typedef struct {
    const Canvas *cv;
    const ItemList *l;
    const View *v;
    unsigned char *mask;
    int mask_id;
    Image *reduced;
    const Image *reduced_src;
    long reduced_f;
} Rc;


static long
clampl(long v, long lo, long hi)
{
    if (v > hi) {
        v = hi;
    }

    if (v < lo) {
        v = lo;
    }

    return v;
}


/* Reads one channel of a pixel as a number from 0 to 255. */
static long
channel(const char *p, int idx)
{
    return p[idx] < 0 ? p[idx] + 256 : p[idx];
}


/* Blends a colour over a pixel; a is the opacity from 0 to 255. */
static void
blend_px(const Canvas *cv, long x, long y, long a, long cr, long cg, long cb)
{
    char *p;

    if (a <= 0 || x < 0 || x >= cv->w || y < 0 || y >= cv->h) {
        return;
    }

    p = cv->buf + (y * cv->w + x) * 4;

    if (a >= 255) {
        p[cv->rb] = to_char(cr);
        p[cv->gb] = to_char(cg);
        p[cv->bb] = to_char(cb);
        p[6 - cv->rb - cv->gb - cv->bb] = 0;

        return;
    }

    p[cv->rb] = to_char((channel(p, cv->rb) * (255 - a) + cr * a + 127) / 255);
    p[cv->gb] = to_char((channel(p, cv->gb) * (255 - a) + cg * a + 127) / 255);
    p[cv->bb] = to_char((channel(p, cv->bb) * (255 - a) + cb * a + 127) / 255);
}


void
canvas_fill_rect(const Canvas *cv, long x0, long y0, long x1, long y1,
                 unsigned long rgb)
{
    long x;
    long y;

    x0 = clampl(x0, 0, cv->w);
    x1 = clampl(x1, 0, cv->w);
    y0 = clampl(y0, 0, cv->h);
    y1 = clampl(y1, 0, cv->h);

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            blend_px(cv, x, y, 255, to_long(rgb / 65536UL),
                     to_long(rgb / 256UL % 256UL), to_long(rgb % 256UL));
        }
    }
}


static long
mask_at(const Rc *rc, long x, long y)
{
    if (rc->mask == NULL) {
        return 255;
    }

    if (x < 0 || x >= rc->cv->w || y < 0 || y >= rc->cv->h) {
        return 0;
    }

    return rc->mask[y * rc->cv->w + x];
}


static void
fill_rect_m(const Rc *rc, long x0, long y0, long x1, long y1,
            unsigned long rgb)
{
    long x;
    long y;
    long cr = to_long(rgb / 65536UL);
    long cg = to_long(rgb / 256UL % 256UL);
    long cb = to_long(rgb % 256UL);

    x0 = clampl(x0, 0, rc->cv->w);
    x1 = clampl(x1, 0, rc->cv->w);
    y0 = clampl(y0, 0, rc->cv->h);
    y1 = clampl(y1, 0, rc->cv->h);

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            blend_px(rc->cv, x, y, mask_at(rc, x, y), cr, cg, cb);
        }
    }
}


static void
blit_glyph(const Rc *rc, const Glyph *g, long x0, long y0, unsigned long rgb)
{
    long cr = to_long(rgb / 65536UL);
    long cg = to_long(rgb / 256UL % 256UL);
    long cb = to_long(rgb % 256UL);
    long y;

    for (y = 0; y < g->h; y++) {
        long x;

        if (y0 + y < 0 || y0 + y >= rc->cv->h) {
            continue;
        }

        for (x = 0; x < g->w; x++) {
            long a = g->a[y * g->w + x];

            if (a == 0) {
                continue;
            }

            a = a * mask_at(rc, x0 + x, y0 + y) / 255;
            blend_px(rc->cv, x0 + x, y0 + y, a, cr, cg, cb);
        }
    }
}


/* Blends a coverage mask in one colour. */
static void
blend_cov(const Rc *rc, const Cov *c, unsigned long rgb)
{
    long cr = to_long(rgb / 65536UL);
    long cg = to_long(rgb / 256UL % 256UL);
    long cb = to_long(rgb % 256UL);
    long y;

    if (c == NULL) {
        return;
    }

    for (y = 0; y < c->h; y++) {
        long x;

        for (x = 0; x < c->w; x++) {
            long a = c->a[y * c->w + x];

            if (a == 0) {
                continue;
            }

            a = a * mask_at(rc, c->x0 + x, c->y0 + y) / 255;
            blend_px(rc->cv, c->x0 + x, c->y0 + y, a, cr, cg, cb);
        }
    }
}


/* Makes the mask of a clip: the coverage of its path and of its parents. */
static unsigned char *
make_mask(const Rc *rc, int id)
{
    const ClipItem *ci = &rc->l->clips[id - 1];
    long bounds[4];
    Cov *c;
    unsigned char *m;
    long y;
    unsigned char *parent = NULL;

    bounds[0] = 0;
    bounds[1] = 0;
    bounds[2] = rc->cv->w;
    bounds[3] = rc->cv->h;
    m = xcalloc(to_size(rc->cv->w * rc->cv->h), 1);
    c = raster_fill(&ci->path, &rc->v->view, ci->evenodd, bounds);

    if (ci->parent > 0) {
        parent = make_mask(rc, ci->parent);
    }

    if (c != NULL) {
        for (y = 0; y < c->h; y++) {
            long x;

            for (x = 0; x < c->w; x++) {
                long a = c->a[y * c->w + x];
                long off = (c->y0 + y) * rc->cv->w + c->x0 + x;

                if (parent != NULL) {
                    a = a * parent[off] / 255;
                }

                m[off] = to_u8(a);
            }
        }
    }

    cov_free(c);
    free(parent);

    return m;
}


static void
set_clip(Rc *rc, int id)
{
    if (id == rc->mask_id) {
        return;
    }

    free(rc->mask);
    rc->mask = id > 0 ? make_mask(rc, id) : NULL;
    rc->mask_id = id;
}


/* The window position of a point of the page given in DVI units. */
static void
dev_point(const View *v, double bx, double by, double *dx, double *dy)
{
    mat_pt(&v->view, bx, by, dx, dy);
}


static void
draw_glyph(const Rc *rc, const Item *it)
{
    const View *v = rc->v;
    double sb = v->d->unit * 72.0;

    if (v->rotate == 0 && it->xf == 0) {
        long ox = v->px0 + round_long(v->eff);
        long oy = v->py0 + round_long(v->eff);
        long x = ox + round_long(dbl(it->x) * v->ppu);
        long y = oy + round_long(dbl(it->y) * v->ppu);
        const Glyph *g;

        if (x < -600 || x > rc->cv->w + 600 || y < -600 ||
            y > rc->cv->h + 600) {
            return;
        }

        g = glyph_get(it->font, it->ch, v->ppu);

        if (g->a != NULL) {
            blit_glyph(rc, g, x + g->left, y - g->top, it->rgb);
        }
    } else {
        Mat total;
        Mat lin;
        double bx = 72.0 + dbl(it->x) * sb;
        double by = 72.0 + dbl(it->y) * sb;
        double dx;
        double dy;
        double k = v->eff / 72.0;
        const Glyph *g;

        if (it->xf > 0) {
            const Mat *xf = &rc->l->xfs[it->xf - 1];

            mat_pt(xf, bx, by, &bx, &by);
            total = mat_mul(*xf, v->view);
        } else {
            total = v->view;
        }

        dev_point(v, bx, by, &dx, &dy);

        if (dx < -600.0 || dx > dbl(rc->cv->w) + 600.0 || dy < -600.0 ||
            dy > dbl(rc->cv->h) + 600.0) {
            return;
        }

        lin.a = total.a / k;
        lin.b = total.b / k;
        lin.c = total.c / k;
        lin.d = total.d / k;
        lin.e = 0.0;
        lin.f = 0.0;

        if (mat_is_ident(&lin)) {
            g = glyph_get(it->font, it->ch, v->ppu);
        } else {
            g = glyph_get_xf(it->font, it->ch, v->ppu, &lin);
        }

        if (g->a != NULL) {
            blit_glyph(rc, g, round_long(dx) + g->left,
                       round_long(dy) - g->top, it->rgb);
        }
    }
}


/* Rules and boxes: the bounding box of their corners on the window. */
static void
draw_rule(const Rc *rc, const Item *it)
{
    const View *v = rc->v;
    double sb = v->d->unit * 72.0;

    if (v->rotate == 0) {
        long ox = v->px0 + round_long(v->eff);
        long oy = v->py0 + round_long(v->eff);
        long x = ox + round_long(dbl(it->x) * v->ppu);
        long y = oy + round_long(dbl(it->y) * v->ppu);

        if (it->type == IT_RULE) {
            long w = ceil_long(dbl(it->w) * v->ppu);
            long h = ceil_long(dbl(it->h) * v->ppu);

            fill_rect_m(rc, x, y - (h < 1 ? 1 : h), x + (w < 1 ? 1 : w), y,
                        it->rgb);
        } else {
            long w = round_long(dbl(it->w) * v->ppu);
            long h = round_long(dbl(it->h) * v->ppu);
            long d = round_long(dbl(it->ch) * v->ppu);

            fill_rect_m(rc, x, y - h, x + w, y - h + 1, it->rgb);
            fill_rect_m(rc, x, y + d - 1, x + w, y + d, it->rgb);
            fill_rect_m(rc, x, y - h, x + 1, y + d, it->rgb);
            fill_rect_m(rc, x + w - 1, y - h, x + w, y + d, it->rgb);
        }
    } else {
        double w = dbl(it->w) * sb;
        double h = dbl(it->h) * sb;
        double bx = 72.0 + dbl(it->x) * sb;
        double by = 72.0 + dbl(it->y) * sb;
        double xs[4];
        double ys[4];
        double minx = 1e30;
        double miny = 1e30;
        double maxx = -1e30;
        double maxy = -1e30;
        int i;
        long x0;
        long y0;
        long x1;
        long y1;

        dev_point(v, bx, by, &xs[0], &ys[0]);
        dev_point(v, bx + w, by, &xs[1], &ys[1]);
        dev_point(v, bx + w, by - h, &xs[2], &ys[2]);
        dev_point(v, bx, by - h, &xs[3], &ys[3]);

        for (i = 0; i < 4; i++) {
            minx = xs[i] < minx ? xs[i] : minx;
            maxx = xs[i] > maxx ? xs[i] : maxx;
            miny = ys[i] < miny ? ys[i] : miny;
            maxy = ys[i] > maxy ? ys[i] : maxy;
        }

        x0 = round_long(minx);
        y0 = round_long(miny);
        x1 = round_long(maxx);
        y1 = round_long(maxy);
        x1 = x1 < x0 + 1 ? x0 + 1 : x1;
        y1 = y1 < y0 + 1 ? y0 + 1 : y1;

        if (it->type == IT_RULE) {
            fill_rect_m(rc, x0, y0, x1, y1, it->rgb);
        } else {
            fill_rect_m(rc, x0, y0, x1, y0 + 1, it->rgb);
            fill_rect_m(rc, x0, y1 - 1, x1, y1, it->rgb);
            fill_rect_m(rc, x0, y0, x0 + 1, y1, it->rgb);
            fill_rect_m(rc, x1 - 1, y0, x1, y1, it->rgb);
        }
    }
}


static void
draw_path(const Rc *rc, const PathItem *pi)
{
    long bounds[4];
    Cov *c;

    bounds[0] = 0;
    bounds[1] = 0;
    bounds[2] = rc->cv->w;
    bounds[3] = rc->cv->h;

    if (pi->do_fill) {
        c = raster_fill(&pi->path, &rc->v->view, pi->evenodd, bounds);
        blend_cov(rc, c, pi->fill);
        cov_free(c);
    }

    if (pi->do_stroke) {
        c = raster_stroke(&pi->path, &rc->v->view, &pi->st, bounds);
        blend_cov(rc, c, pi->stroke);
        cov_free(c);
    }
}


/* Makes a copy of the picture made smaller by an integer factor. */
static const Image *
reduce(Rc *rc, const Image *im, long f)
{
    long w = (im->w + f - 1) / f;
    long h = (im->h + f - 1) / f;
    Image *r;
    long x;
    long y;

    if (rc->reduced != NULL && rc->reduced_src == im && rc->reduced_f == f) {
        return rc->reduced;
    }

    if (rc->reduced != NULL) {
        free(rc->reduced->rgba);
        free(rc->reduced);
    }

    r = xcalloc(1, sizeof *r);
    r->w = w;
    r->h = h;
    r->rgba = xcalloc(to_size(w * h), 4);
    memcpy(r->ext, im->ext, sizeof r->ext);
    r->ok = 1;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            long sum[4] = { 0, 0, 0, 0 };
            long n = 0;
            long yy;
            long xx;
            long k;

            for (yy = y * f; yy < (y + 1) * f && yy < im->h; yy++) {
                for (xx = x * f; xx < (x + 1) * f && xx < im->w; xx++) {
                    const unsigned char *s = im->rgba +
                                             (yy * im->w + xx) * 4;
                    long al = s[3];

                    sum[0] += s[0] * al;
                    sum[1] += s[1] * al;
                    sum[2] += s[2] * al;
                    sum[3] += al;
                    n++;
                }
            }

            for (k = 0; k < 3; k++) {
                r->rgba[(y * w + x) * 4 + k] =
                    to_u8(sum[3] > 0 ? sum[k] / sum[3] : 0);
            }

            r->rgba[(y * w + x) * 4 + 3] = to_u8(n > 0 ? sum[3] / n : 0);
        }
    }

    rc->reduced = r;
    rc->reduced_src = im;
    rc->reduced_f = f;

    return r;
}


/* Bilinear sample of the picture at the pixel position (u, v). */
static void
sample(const Image *im, double u, double v, double *out)
{
    long x0 = floor_long(u - 0.5);
    long y0 = floor_long(v - 0.5);
    double fx = u - 0.5 - dbl(x0);
    double fy = v - 0.5 - dbl(y0);
    double acc[4] = { 0.0, 0.0, 0.0, 0.0 };
    int i;
    int j;
    int k;

    for (j = 0; j < 2; j++) {
        for (i = 0; i < 2; i++) {
            long x = clampl(x0 + i, 0, im->w - 1);
            long y = clampl(y0 + j, 0, im->h - 1);
            const unsigned char *s = im->rgba + (y * im->w + x) * 4;
            double wt = (i == 0 ? 1.0 - fx : fx) * (j == 0 ? 1.0 - fy : fy);

            for (k = 0; k < 3; k++) {
                acc[k] += wt * s[k] * s[3];
            }

            acc[3] += wt * s[3];
        }
    }

    for (k = 0; k < 3; k++) {
        out[k] = acc[3] > 0.0 ? acc[k] / acc[3] : 0.0;
    }

    out[3] = acc[3];
}


/* The matrix from the native space of a picture to the window. */
static int
image_matrix(const ImgItem *im, const View *v, double *ext, double *bbox,
             Mat *total)
{
    double w0;
    double h0;
    double sx;
    double sy;
    Mat nat;
    int is_ps = 0;
    size_t n = strlen(im->name);

    if (n > 3 && (strcmp(im->name + n - 3, ".ps") == 0 ||
                  (n > 4 && strcmp(im->name + n - 4, ".eps") == 0))) {
        is_ps = 1;
    }

    if (!image_extent(im->name, im->page, im->media_box, ext)) {
        return 0;
    }

    if (im->has_bbox && !(!is_ps && fabs(im->bbox[0]) < 1e-6 &&
                          fabs(im->bbox[1]) < 1e-6 &&
                          fabs(im->bbox[2] - 72.0) < 1e-6 &&
                          fabs(im->bbox[3] - 72.0) < 1e-6)) {
        memcpy(bbox, im->bbox, 4 * sizeof bbox[0]);
    } else {
        memcpy(bbox, ext, 4 * sizeof bbox[0]);
    }

    w0 = bbox[2] - bbox[0];
    h0 = bbox[3] - bbox[1];

    if (w0 < 1e-6 || h0 < 1e-6) {
        return 0;
    }

    if (im->width > 0.0 && im->height > 0.0) {
        sx = im->width / w0;
        sy = im->height / h0;
    } else if (im->width > 0.0) {
        sx = im->width / w0;
        sy = sx;
    } else if (im->height > 0.0) {
        sy = im->height / h0;
        sx = sy;
    } else {
        sx = im->xscale;
        sy = im->yscale;
    }

    nat = mat_ident();
    nat.a = sx;
    nat.d = -sy;
    nat.e = -bbox[0] * sx;
    nat.f = bbox[1] * sy;
    *total = mat_mul(mat_mul(nat, im->m), v->view);

    return 1;
}


/* A picture that cannot be shown: a marked empty box with its name. */
static void
draw_missing(const Rc *rc, const ImgItem *im, const View *v)
{
    double w = 72.0 * im->xscale;
    double h = 72.0 * im->yscale;
    Mat m = mat_mul(im->m, v->view);
    Path p;
    StrokeStyle st;
    long bounds[4];
    Cov *c;
    double x0;
    double y0;
    const char *base = strrchr(im->name, '/');
    DFont *f;
    long size;
    double px;
    long i;

    if (im->has_bbox && im->bbox[2] > im->bbox[0] &&
        im->bbox[3] > im->bbox[1]) {
        w = (im->bbox[2] - im->bbox[0]) * im->xscale;
        h = (im->bbox[3] - im->bbox[1]) * im->yscale;

        if (im->width > 0.0 && im->height <= 0.0) {
            h = im->width * h / w;
        } else if (im->height > 0.0 && im->width <= 0.0) {
            w = im->height * w / h;
        }
    }

    if (im->width > 0.0) {
        w = im->width;
    }

    if (im->height > 0.0) {
        h = im->height;
    }

    if (w < 20.0) {
        w = 20.0;
    }

    if (h < 12.0) {
        h = 12.0;
    }

    memset(&p, 0, sizeof p);
    memset(&st, 0, sizeof st);
    st.lw = 0.8;
    st.miter = 10.0;
    bounds[0] = 0;
    bounds[1] = 0;
    bounds[2] = rc->cv->w;
    bounds[3] = rc->cv->h;
    path_rect(&p, 0.0, -h, w, h);
    c = raster_stroke(&p, &m, &st, bounds);
    blend_cov(rc, c, 0xc00000UL);
    cov_free(c);
    path_clear(&p);
    path_move(&p, 0.0, 0.0);
    path_line(&p, w, -h);
    path_move(&p, 0.0, -h);
    path_line(&p, w, 0.0);
    c = raster_stroke(&p, &m, &st, bounds);
    blend_cov(rc, c, 0xe0a0a0UL);
    cov_free(c);
    path_free(&p);

    /* The name goes under the top edge, in the typewriter font. */
    base = base != NULL ? base + 1 : im->name;
    size = round_long(8.0 / 72.27 / v->d->unit);
    f = font_get("cmtt10", size, round_long(10.0 / 72.27 / v->d->unit));
    px = v->ppu;
    mat_pt(&m, 2.0, -h + 11.0, &x0, &y0);

    for (i = 0; base[i] != '\0'; i++) {
        const Glyph *g = glyph_get(f, to_u8(base[i]), px);

        if (g->a != NULL) {
            blit_glyph(rc, g, round_long(x0) + g->left, round_long(y0) - g->top,
                       0xc00000UL);
        }

        x0 += font_width(f, to_u8(base[i])) * px;
    }
}


static void
draw_image(Rc *rc, const ImgItem *im)
{
    const View *v = rc->v;
    double ext[4];
    double bbox[4];
    Mat total;
    Mat inv;
    Image *img;
    const Image *use;
    double scale;
    double dx[4];
    double dy[4];
    double minx = 1e30;
    double miny = 1e30;
    double maxx = -1e30;
    double maxy = -1e30;
    double *r;
    Path p;
    long bounds[4];
    Cov *c;
    long y;
    long f = 1;
    double sx_img;
    int i;

    if (!image_matrix(im, v, ext, bbox, &total) ||
        !mat_inv(&total, &inv)) {
        draw_missing(rc, im, v);

        return;
    }

    r = im->clip ? bbox : ext;
    mat_pt(&total, r[0], r[1], &dx[0], &dy[0]);
    mat_pt(&total, r[2], r[1], &dx[1], &dy[1]);
    mat_pt(&total, r[2], r[3], &dx[2], &dy[2]);
    mat_pt(&total, r[0], r[3], &dx[3], &dy[3]);

    for (i = 0; i < 4; i++) {
        minx = dx[i] < minx ? dx[i] : minx;
        maxx = dx[i] > maxx ? dx[i] : maxx;
        miny = dy[i] < miny ? dy[i] : miny;
        maxy = dy[i] > maxy ? dy[i] : maxy;
    }

    if (maxx < 0.0 || minx > dbl(rc->cv->w) || maxy < 0.0 ||
        miny > dbl(rc->cv->h)) {
        return;
    }

    scale = mat_det_scale(&total);
    img = image_get(im->name, im->page, im->media_box, scale * 72.0);

    if (!img->ok) {
        draw_missing(rc, im, v);

        return;
    }

    /* How many picture pixels fall on one window pixel. */
    sx_img = dbl(img->w) / ((ext[2] - ext[0]) * scale);
    use = img;

    if (sx_img >= 2.0) {
        f = floor_long(sx_img);
        use = reduce(rc, img, f);
    }

    memset(&p, 0, sizeof p);
    path_move(&p, r[0], r[1]);
    path_line(&p, r[2], r[1]);
    path_line(&p, r[2], r[3]);
    path_line(&p, r[0], r[3]);
    path_close(&p);
    bounds[0] = 0;
    bounds[1] = 0;
    bounds[2] = rc->cv->w;
    bounds[3] = rc->cv->h;
    c = raster_fill(&p, &total, 0, bounds);
    path_free(&p);

    if (c == NULL) {
        return;
    }

    for (y = 0; y < c->h; y++) {
        long x;

        for (x = 0; x < c->w; x++) {
            long a = c->a[y * c->w + x];
            double nx;
            double ny;
            double u;
            double w;
            double col[4];

            if (a == 0) {
                continue;
            }

            mat_pt(&inv, dbl(c->x0 + x) + 0.5, dbl(c->y0 + y) + 0.5, &nx, &ny);
            u = (nx - ext[0]) / (ext[2] - ext[0]) * dbl(use->w);
            w = (ext[3] - ny) / (ext[3] - ext[1]) * dbl(use->h);
            sample(use, u, w, col);
            a = round_long(dbl(a) * col[3] / 255.0);
            a = a * mask_at(rc, c->x0 + x, c->y0 + y) / 255;
            blend_px(rc->cv, c->x0 + x, c->y0 + y, a, round_long(col[0]),
                     round_long(col[1]), round_long(col[2]));
        }
    }

    cov_free(c);
}


/* The matrix from the page in bp to the window for this zoom and scroll. */
void
view_make(View *v, const Dvi *d, const ItemList *l, double eff, long w,
          long h, long scroll_x, long scroll_y)
{
    double pw = ceil(l->paper_w * eff);
    double ph = ceil(l->paper_h * eff);
    double k = eff / 72.0;
    long dw = round_long(pw);
    long dh = round_long(ph);
    Mat m = mat_ident();

    v->d = d;
    v->eff = eff;
    v->ppu = d->unit * eff;
    v->rotate = l->rotate;

    if (l->rotate == 90 || l->rotate == 270) {
        long t = dw;

        dw = dh;
        dh = t;
    }

    v->pw = dw;
    v->ph = dh;
    v->px0 = dw < w ? (w - dw) / 2 : -scroll_x;
    v->py0 = dh < h ? (h - dh) / 2 : -scroll_y;

    switch (l->rotate) {
    case 90:
        m.a = 0.0;
        m.b = k;
        m.c = -k;
        m.d = 0.0;
        m.e = dbl(v->px0) + ph;
        m.f = dbl(v->py0);
        break;
    case 180:
        m.a = -k;
        m.d = -k;
        m.e = dbl(v->px0) + pw;
        m.f = dbl(v->py0) + ph;
        break;
    case 270:
        m.a = 0.0;
        m.b = -k;
        m.c = k;
        m.d = 0.0;
        m.e = dbl(v->px0);
        m.f = dbl(v->py0) + pw;
        break;
    default:
        m.a = k;
        m.d = k;
        m.e = dbl(v->px0);
        m.f = dbl(v->py0);
        break;
    }

    v->view = m;
}


void
render_page(const Canvas *cv, const ItemList *l, const View *v)
{
    Rc rc;
    long i;

    memset(&rc, 0, sizeof rc);
    rc.cv = cv;
    rc.l = l;
    rc.v = v;
    canvas_fill_rect(cv, 0, 0, cv->w, cv->h, 0xb0b0b0UL);
    canvas_fill_rect(cv, v->px0, v->py0, v->px0 + v->pw, v->py0 + v->ph,
                     l->has_bg ? l->bg : 0xffffffUL);

    for (i = 0; i < l->n; i++) {
        const Item *it = &l->v[i];

        set_clip(&rc, it->clip);

        switch (it->type) {
        case IT_GLYPH:
            draw_glyph(&rc, it);
            break;
        case IT_RULE:
        case IT_BOX:
            draw_rule(&rc, it);
            break;
        case IT_PATH:
            draw_path(&rc, &l->paths[it->ref]);
            break;
        case IT_IMAGE:
            draw_image(&rc, &l->imgs[it->ref]);
            break;
        default:
            break;
        }
    }

    free(rc.mask);

    if (rc.reduced != NULL) {
        free(rc.reduced->rgba);
        free(rc.reduced);
    }
}
