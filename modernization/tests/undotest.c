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
 * undotest.c - the unit tests of Undo and Redo (src/undo.c).
 *
 *     undotest                 the tests on small documents
 *     undotest --measure FILE  the memory and the time on a large document
 *
 * Every test makes an operation as the Editor does (the hooks of
 * undo_before_key and undo_after_key around it), undoes it, and compares the
 * bytes of the saved document with the bytes before the operation; then it
 * redoes it and compares them with the bytes after it.
 */
#include "../src/util.h"
#include "../src/editor.h"
#include "../src/undo.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails = 0;
static int checks = 0;

#define CHECK(c, ...)                                                          \
    do {                                                                       \
        checks++;                                                              \
        if (!(c)) {                                                            \
            printf("FAIL line %d: ", __LINE__);                                \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
            fails++;                                                           \
        }                                                                      \
    } while (0)

/* An operation with the hooks that the Editor puts around a key. */
#define OP(e, cls, ch, label, stmt)                                            \
    do {                                                                       \
        UndoKey uk_;                                                           \
                                                                               \
        undo_before_key((e), &uk_, (cls), (ch));                               \
        stmt;                                                                  \
        undo_after_key((e), &uk_, (label));                                    \
    } while (0)


typedef struct {
    uint8_t *b;
    size_t n;
} Snap;


static Snap
snap(Editor *e)
{
    Snap s;

    s.b = doc_serialize(e->doc, &s.n);

    return s;
}


static int
snap_eq(const Snap *a, const Snap *b)
{
    return a->n == b->n && memcmp(a->b, b->b, a->n) == 0;
}


static void
snap_free(Snap *s)
{
    free(s->b);
    s->b = NULL;
}


static void
type_text(Editor *e, const char *s)
{
    for (; *s != '\0'; s++) {
        OP(e, 1, *s, "Typing", ed_insert_char(e, u8(*s)));
    }
}


static void
put_cursor(Editor *e, int item, int col, int tok)
{
    e->cur.item = item;
    e->cur.col = col;
    e->cur.tok = tok;
    ed_fix_cursor(e);
}


/*
 * Make the operation, check that Undo restores the document exactly and that
 * Redo makes it again.  The operation has run when this is called, with the
 * snapshot before it in before.
 */
static void
round_trip(Editor *e, const Snap *before, const char *what)
{
    Snap after = snap(e);
    Snap now;

    if (snap_eq(&after, before) != 0) {
        printf("note: %s changed nothing here\n", what);
        snap_free(&after);

        return;
    }

    CHECK(undo_verify(e) == 0, "%s: the shadow is the document", what);
    CHECK(undo_undo(e) == 0, "%s: undo is possible", what);
    now = snap(e);
    CHECK(snap_eq(&now, before), "%s: undo gives the document back", what);
    CHECK(undo_verify(e) == 0, "%s: the shadow follows undo", what);
    snap_free(&now);
    CHECK(undo_redo(e) == 0, "%s: redo is possible", what);
    now = snap(e);
    CHECK(snap_eq(&now, &after), "%s: redo makes the operation again", what);
    CHECK(undo_verify(e) == 0, "%s: the shadow follows redo", what);
    snap_free(&now);
    snap_free(&after);
}


static Editor *
new_editor(void)
{
    Editor *e = ed_new(doc_new(1));

    undo_set_levels(UNDO_DEFAULT_LEVELS);

    return e;
}


static void
end_editor(Editor *e)
{
    Doc *d = e->doc;

    ed_free(e);
    doc_free(d);
}


