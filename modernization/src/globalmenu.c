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

/* globalmenu.c - the Global menu panels; see globalmenu.h. */
#include "util.h"
#include "globalmenu.h"
#include "fonts.h"
#include "widgets.h"
#include "config.h"
#include "marker.h"
#include "note.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NFONTS 20

/* The counters are shown nine to a page. */
#define COUNTER_PAGES ((MAX_COUNTERS + 8) / 9)


void
globalmenu_typeface(Editor *e)
{
    static char labels[NFONTS + 1][32];
    WField f[NFONTS + 1];
    Doc *d = e->doc;
    const char *printer = doc_header_get(d, "DN");

    for (int i = 0; i < NFONTS; i++) {
        memset(&f[i], 0, sizeof f[i]);
        snprintf(labels[i], sizeof labels[i], "Font %d", i + 1);
        f[i].label = labels[i];
        f[i].kind = WF_TEXT;
        doc_font_name(d, i + 1, f[i].val, 32);
    }

    /*
     * The format also lists a typeface and a size for every font, taken
     * from the printer's fonts.  This program has only the generic printer,
     * with one typeface at 10 pitch, so there is nothing to choose.
     */
    memset(&f[NFONTS], 0, sizeof f[NFONTS]);
    f[NFONTS].label = "Printer";
    f[NFONTS].kind = WF_INFO;
    snprintf(f[NFONTS].val, sizeof f[NFONTS].val, "%s, Generic 10.0 pitch",
             printer != NULL ? printer : "Generic");

    if (popup_form("Typeface List", f, NFONTS + 1, NULL) == 0) {
        return;
    }

    for (int i = 0; i < NFONTS; i++) {
        doc_set_font_name(d, i + 1, f[i].val);
    }

    ed_invalidate(e);
}


/* ================================================================ Format */
/*
 * Global, Format.  Six panels are stacked: Global Format and Level
 * 1 to Level 5 Format, each one screen of fields.  A screen of 24 rows has
 * no room for that many fields in one column, so every scope is shown as
 * two panels: the headline and text rulers, and the spacing.  PgUp shows
 * the next panel, PgDn the previous one; Ins applies all of them.
 *
 * The settings are the F records of the header (DOC-FORMAT.txt).  A level
 * panel shows what the level takes from the global settings in brackets;
 * only a field that was changed gets a record of its own.
 */
enum {
    FK_ATTR,
    FK_JUST,
    FK_INDENT,
    FK_FONT,
    FK_TABS,
    FK_SPACE,
    FK_NUM,
    FK_YN,
    FK_FIRST,
    FK_PRINT,
    FK_RULER,
    FK_TAG,
    FK_LNMODE,
    FK_LNSIDE,
    FK_LNSIZE,
    FK_LNFONT,
    FK_LNFMT,
    FK_CLEVEL,
    FK_LIST,
    FK_COUNT
};

typedef struct {
    const char *label;
    const char *code;
    int kind;
    const char *dflt; /* the value of a record that is not there */
} FmtDef;

#define NSCOPES 6
/* The global scope has four panels, each level two. */
#define NGLOBAL 4
#define NPANELS (NGLOBAL + (NSCOPES - 1) * 2)
#define MAXF 20
#define MAXCH 24

static const FmtDef rulers_part[] = {
    {"Headline Attributes", "HA", FK_ATTR, "0"},
    {"Headline Justification", "HJ", FK_JUST, "0"},
    {"Headline Indent", "HI", FK_INDENT, "0"},
    {"Headline Font", "HF", FK_FONT, "4"},
    {"Headline Ruler", "HT", FK_TABS, ""},
    {"Text Attributes", "TA", FK_ATTR, "0"},
    {"Text Justification", "TJ", FK_JUST, "0"},
    {"Text Indent", "TI", FK_INDENT, "0"},
    {"Text Font", "TF", FK_FONT, "2"},
    {"Text Ruler", "TT", FK_TABS, ""}};

static const FmtDef global_space_part[] = {
    {"Line Spacing", "SL", FK_SPACE, "100"},
    {"Headline Spacing", "SH", FK_SPACE, "150"},
    {"Block Spacing", "ST", FK_SPACE, "150"},
    {"Subsection Spacing", "SC", FK_SPACE, "150"},
    {"Peer Section Spacing", "SS", FK_SPACE, "200"},
    {"Indent per Level", "I", FK_NUM, "0"},
    {"Page Break Level", "LP", FK_NUM, "0"},
    {"Page Number Level", "LG", FK_NUM, "0"},
    {"Auto Numbering?", "LA", FK_YN, "1"},
    {"Auto Numbering to Level", "LN", FK_NUM, "99"},
    {"First Block Type", "B", FK_FIRST, "1"},
    {"Print", "P", FK_PRINT, "0"},
    {"Ruler", "U", FK_RULER, "1"},
    {"Winding Columns", "WN", FK_NUM, "1"}};

static const FmtDef level_space_part[] = {
    {"Line Spacing", "SL", FK_SPACE, "100"},
    {"Headline Spacing", "SH", FK_SPACE, "150"},
    {"Block Spacing", "ST", FK_SPACE, "150"},
    {"Subsection Spacing", "SC", FK_SPACE, "150"},
    {"Peer Section Spacing", "SS", FK_SPACE, "200"},
    {"Indent to Next Level", "I", FK_NUM, "0"},
    {"First Block Type", "B", FK_FIRST, "1"},
    {"Section Tag", "G", FK_TAG, ""},
    {"Winding Columns", "WN", FK_NUM, "1"}};

/*
 * The Line Numbering panel is a third panel of the global settings.  Its
 * records have the codes Z followed by a letter; a document that does not
 * number its lines has none of them (LINENUMBERING-LATEX.txt).
 */
static const FmtDef lineno_part[] = {
    {"Numbering", "ZM", FK_LNMODE, "0"},
    {"Number Every", "ZE", FK_NUM, "1"},
    {"Side", "ZS", FK_LNSIDE, "0"},
    {"Number Size", "ZZ", FK_LNSIZE, "0"},
    {"Number Font", "ZY", FK_LNFONT, "0"},
    {"Number Format", "ZF", FK_LNFMT, "0"},
    {"Distance", "ZD", FK_SPACE, "15"},
    {"Equations", "ZQ", FK_YN, "0"}};

/*
 * The Contents and Lists panel is the fourth panel of the global settings:
 * the level of the table of contents (the record YCL, 0 for none) and, for
 * each list, whether it is made: Automatic (no record: when the document has
 * a marker of the kind), On or Off (docs/AUTOLISTS.txt).  The counts of the
 * markers are shown beside the lists.
 */
static const FmtDef lists_part[] = {
    {"Table of Contents Level", "YCL", FK_CLEVEL, "99"},
    {"Table of Figures", "ZLF", FK_LIST, ""},
    {"Figure Markers", "", FK_COUNT, ""},
    {"Table of Pictures", "ZLP", FK_LIST, ""},
    {"Picture Markers", "", FK_COUNT, ""},
    {"Table of Tables", "ZLT", FK_LIST, ""},
    {"Table Markers", "", FK_COUNT, ""},
    {"Index", "ZLI", FK_LIST, ""},
    {"Index Markers", "", FK_COUNT, ""}};

static const char *const list_names[] = {"Automatic", "On", "Off"};
static const char *const list_vals[] = {"", "1", "0"};

static const char *const attr_names[] = {"Normal", "Bold", "Italic",
                                         "Underscore Words"};
static const char *const attr_vals[] = {"0", "1", "4", "2"};
static const char *const just_names[] = {"Left", "Right", "Center", "Even"};
static const char *const just_vals[] = {"0", "1", "2", "3"};
static const char *const indent_names[] = {"None", "First", "All but First",
                                           "All"};
static const char *const indent_vals[] = {"0", "1", "2", "3"};
static const char *const yn_names[] = {"No", "Yes"};
static const char *const yn_vals[] = {"0", "1"};
static const char *const first_names[] = {"Headline", "Joined"};
static const char *const first_vals[] = {"1", "2"};
static const char *const print_names[] = {"Headlines and Text",
                                          "Only Headlines", "Only Text"};
static const char *const print_vals[] = {"0", "1", "2"};
static const char *const ruler_names[] = {"Relative", "10/inch", "12/inch",
                                          "15/inch"};
static const char *const ruler_vals[] = {"1", "2", "3", "4"};
static const char *const lnmode_names[] = {"Off", "Pagewise", "Running",
                                           "Each Section"};
static const char *const lnmode_vals[] = {"0", "1", "2", "3"};
static const char *const lnside_names[] = {"Left", "Right", "Outer", "Inner"};
static const char *const lnside_vals[] = {"0", "1", "2", "3"};
static const char *const lnsize_names[] = {"Tiny", "Script", "Footnote"};
static const char *const lnsize_vals[] = {"0", "1", "2"};
static const char *const lnfont_names[] = {"Sans", "Roman", "Typewriter"};
static const char *const lnfont_vals[] = {"0", "1", "2"};
static const char *const lnfmt_names[] = {"Arabic", "Roman", "Capital Roman"};
static const char *const lnfmt_vals[] = {"0", "1", "2"};


/*
 * The panels in the order of the ring: the global headline and text panel,
 * the global spacing panel, the Line Numbering panel and the Contents and
 * Lists panel, then two panels for each of the five levels.
 */
static int
panel_scope(int p)
{
    return p < NGLOBAL ? 0 : (p - NGLOBAL) / 2 + 1;
}


/* The part of a scope that panel p shows: 0 rulers, 1 spacing, 2 numbering. */
static int
panel_part(int p)
{
    return p < NGLOBAL ? p : (p - NGLOBAL) % 2;
}


typedef struct {
    WField f[NPANELS][MAXF];
    int n[NPANELS];
    const FmtDef *def[NPANELS];
    const char *ch[NPANELS][MAXF][MAXCH + 1];
    char inh[NPANELS][MAXF][64];
    char extra[NPANELS][MAXF][24];
    char fontname[NFONTS][32];
    char init[NPANELS][MAXF][64];
    int init_sel[NPANELS][MAXF];
} FmtState;

static FmtState fs;
static Editor *fmt_ed;


