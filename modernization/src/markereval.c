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
 * markereval.c - the markers that depend on the rest of the document:
 * counters (the number each use gets) and cross-references (the tag or the
 * headline of another section).  See docs/WORKLIST.txt for the rules.
 */
#include "util.h"
#include "marker.h"
#include "fileops.h"
#include "layout.h"
#include "note.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const Tok *k;
    char text[64];
    int n; /* a Note: its number among the notes of its kind */
} CounterUse;

static CounterUse *uses;
static int nuses;
static int capuses;
static int neval; /* counters and cross-references in the document */
typedef struct {
    const uint8_t *md; /* the marker data the entry was made for */
    unsigned sid; /* the identity of the section pointed at */
    char xn[32]; /* the number last written to the marker */
    int item; /* the headline item of the section, or -1 when it is gone */
} XLink;

static int counter_limit; /* a counter stopped at COUNTER_MAX */
static const Doc *cur;
static const Doc *xdoc; /* the document that xlinks belongs to */
static XLink *xlinks; /* the table of cross-reference links */
static int nxlinks;


/* The number styles are in marker.c (marker_number_text). */


/* The style that a tag character stands for, or 0. */
static int
style_of(char c)
{
    return c == '1' ? 1 : c == 'i' ? 2 : c == 'I' ? 3 : c == 'A' ? 4 :
           c == 'a' ? 5 : 0;
}


/*
 * Make the text of a counter from its tag: the first \\X\\ in the tag
 * (X is 1, i, I, A or a) is the number, in the style that the marker chose
 * (override, 0 for the style of the tag); the rest of the tag is kept.
 */
void
marker_counter_text(const char *tag, int override, uint32_t v, char *out,
                    size_t cap)
{
    size_t n = 0;
    int done = 0;

    out[0] = '\0';

    for (const char *p = tag; *p != '\0' && n + 1 < cap; p++) {
        if (done == 0 && p[0] == '\\' && style_of(p[1]) != 0 &&
            p[2] == '\\') {
            char num[32];
            int style = override != 0 ? override : style_of(p[1]);

            marker_number_text(style, v, num, sizeof num);

            for (const char *q = num; *q != '\0' && n + 1 < cap; q++) {
                out[n++] = *q;
            }

            p += 2;
            done = 1;
        } else {
            out[n++] = *p;
        }
    }

    out[n] = '\0';

    /* A tag without a number gets one at its start. */
    if (done == 0) {
        char num[32];

        marker_number_text(override != 0 ? override : 1, v, num, sizeof num);
        snprintf(out, cap, "%s%s", num, tag);
    }
}


/* The tag of counter n (1 to MAX_COUNTERS): the header record, else the default. */
void
marker_counter_tag(const Doc *d, int n, char *out, size_t cap)
{
    char code[16];
    const char *v;

    snprintf(code, sizeof code, "OT%dT", n);
    v = d != NULL ? doc_opt_get(d, code) : NULL;

    if (v != NULL) {
        str_copy(out, cap, v);
    } else {
        str_copy(out, cap, n == 2 ? "\\A\\." : "\\1\\.");
    }
}


/* ----------------------------------------------------------- counters */
static void
add_use(const Tok *k, const char *text, int n)
{
    if (nuses == capuses) {
        capuses = capuses != 0 ? capuses * 2 : 64;
        uses = realloc(uses, usz(capuses) * sizeof *uses);
    }

    uses[nuses].k = k;
    str_copy(uses[nuses].text, sizeof uses[nuses].text, text);
    uses[nuses].n = n;
    nuses++;
}


/* A freed document must not be used by the evaluation any more. */
static void
eval_forget(const Doc *d)
{
    if (cur == d) {
        cur = NULL;
        nuses = 0;
        neval = 0;
        free(xlinks);
        xlinks = NULL;
        nxlinks = 0;
        xdoc = NULL;
        marker_eval_fn = NULL;
    }
}


static void
xref_update(Doc *d);


/*
 * Number the counters of the document in the order of the text.  The first
 * use of a counter takes its value if it has one, else 1; every later use
 * adds one to the counter, or takes the value if it has one; a style on a
 * marker stays with the counter from then on.
 */
