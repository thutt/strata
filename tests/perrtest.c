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
 * perrtest.c - the unit tests of the Print Error markers of the Editor
 * (src/perr.c), of the error file (src/diag.c) and of the way the LaTeX
 * writer and the exports leave the markers out.  The document is
 * tests/data/print_errors.doc.  Usage: perrtest DATADIR WORKDIR.
 */
#include "../src/util.h"
#include "../src/diag.h"
#include "../src/editor.h"
#include "../src/fileops.h"
#include "../src/latex.h"
#include "../src/marker.h"
#include "../src/perr.h"
#include "../src/ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/* The items of the defects in print_errors.doc (as strprint reports them). */
enum {
    IT_EQ_BAD = 7,
    IT_PICTURE = 11,
    IT_CHAR_EQ = 12
};


static Doc *
load(const char *dir, const char *name)
{
    char path[1024];
    char err[256];
    Doc *d;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    d = doc_load(path, err, sizeof err);

    if (d == NULL) {
        printf("cannot load %s: %s\n", path, err);
        exit(2);
    }

    return d;
}


static Diag
mk(int kind, const char *code, const char *msg, int item, int line, int mtype,
   const char *text, int off)
{
    Diag g;

    memset(&g, 0, sizeof g);
    g.kind = kind;
    str_copy(g.code, sizeof g.code, code);
    str_copy(g.msg, sizeof g.msg, msg);
    str_copy(g.text, sizeof g.text, text);
    g.item = item;
    g.line = line;
    g.mtype = mtype;
    g.off = off;

    return g;
}


/* The four defects of the document, as the Printer reports them. */
static int
four(Diag *v)
{
    v[0] = mk(DIAG_ERROR, "equation-invalid", "The equation is not valid.",
              IT_EQ_BAD, 2, 9, "left #R[x] => #^(a = 0)", 16);
    v[1] = mk(DIAG_ERROR, "picture-missing", "The picture is missing.",
              IT_PICTURE, 1, 25, "pics/missing.pcx", 0);
    v[2] = mk(DIAG_WARNING, "char-missing", "No font has U+2320.", IT_CHAR_EQ,
              1, 0, "U+2320", 26);
    v[3] = mk(DIAG_ERROR, "equation-refused", "TeX rejects the equation.",
              IT_CHAR_EQ, 1, 9, "a + \\notamacro b", 60);

    return 4;
}


static const Text *
text_of(const Doc *d, int item)
{
    return &d->items[item].blk->cols[0].text;
}


static int
is_pe(const Tok *k)
{
    return k->kind == TK_MARKER && tok_mtype(k) == MT_PRINT_ERROR;
}


/* The index of the nth (1 is the first) Print Error of a text, or -1. */
static int
pe_at(const Text *t, int nth)
{
    for (int i = 0; i < t->n; i++) {
        if (is_pe(&t->v[i]) != 0 && --nth == 0) {
            return i;
        }
    }

    return -1;
}


static int
type_at(const Text *t, int i)
{
    return i >= 0 && i < t->n && t->v[i].kind == TK_MARKER ?
               tok_mtype(&t->v[i]) :
               -1;
}


static void
test_menu_and_hand(const char *dir)
{
    Doc *d = load(dir, "print_errors.doc");
    Editor *e = ed_new(d);
    int listed = 0;
    const MInfo *mi;
    uint8_t rec[] = "1hand\r\n";

    e->text_width = 80;
    ed_layout(e);

    for (int i = 0; i < marker_menu_count(); i++) {
        if (marker_menu(i)->type == MT_PRINT_ERROR ||
            strcmp(marker_menu(i)->name, "Print Error") == 0) {
            listed++;
        }
    }

    CHECK(listed == 0, "the Print Error is on the Marker Commands menu");
    mi = marker_info(MT_PRINT_ERROR);
    CHECK(mi != NULL && strcmp(mi->name, "Print Error") == 0,
          "marker_info knows the Print Error");

    /* No call that the user's keys reach makes one. */
    e->cur.item = IT_EQ_BAD;
    e->cur.col = 0;
    e->cur.tok = 0;
    ed_insert_marker(e, MT_PRINT_ERROR, rec, sizeof rec - 1);
    CHECK(perr_count(d) == 0, "a Print Error was made by hand");
    CHECK(d->modified == 0, "the refused insert changed the document");
    ed_free(e);
    doc_free(d);
}


