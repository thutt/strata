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
/* dictlist.c - the functions behind Maintain Dictionaries (dictlist.h). */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "util.h"
#include "config.h"
#include "dictlist.h"
#include "hyphlist.h"


/* The longest line of an import that can be a word, in UTF-8 bytes. */
#define LINE_MAX_BYTES (SPELL_MAXWORD * 4)


/* The order of the list: letters without regard to case, then as typed. */
static int
compare_words(const char *x, const char *y)
{
    for (size_t i = 0;; i++) {
        int cx = to_upper(u8(x[i]));
        int cy = to_upper(u8(y[i]));

        if (cx != cy) {
            return cx < cy ? -1 : 1;
        }

        if (cx == 0) {
            break;
        }
    }

    return strcmp(x, y);
}


static int
compare_entries(const void *a, const void *b)
{
    const char *const *pa = a;
    const char *const *pb = b;

    return compare_words(*pa, *pb);
}


/* Compare an entry with a prefix, without regard to case, over the prefix. */
static int
compare_prefix(const char *entry, const char *prefix)
{
    for (size_t i = 0; prefix[i] != '\0'; i++) {
        int ce = to_upper(u8(entry[i]));
        int cp = to_upper(u8(prefix[i]));

        if (ce != cp) {
            return ce < cp ? -1 : 1;
        }

        if (ce == 0) {
            return -1;
        }
    }

    return 0;
}


/* Does the text have no capital letter A to Z? */
static int
all_lower(const char *s)
{
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] >= 'A' && s[i] <= 'Z') {
            return 0;
        }
    }

    return 1;
}


int
dictlist_check_word(const char *word, char *why, size_t cap)
{
    size_t n = strlen(word);
    int letters = 0;

    if (n == 0) {
        str_copy(why, cap, "Type a word");

        return 0;
    }

    if (n > SPELL_MAXWORD) {
        snprintf(why, cap, "A word has at most %d letters", SPELL_MAXWORD);

        return 0;
    }

    for (size_t i = 0; i < n; i++) {
        int c = u8(word[i]);

        if (c == ' ' || c == '\t') {
            str_copy(why, cap, "A word has no spaces");

            return 0;
        }

        if (c >= '0' && c <= '9') {
            str_copy(why, cap, "A word has no digits");

            return 0;
        }

        if (spell_is_letter(c) != 0) {
            letters++;
        } else if (c != '\'' && c != '-') {
            str_copy(why, cap,
                     "A word has letters, apostrophes and hyphens only");

            return 0;
        }
    }

    if (spell_is_letter(u8(word[0])) == 0 ||
        spell_is_letter(u8(word[n - 1])) == 0 || letters == 0) {
        str_copy(why, cap, "A word begins and ends with a letter");

        return 0;
    }

    return 1;
}


void
dictlist_sort(SpellDict *d)
{
    if (d->n > 1) {
        qsort(d->w, usz(d->n), sizeof *d->w, compare_entries);
    }
}


