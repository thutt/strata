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

/* markerui.c - the panels for creating and editing markers. */
#include "util.h"
#include "markerui.h"
#include "fileops.h"
#include "widgets.h"
#include "editor.h"
#include "layout.h"
#include "note.h"
#include "marker.h"
#include "fonts.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXPANEL 8

static const MInfo *cur_info;
static const Doc *cur_doc;
static int nested_field; /* the form has the nested text as its first field */


/* Do two numbers typed on a panel have the same value? */
static int
same_value(const char *a, const char *b, int decimals)
{
    int x;
    int y;

    if (strcmp(a, b) == 0) {
        return 1;
    }

    return marker_parse_num(a, decimals, &x) == 0 &&
           marker_parse_num(b, decimals, &y) == 0 && x == y;
}


/* The tag and the headline of the section whose first block is item. */
static void
section_texts(const Doc *d, int item, char *tag, size_t tagcap, char *head,
              size_t headcap)
{
    Fmt f;

    tag[0] = '\0';
    head[0] = '\0';

    if (item < 0) {
        return;
    }

    fmt_resolve(d, item, 0, &f);
    str_copy(tag, tagcap, f.tag);

    for (int n = slen(tag); n > 0 && tag[n - 1] == ' '; n--) {
        tag[n - 1] = '\0';
    }

    if (d->items[item].blk->ncols > 0) {
        text_plain(&d->items[item].blk->cols[0].text, head, isz(headcap));
    }
}


/*
 * The fields 2 to 5 of a Keycap marker: a value that the marker does not
 * have shows the one of the Keycap Format of the document in brackets, as
 * [0.0] or [Text (Latin Modern Roman)].  A value that is typed replaces the
 * bracket; the bracket as it is has no record.  base is the index of the
 * field 1 in f.
 */
static void
keycap_globals(const Doc *doc, const MPayload *p, int base, WField *f)
{
    static char labels[FONT_COUNT][FONT_LABEL];
    static char first[FONT_LABEL + 2];
    static const char *choices[FONT_COUNT + 2];
    KeycapFmt g;
    int dec[3] = {1, 1, 3};
    int val[3];

    keycap_format_get(doc, &g);
    val[0] = g.fg;
    val[1] = g.bg;
    val[2] = g.scale;

    for (int i = 0; i < 3; i++) {
        char num[32];
        WField *w = &f[base + 1 + i];

        if (mp_get(p, i == 0 ? "2" : i == 1 ? "3" : "4") == NULL) {
            marker_fmt_num(val[i], dec[i], 0, num, sizeof num);
            snprintf(w->val, sizeof w->val, "[%s]", num);
        }
    }

    fonts_labels(labels);
    snprintf(first, sizeof first, "[%s]", labels[g.font - 1]);
    choices[0] = first;

    for (int i = 0; i < FONT_COUNT; i++) {
        choices[i + 1] = labels[i];
    }

    choices[FONT_COUNT + 1] = NULL;
    f[base + 4].choices = choices;

    /* A font number that the list does not have is shown as the bracket. */
    if (f[base + 4].sel > FONT_COUNT) {
        f[base + 4].sel = 0;
    }
}


/*
 * The fields of markers that hold the name of a file: the list of files
 * (Keypad +) is of the files that fit the marker.  Returns the suffixes that
 * limit the list ("" for all files) or NULL when the field is not a file.
 */
static const char *
file_suffixes(int type, const char *id)
{
    if (strcmp(id, "1") != 0) {
        return NULL;
    }

    switch (type) {
        case 16: /* Include */
            return ".doc";

        case 25: /* Picture */
            return ".png .jpg .jpeg .pdf .eps";

        default:
            return NULL;
    }
}


/*
 * The choice list of a field that holds a font number (the Font marker and
 * the Keycap marker): the names of the TeX Font Association, the list of
 * the fonts for printing (docs/FONTS.txt).  The first entry stands for the
 * font that the block has (Text) and has no record.
 */
static const char *const *
font_choices(void)
{
    static const char *const slot[FONT_COUNT] = {
        "Headline", "Text",    "Header/Footer", "Font 4",  "Font 5",
        "Font 6",   "Font 7",  "Font 8",        "Font 9",  "Font 10",
        "Font 11",  "Font 12", "Font 13",       "Font 14", "Font 15",
        "Font 16",  "Font 17", "Font 18",       "Font 19", "Font 20"};
    static char text[FONT_COUNT + 1][96];
    static const char *list[FONT_COUNT + 2];
    FontEntry t[FONT_COUNT];

    fonts_load(t);
    snprintf(text[0], sizeof text[0], "[Text: %.60s]",
             t[1].name[0] != '\0' ? t[1].name : "not set");

    for (int i = 0; i < FONT_COUNT; i++) {
        snprintf(text[i + 1], sizeof text[i + 1], "%s: %.60s", slot[i],
                 t[i].name[0] != '\0' ? t[i].name : "not set");
    }

    for (int i = 0; i <= FONT_COUNT; i++) {
        list[i] = text[i];
    }

    list[FONT_COUNT + 1] = NULL;

    return list;
}


