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
 * This module writes a document as a LaTeX file for pdflatex and latex.
 *
 * The translation is described in docs/PRINT-LATEX.txt.  The document is
 * read in the order of its items.  A section is a headline with the tag
 * that Strata shows (the numbers of LaTeX are not used), a block is a group
 * of paragraphs, the blocks that have columns are tables, and a line that
 * has tab characters is set in boxes that start at the tab stops.  Every
 * marker is either typeset or printed in its long form.
 */
#include "util.h"
#include "log.h"
#include "cond.h"
#include "include.h"
#include "latex.h"
#include "logo.h"
#include "layout.h"
#include "marker.h"
#include "note.h"
#include "border.h"
#include "readfile.h"
#include "hyphlist.h"
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* A cell of the text is 1/10 inch (10 characters per inch). */
/*
 * The size of a ruler unit, in thousandths of an inch, as unit_num / unit_den:
 * the Ruler of the Global Format settings.  Relative (the default) is 1/80 of
 * the width between the margins; 10, 12 and 15 per inch are absolute.  The
 * value is set when the preamble is written (set_ruler_unit).
 */
static int unit_num = 100;
static int unit_den = 1;
static int unit_limit = 0;

/* A length in ruler units as thousandths of an inch, never wider than the text. */
static int
ru(int n)
{
    long v = (long)n * unit_num / unit_den;

    if (unit_limit > 0 && v > unit_limit) {
        v = unit_limit;
    }

    return (int)v;
}

/*
 * The padding of a column of a table.  LaTeX puts \tabcolsep of space on both
 * sides of every column, so a column of a width w takes w plus twice the
 * \tabcolsep.  The file sets \tabcolsep to TABCOLSEP_PT points (see
 * write_preamble), and COLPAD is twice that in thousandths of an inch, rounded
 * up (12 pt are 166.04 thousandths), so that the computed width of a table
 * never goes past the margin.  Both come from this one definition.
 */
#define TABCOLSEP_PT 6
#define COLPAD ((2 * TABCOLSEP_PT * 1000 * 100 + 7226) / 7227)

/* Buffers of text that grow as needed. */
typedef struct {
    char *p;
    size_t n;
    size_t cap;
} Buf;


static void
buf_putn(Buf *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap != 0 ? b->cap : 256;
        char *np;

        while (b->n + n + 1 > nc) {
            nc *= 2;
        }

        np = realloc(b->p, nc);

        if (np == NULL) {
            log_fatal(2, "latex: out of memory growing the output buffer to "
                      "%zu bytes", nc);
        }

        b->p = np;
        b->cap = nc;
    }

    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = '\0';
}


static void
buf_puts(Buf *b, const char *s)
{
    buf_putn(b, s, strlen(s));
}


static void
buf_putc(Buf *b, char c)
{
    buf_putn(b, &c, 1);
}


static void
buf_printf(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));


static void
buf_printf(Buf *b, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    va_list ap2;
    int n;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);

    if (n >= 0 && usz(n) < sizeof tmp) {
        buf_puts(b, tmp);
    } else if (n >= 0) {
        char *big = malloc(usz(n) + 1);

        if (big == NULL) {
            log_fatal(2, "latex: out of memory formatting %d bytes of text "
                      "for the output buffer", n);
        }

        vsnprintf(big, usz(n) + 1, fmt, ap2);
        buf_puts(b, big);
        free(big);
    }

    va_end(ap2);
}


static void
buf_free(Buf *b)
{
    free(b->p);
    b->p = NULL;
    b->n = 0;
    b->cap = 0;
}


/* A length in thousandths of an inch as a TeX dimension. */
static void
put_in(Buf *b, int milli)
{
    int a = milli < 0 ? -milli : milli;

    buf_printf(b, "%s%d.%03din", milli < 0 ? "-" : "", a / 1000, a % 1000);
}


/*
 * A length of a Whitespace marker: milli is in thousandths of the unit and
 * unit is the number of field U (0 inches, then the TeX units of
 * marker_unit_name).
 */
static void
put_size(Buf *b, int milli, int unit)
{
    int a = milli < 0 ? -milli : milli;

    buf_printf(b, "%s%d.%03d%s", milli < 0 ? "-" : "", a / 1000, a % 1000,
               marker_unit_name(unit));
}


/* A number of hundredths as a decimal, such as 1.25. */
static void
put_hund(Buf *b, int v)
{
    int a = v < 0 ? -v : v;

    buf_printf(b, "%s%d.%02d", v < 0 ? "-" : "", a / 100, a % 100);
}


/*
 * The code point that the Printer uses for a CP437 byte.  The byte FF is a
 * no-break space in the table; in a text it is written as the tie (see
 * emit_range), and where only a character is wanted (a heading, a name) it
 * is a plain space.
 */
static unsigned
print_cp(int c)
{
    if ((c & 0xFF) == 0xFF) {
        return 0x20;
    }

    return cp437_uni[c & 0xFF];
}


/* Characters: CP437 bytes, UTF-8 and the escaping for TeX. */
size_t
latex_cp437_utf8(int c, char *out)
{
    char u[4];
    int n = utf8_encode(print_cp(c), u);

    memcpy(out, u, usz(n));
    out[n] = '\0';

    return usz(n);
}


/* The command for a character that TeX treats in a special way, or NULL. */
static const char *
special_text(int c)
{
    switch (c) {
        case '#':
            return "\\#";

        case '$':
            return "\\$";

        case '%':
            return "\\%";

        case '&':
            return "\\&";

        case '_':
            return "\\_";

        case '{':
            return "\\{";

        case '}':
            return "\\}";

        case '~':
            return "\\textasciitilde{}";

        case '^':
            return "\\textasciicircum{}";

        case '\\':
            return "\\textbackslash{}";

        case '[':
            return "{[}";

        case ']':
            return "{]}";

        default:
            return NULL;
    }
}


/*
 * The characters of CP437 that the 8-bit fonts do not have as text, with
 * the TeX command that stands for each (declared in the preamble with
 * \DeclareUnicodeCharacter).  The Latin letters and signs of Latin-1 come
 * from inputenc and the T1 and TS1 encodings, the box drawing and block
 * characters (U+2500 to U+259F) from pmboxdraw.  A character that is in
 * none of the three is printed as a placeholder (see put_cp437).
 */
typedef struct {
    unsigned cp;
    const char *tex;
} SymDef;

static const SymDef symdefs[] = {
    {0x263A, "\\strsmile{white}{black}"},
    {0x263B, "\\strsmile{black}{white}"},
    {0x2665, "\\ensuremath{\\heartsuit}"},
    {0x2666, "\\ensuremath{\\diamondsuit}"},
    {0x2663, "\\ensuremath{\\clubsuit}"},
    {0x2660, "\\ensuremath{\\spadesuit}"},
    {0x2022, "\\textbullet{}"},
    {0x25CB, "\\ensuremath{\\circ}"},
    {0x266A, "\\textmusicalnote{}"},
    {0x25BA, "\\ensuremath{\\blacktriangleright}"},
    {0x25C4, "\\ensuremath{\\blacktriangleleft}"},
    {0x2195, "\\ensuremath{\\updownarrow}"},
    {0x203C, "!!"},
    {0x25AC, "\\rule[0.1ex]{0.8em}{0.5ex}"},
    {0x2191, "\\ensuremath{\\uparrow}"},
    {0x2193, "\\ensuremath{\\downarrow}"},
    {0x2192, "\\ensuremath{\\rightarrow}"},
    {0x2190, "\\ensuremath{\\leftarrow}"},
    {0x2194, "\\ensuremath{\\leftrightarrow}"},
    {0x25B2, "\\ensuremath{\\blacktriangle}"},
    {0x25BC, "\\ensuremath{\\blacktriangledown}"},
    {0x03B1, "\\ensuremath{\\alpha}"},
    {0x0393, "\\ensuremath{\\Gamma}"},
    {0x03C0, "\\ensuremath{\\pi}"},
    {0x03A3, "\\ensuremath{\\Sigma}"},
    {0x03C3, "\\ensuremath{\\sigma}"},
    {0x03C4, "\\ensuremath{\\tau}"},
    {0x03A6, "\\ensuremath{\\Phi}"},
    {0x0398, "\\ensuremath{\\Theta}"},
    {0x03A9, "\\ensuremath{\\Omega}"},
    {0x03B4, "\\ensuremath{\\delta}"},
    {0x03C6, "\\ensuremath{\\varphi}"},
    {0x03B5, "\\ensuremath{\\varepsilon}"},
    {0x221E, "\\ensuremath{\\infty}"},
    {0x2229, "\\ensuremath{\\cap}"},
    {0x2261, "\\ensuremath{\\equiv}"},
    {0x2265, "\\ensuremath{\\geq}"},
    {0x2264, "\\ensuremath{\\leq}"},
    {0x2248, "\\ensuremath{\\approx}"},
    {0x2219, "\\ensuremath{\\cdot}"},
    {0x221A, "\\ensuremath{\\surd}"},
    {0x207F, "\\textsuperscript{n}"},
    {0x25A0, "\\ensuremath{\\blacksquare}"},
    {0x0192, "\\textflorin{}"},
    {0, NULL}};

/* Which characters were printed as placeholders: one flag per CP437 byte. */
static unsigned char missing_seen[256];


/* The commands of symdefs as \DeclareUnicodeCharacter lines. */
static void
put_symdecls(Buf *b)
{
    for (int i = 0; symdefs[i].tex != NULL; i++) {
        buf_printf(b, "\\DeclareUnicodeCharacter{%04X}{%s}\n", symdefs[i].cp,
                   symdefs[i].tex);
    }
}


/* Is the Unicode character printed by the preamble (not as a placeholder)? */
static int
char_known(unsigned cp)
{
    if (cp < 0x100 || (cp >= 0x2500 && cp <= 0x259F)) {
        return 1;
    }

    for (int i = 0; symdefs[i].tex != NULL; i++) {
        if (symdefs[i].cp == cp) {
            return 1;
        }
    }

    return 0;
}


static void
note_missing_char(unsigned cp);


/*
 * Would TeX join the character c to the one that was written last into one
 * ligature?  The fonts of the preamble turn two hyphens into an en dash,
 * three into an em dash, two backticks and two apostrophes into double
 * quotes, two commas, less-than signs and greater-than signs into the
 * quotes of other languages, and an exclamation mark or a question mark
 * followed by a backtick into an inverted mark.  A document prints what its
 * author typed, so put_cp437 writes an empty group between such a pair.
 */
static int
makes_ligature(const Buf *b, int c)
{
    char prev;

    if (b->p == NULL || b->n == 0) {
        return 0;
    }

    prev = b->p[b->n - 1];

    switch (c) {
        case '-':
            return prev == '-';

        case '`':
            return prev == '`' || prev == '!' || prev == '?';

        case '\'':
            return prev == '\'';

        case ',':
            return prev == ',';

        case '<':
            return prev == '<';

        case '>':
            return prev == '>';

        default:
            return 0;
    }
}


/*
 * One CP437 byte.  A character that no font of the preamble has is written
 * as \strmiss{XXXX}, which prints [U+XXXX] in a small typewriter font, and
 * is noted for latex_write to report.
 */
static void
put_cp437(Buf *b, int c)
{
    const char *sp = special_text(c);
    unsigned cp = print_cp(c);
    char u[4];

    if (sp != NULL) {
        buf_puts(b, sp);

        return;
    }

    if (c >= 0x20 && c < 0x7F) {
        if (makes_ligature(b, c) != 0) {
            buf_puts(b, "{}");
        }

        buf_putc(b, chr(c));

        return;
    }

    if (char_known(cp) == 0) {
        missing_seen[c & 0xFF] = 1;
        note_missing_char(cp);
        buf_printf(b, "\\strmiss{%04X}", cp);

        return;
    }

    latex_cp437_utf8(c, u);
    buf_puts(b, u);
}


/*
 * A run of n spaces.  At the beginning of a line (bol is 1) TeX would drop
 * them, so they are written as a space of the right width.
 */
static void
put_spaces(Buf *b, size_t n, int bol)
{
    if (bol != 0) {
        buf_printf(b, "\\hspace*{%zu\\strsp}", n);
    } else {
        buf_putc(b, ' ');

        for (size_t k = 1; k < n; k++) {
            buf_puts(b, "\\ ");
        }
    }
}


/*
 * The legend of a keycap.  A legend is printed as it was typed: the pairs
 * that put_cp437 guards (two hyphens, two backticks and so on) are written
 * with an empty group between them, and so is an f that is followed by an
 * f, an i or an l, which TeX would join into the ligature ff, fi, fl, ffi
 * or ffl.  A legend is set in a box, so it is never hyphenated or broken.
 */
static void
put_legend(Buf *b, const char *s, size_t n)
{
    size_t i = 0;

    while (i < n) {
        if (s[i] == ' ') {
            size_t j = i;

            while (j < n && s[j] == ' ') {
                j++;
            }

            put_spaces(b, j - i, 0);
            i = j;
        } else {
            if (i > 0 && s[i - 1] == 'f' &&
                (s[i] == 'f' || s[i] == 'i' || s[i] == 'l')) {
                buf_puts(b, "{}");
            }

            put_cp437(b, u8(s[i]));
            i++;
        }
    }
}


/*
 * A run of bytes.  bol is 1 at the beginning of a line.  Returns the new
 * value of bol: 0 when something that is not a space was written.
 */
static int
put_bytes(Buf *b, const char *s, size_t n, int bol)
{
    size_t i = 0;

    while (i < n) {
        if (s[i] == ' ') {
            size_t j = i;

            while (j < n && s[j] == ' ') {
                j++;
            }

            put_spaces(b, j - i, bol);
            i = j;
        } else {
            put_cp437(b, u8(s[i]));
            bol = 0;
            i++;
        }
    }

    return bol;
}


size_t
latex_escape(const char *in, char *out, size_t cap)
{
    Buf b = {NULL, 0, 0};
    size_t n;

    put_bytes(&b, in, strlen(in), 0);
    n = b.n;

    if (cap == 0) {
        buf_free(&b);

        return 0;
    }

    if (n >= cap) {
        n = cap - 1;
    }

    if (b.p != NULL) {
        memcpy(out, b.p, n);
    }

    out[n] = '\0';
    buf_free(&b);

    return n;
}


/* Checks of the text of an equation. */
/* The commands that an equation must not use: they read, write or define. */
static const char *const bad_macros[] = {
    "input", "include", "read", "readline", "write", "newwrite", "newread",
    "openin", "openout", "closein", "closeout", "immediate", "catcode", "def",
    "gdef", "edef", "xdef", "let", "futurelet", "csname", "expandafter",
    "noexpand", "newcommand", "renewcommand", "providecommand",
    "newenvironment",
    "renewenvironment", "DeclareRobustCommand", "DeclareMathOperator",
    "usepackage", "RequirePackage", "documentclass", "verb", "special",
    "shipout", "output", "InputIfFileExists", "IfFileExists", "url", "href",
    "includegraphics", "scantokens", "detokenize", "endinput", "batchmode",
    "scrollmode", "errorstopmode", "nonstopmode", "lccode", "uccode", "sfcode",
    "mathcode", "delcode", "dump", "makeatletter", "makeatother", "message",
    "errmessage", "typeout", "fontname", "font", "jobname", "meaning",
    "string", "lowercase", "uppercase", "romannumeral", "number", "the",
    "global", "long", "outer", "protected", "afterassignment", "aftergroup",
    "everypar", "everymath", "everydisplay", "AtBeginDocument",
    "AtEndDocument", "AtBeginShipout", "footnote", "label", "ref", "pageref",
    "directlua", "ExplSyntaxOn", "ExplSyntaxOff", "par", "relax", "endcsname",
    "begingroup", "endgroup", "bgroup", "egroup", "open", "close", "loop",
    "repeat", "if", "else", "fi", "ifx", "ifcase", "ifnum", "ifdim", "ifodd",
    "or", "newif", "setbox", "box", "copy", "hbox", "vbox", "vtop",
    "mathchardef",
    "chardef", "countdef", "dimendef", "toksdef", "skipdef", "muskipdef",
    "newcount", "newdimen", "newskip", "newtoks", "setcounter", "addtocounter",
    "stepcounter", "refstepcounter", "pagestyle", "thispagestyle", "newpage",
    "clearpage", "cleardoublepage", "vspace", "hspace", "includeonly",
    "setlength", "addtolength", "settowidth", "usetikzlibrary", "tikz",
    "pgfmathparse", "write18", "ProvidesPackage", "NeedsTeXFormat",
    "strlandbegin", "strlandend", "strmiss", NULL};

/* The environments that an equation may use. */
static const char *const good_envs[] = {
    "matrix", "pmatrix", "bmatrix", "Bmatrix", "vmatrix", "Vmatrix", "cases",
    "array", "aligned", "gathered", "split", "smallmatrix", "subarray", NULL};


static int
in_list(const char *const *list, const char *name, size_t n)
{
    for (int i = 0; list[i] != NULL; i++) {
        if (strlen(list[i]) == n && strncmp(list[i], name, n) == 0) {
            return 1;
        }
    }

    return 0;
}


int
latex_math_ok(const char *s)
{
    size_t len = strlen(s);
    int depth = 0;
    int ink = 0;

    if (len == 0 || len > 4000) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t ch = u8(s[i]);

        if (ch >= 0x80 || (ch < 0x20 && ch != '\n' && ch != '\t')) {
            return 0;
        }

        if (ch != ' ' && ch != '\n' && ch != '\t') {
            ink = 1;
        }

        if (ch == '%' || ch == '#' || ch == '$') {
            return 0;
        }

        if (ch == '^' && i + 1 < len && s[i + 1] == '^') {
            return 0;
        }

        if (ch == '{') {
            depth++;
        } else if (ch == '}') {
            depth--;

            if (depth < 0) {
                return 0;
            }
        } else if (ch == '\\') {
            size_t j = i + 1;
            size_t nl;

            if (j >= len) {
                return 0;
            }

            if (!is_alpha(s[j])) {
                i = j;
                continue;
            }

            while (j < len && is_alpha(s[j])) {
                j++;
            }

            nl = j - (i + 1);

            if (in_list(bad_macros, s + i + 1, nl) != 0 ||
                strncmp(s + i + 1, "pdf", 3) == 0 ||
                strncmp(s + i + 1, "xetex", 5) == 0 ||
                strncmp(s + i + 1, "luatex", 6) == 0) {
                return 0;
            }

            if ((nl == 5 && strncmp(s + i + 1, "begin", 5) == 0) ||
                (nl == 3 && strncmp(s + i + 1, "end", 3) == 0)) {
                size_t e;

                if (j >= len || s[j] != '{') {
                    return 0;
                }

                e = j + 1;

                while (e < len && s[e] != '}') {
                    e++;
                }

                if (e >= len || in_list(good_envs, s + j + 1, e - j - 1) == 0) {
                    return 0;
                }
            }

            i = j - 1;
        }
    }

    return ink != 0 && depth == 0;
}


/* The state of one run of the writer. */
typedef struct {
    int kind;
    int number;
    int level; /* A levelnote is printed when a section of this level ends. */
    char *text;
} PNote;

typedef struct {
    PNote *v;
    int n;
    int cap;
} NoteList;

enum {
    LM_INDEX,
    LM_FIGURE,
    LM_PICTURE,
    LM_TABLE
};


/* A marker that makes an entry of the index or of a list. */
typedef struct {
    const Tok *tok;
    int kind; /* LM_INDEX, LM_FIGURE, LM_PICTURE or LM_TABLE. */
    char main[160]; /* The main heading, or the name. */
    char sub[160]; /* The subheading (index). */
    char ref[160]; /* The reference text that replaces the page (index). */
} LMark;


typedef struct {
    Doc *d;
    LatexOpts *o;
    FILE *f;
    Buf out;
    Buf dfr; /* The notes that wait until a box or a table is closed. */
    Buf pics; /* The list of the pictures of an export, as comments. */
    NoteFmt nf;
    NoteList lnotes; /* Levelnotes that wait for the end of their section. */
    NoteList enotes; /* Endnotes, printed at the end of the document. */
    unsigned char *want_label; /* Items whose page number is referenced. */
    const char *pretag; /* Text printed at the start of the next line. */
    int piece; /* The number of the last piece that TeX could refuse. */
    int incell; /* Inside a cell of a table. */
    int inbox; /* Inside a box of a line that has tabs. */
    int hold; /* Inside a headline: notes wait until it is written. */
    int fam; /* The font family of the text that is written next. */
    int linew; /* The width of the text in thousandths of an inch. */
    int structured;
    int depth; /* The nesting depth of the item being written. */
    char pagestyle[16]; /* The page numbers of the body: arabic, roman... */
    int contents_done; /* 1: the table of contents has been written. */
    int land; /* 1 while the landscape environment is open. */
    int keep; /* Greater than 0 while a Keep part is set in a box. */

    /* Where the writer is, for the diagnostics. */
    int item; /* The item of the block that is written, or -1. */
    int col; /* The column of a table row (1 is the first), or 0. */
    int line; /* The line of the text of the block (1 is the first). */
    int lstart; /* The token index where that line starts. */
    int off; /* The offset in the line of the token that is written, or -1. */
    const Tok *mtok; /* The marker that is written, or NULL. */
    int quiet; /* 1: no diagnostics (the Error Log itself). */
    unsigned char *mdone; /* 1 for each diagnostic of "failed" marked. */
    int mc_item; /* The place of the characters in mc_seen. */
    int mc_col;
    int mc_line;
    unsigned char mc_seen[256]; /* The missing characters told here. */
    int keep_open; /* 1 between a Start Keep and its End Keep. */
    int numbered; /* 1 when the lines are numbered (lineno is loaded). */
    Diag keep_start; /* Where that Start Keep is. */
    const Doc *orig; /* The document that d was made from, or d itself. */
    const CondMap *cmap; /* What the conditional text took out, or NULL. */
    const IncludeMap *imap; /* What the parts were joined with, or NULL. */
    int hf_any; /* 1: a header or a footer of the document has text. */
    int in_hf; /* 1 while the text of a header or footer is written. */
    int hf_own[7][2]; /* 1: the layout has a header (0) or footer (1). */
    char *hf_f[7][2][3]; /* Their left, centre and right parts (LaTeX). */
    int hf_first_done; /* 1: the style of the first page is set. */
    int print_mode; /* The Print field: 0 both, 1 headlines, 2 text. */
    int skip_item; /* The text block that a joined headline has written. */
    int wind_n; /* The number of winding columns that are open (1: none). */
    int wind_gut; /* Their gutter in thousandths of an inch. */
    int wind_rule; /* and their rule: 0 none, 1 single, 2 double, 3 wide. */
    LMark *lm; /* The markers of the index and the lists. */
    int nlm;
    int caplm;
} Ctx;

static void
put_cp437_string(Buf *b, const char *s);

static int
lm_label(const Ctx *c, const Tok *k, Buf *b);

static int
lm_marker_kept(const Ctx *c, int mtype);

static int
lm_wanted(const Ctx *c, int kind);

static int
winding_used(const Ctx *c);

static int
block_boxed(const Ctx *c, int item);

static int
wide_used(const Ctx *c);

static int
lm_count(const Ctx *c, int kind);

static void
emit_list(Ctx *c, int kind);

/* The run of latex_write that is going on, for put_cp437. */
static Ctx *cur_ctx;

/* The attributes of the text that is written. */
typedef struct {
    int base; /* From the format of the block. */
    int cur; /* From the last attribute token. */
    int openm; /* The attributes of the commands that are open. */
    int nopen; /* How many commands are open. */
    int bol; /* At the beginning of a line. */
    int fam_base; /* The font family of the block: 0 text, 1 sans, 2 mono. */
    int fam_cur; /* The family of the last Font marker, or -1. */
    int curf; /* The flags of the last attribute token. */
} St;


static void
note_add(NoteList *l, int kind, int number, int level, const char *text)
{
    if (l->n == l->cap) {
        PNote *nv;

        l->cap = l->cap != 0 ? l->cap * 2 : 16;
        nv = realloc(l->v, usz(l->cap) * sizeof *nv);

        if (nv == NULL) {
            log_fatal(2, "latex: out of memory growing the list of notes to "
                      "%d entries", l->cap);
        }

        l->v = nv;
    }

    l->v[l->n].kind = kind;
    l->v[l->n].number = number;
    l->v[l->n].level = level;
    l->v[l->n].text = strdup(text);
    l->n++;
}


/*
 * The position that the writer is at, in the document that the owner has:
 * when conditional text left out parts of the document, the position in the
 * copy is turned into the position in the source.
 */
static void
orig_pos_off(const Ctx *c, int at, int *item, int *col, int *line, int *off)
{
    if (c->cmap == NULL) {
        *item = c->item;
        *col = c->col;
        *line = c->line;
        *off = at;
    } else {
        cond_orig_pos(c->cmap, c->d, c->item, c->col, c->line, at, item,
                      col, line, off);
    }

    if (c->imap != NULL) {
        include_orig_pos(c->imap, *item, *col, *line, *off, item, col, line,
                         off);
    }
}


/* The position of the token that is written, in the source. */
static void
orig_pos(const Ctx *c, int *item, int *col, int *line, int *off)
{
    orig_pos_off(c, c->off, item, col, line, off);
}


/* Fill a diagnostic with the position that the writer is at. */
static void
diag_make(Ctx *c, Diag *g, int kind, const char *code, const char *text,
          const char *msg)
{
    memset(g, 0, sizeof *g);
    g->kind = kind;
    str_copy(g->code, sizeof g->code, code);
    str_copy(g->msg, sizeof g->msg, msg);
    orig_pos(c, &g->item, &g->col, &g->line, &g->off);

    if (c->mtok != NULL) {
        g->mtype = tok_mtype(c->mtok);
        diag_marker_text(c->mtok, g->text, sizeof g->text);
    }

    if (text != NULL) {
        str_copy(g->text, sizeof g->text, text);
    }

    diag_locate(c->orig, g);
}


/* Note a diagnostic at the place of the writer (no text: the marker's). */
static void diag_here(Ctx *c, int kind, const char *code, const char *text,
                      const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));
static void
diag_here(Ctx *c, int kind, const char *code, const char *text,
          const char *fmt, ...)
{
    Diag g;
    char msg[DIAG_MSG];
    va_list ap;

    if (c->o->diags == NULL || c->quiet != 0) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    diag_make(c, &g, kind, code, text, msg);
    diag_push(c->o->diags, &g);
}


/* A character that no font has: told once for each line of text. */
static void
note_missing_char(unsigned cp)
{
    Ctx *c = cur_ctx;
    char u[16];
    int idx = -1;

    if (c == NULL || c->o->diags == NULL || c->quiet != 0 || c->item < 0) {
        return;
    }

    for (int i = 0; i < 256; i++) {
        if (cp437_uni[i] == cp) {
            idx = i;
            break;
        }
    }

    if (c->item != c->mc_item || c->col != c->mc_col ||
        c->line != c->mc_line) {
        memset(c->mc_seen, 0, sizeof c->mc_seen);
        c->mc_item = c->item;
        c->mc_col = c->col;
        c->mc_line = c->line;
    }

    if (idx >= 0) {
        if (c->mc_seen[idx] != 0) {
            return;
        }

        c->mc_seen[idx] = 1;
    }

    snprintf(u, sizeof u, "U+%04X", cp);
    diag_here(c, DIAG_WARNING, "char-missing", u,
              "No font has the character U+%04X; it is printed as [U+%04X].",
              cp, cp);
}


static int
is_disabled(const Ctx *c, int piece)
{
    for (int i = 0; i < c->o->ndisabled; i++) {
        if (c->o->disabled[i] == piece) {
            return 1;
        }
    }

    return 0;
}


/* What TeX said of a piece that was given up, or "". */
static const char *
piece_message(const Ctx *c, int piece)
{
    for (int i = 0; i < c->o->ndisabled; i++) {
        if (c->o->disabled[i] == piece && c->o->disabled_msg != NULL &&
            c->o->disabled_msg[i] != NULL) {
            return c->o->disabled_msg[i];
        }
    }

    return "";
}


/* Close the commands of the attributes that are open. */
static void
close_attr(St *st, Buf *b)
{
    while (st->nopen > 0) {
        buf_putc(b, '}');
        st->nopen--;
    }

    st->openm = 0;
}


/* The bits of a run of text, as ensure_attr sees them (see St). */
#define W_UNDER 0x10
#define W_DUNDER 0x20
#define W_SUPER 0x40
#define W_SUB 0x80
#define W_STRIKE 0x400
#define W_SMALLCAPS 0x800


