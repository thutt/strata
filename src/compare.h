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
 * compare.h - the comparison of two documents (Compare Document Versions).
 *
 * The comparison is made block by block (a block is a paragraph, a headline
 * or a table), with a difference of the words inside a block that changed,
 * and with a report of the structure: sections added, removed, moved or
 * moved to another level.  A block whose text is the same but whose format,
 * markers or attributes differ is reported on its own.
 *
 * The report is a list of paragraphs.  It is shown as lines of text (the
 * words that were added are in brackets with plus signs, the words that were
 * removed are in brackets with minus signs) and it can be made into a
 * document, in which the added words are bold and the removed words are
 * underlined as well.
 */
#ifndef COMPARE_H
#define COMPARE_H
#include "msdoc.h"

/*
 * The most differences that the search for the shortest set of differences
 * follows: between blocks and between the words of a block.  When a region
 * of the blocks has more differences than the first limit, the region is
 * reported as removed and inserted as a whole, and too_large is counted.  The
 * memory that the search takes is about the square of the limit in integers
 * (18 MB for the blocks of a document and 4.5 MB for the words of one block
 * at most).
 */
#define CMP_BLOCK_DMAX 3000
#define CMP_WORD_DMAX 1500

/* A pair of words or blocks is the same block changed when this many percent
   of its words are the same. */
#define CMP_SIMILAR 40

/* The width of the lines of the report. */
#define CMP_WRAP 76

/* The marks in the text of a paragraph: the words that were added and the
   words that were removed. */
#define CMP_INS_ON '\x02'
#define CMP_INS_OFF '\x03'
#define CMP_DEL_ON '\x04'
#define CMP_DEL_OFF '\x05'

enum {
    CMP_TITLE, /* the title of the report */
    CMP_HEAD, /* a heading of a part of the report */
    CMP_ENTRY, /* one difference: what and where */
    CMP_DETAIL, /* the text of the difference, indented */
    CMP_PLAIN /* a line of the summary */
};

typedef struct {
    char *s;
    int kind;
} CmpPara;

typedef struct {
    int blocks_a;
    int blocks_b;
    int sections_a;
    int sections_b;
    int inserted; /* blocks only in the second document */
    int deleted; /* blocks only in the first document */
    int changed; /* blocks with other words */
    int moved; /* blocks that moved (the headlines are in sect_moved) */
    int format_changed; /* the text is the same, the rest is not */
    int sect_added;
    int sect_removed;
    int sect_moved;
    int sect_level; /* sections that are now on another level */
    int too_large; /* regions that were reported as replaced */
    CmpPara *para;
    int npara;
    int cappara;
    char **lines; /* the report as lines of text for a pager */
    int nlines;
} CmpReport;


/*
 * Compares the documents a and b.  name_a and name_b are the names that the
 * report shows.  Returns 0, and the report in r, which cmp_free releases.
 */
int
cmp_compare(const Doc *a, const Doc *b, const char *name_a,
            const char *name_b, CmpReport *r);


/* Whether the two documents have no difference at all. */
int
cmp_same(const CmpReport *r);


/* The report as a new unstructured document, to be saved and edited. */
Doc *
cmp_report_doc(const CmpReport *r);


void
cmp_free(CmpReport *r);

#endif
