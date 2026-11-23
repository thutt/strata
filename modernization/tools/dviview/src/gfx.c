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
 * Vector graphics: matrices, paths, and an anti-aliased scan converter for
 * fills and strokes.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dviview.h"

#define SUBROWS 8
#define PI_VALUE 3.14159265358979323846


Mat
mat_ident(void)
{
    Mat m;

    m.a = 1.0;
    m.b = 0.0;
    m.c = 0.0;
    m.d = 1.0;
    m.e = 0.0;
    m.f = 0.0;

    return m;
}


/* Returns the matrix that applies first and then second. */
Mat
mat_mul(Mat first, Mat second)
{
    Mat r;

    r.a = first.a * second.a + first.b * second.c;
    r.b = first.a * second.b + first.b * second.d;
    r.c = first.c * second.a + first.d * second.c;
    r.d = first.c * second.b + first.d * second.d;
    r.e = first.e * second.a + first.f * second.c + second.e;
    r.f = first.e * second.b + first.f * second.d + second.f;

    return r;
}


Mat
mat_translate(double tx, double ty)
{
    Mat m = mat_ident();

    m.e = tx;
    m.f = ty;

    return m;
}


Mat
mat_scale(double sx, double sy)
{
    Mat m = mat_ident();

    m.a = sx;
    m.d = sy;

    return m;
}


int
mat_inv(const Mat *m, Mat *out)
{
    double det = m->a * m->d - m->b * m->c;

    if (fabs(det) < 1e-12) {
        return 0;
    }

    out->a = m->d / det;
    out->b = -m->b / det;
    out->c = -m->c / det;
    out->d = m->a / det;
    out->e = (m->c * m->f - m->d * m->e) / det;
    out->f = (m->b * m->e - m->a * m->f) / det;

    return 1;
}


void
mat_pt(const Mat *m, double x, double y, double *ox, double *oy)
{
    *ox = m->a * x + m->c * y + m->e;
    *oy = m->b * x + m->d * y + m->f;
}


/* The factor by which the matrix scales lengths, on average. */
double
mat_det_scale(const Mat *m)
{
    return sqrt(fabs(m->a * m->d - m->b * m->c));
}


int
mat_is_ident(const Mat *m)
{
    return fabs(m->a - 1.0) < 1e-6 && fabs(m->d - 1.0) < 1e-6 &&
           fabs(m->b) < 1e-6 && fabs(m->c) < 1e-6 && fabs(m->e) < 1e-4 &&
           fabs(m->f) < 1e-4;
}


void
path_clear(Path *p)
{
    p->nop = 0;
    p->nxy = 0;
}


void
path_free(Path *p)
{
    free(p->op);
    free(p->xy);
    memset(p, 0, sizeof *p);
}


void
path_copy(Path *dst, const Path *src)
{
    path_clear(dst);

    if (src->nop > 0) {
        dst->op = xrealloc(dst->op, to_size(src->nop));
        memcpy(dst->op, src->op, to_size(src->nop));
        dst->capop = src->nop;
    }

    if (src->nxy > 0) {
        dst->xy = xrealloc(dst->xy, to_size(src->nxy) * sizeof *dst->xy);
        memcpy(dst->xy, src->xy, to_size(src->nxy) * sizeof *dst->xy);
        dst->capxy = src->nxy;
    }

    dst->nop = src->nop;
    dst->nxy = src->nxy;
}


static void
path_op(Path *p, char op, const double *c, long n)
{
    long i;

    if (p->nop >= p->capop) {
        p->capop = p->capop * 2 + 32;
        p->op = xrealloc(p->op, to_size(p->capop));
    }

    if (p->nxy + n > p->capxy) {
        p->capxy = p->capxy * 2 + 64 + n;
        p->xy = xrealloc(p->xy, to_size(p->capxy) * sizeof *p->xy);
    }

    p->op[p->nop++] = op;

    for (i = 0; i < n; i++) {
        p->xy[p->nxy++] = c[i];
    }
}


void
path_move(Path *p, double x, double y)
{
    double c[2];

    c[0] = x;
    c[1] = y;
    path_op(p, 'M', c, 2);
}


void
path_line(Path *p, double x, double y)
{
    double c[2];

    c[0] = x;
    c[1] = y;
    path_op(p, 'L', c, 2);
}


