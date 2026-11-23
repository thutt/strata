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
 * include.c - the Include marker: the master document joined with the text
 * of its parts.  See include.h for the rules.
 */
#include "include.h"
#include "log.h"
#include "marker.h"
#include "util.h"
#include "layout.h"
#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define NO_SECTION "0.0"

/* A cross-reference of a document and the section that it points at. */
typedef struct {
    int sitem;
    int col;
    int tok;
    int target; /* The first block of the section, or -1. */
} XPre;

/* One document that items were taken from: where its items went. */
typedef struct {
    int n;
    int *o2f; /* The item in the joined document, or -1. */
    XPre *xp;
    int nxp;
    int capxp;
} Src;

typedef struct {
    int item;
    int col;
    int line;
    int off;
} Pos;

/* An item of the joined document that is being made. */
typedef struct {
    int type;
    uint8_t fold;
    uint8_t se;
    Block *blk;
    Src *src; /* Where it came from, or NULL when it is new. */
    int sitem; /* The item in src. */
    int direct; /* 1: an item of the master itself, at src item sitem. */
    Pos at; /* Not direct: the Include marker in the master. */
} RItem;

typedef struct {
    RItem *v;
    int n;
    int cap;
} List;

/* A cross-reference whose number has to be written again. */
typedef struct {
    Block *blk;
    int col;
    int tok;
    Src *src;
    int target; /* The first block of the section, in src. */
} XRef;

typedef struct {
    char path[PATH_MAX];
    long size;
    time_t mtime;
} FileRec;

struct IncludeMap {
    const Doc *orig;
    int n;
    int *oitem; /* The item in the master, per item of the joined one. */
    Pos *at; /* Per item: the marker, or item -1 when the item is direct. */
    int nfiles;
    FileRec *files;
};

typedef struct {
    const Doc *master;
    DiagList *diags;
    Src **srcs;
    int nsrc;
    int capsrc;
    XRef *xr;
    int nxr;
    int capxr;
    FileRec *files;
    int nfiles;
    int capfiles;
    Pos top; /* The Include marker of the master that is being done. */
    char stack[INCLUDE_DEPTH_MAX + 2][PATH_MAX];
} Run;


static void *
grow(void *p, int *cap, int n, size_t size)
{
    if (n < *cap) {
        return p;
    }

    *cap = *cap != 0 ? *cap * 2 : 64;
    p = realloc(p, usz(*cap) * size);

    if (p == NULL) {
        log_fatal(2, "include: out of memory growing a list to %d "
                  "entries", *cap);
    }

    return p;
}


static RItem *
list_add(List *l)
{
    l->v = grow(l->v, &l->cap, l->n, sizeof *l->v);
    memset(&l->v[l->n], 0, sizeof l->v[l->n]);

    return &l->v[l->n++];
}


/* Free the blocks that a list still owns, and the list. */
static void
list_free(List *l)
{
    for (int i = 0; i < l->n; i++) {
        block_free(l->v[i].blk);
    }

    free(l->v);
    memset(l, 0, sizeof *l);
}


static Block *
clone_block(const Block *b)
{
    Block *nb = block_new(0);

    for (int i = 0; i < b->props.n; i++) {
        lines_add(&nb->props, b->props.v[i].s, usz(b->props.v[i].n));
    }

    for (int c = 0; c < b->ncols; c++) {
        const Column *oc = &b->cols[c];
        Column *nc = block_add_column(nb);

        for (int i = 0; i < oc->props.n; i++) {
            lines_add(&nc->props, oc->props.v[i].s,
                      usz(oc->props.v[i].n));
        }

        nc->width = oc->width;

        for (int j = 0; j < oc->text.n; j++) {
            const Tok *k = &oc->text.v[j];
            Tok nt;

            if (k->kind == TK_MARKER) {
                nt = tok_marker(tok_mtype(k), tok_mdata(k), tok_mlen(k));
            } else {
                nt = *k;
            }

            text_insert(&nc->text, nc->text.n, nt);
        }
    }

    return nb;
}


