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

/* edtest.c - headless tests of editor operations (no terminal needed). */
#include "../src/util.h"
#include "../src/editor.h"
#include "../src/marker.h"
#include "../src/note.h"
#include "../src/fonts.h"
#include <sys/stat.h>
#include "../src/ui.h"
#include "../src/fileops.h"
#include "../src/template.h"
#include "../src/eqrender.h"
#include "../src/png.h"
#include "../src/spell.h"
#include "../src/thesaurus.h"
#include "../src/config.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static int fails = 0;


/*
 * The place for a file that the test makes when the command line gives none:
 * the directory for temporary files, with the name NAME.  The text lives
 * until the next call.
 */
static const char *
work_or_tmp(int argc, char **argv, const char *name)
{
    static char buf[512];

    if (argc > 2) {
        return argv[2];
    }

    tmp_path(buf, sizeof buf, name);

    return buf;
}
#define CHECK(c, ...)                                                          \
    do {                                                                       \
        if (!(c)) {                                                            \
            printf("FAIL line %d: ", __LINE__);                                \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
            fails++;                                                           \
        }                                                                      \
    } while (0)


static void
type(Editor *e, const char *s)
{
    for (; *s != '\0'; s++) {
        ed_insert_char(e, u8(*s));
    }
}


/* A copy of every line of the view: its kind, item, row and cells. */
typedef struct {
    int n;
    int width;
    int *info;
    Cell *cells;
} ViewCopy;


static void
view_copy(Editor *e, ViewCopy *vc)
{
    ed_layout(e);
    vc->n = e->view.n;
    vc->width = e->view.width;
    vc->info = malloc(usz(vc->n * 3 + 1) * sizeof *vc->info);
    vc->cells = malloc(usz(vc->n * vc->width + 1) * sizeof *vc->cells);

    for (int i = 0; i < vc->n; i++) {
        vc->info[3 * i] = e->view.lines[i].kind;
        vc->info[3 * i + 1] = e->view.lines[i].item;
        vc->info[3 * i + 2] = e->view.lines[i].row;
        view_line_cells(e, i, &vc->cells[i * vc->width]);
    }
}


static void
view_copy_free(ViewCopy *vc)
{
    free(vc->info);
    free(vc->cells);
}


/* True if the two copies show the same lines. */
static int
view_copy_same(const ViewCopy *a, const ViewCopy *b)
{
    if (a->n != b->n || a->width != b->width) {
        return 0;
    }

    for (int i = 0; i < a->n * 3; i++) {
        if (a->info[i] != b->info[i]) {
            return 0;
        }
    }

    for (int i = 0; i < a->n * a->width; i++) {
        if (a->cells[i].ch != b->cells[i].ch ||
            a->cells[i].attr != b->cells[i].attr ||
            a->cells[i].tok != b->cells[i].tok) {
            return 0;
        }
    }

    return 1;
}


/*
 * After an edit that asked for a partial layout, the view must be the one a
 * full layout gives.  Returns 1 if the edit used the partial layout.
 */
static int
partial_same(Editor *e, const char *what)
{
    ViewCopy part;
    ViewCopy full;
    int partial = e->view.partial;

    view_copy(e, &part);
    ed_invalidate(e);
    view_copy(e, &full);
    CHECK(view_copy_same(&part, &full),
          "the partial layout after %s is the full layout", what);
    view_copy_free(&part);
    view_copy_free(&full);

    return partial;
}


/* The headlines of all sections, in order, joined by '|'. */
static void
headlines(const Doc *d, char *out, size_t cap)
{
    out[0] = '\0';

    for (int i = 0; i + 1 < d->nitems; i++) {
        char tx[64];

        if (d->items[i].type == IT_SECT && d->items[i + 1].type == IT_BLOCK) {
            text_plain(&d->items[i + 1].blk->cols[0].text, tx, sizeof tx);

            for (int n = slen(tx); n > 0 && tx[n - 1] == ' '; n--) {
                tx[n - 1] = '\0';
            }

            strncat(out, tx, cap - strlen(out) - 1);
            strncat(out, "|", cap - strlen(out) - 1);
        }
    }
}


/* Where the bytes of needle first appear in buf, or NULL. */
static const uint8_t *
find_bytes(const uint8_t *buf, size_t len, const char *needle)
{
    size_t n = strlen(needle);

    for (size_t i = 0; i + n <= len; i++) {
        if (memcmp(buf + i, needle, n) == 0) {
            return buf + i;
        }
    }

    return NULL;
}


static int
count_type(Doc *d, int ty)
{
    int n = 0;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == ty) {
            n++;
        }
    }

    return n;
}


static char *
plain(Doc *d, int item, int col)
{
    static char b[512];

    text_plain(&d->items[item].blk->cols[col].text, b, sizeof b);
    return b;
}


static int
balanced(Doc *d)
{
    int dp = 0;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == IT_PLUS) {
            dp++;
        }

        if (d->items[i].type == IT_MINUS && --dp < 0) {
            return 0;
        }
    }

    return dp == 0;
}




/* A marker token with the given type and payload bytes (for the tests). */
static Tok
marker_tok(int type, const char *payload, size_t len)
{
    return tok_marker(type, (const uint8_t *) payload, len);
}

/* Fake picture sizes for the layout of equations (no latex needed): an
   inline picture needs 6 cells and a row above and below the text; a
   displayed one 10 cells and three rows. */
static int
fake_size(const Text *t UNUSED, int tok UNUSED, int display, int *wc,
          int *above, int *below)
{
    if (display != 0) {
        *wc = 10;
        *above = 2;
        *below = 0;
    } else {
        *wc = 6;
        *above = 1;
        *below = 1;
    }

    return 1;
}


/* ============================================================ spell check */
/*
 * Fill a text from a string: ^ is a change of the attribute (bold on, then
 * off), | a tab, @ a marker and ~ a line end.
 */
static void
spell_text(Text *t, const char *s)
{
    static const uint8_t none[1] = {0};

    text_init(t);

    for (; *s != '\0'; s++) {
        Tok k = tok_char(u8(*s));

        if (*s == '^') {
            memset(&k, 0, sizeof k);
            k.kind = TK_ATTR;
            k.a = ATTR_BOLD;
        } else if (*s == '|') {
            memset(&k, 0, sizeof k);
            k.kind = TK_TAB;
            k.a = ' ';
        } else if (*s == '@') {
            k = tok_marker(23, none, 0);
        } else if (*s == '~') {
            memset(&k, 0, sizeof k);
            k.kind = TK_NL;
        }

        text_insert(t, t->n, k);
    }
}


/* The characters of a block (the first column) with a line end as a bar. */
static const char *
flat(const Doc *d, int item)
{
    static char out[512];
    const Text *t = &d->items[item].blk->cols[0].text;
    int n = 0;

    for (int i = 0; i < t->n && n + 1 < isz(sizeof out); i++) {
        if (t->v[i].kind == TK_CHAR) {
            out[n++] = chr(t->v[i].a);
        } else if (t->v[i].kind == TK_NL) {
            out[n++] = '|';
        }
    }

    out[n] = '\0';

    return out;
}


/* The words of a text, separated by a space. */
static const char *
spell_words(const Text *t)
{
    static char out[512];
    int pos = 0;
    SpellWord w;

    out[0] = '\0';

    while (spell_find_word(t, pos, t->n, &w) != 0) {
        if (out[0] != '\0') {
            strcat(out, " ");
        }

        strcat(out, w.too_long != 0 ? "LONG" : w.text);
        pos = w.end;
    }

    return out;
}


/* The words of a string, as a test of the word finder. */
static void
words_are(const char *src, const char *want)
{
    Text t;

    spell_text(&t, src);
    CHECK(strcmp(spell_words(&t), want) == 0, "words of '%s': '%s' not '%s'",
          src, spell_words(&t), want);
    text_free(&t);
}


/* The size of a file and its lines (the longest line goes to *longest). */
static int
file_lines(const char *path, int *longest)
{
    char ln[4096];
    int n = 0;
    FILE *f = fopen(path, "r");

    *longest = 0;

    if (f == NULL) {
        return 0;
    }

    while (fgets(ln, sizeof ln, f) != NULL) {
        n++;

        if (slen(ln) > *longest) {
            *longest = slen(ln);
        }
    }

    fclose(f);

    return n;
}


static long
ms_now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}


/*
 * Search and Replace of the Editor (src/editor.c with src/search.c): the
 * options, the wrap round the document, the places that the interactive
 * Replace walks over, and the replacements.  The document has three blocks,
 * "alpha beta", "Gamma beta" and "beta delta".
 */
static Editor *
search_editor(void)
{
    Doc *d = doc_new(1);
    Editor *e = ed_new(d);

    ed_layout(e);
    type(e, "alpha beta");
    ed_append_block(e, 1);
    type(e, "Gamma beta");
    ed_append_block(e, 1);
    type(e, "beta delta");
    ed_move(e, MV_DOC_START, 0);

    return e;
}


/* The characters of the text of block number n of the document. */
static void
block_string(const Editor *e, int n, char *out, size_t cap)
{
    int seen = 0;
    size_t k = 0;

    for (int i = 0; i < e->doc->nitems; i++) {
        if (e->doc->items[i].type != IT_BLOCK) {
            continue;
        }

        if (seen++ != n) {
            continue;
        }

        const Text *t = &e->doc->items[i].blk->cols[0].text;

        for (int j = 0; j < t->n && k + 1 < cap; j++) {
            if (t->v[j].kind == TK_CHAR) {
                out[k++] = chr(t->v[j].a);
            }
        }
    }

    out[k] = '\0';
}


static void
search_done(Editor *e)
{
    Doc *d = e->doc;

    ed_free(e);
    doc_free(d);
}


static SearchSpec
sspec(const char *pat, int match_case, int whole)
{
    SearchSpec s;

    memset(&s, 0, sizeof s);
    s.o.find = SF_TEXT;
    s.o.match_case = match_case;
    s.o.whole_word = whole;
    s.pattern = pat;

    return s;
}


/*
 * The number styles of the counters and of the tags of the sections: a roman
 * numeral shows 1 to 3999 and a number that a style cannot show is written
 * in arabic digits.
 */
static void
number_style_tests(void)
{
    char out[32];

    marker_number_text(3, 3999, out, sizeof out);
    CHECK(strcmp(out, "MMMCMXCIX") == 0, "number style: 3999 in capital "
                                         "roman is MMMCMXCIX: %s", out);
    marker_number_text(2, 3999, out, sizeof out);
    CHECK(strcmp(out, "mmmcmxcix") == 0, "number style: 3999 in lower case "
                                         "roman is mmmcmxcix: %s", out);
    marker_number_text(3, 4000, out, sizeof out);
    CHECK(strcmp(out, "4000") == 0, "number style: 4000 in capital roman is "
                                    "arabic: %s", out);
    marker_number_text(2, 4000, out, sizeof out);
    CHECK(strcmp(out, "4000") == 0, "number style: 4000 in lower case roman "
                                    "is arabic: %s", out);
    marker_number_text(3, 4294967295u, out, sizeof out);
    CHECK(strcmp(out, "4294967295") == 0, "number style: the largest counter "
                                          "in roman is arabic: %s", out);
    marker_number_text(3, 0, out, sizeof out);
    CHECK(strcmp(out, "0") == 0, "number style: 0 in roman is arabic: %s",
          out);
    marker_number_text(2, 1, out, sizeof out);
    CHECK(strcmp(out, "i") == 0, "number style: 1 in lower case roman is i: "
                                 "%s", out);
    marker_number_text(1, 4000, out, sizeof out);
    CHECK(strcmp(out, "4000") == 0, "number style: 4000 in arabic: %s", out);
    marker_number_text(4, 4000, out, sizeof out);
    CHECK(strcmp(out, "EWV") == 0, "number style: 4000 in capital letters is "
                                   "EWV: %s", out);
}


static void
search_tests(void)
{
    Editor *e = search_editor();
    SearchSpec s = sspec("beta", 0, 0);
    char buf[64];
    Pos start;
    Pos at;
    Pos after;
    int len = 0;
    int first_item;

    /* Search moves from the cursor and wraps round the document. */
    CHECK(ed_search_spec(e, &s, 1) == 1 && e->cur.tok == 6 &&
              e->search_wrapped == 0,
          "search spec: the first beta, after the cursor, no wrap");
    first_item = e->cur.item;
    CHECK(ed_search_spec(e, &s, 1) == 1 && e->cur.item > first_item &&
              e->search_wrapped == 0,
          "search spec: the second beta is in the next block, no wrap");
    CHECK(ed_search_spec(e, &s, 1) == 1 && e->cur.tok == 0,
          "search spec: the third beta begins its block");
    CHECK(ed_search_spec(e, &s, 1) == 1 && e->cur.item == first_item &&
              e->cur.tok == 6 && e->search_wrapped == 1,
          "search spec: the search wraps round to the first beta, and says "
          "so");
    CHECK(ed_search_spec(e, &s, 0) == 1 && e->cur.tok == 0 &&
              e->cur.item > first_item && e->search_wrapped == 1,
          "search spec: backward from the first beta wraps to the last");
    CHECK(ed_search_spec(e, &s, 0) == 1 && e->search_wrapped == 0,
          "search spec: backward, the next beta before it does not wrap");
    s = sspec("zzzz", 0, 0);
    CHECK(ed_search_spec(e, &s, 1) == 0, "search spec: a miss is a miss");

    /* The options change what is found. */
    ed_move(e, MV_DOC_START, 0);
    s = sspec("gamma", 1, 0);
    CHECK(ed_search_spec(e, &s, 1) == 0, "search spec: Match Case, no hit");
    s = sspec("gamma", 0, 0);
    CHECK(ed_search_spec(e, &s, 1) == 1, "search spec: case ignored, hit");
    s = sspec("bet", 0, 1);
    CHECK(ed_search_spec(e, &s, 1) == 0, "search spec: whole word, no hit");

    /* The places that the interactive Replace walks over. */
    s = sspec("beta", 0, 0);
    start.item = 0;
    start.col = 0;
    start.tok = 0;

    for (int i = 0; i < e->doc->nitems; i++) {
        if (e->doc->items[i].type == IT_BLOCK) {
            start.item = i;
            break;
        }
    }

    CHECK(ed_find_from(e, &s, &start, &at, &len) == 1 && at.tok == 6 &&
              len == 4,
          "find from: the first place, 4 tokens");
    start = at;
    CHECK(ed_find_from(e, &s, &start, &at, &len) == 1 && at.tok == 6 &&
              at.item == start.item,
          "find from: the start place itself counts");
    start.tok += len;
    CHECK(ed_find_from(e, &s, &start, &at, &len) == 1 &&
              at.item > start.item,
          "find from: the next place after it is in the next block");
    start = at;
    start.tok += len;
    CHECK(ed_find_from(e, &s, &start, &at, &len) == 1 && at.tok == 0,
          "find from: the third place");
    start = at;
    start.tok += len;
    CHECK(ed_find_from(e, &s, &start, &at, &len) == 0,
          "find from: after the last place there is none, no wrap");

    /* A range: only a place that begins before the limit counts. */
    {
        Pos p0 = start;
        Pos p1;
        Pos p2;
        Pos lim;
        Pos hit;
        int hl = 0;

        p0.item = start.item;
        p0.col = 0;
        p0.tok = 0;

        for (int i = 0; i < e->doc->nitems; i++) {
            if (e->doc->items[i].type == IT_BLOCK) {
                p0.item = i;
                break;
            }
        }

        CHECK(ed_find_from(e, &s, &p0, &p1, &hl) == 1,
              "find range: the first place");
        p2 = p1;
        p2.tok += hl;
        CHECK(ed_find_from(e, &s, &p2, &lim, &hl) == 1,
              "find range: the second place");
        CHECK(ed_find_range(e, &s, &p0, &lim, &hit, &hl) == 1 &&
                  hit.item == p1.item && hit.tok == p1.tok,
              "find range: from the top, the first place is before the "
              "second");
        CHECK(ed_find_range(e, &s, &p2, &lim, &hit, &hl) == 0,
              "find range: a place that begins at the limit does not count");
        lim.tok++;
        CHECK(ed_find_range(e, &s, &p2, &lim, &hit, &hl) == 1 &&
                  hit.item == lim.item && hit.tok == lim.tok - 1,
              "find range: a place that begins before the limit counts");
        CHECK(ed_find_range(e, &s, &p0, &p1, &hit, &hl) == 0,
              "find range: no place before the first one");
        CHECK(ed_find_range(e, &s, &p0, NULL, &hit, &hl) == 1 &&
                  hit.item == p1.item && hit.tok == p1.tok,
              "find range: no limit is the same as find from");
    }

    /* Replace at a place, with and without the case of the text found. */
    start.item = at.item;
    start.col = 0;
    start.tok = 0;
    CHECK(ed_replace_at(e, &start, 4, "OMEGA", 0, &after) == 5 &&
              after.tok == 5 && after.item == start.item,
          "replace at: 5 tokens put in, the cursor place after them");
    block_string(e, 2, buf, sizeof buf);
    CHECK(strcmp(buf, "OMEGA delta") == 0, "replace at: the text");

    /* Replace all: counts, options and the case of the text. */
    search_done(e);
    e = search_editor();
    s = sspec("beta", 1, 0);
    CHECK(ed_replace_all_spec(e, &s, "x", 0) == 3, "replace all: 3 places");
    block_string(e, 0, buf, sizeof buf);
    CHECK(strcmp(buf, "alpha x") == 0, "replace all: the first block");
    search_done(e);

    e = search_editor();
    s = sspec("gamma", 1, 0);
    CHECK(ed_replace_all_spec(e, &s, "x", 0) == 0,
          "replace all: Match Case, no place");
    s = sspec("gamma", 0, 0);
    CHECK(ed_replace_all_spec(e, &s, "delta", 1) == 1,
          "replace all: Follow Found, 1 place");
    block_string(e, 1, buf, sizeof buf);
    CHECK(strcmp(buf, "Delta beta") == 0,
          "replace all: an initial capital is followed");
    search_done(e);

    e = search_editor();
    s = sspec("beta", 0, 1);
    CHECK(ed_replace_all_spec(e, &s, "beta beta", 0) == 3,
          "replace all: a replacement that holds the string is not searched");
    block_string(e, 2, buf, sizeof buf);
    CHECK(strcmp(buf, "beta beta delta") == 0,
          "replace all: no endless run over the replacement");
    search_done(e);

    e = search_editor();
    s = sspec("beta", 0, 0);
    CHECK(ed_replace_all_spec(e, &s, "", 0) == 3,
          "replace all: an empty replacement deletes");
    block_string(e, 0, buf, sizeof buf);
    CHECK(strcmp(buf, "alpha ") == 0, "replace all: deleted");
    search_done(e);
}


/* The rows of the windows in the settings (x11_rows and terminal_rows). */
static void
rows_tests(void)
{
    char home[256];
    char file[300];
    char err[200];
    Config c;
    char buf[512];
    const char *old = getenv("STRATA_HOME");
    char saved[256] = "";

    if (old != NULL) {
        snprintf(saved, sizeof saved, "%s", old);
    }

    snprintf(home, sizeof home, "%s/edtest_rows_home.%d", tmp_dir(), getpid());
    mkdir(home, 0700);
    setenv("STRATA_HOME", home, 1);
    snprintf(file, sizeof file, "%s/config", home);

    config_load(&c);
    CHECK(c.x11_rows == 0 && c.terminal_rows == 0, "no rows by default");

    CHECK(config_set_rows(1, 40, 0, err, sizeof err) == 0, "set: %s", err);
    CHECK(config_set_rows(0, 30, 0, err, sizeof err) == 0, "set: %s", err);
    config_load(&c);
    CHECK(c.x11_rows == 40 && c.terminal_rows == 30, "kept %d and %d",
          c.x11_rows, c.terminal_rows);

    /* Every other line stays, and the other key is not lost. */
    FILE *f = fopen(file, "w");

    fputs("backup=yes\nx11_rows=33\nshow_dots=yes\nterminal_rows=44\n", f);
    fclose(f);
    CHECK(config_set_rows(1, 55, 0, err, sizeof err) == 0, "set: %s", err);
    f = fopen(file, "r");
    buf[fread(buf, 1, sizeof buf - 1, f)] = '\0';
    fclose(f);
    CHECK(strcmp(buf, "backup=yes\nshow_dots=yes\nterminal_rows=44\n"
                      "x11_rows=55\n") == 0,
          "the other lines are as they were: '%s'", buf);

    /* The limits: below 20 is refused, above 200 is 200. */
    CHECK(config_clamp_rows(19, 0) == 20 && config_clamp_rows(-5, 0) == 20 &&
              config_clamp_rows(20, 0) == 20 &&
              config_clamp_rows(200, 0) == 200 &&
              config_clamp_rows(201, 0) == 200 &&
              config_clamp_rows(90, 60) == 60 &&
              config_clamp_rows(40, 60) == 40 &&
              config_clamp_rows(90, 10) == 20 &&
              config_clamp_rows(90, 500) == 90,
          "config_clamp_rows");
    CHECK(config_set_rows(1, 19, 0, err, sizeof err) == 0, "19: %s", err);
    config_load(&c);
    CHECK(c.x11_rows == 20, "19 is written as 20, not %d", c.x11_rows);
    CHECK(config_set_rows(0, 5000, 0, err, sizeof err) == 0, "5000: %s", err);
    CHECK(config_set_rows(1, 120, 60, err, sizeof err) == 0, "120: %s", err);
    config_load(&c);
    CHECK(c.terminal_rows == 200 && c.x11_rows == 60,
          "above the maximum is written as the maximum: %d and %d",
          c.terminal_rows, c.x11_rows);
    f = fopen(file, "r");
    buf[fread(buf, 1, sizeof buf - 1, f)] = '\0';
    fclose(f);
    CHECK(strstr(buf, "terminal_rows=200\n") != NULL &&
              strstr(buf, "x11_rows=60\n") != NULL,
          "the file holds the numbers that were made right: '%s'", buf);

    /* A value in the file that is out of range is moved to the limit. */
    f = fopen(file, "w");
    fputs("x11_rows=5\nterminal_rows=999\n", f);
    fclose(f);
    config_load(&c);
    CHECK(c.x11_rows == 20 && c.terminal_rows == 200, "read %d and %d",
          c.x11_rows, c.terminal_rows);
    f = fopen(file, "w");
    fputs("x11_rows=abc\nterminal_rows=\n", f);
    fclose(f);
    config_load(&c);
    CHECK(c.x11_rows == 0 && c.terminal_rows == 0,
          "text is no number: %d and %d", c.x11_rows, c.terminal_rows);

    /* The default of X11 and 0 take the line out of the file. */
    CHECK(config_set_rows(1, 50, 0, err, sizeof err) == 0, "set: %s", err);
    CHECK(config_set_rows(0, 0, 0, err, sizeof err) == 0, "set: %s", err);
    f = fopen(file, "r");
    buf[fread(buf, 1, sizeof buf - 1, f)] = '\0';
    fclose(f);
    CHECK(strstr(buf, "rows") == NULL, "the lines are gone: '%s'", buf);

    /* A full save of the settings writes the rows too. */
    config_defaults(&c);
    c.x11_rows = 61;
    c.terminal_rows = 29;
    CHECK(config_save(&c, err, sizeof err) == 0, "save: %s", err);
    config_load(&c);
    CHECK(c.x11_rows == 61 && c.terminal_rows == 29, "saved %d and %d",
          c.x11_rows, c.terminal_rows);

    /* A name that is a directory cannot be replaced: an error, no crash. */
    unlink(file);
    mkdir(file, 0700);
    CHECK(config_set_rows(1, 45, 0, err, sizeof err) != 0 && err[0] != '\0',
          "an error is reported");
    rmdir(file);
    unlink(file);
    rmdir(home);

    if (old != NULL) {
        setenv("STRATA_HOME", saved, 1);
    } else {
        unsetenv("STRATA_HOME");
    }
}


