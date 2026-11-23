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
 * msdoc.h - in-memory model of a .DOC file (docs/DOC-FORMAT.txt).
 *
 * The model is lossless: every byte of a file that is not understood is kept
 * verbatim, so load -> save reproduces the input exactly (see tests/).
 *
 * File layout (see docs/DOC-FORMAT.md):
 *   header records ... "*B<n>" body records ... "*E" tail
 * Body grammar:
 *   S            begin a section (first block of a section is its headline)
 *   +  /  -      descend into / ascend from a nested level of sections
 *   B? props     block-level format records (BF, BL, BS, BT, BB, BM, ...)
 *   C? props     column-level format records (CW, CF, CL, CR, CB, CT, ...)
 *   T<text>\0    text of one column.  A column whose records include CW is
 *                followed by another column of the same block.
 */
#ifndef MSDOC_H
#define MSDOC_H
#include <stddef.h>
#include <stdint.h>

/*
 * The deepest level of sections that a number or a tag is made for, and the
 * room for the number of a section such as 1.2.1.  Every place that counts
 * sections uses these two, so that the status line, Go To, the cross
 * references, the Error Log and the Printer give the same number to the same
 * section.  The format numbers 99 levels (its Auto Numbering to Level is
 * 99 and a section of level 100 has no number); sections may nest deeper than
 * that without any limit of the format (a document of 3000
 * levels).  The room is that of 99 numbers of 7 digits and their points.
 */
#define SECT_MAXDEPTH 99
#define SECT_NUMLEN 1024

/* ---- text tokens ------------------------------------------------------- */
enum {
    TK_CHAR, /* a = byte (CP437); esc=1 if stored as 01 <byte>            */
    TK_TAB, /* a = fill char, b = stored extra width (pad - 1)           */
    TK_ATTR, /* a = attribute mask, b = flags (00 or 80)                  */
    TK_MARKER, /* mtype = marker type, mdata/mlen = raw payload             */
    TK_NL, /* end-of-line (stored as 1D)                                */
    TK_CTRL /* a = opaque single-byte code (1C, 1A, 1E, ...)             */
};

/*
 * A token takes 4 bytes, because a large document has millions of them.  A
 * marker token keeps its type and its payload in a table of the program, and
 * the three bytes esc, a and b together are the number of its entry in that
 * table.  Use the tok_m* functions to read a marker and tok_marker to make
 * one; never look at the three bytes.  A token that is copied as a value
 * shares the entry (tok_dup in editor.c makes an entry of its own);
 * tok_release gives an entry back when the token is dropped for good.
 */
typedef struct {
    uint8_t kind;
    uint8_t esc;
    uint8_t a;
    uint8_t b;
} Tok;

typedef struct {
    Tok *v;
    int n;
    int cap;
} Text;

/*
 * The attribute table of the token 0x12 m f (docs/COLOUR-STUDY.txt, probed in
 * sample documents): m is the mask, f the flags.
 */
#define ATTR_BOLD 0x01
#define ATTR_UNDERW 0x02 /* Underscore Words (Ctrl-U) */
#define ATTR_ITALIC 0x04
#define ATTR_SUB 0x08 /* Subscript */
#define ATTR_SUPER 0x10 /* Superscript: the note references use it */
#define ATTR_STRIKE 0x20 /* Strikethrough */
#define ATTR_PD1 0x40 /* Colour 1 (Printer Specific 1) */
#define FLAG_PD2 0x01 /* Colour 2 (Printer Specific 2) */
#define FLAG_UNDER_ALL 0x02 /* Underscore All */
#define FLAG_DUNDER_W 0x04 /* Double Underscore Words */
#define FLAG_SMALLCAPS 0x08 /* Small Caps */
#define FLAG_DUNDER_ALL 0x10 /* Double Underscore All */
#define FLAG_STATE 0x80 /* a state bit of the format: not an attribute */

/*
 * The kinds of marker that have a list in the printed document.  A Figure
 * marker (type 11) makes an entry of the list of figures, a Picture marker
 * (25) one of the list of pictures, a Table marker (29) one of the list of
 * tables and an Index marker (3) one of the index.
 */
enum {
    LK_FIGURE,
    LK_PICTURE,
    LK_TABLE,
    LK_INDEX,
    LK_COUNT
};

