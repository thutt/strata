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
/* hyphlist.c - the personal hyphenation list (hyphlist.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "config.h"
#include "layout.h"
#include "hyphlist.h"

/* A list with more lines than this is not the list of a person. */
#define HYPH_FILE_LINES_MAX 20000


/* A letter of CP437: the ASCII letters and the accented letters. */
static int
letter(int c)
{
    if (is_alpha(c)) {
        return 1;
    }

    if (c >= 0x80) {
        unsigned u = cp437_uni[c & 0xFF];

        return u >= 0xC0 && u <= 0xFF && u != 0xD7 && u != 0xF7;
    }

    return 0;
}


int
hyph_check(const char *word, char *why, size_t cap)
{
    size_t n = strlen(word);
    size_t i = 0;

    if (n == 0) {
        str_copy(why, cap, "Type a word");

        return 0;
    }

    if (n > HYPH_MAXWORD) {
        snprintf(why, cap, "A word has at most %d characters", HYPH_MAXWORD);

        return 0;
    }

    if (word[0] == '=') {
        i = 1;
    }

    if (i >= n) {
        str_copy(why, cap, "Type the word after the equals sign");

        return 0;
    }

    for (size_t j = i; j < n; j++) {
        int c = u8(word[j]);

        if (c == ' ' || c == '\t') {
            str_copy(why, cap, "A word has no spaces");

            return 0;
        }

        if (is_digit(c) != 0 || c == '\'') {
            str_copy(why, cap, "TeX cannot break a word with a digit or an "
                               "apostrophe");

            return 0;
        }

        if (letter(c) == 0 && c != '-') {
            str_copy(why, cap, "A word has letters and hyphens only");

            return 0;
        }
    }

    if (letter(u8(word[i])) == 0 || letter(u8(word[n - 1])) == 0) {
        str_copy(why, cap, "A word begins and ends with a letter");

        return 0;
    }

    for (size_t j = i; j + 1 < n; j++) {
        if (word[j] == '-' && word[j + 1] == '-') {
            str_copy(why, cap, "A hyphen needs a letter on both sides");

            return 0;
        }
    }

    if (word[0] == '=' && memchr(word, '-', n) != NULL) {
        str_copy(why, cap, "A word with an equals sign has no hyphen: it is "
                           "never broken");

        return 0;
    }

    return 1;
}


/* The next letter of an entry in capitals, skipping the marks; 0 at the end. */
static int
next_key(const char *s, size_t *i)
{
    while (s[*i] == '-' || s[*i] == '=') {
        (*i)++;
    }

    if (s[*i] == '\0') {
        return 0;
    }

    return to_upper(u8(s[(*i)++]));
}


int
hyph_same_word(const char *x, const char *y)
{
    size_t i = 0;
    size_t j = 0;

    for (;;) {
        int cx = next_key(x, &i);
        int cy = next_key(y, &j);

        if (cx != cy) {
            return 0;
        }

        if (cx == 0) {
            return 1;
        }
    }
}


int
hyph_compare(const char *x, const char *y)
{
    size_t i = 0;
    size_t j = 0;

    for (;;) {
        int cx = next_key(x, &i);
        int cy = next_key(y, &j);

        if (cx != cy) {
            return cx < cy ? -1 : 1;
        }

        if (cx == 0) {
            break;
        }
    }

    return strcmp(x, y);
}


int
hyph_places(const char *word, char *plain, size_t cap, int *at, int maxat)
{
    size_t o = 0;
    int n = 0;

    for (size_t i = word[0] == '=' ? 1 : 0; word[i] != '\0'; i++) {
        if (word[i] == '-') {
            if (n < maxat) {
                at[n] = isz(o);
            }

            n++;
        } else if (o + 1 < cap) {
            plain[o++] = word[i];
        }
    }

    if (cap > 0) {
        plain[o] = '\0';
    }

    return n < maxat ? n : maxat;
}