void
marker_prepare(Doc *d)
{
    uint32_t state[MAX_COUNTERS + 1] = {0};
    int seen[MAX_COUNTERS + 1] = {0};
    int style[MAX_COUNTERS + 1] = {0};
    int nnote[NK_COUNT] = {0};
    NoteFmt nf;

    note_format_get(d, &nf);
    cur = d;
    counter_limit = 0;
    nuses = 0;
    neval = 0;
    marker_eval_fn = marker_eval;
    marker_forget_fn = eval_forget;

    for (int i = 0; d != NULL && i < d->nitems; i++) {
        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        const Block *b = d->items[i].blk;

        for (int c = 0; c < b->ncols; c++) {
            const Text *t = &b->cols[c].text;

            for (int j = 0; j < t->n; j++) {
                const Tok *k = &t->v[j];
                MPayload p;
                const char *v;
                int n;
                char tag[96];
                char text[64];

                if (k->kind == TK_MARKER && tok_mtype(k) == 2) {
                    neval++;
                }

                if (k->kind == TK_MARKER && tok_mtype(k) == 1) {
                    int kind = note_kind(k, nf.primary);

                    neval++;
                    nnote[kind]++;
                    note_tag_text(nf.k[kind].reftag, nnote[kind], text,
                                  sizeof text);
                    add_use(k, text, nnote[kind]);
                    continue;
                }

                if (k->kind != TK_MARKER || tok_mtype(k) != 17) {
                    continue;
                }

                neval++;
                mp_parse(&p, marker_info(17), tok_mdata(k), tok_mlen(k));
                v = mp_get(&p, "1");
                n = v != NULL ? atoi(v) : 1;
                n = n < 1 || n > MAX_COUNTERS ? 1 : n;
                v = mp_get(&p, "2");

                if (v != NULL && marker_parse_u32(v, &state[n]) == 0) {
                    /* The value that the marker sets. */
                } else if (seen[n] == 0) {
                    state[n] = 1;
                } else if (state[n] < COUNTER_MAX) {
                    state[n]++;
                } else {
                    /* The counter stops at the largest value. */
                    counter_limit = 1;
                }

                seen[n] = 1;
                v = mp_get(&p, "3");

                if (v != NULL && atoi(v) >= 1 && atoi(v) <= 5) {
                    style[n] = atoi(v);
                }

                marker_counter_tag(d, n, tag, sizeof tag);
                marker_counter_text(tag, style[n], state[n], text,
                                    sizeof text);
                add_use(k, text, 0);
                mp_free(&p);
            }
        }
    }

    if (neval > 0) {
        xref_update(d);
    } else {
        free(xlinks);
        xlinks = NULL;
        nxlinks = 0;
        xdoc = NULL;
    }
}


int
marker_counter_limit(void)
{
    return counter_limit;
}


int
marker_prepared_none(const Doc *d)
{
    return cur == d && neval == 0;
}


/* ------------------------------------------------- cross-references */
/*
 * A cross-reference stores the number of the section it points at (XN), as
 * the format does.  A number is not a lasting name, though: adding,
 * deleting or moving a section changes the numbers of the others.  So each
 * section that is pointed at gets a lasting identity (Block.sid of its
 * headline block, which moves with the section and is copied with it), and
 * every cross-reference keeps the identity in a table of this file.  After
 * each edit, xref_update looks the identity up again, writes the section's
 * present number into XN (so that the saved file is right for other readers
 * too) and marks the document as changed when a number moved.
 */
typedef MarkerSecNum SecNum;



static void
trim(char *s)
{
    for (int n = slen(s); n > 0 && s[n - 1] == ' '; n--) {
        s[n - 1] = '\0';
    }
}


/* The numbers of all the sections of the document, in one pass. */
int
marker_section_numbers(const Doc *d, MarkerSecNum **out)
{
    int cnt[SECT_MAXDEPTH + 1] = {0};
    int depth = 0;
    int from = d->structured != 0 ? 1 : 0;
    int n = 0;
    SecNum *sn = malloc(usz(d->nitems + 1) * sizeof *sn);

    for (int i = 0; i < d->nitems; i++) {
        int t = d->items[i].type;

        if (t == IT_PLUS) {
            depth++;

            if (depth <= SECT_MAXDEPTH) {
                cnt[depth] = 0;
            }
        } else if (t == IT_MINUS) {
            depth--;
        } else if (t == IT_SECT && depth >= from && depth <= SECT_MAXDEPTH) {
            if (depth >= 0) {
                cnt[depth]++;
            }

            if (i + 1 < d->nitems && d->items[i + 1].type == IT_BLOCK) {
                int o = 0;

                sn[n].item = i + 1;
                sn[n].num[0] = '\0';

                for (int l = from;
                     l <= depth && o < isz(sizeof sn[n].num) - 12; l++) {
                    o += snprintf(sn[n].num + o, sizeof sn[n].num - usz(o),
                                  l == from ? "%d" : ".%d", cnt[l]);
                }

                n++;
            }
        } else if (t == IT_SECT && depth >= 0 && depth <= SECT_MAXDEPTH) {
            cnt[depth]++;
        }
    }

    *out = sn;

    return n;
}


