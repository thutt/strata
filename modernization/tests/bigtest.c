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
 * bigtest.c - the Editor on a very large document, without a terminal.  It
 * loads the document, lays it out and runs the commands a person gives: page
 * down through the text, Search from the start to the end, Replace all, Go
 * To section, outline collapse and expand, insert and delete in the middle,
 * Save and reload.  Every step prints its time, and a step that needs more
 * than the limit (a second unless the third argument says otherwise) is a
 * failure, like a wrong answer.
 *
 *     bigtest DOC NEEDLE [LIMIT_MS]
 *
 * NEEDLE is a word that is found only near the end of the document.  When
 * the variable BIGTEST_NEEDLE_AFTER is a percent, the needle must be found
 * after that part of the document.
 */
#define _GNU_SOURCE
#include "../src/util.h"
#include "../src/editor.h"
#include "../src/fileops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails = 0;
static long limit_ms = 1000;


/* The time in milliseconds. */
static long
now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


/* The peak memory of the process in kilobytes. */
static long
peak_kb(void)
{
    char line[256];
    long v = -1;
    FILE *f = fopen("/proc/self/status", "r");

    if (f == NULL) {
        return -1;
    }

    while (fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, "VmHWM:", 6) == 0) {
            v = strtol(line + 6, NULL, 10);
            break;
        }
    }

    fclose(f);

    return v;
}


/*
 * Print the time of a step that did count commands, and fail when one of
 * them took more than the limit on the average.
 */
static void
steps(const char *what, long t0, int count)
{
    long ms = now() - t0;

    printf("%-34s %6ld ms  peak %7ld KB\n", what, ms, peak_kb());

    if (ms > limit_ms * count) {
        printf("FAIL %s took %ld ms, the limit is %ld ms\n", what, ms,
               limit_ms * count);
        fails++;
    }

    fflush(stdout);
}


/* Print the time of a step and fail when it was too slow. */
static void
step(const char *what, long t0)
{
    steps(what, t0, 1);
}


static void
check(int ok, const char *what)
{
    if (ok == 0) {
        printf("FAIL %s\n", what);
        fails++;
    }
}


/* The bytes of a file; the length goes to len, NULL when it is missing. */
static uint8_t *
slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf = NULL;
    size_t cap = 0;
    size_t n = 0;

    if (f == NULL) {
        return NULL;
    }

    for (;;) {
        size_t got;

        if (n + 65536 > cap) {
            cap = cap != 0 ? cap * 2 : 1 << 20;
            buf = realloc(buf, cap);
        }

        got = fread(buf + n, 1, 65536, f);
        n += got;

        if (got < 65536) {
            break;
        }
    }

    fclose(f);
    *len = n;

    return buf;
}


/* The rows of the screen as the Editor would draw them: touch every cell. */
static unsigned long
screen_sum(Editor *e, int rows)
{
    unsigned long sum = 0;

    ed_scroll_to_cursor(e, rows);

    for (int r = 0; r < rows && e->top + r < e->view.n; r++) {
        Cell cells[400];

        view_line_cells(e, e->top + r, cells);

        for (int i = 0; i < 80; i++) {
            sum = sum * 31 + cells[i].ch;
        }
    }

    return sum;
}


/* The item of the last section whose number is a first-level one. */
static int
last_section_item(const Doc *d)
{
    for (int i = d->nitems - 1; i > 0; i--) {
        if (d->items[i].type == IT_BLOCK && d->items[i - 1].type == IT_SECT &&
            is_edit_block(d, i) != 0) {
            return i;
        }
    }

    return -1;
}


