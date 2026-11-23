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
 * listtest.c - the unit tests of the marker counters and of the settings of
 * the contents and the lists (docs/AUTOLISTS.txt).
 *
 * The counters are changed by the edits, so every test makes the edit as the
 * Editor does it and then compares the counters of the document with a count
 * made here, by a loop of this file over the whole document.  The loop is the
 * judge: nothing in src/ is used to make it.
 */
#include "../src/util.h"
#include "../src/editor.h"
#include "../src/marker.h"
#include "../src/undo.h"
#include "../src/fileops.h"
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

#define OP(e, cls, ch, label, stmt)                                            \
    do {                                                                       \
        UndoKey uk_;                                                           \
                                                                               \
        undo_before_key((e), &uk_, (cls), (ch));                               \
        stmt;                                                                  \
        undo_after_key((e), &uk_, (label));                                    \
    } while (0)

static const int types[LK_COUNT] = {11, 25, 29, 3};
static const char *const kind_names[LK_COUNT] = {"figure", "picture", "table",
                                                 "index"};


/* The markers of each kind in the document, counted by this file. */
static void
truth(const Doc *d, unsigned out[LK_COUNT])
{
    memset(out, 0, LK_COUNT * sizeof out[0]);

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        const Block *b = d->items[i].blk;

        for (int c = 0; c < b->ncols; c++) {
            for (int j = 0; j < b->cols[c].text.n; j++) {
                const Tok *k = &b->cols[c].text.v[j];

                if (k->kind != TK_MARKER) {
                    continue;
                }

                for (int q = 0; q < LK_COUNT; q++) {
                    out[q] += tok_mtype(k) == types[q];
                }
            }
        }
    }
}


/* The counters are exact: those of the document are those of the loop. */
static void
exact(const Doc *d, const char *what)
{
    unsigned t[LK_COUNT];

    truth(d, t);

    for (int k = 0; k < LK_COUNT; k++) {
        CHECK(doc_marker_count(d, k) == t[k],
              "%s: the count of %s is %u, the document has %u", what,
              kind_names[k], doc_marker_count(d, k), t[k]);
        CHECK(doc_marker_stale(d, k) == 0, "%s: the count of %s is exact",
              what, kind_names[k]);
    }
}


static uint8_t *
payload(int type, const char *name, size_t *len)
{
    MPayload p;
    const MInfo *mi = marker_info(type);
    uint8_t *out;

    memset(&p, 0, sizeof p);
    mp_parse(&p, mi, (const uint8_t *)"", 0);

    if (type == 3) {
        mp_set(&p, "2", name, 1);
    } else if (type == 25) {
        mp_set(&p, "1", "p.png", 1);
        mp_set(&p, "6", name, 1);
    } else {
        mp_set(&p, "1", name, 1);
    }

    out = mp_build(&p, len);
    mp_free(&p);

    return out;
}


