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
 * cmptest.c - the unit tests of the comparison of two documents
 * (src/compare.c): documents of known differences and the report that is
 * expected, the structure (sections added, removed, moved, on another level),
 * the report as a document, and the time and the memory that a large
 * document takes.  Usage: cmptest.
 */
#include "../src/compare.h"
#include "../src/msdoc.h"
#include "../src/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
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


/* ------------------------------------------------------------- documents */
static Block *
make_block(const char *text)
{
    Block *b = block_new(1);
    Tok nl;

    for (const char *s = text; *s != '\0'; s++) {
        text_insert(&b->cols[0].text, b->cols[0].text.n, tok_char(u8(*s)));
    }

    memset(&nl, 0, sizeof nl);
    nl.kind = TK_NL;
    text_insert(&b->cols[0].text, b->cols[0].text.n, nl);

    return b;
}


/* A document with no section: one block for each text. */
static Doc *
flat_doc(const char *const *texts, int n)
{
    Doc *d = doc_new(0);

    doc_delete_items(d, 1, d->nitems - 1);

    for (int i = 0; i < n; i++) {
        doc_insert_item(d, d->nitems, IT_BLOCK, make_block(texts[i]));
    }

    return d;
}


/*
 * A structured document from a list of lines: "T text" is a block of the title
 * page, "H1 text" and "H2 text" are headlines of the first and the second
 * level, "B text" is a block of the section that is open.
 */
static Doc *
struct_doc(const char *const *lines, int n)
{
    Doc *d = doc_new(1);
    int depth = 0;

    doc_delete_items(d, 0, d->nitems);
    doc_insert_item(d, d->nitems, IT_SECT, NULL);

    for (int i = 0; i < n; i++) {
        const char *l = lines[i];

        if (l[0] == 'T') {
            doc_insert_item(d, d->nitems, IT_BLOCK, make_block(l + 2));
        } else if (l[0] == 'H') {
            int level = l[1] - '0';

            while (depth > level) {
                doc_insert_item(d, d->nitems, IT_MINUS, NULL);
                depth--;
            }

            while (depth < level) {
                doc_insert_item(d, d->nitems, IT_PLUS, NULL);
                depth++;
            }

            doc_insert_item(d, d->nitems, IT_SECT, NULL);
            doc_insert_item(d, d->nitems, IT_BLOCK, make_block(l + 3));
        } else {
            doc_insert_item(d, d->nitems, IT_BLOCK, make_block(l + 2));
        }
    }

    while (depth > 0) {
        doc_insert_item(d, d->nitems, IT_MINUS, NULL);
        depth--;
    }

    return d;
}


/* Whether some line of the report holds the text. */
static int
has_line(const CmpReport *r, const char *text)
{
    for (int i = 0; i < r->nlines; i++) {
        if (strstr(r->lines[i], text) != NULL) {
            return 1;
        }
    }

    if (getenv("CMPDUMP") != NULL) {
        printf("--- not found: %s\n", text);

        for (int i = 0; i < r->nlines; i++) {
            printf("| %s\n", r->lines[i]);
        }
    }

    return 0;
}


/* ----------------------------------------------------------------- tests */
static void
test_same(void)
{
    const char *t[] = {"Alpha beta.", "Gamma delta.", ""};
    Doc *a = flat_doc(t, 3);
    Doc *b = flat_doc(t, 3);
    CmpReport r;

    cmp_compare(a, b, "a.doc", "b.doc", &r);
    check(cmp_same(&r) != 0, "same: the documents are the same");
    check(has_line(&r, "same text, sections and format") != 0,
          "same: the report says so");
    check(r.blocks_a == 3 && r.blocks_b == 3, "same: three blocks each");
    cmp_free(&r);
    doc_free(a);
    doc_free(b);
}


static void
test_blocks(void)
{
    const char *ta[] = {"Intro text here.", "The quick brown fox",
                        "This block is removed completely.", "Last block."};
    const char *tb[] = {"Intro text here.", "The slow brown fox jumps",
                        "Last block.", "A new block at the end."};
    Doc *a = flat_doc(ta, 4);
    Doc *b = flat_doc(tb, 4);
    CmpReport r;

    cmp_compare(a, b, "first.doc", "second.doc", &r);
    check(cmp_same(&r) == 0, "blocks: the documents differ");
    check(r.changed == 1, "blocks: one block changed");
    check(r.deleted == 1, "blocks: one block removed");
    check(r.inserted == 1, "blocks: one block inserted");
    check(r.moved == 0 && r.format_changed == 0, "blocks: nothing else");
    check(has_line(&r, "[-quick-] [+slow+]") != 0,
          "blocks: the word that changed is shown");
    check(has_line(&r, "[+jumps+]") != 0, "blocks: the word that was added");
    check(has_line(&r, "[-This block is removed completely.-]") != 0,
          "blocks: the block that was removed is shown");
    check(has_line(&r, "[+A new block at the end.+]") != 0,
          "blocks: the block that was inserted is shown");
    check(has_line(&r, "block 2 of the first document, block 2 of the") != 0,
          "blocks: the numbers of the changed block");
    cmp_free(&r);
    doc_free(a);
    doc_free(b);
}


