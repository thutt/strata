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

/* layout.h - turn a column's tokens into display rows (word wrap, tabs,
 * justification, attributes, markers).  Pure data, no terminal I/O. */
#ifndef LAYOUT_H
#define LAYOUT_H
#include "msdoc.h"

/* display attribute bits carried by every cell */
#define DA_BOLD 0x01
#define DA_UNDER 0x02
#define DA_ITALIC 0x04
#define DA_MARKER 0x08 /* rendered marker text */
#define DA_DIM 0x10 /* ruler, separators */
#define DA_REVERSE 0x20 /* selection / highlight */
#define DA_EQ 0x40 /* a cell under the picture of an equation */
#define DA_KEY 0x80 /* a cell of a keycap, which is drawn in a box */

typedef struct {
    uint8_t ch;
    uint8_t attr;
    int tok;
} Cell; /* ch is CP437 */

/*
 * The cells of a row are kept in the least room they need (a document has
 * millions of them).  A row of plain text, in which cell k is the character
 * token tok0 + k, all with the attribute rattr, keeps nothing (LF_TEXT): its
 * cells are read from the text.  The other rows keep ch and attr of every
 * cell, then nothing (LF_DENSE: the token of cell k is tok0 + k), or a 16
 * bit number per cell (LF_NEAR: the distance from tok0, 0xFFFF for -1) or a
 * 32 bit token per cell (LF_WIDE).  Read them with lrow_cell, giving it the
 * text of the column that the row was laid out from.
 */
enum {
    LF_DENSE,
    LF_NEAR,
    LF_WIDE,
    LF_TEXT
};
#define LROW_PACKED 1

typedef struct {
    uint8_t *cells;
    int n; /* number of cells, left to right, starting at x0 */
    int x0; /* offset of first cell within the column */
    int tok0; /* tokens [tok0,tok1) belong to this row */
    int tok1;
    int pic_tok; /* a row of free cells under a picture: its marker, else -1 */
    uint8_t fmt; /* LF_DENSE, LF_NEAR, LF_WIDE or LF_TEXT */
    uint8_t rattr; /* LF_TEXT: the attribute of all cells */
} LRow;


/* Cell k of a row, 0 <= k < r->n, of the column whose text is t. */
Cell
lrow_cell(const LRow *r, int k, const Text *t);


/* The token of cell k of a row (-1 for a cell that has none). */
int
lrow_tok(const LRow *r, int k);

typedef struct {
    LRow *rows;
    int nrows;
    int width;
} ColLayout;

#define MAXTABS 24
typedef struct {
    int attr;
    int just; /* 0 left, 1 right, 2 center, 3 even */
    int indent;
    int ntab;
    int tabpos[MAXTABS];
    char tabtype[MAXTABS];
    int bl; /* left block indent                          */
    int br; /* right block indent (the print uses it)    */
    int defstep; /* default tab interval when none set        */
    int headline;
    int level;
    char tag[SECT_NUMLEN]; /* section tag drawn before a headline ("1.1.6 "
""), or empty */
} Fmt;


void
fmt_resolve(const Doc *d, int item, int col, Fmt *f);


/* A record of a block: its own, else the one of its named format. */
const char *
fmt_block_prop(const Doc *d, const Block *b, const char *code, char sub);


void
layout_column(const Fmt *f, Text *t, int width, ColLayout *out, int fix_tabs);


void
layout_free(ColLayout *l);


/* where is token index `tok` in a laid-out column? returns row, sets *x */
int
layout_locate(const ColLayout *l, int tok, int *x);


/* token index at (row, x) (nearest cell) */
int
layout_tok_at(const ColLayout *l, int row, int x);


/*
 * Equations typeset as pictures (the X11 version; see docs/EQUATIONS.txt).
 * An equation that is on its own in a line is displayed; eqn_is_display
 * tells whether the Equation marker at token tok is.  The hooks are set by
 * the program when it can show pictures and are NULL otherwise:
 *
 *   layout_eq_size   1 when the picture of the equation is ready, with its
 *                    width in cells and the rows it needs above and below
 *                    the text row (a displayed equation has no text row:
 *                    its rows are the ones above and the one below),
 *                    else 0 (the label is drawn);
 *   layout_eq_image  the pixels of that picture, its size and where it sits
 *                    in the cells that were reserved for it;
 *   layout_eq_want   called for an equation that is on the screen so that
 *                    the picture is made when it does not exist yet.
 */
int
eqn_is_display(const Text *t, int tok);


extern int (*layout_eq_size)(const Text *t, int tok, int display,
                             int *wcells, int *above, int *below);
extern const uint8_t *(*layout_eq_image)(const Text *t, int tok, int display,
                                         int *w, int *h, int *xoff,
                                         int *yoff);
extern void (*layout_eq_want)(const Text *t, int tok, int display);


/* draw end-of-line (0x11) and tab (0x10) glyphs */
extern int layout_show_marks;
extern int layout_show_dots; /* draw a dot for every space */
extern int layout_hide_shy; /* draw no mark for a soft hyphen (export) */
extern const unsigned short cp437_uni[256];
extern const char cp437_ascii[256 + 1];

/* The ASCII look-alike of the Unicode character of a cell. */
char layout_ascii_of(unsigned cp);

/*
 * Names of files are UTF-8 and the screen has the cells of the code page of
 * the document format, one byte for a cell.  utf8_cell returns the cell of
 * the character at s and sets *used to the bytes it takes: a character with
 * no cell is '?', and a byte that is not part of a well formed UTF-8
 * character is its own cell, so that text that is in the code page already
 * is shown as it is.
 */
int
utf8_cell(const char *s, size_t *used);


/* The number of cells that a text takes on the screen. */
int
swidth(const char *s);


/* The cells of a name, one byte for each, in out. */
void
name_to_cells(const char *name, char *out, size_t cap);


/*
 * The name of a file as it is written in a document, in the code page of the
 * document format, as the UTF-8 name of a file: the other way of
 * name_to_cells.
 */
void
name_from_cells(const char *cells, char *out, size_t cap);


/*
 * A name that fits in width cells: the end of a longer name, with the
 * extension, after three dots (...long-name.doc).  The result is UTF-8 and
 * is cut between characters.
 */
void
name_tail(const char *name, int width, char *out, size_t cap);


/*
 * The fields of a panel that hold the name of a file hold it as UTF-8, as
 * the file system has it, and are edited one character at a time: name_prev
 * and name_next give the start of the character before pos and the end of the
 * one at pos, key_utf8 writes the bytes of a key (a byte of the code page of
 * the document format, as the keyboard gives it) and returns how many, and
 * field_cells makes the cells of the field and the cell of the place pos.
 */
int
name_prev(const char *s, int pos);


int
name_next(const char *s, int pos);


int
key_utf8(int k, char *out);


void
field_cells(const char *val, int pos, char *cells, size_t cap, int *cellpos);
#endif