/* The code point of the UTF-8 character at s, and its length in *n. */
static unsigned
decode_utf8(const char *s, size_t *n)
{
    unsigned c = u8(s[0]);

    if (c < 0x80) {
        *n = 1;

        return c;
    }

    if ((c & 0xE0) == 0xC0 && (u8(s[1]) & 0xC0) == 0x80) {
        *n = 2;

        return ((c & 0x1F) << 6) | (u8(s[1]) & 0x3Fu);
    }

    if ((c & 0xF0) == 0xE0 && (u8(s[1]) & 0xC0) == 0x80 &&
        (u8(s[2]) & 0xC0) == 0x80) {
        *n = 3;

        return ((c & 0x0F) << 12) | ((u8(s[1]) & 0x3Fu) << 6) |
               (u8(s[2]) & 0x3Fu);
    }

    /* A byte that is not UTF-8 is taken alone and is no character. */
    *n = 1;

    return 0xFFFD;
}


/* A line of the file (UTF-8) as an entry in CP437; 0 when it has a character
   that CP437 has not. */
static int
entry_of_utf8(const char *ln, char *out, size_t cap)
{
    size_t o = 0;
    size_t i = 0;

    while (ln[i] != '\0') {
        size_t n;
        unsigned cp = decode_utf8(ln + i, &n);
        int b = -1;

        if (cp < 0x80) {
            b = isz(cp);
        } else {
            for (int k = 0x80; k < 256; k++) {
                if (cp437_uni[k] == cp) {
                    b = k;
                    break;
                }
            }
        }

        if (b < 0 || o + 1 >= cap) {
            return 0;
        }

        out[o++] = chr(b);
        i += n;
    }

    out[o] = '\0';

    return 1;
}


char *
hyph_tex_of(const char *text, int *skipped)
{
    size_t cap = strlen(text) * 2 + 64;
    char *out = malloc(cap);
    size_t o = 0;
    size_t col = 0;
    int words = 0;
    int lines = 0;

    if (out == NULL) {
        abort();
    }

    *skipped = 0;
    o += usz(snprintf(out, cap, "\\hyphenation{"));
    col = o;

    for (const char *p = text; *p != '\0' && lines < HYPH_FILE_LINES_MAX;) {
        char ln[HYPH_MAXWORD * 4 + 8];
        char w[HYPH_MAXWORD + 2];
        char why[96];
        size_t len = strcspn(p, "\n");
        size_t k = 0;
        int ok;

        lines++;

        if (len < sizeof ln) {
            memcpy(ln, p, len);
            ln[len] = '\0';
        } else {
            ln[0] = '\0';
            (*skipped)++;
        }

        p += len;

        if (*p == '\n') {
            p++;
        }

        ln[strcspn(ln, "\r")] = '\0';

        while (ln[k] == ' ' || ln[k] == '\t') {
            k++;
        }

        for (size_t e = strlen(ln); e > k && (ln[e - 1] == ' ' ||
                                              ln[e - 1] == '\t');) {
            ln[--e] = '\0';
        }

        if (ln[k] == '\0') {
            continue;
        }

        ok = entry_of_utf8(ln + k, w, sizeof w) != 0 &&
             hyph_check(w, why, sizeof why) != 0;

        if (ok == 0) {
            (*skipped)++;
            continue;
        }

        /* The word as the Printer sets it: a letter of Latin-1 is a
           character of UTF-8, which inputenc maps to the font. */
        if (col > 70) {
            out[o++] = '\n';
            col = 0;
        } else if (words > 0) {
            out[o++] = ' ';
            col++;
        }

        for (size_t i = w[0] == '=' ? 1 : 0; w[i] != '\0'; i++) {
            char u[4];
            int n = w[i] == '-' ? 1 : utf8_encode(cp437_uni[u8(w[i])], u);

            if (w[i] == '-') {
                u[0] = '-';
            }

            memcpy(out + o, u, usz(n));
            o += usz(n);
            col++;
        }

        words++;
    }

    if (words == 0) {
        free(out);

        return NULL;
    }

    out[o++] = '}';
    out[o++] = '\n';
    out[o] = '\0';

    return out;
}


