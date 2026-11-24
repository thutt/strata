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
 * memprof.c - the memory and the time of opening a document.  It loads the
 * file, lays it out like the editor does, lays it out again (which is what
 * repaginating costs) and prints the peak and current memory after every
 * step together with the size and the count of the big structures.
 */
#define _GNU_SOURCE
#include "../src/util.h"
#include "../src/editor.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


/* The time in milliseconds. */
static long
now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


/* Read a number of kilobytes from a line of /proc/self/status. */
static long
status_kb(const char *key)
{
    char line[256];
    long v = -1;
    FILE *f = fopen("/proc/self/status", "r");

    if (f == NULL) {
        return -1;
    }

    while (fgets(line, sizeof line, f) != NULL) {
        size_t n = strlen(key);

        if (strncmp(line, key, n) == 0) {
            v = strtol(line + n, NULL, 10);
            break;
        }
    }

    fclose(f);

    return v;
}


static void
report(const char *what, long t0)
{
    struct mallinfo2 mi = mallinfo2();

    printf("%-28s %6ld ms  RSS %6ld KB  peak %6ld KB  heap in use %6zu KB\n",
           what, now() - t0, status_kb("VmRSS:"), status_kb("VmHWM:"),
           (mi.uordblks + mi.hblkhd) / 1024);
    fflush(stdout);
}


/* One line of the account: a structure, how many, and its size. */
static void
row(const char *name, size_t count, size_t each)
{
    printf("  %-26s %10zu %12zu %14zu\n", name, count, each, count * each);
}


static void
account(const Editor *e)
{
    const Doc *d = e->doc;
    size_t ntok = 0;
    size_t captok = 0;
    size_t nmark = 0;
    size_t ncols = 0;
    size_t nprops = 0;
    size_t propbytes = 0;
    size_t ncells = 0;
    size_t cellbytes = 0;
#ifdef LROW_PACKED
    size_t nfmt[4] = {0, 0, 0, 0};
#endif
    size_t nrows = 0;
    size_t ncl = 0;

    for (int i = 0; i < d->nitems; i++) {
        const Block *b = d->items[i].blk;

        if (b == NULL) {
            continue;
        }

        nprops += usz(b->props.n);

        for (int k = 0; k < b->props.n; k++) {
            propbytes += usz(b->props.v[k].n) + 1;
        }

        for (int c = 0; c < b->ncols; c++) {
            const Column *col = &b->cols[c];

            ncols++;
            ntok += usz(col->text.n);
            captok += usz(col->text.cap);
            nprops += usz(col->props.n);

            for (int k = 0; k < col->props.n; k++) {
                propbytes += usz(col->props.v[k].n) + 1;
            }

            for (int k = 0; k < col->text.n; k++) {
                if (col->text.v[k].kind == TK_MARKER) {
                    nmark++;
                }
            }
        }
    }

    for (int i = 0; i < e->view.nbv; i++) {
        const BView *bv = &e->view.bv[i];

        for (int c = 0; c < bv->ncl; c++) {
            ncl++;

            for (int r = 0; r < bv->cl[c].nrows; r++) {
                nrows++;
                ncells += usz(bv->cl[c].rows[r].n);
#ifdef LROW_PACKED
                {
                    const LRow *lr = &bv->cl[c].rows[r];
                    size_t es = lr->fmt == LF_NEAR ? 2 :
                                lr->fmt == LF_WIDE ? 4 : 0;

                    nfmt[lr->fmt]++;
                    cellbytes += lr->fmt == LF_TEXT ? 0 : usz(lr->n) * (2 + es);
                }
#else
                cellbytes += usz(bv->cl[c].rows[r].n) * sizeof(Cell);
#endif
            }
        }
    }

    printf("\nPer-structure account (counts from the live data):\n");
    printf("  %-26s %10s %12s %14s\n", "structure", "count", "bytes each",
           "bytes total");
    row("Item", usz(d->nitems), sizeof(Item));
    row("Block", usz(d->nitems), sizeof(Block));
    row("Column", ncols, sizeof(Column));
    row("Tok (used)", ntok, sizeof(Tok));
    row("Tok (capacity, slack incl)", captok, sizeof(Tok));
    printf("  %-26s %10zu\n", "markers in the text", nmark);
    printf("  %-26s %10zu %12s %14zu\n", "property records", nprops, "-",
           propbytes);
    printf("  %-26s %10zu %12s %14zu\n", "cells (layout)", ncells, "-",
           cellbytes);
#ifdef LROW_PACKED
    printf("  rows by format: text %zu, dense %zu, near %zu, wide %zu\n",
           nfmt[LF_TEXT], nfmt[LF_DENSE], nfmt[LF_NEAR], nfmt[LF_WIDE]);
#endif
    row("LRow (layout)", nrows, sizeof(LRow));
    row("ColLayout", ncl, sizeof(ColLayout));
    row("VLine (cap)", usz(e->view.cap), sizeof(VLine));
    row("BView", usz(e->view.nbv), sizeof(BView));
}


/* A checksum of everything the screen would show, for every view line. */
static unsigned long
view_sum(Editor *e)
{
    unsigned long h = 1469598103934665603UL;
    Cell *cells = malloc(usz(e->view.width) * sizeof *cells);

    for (int i = 0; i < e->view.n; i++) {
        h = (h ^ uns(e->view.lines[i].kind)) * 1099511628211UL;
        h = (h ^ uns(e->view.lines[i].item)) * 1099511628211UL;
        h = (h ^ uns(e->view.lines[i].row)) * 1099511628211UL;
        view_line_cells(e, i, cells);

        for (int k = 0; k < e->view.width; k++) {
            h = (h ^ cells[k].ch) * 1099511628211UL;
            h = (h ^ cells[k].attr) * 1099511628211UL;
            h = (h ^ uns(cells[k].tok + 1)) * 1099511628211UL;
        }
    }

    free(cells);

    return h;
}


int
main(int argc, char **argv)
{
    char err[128] = "";
    long t0;
    long t1;

    if (argc < 2) {
        fprintf(stderr, "usage: memprof file.doc\n");
        return 2;
    }

    report("start", now());
    t0 = now();
    Doc *d = doc_load(argv[1], err, sizeof err);

    if (d == NULL) {
        fprintf(stderr, "load: %s\n", err);
        return 1;
    }

    report("doc_load", t0);
    Editor *e = ed_new(d);

    e->text_width = 80;
    t0 = now();
    ed_layout(e);
    report("first layout", t0);
    t1 = now();
    ed_invalidate(e);
    ed_layout(e);
    report("repaginate (relayout)", t1);
    printf("vlines %d, items %d\n", e->view.n, d->nitems);
    t0 = now();
    printf("screen checksum %016lx\n", view_sum(e));
    report("checksum of all cells", t0);
    account(e);
    t0 = now();
    size_t len = 0;
    uint8_t *out = doc_serialize(d, &len);

    report("serialize", t0);
    free(out);
    ed_free(e);
    doc_free(d);

    return 0;
}
