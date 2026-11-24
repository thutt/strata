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
 * searchtest.c - the unit tests of the matcher of Search and Replace
 * (src/search.c): the case, the whole word, the attribute, a tab, an end of
 * line, a no-break space, a marker, the direction, the case of a
 * replacement, and the time that a search takes over the reference manual.
 * Usage: searchtest [document].  The document defaults to the reference
 * manual of the manual folder; when it is not there the speed test is
 * skipped.
 */
#include "../src/msdoc.h"
#include "../src/search.h"
#include "../src/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int checks;
static int failures;


static void
check(int ok, const char *what)
{
    checks++;

    if (ok == 0) {
        failures++;
        printf("FAIL %s\n", what);
    }
}


/* ----------------------------------------------------------------- texts */
static void
add_str(Text *t, const char *s)
{
    for (; *s != '\0'; s++) {
        text_insert(t, t->n, tok_char(u8(*s)));
    }
}


static void
add_kind(Text *t, int kind, int a)
{
    Tok k;

    memset(&k, 0, sizeof k);
    k.kind = u8(kind);
    k.a = u8(a);
    text_insert(t, t->n, k);
}


static void
add_attr(Text *t, int mask)
{
    add_kind(t, TK_ATTR, mask);
}


static void
add_tab(Text *t)
{
    add_kind(t, TK_TAB, ' ');
}


static void
add_nl(Text *t)
{
    add_kind(t, TK_NL, 0);
}


static void
add_marker(Text *t)
{
    add_kind(t, TK_MARKER, 1);
}


static Text
new_text(void)
{
    Text t;

    memset(&t, 0, sizeof t);

    return t;
}


/* Free what a Text holds. */
static void
drop(Text *t)
{
    free(t->v);
    memset(t, 0, sizeof *t);
}


static SearchSpec
spec(const char *pat, int match_case, int whole, int attr)
{
    SearchSpec s;

    memset(&s, 0, sizeof s);
    s.o.find = SF_TEXT;
    s.o.match_case = match_case;
    s.o.whole_word = whole;
    s.o.attr = attr;
    s.pattern = pat;

    return s;
}


static SearchSpec
special(int find)
{
    SearchSpec s = spec("", 0, 0, SA_ANY);

    s.o.find = find;

    return s;
}


/* The first hit forward from token from, or -1. */
static int
fwd(const Text *t, int from, const SearchSpec *s, int *len)
{
    int n;

    return search_text_find(t, from, 1, s, len != NULL ? len : &n);
}


static int
bwd(const Text *t, int from, const SearchSpec *s, int *len)
{
    int n;

    return search_text_find(t, from, 0, s, len != NULL ? len : &n);
}


/* ----------------------------------------------------------------- tests */
static void
test_plain(void)
{
    Text t = new_text();
    SearchSpec s = spec("cat", 0, 0, SA_ANY);
    int len = 0;

    add_str(&t, "the cat sat");
    check(fwd(&t, 0, &s, &len) == 4 && len == 3, "plain: found at 4, 3 long");
    check(fwd(&t, 5, &s, &len) == -1, "plain: not found after the hit");
    check(fwd(&t, 100, &s, &len) == -1, "plain: a start past the end");
    check(bwd(&t, 100, &s, &len) == 4, "plain: backward from past the end");
    check(bwd(&t, 3, &s, &len) == -1, "plain: backward from before the hit");
    s = spec("", 0, 0, SA_ANY);
    check(fwd(&t, 0, &s, &len) == -1, "empty string: finds nothing");
    drop(&t);
}


static void
test_case(void)
{
    Text t = new_text();
    SearchSpec ig = spec("the", 0, 0, SA_ANY);
    SearchSpec mc = spec("the", 1, 0, SA_ANY);

    add_str(&t, "The the THE");
    check(fwd(&t, 0, &ig, NULL) == 0, "case: ignored, the first");
    check(fwd(&t, 1, &ig, NULL) == 4, "case: ignored, the second");
    check(fwd(&t, 0, &mc, NULL) == 4, "case: matched, only the lower one");
    check(fwd(&t, 5, &mc, NULL) == -1, "case: matched, no other");
    drop(&t);
}