/* Typing is grouped by words; Undo takes a word and its space at a time. */
static void
test_typing(void)
{
    Editor *e = new_editor();
    Snap empty = snap(e);
    Snap full;
    Snap now;
    int nu;
    int nr;

    type_text(e, "hello world foo");
    full = snap(e);
    undo_counts(e, &nu, &nr);
    CHECK(nu == 3, "typing: three words are three operations (%d)", nu);
    CHECK(undo_undo(e) == 0, "typing: first undo");
    now = snap(e);
    CHECK(snap_eq(&now, &full) == 0, "typing: the last word is gone");
    snap_free(&now);
    CHECK(undo_undo(e) == 0, "typing: second undo");
    CHECK(undo_undo(e) == 0, "typing: third undo");
    now = snap(e);
    CHECK(snap_eq(&now, &empty), "typing: back to the empty document");
    snap_free(&now);
    CHECK(undo_undo(e) != 0, "typing: nothing more to undo");
    CHECK(undo_redo(e) == 0, "typing: redo");
    CHECK(undo_redo(e) == 0, "typing: redo again");
    CHECK(undo_redo(e) == 0, "typing: redo a third time");
    now = snap(e);
    CHECK(snap_eq(&now, &full), "typing: redo gives the typed text");
    snap_free(&now);
    CHECK(undo_redo(e) != 0, "typing: nothing more to redo");

    /* A run of deletes is grouped too, and a move of the cursor breaks it. */
    {
        Snap before = snap(e);

        OP(e, 2, 0, "Deleting", ed_backspace(e));
        OP(e, 2, 0, "Deleting", ed_backspace(e));
        OP(e, 0, 0, "Move", ed_move(e, MV_LEFT, 20));
        OP(e, 2, 0, "Deleting", ed_backspace(e));
        undo_counts(e, &nu, &nr);
        CHECK(nu == 5, "typing: two runs of backspaces (%d)", nu);
        CHECK(undo_undo(e) == 0, "typing: undo the second run");
        CHECK(undo_undo(e) == 0, "typing: undo the first run");
        now = snap(e);
        CHECK(snap_eq(&now, &before), "typing: both runs are undone");
        snap_free(&now);
        snap_free(&before);
    }

    /* A new edit takes the redo operations away. */
    OP(e, 1, 'x', "Typing", ed_insert_char(e, 'x'));
    undo_counts(e, &nu, &nr);
    CHECK(nr == 0, "redo: a new edit clears the redo operations (%d)", nr);
    snap_free(&empty);
    snap_free(&full);
    end_editor(e);
}


/* Every command of the structure makes one operation that round trips. */
static void
test_commands(void)
{
    Editor *e = new_editor();
    Snap s;

    type_text(e, "one");
    OP(e, 0, 13, "New line", ed_insert_nl(e));
    type_text(e, "two");
    OP(e, 0, 13, "New line", ed_insert_nl(e));
    type_text(e, "three cats and cats");

    s = snap(e);
    OP(e, 0, 0, "Replace", ed_replace_all(e, "cats", "dogs", 1));
    round_trip(e, &s, "replace all");
    snap_free(&s);

    put_cursor(e, 0, 0, 0);
    s = snap(e);
    OP(e, 0, 0, "Split", ed_split(e, 1));
    round_trip(e, &s, "split a block");
    snap_free(&s);

    put_cursor(e, 0, 0, 0);
    s = snap(e);
    OP(e, 0, 0, "Join", ed_join(e, 1));
    round_trip(e, &s, "join blocks");
    snap_free(&s);

    s = snap(e);
    OP(e, 0, 0, "Append", ed_append_section(e, 0));
    round_trip(e, &s, "append a section");
    snap_free(&s);

    put_cursor(e, 0, 0, 0);
    s = snap(e);
    OP(e, 0, 0, "Format", ed_toggle_attr(e, 0x01));
    round_trip(e, &s, "an attribute");
    snap_free(&s);

    s = snap(e);
    OP(e, 0, 0, "Marker", ed_insert_marker(e, 1, (const uint8_t *)"a", 1));
    round_trip(e, &s, "a marker");
    snap_free(&s);

    s = snap(e);
    OP(e, 0, 0, "Structure", ed_set_structured(e, 0));
    round_trip(e, &s, "structured to unstructured");
    snap_free(&s);

    /* Cut and Paste through the clipboard. */
    put_cursor(e, 0, 0, 0);
    ed_sel_begin(e, SEL_BLOCK);
    s = snap(e);
    OP(e, 0, 13, "Cut", ed_sel_apply(e, OP_CUT));
    round_trip(e, &s, "cut a block");
    snap_free(&s);
    s = snap(e);
    OP(e, 0, 0, "Paste", ed_paste(e));
    round_trip(e, &s, "paste");
    snap_free(&s);
    end_editor(e);
}


