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
 * A JPEG decoder: baseline, extended sequential and progressive Huffman
 * coding, 8 bits, one, three or four components, restart intervals, any
 * sampling factors.  libjpeg is not used because its header files are not
 * installed on the build machine.  Arithmetic coding, 12 bit samples and
 * lossless JPEG are not supported.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "dviview.h"

#define PI_VALUE 3.14159265358979323846

typedef struct {
    int present;
    long maxcode[18];
    long valptr[17];
    long mincode[17];
    unsigned char vals[256];
    int fast_ok;
} Huff;

typedef struct {
    int id;
    int h;
    int v;
    int tq;
    long bw;
    long bh;
    long wblocks;
    long hblocks;
    int *coef;
    int dc_pred;
    int td;
    int ta;
} Comp;

typedef struct {
    const unsigned char *d;
    size_t len;
    size_t pos;
    unsigned long bits;
    int nbits;
    int marker;
    long eobrun;
    Huff dc[4];
    Huff ac[4];
    int qt[4][64];
    Comp comp[4];
    int ncomp;
    long width;
    long height;
    int hmax;
    int vmax;
    int progressive;
    int restart;
    int adobe;
    int adobe_transform;
    double dpi;
    long mcux;
    long mcuy;
} Jpg;

static const int zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33,
    40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43,
    36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53,
    60, 61, 54, 47, 55, 62, 63
};


static long
rd16(const unsigned char *p)
{
    return p[0] * 256L + p[1];
}


/* Builds the decoding tables of a Huffman table from its code lengths. */
static void
huff_build(Huff *h, const unsigned char *counts)
{
    long code = 0;
    long k = 0;
    int l;

    for (l = 1; l <= 16; l++) {
        h->valptr[l] = k;
        h->mincode[l] = code;
        code += counts[l - 1];
        k += counts[l - 1];
        h->maxcode[l] = counts[l - 1] > 0 ? code - 1 : -1;
        code *= 2;
    }

    h->maxcode[17] = 0x7fffffffL;
    h->present = 1;
}


static int
fill_bits(Jpg *j)
{
    while (j->nbits <= 24) {
        unsigned long b = 0;

        if (j->marker == 0 && j->pos < j->len) {
            b = j->d[j->pos++];

            if (b == 0xff) {
                unsigned char n = j->pos < j->len ? j->d[j->pos] : 0xd9;

                if (n == 0) {
                    j->pos++;
                } else {
                    j->marker = n;
                    j->pos--;
                    b = 0;
                }
            }
        }

        j->bits |= b << (24 - j->nbits);
        j->nbits += 8;
    }

    return 0;
}


static int
get_bits(Jpg *j, int n)
{
    int v;

    if (n == 0) {
        return 0;
    }

    if (j->nbits < n) {
        fill_bits(j);
    }

    v = to_int(to_long(j->bits >> (32 - n)));
    j->bits = (j->bits << n) & 0xffffffffUL;
    j->nbits -= n;

    return v;
}


static int
decode_huff(Jpg *j, const Huff *h)
{
    long code = 0;
    int l;

    for (l = 1; l <= 16; l++) {
        code = code * 2 + get_bits(j, 1);

        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] &&
            code >= h->mincode[l]) {
            long idx = h->valptr[l] + code - h->mincode[l];

            return idx >= 0 && idx < 256 ? h->vals[idx] : 0;
        }
    }

    return 0;
}


/* Reads n bits and extends the sign as the standard describes. */
static int
receive_extend(Jpg *j, int n)
{
    int v;

    if (n == 0) {
        return 0;
    }

    v = get_bits(j, n);

    if (v < (1 << (n - 1))) {
        v += 1 - (1 << n);
    }

    return v;
}


static void
decode_block_seq(Jpg *j, Comp *c, int *blk)
{
    int t = decode_huff(j, &j->dc[c->td]);
    int diff = t > 0 ? receive_extend(j, t) : 0;
    int k;

    c->dc_pred += diff;
    blk[0] = c->dc_pred;

    for (k = 1; k < 64; k++) {
        int rs = decode_huff(j, &j->ac[c->ta]);
        int r = rs >> 4;
        int s = rs & 15;

        if (s == 0) {
            if (r != 15) {
                break;
            }

            k += 15;
            continue;
        }

        k += r;

        if (k > 63) {
            break;
        }

        blk[zigzag[k]] = receive_extend(j, s);
    }
}


static void
decode_dc_first(Jpg *j, Comp *c, int *blk, int al)
{
    int t = decode_huff(j, &j->dc[c->td]);
    int diff = t > 0 ? receive_extend(j, t) : 0;

    c->dc_pred += diff;
    blk[0] = c->dc_pred * (1 << al);
}


