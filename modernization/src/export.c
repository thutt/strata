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

/* export.c - formatted text rendering (Print/Preview/Export) and ASCII
   import. */
#include "util.h"
#include "ui.h"
#include "note.h"
#include "marker.h"
#include "fileops.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


/* ------------------------------------------------------------------ notes */
/*
 * The notes of a printout.  The text printout has no pages, so a footnote
 * is printed after the block that has its mark, a levelnote after the
 * section at the note level that it is in, and endnotes at the end under
 * the title of the note page (the Note Format settings).
 */
typedef struct {
    int after; /* the item after whose last line the note is printed */
    int kind;
    int number;
    char text[600];
} PNote;


static void
add_line(char ***out, int *n, int *cap, const char *s)
{
    if (*n == *cap) {
        *cap *= 2;
        *out = realloc(*out, usz(*cap) * sizeof **out);
    }

    (*out)[(*n)++] = strdup(s);
}


/* Print one note: its tag, then its text, wrapped at width. */
static void
add_note(char ***out, int *n, int *cap, const char *tag, const char *text,
         int width)
{
    char line[256];
    int lw = width < 250 ? width : 250;
    int pos = 0;
    int ind = slen(tag) + 1;
    const char *p = text;

    snprintf(line, sizeof line, "%s ", tag);
    pos = slen(line);

    while (*p != '\0') {
        char word[200];
        int wl = 0;

        while (*p == ' ' || *p == '\n') {
            p++;
        }

        while (*p != '\0' && *p != ' ' && *p != '\n' && wl < 199) {
            word[wl++] = *p++;
        }

        word[wl] = '\0';

        if (wl == 0) {
            break;
        }

        if (pos + wl > lw && pos > ind) {
            while (pos > 0 && line[pos - 1] == ' ') {
                pos--;
            }

            line[pos] = '\0';
            add_line(out, n, cap, line);
            memset(line, ' ', usz(ind));
            pos = ind;
        }

        for (int i = 0; i < wl && pos < 250; i++) {
            line[pos++] = word[i];
        }

        if (pos < 250) {
            line[pos++] = ' ';
        }
    }

    while (pos > 0 && line[pos - 1] == ' ') {
        pos--;
    }

    line[pos] = '\0';
    add_line(out, n, cap, line);
}


/* The notes of the document, in the order of the text. */
static PNote *
collect_notes(const Doc *d, const NoteFmt *nf, int *count)
{
    int cap = 8;
    int n = 0;
    PNote *pn = malloc(usz(cap) * sizeof *pn);
    int last = d->nitems - 1;

    while (last > 0 && d->items[last].type != IT_BLOCK) {
        last--;
    }

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        const Block *b = d->items[i].blk;

        for (int c = 0; c < b->ncols; c++) {
            const Text *t = &b->cols[c].text;

            for (int j = 0; j < t->n; j++) {
                const Tok *k = &t->v[j];

                if (k->kind != TK_MARKER || tok_mtype(k) != 1) {
                    continue;
                }

                if (n == cap) {
                    cap *= 2;
                    pn = realloc(pn, usz(cap) * sizeof *pn);
                }

                pn[n].kind = note_kind(k, nf->primary);
                pn[n].number = marker_note_number(k);
                pn[n].after = i;

                if (marker_nested_text(k, pn[n].text, sizeof pn[n].text) <
                    0) {
                    pn[n].text[0] = '\0';
                }

                if (pn[n].kind == NK_END) {
                    pn[n].after = last;
                } else if (pn[n].kind == NK_LEVEL && d->structured != 0) {
                    int s = sect_start(d, i);

                    while (s > 0 && doc_item_level(d, s) > nf->level) {
                        int q = s - 1;

                        while (q > 0 &&
                               !(d->items[q].type == IT_SECT &&
                                 doc_item_level(d, q) <
                                     doc_item_level(d, s))) {
                            q--;
                        }

                        s = q;
                    }

                    if (s >= 0) {
                        int e = sect_end(d, s);

                        while (e > 0 && (e >= d->nitems ||
                                         d->items[e].type != IT_BLOCK)) {
                            e--;
                        }

                        pn[n].after = e;
                    }
                } else if (pn[n].kind == NK_LEVEL) {
                    pn[n].after = last;
                }

                n++;
            }
        }
    }

    *count = n;

    return pn;
}