/* The limit: the oldest drops; a setting of 0 keeps nothing. */
static void
test_limits(void)
{
    Editor *e = new_editor();
    int nu;
    int nr;

    undo_set_levels(3);

    for (int i = 0; i < 6; i++) {
        OP(e, 0, 13, "New line", ed_insert_nl(e));
    }

    undo_counts(e, &nu, &nr);
    CHECK(nu == 3, "limit: three levels keep three operations (%d)", nu);

    for (int i = 0; i < 3; i++) {
        CHECK(undo_undo(e) == 0, "limit: undo %d", i);
    }

    CHECK(undo_undo(e) != 0, "limit: the fourth undo has nothing");
    end_editor(e);

    e = new_editor();
    undo_set_levels(0);
    OP(e, 0, 13, "New line", ed_insert_nl(e));
    undo_counts(e, &nu, &nr);
    CHECK(nu == 0 && e->undo == NULL, "off: nothing is kept (%d)", nu);
    CHECK(undo_undo(e) != 0, "off: undo says so");
    CHECK(undo_clamp_levels(-4) == 0 && undo_clamp_levels(9999) == 500,
          "the limits of the setting");
    undo_set_levels(UNDO_DEFAULT_LEVELS);
    end_editor(e);
}


/*
 * The shadow is a copy of the whole document.  It keeps the text of a column
 * that holds no marker at about one byte for a character, and not the four
 * bytes of a token: for a document of 3000 blocks of 400 characters the
 * shadow must be less than twice the characters (it was more than four
 * times).  A column with a marker keeps its tokens, and Undo of an edit in a
 * packed block and in a block with a marker gives the document back.
 */
static void
test_shadow_memory(void)
{
    enum { BLOCKS = 3000, WIDTH = 400 };
    size_t cap = (size_t)BLOCKS * (WIDTH + 16) + 256;
    char *buf = malloc(cap);
    size_t n = 0;
    char err[160];
    Doc *d;
    Editor *e;
    Snap before;
    size_t chars = (size_t)BLOCKS * WIDTH;

    n += (size_t)snprintf(buf + n, cap - n,
                          "*R2\r\nMR1001\r\nPP\\1\\%c\r\n*B0\r\nS\r\n", 0);

    for (int i = 0; i < BLOCKS; i++) {
        n += (size_t)snprintf(buf + n, cap - n, "%sT", i != 0 ? "B\r\n" : "");

        for (int k = 0; k < WIDTH; k++) {
            buf[n++] = (char)('a' + (i * 7 + k * 3) % 26);
        }

        n += (size_t)snprintf(buf + n, cap - n, "%c%c\r\n", 0x1d, 0);
    }

    n += (size_t)snprintf(buf + n, cap - n, "*E\r\n");
    d = doc_load_mem((const uint8_t *)buf, n, err, sizeof err);
    free(buf);
    CHECK(d != NULL, "shadow memory: the document loads (%s)", err);

    if (d == NULL) {
        return;
    }

    e = ed_new(d);
    undo_set_levels(UNDO_DEFAULT_LEVELS);
    put_cursor(e, 1500, 3, 0);
    before = snap(e);
    OP(e, 1, 'Q', "Typing", ed_insert_char(e, u8('Q')));
    CHECK(undo_shadow_bytes(e) > chars / 2,
          "shadow memory: the shadow holds the text (%zu bytes)",
          undo_shadow_bytes(e));
    CHECK(undo_shadow_bytes(e) < 2 * chars,
          "shadow memory: less than two bytes for a character: %zu bytes "
          "for %zu characters",
          undo_shadow_bytes(e), chars);
    CHECK(undo_verify(e) == 0, "shadow memory: the shadow is the document");

    {
        /* The buffer of a save has the size of the file, and a copy of a
           block has the room of its text and no more. */
        Snap s = snap(e);
        Block *c = block_clone_full(d->items[1500].blk);

        size_t runs = doc_serialize_runs;
        size_t blen;
        uint8_t *again = doc_serialize(d, &blen);

        /* The buffer is cut to the file with realloc: a large block is cut
           in pages, so up to a page and a half of the file is left over,
           where a buffer that grew by doubling holds about twice the file. */
        CHECK(malloc_usable_size(s.b) <= s.n + 8192,
              "save buffer: the size of the document, no more (%zu for %zu)",
              malloc_usable_size(s.b), s.n);
        CHECK(doc_serialize_runs == runs + 1,
              "save buffer: one run over the document (%zu runs)",
              doc_serialize_runs - runs);
        CHECK(blen == s.n && memcmp(again, s.b, blen) == 0,
              "save buffer: the same bytes as the snapshot");
        free(again);
        CHECK(c->cols[0].text.cap == c->cols[0].text.n,
              "block copy: no free room in the text (%d for %d)",
              c->cols[0].text.cap, c->cols[0].text.n);
        block_free(c);
        snap_free(&s);
    }

    CHECK(undo_undo(e) == 0, "shadow memory: undo of typing in a packed block");

    {
        Snap now = snap(e);

        CHECK(snap_eq(&now, &before), "shadow memory: undo gives the bytes back");
        snap_free(&now);
    }

    CHECK(undo_verify(e) == 0, "shadow memory: the shadow follows undo");
    OP(e, 0, 0, "Replace", ed_replace_all(e, "adg", "ADG", 1));
    CHECK(undo_verify(e) == 0, "shadow memory: Replace All, the shadow is the "
                               "document");
    CHECK(undo_undo(e) == 0, "shadow memory: undo of Replace All");

    {
        Snap now = snap(e);

        CHECK(snap_eq(&now, &before), "shadow memory: undo of Replace All "
                                      "gives the bytes back");
        snap_free(&now);
    }

    snap_free(&before);
    end_editor(e);
}


