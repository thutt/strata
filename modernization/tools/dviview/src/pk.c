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
 * Reading of PK bitmap fonts and scaling of their glyphs.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dviview.h"

typedef struct {
    const unsigned char *p;
    long pos;
    long dyn_f;
    long repeat;
} Nyb;


static long
get_nyb(Nyb *s)
{
    long b = s->p[s->pos / 2];
    long r;

    if (s->pos % 2 == 0) {
        r = b / 16;
    } else {
        r = b % 16;
    }

    s->pos++;

    return r;
}


static long
packed(Nyb *s)
{
    long i = get_nyb(s);
    long j;

    if (i == 0) {
        do {
            j = get_nyb(s);
            i++;
        } while (j == 0 && i < 20);

        while (i > 0) {
            j = j * 16 + get_nyb(s);
            i--;
        }

        return j - 15 + (13 - s->dyn_f) * 16 + s->dyn_f;
    }

    if (i <= s->dyn_f) {
        return i;
    }

    if (i < 14) {
        return (i - s->dyn_f - 1) * 16 + get_nyb(s) + s->dyn_f + 1;
    }

    if (i == 14) {
        s->repeat = packed(s);
    } else {
        s->repeat = 1;
    }

    return packed(s);
}


/* Decodes one character raster into one byte per pixel (0 or 1). */
static void
decode(PkChar *c, const unsigned char *data, long dyn_f, int turn_on)
{
    long w = c->w;
    long h = c->h;
    long total = w * h;
    long i;

    c->bits = xcalloc(to_size(total > 0 ? total : 1), 1);

    if (dyn_f == 14) {
        for (i = 0; i < total; i++) {
            c->bits[i] = (data[i / 8] >> (7 - i % 8)) & 1;
        }

        return;
    }

    {
        Nyb s;
        long x = 0;
        long y = 0;
        int color = turn_on;

        s.p = data;
        s.pos = 0;
        s.dyn_f = dyn_f;
        s.repeat = 0;

        while (y < h && w > 0) {
            long count = packed(&s);

            while (count > 0 && y < h) {
                long n = w - x;

                if (n > count) {
                    n = count;
                }

                if (color != 0) {
                    memset(c->bits + y * w + x, 1, to_size(n));
                }

                x += n;
                count -= n;

                if (x == w) {
                    long r;

                    for (r = 1; r <= s.repeat && y + r < h; r++) {
                        memcpy(c->bits + (y + r) * w, c->bits + y * w,
                               to_size(w));
                    }

                    y += 1 + s.repeat;
                    s.repeat = 0;
                    x = 0;
                }
            }

            color = !color;
        }
    }
}