/* Print the notes that go after item it. */
static void
flush_notes(char ***out, int *n, int *cap, const PNote *pn, int npn, int it,
            const NoteFmt *nf, int width)
{
    for (int kind = 0; kind < NK_COUNT; kind++) {
        int first = 1;
        int lastnum = 0;

        for (int i = 0; i < npn; i++) {
            char tag[96];
            int num = pn[i].number;

            if (pn[i].after != it || pn[i].kind != kind) {
                continue;
            }

            if (first != 0) {
                if (kind == NK_END) {
                    add_line(out, n, cap, "");
                    add_line(out, n, cap, nf->title);
                    add_line(out, n, cap, "");
                } else {
                    char bar[64];
                    int bl = nf->k[kind].sep / 100;

                    bl = bl > width ? width : bl;
                    bl = bl > 60 ? 60 : bl;
                    memset(bar, '-', usz(bl));
                    bar[bl] = '\0';
                    add_line(out, n, cap, "");
                    add_line(out, n, cap, bar);
                }

                first = 0;
            }

            /* A levelnote list that is numbered again starts at 1. */
            if (kind == NK_LEVEL && nf->k[kind].renumber != 0) {
                num = ++lastnum;
            }

            note_tag_text(nf->k[kind].notetag, num, tag, sizeof tag);
            add_note(out, n, cap, tag, pn[i].text, width);
        }
    }
}


char **
render_lines(Doc *d, int width, int *n)
{
    int saved_marks = layout_show_marks;
    int saved_dots = layout_show_dots;
    int saved_shy = layout_hide_shy;
    int saved_hide = marker_hide_perr;

    marker_hide_perr = 1;

    layout_show_marks = 0;
    layout_show_dots = 0;
    layout_hide_shy = 1;
    Editor *e = ed_new(d);
    uint8_t *folds = malloc(usz(d->nitems + 1));

    for (int i = 0; i < d->nitems; i++) {
        folds[i] = d->items[i].fold;
        d->items[i].fold = 0;
    }

    e->text_width = width;
    ed_layout(e);
    View *v = &e->view;
    int ocap = v->n + 8;
    char **out = malloc(usz(ocap) * sizeof(char *));
    int k = 0;
    PNote *pn = NULL;
    int npn = 0;
    NoteFmt nf;

    note_format_get(d, &nf);

    if (marker_print_mode != 0) {
        pn = collect_notes(d, &nf, &npn);
    }

    Cell *cells = malloc(usz(v->width) * sizeof(Cell));

    for (int i = 0; i < v->n; i++) {
        const VLine *l = &v->lines[i];

        /*
         * A block of Keep markers only prints nothing: not its line, and
         * not the blank line that separates it from the next block.
         */
        if (marker_print_mode != 0 && l->item >= 0 &&
            d->items[l->item].type == IT_BLOCK &&
            block_only_keep(d->items[l->item].blk) != 0) {
            continue;
        }

        view_line_cells(e, i, cells);

        char *s = malloc(usz(v->width + 1));
        int len = v->width;

        for (int x = 0; x < v->width; x++) {
            uint8_t c = cells[x].ch;

            /*
             * The mark between columns and the dim line that separates two
             * blocks on the screen are not part of the text; the borders are
             * (the cells of a border are never dim).
             */
            if ((c == ':' && cells[x].attr == DA_DIM) ||
                (l->kind != 0 && cells[x].attr == DA_DIM)) {
                c = ' ';
            }

            s[x] = c != '\0' ? chr(c) : ' ';
        }

        while (len > 0 && s[len - 1] == ' ') {
            len--;
        }

        s[len] = 0;
        add_line(&out, &k, &ocap, s);
        free(s);

        if (l->kind != 0) {
            continue;
        }

        if (npn > 0) {
            int nx = i + 1;

            while (nx < v->n && v->lines[nx].kind != 0) {
                nx++;
            }

            if (nx >= v->n || v->lines[nx].item != l->item) {
                flush_notes(&out, &k, &ocap, pn, npn, l->item, &nf, width);
            }
        }
    }

    free(pn);

    free(cells);

    for (int i = 0; i < d->nitems; i++) {
        d->items[i].fold = folds[i];
    }

    free(folds);
    e->doc = NULL;
    ed_free(e);
    layout_show_marks = saved_marks;
    layout_show_dots = saved_dots;
    layout_hide_shy = saved_shy;
    marker_hide_perr = saved_hide;

    *n = k;
    return out;
}