char *
hyph_tex(void)
{
    FILE *f = config_read_file("hyphenation");
    char *text;
    char *res;
    size_t n = 0;
    size_t cap = 4096;
    int skipped;

    if (f == NULL) {
        return NULL;
    }

    text = malloc(cap);

    if (text == NULL) {
        abort();
    }

    for (;;) {
        size_t got = fread(text + n, 1, cap - n - 1, f);

        n += got;

        if (got == 0 || n > (size_t) 4 * 1024 * 1024) {
            break;
        }

        if (n + 1 >= cap) {
            cap *= 2;
            text = realloc(text, cap);

            if (text == NULL) {
                abort();
            }
        }
    }

    fclose(f);
    text[n] = '\0';
    res = hyph_tex_of(text, &skipped);
    free(text);

    return res;
}


char **
hyph_hyc_words(const unsigned char *data, size_t n, int *count, char *err,
               size_t cap)
{
    enum { PAGE = 512 };
    char **w = NULL;
    int nw = 0;
    int wcap = 0;
    unsigned long want;

    *count = 0;

    if (n % PAGE != 0 || n < 2 * (size_t) PAGE) {
        str_copy(err, cap, "This is not a hyphenation dictionary (its size is "
                           "not a whole number of pages)");

        return NULL;
    }

    want = ((unsigned long) data[PAGE - 4] << 24) |
           ((unsigned long) data[PAGE - 3] << 16) |
           ((unsigned long) data[PAGE - 2] << 8) | data[PAGE - 1];

    for (size_t p = 1; p < n / PAGE; p++) {
        const unsigned char *page = data + p * PAGE;
        int ne = page[PAGE - 1];
        size_t off = 0;

        if (ne + 1 > PAGE) {
            str_copy(err, cap, "The hyphenation dictionary is damaged");
            hyph_free_words(w, nw);

            return NULL;
        }

        for (int e = 0; e < ne; e++) {
            size_t len = page[PAGE - 1 - isz(usz(ne)) + e];
            unsigned long mask;
            char *word;
            size_t o = 0;
            size_t letters;

            if (len < 5 || off + len > PAGE - 1 - usz(ne)) {
                str_copy(err, cap, "The hyphenation dictionary is damaged");
                hyph_free_words(w, nw);

                return NULL;
            }

            letters = len - 4;
            mask = ((unsigned long) page[off + letters] << 24) |
                   ((unsigned long) page[off + letters + 1] << 16) |
                   ((unsigned long) page[off + letters + 2] << 8) |
                   page[off + letters + 3];
            word = malloc(letters * 2 + 3);

            if (word == NULL) {
                abort();
            }

            if (mask == 0) {
                word[o++] = '=';
            }

            for (size_t i = 0; i < letters; i++) {
                word[o++] = chr(to_lower(page[off + i]));

                if (i < 32 && ((mask >> i) & 1UL) != 0 && i + 1 < letters) {
                    word[o++] = '-';
                }
            }

            word[o] = '\0';
            off += len;

            if (nw >= wcap) {
                wcap = wcap == 0 ? 64 : wcap * 2;
                w = realloc(w, usz(wcap) * sizeof *w);

                if (w == NULL) {
                    abort();
                }
            }

            w[nw++] = word;
        }
    }

    if ((unsigned long) nw != want) {
        snprintf(err, cap, "The hyphenation dictionary gives %lu words and has "
                           "%d", want, nw);
        hyph_free_words(w, nw);

        return NULL;
    }

    *count = nw;

    return w;
}


void
hyph_free_words(char **w, int count)
{
    for (int i = 0; i < count; i++) {
        free(w[i]);
    }

    free(w);
}