void
path_curve(Path *p, const double *c)
{
    path_op(p, 'C', c, 6);
}


void
path_close(Path *p)
{
    path_op(p, 'Z', NULL, 0);
}


void
path_rect(Path *p, double x, double y, double w, double h)
{
    path_move(p, x, y);
    path_line(p, x + w, y);
    path_line(p, x + w, y + h);
    path_line(p, x, y + h);
    path_close(p);
}


void
path_transform(Path *p, const Mat *m)
{
    long i;

    for (i = 0; i + 1 < p->nxy; i += 2) {
        double x;
        double y;

        mat_pt(m, p->xy[i], p->xy[i + 1], &x, &y);
        p->xy[i] = x;
        p->xy[i + 1] = y;
    }
}


/* Contours of points, the form the scan converter works on. */
typedef struct {
    double *x;
    double *y;
    long n;
    long cap;
    long *start;
    int *closed;
    long nc;
    long capc;
} Poly;


static void
poly_free(Poly *q)
{
    free(q->x);
    free(q->y);
    free(q->start);
    free(q->closed);
    memset(q, 0, sizeof *q);
}


static void
poly_begin(Poly *q)
{
    if (q->nc >= q->capc) {
        q->capc = q->capc * 2 + 16;
        q->start = xrealloc(q->start, to_size(q->capc) * sizeof *q->start);
        q->closed = xrealloc(q->closed, to_size(q->capc) * sizeof *q->closed);
    }

    q->start[q->nc] = q->n;
    q->closed[q->nc] = 0;
    q->nc++;
}


static void
poly_pt(Poly *q, double x, double y)
{
    if (q->n >= q->cap) {
        q->cap = q->cap * 2 + 256;
        q->x = xrealloc(q->x, to_size(q->cap) * sizeof *q->x);
        q->y = xrealloc(q->y, to_size(q->cap) * sizeof *q->y);
    }

    q->x[q->n] = x;
    q->y[q->n] = y;
    q->n++;
}


static long
poly_end(const Poly *q, long c)
{
    return c + 1 < q->nc ? q->start[c + 1] : q->n;
}


/* Adds a curve as line segments; the number of steps follows its size. */
static void
flatten_curve(Poly *q, double x0, double y0, const double *c)
{
    double len = hypot(c[0] - x0, c[1] - y0) + hypot(c[2] - c[0], c[3] - c[1]) +
                 hypot(c[4] - c[2], c[5] - c[3]);
    long steps = ceil_long(sqrt(len) * 1.2) + 2;
    long i;

    if (steps > 80) {
        steps = 80;
    }

    for (i = 1; i <= steps; i++) {
        double t = dbl(i) / dbl(steps);
        double u = 1.0 - t;
        double w0 = u * u * u;
        double w1 = 3.0 * u * u * t;
        double w2 = 3.0 * u * t * t;
        double w3 = t * t * t;

        poly_pt(q, w0 * x0 + w1 * c[0] + w2 * c[2] + w3 * c[4],
                w0 * y0 + w1 * c[1] + w2 * c[3] + w3 * c[5]);
    }
}


/* Converts a path through a matrix into contours of points. */
static void
flatten(const Path *p, const Mat *m, Poly *q)
{
    long ix = 0;
    long i;
    double sx = 0.0;
    double sy = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    int open = 0;

    for (i = 0; i < p->nop; i++) {
        double pt[6];
        long k;

        switch (p->op[i]) {
        case 'M':
            mat_pt(m, p->xy[ix], p->xy[ix + 1], &sx, &sy);
            ix += 2;
            poly_begin(q);
            poly_pt(q, sx, sy);
            cx = sx;
            cy = sy;
            open = 1;
            break;
        case 'L':
            mat_pt(m, p->xy[ix], p->xy[ix + 1], &cx, &cy);
            ix += 2;

            if (open) {
                poly_pt(q, cx, cy);
            }

            break;
        case 'C':
            for (k = 0; k < 3; k++) {
                mat_pt(m, p->xy[ix + 2 * k], p->xy[ix + 2 * k + 1],
                       &pt[2 * k], &pt[2 * k + 1]);
            }

            ix += 6;

            if (open) {
                flatten_curve(q, cx, cy, pt);
            }

            cx = pt[4];
            cy = pt[5];
            break;
        case 'Z':
            if (open) {
                q->closed[q->nc - 1] = 1;
            }

            cx = sx;
            cy = sy;
            break;
        default:
            break;
        }
    }
}


