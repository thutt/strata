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
 * memtest.c - proves that the compact in-memory form of a large document
 * changes nothing that can be seen.  It runs a fixed series of operations
 * on a big document (twain.doc) and, after each, takes a checksum of the
 * saved bytes and of every cell of every line of the view.  The expected
 * checksums were taken from the program before the memory was reduced.
 * Run with a second argument to print the checksums in the form of the file
 * memtest.expect instead of comparing them.
 * The editor has no undo and no page numbers, so these operations stand in
 * for them: edits at the start, in the middle and at the end, search,
 * copy and move of blocks, markers, going to places, and a new layout of
 * the whole document (which is what repaginating does).
 */
#include "../src/util.h"
#include "../src/editor.h"
#include "../src/marker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FNV_BASIS 1469598103934665603UL
#define FNV_PRIME 1099511628211UL

static const uint8_t mk1[] = {'1', '1', '2', '\r', '\n'};
static const uint8_t mk2[] = {'1', '1', '3', '\r', '\n'};
static int fails = 0;
static int print_only = 0;


static unsigned long
mix(unsigned long h, unsigned long v)
{
    return (h ^ v) * FNV_PRIME;
}


/* A checksum of everything the screen would show, for every view line. */
static unsigned long
view_sum(Editor *e)
{
    unsigned long h = FNV_BASIS;
    Cell *cells;

    ed_layout(e);
    cells = malloc(usz(e->view.width) * sizeof *cells);
    h = mix(h, uns(e->view.n));

    for (int i = 0; i < e->view.n; i++) {
        h = mix(h, uns(e->view.lines[i].kind));
        h = mix(h, uns(e->view.lines[i].item));
        h = mix(h, uns(e->view.lines[i].row));
        view_line_cells(e, i, cells);

        for (int k = 0; k < e->view.width; k++) {
            h = mix(h, cells[k].ch);
            h = mix(h, cells[k].attr);
            h = mix(h, uns(cells[k].tok + 1));
        }
    }

    free(cells);

    return h;
}


/* A checksum of the file that the document would be saved as. */
static unsigned long
file_sum(Doc *d)
{
    size_t len = 0;
    uint8_t *b = doc_serialize(d, &len);
    unsigned long h = mix(FNV_BASIS, len);

    for (size_t i = 0; i < len; i++) {
        h = mix(h, b[i]);
    }

    free(b);

    return h;
}


static unsigned long
cursor_sum(const Editor *e)
{
    unsigned long h = mix(FNV_BASIS, uns(e->cur.item));

    h = mix(h, uns(e->cur.col));

    return mix(h, uns(e->cur.tok));
}


/* Compare the three checksums of a step with the expected ones. */
static void
step(Editor *e, const char *name, const unsigned long *want)
{
    unsigned long f = file_sum(e->doc);
    unsigned long v = view_sum(e);
    unsigned long c = cursor_sum(e);

    if (print_only != 0) {
        printf("    {\"%s\",\n     {0x%lxUL, 0x%lxUL, 0x%lxUL}},\n", name, f, v,
               c);

        return;
    }

    if (f != want[0] || v != want[1] || c != want[2]) {
        printf("FAIL %s: file %lx (want %lx), view %lx (want %lx), "
               "cursor %lx (want %lx)\n",
               name, f, want[0], v, want[1], c, want[2]);
        fails++;
    } else {
        printf("ok   %s\n", name);
    }
}


static void
type(Editor *e, const char *s)
{
    for (; *s != '\0'; s++) {
        ed_insert_char(e, u8(*s));
    }
}


/* Put the cursor at the start of the text of the n-th block. */
static int
goto_block(Editor *e, int n)
{
    const Doc *d = e->doc;
    int seen = 0;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == IT_BLOCK && d->items[i].blk->ncols > 0 &&
            seen++ == n) {
            e->cur.item = i;
            e->cur.col = 0;
            e->cur.tok = 0;
            ed_fix_cursor(e);

            return i;
        }
    }

    return -1;
}


typedef struct {
    const char *name;
    unsigned long want[3];
} Expect;


/* The expected checksums, in the order of the steps. */
static const Expect expect[] = {
#include "memtest.expect"
};


int
main(int argc, char **argv)
{
    char err[128] = "";
    int s = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: memtest twain.doc [print]\n");

        return 2;
    }

    print_only = argc > 2 ? 1 : 0;

    Doc *d = doc_load(argv[1], err, sizeof err);

    if (d == NULL) {
        printf("FAIL load: %s\n", err);

        return 1;
    }

    /* The saved bytes are exactly the bytes of the file. */
    {
        FILE *f = fopen(argv[1], "rb");
        size_t len = 0;
        uint8_t *out = doc_serialize(d, &len);
        uint8_t *orig = malloc(len + 1);

        if (f == NULL || fread(orig, 1, len + 1, f) != len ||
            memcmp(orig, out, len) != 0) {
            printf("FAIL round trip: the saved bytes differ from the file\n");
            fails++;
        } else {
            printf("ok   round trip, %zu bytes, byte for byte\n", len);
        }

        if (f != NULL) {
            fclose(f);
        }

        free(orig);
        free(out);
    }

    Editor *e = ed_new(d);

    e->text_width = 80;
