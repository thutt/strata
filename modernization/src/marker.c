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

/* marker.c - the marker commands. */
#include "util.h"
#include "marker.h"
#include "layout.h"
#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

int marker_print_mode;
int marker_hide_perr;
int (*marker_eval_fn)(const Tok *k, char *buf, size_t cap);
void (*marker_forget_fn)(const Doc *d);
int (*marker_failed_fn)(const Tok *k);
int (*marker_picture_fn)(const char *docpath, const char *name, char *why,
                         size_t cap);

/* ------------------------------------------------------------- the table */
/*
 * The type numbers were found by creating each marker in a
 * program that has them and reading the bytes it wrote.  The field
 * identifiers come from the same experiments and from the sample documents.
 * Entries marked "inferred" were not seen in a file written by that
 * program.
 */
static const char *const keep_choices[] = {"Start (Fixed)", "Start (Floating)",
                                           "End", NULL};
/* The treatments of a Whitespace marker, stored as 0, 1 and 2. */
static const char *const treatment_choices[] = {
    "Crop at End of Page", "Place All on Next Page", "Float to Next Page",
    NULL};
static const char *const break_choices[] = {"Page", "Odd Page", "Even Page",
                                            NULL};
static const char *const position_choices[] = {"Fixed", "Floating", "In Text",
                                               NULL};
/* The first entry is the orientation of the document and has no record. */
static const char *const orientation_choices[] = {"[Portrait]", "Portrait",
                                                  "Landscape", NULL};

/* The units of a length that TeX accepts; inches have no record. */
static const char *const unit_choices[] = {"in", "cm", "mm", "pt", "bp", "pc",
                                           "dd", "cc", "sp", "em", "ex",
                                           NULL};

/* The fixed names of the font numbers, used when the font list is absent. */
static const char *const keycap_font_choices[] = {
    "[Text]",  "Headline", "Text",    "Header/Footer", "Font 4",  "Font 5",
    "Font 6",  "Font 7",   "Font 8",  "Font 9",        "Font 10", "Font 11",
    "Font 12", "Font 13",  "Font 14", "Font 15",       "Font 16", "Font 17",
    "Font 18", "Font 19",  "Font 20", NULL};

static const char *const style_choices[] = {
    "[Style of the counter]", "Arabic (1)", "Lower Case Roman (i)",
    "Upper Case Roman (I)", "Upper Case Letters (A)",
    "Lower Case Letters (a)", NULL};

static const MField f_counter[] = {
    {"1", "Counter (1-99)", MF_NUM, 0, "1", NULL, 0, 0},
    {"2", "Set Value", MF_NUM, 0, NULL, NULL, 0, 0},
    {"3", "Number Style", MF_CHOICE, 0, NULL, style_choices, 0, 0},
};

/* The first entry stands for the Primary Note Type and has no record. */
static const char *const note_choices[] = {"[Footnote]", "Footnote",
                                           "Levelnote", "Endnote", NULL};

static const MField f_note[] = {
    {"1", "Type", MF_CHOICE, 0, NULL, note_choices, 0, 0},
};

static const MField f_comment[] = {
    {"1", "Name", MF_TEXT, 0, NULL, NULL, 0, 0},
    {"2", "Created", MF_TEXT, 0, NULL, NULL, 0, 0},
};

static const MField f_date[] = {
    {"1", "Format", MF_TEXT, 0, "Month D, Year", NULL, 0, 0},
};

static const MField f_time[] = {
    {"1", "Format", MF_TEXT, 0, "H:MM am/pm", NULL, 0, 0},
};

/*
 * What a Section marker of a header or footer prints (inferred: the
 * record of the marker is not known, this one is written by Strata; the
 * record SH holds the stored value).  The first entry is the default and has
 * no record.
 */
static const char *const section_choices[] = {"Number and Title", "Number Only",
                                              "Title Only", NULL};

static const MField f_section[] = {
    {"S", "Show", MF_CHOICE, 0, NULL, section_choices, 1, 0},
};

static const MField f_figure[] = {
    {"1", "Name (for Table of Figures)", MF_TEXT, 0, NULL, NULL, 0, 0},
};

static const MField f_table[] = {
    {"1", "Name (for Table of Tables)", MF_TEXT, 0, NULL, NULL, 0, 1},
};

/* The font of a Font marker: the first entry, [Text], is the font that the
   block has and means no record; the others are stored as their number. */
static const MField f_font[] = {
    {"1", "Font", MF_CHOICE, 0, NULL, keycap_font_choices, 0, 0},
};

static const MField f_if[] = {
    {"1", "Expression", MF_TEXT, 0, NULL, NULL, 0, 1},
};

static const MField f_include[] = {
    {"1", "File Name", MF_TEXT, 0, NULL, NULL, 0, 1},
};

static const MField f_index[] = {
    {"2", "Main Heading", MF_TEXT, 0, NULL, NULL, 0, 1},
    {"3", "Subheading", MF_TEXT, 0, NULL, NULL, 0, 0},
    {"4", "Reference Text", MF_TEXT, 0, NULL, NULL, 0, 0},
};

static const MField f_keep[] = {
    {"1", "Keep", MF_CHOICE, 0, NULL, keep_choices, 0, 0},
};

/*
 * The values of the fields 2 to 5 that are left out are those of the Keycap
 * Format of the document (keycap_format_get); the panel shows them in
 * brackets.  The list of fonts of field 5 is made by the panel.
 */
static const MField f_keycap[] = {
    {"1", "Legend", MF_TEXT, 0, NULL, NULL, 0, 1},
    {"2", "Foreground", MF_NUM, 1, NULL, NULL, 0, 0},
    {"3", "Background", MF_NUM, 1, NULL, NULL, 0, 0},
    {"4", "Scale", MF_NUM, 3, NULL, NULL, 0, 0},
    {"5", "Font", MF_CHOICE, 0, NULL, NULL, 0, 0},
};

static const MField f_line[] = {
    {"2", "Length", MF_NUM, 3, "RM", NULL, 0, 0},
    {"3", "Weight", MF_NUM, 3, NULL, NULL, 0, 0},
    {"4", "Center", MF_NUM, 0, NULL, NULL, 0, 0},
    {"1", "String", MF_TEXT, 0, NULL, NULL, 0, 0},
};

static const MField f_merge[] = {
    {"1", "Variable Name", MF_TEXT, 0, NULL, NULL, 0, 1},
};

static const MField f_orientation[] = {
    {"1", "Orientation", MF_CHOICE, 0, NULL, orientation_choices, 0, 0},
};

static const MField f_pagebreak[] = {
    {"1", "Start Following Text on Next", MF_CHOICE, 0, NULL, break_choices, 0,
     0},
};