/* Open the commands for the attributes that the text has now. */
static void
ensure_attr(St *st, Buf *b)
{
    int m = st->base | st->cur;
    int fl = st->curf;
    int fam = st->fam_cur >= 0 ? st->fam_cur : st->fam_base;
    int dbl = (fl & (FLAG_DUNDER_W | FLAG_DUNDER_ALL)) != 0;
    int want = (m & ATTR_BOLD) | (m & ATTR_ITALIC) |
               ((m & ATTR_UNDERW) != 0 || (fl & FLAG_UNDER_ALL) != 0 ?
                    W_UNDER : 0) |
               (dbl != 0 ? W_DUNDER : 0) |
               ((m & ATTR_SUPER) != 0 ? W_SUPER : 0) |
               ((m & ATTR_SUB) != 0 ? W_SUB : 0) |
               ((m & ATTR_STRIKE) != 0 ? W_STRIKE : 0) |
               ((fl & FLAG_SMALLCAPS) != 0 ? W_SMALLCAPS : 0) |
               (fam << 8);

    if (want == st->openm) {
        return;
    }

    close_attr(st, b);

    if ((want & 0x100) != 0) {
        buf_puts(b, "{\\sffamily ");
        st->nopen++;
    }

    if ((want & 0x200) != 0) {
        buf_puts(b, "{\\ttfamily ");
        st->nopen++;
    }

    if ((want & ATTR_BOLD) != 0) {
        buf_puts(b, "\\textbf{");
        st->nopen++;
    }

    if ((want & ATTR_ITALIC) != 0) {
        buf_puts(b, "\\textit{");
        st->nopen++;
    }

    if ((want & W_DUNDER) != 0) {
        buf_puts(b, "\\uuline{");
        st->nopen++;
    } else if ((want & W_UNDER) != 0) {
        buf_puts(b, "\\uline{");
        st->nopen++;
    }

    if ((want & W_STRIKE) != 0) {
        buf_puts(b, "\\sout{");
        st->nopen++;
    }

    if ((want & W_SUPER) != 0) {
        buf_puts(b, "\\textsuperscript{");
        st->nopen++;
    }

    if ((want & W_SUB) != 0) {
        buf_puts(b, "\\textsubscript{");
        st->nopen++;
    }

    if ((want & W_SMALLCAPS) != 0) {
        buf_puts(b, "\\textsc{");
        st->nopen++;
    }

    st->openm = want;
}


/* Markers that are printed in their long form. */
/* A marker that is not typeset: its label in the Long display mode. */
static void
put_long_only(Ctx *c, const Tok *k, St *st, Buf *b)
{
    char lb[600];

    ensure_attr(st, b);
    marker_long_label(k, lb, sizeof lb);
    buf_puts(b, strlen(lb) <= 48 ? "\\strlong{" : "\\strlongbr{");
    put_bytes(b, lb, strlen(lb), 0);
    buf_putc(b, '}');
    st->bol = 0;

    if (tok_mtype(k) >= 0 && tok_mtype(k) <= LATEX_MAXTYPE) {
        c->o->longform[tok_mtype(k)]++;
    }
}


/* A marker that is not typeset at all: told, then printed in the long form. */
static void
put_long(Ctx *c, const Tok *k, St *st, Buf *b)
{
    const MInfo *mi = marker_info(tok_mtype(k));

    diag_here(c, DIAG_WARNING, "marker-unsupported", NULL,
              "%s markers are not typeset by Strata; the marker is printed "
              "as text.", mi != NULL ? mi->name : "This kind of");
    put_long_only(c, k, st, b);
}


/* Notes: footnotes, levelnotes and endnotes. */
/* A reference or note tag with its attributes (superscript, bold, italic). */
static void
put_tag(Buf *b, int attr, const char *tag)
{
    int nb = 0;

    if ((attr & NA_SUPER) != 0) {
        buf_puts(b, "\\textsuperscript{");
        nb++;
    }

    if ((attr & NA_BOLD) != 0) {
        buf_puts(b, "\\textbf{");
        nb++;
    }

    if ((attr & NA_ITALIC) != 0) {
        buf_puts(b, "\\textit{");
        nb++;
    }

    put_bytes(b, tag, strlen(tag), 0);

    while (nb > 0) {
        buf_putc(b, '}');
        nb--;
    }
}


/* The text of a note: one line, the ends of line are spaces. */
static void
note_plain(const Tok *k, char *buf, int cap)
{
    int n = marker_nested_text(k, buf, cap);

    if (n < 0) {
        buf[0] = '\0';
        n = 0;
    }

    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            buf[i] = ' ';
        }
    }
}


/*
 * The family that a font of the Typeface list stands for.  The names of
 * the fonts are all that is known of them (docs/PRINT-LATEX.txt): a name
 * that tells of programs gets the monospaced font and a headline name the
 * sans serif font, any other name the text font.
 */
static int
font_family(const Doc *d, int n)
{
    static const char *const mono[] = {"program", "code", "courier", "mono",
                                       "typewriter", "fixed", "listing",
                                       "source", NULL};
    static const char *const sans[] = {"head", "title", "sans", "helv",
                                       "arial", "gothic", NULL};
    char name[96];

    if (n < 1 || n > 20) {
        return 0;
    }

    doc_font_name(d, n, name, sizeof name);

    for (char *p = name; *p != '\0'; p++) {
        *p = chr(to_lower(*p));
    }

    for (int i = 0; mono[i] != NULL; i++) {
        if (strstr(name, mono[i]) != NULL) {
            return 2;
        }
    }

    for (int i = 0; sans[i] != NULL; i++) {
        /* "Header/Footer" is not a headline font. */
        if (strstr(name, sans[i]) != NULL && strstr(name, "header") == NULL) {
            return 1;
        }
    }

    return 0;
}


/* The font number of a column: its own, its block's, or the global one. */
static int
font_number(const Doc *d, const Block *b, int col, int headline, int level)
{
    const char *v = NULL;

    if (col >= 0 && col < b->ncols) {
        v = props_get(&b->cols[col].props, "CF", 'F');
    }

    if (v == NULL) {
        v = props_get(&b->props, "BF", 'F');
    }

    if (v == NULL) {
        const char *nm = props_get(&b->props, "BG", 0);

        v = nm != NULL ? doc_named_get(d, nm, "BFF") : NULL;
    }

    if (v == NULL && level > 0) {
        v = doc_fmt_get(d, level, headline != 0 ? "HF" : "TF");
    }

    if (v == NULL) {
        v = doc_fmt_get(d, 0, headline != 0 ? "HF" : "TF");
    }

    return v != NULL ? atoi(v) : 0;
}


static void
emit_note(Ctx *c, const Text *t, int i, St *st, Buf *b)
{
    const Tok *k = &t->v[i];
    int kind = note_kind(k, c->nf.primary);
    int num = marker_note_number(k);
    const NoteKind *nk = &c->nf.k[kind];
    char ref[96];
    char tag[96];
    char text[8192];

    note_plain(k, text, sizeof text);
    note_tag_text(nk->reftag, num, ref, sizeof ref);
    note_tag_text(nk->notetag, num, tag, sizeof tag);
    ensure_attr(st, b);
    st->bol = 0;

    if (kind == NK_FOOT) {
        Buf nt = {NULL, 0, 0};

        buf_puts(b, "\\strfnmark{");
        put_tag(b, nk->refattr, ref);
        buf_putc(b, '}');
        buf_puts(&nt, "\\strfntext{");
        put_tag(&nt, nk->noteattr, tag);
        buf_puts(&nt, "\\ ");
        put_bytes(&nt, text, strlen(text), 0);
        /* The empty group keeps a space that begins the next text: TeX
           drops the spaces at the start of a line. */
        buf_puts(&nt, "}%\n{}");

        if (c->incell > 0 || c->inbox > 0 || c->hold > 0) {
            /* A footnote cannot be made inside a box: it waits. */
            buf_puts(&c->dfr, nt.p);
        } else {
            buf_puts(b, nt.p);
        }

        buf_free(&nt);
    } else {
        int lv = c->structured != 0 ? c->depth : 0;

        if (kind == NK_LEVEL) {
            lv = lv < c->nf.level ? lv : c->nf.level;
        }

        put_tag(b, nk->refattr, ref);
        note_add(kind == NK_END ? &c->enotes : &c->lnotes, kind, num, lv, text);
    }
}


/* Print the notes of a list that are not for later and remove them. */
static void
put_note_entries(Ctx *c, Buf *b, const NoteList *l, int kind)
{
    int first = 1;

    for (int i = 0; i < l->n; i++) {
        char tag[96];
        const PNote *pn = &l->v[i];
        const NoteKind *nk = &c->nf.k[pn->kind];

        if (pn->kind != kind) {
            continue;
        }

        if (first != 0 && kind != NK_END) {
            buf_puts(b, "\\strnotesep{");
            put_in(b, nk->sep > 0 ? nk->sep : 2000);
            buf_puts(b, "}\n");
        }

        first = 0;
        note_tag_text(nk->notetag, pn->number, tag, sizeof tag);
        buf_puts(b, "\\strnote{");
        put_tag(b, nk->noteattr, tag);
        buf_puts(b, "}{");
        put_bytes(b, pn->text, strlen(pn->text), 0);
        buf_puts(b, "}\n");
    }
}


/* A section of this level (or a deeper one) ends: print its levelnotes. */
static void
flush_levelnotes(Ctx *c, int level)
{
    NoteList keep = {NULL, 0, 0};
    NoteList out = {NULL, 0, 0};

    for (int i = 0; i < c->lnotes.n; i++) {
        PNote *pn = &c->lnotes.v[i];

        note_add(pn->level >= level ? &out : &keep, pn->kind, pn->number,
                 pn->level, pn->text);
        free(pn->text);
    }

    if (out.n > 0) {
        put_note_entries(c, &c->out, &out, NK_LEVEL);
    }

    for (int i = 0; i < out.n; i++) {
        free(out.v[i].text);
    }

    free(out.v);
    free(c->lnotes.v);
    c->lnotes = keep;
}


/* Pictures, equations and the markers that are boxes or rules. */
/* The kind of a picture file from its first bytes, or 0. */
int
latex_picture_kind(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t h[8] = {0};
    size_t n;

    if (f == NULL) {
        return 0;
    }

    n = fread(h, 1, sizeof h, f);
    fclose(f);

    if (n >= 8 && memcmp(h, "\x89PNG\r\n\x1a\n", 8) == 0) {
        return 1;
    }

    if (n >= 3 && h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF) {
        return 2;
    }

    if (n >= 4 && memcmp(h, "%PDF", 4) == 0) {
        return 3;
    }

    if (n >= 4 && (memcmp(h, "%!PS", 4) == 0 ||
                   (h[0] == 0xC5 && h[1] == 0xD0 && h[2] == 0xD3 &&
                    h[3] == 0xC6))) {
        return 4;
    }

    return 0;
}


/* The CRC of PNG chunks. */
static uint32_t
crc32_of(const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    static int ready;
    uint32_t crc = 0xFFFFFFFFu;

    if (ready == 0) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;

            for (int k = 0; k < 8; k++) {
                v = (v & 1) != 0 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            }

            table[i] = v;
        }

        ready = 1;
    }

    for (size_t i = 0; i < n; i++) {
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }

    return ~crc;
}


static uint32_t
be32(const uint8_t *p)
{
    uint32_t v = p[0];

    v = v << 8 | p[1];
    v = v << 8 | p[2];

    return v << 8 | p[3];
}


/* Tell whether the data hold the text somewhere. */
static int
has_text(const uint8_t *d, size_t n, const char *text)
{
    size_t tl = strlen(text);

    for (size_t i = 0; i + tl <= n; i++) {
        if (d[i] == u8(text[0]) && memcmp(d + i, text, tl) == 0) {
            return 1;
        }
    }

    return 0;
}


/* Tell whether the chunks of a PNG file are whole, with their checks. */
static int
png_whole(const uint8_t *d, size_t n)
{
    size_t pos = 8;
    int first = 1;

    while (pos + 12 <= n) {
        size_t len = be32(d + pos);

        if (len > n || pos + 12 + len > n) {
            return 0;
        }

        if (first != 0 && (memcmp(d + pos + 4, "IHDR", 4) != 0 || len != 13)) {
            return 0;
        }

        first = 0;

        if (crc32_of(d + pos + 4, len + 4) != be32(d + pos + 8 + len)) {
            return 0;
        }

        if (memcmp(d + pos + 4, "IEND", 4) == 0) {
            return 1;
        }

        pos += 12 + len;
    }

    return 0;
}


/* Tell whether a JPEG file has a frame header and its end marker. */
static int
jpeg_whole(const uint8_t *d, size_t n)
{
    size_t pos = 2;
    size_t end = n;

    while (end > 2 && d[end - 1] == 0) {
        end--;
    }

    if (end < 4 || d[end - 2] != 0xFF || d[end - 1] != 0xD9) {
        return 0;
    }

    while (pos + 4 <= n && d[pos] == 0xFF) {
        int m = d[pos + 1];

        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            return 1;
        }

        if (m == 0xDA) {
            return 0;
        }

        pos += 2 + usz(d[pos + 2] << 8 | d[pos + 3]);
    }

    return 0;
}


/* A picture file larger than this is not read to check it: not valid. */
#define PICTURE_FILE_MAX (256u * 1024u * 1024u)


/*
 * Can TeX read the picture?  A damaged file can make the engine stop
 * without a message, so the structure is checked first.
 */
static int
picture_valid(const char *path, int kind)
{
    size_t n = 0;
    uint8_t *d = readfile_all(path, PICTURE_FILE_MAX, &n, NULL, 0);
    int ok = 0;

    if (d == NULL) {
        return 0;
    }

    if (kind == 1) {
        ok = png_whole(d, n);
    } else if (kind == 2) {
        ok = jpeg_whole(d, n);
    } else if (kind == 3) {
        ok = n > 1024 ? has_text(d + n - 1024, 1024, "%%EOF") :
                        has_text(d, n, "%%EOF");
    } else if (kind == 4) {
        ok = has_text(d, n, "%%BoundingBox: ");
    }

    free(d);

    return ok;
}


/* Add a place that was looked in to the list of them, once. */
static void
tried_add(char *tried, size_t tcap, const char *dir)
{
    char full[PATH_MAX];
    size_t n;

    if (tried == NULL || tcap == 0 || dir == NULL || dir[0] == '\0') {
        return;
    }

    /* A place is told by its full path when it can be made. */
    if (realpath(dir, full) == NULL) {
        str_copy(full, sizeof full, dir);
    }

    /* A place that is already in the list is not told a second time. */
    n = strlen(full);

    for (const char *q = tried; (q = strstr(q, full)) != NULL; q++) {
        if ((q == tried || q[-1] == ' ') &&
            (q[n] == '\0' || q[n] == ',')) {
            return;
        }
    }

    n = strlen(tried);

    if (n > 0) {
        snprintf(tried + n, tcap - n, ", ");
        n = strlen(tried);
    }

    snprintf(tried + n, tcap - n, "%s", full);
}


/* The file is there, a regular file: its full path in out. */
static int
picture_here(const char *path, char *out, size_t cap)
{
    char real[PATH_MAX];
    struct stat st;

    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) ||
        realpath(path, real) == NULL) {
        return 0;
    }

    str_copy(out, cap, real);

    return 1;
}


/*
 * Look for the file of a picture in the places of the rule (latex.h), any
 * case of the letters for a name without a directory.  With dos set a
 * backslash is a separator, as it is in the names of the old program.
 */
static int
find_picture_as(const char *docdir, const char *workdir,
                const char *name, int dos, char *out, size_t cap,
                char *tried, size_t tcap)
{
    char norm[512];
    const char *base;
    const char *dirs[3];
    int nd = 0;

    str_copy(norm, sizeof norm, name);

    for (char *p = norm; dos != 0 && *p != '\0'; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }

    base = strrchr(norm, '/');
    base = base != NULL ? base + 1 : norm;

    if (*base == '\0' || strlen(base) > 255) {
        return 0;
    }

    /* A full path is used as it is, and nothing else is tried. */
    if (norm[0] == '/') {
        tried_add(tried, tcap, norm);

        return picture_here(norm, out, cap);
    }

    dirs[nd++] = docdir != NULL && docdir[0] != '\0' ? docdir : ".";

    if (workdir != NULL && workdir[0] != '\0' &&
        strcmp(workdir, dirs[0]) != 0) {
        dirs[nd++] = workdir;
    }

    if (strcmp(dirs[0], ".") != 0 &&
        (nd < 2 || strcmp(dirs[1], ".") != 0)) {
        dirs[nd++] = ".";
    }

    for (int d = 0; d < nd; d++) {
        char path[1024];
        DIR *dp;
        struct dirent *e;

        tried_add(tried, tcap, dirs[d]);
        path_join(path, sizeof path, dirs[d], norm);

        if (picture_here(path, out, cap) != 0) {
            return 1;
        }

        /* A name without a directory also matches any case of its letters
           (the names of a DOS disk have no case). */
        if (base != norm) {
            continue;
        }

        dp = opendir(dirs[d]);

        if (dp == NULL) {
            continue;
        }

        while ((e = readdir(dp)) != NULL) {
            if (strcasecmp(e->d_name, base) == 0) {
                path_join(path, sizeof path, dirs[d], e->d_name);

                if (picture_here(path, out, cap) != 0) {
                    closedir(dp);

                    return 1;
                }
            }
        }

        closedir(dp);
    }

    return 0;
}


/*
 * Look for the file of a picture: the name as it is written, and, when it
 * has a backslash, the name with the backslashes made separators.  A file
 * system allows a backslash in a name, and a name of the old program has it
 * for a separator.  The Editor and the Printer both use this function; the
 * rule of the places is in latex.h and docs/PICTURES-PATHS.txt.
 */
int
latex_find_picture(const char *docdir, const char *workdir, const char *name,
                     char *out, size_t cap, char *tried, size_t tcap)
{
    char utf8[512];

    if (tried != NULL && tcap > 0) {
        tried[0] = '\0';
    }

    if (strchr(name, '\\') != NULL &&
        find_picture_as(docdir, workdir, name, 0, out, cap, tried, tcap) != 0) {
        return 1;
    }

    if (find_picture_as(docdir, workdir, name, 1, out, cap, tried, tcap) != 0) {
        return 1;
    }

    /* The name of a document is in the code page of the document format, a
       letter such as an e with an acute accent is one byte; the file system
       has the UTF-8 of it. */
    name_from_cells(name, utf8, sizeof utf8);

    return strcmp(utf8, name) != 0 &&
           find_picture_as(docdir, workdir, utf8, 1, out, cap, tried,
                           tcap) != 0;
}


/*
 * Can the path be written into TeX text as \detokenize{path}?  Any printable
 * ASCII character can, but the braces and the backslash: those are the only
 * ones that the command cannot take.  A letter that is not ASCII is not taken,
 * because the engine reads the bytes of it as active characters.
 */
static int
safe_path(const char *p)
{
    for (; *p != '\0'; p++) {
        if (*p < 32 || *p > 126 || *p == '{' || *p == '}' || *p == '\\') {
            return 0;
        }
    }

    return 1;
}


/* Copy a file byte for byte.  Returns 0, or -1 when it cannot be done. */
static int
copy_picture_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    FILE *out;
    char buf[65536];
    size_t got;
    int ok = 1;

    if (in == NULL) {
        return -1;
    }

    out = fopen(to, "wb");

    if (out == NULL) {
        fclose(in);

        return -1;
    }

    while ((got = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, got, out) != got) {
            ok = 0;
            break;
        }
    }

    if (ferror(in) != 0) {
        ok = 0;
    }

    fclose(in);

    if (fclose(out) != 0) {
        ok = 0;
    }

    return ok != 0 ? 0 : -1;
}


/*
 * A text for a comment line: every byte that is not a printable character
 * of ASCII becomes a question mark, so that a name cannot end the comment.
 */
static void
comment_text(const char *in, char *out, size_t cap)
{
    size_t n = 0;

    for (; *in != '\0' && n + 1 < cap; in++) {
        out[n++] = u8(*in) >= ' ' && u8(*in) < 0x7F ? *in : '?';
    }

    out[n] = '\0';
}


/*
 * Write a path into TeX text so that the engine reads the path as it is: the
 * characters go through \detokenize, but for the percent sign and the number
 * sign, which TeX reads before the command can act (a comment, a parameter):
 * the percent sign is written as the macro of the format for it and the
 * number sign with \string.  The text is for an \edef.
 */
static void
put_tex_path(Buf *b, const char *path)
{
    buf_puts(b, "\\detokenize{");

    for (; *path != '\0'; path++) {
        if (*path == '%') {
            buf_puts(b, "}\\csname @percentchar\\endcsname\\detokenize{");
        } else if (*path == '#') {
            buf_puts(b, "}\\string#\\detokenize{");
        } else {
            buf_printf(b, "%c", *path);
        }
    }

    buf_puts(b, "}");
}


/*
 * A picture that cannot be printed because its file is not there (or cannot
 * be read, or is no picture) is printed as a box that says so, in red, so
 * that a page with a gap cannot be taken for a picture that is not set to
 * print.  The error is in the Error Log.
 */
static void
put_missing_picture(Ctx *c, const char *text, St *st, Buf *b)
{
    ensure_attr(st, b);
    buf_puts(b, "\\strmissing{");
    put_bytes(b, text, strlen(text), 0);
    buf_putc(b, '}');
    st->bol = 0;
    c->o->longform[25]++;
}


static void
emit_picture(Ctx *c, const Text *t, int i, St *st, Buf *b)
{
    const Tok *k = &t->v[i];
    MPayload p;
    const char *name;
    char real[PATH_MAX];
    char use[PATH_MAX + 64];
    char tried[1024];
    char gap[PATH_MAX + 80];
    int kind = 0;
    int ok = 0;
    int piece = 0;

    gap[0] = '\0';
    mp_parse(&p, marker_info(25), tok_mdata(k), tok_mlen(k));
    name = mp_get(&p, "1");

    if (name == NULL || name[0] == '\0') {
        diag_here(c, DIAG_ERROR, "picture-noname", "",
                  "The picture marker has no file name.");
        str_copy(gap, sizeof gap, "Picture marker without a file name");
    } else if (latex_find_picture(c->o->docdir, c->o->picwork, name, real,
                                  sizeof real, tried, sizeof tried) == 0) {
        diag_here(c, DIAG_ERROR, "picture-missing", NULL,
                  "Picture file not found: %s (looked in %s).", name, tried);
        snprintf(gap, sizeof gap, "Picture file not found: %s", name);
    } else if (access(real, R_OK) != 0) {
        diag_here(c, DIAG_ERROR, "picture-unreadable", NULL,
                  "The picture file %s cannot be read: %s.", name,
                  strerror(errno));
        snprintf(gap, sizeof gap, "Picture file cannot be read: %s", name);
    } else {
        kind = latex_picture_kind(real);

        if (kind == 0) {
            diag_here(c, DIAG_ERROR, "picture-type", NULL,
                      "The picture file %s is of a type that cannot be "
                      "printed (only PNG, JPEG, PDF and PostScript can).",
                      name);
            snprintf(gap, sizeof gap, "Picture file is not an image "
                     "that can be printed: %s", name);
        } else if (picture_valid(real, kind) == 0) {
            diag_here(c, DIAG_ERROR, "picture-damaged", NULL,
                      "The picture file %s is incomplete or damaged.", name);
            snprintf(gap, sizeof gap, "Picture file is damaged: %s", name);
            kind = 0;
        }
    }

    if (kind != 0) {
        static const char *const ext[] = {"", "png", "jpg", "pdf", "eps"};

        piece = ++c->piece;
        c->o->npictures++;

        if (c->o->no_pictures != 0) {
            ok = 0;
        } else if (c->o->picdir != NULL &&
                   (c->o->copy_pictures != 0 || kind == 4)) {
            const char *shown = kind == 4 && c->o->eps_pdf != 0 ? "pdf" :
                                ext[kind];
            char dest[PATH_MAX + 64];

            snprintf(dest, sizeof dest, "%s/pic%d.%s", c->o->picdir, piece,
                     ext[kind]);

            if (copy_picture_file(real, dest) == 0) {
                snprintf(use, sizeof use, "%s/pic%d.%s", c->o->picref, piece,
                         shown);
                ok = 1;

                if (c->o->export_name != NULL) {
                    char shown_name[256];

                    comment_text(name, shown_name, sizeof shown_name);
                    buf_printf(&c->pics, "%% %s = %s\n", use, shown_name);
                }
            }
        } else if (c->o->workdir != NULL) {
            snprintf(use, sizeof use, "%s/pic%d.%s", c->o->workdir, piece,
                     ext[kind]);
            unlink(use);

            if (symlink(real, use) == 0) {
                snprintf(use, sizeof use, "pic%d.%s", piece,
                         kind == 4 && c->o->eps_pdf != 0 ? "pdf" : ext[kind]);
                ok = 1;
            }
        } else if (safe_path(real) != 0) {
            str_copy(use, sizeof use, real);
            ok = 1;

            if (c->o->export_name != NULL) {
                char shown_name[256];

                comment_text(name, shown_name, sizeof shown_name);
                buf_printf(&c->pics, "%% %s = %s\n", use, shown_name);
            }
        }
    }

    if (ok != 0 && is_disabled(c, piece) == 0) {
        const char *w = mp_get(&p, "2");
        const char *h = mp_get(&p, "3");
        const char *sc = mp_get(&p, "4");
        int wv = w != NULL ? atoi(w) : 0;
        int hv = h != NULL ? atoi(h) : 0;
        int sv = sc != NULL ? atoi(sc) : 0;

        /* A name with a percent sign or a number sign is made into a macro
           first, which the picture command then reads as a name. */
        int special = strpbrk(use, "%#") != NULL;

        if (special != 0) {
            buf_puts(b, "%\n\\begingroup\\edef\\strfile{");
            put_tex_path(b, use);
            buf_puts(b, "}\\strpic{");
        } else {
            buf_puts(b, "%\n\\strpic{");
        }

        if (wv > 0 && hv > 0) {
            buf_puts(b, "width=");
            put_in(b, wv);
            buf_puts(b, ",height=");
            put_in(b, hv);
            buf_puts(b, ",keepaspectratio");
        } else if (wv > 0) {
            buf_puts(b, "width=");
            put_in(b, wv);
        } else if (hv > 0) {
            buf_puts(b, "height=");
            put_in(b, hv);
        } else if (sv > 0) {
            buf_printf(b, "scale=%d.%03d", sv / 1000, sv % 1000);
        }

        if (special != 0) {
            buf_printf(b, "}{\\strfile}\\endgroup%%F%d\n{}", piece);
        } else {
            buf_puts(b, "}{");
            put_tex_path(b, use);
            buf_printf(b, "}%%F%d\n{}", piece);
        }
        st->bol = 0;
    } else {
        if (kind != 0 && is_disabled(c, piece) != 0) {
            const char *why = piece_message(c, piece);

            diag_here(c, DIAG_ERROR, "picture-refused", NULL,
                      "The picture cannot be used%s%s%s, so it is printed "
                      "as text.", why[0] != '\0' ? " (" : "", why,
                      why[0] != '\0' ? ")" : "");
        } else if (kind != 0 && c->o->no_pictures != 0) {
            diag_here(c, DIAG_ERROR, "picture-refused", NULL,
                      "TeX stopped without saying why, so no picture is "
                      "printed.");
        } else if (kind != 0) {
            diag_here(c, DIAG_ERROR, "picture-link", NULL,
                      "The picture file cannot be given to TeX here: its "
                      "name has a brace, a backslash or a letter that is not "
                      "ASCII (copy the pictures to avoid this).");
        }

        if (kind == 0 && gap[0] != '\0') {
            put_missing_picture(c, gap, st, b);
        } else {
            put_long_only(c, k, st, b);
        }
    }

    mp_free(&p);
}


static void
emit_equation(Ctx *c, const Text *t, int i, St *st, Buf *b)
{
    const Tok *k = &t->v[i];
    char eq[8192];
    int n = marker_equation_text(k, eq, sizeof eq);
    int piece;

    if (n < 0 || n >= isz(sizeof eq) - 1) {
        diag_here(c, DIAG_ERROR, "equation-unreadable", NULL,
                  "The equation cannot be read: it is too long or damaged, "
                  "so it is printed as text.");
        put_long_only(c, k, st, b);

        return;
    }

    for (int j = 0; j < n; j++) {
        if (eq[j] == '\n') {
            eq[j] = ' ';
        }
    }

    while (n > 0 && eq[n - 1] == ' ') {
        eq[--n] = '\0';
    }

    if (latex_math_ok(eq) == 0) {
        if (n == 0) {
            diag_here(c, DIAG_WARNING, "equation-empty", NULL,
                      "The equation is empty.");
        } else {
            diag_here(c, DIAG_ERROR, "equation-invalid", NULL,
                      "The equation is not valid TeX math, "
                      "so it is printed as text.");
        }

        put_long_only(c, k, st, b);

        return;
    }

    piece = ++c->piece;

    if (is_disabled(c, piece) != 0) {
        const char *why = piece_message(c, piece);

        diag_here(c, DIAG_ERROR, "equation-refused", NULL,
                  "TeX rejects the equation%s%s%s, so it is printed as "
                  "text.", why[0] != '\0' ? " (" : "", why,
                  why[0] != '\0' ? ")" : "");
        put_long_only(c, k, st, b);

        return;
    }

    if (eqn_is_display(t, i) != 0 && c->incell == 0 && c->inbox == 0) {
        close_attr(st, b);
        buf_printf(b, "%%\n\\[%s\\]%%F%d\n{}", eq, piece);
    } else {
        ensure_attr(st, b);
        buf_printf(b, "%%\n$%s$%%F%d\n{}", eq, piece);
    }

    st->bol = 0;
}