static void
decode_dc_refine(Jpg *j, int *blk, int al)
{
    if (get_bits(j, 1) != 0) {
        blk[0] |= 1 << al;
    }
}


static void
decode_ac_first(Jpg *j, Comp *c, int *blk, int ss, int se, int al)
{
    int k;

    if (j->eobrun > 0) {
        j->eobrun--;
        return;
    }

    for (k = ss; k <= se; k++) {
        int rs = decode_huff(j, &j->ac[c->ta]);
        int r = rs >> 4;
        int s = rs & 15;

        if (s == 0) {
            if (r < 15) {
                j->eobrun = (1L << r) - 1;

                if (r > 0) {
                    j->eobrun += get_bits(j, r);
                }

                break;
            }

            k += 15;
            continue;
        }

        k += r;

        if (k > 63) {
            break;
        }

        blk[zigzag[k]] = receive_extend(j, s) * (1 << al);
    }
}


/* Refines the already known coefficient of the position idx by one bit. */
static void
refine_one(Jpg *j, int *blk, long idx, int p1, int m1)
{
    if (blk[idx] != 0 && get_bits(j, 1) != 0) {
        if ((blk[idx] & p1) == 0) {
            blk[idx] += blk[idx] >= 0 ? p1 : m1;
        }
    }
}


static void
decode_ac_refine(Jpg *j, Comp *c, int *blk, int ss, int se, int al)
{
    int p1 = 1 << al;
    int m1 = -1 * p1;
    int k = ss;

    if (j->eobrun <= 0) {
        for (; k <= se; k++) {
            int rs = decode_huff(j, &j->ac[c->ta]);
            int r = rs >> 4;
            int s = rs & 15;
            int val = 0;

            if (s != 0) {
                val = get_bits(j, 1) != 0 ? p1 : m1;
            } else if (r != 15) {
                j->eobrun = 1L << r;

                if (r > 0) {
                    j->eobrun += get_bits(j, r);
                }

                break;
            }

            while (k <= se) {
                long idx = zigzag[k];

                if (blk[idx] != 0) {
                    refine_one(j, blk, idx, p1, m1);
                } else {
                    if (r == 0) {
                        break;
                    }

                    r--;
                }

                k++;
            }

            if (val != 0 && k <= 63) {
                blk[zigzag[k]] = val;
            }
        }
    }

    if (j->eobrun > 0) {
        for (; k <= se; k++) {
            refine_one(j, blk, zigzag[k], p1, m1);
        }

        j->eobrun--;
    }
}


/* Skips to the next restart marker and resets the decoder state. */
static void
do_restart(Jpg *j)
{
    int i;

    j->bits = 0;
    j->nbits = 0;

    while (j->pos + 1 < j->len &&
           !(j->d[j->pos] == 0xff && j->d[j->pos + 1] >= 0xd0 &&
             j->d[j->pos + 1] <= 0xd7)) {
        if (j->d[j->pos] == 0xff && j->d[j->pos + 1] != 0 &&
            j->d[j->pos + 1] != 0xff) {
            break;
        }

        j->pos++;
    }

    if (j->pos + 1 < j->len && j->d[j->pos + 1] >= 0xd0 &&
        j->d[j->pos + 1] <= 0xd7) {
        j->pos += 2;
    }

    j->marker = 0;
    j->eobrun = 0;

    for (i = 0; i < 4; i++) {
        j->comp[i].dc_pred = 0;
    }
}


typedef struct {
    int ncs;
    int idx[4];
    int ss;
    int se;
    int ah;
    int al;
} Scan;


static void
decode_one(Jpg *j, Comp *c, int *blk, const Scan *sc)
{
    if (!j->progressive) {
        decode_block_seq(j, c, blk);
    } else if (sc->ss == 0) {
        if (sc->ah == 0) {
            decode_dc_first(j, c, blk, sc->al);
        } else {
            decode_dc_refine(j, blk, sc->al);
        }
    } else if (sc->ah == 0) {
        decode_ac_first(j, c, blk, sc->ss, sc->se, sc->al);
    } else {
        decode_ac_refine(j, c, blk, sc->ss, sc->se, sc->al);
    }
}