static const MField f_picture[] = {
    {"1", "File Name", MF_TEXT, 0, NULL, NULL, 0, 1},
    {"5", "Position", MF_CHOICE, 0, NULL, position_choices, 0, 0},
    {"2", "Width", MF_NUM, 3, NULL, NULL, 0, 0},
    {"3", "Height", MF_NUM, 3, NULL, NULL, 0, 0},
    {"U", "Unit", MF_CHOICE, 0, NULL, unit_choices, 0, 0},
    {"4", "Scale", MF_NUM, 3, NULL, NULL, 0, 0},
    {"6", "Name (for Table of Figures)", MF_TEXT, 0, NULL, NULL, 0, 0},
};

static const MField f_whitespace[] = {
    {"1", "Size (vertical)", MF_NUM, 3, NULL, NULL, 0, 1},
    {"U", "Unit", MF_CHOICE, 0, NULL, unit_choices, 0, 0},
    {"2", "Treatment at End of Page", MF_CHOICE, 0, "1", treatment_choices, 0,
     0},
};

static const char *const xref_choices[] = {"[Tag]", "Tag", "Headline", "Both",
                                           "Page Number", NULL};

static const MField f_xref[] = {
    {"XN", "Section Number", MF_TEXT, 0, NULL, NULL, 0, 1},
    {"XT", "Style", MF_CHOICE, 0, NULL, xref_choices, 0, 0},
};

#define NF(a) (sizeof(a) / sizeof((a)[0]))
#define M(t, nm, av, f) {t, nm, av, 0, f, NF(f)}
#define M0(t, nm, av) {t, nm, av, 0, NULL, 0}
#define MN(t, nm, f) {t, nm, MA_ANY, 1, f, NF(f)}
#define MN0(t, nm) {t, nm, MA_ANY, 1, NULL, 0}

/* In the order of the Marker Commands menu. */
static const MField f_perr[] = {
    {"1", "Message", MF_TEXT, 0, NULL, NULL, 0, 0},
    {"2", "Code", MF_TEXT, 0, NULL, NULL, 0, 0},
    {"3", "Run", MF_TEXT, 0, NULL, NULL, 0, 0},
};

/* The Print Error marker: known to the program, but not on the menu. */
static const MInfo perr_info = {MT_PRINT_ERROR, "Print Error", MA_ANY, 0,
                                f_perr, 3};

static const MInfo menu[] = {
    M0(5, "Author", MA_ANY),
    M0(39, "Column Break", MA_ANY),
    MN(6, "Comment", f_comment),
    M(17, "Counter", MA_ANY, f_counter),
    M(2, "Cross-reference", MA_STRUCTURED, f_xref),
    M(7, "Date", MA_ANY, f_date),
    M0(8, "Description", MA_ANY),
    M0(34, "Else", MA_ANY),
    M0(35, "Endif", MA_ANY),
    MN0(9, "Equation"),
    M(11, "Figure", MA_ANY, f_figure),
    M(12, "Filedate", MA_ANY, f_date),
    M0(13, "Filename", MA_ANY),
    M(14, "Filetime", MA_ANY, f_time),
    M(15, "Font", MA_ANY, f_font),
    M(33, "If", MA_ANY, f_if),
    M(16, "Include", MA_ANY, f_include),
    M(3, "Index", MA_ANY, f_index),
    M(4, "Keep", MA_ANY, f_keep),
    M(37, "Keycap", MA_ANY, f_keycap),
    MN0(MT_LATEX, "LaTeX"),
    M(18, "Line", MA_ANY, f_line),
    M(19, "Merge", MA_ANY, f_merge),
    MN(1, "Note", f_note),
    M(36, "Orientation", MA_ANY, f_orientation),
    M(23, "Page Break", MA_ANY, f_pagebreak),
    M0(22, "Page Number", MA_HEADFOOT),
    M0(24, "Pathname", MA_ANY),
    M(25, "Picture", MA_ANY, f_picture),
    M0(26, "Printer", MA_ANY),
    M0(27, "Revision", MA_ANY),
    M(28, "Section", MA_HEADFOOT, f_section),
    M(29, "Table", MA_ANY, f_table),
    M(30, "Time", MA_ANY, f_time),
    M(31, "Whitespace", MA_ANY, f_whitespace),
};

int
marker_menu_count(void)
{
    return isz(NF(menu));
}


const MInfo *
marker_menu(int i)
{
    if (i < 0 || i >= isz(NF(menu))) {
        return NULL;
    }

    return &menu[i];
}


const MInfo *
marker_info(int type)
{
    if (type == MT_PRINT_ERROR) {
        return &perr_info;
    }

    for (int i = 0; i < isz(NF(menu)); i++) {
        if (menu[i].type == type) {
            return &menu[i];
        }
    }

    return NULL;
}


const char *
marker_name(int type)
{
    const MInfo *mi = marker_info(type);

    return mi != NULL ? mi->name : NULL;
}


/* ------------------------------------------------------------- payload */
static int
id_len(const MInfo *mi)
{
    return mi != NULL && mi->type == 2 ? 2 : 1;
}


/*
 * The number of bytes at the start of the payload that belong to the nested
 * text blocks.  They end at the first record that is a numbered field.
 */
static size_t
nested_end(const uint8_t *d, size_t n)
{
    size_t i = 0;

    while (i < n) {
        if (d[i] == 'T') {
            size_t e = msdoc_scan_text(d, n, i + 1);

            if (e == SIZE_MAX) {
                break;
            }

            i = e;

            if (i + 1 < n && d[i] == '\r' && d[i + 1] == '\n') {
                i += 2;
            }

            continue;
        }

        if (is_digit(d[i])) {
            break;
        }

        while (i + 1 < n && !(d[i] == '\r' && d[i + 1] == '\n')) {
            i++;
        }

        i = i + 2 < n ? i + 2 : n;
    }

    return i;
}


static char *
dup_bytes(const uint8_t *s, size_t n)
{
    char *r = malloc(n + 1);

    memcpy(r, s, n);
    r[n] = '\0';

    return r;
}


void
mp_parse(MPayload *p, const MInfo *mi, const uint8_t *data, size_t len)
{
    memset(p, 0, sizeof *p);

    size_t i = 0;

    if (mi != NULL && mi->nested != 0) {
        i = nested_end(data, len);
    }

    p->nested = malloc(i + 1);

    if (i > 0) {
        memcpy(p->nested, data, i);
    }

    p->nested_len = i;

    size_t idl = usz(id_len(mi));

    while (i < len && p->n < MP_MAXREC) {
        size_t e = i;

        while (e + 1 < len && !(data[e] == '\r' && data[e + 1] == '\n')) {
            e++;
        }

        if (e + 1 >= len) {
            e = len;
        }

        size_t n = e - i;
        int nul = 0;

        if (n > 0 && data[i + n - 1] == 0) {
            nul = 1;
            n--;
        }

        if (n > 0) {
            size_t il = n < idl ? n : idl;

            p->id[p->n] = dup_bytes(data + i, il);
            p->val[p->n] = dup_bytes(data + i + il, n - il);
            p->nul[p->n] = u8(nul);
            p->n++;
        }

        i = e + 2 < len ? e + 2 : len;
    }
}