static int
check(const WField *f, int n UNUSED, char *err, int errcap)
{
    const MInfo *mi = cur_info;
    int base = nested_field != 0 ? 1 : 0;

    for (size_t i = 0; i < mi->nfields; i++) {
        const MField *mf = &mi->fields[i];
        const char *v = f[base + isz(i)].val;

        if (mf->kind == MF_TEXT && mf->required != 0 && v[0] == '\0') {
            snprintf(err, usz(errcap), "No string specified");

            return 0;
        }

        if (mi->type == 2 && i == 0 && cur_doc != NULL &&
            doc_find_section(cur_doc, v) < 0) {
            snprintf(err, usz(errcap), "Cannot find section number");

            return 0;
        }

        if (mi->type == 17 && i == 0 && v[0] != '\0') {
            int c;

            if (marker_parse_num(v, 0, &c) != 0 || c < 1 ||
                c > MAX_COUNTERS) {
                snprintf(err, usz(errcap), "Counter must be 1 to %d",
                         MAX_COUNTERS);

                return 0;
            }
        }

        if (mi->type == 17 && i == 1 && v[0] != '\0') {
            uint32_t c;

            if (marker_parse_u32(v, &c) != 0) {
                snprintf(err, usz(errcap), "Set Value must be 0 to %u",
                         COUNTER_MAX);

                return 0;
            }

            continue;
        }

        if (mf->kind == MF_NUM && v[0] != '\0' && v[0] != '[') {
            int x;

            if (mf->dflt == NULL || strcmp(v, mf->dflt) != 0) {
                if (marker_parse_num(v, mf->decimals, &x) != 0) {
                    snprintf(err, usz(errcap), "Not a valid number (%s)",
                             mf->label);

                    return 0;
                }

                /* The shades are 0.0 to 100.0 and the scale 0.001 to 10. */
                if (mi->type == 37 && i >= 1 && i <= 2 &&
                    (x < 0 || x > 1000)) {
                    snprintf(err, usz(errcap), "%s must be 0.0 to 100.0",
                             mf->label);

                    return 0;
                }

                if (mi->type == 37 && i == 3 && (x < 1 || x > 10000)) {
                    snprintf(err, usz(errcap), "Scale must be 0.001 to 10.000");

                    return 0;
                }
            }
        }
    }

    return 1;
}


