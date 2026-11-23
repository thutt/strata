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

/* search.c - the matcher of Search and Replace (see search.h). */
#include "search.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>


/* The attribute bits of the attribute token that the layout draws. */
#define MASK_BOLD 0x01
#define MASK_ITALIC 0x04
#define MASK_UNDER 0x02


int
search_attr_ok(int want, int mask)
{
    switch (want) {
        case SA_PLAIN:
            return mask == 0;

        case SA_BOLD:
            return (mask & MASK_BOLD) != 0;

        case SA_ITALIC:
            return (mask & MASK_ITALIC) != 0;

        case SA_UNDER:
            return (mask & MASK_UNDER) != 0;

        default:
            return 1;
    }
}


/*
 * The character that a token stands for in a string: a typed character, a
 * no-break space, and a tab as a space.  -1 for anything else.
 */
static int
char_of(const Tok *k)
{
    if (k->kind == TK_CTRL && k->a == NBSP_CTRL) {
        return NBSP_BYTE;
    }

    return k->kind == TK_CHAR ? k->a :
           k->kind == TK_TAB  ? ' ' :
                                -1;
}


/* A character of a word: a letter or digit, and the accented letters. */
static int
word_char(int c)
{
    return c >= 0 && (is_alnum(c) != 0 || (c >= 0x80 && c <= 0xA5));
}


/* Whether the token is a soft hyphen: a search goes through it. */
static int
is_shy(const Tok *k)
{
    return k->kind == TK_CTRL && k->a == SHY_CTRL;
}


/* Whether the neighbour of the match, skipping attribute tokens, is a word
   character.  dir is -1 for the token before and 1 for the one after. */
static int
touches_word(const Text *t, int i, int dir)
{
    for (int j = i + dir; j >= 0 && j < t->n; j += dir) {
        if (t->v[j].kind == TK_ATTR || is_shy(&t->v[j]) != 0) {
            continue;
        }

        return word_char(char_of(&t->v[j]));
    }

    return 0;
}


static int
chars_equal(int a, int b, int match_case)
{
    return match_case != 0 ? a == b : to_lower(a) == to_lower(b);
}


/*
 * The tokens of the string pattern at token i, or 0 when it is not there.  A
 * soft hyphen inside the text is not part of the word: the pattern goes
 * through it and the tokens it takes are counted in the result.
 */
static int
match_string(const Text *t, int i, const SearchSpec *s)
{
    const char *p = s->pattern;
    int ti = i;
    int last = i;

    for (int j = 0; p[j] != '\0'; j++) {
        while (j > 0 && ti < t->n && is_shy(&t->v[ti]) != 0) {
            ti++;
        }

        if (ti >= t->n) {
            return 0;
        }

        int c = char_of(&t->v[ti]);

        if (c < 0 || chars_equal(c, u8(p[j]), s->o.match_case) == 0) {
            return 0;
        }

        last = ti;
        ti++;
    }

    if (s->o.whole_word != 0) {
        int pl = slen(p);

        if (word_char(u8(p[0])) != 0 && touches_word(t, i, -1) != 0) {
            return 0;
        }

        if (pl > 0 && word_char(u8(p[pl - 1])) != 0 &&
            touches_word(t, last, 1) != 0) {
            return 0;
        }
    }

    return last - i + 1;
}


/*
 * The tokens of a match at token i, or 0.  at holds the attribute mask in
 * force at each token, or is NULL when the attribute does not matter.
 */
static int
match_at(const Text *t, int i, const SearchSpec *s, const uint8_t *at)
{
    const Tok *k = &t->v[i];

    if (at != NULL && search_attr_ok(s->o.attr, at[i]) == 0) {
        return 0;
    }

    switch (s->o.find) {
        case SF_TAB:
            return k->kind == TK_TAB;

        case SF_EOL:
            return k->kind == TK_NL;

        case SF_NBSP:
            return k->kind == TK_CTRL && k->a == NBSP_CTRL;

        case SF_SHY:
            return is_shy(k);

        case SF_MARKER:
            return k->kind == TK_MARKER;

        default:
            break;
    }

    if (s->pattern[0] == '\0') {
        /* The first character of a run of the attribute. */
        if (at == NULL || char_of(k) < 0) {
            return 0;
        }

        for (int j = i - 1; j >= 0; j--) {
            if (t->v[j].kind == TK_ATTR) {
                continue;
            }

            return search_attr_ok(s->o.attr, at[j]) == 0;
        }

        return 1;
    }

    return match_string(t, i, s);
}


int
search_text_find(const Text *t, int from, int forward, const SearchSpec *s,
                 int *len)
{
    uint8_t *at = NULL;
    int found = -1;

    *len = 0;

    if (t->n == 0 || (s->o.find == SF_TEXT && s->pattern[0] == '\0' &&
                      s->o.attr == SA_ANY)) {
        return -1;
    }

    if (s->o.attr != SA_ANY) {
        int cur = 0;

        at = malloc(usz(t->n));

        if (at == NULL) {
            return -1;
        }

        for (int i = 0; i < t->n; i++) {
            if (t->v[i].kind == TK_ATTR) {
                cur = t->v[i].a;
            }

            at[i] = u8(cur);
        }
    }

    if (forward != 0) {
        for (int i = from < 0 ? 0 : from; i < t->n && found < 0; i++) {
            int n = match_at(t, i, s, at);

            if (n != 0) {
                found = i;
                *len = n;
            }
        }
    } else {
        for (int i = from >= t->n ? t->n - 1 : from; i >= 0 && found < 0; i--) {
            int n = match_at(t, i, s, at);

            if (n != 0) {
                found = i;
                *len = n;
            }
        }
    }

    free(at);

    return found;
}


void
search_replacement(const Text *t, int i, int len, const char *rep, int follow,
                   char *out, size_t cap)
{
    int letters = 0;
    int uppers = 0;
    int first_upper = 0;

    if (cap == 0) {
        return;
    }

    str_copy(out, cap, rep);

    if (follow == 0) {
        return;
    }

    for (int j = i; j < i + len && j < t->n; j++) {
        int c = char_of(&t->v[j]);

        if (c >= 0 && is_alpha(c) != 0) {
            if (letters == 0) {
                first_upper = c >= 'A' && c <= 'Z';
            }

            letters++;

            if (c >= 'A' && c <= 'Z') {
                uppers++;
            }
        }
    }

    if (letters >= 2 && uppers == letters) {
        for (size_t k = 0; out[k] != '\0'; k++) {
            out[k] = chr(to_upper(u8(out[k])));
        }
    } else if (letters >= 1 && first_upper != 0 && uppers == 1) {
        for (size_t k = 0; out[k] != '\0'; k++) {
            if (is_alpha(u8(out[k])) != 0) {
                out[k] = chr(to_upper(u8(out[k])));
                break;
            }
        }
    }
}