static void
test_whole_word(void)
{
    Text t = new_text();
    SearchSpec ww = spec("the", 0, 1, SA_ANY);
    SearchSpec any = spec("the", 0, 0, SA_ANY);

    add_str(&t, "other then the (the) the.");
    check(fwd(&t, 0, &any, NULL) == 1, "word: without the option, inside");
    check(fwd(&t, 0, &ww, NULL) == 11, "word: with the option, the word");
    check(fwd(&t, 12, &ww, NULL) == 16, "word: between brackets");
    check(fwd(&t, 17, &ww, NULL) == 21, "word: before a full stop");
    drop(&t);

    t = new_text();
    add_str(&t, "a");
    add_attr(&t, ATTR_BOLD);
    add_str(&t, "the");
    check(fwd(&t, 0, &ww, NULL) == -1,
          "word: a letter before, with an attribute between, is not a word");
    drop(&t);

    t = new_text();
    add_str(&t, "x the");
    check(fwd(&t, 0, &ww, NULL) == 2, "word: the space before is a limit");
    add_tab(&t);
    add_str(&t, "the");
    check(fwd(&t, 3, &ww, NULL) == 6, "word: a tab is a limit");
    drop(&t);

    /* A string that begins with a space is not tested at its start. */
    t = new_text();
    add_str(&t, "x the");
    ww = spec(" the", 0, 1, SA_ANY);
    check(fwd(&t, 0, &ww, NULL) == 1, "word: a string that starts with a space");
    drop(&t);
}


static void
test_attribute(void)
{
    Text t = new_text();
    SearchSpec s;
    int len = 0;

    add_str(&t, "plain ");
    add_attr(&t, ATTR_BOLD);
    add_str(&t, "bold");
    add_attr(&t, 0);
    add_str(&t, " plain ");
    add_attr(&t, ATTR_ITALIC);
    add_str(&t, "slant");
    add_attr(&t, ATTR_UNDERW);
    add_str(&t, "under");
    add_attr(&t, 0);

    s = spec("bold", 0, 0, SA_BOLD);
    check(fwd(&t, 0, &s, NULL) == 7, "attribute: bold text found");
    s = spec("plain", 0, 0, SA_BOLD);
    check(fwd(&t, 0, &s, NULL) == -1, "attribute: plain text is not bold");
    s = spec("plain", 0, 0, SA_PLAIN);
    check(fwd(&t, 0, &s, NULL) == 0, "attribute: plain text found");
    s = spec("bold", 0, 0, SA_PLAIN);
    check(fwd(&t, 0, &s, NULL) == -1, "attribute: bold text is not plain");
    s = spec("slant", 0, 0, SA_ITALIC);
    check(fwd(&t, 0, &s, &len) >= 0 && len == 5, "attribute: italic text");
    s = spec("under", 0, 0, SA_UNDER);
    check(fwd(&t, 0, &s, NULL) >= 0, "attribute: underscored text");
    s = spec("under", 0, 0, SA_ITALIC);
    check(fwd(&t, 0, &s, NULL) == -1, "attribute: underscored is not italic");
    s = spec("bold", 0, 0, SA_ANY);
    check(fwd(&t, 0, &s, NULL) == 7, "attribute: any finds bold too");
    drop(&t);
}


static void
test_run_start(void)
{
    Text t = new_text();
    SearchSpec s = spec("", 0, 0, SA_BOLD);

    /* ab [bold: cd] ef [bold: gh]: tokens a0 b1 A2 c3 d4 A5 e6 f7 A8 g9 h10 */
    add_str(&t, "ab");
    add_attr(&t, ATTR_BOLD);
    add_str(&t, "cd");
    add_attr(&t, 0);
    add_str(&t, "ef");
    add_attr(&t, ATTR_BOLD);
    add_str(&t, "gh");
    check(fwd(&t, 0, &s, NULL) == 3, "run: the first character of a bold run");
    check(fwd(&t, 4, &s, NULL) == 9, "run: the next run, not the middle");
    check(bwd(&t, 10, &s, NULL) == 9, "run: backward finds the last start");
    check(bwd(&t, 8, &s, NULL) == 3, "run: backward, the one before");
    drop(&t);
}