/*
 * The link of a cross-reference marker.  The table is in the order of the
 * document and the markers are asked for in that order (by the update after
 * an edit and by the printer), so the entry after the last one found is
 * tried first; any other marker is looked for from the start.  A document
 * of many thousands of cross-references is therefore not searched once for
 * each of them.
 */
static int xhint;


static const XLink *
xlink_of(const Tok *k)
{
    const uint8_t *md = tok_mdata(k);

    for (int step = 0; step < 2; step++) {
        int i = xhint + step;

        if (i < nxlinks && xlinks[i].md == md) {
            xhint = i;

            return &xlinks[i];
        }
    }

    for (int i = 0; i < nxlinks; i++) {
        if (xlinks[i].md == md) {
            xhint = i;

            return &xlinks[i];
        }
    }

    return NULL;
}


/* A table of the section numbers, to find a number without a scan. */
typedef struct {
    int *slot; /* the index into the numbers, or -1 */
    size_t mask; /* the size of the table less one; a power of two */
} NumHash;


static size_t
num_hash(const char *s)
{
    size_t h = 5381;

    for (; *s != '\0'; s++) {
        h = h * 33 + u8(*s);
    }

    return h;
}


static void
numhash_build(NumHash *h, const SecNum *sn, int n)
{
    int cap = 16;

    while (cap < 2 * n) {
        cap *= 2;
    }

    h->mask = usz(cap) - 1;
    h->slot = malloc(usz(cap) * sizeof *h->slot);

    for (int i = 0; i < cap; i++) {
        h->slot[i] = -1;
    }

    for (int i = n - 1; i >= 0; i--) {
        /* Going backward lets the first of equal numbers win. */
        size_t at = num_hash(sn[i].num) & h->mask;

        while (h->slot[at] >= 0 &&
               strcmp(sn[h->slot[at]].num, sn[i].num) != 0) {
            at = (at + 1) & h->mask;
        }

        h->slot[at] = i;
    }
}


/* The index of the section with the number, or -1. */
static int
numhash_find(const NumHash *h, const SecNum *sn, const char *num)
{
    size_t at = num_hash(num) & h->mask;

    while (h->slot[at] >= 0) {
        if (strcmp(sn[h->slot[at]].num, num) == 0) {
            return h->slot[at];
        }

        at = (at + 1) & h->mask;
    }

    return -1;
}


/*
 * Bring every cross-reference of the document up to date with the sections:
 * resolve the ones that are new (just read, or edited on their panel) by
 * their number, follow the others by the identity of their section.
 */
