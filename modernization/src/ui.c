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

/* ui.c - menus, prompts, help viewer and the main loop. */
#include "util.h"
#include "ui.h"
#include "term.h"
#include "widgets.h"
#include "bordermenu.h"
#include "comparemenu.h"
#include "dictmenu.h"
#include "recoverui.h"
#include "backup.h"
#include "marker.h"
#include "markerui.h"
#include "filemenu.h"
#include "menu.h"
#include "fileops.h"
#include "printmenu.h"
#include "globalmenu.h"
#include "library.h"
#include "macro.h"
#include "eqrender.h"
#include "config.h"
#include "spellui.h"
#include "thesmenu.h"
#include "preview.h"
#include "errlist.h"
#include "perr.h"
#include "undo.h"
#include "log.h"
#include "latex.h"
#include "latexmark.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

/* the running application (for redraw callbacks) */
static App *A;
#define ED (A->ed)


/* =============================================================== drawing */
static int
bottom_rows(void)
{
    return ED->opt_keys ? 2 : ((ED->msg[0]) != '\0' ? 1 : 0);
}


/* The rows that the status line, the expanded line and the ruler take. */
static int
head_rows(const Editor *e)
{
    return 1 + (e->opt_expanded != 0 ? 1 : 0) + (e->opt_ruler != 0 ? 1 : 0);
}


/* 1 while the screen shows two windows (Edit, Window, Open). */
static int
two_windows(void)
{
    return A->nwin == 2 && A->wzoom == 0 && A->ed == A->win[A->wcur];
}


/* The most rows that the first window can take, and the least. */
static int
split_clamped(int avail)
{
    int s = A->wsplit;

    if (avail < 10) {
        return avail / 2;
    }

    return s < 5 ? 5 : s > avail - 5 ? avail - 5 : s;
}


/* The first screen row and the number of rows of the window w. */
static void
win_region(int w, int *top, int *rows)
{
    int avail = term_rows() - bottom_rows();

    if (two_windows() == 0) {
        *top = 0;
        *rows = avail < 1 ? 1 : avail;

        return;
    }

    int s = split_clamped(avail);

    *top = w == 0 ? 0 : s;
    *rows = w == 0 ? s : avail - s;
}


/* The first screen row of the text of the window of the cursor. */
static int
text_y0(void)
{
    int top;
    int rows;

    win_region(two_windows() != 0 ? A->wcur : 0, &top, &rows);

    return top + head_rows(ED);
}


/* The number of rows of the text of the window of the cursor. */
static int
text_rows(void)
{
    int top;
    int rows;

    win_region(two_windows() != 0 ? A->wcur : 0, &top, &rows);

    int r = rows - head_rows(ED);

    return r < 1 ? 1 : r;
}


/*
 * A large document takes time to lay out.  The layout is made again only
 * when something may have changed it: after a key that is not known to
 * leave the layout alone (layout_stale), or when the width changed.  A key
 * that moves the cursor leaves it alone (layout_precise), and a key that
 * types a character marks the one block it changed.  The menus draw the
 * text again and again without changing it.
 */
static int layout_stale = 1;
static int layout_precise;


/* What a layout depends on, besides the text of the blocks. */
typedef struct {
    const Doc *doc;
    unsigned gen;
    int nitems;
    int only_s;
    int hide_title;
    int expanded;
    int marks;
    int dots;
    int unused; /* No padding is left in the structure. */
} LayoutSig;


static LayoutSig
layout_sig(void)
{
    LayoutSig s;

    memset(&s, 0, sizeof s);
    s.doc = ED->doc;
    s.gen = ED->doc->gen;
    s.nitems = ED->doc->nitems;
    s.only_s = ED->only_s;
    s.hide_title = ED->hide_title;
    s.expanded = ED->opt_expanded;
    s.marks = layout_show_marks;
    s.dots = layout_show_dots;

    return s;
}


/*
 * Called after a key was handled: the layout is made again at the next
 * drawing unless the key is known to leave it alone, or nothing it
 * depends on changed (a menu that was left with Esc, say).
 */
static void
layout_after_key(const LayoutSig *before)
{
    LayoutSig now = layout_sig();

    if (layout_precise == 0 && ED->view.partial == 0 &&
        memcmp(before, &now, sizeof now) != 0) {
        layout_stale = 1;
    }

    layout_precise = 0;
    ED->view.partial = 0;
}


/*
 * The window moves to a new top in a few steps when the cursor has gone onto
 * a picture or off one, or down the rows of a tall one: the picture is one
 * line for the cursor, but it is many rows on the screen, and moving the
 * window by all of them in one drawing makes the text leap.  A step moves
 * the window by at most GLIDE_ROWS rows (more only when the distance is so
 * long that GLIDE_FRAMES steps would not reach it), the steps are
 * GLIDE_MS milliseconds apart, so that the whole move takes about 150 ms at
 * the most, and the steps stop at once when a key is waiting: the key is
 * then handled and the window is drawn at its final place.  The file that
 * STRATA_SCROLL_LOG names (a test aid) gets a line for each step, and one
 * that starts with the word abort when a key ended the steps.
 */
#define GLIDE_ROWS 4
#define GLIDE_FRAMES 14
#define GLIDE_MS 10
#define GLIDE_MIN 3


static void
glide_log(const char *what, int from, int to)
{
    static const char *path;
    static int known;

    if (known == 0) {
        path = getenv("STRATA_SCROLL_LOG");
        known = 1;
    }

    if (path == NULL || *path == '\0') {
        return;
    }

    FILE *f = fopen(path, "a");

    if (f != NULL) {
        fprintf(f, "%s%d %d\n", what, from, to);
        fclose(f);
    }
}


static void
glide(int from, int W)
{
    int target = ED->top;
    int dist = target > from ? target - from : from - target;

    if (ED->glide == 0) {
        return;
    }

    ED->glide = 0;

    if (dist < GLIDE_MIN) {
        return;
    }

    int frames = (dist + GLIDE_ROWS - 1) / GLIDE_ROWS;

    if (frames > GLIDE_FRAMES) {
        frames = GLIDE_FRAMES;
    }

    int step = (dist + frames - 1) / frames;
    int dir = target > from ? 1 : -1;
    int shown = from;

    for (int f = 1; f < frames; f++) {
        if (term_key_pending() != 0) {
            glide_log("abort ", shown, target);
            break;
        }

        int next = from + dir * step * f;

        if ((dir > 0 && next > target) || (dir < 0 && next < target)) {
            break;
        }

        ED->top = next;
        term_clear();
        ed_draw(ED, 0, text_y0(), W, text_rows());
        ed_status(ED, W);
        term_cursor(0, 0, 0);
        term_present();
        glide_log("", shown, next);
        shown = next;

        struct timespec ts = {0, GLIDE_MS * 1000000L};

        nanosleep(&ts, NULL);
    }

    ED->top = target;
    glide_log("", shown, target);
}


/* The screen with two windows: each one has its status line and its text. */
static void
draw_windows(void)
{
    int W = term_cols();
    int tw = W < 80 ? W : 80;
    int x;
    Editor *cur = A->win[A->wcur];

    if (tw < 40) {
        tw = 40;
    }

    term_clear();

    for (int w = 0; w < 2; w++) {
        Editor *e = A->win[w];
        int top;
        int rows;
        int hdr = head_rows(e);

        win_region(w, &top, &rows);

        int trows = rows - hdr < 1 ? 1 : rows - hdr;

        if (layout_stale != 0 || e->text_width != tw) {
            ed_invalidate(e);
        }

        e->text_width = tw;
        ed_layout(e);
        ed_scroll_to_cursor(e, trows);
        ed_draw(e, 0, top + hdr, W, trows);
        ed_status_at(e, W, top, w == A->wcur);
    }

    layout_stale = 0;

    int line = ed_cursor_screen(cur, &x, NULL);

    if (line >= 0) {
        term_cursor(x, text_y0() + line - cur->top, 1);
    } else {
        term_cursor(0, 0, 0);
    }
}


static void
draw_base(void)
{
    if (two_windows() != 0) {
        draw_windows();

        return;
    }

    int W = term_cols();
    int old_width = ED->text_width;

    ED->text_width = W < 80 ? W : 80;
    if (ED->text_width < 40) {
        ED->text_width = 40;
    }

    if (layout_stale != 0 || old_width != ED->text_width) {
        ed_invalidate(ED);
    }

    layout_stale = 0;
    ed_layout(ED);

    int from = ED->top;

    ed_scroll_to_cursor(ED, text_rows());
    glide(from, W);
    term_clear();
    ed_draw(ED, 0, text_y0(), W, text_rows());
    ed_status(ED, W);
    int x;
    int line = ed_cursor_screen(ED, &x, NULL);

    if (line >= 0) {
        term_cursor(x, text_y0() + line - ED->top, 1);
    } else {
        term_cursor(0, 0, 0);
    }
}


/* A drawing that takes longer than this (in milliseconds) is logged by a
   debug build: the keys that wait behind it make the cursor lag. */
#define SLOW_DRAW_MS 30


static void
redraw(void)
{
    struct timespec t0;
    struct timespec t1;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    draw_base();
    term_present();
    clock_gettime(CLOCK_MONOTONIC, &t1);

    long ms = (t1.tv_sec - t0.tv_sec) * 1000L +
              (t1.tv_nsec - t0.tv_nsec) / 1000000L;

    if (ms >= SLOW_DRAW_MS) {
        log_debug("stred: drawing the screen took %ld ms: the keys that "
                  "arrive meanwhile wait behind it", ms);
    }
}


/* =============================================================== helpers */
static void
sel_start(int kind, int op, int move)
{
    ed_sel_begin(ED, kind);
    A->pending_op = op;
    A->pending_move = move;
    ed_msg(ED, "Highlight the area with the cursor keys, then press Enter (Esc "
               "cancels)");
}


static void
choose_unit_and_select(const char *title, int op, int move, int allow_section)
{
    const char *it[] = {"Text", "Column", "Block", "Section"};
    int n = allow_section != 0 && ED->doc->structured != 0 ? 4 : 3;
    int c = popup_menu(title, it, n, 0);

    if (c < 0) {
        return;
    }

    sel_start(c == 0 ? SEL_TEXT :
              c == 1 ? SEL_COLUMN :
              c == 2 ? SEL_BLOCK :
                       SEL_SECTION,
              op, move);
}


/*
 * Case (Ctrl-F6): the box prompts for what to do, then the unit is chosen and
 * highlighted as for Copy; Enter changes the case.  An empty highlight of
 * the kind Text takes the word under the cursor.
 */
static void
cmd_case(void)
{
    const char *it[] = {"Upper", "Lower", "Capitalize"};
    int c = popup_menu("Case", it, 3, 0);

    if (c < 0) {
        return;
    }

    A->case_mode = c == 0 ? CASE_UPPER : c == 1 ? CASE_LOWER : CASE_CAPITALIZE;
    choose_unit_and_select("Case of", OP_CASE, 0, 1);
}


/* Save at once under the document's name (Ctrl-S); see filemenu.c. */
static int
do_save(int as UNUSED)
{
    return filemenu_quick_save(ED);
}


static int
confirm_discard(void)
{
    if (ED->doc->modified == 0) {
        return 1;
    }

    /* Quitting is the only reason to ask, so the answers say so. */
    const char *it[] = {"Save and quit", "Discard changes and quit",
                        "Cancel"};
    int c = popup_menu("Document has unsaved changes", it, 3, 0);

    if (c == 0) {
        return do_save(0) == 0;
    }

    return c == 1;
}


/* ======================================================= Edit, Window */
/* Ask what to do with the unsaved changes of the window of the cursor. */
static int
confirm_close_window(void)
{
    if (ED->doc->modified == 0) {
        return 1;
    }

    const char *it[] = {"Save and close", "Discard changes and close",
                        "Cancel"};
    int c = popup_menu("Document has unsaved changes", it, 3, 0);

    if (c == 0) {
        return do_save(0) == 0;
    }

    return c == 1;
}


/* Edit, Window, Open: a second window with a new document. */
static void
window_open(void)
{
    Editor *ne;

    if (A->nwin == 2) {
        ed_msg(ED, "Two windows are open already");

        return;
    }

    ne = ed_new(doc_new(0));
    ne->opt_expanded = ED->opt_expanded;
    ne->opt_ruler = ED->opt_ruler;
    ne->opt_keys = ED->opt_keys;
    ne->text_width = ED->text_width;
    ne->hide_title = ED->hide_title;
    ed_clip_move(ED, ne);
    A->win[0] = ED;
    A->win[1] = ne;
    A->nwin = 2;
    A->wcur = 1;
    A->wzoom = 0;
    A->wsplit = (term_rows() - bottom_rows()) / 2;
    A->ed = ne;
    ed_fix_cursor(ne);
    layout_stale = 1;
    ed_msg(ne, "Window opened: retrieve or create a document in it");
}


/* Edit, Window, Close: the window of the cursor goes. */
static void
window_close(void)
{
    Editor *gone = ED;
    Editor *keep;

    if (A->nwin != 2) {
        ed_msg(ED, "There is one window only");

        return;
    }

    if (confirm_close_window() == 0) {
        return;
    }

    keep = A->win[A->wcur ^ 1];
    ed_clip_move(gone, keep);
    A->ed = keep;
    A->win[0] = keep;
    A->win[1] = NULL;
    A->nwin = 1;
    A->wcur = 0;
    A->wzoom = 0;
    doc_free(gone->doc);
    ed_free(gone);
    layout_stale = 1;
    ed_msg(keep, "Window closed");
}


/* Edit, Window, Switch: the cursor goes to the other window. */
static void
window_switch(void)
{
    Editor *from = ED;

    if (A->nwin != 2) {
        ed_msg(ED, "There is one window only");

        return;
    }

    A->wcur ^= 1;
    A->ed = A->win[A->wcur];
    ed_clip_move(from, A->ed);
    layout_stale = 1;
}


/* Edit, Window, Adjust: Up and Down change the size of the windows. */
static void
window_adjust(void)
{
    int old = A->wsplit;
    int avail = term_rows() - bottom_rows();

    if (A->nwin != 2 || A->wzoom != 0) {
        ed_msg(ED, "Adjust needs two windows that share the screen");

        return;
    }

    A->wsplit = split_clamped(avail);
    ed_msg(ED, "Up and Down change the size of the windows; Enter accepts, Esc "
               "cancels");

    for (;;) {
        redraw();

        int k = term_getkey(-1);
        int base = K_BASE(k);

        if (k == K_RESIZE || k == K_NONE) {
            continue;
        }

        if (base == K_UP) {
            A->wsplit = split_clamped(avail) - 1;
            A->wsplit = split_clamped(avail);
        } else if (base == K_DOWN) {
            A->wsplit = split_clamped(avail) + 1;
            A->wsplit = split_clamped(avail);
        } else if (base == 13 || base == 10) {
            break;
        } else if (base == K_ESC) {
            A->wsplit = old;

            break;
        }
    }

    ED->msg[0] = '\0';
    layout_stale = 1;
}


/* The entries of Edit, Window, in the order of the menu. */
enum {
    WIN_OPEN,
    WIN_CLOSE,
    WIN_ADJUST,
    WIN_ZOOM_UP,
    WIN_ZOOM_DOWN,
    WIN_SWITCH,
    WIN_COUNT
};


static void
run_window(int i)
{
    switch (i) {
        case WIN_OPEN:
            window_open();
            break;

        case WIN_CLOSE:
            window_close();
            break;

        case WIN_ADJUST:
            window_adjust();
            break;

        case WIN_ZOOM_UP:
            A->wzoom = 1;
            layout_stale = 1;
            break;

        case WIN_ZOOM_DOWN:
            A->wzoom = 0;
            layout_stale = 1;
            break;

        default:
            window_switch();
            break;
    }
}


static void
load_into_editor(Doc *d)
{
    Doc *old = ED->doc;

    ed_set_doc(ED, d);
    ed_restore_position(ED);
    doc_free(old);
}