static void
spell_tests(const char *data, const char *twain)
{
    char path[512];
    char err[200];
    Text t;
    SpellWord w;

    /* What a word is (the rules were seen in the original). */
    words_are("It's a dont-stop wrold, isn't it?",
              "It's a dont-stop wrold isn't it");
    words_are("'qqe' qqf's qqg. qqh,qqi (qql) qqs- -qqt",
              "qqe qqf's qqg qqh,qqi qql qqs qqt");
    words_are("qqa.qqb qqm_qqn qqv/qqw", "qqa.qqb qqm_qqn qqv/qqw");
    words_are("1qqp qqo1 3.14 123 3rd x2y", "qqo1 x2y");
    words_are("--- ... ,,,", "");
    words_are("x^qz^t", "x qz t");
    words_are("ab|cd@ef~gh", "ab cd ef gh");
    words_are("  \"quoted\"  ", "quoted");
    words_are("rock-n-roll Dr. e.g.", "rock-n-roll Dr e.g");

    {
        char longw[100];

        memset(longw, 'a', 70);
        longw[70] = '\0';
        spell_text(&t, longw);
        CHECK(spell_find_word(&t, 0, t.n, &w) != 0 && w.too_long != 0 &&
                  w.len == 70,
              "a word of 70 letters is found and is too long");
        text_free(&t);
        memset(longw, 'a', 65);
        longw[65] = '\0';
        spell_text(&t, longw);
        CHECK(spell_find_word(&t, 0, t.n, &w) != 0 && w.too_long == 0,
              "a word of 65 letters is not too long");
        text_free(&t);
    }

    /* The word at the cursor. */
    spell_text(&t, "ab cdef gh");
    CHECK(spell_word_at(&t, 4, &w) != 0 && strcmp(w.text, "cdef") == 0,
          "word at a cursor inside a word");
    CHECK(spell_word_at(&t, 3, &w) != 0 && strcmp(w.text, "cdef") == 0,
          "word at a cursor on its first letter");
    CHECK(spell_word_at(&t, 7, &w) != 0 && strcmp(w.text, "cdef") == 0,
          "word at a cursor just after the word");
    CHECK(spell_word_at(&t, 2, &w) != 0 && strcmp(w.text, "ab") == 0,
          "word at a cursor on the space after a word: the word before");
    text_free(&t);

    /* The replacement keeps the attributes round the word. */
    spell_text(&t, "x^qz^t and");
    CHECK(spell_find_word(&t, 2, t.n, &w) != 0 && strcmp(w.text, "qz") == 0,
          "the bold word is qz");

    int n0 = t.n;
    int delta = spell_replace_word(&t, &w, "quiz");

    CHECK(delta == 2 && t.n == n0 + 2, "replacement grew by %d", delta);
    CHECK(t.v[1].kind == TK_ATTR && t.v[1].a == ATTR_BOLD &&
              t.v[6].kind == TK_ATTR && t.v[0].a == 'x' && t.v[7].a == 't',
          "the attribute tokens are where they were");
    CHECK(strcmp(spell_words(&t), "x quiz t and") == 0, "the text is now '%s'",
          spell_words(&t));
    spell_find_word(&t, 2, t.n, &w);
    delta = spell_replace_word(&t, &w, "q");
    CHECK(delta == -3 && strcmp(spell_words(&t), "x q t and") == 0,
          "a shorter word (%d, '%s')", delta, spell_words(&t));
    text_free(&t);

    /* A soft hyphen is inside the word: the word is whole, without it. */
    {
        Text st;
        const char *pieces[3] = {"hyph", "en", "ation"};

        text_init(&st);

        for (int p = 0; p < 3; p++) {
            for (const char *c = pieces[p]; *c != '\0'; c++) {
                text_insert(&st, st.n, tok_char(u8(*c)));
            }

            if (p < 2) {
                text_insert(&st, st.n, tok_shy());
            }
        }

        CHECK(spell_find_word(&st, 0, st.n, &w) != 0 &&
                  strcmp(w.text, "hyphenation") == 0 && w.len == 11 &&
                  w.start == 0 && w.end == st.n,
              "spell: a soft hyphen inside a word is skipped: '%s'", w.text);
        CHECK(spell_word_at(&st, 6, &w) != 0 &&
                  strcmp(w.text, "hyphenation") == 0,
              "spell: the word at a cursor behind a soft hyphen");

        int d0 = spell_replace_word(&st, &w, "hyphens");

        CHECK(st.n == 7 && d0 == 7 - 13, "spell: a replacement takes the "
                                           "soft hyphens too (%d)", st.n);
        text_free(&st);

        /* A soft hyphen at the edge of a word is not part of it. */
        text_init(&st);
        text_insert(&st, 0, tok_shy());
        text_insert(&st, 1, tok_char('o'));
        text_insert(&st, 2, tok_char('k'));
        text_insert(&st, 3, tok_shy());
        CHECK(spell_find_word(&st, 0, st.n, &w) != 0 && w.start == 1 &&
                  w.end == 3 && strcmp(w.text, "ok") == 0,
              "spell: soft hyphens at both ends are not in the word");
        text_free(&st);
    }

    /* The personal list: lower case entries stand for the capital too. */
    {
        SpellDict dd;
        char home[256];
        char file[300];

        snprintf(home, sizeof home, "%s/edtest_spell_home.%d", tmp_dir(),
                 getpid());
        mkdir(home, 0700);
        setenv("STRATA_HOME", home, 1);
        spell_dict_load(&dd);
        CHECK(dd.n == 0, "no list yet");
        CHECK(spell_dict_add(&dd, "wrold", err, sizeof err) == 0, "add: %s",
              err);
        CHECK(spell_dict_add(&dd, "Foo's", err, sizeof err) == 0, "add: %s",
              err);
        CHECK(spell_dict_has(&dd, "wrold") != 0 &&
                  spell_dict_has(&dd, "Wrold") != 0 &&
                  spell_dict_has(&dd, "WROLD") == 0,
              "an entry in lower case is also the word with a capital");
        CHECK(spell_dict_has(&dd, "Foo's") != 0 &&
                  spell_dict_has(&dd, "foo's") == 0,
              "an entry with a capital is only that word");
        spell_dict_free(&dd);
        spell_dict_load(&dd);
        CHECK(dd.n == 2 && spell_dict_has(&dd, "wrold") != 0,
              "the list is kept in the file");
        spell_dict_free(&dd);
        snprintf(file, sizeof file, "%s/dictionary", home);

        FILE *f = fopen(file, "r");
        char ln[64] = "";

        if (f != NULL) {
            if (fgets(ln, sizeof ln, f) == NULL) {
                ln[0] = '\0';
            }

            fclose(f);
        }

        CHECK(strcmp(ln, "wrold\n") == 0, "the file is plain text: '%s'", ln);
        unlink(file);
        rmdir(home);
    }

    /* The walk through a document with the fake checker. */
    snprintf(path, sizeof path, "%s/spell.doc", data);

    Doc *d = doc_load(path, err, sizeof err);

    CHECK(d != NULL, "load spell.doc: %s", err);

    if (d == NULL) {
        return;
    }

    char fake[300];
    char logf[300];

    /* The data directory is tests/data; the script is one up. */
    snprintf(fake, sizeof fake, "%s/../fakespell.sh", data);
    snprintf(logf, sizeof logf, "%s/edtest_spell_log.%d", tmp_dir(), getpid());

    unlink(logf);
    setenv("STRATA_SPELL", fake, 1);
    setenv("FAKESPELL_LOG", logf, 1);

    SpellProc *proc = spell_proc_open("en_US", err, sizeof err);

    CHECK(proc != NULL, "the fake checker starts: %s", err);

    if (proc == NULL) {
        doc_free(d);

        return;
    }

    {
        SpellDict dd;
        SpellRun *r;
        SpellHit h;
        static const char *const want[] = {"quikc",   "teh",  "dont-stop",
                                           "wrold",   "Strata3", "x2y",
                                           "foootnote", "zzq", "teh", "zzq"};
        int got = 0;
        int rc;
        int in_note_seen = 0;
        int cur_item = 0;
        int cur_col = 0;
        int cur_tok = 0;

        memset(&dd, 0, sizeof dd);
        r = spellrun_new(d, 0, d->nitems - 1, proc, &dd, 1);
        spellrun_track(r, &cur_item, &cur_col, &cur_tok);

        while ((rc = spellrun_next(r, 200, &h)) == SR_HIT ||
               rc == SR_BUSY) {
            if (rc == SR_BUSY) {
                continue;
            }

            CHECK(got < 10 && strcmp(h.word, want[got]) == 0,
                  "hit %d is '%s' (wanted '%s')", got, h.word,
                  got < 10 ? want[got] : "none");

            if (strcmp(h.word, "quikc") == 0) {
                CHECK(h.nsug == 2 && strcmp(h.sug[0], "quick") == 0,
                      "suggestions of quikc");
                spellrun_replace(r, "quick");
            } else if (strcmp(h.word, "teh") == 0 && got == 1) {
                spellrun_replace(r, "the");
            } else if (strcmp(h.word, "dont-stop") == 0) {
                CHECK(h.nsug == 1 && strcmp(h.sug[0], "don't-stop") == 0,
                      "the suggestion for a part is put in the word: '%s'",
                      h.sug[0]);
                spellrun_replace(r, h.sug[0]);
            } else if (strcmp(h.word, "Strata3") == 0) {
                CHECK(h.no_guess != 0 && h.nsug == 0,
                      "a word with a digit is flagged without a guess");
            } else if (strcmp(h.word, "foootnote") == 0) {
                CHECK(h.in_note != 0, "foootnote is in a note");
                in_note_seen = 1;
                spellrun_replace(r, "footnote");
            } else if (strcmp(h.word, "zzq") == 0 && got == 7) {
                spellrun_accept(r);
            }

            got++;
        }

        CHECK(rc == SR_DONE, "the walk ends (%d)", rc);
        CHECK(got == 8, "the number of hits is %d: the second teh was "
                        "corrected by itself and the second zzq accepted",
              got);
        CHECK(in_note_seen != 0, "the text of the note was checked");
        CHECK(spellrun_replaced(r) == 5,
              "five words were replaced (%d), one of them by Auto Correct",
              spellrun_replaced(r));
        spellrun_free(r);
    }

    CHECK(strstr(plain(d, 1, 0), "quick brown fox jumpd over the lazy") !=
              NULL,
          "quikc and teh are replaced in block 1: '%s'", plain(d, 1, 0));
    CHECK(strstr(flat(d, 1), "|It's a don't-stop wrold, isn't it?") != NULL,
          "dont-stop is replaced: '%s'", flat(d, 1));
    CHECK(strstr(plain(d, 4, 0), "and the end zzq") != NULL,
          "the second teh was replaced by itself: '%s'", plain(d, 4, 0));

    {
        /* The bold run of block 2 is still bold; the note has the new text. */
        const Text *t2 = &d->items[2].blk->cols[0].text;
        int bold = 0;

        for (int i = 0; i < t2->n; i++) {
            bold += t2->v[i].kind == TK_ATTR;
        }

        CHECK(bold == 2, "block 2 has its two attribute changes (%d)", bold);

        const Text *t3 = &d->items[3].blk->cols[0].text;
        char nb[200] = "";

        for (int i = 0; i < t3->n; i++) {
            if (t3->v[i].kind == TK_MARKER) {
                marker_nested_text(&t3->v[i], nb, sizeof nb);
            }
        }

        CHECK(strstr(nb, "A footnote here.") != NULL,
              "the text of the note was replaced: '%s'", nb);
        CHECK(d->modified != 0, "the document is changed");
    }

    /* The checker was fed in lines, never the document at once. */
    {
        int longest;
        int lines = file_lines(logf, &longest);

        CHECK(lines >= 6 && longest < 1600,
              "the checker got %d lines, the longest of %d bytes", lines,
              longest);
    }

    /* Auto Correct off: the second teh is asked again. */
    {
        Doc *d2 = doc_load(path, err, sizeof err);
        SpellRun *r = spellrun_new(d2, 0, d2->nitems - 1, proc, NULL, 0);
        SpellHit h;
        int tehs = 0;
        int rc;

        while ((rc = spellrun_next(r, 200, &h)) == SR_HIT ||
               rc == SR_BUSY) {
            if (rc == SR_HIT && strcmp(h.word, "teh") == 0) {
                tehs++;
                spellrun_replace(r, "the");
            }
        }

        CHECK(tehs == 2, "without Auto Correct both teh are asked (%d)", tehs);
        spellrun_free(r);

        /* Quit: the corrections that are still to make are made. */
        doc_free(d2);
        d2 = doc_load(path, err, sizeof err);
        r = spellrun_new(d2, 0, d2->nitems - 1, proc, NULL, 1);

        while ((rc = spellrun_next(r, 200, &h)) == SR_BUSY) {
        }

        CHECK(rc == SR_HIT && strcmp(h.word, "quikc") == 0, "first hit");
        spellrun_replace(r, "quick");
        rc = spellrun_next(r, 200, &h);
        CHECK(rc == SR_HIT && strcmp(h.word, "teh") == 0, "second hit");
        spellrun_replace(r, "the");
        CHECK(spellrun_pending_autocorrect(r) != 0, "corrections are pending");
        spellrun_finish_autocorrect(r);
        CHECK(strstr(plain(d2, 4, 0), "and the end") != NULL,
              "Quit, Yes makes the corrections in the rest: '%s'",
              plain(d2, 4, 0));
        spellrun_free(r);
        doc_free(d2);
    }

    /* A range of items: only block 4. */
    {
        Doc *d2 = doc_load(path, err, sizeof err);
        SpellRun *r = spellrun_new(d2, 4, 4, proc, NULL, 0);
        SpellHit h;
        int rc;
        int hits = 0;

        while ((rc = spellrun_next(r, 200, &h)) == SR_HIT ||
               rc == SR_BUSY) {
            hits += rc == SR_HIT;
        }

        CHECK(hits == 2, "block 4 alone has teh and zzq (%d)", hits);
        spellrun_free(r);
        doc_free(d2);
    }

    /* The Line: change the characters of a line, keep the attributes. */
    {
        Doc *d2 = doc_load(path, err, sizeof err);
        SpellRun *r = spellrun_new(d2, 2, 2, proc, NULL, 0);
        SpellHit h;
        int rc;
        char line[300];

        while ((rc = spellrun_next(r, 200, &h)) == SR_BUSY) {
        }

        CHECK(rc == SR_HIT && strcmp(h.word, "Strata3") == 0,
              "block 2 first hit is Strata3");
        spellrun_line(r, line, sizeof line);
        CHECK(strncmp(line, "xqzt and a tab", 14) == 0, "the line is '%s'",
              line);
        CHECK(spellrun_set_line(r, "xqzt and a tab\x10word. Strata and 3rd "
                                   "and x2y.") == 0,
              "the line can be changed");
        CHECK(strstr(plain(d2, 2, 0), "Strata and 3rd") != NULL,
              "the line was changed: '%s'", plain(d2, 2, 0));
        CHECK(spellrun_set_line(r, "xqzt and a tabword. Strata and 3rd and "
                                   "x2y.") == -1,
              "a tab cannot be deleted by editing the line");
        spellrun_free(r);
        doc_free(d2);
    }

    /* The context of a hit: the line before, a tab as 0x10, a marker as 4. */
    {
        SpellRun *r = spellrun_new(d, 3, 3, proc, NULL, 0);
        SpellHit h;
        int rc;
        char ctx[400];
        int hs;
        int he;

        while ((rc = spellrun_next(r, 200, &h)) == SR_BUSY) {
        }

        spellrun_context(r, ctx, sizeof ctx, &hs, &he, 2);
        CHECK(rc == SR_HIT && strcmp(h.word, "zzq") == 0 &&
                  strstr(ctx, "Befor\x04 Aftr zzq.") != NULL &&
                  strncmp(ctx + hs, "zzq", 3) == 0 && he - hs == 3,
              "context of zzq: '%s' %d..%d", ctx, hs, he);
        spellrun_free(r);
    }

    spell_proc_close(proc);
    doc_free(d);
    unsetenv("STRATA_SPELL");
    unsetenv("FAKESPELL_LOG");
    unlink(logf);

    /* The real checker, when there is one (no variable, so PATH). */
    {
        char prog[300];

        if (spell_find_program(prog, sizeof prog) != 0) {
            printf("edtest: no aspell or hunspell in PATH: the test of the "
                   "real checker is skipped\n");

            return;
        }

        proc = spell_proc_open("en_US", err, sizeof err);

        if (proc == NULL) {
            printf("edtest: %s is there but did not start (%s): the test of "
                   "the real checker is skipped\n",
                   prog, err);

            return;
        }

        SpellMiss m;
        char sug[SPELL_MAXSUG][SPELL_MAXWORD + 1];
        int rc = spell_proc_check_word(proc, "quikc", &m);
        int n = rc == 0 ? spell_whole_words("quikc", 0, &m, sug,
                                            SPELL_MAXSUG) :
                          0;
        int found = 0;

        for (int i = 0; i < n; i++) {
            found += strcmp(sug[i], "quick") == 0;
        }

        CHECK(rc == 0 && found == 1, "%s: quikc is wrong and quick is "
                                     "suggested (%d, %d)",
              prog, rc, n);
        CHECK(spell_proc_check_word(proc, "brown", &m) == 1,
              "%s: brown is right", prog);
        CHECK(spell_proc_check_word(proc, "dont-stop", &m) == 0 &&
                  strcmp(m.orig, "dont") == 0,
              "%s: dont-stop is wrong, in the part dont", prog);
        printf("edtest: the real checker %s was used\n", prog);

        /* A document with it. */
        snprintf(path, sizeof path, "%s/spell.doc", data);
        d = doc_load(path, err, sizeof err);

        SpellRun *r = spellrun_new(d, 0, d->nitems - 1, proc, NULL, 0);
        SpellHit h;
        int hits = 0;
        int first_ok = 0;

        while ((rc = spellrun_next(r, 500, &h)) == SR_HIT ||
               rc == SR_BUSY) {
            if (rc == SR_HIT) {
                hits++;
                first_ok += hits == 1 && strcmp(h.word, "quikc") == 0 &&
                            strcmp(h.sug[0], "quick") == 0;
            }
        }

        CHECK(first_ok == 1 && hits > 8, "real checker on spell.doc: %d hits",
              hits);
        spellrun_free(r);
        doc_free(d);

        /* Time on the big document: the speed and the longest wait. */
        if (twain != NULL && access(twain, R_OK) == 0) {
            Doc *big = doc_load(twain, err, sizeof err);

            if (big != NULL) {
                SpellRun *br = spellrun_new(big, 0, big->nitems - 1, proc,
                                            NULL, 0);
                const char *tms = getenv("SPELL_TWAIN_MS");
                long twain_ms = tms != NULL ? atol(tms) : 15000;
                long t0 = ms_now();
                long worst = 0;
                long last = t0;
                int bh = 0;

                while (ms_now() - t0 < twain_ms &&
                       ((rc = spellrun_next(br, 40, &h)) == SR_HIT ||
                        rc == SR_BUSY)) {
                    long now = ms_now();

                    if (now - last > worst) {
                        worst = now - last;
                    }

                    last = now;
                    bh += rc == SR_HIT;
                }

                printf("edtest: twain.doc, 15 MB: %d unknown words in %ld ms "
                       "(stopped after %ld ms at most), the longest wait "
                       "between two calls %ld ms\n",
                       bh, ms_now() - t0, twain_ms, worst);
                CHECK(worst < 1000, "the editor would freeze for %ld ms",
                      worst);
                spellrun_free(br);
                doc_free(big);
            }
        }

        spell_proc_close(proc);
    }
}




