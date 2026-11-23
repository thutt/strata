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

/* library.c - the library of cut buffers; see library.h and TOOLS-MENU.txt. */
#include "util.h"
#include "library.h"
#include "macro.h"
#include "filemenu.h"
#include "marker.h"
#include "widgets.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define LIB_MAX 200

typedef struct {
    char name[32];
    char desc[80];
    Clip clip;
} LibEntry;

static LibEntry entries[LIB_MAX];
static int nentries;
static char lib_desc[160];
static char lib_author[160];
static int lib_rev;

static const char *const include_choices[] = {"No", "Yes", NULL};
static const char *const retrieve_choices[] = {"Overwrite Current Library "
                                               "Entries",
                                               "Merge with Current Library "
                                               "Entries",
                                               NULL};


int
library_count(void)
{
    return nentries;
}


static int
lib_find(const char *name)
{
    for (int i = 0; i < nentries; i++) {
        if (strcasecmp(entries[i].name, name) == 0) {
            return i;
        }
    }

    return -1;
}


/* The names of the entries, for the checklist of a name field. */
static const char *const *
lib_names(void)
{
    static const char *names[LIB_MAX + 1];

    for (int i = 0; i < nentries; i++) {
        names[i] = entries[i].name;
    }

    names[nentries] = NULL;

    return names;
}


/* The letter that tells the kind of an entry in a library file. */
static char
kind_letter(int kind)
{
    return kind == SEL_SECTION ? 'S' : kind == SEL_BLOCK ? 'B' : 'T';
}


/*
 * Make or replace the entry called name; the entry takes the clip.  The
 * entries stay in the alphabetical order of the format.
 */
static void
lib_put(const char *name, const char *desc, Clip *clip)
{
    int at = lib_find(name);

    if (at >= 0) {
        clip_free(&entries[at].clip);
    } else {
        if (nentries == LIB_MAX) {
            clip_free(clip);

            return;
        }

        at = nentries;

        while (at > 0 && strcasecmp(entries[at - 1].name, name) > 0) {
            entries[at] = entries[at - 1];
            at--;
        }

        nentries++;
    }

    str_copy(entries[at].name, sizeof entries[at].name, name);
    str_copy(entries[at].desc, sizeof entries[at].desc, desc);
    entries[at].clip = *clip;
    memset(clip, 0, sizeof *clip);
}


static void
lib_remove(int at)
{
    clip_free(&entries[at].clip);

    for (int i = at; i + 1 < nentries; i++) {
        entries[i] = entries[i + 1];
    }

    nentries--;
    memset(&entries[nentries], 0, sizeof entries[nentries]);
}


/* ----------------------------------------------------------- the panels */
static void
text_field(WField *f, const char *label, const char *val, int pick,
           const char *suffixes)
{
    memset(f, 0, sizeof *f);
    f->label = label;
    f->kind = WF_TEXT;
    str_copy(f->val, sizeof f->val, val);
    f->pick = pick;
    f->suffixes = suffixes;
}


static void
name_field(WField *f, const char *label, const char *val)
{
    text_field(f, label, val, PICK_LIST, NULL);
    f->list = lib_names();
}


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


/* ------------------------------------------------- entries and documents */
static Block *
text_block(const char *s)
{
    Block *b = block_new(1);
    Tok nl;

    memset(&nl, 0, sizeof nl);
    nl.kind = TK_NL;

    for (; *s != '\0'; s++) {
        text_insert(&b->cols[0].text, b->cols[0].text.n, tok_char(u8(*s)));
    }

    text_insert(&b->cols[0].text, b->cols[0].text.n, nl);

    return b;
}


/* The blocks of an entry's contents, in a section of a new document. */
static void
add_contents(Doc *d, const Clip *c)
{
    if (c->kind == SEL_SECTION) {
        for (int i = 0; i < c->n; i++) {
            Block *b = c->items[i].blk != NULL ?
                           block_clone_full(c->items[i].blk) :
                           NULL;

            doc_insert_item(d, d->nitems, c->items[i].type, b);
        }

        return;
    }

    doc_insert_item(d, d->nitems, IT_SECT, NULL);

    if (c->kind == SEL_TEXT) {
        Block *b = block_new(1);
        Tok nl;

        memset(&nl, 0, sizeof nl);
        nl.kind = TK_NL;

        for (int i = 0; i < c->text.n; i++) {
            text_insert(&b->cols[0].text, b->cols[0].text.n,
                        tok_dup(&c->text.v[i]));
        }

        if (c->text.n == 0 || c->text.v[c->text.n - 1].kind != TK_NL) {
            text_insert(&b->cols[0].text, b->cols[0].text.n, nl);
        }

        doc_insert_item(d, d->nitems, IT_BLOCK, b);

        return;
    }

    for (int i = 0; i < c->n; i++) {
        if (c->items[i].type == IT_BLOCK) {
            doc_insert_item(d, d->nitems, IT_BLOCK,
                            block_clone_full(c->items[i].blk));
        }
    }
}


