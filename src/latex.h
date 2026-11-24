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
 * Write a loaded document as a LaTeX file (see docs/PRINT-LATEX.txt).
 *
 * The module only reads the document.  It is linked into strprint and into
 * the editor, which uses it for the estimate of the pages of a preview (the
 * editor does not write files through it).  The file is made for
 * pdflatex and for latex (T1 fonts, inputenc utf8); see docs/PRINT-LATEX.txt.
 */
#ifndef LATEX_H
#define LATEX_H
#include "diag.h"
#include "msdoc.h"
#include "vars.h"
#include <stdio.h>

/* The most characters that latex_write lists as missing. */
#define LATEX_MAXMISSING 256

/* The highest marker type that has a counter of long-form uses. */
#define LATEX_MAXTYPE 41

/*
 * Some pieces can be refused by TeX although they are written correctly as
 * far as the program can tell: an equation that is not TeX, a picture that
 * TeX cannot read.  Each of them is written on a line of its own that ends
 * with a comment %F<n>, where n counts those pieces from 1 in the order of
 * the document.  A piece whose number is in the list "disabled" is printed
 * in the long form instead.
 */
/* The value of an option of a part of the print that the document decides. */
#define LATEX_AUTO (-1)

typedef struct {
    int draft; /* 1: a draft print (comments are printed, one spacing). */
    int double_spaced; /* 1: the lines of the text are double spaced. */
    /*
     * 1: every colour of the print is a grey (xcolor option gray, the
     * luminance of the colour), for a printer with no colour.  0: the
     * colours of the print are colours.
     */
    /*
     * 1: make the index at the end; the list of figures; the list of
     * pictures; the list of tables.  0: do not.  LATEX_AUTO: the document
     * says (its setting of the list, else its count of the markers of the
     * kind: docs/AUTOLISTS.txt).  A caller that clears the options gets 0,
     * as before; the Printer (strprint) starts with LATEX_AUTO.
     */
    int index;
    int list_figures;
    int list_pictures;
    int list_tables;
    int black_white;
    /*
     * The deepest level of headline that the table of contents lists, or 0
     * for no table of contents.  The page after the title page (or the
     * first page) holds the table: the tag, the title and the page number
     * of each headline.  A document with no headline gets a line that says
     * so.  LATEX_AUTO: the level of the document (YCL, 99 when it has none)
     * when it is structured and has a headline, else none.
     */
    int contents;
    const char *docdir; /* Where the pictures of the document are looked for. */
    const char *workdir; /* A directory for links to pictures, or NULL. */
    /* The working directory of the panels, where a picture is looked for
       after the directory of the document (see latex_find_picture). */
    const char *picwork;
    const int *disabled; /* Numbers of the pieces to print in long form. */
    int ndisabled;
    int no_pictures; /* 1: every picture is printed in the long form. */
    /*
     * 1 for pdflatex: a PostScript picture is written as picN.pdf and
     * strprint makes that file from the link picN.eps with ghostscript,
     * because pdflatex cannot read PostScript without shell escape.
     */
    int eps_pdf;
    /*
     * 1: write a \write16 line before each block with the position in the
     * document in thousandths ("@@strprint pos 250").  TeX prints it when
     * the page that holds it is shipped, so that a program that reads the
     * output of TeX determines how far the document has come at each page.
     */
    int marks;

    /*
     * 1: write a comment line "%@P item column line" at the start of each
     * line of text of the document, so that the line of a TeX error can be
     * turned into a position of the document (diag_tex_position).
     */
    int posmarks;

    /*
     * The messages of TeX for the pieces of the list "disabled" (the same
     * order; an entry may be NULL), told in the diagnostics.
     */
    const char *const *disabled_msg;

    /*
     * When it is not NULL and holds diagnostics, the file ends with an
     * Error Log: a new page with the heading "Error Log" and one numbered
     * entry for each of them.
     */
    const DiagList *errlog;

    /*
     * When it is not NULL, this is a FAILED PRINT and the list holds its
     * diagnostics (numbered from 1 as in the Error Log).  Each of them is
     * marked in the pages with a small red label that carries its number
     * ("E3" for an error, "W3" for a warning) at the place that the
     * diagnostic has; one without a place is shown by the banner and the
     * log only.  With banner not 0 every page also carries the banner
     * "PRINT FAILED - N ERRORS - ..." in the top margin, and a DVI says so
     * to the Viewer with the special "strata:print-failed N".
     */
    const DiagList *failed;
    int banner;
    int preview; /* 1: the file is a Preview, so the banner shows it. */

    /*
     * The variables that the If markers of the document test (see cond.h),
     * or NULL when none are set.  Only the branches that the variables
     * choose are written, and the three markers are not.
     */
    const VarList *vars;

    /*
     * 1: the text of a LaTeX marker is not written: the marker is printed
     * as the words LaTeX instructions and the writer tells so (for a print
     * of a document of a stranger).  0, the default: the text is written as
     * it is.
     */
    int no_latex;

    /*
     * When it is not NULL, latex_write fills it with the diagnostics of
     * the document (it is cleared first): an equation that is not TeX, a
     * character that no font has, a picture that cannot be used, a marker
     * that is not typeset, a marker without its partner.
     */
    DiagList *diags;

    /*
     * Export as LaTeX.  When export_name is not NULL the file is made to be
     * kept and compiled by hand: it begins with a comment that says what
     * made it, from which document, on which day (export_date, or NULL),
     * the file name that the command to compile uses (export_tex) and the
     * packages it needs, and it ends with a list of its pictures.  The
     * pictures are copied into the directory picdir (when it is not NULL)
     * and written with the relative directory picref in front of the name;
     * with copy_pictures 0 only a PostScript picture is copied (as it must
     * be made into a PDF) and the others are named where they are.
     */
    const char *export_name;
    const char *export_tex;
    const char *export_date;
    const char *picdir;
    const char *picref;
    int copy_pictures;

    /* Results of latex_write and latex_probe. */
    /* The uses of each marker type in the long form. */
    int longform[LATEX_MAXTYPE + 1];
    int npieces; /* The number of pieces that can be refused. */
    int npictures; /* How many of them are pictures. */
    int pagerefs; /* 1 if the file has page references (two runs). */
    /*
     * The characters that no font of the preamble has: they are printed as
     * [U+XXXX], each is listed here once (the Unicode value, in the order
     * of the CP437 table).
     */
    unsigned missing[LATEX_MAXMISSING];
    int nmissing;
} LatexOpts;