static void
bbox_of(const Poly *q, double *x0, double *y0, double *x1, double *y1)
{
    long i;

    *x0 = 1e30;
    *y0 = 1e30;
    *x1 = -1e30;
    *y1 = -1e30;

    for (i = 0; i < q->n; i++) {
        if (q->x[i] < *x0) {
            *x0 = q->x[i];
        }

        if (q->x[i] > *x1) {
            *x1 = q->x[i];
        }

        if (q->y[i] < *y0) {
            *y0 = q->y[i];
        }

        if (q->y[i] > *y1) {
            *y1 = q->y[i];
        }
    }
}


typedef struct {
    double x;
    int dir;
} Cross;


static int
cross_cmp(const void *a, const void *b)
{
    const Cross *ca = a;
    const Cross *cb = b;

    if (ca->x < cb->x) {
        return -1;
    }

    return ca->x > cb->x ? 1 : 0;
}


/* Walks the crossings of one sub-row and adds the covered spans. */
static void
add_spans(const Cross *cr, long n, int evenodd, const long *bx, double *acc,
          double *dacc)
{
    long i;
    int wind = 0;

    for (i = 0; i + 1 < n; i++) {
        int inside;
        double xa;
        double xb;
        long ia;
        long ib;

        wind += evenodd ? 1 : cr[i].dir;
        inside = evenodd ? (wind % 2 != 0) : (wind != 0);

        if (!inside) {
            continue;
        }

        xa = cr[i].x;
        xb = cr[i + 1].x;

        if (xa < dbl(bx[0])) {
            xa = dbl(bx[0]);
        }

        if (xb > dbl(bx[1])) {
            xb = dbl(bx[1]);
        }

        if (xb <= xa) {
            continue;
        }

        ia = floor_long(xa);
        ib = floor_long(xb);

        if (ia == ib) {
            acc[ia - bx[0]] += xb - xa;
        } else {
            acc[ia - bx[0]] += dbl(ia + 1) - xa;
            acc[ib - bx[0]] += xb - dbl(ib);
            dacc[ia + 1 - bx[0]] += 1.0;
            dacc[ib - bx[0]] -= 1.0;
        }
    }
}


/* Records the crossing of one edge with the sub-rows; pass 0 only counts. */
static void
edge_cross(double x0, double y0, double x1, double y1, long sy0, long nsub,
           long *cnt, Cross *cr, int pass)
{
    int dir = 1;
    long k0;
    long k1;
    long k;

    if (fabs(y0 - y1) < 1e-12) {
        return;
    }

    if (y0 > y1) {
        double t = x0;

        x0 = x1;
        x1 = t;
        t = y0;
        y0 = y1;
        y1 = t;
        dir = -1;
    }

    k0 = ceil_long(y0 * dbl(SUBROWS) - 0.5);
    k1 = ceil_long(y1 * dbl(SUBROWS) - 0.5);

    if (k0 < sy0) {
        k0 = sy0;
    }

    if (k1 > sy0 + nsub) {
        k1 = sy0 + nsub;
    }

    for (k = k0; k < k1; k++) {
        double ys = (dbl(k) + 0.5) / dbl(SUBROWS);
        long row = k - sy0;

        if (pass == 0) {
            cnt[row + 1]++;
        } else {
            long at = cnt[row] + cnt[nsub + 2 + row];

            cr[at].x = x0 + (ys - y0) * (x1 - x0) / (y1 - y0);
            cr[at].dir = dir;
            cnt[nsub + 2 + row]++;
        }
    }
}