static void
test_format(void)
{
    const char *t[] = {"Same words one.", "Same words two."};
    Doc *a = flat_doc(t, 2);
    Doc *b = flat_doc(t, 2);
    CmpReport r;
    Tok bold;

    memset(&bold, 0, sizeof bold);
    bold.kind = TK_ATTR;
    bold.a = ATTR_BOLD;
    text_insert(&b->items[1].blk->cols[0].text, 0, bold);
    lines_add(&b->items[2].blk->props, "BL5", 3);
    cmp_compare(a, b, "a.doc", "b.doc", &r);
    check(r.format_changed == 2, "format: two blocks with another format");
    check(r.changed == 0 && r.inserted == 0 && r.deleted == 0,
          "format: the words are the same");
    check(has_line(&r, "Markers or attributes changed") != 0,
          "format: the attribute is reported");
    check(has_line(&r, "Format changed") != 0, "format: the format");
    cmp_free(&r);
    doc_free(a);
    doc_free(b);
}


static void
test_structure(void)
{
    const char *ta[] = {"T Title page.",  "H1 One",  "B Body one.",
                        "H1 Two",         "B Body two.", "H1 Three",
                        "B Body three."};
    const char *tb[] = {"T Title page.",  "H1 One",    "B Body one.",
                        "H1 Three",       "B Body three.", "H1 Two",
                        "B Body two.",    "H1 Four",   "B Body four."};
    Doc *a = struct_doc(ta, 7);
    Doc *b = struct_doc(tb, 9);
    CmpReport r;

    cmp_compare(a, b, "a.doc", "b.doc", &r);
    check(r.sections_a == 3 && r.sections_b == 4, "structure: the sections");
    check(r.sect_added == 1, "structure: one section added");
    check(r.sect_removed == 0, "structure: no section removed");
    check(r.sect_moved == 1, "structure: one section moved");
    check(r.moved == 1, "structure: the block of the moved section moved");
    check(r.inserted == 1, "structure: the block of the new section");
    check(has_line(&r, "Section added: 4 \"Four\"") != 0,
          "structure: the new section by its number and headline");
    check(has_line(&r, "Section moved: \"Two\" from 2") != 0 ||
              has_line(&r, "Section moved: \"Three\" from 3") != 0,
          "structure: the moved section");
    cmp_free(&r);
    doc_free(a);
    doc_free(b);
}


static void
test_removed_and_level(void)
{
    const char *ta[] = {"H1 One", "B Body one.", "H1 Two", "B Body two.",
                        "H1 Gone", "B Body gone."};
    const char *tb[] = {"H1 One", "B Body one.", "H2 Two", "B Body two."};
    Doc *a = struct_doc(ta, 6);
    Doc *b = struct_doc(tb, 4);
    CmpReport r;

    cmp_compare(a, b, "a.doc", "b.doc", &r);
    check(r.sect_removed == 1, "level: the section that is gone");
    check(r.deleted == 1, "level: its body");
    check(r.sect_level == 1, "level: one section on another level");
    check(has_line(&r, "Section removed: 3 \"Gone\"") != 0,
          "level: the section that was removed");
    check(has_line(&r, "Section on another level: \"Two\" is 2 in the first") !=
              0,
          "level: the section on another level");
    cmp_free(&r);
    doc_free(a);
    doc_free(b);
}