/* A count is a number of at most DOC_MAX_DIGITS digits, as a record holds it. */
#define LIST_COUNT_MAX 9999999u

/* ---- structure --------------------------------------------------------- */
typedef struct {
    uint8_t *s;
    int n;
} Str; /* length-aware record text */
typedef struct {
    Str *v;
    int n;
    int cap;
} Lines; /* raw property records */

typedef struct {
    Lines props; /* C? records preceding this column's T             */
    Text text;
    int width; /* CW value, or 0 when absent (last column)          */
} Column;

typedef struct {
    Lines props; /* B? records preceding the block's first column     */
    Column *cols;
    int ncols;
    int capcols;
    int dirty; /* text was edited: recompute tab widths on save     */
    unsigned sid; /* what a cross-reference holds on to, or 0        */
} Block;

enum {
    IT_SECT,
    IT_PLUS,
    IT_MINUS,
    IT_BLOCK
};
/* fold (sections): 0 all, 1 headline+subsections only, 2 collapsed */
typedef struct {
    int type;
    Block *blk;
    uint8_t fold;
    uint8_t se; /* IT_SECT written as SE<fold>, which carries the fold state */
} Item;

typedef struct {
    Lines header; /* raw records before "*B"                        */
    int structured; /* the number after "*B"                          */
    Item *items;
    int nitems;
    int capitems;
    uint8_t *tail;
    size_t tail_len; /* everything from "*E" on          */
    char *path;
    int modified;
    unsigned gen; /* counts the changes made to the document */
    unsigned next_sid; /* the last Block.sid that was handed out      */
    /*
     * Where the Editor left the cursor when the document was saved: the
     * item, the column, the token and the screen row of the cursor.  It is
     * written as the record ZP before *B and read back by the loader; it
     * is not part of the settings in header, and setting it does not mark
     * the document as changed.  vp_set is 0 when there is none.
     */
    int vp_set;
    int vp_item;
    int vp_col;
    int vp_tok;
    int vp_row;
    /*
     * 2 when a part of a document made for Print or Preview follows a
     * landscape orientation marker that is not in the part.  It is written
     * as the record PO before *B; a whole document has 0.
     */
    int part_orient;
    /*
     * The number of markers of each kind that the lists are made from
     * (docs/AUTOLISTS.txt).  The counts follow the edits: the count of a kind
     * goes up when a marker of the kind enters a block of the document and
     * down when it leaves one, and nothing searches the document for them
     * but the loader, once, for a kind that the file has no count for.
     * mk_stale has the bit of every kind whose count is not exact: a count
     * that was at LIST_COUNT_MAX when a marker came in, and a count that was
     * 0 when a marker went out.  mk_have is the loader's: the bits of the
     * kinds that the file had a count for.  mk_loading is 1 while the loader
     * builds the document, when nothing is counted.
     */
    unsigned mk_count[LK_COUNT];
    unsigned char mk_stale;
    unsigned char mk_have;
    int mk_loading;
    /*
     * 1 when the counts are written with the document: the file had counts,
     * or an edit has changed one.  A document that was loaded with none and
     * has had no marker of the kinds edited is saved without them (its bytes
     * are those of the file), and counted again when it is loaded.
     */
    int mk_write;
    /*
     * The version of the file format that the loader read (the number in
     * the first record, *R<n>), or STRATA_FILE_FORMAT for a document that
     * was made in memory.  The writer always writes STRATA_FILE_FORMAT.
     */
    int file_version;
} Doc;


/*
 * Remember where the cursor is (item, column, token, and the screen row of
 * the cursor, counted from 0 at the top of the text).  The document is not
 * marked as changed.
 */
void
doc_set_view_pos(Doc *d, int item, int col, int tok, int row);


/* Forget the position: nothing is written for it. */
void
doc_clear_view_pos(Doc *d);


/* The remembered position; 0 when there is none. */
int
doc_get_view_pos(const Doc *d, int *item, int *col, int *tok, int *row);