const char *
mp_get(const MPayload *p, const char *id)
{
    for (int i = 0; i < p->n; i++) {
        if (strcmp(p->id[i], id) == 0) {
            return p->val[i];
        }
    }

    return NULL;
}


void
mp_set(MPayload *p, const char *id, const char *val, int nul)
{
    int at = -1;

    for (int i = 0; i < p->n; i++) {
        if (strcmp(p->id[i], id) == 0) {
            at = i;
            break;
        }
    }

    if (val == NULL) {
        if (at >= 0) {
            free(p->id[at]);
            free(p->val[at]);

            for (int i = at; i + 1 < p->n; i++) {
                p->id[i] = p->id[i + 1];
                p->val[i] = p->val[i + 1];
                p->nul[i] = p->nul[i + 1];
            }

            p->n--;
        }

        return;
    }

    if (at < 0) {
        if (p->n >= MP_MAXREC) {
            return;
        }

        /* Keep the records in order of their identifiers. */
        at = p->n;

        while (at > 0 && strcmp(p->id[at - 1], id) > 0) {
            p->id[at] = p->id[at - 1];
            p->val[at] = p->val[at - 1];
            p->nul[at] = p->nul[at - 1];
            at--;
        }

        p->n++;
        p->id[at] = strdup(id);
        p->val[at] = NULL;
    }

    free(p->val[at]);
    p->val[at] = strdup(val);
    p->nul[at] = u8(nul);
}


uint8_t *
mp_build(const MPayload *p, size_t *len)
{
    size_t cap = p->nested_len + 1;

    for (int i = 0; i < p->n; i++) {
        cap += strlen(p->id[i]) + strlen(p->val[i]) + 3;
    }

    uint8_t *out = malloc(cap);
    size_t o = 0;

    memcpy(out, p->nested, p->nested_len);
    o = p->nested_len;

    for (int i = 0; i < p->n; i++) {
        size_t il = strlen(p->id[i]);
        size_t vl = strlen(p->val[i]);

        memcpy(out + o, p->id[i], il);
        o += il;
        memcpy(out + o, p->val[i], vl);
        o += vl;

        if (p->nul[i] != 0) {
            out[o++] = 0;
        }

        out[o++] = '\r';
        out[o++] = '\n';
    }

    *len = o;

    return out;
}


void
mp_free(MPayload *p)
{
    for (int i = 0; i < p->n; i++) {
        free(p->id[i]);
        free(p->val[i]);
    }

    free(p->nested);
    memset(p, 0, sizeof *p);
}


int
mp_nested_plain(const MPayload *p, char *buf, int cap)
{
    static const char head[] = "S\r\nT";
    static const char tail[] = "\x1d\x00\r\n";
    size_t hl = sizeof head - 1;
    size_t tl = 4;

    buf[0] = '\0';

    if (p->nested_len < hl + tl || memcmp(p->nested, head, hl) != 0 ||
        memcmp(p->nested + p->nested_len - tl, tail, tl) != 0) {
        return 0;
    }

    size_t n = p->nested_len - hl - tl;

    for (size_t i = 0; i < n; i++) {
        if (p->nested[hl + i] < 0x20) {
            return 0;
        }
    }

    if (n >= usz(cap)) {
        return 0;
    }

    memcpy(buf, p->nested + hl, n);
    buf[n] = '\0';

    return 1;
}


void
mp_set_nested_plain(MPayload *p, const char *text)
{
    size_t n = strlen(text);

    free(p->nested);
    p->nested = malloc(n + 9);
    memcpy(p->nested, "S\r\nT", 4);
    memcpy(p->nested + 4, text, n);
    memcpy(p->nested + 4 + n, "\x1d\x00\r\n", 4);
    p->nested_len = n + 8;
}


/* -------------------------------------------------------------- numbers */
static int
pow10_of(int decimals)
{
    int s = 1;

    for (int i = 0; i < decimals; i++) {
        s *= 10;
    }

    return s;
}


const char *
marker_unit_name(int u)
{
    static const char *const names[] = {"in", "cm", "mm", "pt", "bp", "pc",
                                        "dd", "cc", "sp", "em", "ex"};

    return u >= 0 && u <= 10 ? names[u] : "in";
}


void
marker_fmt_num(int v, int decimals, int trim, char *buf, size_t cap)
{
    int scale = pow10_of(decimals);
    int whole = v / scale;
    int frac = v % scale;
    char fd[16];

    if (frac < 0) {
        frac = -frac;
    }

    if (decimals <= 0 || decimals > 9) {
        snprintf(buf, cap, "%d", v);

        return;
    }

    /* The fraction, padded with zeros on the left to its full width. */
    for (int i = decimals - 1; i >= 0; i--) {
        fd[i] = chr('0' + frac % 10);
        frac /= 10;
    }

    fd[decimals] = '\0';

    if (trim != 0) {
        int n = decimals;

        while (n > 0 && fd[n - 1] == '0') {
            fd[--n] = '\0';
        }
    }

    if (fd[0] == '\0') {
        snprintf(buf, cap, "%d", whole);
    } else {
        snprintf(buf, cap, "%d.%s", whole, fd);
    }
}


int
marker_parse_num(const char *s, int decimals, int *out)
{
    int scale = pow10_of(decimals);
    int whole = 0;
    int frac = 0;
    int fscale = scale;
    int digits = 0;

    while (*s == ' ') {
        s++;
    }

    while (is_digit(*s)) {
        if (whole > 1000000) {
            return -1;
        }

        whole = whole * 10 + (*s - '0');
        s++;
        digits++;
    }

    if (*s == '.' && decimals > 0) {
        s++;

        while (is_digit(*s)) {
            fscale /= 10;

            if (fscale == 0) {
                return -1;
            }

            frac += (*s - '0') * fscale;
            s++;
            digits++;
        }
    }

    while (*s == ' ') {
        s++;
    }

    if (digits == 0 || *s != '\0') {
        return -1;
    }

    *out = whole * scale + frac;

    return 0;
}


int
marker_parse_u32(const char *s, uint32_t *out)
{
    uint64_t v = 0;
    int digits = 0;

    while (*s == ' ') {
        s++;
    }

    while (is_digit(*s)) {
        v = v * 10 + uns(*s - '0');

        if (v > COUNTER_MAX) {
            return -1;
        }

        s++;
        digits++;
    }

    while (*s == ' ' || *s == '\r' || *s == '\n') {
        s++;
    }

    if (digits == 0 || *s != '\0') {
        return -1;
    }

    *out = v & UINT32_MAX;

    return 0;
}


/* ----------------------------------------------------- number styles */
static void
roman(uint32_t v, int upper, char *out, size_t cap)
{
    static const struct {
        uint32_t v;
        const char *s;
    } t[] = {{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"},
             {90, "XC"},  {50, "L"},   {40, "XL"}, {10, "X"},   {9, "IX"},
             {5, "V"},    {4, "IV"},   {1, "I"}};
    size_t n = 0;

    out[0] = '\0';

    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        while (v >= t[i].v) {
            for (const char *p = t[i].s; *p != '\0' && n + 1 < cap; p++) {
                out[n++] = chr(upper != 0 ? *p : to_lower(*p));
            }

            v -= t[i].v;
        }
    }

    out[n] = '\0';
}