/* The names and record values of a field that is a choice. */
static int
fmt_choices(const FmtDef *def, const char *const **names,
            const char *const **vals)
{
    switch (def->kind) {
        case FK_ATTR:
            *names = attr_names;
            *vals = attr_vals;

            return 4;

        case FK_JUST:
            *names = just_names;
            *vals = just_vals;

            return 4;

        case FK_INDENT:
            *names = indent_names;
            *vals = indent_vals;

            return 4;

        case FK_YN:
            *names = yn_names;
            *vals = yn_vals;

            return 2;

        case FK_FIRST:
            *names = first_names;
            *vals = first_vals;

            return 2;

        case FK_PRINT:
            *names = print_names;
            *vals = print_vals;

            return 3;

        case FK_RULER:
            *names = ruler_names;
            *vals = ruler_vals;

            return 4;

        case FK_LNMODE:
            *names = lnmode_names;
            *vals = lnmode_vals;

            return 4;

        case FK_LNSIDE:
            *names = lnside_names;
            *vals = lnside_vals;

            return 4;

        case FK_LNSIZE:
            *names = lnsize_names;
            *vals = lnsize_vals;

            return 3;

        case FK_LNFONT:
            *names = lnfont_names;
            *vals = lnfont_vals;

            return 3;

        case FK_LNFMT:
            *names = lnfmt_names;
            *vals = lnfmt_vals;

            return 3;

        case FK_LIST:
            *names = list_names;
            *vals = list_vals;

            return 3;

        default:
            return 0;
    }
}


/* Spacing is kept in hundredths of an inch in the file: 150 is 1.50. */
static void
space_text(const char *rec, char *out, size_t cap)
{
    int v = atoi(rec);

    snprintf(out, cap, "%d.%02d", v / 100, v % 100);
}


/* Returns the hundredths of s, or -1 when it is not a number. */
static int
space_parse(const char *s)
{
    int whole = 0;
    int frac = 0;
    int digits = 0;
    int seen_dot = 0;
    int fd = 0;

    for (; *s != '\0'; s++) {
        if (*s >= '0' && *s <= '9') {
            if (seen_dot != 0) {
                if (fd < 2) {
                    frac = frac * 10 + (*s - '0');
                    fd++;
                }
            } else {
                whole = whole * 10 + (*s - '0');

                if (whole > 99) {
                    return -1;
                }
            }

            digits++;
        } else if (*s == '.' && seen_dot == 0) {
            seen_dot = 1;
        } else {
            return -1;
        }
    }

    if (digits == 0) {
        return -1;
    }

    if (fd == 1) {
        frac *= 10;
    }

    return whole * 100 + frac;
}


/* The text a field shows for the record value rec (never NULL). */
static void
value_text(const FmtDef *def, const char *rec, char *out, size_t cap)
{
    const char *const *names;
    const char *const *vals;
    int n = fmt_choices(def, &names, &vals);

    if (def->kind == FK_SPACE) {
        space_text(rec, out, cap);
    } else if (def->kind == FK_FONT) {
        int k = atoi(rec);

        snprintf(out, cap, "%s", k >= 1 && k <= NFONTS ?
                                     fs.fontname[k - 1] :
                                     rec);
    } else if (n > 0) {
        for (int i = 0; i < n; i++) {
            if (strcmp(vals[i], rec) == 0) {
                snprintf(out, cap, "%s", names[i]);

                return;
            }
        }

        snprintf(out, cap, "%s", rec);
    } else if (rec[0] == '\0' && def->kind == FK_TABS) {
        snprintf(out, cap, "None");
    } else {
        snprintf(out, cap, "%s", rec);
    }
}


/* The record value that is in force: the document's, else the default. */
static const char *
effective(const Doc *d, int scope, const FmtDef *def)
{
    const char *v = doc_fmt_get(d, scope, def->code);

    if (v == NULL && scope > 0) {
        v = doc_fmt_get(d, 0, def->code);
    }

    return v != NULL ? v : def->dflt;
}


/* Set up the field i of panel p from the document. */
static void
wind_setup(const Doc *d, int p, WField *f);


static void
build_field(const Doc *d, int p, int i)
{
    const FmtDef *def = &fs.def[p][i];
    WField *f = &fs.f[p][i];
    int scope = panel_scope(p);
    const char *mine = doc_fmt_get(d, scope, def->code);
    const char *const *names;
    const char *const *vals;
    int n = fmt_choices(def, &names, &vals);
    char tmp[64];

    memset(f, 0, sizeof *f);
    f->label = def->label;

    if (def->kind == FK_CLEVEL) {
        f->kind = WF_TEXT;
        snprintf(f->val, sizeof f->val, "%d", doc_contents_level(d));
        str_copy(fs.init[p][i], sizeof fs.init[p][i], f->val);
        fs.init_sel[p][i] = 0;

        return;
    }

    if (def->kind == FK_COUNT) {
        int kind = i == 2 ? LK_FIGURE : i == 4 ? LK_PICTURE :
                   i == 6 ? LK_TABLE : LK_INDEX;

        f->kind = WF_INFO;
        snprintf(f->val, sizeof f->val, "%u%s", doc_marker_count(d, kind),
                 doc_marker_stale(d, kind) != 0 ? " (counted again at the "
                                                  "next load)" : "");
        str_copy(fs.init[p][i], sizeof fs.init[p][i], f->val);
        fs.init_sel[p][i] = 0;

        return;
    }

    /* What a level takes from the global settings, shown in brackets. */
    if (scope > 0) {
        char t[40];

        value_text(def, effective(d, 0, def), t, sizeof t);
        snprintf(fs.inh[p][i], sizeof fs.inh[p][i], "[%s]", t);
    }

    if (def->kind == FK_FONT) {
        int k = 0;

        if (scope > 0) {
            fs.ch[p][i][k++] = fs.inh[p][i];
        }

        for (int j = 0; j < NFONTS; j++) {
            fs.ch[p][i][k++] = fs.fontname[j];
        }

        fs.ch[p][i][k] = NULL;
        f->kind = WF_CHOICE;
        f->choices = fs.ch[p][i];

        int cur = mine != NULL ? atoi(mine) : (scope > 0 ? 0 : atoi(def->dflt));

        f->sel = scope > 0 ? (mine != NULL ? cur : 0) : cur - 1;

        if (f->sel < 0 || f->sel >= k) {
            f->sel = 0;
        }
    } else if (n > 0) {
        int k = 0;
        int sel = -1;

        if (scope > 0) {
            fs.ch[p][i][k++] = fs.inh[p][i];
        }

        for (int j = 0; j < n; j++) {
            if (mine != NULL && strcmp(mine, vals[j]) == 0) {
                sel = k;
            }

            fs.ch[p][i][k++] = names[j];
        }

        if (mine != NULL && sel < 0) {
            snprintf(fs.extra[p][i], sizeof fs.extra[p][i], "%s", mine);
            sel = k;
            fs.ch[p][i][k++] = fs.extra[p][i];
        }

        if (mine == NULL) {
            sel = 0;

            if (scope == 0) {
                for (int j = 0; j < n; j++) {
                    if (strcmp(def->dflt, vals[j]) == 0) {
                        sel = j;
                    }
                }
            }
        }

        fs.ch[p][i][k] = NULL;
        f->kind = WF_CHOICE;
        f->choices = fs.ch[p][i];
        f->sel = sel;
    } else {
        f->kind = WF_TEXT;

        if (mine != NULL || scope == 0) {
            value_text(def, mine != NULL ? mine : def->dflt, tmp, sizeof tmp);

            /* A ruler of no tab stops, or an empty tag, is shown empty. */
            if (def->kind == FK_TABS || def->kind == FK_TAG) {
                snprintf(tmp, sizeof tmp, "%s", mine != NULL ? mine : "");
            }
        } else {
            snprintf(tmp, sizeof tmp, "%s", fs.inh[p][i]);
        }

        snprintf(f->val, sizeof f->val, "%s", tmp);
    }

    str_copy(fs.init[p][i], sizeof fs.init[p][i], f->val);
    fs.init_sel[p][i] = f->sel;

    if (strcmp(def->code, "WN") == 0 && f->kind == WF_TEXT) {
        wind_setup(d, p, f);
    }
}


/* Check every number; returns 0 and the panel in *bad when one is wrong. */
static int
fmt_check(int *bad, char *err, size_t cap)
{
    for (int p = 0; p < NPANELS; p++) {
        for (int i = 0; i < fs.n[p]; i++) {
            const FmtDef *def = &fs.def[p][i];
            const char *v = fs.f[p][i].val;

            if (fs.f[p][i].kind != WF_TEXT || v[0] == '\0' || v[0] == '[') {
                continue;
            }

            if (def->kind == FK_CLEVEL) {
                int ok = v[0] >= '0' && v[0] <= '9' && strlen(v) <= 2;

                for (const char *q = v; ok != 0 && *q != '\0'; q++) {
                    ok = *q >= '0' && *q <= '9';
                }

                if (ok == 0) {
                    snprintf(err, cap, "%s is a number from 0 to 99",
                             def->label);
                    *bad = p;

                    return 0;
                }
            }

            if (def->kind == FK_SPACE && space_parse(v) < 0) {
                snprintf(err, cap, "%s must be a number such as 1.50",
                         def->label);
                *bad = p;

                return 0;
            }

            if (def->kind == FK_NUM) {
                for (const char *c = v; *c != '\0'; c++) {
                    if (*c < '0' || *c > '9') {
                        snprintf(err, cap, "%s must be a whole number",
                                 def->label);
                        *bad = p;

                        return 0;
                    }
                }
            }

            if (strcmp(def->code, "ZE") == 0 &&
                (atoi(v) < 1 || atoi(v) > 99)) {
                snprintf(err, cap, "%s must be a number from 1 to 99",
                         def->label);
                *bad = p;

                return 0;
            }

            if (strcmp(def->code, "ZD") == 0 && space_parse(v) > 200) {
                snprintf(err, cap, "%s must be from 0 to 2.00 inches",
                         def->label);
                *bad = p;

                return 0;
            }
        }
    }

    return 1;
}


/*
 * The Winding Column Layout panel behind the field Winding Columns (F2 or
 * Keypad +): Number of Columns, Gutter Size and Vertical Rules.  The gutter
 * and the rules are the records WG (thousandths of an inch) and WR (0 none, 1
 * single, 2 double, 3 wide) of the Global and of the level settings.  The
 * Spacing for Bottom Alignment is left out.
 */
static struct {
    char gut[24];
    char init_gut[24];
    int rule;
    int init_rule;
} wst[NPANELS];

