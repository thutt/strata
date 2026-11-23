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

/* png.c - a small PNG reader; see png.h. */
#include "util.h"
#include "png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Reads a depth as a plain decimal number: digits only, at most seven of
 * them, optionally followed by white space.  Anything else (a sign, a
 * letter, an empty text, a huge number) gives -1.  A depth above the
 * limit also gives -1.
 */
int
png_parse_depth(const char *text, int limit)
{
    int v = 0;
    int n = 0;

    while (text[n] >= '0' && text[n] <= '9' && n < 7) {
        v = v * 10 + (text[n] - '0');
        n++;
    }

    if (n == 0) {
        return -1;
    }

    while (text[n] == ' ' || text[n] == '\t' || text[n] == '\r' ||
           text[n] == '\n') {
        n++;
    }

    if (text[n] != '\0' || v > limit) {
        return -1;
    }

    return v;
}


/* A long that is known to be small, as an int (clamped to INT_MAX). */
static int
small_long(long n)
{
    int r = INT_MAX;

    if (n >= 0 && n < INT_MAX) {
        r = (int) n;
    }

    return r;
}


/*
 * A decimal number with at most three digits after the point, read from
 * *at, as thousandths.  Leading blanks are skipped.  Returns 1 and moves *at
 * past the number, or 0 when there is no number or when it is too large.
 */
static int
parse_thousandths(const char **at, long *out)
{
    const char *p = *at;
    long whole = 0;
    long frac = 0;
    int digits = 0;
    int places = 0;

    while (*p == ' ' || *p == '\t') {
        p++;
    }

    while (*p >= '0' && *p <= '9' && digits < 7) {
        whole = whole * 10 + (*p - '0');
        digits++;
        p++;
    }

    if (digits == 0) {
        return 0;
    }

    if (*p == '.') {
        p++;

        while (*p >= '0' && *p <= '9') {
            if (places < 3) {
                frac = frac * 10 + (*p - '0');
                places++;
            }

            p++;
        }
    }

    for (; places < 3; places++) {
        frac *= 10;
    }

    *out = whole * 1000 + frac;
    *at = p;

    return 1;
}


int
png_page_pixels(const char *info, int dpi, int *w, int *h)
{
    const char *tag = "Page size:";
    const char *p = strstr(info, tag);
    long wm;
    long hm;

    if (p == NULL || dpi <= 0 || dpi > 10000) {
        return 0;
    }

    p += strlen(tag);

    if (parse_thousandths(&p, &wm) == 0) {
        return 0;
    }

    while (*p == ' ' || *p == '\t') {
        p++;
    }

    if (*p != 'x') {
        return 0;
    }

    p++;

    if (parse_thousandths(&p, &hm) == 0) {
        return 0;
    }

    /* The sizes are in points of 1/72 inch, in thousandths; round up. */
    long pw = (wm * dpi + 71999) / 72000;
    long ph = (hm * dpi + 71999) / 72000;

    if (pw < 1 || ph < 1 || pw > PNG_MAX_SIDE || ph > PNG_MAX_SIDE) {
        return 0;
    }

    *w = small_long(pw);
    *h = small_long(ph);

    return 1;
}


/* ------------------------------------------------------------- inflate */
typedef struct {
    const uint8_t *in;
    size_t len;
    size_t pos;
    uint32_t bits;
    int nbits;
    uint8_t *out;
    size_t outlen;
    size_t outcap;
    size_t outmax;
    int bad;
} Inf;

typedef struct {
    uint16_t count[16];
    uint16_t symbol[288];
} Huff;


static int
get_bit(Inf *s)
{
    if (s->nbits == 0) {
        if (s->pos >= s->len) {
            s->bad = 1;

            return 0;
        }

        s->bits = s->in[s->pos++];
        s->nbits = 8;
    }

    int b = isz(s->bits & 1);

    s->bits >>= 1;
    s->nbits--;

    return b;
}


static int
get_bits(Inf *s, int n)
{
    int v = 0;

    for (int i = 0; i < n; i++) {
        v |= get_bit(s) << i;
    }

    return v;
}