int
markerui_edit(const Doc *doc, const MInfo *mi, const uint8_t *data, size_t len,
              uint8_t **out, size_t *outlen, NestedEditor nested_edit)
{
    WField f[MAXPANEL];
    MPayload p;
    int n = 0;
    int result = 0;
    int editable = 0;

    memset(f, 0, sizeof f);
    mp_parse(&p, mi, data, len);
    cur_info = mi;
    cur_doc = doc;
    nested_field = 0;

    if (mi->nested != 0 && nested_edit != NULL) {
        /* The text is edited first, in an editor of its own. */
        uint8_t *nb;
        size_t nlen;

        if (nested_edit(mi->name, p.nested, p.nested_len, &nb, &nlen) == 0) {
            mp_free(&p);

            return 0;
        }

        free(p.nested);
        p.nested = nb;
        p.nested_len = nlen;
    } else if (mi->nested != 0) {
        char text[160];

        f[n].label = mi->type == 9 ? "Equation" :
                     mi->type == 6 ? "Comment" :
                                     "Note";

        if (p.nested_len == 0 || mp_nested_plain(&p, text, sizeof text) != 0) {
            f[n].kind = WF_TEXT;
            str_copy(f[n].val, sizeof f[n].val, p.nested_len == 0 ? "" : text);
            editable = 1;
        } else {
            f[n].kind = WF_INFO;
            str_copy(f[n].val, sizeof f[n].val, "(formatted text)");
        }

        nested_field = 1;
        n++;
    }

    for (size_t i = 0; i < mi->nfields && n < MAXPANEL; i++) {
        const MField *mf = &mi->fields[i];
        const char *v = mp_get(&p, mf->id);

        f[n].label = mf->label;

        if (mf->kind == MF_CHOICE) {
            f[n].kind = WF_CHOICE;
            f[n].choices = (mi->type == 15 && strcmp(mf->id, "1") == 0) ||
                                   (mi->type == 37 &&
                                    strcmp(mf->id, "5") == 0) ?
                               font_choices() :
                               mf->choices;

            /* A field with a stored default has no record for that
               entry, which need not be the first one of the list. */
            if (v != NULL) {
                f[n].sel = atoi(v) - mf->base;
            } else if (mf->dflt != NULL) {
                f[n].sel = atoi(mf->dflt) - mf->base;
            } else {
                f[n].sel = 0;
            }

            if (f[n].sel < 0) {
                f[n].sel = 0;
            }
        } else if (mf->kind == MF_NUM) {
            f[n].kind = WF_TEXT;

            if (v != NULL) {
                marker_fmt_num(atoi(v), mf->decimals, 0, f[n].val,
                               sizeof f[n].val);
            } else if (mf->dflt != NULL) {
                str_copy(f[n].val, sizeof f[n].val, mf->dflt);
            }
        } else {
            const char *suf = file_suffixes(mi->type, mf->id);

            f[n].kind = WF_TEXT;
            str_copy(f[n].val, sizeof f[n].val,
                     v != NULL        ? v :
                     mf->dflt != NULL ? mf->dflt :
                                        "");

            if (suf != NULL) {
                f[n].pick = PICK_FILE;
                f[n].suffixes = suf;
            }
        }

        n++;
    }

    if (mi->type == 37) {
        keycap_globals(doc, &p, nested_field != 0 ? 1 : 0, f);
    }

    if (mi->type == 1) {
        /* The first choice is the Primary Note Type, in brackets. */
        static const char *const names[] = {"[Footnote]", "[Levelnote]",
                                            "[Endnote]"};
        static const char *choices[5];
        NoteFmt nf;

        note_format_get(doc, &nf);
        choices[0] = names[nf.primary];
        choices[1] = "Footnote";
        choices[2] = "Levelnote";
        choices[3] = "Endnote";
        choices[4] = NULL;
        f[nested_field != 0 ? 1 : 0].choices = choices;
    }

    if (mi->type == 6) {
        /* A comment has the name of its author and the time it was made;
           the time stays as it was when the comment is edited. */
        int nm = nested_field != 0 ? 1 : 0;
        const char *who = getenv("USER");

        if (f[nm].val[0] == '\0' && who != NULL) {
            str_copy(f[nm].val, sizeof f[nm].val, who);
        }

        if (f[nm + 1].val[0] == '\0') {
            time_t now = time(NULL);
            struct tm tm = *localtime(&now);

            strftime(f[nm + 1].val, sizeof f[nm + 1].val, "%Y-%m-%d %H:%M",
                     &tm);
        }

        f[nm + 1].kind = WF_INFO;
    }

    if (mi->type == 2 && n + 2 <= MAXPANEL) {
        /* The tag and headline of the section that is referred to. */
        static char tag[48];
        static char head[160];
        const char *num = mp_get(&p, "XN");

        section_texts(doc, num != NULL ? doc_find_section(doc, num) : -1, tag,
                      sizeof tag, head, sizeof head);
        f[n].label = "Section Tag";
        f[n].kind = WF_INFO;
        str_copy(f[n].val, sizeof f[n].val, tag);
        n++;
        f[n].label = "Headline";
        f[n].kind = WF_INFO;
        str_copy(f[n].val, sizeof f[n].val, head);
        n++;
    }

    if (n == 0) {
        /* A marker without a panel is inserted as it is. */
        *out = mp_build(&p, outlen);
        mp_free(&p);

        return 1;
    }

    char place[80];

    snprintf(place, sizeof place, "Marker:%s", mi->name);
    w_set_help(place);
    int accepted = popup_form(mi->name, f, n, check);

    w_set_help(NULL);

    if (accepted != 0) {
        int base = 0;

        if (nested_field != 0) {
            base = 1;

            if (editable != 0 && f[0].val[0] != '\0') {
                mp_set_nested_plain(&p, f[0].val);
            }
        }

        for (size_t i = 0; i < mi->nfields; i++) {
            const MField *mf = &mi->fields[i];
            const WField *w = &f[base + isz(i)];
            char num[32];

            if (mf->kind == MF_CHOICE) {
                if (mf->dflt != NULL ? w->sel + mf->base == atoi(mf->dflt) :
                                       w->sel == 0) {
                    mp_set(&p, mf->id, NULL, 0);
                } else {
                    snprintf(num, sizeof num, "%d", w->sel + mf->base);
                    mp_set(&p, mf->id, num, 0);
                }
            } else if (mi->type == 17 && strcmp(mf->id, "2") == 0) {
                /* The Set Value of a counter: 0 to COUNTER_MAX. */
                uint32_t u;

                if (marker_parse_u32(w->val, &u) != 0) {
                    mp_set(&p, mf->id, NULL, 0);
                } else {
                    snprintf(num, sizeof num, "%" PRIu32, u);
                    mp_set(&p, mf->id, num, 0);
                }
            } else if (mf->kind == MF_NUM) {
                int v;

                if (w->val[0] == '\0' || w->val[0] == '[' ||
                    (mf->dflt != NULL &&
                     same_value(w->val, mf->dflt, mf->decimals)) ||
                    marker_parse_num(w->val, mf->decimals, &v) != 0) {
                    mp_set(&p, mf->id, NULL, 0);
                } else {
                    snprintf(num, sizeof num, "%d", v);
                    mp_set(&p, mf->id, num, 0);
                }
            } else if (w->val[0] == '\0' ||
                       (mf->dflt != NULL && strcmp(w->val, mf->dflt) == 0)) {
                mp_set(&p, mf->id, NULL, 1);
            } else {
                mp_set(&p, mf->id, w->val, 1);
            }
        }

        *out = mp_build(&p, outlen);
        result = 1;
    }

    mp_free(&p);

    return result;
}