/* A block of plain text that tells what is wrong. */
static Block *
notice_block(const char *msg)
{
    Block *b = block_new(1);
    Tok k;

    memset(&k, 0, sizeof k);
    k.kind = TK_CHAR;

    for (const char *p = msg; *p != '\0'; p++) {
        unsigned char ch = (unsigned char)*p;

        k.a = ch >= 32 && ch < 127 ? ch : '?';
        text_insert(&b->cols[0].text, b->cols[0].text.n, k);
    }

    memset(&k, 0, sizeof k);
    k.kind = TK_NL;
    text_insert(&b->cols[0].text, b->cols[0].text.n, k);

    return b;
}


/* The Include markers of a block: how many, and where the first is. */
static int
find_marker(const Block *b, int *col, int *tok, int *line, int *off)
{
    int found = 0;

    for (int c = 0; c < b->ncols; c++) {
        const Text *t = &b->cols[c].text;
        int ln = 1;
        int lstart = 0;

        for (int j = 0; j < t->n; j++) {
            const Tok *k = &t->v[j];

            if (k->kind == TK_MARKER && tok_mtype(k) == INCLUDE_MTYPE) {
                if (found == 0) {
                    *col = c;
                    *tok = j;
                    *line = ln;
                    *off = j - lstart;
                }

                found++;
            }

            if (k->kind == TK_NL) {
                ln++;
                lstart = j + 1;
            }
        }
    }

    return found;
}


/* Nothing in the block but the marker, spaces, attributes and line ends. */
static int
marker_alone(const Block *b)
{
    if (b->ncols != 1) {
        return 0;
    }

    for (int j = 0; j < b->cols[0].text.n; j++) {
        const Tok *k = &b->cols[0].text.v[j];

        if (k->kind == TK_MARKER) {
            continue;
        }

        if (k->kind == TK_NL || k->kind == TK_ATTR || k->kind == TK_TAB ||
            (k->kind == TK_CHAR && k->a == ' ')) {
            continue;
        }

        return 0;
    }

    return 1;
}


/* ---------------------------------------------------------- the files */
static int
join_path(char *out, size_t cap, const char *dir, const char *name)
{
    size_t dl = dir != NULL ? strlen(dir) : 0;
    size_t nl = strlen(name);
    int slash = dl > 0 && dir[dl - 1] != '/';

    if (dl + usz(slash) + nl + 1 > cap) {
        return -1;
    }

    if (dl > 0) {
        memcpy(out, dir, dl);
    }

    if (slash != 0) {
        out[dl] = '/';
    }

    memcpy(out + dl + usz(slash), name, nl + 1);

    return 0;
}


static int
is_file(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISREG(st.st_mode) ? 1 : 0;
}


/*
 * Find a file whose name is path, in the case of the letters that is on the
 * disk.  Returns 1 and the real path in out, or 0.
 */
static int
find_case(const char *path, char *out, size_t cap)
{
    char dir[PATH_MAX];
    const char *base = path;
    char *slash;
    DIR *d;
    struct dirent *e;
    int found = 0;

    if (is_file(path) != 0) {
        str_copy(out, cap, path);

        return 1;
    }

    str_copy(dir, sizeof dir, path);
    slash = strrchr(dir, '/');

    if (slash == NULL) {
        str_copy(dir, sizeof dir, ".");
    } else {
        base = strrchr(path, '/') + 1;
        *slash = '\0';

        if (dir[0] == '\0') {
            str_copy(dir, sizeof dir, "/");
        }
    }

    d = opendir(dir);

    if (d == NULL) {
        return 0;
    }

    for (e = readdir(d); e != NULL && found == 0; e = readdir(d)) {
        char cand[PATH_MAX];

        if (strcasecmp(e->d_name, base) != 0) {
            continue;
        }

        if (join_path(cand, sizeof cand, slash == NULL ? NULL : dir,
                      e->d_name) != 0) {
            continue;
        }

        if (is_file(cand) != 0) {
            str_copy(out, cap, cand);
            found = 1;
        }
    }

    closedir(d);

    return found;
}