/* The scan converter proper: fills the contours into a coverage mask. */
static Cov *
fill_poly(const Poly *q, int evenodd, const long *bounds)
{
    double fx[4];
    long b[4];
    long rows;
    long nsub;
    long *cnt;
    Cross *cr;
    long c;
    long r;
    long s;
    Cov *cov;
    double *acc;
    double *dacc;
    long bx[2];

    if (q->n < 2) {
        return NULL;
    }

    bbox_of(q, &fx[0], &fx[1], &fx[2], &fx[3]);
    b[0] = floor_long(fx[0]);
    b[1] = floor_long(fx[1]);
    b[2] = ceil_long(fx[2]) + 1;
    b[3] = ceil_long(fx[3]) + 1;
    b[0] = b[0] < bounds[0] ? bounds[0] : b[0];
    b[1] = b[1] < bounds[1] ? bounds[1] : b[1];
    b[2] = b[2] > bounds[2] ? bounds[2] : b[2];
    b[3] = b[3] > bounds[3] ? bounds[3] : b[3];

    if (b[2] <= b[0] || b[3] <= b[1]) {
        return NULL;
    }

    rows = b[3] - b[1];
    nsub = rows * SUBROWS;
    cnt = xcalloc(to_size(2 * nsub + 4), sizeof *cnt);

    for (c = 0; c < q->nc; c++) {
        long i;
        long e = poly_end(q, c);

        for (i = q->start[c]; i < e; i++) {
            long j = i + 1 < e ? i + 1 : q->start[c];

            if (e - q->start[c] < 2) {
                break;
            }

            edge_cross(q->x[i], q->y[i], q->x[j], q->y[j], b[1] * SUBROWS,
                       nsub, cnt, NULL, 0);
        }
    }

    /* Turn the counts into start offsets; the second half counts again. */
    for (r = 0; r < nsub; r++) {
        cnt[r + 1] += cnt[r];
    }

    cr = xcalloc(to_size(cnt[nsub]) + 1, sizeof *cr);

    for (c = 0; c < q->nc; c++) {
        long i;
        long e = poly_end(q, c);

        for (i = q->start[c]; i < e; i++) {
            long j = i + 1 < e ? i + 1 : q->start[c];

            if (e - q->start[c] < 2) {
                break;
            }

            edge_cross(q->x[i], q->y[i], q->x[j], q->y[j], b[1] * SUBROWS,
                       nsub, cnt, cr, 1);
        }
    }

    cov = xcalloc(1, sizeof *cov);
    cov->x0 = b[0];
    cov->y0 = b[1];
    cov->w = b[2] - b[0];
    cov->h = rows;
    cov->a = xcalloc(to_size(cov->w * cov->h), 1);
    acc = xcalloc(to_size(cov->w + 2), sizeof *acc);
    dacc = xcalloc(to_size(cov->w + 2), sizeof *dacc);
    bx[0] = b[0];
    bx[1] = b[2];

    for (r = 0; r < rows; r++) {
        double run = 0.0;
        long x;

        memset(acc, 0, to_size(cov->w + 2) * sizeof *acc);
        memset(dacc, 0, to_size(cov->w + 2) * sizeof *dacc);

        for (s = 0; s < SUBROWS; s++) {
            long k = r * SUBROWS + s;
            long n = cnt[k + 1] - cnt[k];

            if (n > 1) {
                qsort(cr + cnt[k], to_size(n), sizeof *cr, cross_cmp);
                add_spans(cr + cnt[k], n, evenodd, bx, acc, dacc);
            }
        }

        for (x = 0; x < cov->w; x++) {
            double v;

            run += dacc[x];
            v = (acc[x] + run) * 255.0 / dbl(SUBROWS);
            cov->a[r * cov->w + x] = to_u8(round_long(v));
        }
    }

    free(acc);
    free(dacc);
    free(cr);
    free(cnt);

    return cov;
}


void
cov_free(Cov *c)
{
    if (c != NULL) {
        free(c->a);
        free(c);
    }
}


Cov *
raster_fill(const Path *p, const Mat *view, int evenodd, const long *bounds)
{
    Poly q;
    Cov *c;

    memset(&q, 0, sizeof q);
    flatten(p, view, &q);
    c = fill_poly(&q, evenodd, bounds);
    poly_free(&q);

    return c;
}


/* Adds a polygon with positive orientation, so that unions stay solid. */
static void
add_poly(Poly *out, const double *x, const double *y, long n)
{
    double area = 0.0;
    long i;

    for (i = 0; i < n; i++) {
        long j = (i + 1) % n;

        area += x[i] * y[j] - x[j] * y[i];
    }

    poly_begin(out);

    for (i = 0; i < n; i++) {
        long k = area >= 0.0 ? i : n - 1 - i;

        poly_pt(out, x[k], y[k]);
    }

    out->closed[out->nc - 1] = 1;
}