static void
decode_scan(Jpg *j, const Scan *sc)
{
    long total;
    long n;
    long since = 0;
    int i;

    for (i = 0; i < 4; i++) {
        j->comp[i].dc_pred = 0;
    }

    j->bits = 0;
    j->nbits = 0;
    j->marker = 0;
    j->eobrun = 0;

    if (sc->ncs == 1) {
        Comp *c = &j->comp[sc->idx[0]];
        long bx = (j->width * c->h + j->hmax * 8L - 1) / (j->hmax * 8L);
        long by = (j->height * c->v + j->vmax * 8L - 1) / (j->vmax * 8L);

        total = bx * by;

        for (n = 0; n < total; n++) {
            long row = n / bx;
            long col = n % bx;

            if (j->restart > 0 && since == j->restart) {
                do_restart(j);
                since = 0;
            }

            decode_one(j, c, c->coef + (row * c->wblocks + col) * 64, sc);
            since++;
        }

        return;
    }

    total = j->mcux * j->mcuy;

    for (n = 0; n < total; n++) {
        long mr = n / j->mcux;
        long mc = n % j->mcux;
        int k;

        if (j->restart > 0 && since == j->restart) {
            do_restart(j);
            since = 0;
        }

        for (k = 0; k < sc->ncs; k++) {
            Comp *c = &j->comp[sc->idx[k]];
            int by;
            int bx;

            for (by = 0; by < c->v; by++) {
                for (bx = 0; bx < c->h; bx++) {
                    long row = mr * c->v + by;
                    long col = mc * c->h + bx;

                    decode_one(j, c, c->coef + (row * c->wblocks + col) * 64,
                               sc);
                }
            }
        }

        since++;
    }
}


static int
parse_sof(Jpg *j, const unsigned char *p, long len)
{
    int i;

    if (len < 6 || p[0] != 8) {
        return -1;
    }

    j->height = rd16(p + 1);
    j->width = rd16(p + 3);
    j->ncomp = p[5];

    if (j->width <= 0 || j->height <= 0 || j->ncomp < 1 || j->ncomp > 4 ||
        j->width * j->height > 40000000L || len < 6 + 3 * j->ncomp) {
        return -1;
    }

    for (i = 0; i < j->ncomp; i++) {
        Comp *c = &j->comp[i];

        c->id = p[6 + 3 * i];
        c->h = p[7 + 3 * i] >> 4;
        c->v = p[7 + 3 * i] & 15;
        c->tq = p[8 + 3 * i] & 3;

        if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4) {
            return -1;
        }

        j->hmax = c->h > j->hmax ? c->h : j->hmax;
        j->vmax = c->v > j->vmax ? c->v : j->vmax;
    }

    j->mcux = (j->width + 8L * j->hmax - 1) / (8L * j->hmax);
    j->mcuy = (j->height + 8L * j->vmax - 1) / (8L * j->vmax);

    for (i = 0; i < j->ncomp; i++) {
        Comp *c = &j->comp[i];

        c->wblocks = j->mcux * c->h;
        c->hblocks = j->mcuy * c->v;
        c->coef = xcalloc(to_size(c->wblocks * c->hblocks * 64), sizeof(int));
    }

    return 0;
}


static void
parse_dht(Jpg *j, const unsigned char *p, long len)
{
    long pos = 0;

    while (pos + 17 <= len) {
        int tc = p[pos] >> 4;
        int th = p[pos] & 3;
        long total = 0;
        int i;
        Huff *h = tc == 0 ? &j->dc[th] : &j->ac[th];

        for (i = 0; i < 16; i++) {
            total += p[pos + 1 + i];
        }

        if (total > 256 || pos + 17 + total > len) {
            return;
        }

        memset(h, 0, sizeof *h);
        memcpy(h->vals, p + pos + 17, to_size(total));
        huff_build(h, p + pos + 1);
        pos += 17 + total;
    }
}


static void
parse_dqt(Jpg *j, const unsigned char *p, long len)
{
    long pos = 0;

    while (pos < len) {
        int pq = p[pos] >> 4;
        int tq = p[pos] & 3;
        int i;

        pos++;

        if (pos + (pq != 0 ? 128 : 64) > len) {
            return;
        }

        for (i = 0; i < 64; i++) {
            j->qt[tq][zigzag[i]] = pq != 0 ? to_int(rd16(p + pos + 2 * i)) :
                                   p[pos + i];
        }

        pos += pq != 0 ? 128 : 64;
    }
}


static void
parse_app(Jpg *j, int marker, const unsigned char *p, long len)
{
    if (marker == 0xe0 && len >= 14 && memcmp(p, "JFIF", 4) == 0) {
        long dx = rd16(p + 8);

        if (p[7] == 1 && dx > 0) {
            j->dpi = dbl(dx);
        } else if (p[7] == 2 && dx > 0) {
            j->dpi = dbl(dx) * 2.54;
        }
    } else if (marker == 0xee && len >= 12 && memcmp(p, "Adobe", 5) == 0) {
        j->adobe = 1;
        j->adobe_transform = p[11];
    }
}


