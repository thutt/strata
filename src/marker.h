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
 * marker.h - the marker commands.
 *
 * A marker is a non-text object in the text, such as a date, a picture or a
 * note.  Its payload is a list of records.  Markers that hold text (notes,
 * comments, equations) start with that text as nested blocks.  Every other
 * record is one field of the marker's panel: the first byte is the field's
 * identifier, and the rest is its value.  Text values end with a NUL byte,
 * and a field that has its default value is left out.
 */
#ifndef MARKER_H
#define MARKER_H
#include "msdoc.h"
#include <stdint.h>

/* The number of counters of a document. */
#define MAX_COUNTERS 99

/* The largest value that one counter counts up to (an unsigned 32-bit int). */
#define COUNTER_MAX UINT32_MAX

/* How a field is stored and edited. */
typedef enum {
    MF_TEXT, /* a string */
    MF_NUM, /* a number kept as an integer scaled by a power of ten */
    MF_CHOICE /* one entry of a list, kept as its position plus a base */
} MFKind;

typedef struct {
    const char *id; /* first characters of the field's record */
    const char *label; /* the text shown beside the field on the panel */
    MFKind kind;
    int decimals; /* MF_NUM: digits after the decimal point */
    /*
     * MF_TEXT and MF_NUM: the value that is left out.  MF_CHOICE: the
     * stored value of the entry that has no record (NULL: the first one).
     */
    const char *dflt;
    const char *const *choices; /* MF_CHOICE: the list, ending with NULL */
    int base; /* MF_CHOICE: stored value of the first entry */
    int required; /* MF_TEXT: the panel rejects an empty value */
} MField;

/* Where a marker is allowed. */
typedef enum {
    MA_ANY, /* anywhere */
    MA_STRUCTURED, /* only in a structured document */
    MA_HEADFOOT /* only in a header or footer, not in the body */
} MAvail;

typedef struct {
    int type; /* the number stored in the file */
    const char *name; /* the name on the Marker Commands menu */
    MAvail avail;
    int nested; /* 1 if the payload begins with nested text blocks */
    const MField *fields;
    size_t nfields;
} MInfo;

/* The description of a marker type, or NULL for a type that is not known. */
const MInfo *
marker_info(int type);


/* The Marker Commands menu: entry i, in the order of the menu. */
int
marker_menu_count(void);


const MInfo *
marker_menu(int i);


/* ------------------------------------------------------------- payload */
#define MP_MAXREC 16

typedef struct {
    uint8_t *nested; /* the nested text blocks, copied */
    size_t nested_len;
    int n;
    char *id[MP_MAXREC];
    char *val[MP_MAXREC];
    uint8_t nul[MP_MAXREC]; /* the record's value ended with a NUL byte */
} MPayload;


void
mp_parse(MPayload *p, const MInfo *mi, const uint8_t *data, size_t len);


/* The value of a record, or NULL when it is not there. */
const char *
mp_get(const MPayload *p, const char *id);


/* Set a record's value; NULL removes the record. */
void
mp_set(MPayload *p, const char *id, const char *val, int nul);


/* The payload bytes; the caller frees them. */
uint8_t *
mp_build(const MPayload *p, size_t *len);


void
mp_free(MPayload *p);


/*
 * Nested text.  The text of a note, comment or equation can be edited as one
 * plain line when it is a single block of ordinary characters.  Returns 1
 * and the text in buf when it is, 0 when it is not.
 */
int
mp_nested_plain(const MPayload *p, char *buf, int cap);


void
mp_set_nested_plain(MPayload *p, const char *text);


/* ---------------------------------------------------------- appearance */
/* How a marker is drawn in the text. */
typedef enum {
    MR_GLYPH, /* a single marker character */
    MR_BRACKET, /* the text in square brackets */
    MR_TEXT, /* the text as if it had been typed */
    MR_VALUE, /* the value of the marker, in the marker color, no brackets */
    MR_KEYCAP, /* a keycap: its legend with a space on both sides, in a box */
    MR_ERROR, /* a Print Error: the text in brackets, in a style that shows */
    MR_MISSING /* a picture whose file cannot be used: dim, in brackets */
} MRender;

/*
 * The PRINT ERROR marker (type 40; the format uses the types 1 to 39).  It
 * is made only by the Editor from the error file of a Preview or a Print (see
 * perr.h): it is not in the Marker Commands menu, marker_menu does not list
 * it and no key makes one.  Its records are 1, the message, 2, the code of
 * the error, and 3, the run that found it (P for Print, V for Preview).
 */
#define MT_PRINT_ERROR 40

/*
 * The LaTeX marker (type 41): its text is raw LaTeX that the Printer writes
 * as it is.  The text is kept as the nested blocks of the marker, like the
 * text of a note or an equation (see marker_latex_text).
 */
