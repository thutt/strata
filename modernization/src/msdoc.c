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

/* msdoc.c - load/save/model for the .DOC file format. */
#include "util.h"
#include "log.h"
#include "backup.h"
#include "msdoc.h"
#include "marker.h"
#include "readfile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>


/* ------------------------------------------------------------------ util */
static void *
xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n != 0 ? n : 1);

    if (q == NULL) {
        log_fatal(1, "msdoc: out of memory allocating %zu bytes", n);
    }

    return q;
}


void
lines_add(Lines *l, const void *s, size_t n)
{
    if (l->n == l->cap) {
        l->cap = l->cap != 0 ? l->cap * 2 : 8;
        l->v = xrealloc(l->v, usz(l->cap) * sizeof *l->v);
    }

    l->v[l->n].s = xrealloc(NULL, n + 1);
    memcpy(l->v[l->n].s, s, n);
    l->v[l->n].s[n] = 0;
    l->v[l->n].n = isz(n);
    l->n++;
}


void
lines_free(Lines *l)
{
    for (int i = 0; i < l->n; i++) {
        free(l->v[i].s);
    }

    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}


const char *
props_get(const Lines *l, const char *code, char sub)
{
    for (int i = 0; i < l->n; i++) {
        const Str *r = &l->v[i];

        if (r->n >= 2 && r->s[0] == u8(code[0]) && r->s[1] == u8(code[1])) {
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


void
props_set(Lines *l, const char *code, char sub, const char *val)
{
    char buf[256];
    int n = snprintf(buf, sizeof buf, "%c%c%c%s", code[0], code[1],
                     sub != '\0' ? sub : ' ', val);
    char *p = buf;

    if (sub == '\0') {
        memmove(buf + 2, buf + 3, usz(n - 2));
        n--;
    }

    for (int i = 0; i < l->n; i++) {
        Str *r = &l->v[i];

        if (r->n >= 2 && r->s[0] == u8(code[0]) && r->s[1] == u8(code[1]) &&
            (sub == 0 || (r->n >= 3 && r->s[2] == u8(sub)))) {
            free(r->s);
            r->s = xrealloc(NULL, usz(n + 1));
            memcpy(r->s, p, usz(n));
            r->s[n] = 0;
            r->n = n;
            return;
        }
    }

    lines_add(l, p, usz(n));
}


void
props_remove(Lines *l, const char *code)
{
    for (int i = 0; i < l->n; i++) {
        const Str *r = &l->v[i];

        if (r->n >= 2 && r->s[0] == u8(code[0]) && r->s[1] == u8(code[1])) {
            free(l->v[i].s);

            for (int j = i; j + 1 < l->n; j++) {
                l->v[j] = l->v[j + 1];
            }

            l->n--;

            return;
        }
    }
}


const char *
doc_header_get(const Doc *d, const char *code)
{
    static char buf[512];
    const char *v = props_get(&d->header, code, 0);

    if (v == NULL) {
        return NULL;
    }

    str_copy(buf, sizeof buf, v);

    return buf;
}


int
doc_desc_valid(const char *text)
{
    size_t n = strlen(text);

    if (n > DOC_MAX_DESC) {
        return 0;
    }

    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];

        if (c < 0x20 || c == 0x7f) {
            return 0;
        }
    }

    return 1;
}


/* Is the header record r (len bytes, "MD" first) a valid description? */
static int
md_record_ok(const uint8_t *r, size_t len)
{
    size_t n = len - 2;

    if (n > 0 && r[len - 1] == 0) {
        n--;
    }

    if (n > DOC_MAX_DESC) {
        return 0;
    }

    for (size_t i = 0; i < n; i++) {
        if (r[2 + i] < 0x20 || r[2 + i] == 0x7f) {
            return 0;
        }
    }

    return 1;
}


int
doc_peek_description(const char *path, char *out, size_t cap)
{
    uint8_t b[16384];
    size_t n;
    FILE *fp = fopen(path, "rb");
    size_t i = 0;

    out[0] = '\0';

    if (fp == NULL) {
        return 0;
    }

    n = fread(b, 1, sizeof b, fp);
    fclose(fp);

    if (n < 3 || memcmp(b, "*R", 2) != 0) {
        return 0;
    }

    while (i < n) {
        size_t e = i;

        while (e + 1 < n && !(b[e] == '\r' && b[e + 1] == '\n')) {
            e++;
        }

        if (e + 1 >= n) {
            return 0;
        }

        if (e - i >= 2 && b[i] == '*' && b[i + 1] == 'B') {
            return 0;
        }

        if (e - i >= 2 && b[i] == 'M' && b[i + 1] == 'D') {
            size_t len = e - i;

            if (md_record_ok(b + i, len) == 0) {
                return 0;
            }

            len -= 2;

            if (len > 0 && b[i + 2 + len - 1] == 0) {
                len--;
            }

            if (len + 1 > cap) {
                return 0;
            }

            memcpy(out, b + i + 2, len);
            out[len] = '\0';

            return len > 0;
        }

        i = e + 2;
    }

    return 0;
}


/* ------------------------------------------------------ file format version */
/* The text of a value of the first record, safe to show in a message. */
static void
docver_value_text(const uint8_t *v, size_t n, char *out, size_t cap)
{
    size_t k = 0;

    for (size_t i = 0; i < n && k + 6 < cap; i++) {
        out[k++] = v[i] >= 0x20 && v[i] < 0x7f ? (char)v[i] : '?';
    }

    if (n > k) {
        out[k++] = '.';
        out[k++] = '.';
        out[k++] = '.';
    }

    out[k] = '\0';
}


int
docver_check(const uint8_t *b, size_t n, int *version, char *err,
             size_t errlen)
{
    size_t e = 2;
    size_t digits = 0;
    char shown[48];
    long v = 0;

    if (n < 2 || b[0] != '*' || b[1] != 'R') {
        snprintf(err, errlen, "The file is not a document of this software.");

        return -1;
    }

    /* The first record ends at the first CR or LF, or after 32 bytes. */
    while (e < n && e < 34 && b[e] != '\r' && b[e] != '\n') {
        e++;
    }

    while (2 + digits < e && b[2 + digits] >= '0' && b[2 + digits] <= '9') {
        digits++;
    }

    docver_value_text(b + 2, e - 2, shown, sizeof shown);

    /* A library file of version 2 has *R2h as its first record: lower case
       letters after the number are flags that are kept. */
    size_t flags = 0;

    while (2 + digits + flags < e && b[2 + digits + flags] >= 'a' &&
           b[2 + digits + flags] <= 'z') {
        flags++;
    }

    if (digits == 0 || 2 + digits + flags != e) {
        snprintf(err, errlen, "The file version is \"%s\".  This software "
                 "reads versions %d to %d.", shown, STRATA_FILE_FORMAT_MIN,
                 STRATA_FILE_FORMAT);

        return -1;
    }

    if (digits > 9) {
        char num[48];

        docver_value_text(b + 2, digits, num, sizeof num);
        snprintf(err, errlen, "The file is version %s.  This software reads "
                 "up to version %d.", num, STRATA_FILE_FORMAT);

        return -1;
    }

    for (size_t i = 0; i < digits; i++) {
        v = v * 10 + (b[2 + i] - '0');
    }

    if (v > STRATA_FILE_FORMAT) {
        snprintf(err, errlen, "The file is version %ld.  This software reads "
                 "up to version %d.", v, STRATA_FILE_FORMAT);

        return -1;
    }

    if (v < STRATA_FILE_FORMAT_MIN) {
        snprintf(err, errlen, "The file version is %ld.  This software reads "
                 "versions %d to %d.", v, STRATA_FILE_FORMAT_MIN,
                 STRATA_FILE_FORMAT);

        return -1;
    }

    if (version != NULL) {
        *version = (int)v;
    }

    return 0;
}


int
docver_file_version(const char *path)
{
    uint8_t b[64];
    char err[160];
    int v = 0;
    FILE *fp = fopen(path, "rb");
    size_t n;

    if (fp == NULL) {
        return -1;
    }

    n = fread(b, 1, sizeof b, fp);
    fclose(fp);

    return docver_check(b, n, &v, err, sizeof err) == 0 ? v : -1;
}


int
docver_special_name(const char *path, int version, char *out, size_t cap)
{
    const char *slash = strrchr(path, '/');
    size_t dir = slash != NULL ? (size_t)(slash - path) + 1 : 0;
    const char *name = path + dir;
    int w = snprintf(out, cap, "%.*sV%d-%s", (int)dir, path, version, name);

    return w < 0 || (size_t)w >= cap ? -1 : 0;
}


/* Copy the file src to the new file dst; an existing dst is never replaced. */
static int
docver_copy_new(const char *src, const char *dst, int *exists, char *why,
                size_t whylen)
{
    char tmp[PATH_MAX];
    struct stat st;
    FILE *in = fopen(src, "rb");
    int fd;
    FILE *out;
    uint8_t buf[65536];
    size_t k;
    int ok = 1;

    *exists = 0;

    if (in == NULL) {
        snprintf(why, whylen, "%s", strerror(errno));

        return -1;
    }

    if (fstat(fileno(in), &st) != 0) {
        snprintf(why, whylen, "%s", strerror(errno));
        fclose(in);

        return -1;
    }

    snprintf(tmp, sizeof tmp, "%s.sbk%ld", dst, (long)getpid());
    fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, st.st_mode & 0777);

    if (fd < 0) {
        snprintf(why, whylen, "%s", strerror(errno));
        fclose(in);

        return -1;
    }

    out = fdopen(fd, "wb");

    while (out != NULL && (k = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, k, out) != k) {
            ok = 0;
            break;
        }
    }

    if (out == NULL || ferror(in) != 0 || ok == 0 || fflush(out) != 0 ||
        fsync(fileno(out)) != 0 || fclose(out) != 0) {
        snprintf(why, whylen, "%s", strerror(errno != 0 ? errno : EIO));
        unlink(tmp);
        fclose(in);

        return -1;
    }

    fclose(in);

    /* link fails when the name exists, so the special backup is never
       replaced; the temporary name goes either way. */
    if (link(tmp, dst) != 0) {
        int e = errno;

        unlink(tmp);

        if (e == EEXIST) {
            *exists = 1;

            return 0;
        }

        snprintf(why, whylen, "%s", strerror(e));

        return -1;
    }

    unlink(tmp);

    return 0;
}


int
docver_special_backup(const char *path, int *made, char *err, size_t errlen)
{
    char name[PATH_MAX];
    char why[160];
    int exists = 0;
    int v = docver_file_version(path);

    if (made != NULL) {
        *made = 0;
    }

    if (v < STRATA_FILE_FORMAT_MIN || v >= STRATA_FILE_FORMAT) {
        return 0;
    }

    if (docver_special_name(path, v, name, sizeof name) != 0) {
        snprintf(err, errlen, "The special backup of %s cannot be written: "
                 "the name is too long", path);

        return -1;
    }

    if (docver_copy_new(path, name, &exists, why, sizeof why) != 0) {
        snprintf(err, errlen, "The special backup %s cannot be written: %s",
                 name, why);

        return -1;
    }

    if (made != NULL) {
        *made = exists == 0;
    }

    return 0;
}


/* The order of the M records in a header: description, author, revision. */
static int
header_rank(const char *code)
{
    return strcmp(code, "MD") == 0 ? 0 :
           strcmp(code, "MA") == 0 ? 1 :
           strcmp(code, "MR") == 0 ? 2 :
                                     9;
}


/* ------------------------------------------------------------ font names */
static const char *const default_font_names[] = {
    "Intro head", "Text", "Header/Footer", "Level 1 head", "Level 2 head"};


static void
default_font_name(int n, char *buf, size_t cap)
{
    if (n >= 1 && n <= 5) {
        snprintf(buf, cap, "%s", default_font_names[n - 1]);
    } else {
        snprintf(buf, cap, "Font %d", n);
    }
}


/*
 * The number of the DF record r, or 0 when it is not one; name_at is where
 * its name starts.
 */
