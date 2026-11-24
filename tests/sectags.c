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
 * The section tags of a document, as the Editor draws them, and the checks of
 * the numbers of the sections.
 *
 *     sectags FILE.doc
 *     sectags --check FILE.doc
 *
 * The first form lists, for every headline of the document, the number of
 * the item, the level, the tag and the first word of the text of the
 * headline, one headline to a line, separated by a tab character:
 *
 *     ITEM LEVEL TAG HEADWORD
 *
 * The tag is made by fmt_resolve, the function that the Editor calls, so
 * that tests/secnum.py can compare it with the number that the Printer
 * prints in front of the headline.  A tag that has no text is the empty
 * word between the tab characters.  The bytes of the tag are written as
 * they are, in the code page of the document.
 *
 * The second form checks the document and writes a line "FAIL" for each
 * check that fails, and at the end the number of checks and of failures (the
 * exit status is 1 when there is one).  What it checks:
 *   - the number of a section as the status line shows it (sect_number) is
 *     the number that Go To section and the sections of a Print take
 *     (sect_find), the number that the Error Log names (diag_locate) and
 *     the number of a cross-reference (marker_section_numbers);
 *   - every part of the document that a Print or a Preview can make (the
 *     current section of every section, every block, and the range of
 *     sections from one section to another one) has, for each headline, the
 *     level and the tag that the headline has in the whole document, and has
 *     no headline that the whole document does not have.
 */
#include "../src/util.h"
#include "../src/diag.h"
#include "../src/editor.h"
#include "../src/fileops.h"
#include "../src/layout.h"
#include "../src/marker.h"
#include "../src/msdoc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXHEAD 4096

static int checks;
static int failures;

#define CHECK(c, ...)                                                          \
    do {                                                                       \
        checks++;                                                              \
        if (!(c)) {                                                            \
            printf("FAIL ");                                                   \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
            failures++;                                                        \
        }                                                                      \
    } while (0)


static size_t
strlen_u8(const uint8_t *s)
{
    size_t n = 0;

    while (s[n] != 0) {
        n++;
    }

    return n;
}


/* The first word of the text of a headline: its letters and digits. */
static void
head_word(const Block *b, uint8_t *out, size_t cap)
{
    size_t o = 0;
    int started = 0;

    out[0] = '\0';

    if (b->ncols < 1) {
        return;
    }

    const Text *t = &b->cols[0].text;

    for (int i = 0; i < t->n && o + 1 < cap; i++) {
        const Tok *k = &t->v[i];

        if (k->kind != TK_CHAR) {
            if (started != 0) {
                break;
            }

            continue;
        }

        if (!((k->a >= '0' && k->a <= '9') || (k->a >= 'A' && k->a <= 'Z') ||
              (k->a >= 'a' && k->a <= 'z'))) {
            if (started != 0) {
                break;
            }

            continue;
        }

        started = 1;
        out[o++] = k->a;
    }

    out[o] = '\0';
}


/* The headlines of d: the item and the Fmt of each. */
static int
headlines(const Doc *d, int *item, Fmt *fm, int cap)
{
    int n = 0;

    for (int i = 1; i < d->nitems && n < cap; i++) {
        Fmt f;

        if (d->items[i].type != IT_BLOCK || d->items[i - 1].type != IT_SECT) {
            continue;
        }

        fmt_resolve(d, i, 0, &f);

        if (f.headline != 0 && f.level >= 1) {
            item[n] = i;
            fm[n] = f;
            n++;
        }
    }

    return n;
}


/*
 * The numbers that the Editor, the Error Log and the cross-references give
 * to the sections.  A section nested deeper than SECT_MAXDEPTH levels has no
 * number of its own (the original gives it no tag): its status line shows
 * the number of its ancestor of the last numbered level, Go To takes that
 * number to the ancestor, and no cross-reference can name it.
 */
static void
check_numbers(const char *name, const Doc *d, const int *item, const Fmt *fm,
              int n)
{
    MarkerSecNum *sn = NULL;
    int nsn = marker_section_numbers(d, &sn);
    int last_numbered = -1;

    for (int k = 0; k < n; k++) {
        char num[SECT_NUMLEN];
        Diag g;
        int found = -1;
        int unnumbered = fm[k].level > SECT_MAXDEPTH;
        int want = unnumbered ? last_numbered : item[k];

        if (fm[k].level == SECT_MAXDEPTH) {
            last_numbered = item[k];
        }

        sect_number(d, item[k], num, sizeof num);
        memset(&g, 0, sizeof g);
        g.item = item[k];
        diag_locate(d, &g);

        for (int j = 0; j < nsn; j++) {
            if (sn[j].item == item[k]) {
                found = j;
            }
        }

        int at = sect_find(d, num);

        CHECK(at == want, "%s: item %d: Go To section %s finds item %d, "
              "expected %d", name, item[k], num, at, want);
        CHECK(strcmp(g.sect_tag, num) == 0,
              "%s: item %d: the Error Log shows section [%s], the status line "
              "[%s]", name, item[k], g.sect_tag, num);
        CHECK(unnumbered ? found < 0 :
              (found >= 0 && strcmp(sn[found].num, num) == 0),
              "%s: item %d: the cross-reference number is [%s], the status "
              "line [%s]", name, item[k], found >= 0 ? sn[found].num : "-",
              num);
    }

    free(sn);
}


