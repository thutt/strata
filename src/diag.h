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
 * diag.h - the diagnostics of a print or a preview: the errors and warnings
 * that the LaTeX writer and LaTeX itself find in a document, each with a
 * position that the owner can use to find the place in the Editor.  The
 * module is shared by strprint (which collects, prints and sends them), the
 * Editor (which shows the list and jumps to a position) and the tests.  See
 * docs/PRINT-LATEX.txt, section 17.
 */
#ifndef DIAG_H
#define DIAG_H
#include "msdoc.h"
#include <stddef.h>

#define DIAG_ERROR 1
#define DIAG_WARNING 2

/* The most diagnostics that a run keeps; the rest are only counted. */
#define DIAG_MAX 200

#define DIAG_CODE 28
#define DIAG_MSG 240
#define DIAG_TEXT 72 /* The text that is wrong, cut to this. */
#define DIAG_TITLE 48 /* The headline of the section, cut to this. */

typedef struct {
    int kind; /* DIAG_ERROR or DIAG_WARNING. */
    char code[DIAG_CODE]; /* A short code such as "equation-invalid". */
    char msg[DIAG_MSG]; /* The message, a plain English sentence. */
    char text[DIAG_TEXT]; /* The text that is wrong, or empty. */

    /* The position.  item is the truth; the rest is made from it. */
    int item; /* The item (block) in the document, or -1 for none. */
    int col; /* The column of a table row (1 is the first), or 0. */
    int line; /* The line of the text of the block (1 is the first), or 0. */
    int mtype; /* The type of the marker, or 0 when it is not a marker. */
    int texline; /* The line of the .tex file for an error of TeX, or 0. */
    int off; /* The offset of the token in its line (0 is the first), or -1. */

    /* Made by diag_locate from item. */
    int sect_ord; /* The ordinal of the section (1 is the first), or 0. */
    int sect_level; /* The level of the section: 0 is the title page. */
    char sect_tag[SECT_NUMLEN]; /* The number such as "3.1.1", or empty. */
    char sect_title[DIAG_TITLE]; /* The text of the headline. */
    int block; /* The block in its section (1 is the headline), or 0. */

    /* Set by the Editor when the document changed after the run. */
    int moved;
    int seq; /* The order in which the diagnostics were found. */
} Diag;

typedef struct {
    Diag *v;
    int n;
    int cap;
    int dropped; /* The diagnostics that did not fit. */
} DiagList;


void
diag_list_init(DiagList *l);


void
diag_list_free(DiagList *l);


/* Forget the diagnostics, keep the room. */
void
diag_list_clear(DiagList *l);


/* Make the list hold n diagnostics (those over DIAG_MAX are dropped). */
void
diag_list_set(DiagList *l, const Diag *v, int n, int dropped);


/* A copy of a list (dst must be empty or initialized). */
void
diag_list_copy(DiagList *dst, const DiagList *src);


/*
 * Add a diagnostic with the message made by printf.  Returns the new entry
 * (to fill in the position), or NULL when the list is full: then only the
 * count of the diagnostics that were dropped grows.
 */
Diag *
diag_add(DiagList *l, int kind, const char *code, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));


/* Add a copy of a diagnostic; returns 0, or -1 when the list is full. */
int
diag_push(DiagList *l, const Diag *g);


/* The number of errors and of warnings in the list. */
void
diag_counts(const DiagList *l, int *errors, int *warnings);


/*
 * Put the diagnostics in the order of the document (the items, then the
 * columns and lines); those without a position come last.  The order in
 * which they were found decides between equal positions.
 */
void
diag_sort(DiagList *l);


/*
 * Fill in the section and the block of a diagnostic from its item, for the
 * document that the item belongs to.  An item that is not a block of the
 * document leaves the position unknown (sect_ord and block 0).
 */
void
diag_locate(const Doc *d, Diag *g);


/*
 * The position in words, such as "Section 3.1.1 Definition, block 2, line
 * 4, Equation marker".  The result is "no position" when none is known.
 */
void
diag_position(const Diag *g, char *out, size_t cap);


/*
 * One line of text for a diagnostic, for a terminal: the kind, the
 * position, the message, the code and the text that is wrong.  The bytes
 * above 127 are shown as '?', so that the line is plain ASCII.
 */
