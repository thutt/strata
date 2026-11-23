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

/* The MyThes reader; see thesaurus.h and docs/THESAURUS.txt. */
#include "util.h"
#include "layout.h"
#include "thesaurus.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define TH_KEY (3 * TH_HEAD)
#define TH_MAXCAND 40
#define TH_USUAL "/usr/share/mythes"

/* A file that is mapped, or not. */
typedef struct {
    void *base; /* What munmap needs. */
    size_t len; /* The size of the file. */
    const char *p; /* The text. */
    size_t n; /* Its size; the index leaves out the final newlines. */
} Map;

static struct {
    int tried; /* The files were looked for. */
    int ok; /* And could be opened. */
    int utf8; /* The character set is UTF-8. */
    int have_override; /* Th_use named something. */
    char override[512];
    char path[512]; /* The .dat file in use. */
    Map idx;
    Map dat;
    size_t first; /* The start of the first entry line of the index. */
} th;


/* Files. */
static void
unmap(Map *m)
{
    if (m->base != NULL) {
        munmap(m->base, m->len);
    }

    memset(m, 0, sizeof *m);
}


static int
map_file(const char *path, Map *m)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    struct stat st;

    memset(m, 0, sizeof *m);

    if (fd < 0) {
        return 0;
    }

    if (fstat(fd, &st) != 0 || S_ISREG(st.st_mode) == 0 || st.st_size <= 0) {
        close(fd);

        return 0;
    }

    size_t n = lsz(st.st_size);
    void *p = mmap(NULL, n, PROT_READ, MAP_PRIVATE, fd, 0);

    close(fd);

    if (p == MAP_FAILED) {
        return 0;
    }

    m->base = p;
    m->len = n;
    m->p = p;
    m->n = n;

    return 1;
}


static size_t
line_end(const Map *m, size_t pos)
{
    while (pos < m->n && m->p[pos] != '\n') {
        pos++;
    }

    return pos;
}


/* The start of the line after the one that starts at pos (n at the end). */
static size_t
line_next(const Map *m, size_t pos)
{
    size_t e = line_end(m, pos);

    return e < m->n ? e + 1 : m->n;
}


/* The start of the line before the one that starts at pos; lo is a limit. */
static size_t
line_prev(const Map *m, size_t lo, size_t pos)
{
    if (pos <= lo) {
        return lo;
    }

    size_t p = pos - 1;

    while (p > lo && m->p[p - 1] != '\n') {
        p--;
    }

    return p;
}


/* The first line of a file is its character set; fill name, move past it. */
static int
first_line(const Map *m, char *name, size_t cap, size_t *next)
{
    size_t e = line_end(m, 0);
    size_t n = e;

    if (e >= m->n || e >= 64) {
        return 0;
    }

    if (n > 0 && m->p[n - 1] == '\r') {
        n--;
    }

    if (n >= cap) {
        return 0;
    }

    memcpy(name, m->p, n);
    name[n] = '\0';
    *next = e + 1;

    return 1;
}


static void
th_close(void)
{
    unmap(&th.idx);
    unmap(&th.dat);
    th.ok = 0;
    th.path[0] = '\0';
}


/* Try the files base.idx and base.dat. */
static int
open_base(const char *base)
{
    char ip[sizeof th.path];
    char dp[sizeof th.path];
    char cs[64];
    char cnt[64];
    size_t next;
    size_t dnext;

    if (strlen(base) + 5 > sizeof ip) {
        return 0;
    }

    snprintf(ip, sizeof ip, "%s.idx", base);
    snprintf(dp, sizeof dp, "%s.dat", base);

    if (map_file(ip, &th.idx) == 0 || map_file(dp, &th.dat) == 0) {
        th_close();

        return 0;
    }

    if (first_line(&th.idx, cs, sizeof cs, &next) == 0 ||
        first_line(&th.dat, cnt, sizeof cnt, &dnext) == 0) {
        th_close();

        return 0;
    }

    th.utf8 = strcmp(cs, "UTF-8") == 0 || strcmp(cs, "utf-8") == 0;

    /* The second line of the index is the number of entries. */
    size_t after = line_next(&th.idx, next);

    th.first = after;

    while (th.idx.n > th.first && (th.idx.p[th.idx.n - 1] == '\n' ||
                                   th.idx.p[th.idx.n - 1] == '\r')) {
        th.idx.n--;
    }

    snprintf(th.path, sizeof th.path, "%s", dp);
    th.ok = 1;

    return 1;
}