/* A library as a document: entries are sections of the root section. */
static Doc *
lib_to_doc(void)
{
    Doc *d = doc_new(1);

    doc_delete_items(d, 0, d->nitems);
    doc_insert_item(d, d->nitems, IT_SECT, NULL);
    doc_insert_item(d, d->nitems, IT_BLOCK, block_new(1));
    doc_insert_item(d, d->nitems, IT_PLUS, NULL);

    for (int i = 0; i < nentries; i++) {
        char head[64];

        snprintf(head, sizeof head, "%c%.62s",
                 kind_letter(entries[i].clip.kind), entries[i].name);
        doc_insert_item(d, d->nitems, IT_SECT, NULL);
        doc_insert_item(d, d->nitems, IT_BLOCK, text_block(head));
        doc_insert_item(d, d->nitems, IT_BLOCK, text_block(entries[i].desc));
        doc_insert_item(d, d->nitems, IT_PLUS, NULL);
        add_contents(d, &entries[i].clip);
        doc_insert_item(d, d->nitems, IT_MINUS, NULL);
    }

    doc_insert_item(d, d->nitems, IT_MINUS, NULL);

    return d;
}


/* The first line of a block's first column. */
static void
block_line(const Block *b, char *buf, int cap)
{
    buf[0] = '\0';

    if (b != NULL && b->ncols > 0) {
        text_plain(&b->cols[0].text, buf, cap);
    }
}


/*
 * Read the entries of a library document into the library.  Returns the
 * number of entries read, or -1 when the document is not a library.
 */
static int
lib_from_doc(const Doc *d, int merge)
{
    int n = d->nitems;
    int i = 0;
    int count = 0;
    LibEntry *fresh = calloc(LIB_MAX, sizeof *fresh);
    int nfresh = 0;

    if (d->structured == 0 || n < 2 || d->items[0].type != IT_SECT) {
        free(fresh);

        return -1;
    }

    while (i < n && d->items[i].type != IT_PLUS) {
        i++;
    }

    i++;

    while (i < n && d->items[i].type == IT_SECT && nfresh < LIB_MAX) {
        char head[64];
        char desc[96];
        char kind;
        Clip clip;
        int start;
        int j;
        int depth = 1;

        i++;
        head[0] = '\0';
        desc[0] = '\0';

        if (i < n && d->items[i].type == IT_BLOCK) {
            block_line(d->items[i].blk, head, sizeof head);
            i++;
        }

        if (i < n && d->items[i].type == IT_BLOCK) {
            block_line(d->items[i].blk, desc, sizeof desc);
            i++;
        }

        if (i >= n || d->items[i].type != IT_PLUS) {
            break;
        }

        start = i + 1;

        for (j = start; j < n && depth > 0; j++) {
            if (d->items[j].type == IT_PLUS) {
                depth++;
            } else if (d->items[j].type == IT_MINUS) {
                depth--;
            }
        }

        i = j;
        kind = head[0];
        memset(&clip, 0, sizeof clip);

        if (kind == 'S') {
            clip.kind = SEL_SECTION;
            clip.n = j - 1 - start;
            clip.items = calloc(usz(clip.n > 0 ? clip.n : 1), sizeof(Item));

            for (int k = 0; k < clip.n; k++) {
                const Item *src = &d->items[start + k];

                clip.items[k].type = src->type;
                clip.items[k].fold = src->fold;
                clip.items[k].se = src->se;
                clip.items[k].blk = src->blk != NULL ?
                                        block_clone_full(src->blk) :
                                        NULL;
            }
        } else if (kind == 'T') {
            clip.kind = SEL_TEXT;

            for (int k = start; k < j - 1; k++) {
                if (d->items[k].type == IT_BLOCK) {
                    const Text *t = &d->items[k].blk->cols[0].text;

                    for (int m = 0; m < t->n; m++) {
                        text_insert(&clip.text, clip.text.n,
                                    tok_dup(&t->v[m]));
                    }

                    break;
                }
            }
        } else {
            clip.kind = SEL_BLOCK;

            for (int k = start; k < j - 1; k++) {
                if (d->items[k].type == IT_BLOCK) {
                    clip.n++;
                }
            }

            clip.items = calloc(usz(clip.n > 0 ? clip.n : 1), sizeof(Item));

            int at = 0;

            for (int k = start; k < j - 1; k++) {
                if (d->items[k].type == IT_BLOCK) {
                    clip.items[at].type = IT_BLOCK;
                    clip.items[at].blk = block_clone_full(d->items[k].blk);
                    at++;
                }
            }
        }

        str_copy(fresh[nfresh].name, sizeof fresh[nfresh].name,
                 kind == 'S' || kind == 'T' || kind == 'B' || kind == 'C' ?
                     head + 1 :
                     head);
        str_copy(fresh[nfresh].desc, sizeof fresh[nfresh].desc, desc);
        fresh[nfresh].clip = clip;
        nfresh++;
    }

    if (merge == 0) {
        while (nentries > 0) {
            lib_remove(nentries - 1);
        }
    }

    for (int k = 0; k < nfresh; k++) {
        lib_put(fresh[k].name, fresh[k].desc, &fresh[k].clip);
        count++;
    }

    free(fresh);

    return count;
}