static int
huff_build(Huff *h, const uint8_t *lengths, int n)
{
    int offs[16];
    int left = 1;

    memset(h->count, 0, sizeof h->count);

    for (int i = 0; i < n; i++) {
        h->count[lengths[i]]++;
    }

    h->count[0] = 0;

    for (int i = 1; i < 16; i++) {
        left <<= 1;
        left -= h->count[i];

        if (left < 0) {
            return -1;
        }
    }

    offs[1] = 0;

    for (int i = 1; i < 15; i++) {
        offs[i + 1] = offs[i] + h->count[i];
    }

    for (int i = 0; i < n; i++) {
        if (lengths[i] != 0) {
            h->symbol[offs[lengths[i]]++] = u16(uns(i));
        }
    }

    return 0;
}


static int
huff_decode(Inf *s, const Huff *h)
{
    int code = 0;
    int first = 0;
    int index = 0;

    for (int len = 1; len <= 15; len++) {
        code |= get_bit(s);

        int count = h->count[len];

        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }

        index += count;
        first += count;
        first <<= 1;
        code <<= 1;

        if (s->bad != 0) {
            return -1;
        }
    }

    return -1;
}


static void
put_byte(Inf *s, int b)
{
    if (s->bad != 0) {
        return;
    }

    if (s->outlen >= s->outmax) {
        /* More data than the header allows: stop, whatever the stream. */
        s->bad = 1;

        return;
    }

    if (s->outlen == s->outcap) {
        size_t cap = s->outcap != 0 ? s->outcap * 2 : 65536;

        if (cap > s->outmax) {
            cap = s->outmax;
        }

        uint8_t *p = realloc(s->out, cap);

        if (p == NULL) {
            s->bad = 1;

            return;
        }

        s->out = p;
        s->outcap = cap;
    }

    s->out[s->outlen++] = u8(b);
}


static const uint16_t len_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11, 13,
                                      15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                      67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
                                      1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                      4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t dist_base[30] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
    33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,
                                       4, 4, 5, 5, 6, 6, 7, 7,  8,  8,
                                       9, 9, 10, 10, 11, 11, 12, 12, 13, 13};


static int
inflate_codes(Inf *s, const Huff *lit, const Huff *dist)
{
    for (;;) {
        int sym = huff_decode(s, lit);

        if (sym < 0) {
            return -1;
        }

        if (sym < 256) {
            put_byte(s, sym);
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;

            if (sym >= 29) {
                return -1;
            }

            int len = len_base[sym] + get_bits(s, len_extra[sym]);
            int ds = huff_decode(s, dist);

            if (ds < 0 || ds >= 30) {
                return -1;
            }

            size_t d = usz(dist_base[ds] + get_bits(s, dist_extra[ds]));

            if (d > s->outlen) {
                return -1;
            }

            for (int i = 0; i < len; i++) {
                put_byte(s, s->out[s->outlen - d]);
            }
        }

        if (s->bad != 0) {
            return -1;
        }
    }
}