static void
test_apply(const char *dir)
{
    Doc *d = load(dir, "print_errors.doc");
    Editor *e = ed_new(d);
    Diag v[4];
    int n = four(v);
    const Text *t;
    int p;

    e->text_width = 80;
    ed_layout(e);
    CHECK(d->modified == 0, "the document starts unmodified");
    CHECK(perr_apply(e, PERR_RUN_PRINT, v, n, NULL) == 4,
          "four markers inserted");
    CHECK(perr_count(d) == 4, "four markers counted (%d)", perr_count(d));
    CHECK(d->modified != 0, "the insertion marks the document as changed");

    /* The equation of the first defect: the marker is right before it. */
    t = text_of(d, IT_EQ_BAD);
    p = pe_at(t, 1);
    CHECK(p > 0 && type_at(t, p + 1) == 9,
          "the first marker is in front of the equation (%d)", p);

    /* The picture: the marker is in front of the picture marker. */
    t = text_of(d, IT_PICTURE);
    p = pe_at(t, 1);
    CHECK(p == 0 && type_at(t, 1) == 25,
          "the second marker is in front of the picture (%d)", p);

    /* The missing character: right before it; and the refused equation. */
    t = text_of(d, IT_CHAR_EQ);
    p = pe_at(t, 1);
    CHECK(p > 0 && t->v[p + 1].kind == TK_CHAR && t->v[p + 1].a == 0xF4,
          "the third marker is in front of the character (%d)", p);
    p = pe_at(t, 2);
    CHECK(p > 0 && type_at(t, p + 1) == 9,
          "the fourth marker is in front of the second equation (%d)", p);

    /* The text of the error is kept, with the code. */
    {
        char text[400];

        perr_text(&text_of(d, IT_PICTURE)->v[0], text, sizeof text);
        CHECK(strcmp(text, "The picture is missing. [picture-missing]") == 0,
              "the text of the marker: %s", text);
    }

    /* The label on the screen. */
    {
        char lb[400];

        CHECK(marker_render(&text_of(d, IT_PICTURE)->v[0], lb, sizeof lb) ==
                  MR_ERROR,
              "a Print Error is drawn as an error");
        CHECK(strncmp(lb, "PRINT ERROR: The picture is missing.", 35) == 0,
              "the label: %s", lb);
    }

    /* A new run of the same kind replaces them; it does not add. */
    CHECK(perr_apply(e, PERR_RUN_PRINT, v, 2, NULL) == 2, "two inserted");
    CHECK(perr_count(d) == 2, "the old markers were removed (%d)",
          perr_count(d));

    /* A run of the other kind leaves the markers of the first alone. */
    CHECK(perr_apply(e, PERR_RUN_PREVIEW, v + 2, 2, NULL) == 2, "preview");
    CHECK(perr_count(d) == 4, "markers of both runs (%d)", perr_count(d));
    CHECK(perr_apply(e, PERR_RUN_PRINT, v, 0, NULL) == 0, "a clean print");
    CHECK(perr_count(d) == 2, "only the preview ones remain (%d)",
          perr_count(d));
    CHECK(perr_apply(e, PERR_RUN_PREVIEW, v, 0, NULL) == 0, "a clean preview");
    CHECK(perr_count(d) == 0, "a clean run removes the markers (%d)",
          perr_count(d));
    ed_free(e);
    doc_free(d);
}


/* The cursor goes to a place; the message line reports where it landed. */
static void
at(Editor *e, int item, int tok)
{
    e->cur.item = item;
    e->cur.col = 0;
    e->cur.tok = tok;
}


