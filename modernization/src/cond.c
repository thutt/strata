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

/* cond.c - conditional text; see cond.h for the rules. */
#include "cond.h"
#include "log.h"
#include "marker.h"
#include "util.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MT_IF 33
#define MT_ELSE 34
#define MT_ENDIF 35
#define MT_XREF 2

/* The most levels that are tracked; the limit of the owner is lower. */
#define LEVELS 64

/* The text of the number that no section has. */
#define NO_SECTION "0.0"


/* ------------------------------------------------------------- expression */
static void say(char *code, size_t codelen, char *msg, size_t msglen,
                const char *c, const char *fmt, ...)
    __attribute__((format(printf, 6, 7)));
static void
say(char *code, size_t codelen, char *msg, size_t msglen, const char *c,
    const char *fmt, ...)
{
    va_list ap;

    str_copy(code, codelen, c);
    va_start(ap, fmt);
    vsnprintf(msg, msglen, fmt, ap);
    va_end(ap);
}


int
cond_parse(const char *text, CondExpr *e, char *code, size_t codelen,
           char *msg, size_t msglen)
{
    const char *p = text;
    size_t n = 0;
    char err[160];

    memset(e, 0, sizeof *e);

    while (*p == ' ') {
        p++;
    }

    if (*p == '\0') {
        say(code, codelen, msg, msglen, "if-empty",
            "This If has no expression: it needs a variable, an operator "
            "and a value, such as COLOR = red.");

        return -1;
    }

    if (!is_alpha(*p) && *p != '.') {
        say(code, codelen, msg, msglen, "if-no-variable",
            "This If does not begin with the name of a variable: a name "
            "starts with a letter.");

        return -1;
    }

    while (is_alpha(p[n]) || is_digit(p[n]) || p[n] == '-' ||
           p[n] == '_' || (n == 0 && p[n] == '.')) {
        n++;
    }

    if (n > VAR_LEN_MAX) {
        say(code, codelen, msg, msglen, "if-too-long",
            "The variable name of this If is longer than 255 characters.");

        return -1;
    }

    memcpy(e->name, p, n);
    e->name[n] = '\0';
    p += n;

    while (*p == ' ') {
        p++;
    }

    if (p[0] == '<' && p[1] == '=') {
        e->op = COND_LE;
        p += 2;
    } else if (p[0] == '>' && p[1] == '=') {
        e->op = COND_GE;
        p += 2;
    } else if (p[0] == '<' && p[1] == '>') {
        say(code, codelen, msg, msglen, "if-bad-operator",
            "The operator <> is not known: not equal is written #.");

        return -1;
    } else if (p[0] == '<') {
        e->op = COND_LT;
        p++;
    } else if (p[0] == '>') {
        e->op = COND_GT;
        p++;
    } else if (p[0] == '=' && p[1] == '=') {
        say(code, codelen, msg, msglen, "if-bad-operator",
            "The operator == is not known: equal is written =.");

        return -1;
    } else if (p[0] == '=') {
        e->op = COND_EQ;
        p++;
    } else if (p[0] == '#') {
        e->op = COND_NE;
        p++;
    } else {
        say(code, codelen, msg, msglen, "if-bad-operator",
            "The operator of this If is missing or not known: the "
            "operators are = # < > <= >=, and # means not equal.");

        return -1;
    }

    while (*p == ' ') {
        p++;
    }

    n = strlen(p);

    while (n > 0 && p[n - 1] == ' ') {
        n--;
    }

    if (n == 0) {
        say(code, codelen, msg, msglen, "if-no-literal",
            "This If has nothing after the operator to compare the "
            "variable with.");

        return -1;
    }

    if (n > VAR_LEN_MAX) {
        say(code, codelen, msg, msglen, "if-too-long",
            "The value of this If is longer than 255 characters.");

        return -1;
    }

    memcpy(e->lit, p, n);
    e->lit[n] = '\0';

    if (var_check_value(e->lit, err, sizeof err) != 0) {
        say(code, codelen, msg, msglen, "if-bad-literal",
            "The value of this If is not allowed: %s", err);

        return -1;
    }

    return 0;
}


/* Is s an integer: all digits, and at most 4294967295?  The value is v. */
static int
as_uint32(const char *s, unsigned long long *v)
{
    size_t n = strlen(s);
    size_t i = 0;

    if (n == 0) {
        return 0;
    }

    for (size_t k = 0; k < n; k++) {
        if (!is_digit(s[k])) {
            return 0;
        }
    }

    while (i + 1 < n && s[i] == '0') {
        i++;
    }

    if (n - i > 10) {
        return 0;
    }

    *v = strtoull(s + i, NULL, 10);

    return *v <= 4294967295ULL;
}