/*
 * The marker counters and the settings of the lists and the contents.
 *
 * doc_marker_kind gives the kind (LK_...) of a marker type, or -1 when the
 * type has no list.  doc_count_marker changes the count of the kind of a
 * marker type by +1 or -1 (a marker came in or went out) and keeps the count
 * between 0 and LIST_COUNT_MAX: a +1 at the largest value changes nothing and
 * a -1 at 0 changes nothing, and each marks the count stale.  doc_count_text
 * does it for every marker of tokens [from, to) of a text, and doc_count_block
 * for every marker of a block: they read only what they are given.
 * doc_recount counts the whole document once (the loader, and the tests that
 * check the counts); it sets the counts exact.
 *
 * The setting of a list is its F record (FZLF, FZLP, FZLT, FZLI): 1 on, 0 off,
 * and none at all is automatic (-1 here).  A list is printed when its setting
 * is on, or when it is automatic and its count is not 0.  The level of the
 * table of contents is the record YCL: 0 for none, 1 to 99, 99 when there is
 * no record.
 */
int
doc_marker_kind(int mtype);


void
doc_count_marker(Doc *d, int mtype, int sign);


void
doc_count_text(Doc *d, const Text *t, int from, int to, int sign);


void
doc_count_block(Doc *d, const Block *b, int sign);


void
doc_recount(Doc *d);


/*
 * A document that is a copy of the struct of another one, with items that are
 * the other's (a part for Print, Save Section or Preview) has no counts of
 * its own: this forgets the ones that were copied, so that a part is written
 * with none and counted when it is loaded.
 */
void
doc_count_forget(Doc *d);


unsigned
doc_marker_count(const Doc *d, int kind);


int
doc_marker_stale(const Doc *d, int kind);


/* Tokens that the counting functions have read, for the tests. */
extern unsigned long doc_count_tokens_read;

/* Times that the whole document was counted, for the tests. */
extern unsigned long doc_count_recounts;


int
doc_list_setting(const Doc *d, int kind);


void
doc_list_set(Doc *d, int kind, int setting);


int
doc_list_on(const Doc *d, int kind);


int
doc_contents_level(const Doc *d);


void
doc_contents_set(Doc *d, int level);


/*
 * The type size in points that an envelope needs, for a page of the width w
 * and the length h (thousandths of an inch, in either order), or 0 when the
 * page is not an envelope.  A page is an envelope when each side is within
 * 0.3 inch of an envelope of the paper list and the templates, or when it
 * has the proportions of one (the long side at least twice the short side,
 * the short side 2.5 to 5 inches, the long side at most 11.5 inches).
 */
int
doc_envelope_points(int w, int h);


/* Mark the document as changed: it has to be saved, and laid out again. */
static inline void
doc_touch(Doc *d)
{
    d->modified = 1;
    d->gen++;
}


/*
 * What the loader accepts.  A document that goes beyond one of these limits
 * is refused with a message that names it; nothing is allocated for the
 * claim of a file before it is checked.  The limits are far above any
 * document a person writes (a document of 4000 pages has about 12 MB, 160000
 * blocks and 40000 markers).
 */
#define DOC_MAX_BYTES (32u * 1024u * 1024u) /* the size of the file    */
#define DOC_MAX_ITEMS 500000 /* sections, level marks and blocks together     */
#define DOC_MAX_MARKERS 1000000 /* markers alive in the program at once      */
#define DOC_MAX_HEADER 20000 /* records of the header                        */
#define DOC_MAX_PROPS 2000 /* property records of one block or one column    */
#define DOC_MAX_COLUMNS 5000 /* columns of one block                         */
#define DOC_MAX_DIGITS 7 /* digits of a number in a property record          */
#define DOC_MAX_MTYPE_DIGITS 6 /* digits of the type number of a marker      */


/* The number of markers that are alive in the program now. */
size_t
tok_marker_live(void);


/*
 * The version of the file format (docs/DOC-FORMAT.txt, section 0): the
 * number of the first record of a file, *R<n>.  Files of version 2 have
 * *R2.  The programs read STRATA_FILE_FORMAT_MIN up to STRATA_FILE_FORMAT
 * and write only STRATA_FILE_FORMAT.  The number is incremented by one each
 * time the format changes in an incompatible way, at most once between two
 * releases.  DOC_FIRST_RECORD is the first record that is written.
 */
#include "build_type.h"
#define STRATA_FILE_FORMAT_MIN 2
#define DOC_FIRST_RECORD "*R" STRATA_FILE_FORMAT_TEXT