/* The words of the sense s of an entry as one string, "a,b,c". */
static const char *
sense_words(const ThEntry *en, int s)
{
    static char b[512];

    b[0] = '\0';

    size_t len = 0;

    for (int i = 0; i < en->senses[s].nwords; i++) {
        const char *w = en->senses[s].words[i];

        if (i > 0 && len + 1 < sizeof b) {
            b[len++] = ',';
        }

        for (; *w != '\0' && len + 1 < sizeof b; w++) {
            b[len++] = *w;
        }
    }

    b[len] = '\0';

    return b;
}


/* Does the entry have the word w in any sense? */
static int
entry_has(const ThEntry *en, const char *w)
{
    for (int s = 0; s < en->nsenses; s++) {
        for (int i = 0; i < en->senses[s].nwords; i++) {
            if (strcmp(en->senses[s].words[i], w) == 0) {
                return 1;
            }
        }
    }

    return 0;
}


static void
write_text_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");

    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}


/* The tests of the thesaurus: the reader, the lookup and the word finder. */
static void
thesaurus_tests(const char *datadir, const char *scratch)
{
    ThEntry en;
    char names[TH_LIST][TH_HEAD];
    char out[TH_HEAD];
    int mark;
    char path[512];

    th_use(datadir);
    CHECK(th_available() == 1, "the test thesaurus opens (%s)", th_path());

    /* Look-up: the plain words, then the similar terms, nothing else. */
    CHECK(th_lookup("quick", &en) == 1 && strcmp(en.head, "quick") == 0,
          "quick is found");
    CHECK(en.nsenses == 3, "quick has 3 senses (%d)", en.nsenses);
    CHECK(en.nsenses == 3 && strcmp(en.senses[0].pos, "noun") == 0 &&
              strcmp(sense_words(&en, 0), "center,core,next day") == 0,
          "the related term is not shown (%s)", sense_words(&en, 0));
    CHECK(en.nsenses == 3 &&
              strcmp(sense_words(&en, 1), "fast,rapid,swift,speedy,brisk,"
                                          "nimble") == 0,
          "similar terms follow the synonyms, antonyms and generic terms "
          "are left out (%s)", sense_words(&en, 1));
    CHECK(en.nsenses == 3 && strcmp(en.senses[2].pos, "adv") == 0,
          "a sense that has no word to show is dropped");
    CHECK(entry_has(&en, "next day") == 1, "a word of two parts is kept");
    th_free(&en);

    CHECK(th_lookup("Section", &en) == 1 && en.nsenses == 3 &&
              entry_has(&en, "piece of writing") == 1 &&
              strcmp(en.senses[2].pos, "verb") == 0,
          "section: senses of two parts of speech");
    th_free(&en);
    CHECK(th_lookup("happy", &en) == 1 && en.nsenses == 2 &&
              entry_has(&en, "sad") == 0 && entry_has(&en, "lucky") == 1,
          "happy: the antonym is left out");
    th_free(&en);
    CHECK(th_lookup("piece of writing", &en) == 1 && entry_has(&en,
          "article") == 1, "a headword of several words is found");
    th_free(&en);

    /* Forms of a word are found under the stem, as typed. */
    CHECK(th_lookup("sections", &en) == 1 && strcmp(en.head, "section") == 0,
          "sections is found as section");
    th_free(&en);
    CHECK(th_lookup("HEADLINES", &en) == 1 &&
              strcmp(en.head, "headline") == 0,
          "headlines is found in any case");
    th_free(&en);
    CHECK(th_lookup("running", &en) == 1 && strcmp(en.head, "run") == 0,
          "running is found as run (doubled consonant)");
    th_free(&en);
    CHECK(th_lookup("ran", &en) == 0, "ran is not found");
    CHECK(th_lookup("zebra", &en) == 2 && strcmp(en.head, "zebra") == 0 &&
          en.nsenses == 0 && en.senses == NULL,
          "zebra has only a generic term: the headword, and no synonym");
    CHECK(th_lookup("Zebras", &en) == 2 && strcmp(en.head, "zebra") == 0,
          "zebras has no synonym either, under its stem");
    CHECK(th_lookup("", &en) == 0 && th_lookup("   ", &en) == 0,
          "an empty word is not found");
    CHECK(th_stem("Running", 0, out) == 1 && strcmp(out, "running") == 0,
          "the first stem is the word in lower case");

    int found = 0;

    for (int i = 0; th_stem("running", i, out) != 0; i++) {
        found += strcmp(out, "run") == 0;
    }

    CHECK(found == 1, "run is a stem of running");
    found = 0;

    for (int i = 0; th_stem("tries", i, out) != 0; i++) {
        found += strcmp(out, "try") == 0;
    }

    CHECK(found == 1, "try is a stem of tries");
    found = 0;

    for (int i = 0; th_stem("boxes", i, out) != 0; i++) {
        found += strcmp(out, "box") == 0;
    }

    CHECK(found == 1, "box is a stem of boxes");
    CHECK(th_stem("x", 1, out) == 0, "a one letter word has no stem");

    /* Similar words for a word that was not found. */
    int n = th_neighbours("abcd", 0, TH_LIST, 5, names, &mark);

    CHECK(n >= 5 && strcmp(names[mark], "abbot") == 0 &&
              strcmp(names[0], "abbey") == 0,
          "abcd: abbot is the nearest word before it (%d, %s)", n,
          names[mark]);
    CHECK(strcmp(names[n - 1], "abide") == 0 || n == TH_LIST,
          "the list goes on after the word");
    n = th_neighbours("zzzqx", 0, TH_LIST, 5, names, &mark);
    CHECK(n >= 2 && strcmp(names[n - 1], "zygote") == 0 && mark == n - 1,
          "zzzqx: the end of the dictionary ends the list (%d)", n);
    n = th_neighbours("a", 0, TH_LIST, 5, names, &mark);
    CHECK(n == TH_LIST && mark == 0 && strcmp(names[0], "abbey") == 0,
          "a word before the first one: the list starts at the first");
    n = th_neighbours("abbey", 3, 1, 0, names, &mark);
    CHECK(n == 1 && strcmp(names[0], "abduct") == 0,
          "a step of 3 from abbey is abduct (%s)", names[0]);
    n = th_neighbours("abbey", -3, 1, 0, names, &mark);
    CHECK(n == 1 && strcmp(names[0], "abbey") == 0,
          "a step before the first word stays at it");
    n = th_neighbours("zygote", 2, 1, 0, names, &mark);
    CHECK(n == 1 && strcmp(names[0], "zygote") == 0,
          "a step past the last word stays at it");

    /* The case of the replacement. */
    th_match_case("Quick", "center", out, sizeof out);
    CHECK(strcmp(out, "Center") == 0, "Quick -> Center (%s)", out);
    th_match_case("QUICK", "center", out, sizeof out);
    CHECK(strcmp(out, "CENTER") == 0, "QUICK -> CENTER (%s)", out);
    th_match_case("quick", "Center", out, sizeof out);
    CHECK(strcmp(out, "Center") == 0, "quick keeps the word (%s)", out);
    th_match_case("Quick", "next day", out, sizeof out);
    CHECK(strcmp(out, "Next day") == 0, "only the first letter (%s)", out);
    th_match_case("QUICK", "next day", out, sizeof out);
    CHECK(strcmp(out, "NEXT DAY") == 0, "all capitals (%s)", out);
    th_match_case("A", "an", out, sizeof out);
    CHECK(strcmp(out, "An") == 0, "one capital letter is a capital (%s)", out);
    th_match_case("don't", "do not", out, sizeof out);
    CHECK(strcmp(out, "do not") == 0, "lower case stays (%s)", out);

    /* Damaged and empty data must give "not found", never a crash. */
    snprintf(path, sizeof path, "%s.thd", scratch);
    write_text_file(path, "");
    char idx[600];
    char dat[600];

    snprintf(idx, sizeof idx, "%s.idx", path);
    snprintf(dat, sizeof dat, "%s.dat", path);
    write_text_file(idx, "UTF-8\n3\nabc|0\nbad|99999\ncut|30\n");
    write_text_file(dat, "UTF-8\nabc|2\n(noun)|one|two\n");
    th_use(path);
    CHECK(th_available() == 1, "damaged data opens");
    CHECK(th_lookup("abc", &en) == 0, "an entry with a wrong head is not "
          "found");
    CHECK(th_lookup("bad", &en) == 0, "an offset past the end is not found");
    CHECK(th_lookup("cut", &en) == 0, "an offset into the middle is not "
          "found");
    n = th_neighbours("b", 0, TH_LIST, 5, names, &mark);
    CHECK(n == 3, "the names of damaged data are listed (%d)", n);
    write_text_file(idx, "UTF-8\n1\nabc|6\n");
    write_text_file(dat, "UTF-8\nabc|3\n(noun)|one|two\n(noun)|three\n");
    th_use(path);
    CHECK(th_lookup("abc", &en) == 0, "fewer lines than the count are not "
          "found");
    write_text_file(idx, "UTF-8\n0\n");
    write_text_file(dat, "UTF-8\n");
    th_use(path);
    CHECK(th_available() == 1 && th_lookup("abc", &en) == 0 &&
              th_neighbours("abc", 0, TH_LIST, 5, names, &mark) == 0,
          "an empty dictionary finds nothing");
    unlink(idx);
    unlink(dat);
    unlink(path);

    th_use("");
    CHECK(th_available() == 0 && th_lookup("quick", &en) == -1 &&
              th_neighbours("a", 0, TH_LIST, 5, names, &mark) == 0,
          "an empty path means that there is no thesaurus");
    th_use("/nonexistent/thesaurus");
    CHECK(th_available() == 0, "a wrong path means that there is none");
    snprintf(path, sizeof path, "%s/th_test.dat", datadir);
    th_use(path);
    CHECK(th_available() == 1, "the .dat file can be named");
    snprintf(path, sizeof path, "%s/th_test", datadir);
    th_use(path);
    CHECK(th_available() == 1, "the path without a suffix can be named");
    th_use(datadir);

    /* The word finder and the replacement, on a document. */
    Doc *d = doc_new(1);
    Editor *e = ed_new(d);
    int from;
    int to;
    char w[TH_HEAD];
    const char *line = "Quick sections don't re-enter 123 end.";

    ed_layout(e);
    type(e, line);

    Text *t = ed_text(e);
    int endtok = e->cur.tok;

    e->cur.tok = 0;
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 && from == 0 &&
              to == 5 && strcmp(w, "Quick") == 0,
          "word: at the start of Quick");
    e->cur.tok = 2;
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 &&
              strcmp(w, "Quick") == 0, "word: in the middle");
    e->cur.tok = 5;
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 &&
              strcmp(w, "Quick") == 0 && to == 5,
          "word: just after its last letter");
    e->cur.tok = 6;
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 &&
              strcmp(w, "sections") == 0, "word: at the start of the next");

    int p = plen(strstr(line, "don't"), line);

    e->cur.tok = p + 3;
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 &&
              strcmp(w, "don't") == 0, "word: an apostrophe is inside");
    p = plen(strstr(line, "re-enter"), line);
    e->cur.tok = p + 4;
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 &&
              strcmp(w, "re-enter") == 0, "word: a hyphen is inside");
    p = plen(strstr(line, "123"), line);
    e->cur.tok = p + 1;
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 &&
              strcmp(w, "re-enter") == 0,
          "word: on digits the word before is used");
    e->cur.tok = endtok;
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 &&
              strcmp(w, "end") == 0 && to == endtok - 1,
          "word: at the end of the line the word before the full stop");
    e->cur.tok = 0;

    /* A word that is cut. */
    ed_append_block(e, 1);
    for (int i = 0; i < 70; i++) {
        ed_insert_char(e, 'a');
    }

    e->cur.tok = 0;
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 &&
              slen(w) == THES_MAXWORD, "word: a long word is cut (%d)",
          slen(w));

    /* No word: an empty line, or a line of spaces. */
    ed_append_block(e, 1);
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 0,
          "word: none on an empty line");
    type(e, "   ");
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 0,
          "word: none on a line of spaces");

    /* Attributes and markers end a word. */
    ed_append_block(e, 1);
    type(e, "x ");
    ed_toggle_attr(e, ATTR_BOLD);
    type(e, "sec");
    ed_toggle_attr(e, ATTR_BOLD);
    type(e, "tion");
    t = ed_text(e);
    e->cur.tok = 7;
    CHECK(t->v[6].kind == TK_ATTR, "the test text has the attribute at 6");
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 &&
              strcmp(w, "tion") == 0 && from == 7,
          "word: an attribute ends the word (tion)");
    e->cur.tok = 3;
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 &&
              strcmp(w, "sec") == 0, "word: the first part (sec)");
    e->cur.tok = 6;
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 &&
              strcmp(w, "sec") == 0 && to == 6,
          "word: on the attribute the word before it");

    ed_append_block(e, 1);
    type(e, "before");

    static const uint8_t mdata[] = "11\r\n";

    ed_insert_marker(e, 36, mdata, sizeof mdata - 1);
    type(e, "happy");
    t = ed_text(e);
    e->cur.tok = 6;
    CHECK(t->v[6].kind == TK_MARKER, "the test text has a marker at 6");
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 &&
              strcmp(w, "before") == 0, "word: on a marker, the word before");
    e->cur.tok = 7;
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 &&
              strcmp(w, "happy") == 0, "word: after a marker");

    /* The replacement keeps the attribute, moves the cursor and marks the
     * document changed. */
    ed_append_block(e, 1);
    type(e, "a ");
    ed_toggle_attr(e, ATTR_BOLD);
    type(e, "happy");
    ed_toggle_attr(e, ATTR_BOLD);
    type(e, " end");
    t = ed_text(e);
    e->cur.tok = 3;
    CHECK(ed_word_at(e, &from, &to, w, sizeof w) == 1 &&
              strcmp(w, "happy") == 0 && from == 3 && to == 8,
          "word: the bold word");
    d->modified = 0;
    th_match_case(w, "joyful", out, sizeof out);
    ed_replace_range(e, from, to, bytes(out), slen(out));
    t = ed_text(e);
    CHECK(t->v[2].kind == TK_ATTR && t->v[2].a == ATTR_BOLD &&
              t->v[2 + 6 + 1].kind == TK_ATTR &&
              t->v[2 + 6 + 1].a == 0,
          "the replacement keeps the attribute tokens");
    CHECK(e->cur.tok == 3 + 6, "the cursor is after the new word (%d)",
          e->cur.tok);
    CHECK(d->modified != 0, "the document is marked changed");
    CHECK(strcmp(plain(d, e->cur.item, 0), "a joyful end") == 0, "text: %s",
          plain(d, e->cur.item, 0));

    ed_free(e);
    doc_free(d);
    th_use(NULL);
}


/* The conversion of Case (Ctrl-F6) and its command on a small document. */
static void
case_string(const char *in, int mode, char *out, size_t cap)
{
    int in_word = 0;
    size_t n = 0;

    for (const char *p = in; *p != '\0' && n + 1 < cap; p++) {
        out[n++] = (char) ed_case_char((unsigned char) *p, mode, &in_word);
    }

    out[n] = '\0';
}


/*
 * The attribute that the status line names is the one that the screen draws:
 * the attribute of the format of the block joined with the attributes that
 * the text set.  A headline is bold by the format and has no attribute mark
 * of its own.
 */
static void
attr_effective_tests(const char *sample)
{
    char path[512];
    char err[256];

    snprintf(path, sizeof path, "%s/plan-title-table.doc", sample);
    Doc *d = doc_load(path, err, sizeof err);

    CHECK(d != NULL, "attr: load: %s", err);

    if (d == NULL) {
        return;
    }

    Editor *e = ed_new(d);

    ed_layout(e);
    ed_move(e, MV_DOC_START, 24);
    CHECK(ed_attr_at(e) == 0, "attr: the headline holds no attribute mark");
    CHECK((ed_attr_effective(e) & 1) != 0,
          "attr: the headline is bold by its format");

    ed_toggle_attr(e, ATTR_ITALIC);
    CHECK((ed_attr_effective(e) & 1) != 0 &&
          (ed_attr_effective(e) & 4) != 0,
          "attr: italic set in a bold headline gives bold italic");

    ed_move(e, MV_DOWN, 24);
    ed_move(e, MV_DOWN, 24);
    ed_move(e, MV_DOWN, 24);
    CHECK(ed_attr_effective(e) == ed_attr_at(e),
          "attr: a text block has the attribute of its marks only");
    ed_free(e);
    doc_free(d);
}


static void
case_tests(void)
{
    char out[128];

    case_string("hello World 3rd don't WELL-known", CASE_UPPER, out,
                sizeof out);
    CHECK(strcmp(out, "HELLO WORLD 3RD DON'T WELL-KNOWN") == 0,
          "upper: %s", out);
    case_string("Hello WORLD 3RD", CASE_LOWER, out, sizeof out);
    CHECK(strcmp(out, "hello world 3rd") == 0, "lower: %s", out);
    case_string("hello wORLD 3rd don't well-KNOWN a", CASE_CAPITALIZE, out,
                sizeof out);
    CHECK(strcmp(out, "Hello World 3rd Don't Well-Known A") == 0,
          "capitalize: %s", out);

    /* The code page 437: the pairs change, the letters with one case and the
       sharp s stay. */
    case_string("\x81\x82\x84\x86\x87\x91\x94\xA4", CASE_UPPER, out,
                sizeof out);
    CHECK(strcmp(out, "\x9A\x90\x8E\x8F\x80\x92\x99\xA5") == 0,
          "upper case of the accented letters");
    case_string("\x9A\x90\x8E\x8F\x80\x92\x99\xA5", CASE_LOWER, out,
                sizeof out);
    CHECK(strcmp(out, "\x81\x82\x84\x86\x87\x91\x94\xA4") == 0,
          "lower case of the accented letters");
    case_string("\x85\x88\x95\x97\x98\xA0\xA3\xE1", CASE_UPPER, out,
                sizeof out);
    CHECK(strcmp(out, "\x85\x88\x95\x97\x98\xA0\xA3\xE1") == 0,
          "letters with one case and the sharp s stay");
    case_string("\x82tude \x94l \xE1", CASE_CAPITALIZE, out, sizeof out);
    CHECK(strcmp(out, "\x90tude \x99l \xE1") == 0,
          "capitalize with accents: %s", out);
    case_string("a1!\xB0\xDB.", CASE_UPPER, out, sizeof out);
    CHECK(strcmp(out, "A1!\xB0\xDB.") == 0, "other characters stay");

    /* The command on a document: text, a column, blocks, and the word under
       the cursor. */
    Doc *d = doc_new(0);
    Block *b1 = block_new(1);
    Block *b2 = block_new(1);
    static const char *const t1 = "one two";
    static const char *const t2 = "three four";

    doc_delete_items(d, 0, d->nitems);
    doc_insert_item(d, 0, IT_SECT, NULL);
    text_free(&b1->cols[0].text);
    text_init(&b1->cols[0].text);
    text_free(&b2->cols[0].text);
    text_init(&b2->cols[0].text);

    for (const char *p = t1; *p != '\0'; p++) {
        text_insert(&b1->cols[0].text, b1->cols[0].text.n, tok_char(u8(*p)));
    }

    for (const char *p = t2; *p != '\0'; p++) {
        text_insert(&b2->cols[0].text, b2->cols[0].text.n, tok_char(u8(*p)));
    }

    doc_insert_item(d, 1, IT_BLOCK, b1);
    doc_insert_item(d, 2, IT_BLOCK, b2);

    Editor *e = ed_new(d);

    e->text_width = 80;
    ed_layout(e);

    char got[64];
    Text *t;

    /* Nothing is highlighted: the word under the cursor. */
    e->cur.item = 1;
    e->cur.col = 0;
    e->cur.tok = 5;
    e->sel.kind = SEL_TEXT;
    e->sel.anchor = e->cur;
    CHECK(ed_case_apply(e, CASE_UPPER) == 0, "the word under the cursor");
    t = ed_text(e);
    text_plain(t, got, sizeof got);
    CHECK(strcmp(got, "one TWO") == 0, "the word is changed: %s", got);

    /* A highlight of text. */
    e->cur.tok = 0;
    e->sel.kind = SEL_TEXT;
    e->sel.anchor = e->cur;
    e->cur.tok = 3;
    CHECK(ed_case_apply(e, CASE_CAPITALIZE) == 0, "text");
    text_plain(ed_text(e), got, sizeof got);
    CHECK(strcmp(got, "One TWO") == 0, "text is capitalized: %s", got);

    /* The blocks from the cursor to another block. */
    e->cur.item = 1;
    e->sel.kind = SEL_BLOCK;
    e->sel.anchor = e->cur;
    e->cur.item = 2;
    CHECK(ed_case_apply(e, CASE_UPPER) == 0, "blocks");
    e->cur.item = 1;
    text_plain(ed_text(e), got, sizeof got);
    CHECK(strcmp(got, "ONE TWO") == 0, "the first block: %s", got);
    e->cur.item = 2;
    text_plain(ed_text(e), got, sizeof got);
    CHECK(strcmp(got, "THREE FOUR") == 0, "the second block: %s", got);

    /* A column. */
    e->sel.kind = SEL_COLUMN;
    e->sel.anchor = e->cur;
    CHECK(ed_case_apply(e, CASE_LOWER) == 0, "column");
    text_plain(ed_text(e), got, sizeof got);
    CHECK(strcmp(got, "three four") == 0, "the column is lower case: %s", got);

    /* A cursor that is not at a word changes nothing and says so. */
    text_insert(&b1->cols[0].text, 0, tok_char(' '));
    e->cur.item = 1;
    e->cur.tok = 0;
    e->sel.kind = SEL_TEXT;
    e->sel.anchor = e->cur;
    CHECK(ed_case_apply(e, CASE_LOWER) == -1 &&
              strstr(e->msg, "No word under the cursor") != NULL,
          "no word: %s", e->msg);

    ed_free(e);
    doc_free(d);
}