#define MT_LATEX 41

/* 1 while text is made for a file or a screen image: no Print Error shows. */
extern int marker_hide_perr;


/*
 * 0 while the screen is drawn, 1 while a draft print is made and 2 for a
 * final print and the preview.  A comment is printed in a draft print only;
 * a counter and a cross-reference are printed as their text.
 */
extern int marker_print_mode;


/*
 * The markers that depend on the rest of the document (markereval.c).
 * marker_prepare numbers the counters of a document and sets the hook that
 * marker_render uses, so it must be called before a layout.  marker_eval
 * gives the text of a counter or a cross-reference, or returns 0.  The
 * other functions make the text of a counter: marker_number_text a number
 * in a style (1 arabic, 2 lower case roman, 3 upper case roman, 4 upper
 * case letters, 5 lower case letters) and marker_counter_text a tag such
 * as "\1\." with the number in it.
 */
extern int (*marker_eval_fn)(const Tok *k, char *buf, size_t cap);

/*
 * Set by the Editor: tells whether the file of a Picture marker can be used.
 * docpath is the file of the document (NULL for none) and name the file
 * name of the marker.  Returns 0 when the file can be used, else 1 and a
 * few words in why ("file not found", "cannot be read", "not a picture").
 * The screen then shows the marker as an error (MR_MISSING).  A program that
 * has no such function (the Printer) leaves it NULL.
 */
extern int (*marker_picture_fn)(const char *docpath, const char *name,
                                char *why, size_t cap);

/*
 * Does the picture of an Equation, a Picture, a Keycap or a LaTeX marker,
 * which the display mode Inline draws as a picture, fail (LaTeX is not
 * installed, reports an error, takes more than 5 seconds, or rejects the
 * picture), or is the marker one that is never drawn as a picture (the rule
 * table of latexmark.h)?  Such a
 * marker is then drawn in its Short form, never as an empty box
 * (docs/KEYCAP-FALLBACK.txt).  A program that has no pictures leaves it
 * NULL.
 */
extern int (*marker_failed_fn)(const Tok *k);

/* Set with marker_eval_fn: a document that is freed is dropped from it. */
extern void (*marker_forget_fn)(const Doc *d);


void
marker_prepare(Doc *d);


/*
 * True if the last marker_prepare was for d and found no counter and no
 * cross-reference in it: then the text of no block depends on another
 * block, and the numbering need not be made again after an edit that adds
 * or removes none.
 */
int
marker_prepared_none(const Doc *d);


int
marker_eval(const Tok *k, char *buf, size_t cap);


void
marker_number_text(int style, uint32_t v, char *out, size_t cap);


void
marker_counter_text(const char *tag, int override, uint32_t v, char *out,
                    size_t cap);


/*
 * 1 if, in the last marker_prepare, a counter was used again after it had
 * reached COUNTER_MAX: it stays there (it does not wrap to 0).  The Printer
 * says so once in its log.
 */
int
marker_counter_limit(void);


/*
 * Reads a whole number of 0 to COUNTER_MAX (digits only, spaces and a line
 * end at the ends allowed): returns 0 and sets *out, or -1 for anything else, a sign, a
 * decimal point, an empty text or a number that is too large.
 */
int
marker_parse_u32(const char *s, uint32_t *out);


void
marker_counter_tag(const Doc *d, int n, char *out, size_t cap);


/*
 * The document that markers are drawn for.  Several markers show the
 * document's own values, such as its name or its author.
 */
void
marker_set_doc(const Doc *d);


/*
 * Name the file that a print of a copy of the document stands for.  The
 * markers Filename, Pathname, Filedate and Filetime then show the name, the
 * path and the time of the file path, not those of the document that is
 * loaded, which is a copy that the Printer is given.  An empty path says that
 * the document has no file yet: Filename and Pathname give their labels and
 * Filedate and Filetime the time now.  NULL (the default) takes the document
 * that is loaded.  The text is copied.
 */
void
marker_set_source(const char *path);


/* Forget the document when it is freed, so that no stale pointer is kept. */
void
marker_doc_freed(const Doc *d);


MRender
marker_render(const Tok *k, char *buf, int cap);


/*
 * Tell whether a block holds nothing but Keep markers (and line ends).  Such
 * a block prints nothing at all, not even an empty line.
 */
int
block_only_keep(const Block *b);


/*
 * The label of a marker as the display mode Long draws it, whatever mode
 * the document is in (the LaTeX printout uses it for the markers it cannot
 * typeset).  The label is never empty.
 */
void
marker_long_label(const Tok *k, char *buf, int cap);