#define STEP(name) step(e, name, expect[s++].want)
    STEP("layout");

    /* Repaginating: the whole document is laid out again. */
    ed_invalidate(e);
    STEP("layout again");

    /* An edit at the start. */
    ed_move(e, MV_DOC_START, 24);
    type(e, "START ");
    STEP("edit at start");
    ed_backspace(e);
    ed_delete(e);
    STEP("backspace and delete at start");

    /* An edit in the middle, then a new line. */
    goto_block(e, 20000);
    ed_move(e, MV_RIGHT, 24);
    ed_move(e, MV_RIGHT, 24);
    type(e, "MIDDLE edit, with a long run of text that has to wrap onto "
            "another row of the page when it is laid out");
    STEP("edit in the middle");
    ed_toggle_attr(e, ATTR_BOLD);
    type(e, "bold");
    ed_toggle_attr(e, ATTR_BOLD);
    STEP("attribute in the middle");
    ed_insert_nl(e);
    ed_insert_tab(e, ' ');
    type(e, "after tab");
    STEP("new line and tab in the middle");

    /* A marker, which has data of its own. */
    ed_insert_marker(e, 17, mk1, sizeof mk1);
    STEP("marker inserted");
    ed_move(e, MV_LEFT, 24);
    ed_replace_marker(e, e->cur.tok, mk2, sizeof mk2);
    STEP("marker replaced");

    /* An edit at the end. */
    ed_move(e, MV_DOC_END, 24);
    type(e, " END");
    STEP("edit at end");
    ed_insert_nl(e);
    type(e, "last line");
    STEP("new line at end");

    /* Search. */
    ed_move(e, MV_DOC_START, 24);

    for (int i = 0; i < 5; i++) {
        int found = ed_search(e, "Mark Twain", 1, 1);

        if (print_only == 0 && found != 1) {
            printf("FAIL search %d found nothing\n", i);
            fails++;
        }
    }

    STEP("search forward five times");
    ed_search(e, "Innocents", 0, 1);
    STEP("search backward");

    /* Block copy and paste, block move (cut and paste), marker blocks. */
    goto_block(e, 300);
    ed_sel_begin(e, SEL_BLOCK);
    ed_move(e, MV_DOWN, 24);
    ed_move(e, MV_DOWN, 24);
    ed_sel_apply(e, OP_COPY);
    goto_block(e, 30000);
    ed_paste(e);
    STEP("block copy and paste");
    goto_block(e, 20000);
    ed_sel_begin(e, SEL_BLOCK);
    ed_move(e, MV_DOWN, 24);
    ed_sel_apply(e, OP_CUT);
    goto_block(e, 5);
    ed_paste(e);
    STEP("block move");
    goto_block(e, 100);
    ed_sel_begin(e, SEL_SECTION);
    ed_sel_apply(e, OP_CUT);
    ed_paste(e);
    ed_paste(e);
    STEP("section cut and pasted twice");
    goto_block(e, 700);
    ed_sel_begin(e, SEL_BLOCK);
    ed_sel_apply(e, OP_DELETE);
    STEP("block delete");

    /* Replace, and going to places. */
    ed_replace_all(e, "Twain", "TWAIN", 0);
    STEP("replace all");
    ed_move(e, MV_DOC_START, 24);
    ed_move(e, MV_SECT_NEXT_HEAD, 24);
    ed_move(e, MV_SECT_NEXT_HEAD, 24);
    ed_move(e, MV_PGDN, 24);
    ed_move(e, MV_PGDN, 24);
    STEP("go to section and page down");
    ed_move(e, MV_DOC_END, 24);
    ed_move(e, MV_PGUP, 24);
    STEP("go to end and page up");
    ed_fold(e, 3, 1);
    STEP("outline level one");
    ed_fold(e, 4, 0);
    STEP("whole document");

    /* A new layout of everything gives the same screen. */
    {
        unsigned long before = view_sum(e);

        ed_invalidate(e);

        if (view_sum(e) != before) {
            printf("FAIL repaginate: the view changed\n");
            fails++;
        } else {
            printf("ok   repaginate keeps the view\n");
        }
    }

    ed_free(e);
    doc_free(d);

    if (print_only == 0 && usz(s) != sizeof expect / sizeof expect[0]) {
        printf("FAIL %d steps run, %zu expected\n", s,
               sizeof expect / sizeof expect[0]);
        fails++;
    }

    printf("memtest: %d failures\n", fails);

    return fails != 0 ? 1 : 0;
}
