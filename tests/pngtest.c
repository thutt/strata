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
 * Hostile and damaged pictures for the PNG reader (src/png.c).  LaTeX and
 * dvipng may be compromised, so the reader must refuse every picture that
 * is cut off, claims a huge size, has a bad palette, filter or checksum, is
 * interlaced or is a decompression bomb, without a crash, a hang or a large
 * allocation.  The test makes all of its files itself, in a private
 * directory below TMPDIR, and removes them again.  The argument is the
 * directory of the test data (tests/data) that holds picture.png.
 */
#include "../src/util.h"
#include "../src/build_type.h"
#include "../src/png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#define PICTURE_SUM 4070330229u

static int failures;
static int checks;
static char dir[PATH_MAX];
static unsigned long made;

typedef struct {
    uint8_t *d;
    size_t n;
    size_t cap;
} Buf;

typedef struct {
    Buf *out;
    uint32_t acc;
    int nacc;
} Bits;


static void
fail_now(const char *what)
{
    fprintf(stderr, "pngtest: %s\n", what);
    exit(2);
}


static void
put(Buf *b, const void *p, size_t n)
{
    if (n == 0) {
        return;
    }

    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 64;
        b->d = realloc(b->d, b->cap);

        if (b->d == NULL) {
            fail_now("out of memory");
        }
    }

    memcpy(b->d + b->n, p, n);
    b->n += n;
}


static void
put8(Buf *b, int v)
{
    uint8_t c = u8(v);

    put(b, &c, 1);
}


static void
put32(Buf *b, uint32_t v)
{
    put8(b, isz(v >> 24));
    put8(b, isz((v >> 16) & 0xFF));
    put8(b, isz((v >> 8) & 0xFF));
    put8(b, isz(v & 0xFF));
}


static uint32_t
crc_of(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];

        for (int k = 0; k < 8; k++) {
            crc = (crc & 1) != 0 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
        }
    }

    return crc ^ 0xFFFFFFFFu;
}


/* Writes one chunk with a claimed length and a good or bad checksum. */
static void
chunk_ex(Buf *b, uint32_t claimed, const char *type, const void *body,
         size_t n, int good_crc)
{
    Buf t = {NULL, 0, 0};

    put(&t, type, 4);
    put(&t, body, n);

    uint32_t crc = crc_of(t.d, t.n);

    put32(b, claimed);
    put(b, t.d, t.n);
    put32(b, good_crc != 0 ? crc : crc ^ 0x1u);
    free(t.d);
}


static void
chunk(Buf *b, const char *type, const void *body, size_t n)
{
    chunk_ex(b, uns(isz(n)), type, body, n, 1);
}


static void
signature(Buf *b)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};

    put(b, sig, 8);
}


static void
ihdr_ex(Buf *b, uint32_t w, uint32_t h, int depth, int ctype, int comp,
        int interlace)
{
    Buf t = {NULL, 0, 0};

    put32(&t, w);
    put32(&t, h);
    put8(&t, depth);
    put8(&t, ctype);
    put8(&t, comp);
    put8(&t, 0);
    put8(&t, interlace);
    chunk(b, "IHDR", t.d, t.n);
    free(t.d);
}


/* A zlib stream of stored blocks, with a good or a bad header. */
static void
zlib_stored(Buf *out, const uint8_t *p, size_t n)
{
    size_t pos = 0;

    put8(out, 0x78);
    put8(out, 0x01);

    do {
        size_t k = n - pos > 65535 ? 65535 : n - pos;
        int last = pos + k == n ? 1 : 0;

        put8(out, last);
        put8(out, isz(k & 0xFF));
        put8(out, isz(k >> 8));
        put8(out, isz(~k & 0xFF));
        put8(out, isz((~k >> 8) & 0xFF));
        put(out, p + pos, k);
        pos += k;
    } while (pos < n);

    put32(out, 0);
}


static void
putbits(Bits *w, uint32_t v, int n)
{
    for (int i = 0; i < n; i++) {
        w->acc |= ((v >> i) & 1) << w->nacc;
        w->nacc++;

        if (w->nacc == 8) {
            put8(w->out, isz(w->acc));
            w->acc = 0;
            w->nacc = 0;
        }
    }
}


/* A Huffman code is sent from its most significant bit. */
static void
puthuff(Bits *w, uint32_t code, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        putbits(w, (code >> i) & 1, 1);
    }
}