/*
 * Check the first record of the n bytes at b before anything else of the
 * file is read.  Returns 0 and sets *version (when it is not NULL) for a
 * file that the programs read.  Otherwise returns -1 with the message in
 * err, one of three kinds: the file is not a document of this software (no
 * first record *R<n>), the file version is below STRATA_FILE_FORMAT_MIN or
 * is damaged (an unsupported format), or the file version is above
 * STRATA_FILE_FORMAT (the software is too old to read the file).
 */
int
docver_check(const uint8_t *b, size_t n, int *version, char *err,
             size_t errlen);


/*
 * The version of the file at path, read with docver_check from the first
 * bytes: returns it, or -1 when the file does not exist, cannot be read or
 * is not a file that the programs read (err is not set).
 */
int
docver_file_version(const char *path);


/*
 * The name of the special backup of the file path for the file version
 * version: V<version>-<name>.<extension> in the directory of the file.
 * Returns 0, or -1 when the name does not fit out.
 */
int
docver_special_name(const char *path, int version, char *out, size_t cap);


/*
 * Make the special backup of the file path if the file holds an older
 * version than STRATA_FILE_FORMAT that the programs read: a copy of the
 * file next to it (docver_special_name).  A special backup that exists is
 * not written again and nothing is changed.  Returns 0 when there is
 * nothing to do or the backup exists or was made (*made is 1 when it was
 * made now, when made is not NULL), and -1 with a message in err when the
 * special backup cannot be written.
 */
int
docver_special_backup(const char *path, int *made, char *err, size_t errlen);


/* lifecycle */
Doc *
doc_load(const char *path, char *err, size_t errlen);


/* Load a document that is held in memory; it has no path. */
Doc *
doc_load_mem(const uint8_t *data, size_t len, char *err, size_t errlen);


/*
 * Recovery: the part of a damaged document that can be read.  A document that
 * the loader refuses because it is cut short or damaged after a readable
 * beginning (no *R record is the start of a file that is no document, and
 * that is refused as before) gives a document that holds every complete
 * block and section before the damage, with the end record supplied.  The
 * damage is not mended and the reader does not look for a place to go on: it
 * stops at the first record that is wrong.  A block that is cut in the
 * middle is dropped, except the columns of it that are complete (a row of a
 * table keeps its whole columns).  Every limit of the loader holds: a file
 * over a limit, a directory, a pipe and the like are refused as before.
 */
typedef struct {
    int damaged; /* 1: the file was damaged and the document is the part */
    char why[200]; /* the message that the loader gives for the file */
    int blocks; /* the blocks of the document that was made */
    int sections; /* its sections */
    size_t bytes_read; /* the bytes of the file before the damage */
    size_t file_size;
} DocRecovery;


/*
 * Load path as doc_load does; when the file is damaged and can be recovered,
 * the part that can be read is returned and *r says what happened.  The
 * document has the path doc_recovered_path gives (never the path of the
 * file, so that saving cannot overwrite the damaged file) and is marked as
 * changed.  Returns NULL, with err, when nothing can be recovered.  The file
 * is not changed.
 */
Doc *
doc_recover(const char *path, DocRecovery *r, char *err, size_t errlen);


/* The same for a document that is held in memory (no path is set). */
Doc *
doc_recover_mem(const uint8_t *data, size_t len, DocRecovery *r, char *err,
                size_t errlen);


/*
 * The name for the recovered document: NAME.recovered.doc beside the
 * source file, or NAME.recovered2.doc, NAME.recovered3.doc and so on when that
 * name is taken, in the folder of the source file when it can be written, else
 * in the working directory.  Never the name of the source file.  Returns 0, or
 * -1 when no name fits.
 */
int
doc_recovered_path(const char *orig, char *out, size_t cap);


Doc *
doc_new(int structured);


int
doc_save(Doc *d, const char *path, char *err, size_t errlen);


/*
 * Save the document with depth numbered backups of the file that it
 * replaces (see backup.h); *unchanged is set to 1 when the file already had
 * exactly this content and nothing was written, and to 2 when it was saved but
 * no backup could be made (err says why).  Returns 0 or -1.
 */
int
doc_save_depth(Doc *d, const char *path, int depth, int *unchanged, char *err,
               size_t errlen);


void
doc_free(Doc *d);


/* serialization to memory (used by save and the round-trip test) */
uint8_t *
doc_serialize(Doc *d, size_t *len);