/* The longest text of a LaTeX marker that is written (bytes). */
#define LATEX_RAW_MAX 65536


/*
 * A LaTeX marker: its text is written as it is, on lines of their own, with
 * nothing added to it, escaped or wrapped.  The text of a marker that has
 * an open group or environment cannot be repaired here: TeX reports it, and the
 * error is found at the line of the document that holds the marker.  A
 * comment sign ends the text so that the end of its last line does not make
 * a space, and an empty group keeps the space that follows the marker.
 * When LaTeX markers are not allowed (no_latex) the text is not written at
 * all and the marker is printed as its words.
 */
static void
emit_latex_marker(Ctx *c, const Tok *k, St *st, Buf *b)
{
    char *raw;
    int n;
    char u[4];

    if (c->o->no_latex != 0) {
        diag_here(c, DIAG_WARNING, "latex-marker-off", NULL,
                  "LaTeX markers are not allowed in this print: the text of "
                  "this one is not written and the marker is printed as "
                  "text.");
        put_long_only(c, k, st, b);

        return;
    }

    raw = malloc(LATEX_RAW_MAX);
    n = marker_latex_text(k, raw, LATEX_RAW_MAX);

    if (n < 0) {
        free(raw);
        diag_here(c, DIAG_ERROR, "latex-marker-unreadable", NULL,
                  "The text of this LaTeX marker is too long or damaged, so "
                  "it is not written and the marker is printed as text.");
        put_long_only(c, k, st, b);

        return;
    }

    if (n == 0) {
        free(raw);

        return;
    }

    close_attr(st, b);
    buf_puts(b, "%\n");

    for (int j = 0; j < n; j++) {
        uint8_t ch = u8(raw[j]);

        if (ch >= 0x80) {
            latex_cp437_utf8(ch, u);
            buf_puts(b, u);
        } else {
            buf_putc(b, chr(ch));
        }
    }

    /* The empty group keeps the space that follows the marker: a space at
       the start of a line of the file would be lost. */
    buf_puts(b, "%\n{}");
    st->bol = 0;
    free(raw);
}


/* The family command for the font number n of a Keycap (see emit_keycap). */
static const char *
keycap_family(const Doc *d, int n)
{
    switch (font_family(d, n)) {
        case 1:
            return "\\sffamily ";

        case 2:
            return "\\ttfamily ";

        default:
            return "\\rmfamily ";
    }
}


/*
 * A keycap.  Field 5 is the font as a number: 1 Headline, 2 Text (no
 * record), 3 Header/Footer and 4 to 20 the fonts of the Global Typeface
 * settings, which are the DF records of the header, so the number is the
 * number of the font.  The legend is set in the family that the name of
 * that font stands for (font_family); a number without a record has the
 * roman face.
 */
static void
emit_keycap(Ctx *c, const Tok *k, St *st, Buf *b)
{
    MPayload p;
    KeycapFmt kf;
    const char *legend;
    int fg;
    int bg;
    int sc;
    int font;

    /* A value that the marker leaves out is the one of the Keycap Format of
       the document, as on the screen. */
    keycap_values(c->d, k, &kf);
    fg = kf.fg;
    bg = kf.bg;
    sc = kf.scale;
    font = kf.font;
    mp_parse(&p, marker_info(37), tok_mdata(k), tok_mlen(k));
    legend = mp_get(&p, "1");
    fg = fg < 0 ? 0 : fg > 1000 ? 1000 : fg;
    bg = bg < 0 ? 0 : bg > 1000 ? 1000 : bg;
    sc = sc < 1 ? 1 : sc > 10000 ? 10000 : sc;
    ensure_attr(st, b);
    buf_printf(b, "\\strkey{%d.%03d}{%d.%03d}{%d.%03d}{%s}{", fg / 1000,
               fg % 1000, bg / 1000, bg % 1000, sc / 1000, sc % 1000,
               keycap_family(c->d, font));

    if (legend != NULL) {
        put_legend(b, legend, strlen(legend));
    }

    buf_putc(b, '}');
    st->bol = 0;
    mp_free(&p);
}


static void
emit_line_marker(const Tok *k, St *st, Buf *b)
{
    MPayload p;
    const char *str;
    const char *v;
    int len = 0;
    int wt = 0;

    mp_parse(&p, marker_info(18), tok_mdata(k), tok_mlen(k));
    str = mp_get(&p, "1");
    v = mp_get(&p, "2");
    len = v != NULL ? atoi(v) : 0;
    v = mp_get(&p, "3");
    wt = v != NULL ? atoi(v) : 0;
    ensure_attr(st, b);

    if (str != NULL && str[0] != '\0') {
        if (len > 0) {
            buf_puts(b, "\\leavevmode\\hbox to ");
            put_in(b, len);
            buf_puts(b, "{\\leaders\\hbox{");
        } else {
            buf_puts(b, "\\leavevmode\\leaders\\hbox{");
        }

        put_bytes(b, str, strlen(str), 0);
        buf_puts(b, len > 0 ? "}\\hfill}" : "}\\hfill\\null ");
    } else {
        buf_puts(b, len > 0 ? "\\leavevmode\\rule[0.3ex]{" :
                              "\\leavevmode\\leaders\\hrule ");

        if (len > 0) {
            put_in(b, len);
            buf_puts(b, "}{");
        } else {
            buf_puts(b, "height ");
        }

        if (wt > 0) {
            /* A weight is cut to the height of the characters. */
            put_in(b, wt < 3 ? 3 : wt > 20 ? 20 : wt);
        } else {
            buf_puts(b, "0.4pt");
        }

        buf_puts(b, len > 0 ? "}" : " depth 0pt\\hfill\\null ");
    }

    st->bol = 0;
    mp_free(&p);
}


/* A marker that gives a break or a space of its own. */
static void
emit_vertical(Ctx *c, const Tok *k, St *st, Buf *b)
{
    MPayload p;
    const char *v;

    if (tok_mtype(k) == 31) {
        int ws;
        int unit;
        int treat;

        mp_parse(&p, marker_info(31), tok_mdata(k), tok_mlen(k));
        v = mp_get(&p, "1");
        ws = v != NULL ? atoi(v) : 0;
        v = mp_get(&p, "2");
        treat = v != NULL ? atoi(v) : 1;
        treat = treat >= 0 && treat <= 2 ? treat : 1;
        v = mp_get(&p, "U");
        unit = v != NULL ? atoi(v) : 0;
        mp_free(&p);

        /* A size of 0 (or none) prints nothing. */
        if (ws == 0) {
            return;
        }

        if (c->incell > 0 || c->inbox > 0) {
            buf_puts(b, "\\rule{0pt}{");
            put_size(b, ws, unit);
            buf_putc(b, '}');
        } else {
            close_attr(st, b);
            if (ws < 0 || c->keep > 0) {
                buf_puts(b, "\\par\\vspace*{");
                put_size(b, ws, unit);
                buf_puts(b, "}\n");
            } else {
                /* The macro knows the treatments: see strws. */
                buf_puts(b, "\\strws{");
                put_size(b, ws, unit);
                buf_printf(b, "}{%d}\n", treat);
            }

            st->bol = 1;
        }

        return;
    }

    if (c->incell > 0 || c->inbox > 0) {
        diag_here(c, DIAG_WARNING, "marker-incell", NULL,
                  "A page break or column break inside a table or a line "
                  "with tabs cannot be honored; it is printed as text.");
        put_long_only(c, k, st, b);

        return;
    }

    close_attr(st, b);
    mp_parse(&p, marker_info(23), tok_mdata(k), tok_mlen(k));
    v = tok_mtype(k) == 23 ? mp_get(&p, "1") : NULL;
    buf_puts(b, "\\par");
    buf_puts(b, v != NULL && atoi(v) == 1 ? "\\strpbodd\n" :
                v != NULL && atoi(v) == 2 ? "\\strpbeven\n" :
                                            "\\clearpage\n");
    mp_free(&p);
    st->bol = 1;
}


/* A cross-reference: the tag or headline of a section, or its page. */
static void
emit_xref(Ctx *c, const Tok *k, St *st, Buf *b)
{
    char text[400];
    MPayload p;
    const char *xt;
    int style;
    int item = marker_xref_item(k);

    mp_parse(&p, marker_info(2), tok_mdata(k), tok_mlen(k));
    xt = mp_get(&p, "XT");
    style = xt != NULL ? atoi(xt) : 0;
    mp_free(&p);
    ensure_attr(st, b);
    st->bol = 0;

    if (style == 4 && item >= 0 && c->want_label != NULL) {
        buf_printf(b, "\\pageref{s%d}", item);
        c->o->pagerefs = 1;

        return;
    }

    if (marker_eval(k, text, sizeof text) == 0) {
        diag_here(c, DIAG_WARNING, "xref-unresolved", NULL,
                  "The cross-reference points at a section that is not in "
                  "the document (or not in this part of it).");
        put_long_only(c, k, st, b);

        return;
    }

    put_bytes(b, text, strlen(text), 0);
}


/*
 * A special symbol: the text a name or a code stands for.  The names and
 * codes that are known are the ones of the Special Symbols table; any other
 * symbol is printed in its long form.
 */
static void
emit_symbol(Ctx *c, const Tok *k, St *st, Buf *b)
{
    static const struct {
        const char *name;
        const char *text;
    } names[] = {{"-", "-"},
                 {"--", "\xE2\x80\x93"},
                 {"---", "\xE2\x80\x94"},
                 {"(c)", "\xC2\xA9"},
                 {"(r)", "\xC2\xAE"},
                 {"tm", "\xE2\x84\xA2"}};
    MPayload p;
    const char *name;
    const char *code;
    const char *text = NULL;

    mp_parse(&p, marker_info(32), tok_mdata(k), tok_mlen(k));
    name = mp_get(&p, "2");
    code = mp_get(&p, "1");

    for (size_t i = 0; name != NULL && i < sizeof names / sizeof names[0];
         i++) {
        if (strcmp(names[i].name, name) == 0) {
            text = names[i].text;
        }
    }

    if (text == NULL && code != NULL && atoi(code) == 440) {
        text = "\xC2\xA9";
    }

    if (text != NULL) {
        ensure_attr(st, b);
        buf_puts(b, text);
        st->bol = 0;
    } else {
        diag_here(c, DIAG_WARNING, "symbol-unknown", NULL,
                  "The special symbol is not known to Strata; it is printed "
                  "as text.");
        put_long_only(c, k, st, b);
    }

    mp_free(&p);
}


/* Make a diagnostic for a marker that waits for its partner. */
static void
pend_here(Ctx *c, Diag *g, int kind, const char *code, const char *msg)
{
    diag_make(c, g, kind, code, NULL, msg);
}


/* Is this Keep marker an End Keep?  (The field is 2 for End.) */
static int
keep_is_end(const Tok *k)
{
    MPayload p;
    const char *v;
    int end;

    mp_parse(&p, marker_info(4), tok_mdata(k), tok_mlen(k));
    v = mp_get(&p, "1");
    end = v != NULL && atoi(v) == 2;
    mp_free(&p);

    return end;
}


/*
 * Keep markers must come in pairs: the writer tells an End without a Start
 * and a second Start.  What is still open at the end is told by close_pairs.
 * The If, Else and Endif markers are paired before the writer starts (see
 * cond.h), and they are not in the document that it writes.
 */
static void
track_pairs(Ctx *c, const Tok *k)
{
    switch (tok_mtype(k)) {
        case 4:
            if (keep_is_end(k) != 0) {
                if (c->keep_open == 0) {
                    diag_here(c, DIAG_ERROR, "keep-unmatched", NULL,
                              "This End Keep has no Start Keep before it.");
                }

                c->keep_open = 0;
            } else {
                if (c->keep_open != 0) {
                    diag_here(c, DIAG_ERROR, "keep-doubled", NULL,
                              "A Keep was started while another is still "
                              "open; the first has no End Keep.");
                }

                c->keep_open = 1;
                pend_here(c, &c->keep_start, DIAG_ERROR, "keep-unclosed",
                          "This Start Keep has no End Keep after it.");
            }

            break;

        default:
            break;
    }
}


/* Tell the Keep marker that was never closed. */
static void
close_pairs(Ctx *c)
{
    if (c->o->diags == NULL) {
        return;
    }

    if (c->keep_open != 0) {
        diag_push(c->o->diags, &c->keep_start);
    }
}


static void
emit_marker_in(Ctx *c, const Text *t, int i, St *st, Buf *b);


static void
emit_marker(Ctx *c, const Text *t, int i, St *st, Buf *b)
{
    /* A Print Error is not part of the document: it is left out. */
    if (tok_mtype(&t->v[i]) == MT_PRINT_ERROR) {
        return;
    }

    c->mtok = &t->v[i];
    track_pairs(c, c->mtok);
    emit_marker_in(c, t, i, st, b);
    c->mtok = NULL;
}


static void
emit_marker_in(Ctx *c, const Text *t, int i, St *st, Buf *b)
{
    const Tok *k = &t->v[i];
    char text[600];

    switch (tok_mtype(k)) {
        case 1:
            emit_note(c, t, i, st, b);
            break;

        case 2:
            emit_xref(c, k, st, b);
            break;

        case 5:
        case 7:
        case 8:
        case 12:
        case 13:
        case 14:
        case 24:
        case 26:
        case 27:
        case 30:
            marker_value(k, text, sizeof text);
            ensure_attr(st, b);
            put_bytes(b, text, strlen(text), 0);
            st->bol = 0;
            break;

        case 6:
            /* A comment is part of a draft print only. */
            if (marker_print_mode == 1) {
                size_t tn;
                const char *tp = text;

                marker_render(k, text, sizeof text);
                tn = strlen(text);

                /* The label has its brackets; strlongbr puts them on. */
                if (tn >= 2 && text[0] == '[' && text[tn - 1] == ']') {
                    tp = text + 1;
                    tn -= 2;
                }

                ensure_attr(st, b);
                buf_puts(b, "\\strlongbr{");
                put_bytes(b, tp, tn, 0);
                buf_putc(b, '}');
                st->bol = 0;
            }

            break;

        case 9:
            emit_equation(c, t, i, st, b);
            break;

        case 15: {
            MPayload fp;
            const char *fv;

            /* A font marker changes the family until the end of the block;
               a marker without a number goes back to the block's font. */
            mp_parse(&fp, marker_info(15), tok_mdata(k), tok_mlen(k));
            fv = mp_get(&fp, "1");
            st->fam_cur = fv != NULL ? font_family(c->d, atoi(fv)) : -1;
            mp_free(&fp);
            break;
        }

        case 17:
            if (marker_eval(k, text, sizeof text) != 0) {
                ensure_attr(st, b);
                put_bytes(b, text, strlen(text), 0);
                st->bol = 0;
            } else {
                diag_here(c, DIAG_WARNING, "counter-unresolved", NULL,
                          "The value of the counter cannot be worked out; "
                          "it is printed as text.");
                put_long_only(c, k, st, b);
            }

            break;

        case 18:
            emit_line_marker(k, st, b);
            break;

        case 22:
            ensure_attr(st, b);
            buf_puts(b, "\\thepage{}");
            st->bol = 0;
            break;

        case 28:
            if (c->in_hf == 0) {
                put_long(c, k, st, b);
                break;
            }

            {
                MPayload sp;
                const char *sh;

                mp_parse(&sp, marker_info(28), tok_mdata(k), tok_mlen(k));
                sh = mp_get(&sp, "S");
                ensure_attr(st, b);
                buf_puts(b, sh != NULL && strcmp(sh, "2") == 0 ?
                                "\\strsecnum{}" :
                            sh != NULL && strcmp(sh, "3") == 0 ?
                                "\\strsectitle{}" : "\\strsecmark{}");
                mp_free(&sp);
            }

            st->bol = 0;
            break;

        case 23:
        case 31:
        case 39:
            emit_vertical(c, k, st, b);
            break;

        case 25:
            lm_label(c, k, b);
            emit_picture(c, t, i, st, b);
            break;

        case 3:
        case 11:
        case 29:
            /* The entry of the index or of a list is not printed. */
            if (lm_label(c, k, b) == 0 && c->nlm >= 0 &&
                lm_marker_kept(c, tok_mtype(k)) == 0) {
                put_long(c, k, st, b);
            }

            break;

        case 32:
            emit_symbol(c, k, st, b);
            break;

        case 4:
            /* A Keep marker is never printed: write_body keeps the blocks
               between a Start and an End on one page. */
            break;

        case 36:
            /* The orientation is set by write_body at the block. */
            break;

        case 37:
            emit_keycap(c, k, st, b);
            break;

        case MT_LATEX:
            emit_latex_marker(c, k, st, b);
            break;

        case INCLUDE_MTYPE:
            /* The marker is here because it could not be done (include.c
               has told why): it is printed as text, without a second
               message. */
            put_long_only(c, k, st, b);
            break;

        default:
            put_long(c, k, st, b);
            break;
    }
}


/* Text: tokens, lines and the paragraphs of a block. */
/* The width in cells of the tokens up to the next tab or end of line. */
static int
cells_of(const Text *t, int i0, int i1, int decimal)
{
    int w = 0;

    for (int i = i0; i < i1; i++) {
        const Tok *k = &t->v[i];

        if (k->kind == TK_TAB || k->kind == TK_NL) {
            break;
        }

        if (k->kind == TK_CHAR) {
            if (decimal != 0 && (k->a == '.' || k->a == ',')) {
                break;
            }

            w++;
        } else if (k->kind == TK_CTRL) {
            if (k->a == 0x1E || k->a == 0x1A) {
                w++;
            }
        } else if (k->kind == TK_MARKER) {
            char lb[96];

            switch (marker_render(k, lb, sizeof lb)) {
                case MR_GLYPH:
                    w += 1;
                    break;

                case MR_TEXT:
                case MR_VALUE:
                case MR_KEYCAP:
                    w += slen(lb);
                    break;

                case MR_MISSING:
                case MR_BRACKET:
                case MR_ERROR:
                default:
                    w += slen(lb) + 2;
                    break;
            }
        }
    }

    return w;
}


/*
 * Put the labels of the diagnostics of a failed print that belong here:
 * those at the place of the writer.  With exact not 0 the offset must be
 * that of the token; else the diagnostics of the line that have no
 * offset (and, on the first line, those of the block that have no line)
 * are put.
 */
static void
put_marks(Ctx *c, Buf *b, int exact)
{
    const DiagList *l = c->o->failed;
    int item;
    int col;
    int line;
    int off;

    if (l == NULL || c->mdone == NULL || c->item < 0) {
        return;
    }

    orig_pos(c, &item, &col, &line, &off);

    for (int i = 0; i < l->n; i++) {
        const Diag *g = &l->v[i];
        int here;

        if (c->mdone[i] != 0 || g->item != item || g->col != col) {
            continue;
        }

        if (exact != 0) {
            here = g->line == line && g->off == off && g->off >= 0;
        } else {
            here = (g->line == line && g->off < 0) ||
                   (line == 1 && g->line <= 0);
        }

        if (here != 0) {
            c->mdone[i] = 1;
            buf_printf(b, "{\\strmark%c{%d}}",
                       g->kind == DIAG_ERROR ? 'e' : 'w', i + 1);
        }
    }
}


/* The tokens [i0, i1) of one line, with no tab and no text indent in them. */
static void
emit_range(Ctx *c, const Text *t, int i0, int i1, St *st, Buf *b)
{
    for (int i = i0; i < i1; i++) {
        const Tok *k = &t->v[i];

        c->off = i >= c->lstart ? i - c->lstart : -1;
        put_marks(c, b, 1);

        switch (k->kind) {
            case TK_CHAR:
                ensure_attr(st, b);

                if (k->a == ' ') {
                    int j = i;

                    while (j < i1 && t->v[j].kind == TK_CHAR &&
                           t->v[j].a == ' ') {
                        j++;
                    }

                    put_spaces(b, usz(j - i), st->bol);
                    i = j - 1;
                } else if (k->a == NBSP_BYTE) {
                    buf_putc(b, '~');
                    st->bol = 0;
                } else {
                    put_cp437(b, k->a);
                    st->bol = 0;
                }

                break;

            case TK_CTRL:
                if (k->a == NBSP_CTRL) {
                    ensure_attr(st, b);
                    buf_putc(b, '~');
                    st->bol = 0;
                } else if (k->a == SHY_CTRL) {
                    ensure_attr(st, b);
                    buf_puts(b, "\\-");
                    st->bol = 0;
                }

                break;

            case TK_ATTR:
                st->cur = k->a;
                st->curf = k->b;
                break;

            case TK_TAB:
                ensure_attr(st, b);
                buf_puts(b, "\\quad ");
                st->bol = 0;
                break;

            case TK_MARKER:
                emit_marker(c, t, i, st, b);
                break;

            default:
                break;
        }
    }
}


/*
 * A tag longer than this many characters may be broken after a point.  The
 * tag of a section nested deeply has a point for every level (1.1.1.1.1...)
 * and is one word to TeX, wider than a line, so that it would run out of the
 * page.  Tags of the usual length are written as they are.
 */
#define PRETAG_BREAKABLE 40


/* Put the text that starts the line, such as a section tag. */
static void
take_pretag(Ctx *c, St *st, Buf *b)
{
    if (c->pretag != NULL) {
        size_t len = strlen(c->pretag);

        ensure_attr(st, b);

        if (len <= PRETAG_BREAKABLE) {
            put_bytes(b, c->pretag, len, 0);
        } else {
            size_t from = 0;

            for (size_t i = 0; i < len; i++) {
                if (c->pretag[i] == '.' && i + 1 < len) {
                    put_bytes(b, c->pretag + from, i + 1 - from, 0);
                    buf_puts(b, "\\allowbreak{}");
                    from = i + 1;
                }
            }

            put_bytes(b, c->pretag + from, len - from, 0);
        }

        st->bol = 0;
        c->pretag = NULL;
    }
}


/*
 * A line that has tab characters.  It is made of boxes: the text of every
 * piece is set in a box and the space before it is made so that the piece
 * is where the tab stop of the format puts it (see the macros \strtab
 * and \strgap in the preamble).  The stops are those of the format.
 */
static void
emit_tab_line(Ctx *c, const Text *t, int i0, int i1, const Fmt *f, St *st,
              Buf *b)
{
    int stop1 = f->ntab != 0 ? f->tabpos[0] : 5;
    int x = (f->indent == 1 || f->indent == 3) ? stop1 : 0;
    int k = i0;
    int hang = -1;

    while (k < i1 && t->v[k].kind != TK_TAB) {
        k++;
    }

    buf_puts(b, "\\strstart{");
    put_in(b, ru(x));
    buf_puts(b, "}");
    take_pretag(c, st, b);
    buf_puts(b, "\\strseg{");
    c->inbox++;
    emit_range(c, t, i0, k, st, b);
    close_attr(st, b);
    c->inbox--;
    buf_puts(b, "}");
    x += cells_of(t, i0, k, 0);

    while (k < i1) {
        int nx = k + 1;
        int stop = -1;
        int total;
        int sw;
        int tstop;
        int type = 0;
        char ty = 'L';
        int fill = t->v[k].a != '\0' ? t->v[k].a : ' ';

        while (nx < i1 && t->v[nx].kind != TK_TAB) {
            nx++;
        }

        for (int s = 0; s < f->ntab; s++) {
            if (f->tabpos[s] > x) {
                stop = f->tabpos[s];
                ty = f->tabtype[s];
                break;
            }
        }

        if (stop < 0 && f->ntab == 0) {
            stop = ((x / f->defstep) + 1) * f->defstep;
        }

        total = stop < 0 ? 1 : stop - x;
        total = total < 1 ? 1 : total;
        sw = cells_of(t, k + 1, nx, ty == 'D' || ty == ',');

        if (ty == 'R' || ty == 'N' || ty == 'D' || ty == ',') {
            total -= sw;
            type = 1;
        } else if (ty == 'C') {
            total -= sw / 2;
            type = 2;
        }

        total = total < 1 ? 1 : total;
        tstop = stop < 0 ? x + 1 : stop;
        x += total;

        if (nx == i1 && type == 0) {
            /* The last piece is plain text so that it can go on lines. */
            buf_puts(b, "\\strgap{");
            put_in(b, ru(tstop));
            buf_puts(b, "}{");

            if (fill != ' ') {
                put_cp437(b, fill);
            }

            buf_puts(b, "}");
            emit_range(c, t, k + 1, nx, st, b);
            hang = x;
        } else {
            buf_printf(b, "\\strtab{%d}{", type);
            put_in(b, ru(tstop));
            buf_puts(b, "}{");

            if (fill != ' ') {
                put_cp437(b, fill);
            }

            buf_puts(b, "}{");
            c->inbox++;
            emit_range(c, t, k + 1, nx, st, b);
            close_attr(st, b);
            c->inbox--;
            buf_puts(b, "}");
        }

        x += cells_of(t, k + 1, nx, 0);
        k = nx;
    }

    if (hang >= 0) {
        buf_puts(b, "\\hangindent=");
        put_in(b, ru(hang));
        buf_puts(b, "\\hangafter=1 ");
    }
}


/* One line of a block: the tokens [i0, i1), without the end of line. */
static void
emit_line(Ctx *c, const Text *t, int i0, int i1, const Fmt *f, St *st, Buf *b)
{
    int has_tab = 0;
    int ti = -1;

    st->bol = 1;

    for (int i = i0; i < i1; i++) {
        if (t->v[i].kind == TK_TAB) {
            has_tab = 1;
        } else if (t->v[i].kind == TK_CTRL && t->v[i].a == 0x1A && ti < 0) {
            ti = i;
        }
    }

    if (i0 == i1) {
        if (c->pretag != NULL) {
            take_pretag(c, st, b);
        } else {
            buf_puts(b, "\\mbox{}");
        }
    } else if (has_tab != 0) {
        emit_tab_line(c, t, i0, i1, f, st, b);
    } else if (ti >= 0) {
        Buf pre = {NULL, 0, 0};

        c->inbox++;
        take_pretag(c, st, &pre);
        emit_range(c, t, i0, ti, st, &pre);
        close_attr(st, &pre);
        c->inbox--;
        buf_puts(b, "\\strti{");
        buf_puts(b, pre.p != NULL ? pre.p : "");
        buf_puts(b, "}");
        buf_free(&pre);
        st->bol = 0;
        emit_range(c, t, ti + 1, i1, st, b);
    } else {
        take_pretag(c, st, b);
        emit_range(c, t, i0, i1, st, b);
    }

    close_attr(st, b);

    if (c->incell == 0 && c->hold == 0 && c->dfr.n > 0) {
        buf_puts(b, "%\n");
        buf_puts(b, c->dfr.p);
        c->dfr.n = 0;
        c->dfr.p[0] = '\0';
    }
}


/*
 * All the lines of a text; each ends the paragraph unless it is the last of
 * a cell.
 */
static void
emit_text_lines(Ctx *c, const Text *t, const Fmt *f, Buf *b, int in_cell)
{
    St st = {f->attr, 0, 0, 0, 1, c->fam, -1, 0};
    int s = 0;
    int ln = 0;

    while (s < t->n) {
        int e = s;

        while (e < t->n && t->v[e].kind != TK_NL) {
            e++;
        }

        c->line = ++ln;
        c->lstart = s;

        if (c->o->posmarks != 0) {
            int pitem;
            int pcol;
            int pline;
            int poff;

            orig_pos_off(c, -1, &pitem, &pcol, &pline, &poff);
            buf_printf(b, "%%\n%%@P %d %d %d\n", pitem, pcol, pline);
        }

        put_marks(c, b, 0);
        emit_line(c, t, s, e, f, &st, b);
        s = e + 1;

        if (in_cell == 0 || s < t->n) {
            buf_puts(b, "\\par\n");
        } else {
            buf_putc(b, '\n');
        }
    }
}