static int
ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s);
    size_t b = strlen(suffix);

    return a >= b && strcmp(s + a - b, suffix) == 0;
}


/* Open the data that a path names: a directory, a .dat file or a base. */
static int
open_path(const char *path)
{
    static const char *const names[] = {"th_en_US_v2", "th_en_US_new",
                                        "th_en_US", "th_test"};
    struct stat st;
    char base[sizeof th.path];

    if (*path == '\0') {
        return 0;
    }

    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode) != 0) {
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
            path_join(base, sizeof base, path, names[i]);

            if (open_base(base) != 0) {
                return 1;
            }
        }

        /* Any other pair of files in the directory: the first by name. */
        DIR *d = opendir(path);
        char best[256] = "";

        while (d != NULL) {
            const struct dirent *de = readdir(d);

            if (de == NULL) {
                break;
            }

            if (ends_with(de->d_name, ".dat") != 0 &&
                strlen(de->d_name) < sizeof best &&
                (best[0] == '\0' || strcmp(de->d_name, best) < 0)) {
                snprintf(best, sizeof best, "%s", de->d_name);
            }
        }

        if (d != NULL) {
            closedir(d);
        }

        if (best[0] != '\0') {
            best[strlen(best) - 4] = '\0';
            path_join(base, sizeof base, path, best);

            return open_base(base);
        }

        return 0;
    }

    snprintf(base, sizeof base, "%s", path);

    if (ends_with(base, ".dat") != 0 || ends_with(base, ".idx") != 0) {
        base[strlen(base) - 4] = '\0';
    }

    return open_base(base);
}


static void
th_find(void)
{
    if (th.tried != 0) {
        return;
    }

    th.tried = 1;

    if (th.have_override != 0) {
        open_path(th.override);

        return;
    }

    const char *env = getenv("STRATA_THESAURUS");

    if (env != NULL) {
        open_path(env);
    } else {
        open_path(TH_USUAL);
    }
}


void
th_use(const char *path)
{
    th_close();
    th.tried = 0;
    th.have_override = path != NULL;

    if (path != NULL) {
        snprintf(th.override, sizeof th.override, "%s", path);
    }
}


int
th_available(void)
{
    th_find();

    return th.ok;
}


const char *
th_path(void)
{
    th_find();

    return th.path;
}


/* Words. */
/* One CP437 byte to its UTF-8 bytes; returns the length. */
static int
cp437_to_utf8(int c, char out[4])
{
    return utf8_encode(cp437_uni[c & 0xFF], out);
}


/* Decode one UTF-8 character at s; returns its length, 0 when damaged. */
static int
utf8_next(const char *s, size_t left, unsigned *cp)
{
    unsigned b = u8(s[0]);
    unsigned c1 = left >= 2 ? u8(s[1]) : 0;
    unsigned c2 = left >= 3 ? u8(s[2]) : 0;

    if (b < 0x80U) {
        *cp = b;

        return 1;
    }

    if (b >= 0xC0U && b < 0xE0U && left >= 2 && (c1 & 0xC0U) == 0x80U) {
        *cp = ((b & 0x1FU) << 6) | (c1 & 0x3FU);

        return 2;
    }

    if (b >= 0xE0U && b < 0xF0U && left >= 3 && (c1 & 0xC0U) == 0x80U &&
        (c2 & 0xC0U) == 0x80U) {
        *cp = ((b & 0x0FU) << 12) | ((c1 & 0x3FU) << 6) | (c2 & 0x3FU);

        return 3;
    }

    return 0;
}


/*
 * The bytes of the data, as CP437.  A letter that CP437 does not have makes
 * the word unusable (-1).  Returns the length.
 */