static void
put_marker(Editor *e, int type, const char *name)
{
    size_t n;
    uint8_t *data = payload(type, name, &n);

    OP(e, 0, 0, "Marker", ed_insert_marker(e, type, data, n));
    free(data);
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


static Doc *
reload(Doc *d)
{
    size_t n;
    uint8_t *b = doc_serialize(d, &n);
    char err[200];
    Doc *r = doc_load_mem(b, n, err, sizeof err);

    free(b);

    return r;
}


static int
has_record(Doc *d, const char *rec)
{
    size_t n;
    uint8_t *b = doc_serialize(d, &n);
    size_t rl = strlen(rec);
    int found = 0;

    /* The bytes hold NUL characters, so strstr is not used. */
    for (size_t i = 0; i + rl <= n && found == 0; i++) {
        found = memcmp(b + i, rec, rl) == 0;
    }

    free(b);

    return found;
}


/* A document that is made by hand: the bytes of a saved one with a record put
   in before *B, or a record taken out. */
static Doc *
hand_made(Doc *d, const char *add, const char *drop_prefix)
{
    size_t n;
    uint8_t *b = doc_serialize(d, &n);
    size_t cap = n + 256;
    char *out = malloc(cap);
    size_t o = 0;
    size_t i = 0;
    char err[200];
    Doc *r;

    while (i < n) {
        size_t e = i;

        while (e + 1 < n && !(b[e] == '\r' && b[e + 1] == '\n')) {
            e++;
        }

        e = e + 2 > n ? n : e + 2;

        int drop = drop_prefix != NULL &&
                   strncmp((const char *)b + i, drop_prefix,
                           strlen(drop_prefix)) == 0;

        if (b[i] == '*' && b[i + 1] == 'B' && add != NULL) {
            size_t al = strlen(add);

            memcpy(out + o, add, al);
            o += al;
        }

        if (drop == 0) {
            memcpy(out + o, b + i, e - i);
            o += e - i;
        }

        i = e;
    }

    r = doc_load_mem((uint8_t *)out, o, err, sizeof err);

    if (r == NULL) {
        printf("hand made document refused: %s\n", err);
    }

    free(out);
    free(b);

    return r;
}


/* The defaults of a new document and the settings. */
static void
test_defaults(void)
{
    Doc *d = doc_new(1);
    Doc *u = doc_new(0);

    for (int k = 0; k < LK_COUNT; k++) {
        CHECK(doc_marker_count(d, k) == 0, "new: no %s", kind_names[k]);
        CHECK(doc_list_setting(d, k) == -1, "new: %s is automatic",
              kind_names[k]);
        CHECK(doc_list_on(d, k) == 0, "new: the list of %s is off",
              kind_names[k]);
    }

    CHECK(doc_contents_level(d) == 99, "new: the contents level is 99");
    CHECK(doc_contents_level(u) == 99, "new unstructured: level is 99");
    CHECK(has_record(d, "ZN") == 0, "new: no count is written");
    CHECK(has_record(d, "FZL") == 0, "new: no list setting is written");

    doc_list_set(d, LK_TABLE, 1);
    CHECK(doc_list_setting(d, LK_TABLE) == 1 && doc_list_on(d, LK_TABLE) == 1,
          "explicit on, no marker: the list is on");
    doc_list_set(d, LK_TABLE, 0);
    CHECK(doc_list_setting(d, LK_TABLE) == 0 && doc_list_on(d, LK_TABLE) == 0,
          "explicit off");
    doc_list_set(d, LK_TABLE, -1);
    CHECK(doc_list_setting(d, LK_TABLE) == -1,
          "back to automatic: the record is gone");
    CHECK(has_record(d, "FZL") == 0, "automatic is not written");

    doc_list_set(d, LK_INDEX, 0);
    doc_contents_set(d, 3);
    {
        Doc *r = reload(d);

        CHECK(r != NULL && doc_list_setting(r, LK_INDEX) == 0,
              "the explicit off of the index is saved and read");
        CHECK(r != NULL && doc_contents_level(r) == 3,
              "the contents level 3 is saved and read");
        doc_contents_set(r, 0);
        CHECK(doc_contents_level(r) == 0, "level 0: no contents");
        doc_contents_set(r, 99);
        CHECK(doc_contents_level(r) == 99, "level 99");
        doc_contents_set(r, 400);
        CHECK(doc_contents_level(r) == 99, "a level over 99 is 99");
        doc_free(r);
    }

    doc_free(d);
    doc_free(u);
}


/* A marker inserted, deleted, cut, pasted, undone and redone. */
static void
test_edits(void)
{
    Editor *e = new_editor();
    Doc *d = e->doc;

    type_text(e, "abc");

    for (int k = 0; k < LK_COUNT; k++) {
        put_marker(e, types[k], "entry");
        CHECK(doc_marker_count(d, k) == 1, "insert: one %s", kind_names[k]);
        CHECK(doc_list_on(d, k) == 1, "insert: the list of %s is on",
              kind_names[k]);
        exact(d, "insert");
    }

    /* A second Figure marker, then Backspace removes the second only. */
    put_marker(e, 11, "second");
    CHECK(doc_marker_count(d, LK_FIGURE) == 2, "two figures");
    OP(e, 2, 0, "Deleting", ed_backspace(e));
    CHECK(doc_marker_count(d, LK_FIGURE) == 1, "backspace: one figure");
    exact(d, "backspace");
    CHECK(undo_undo(e) == 0, "undo the backspace");
    CHECK(doc_marker_count(d, LK_FIGURE) == 2, "undo: two figures again");
    exact(d, "undo");
    CHECK(undo_redo(e) == 0, "redo the backspace");
    CHECK(doc_marker_count(d, LK_FIGURE) == 1, "redo: one figure");
    exact(d, "redo");

    /* Delete forward on a marker. */
    put_cursor(e, e->cur.item, 0, 3);
    OP(e, 2, 0, "Deleting", ed_delete(e));
    exact(d, "delete");

    /* A selection of text with markers cut and pasted. */
    put_cursor(e, e->cur.item, 0, 0);
    put_marker(e, 11, "again");
    exact(d, "a figure to cut");
    put_cursor(e, e->cur.item, 0, 0);
    ed_sel_begin(e, SEL_TEXT);
    put_cursor(e, e->cur.item, 0, ed_text(e)->n - 1);
    OP(e, 0, 13, "Cut", ed_sel_apply(e, OP_CUT));
    exact(d, "cut text");
    CHECK(doc_marker_count(d, LK_FIGURE) == 0 &&
              doc_list_on(d, LK_FIGURE) == 0,
          "cut: the last figure is gone and the list is off");
    OP(e, 0, 0, "Paste", ed_paste(e));
    exact(d, "paste text");
    CHECK(doc_list_on(d, LK_FIGURE) == 1, "paste: the list is on again");
    CHECK(undo_undo(e) == 0, "undo the paste");
    exact(d, "undo paste");
    CHECK(undo_undo(e) == 0, "undo the cut");
    exact(d, "undo cut");
    CHECK(undo_redo(e) == 0 && undo_redo(e) == 0, "redo both");
    exact(d, "redo both");

    /* A block with markers: append, delete, cut, paste, split and join. */
    OP(e, 0, 13, "Append", ed_append_block(e, 0));
    type_text(e, "next");
    put_marker(e, 29, "t");
    put_marker(e, 3, "i");
    exact(d, "block markers");
    put_cursor(e, e->cur.item, 0, 2);
    OP(e, 0, 0, "Split", ed_split(e, 1));
    exact(d, "split");
    put_cursor(e, e->cur.item - 1, 0, 0);
    OP(e, 0, 0, "Join", ed_join(e, 1));
    exact(d, "join");
    ed_sel_begin(e, SEL_BLOCK);
    OP(e, 0, 13, "Cut", ed_sel_apply(e, OP_CUT));
    exact(d, "cut block");
    OP(e, 0, 0, "Paste", ed_paste(e));
    exact(d, "paste block");
    OP(e, 0, 0, "Paste", ed_paste(e));
    exact(d, "paste block twice");
    ed_sel_begin(e, SEL_BLOCK);
    OP(e, 0, 13, "Delete", ed_sel_apply(e, OP_DELETE));
    exact(d, "delete block");

    /* A Replace that removes no marker leaves the counts alone. */
    OP(e, 0, 0, "Replace", ed_replace_all(e, "a", "bb", 1));
    exact(d, "replace");

    /* The panel changes the fields of a marker, not its kind. */
    for (int i = 0; i < ed_text(e)->n; i++) {
        if (ed_text(e)->v[i].kind == TK_MARKER) {
            size_t n;
            uint8_t *data = payload(tok_mtype(&ed_text(e)->v[i]), "new", &n);

            ed_replace_marker(e, i, data, n);
            free(data);
            break;
        }
    }

    exact(d, "replace marker");
    end_editor(e);
}


/* The counts are saved and read; a file without them is counted once. */
static void
test_load(void)
{
    Editor *e = new_editor();
    Doc *d = e->doc;
    Doc *r;
    unsigned long before;

    type_text(e, "x");
    put_marker(e, 11, "f");
    put_marker(e, 11, "g");
    put_marker(e, 29, "t");
    CHECK(has_record(d, "ZNF2") != 0, "saved: ZNF2");
    CHECK(has_record(d, "ZNT1") != 0, "saved: ZNT1");
    CHECK(has_record(d, "ZNP") == 0, "saved: no record of a kind with none");

    before = doc_count_recounts;
    r = reload(d);
    CHECK(r != NULL && doc_marker_count(r, LK_FIGURE) == 2 &&
              doc_marker_count(r, LK_TABLE) == 1,
          "load: the saved counts");
    CHECK(doc_count_recounts - before == 1,
          "load: one recount, for the kinds that have no record (%lu)",
          doc_count_recounts - before);
    exact(r, "load");
    doc_free(r);

    /* All four kinds with a record: nothing is counted at the load. */
    put_marker(e, 25, "p");
    put_marker(e, 3, "i");
    before = doc_count_recounts;
    r = reload(d);
    CHECK(doc_count_recounts - before == 0,
          "load with all four counts: no recount (%lu)",
          doc_count_recounts - before);
    exact(r, "load all four");
    doc_free(r);

    /* An old file: no count at all. */
    r = hand_made(d, NULL, "ZN");
    CHECK(r != NULL && doc_marker_count(r, LK_FIGURE) == 2 &&
              doc_marker_count(r, LK_PICTURE) == 1 &&
              doc_marker_count(r, LK_INDEX) == 1,
          "old file: counted at the load");
    exact(r, "old file");
    CHECK(has_record(r, "ZN") == 0,
          "old file: saved as it is, with no counts, until a marker is "
          "edited (the bytes of an unedited document stay)");
    {
        Editor *e2 = ed_new(r);
        size_t n1;
        size_t n2;
        uint8_t *b1;
        uint8_t *b2;

        b1 = doc_serialize(r, &n1);
        put_marker(e2, 29, "t");
        CHECK(has_record(r, "ZNT2") != 0 && has_record(r, "ZNF2") != 0,
              "old file: after an edit of a marker the counts are written");
        b2 = doc_serialize(r, &n2);
        CHECK(n2 > n1, "old file: the edit changed the bytes");
        free(b1);
        free(b2);
        ed_free(e2);
    }

    doc_free(r);

    /* A count that is marked stale is counted again. */
    r = hand_made(d, "ZNX1\r\n", "ZNX");
    exact(r, "stale record");
    CHECK(r != NULL && doc_marker_count(r, LK_FIGURE) == 2,
          "stale: counted again");
    doc_free(r);

    /* A wrong count that is not marked is believed (the file is the owner's
       record) until an edit or a recount; the kind with the wrong count is
       what the guard of the next test is about. */
    r = hand_made(d, "ZNF7\r\n", "ZNF");
    CHECK(r != NULL && doc_marker_count(r, LK_FIGURE) == 7,
          "a count in the file is trusted");
    doc_free(r);

    /* Damaged records are ignored and the kind is counted. */
    r = hand_made(d, "ZNF12x\r\nZNT\r\nZNP99999999\r\n", "ZN");
    exact(r, "damaged counts");
    doc_free(r);

    end_editor(e);
}


/* The largest value, and a count that would go below 0. */
static void
test_limits(void)
{
    Editor *e = new_editor();
    Doc *d = e->doc;
    Doc *r;
    char rec[64];

    type_text(e, "x");

    /* Overflow: a count at the largest value stays there, and says so. */
    d->mk_count[LK_FIGURE] = LIST_COUNT_MAX;
    put_marker(e, 11, "f");
    CHECK(doc_marker_count(d, LK_FIGURE) == LIST_COUNT_MAX,
          "saturated: the count stays at the largest value");
    CHECK(doc_marker_stale(d, LK_FIGURE) != 0,
          "saturated: the count is marked stale");
    CHECK(doc_list_on(d, LK_FIGURE) == 1, "saturated: the list is on");
    CHECK(has_record(d, "ZNF9999999") != 0 && has_record(d, "ZNX1") != 0,
          "saturated: saved with the stale mark");
    r = reload(d);
    CHECK(r != NULL && doc_marker_count(r, LK_FIGURE) == 1,
          "saturated: counted again at the load (%u)",
          r != NULL ? doc_marker_count(r, LK_FIGURE) : 0);
    exact(r, "saturated reload");
    doc_free(r);

    /* A file whose count is the largest value and not marked. */
    snprintf(rec, sizeof rec, "ZNF%u\r\n", LIST_COUNT_MAX);
    r = hand_made(d, rec, "ZN");
    CHECK(r != NULL && doc_marker_count(r, LK_FIGURE) == LIST_COUNT_MAX &&
              doc_marker_stale(r, LK_FIGURE) == 0,
          "a file with the largest count");
    doc_free(r);

    /* Underflow: a hand made file says 0 figures and has two. */
    r = hand_made(d, "ZNF0\r\n", "ZN");
    {
        Editor *e2 = ed_new(r);

        CHECK(doc_marker_count(r, LK_FIGURE) == 0,
              "underflow: the file says 0");
        CHECK(doc_list_on(r, LK_FIGURE) == 0,
              "underflow: so the list is off, as the file says");
        put_cursor(e2, 0, 0, 0);

        for (int i = 0; i < ed_text(e2)->n; i++) {
            if (ed_text(e2)->v[i].kind == TK_MARKER) {
                put_cursor(e2, e2->cur.item, 0, i);
                break;
            }
        }

        OP(e2, 2, 0, "Deleting", ed_delete(e2));
        CHECK(doc_marker_count(r, LK_FIGURE) == 0,
              "underflow: the count does not go below 0");
        CHECK(doc_marker_stale(r, LK_FIGURE) != 0,
              "underflow: the count is marked stale");
        CHECK(has_record(r, "ZNX1") != 0, "underflow: saved with the mark");
        {
            Doc *r2 = reload(r);

            exact(r2, "underflow reload");
            doc_free(r2);
        }
        ed_free(e2);
        doc_free(r);
    }

    end_editor(e);
}


/* A part of a document has no counts of its own. */
static void
test_part(void)
{
    Editor *e = new_editor();
    Doc *d = e->doc;
    Doc part;

    type_text(e, "x");
    put_marker(e, 11, "f");
    doc_part_untitled(d, &part);
    CHECK(doc_marker_count(&part, LK_FIGURE) == 0, "part: no count copied");
    CHECK(has_record(&part, "ZNF") == 0, "part: no count written");
    doc_part_free(&part);
    end_editor(e);
}


static double
now_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);

    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}