static void
test_find(const char *dir)
{
    Doc *d = load(dir, "print_errors.doc");
    Editor *e = ed_new(d);
    Diag v[4];
    int n = four(v);
    const Text *t;

    e->text_width = 80;
    ed_layout(e);

    /* None: the message shows so and the cursor does not move. */
    at(e, IT_PICTURE, 0);
    CHECK(perr_find(e, 1) == 0, "find with none");
    CHECK(strstr(e->msg, "No print errors") != NULL, "none: %s", e->msg);
    CHECK(e->cur.item == IT_PICTURE && e->cur.tok == 0, "cursor kept");

    perr_apply(e, PERR_RUN_PRINT, v, n, NULL);

    /* From the start of the document: the first one, then each next. */
    at(e, IT_EQ_BAD, 0);
    CHECK(perr_find(e, 1) == 1, "find next from the start of the block");
    CHECK(e->cur.item == IT_EQ_BAD &&
              e->cur.tok == pe_at(text_of(d, IT_EQ_BAD), 1),
          "the cursor is on the first marker (%d)", e->cur.tok);
    CHECK(strstr(e->msg, "Print error 1 of 4") != NULL, "first: %s", e->msg);
    CHECK(perr_find(e, 1) == 1, "find the second");
    t = text_of(d, e->cur.item);
    CHECK(e->cur.item == IT_PICTURE && e->cur.tok == 0 &&
              type_at(t, 0) == MT_PRINT_ERROR,
          "the cursor is on the marker of the picture");
    CHECK(strstr(e->msg, "Print error 2 of 4") != NULL, "second: %s",
          e->msg);

    /* The marker under the cursor is not found again: the next one. */
    CHECK(perr_find(e, 1) == 1, "find the third");
    CHECK(e->cur.item == IT_CHAR_EQ &&
              is_pe(&text_of(d, e->cur.item)->v[e->cur.tok]) != 0,
          "the third marker (item %d)", e->cur.item);
    CHECK(strstr(e->msg, "Print error 3 of 4") != NULL, "message: %s", e->msg);
    CHECK(strstr(e->msg, "No font has U+2320") != NULL,
          "the message shows the text: %s", e->msg);
    CHECK(perr_find(e, 1) == 1, "find the fourth");
    CHECK(strstr(e->msg, "Print error 4 of 4") != NULL, "fourth: %s", e->msg);
    CHECK(strstr(e->msg, "Wrapped") == NULL, "no wrap yet: %s", e->msg);

    /* From the end: it wraps to the first, with a message. */
    CHECK(perr_find(e, 1) == 1, "wrap forward");
    CHECK(e->cur.item == IT_EQ_BAD, "wrapped to the first (item %d)",
          e->cur.item);
    CHECK(strstr(e->msg, "Wrapped to the start") != NULL, "wrap: %s", e->msg);
    CHECK(strstr(e->msg, "Print error 1 of 4") != NULL, "wrap: %s", e->msg);

    /* Backward from the first wraps to the last. */
    CHECK(perr_find(e, 0) == 1, "wrap backward");
    CHECK(e->cur.item == IT_CHAR_EQ, "wrapped to the last (item %d)",
          e->cur.item);
    CHECK(strstr(e->msg, "Wrapped to the end") != NULL, "wrap: %s", e->msg);
    CHECK(strstr(e->msg, "Print error 4 of 4") != NULL, "wrap: %s", e->msg);

    /* Backward in the middle. */
    CHECK(perr_find(e, 0) == 1, "previous");
    CHECK(strstr(e->msg, "Print error 3 of 4") != NULL, "previous: %s",
          e->msg);
    CHECK(strstr(e->msg, "Wrapped") == NULL, "no wrap: %s", e->msg);

    /* From a place between two markers (on the picture marker). */
    at(e, IT_PICTURE, 1);
    CHECK(perr_find(e, 0) == 1 && e->cur.item == IT_PICTURE &&
              e->cur.tok == 0,
          "previous from the picture lands on the marker before it");
    at(e, IT_PICTURE, 1);
    CHECK(perr_find(e, 1) == 1 && e->cur.item == IT_CHAR_EQ,
          "next from the picture lands in the next block");

    /* Del and Backspace delete a Print Error like any other token. */
    at(e, IT_PICTURE, 0);
    CHECK(perr_here(e) != NULL, "the cursor is on the marker");
    ed_delete(e);
    CHECK(perr_count(d) == 3 && perr_here(e) == NULL,
          "Del removed the marker (%d)", perr_count(d));
    at(e, IT_CHAR_EQ, pe_at(text_of(d, IT_CHAR_EQ), 1) + 1);
    ed_backspace(e);
    CHECK(perr_count(d) == 2, "Backspace removed one (%d)", perr_count(d));
    perr_apply(e, PERR_RUN_PRINT, v, n, NULL);
    CHECK(perr_count(d) == 4, "four again (%d)", perr_count(d));
    at(e, IT_CHAR_EQ, pe_at(text_of(d, IT_CHAR_EQ), 1));

    /* Remove the one under the cursor, then all. */
    CHECK(perr_remove_here(e) == 1, "remove here");
    CHECK(perr_count(d) == 3, "three remain (%d)", perr_count(d));
    CHECK(perr_remove_here(e) == 0, "nothing to remove here now");
    CHECK(perr_here(e) == NULL, "the cursor is not on a marker");
    CHECK(perr_remove_all(e) == 3, "remove all");
    CHECK(perr_count(d) == 0, "none remain");
    CHECK(perr_remove_all(e) == 0, "remove all of none");
    ed_free(e);
    doc_free(d);
}