static int
do_retrieve(const char *given)
{
    char name[512] = "";
    char err[128];

    if (given != NULL) {
        snprintf(name, sizeof name, "%s", given);
    } else {
        /* list *.doc in the current directory */
        DIR *dp = opendir(".");
        char **names = NULL;
        int cap = 0;
        int n = 0;

        if (dp != NULL) {
            struct dirent *de;

            while ((de = readdir(dp)) != NULL) {
                size_t l = strlen(de->d_name);

                if (l > 4 && strcasecmp(de->d_name + l - 4, ".doc") == 0) {
                    if (n == cap) {
                        char **grown = realloc(names, usz(cap + 256) *
                                                          sizeof(char *));

                        if (grown == NULL) {
                            break;
                        }

                        names = grown;
                        cap += 256;
                    }

                    names[n++] = strdup(de->d_name);
                }
            }

            closedir(dp);
        }

        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                if (strcmp(names[j], names[i]) < 0) {
                    char *t = names[i];

                    names[i] = names[j];
                    names[j] = t;
                }
            }
        }

        const char **it = malloc(usz((n > 0 ? n + 2 : 2)) * sizeof(char *));

        for (int i = 0; i < n; i++) {
            it[i] = names[i];
        }

        it[n] = "(type a "
                "name...)";
        int c = popup_menu("Retrieve Document", it, n + 1, 0);

        if (c >= 0 && c < n) {
            snprintf(name, sizeof name, "%s", names[c]);
        } else if (c == n) {
            if (popup_name_input("Retrieve", "File name:", name,
                                 sizeof name) == 0) {
                name[0] = 0;
            }
        }

        for (int i = 0; i < n; i++) {
            free(names[i]);
        }

        free(names);
        free(it);
    }

    if (name[0] == '\0') {
        return -1;
    }

    doc_name_complete(name, sizeof name);

    int rec = -1;
    Doc *d = recoverui_load(name, err, sizeof err, "Cannot retrieve", &rec);

    if (d == NULL) {
        popup_msg("Cannot retrieve", err);
        return -1;
    }

    load_into_editor(d);

    if (rec >= 0) {
        ed_msg(ED, "Recovered %d blocks of %s; the original is not changed",
               rec, name);
    } else {
        ed_msg(ED, "Retrieved %s", name);
    }

    return 0;
}


static void
do_new(void)
{
    filemenu_new(ED);
}


/* Tools, Thesaurus and Ctrl-T: look up the word of the cursor. */
static void
do_thesaurus(void)
{
    int line;

    ed_layout(ED);
    line = ed_cursor_screen(ED, NULL, NULL);
    thesmenu_lookup(ED, line >= 0 ? text_y0() + line - ED->top : 0);
}


static void
do_count(void)
{
    int w;
    int ch;
    int b;
    int s;
    char m[128];

    ed_doc_stats(ED->doc, &w, &ch, &b, &s);
    snprintf(m, sizeof m, "Total number of words in document: %d", w);
    popup_msg("Word Count", m);
}


/*
 * Tools, Spell: Document checks all of the text, Word the word at the cursor.
 * Block and Section ask for the area to be highlighted first;
 * Enter then starts the check (finish_selection).
 */
static void
do_spell(int what)
{
    if (what == 0) {
        spellui_check(ED, 0, ED->doc->nitems - 1);
    } else if (what == 3) {
        spellui_word(ED);
    } else if (is_edit_block(ED->doc, ED->cur.item) == 0) {
        ed_msg(ED, "The cursor is not in a block");
    } else {
        ed_sel_begin(ED, what == 1 ? SEL_BLOCK : SEL_SECTION);
        A->pending_op = OP_SPELL;
        A->pending_move = 0;
        ed_msg(ED, what == 1 ?
                       "Use arrow keys to highlight block(s); then press "
                       "Enter." :
                       "Use arrow keys to highlight section(s); then press "
                       "Enter.");
    }
}


/* Autosort, Table: highlight blocks, then sort them. */
static void
autosort_table(void)
{
    if (is_edit_block(ED->doc, ED->cur.item) == 0) {
        ed_msg(ED, "The cursor is not in a block");

        return;
    }

    ED->sort_descending = 0;
    ed_sel_begin(ED, SEL_BLOCK);
    A->pending_op = OP_SORT;
    A->pending_move = 0;
    ed_msg(ED, "Use arrow keys to highlight block(s); then press Enter.  "
               "Home, End: begin or end of section.  +, -: change "
               "direction");
}


/* block / column format */
static void
set_prop(const char *code, char sub, const char *val)
{
    Doc *d = ED->doc;
    Block *b = d->items[ED->cur.item].blk;

    if (b->ncols > 1) {
        char cc[3] = {'C', code[1], 0};

        props_set(&b->cols[ED->cur.col].props, cc, sub, val);
    } else {
        props_set(&b->props, code, sub, val);
    }
}


static void
block_format_editor(void)
{
    Doc *d = ED->doc;

    if (is_edit_block(d, ED->cur.item) == 0) {
        return;
    }

    Fmt f;

    fmt_resolve(d, ED->cur.item, ED->cur.col, &f);
    char buf[96];
    char v[96];

    snprintf(buf, sizeof buf, "%d", f.bl);

    if (popup_input("Block Format Editor",
                    "L: left block indent (ruler "
                    "units):",
                    buf, sizeof buf) == 0) {
        return;
    }

    int bl = atoi(buf);

    snprintf(v, sizeof v, "%d", bl);
    const char *js[] = {"Left", "Right", "Center", "Even"};
    int j = popup_menu("Justification", js, 4, f.just);

    if (j < 0) {
        return;
    }

    const char *ind[] = {"None", "First", "All but First", "All"};
    int ii = popup_menu("Indent", ind, 4, f.indent);

    if (ii < 0) {
        return;
    }

    const char *at[] = {"Normal", "Bold", "Italics", "Bold Italics",
                        "Underscore Words"};
    int amap[] = {0, 1, 4, 5, 2};
    int ai = 0;

    for (int i = 0; i < 5; i++) {
        if (amap[i] == f.attr) {
            ai = i;
        }
    }

    int a = popup_menu("Attributes", at, 5, ai);

    if (a < 0) {
        return;
    }

    buf[0] = 0;

    for (int t = 0; t < f.ntab; t++) {
        char tmp[16];

        snprintf(tmp, sizeof tmp, "%s%d%s", t != 0 ? "," : "", f.tabpos[t],
                 f.tabtype[t] == 'L' ? "" : (char[2]) {f.tabtype[t], 0});
        strcat(buf, tmp);
    }

    if (popup_input("Block Format Editor",
                    "Tabs (e.g. 5,20R,40D; 0 = none):", buf, sizeof buf) == 0) {
        return;
    }

    Block *b = d->items[ED->cur.item].blk;

    if (b->ncols == 1) {
        props_set(&b->props, "BL", 0, v);
    }

    snprintf(v, sizeof v, "%d", j);
    set_prop("BF", 'J', v);
    snprintf(v, sizeof v, "%d", ii);
    set_prop("BF", 'I', v);
    snprintf(v, sizeof v, "%d", amap[a]);
    set_prop("BF", 'A', v);
    set_prop("BF", 'T', buf[0] != '\0' ? buf : "0");
    b->dirty = 1;
    doc_touch(d);
    ed_invalidate(ED);
}


static void
hanging_indent(void)
{
    int x;

    if (ed_cursor_screen(ED, &x, NULL) < 0) {
        return;
    }

    BView *bv = &ED->view.bv[ED->cur.item];
    int pos = x - bv->colx[ED->cur.col];
    Fmt f;

    fmt_resolve(ED->doc, ED->cur.item, ED->cur.col, &f);
    pos -= f.bl;

    if (pos < 1) {
        ed_msg(ED, "Type the hanging characters first, then press "
                   "Ctrl-H");
        return;
    }

    char v[16];

    snprintf(v, sizeof v, "%d", pos);
    set_prop("BF", 'T', v);
    set_prop("BF", 'I', "2");
    ED->doc->items[ED->cur.item].blk->dirty = 1;
    doc_touch(ED->doc);
    ed_invalidate(ED);
    ed_msg(ED, "Hanging indent set at %d", pos);
}


/*
 * Move the section of the cursor one level: right (demote) makes it the
 * last subsection of the section before it; left (promote) makes it a
 * peer of its parent, placed after it, and the sections that followed it
 * become its subsections.  It is done by editing the + and - items; the
 * items of the section stay where they are.
 */
static void
section_move_level(int right)
{
    Doc *d = ED->doc;

    if (d->structured == 0) {
        return;
    }

    int s = sect_start(d, ED->cur.item);

    if (s < 0) {
        return;
    }

    int off = ED->cur.item - s;
    int e = sect_end(d, s);
    int hb = sect_blocks_end(d, s);
    int ns = s;

    if (right != 0) {
        if (s < 2 || (d->items[s - 1].type != IT_MINUS &&
                      d->items[s - 1].type != IT_BLOCK)) {
            ed_msg(ED, "No previous "
                       "section to become a subsection of");
            return;
        }

        if (d->items[s - 1].type == IT_MINUS) {
            /* The group of the previous peer takes the section in. */
            doc_delete_items(d, s - 1, 1);
            doc_insert_item(d, e - 1, IT_MINUS, NULL);
            ns = s - 1;
        } else {
            int ps = sect_start(d, s - 1);

            if (ps < 0 || doc_item_level(d, ps) != doc_item_level(d, s)) {
                ed_msg(ED, "No previous peer section");
                return;
            }

            doc_insert_item(d, s, IT_PLUS, NULL);
            doc_insert_item(d, e + 1, IT_MINUS, NULL);
            ns = s + 1;
        }
    } else {
        if (doc_item_level(d, s) < 2) {
            ed_msg(ED, "The section cannot move left");
            return;
        }

        /* The item that closes the group the section is in. */
        int g = e;
        int depth = 0;

        while (g < d->nitems) {
            if (d->items[g].type == IT_PLUS) {
                depth++;
            } else if (d->items[g].type == IT_MINUS) {
                if (depth == 0) {
                    break;
                }

                depth--;
            }

            g++;
        }

        if (g >= d->nitems) {
            ed_msg(ED, "The section cannot move left");
            return;
        }

        int has_children = hb < e;

        if (e == g) {
            /* Nothing follows: the group is closed before the section. */
            doc_delete_items(d, g, 1);
        } else if (has_children) {
            /* The following sections join the subsections. */
            doc_delete_items(d, e - 1, 1);
        } else {
            /* The following sections become the subsections. */
            doc_insert_item(d, hb, IT_PLUS, NULL);
        }

        if (d->items[s - 1].type == IT_PLUS) {
            /* The group had only this section and its followers. */
            doc_delete_items(d, s - 1, 1);
            ns = s - 1;
        } else {
            doc_insert_item(d, s, IT_MINUS, NULL);
            ns = s + 1;
        }
    }

    ED->cur.item = ns + off;
    doc_touch(ED->doc);
    ed_invalidate(ED);
    ed_fix_cursor(ED);
}


/* =============================================================== menus */
static void
cmd_append(void)
{
    const char *it[] = {"Column", "Block", "Global-block", "Peer-section",
                        "Sub-section"};
    int c = popup_menu("Append", it, ED->doc->structured ? 5 : 3, 1);

    switch (c) {
        case 0:
            ed_append_column(ED);
            break;

        case 1:
            ed_append_block(ED, 0);
            break;

        case 2:
            ed_append_block(ED, 1);
            break;

        case 3:
            ed_append_section(ED, 0);
            break;

        case 4:
            ed_append_section(ED, 1);
            break;

        default:
            break;
    }
}


static void
cmd_split(void)
{
    const char *it[] = {"Column", "Block", "Section"};
    int c = popup_menu("Split", it, ED->doc->structured ? 3 : 2, 1);

    if (c >= 0) {
        ed_split(ED, c);
    }
}


static void
cmd_join(void)
{
    const char *it[] = {"Block", "Section"};
    int c = popup_menu("Join", it, ED->doc->structured ? 2 : 1, 0);

    if (c >= 0 && ed_join(ED, c + 1) != 0) {
        if (ED->msg[0] == '\0') {
            ed_msg(ED, "Cannot "
                       "join here");
        }
    }
}


/* The Print Error under the cursor is shown, and cannot be changed. */
static void
show_print_error(const Tok *k)
{
    char text[DIAG_MSG + 120];

    perr_text(k, text, sizeof text);
    popup_msg("Print Error (read only; Del removes it)", text);
}


/* Find the next or the previous Print Error: the search method is set. */
static void
find_error(int forward)
{
    ED->search_method = 1;
    ED->search_back = forward == 0;
    perr_find(ED, forward);
}


static void
remove_error_here(void)
{
    if (perr_remove_here(ED) != 0) {
        ed_msg(ED, "Print Error removed");
    } else {
        ed_msg(ED, "The cursor is not on a Print Error");
    }
}


static void
remove_all_errors(void)
{
    int n = perr_remove_all(ED);

    if (n == 0) {
        ed_msg(ED, "No print errors");
    } else {
        ed_msg(ED, "%d print error%s removed", n, n == 1 ? "" : "s");
    }
}


/* The words that say what a search looked for, for the messages. */
static const char *const find_names[SF_COUNT] = {
    "text", "a tab", "an end of line", "a no-break space", "a marker",
    "a soft hyphen"};
static const char *const attr_names[SA_COUNT] = {
    "text", "plain text", "bold text", "italic text", "underscored text"};


/* What the user would call the thing that is searched for. */
static void
spec_desc(const SearchSpec *s, char *out, size_t cap)
{
    if (s->o.find != SF_TEXT) {
        str_copy(out, cap, find_names[s->o.find]);
    } else if (s->pattern[0] == '\0') {
        str_copy(out, cap, attr_names[s->o.attr]);
    } else {
        str_copy(out, cap, s->pattern);
    }
}


/* The specification of a search from the options that Search keeps. */
static void
search_spec_of(SearchSpec *s)
{
    s->o = ED->sopt;
    s->pattern = ED->search;
}


/* Whether the specification looks for something at all. */
static int
spec_is_empty(const SearchSpec *s)
{
    return s->o.find == SF_TEXT && s->pattern[0] == '\0' &&
           s->o.attr == SA_ANY;
}


/* Search again: the method and the direction of the last Search. */

/* The message of a search that found a place: it shows when it wrapped. */
static void
found_msg(void)
{
    if (ED->search_wrapped == 0) {
        ed_msg(ED, "Found");
    } else if (ED->search_back == 0) {
        ed_msg(ED, "Found: the search wrapped to the start of the document");
    } else {
        ed_msg(ED, "Found: the search wrapped to the end of the document");
    }
}

static void
search_again(void)
{
    SearchSpec s;
    char what[96];

    if (ED->search_method == 1) {
        perr_find(ED, ED->search_back == 0);

        return;
    }

    search_spec_of(&s);

    if (spec_is_empty(&s) != 0) {
        ed_msg(ED, "Nothing to search for: use Search (F9) first");

        return;
    }

    if (ed_search_spec(ED, &s, ED->search_back == 0) == 0) {
        spec_desc(&s, what, sizeof what);
        ed_msg(ED, "Not found: %s", what);
    } else {
        found_msg();
    }
}


/* The choices of Find, in the Search panel; the second is the Print Error. */
static const char *const search_finds[] = {
    "Text", "Print Error", "Tab", "End of Line", "Nonbreaking Space", "Marker",
    "Soft Hyphen", NULL};
static const char *const attr_choices[] = {
    "Any", "Plain", "Bold", "Italics", "Underscore", NULL};
static const char *const no_yes[] = {"No", "Yes", NULL};


/* The place of what is searched for in the list of the Find field. */
static int
find_index(int method, int find)
{
    return method == 1 ? 1 : find == SF_TEXT ? 0 : find + 1;
}


/*
 * The Search panel: what to search for, what to find (the text, a Print
 * Error, a tab, an end of line, a no-break space or a marker), the
 * direction, the case, the whole word and the attribute.  The method, the
 * direction and the options are kept for Search again (Ctrl-F9).
 */
static void
cmd_search_panel(void)
{
    static const char *const dirs[] = {"Forward", "Backward", NULL};
    WField f[6];

    memset(f, 0, sizeof f);
    f[0].label = "Search for";
    f[0].kind = WF_TEXT;
    snprintf(f[0].val, sizeof f[0].val, "%s", ED->search);
    f[1].label = "Find";
    f[1].kind = WF_CHOICE;
    f[1].choices = search_finds;
    f[1].sel = find_index(ED->search_method, ED->sopt.find);
    f[2].label = "Direction";
    f[2].kind = WF_CHOICE;
    f[2].choices = dirs;
    f[2].sel = ED->search_back;
    f[3].label = "Match Case";
    f[3].kind = WF_CHOICE;
    f[3].choices = no_yes;
    f[3].sel = ED->sopt.match_case;
    f[3].dep = 2;
    f[3].dep_sel = 0;
    f[4].label = "Whole Word";
    f[4].kind = WF_CHOICE;
    f[4].choices = no_yes;
    f[4].sel = ED->sopt.whole_word;
    f[4].dep = 2;
    f[4].dep_sel = 0;
    f[5].label = "Attribute";
    f[5].kind = WF_CHOICE;
    f[5].choices = attr_choices;
    f[5].sel = ED->sopt.attr;

    if (popup_form("Search", f, 6, NULL) == 0) {
        return;
    }

    ED->search_method = f[1].sel == 1;
    ED->sopt.find = f[1].sel <= 1 ? SF_TEXT : f[1].sel - 1;
    ED->search_back = f[2].sel;
    ED->sopt.match_case = f[3].sel;
    ED->sopt.whole_word = f[4].sel;
    ED->sopt.attr = f[5].sel;

    if (f[0].val[0] != '\0') {
        str_copy(ED->search, sizeof ED->search, f[0].val);
    }

    if (ED->search_method == 0) {
        SearchSpec s;

        search_spec_of(&s);

        if (ED->sopt.find == SF_TEXT && f[0].val[0] == '\0') {
            /* An empty string: the attribute only, or nothing. */
            s.pattern = "";

            if (s.o.attr == SA_ANY) {
                return;
            }

            if (ed_search_spec(ED, &s, ED->search_back == 0) == 0) {
                ed_msg(ED, "Not found: %s", attr_names[s.o.attr]);
            } else {
                found_msg();
            }

            return;
        }
    }

    search_again();
}