/*
 * The value of a marker that stands for a text of the document or of the
 * system: Author, Description, Date, Time, Filedate, Filetime, Filename,
 * Pathname, Printer and Revision.  Returns 1 and the text in buf, or 0 for
 * any other marker.
 */
int
marker_value(const Tok *k, char *buf, int cap);


/*
 * The first block of the section that a cross-reference points at (after
 * marker_prepare), or -1 when the section is gone or k is not one.
 */
int
marker_xref_item(const Tok *k);


/* A section of a document: the first block of it and its number, 1.2.1. */
typedef struct {
    int item; /* the first block of the section */
    char num[SECT_NUMLEN]; /* its number as the status line shows it, 1.2.1 */
} MarkerSecNum;


/*
 * The numbers of all the sections of the document in one pass; the result
 * is in memory that the caller frees, and the count is returned.
 */
int
marker_section_numbers(const Doc *d, MarkerSecNum **out);


/*
 * Display modes, set on Global, Marker, Marker Display Modes.  The mode of
 * a marker type is kept in the header as OM<type>,<mode>, and left out when
 * it is the default mode of the type.  With OH1 a hidden marker is drawn in
 * its short form.
 */
enum {
    MM_DIAMOND,
    MM_NAME,
    MM_SHORT,
    MM_LONG,
    MM_HIDDEN,
    MM_INLINE /* an equation, picture or keycap: the image itself */
};


const char *
marker_mode_name(int mode);


int
marker_default_mode(int type);


int
marker_mode(const Doc *d, int type);


void
marker_set_mode(Doc *d, int type, int mode);


int
marker_show_hidden(const Doc *d);


/*
 * The text of an Equation marker: the nested text with a line feed for
 * every end of line.  Returns the length, or -1 when the marker is not an
 * equation.
 */
int
marker_equation_text(const Tok *k, char *buf, int cap);


/*
 * The text of a marker that holds text (a note, comment or equation): the
 * nested text with a line feed for every end of line.  Returns the length,
 * or -1 when the marker holds no text.
 */
int
marker_nested_text(const Tok *k, char *buf, int cap);


/*
 * The text of a LaTeX marker exactly as it was typed: every block of the
 * nested text, a line feed for every end of line and between two blocks, a
 * tab for every tab, and the code page 437 byte FF for a nonbreaking space;
 * the changes of attribute are left out.  The bytes of the text are not
 * changed in any other way.  Returns the length, or -1 when the marker is
 * not a LaTeX marker or the text does not fit in cap bytes (with the
 * ending NUL).
 */
int
marker_latex_text(const Tok *k, char *buf, int cap);


/*
 * Is a Picture marker of the position In Text, so that it is a word in the
 * line?  The unit of the lengths of a marker that has the field U (Whitespace
 * and Picture) as TeX writes it; inches when there is none.  The returned
 * string is a constant.
 */
int
marker_picture_in_text(const Tok *k);


const char *
marker_length_unit(const Tok *k);


/*
 * The number that a Note marker got in the last marker_prepare (1 and up,
 * counted for the kind of the note), or 0.
 */
int
marker_note_number(const Tok *k);


/*
 * The name of a Whitespace unit (field U of marker 31): 0 is "in" (no
 * record), then cm, mm, pt, bp, pc, dd, cc, sp, em and ex.  A number that is
 * not a unit gives "in".
 */
const char *
marker_unit_name(int u);


/* The marker's own panel fields as text, scaled numbers made readable. */
void
marker_fmt_num(int v, int decimals, int trim, char *buf, size_t cap);


/* Parse a number typed on a panel; returns 0 and the scaled value, or -1. */
int
marker_parse_num(const char *s, int decimals, int *out);

/*
 * The Keycap Format of a document (Global, Marker): the defaults of the
 * keycap markers that have no value of their own.  The foreground and the
 * background are in tenths (0.0 and 100.0), the scale in thousandths
 * (1.000) and the font is a font number, 1 to 20 (2, Text).  The
 * records are OKF, OKB, OKS and OKT, left out for the default.
 */
typedef struct {
    int fg;
    int bg;
    int scale;
    int font;
} KeycapFmt;


/* The settings of the document: its records, else the defaults. */
void
keycap_format_get(const Doc *d, KeycapFmt *f);


/* Write the settings; a value that is the default has no record. */
void
keycap_format_set(Doc *d, const KeycapFmt *f);


/*
 * The values that a Keycap marker is shown with: those of its own fields 2
 * to 5 where it has them, else those of the Keycap Format of the document
 * (d may be NULL for the defaults).  Returns 0, or -1 when k is not a
 * Keycap marker.
 */
int
keycap_values(const Doc *d, const Tok *k, KeycapFmt *out);
#endif