/* The first place whose entry is not before the word. */
static int
lower_bound(const SpellDict *d, const char *word)
{
    int lo = 0;
    int hi = d->n;

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;

        if (compare_words(d->w[mid], word) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}


int
dictlist_find(const SpellDict *d, const char *word)
{
    int at = lower_bound(d, word);

    if (at < d->n && strcmp(d->w[at], word) == 0) {
        return at;
    }

    return -1;
}


int
dictlist_covers(const SpellDict *d, const char *word)
{
    char alt[SPELL_MAXWORD * 4 + 8];

    if (dictlist_find(d, word) >= 0) {
        return 1;
    }

    str_copy(alt, sizeof alt, word);
    alt[0] = chr(to_lower(u8(alt[0])));

    return strcmp(alt, word) != 0 && all_lower(alt) != 0 &&
           dictlist_find(d, alt) >= 0;
}


int
dictlist_add(SpellDict *d, const char *word)
{
    int at;
    char *entry;

    if (dictlist_covers(d, word) != 0) {
        return 0;
    }

    at = lower_bound(d, word);
    spell_dict_append(d, word);
    entry = d->w[d->n - 1];
    memmove(d->w + at + 1, d->w + at, usz(d->n - 1 - at) * sizeof *d->w);
    d->w[at] = entry;

    return 1;
}


void
dictlist_remove(SpellDict *d, int index)
{
    if (index < 0 || index >= d->n) {
        return;
    }

    free(d->w[index]);
    memmove(d->w + index, d->w + index + 1,
            usz(d->n - 1 - index) * sizeof *d->w);
    d->n--;
}


int
dictlist_prefix(const SpellDict *d, const char *prefix)
{
    int lo = 0;
    int hi = d->n;

    if (prefix[0] == '\0') {
        return d->n > 0 ? 0 : -1;
    }

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;

        if (compare_prefix(d->w[mid], prefix) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    if (lo < d->n && compare_prefix(d->w[lo], prefix) == 0) {
        return lo;
    }

    return -1;
}


void
dictlist_load(SpellDict *d)
{
    spell_dict_load(d);
    dictlist_sort(d);
}


/* The list as the lines of the file: UTF-8, one word on a line. */
static char *
list_text(const SpellDict *d)
{
    size_t total = 1;
    size_t o = 0;
    char *buf;

    for (int i = 0; i < d->n; i++) {
        total += strlen(d->w[i]) * 3 + 1;
    }

    buf = malloc(total);

    if (buf == NULL) {
        abort();
    }

    for (int i = 0; i < d->n; i++) {
        spell_to_utf8(d->w[i], buf + o, total - o);
        o += strlen(buf + o);
        buf[o++] = '\n';
    }

    buf[o] = '\0';

    return buf;
}


int
dictlist_save(const SpellDict *d, char *err, size_t cap)
{
    char *buf = list_text(d);
    int rc = config_write_file("dictionary", buf, err, isz(cap));

    free(buf);

    return rc;
}


/* Take the blanks off both ends of a line, in place. */
static void
trim_line(char *s)
{
    size_t n = strlen(s);
    size_t b = 0;

    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        n--;
    }

    s[n] = '\0';

    while (s[b] == ' ' || s[b] == '\t') {
        b++;
    }

    if (b > 0) {
        memmove(s, s + b, n - b + 1);
    }
}


/*
 * Keep, in a new list, the words of cand that d does not cover, one of each.
 * Words in lower case are taken or left on their own pass: a word with an
 * initial capital is covered by the same word in lower case, also when that
 * one is new.
 */
static int
take_new(SpellDict *d, const SpellDict *cand, int lower, int *dups)
{
    SpellDict fresh;
    int added = 0;

    memset(&fresh, 0, sizeof fresh);

    for (int i = 0; i < cand->n; i++) {
        const char *w = cand->w[i];

        if ((all_lower(w) != 0) != (lower != 0)) {
            continue;
        }

        if (i > 0 && strcmp(cand->w[i - 1], w) == 0) {
            (*dups)++;
        } else if (dictlist_covers(d, w) != 0) {
            (*dups)++;
        } else {
            spell_dict_append(&fresh, w);
            added++;
        }
    }

    for (int i = 0; i < fresh.n; i++) {
        spell_dict_append(d, fresh.w[i]);
    }

    spell_dict_free(&fresh);
    dictlist_sort(d);

    return added;
}


/* ====================================================== hyphenation list */
static int
lower_bound_hy(const SpellDict *d, const char *word)
{
    int lo = 0;
    int hi = d->n;

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;

        if (hyph_compare(d->w[mid], word) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}


static int
compare_hy_entries(const void *a, const void *b)
{
    const char *const *pa = a;
    const char *const *pb = b;

    return hyph_compare(*pa, *pb);
}


void
dicthyph_sort(SpellDict *d)
{
    if (d->n > 1) {
        qsort(d->w, usz(d->n), sizeof *d->w, compare_hy_entries);
    }
}


int
dicthyph_find(const SpellDict *d, const char *word)
{
    int at = lower_bound_hy(d, word);

    /*
     * Entries with the same letters are next to each other, in the order of
     * the way they were typed, so the one that is the same word may be just
     * before the place of the word.
     */
    for (int i = at > 0 ? at - 1 : 0; i < d->n && i <= at; i++) {
        if (hyph_same_word(d->w[i], word) != 0) {
            return i;
        }
    }

    return -1;
}


int
dicthyph_add(SpellDict *d, const char *word)
{
    int at;
    char *entry;

    if (dicthyph_find(d, word) >= 0) {
        return 0;
    }

    at = lower_bound_hy(d, word);
    spell_dict_append(d, word);
    entry = d->w[d->n - 1];
    memmove(d->w + at + 1, d->w + at, usz(d->n - 1 - at) * sizeof *d->w);
    d->w[at] = entry;

    return 1;
}


int
dicthyph_prefix(const SpellDict *d, const char *prefix)
{
    for (int i = 0; i < d->n; i++) {
        size_t k = 0;
        size_t j = 0;
        int same = 1;

        for (; prefix[j] != '\0'; j++) {
            while (d->w[i][k] == '-' || d->w[i][k] == '=') {
                k++;
            }

            if (d->w[i][k] == '\0' ||
                to_upper(u8(d->w[i][k])) != to_upper(u8(prefix[j]))) {
                same = 0;
                break;
            }

            k++;
        }

        if (same != 0) {
            return i;
        }
    }

    return -1;
}


void
dicthyph_load(SpellDict *d)
{
    char ln[HYPH_MAXWORD * 4 + 8];
    FILE *f = config_read_file("hyphenation");

    memset(d, 0, sizeof *d);

    if (f == NULL) {
        return;
    }

    while (d->n < 20000 && fgets(ln, sizeof ln, f) != NULL) {
        char w[HYPH_MAXWORD + 2];
        char why[96];

        ln[strcspn(ln, "\r\n")] = '\0';
        trim_line(ln);

        if (ln[0] == '\0') {
            continue;
        }

        spell_from_utf8(ln, w, sizeof w);

        if (hyph_check(w, why, sizeof why) != 0) {
            spell_dict_append(d, w);
        }
    }

    fclose(f);
    dicthyph_sort(d);
}


int
dicthyph_save(const SpellDict *d, char *err, size_t cap)
{
    char *buf = list_text(d);
    int rc = config_write_file("hyphenation", buf, err, isz(cap));

    free(buf);

    return rc;
}


/* The words of cand (any order) that d has not, one of each, into d. */
static void
merge_hyph(SpellDict *d, SpellDict *cand, DictImport *res)
{
    dicthyph_sort(cand);

    for (int i = 0; i < cand->n; i++) {
        if (i > 0 && hyph_same_word(cand->w[i - 1], cand->w[i]) != 0) {
            res->duplicate++;
        } else if (dicthyph_add(d, cand->w[i]) != 0) {
            res->added++;
        } else {
            res->duplicate++;
        }
    }
}


static int
import_text(SpellDict *d, const char *path, DictImport *res, char *err,
            size_t cap, int hy);


int
dicthyph_import(SpellDict *d, const char *path, DictImport *res, char *err,
                size_t cap)
{
    size_t n = strlen(path);
    unsigned char *data;
    char **w;
    FILE *f;
    long size;
    int count = 0;
    SpellDict cand;

    if (n < 4 || strcasecmp(path + n - 4, ".hyc") != 0) {
        return import_text(d, path, res, err, cap, 1);
    }

    memset(res, 0, sizeof *res);
    f = fopen(path, "rb");

    if (f == NULL) {
        snprintf(err, cap, "Cannot read %.200s: %.80s", path, strerror(errno));

        return -1;
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);

    if (size <= 0 || size > 16L * 1024 * 1024) {
        fclose(f);
        snprintf(err, cap, "%.200s is not a hyphenation dictionary", path);

        return -1;
    }

    data = malloc(lsz(size));

    if (data == NULL) {
        abort();
    }

    if (fread(data, 1, lsz(size), f) != lsz(size)) {
        fclose(f);
        free(data);
        snprintf(err, cap, "Cannot read %.200s", path);

        return -1;
    }

    fclose(f);
    w = hyph_hyc_words(data, lsz(size), &count, err, cap);
    free(data);

    if (w == NULL) {
        return -1;
    }

    memset(&cand, 0, sizeof cand);

    for (int i = 0; i < count; i++) {
        char why[96];

        if (hyph_check(w[i], why, sizeof why) == 0) {
            res->refused++;
        } else {
            spell_dict_append(&cand, w[i]);
        }
    }

    hyph_free_words(w, count);
    merge_hyph(d, &cand, res);
    spell_dict_free(&cand);

    return 0;
}


/*
 * Is the whole file UTF-8?  A word list of the earlier program has its
 * accented letters in code page 437; such a file is not UTF-8 (a byte above
 * 7F that no UTF-8 sequence explains), and its bytes are taken as they are.
 * The file is read to its end and put back at its start.
 */
static int
file_is_utf8(FILE *f)
{
    int need = 0;
    int c;
    int ok = 1;

    while (ok != 0 && (c = fgetc(f)) != EOF) {
        if (need > 0) {
            ok = (c & 0xC0) == 0x80;
            need--;
        } else if (c >= 0xF8 || (c >= 0x80 && c < 0xC2)) {
            ok = 0;
        } else if (c >= 0xF0) {
            need = 3;
        } else if (c >= 0xE0) {
            need = 2;
        } else if (c >= 0xC0) {
            need = 1;
        }
    }

    rewind(f);

    return ok != 0 && need == 0;
}


/* Read the lines of a text file that are words; hy is 1 for hyphenation. */
static int
import_text(SpellDict *d, const char *path, DictImport *res, char *err,
            size_t cap, int hy)
{
    FILE *f = fopen(path, "r");
    SpellDict cand;
    char ln[LINE_MAX_BYTES + 2];
    int first = 1;
    int old_cp;

    memset(res, 0, sizeof *res);
    memset(&cand, 0, sizeof cand);

    if (f == NULL) {
        snprintf(err, cap, "Cannot read %.200s: %.80s", path, strerror(errno));

        return -1;
    }

    old_cp = file_is_utf8(f) == 0;

    while (fgets(ln, sizeof ln, f) != NULL) {
        char w[SPELL_MAXWORD + 2];
        char why[96];
        size_t len = strlen(ln);
        int cut = len > 0 && ln[len - 1] != '\n' && feof(f) == 0;

        if (cut != 0) {
            int c;

            do {
                c = fgetc(f);
            } while (c != EOF && c != '\n');
        }

        ln[strcspn(ln, "\r\n")] = '\0';

        /*
         * A word list of the earlier program ends with the byte 1A (Ctrl-Z)
         * on a line of its own.  It is the end of the list and not a word.
         */
        if (u8(ln[0]) == 0x1A && ln[1] == '\0') {
            break;
        }

        if (first != 0 && u8(ln[0]) == 0xEF && u8(ln[1]) == 0xBB &&
            u8(ln[2]) == 0xBF) {
            memmove(ln, ln + 3, strlen(ln + 3) + 1);
        }

        first = 0;
        trim_line(ln);

        if (ln[0] == '\0' && cut == 0) {
            continue;
        }

        if (cut != 0) {
            res->refused++;
            continue;
        }

        if (old_cp != 0) {
            /* The letters are already the characters of code page 437. */
            if (strlen(ln) > SPELL_MAXWORD) {
                res->refused++;
                continue;
            }

            str_copy(w, sizeof w, ln);
        } else {
            spell_from_utf8(ln, w, sizeof w);
        }

        if (strlen(w) > SPELL_MAXWORD ||
            (hy != 0 ? hyph_check(w, why, sizeof why)
                     : dictlist_check_word(w, why, sizeof why)) == 0) {
            res->refused++;
        } else {
            spell_dict_append(&cand, w);
        }
    }

    fclose(f);

    if (hy != 0) {
        merge_hyph(d, &cand, res);
    } else {
        dictlist_sort(&cand);
        res->added = take_new(d, &cand, 1, &res->duplicate);
        res->added += take_new(d, &cand, 0, &res->duplicate);
    }

    spell_dict_free(&cand);

    return 0;
}


int
dictlist_import(SpellDict *d, const char *path, DictImport *res, char *err,
                size_t cap)
{
    return import_text(d, path, res, err, cap, 0);
}


int
dictlist_export(const SpellDict *d, const char *path, char *err, size_t cap)
{
    char tmp[PATH_MAX];
    char *buf;
    long pid = getpid();
    int fd;
    FILE *f;

    snprintf(tmp, sizeof tmp, "%s.%ld.tmp", path, pid);
    fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0666);
    f = fd >= 0 ? fdopen(fd, "w") : NULL;

    if (f == NULL) {
        snprintf(err, cap, "Cannot write %.200s: %.80s", path, strerror(errno));

        if (fd >= 0) {
            close(fd);
        }

        return -1;
    }

    buf = list_text(d);
    fputs(buf, f);
    free(buf);

    if (fclose(f) != 0 || rename(tmp, path) != 0) {
        snprintf(err, cap, "Cannot write %.200s: %.80s", path, strerror(errno));
        unlink(tmp);

        return -1;
    }

    return 0;
}