int
cond_compare(const char *left, int op, const char *right)
{
    unsigned long long a;
    unsigned long long b;
    int r;

    if (as_uint32(left, &a) != 0 && as_uint32(right, &b) != 0) {
        r = a < b ? -1 : (a > b ? 1 : 0);
    } else {
        r = strcmp(left, right);
    }

    switch (op) {
        case COND_EQ:
            return r == 0;

        case COND_NE:
            return r != 0;

        case COND_LT:
            return r < 0;

        case COND_GT:
            return r > 0;

        case COND_LE:
            return r <= 0;

        case COND_GE:
            return r >= 0;

        default:
            break;
    }

    return 0;
}


int
cond_eval(const VarList *vars, const CondExpr *e)
{
    return cond_compare(vars_value(vars, e->name), e->op, e->lit);
}


/* ------------------------------------------------------ the filtered copy */
typedef struct {
    int *map; /* the source token index of each token of the copy */
    int n;
} TokMap;

typedef struct {
    TokMap *cols; /* NULL: the copy has the tokens of the source */
    int ncols;
} ItemMap;

struct CondMap {
    const Doc *orig;
    int n; /* the items of the copy */
    int *orig_item; /* the source item of each item of the copy */
    ItemMap *im;
};

/* An item that is kept, before the structure is made right. */
typedef struct {
    int type;
    Block *blk;
    int orig;
    int pid; /* the number of the piece in the first list, or -1 */
    int depth; /* IT_SECT: the sections it is inside of in the source */
    ItemMap im;
    uint8_t fold;
    uint8_t se;
} Piece;

/* A cross-reference that was copied; its XN is made again at the end. */
typedef struct {
    const Tok *orig; /* the marker in the source */
    int piece; /* the piece that holds the copy */
    int col;
    int tok;
} XRef;

typedef struct {
    int parent_active;
    int cond;
    int in_else;
    Diag open; /* where the If is; told if the Endif never comes */
} Level;

typedef struct {
    const Doc *src;
    const VarList *vars;
    DiagList *diags;
    Level lv[LEVELS];
    int depth;
    int active;
    Piece *pc;
    int npc;
    int cappc;
    XRef *xr;
    int nxr;
    int capxr;
} Run;


static void
recompute_active(Run *r)
{
    if (r->depth == 0) {
        r->active = 1;
    } else {
        const Level *l = &r->lv[r->depth - 1];

        r->active = l->parent_active != 0 &&
                    (l->in_else != 0 ? l->cond == 0 : l->cond != 0);
    }
}


/* Make a diagnostic for a marker at a place of the source. */
static void
make_diag(Run *r, Diag *g, int kind, const char *code, const char *text,
          const char *msg, int item, int col, int line, int off, int mtype)
{
    memset(g, 0, sizeof *g);
    g->kind = kind;
    str_copy(g->code, sizeof g->code, code);
    str_copy(g->msg, sizeof g->msg, msg);

    if (text != NULL) {
        str_copy(g->text, sizeof g->text, text);
    }

    g->item = item;
    g->col = col;
    g->line = line;
    g->off = off;
    g->mtype = mtype;
    diag_locate(r->src, g);
}


static void
tell(Run *r, const Diag *g)
{
    if (r->diags != NULL) {
        diag_push(r->diags, g);
    }
}


static Piece *
add_piece(Run *r)
{
    if (r->npc == r->cappc) {
        Piece *nv;

        r->cappc = r->cappc != 0 ? r->cappc * 2 : 256;
        nv = realloc(r->pc, usz(r->cappc) * sizeof *nv);

        if (nv == NULL) {
            log_fatal(2, "cond: out of memory growing the list of items to "
                      "%d entries", r->cappc);
        }

        r->pc = nv;
    }

    memset(&r->pc[r->npc], 0, sizeof r->pc[r->npc]);
    r->pc[r->npc].pid = r->npc;

    return &r->pc[r->npc++];
}


