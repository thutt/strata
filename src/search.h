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
 * search.h - the matcher of Search and Replace.
 *
 * A search looks in one text (the tokens of a column) for a place that
 * matches a specification: a string of characters, or a tab, an end of line,
 * a no-break space or a marker; with the case of the letters, a whole word
 * and the attribute of the text as options.  The matcher knows nothing of
 * the cursor or of the screen: the Editor walks over the texts of the
 * document and asks the matcher about each one.
 */
#ifndef SEARCH_H
#define SEARCH_H
#include <stddef.h>
#include "msdoc.h"

/* What a search looks for. */
typedef enum {
    SF_TEXT, /* the characters of the pattern */
    SF_TAB, /* a tab */
    SF_EOL, /* an end of line */
    SF_NBSP, /* a no-break space */
    SF_MARKER, /* a marker of any kind */
    SF_SHY, /* a soft hyphen */
    SF_COUNT
} SearchFind;

/* The attribute that the text found must have. */
typedef enum {
    SA_ANY, /* any text */
    SA_PLAIN, /* no attribute */
    SA_BOLD,
    SA_ITALIC,
    SA_UNDER,
    SA_COUNT
} SearchAttr;

/* The options that the panels of Search and of Replace keep. */
typedef struct {
    int find; /* a SearchFind */
    int match_case; /* 1: the case of letters must be the same */
    int whole_word; /* 1: the text found must not touch a letter or digit */
    int attr; /* a SearchAttr */
} SearchOpts;

/* One search: the options and the string. */
typedef struct {
    SearchOpts o;
    const char *pattern; /* for SF_TEXT; empty with an attribute means the
                            first character of each run of that attribute */
} SearchSpec;


/*
 * Whether the attribute mask of the text (the mask of the last attribute
 * token before it) satisfies the wanted attribute.
 */
int
search_attr_ok(int want, int mask);


/*
 * Find the next match in the text t.  Forward, the match begins at the
 * token from or after it; backward, at the token from or before it.  Return
 * the index of the first token of the match and set *len to the number of
 * its tokens, or return -1.  An empty string with the attribute SA_ANY
 * finds nothing.
 */
int
search_text_find(const Text *t, int from, int forward, const SearchSpec *s,
                 int *len);


/*
 * Make the replacement for the text found at the tokens [i, i + len) of t:
 * rep as it is, or, when follow is not 0, with the case of the text found
 * (all capitals gives capitals, an initial capital gives an initial
 * capital).  The result is a string of bytes ended by 0; it is cut to cap
 * bytes.
 */
void
search_replacement(const Text *t, int i, int len, const char *rep, int follow,
                   char *out, size_t cap);
#endif