/* Try the name as it is, then with the extension .doc added. */
static int
find_name_as(const char *dir, const char *name, char *out, size_t cap)
{
    char cand[PATH_MAX];

    if (name[0] == '/') {
        str_copy(cand, sizeof cand, name);
    } else if (join_path(cand, sizeof cand, dir, name) != 0) {
        return 0;
    }

    if (find_case(cand, out, cap) != 0) {
        return 1;
    }

    if (strlen(cand) + 5 <= sizeof cand) {
        strcat(cand, ".doc");

        if (find_case(cand, out, cap) != 0) {
            return 1;
        }
    }

    return 0;
}


/*
 * Try the name as it is written in the document and, when it has letters
 * that are not ASCII, as the UTF-8 name that a file system has: the name of
 * a document is in the code page of the document format.
 */
static int
find_name(const char *dir, const char *name, char *out, size_t cap)
{
    char utf8[PATH_MAX];

    if (find_name_as(dir, name, out, cap) != 0) {
        return 1;
    }

    name_from_cells(name, utf8, sizeof utf8);

    return strcmp(utf8, name) != 0 && find_name_as(dir, utf8, out, cap) != 0;
}


/*
 * Look for the part: in the directory of the document that holds the
 * marker, then in the working directory.  An absolute name is taken as it
 * is.  Returns 1 and the path in out, or 0.
 */
static int
find_part(const char *name, const char *dir, char *out, size_t cap)
{
    if (name[0] == '/') {
        return find_name(NULL, name, out, cap);
    }

    if (dir != NULL && dir[0] != '\0' &&
        find_name(dir, name, out, cap) != 0) {
        return 1;
    }

    return find_name(NULL, name, out, cap);
}