/* How many runs over a document doc_serialize has made (one for each call). */
extern size_t doc_serialize_runs;


/* text helpers */
void
text_init(Text *t);


void
text_free(Text *t);


int
text_insert(Text *t, int pos, Tok tok);


void
text_delete(Text *t, int pos, int n);


Tok
tok_char(uint8_t c);


/* The token of a no-break space: the control character NBSP_CTRL. */
Tok
tok_nbsp(void);


/* The token of a soft hyphen: the control character SHY_CTRL. */
Tok
tok_shy(void);


/* A marker token of the given type with a copy of the payload. */
Tok
tok_marker(int type, const uint8_t *data, size_t len);


/* Replace the payload of a marker token by a copy of data. */
void
tok_marker_set(Tok *k, const uint8_t *data, size_t len);


/* The type, the payload (NUL terminated after len bytes) and its length. */
int
tok_mtype(const Tok *k);


const uint8_t *
tok_mdata(const Tok *k);


size_t
tok_mlen(const Tok *k);


/* Give back what a token owns; the token must not be used afterwards. */
void
tok_release(Tok *k);


/* Make a text just large enough for its tokens. */
void
text_trim(Text *t);


int
text_plain(const Text *t, char *buf, int cap); /* for headlines etc. */


/* structure helpers */
Block *
block_new(int ncols);


Block *
block_clone_format(const Block *src); /* same props, empty text   */


void
block_free(Block *b);


Column *
block_add_column(Block *b);


int
doc_insert_item(Doc *d, int at, int type, Block *blk);


void
doc_delete_items(Doc *d, int at, int n);


/*
 * Take the items lo to hi out of the document and put them back so that the
 * first of them is the item to of the document without them.  The blocks
 * themselves do not move, so a cross reference that holds on to a block still
 * finds it, and the marker counts do not change.
 */
void
doc_move_items(Doc *d, int lo, int hi, int to);


/* property lookup: finds a record with the given 2-letter code
 * (e.g. "BF", "CW") whose next char is `sub`, returns pointer to the value
 * text after the sub-code, or NULL.  sub==0 matches the plain record. */
const char *
props_get(const Lines *l, const char *code, char sub);


void
props_set(Lines *l, const char *code, char sub, const char *val);


/* Take away the record with the code (and no sub letter), if it is there. */
void
props_remove(Lines *l, const char *code);


void
lines_add(Lines *l, const void *s, size_t n);


void
lines_free(Lines *l);


void
block_fix_tabs(Block *b); /* layout.c: recompute tab widths */


/* NUL-terminated copy valid until next call */
const char *
doc_header_get(const Doc *d, const char *code);


/*
 * Set a text record of the header, such as MD (the description) or MA (the
 * author).  An empty text removes the record.
 */
void
doc_header_set(Doc *d, const char *code, const char *val);


/*
 * Font names: the DF<n>N<name> records.  doc_font_name copies the name of
 * font n (1 to 20) into buf, the default name when the document has none.
 * doc_set_font_name sets it, or drops the record when it is the default.
 */
void
doc_font_name(const Doc *d, int n, char *buf, size_t cap);


void
doc_set_font_name(Doc *d, int n, const char *name);


/*
 * Named block formats (FN records of the header).  doc_named_count and
 * doc_named_name list them in the order of the file, doc_named_get gives
 * the text after code (such as "BFJ" or "BSB") of a record of one, or NULL.
 * doc_named_set replaces or adds the definition called name with the given
 * records (such as "BFJ2"), in alphabetical order; doc_named_delete
 * removes one.
 */
int
doc_named_count(const Doc *d);


/*
 * Give dst every named block format of src: a format of the same name is
 * replaced and the others are added; the formats that only dst has stay.  The
 * result is the number of formats that src has.
 */
int
doc_named_copy(Doc *dst, const Doc *src);


void
doc_named_name(const Doc *d, int index, char *buf, size_t cap);


const char *
doc_named_get(const Doc *d, const char *name, const char *code);


void
doc_named_set(Doc *d, const char *name, const char *const *recs, int nrecs);


void
doc_named_delete(Doc *d, const char *name);