static int
df_number(const Str *r, int *name_at)
{
    int v = 0;
    int i = 2;

    if (r->n < 4 || r->s[0] != 'D' || r->s[1] != 'F') {
        return 0;
    }

    for (; i < r->n && r->s[i] >= '0' && r->s[i] <= '9'; i++) {
        v = v * 10 + (r->s[i] - '0');
    }

    if (i < r->n && r->s[i] == 'N' && i > 2) {
        *name_at = i + 1;

        return v;
    }

    return 0;
}


void
doc_font_name(const Doc *d, int n, char *buf, size_t cap)
{
    const Lines *l = &d->header;

    for (int i = 0; i < l->n; i++) {
        int at;

        if (df_number(&l->v[i], &at) == n) {
            size_t k = 0;

            for (int j = at; j < l->v[i].n && l->v[i].s[j] != 0 &&
                             k + 1 < cap;
                 j++) {
                buf[k++] = chr(l->v[i].s[j]);
            }

            buf[k] = '\0';

            return;
        }
    }

    default_font_name(n, buf, cap);
}


void
doc_set_font_name(Doc *d, int n, const char *name)
{
    Lines *l = &d->header;
    char dflt[32];
    char cur[64];

    default_font_name(n, dflt, sizeof dflt);
    doc_font_name(d, n, cur, sizeof cur);

    if (strcmp(cur, name) == 0) {
        return;
    }

    int at = -1;

    for (int i = 0; i < l->n; i++) {
        int unused;

        if (df_number(&l->v[i], &unused) == n) {
            at = i;
        }
    }

    if (at >= 0) {
        free(l->v[at].s);

        for (int i = at; i + 1 < l->n; i++) {
            l->v[i] = l->v[i + 1];
        }

        l->n--;
    }

    doc_touch(d);

    if (strcmp(name, dflt) == 0) {
        return;
    }

    /* Before the first font of a higher number, else after the last font. */
    int pos = -1;
    int last_df = -1;
    int dp = -1;
    int first_y = -1;

    for (int i = 0; i < l->n; i++) {
        int unused;
        int k = df_number(&l->v[i], &unused);

        if (k > n && pos < 0) {
            pos = i;
        }

        if (k != 0) {
            last_df = i;
        }

        if (l->v[i].n >= 2 && l->v[i].s[0] == 'D' && l->v[i].s[1] == 'P') {
            dp = i;
        }

        if (l->v[i].n >= 1 && l->v[i].s[0] == 'Y' && first_y < 0) {
            first_y = i;
        }
    }

    if (pos < 0) {
        pos = last_df >= 0 ? last_df + 1 :
              dp >= 0      ? dp + 1 :
              first_y >= 0 ? first_y :
                             l->n - 1;
    }

    char rec[96];
    int len = snprintf(rec, sizeof rec, "DF%dN%s", n, name);

    /* The NUL that ends the name is part of the record. */
    lines_add(l, rec, usz(len) + 1);

    Str added = l->v[l->n - 1];

    for (int i = l->n - 1; i > pos; i--) {
        l->v[i] = l->v[i - 1];
    }

    l->v[pos] = added;
}


/* Set a record of the header; a text ends with a NUL byte, a number not. */
static void
header_set(Doc *d, const char *code, const char *val, int nul)
{
    Lines *l = &d->header;
    int at = -1;

    for (int i = 0; i < l->n; i++) {
        if (l->v[i].n >= 2 && l->v[i].s[0] == u8(code[0]) &&
            l->v[i].s[1] == u8(code[1])) {
            at = i;
            break;
        }
    }

    if (val == NULL || val[0] == '\0') {
        /* A text that is empty is not written at all. */
        if (at >= 0) {
            free(l->v[at].s);

            for (int i = at; i + 1 < l->n; i++) {
                l->v[i] = l->v[i + 1];
            }

            l->n--;
            doc_touch(d);
        }

        return;
    }

    size_t vl = strlen(val);
    size_t rl = 2 + vl + (nul != 0 ? 1 : 0);
    uint8_t *rec = xrealloc(NULL, rl + 1);

    rec[0] = u8(code[0]);
    rec[1] = u8(code[1]);
    memcpy(rec + 2, val, vl);
    if (nul != 0) {
        rec[rl - 1] = 0;
    }

    rec[rl] = 0;

    if (at >= 0) {
        free(l->v[at].s);
        l->v[at].s = rec;
        l->v[at].n = isz(rl);
        doc_touch(d);

        return;
    }

    /*
     * A new record goes among the M records in the order of the format:
     * description, author, revision.  With no M record it goes
     * before the *B record that ends the header.
     */
    int pos = l->n;
    int lastm = -1;
    int mine = header_rank(code);

    for (int i = 0; i < l->n; i++) {
        const Str *r = &l->v[i];

        if (r->n >= 2 && r->s[0] == 'M') {
            char rc[3] = {chr(r->s[0]), chr(r->s[1]), '\0'};

            lastm = i;

            if (header_rank(rc) > mine) {
                pos = i;
                break;
            }
        }
    }

    if (pos == l->n && lastm >= 0) {
        pos = lastm + 1;
    }

    for (int i = 0; lastm < 0 && i < l->n; i++) {
        if (l->v[i].n >= 2 && l->v[i].s[0] == '*' && l->v[i].s[1] == 'B') {
            pos = i;
            break;
        }
    }

    /* Make room: the new last entry is only a place holder. */
    lines_add(l, "", 0);
    free(l->v[l->n - 1].s);

    for (int i = l->n - 1; i > pos; i--) {
        l->v[i] = l->v[i - 1];
    }

    l->v[pos].s = rec;
    l->v[pos].n = isz(rl);
    doc_touch(d);
}


void
doc_header_set(Doc *d, const char *code, const char *val)
{
    header_set(d, code, val, 1);
}


void
doc_set_view_pos(Doc *d, int item, int col, int tok, int row)
{
    d->vp_set = 1;
    d->vp_item = item < 0 ? 0 : item;
    d->vp_col = col < 0 ? 0 : col;
    d->vp_tok = tok < 0 ? 0 : tok;
    d->vp_row = row < 0 ? 0 : row;
}


void
doc_clear_view_pos(Doc *d)
{
    d->vp_set = 0;
    d->vp_item = 0;
    d->vp_col = 0;
    d->vp_tok = 0;
    d->vp_row = 0;
}


int
doc_get_view_pos(const Doc *d, int *item, int *col, int *tok, int *row)
{
    if (d->vp_set == 0) {
        return 0;
    }

    *item = d->vp_item;
    *col = d->vp_col;
    *tok = d->vp_tok;
    *row = d->vp_row;

    return 1;
}


/*
 * Read a record of the counts of the markers: ZNF, ZNP, ZNT and ZNI followed
 * by the count of the figures, pictures, tables and the index entries, and
 * ZNX followed by the kinds whose counts are not exact, as the sum of 1, 2, 4
 * and 8.  Only plain decimal numbers of at most DOC_MAX_DIGITS digits are
 * taken; any other record is ignored, and the kind is counted when the load
 * is over.
 */
static void
parse_counts(Doc *d, const uint8_t *r, size_t len)
{
    unsigned v = 0;
    size_t digits = len - 3;

    if (digits == 0 || digits > DOC_MAX_DIGITS) {
        return;
    }

    for (size_t i = 3; i < len; i++) {
        if (r[i] < '0' || r[i] > '9') {
            return;
        }

        v = v * 10 + (unsigned)(r[i] - '0');
    }

    if (r[2] == 'X') {
        d->mk_stale = (uint8_t)(d->mk_stale | (v & 15u));

        return;
    }

    for (int k = 0; k < LK_COUNT; k++) {
        if (r[2] == (uint8_t)"FPTI"[k]) {
            d->mk_count[k] = v;
            d->mk_have = (uint8_t)(d->mk_have | (1u << k));
            d->mk_write = 1;
        }
    }
}


/*
 * The load is over: a kind whose count the file had and trusted is kept, and
 * every other kind is counted, once, in one pass over the document.
 */
static void
finish_counts(Doc *d)
{
    unsigned keep[LK_COUNT];
    unsigned need = 0;

    d->mk_loading = 0;

    for (int k = 0; k < LK_COUNT; k++) {
        if ((d->mk_have & (1u << k)) == 0 || (d->mk_stale & (1u << k)) != 0) {
            need |= 1u << k;
        }

        keep[k] = d->mk_count[k];
    }

    if (need == 0) {
        return;
    }

    doc_recount(d);

    for (int k = 0; k < LK_COUNT; k++) {
        if ((need & (1u << k)) == 0) {
            d->mk_count[k] = keep[k];
        }
    }
}


/*
 * Read the record ZP<item>,<col>,<tok>,<row> that holds the position of the
 * cursor.  Four plain decimal numbers of at most DOC_MAX_DIGITS digits each
 * are the only form that is taken; anything else is ignored, and the
 * document opens at the top.
 */
static void
parse_view_pos(Doc *d, const uint8_t *r, size_t len)
{
    int v[4] = {0, 0, 0, 0};
    size_t i = 2;

    for (int k = 0; k < 4; k++) {
        int digits = 0;

        while (i < len && r[i] >= '0' && r[i] <= '9') {
            if (digits >= DOC_MAX_DIGITS) {
                return;
            }

            v[k] = v[k] * 10 + (r[i] - '0');
            digits++;
            i++;
        }

        if (digits == 0) {
            return;
        }

        if (k < 3) {
            if (i >= len || r[i] != ',') {
                return;
            }

            i++;
        }
    }

    if (i != len) {
        return;
    }

    doc_set_view_pos(d, v[0], v[1], v[2], v[3]);
}


/* ------------------------------------------------------- named formats */
/*
 * Named block formats.  The header holds them after the F records as
 *
 *     FN<name>NUL  B records ...  FN<name>NUL  B records ...  FN NUL
 *
 * in alphabetical order; every FN record ends the definition before it, and
 * the last one, which has no name, ends the last definition.
 */
static int
is_fn(const Str *r)
{
    return r->n >= 2 && r->s[0] == 'F' && r->s[1] == 'N';
}


/* The name in an FN record; empty for the record that ends the list. */
static const char *
fn_name(const Str *r)
{
    return cstr(r->s) + 2;
}


static void
lines_insert_at(Lines *l, int pos, const void *s, size_t n)
{
    lines_add(l, s, n);

    Str added = l->v[l->n - 1];

    for (int i = l->n - 1; i > pos; i--) {
        l->v[i] = l->v[i - 1];
    }

    l->v[pos] = added;
}


static void
lines_remove_at(Lines *l, int pos)
{
    free(l->v[pos].s);

    for (int i = pos; i + 1 < l->n; i++) {
        l->v[i] = l->v[i + 1];
    }

    l->n--;
}


/* The records of definition a..b-1 of the named format called name. */
static int
named_range(const Lines *l, const char *name, int *a, int *b)
{
    for (int i = 0; i < l->n; i++) {
        if (is_fn(&l->v[i]) && fn_name(&l->v[i])[0] != '\0' &&
            strcmp(fn_name(&l->v[i]), name) == 0) {
            int j = i + 1;

            while (j < l->n && is_fn(&l->v[j]) == 0) {
                j++;
            }

            *a = i;
            *b = j;

            return 1;
        }
    }

    return 0;
}


int
doc_named_count(const Doc *d)
{
    int n = 0;

    for (int i = 0; i < d->header.n; i++) {
        if (is_fn(&d->header.v[i]) && fn_name(&d->header.v[i])[0] != '\0') {
            n++;
        }
    }

    return n;
}


void
doc_named_name(const Doc *d, int index, char *buf, size_t cap)
{
    int n = 0;

    buf[0] = '\0';

    for (int i = 0; i < d->header.n; i++) {
        if (is_fn(&d->header.v[i]) && fn_name(&d->header.v[i])[0] != '\0') {
            if (n == index) {
                str_copy(buf, cap, fn_name(&d->header.v[i]));

                return;
            }

            n++;
        }
    }
}