/* The key that answers a question about a place of Replace. */
static int
replace_key(void)
{
    for (;;) {
        redraw();

        int k = term_getkey(-1);

        if (k == K_RESIZE || k == K_NONE) {
            continue;
        }

        if (K_BASE(k) == K_ESC) {
            return 'Q';
        }

        if (K_BASE(k) == 13 || K_BASE(k) == K_INS) {
            return 'R';
        }

        if (K_BASE(k) < 128) {
            int c = to_upper(K_BASE(k));

            if (c == 'R' || c == 'O' || c == 'S' || c == 'A' || c == 'Q') {
                return c;
            }
        }
    }
}


/*
 * Where the replacements before the start of a run move the start: a
 * replacement of len tokens by lb tokens in the column of the start, before
 * it, moves it by the difference; one that covers it puts it after the
 * replacement.
 */
static void
start_follows(Pos *start, const Pos *at, int len, int lb)
{
    if (at->item != start->item || at->col != start->col ||
        at->tok >= start->tok) {
        return;
    }

    if (at->tok + len <= start->tok) {
        start->tok += lb - len;
    } else {
        start->tok = at->tok + lb;
    }
}


/*
 * Replace from the cursor, asking at each place: R replaces and goes on, O
 * replaces and stops, S leaves the place, A replaces this one and all the
 * others without asking, Q stops.  At the end of the document the run goes
 * on from the beginning and stops where it started, so every place of the
 * document is looked at once.  The whole run is one operation of Undo,
 * since it is one command.
 */
static void
replace_run(const SearchSpec *s, const char *rep, int follow)
{
    Pos start = ED->cur;
    Pos pos = ED->cur;
    Pos at;
    Pos last;
    int len;
    int count = 0;
    int all = 0;
    int stopped = 0;
    int wrapped = 0;
    int before_wrap = 0;
    char what[96];

    spec_desc(s, what, sizeof what);
    memset(&last, 0, sizeof last);

    for (;;) {
        const Pos *limit = wrapped != 0 ? &start : NULL;
        int key = 'R';
        int lb;

        if (ed_find_range(ED, s, &pos, limit, &at, &len) == 0) {
            if (wrapped != 0 || stopped != 0) {
                break;
            }

            /* The end of the document: go on from its beginning. */
            wrapped = 1;
            before_wrap = count;
            memset(&pos, 0, sizeof pos);

            continue;
        }

        if (all == 0) {
            ed_goto(ED, at.item, at.col, at.tok);
            ed_msg(ED, "Replace?  R replace  O once  S skip  A all  Q quit%s",
                   wrapped != 0 ? "  (wrapped to the start)" : "");
            key = replace_key();
        }

        if (key == 'Q') {
            stopped = 1;
            break;
        }

        if (key == 'S') {
            pos = at;
            pos.tok = at.tok + len;
            continue;
        }

        lb = ed_replace_at(ED, &at, len, rep, follow, &pos);

        if (wrapped != 0) {
            start_follows(&start, &at, len, lb);
        }

        last = pos;
        count++;

        if (key == 'A') {
            all = 1;
        } else if (key == 'O') {
            break;
        }
    }

    if (count != 0) {
        ed_goto(ED, last.item, last.col, last.tok);
    }

    if (count == 0 && stopped == 0) {
        ed_msg(ED, "Not found: %s", what);
    } else {
        ed_msg(ED, "%d replacement%s%s%s", count, count == 1 ? "" : "s",
               wrapped != 0 && count > before_wrap ?
                   " (wrapped to the start)" : "",
               stopped != 0 ? " (stopped)" : "");
    }
}


/*
 * The Replace panel: the string and its replacement, what to find (the text,
 * a tab or a no-break space), the case, the whole word, the attribute, the
 * case of the replacement and whether to ask at each place.
 */
static void
cmd_replace_panel(void)
{
    static const char *const finds[] = {"Text", "Tab", "Nonbreaking Space",
                                        "Soft Hyphen", NULL};
    static const char *const follows[] = {"As Typed", "Follow Found", NULL};
    WField f[8];
    SearchSpec s;
    char rep[80];

    memset(f, 0, sizeof f);
    f[0].label = "Search for";
    f[0].kind = WF_TEXT;
    snprintf(f[0].val, sizeof f[0].val, "%s", ED->search);
    f[1].label = "Replace with";
    f[1].kind = WF_TEXT;
    snprintf(f[1].val, sizeof f[1].val, "%s", ED->replace);
    f[2].label = "Find";
    f[2].kind = WF_CHOICE;
    f[2].choices = finds;
    f[2].sel = ED->ropt.find == SF_TAB    ? 1 :
               ED->ropt.find == SF_NBSP   ? 2 :
               ED->ropt.find == SF_SHY    ? 3 :
                                            0;
    f[3].label = "Match Case";
    f[3].kind = WF_CHOICE;
    f[3].choices = no_yes;
    f[3].sel = ED->ropt.match_case;
    f[3].dep = 3;
    f[3].dep_sel = 0;
    f[4].label = "Whole Word";
    f[4].kind = WF_CHOICE;
    f[4].choices = no_yes;
    f[4].sel = ED->ropt.whole_word;
    f[4].dep = 3;
    f[4].dep_sel = 0;
    f[5].label = "Attribute";
    f[5].kind = WF_CHOICE;
    f[5].choices = attr_choices;
    f[5].sel = ED->ropt.attr;
    f[6].label = "Replacement Case";
    f[6].kind = WF_CHOICE;
    f[6].choices = follows;
    f[6].sel = ED->rep_follow;
    f[7].label = "Ask at Each";
    f[7].kind = WF_CHOICE;
    f[7].choices = no_yes;
    f[7].sel = ED->rep_ask;

    if (popup_form("Replace", f, 8, NULL) == 0) {
        return;
    }

    str_copy(ED->search, sizeof ED->search, f[0].val);
    str_copy(ED->replace, sizeof ED->replace, f[1].val);
    ED->ropt.find = f[2].sel == 1 ? SF_TAB :
                    f[2].sel == 2 ? SF_NBSP :
                    f[2].sel == 3 ? SF_SHY :
                                    SF_TEXT;
    ED->ropt.match_case = f[3].sel;
    ED->ropt.whole_word = f[4].sel;
    ED->ropt.attr = f[5].sel;
    ED->rep_follow = f[6].sel;
    ED->rep_ask = f[7].sel;
    str_copy(rep, sizeof rep, f[1].val);
    s.o = ED->ropt;
    s.pattern = ED->search;

    if (ED->ropt.find == SF_TEXT && ED->search[0] == '\0') {
        ed_msg(ED, "Replace needs the text to search for");

        return;
    }

    if (ED->rep_ask != 0) {
        replace_run(&s, rep, ED->rep_follow);
    } else {
        int n = ed_replace_all_spec(ED, &s, rep, ED->rep_follow);
        char what[96];

        if (n == 0) {
            spec_desc(&s, what, sizeof what);
            ed_msg(ED, "Not found: %s", what);
        } else {
            ed_msg(ED, "%d replacement%s", n, n == 1 ? "" : "s");
        }
    }
}


static void
cmd_search(int replace)
{
    if (replace == 0) {
        cmd_search_panel();
    } else {
        cmd_replace_panel();
    }
}


static void
cmd_goto(void)
{
    const char *it[] = {"Document start", "Document end", "Section number...",
                        "Next section", "Previous section"};
    int c = popup_menu("Go To", it, 5, 0);

    if (c == 0) {
        ed_move(ED, MV_DOC_START, 0);
    } else if (c == 1) {
        ed_move(ED, MV_DOC_END, 0);
    } else if (c == 3) {
        ed_move(ED, MV_SECT_NEXT_HEAD, 0);
    } else if (c == 4) {
        ed_move(ED, MV_SECT_HEAD, 0);
    } else if (c == 2) {
        char b[32] = "";

        if (popup_input("Go To Section", "Section number (e.g. 2.1):", b,
                        sizeof b) == 0) {
            return;
        }

        int item = sect_find(ED->doc, b);

        if (item >= 0) {
            ED->cur.item = item;
            ED->cur.col = 0;
            ED->cur.tok = 0;
            ed_fix_cursor(ED);
            return;
        }

        ed_msg(ED, "No section %s", b);
    }
}


static void
handle_key(int k);


static void
toggle_section_only(void);


/* The title of the text editor that is open inside a marker, or NULL. */
static const char *nested_title;


/*
 * The note editor.  The blocks of a note, comment or equation are loaded as a
 * document of their own and edited with the same editor: the status line
 * says NOTE EDIT, Ins keeps the changes and Esc goes back to the text as it
 * was.
 */
static int
note_editor(const char *title, const uint8_t *in, size_t inlen, uint8_t **out,
            size_t *outlen)
{
    static const char head[] = DOC_FIRST_RECORD "\r\nMR1006\r\n*B0\r\n";
    static const char blank[] = "S\r\nT\x1d\x00\r\n";
    static const char foot[] = "*E\r\n";
    char err[96];
    size_t hl = sizeof head - 1;
    size_t bl = inlen > 0 ? inlen : sizeof blank - 1;
    uint8_t *buf = malloc(hl + bl + sizeof foot);

    memcpy(buf, head, hl);
    memcpy(buf + hl, inlen > 0 ? in : bytes(blank), bl);
    memcpy(buf + hl + bl, foot, sizeof foot - 1);

    Doc *nd = doc_load_mem(buf, hl + bl + sizeof foot - 1, err, sizeof err);

    free(buf);

    if (nd == NULL) {
        popup_msg(title, err);

        return 0;
    }

    Editor *outer = A->ed;
    Editor *ne = ed_new(nd);
    int accepted = 0;
    int saved_quit = A->quit;
    int saved_op = A->pending_op;
    const char *saved_title = nested_title;
    char mode[32];

    snprintf(mode, sizeof mode, "%s EDIT", title);

    for (char *c = mode; *c != '\0'; c++) {
        *c = chr(to_upper(*c));
    }

    ne->opt_expanded = outer->opt_expanded;
    ne->opt_ruler = outer->opt_ruler;
    ne->opt_keys = outer->opt_keys;
    ne->text_width = outer->text_width;
    ne->mode = mode;
    nested_title = title;
    A->ed = ne;
    A->pending_op = -1;
    ed_msg(ne, strcmp(title, "Equation") == 0 ?
                   "Ins saves, Esc restores, Ctrl-W views the equation" :
                   "Press Ins to Save Changes or Esc to Restore Previous "
                   "Version");
    redraw();

    for (;;) {
        int k = term_getkey(-1);

        if (k == K_RESIZE || k == K_NONE) {
            redraw();
            continue;
        }

        if (K_BASE(k) == K_INS && (k & (KM_SHIFT | KM_CTRL | KM_ALT)) == 0 &&
            ne->sel.kind == SEL_NONE) {
            accepted = 1;
            break;
        }

        if (K_BASE(k) == K_ESC && ne->sel.kind == SEL_NONE) {
            break;
        }

        /* Lists of files on the panels begin in the working directory. */
        if (A->ed != NULL) {
            widgets_set_dir(filemenu_workdir(A->ed));
        }

        {
            LayoutSig before = layout_sig();

            handle_key(k);
            layout_after_key(&before);
        }
        A->quit = saved_quit;
        ed_fix_cursor(ED);
        redraw();
    }

    if (accepted != 0) {
        size_t len;
        uint8_t *all = doc_serialize(nd, &len);
        size_t start = 0;

        for (int i = 0; i < nd->header.n; i++) {
            start += usz(nd->header.v[i].n) + 2;
        }

        size_t end = len - nd->tail_len;
        size_t n = end > start ? end - start : 0;

        /* A note that was left empty stays empty. */
        if (n == sizeof blank - 1 && memcmp(all + start, blank, n) == 0) {
            n = 0;
        }

        *out = malloc(n + 1);
        memcpy(*out, all + start, n);
        *outlen = n;
        free(all);
    }

    nested_title = saved_title;
    A->ed = outer;
    A->pending_op = saved_op;
    ed_free(ne);
    doc_free(nd);

    return accepted;
}


/* Is a marker command allowed where the cursor is? */
static int
marker_allowed(const MInfo *mi)
{
    if (mi->avail == MA_STRUCTURED) {
        return ED->doc->structured != 0;
    }

    /* The editor has no way to edit headers and footers yet. */
    return mi->avail != MA_HEADFOOT;
}


/*
 * The MARKER command.  On a marker it opens the marker's panel to change it.
 * Elsewhere it offers the marker commands and inserts
 * the marker that was chosen.
 */
/* The Marker Commands table: choose a command and insert its marker. */
static void
marker_commands(void)
{
    static int last;
    uint8_t *out;
    size_t olen;

    if (ed_text(ED) == NULL) {
        return;
    }

    int n = marker_menu_count();
    const char *names[64];
    char text[64][40];

    for (int i = 0; i < n && i < 64; i++) {
        const MInfo *mi = marker_menu(i);

        if (marker_allowed(mi) != 0) {
            str_copy(text[i], sizeof text[i], mi->name);
        } else {
            snprintf(text[i], sizeof text[i], "(%s)", mi->name);
        }

        names[i] = text[i];
    }

    int c = popup_grid("Marker Commands", names, n, 13, &last);

    if (c < 0) {
        return;
    }

    const MInfo *mi = marker_menu(c);

    if (marker_allowed(mi) == 0) {
        popup_msg("Marker", mi->avail == MA_STRUCTURED ?
                                "Invalid marker selection (Not available in "
                                "an unstructured document)" :
                                "Invalid marker selection (Not available in "
                                "document body)");

        return;
    }

    if (markerui_edit(ED->doc, mi, NULL, 0, &out, &olen, note_editor) != 0) {
        ed_insert_marker(ED, mi->type, out, olen);
        free(out);
    }
}


/*
 * The MARKER key.  On a marker it opens the marker's panel to change it.
 * Elsewhere it shows the Marker Commands.
 */
static void
cmd_marker(void)
{
    Text *t = ed_text(ED);
    uint8_t *out;
    size_t olen;

    if (t == NULL) {
        return;
    }

    if (ED->cur.tok < t->n && t->v[ED->cur.tok].kind == TK_MARKER) {
        const Tok *k = &t->v[ED->cur.tok];
        const MInfo *mi = marker_info(tok_mtype(k));

        if (tok_mtype(k) == MT_PRINT_ERROR) {
            show_print_error(k);

            return;
        }

        if (mi == NULL) {
            ed_msg(ED, "Marker type %d cannot be edited", tok_mtype(k));

            return;
        }

        if (markerui_edit(ED->doc, mi, tok_mdata(k), tok_mlen(k), &out, &olen,
                          note_editor) != 0) {
            ed_replace_marker(ED, ED->cur.tok, out, olen);
            free(out);
        }

        return;
    }

    marker_commands();
}


static void
cmd_setattr(void)
{
    const char *it[] = {"Normal", "Bold", "Italics", "Underscore Words"};
    int c = popup_menu("Set Attributes", it, 4, 1);

    if (c == 0) {
        ed_toggle_attr(ED, 0);
    } else if (c == 1) {
        ed_toggle_attr(ED, ATTR_BOLD);
    } else if (c == 2) {
        ed_toggle_attr(ED, ATTR_ITALIC);
    } else if (c == 3) {
        ed_toggle_attr(ED, ATTR_UNDERW);
    }
}


/* A field that shows a setting with no effect yet, so it is not editable. */
static void
info_field(WField *f, const char *label, const char *value)
{
    memset(f, 0, sizeof *f);
    f->label = label;
    f->kind = WF_INFO;
    str_copy(f->val, sizeof f->val, value);
}


/* A field for a whole number, shown as text. */
static void
number_field(WField *f, const char *label, int n)
{
    memset(f, 0, sizeof *f);
    f->label = label;
    f->kind = WF_TEXT;
    snprintf(f->val, sizeof f->val, "%d", n);
}


/* The check of the Options panel: Backups Kept is a whole number. */
static int
check_options(const WField *f, int n UNUSED, char *err, int errcap)
{
    const char *v = f[8].val;
    int ok = v[0] != '\0' && strlen(v) <= 9;

    for (int i = 0; ok != 0 && v[i] != '\0'; i++) {
        ok = v[i] >= '0' && v[i] <= '9';
    }

    if (ok == 0) {
        snprintf(err, usz(errcap), "Backups Kept is a whole number from 0 to "
                                   "%d", CONFIG_BACKUPS_MAX);

        return 0;
    }

    return 1;
}


static void
yes_no_field(WField *f, const char *label, int on)
{
    static const char *const yn[] = {"No", "Yes", NULL};

    memset(f, 0, sizeof *f);
    f->label = label;
    f->kind = WF_CHOICE;
    f->choices = yn;
    f->sel = on != 0;
}