static void
xref_update(Doc *d)
{
    SecNum *sn;
    int nsn = marker_section_numbers(d, &sn);
    XLink *nl = NULL;
    int nn = 0;
    int cap = 0;
    int have_old = xlinks != NULL && xdoc == d;
    NumHash nh;
    int *by_sid;
    size_t nsid;

    numhash_build(&nh, sn, nsn);
    /* A section gets at most one new identity in this pass. */
    nsid = d->next_sid + usz(nsn) + 2;
    by_sid = malloc(nsid * sizeof *by_sid);

    for (size_t s = 0; s < nsid; s++) {
        by_sid[s] = -1;
    }

    for (int s = 0; s < nsn; s++) {
        unsigned id = d->items[sn[s].item].blk->sid;

        if (id != 0 && id < nsid && by_sid[id] < 0) {
            by_sid[id] = s;
        }
    }

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        Block *b = d->items[i].blk;

        for (int c = 0; c < b->ncols; c++) {
            Text *t = &b->cols[c].text;

            for (int j = 0; j < t->n; j++) {
                Tok *k = &t->v[j];
                MPayload p;
                const char *num;
                const XLink *old;
                XLink e;

                if (k->kind != TK_MARKER || tok_mtype(k) != 2) {
                    continue;
                }

                mp_parse(&p, marker_info(2), tok_mdata(k), tok_mlen(k));
                num = mp_get(&p, "XN");
                memset(&e, 0, sizeof e);
                e.md = tok_mdata(k);
                e.item = -1;
                old = have_old != 0 ? xlink_of(k) : NULL;

                if (old != NULL && num != NULL && strcmp(old->xn, num) == 0) {
                    e.sid = old->sid;
                } else if (num != NULL) {
                    /* A new reference: it points at the section that has
                       the number it holds now. */
                    int s = numhash_find(&nh, sn, num);

                    if (s >= 0) {
                        Block *hb = d->items[sn[s].item].blk;

                        if (hb->sid == 0) {
                            hb->sid = ++d->next_sid;
                        }

                        e.sid = hb->sid;
                        by_sid[hb->sid] = s;
                    }
                }

                str_copy(e.xn, sizeof e.xn, num != NULL ? num : "");

                if (e.sid != 0 && e.sid < nsid && by_sid[e.sid] >= 0) {
                    const SecNum *cs = &sn[by_sid[e.sid]];

                    e.item = cs->item;

                    if (strcmp(cs->num, e.xn) != 0) {
                        /* The section has another number now. */
                        size_t len;
                        uint8_t *nd;

                        mp_set(&p, "XN", cs->num, 1);
                        nd = mp_build(&p, &len);
                        tok_marker_set(k, nd, len);
                        free(nd);
                        e.md = tok_mdata(k);
                        str_copy(e.xn, sizeof e.xn, cs->num);
                        d->modified = 1;
                    }
                }

                mp_free(&p);

                if (nn == cap) {
                    cap = cap != 0 ? cap * 2 : 64;
                    nl = realloc(nl, usz(cap) * sizeof *nl);
                }

                nl[nn++] = e;
            }
        }
    }

    free(xlinks);
    xlinks = nl;
    nxlinks = nn;
    xdoc = d;
    xhint = 0;
    free(nh.slot);
    free(by_sid);
    free(sn);
}


/*
 * The text a cross-reference stands for: the tag of the section, its
 * headline, both, or (the page number is only known when the pages are
 * made) a note that says so.  Returns 0 when the section is not there.
 */
static int
xref_text(const Tok *k, char *buf, size_t cap)
{
    MPayload p;
    const char *st;
    const XLink *lk;
    int style;
    int item;
    Fmt f;
    char head[160] = "";
    char tag[64];
    int ok = 0;

    if (cur == NULL) {
        return 0;
    }

    mp_parse(&p, marker_info(2), tok_mdata(k), tok_mlen(k));
    st = mp_get(&p, "XT");
    style = st != NULL ? atoi(st) : 0;

    /* The style [Tag] is what the Cross-reference Format of the document
       says: the tag, or the headline (OC2), or both (OC3). */
    if (style == 0) {
        const char *oc = doc_opt_get(cur, "OC");
        int g = oc != NULL ? atoi(oc) : 1;

        style = g == 2 || g == 3 ? g : 1;
    }

    lk = xlink_of(k);
    item = lk != NULL ? lk->item : -1;

    if (item < 0) {
        str_copy(buf, cap, "[no section]");
        mp_free(&p);

        return 1;
    }

    fmt_resolve(cur, item, 0, &f);
    str_copy(tag, sizeof tag, f.tag);
    trim(tag);

    if (tag[0] == '\0') {
        str_copy(tag, sizeof tag, lk->xn);
    }

    if (cur->items[item].blk->ncols > 0) {
        text_plain(&cur->items[item].blk->cols[0].text, head, sizeof head);
    }

    if (style == 2) {
        str_copy(buf, cap, head);
    } else if (style == 3) {
        snprintf(buf, cap, "%s %s", tag, head);
    } else if (style == 4) {
        snprintf(buf, cap, "page of %s", tag);
    } else {
        str_copy(buf, cap, tag);
    }

    ok = 1;
    mp_free(&p);

    return ok;
}


int
marker_xref_item(const Tok *k)
{
    const XLink *lk = tok_mtype(k) == 2 ? xlink_of(k) : NULL;

    return lk != NULL ? lk->item : -1;
}


/* The hook of marker_render: the text of a counter or a cross-reference. */
int
marker_note_number(const Tok *k)
{
    for (int i = 0; i < nuses; i++) {
        if (uses[i].k == k) {
            return uses[i].n;
        }
    }

    return 0;
}


int
marker_eval(const Tok *k, char *buf, size_t cap)
{
    if (tok_mtype(k) == 2) {
        return xref_text(k, buf, cap);
    }

    for (int i = 0; i < nuses; i++) {
        if (uses[i].k == k) {
            str_copy(buf, cap, uses[i].text);

            return 1;
        }
    }

    return 0;
}