const char *
doc_named_get(const Doc *d, const char *name, const char *code)
{
    static char buf[128];
    int a;
    int b;

    if (named_range(&d->header, name, &a, &b) == 0) {
        return NULL;
    }

    for (int i = a + 1; i < b; i++) {
        int cl = isz(strlen(code));

        if (d->header.v[i].n >= cl &&
            memcmp(d->header.v[i].s, code, usz(cl)) == 0) {
            str_copy(buf, sizeof buf, cstr(d->header.v[i].s) + cl);

            return buf;
        }
    }

    return NULL;
}


void
doc_named_delete(Doc *d, const char *name)
{
    Lines *l = &d->header;
    int a;
    int b;

    if (named_range(l, name, &a, &b) == 0) {
        return;
    }

    for (int i = b - 1; i >= a; i--) {
        lines_remove_at(l, i);
    }

    /* A list with nothing left in it is the end record alone. */
    for (int i = 0; i < l->n; i++) {
        if (is_fn(&l->v[i])) {
            if (fn_name(&l->v[i])[0] == '\0') {
                lines_remove_at(l, i);
            }

            break;
        }
    }

    doc_touch(d);
}


int
doc_named_copy(Doc *dst, const Doc *src)
{
    int count = 0;

    for (int i = 0; i < src->header.n; i++) {
        const Str *r = &src->header.v[i];
        const char *recs[256];
        int nr = 0;

        if (is_fn(r) == 0 || fn_name(r)[0] == '\0') {
            continue;
        }

        for (int j = i + 1; j < src->header.n && is_fn(&src->header.v[j]) == 0 &&
                            nr < 256;
             j++) {
            recs[nr++] = cstr(src->header.v[j].s);
        }

        doc_named_set(dst, fn_name(r), recs, nr);
        count++;
    }

    return count;
}


void
doc_named_set(Doc *d, const char *name, const char *const *recs, int nrecs)
{
    Lines *l = &d->header;
    int pos = -1;
    int term = -1;
    int last_f = -1;
    int first_y = -1;
    char head[80];

    doc_named_delete(d, name);

    for (int i = 0; i < l->n; i++) {
        if (is_fn(&l->v[i])) {
            if (fn_name(&l->v[i])[0] == '\0') {
                term = i;
            } else if (pos < 0 && strcmp(fn_name(&l->v[i]), name) > 0) {
                pos = i;
            }
        } else if (l->v[i].n >= 2 && l->v[i].s[0] == 'F') {
            last_f = i;
        } else if (first_y < 0 && l->v[i].n >= 1 && l->v[i].s[0] == 'Y') {
            first_y = i;
        }
    }

    if (term < 0) {
        /* No list yet: start one after the F records. */
        int at = last_f >= 0 ? last_f + 1 : first_y >= 0 ? first_y : l->n;

        lines_insert_at(l, at, "FN", 3);
        term = at;
    }

    if (pos < 0) {
        pos = term;
    }

    int len = snprintf(head, sizeof head, "FN%s", name) + 1;

    lines_insert_at(l, pos, head, usz(len));

    for (int i = 0; i < nrecs; i++) {
        lines_insert_at(l, pos + 1 + i, recs[i], strlen(recs[i]));
    }

    doc_touch(d);
}


/* ------------------------------------------------------- other settings */
/*
 * The records of the header that the Marker Format panel sets, in the order
 * of the format: the page date and time formats after
 * the page number format, then the note type, the cross-reference format,
 * the display of hidden markers and the display mode of each marker after
 * the fonts.  Records of the same kind are ordered by the number that
 * follows the letters.
 */
static const char *const opt_order[] = {
    "MR", "PML", "PMR", "PMT", "PMB", "PHB", "PFB", "PHI", "PFI", "PHW",
    "PFW", "PP",
    "PD", "PT",  NULL};

/* After these come the records of the six page layouts, P1 to P6. */
static const char *const layout_order[] = {"ML", "MR", "MT", "MB", "HI", "FI",
                                           "HW", "FW", "HB", "FB", "C",  "P",
                                           "Q",  NULL};

/*
 * The records OKF, OKB, OKS and OKT of the Keycap Format come after OT.  Where
 * the format puts them among OC, OH, OM and OT is not known.
 */
static const char *const late_order[] = {"DP", "DF", "NT", "YC", "YI", "OC",
                                         "OH", "OM", "OT", "OKF", "OKB", "OKS",
                                         "OKT", NULL};


static int
opt_prefix(const uint8_t *s, int n, const char *p)
{
    int pl = isz(strlen(p));

    return n >= pl && memcmp(s, p, usz(pl)) == 0;
}


static int
opt_rank(const uint8_t *s, int n)
{
    if (n < 2) {
        return -1;
    }

    for (int k = 0; opt_order[k] != NULL; k++) {
        if (opt_prefix(s, n, opt_order[k])) {
            return k;
        }
    }

    if (s[0] == 'P' && s[1] >= '1' && s[1] <= '9') {
        int k = 0;

        for (; layout_order[k] != NULL; k++) {
            if (opt_prefix(s + 2, n - 2, layout_order[k])) {
                break;
            }
        }

        return 200 + (s[1] - '0') * 20 + k;
    }

    for (int k = 0; late_order[k] != NULL; k++) {
        if (opt_prefix(s, n, late_order[k])) {
            return 400 + k;
        }
    }

    return -1;
}


static int
opt_minor(const Str *r)
{
    int v = 0;

    for (int i = 2; i < r->n && r->s[i] >= '0' && r->s[i] <= '9'; i++) {
        v = v * 10 + (r->s[i] - '0');
    }

    return v;
}


static int
opt_match(const Str *r, const char *code)
{
    size_t cl = strlen(code);

    return r->n >= isz(cl) && memcmp(r->s, code, cl) == 0;
}


static int
hf_parse(const Str *s, int *layout, int *footer);

static int
hf_end(const Lines *l, int i);


const char *
doc_opt_get(const Doc *d, const char *code)
{
    static char buf[256];
    const Lines *l = &d->header;

    for (int i = 0; i < l->n; i++) {
        if (opt_match(&l->v[i], code)) {
            str_copy(buf, sizeof buf, cstr(l->v[i].s) + strlen(code));

            return buf;
        }
    }

    return NULL;
}


void
doc_opt_set(Doc *d, const char *code, const char *val, int nul)
{
    Lines *l = &d->header;

    for (int i = 0; i < l->n; i++) {
        if (opt_match(&l->v[i], code)) {
            free(l->v[i].s);

            for (int j = i; j + 1 < l->n; j++) {
                l->v[j] = l->v[j + 1];
            }

            l->n--;
            doc_touch(d);
            break;
        }
    }

    if (val == NULL || val[0] == '\0') {
        return;
    }

    char rec[300];
    int len = snprintf(rec, sizeof rec, "%s%s", code, val);

    if (nul != 0) {
        len++;
    }

    uint8_t bytes[300];
    Str mine;

    memcpy(bytes, rec, usz(len));
    mine.s = bytes;
    mine.n = len;

    int mr = opt_rank(mine.s, mine.n);
    int mm = opt_minor(&mine);
    int pos = -1;
    int after = -1;
    int bpos = l->n;

    for (int i = 0; i < l->n; i++) {
        int rk = opt_rank(l->v[i].s, l->v[i].n);

        if (l->v[i].n >= 2 && l->v[i].s[0] == '*' && l->v[i].s[1] == 'B') {
            bpos = i;
        }

        if (rk < 0) {
            continue;
        }

        if (rk > mr || (rk == mr && opt_minor(&l->v[i]) > mm)) {
            if (pos < 0) {
                pos = i;
            }
        } else {
            int ly;
            int ft;

            /* The blocks of a header or footer stay with its record. */
            after = hf_parse(&l->v[i], &ly, &ft) != 0 ? hf_end(l, i) - 1 : i;
        }
    }

    if (pos < 0) {
        pos = after >= 0 ? after + 1 : bpos;
    }

    lines_add(l, rec, usz(len));

    Str added = l->v[l->n - 1];

    for (int i = l->n - 1; i > pos; i--) {
        l->v[i] = l->v[i - 1];
    }

    l->v[pos] = added;
    doc_touch(d);
}


/* ------------------------------------------------- headers and footers */
typedef struct {
    uint8_t p[400];
    size_t n;
} Buf8;


/*
 * Is s the record of a header or footer (P<n>HB<k>, P<n>FB<k>, PHB, PFB)?
 * Then the layout and 0 for a header, 1 for a footer are set.
 */
static int
hf_parse(const Str *s, int *layout, int *footer)
{
    int p = 1;

    if (s->n < 3 || s->s[0] != 'P') {
        return 0;
    }

    *layout = 0;

    if (s->s[1] >= '1' && s->s[1] <= '6') {
        *layout = s->s[1] - '0';
        p = 2;
    }

    if (p + 1 >= s->n || (s->s[p] != 'H' && s->s[p] != 'F') ||
        s->s[p + 1] != 'B') {
        return 0;
    }

    for (int q = p + 2; q < s->n; q++) {
        if (s->s[q] < '0' || s->s[q] > '9') {
            return 0;
        }
    }

    *footer = s->s[p] == 'F';

    return 1;
}


/* The index of the record of the header or footer of a layout, or -1. */
static int
hf_find(const Lines *l, int layout, int footer)
{
    for (int i = 0; i < l->n; i++) {
        int ly;
        int ft;

        if (hf_parse(&l->v[i], &ly, &ft) != 0 && ly == layout &&
            ft == footer) {
            return i;
        }
    }

    return -1;
}


/* The first record after the blocks that follow record i. */
static int
hf_end(const Lines *l, int i)
{
    int j = i + 1;

    while (j < l->n && l->v[j].n > 0 &&
           (l->v[j].s[0] == 'S' || l->v[j].s[0] == 'B' ||
            l->v[j].s[0] == 'C' || l->v[j].s[0] == 'T')) {
        j++;
    }

    return j;
}


Doc *
doc_hf_doc(const Doc *d, int layout, int footer, int *own)
{
    static const char head[] = DOC_FIRST_RECORD "\r\nMR1006\r\n*B0\r\n";
    static const char foot[] = "*E\r\n";
    const Lines *l = &d->header;
    int i = hf_find(l, layout, footer);
    size_t cap;
    size_t n;
    uint8_t *buf;
    Doc *nd;
    char err[128];

    if (own != NULL) {
        *own = i >= 0;
    }

    if (i < 0 || hf_end(l, i) == i + 1) {
        return NULL;
    }

    cap = sizeof head + sizeof foot;

    for (int j = i + 1; j < hf_end(l, i); j++) {
        cap += usz(l->v[j].n) + 2;
    }

    buf = malloc(cap);

    if (buf == NULL) {
        return NULL;
    }

    n = sizeof head - 1;
    memcpy(buf, head, n);

    for (int j = i + 1; j < hf_end(l, i); j++) {
        memcpy(buf + n, l->v[j].s, usz(l->v[j].n));
        n += usz(l->v[j].n);
        buf[n++] = '\r';
        buf[n++] = '\n';
    }

    memcpy(buf + n, foot, sizeof foot - 1);
    n += sizeof foot - 1;
    nd = doc_load_mem(buf, n, err, sizeof err);
    free(buf);

    return nd;
}