static void
test_report_document(void)
{
    const char *ta[] = {"One two three.", "Gone block."};
    const char *tb[] = {"One four three.", "New block."};
    Doc *a = flat_doc(ta, 2);
    Doc *b = flat_doc(tb, 2);
    CmpReport r;
    Doc *rd;
    size_t len = 0;
    uint8_t *data;
    Doc *back;
    char err[128];
    int found_bold = 0;
    int found_under = 0;
    int found_title = 0;

    cmp_compare(a, b, "a.doc", "b.doc", &r);
    rd = cmp_report_doc(&r);
    check(rd != NULL && rd->nitems > 3, "document: the report has blocks");
    data = doc_serialize(rd, &len);
    back = doc_load_mem(data, len, err, sizeof err);
    check(back != NULL, "document: the report is a document that loads");

    for (int i = 0; back != NULL && i < back->nitems; i++) {
        const Item *it = &back->items[i];

        if (it->type != IT_BLOCK || it->blk == NULL) {
            continue;
        }

        const Text *t = &it->blk->cols[0].text;
        char plain[512];

        text_plain(t, plain, sizeof plain);
        found_title += strstr(plain, "Comparison of two documents") != NULL;

        for (int k = 0; k < t->n; k++) {
            if (t->v[k].kind == TK_ATTR && t->v[k].a == ATTR_BOLD) {
                found_bold = 1;
            }

            if (t->v[k].kind == TK_ATTR && t->v[k].a == ATTR_UNDERW) {
                found_under = 1;
            }
        }
    }

    check(found_title == 1, "document: the title");
    check(found_bold != 0, "document: the words that were added are bold");
    check(found_under != 0,
          "document: the words that were removed are underlined");
    free(data);
    doc_free(back);
    doc_free(rd);
    cmp_free(&r);
    doc_free(a);
    doc_free(b);
}


#define POOLW 64


/* Makes the text of paragraph i in the pool and returns it. */
static const char *
numbered(char *pool, const char *word, int i)
{
    char *s = pool + (size_t) i * POOLW;

    snprintf(s, POOLW, "%s paragraph number %d with some ordinary words", word,
             i);

    return s;
}


static void
test_large(void)
{
    enum { N = 20000 };
    const char **ta = malloc(N * sizeof *ta);
    const char **tb = malloc(N * sizeof *tb);
    char *pa = malloc((size_t) N * POOLW);
    char *pb = malloc((size_t) N * POOLW);
    char *pc = malloc((size_t) N * POOLW);
    struct rusage ru;
    CmpReport r;
    clock_t t0;
    long ms;
    Doc *a;
    Doc *b;

    for (int i = 0; i < N; i++) {
        ta[i] = numbered(pa, "alpha", i);
        tb[i] = (i % 100 == 50) ? numbered(pb, "changed", i) :
                                  numbered(pc, "alpha", i);
    }

    a = flat_doc(ta, N);
    b = flat_doc(tb, N);
    t0 = clock();
    cmp_compare(a, b, "big-a.doc", "big-b.doc", &r);
    ms = (clock() - t0) * 1000L / CLOCKS_PER_SEC;
    getrusage(RUSAGE_SELF, &ru);
    printf("large: %d blocks, %d changed, %ld ms, peak %ld KB\n", N,
           r.changed, ms, ru.ru_maxrss);
    check(r.changed == 200, "large: 200 blocks changed");
    check(r.too_large == 0, "large: a few differences are compared in full");
    check(ru.ru_maxrss < 400000, "large: the memory stays below 400 MB");
    check(ms < 20000, "large: the time stays below 20 seconds");
    cmp_free(&r);
    doc_free(a);
    doc_free(b);

    /* Two documents with nothing in common: too different to compare. */
    for (int i = 0; i < 4000; i++) {
        ta[i] = numbered(pa, "left", i);
        tb[i] = numbered(pb, "right", i);
    }

    a = flat_doc(ta, 4000);
    b = flat_doc(tb, 4000);
    t0 = clock();
    cmp_compare(a, b, "x.doc", "y.doc", &r);
    ms = (clock() - t0) * 1000L / CLOCKS_PER_SEC;
    getrusage(RUSAGE_SELF, &ru);
    printf("large: 4000 blocks with nothing in common, %ld ms, peak %ld KB\n",
           ms, ru.ru_maxrss);
    check(r.too_large == 1, "large: too many differences are reported so");
    check(r.changed == 4000 && r.deleted == 0 && r.inserted == 0,
          "large: the blocks are paired as changed, not compared by word");
    check(has_line(&r, "too different for a detailed comparison") != 0,
          "large: the report says so");
    cmp_free(&r);
    doc_free(a);
    doc_free(b);
    free(pa);
    free(pb);
    free(pc);
    free(ta);
    free(tb);
}


int
main(void)
{
    test_same();
    test_blocks();
    test_format();
    test_structure();
    test_removed_and_level();
    test_report_document();
    test_large();
    printf("cmptest: %d checks, %d failures\n", checks, failures);

    return failures == 0 ? 0 : 1;
}