static void
add_circle(Poly *out, double cx, double cy, double r)
{
    double x[24];
    double y[24];
    long i;
    long n = r > 6.0 ? 24 : 12;

    for (i = 0; i < n; i++) {
        double t = 2.0 * PI_VALUE * dbl(i) / dbl(n);

        x[i] = cx + r * cos(t);
        y[i] = cy + r * sin(t);
    }

    add_poly(out, x, y, n);
}


/* The quad of one segment, with the square cap extension when asked. */
static void
add_segment(Poly *out, double x0, double y0, double x1, double y1, double hw,
            int ext0, int ext1)
{
    double dx = x1 - x0;
    double dy = y1 - y0;
    double len = hypot(dx, dy);
    double ux;
    double uy;
    double x[4];
    double y[4];

    if (len < 1e-9) {
        return;
    }

    ux = dx / len;
    uy = dy / len;

    if (ext0) {
        x0 -= ux * hw;
        y0 -= uy * hw;
    }

    if (ext1) {
        x1 += ux * hw;
        y1 += uy * hw;
    }

    x[0] = x0 - uy * hw;
    y[0] = y0 + ux * hw;
    x[1] = x1 - uy * hw;
    y[1] = y1 + ux * hw;
    x[2] = x1 + uy * hw;
    y[2] = y1 - ux * hw;
    x[3] = x0 + uy * hw;
    y[3] = y0 - ux * hw;
    add_poly(out, x, y, 4);
}


/* The join at the point (x, y) between two directions. */
static void
add_join(Poly *out, double x, double y, const double *d1, const double *d2,
         double hw, int join, double miter)
{
    double cross = d1[0] * d2[1] - d1[1] * d2[0];
    double dot = d1[0] * d2[0] + d1[1] * d2[1];
    double side = cross > 0.0 ? -1.0 : 1.0;
    double n1x = -d1[1] * side * hw;
    double n1y = d1[0] * side * hw;
    double n2x = -d2[1] * side * hw;
    double n2y = d2[0] * side * hw;
    double px[4];
    double py[4];

    if (join == 1) {
        add_circle(out, x, y, hw);
        return;
    }

    if (fabs(cross) < 1e-9 && dot > 0.0) {
        return;
    }

    px[0] = x;
    py[0] = y;
    px[1] = x + n1x;
    py[1] = y + n1y;

    if (join == 0 && dot > -0.999999) {
        /* The miter length over the width is 1 / sin(half the angle). */
        double half = sqrt((1.0 + dot) / 2.0);

        if (half > 1e-9 && 1.0 / half <= miter) {
            double mx = n1x + n2x;
            double my = n1y + n2y;
            double ml = hypot(mx, my);

            if (ml > 1e-9) {
                double k = hw / half / ml;

                px[2] = x + mx * k;
                py[2] = y + my * k;
                px[3] = x + n2x;
                py[3] = y + n2y;
                add_poly(out, px, py, 4);

                return;
            }
        }
    }

    px[2] = x + n2x;
    py[2] = y + n2y;
    add_poly(out, px, py, 3);
}


/* Strokes one polyline (already dashed) into polygons. */
static void
stroke_line(Poly *out, const double *x, const double *y, long n, int closed,
            double hw, const StrokeStyle *st)
{
    long i;
    long nseg = closed ? n : n - 1;

    if (n < 2) {
        if (n == 1 && st->cap == 1) {
            add_circle(out, x[0], y[0], hw);
        }

        return;
    }

    for (i = 0; i < nseg; i++) {
        long j = (i + 1) % n;
        int e0 = !closed && i == 0 && st->cap == 2;
        int e1 = !closed && i == nseg - 1 && st->cap == 2;

        add_segment(out, x[i], y[i], x[j], y[j], hw, e0, e1);
    }

    if (!closed && st->cap == 1) {
        add_circle(out, x[0], y[0], hw);
        add_circle(out, x[n - 1], y[n - 1], hw);
    }

    for (i = closed ? 0 : 1; i < (closed ? n : n - 1); i++) {
        long p = (i + n - 1) % n;
        long k = (i + 1) % n;
        double d1[2];
        double d2[2];
        double l1 = hypot(x[i] - x[p], y[i] - y[p]);
        double l2 = hypot(x[k] - x[i], y[k] - y[i]);

        if (l1 < 1e-9 || l2 < 1e-9) {
            continue;
        }

        d1[0] = (x[i] - x[p]) / l1;
        d1[1] = (y[i] - y[p]) / l1;
        d2[0] = (x[k] - x[i]) / l2;
        d2[1] = (y[k] - y[i]) / l2;
        add_join(out, x[i], y[i], d1, d2, hw, st->join, st->miter);
    }
}


