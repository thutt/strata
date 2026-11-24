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
 * fileops.h - what the File menu does to a document: retrieve a file into
 * the current one in its different ways, save a part of a document, start a
 * new document from a template, and import and export ASCII text.  Nothing
 * here asks the user anything.
 */
#ifndef FILEOPS_H
#define FILEOPS_H
#include "editor.h"
#include "msdoc.h"

/* A deep copy of a document, with no path. */
Doc *
doc_clone(const Doc *d);


/* --------------------------------------------------------- parts of text */
/*
 * These make a document that holds a part of another one, for printing and
 * previewing.  The items are shared with the source, so the part must not
 * outlive it and is released with doc_part_free, which does not free them.
 */

/* The first block of the section whose number is num, or -1. */
int
doc_find_section(const Doc *d, const char *num);


/*
 * The sections from the one numbered from to the one numbered to, with all
 * that is below them.  An empty from means the first section and an empty
 * to means the last.  Returns 0, or -1 with a message in err.
 */
int
doc_part_sections(Doc *d, const char *from, const char *to, Doc *out,
                  char *err, size_t errlen);


/* The section the cursor is in, or the block in an unstructured document. */
int
doc_part_current(Doc *d, int cur_item, Doc *out);


/* Only the block at cur_item. */
int
doc_part_block(Doc *d, int cur_item, Doc *out);


/* The whole document without the title page, which is above level 1. */
int
doc_part_untitled(Doc *d, Doc *out);


void
doc_part_free(Doc *p);


/* --------------------------------------------------------- the title page */
/*
 * The title page as the records of a section, for the title page editor:
 * "S" and then its blocks.  NULL, with no length, when there is none.  The
 * caller frees the bytes.
 */
uint8_t *
doc_title_bytes(Doc *d, size_t *len);


/*
 * Replace the title page by the section in bytes, which the title page
 * editor made.  A title page that is only one empty block is no title page:
 * it is removed.  Returns 0, or -1 with a message in err.
 */
int
doc_set_title(Doc *d, const uint8_t *bytes, size_t len, char *err,
              size_t errlen);


/* ------------------------------------------------------------- retrieve */
/* What Retrieve takes from the file: the choices of the Retrieve panel. */
typedef enum {
    RET_ENTIRE, /* the text and the settings; replaces the document */
    RET_BODY, /* the text; the document keeps its settings */
    RET_GLOBAL, /* the global settings; the document keeps its text */
    RET_TYPEFACE, /* the typeface settings only */
    RET_NAMED, /* the named block settings only */
    RET_INSERT /* the text, after the current section (or block) */
} RetrieveMode;


/*
 * Apply src to dst as mode says.  cur_item is the item the cursor is in
 * (for RET_INSERT).  RET_ENTIRE is not done here, since it replaces the
 * whole document.  Returns 0, or -1 with a message in err.
 */
int
doc_retrieve_into(Doc *dst, const Doc *src, RetrieveMode mode, int cur_item,
                  char *err, size_t errlen);


/* --------------------------------------------------------------- save */
typedef enum {
    SAVE_ENTIRE, /* the text and the settings */
    SAVE_GLOBAL, /* the global settings only: a template file */
    SAVE_EXTRACT /* the current section, or block, with the settings */
} SaveMode;


/* Write a document, or part of it, to path.  Returns 0 or -1. */
int
doc_save_mode(Doc *d, const char *path, SaveMode mode, int cur_item, char *err,
              size_t errlen);


/* --------------------------------------------------------- ASCII text */
/* The character sets of the Import and Export panels. */
typedef enum {
    CS_ASCII7, /* 96 character ASCII (7 bit) */
    CS_437, /* IBM extended ASCII (CP 437), the default */
    CS_850, /* IBM multilingual (CP 850) */
    CS_860, /* Portuguese (CP 860) */
    CS_865, /* Norwegian and Danish (CP 865) */
    CS_UTF8, /* UTF-8 */
    CS_LATIN1 /* ISO 8859-1 (Latin-1) */
} Charset;


/*
 * A byte of a file in a one byte character set as a byte of CP 437, and
 * back.  A character that the target lacks becomes a question mark.  For
 * CS_UTF8 a byte is not a character: these functions treat the bytes above
 * 7F as missing, and the text code uses charset_put and the importer.
 */
uint8_t
charset_to_437(Charset cs, uint8_t b);


uint8_t
charset_from_437(Charset cs, uint8_t b);


/* Write one byte of CP 437 text to a file in a character set. */
void
charset_put(FILE *f, Charset cs, uint8_t b);


/*
 * Decode one UTF-8 character at s (left bytes remain).  Returns its length
 * and sets *cp, or returns 0 when the bytes are not well formed UTF-8 (a
 * lone continuation byte, a cut or overlong sequence, a surrogate, a value
 * above 10FFFF).
 */
size_t
utf8_decode_strict(const uint8_t *s, size_t left, unsigned *cp);


/* The byte of CP 437 that shows a code point (20 to FF), or -1. */
int
cp437_of_unicode(unsigned cp);


/*
 * Insert ASCII text at the cursor.  With as_document, a blank line starts a
 * new block and a single line end is a space; otherwise every line end is
 * kept.  The cursor ends up at the start of the new text.  For UTF-8 and
 * Latin-1 a character with no place in CP 437 is inserted as a question
 * mark; the number of such characters is returned (0 for the other sets).
 * A UTF-8 byte order mark at the start is skipped, and a byte that is not
 * well formed UTF-8 is taken as a Latin-1 character.  When the document
 * would get more blocks than a document may have (DOC_MAX_ITEMS) the import
 * stops there and -1 is returned.
 */
int
ed_import_ascii(Editor *e, const uint8_t *data, size_t len, int as_document,
                Charset cs);


/*
 * Write a document as ASCII text with CR LF line ends.  A screen image is
 * the text as it is laid out on the screen; otherwise it is only the text,
 * a block on a line, with tabs, and with each column on a line of its own.
 * With cur_item set to an item of a structured document only that section
 * is written; -1 writes it all.
 */
int
doc_export_ascii(Doc *d, const char *path, int screen_image, Charset cs,
                 int cur_item, char *err, size_t errlen);
#endif