static void
add_xref(Run *r, const Tok *orig, int piece, int col, int tok)
{
    if (r->nxr == r->capxr) {
        XRef *nv;

        r->capxr = r->capxr != 0 ? r->capxr * 2 : 256;
        nv = realloc(r->xr, usz(r->capxr) * sizeof *nv);

        if (nv == NULL) {
            log_fatal(2, "cond: out of memory growing the list of "
                      "cross-references to %d entries", r->capxr);
        }

        r->xr = nv;
    }

    r->xr[r->nxr].orig = orig;
    r->xr[r->nxr].piece = piece;
    r->xr[r->nxr].col = col;
    r->xr[r->nxr].tok = tok;
    r->nxr++;
}


/* Does the document have an If, Else or Endif marker at all? */
static int
has_conditionals(const Doc *d)
{
    for (int i = 0; i < d->nitems; i++) {
        const Block *b;

        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        b = d->items[i].blk;

        for (int c = 0; c < b->ncols; c++) {
            const Text *t = &b->cols[c].text;

            for (int j = 0; j < t->n; j++) {
                if (t->v[j].kind == TK_MARKER) {
                    int mt = tok_mtype(&t->v[j]);

                    if (mt == MT_IF || mt == MT_ELSE || mt == MT_ENDIF) {
                        return 1;
                    }
                }
            }
        }
    }

    return 0;
}


/* The marker of the If: its expression text (empty when it has none). */
static void
expr_text(const Tok *k, char *out, size_t cap)
{
    MPayload p;
    const char *v;

    mp_parse(&p, marker_info(MT_IF), tok_mdata(k), tok_mlen(k));
    v = mp_get(&p, "1");
    str_copy(out, cap, v != NULL ? v : "");
    mp_free(&p);
}


/* Handle an If, Else or Endif marker at a place of the source. */
static void
handle_marker(Run *r, const Tok *k, int item, int col, int line, int off)
{
    int mt = tok_mtype(k);
    Diag g;

    if (mt == MT_IF) {
        char text[1024];
        char code[DIAG_CODE];
        char msg[DIAG_MSG];
        CondExpr e;
        int result = 0;
        int bad;

        expr_text(k, text, sizeof text);
        bad = cond_parse(text, &e, code, sizeof code, msg, sizeof msg);

        if (bad != 0) {
            make_diag(r, &g, DIAG_ERROR, code, text, msg, item, col, line,
                      off, mt);
            tell(r, &g);
        } else {
            result = cond_eval(r->vars, &e);
        }

        if (r->depth >= LEVELS) {
            make_diag(r, &g, DIAG_ERROR, "if-too-deep", text,
                      "These Ifs are nested too deep: the limit is ten.",
                      item, col, line, off, mt);
            tell(r, &g);

            return;
        }

        if (r->depth >= COND_DEPTH_MAX) {
            make_diag(r, &g, DIAG_ERROR, "if-too-deep", text,
                      "These Ifs are nested too deep: the limit is ten.",
                      item, col, line, off, mt);
            tell(r, &g);
        }

        r->lv[r->depth].parent_active = r->active;
        r->lv[r->depth].cond = result;
        r->lv[r->depth].in_else = 0;
        make_diag(r, &r->lv[r->depth].open, DIAG_ERROR, "if-unclosed", text,
                  "This If has no Endif after it.", item, col, line, off,
                  mt);
        r->depth++;
    } else if (mt == MT_ELSE) {
        if (r->depth == 0) {
            make_diag(r, &g, DIAG_ERROR, "else-unmatched", NULL,
                      "This Else has no If before it.", item, col, line, off,
                      mt);
            tell(r, &g);
        } else if (r->lv[r->depth - 1].in_else != 0) {
            make_diag(r, &g, DIAG_ERROR, "else-doubled", NULL,
                      "This If already has an Else.", item, col, line, off,
                      mt);
            tell(r, &g);
        } else {
            r->lv[r->depth - 1].in_else = 1;
        }
    } else {
        if (r->depth == 0) {
            make_diag(r, &g, DIAG_ERROR, "endif-unmatched", NULL,
                      "This Endif has no If before it.", item, col, line,
                      off, mt);
            tell(r, &g);
        } else {
            r->depth--;
        }
    }

    recompute_active(r);
}


/* Is every token of the text an end of line (or are there none)? */
static int
only_ends(const Text *t)
{
    for (int i = 0; i < t->n; i++) {
        if (t->v[i].kind != TK_NL) {
            return 0;
        }
    }

    return 1;
}


/*
 * Copy one block with only the tokens of the branches that are chosen.
 * Returns the new block, or NULL when nothing of it is left but ends of
 * line after a marker or a branch was taken out of it (the block is then
 * left out as well).  *im gets the map of the tokens when they changed.
 */
