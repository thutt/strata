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

/* editor.h - editor state and operations (no terminal I/O in editor.c/view.c
 * except through the drawing functions in view.c). */
#ifndef EDITOR_H
#define EDITOR_H
#include "diag.h"
#include "msdoc.h"
#include "layout.h"
#include "search.h"

typedef struct {
    int item;
    int col;
    int tok;
} Pos;

struct Undo; /* The history of the changes, in undo.c. */

enum {
    SEL_NONE,
    SEL_TEXT,
    SEL_COLUMN,
    SEL_BLOCK,
    SEL_SECTION
};
typedef struct {
    int kind;
    Pos anchor;
} Sel;

typedef struct {
    int kind;
    Item *items;
    int n;
    Text text;
} Clip;

/* Free a clipboard, and copy one (the copy owns all of its data). */
void
clip_free(Clip *c);


void
clip_copy(Clip *dst, const Clip *src);


/* A copy of a token that owns its marker data. */
Tok
tok_dup(const Tok *k);


/* ---- laid-out view of the document ------------------------------------ */
typedef struct {
    int col;
    int row;
    int x0;
    int x1;
} Seg;
/*
 * A line of the view.  Its cells are not kept: view_line_cells makes them
 * from the layout of the block when they are drawn, so that a large
 * document takes little memory and time.
 */
typedef struct {
    int kind; /* 0 text row, 1 block separator, 2 section separator */
    int item; /* owning item, -1 for none                           */
    int row; /* kind 0: the row of the block                       */
} VLine;
typedef struct {
    int ncl; /* number of columns laid out                         */
    ColLayout *cl; /* one per column                                    */
    int line0; /* first vline */
    int nrows; /* number of text rows */
    int *colx; /* the next three have one entry per column          */
    int *colw;
    int *sepx;
    int visible;
    int loffs; /* the indentation of the block's level */
    int stale; /* the layout must be made again */
} BView;
typedef struct {
    VLine *lines;
    int n;
    int cap;
    BView *bv;
    int nbv;
    int width;
    int valid;
    int all_stale; /* every block must be laid out again */
    int markers_ok; /* the counters need not be numbered again */
    int partial; /* the last change asked for a partial layout only */
} View;

/* ---- editor ------------------------------------------------------------ */
typedef struct {
    Doc *doc;
    Pos cur;
    int want_x;
    /*
     * The row of a displayed picture that the cursor is on, counted from the
     * top row of the picture.  A picture is one line of the text and one
     * stop of the cursor, which stands on its first row; only Page Up,
     * Page Down, Up and Down on a picture that is taller than the window
     * move the cursor down its rows, a window at a time, so that the whole
     * picture can be seen.  The number counts only while the cursor is at
     * the position in prow_at; any other position has the top row.
     */
    int prow;
    Pos prow_at;
    /*
     * Set when the cursor moved onto or off a picture, or down its rows:
     * the next drawing then moves the window to the new top in a few
     * steps (see glide in ui.c) and does not make the text leap.
     */
    int glide;
    Sel sel;
    Clip clip;
    int overtype;
    int hide_title; /* keep the title page out of the text */
    int only_s; /* Edit, Section: the S item of the only section, or -1 */
    int sort_descending; /* the direction of the next Autosort, Table */
    char msg[160];
    View view;
    /* first visible vline                          */
    int top;
    /* page width used for layout (<= 80)           */
    int text_width;
    int opt_expanded;
    int opt_ruler;
    int opt_keys;
    int opt_backups; /* numbered backups of a saved document, 0 to 99 */
    const char *mode; /* the name shown on the status line, or NULL */
    char search[80];
    char replace[80];
    int search_case;

    int search_method; /* What Search looks for: 0 text, 1 a Print Error. */
    int search_back; /* 1: the search repeats backward. */
    int search_wrapped; /* 1: the last search passed the end of the text. */
    SearchOpts sopt; /* The options of the Search panel. */
    SearchOpts ropt; /* The options of the Replace panel. */
    int rep_follow; /* Replace: 1 gives the replacement the case found. */
    int rep_ask; /* Replace: 1 asks at each place, 0 replaces them all. */
    struct Undo *undo; /* The shadow and the patches of Undo, or NULL. */
} Editor;


Editor *
ed_new(Doc *d);


void
ed_free(Editor *e);


/*
 * The status line of the window of e, its expanded line and its ruler, drawn
 * from the screen row y0 down; strip is 1 for the window of the cursor, which
 * also draws the labels of the function keys and the message at the foot of
 * the screen.
 */