/*
 * Autosave (Global, Options): on or off, with an interval in seconds.  A
 * document that has unsaved changes is saved under its name when the
 * interval has passed since the first change, without asking.  An Autosave
 * makes no numbered backup.  The check is made while the program waits for a
 * key.
 */
/* The defaults: Autosave is on, every two minutes. */
static int autosave_on = 1;
static int autosave_seconds = 120;
static time_t autosave_since;


/* Returns 1 if the document was saved, so that the screen is drawn. */
static int
autosave_poll(void)
{
    Doc *d = A->ed->doc;
    time_t now = time(NULL);

    if (d->modified == 0) {
        autosave_since = 0;

        return 0;
    }

    if (autosave_since == 0) {
        autosave_since = now;

        return 0;
    }

    if (autosave_on == 0 || d->path == NULL ||
        now - autosave_since < autosave_seconds) {
        return 0;
    }

    filemenu_autosave(ED);
    autosave_since = 0;

    return 1;
}


/*
 * Global, Options.  The panel has thirteen settings.  Those that Strata acts
 * on can be changed; the others are shown with their default values, and
 * dimmed.
 */
static void
options_menu(void)
{
    WField f[13];
    int n = 0;

    yes_no_field(&f[n++], "Expanded Status Line?", ED->opt_expanded);
    yes_no_field(&f[n++], "Display On-Screen Help?", ED->opt_keys);
    info_field(&f[n++], "Block Indicators", "Solid Lines");
    yes_no_field(&f[n++], "Display Tab & End-of-Line Characters?",
                 layout_show_marks);
    yes_no_field(&f[n++], "Display Dots for Spaces?", layout_show_dots);
    info_field(&f[n++], "Display Even Justification?", "No");
    info_field(&f[n++], "Display +/- with Section Tags?", "No");
    info_field(&f[n++], "Units", "Inches (in)");
    number_field(&f[n++], "Backups Kept (0 to 99)", ED->opt_backups);
    /* Autosave is a setting of the user, made in str (Setup) only. */
    info_field(&f[n++], "Line Weight (Borders)", "100 %");
    info_field(&f[n++], "(Other)", "100 %");

    if (popup_form("Options", f, n, check_options) == 0) {
        return;
    }

    ED->opt_expanded = f[0].sel;
    ED->opt_keys = f[1].sel;
    layout_show_marks = f[3].sel;
    layout_show_dots = f[4].sel;
    {
        /* A lower number asks first; No keeps the number that it was. */
        int kept = config_clamp_backups(atoi(f[8].val));
        char text[600];

        if (kept < ED->opt_backups) {
            backup_lowering_text(ED->opt_backups, kept, text, sizeof text);

            if (popup_confirm("Backups Kept", text) == 0) {
                kept = ED->opt_backups;
            }
        }

        ED->opt_backups = kept;
    }

    ed_invalidate(ED);

    /* The settings are the user's: they are kept in ~/.strata. */
    Config cfg;
    char err[160];

    config_load(&cfg);
    cfg.expanded_status = ED->opt_expanded;
    cfg.onscreen_help = ED->opt_keys;
    cfg.show_marks = layout_show_marks;
    cfg.show_dots = layout_show_dots;
    cfg.backups = ED->opt_backups;
    cfg.backups_explicit = 1;

    if (config_save(&cfg, err, sizeof err) != 0) {
        popup_msg("Options", err);
    }
}


static int
do_quit(void)
{
    if (A->nwin == 2 && A->ed == A->win[A->wcur]) {
        /* The other window has its say first. */
        Editor *here = A->ed;

        A->ed = A->win[A->wcur ^ 1];

        int ok = confirm_discard();

        A->ed = here;

        if (ok == 0) {
            return 0;
        }
    }

    if (confirm_discard() != 0) {
        A->quit = 1;
        return 1;
    }

    return 0;
}


/* ============================================================ Edit menu */
/* Edit, Title: the title page, edited like a note. */
static void
edit_title(void)
{
    size_t len;
    uint8_t *in = doc_title_bytes(ED->doc, &len);
    uint8_t *out = NULL;
    size_t olen = 0;
    char err[96];
    int ok = note_editor("Title Page", in, in != NULL ? len : 0, &out, &olen);

    free(in);

    if (ok == 0) {
        return;
    }

    if (doc_set_title(ED->doc, out, olen, err, sizeof err) != 0) {
        popup_msg("Title Page", err);
    }

    free(out);
    ed_invalidate(ED);
    ed_fix_cursor(ED);
}


/* Edit, Section and Edit, Document (Ctrl-^): the text of one section only. */
static void
toggle_section_only(void)
{
    if (ED->only_s >= 0) {
        ED->only_s = -1;
    } else if (ED->doc->structured == 0) {
        ed_msg(ED, "Only a structured document has sections");

        return;
    } else {
        int s = sect_start(ED->doc, ED->cur.item);

        if (s < 0) {
            ed_msg(ED, "There is no current section");

            return;
        }

        ED->only_s = s;
    }

    ed_invalidate(ED);
    ed_fix_cursor(ED);
}


/* ============================================================ equations */
/*
 * Displayed equations are shown as pictures in the X11 version: the
 * hooks of layout.h look the pictures up and ask for the ones that are
 * missing (EQUATIONS.txt).  The terminal has no pictures and keeps the
 * label.
 */
static int eq_cell_w;
static int eq_cell_h;


/*
 * The places where the file of a picture is looked for: the directory of the
 * document and the working directory of the panels (the rule is in
 * latex_find_picture and in docs/PICTURES-PATHS.txt).  docpath is the file
 * of the document or NULL.
 */
static void
picture_dirs(const char *docpath, char *docdir, size_t dcap,
             const char **workdir)
{
    const char *sl = docpath != NULL ? strrchr(docpath, '/') : NULL;

    if (sl == NULL) {
        str_copy(docdir, dcap, ".");
    } else if (sl == docpath) {
        str_copy(docdir, dcap, "/");
    } else {
        size_t n = lsz(sl - docpath);

        n = n < dcap - 1 ? n : dcap - 1;
        memcpy(docdir, docpath, n);
        docdir[n] = '\0';
    }

    *workdir = A != NULL && A->ed != NULL ? filemenu_workdir(A->ed) : NULL;
}


/*
 * The file of a picture: the place where the name is found, or the name as
 * it was typed when there is none.
 */
static void
picture_path(const char *name, char *full, size_t cap)
{
    char docdir[1024];
    char found[PATH_MAX];
    const char *workdir;

    str_copy(full, cap, name);
    picture_dirs(A != NULL && A->ed != NULL ? A->ed->doc->path : NULL, docdir,
                 sizeof docdir, &workdir);

    if (latex_find_picture(docdir, workdir, name, found, sizeof found, NULL,
                           0) != 0) {
        str_copy(full, cap, found);
    }
}


/*
 * Can the file of a Picture marker be used?  0 when it can; else 1 (it is not
 * there), 2 (it cannot be read) or 3 (it is not a picture that can be
 * printed), and in why a few words.  Only the first bytes are read, so that
 * this is cheap; the printer reads all of the file.
 */
enum {
    PIC_OK,
    PIC_NOTFOUND,
    PIC_NOREAD,
    PIC_NOTPIC
};


static int
picture_status(const char *docdir, const char *workdir, const char *name,
               char *why, size_t cap)
{
    char found[PATH_MAX];

    why[0] = '\0';

    if (latex_find_picture(docdir, workdir, name, found, sizeof found, NULL,
                           0) == 0) {
        str_copy(why, cap, "file not found");

        return PIC_NOTFOUND;
    }

    if (access(found, R_OK) != 0) {
        str_copy(why, cap, "cannot be read");

        return PIC_NOREAD;
    }

    if (latex_picture_kind(found) == 0) {
        str_copy(why, cap, "not a picture");

        return PIC_NOTPIC;
    }

    return PIC_OK;
}


/*
 * The answers of picture_status are kept for a short time, so that drawing a
 * screen of pictures does not look at a file for every row.  An answer is
 * used for PIC_TTL_MS, then the file is looked at again; the cursor coming to
 * a marker looks at once (picture_note).
 */
#define PIC_CACHE 128
#define PIC_TTL_MS 2000

typedef struct {
    char *docdir;
    char *workdir;
    char *name;
    int status;
    char why[48];
    long long when; /* milliseconds on the monotonic clock */
} PicEntry;

static PicEntry pic_cache[PIC_CACHE];
static int pic_next; /* the entry that is replaced next */


static long long
pic_now(void)
{
    struct timespec ts;
    long long ms;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    ms = ts.tv_sec;
    ms = ms * 1000 + ts.tv_nsec / 1000000;

    return ms;
}


static char *
pic_dup(const char *s)
{
    char *d = malloc(strlen(s) + 1);

    if (d != NULL) {
        memcpy(d, s, strlen(s) + 1);
    }

    return d;
}


static int
pic_same(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return a == b;
    }

    return strcmp(a, b) == 0;
}


static PicEntry *
pic_find(const char *docdir, const char *workdir, const char *name)
{
    for (int i = 0; i < PIC_CACHE; i++) {
        PicEntry *e = &pic_cache[i];

        if (e->name != NULL && strcmp(e->name, name) == 0 &&
            pic_same(e->docdir, docdir) && pic_same(e->workdir, workdir)) {
            return e;
        }
    }

    return NULL;
}


static PicEntry *
pic_add(const char *docdir, const char *workdir, const char *name)
{
    PicEntry *e = &pic_cache[pic_next];

    pic_next = (pic_next + 1) % PIC_CACHE;
    free(e->docdir);
    free(e->workdir);
    free(e->name);
    e->docdir = pic_dup(docdir);
    e->workdir = workdir != NULL ? pic_dup(workdir) : NULL;
    e->name = pic_dup(name);
    e->when = 0;
    e->status = PIC_OK;
    e->why[0] = '\0';

    return e;
}


/* The answer for a marker; fresh asks for a look at the file now. */
static int
picture_lookup(const char *docpath, const char *name, int fresh, char *why,
               size_t cap)
{
    char docdir[1024];
    const char *workdir;
    PicEntry *e;
    long long now = pic_now();

    picture_dirs(docpath, docdir, sizeof docdir, &workdir);
    e = pic_find(docdir, workdir, name);

    if (e == NULL) {
        e = pic_add(docdir, workdir, name);
        fresh = 1;
    }

    if (fresh != 0 || now - e->when >= PIC_TTL_MS || now < e->when) {
        e->status = picture_status(docdir, workdir, name, e->why,
                                   sizeof e->why);
        e->when = now;
    }

    str_copy(why, cap, e->why);

    return e->status;
}


/* The function that the markers use (marker_picture_fn). */
static int
picture_check(const char *docpath, const char *name, char *why, size_t cap)
{
    return picture_lookup(docpath, name, 0, why, cap) != PIC_OK ? 1 : 0;
}


/*
 * Look again, after a key, at the pictures that were not usable when they
 * were last looked at and have not been looked at for a while (the file may
 * have been made since); when one is usable now the screen is laid out
 * again, so that the marker stops being dim.
 */
static void
picture_refresh(void)
{
    long long now = pic_now();
    int changed = 0;

    for (int i = 0; i < PIC_CACHE; i++) {
        PicEntry *e = &pic_cache[i];

        if (e->name == NULL || e->status == PIC_OK ||
            (now - e->when < PIC_TTL_MS && now >= e->when)) {
            continue;
        }

        int before = e->status;

        e->status = picture_status(e->docdir, e->workdir, e->name, e->why,
                                   sizeof e->why);
        e->when = now;
        changed |= e->status != before;
    }

    if (changed != 0 && ED != NULL) {
        ed_invalidate(ED);
    }
}


/*
 * The cursor is on a Picture marker whose file cannot be used: a message
 * says so (and the file is looked at again at once).  A message that a
 * command made is left alone.
 */
static void
picture_note(void)
{
    Text *t;
    const Tok *k;
    MPayload p;
    const char *name;
    char why[48];
    int st;

    if (ED == NULL || ED->doc == NULL || ED->msg[0] != '\0' ||
        ED->cur.tok < 0) {
        return;
    }

    t = ed_text(ED);

    if (t == NULL || ED->cur.tok >= t->n) {
        return;
    }

    k = &t->v[ED->cur.tok];

    if (k->kind != TK_MARKER || tok_mtype(k) != 25) {
        return;
    }

    mp_parse(&p, marker_info(25), tok_mdata(k), tok_mlen(k));
    name = mp_get(&p, "1");

    if (name != NULL && name[0] != '\0') {
        st = picture_lookup(ED->doc->path, name, 1, why, sizeof why);

        if (st == PIC_NOTFOUND) {
            ed_msg(ED, "Picture file not found: %s", name);
        } else if (st == PIC_NOREAD) {
            ed_msg(ED, "Picture file cannot be read: %s", name);
        } else if (st == PIC_NOTPIC) {
            ed_msg(ED, "Picture file is not a picture: %s", name);
        }
    }

    mp_free(&p);
}


/*
 * What the renderer needs to make the picture of a Picture marker: the
 * file (a relative name is looked for next to the document), the width and
 * the height and the scale.  Returns 0 when the marker has no file name.
 */
static int
picture_spec(const Tok *k, char *spec, size_t cap)
{
    MPayload p;
    const char *v;
    char full[1024];
    int wid = 0;
    int hei = 0;
    int scl = 0;
    int ok = 0;

    mp_parse(&p, marker_info(25), tok_mdata(k), tok_mlen(k));
    v = mp_get(&p, "1");

    if (v != NULL && v[0] != '\0') {
        picture_path(v, full, sizeof full);

        v = mp_get(&p, "2");
        wid = v != NULL ? atoi(v) : 0;
        v = mp_get(&p, "3");
        hei = v != NULL ? atoi(v) : 0;
        v = mp_get(&p, "4");
        scl = v != NULL ? atoi(v) : 0;
        snprintf(spec, cap, "%.900s|%d|%d|%d|%s", full, wid, hei, scl,
                 marker_length_unit(k));
        ok = 1;
    }

    mp_free(&p);

    return ok;
}


/*
 * What the renderer needs to make the picture of a Keycap marker: the
 * legend, the foreground and the background (tenths), the scale
 * (thousandths) and the font number.  A value that the marker does not have
 * is the one of the Keycap Format of the document (doc may be NULL for the
 * defaults).
 */
static int
keycap_spec(const Doc *doc, const Tok *k, char *spec, size_t cap)
{
    MPayload p;
    KeycapFmt g;
    const char *v;
    int ok = 0;

    mp_parse(&p, marker_info(37), tok_mdata(k), tok_mlen(k));
    v = mp_get(&p, "1");

    if (v != NULL && v[0] != '\0') {
        char leg[200];

        str_copy(leg, sizeof leg, v);

        for (char *c = leg; *c != '\0'; c++) {
            if (*c == '|') {
                *c = '/';
            }
        }

        keycap_values(doc, k, &g);
        snprintf(spec, cap, "%s|%d|%d|%d|%d", leg, g.fg, g.bg, g.scale,
                 g.font);
        ok = 1;
    }

    mp_free(&p);

    return ok;
}


/* ---------------------------------------------------- LaTeX markers */
/*
 * The picture of a LaTeX marker (docs/LATEXMARKER-PICTURES.txt).  The text of
 * the marker is checked alone first (latexmark.h); text that can be a piece
 * of a line is typeset in the background with the preamble of the Printer
 * and shown as a picture in the line.  Anything else, and every failure, is
 * drawn by the display mode of the marker settings; the reason is told in
 * the message line when the cursor is on the marker and in the log.
 */
#define LATEX_RAW 4096

/* The setting of the Printer that does not allow LaTeX markers at all. */
static int latex_markers_off;


/* The place of a marker for a message: its block and its line. */
static void
marker_place(const Text *t, int tok, int *block, int *line)
{
    const Doc *d = A != NULL && A->ed != NULL ? A->ed->doc : NULL;

    *block = 0;
    *line = 1;

    /* The blocks are counted, not the other items of the document. */
    for (int i = 0, n = 0; d != NULL && i < d->nitems; i++) {
        const Block *b = d->items[i].type == IT_BLOCK ? d->items[i].blk : NULL;

        n += b != NULL;

        for (int c = 0; b != NULL && c < b->ncols; c++) {
            if (&b->cols[c].text == t) {
                *block = n;
            }
        }
    }

    for (int i = 0; i < tok && i < t->n; i++) {
        *line += t->v[i].kind == TK_NL;
    }
}


/*
 * The text that LaTeX gets for the marker k and, when it cannot be a
 * picture, the reason (a phrase for "drawn as Short: ").  Returns 1 when
 * the text may be typeset, with the spec for the renderer: the size of the
 * text of the document, a bar and the text.
 */