static void
test_unfound(const char *dir)
{
    Doc *d = load(dir, "print_errors.doc");
    Editor *e = ed_new(d);
    Diag v[2];
    const Text *t;

    e->text_width = 80;
    ed_layout(e);

    /* A diagnostic with no place at all, and one whose block is gone. */
    v[0] = mk(DIAG_ERROR, "tex-error", "TeX said no.", DIAG_NOITEM, 0, 0, "",
              -1);
    v[1] = mk(DIAG_ERROR, "picture-missing", "Gone.", IT_PICTURE, 3, 25,
              "other.pcx", 0);
    CHECK(perr_apply(e, PERR_RUN_PRINT, v, 2, NULL) == 2, "both placed");
    CHECK(perr_count(d) == 2, "two markers");
    CHECK(pe_at(text_of(d, 1), 1) >= 0 || pe_at(text_of(d, 2), 1) >= 0 ||
              perr_count(d) == 2,
          "a marker at the start");

    {
        char text[400];
        int found = 0;

        for (int i = 0; i < d->nitems; i++) {
            if (d->items[i].type != IT_BLOCK) {
                continue;
            }

            t = text_of(d, i);

            for (int k = 0; k < t->n; k++) {
                if (is_pe(&t->v[k]) != 0) {
                    perr_text(&t->v[k], text, sizeof text);

                    if (strstr(text, "nearest") != NULL) {
                        found++;
                    }
                }
            }
        }

        CHECK(found == 2, "both messages say the place is a guess (%d)",
              found);
    }

    ed_free(e);
    doc_free(d);
}


static void
test_part(const char *dir)
{
    Doc *d = load(dir, "print_errors.doc");
    Editor *e = ed_new(d);
    Doc part;
    Diag v[1];
    int pi = -1;
    char err[200];

    e->text_width = 80;
    ed_layout(e);

    /* Preview of a block: the item is that of the part. */
    CHECK(doc_part_block(d, IT_CHAR_EQ, &part) == 0, "part of a block");

    for (int i = 0; i < part.nitems; i++) {
        if (part.items[i].type == IT_BLOCK &&
            part.items[i].blk == d->items[IT_CHAR_EQ].blk) {
            pi = i;
        }
    }

    CHECK(pi >= 0 && pi != IT_CHAR_EQ, "the block has another item (%d)", pi);
    v[0] = mk(DIAG_ERROR, "equation-refused", "TeX rejects.", pi, 1, 9,
              "a + \\notamacro b", 60);
    CHECK(perr_apply(e, PERR_RUN_PREVIEW, v, 1, &part) == 1, "placed");
    CHECK(pe_at(text_of(d, IT_CHAR_EQ), 1) > 0, "in the block of the part");
    CHECK(perr_count(d) == 1, "only one marker");

    /* A run of the part leaves the markers outside it alone. */
    {
        Diag w = mk(DIAG_ERROR, "picture-missing", "Missing.", IT_PICTURE, 1,
                    25, "pics/missing.pcx", 0);

        CHECK(perr_apply(e, PERR_RUN_PREVIEW, &w, 1, NULL) == 1,
              "the whole document");
        CHECK(perr_count(d) == 1, "the whole run replaced the old one (%d)",
              perr_count(d));
        CHECK(perr_apply(e, PERR_RUN_PREVIEW, v, 0, &part) == 0, "clean part");
        CHECK(perr_count(d) == 1, "the part run kept the one outside (%d)",
              perr_count(d));
    }

    doc_part_free(&part);

    /* A range of sections: the second section only. */
    CHECK(doc_part_sections(d, "2", "2", &part, err, sizeof err) == 0,
          "part of sections: %s", err);
    pi = -1;

    for (int i = 0; i < part.nitems; i++) {
        if (part.items[i].type == IT_BLOCK &&
            part.items[i].blk == d->items[IT_PICTURE].blk) {
            pi = i;
        }
    }

    v[0] = mk(DIAG_ERROR, "picture-missing", "Missing.", pi, 1, 25,
              "pics/missing.pcx", 0);
    CHECK(perr_apply(e, PERR_RUN_PRINT, v, 1, &part) == 1, "range placed");
    CHECK(pe_at(text_of(d, IT_PICTURE), 1) == 0, "the picture marker");
    doc_part_free(&part);
    ed_free(e);
    doc_free(d);
}