/*
 * A delete does not search the document: in a document of 3600 pages the
 * counting functions read no more than the token that is deleted, and the
 * whole-document count is not called.
 */
static void
test_no_search(void)
{
    Editor *e = new_editor();
    Doc *d = e->doc;
    enum {
        BLOCKS = 20000
    };
    unsigned long tokens;
    unsigned long recounts;
    double t0;
    double t1;

    type_text(e, "a");

    for (int i = 0; i < BLOCKS; i++) {
        ed_append_block(e, 0);

        for (int j = 0; j < 40; j++) {
            ed_insert_char(e, u8('a' + j % 26));
        }

        if (i % 5 == 0) {
            size_t n;
            uint8_t *data = payload(11, "f", &n);

            ed_insert_marker(e, 11, data, n);
            free(data);
        }
    }

    exact(d, "big document");
    CHECK(doc_marker_count(d, LK_FIGURE) == BLOCKS / 5, "big: %d figures",
          BLOCKS / 5);

    /* Delete the marker of the last block that has one, cursor on it. */
    for (int i = d->nitems - 1; i >= 0; i--) {
        const Text *t = d->items[i].type == IT_BLOCK ?
                            &d->items[i].blk->cols[0].text : NULL;
        int at = -1;

        for (int j = 0; t != NULL && j < t->n; j++) {
            if (t->v[j].kind == TK_MARKER) {
                at = j;
            }
        }

        if (at >= 0) {
            put_cursor(e, i, 0, at);
            break;
        }
    }

    tokens = doc_count_tokens_read;
    recounts = doc_count_recounts;
    t0 = now_ms();
    OP(e, 2, 0, "Deleting", ed_delete(e));
    t1 = now_ms();
    CHECK(doc_count_recounts == recounts,
          "delete: the whole document was not counted");
    CHECK(doc_count_tokens_read - tokens <= 2,
          "delete: %lu tokens were read to count (a token is one)",
          doc_count_tokens_read - tokens);
    CHECK(doc_marker_count(d, LK_FIGURE) == BLOCKS / 5 - 1,
          "delete: one figure less");
    printf("note: a delete in %d blocks: %.3f ms, %lu tokens read\n", BLOCKS,
           t1 - t0, doc_count_tokens_read - tokens);

    /* Delete a block: the tokens of that block only. */
    tokens = doc_count_tokens_read;
    ed_sel_begin(e, SEL_BLOCK);
    OP(e, 0, 13, "Delete", ed_sel_apply(e, OP_DELETE));
    CHECK(doc_count_tokens_read - tokens <= 100,
          "block delete: %lu tokens read, the block has about 42",
          doc_count_tokens_read - tokens);
    CHECK(doc_count_recounts == recounts,
          "block delete: the whole document was not counted");
    exact(d, "block delete");
    end_editor(e);
}