void
ed_status_at(Editor *e, int w, int y0, int strip);


/* Move the clipboard of one Editor to another (the other one loses its own). */
void
ed_clip_move(Editor *from, Editor *to);


void
ed_set_doc(Editor *e, Doc *d);


void
ed_msg(Editor *e, const char *fmt, ...);


/* view (view.c) */
/* (re)build the view if stale */
void
ed_layout(Editor *e);


/* Make every block be laid out again. */
void
ed_invalidate(Editor *e);


/*
 * Only the block of the cursor changed its text, as when a character is
 * typed.  Only that block is laid out again.
 */
void
ed_invalidate_block(Editor *e);


/*
 * Items were inserted into the document (n items at index at), or deleted
 * from it, and all of them are blocks without a counter or a
 * cross-reference.  The layouts of the other blocks are kept, and only the
 * blocks next to the change are laid out again.  The document must be in
 * its new state.  If the view cannot be updated this way, every block is
 * laid out again.
 */
void
ed_view_inserted(Editor *e, int at, int n);


void
ed_view_deleted(Editor *e, int at, int n);


/*
 * The cells of line li of the view, ed_layout done: view.width cells are
 * written to out.  nseg and seg give the columns of a text line.
 */
void
view_line_cells(Editor *e, int li, Cell *out);


/* The columns of a line, from x0 up to but not including x1. */
void
view_line_seg(const Editor *e, int li, int s, int *col, int *x0, int *x1);


int
view_line_nseg(const Editor *e, int li);


void
ed_view_free(Editor *e);


/* in vline coords; returns vline */
int
ed_cursor_screen(Editor *e, int *x, int *y);


/*
 * The cursor is on a displayed Picture marker (a picture that is the only
 * thing in its rows; the equation of a displayed block is not one):
 * returns the number of rows of the picture and sets *first to its first
 * row in the block; returns 0 when it is not.
 */
int
ed_picture_rows(Editor *e, int *first);


/* The row of the picture that the cursor is on: 0 for the top row. */
int
ed_picture_row(Editor *e);


/* Put the cursor on row r of the picture it is on (clamped to its rows). */
void
ed_set_picture_row(Editor *e, int r);


void
ed_scroll_to_cursor(Editor *e, int rows);


void
ed_draw(Editor *e, int x0, int y0, int w, int h);


/* draw status/ruler/key lines  */
void
ed_status(Editor *e, int w);

/* movement */
enum {
    MV_LEFT,
    MV_RIGHT,
    MV_UP,
    MV_DOWN,
    MV_HOME,
    MV_END,
    MV_PGUP,
    MV_PGDN,
    MV_WORD_LEFT,
    MV_WORD_RIGHT,
    MV_BLOCK_START,
    MV_BLOCK_END,
    MV_DOC_START,
    MV_DOC_END,
    MV_COL_LEFT,
    MV_COL_RIGHT,
    MV_SECT_HEAD,
    MV_SECT_NEXT_HEAD,
    MV_SECT_PREV_SIB,
    MV_SECT_NEXT_SIB,
    MV_LEVEL_UP,
    MV_LEVEL_DOWN
};


void
ed_move(Editor *e, int mv, int screen_rows);


/* editing */
void
ed_insert_char(Editor *e, uint8_t c);


/*
 * Ctrl-G: the text indent character.  The lines that continue the one that
 * holds it begin below the place after it, until the end of the paragraph.
 */
void
ed_insert_text_indent(Editor *e);


void
ed_insert_nl(Editor *e);


void
ed_insert_tab(Editor *e, uint8_t fill);


void
ed_backspace(Editor *e);


void
ed_delete(Editor *e);


void
ed_delete_word(Editor *e, int forward);


void
ed_toggle_attr(Editor *e, int mask);


/* The flags of the attribute that is in effect at the cursor. */
int
ed_flags_at(Editor *e);


int
ed_attr_at(Editor *e);


int
ed_attr_effective(Editor *e);


void
ed_insert_marker(Editor *e, int type, const uint8_t *data, size_t len);


/* Replace the payload of the marker at token index tok. */
void
ed_replace_marker(Editor *e, int tok, const uint8_t *data, size_t len);


Text *
ed_text(Editor *e);


/* The longest word that the thesaurus looks up (the rest is cut off). */
#define THES_MAXWORD 63