/* Undoing back to the saved state makes the document unchanged. */
static void
test_changed_flag(void)
{
    Editor *e = new_editor();

    type_text(e, "abc ");
    e->doc->modified = 0; /* A save. */
    OP(e, 0, 0, "Move", ed_move(e, MV_LEFT, 20));
    CHECK(e->doc->modified == 0, "flag: a move does not change the document");
    type_text(e, "def");
    CHECK(e->doc->modified != 0, "flag: typing changes the document");
    CHECK(undo_undo(e) == 0, "flag: undo");
    CHECK(e->doc->modified == 0, "flag: back to the saved state");
    CHECK(undo_redo(e) == 0, "flag: redo");
    CHECK(e->doc->modified != 0, "flag: redo changes it again");
    end_editor(e);
}


/* Random operations, undos and redos against the states that were saved. */
static void
test_fuzz(void)
{
    enum {
        STEPS = 400,
        MAXSNAP = 600
    };
    Editor *e = new_editor();
    Snap *states = calloc(MAXSNAP, sizeof *states);

    undo_set_levels(UNDO_MAX_LEVELS);
    int pos = 0;
    int top = 0;

    srand(12345);
    states[0] = snap(e);

    for (int i = 0; i < STEPS; i++) {
        int r = rand() % 12;

        if (r < 6) {
            int c = 'a' + rand() % 26;

            OP(e, 1, c, "Typing", ed_insert_char(e, u8(c)));
        } else if (r == 6) {
            OP(e, 0, 13, "New line", ed_insert_nl(e));
        } else if (r == 7) {
            OP(e, 2, 0, "Deleting", ed_backspace(e));
        } else if (r == 8) {
            OP(e, 0, 0, "Move", ed_move(e, rand() % 2 != 0 ? MV_UP : MV_DOWN,
                                        20));
        } else if (r == 9) {
            OP(e, 0, 0, "Replace", ed_replace_all(e, "a", "bb", 1));
        } else if (r == 10) {
            if (undo_undo(e) == 0) {
                pos--;
                Snap now = snap(e);

                CHECK(snap_eq(&now, &states[pos]),
                      "fuzz: undo to state %d at step %d", pos, i);
                snap_free(&now);
            }

            continue;
        } else {
            if (undo_redo(e) == 0) {
                pos++;
                Snap now = snap(e);

                CHECK(snap_eq(&now, &states[pos]),
                      "fuzz: redo to state %d at step %d", pos, i);
                snap_free(&now);
            }

            continue;
        }

        {
            int nu;
            int nr;
            Snap now = snap(e);

            undo_counts(e, &nu, &nr);

            if (snap_eq(&now, &states[pos]) != 0) {
                snap_free(&now);
            } else if (nr == 0 && nu >= 1 && nu <= MAXSNAP - 2) {
                /* The operation made a state (or joined a run of typing). */
                if (nu == pos + 1) {
                    for (int k = pos + 1; k <= top; k++) {
                        snap_free(&states[k]);
                    }

                    pos++;
                    top = pos;
                    states[pos] = now;
                } else if (nu == pos) {
                    snap_free(&states[pos]);
                    states[pos] = now;
                } else {
                    snap_free(&now);
                    pos = nu;
                }
            } else {
                snap_free(&now);
            }
        }
    }

    CHECK(undo_verify(e) == 0, "fuzz: the shadow is the document");

    for (int k = 0; k < MAXSNAP; k++) {
        snap_free(&states[k]);
    }

    free(states);
    end_editor(e);
}