/* A run of random edits, undo and redo; the counters stay exact. */
static void
test_fuzz(void)
{
    Editor *e = new_editor();
    Doc *d = e->doc;

    undo_set_levels(UNDO_MAX_LEVELS);
    srand(4242);

    for (int i = 0; i < 1500; i++) {
        int r = rand() % 16;

        if (r < 4) {
            int c = 'a' + rand() % 26;

            OP(e, 1, c, "Typing", ed_insert_char(e, u8(c)));
        } else if (r == 4) {
            put_marker(e, types[rand() % LK_COUNT], "m");
        } else if (r == 5) {
            put_marker(e, 1, "m");
        } else if (r == 6) {
            OP(e, 2, 0, "Deleting", ed_backspace(e));
        } else if (r == 7) {
            OP(e, 2, 0, "Deleting", ed_delete(e));
        } else if (r == 8) {
            OP(e, 0, 13, "New line", ed_insert_nl(e));
        } else if (r == 9) {
            OP(e, 0, 0, "Append", ed_append_block(e, 0));
        } else if (r == 10) {
            OP(e, 0, 0, "Split", ed_split(e, 1));
        } else if (r == 11) {
            OP(e, 0, 0, "Join", ed_join(e, 1));
        } else if (r == 12) {
            ed_sel_begin(e, SEL_BLOCK);
            OP(e, 0, 13, "Cut", ed_sel_apply(e, rand() % 2 != 0 ? OP_CUT :
                                                                  OP_DELETE));
        } else if (r == 13) {
            OP(e, 0, 0, "Paste", ed_paste(e));
        } else if (r == 14) {
            undo_undo(e);
        } else {
            undo_redo(e);
        }

        if (i % 25 == 0) {
            exact(d, "fuzz");
        }

        if (rand() % 30 == 0) {
            put_cursor(e, rand() % d->nitems, 0, 0);
        }
    }

    exact(d, "fuzz end");
    end_editor(e);
}


int
main(void)
{
    test_defaults();
    test_edits();
    test_load();
    test_limits();
    test_part();
    test_no_search();
    test_fuzz();
    printf("listtest: %d checks, %d failures\n", checks, fails);

    return fails != 0 ? 1 : 0;
}