void
diag_describe(const Diag *g, char *out, size_t cap);


/*
 * The text of the marker as the owner knows it, cut to DIAG_TEXT: the text
 * of an equation, the name of a picture file, else the long label.
 */
void
diag_marker_text(const Tok *k, char *out, size_t cap);


/*
 * The wire form of a diagnostic, for the line "@@strprint diag ..." of the
 * progress channel: words key=value separated by one space, every byte
 * that is not a letter, a digit or one of ".,:/-" written as %XX.  Returns
 * the length, or -1 when cap is too small.  The text "diag " is not
 * included.
 */
int
diag_encode(const Diag *g, char *out, size_t cap);


/* Read the wire form (the text after "diag "); returns 0 or -1. */
int
diag_decode(Diag *g, const char *s);


/*
 * Make the items of the diagnostics those of another document.  part is a
 * part of real (its blocks are the blocks of real, see fileops.h); each
 * diagnostic whose item is in part gets the item of the same block of
 * real, and the others get -1.  The section and block are made again for
 * real.
 */
void
diag_remap(DiagList *l, const Doc *part, const Doc *real);


/* The same for an array of n diagnostics. */
void
diag_remap_array(Diag *v, int n, const Doc *part, const Doc *real);


/*
 * Find the place of a diagnostic in a document that may have been edited
 * since the run: the section by its ordinal and headline, the block by its
 * ordinal, the line, the marker by its type and text.  Returns 1 and the
 * item, the column (0 is the first) and the token when the exact place
 * exists; else sets g->moved, returns 0 and gives the nearest place (the
 * block, the section, or the start of the document).  The diagnostic is
 * not changed except for moved and the section of the new place.
 */
int
diag_refind(const Doc *d, Diag *g, int *item, int *col, int *tok);


/* The line of a diagnostic that has no item: its position is unknown. */
#define DIAG_NOITEM (-1)

/*
 * An error of TeX from a log: the message of a line that begins with "! ",
 * the line number of the .tex file and the text of the line (as TeX shows
 * it).
 */
typedef struct {
    char msg[DIAG_MSG];
    char ctx[DIAG_TEXT];
    int line; /* 0 when the log gives none. */
} TexErr;


/*
 * Every error of a TeX log, in order, up to max; returns how many.  The
 * lines "! Emergency stop." and the like, which say nothing of their own,
 * are left out.
 */
int
diag_tex_errors(const char *log, TexErr *v, int max);


/*
 * The position in the document of a line of the .tex file that the writer
 * made with position marks (LatexOpts.posmarks): the last mark at or before
 * the line.  Returns 1 and the item, column and line, or 0 when no mark
 * comes before.
 */
int
diag_tex_position(const char *tex, int texline, int *item, int *col,
                  int *line);


/*
 * THE ERROR FILE.  The Printer writes the diagnostics of a run into a file
 * that the Editor names (strprint --errors FILE).  The format is text, one
 * record per line, ASCII only:
 *
 *   STRATA-ERRORS 1
 *   diag k=E i=12 o=0 l=2 t=0 x=0 p=5 c=code m=message w=text
 *   diag ...
 *   end errors=N warnings=M dropped=K status=S
 *
 * The values are written with every byte that is not a letter, a digit or
 * one of ".,:/-" as %XX (see diag_encode).  The keys are k the kind (E or
 * W), i the item of the part that was printed, o the column of a table row,
 * l the line of the block, t the marker type, x the line of the .tex file,
 * p the offset of the token in its line, c the code, m the message and w
 * the text that is wrong.  The last line ends the file: a file without it
 * was not completed.  status is the exit status of the Printer.
 */
#define DIAG_FILE_MAGIC "STRATA-ERRORS 1"

/* Write the file; returns 0, or -1 when it cannot be written. */
int
diag_file_write(const char *path, const DiagList *l, int status);


/*
 * Read the file into l (which is initialized).  Returns 0 and sets
 * *complete to 1 when the last line was found, 0 when it was not; returns
 * -1 when the file cannot be read or is not an error file.  *status is the
 * status of the Printer when the file is complete, else -1.
 */
int
diag_file_read(const char *path, DiagList *l, int *complete, int *status);
#endif