static int
parse_sos(Jpg *j, const unsigned char *p, long len, Scan *sc)
{
    int i;

    if (len < 1) {
        return -1;
    }

    sc->ncs = p[0];

    if (sc->ncs < 1 || sc->ncs > 4 || len < 1 + 2 * sc->ncs + 3) {
        return -1;
    }

    for (i = 0; i < sc->ncs; i++) {
        int cid = p[1 + 2 * i];
        int k;
        int found = -1;

        for (k = 0; k < j->ncomp; k++) {
            if (j->comp[k].id == cid) {
                found = k;
            }
        }

        if (found < 0) {
            return -1;
        }

        sc->idx[i] = found;
        j->comp[found].td = p[2 + 2 * i] >> 4 & 3;
        j->comp[found].ta = p[2 + 2 * i] & 3;
    }

    sc->ss = p[1 + 2 * sc->ncs];
    sc->se = p[2 + 2 * sc->ncs];
    sc->ah = p[3 + 2 * sc->ncs] >> 4;
    sc->al = p[3 + 2 * sc->ncs] & 15;

    if (!j->progressive) {
        sc->ss = 0;
        sc->se = 63;
    }

    if (sc->se > 63 || sc->ss > sc->se) {
        return -1;
    }

    return 0;
}


/* The inverse DCT of one block into 8x8 samples. */
static void
idct_block(const int *coef, const int *q, const double *cs,
           unsigned char *out, long stride)
{
    double tmp[64];
    int x;
    int y;
    int u;
    int v;

    for (v = 0; v < 8; v++) {
        for (x = 0; x < 8; x++) {
            double s = 0.0;

            for (u = 0; u < 8; u++) {
                s += cs[x * 8 + u] * dbl(coef[v * 8 + u] * q[v * 8 + u]);
            }

            tmp[v * 8 + x] = s;
        }
    }

    for (x = 0; x < 8; x++) {
        for (y = 0; y < 8; y++) {
            double s = 0.0;
            long val;

            for (v = 0; v < 8; v++) {
                s += cs[y * 8 + v] * tmp[v * 8 + x];
            }

            val = round_long(s / 4.0 + 128.0);
            out[y * stride + x] = to_u8(val);
        }
    }
}


/* Makes the full plane of one component from its coefficients. */
static unsigned char *
make_plane(const Jpg *j, const Comp *c)
{
    long pw = c->wblocks * 8;
    long ph = c->hblocks * 8;
    unsigned char *pl = xcalloc(to_size(pw * ph), 1);
    double cs[64];
    long by;
    long bx;
    int x;
    int u;

    for (x = 0; x < 8; x++) {
        for (u = 0; u < 8; u++) {
            double cu = u == 0 ? 1.0 / sqrt(2.0) : 1.0;

            cs[x * 8 + u] = cu * cos((2.0 * x + 1.0) * u * PI_VALUE / 16.0);
        }
    }

    for (by = 0; by < c->hblocks; by++) {
        for (bx = 0; bx < c->wblocks; bx++) {
            idct_block(c->coef + (by * c->wblocks + bx) * 64, j->qt[c->tq],
                       cs, pl + by * 8 * pw + bx * 8, pw);
        }
    }

    return pl;
}


static unsigned char
clamp8(double v)
{
    return to_u8(round_long(v));
}