void
doc_hf_get(const Doc *d, int layout, int footer, HfText *out)
{
    Doc *hd = doc_hf_doc(d, layout, footer, &out->own);
    int used[3] = {0, 0, 0};
    int blocks = 0;

    memset(out->part, 0, sizeof out->part);
    out->plain = 1;

    if (hd == NULL) {
        return;
    }

    for (int i = 0; i < hd->nitems; i++) {
        const Block *blk;

        if (hd->items[i].type != IT_BLOCK) {
            continue;
        }

        if (++blocks > 1) {
            out->plain = 0;
            break;
        }

        blk = hd->items[i].blk;

        for (int k = 0; k < blk->ncols; k++) {
            const char *j = props_get(&blk->cols[k].props, "CF", 'J');
            const Text *t = &blk->cols[k].text;
            int part;
            size_t at = 0;
            char *o;

            if (j == NULL) {
                j = props_get(&blk->props, "BF", 'J');
            }

            part = j == NULL ? 0 : atoi(j) == 2 ? 1 : atoi(j) == 1 ? 2 : 0;
            o = out->part[part];

            if (used[part]++ != 0) {
                out->plain = 0;
            }

            for (int q = 0; q < t->n; q++) {
                const Tok *tk = &t->v[q];
                const char *m = NULL;

                if (tk->kind == TK_NL && q == t->n - 1) {
                    continue;
                }

                if (tk->kind == TK_CHAR && tk->a >= 0x20 && tk->a < 0x7f &&
                    tk->a != '|') {
                    if (at + 1 < sizeof out->part[0]) {
                        o[at++] = (char)tk->a;
                    }

                    continue;
                }

                if (tk->kind == TK_MARKER && tok_mtype(tk) == 22) {
                    m = "\\page\\";
                } else if (tk->kind == TK_MARKER && tok_mtype(tk) == 28) {
                    MPayload mp;
                    const char *sh;

                    mp_parse(&mp, marker_info(28), tok_mdata(tk),
                             tok_mlen(tk));
                    sh = mp_get(&mp, "S");
                    m = sh != NULL && strcmp(sh, "2") == 0 ?
                            "\\sectionnumber\\" :
                        sh != NULL && strcmp(sh, "3") == 0 ?
                            "\\sectiontitle\\" : "\\section\\";
                    mp_free(&mp);
                }

                if (m == NULL) {
                    out->plain = 0;

                    if (at + 1 < sizeof out->part[0]) {
                        o[at++] = '?';
                    }

                    continue;
                }

                for (const char *c = m; *c != '\0'; c++) {
                    if (at + 1 < sizeof out->part[0]) {
                        o[at++] = *c;
                    }
                }
            }
        }
    }

    doc_free(hd);
}


int
doc_level_indent(const Doc *d, int level)
{
    int sum = 0;

    for (int l = 1; l < level; l++) {
        const char *v = doc_fmt_get(d, l, "I");

        if (v == NULL || v[0] == '\0') {
            v = doc_fmt_get(d, 0, "I");
        }

        if (v != NULL && atoi(v) > 0) {
            sum += atoi(v);
        }
    }

    return sum;
}


void
doc_hf_clear(Doc *d, int layout, int footer, int none)
{
    Lines *l = &d->header;
    int i = hf_find(l, layout, footer);
    char code[16];

    if (i >= 0) {
        int e = hf_end(l, i);

        while (e > i) {
            lines_remove_at(l, --e);
        }

        doc_touch(d);
    }

    if (none == 0) {
        return;
    }

    /* The record with no block: nothing is printed on that layout. */
    if (layout > 0) {
        snprintf(code, sizeof code, "P%d%cB", layout, footer != 0 ? 'F' : 'H');
    } else {
        snprintf(code, sizeof code, "P%cB", footer != 0 ? 'F' : 'H');
    }

    doc_opt_set(d, code, "0", 0);

    if (layout == 0) {
        /* The Global records have no count. */
        i = hf_find(l, layout, footer);

        if (i >= 0) {
            l->v[i].n = 3;
            l->v[i].s[3] = '\0';
        }
    }
}


/* A part as the bytes of a text record: the text, then the end of line. */
static void
hf_text_record(Buf8 *b, const char *text)
{
    b->n = 0;
    b->p[b->n++] = 'T';

    for (const char *q = text; *q != '\0' && b->n + 12 < sizeof b->p; q++) {
        if (strncmp(q, "\\page\\", 6) == 0) {
            memcpy(b->p + b->n, "\x13" "22(\x13)", 6);
            b->n += 6;
            q += 5;
        } else if (strncmp(q, "\\sectionnumber\\", 15) == 0 ||
                   strncmp(q, "\\sectiontitle\\", 14) == 0) {
            int num = strncmp(q, "\\sectionnumber\\", 15) == 0;
            const char *rec = num != 0 ? "S2\r\n" : "S3\r\n";

            memcpy(b->p + b->n, "\x13" "28(", 4);
            b->n += 4;
            memcpy(b->p + b->n, rec, 4);
            b->n += 4;
            memcpy(b->p + b->n, "\x13)", 2);
            b->n += 2;
            q += num != 0 ? 14 : 13;
        } else if (strncmp(q, "\\section\\", 9) == 0) {
            memcpy(b->p + b->n, "\x13" "28(\x13)", 6);
            b->n += 6;
            q += 8;
        } else if ((unsigned char)*q >= 0x20 && (unsigned char)*q < 0x7f) {
            b->p[b->n++] = (uint8_t)*q;
        }
    }

    b->p[b->n++] = 0x1d;
    b->p[b->n++] = 0;
}


void
doc_hf_set(Doc *d, int layout, int footer, const char *left,
           const char *centre, const char *right)
{
    Lines *l;
    int i;
    char code[16];
    const char *parts[3];
    static const char *const just[3] = {"CFJ0", "CFJ2", "CFJ1"};
    int at;

    parts[0] = left;
    parts[1] = centre;
    parts[2] = right;
    doc_hf_clear(d, layout, footer, 0);

    if (layout > 0) {
        snprintf(code, sizeof code, "P%d%cB", layout, footer != 0 ? 'F' : 'H');
    } else {
        snprintf(code, sizeof code, "P%cB", footer != 0 ? 'F' : 'H');
    }

    doc_opt_set(d, code, "1", 0);
    l = &d->header;
    i = hf_find(l, layout, footer);

    if (i < 0) {
        return;
    }

    if (layout == 0) {
        l->v[i].n = 3;
        l->v[i].s[3] = '\0';
    }

    at = i + 1;
    lines_insert_at(l, at++, "S", 1);

    for (int k = 0; k < 3; k++) {
        Buf8 b;

        if (k < 2) {
            lines_insert_at(l, at++, "CW26", 4);
        }

        lines_insert_at(l, at++, just[k], 4);
        hf_text_record(&b, parts[k] != NULL ? parts[k] : "");
        lines_insert_at(l, at++, b.p, b.n);
    }

    doc_touch(d);
}



/* ------------------------------------------------------- format settings */
/*
 * The order of the F records of a header in the format: first
 * the global ones, then those of level 1, level 2 and so on.  Within a
 * scope a record has the rank of its code in these lists.
 */
static const char *const global_fmt_codes[] = {
    "HA", "HJ", "HI", "HF", "HT", "TA", "TJ", "TI", "TF", "TT",
    "SL", "SH", "ST", "SS", "SC", "U",  "I",  "LG", "LP", "LA",
    "LN", "B",  "P",  "WN", "WB", "WG", "WR", "ZM", "ZE", "ZS", "ZZ", "ZY",
    "ZF", "ZD", "ZQ", "ZLF", "ZLP", "ZLT", "ZLI", NULL};

static const char *const level_fmt_codes[] = {
    "HA", "HJ", "HI", "HF", "HT", "TA", "TJ", "TI", "TF", "TT", "I",
    "SL", "SH", "ST", "SS", "SC", "B",  "WN", "WB", "WG", "WR", "G",
    NULL};


/*
 * Match record r against the F record with the given scope and code.
 * Returns the offset of its value, or 0 when it is another record.
 */
static int
fmt_match(const Str *r, int level, const char *code)
{
    int at = 1;
    int cl = isz(strlen(code));

    if (r->n < 2 || r->s[0] != 'F') {
        return 0;
    }

    if (level > 0) {
        if (r->s[1] != u8('0' + level)) {
            return 0;
        }

        at = 2;
    } else if (r->s[1] >= '0' && r->s[1] <= '9') {
        return 0;
    }

    if (r->n < at + cl || memcmp(r->s + at, code, usz(cl)) != 0) {
        return 0;
    }

    return at + cl;
}


/* The rank of a record among the F records, or -1 when it has none. */
static int
fmt_rank(const Str *r)
{
    for (int level = 0; level <= 5; level++) {
        const char *const *list =
            level == 0 ? global_fmt_codes : level_fmt_codes;

        for (int k = 0; list[k] != NULL; k++) {
            if (fmt_match(r, level, list[k]) != 0) {
                return level * 100 + k;
            }
        }
    }

    return -1;
}


const char *
doc_fmt_get(const Doc *d, int level, const char *code)
{
    static char buf[256];
    const Lines *l = &d->header;

    for (int i = 0; i < l->n; i++) {
        int at = fmt_match(&l->v[i], level, code);

        if (at != 0) {
            str_copy(buf, sizeof buf, cstr(l->v[i].s) + at);

            return buf;
        }
    }

    return NULL;
}


void
doc_fmt_set(Doc *d, int level, const char *code, const char *val)
{
    Lines *l = &d->header;
    const char *const *list = level == 0 ? global_fmt_codes : level_fmt_codes;
    int mine = -1;
    int at = -1;

    for (int k = 0; list[k] != NULL; k++) {
        if (strcmp(list[k], code) == 0) {
            mine = level * 100 + k;
        }
    }

    for (int i = 0; i < l->n; i++) {
        if (fmt_match(&l->v[i], level, code) != 0) {
            at = i;
            break;
        }
    }

    if (at >= 0) {
        free(l->v[at].s);

        for (int i = at; i + 1 < l->n; i++) {
            l->v[i] = l->v[i + 1];
        }

        l->n--;
        doc_touch(d);
    }

    if (val == NULL || val[0] == '\0') {
        return;
    }

    /* The section tag is a text, so a NUL byte ends it. */
    char rec[300];
    int len;

    if (level > 0) {
        len = snprintf(rec, sizeof rec, "F%d%s%s", level, code, val);
    } else {
        len = snprintf(rec, sizeof rec, "F%s%s", code, val);
    }

    if (strcmp(code, "G") == 0) {
        len++;
    }

    int pos = -1;
    int last = -1;
    int first_y = -1;

    for (int i = 0; i < l->n; i++) {
        int rk = fmt_rank(&l->v[i]);

        if (rk >= 0) {
            last = i;

            if (rk > mine && pos < 0) {
                pos = i;
            }
        }

        if (first_y < 0 && l->v[i].n >= 1 && l->v[i].s[0] == 'Y') {
            first_y = i;
        }
    }

    if (pos < 0) {
        pos = last >= 0 ? last + 1 : first_y >= 0 ? first_y : l->n;
    }

    lines_add(l, rec, usz(len));

    Str added = l->v[l->n - 1];

    for (int i = l->n - 1; i > pos; i--) {
        l->v[i] = l->v[i - 1];
    }

    l->v[pos] = added;
    doc_touch(d);
}


int
doc_revision(const Doc *d)
{
    const char *v = props_get(&d->header, "MR", 0);

    return v != NULL ? atoi(v) : 1000;
}


void
doc_set_revision(Doc *d, int rev)
{
    char num[16];

    snprintf(num, sizeof num, "%d", rev);
    header_set(d, "MR", num, 0);
}


/* ------------------------------------------------------------------ text */
void
text_init(Text *t)
{
    t->v = NULL;
    t->n = t->cap = 0;
}


void
text_free(Text *t)
{
    for (int i = 0; i < t->n; i++) {
        tok_release(&t->v[i]);
    }

    free(t->v);
    text_init(t);
}


Tok
tok_char(uint8_t c)
{
    Tok t;

    memset(&t, 0, sizeof t);
    t.kind = TK_CHAR;
    t.a = c;

    return t;
}


Tok
tok_nbsp(void)
{
    Tok t;

    memset(&t, 0, sizeof t);
    t.kind = TK_CTRL;
    t.a = NBSP_CTRL;

    return t;
}


Tok
tok_shy(void)
{
    Tok t;

    memset(&t, 0, sizeof t);
    t.kind = TK_CTRL;
    t.a = SHY_CTRL;

    return t;
}


/* ----------------------------------------------------------- marker table */
/*
 * The payloads of all marker tokens.  An entry that is free is linked into
 * a list through its len field; its data is NULL.  The number of an entry
 * has to fit in the 24 bits of three bytes of a token.
 */
typedef struct {
    uint8_t *data;
    size_t len; /* The payload length, or the next free entry when free. */
    int type;
} MEntry;

#define MENTRY_NONE 0xFFFFFFu
static MEntry *mtab;
static size_t mtab_n;
static size_t mtab_cap;
static size_t mtab_free = MENTRY_NONE;
static size_t mtab_live; /* entries that hold a marker now */