/*
 * A zlib stream of one fixed-Huffman block: a zero byte, then "matches"
 * copies of 258 bytes each.  The stream is about 1.6 MB and inflates to
 * 258 MB when it is not stopped.
 */
static void
zlib_bomb(Buf *out, size_t matches)
{
    Bits w = {out, 0, 0};

    put8(out, 0x78);
    put8(out, 0x01);
    putbits(&w, 1, 1);
    putbits(&w, 1, 2);
    puthuff(&w, 0x30, 8);

    for (size_t i = 0; i < matches; i++) {
        puthuff(&w, 0xC5, 8);
        puthuff(&w, 0, 5);
    }

    puthuff(&w, 0, 7);
    putbits(&w, 0, 7);
    put32(out, 0);
}


/* Rows of a picture: the filter byte, then stride bytes of fill. */
static uint8_t *
make_rows(int h, size_t stride, int filter, int fill, size_t *n)
{
    *n = usz(h) * (stride + 1);

    uint8_t *p = malloc(*n + 1);

    if (p == NULL) {
        fail_now("out of memory");
    }

    for (int y = 0; y < h; y++) {
        p[usz(y) * (stride + 1)] = u8(filter);
        memset(p + usz(y) * (stride + 1) + 1, fill, stride);
    }

    return p;
}


/* A whole picture from raw rows that include the filter bytes. */
static void
make_png(Buf *b, int w, int h, int depth, int ctype, const uint8_t *rows,
         size_t n, const uint8_t *plte, size_t plte_n)
{
    Buf z = {NULL, 0, 0};

    signature(b);
    ihdr_ex(b, uns(w), uns(h), depth, ctype, 0, 0);

    if (plte != NULL) {
        chunk(b, "PLTE", plte, plte_n);
    }

    zlib_stored(&z, rows, n);
    chunk(b, "IDAT", z.d, z.n);
    chunk(b, "IEND", NULL, 0);
    free(z.d);
}


static void
check(const char *what, int ok)
{
    checks++;

    if (ok == 0) {
        failures++;
        printf("FAIL %s\n", what);
    }
}


/* Writes the bytes to a file in the private directory and reads them. */
static int
load_file(const Buf *b, uint8_t **rgb, int *w, int *h)
{
    char path[PATH_MAX];
    char name[64];

    made++;
    snprintf(name, sizeof name, "h%lu.png", made);
    snprintf(path, sizeof path, "%.4000s/%s", dir, name);

    FILE *fp = fopen(path, "wb");

    if (fp == NULL) {
        fail_now("cannot write a test file");
    }

    fwrite(b->d, 1, b->n, fp);
    fclose(fp);

    int ok = png_load(path, 0xFFFFFF, w, h, rgb);

    unlink(path);

    return ok;
}


/* The picture must be refused, with a reason that contains "why". */
static void
refused(const char *what, const Buf *b, const char *why)
{
    uint8_t *rgb = NULL;
    int w;
    int h;
    int ok = load_file(b, &rgb, &w, &h);
    char msg[160];

    snprintf(msg, sizeof msg, "%s is refused", what);
    check(msg, ok == 0);

    if (ok != 0) {
        free(rgb);
    }

    snprintf(msg, sizeof msg, "%s: the reason mentions \"%s\" (it is \"%s\")",
             what, why, png_error());
    check(msg, strstr(png_error(), why) != NULL);
}


/* The picture must load; the result is returned (the caller frees it). */
static uint8_t *
accepted(const char *what, const Buf *b, int *w, int *h)
{
    uint8_t *rgb = NULL;
    int ok = load_file(b, &rgb, w, h);
    char msg[160];

    snprintf(msg, sizeof msg, "%s is accepted (%s)", what,
             ok != 0 ? "yes" : png_error());
    check(msg, ok != 0);

    return ok != 0 ? rgb : NULL;
}