/*
 * The number v in the style: 1 arabic, 2 lower case roman, 3 upper case
 * roman, 4 upper case letters, 5 lower case letters.  A counter holds 0 to
 * 4294967295 (COUNTER_MAX).  Roman numerals show 1 to 3999; the letters go
 * on after Z with AA, AB and so on, as the columns of a spreadsheet do, and
 * 4294967295 is "MWLQKWU" (7 letters).  A number that
 * a style cannot show (0 in roman and in letters, above 3999 in roman) is
 * shown in arabic digits.  The screen and the print both use this function.
 */
void
marker_number_text(int style, uint32_t v, char *out, size_t cap)
{
    if ((style == 2 || style == 3) && v >= 1 && v <= 3999) {
        roman(v, style == 3, out, cap);
    } else if ((style == 4 || style == 5) && v >= 1) {
        char rev[16];
        int n = 0;

        while (v > 0 && n < 15) {
            v--;
            rev[n++] = chr((style == 4 ? 'A' : 'a') + isz(v % 26));
            v /= 26;
        }

        for (int i = 0; i < n && uns(i) + 1 < cap; i++) {
            out[i] = rev[n - 1 - i];
            out[i + 1] = '\0';
        }
    } else {
        snprintf(out, cap, "%" PRIu32, v);
    }
}


/* ------------------------------------------------------- date and time */
static const char *const month_names[] = {
    "January", "February", "March",     "April",   "May",      "June",
    "July",    "August",   "September", "October", "November", "December"};


/* Does the text at s start with word, ignoring case? */
static int
starts_with(const char *s, const char *word)
{
    size_t n = strlen(word);

    for (size_t i = 0; i < n; i++) {
        if (to_lower(s[i]) != to_lower(word[i])) {
            return 0;
        }
    }

    return 1;
}


/*
 * Fill a date or time format.  The date words are Month, Mon, Year, YYYY,
 * YY, MM, M, DD and D; the time words are HH, H, MM, SS and am/pm.
 */
static void
fmt_when(const char *f, const struct tm *tm, int is_time, char *out, size_t cap)
{
    size_t o = 0;

    out[0] = '\0';

    while (*f != '\0' && o + 24 < cap) {
        char piece[32] = "";
        int used = 1;

        if (is_time == 0 && starts_with(f, "month")) {
            snprintf(piece, sizeof piece, "%s", month_names[tm->tm_mon % 12]);
            used = 5;
        } else if (is_time == 0 && starts_with(f, "mon")) {
            snprintf(piece, sizeof piece, "%.3s", month_names[tm->tm_mon % 12]);
            used = 3;
        } else if (is_time == 0 &&
                   (starts_with(f, "year") || starts_with(f, "yyyy"))) {
            snprintf(piece, sizeof piece, "%d", tm->tm_year + 1900);
            used = 4;
        } else if (is_time == 0 && starts_with(f, "yy")) {
            snprintf(piece, sizeof piece, "%02d", tm->tm_year % 100);
            used = 2;
        } else if (is_time == 0 && starts_with(f, "mm")) {
            snprintf(piece, sizeof piece, "%02d", tm->tm_mon + 1);
            used = 2;
        } else if (is_time == 0 && starts_with(f, "m")) {
            snprintf(piece, sizeof piece, "%d", tm->tm_mon + 1);
        } else if (is_time == 0 && starts_with(f, "dd")) {
            snprintf(piece, sizeof piece, "%02d", tm->tm_mday);
            used = 2;
        } else if (is_time == 0 && starts_with(f, "d")) {
            snprintf(piece, sizeof piece, "%d", tm->tm_mday);
        } else if (is_time != 0 && starts_with(f, "am/pm")) {
            snprintf(piece, sizeof piece, "%s", tm->tm_hour < 12 ? "am" : "pm");
            used = 5;
        } else if (is_time != 0 && starts_with(f, "hh")) {
            snprintf(piece, sizeof piece, "%02d", (tm->tm_hour + 11) % 12 + 1);
            used = 2;
        } else if (is_time != 0 && starts_with(f, "h")) {
            snprintf(piece, sizeof piece, "%d", (tm->tm_hour + 11) % 12 + 1);
        } else if (is_time != 0 && starts_with(f, "mm")) {
            snprintf(piece, sizeof piece, "%02d", tm->tm_min);
            used = 2;
        } else if (is_time != 0 && starts_with(f, "ss")) {
            snprintf(piece, sizeof piece, "%02d", tm->tm_sec);
            used = 2;
        } else {
            piece[0] = *f;
            piece[1] = '\0';
        }

        size_t pl = strlen(piece);

        memcpy(out + o, piece, pl);
        o += pl;
        out[o] = '\0';
        f += used;
    }
}


/* ----------------------------------------------------------- appearance */
static const Doc *cur_doc;
static char source_path[PATH_MAX];
static int source_set;


void
marker_set_doc(const Doc *d)
{
    cur_doc = d;
}


void
marker_set_source(const char *path)
{
    if (path == NULL) {
        source_set = 0;
        source_path[0] = '\0';

        return;
    }

    source_set = 1;
    snprintf(source_path, sizeof source_path, "%s", path);
}


/*
 * The path of the file that the markers Filename, Pathname, Filedate and
 * Filetime stand for, or NULL when there is none.
 */
static const char *
file_path(void)
{
    if (source_set != 0) {
        return source_path[0] != '\0' ? source_path : NULL;
    }

    return cur_doc != NULL ? cur_doc->path : NULL;
}


void
marker_doc_freed(const Doc *d)
{
    if (cur_doc == d) {
        cur_doc = NULL;
    }

    if (marker_forget_fn != NULL) {
        marker_forget_fn(d);
    }
}


static const char *
base_name(const char *path)
{
    const char *sl = strrchr(path, '/');

    return sl != NULL ? sl + 1 : path;
}


static const char *
field_or(const MPayload *p, const char *id, const char *dflt)
{
    const char *v = mp_get(p, id);

    return v != NULL ? v : dflt;
}


static int
field_int(const MPayload *p, const char *id, int dflt)
{
    const char *v = mp_get(p, id);

    return v != NULL ? atoi(v) : dflt;
}


/* The format of the document for dates or times, else the built-in one. */
static const char *
doc_default_fmt(const char *code, const char *dflt)
{
    const char *v = cur_doc != NULL ? doc_opt_get(cur_doc, code) : NULL;

    return v != NULL ? v : dflt;
}


/*
 * The text of the nested records of a marker, for labels and for the
 * equation: ordinary characters, a line feed for every end of line.  The
 * control codes that the editor leaves in a saved text (the position of the
 * cursor, 1C) are skipped.  Returns the length.
 */