static size_t
mentry_of(const Tok *k)
{
    size_t i = k->esc;

    i = (i << 8) | k->a;

    return (i << 8) | k->b;
}


static void
mentry_set(Tok *k, size_t i)
{
    k->esc = (i >> 16) & 0xFFu;
    k->a = (i >> 8) & 0xFFu;
    k->b = i & 0xFFu;
}


static size_t
mentry_new(void)
{
    size_t i;

    mtab_live++;

    if (mtab_free != MENTRY_NONE) {
        i = mtab_free;
        mtab_free = mtab[i].len;

        return i;
    }

    if (mtab_n >= MENTRY_NONE) {
        log_fatal(1, "msdoc: too many markers in the document: %zu are open "
                  "and at most %u can be", mtab_n, MENTRY_NONE);
    }

    if (mtab_n == mtab_cap) {
        mtab_cap = mtab_cap != 0 ? mtab_cap * 2 : 64;
        mtab = xrealloc(mtab, mtab_cap * sizeof *mtab);
    }

    return mtab_n++;
}


static void
mentry_fill(size_t i, const uint8_t *data, size_t len)
{
    mtab[i].data = xrealloc(mtab[i].data, len + 1);

    if (len != 0) {
        memcpy(mtab[i].data, data, len);
    }

    mtab[i].data[len] = 0;
    mtab[i].len = len;
}


size_t
tok_marker_live(void)
{
    return mtab_live;
}


Tok
tok_marker(int type, const uint8_t *data, size_t len)
{
    Tok k;
    size_t i = mentry_new();

    memset(&k, 0, sizeof k);
    k.kind = TK_MARKER;
    mtab[i].data = NULL;
    mtab[i].type = type;
    mentry_fill(i, data, len);
    mentry_set(&k, i);

    return k;
}


void
tok_marker_set(Tok *k, const uint8_t *data, size_t len)
{
    /* The data may point into the entry itself, so copy before freeing. */
    size_t i = mentry_of(k);
    uint8_t *old = mtab[i].data;

    mtab[i].data = NULL;
    mentry_fill(i, data, len);
    free(old);
}


int
tok_mtype(const Tok *k)
{
    return mtab[mentry_of(k)].type;
}


const uint8_t *
tok_mdata(const Tok *k)
{
    return mtab[mentry_of(k)].data;
}


size_t
tok_mlen(const Tok *k)
{
    return mtab[mentry_of(k)].len;
}


void
tok_release(Tok *k)
{
    if (k->kind == TK_MARKER) {
        size_t i = mentry_of(k);

        free(mtab[i].data);
        mtab[i].data = NULL;
        mtab[i].len = mtab_free;
        mtab_free = i;

        if (mtab_live != 0) {
            mtab_live--;
        }
    }
}


void
text_trim(Text *t)
{
    if (t->cap > t->n) {
        t->cap = t->n;
        t->v = xrealloc(t->v, usz(t->cap) * sizeof *t->v);
    }
}


static void
text_push(Text *t, Tok k)
{
    if (t->n == t->cap) {
        t->cap = t->cap != 0 ? t->cap * 2 : 16;
        t->v = xrealloc(t->v, usz(t->cap) * sizeof *t->v);
    }

    t->v[t->n++] = k;
}


int
text_insert(Text *t, int pos, Tok k)
{
    if (pos < 0 || pos > t->n) {
        return -1;
    }

    text_push(t, k);
    memmove(&t->v[pos + 1], &t->v[pos], usz((t->n - 1 - pos)) * sizeof(Tok));
    t->v[pos] = k;

    return 0;
}


void
text_delete(Text *t, int pos, int n)
{
    if (pos < 0 || pos >= t->n) {
        return;
    }

    if (pos + n > t->n) {
        n = t->n - pos;
    }

    for (int i = 0; i < n; i++) {
        tok_release(&t->v[pos + i]);
    }

    memmove(&t->v[pos], &t->v[pos + n], usz((t->n - pos - n)) * sizeof(Tok));
    t->n -= n;
}


int
text_plain(const Text *t, char *buf, int cap)
{
    int o = 0;

    for (int i = 0; i < t->n && o < cap - 1; i++) {
        const Tok *k = &t->v[i];

        if (k->kind == TK_CHAR) {
            buf[o++] = chr(k->a);
        } else if (k->kind == TK_TAB) {
            buf[o++] = ' ';
        } else if (k->kind == TK_NL) {
            break;
        }
    }

    buf[o] = 0;

    return o;
}


/* ------------------------------------------------- record scan/tokenize */
/* Scan T-record payload starting at i; returns index just past the
 * terminating NUL (depth 0), or SIZE_MAX if the buffer ends first.  The same
 * operand rules are used by tokenize(). */
size_t
msdoc_scan_text(const uint8_t *b, size_t n, size_t i)
{
    int depth = 0;

    while (i < n) {
        uint8_t c = b[i];

        if (c == 0 && depth == 0) {
            return i + 1;
        }

        if (c == 0x01) {
            i += 2;
            continue;
        }

        if (c == 0x10 || c == 0x12) {
            i += 3;
            continue;
        }

        if (c == 0x13) {
            if (i + 1 < n && b[i + 1] == ')') {
                if (depth > 0) {
                    depth--;
                }

                i += 2;
                continue;
            }

            size_t j = i + 1;

            while (j < n && is_digit(b[j])) {
                j++;
            }

            if (j > i + 1 && j - i - 1 <= DOC_MAX_MTYPE_DIGITS && j < n &&
                b[j] == '(') {
                depth++;
                i = j + 1;
                continue;
            }

            i++;
            continue;
        }

        i++;
    }

    return SIZE_MAX;
}


/*
 * Tokenize payload b[0..n) (excluding the final NUL).  Returns false when
 * the text holds more markers than the program can keep; the tokens made
 * so far stay in t, which the caller frees.
 */
static bool
tokenize(Text *t, const uint8_t *b, size_t n)
{
    size_t i = 0;

    while (i < n) {
        uint8_t c = b[i];
        Tok k;

        memset(&k, 0, sizeof k);

        if (c == 0x01 && i + 1 < n) {
            k.kind = TK_CHAR;
            k.esc = 1;
            k.a = b[i + 1];
            i += 2;
        } else if (c == 0x10 && i + 2 < n + 0) {
            k.kind = TK_TAB;
            k.a = b[i + 1];
            k.b = b[i + 2];
            i += 3;
        } else if (c == 0x12 && i + 2 < n + 0) {
            k.kind = TK_ATTR;
            k.a = b[i + 1];
            k.b = b[i + 2];
            i += 3;
        } else if (c == 0x13) {
            /* marker open: 13 <digits> ( payload 13 ) */
            size_t j = i + 1;
            int ty = 0;

            while (j < n && is_digit(b[j])) {
                /* Only a number of few digits is a type; it cannot overflow. */
                if (j - i - 1 < DOC_MAX_MTYPE_DIGITS) {
                    ty = ty * 10 + (b[j] - '0');
                }

                j++;
            }

            if (j > i + 1 && j - i - 1 <= DOC_MAX_MTYPE_DIGITS && j < n &&
                b[j] == '(') {
                if (tok_marker_live() >= DOC_MAX_MARKERS) {
                    return false;
                }

                size_t p0 = j + 1;
                size_t p = p0;
                int depth = 1;

                while (p < n && depth != 0) {
                    uint8_t d = b[p];

                    if (d == 0x01) {
                        p += 2;
                        continue;
                    }

                    if (d == 0x10 || d == 0x12) {
                        p += 3;
                        continue;
                    }

                    if (d == 0x13) {
                        if (p + 1 < n && b[p + 1] == ')') {
                            depth--;

                            if (depth == 0) {
                                break;
                            }

                            p += 2;
                            continue;
                        }

                        size_t q = p + 1;

                        while (q < n && is_digit(b[q])) {
                            q++;
                        }

                        if (q > p + 1 && q - p - 1 <= DOC_MAX_MTYPE_DIGITS &&
                            q < n && b[q] == '(') {
                            depth++;
                            p = q + 1;
                            continue;
                        }
                    }

                    p++;
                }

                k = tok_marker(ty, b + p0, p - p0);
                i = (p < n) ? p + 2 : n;
            } else {
                k.kind = TK_CTRL;
                k.a = c;
                i++;
            } /* stray 0x13 */
        } else if (c == 0x1d) {
            k.kind = TK_NL;
            i++;
        } else if (c < 0x20) {
            k.kind = TK_CTRL;
            k.a = c;
            i++;
        } else {
            k.kind = TK_CHAR;
            k.a = c;
            i++;
        }

        text_push(t, k);
    }

    return true;
}

typedef struct {
    uint8_t *b;
    size_t n;
    size_t cap;
} Buf;


static void
buf_put(Buf *o, const void *p, size_t n)
{
    if (o->n + n > o->cap) {
        size_t grown = o->cap + o->cap / 2 + 64;

        o->cap = grown > o->n + n ? grown : o->n + n;
        o->b = xrealloc(o->b, o->cap);
    }

    memcpy(o->b + o->n, p, n);
    o->n += n;
}


static void
buf_c(Buf *o, uint8_t c)
{
    buf_put(o, &c, 1);
}


static void
emit_text(Buf *o, const Text *t)
{
    buf_c(o, 'T');

    for (int i = 0; i < t->n; i++) {
        const Tok *k = &t->v[i];

        switch (k->kind) {
            case TK_CHAR:
                if (k->esc != '\0') {
                    buf_c(o, 0x01);
                }
                buf_c(o, k->a);
                break;

            case TK_TAB:
                buf_c(o, 0x10);
                buf_c(o, k->a);
                buf_c(o, k->b);
                break;

            case TK_ATTR:
                buf_c(o, 0x12);
                buf_c(o, k->a);
                buf_c(o, k->b);
                break;

            case TK_NL:
                buf_c(o, 0x1d);
                break;

            case TK_CTRL:
                buf_c(o, k->a);
                break;

            case TK_MARKER: {
                char h[24];
                int hl = snprintf(h, sizeof h, "%c%d(", 0x13, tok_mtype(k));

                buf_put(o, h, usz(hl));
                buf_put(o, tok_mdata(k), tok_mlen(k));
                buf_c(o, 0x13);
                buf_c(o, ')');
                break;
            }

            default:
                break;
        }
    }

    buf_c(o, 0);
    buf_put(o, "\r\n", 2);
}


/* -------------------------------------------------------------- structure */
Block *
block_new(int ncols)
{
    Block *b = calloc(1, sizeof *b);

    for (int i = 0; i < ncols; i++) {
        block_add_column(b);
    }

    return b;
}


Column *
block_add_column(Block *b)
{
    if (b->ncols == b->capcols) {
        b->capcols = b->capcols != 0 ? b->capcols * 2 : 2;
        b->cols = xrealloc(b->cols, usz(b->capcols) * sizeof *b->cols);
    }

    Column *c = &b->cols[b->ncols++];

    memset(c, 0, sizeof *c);

    return c;
}


void
block_free(Block *b)
{
    if (b == NULL) {
        return;
    }

    lines_free(&b->props);

    for (int i = 0; i < b->ncols; i++) {
        lines_free(&b->cols[i].props);
        text_free(&b->cols[i].text);
    }

    free(b->cols);
    free(b);
}


Block *
block_clone_format(const Block *s)
{
    Block *b = block_new(0);

    for (int i = 0; i < s->props.n; i++) {
        lines_add(&b->props, s->props.v[i].s, usz(s->props.v[i].n));
    }

    for (int c = 0; c < s->ncols; c++) {
        Column *d = block_add_column(b);

        d->width = s->cols[c].width;

        for (int i = 0; i < s->cols[c].props.n; i++) {
            lines_add(&d->props, s->cols[c].props.v[i].s,
                      usz(s->cols[c].props.v[i].n));
        }

        /* every column holds at least an end-of-line */
        Tok nl;

        memset(&nl, 0, sizeof nl);
        nl.kind = TK_NL;
        text_insert(&d->text, 0, nl);
    }

    return b;
}