/* The body records of the contents of an entry, for the text editor. */
static uint8_t *
contents_bytes(const Clip *c, size_t *len)
{
    Doc *t = doc_new(0);
    size_t n;
    uint8_t *all;
    uint8_t *out;
    const char *begin = "*B0\r\n";
    size_t bl = strlen(begin);
    size_t at = 0;

    doc_delete_items(t, 0, t->nitems);
    add_contents(t, c);
    all = doc_serialize(t, &n);
    doc_free(t);

    while (at + bl <= n && memcmp(all + at, begin, bl) != 0) {
        at++;
    }

    at += bl;

    /* The body ends where the "*E" record begins. */
    size_t end = n;

    while (end >= 4 && memcmp(all + end - 4, "*E\r\n", 4) != 0) {
        end--;
    }

    end = end >= 4 ? end - 4 : n;
    *len = end > at ? end - at : 0;
    out = malloc(*len + 1);

    if (*len > 0) {
        memcpy(out, all + at, *len);
    }

    free(all);

    return out;
}


/* Put the blocks that the text editor returned back into a clip. */
static int
contents_clip(const uint8_t *bytes, size_t len, int kind, Clip *out)
{
    static const char head[] = DOC_FIRST_RECORD "\r\nMR1006\r\n*B0\r\n";
    static const char foot[] = "*E\r\n";
    char err[96];
    size_t hl = sizeof head - 1;
    uint8_t *buf = malloc(hl + len + sizeof foot);

    memcpy(buf, head, hl);
    memcpy(buf + hl, bytes, len);
    memcpy(buf + hl + len, foot, sizeof foot - 1);

    Doc *d = doc_load_mem(buf, hl + len + sizeof foot - 1, err, sizeof err);

    free(buf);

    if (d == NULL) {
        return -1;
    }

    memset(out, 0, sizeof *out);
    out->kind = kind == SEL_TEXT ? SEL_TEXT : SEL_BLOCK;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        if (out->kind == SEL_TEXT) {
            const Text *t = &d->items[i].blk->cols[0].text;

            for (int m = 0; m < t->n; m++) {
                text_insert(&out->text, out->text.n, tok_dup(&t->v[m]));
            }

            break;
        }

        out->items = realloc(out->items, usz(out->n + 1) * sizeof(Item));
        memset(&out->items[out->n], 0, sizeof(Item));
        out->items[out->n].type = IT_BLOCK;
        out->items[out->n].blk = block_clone_full(d->items[i].blk);
        out->n++;
    }

    doc_free(d);

    return 0;
}