static int
nested_text(const MPayload *p, char *buf, int cap)
{
    int n = 0;
    size_t i = 0;

    /* The nested text is the records "S" and "T<text>": skip to the T. */
    while (i < p->nested_len && p->nested[i] != 'T') {
        i++;
    }

    for (i++; i < p->nested_len && p->nested[i] != 0 && n < cap - 1; i++) {
        uint8_t c = p->nested[i];

        if (c == 0x1D) {
            buf[n++] = '\n';
        } else if (c == 0x12 || c == 0x10) {
            i += 2;
        } else if (c == 0x01 && i + 1 < p->nested_len) {
            buf[n++] = chr(p->nested[++i]);
        } else if (c >= 0x20) {
            buf[n++] = chr(c);
        }
    }

    while (n > 0 && buf[n - 1] == '\n') {
        n--;
    }

    buf[n] = '\0';

    return n;
}


/* The nested text on one line, for the label of an equation or a note. */
static void
nested_label(const MPayload *p, char *buf, size_t cap)
{
    int n = nested_text(p, buf, isz(cap));

    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            buf[i] = ' ';
        }
    }
}


static void
line_label(const MPayload *p, char *buf, size_t cap)
{
    char num[32];
    const char *len = mp_get(p, "2");
    const char *wt = mp_get(p, "3");
    const char *ctr = mp_get(p, "4");
    const char *str = mp_get(p, "1");

    if (len != NULL) {
        marker_fmt_num(atoi(len), 3, 1, num, sizeof num);
        snprintf(buf, cap, "LN Length:%s in", num);
    } else {
        snprintf(buf, cap, "LN Length:RM");
    }

    if (wt != NULL) {
        size_t n = strlen(buf);

        marker_fmt_num(atoi(wt), 3, 1, num, sizeof num);
        snprintf(buf + n, cap - n, ", Weight:%s in", num);
    }

    if (ctr != NULL) {
        size_t n = strlen(buf);

        snprintf(buf + n, cap - n, ", Center:%d", atoi(ctr));
    }

    if (str != NULL && str[0] != '\0') {
        size_t n = strlen(buf);

        snprintf(buf + n, cap - n, ", String:\"%s\"", str);
    }
}


/* ------------------------------------------------------- display modes */
const char *
marker_mode_name(int mode)
{
    static const char *const names[] = {"Diamond", "Name",   "Short",
                                        "Long",    "Hidden", "Inline"};

    return mode >= 0 && mode < 6 ? names[mode] : "?";
}


/*
 * The markers that marker_render makes a glyph of: their label is made here,
 * from the display mode, whatever that mode is.
 */
static int
glyph_marker(int type)
{
    return type == 1 || type == 3 || type == 6 || type == 11 || type == 15 ||
           type == 29;
}


int
marker_default_mode(int type)
{
    switch (type) {
        case 2: /* Cross-reference: its text */
        case 17: /* Counter: its number */
            return MM_LONG;

        case 9: /* Equation: the picture of it */
        case 25: /* Picture: the image */
        case 37: /* Keycap: the legend in a box */
        case MT_LATEX: /* LaTeX: the picture that LaTeX makes of it */
            return MM_INLINE;

        case 22: /* Page Number */
        case 28: /* Section */
        case 34: /* Else */
        case 35: /* Endif */
            return MM_NAME;

        case 1: /* Note */
        case 3: /* Index */
        case 6: /* Comment */
        case 11: /* Figure */
        case 15: /* Font */
        case 18: /* Line */
        case 23: /* Page Break */
        case 29: /* Table */
        case 31: /* Whitespace */
        case 36: /* Orientation */
        case 39: /* Column Break */
            return MM_SHORT;

        default:
            return MM_LONG;
    }
}


int
marker_mode(const Doc *d, int type)
{
    char code[16];
    const char *v;

    if (d == NULL) {
        return marker_default_mode(type);
    }

    snprintf(code, sizeof code, "OM%d,", type);
    v = doc_opt_get(d, code);

    return v != NULL ? atoi(v) : marker_default_mode(type);
}


void
marker_set_mode(Doc *d, int type, int mode)
{
    char code[16];
    char val[16];

    snprintf(code, sizeof code, "OM%d,", type);
    snprintf(val, sizeof val, "%d", mode);
    doc_opt_set(d, code, mode == marker_default_mode(type) ? NULL : val, 0);
}


int
marker_show_hidden(const Doc *d)
{
    const char *v = d != NULL ? doc_opt_get(d, "OH") : NULL;

    return v != NULL && atoi(v) != 0;
}


/* The unit of a Whitespace marker as TeX writes it: in when none is given. */
static const char *
whitespace_unit(const MPayload *p)
{
    int u = field_int(p, "U", 0);

    return unit_choices[u >= 1 && u <= 10 ? u : 0];
}


/* The treatment of a Whitespace marker; Place All is the default. */
static const char *
whitespace_treatment(const MPayload *p)
{
    int t = field_int(p, "2", 1);

    return treatment_choices[t >= 0 && t <= 2 ? t : 1];
}


/*
 * The label of a marker in the Name, Short or Long mode, for the markers
 * whose labels were captured from sample documents.  Returns 0 for the rest,
 * which keep the label that marker_render made.
 */