static void
test_good(void)
{
    static const int depths[4] = {1, 2, 4, 8};
    Buf b = {NULL, 0, 0};
    size_t n;
    int w;
    int h;

    /* RGB, 8 bits: the bytes arrive in the same order. */
    static const uint8_t px[2 * 3 + 1] = {0, 10, 20, 30, 40, 50, 60};
    uint8_t *rgb;

    make_png(&b, 2, 1, 8, 2, px, sizeof px, NULL, 0);
    rgb = accepted("an RGB picture", &b, &w, &h);
    check("the RGB picture has its size", rgb != NULL && w == 2 && h == 1);
    check("the RGB picture has its pixels", rgb != NULL &&
          rgb[0] == 10 && rgb[2] == 30 && rgb[3] == 40 && rgb[5] == 60);
    free(rgb);
    free(b.d);
    b.d = NULL;
    b.n = 0;
    b.cap = 0;

    for (int ct = 0; ct < 4; ct++) {
        int types[4] = {0, 3, 4, 6};
        int ctype = types[ct];

        for (int di = 0; di < 4; di++) {
            int depth = depths[di];
            int chans = ctype == 0 || ctype == 3 ? 1 : ctype == 4 ? 2 : 4;
            uint8_t pal[12] = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
            char what[64];

            if (ctype > 3 && depth != 8) {
                continue;
            }

            size_t stride = (usz(7) * usz(chans) * usz(depth) + 7) / 8;

            for (int filter = 0; filter <= 4; filter++) {
                uint8_t *rows = make_rows(5, stride, filter, 0, &n);

                b.n = 0;
                make_png(&b, 7, 5, depth, ctype, rows, n,
                         ctype == 3 ? pal : NULL, ctype == 3 ? 12 : 0);
                snprintf(what, sizeof what,
                         "type %d depth %d filter %d", ctype, depth,
                         filter);
                rgb = accepted(what, &b, &w, &h);
                check("a good picture has its size",
                      rgb != NULL && w == 7 && h == 5);
                free(rgb);
                free(rows);
            }
        }
    }

    free(b.d);
}


/* Every prefix of a good picture is refused, whatever chunk it cuts. */
static void
test_truncation(void)
{
    static const uint8_t pal[6] = {0, 0, 0, 255, 255, 255};
    Buf b = {NULL, 0, 0};
    size_t n;
    uint8_t *rows = make_rows(3, 1, 0, 0x55, &n);
    int bad = 0;

    make_png(&b, 8, 3, 1, 3, rows, n, pal, sizeof pal);

    uint8_t *rgb = NULL;
    int w;
    int h;

    check("the whole small picture loads",
          png_decode(b.d, b.n, 0xFFFFFF, &w, &h, &rgb) != 0);
    free(rgb);

    for (size_t len = 0; len < b.n; len++) {
        rgb = NULL;

        if (png_decode(b.d, len, 0xFFFFFF, &w, &h, &rgb) != 0) {
            bad++;
            free(rgb);
        }
    }

    check("every prefix of a good picture is refused", bad == 0);
    free(rows);
    free(b.d);
}