/* The headlines of a part have the level and the tag of the whole. */
static void
check_part(const char *name, const char *what, const Doc *d, const int *item,
           const Fmt *fm, int n, Doc *part)
{
    static int pitem[MAXHEAD];
    static Fmt pfm[MAXHEAD];
    int pn;

    doc_levels_end();
    doc_levels_begin(part);
    pn = headlines(part, pitem, pfm, MAXHEAD);
    doc_levels_end();
    doc_levels_begin(d);

    for (int p = 0; p < pn; p++) {
        const Block *b = part->items[pitem[p]].blk;
        int k;

        for (k = 0; k < n; k++) {
            if (d->items[item[k]].blk == b) {
                break;
            }
        }

        CHECK(k < n, "%s: %s: item %d is a headline of the part and not of "
              "the document", name, what, pitem[p]);

        if (k < n) {
            CHECK(pfm[p].level == fm[k].level,
                  "%s: %s: headline %d is on level %d, in the document on "
                  "level %d", name, what, item[k], pfm[p].level,
                  fm[k].level);
            CHECK(strcmp(pfm[p].tag, fm[k].tag) == 0,
                  "%s: %s: headline %d has the tag [%s], in the document "
                  "[%s]", name, what, item[k], pfm[p].tag, fm[k].tag);
        }
    }
}


static void
check_parts(const char *name, Doc *d, const int *item, const Fmt *fm, int n)
{
    int total = 0;

    /* The current section of every section. */
    for (int k = 0; k < n; k++) {
        Doc part;
        char what[64];

        if (doc_part_current(d, item[k], &part) == 0) {
            snprintf(what, sizeof what, "the section of item %d", item[k]);
            check_part(name, what, d, item, fm, n, &part);
            doc_part_free(&part);
            total++;
        }
    }

    /* Every block of the document, as a Preview of a block makes it. */
    for (int i = 0; i < d->nitems; i++) {
        Doc part;
        char what[64];

        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        if (doc_part_block(d, i, &part) == 0) {
            snprintf(what, sizeof what, "the block of item %d", i);
            check_part(name, what, d, item, fm, n, &part);
            doc_part_free(&part);
            total++;
        }
    }

    /* Ranges of sections, by the numbers that Print and Preview take. */
    for (int a = -1; a < n; a++) {
        for (int z = (a < 0 ? 0 : a); z < n; z++) {
            Doc part;
            char from[SECT_NUMLEN] = "";
            char to[SECT_NUMLEN];
            char what[3 * SECT_NUMLEN];
            char err[128];

            if (n > 40 && (z - a) > 3 && z != n - 1) {
                continue;
            }

            if (a >= 0) {
                sect_number(d, item[a], from, sizeof from);
            }

            sect_number(d, item[z], to, sizeof to);

            if (doc_part_sections(d, from, to, &part, err, sizeof err) == 0) {
                snprintf(what, sizeof what, "sections [%s] to [%s]", from,
                         to);
                check_part(name, what, d, item, fm, n, &part);
                doc_part_free(&part);
                total++;
            }
        }
    }

    CHECK(total > 0, "%s: no part of the document was made", name);
}


static int
check_file(const char *path)
{
    char err[200];
    static int item[MAXHEAD];
    static Fmt fm[MAXHEAD];
    Doc *d = doc_load(path, err, sizeof err);
    int n;

    if (d == NULL) {
        fprintf(stderr, "sectags: %s: %s\n", path, err);

        return 1;
    }

    doc_levels_begin(d);
    n = headlines(d, item, fm, MAXHEAD);
    check_numbers(path, d, item, fm, n);

    if (d->structured != 0 && n > 0) {
        check_parts(path, d, item, fm, n);
    }

    doc_levels_end();
    doc_free(d);
    printf("sectags: %s: %d headlines\n", path, n);

    return 0;
}


int
main(int argc, char **argv)
{
    char err[200];
    Doc *d;

    if (argc == 3 && strcmp(argv[1], "--check") == 0) {
        int rc = check_file(argv[2]);

        printf("sectags: %d checks, %d failures\n", checks, failures);

        return rc != 0 || failures != 0 ? 1 : 0;
    }

    if (argc != 2) {
        fprintf(stderr, "usage: sectags [--check] FILE.doc\n");

        return 2;
    }

    d = doc_load(argv[1], err, sizeof err);

    if (d == NULL) {
        fprintf(stderr, "sectags: %s: %s\n", argv[1], err);

        return 1;
    }

    doc_levels_begin(d);

    for (int i = 1; i < d->nitems; i++) {
        Fmt f;
        uint8_t word[64];

        if (d->items[i].type != IT_BLOCK || d->items[i - 1].type != IT_SECT) {
            continue;
        }

        fmt_resolve(d, i, 0, &f);

        if (f.headline == 0 || f.level < 1) {
            continue;
        }

        head_word(d->items[i].blk, word, sizeof word);
        printf("%d\t%d\t%s\t", i, f.level, f.tag);
        fwrite(word, 1, strlen_u8(word), stdout);
        printf("\n");
    }

    doc_levels_end();
    doc_free(d);

    return 0;
}
