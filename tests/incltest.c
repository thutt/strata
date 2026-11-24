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
 * incltest.c - the unit tests of the Include marker (src/include.c): the
 * joined document, the loop guard, the depth limit, a missing part, a part
 * that is not a document, and the map of positions for the diagnostics.
 * Usage: incltest.
 */
#include "../src/include.h"
#include "../src/marker.h"
#include "../src/util.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks;
static int failures;
static char dir[PATH_MAX];


static void
check(int ok, const char *what)
{
    checks++;

    if (ok == 0) {
        failures++;
        printf("FAIL %s\n", what);
    }
}


/* ------------------------------------------------------------- documents */
typedef struct {
    uint8_t *p;
    size_t n;
    size_t cap;
} Bytes;


static void
put(Bytes *b, const void *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->p = realloc(b->p, b->cap);
    }

    memcpy(b->p + b->n, s, n);
    b->n += n;
}


static void
puts_(Bytes *b, const char *s)
{
    put(b, s, strlen(s));
}


/* One block of one column: the text ends with an end of line. */
static void
blk(Bytes *b, const char *text)
{
    puts_(b, "T");
    puts_(b, text);
    put(b, "\x1d\x00\r\n", 4);
}


/* A block that holds an Include marker. */
static void
inc(Bytes *b, const char *name)
{
    puts_(b, "T\x13" "16(1");
    puts_(b, name);
    put(b, "\x00\r\n", 3);
    put(b, "\x13)\x1d\x00\r\n", 6);
}


static void
start(Bytes *b, int structured)
{
    memset(b, 0, sizeof *b);
    puts_(b, "*R2\r\nMR1001\r\nPP\\1\\");
    put(b, "\x00\r\n", 3);
    puts_(b, "P1C33\r\nFHA1\r\nFTT5\r\nDP0,8500,11000\r\n");

    if (structured != 0) {
        puts_(b, "*B1\r\nS\r\n+\r\n");
    } else {
        puts_(b, "*B0\r\nS\r\n");
    }
}


/* Start a section of a structured document (its headline follows). */
static void
sect(Bytes *b)
{
    puts_(b, "SE0\r\n");
}


static Doc *
finish(Bytes *b, int structured)
{
    char err[256];
    Doc *d;

    if (structured != 0) {
        puts_(b, "-\r\n");
    }

    puts_(b, "*E\r\n");
    d = doc_load_mem(b->p, b->n, err, sizeof err);

    if (d == NULL) {
        printf("FAIL the test document does not load: %s\n", err);
        failures++;
    }

    return d;
}


/* Write the bytes as the file name in the test directory. */
static void
save(Bytes *b, int structured, const char *name)
{
    char path[PATH_MAX + 80];
    FILE *f;

    if (structured != 0) {
        puts_(b, "-\r\n");
    }

    puts_(b, "*E\r\n");
    snprintf(path, sizeof path, "%s/%.60s", dir, name);
    f = fopen(path, "wb");

    if (f == NULL) {
        printf("FAIL cannot write %s\n", path);
        failures++;

        return;
    }

    fwrite(b->p, 1, b->n, f);
    fclose(f);
    free(b->p);
}


static void
save_text(const char *name, const char *text)
{
    char path[PATH_MAX + 80];
    FILE *f;

    snprintf(path, sizeof path, "%s/%.60s", dir, name);
    f = fopen(path, "wb");

    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}


/* The master that is made of bytes, with the path in the test directory. */
static Doc *
master_of(Bytes *b, int structured, const char *name)
{
    Doc *d = finish(b, structured);
    char path[PATH_MAX + 80];

    free(b->p);

    if (d != NULL) {
        snprintf(path, sizeof path, "%s/%.60s", dir, name);
        d->path = strdup(path);
    }

    return d;
}


/* The characters of the first line of a block, as text. */
static void
text_of(const Doc *d, int item, char *out, size_t cap)
{
    size_t n = 0;
    const Block *b = d->items[item].blk;

    out[0] = '\0';

    if (b == NULL) {
        return;
    }

    for (int j = 0; j < b->cols[0].text.n && n + 1 < cap; j++) {
        const Tok *k = &b->cols[0].text.v[j];

        if (k->kind == TK_CHAR) {
            memcpy(out + n, &k->a, 1);
            n++;
        } else if (k->kind == TK_NL) {
            break;
        }
    }

    out[n] = '\0';
}


/* The texts of the blocks of the document, joined with a bar. */
static void
all_text(const Doc *d, char *out, size_t cap)
{
    size_t n = 0;

    out[0] = '\0';

    for (int i = 0; i < d->nitems; i++) {
        char t[200];

        if (d->items[i].blk == NULL) {
            continue;
        }

        text_of(d, i, t, sizeof t);

        if (n + strlen(t) + 2 >= cap) {
            break;
        }

        if (n > 0) {
            out[n++] = '|';
        }

        memcpy(out + n, t, strlen(t));
        n += strlen(t);
        out[n] = '\0';
    }
}