int
doc_insert_item(Doc *d, int at, int type, Block *blk)
{
    if (d->nitems == d->capitems) {
        d->capitems = d->capitems != 0 ? d->capitems * 2 : 64;
        d->items = xrealloc(d->items, usz(d->capitems) * sizeof *d->items);
    }

    if (at < 0 || at > d->nitems) {
        at = d->nitems;
    }

    memmove(&d->items[at + 1], &d->items[at],
            usz((d->nitems - at)) * sizeof(Item));
    d->items[at].type = type;
    d->items[at].blk = blk;
    d->items[at].fold = 0;
    d->items[at].se = 0;
    d->nitems++;

    if (type == IT_BLOCK && blk != NULL) {
        doc_count_block(d, blk, 1);
    }

    doc_touch(d);

    return at;
}


void
doc_move_items(Doc *d, int lo, int hi, int to)
{
    int n = hi - lo + 1;
    Item *tmp;

    if (n <= 0 || lo < 0 || hi >= d->nitems || to < 0 ||
        to > d->nitems - n || to == lo) {
        return;
    }

    tmp = xrealloc(NULL, usz(n) * sizeof *tmp);
    memcpy(tmp, &d->items[lo], usz(n) * sizeof *tmp);
    memmove(&d->items[lo], &d->items[hi + 1],
            usz(d->nitems - hi - 1) * sizeof *tmp);
    memmove(&d->items[to + n], &d->items[to],
            usz(d->nitems - n - to) * sizeof *tmp);
    memcpy(&d->items[to], tmp, usz(n) * sizeof *tmp);
    free(tmp);
    doc_touch(d);
}


void
doc_delete_items(Doc *d, int at, int n)
{
    if (at < 0 || at >= d->nitems) {
        return;
    }

    if (at + n > d->nitems) {
        n = d->nitems - at;
    }

    for (int i = 0; i < n; i++) {
        if (d->items[at + i].type == IT_BLOCK && d->items[at + i].blk != NULL) {
            doc_count_block(d, d->items[at + i].blk, -1);
        }

        block_free(d->items[at + i].blk);
    }

    memmove(&d->items[at], &d->items[at + n],
            usz(d->nitems - at - n) * sizeof(Item));
    d->nitems -= n;
    doc_touch(d);
}


/* ---------------------------------------------------- marker counters */
unsigned long doc_count_tokens_read;
unsigned long doc_count_recounts;


int
doc_marker_kind(int mtype)
{
    switch (mtype) {
        case 11:
            return LK_FIGURE;

        case 25:
            return LK_PICTURE;

        case 29:
            return LK_TABLE;

        case 3:
            return LK_INDEX;

        default:
            return -1;
    }
}


void
doc_count_marker(Doc *d, int mtype, int sign)
{
    int k = doc_marker_kind(mtype);

    if (k < 0 || d->mk_loading != 0) {
        return;
    }

    d->mk_write = 1;

    if (sign > 0) {
        if (d->mk_count[k] >= LIST_COUNT_MAX) {
            /* The count stays at the largest value and is no longer exact. */
            d->mk_stale = (uint8_t)(d->mk_stale | (1u << k));
        } else {
            d->mk_count[k]++;
        }
    } else if (d->mk_count[k] == 0) {
        /* A marker went out that was never counted: the count is wrong. */
        d->mk_stale = (uint8_t)(d->mk_stale | (1u << k));
    } else {
        d->mk_count[k]--;
    }
}


void
doc_count_text(Doc *d, const Text *t, int from, int to, int sign)
{
    if (from < 0) {
        from = 0;
    }

    if (to > t->n) {
        to = t->n;
    }

    for (int i = from; i < to; i++) {
        doc_count_tokens_read++;

        if (t->v[i].kind == TK_MARKER) {
            doc_count_marker(d, tok_mtype(&t->v[i]), sign);
        }
    }
}


void
doc_count_block(Doc *d, const Block *b, int sign)
{
    if (d->mk_loading != 0) {
        return;
    }

    for (int c = 0; c < b->ncols; c++) {
        doc_count_text(d, &b->cols[c].text, 0, b->cols[c].text.n, sign);
    }
}


void
doc_recount(Doc *d)
{
    int was = d->mk_loading;
    int wrote = d->mk_write;

    doc_count_recounts++;
    memset(d->mk_count, 0, sizeof d->mk_count);
    d->mk_stale = 0;
    d->mk_loading = 0;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == IT_BLOCK && d->items[i].blk != NULL) {
            doc_count_block(d, d->items[i].blk, 1);
        }
    }

    d->mk_loading = was;
    d->mk_write = wrote;
}


void
doc_count_forget(Doc *d)
{
    memset(d->mk_count, 0, sizeof d->mk_count);
    d->mk_stale = 0;
    d->mk_have = 0;
    d->mk_write = 0;
}


unsigned
doc_marker_count(const Doc *d, int kind)
{
    return kind >= 0 && kind < LK_COUNT ? d->mk_count[kind] : 0;
}


int
doc_marker_stale(const Doc *d, int kind)
{
    return kind >= 0 && kind < LK_COUNT && (d->mk_stale & (1u << kind)) != 0;
}


static const char *const list_codes[LK_COUNT] = {"ZLF", "ZLP", "ZLT", "ZLI"};


int
doc_list_setting(const Doc *d, int kind)
{
    const char *v;

    if (kind < 0 || kind >= LK_COUNT) {
        return -1;
    }

    v = doc_fmt_get(d, 0, list_codes[kind]);

    if (v == NULL || (v[0] != '0' && v[0] != '1')) {
        return -1;
    }

    return v[0] == '1';
}


void
doc_list_set(Doc *d, int kind, int setting)
{
    if (kind < 0 || kind >= LK_COUNT) {
        return;
    }

    doc_fmt_set(d, 0, list_codes[kind],
                setting < 0 ? NULL : setting == 0 ? "0" : "1");
}


int
doc_list_on(const Doc *d, int kind)
{
    int s = doc_list_setting(d, kind);

    if (s >= 0) {
        return s;
    }

    return doc_marker_count(d, kind) != 0;
}


int
doc_contents_level(const Doc *d)
{
    const char *v = props_get(&d->header, "YC", 'L');
    int n;

    if (v == NULL || v[0] < '0' || v[0] > '9') {
        return 99;
    }

    n = atoi(v);

    return n > 99 ? 99 : n;
}


/*
 * The sizes of the envelopes of the paper list and of the envelope
 * templates.  The Printer takes the type size of a document whose page has
 * one of these sizes, and the Editor takes it when the paper is chosen, so
 * that the print of an envelope made in the Editor has the same size of
 * type as the print of a template.  An envelope needs larger type than a
 * letter: every size has 14 points; a template may name another size in its
 * own record PS (the German DL window envelope has 11, the European C5
 * has 12).
 */
static const struct {
    int w;
    int h;
    int points;
} envelope_sizes[] = {{3875, 7500, 14},   /* Monarch, number 7 3/4. */
                      {4125, 9500, 14},   /* Number 10. */
                      {3875, 8875, 14},   /* Number 9. */
                      {4331, 8661, 14},   /* DL. */
                      {6378, 9016, 14},   /* C5. */
                      {4488, 9016, 14},   /* C6/5. */
                      {4488, 6378, 14},   /* C6, Yougata 2. */
                      {4134, 9252, 14}};  /* Yougata 4. */

/* How far a page may differ from an envelope of the table, in thousandths of
   an inch, on each side: the first rule matched sizes exactly and missed a
   business envelope that measures a quarter inch more than the list says. */
#define ENVELOPE_TOLERANCE 300

/* The proportions of any envelope that is not in the table: the long side is
   at least twice the short side, the short side is between 2.5 and 5 inches
   and the long side is at most 11.5 inches. */
#define ENVELOPE_SHORT_MIN 2500
#define ENVELOPE_SHORT_MAX 5000
#define ENVELOPE_LONG_MAX 11500


int
doc_envelope_points(int w, int h)
{
    int lo = w < h ? w : h;
    int hi = w < h ? h : w;

    for (size_t i = 0; i < sizeof envelope_sizes / sizeof envelope_sizes[0];
         i++) {
        int ew = envelope_sizes[i].w;
        int eh = envelope_sizes[i].h;

        if (abs(ew - lo) <= ENVELOPE_TOLERANCE &&
            abs(eh - hi) <= ENVELOPE_TOLERANCE) {
            return envelope_sizes[i].points;
        }
    }

    if (lo >= ENVELOPE_SHORT_MIN && lo <= ENVELOPE_SHORT_MAX &&
        hi <= ENVELOPE_LONG_MAX && hi >= 2 * lo) {
        return 14;
    }

    return 0;
}


void
doc_contents_set(Doc *d, int level)
{
    char rec[16];
    int len;
    int at = -1;
    int first_y = -1;
    int bpos = -1;

    level = level < 0 ? 0 : level > 99 ? 99 : level;
    len = snprintf(rec, sizeof rec, "YCL%d", level);

    for (int i = 0; i < d->header.n; i++) {
        const Str *r = &d->header.v[i];

        if (r->n >= 3 && r->s[0] == 'Y' && r->s[1] == 'C' && r->s[2] == 'L') {
            at = i;
            break;
        }

        if (first_y < 0 && r->n >= 1 && r->s[0] == 'Y') {
            first_y = i;
        }

        if (bpos < 0 && r->n >= 2 && r->s[0] == '*' && r->s[1] == 'B') {
            bpos = i;
        }
    }

    if (at >= 0) {
        if (d->header.v[at].n == len &&
            memcmp(d->header.v[at].s, rec, usz(len)) == 0) {
            return;
        }

        free(d->header.v[at].s);
        d->header.v[at].s = xrealloc(NULL, usz(len + 1));
        memcpy(d->header.v[at].s, rec, usz(len + 1));
        d->header.v[at].n = len;
        doc_touch(d);

        return;
    }

    if (level == 99) {
        return;
    }

    lines_insert_at(&d->header, first_y >= 0 ? first_y :
                    bpos >= 0 ? bpos : d->header.n, rec, usz(len));
    doc_touch(d);
}


static const Doc *level_doc;
static int *level_tab;
static int level_n;


void
doc_levels_begin(const Doc *d)
{
    int lvl = 0;

    free(level_tab);
    level_tab = malloc(usz(d->nitems + 1) * sizeof *level_tab);
    level_n = d->nitems;
    level_doc = d;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == IT_PLUS) {
            lvl++;
        } else if (d->items[i].type == IT_MINUS) {
            lvl--;
        }

        level_tab[i] = lvl < 0 ? 0 : lvl;
    }
}


void
doc_levels_end(void)
{
    free(level_tab);
    level_tab = NULL;
    level_doc = NULL;
    level_n = 0;
}


int
doc_item_level(const Doc *d, int idx)
{
    int lvl = 0;

    if (level_doc == d && level_n == d->nitems && idx >= 0 &&
        idx < level_n) {
        return level_tab[idx];
    }


    for (int i = 0; i <= idx && i < d->nitems; i++) {
        if (d->items[i].type == IT_PLUS) {
            lvl++;
        } else if (d->items[i].type == IT_MINUS) {
            lvl--;
        }
    }

    return lvl < 0 ? 0 : lvl;
}