/*
 * Single records that the Marker Format panel sets: PD and PT (date and
 * time format, texts), NT (primary note type), OC (cross-reference format),
 * OH (display hidden markers) and OM<type>, (the display mode of a marker;
 * the code includes the comma).  doc_opt_get gives the text after the code
 * or NULL.  doc_opt_set writes the record where the format puts it, and
 * removes it when val is NULL or empty; nul is 1 for a text.
 */
const char *
doc_opt_get(const Doc *d, const char *code);


void
doc_opt_set(Doc *d, const char *code, const char *val, int nul);


/*
 * The header and the footer of the page layouts (records P<n>HB, P<n>FB with
 * their blocks; layout 0 is the Global Page Layout, 1 to 6 the Title, Table
 * of Contents, First, Even, Odd and Index layouts; footer is 0 or 1).
 *
 * doc_hf_doc returns the blocks of the record as a small document of its own
 * (for free with doc_free), or NULL when the layout has none; *own is 1 when
 * the layout has the record (it may have no block: no header).
 *
 * doc_hf_get gives the three parts of the text, left, centre and right, as
 * plain text with \page\ for the Page Number marker and \section\ for the
 * Section marker.  *plain is 0 when the text holds more than that (another
 * marker, an attribute, several lines or blocks): the parts are then a
 * summary and doc_hf_set must not be used.
 *
 * doc_hf_set writes the record and the block (empty parts are allowed: the
 * header is then empty); doc_hf_clear removes the record and its blocks (the
 * layout takes the Global one), or, with none set, leaves an empty record: no
 * header on that layout.  The text of a part is printable ASCII.
 */
typedef struct {
    char part[3][128];
    int own;
    int plain;
} HfText;

Doc *
doc_hf_doc(const Doc *d, int layout, int footer, int *own);

void
doc_hf_get(const Doc *d, int layout, int footer, HfText *out);

/*
 * The indent of the sections of a level, in ruler units: the sum, over the
 * levels above it, of the Indent to Next Level of that level (F<n>I), which is
 * the Indent per Level (FI) where the level sets none.  Level 1 and an
 * unstructured document have none.
 */
int
doc_level_indent(const Doc *d, int level);

void
doc_hf_set(Doc *d, int layout, int footer, const char *left,
           const char *centre, const char *right);

void
doc_hf_clear(Doc *d, int layout, int footer, int none);


/*
 * The settings of the Global Format panels, which are the F records of the
 * header.  level is 0 for the global settings and 1 to 5 for a level;
 * code is what follows the F (and the level), such as "HA" for the
 * headline attributes or "SH" for the headline spacing.  doc_fmt_get gives
 * the text after the code, or NULL when there is no such record.
 * doc_fmt_set writes the record in the place where the format puts it,
 * or removes it when val is NULL or empty.
 */
const char *
doc_fmt_get(const Doc *d, int level, const char *code);


void
doc_fmt_set(Doc *d, int level, const char *code, const char *val);


/*
 * The revision of the document in thousandths, such as 1007 for 1.007; a
 * new document is 1000.  Every save adds one.
 */
int
doc_revision(const Doc *d);


void
doc_set_revision(Doc *d, int rev);


/* level (nesting depth) of the section containing item `i` */
int
doc_item_level(const Doc *d, int i);


/*
 * Between doc_levels_begin and doc_levels_end the levels of all items of d
 * are kept in a table, so that doc_item_level takes no time.  The document
 * must not change in between.
 */
void
doc_levels_begin(const Doc *d);


void
doc_levels_end(void);


/* marker names, by type number (defined in marker.c) */
const char *
marker_name(int mtype);


/*
 * Scan the text of a T record starting at i.  Returns the index just past
 * the terminating NUL, or SIZE_MAX when the buffer ends first.
 */
size_t
msdoc_scan_text(const uint8_t *b, size_t n, size_t i);


/* The longest description of a document, in characters (the record MD). */
#define DOC_MAX_DESC 250


/*
 * Is text a description that a document may have: at most DOC_MAX_DESC
 * characters and no control character?  Returns 1 or 0.
 */
int
doc_desc_valid(const char *text);


/*
 * Read the description (the record MD) of the document file path without
 * loading it: only the start of the file is read.  Returns 1 and the text in
 * out when the file is a document that has a valid description, else 0 (out
 * is empty).
 */
int
doc_peek_description(const char *path, char *out, size_t cap);
#endif