/* A change between two keys is an operation of its own. */
static void
test_external(void)
{
    Editor *e = new_editor();
    Snap before;
    Snap now;
    int nu;
    int nr;

    type_text(e, "abc");
    before = snap(e);
    ed_insert_nl(e); /* Made with no key, as the markers of a Preview are. */
    OP(e, 1, 'x', "Typing", ed_insert_char(e, 'x'));
    undo_counts(e, &nu, &nr);
    CHECK(nu == 3, "external: typing, the change and typing (%d)", nu);
    CHECK(undo_undo(e) == 0, "external: undo the typing");
    CHECK(undo_undo(e) == 0, "external: undo the change");
    now = snap(e);
    CHECK(snap_eq(&now, &before), "external: the change is undone by itself");
    snap_free(&now);
    snap_free(&before);
    CHECK(undo_verify(e) == 0, "external: the shadow is the document");
    end_editor(e);
}


/* A Replace All of a few words in a large document keeps few items. */
static void
test_hunks(void)
{
    Editor *e = new_editor();
    Snap s;
    size_t bytes;
    int nu;
    int nr;

    for (int i = 0; i < 300; i++) {
        type_text(e, i == 5 || i == 250 ? "needle in hay" : "plain hay hay");
        OP(e, 0, 13, "New line", ed_insert_nl(e));
    }

    undo_set_levels(UNDO_DEFAULT_LEVELS);
    undo_reset(e);
    OP(e, 0, 0, "Move", ed_move(e, MV_DOC_START, 20));
    s = snap(e);
    OP(e, 0, 0, "Replace", ed_replace_all(e, "needle", "pin", 1));
    bytes = undo_history_bytes(e);
    undo_counts(e, &nu, &nr);
    CHECK(nu == 1, "hunks: Replace All is one operation (%d)", nu);
    CHECK(bytes > 0 && bytes < 40000, "hunks: two stretches only (%zu)",
          bytes);
    round_trip(e, &s, "replace all in two places");
    snap_free(&s);
    end_editor(e);
}


/* The memory and the time on a large document
   ------------------------------------------------------------------------ */
static double
now_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);

    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1.0e6;
}


static void
measure(const char *path)
{
    char err[160];
    Doc *d = doc_load(path, err, sizeof err);
    Editor *e;
    double t;
    int nu;
    int nr;

    if (d == NULL) {
        printf("FAIL cannot load %s: %s\n", path, err);
        fails++;

        return;
    }

    e = ed_new(d);
    printf("document: %d items\n", d->nitems);

    for (int pass = 0; pass < 2; pass++) {
        undo_set_levels(pass == 0 ? 0 : UNDO_DEFAULT_LEVELS);
        undo_reset(e);
        put_cursor(e, d->nitems / 2, 0, 0);
        t = now_ms();

        for (int i = 0; i < 200; i++) {
            int c = 'a' + i % 26;

            OP(e, 1, c, "Typing", ed_insert_char(e, u8(c)));
        }

        printf("levels %3d: typing 200 characters %.1f ms (%.2f ms each)\n",
               undo_get_levels(), now_ms() - t, (now_ms() - t) / 200.0);

        if (pass == 1) {
            t = now_ms();
            OP(e, 0, 0, "Replace", ed_replace_all(e, "the", "THE", 1));
            printf("levels %3d: Replace All %.1f ms\n", undo_get_levels(),
                   now_ms() - t);
            undo_counts(e, &nu, &nr);
            printf("operations %d, shadow %zu bytes, history %zu bytes\n", nu,
                   undo_shadow_bytes(e), undo_history_bytes(e));
            t = now_ms();
            CHECK(undo_undo(e) == 0, "measure: undo of Replace All");
            printf("undo of Replace All %.1f ms\n", now_ms() - t);
            t = now_ms();
            CHECK(undo_redo(e) == 0, "measure: redo of Replace All");
            printf("redo of Replace All %.1f ms\n", now_ms() - t);
            CHECK(undo_verify(e) == 0, "measure: the shadow is the document");
            undo_reset(e);
            OP(e, 0, 0, "Move", ed_move(e, MV_DOC_START, 20));
            t = now_ms();
            OP(e, 0, 0, "Replace", ed_replace_all(e, "Autosort", "Autosor", 1));
            printf("sparse Replace All %.1f ms, history %zu bytes\n",
                   now_ms() - t, undo_history_bytes(e));
        }
    }

    ed_free(e);
    doc_free(d);
}


int
main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--measure") == 0) {
        measure(argv[2]);
    } else {
        test_typing();
        test_commands();
        test_limits();
        test_shadow_memory();
        test_changed_flag();
        test_external();
        test_hunks();
        test_fuzz();
    }

    printf("undotest: %d checks, %d failures\n", checks, fails);

    return fails != 0 ? 1 : 0;
}