static int
has_code(const DiagList *l, const char *code)
{
    for (int i = 0; i < l->n; i++) {
        if (strcmp(l->v[i].code, code) == 0) {
            return 1;
        }
    }

    return 0;
}


/* ----------------------------------------------------------------- tests */
static void
test_no_marker(void)
{
    Bytes b;
    Doc *d;
    IncludeMap *m = NULL;
    Doc *j;

    start(&b, 0);
    blk(&b, "Plain.");
    d = master_of(&b, 0, "plain.doc");
    marker_set_doc(d);
    marker_prepare(d);
    j = include_expand(d, dir, NULL, &m);
    check(j == NULL && m == NULL, "no marker: the master is written as it is");
    doc_free(d);
}


static void
test_joined(void)
{
    Bytes b;
    Doc *d;
    Doc *j;
    IncludeMap *m = NULL;
    DiagList dl;
    char t[400];
    int oi;
    int oc;
    int ol;
    int oo;

    start(&b, 0);
    blk(&b, "P1");
    blk(&b, "P2");
    save(&b, 0, "part.doc");
    start(&b, 0);
    blk(&b, "A");
    inc(&b, "part");
    blk(&b, "Z");
    d = master_of(&b, 0, "m.doc");
    diag_list_init(&dl);
    marker_set_doc(d);
    marker_prepare(d);
    j = include_expand(d, dir, &dl, &m);
    check(j != NULL && m != NULL, "joined: the document is made");

    if (j == NULL) {
        return;
    }

    all_text(j, t, sizeof t);
    check(strcmp(t, "A|P1|P2|Z") == 0, "joined: the parts at the marker");
    check(dl.n == 0, "joined: no error");
    check(include_files(m) == 1, "joined: one part file was read");
    check(j->structured == 0, "joined: the master's structure");

    include_orig_pos(m, 1, 0, 1, 0, &oi, &oc, &ol, &oo);
    check(oi == 1, "map: the first block is the first block of the master");
    include_orig_pos(m, 2, 0, 1, 0, &oi, &oc, &ol, &oo);
    check(oi == 2 && ol == 1 && oo == 0,
          "map: a block of the part is at the marker");
    include_orig_pos(m, 3, 0, 1, 3, &oi, &oc, &ol, &oo);
    check(oi == 2 && oo == 0, "map: the second block of the part too");
    include_orig_pos(m, 4, 0, 1, 2, &oi, &oc, &ol, &oo);
    check(oi == 3 && oo == 2, "map: the block after is item 3 of the master "
          "with its own offset");
    check(include_orig(m) == d, "map: the master is remembered");

    doc_free(j);
    include_map_free(m);
    diag_list_free(&dl);
    doc_free(d);
}


static void
test_sections(void)
{
    Bytes b;
    Doc *d;
    Doc *j;
    IncludeMap *m = NULL;
    DiagList dl;
    char t[400];
    int types = 0;

    start(&b, 1);
    sect(&b);
    blk(&b, "PartHead");
    blk(&b, "PartText");
    save(&b, 1, "sp.doc");
    start(&b, 1);
    sect(&b);
    blk(&b, "Head");
    blk(&b, "Text");
    inc(&b, "sp");
    sect(&b);
    blk(&b, "Next");
    d = master_of(&b, 1, "ms.doc");
    diag_list_init(&dl);
    marker_set_doc(d);
    marker_prepare(d);
    j = include_expand(d, dir, &dl, &m);
    check(j != NULL && dl.n == 0, "sections: made without an error");

    if (j == NULL) {
        return;
    }

    all_text(j, t, sizeof t);
    check(strcmp(t, "Head|Text|PartHead|PartText|Next") == 0,
          "sections: the part after the text of the section");

    for (int i = 0; i < j->nitems; i++) {
        if (j->items[i].type == IT_PLUS) {
            types++;
        } else if (j->items[i].type == IT_MINUS) {
            types--;
        }
    }

    check(types == 0, "sections: the groups of sections are balanced");
    check(j->nitems == d->nitems - 1 + 5, "sections: the marker's block is "
          "gone and the part's items (group, section, two blocks, group "
          "end) are in");

    doc_free(j);
    include_map_free(m);
    diag_list_free(&dl);
    doc_free(d);
}