static int
data_to_cp437(const char *src, size_t n, char *dst, int cap)
{
    int len = 0;
    size_t i = 0;

    while (i < n) {
        char c = src[i];

        if (len + 1 >= cap) {
            return -1;
        }

        if (th.utf8 == 0 || u8(c) < 0x80) {
            dst[len++] = c;
            i++;
            continue;
        }

        unsigned cp = 0;
        int k = utf8_next(src + i, n - i, &cp);
        int found = -1;

        if (k == 0) {
            return -1;
        }

        for (int b = 128; b < 256 && found < 0; b++) {
            if (cp437_uni[b] == cp) {
                found = b;
            }
        }

        if (found < 0) {
            return -1;
        }

        dst[len++] = chr(found);
        i += usz(k);
    }

    dst[len] = '\0';

    return len;
}


/* The word, lower case, without spaces at its ends; its length or 0. */
static int
lower_word(const char *in, char out[TH_HEAD])
{
    while (*in == ' ') {
        in++;
    }

    int n = slen(in);

    while (n > 0 && in[n - 1] == ' ') {
        n--;
    }

    if (n == 0 || n >= TH_HEAD) {
        return 0;
    }

    for (int i = 0; i < n; i++) {
        out[i] = chr(to_lower(u8(in[i])));
    }

    out[n] = '\0';

    return n;
}


/* The key that the index is searched for: the word in the data's charset. */
static void
make_key(const char *w, char key[TH_KEY])
{
    int n = 0;

    for (; *w != '\0' && n + 4 < TH_KEY; w++) {
        if (th.utf8 == 0 || u8(*w) < 0x80) {
            key[n++] = *w;
        } else {
            n += cp437_to_utf8(u8(*w), key + n);
        }
    }

    key[n] = '\0';
}


/* Compare the headword of the line at pos with key, bytes unsigned. */
static int
cmp_head(const Map *m, size_t pos, const char *key)
{
    for (size_t i = 0;; i++) {
        unsigned a = pos + i < m->n ? u8(m->p[pos + i]) : 0;
        unsigned b = u8(key[i]);

        if (a == '|' || a == '\n' || a == '\r') {
            a = 0;
        }

        if (a != b) {
            return a < b ? -1 : 1;
        }

        if (a == 0) {
            return 0;
        }
    }
}


/* The first line of the index whose headword is not before key. */
static size_t
idx_lower(const char *key)
{
    const Map *m = &th.idx;
    size_t lo = th.first;
    size_t hi = m->n;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        size_t ls = mid;

        while (ls > lo && m->p[ls - 1] != '\n') {
            ls--;
        }

        if (cmp_head(m, ls, key) < 0) {
            lo = line_next(m, ls);
        } else {
            hi = ls;
        }
    }

    return lo;
}


/* The offset into the data that the index line at pos holds. */
static int
idx_offset(size_t pos, size_t *off)
{
    const Map *m = &th.idx;
    size_t e = line_end(m, pos);
    size_t p = pos;
    size_t v = 0;
    int digits = 0;

    while (p < e && m->p[p] != '|') {
        p++;
    }

    if (p >= e) {
        return 0;
    }

    for (p++; p < e && m->p[p] >= '0' && m->p[p] <= '9'; p++) {
        if (v > th.dat.n) {
            return 0;
        }

        v = v * 10 + usz(m->p[p] - '0');
        digits++;
    }

    if (digits == 0 || v >= th.dat.n) {
        return 0;
    }

    *off = v;

    return 1;
}


/* The headword of the index line at pos, as CP437; 0 when it has none. */
static int
idx_name(size_t pos, char out[TH_HEAD])
{
    const Map *m = &th.idx;
    size_t e = line_end(m, pos);
    size_t p = pos;

    while (p < e && m->p[p] != '|') {
        p++;
    }

    if (p >= e || p == pos) {
        return 0;
    }

    return data_to_cp437(m->p + pos, p - pos, out, TH_HEAD) > 0;
}