/* The settings of the paragraphs of a block, from its format. */
static void
block_setup(Buf *b, const Fmt *f, int bl, int stretch)
{
    int stop1 = f->ntab != 0 ? f->tabpos[0] : 5;
    int first = (f->indent == 1 || f->indent == 3) ? stop1 : 0;
    int rest = (f->indent == 2 || f->indent == 3) ? stop1 : 0;
    int left = bl + rest;

    buf_puts(b, "\\parskip=0pt ");

    if (stretch != 100) {
        buf_puts(b, "\\setstretch{");
        put_hund(b, stretch);
        buf_puts(b, "}");
    }

    buf_puts(b, "\\leftskip=");

    if (f->just == 1 || f->just == 2) {
        put_in(b, ru(bl));
        buf_puts(b, " plus 1fil \\rightskip=");
        put_in(b, ru(f->br));
        buf_puts(b, f->just == 2 ? " plus 1fil" : "");
        buf_puts(b, " \\parfillskip=0pt \\parindent=0pt ");
    } else {
        put_in(b, ru(left));
        buf_puts(b, " \\rightskip=");
        put_in(b, ru(f->br));
        buf_puts(b, f->just == 3 ? "" : " plus 2em");
        buf_puts(b, " \\parfillskip=0pt plus 1fil \\parindent=");
        put_in(b, ru(first - rest));
        buf_putc(b, ' ');
    }
}


/* A setting of the Global Format panels, from the level or else the global. */
static int
fmt_space(const Doc *d, int level, const char *code, int dflt)
{
    const char *v = level > 0 ? doc_fmt_get(d, level, code) : NULL;

    if (v == NULL || v[0] == '\0') {
        v = doc_fmt_get(d, 0, code);
    }

    return v != NULL && v[0] != '\0' ? atoi(v) : dflt;
}


/*
 * The Page Break Level of the Global Format settings: a headline of this
 * level or a higher one (a smaller number) starts a page.  0 is none.
 */
static int
page_break_level(const Doc *d)
{
    const char *v = props_get(&d->header, "FL", 'P');
    int n = v != NULL && v[0] != '\0' ? atoi(v) : 0;

    return n < 0 ? 0 : n;
}


/*
 * The Page Number Level of the Global Format settings: the page number starts
 * again at 1 at every headline of this level or a higher one.  0 is never.
 */
static int
page_number_level(const Doc *d)
{
    const char *v = props_get(&d->header, "FL", 'G');
    int n = v != NULL && v[0] != '\0' ? atoi(v) : 0;

    return n < 0 ? 0 : n;
}


static void
page_geometry(const Doc *d, int *pw, int *ph, int *ml, int *mr, int *mt,
              int *mb);


/*
 * The indent of the level of an item (Indent to Next Level), in ruler units.
 * As on the screen (view.c), the indent of a deeply nested section stops 24
 * cells short of the width of the text, so that the headline and the text of
 * a section of any level keep a width to be read in.
 */
static int
item_indent(const Ctx *c, int item)
{
    int pw;
    int ph;
    int ml;
    int mr;
    int mt;
    int mb;
    long cells;
    long cap;
    int sum;

    if (c->structured == 0) {
        return 0;
    }

    sum = doc_level_indent(c->d, doc_item_level(c->d, item));
    page_geometry(c->d, &pw, &ph, &ml, &mr, &mt, &mb);
    cells = (long)(pw - ml - mr) * unit_den / unit_num;
    cap = cells > 24 ? cells - 24 : 0;

    return sum > cap ? (int)cap : sum;
}


/* The first block of the document, or -1 when it has none. */
static int
first_block_item(const Doc *d)
{
    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == IT_BLOCK) {
            return i;
        }
    }

    return -1;
}


/*
 * Does the headline of this item start a new page?  Not the first block of
 * the document or of a part, and not inside a Keep box, where a page break
 * cannot be made.
 */
static int
headline_breaks(const Ctx *c, int item, int level)
{
    int pbl = page_break_level(c->d);

    return pbl > 0 && level <= pbl && c->keep == 0 &&
           item > first_block_item(c->d);
}


/*
 * A spacing of a block in hundredths ('L' for the line spacing, 'B' for the
 * space after the block): the one that the block or its named format sets,
 * else the one of the level or of the document.
 */
static int
block_space(const Doc *d, const Block *blk, char sub, int dflt)
{
    const char *v = fmt_block_prop(d, blk, "BS", sub);

    return v != NULL && v[0] != '\0' ? atoi(v) : dflt;
}


/*
 * The line spacing of a block in hundredths: the document's own for a final
 * print, one line for a draft, and twice that when the print is double
 * spaced.
 */
static int
line_stretch(const Ctx *c, int v)
{
    int s = c->o->draft != 0 ? 100 : v;

    return c->o->double_spaced != 0 ? 2 * s : s;
}


/* Space of v hundredths of a line; one line is none to add. */
static void
put_gap(Ctx *c, Buf *b, int v)
{
    if (c->o->draft == 0 && v > 100) {
        buf_puts(b, "\\vspace{");
        put_hund(b, v - 100);
        buf_puts(b, "\\baselineskip}\n");
    }
}


static void
flush_deferred(Ctx *c, Buf *b)
{
    /* A Keep box is still open: its notes wait until the box is placed. */
    if (c->keep == 0 && c->dfr.n > 0) {
        buf_puts(b, c->dfr.p);
        c->dfr.n = 0;
        c->dfr.p[0] = '\0';
    }
}


/* Blocks, headlines and tables. */
/*
 * First Block Type: with Joined (FB2 for the level, else for the document) the
 * headline of a section and the first block of its text are one paragraph.
 */
static int
joined_wanted(const Ctx *c, int level)
{
    return c->structured != 0 && fmt_space(c->d, level, "B", 1) == 2;
}


/* The first text block of the section that the headline item starts, or -1. */
static int
join_partner(const Ctx *c, int item)
{
    Fmt f;
    int j = item + 1;

    if (j >= c->d->nitems || c->d->items[j].type != IT_BLOCK) {
        return -1;
    }

    fmt_resolve(c->d, j, 0, &f);

    return f.headline != 0 ? -1 : j;
}


/* Why the two blocks cannot be joined, or NULL when they can. */
static const char *
join_refusal(const Ctx *c, int item, int j, int level)
{
    const Block *hb = c->d->items[item].blk;
    const Block *tb = c->d->items[j].blk;

    if (hb->ncols > 1 || tb->ncols > 1) {
        return "Cannot join multicolumn headline/text blocks";
    }

    if (font_number(c->d, hb, 0, 1, level) !=
        font_number(c->d, tb, 0, 0, level)) {
        return "Cannot join headline/text blocks - different fonts";
    }

    return NULL;
}


/*
 * The mark that the Section marker of a header or footer prints: the tag of
 * the headline as it is printed at the headline (the blanks at its end taken
 * off, so "1.1.6" or "A)") and its title (80 characters at most), in two
 * groups, {TAG}{TITLE}.  The tag is empty when the numbering is off.
 */
static void
emit_secmark(Ctx *c, const Block *blk, const Fmt *f)
{
    char title[200];
    char tag[SECT_NUMLEN];
    size_t n;

    if (c->hf_any == 0 || blk->ncols <= 0) {
        return;
    }

    text_plain(&blk->cols[0].text, title, isz(sizeof title));
    title[80] = '\0';
    snprintf(tag, sizeof tag, "%s", f->tag);

    for (n = strlen(tag); n > 0 && tag[n - 1] == ' '; n--) {
        tag[n - 1] = '\0';
    }

    buf_puts(&c->out, "\\mark{{");
    put_cp437_string(&c->out, tag);
    buf_puts(&c->out, "}{");
    put_cp437_string(&c->out, title);
    buf_puts(&c->out, "}}\n");
}


/* The headline and the text of its section as one paragraph. */
static void
emit_joined(Ctx *c, int item, int j, const Fmt *f)
{
    const Doc *d = c->d;
    const Block *blk = d->items[item].blk;
    const Block *tb = d->items[j].blk;
    int lv = f->level;
    int first_child = item >= 2 && d->items[item - 2].type == IT_PLUS;
    int before = fmt_space(d, lv, first_child ? "SC" : "SS", 100);
    int stretch = line_stretch(c, block_space(d, tb, 'L',
                                              fmt_space(d, lv, "SL", 100)));
    Fmt ft;
    Buf hb = {NULL, 0, 0};
    Buf head = {NULL, 0, 0};
    Buf text = {NULL, 0, 0};

    fmt_resolve(d, j, 0, &ft);
    ft.bl += item_indent(c, j);
    buf_puts(&c->out, "\\par\n");

    if (headline_breaks(c, item, lv) != 0) {
        buf_puts(&c->out, "\\clearpage\n");
    } else {
        put_gap(c, &c->out, before);
    }

    if (lv <= page_number_level(d)) {
        buf_puts(&c->out, "\\setcounter{page}{1}\n");
    }

    if (c->keep == 0) {
        buf_puts(&c->out, "\\needspace{4\\baselineskip}\n");
    }

    emit_secmark(c, blk, f);

    block_setup(&hb, &ft, ft.bl, stretch);

    if (c->want_label != NULL && c->want_label[item] != 0) {
        buf_printf(&head, "\\label{s%d}", item);
    }

    c->hold++;
    c->fam = font_family(d, font_number(d, blk, 0, 1, lv));
    c->pretag = f->tag[0] != '\0' ? f->tag : NULL;

    if (blk->ncols > 0) {
        Fmt f2 = *f;

        f2.indent = 0;
        emit_text_lines(c, &blk->cols[0].text, &f2, &head, 1);
    }

    c->pretag = NULL;
    c->item = j;
    c->fam = font_family(d, font_number(d, tb, 0, 0, lv));

    if (tb->ncols > 0) {
        emit_text_lines(c, &tb->cols[0].text, &ft, &text, 1);
    }

    c->hold--;
    buf_puts(&c->out, "{");
    buf_puts(&c->out, hb.p != NULL ? hb.p : "");
    buf_printf(&c->out, "\n{\\strhfont{%d}", lv);
    buf_puts(&c->out, head.p != NULL ? head.p : "");
    buf_puts(&c->out, "}\\ \\ ");
    buf_puts(&c->out, text.p != NULL ? text.p : "");
    buf_puts(&c->out, "\\par}\n");
    flush_deferred(c, &c->out);
    put_gap(c, &c->out, block_space(d, tb, 'B', fmt_space(d, lv, "ST", 100)));
    buf_free(&hb);
    buf_free(&head);
    buf_free(&text);
    c->skip_item = j;
}



static void
emit_headline(Ctx *c, int item, const Fmt *f)
{
    const Doc *d = c->d;
    const Block *blk = d->items[item].blk;
    int lv = f->level;
    int first_child = item >= 2 && d->items[item - 2].type == IT_PLUS;
    int before = fmt_space(d, lv, first_child ? "SC" : "SS", 100);
    int after = fmt_space(d, lv, "SH", 100);
    int stretch = line_stretch(c, fmt_space(d, lv, "SL", 100));
    Fmt f2 = *f;
    Buf hb = {NULL, 0, 0};
    Buf body = {NULL, 0, 0};

    c->item = item;
    c->col = 0;
    f2.indent = 0;

    if (c->print_mode == 0 && joined_wanted(c, lv) != 0 &&
        join_partner(c, item) >= 0) {
        int j = join_partner(c, item);
        const char *why = join_refusal(c, item, j, lv);

        if (why == NULL) {
            emit_joined(c, item, j, f);

            return;
        }

        diag_here(c, DIAG_ERROR, "join-refused", NULL, "%s", why);
    }

    buf_puts(&c->out, "\\par\n");

    if (headline_breaks(c, item, lv) != 0) {
        /* A page begins at this headline: no space is left before it. */
        buf_puts(&c->out, "\\clearpage\n");
    } else {
        put_gap(c, &c->out, before);
    }

    if (lv <= page_number_level(d)) {
        /* The numbering starts again here: this page is page 1. */
        buf_puts(&c->out, "\\setcounter{page}{1}\n");
    }

    if (c->keep == 0) {
        buf_puts(&c->out, "\\needspace{4\\baselineskip}\n");
    }

    block_setup(&hb, &f2, f->bl, stretch);

    emit_secmark(c, blk, f);

    if (c->want_label != NULL && c->want_label[item] != 0) {
        buf_printf(&body, "\\label{s%d}", item);
    }

    c->hold++;
    c->fam = font_family(d, font_number(d, blk, 0, 1, lv));
    c->pretag = f->tag[0] != '\0' ? f->tag : NULL;

    if (blk->ncols > 0) {
        emit_text_lines(c, &blk->cols[0].text, &f2, &body, 1);
    }

    c->pretag = NULL;
    c->hold--;
    buf_printf(&c->out, "\\strhead{%d}{", lv);
    buf_puts(&c->out, hb.p != NULL ? hb.p : "");
    buf_puts(&c->out, "}{");
    buf_puts(&c->out, body.p != NULL ? body.p : "");
    buf_puts(&c->out, "}\n");
    flush_deferred(c, &c->out);
    put_gap(c, &c->out, after);
    buf_free(&hb);
    buf_free(&body);
}


static void
emit_block(Ctx *c, int item)
{
    const Block *blk = c->d->items[item].blk;
    Fmt f;
    int stretch;

    c->item = item;
    c->col = 0;
    fmt_resolve(c->d, item, 0, &f);
    f.bl += item_indent(c, item);

    if (c->structured != 0 && f.headline != 0 && f.level >= 1) {
        emit_headline(c, item, &f);

        return;
    }

    stretch = line_stretch(c, block_space(c->d, blk, 'L',
                                          fmt_space(c->d, c->depth, "SL",
                                                    100)));
    c->fam = font_family(c->d, font_number(c->d, blk, 0, 0, c->depth));
    buf_puts(&c->out, "{");
    block_setup(&c->out, &f, f.bl, stretch);
    buf_putc(&c->out, '\n');

    if (blk->ncols > 0) {
        emit_text_lines(c, &blk->cols[0].text, &f, &c->out, 0);
    }

    buf_puts(&c->out, "}\n");
    put_gap(c, &c->out, block_space(c->d, blk, 'B',
                                    fmt_space(c->d, c->depth, "ST", 100)));
}


/* Tell whether two blocks have the same columns (rows of one table). */
static int
same_columns(const Block *a, const Block *b)
{
    if (a->ncols != b->ncols) {
        return 0;
    }

    for (int i = 0; i < a->ncols; i++) {
        if (a->cols[i].width != b->cols[i].width) {
            return 0;
        }
    }

    return 1;
}


/* ------------------------------------------------------- the borders */
/*
 * The borders of a block (see border.h): CB on each column, BT and BB (the Top
 * and the Bottom Gutter, thousandths of an inch) and BH (the Shading, percent)
 * on the block.  A block with a border or a shading is set as a table of one
 * or more columns.  The lines are the width of a rule of the table (single one
 * line, double two, wide a thick one): the Line Weight of Global Options is
 * dimmed in the Editor and is not used.
 */
static int
block_shade(const Block *blk)
{
    const char *v = props_get(&blk->props, "BH", 0);
    int n = v != NULL ? atoi(v) : 0;

    return n < 0 ? 0 : n > 100 ? 100 : n;
}


static int
block_gutter(const Block *blk, const char *code)
{
    const char *v = props_get(&blk->props, code, 0);
    int n = v != NULL ? atoi(v) : 0;

    return n < 0 ? 0 : n > 3000 ? 3000 : n;
}


/* Does a block have a border or a shading? */
static int
block_bordered(const Block *blk)
{
    int flag;

    for (int c = 0; c < blk->ncols; c++) {
        if (border_parse(props_get(&blk->cols[c].props, "CB", 0), &flag) != 0) {
            return 1;
        }
    }

    return block_shade(blk) > 0;
}


/* The flag of the borders of a block: 1 Connect Borders Yes, 0 No. */
static int
block_connect(const Block *blk)
{
    int flag = 1;

    for (int c = 0; c < blk->ncols; c++) {
        if (border_parse(props_get(&blk->cols[c].props, "CB", 0), &flag) !=
            0) {
            return flag != 0;
        }
    }

    return 1;
}



/* The weight (none, single, double, wide) of a side of a column's border. */
static int
border_weight(const Column *col, int side)
{
    int flag;

    return border_get(border_parse(props_get(&col->props, "CB", 0), &flag),
                      side);
}


/* The sides that have a border: 1 left, 2 top, 4 right, 8 bottom. */
static int
border_sides(const Column *col)
{
    return (border_weight(col, BS_LEFT) != 0 ? 1 : 0) |
           (border_weight(col, BS_TOP) != 0 ? 2 : 0) |
           (border_weight(col, BS_RIGHT) != 0 ? 4 : 0) |
           (border_weight(col, BS_BOTTOM) != 0 ? 8 : 0);
}


/*
 * How a table draws its borders.  The lines are never made by the rules of
 * the table, which stop at the end of a row and leave a gap in a vertical
 * line wherever a double line is drawn between two rows.  Every cell draws
 * the vertical lines of its own edges itself, in the padding of the cell and
 * with a total width of zero, so that the width of the columns does not
 * change.  Every line between two rows is a row of its own, as high as the
 * line, made of one box for each column and the vertical lines that go
 * through it, so that corners, tees and crossings are closed for every kind
 * of line.  A line between two columns is drawn once, at the right edge of
 * the left column; where the two columns disagree the stronger one is drawn.
 */
#define BORDER_WIDE_PT "1.2pt"


/* The width of the group of rules of a vertical line of weight w. */
static const char *
vgroup_width(int w)
{
    return w == BW_SINGLE ? "\\arrayrulewidth" :
           w == BW_DOUBLE ? "\\dimexpr2\\arrayrulewidth+\\doublerulesep\\relax" :
           w == BW_WIDE ? BORDER_WIDE_PT : "0pt";
}


/* The rules of a vertical line, as wide as vgroup_width says. */
static void
put_vgroup(Buf *b, int w)
{
    if (w == BW_SINGLE) {
        buf_puts(b, "\\vrule width\\arrayrulewidth ");
    } else if (w == BW_DOUBLE) {
        buf_puts(b, "\\vrule width\\arrayrulewidth \\kern\\doublerulesep "
                 "\\vrule width\\arrayrulewidth ");
    } else if (w == BW_WIDE) {
        buf_puts(b, "\\vrule width" BORDER_WIDE_PT " ");
    }
}


/* The width of the cell of a column: the text and the padding of both sides. */
static void
put_cellw(Buf *b, int w, int shade)
{
    if (shade > 0) {
        put_in(b, w + COLPAD);
    } else {
        buf_puts(b, "\\dimexpr");
        put_in(b, w);
        buf_puts(b, "+2\\tabcolsep\\relax");
    }
}


/*
 * The vertical lines of a cell, to be put after the text of the cell: back to
 * the left edge, the line at the left edge when left is not none, on to the
 * right edge, and the line at the right edge.  The total width is zero.
 */
static void
put_cell_vlines(Buf *b, int w, int shade, int left, int right)
{
    if (left == BW_NONE && right == BW_NONE) {
        return;
    }

    buf_puts(b, "\\kern-");
    put_cellw(b, w, shade);
    buf_putc(b, ' ');
    put_vgroup(b, left);
    buf_puts(b, "\\kern\\dimexpr");
    put_cellw(b, w, shade);
    buf_printf(b, "-%s-%s\\relax ", vgroup_width(left), vgroup_width(right));
    put_vgroup(b, right);
}


/*
 * A line between two rows of a table: a row of its own with a box for each
 * column.  above and below are the weights of the bottom side of the cells
 * above and the top side of the cells below, and va and vb the weights of the
 * vertical lines at the left edge, between the columns and at the right edge
 * (n + 1 of each) of the row above and the row below.
 */
static void
put_rules(Buf *b, const int *above, const int *below, const int *va,
          const int *vb, const int *w, int n)
{
    int hmax = BW_NONE;
    int dbl = 0;

    for (int k = 0; k < n; k++) {
        int h = above[k] > below[k] ? above[k] : below[k];

        hmax = h > hmax ? h : hmax;
        dbl |= h == BW_DOUBLE;
    }

    if (hmax == BW_NONE) {
        return;
    }

    for (int k = 0; k < n; k++) {
        int h = above[k] > below[k] ? above[k] : below[k];
        int left = k == 0 ? (va[0] > vb[0] ? va[0] : vb[0]) : BW_NONE;
        int right = va[k + 1] > vb[k + 1] ? va[k + 1] : vb[k + 1];
        Buf cw = {NULL, 0, 0};

        put_cellw(&cw, w[k], 0);
        buf_puts(b, k == 0 ? "\\omit" : " & \\omit");
        buf_puts(b, "\\vbox to ");
        buf_puts(b, dbl != 0 ? vgroup_width(BW_DOUBLE) :
                     hmax == BW_WIDE ? BORDER_WIDE_PT : "\\arrayrulewidth");
        buf_puts(b, "{");

        if (h == BW_NONE) {
            buf_puts(b, "\\hrule height0pt width");
            buf_puts(b, cw.p);
        } else if (h == BW_WIDE) {
            buf_puts(b, "\\hrule height" BORDER_WIDE_PT " width");
            buf_puts(b, cw.p);
        } else {
            for (int r = 0; r < (h == BW_DOUBLE ? 2 : 1); r++) {
                if (r > 0) {
                    buf_puts(b, "\\vskip\\doublerulesep ");
                }

                buf_puts(b, "\\hrule height\\arrayrulewidth width");
                buf_puts(b, cw.p);
                buf_putc(b, ' ');
            }
        }

        buf_puts(b, "\\vss}");
        put_cell_vlines(b, w[k], 0, left, right);
        buf_free(&cw);
    }

    buf_puts(b, " \\\\\n");
}


/* The start of a table environment: its name and the column widths. */
static void
put_tabhead(Ctx *c, const char *env, const int *w, int n)
{
    /*
     * A longtable has no alignment argument: with [l] the package sets its
     * left space to zero and the table starts at the page margin whatever
     * LTleft says; without it the LTleft and LTright set before it are kept.
     */
    if (strcmp(env, "tabular") == 0) {
        buf_printf(&c->out, "\\begin{%s}[t]{", env);
    } else {
        buf_printf(&c->out, "\\begin{%s}{", env);
    }

    for (int k = 0; k < n; k++) {
        buf_puts(&c->out, "p{");
        put_in(&c->out, w[k]);
        buf_putc(&c->out, '}');
    }

    buf_puts(&c->out, "}\n");
}


/* The blocks first to last, which have columns: a table with a row each. */
static void
emit_table(Ctx *c, int first, int last)
{
    const Doc *d = c->d;
    const Block *b0 = d->items[first].blk;
    int n = b0->ncols;
    int *w = malloc(usz(n) * sizeof *w);
    int *top = calloc(usz(n), sizeof *top);
    int *bot = calloc(usz(n), sizeof *bot);
    int *none = calloc(usz(n), sizeof *none);
    int *vcur = calloc(usz(n) + 1, sizeof *vcur);
    int *vprev = calloc(usz(n) + 1, sizeof *vprev);
    int *vnone = calloc(usz(n) + 1, sizeof *vnone);
    int pad = COLPAD;
    Fmt f0;
    int bl;
    int avail;
    int sum = 0;
    int borders = 0;

    if (w == NULL || top == NULL || bot == NULL || none == NULL ||
        vcur == NULL || vprev == NULL || vnone == NULL) {
        log_fatal(2, "latex: out of memory making the column tables of a "
                  "table with %d columns", n);
    }

    fmt_resolve(d, first, 0, &f0);
    f0.bl += item_indent(c, first);
    bl = ru(f0.bl);
    avail = c->linew - bl;

    if (c->land != 0) {
        int pw;
        int ph;
        int ml;
        int mr;
        int mt;
        int mb;

        /* The page is turned: the text is as wide as the page is high. */
        page_geometry(d, &pw, &ph, &ml, &mr, &mt, &mb);
        avail = ph - mt - mb - bl;
    }

    avail = avail < 1500 ? 1500 : avail;

    for (int k = 0; k < n; k++) {
        w[k] = ru(b0->cols[k].width);
        sum += w[k];
    }

    if (b0->cols[n - 1].width == 0) {
        w[n - 1] = avail - sum - n * pad;
        w[n - 1] = w[n - 1] < 800 ? 800 : w[n - 1];
        sum += w[n - 1];
    }

    if (sum + n * pad > avail) {
        int room = avail - n * pad;

        room = room < n * 300 ? n * 300 : room;

        for (int k = 0; k < n; k++) {
            w[k] = w[k] * room / sum;
            w[k] = w[k] < 300 ? 300 : w[k];
        }
    }

    for (int i = first; i <= last; i++) {
        for (int k = 0; k < n; k++) {
            borders |= border_sides(&d->items[i].blk->cols[k]) != 0;
        }
    }

    /*
     * A table in a Keep part is set one tabular per row: a longtable cannot
     * be put in a box, and a box of rows can be broken between the rows
     * again when the part is longer than a page.
     */
    if (c->keep == 0) {
        buf_puts(&c->out, "\\par\\setlength{\\LTleft}{");
        put_in(&c->out, bl);
        buf_puts(&c->out, "}\\setlength{\\LTright}{0pt plus 1fil}\n");
        put_tabhead(c, "longtable", w, n);
    } else {
        buf_puts(&c->out, "\\par\n");

        /* The rows of a table take no line number: see lineno_package. */
        if (c->numbered != 0) {
            buf_puts(&c->out, "\\global\\strkeeptabletrue\n");
        }
    }

    for (int i = first; i <= last; i++) {
        const Block *blk = d->items[i].blk;

        if (c->keep > 0) {
            buf_puts(&c->out, "\\noindent\\hspace*{");
            put_in(&c->out, bl);
            buf_puts(&c->out, "}");
            put_tabhead(c, "tabular", w, n);
        }

        int shade = block_shade(blk);
        int gt = block_gutter(blk, "BT");
        int gb = block_gutter(blk, "BB");

        for (int k = 0; k < n; k++) {
            top[k] = border_weight(&blk->cols[k], BS_TOP);
        }

        /*
         * The vertical lines of the row: at the left edge, then one between
         * each two columns (the stronger of the right side of the left column
         * and the left side of the right one) and one at the right edge.
         */
        for (int k = 0; k <= n; k++) {
            int l = k < n ? border_weight(&blk->cols[k], BS_LEFT) : BW_NONE;
            int r = k > 0 ? border_weight(&blk->cols[k - 1], BS_RIGHT) :
                    BW_NONE;

            vcur[k] = l > r ? l : r;
        }

        if (borders != 0) {
            put_rules(&c->out, i == first ? none : bot, top,
                      i == first ? vnone : vprev, vcur, w, n);
        }

        for (int k = 0; k < n; k++) {
            Fmt fk;
            Buf cell = {NULL, 0, 0};
            Buf setup = {NULL, 0, 0};

            fmt_resolve(d, i, k, &fk);
            block_setup(&setup, &fk, 0, 100);
            c->item = i;
            c->col = k + 1;
            c->incell++;
            c->fam = font_family(d, font_number(d, blk, k, 0, c->depth));

            /*
             * A headline that has columns (a side head) shows its tag at
             * the start of the text of its first column, as the screen does.
             */
            c->pretag = fk.tag[0] != '\0' ? fk.tag : NULL;
            emit_text_lines(c, &blk->cols[k].text, &fk, &cell, 1);
            c->pretag = NULL;
            c->incell--;

            if (k > 0) {
                buf_puts(&c->out, " & ");
            }

            if (borders != 0 || shade > 0) {
                /* A shaded cell has no space of its own (@{}): the box that
                   fills it is as wide as the cell and the lines are not
                   painted over. */
                buf_puts(&c->out, "\\multicolumn{1}{");
                buf_puts(&c->out, shade > 0 ? "@{}p{" : "p{");
                put_in(&c->out, shade > 0 ? w[k] + pad : w[k]);
                buf_puts(&c->out, shade > 0 ? "}@{}" : "}");

                if (vcur[k + 1] != BW_NONE || (k == 0 && vcur[0] != BW_NONE)) {
                    /* The lines come after the text, so that the shading
                       does not paint over them. */
                    /* An expression between columns replaces the padding
                       of the right side: it is put back first. */
                    buf_puts(&c->out, shade > 0 ? "@{" : "@{\\hskip\\tabcolsep ");
                    put_cell_vlines(&c->out, w[k], shade,
                                    k == 0 ? vcur[0] : BW_NONE, vcur[k + 1]);
                    buf_puts(&c->out, "}");
                }

                buf_puts(&c->out, "}{");
            }

            if (shade > 0) {
                buf_printf(&c->out, "\\fboxsep=0pt\\colorbox[gray]{%d.%02d}{"
                           "\\hspace*{", (100 - shade) / 100,
                           (100 - shade) % 100);
                put_in(&c->out, pad / 2);
                buf_puts(&c->out, "}\\parbox[t]{");
                put_in(&c->out, w[k]);
                buf_puts(&c->out, "}");
            }

            buf_puts(&c->out, "{");
            buf_puts(&c->out, setup.p != NULL ? setup.p : "");

            if (gt > 0) {
                /* The Top Gutter: room inside the border above the text. */
                buf_puts(&c->out, "\\rule{0pt}{\\dimexpr\\ht\\strutbox+");
                put_in(&c->out, gt);
                buf_puts(&c->out, "\\relax}");
            }

            buf_puts(&c->out, cell.p != NULL ? cell.p : "");

            if (gb > 0) {
                buf_puts(&c->out, "\\rule[-\\dimexpr\\dp\\strutbox+");
                put_in(&c->out, gb);
                buf_puts(&c->out, "\\relax]{0pt}{\\dimexpr\\dp\\strutbox+");
                put_in(&c->out, gb);
                buf_puts(&c->out, "\\relax}");
            }

            buf_puts(&c->out, "}");

            if (shade > 0) {
                buf_puts(&c->out, "\\hspace*{");
                put_in(&c->out, pad / 2);
                buf_puts(&c->out, "}}");
            }

            if (borders != 0 || shade > 0) {
                buf_puts(&c->out, "}");
            }

            c->col = 0;
            buf_free(&cell);
            buf_free(&setup);
            bot[k] = border_weight(&blk->cols[k], BS_BOTTOM);
        }

        /*
         * The last row of a table that has lines is followed by the row that
         * closes it.  The star keeps the two together: without it the
         * closing line can be alone at the top of the next page, a page with
         * a line and nothing else.  The same star follows the first row of a
         * table of two rows or more, so that a header row is never alone at
         * the foot of a page.
         */
        buf_puts(&c->out, c->keep == 0 &&
                 ((borders != 0 && i == last) ||
                  (i == first && last > first)) ?
                 " \\\\*\n" : " \\\\\n");
        memcpy(vprev, vcur, (usz(n) + 1) * sizeof *vcur);

        if (c->keep > 0) {
            if (borders != 0 && i == last) {
                put_rules(&c->out, bot, none, vcur, vnone, w, n);
            }

            buf_puts(&c->out, "\\end{tabular}\\par\\nointerlineskip"
                     "\\vskip0pt\n");
        }
    }

    if (c->keep == 0) {
        if (borders != 0) {
            put_rules(&c->out, bot, none, vcur, vnone, w, n);
        }

        buf_puts(&c->out, "\\end{longtable}\n");
    }

    flush_deferred(c, &c->out);

    if (block_bordered(d->items[last].blk) != 0) {
        /* Connect Borders No: the spacing of the blocks is 1.5; Yes: at most
           1.00, so that the borders close up (Reference p. 4-36). */
        int sp = fmt_space(d, c->depth, "ST", 100);

        put_gap(c, &c->out, block_connect(d->items[last].blk) != 0 ?
                                (sp > 100 ? 100 : sp) : 150);
    } else {
        put_gap(c, &c->out, fmt_space(d, c->depth, "ST", 100));
    }

    free(w);
    free(top);
    free(bot);
    free(none);
    free(vcur);
    free(vprev);
    free(vnone);
}