static int
latex_spec(const Doc *doc, const Tok *k, char *spec, size_t cap, char *why,
           size_t wcap)
{
    char raw[LATEX_RAW];
    char utf[3 * LATEX_RAW];
    char prep[LMK_MAX + 1];
    size_t n = 0;
    int len = marker_latex_text(k, raw, sizeof raw);

    if (len < 0) {
        str_copy(why, wcap, "its text is too long or damaged");

        return 0;
    }

    for (int i = 0; i < len; i++) {
        uint8_t ch = u8(raw[i]);

        if (ch >= 0x80) {
            char u[4];
            size_t m = latex_cp437_utf8(ch, u);

            memcpy(utf + n, u, m);
            n += m;
        } else {
            utf[n++] = raw[i];
        }
    }

    utf[n] = '\0';

    if (lmk_prepare(utf, prep, sizeof prep) < 0) {
        str_copy(why, wcap, "its text is too long to draw as a picture");

        return 0;
    }

    if (prep[0] == '\0') {
        str_copy(why, wcap, "it is empty");

        return 0;
    }

    snprintf(spec, cap, "%d|%s", doc != NULL ? latex_text_points(doc) : 11,
             prep);

    return 1;
}


/* The piece that the renderer needs, as a function for the renderer. */
static int
latex_head(int pts, FILE *out)
{
    return latex_snippet_head(pts, out);
}


/* Has this reason been written to the log already?  A short memory. */
static int
latex_seen(const char *key)
{
    static unsigned long seen[64];
    static int next;
    unsigned long h = 5381;

    for (const char *p = key; *p != '\0'; p++) {
        h = h * 33 + u8(*p);
    }

    for (int i = 0; i < 64; i++) {
        if (seen[i] == h) {
            return 1;
        }
    }

    seen[next] = h;
    next = (next + 1) % 64;

    return 0;
}


/*
 * Why the LaTeX marker k is drawn in the Short form although its display mode
 * is Inline: 1 and the reason, or 0 when it is drawn as a picture (or still
 * being made).  The text is given to LaTeX alone; the reasons are that LaTeX
 * is not installed, a mistake that LaTeX reports, a job that takes more
 * than 5 seconds, and a result that is refused (nothing visible, larger than
 * the picture job makes).  Nothing is started here.
 */
static int
latex_failure(const Doc *doc, const Tok *k, char *why, size_t wcap)
{
    char spec[LMK_MAX + 16];

    why[0] = '\0';

    if (latex_markers_off != 0) {
        str_copy(why, wcap, "LaTeX markers are not allowed in this setup");

        return 1;
    }

    if (latex_spec(doc, k, spec, sizeof spec, why, wcap) == 0) {
        return 1;
    }

    if (eqr_pictures_enabled() == 0) {
        str_copy(why, wcap, "LaTeX is not installed");

        return 1;
    }

    const char *err = eqr_error(spec, 4);

    if (err != NULL) {
        if (eqr_error_kind(spec, 4, eqr_dpi()) == 'E') {
            snprintf(why, wcap, "LaTeX reported: %s", err);
        } else {
            str_copy(why, wcap, err);
        }

        return 1;
    }

    return 0;
}


/* The same for the marker at tok of a text, only when the mode is Inline. */
static int
latex_refusal(const Text *t, int tok, char *why, size_t wcap)
{
    const Doc *doc = A != NULL && A->ed != NULL ? A->ed->doc : NULL;

    why[0] = '\0';

    if (doc == NULL || marker_mode(doc, MT_LATEX) != MM_INLINE ||
        term_is_gui() == 0) {
        return 0;
    }

    return latex_failure(doc, &t->v[tok], why, wcap);
}


/*
 * The picture of a LaTeX marker, or NULL: not asked for, not renderable,
 * not ready yet or failed.  A refusal and an error are written to the log
 * once: the error of LaTeX as a warning (the Error Log of the run), the
 * other reasons for a developer.
 */
static const EqImage *
latex_find(const Text *t, int tok, int want)
{
    char spec[LMK_MAX + 16];
    char why[200];
    const Doc *doc = A != NULL && A->ed != NULL ? A->ed->doc : NULL;

    if (doc == NULL || marker_mode(doc, MT_LATEX) != MM_INLINE ||
        latex_markers_off != 0) {
        return NULL;
    }

    if (latex_spec(doc, &t->v[tok], spec, sizeof spec, why, sizeof why) == 0) {
        if (want != 0 && latex_seen(spec) == 0) {
            int block;
            int line;

            marker_place(t, tok, &block, &line);
            log_debug("The LaTeX marker in block %d, line %d is drawn by "
                      "settings: %s.", block, line, why);
        }

        return NULL;
    }

    const EqImage *im = want != 0 ? eqr_request(spec, 4) : eqr_peek(spec, 4);

    if (im == NULL && want != 0) {
        const char *err = eqr_error_take(spec, 4, eqr_dpi());

        if (err != NULL) {
            int block;
            int line;

            marker_place(t, tok, &block, &line);

            if (eqr_error_kind(spec, 4, eqr_dpi()) == 'E') {
                log_warn("The LaTeX marker in block %d, line %d does not "
                         "typeset, so it is drawn as Short: %s",
                         block, line, err);
            } else {
                log_debug("The picture of the LaTeX marker in block %d, "
                          "line %d is refused, so it is drawn as Short: "
                          "%s.", block, line, err);
            }
        }
    }

    return im;
}


/*
 * The text and the display with which the picture of an Equation (9), a
 * Picture (25) or a Keycap (37) marker is asked for.  An equation is asked
 * for in a line (variant 0) or displayed (variant 1); the others have one
 * variant.  Returns 0 when the marker has no such picture.
 */
static int
image_spec(const Tok *k, int variant, char *spec, size_t cap, int *display)
{
    const Doc *doc = A != NULL && A->ed != NULL ? A->ed->doc : NULL;

    if (k->kind != TK_MARKER) {
        return 0;
    }

    switch (tok_mtype(k)) {
        case 37:
            *display = 3;

            return variant == 0 && keycap_spec(doc, k, spec, cap) != 0;

        case 25:
            *display = 2;

            return variant == 0 && picture_spec(k, spec, cap) != 0;

        case 9:
            *display = variant;

            return variant <= 1 && marker_equation_text(k, spec, (int)cap) >= 0;

        default:
            return 0;
    }
}


/*
 * Why the picture of the Equation, Picture or Keycap marker k cannot be
 * made: 1 and the reason, or 0 (it is made, or still being made, or the
 * marker is not shown Inline).  LaTeX that is not installed, an error of
 * LaTeX, a job that takes more than 5 seconds and a refused picture all
 * count.  Nothing is started here.
 */
static int
image_failure(const Tok *k, char *why, size_t wcap)
{
    char spec[1024];
    int display;

    why[0] = '\0';

    if (k->kind == TK_MARKER && tok_mtype(k) == MT_LATEX) {
        return latex_failure(A != NULL && A->ed != NULL ? A->ed->doc : NULL,
                             k, why, wcap);
    }

    for (int variant = 0; variant < 2; variant++) {
        if (image_spec(k, variant, spec, sizeof spec, &display) == 0) {
            continue;
        }

        if (display >= 2 ? eqr_pictures_enabled() == 0 :
                           eqr_enabled() == 0) {
            str_copy(why, wcap, "LaTeX is not installed");

            return 1;
        }

        const char *err = eqr_error(spec, display);

        if (err != NULL) {
            str_copy(why, wcap, err);

            /* Only the first line of the message of LaTeX is told. */
            for (char *c = why; *c != '\0'; c++) {
                if (*c == '\n') {
                    *c = '\0';
                    break;
                }
            }

            return 1;
        }
    }

    return 0;
}


/* The hook of marker_render: is the picture of this marker a failure? */
static int
image_failed(const Tok *k)
{
    char why[200];

    return image_failure(k, why, sizeof why);
}


static const char *
image_name(const Tok *k)
{
    return tok_mtype(k) == 37 ? "Keycap" : tok_mtype(k) == 25 ? "Picture" :
                                                                "Equation";
}


/*
 * A picture that failed is written to the log once, with the place of the
 * marker and the first line of the message: the Error Log of the run.
 */
static void
image_log(const Text *t, int tok)
{
    char spec[1024];
    int display;
    int block;
    int line;

    for (int variant = 0; variant < 2; variant++) {
        if (image_spec(&t->v[tok], variant, spec, sizeof spec,
                       &display) == 0) {
            continue;
        }

        const char *err = eqr_error_take(spec, display, eqr_dpi());

        /* LaTeX that is not installed has no entry of the renderer: it is
           written once for each marker text. */
        if (err == NULL && (display >= 2 ? eqr_pictures_enabled() == 0 :
                                           eqr_enabled() == 0)) {
            char key[1100];

            snprintf(key, sizeof key, "nolatex|%s", spec);
            err = latex_seen(key) == 0 ? "LaTeX is not installed" : NULL;
        }

        if (err == NULL) {
            continue;
        }

        char first[200];

        str_copy(first, sizeof first, err);

        for (char *c = first; *c != '\0'; c++) {
            if (*c == '\n') {
                *c = '\0';
                break;
            }
        }

        marker_place(t, tok, &block, &line);
        log_warn("The %s in block %d, line %d cannot be drawn as a picture, "
                 "so it is drawn as Short: %s", image_name(&t->v[tok]),
                 block, line, first);
    }
}


/*
 * The cursor is on a LaTeX marker that is drawn Short: the message
 * line says why, unless a command made a message.
 */
static void
latex_note(void)
{
    Text *t;
    char why[200];

    if (ED == NULL || ED->doc == NULL || ED->msg[0] != '\0' ||
        ED->cur.tok < 0) {
        return;
    }

    t = ed_text(ED);

    if (t != NULL && ED->cur.tok < t->n &&
        t->v[ED->cur.tok].kind == TK_MARKER &&
        (tok_mtype(&t->v[ED->cur.tok]) == 9 ||
         tok_mtype(&t->v[ED->cur.tok]) == 25 ||
         tok_mtype(&t->v[ED->cur.tok]) == 37) &&
        marker_mode(ED->doc, tok_mtype(&t->v[ED->cur.tok])) == MM_INLINE &&
        image_failure(&t->v[ED->cur.tok], why, sizeof why) != 0) {
        ed_msg(ED, "%s drawn as Short: %s", image_name(&t->v[ED->cur.tok]),
               why);

        return;
    }

    if (t == NULL || ED->cur.tok >= t->n ||
        t->v[ED->cur.tok].kind != TK_MARKER ||
        tok_mtype(&t->v[ED->cur.tok]) != MT_LATEX) {
        return;
    }

    if (latex_refusal(t, ED->cur.tok, why, sizeof why) != 0) {
        ed_msg(ED, "LaTeX marker drawn as Short: %s", why);
    }
}


static const EqImage *
eq_find(const Text *t, int tok, int display, int want)
{
    char tex[1024];

    /* A LaTeX marker that LaTeX can typeset as a piece of a line (41). */
    if (tok >= 0 && tok < t->n && t->v[tok].kind == TK_MARKER &&
        tok_mtype(&t->v[tok]) == MT_LATEX) {
        return latex_find(t, tok, want);
    }

    /* A keycap is made like an equation, in a line of text (display 3). */
    if (tok >= 0 && tok < t->n && t->v[tok].kind == TK_MARKER &&
        tok_mtype(&t->v[tok]) == 37) {
        if (A != NULL && A->ed != NULL &&
            marker_mode(A->ed->doc, 37) != MM_INLINE) {
            return NULL;
        }

        if (keycap_spec(A != NULL && A->ed != NULL ? A->ed->doc : NULL,
                        &t->v[tok], tex, sizeof tex) == 0) {
            return NULL;
        }

        const EqImage *ki = want != 0 ? eqr_request(tex, 3) : eqr_peek(tex, 3);

        if (ki == NULL && want != 0) {
            image_log(t, tok);
        }

        return ki;
    }

    /* A picture is made like an equation, from a file (display 2). */
    if (tok >= 0 && tok < t->n && t->v[tok].kind == TK_MARKER &&
        tok_mtype(&t->v[tok]) == 25) {
        if (A != NULL && A->ed != NULL &&
            marker_mode(A->ed->doc, 25) != MM_INLINE) {
            return NULL;
        }

        if (picture_spec(&t->v[tok], tex, sizeof tex) == 0) {
            return NULL;
        }

        const EqImage *pi = want != 0 ? eqr_request(tex, 2) : eqr_peek(tex, 2);

        if (pi == NULL && want != 0) {
            image_log(t, tok);
        }

        return pi;
    }

    /* Only the display mode Inline shows the picture, the others the marker. */
    if (A != NULL && A->ed != NULL && marker_mode(A->ed->doc, 9) != MM_INLINE) {
        return NULL;
    }

    if (tok < 0 || tok >= t->n || marker_equation_text(&t->v[tok], tex,
                                                        sizeof tex) < 0) {
        return NULL;
    }

    const EqImage *ei = want != 0 ? eqr_request(tex, display) :
                                    eqr_peek(tex, display);

    if (ei == NULL && want != 0) {
        image_log(t, tok);
    }

    return ei;
}


/*
 * Where a picture goes in the cells.  A displayed equation is centred in
 * rows of its own: hr rows, all of them "above" the empty text row.  An
 * inline equation has its baseline on the baseline of the text row, using
 * the depth that dvipng reported, and takes the rows above and below that
 * it needs.  xoff and yoff are pixels from the top left cell of the
 * picture.
 */
static void
eq_geometry_raw(const EqImage *im, int display, int *wc, int *above,
                int *below, int *xoff, int *yoff)
{
    *wc = (im->w + eq_cell_w - 1) / eq_cell_w;

    if (display != 0) {
        int hr = (im->h + eq_cell_h / 2 + eq_cell_h - 1) / eq_cell_h;

        *above = hr - 1;
        *below = 0;
        *xoff = (*wc * eq_cell_w - im->w) / 2;
        *yoff = (hr * eq_cell_h - im->h) / 2;

        return;
    }

    int asc = eq_cell_h * 3 / 4;
    int bottom = asc + im->depth;
    int top = bottom - im->h;

    *above = top < 0 ? (-top + eq_cell_h - 1) / eq_cell_h : 0;
    *below = bottom > eq_cell_h ? (bottom - eq_cell_h + eq_cell_h - 1) /
                                      eq_cell_h :
                                  0;
    *xoff = 0;
    *yoff = top + *above * eq_cell_h;
}


/*
 * The cells a picture takes are limited, so that a picture that is far
 * larger than any screen cannot make the text layout reserve a huge area.
 */
#define EQ_MAX_CELLS 300


static void
eq_geometry(const EqImage *im, int display, int *wc, int *above, int *below,
            int *xoff, int *yoff)
{
    eq_geometry_raw(im, display, wc, above, below, xoff, yoff);

    *wc = *wc > EQ_MAX_CELLS ? EQ_MAX_CELLS : *wc;
    *above = *above > EQ_MAX_CELLS ? EQ_MAX_CELLS : *above;
    *below = *below > EQ_MAX_CELLS ? EQ_MAX_CELLS : *below;
}


static int
eq_size(const Text *t, int tok, int display, int *wcells, int *above,
        int *below)
{
    const EqImage *im = eq_find(t, tok, display, 0);
    int xo;
    int yo;

    if (im == NULL) {
        return 0;
    }

    eq_geometry(im, display, wcells, above, below, &xo, &yo);

    return 1;
}


static const uint8_t *
eq_image(const Text *t, int tok, int display, int *w, int *h, int *xoff,
         int *yoff)
{
    const EqImage *im = eq_find(t, tok, display, 0);
    int wc;
    int ab;
    int bl;

    if (im == NULL) {
        return NULL;
    }

    eq_geometry(im, display, &wc, &ab, &bl, xoff, yoff);
    *w = im->w;
    *h = im->h;

    return im->rgb;
}


static void
eq_want(const Text *t, int tok, int display)
{
    eq_find(t, tok, display, 1);
}


/*
 * Ctrl-W in the equation editor: an enlarged picture of the equation as it
 * is now, or the message that latex gave when it could not typeset it.
 */
static void
doc_plain(const Doc *d, char *buf, int cap)
{
    int n = 0;

    for (int i = 0; i < d->nitems && n < cap - 1; i++) {
        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        const Text *t = &d->items[i].blk->cols[0].text;

        for (int k = 0; k < t->n && n < cap - 1; k++) {
            if (t->v[k].kind == TK_CHAR) {
                buf[n++] = chr(t->v[k].a);
            } else if (t->v[k].kind == TK_NL) {
                buf[n++] = '\n';
            } else if (t->v[k].kind == TK_TAB) {
                buf[n++] = ' ';
            }
        }

        if (n > 0 && buf[n - 1] != '\n' && n < cap - 1) {
            buf[n++] = '\n';
        }
    }

    while (n > 0 && buf[n - 1] == '\n') {
        n--;
    }

    buf[n] = '\0';
}