/* ----------------------------------------------------------- the library */
void
library_edit(Editor *e, LibraryEdit edit)
{
    WField f[4];
    WField pick[1];
    char old[32];
    int at;
    Clip clip;

    name_field(&pick[0], "Entry Name", "");

    if (popup_form("Library Entry", pick, 1, NULL) == 0) {
        return;
    }

    str_copy(old, sizeof old, pick[0].val);
    at = lib_find(old);
    memset(&clip, 0, sizeof clip);

    name_field(&f[0], "Entry Name", old);
    text_field(&f[1], "Description", at >= 0 ? entries[at].desc : "",
               PICK_NONE, NULL);
    memset(&f[2], 0, sizeof f[2]);
    f[2].label = "Contents";
    f[2].kind = WF_INFO;
    str_copy(f[2].val, sizeof f[2].val, "...");
    choice_field(&f[3], "Include in Library?", include_choices,
                 at >= 0 ? 1 : 0);

    if (popup_form("Library Entry", f, 4, NULL) == 0) {
        return;
    }

    if (f[0].val[0] == '\0') {
        popup_msg("Library Entry", "Enter the name of the entry");

        return;
    }

    if (at >= 0) {
        clip_copy(&clip, &entries[at].clip);
    } else {
        clip.kind = SEL_BLOCK;
    }

    if (clip.kind != SEL_SECTION &&
        popup_yn("Edit the contents of the entry?") != 0) {
        size_t len;
        uint8_t *in = contents_bytes(&clip, &len);
        uint8_t *out = NULL;
        size_t olen = 0;

        if (edit("Library Contents", in, len, &out, &olen) != 0 &&
            out != NULL) {
            Clip changed;

            if (contents_clip(out, olen, clip.kind, &changed) == 0) {
                clip_free(&clip);
                clip = changed;
            }
        }

        free(in);
        free(out);
    }

    /* A new name makes a copy; the old entry stays. */
    if (f[3].sel == 0) {
        int gone = lib_find(f[0].val);

        if (gone >= 0) {
            lib_remove(gone);
        }

        clip_free(&clip);
    } else {
        lib_put(f[0].val, f[1].val, &clip);
    }

    ed_msg(e, "%d library entr%s", nentries, nentries == 1 ? "y" : "ies");
}


void
library_delete(Editor *e)
{
    WField f[1];

    if (nentries == 0) {
        popup_msg("Delete from Library", "The library has no entries");

        return;
    }

    name_field(&f[0], "Entry Name", "");

    for (;;) {
        if (popup_form("Delete from Library", f, 1, NULL) == 0) {
            return;
        }

        if (lib_find(f[0].val) >= 0) {
            break;
        }

        popup_msg("Delete from Library", "No such library entry");
    }

    char q[96];
    int at = lib_find(f[0].val);

    snprintf(q, sizeof q, "Delete the library entry %.60s?", entries[at].name);

    if (popup_yn(q) != 0) {
        lib_remove(at);
        ed_msg(e, "Deleted");
    }
}


static int
check_name(const WField *f, int n UNUSED, char *err, int errcap)
{
    if (f[1].val[0] == '\0') {
        snprintf(err, usz(errcap), "No file name specified");

        return 0;
    }

    return 1;
}


static void
resolve(const char *dir, const char *name, char *out, size_t cap)
{
    char file[PATH_MAX];

    str_copy(file, sizeof file, name);

    if (strchr(file, '.') == NULL) {
        size_t k = strlen(file);

        str_copy(file + k, sizeof file - k, ".lib");
    }

    if (file[0] == '/') {
        str_copy(out, cap, file);
    } else {
        path_join(out, cap, dir, file);
    }
}


void
library_save(Editor *e)
{
    WField f[5];
    char rev[32];
    struct stat st;

    marker_fmt_num(lib_rev, 3, 0, rev, sizeof rev);
    text_field(&f[0], "Working Directory", filemenu_workdir(e), PICK_DIR,
               NULL);
    text_field(&f[1], "File Name", "", PICK_FILE, ".lib");
    text_field(&f[2], "Description", lib_desc, PICK_NONE, NULL);
    text_field(&f[3], "Author", lib_author, PICK_NONE, NULL);
    text_field(&f[4], "Revision", rev, PICK_NONE, NULL);

    if (popup_form("Save Library", f, 5, check_name) == 0) {
        return;
    }

    char path[PATH_MAX];
    char err[160];
    int typed = 0;

    filemenu_set_workdir(f[0].val);
    resolve(f[0].val, f[1].val, path, sizeof path);

    if (stat(path, &st) == 0 &&
        popup_yn("Overwrite old version on disk?") == 0) {
        return;
    }

    str_copy(lib_desc, sizeof lib_desc, f[2].val);
    str_copy(lib_author, sizeof lib_author, f[3].val);

    if (marker_parse_num(f[4].val, 3, &typed) != 0 || typed <= 0) {
        typed = lib_rev > 0 ? lib_rev + 1 : 1000;
    }

    lib_rev = typed;

    Doc *d = lib_to_doc();

    doc_header_set(d, "MD", lib_desc);
    doc_header_set(d, "MA", lib_author);
    doc_set_revision(d, lib_rev);

    if (doc_save(d, path, err, sizeof err) != 0) {
        popup_msg("Save failed", err);
    } else {
        ed_msg(e, "Saved %s", path);
    }

    doc_free(d);
}


