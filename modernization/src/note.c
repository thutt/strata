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

/* note.c - the settings of notes, their marks, and the kind of a note. */
#include "util.h"
#include "note.h"
#include "marker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const dflt_ref[NK_COUNT] = {"\\1\\", "\\a\\", "[\\1\\]"};
static const char *const dflt_note[NK_COUNT] = {"\\1\\", "\\a\\)", "\\1\\."};
static const int dflt_refattr[NK_COUNT] = {NA_SUPER, NA_SUPER, 0};
static const int dflt_noteattr[NK_COUNT] = {NA_BOLD, NA_BOLD, 0};


static int
rec_int(const Doc *d, const char *code, int dflt)
{
    const char *v = doc_opt_get(d, code);

    return v != NULL && v[0] != '\0' ? atoi(v) : dflt;
}


static void
rec_text(const Doc *d, const char *code, const char *dflt, char *out,
         size_t cap)
{
    const char *v = doc_opt_get(d, code);

    str_copy(out, cap, v != NULL ? v : dflt);
}


void
note_format_get(const Doc *d, NoteFmt *f)
{
    memset(f, 0, sizeof *f);

    for (int k = 0; k < NK_COUNT; k++) {
        char code[16];
        NoteKind *n = &f->k[k];

        snprintf(code, sizeof code, "N%dR", k + 1);
        rec_text(d, code, dflt_ref[k], n->reftag, sizeof n->reftag);
        snprintf(code, sizeof code, "N%dN", k + 1);
        rec_text(d, code, dflt_note[k], n->notetag, sizeof n->notetag);
        snprintf(code, sizeof code, "N%dX", k + 1);
        n->refattr = rec_int(d, code, dflt_refattr[k]);
        snprintf(code, sizeof code, "N%dY", k + 1);
        n->noteattr = rec_int(d, code, dflt_noteattr[k]);
        snprintf(code, sizeof code, "N%dSL", k + 1);
        n->sp_line = rec_int(d, code, 100);
        snprintf(code, sizeof code, "N%dST", k + 1);
        n->sp_block = rec_int(d, code, 150);
        snprintf(code, sizeof code, "N%dSN", k + 1);
        n->sp_note = rec_int(d, code, 150);
        snprintf(code, sizeof code, "NS%d", k + 1);
        n->sep = rec_int(d, code, 2000);
        snprintf(code, sizeof code, "NZ%d", k + 1);
        n->renumber = rec_int(d, code, 0) != 0;
    }

    f->bottom = rec_int(d, "NB", 1) != 0;
    f->level = rec_int(d, "NL", 1);
    rec_text(d, "NE", "Endnotes", f->title, sizeof f->title);

    int p = rec_int(d, "NT", 1);

    f->primary = p >= 2 && p <= 3 ? p - 1 : NK_FOOT;
}


/* Set a record: no record when the value is the default. */
static void
put_int(Doc *d, const char *code, int v, int dflt)
{
    char s[24];
    const char *cur = doc_opt_get(d, code);
    int have = cur != NULL && cur[0] != '\0';

    if (v == dflt) {
        if (have != 0) {
            doc_opt_set(d, code, NULL, 0);
        }

        return;
    }

    snprintf(s, sizeof s, "%d", v);

    if (have == 0 || strcmp(cur, s) != 0) {
        doc_opt_set(d, code, s, 0);
    }
}


static void
put_text(Doc *d, const char *code, const char *v, const char *dflt)
{
    const char *cur = doc_opt_get(d, code);
    int have = cur != NULL;

    if (strcmp(v, dflt) == 0) {
        if (have != 0) {
            doc_opt_set(d, code, NULL, 0);
        }

        return;
    }

    if (have == 0 || strcmp(cur, v) != 0) {
        doc_opt_set(d, code, v, 1);
    }
}


void
note_format_set(Doc *d, const NoteFmt *f)
{
    for (int k = 0; k < NK_COUNT; k++) {
        char code[16];
        const NoteKind *n = &f->k[k];

        snprintf(code, sizeof code, "N%dR", k + 1);
        put_text(d, code, n->reftag, dflt_ref[k]);
        snprintf(code, sizeof code, "N%dN", k + 1);
        put_text(d, code, n->notetag, dflt_note[k]);
        snprintf(code, sizeof code, "N%dX", k + 1);
        put_int(d, code, n->refattr, dflt_refattr[k]);
        snprintf(code, sizeof code, "N%dY", k + 1);
        put_int(d, code, n->noteattr, dflt_noteattr[k]);
        snprintf(code, sizeof code, "N%dSL", k + 1);
        put_int(d, code, n->sp_line, 100);
        snprintf(code, sizeof code, "N%dST", k + 1);
        put_int(d, code, n->sp_block, 150);
        snprintf(code, sizeof code, "N%dSN", k + 1);
        put_int(d, code, n->sp_note, 150);
        snprintf(code, sizeof code, "NS%d", k + 1);
        put_int(d, code, n->sep, 2000);
        snprintf(code, sizeof code, "NZ%d", k + 1);
        put_int(d, code, n->renumber, 0);
    }

    put_int(d, "NB", f->bottom, 1);
    put_int(d, "NL", f->level, 1);
    put_text(d, "NE", f->title, "Endnotes");
    put_int(d, "NT", f->primary == NK_FOOT ? 1 : f->primary + 1, 1);
}


void
note_tag_text(const char *tag, int n, char *out, size_t cap)
{
    int coded = 0;

    for (const char *p = tag; p[0] != '\0' && p[1] != '\0' && p[2] != '\0';
         p++) {
        if (p[0] == '\\' && p[2] == '\\' &&
            (p[1] == '1' || p[1] == 'i' || p[1] == 'I' || p[1] == 'A' ||
             p[1] == 'a')) {
            coded = 1;
        }
    }

    if (coded != 0) {
        marker_counter_text(tag, 0, uns(n), out, cap);

        return;
    }

    /* Plain text stands for itself, once for the first note, twice for
       the second, and so on. */
    size_t o = 0;
    size_t tl = strlen(tag);

    out[0] = '\0';

    for (int i = 0; i < n && tl > 0 && o + tl < cap; i++) {
        memcpy(out + o, tag, tl);
        o += tl;
        out[o] = '\0';
    }
}


int
note_kind(const Tok *k, int primary)
{
    MPayload p;
    int kind = primary;

    if (k->kind != TK_MARKER || tok_mtype(k) != 1) {
        return NK_FOOT;
    }

    mp_parse(&p, marker_info(1), tok_mdata(k), tok_mlen(k));

    const char *v = mp_get(&p, "1");

    if (v != NULL && atoi(v) >= 1 && atoi(v) <= 3) {
        kind = atoi(v) - 1;
    }

    mp_free(&p);

    return kind;
}


int
note_text(const Tok *k, char *buf, int cap)
{
    return marker_nested_text(k, buf, cap);
}