static void
test_headers(void)
{
    Buf b = {NULL, 0, 0};
    Buf z = {NULL, 0, 0};
    size_t n;
    uint8_t *rows = make_rows(2, 2, 0, 0, &n);

    zlib_stored(&z, rows, n);

    /* Huge claimed sizes. */
    static const uint32_t sizes[][2] = {
        {0xFFFFFFFFu, 0xFFFFFFFFu}, {0x80000000u, 1}, {1, 0x80000000u},
        {20000, 20000}, {20001, 1}, {1, 20001}, {4001, 2001},
        {100000, 100000}};

    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        char what[64];

        b.n = 0;
        signature(&b);
        ihdr_ex(&b, sizes[i][0], sizes[i][1], 8, 2, 0, 0);
        chunk(&b, "IDAT", z.d, z.n);
        chunk(&b, "IEND", NULL, 0);
        snprintf(what, sizeof what, "the size %lux%lu",
                 (unsigned long) sizes[i][0], (unsigned long) sizes[i][1]);
        refused(what, &b, "too");
    }

    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 0, 5, 8, 2, 0, 0);
    chunk(&b, "IDAT", z.d, z.n);
    chunk(&b, "IEND", NULL, 0);
    refused("a width of zero", &b, "no size");

    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 5, 0, 8, 2, 0, 0);
    chunk(&b, "IDAT", z.d, z.n);
    chunk(&b, "IEND", NULL, 0);
    refused("a height of zero", &b, "no size");

    /* Other bad header fields. */
    static const int fields[][3] = {{16, 2, 0}, {3, 0, 0}, {8, 5, 0},
                                    {8, 1, 0}, {4, 2, 0}, {1, 6, 0},
                                    {8, 2, 1}};

    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        char what[64];

        b.n = 0;
        signature(&b);
        ihdr_ex(&b, 2, 2, fields[i][0], fields[i][1], fields[i][2], 0);
        chunk(&b, "IDAT", z.d, z.n);
        chunk(&b, "IEND", NULL, 0);
        snprintf(what, sizeof what, "depth %d type %d method %d",
                 fields[i][0], fields[i][1], fields[i][2]);
        refused(what, &b, "picture");
    }

    /* A header of the wrong length. */
    b.n = 0;
    signature(&b);
    chunk(&b, "IHDR", "0123456789012345", 16);
    chunk(&b, "IDAT", z.d, z.n);
    chunk(&b, "IEND", NULL, 0);
    refused("an IHDR chunk of 16 bytes", &b, "header");

    /* Interlaced pictures are not supported. */
    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 2, 2, 8, 2, 0, 1);
    chunk(&b, "IDAT", z.d, z.n);
    chunk(&b, "IEND", NULL, 0);
    refused("an interlaced picture", &b, "nterlaced");

    /* The header is missing, or twice. */
    b.n = 0;
    signature(&b);
    chunk(&b, "IDAT", z.d, z.n);
    chunk(&b, "IEND", NULL, 0);
    refused("a missing IHDR", &b, "header");

    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 2, 2, 8, 2, 0, 0);
    ihdr_ex(&b, 2, 2, 8, 2, 0, 0);
    chunk(&b, "IDAT", z.d, z.n);
    chunk(&b, "IEND", NULL, 0);
    refused("a duplicated IHDR", &b, "header");

    /* The end chunk is missing: the file is cut off. */
    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 2, 2, 8, 2, 0, 0);
    chunk(&b, "IDAT", z.d, z.n);
    refused("a missing IEND", &b, "cut off");

    b.n = 0;
    signature(&b);
    refused("a signature only", &b, "cut off");

    /* A chunk that is longer than the file. */
    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 2, 2, 8, 2, 0, 0);
    chunk_ex(&b, 0xFFFFFFFFu, "IDAT", z.d, z.n, 1);
    chunk(&b, "IEND", NULL, 0);
    refused("a chunk length of 4 GB", &b, "cut off");

    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 2, 2, 8, 2, 0, 0);
    chunk_ex(&b, uns(isz(z.n)) + 1, "IDAT", z.d, z.n, 1);
    chunk(&b, "IEND", NULL, 0);
    refused("a chunk length one too long", &b, "");

    /* A bad checksum is refused: dvipng always writes a good one. */
    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 2, 2, 8, 2, 0, 0);
    chunk_ex(&b, uns(isz(z.n)), "IDAT", z.d, z.n, 0);
    chunk(&b, "IEND", NULL, 0);
    refused("a bad checksum", &b, "checksum");

    /* A not-a-PNG file, and an empty file. */
    b.n = 0;
    put(&b, "GIF89a.......................", 28);
    refused("a GIF file", &b, "not a PNG");

    b.n = 0;
    refused("an empty file", &b, "not a PNG");

    free(z.d);
    free(rows);
    free(b.d);
}