static void
equation_view(void)
{
    char tex[1024];

    doc_plain(ED->doc, tex, sizeof tex);

    if (eqr_enabled() == 0) {
        popup_msg("Equation", term_is_gui() != 0 ?
                                  "latex and dvipng are needed to typeset "
                                  "an equation" :
                                  "Viewing an equation needs the X11 version");

        return;
    }

    int dpi = eqr_dpi() * 3;
    int refitted = 0;

    for (;;) {
        draw_base();

        const EqImage *im = eqr_request_dpi(tex, 1, dpi);
        const char *err = im == NULL ? eqr_error_dpi(tex, 1, dpi) : NULL;
        int C = term_cols();
        int R = term_rows();

        /*
         * A picture that is larger than the window is made again smaller,
         * once, so that all of it can be seen.
         */
        if (im != NULL && refitted == 0) {
            int aw = (C - 6) * eq_cell_w;
            int ah = (R - 6) * eq_cell_h;

            if (im->w > aw || im->h > ah) {
                int by_w = dpi * aw / im->w;
                int by_h = dpi * ah / im->h;
                int fit = (by_w < by_h ? by_w : by_h) * 95 / 100;

                dpi = fit < 30 ? 30 : fit;
                refitted = 1;

                continue;
            }
        }
        char text[480] = "";
        char *lines[8];
        int nl = 0;
        int pw;
        int ph;

        if (im != NULL) {
            pw = (im->w + eq_cell_w - 1) / eq_cell_w + 4;
            ph = (im->h + eq_cell_h - 1) / eq_cell_h + 4;
        } else {
            snprintf(text, sizeof text, "%s", err != NULL ?
                                                  err :
                                                  "Typesetting...");

            for (char *p = strtok(text, "\n"); p != NULL && nl < 8;
                 p = strtok(NULL, "\n")) {
                lines[nl++] = p;
            }

            pw = 66;
            ph = nl + 5;
        }

        pw = pw > C - 2 ? C - 2 : pw < 24 ? 24 : pw;
        ph = ph > R - 2 ? R - 2 : ph;

        int x = (C - pw) / 2;
        int y = (R - ph) / 2;

        term_style(TS_PANEL);
        term_fill(x, y, pw, ph, ' ', 0);
        term_box(x, y, pw, ph, 0, 1);
        term_puts(x + 2, y, " Equation ", TA_BOLD);
        term_puts(x + 2, y + ph - 1, " Esc: back to the editor ", 0);

        if (im != NULL) {
            int iw = pw - 4;
            int ih = ph - 4;

            for (int r = 0; r < ih; r++) {
                for (int c = 0; c < iw; c++) {
                    term_putu(x + 2 + c, y + 2 + r, TERM_IMAGE_CELL, 0);
                }
            }

            term_image(x + 2, y + 2, (iw * eq_cell_w - im->w) / 2,
                       (ih * eq_cell_h - im->h) / 2, im->rgb, im->w, im->h);
        } else {
            if (err != NULL) {
                term_puts(x + 2, y + 1, "latex could not typeset the equation:",
                          TA_BOLD);
            }

            for (int i = 0; i < nl; i++) {
                char one[80];

                str_copy(one, sizeof one, lines[i]);

                if (pw - 4 < isz(sizeof one) && pw - 4 >= 0) {
                    one[pw - 4] = '\0';
                }

                term_puts(x + 2, y + 3 + i, one, 0);
            }
        }

        term_present();

        int k = term_getkey(-1);

        if (k == K_RESIZE || k == K_NONE) {
            continue;
        }

        break;
    }
}


/* Called while the program waits for a key: a picture may be ready. */
static int
eq_poll(void)
{
    if (eqr_poll() != 0) {
        if (A != NULL && A->ed != NULL) {
            ed_invalidate(A->ed);
        }

        return 1;
    }

    return 0;
}


static void
equations_init(void)
{
    unsigned fg;
    unsigned bg;

    if (term_gui_info(&eq_cell_w, &eq_cell_h, &fg, &bg) == 0) {
        return;
    }

    eqr_configure(eq_cell_h * 7, fg, bg);
    eqr_set_marker_head(latex_head);

    /* A marker whose picture cannot be made is drawn Short, also when
       LaTeX is not installed. */
    marker_failed_fn = image_failed;
    layout_eq_want = eq_want;

    if (eqr_enabled() != 0 || eqr_pictures_enabled() != 0) {
        layout_eq_size = eq_size;
        layout_eq_image = eq_image;
        term_set_idle(eq_poll);
    }
}


/* ============================================================ menu bar */
/*
 * The menu tree.  An entry that is not implemented is
 * dimmed and cannot be chosen.  The hints are the lines shown below
 * the bar.
 */
#define MI(n, h, a) {n, h, a, NULL, 0}
#define MSUB(n, h, a, s) {n, h, a, s, sizeof s / sizeof s[0]}

static const MenuItem file_items[] = {
    MI("Save", "Save the current document on the disk [Ctrl-S]", 1),
    MI("Retrieve", "Retrieve a new document file from the disk [Ctrl-R]", 1),
    MI("New", "Create a new document", 1),
    MI("Delete", "Delete a document file from the disk", 1),
    MI("Import", "Import an ASCII text file", 1),
    MI("Export", "Export an ASCII text file", 1)};

static const MenuItem named_items[] = {
    MI("Edit", "Edit a named block format", 1),
    MI("Delete", "Delete a named block format", 1)};

static const MenuItem global_items[] = {
    MI("Format", "Global section settings", 1),
    MI("Page", "Page layout settings", 1),
    MI("Typeface", "Font assignments", 1),
    MI("Marker", "Marker settings", 1),
    MI("Options", "Other settings", 1),
    MSUB("Named", "Edit, Delete named block format settings", 1,
         named_items)};

static const MenuItem edit_title_item =
    MI("Title", "Edit title page", 1);
static const MenuItem edit_struct_item =
    MI("Structured", "Enter structured mode", 1);
static const MenuItem edit_unstruct_item =
    MI("Unstructured", "Enter unstructured mode", 1);
static const MenuItem edit_section_item =
    MI("Section", "Edit current section only [Ctrl-^]", 1);
static const MenuItem edit_document_item =
    MI("Document", "Edit entire document [Ctrl-^]", 1);
static const MenuItem window_items[] = {
    MI("Open", "Open a second window", 1),
    MI("Close", "Close the window of the cursor", 1),
    MI("Adjust", "Change the size of the windows", 1),
    MI("Zoom-up", "The window of the cursor fills the screen", 1),
    MI("Zoom-down", "Both windows share the screen again", 1),
    MI("Switch", "Move the cursor to the other window", 1)};

static const MenuItem edit_window_item =
    MSUB("Window", "Open/Close, Adjust, Zoom, Switch", 1, window_items);

static const MenuItem preview_items[] = {
    MI("Document", "Preview entire document", 1),
    MI("Block", "Preview the current block", 1),
    MI("Section", "Preview the current section", 1)};

static const MenuItem print_items[] = {
    MI("Draft", "Print document in draft quality", 1),
    MI("Final", "Print document in final quality", 1),
    MSUB("Preview", "Document, Block, Section", 1, preview_items),
    MI("Next Error", "Find the next Print Error [Ctrl-F8]", 1),
    MI("Previous Error", "Find the previous Print Error [Ctrl-F7]", 1),
    MI("Remove Error", "Remove the Print Error under the cursor", 1),
    MI("Remove Print Errors", "Remove all the Print Errors", 1)};

static const MenuItem macro_items[] = {
    MI("Edit", "Edit a macro", 1),
    MI("Delete", "Delete a macro", 1),
    MI("Learn", "Record keystrokes as a macro", 1),
    MI("Save", "Save macros in a file", 1),
    MI("Retrieve", "Retrieve macros from a file", 1)};

static const MenuItem library_items[] = {
    MI("Edit", "Edit a library entry", 1),
    MI("Delete", "Delete a library entry", 1),
    MI("Save", "Save the library in a file", 1),
    MI("Retrieve", "Retrieve a library from a file", 1)};

static const MenuItem spell_items[] = {
    MI("Document", "Check spelling of the document", 1),
    MI("Block", "Check spelling of the current block", 1),
    MI("Section", "Check spelling of the current section", 1),
    MI("Word", "Check spelling of the current word [Ctrl-C]", 1)};

static const MenuItem sort_items[] = {
    MI("Section", "Sort the subsections of the current section", 1),
    MI("Table", "Sort the highlighted blocks", 1)};

static const MenuItem tools_items[] = {
    MSUB("Macros", "Edit, Delete, Learn, Save, Retrieve", 1, macro_items),
    MSUB("Library", "Edit, Delete, Save, Retrieve", 1, library_items),
    MSUB("Spell", "Document, Block, Section, Word", 1, spell_items),
    MI("Thesaurus", "Find synonyms for the current word [Ctrl-T]", 1),
    MSUB("Autosort", "Section, Table", 1, sort_items),
    MI("Count", "Display word count", 1),
    MI("Undo", "Undo the last change [Ctrl-/]", 1),
    MI("Redo", "Do again what was undone [Alt-/]", 1)};

static const MenuItem top_items[] = {
    MSUB("File", "Save, Retrieve, New, Delete, Import, Export", 1,
         file_items),
    MSUB("Global", "Format, Page, Typeface, Marker, Options, Named", 1,
         global_items),
    MI("Edit", "Title, Structured/Unstructured, Section/Document, Window", 1),
    MSUB("Print", "Draft, Final, Preview, Errors", 1,
         print_items),
    MSUB("Tools", "Macros, Library, Spell, Thesaurus, Autosort, Count, Undo", 1,
         tools_items),
    MI("Quit", "Return to the Document Manager [Ctrl-Q]", 1)};


/* The entries of the Tools menu that are not a submenu. */
enum {
    TOOLS_THESAURUS = 3
};

enum {
    TOP_FILE,
    TOP_GLOBAL,
    TOP_EDIT,
    TOP_PRINT,
    TOP_TOOLS,
    TOP_QUIT,
    TOP_COUNT
};


static void
run_file(int cmd)
{
    switch (cmd) {
        case 0:
            filemenu_save(ED);
            break;

        case 1:
            filemenu_retrieve(ED);
            break;

        case 2:
            filemenu_new(ED);
            break;

        case 3:
            filemenu_delete(ED);
            break;

        case 4:
            filemenu_import(ED);
            break;

        default:
            filemenu_export(ED);
            break;
    }
}


static void
run_edit(const char *name)
{
    if (strcmp(name, "Title") == 0) {
        edit_title();
    } else if (strcmp(name, "Structured") == 0) {
        ed_set_structured(ED, 1);
    } else if (strcmp(name, "Unstructured") == 0) {
        ed_set_structured(ED, 0);
    } else if (strcmp(name, "Section") == 0 || strcmp(name, "Document") == 0) {
        toggle_section_only();
    }
}


/*
 * Write the names of the entries of one menu level on one line: the path of
 * the level (the names of the entries that were chosen to reach it, joined
 * by colons), a tab, and the names joined by bars.
 */
static void
dump_level(FILE *out, const char *path, const MenuItem *items, int n)
{
    fprintf(out, "menu\t%s\t", path);

    for (int i = 0; i < n; i++) {
        fprintf(out, "%s%s", i > 0 ? "|" : "", items[i].name);
    }

    fprintf(out, "\n");
}


/* Write the level and every level below it. */
static void
dump_tree(FILE *out, const char *path, const MenuItem *items, int n)
{
    dump_level(out, path, items, n);

    for (int i = 0; i < n; i++) {
        if (items[i].sub != NULL) {
            char sub[160];

            snprintf(sub, sizeof sub, "%s%s%s", path,
                     path[0] != '\0' ? ":" : "", items[i].name);
            dump_tree(out, sub, items[i].sub, items[i].nsub);
        }
    }
}


void
ui_dump_menus(FILE *out)
{
    /*
     * The entries of Edit that the document makes possible: an unstructured
     * document, a structured document that can be made unstructured, and
     * one that cannot, with Section or with Document.
     */
    const MenuItem *variants[5][4] = {
        {&edit_title_item, &edit_struct_item, &edit_window_item, NULL},
        {&edit_title_item, &edit_unstruct_item, &edit_section_item,
         &edit_window_item},
        {&edit_title_item, &edit_unstruct_item, &edit_document_item,
         &edit_window_item},
        {&edit_title_item, &edit_section_item, &edit_window_item, NULL},
        {&edit_title_item, &edit_document_item, &edit_window_item, NULL}};

    dump_tree(out, "", top_items, TOP_COUNT);

    for (int v = 0; v < 5; v++) {
        MenuItem edit[4];
        int ne = 0;

        for (int i = 0; i < 4 && variants[v][i] != NULL; i++) {
            edit[ne++] = *variants[v][i];
        }

        dump_level(out, "Edit", edit, ne);
    }
}


/* The F12 menu: choose a command and run it. */
static void
menu_bar(void)
{
    int path[MENU_MAXDEPTH];
    MenuItem top[TOP_COUNT];
    MenuItem edit[4];
    int ne = 0;

    /* What Edit offers depends on the document. */
    edit[ne++] = edit_title_item;

    if (ED->doc->structured == 0) {
        edit[ne++] = edit_struct_item;
    } else if (doc_can_unstructure(ED->doc) != 0) {
        edit[ne++] = edit_unstruct_item;
    }

    if (ED->doc->structured != 0) {
        edit[ne++] = ED->only_s >= 0 ? edit_document_item :
                                       edit_section_item;
    }

    MenuItem wsub[WIN_COUNT];

    memcpy(wsub, window_items, sizeof wsub);
    wsub[WIN_OPEN].avail = A->nwin == 1;
    wsub[WIN_CLOSE].avail = A->nwin == 2;
    wsub[WIN_ADJUST].avail = A->nwin == 2 && A->wzoom == 0;
    wsub[WIN_ZOOM_UP].avail = A->nwin == 2 && A->wzoom == 0;
    wsub[WIN_ZOOM_DOWN].avail = A->nwin == 2 && A->wzoom != 0;
    wsub[WIN_SWITCH].avail = A->nwin == 2;
    edit[ne] = edit_window_item;
    edit[ne].sub = wsub;
    ne++;
    memcpy(top, top_items, sizeof top);
    top[TOP_EDIT].sub = edit;
    top[TOP_EDIT].nsub = ne;

    int n = menu_run(top, TOP_COUNT, draw_base, path);

    if (n == 0) {
        return;
    }

    switch (path[0]) {
        case TOP_FILE:
            run_file(path[1]);
            break;

        case TOP_GLOBAL:
            if (path[1] == 0) {
                globalmenu_format(ED);
            } else if (path[1] == 1) {
                globalmenu_page(ED);
            } else if (path[1] == 2) {
                globalmenu_typeface(ED);
            } else if (path[1] == 3) {
                globalmenu_marker(ED);
            } else if (path[1] == 5) {
                globalmenu_named(ED, path[2]);
            } else if (path[1] == 4) {
                options_menu();
            }

            break;

        case TOP_EDIT:
            if (strcmp(edit[path[1]].name, "Window") == 0 && n >= 3) {
                run_window(path[2]);
            } else {
                run_edit(edit[path[1]].name);
            }

            break;

        case TOP_PRINT:
            if (path[1] == 0 || path[1] == 1) {
                printmenu_print(ED, path[1] == 0);
            } else if (path[1] == 2) {
                printmenu_preview(ED, path[2] == 0 ? PV_DOCUMENT :
                                      path[2] == 1 ? PV_BLOCK :
                                                     PV_SECTION);
            } else if (path[1] == 3) {
                find_error(1);
            } else if (path[1] == 4) {
                find_error(0);
            } else if (path[1] == 5) {
                remove_error_here();
            } else if (path[1] == 6) {
                remove_all_errors();
            }

            break;

        case TOP_TOOLS:
            if (path[1] == 0) {
                switch (path[2]) {
                    case 0:
                        macro_edit(ED);
                        break;

                    case 1:
                        macro_delete(ED);
                        break;

                    case 2:
                        macro_learn(ED);
                        break;

                    case 3:
                        macro_save(ED);
                        break;

                    default:
                        macro_retrieve(ED);
                        break;
                }
            } else if (path[1] == 1) {
                switch (path[2]) {
                    case 0:
                        library_edit(ED, note_editor);
                        break;

                    case 1:
                        library_delete(ED);
                        break;

                    case 2:
                        library_save(ED);
                        break;

                    default:
                        library_retrieve(ED);
                        break;
                }
            } else if (path[1] == 2) {
                do_spell(path[2]);
            } else if (path[1] == TOOLS_THESAURUS) {
                do_thesaurus();
            } else if (path[1] == 5) {
                do_count();
            } else if (path[1] == 6) {
                undo_undo(ED);
            } else if (path[1] == 7) {
                undo_redo(ED);
            } else if (path[1] == 4 && path[2] == 0) {
                ed_sort_sections(ED, 0);
            } else if (path[1] == 4) {
                autosort_table();
            }

            break;

        default:
            do_quit();
            break;
    }
}