static Block *
copy_block(Run *r, const Block *b, int item, int headline, ItemMap *im,
           int *xr_from)
{
    Block *nb = block_new(0);
    int changed = 0;
    int empty = 1;

    for (int i = 0; i < b->props.n; i++) {
        lines_add(&nb->props, b->props.v[i].s, usz(b->props.v[i].n));
    }

    nb->dirty = b->dirty;
    nb->sid = b->sid;
    im->ncols = b->ncols;
    im->cols = calloc(usz(b->ncols) + 1, sizeof *im->cols);

    if (im->cols == NULL) {
        log_fatal(2, "cond: out of memory for the map of a block with %d "
                  "columns", b->ncols);
    }

    *xr_from = r->nxr;

    for (int c = 0; c < b->ncols; c++) {
        const Column *oc = &b->cols[c];
        Column *nc = block_add_column(nb);
        const Text *t = &oc->text;
        int ln = 1;
        int lstart = 0;
        int *map = malloc((usz(t->n) + 2) * sizeof *map);
        int removed = 0;

        if (map == NULL) {
            log_fatal(2, "cond: out of memory for the map of %d tokens",
                      t->n);
        }

        for (int i = 0; i < oc->props.n; i++) {
            lines_add(&nc->props, oc->props.v[i].s, usz(oc->props.v[i].n));
        }

        nc->width = oc->width;

        for (int j = 0; j < t->n; j++) {
            const Tok *k = &t->v[j];
            int keep = r->active;
            int col = b->ncols > 1 ? c + 1 : 0;

            if (k->kind == TK_MARKER) {
                int mt = tok_mtype(k);

                if (mt == MT_IF || mt == MT_ELSE || mt == MT_ENDIF) {
                    handle_marker(r, k, item, col, ln, j - lstart);
                    keep = 0;
                }
            }

            if (keep != 0) {
                Tok nt;

                if (k->kind == TK_MARKER) {
                    nt = tok_marker(tok_mtype(k), tok_mdata(k), tok_mlen(k));

                    if (tok_mtype(k) == MT_XREF) {
                        add_xref(r, k, -1, c, nc->text.n);
                    }
                } else {
                    nt = *k;
                }

                map[nc->text.n] = j;
                text_insert(&nc->text, nc->text.n, nt);
            } else {
                removed = 1;
            }

            if (k->kind == TK_NL) {
                ln++;
                lstart = j + 1;
            }
        }

        /* A text that lost its last end of line gets one again. */
        if (removed != 0 && nc->text.n > 0 &&
            nc->text.v[nc->text.n - 1].kind != TK_NL) {
            Tok nl;

            memset(&nl, 0, sizeof nl);
            nl.kind = TK_NL;
            map[nc->text.n] = t->n > 0 ? t->n - 1 : 0;
            text_insert(&nc->text, nc->text.n, nl);
        }

        if (removed != 0) {
            changed = 1;
            im->cols[c].map = map;
            im->cols[c].n = nc->text.n;
        } else {
            free(map);
        }

        if (only_ends(&nc->text) == 0) {
            empty = 0;
        }
    }

    if (changed != 0 && empty != 0 && headline == 0) {
        block_free(nb);

        for (int c = 0; c < im->ncols; c++) {
            free(im->cols[c].map);
        }

        free(im->cols);
        im->cols = NULL;
        r->nxr = *xr_from;

        return NULL;
    }

    if (changed == 0) {
        free(im->cols);
        im->cols = NULL;
    }

    return nb;
}


/*
 * Make the structure of the section items again after items were left out.
 * Every section that is kept has the depth it had in the source, as far
 * as the sections around it allow: a section is at most one level deeper than
 * the section before it, so a section whose parent was left out moves up.
 * The plus and minus items are made from these depths, so that no plus holds
 * nothing and no minus is without its plus.  Returns 1 when a section had to
 * move.
 */