static int
inflate_all(Inf *s)
{
    int last;

    do {
        last = get_bit(s);

        int type = get_bits(s, 2);

        if (type == 0) {
            s->nbits = 0;

            if (s->pos + 4 > s->len) {
                return -1;
            }

            size_t n = s->in[s->pos] | (usz(s->in[s->pos + 1]) << 8);
            size_t nn = s->in[s->pos + 2] | (usz(s->in[s->pos + 3]) << 8);

            if ((n ^ nn) != 0xFFFF) {
                return -1;
            }

            s->pos += 4;

            if (s->pos + n > s->len) {
                return -1;
            }

            for (size_t i = 0; i < n && s->bad == 0; i++) {
                put_byte(s, s->in[s->pos++]);
            }
        } else if (type == 1) {
            static Huff lit;
            static Huff dist;
            uint8_t l[288];
            uint8_t dl[30];

            for (int i = 0; i < 144; i++) {
                l[i] = 8;
            }

            for (int i = 144; i < 256; i++) {
                l[i] = 9;
            }

            for (int i = 256; i < 280; i++) {
                l[i] = 7;
            }

            for (int i = 280; i < 288; i++) {
                l[i] = 8;
            }

            for (int i = 0; i < 30; i++) {
                dl[i] = 5;
            }

            if (huff_build(&lit, l, 288) != 0 ||
                huff_build(&dist, dl, 30) != 0) {
                return -1;
            }

            if (inflate_codes(s, &lit, &dist) != 0) {
                return -1;
            }
        } else if (type == 2) {
            static const uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                              11, 4,  12, 3, 13, 2, 14, 1, 15};
            uint8_t lengths[320];
            Huff lit;
            Huff dist;
            Huff cl;
            int nlen = get_bits(s, 5) + 257;
            int ndist = get_bits(s, 5) + 1;
            int ncode = get_bits(s, 4) + 4;

            if (nlen > 286 || ndist > 30) {
                return -1;
            }

            memset(lengths, 0, sizeof lengths);

            for (int i = 0; i < ncode; i++) {
                lengths[order[i]] = u8(get_bits(s, 3));
            }

            if (huff_build(&cl, lengths, 19) != 0) {
                return -1;
            }

            int idx = 0;

            memset(lengths, 0, sizeof lengths);

            while (idx < nlen + ndist) {
                int sym = huff_decode(s, &cl);

                if (sym < 0) {
                    return -1;
                }

                if (sym < 16) {
                    lengths[idx++] = u8(sym);
                } else {
                    int rep;
                    int val = 0;

                    if (sym == 16) {
                        if (idx == 0) {
                            return -1;
                        }

                        val = lengths[idx - 1];
                        rep = 3 + get_bits(s, 2);
                    } else if (sym == 17) {
                        rep = 3 + get_bits(s, 3);
                    } else {
                        rep = 11 + get_bits(s, 7);
                    }

                    if (idx + rep > nlen + ndist) {
                        return -1;
                    }

                    while (rep-- > 0) {
                        lengths[idx++] = u8(val);
                    }
                }
            }

            if (huff_build(&lit, lengths, nlen) != 0 ||
                huff_build(&dist, lengths + nlen, ndist) != 0) {
                return -1;
            }

            if (inflate_codes(s, &lit, &dist) != 0) {
                return -1;
            }
        } else {
            return -1;
        }

        if (s->bad != 0) {
            return -1;
        }
    } while (last == 0);

    return 0;
}


/* ----------------------------------------------------------------- png */
static const char *png_why = "";


const char *
png_error(void)
{
    return png_why;
}


static int
fail(const char *why)
{
    png_why = why;

    return 0;
}


static uint32_t
be32(const uint8_t *p)
{
    return uns(p[0]) << 24 | uns(p[1]) << 16 | uns(p[2]) << 8 | uns(p[3]);
}


static uint32_t
crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    static int made;

    if (made == 0) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;

            for (int k = 0; k < 8; k++) {
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }

            table[i] = c;
        }

        made = 1;
    }

    for (size_t i = 0; i < n; i++) {
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc;
}


/* Multiplies two sizes; returns 0 when the product does not fit. */
static int
mul_size(size_t a, size_t b, size_t *r)
{
    if (a != 0 && b > SIZE_MAX / a) {
        return 0;
    }

    *r = a * b;

    return 1;
}


static int
paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = abs(p - a);
    int pb = abs(p - b);
    int pc = abs(p - c);

    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}


/* Undoes the filter of one row; the filter type was checked before. */
static void
unfilter_row(const uint8_t *row, int f, size_t stride, size_t bpp,
             const uint8_t *prev, uint8_t *cur)
{
    for (size_t i = 0; i < stride; i++) {
        int x = row[1 + i];
        int a = i >= bpp ? cur[i - bpp] : 0;
        int b = prev[i];
        int c = i >= bpp ? prev[i - bpp] : 0;

        switch (f) {
            case 1:
                x += a;
                break;

            case 2:
                x += b;
                break;

            case 3:
                x += (a + b) / 2;
                break;

            case 4:
                x += paeth(a, b, c);
                break;

            default:
                break;
        }

        cur[i] = u8(x);
    }
}