/* =============================================================== keys */
static void
finish_selection(void)
{
    int op = A->pending_op;
    int mv = A->pending_move;

    int lib = A->lib_pending;
    int done;

    if (op == OP_SPELL) {
        int lo;
        int hi;

        ed_sel_range(ED, &lo, &hi);
        ed_sel_cancel(ED);
        A->pending_op = -1;
        A->pending_move = 0;
        spellui_check(ED, lo, hi);

        return;
    }

    if (op == OP_CASE) {
        ed_case_apply(ED, A->case_mode);
        A->pending_op = -1;
        A->pending_move = 0;

        return;
    }

    if (op == OP_BORDER) {
        A->pending_op = -1;
        A->pending_move = 0;
        bordermenu_run(ED);

        return;
    }

    if (op == OP_MATH) {
        ed_math_apply(ED);
        A->pending_op = -1;
        A->pending_move = 0;

        return;
    }

    if (mv != 0) {
        A->moving = 1;
        A->move_steps = 0;
        A->pending_op = -1;
        A->pending_move = 0;
        ed_msg(ED, "%s",
               ED->sel.kind == SEL_COLUMN ?
                   "Move column: Left, Right one column; Home, End first, "
                   "last; Enter accepts, Esc cancels" :
               ED->sel.kind == SEL_BLOCK ?
                   "Move block: Up, Down one block; Home, End first, last "
                   "block of the section; Enter accepts, Esc cancels" :
                   "Move section: Up, Down one section; Home, End first, "
                   "last at the same level; Left, Right one level; PgUp, "
                   "PgDn next peer section; Enter accepts, Esc cancels");

        return;
    }

    done = ed_sel_apply(ED, op);

    A->lib_pending = 0;

    if (done == 0 && lib != 0) {
        library_add_clip(ED);
    }

    A->pending_op = -1;
    A->pending_move = 0;
}


/* The message that the message line showed before the last key. */
static char last_msg[160];


/*
 * The place that F1 asks the help about when no message has a topic: the text
 * editor of a marker (such as "Text Editor:Note"), the marker that is under
 * the cursor (such as "Marker:Note"), or the Edit Screen.
 */
static const char *
editor_help_place(void)
{
    static char place[96];
    Text *t = ed_text(ED);

    if (nested_title != NULL) {
        snprintf(place, sizeof place, "Text Editor:%s", nested_title);

        return place;
    }

    if (t != NULL && ED->cur.tok < t->n &&
        t->v[ED->cur.tok].kind == TK_MARKER) {
        const MInfo *mi = marker_info(tok_mtype(&t->v[ED->cur.tok]));

        if (tok_mtype(&t->v[ED->cur.tok]) == MT_PRINT_ERROR) {
            return "Print Errors";
        }

        if (mi != NULL) {
            snprintf(place, sizeof place, "Marker:%s", mi->name);

            return place;
        }
    }

    return "Edit Screen";
}


/*
 * F1 on the Edit Screen: the help for the message that was on the message
 * line, when there is a topic for it, and the help for the place when not.
 */
static void
editor_help(void)
{
    int t = -1;

    if (last_msg[0] != '\0') {
        t = help_find_message(w_help, last_msg);
    }

    if (t < 0) {
        t = help_find_context(w_help, editor_help_place());
    }

    help_viewer(t);
}


/*
 * The commands that have a function key.  This table is both what the keys
 * run (fkey_run) and what the Ctrl-F list shows (alt_commands), so the two
 * cannot differ.  mod is 0 for the key alone, 1 for Ctrl and 2 for Alt.
 */
typedef struct {
    const char *name;
    int mod;
    int n; /* the number of the function key, 1 to 12 */
    void (*run)(void);
} FKey;


static void
fk_help(void)
{
    editor_help();
}


static void
fk_split(void)
{
    cmd_split();
}


static void
fk_copy(void)
{
    choose_unit_and_select("Copy", OP_COPY, 0, 1);
}


static void
fk_cut(void)
{
    choose_unit_and_select("Cut", OP_CUT, 0, 1);
}


static void
fk_paste(void)
{
    ed_paste(ED);
}


static void
fk_format(void)
{
    block_format_editor();
}


static void
fk_move(void)
{
    const char *it[] = {"Column", "Block", "Section"};
    int n = ED->doc->structured != 0 ? 3 : 2;
    int c = popup_menu("Move", it, n, 0);

    if (c < 0) {
        return;
    }

    sel_start(c == 0 ? SEL_COLUMN : c == 1 ? SEL_BLOCK : SEL_SECTION, OP_CUT,
              1);
}


static void
fk_table(void)
{
    const char *it[] = {"Add column", "Split column here"};
    int c = popup_menu("Table", it, 2, 0);

    if (c == 0) {
        ed_append_column(ED);
    } else if (c == 1) {
        ed_split(ED, 0);
    }
}


static void
fk_search(void)
{
    cmd_search(0);
}


static void
fk_menu(void)
{
    menu_bar();
}


static void
fk_learn(void)
{
    macro_learn(ED);
}


static void
fk_math(void)
{
    choose_unit_and_select("Math of", OP_MATH, 0, 1);
}


static void
fk_libcopy(void)
{
    choose_unit_and_select("Copy to Library", OP_COPY, 0, 1);

    if (ED->sel.kind != SEL_NONE) {
        A->lib_pending = 1;
    }
}


static void
fk_libcut(void)
{
    choose_unit_and_select("Cut to Library", OP_CUT, 0, 1);

    if (ED->sel.kind != SEL_NONE) {
        A->lib_pending = 1;
    }
}


static void
fk_libpaste(void)
{
    library_paste(ED);
}


static void
fk_case(void)
{
    cmd_case();
}


static void
fk_preverr(void)
{
    find_error(0);
}


static void
fk_nexterr(void)
{
    find_error(1);
}


static void
fk_searchagain(void)
{
    search_again();
}


static void
fk_undo(void)
{
    undo_undo(ED);
}


static void
fk_redo(void)
{
    undo_redo(ED);
}


static void
fk_marker(void)
{
    cmd_marker();
}


static void
fk_append(void)
{
    cmd_append();
}


static void
fk_join(void)
{
    cmd_join();
}


static void
fk_delete(void)
{
    choose_unit_and_select("Delete", OP_DELETE, 0, 1);
}


static void
fk_overtype(void)
{
    ED->overtype = (ED->overtype == 0);
    ed_msg(ED, ED->overtype ? "Overtype on" : "Overtype off");
}


static void
fk_setattr(void)
{
    cmd_setattr();
}


static void
fk_goto(void)
{
    cmd_goto();
}


static void
fk_border(void)
{
    ed_sel_begin(ED, SEL_COLUMN);
    A->pending_op = OP_BORDER;
    A->pending_move = 0;
    ed_msg(ED, "Use arrow keys to highlight column(s); then press Enter");
}


static void
fk_replace(void)
{
    cmd_search(1);
}


static const FKey fkeys[] = {
    {"Help", 0, 1, fk_help},
    {"Split", 0, 2, fk_split},
    {"Copy", 0, 3, fk_copy},
    {"Cut", 0, 4, fk_cut},
    {"Paste", 0, 5, fk_paste},
    {"Format", 0, 6, fk_format},
    {"Move", 0, 7, fk_move},
    {"Table", 0, 8, fk_table},
    {"Search", 0, 9, fk_search},
    {"Menu", 0, 12, fk_menu},
    {"Learn", 1, 1, fk_learn},
    {"Math", 1, 2, fk_math},
    {"Lib Copy", 1, 3, fk_libcopy},
    {"Lib Cut", 1, 4, fk_libcut},
    {"Lib Paste", 1, 5, fk_libpaste},
    {"Case", 1, 6, fk_case},
    {"Prev Error", 1, 7, fk_preverr},
    {"Next Error", 1, 8, fk_nexterr},
    {"Search Again", 1, 9, fk_searchagain},
    {"Undo", 1, 10, fk_undo},
    {"Redo", 1, 11, fk_redo},
    {"Marker", 1, 12, fk_marker},
    {"Append", 2, 2, fk_append},
    {"Join", 2, 3, fk_join},
    {"Delete", 2, 4, fk_delete},
    {"Overtype", 2, 5, fk_overtype},
    {"Set Attribute", 2, 6, fk_setattr},
    {"Go To", 2, 7, fk_goto},
    {"Border", 2, 8, fk_border},
    {"Replace", 2, 9, fk_replace},
    {"Marker", 2, 12, fk_marker}};

#define FKEY_COUNT ((int) (sizeof fkeys / sizeof fkeys[0]))


/* The name of the key of an entry: F3, Ctrl-F9, Alt-F2. */
static void
fkey_keyname(const FKey *f, char *out, size_t cap)
{
    snprintf(out, cap, "%s%sF%d", f->mod == 1 ? "Ctrl-" : "",
             f->mod == 2 ? "Alt-" : "", f->n);
}


/* Run the command of a function key; 1 when there is one. */
static int
fkey_run(int mod, int n)
{
    for (int i = 0; i < FKEY_COUNT; i++) {
        if (fkeys[i].mod == mod && fkeys[i].n == n) {
            fkeys[i].run();

            return 1;
        }
    }

    return 0;
}


void
ui_print_fkeys(void)
{
    for (int i = 0; i < FKEY_COUNT; i++) {
        char key[24];

        fkey_keyname(&fkeys[i], key, sizeof key);
        printf("%s\t%s\n", fkeys[i].name, key);
    }
}


/* The keys that are not function keys but that a terminal may not pass. */
static const char *const other_keys[] = {
    "Promote section (Alt-[)", "Demote section (Alt-])",
    "Soft hyphen (Alt-_)"};

enum {
    OTHER_COUNT = 3
};


/*
 * The Ctrl-F list: every command that has a function key, with the key, and
 * the commands of Alt-[, Alt-] and the soft hyphen, for a terminal that does
 * not pass the Alt function keys or Alt with another key.
 */
static void
alt_commands(void)
{
    static char labels[FKEY_COUNT][40];
    const char *it[FKEY_COUNT + OTHER_COUNT];
    int c;

    for (int i = 0; i < FKEY_COUNT; i++) {
        char key[24];

        fkey_keyname(&fkeys[i], key, sizeof key);
        snprintf(labels[i], sizeof labels[i], "%s (%s)", fkeys[i].name, key);
        it[i] = labels[i];
    }

    for (int i = 0; i < OTHER_COUNT; i++) {
        it[FKEY_COUNT + i] = other_keys[i];
    }

    c = popup_menu("Commands", it, FKEY_COUNT + OTHER_COUNT, 0);

    if (c < 0) {
        return;
    }

    if (c < FKEY_COUNT) {
        fkeys[c].run();
    } else if (c == FKEY_COUNT) {
        section_move_level(0);
    } else if (c == FKEY_COUNT + 1) {
        section_move_level(1);
    } else {
        ed_insert_char(ED, SHY_CTRL);
        layout_precise = 1;
    }
}


/*
 * A key while the highlighted columns, blocks or sections of a Move are
 * moving: the cursor keys move them a step, Enter keeps them where they are
 * and Esc takes them back to where they were.
 */
static void
move_mode_key(int k)
{
    int base = K_BASE(k);
    int plain = (k & ~0xFFFF) == 0;
    int dir = -1;

    if (plain != 0 && (base == 13 || base == 10)) {
        ed_sel_cancel(ED);
        A->moving = 0;
        ed_msg(ED, "Moved");

        return;
    }

    if (base == K_ESC) {
        for (int i = 0; i < A->move_steps; i++) {
            undo_undo(ED);
        }

        ed_sel_cancel(ED);
        A->moving = 0;
        ed_msg(ED, "Cancelled");

        return;
    }

    if (plain != 0) {
        dir = base == K_UP     ? MOVE_UP :
              base == K_DOWN   ? MOVE_DOWN :
              base == K_HOME   ? MOVE_HOME :
              base == K_END    ? MOVE_END :
              base == K_PGUP   ? MOVE_PGUP :
              base == K_PGDN   ? MOVE_PGDN :
              base == K_LEFT   ? MOVE_LEFT :
              base == K_RIGHT  ? MOVE_RIGHT :
                                 -1;
    }

    if (dir < 0) {
        ed_msg(ED, "Moving: the cursor keys move it, Enter accepts, Esc "
                   "cancels");

        return;
    }

    if (ed_move_step(ED, dir) == 0) {
        A->move_steps++;
    }

    layout_precise = 1;
}


static void
handle_key_inner(int k)
{
    int base = K_BASE(k);
    int shift = k & KM_SHIFT;
    int ctrl = k & KM_CTRL;
    int alt = k & KM_ALT;
    int rows = text_rows();

    str_copy(last_msg, sizeof last_msg, ED->msg);
    ED->msg[0] = 0;

    /* A macro runs when Alt and its key are pressed. */
    if (macro_play(k) != 0) {
        return;
    }

    /* The columns, blocks or sections that Move highlighted are moving. */
    if (A->moving != 0) {
        move_mode_key(k);

        return;
    }

    /* highlight mode */
    if (ED->sel.kind != SEL_NONE) {
        if (base == 13 && (k & ~0xFFFF) == 0) {
            finish_selection();
            return;
        }

        if (base == K_ESC) {
            ed_sel_cancel(ED);
            A->pending_op = -1;
            A->lib_pending = 0;
            ed_msg(ED, "Cancelled");
            return;
        }
    }

    /* In Autosort, Home and End go to the edges of the section. */
    if (ED->sel.kind == SEL_BLOCK && A->pending_op == OP_SORT &&
        (base == K_HOME || base == K_END) && (k & ~0xFFFF) == 0) {
        ed_sel_section_edge(ED, base == K_END);

        return;
    }

    /* movement */
    int mv = -1;

    switch (base) {
        case K_LEFT:
            mv = ctrl != 0 ? MV_WORD_LEFT : shift != 0 ? MV_COL_LEFT : MV_LEFT;
            break;

        case K_RIGHT:
            mv = ctrl != 0  ? MV_WORD_RIGHT :
                 shift != 0 ? MV_COL_RIGHT :
                              MV_RIGHT;
            break;

        case K_UP:
            mv = shift != 0 ? MV_SECT_HEAD : MV_UP;
            break;

        case K_DOWN:
            mv = shift != 0 ? MV_SECT_NEXT_HEAD : MV_DOWN;
            break;

        case K_HOME:
            mv = shift != 0 ? MV_DOC_START :
                 ctrl != 0  ? MV_BLOCK_START :
                              MV_HOME;
            break;

        case K_END:
            mv = shift != 0 ? MV_DOC_END : ctrl != 0 ? MV_BLOCK_END : MV_END;
            break;

        case K_PGUP:
            mv = shift != 0 ? MV_SECT_PREV_SIB : MV_PGUP;
            break;

        case K_PGDN:
            mv = shift != 0 ? MV_SECT_NEXT_SIB : MV_PGDN;
            break;

        default:
            break;
    }

    if (mv >= 0) {
        ed_move(ED, mv, rows);
        layout_precise = 1;

        return;
    }

    if (ED->sel.kind != SEL_NONE && A->pending_op == OP_SORT &&
        (k & ~0xFFFF) == 0 && (base == '+' || base == '-')) {
        ED->sort_descending = (ED->sort_descending == 0);
        ed_msg(ED, ED->sort_descending != 0 ? "Sort direction: descending" :
                                              "Sort direction: ascending");

        return;
    }

    if (ED->sel.kind != SEL_NONE && k != K_F(1)) {
        ed_msg(ED, "Highlighting: move the cursor, Enter to accept, Esc to "
                   "cancel");
        return;
    }

    /* alt keys */
    if (alt != 0 && ctrl == 0) {
        if (base >= '1' && base <= '9') {
            ed_fold(ED, 3, base - '0');
            return;
        }

        if (base == '0') {
            ed_fold(ED, 4, 0);
            return;
        }

        switch (base) {
            case '/':
                undo_redo(ED);
                return; /* Alt-/ */

            case '_':
                ed_insert_char(ED, SHY_CTRL);
                layout_precise = 1;
                return; /* Alt-_ (Alt-Shift-Hyphen): a soft hyphen */

            case '-':
                ed_fold(ED, 0, 0);
                return;

            case '+':
            case '=':
                ed_fold(ED, 1, 0);
                return;

            case '*':
                ed_fold(ED, 2, 0);
                return;

            case '[':
            case '<':
            case ',':
                section_move_level(0);
                return;

            case ']':
            case '>':
            case '.':
                section_move_level(1);
                return;

            case 'i':
            case 'I':
                ed_toggle_attr(ED, ATTR_ITALIC);
                return;

            case 'o':
            case 'O':
                ED->overtype = (ED->overtype == 0);
                ed_msg(ED, ED->overtype ? "Overtype on" : "Overtype off");
                return;

            case K_BKSP:
                ed_delete_word(ED, 0);
                return;

            default:
                break;
        }
    }

    /* function keys */
    if (base >= K_F(1) && base <= K_F(12)) {
        int n = base - K_F1 + 1;

        /*
         * Shift has no function key command: the key strip shows empty
         * labels while Shift is held.  When Shift is held with Ctrl or
         * Alt, the Ctrl or Alt command is taken.  F12 is the menu key and
         * is reserved for it; Ctrl-F12 and Alt-F12 are the Marker command.
         * The Ctrl-F1 that ends a recording is taken by the recorder before
         * it gets here.
         */
        if (shift != 0 && ctrl == 0 && alt == 0) {
            return;
        }

        if (alt != 0) {
            if (fkey_run(2, n) == 0) {
                ed_msg(ED, "Alt-F%d: not available", n);
            }

            return;
        }

        if (ctrl != 0 && fkey_run(1, n) != 0) {
            return;
        }

        fkey_run(0, n);

        return;
    }

    if (base == K_INS) {
        if (shift != 0) {
            ed_append_section(ED, 1);
        } else {
            ed_append_section(ED, 0);
        }

        return;
    }

    if (base == K_DEL) {
        ed_delete(ED);
        layout_precise = 1;

        return;
    }

    if (base == K_BKSP) {
        ed_backspace(ED);
        layout_precise = 1;

        return;
    }

    if (base == K_ESC) {
        return;
    }

    /* Ctrl-Shift-Space: a space that a line is never broken at. */
    if (k == K_NBSP) {
        ed_insert_char(ED, NBSP_BYTE);
        layout_precise = 1;

        return;
    }

    /* control keys (plain ASCII 1..26) */
    if ((k & ~0xFF) == 0) {
        switch (k) {
            case 1:
                ed_append_block(ED, 0);
                return; /* Ctrl-A */

            case 2:
                ed_toggle_attr(ED, ATTR_BOLD);
                return; /* Ctrl-B */

            case 3:
                spellui_word(ED);
                return; /* Ctrl-C */

            case 4:
                ed_delete_word(ED, 1);
                return; /* Ctrl-D */

            case 5:
                block_format_editor();
                return; /* Ctrl-E */

            /* Ctrl-F: Alt-command menu (terminal-friendly) */
            case 6:
                alt_commands();
                return;

            case 7:
                ed_insert_text_indent(ED);
                layout_precise = 1;
                return; /* Ctrl-G */

            case 8:
                hanging_indent();
                return; /* Ctrl-H */

            case 9:
                ed_insert_tab(ED, ' ');
                return; /* Tab */

            case 10:
            case 13:
                ed_insert_nl(ED);
                return; /* Enter */

            /* Ctrl-K: cut block */
            case 11: {
                ED->sel.kind = SEL_BLOCK;
                ED->sel.anchor = ED->cur;
                ed_sel_apply(ED, OP_CUT);
                return;
            }

            /* Ctrl-L */
            case 12:
                if (ED->doc->structured != 0) {
                    ED->sel.kind = SEL_SECTION;
                    ED->sel.anchor = ED->cur;
                    ed_sel_apply(ED, OP_CUT);
                }
                return;

            /* Ctrl-N: normal */
            case 14:
                ed_toggle_attr(ED, 0);
                return;

            case 15:
                ed_append_block(ED, 1);
                return; /* Ctrl-O */

            /* Ctrl-P (extra) */
            case 16:
                printmenu_print(ED, 0);
                return;

            case 17:
                do_quit();
                return; /* Ctrl-Q */

            case 18:
                filemenu_retrieve(ED);
                return; /* Ctrl-R */

            case 19:
                do_save(0);
                return; /* Ctrl-S */

            case 20:
                do_thesaurus();
                return; /* Ctrl-T */

            case 31:
                undo_undo(ED);
                return; /* Ctrl-/ (the byte 1F of a terminal) */

            case 30:
                toggle_section_only();
                return; /* Ctrl-^ */

            case 21:
                ed_toggle_attr(ED, ATTR_UNDERW);
                return; /* Ctrl-U */

            case 22:
                ed_split(ED, 1);
                return; /* Ctrl-V */

            case 23:
                if (ED->mode != NULL &&
                    strcmp(ED->mode, "EQUATION EDIT") == 0) {
                    equation_view();
                }

                return; /* Ctrl-W in the equation editor */

            /* Ctrl-X */
            case 24:
                ED->sel.kind = SEL_TEXT;
                ED->sel.anchor = ED->cur;
                A->pending_op = OP_CUT;
                A->pending_move = 0;
                ed_msg(ED, "Highlight "
                           "text, Enter to cut");
                return;

            case 25:
                ed_split(ED, 2);
                return; /* Ctrl-Y */

            case 26:
                undo_undo(ED);
                return; /* Ctrl-Z */

            default:
                break;
        }

        if (k >= 32 && k <= 255) {
            ed_insert_char(ED, u8(k));
            layout_precise = 1;

            return;
        }
    }
}