/* Keep: the blocks between a Start and an End are set on one page. */

/*
 * The parts of the document that are kept together.  Tested with the sample
 * documents (docs/MARKERS.txt): a Start opens a region, a Start inside a
 * region is ignored, the first End closes it, an End with no open region is
 * ignored and a Start with no End keeps to the end of the document.  The
 * region is made of whole blocks (and of whole tables), and a section
 * boundary breaks it into one part for each section.  Part i is the items
 * from i to end[i]; end[i] is -1 where no part starts.
 */
typedef struct {
    int *end;
    int *floating;
} KeepPlan;


/* What a Keep marker is: 0 Start (Fixed), 1 Start (Floating), 2 End. */
static int
keep_kind(const Tok *k)
{
    MPayload p;
    const char *v;
    int kind = 0;

    mp_parse(&p, marker_info(4), tok_mdata(k), tok_mlen(k));
    v = mp_get(&p, "1");

    if (v != NULL) {
        kind = atoi(v);
    }

    mp_free(&p);

    return kind >= 0 && kind <= 2 ? kind : 0;
}


/* Tell whether the text has a marker of the type, or any marker if -1. */
static int
text_has_marker(const Text *t, int type)
{
    for (int i = 0; i < t->n; i++) {
        if (t->v[i].kind == TK_MARKER &&
            (type < 0 || tok_mtype(&t->v[i]) == type)) {
            return 1;
        }
    }

    return 0;
}


/* The first and last item of the table (or single block) of each block. */
static void
group_bounds(const Doc *d, int *gf, int *gl)
{
    int i = 0;

    while (i < d->nitems) {
        int last = i;

        if (d->items[i].type == IT_BLOCK) {
            const Block *blk = d->items[i].blk;

            while (blk->ncols > 1 && last + 1 < d->nitems &&
                   d->items[last + 1].type == IT_BLOCK &&
                   same_columns(blk, d->items[last + 1].blk) != 0) {
                last++;
            }
        }

        for (int j = i; j <= last; j++) {
            gf[j] = i;
            gl[j] = last;
        }

        i = last + 1;
    }
}


/*
 * Tell whether the items a to b can be set in a box: a page break or an
 * orientation marker cannot be, and the text of a title page is not.
 */
static int
keep_boxable(const Doc *d, int a, int b)
{
    int blocks = 0;

    for (int i = a; i <= b; i++) {
        const Item *it = &d->items[i];

        if (it->type != IT_BLOCK) {
            continue;
        }

        for (int k = 0; k < it->blk->ncols; k++) {
            const Text *t = &it->blk->cols[k].text;

            if (text_has_marker(t, 23) != 0 || text_has_marker(t, 36) != 0 ||
                text_has_marker(t, 39) != 0) {
                return 0;
            }
        }

        if (block_only_keep(it->blk) == 0) {
            blocks++;
        }
    }

    return blocks > 0;
}


/* Make the plan; NULL when the document has no Keep marker. */
static KeepPlan *
keep_plan(const Doc *d)
{
    KeepPlan *kp;
    int *gf;
    int *gl;
    int any = 0;
    int open = 0;
    int start = 0;
    int fl = 0;
    int done = -1;
    int n = d->nitems;

    for (int i = 0; i < n && any == 0; i++) {
        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        for (int k = 0; k < d->items[i].blk->ncols; k++) {
            any |= text_has_marker(&d->items[i].blk->cols[k].text, 4);
        }
    }

    if (any == 0) {
        return NULL;
    }

    kp = malloc(sizeof *kp);

    if (kp != NULL) {
        kp->end = malloc((usz(n) + 1) * sizeof *kp->end);
        kp->floating = calloc(usz(n) + 1, sizeof *kp->floating);
    }

    gf = malloc((usz(n) + 1) * sizeof *gf);
    gl = malloc((usz(n) + 1) * sizeof *gl);

    if (kp == NULL || kp->end == NULL || kp->floating == NULL ||
        gf == NULL || gl == NULL) {
        log_fatal(2, "latex: out of memory making the tables of a block of "
                  "%d columns", n);
    }

    for (int i = 0; i <= n; i++) {
        kp->end[i] = -1;
    }

    group_bounds(d, gf, gl);

    for (int i = 0; i < n; i++) {
        const Item *it = &d->items[i];

        if (it->type == IT_SECT && open != 0) {
            /* A new section: the part so far ends, another one begins. */
            if (i - 1 > done && keep_boxable(d, start, i - 1) != 0) {
                kp->end[start] = i - 1;
                kp->floating[start] = fl;
            }

            done = i - 1;
            start = i;
        }

        if (it->type != IT_BLOCK) {
            continue;
        }

        for (int k = 0; k < it->blk->ncols; k++) {
            const Text *t = &it->blk->cols[k].text;

            for (int j = 0; j < t->n; j++) {
                int kind;

                if (t->v[j].kind != TK_MARKER || tok_mtype(&t->v[j]) != 4) {
                    continue;
                }

                kind = keep_kind(&t->v[j]);

                if (kind == 2 && open != 0) {
                    if (keep_boxable(d, start, gl[i]) != 0) {
                        kp->end[start] = gl[i];
                        kp->floating[start] = fl;
                    }

                    done = gl[i];
                    open = 0;
                } else if (kind != 2 && open == 0) {
                    open = 1;
                    fl = kind == 1;
                    start = gf[i] > done + 1 ? gf[i] : done + 1;
                }
            }
        }
    }

    if (open != 0 && n - 1 > done && keep_boxable(d, start, n - 1) != 0) {
        kp->end[start] = n - 1;
        kp->floating[start] = fl;
    }

    free(gf);
    free(gl);

    return kp;
}


static void
keep_free(KeepPlan *kp)
{
    if (kp != NULL) {
        free(kp->end);
        free(kp->floating);
        free(kp);
    }
}


/* Start a part: its text is set in a box that is measured at the end. */
static void
keep_open(Ctx *c)
{
    buf_puts(&c->out, "\\par\\begingroup\\setbox\\strkeepbox\\vbox{"
             "\\hsize=\\linewidth\n");

    if (c->numbered != 0) {
        buf_puts(&c->out, "\\global\\strkeeptablefalse\n");
    }

    c->keep++;
    c->hold++;
}


/* End a part: place the box on one page, or break it when too long. */
static void
keep_close(Ctx *c, int floating)
{
    c->hold--;
    c->keep--;
    buf_puts(&c->out, "\\par}\n");
    buf_puts(&c->out, floating != 0 ? "\\strkeepfloat" : "\\strkeepplace");
    buf_puts(&c->out, "\\endgroup\n");
    flush_deferred(c, &c->out);
}


/*
 * Line numbering.  The settings are the global F records ZM (the mode: 0 off,
 * 1 pagewise, 2 running, 3 running that starts again at each section of the
 * first level), ZE (a number on every n-th line), ZS (the side: 0 left,
 * 1 right, 2 outer, 3 inner), ZZ (the size of the number), ZY (its font), ZF
 * (its format), ZD (its distance from the text in hundredths of an inch) and
 * ZQ (1: the lines of a displayed equation are numbered too).  A document
 * without ZM, or with ZM of 0, is written as if the feature did not exist.
 */
typedef struct {
    int mode;
    int every;
    int side;
    int size;
    int font;
    int format;
    int dist;
    int equations;
} LineNum;


/* One setting of the line numbering, kept inside the range of its choices. */
static int
lineno_value(const Doc *d, const char *code, int dflt, int max)
{
    const char *v = doc_fmt_get(d, 0, code);
    int n = v != NULL && v[0] != '\0' ? atoi(v) : dflt;

    return n < 0 || n > max ? dflt : n;
}


/* Read the settings; returns 1 when the lines are to be numbered. */
static int
lineno_read(const Doc *d, LineNum *ln)
{
    ln->mode = lineno_value(d, "ZM", 0, 3);
    ln->every = lineno_value(d, "ZE", 1, 99);
    ln->side = lineno_value(d, "ZS", 0, 3);
    ln->size = lineno_value(d, "ZZ", 0, 2);
    ln->font = lineno_value(d, "ZY", 0, 2);
    ln->format = lineno_value(d, "ZF", 0, 2);
    ln->dist = lineno_value(d, "ZD", 15, 200);
    ln->equations = lineno_value(d, "ZQ", 0, 1);

    if (ln->every < 1) {
        ln->every = 1;
    }

    return ln->mode != 0 ? 1 : 0;
}


/*
 * The personal hyphenation list of the user (the file hyphenation of the
 * settings folder): where a word may be broken, and a word with no hyphen,
 * which is never broken.  TeX reads them as exceptions to its patterns.
 */
static void
hyphenation_list(Buf *b)
{
    char *t = hyph_tex();

    if (t != NULL) {
        buf_puts(b, "% The personal hyphenation list\n");
        buf_puts(b, t);
        free(t);
    }
}


/*
 * The package and its settings, before \begin{document}.  The options are
 * those of lineno: pagewise or running numbers, the side (switch puts the
 * number on the outer margin and switch* on the inner one), the distance
 * from the text and mathlines for the lines of a displayed equation.
 */
static void
lineno_package(Ctx *c, Buf *b)
{
    static const char *const sizes[] = {"tiny", "scriptsize", "footnotesize"};
    static const char *const fonts[] = {"sffamily", "rmfamily", "ttfamily"};
    static const char *const formats[] = {"arabic", "roman", "Roman"};
    LineNum ln;

    if (lineno_read(c->d, &ln) == 0) {
        return;
    }

    c->numbered = 1;

    /*
     * Pagewise numbers (and the switch of the side, which is made of them)
     * are known only after a first run, so the Printer must run LaTeX twice.
     */
    if (ln.mode == 1 || ln.side == 2 || ln.side == 3) {
        c->o->pagerefs = 1;
    }

    buf_puts(b, "\\usepackage[");

    if (ln.side == 2 || ln.side == 3) {
        buf_puts(b, ln.side == 2 ? "switch" : "switch*");

        if (ln.mode == 1) {
            buf_puts(b, ",pagewise");
        }
    } else {
        /* The option left is the default and, with pagewise, is wrong. */
        buf_puts(b, ln.mode == 1 ? "pagewise" : "running");

        if (ln.side == 1) {
            buf_puts(b, ",right");
        }
    }

    if (ln.equations != 0) {
        buf_puts(b, ",mathlines");
    }

    buf_printf(b, ",sep=%d.%02din]{lineno}\n", ln.dist / 100, ln.dist % 100);
    buf_printf(b, "\\def\\linenumberfont{\\normalfont\\%s\\%s}\n",
               sizes[ln.size], fonts[ln.font]);

    if (ln.format != 0) {
        /* A roman numeral shows 1 to 3999: above that the number is arabic. */
        buf_printf(b, "\\renewcommand{\\thelinenumber}{\\ifnum"
                      "\\value{linenumber}>3999 \\arabic{linenumber}"
                      "\\else\\%s{linenumber}\\fi}\n",
                   formats[ln.format]);
    }

    /*
     * A Keep block is set in a box, and lineno numbers no line that is inside
     * a box.  Placing the lines of the box on the page, not the box itself,
     * lets them be numbered, and it keeps them together as before because
     * the space is asked for first.  A part that holds a table is placed as
     * the box when it fits on a page, because its rows are not text lines
     * and take no number (the switch is set by the table, see emit_table);
     * a part longer than a page is always unboxed so that it can break.
     */
    buf_puts(b, "\\newif\\ifstrkeeptable\n");
    buf_puts(b, "\\renewcommand{\\strkeepplace}{\\par\\ifdim\\dimexpr\\ht"
                "\\strkeepbox\n +\\dp\\strkeepbox\\relax>\\textheight\n "
                "\\Needspace*{\\textheight}\\unvbox\\strkeepbox\\else\n "
                "\\Needspace*{\\dimexpr\\ht\\strkeepbox+\\dp\\strkeepbox"
                "\\relax}\n \\ifstrkeeptable\\box\\strkeepbox\\else"
                "\\unvbox\\strkeepbox\\fi\\fi}\n");

    if (ln.mode == 3) {
        buf_puts(b, "\\let\\strheadnumbered\\strhead\n"
                    "\\renewcommand{\\strhead}[3]{\\ifnum#1=1 "
                    "\\resetlinenumber\\fi\\strheadnumbered{#1}{#2}{#3}}\n");
    }
}


/* The start of the numbering, after \begin{document}. */
static void
lineno_start(Ctx *c, Buf *b)
{
    LineNum ln;

    if (lineno_read(c->d, &ln) == 0) {
        return;
    }

    buf_puts(b, "\\linenumbers\n");

    if (ln.every > 1) {
        buf_printf(b, "\\modulolinenumbers[%d]\n", ln.every);
    }
}


/*
 * The comment at the top of an exported file: what made it, from which
 * document and when, how to make the pages and what is needed to do that.
 */
static void
write_export_header(Ctx *c, Buf *b)
{
    LineNum ln_head;

    const LatexOpts *o = c->o;
    const char *tex = o->export_tex != NULL ? o->export_tex : "FILE.tex";
    char shown[512];

    buf_puts(b, "% LaTeX file exported by Strata.\n");
    comment_text(o->export_name, shown, sizeof shown);
    buf_printf(b, "%% Source document: %s\n", shown);

    if (o->export_date != NULL) {
        buf_printf(b, "%% Exported on: %s\n", o->export_date);
    }

    buf_puts(b, "%\n");
    buf_puts(b, "% To make the pages, run LaTeX on this file twice, so that "
                "the page numbers\n");
    buf_puts(b, "% of the references settle:\n");
    buf_printf(b, "%%     pdflatex %s\n", tex);
    buf_puts(b, "% The DVI route works too: latex, then dvipdfmx if a PDF is "
                "wanted.\n");
    buf_puts(b, "%\n");
    buf_puts(b, "% The packages needed are all in a standard TeX Live "
                "installation:\n");
    buf_puts(b, "%     geometry fontenc inputenc lmodern textcomp pmboxdraw "
                "amsmath amssymb\n");
    buf_puts(b, "%     ulem graphicx adjustbox xcolor tikz longtable setspace "
                "needspace atbegshi xspace\n");
    buf_puts(b, "%     pdflscape (lscape for the DVI route)\n");

    if (lineno_read(c->d, &ln_head) != 0) {
        buf_puts(b, "%     lineno\n");
    }

    buf_puts(b, "%\n");

    if (o->picdir != NULL) {
        buf_printf(b, "%% The pictures are in the folder %s next to this "
                      "file.\n", o->picref);
        buf_puts(b, "% Keep the folder with the file.\n");
    }

    buf_puts(b, "%\n");
    buf_puts(b, "% This file is made again each time the document is "
                "exported.  A change that\n");
    buf_puts(b, "% you make here is lost then: make it in the document.\n");
    buf_puts(b, "%\n");
}


/* The end of an exported file: the pictures that it uses. */
static void
write_export_tail(Ctx *c, Buf *b)
{
    if (c->o->export_name == NULL) {
        return;
    }

    buf_puts(b, "% Pictures used by this file (the name in the file = the name"
                " in the document):\n");

    if (c->pics.n == 0) {
        buf_puts(b, "%     none\n");
    } else {
        buf_puts(b, c->pics.p);
    }
}


/* The preamble of the file. */
/*
 * The part before \documentclass: is the engine writing a PDF (pdflatex) or
 * a DVI file (latex)?  The options of the packages that depend on it are
 * passed here, so that the same file works in pdflatex, in latex followed by
 * dvipdfmx, and in latex alone.
 */
static const char *const preamble_0[] = {
    "\\newif\\ifstrpdf \\strpdffalse",
    "\\ifdefined\\pdfoutput\\ifnum\\pdfoutput>0 \\strpdftrue\\fi\\fi",
    "\\ifstrpdf\\else",
    " \\PassOptionsToPackage{dvipdfmx}{graphicx}",
    " \\PassOptionsToPackage{dvipdfmx}{xcolor}",
    " \\PassOptionsToPackage{dvipdfmx}{geometry}",
    " \\PassOptionsToPackage{dvipdfmx}{lscape}",
    " \\def\\pgfsysdriver{pgfsys-dvipdfmx.def}",
    "\\fi",
    NULL};

/* The fixed part after the page geometry, line by line. */
static const char *const preamble_a[] = {
    "\\usepackage[T1]{fontenc}",
    "\\usepackage[utf8]{inputenc}",
    "\\usepackage{lmodern}",
    "\\usepackage{textcomp}",
    "\\usepackage{pmboxdraw}",
    "\\usepackage{amsmath,amssymb}",
    "\\usepackage[normalem]{ulem}",
    "\\usepackage{graphicx}",
    "\\usepackage{adjustbox,xcolor,tikz,longtable,setspace,needspace}",
    "\\usepackage{atbegshi,xspace}",
    "\\newif\\ifstrland \\strlandfalse",
    "\\ifstrpdf",
    " \\usepackage{pdflscape}",
    "\\else",
    " \\usepackage{lscape}",
    " \\AtBeginShipout{\\ifstrland\\AtBeginShipoutUpperLeft{%",
    "  \\special{pdf: put @thispage <</Rotate 90>>}}\\fi}",
    "\\fi",
    "\\newcommand{\\strlandbegin}{\\clearpage\\strlandtrue",
    " \\begin{landscape}}",
    "\\newcommand{\\strlandend}{\\end{landscape}\\clearpage\\strlandfalse}",
    "\\newcommand{\\strmiss}[1]{{\\ttfamily\\scriptsize [U+#1]}}",
    "\\newcommand{\\strsp}{\\fontdimen2\\font}",
    "\\setlength{\\parindent}{0pt}\\setlength{\\parskip}{0pt}",
    "\\setlength{\\emergencystretch}{3em}\\tolerance=1000",
    "\\hbadness=10000 \\vbadness=10000 \\hfuzz=2pt \\raggedbottom",
    "\\newbox\\strbox \\newdimen\\strx \\newdimen\\strw \\newdimen\\strt",
    NULL};

/*
 * The text \strata: the word Strata with the joined crossbar of its Ts.
 * The stems of the Ts are rules with the stroke of the small letters r and
 * a; the numbers of the design are at the top of the macro.  The S is the
 * drawn S of the logo \stratalogo (logo/stratalogo.def), made once as a
 * box and scaled to the font size at every use.
 */
static const char *const preamble_strata[] = {
    "\\newlength{\\strlU}",
    "\\newlength{\\strlS}",
    "\\newlength{\\strlW}",
    "\\newlength{\\strlC}",
    "\\newlength{\\strlB}",
    "\\newlength{\\strlI}",
    "\\newsavebox{\\strlbT}",
    "\\newsavebox{\\strlbR}",
    "\\newsavebox{\\strlbA}",
    "\\newsavebox{\\strlbS}",
    "\\newif\\ifstrlSdrawn",
    "\\makeatletter",
    "% The numbers of the logo are all here, as fractions of the font size U.",
    "%",
    "% The small letters r and a are \\strlkSmall of U.  In Helvetica Bold the",
    "% stem of the letters I, R and T is 0.149 of the size of the letter (this",
    "% was measured on the drawing of the font), so the small letters have a",
    "% stroke of 0.092 U.  The stems of the two Ts have the stroke of the",
    "% letter I of the same font at the size of the small letters, and are as",
    "% high as a capital T; each is a rule of exactly that stroke, with the",
    "% flat ends of the font.  (A stem drawn with the glyph of the letter I",
    "% looks the same, but puts an I in the text of the PDF, where a search",
    "% for the word would not find it.)  \\strlkHs widens (above 1) or",
    "% narrows (below 1) the stems; 1 is the stroke of the font.",
    "\\newcommand{\\strlkSmall}{0.618}",
    "\\newcommand{\\strlkHs}{1}",
    "% The height of a capital T in the drawing of the font is 0.726 U (the",
    "% metrics of the font say 0.708 U).  The crossbar is as high as the",
    "% drawing.  The stems end half a bar below its top, hidden in the bar, so",
    "% that no nub shows above it when the page is drawn on a screen.",
    "\\newcommand{\\strlkCap}{0.726}",
    "% The thickness of the crossbar, as a fraction of the stroke of a stem.",
    "\\newcommand{\\strlkBar}{0.90}",
    "% The space after the S before the crossbar begins, and the arm of the",
    "% bar to the left of the first stem.",
    "\\newcommand{\\strlkSB}{0.063}",
    "\\newcommand{\\strlkArm}{0.206}",
    "% The space from the first stem to the r, from the r and the a (which are",
    "% one box) to the second stem, and from the second stem to the last a.",
    "\\newcommand{\\strlkTR}{0.062}",
    "\\newcommand{\\strlkRT}{0.064}",
    "\\newcommand{\\strlkTA}{0.050}",
    "% The end of the bar after the last a.",
    "\\newcommand{\\strlkEnd}{0.088}",
    "% The S is the S of the logo: one filled outline of a stroke with bends,",
    "% as in \\stratalogo, but with the top arm cut at the width of the body",
    "% so that it ends above the bottom arm.  It is drawn once, at the size",
    "% U = 10 pt (the unit of the drawing is 0.1 pt, so U is 100 units), into",
    "% the box \\strlbS and scaled at every use.  In units of U: the stroke is",
    "% 0.092 (the stroke of the stems of the Ts), the outer radius of a bend",
    "% is two strokes and the inner one stroke, the height is 0.726 (a capital",
    "% letter of the font) and the width is 46/43.5 of the height, the",
    "% proportions of the S of the logo.",
    "\\newcommand{\\strlmakeS}{%",
    " \\global\\setbox\\strlbS\\hbox{%",
    "  \\begin{tikzpicture}[baseline=0pt,x=0.1pt,y=0.1pt]",
    "  \\fill (76.77,63.4) -- (18.4,63.4)",
    "   arc[start angle=90,end angle=180,radius=9.2] -- (9.2,50.1)",
    "   arc[start angle=180,end angle=270,radius=9.2] -- (18.4,40.9)",
    "   -- (58.37,40.9) arc[start angle=90,end angle=0,radius=18.4]",
    "   -- (76.77,18.4) arc[start angle=0,end angle=-90,radius=18.4]",
    "   -- (0,0) -- (0,9.2) -- (58.37,9.2)",
    "   arc[start angle=-90,end angle=0,radius=9.2] -- (67.57,22.5)",
    "   arc[start angle=0,end angle=90,radius=9.2] -- (58.37,31.7)",
    "   -- (18.4,31.7) arc[start angle=270,end angle=180,radius=18.4]",
    "   -- (0,54.2) arc[start angle=180,end angle=90,radius=18.4]",
    "   -- (76.77,72.6) -- cycle;",
    " \\end{tikzpicture}}%",
    " \\global\\strlSdrawntrue}",
    "% The S at the size U of the text: the box scaled, and in a PDF an",
    "% invisible letter S under it, so that the text of the page keeps it.",
    "\\newcommand{\\strlputS}{%",
    " \\ifstrlSdrawn\\else\\strlmakeS\\fi",
    " \\pgfmathsetmacro{\\strlf}{\\strlU/10}%",
    " \\ifnum\\ifx\\pdfoutput\\undefined0\\else\\pdfoutput\\fi>0",
    "  \\rlap{\\pdfliteral{3 Tr}S\\pdfliteral{0 Tr}}\\fi",
    " \\scalebox{\\strlf}{\\usebox{\\strlbS}}}",
    "\\newcommand{\\strlogo}{\\leavevmode\\begingroup",
    " \\setlength{\\strlU}{\\f@size pt}%",
    " \\fontencoding{T1}\\fontfamily{phv}\\fontseries{b}\\fontshape{n}%",
    " \\selectfont",
    " \\setlength{\\strlC}{\\dimexpr\\strlkCap\\strlU\\relax}%",
    " \\setlength{\\strlS}{\\dimexpr\\strlkSmall\\strlU\\relax}%",
    " \\sbox{\\strlbR}{\\fontsize{\\the\\strlS}{0pt}\\selectfont RA}%",
    " \\sbox{\\strlbA}{\\fontsize{\\the\\strlS}{0pt}\\selectfont A}%",
    " \\setlength{\\strlI}{\\dimexpr0.149\\strlS\\relax}%",
    " \\setlength{\\strlW}{\\dimexpr\\strlkHs\\strlI\\relax}%",
    " \\sbox{\\strlbT}{\\rule[0pt]{\\strlW}%",
    "  {\\dimexpr\\strlC-\\strlkBar\\strlW/2\\relax}}%",
    " \\setlength{\\strlB}{\\dimexpr\\strlkArm\\strlU+\\strlW%",
    "  +\\strlkTR\\strlU+\\wd\\strlbR+\\strlkRT\\strlU+\\strlW%",
    "  +\\strlkTA\\strlU+\\wd\\strlbA+\\strlkEnd\\strlU\\relax}%",
    " \\strlputS\\kern\\strlkSB\\strlU\\hbox to \\strlB{%",
    "  \\rlap{\\raisebox{\\dimexpr\\strlC-\\strlkBar\\strlW\\relax}%",
    "   {\\rule{\\strlB}{\\strlkBar\\strlW}}}%",
    "  \\kern\\strlkArm\\strlU\\usebox{\\strlbT}\\kern\\strlkTR\\strlU",
    "  \\usebox{\\strlbR}\\kern\\strlkRT\\strlU\\usebox{\\strlbT}%",
    "  \\kern\\strlkTA\\strlU\\usebox{\\strlbA}\\hss}\\endgroup}",
    "\\makeatother",
    "\\newcommand{\\strata}{\\strlogo\\xspace}",
    NULL};


/*
 * The print in black and white: the option gray of xcolor does not change a
 * picture that is a file, so a rectangle that is drawn over each picture with
 * the blend mode Saturation (and a grey fill, so no saturation) takes its
 * colours away and keeps its lightness.  Only the PDF route can do it (the
 * route by DVI has no \\pdfliteral).
 */