int
main(int argc, char **argv)
{
    const char *sample = argc > 1 ? argv[1] : "tests/samples";
    char path[512];
    char err[128];

    /* SPELL_ONLY: only the tests of the spell check (for a sanitizer run). */
    if (getenv("SPELL_ONLY") != NULL) {
        const char *tw = getenv("SPELL_TWAIN");

        spell_tests(argc > 3 ? argv[3] : "tests/data",
                    tw != NULL ? tw : "../sample.docs/twain.doc");
        rows_tests();
        printf(fails != 0 ? "edtest: %d FAILURES\n" : "edtest: all passed\n",
               fails);

        return fails != 0;
    }

    snprintf(path, sizeof path, "%s/brief-long.doc", sample);
    Doc *d = doc_load(path, err, sizeof err);
    CHECK(d != NULL, "load: %s", err);
    if (d == NULL) {
        return 1;
    }

    case_tests();
    attr_effective_tests(sample);

    Editor *e = ed_new(d);

    e->text_width = 80;
    ed_layout(e);
    CHECK(e->view.n > 20, "view has %d lines", e->view.n);

    /* navigation */
    ed_move(e, MV_DOC_START, 24);
    CHECK(strstr(plain(d, e->cur.item, 0), "Aims and Approach") != NULL,
          "doc start on "
          "first headline: '%s'",
          plain(d, e->cur.item, 0));
    for (int i = 0; i < 10; i++) {
        ed_move(e, MV_RIGHT, 24);
    }
    CHECK(e->cur.tok == 10, "right x10 -> tok %d", e->cur.tok);
    ed_move(e, MV_DOWN, 24);
    ed_move(e, MV_DOWN, 24);
    CHECK(e->cur.item > 3, "down moved to item %d", e->cur.item);
    ed_move(e, MV_SECT_NEXT_HEAD, 24);
    CHECK(strstr(plain(d, e->cur.item, 0), "Land and Layout") != NULL,
          "next headline: '%s'", plain(d, e->cur.item, 0));

    /* typing + attribute */
    ed_move(e, MV_BLOCK_END, 24);
    type(e, " EDITED");
    CHECK(strstr(plain(d, e->cur.item, 0), "EDITED") != NULL, "typed text");
    ed_toggle_attr(e, ATTR_BOLD);
    type(e, "bold");
    ed_toggle_attr(e, ATTR_BOLD);

    /* append section / subsection keep structure balanced */
    int nsec = count_type(d, IT_SECT);

    ed_append_section(e, 0);
    type(e, "New Peer Section");
    ed_append_section(e, 1);
    type(e, "New Subsection");
    CHECK(count_type(d, IT_SECT) == nsec + 2, "sections %d",
          count_type(d, IT_SECT));
    CHECK(balanced(d) != 0, "balanced after append");
    ed_append_block(e, 1);
    type(e, "body text of the subsection");
    ed_split(e, 1); /* split at end: new empty block */
    CHECK(strcmp(plain(d, e->cur.item, 0), "") == 0, "split produced empty "
                                                     "block");
    type(e, "second");
    ed_move(e, MV_UP, 24);
    CHECK(ed_join(e, 1) == 0, "join blocks");
    CHECK(strstr(plain(d, e->cur.item, 0), "subsectionsecond") != NULL,
          "joined: '%s'", plain(d, e->cur.item, 0));

    /* folding */
    ed_move(e, MV_DOC_START, 24);
    ed_fold(e, 3, 1); /* outline level 1 */
    ed_layout(e);
    int vis = 0;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == IT_BLOCK && e->view.bv[i].visible != 0) {
            vis++;
        }
    }
    CHECK(vis < 15, "collapsed to %d visible blocks", vis);
    ed_fold(e, 3, 1);
    ed_layout(e);
    /* toggle twice -> all visible again */
    ed_fold(e, 4, 0);
    ed_fold(e, 4, 0);
    ed_layout(e);
    int vis2 = 0;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == IT_BLOCK && e->view.bv[i].visible != 0) {
            vis2++;
        }
    }

    for (int i = 0; i < d->nitems; i++) {
        d->items[i].fold = 0;
    }

    ed_invalidate(e);
    ed_layout(e);

    /* search + replace */
    ed_move(e, MV_DOC_START, 24);
    CHECK(ed_search(e, "frost", 1, 1) == 1, "search found");
    CHECK(ed_search(e, "zzzzqqq", 1, 1) == 0, "search miss");
    int n = ed_replace_all(e, "club", "XYZ", 0);
    CHECK(n > 0, "replace count %d", n);

    /* The no-break space: typed as the byte FF, kept as the character 1E,
       found by a string that holds FF, ended by a word count. */
    {
        int w0;
        int w1;
        int dummy;

        ed_doc_stats(d, &w0, &dummy, &dummy, &dummy);
        ed_move(e, MV_DOC_START, 24);
        ed_move(e, MV_BLOCK_START, 24);

        int start = e->cur.tok;
        const Text *nt;

        type(e, "ZQ");
        ed_insert_char(e, NBSP_BYTE);
        type(e, "ZR ");
        nt = ed_text(e);
        CHECK(nt->v[start + 2].kind == TK_CTRL &&
                  nt->v[start + 2].a == NBSP_CTRL,
              "a typed no-break space is the character 1E");
        ed_doc_stats(d, &w1, &dummy, &dummy, &dummy);
        CHECK(w1 == w0 + 2, "a no-break space ends a word: %d to %d", w0, w1);
        ed_move(e, MV_DOC_START, 24);
        CHECK(ed_search(e, "ZQ\xffZR", 1, 0) == 1, "search for a no-break "
                                                     "space");
        ed_move(e, MV_DOC_START, 24);
        CHECK(ed_search(e, "ZQ ZR", 1, 0) == 0, "a space is not a no-break "
                                                "space");
        CHECK(ed_replace_all(e, "ZQ\xffZR", "ZS\xffZT", 0) == 1,
              "replace a no-break space");
        nt = ed_text(e);
        CHECK(nt->v[start + 2].kind == TK_CTRL &&
                  nt->v[start + 2].a == NBSP_CTRL &&
                  nt->v[start + 1].a == 'S' && nt->v[start + 3].a == 'Z',
              "the replacement holds a no-break space");
    }

    /* The soft hyphen: typed as 1F, held as the character 1F, found by a
       search that goes through it, no part of a word or of the count. */
    {
        int w0;
        int w1;
        int c0;
        int c1;
        int dummy;

        ed_doc_stats(d, &w0, &c0, &dummy, &dummy);
        ed_move(e, MV_DOC_START, 24);
        ed_move(e, MV_BLOCK_START, 24);

        int start = e->cur.tok;
        const Text *nt;

        type(e, "ZQ");
        ed_insert_char(e, SHY_CTRL);
        type(e, "ZR ");
        nt = ed_text(e);
        CHECK(nt->v[start + 2].kind == TK_CTRL &&
                  nt->v[start + 2].a == SHY_CTRL,
              "a typed soft hyphen is the character 1F");
        ed_doc_stats(d, &w1, &c1, &dummy, &dummy);
        CHECK(w1 == w0 + 1, "a soft hyphen does not end a word: %d to %d", w0,
              w1);
        CHECK(c1 == c0 + 5, "a soft hyphen is not counted: %d to %d", c0, c1);
        ed_move(e, MV_DOC_START, 24);
        CHECK(ed_search(e, "ZQZR", 1, 0) == 1, "search goes through a soft "
                                               "hyphen");
        ed_move(e, MV_DOC_START, 24);
        CHECK(ed_replace_all(e, "ZQZR", "ZSZT", 0) == 1,
              "replace takes the soft hyphen with the word");
        nt = ed_text(e);
        CHECK(nt->v[start].a == 'Z' && nt->v[start + 1].a == 'S' &&
                  nt->v[start + 2].a == 'Z' && nt->v[start + 3].a == 'T',
              "the replaced word has no soft hyphen");
    }

    /* The no-break space: typed as FF, held as the character 1E. */
    ed_move(e, MV_DOC_START, 24);
    ed_move(e, MV_BLOCK_START, 24);
    type(e, "QQa");
    ed_insert_char(e, NBSP_BYTE);
    type(e, "bQQ");

    {
        Text *t = ed_text(e);
        int at = e->cur.tok - 7;

        CHECK(t->v[at + 3].kind == TK_CTRL && t->v[at + 3].a == NBSP_CTRL,
              "typed FF is the character 1E");
        CHECK(t->v[at + 2].kind == TK_CHAR && t->v[at + 4].kind == TK_CHAR,
              "the letters around it are characters");

        char nd[8] = "a";

        nd[1] = chr(NBSP_BYTE);
        nd[2] = 'b';
        nd[3] = '\0';
        ed_move(e, MV_DOC_START, 24);
        CHECK(ed_search(e, nd, 1, 0) == 1, "search for a no-break space");
        CHECK(ed_search(e, "a b", 1, 0) == 0, "a space is not a no-break one");

        char rb[8] = "[";

        rb[1] = chr(NBSP_BYTE);
        rb[2] = ']';
        rb[3] = '\0';
        CHECK(ed_replace_all(e, "QQ", rb, 0) == 2, "replace with no-break");

        Text *t2 = ed_text(e);
        int found = 0;

        for (int i = 0; i < t2->n; i++) {
            if (t2->v[i].kind == TK_CTRL && t2->v[i].a == NBSP_CTRL &&
                i > 0 && t2->v[i - 1].a == '[') {
                found++;
            }
        }

        CHECK(found == 2, "the replacement holds no-break tokens (%d)",
              found);
    }

    /* cut / paste a section */
    ed_search(e, "Water Supply", 1, 1);
    ed_sel_begin(e, SEL_SECTION);
    int before = d->nitems;
    CHECK(ed_sel_apply(e, OP_CUT) == 0, "cut section: %s", e->msg);
    CHECK(d->nitems < before && balanced(d) != 0, "cut removed items %d -> %d",
          before, d->nitems);
    ed_paste(e);
    CHECK(d->nitems == before && balanced(d) != 0, "paste restored items %d",
          d->nitems);

    /* block copy/paste */
    ed_move(e, MV_DOC_START, 24);
    ed_move(e, MV_DOWN, 24);
    ed_sel_begin(e, SEL_BLOCK);
    CHECK(ed_sel_apply(e, OP_COPY) == 0, "copy "
                                         "block");
    int b2 = d->nitems;

    ed_paste(e);
    CHECK(d->nitems == b2 + 1, "pasted block");

    /* text selection */
    ed_move(e, MV_BLOCK_START, 24);
    ed_sel_begin(e, SEL_TEXT);

    for (int i = 0; i < 5; i++) {
        ed_move(e, MV_RIGHT, 24);
    }
    CHECK(ed_sel_apply(e, OP_CUT) == 0, "cut text");
    ed_move(e, MV_BLOCK_END, 24);
    ed_paste(e);

    /* save + reload */
    char out[512];

    snprintf(out, sizeof out, "%s", work_or_tmp(argc, argv, "edtest_out.doc"));
    CHECK(doc_save(d, out, err, sizeof err) == 0, "save: %s", err);
    Doc *r = doc_load(out, err, sizeof err);
    CHECK(r != NULL, "reload: %s", err);
    if (r != NULL) {
        CHECK(r->nitems == d->nitems, "reload items %d vs %d", r->nitems,
              d->nitems);
        size_t l1;
        size_t l2;
        uint8_t *a = doc_serialize(d, &l1);
        uint8_t *b = doc_serialize(r, &l2);
        CHECK(l1 == l2 && memcmp(a, b, l1) == 0, "stable after reload");
        free(a);
        free(b);
        doc_free(r);
    }

    ed_free(e);
    doc_free(d);

    /* new doc with columns */
    d = doc_new(1);
    e = ed_new(d);
    ed_layout(e);
    type(e, "Title");
    ed_append_block(e, 1);
    type(e, "left");
    ed_split(e, 0);
    type(e, "right");
    CHECK(d->items[e->cur.item].blk->ncols == 2, "two columns");
    ed_layout(e);
    snprintf(out, sizeof out, "%s.new",
             work_or_tmp(argc, argv, "edtest_out.doc"));
    CHECK(doc_save(d, out, err, sizeof err) == 0, "save new");
    r = doc_load(out, err, sizeof err);
    CHECK(r != NULL && r->nitems == d->nitems, "new reload");
    ed_free(e);
    doc_free(d);

    if (r != NULL) {
        doc_free(r);
    }

    /* cutting the whole document's content must leave a valid, editable
       skeleton */

    snprintf(path, sizeof path, "%s/plan-title-table.doc", sample);
    d = doc_load(path, err, sizeof err);
    e = ed_new(d);
    ed_layout(e);
    ed_move(e, MV_DOC_START, 24);
    ed_sel_begin(e, SEL_SECTION);
    CHECK(ed_sel_apply(e, OP_DELETE) == 0, "delete only section: %s", e->msg);
    ed_layout(e);
    CHECK(is_edit_block(d, e->cur.item) && balanced(d) != 0,
          "valid skeleton after "
          "deleting everything");
    CHECK(sect_start(d, e->cur.item) >= 0, "cursor block belongs to a section");
    type(e, "again");
    CHECK(doc_save(d, out, err, sizeof err) == 0, "save skeleton");
    r = doc_load(out, err, sizeof err);
    CHECK(r != NULL && r->nitems == d->nitems, "reload skeleton");
    ed_free(e);
    doc_free(d);

    if (r != NULL) {
        doc_free(r);
    }

    /*
     * A word that ends exactly at the right edge, followed by a space,
     * leaves the row ending in the break.  The next row must start at the
     * next token.
     */
    d = doc_new(1);
    e = ed_new(d);
    e->text_width = 80;
    ed_layout(e);
    ed_append_block(e, 1);

    for (int i = 0; i < 79; i++) {
        ed_insert_char(e, 'a');
    }

    type(e, " b");
    ed_layout(e);

    ColLayout *cl = &e->view.bv[e->cur.item].cl[0];

    CHECK(cl->nrows == 2, "wrapped into %d rows", cl->nrows);
    CHECK(cl->rows[0].tok1 == 80, "row 0 ends at token %d", cl->rows[0].tok1);
    CHECK(cl->rows[1].tok0 == 80, "row 1 starts at token %d", cl->rows[1].tok0);
    ed_free(e);
    doc_free(d);

    /*
     * Every row of a table starts with a block property record.  The rows
     * are separate blocks of two columns, not one block of six.
     */
    snprintf(path, sizeof path, "%s/table_rows.doc",
             argc > 3 ? argv[3] : "tests/data");
    d = doc_load(path, err, sizeof err);
    CHECK(d != NULL, "load table rows: %s", err);

    if (d != NULL) {
        int rows = 0;
        int widest = 0;

        for (int i = 0; i < d->nitems; i++) {
            if (d->items[i].type != IT_BLOCK) {
                continue;
            }

            int nc = d->items[i].blk->ncols;

            if (nc == 2) {
                rows++;
            }

            if (nc > widest) {
                widest = nc;
            }
        }

        CHECK(rows == 3, "table has %d rows of two columns", rows);
        CHECK(widest == 2, "widest block has %d columns", widest);
        doc_free(d);
    }

    /*
     * There is no limit on the number of columns.  A table of 50 narrow
     * columns is wider than the screen: it is clipped, and the cursor can
     * still be moved through every column.
     */
    snprintf(path, sizeof path, "%s/many_columns.doc",
             argc > 3 ? argv[3] : "tests/data");
    d = doc_load(path, err, sizeof err);
    CHECK(d != NULL, "load many columns: %s", err);

    if (d != NULL) {
        e = ed_new(d);
        e->text_width = 80;
        ed_layout(e);

        int wide = -1;

        for (int i = 0; i < d->nitems; i++) {
            if (d->items[i].type == IT_BLOCK && d->items[i].blk->ncols == 50) {
                wide = i;
            }
        }

        CHECK(wide >= 0, "a block with 50 columns");

        if (wide >= 0) {
            CHECK(e->view.bv[wide].ncl == 50, "laid out %d columns",
                  e->view.bv[wide].ncl);
            e->cur.item = wide;
            e->cur.col = 0;
            e->cur.tok = 0;

            for (int i = 0; i < 200 && e->cur.col < 49; i++) {
                ed_move(e, MV_RIGHT, 24);
                CHECK(ed_cursor_screen(e, NULL, NULL) >= 0,
                      "cursor has a screen position in column %d", e->cur.col);
            }

            CHECK(e->cur.item == wide && e->cur.col == 49,
                  "cursor reached column %d", e->cur.col);
        }

        ed_free(e);
        doc_free(d);
    }

    /* Splitting a block keeps adding columns past the old limit of 12. */
    d = doc_new(1);
    e = ed_new(d);
    e->text_width = 80;
    ed_layout(e);
    ed_append_block(e, 1);

    for (int i = 0; i < 20; i++) {
        ed_split(e, 0);
    }

    CHECK(d->items[e->cur.item].blk->ncols == 21, "split made %d columns",
          d->items[e->cur.item].blk->ncols);
    ed_free(e);
    doc_free(d);

    /*
     * The marker table: every command of the original's menu, with its own
     * type number, and payloads that can be taken apart and put back.
     */
    CHECK(marker_menu_count() == 35, "marker menu has %d commands",
          marker_menu_count());

    for (int i = 0; i < marker_menu_count(); i++) {
        const MInfo *mi = marker_menu(i);

        CHECK(marker_info(mi->type) == mi, "type %d is found again", mi->type);

        for (int j = i + 1; j < marker_menu_count(); j++) {
            CHECK(marker_menu(j)->type != mi->type, "type %d is used twice",
                  mi->type);
        }
    }

    CHECK(marker_name(32) == NULL && marker_name(20) == NULL &&
              marker_name(21) == NULL && marker_name(10) == NULL,
          "Symbol, Markup, Escape Codes and Worksheet are removed");
    CHECK(strcmp(marker_name(18), "Line") == 0, "type 18 is Line");
    CHECK(strcmp(marker_name(15), "Font") == 0, "type 15 is Font");
    CHECK(marker_name(99) == NULL, "an unknown type has no name");

    static const struct {
        int type;
        const char *payload;
        size_t len;
        const char *label;
    } cases[] = {
        {18, "23000\r\n3500\r\n430\r\n", 18,
         "LN Length:3 in, Weight:0.5 in, Center:30"},
        {18, "", 0, "LN Length:RM"},
        {31, "13000\r\n", 7, "WS 3 in"},
        {4, "12\r\n", 4, "End Keep"},
        {23, "11\r\n", 4, "PB Odd Page"},
        {37, "1Alt-F1\0\r\n", 10, " Alt-F1 "},
        {17, "13\r\n21\r\n", 8, "C3"},
        {2, "XN3.1\0\r\nXT2\r\n", 13, "XREF 3.1"},
        {25, "1MUSBAR.GMF\0\r\n", 14, "PIC MUSBAR.GMF"},
        {8, "", 0, "DESCRIPTION"},
        {41, "S\r\nT\\hrule\x1d\x00\r\n", 14, "LaTeX instructions"},
        {99, "", 0, "M99"},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        Tok k = marker_tok(cases[i].type, cases[i].payload, cases[i].len);
        char lb[96];

        marker_render(&k, lb, sizeof lb);
        CHECK(strcmp(lb, cases[i].label) == 0,
              "marker %d label '%s', "
              "expected '%s'",
              cases[i].type, lb, cases[i].label);

        MPayload mp;
        size_t blen;

        mp_parse(&mp, marker_info(tok_mtype(&k)), tok_mdata(&k), tok_mlen(&k));

        uint8_t *back = mp_build(&mp, &blen);

        CHECK(blen == tok_mlen(&k) && memcmp(back, tok_mdata(&k), blen) == 0,
              "marker %d payload survives parse and build", cases[i].type);
        free(back);
        mp_free(&mp);
        tok_release(&k);
    }

    /* A note keeps its nested text and its field through an edit. */
    {
        static const char note[] = "S\r\nTModule name.\x1d\x00\r\n12\r\n";
        MPayload mp;
        char text[64];
        size_t blen;

        mp_parse(&mp, marker_info(1), bytes(note), sizeof note - 1);
        CHECK(mp_nested_plain(&mp, text, sizeof text) != 0 &&
                  strcmp(text, "Module name.") == 0,
              "note text is plain");
        CHECK(mp_get(&mp, "1") != NULL && strcmp(mp_get(&mp, "1"), "2") == 0,
              "note keeps its type field");
        mp_set_nested_plain(&mp, "Other.");
        mp_set(&mp, "1", "1", 0);

        uint8_t *back = mp_build(&mp, &blen);

        CHECK(blen == 18 &&
                  memcmp(back, "S\r\nTOther.\x1d\x00\r\n11\r\n", 18) == 0,
              "edited note is written back");
        free(back);
        mp_free(&mp);
    }

    /*
     * The text of a LaTeX marker: every block, a line feed for each end of
     * line, a tab, the nonbreaking space as the byte FF, no change of
     * attribute; and nothing for any other marker.
     */
    {
        static const char lx[] =
            "S\r\nTone\x1d\x10 \x03two\x1e\x1d\x00\r\nB\r\n"
            "Tthree \x12\x01\x00%\x1d\x00\r\n";
        Tok k = marker_tok(41, lx, sizeof lx - 1);
        Tok other = marker_tok(1, lx, sizeof lx - 1);
        char text[64];
        int lxn = marker_latex_text(&k, text, sizeof text);

        CHECK(lxn == 17 &&
                  memcmp(text, "one\n\ttwo\xff\nthree %", 18) == 0,
              "the text of a LaTeX marker is every block as it was typed "
              "(got %d bytes)", lxn);
        CHECK(marker_latex_text(&k, text, 8) == -1,
              "a text that does not fit is refused");
        CHECK(marker_latex_text(&other, text, sizeof text) == -1,
              "only a LaTeX marker has such a text");
        tok_release(&k);
        tok_release(&other);
    }

    /* Fields are written in the order of their identifiers. */
    {
        MPayload mp;
        size_t blen;

        mp_parse(&mp, marker_info(25), NULL, 0);
        mp_set(&mp, "6", "Name", 1);
        mp_set(&mp, "1", "F.GMF", 1);
        mp_set(&mp, "2", "3000", 0);

        uint8_t *back = mp_build(&mp, &blen);

        CHECK(blen == 24 &&
                  memcmp(back, "1F.GMF\0\r\n23000\r\n6Name\0\r\n", 24) == 0,
              "fields are in order");
        free(back);
        mp_free(&mp);
    }

    /*
     * A document written by the original with one of nearly every marker:
     * all 36 types are recognised, none shows as an unknown M<number>, and
     * the symbols are drawn as text.
     */
    snprintf(path, sizeof path, "%s/markers.doc",
             argc > 3 ? argv[3] : "tests/data");
    d = doc_load(path, err, sizeof err);
    CHECK(d != NULL, "load markers: %s", err);

    if (d != NULL) {
        int seen[64] = {0};
        int ntypes = 0;
        int nmark = 0;
        int unknown = 0;

        for (int i = 0; i < d->nitems; i++) {
            if (d->items[i].type != IT_BLOCK) {
                continue;
            }

            for (int c = 0; c < d->items[i].blk->ncols; c++) {
                const Text *t = &d->items[i].blk->cols[c].text;

                for (int k = 0; k < t->n; k++) {
                    char lb[96];

                    if (t->v[k].kind != TK_MARKER) {
                        continue;
                    }

                    nmark++;
                    marker_render(&t->v[k], lb, sizeof lb);

                    int mty = tok_mtype(&t->v[k]);

                    if (marker_info(mty) == NULL) {
                        unknown++;
                    } else if (mty < 64 && seen[mty] == 0) {
                        seen[mty] = 1;
                        ntypes++;
                    }

                    MPayload mp;
                    size_t blen;

                    mp_parse(&mp, marker_info(mty), tok_mdata(&t->v[k]),
                             tok_mlen(&t->v[k]));

                    uint8_t *back = mp_build(&mp, &blen);

                    CHECK(blen == tok_mlen(&t->v[k]) &&
                              memcmp(back, tok_mdata(&t->v[k]), blen) == 0,
                          "marker type %d survives parse and build",
                          tok_mtype(&t->v[k]));
                    free(back);
                    mp_free(&mp);

                }
            }
        }

        CHECK(nmark == 43, "markers.doc has %d markers", nmark);
        /* The Worksheet, Markup, Escape Codes and Symbol markers were
           removed: the five in the file (one of each, and two symbols) are
           kept as markers of a type that is not known. */
        CHECK(ntypes == 31, "markers.doc has %d types", ntypes);
        CHECK(unknown == 6, "markers.doc has %d unknown markers", unknown);
        doc_free(d);
    }

    /*
     * In a block of columns the Down key stays in its column and skips the
     * rows where that column has no text, so it never gets stuck.
     */
    snprintf(path, sizeof path, "%s/two_columns.doc",
             argc > 3 ? argv[3] : "tests/data");
    d = doc_load(path, err, sizeof err);
    CHECK(d != NULL, "load two columns: %s", err);

    if (d != NULL) {
        e = ed_new(d);
        e->text_width = 80;
        ed_layout(e);

        int first = -1;
        int second = -1;

        for (int i = 0; i < d->nitems; i++) {
            if (d->items[i].type == IT_BLOCK && d->items[i].blk->ncols == 2) {
                if (first < 0) {
                    first = i;
                } else if (second < 0) {
                    second = i;
                }
            }
        }

        CHECK(first >= 0 && second >= 0, "two blocks of two columns");

        if (first >= 0 && second >= 0) {
            e->cur.item = first;
            e->cur.col = 0;
            e->cur.tok = 0;
            ed_fix_cursor(e);
            ed_move(e, MV_DOWN, 24);
            CHECK(e->cur.item == second && e->cur.col == 0,
                  "Down in column 1 reaches the next block (item %d col %d)",
                  e->cur.item, e->cur.col);
            ed_move(e, MV_UP, 24);
            CHECK(e->cur.item == first && e->cur.col == 0,
                  "Up in column 1 comes back (item %d col %d)", e->cur.item,
                  e->cur.col);

            e->cur.col = 1;
            e->cur.tok = 0;
            ed_fix_cursor(e);
            /* A move to the right and back sets where the cursor is. */
            ed_move(e, MV_RIGHT, 24);
            ed_move(e, MV_LEFT, 24);
            ed_move(e, MV_DOWN, 24);
            CHECK(e->cur.item == first && e->cur.col == 1,
                  "Down in column 2 goes to its next row (item %d col %d)",
                  e->cur.item, e->cur.col);
            ed_move(e, MV_DOWN, 24);
            CHECK(e->cur.item == second && e->cur.col == 1,
                  "Down at the end of column 2 reaches the next block "
                  "(item %d col %d)",
                  e->cur.item, e->cur.col);
        }

        ed_free(e);
        doc_free(d);
    }

    /*
     * A section can start with SE<n>.  It is a real section: it is numbered,
     * the status line holds it, it opens contracted when n is 2, and the
     * outline commands work on it.
     */
    snprintf(path, sizeof path, "%s/sections_se.doc",
             argc > 3 ? argv[3] : "tests/data");
    d = doc_load(path, err, sizeof err);
    CHECK(d != NULL, "load SE sections: %s", err);

    if (d != NULL) {
        int nsect = 0;
        int body_p = -1;
        int body_q = -1;
        int body_r = -1;

        for (int i = 0; i < d->nitems; i++) {
            if (d->items[i].type == IT_SECT) {
                nsect++;
            }
        }

        for (int i = 0; i < d->nitems; i++) {
            char tx[64];

            if (d->items[i].type != IT_BLOCK) {
                continue;
            }

            text_plain(&d->items[i].blk->cols[0].text, tx, sizeof tx);

            if (strcmp(tx, "Purpose body.") == 0) {
                body_p = i;
            } else if (strcmp(tx, "Prerequisites body.") == 0) {
                body_q = i;
            } else if (strcmp(tx, "Related body.") == 0) {
                body_r = i;
            }
        }

        CHECK(nsect == 6, "SE document has %d sections", nsect);
        CHECK(body_p >= 0 && body_q >= 0 && body_r >= 0, "found the blocks");

        char num[32];

        sect_number(d, body_p, num, sizeof num);
        CHECK(strcmp(num, "1") == 0, "Purpose is section %s", num);
        sect_number(d, body_q, num, sizeof num);
        CHECK(strcmp(num, "2") == 0, "Prerequisites is section %s", num);
        sect_number(d, body_r, num, sizeof num);
        CHECK(strcmp(num, "2.1") == 0, "Related is section %s", num);

        e = ed_new(d);
        e->text_width = 80;
        ed_layout(e);
        CHECK(e->view.bv[body_p].visible != 0, "Purpose is expanded");
        CHECK(e->view.bv[body_q].visible == 0,
              "Prerequisites opens contracted (SE2)");

        /* Contract and expand the first section, as the keypad - and + do. */
        e->cur.item = body_p;
        e->cur.col = 0;
        e->cur.tok = 0;
        ed_fold(e, 0, 0);
        ed_layout(e);
        CHECK(e->view.bv[body_p].visible == 0, "Purpose can be contracted");
        ed_fold(e, 1, 0);
        ed_layout(e);
        CHECK(e->view.bv[body_p].visible != 0, "Purpose can be expanded");
        ed_free(e);
        doc_free(d);
    }

    /* The revision: a new document is 1.000, and it is a number record. */
    {
        Doc *rd = doc_new(1);
        size_t rlen;
        char rerr[64];

        CHECK(doc_revision(rd) == 1000, "a new document is revision %d",
              doc_revision(rd));
        doc_set_revision(rd, 1007);

        uint8_t *rb = doc_serialize(rd, &rlen);
        Doc *rr = doc_load_mem(rb, rlen, rerr, sizeof rerr);

        CHECK(rr != NULL && doc_revision(rr) == 1007,
              "the revision is kept: %s", rerr);
        CHECK(memmem(rb, rlen, "\r\nMR1007\r\n", 8) != NULL,
              "the revision is written without a NUL");

        if (rr != NULL) {
            doc_free(rr);
        }

        free(rb);
        doc_free(rd);
    }

    /* The text records of the header: description and author. */
    {
        Doc *hd = doc_new(1);
        size_t hlen;
        char herr[64];

        doc_header_set(hd, "MA", "An Author");
        doc_header_set(hd, "MD", "A Description");

        uint8_t *hb = doc_serialize(hd, &hlen);
        Doc *hr = doc_load_mem(hb, hlen, herr, sizeof herr);

        CHECK(hr != NULL, "header reloads: %s", herr);

        if (hr != NULL) {
            CHECK(strcmp(doc_header_get(hr, "MD"), "A Description") == 0,
                  "description is kept");
            CHECK(strcmp(doc_header_get(hr, "MA"), "An Author") == 0,
                  "author is kept");
            /* The text records end with a NUL byte, so search by length. */
            const void *pd = memmem(hb, hlen, "\r\nMD", 4);
            const void *pa = memmem(hb, hlen, "\r\nMA", 4);

            CHECK(pd != NULL && pa != NULL && pd < pa, "MD comes before MA");

            doc_header_set(hr, "MD", "");
            CHECK(doc_header_get(hr, "MD") == NULL,
                  "an empty description is removed");
            doc_free(hr);
        }

        free(hb);
        doc_free(hd);
    }

    /* ---- UTF-8 stage 0: the encoder, the table, the character sets ---- */
    {
        static const struct {
            unsigned cp;
            size_t len;
            const char *bytes;
        } enc[] = {
            {0x41, 1, "A"},
            {0xE9, 2, "\xC3\xA9"},
            {0x7FF, 2, "\xDF\xBF"},
            {0x800, 3, "\xE0\xA0\x80"},
            {0x20AC, 3, "\xE2\x82\xAC"},
            {0xFFFF, 3, "\xEF\xBF\xBF"},
            {0x10000, 4, "\xF0\x90\x80\x80"},
            {0x1F600, 4, "\xF0\x9F\x98\x80"},
            {0x10FFFF, 4, "\xF4\x8F\xBF\xBF"},
            {0x110000, 3, "\xEF\xBF\xBD"},
        };

        for (size_t i = 0; i < sizeof enc / sizeof enc[0]; i++) {
            /* A buffer of exactly 4 bytes: ASan sees any write past it. */
            char *eb = malloc(4);
            int el = utf8_encode(enc[i].cp, eb);

            CHECK(usz(el) == enc[i].len &&
                      memcmp(eb, enc[i].bytes, enc[i].len) == 0,
                  "utf8_encode of U+%04X: length %d", enc[i].cp, el);
            free(eb);
        }

        /* utf8_put (export, the X11 dump, the help dump) with a code point
           above FFFF. */
        FILE *tf = tmp_file();
        char pb[8] = "";

        CHECK(tf != NULL, "a temporary file");

        if (tf != NULL) {
            utf8_put(tf, 0x1F600);
            utf8_put(tf, 0xE9);
            rewind(tf);
            CHECK(fread(pb, 1, sizeof pb, tf) == 6 &&
                      memcmp(pb, "\xF0\x9F\x98\x80\xC3\xA9", 6) == 0,
                  "utf8_put writes the four byte form");
            fclose(tf);
        }

        /* The Spell conversion of every CP 437 byte into exact buffers. */
        for (int c = 1; c < 256; c++) {
            char in[2] = {chr(c), '\0'};
            char *sb = malloc(5);

            spell_to_utf8(in, sb, 5);
            CHECK(strlen(sb) >= 1 && strlen(sb) <= 3,
                  "spell_to_utf8 of byte %02X", uns(c));
            free(sb);
        }

        /* FF is the no-break space for a conversion; the screen draws it
           as a blank. */
        CHECK(cp437_uni[0xFF] == 0x00A0, "cp437_uni of FF is U+00A0");
        CHECK(cp437_of_unicode(0x00A0) == 0xFF, "U+00A0 is the byte FF");
        CHECK(cp437_of_unicode(0x20) == 0x20, "U+0020 is the byte 20");
        CHECK(cp437_of_unicode(0x2302) == 0x7F, "U+2302 is the byte 7F");
        CHECK(cp437_of_unicode(0x4E2D) == -1, "a Chinese character has none");
        CHECK(cp437_of_unicode(0x263A) == -1, "a control glyph is not kept");
        CHECK(cp437_of_unicode(0x1F600) == -1, "an emoji has no place");

        /* Latin-1. */
        CHECK(charset_to_437(CS_LATIN1, 0xE9) == 0x82, "Latin-1 e acute");
        CHECK(charset_to_437(CS_LATIN1, 0xA0) == 0xFF, "Latin-1 A0 is FF");
        CHECK(charset_to_437(CS_LATIN1, 0x41) == 0x41, "Latin-1 ASCII");
        CHECK(charset_to_437(CS_LATIN1, 0x80) == '?', "Latin-1 80 has none");
        CHECK(charset_to_437(CS_LATIN1, 0xC3) == '?', "Latin-1 A tilde none");
        CHECK(charset_from_437(CS_LATIN1, 0x82) == 0xE9, "to Latin-1 e acute");
        CHECK(charset_from_437(CS_LATIN1, 0xFF) == 0xA0, "to Latin-1 FF");
        CHECK(charset_from_437(CS_LATIN1, 0xE0) == '?', "alpha is ? in L1");
        CHECK(charset_from_437(CS_LATIN1, 0xC4) == '?', "box is ? in L1");
        CHECK(charset_from_437(CS_LATIN1, 0x7F) == '?', "house is ? in L1");
        CHECK(charset_from_437(CS_LATIN1, 0x07) == '*', "bullet is * in L1");
        CHECK(charset_from_437(CS_LATIN1, 0x98) == 0xFF, "y diaeresis");
        CHECK(charset_from_437(CS_LATIN1, 0xE1) == 0xDF, "sharp s");

        /* Every byte above 7F: CP 437 to Latin-1 and back is stable. */
        for (int c = 0x80; c < 256; c++) {
            uint8_t l1 = charset_from_437(CS_LATIN1, u8(c));

            if (l1 != '?') {
                CHECK(charset_to_437(CS_LATIN1, l1) == c,
                      "Latin-1 round trip of %02X", uns(c));
            }
        }

        /* The strict decoder. */
        {
            unsigned cp = 0;
            static const uint8_t ok1[] = {0xC3, 0xA9};
            static const uint8_t ok4[] = {0xF0, 0x9F, 0x98, 0x80};
            static const uint8_t bad[][4] = {
                {0x80, 0, 0, 0},       /* A lone continuation byte. */
                {0xC0, 0x80, 0, 0},    /* An overlong form. */
                {0xC1, 0xBF, 0, 0},    /* An overlong form. */
                {0xE0, 0x80, 0x80, 0}, /* An overlong form. */
                {0xED, 0xA0, 0x80, 0}, /* A surrogate. */
                {0xF4, 0x90, 0x80, 0x80}, /* Above 10FFFF. */
                {0xF8, 0x88, 0x80, 0x80}, /* A five byte lead. */
                {0xFF, 0, 0, 0},
                {0xC3, 0x41, 0, 0},    /* A bad continuation. */
            };

            CHECK(utf8_decode_strict(ok1, 2, &cp) == 2 && cp == 0xE9,
                  "decode e acute");
            CHECK(utf8_decode_strict(ok4, 4, &cp) == 4 && cp == 0x1F600,
                  "decode four bytes");
            CHECK(utf8_decode_strict(ok4, 3, &cp) == 0, "a cut sequence");
            CHECK(utf8_decode_strict(ok4, 0, &cp) == 0, "nothing left");

            for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
                CHECK(utf8_decode_strict(bad[i], 4, &cp) == 0,
                      "damaged UTF-8 case %d is refused", isz(i));
            }
        }

        /* Import and export of UTF-8 and Latin-1 in the Editor. */
        {
            static const char u8in[] =
                "\xEF\xBB\xBF"
                "Caf\xC3\xA9 \xE4\xB8\xAD \xF0\x9F\x98\x80\n"
                "x\xE9y \xFF\xC3(\n";
            Doc *ud = doc_new(0);
            Editor *ue = ed_new(ud);
            char t0[160] = "";
            int lost;

            ue->text_width = 80;
            ed_layout(ue);
            lost = ed_import_ascii(ue, bytes(u8in), strlen(u8in), 0, CS_UTF8);

            /*
             * Lost: the Chinese character, the emoji and the byte C3 with a
             * bad follower, which is taken as A tilde.  The byte FF is y
             * diaeresis and is kept.
             */
            CHECK(lost == 3, "UTF-8 import counts what was lost: %d", lost);

            for (int i = 0; i < ud->nitems; i++) {
                if (ud->items[i].type == IT_BLOCK &&
                    ud->items[i].blk->ncols > 0) {
                    text_plain(&ud->items[i].blk->cols[0].text, t0, 160);
                    break;
                }
            }

            CHECK(strncmp(t0, "Caf\x82 ? ?", 8) == 0,
                  "UTF-8 import text: '%s'", t0);
            ed_free(ue);
            doc_free(ud);

            ud = doc_new(0);
            ue = ed_new(ud);
            ue->text_width = 80;
            ed_layout(ue);
            lost = ed_import_ascii(ue, bytes("a\xE9\xA0\x80\n"), 5, 0,
                                   CS_LATIN1);
            CHECK(lost == 1, "Latin-1 import counts what was lost: %d", lost);
            ed_free(ue);
            doc_free(ud);

            ud = doc_new(0);
            ue = ed_new(ud);
            ue->text_width = 80;
            ed_layout(ue);
            lost = ed_import_ascii(ue, bytes("a\xE9\xA0\x80\n"), 5, 0,
                                   CS_437);
            CHECK(lost == 0, "the old sets count nothing: %d", lost);
            ed_free(ue);
            doc_free(ud);
        }
    }

    /* ---- the File menu: character sets, ASCII import and export ---- */
    {
        const char *dd = argc > 3 ? argv[3] : "tests/data";

        CHECK(charset_to_437(CS_437, 0x82) == 0x82, "CP 437 is as it is");
        CHECK(charset_to_437(CS_ASCII7, 0xC1) == 0x41, "7 bit drops the bit");
        CHECK(charset_to_437(CS_850, 0x41) == 0x41, "ASCII is the same");
        CHECK(charset_to_437(CS_850, 0xB5) == '?',
              "a letter that CP 437 lacks becomes ?");
        CHECK(charset_from_437(CS_ASCII7, 0x82) == 'e',
              "an accented letter loses its accent in 7 bit ASCII");
        CHECK(charset_from_437(CS_437, 0x07) == '*',
              "the bullet is written as an asterisk");

        Doc *id = doc_new(0);
        Editor *ie = ed_new(id);
        static const char para[] = "Para one a\nline two\n\nPara two\n";

        ie->text_width = 80;
        ed_layout(ie);
        ed_import_ascii(ie, bytes(para), strlen(para), 1, CS_437);

        char b0[160] = "";
        char b1[160] = "";
        int nb = 0;

        for (int i = 0; i < id->nitems; i++) {
            if (id->items[i].type == IT_BLOCK && id->items[i].blk->ncols > 0) {
                char *dst = nb == 0 ? b0 : b1;

                text_plain(&id->items[i].blk->cols[0].text, dst, 160);

                if (dst[0] != '\0') {
                    nb++;
                }
            }
        }

        CHECK(strcmp(b0, "Para one a line two") == 0,
              "import joins lines: '%s'", b0);
        CHECK(strcmp(b1, "Para two") == 0,
              "a blank line starts a new block: '%s'", b1);
        CHECK(ie->cur.tok == 0, "the cursor is at the start of the new text");

        /*
         * An import cannot make a document that is too large to load again.
         * The document is loaded with almost as many blocks as the limit
         * allows, so that a few paragraphs reach it.
         */
        {
            static const char head[] = "*R2\r\nMR1001\r\n*B1\r\n";
            static const char tail[] = "*E\r\n";
            size_t sects = DOC_MAX_ITEMS - 40u;
            size_t full_n = sizeof head - 1 + 6 * sects + sizeof tail - 1;
            uint8_t *full = malloc(full_n);
            uint8_t *at = full;
            char perr[128] = "";

            memcpy(at, head, sizeof head - 1);
            at += sizeof head - 1;

            for (size_t k = 0; k < sects; k++) {
                memcpy(at, "Tx\035\000\r\n", 6);
                at += 6;
            }

            memcpy(at, tail, sizeof tail - 1);

            Doc *md = doc_load_mem(full, full_n, perr, sizeof perr);

            CHECK(md != NULL, "a document of %zu blocks loads: %s", sects,
                  perr);

            if (md != NULL) {
                Editor *me = ed_new(md);
                uint8_t many[400];

                for (size_t k = 0; k < sizeof many; k += 4) {
                    memcpy(many + k, "a\n\n\n", 4);
                }

                me->text_width = 80;
                ed_layout(me);

                int items_before = md->nitems;
                int got = ed_import_ascii(me, many, sizeof many, 1, CS_437);

                CHECK(got == -1,
                      "an import of too many paragraphs stops and says so "
                      "(%d, items %d to %d)", got, items_before, md->nitems);
                CHECK(md->nitems <= DOC_MAX_ITEMS,
                      "the imported document has at most %d blocks: %d",
                      DOC_MAX_ITEMS, md->nitems);
                ed_free(me);
                doc_free(md);
            }

            free(full);
        }

        snprintf(path, sizeof path, "%s.exp", work_or_tmp(argc, argv, "e"));

        char fb[400];
        FILE *ff;
        size_t fn = 0;

        CHECK(doc_export_ascii(id, path, 0, CS_437, -1, err, sizeof err) == 0,
              "export data only: %s", err);
        ff = fopen(path, "rb");

        if (ff != NULL) {
            fn = fread(fb, 1, sizeof fb - 1, ff);
            fclose(ff);
        }

        fb[fn] = '\0';
        CHECK(strstr(fb, "Para one a line two\r\n") != NULL &&
                  strstr(fb, "Para two\r\n") != NULL,
              "exported text has CR LF line ends");

        Doc *kd = doc_new(0);
        Editor *ke = ed_new(kd);
        static const char kept[] = "one\ntwo\n";

        ke->text_width = 80;
        ed_layout(ke);
        ed_import_ascii(ke, bytes(kept), strlen(kept), 0, CS_437);

        int nls = 0;

        for (int i = 0; i < kd->nitems; i++) {
            if (kd->items[i].type == IT_BLOCK && kd->items[i].blk->ncols > 0) {
                const Text *kt = &kd->items[i].blk->cols[0].text;

                for (int k = 0; k < kt->n; k++) {
                    nls += kt->v[k].kind == TK_NL;
                }
            }
        }

        CHECK(nls >= 2, "preserved line ends: %d", nls);
        ed_free(ke);
        doc_free(kd);
        ed_free(ie);
        doc_free(id);

        /* ---- retrieve ---- */
        char pp[512];
        char cc[512];
        Doc *src;
        Doc *dst;

        snprintf(pp, sizeof pp, "%s/orchard.set", sample);
        snprintf(cc, sizeof cc, "%s/plan-title-table.doc", sample);
        src = doc_load(pp, err, sizeof err);

        CHECK(src != NULL, "load the template: %s", err);

        if (src != NULL) {
            dst = doc_load(cc, err, sizeof err);

            int rev = doc_revision(dst);
            int n0 = dst->nitems;

            CHECK(doc_retrieve_into(dst, src, RET_BODY, 0, err, sizeof err) ==
                          0 &&
                      dst->nitems == src->nitems && doc_revision(dst) == rev &&
                      doc_header_get(dst, "MD") == NULL,
                  "Retrieve Body Only: text from the file, settings kept");
            doc_free(dst);

            dst = doc_load(cc, err, sizeof err);
            CHECK(doc_retrieve_into(dst, src, RET_GLOBAL, 0, err, sizeof err) ==
                          0 &&
                      dst->nitems == n0 && doc_revision(dst) == rev &&
                      doc_header_get(dst, "MD") != NULL &&
                      strcmp(doc_header_get(dst, "MD"), "club template") ==
                          0,
                  "Retrieve Global Settings: settings from the file, text "
                  "and revision kept");
            doc_free(dst);

            dst = doc_load(cc, err, sizeof err);
            CHECK(doc_retrieve_into(dst, src, RET_TYPEFACE, 0, err,
                                    sizeof err) == 0 &&
                      dst->nitems == n0 && doc_header_get(dst, "MD") == NULL &&
                      props_get(&dst->header, "DF", '1') != NULL &&
                      props_get(&dst->header, "FL", 'A') != NULL,
                  "Retrieve Typeface Settings: only the DF records change");
            doc_free(dst);

            dst = doc_load(cc, err, sizeof err);
            CHECK(doc_retrieve_into(dst, src, RET_INSERT, 3, err, sizeof err) ==
                          0 &&
                      dst->nitems == n0 + src->nitems,
                  "Retrieve and Insert adds the text of the file");
            CHECK(doc_retrieve_into(dst, src, RET_NAMED, 0, err, sizeof err) !=
                      0,
                  "named block settings are refused");
            doc_free(dst);
            doc_free(src);
        }

        /* ---- save a part ---- */
        char sp[512];
        Doc *sd;

        snprintf(sp, sizeof sp, "%s/sections_se.doc", dd);
        sd = doc_load(sp, err, sizeof err);

        if (sd != NULL) {
            int q = -1;

            for (int i = 0; i < sd->nitems; i++) {
                char tx[64];

                if (sd->items[i].type == IT_BLOCK &&
                    sd->items[i].blk->ncols > 0) {
                    text_plain(&sd->items[i].blk->cols[0].text, tx, sizeof tx);

                    if (strcmp(tx, "Prerequisites body.") == 0) {
                        q = i;
                    }
                }
            }

            snprintf(path, sizeof path, "%s.part",
                     work_or_tmp(argc, argv, "e"));
            CHECK(q >= 0 && doc_save_mode(sd, path, SAVE_EXTRACT, q, err,
                                          sizeof err) == 0,
                  "extract a section: %s", err);

            Doc *pd = doc_load(path, err, sizeof err);
            int has_q = 0;
            int has_p = 0;

            for (int i = 0; pd != NULL && i < pd->nitems; i++) {
                char tx[64];

                if (pd->items[i].type == IT_BLOCK &&
                    pd->items[i].blk->ncols > 0) {
                    text_plain(&pd->items[i].blk->cols[0].text, tx, sizeof tx);
                    has_q += strcmp(tx, "Prerequisites body.") == 0;
                    has_p += strcmp(tx, "Purpose body.") == 0;
                }
            }

            CHECK(has_q == 1 && has_p == 0,
                  "the extracted file holds only that section");

            if (pd != NULL) {
                doc_free(pd);
            }

            CHECK(doc_save_mode(sd, path, SAVE_GLOBAL, q, err, sizeof err) == 0,
                  "save global settings: %s", err);
            pd = doc_load(path, err, sizeof err);
            CHECK(pd != NULL && pd->nitems == 0, "a settings file has no text");

            if (pd != NULL) {
                doc_free(pd);
            }

            doc_free(sd);
        }

        /* ---- templates ---- */
        char tp[512];

        CHECK(template_find("BLANK.SET", dd, tp, sizeof tp) != 0,
              "a template is found, whatever the case of its name");
        CHECK(template_find("NOSUCH.SET", dd, tp, sizeof tp) == 0,
              "a missing template is not found");
    }


    /* ---- Autosort ---- */
    {
        char dir[256];
        char hl[512];
        const char *names[] = {"sort_sections.doc", "sort_ties.doc"};
        const char *want[] = {
            "Parent|10 x|9 x|apple|Apple|Apple pie|banana|z|a|cherry|",
            "P|A-b|a_b|a b|ab|bob|bOb|Bob|BOB|"};
        const char *first_p[] = {"Parent", "P"};

        snprintf(dir, sizeof dir, "%s", argc > 3 ? argv[3] : "tests/data");

        for (int t = 0; t < 2; t++) {
            snprintf(path, sizeof path, "%s/%s", dir, names[t]);
            d = doc_load(path, err, sizeof err);
            CHECK(d != NULL, "load %s: %s", names[t], err);

            if (d == NULL) {
                continue;
            }

            Editor *se = ed_new(d);

            for (int i = 0; i < d->nitems; i++) {
                char tx[64];

                if (d->items[i].type == IT_BLOCK) {
                    text_plain(&d->items[i].blk->cols[0].text, tx, sizeof tx);

                    if (strncmp(tx, first_p[t], strlen(first_p[t])) == 0) {
                        se->cur.item = i;
                        break;
                    }
                }
            }

            CHECK(ed_sort_sections(se, 0) > 0, "sort %s", names[t]);
            headlines(d, hl, sizeof hl);
            CHECK(strcmp(hl, want[t]) == 0,
                  "the sorted order of %s is %s, not %s", names[t], hl,
                  want[t]);
            ed_free(se);
            doc_free(d);
        }

        snprintf(path, sizeof path, "%s/sort_table.doc", dir);
        d = doc_load(path, err, sizeof err);
        CHECK(d != NULL, "load sort_table.doc: %s", err);

        if (d != NULL) {
            Editor *se = ed_new(d);
            int first = 4; /* the first row, after the headline */

            se->cur.item = first;
            ed_sel_begin(se, SEL_BLOCK);
            se->cur.item = d->nitems - 2;
            CHECK(ed_sel_apply(se, OP_SORT) == 0, "sort the table");
            char c0[64];
            char c1[64];

            text_plain(&d->items[first].blk->cols[0].text, c0, sizeof c0);
            text_plain(&d->items[first].blk->cols[1].text, c1, sizeof c1);
            CHECK(strncmp(c0, "Apple", 5) == 0 && strncmp(c1, "9", 1) == 0,
                  "the first row after sorting is Apple, 9 (%s, %s)", c0, c1);
            text_plain(&d->items[first + 3].blk->cols[0].text, c0, sizeof c0);
            CHECK(strncmp(c0, "pear", 4) == 0, "the last row is pear (%s)",
                  c0);
            ed_free(se);
            doc_free(d);
        }
    }


    /* ---- font names ---- */
    {
        char dir[256];
        char nm[64];

        snprintf(dir, sizeof dir, "%s", argc > 3 ? argv[3] : "tests/data");
        snprintf(path, sizeof path, "%s/sort_ties.doc", dir);
        d = doc_load(path, err, sizeof err);
        CHECK(d != NULL, "load for font names: %s", err);

        if (d != NULL) {
            doc_font_name(d, 1, nm, sizeof nm);
            CHECK(strcmp(nm, "Intro head") == 0, "font 1 is %s", nm);
            doc_font_name(d, 6, nm, sizeof nm);
            CHECK(strcmp(nm, "Font 6") == 0, "font 6 has its default name");
            doc_set_font_name(d, 6, "Zed");
            doc_font_name(d, 6, nm, sizeof nm);
            CHECK(strcmp(nm, "Zed") == 0, "font 6 is renamed (%s)", nm);

            size_t len;
            uint8_t *bytes = doc_serialize(d, &len);
            const char *want = "DF5NLevel 2 head";
            const uint8_t *at5 = find_bytes(bytes, len, want);
            const uint8_t *at6 = find_bytes(bytes, len, "DF6NZed");

            CHECK(at5 != NULL && at6 != NULL && at6 > at5,
                  "DF6 is written after DF5");
            free(bytes);
            doc_set_font_name(d, 6, "Font 6");
            bytes = doc_serialize(d, &len);
            CHECK(find_bytes(bytes, len, "DF6N") == NULL,
                  "a font with its default name has no record");
            free(bytes);
            doc_set_font_name(d, 1, "Title");
            doc_font_name(d, 1, nm, sizeof nm);
            CHECK(strcmp(nm, "Title") == 0, "an existing record is replaced");
            doc_free(d);
        }
    }


    /* Equations: the text of the marker, displayed or inline, and the safety
       rules (docs/EQUATIONS.txt). */
    {
        char eqpath[512];
        char eqerr[96];

        snprintf(eqpath, sizeof eqpath, "%s/equation.doc", argv[3]);

        Doc *eqd = doc_load(eqpath, eqerr, sizeof eqerr);

        CHECK(eqd != NULL, "equation.doc loads");

        if (eqd != NULL) {
            int bi = 0;

            while (bi < eqd->nitems && eqd->items[bi].type != IT_BLOCK) {
                bi++;
            }

            const Text *t = &eqd->items[bi].blk->cols[0].text;
            int at = -1;
            char tex[256];

            for (int i = 0; i < t->n; i++) {
                if (t->v[i].kind == TK_MARKER && tok_mtype(&t->v[i]) == 9) {
                    at = i;
                }
            }

            CHECK(at >= 0, "equation.doc has an Equation marker");

            if (at >= 0) {
                CHECK(marker_equation_text(&t->v[at], tex, sizeof tex) > 0 &&
                          strcmp(tex, "y = 10 + e^x") == 0,
                      "the text of the equation is y = 10 + e^x");
                CHECK(eqn_is_display(t, at) != 0,
                      "an equation that is alone in its block is displayed");
            }

            doc_free(eqd);
        }

        Text line;

        text_init(&line);
        text_insert(&line, 0, tok_marker(9, NULL, 0));
        CHECK(eqn_is_display(&line, 0) != 0, "a lone marker is displayed");
        text_insert(&line, 0, tok_char('a'));
        CHECK(eqn_is_display(&line, 1) == 0,
              "text before the equation makes it inline");
        text_free(&line);
        text_init(&line);
        text_insert(&line, 0, tok_marker(9, NULL, 0));
        text_insert(&line, 1, tok_char(' '));
        text_insert(&line, 2, tok_marker(9, NULL, 0));
        CHECK(eqn_is_display(&line, 0) == 0,
              "two equations on a line are both inline");
        text_free(&line);
        text_init(&line);
        text_insert(&line, 0, tok_marker(9, NULL, 0));

        Tok nl;

        memset(&nl, 0, sizeof nl);
        nl.kind = TK_NL;
        text_insert(&line, 1, nl);
        text_insert(&line, 2, tok_char('x'));
        CHECK(eqn_is_display(&line, 0) != 0,
              "text on the next line does not make it inline");
        text_free(&line);

        CHECK(eqr_unsafe("\\input{/etc/passwd}") != NULL,
              "\\input is refused");
        CHECK(eqr_unsafe("x \\write18{ls}") != NULL, "\\write is refused");
        CHECK(eqr_unsafe("\\usepackage{x}") != NULL,
              "\\usepackage is refused");
        CHECK(eqr_unsafe("\\frac{a}{b} + \\inputx") == NULL,
              "a command that only begins like a refused one is allowed");
        CHECK(eqr_unsafe("e^{i\\pi} + 1 = 0") == NULL,
              "ordinary math is allowed");

        /* The renderer runs latex and dvipng, when they are installed. */
        char cdir[PATH_MAX];

        tmp_path(cdir, sizeof cdir, "strata-eqtest-XXXXXX");

        if (mkdtemp(cdir) != NULL) {
            setenv("XDG_CACHE_HOME", cdir, 1);
            eqr_configure(100, 0xFFFFFF, 0x000000);

            if (eqr_enabled() == 0) {
                printf("skip: latex or dvipng is not installed\n");
            } else {
                const EqImage *im = eqr_request("x^2", 1);

                for (int i = 0; i < 150 && im == NULL; i++) {
                    usleep(200000);
                    eqr_poll();
                    im = eqr_request("x^2", 1);
                }

                CHECK(im != NULL && im->w > 0 && im->h > 0 && im->rgb != NULL,
                      "x^2 is typeset to an image");
                eqr_request("\\frac{1", 1);

                for (int i = 0; i < 150 && eqr_error("\\frac{1", 1) == NULL;
                     i++) {
                    usleep(200000);
                    eqr_poll();
                }

                CHECK(eqr_error("\\frac{1", 1) != NULL,
                      "a TeX error is reported");
            }

            /* A picture is made by pdflatex and pdftoppm. */
            if (eqr_pictures_enabled() == 0) {
                printf("skip: pdflatex or pdftoppm is not installed\n");
            } else {
                char spec[600];
                char bad[600];

                snprintf(spec, sizeof spec, "%s/picture.png|1500|0|0",
                         argc > 3 ? argv[3] : "tests/data");
                snprintf(bad, sizeof bad, "%s/picture.png.exe|0|0|0",
                         argc > 3 ? argv[3] : "tests/data");

                const EqImage *pi = eqr_request(spec, 2);

                for (int i = 0; i < 150 && pi == NULL &&
                                eqr_error(spec, 2) == NULL;
                     i++) {
                    usleep(200000);
                    eqr_poll();
                    pi = eqr_request(spec, 2);
                }

                CHECK(pi != NULL && pi->w > 100 && pi->h > 20 &&
                          pi->rgb != NULL,
                      "a picture file is typeset to an image (%s)",
                      eqr_error(spec, 2) != NULL ? eqr_error(spec, 2) : "ok");

                /* The same width in centimetres is narrower than in inches. */
                char spec_cm[600];

                snprintf(spec_cm, sizeof spec_cm,
                         "%s/picture.png|1500|0|0|cm",
                         argc > 3 ? argv[3] : "tests/data");

                const EqImage *pc = eqr_request(spec_cm, 2);

                for (int i = 0; i < 150 && pc == NULL &&
                                eqr_error(spec_cm, 2) == NULL;
                     i++) {
                    usleep(200000);
                    eqr_poll();
                    pc = eqr_request(spec_cm, 2);
                }

                CHECK(pc != NULL && pi != NULL && pc->w * 2 < pi->w,
                      "a picture 1.5 cm wide is narrower than 1.5 in (%d, %d)",
                      pc != NULL ? pc->w : 0, pi != NULL ? pi->w : 0);
                eqr_request(bad, 2);

                for (int i = 0; i < 150 && eqr_error(bad, 2) == NULL; i++) {
                    usleep(200000);
                    eqr_poll();
                }

                CHECK(eqr_error(bad, 2) != NULL,
                      "a file that LaTeX cannot read is an error");

                /* A keycap is a box around a legend, in a line of text. */
                const char *kspec = "PrtScr|0|1000|1000|2";
                const EqImage *ki = eqr_request(kspec, 3);

                for (int i = 0; i < 150 && ki == NULL &&
                                eqr_error(kspec, 3) == NULL;
                     i++) {
                    usleep(200000);
                    eqr_poll();
                    ki = eqr_request(kspec, 3);
                }

                CHECK(ki != NULL && ki->w > 2 * ki->h && ki->depth > 0 &&
                          ki->depth < ki->h,
                      "a keycap is a wide box with room below the baseline "
                      "(%s)",
                      eqr_error(kspec, 3) != NULL ? eqr_error(kspec, 3) :
                                                    "ok");
            }
        }
    }


    /*
     * A picture with a block of its own (a Picture marker, type 25): Up and
     * Down go through its rows one at a time, so that the window scrolls a
     * line at a time; the picture is three rows here (fake_size).  The
     * equation of a displayed block, tested below, is passed over as one.
     */
    {
        static const uint8_t doc_text[] =
            "*R2\r\nMR1001\r\nPP\\1\\\0\r\nP1C33\r\nFHA1\r\nFTT5\r\n"
            "DP0,8500,11000\r\n*B0\r\nS\r\n"
            "TBefore.\x1d\0\r\n"
            "B\r\nT\x13" "25(1pics/p.png\0\r\n22000\r\n\x13)\x1d\0\r\n"
            "B\r\nTAfter.\x1d\0\r\n"
            "*E\r\n";
        char perr[96];
        Doc *pd = doc_load_mem(doc_text, sizeof doc_text - 1, perr,
                               sizeof perr);

        CHECK(pd != NULL, "the document with a picture loads: %s", perr);

        if (pd != NULL) {
            layout_eq_size = fake_size;

            Editor *pe = ed_new(pd);
            int blk[8];
            int nblk = 0;
            int first = -1;

            pe->text_width = 80;

            for (int i = 0; i < pd->nitems && nblk < 8; i++) {
                if (pd->items[i].type == IT_BLOCK) {
                    blk[nblk++] = i;
                }
            }

            ed_layout(pe);
            pe->cur.item = blk[1];
            pe->cur.col = 0;
            pe->cur.tok = 0;
            ed_fix_cursor(pe);

            int prows = ed_picture_rows(pe, &first);

            CHECK(nblk == 3 && prows == 3 && first == 0,
                  "the picture has 3 rows (%d blocks, %d rows)", nblk,
                  prows);

            int line0 = ed_cursor_screen(pe, NULL, NULL);

            /*
             * A picture is one line and one stop of the cursor, whatever
             * the number of its rows: one Down leaves it, and the end of
             * line mark under the image is not a stop.
             */
            CHECK(pe->cur.item == blk[1] && pe->cur.tok == 0 &&
                      ed_picture_row(pe) == 0 &&
                      ed_cursor_screen(pe, NULL, NULL) == line0,
                  "the cursor is on the first row of the picture");
            ed_move(pe, MV_DOWN, 24);
            CHECK(pe->cur.item == blk[2],
                  "one Down leaves the picture for the block below");
            ed_move(pe, MV_UP, 24);
            CHECK(pe->cur.item == blk[1] && pe->cur.tok == 0 &&
                      ed_picture_row(pe) == 0,
                  "one Up comes back onto the picture, not to the end of "
                  "its line");
            ed_move(pe, MV_UP, 24);
            CHECK(pe->cur.item == blk[0],
                  "one more Up leaves the picture for the block above");
            ed_move(pe, MV_DOWN, 24);
            CHECK(pe->cur.item == blk[1] && ed_picture_row(pe) == 0,
                  "Down from the block above comes to the picture");
            /* Right still reaches the end of line mark under the image. */
            ed_move(pe, MV_RIGHT, 24);
            CHECK(pe->cur.item == blk[1] && pe->cur.tok == 1,
                  "Right goes to the end of line mark of the picture");
            ed_move(pe, MV_UP, 24);
            CHECK(pe->cur.item == blk[1] && pe->cur.tok == 0,
                  "Up from that mark goes to the picture");
            layout_eq_size = NULL;
            ed_free(pe);
            doc_free(pd);
        }
    }

    /* Moving through equations with pictures: the rows that only hold a
       picture are passed over as one. */
    {
        char ipath[512];
        char ierr[96];

        snprintf(ipath, sizeof ipath, "%s/equation_inline.doc", argv[3]);

        Doc *id = doc_load(ipath, ierr, sizeof ierr);

        CHECK(id != NULL, "equation_inline.doc loads");

        if (id != NULL) {
            layout_eq_size = fake_size;

            Editor *ie = ed_new(id);
            int blk[8];
            int nblk = 0;

            ie->text_width = 80;

            for (int i = 0; i < id->nitems && nblk < 8; i++) {
                if (id->items[i].type == IT_BLOCK) {
                    blk[nblk++] = i;
                }
            }

            ed_layout(ie);

            /* The second block is the displayed equation: 3 rows of picture
               and the row of the end of line. */
            ColLayout *dl = &ie->view.bv[blk[1]].cl[0];

            CHECK(dl->nrows == 4 && dl->rows[0].pic_tok >= 0 &&
                      dl->rows[2].pic_tok >= 0 && dl->rows[3].pic_tok < 0,
                  "a displayed equation has rows of picture and a text row");

            ie->cur.item = blk[1];
            ie->cur.col = 0;
            ie->cur.tok = 0;
            ed_fix_cursor(ie);
            ed_move(ie, MV_DOWN, 24);
            CHECK(ie->cur.item == blk[2],
                  "Down from a displayed equation goes to the next block: "
                  "the mark under the image is not a stop");
            ed_move(ie, MV_UP, 24);
            CHECK(ie->cur.item == blk[1] && ie->cur.tok == 0,
                  "Up comes back onto the equation");
            ed_move(ie, MV_UP, 24);
            CHECK(ie->cur.item == blk[0],
                  "and one more Up leaves the equation");

            /* The first block has inline pictures, each with a row above
               and below the text. */
            ColLayout *il = &ie->view.bv[blk[0]].cl[0];
            int text_rows = 0;
            int picture_rows = 0;

            for (int ri = 0; ri < il->nrows; ri++) {
                if (il->rows[ri].pic_tok >= 0) {
                    picture_rows++;
                } else {
                    text_rows++;
                }
            }

            CHECK(picture_rows >= 4 && text_rows >= 2,
                  "inline pictures make rows of free cells next to the text");
            ie->cur.item = blk[0];
            ie->cur.col = 0;
            ie->cur.tok = 0;
            ed_fix_cursor(ie);

            int steps = 0;
            int last_item = ie->cur.item;

            while (ie->cur.item == blk[0] && steps < 20) {
                ed_move(ie, MV_DOWN, 24);
                steps++;
            }

            CHECK(steps <= text_rows + 1 && last_item == blk[0],
                  "Down goes from text row to text row in a line with "
                  "inline pictures");
            layout_eq_size = NULL;
            ed_free(ie);
            doc_free(id);
        }
    }

    /* Down on the last line goes to the end of that line. */
    {
        char ppath[512];
        char perr[96];

        snprintf(ppath, sizeof ppath, "%s/plain_text.doc", argv[3]);

        Doc *pd = doc_load(ppath, perr, sizeof perr);

        CHECK(pd != NULL, "plain_text.doc loads");

        if (pd != NULL) {
            Editor *pe = ed_new(pd);
            int last = -1;

            pe->text_width = 80;

            for (int i = 0; i < pd->nitems; i++) {
                if (pd->items[i].type == IT_BLOCK) {
                    last = i;
                }
            }

            ed_layout(pe);
            pe->cur.item = last;
            pe->cur.col = 0;
            pe->cur.tok = 0;
            ed_fix_cursor(pe);

            const Text *pt = &pd->items[last].blk->cols[0].text;

            ed_move(pe, MV_DOWN, 24);
            CHECK(pe->cur.item == last && pe->cur.tok == pt->n - 1 &&
                      pt->v[pt->n - 1].kind == TK_NL,
                  "Down on the last line goes to the end of the line");
            ed_move(pe, MV_DOWN, 24);
            CHECK(pe->cur.item == last && pe->cur.tok == pt->n - 1,
                  "and stays there when Down is pressed again");
            ed_free(pe);
            doc_free(pd);
        }
    }

    /* Structural edits lay out only the blocks they changed. */
    {
        const char *names[] = {"sort_sections.doc", "plain_text.doc",
                               "two_columns.doc"};

        for (int f = 0; f < 3; f++) {
            char tp[512];
            char terr[96];

            snprintf(tp, sizeof tp, "%s/%s", argv[3], names[f]);

            Doc *td = doc_load(tp, terr, sizeof terr);

            CHECK(td != NULL, "%s loads", names[f]);

            if (td == NULL) {
                continue;
            }

            Editor *te = ed_new(td);
            int first = -1;
            int last = -1;

            te->text_width = 80;
            ed_layout(te);

            for (int i = 0; i < td->nitems; i++) {
                if (is_edit_block(td, i) != 0 && td->items[i - 1].type != IT_SECT) {
                    last = i;

                    if (first < 0) {
                        first = i;
                    }
                }
            }

            if (first < 0) {
                first = last = 1;
            }

            /* Typing, Enter and Backspace in a block. */
            te->cur.item = first;
            te->cur.col = 0;
            te->cur.tok = 0;
            ed_fix_cursor(te);
            type(te, "xy");
            CHECK(partial_same(te, "typing") != 0, "typing is partial");
            ed_insert_nl(te);
            CHECK(partial_same(te, "Enter") != 0, "Enter is partial");
            ed_backspace(te);
            CHECK(partial_same(te, "Backspace") != 0,
                  "Backspace is partial");

            /* Split a block, then join it again (single-column blocks). */
            if (td->items[te->cur.item].blk->ncols == 1) {
                ed_split(te, 1);
                CHECK(partial_same(te, "a block split") != 0,
                      "a split is partial in %s", names[f]);
                te->cur.item--;
                te->cur.tok = 0;
                ed_fix_cursor(te);
                ed_join(te, 1);
                CHECK(partial_same(te, "a join") != 0,
                      "a join is partial in %s", names[f]);
            }

            /* Copy the first block and paste it after the last. */
            te->cur.item = first;
            te->cur.col = 0;
            te->cur.tok = 0;
            ed_fix_cursor(te);
            ed_sel_begin(te, SEL_BLOCK);
            ed_sel_apply(te, OP_COPY);
            te->cur.item = last;
            ed_fix_cursor(te);
            ed_paste(te);
            CHECK(partial_same(te, "a paste") != 0, "a paste is partial");
            ed_paste(te);
            CHECK(partial_same(te, "a second paste") != 0,
                  "a second paste is partial");

            /* Delete and cut the pasted blocks. */
            ed_sel_begin(te, SEL_BLOCK);
            ed_sel_apply(te, OP_DELETE);
            CHECK(partial_same(te, "a block delete") != 0,
                  "a block delete is partial");
            ed_sel_begin(te, SEL_BLOCK);
            ed_sel_apply(te, OP_CUT);
            CHECK(partial_same(te, "a block cut") != 0,
                  "a block cut is partial");

            /* A new block, a new column and a column split. */
            ed_append_block(te, 0);
            CHECK(partial_same(te, "a new block") != 0,
                  "a new block is partial");
            ed_append_column(te);
            CHECK(partial_same(te, "a new column") != 0,
                  "a new column is partial");

            /* The headline of a section: delete it, so that the next
               block becomes the headline. */
            for (int i = 1; i < td->nitems; i++) {
                if (td->items[i - 1].type == IT_SECT &&
                    is_edit_block(td, i) != 0 && td->items[i + 1].type == IT_BLOCK) {
                    te->cur.item = i;
                    te->cur.col = 0;
                    te->cur.tok = 0;
                    ed_fix_cursor(te);
                    ed_sel_begin(te, SEL_BLOCK);
                    ed_sel_apply(te, OP_DELETE);
                    partial_same(te, "the delete of a headline");
                    break;
                }
            }

            ed_free(te);
            doc_free(td);
        }
    }

    /* A document with counters is laid out in full after a paste. */
    {
        const char *names[] = {"counters.doc", "xref.doc"};

        for (int f = 0; f < 2; f++) {
            char cp[512];
            char cerr[96];

            snprintf(cp, sizeof cp, "%s/%s", argv[3], names[f]);

            Doc *cdoc = doc_load(cp, cerr, sizeof cerr);

            CHECK(cdoc != NULL, "%s loads", names[f]);

            if (cdoc == NULL) {
                continue;
            }

            Editor *ce = ed_new(cdoc);

            ce->text_width = 80;
            ed_layout(ce);

            for (int i = 1; i < cdoc->nitems; i++) {
                if (is_edit_block(cdoc, i) != 0) {
                    ce->cur.item = i;
                    ce->cur.col = 0;
                    ce->cur.tok = 0;
                    ed_fix_cursor(ce);
                    ed_sel_begin(ce, SEL_BLOCK);
                    ed_sel_apply(ce, OP_COPY);
                    ed_paste(ce);
                    CHECK(partial_same(ce, "a paste in a document with "
                                           "counters") == 0,
                          "a paste in %s is laid out in full", names[f]);
                    break;
                }
            }

            ed_free(ce);
            doc_free(cdoc);
        }
    }

    /* Counters, cross-references and comments (docs/WORKLIST.txt). */
    {
        char txt[96];

        marker_number_text(5, 27, txt, sizeof txt);
        CHECK(strcmp(txt, "aa") == 0, "27 in lower case letters is aa");
        marker_number_text(4, 26, txt, sizeof txt);
        CHECK(strcmp(txt, "Z") == 0, "26 in upper case letters is Z");
        marker_number_text(2, 1994, txt, sizeof txt);
        CHECK(strcmp(txt, "mcmxciv") == 0, "1994 in lower case roman");
        marker_number_text(3, 4, txt, sizeof txt);
        CHECK(strcmp(txt, "IV") == 0, "4 in upper case roman is IV");
        marker_number_text(2, 0, txt, sizeof txt);
        CHECK(strcmp(txt, "0") == 0, "a number a style cannot show is arabic");
        {
            /* The values around the limits of the counters. */
            static const struct {
                uint32_t v;
                const char *s[5];
            } big[] = {
                {0, {"0", "0", "0", "0", "0"}},
                {1, {"1", "i", "I", "A", "a"}},
                {3999, {"3999", "mmmcmxcix", "MMMCMXCIX", "EWU", "ewu"}},
                {4000, {"4000", "4000", "4000", "EWV", "ewv"}},
                {65535, {"65535", "65535", "65535", "CRXO", "crxo"}},
                {65536, {"65536", "65536", "65536", "CRXP", "crxp"}},
                {2147483647, {"2147483647", "2147483647", "2147483647",
                              "FXSHRXW", "fxshrxw"}},
                {2147483648u, {"2147483648", "2147483648", "2147483648",
                               "FXSHRXX", "fxshrxx"}},
                {4294967294u, {"4294967294", "4294967294", "4294967294",
                               "MWLQKWT", "mwlqkwt"}},
                {4294967295u, {"4294967295", "4294967295", "4294967295",
                               "MWLQKWU", "mwlqkwu"}},
            };

            for (size_t bi = 0; bi < sizeof big / sizeof big[0]; bi++) {
                for (int st = 1; st <= 5; st++) {
                    marker_number_text(st, big[bi].v, txt, sizeof txt);
                    CHECK(strcmp(txt, big[bi].s[st - 1]) == 0,
                          "number %u in style %d is %s, not %s",
                          (unsigned) big[bi].v, st, big[bi].s[st - 1], txt);
                }
            }
        }
        marker_counter_text("Figure \\1\\:", 0, 3, txt, sizeof txt);
        CHECK(strcmp(txt, "Figure 3:") == 0, "a tag keeps the text around");
        marker_counter_text("\\1\\.", 3, 7, txt, sizeof txt);
        CHECK(strcmp(txt, "VII.") == 0, "a style of the marker replaces that "
                                        "of the tag");

        Doc *cd = doc_new(0);
        Block *cb = block_new(1);
        static const char *const pay[] = {
            "", "", "210\r\n", "", "12\r\n", "12\r\n33\r\n", "12\r\n",
            "12\r\n225\r\n"};
        static const size_t paylen[] = {0, 0, 5, 0, 4, 9, 4, 8};
        static const char *const want[] = {"1.",  "2.",  "10.", "11.",
                                           "A.",  "II.", "III.", "XXV."};
        Tok toks[8];

        doc_delete_items(cd, 0, cd->nitems);
        doc_insert_item(cd, 0, IT_SECT, NULL);
        text_free(&cb->cols[0].text);
        text_init(&cb->cols[0].text);

        for (int i = 0; i < 8; i++) {
            toks[i] = marker_tok(17, pay[i], paylen[i]);
            text_insert(&cb->cols[0].text, cb->cols[0].text.n, toks[i]);
        }

        doc_insert_item(cd, 1, IT_BLOCK, cb);
        marker_prepare(cd);

        for (int i = 0; i < 8; i++) {
            const Tok *k = &cb->cols[0].text.v[i];

            CHECK(marker_eval(k, txt, sizeof txt) != 0 &&
                      strcmp(txt, want[i]) == 0,
                  "counter use %d numbers as the rules say (got %s)", i + 1,
                  txt);
        }

        {
            Tok c12 = marker_tok(17, "112\r\n", 5);
            char lb2[96];

            marker_set_doc(cd);
            marker_set_mode(cd, 17, MM_SHORT);
            marker_prepare(cd);
            marker_print_mode = 0;
            CHECK(marker_render(&c12, lb2, sizeof lb2) == MR_BRACKET &&
                      strcmp(lb2, "C12") == 0,
                  "a counter above nine has its number in the label");
            doc_opt_set(cd, "OT12N", "list of books", 1);
            CHECK(marker_render(&c12, lb2, sizeof lb2) == MR_BRACKET &&
                      strcmp(lb2, "C12, list of books") == 0,
                  "the description of a counter is in its label");
            marker_set_mode(cd, 17, MM_LONG);
            tok_release(&c12);
        }

        /* The long label of a Whitespace marker shows its treatment. */
        {
            static const struct {
                const char *payload;
                const char *label;
            } wl[] = {
                {"13000\r\n", "WHITESPACE 3 in, Place All on Next Page"},
                {"13000\r\n20\r\n", "WHITESPACE 3 in, Crop at End of Page"},
                {"11500\r\n22\r\nU1\r\n",
                 "WHITESPACE 1.5 cm, Float to Next Page"},
            };

            marker_set_doc(cd);
            marker_set_mode(cd, 31, MM_LONG);
            marker_prepare(cd);
            marker_print_mode = 0;

            for (size_t i = 0; i < sizeof wl / sizeof wl[0]; i++) {
                Tok w = marker_tok(31, wl[i].payload,
                                   strlen(wl[i].payload));
                char lb3[96];

                marker_render(&w, lb3, sizeof lb3);
                CHECK(strcmp(lb3, wl[i].label) == 0,
                      "Whitespace long label is '%s', expected '%s'", lb3,
                      wl[i].label);
                tok_release(&w);
            }

            marker_set_mode(cd, 31, MM_SHORT);
        }

        doc_free(cd);

        char sp[512];
        Doc *sd;

        snprintf(sp, sizeof sp, "%s/sort_sections.doc", argv[3]);
        sd = doc_load(sp, err, sizeof err);

        if (sd != NULL) {
            /* The references are in the body of the first section. */
            Block *xb_blk = NULL;
            int xitem = -1;
            Tok xtoks[6];
            int xat[6];

            for (int i = 0; i < sd->nitems; i++) {
                char tx[64];

                if (sd->items[i].type == IT_BLOCK) {
                    text_plain(&sd->items[i].blk->cols[0].text, tx,
                               sizeof tx);

                    if (strcmp(tx, "Parent body") == 0) {
                        xb_blk = sd->items[i].blk;
                        xitem = i;
                    }
                }
            }

            CHECK(xb_blk != NULL, "the body of the first section is there");

            xtoks[0] = marker_tok(2, "XN1.1\0\r\n", 9);
            xtoks[1] = marker_tok(2, "XN1.1\0\r\nXT2\r\n", 14);
            xtoks[2] = marker_tok(2, "XN1.1\0\r\nXT3\r\n", 14);
            xtoks[3] = marker_tok(2, "XN1.1\0\r\nXT4\r\n", 14);
            xtoks[4] = marker_tok(2, "XN9.9\0\r\n", 9);
            xtoks[5] = marker_tok(2, "XN1\0\r\nXT2\r\n", 12);

            for (int i = 0; i < 6; i++) {
                xat[i] = xb_blk->cols[0].text.n;
                text_insert(&xb_blk->cols[0].text, xb_blk->cols[0].text.n,
                            xtoks[i]);
            }

            marker_prepare(sd);

            Tok *x0 = &xb_blk->cols[0].text.v[xat[0]];
            Tok *x2 = &xb_blk->cols[0].text.v[xat[1]];
            Tok *x3 = &xb_blk->cols[0].text.v[xat[2]];
            Tok *x4 = &xb_blk->cols[0].text.v[xat[3]];
            Tok *xb = &xb_blk->cols[0].text.v[xat[4]];
            char tagonly[96];

            CHECK(marker_eval(x0, tagonly, sizeof tagonly) != 0 &&
                      tagonly[0] != '\0' && strstr(tagonly, "banana") == NULL,
                  "a cross-reference is the tag of the section");
            CHECK(marker_eval(x2, txt, sizeof txt) != 0 &&
                      strcmp(txt, "banana") == 0,
                  "XT2 is the headline of the section");
            CHECK(marker_eval(x3, txt, sizeof txt) != 0 &&
                      strncmp(txt, tagonly, strlen(tagonly)) == 0 &&
                      strstr(txt, " banana") != NULL,
                  "XT3 is the tag and the headline");
            CHECK(marker_eval(x4, txt, sizeof txt) != 0 &&
                      strstr(txt, "page") != NULL,
                  "XT4 is the page number, which is only known at print");
            CHECK(marker_eval(xb, txt, sizeof txt) != 0 &&
                      strcmp(txt, "[no section]") == 0,
                  "a reference to a section that is not there says so");

            /* Sort the subsections of the first section: banana gets a
               new number, and the references follow it. */
            Editor *se = ed_new(sd);

            sd->modified = 0;
            se->cur.item = xitem - 1;
            CHECK(ed_sort_sections(se, 0) > 0, "sort for the references");
            xb_blk = NULL;

            for (int i = 0; i < sd->nitems; i++) {
                char tx[64];

                if (sd->items[i].type == IT_BLOCK) {
                    text_plain(&sd->items[i].blk->cols[0].text, tx,
                               sizeof tx);

                    if (strcmp(tx, "Parent body") == 0) {
                        xb_blk = sd->items[i].blk;
                    }
                }
            }

            marker_prepare(sd);
            CHECK(xb_blk != NULL, "the body is there after the sort");

            if (xb_blk != NULL) {
                x2 = &xb_blk->cols[0].text.v[xat[1]];
            }

            char nb[32];
            const char *xn;
            MPayload pp;
            int bi = -1;

            for (int i = 0; i < sd->nitems; i++) {
                char tx[64];

                if (sd->items[i].type == IT_BLOCK) {
                    text_plain(&sd->items[i].blk->cols[0].text, tx,
                               sizeof tx);

                    if (strcmp(tx, "banana") == 0) {
                        bi = i;
                    }
                }
            }

            sect_number(sd, bi - 1, nb, sizeof nb);
            mp_parse(&pp, marker_info(2), tok_mdata(x2), tok_mlen(x2));
            xn = mp_get(&pp, "XN");
            CHECK(strcmp(nb, "1.1") != 0,
                  "sorting gave banana another number (%s)", nb);
            CHECK(xn != NULL && strcmp(xn, nb) == 0,
                  "the reference was changed to the new number %s, not %s",
                  nb, xn != NULL ? xn : "(none)");
            CHECK(marker_eval(x2, txt, sizeof txt) != 0 &&
                      strcmp(txt, "banana") == 0,
                  "the reference still shows banana after the sort");
            CHECK(sd->modified != 0,
                  "a number that changed makes the document modified");
            mp_free(&pp);
            ed_free(se);
            doc_free(sd);
        }

        /* A comment is printed in a draft print and not in a final one. */
        static const char cpay[] = "S\r\nTremember\x1d\x00\r\n1Ann\0\r\n"
                                   "22026-11-11 10:00\0\r\n";
        Tok ck = marker_tok(6, cpay, sizeof cpay - 1);
        char lb[200];

        marker_print_mode = 1;
        CHECK(marker_render(&ck, lb, sizeof lb) == MR_TEXT &&
                  strstr(lb, "Comment by Ann, 2026-11-11 10:00: remember") !=
                      NULL,
              "a draft print has the comment with its name and time");
        marker_print_mode = 2;
        CHECK(marker_render(&ck, lb, sizeof lb) == MR_TEXT && lb[0] == '\0',
              "a final print has nothing of the comment");
        marker_print_mode = 0;
        CHECK(marker_render(&ck, lb, sizeof lb) == MR_BRACKET &&
                  strstr(lb, "COM \"remember") != NULL,
              "on the screen a comment is its short form, not a glyph");
        tok_release(&ck);
    }

    /* A Keep marker is never printed, and a block that holds Keep markers
       only prints nothing, not even a blank line; the screen shows the
       label. */
    {
        char kp[512];
        char kerr[200];
        int kn;
        int shown = 0;
        int blanks = 0;
        int labels = 0;

        snprintf(kp, sizeof kp, "%s/keep_fixed.doc",
                 argc > 3 ? argv[3] : "tests/data");

        Doc *kd = doc_load(kp, kerr, sizeof kerr);

        CHECK(kd != NULL, "keep_fixed.doc loads");

        if (kd != NULL) {
            char **kl;

            marker_print_mode = 0;
            kl = render_lines(kd, 60, &kn);

            for (int i = 0; i < kn; i++) {
                shown += strstr(kl[i], "Start Keep (Fixed)") != NULL;
            }

            free_lines(kl, kn);
            CHECK(shown == 1, "the screen shows the Keep label (%d)", shown);

            for (int mode = 1; mode <= 2; mode++) {
                marker_print_mode = mode;
                kl = render_lines(kd, 60, &kn);
                labels = 0;
                blanks = 0;

                for (int i = 0; i < kn; i++) {
                    labels += strstr(kl[i], "Keep") != NULL;
                    blanks += kl[i][0] == '\0';
                }

                CHECK(labels == 0, "print mode %d prints no Keep label", mode);
                CHECK(blanks == 4, "print mode %d: Keep blocks leave no "
                      "blank lines (%d)", mode, blanks);
                free_lines(kl, kn);
            }

            marker_print_mode = 0;
            doc_free(kd);
        }
    }

    /* The list of fonts for printing: defaults, the file of the user, and the
       checks on what may be written in it. */
    {
        FontEntry ft[FONT_COUNT];
        FontEntry back[FONT_COUNT];
        char fdir[PATH_MAX];
        char ferr[200];
        char line[96];
        struct stat fst;

        fonts_defaults(ft);
        CHECK(strcmp(ft[1].name, "Latin Modern Roman") == 0 &&
                  strcmp(ft[1].package, "lmodern") == 0 &&
                  strcmp(ft[3].name, "Times") == 0 &&
                  strcmp(ft[4].options, "scaled=0.92") == 0 &&
                  ft[4].family == FF_SANS && ft[5].family == FF_MONO &&
                  strcmp(ft[18].ctan, "lato") == 0 &&
                  strcmp(ft[18].map, "lato.map") == 0,
              "the list of fonts starts with the standard LaTeX fonts");
        fonts_package_line(&ft[4], line, sizeof line);
        CHECK(strcmp(line, "\\usepackage[scaled=0.92]{helvet}") == 0,
              "the package line of Helvetica is %s", line);
        fonts_package_line(&ft[1], line, sizeof line);
        CHECK(strcmp(line, "\\usepackage{lmodern}") == 0,
              "a package without options has none: %s", line);
        fonts_package_line(&ft[17], line, sizeof line);
        CHECK(line[0] == '\0', "Computer Modern needs no package");
        CHECK(strcmp(fonts_family_command(FF_SANS), "\\sffamily") == 0 &&
                  strcmp(fonts_family_command(FF_MONO), "\\ttfamily") == 0,
              "the family commands");
        CHECK(fonts_word_ok("scaled=0.92") != 0 && fonts_word_ok("a b") == 0 &&
                  fonts_word_ok("a;b") == 0 && fonts_word_ok("a|b") == 0 &&
                  fonts_word_ok("$(x)") == 0,
              "only safe characters are allowed in a package name");

        tmp_path(fdir, sizeof fdir, "strata-fonttest-XXXXXX");

        if (mkdtemp(fdir) != NULL) {
            char fpath[PATH_MAX + 64];

            setenv("STRATA_HOME", fdir, 1);
            str_copy(ft[19].name, sizeof ft[19].name, "Charter Again");
            str_copy(ft[19].package, sizeof ft[19].package, "charter");
            ft[19].family = FF_ROMAN;
            CHECK(fonts_save(ft, ferr, sizeof ferr) == 0, "save the list: %s",
                  ferr);
            snprintf(fpath, sizeof fpath, "%s/fonts", fdir);
            CHECK(stat(fpath, &fst) == 0 && (fst.st_mode & 0777) == 0600,
                  "the file of the list is for the user only");
            fonts_load(back);
            CHECK(strcmp(back[19].name, "Charter Again") == 0 &&
                      strcmp(back[19].package, "charter") == 0 &&
                      strcmp(back[4].package, "helvet") == 0,
                  "the list is read back");
            str_copy(ft[19].package, sizeof ft[19].package, "x y");
            CHECK(fonts_save(ft, ferr, sizeof ferr) != 0 &&
                      strstr(ferr, "Font 20") != NULL,
                  "an unsafe package name is refused (%s)", ferr);
            unsetenv("STRATA_HOME");
        }

        char msg[200];

        CHECK(fonts_install(&ft[1], msg, sizeof msg, NULL, NULL) != 0 &&
                  strstr(msg, "no archive") != NULL,
              "a font without an archive cannot be installed (%s)", msg);
        str_copy(ft[19].ctan, sizeof ft[19].ctan, "../x");
        CHECK(fonts_install(&ft[19], msg, sizeof msg, NULL, NULL) != 0,
              "an archive name with a slash is refused");

        if (fonts_installed(&ft[1]) == 0) {
            printf("skip: TeX is not installed (lmodern was not found)\n");
        } else {
            CHECK(fonts_installed(&ft[1]) != 0 &&
                      fonts_installed(&ft[17]) != 0,
                  "lmodern is found, and Computer Modern needs nothing");
        }
    }

    /* Orientation, Whitespace and Keycap fields: the choices, and the label of
       a Whitespace marker with a unit. */
    {
        const MInfo *oi = marker_info(36);
        const MInfo *wi = marker_info(31);
        const MInfo *ki = marker_info(37);
        char lb[96];
        Tok ws = marker_tok(31, "11500\r\nU1\r\n", 12);
        Tok ws0 = marker_tok(31, "13000\r\n", 8);

        CHECK(oi != NULL && oi->nfields == 1 &&
                  strcmp(oi->fields[0].choices[0], "[Portrait]") == 0 &&
                  strcmp(oi->fields[0].choices[2], "Landscape") == 0 &&
                  oi->fields[0].choices[3] == NULL,
              "Orientation offers the document's, Portrait and Landscape");
        CHECK(wi != NULL && wi->nfields == 3 &&
                  strcmp(wi->fields[2].id, "2") == 0 &&
                  strcmp(wi->fields[2].dflt, "1") == 0 &&
                  strcmp(wi->fields[2].choices[0], "Crop at End of Page") ==
                      0 &&
                  strcmp(wi->fields[2].choices[1],
                         "Place All on Next Page") == 0 &&
                  strcmp(wi->fields[2].choices[2], "Float to Next Page") ==
                      0 &&
                  wi->fields[2].choices[3] == NULL,
              "Whitespace has the treatment field 2, Place All is its "
              "default");
        CHECK(wi != NULL && wi->nfields == 3 &&
                  strcmp(wi->fields[1].id, "U") == 0 &&
                  strcmp(wi->fields[1].choices[0], "in") == 0 &&
                  strcmp(wi->fields[1].choices[1], "cm") == 0,
              "Whitespace has a unit, inches at first");
        const MInfo *fi = marker_info(15);

        CHECK(fi != NULL && fi->nfields == 1 &&
                  fi->fields[0].kind == MF_CHOICE &&
                  strcmp(fi->fields[0].choices[0], "[Text]") == 0 &&
                  strcmp(fi->fields[0].choices[7], "Font 7") == 0,
              "the Font marker chooses among the fonts, [Text] at first");
        CHECK(ki != NULL && ki->nfields == 5 &&
                  strcmp(ki->fields[4].id, "5") == 0 &&
                  strcmp(ki->fields[4].label, "Font") == 0,
              "Keycap has a font field, number 5");
        marker_render(&ws, lb, sizeof lb);
        CHECK(strcmp(lb, "WS 1.5 cm") == 0, "Whitespace label has the unit "
                                            "(%s)", lb);
        marker_render(&ws0, lb, sizeof lb);
        CHECK(strcmp(lb, "WS 3 in") == 0, "Whitespace without a unit is in "
                                          "inches (%s)", lb);
        tok_release(&ws);
        tok_release(&ws0);

        const MInfo *pi = marker_info(25);
        Tok pt_in = marker_tok(25, "1a.png\0\r\n52\r\n", 14);
        Tok pt_fl = marker_tok(25, "1a.png\0\r\n51\r\n", 14);

        CHECK(pi != NULL && pi->nfields == 7 &&
                  strcmp(pi->fields[1].choices[2], "In Text") == 0 &&
                  strcmp(pi->fields[4].id, "U") == 0,
              "Picture has the position In Text and a unit");
        CHECK(marker_picture_in_text(&pt_in) != 0 &&
                  marker_picture_in_text(&pt_fl) == 0,
              "a picture In Text is told from a floating one");
        tok_release(&pt_in);
        tok_release(&pt_fl);
    }

    /* The Keycap Format: the records are written and read, the defaults
       leave no record, and a value of the marker wins over the global one. */
    {
        Doc *kd = doc_new(1);
        KeycapFmt kf;
        KeycapFmt kv;
        size_t klen;
        char kerr[64];
        uint8_t *kb;
        Doc *kr;
        Tok kk0 = marker_tok(37, "1Esc\0\r\n", 7);
        Tok kk1 = marker_tok(37, "1Esc\0\r\n2300\r\n3500\r\n53\r\n", 23);
        Tok kk2 = marker_tok(36, "", 0);

        keycap_format_get(kd, &kf);
        CHECK(kf.fg == 0 && kf.bg == 1000 && kf.scale == 1000 && kf.font == 2,
              "the Keycap Format has the defaults 0.0, 100.0, 1.000, Text");
        kb = doc_serialize(kd, &klen);
        CHECK(memmem(kb, klen, "\r\nOK", 5) == NULL,
              "the default Keycap Format leaves no record");
        free(kb);

        kf.fg = 200;
        kf.bg = 800;
        kf.scale = 1500;
        kf.font = 7;
        keycap_format_set(kd, &kf);
        CHECK(strcmp(doc_opt_get(kd, "OKF"), "200") == 0 &&
                  strcmp(doc_opt_get(kd, "OKB"), "800") == 0 &&
                  strcmp(doc_opt_get(kd, "OKS"), "1500") == 0 &&
                  strcmp(doc_opt_get(kd, "OKT"), "7") == 0,
              "the Keycap Format is stored as OKF200 OKB800 OKS1500 OKT7");
        kb = doc_serialize(kd, &klen);
        kr = doc_load_mem(kb, klen, kerr, sizeof kerr);
        CHECK(kr != NULL, "a header with the Keycap Format loads: %s", kerr);

        if (kr != NULL) {
            KeycapFmt rf;

            keycap_format_get(kr, &rf);
            CHECK(rf.fg == 200 && rf.bg == 800 && rf.scale == 1500 &&
                      rf.font == 7,
                  "the Keycap Format is read back (%d %d %d %d)", rf.fg,
                  rf.bg, rf.scale, rf.font);
            doc_free(kr);
        }

        {
            const void *po = memmem(kb, klen, "\r\nOKF200", 8);
            const void *pb = memmem(kb, klen, "\r\nOKB800", 8);
            const void *ps = memmem(kb, klen, "\r\nOKS1500", 9);
            const void *pt = memmem(kb, klen, "\r\nOKT7", 6);

            CHECK(po != NULL && pb != NULL && ps != NULL && pt != NULL &&
                      po < pb && pb < ps && ps < pt,
                  "the records OKF, OKB, OKS and OKT are in this order");
        }

        free(kb);

        CHECK(keycap_values(kd, &kk0, &kv) == 0 && kv.fg == 200 &&
                  kv.bg == 800 && kv.scale == 1500 && kv.font == 7,
              "a keycap without values takes the global ones");
        CHECK(keycap_values(kd, &kk1, &kv) == 0 && kv.fg == 300 &&
                  kv.bg == 500 && kv.scale == 1500 && kv.font == 3,
              "a keycap value wins over the global one (%d %d %d %d)", kv.fg,
              kv.bg, kv.scale, kv.font);
        CHECK(keycap_values(kd, &kk2, &kv) != 0,
              "only a Keycap marker has Keycap values");
        CHECK(keycap_values(NULL, &kk0, &kv) == 0 && kv.fg == 0 &&
                  kv.bg == 1000 && kv.scale == 1000 && kv.font == 2,
              "a keycap without a document has the defaults");

        keycap_format_get(kd, &kf);
        kf.fg = 0;
        kf.bg = 1000;
        kf.scale = 1000;
        kf.font = 2;
        keycap_format_set(kd, &kf);
        CHECK(doc_opt_get(kd, "OKF") == NULL &&
                  doc_opt_get(kd, "OKB") == NULL &&
                  doc_opt_get(kd, "OKS") == NULL &&
                  doc_opt_get(kd, "OKT") == NULL,
              "the default values remove the records");
        tok_release(&kk0);
        tok_release(&kk1);
        tok_release(&kk2);
        doc_free(kd);
    }

    /* A new document justifies its text evenly; a header without the
       record still means Left. */
    {
        Doc *jd = doc_new(1);
        const char *tj = doc_fmt_get(jd, 0, "TJ");

        CHECK(tj != NULL && strcmp(tj, "3") == 0,
              "a new document has Text Justification Even");
        doc_free(jd);
    }

    /* Notes: their marks, the settings in the header, and the printout. */
    {
        char txt[160];
        NoteFmt nf;
        NoteFmt nf2;
        Doc *nd = doc_new(0);
        Block *nb = block_new(1);
        static const char fpay[] = "S\r\nTfirst note\x1d\x00\r\n";
        static const char lpay[] = "S\r\nTa levelnote\x1d\x00\r\n12\r\n";
        static const char epay[] = "S\r\nTan endnote\x1d\x00\r\n13\r\n";

        note_tag_text("\\1\\", 3, txt, sizeof txt);
        CHECK(strcmp(txt, "3") == 0, "the mark \\1\\ of note 3 is 3");
        note_tag_text("\\a\\)", 2, txt, sizeof txt);
        CHECK(strcmp(txt, "b)") == 0, "the tag \\a\\) of note 2 is b)");
        note_tag_text("[\\I\\]", 4, txt, sizeof txt);
        CHECK(strcmp(txt, "[IV]") == 0, "the tag [\\I\\] of note 4");
        note_tag_text("*", 3, txt, sizeof txt);
        CHECK(strcmp(txt, "***") == 0, "a plain tag repeats for each note");

        note_format_get(nd, &nf);
        CHECK(strcmp(nf.k[NK_FOOT].reftag, "\\1\\") == 0 &&
                  strcmp(nf.k[NK_LEVEL].notetag, "\\a\\)") == 0 &&
                  strcmp(nf.title, "Endnotes") == 0 && nf.bottom == 1 &&
                  nf.k[NK_FOOT].sep == 2000 && nf.level == 1,
              "the defaults of the Note Format");
        nf.k[NK_FOOT].sp_line = 125;
        nf.k[NK_FOOT].noteattr = NA_BOLD | NA_ITALIC;
        nf.k[NK_FOOT].reftag[0] = '\0';
        str_copy(nf.k[NK_FOOT].reftag, sizeof nf.k[NK_FOOT].reftag, "\\A\\");
        nf.k[NK_FOOT].sep = 3500;
        nf.k[NK_FOOT].renumber = 1;
        nf.bottom = 0;
        nf.level = 2;
        str_copy(nf.title, sizeof nf.title, "Notes Here");
        nf.primary = NK_LEVEL;
        note_format_set(nd, &nf);
        CHECK(strcmp(doc_opt_get(nd, "N1SL"), "125") == 0 &&
                  strcmp(doc_opt_get(nd, "N1Y"), "5") == 0 &&
                  strcmp(doc_opt_get(nd, "N1R"), "\\A\\") == 0 &&
                  strcmp(doc_opt_get(nd, "NS1"), "3500") == 0 &&
                  strcmp(doc_opt_get(nd, "NZ1"), "1") == 0 &&
                  strcmp(doc_opt_get(nd, "NB"), "0") == 0 &&
                  strcmp(doc_opt_get(nd, "NL"), "2") == 0 &&
                  strcmp(doc_opt_get(nd, "NE"), "Notes Here") == 0 &&
                  strcmp(doc_opt_get(nd, "NT"), "2") == 0,
              "the records that the original wrote for the Note Format");
        CHECK(doc_opt_get(nd, "N2R") == NULL && doc_opt_get(nd, "N1N") == NULL,
              "a value that is the default has no record");
        note_format_get(nd, &nf2);
        CHECK(nf2.k[NK_FOOT].sp_line == 125 && nf2.k[NK_FOOT].sep == 3500 &&
                  nf2.k[NK_FOOT].noteattr == 5 && nf2.bottom == 0 &&
                  nf2.level == 2 && nf2.primary == NK_LEVEL,
              "the settings are read back");
        nf2 = nf;
        note_format_set(nd, &nf2);
        note_format_get(nd, &nf);
        nf.k[NK_FOOT].sp_line = 100;
        nf.k[NK_FOOT].noteattr = NA_BOLD;
        str_copy(nf.k[NK_FOOT].reftag, sizeof nf.k[NK_FOOT].reftag, "\\1\\");
        nf.k[NK_FOOT].sep = 2000;
        nf.k[NK_FOOT].renumber = 0;
        nf.bottom = 1;
        nf.level = 1;
        str_copy(nf.title, sizeof nf.title, "Endnotes");
        nf.primary = NK_FOOT;
        note_format_set(nd, &nf);
        CHECK(doc_opt_get(nd, "N1SL") == NULL &&
                  doc_opt_get(nd, "NT") == NULL &&
                  doc_opt_get(nd, "NE") == NULL,
              "the defaults again leave no record");

        /* Numbering: footnotes and levelnotes count apart. */
        doc_delete_items(nd, 0, nd->nitems);
        doc_insert_item(nd, 0, IT_SECT, NULL);
        text_free(&nb->cols[0].text);
        text_init(&nb->cols[0].text);

        Tok n0 = marker_tok(1, fpay, sizeof fpay - 1);
        Tok n1 = marker_tok(1, lpay, sizeof lpay - 1);
        Tok n2 = marker_tok(1, fpay, sizeof fpay - 1);
        Tok n3 = marker_tok(1, epay, sizeof epay - 1);

        text_insert(&nb->cols[0].text, 0, n0);
        text_insert(&nb->cols[0].text, 1, n1);
        text_insert(&nb->cols[0].text, 2, n2);
        text_insert(&nb->cols[0].text, 3, n3);
        doc_insert_item(nd, 1, IT_BLOCK, nb);
        marker_prepare(nd);

        const Tok *t0 = &nb->cols[0].text.v[0];
        const Tok *t1 = &nb->cols[0].text.v[1];
        const Tok *t2 = &nb->cols[0].text.v[2];
        const Tok *t3 = &nb->cols[0].text.v[3];

        CHECK(note_kind(t0, NK_FOOT) == NK_FOOT &&
                  note_kind(t1, NK_FOOT) == NK_LEVEL &&
                  note_kind(t3, NK_FOOT) == NK_END,
              "the kind of a note is its type field");
        CHECK(note_kind(t0, NK_LEVEL) == NK_LEVEL &&
                  note_kind(t0, NK_END) == NK_END,
              "a note without a type is of the primary type");
        CHECK(marker_note_number(t0) == 1 && marker_note_number(t2) == 2 &&
                  marker_note_number(t1) == 1 && marker_note_number(t3) == 1,
              "each kind of note is counted apart");
        CHECK(marker_eval(t2, txt, sizeof txt) != 0 && strcmp(txt, "2") == 0,
              "the second footnote has the mark 2");
        CHECK(marker_eval(t1, txt, sizeof txt) != 0 && strcmp(txt, "a") == 0,
              "the first levelnote has the mark a");
        CHECK(marker_eval(t3, txt, sizeof txt) != 0 &&
                  strcmp(txt, "[1]") == 0,
              "the first endnote has the mark [1]");

        marker_print_mode = 2;

        int pn;
        char **pl = render_lines(nd, 60, &pn);
        int found_foot = 0;
        int found_end = 0;

        for (int i = 0; i < pn; i++) {
            if (strstr(pl[i], "1 first note") != NULL ||
                strstr(pl[i], "2 first note") != NULL) {
                found_foot++;
            }

            if (strstr(pl[i], "[1].") == NULL &&
                strstr(pl[i], "1. an endnote") != NULL) {
                found_end++;
            }
        }

        marker_print_mode = 0;
        CHECK(found_foot == 2, "both footnotes are in the printout (%d)",
              found_foot);
        CHECK(found_end == 1, "the endnote is in the printout (%d)",
              found_end);
        free_lines(pl, pn);
        doc_free(nd);
    }

    {
        const char *tw = getenv("SPELL_TWAIN");

        spell_tests(argc > 3 ? argv[3] : "tests/data",
                    tw != NULL ? tw : "../sample.docs/twain.doc");
        rows_tests();
        search_tests();
        number_style_tests();
    }

    thesaurus_tests(argc > 3 ? argv[3] : "tests/data",
                    work_or_tmp(argc, argv, "edtest_out.doc"));

    printf(fails != 0 ? "edtest: %d FAILURES\n" : "edtest: all passed\n",
           fails);

    return fails != 0;
}