/*
 * The word of the cursor, for the thesaurus.  It is the run of letters (with
 * an apostrophe or a hyphen between letters) that holds the cursor's token;
 * if that is not a letter, the run before the cursor, looking back over what
 * is not a letter on the same line.  An attribute, a marker or a tab ends a
 * word.  Sets the tokens [from, to) and the text (CP437) in buf, which holds
 * cap bytes, and returns 1; returns 0 when there is no word.
 */
int
ed_word_at(Editor *e, int *from, int *to, char *buf, int cap);


/*
 * Replace the tokens [from, to) of the text of the cursor's column by the n
 * bytes of text.  The attributes in force stay as they were, because the new
 * characters take the old place in the token stream.  The cursor goes after
 * the new text and the document is changed.
 */
void
ed_replace_range(Editor *e, int from, int to, const uint8_t *text, int n);


/* structure */
void
ed_append_block(Editor *e, int global);


void
ed_append_column(Editor *e);


void
ed_append_section(Editor *e, int sub);


void
ed_split(Editor *e, int what); /* 0 column, 1 block, 2 section */


/* 1 block, 2 section; returns 0 on success */
int
ed_join(Editor *e, int what);


/* cmd: 0 collapse sect,1 expand sect,2 outline sect,3 level n,4 whole doc */
void
ed_fold(Editor *e, int cmd, int arg);

/* selection / clipboard */
enum {
    OP_COPY,
    OP_CUT,
    OP_DELETE,
    OP_SORT, /* Autosort, Table: sort the highlighted blocks */
    OP_SPELL, /* Tools, Spell: check the highlighted blocks or sections */
    OP_CASE, /* Case (Ctrl-F6): change the case of the highlighted text */
    OP_MATH, /* Math (Ctrl-F2): calculate the highlighted numbers */
    OP_BORDER /* Border (Alt-F8): the Border Layout panel */
};


/*
 * Math (Ctrl-F2): calculate the numbers of the highlighted text (the unit
 * that the selection has), left to right and top to bottom, with calc_eval,
 * and put the result on the clipboard as text.  The message line shows
 * "Result: N", "Cannot divide by zero" (the clipboard is not changed) or why
 * nothing was calculated.  The document is not changed and there is no Undo.
 * Returns 0, or -1 when nothing was calculated.
 */
int
ed_math_apply(Editor *e);


/* The three changes of Case (Ctrl-F6). */
enum {
    CASE_UPPER,
    CASE_LOWER,
    CASE_CAPITALIZE
};


/*
 * The character c (a byte of the code page 437) in the case that mode says.
 * Capitalize needs to know whether c continues a word: *in_word is 0 at
 * the start of the text and is kept up to date by the calls.  A letter
 * is one of A to Z, a to z and the letters of the code page that have both
 * cases (the pairs of ed_case_char in editor.c); every other byte is
 * returned as it is.
 */
int
ed_case_char(int c, int mode, int *in_word);


/*
 * Change the case of the highlighted text (the unit that the selection
 * has: text of one column, a column, blocks or a section), or of the word
 * under the cursor when the highlight of the kind text is empty.  Only
 * letters change: markers (the Print Error ones too), attributes, tabs and
 * line ends stay.  Returns 0 and the message "...N characters changed", or
 * -1 with the message of the reason.  The change is one change of the
 * document (one Undo operation, as the key that called it).
 */
int
ed_case_apply(Editor *e, int mode);


void
ed_sel_begin(Editor *e, int kind);


void
ed_sel_cancel(Editor *e);


int
ed_sel_apply(Editor *e, int op); /* 0 ok; -1 error (msg set) */


int
ed_paste(Editor *e);


/*
 * The steps of Move (F7), in the place: the highlighted columns, blocks or
 * sections move one step at a time.  Up and Down move one block or section;
 * Home and End move to the first and the last place of the same kind in the
 * same section (or among the same peers); PgUp and PgDn move a section to the
 * next peer section of its level, wherever it is; Left and Right move a
 * column left or right and a section one level out or in (always with its
 * subsections).  The highlight stays on what moved.
 */
enum {
    MOVE_UP,
    MOVE_DOWN,
    MOVE_HOME,
    MOVE_END,
    MOVE_PGUP,
    MOVE_PGDN,
    MOVE_LEFT,
    MOVE_RIGHT
};


/* Move the highlight one step.  0 moved; -1 did not (the message shows why). */
int
ed_move_step(Editor *e, int dir);