static const char *const preamble_grey_pictures[] = {
    "\\ifdefined\\pdfliteral",
    "\\newsavebox{\\strgbox}",
    "\\pdfpageresources\\expandafter{\\the\\pdfpageresources",
    " /ExtGState << /StrGrey << /BM /Saturation >> >>}",
    "\\makeatletter",
    "\\renewcommand{\\strpic}[2]{\\sbox\\strgbox{\\adjustbox{max width=\\linewidth,",
    " max height=0.85\\textheight}{\\includegraphics[#1]{#2}}}%",
    " \\usebox\\strgbox\\hskip-\\wd\\strgbox",
    " \\pdfliteral{q /StrGrey gs 0.5 g -1",
    "  \\strip@pt\\dimexpr-\\dp\\strgbox*7200/7227-1pt\\relax\\space",
    "  \\strip@pt\\dimexpr\\wd\\strgbox*7200/7227+2pt\\relax\\space",
    "  \\strip@pt\\dimexpr(\\ht\\strgbox+\\dp\\strgbox)*7200/7227+2pt\\relax",
    "  \\space re f Q}%",
    " \\hskip\\wd\\strgbox}",
    "\\makeatother",
    "\\fi",
    NULL};


static const char *const preamble_b[] = {
    "\\newcommand{\\strlong}[1]{\\fcolorbox{black!40}{black!5}{\\ttfamily",
    " \\footnotesize #1}}",
    "\\newcommand{\\strlongbr}[1]{{\\ttfamily\\footnotesize [#1]}}",
    "\\newcommand{\\strmissing}[1]{\\adjustbox{max width=\\linewidth}{%",
    " \\fcolorbox{red!70!black}{red!8}{\\ttfamily\\footnotesize #1}}}",
    "\\newcommand{\\strpbodd}{\\clearpage\\ifodd\\value{page}\\else",
    " \\mbox{}\\clearpage\\fi}",
    "\\newcommand{\\strpbeven}{\\clearpage\\ifodd\\value{page}\\mbox{}",
    " \\clearpage\\fi}",
    "\\newcommand{\\strpic}[2]{\\adjustbox{max width=\\linewidth,",
    " max height=0.85\\textheight}{\\includegraphics[#1]{#2}}}",
    "\\newdimen\\strem",
    "\\newcommand{\\strkey}[5]{\\begingroup \\strem=1em",
    " \\setbox\\strbox\\hbox{\\textcolor[gray]{#1}{#4\\upshape #5}}%",
    " \\scalebox{#3}{\\tikz[baseline=0pt]{%",
    "  \\path[draw=black,line width=0.4pt,",
    "   rounded corners=\\the\\dimexpr0.2\\strem\\relax,",
    "   fill={rgb,1:red,#2;green,#2;blue,#2}]",
    "   (0,{-\\the\\dimexpr0.333\\strem\\relax}) rectangle",
    "   ({\\the\\dimexpr\\wd\\strbox+0.62\\strem\\relax},",
    "   {\\the\\dimexpr1.0\\strem\\relax});",
    "  \\node[anchor=base west,inner sep=0pt]",
    "   at ({\\the\\dimexpr0.31\\strem\\relax},0) {\\box\\strbox};}}%",
    " \\endgroup}",
    "\\newcommand{\\strsmile}[2]{{\\strem=1em",
    " \\tikz[baseline={-0.3\\strem},line width=0.3pt]{%",
    "  \\draw[fill=#1] (0,0) circle (0.42\\strem);",
    "  \\fill[#2] (-0.15\\strem,0.1\\strem) circle (0.05\\strem)",
    "   (0.15\\strem,0.1\\strem) circle (0.05\\strem);",
    "  \\draw[#2] (-0.2\\strem,-0.08\\strem) .. controls",
    "   (-0.1\\strem,-0.25\\strem) and (0.1\\strem,-0.25\\strem) ..",
    "   (0.2\\strem,-0.08\\strem);}}}",
    "\\newcommand{\\strstart}[1]{\\noindent\\hbox to #1{}\\strx=#1\\relax}",
    "\\newcommand{\\strfillbox}[2]{\\ifdim#1>0pt \\hbox to #1{%",
    " \\ifx\\relax#2\\relax\\hfil\\else\\xleaders\\hbox{#2}\\hfil\\fi}\\fi}",
    "\\newcommand{\\strseg}[1]{\\setbox\\strbox\\hbox{#1}%",
    " \\advance\\strx\\wd\\strbox \\box\\strbox}",
    "\\newcommand{\\strgap}[2]{\\strt=#1\\relax \\strw=\\strt",
    " \\advance\\strw-\\strx \\ifdim\\strw<.3em \\strw=.3em\\fi",
    " \\strfillbox{\\strw}{#2}\\advance\\strx\\strw}",
    "\\newcommand{\\strtab}[4]{\\setbox\\strbox\\hbox{#4}\\strt=#2\\relax",
    " \\ifcase#1\\relax\\strw=\\strt \\advance\\strw-\\strx",
    " \\or\\strw=\\strt \\advance\\strw-\\wd\\strbox \\advance\\strw-\\strx",
    " \\else\\strw=\\strt \\advance\\strw-.5\\wd\\strbox",
    " \\advance\\strw-\\strx\\fi",
    " \\ifdim\\strw<.3em \\strw=.3em\\fi \\strfillbox{\\strw}{#3}%",
    " \\advance\\strx\\strw \\box\\strbox \\advance\\strx\\wd\\strbox}",
    "\\newcommand{\\strti}[1]{\\setbox\\strbox\\hbox{#1}%",
    " \\hangindent=\\dimexpr\\wd\\strbox+\\parindent\\relax \\hangafter=1",
    " \\leavevmode\\box\\strbox}",
    "\\newcommand{\\strhfont}[1]{\\ifcase#1\\relax\\or\\Large\\or\\large\\else",
    " \\normalsize\\fi}",
    "\\newbox\\strkeepbox",
    "\\newcommand{\\strkeepplace}{\\par\\ifdim\\dimexpr\\ht\\strkeepbox",
    " +\\dp\\strkeepbox\\relax>\\textheight",
    " \\Needspace*{\\textheight}\\unvbox\\strkeepbox\\else",
    " \\Needspace*{\\dimexpr\\ht\\strkeepbox+\\dp\\strkeepbox\\relax}",
    " \\box\\strkeepbox\\fi}",
    "\\setlength{\\intextsep}{0pt}\\setlength{\\floatsep}{0pt}",
    "\\setlength{\\textfloatsep}{0pt}",
    "\\newcommand{\\strkeepfloat}{\\par\\ifdim\\dimexpr\\ht\\strkeepbox",
    " +\\dp\\strkeepbox\\relax>0.9\\textheight \\strkeepplace\\else",
    " \\begin{figure}[!ht]\\unvbox\\strkeepbox\\end{figure}\\fi}",
    "\\newdimen\\strwsh",
    "\\newcommand{\\strwsall}[1]{\\penalty0 \\vspace*{#1}\\penalty0 }",
    "\\newcommand{\\strwsfloat}{\\ifdim\\dimexpr\\pagetotal+\\strwsh",
    " \\relax>\\pagegoal \\ifinner \\strwsall{\\strwsh}\\else",
    " \\begin{figure}[!t]\\nointerlineskip\\vbox to \\strwsh{}\\end{figure}\\fi",
    " \\else \\strwsall{\\strwsh}\\fi}",
    "\\newcommand{\\strws}[2]{\\par\\strwsh=#1\\relax",
    " \\ifdim\\strwsh>\\textheight \\strwsh=\\textheight\\fi",
    " \\ifcase#2\\relax\\vspace{\\strwsh}\\or\\strwsall{\\strwsh}\\else",
    " \\strwsfloat\\fi}",
    "\\newcommand{\\strhead}[3]{{\\strhfont{#1}#2\\noindent #3\\par}\\nobreak}",
    "\\newcommand{\\strnotesep}[1]{\\par\\vspace{\\baselineskip}\\noindent",
    " \\rule{#1}{0.4pt}\\par\\vspace{0.5\\baselineskip}}",
    "\\newcommand{\\strnote}[2]{{\\footnotesize\\noindent\\hangindent=1.5em",
    " #1\\ #2\\par}}",
    "\\makeatletter",
    "\\renewcommand{\\@makefnmark}{\\hbox{\\@thefnmark}}",
    "\\renewcommand{\\@makefntext}[1]{\\noindent #1}",
    "\\newcommand{\\strfnmark}[1]{{\\def\\@thefnmark{#1}\\@footnotemark}}",
    "\\newcommand{\\strfntext}[1]{{\\def\\@thefnmark{}\\@footnotetext{#1}}}",
    "\\makeatother",
    NULL};


static void
put_lines(Buf *b, const char *const *lines)
{
    for (int i = 0; lines[i] != NULL; i++) {
        buf_puts(b, lines[i]);
        buf_putc(b, '\n');
    }
}


/* A number of the header, such as PML (the left margin), or dflt. */
static int
header_num(const Doc *d, const char *code, char sub, int dflt)
{
    const char *v = props_get(&d->header, code, sub);

    return v != NULL && v[0] != '\0' ? atoi(v) : dflt;
}


/*
 * The page size and the margins of the document, in thousandths of an inch.
 * Margins that leave no room for text are replaced.
 */
static void
page_geometry(const Doc *d, int *pw, int *ph, int *ml, int *mr, int *mt,
              int *mb)
{
    const char *dp = props_get(&d->header, "DP", 0);
    int n = 0;
    int w = 0;
    int h = 0;

    *pw = 8500;
    *ph = 11000;
    *ml = header_num(d, "PM", 'L', 1000);
    *mr = header_num(d, "PM", 'R', 1000);
    *mt = header_num(d, "PM", 'T', 1000);
    *mb = header_num(d, "PM", 'B', 1000);

    if (dp != NULL && sscanf(dp, "%d,%d,%d", &n, &w, &h) == 3) {
        if (w > 0 && h > 0) {
            *pw = w;
            *ph = h;
        } else if (n == 1) {
            *ph = 14000;
        }
    }

    /* Margins that leave no room for text are replaced. */
    if (*pw - *ml - *mr < 2000) {
        *ml = 1000;
        *mr = 1000;
    }

    if (*ph - *mt - *mb < 2000) {
        *mt = 1000;
        *mb = 1000;
    }
}


/* The labels of a failed print, only in the file of a failed print. */
static const char *const marks_defs[] = {
    "\\newcommand{\\strmarke}[1]{{\\fboxsep=1pt\\colorbox{red}{",
    " \\textcolor{white}{\\normalfont\\sffamily\\bfseries\\footnotesize",
    " E#1}}}}",
    "\\newcommand{\\strmarkw}[1]{{\\fboxsep=1pt\\colorbox{orange}{",
    " \\textcolor{black}{\\normalfont\\sffamily\\bfseries\\footnotesize",
    " W#1}}}}",
    NULL};


/*
 * The banner of a failed print.  It is put on every page when the page is
 * shipped, in the top margin, so that it changes nothing in the layout of
 * the text.  A DVI also gets the special that tells the Viewer.  mt is the
 * top margin in thousandths of an inch.
 */
static void
write_banner(Ctx *c, Buf *b, int mt)
{
    int errors;
    int warnings;
    int depth;

    if (c->o->failed == NULL) {
        return;
    }

    put_lines(b, marks_defs);

    if (c->o->banner == 0) {
        return;
    }

    diag_counts(c->o->failed, &errors, &warnings);
    depth = mt * 3 / 4;

    if (depth > 450) {
        depth = 450;
    } else if (depth < 200) {
        depth = 200;
    }

    buf_printf(b, "\\newcommand{\\strbantext}{\\normalfont\\sffamily"
               "\\bfseries\\LARGE PRINT FAILED - %d ERROR%s - THIS %s IS "
               "INACCURATE}\n", errors, errors == 1 ? "" : "S",
               c->o->preview != 0 ? "PREVIEW" : "PRINT");
    /*
     * The special that tells the Viewer is in the same box as the banner,
     * so that it is on every page, the first included.  A landscape page
     * is a portrait sheet that is turned when it is shown, so its banner
     * is written along the left edge of the sheet.
     */
    buf_printf(b, "\\AtBeginShipout{\\AtBeginShipoutUpperLeft{"
               "\\ifstrpdf\\else\\special{strata:print-failed %d}\\fi\n",
               errors);
    buf_puts(b, "\\color{red}\\ifstrland\n"
                " \\raisebox{-\\paperheight}[0pt][0pt]{\\hspace*{");
    put_in(b, depth);
    buf_puts(b, "}\\rotatebox{90}{\\makebox[\\paperheight]{\\adjustbox{"
                "max width=\\dimexpr\\paperheight-0.4in\\relax}{"
                "\\strbantext}}}}\n\\else\n \\raisebox{-");
    put_in(b, depth);
    buf_puts(b, "}[0pt][0pt]{\\makebox[\\paperwidth]{\\adjustbox{max width="
                "\\dimexpr\\paperwidth-0.4in\\relax}{\\strbantext}}}\n"
                "\\fi}}\n");
}


/* ------------------------------------------------- headers and footers */
/*
 * The header and the footer of the page layouts.  The records P<n>HB<k> and
 * P<n>FB<k> of the document header (n is 1 to 6, none for the Global Page
 * Layout) are followed by the blocks of the text of the header or footer.  A
 * block of columns gives the left, centre and right parts (the justification
 * of a column says which); the markers Page Number and Section are the
 * page number and the headline of the section.  A layout with no record of
 * its own takes the Global Page Layout's header or footer.  Nothing at all is
 * written when no header and no footer of the document has text: such a
 * document is printed as before.
 */
enum {
    HF_GLOBAL,
    HF_TITLE,
    HF_TOC,
    HF_FIRST,
    HF_EVEN,
    HF_ODD,
    HF_INDEX
};


/* Remove the end of line and the blanks at the end of the text of b. */
static void
hf_trim(Buf *b)
{
    while (b->n > 0 && (b->p[b->n - 1] == '\n' || b->p[b->n - 1] == ' ')) {
        b->n--;
        b->p[b->n] = '\0';
    }
}


/*
 * The LaTeX of the three parts of one header or footer, from its blocks (the
 * small document that doc_hf_doc makes).  A part that is empty stays NULL.
 */
static void
hf_render(Ctx *c, int lay, int slot, Doc *hd)
{
    Doc *saved_d = c->d;
    int saved_item = c->item;
    int saved_quiet = c->quiet;
    int saved_fam = c->fam;
    int saved_marks = c->o->posmarks;

    for (int i = 0; i < hd->nitems; i++) {
        const Block *blk;
        Fmt f;

        if (hd->items[i].type != IT_BLOCK) {
            continue;
        }

        blk = hd->items[i].blk;
        fmt_resolve(hd, i, 0, &f);
        f.indent = 0;
        c->d = hd;
        c->item = -1;
        c->quiet = 1;
        c->fam = font_family(saved_d, 3);
        c->in_hf = 1;
        c->o->posmarks = 0; /* a comment would swallow the brace */

        for (int k = 0; k < blk->ncols; k++) {
            const char *j = props_get(&blk->cols[k].props, "CF", 'J');
            Buf t = {NULL, 0, 0};
            int part;
            int lines = 1;

            if (j == NULL) {
                j = props_get(&blk->props, "BF", 'J');
            }

            part = j == NULL ? 0 : atoi(j) == 2 ? 1 : atoi(j) == 1 ? 2 : 0;
            emit_text_lines(c, &blk->cols[k].text, &f, &t, 1);
            hf_trim(&t);

            /* A line with nothing in it is written as an empty box. */
            while (t.n >= 7 && strcmp(t.p + t.n - 7, "\\mbox{}") == 0) {
                t.n -= 7;
                t.p[t.n] = '\0';
                hf_trim(&t);
            }

            if (t.n == 0) {
                buf_free(&t);
                continue;
            }

            for (const char *q = t.p; (q = strstr(q, "\\par\n")) != NULL;
                 q += 5) {
                lines++;
            }

            {
                Buf o = {NULL, 0, 0};
                const char *w = part == 0 ? "l" : part == 1 ? "c" : "r";

                if (lines > 1 && blk->ncols == 1) {
                    buf_printf(&o, "\\parbox[t]{\\textwidth}{\\%s %s}",
                               part == 0 ? "raggedright" :
                               part == 1 ? "centering" : "raggedleft", t.p);
                } else if (lines > 1) {
                    buf_printf(&o, "\\begin{tabular}[t]{@{}%s@{}}", w);

                    for (const char *q = t.p; *q != '\0';) {
                        if (strncmp(q, "\\par\n", 5) == 0) {
                            buf_puts(&o, "\\\\\n");
                            q += 5;
                        } else {
                            buf_putc(&o, *q++);
                        }
                    }

                    buf_puts(&o, "\\end{tabular}");
                } else {
                    buf_puts(&o, t.p);
                }

                if (c->hf_f[lay][slot][part] != NULL) {
                    size_t n1 = strlen(c->hf_f[lay][slot][part]);
                    char *m = realloc(c->hf_f[lay][slot][part], n1 + o.n + 2);

                    if (m != NULL) {
                        m[n1] = ' ';
                        memcpy(m + n1 + 1, o.p, o.n + 1);
                        c->hf_f[lay][slot][part] = m;
                    }
                } else {
                    c->hf_f[lay][slot][part] = strdup(o.p);
                }

                buf_free(&o);
            }

            buf_free(&t);
        }

        c->in_hf = 0;
        break;
    }

    c->o->posmarks = saved_marks;
    c->d = saved_d;
    c->item = saved_item;
    c->quiet = saved_quiet;
    c->fam = saved_fam;
}


/* Read the headers and footers of the layouts out of the document header. */
static void
hf_load(Ctx *c)
{
    for (int lay = 0; lay < 7; lay++) {
        for (int slot = 0; slot < 2; slot++) {
            int own = 0;
            Doc *hd = doc_hf_doc(c->d, lay, slot, &own);

            c->hf_own[lay][slot] = own;

            if (hd != NULL) {
                hf_render(c, lay, slot, hd);
                doc_free(hd);
            }
        }
    }

    for (int lay = 0; lay < 7; lay++) {
        for (int slot = 0; slot < 2; slot++) {
            for (int part = 0; part < 3; part++) {
                if (c->hf_f[lay][slot][part] != NULL) {
                    c->hf_any = 1;
                }
            }
        }
    }
}


static void
hf_free(Ctx *c)
{
    for (int lay = 0; lay < 7; lay++) {
        for (int slot = 0; slot < 2; slot++) {
            for (int part = 0; part < 3; part++) {
                free(c->hf_f[lay][slot][part]);
                c->hf_f[lay][slot][part] = NULL;
            }
        }
    }
}


/* The part of a header or footer that a layout has: its own, or Global's. */
static const char *
hf_part(const Ctx *c, int lay, int slot, int part)
{
    if (c->hf_own[lay][slot] == 0) {
        lay = HF_GLOBAL;
    }

    return c->hf_f[lay][slot][part];
}


/*
 * The indent of the header (slot 0) or of the footer (slot 1) of a layout in
 * thousandths of an inch: the record of the layout (P1HI, P1FI, ...), or the
 * one of the Global Page Layout (PHI, PFI), or none.
 */
static int
hf_indent(const Ctx *c, int lay, int slot)
{
    char code[16];
    const char *v = NULL;

    if (lay > 0) {
        snprintf(code, sizeof code, "P%d%cI", lay, slot == 0 ? 'H' : 'F');
        v = doc_opt_get(c->d, code);
    }

    if (v == NULL) {
        v = doc_opt_get(c->d, slot == 0 ? "PHI" : "PFI");
    }

    return v != NULL && atoi(v) > 0 ? atoi(v) : 0;
}


/*
 * The page styles: strt (title page), strc (table of contents), strf (first
 * page), strx (index) and strn (the others, odd or even by the page number).
 */
static void
hf_styles(Ctx *c, Buf *b)
{
    static const char *const names[] = {"strt", "strc", "strf", "strx"};
    static const int lays[] = {HF_TITLE, HF_TOC, HF_FIRST, HF_INDEX};
    static const char *const fld[] = {"L", "C", "R"};

    buf_puts(b, "\\makeatletter\n"
                "\\def\\strmkboth#1#2{\\ifx\\relax#1\\relax#2\\else#1 #2\\fi}\n"
                "\\def\\strmknum#1#2{\\ifx\\relax#1\\relax#2\\else#1\\fi}\n"
                "\\def\\strmktitle#1#2{#2}\n"
                "\\newcommand{\\strsecmark}{\\expandafter\\strmkboth"
                "\\firstmark}\n"
                "\\newcommand{\\strsecnum}{\\expandafter\\strmknum"
                "\\firstmark}\n"
                "\\newcommand{\\strsectitle}{\\expandafter\\strmktitle"
                "\\firstmark}\n"
                "\\makeatother\n");

    for (int k = 0; k < 5; k++) {
        buf_printf(b, "\\fancypagestyle{%s}{\\fancyhf{}"
                   "\\renewcommand{\\headrulewidth}{0pt}"
                   "\\renewcommand{\\footrulewidth}{0pt}\n",
                   k < 4 ? names[k] : "strn");

        for (int slot = 0; slot < 2; slot++) {
            for (int part = 0; part < 3; part++) {
                const char *v;

                if (k < 4 && !(k == 2 && c->hf_own[HF_FIRST][slot] == 0)) {
                    v = hf_part(c, lays[k], slot, part);

                    if (v != NULL) {
                        buf_printf(b, " \\fancy%s[%s]{%s}\n",
                                   slot == 0 ? "head" : "foot", fld[part], v);
                    }

                    continue;
                }

                {
                    const char *o = hf_part(c, HF_ODD, slot, part);
                    const char *e = hf_part(c, HF_EVEN, slot, part);

                    if (o != NULL || e != NULL) {
                        buf_printf(b, " \\fancy%s[%s]{\\ifodd\\value{page}{}%s"
                                   "\\else{}%s\\fi}\n",
                                   slot == 0 ? "head" : "foot", fld[part],
                                   o != NULL ? o : "", e != NULL ? e : "");
                    }
                }
            }
        }

        /* The indent moves the left end of the header and of the footer in
           from the left margin of the text. */
        for (int slot = 0; slot < 2; slot++) {
            int parity = k == 4 || (k == 2 && c->hf_own[HF_FIRST][slot] == 0);

            for (int e = 0; e < (parity != 0 ? 2 : 1); e++) {
                int ind = hf_indent(c, parity != 0 ? (e == 0 ? HF_EVEN :
                                                       HF_ODD) : lays[k],
                                    slot);

                if (ind > 0) {
                    buf_printf(b, " \\fancy%soffset[L%s]{-",
                               slot == 0 ? "head" : "foot",
                               parity == 0 ? "" : e == 0 ? "E" : "O");
                    put_in(b, ind);
                    buf_puts(b, "}\n");
                }
            }
        }

        buf_puts(b, "}\n");
    }

    buf_puts(b, "\\pagestyle{strn}\n");
}



/* The style of the first page of the body: the First Page Layout. */
static void
hf_first(Ctx *c)
{
    if (c->hf_any != 0 && c->hf_first_done == 0) {
        buf_puts(&c->out, "\\thispagestyle{strf}\n");
    }

    c->hf_first_done = 1;
}


/* A document with no structure has no title page: its page 1 is first. */
static void
hf_first_unstructured(Ctx *c)
{
    if (c->structured == 0) {
        hf_first(c);
    }
}



/* Set the ruler unit from the Ruler of the Global Format settings. */
static void
set_ruler_unit(const Doc *d, int linew)
{
    const char *v = doc_fmt_get(d, 0, "U");
    int u = v != NULL ? atoi(v) : 0;

    unit_limit = linew;
    unit_num = linew > 0 ? linew : 6500;
    unit_den = 80;

    if (u == 2) {
        unit_num = 1000;
        unit_den = 10;
    } else if (u == 3) {
        unit_num = 1000;
        unit_den = 12;
    } else if (u == 4) {
        unit_num = 1000;
        unit_den = 15;
    }
}


/*
 * Number the pages in the style: arabic, roman, Roman, alph or Alph.  A roman
 * numeral shows 1 to 3999: from the page 4000 the number is arabic, as it is
 * for the counters and the tags of the sections.
 */
static void
put_pagenumbering(Buf *b, const char *style)
{
    buf_printf(b, "\\pagenumbering{%s}\n", style);

    if (strcmp(style, "roman") == 0 || strcmp(style, "Roman") == 0) {
        buf_printf(b, "\\renewcommand{\\thepage}{\\ifnum\\value{page}>3999 "
                      "\\arabic{page}\\else\\%s{page}\\fi}\n",
                   style);
    }
}


/*
 * The size of the text in points.  The record PS of the header names it (8,
 * 9, 10, 11, 12, 14, 17 or 20); a document with no record and a page of
 * the size of an envelope (any envelope of the paper list or of the
 * templates, in either orientation, whatever paper entry or Custom size the
 * record DP names) has 14 points, as an envelope needs larger type; any
 * other document has 11.
 */
static int
text_points(const Doc *d)
{
    static const int sizes[] = {8, 9, 10, 11, 12, 14, 17, 20};
    const char *ps = props_get(&d->header, "PS", 0);
    const char *dp = props_get(&d->header, "DP", 0);

    if (ps != NULL) {
        int v = atoi(ps);

        for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
            if (sizes[i] == v) {
                return v;
            }
        }
    }

    if (ps == NULL) {
        int pw;
        int ph;
        int ml;
        int mr;
        int mt;
        int mb;
        int pts;

        page_geometry(d, &pw, &ph, &ml, &mr, &mt, &mb);
        pts = doc_envelope_points(pw, ph);

        if (pts > 0) {
            return pts;
        }

        if (dp != NULL && atoi(dp) >= 12 && atoi(dp) <= 15) {
            return 14;
        }
    }

    return 11;
}


static void
write_preamble(Ctx *c, Buf *b)
{
    const Doc *d = c->d;
    int pw;
    int ph;
    int ml;
    int mr;
    int mt;
    int mb;
    const char *pp = props_get(&d->header, "PP", 0);
    const char *style = "arabic";

    page_geometry(d, &pw, &ph, &ml, &mr, &mt, &mb);
    hf_load(c);

    c->linew = pw - ml - mr;
    set_ruler_unit(d, c->linew);

    if (c->o->export_name != NULL) {
        write_export_header(c, b);
    }

    put_lines(b, preamble_0);

    if (c->o->black_white != 0) {
        buf_puts(b, "\\PassOptionsToPackage{gray}{xcolor}\n");
    }

    {
        int pts = text_points(d);

        if (pts == 11) {
            buf_puts(b, "\\documentclass[11pt]{article}\n");
        } else {
            buf_printf(b, "\\documentclass[%dpt]{extarticle}\n", pts);
        }
    }

    buf_puts(b, "\\usepackage[paperwidth=");
    put_in(b, pw);
    buf_puts(b, ",paperheight=");
    put_in(b, ph);
    buf_puts(b, ",left=");
    put_in(b, ml);
    buf_puts(b, ",right=");
    put_in(b, mr);
    buf_puts(b, ",top=");
    put_in(b, mt);
    buf_puts(b, ",bottom=");
    put_in(b, mb);

    if (c->hf_any != 0) {
        /* doc_opt_get answers in one buffer: read each value at once. */
        const char *v = doc_opt_get(d, "PHW");
        int hs = v != NULL ? atoi(v) : 500;
        int fs;

        v = doc_opt_get(d, "PFW");
        fs = (v != NULL ? atoi(v) : 500) + 150;

        /* The header and the footer stay inside the margins. */
        hs = hs > mt - 300 ? mt - 300 : hs;
        fs = fs > mb - 100 ? mb - 100 : fs;
        hs = hs < 100 ? 100 : hs;
        fs = fs < 200 ? 200 : fs;
        buf_puts(b, ",headheight=14pt,headsep=");
        put_in(b, hs);
        buf_puts(b, ",footskip=");
        put_in(b, fs);
    }

    buf_puts(b, "]{geometry}\n");

    if (c->hf_any != 0) {
        buf_puts(b, "\\usepackage{fancyhdr}\n");
    }

    put_lines(b, preamble_a);

    if (winding_used(c) != 0) {
        buf_puts(b, "\\usepackage{multicol}\n");
    }


    if (wide_used(c) != 0) {
        buf_puts(b, "\\usepackage{array}\n");
    }

    buf_printf(b, "\\setlength{\\tabcolsep}{%dpt}\n", TABCOLSEP_PT);
    put_symdecls(b);
    put_lines(b, preamble_b);

    if (c->o->black_white != 0) {
        put_lines(b, preamble_grey_pictures);
    }

    put_lines(b, preamble_strata);
    put_lines(b, logo_def_lines);
    put_lines(b, lms_def_lines);
    write_banner(c, b, mt);
    lineno_package(c, b);
    hyphenation_list(b);
    buf_puts(b, "\\begin{document}\n");

    if (c->hf_any != 0) {
        /* A mark at the start: the Section marker has nothing to print
           before the first headline, and \firstmark is never empty. */
        buf_puts(b, "\\mark{{}{}}\n");
    }

    if (pp != NULL && pp[0] == '\\' && pp[1] != '\0') {
        style = pp[1] == 'i' ? "roman" : pp[1] == 'I' ? "Roman" :
                pp[1] == 'A' ? "Alph" : pp[1] == 'a' ? "alph" : "arabic";
    }

    snprintf(c->pagestyle, sizeof c->pagestyle, "%s", style);
    put_pagenumbering(b, style);

    if (c->hf_any != 0) {
        hf_styles(c, b);
    }

    lineno_start(c, b);
}