static const char *const wind_rules[] = {"None", "Single", "Double", "Wide",
                                         NULL};


static int
wind_check(const WField *f, int n, char *err, int errcap)
{
    int out;

    (void)n;

    if (f[0].val[0] != '\0' && (atoi(f[0].val) < 1 || atoi(f[0].val) > 9)) {
        snprintf(err, (size_t)errcap, "Number of Columns must be from 1 to 9");

        return 0;
    }

    if (f[1].val[0] != '\0' && marker_parse_num(f[1].val, 3, &out) != 0) {
        snprintf(err, (size_t)errcap, "Gutter Size must be a number such as "
                                      "0.250");

        return 0;
    }

    return 1;
}


static void
wind_call(char *val, size_t cap, int arg)
{
    WField f[3];

    memset(f, 0, sizeof f);
    f[0].label = "Number of Columns";
    f[0].kind = WF_TEXT;
    str_copy(f[0].val, sizeof f[0].val, val);
    f[1].label = "Gutter Size";
    f[1].kind = WF_TEXT;
    str_copy(f[1].val, sizeof f[1].val, wst[arg].gut);
    f[2].label = "Vertical Rules";
    f[2].kind = WF_CHOICE;
    f[2].choices = wind_rules;
    f[2].sel = wst[arg].rule;

    if (popup_form("Winding Column Layout", f, 3, wind_check) != 0) {
        str_copy(val, cap, f[0].val);
        str_copy(wst[arg].gut, sizeof wst[arg].gut, f[1].val);
        wst[arg].rule = f[2].sel;
    }
}


/* Set up the call of the field Winding Columns of panel p. */
static void
wind_setup(const Doc *d, int p, WField *f)
{
    int scope = panel_scope(p);
    const char *g = doc_fmt_get(d, scope, "WG");
    const char *r = doc_fmt_get(d, scope, "WR");
    int out;

    if (g == NULL || g[0] == '\0') {
        g = scope > 0 ? doc_fmt_get(d, 0, "WG") : NULL;
    }

    if (r == NULL || r[0] == '\0') {
        r = scope > 0 ? doc_fmt_get(d, 0, "WR") : NULL;
    }

    marker_fmt_num(g != NULL && g[0] != '\0' ? atoi(g) : 250, 3, 0,
                   wst[p].gut, sizeof wst[p].gut);
    wst[p].rule = r != NULL && atoi(r) >= 0 && atoi(r) <= 3 ? atoi(r) : 0;
    str_copy(wst[p].init_gut, sizeof wst[p].init_gut, wst[p].gut);
    wst[p].init_rule = wst[p].rule;
    f->pick = PICK_CALL;
    f->call = wind_call;
    f->arg = p;
    (void)out;
}


/* Write the gutter and the rules of panel p if they were changed. */
static void
wind_apply(Doc *d, int p)
{
    int scope = panel_scope(p);
    int out;
    char rec[32];

    if (strcmp(wst[p].gut, wst[p].init_gut) != 0 &&
        marker_parse_num(wst[p].gut, 3, &out) == 0) {
        snprintf(rec, sizeof rec, "%d", out);
        doc_fmt_set(d, scope, "WG", scope == 0 && out == 250 ? NULL : rec);
    }

    if (wst[p].rule != wst[p].init_rule) {
        snprintf(rec, sizeof rec, "%d", wst[p].rule);
        doc_fmt_set(d, scope, "WR", scope == 0 && wst[p].rule == 0 ? NULL :
                                                                      rec);
    }
}


/* Write the changes of field i of panel p to the document. */
static void
apply_field(Doc *d, int p, int i)
{
    const FmtDef *def = &fs.def[p][i];
    const WField *f = &fs.f[p][i];
    int scope = panel_scope(p);
    char rec[64];

    if (def->kind == FK_COUNT) {
        return;
    }

    if (def->kind == FK_CLEVEL) {
        if (strcmp(f->val, fs.init[p][i]) != 0) {
            doc_contents_set(d, atoi(f->val));
        }

        return;
    }

    if (f->kind == WF_CHOICE) {
        const char *const *names = NULL;
        const char *const *vals = NULL;
        int n = fmt_choices(def, &names, &vals);
        int k = f->sel;

        if (k == fs.init_sel[p][i]) {
            return;
        }

        if (scope > 0) {
            if (k == 0) {
                doc_fmt_set(d, scope, def->code, NULL);

                return;
            }

            k--;
        }

        if (def->kind == FK_FONT) {
            snprintf(rec, sizeof rec, "%d", k + 1);
        } else if (k < n) {
            snprintf(rec, sizeof rec, "%s", vals[k]);
        } else {
            snprintf(rec, sizeof rec, "%s", fs.extra[p][i]);
        }
    } else {
        const char *v = f->val;

        if (strcmp(v, fs.init[p][i]) == 0) {
            return;
        }

        if (v[0] == '\0' || v[0] == '[') {
            if (scope > 0 || def->kind == FK_TABS || def->kind == FK_TAG) {
                doc_fmt_set(d, scope, def->code, NULL);

                return;
            }

            snprintf(rec, sizeof rec, "%s", def->dflt);
        } else if (def->kind == FK_SPACE) {
            snprintf(rec, sizeof rec, "%d", space_parse(v));
        } else if (def->kind == FK_NUM) {
            snprintf(rec, sizeof rec, "%d", atoi(v));
        } else {
            str_copy(rec, sizeof rec, v);
        }
    }

    /* A global setting that has its default value has no record. */
    if (scope == 0 && strcmp(rec, def->dflt) == 0) {
        doc_fmt_set(d, scope, def->code, NULL);
    } else {
        doc_fmt_set(d, scope, def->code, rec);
    }
}


void
globalmenu_format(Editor *e)
{
    Doc *d = e->doc;
    int page = 0;

    fmt_ed = e;

    for (int j = 0; j < NFONTS; j++) {
        doc_font_name(d, j + 1, fs.fontname[j], sizeof fs.fontname[j]);
    }

    for (int p = 0; p < NPANELS; p++) {
        int part = panel_part(p);

        if (part == 0) {
            fs.def[p] = rulers_part;
            fs.n[p] = isz(sizeof rulers_part / sizeof rulers_part[0]);
        } else if (p == 1) {
            fs.def[p] = global_space_part;
            fs.n[p] = isz(sizeof global_space_part /
                          sizeof global_space_part[0]);
        } else if (p == 2) {
            fs.def[p] = lineno_part;
            fs.n[p] = isz(sizeof lineno_part / sizeof lineno_part[0]);
        } else if (p == 3) {
            fs.def[p] = lists_part;
            fs.n[p] = isz(sizeof lists_part / sizeof lists_part[0]);
        } else {
            fs.def[p] = level_space_part;
            fs.n[p] = isz(sizeof level_space_part /
                          sizeof level_space_part[0]);
        }

        for (int i = 0; i < fs.n[p]; i++) {
            build_field(d, p, i);
        }
    }

    for (;;) {
        char title[48];
        int scope = panel_scope(page);
        int part = panel_part(page);
        const char *what = part == 0 ? "headline and text" :
                           part == 1 ? "spacing" :
                           part == 2 ? "line numbering" :
                                       "contents and lists";

        if (scope == 0) {
            snprintf(title, sizeof title, "Global Format (%s)", what);
        } else {
            snprintf(title, sizeof title, "Level %d Format (%s)", scope, what);
        }

        int r = popup_form_nav(title, fs.f[page], fs.n[page], NULL, 1);

        if (r == POPUP_NEXT) {
            page = (page + 1) % NPANELS;
        } else if (r == POPUP_PREV) {
            page = (page + NPANELS - 1) % NPANELS;
        } else if (r == 0) {
            return;
        } else {
            char err[96];
            int bad = 0;

            if (fmt_check(&bad, err, sizeof err) != 0) {
                break;
            }

            popup_msg("Format", err);
            page = bad;
        }
    }

    for (int p = 0; p < NPANELS; p++) {
        for (int i = 0; i < fs.n[p]; i++) {
            apply_field(d, p, i);

            if (strcmp(fs.def[p][i].code, "WN") == 0) {
                wind_apply(d, p);
            }
        }
    }

    ed_invalidate(e);
}


/* ================================================================ Marker */
/*
 * Global, Marker: the Marker Format panel and, behind it, the Marker Display
 * Modes panel.  The 39 markers are listed in three columns on one
 * screen; here each column is a panel of its own, reached with PgUp and
 * PgDn, as are the Marker Format panel and the three columns.
 *
 * The Keycap Format panel (Foreground, Background, Scale and Font, with the
 * records OKF, OKB, OKS and OKT), the Note Format panels and the Counter
 * Format panels follow in the ring, in this order when going PgUp.  The
 * Screen marker and its format are removed (docs/REMOVAL.txt).
 */
typedef struct {
    int type;
    const char *name;
} MarkerName;

/* The number of markers on each of the three pages of display modes. */
static const int marker_page_count[3] = {12, 12, 11};

static const MarkerName marker_names[3][13] = {
    {{5, "Author"},
     {39, "Column Break"},
     {6, "Comment"},
     {17, "Counter"},
     {2, "Cross-reference"},
     {7, "Date"},
     {8, "Description"},
     {34, "Else"},
     {35, "Endif"},
     {9, "Equation"},
     {11, "Figure"},
     {12, "Filedate"}},
    {{13, "Filename"},
     {14, "Filetime"},
     {15, "Font"},
     {33, "If"},
     {16, "Include"},
     {3, "Index"},
     {4, "Keep"},
     {37, "Keycap"},
     {41, "LaTeX"},
     {18, "Line"},
     {19, "Merge"},
     {1, "Note"}},
    {{36, "Orientation"},
     {23, "Page Break"},
     {22, "Page Number"},
     {24, "Pathname"},
     {25, "Picture"},
     {26, "Printer"},
     {27, "Revision"},
     {28, "Section"},
     {29, "Table"},
     {30, "Time"},
     {31, "Whitespace"}}};

static const char *const mode_choices[] = {"Diamond", "Name", "Short", "Long",
                                           "Hidden", NULL};

/* The markers that are images have one more way to be shown: the image. */
static const char *const image_mode_choices[] = {
    "Diamond", "Name", "Short", "Long", "Hidden", "Inline", NULL};
static const char *const global_mode_choices[] = {"n/a", "Diamond", "Name",
                                                  "Short", "Long", "Hidden",
                                                  NULL};
