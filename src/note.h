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

/*
 * note.h - the settings of the three kinds of notes (Global, Marker, Note
 * Format).  They are kept in header records, and a record is written only
 * when its value differs from the default (docs/NOTE-FORMAT.txt):
 *
 *   N<k>R  reference tag (the mark in the text), N<k>N note tag (before the
 *          note); k is 1 footnote, 2 levelnote, 3 endnote; text ending in NUL
 *   N<k>X  attributes of the reference tag, N<k>Y of the note tag
 *   N<k>SL, N<k>ST, N<k>SN  spacing of lines, blocks and notes (hundredths)
 *   NS<k>  separator bar (thousandths of an inch), k 1 or 2
 *   NZ<k>  renumber, k 1 or 2 (1 means yes)
 *   NB     position of footnotes (0 after the text, else bottom of page)
 *   NL     note level of levelnotes
 *   NE     title of the page of endnotes
 *   NT     primary note type (2 levelnotes, 3 endnotes, else footnotes)
 */
#ifndef NOTE_H
#define NOTE_H
#include "msdoc.h"

enum {
    NK_FOOT,
    NK_LEVEL,
    NK_END,
    NK_COUNT
};

/* Attribute masks, as a document stores them. */
#define NA_BOLD 1
#define NA_ITALIC 4
#define NA_SUPER 16

typedef struct {
    char reftag[48];
    int refattr;
    char notetag[48];
    int noteattr;
    int sp_line; /* hundredths */
    int sp_block;
    int sp_note;
    int sep; /* thousandths of an inch */
    int renumber;
} NoteKind;

typedef struct {
    NoteKind k[NK_COUNT];
    int bottom; /* footnotes: 1 bottom of page, 0 after the text */
    int level; /* levelnotes: the level of the section they end */
    char title[48]; /* the title of the endnote page */
    int primary; /* the kind a new note has */
} NoteFmt;


/* The settings of a document: its records, else the defaults. */
void
note_format_get(const Doc *d, NoteFmt *f);


/* Write the settings; a value that is the default has no record. */
void
note_format_set(Doc *d, const NoteFmt *f);


/*
 * The mark of the n-th note (1 and up) from a tag: \1\ (numbers), \i\ and
 * \I\ (roman), \a\ and \A\ (letters), or text that is repeated n times.
 */
void
note_tag_text(const char *tag, int n, char *out, size_t cap);


/*
 * The kind of a Note marker: its type field (1 footnote, 2 levelnote, 3
 * endnote), or primary, the Primary Note Type of the document, when the
 * marker has no type field (the panel shows that as [Footnote] and so on).
 * note_text gives the plain text of the note, with line feeds between
 * its lines.
 */
int
note_kind(const Tok *k, int primary);


int
note_text(const Tok *k, char *buf, int cap);
#endif