static int
mode_label(int type, int mode, const MPayload *p, char *buf, size_t cap)
{
    char tmp[96];

    if (mode == MM_NAME) {
        static const struct {
            int type;
            const char *name;
        } names[] = {{1, "NOTE"},       {2, "XREF"},    {3, "INDEX"},
                     {4, "KEEP"},       {5, "AUTHOR"},  {6, "COM"},
                     {7, "DATE"},       {8, "DESC"},    {9, "EQN"},
                     {11, "FIG"},    {12, "FDATE"},
                     {13, "FNAME"},     {14, "FTIME"},  {15, "FONT"},
                     {16, "INCLUDE"},   {17, "C"},      {18, "LN"},
                     {19, "MERGE"},     {20, "MARK"},   {21, "ESC"},
                     {22, "PAGE"},      {23, "PB"},     {24, "PATH"},
                     {25, "PIC"},       {26, "PRINTER"}, {27, "REV"},
                     {28, "SECTION"},   {29, "TABLE"},  {30, "TIME"},
                     {31, "WS"},        {33, "IF"},
                     {34, "ELSE"},      {35, "ENDIF"},  {36, "ORIENTATION"},
                     {37, "KEY"},       {39, "CB"},     {41, "LaTeX"}};

        for (size_t i = 0; i < NF(names); i++) {
            if (names[i].type == type) {
                str_copy(buf, cap, names[i].name);

                return 1;
            }
        }

        return 0;
    }

    switch (type) {
        case 6:
            nested_label(p, tmp, sizeof tmp);
            snprintf(buf, cap, "%s \"%s\"", mode == MM_SHORT ? "COM" :
                                                               "COMMENT",
                     tmp);

            return 1;

        case 1:
            if (mode == MM_SHORT) {
                str_copy(buf, cap, "NOTE");
            } else {
                nested_label(p, tmp, sizeof tmp);
                snprintf(buf, cap, "NOTE \"%s\"", tmp);
            }

            return 1;

        case 8:
            str_copy(buf, cap, mode == MM_SHORT ? "DESC" : "DESCRIPTION");

            return 1;

        case 9:
            nested_label(p, tmp, sizeof tmp);
            snprintf(buf, cap, "%s %s", mode == MM_SHORT ? "EQN" :
                                                           "EQUATION",
                     tmp);

            return 1;

        case 11:
            snprintf(buf, cap, "%s \"%s\"", mode == MM_SHORT ? "FIG" :
                                                               "FIGURE",
                     field_or(p, "1", ""));

            return 1;

        case 17: {
            int n = field_int(p, "1", 1);
            char code[16];
            const char *desc = NULL;

            /* The description of the counter is a comment for the reader. */
            if (cur_doc != NULL && n >= 1 && n <= MAX_COUNTERS) {
                snprintf(code, sizeof code, "OT%dN", n);
                desc = doc_opt_get(cur_doc, code);
            }

            snprintf(buf, cap, "%s%d%s%s", mode == MM_SHORT ? "C" : "COUNTER",
                     n, desc != NULL && desc[0] != '\0' ? ", " : "",
                     desc != NULL ? desc : "");

            return 1;
        }

        case 18:
            if (mode == MM_LONG) {
                line_label(p, buf, cap);

                if (strncmp(buf, "LN", 2) == 0) {
                    memmove(buf + 4, buf + 2, strlen(buf + 2) + 1);
                    memcpy(buf, "LINE", 4);
                }

                return 1;
            }

            return 0;

        case 19:
            if (mode == MM_SHORT) {
                snprintf(buf, cap, "MERGE \"%s\"", field_or(p, "1", ""));
            } else {
                snprintf(buf, cap, "MERGE \"%s\", Delete:%s",
                         field_or(p, "1", ""),
                         field_int(p, "2", 0) == 0 ? "Yes" : "No");
            }

            return 1;

        case 31:
            if (mode == MM_LONG) {
                marker_fmt_num(field_int(p, "1", 0), 3, 1, tmp, sizeof tmp);
                snprintf(buf, cap, "WHITESPACE %s %s, %s", tmp,
                         whitespace_unit(p), whitespace_treatment(p));

                return 1;
            }

            return 0;

        case 39:
            str_copy(buf, cap, mode == MM_SHORT ? "CB" : "COLUMN BREAK");

            return 1;

        case MT_LATEX:
            str_copy(buf, cap, "LaTeX instructions");

            return 1;

        case 3:
            snprintf(buf, cap, "INDEX %s", field_or(p, "2", ""));

            return 1;

        case 15:
            str_copy(buf, cap, "FONT");

            return 1;

        case 29:
            snprintf(buf, cap, "TABLE \"%s\"", field_or(p, "1", ""));

            return 1;

        case 4:
        case 5:
        case 7:
        case 12:
        case 13:
        case 14:
        case 24:
        case 26:
        case 27:
        case 30:
        case 33:
        case 34:
        case 35:
        case 36:
        case 37:
            return 0;

        default:
            return 0;
    }
}


/* Set while marker_long_label makes a label: every marker is in Long mode. */
static int force_long;


/* Change the label the way the display mode of the marker's type says. */
static MRender
apply_mode(const Tok *k, MRender r, const MPayload *p, char *buf, size_t cap)
{
    int dflt;
    int mode;

    /* A Print Error is shown in every mode and never printed. */
    if (tok_mtype(k) == MT_PRINT_ERROR) {
        if (marker_print_mode != 0 || marker_hide_perr != 0) {
            buf[0] = '\0';

            return MR_TEXT;
        }

        snprintf(buf, cap, "PRINT ERROR: %s [%s]", field_or(p, "1", ""),
                 field_or(p, "2", ""));

        return MR_ERROR;
    }

    dflt = marker_default_mode(tok_mtype(k));
    mode = force_long != 0 ? MM_LONG : marker_mode(cur_doc, tok_mtype(k));

    /*
     * A Picture marker whose file cannot be used is an error, and it is shown
     * as one in every display mode (but Hidden, which shows nothing): a dim
     * label that says what is wrong, so that it cannot be taken for the
     * Short or Long form of a picture that is set to show.
     */
    if (tok_mtype(k) == 25 && marker_print_mode == 0 && force_long == 0 &&
        marker_picture_fn != NULL && mode != MM_HIDDEN) {
        const char *name = field_or(p, "1", "");
        char why[48];

        if (name[0] != '\0' &&
            marker_picture_fn(file_path(), name, why, sizeof why) != 0) {
            if (mode == MM_LONG) {
                snprintf(buf, cap, "PICTURE \"%s\": %s", name, why);
            } else {
                snprintf(buf, cap, "PIC %s: %s", name, why);
            }

            return MR_MISSING;
        }
    }

    /* A Keep marker is never printed: it prints nothing. */
    if (tok_mtype(k) == 4 && marker_print_mode != 0) {
        buf[0] = '\0';

        return MR_TEXT;
    }

    /*
     * The picture of an Equation, a Picture or a Keycap that the display mode
     * Inline asks for could not be made: the marker is drawn by the settings
     * of the markers in the Short form, and not as an empty box
     * (docs/KEYCAP-FALLBACK.txt).
     */
    if (mode == MM_INLINE && marker_print_mode == 0 && force_long == 0 &&
        marker_failed_fn != NULL &&
        (tok_mtype(k) == 9 || tok_mtype(k) == 25 || tok_mtype(k) == 37 ||
         tok_mtype(k) == MT_LATEX) &&
        marker_failed_fn(k) != 0) {
        mode = MM_SHORT;
    }

    /* A comment is printed in a draft print only. */
    if (tok_mtype(k) == 6 && marker_print_mode != 0) {
        if (marker_print_mode == 2) {
            buf[0] = '\0';
        } else {
            char text[160];

            nested_label(p, text, sizeof text);
            snprintf(buf, cap, "[Comment by %s, %s: %s]",
                     field_or(p, "1", ""), field_or(p, "2", ""), text);
        }

        return MR_TEXT;
    }

    /* A counter and a cross-reference show their text: in a print always,
       on the screen in the long mode. */
    if ((tok_mtype(k) == 17 || tok_mtype(k) == 2 || tok_mtype(k) == 1) &&
        marker_eval_fn != NULL &&
        (marker_print_mode != 0 || (mode == MM_LONG && tok_mtype(k) != 1))) {
        if (marker_eval_fn(k, buf, cap) != 0) {
            return marker_print_mode != 0 ? MR_TEXT : MR_VALUE;
        }
    }

    /* A keycap is its legend in a box (when the picture of it is not shown);
       a printout of text has [legend]. */
    if (tok_mtype(k) == 37 && (marker_print_mode != 0 || mode == MM_INLINE)) {
        if (marker_print_mode != 0) {
            snprintf(buf, cap, "[%s]", field_or(p, "1", ""));

            return MR_TEXT;
        }

        snprintf(buf, cap, " %s ", field_or(p, "1", ""));

        return MR_KEYCAP;
    }

    if (mode == MM_HIDDEN && marker_show_hidden(cur_doc) != 0) {
        mode = MM_SHORT;
    }

    if (mode == dflt && glyph_marker(tok_mtype(k)) == 0) {
        return r;
    }

    switch (mode) {
        case MM_DIAMOND:
            return MR_GLYPH;

        case MM_HIDDEN:
            buf[0] = '\0';

            return MR_TEXT;

        default:
            if (mode_label(tok_mtype(k), mode, p, buf, cap) == 0 &&
                glyph_marker(tok_mtype(k)) != 0) {
                /* A marker that is drawn as a glyph has no label yet. */
                snprintf(buf, cap, "%s", marker_name(tok_mtype(k)));
            }

            return MR_BRACKET;
    }
}