static int
rebuild_structure(const Run *r, Piece *out, int *nout)
{
    int open = 0;
    int n = 0;
    int moved = 0;
    int have_sect = 0;

    for (int i = 0; i < r->npc; i++) {
        const Piece *p = &r->pc[i];

        if (p->type == IT_SECT) {
            int want = p->depth;

            if (have_sect == 0) {
                want = 0;
            } else if (want > open + 1) {
                want = open + 1;
            }

            if (want != p->depth) {
                moved = 1;
            }

            while (open < want) {
                memset(&out[n], 0, sizeof out[n]);
                out[n].type = IT_PLUS;
                out[n].orig = -1;
                out[n].pid = -1;
                n++;
                open++;
            }

            while (open > want) {
                memset(&out[n], 0, sizeof out[n]);
                out[n].type = IT_MINUS;
                out[n].orig = -1;
                out[n].pid = -1;
                n++;
                open--;
            }

            have_sect = 1;
        }

        out[n++] = *p;
    }

    while (open > 0) {
        memset(&out[n], 0, sizeof out[n]);
        out[n].type = IT_MINUS;
        out[n].orig = -1;
        out[n].pid = -1;
        n++;
        open--;
    }

    *nout = n;

    return moved;
}


/* The number of the section in the copy whose first block is item. */
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


Doc *
cond_filter(const Doc *src, const VarList *vars, DiagList *diags,
            CondMap **map)
{
    Run r;
    Doc *out;
    Piece *fin;
    int nfin = 0;
    int depth = 0;
    int *o2f;
    int *p2f;
    CondMap *m;
    int restructured;

    *map = NULL;

    if (has_conditionals(src) == 0) {
        return NULL;
    }

    memset(&r, 0, sizeof r);
    r.src = src;
    r.vars = vars;
    r.diags = diags;
    r.active = 1;

    for (int i = 0; i < src->nitems; i++) {
        const Item *it = &src->items[i];

        if (it->type == IT_BLOCK) {
            Piece *p;
            ItemMap im;
            int xr_from;
            Block *nb;
            int headline = i > 0 && src->items[i - 1].type == IT_SECT &&
                           r.npc > 0 && r.pc[r.npc - 1].orig == i - 1;

            memset(&im, 0, sizeof im);
            nb = copy_block(&r, it->blk, i, headline, &im, &xr_from);

            if (nb == NULL) {
                continue;
            }

            p = add_piece(&r);
            p->type = IT_BLOCK;
            p->blk = nb;
            p->orig = i;
            p->im = im;

            for (int x = xr_from; x < r.nxr; x++) {
                r.xr[x].piece = r.npc - 1;
            }
        } else if (it->type == IT_PLUS) {
            depth++;
        } else if (it->type == IT_MINUS) {
            depth--;
        } else if (r.active != 0) {
            Piece *p = add_piece(&r);

            p->type = it->type;
            p->orig = i;
            p->depth = depth;
            p->fold = it->fold;
            p->se = it->se;
        }
    }

    /* Every If that is still open has no Endif. */
    for (int i = 0; i < r.depth; i++) {
        tell(&r, &r.lv[i].open);
    }

    fin = calloc(2 * usz(r.npc) + 8, sizeof *fin);
    o2f = malloc((usz(src->nitems) + 1) * sizeof *o2f);
    p2f = malloc((usz(r.npc) + 1) * sizeof *p2f);

    if (fin == NULL || o2f == NULL || p2f == NULL) {
        log_fatal(2, "cond: out of memory for %d items of the copy", r.npc);
    }

    restructured = rebuild_structure(&r, fin, &nfin);

    if (restructured != 0 && diags != NULL) {
        Diag g;

        make_diag(&r, &g, DIAG_WARNING, "if-structure", NULL,
                  "Text that depends on an If cuts across the sections: a "
                  "section whose parent is left out moves up a level.", -1,
                  0, 0, -1, 0);
        tell(&r, &g);
    }

    for (int i = 0; i < src->nitems; i++) {
        o2f[i] = -1;
    }

    for (int i = 0; i < r.npc; i++) {
        p2f[i] = -1;
    }

    /* The copy of the document: the header and the tail are the same. */
    out = calloc(1, sizeof *out);

    if (out == NULL) {
        log_fatal(2, "cond: out of memory for the copy of a document of %d "
                  "items", src->nitems);
    }

    out->structured = src->structured;
    out->next_sid = src->next_sid;

    for (int i = 0; i < src->header.n; i++) {
        lines_add(&out->header, src->header.v[i].s,
                  usz(src->header.v[i].n));
    }

    out->tail_len = src->tail_len;
    out->tail = malloc(src->tail_len + 1);

    if (out->tail == NULL) {
        log_fatal(2, "cond: out of memory for a tail of %zu bytes",
                  src->tail_len);
    }

    if (src->tail_len > 0) {
        memcpy(out->tail, src->tail, src->tail_len);
    }

    m = calloc(1, sizeof *m);

    if (m == NULL) {
        log_fatal(2, "cond: out of memory for the record of a copy of %d "
                  "items", nfin);
    }

    m->orig = src;
    m->n = nfin;
    m->orig_item = malloc((usz(nfin) + 1) * sizeof *m->orig_item);
    m->im = calloc(usz(nfin) + 1, sizeof *m->im);

    if (m->orig_item == NULL || m->im == NULL) {
        log_fatal(2, "cond: out of memory for the map of %d items", nfin);
    }

    for (int i = 0; i < nfin; i++) {
        int at = doc_insert_item(out, out->nitems, fin[i].type, fin[i].blk);

        out->items[at].fold = fin[i].fold;
        out->items[at].se = fin[i].se;
        m->orig_item[at] = fin[i].orig;
        m->im[at] = fin[i].im;

        if (fin[i].orig >= 0) {
            o2f[fin[i].orig] = at;
        }

        if (fin[i].pid >= 0) {
            p2f[fin[i].pid] = at;
        }
    }

    out->modified = 0;

    /* The cross-references of the copy point at the same sections. */
    {
        MarkerSecNum *sn;
        int nsn = marker_section_numbers(out, &sn);

        for (int x = 0; x < r.nxr; x++) {
            int pi = r.xr[x].piece;
            int fi = pi >= 0 ? p2f[pi] : -1;
            int target = marker_xref_item(r.xr[x].orig);
            const char *num = NO_SECTION;
            Tok *t;
            MPayload p;
            size_t len;
            uint8_t *nd;

            if (fi < 0 || target < 0) {
                continue;
            }

            if (o2f[target] >= 0) {
                const char *found = number_of(sn, nsn, o2f[target]);

                if (found != NULL) {
                    num = found;
                }
            }

            t = &out->items[fi].blk->cols[r.xr[x].col].text.v[r.xr[x].tok];
            mp_parse(&p, marker_info(MT_XREF), tok_mdata(t), tok_mlen(t));
            mp_set(&p, "XN", num, 1);
            nd = mp_build(&p, &len);
            tok_marker_set(t, nd, len);
            free(nd);
            mp_free(&p);
        }

        free(sn);
    }

    free(fin);
    free(o2f);
    free(p2f);
    free(r.pc);
    free(r.xr);
    *map = m;

    return out;
}