static void
test_specials(void)
{
    Text t = new_text();
    SearchSpec s;
    int len = 0;

    add_str(&t, "a");
    add_tab(&t);
    add_str(&t, "b");
    add_nl(&t);
    add_str(&t, "c");
    text_insert(&t, t.n, tok_nbsp());
    add_str(&t, "d");
    add_marker(&t);
    add_str(&t, "e");

    s = special(SF_TAB);
    check(fwd(&t, 0, &s, &len) == 1 && len == 1, "special: a tab");
    check(fwd(&t, 2, &s, NULL) == -1, "special: no second tab");
    s = special(SF_EOL);
    check(fwd(&t, 0, &s, NULL) == 3, "special: an end of line");
    s = special(SF_NBSP);
    check(fwd(&t, 0, &s, NULL) == 5, "special: a no-break space");
    s = special(SF_MARKER);
    check(fwd(&t, 0, &s, NULL) == 7, "special: a marker");
    check(bwd(&t, t.n - 1, &s, NULL) == 7, "special: a marker, backward");

    /* The attribute applies to the special too. */
    s = special(SF_TAB);
    s.o.attr = SA_BOLD;
    check(fwd(&t, 0, &s, NULL) == -1, "special: a tab that is not bold");
    drop(&t);
}


/* The soft hyphen: found as a thing, and a string goes through it. */
static void
test_shy(void)
{
    Text t = new_text();
    SearchSpec s;
    int len = 0;

    add_str(&t, "a hy");
    text_insert(&t, t.n, tok_shy());
    add_str(&t, "phen");
    text_insert(&t, t.n, tok_shy());
    add_str(&t, "ation b");

    s = special(SF_SHY);
    check(fwd(&t, 0, &s, &len) == 4 && len == 1, "shy: found as a thing");
    check(fwd(&t, 5, &s, NULL) == 9, "shy: the second one");
    check(fwd(&t, 10, &s, NULL) == -1, "shy: no third");
    s = spec("hyphenation", 0, 0, SA_ANY);
    check(fwd(&t, 0, &s, &len) == 2 && len == 13,
          "shy: a string goes through soft hyphens (all 13 tokens)");
    s = spec("phen", 0, 1, SA_ANY);
    check(fwd(&t, 0, &s, NULL) == -1, "shy: whole word, part of a word");
    s = spec("hyphenation", 0, 1, SA_ANY);
    check(fwd(&t, 0, &s, NULL) == 2, "shy: whole word, the word");
    s = spec("hyphenation ", 0, 0, SA_ANY);
    check(fwd(&t, 0, &s, &len) == 2 && len == 14, "shy: then a space");
    drop(&t);

    /* A soft hyphen at the start of the text matched is not the start. */
    t = new_text();
    text_insert(&t, t.n, tok_shy());
    add_str(&t, "ab");
    s = spec("ab", 0, 0, SA_ANY);
    check(fwd(&t, 0, &s, &len) == 1 && len == 2, "shy: a match starts at a "
                                                 "letter");
    drop(&t);
}


static void
test_legacy(void)
{
    Text t = new_text();
    SearchSpec s = spec("a b", 0, 0, SA_ANY);
    int len = 0;

    /* A tab is a space in a string, as it always was. */
    add_str(&t, "a");
    add_tab(&t);
    add_str(&t, "b");
    check(fwd(&t, 0, &s, &len) == 0 && len == 3, "legacy: a tab is a space");
    drop(&t);

    /* A no-break space is not a space. */
    t = new_text();
    add_str(&t, "a");
    text_insert(&t, t.n, tok_nbsp());
    add_str(&t, "b");
    check(fwd(&t, 0, &s, NULL) == -1, "legacy: a no-break space is not a space");
    s = spec("a\xff" "b", 0, 0, SA_ANY);
    check(fwd(&t, 0, &s, NULL) == 0, "legacy: a no-break space matches itself");
    drop(&t);

    /* An attribute inside the string breaks the match, as it always did. */
    t = new_text();
    add_str(&t, "a");
    add_attr(&t, ATTR_BOLD);
    add_str(&t, "b");
    s = spec("ab", 0, 0, SA_ANY);
    check(fwd(&t, 0, &s, NULL) == -1, "legacy: an attribute breaks a match");
    drop(&t);
}


static void
rep_of(const char *found, const char *rep, int follow, char *out, size_t cap)
{
    Text t = new_text();

    add_str(&t, found);
    search_replacement(&t, 0, t.n, rep, follow, out, cap);
    drop(&t);
}


