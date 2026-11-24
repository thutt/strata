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
 * cond.h - conditional text: the If, Else and Endif markers (types 33, 34
 * and 35).  An If marker holds an expression of three parts: the name of a
 * variable, an operator and a literal.  The operators are = # < > <= >=
 * (# is "not equal").  The rules are the owner's (docs/WORKLIST.txt, the
 * item "Conditional text that works"):
 *
 *   - The comparison is numeric only when both sides are integers: all
 *     digits and not more than 4294967295 (an unsigned 32 bit value; "09"
 *     is the number 9).  In every other case both sides are text, compared
 *     byte by byte in the order of the ASCII codes (case sensitive; a
 *     prefix is less than the longer text).  A variable that is not set,
 *     or is empty, is the text "undefined".
 *   - No comparison ever fails a print or a preview.  A bad expression, an
 *     Else or an Endif without its If, an If without its Endif and a nest
 *     deeper than ten levels are errors of the document (the Error Log); the
 *     branch of a bad expression is left out.
 *   - Only Print, Preview and Export as LaTeX choose a branch.  The text of
 *     the branch that is not chosen is left out of the file, together with
 *     the three markers; the screen, Search and Export as text show
 *     everything.
 */
#ifndef COND_H
#define COND_H
#include "diag.h"
#include "msdoc.h"
#include "vars.h"
#include <stddef.h>

/* The most levels of If that may be open at once. */
#define COND_DEPTH_MAX 10

/* The operators of an expression. */
enum {
    COND_EQ,
    COND_NE,
    COND_LT,
    COND_GT,
    COND_LE,
    COND_GE
};

typedef struct {
    char name[VAR_LEN_MAX + 1];
    int op;
    char lit[VAR_LEN_MAX + 1];
} CondExpr;


/*
 * Read the text of an If marker.  Returns 0, or -1 with a short code (such
 * as "if-bad-operator") and a sentence in msg that says what is wrong.
 */
int
cond_parse(const char *text, CondExpr *e, char *code, size_t codelen,
           char *msg, size_t msglen);


/* Compare two texts with an operator by the rules above; 1 or 0. */
int
cond_compare(const char *left, int op, const char *right);


/* Evaluate an expression with the variables (NULL: none are set). */
int
cond_eval(const VarList *vars, const CondExpr *e);


/*
 * The record of what a filtered copy of a document has taken out, so that a
 * position in the copy can be told as a position in the source document.
 */
typedef struct CondMap CondMap;


/*
 * Make the copy of a document with only the text of the chosen branches: the
 * If, Else and Endif markers are gone, and so are the blocks, the section
 * items and the tokens of every branch that is not chosen.  The result is
 * NULL when the document has none of the three markers (the document itself
 * is then written).  The cross-references of the copy are made to point at
 * the same sections as in the source (which must have been prepared by
 * marker_prepare).  The errors of the markers are added to diags (when it
 * is not NULL) with positions in the source.  *map is the
 * record for cond_orig_pos and is freed with cond_map_free.
 */
Doc *
cond_filter(const Doc *src, const VarList *vars, DiagList *diags,
            CondMap **map);


/*
 * The position in the source of a position (item, col, line, off) in the
 * copy; the meaning of the fields is that of Diag.
 */
void
cond_orig_pos(const CondMap *m, const Doc *copy, int item, int col, int line,
              int off, int *oitem, int *ocol, int *oline, int *ooff);


/* The source document that the copy was made from. */
const Doc *
cond_orig(const CondMap *m);


/* Free the record. */
void
cond_map_free(CondMap *m);

#endif