typedef struct {
    int width;
    int height;
    int depth;
    int ctype;
    int chans;
    uint8_t pal[256][3];
    uint8_t trans[256];
    int npal;
} Hdr;


/* Converts the unfiltered row to RGB, with every palette index checked. */
static int
convert_row(const Hdr *hd, const uint8_t *cur, uint32_t background,
            uint8_t *o)
{
    for (int x = 0; x < hd->width; x++) {
        int r;
        int g;
        int bl;
        int al = 255;

        if (hd->ctype == 3) {
            int bit = x * hd->depth;
            int v = (cur[bit / 8] >> (8 - hd->depth - bit % 8)) &
                    ((1 << hd->depth) - 1);

            if (v >= hd->npal) {
                return 0;
            }

            r = hd->pal[v][0];
            g = hd->pal[v][1];
            bl = hd->pal[v][2];
            al = hd->trans[v];
        } else if (hd->ctype == 0 || hd->ctype == 4) {
            int v;

            if (hd->depth == 8) {
                v = cur[x * hd->chans];
                al = hd->ctype == 4 ? cur[x * 2 + 1] : 255;
            } else {
                int bit = x * hd->depth;

                v = (cur[bit / 8] >> (8 - hd->depth - bit % 8)) &
                    ((1 << hd->depth) - 1);
                v = v * 255 / ((1 << hd->depth) - 1);
            }

            r = g = bl = v;
        } else {
            r = cur[x * hd->chans];
            g = cur[x * hd->chans + 1];
            bl = cur[x * hd->chans + 2];
            al = hd->ctype == 6 ? cur[x * 4 + 3] : 255;
        }

        if (al != 255) {
            int br = isz((background >> 16) & 0xFF);
            int bg = isz((background >> 8) & 0xFF);
            int bb = isz(background & 0xFF);

            r = (r * al + br * (255 - al)) / 255;
            g = (g * al + bg * (255 - al)) / 255;
            bl = (bl * al + bb * (255 - al)) / 255;
        }

        o[0] = u8(r);
        o[1] = u8(g);
        o[2] = u8(bl);
        o += 3;
    }

    return 1;
}


/* Checks the header chunk and fills in the header; 0 means refused. */
static int
read_header(const uint8_t *body, size_t n, Hdr *hd)
{
    if (n != 13) {
        return fail("The picture file has a bad header");
    }

    uint32_t w = be32(body);
    uint32_t h = be32(body + 4);

    hd->depth = body[8];
    hd->ctype = body[9];

    if (w == 0 || h == 0 || w > PNG_MAX_SIDE || h > PNG_MAX_SIDE) {
        return fail("The picture is too large or has no size");
    }

    hd->width = isz(w);
    hd->height = isz(h);

    if (usz(hd->width) > PNG_MAX_PIXELS / usz(hd->height)) {
        return fail("The picture has too many pixels");
    }

    if (body[10] != 0 || body[11] != 0) {
        return fail("The picture file has an unknown method");
    }

    if (body[12] != 0) {
        return fail("An interlaced picture is not supported");
    }

    hd->chans = hd->ctype == 0 ? 1 : hd->ctype == 2 ? 3 :
                hd->ctype == 3 ? 1 : hd->ctype == 4 ? 2 :
                hd->ctype == 6 ? 4 : 0;

    int depth_ok = hd->depth == 8;

    if (hd->ctype == 3 || hd->ctype == 0) {
        depth_ok = hd->depth == 1 || hd->depth == 2 || hd->depth == 4 ||
                   hd->depth == 8;
    }

    if (hd->chans == 0 || depth_ok == 0) {
        return fail("The picture has an unsupported color format");
    }

    return 1;
}