static void
test_data(void)
{
    static const uint8_t pal2[6] = {0, 0, 0, 255, 255, 255};
    Buf b = {NULL, 0, 0};
    Buf z = {NULL, 0, 0};
    size_t n;
    uint8_t *rows = make_rows(4, 3 * 4, 0, 0, &n);
    uint8_t *rgb;
    int w;
    int h;

    /* A bad filter type in one row, in each row and 255. */
    for (int y = 0; y < 4; y++) {
        static const int bads[3] = {5, 9, 255};

        for (int k = 0; k < 3; k++) {
            char what[64];

            rows[usz(y) * 13] = u8(bads[k]);
            b.n = 0;
            make_png(&b, 4, 4, 8, 2, rows, n, NULL, 0);
            snprintf(what, sizeof what, "filter %d in row %d", bads[k], y);
            refused(what, &b, "filter");
            rows[usz(y) * 13] = 0;
        }
    }

    free(rows);

    /* Data that is too short, or too long: both are refused. */
    rows = make_rows(4, 12, 0, 0, &n);
    b.n = 0;
    make_png(&b, 4, 4, 8, 2, rows, n - 1, NULL, 0);
    refused("data one byte short", &b, "incomplete");

    b.n = 0;
    make_png(&b, 4, 4, 8, 2, rows, 5, NULL, 0);
    refused("data of five bytes", &b, "incomplete");

    free(rows);
    rows = make_rows(5, 12, 0, 0, &n);
    b.n = 0;
    make_png(&b, 4, 4, 8, 2, rows, n, NULL, 0);
    refused("data one row too long", &b, "larger");
    free(rows);

    /* A stored block whose length check bytes are wrong. */
    rows = make_rows(4, 12, 0, 0, &n);
    zlib_stored(&z, rows, n);
    z.d[5] ^= 0x01;
    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 4, 4, 8, 2, 0, 0);
    chunk(&b, "IDAT", z.d, z.n);
    chunk(&b, "IEND", NULL, 0);
    refused("a stored block with a bad length", &b, "damaged");

    /* A bad zlib header and a preset dictionary. */
    z.n = 0;
    zlib_stored(&z, rows, n);
    z.d[0] = 0x79;
    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 4, 4, 8, 2, 0, 0);
    chunk(&b, "IDAT", z.d, z.n);
    chunk(&b, "IEND", NULL, 0);
    refused("a bad zlib header", &b, "damaged");

    z.n = 0;
    zlib_stored(&z, rows, n);
    z.d[1] = 0x3F;
    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 4, 4, 8, 2, 0, 0);
    chunk(&b, "IDAT", z.d, z.n);
    chunk(&b, "IEND", NULL, 0);
    refused("a zlib stream with a dictionary", &b, "damaged");
    free(rows);

    /* Palettes: an index beyond the palette, and bad palette chunks. */
    rows = make_rows(2, 1, 0, 0xFF, &n);
    b.n = 0;
    make_png(&b, 4, 2, 2, 3, rows, n, pal2, sizeof pal2);
    refused("a palette index beyond the palette", &b, "palette");
    free(rows);

    rows = make_rows(2, 4, 0, 200, &n);
    b.n = 0;
    make_png(&b, 4, 2, 8, 3, rows, n, pal2, sizeof pal2);
    refused("a palette index of 200 in a palette of 2", &b, "palette");

    static const uint8_t odd[4] = {1, 2, 3, 4};

    b.n = 0;
    make_png(&b, 4, 2, 8, 3, rows, n, odd, sizeof odd);
    refused("a palette chunk of 4 bytes", &b, "palette");

    b.n = 0;
    make_png(&b, 4, 2, 8, 3, rows, n, odd, 0);
    refused("a palette chunk of 0 bytes", &b, "palette");

    uint8_t big[257 * 3];

    memset(big, 7, sizeof big);
    b.n = 0;
    make_png(&b, 4, 2, 8, 3, rows, n, big, sizeof big);
    refused("a palette of 257 entries", &b, "palette");

    b.n = 0;
    make_png(&b, 4, 2, 8, 3, rows, n, NULL, 0);
    refused("a palette picture without a palette", &b, "palette");

    b.n = 0;
    make_png(&b, 4, 2, 8, 0, rows, n, pal2, sizeof pal2);
    refused("a palette in a grey picture", &b, "palette");

    /* A palette of 256 entries is good. */
    memset(rows, 0, n);
    b.n = 0;
    make_png(&b, 4, 2, 8, 3, rows, n, big, 256 * 3);
    rgb = accepted("a palette of 256 entries", &b, &w, &h);
    free(rgb);

    /* A palette chunk after the data. */
    Buf zz = {NULL, 0, 0};

    zlib_stored(&zz, rows, n);
    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 4, 2, 8, 3, 0, 0);
    chunk(&b, "IDAT", zz.d, zz.n);
    chunk(&b, "PLTE", pal2, sizeof pal2);
    chunk(&b, "IEND", NULL, 0);
    refused("a palette after the data", &b, "palette");

    /* Trailing data after IEND, and a second IEND: both are accepted. */
    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 4, 2, 8, 3, 0, 0);
    chunk(&b, "PLTE", pal2, sizeof pal2);
    chunk(&b, "IDAT", zz.d, zz.n);
    chunk(&b, "IEND", NULL, 0);
    put(&b, "trailing garbage that is not a chunk", 36);
    rgb = accepted("trailing data after IEND", &b, &w, &h);
    free(rgb);

    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 4, 2, 8, 3, 0, 0);
    chunk(&b, "PLTE", pal2, sizeof pal2);
    chunk(&b, "IDAT", zz.d, zz.n);
    chunk(&b, "IEND", NULL, 0);
    chunk(&b, "IEND", NULL, 0);
    rgb = accepted("a duplicated IEND", &b, &w, &h);
    free(rgb);

    free(zz.d);
    free(rows);
    free(z.d);
    free(b.d);
}