static void
test_copy(const char *dir)
{
    Doc *d = load(dir, "print_errors.doc");
    Editor *e = ed_new(d);
    Diag v[4];
    int n = four(v);
    const Text *t;
    int before;

    e->text_width = 80;
    ed_layout(e);
    perr_apply(e, PERR_RUN_PRINT, v, n, NULL);

    /* Copy the whole text of the block and paste it at the end of another. */
    t = text_of(d, IT_CHAR_EQ);
    at(e, IT_CHAR_EQ, 0);
    ed_sel_begin(e, SEL_TEXT);
    e->cur.tok = t->n;
    CHECK(ed_sel_apply(e, OP_COPY) == 0, "copy");
    before = perr_count(d);
    at(e, 1, text_of(d, 1)->n);
    CHECK(ed_paste(e) == 0, "paste");
    CHECK(perr_count(d) == before, "a pasted Print Error was dropped (%d)",
          perr_count(d));

    /* Cut takes them out of the text and does not put them on the board. */
    t = text_of(d, IT_CHAR_EQ);
    at(e, IT_CHAR_EQ, 0);
    ed_sel_begin(e, SEL_TEXT);
    e->cur.tok = t->n;
    CHECK(ed_sel_apply(e, OP_CUT) == 0, "cut");
    at(e, 1, text_of(d, 1)->n);
    ed_paste(e);
    CHECK(perr_count(d) == before - 2, "cut removed two, pasted none (%d)",
          perr_count(d));
    ed_free(e);
    doc_free(d);
}


static void
test_save_load(const char *dir, const char *work)
{
    Doc *d = load(dir, "print_errors.doc");
    Editor *e = ed_new(d);
    Diag v[4];
    int n = four(v);
    char path[1024];
    char err[256];
    char t1[400];
    char t2[400];
    Doc *r;

    e->text_width = 80;
    ed_layout(e);
    perr_apply(e, PERR_RUN_PRINT, v, n, NULL);
    snprintf(path, sizeof path, "%s/marked.doc", work);
    CHECK(doc_save(d, path, err, sizeof err) == 0, "save: %s", err);
    r = doc_load(path, err, sizeof err);
    CHECK(r != NULL, "load: %s", err);

    if (r != NULL) {
        CHECK(perr_count(r) == 4, "four markers after the load (%d)",
              perr_count(r));
        perr_text(&text_of(r, IT_PICTURE)->v[0], t1, sizeof t1);
        perr_text(&text_of(d, IT_PICTURE)->v[0], t2, sizeof t2);
        CHECK(strcmp(t1, t2) == 0 && t1[0] != '\0', "the text survived: %s",
              t1);

        /* Saved again, the file is the same. */
        {
            char path2[1024];
            FILE *f1;
            FILE *f2;
            int same = 1;
            int c1;
            int c2;

            snprintf(path2, sizeof path2, "%s/marked2.doc", work);
            CHECK(doc_save(r, path2, err, sizeof err) == 0, "save again");
            f1 = fopen(path, "rb");
            f2 = fopen(path2, "rb");

            do {
                c1 = fgetc(f1);
                c2 = fgetc(f2);
                same = same != 0 && c1 == c2;
            } while (c1 != EOF && c2 != EOF);

            fclose(f1);
            fclose(f2);
            CHECK(same != 0, "load and save lose nothing");
        }

        doc_free(r);
    }

    ed_free(e);
    doc_free(d);
}


/* The LaTeX of a document in a file; returns its bytes (the caller frees). */
static char *
tex_of(Doc *d, size_t *len)
{
    LatexOpts o;
    FILE *f = tmp_file();
    char *buf;

    memset(&o, 0, sizeof o);
    o.docdir = ".";
    latex_write(d, f, &o);
    *len = lsz(ftell(f));
    rewind(f);
    buf = malloc(*len + 1);

    if (fread(buf, 1, *len, f) != *len) {
        *len = 0;
    }

    buf[*len] = '\0';
    fclose(f);

    return buf;
}