void
free_lines(char **l, int n)
{
    for (int i = 0; i < n; i++) {
        free(l[i]);
    }

    free(l);
}


static void
put_utf8(FILE *f, unsigned char ch)
{
    utf8_put(f, cp437_uni[ch]);
}


/* Write rendered lines to a file as text, the box characters as plain ones. */
static int
export_lines(char **lines, int n, const char *path, char *err, size_t errlen)
{
    FILE *f = fopen(path, "w");

    if (f == NULL) {
        snprintf(err, errlen, "cannot write %s", path);

        return -1;
    }

    for (int i = 0; i < n; i++) {
        for (const char *s = lines[i]; *s != '\0'; s++) {
            uint8_t ch = u8(*s);

            if (ch >= 0x80 || ch < 0x20) {
                if (ch >= 0xB0 && ch <= 0xDF) {
                    fputc(ch == 0xCD ? '=' : ch == 0xC4 ? '-' :
                          ch == 0xB3 || ch == 0xBA ? '|' : '+', f);
                } else {
                    put_utf8(f, ch);
                }
            } else {
                fputc(ch, f);
            }
        }

        fputc('\n', f);
    }

    if (fclose(f) != 0) {
        snprintf(err, errlen, "write error on %s", path);

        return -1;
    }

    return 0;
}


int
export_text(Doc *d, const char *path, int width, char *err, size_t errlen)
{
    int n;
    char **lines = render_lines(d, width, &n);
    int r = export_lines(lines, n, path, err, errlen);

    free_lines(lines, n);

    return r;
}


/* Import an ASCII file: each paragraph (separated by blank lines) becomes a
   block. */


Doc *
import_text(const char *path, int structured, char *err, size_t errlen)
{
    FILE *f = fopen(path, "r");

    if (f == NULL) {
        snprintf(err, errlen, "cannot open %s", path);
        return NULL;
    }

    Doc *d = doc_new(structured);
    /* remove the placeholder content: keep S(+)S, drop the empty block,
       refill */
    int bi = -1;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == IT_BLOCK) {
            bi = i;
        }
    }

    Block *cur = d->items[bi].blk;
    char line[4096];
    int blank_run = 1;
    int have = 0;
    Text *t = &cur->cols[0].text;

    text_delete(t, 0, t->n);
    int at = bi;

    while (fgets(line, sizeof line, f) != NULL) {
        size_t l = strlen(line);

        while (l != 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) {
            line[--l] = 0;
        }

        int blank = 1;

        for (size_t i = 0; i < l; i++) {
            if (!is_space(line[i])) {
                blank = 0;
            }
        }

        if (blank != 0) {
            if (have != 0 && blank_run == 0) {
                Tok nl;

                memset(&nl, 0, sizeof nl);
                nl.kind = TK_NL;
                text_insert(t, t->n, nl);
                Block *nb = block_new(1);

                doc_insert_item(d, ++at, IT_BLOCK, nb);
                t = &nb->cols[0].text;
                have = 0;
            }

            blank_run = 1;
            continue;
        }

        if (have != 0) {
            text_insert(t, t->n, tok_char(' '));
        }

        for (size_t i = 0; i < l; i++) {
            if (line[i] == '\t') {
                Tok k;

                memset(&k, 0, sizeof k);
                k.kind = TK_TAB;
                k.a = ' ';
                text_insert(t, t->n, k);
            } else {
                text_insert(t, t->n, tok_char(u8(line[i])));
            }
        }

        have = 1;
        blank_run = 0;
    }

    Tok nl;

    memset(&nl, 0, sizeof nl);
    nl.kind = TK_NL;
    text_insert(t, t->n, nl);

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == IT_BLOCK) {
            d->items[i].blk->dirty = 1;
        }
    }

    fclose(f);
    doc_touch(d);

    return d;
}