/* Decompression bombs and floods of chunks. */
static void
test_bombs(void)
{
    Buf b = {NULL, 0, 0};
    Buf z = {NULL, 0, 0};

    /* 1.6 MB that inflate to 258 MB, for a picture of 1 MB. */
    zlib_bomb(&z, 1000000);
    signature(&b);
    ihdr_ex(&b, 1000, 1000, 8, 0, 0, 0);
    chunk(&b, "IDAT", z.d, z.n);
    chunk(&b, "IEND", NULL, 0);
    refused("a decompression bomb", &b, "larger");

    /* The same, claimed for a small picture. */
    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 2, 2, 8, 0, 0, 0);
    chunk(&b, "IDAT", z.d, z.n);
    chunk(&b, "IEND", NULL, 0);
    refused("a bomb in a picture of four pixels", &b, "larger");

    /* A bomb for the largest picture the reader accepts. */
    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 2828, 2828, 8, 6, 0, 0);
    chunk(&b, "IDAT", z.d, z.n);
    chunk(&b, "IEND", NULL, 0);
    refused("a bomb in the largest picture", &b, "larger");

    free(z.d);

    /* A flood of empty data chunks. */
    z.d = NULL;
    z.n = 0;
    z.cap = 0;
    b.n = 0;
    signature(&b);
    ihdr_ex(&b, 2, 2, 8, 0, 0, 0);

    for (int i = 0; i < 200000; i++) {
        chunk(&b, "IDAT", NULL, 0);
    }

    chunk(&b, "IEND", NULL, 0);
    refused("200000 empty data chunks", &b, "damaged");

    free(b.d);
}


/* A file that is far too large is not read beyond the limit. */
static void
test_big_file(void)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    static uint8_t zeros[1 << 20];
    char path[PATH_MAX];
    uint8_t *rgb = NULL;
    int w;
    int h;

    snprintf(path, sizeof path, "%.4000s/big.png", dir);

    FILE *fp = fopen(path, "wb");

    if (fp == NULL) {
        fail_now("cannot write a test file");
    }

    fwrite(sig, 1, sizeof sig, fp);

    for (int i = 0; i < 40; i++) {
        fwrite(zeros, 1, sizeof zeros, fp);
    }

    fclose(fp);
    check("a file of 40 MB is refused",
          png_load(path, 0xFFFFFF, &w, &h, &rgb) == 0);
    check("a file of 40 MB: the reason is its size",
          strstr(png_error(), "too large") != NULL);
    unlink(path);
}


static void
test_depth(void)
{
    check("the depth 5", png_parse_depth("5\n", 10) == 5);
    check("the depth 0", png_parse_depth("0", 10) == 0);
    check("the depth 10 is the limit", png_parse_depth("10 \r\n", 10) == 10);
    check("the depth 11 is above the limit",
          png_parse_depth("11\n", 10) == -1);
    check("the depth -1", png_parse_depth("-1\n", 10) == -1);
    check("the depth +3", png_parse_depth("+3\n", 10) == -1);
    check("the depth abc", png_parse_depth("abc\n", 10) == -1);
    check("an empty depth", png_parse_depth("", 10) == -1);
    check("a depth that is a blank line", png_parse_depth("\n", 10) == -1);
    check("the depth 5x", png_parse_depth("5x\n", 10) == -1);
    check("the depth 3 4", png_parse_depth("3 4\n", 10) == -1);
    check("the depth 1.5", png_parse_depth("1.5\n", 10) == -1);
    check("the depth 99999999999",
          png_parse_depth("99999999999\n", PNG_MAX_DEPTH) == -1);
    check("the depth 2147483648",
          png_parse_depth("2147483648\n", PNG_MAX_DEPTH) == -1);
    check("the depth 0x10", png_parse_depth("0x10\n", PNG_MAX_DEPTH) == -1);
    check("the depth 100000 is the largest",
          png_parse_depth("100000\n", PNG_MAX_DEPTH) == 100000);
    check("the depth 100001 is too large",
          png_parse_depth("100001\n", PNG_MAX_DEPTH) == -1);
}


/*
 * The pixels of a page: the sizes are rounded up, as the raster program makes
 * the picture, because the pixel that a page covers only in part is not
 * painted in the color of the page unless the page is scaled to the whole
 * pixels.
 */