static void
test_not_printed(const char *dir, const char *work)
{
    Doc *d = load(dir, "print_errors.doc");
    Doc *m = load(dir, "print_errors.doc");
    Editor *e = ed_new(m);
    Diag v[4];
    int n = four(v);
    char *a;
    char *b;
    size_t la;
    size_t lb;
    char **l1;
    char **l2;
    int n1;
    int n2;
    int same = 1;

    e->text_width = 80;
    ed_layout(e);
    perr_apply(e, PERR_RUN_PRINT, v, n, NULL);
    CHECK(perr_count(m) == 4, "markers are in");
    a = tex_of(d, &la);
    b = tex_of(m, &lb);
    CHECK(la > 100 && la == lb && memcmp(a, b, la) == 0,
          "the LaTeX is the same with and without markers (%zu, %zu)", la,
          lb);
    free(a);
    free(b);

    /* The text export and the screen image leave them out. */
    {
        char p1[1024];
        char p2[1024];
        char err[200];
        char s1[1024];
        char s2[1024];

        snprintf(p1, sizeof p1, "%s/plain1.txt", work);
        snprintf(p2, sizeof p2, "%s/plain2.txt", work);
        snprintf(s1, sizeof s1, "%s/shot1.txt", work);
        snprintf(s2, sizeof s2, "%s/shot2.txt", work);
        CHECK(doc_export_ascii(d, p1, 0, CS_ASCII7, -1, err, sizeof err) == 0,
              "export 1");
        CHECK(doc_export_ascii(m, p2, 0, CS_ASCII7, -1, err, sizeof err) == 0,
              "export 2");
        CHECK(doc_export_ascii(d, s1, 1, CS_ASCII7, -1, err, sizeof err) == 0,
              "screen 1");
        CHECK(doc_export_ascii(m, s2, 1, CS_ASCII7, -1, err, sizeof err) == 0,
              "screen 2");

        for (int k = 0; k < 2; k++) {
            FILE *f1 = fopen(k == 0 ? p1 : s1, "rb");
            FILE *f2 = fopen(k == 0 ? p2 : s2, "rb");
            int c1;
            int c2;
            int ok = 1;
            long len = 0;

            do {
                c1 = fgetc(f1);
                c2 = fgetc(f2);
                ok = ok != 0 && c1 == c2;
                len++;
            } while (c1 != EOF && c2 != EOF);

            fclose(f1);
            fclose(f2);
            CHECK(ok != 0 && len > 50, "%s is the same with markers",
                  k == 0 ? "the text export" : "the screen image");
        }
    }

    marker_print_mode = 2;
    l1 = render_lines(d, 70, &n1);
    l2 = render_lines(m, 70, &n2);
    marker_print_mode = 0;
    same = n1 == n2;

    for (int i = 0; same != 0 && i < n1; i++) {
        same = strcmp(l1[i], l2[i]) == 0;
    }

    CHECK(same != 0, "the printout lines are the same (%d, %d)", n1, n2);
    free_lines(l1, n1);
    free_lines(l2, n2);
    ed_free(e);
    doc_free(d);
    doc_free(m);
}