static const char *const hidden_choices[] = {"No", "Yes", NULL};
static const char *const notetype_choices[] = {"Footnotes", "Levelnotes",
                                               "Endnotes", NULL};
static const char *const xref_choices[] = {"Tag", "Headline", "Both", NULL};


/* The stored value of a choice that starts at base (the default is first). */
static void
choice_field(WField *f, const char *label, const char *const *choices,
             int sel)
{
    memset(f, 0, sizeof *f);
    f->label = label;
    f->kind = WF_CHOICE;
    f->choices = choices;
    f->sel = sel;
}


static void
info_field_text(WField *f, const char *label, const char *val)
{
    memset(f, 0, sizeof *f);
    f->label = label;
    f->kind = WF_INFO;
    str_copy(f->val, sizeof f->val, val);
}


/* Set an option record only when its value changes. */
static void
opt_apply(Doc *d, const char *code, const char *val, int nul)
{
    const char *cur = doc_opt_get(d, code);

    if (strcmp(cur != NULL ? cur : "", val != NULL ? val : "") != 0) {
        doc_opt_set(d, code, val, nul);
    }
}



/* ---------------------------------------------------------------- notes */
/*
 * The three Note Format panels: Footnote, Levelnote and Endnote.  Only the
 * settings that are kept in the file are shown (note.h).
 */
#define NOTE_FIELDS 10

static const char *const noteattr_names[] = {
    "Normal",
    "Bold",
    "Italics",
    "Bold Italics",
    "Superscript",
    "Bold Superscript",
    "Italics Superscript",
    "Bold Italics Superscript",
    "Other",
    NULL};
static const int noteattr_masks[] = {0, 1, 4, 5, 16, 17, 20, 21};
static const char *const notepos_choices[] = {"Bottom of Page", "After Text",
                                              NULL};
static const char *const noyes_choices[] = {"No", "Yes", NULL};


static int
attr_sel(int mask)
{
    for (int i = 0; i < 8; i++) {
        if (noteattr_masks[i] == mask) {
            return i;
        }
    }

    return 8;
}


static int
attr_mask(int sel, int old)
{
    return sel >= 0 && sel < 8 ? noteattr_masks[sel] : old;
}


static void
text_field(WField *f, const char *label, const char *val)
{
    memset(f, 0, sizeof *f);
    f->label = label;
    f->kind = WF_TEXT;
    str_copy(f->val, sizeof f->val, val);
}


static void
hundredths_field(WField *f, const char *label, int v)
{
    char t[32];

    marker_fmt_num(v, 2, 0, t, sizeof t);
    text_field(f, label, t);
}


/* Fill the panel of kind k from the settings; returns the field count. */
static int
note_panel_fill(WField *f, int k, const NoteFmt *nf)
{
    const NoteKind *n = &nf->k[k];
    char t[32];
    int c = 0;

    hundredths_field(&f[c++], "Line Spacing", n->sp_line);
    hundredths_field(&f[c++], "Block Spacing", n->sp_block);
    hundredths_field(&f[c++], "Note Spacing", n->sp_note);
    text_field(&f[c++], "Reference Tag", n->reftag);
    choice_field(&f[c], "Reference Tag Attributes", noteattr_names,
                 attr_sel(n->refattr));
    c++;
    text_field(&f[c++], "Note Tag", n->notetag);
    choice_field(&f[c], "Note Tag Attributes", noteattr_names,
                 attr_sel(n->noteattr));
    c++;

    if (k == NK_END) {
        text_field(&f[c++], "Note Page Title", nf->title);

        return c;
    }

    marker_fmt_num(n->sep, 3, 0, t, sizeof t);
    text_field(&f[c++], "Separator Bar (inches)", t);
    choice_field(&f[c], "Renumber?", noyes_choices, n->renumber);
    c++;

    if (k == NK_FOOT) {
        choice_field(&f[c], "Position of Notes", notepos_choices,
                     nf->bottom != 0 ? 0 : 1);
    } else {
        snprintf(t, sizeof t, "%d", nf->level);
        text_field(&f[c], "Note Level", t);
    }

    return c + 1;
}


/* Read the panel of kind k back; values that cannot be read stay. */
static void
note_panel_read(const WField *f, int k, NoteFmt *nf)
{
    NoteKind *n = &nf->k[k];
    int v;

    if (marker_parse_num(f[0].val, 2, &v) == 0 && v >= 0) {
        n->sp_line = v;
    }

    if (marker_parse_num(f[1].val, 2, &v) == 0 && v >= 0) {
        n->sp_block = v;
    }

    if (marker_parse_num(f[2].val, 2, &v) == 0 && v >= 0) {
        n->sp_note = v;
    }

    if (f[3].val[0] != '\0') {
        str_copy(n->reftag, sizeof n->reftag, f[3].val);
    }

    n->refattr = attr_mask(f[4].sel, n->refattr);

    if (f[5].val[0] != '\0') {
        str_copy(n->notetag, sizeof n->notetag, f[5].val);
    }

    n->noteattr = attr_mask(f[6].sel, n->noteattr);

    if (k == NK_END) {
        if (f[7].val[0] != '\0') {
            str_copy(nf->title, sizeof nf->title, f[7].val);
        }

        return;
    }

    if (marker_parse_num(f[7].val, 3, &v) == 0 && v >= 0) {
        n->sep = v;
    }

    n->renumber = f[8].sel;

    if (k == NK_FOOT) {
        nf->bottom = f[9].sel == 0;
    } else if (atoi(f[9].val) >= 1 && atoi(f[9].val) <= 9) {
        nf->level = atoi(f[9].val);
    }
}


/* The numbers of the Keycap Format panel; 0 when all of them are good. */
static int
keycap_panel_values(const WField *kc, KeycapFmt *kf)
{
    int fg;
    int bg;
    int sc;

    if (marker_parse_num(kc[0].val, 1, &fg) != 0 || fg < 0 || fg > 1000 ||
        marker_parse_num(kc[1].val, 1, &bg) != 0 || bg < 0 || bg > 1000 ||
        marker_parse_num(kc[2].val, 3, &sc) != 0 || sc < 100 || sc > 10000) {
        return -1;
    }

    kf->fg = fg;
    kf->bg = bg;
    kf->scale = sc;
    kf->font = kc[3].sel + 1;

    return 0;
}


/* Refuse a Keycap Format that the keycaps cannot be made from. */
static int
keycap_check(const WField *kc, int n UNUSED, char *err, int errcap)
{
    KeycapFmt kf;

    if (keycap_panel_values(kc, &kf) != 0) {
        snprintf(err, usz(errcap),
                 "Foreground and Background are 0.0 to 100.0, Scale is 0.100 "
                 "to 10.000");

        return 0;
    }

    return 1;
}