void
library_retrieve(Editor *e)
{
    WField f[3];

    text_field(&f[0], "Working Directory", filemenu_workdir(e), PICK_DIR,
               NULL);
    text_field(&f[1], "File Name", "", PICK_FILE, ".lib");
    choice_field(&f[2], "Options", retrieve_choices, 0);

    if (popup_form("Retrieve Library", f, 3, check_name) == 0) {
        return;
    }

    char path[PATH_MAX];
    char err[160];

    filemenu_set_workdir(f[0].val);
    resolve(f[0].val, f[1].val, path, sizeof path);

    Doc *d = doc_load(path, err, sizeof err);

    if (d == NULL) {
        popup_msg("Retrieve failed", err);

        return;
    }

    int n = lib_from_doc(d, f[2].sel);

    if (n < 0) {
        popup_msg("Retrieve failed", "That file is not a library");
    } else {
        const char *v = doc_header_get(d, "MD");

        str_copy(lib_desc, sizeof lib_desc, v != NULL ? v : "");
        v = doc_header_get(d, "MA");
        str_copy(lib_author, sizeof lib_author, v != NULL ? v : "");
        lib_rev = doc_revision(d);
        ed_msg(e, "Retrieved %d library entr%s", n, n == 1 ? "y" : "ies");
    }

    doc_free(d);
}


int
library_load_initial(const char *dir, const char *name, char *why, size_t cap)
{
    char path[PATH_MAX];
    char err[160];

    initial_file_path(dir, name, ".lib", path, sizeof path);

    Doc *d = doc_load(path, err, sizeof err);

    if (d == NULL) {
        snprintf(why, cap, "cannot read %s: %s", path, err);

        return -1;
    }

    int n = lib_from_doc(d, 0);

    if (n < 0) {
        snprintf(why, cap, "%s is not a library", path);
    } else {
        const char *v = doc_header_get(d, "MD");

        str_copy(lib_desc, sizeof lib_desc, v != NULL ? v : "");
        v = doc_header_get(d, "MA");
        str_copy(lib_author, sizeof lib_author, v != NULL ? v : "");
        lib_rev = doc_revision(d);
    }

    doc_free(d);

    return n;
}


void
library_add_clip(Editor *e)
{
    WField f[3];

    if (e->clip.kind == SEL_NONE) {
        return;
    }

    name_field(&f[0], "Entry name", "");
    text_field(&f[1], "Description", "", PICK_NONE, NULL);
    choice_field(&f[2], "Include in Library?", include_choices, 1);

    for (;;) {
        if (popup_form("Copy to Library", f, 3, NULL) == 0) {
            return;
        }

        if (f[0].val[0] == '\0') {
            popup_msg("Copy to Library", "Enter a name for the entry");

            continue;
        }

        if (lib_find(f[0].val) >= 0 &&
            popup_yn("Replace the entry that has this name?") == 0) {
            continue;
        }

        break;
    }

    if (f[2].sel == 0) {
        ed_msg(e, "Not included in the library");

        return;
    }

    Clip c;

    clip_copy(&c, &e->clip);
    lib_put(f[0].val, f[1].val, &c);
    ed_msg(e, "Library entry %s made", f[0].val);
}


void
library_paste(Editor *e)
{
    WField f[1];
    int at;

    if (nentries == 0) {
        popup_msg("Paste from Library", "The library has no entries");

        return;
    }

    name_field(&f[0], "Entry name", "");

    for (;;) {
        if (popup_form("Paste from Library", f, 1, NULL) == 0) {
            return;
        }

        at = lib_find(f[0].val);

        if (at >= 0) {
            break;
        }

        popup_msg("Paste from Library", "No such library entry");
    }

    Clip saved = e->clip;

    memset(&e->clip, 0, sizeof e->clip);
    clip_copy(&e->clip, &entries[at].clip);
    ed_paste(e);
    clip_free(&e->clip);
    e->clip = saved;
}