/* The entry. */
static int
add_word(ThSense *s, int *cap, const char *w, int n)
{
    for (int i = 0; i < s->nwords; i++) {
        if (strcmp(s->words[i], w) == 0) {
            return 1;
        }
    }

    if (s->nwords == *cap) {
        int nc = *cap == 0 ? 16 : *cap * 2;
        char **nw = realloc(s->words, usz(nc) * sizeof *nw);

        if (nw == NULL) {
            return 0;
        }

        s->words = nw;
        *cap = nc;
    }

    char *copy = malloc(usz(n) + 1);

    if (copy == NULL) {
        return 0;
    }

    memcpy(copy, w, usz(n) + 1);
    s->words[s->nwords++] = copy;

    return 1;
}


static void
free_sense(ThSense *s)
{
    for (int i = 0; i < s->nwords; i++) {
        free(s->words[i]);
    }

    free(s->words);
    s->words = NULL;
    s->nwords = 0;
}


/*
 * Read one sense line [ls, e).  The plain words come first and then those
 * marked "(similar term)"; other relations are left out.  Returns 1 when the
 * sense has a word to show, 0 when it has none, -1 when memory is short.
 */
static int
read_sense(size_t ls, size_t e, ThSense *s)
{
    const Map *m = &th.dat;
    int cap = 0;
    int similar_pass;

    memset(s, 0, sizeof *s);

    for (similar_pass = 0; similar_pass < 2; similar_pass++) {
        size_t p = ls;
        int field = 0;

        while (p <= e) {
            size_t q = p;

            while (q < e && m->p[q] != '|') {
                q++;
            }

            size_t fe = q;

            if (fe > p && m->p[fe - 1] == '\r') {
                fe--;
            }

            if (field == 0) {
                if (similar_pass == 0 && fe - p >= 3 && m->p[p] == '(') {
                    size_t len = fe - p - 2;

                    if (len >= sizeof s->pos) {
                        len = sizeof s->pos - 1;
                    }

                    memcpy(s->pos, m->p + p + 1, len);
                    s->pos[len] = '\0';
                }
            } else if (fe > p) {
                size_t wn = fe - p;
                int kind = 0; /* 0 plain, 1 similar, 2 other. */

                if (m->p[fe - 1] == ')') {
                    size_t b = fe - 1;

                    while (b > p && m->p[b] != '(') {
                        b--;
                    }

                    if (b > p + 1 && m->p[b - 1] == ' ') {
                        kind = 2;
                        wn = b - 1 - p;

                        if (fe - b - 2 == 12 &&
                            strncmp(m->p + b + 1, "similar term", 12) == 0) {
                            kind = 1;
                        }
                    }
                }

                if (kind == similar_pass) {
                    char w[TH_HEAD];
                    int n = data_to_cp437(m->p + p, wn, w, TH_HEAD);

                    if (n > 0 && add_word(s, &cap, w, n) == 0) {
                        free_sense(s);

                        return -1;
                    }
                }
            }

            field++;
            p = q + 1;
        }
    }

    return s->nwords > 0;
}


/*
 * Read the entry that the index line at pos names.  Returns 1 when it has
 * senses with a word to show, 2 when the headword is in the data but none of
 * its senses has a plain or similar word (only generic or related terms),
 * and 0 when the line names no entry or the data is damaged.
 */
static int
read_entry(size_t pos, const char *key, ThEntry *out)
{
    const Map *m = &th.dat;
    size_t off;

    memset(out, 0, sizeof *out);

    if (idx_offset(pos, &off) == 0 || cmp_head(m, off, key) != 0) {
        return 0;
    }

    /* The headword line: headword | the number of sense lines. */
    size_t e = line_end(m, off);
    size_t p = off;
    int count = 0;

    while (p < e && m->p[p] != '|') {
        p++;
    }

    for (p++; p < e && m->p[p] >= '0' && m->p[p] <= '9' && count < 100000;
         p++) {
        count = count * 10 + (m->p[p] - '0');
    }

    if (count <= 0) {
        return 0;
    }

    if (count > TH_MAXSENSES) {
        count = TH_MAXSENSES;
    }

    out->senses = calloc(usz(count), sizeof *out->senses);

    if (out->senses == NULL) {
        return 0;
    }

    size_t ls = line_next(m, off);

    for (int i = 0; i < count; i++) {
        if (ls >= m->n) {
            th_free(out);

            return 0;
        }

        size_t le = line_end(m, ls);
        int r = read_sense(ls, le, &out->senses[out->nsenses]);

        if (r < 0) {
            th_free(out);

            return 0;
        }

        if (r > 0) {
            out->nsenses++;
        }

        ls = le < m->n ? le + 1 : m->n;
    }

    if (out->nsenses == 0) {
        th_free(out);

        return 2;
    }

    return 1;
}