/* ------------------------------------------------------------------ load */
Doc *
doc_new(int structured)
{
    Doc *d = calloc(1, sizeof *d);

    d->structured = structured;
    d->file_version = STRATA_FILE_FORMAT;
    /* Header modelled on the records present in every shipped sample, with
       one choice of Strata: the text of a new document is justified evenly
       (FTJ3).  The format starts with Left, which a file without the
       record still means. */
    static const char *hdr[] = {
        DOC_FIRST_RECORD, "MR1000", "PP\\1\\", "P1C33",  "P1P\\i\\", "P2P\\i\\",
        "FHA1", "FTJ3",   "FTT5",    "FSH150", "FST150",   "FSS200",
        "FSC200", "FI3",  "DP0,8500,11000", "YCL99", "YIWN2"};

    for (unsigned i = 0; i < sizeof hdr / sizeof hdr[0]; i++) {
        size_t n = strlen(hdr[i]);

        /* these end with a NUL */
        if (hdr[i][0] == 'P' && (hdr[i][1] == 'P' || hdr[i][2] == 'P')) {
            n++;
        }

        lines_add(&d->header, hdr[i], n);
    }

    lines_add(&d->header, structured != 0 ? "*B1" : "*B0", 3);
    d->tail = xrealloc(NULL, 4);
    memcpy(d->tail, "*E\r\n", 4);
    d->tail_len = 4;
    Block *b = block_new(1);
    Tok nl;

    memset(&nl, 0, sizeof nl);
    nl.kind = TK_NL;
    text_insert(&b->cols[0].text, 0, nl);

    if (structured != 0) {
        doc_insert_item(d, d->nitems, IT_SECT, NULL);
        doc_insert_item(d, d->nitems, IT_PLUS, NULL);
    }

    doc_insert_item(d, d->nitems, IT_SECT, NULL);
    doc_insert_item(d, d->nitems, IT_BLOCK, b);

    if (structured != 0) {
        doc_insert_item(d, d->nitems, IT_MINUS, NULL);
    }

    d->modified = 0;

    return d;
}


static void
seterr(char *e, size_t n, const char *m)
{
    if (e != NULL && n != 0) {
        str_copy(e, n, m);
    }
}


static void
flush_orphan(Doc *d, Lines *bp, Lines *cp)
{
    if (bp->n == 0 && cp->n == 0) {
        return;
    }

    Block *b = block_new(0);

    b->props = *bp;
    memset(bp, 0, sizeof *bp);

    /* props only */
    if (cp->n != 0) {
        Column *c = block_add_column(b);

        c->props = *cp;
        memset(cp, 0, sizeof *cp);
        b->ncols = 0;

        /* keep orphan column props in block props to stay lossless */
        for (int i = 0; i < c->props.n; i++) {
            lines_add(&b->props, c->props.v[i].s, usz(c->props.v[i].n));
        }

        lines_free(&c->props);
    }

    doc_insert_item(d, d->nitems, IT_BLOCK, b);
}


static Doc *
parse_doc(uint8_t *b, size_t sz, const char *path, char *err, size_t errlen,
          DocRecovery *rec);


Doc *
doc_load(const char *path, char *err, size_t errlen)
{
    size_t sz = 0;
    uint8_t *b = readfile_all(path, DOC_MAX_BYTES, &sz, err, errlen);

    if (b == NULL) {
        return NULL;
    }

    return parse_doc(b, sz, path, err, errlen, NULL);
}


Doc *
doc_load_mem(const uint8_t *data, size_t len, char *err, size_t errlen)
{
    uint8_t *b = xrealloc(NULL, len + 1);

    memcpy(b, data, len);

    return parse_doc(b, len, NULL, err, errlen, NULL);
}


Doc *
doc_recover(const char *path, DocRecovery *r, char *err, size_t errlen)
{
    size_t sz = 0;
    uint8_t *b = readfile_all(path, DOC_MAX_BYTES, &sz, err, errlen);
    Doc *d;

    memset(r, 0, sizeof *r);

    if (b == NULL) {
        return NULL;
    }

    r->file_size = sz;
    d = parse_doc(b, sz, path, err, errlen, r);

    if (d != NULL && r->damaged != 0) {
        char np[PATH_MAX];

        if (doc_recovered_path(path, np, sizeof np) != 0) {
            doc_free(d);
            seterr(err, errlen, "no name for the recovered document");

            return NULL;
        }

        free(d->path);
        d->path = strdup(np);
    }

    return d;
}


Doc *
doc_recover_mem(const uint8_t *data, size_t len, DocRecovery *r, char *err,
                size_t errlen)
{
    uint8_t *b = xrealloc(NULL, len + 1);

    memset(r, 0, sizeof *r);
    memcpy(b, data, len);
    r->file_size = len;

    return parse_doc(b, len, NULL, err, errlen, r);
}


int
doc_recovered_path(const char *orig, char *out, size_t cap)
{
    char dir[PATH_MAX];
    char stem[PATH_MAX];
    const char *slash = strrchr(orig, '/');
    const char *base = slash != NULL ? slash + 1 : orig;
    size_t bl = strlen(base);
    size_t sl = bl;

    if (sl > 4 && strcasecmp(base + sl - 4, ".doc") == 0) {
        sl -= 4;
    }

    if (sl >= sizeof stem) {
        return -1;
    }

    memcpy(stem, base, sl);
    stem[sl] = '\0';

    if (slash != NULL) {
        size_t dl = (size_t)(slash - orig);

        if (dl >= sizeof dir) {
            return -1;
        }

        memcpy(dir, orig, dl);
        dir[dl] = '\0';

        if (dl == 0) {
            snprintf(dir, sizeof dir, "/");
        }
    } else {
        snprintf(dir, sizeof dir, ".");
    }

    /* The folder of the source file when it can be written, else the working
       directory. */
    for (int pass = 0; pass < 2; pass++) {
        const char *where = pass == 0 ? dir : ".";

        if (access(where, W_OK) != 0) {
            continue;
        }

        for (int n = 1; n < 10000; n++) {
            char name[2 * PATH_MAX + 40];
            struct stat st;
            int len;

            if (n == 1) {
                len = snprintf(name, sizeof name, "%s/%s.recovered.doc", where,
                               stem);
            } else {
                len = snprintf(name, sizeof name, "%s/%s.recovered%d.doc",
                               where, stem, n);
            }

            if (len < 0 || (size_t)len >= cap || (size_t)len >= sizeof name) {
                break;
            }

            if (lstat(name, &st) != 0 && errno == ENOENT) {
                memcpy(out, name, (size_t)len + 1);

                return 0;
            }
        }
    }

    return -1;
}


/*
 * A property record holds numbers of at most DOC_MAX_DIGITS digits, so that
 * every program that reads one with an integer function gets a value that
 * fits.  A record that ends with a NUL holds a text, which may hold any
 * digits.
 */
static bool
record_digits_ok(const uint8_t *r, size_t len)
{
    size_t run = 0;

    if (len != 0 && r[len - 1] == 0) {
        return true;
    }

    for (size_t i = 0; i < len; i++) {
        if (is_digit(r[i])) {
            run++;

            if (run > DOC_MAX_DIGITS) {
                return false;
            }
        } else {
            run = 0;
        }
    }

    return true;
}


/* The width of a column: digits only, else none (the record is kept). */
static int
column_width(const char *w)
{
    int v = 0;

    if (w[0] == '\0') {
        return 0;
    }

    for (int i = 0; w[i] != '\0'; i++) {
        if (!is_digit(w[i]) || i >= DOC_MAX_DIGITS) {
            return 0;
        }

        v = v * 10 + (w[i] - '0');
    }

    return v;
}


/* Give back everything that a refused document had taken, and say why. */
static Doc *
parse_fail(Doc *d, uint8_t *b, Lines *bp, Lines *cp, Block *cur, char *err,
           size_t errlen, const char *msg)
{
    block_free(cur);
    lines_free(bp);
    lines_free(cp);
    doc_free(d);
    free(b);
    seterr(err, errlen, msg);

    return NULL;
}


/*
 * Parse a document held in b, which is freed here.  With rec the loader
 * recovers: a document that is cut short or damaged after a readable
 * beginning gives the part that was read (see msdoc.h).
 */