static void
convert(const Jpg *j, unsigned char **planes, Image *im)
{
    long x;
    long y;

    for (y = 0; y < j->height; y++) {
        for (x = 0; x < j->width; x++) {
            double s[4] = { 0.0, 0.0, 0.0, 0.0 };
            unsigned char *o = im->rgba + (y * j->width + x) * 4;
            int k;
            double r;
            double g;
            double b;

            for (k = 0; k < j->ncomp; k++) {
                const Comp *c = &j->comp[k];
                long sx = x * c->h / j->hmax;
                long sy = y * c->v / j->vmax;

                s[k] = planes[k][sy * c->wblocks * 8 + sx];
            }

            if (j->ncomp == 1) {
                r = s[0];
                g = s[0];
                b = s[0];
            } else if (j->ncomp == 3 &&
                       !(j->adobe && j->adobe_transform == 0)) {
                r = s[0] + 1.402 * (s[2] - 128.0);
                g = s[0] - 0.344136 * (s[1] - 128.0) -
                    0.714136 * (s[2] - 128.0);
                b = s[0] + 1.772 * (s[1] - 128.0);
            } else if (j->ncomp == 3) {
                r = s[0];
                g = s[1];
                b = s[2];
            } else {
                double c0 = s[0];
                double c1 = s[1];
                double c2 = s[2];

                if (j->adobe_transform == 2) {
                    c0 = s[0] + 1.402 * (s[2] - 128.0);
                    c1 = s[0] - 0.344136 * (s[1] - 128.0) -
                         0.714136 * (s[2] - 128.0);
                    c2 = s[0] + 1.772 * (s[1] - 128.0);
                }

                if (j->adobe) {
                    r = c0 * s[3] / 255.0;
                    g = c1 * s[3] / 255.0;
                    b = c2 * s[3] / 255.0;
                } else {
                    r = (255.0 - c0) * (255.0 - s[3]) / 255.0;
                    g = (255.0 - c1) * (255.0 - s[3]) / 255.0;
                    b = (255.0 - c2) * (255.0 - s[3]) / 255.0;
                }
            }

            o[0] = clamp8(r);
            o[1] = clamp8(g);
            o[2] = clamp8(b);
            o[3] = 255;
        }
    }
}


/* Walks the markers; returns 0 when all scans have been decoded. */
static int
read_markers(Jpg *j)
{
    size_t pos = 2;
    int got_sof = 0;
    int got_scan = 0;

    while (pos + 4 <= j->len) {
        int m;
        long len;
        const unsigned char *p;

        if (j->d[pos] != 0xff) {
            pos++;
            continue;
        }

        m = j->d[pos + 1];

        if (m == 0xff || m == 0 || (m >= 0xd0 && m <= 0xd8) || m == 0x01) {
            pos += m == 0xff ? 1 : 2;
            continue;
        }

        if (m == 0xd9) {
            break;
        }

        len = rd16(j->d + pos + 2);

        if (len < 2 || pos + 2 + to_size(len) > j->len) {
            break;
        }

        p = j->d + pos + 4;
        len -= 2;
        pos += 4 + to_size(len);

        if (m == 0xc0 || m == 0xc1 || m == 0xc2) {
            j->progressive = m == 0xc2;

            if (got_sof || parse_sof(j, p, len) != 0) {
                return -1;
            }

            got_sof = 1;
        } else if (m == 0xc3 || (m >= 0xc5 && m <= 0xcf && m != 0xc8 &&
                                 m != 0xc4 && m != 0xcc)) {
            return -1;
        } else if (m == 0xc4) {
            parse_dht(j, p, len);
        } else if (m == 0xdb) {
            parse_dqt(j, p, len);
        } else if (m == 0xdd && len >= 2) {
            j->restart = to_int(rd16(p));
        } else if (m >= 0xe0 && m <= 0xef) {
            parse_app(j, m, p, len);
        } else if (m == 0xda) {
            Scan sc;

            if (!got_sof || parse_sos(j, p, len, &sc) != 0) {
                return -1;
            }

            j->pos = pos;
            decode_scan(j, &sc);
            pos = j->pos;
            got_scan = 1;

            /* Move on to the next marker after the entropy coded data. */
            while (pos + 1 < j->len &&
                   !(j->d[pos] == 0xff && j->d[pos + 1] != 0 &&
                     !(j->d[pos + 1] >= 0xd0 && j->d[pos + 1] <= 0xd7) &&
                     j->d[pos + 1] != 0xff)) {
                pos++;
            }
        }
    }

    return got_sof && got_scan ? 0 : -1;
}


int
image_decode_jpeg(const unsigned char *d, size_t len, Image *im, double *dpi)
{
    Jpg *j;
    unsigned char *planes[4] = { NULL, NULL, NULL, NULL };
    int i;
    int rc;

    if (len < 4 || d[0] != 0xff || d[1] != 0xd8) {
        return -1;
    }

    j = xcalloc(1, sizeof *j);
    j->d = d;
    j->len = len;
    j->dpi = 72.0;
    rc = read_markers(j);

    if (rc == 0) {
        im->w = j->width;
        im->h = j->height;
        im->rgba = xcalloc(to_size(im->w * im->h), 4);

        for (i = 0; i < j->ncomp; i++) {
            planes[i] = make_plane(j, &j->comp[i]);
        }

        convert(j, planes, im);
        *dpi = j->dpi;
    }

    for (i = 0; i < 4; i++) {
        free(planes[i]);
        free(j->comp[i].coef);
    }

    free(j);

    return rc;
}