static void
test_errors(void)
{
    Bytes b;
    Doc *d;
    Doc *j;
    IncludeMap *m = NULL;
    DiagList dl;
    char t[400];

    save_text("junk.doc", "this is not a document");
    start(&b, 0);
    blk(&b, "Good");
    save(&b, 0, "good.doc");
    start(&b, 0);
    blk(&b, "Top");
    inc(&b, "nowhere");
    inc(&b, "junk");
    inc(&b, "");
    inc(&b, "good");
    d = master_of(&b, 0, "e.doc");
    diag_list_init(&dl);
    marker_set_doc(d);
    marker_prepare(d);
    j = include_expand(d, dir, &dl, &m);
    check(j != NULL, "errors: the document is made");

    if (j == NULL) {
        return;
    }

    check(has_code(&dl, "include-missing") != 0, "errors: missing");
    check(has_code(&dl, "include-damaged") != 0, "errors: not a document");
    check(has_code(&dl, "include-noname") != 0, "errors: no name");
    check(dl.n == 3, "errors: three errors, the good one is fine");

    for (int i = 0; i < dl.n; i++) {
        check(dl.v[i].mtype == INCLUDE_MTYPE && dl.v[i].item == i + 2,
              "errors: at the marker in the master");
        check(strncmp(dl.v[i].msg, "Invalid Include request",
                      strlen("Invalid Include request")) == 0,
              "errors: the message starts with Invalid Include request");
    }

    all_text(j, t, sizeof t);
    check(strstr(t, "Top|[Invalid Include request: ") == t &&
          strstr(t, "|Good") != NULL, "errors: a notice at each place");
    check(include_files(m) == 1, "errors: only the good file counts");

    doc_free(j);
    include_map_free(m);
    diag_list_free(&dl);
    doc_free(d);
}


static void
test_loops(void)
{
    Bytes b;
    Doc *d;
    Doc *j;
    IncludeMap *m = NULL;
    DiagList dl;
    char t[400];

    start(&b, 0);
    blk(&b, "Self");
    inc(&b, "self");
    save(&b, 0, "self.doc");
    start(&b, 0);
    blk(&b, "Ping");
    inc(&b, "pong");
    save(&b, 0, "ping.doc");
    start(&b, 0);
    blk(&b, "Pong");
    inc(&b, "ping");
    save(&b, 0, "pong.doc");

    start(&b, 0);
    inc(&b, "self");
    d = master_of(&b, 0, "m1.doc");
    diag_list_init(&dl);
    marker_set_doc(d);
    marker_prepare(d);
    j = include_expand(d, dir, &dl, &m);
    check(j != NULL && has_code(&dl, "include-loop") != 0 && dl.n == 1,
          "loop: a part that includes itself");

    if (j != NULL) {
        all_text(j, t, sizeof t);
        check(strncmp(t, "Self|[Invalid Include request", 29) == 0,
              "loop: the part is in once, the notice after it");
        doc_free(j);
        include_map_free(m);
    }

    diag_list_free(&dl);
    doc_free(d);

    start(&b, 0);
    inc(&b, "ping");
    d = master_of(&b, 0, "m2.doc");
    diag_list_init(&dl);
    marker_set_doc(d);
    marker_prepare(d);
    m = NULL;
    j = include_expand(d, dir, &dl, &m);
    check(j != NULL && has_code(&dl, "include-loop") != 0 && dl.n == 1,
          "loop: two parts that include each other");
    check(dl.n == 1 && dl.v[0].item == 1, "loop: told at the marker of the "
          "master, not in the part");

    if (j != NULL) {
        doc_free(j);
        include_map_free(m);
    }

    diag_list_free(&dl);
    doc_free(d);
}


static void
test_depth(void)
{
    Bytes b;
    Doc *d;
    Doc *j;
    IncludeMap *m = NULL;
    DiagList dl;
    char name[40];
    char next[40];
    char t[400];

    for (int i = 1; i <= 10; i++) {
        snprintf(name, sizeof name, "n%d.doc", i);
        snprintf(next, sizeof next, "n%d", i + 1);
        start(&b, 0);
        snprintf(t, sizeof t, "L%d", i);
        blk(&b, t);

        if (i < 10) {
            inc(&b, next);
        }

        save(&b, 0, name);
    }

    /* Eight levels of parts are allowed: n1 to n8 are in. */
    start(&b, 0);
    inc(&b, "n1");
    d = master_of(&b, 0, "md.doc");
    diag_list_init(&dl);
    marker_set_doc(d);
    marker_prepare(d);
    j = include_expand(d, dir, &dl, &m);
    check(j != NULL && has_code(&dl, "include-deep") != 0 && dl.n == 1,
          "depth: the ninth level is too deep, told once");

    if (j != NULL) {
        all_text(j, t, sizeof t);
        check(strncmp(t, "L1|L2|L3|L4|L5|L6|L7|L8|[Invalid Include", 40) == 0,
              "depth: eight levels are in");
        check(include_files(m) == 8, "depth: eight files were read");
        doc_free(j);
        include_map_free(m);
    }

    diag_list_free(&dl);
    doc_free(d);
}


int
main(void)
{
    char path[PATH_MAX + 80];

    tmp_path(path, sizeof path, "strata-incltestXXXXXX");

    if (mkdtemp(path) == NULL) {
        printf("FAIL cannot make a directory\n");

        return 1;
    }

    str_copy(dir, sizeof dir, path);
    test_no_marker();
    test_joined();
    test_sections();
    test_errors();
    test_loops();
    test_depth();

    /* The test directory is removed with its files. */
    {
        char cmd[PATH_MAX + 16];

        snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);

        if (system(cmd) != 0) {
            printf("FAIL cannot remove %s\n", dir);
            failures++;
        }
    }

    printf("incltest: %d checks, %d failures\n", checks, failures);

    return failures != 0 ? 1 : 0;
}