static Doc *
parse_doc(uint8_t *b, size_t sz, const char *path, char *err, size_t errlen,
          DocRecovery *rec)
{
    int fver = STRATA_FILE_FORMAT;
    char verr[200];

    if (docver_check(b, sz, &fver, verr, sizeof verr) != 0) {
        free(b);
        seterr(err, errlen, verr);
        return NULL;
    }

    if (sz > DOC_MAX_BYTES) {
        free(b);
        seterr(err, errlen, "document too large (the limit is 32 MB)");
        return NULL;
    }

    Doc *d = calloc(1, sizeof *d);

    d->path = path != NULL ? strdup(path) : NULL;
    d->file_version = fver;
    d->mk_loading = 1;
    size_t i = 0;
    size_t n = sz;
    int in_body = 0;
    Lines bp = {0};
    Lines cp = {0};
    Block *cur = NULL; /* block being built when columns continue */
    size_t stopped = SIZE_MAX; /* recovery: where the damage is */

    while (i < n) {
        size_t start = i;
        size_t end;
        size_t next;

        if (d->nitems > DOC_MAX_ITEMS) {
            return parse_fail(d, b, &bp, &cp, cur, err, errlen,
                              "document too large: more than 500000 "
                              "blocks and sections");
        }

        if (d->header.n > DOC_MAX_HEADER) {
            return parse_fail(d, b, &bp, &cp, cur, err, errlen,
                              "damaged document: more than 20000 lines "
                              "of settings");
        }

        if (bp.n > DOC_MAX_PROPS || cp.n > DOC_MAX_PROPS) {
            return parse_fail(d, b, &bp, &cp, cur, err, errlen,
                              "damaged document: more than 2000 lines of "
                              "settings for one block");
        }

        if (b[i] == 'T') {
            size_t e = msdoc_scan_text(b, n, i + 1);

            if (e == SIZE_MAX) {
                if (rec != NULL) {
                    seterr(rec->why, sizeof rec->why,
                           "damaged document: a piece of text has no end");
                    stopped = start;
                    break;
                }

                return parse_fail(d, b, &bp, &cp, cur, err, errlen,
                                  "damaged document: a piece of text has "
                                  "no end");
            }

            end = e; /* includes NUL */
            next = (end + 1 < n && b[end] == '\r' && b[end + 1] == '\n') ?
                       end + 2 :
                       end;
        } else {
            end = i;

            while (end + 1 < n && !(b[end] == '\r' && b[end + 1] == '\n')) {
                end++;
            }

            if (end + 1 >= n) {
                end = n;
            }

            next = (end < n) ? end + 2 : n;
        }

        size_t len = end - start;

        if (in_body == 0 && len >= 3 && b[start] == 'Z' &&
            b[start + 1] == 'N') {
            /* The counts of the markers are not settings: they are kept
               apart, and the writer makes them again. */
            parse_counts(d, b + start, len);
            i = next;
            continue;
        }

        if (in_body == 0 && len >= 2 && b[start] == 'Z' &&
            b[start + 1] == 'P') {
            /* The position of the cursor is not a setting: it is kept apart. */
            parse_view_pos(d, b + start, len);
            i = next;
            continue;
        }

        /* The record OLP of the old setting "Draw LaTeX Markers As Pictures?"
           is read and dropped: the display mode of the LaTeX marker (Inline)
           replaced it (docs/LATEXMARKER-PICTURES.txt). */
        if (in_body == 0 && len >= 3 && b[start] == 'O' &&
            b[start + 1] == 'L' && b[start + 2] == 'P') {
            i = next;
            continue;
        }

        if (b[start] != 'T' && !record_digits_ok(b + start, len)) {
            if (rec != NULL) {
                seterr(rec->why, sizeof rec->why,
                       "damaged document: a number in the settings has too "
                       "many digits");
                stopped = start;
                break;
            }

            return parse_fail(d, b, &bp, &cp, cur, err, errlen,
                              "damaged document: a number in the "
                              "settings has too many digits");
        }

        if (in_body == 0 && len >= 2 && b[start] == 'M' && b[start + 1] == 'D' &&
            md_record_ok(b + start, len) == 0) {
            if (rec != NULL) {
                seterr(rec->why, sizeof rec->why,
                       "damaged document: the description is longer than 250 "
                       "characters or has a control character");
                stopped = start;
                break;
            }

            return parse_fail(d, b, &bp, &cp, cur, err, errlen,
                              "damaged document: the description is longer "
                              "than 250 characters or has a control "
                              "character");
        }

        if (in_body == 0) {
            if (len >= 2 && b[start] == '*' && b[start + 1] == 'B') {
                in_body = 1;
                d->structured = atoi(cstr(b) + start + 2);

                if (len > 2 && !is_digit(b[start + 2])) {
                    d->structured = 0;
                }
            }

            lines_add(&d->header, b + start, len);
            i = next;
            continue;
        }

        /* body */
        if (len >= 2 && b[start] == '*' && b[start + 1] == 'E') {
            if (cur != NULL) {
                doc_insert_item(d, d->nitems, IT_BLOCK, cur);
                cur = NULL;
            }

            flush_orphan(d, &bp, &cp);
            d->tail_len = n - start;
            d->tail = xrealloc(NULL, d->tail_len);
            memcpy(d->tail, b + start, d->tail_len);
            i = n;
            break;
        }

        /*
         * SE<n> starts a section like S does and also records the fold
         * state: 0 expanded, 1 headlines and subsections, 2 contracted.
         * Newer documents write every section that way.
         */
        bool sect_se = len == 3 && b[start] == 'S' && b[start + 1] == 'E' &&
                       is_digit(b[start + 2]);

        if (sect_se) {
            if (cur != NULL) {
                doc_insert_item(d, d->nitems, IT_BLOCK, cur);
                cur = NULL;
            }

            flush_orphan(d, &bp, &cp);
            doc_insert_item(d, d->nitems, IT_SECT, NULL);
            d->items[d->nitems - 1].se = 1;
            d->items[d->nitems - 1].fold = u8(b[start + 2] - '0');
        } else if (len == 1 &&
                   (b[start] == 'S' || b[start] == '+' || b[start] == '-')) {
            if (cur != NULL) {
                doc_insert_item(d, d->nitems, IT_BLOCK, cur);
                cur = NULL;
            }

            flush_orphan(d, &bp, &cp);
            doc_insert_item(d, d->nitems,
                            b[start] == 'S' ? IT_SECT :
                            b[start] == '+' ? IT_PLUS :
                                              IT_MINUS,
                            NULL);
        } else if (b[start] == 'T') {
            if (cur == NULL) {
                cur = block_new(0);
                cur->props = bp;
                memset(&bp, 0, sizeof bp);
            }

            if (cur->ncols >= DOC_MAX_COLUMNS) {
                return parse_fail(d, b, &bp, &cp, cur, err, errlen,
                                  "damaged document: more than 5000 "
                                  "columns in one block");
            }

            Column *c = block_add_column(cur);

            c->props = cp;
            memset(&cp, 0, sizeof cp);
            /* drop 'T' and NUL */
            bool markers_ok = tokenize(&c->text, b + start + 1,
                                       len >= 2 ? len - 2 : 0);

            if (!markers_ok) {
                return parse_fail(d, b, &bp, &cp, cur, err, errlen,
                                  "document too large: more than 1000000 "
                                  "markers");
            }

            /* A loaded text does not need the room that growing leaves. */
            text_trim(&c->text);
            const char *w = props_get(&c->props, "CW", 0);

            c->width = w != NULL ? column_width(w) : 0;

            if (w == NULL) {
                doc_insert_item(d, d->nitems, IT_BLOCK, cur);
                cur = NULL;
            }
        } else if (len >= 2 && b[start] == 'C') {
            lines_add(&cp, b + start, len);
        } else {
            /*
             * A block property record ("B", "BM15", and so on) begins a new
             * block.  It follows a block whose last column carries CW, as in
             * the rows of a table.
             */
            if (b[start] == 'B' && cur != NULL) {
                doc_insert_item(d, d->nitems, IT_BLOCK, cur);
                cur = NULL;
            }

            /*
             * Any other record belongs to the block being built when that
             * block is still open between two columns.  Keep it with the
             * next column's properties so that it is written back in the
             * same place.  Otherwise it belongs to the next block.
             */
            if (cur != NULL) {
                lines_add(&cp, b + start, len);
            } else {
                lines_add(&bp, b + start, len);
            }
        }

        i = next;
    }

    if (rec != NULL && stopped == SIZE_MAX &&
        (in_body == 0 || d->tail == NULL)) {
        seterr(rec->why, sizeof rec->why, in_body == 0 ?
               "damaged document: it is cut short, the start of the text is "
               "missing" :
               "damaged document: it is cut short, the end of the document "
               "is missing");
        stopped = n;
    }

    if (rec != NULL && stopped != SIZE_MAX) {
        /* The part that was read: the end record is supplied. */
        if (in_body == 0) {
            lines_add(&d->header, "*B0", 3);
            d->structured = 0;
        }

        if (cur != NULL) {
            doc_insert_item(d, d->nitems, IT_BLOCK, cur);
            cur = NULL;
        }

        free(d->tail);
        d->tail_len = 4;
        d->tail = xrealloc(NULL, d->tail_len);
        memcpy(d->tail, "*E\r\n", 4);

        for (int k = 0; k < d->nitems; k++) {
            rec->blocks += d->items[k].type == IT_BLOCK;
            rec->sections += d->items[k].type == IT_SECT;
        }

        finish_counts(d);
        rec->damaged = 1;
        rec->bytes_read = stopped;
        lines_free(&bp);
        lines_free(&cp);
        d->modified = 1;
        free(b);

        return d;
    }

    if (in_body == 0) {
        return parse_fail(d, b, &bp, &cp, cur, err, errlen,
                          "damaged document: it is cut short, the "
                          "start of the text is missing");
    }

    if (d->tail == NULL) {
        return parse_fail(d, b, &bp, &cp, cur, err, errlen,
                          "damaged document: it is cut short, the "
                          "end of the document is missing");
    }

    if (cur != NULL) {
        doc_insert_item(d, d->nitems, IT_BLOCK, cur);
    }

    lines_free(&bp);
    lines_free(&cp);
    finish_counts(d);
    d->modified = 0;
    free(b);

    return d;
}


/* ------------------------------------------------------------------ save */
static void
serialize_run(Doc *d, Buf *out)
{
    Buf o = *out;

    for (int i = 0; i < d->header.n; i++) {
        const Str *h = &d->header.v[i];

        if (d->part_orient == 2 && h->n >= 2 && h->s[0] == '*' &&
            h->s[1] == 'B') {
            /* A part that starts in landscape says so just before *B. */
            buf_put(&o, "PO2\r\n", 5);
        }

        if (h->n >= 2 && h->s[0] == '*' && h->s[1] == 'B') {
            /* The counts of the markers go just before the *B record: a
               kind with no marker has no record, and the loader counts it. */
            for (int k = 0; d->mk_write != 0 && k < LK_COUNT; k++) {
                if (d->mk_count[k] != 0 || (d->mk_stale & (1u << k)) != 0) {
                    char rec[32];
                    int rl = snprintf(rec, sizeof rec, "ZN%c%u\r\n",
                                      "FPTI"[k], d->mk_count[k]);

                    buf_put(&o, rec, usz(rl));
                }
            }

            if (d->mk_write != 0 && d->mk_stale != 0) {
                char rec[32];
                int rl = snprintf(rec, sizeof rec, "ZNX%u\r\n",
                                  (unsigned)d->mk_stale);

                buf_put(&o, rec, usz(rl));
            }
        }

        if (d->vp_set != 0 && h->n >= 2 && h->s[0] == '*' && h->s[1] == 'B') {
            /* The position of the cursor goes just before the *B record. */
            char rec[64];
            int rl = snprintf(rec, sizeof rec, "ZP%d,%d,%d,%d\r\n",
                              d->vp_item, d->vp_col, d->vp_tok, d->vp_row);

            buf_put(&o, rec, usz(rl));
        }

        if (i == 0 && h->n >= 2 && h->s[0] == '*' && h->s[1] == 'R') {
            /* Only the current version of the file format is written; the
               flags after the number (*R2h of a library) are kept. */
            size_t k = 2;

            while (k < usz(h->n) && h->s[k] >= '0' && h->s[k] <= '9') {
                k++;
            }

            buf_put(&o, DOC_FIRST_RECORD, sizeof DOC_FIRST_RECORD - 1);
            buf_put(&o, h->s + k, usz(h->n) - k);
        } else {
            buf_put(&o, h->s, usz(h->n));
        }

        buf_put(&o, "\r\n", 2);
    }

    for (int i = 0; i < d->nitems; i++) {
        Item *it = &d->items[i];

        switch (it->type) {
            case IT_SECT:
                if (it->se != 0) {
                    char rec[8];

                    snprintf(rec, sizeof rec, "SE%d\r\n", it->fold % 10);
                    buf_put(&o, rec, 5);
                } else {
                    buf_put(&o, "S\r\n", 3);
                }

                break;

            case IT_PLUS:
                buf_put(&o, "+\r\n", 3);
                break;

            case IT_MINUS:
                buf_put(&o, "-\r\n", 3);
                break;

            case IT_BLOCK: {
                Block *bk = it->blk;

                if (bk->dirty != 0) {
                    block_fix_tabs(bk);
                }

                for (int p = 0; p < bk->props.n; p++) {
                    buf_put(&o, bk->props.v[p].s, usz(bk->props.v[p].n));
                    buf_put(&o, "\r\n", 2);
                }

                for (int c = 0; c < bk->ncols; c++) {
                    Column *col = &bk->cols[c];

                    for (int p = 0; p < col->props.n; p++) {
                        buf_put(&o, col->props.v[p].s, usz(col->props.v[p].n));
                        buf_put(&o, "\r\n", 2);
                    }

                    emit_text(&o, &col->text);
                }

                break;
            }

            default:
                break;
        }
    }

    if (d->tail_len != 0) {
        buf_put(&o, d->tail, d->tail_len);
    }

    *out = o;
}


/*
 * The number of bytes that the document is likely to take: the lines, one
 * byte for each token of a text and a few bytes for each column.  It is only
 * a guess, to make the buffer about the size of the file in one run (a buffer
 * that grows by doubling holds up to twice the file while it is built, and a
 * document of 4000 pages is 15 MB); a guess that is too small makes the
 * buffer grow, and the bytes are the same.
 */
static size_t
serialize_estimate(const Doc *d)
{
    size_t n = d->tail_len + 64 + 48;

    for (int i = 0; i < d->header.n; i++) {
        n += usz(d->header.v[i].n) + 2;
    }

    for (int i = 0; i < d->nitems; i++) {
        const Item *it = &d->items[i];

        if (it->type != IT_BLOCK) {
            n += 5;

            continue;
        }

        const Block *bk = it->blk;

        for (int p = 0; p < bk->props.n; p++) {
            n += usz(bk->props.v[p].n) + 2;
        }

        for (int c = 0; c < bk->ncols; c++) {
            const Column *col = &bk->cols[c];

            n += usz(col->text.n) + 4;

            for (int p = 0; p < col->props.n; p++) {
                n += usz(col->props.v[p].n) + 2;
            }
        }
    }

    return n;
}


size_t doc_serialize_runs;


/*
 * The bytes of the document, made in one run over the document.  The buffer
 * starts at the estimate and is cut to the size of the file at the end.
 */
uint8_t *
doc_serialize(Doc *d, size_t *len)
{
    Buf o = {0};
    size_t est = serialize_estimate(d);

    o.cap = est + est / 16 + 64;
    o.b = xrealloc(NULL, o.cap);
    doc_serialize_runs++;
    serialize_run(d, &o);

    if (o.cap > o.n + 1) {
        o.b = xrealloc(o.b, o.n + 1);
    }

    *len = o.n;

    return o.b;
}


int
doc_save_depth(Doc *d, const char *path, int depth, int *unchanged, char *err,
                size_t errlen)
{
    size_t len;
    uint8_t *buf = doc_serialize(d, &len);
    int same = 0;
    int r = backup_write(path, buf, len, depth, &same, err, errlen);

    free(buf);

    if (r != 0) {
        return -1;
    }

    if (unchanged != NULL) {
        *unchanged = same;
    }

    if (d->path == NULL || strcmp(d->path, path) != 0) {
        free(d->path);
        d->path = strdup(path);
    }

    d->modified = 0;

    return 0;
}


int
doc_save(Doc *d, const char *path, char *err, size_t errlen)
{
    return doc_save_depth(d, path, 0, NULL, err, errlen);
}


void
doc_free(Doc *d)
{
    if (d == NULL) {
        return;
    }

    marker_doc_freed(d);
    lines_free(&d->header);

    for (int i = 0; i < d->nitems; i++) {
        block_free(d->items[i].blk);
    }

    free(d->items);
    free(d->tail);
    free(d->path);
    free(d);
}