void
marker_long_label(const Tok *k, char *buf, int cap)
{
    int saved = marker_print_mode;

    marker_print_mode = 0;
    force_long = 1;
    marker_render(k, buf, cap);
    force_long = 0;
    marker_print_mode = saved;

    if (buf[0] == '\0') {
        snprintf(buf, usz(cap), "%s", marker_name(tok_mtype(k)));
    }
}


int
marker_value(const Tok *k, char *buf, int cap)
{
    MPayload p;
    int ok = 1;

    buf[0] = '\0';

    switch (tok_mtype(k)) {
        case 5:
        case 8: {
            const char *v = cur_doc != NULL ?
                                doc_header_get(cur_doc,
                                               tok_mtype(k) == 5 ? "MA" : "MD") :
                                NULL;

            snprintf(buf, usz(cap), "%s", v != NULL ? v : "");
            break;
        }

        case 12:
        case 14: {
            struct stat st;
            time_t t = time(NULL);
            struct tm tm;

            if (file_path() != NULL && stat(file_path(), &st) == 0) {
                t = st.st_mtime;
            }

            tm = *localtime(&t);
            mp_parse(&p, marker_info(tok_mtype(k)), tok_mdata(k), tok_mlen(k));

            if (tok_mtype(k) == 12) {
                fmt_when(field_or(&p, "1", doc_default_fmt("PD",
                                                           "Month D, Year")),
                         &tm, 0, buf, usz(cap));
            } else {
                fmt_when(field_or(&p, "1", doc_default_fmt("PT",
                                                           "H:MM am/pm")),
                         &tm, 1, buf, usz(cap));
            }

            mp_free(&p);
            break;
        }

        case 7:
        case 13:
        case 24:
        case 26:
        case 27:
        case 30: {
            int saved = marker_print_mode;

            marker_print_mode = 0;
            marker_render(k, buf, cap);
            marker_print_mode = saved;
            break;
        }

        default:
            ok = 0;
            break;
    }

    return ok;
}


int
marker_equation_text(const Tok *k, char *buf, int cap)
{
    MPayload p;

    if (tok_mtype(k) != 9) {
        return -1;
    }

    mp_parse(&p, marker_info(9), tok_mdata(k), tok_mlen(k));

    int n = nested_text(&p, buf, cap);

    mp_free(&p);

    return n;
}


int
marker_latex_text(const Tok *k, char *buf, int cap)
{
    MPayload p;
    int n = 0;
    int ok = 1;
    size_t i = 0;

    if (k->kind != TK_MARKER || tok_mtype(k) != MT_LATEX || cap < 1) {
        return -1;
    }

    mp_parse(&p, marker_info(MT_LATEX), tok_mdata(k), tok_mlen(k));

    /* The nested text is a list of records, each ended by CR LF: S starts
       the text, B another block, T gives the characters of a block.  The
       text of a block ends with the end of a line, so the blocks follow each
       other line by line. */
    while (i < p.nested_len && ok != 0) {
        uint8_t kind = p.nested[i];

        if (kind != 'T') {
            while (i + 1 < p.nested_len &&
                   !(p.nested[i] == '\r' && p.nested[i + 1] == '\n')) {
                i++;
            }

            i += 2;

            continue;
        }

        for (i++; i < p.nested_len && p.nested[i] != 0; i++) {
            uint8_t c = p.nested[i];
            int out = -1;

            if (c == 0x1D) {
                out = '\n';
            } else if (c == 0x10) {
                out = '\t';
                i += 2;
            } else if (c == 0x12) {
                i += 2;
            } else if (c == 0x1E) {
                out = 0xFF;
            } else if (c == 0x01 && i + 1 < p.nested_len) {
                out = p.nested[++i];
            } else if (c >= 0x20) {
                out = c;
            }

            if (out >= 0) {
                if (n + 1 >= cap) {
                    ok = 0;
                    break;
                }

                buf[n++] = chr(out);
            }
        }

        /* The text of a block ends with a NUL byte and CR LF. */
        i += 3;
    }

    mp_free(&p);

    if (ok == 0) {
        return -1;
    }

    /* The end of the last line is not part of the text. */
    while (n > 0 && buf[n - 1] == '\n') {
        n--;
    }

    buf[n] = '\0';

    return n;
}


int
marker_nested_text(const Tok *k, char *buf, int cap)
{
    const MInfo *mi = marker_info(tok_mtype(k));
    MPayload p;
    int n;

    if (k->kind != TK_MARKER || mi == NULL || mi->nested == 0) {
        return -1;
    }

    mp_parse(&p, mi, tok_mdata(k), tok_mlen(k));
    n = nested_text(&p, buf, cap);
    mp_free(&p);

    return n;
}


int
marker_picture_in_text(const Tok *k)
{
    MPayload p;
    int r;

    if (k->kind != TK_MARKER || tok_mtype(k) != 25) {
        return 0;
    }

    mp_parse(&p, marker_info(25), tok_mdata(k), tok_mlen(k));
    r = field_int(&p, "5", 0) == 2;
    mp_free(&p);

    return r;
}


const char *
marker_length_unit(const Tok *k)
{
    MPayload p;
    const char *u;

    mp_parse(&p, marker_info(tok_mtype(k)), tok_mdata(k), tok_mlen(k));
    u = whitespace_unit(&p);
    mp_free(&p);

    return u;
}