void
globalmenu_marker(Editor *e)
{
    Doc *d = e->doc;
    WField fmt[9];
    WField col[3][14];
    WField kc[4];
    char klabels[FONT_COUNT][FONT_LABEL];
    const char *kchoices[FONT_COUNT + 1];
    KeycapFmt kf;
    WField np[NK_COUNT][NOTE_FIELDS];
    int npn[NK_COUNT];
    NoteFmt nfmt;
    const char *v;
    int page = 0;
    int base = 0;

    v = doc_opt_get(d, "OH");
    choice_field(&fmt[0], "Display Hidden Markers?", hidden_choices,
                 v != NULL && atoi(v) != 0 ? 1 : 0);
    info_field_text(&fmt[1], "Marker Display Modes",
                    "Custom (PgUp shows the modes)");
    info_field_text(&fmt[2], "Note Format", "PgDn after the counters");
    note_format_get(d, &nfmt);

    for (int k = 0; k < NK_COUNT; k++) {
        npn[k] = note_panel_fill(np[k], k, &nfmt);
    }

    v = doc_opt_get(d, "NT");
    choice_field(&fmt[3], "Primary Note Type", notetype_choices,
                 v != NULL && atoi(v) >= 2 && atoi(v) <= 3 ? atoi(v) - 1 : 0);
    v = doc_opt_get(d, "OC");
    choice_field(&fmt[4], "Cross-reference Format", xref_choices,
                 v != NULL && atoi(v) >= 2 && atoi(v) <= 3 ? atoi(v) - 1 : 0);
    info_field_text(&fmt[5], "Counter Format", "PgDn shows the counters");
    info_field_text(&fmt[6], "Keycap Format", "PgDn after the Note formats");

    memset(&fmt[7], 0, sizeof fmt[7]);
    fmt[7].label = "Date Format";
    fmt[7].kind = WF_TEXT;
    v = doc_opt_get(d, "PD");
    str_copy(fmt[7].val, sizeof fmt[7].val, v != NULL ? v : "Month D, Year");
    memset(&fmt[8], 0, sizeof fmt[8]);
    fmt[8].label = "Time Format";
    fmt[8].kind = WF_TEXT;
    v = doc_opt_get(d, "PT");
    str_copy(fmt[8].val, sizeof fmt[8].val, v != NULL ? v : "H:MM am/pm");

    /* The Keycap Format: three numbers and the font of the keycaps. */
    keycap_format_get(d, &kf);
    fonts_labels(klabels);

    for (int i = 0; i < FONT_COUNT; i++) {
        kchoices[i] = klabels[i];
    }

    kchoices[FONT_COUNT] = NULL;
    memset(kc, 0, sizeof kc);
    kc[0].label = "Foreground";
    kc[1].label = "Background";
    kc[2].label = "Scale";

    for (int i = 0; i < 3; i++) {
        kc[i].kind = WF_TEXT;
        marker_fmt_num(i == 0 ? kf.fg : i == 1 ? kf.bg : kf.scale,
                       i == 2 ? 3 : 1, 0, kc[i].val, sizeof kc[i].val);
    }

    choice_field(&kc[3], "Font", kchoices, kf.font - 1);

    for (int c = 0; c < 3; c++) {
        base = 0;

        if (c == 0) {
            choice_field(&col[c][0], "Global display mode",
                         global_mode_choices, 0);
            base = 1;
        }

        for (int i = 0; i < marker_page_count[c]; i++) {
            int t = marker_names[c][i].type;

            choice_field(&col[c][base + i], marker_names[c][i].name,
                         t == 9 || t == 25 || t == 37 || t == 41 ? image_mode_choices :
                                                        mode_choices,
                         marker_mode(d, t));
        }
    }

    /* The counters: a description and a tag such as \1\. each. */
    static WField cf[2 * MAX_COUNTERS];
    static char cflabel[2 * MAX_COUNTERS][32];

    for (int n = 1; n <= MAX_COUNTERS; n++) {
        char code[16];
        const char *dd = n == 1 ? "Numeric" : n == 2 ? "Alphabetic" : "";
        char dtag[96];

        snprintf(code, sizeof code, "OT%dN", n);
        v = doc_opt_get(d, code);
        snprintf(cflabel[2 * n - 2], sizeof cflabel[0], "C%d Description", n);
        memset(&cf[2 * n - 2], 0, sizeof cf[0]);
        cf[2 * n - 2].label = cflabel[2 * n - 2];
        cf[2 * n - 2].kind = WF_TEXT;
        str_copy(cf[2 * n - 2].val, sizeof cf[0].val, v != NULL ? v : dd);
        marker_counter_tag(d, n, dtag, sizeof dtag);
        snprintf(cflabel[2 * n - 1], sizeof cflabel[0], "C%d Tag", n);
        memset(&cf[2 * n - 1], 0, sizeof cf[0]);
        cf[2 * n - 1].label = cflabel[2 * n - 1];
        cf[2 * n - 1].kind = WF_TEXT;
        str_copy(cf[2 * n - 1].val, sizeof cf[0].val, dtag);
    }

    for (;;) {
        int r;

        if (page >= 5 + NK_COUNT) {
            char title[48];

            /* PgDn goes back through the ring, so the pages of the
               counters are in the reverse order: PgDn from Marker Format
               shows counters 1 to 9, and PgDn again shows 10 to 18. */
            int cpage = 5 + NK_COUNT + COUNTER_PAGES - page;

            snprintf(title, sizeof title, "Counter Format (%d of %d)",
                     cpage, COUNTER_PAGES);
            r = popup_form_nav(title, &cf[(cpage - 1) * 18], 18, NULL, 1);
        } else if (page >= 5) {
            static const char *const ntitle[NK_COUNT] = {
                "Footnote Format", "Levelnote Format", "Endnote Format"};
            int nk = 4 + NK_COUNT - page;

            r = popup_form_nav(ntitle[nk], np[nk], npn[nk], NULL, 1);
        } else if (page == 4) {
            r = popup_form_nav("Keycap Format", kc, 4, keycap_check, 1);
        } else if (page == 0) {
            r = popup_form_nav("Marker Format", fmt, 9, NULL, 1);
        } else {
            char title[48];

            snprintf(title, sizeof title, "Marker Display Modes (%d of 3)",
                     page);
            r = popup_form_nav(title, col[page - 1],
                               marker_page_count[page - 1] + (page == 1),
                               NULL, 1);
        }

        if (r == POPUP_NEXT) {
            page = (page + 1) % (5 + COUNTER_PAGES + NK_COUNT);
        } else if (r == POPUP_PREV) {
            page = (page + 4 + COUNTER_PAGES + NK_COUNT) %
                   (5 + COUNTER_PAGES + NK_COUNT);
        } else if (r == 0) {
            return;
        } else {
            break;
        }
    }

    for (int n = 1; n <= MAX_COUNTERS; n++) {
        char code[16];
        char dtag[96];
        const char *dd = n == 1 ? "Numeric" : n == 2 ? "Alphabetic" : "";

        snprintf(code, sizeof code, "OT%dN", n);
        opt_apply(d, code, strcmp(cf[2 * n - 2].val, dd) == 0 ?
                               NULL :
                               cf[2 * n - 2].val, 1);

        /* The default tag has no record of its own. */
        snprintf(code, sizeof code, "OT%dT", n);
        str_copy(dtag, sizeof dtag, n == 2 ? "\\A\\." : "\\1\\.");
        opt_apply(d, code, strcmp(cf[2 * n - 1].val, dtag) == 0 ?
                               NULL :
                               cf[2 * n - 1].val, 1);
    }

    for (int k = 0; k < NK_COUNT; k++) {
        note_panel_read(np[k], k, &nfmt);
    }

    nfmt.primary = fmt[3].sel;
    note_format_set(d, &nfmt);

    /* A value that cannot be read (the panel has refused it) is left. */
    if (keycap_panel_values(kc, &kf) == 0) {
        keycap_format_set(d, &kf);
    }

    opt_apply(d, "OH", fmt[0].sel != 0 ? "1" : NULL, 0);

    {
        char n[16] = "";

        if (fmt[4].sel != 0) {
            snprintf(n, sizeof n, "%d", fmt[4].sel + 1);
        }

        opt_apply(d, "OC", n, 0);
    }

    opt_apply(d, "PD", strcmp(fmt[7].val, "Month D, Year") == 0 ?
                           NULL :
                           fmt[7].val, 1);
    opt_apply(d, "PT", strcmp(fmt[8].val, "H:MM am/pm") == 0 ?
                           NULL :
                           fmt[8].val, 1);

    int global = col[0][0].sel;

    for (int c = 0; c < 3; c++) {
        int b = c == 0 ? 1 : 0;

        for (int i = 0; i < marker_page_count[c]; i++) {
            int mode = global != 0 ? global - 1 : col[c][b + i].sel;

            if (mode != marker_mode(d, marker_names[c][i].type)) {
                marker_set_mode(d, marker_names[c][i].type, mode);
            }
        }
    }

    ed_invalidate(e);
}


/* ================================================================== Page */
/*
 * Global, Page: the Global Page Layout and the layouts of the title page,
 * the table of contents, the first page, the even and odd pages and the
 * index, one panel each, with PgUp and PgDn as on the Format panels.  A
 * layout shows in brackets what it takes from the global layout.  The
 * records are PML, PMR, PMT, PMB (margins), PHI, PFI (header and footer
 * indent), PHW, PFW (whitespace) and PP (page number tag) for the global
 * layout, and P1ML ... P1P for the layout 1 to 6; DP holds the paper.
 * The Header and the Footer are one line of text: the left, centre and right
 * parts, separated by |, with \page\ for the page number and \section\ for
 * the headline of the section (see doc_hf_get).  Without a |, the text is
 * the centre part; with one |, the left and the right part.  A layout that
 * has none of its own shows the Global text in brackets; an empty field is no
 * header on that layout.  A header that holds more than that (an attribute, a
 * marker of another kind, several lines) is shown and cannot be edited here.
 * The paper bin, which the generic printer has only one of, is shown only.
 */
enum {
    PK_LEN,
    PK_TAG,
    PK_PCT,
    PK_INFO,
    PK_PAPER,
    PK_HF
};

typedef struct {
    const char *label;
    const char *code;
    int kind;
    const char *dflt;
} PageDef;

#define NLAYOUTS 7
#define PAGEF 14

static const PageDef page_defs[] = {
    {"Left Margin", "ML", PK_LEN, "1000"},
    {"Right Margin", "MR", PK_LEN, "1000"},
    {"Top Margin", "MT", PK_LEN, "1000"},
    {"Bottom Margin", "MB", PK_LEN, "1000"},
    {"Header", "H", PK_HF, ""},
    {"Footer", "F", PK_HF, ""},
    {"Header Indent", "HI", PK_LEN, "0"},
    {"Footer Indent", "FI", PK_LEN, "0"},
    {"Header Whitespace", "HW", PK_LEN, "500"},
    {"Footer Whitespace", "FW", PK_LEN, "500"},
    {"Vertical Centering (%)", "C", PK_PCT, "33"},
    {"Page Number Tag", "P", PK_TAG, "\\1\\"},
    {"Paper Size", "", PK_PAPER, ""},
    {"Paper Bin", "", PK_INFO, "1"}};

static const char *const layout_titles[NLAYOUTS] = {
    "Global Page Layout",
    "Title Page Layout",
    "Table of Contents Page Layout",
    "First Page Layout",
    "Even Page Layout",
    "Odd Page Layout",
    "Index Page Layout"};

static const struct {
    const char *name;
    int w;
    int h;
} papers[] = {{"Letter (8 1/2 x 11 in)", 8500, 11000},
              {"Legal (8 1/2 x 14 in)", 8500, 14000},
              {"Fanfold A4 (8 1/2 x 12 in)", 8500, 12000},
              {"Half Letter (5 1/2 x 8 1/2 in)", 5500, 8500},
              {"Computer Paper (14 x 11 in)", 14000, 11000},
              {"Tabloid (11 x 17 in)", 11000, 17000},
              {"Newspaper (17 x 22 in)", 17000, 22000},
              {"A3 (297 x 420 mm)", 11693, 16535},
              {"A4 (210 x 297 mm)", 8268, 11693},
              {"A5 (148 x 210 mm)", 5827, 8268},
              {"B4 (250 x 352 mm)", 9843, 13858},
              {"B5 (176 x 250 mm)", 6929, 9843},
              {"Monarch Envelope (3 7/8 x 7 1/2 in)", 3875, 7500},
              {"Business Envelope (4 1/8 x 9 1/2 in)", 4125, 9500},
              {"DL Envelope (110 x 220 mm)", 4331, 8661},
              {"C5 Envelope (162 x 229 mm)", 6378, 9016},
              {"Number 9 Envelope (3 7/8 x 8 7/8 in)", 3875, 8875},
              {"C6/5 Envelope (114 x 229 mm)", 4488, 9016},
              {"C6, Yougata 2 Envelope (114 x 162 mm)", 4488, 6378},
              {"Yougata 4 Envelope (105 x 235 mm)", 4134, 9252},
              {"Custom (8.5 in x 11 in)", 8500, 11000}};

#define NPAPERS 21
#define PAPER_CUSTOM 20
#define PAPER_FIRST_ENVELOPE 12
#define PAPER_LAST_ENVELOPE 19

/* The Custom entry before the envelopes were added after the C5 entry. */
#define PAPER_CUSTOM_OLD 16

/* The right and bottom margin of an envelope chosen in the list: half an
   inch. */
#define ENVELOPE_MARGIN "500"

/*
 * The return address of an envelope stands 7 mm from the left edge and 7 mm
 * from the top edge, measured to the first glyph box of the first line in
 * the PDF (the owner's decision of 2026-11-20: 5 mm at first, then 2 mm more
 * after the first print on the HP LaserJet 2015dn).  The left margin is
 * 7 mm, 0.276 inch.  The glyph box of a
 * line begins below the top margin by an amount that depends on the size of
 * the type (3.475 points at 8, 3.443 at 11, 3.718 at 12, 4.063 at 14), so
 * the top margin is taken from this table of thousandths of an inch, made
 * by measuring the PDF: margin = (19.843 points - offset) / 0.072.
 */