void
cond_orig_pos(const CondMap *m, const Doc *copy, int item, int col, int line,
              int off, int *oitem, int *ocol, int *oline, int *ooff)
{
    int ci = col > 0 ? col - 1 : 0;
    const Text *t;
    const Text *ot;
    const TokMap *tm;
    int ln = 1;
    int s = 0;
    int idx;
    int oidx;

    *oitem = item;
    *ocol = col;
    *oline = line;
    *ooff = off;

    if (item < 0 || item >= m->n) {
        return;
    }

    *oitem = m->orig_item[item];

    if (*oitem < 0 || m->im[item].cols == NULL || ci >= m->im[item].ncols ||
        m->im[item].cols[ci].map == NULL || line <= 0) {
        return;
    }

    tm = &m->im[item].cols[ci];
    t = &copy->items[item].blk->cols[ci].text;
    ot = &m->orig->items[*oitem].blk->cols[ci].text;

    for (int j = 0; j < t->n && ln < line; j++) {
        if (t->v[j].kind == TK_NL) {
            ln++;
            s = j + 1;
        }
    }

    idx = off >= 0 ? s + off : s;

    if (idx >= tm->n) {
        idx = tm->n > 0 ? tm->n - 1 : 0;
    }

    oidx = tm->n > 0 ? tm->map[idx] : 0;
    ln = 1;
    s = 0;

    for (int j = 0; j < oidx && j < ot->n; j++) {
        if (ot->v[j].kind == TK_NL) {
            ln++;
            s = j + 1;
        }
    }

    *oline = ln;
    *ooff = off >= 0 ? oidx - s : -1;
}


const Doc *
cond_orig(const CondMap *m)
{
    return m->orig;
}


void
cond_map_free(CondMap *m)
{
    if (m == NULL) {
        return;
    }

    for (int i = 0; i < m->n; i++) {
        if (m->im[i].cols != NULL) {
            for (int c = 0; c < m->im[i].ncols; c++) {
                free(m->im[i].cols[c].map);
            }

            free(m->im[i].cols);
        }
    }

    free(m->im);
    free(m->orig_item);
    free(m);
}