/*
 * Write the document as a complete LaTeX file.  The document is changed in
 * one way only: its counters and cross-references are brought up to date.
 * Returns 0, or -1 when the file cannot be written.
 */
int
latex_write(Doc *d, FILE *out, LatexOpts *o);


/*
 * Estimate how many pages the document has, before TeX has run, from the
 * page size and margins of the document, the length of its lines of text,
 * its spacing and its tables.  The result is 1 or more.  The constants of
 * the model were fitted to the page counts of the sample documents; see
 * docs/PRINT-LATEX.txt.
 */
int
latex_estimate_pages(const Doc *d);


/*
 * The same with the characters per inch and the lines per inch (both times
 * 100) given, to fit the model to measured page counts.
 */
int
latex_estimate_with(const Doc *d, int cpi_x100, int lpi_x100);


/*
 * Write a small file that holds only the equations of the document, one
 * piece per line as latex_write does, so that all that TeX rejects can be
 * found in one run.
 */
int
latex_probe(Doc *d, FILE *out, LatexOpts *o);


/*
 * Escape a text of CP437 bytes for TeX: the characters # $ % & _ { } ~ ^ \
 * get their commands, the other characters become UTF-8 (symbols that the
 * text font lacks go through the symbol font commands of the preamble).
 * Runs of spaces keep their width.  The result is always NUL terminated and
 * cut when cap is too small; the length is returned.
 */
size_t
latex_escape(const char *in, char *out, size_t cap);


/*
 * The UTF-8 form of a CP437 byte, 1 to 3 bytes and a NUL in out (room for
 * 4).  Returns the number of bytes.
 */
size_t
latex_cp437_utf8(int c, char *out);


/*
 * The size of the text in points that the Printer uses for the document d
 * (8, 9, 10, 11, 12, 14, 17 or 20).
 */
int
latex_text_points(const Doc *d);


/*
 * Write to out the head of a file that typesets a piece of text alone, with
 * the preamble of the Printer at the text size pts: everything up to and
 * including \begin{document}.  Returns 0, or -1 when the write failed.  The
 * Editor draws a LaTeX marker as a picture with it (LATEXMARKER-PICTURES.txt).
 */
int
latex_snippet_head(int pts, FILE *out);


/*
 * Is the text of an equation safe to give to TeX as math?  It must have
 * balanced braces, no comment, no macro that reads or writes files or
 * defines commands, and only the environments of a short list.
 */
int
latex_math_ok(const char *s);

/*
 * Where the file of a Picture marker is looked for, one rule for the Editor
 * and the Printer (docs/PICTURES-PATHS.txt).  A name with a full path is
 * used as it is.  Any other name is looked for in the directory of the
 * document, then in the working directory (the one the panels show; NULL or
 * empty for none), then in the directory the program was started in.  A
 * backslash in the name is a slash, a name without a directory also matches
 * any case of its letters.  Returns 1 and the full path of the file in out
 * when it is there; else 0.  tried (may be NULL) gets the places that were
 * looked in, as text, cut to tcap.
 */
int
latex_find_picture(const char *docdir, const char *workdir, const char *name,
                   char *out, size_t cap, char *tried, size_t tcap);


/*
 * The kind of a picture file from its first bytes: 1 PNG, 2 JPEG, 3 PDF,
 * 4 PostScript, or 0 when the file cannot be read or is none of them.
 */
int
latex_picture_kind(const char *path);
#endif