static void
test_page_pixels(void)
{
    int w = 0;
    int h = 0;
    int ok;

    ok = png_page_pixels("Title:  x\nPage size:   68.344 x 25.196 pts\n",
                         100, &w, &h);
    check("a page of 68.344 by 25.196 points at 100 dpi is read", ok == 1);
    check("its width 94.92 pixels is rounded up to 95", w == 95);
    check("its height 34.99 pixels is rounded up to 35", h == 35);

    ok = png_page_pixels("Page size:      68.344 x 25.196 pts\n", 72, &w, &h);
    check("the same page at 72 dpi is read", ok == 1);
    check("at 72 dpi the width 68.344 is 69", w == 69);
    check("at 72 dpi the height 25.196 is 26", h == 26);

    ok = png_page_pixels("Page size:      612 x 792 pts (letter)\n", 72, &w,
                         &h);
    check("a letter page is read, whole sizes stay", ok == 1 && w == 612 &&
                                                       h == 792);

    ok = png_page_pixels("Page size: 83.001 x 10.999 pts\n", 72, &w, &h);
    check("83.001 points at 72 dpi is 84 pixels and 10.999 is 11",
          ok == 1 && w == 84 && h == 11);

    ok = png_page_pixels("Page size: 100 x 50\n", 144, &w, &h);
    check("a size with no unit is read as points",
          ok == 1 && w == 200 && h == 100);

    check("a text with no page size line is refused",
          png_page_pixels("Pages: 1\n", 100, &w, &h) == 0);
    check("a size with no x is refused",
          png_page_pixels("Page size: 68.3 25.1 pts\n", 100, &w, &h) == 0);
    check("a size with a letter in it is refused",
          png_page_pixels("Page size: abc x 25.1 pts\n", 100, &w, &h) == 0);
    check("a resolution of zero is refused",
          png_page_pixels("Page size: 68.3 x 25.1 pts\n", 0, &w, &h) == 0);
    check("a page below one pixel is refused",
          png_page_pixels("Page size: 0.0 x 25.1 pts\n", 72, &w, &h) == 0);
    check("a page above the limit is refused",
          png_page_pixels("Page size: 99999 x 25.1 pts\n", 1000, &w, &h) == 0);
}


/* The pictures of the project load exactly as they did before. */
static void
test_regression(const char *datadir)
{
    char path[PATH_MAX];
    uint8_t *rgb;
    int w;
    int h;
    uint32_t sum = 2166136261u;

    snprintf(path, sizeof path, "%s/picture.png", datadir);
    int ok = png_load(path, 0xFFFFFF, &w, &h, &rgb);

    check("picture.png loads", ok != 0);

    if (ok == 0) {
        return;
    }

    for (size_t i = 0; i < usz(w) * usz(h) * 3; i++) {
        sum = (sum ^ rgb[i]) * 16777619u;
    }

    free(rgb);
    printf("pngtest: picture.png sum %lu\n", (unsigned long) sum);
    check("picture.png is 60 by 30", w == 60 && h == 30);
    check("picture.png has the pixels it always had", sum == PICTURE_SUM);
}


int
main(int argc, char **argv)
{
    char tmpl[PATH_MAX];

    if (argc != 2) {
        fprintf(stderr, "usage: pngtest DATA-DIRECTORY\n");

        return 2;
    }

    /* A hang would be a failure too. */
    alarm(300);
    tmp_path(tmpl, sizeof tmpl, "strata-pngtest-XXXXXX");

    if (mkdtemp(tmpl) == NULL) {
        fail_now("cannot make a directory");
    }

    snprintf(dir, sizeof dir, "%s", tmpl);
    test_good();
    test_truncation();
    test_headers();
    test_data();
    test_bombs();
    test_depth();
    test_page_pixels();
    test_regression(argv[1]);

    struct rusage ru;

    getrusage(RUSAGE_SELF, &ru);
    printf("pngtest: peak memory %ld KB before the large file\n",
           ru.ru_maxrss);
    test_big_file();
    getrusage(RUSAGE_SELF, &ru);
    printf("pngtest: peak memory %ld KB\n", ru.ru_maxrss);
    /* The sanitizer keeps freed memory and shadow memory: not measured. */
    check("the peak memory is below 100 MB",
          strata_asan != 0 || ru.ru_maxrss < 100 * 1024);
    rmdir(dir);
    printf("pngtest: %d checks, %d failures\n", checks, failures);

    return failures == 0 ? 0 : 1;
}