int
main(int argc, char **argv)
{
    char err[160] = "";
    char first[64];
    char last[64];
    const char *path;
    const char *needle;
    char out1[512];
    char out2[512];
    long t0;
    Doc *d;
    Editor *e;
    unsigned long sum = 0;
    int mid;
    int lastitem;
    int nitems;
    int count;
    size_t n1 = 0;
    size_t n2 = 0;
    size_t n0 = 0;
    uint8_t *b0;
    uint8_t *b1;
    uint8_t *b2;

    if (argc < 3) {
        fprintf(stderr, "usage: bigtest DOC NEEDLE [LIMIT_MS]\n");

        return 2;
    }

    path = argv[1];
    needle = argv[2];

    if (argc > 3) {
        limit_ms = atol(argv[3]);
    }

    t0 = now();
    d = doc_load(path, err, sizeof err);

    if (d == NULL) {
        fprintf(stderr, "load: %s\n", err);

        return 1;
    }

    step("load", t0);
    e = ed_new(d);
    e->text_width = 80;
    t0 = now();
    ed_layout(e);
    step("first layout", t0);
    printf("items %d, lines %d\n", d->nitems, e->view.n);
    check(e->view.n > 40000, "the layout has the lines of 700 pages");

    /* Scrolling: page down through the whole document. */
    t0 = now();

    for (int i = 0; i < e->view.n / 20 + 10; i++) {
        long t1 = now();

        ed_move(e, MV_PGDN, 24);
        ed_layout(e);
        sum += screen_sum(e, 24);

        if (now() - t1 > limit_ms) {
            printf("FAIL one page down took %ld ms\n", now() - t1);
            fails++;
            break;
        }
    }

    printf("scroll sum %lu\n", sum);
    step("page down to the end (all)", t0);
    t0 = now();

    for (int i = 0; i < 200; i++) {
        ed_move(e, MV_PGUP, 24);
        ed_layout(e);
        sum += screen_sum(e, 24);
    }

    step("200 pages up", t0);
    t0 = now();
    ed_move(e, MV_DOC_END, 24);
    ed_layout(e);
    sum += screen_sum(e, 24);
    ed_move(e, MV_DOC_START, 24);
    ed_layout(e);
    sum += screen_sum(e, 24);
    step("document end and start", t0);

    /* Search from the start to the end. */
    t0 = now();
    check(ed_search(e, needle, 1, 0) != 0, "search finds the needle");
    step("search from start to end", t0);
    lastitem = last_section_item(d);
    printf("the needle is at item %d of %d\n", e->cur.item, d->nitems);

    if (getenv("BIGTEST_NEEDLE_AFTER") != NULL) {
        /* The percent of the items that must come before the needle. */
        int pct = atoi(getenv("BIGTEST_NEEDLE_AFTER"));

        check(e->cur.item * 100 > d->nitems * pct,
              "the needle is near the end");
    }

    ed_move(e, MV_DOC_START, 24);
    t0 = now();
    check(ed_search(e, "zzqqxxnotthere", 1, 0) == 0, "a miss is a miss");
    step("search that finds nothing", t0);
    t0 = now();
    ed_layout(e);
    sum += screen_sum(e, 24);
    step("layout after the search", t0);

    /* Replace all. */
    t0 = now();
    count = ed_replace_all(e, "the", "THE", 0);
    step("replace all (the -> THE)", t0);
    printf("replaced %d\n", count);
    check(count > 1000, "replace all replaced many");
    t0 = now();
    ed_layout(e);
    step("layout after replace all", t0);
    t0 = now();
    check(ed_replace_all(e, "THE", "the", 0) >= count, "replace back");
    ed_layout(e);
    step("replace all back and layout", t0);

    /* Go To section: the loop of the Go To command. */
    sect_number(d, 1, first, sizeof first);

    if (lastitem > 0) {
        sect_number(d, lastitem - 1, last, sizeof last);
    } else {
        strcpy(last, "1");
    }


    /* Go To section finds what the status line numbers (a sample). */
    for (int i = 1; i + 1 < d->nitems; i += 97) {
        char num[64];

        if (d->items[i].type == IT_SECT && is_edit_block(d, i + 1) != 0) {
            sect_number(d, i, num, sizeof num);
            check(sect_find(d, num) == i + 1, "sect_find agrees with "
                  "sect_number");
        }
    }

    t0 = now();
    check(doc_find_section(d, last) > 0, "the last section is found");
    step("go to the last section", t0);
    printf("section %s is item %d\n", last, doc_find_section(d, last));
    ed_goto(e, doc_find_section(d, last), 0, 0);
    ed_layout(e);
    sum += screen_sum(e, 24);

    /* Outline: collapse and expand. */
    t0 = now();
    ed_fold(e, 4, 0);
    ed_layout(e);
    step("outline: collapse the document", t0);
    printf("lines when collapsed %d\n", e->view.n);
    t0 = now();
    ed_fold(e, 3, 1);
    ed_layout(e);
    step("outline: show level 1", t0);
    t0 = now();
    ed_fold(e, 3, 3);
    ed_layout(e);
    step("outline: show level 3", t0);
    t0 = now();
    ed_fold(e, 1, 0);
    ed_layout(e);
    step("outline: expand a section", t0);

    /* Autosort of the subsections of a chapter, both ways. */
    ed_goto(e, doc_find_section(d, "3"), 0, 0);
    ed_layout(e);
    nitems = d->nitems;
    t0 = now();
    check(ed_sort_sections(e, 0) > 1, "sort the sections of chapter 3");
    ed_layout(e);
    step("sort sections ascending", t0);
    t0 = now();
    check(ed_sort_sections(e, 1) > 1, "sort the sections back, descending");
    ed_layout(e);
    step("sort sections descending", t0);
    check(d->nitems == nitems, "sorting keeps every item");

    /* Insert and delete in the middle. */
    mid = d->nitems / 2;

    /* A paragraph: a block of one column with a good deal of text. */
    while (mid < d->nitems &&
           (is_edit_block(d, mid) == 0 || d->items[mid].blk->ncols != 1 ||
            d->items[mid].blk->cols[0].text.n < 200)) {
        mid++;
    }

    ed_goto(e, mid, 0, 0);
    ed_layout(e);
    t0 = now();

    for (int i = 0; i < 40; i++) {
        long t1 = now();

        ed_insert_char(e, u8('x'));
        ed_layout(e);
        sum += screen_sum(e, 24);

        if (now() - t1 > limit_ms) {
            printf("FAIL one typed character took %ld ms\n", now() - t1);
            fails++;
            break;
        }
    }

    steps("type 40 characters (layout each)", t0, 40);
    t0 = now();

    for (int i = 0; i < 40; i++) {
        ed_backspace(e);
        ed_layout(e);
        sum += screen_sum(e, 24);
    }

    steps("delete 40 characters", t0, 40);
    t0 = now();
    ed_split(e, 1);
    ed_layout(e);
    step("split a block", t0);
    t0 = now();
    ed_goto(e, mid, 0, 0);
    check(ed_join(e, 1) == 0, "join the blocks again");
    ed_layout(e);
    step("join the blocks", t0);
    t0 = now();
    ed_append_section(e, 0);
    ed_layout(e);
    step("append a section", t0);

    /* Save and reload; a second save must give the same bytes. */
    d->modified = 1;
    t0 = now();
    tmp_path(out1, sizeof out1, "bigtest-1.doc");
    tmp_path(out2, sizeof out2, "bigtest-2.doc");
    check(doc_save(d, out1, err, sizeof err) == 0, "save");
    step("save", t0);
    ed_free(e);
    doc_free(d);
    t0 = now();
    d = doc_load(out1, err, sizeof err);
    check(d != NULL, "reload");
    step("reload", t0);

    if (d == NULL) {
        return 1;
    }

    t0 = now();
    check(doc_save(d, out2, err, sizeof err) == 0, "second save");
    step("second save", t0);
    b1 = slurp(out1, &n1);
    b2 = slurp(out2, &n2);
    check(b1 != NULL && b2 != NULL && n1 == n2 && memcmp(b1, b2, n1) == 0,
          "the second save has the same bytes");
    b0 = slurp(path, &n0);
    printf("sizes: original %zu, first save %zu, second save %zu\n", n0, n1,
           n2);
    free(b0);
    free(b1);
    free(b2);
    remove(out1);
    remove(out2);
    doc_free(d);
    printf("peak memory %ld KB\n", peak_kb());

    if (fails != 0) {
        printf("bigtest: %d FAILED\n", fails);

        return 1;
    }

    printf("bigtest: ok\n");

    return 0;
}