#define ENVELOPE_LEFT "276"

static const struct {
    int points;
    const char *top;
} envelope_top[] = {{8, "228"},  {9, "237"},  {10, "228"}, {11, "230"},
                    {12, "224"}, {14, "219"}, {17, "204"}, {20, "195"}};

typedef struct {
    WField f[NLAYOUTS][PAGEF];
    int n[NLAYOUTS];
    const PageDef *def[NLAYOUTS][PAGEF];
    const char *ch[NPAPERS + 1];
    char init[NLAYOUTS][PAGEF][512];
    int init_sel;
    char custom_name[64]; /* The text of the Custom entry: its real size. */
    int custom_w;         /* The width of the Custom entry, thousandths. */
    int custom_h;         /* The length of the Custom entry, thousandths. */
} PageState;

static PageState ps;


/* The record code of field i of layout p: P + code, or P<n> + code. */
static void
page_code(int p, const PageDef *def, char *out, size_t cap)
{
    if (p == 0) {
        snprintf(out, cap, "P%s", def->code);
    } else {
        snprintf(out, cap, "P%d%s", p, def->code);
    }
}


static void
page_value(const PageDef *def, const char *rec, char *out, size_t cap)
{
    if (def->kind == PK_LEN) {
        marker_fmt_num(atoi(rec), 3, 0, out, cap);
    } else {
        str_copy(out, cap, rec);
    }
}


/* The index of A4 (210 x 297 mm) in the table of the papers. */
#define PAPER_A4 8


void
doc_default_paper(Doc *d)
{
    Config c;
    char rec[64];

    config_load(&c);

    if (strcmp(c.default_paper, "a4") != 0) {
        return;
    }

    snprintf(rec, sizeof rec, "%d,%d,%d", PAPER_A4, papers[PAPER_A4].w,
             papers[PAPER_A4].h);
    doc_opt_set(d, "DP", rec, 0);
}


/*
 * The DP record is "<index>,<width>,<length>" in thousandths of an inch.  The
 * size of an envelope is stored as it lies in the printer, the long side as
 * the width, whatever the order of the named list.
 */
static int
paper_size_is(int i, int w, int h)
{
    int pw = papers[i].w;
    int ph = papers[i].h;

    return (abs(pw - w) <= 2 && abs(ph - h) <= 2) ||
           (abs(pw - h) <= 2 && abs(ph - w) <= 2);
}


/* The text of one side in inches: eighths as 7/8, else thousandths. */
static void
paper_inch_text(int n, char *out, size_t cap)
{
    int whole = n / 1000;
    int rest = n % 1000;
    int eighth = (rest * 8 + 500) / 1000;

    if (abs(rest * 8 - eighth * 1000) <= 8) {
        if (eighth == 0) {
            snprintf(out, cap, "%d", whole);
        } else if (eighth == 8) {
            snprintf(out, cap, "%d", whole + 1);
        } else if (eighth % 2 == 0 && eighth % 4 != 0) {
            snprintf(out, cap, "%d %d/4", whole, eighth / 2);
        } else if (eighth == 4) {
            snprintf(out, cap, "%d 1/2", whole);
        } else {
            snprintf(out, cap, "%d %d/8", whole, eighth);
        }

        if (whole == 0) {
            /* A side below one inch has no whole part to show. */
            char *sp = strchr(out, ' ');

            if (sp != NULL) {
                memmove(out, sp + 1, strlen(sp + 1) + 1);
            }
        }
    } else {
        size_t len;

        snprintf(out, cap, "%d.%03d", whole, rest);
        len = strlen(out);

        /* The thousandths without the zeros at the end: 3.6, not 3.600. */
        while (len > 0 && out[len - 1] == '0') {
            out[--len] = '\0';
        }
    }
}


/* The text of the Custom entry: the shorter side first, in mm or inches. */
static void
paper_custom_text(int w, int h, char *out, size_t cap)
{
    int lo = w < h ? w : h;
    int hi = w < h ? h : w;
    int lo_mm = (lo * 254 + 5000) / 10000;
    int hi_mm = (hi * 254 + 5000) / 10000;
    char a[24];
    char b[24];

    if (abs(lo * 254 - lo_mm * 10000) <= 600 &&
        abs(hi * 254 - hi_mm * 10000) <= 600) {
        snprintf(out, cap, "Custom (%d x %d mm)", lo_mm, hi_mm);

        return;
    }

    paper_inch_text(lo, a, sizeof a);
    paper_inch_text(hi, b, sizeof b);
    snprintf(out, cap, "Custom (%s x %s in)", a, b);
}


/*
 * The paper of the DP record.  A size that is stored and does not belong to
 * the named paper of the index is looked up in the list (either way round)
 * and else is Custom; the real size of a Custom paper is in custom_w and
 * custom_h.  A record with no size has the paper of its index.
 */
static int
paper_index(const Doc *d)
{
    const char *v = doc_opt_get(d, "DP");
    int idx = 0;
    int w = 0;
    int h = 0;
    int n;

    ps.custom_w = papers[PAPER_CUSTOM].w;
    ps.custom_h = papers[PAPER_CUSTOM].h;

    if (v == NULL) {
        return 0;
    }

    n = sscanf(v, "%d,%d,%d", &idx, &w, &h);

    if (n < 1 || idx < 0 || idx >= NPAPERS) {
        return 0;
    }

    if (n < 3 || w <= 0 || h <= 0) {
        /* A record of an older version names the Custom paper as 16. */
        return idx >= PAPER_CUSTOM_OLD && idx < PAPER_CUSTOM ? PAPER_CUSTOM :
                                                               idx;
    }

    if (idx != PAPER_CUSTOM && paper_size_is(idx, w, h)) {
        return idx;
    }

    for (int i = 0; i < PAPER_CUSTOM; i++) {
        if (paper_size_is(i, w, h)) {
            return i;
        }
    }

    ps.custom_w = w;
    ps.custom_h = h;

    return PAPER_CUSTOM;
}


/* The text of a header or footer as one line: left | centre | right. */
static void
hf_line(const HfText *h, char *out, size_t cap)
{
    const char *l = h->part[0];
    const char *c = h->part[1];
    const char *r = h->part[2];

    if (l[0] == '\0' && r[0] == '\0') {
        str_copy(out, cap, c);
    } else if (c[0] == '\0') {
        snprintf(out, cap, "%s | %s", l, r);
    } else {
        snprintf(out, cap, "%s | %s | %s", l, c, r);
    }
}


/* The field of the Header or Footer of layout p. */
static void
hf_field(const Doc *d, int p, int footer, WField *f)
{
    HfText h;
    char line[400];

    doc_hf_get(d, p, footer, &h);
    f->kind = h.plain != 0 ? WF_TEXT : WF_INFO;

    if (h.own == 0 && p > 0) {
        doc_hf_get(d, 0, footer, &h);
        f->kind = h.plain != 0 ? WF_TEXT : WF_INFO;
        hf_line(&h, line, sizeof line);
        snprintf(f->val, sizeof f->val, "[%s]", line);

        return;
    }

    hf_line(&h, f->val, sizeof f->val);
}


/* Is the text of a field good for a header? Returns 0, or -1 with err. */
static int
hf_check(const char *v, char *err, size_t cap)
{
    int bars = 0;

    for (const char *q = v; *q != '\0'; q++) {
        if (*q == '|') {
            bars++;
        } else if ((unsigned char)*q < 0x20 || (unsigned char)*q >= 0x7f) {
            snprintf(err, cap, "A header or footer takes plain ASCII text");

            return -1;
        }
    }

    if (bars > 2) {
        snprintf(err, cap, "A header or footer has at most three parts, "
                           "separated by |");

        return -1;
    }

    return 0;
}


/* Write the text of a field into the document. */
static void
hf_store(Doc *d, int p, int footer, const char *v)
{
    char part[3][128];
    int n = 0;
    const char *q = v;

    while (*v == ' ') {
        v++;
    }

    if (v[0] == '[') {
        doc_hf_clear(d, p, footer, 0);

        return;
    }

    if (v[0] == '\0') {
        doc_hf_clear(d, p, footer, p > 0);

        return;
    }

    memset(part, 0, sizeof part);

    for (q = v; n < 3; n++) {
        const char *e = strchr(q, '|');
        size_t len = e != NULL ? (size_t)(e - q) : strlen(q);
        size_t a = 0;

        while (a < len && q[a] == ' ') {
            a++;
        }

        while (len > a && q[len - 1] == ' ') {
            len--;
        }

        len = len - a < sizeof part[0] - 1 ? len - a : sizeof part[0] - 1;
        memcpy(part[n], q + a, len);

        if (e == NULL) {
            n++;
            break;
        }

        q = e + 1;
    }

    if (n == 1) {
        doc_hf_set(d, p, footer, "", part[0], "");
    } else if (n == 2) {
        doc_hf_set(d, p, footer, part[0], "", part[1]);
    } else {
        doc_hf_set(d, p, footer, part[0], part[1], part[2]);
    }
}


/*
 * An envelope was chosen in the Paper Size list.  An envelope needs what its
 * template has: text near the corner and larger type.  The templates have
 * zero margins and place the sender by an indent of their own; a document
 * that has only text must not start at the edge of the paper, which no
 * printer prints.  So the four margins become half an inch, which puts the
 * sender half an inch from the left and a little more from the top, unless
 * the same visit of the panel changed that margin (the field no longer has
 * the value that it started with), and the type size becomes the one of the
 * envelope when the document names none.  It is done once, at the choice: a
 * later choice of another paper leaves the margins as they are, and the
 * margins stay what the owner makes them afterwards.
 */