void
th_free(ThEntry *e)
{
    for (int i = 0; i < e->nsenses; i++) {
        free_sense(&e->senses[i]);
    }

    free(e->senses);
    memset(e, 0, sizeof *e);
}


/* Stems. */
static int
is_cons(int c)
{
    return is_alpha(c) && strchr("aeiou", to_lower(c)) == NULL;
}


/* Add a candidate when it is long enough and new; with the doubled last
 * consonant undone as well, when dbl is set. */
static void
cand_add(char c[][TH_HEAD], int *n, const char *s, int len, int dbl)
{
    if (len < 2 || len >= TH_HEAD || *n >= TH_MAXCAND) {
        return;
    }

    int known = 0;

    for (int i = 0; i < *n; i++) {
        if (strlen(c[i]) == usz(len) && strncmp(c[i], s, usz(len)) == 0) {
            known = 1;
        }
    }

    if (known == 0) {
        memcpy(c[*n], s, usz(len));
        c[*n][len] = '\0';
        (*n)++;
    }

    if (dbl != 0 && len >= 4 && s[len - 1] == s[len - 2] &&
        is_cons(u8(s[len - 1])) != 0) {
        cand_add(c, n, s, len - 1, 0);
    }
}


static int
has_suffix(const char *w, int n, const char *suf)
{
    int k = slen(suf);

    return n > k && strcmp(w + n - k, suf) == 0;
}


/* Replace the suffix cut (k bytes) by add, and add the result. */
static void
cand_swap(char c[][TH_HEAD], int *n, const char *w, int wl, int cut,
          const char *add, int dbl)
{
    char t[TH_HEAD];
    int al = slen(add);

    if (wl - cut + al >= TH_HEAD) {
        return;
    }

    memcpy(t, w, usz(wl - cut));
    memcpy(t + wl - cut, add, usz(al) + 1);
    cand_add(c, n, t, wl - cut + al, dbl);
}


static int
make_cands(const char *word, char c[][TH_HEAD])
{
    char w[TH_HEAD];
    int n = 0;
    int wl = lower_word(word, w);

    if (wl == 0) {
        return 0;
    }

    cand_add(c, &n, w, wl, 0);

    if (has_suffix(w, wl, "'s") != 0) {
        cand_swap(c, &n, w, wl, 2, "", 0);
    }

    if (has_suffix(w, wl, "ies") != 0) {
        cand_swap(c, &n, w, wl, 3, "y", 0);
    }

    if (has_suffix(w, wl, "ied") != 0) {
        cand_swap(c, &n, w, wl, 3, "y", 0);
    }

    if (has_suffix(w, wl, "es") != 0) {
        cand_swap(c, &n, w, wl, 2, "", 0);
    }

    if (has_suffix(w, wl, "s") != 0 && has_suffix(w, wl, "ss") == 0) {
        cand_swap(c, &n, w, wl, 1, "", 0);
    }

    if (has_suffix(w, wl, "ed") != 0) {
        cand_swap(c, &n, w, wl, 2, "", 1);
        cand_swap(c, &n, w, wl, 2, "e", 0);
    }

    if (has_suffix(w, wl, "ing") != 0) {
        cand_swap(c, &n, w, wl, 3, "", 1);
        cand_swap(c, &n, w, wl, 3, "e", 0);
    }

    if (has_suffix(w, wl, "er") != 0) {
        cand_swap(c, &n, w, wl, 2, "", 1);
    }

    if (has_suffix(w, wl, "est") != 0) {
        cand_swap(c, &n, w, wl, 3, "", 1);
    }

    if (has_suffix(w, wl, "ly") != 0) {
        cand_swap(c, &n, w, wl, 2, "", 0);
    }

    return n;
}