static void
test_replacement(void)
{
    char out[64];

    rep_of("the", "cat", 0, out, sizeof out);
    check(strcmp(out, "cat") == 0, "replacement: as typed");
    rep_of("THE", "cat", 1, out, sizeof out);
    check(strcmp(out, "CAT") == 0, "replacement: all capitals follow");
    rep_of("The", "cat", 1, out, sizeof out);
    check(strcmp(out, "Cat") == 0, "replacement: an initial capital follows");
    rep_of("the", "Cat", 1, out, sizeof out);
    check(strcmp(out, "Cat") == 0, "replacement: lower case leaves it as typed");
    rep_of("tHE", "cat", 1, out, sizeof out);
    check(strcmp(out, "cat") == 0, "replacement: a mixed case leaves it");
    rep_of("A", "the", 1, out, sizeof out);
    check(strcmp(out, "The") == 0, "replacement: one capital letter");
    rep_of("123", "cat", 1, out, sizeof out);
    check(strcmp(out, "cat") == 0, "replacement: no letters leaves it");
    rep_of("THE", "a-b", 1, out, sizeof out);
    check(strcmp(out, "A-B") == 0, "replacement: only letters change");
    rep_of("The", "", 1, out, sizeof out);
    check(strcmp(out, "") == 0, "replacement: an empty replacement");
    rep_of("THE", "abcdefgh", 1, out, 4);
    check(strcmp(out, "ABC") == 0, "replacement: cut to the room");
}


/* ----------------------------------------------------------------- speed */
static double
now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1.0e6;
}


/* Count the hits of s over all the texts of the document. */
static long
count_hits(const Doc *d, const SearchSpec *s, double *ms)
{
    long hits = 0;
    double t0 = now_ms();

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type != IT_BLOCK || d->items[i].blk == NULL) {
            continue;
        }

        for (int c = 0; c < d->items[i].blk->ncols; c++) {
            const Text *t = &d->items[i].blk->cols[c].text;
            int from = 0;
            int len;
            int at;

            while ((at = search_text_find(t, from, 1, s, &len)) >= 0) {
                hits++;
                from = at + len;
            }
        }
    }

    *ms = now_ms() - t0;

    return hits;
}


static void
test_speed(const char *path)
{
    char err[200];
    Doc *d = doc_load(path, err, sizeof err);
    long tokens = 0;
    double ms;
    SearchSpec s;

    if (d == NULL) {
        printf("searchtest: speed test skipped (%s)\n", err);

        return;
    }

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == IT_BLOCK && d->items[i].blk != NULL) {
            for (int c = 0; c < d->items[i].blk->ncols; c++) {
                tokens += d->items[i].blk->cols[c].text.n;
            }
        }
    }

    s = spec("the", 0, 0, SA_ANY);
    long a = count_hits(d, &s, &ms);

    printf("searchtest: %ld tokens; \"the\" ignoring case: %ld hits, %.1f ms\n",
           tokens, a, ms);
    check(a > 100 && ms < 5000.0, "speed: a plain search of the manual");

    s = spec("the", 1, 1, SA_ANY);
    long b = count_hits(d, &s, &ms);

    printf("searchtest: \"the\" matching case, whole word: %ld hits, %.1f ms\n",
           b, ms);
    check(b > 10 && b < a && ms < 5000.0, "speed: case and whole word");

    s = spec("the", 0, 0, SA_BOLD);
    long c = count_hits(d, &s, &ms);

    printf("searchtest: \"the\" in bold text: %ld hits, %.1f ms\n", c, ms);
    check(c <= a && ms < 5000.0, "speed: an attribute");

    s = spec("", 0, 0, SA_BOLD);
    long r = count_hits(d, &s, &ms);

    printf("searchtest: the starts of bold runs: %ld, %.1f ms\n", r, ms);
    check(ms < 5000.0, "speed: the runs of an attribute");

    s = special(SF_MARKER);
    long m = count_hits(d, &s, &ms);

    printf("searchtest: markers: %ld, %.1f ms\n", m, ms);
    check(m > 0 && ms < 5000.0, "speed: the markers");
    doc_free(d);
}


int
main(int argc, char **argv)
{
    test_plain();
    test_case();
    test_whole_word();
    test_attribute();
    test_run_start();
    test_specials();
    test_shy();
    test_legacy();
    test_replacement();
    test_speed(argc > 1 ? argv[1] : "manual/doc/strata-reference-manual.doc");
    printf("searchtest: %d checks, %d failures\n", checks, failures);

    return failures != 0;
}