static void
envelope_defaults(Doc *d)
{
    static const char *const codes[4] = {"ML", "MR", "MT", "MB"};
    const char *dp = doc_opt_get(d, "DP");
    const char *psr = doc_opt_get(d, "PS");
    const char *top = "140";
    int pw = 0;
    int ph = 0;
    int pts = 0;

    if (dp != NULL && sscanf(dp, "%*d,%d,%d", &pw, &ph) == 2) {
        pts = doc_envelope_points(pw, ph);
    }

    if (psr != NULL) {
        pts = atoi(psr);
    }

    for (size_t t = 0; t < sizeof envelope_top / sizeof envelope_top[0]; t++) {
        if (envelope_top[t].points == pts) {
            top = envelope_top[t].top;
        }
    }

    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < ps.n[0]; i++) {
            const PageDef *def = ps.def[0][i];
            const char *margin = ENVELOPE_MARGIN;
            char code[16];

            if (strcmp(def->code, codes[k]) != 0) {
                continue;
            }

            if (k == 0) {
                margin = ENVELOPE_LEFT;
            } else if (k == 2) {
                margin = top;
            }

            if (strcmp(ps.f[0][i].val, ps.init[0][i]) == 0) {
                page_code(0, def, code, sizeof code);
                doc_opt_set(d, code, margin, 0);
            }
        }
    }

    if (doc_opt_get(d, "PS") == NULL && dp != NULL &&
        sscanf(dp, "%*d,%d,%d", &pw, &ph) == 2 &&
        doc_envelope_points(pw, ph) > 0) {
        char rec[16];

        snprintf(rec, sizeof rec, "%d", doc_envelope_points(pw, ph));
        doc_opt_set(d, "PS", rec, 0);
    }
}


void
globalmenu_page(Editor *e)
{
    Doc *d = e->doc;
    int page = 0;
    int envelope = 0;

    for (int i = 0; i < NPAPERS; i++) {
        ps.ch[i] = papers[i].name;
    }

    ps.ch[NPAPERS] = NULL;
    paper_index(d);
    paper_custom_text(ps.custom_w, ps.custom_h, ps.custom_name,
                      sizeof ps.custom_name);
    ps.ch[PAPER_CUSTOM] = ps.custom_name;

    for (int p = 0; p < NLAYOUTS; p++) {
        int n = 0;

        for (size_t k = 0; k < sizeof page_defs / sizeof page_defs[0]; k++) {
            const PageDef *def = &page_defs[k];

            if (def->kind == PK_PAPER && p != 0) {
                continue;
            }

            if (def->kind == PK_PCT && p != 1) {
                continue;
            }

            ps.def[p][n++] = def;
        }

        ps.n[p] = n;

        for (int i = 0; i < n; i++) {
            const PageDef *def = ps.def[p][i];
            WField *f = &ps.f[p][i];
            char code[16];
            const char *mine = NULL;
            char tmp[64];

            memset(f, 0, sizeof *f);
            f->label = def->label;
            page_code(p, def, code, sizeof code);

            if (def->kind == PK_PAPER) {
                f->kind = WF_CHOICE;
                f->choices = ps.ch;
                f->sel = paper_index(d);
                ps.init_sel = f->sel;

                continue;
            }

            if (def->kind == PK_INFO) {
                f->kind = WF_INFO;
                str_copy(f->val, sizeof f->val, def->dflt);

                continue;
            }

            if (def->kind == PK_HF) {
                hf_field(d, p, def->code[0] == 'F', f);
                str_copy(ps.init[p][i], sizeof ps.init[p][i], f->val);

                continue;
            }

            mine = doc_opt_get(d, code);
            f->kind = WF_TEXT;

            if (mine != NULL) {
                page_value(def, mine, tmp, sizeof tmp);
            } else {
                char g[16];
                const char *gv;
                char t[40];

                page_code(0, def, g, sizeof g);
                gv = doc_opt_get(d, g);
                page_value(def, gv != NULL ? gv : def->dflt, t, sizeof t);

                if (p == 0) {
                    str_copy(tmp, sizeof tmp, t);
                } else {
                    snprintf(tmp, sizeof tmp, "[%s]", t);
                }
            }

            str_copy(f->val, sizeof f->val, tmp);
            str_copy(ps.init[p][i], sizeof ps.init[p][i], f->val);
        }
    }

    for (;;) {
        int r = popup_form_nav(layout_titles[page], ps.f[page], ps.n[page],
                               NULL, 1);

        if (r == POPUP_NEXT) {
            page = (page + 1) % NLAYOUTS;
        } else if (r == POPUP_PREV) {
            page = (page + NLAYOUTS - 1) % NLAYOUTS;
        } else if (r == 0) {
            return;
        } else {
            char err[96] = "";
            int bad = -1;

            for (int p = 0; p < NLAYOUTS && bad < 0; p++) {
                for (int i = 0; i < ps.n[p]; i++) {
                    const PageDef *def = ps.def[p][i];
                    const char *v = ps.f[p][i].val;
                    int out;

                    if (def->kind == PK_HF && ps.f[p][i].kind == WF_TEXT &&
                        v[0] != '[' && hf_check(v, err, sizeof err) != 0) {
                        bad = p;
                        break;
                    }

                    if (ps.f[p][i].kind != WF_TEXT || v[0] == '\0' ||
                        v[0] == '[' || def->kind == PK_TAG ||
                        def->kind == PK_HF) {
                        continue;
                    }

                    if (def->kind == PK_LEN &&
                        marker_parse_num(v, 3, &out) != 0) {
                        snprintf(err, sizeof err, "%s must be a number such "
                                                  "as 1.000",
                                 def->label);
                        bad = p;
                        break;
                    }

                    if (def->kind == PK_PCT &&
                        (atoi(v) < 0 || atoi(v) > 100)) {
                        snprintf(err, sizeof err, "%s must be 0 to 100",
                                 def->label);
                        bad = p;
                        break;
                    }
                }
            }

            if (bad < 0) {
                break;
            }

            popup_msg("Page Layout", err);
            page = bad;
        }
    }

    for (int p = 0; p < NLAYOUTS; p++) {
        for (int i = 0; i < ps.n[p]; i++) {
            const PageDef *def = ps.def[p][i];
            const WField *f = &ps.f[p][i];
            char code[16];
            char rec[64];
            int out;
            int is_env;

            if (def->kind == PK_PAPER) {
                if (f->sel != ps.init_sel) {
                    int pw = papers[f->sel].w;
                    int ph = papers[f->sel].h;

                    if (f->sel == PAPER_CUSTOM) {
                        pw = ps.custom_w;
                        ph = ps.custom_h;
                    }

                    /*
                     * An envelope lies in the printer with its long side
                     * across, as the templates store it, so that the
                     * Preview and the print show it that way up.
                     */
                    is_env = (f->sel >= PAPER_FIRST_ENVELOPE &&
                              f->sel <= PAPER_LAST_ENVELOPE) ||
                             (f->sel == PAPER_CUSTOM &&
                              doc_envelope_points(pw, ph) > 0);

                    if (is_env != 0 && pw < ph) {
                        int t = pw;

                        pw = ph;
                        ph = t;
                    }

                    snprintf(rec, sizeof rec, "%d,%d,%d",
                             f->sel == PAPER_CUSTOM ? PAPER_CUSTOM_OLD :
                                                      f->sel,
                             pw, ph);
                    doc_opt_set(d, "DP", rec, 0);
                    envelope = is_env;
                }

                continue;
            }

            if (f->kind != WF_TEXT || strcmp(f->val, ps.init[p][i]) == 0) {
                continue;
            }

            if (def->kind == PK_HF) {
                hf_store(d, p, def->code[0] == 'F', f->val);

                continue;
            }

            page_code(p, def, code, sizeof code);

            if (f->val[0] == '\0' || f->val[0] == '[') {
                doc_opt_set(d, code, NULL, 0);
            } else if (def->kind == PK_LEN) {
                marker_parse_num(f->val, 3, &out);
                snprintf(rec, sizeof rec, "%d", out);
                doc_opt_set(d, code, strcmp(rec, def->dflt) == 0 && p == 0 ?
                                         NULL :
                                         rec,
                            0);
            } else if (def->kind == PK_PCT) {
                snprintf(rec, sizeof rec, "%d", atoi(f->val));
                doc_opt_set(d, code, rec, 0);
            } else {
                doc_opt_set(d, code, f->val, 1);
            }
        }
    }

    if (envelope != 0) {
        envelope_defaults(d);
    }

    ed_invalidate(e);
}


/* ================================================================= Named */
/*
 * Global, Named: edit or delete a named block format.  The name is asked
 * for on the panel ("Retrieve Named Block" and "Save as"); here
 * the name to retrieve is chosen from a list first, with "(new)" for a
 * format that does not exist yet.  A named format only sets what it
 * names, so every field starts empty; the records are the B records of the
 * FN definition in the header:
 *
 *   BFA attributes, BFJ justification, BFI indent, BFF font, BFT tab stops,
 *   BL and BR block indent (left and right, in characters), BN keep the
 *   block on one page (2 for Yes), BSB and BSL block and line spacing (in
 *   hundredths), BU ruler (2 for 10/inch), BV vertical centering (percent)
 *   and BWN the number of winding columns.
 */
enum {
    NF_NAME,
    NF_TABS,
    NF_ATTR,
    NF_JUST,
    NF_INDENT,
    NF_FONT,
    NF_LINE,
    NF_BLOCK,
    NF_RULER,
    NF_LEFT,
    NF_RIGHT,
    NF_VCENTER,
    NF_WIND,
    NF_KEEP,
    NF_COUNT
};

static const char *const nf_codes[NF_COUNT] = {
    "", "BFT", "BFA", "BFJ", "BFI", "BFF", "BSL",
    "BSB", "BU", "BL", "BR", "BV", "BWN", "BN"};

/* The records are written in this order. */
static const int nf_order[] = {NF_ATTR,  NF_JUST,  NF_INDENT, NF_FONT,
                               NF_TABS,  NF_LEFT,  NF_RIGHT,  NF_KEEP,
                               NF_BLOCK, NF_LINE,  NF_RULER,  NF_VCENTER,
                               NF_WIND};

static const char *const nf_attr[] = {"[]", "Normal", "Bold", "Italic",
                                      "Underscore Words", NULL};
static const char *const nf_attr_vals[] = {"", "0", "1", "4", "16"};
static const char *const nf_just[] = {"[]", "Left", "Right", "Center", "Even",
                                      NULL};
static const char *const nf_just_vals[] = {"", "0", "1", "2", "3"};
static const char *const nf_indent[] = {"[]", "None", "First", "All but First",
                                        "All", NULL};
static const char *const nf_indent_vals[] = {"", "0", "1", "2", "3"};
static const char *const nf_ruler[] = {"[]", "Relative", "10/inch", "12/inch",
                                       "15/inch", NULL};