Pk *
pk_load(const char *path)
{
    size_t len;
    unsigned char *d = read_file(path, &len);
    Pk *pk;
    long pos;
    long cap = 0;

    if (d == NULL || len < 15 || d[0] != 247 || d[1] != 89) {
        free(d);
        return NULL;
    }

    pk = xcalloc(1, sizeof *pk);
    pos = 3 + d[2] + 16;

    while (pos < to_long(len)) {
        long f = d[pos];
        long plen;
        long cc;
        long w;
        long h;
        long hoff;
        long voff;
        long hdr;
        long lf;
        const unsigned char *q;

        if (f == 244) {
            pos += 5;
            continue;
        }

        if (f == 246) {
            pos++;
            continue;
        }

        if (f == 245 || f == 247) {
            break;
        }

        if (f >= 240 && f <= 243) {
            long nb = f - 239;

            pos += 1 + nb + get_u(d + pos + 1, to_int(nb));
            continue;
        }

        q = d + pos + 1;

        if (f % 8 < 4) {
            lf = 1;
            plen = (f % 4) * 256 + q[0];
            cc = q[1];
            w = q[6];
            h = q[7];
            hoff = get_s(q + 8, 1);
            voff = get_s(q + 9, 1);
            hdr = 10;
        } else if (f % 8 < 7) {
            lf = 2;
            plen = (f % 4) * 65536 + get_u(q, 2);
            cc = q[2];
            w = get_u(q + 8, 2);
            h = get_u(q + 10, 2);
            hoff = get_s(q + 12, 2);
            voff = get_s(q + 14, 2);
            hdr = 16;
        } else {
            lf = 4;
            plen = get_u(q, 4);
            cc = get_u(q + 4, 4);
            w = get_u(q + 20, 4);
            h = get_u(q + 24, 4);
            hoff = get_s(q + 28, 4);
            voff = get_s(q + 32, 4);
            hdr = 36;
        }

        if (pk->n >= cap) {
            cap = cap * 2 + 64;
            pk->chars = xrealloc(pk->chars,
                                 to_size(cap) * sizeof *pk->chars);
        }

        if (pos + 1 + hdr > to_long(len)) {
            break;
        }

        {
            PkChar *c = &pk->chars[pk->n];

            c->ch = cc;
            c->w = w;
            c->h = h;
            c->hoff = hoff;
            c->voff = voff;
            decode(c, q + hdr, f / 16, (f & 8) != 0);
            pk->n++;
        }

        /* The length counts the bytes that follow the character code. */
        pos += 1 + lf + (lf == 4 ? 4 : 1) + plen;
    }

    free(d);

    return pk;
}


/* Scales a PK character to the target resolution by area averaging. */
Glyph *
pk_glyph(const Pk *pk, long ch, double ratio)
{
    long i;
    Glyph *g;

    for (i = 0; i < pk->n; i++) {
        const PkChar *c = &pk->chars[i];
        long x0;
        long y0;
        long x1;
        long y1;
        long gx;
        long gy;

        if (c->ch != ch) {
            continue;
        }

        g = xcalloc(1, sizeof *g);
        x0 = floor_long(-dbl(c->hoff) * ratio);
        x1 = ceil_long(dbl(c->w - c->hoff) * ratio);
        y0 = floor_long(-dbl(c->voff) * ratio);
        y1 = ceil_long(dbl(c->h - c->voff) * ratio);
        g->left = x0;
        g->top = -y0;
        g->w = x1 - x0;
        g->h = y1 - y0;

        if (g->w <= 0 || g->h <= 0 || g->w > 4000 || g->h > 4000) {
            g->w = 0;
            g->h = 0;
            return g;
        }

        g->a = xcalloc(to_size(g->w * g->h), 1);

        for (gy = 0; gy < g->h; gy++) {
            for (gx = 0; gx < g->w; gx++) {
                double sx0 = dbl(x0 + gx) / ratio + dbl(c->hoff);
                double sx1 = dbl(x0 + gx + 1) / ratio + dbl(c->hoff);
                double sy0 = dbl(y0 + gy) / ratio + dbl(c->voff);
                double sy1 = dbl(y0 + gy + 1) / ratio + dbl(c->voff);
                double sum = 0.0;
                double area = 0.0;
                long ix;
                long iy;

                for (iy = floor_long(sy0); iy < ceil_long(sy1); iy++) {
                    double wy = fmin(sy1, dbl(iy + 1)) - fmax(sy0, dbl(iy));

                    for (ix = floor_long(sx0); ix < ceil_long(sx1); ix++) {
                        double wx = fmin(sx1, dbl(ix + 1)) -
                                    fmax(sx0, dbl(ix));
                        double wgt = wx * wy;

                        area += wgt;

                        if (ix >= 0 && ix < c->w && iy >= 0 && iy < c->h &&
                            c->bits[iy * c->w + ix] != 0) {
                            sum += wgt;
                        }
                    }
                }

                if (area > 0.0) {
                    g->a[gy * g->w + gx] = to_u8(round_long(255.0 * sum /
                                                            area));
                }
            }
        }

        return g;
    }

    return NULL;
}