static void
test_file(const char *work)
{
    char path[1024];
    DiagList l;
    DiagList r;
    Diag v[4];
    int n = four(v);
    int complete = 0;
    int status = 0;

    snprintf(path, sizeof path, "%s/errors.txt", work);
    diag_list_init(&l);
    diag_list_init(&r);
    diag_list_set(&l, v, n, 3);

    /* The text has a percent sign, a space, a newline and a high byte. */
    str_copy(l.v[0].msg, sizeof l.v[0].msg, "100% bad\nsecond line \xe9 end");
    CHECK(diag_file_write(path, &l, 4) == 0, "write the error file");
    CHECK(diag_file_read(path, &r, &complete, &status) == 0, "read it");
    CHECK(complete == 1 && status == 4, "complete, status %d", status);
    CHECK(r.n == 4 && r.dropped == 3, "four records, 3 dropped (%d, %d)", r.n,
          r.dropped);
    CHECK(strcmp(r.v[0].msg, l.v[0].msg) == 0, "the escaping is lossless");
    CHECK(r.v[2].off == 26 && r.v[2].kind == DIAG_WARNING &&
              strcmp(r.v[2].code, "char-missing") == 0,
          "the fields of a record");
    CHECK(r.v[3].item == IT_CHAR_EQ && r.v[3].line == 1 && r.v[3].mtype == 9 &&
              strcmp(r.v[3].text, "a + \\notamacro b") == 0,
          "the position and text of a record");

    /* Every line is plain ASCII and at most 4 KB. */
    {
        FILE *f = fopen(path, "rb");
        int c;
        int bad = 0;
        int lines = 0;

        while ((c = fgetc(f)) != EOF) {
            if (c == '\n') {
                lines++;
            } else if (c < 0x20 || c > 0x7E) {
                bad++;
            }
        }

        fclose(f);
        CHECK(bad == 0, "the file has %d non ASCII bytes", bad);
        CHECK(lines == 6, "six lines: magic, four records, end (%d)", lines);
    }

    /* A file cut short is read as not complete. */
    {
        FILE *f = fopen(path, "r+b");
        char buf[4096];
        size_t got = fread(buf, 1, sizeof buf - 1, f);
        char *end;

        buf[got] = '\0';
        end = strstr(buf, "\nend ");

        fclose(f);
        f = fopen(path, "wb");
        fwrite(buf, 1, lsz(end - buf) + 1, f);
        fclose(f);
        diag_list_clear(&r);
        {
            int rc = diag_file_read(path, &r, &complete, &status);

            CHECK(rc == 0 && complete == 0 && status == -1 && r.n == 4,
                  "a cut file is not complete (%d %d %d %d)", rc, complete,
                  status, r.n);
        }
    }

    /* A file that is not ours is refused. */
    {
        FILE *f = fopen(path, "wb");

        fputs("hello\n", f);
        fclose(f);
        diag_list_clear(&r);
        CHECK(diag_file_read(path, &r, &complete, &status) == -1,
              "a foreign file is refused");
    }

    CHECK(diag_file_read("/nonexistent/errors.txt", &r, &complete, &status) ==
              -1,
          "a missing file");
    diag_list_free(&l);
    diag_list_free(&r);
}


static void
test_tex_log(void)
{
    const char *log =
        "This is pdfTeX\n(./doc.tex\n"
        "! Undefined control sequence.\n"
        "l.12 \\foo\n"
        "         bar\n"
        "! Missing $ inserted.\n"
        "<inserted text> \n"
        "                $\n"
        "l.20 x_\n"
        "! Emergency stop.\n"
        "l.30 end\n";
    const char *tex =
        "one\n%\n%@P 6 0 1\nline of block six\n%\n%@P 8 2 3\nnext\n"
        "after\n";
    TexErr v[10];
    int n = diag_tex_errors(log, v, 10);
    int item = 0;
    int col = 0;
    int line = 0;

    CHECK(n == 2, "two errors of TeX, the emergency stop left out (%d)", n);
    CHECK(n >= 2 && strcmp(v[0].msg, "Undefined control sequence.") == 0 &&
              v[0].line == 12,
          "the first error: %s at line %d", n > 0 ? v[0].msg : "", v[0].line);
    CHECK(n >= 2 && strstr(v[1].msg, "Missing $ inserted") != NULL &&
              v[1].line == 20,
          "the second error: %s at line %d", n > 1 ? v[1].msg : "",
          v[1].line);
    CHECK(diag_tex_errors(log, v, 1) == 1, "the limit is kept");
    CHECK(diag_tex_errors("no errors here\n", v, 10) == 0, "a clean log");

    /* The line of the file is mapped by the last mark before it. */
    CHECK(diag_tex_position(tex, 1, &item, &col, &line) == 0,
          "no mark before line 1");
    CHECK(diag_tex_position(tex, 4, &item, &col, &line) == 1 && item == 6 &&
              col == 0 && line == 1,
          "line 4 is block 6 (%d %d %d)", item, col, line);
    CHECK(diag_tex_position(tex, 7, &item, &col, &line) == 1 && item == 8 &&
              col == 2 && line == 3,
          "line 7 is item 8, column 2, line 3 (%d %d %d)", item, col, line);
    CHECK(diag_tex_position(tex, 8, &item, &col, &line) == 1 && item == 8,
          "a later line keeps the last mark");
}


int
main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "tests/data";
    const char *work = argc > 2 ? argv[2] : tmp_dir();

    test_menu_and_hand(dir);
    test_apply(dir);
    test_find(dir);
    test_unfound(dir);
    test_part(dir);
    test_copy(dir);
    test_save_load(dir, work);
    test_not_printed(dir, work);
    test_file(work);
    test_tex_log();
    printf("perrtest: %d checks, %d failures\n", checks, fails);

    return fails != 0;
}