/*
 * The size of the text in points that the Printer uses for the document d.
 */
int
latex_text_points(const Doc *d)
{
    return text_points(d);
}


/*
 * The head of a file that typesets a piece of a LaTeX marker alone: the
 * preamble of the Printer (the same packages, symbols and macros, so that a
 * marker with the Strata logo or a keycap makes what a print makes) with the
 * text size pts and without the page geometry, the headers and the other
 * parts that belong to the pages of the document, ending with the start of
 * the document.
 */
int
latex_snippet_head(int pts, FILE *out)
{
    Buf b;
    int ok;

    memset(&b, 0, sizeof b);
    put_lines(&b, preamble_0);
    buf_printf(&b, "\\documentclass[%dpt]{extarticle}\n", pts);
    put_lines(&b, preamble_a);
    buf_printf(&b, "\\setlength{\\tabcolsep}{%dpt}\n", TABCOLSEP_PT);
    put_symdecls(&b);
    put_lines(&b, preamble_b);
    put_lines(&b, preamble_strata);
    put_lines(&b, logo_def_lines);
    put_lines(&b, lms_def_lines);
    buf_puts(&b, "\\begin{document}\n");
    ok = fwrite(b.p, 1, b.n, out) == b.n;
    buf_free(&b);

    return ok != 0 ? 0 : -1;
}


/* The body of the file. */
/* Find the cross-references that print a page number: their sections. */
static void
scan_labels(Ctx *c)
{
    const Doc *d = c->d;

    for (int i = 0; i < d->nitems; i++) {
        const Block *blk;

        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        blk = d->items[i].blk;

        for (int col = 0; col < blk->ncols; col++) {
            const Text *t = &blk->cols[col].text;

            for (int j = 0; j < t->n; j++) {
                const Tok *k = &t->v[j];
                MPayload p;
                const char *xt;
                int item;

                if (k->kind != TK_MARKER || tok_mtype(k) != 2) {
                    continue;
                }

                mp_parse(&p, marker_info(2), tok_mdata(k), tok_mlen(k));
                xt = mp_get(&p, "XT");
                item = marker_xref_item(k);

                if (xt != NULL && atoi(xt) == 4 && item >= 0) {
                    if (c->want_label == NULL) {
                        c->want_label = calloc(usz(d->nitems) + 1, 1);
                    }

                    if (c->want_label != NULL) {
                        c->want_label[item] = 1;
                    }
                }

                mp_free(&p);
            }
        }
    }

    /* The table of contents refers to the page of each headline it lists. */
    if (c->o->contents > 0 && c->structured != 0) {
        for (int i = 0; i < d->nitems; i++) {
            Fmt f;

            if (d->items[i].type != IT_BLOCK) {
                continue;
            }

            fmt_resolve(d, i, 0, &f);

            if (f.headline == 0 || f.level < 1 || f.level > c->o->contents) {
                continue;
            }

            if (c->want_label == NULL) {
                c->want_label = calloc(usz(d->nitems) + 1, 1);
            }

            if (c->want_label != NULL) {
                c->want_label[i] = 1;
            }
        }
    }
}


static void
flush_out(Ctx *c)
{
    if (c->out.n > 0) {
        fwrite(c->out.p, 1, c->out.n, c->f);
        c->out.n = 0;
        c->out.p[0] = '\0';
    }
}


/*
 * The orientation markers (type 36) of the blocks first..last: the value of
 * the first one (0 when there is none) and of the last one.  A value is 1
 * for portrait (also no record) and 2 for landscape.
 */
static void
orient_scan(const Doc *d, int first, int last, int *start, int *end)
{
    *start = 0;
    *end = 0;

    for (int i = first; i <= last; i++) {
        const Block *blk;

        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        blk = d->items[i].blk;

        for (int col = 0; col < blk->ncols; col++) {
            const Text *t = &blk->cols[col].text;

            for (int j = 0; j < t->n; j++) {
                const Tok *k = &t->v[j];
                MPayload p;
                const char *v;
                int o;

                if (k->kind != TK_MARKER || tok_mtype(k) != 36) {
                    continue;
                }

                mp_parse(&p, marker_info(36), tok_mdata(k), tok_mlen(k));
                v = mp_get(&p, "1");
                o = v != NULL && atoi(v) == 2 ? 2 : 1;
                mp_free(&p);
                *start = *start == 0 ? o : *start;
                *end = o;
            }
        }
    }
}


/* Open or close the landscape environment so that it matches want. */
static void
set_orient(Ctx *c, int want)
{
    if (want == 2 && c->land == 0) {
        buf_puts(&c->out, "\\strlandbegin\n");
        c->land = 1;
    } else if (want == 1 && c->land != 0) {
        buf_puts(&c->out, "\\strlandend\n");
        c->land = 0;
    }
}


/*
 * The model of the estimate (see latex_estimate_pages): the characters and
 * the lines per inch, times 100.  The defaults were fitted to the page
 * counts of the sample documents (docs/PRINT-LATEX.txt, Preview).
 */
typedef struct {
    int cpi;
    int lpi;
} EstModel;

#define EST_CPI 1400
#define EST_LPI 510

/*
 * The height of a row of a table, in hundredths of a line: a row has rules
 * and room above and below the text.
 */
#define EST_ROW_H 125

/*
 * The height that a Picture marker adds to its paragraph, in hundredths of a
 * line.  The pictures are scaled to the width of the line; the height of the
 * file is not read here, so this is the mean of the pictures of the manuals.
 */
#define EST_PICTURE_H 1200

/*
 * The rows that a page of the contents, of a list or of the index holds, as
 * a part of the lines of a text page, in hundredths.
 */
#define EST_ROWS_PERCENT 90

/*
 * The index is set in a denser type than the contents and the lists: a page
 * of it holds this part of the lines of a text page, in hundredths (fitted
 * to the index of the Reference Manual and of the structured sample).
 */
#define EST_INDEX_PERCENT 110


static long *
est_positions(const Doc *d, const EstModel *m, long *page_h);


/* One line of the table of contents. */
typedef struct {
    int item; /* The item of the headline. */
    int level; /* Its level, 1 or more. */
    char tag[SECT_NUMLEN]; /* Its tag as the screen shows it, "1.2". */
    char title[200]; /* The text of the headline (CP437). */
} TocEntry;


/*
 * The headlines that the table of contents lists, in the order of the text:
 * those of the levels up to the one that was asked for.  Returns their
 * number (0 for none, also for a document that has no structure) or -1 when
 * there is no memory.  The list is for free() when it is not NULL.
 */
static int
toc_collect(const Ctx *c, TocEntry **out)
{
    const Doc *d = c->d;
    TocEntry *v = NULL;
    int n = 0;
    int cap = 0;

    *out = NULL;

    if (c->structured == 0) {
        return 0;
    }

    for (int i = 0; i < d->nitems; i++) {
        const Block *blk;
        Fmt f;
        size_t len;

        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        fmt_resolve(d, i, 0, &f);

        if (f.headline == 0 || f.level < 1 || f.level > c->o->contents) {
            continue;
        }

        if (n == cap) {
            TocEntry *w;

            cap = cap == 0 ? 64 : 2 * cap;
            w = realloc(v, usz(cap) * sizeof *v);

            if (w == NULL) {
                free(v);

                return -1;
            }

            v = w;
        }

        blk = d->items[i].blk;
        v[n].item = i;
        v[n].level = f.level;
        snprintf(v[n].tag, sizeof v[n].tag, "%s", f.tag);
        len = strlen(v[n].tag);

        while (len > 0 && v[n].tag[len - 1] == ' ') {
            len--;
            v[n].tag[len] = '\0';
        }

        v[n].title[0] = '\0';

        if (blk->ncols > 0) {
            text_plain(&blk->cols[0].text, v[n].title,
                       isz(sizeof v[n].title));
        }

        n++;
    }

    *out = v;

    return n;
}


/* Write the bytes of s, a CP437 string, as text. */
static void
put_cp437_string(Buf *b, const char *s)
{
    for (const char *q = s; *q != '\0'; q++) {
        put_cp437(b, u8(*q));
    }
}


/* The room of a tag in the table of contents, in characters. */
#define CONTENTS_TAGMAX 30


/*
 * A tag that may be broken after each point, for the narrow column of the
 * contents that holds the tag of a deeply nested section.
 */
static void
put_tag_breakable(Buf *b, const char *tag)
{
    char seg[SECT_NUMLEN];
    size_t n = 0;

    for (const char *p = tag;; p++) {
        if (*p == '\0' || *p == '.') {
            if (*p == '.') {
                seg[n++] = '.';
            }

            seg[n] = '\0';
            put_cp437_string(b, seg);
            n = 0;

            if (*p == '\0') {
                break;
            }

            buf_puts(b, "\\allowbreak{}");
        } else {
            seg[n++] = *p;
        }
    }
}


/*
 * The table of contents: a page of the lines "tag  title ..... page", with
 * the page numbers in roman numerals; the body then starts again at 1.  The
 * page of each headline is read from the label that the headline carries
 * (the second run of LaTeX fills them in, as it does for a cross-reference
 * to a page).  A document that has no headline gets one line that says so.
 */
/*
 * The title of a headline as a row of the contents: its characters, and the
 * TeX of its LaTeX markers (the logo of a name in a headline is one), so that
 * the row shows what the headline shows.  A headline with no LaTeX marker is
 * its plain text.
 */
static void
put_toc_title(Ctx *c, Buf *b, const Text *t, const char *plain)
{
    int have = 0;

    for (int j = 0; j < t->n && t->v[j].kind != TK_NL; j++) {
        have |= t->v[j].kind == TK_MARKER && tok_mtype(&t->v[j]) == MT_LATEX;
    }

    if (have == 0 || c->o->no_latex != 0) {
        put_cp437_string(b, plain);

        return;
    }

    for (int j = 0; j < t->n && t->v[j].kind != TK_NL; j++) {
        const Tok *k = &t->v[j];

        if (k->kind == TK_CHAR) {
            put_cp437(b, k->a);
        } else if (k->kind == TK_TAB) {
            put_cp437(b, ' ');
        } else if (k->kind == TK_MARKER && tok_mtype(k) == MT_LATEX) {
            char *raw = malloc(LATEX_RAW_MAX);
            int n = raw != NULL ? marker_latex_text(k, raw, LATEX_RAW_MAX) :
                                  -1;

            for (int q = 0; q < n; q++) {
                uint8_t ch = u8(raw[q]);

                if (ch >= 0x80) {
                    char u[4];

                    latex_cp437_utf8(ch, u);
                    buf_puts(b, u);
                } else {
                    buf_putc(b, chr(ch));
                }
            }

            buf_puts(b, "{}");
            free(raw);
        }
    }
}


static void
emit_contents_page(Ctx *c)
{
    TocEntry *v = NULL;
    int n = toc_collect(c, &v);
    int width[10];
    Buf *b = &c->out;

    memset(width, 0, sizeof width);
    buf_puts(b, "\\noindent{\\strhfont{1}\\bfseries Contents}\\par\n"
                "\\vspace{\\baselineskip}\n");

    if (n <= 0) {
        buf_puts(b, "\\noindent This document has no headlines to list."
                    "\\par\n");
    } else {
        /* The room for the tags of a level: the longest of them. */
        for (int i = 0; i < n; i++) {
            int lv = v[i].level < 9 ? v[i].level : 9;
            int len = isz(strlen(v[i].tag));

            len = len > CONTENTS_TAGMAX ? CONTENTS_TAGMAX : len;
            width[lv] = len > width[lv] ? len : width[lv];
        }

        buf_puts(b, "\\makeatletter\n");

        for (int i = 0; i < n; i++) {
            int lv = v[i].level < 9 ? v[i].level : 9;
            int ind = (lv - 1) * 15;
            int num = width[lv] > 0 ? 6 * width[lv] + 8 : 0;

            buf_printf(b, "\\@dottedtocline{1}{%d.%dem}{%d.%dem}{", ind / 10,
                       ind % 10, num / 10, num % 10);

            if (width[lv] > 0) {
                buf_puts(b, "\\numberline{");

                if (strlen(v[i].tag) > CONTENTS_TAGMAX) {
                    buf_printf(b, "\\parbox[t]{%d.%dem}{\\raggedright ",
                               num / 10, num % 10);
                    put_tag_breakable(b, v[i].tag);
                    buf_puts(b, "\\strut}");
                } else {
                    put_cp437_string(b, v[i].tag);
                }

                buf_puts(b, "}");
            }

            put_toc_title(c, b, &c->d->items[v[i].item].blk->cols[0].text,
                          v[i].title);
            buf_printf(b, "}{\\pageref{s%d}}\n", v[i].item);
        }

        buf_puts(b, "\\makeatother\n");
        c->o->pagerefs = 1;
    }

    free(v);
}


/* The options of the print that the document may decide (LATEX_AUTO). */
typedef struct {
    int contents;
    int index;
    int list_figures;
    int list_pictures;
    int list_tables;
} PartOpts;


/* Is there a headline of the levels 1 to level in the document? */
static int
has_headline(const Ctx *c, int level)
{
    const Doc *d = c->d;

    for (int i = 0; i < d->nitems; i++) {
        Fmt f;

        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        fmt_resolve(d, i, 0, &f);

        if (f.headline != 0 && f.level >= 1 && f.level <= level) {
            return 1;
        }
    }

    return 0;
}


/*
 * What the print makes of the contents, the index and the lists.  An option
 * that the caller gave (a command line option, a field of the Print panel)
 * wins.  An option that is LATEX_AUTO is the document's: the level of its
 * contents (YCL; none for a document that is not structured or has no
 * headline of the levels it lists) and, for each list, its setting when it
 * has one, else whether the document has a marker of the kind (docs/
 * AUTOLISTS.txt).  The options of the caller are put back at the end.
 */
static void
resolve_parts(const Ctx *c, PartOpts *was)
{
    LatexOpts *o = c->o;
    Doc *d = c->d;

    /* The print reads every block anyway: the counts of the markers are
       made again here, once, from the document that is printed, so that a
       count in the file that is wrong (a file made by hand or by another
       program) cannot hide a list.  The Editor does not do this at an edit. */
    doc_recount(d);
    was->contents = o->contents;
    was->index = o->index;
    was->list_figures = o->list_figures;
    was->list_pictures = o->list_pictures;
    was->list_tables = o->list_tables;

    if (o->contents == LATEX_AUTO) {
        int level = c->structured != 0 ? doc_contents_level(d) : 0;

        o->contents = level > 0 && has_headline(c, level) != 0 ? level : 0;
    }

    if (o->index == LATEX_AUTO) {
        o->index = doc_list_on(d, LK_INDEX);
    }

    if (o->list_figures == LATEX_AUTO) {
        o->list_figures = doc_list_on(d, LK_FIGURE);
    }

    if (o->list_pictures == LATEX_AUTO) {
        o->list_pictures = doc_list_on(d, LK_PICTURE);
    }

    if (o->list_tables == LATEX_AUTO) {
        o->list_tables = doc_list_on(d, LK_TABLE);
    }
}


/* Does the print have pages before the text: contents or lists? */
static int
front_wanted(const Ctx *c)
{
    return c->o->contents > 0 || lm_count(c, LM_FIGURE) > 0 ||
           lm_count(c, LM_PICTURE) > 0 || lm_count(c, LM_TABLE) > 0;
}


/*
 * The pages before the text, numbered in roman numerals: the table of
 * contents, the list of figures and the list of tables, one after the other,
 * each on a page of its own; the body then starts again at 1.
 */
static void
emit_contents(Ctx *c)
{
    Buf *b = &c->out;
    int first = 1;

    c->contents_done = 1;
    put_pagenumbering(b, "roman");

    if (c->hf_any != 0) {
        buf_puts(b, "\\thispagestyle{strc}\n");
    }

    if (c->o->contents > 0) {
        emit_contents_page(c);
        first = 0;
    }

    for (int kind = LM_FIGURE; kind <= LM_TABLE; kind++) {
        if (lm_count(c, kind) == 0) {
            continue;
        }

        if (first == 0) {
            buf_puts(b, "\\clearpage\n");

            if (c->hf_any != 0) {
                buf_puts(b, "\\thispagestyle{strc}\n");
            }
        }

        emit_list(c, kind);
        first = 0;
    }

    buf_puts(b, "\\clearpage\n");
    put_pagenumbering(b, c->pagestyle);
}


/* ------------------------------------------------- the index and the lists */
/*
 * The Index markers make the index at the end of the print, the Figure
 * markers (and the Name of a Picture) the list of figures and the Table
 * markers the list of tables, when the print asks for them.  Each marker is
 * found before the text is written (scan_lists) and carries a label lmN; the
 * second run of LaTeX has the page of the label (as for the table of
 * contents).  The lists come after the table of contents, the index last.
 */
static int
lm_wanted(const Ctx *c, int kind)
{
    return kind == LM_INDEX ? c->o->index != 0 :
           kind == LM_FIGURE ? c->o->list_figures != 0 :
           kind == LM_PICTURE ? c->o->list_pictures != 0 :
                                c->o->list_tables != 0;
}


static void
lm_add(Ctx *c, const Tok *k, int kind, const char *main, const char *sub,
       const char *ref)
{
    LMark *m;

    if (c->nlm == c->caplm) {
        LMark *w;

        c->caplm = c->caplm != 0 ? 2 * c->caplm : 64;
        w = realloc(c->lm, usz(c->caplm) * sizeof *w);

        if (w == NULL) {
            log_fatal(2, "latex: out of memory growing the list of the "
                      "entries of the index to %d entries", c->caplm);
        }

        c->lm = w;
    }

    m = &c->lm[c->nlm++];
    memset(m, 0, sizeof *m);
    m->tok = k;
    m->kind = kind;
    snprintf(m->main, sizeof m->main, "%s", main);
    snprintf(m->sub, sizeof m->sub, "%s", sub);
    snprintf(m->ref, sizeof m->ref, "%s", ref);
}


/* Find the markers of the index and the lists, in the order of the text. */
static void
scan_lists(Ctx *c)
{
    const Doc *d = c->d;

    if (c->o->index == 0 && c->o->list_figures == 0 &&
        c->o->list_pictures == 0 && c->o->list_tables == 0) {
        return;
    }

    for (int i = 0; i < d->nitems; i++) {
        const Block *blk;

        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        blk = d->items[i].blk;

        for (int col = 0; col < blk->ncols; col++) {
            const Text *t = &blk->cols[col].text;

            for (int j = 0; j < t->n; j++) {
                const Tok *k = &t->v[j];
                int mt;
                MPayload p;
                const char *a;
                const char *s;
                const char *r;

                if (k->kind != TK_MARKER) {
                    continue;
                }

                mt = tok_mtype(k);

                if (mt != 3 && mt != 11 && mt != 29 && mt != 25) {
                    continue;
                }

                mp_parse(&p, marker_info(mt), tok_mdata(k), tok_mlen(k));

                if (mt == 3 && lm_wanted(c, LM_INDEX) != 0) {
                    a = mp_get(&p, "2");
                    s = mp_get(&p, "3");
                    r = mp_get(&p, "4");

                    if (a != NULL && a[0] != '\0') {
                        lm_add(c, k, LM_INDEX, a, s != NULL ? s : "",
                               r != NULL ? r : "");
                    }
                } else if (mt == 11 && lm_wanted(c, LM_FIGURE) != 0) {
                    a = mp_get(&p, "1");

                    if (a != NULL && a[0] != '\0') {
                        lm_add(c, k, LM_FIGURE, a, "", "");
                    }
                } else if (mt == 25 && lm_wanted(c, LM_PICTURE) != 0) {
                    a = mp_get(&p, "6");

                    if (a != NULL && a[0] != '\0') {
                        lm_add(c, k, LM_PICTURE, a, "", "");
                    }
                } else if (mt == 29 && lm_wanted(c, LM_TABLE) != 0) {
                    a = mp_get(&p, "1");

                    if (a != NULL && a[0] != '\0') {
                        lm_add(c, k, LM_TABLE, a, "", "");
                    }
                }

                mp_free(&p);
            }
        }
    }

    if (c->nlm > 0) {
        c->o->pagerefs = 1;
    }
}


/* How many entries of a kind the document has. */
static int
lm_count(const Ctx *c, int kind)
{
    int n = 0;

    for (int i = 0; i < c->nlm; i++) {
        n += c->lm[i].kind == kind;
    }

    return n;
}


/* 1 when the print makes the index or the list of that marker: it is silent. */
static int
lm_marker_kept(const Ctx *c, int mtype)
{
    return lm_wanted(c, mtype == 3 ? LM_INDEX : mtype == 11 ? LM_FIGURE :
                        mtype == 25 ? LM_PICTURE : LM_TABLE);
}


/* Write the label of the marker k, if it makes an entry. Returns 1 if so. */
static int
lm_label(const Ctx *c, const Tok *k, Buf *b)
{
    for (int i = 0; i < c->nlm; i++) {
        if (c->lm[i].tok == k) {
            buf_printf(b, "\\label{lm%d}", i);

            return 1;
        }
    }

    return 0;
}


/* The page of a label as a number, 0 when it is not known yet; the joining
   of runs works on arabic numbers only. */
static const char *const index_macros[] = {
    "\\makeatletter",
    "\\newcount\\strixs \\newcount\\strixe \\newcount\\strixv",
    "\\newcount\\strixn \\newcount\\strixf",
    "\\def\\strixnum#1{\\ifcsname r@#1\\endcsname\\expandafter\\expandafter"
    "\\expandafter\\strixgrab"
    "\\csname r@#1\\endcsname\\@nil\\else0\\fi}",
    "\\def\\strixgrab#1#2#3\\@nil{#2}",
    "\\def\\strixflush{\\ifnum\\strixn>0 \\ifnum\\strixf>0 , \\fi\\strixf=1",
    " \\ifnum\\strixs=\\strixe \\the\\strixs",
    " \\else\\ifnum\\strixe=\\numexpr\\strixs+1\\relax "
    "\\the\\strixs, \\the\\strixe",
    " \\else \\the\\strixs--\\the\\strixe \\fi\\fi\\fi}",
    "\\def\\strixpages#1{\\strixn=0 \\strixf=0 \\@for\\strixl:=#1\\do{%",
    " \\strixv=\\expandafter\\strixnum\\expandafter{\\strixl}\\relax",
    " \\ifnum\\strixn=0 \\strixs=\\strixv \\strixe=\\strixv \\strixn=1",
    " \\else\\ifnum\\strixv=\\strixe\\else",
    " \\ifnum\\strixv=\\numexpr\\strixe+1\\relax \\strixe=\\strixv",
    " \\else\\strixflush \\strixs=\\strixv \\strixe=\\strixv \\fi\\fi\\fi}%",
    " \\strixflush}",
    "\\def\\strixplain#1{\\strixf=0 \\@for\\strixl:=#1\\do{\\ifnum\\strixf>0 , "
    "\\fi\\strixf=1 \\pageref{\\strixl}}}",
    "\\makeatother",
    "\\newcommand{\\strixline}[3]{\\par\\noindent\\hangindent=3em "
    "\\hangafter=1 \\hspace*{#1em}#2\\ifx\\relax#3\\relax\\else, #3\\fi}",
    NULL};


static const LMark *lm_base;


static int
lm_cmp(const void *pa, const void *pb)
{
    const LMark *lm = lm_base;
    const LMark *a = &lm[*(const int *)pa];
    const LMark *b = &lm[*(const int *)pb];
    int r = strcasecmp(a->main, b->main);

    if (r == 0) {
        r = strcasecmp(a->sub, b->sub);
    }

    if (r == 0) {
        r = *(const int *)pa - *(const int *)pb;
    }

    return r;
}


/* The pages (or the reference text) of the entries ix[a..b-1], as text. */
static void
index_pages(const Ctx *c, const int *ix, int a, int b, Buf *o)
{
    Buf lst = {NULL, 0, 0};

    for (int k = a; k < b; k++) {
        if (c->lm[ix[k]].ref[0] != '\0') {
            put_cp437_string(o, c->lm[ix[k]].ref);
            buf_free(&lst);

            return;
        }

        buf_printf(&lst, "%slm%d", lst.n > 0 ? "," : "", ix[k]);
    }

    if (lst.n > 0) {
        buf_printf(o, "\\%s{%s}",
                   strcmp(c->pagestyle, "arabic") == 0 ? "strixpages" :
                                                         "strixplain", lst.p);
    }

    buf_free(&lst);
}


/* The index: the entries in alphabetical order, with the pages of each. */
static void
emit_index(Ctx *c)
{
    Buf *b = &c->out;
    int *ix = NULL;
    int n = 0;

    if (c->o->index == 0) {
        return;
    }

    ix = malloc(usz(c->nlm + 1) * sizeof *ix);

    if (ix == NULL) {
        return;
    }

    for (int i = 0; i < c->nlm; i++) {
        if (c->lm[i].kind == LM_INDEX) {
            ix[n++] = i;
        }
    }

    lm_base = c->lm;
    qsort(ix, usz(n), sizeof *ix, lm_cmp);

    if (n == 0) {
        /* No marker, no page: the print is as it was. */
        free(ix);

        return;
    }

    buf_puts(b, "\\clearpage\n");

    if (c->hf_any != 0) {
        buf_puts(b, "\\thispagestyle{strx}\n");
    }

    buf_puts(b, "\\noindent{\\strhfont{1}\\bfseries Index}\\par\n"
                "\\vspace{\\baselineskip}\n");

    put_lines(b, index_macros);

    for (int a = 0; a < n;) {
        int e = a;
        int s;

        while (e < n && strcasecmp(c->lm[ix[e]].main,
                                   c->lm[ix[a]].main) == 0) {
            e++;
        }

        /* The main heading: the entries that have no subheading. */
        s = a;

        while (s < e && c->lm[ix[s]].sub[0] == '\0') {
            s++;
        }

        buf_puts(b, "\\strixline{0}{");
        put_cp437_string(b, c->lm[ix[a]].main);
        buf_puts(b, "}{");
        index_pages(c, ix, a, s, b);
        buf_puts(b, "}\n");

        for (int p = s; p < e;) {
            int q = p;

            while (q < e && strcasecmp(c->lm[ix[q]].sub,
                                       c->lm[ix[p]].sub) == 0) {
                q++;
            }

            buf_puts(b, "\\strixline{1.5}{");
            put_cp437_string(b, c->lm[ix[p]].sub);
            buf_puts(b, "}{");
            index_pages(c, ix, p, q, b);
            buf_puts(b, "}\n");
            p = q;
        }

        a = e;
    }

    buf_puts(b, "\\par\n");
    free(ix);
}


/* A list of the figures or of the tables, with the page of each. */
static void
emit_list(Ctx *c, int kind)
{
    Buf *b = &c->out;
    int n = 0;

    buf_printf(b, "\\noindent{\\strhfont{1}\\bfseries List of %s}\\par\n"
               "\\vspace{\\baselineskip}\n",
               kind == LM_FIGURE ? "Figures" :
               kind == LM_PICTURE ? "Pictures" : "Tables");

    for (int i = 0; i < c->nlm; i++) {
        if (c->lm[i].kind != kind) {
            continue;
        }

        if (n++ == 0) {
            buf_puts(b, "\\makeatletter\n");
        }

        buf_puts(b, "\\@dottedtocline{1}{0em}{0em}{");
        put_cp437_string(b, c->lm[i].main);
        buf_printf(b, "}{\\pageref{lm%d}}\n", i);
    }

    buf_puts(b, "\\makeatother\n");
}



/* ----------------------------------------------------- winding columns */
/*
 * Winding Columns: the number of columns (1 to 9) in which the text of a block
 * is set, from the block (BWN), else its level (F<n>WN), else the document
 * (FWN).  The text flows down one column and winds to the top of the next
 * (the multicol package).  Headlines and the blocks of a table keep the full
 * width, and so does the title page.  The gutter is FWG (F<n>WG) in
 * thousandths of an inch (0.250 in by default) and the rule between the
 * columns FWR (F<n>WR): 0 none, 1 single, 2 double, 3 wide.
 */