/* The name of the operation that a key makes, for "Undone: Paste". */
static const char *
undo_label(int k, int cls, int sel, int pending)
{
    int base = K_BASE(k);

    if (cls == 1) {
        return "Typing";
    }

    if (cls == 2) {
        return "Deleting";
    }

    if (sel != SEL_NONE && base == 13) {
        return pending == OP_CUT ? "Cut" :
               pending == OP_DELETE ? "Delete" :
               pending == OP_SORT ? "Sort" :
               pending == OP_CASE ? "Case" :
               pending == OP_MATH ? "Math" :
               pending == OP_BORDER ? "Border" :
                                    "Edit";
    }

    if (base == 13) {
        return "New line";
    }

    if (base == K_F(5) && (k & (KM_CTRL | KM_ALT)) == 0) {
        return "Paste";
    }

    if (base == K_F(2) && (k & (KM_CTRL | KM_ALT)) == 0) {
        return "Split";
    }

    return "Edit";
}


/* The class of a key for the grouping of typing: see UndoKey. */
static int
undo_key_class(int k, int sel)
{
    int base = K_BASE(k);

    if (sel != SEL_NONE || (k & (KM_CTRL | KM_ALT | KM_SHIFT)) != 0) {
        return 0;
    }

    if (base == K_BKSP || base == K_DEL) {
        return 2;
    }

    return (k & ~0xFF) == 0 && (k >= 32 || k == 9) ? 1 : 0;
}


/*
 * Handle a key, and keep what it changed for Undo: the document is compared
 * with the copy that was made after the key before.
 */
static void
handle_key(int k)
{
    Editor *e0 = ED;
    Doc *d0 = ED->doc;
    int sel = ED->sel.kind;
    int pending = A->pending_op;
    int cls = undo_key_class(k, sel);
    UndoKey uk;

    undo_before_key(e0, &uk, cls, k);
    handle_key_inner(k);

    if (ED == e0) {
        picture_refresh();
        picture_note();
        latex_note();
    }

    if (ED != e0) {
        return;
    }

    if (ED->doc != d0) {
        undo_reset(ED);

        return;
    }

    undo_after_key(e0, &uk, undo_label(k, cls, sel, pending));
}


/* =============================================================== Document
   Manager */


static int
docmgr(char *fname_out, size_t cap)
{
    const char *it[] = {"Edit / Preview / Print Document",
                        "Maintain Dictionaries",
                        "Compare Document Versions",
                        "Quit"};

    for (;;) {
        term_clear();
        int C = term_cols();

        term_fill(0, 0, C, 1, ' ', TS_STATUS);
        term_puts(2, 0,
                  "Strata - "
                  "Document Manager",
                  TS_STATUS);
        term_puts(C - 40, 0, "F1 Help", TS_STATUS);
        term_present();
        int c = popup_menu("Document Manager", it, 4, 0);

        if (c < 0) {
            continue;
        }

        if (c == 3) {
            return 0;
        }

        if (c == 1) {
            dict_panel(".");

            continue;
        }

        if (c == 2) {
            compare_panel(".", "");

            continue;
        }

        if (c == 0) {
            fname_out[0] = 0;
            const char *how[] = {"Enter a file name",
                                 "Choose from this "
                                 "directory",
                                 "New document"};
            int h = popup_menu("Edit / Preview / Print Document", how, 3, 0);

            if (h == 0) {
                if (popup_name_input("Edit / Preview / Print Document",
                                "Document name:", fname_out,
                                isz(cap)) != 0 &&
                    fname_out[0] != '\0') {
                    return 1;
                }
            } else if (h == 1) {
                return 2;
            } else if (h == 2) {
                return 3;
            }

            continue;
        }

    }
}


/* =============================================================== main loop */
int
app_run(App *a, const char *initial)
{
    /* the defaults of the Editor */
    A = a;
    a->pending_op = -1;
    w_redraw = draw_base;
    layout_show_marks = 1;

    /* The settings of the user. */
    Config cfg;

    config_load(&cfg);
    layout_show_marks = cfg.show_marks;
    layout_show_dots = cfg.show_dots;
    latex_markers_off = cfg.latex_markers_off;
    autosave_on = cfg.autosave;
    autosave_seconds = cfg.autosave_seconds;
    undo_set_levels(cfg.undo_levels);
    /* The printer list is looked for in the background, once. */
    pj_printers_prefetch();

    if (term_open() != 0) {
        fprintf(stderr, "stred needs an interactive terminal "
                        "(or an X display with the x11 build)\n");
        return 1;
    }

    term_set_title("stred");
    equations_init();
    marker_picture_fn = picture_check;
    char fname[512] = "";

    if (a->help == NULL && a->helppath[0] != '\0') {
        a->help = help_load(a->helppath);
    }

    w_help = a->help;
    Doc *d = NULL;
    char err[160];

    int started_rec = -1;

    if (initial != NULL && *initial != '\0') {
        /* No editor exists yet: the box has no screen of a document to
           repaint behind it. */
        w_redraw = NULL;
        d = recoverui_load(initial, err, sizeof err, "Cannot open",
                           &started_rec);
        w_redraw = draw_base;

        if (d == NULL) {
            FILE *f = fopen(initial, "rb");

            if (f != NULL) {
                fclose(f);
                term_close();
                log_error("stred: cannot open the document %s: %s", initial,
                          err);
                fprintf(stderr, "%s: %s\n", initial, err);
                return 1;
            }

            if (a->template_path[0] != '\0') {
                d = doc_load(a->template_path, err, sizeof err);

                if (d == NULL) {
                    term_close();
                    fprintf(stderr, "%s: %s\n", a->template_path, err);
                    return 1;
                }

                /* A document made from a template starts at the top. */
                doc_clear_view_pos(d);
            } else {
                d = doc_new((a->new_flat == 0));
                doc_default_paper(d);
            }

            free(d->path);
            d->path = strdup(initial);

            /* A blank description or author keeps the template's. */
            if (a->description[0] != '\0' || a->template_path[0] == '\0') {
                doc_header_set(d, "MD", a->description);
            }

            if (a->author[0] != '\0' || a->template_path[0] == '\0') {
                doc_header_set(d, "MA", a->author);
            }
        }
    }

    a->ed = ed_new(d != NULL ? d : doc_new(1));
    a->win[0] = a->ed;
    a->nwin = 1;
    a->ed->hide_title = 1;
    ed_fix_cursor(a->ed);
    ed_restore_position(a->ed);

    if (started_rec >= 0) {
        ed_msg(a->ed, "Recovered %d blocks of %s; the original is not changed",
               started_rec, initial);
    }

    const char *charset_notice = term_charset_notice();

    if (charset_notice != NULL && started_rec < 0) {
        ed_msg(a->ed, "%s", charset_notice);
    }

    if (d == NULL) {
        /* Document Manager first */
        for (;;) {
            int r = docmgr(fname, sizeof fname);

            if (r == 0) {
                term_close();
                doc_free(a->ed->doc);
                ed_free(a->ed);
                help_free(a->help);
                return 0;
            }

            if (r == 1) {
                doc_name_complete(fname, sizeof fname);

                int rec = -1;
                Doc *nd = recoverui_load(fname, err, sizeof err, "Cannot open",
                                         &rec);

                if (nd == NULL) {
                    FILE *f = fopen(fname, "rb");

                    if (f != NULL) {
                        fclose(f);
                        popup_msg("Cannot open", err);
                        continue;
                    }

                    if (popup_yn("File does not exist. Create it?") == 0) {
                        continue;
                    }

                    nd = doc_new(1);
                    doc_default_paper(nd);
                    nd->path = strdup(fname);
                }

                load_into_editor(nd);
                break;
            }

            if (r == 2) {
                if (do_retrieve(NULL) == 0) {
                    break;
                }

                continue;
            }

            if (r == 3) {
                do_new();
                break;
            }

        }
    }

    ED->opt_expanded = cfg.expanded_status;
    ED->opt_keys = cfg.onscreen_help;
    ED->opt_backups = cfg.backups;

    /*
     * The macro set and the library of the settings are loaded before the
     * first key is read.  They are not a change of the document and make no
     * Undo operation.  A file that cannot be used is a message and a line of
     * the log; the Editor starts anyway.
     */
    {
        char why[PATH_MAX + 200];

        if (cfg.initial_macros[0] != '\0' &&
            macro_load_initial(filemenu_workdir(ED), cfg.initial_macros, why,
                               sizeof why) < 0) {
            log_error("stred: macro set not loaded: %s", why);
            ed_msg(ED, "Macro set not loaded: %s", why);
        }

        if (cfg.initial_library[0] != '\0' &&
            library_load_initial(filemenu_workdir(ED), cfg.initial_library,
                                 why, sizeof why) < 0) {
            log_error("stred: library not loaded: %s", why);
            ed_msg(ED, "Library not loaded: %s", why);
        }
    }

    /* What str was told by its Setup replaces the file. */
    if (a->autosave_off != 0) {
        autosave_on = 0;
    }

    if (a->autosave_seconds >= 1 && a->autosave_seconds <= 86400) {
        autosave_seconds = a->autosave_seconds;
    }

    redraw();

    /*
     * A drawing that is left out because another key is waiting is made as
     * soon as no key is waiting: until then the keys are read without
     * waiting (term_getkey(0)), and when none comes the drawing is made.
     * Without this a display that is slower than the keyboard repeat builds
     * a queue of keys, and the cursor goes on moving after the key was
     * released.
     */
    int draw_owed = 0;

    while (a->quit == 0) {
        int k = term_getkey(draw_owed != 0 ? 0 :
                            autosave_on != 0 ? 1000 : -1);

        if (k == K_NONE && draw_owed != 0) {
            draw_owed = 0;
            redraw();
            continue;
        }

        if (k == K_NONE && autosave_on != 0) {
            if (autosave_poll() != 0) {
                redraw();
            }

            continue;
        }

        if (k == K_RESIZE || k == K_NONE) {
            redraw();
            continue;
        }

        {
            LayoutSig before = layout_sig();

            handle_key(k);
            layout_after_key(&before);
        }

        if (a->quit != 0) {
            break;
        }

        ed_fix_cursor(ED);

        if (autosave_on != 0) {
            autosave_poll();
        }

        if (term_input_pending() != 0) {
            draw_owed = 1;

            continue;
        }

        draw_owed = 0;
        redraw();
    }

    term_close();

    if (a->nwin == 2) {
        Editor *other = a->win[a->wcur ^ 1];

        doc_free(other->doc);
        ed_free(other);
        a->win[a->wcur ^ 1] = NULL;
        a->nwin = 1;
    }

    doc_free(a->ed->doc);
    ed_free(a->ed);
    a->ed = NULL;
    help_free(a->help);
    a->help = NULL;

    return 0;
}


int
preview_file(App *a, const char *path)
{
    char err[160];
    Doc *d = doc_load(path, err, sizeof err);

    if (d == NULL) {
        log_error("stred: cannot open the document %s for the preview: %s",
                  path, err);
        fprintf(stderr, "%s: %s\n", path, err);

        return 1;
    }

    if (a->helppath[0] != '\0') {
        a->help = help_load(a->helppath);
    }

    A = a;
    w_help = a->help;
    w_redraw = NULL;

    if (term_open() != 0) {
        fprintf(stderr, "Preview needs an interactive terminal "
                        "(or an X display with the x11 build)\n");
        doc_free(d);

        return 1;
    }

    Editor *e = ed_new(d);
    Doc part = *d;
    doc_clear_view_pos(&part);
    int rc = 0;
    DiagList faults;

    part.items = malloc(usz(d->nitems + 1) * sizeof *part.items);
    memcpy(part.items, d->items, usz(d->nitems) * sizeof *part.items);
    diag_list_init(&faults);
    preview_set_banner(a->banner);
    preview_run(e, &part, a->error_log, &faults);
    doc_part_free(&part);

    /*
     * Anything but a plain success is shown, as the user may not see it.
     * With errors in the document the message is the count and the list
     * follows as text when the terminal is given back; the status is 5.
     */
    if (faults.n > 0 && strncmp(e->msg, "Preview done", 12) == 0) {
        rc = 5;
    } else if (strncmp(e->msg, "Preview done", 12) != 0 &&
               strcmp(e->msg, "Preview cancelled") != 0) {
        popup_msg("Preview Document", e->msg);
        rc = 1;
    }

    term_close();

    if (faults.n > 0) {
        char *text = errlist_text(faults.v, faults.n, faults.dropped);

        fprintf(stderr, "%s: the document has errors or warnings:\n", path);

        for (int i = 0; i < faults.n; i++) {
            if (faults.v[i].kind == DIAG_ERROR) {
                fprintf(stderr, "PRINT FAILED: the preview is inaccurate.\n");
                break;
            }
        }

        if (text != NULL) {
            fputs(text, stderr);
            free(text);
        }
    }

    diag_list_free(&faults);
    ed_free(e);
    doc_free(d);

    return rc;
}