/* ------------------------------------------------------------ errors */
static void
report(Run *r, const char *code, const char *name, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
static void
report(Run *r, const char *code, const char *name, const char *fmt, ...)
{
    Diag g;
    char why[DIAG_MSG - 40];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    memset(&g, 0, sizeof g);
    g.kind = DIAG_ERROR;
    str_copy(g.code, sizeof g.code, code);
    snprintf(g.msg, sizeof g.msg, "Invalid Include request: %s", why);
    str_copy(g.text, sizeof g.text, name);
    g.item = r->top.item;
    g.col = r->top.col;
    g.line = r->top.line;
    g.off = r->top.off;
    g.mtype = INCLUDE_MTYPE;
    diag_locate(r->master, &g);

    if (r->diags != NULL) {
        diag_push(r->diags, &g);
    }
}


/* Put a notice of what went wrong in the place of the marker. */
static void
notice(Run *r, List *out, const char *why)
{
    char msg[160];
    RItem *it = list_add(out);

    snprintf(msg, sizeof msg, "[Invalid Include request: %.120s]", why);
    it->type = IT_BLOCK;
    it->blk = notice_block(msg);
    it->at = r->top;
}


static void
fail(Run *r, List *out, const char *code, const char *name, const char *why)
{
    report(r, code, name, "%s", why);
    notice(r, out, why);
}


/* --------------------------------------------------------- the walk */
static Src *
src_new(Run *r, int n)
{
    Src *s = calloc(1, sizeof *s);

    if (s == NULL) {
        log_fatal(2, "include: out of memory for a record of %d items", n);
    }

    s->n = n;
    s->o2f = malloc((usz(n) + 1) * sizeof *s->o2f);

    if (s->o2f == NULL) {
        log_fatal(2, "include: out of memory for a map of %d items", n);
    }

    for (int i = 0; i < n; i++) {
        s->o2f[i] = -1;
    }

    r->srcs = grow(r->srcs, &r->capsrc, r->nsrc, sizeof *r->srcs);
    r->srcs[r->nsrc++] = s;

    return s;
}


/*
 * Note the cross-references of a prepared document, with the sections they
 * point at.  This must be done before another document is prepared.
 */
static void
collect_xrefs(Src *s, const Doc *d)
{
    for (int i = 0; i < d->nitems; i++) {
        const Block *b = d->items[i].blk;

        if (b == NULL) {
            continue;
        }

        for (int c = 0; c < b->ncols; c++) {
            const Text *t = &b->cols[c].text;

            for (int j = 0; j < t->n; j++) {
                const Tok *k = &t->v[j];
                XPre *x;

                if (k->kind != TK_MARKER || tok_mtype(k) != 2) {
                    continue;
                }

                s->xp = grow(s->xp, &s->capxp, s->nxp, sizeof *s->xp);
                x = &s->xp[s->nxp++];
                x->sitem = i;
                x->col = c;
                x->tok = j;
                x->target = marker_xref_item(k);
            }
        }
    }
}


/* Copy item i of a document to the end of the list. */
static void
push_clone(Run *r, Src *s, const Doc *d, int depth, int i, int *xi,
           List *out)
{
    const Item *it = &d->items[i];
    RItem *ri = list_add(out);

    ri->type = it->type;
    ri->fold = it->fold;
    ri->se = it->se;
    ri->src = s;
    ri->sitem = i;

    if (depth == 0) {
        ri->direct = 1;
    } else {
        ri->at = r->top;
    }

    if (it->blk != NULL) {
        ri->blk = clone_block(it->blk);
    }

    while (*xi < s->nxp && s->xp[*xi].sitem <= i) {
        if (s->xp[*xi].sitem == i && ri->blk != NULL) {
            XRef *x;

            r->xr = grow(r->xr, &r->capxr, r->nxr, sizeof *r->xr);
            x = &r->xr[r->nxr++];
            x->blk = ri->blk;
            x->col = s->xp[*xi].col;
            x->tok = s->xp[*xi].tok;
            x->src = s;
            x->target = s->xp[*xi].target;
        }

        (*xi)++;
    }
}


/* Add an item that no document has: the end of a group of sections. */
static void
push_new(Run *r, List *out, int type)
{
    RItem *ri = list_add(out);

    ri->type = type;
    ri->at = r->top;
}


static void
expand_doc(Run *r, Src *s, const Doc *d, const char *dir, int depth,
           List *out);


/* Move the items of a list to the end of another, from index a to b-1. */
static void
move_items(List *out, List *sub, int a, int b, int as_block)
{
    for (int i = a; i < b; i++) {
        RItem *ri;

        if (as_block != 0 && sub->v[i].blk == NULL) {
            continue;
        }

        ri = list_add(out);
        *ri = sub->v[i];
        sub->v[i].blk = NULL;

        if (as_block != 0) {
            ri->type = IT_BLOCK;
            ri->fold = 0;
            ri->se = 0;
        }
    }
}


/*
 * Does the list of a part start with the root section (and the blocks of
 * its title page) and its group, and end with the end of that group?  Then
 * the sections are in between.  Returns the index of the start of the
 * group, or -1.
 */
static int
has_sections(const Doc *part, const List *sub)
{
    int p = 1;

    if (part->structured == 0 || sub->n < 3 || sub->v[0].type != IT_SECT ||
        sub->v[sub->n - 1].type != IT_MINUS) {
        return -1;
    }

    while (p < sub->n && sub->v[p].type == IT_BLOCK) {
        p++;
    }

    return p < sub->n - 1 && sub->v[p].type == IT_PLUS ? p : -1;
}


/*
 * Do the Include marker in item i of d.  Returns how many items after item
 * i were done as well (0 or 1).
 */
static int
do_include(Run *r, Src *s, const Doc *d, const char *dir, int depth, int i,
           int *xi, List *out)
{
    const Block *b = d->items[i].blk;
    int col = 0;
    int tok = 0;
    int line = 0;
    int off = 0;
    int n = find_marker(b, &col, &tok, &line, &off);
    MPayload p;
    const Tok *k = &b->cols[col].text.v[tok];
    const char *raw;
    char name[PATH_MAX];
    char path[PATH_MAX];
    char real[PATH_MAX];
    char pdir[PATH_MAX];
    char *slash;
    char err[160];
    char why[DIAG_MSG - 60];
    struct stat st;
    Doc *part;
    Src *ps;
    List sub;
    int skip = 0;
    int first;

    if (depth == 0) {
        r->top.item = i;
        r->top.col = b->ncols > 1 ? col + 1 : 0;
        r->top.line = line;
        r->top.off = off;
    }

    mp_parse(&p, marker_info(INCLUDE_MTYPE), tok_mdata(k), tok_mlen(k));
    raw = mp_get(&p, "1");
    str_copy(name, sizeof name, raw != NULL ? raw : "");
    mp_free(&p);

    for (char *c = name; *c != '\0'; c++) {
        if (*c == '\\') {
            *c = '/';
        }
    }

    for (int l = slen(name); l > 0 && name[l - 1] == ' '; l--) {
        name[l - 1] = '\0';
    }

    if (n > 1 || marker_alone(b) == 0) {
        report(r, "include-place", name, "%s",
               "The Include marker must be alone in its block.");
        push_clone(r, s, d, depth, i, xi, out);

        return 0;
    }

    if (d->structured != 0 && i > 0 && d->items[i - 1].type == IT_SECT) {
        fail(r, out, "include-place", name,
             "The Include marker cannot be in a headline.");

        return 0;
    }

    if (name[0] == '\0') {
        fail(r, out, "include-noname", name,
             "The Include marker has no file name.");

        return 0;
    }

    if (depth + 1 > INCLUDE_DEPTH_MAX) {
        snprintf(why, sizeof why, "The parts are nested more than %d "
                 "levels deep.", INCLUDE_DEPTH_MAX);
        fail(r, out, "include-deep", name, why);

        return 0;
    }

    if (find_part(name, dir, path, sizeof path) == 0) {
        snprintf(why, sizeof why, "The file \"%.80s\" cannot be found.",
                 name);
        fail(r, out, "include-missing", name, why);

        return 0;
    }

    if (access(path, R_OK) != 0 || stat(path, &st) != 0) {
        snprintf(why, sizeof why, "The file \"%.80s\" cannot be read.",
                 name);
        fail(r, out, "include-unreadable", name, why);

        return 0;
    }

    if (realpath(path, real) == NULL) {
        str_copy(real, sizeof real, path);
    }

    for (int l = 0; l <= depth; l++) {
        if (strcmp(r->stack[l], real) == 0) {
            snprintf(why, sizeof why, "The file \"%.80s\" includes itself, "
                     "directly or through other parts.", name);
            fail(r, out, "include-loop", name, why);

            return 0;
        }
    }

    part = doc_load(path, err, sizeof err);

    if (part == NULL) {
        snprintf(why, sizeof why, "The file \"%.80s\" is not a Strata "
                 "document or is damaged.", name);
        fail(r, out, "include-damaged", name, why);

        return 0;
    }

    r->files = grow(r->files, &r->capfiles, r->nfiles, sizeof *r->files);
    str_copy(r->files[r->nfiles].path, sizeof r->files[0].path, real);
    r->files[r->nfiles].size = st.st_size;
    r->files[r->nfiles].mtime = st.st_mtime;
    r->nfiles++;
    str_copy(r->stack[depth + 1], sizeof r->stack[0], real);
    marker_set_doc(part);
    marker_prepare(part);
    ps = src_new(r, part->nitems);
    memset(&sub, 0, sizeof sub);
    str_copy(pdir, sizeof pdir, real);
    slash = strrchr(pdir, '/');

    if (slash == NULL) {
        str_copy(pdir, sizeof pdir, ".");
    } else if (slash == pdir) {
        str_copy(pdir, sizeof pdir, "/");
    } else {
        *slash = '\0';
    }

    expand_doc(r, ps, part, pdir, depth + 1, &sub);
    first = d->structured != 0 ? has_sections(part, &sub) : -1;
    doc_free(part);

    if (first >= 0 && i + 1 < d->nitems &&
        d->items[i + 1].type == IT_BLOCK) {
        list_free(&sub);
        fail(r, out, "include-place", name,
             "A part with sections must be in the last block of its "
             "section.");

        return 0;
    }

    if (first < 0) {
        move_items(out, &sub, 0, sub.n, 1);
    } else {
        move_items(out, &sub, 1, first, 0);

        if (i + 1 < d->nitems && d->items[i + 1].type == IT_PLUS) {
            push_clone(r, s, d, depth, i + 1, xi, out);
            move_items(out, &sub, first + 1, sub.n - 1, 0);
            skip = 1;
        } else {
            push_new(r, out, IT_PLUS);
            move_items(out, &sub, first + 1, sub.n - 1, 0);
            push_new(r, out, IT_MINUS);
        }
    }

    list_free(&sub);

    return skip;
}


static void
expand_doc(Run *r, Src *s, const Doc *d, const char *dir, int depth,
           List *out)
{
    int xi = 0;

    collect_xrefs(s, d);

    for (int i = 0; i < d->nitems; i++) {
        const Block *b = d->items[i].blk;
        int col;
        int tok;
        int line;
        int off;

        if (d->items[i].type == IT_BLOCK && b != NULL &&
            find_marker(b, &col, &tok, &line, &off) > 0) {
            i += do_include(r, s, d, dir, depth, i, &xi, out);
        } else {
            push_clone(r, s, d, depth, i, &xi, out);
        }
    }
}


/* ---------------------------------------------------------- the result */
static const char *
number_of(const MarkerSecNum *sn, int nsn, int item)
{
    for (int i = 0; i < nsn; i++) {
        if (sn[i].item == item) {
            return sn[i].num;
        }
    }

    return NULL;
}


static void
copy_header(Doc *out, const Doc *src)
{
    for (int i = 0; i < src->header.n; i++) {
        lines_add(&out->header, src->header.v[i].s,
                  usz(src->header.v[i].n));
    }

    out->tail_len = src->tail_len;
    out->tail = malloc(src->tail_len + 1);

    if (out->tail == NULL) {
        log_fatal(2, "include: out of memory for a tail of %zu bytes",
                  src->tail_len);
    }

    if (src->tail_len > 0) {
        memcpy(out->tail, src->tail, src->tail_len);
    }
}


static int
has_include(const Doc *d)
{
    for (int i = 0; i < d->nitems; i++) {
        const Block *b = d->items[i].blk;
        int col;
        int tok;
        int line;
        int off;

        if (b != NULL && find_marker(b, &col, &tok, &line, &off) > 0) {
            return 1;
        }
    }

    return 0;
}


Doc *
include_expand(const Doc *master, const char *master_dir, DiagList *diags,
               IncludeMap **map)
{
    Run *r;
    List all;
    Doc *out;
    IncludeMap *m;
    MarkerSecNum *sn;
    int nsn;

    *map = NULL;

    if (has_include(master) == 0) {
        return NULL;
    }

    r = calloc(1, sizeof *r);

    if (r == NULL) {
        log_fatal(2, "include: out of memory for the run of %zu bytes",
                  sizeof *r);
    }

    r->master = master;
    r->diags = diags;
    r->top.item = -1;

    if (master->path != NULL && realpath(master->path, r->stack[0]) == NULL) {
        str_copy(r->stack[0], sizeof r->stack[0], master->path);
    }

    memset(&all, 0, sizeof all);
    expand_doc(r, src_new(r, master->nitems), master, master_dir, 0, &all);

    out = calloc(1, sizeof *out);

    if (out == NULL) {
        log_fatal(2, "include: out of memory for a document of %d items",
                  all.n);
    }

    out->structured = master->structured;
    copy_header(out, master);

    if (master->path != NULL) {
        out->path = strdup(master->path);
    }

    m = calloc(1, sizeof *m);

    if (m == NULL) {
        log_fatal(2, "include: out of memory for the record of %d items",
                  all.n);
    }

    m->orig = master;
    m->n = all.n;
    m->oitem = malloc((usz(all.n) + 1) * sizeof *m->oitem);
    m->at = malloc((usz(all.n) + 1) * sizeof *m->at);

    if (m->oitem == NULL || m->at == NULL) {
        log_fatal(2, "include: out of memory for the map of %d items",
                  all.n);
    }

    for (int i = 0; i < all.n; i++) {
        RItem *ri = &all.v[i];
        int at = doc_insert_item(out, out->nitems, ri->type, ri->blk);

        out->items[at].fold = ri->fold;
        out->items[at].se = ri->se;
        ri->blk = NULL;

        if (ri->src != NULL && ri->sitem >= 0) {
            ri->src->o2f[ri->sitem] = at;
        }

        if (ri->direct != 0) {
            m->oitem[at] = ri->sitem;
            m->at[at].item = -1;
        } else {
            m->oitem[at] = ri->at.item;
            m->at[at] = ri->at;
        }
    }

    out->modified = 0;

    /* The cross-references point at the same sections as before. */
    nsn = marker_section_numbers(out, &sn);

    for (int x = 0; x < r->nxr; x++) {
        const XRef *xr = &r->xr[x];
        const char *num = NO_SECTION;
        int fi;
        Tok *t;
        MPayload p;
        size_t len;
        uint8_t *nd;

        if (xr->target < 0) {
            continue;
        }

        fi = xr->src->o2f[xr->target];

        if (fi >= 0 && number_of(sn, nsn, fi) != NULL) {
            num = number_of(sn, nsn, fi);
        }

        t = &xr->blk->cols[xr->col].text.v[xr->tok];
        mp_parse(&p, marker_info(2), tok_mdata(t), tok_mlen(t));
        mp_set(&p, "XN", num, 1);
        nd = mp_build(&p, &len);
        tok_marker_set(t, nd, len);
        free(nd);
        mp_free(&p);
    }

    free(sn);
    m->nfiles = r->nfiles;
    m->files = r->files;
    r->files = NULL;

    for (int i = 0; i < r->nsrc; i++) {
        free(r->srcs[i]->o2f);
        free(r->srcs[i]->xp);
        free(r->srcs[i]);
    }

    free(r->srcs);
    free(r->xr);
    free(r);
    free(all.v);
    *map = m;

    return out;
}


void
include_orig_pos(const IncludeMap *m, int item, int col, int line, int off,
                 int *oitem, int *ocol, int *oline, int *ooff)
{
    *oitem = item;
    *ocol = col;
    *oline = line;
    *ooff = off;

    if (item < 0 || item >= m->n) {
        return;
    }

    *oitem = m->oitem[item];

    if (m->at[item].item >= 0) {
        *ocol = m->at[item].col;
        *oline = m->at[item].line;
        *ooff = m->at[item].off;
    }
}


void
include_remap_diags(const IncludeMap *m, DiagList *l, int from)
{
    for (int i = from; i < l->n; i++) {
        Diag *g = &l->v[i];

        include_orig_pos(m, g->item, g->col, g->line, g->off, &g->item,
                         &g->col, &g->line, &g->off);
        diag_locate(m->orig, g);
    }
}


const Doc *
include_orig(const IncludeMap *m)
{
    return m->orig;
}


int
include_files(const IncludeMap *m)
{
    return m->nfiles;
}


void
include_file(const IncludeMap *m, int i, const char **path, long *size,
             time_t *mtime)
{
    *path = m->files[i].path;
    *size = m->files[i].size;
    *mtime = m->files[i].mtime;
}


void
include_map_free(IncludeMap *m)
{
    if (m == NULL) {
        return;
    }

    free(m->oitem);
    free(m->at);
    free(m->files);
    free(m);
}