/* Checks the palette chunk and copies it; 0 means refused. */
static int
read_palette(const uint8_t *body, size_t n, Hdr *hd, int have_plte,
             size_t idatlen)
{
    if (have_plte != 0 || idatlen != 0 || n == 0 || n % 3 != 0 ||
        n / 3 > 256 || hd->ctype == 0 || hd->ctype == 4) {
        return fail("The picture has a bad palette");
    }

    hd->npal = isz(n / 3);

    for (int i = 0; i < hd->npal; i++) {
        memcpy(hd->pal[i], body + 3 * usz(i), 3);
    }

    return 1;
}


/*
 * Reads the chunks.  On success the header is filled in and *idatp is a
 * malloc'ed copy of all IDAT data.  Every length is checked against what
 * remains of the file before it is used, and so is the checksum.  Data
 * after the IEND chunk is ignored.
 */
static int
read_chunks(const uint8_t *data, size_t len, Hdr *hd, uint8_t **idatp,
            size_t *idatlenp)
{
    size_t pos = 8;
    uint8_t *idat = NULL;
    size_t idatlen = 0;
    int have_hdr = 0;
    int have_plte = 0;
    int have_end = 0;
    int ok = 1;

    while (ok != 0 && have_end == 0) {
        if (len - pos < 12) {
            ok = fail("The picture file is cut off");
            break;
        }

        size_t n = be32(data + pos);
        const uint8_t *type = data + pos + 4;
        const uint8_t *body = data + pos + 8;

        if (n > len - pos - 12) {
            ok = fail("The picture file is cut off");
            break;
        }

        uint32_t crc = crc32_update(0xFFFFFFFFu, type, n + 4) ^ 0xFFFFFFFFu;

        if (crc != be32(body + n)) {
            ok = fail("The picture file is damaged (bad checksum)");
        } else if (have_hdr == 0 && memcmp(type, "IHDR", 4) != 0) {
            ok = fail("The picture file has no header");
        } else if (memcmp(type, "IHDR", 4) == 0) {
            ok = have_hdr == 0 ? read_header(body, n, hd) :
                 fail("The picture file has a bad header");
            have_hdr = 1;
        } else if (memcmp(type, "PLTE", 4) == 0) {
            ok = read_palette(body, n, hd, have_plte, idatlen);
            have_plte = 1;
        } else if (memcmp(type, "tRNS", 4) == 0 && hd->ctype == 3) {
            if (n > 256) {
                ok = fail("The picture has a bad transparency table");
            } else {
                memcpy(hd->trans, body, n);
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            uint8_t *p = NULL;

            if (n > PNG_MAX_FILE - idatlen) {
                ok = fail("The picture file is too large");
            } else if (n == 0) {
                /* An empty chunk adds nothing (and realloc of 0 frees). */
                p = idat;
            } else {
                p = realloc(idat, idatlen + n);
            }

            if (ok != 0 && p == NULL && n != 0) {
                ok = fail("There is not enough memory for the picture");
            } else if (ok != 0 && n != 0) {
                idat = p;
                memcpy(idat + idatlen, body, n);
                idatlen += n;
            }
        } else if (memcmp(type, "IEND", 4) == 0) {
            have_end = 1;
        }

        pos += 12 + n;
    }

    if (ok != 0 && hd->ctype == 3 && hd->npal == 0) {
        ok = fail("The picture has no palette");
    }

    if (ok == 0) {
        free(idat);

        return 0;
    }

    *idatp = idat;
    *idatlenp = idatlen;

    return 1;
}


/*
 * Inflates the IDAT data into exactly the size the header gives.  A
 * stream that is shorter or longer is refused; the inflater stops at the
 * size, so that a small stream cannot grow into a large allocation.
 */
static int
inflate_image(const Hdr *hd, const uint8_t *idat, size_t idatlen,
              size_t *stridep, Inf *s)
{
    size_t stride;
    size_t expected;

    if (mul_size(usz(hd->width), usz(hd->chans), &stride) == 0 ||
        mul_size(stride, usz(hd->depth), &stride) == 0) {
        return fail("The picture is too large");
    }

    stride = (stride + 7) / 8;

    if (mul_size(stride + 1, usz(hd->height), &expected) == 0 ||
        expected > PNG_MAX_INFLATED) {
        return fail("The picture is too large");
    }

    /* The zlib header: deflate, a window of at most 32K, no dictionary. */
    if (idatlen < 6 || (idat[0] & 0x0F) != 8 || (idat[0] >> 4) > 7 ||
        (idat[1] & 0x20) != 0 || (idat[0] * 256 + idat[1]) % 31 != 0) {
        return fail("The picture data is damaged");
    }

    memset(s, 0, sizeof *s);
    s->in = idat + 2;
    s->len = idatlen - 2;
    s->outmax = expected;

    int rc = inflate_all(s);

    if (rc != 0 || s->outlen != expected) {
        int big = s->bad != 0 && s->outlen >= expected;

        free(s->out);
        s->out = NULL;

        return fail(big != 0 ?
                    "The picture data is larger than the size in its header" :
                    "The picture data is damaged or incomplete");
    }

    *stridep = stride;

    return 1;
}


int
png_decode(const uint8_t *data, size_t len, uint32_t background, int *w,
           int *h, uint8_t **rgb)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    Hdr hd;
    uint8_t *idat = NULL;
    size_t idatlen = 0;
    size_t stride = 0;
    Inf s;

    png_why = "The picture file is not a PNG file";

    if (len < 8 || memcmp(data, sig, 8) != 0) {
        return 0;
    }

    memset(&hd, 0, sizeof hd);
    memset(hd.trans, 255, sizeof hd.trans);

    if (read_chunks(data, len, &hd, &idat, &idatlen) == 0) {
        return 0;
    }

    int rc = inflate_image(&hd, idat, idatlen, &stride, &s);

    free(idat);

    if (rc == 0) {
        return 0;
    }

    /* Every filter type is checked before the picture is allocated. */
    for (int y = 0; y < hd.height; y++) {
        if (s.out[usz(y) * (stride + 1)] > 4) {
            free(s.out);

            return fail("The picture data has a bad filter");
        }
    }

    size_t bytes = usz(hd.width) * usz(hd.height) * 3;
    uint8_t *img = malloc(bytes);
    uint8_t *prev = calloc(stride, 1);
    uint8_t *cur = malloc(stride);
    int ok = img != NULL && prev != NULL && cur != NULL;
    size_t bpp = (usz(hd.chans) * usz(hd.depth) + 7) / 8;

    if (ok == 0) {
        png_why = "There is not enough memory for the picture";
    }

    for (int y = 0; ok != 0 && y < hd.height; y++) {
        const uint8_t *row = s.out + usz(y) * (stride + 1);

        unfilter_row(row, row[0], stride, bpp, prev, cur);

        if (convert_row(&hd, cur, background,
                        img + usz(y) * usz(hd.width) * 3) == 0) {
            png_why = "The picture has a color that is not in its palette";
            ok = 0;
        }

        memcpy(prev, cur, stride);
    }

    free(prev);
    free(cur);
    free(s.out);

    if (ok == 0) {
        free(img);

        return 0;
    }

    *w = hd.width;
    *h = hd.height;
    *rgb = img;

    return 1;
}


int
png_load(const char *path, uint32_t background, int *w, int *h,
         uint8_t **rgb)
{
    FILE *fp = fopen(path, "rb");

    png_why = "The picture file cannot be read";

    if (fp == NULL) {
        return 0;
    }

    size_t cap = 1 << 16;
    size_t n = 0;
    uint8_t *buf = malloc(cap);
    size_t k;

    while (buf != NULL && (k = fread(buf + n, 1, cap - n, fp)) > 0) {
        n += k;

        if (n == cap) {
            if (cap >= PNG_MAX_FILE) {
                free(buf);
                fclose(fp);

                return fail("The picture file is too large");
            }

            cap *= 2;

            uint8_t *p = realloc(buf, cap);

            if (p == NULL) {
                free(buf);
            }

            buf = p;
        }
    }

    fclose(fp);

    if (buf == NULL) {
        return fail("There is not enough memory for the picture");
    }

    int ok = png_decode(buf, n, background, w, h, rgb);

    free(buf);

    return ok;
}