/* Splits a polyline by the dash pattern and strokes the pieces. */
static void
stroke_dashed(Poly *out, const double *x, const double *y, long n, int closed,
              double hw, const StrokeStyle *st, double scale)
{
    double *px;
    double *py;
    long np = 0;
    long i;
    long nseg = closed ? n : n - 1;
    long di = 0;
    double total = 0.0;
    double left;
    int on = 1;
    double phase;
    long guard = 0;

    for (i = 0; i < st->ndash; i++) {
        total += st->dash[i] * scale;
    }

    if (st->ndash == 0 || total < 0.2) {
        stroke_line(out, x, y, n, closed, hw, st);
        return;
    }

    px = xcalloc(to_size(n + 2), sizeof *px);
    py = xcalloc(to_size(n + 2), sizeof *py);
    phase = fmod(st->phase * scale, total * (st->ndash % 2 != 0 ? 2.0 : 1.0));

    if (phase < 0.0) {
        phase += total * 2.0;
    }

    left = st->dash[0] * scale;

    while (phase > 0.0) {
        if (phase >= left) {
            phase -= left;
            di = (di + 1) % st->ndash;
            on = !on;
            left = st->dash[di] * scale;
        } else {
            left -= phase;
            phase = 0.0;
        }
    }

    if (on) {
        px[0] = x[0];
        py[0] = y[0];
        np = 1;
    }

    for (i = 0; i < nseg; i++) {
        long j = (i + 1) % n;
        double sx = x[i];
        double sy = y[i];
        double len = hypot(x[j] - sx, y[j] - sy);
        double done = 0.0;

        while (len - done > left && guard < 200000) {
            double t;
            double mx;
            double my;

            guard++;

            done += left;
            t = len > 0.0 ? done / len : 0.0;
            mx = sx + (x[j] - sx) * t;
            my = sy + (y[j] - sy) * t;

            if (on) {
                px[np] = mx;
                py[np] = my;
                np++;
                stroke_line(out, px, py, np, 0, hw, st);
                np = 0;
            } else {
                px[0] = mx;
                py[0] = my;
                np = 1;
            }

            on = !on;
            di = (di + 1) % st->ndash;
            left = st->dash[di] * scale;

            if (left < 1e-6) {
                left = 1e-6;
            }
        }

        left -= len - done;

        if (on) {
            px[np] = x[j];
            py[np] = y[j];
            np++;
        }
    }

    if (on && np > 1) {
        stroke_line(out, px, py, np, 0, hw, st);
    }

    free(px);
    free(py);
}


Cov *
raster_stroke(const Path *p, const Mat *view, const StrokeStyle *st,
              const long *bounds)
{
    Poly q;
    Poly out;
    Cov *c;
    double scale = mat_det_scale(view);
    double lw = st->lw * scale;
    long k;
    long wide[4];

    if (lw < 1.0) {
        lw = 1.0;
    }

    memset(&q, 0, sizeof q);
    memset(&out, 0, sizeof out);
    flatten(p, view, &q);

    for (k = 0; k < q.nc; k++) {
        long a = q.start[k];
        long n = poly_end(&q, k) - a;
        int closed = q.closed[k];

        if (closed && n > 1 && fabs(q.x[a] - q.x[a + n - 1]) < 1e-9 &&
            fabs(q.y[a] - q.y[a + n - 1]) < 1e-9) {
            n--;
        }

        stroke_dashed(&out, q.x + a, q.y + a, n, closed, lw / 2.0, st,
                      scale);
    }

    wide[0] = bounds[0];
    wide[1] = bounds[1];
    wide[2] = bounds[2];
    wide[3] = bounds[3];
    c = fill_poly(&out, 0, wide);
    poly_free(&q);
    poly_free(&out);

    return c;
}