MRender
marker_render(const Tok *k, char *buf, int cap)
{
    const MInfo *mi = marker_info(tok_mtype(k));
    MPayload p;
    MRender r = MR_BRACKET;
    size_t bc = usz(cap);
    char tmp[96];
    time_t now = time(NULL);
    struct tm tm = *localtime(&now);

    buf[0] = '\0';
    mp_parse(&p, mi, tok_mdata(k), tok_mlen(k));

    switch (tok_mtype(k)) {
        case 1: /* Note */
        case 3: /* Index */
        case 6: /* Comment */
        case 11: /* Figure */
        case 15: /* Font */
        case 29: /* Table */
            r = MR_GLYPH;
            break;

        case 2: {
            const char *num = mp_get(&p, "XN");

            snprintf(buf, bc, "XREF %s", num != NULL ? num : "");
            break;
        }

        case 4: {
            int v = field_int(&p, "1", 0);

            if (v == 2) {
                snprintf(buf, bc, "End Keep");
            } else {
                snprintf(buf, bc, "Start Keep (%s)",
                         v == 1 ? "Floating" : "Fixed");
            }

            break;
        }

        case 5:
            snprintf(buf, bc, "AUTHOR");
            break;

        case 7:
            fmt_when(field_or(&p, "1",
                              doc_default_fmt("PD", "Month D, Year")),
                     &tm, 0, buf, bc);
            break;

        case 8:
            snprintf(buf, bc, "DESCRIPTION");
            break;

        case 9:
            nested_label(&p, tmp, sizeof tmp);
            snprintf(buf, bc, "EQN %s", tmp);
            break;

        case 12:
            snprintf(buf, bc, "FILEDATE");
            break;

        case 13:
            name_to_cells(file_path() != NULL ? base_name(file_path()) :
                                               "FILENAME", buf, bc);

            for (char *c = buf; *c != '\0'; c++) {
                *c = chr(to_upper(*c));
            }

            break;

        case 14:
            snprintf(buf, bc, "FILETIME");
            break;

        case 16:
            snprintf(buf, bc, "INCLUDE \"%s\"", field_or(&p, "1", ""));
            break;

        case 17:
            /* The label has no start value. */
            snprintf(buf, bc, "C%d", field_int(&p, "1", 1));
            break;

        case 18:
            line_label(&p, buf, bc);
            break;

        case 19:
            snprintf(buf, bc, "MERGE \"%s\", Delete:%s", field_or(&p, "1", ""),
                     field_int(&p, "2", 0) == 0 ? "Yes" : "No");
            break;

        case 22:
            snprintf(buf, bc, "PAGE");
            break;

        case 23: {
            int v = field_int(&p, "1", 0);

            snprintf(buf, bc, "PB%s",
                     v == 1 ? " Odd Page" :
                     v == 2 ? " Even Page" :
                              "");
            break;
        }

        case 24:
            name_to_cells(file_path() != NULL ? file_path() : "PATHNAME", buf,
                         bc);
            break;

        case 25:
            snprintf(buf, bc, "PIC %s", field_or(&p, "1", ""));
            break;

        case 26: {
            const char *name =
                cur_doc != NULL ? doc_header_get(cur_doc, "DN") : NULL;

            snprintf(buf, bc, "%s", name != NULL ? name : "Generic");
            break;
        }

        case 27: {
            const char *rev =
                cur_doc != NULL ? doc_header_get(cur_doc, "MR") : NULL;

            marker_fmt_num(rev != NULL ? atoi(rev) : 1000, 3, 0, tmp,
                           sizeof tmp);
            snprintf(buf, bc, "%s", tmp);
            break;
        }

        case 28:
            snprintf(buf, bc, "SECTION");
            break;

        case 30:
            fmt_when(field_or(&p, "1",
                              doc_default_fmt("PT", "H:MM am/pm")),
                     &tm, 1, buf, bc);
            break;

        case 31:
            marker_fmt_num(field_int(&p, "1", 0), 3, 1, tmp, sizeof tmp);
            snprintf(buf, bc, "WS %s %s", tmp, whitespace_unit(&p));
            break;

        case 33:
            snprintf(buf, bc, "IF \"%s\"", field_or(&p, "1", ""));
            break;

        case 34:
            snprintf(buf, bc, "ELSE");
            break;

        case 35:
            snprintf(buf, bc, "ENDIF");
            break;

        case 36:
            snprintf(buf, bc, "ORIENTATION%s",
                     field_int(&p, "1", 1) == 2 ? " Landscape" : "");
            break;

        case 37:
            snprintf(buf, bc, "Key: %s", field_or(&p, "1", ""));
            break;

        case 39:
            snprintf(buf, bc, "CB");
            break;

        case MT_LATEX:
            snprintf(buf, bc, "LaTeX instructions");
            break;

        default:
            snprintf(buf, bc, "M%d", tok_mtype(k));
            break;
    }

    r = apply_mode(k, r, &p, buf, bc);
    mp_free(&p);

    return r;
}


/* ----------------------------------------------------------- keycap format */
static int
keycap_rec(const Doc *d, const char *code, int dflt)
{
    const char *v = doc_opt_get(d, code);

    return v != NULL && v[0] != '\0' ? atoi(v) : dflt;
}


void
keycap_format_get(const Doc *d, KeycapFmt *f)
{
    if (d == NULL) {
        f->fg = 0;
        f->bg = 1000;
        f->scale = 1000;
        f->font = 2;

        return;
    }

    f->fg = keycap_rec(d, "OKF", 0);
    f->bg = keycap_rec(d, "OKB", 1000);
    f->scale = keycap_rec(d, "OKS", 1000);
    f->font = keycap_rec(d, "OKT", 2);

    if (f->font < 1 || f->font > 20) {
        f->font = 2;
    }
}


static void
keycap_rec_set(Doc *d, const char *code, int v, int dflt)
{
    char val[16];
    const char *cur = doc_opt_get(d, code);

    snprintf(val, sizeof val, "%d", v);

    if (v == dflt) {
        if (cur != NULL) {
            doc_opt_set(d, code, NULL, 0);
        }
    } else if (cur == NULL || strcmp(cur, val) != 0) {
        doc_opt_set(d, code, val, 0);
    }
}


void
keycap_format_set(Doc *d, const KeycapFmt *f)
{
    keycap_rec_set(d, "OKF", f->fg, 0);
    keycap_rec_set(d, "OKB", f->bg, 1000);
    keycap_rec_set(d, "OKS", f->scale, 1000);
    keycap_rec_set(d, "OKT", f->font, 2);
}



int
keycap_values(const Doc *d, const Tok *k, KeycapFmt *out)
{
    MPayload p;
    const char *v;

    if (k->kind != TK_MARKER || tok_mtype(k) != 37) {
        return -1;
    }

    keycap_format_get(d, out);
    mp_parse(&p, marker_info(37), tok_mdata(k), tok_mlen(k));
    v = mp_get(&p, "2");
    out->fg = v != NULL ? atoi(v) : out->fg;
    v = mp_get(&p, "3");
    out->bg = v != NULL ? atoi(v) : out->bg;
    v = mp_get(&p, "4");
    out->scale = v != NULL ? atoi(v) : out->scale;
    v = mp_get(&p, "5");
    out->font = v != NULL ? atoi(v) : out->font;
    mp_free(&p);

    return 0;
}


int
block_only_keep(const Block *b)
{
    int found = 0;

    for (int k = 0; k < b->ncols; k++) {
        const Text *t = &b->cols[k].text;

        for (int i = 0; i < t->n; i++) {
            if (t->v[i].kind == TK_MARKER && tok_mtype(&t->v[i]) == 4) {
                found = 1;
            } else if (t->v[i].kind != TK_NL) {
                return 0;
            }
        }
    }

    return found;
}