static const char *const nf_ruler_vals[] = {"", "1", "2", "3", "4"};
static const char *const nf_keep[] = {"[]", "No", "Yes", NULL};
static const char *const nf_keep_vals[] = {"", "1", "2"};


static const char *const *
nf_vals(int k)
{
    return k == NF_ATTR   ? nf_attr_vals :
           k == NF_JUST   ? nf_just_vals :
           k == NF_INDENT ? nf_indent_vals :
           k == NF_RULER  ? nf_ruler_vals :
                            nf_keep_vals;
}


/*
 * The Winding Column Layout panel of the Named Block Format panel: the number
 * of columns, the gutter (BWG, thousandths of an inch) and the vertical rules
 * (BWR: 0 none, 1 single, 2 double, 3 wide).  A block takes its own number,
 * gutter and rules, else those of its level and the document.  An empty field
 * and [] leave the setting to them.  BWG and BWR are records of Strata's own.
 */
static struct {
    char gut[24];
    int rule; /* 0: not set, else 1 + the rule */
} nwst;

static const char *const nwind_rules[] = {"[]", "None", "Single", "Double",
                                          "Wide", NULL};


static void
nwind_call(char *val, size_t cap, int arg)
{
    WField f[3];

    (void)arg;
    memset(f, 0, sizeof f);
    f[0].label = "Number of Columns";
    f[0].kind = WF_TEXT;
    str_copy(f[0].val, sizeof f[0].val, val);
    f[1].label = "Gutter Size";
    f[1].kind = WF_TEXT;
    str_copy(f[1].val, sizeof f[1].val, nwst.gut);
    f[2].label = "Vertical Rules";
    f[2].kind = WF_CHOICE;
    f[2].choices = nwind_rules;
    f[2].sel = nwst.rule;

    if (popup_form("Winding Column Layout", f, 3, wind_check) != 0) {
        str_copy(val, cap, f[0].val);
        str_copy(nwst.gut, sizeof nwst.gut, f[1].val);
        nwst.rule = f[2].sel;
    }
}


static void
named_edit(Editor *e)
{
    Doc *d = e->doc;
    int nn = doc_named_count(d);
    char names[40][32];
    const char *items[41];
    WField f[NF_COUNT];
    static const char *fontch[NFONTS + 2];
    static char fontnames[NFONTS][32];
    char old[32] = "";
    int c = 0;

    items[0] = "(new)";

    for (int i = 0; i < nn && i < 40; i++) {
        doc_named_name(d, i, names[i], sizeof names[i]);
        items[i + 1] = names[i];
    }

    if (nn > 40) {
        nn = 40;
    }

    if (nn > 0) {
        c = popup_menu("Retrieve Named Block", items, nn + 1, 0);

        if (c < 0) {
            return;
        }

        if (c > 0) {
            str_copy(old, sizeof old, names[c - 1]);
        }
    }

    memset(f, 0, sizeof f);
    f[NF_NAME].label = "Save as";
    f[NF_NAME].kind = WF_TEXT;
    str_copy(f[NF_NAME].val, sizeof f[NF_NAME].val, old);
    f[NF_TABS].label = "Ruler (tab stops)";
    f[NF_TABS].kind = WF_TEXT;
    f[NF_ATTR].label = "Attributes";
    f[NF_ATTR].choices = nf_attr;
    f[NF_JUST].label = "Justification";
    f[NF_JUST].choices = nf_just;
    f[NF_INDENT].label = "Indent";
    f[NF_INDENT].choices = nf_indent;
    f[NF_FONT].label = "Font";
    f[NF_LINE].label = "Line Spacing";
    f[NF_LINE].kind = WF_TEXT;
    f[NF_BLOCK].label = "Block Spacing";
    f[NF_BLOCK].kind = WF_TEXT;
    f[NF_RULER].label = "Ruler";
    f[NF_RULER].choices = nf_ruler;
    f[NF_LEFT].label = "Block Indent (Left)";
    f[NF_LEFT].kind = WF_TEXT;
    f[NF_RIGHT].label = "Block Indent (Right)";
    f[NF_RIGHT].kind = WF_TEXT;
    f[NF_VCENTER].label = "Vertical Centering (%)";
    f[NF_VCENTER].kind = WF_TEXT;
    f[NF_WIND].label = "Winding Columns";
    f[NF_WIND].kind = WF_TEXT;
    f[NF_WIND].pick = PICK_CALL;
    f[NF_WIND].call = nwind_call;
    f[NF_KEEP].label = "Keep Block on One Page?";
    f[NF_KEEP].choices = nf_keep;

    fontch[0] = "[]";

    for (int i = 0; i < NFONTS; i++) {
        doc_font_name(d, i + 1, fontnames[i], sizeof fontnames[i]);
        fontch[i + 1] = fontnames[i];
    }

    fontch[NFONTS + 1] = NULL;
    f[NF_FONT].choices = fontch;

    for (int k = 0; k < NF_COUNT; k++) {
        if (f[k].choices != NULL) {
            f[k].kind = WF_CHOICE;
        }
    }

    memset(&nwst, 0, sizeof nwst);

    if (old[0] != '\0') {
        /* doc_named_get answers in a buffer that the next call reuses. */
        const char *g = doc_named_get(d, old, "BWG");

        if (g != NULL && g[0] != '\0') {
            marker_fmt_num(atoi(g), 3, 0, nwst.gut, sizeof nwst.gut);
        }

        const char *r = doc_named_get(d, old, "BWR");

        if (r != NULL && atoi(r) >= 0 && atoi(r) <= 3) {
            nwst.rule = atoi(r) + 1;
        }
    }

    /* Fill the fields from the format that was chosen. */
    for (int k = 1; k < NF_COUNT && old[0] != '\0'; k++) {
        const char *v = doc_named_get(d, old, nf_codes[k]);

        if (v == NULL) {
            continue;
        }

        if (f[k].kind == WF_CHOICE) {
            if (k == NF_FONT) {
                f[k].sel = atoi(v) >= 1 && atoi(v) <= NFONTS ? atoi(v) : 0;
            } else {
                const char *const *vals = nf_vals(k);

                for (int j = 1; f[k].choices[j] != NULL; j++) {
                    if (strcmp(vals[j], v) == 0) {
                        f[k].sel = j;
                    }
                }
            }
        } else if (k == NF_LINE || k == NF_BLOCK) {
            char t[24];

            snprintf(t, sizeof t, "%d.%02d", atoi(v) / 100, atoi(v) % 100);
            str_copy(f[k].val, sizeof f[k].val, t);
        } else {
            str_copy(f[k].val, sizeof f[k].val, v);
        }
    }

    for (;;) {
        if (popup_form("Named Block Format", f, NF_COUNT, NULL) == 0) {
            return;
        }

        int bad = 0;

        if (f[NF_NAME].val[0] == '\0') {
            popup_msg("Named Block Format", "Enter a name to save the "
                                            "format as");
            continue;
        }

        for (const char *p = f[NF_NAME].val; *p != '\0'; p++) {
            if (u8(*p) < 32) {
                bad = 1;
            }
        }

        for (int k = NF_LINE; k <= NF_BLOCK; k++) {
            int out;

            if (f[k].val[0] != '\0' &&
                marker_parse_num(f[k].val, 2, &out) != 0) {
                bad = 1;
            }
        }

        for (int k = NF_LEFT; k <= NF_WIND; k++) {
            for (const char *p = f[k].val; *p != '\0'; p++) {
                if (*p < '0' || *p > '9') {
                    bad = 1;
                }
            }
        }

        if (bad == 0) {
            break;
        }

        popup_msg("Named Block Format", "Invalid number");
    }

    const char *recs[NF_COUNT + 2];
    char buf[NF_COUNT + 2][48];
    int n = 0;

    for (size_t i = 0; i < sizeof nf_order / sizeof nf_order[0]; i++) {
        int k = nf_order[i];
        const char *v = "";
        char val[40];

        if (f[k].kind == WF_CHOICE) {
            if (f[k].sel == 0) {
                continue;
            }

            if (k == NF_FONT) {
                snprintf(val, sizeof val, "%d", f[k].sel);
                v = val;
            } else {
                v = nf_vals(k)[f[k].sel];
            }
        } else if (f[k].val[0] == '\0') {
            continue;
        } else if (k == NF_LINE || k == NF_BLOCK) {
            int out = 0;

            marker_parse_num(f[k].val, 2, &out);
            snprintf(val, sizeof val, "%d", out);
            v = val;
        } else if (k == NF_LEFT || k == NF_RIGHT || k == NF_VCENTER ||
                   k == NF_WIND) {
            snprintf(val, sizeof val, "%d", atoi(f[k].val));
            v = val;
        } else {
            str_copy(val, sizeof val, f[k].val);
            v = val;
        }

        snprintf(buf[n], sizeof buf[n], "%s%s", nf_codes[k], v);
        recs[n] = buf[n];
        n++;
    }

    if (nwst.gut[0] != '\0') {
        int out = 0;

        marker_parse_num(nwst.gut, 3, &out);
        snprintf(buf[n], sizeof buf[n], "BWG%d", out);
        recs[n] = buf[n];
        n++;
    }

    if (nwst.rule > 0) {
        snprintf(buf[n], sizeof buf[n], "BWR%d", nwst.rule - 1);
        recs[n] = buf[n];
        n++;
    }

    doc_named_set(d, f[NF_NAME].val, recs, n);
    ed_invalidate(e);
}


static void
named_delete(Editor *e)
{
    Doc *d = e->doc;
    int nn = doc_named_count(d);
    char names[40][32];
    const char *items[40];

    if (nn == 0) {
        popup_msg("Delete Named Block", "There are no named block formats");

        return;
    }

    if (nn > 40) {
        nn = 40;
    }

    for (int i = 0; i < nn; i++) {
        doc_named_name(d, i, names[i], sizeof names[i]);
        items[i] = names[i];
    }

    int c = popup_menu("Delete Named Block", items, nn, 0);

    if (c < 0) {
        return;
    }

    char q[80];

    snprintf(q, sizeof q, "Delete the named block format %s?", names[c]);

    if (popup_yn(q) != 0) {
        doc_named_delete(d, names[c]);
        ed_invalidate(e);
    }
}


void
globalmenu_named(Editor *e, int del)
{
    if (del != 0) {
        named_delete(e);
    } else {
        named_edit(e);
    }
}