int
th_stem(const char *word, int try, char out[TH_HEAD])
{
    char c[TH_MAXCAND][TH_HEAD];
    int n = make_cands(word, c);

    if (try < 0 || try >= n) {
        return 0;
    }

    memcpy(out, c[try], TH_HEAD);

    return 1;
}


/* Lookup. */
int
th_lookup(const char *word, ThEntry *out)
{
    char c[TH_MAXCAND][TH_HEAD];

    memset(out, 0, sizeof *out);

    if (th_available() == 0) {
        return -1;
    }

    int n = make_cands(word, c);
    int bare = -1; /* The first candidate that is a headword with no word. */

    for (int i = 0; i < n; i++) {
        char key[TH_KEY];

        make_key(c[i], key);
        size_t pos = idx_lower(key);

        if (pos < th.idx.n && cmp_head(&th.idx, pos, key) == 0) {
            int r = read_entry(pos, key, out);

            if (r == 1) {
                memcpy(out->head, c[i], TH_HEAD);

                return 1;
            }

            if (r == 2 && bare < 0) {
                bare = i;
            }
        }
    }

    if (bare >= 0) {
        memset(out, 0, sizeof *out);
        memcpy(out->head, c[bare], TH_HEAD);

        return 2;
    }

    return 0;
}


int
th_neighbours(const char *word, int step, int count, int mark_in,
              char names[][TH_HEAD], int *mark)
{
    char w[TH_HEAD];
    char key[TH_KEY];

    *mark = 0;

    if (th_available() == 0 || th.idx.n <= th.first || count <= 0) {
        return 0;
    }

    if (lower_word(word, w) == 0) {
        w[0] = '\0';
    }

    make_key(w, key);

    size_t pos = idx_lower(key);
    size_t target;

    if (pos < th.idx.n && cmp_head(&th.idx, pos, key) == 0) {
        target = pos;
    } else {
        target = line_prev(&th.idx, th.first, pos < th.idx.n ? pos :
                                                th.idx.n);
    }

    for (; step > 0; step--) {
        size_t nx = line_next(&th.idx, target);

        if (nx >= th.idx.n) {
            break;
        }

        target = nx;
    }

    for (; step < 0; step++) {
        target = line_prev(&th.idx, th.first, target);
    }

    size_t start = target;

    for (int i = 0; i < mark_in && start > th.first; i++) {
        start = line_prev(&th.idx, th.first, start);
    }

    int n = 0;
    size_t p = start;

    while (n < count && p < th.idx.n) {
        if (idx_name(p, names[n]) != 0) {
            if (p == target) {
                *mark = n;
            }

            n++;
        }

        p = line_next(&th.idx, p);
    }

    return n;
}


/* Case. */
void
th_match_case(const char *orig, const char *repl, char *out, int cap)
{
    int letters = 0;
    int lower = 0;
    int first_upper = 0;
    int seen_first = 0;

    for (const char *s = orig; *s != '\0'; s++) {
        if (is_alpha(u8(*s)) != 0) {
            letters++;

            if (seen_first == 0) {
                seen_first = 1;
                first_upper = *s >= 'A' && *s <= 'Z';
            }

            if (*s >= 'a' && *s <= 'z') {
                lower++;
            }
        }
    }

    int all_upper = letters >= 2 && lower == 0;
    int n = 0;
    int done_first = 0;

    for (; repl[n] != '\0' && n + 1 < cap; n++) {
        int ch = u8(repl[n]);

        if (all_upper != 0) {
            ch = to_upper(ch);
        } else if (first_upper != 0 && done_first == 0 && is_alpha(ch) != 0) {
            ch = to_upper(ch);
            done_first = 1;
        }

        out[n] = chr(ch);
    }

    if (cap > 0) {
        out[n] = '\0';
    }
}