/*
 * The title page: the blocks of the first section, when a record "=1" in
 * front of it shows that there is one.  Sets the item range [lo, hi) of the blocks
 * and returns 1, or returns 0 when the document has no title page.
 */
int
doc_title_range(const Doc *d, int *lo, int *hi);


/*
 * Switch between structured and unstructured.  A document can become
 * unstructured only when it has a single section.  Returns 0, or -1 with a
 * message set.
 */
int
ed_set_structured(Editor *e, int structured);


/* Can the document become unstructured? */
int
doc_can_unstructure(const Doc *d);


/*
 * Autosort.  Sort the blocks of the current selection, or the subsections of
 * the current section, by the text they start with, ignoring case.  The
 * order is that of the format: digits before letters, and a lower case letter
 * before the same upper case letter.  Returns the number of entries, or -1
 * with a message set.
 */
int
ed_sort_sections(Editor *e, int descending);


/* Autosort, Table: Home and End move the highlight to the section's edge. */
void
ed_sel_section_edge(Editor *e, int end);


/* item range for block/section selections */
void
ed_sel_range(Editor *e, int *lo, int *hi);


/*
 * Move the cursor to a place of the document, unfolding what hides it, and
 * leaving the highlight and Edit, Section if they are in the way.
 */
void
ed_goto(Editor *e, int item, int col, int tok);


/* search */
int
ed_search(Editor *e, const char *needle, int forward, int casefold);


/*
 * Search with the options of spec: the cursor goes to the next place after
 * (forward) or before (backward) the cursor, and wraps round the document.
 * Return 1 when a place was found.  e->search_wrapped tells whether the
 * place was found after the search passed the end (forward) or the
 * beginning (backward) of the document.
 */
int
ed_search_spec(Editor *e, const SearchSpec *spec, int forward);


/*
 * Find the next place for spec, forward only, from the position start
 * (that position too), without wrapping and without moving the cursor.  On
 * a hit *at is the place and *len the number of tokens; return 1, else 0.
 */
int
ed_find_from(Editor *e, const SearchSpec *spec, const Pos *start, Pos *at,
             int *len);


/*
 * As ed_find_from, but only a match that begins before the position limit
 * counts (NULL: no limit, the end of the document).  Replace uses it to go
 * round the document and to stop where it started.
 */
int
ed_find_range(Editor *e, const SearchSpec *spec, const Pos *start,
              const Pos *limit, Pos *at, int *len);


/*
 * Replace the len tokens at the place at by rep (with the case of the text
 * found when follow is not 0).  *after is the place just after the
 * replacement.  Return the number of tokens that were put in.
 */
int
ed_replace_at(Editor *e, const Pos *at, int len, const char *rep, int follow,
              Pos *after);


/*
 * Replace every place of spec in the whole document by rep and return the
 * number of places.  One call is one operation of Undo.
 */
int
ed_replace_all_spec(Editor *e, const SearchSpec *spec, const char *rep,
                    int follow);


int
ed_replace_all(Editor *e, const char *a, const char *b, int casefold);


/*
 * The first block of the section with the number num (as the status line
 * shows it, 2.1), or -1.  It makes one pass over the document, so that Go To
 * section and the section ranges of Print take no longer on a document of
 * thousands of sections than they take on a short one.
 */
int
sect_find(const Doc *d, const char *num);


/* helpers shared with view/ui */
int
sect_start(const Doc *d, int item);


int
sect_blocks_end(const Doc *d, int s);


int
sect_end(const Doc *d, int s);


int
sect_number(const Doc *d, int item, char *buf, int cap);


int
is_edit_block(const Doc *d, int i);


void
ed_fix_cursor(Editor *e);


/*
 * Put the position of the cursor into the document, so that the next save
 * writes it: the item, the column, the token and the screen row.  A cursor
 * at the very start of the text (where a document opens without a position)
 * writes nothing, and removes a position that was there.  The document is
 * not marked as changed.
 */
void
ed_remember_position(Editor *e);


/*
 * Go back to the position that the document holds, and scroll so that the
 * cursor is on the screen row that it had.  A position that is no longer
 * there (the document is shorter now) is moved to the nearest place that
 * is; a document without a position stays at the start.
 */
void
ed_restore_position(Editor *e);


Block *
block_clone_full(const Block *b);


void
ed_doc_stats(const Doc *d, int *words, int *chars, int *blocks, int *sections);
#endif