static int
winding_of(const Ctx *c, int item)
{
    const Block *blk = c->d->items[item].blk;
    const char *v = fmt_block_prop(c->d, blk, "BW", 'N');
    int level = c->structured != 0 ? doc_item_level(c->d, item) : 0;
    int n;
    Fmt f;

    if (blk->ncols != 1 || (c->structured != 0 && level == 0)) {
        return 1;
    }

    fmt_resolve(c->d, item, 0, &f);

    if (f.headline != 0) {
        return 1;
    }

    n = v != NULL && v[0] != '\0' ? atoi(v) : fmt_space(c->d, level, "WN", 1);

    return n < 1 ? 1 : n > 9 ? 9 : n;
}


/*
 * The gutter and the rule of the columns of a block: its own (BWG, BWR, in the
 * block or in its named format), else those of its level, else the
 * document's.
 */
static void
winding_look(const Ctx *c, int item, int *gutter, int *rule)
{
    const Block *blk = c->d->items[item].blk;
    const char *g = fmt_block_prop(c->d, blk, "BW", 'G');
    const char *r = fmt_block_prop(c->d, blk, "BW", 'R');
    int level = c->structured != 0 ? doc_item_level(c->d, item) : 0;

    *gutter = g != NULL && g[0] != '\0' ? atoi(g) :
                                          fmt_space(c->d, level, "WG", 250);
    *rule = r != NULL && r[0] != '\0' ? atoi(r) :
                                        fmt_space(c->d, level, "WR", 0);
}


/* Does any block of the document wind its text? */
static int
winding_used(const Ctx *c)
{
    for (int i = 0; i < c->d->nitems; i++) {
        if (c->d->items[i].type == IT_BLOCK && winding_of(c, i) > 1) {
            return 1;
        }
    }

    return 0;
}


/* Close the columns that are open and open n (1 for none) for the item. */
static void
winding_set(Ctx *c, int n, int gutter, int rule)
{
    Buf *b = &c->out;

    if (n == c->wind_n && (n == 1 || (gutter == c->wind_gut &&
                                      rule == c->wind_rule))) {
        return;
    }

    if (c->wind_n > 1) {
        buf_puts(b, "\\end{multicols}\n");
    }

    c->wind_n = n;
    c->wind_gut = gutter;
    c->wind_rule = rule;

    if (n > 1) {
        buf_puts(b, "\\setlength{\\columnsep}{");
        put_in(b, gutter);
        buf_printf(b, "}\\setlength{\\columnseprule}{%s}\n",
                   rule == 1 ? "0.4pt" : rule == 2 ? "1.2pt" :
                   rule == 3 ? "2pt" : "0pt");
        buf_printf(b, "\\begin{multicols}{%d}\n", n);
    }
}



/* Is the block a text block that is set in a box (a border or a shading)? */
static int
block_boxed(const Ctx *c, int item)
{
    Fmt f;

    if (block_bordered(c->d->items[item].blk) == 0) {
        return 0;
    }

    fmt_resolve(c->d, item, 0, &f);

    return c->structured == 0 || f.headline == 0;
}


/* Does a block of the document have a wide border (needs the array package)? */
static int
wide_used(const Ctx *c)
{
    for (int i = 0; i < c->d->nitems; i++) {
        const Block *b;
        int flag;

        if (c->d->items[i].type != IT_BLOCK) {
            continue;
        }

        b = c->d->items[i].blk;

        for (int k = 0; k < b->ncols; k++) {
            int m = border_parse(props_get(&b->cols[k].props, "CB", 0), &flag);

            for (int side = 0; side < 4; side++) {
                if (border_get(m, side) == BW_WIDE) {
                    return 1;
                }
            }
        }
    }

    return 0;
}


/*
 * The Print field of the Global Format settings: with Only Headlines the
 * blocks of the text of a section are left out, with Only Text the headlines
 * are.  The title page is printed in both.
 */
static int
print_skip(Ctx *c, int item)
{
    Fmt f;
    int head;

    if (c->print_mode == 0) {
        return 0;
    }

    fmt_resolve(c->d, item, 0, &f);
    head = c->structured != 0 && f.headline != 0 && f.level >= 1;

    if (head != 0 && joined_wanted(c, f.level) != 0 &&
        join_partner(c, item) >= 0) {
        /* The blocks cannot be joined when one of them is not printed. */
        c->item = item;
        diag_here(c, DIAG_ERROR, "join-unprinted", NULL,
                  "Cannot join headline/text blocks - unprinted");
    }

    return c->print_mode == 1 ? head == 0 && c->depth > 0 : head != 0;
}


static void
write_body(Ctx *c)
{
    const Doc *d = c->d;
    int in_title = 0;
    int title_pct = header_num(d, "P1", 'C', 33);
    long *pos = NULL;
    long page_h = 0;
    long mark_last = -1;
    KeepPlan *kp = keep_plan(d);
    int kend = -1;
    int kfl = 0;

    if (c->o->marks != 0) {
        {
            EstModel m = {EST_CPI, EST_LPI};

            pos = est_positions(d, &m, &page_h);
        }
    }

    /* A document without a headline has its (empty) contents first. */
    if (front_wanted(c) != 0) {
        TocEntry *t = NULL;
        int nt = toc_collect(c, &t);
        int has_plus = 0;

        free(t);

        for (int i = 0; i < d->nitems; i++) {
            has_plus |= d->items[i].type == IT_PLUS;
        }

        if ((c->o->contents > 0 && nt <= 0) ||
            (c->o->contents == 0 && (c->structured == 0 || has_plus == 0))) {
            emit_contents(c);
        }
    }

    /*
     * A part of a document that follows a landscape orientation marker
     * starts in landscape: the record PO of its header says so.
     */
    {
        const char *po = props_get(&d->header, "PO", 0);

        if (po != NULL && atoi(po) == 2) {
            set_orient(c, 2);
        }
    }

    for (int i = 0; i < d->nitems; i++) {
        const Item *it = &d->items[i];

        if (c->out.n > 262144) {
            flush_out(c);
        }

        /* A Keep part is set in a box; not in the text of a title page. */
        if (kp != NULL && kend < 0 && kp->end[i] >= 0 && in_title == 0 &&
            (c->structured == 0 || c->depth > 0)) {
            keep_open(c);
            kend = kp->end[i];
            kfl = kp->floating[i];
        }

        switch (it->type) {
            case IT_SECT:
                winding_set(c, 1, 0, 0);
                flush_levelnotes(c, c->depth);
                break;

            case IT_PLUS:
                winding_set(c, 1, 0, 0);

                if (in_title != 0) {
                    buf_printf(&c->out, "\\vspace{\\stretch{%d}}\\clearpage"
                               "\\setcounter{page}{1}\n", 100 - title_pct);
                    in_title = 0;
                }

                if (front_wanted(c) != 0 && c->contents_done == 0) {
                    emit_contents(c);
                }

                hf_first(c);
                c->depth++;
                break;

            case IT_MINUS:
                winding_set(c, 1, 0, 0);
                c->depth--;
                flush_levelnotes(c, c->depth + 1);
                break;

            case IT_BLOCK: {
                const Block *blk = it->blk;
                int last = i;
                int o1;
                int o2;

                /* The rows of a table are written as one piece. */
                while (blk->ncols > 1 && last + 1 < d->nitems &&
                       d->items[last + 1].type == IT_BLOCK &&
                       same_columns(blk, d->items[last + 1].blk) != 0) {
                    last++;
                }

                /*
                 * An orientation marker in the middle of a block changes
                 * the page from the beginning of that block.
                 */
                if (i == c->skip_item) {
                    i = last;
                    break;
                }

                if (print_skip(c, i) != 0) {
                    i = last;
                    break;
                }

                orient_scan(d, i, last, &o1, &o2);
                set_orient(c, o1);

                if (c->structured != 0 && c->depth == 0 && in_title == 0) {
                    buf_printf(&c->out, "\\thispagestyle{%s}\\null"
                               "\\vspace*{\\stretch{%d}}\n",
                               c->hf_any != 0 ? "strt" : "empty", title_pct);
                    in_title = 1;
                }

                if (pos != NULL && pos[d->nitems] > 0) {
                    long m = pos[i] * 1000 / pos[d->nitems];

                    if (m != mark_last) {
                        buf_printf(&c->out, "\\write16{@@strprint pos %ld}\n",
                                   m);
                        mark_last = m;
                    }
                }

                if (blk->ncols > 1 || block_boxed(c, i) != 0) {
                    winding_set(c, 1, 0, 0);
                    emit_table(c, i, last);
                    i = last;
                } else if (block_only_keep(blk) == 0) {
                    int wn = in_title != 0 ? 1 : winding_of(c, i);
                    int wg = 0;
                    int wr = 0;

                    if (wn > 1) {
                        winding_look(c, i, &wg, &wr);
                    }

                    winding_set(c, wn, wg, wr);
                    emit_block(c, i);
                }

                /* A second marker in the same piece takes effect after it. */
                set_orient(c, o2);
                break;
            }

            default:
                break;
        }

        if (kend >= 0 && i >= kend) {
            keep_close(c, kfl);
            kend = -1;
        }
    }

    if (kend >= 0) {
        keep_close(c, kfl);
    }

    keep_free(kp);
    winding_set(c, 1, 0, 0);

    if (in_title != 0) {
        buf_puts(&c->out, "\\par\\vspace{\\stretch{1}}\\clearpage\n");
    }

    free(pos);

    /* The landscape environment is closed at the end of the document. */
    set_orient(c, 1);

    flush_levelnotes(c, 0);

    if (c->enotes.n > 0) {
        buf_puts(&c->out, "\\clearpage\n{\\Large\\bfseries ");
        put_bytes(&c->out, c->nf.title, strlen(c->nf.title), 0);
        buf_puts(&c->out, "\\par}\\vspace{\\baselineskip}\n");
        put_note_entries(c, &c->out, &c->enotes, NK_END);
    }
}


/*
 * The Error Log: a new page that lists the diagnostics, one numbered entry
 * for each with its message, its position in words and the text that is
 * wrong.  It follows the last page of the document.
 */
static void
write_errlog(Ctx *c)
{
    const DiagList *l = c->o->errlog;
    Buf *b = &c->out;
    LineNum ln_err;
    int errors;
    int warnings;

    if (l == NULL || l->n == 0) {
        return;
    }

    c->quiet = 1;
    diag_counts(l, &errors, &warnings);
    buf_puts(b, "\\clearpage\\begingroup\\normalfont\\normalsize\\sloppy"
                "\\raggedright\\noindent\n");

    if (lineno_read(c->d, &ln_err) != 0) {
        buf_puts(b, "\\nolinenumbers\n");
    }

    buf_puts(b, "{\\Large\\bfseries Error Log\\par}"
                "\\vspace{0.5\\baselineskip}\n");
    buf_printf(b, "\\noindent %d error%s and %d warning%s found in this "
               "document.  Each entry gives the place in the document, so "
               "that it can be fixed in the Editor.\\par\n", errors,
               errors == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s");

    for (int i = 0; i < l->n; i++) {
        const Diag *g = &l->v[i];
        char pos[240];
        char said[DIAG_MSG];

        diag_position(g, pos, sizeof pos);
        buf_printf(b, "\\medskip\\par\\noindent\\textbf{%d.\\ %s.}\\ ", i + 1,
                   g->kind == DIAG_ERROR ? "Error" : "Warning");

        /* A message may hold the name of a file, which is UTF-8; the text
           that is written is in the code page of the document format. */
        name_to_cells(g->msg, said, sizeof said);
        put_bytes(b, said, strlen(said), 0);
        buf_puts(b, "\\par\n\\noindent\\hspace*{1.5em}\\textit{Position:}\\ ");
        put_bytes(b, pos, strlen(pos), 0);
        buf_puts(b, "\\par\n");

        if (g->text[0] != '\0') {
            buf_puts(b, "\\noindent\\hspace*{1.5em}\\textit{Text:}\\ "
                        "{\\ttfamily\\small ");
            put_bytes(b, g->text, strlen(g->text), 0);
            buf_puts(b, "}\\par\n");
        }
    }

    if (l->dropped > 0) {
        buf_printf(b, "\\medskip\\par\\noindent %d more diagnostic%s found "
                   "and not listed.\\par\n", l->dropped,
                   l->dropped == 1 ? " was" : "s were");
    }

    buf_puts(b, "\\endgroup\n");
    c->quiet = 0;
}


/* The print log lists the part files that went into the print. */
static void
log_included_files(const IncludeMap *m)
{
    for (int i = 0; i < include_files(m); i++) {
        const char *path;
        long size;
        time_t mtime;
        struct tm tmv;
        char when[32];

        include_file(m, i, &path, &size, &mtime);
        when[0] = '\0';

        if (localtime_r(&mtime, &tmv) != NULL) {
            strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tmv);
        }

        log_note("latex: part %d of the print is %s, %ld bytes, changed %s",
                 i + 1, path, size, when);
    }
}


int
latex_write(Doc *d, FILE *out, LatexOpts *o)
{
    Ctx c;
    int saved = marker_print_mode;
    Doc *work = NULL;
    CondMap *cmap = NULL;
    Doc *orig = d;
    Doc *joined = NULL;
    IncludeMap *imap = NULL;
    int ndiags;
    PartOpts was;

    memset(&c, 0, sizeof c);
    c.d = d;
    c.orig = d;
    c.o = o;
    c.f = out;
    c.item = -1;
    c.skip_item = -1;
    c.off = -1;
    c.mc_item = -2;
    cur_ctx = &c;

    if (o->diags != NULL) {
        diag_list_clear(o->diags);
    }

    c.structured = d->structured != 0;
    marker_print_mode = o->draft != 0 ? 1 : 2;
    marker_set_doc(d);
    marker_prepare(d);

    /* The text of the parts is put at the Include markers. */
    joined = include_expand(d, o->docdir, o->diags, &imap);

    if (joined != NULL) {
        d = joined;
        c.d = joined;
        c.imap = imap;
        log_included_files(imap);
        marker_set_doc(joined);
        marker_prepare(joined);
    }

    /* Only the branches of the If markers that are chosen are written. */
    ndiags = o->diags != NULL ? o->diags->n : 0;
    work = cond_filter(d, o->vars, o->diags, &cmap);

    if (imap != NULL && o->diags != NULL) {
        include_remap_diags(imap, o->diags, ndiags);
    }

    if (work != NULL) {
        d = work;
        c.d = work;
        c.cmap = cmap;
        marker_set_doc(work);
        marker_prepare(work);
    }

    if (marker_counter_limit() != 0) {
        diag_here(&c, DIAG_WARNING, "counter-limit", NULL,
                  "A counter has reached 4294967295 and stays there; "
                  "its later uses show the same number.");
    }

    doc_levels_begin(d);
    c.print_mode = fmt_space(d, 0, "P", 0);
    note_format_get(d, &c.nf);
    memset(o->longform, 0, sizeof o->longform);
    o->pagerefs = 0;
    o->npictures = 0;
    memset(missing_seen, 0, sizeof missing_seen);
    /* The parts that the document decides, from its settings and counts. */
    resolve_parts(&c, &was);
    /* Only Text has no table of contents. */
    o->contents = c.print_mode == 2 ? 0 : o->contents;
    scan_labels(&c);
    scan_lists(&c);

    if (o->failed != NULL && o->failed->n > 0) {
        c.mdone = calloc(usz(o->failed->n), 1);
    }

    write_preamble(&c, &c.out);
    hf_first_unstructured(&c);
    write_body(&c);
    emit_index(&c);
    c.item = -1;
    c.col = 0;
    c.line = 0;
    close_pairs(&c);
    write_errlog(&c);
    cur_ctx = NULL;
    o->contents = was.contents;
    o->index = was.index;
    o->list_figures = was.list_figures;
    o->list_pictures = was.list_pictures;
    o->list_tables = was.list_tables;
    buf_puts(&c.out, "\\end{document}\n");
    write_export_tail(&c, &c.out);
    flush_out(&c);
    o->npieces = c.piece;
    o->nmissing = 0;

    for (int i = 0; i < 256; i++) {
        if (missing_seen[i] != 0 && o->nmissing < LATEX_MAXMISSING) {
            o->missing[o->nmissing++] = cp437_uni[i];
        }
    }

    doc_levels_end();
    marker_print_mode = saved;

    if (work != NULL) {
        doc_free(work);
        cond_map_free(cmap);
    }

    if (joined != NULL) {
        doc_free(joined);
        include_map_free(imap);
    }

    if (work != NULL || joined != NULL) {
        marker_set_doc(orig);
        marker_prepare(orig);
    }

    for (int i = 0; i < c.lnotes.n; i++) {
        free(c.lnotes.v[i].text);
    }

    for (int i = 0; i < c.enotes.n; i++) {
        free(c.enotes.v[i].text);
    }

    free(c.lnotes.v);
    free(c.enotes.v);
    free(c.want_label);
    free(c.mdone);
    free(c.lm);
    hf_free(&c);
    buf_free(&c.out);
    buf_free(&c.dfr);
    buf_free(&c.pics);

    return ferror(out) != 0 ? -1 : 0;
}


int
latex_probe(Doc *d, FILE *out, LatexOpts *o)
{
    char *mem = NULL;
    size_t len = 0;
    FILE *m = open_memstream(&mem, &len);
    const char *p;
    const char *end;
    int r;

    if (m == NULL) {
        return -1;
    }

    r = latex_write(d, m, o);
    fclose(m);

    if (r != 0 || mem == NULL) {
        free(mem);

        return -1;
    }

    fputs("\\documentclass{article}\n\\usepackage{amsmath,amssymb}\n", out);
    fputs("\\begin{document}\n\\sloppy\n", out);
    p = mem;
    end = mem + len;

    while (p < end) {
        const char *e = memchr(p, '\n', usz(isz(lsz(end - p))));
        size_t n;
        const char *q;

        e = e != NULL ? e : end;
        n = lsz(e - p);
        q = e;

        while (q > p && is_digit(q[-1])) {
            q--;
        }

        if (q < e && q - p >= 3 && q[-1] == 'F' && q[-2] == '%' &&
            (p[0] == '$' || (p[0] == '\\' && p[1] == '['))) {
            fwrite(p, 1, n, out);
            fputs("\n\\par\n", out);
        }

        p = e + 1;
    }

    fputs("\\end{document}\n", out);
    free(mem);

    return ferror(out) != 0 ? -1 : 0;
}


/* The estimate of the pages, and the positions of the marks. */

/* The width of text in character cells, and the page breaks in it. */
static int
est_width(const Text *t, int s, int e, int *breaks)
{
    int w = 0;

    for (int i = s; i < e; i++) {
        switch (t->v[i].kind) {
            case TK_CHAR:
                w++;
                break;

            case TK_TAB:
                w += 4;
                break;

            case TK_MARKER:
                if (tok_mtype(&t->v[i]) == MT_PRINT_ERROR) {
                    break;
                }

                w += 6;

                if (tok_mtype(&t->v[i]) == 23) {
                    (*breaks)++;
                }

                break;

            case TK_ATTR:
            case TK_NL:
            case TK_CTRL:
            default:
                break;
        }
    }

    return w;
}


/* The lines that a column takes when a line holds cpl characters. */
static long
est_column_lines(const Text *t, int cpl, int *breaks)
{
    long lines = 0;
    int s = 0;

    cpl = cpl < 4 ? 4 : cpl;

    while (s < t->n) {
        int e = s;
        int w;

        while (e < t->n && t->v[e].kind != TK_NL) {
            e++;
        }

        w = est_width(t, s, e, breaks);
        lines += w <= cpl ? 1 : (w + cpl - 1) / cpl;
        s = e + 1;
    }

    return lines > 0 ? lines : 1;
}


/* The number of Picture markers in a text. */
static long
est_pictures(const Text *t)
{
    long n = 0;

    for (int i = 0; i < t->n; i++) {
        if (t->v[i].kind == TK_MARKER && tok_mtype(&t->v[i]) == 25) {
            n++;
        }
    }

    return n;
}


/* The characters that fit in w thousandths of an inch. */
static int
cells(int w, int cpi_x100)
{
    return isz(lsz(w) * lsz(cpi_x100) / 100000);
}


/*
 * The height of the blocks first to last (a table when they have columns),
 * in hundredths of a line.
 */
static long
est_group(const Doc *d, int first, int last, int stretch, int gap, int linew,
          const EstModel *m, int *breaks)
{
    int cpi_x100 = m->cpi;
    long h = 0;
    int headline = d->structured != 0 && first > 0 &&
                   d->items[first - 1].type == IT_SECT;

    for (int i = first; i <= last; i++) {
        const Block *b = d->items[i].blk;
        long rows = 1;

        if (b->ncols > 1) {
            int left = linew;

            for (int k = 0; k < b->ncols; k++) {
                const Column *col = &b->cols[k];
                int w = col->width > 0 ? ru(col->width) : left;
                long n;

                w = w > left || k == b->ncols - 1 ? left : w;
                left -= w;
                n = est_column_lines(&col->text, cells(w, cpi_x100), breaks);
                rows = n > rows ? n : rows;
                h += est_pictures(&col->text) * EST_PICTURE_H;
            }

            h += rows * EST_ROW_H;
        } else if (b->ncols == 1) {
            const char *bl = props_get(&b->props, "BL", 0);
            int w = linew - ru(bl != NULL ? atoi(bl) : 0);

            rows = est_column_lines(&b->cols[0].text, cells(w, cpi_x100),
                                    breaks);
            h += rows * stretch;
            h += est_pictures(&b->cols[0].text) * EST_PICTURE_H;
        } else {
            h += 100;
        }
    }

    if (headline != 0) {
        h += 100;
    }

    return gap > 100 ? h + gap - 100 : h;
}


/*
 * The position of the start of each item in the document, in hundredths of
 * a line from the top of the first page (a page break moves to the start of
 * the next page), and the length of a page.  The array has nitems + 1
 * entries, the last one is the end of the document; the caller frees it.
 */
static long *
est_positions(const Doc *d, const EstModel *m, long *page_h)
{
    long *pos = malloc(usz(d->nitems + 1) * sizeof *pos);
    int pw;
    int ph;
    int ml;
    int mr;
    int mt;
    int mb;
    long y = 0;
    int depth = 0;
    int in_title = 0;
    long ph_x100;
    int stretch = fmt_space(d, 0, "SL", 100);
    int gap = fmt_space(d, 0, "ST", 100);
    int pbl = page_break_level(d);

    page_geometry(d, &pw, &ph, &ml, &mr, &mt, &mb);
    ph_x100 = 1L * (ph - mt - mb) * m->lpi / 1000;
    ph_x100 = ph_x100 < 400 ? 400 : ph_x100;
    *page_h = ph_x100;

    for (int i = 0; i < d->nitems; i++) {
        const Item *it = &d->items[i];
        int last = i;
        int breaks = 0;

        pos[i] = y;

        if (it->type == IT_PLUS) {
            if (in_title != 0) {
                y = (y + ph_x100 - 1) / ph_x100 * ph_x100;
                in_title = 0;
            }

            depth++;
            stretch = fmt_space(d, depth, "SL", 100);
            gap = fmt_space(d, depth, "ST", 100);
        } else if (it->type == IT_MINUS) {
            depth--;
            stretch = fmt_space(d, depth, "SL", 100);
            gap = fmt_space(d, depth, "ST", 100);
        } else if (it->type == IT_BLOCK) {
            const Block *blk = it->blk;

            while (blk->ncols > 1 && last + 1 < d->nitems &&
                   d->items[last + 1].type == IT_BLOCK &&
                   same_columns(blk, d->items[last + 1].blk) != 0) {
                last++;
            }

            if (d->structured != 0 && depth == 0) {
                in_title = 1;
            }

            /* A headline at or above the Page Break Level starts a page. */
            if (pbl > 0 && y > 0 && d->structured != 0 && i > 0 &&
                d->items[i - 1].type == IT_SECT &&
                doc_item_level(d, i) <= pbl) {
                y = (y + ph_x100 - 1) / ph_x100 * ph_x100;
            }

            pos[i] = y;
            y += est_group(d, i, last, stretch, gap, pw - ml - mr, m, &breaks);

            if (breaks > 0) {
                y = (y + ph_x100 - 1) / ph_x100 * ph_x100;
            }

            for (int k = i + 1; k <= last; k++) {
                pos[k] = y;
            }

            i = last;
        }
    }

    if (in_title != 0) {
        y = (y + ph_x100 - 1) / ph_x100 * ph_x100;
    }

    pos[d->nitems] = y;

    return pos;
}


/* The pages that a list of n rows takes, with rows_pp rows to a page. */
static long
est_list_pages(long n, long rows_pp)
{
    return n > 0 ? (n + rows_pp - 1) / rows_pp : 0;
}


/*
 * The entries that the lists and the index would hold: the markers of each
 * kind that have a name (a Figure, Picture or Table marker with no name and
 * an Index marker with no main heading are listed nowhere, as the Printer
 * does).  One pass over the markers of the text, made when the estimate is
 * asked for, never at an edit.
 */
static void
est_entries(const Doc *d, long n[LK_COUNT])
{
    for (int k = 0; k < LK_COUNT; k++) {
        n[k] = 0;
    }

    for (int i = 0; i < d->nitems; i++) {
        const Block *blk;

        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        blk = d->items[i].blk;

        for (int col = 0; col < blk->ncols; col++) {
            const Text *t = &blk->cols[col].text;

            for (int j = 0; j < t->n; j++) {
                const Tok *k = &t->v[j];
                MPayload p;
                const char *a;
                int mt;
                int kind;

                if (k->kind != TK_MARKER) {
                    continue;
                }

                mt = tok_mtype(k);
                kind = mt == 11 ? LK_FIGURE : mt == 25 ? LK_PICTURE :
                       mt == 29 ? LK_TABLE : mt == 3 ? LK_INDEX : -1;

                if (kind < 0) {
                    continue;
                }

                mp_parse(&p, marker_info(mt), tok_mdata(k), tok_mlen(k));
                a = mp_get(&p, mt == 3 ? "2" : mt == 25 ? "6" : "1");

                if (a != NULL && a[0] != '\0') {
                    n[kind]++;
                }

                mp_free(&p);
            }
        }
    }
}


/*
 * The pages that the print makes of the document's own settings besides the
 * text: the table of contents, the lists and the index, each as the stored
 * settings and the marker counters say (docs/AUTOLISTS.txt).  The rows of a
 * page are a part of the lines of a text page.
 */
static long
est_generated_pages(const Doc *d, long page_h)
{
    long rows_pp = page_h * EST_ROWS_PERCENT / 10000;
    long idx_pp = page_h * EST_INDEX_PERCENT / 10000;
    long entries[LK_COUNT];
    long pages = 0;
    int level = d->structured != 0 ? doc_contents_level(d) : 0;

    rows_pp = rows_pp < 4 ? 4 : rows_pp;
    idx_pp = idx_pp < 4 ? 4 : idx_pp;

    if (level > 0) {
        long rows = 0;

        for (int i = 0; i < d->nitems; i++) {
            Fmt f;

            if (d->items[i].type != IT_BLOCK) {
                continue;
            }

            fmt_resolve(d, i, 0, &f);

            if (f.headline != 0 && f.level >= 1 && f.level <= level) {
                rows++;
            }
        }

        pages += est_list_pages(rows, rows_pp);
    }

    est_entries(d, entries);

    for (int k = 0; k < LK_COUNT; k++) {
        if (doc_list_on(d, k) != 0) {
            pages += est_list_pages(entries[k], k == LK_INDEX ?
                                    idx_pp : rows_pp);
        }
    }

    return pages;
}


int
latex_estimate_with(const Doc *src, int cpi_x100, int lpi_x100)
{
    EstModel m = {cpi_x100, lpi_x100};
    const Doc *d = src;
    Doc *work;
    CondMap *cmap = NULL;
    long page_h;
    long *pos;
    long total;
    long pages;

    /* The print writes only the branches of the If markers that the
       variables of the process choose: the estimate counts the same text
       (the Editor and the Printer are of the same build type). */
    work = cond_filter(src, vars_process(), NULL, &cmap);

    if (work != NULL) {
        d = work;
    }

    pos = est_positions(d, &m, &page_h);
    total = pos[d->nitems];
    pages = (total + page_h - 1) / page_h;
    free(pos);
    pages += est_generated_pages(d, page_h);

    if (work != NULL) {
        doc_free(work);
        cond_map_free(cmap);
    }

    return pages < 1 ? 1 : isz(lsz(pages));
}


int
latex_estimate_pages(const Doc *d)
{
    return latex_estimate_with(d, EST_CPI, EST_LPI);
}
