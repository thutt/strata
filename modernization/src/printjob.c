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

/* printjob.c - strprint and lp as child processes; see printjob.h. */
#include "util.h"
#include "printjob.h"
#include "sysprog.h"
#include "build_type.h"
#include "vars.h"
#include "log.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* The most that lp may take to answer, in milliseconds. */
#define LP_TIMEOUT_MS 60000
#define LPSTAT_TIMEOUT_MS 3000
#define LPSTAT_PANEL_MS 300
#define LPSTAT_REFRESH_MS 30000


void
pj_opts_init(PjOpts *o)
{
    memset(o, 0, sizeof *o);
    o->copies = 1;
    o->error_log = 1;
    o->banner = 1;
    o->keep_pdf = 1;
    o->index = PJ_AUTO;
    o->list_figures = PJ_AUTO;
    o->list_pictures = PJ_AUTO;
    o->list_tables = PJ_AUTO;
    o->contents = PJ_AUTO;
}


int
pj_printer_wanted(const PjOpts *o)
{
    return o->printer[0] != '\0' &&
           strcasecmp(o->printer, PJ_FILE_ONLY) != 0 &&
           strcasecmp(o->printer, PJ_PDF_VIEWER) != 0 &&
           strcasecmp(o->printer, PJ_PDF_VIEWER_OLD) != 0;
}


int
pj_open_wanted(const PjOpts *o)
{
    return strcasecmp(o->printer, PJ_PDF_VIEWER) == 0 ||
           strcasecmp(o->printer, PJ_PDF_VIEWER_OLD) == 0;
}


/* Read a number of digits that lies between lo and hi. */
static int
parse_number(const char *s, int lo, int hi, int *out)
{
    long v = 0;

    if (*s == '\0') {
        return -1;
    }

    for (; *s != '\0'; s++) {
        if (is_digit(*s) == 0) {
            return -1;
        }

        v = v * 10 + (*s - '0');

        if (v > hi) {
            return -1;
        }
    }

    if (v < lo) {
        return -1;
    }

    *out = isz(lsz(v));

    return 0;
}


int
pj_parse_copies(const char *s, int *out, char *err, size_t cap)
{
    if (parse_number(s, 1, PJ_COPIES_MAX, out) != 0) {
        snprintf(err, cap, "Copies must be a number from 1 to %d",
                 PJ_COPIES_MAX);

        return -1;
    }

    return 0;
}


int
pj_parse_page(const char *s, int *out, char *err, size_t cap)
{
    if (*s == '\0') {
        *out = 0;

        return 0;
    }

    if (parse_number(s, 1, PJ_PAGE_MAX, out) != 0) {
        snprintf(err, cap, "A page must be a number from 1 to %d",
                 PJ_PAGE_MAX);

        return -1;
    }

    return 0;
}


/* A queue name that cannot be taken for an option or a path. */
static int
valid_queue(const char *q)
{
    if (q[0] == '\0' || q[0] == '-' || strlen(q) >= PJ_QNAME) {
        return 0;
    }

    for (; *q != '\0'; q++) {
        if (u8(*q) <= ' ' || u8(*q) >= 0x7F || *q == '/' || *q == '#' ||
            *q == '\\' || *q == '\'' || *q == '"' || *q == ',') {
            return 0;
        }
    }

    return 1;
}


int
pj_check(const PjOpts *o, char *err, size_t cap)
{
    if (o->copies < 1 || o->copies > PJ_COPIES_MAX) {
        snprintf(err, cap, "Copies must be a number from 1 to %d",
                 PJ_COPIES_MAX);

        return -1;
    }

    if (o->sides < PJ_DEFAULT || o->sides > PJ_TWO || o->sort < PJ_DEFAULT ||
        o->sort > PJ_SORT_NO) {
        snprintf(err, cap, "Sides or sort is not a known choice");

        return -1;
    }

    if (pj_printer_wanted(o) != 0 &&
        strcasecmp(o->printer, PJ_SYSTEM_DEFAULT) != 0 &&
        valid_queue(o->printer) == 0) {
        snprintf(err, cap, "\"%.60s\" is not the name of a printer queue",
                 o->printer);

        return -1;
    }

    if (o->page_from < 0 || o->page_from > PJ_PAGE_MAX || o->page_to < 0 ||
        o->page_to > PJ_PAGE_MAX) {
        snprintf(err, cap, "A page must be a number from 1 to %d",
                 PJ_PAGE_MAX);

        return -1;
    }

    if (pj_printer_wanted(o) == 0) {
        const char *what = NULL;

        if (o->page_from != 0 || o->page_to != 0) {
            what = "A page range";
        } else if (o->copies > 1) {
            what = "More than one copy";
        } else if (o->sides != PJ_DEFAULT) {
            what = "Double sided";
        } else if (o->sort != PJ_SORT_DEFAULT) {
            what = "Sort output pages";
        }

        if (what != NULL && pj_open_wanted(o) != 0) {
            snprintf(err, cap, "%s needs a printer: name one in Printer "
                               "(the program that opens the PDF has its own "
                               "print dialog)", what);

            return -1;
        }

        if (what != NULL) {
            snprintf(err, cap, "%s needs a printer: name one in Printer",
                     what);

            return -1;
        }
    }

    if (o->page_from != 0 && o->page_to != 0 && o->page_from > o->page_to) {
        snprintf(err, cap, "The first page is after the last page");

        return -1;
    }

    return 0;
}


void
pj_default_out(const char *docpath, int draft, char *out, size_t cap)
{
    char *dot;
    char *slash;
    char *first;

    out[0] = '\0';

    if (docpath == NULL || docpath[0] == '\0') {
        return;
    }

    str_copy(out, cap, docpath);
    dot = strrchr(out, '.');
    slash = strrchr(out, '/');
    first = slash != NULL ? slash + 1 : out;

    /* A dot that is the first character of the name does not start an
       extension (name_has_extension): .notes becomes .notes.pdf. */
    if (dot != NULL && dot > first) {
        *dot = '\0';
    }

    if (strlen(out) + 11 < cap) {
        strcat(out, draft != 0 ? "-draft.pdf" : ".pdf");
    }
}


int
pj_check_out(const char *out, char *err, size_t cap)
{
    char dir[PATH_MAX];
    struct stat st;
    const char *sl;

    if (out[0] == '\0') {
        snprintf(err, cap, "No output file given");

        return -1;
    }

    sl = strrchr(out, '/');

    /* The name of a file is 255 bytes at most. */
    if (strlen(sl != NULL ? sl + 1 : out) > 255) {
        snprintf(err, cap, "The name of the output file is longer than 255 "
                 "bytes");

        return -1;
    }

    if (sl == NULL) {
        str_copy(dir, sizeof dir, ".");
    } else if (sl == out) {
        str_copy(dir, sizeof dir, "/");
    } else {
        size_t n = lsz(sl - out);

        n = n < sizeof dir - 1 ? n : sizeof dir - 1;
        memcpy(dir, out, n);
        dir[n] = '\0';
    }

    if (stat(out, &st) == 0 && S_ISDIR(st.st_mode) != 0) {
        snprintf(err, cap, "%.200s is a directory", out);

        return -1;
    }

    if (stat(dir, &st) != 0 || S_ISDIR(st.st_mode) == 0) {
        snprintf(err, cap, "Cannot find the directory %.200s", dir);

        return -1;
    }

    if (access(dir, W_OK | X_OK) != 0) {
        snprintf(err, cap, "Cannot write in the directory %.200s", dir);

        return -1;
    }

    return 0;
}


void
pj_argv_init(PjArgv *a)
{
    memset(a, 0, sizeof *a);
    a->v[0] = NULL;
}


void
pj_argv_add(PjArgv *a, const char *s)
{
    if (a->n < PJ_MAXARGV) {
        str_copy(a->buf[a->n], sizeof a->buf[a->n], s);
        a->v[a->n] = a->buf[a->n];
        a->n++;
        a->v[a->n] = NULL;
    }
}


int
pj_lp_argv(const PjOpts *o, const char *lp, const char *pdf, int pages,
           PjArgv *a, char *err, size_t cap)
{
    char num[32];
    int from = o->page_from;
    int to = o->page_to;

    if (pj_check(o, err, cap) != 0) {
        return -1;
    }

    if (pj_printer_wanted(o) == 0) {
        snprintf(err, cap, "No printer is named");

        return -1;
    }

    if (pdf[0] != '/') {
        snprintf(err, cap, "The PDF must be given with its full path");

        return -1;
    }

    if (pages > 0 && from > pages) {
        snprintf(err, cap, "The document has only %d page%s, so it has no "
                           "page %d", pages, pages == 1 ? "" : "s", from);

        return -1;
    }

    pj_argv_init(a);
    pj_argv_add(a, lp);

    /* The default queue of the system is the one that lp takes alone. */
    if (strcasecmp(o->printer, PJ_SYSTEM_DEFAULT) != 0) {
        pj_argv_add(a, "-d");
        pj_argv_add(a, o->printer);
    }

    pj_argv_add(a, "-n");
    snprintf(num, sizeof num, "%d", o->copies);
    pj_argv_add(a, num);

    if (o->sides == PJ_ONE) {
        pj_argv_add(a, "-o");
        pj_argv_add(a, "sides=one-sided");
    } else if (o->sides == PJ_TWO) {
        pj_argv_add(a, "-o");
        pj_argv_add(a, "sides=two-sided-long-edge");
    }

    if (o->sort == PJ_SORT_YES) {
        pj_argv_add(a, "-o");
        pj_argv_add(a, "collate=true");
    } else if (o->sort == PJ_SORT_NO) {
        pj_argv_add(a, "-o");
        pj_argv_add(a, "collate=false");
    }

    if (o->envelope != 0) {
        /* No scaling of an envelope: no media option, which a printer may
           answer by holding the job. */
        pj_argv_add(a, "-o");
        pj_argv_add(a, "print-scaling=none");
    }

    if (from != 0 || to != 0) {
        char range[48];

        from = from != 0 ? from : 1;

        if (to != 0 && pages > 0 && to > pages) {
            to = pages;
        }

        if (to == 0 && pages > 0) {
            to = pages;
        }

        if (to != 0) {
            snprintf(range, sizeof range, "%d-%d", from, to);
        } else {
            snprintf(range, sizeof range, "%d-", from);
        }

        pj_argv_add(a, "-P");
        pj_argv_add(a, range);
    }

    if (o->title[0] != '\0') {
        pj_argv_add(a, "-t");
        pj_argv_add(a, o->title);
    }

    pj_argv_add(a, pdf);

    return 0;
}


int
pj_find_lp(char *out, size_t cap)
{
    return sp_find_env("STRATA_LP", "lp", out, cap);
}


/* A line of "lpstat -a": the name of a queue, and whether it is not
   accepting jobs now. */
static void
take_queue(PjQueues *q, const char *line)
{
    char name[PJ_QNAME];
    size_t n = 0;

    while (line[n] != '\0' && line[n] != ' ' && line[n] != '\t' &&
           n < sizeof name - 1) {
        name[n] = line[n];
        n++;
    }

    name[n] = '\0';

    if (valid_queue(name) == 0 || line[n] == '\0' || line[n] == ':' ||
        q->n >= PJ_MAXQ) {
        return;
    }

    /* A message such as "lpstat: ..." has a colon after the first word. */
    if (n > 0 && name[n - 1] == ':') {
        return;
    }

    str_copy(q->name[q->n], sizeof q->name[q->n], name);
    q->off[q->n] = strstr(line + n, "not accepting") != NULL;
    q->n++;
}


/*
 * The answers of lpstat.  Each flag has a slot: lpstat is started once per
 * run of the program, in the background, and its answer is kept; a panel
 * that needs the answer waits for it only a short time (LPSTAT_PANEL_MS), so
 * a scheduler that answers slowly never freezes the program.
 */
typedef struct {
    PvJob job;
    int state; /* 0 not started, 1 running, 2 answered or given up. */
    int ok; /* 1 when an answer of lpstat is held in text. */
    char text[8192]; /* The answer that is held, until a newer one is there. */
    size_t len;
    char fresh[8192]; /* What the run that is going now has written. */
    size_t flen;
    long start_ms;
    long done_ms; /* When the run ended, for the age of the answer. */
} LpSlot;

static LpSlot lp_slot[2];

#define SLOT_QUEUES 0
#define SLOT_DEFAULT 1


static long
now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}


/*
 * The age after which the answer of lpstat is asked for again: 30 seconds.
 * A debug build reads another age from STRATA_LPSTAT_REFRESH_MS, so that a
 * test need not wait; a release build never looks at the variable.
 */
static long
refresh_ms(void)
{
    const char *v;

    if (strata_release == 0) {
        v = getenv("STRATA_LPSTAT_REFRESH_MS");

        if (v != NULL && v[0] >= '0' && v[0] <= '9') {
            return atol(v);
        }
    }

    return LPSTAT_REFRESH_MS;
}


/*
 * Start lpstat with one flag (STRATA_LPSTAT names it, else the fixed folders
 * of the system, never PATH).  It does nothing while a run is going, nor
 * while the answer that is held is younger than refresh_ms(); an older
 * answer is asked for again, and stays in use until the new one is there.
 */
static void
slot_start(int which, const char *flag)
{
    LpSlot *s = &lp_slot[which];
    char lpstat[PATH_MAX];
    char fl[8];
    char *argv[3];

    if (s->state == 1) {
        return;
    }

    if (s->state == 2 && now_ms() - s->done_ms < refresh_ms()) {
        return;
    }

    s->state = 2;
    s->flen = 0;
    s->fresh[0] = '\0';
    s->done_ms = now_ms();

    if (sp_find_env("STRATA_LPSTAT", "lpstat", lpstat, sizeof lpstat) != 0) {
        return;
    }

    str_copy(fl, sizeof fl, flag);
    argv[0] = lpstat;
    argv[1] = fl;
    argv[2] = NULL;

    if (pv_job_start(&s->job, argv, tmp_dir()) != 0) {
        return;
    }

    s->state = 1;
    s->start_ms = now_ms();
}


/* Read what lpstat has written so far, and finish the slot when it is over. */
static void
slot_read(LpSlot *s)
{
    long got;

    while (s->flen < sizeof s->fresh - 1 &&
           (got = pv_job_read(&s->job, s->fresh + s->flen,
                              sizeof s->fresh - 1 - s->flen)) > 0) {
        s->flen += lsz(got);
    }

    if (pv_job_done(&s->job) != 0) {
        while (s->flen < sizeof s->fresh - 1 &&
               (got = pv_job_read(&s->job, s->fresh + s->flen,
                                  sizeof s->fresh - 1 - s->flen)) > 0) {
            s->flen += lsz(got);
        }

        s->state = 3;
    } else if (s->flen >= sizeof s->fresh - 1 ||
               now_ms() - s->start_ms >= LPSTAT_TIMEOUT_MS) {
        s->state = 3;
    }

    if (s->state == 3) {
        pv_job_kill(&s->job);
        s->fresh[s->flen] = '\0';
        memcpy(s->text, s->fresh, s->flen + 1);
        s->len = s->flen;
        s->ok = 1;
        s->state = 2;
        s->done_ms = now_ms();
    }
}


/*
 * Wait for a slot at most max_ms (counted from now).  Returns 1 when no run
 * is going (the answer is there, possibly "lpstat not found") and 0 when a
 * run is still going.  The answer is kept, so a later call costs nothing.
 */
static int
slot_wait(int which, long max_ms)
{
    LpSlot *s = &lp_slot[which];
    long t0 = now_ms();

    while (s->state == 1) {
        slot_read(s);

        if (s->state != 1 || now_ms() - t0 >= max_ms) {
            break;
        }

        usleep(5000);
    }

    return s->state == 2;
}


void
pj_printers_reset(void)
{
    for (int i = 0; i < 2; i++) {
        if (lp_slot[i].state == 1) {
            pv_job_kill(&lp_slot[i].job);
        }

        memset(&lp_slot[i], 0, sizeof lp_slot[i]);
    }
}


void
pj_printers_prefetch(void)
{
    slot_start(SLOT_QUEUES, "-a");
    slot_start(SLOT_DEFAULT, "-d");
}


int
pj_queues_wait(PjQueues *q, long max_ms)
{
    char text[8192];
    int found = 0;

    memset(q, 0, sizeof *q);
    q->list[0] = PJ_FILE_ONLY;
    q->list[1] = PJ_PDF_VIEWER;
    q->list[2] = PJ_SEARCH;
    q->list[3] = NULL;
    q->nlist = 3;

    /* A look that does not wait (a panel that is open) never starts lpstat. */
    if (max_ms > 0) {
        pj_printers_prefetch();
    }

    /* A run that is going gets a short time; when it is not over and an
       older answer is held, that answer is used. */
    if (slot_wait(SLOT_QUEUES, max_ms) == 0 &&
        lp_slot[SLOT_QUEUES].ok == 0) {
        q->pending = 1;

        return PJ_Q_PENDING;
    }

    if (lp_slot[SLOT_QUEUES].ok == 0) {
        return -1;
    }

    str_copy(text, sizeof text, lp_slot[SLOT_QUEUES].text);

    for (char *p = text; *p != '\0';) {
        char *e = strchr(p, '\n');

        if (e != NULL) {
            *e = '\0';
        }

        take_queue(q, p);
        p = e != NULL ? e + 1 : p + strlen(p);
    }

    for (int i = 0; i < q->n; i++) {
        q->list[2 + i] = q->name[i];
        found++;
    }

    /* With queues there is also the default queue of the system. */
    if (found > 0) {
        q->list[2 + found] = PJ_SYSTEM_DEFAULT;
        found++;
    }

    /* The last entry starts the search for printers. */
    q->list[2 + found] = PJ_SEARCH;
    found++;

    q->list[2 + found] = NULL;
    q->nlist = 2 + found;

    return q->n;
}


int
pj_queues(PjQueues *q)
{
    return pj_queues_wait(q, LPSTAT_PANEL_MS);
}


/*
 * The search for printers.  "lpinfo -v" runs in the background and is read
 * while the panel draws a message; the answer is parsed into devices and
 * written as a text that says what to type.  Strata adds no queue itself.
 */
struct PjSearch {
    PvJob job;
    char text[16384];
    size_t len;
    long start_ms;
    long end_ms;
    int done;
    int timed_out;
    int failed; /* lpinfo ended with a status that is not 0. */
};


int
pj_search_start(PjSearch **sp, char *err, size_t cap)
{
    char lpinfo[PATH_MAX];
    char flag[] = "-v";
    char *argv[3];
    PjSearch *s;

    *sp = NULL;

    if (sp_find_env("STRATA_LPINFO", "lpinfo", lpinfo, sizeof lpinfo) != 0) {
        snprintf(err, cap, "lpinfo was not found in the system folders "
                           "(/usr/bin, /usr/sbin and the others): install "
                           "the cups package");

        return -1;
    }

    s = calloc(1, sizeof *s);

    if (s == NULL) {
        snprintf(err, cap, "Out of memory");

        return -1;
    }

    argv[0] = lpinfo;
    argv[1] = flag;
    argv[2] = NULL;

    if (pv_job_start(&s->job, argv, tmp_dir()) != 0) {
        free(s);
        snprintf(err, cap, "lpinfo could not be started");

        return -1;
    }

    s->start_ms = now_ms();
    *sp = s;

    return 0;
}


int
pj_search_poll(PjSearch *s)
{
    long got;

    if (s->done != 0) {
        return 1;
    }

    while (s->len < sizeof s->text - 1 &&
           (got = pv_job_read(&s->job, s->text + s->len,
                              sizeof s->text - 1 - s->len)) > 0) {
        s->len += lsz(got);
    }

    if (pv_job_done(&s->job) != 0) {
        while (s->len < sizeof s->text - 1 &&
               (got = pv_job_read(&s->job, s->text + s->len,
                                  sizeof s->text - 1 - s->len)) > 0) {
            s->len += lsz(got);
        }

        s->failed = !WIFEXITED(s->job.status) ||
                    WEXITSTATUS(s->job.status) != 0;
        s->done = 1;
    } else if (now_ms() - s->start_ms >= PJ_SEARCH_MS) {
        s->timed_out = 1;
        s->done = 1;
    }

    if (s->done != 0) {
        pv_job_kill(&s->job);
        s->text[s->len] = '\0';
        s->end_ms = now_ms();
    }

    return s->done;
}


long
pj_search_elapsed(const PjSearch *s)
{
    return (s->done != 0 ? s->end_ms : now_ms()) - s->start_ms;
}


void
pj_search_end(PjSearch *s)
{
    if (s == NULL) {
        return;
    }

    if (s->done == 0) {
        pv_job_kill(&s->job);
    }

    free(s);
}


/* The value of one hexadecimal digit, or -1. */
static int
hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}


/* Decode %XX in a part of a URI (lpinfo writes spaces as %20). */
static void
uri_decode(const char *in, size_t n, char *out, size_t cap)
{
    size_t o = 0;

    out[0] = '\0';

    for (size_t i = 0; i < n && in[i] != '\0' && o + 2 < cap; i++) {
        int hi = i + 2 < n ? hexval(in[i + 1]) : -1;
        int lo = i + 2 < n ? hexval(in[i + 2]) : -1;

        if (in[i] == '%' && hi >= 0 && lo >= 0) {
            o += lsz(snprintf(out + o, cap - o, "%c", hi * 16 + lo));
            i += 2;
        } else {
            out[o++] = in[i];
            out[o] = '\0';
        }
    }
}


/* A queue name from a text: letters, digits and _ only, at most 63. */
static void
queue_name_of(const char *text, char *out, size_t cap)
{
    size_t o = 0;
    int last_us = 1;

    for (size_t i = 0; text[i] != '\0' && o + 1 < cap && o < 63; i++) {
        char c = text[i];
        int keep = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                   (c >= 'A' && c <= 'Z');

        if (keep != 0) {
            out[o++] = c;
            last_us = 0;
        } else if (last_us == 0) {
            out[o++] = '_';
            last_us = 1;
        }
    }

    while (o > 0 && out[o - 1] == '_') {
        o--;
    }

    out[o] = '\0';

    if (o == 0) {
        str_copy(out, cap, "printer");
    }
}


/* One line of "lpinfo -v": "network socket://192.168.1.201:9100". */
static int
parse_device(const char *line, PjDevice *d)
{
    const char *sp = strchr(line, ' ');
    const char *uri;
    const char *rest;
    char scheme[16];
    size_t cl;
    size_t sl;
    const char *colon;
    char text[160];

    if (sp == NULL) {
        return 0;
    }

    cl = lsz(sp - line);

    if (!((cl == 7 && strncmp(line, "network", 7) == 0) ||
          (cl == 6 && strncmp(line, "direct", 6) == 0))) {
        return 0;
    }

    uri = sp + 1;

    while (*uri == ' ') {
        uri++;
    }

    colon = strstr(uri, "://");

    if (colon == NULL || colon == uri) {
        return 0;
    }

    memset(d, 0, sizeof *d);
    memcpy(d->cls, line, cl);
    d->cls[cl] = '\0';
    str_copy(d->uri, sizeof d->uri, uri);

    for (size_t i = strlen(d->uri); i > 0 && (d->uri[i - 1] == ' ' ||
                                              d->uri[i - 1] == '\r'); i--) {
        d->uri[i - 1] = '\0';
    }

    sl = lsz(colon - uri);

    if (sl >= sizeof scheme) {
        return 0;
    }

    memcpy(scheme, uri, sl);
    scheme[sl] = '\0';
    rest = colon + 3;
    d->driverless = strcmp(scheme, "ipp") == 0 || strcmp(scheme, "ipps") == 0 ||
                    strcmp(scheme, "dnssd") == 0;

    if (strcmp(scheme, "dnssd") == 0) {
        const char *us = strstr(rest, "._");
        size_t n = us != NULL ? lsz(us - rest) : strcspn(rest, "/?");

        uri_decode(rest, n, text, sizeof text);
        str_copy(d->model, sizeof d->model, text);

        {
            char *at = strstr(text, " @ ");

            if (at != NULL) {
                *at = '\0';
            }
        }

        queue_name_of(text, d->name, sizeof d->name);
    } else if (strcmp(scheme, "usb") == 0) {
        size_t n = strcspn(rest, "?");

        uri_decode(rest, n, text, sizeof text);

        for (char *p = text; *p != '\0'; p++) {
            if (*p == '/') {
                *p = ' ';
            }
        }

        str_copy(d->model, sizeof d->model, text);
        queue_name_of(text, d->name, sizeof d->name);
    } else {
        char host[96];
        size_t n = strcspn(rest, "/:?");

        if (n >= sizeof host) {
            n = sizeof host - 1;
        }

        memcpy(host, rest, n);
        host[n] = '\0';
        snprintf(text, sizeof text, "printer %s", host);
        queue_name_of(text, d->name, sizeof d->name);
    }

    return 1;
}


int
pj_parse_devices(const char *text, PjDevice *dev, int max)
{
    int n = 0;
    char line[512];

    for (const char *p = text; *p != '\0' && n < max;) {
        const char *e = strchr(p, '\n');
        size_t len = e != NULL ? lsz(e - p) : strlen(p);

        if (len >= sizeof line) {
            len = sizeof line - 1;
        }

        memcpy(line, p, len);
        line[len] = '\0';

        if (parse_device(line, &dev[n]) != 0) {
            n++;
        }

        p = e != NULL ? e + 1 : p + len;
    }

    return n;
}


int
pj_search_report(const PjSearch *s, char *out, size_t cap)
{
    PjDevice dev[PJ_MAXDEV];
    int n = pj_parse_devices(s->text, dev, PJ_MAXDEV);
    size_t o = 0;

    out[0] = '\0';

    if (n == 0 && s->failed != 0) {
        char msg[400];

        str_copy(msg, sizeof msg, s->text[0] != '\0' ? s->text :
                                                      "(no message)");
        snprintf(out, cap, "The search for printers failed.  lpinfo reported:\n"
                           "\n%s\n\nThe search needs the lpadmin group or "
                           "a running CUPS (sudo systemctl status cups).",
                 msg);

        return 0;
    }

    if (n == 0) {
        snprintf(out, cap, "%s\n\nThe printer must be switched on and be on "
                           "the same network as this computer.  Choose the "
                           "entry again to search again.",
                 s->timed_out != 0 ? "No printer was found in 10 seconds." :
                                     "No printer was found.");

        return 0;
    }

    o += (size_t)snprintf(out + o, cap - o, "Printers found: %d.  Strata "
                          "does not add a queue itself: type the command "
                          "of the printer in a terminal.\n", n);

    for (int i = 0; i < n && o + 1 < cap; i++) {
        const PjDevice *d = &dev[i];

        o += (size_t)snprintf(out + o, cap - o, "\n%d. %s: %s\n", i + 1,
                              d->cls, d->uri);

        if (d->model[0] != '\0' && o + 1 < cap) {
            o += (size_t)snprintf(out + o, cap - o, "   %s\n", d->model);
        }

        if (o + 1 >= cap) {
            break;
        }

        o += (size_t)snprintf(out + o, cap - o,
                              "   sudo /usr/sbin/lpadmin -p %s -E \\\n"
                              "        -v %s \\\n"
                              "        -m %s\n", d->name, d->uri,
                              d->driverless != 0 ? "everywhere" : "DRIVER");

        if (d->driverless == 0 && o + 1 < cap) {
            o += (size_t)snprintf(out + o, cap - o,
                                  "   Find DRIVER with: /usr/sbin/lpinfo -m "
                                  "| grep -i MODEL\n");
        }
    }

    if (s->timed_out != 0 && o + 1 < cap) {
        snprintf(out + o, cap - o, "\nThe search was stopped after 10 "
                                   "seconds: more printers may exist.\n");
    }

    return n;
}


int
pj_default_queue(char *out, size_t cap)
{
    char text[1024];
    const char *key = "system default destination:";
    const char *at;
    size_t n = 0;

    out[0] = '\0';

    pj_printers_prefetch();

    slot_wait(SLOT_DEFAULT, LPSTAT_PANEL_MS);

    if (lp_slot[SLOT_DEFAULT].ok == 0) {
        return 0;
    }

    str_copy(text, sizeof text, lp_slot[SLOT_DEFAULT].text);
    at = strstr(text, key);

    if (at == NULL) {
        return 0;
    }

    at += strlen(key);

    while (*at == ' ' || *at == '\t') {
        at++;
    }

    while (at[n] != '\0' && at[n] != '\n' && at[n] != ' ' && at[n] != '\t' &&
           n < cap - 1) {
        out[n] = at[n];
        n++;
    }

    out[n] = '\0';

    return valid_queue(out) != 0;
}


int
pj_pdf_dir_check(const char *setting, char *dir, size_t dcap, char *why,
                 size_t wcap)
{
    struct stat st;

    dir[0] = '\0';
    why[0] = '\0';

    if (setting[0] == '\0') {
        return 0;
    }

    if (setting[0] == '~' && (setting[1] == '/' || setting[1] == '\0')) {
        const char *home = getenv("HOME");

        if (home == NULL || home[0] != '/') {
            snprintf(why, wcap, "The PDF Directory starts with ~/, but the "
                                "home directory of the user is not known");

            return 1;
        }

        snprintf(dir, dcap, "%s%s", home, setting + 1);
    } else if (setting[0] == '/') {
        str_copy(dir, dcap, setting);
    } else {
        snprintf(why, wcap, "The PDF Directory is a full path or starts "
                            "with ~/, not a relative path");

        return 1;
    }

    if (stat(dir, &st) != 0 || S_ISDIR(st.st_mode) == 0) {
        snprintf(why, wcap, "The PDF Directory does not exist");

        return 2;
    }

    if (access(dir, W_OK | X_OK) != 0) {
        snprintf(why, wcap, "The PDF Directory cannot be written");

        return 3;
    }

    return 0;
}


void
pj_default_out_in(const char *docpath, int draft, const char *pdf_dir,
                  char *out, size_t cap, char *note, size_t ncap)
{
    char dir[PATH_MAX];
    char full[PATH_MAX];
    char why[160];
    const char *slash;
    int r;

    note[0] = '\0';
    pj_default_out(docpath, draft, out, cap);

    if (out[0] == '\0' || pdf_dir == NULL || pdf_dir[0] == '\0') {
        return;
    }

    r = pj_pdf_dir_check(pdf_dir, dir, sizeof dir, why, sizeof why);

    if (r != 0) {
        /* One line of the message area: short. */
        snprintf(note, ncap, "The PDF Directory %s: the document's directory "
                             "is used",
                 r == 1 ? "is a relative path" :
                 r == 2 ? "does not exist" :
                          "cannot be written");

        return;
    }

    slash = strrchr(out, '/');
    path_join(full, sizeof full, dir, slash != NULL ? slash + 1 : out);
    str_copy(out, cap, full);
}


/* The directory of a path, with "." for none. */
static void
dir_of(const char *path, char *out, size_t cap)
{
    const char *sl = strrchr(path, '/');

    if (sl == NULL) {
        str_copy(out, cap, ".");
    } else if (sl == path) {
        str_copy(out, cap, "/");
    } else {
        size_t n = lsz(sl - path);

        n = n < cap - 1 ? n : cap - 1;
        memcpy(out, path, n);
        out[n] = '\0';
    }
}


/* Write the part of the document as a .DOC file, for strprint. */
static int
write_part(Doc *part, const char *path)
{
    size_t len;
    uint8_t *buf = doc_serialize(part, &len);
    FILE *f = fopen(path, "wb");
    int ok;

    if (f == NULL) {
        free(buf);

        return -1;
    }

    ok = fwrite(buf, 1, len, f) == len;
    ok = fclose(f) == 0 && ok != 0;
    free(buf);

    return ok != 0 ? 0 : -1;
}


/*
 * Put the finished PDF in its place: a copy is made in the directory of the
 * file under a temporary name and renamed, so that the old file is kept
 * until the new one is whole.  Returns 0, or -1 with errno set.
 */
static int
put_pdf(const char *src, const char *dst)
{
    char dir[PATH_MAX];
    char tmp[PATH_MAX + 32];
    char buf[65536];
    int in = open(src, O_RDONLY);
    int out;
    mode_t mask;
    ssize_t got;
    int saved;

    if (in < 0) {
        return -1;
    }

    dir_of(dst, dir, sizeof dir);
    snprintf(tmp, sizeof tmp, "%s/.strprint.XXXXXX", dir);
    out = mkstemp(tmp);

    if (out < 0) {
        saved = errno;
        close(in);
        errno = saved;

        return -1;
    }

    while ((got = read(in, buf, sizeof buf)) > 0) {
        ssize_t off = 0;

        while (off < got) {
            ssize_t w = write(out, buf + off, lsz(got - off));

            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }

                saved = errno;
                close(in);
                close(out);
                unlink(tmp);
                errno = saved;

                return -1;
            }

            off += w;
        }
    }

    saved = errno;
    close(in);
    mask = umask(0);
    umask(mask);

    if (got < 0 || fchmod(out, 0666 & ~mask) != 0 || close(out) != 0 ||
        rename(tmp, dst) != 0) {
        if (got < 0) {
            errno = saved;
        } else {
            saved = errno;
        }

        unlink(tmp);

        return -1;
    }

    return 0;
}


/* The full path of a file name: the name itself when it has one. */
static void
full_path(const char *name, char *out, size_t cap)
{
    char cwd[PATH_MAX];

    if (name[0] == '/' || getcwd(cwd, sizeof cwd) == NULL) {
        str_copy(out, cap, name);
    } else {
        path_join(out, cap, cwd, name);
    }
}


void
pj_full_path(const char *name, char *out, size_t cap)
{
    full_path(name, out, cap);
}


/* The text for a failure of strprint. */
static void
strprint_failure(const PvProg *p, int status, char *out, size_t cap)
{
    if (p->err1[0] != '\0') {
        snprintf(out, cap, "LaTeX failed: %s %s", p->err1, p->err2);
    } else if (p->note[0] != '\0') {
        snprintf(out, cap, "Print failed: %s", p->note);
    } else {
        snprintf(out, cap, "Print failed (strprint ended with status %d)",
                 status);
    }
}


/* Remove trailing white space and turn the lines into one. */
static void
flatten(char *s)
{
    size_t n = strlen(s);

    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                     s[n - 1] == ' ')) {
        s[--n] = '\0';
    }

    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\n' || s[i] == '\r') {
            s[i] = ' ';
        }
    }
}


typedef struct {
    char dir[1024];
    PvJob job;
} Work;


static void
work_end(Work *w)
{
    pv_sig_set(0, 0);
    pv_job_kill(&w->job);

    if (w->dir[0] != '\0') {
        pv_rmtree(w->dir);
        w->dir[0] = '\0';
    }

    pv_sig_release();
}


static void
fail(PjResult *res, int status, const char *title, const char *text)
{
    res->status = status;
    str_copy(res->title, sizeof res->title, title);
    str_copy(res->msg, sizeof res->msg, text);
}


/* The text of a failure that names a file or another text. */
static void
fail_with(PjResult *res, int status, const char *before, const char *what,
          const char *after)
{
    char m[1024];

    snprintf(m, sizeof m, "%s%.600s%s", before, what, after);
    fail(res, status, "Print", m);
}


/*
 * What a file is as a PDF viewer: 1 for a regular file that the user may run,
 * that the user or root owns, that is not writable by its group or by others
 * and whose directory is not writable by others (unless the sticky bit is
 * set); -1 for a program that is not safe to start; 0 for no program.
 */
static int
viewer_state(const char *path)
{
    return sp_state(path);
}


/*
 * The text of a setting that may be a program: only letters, digits and
 * . _ + - (and / in a full path), a full path starts with a slash and has no
 * parent directory component in it, and nothing starts with a minus.
 */
static int
viewer_form_ok(const char *s)
{
    const char *p;

    /* A full path: it starts with a slash, holds no space and no character
       of a shell, and no .. component; a bare name is never searched. */
    if (s[0] != '/' || strlen(s) >= PATH_MAX) {
        return 0;
    }

    for (p = s; *p != '\0'; p++) {
        int c = u8(*p);

        if (isalnum(c) == 0 && c != '.' && c != '_' && c != '+' &&
            c != '-' && c != '/') {
            return 0;
        }
    }

    for (p = s; *p != '\0'; p++) {
        if ((p == s || p[-1] == '/') && p[0] == '.' && p[1] == '.' &&
            (p[2] == '/' || p[2] == '\0')) {
            return 0;
        }
    }

    return 1;
}


/*
 * Find a program: a full path is used as it is, a name is looked for in the
 * entries of PATH that are full paths and not empty.  The directory of the
 * Editor is not looked at: the programs that show a PDF are those of the
 * system.  *unsafe is set when a program was found that is not safe.
 */
/*
 * The directories of the system in which a viewer is looked for when the
 * setting is empty: never the environment variable PATH and never the
 * current directory.
 */
static const char *const viewer_system_dirs[] = {"/usr/bin", "/usr/local/bin",
                                                 "/bin", NULL};
static const char *const *viewer_dirs = viewer_system_dirs;


void
pj_viewer_dirs_set(const char *const *dirs)
{
    viewer_dirs = dirs != NULL ? dirs : viewer_system_dirs;
}


static int
viewer_find(const char *name, char *out, size_t cap, int *unsafe)
{
    const char *const *dirs = viewer_dirs;
    int st;

    if (strchr(name, '/') != NULL) {
        st = viewer_state(name);

        if (st > 0) {
            str_copy(out, cap, name);

            return 0;
        }

        *unsafe = *unsafe || st < 0;

        return -1;
    }

    for (int i = 0; dirs[i] != NULL; i++) {
        char cand[PATH_MAX];

        path_join(cand, sizeof cand, dirs[i], name);
        st = viewer_state(cand);

        if (st > 0) {
            str_copy(out, cap, cand);

            return 0;
        }

        *unsafe = *unsafe || st < 0;
    }

    return -1;
}


/* Whether there is a display that a program can open a window on. */
static int
have_display(void)
{
    const char *x = getenv("DISPLAY");
    const char *w = getenv("WAYLAND_DISPLAY");

    return (x != NULL && x[0] != '\0') || (w != NULL && w[0] != '\0');
}


/*
 * Start a program that is not waited for: its own session, no terminal, no
 * output.  Returns 0 when it started, and -1 when it could not be started
 * (the pipe that the second child writes an error to is closed by a program
 * that starts).
 */
static int
start_detached(char *const argv[])
{
    int fds[2];
    pid_t pid;
    int status = 0;
    int err = 0;
    ssize_t got;

    if (pipe(fds) != 0) {
        return -1;
    }

    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    pid = fork();

    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);

        return -1;
    }

    if (pid == 0) {
        pid_t second = fork();

        if (second == 0) {
            int null = open("/dev/null", O_RDWR);
            int e;
            ssize_t w;

            setsid();

            if (null >= 0) {
                dup2(null, 0);
                dup2(null, 1);
                dup2(null, 2);

                if (null > 2) {
                    close(null);
                }
            }

            /* The program is started by its full path, with the environment
               of the run but for the variables that load another library
               into it; it is not searched for and no shell starts it. */
            unsetenv("LD_PRELOAD");
            unsetenv("LD_AUDIT");
            execv(argv[0], argv);
            e = errno;
            w = write(fds[1], &e, sizeof e);
            _exit(w < 0 ? 126 : 127);
        }

        _exit(second < 0 ? 1 : 0);
    }

    close(fds[1]);
    waitpid(pid, &status, 0);
    got = read(fds[0], &err, sizeof err);
    close(fds[0]);

    return got > 0 ? -1 : 0;
}


/*
 * The programs that show a PDF file, in the order in which they are looked
 * for in the fixed system folders.  Each of them takes the file name as its only argument.  A
 * browser is not in the list, and no program that edits a document.
 */
static const char *const viewers[] = {"evince", "okular", "atril", "zathura",
                                      "qpdfview", "mupdf", "xpdf",
                                      "epdfview", NULL};

/* The words in the name of a program that is a word processor. */
static const char *const processors[] = {"soffice", "libreoffice", "oowriter",
                                         "oodraw", "lowriter", "lodraw",
                                         "abiword", "wps", "calligra",
                                         "winword", NULL};


/* Whether text holds word, without regard to the case of the letters. */
static int
holds_word(const char *text, const char *word)
{
    size_t n = strlen(word);

    for (; *text != '\0'; text++) {
        if (strncasecmp(text, word, n) == 0) {
            return 1;
        }
    }

    return 0;
}


int
pj_viewer_refused(const char *setting)
{
    const char *slash = strrchr(setting, '/');
    const char *base = slash != NULL ? slash + 1 : setting;

    for (int i = 0; processors[i] != NULL; i++) {
        if (holds_word(base, processors[i]) != 0) {
            return 1;
        }
    }

    return 0;
}


/* The checks of a program that is known by its path or by a name of the
   default list; the name of a setting must be a full path (see below). */
static int
viewer_check_found(const char *setting, char *found, size_t fcap, char *why,
                   size_t wcap)
{
    int unsafe = 0;

    found[0] = '\0';

    if (pj_viewer_refused(setting) != 0) {
        snprintf(why, wcap, "The PDF viewer must be a program that shows PDF "
                            "files, not a word processor");

        return PJ_VIEWER_PROCESSOR;
    }

    if (viewer_find(setting, found, fcap, &unsafe) != 0) {
        found[0] = '\0';

        if (unsafe != 0) {
            snprintf(why, wcap, "The PDF viewer can be changed by other "
                                "users (its file or its directory is "
                                "writable by others, or it belongs to "
                                "another user), so it is not started");

            return PJ_VIEWER_UNSAFE;
        }

        snprintf(why, wcap, "The PDF viewer is not an executable file that "
                            "you can run");

        return PJ_VIEWER_NOT_FOUND;
    }

    why[0] = '\0';

    return PJ_VIEWER_OK;
}


int
pj_viewer_check(const char *setting, char *found, size_t fcap, char *why,
                size_t wcap)
{
    found[0] = '\0';

    if (viewer_form_ok(setting) == 0) {
        snprintf(why, wcap, "The PDF viewer must be given with its full "
                            "path, such as /usr/bin/evince (letters, digits "
                            "and . _ + - only, no space, no ..)");

        return PJ_VIEWER_BAD_FORM;
    }

    return viewer_check_found(setting, found, fcap, why, wcap);
}


int
pj_viewer_default(char *found, size_t cap)
{
    char why[256];

    for (int i = 0; viewers[i] != NULL; i++) {
        if (viewer_check_found(viewers[i], found, cap, why, sizeof why) ==
            PJ_VIEWER_OK) {
            return 1;
        }
    }

    found[0] = '\0';

    return 0;
}


int
pj_open_pdf(const PjOpts *o, const char *pdf, char *why, size_t cap)
{
    static const char none[] = "no PDF viewer was found: set one in Setup";
    PjArgv a;
    char prog[PATH_MAX];

    if (pdf[0] != '/') {
        snprintf(why, cap, "the PDF was not given with its full path");

        return 0;
    }

    pj_argv_init(&a);

    if (o->viewer[0] != '\0' && o->viewer[0] != '/') {
        /* A name without a path (an older settings file) is never run and
           never searched for: the setting is treated as not given. */
        log_warn("print: the setting pdf_viewer (%.100s) is not used: it is "
                 "not a full path", o->viewer);
    }

    if (o->viewer[0] == '/') {
        /* The program that the user named, if it passes the check: a word
           processor and a program that others could have changed are
           treated as if no program was found. */
        char reason[256];
        int r = pj_viewer_check(o->viewer, prog, sizeof prog, reason,
                                sizeof reason);

        if (r == PJ_VIEWER_PROCESSOR || r == PJ_VIEWER_BAD_FORM ||
            r == PJ_VIEWER_UNSAFE) {
            log_warn("print: the setting pdf_viewer (%.100s) is not used: "
                     "%s", o->viewer, reason);
            snprintf(why, cap, "%s", none);

            return 0;
        }

        if (r != PJ_VIEWER_OK) {
            snprintf(why, cap, "the program of the setting pdf_viewer was "
                               "not found");

            return 0;
        }

        pj_argv_add(&a, prog);
    } else {
        /* A viewer needs a display to open its window on. */
        if (pj_viewer_default(prog, sizeof prog) == 0 ||
            have_display() == 0) {
            snprintf(why, cap, "%s", none);

            return 0;
        }

        pj_argv_add(&a, prog);
    }

    pj_argv_add(&a, pdf);

    if (start_detached(a.v) != 0) {
        snprintf(why, cap, "the program that opens it could not be started");

        return 0;
    }

    return 1;
}

/* The working directory of the panels, given to strprint (pictures). */
static char pj_work_dir[PATH_MAX];


void
pj_set_workdir(const char *dir)
{
    str_copy(pj_work_dir, sizeof pj_work_dir, dir != NULL ? dir : "");
}


/* Send the PDF with lp and wait for the answer.  Returns the status. */
static int
send_pdf(const PjOpts *o, const char *lpprog, const char *pdf, int pages,
         PjTick tick, void *ctx, PvProg *prog, Work *w, PjResult *res,
         int kept)
{
    PjArgv a;
    char err[256];
    char text[2048];
    size_t len = 0;
    int waited = 0;
    int status;
    int first = 1;

    if (pj_lp_argv(o, lpprog, pdf, pages, &a, err, sizeof err) != 0) {
        char m[1024];

        if (kept != 0) {
            snprintf(m, sizeof m, "The PDF %.400s was made but was not "
                     "sent.  %s", pdf, err);
        } else {
            snprintf(m, sizeof m, "Nothing was sent to %.100s.  %s  No PDF "
                     "was kept: choose File only to keep one.", o->printer,
                     err);
        }

        fail(res, PJ_NOTSENT, "Print", m);

        return res->status;
    }

    if (pv_job_start(&w->job, a.v, w->dir) != 0) {
        if (kept != 0) {
            fail_with(res, PJ_NOTSENT, "The PDF ", pdf,
                      " was made but lp could not be started.");
        } else {
            fail(res, PJ_NOTSENT, "Print", "lp could not be started, so "
                 "nothing was sent.  No PDF was kept: choose File only to "
                 "keep one.");
        }

        return res->status;
    }

    pv_sig_set(w->job.pid, 0);
    prog->stage = PV_VIEWER;

    for (;;) {
        long got;
        int done;

        if (pv_sig_caught() != 0) {
            res->status = PJ_CANCELLED;
            str_copy(res->msg, sizeof res->msg, "Print stopped by a signal");

            return PJ_CANCELLED;
        }

        while ((got = pv_job_read(&w->job, text + len,
                                  sizeof text - 1 - len)) > 0) {
            len += lsz(got);
        }

        done = pv_job_done(&w->job);

        if (done != 0) {
            while ((got = pv_job_read(&w->job, text + len,
                                      sizeof text - 1 - len)) > 0) {
                len += lsz(got);
            }

            break;
        }

        if (tick(ctx, PJ_SEND, prog, first) != 0) {
            res->status = PJ_CANCELLED;
            str_copy(res->title, sizeof res->title, "Print");
            if (kept != 0) {
                snprintf(res->msg, sizeof res->msg,
                         "Cancelled while sending to %s.  The PDF %s was "
                         "made; the print job may or may not have been "
                         "queued.", o->printer, pdf);
            } else {
                snprintf(res->msg, sizeof res->msg,
                         "Cancelled while sending to %s.  The print job may "
                         "or may not have been queued; no PDF was kept.",
                         o->printer);
            }

            return PJ_CANCELLED;
        }

        first = 0;
        waited += 50;

        if (waited >= LP_TIMEOUT_MS) {
            pv_job_kill(&w->job);
            if (kept != 0) {
                fail_with(res, PJ_NOTSENT, "The PDF ", pdf,
                          " was made but lp did not answer within a "
                          "minute.");
            } else {
                fail(res, PJ_NOTSENT, "Print", "lp did not answer within a "
                     "minute.  No PDF was kept: choose File only to keep "
                     "one.");
            }

            return res->status;
        }
    }

    text[len] = '\0';
    flatten(text);
    status = WIFEXITED(w->job.status) ? WEXITSTATUS(w->job.status) : -1;

    if (status != 0) {
        res->status = PJ_NOTSENT;
        str_copy(res->title, sizeof res->title, "Print");
        if (kept != 0) {
            snprintf(res->msg, sizeof res->msg,
                     "lp failed (status %d): %s  The PDF %s was made.",
                     status, text[0] != '\0' ? text : "(no message)", pdf);
        } else {
            snprintf(res->msg, sizeof res->msg,
                     "lp failed (status %d): %s  Nothing was sent and no "
                     "PDF was kept: choose File only to keep one.", status,
                     text[0] != '\0' ? text : "(no message)");
        }

        return PJ_NOTSENT;
    }

    res->status = PJ_DONE;
    res->sent = 1;
    str_copy(res->title, sizeof res->title, "Print");

    if (kept == 0) {
        char what[160];

        snprintf(what, sizeof what, "%d cop%s%s%s", o->copies,
                 o->copies == 1 ? "y" : "ies",
                 o->sides == PJ_TWO ? ", double sided" :
                 o->sides == PJ_ONE ? ", one sided" : "",
                 o->sort == PJ_SORT_YES ? ", sorted" :
                 o->sort == PJ_SORT_NO ? ", not sorted" : "");
        snprintf(res->msg, sizeof res->msg,
                 "Sent to %s: %d page%s, %s.  No PDF was kept", o->printer,
                 res->pages, res->pages == 1 ? "" : "s", what);

        return PJ_DONE;
    }

    if (res->pages > 0) {
        snprintf(res->msg, sizeof res->msg,
                 "Wrote %s (%d page%s) and sent it to %s (%s)", pdf,
                 res->pages, res->pages == 1 ? "" : "s", o->printer,
                 text[0] != '\0' ? text : "no answer from lp");
    } else {
        snprintf(res->msg, sizeof res->msg, "Wrote %s and sent it to %s (%s)",
                 pdf, o->printer, text[0] != '\0' ? text : "no answer from lp");
    }

    return PJ_DONE;
}


int
pj_run(const PjOpts *o, Doc *part, const char *docdir, int estimate,
       PjTick tick, void *ctx, PjResult *res)
{
    Work w;
    PvProg prog;
    char strprint[PATH_MAX];
    char pdflatex[PATH_MAX];
    char lpprog[PATH_MAX];
    char partdoc[1100];
    char tmppdf[1100];
    char ddir[PATH_MAX];
    char out[PATH_MAX];
    char srcpath[PATH_MAX];
    char err[256];
    char buf[4096];
    char *argv[32 + VARS_ARGC];
    char errfile[1200];
    char a_srcpath[] = "--source-path";
    char a_errors[] = "--errors";
    char a_o[] = "-o";
    char a_progress[] = "--progress";
    char a_docdir[] = "--docdir";
    char a_workdir[] = "--workingdir";
    char a_draft[] = "--draft";
    char a_double[] = "--double-spaced";
    char a_bw[] = "--black-and-white";
    char a_index[] = "--index";
    char a_lfig[] = "--list-figures";
    char a_lpic[] = "--list-pictures";
    char a_ltab[] = "--list-tables";
    char a_noindex[] = "--no-index";
    char a_nolfig[] = "--no-list-figures";
    char a_nolpic[] = "--no-list-pictures";
    char a_noltab[] = "--no-list-tables";
    char a_contents[] = "--contents";
    char a_level[16];
    char a_nolog[] = "--no-error-log";
    char a_nobanner[] = "--no-banner";
    int n = 0;
    int have_lp = 0;
    int qonly = 0;
    int status;
    int first = 1;

    memset(res, 0, sizeof *res);
    memset(&w, 0, sizeof w);
    w.job.fd = -1;

    if (pj_printer_wanted(o) != 0) {
        have_lp = pj_find_lp(lpprog, sizeof lpprog) == 0;
    }

    /* A print to a queue that has its lp makes no file of its own. */
    qonly = have_lp != 0 && o->keep_pdf == 0;

    if (pj_check(o, err, sizeof err) != 0 ||
        (qonly == 0 && pj_check_out(o->out, err, sizeof err) != 0)) {
        fail(res, PJ_FAILED, "Print", err);

        return res->status;
    }

    if (pv_find_prog("STRATA_STRPRINT", "strprint", strprint,
                     sizeof strprint) != 0) {
        fail(res, PJ_FAILED, "Print",
             "Print needs strprint: it was not found next to stred or in "
             "the system folders.");

        return res->status;
    }

    if (sp_find("pdflatex", pdflatex, sizeof pdflatex, NULL) != 0) {
        char tried[512];
        char m[800];

        sp_tried(tried, sizeof tried);
        snprintf(m, sizeof m, "Print needs LaTeX: pdflatex was not found in "
                              "%s.  See README.txt for the packages.", tried);
        fail(res, PJ_FAILED, "Print", m);

        return res->status;
    }

    full_path(o->out, out, sizeof out);
    str_copy(ddir, sizeof ddir, docdir);

    if (o->source_path[0] != '\0') {
        full_path(o->source_path, srcpath, sizeof srcpath);
    } else {
        srcpath[0] = '\0';
    }

    if (pv_mkdtemp(w.dir, sizeof w.dir) != 0) {
        fail_with(res, PJ_FAILED, "Cannot make a temporary directory: ",
                  strerror(errno), "");

        return res->status;
    }

    pv_sig_hook();
    path_join(partdoc, sizeof partdoc, w.dir, "part.doc");
    path_join(tmppdf, sizeof tmppdf, w.dir, "print.pdf");
    pv_prog_init(&prog, estimate);

    if (write_part(part, partdoc) != 0) {
        fail_with(res, PJ_FAILED, "Cannot write in ", w.dir, "");
        work_end(&w);

        return res->status;
    }

    argv[n++] = strprint;
    argv[n++] = partdoc;
    argv[n++] = a_o;
    argv[n++] = tmppdf;
    argv[n++] = a_progress;
    argv[n++] = a_docdir;
    argv[n++] = ddir;

    if (pj_work_dir[0] != '\0') {
        argv[n++] = a_workdir;
        argv[n++] = pj_work_dir;
    }

    argv[n++] = a_srcpath;
    argv[n++] = srcpath;

    if (o->draft != 0) {
        argv[n++] = a_draft;
    }

    if (o->double_spaced != 0) {
        argv[n++] = a_double;
    }

    if (o->black_white != 0) {
        argv[n++] = a_bw;
    }

    if (o->index != PJ_AUTO) {
        argv[n++] = o->index != 0 ? a_index : a_noindex;
    }

    if (o->list_figures != PJ_AUTO) {
        argv[n++] = o->list_figures != 0 ? a_lfig : a_nolfig;
    }

    if (o->list_pictures != PJ_AUTO) {
        argv[n++] = o->list_pictures != 0 ? a_lpic : a_nolpic;
    }

    if (o->list_tables != PJ_AUTO) {
        argv[n++] = o->list_tables != 0 ? a_ltab : a_noltab;
    }

    if (o->contents != PJ_AUTO) {
        snprintf(a_level, sizeof a_level, "%d", o->contents);
        argv[n++] = a_contents;
        argv[n++] = a_level;
    }

    if (o->error_log == 0) {
        argv[n++] = a_nolog;
    }

    if (o->banner == 0) {
        argv[n++] = a_nobanner;
    }

    /* The faults come back in a file: the caller's, or one of our own. */
    if (o->errors_file != NULL && o->errors_file[0] != '\0') {
        str_copy(errfile, sizeof errfile, o->errors_file);
    } else {
        path_join(errfile, sizeof errfile, w.dir, "errors.txt");
    }

    argv[n++] = a_errors;
    argv[n++] = errfile;

    n = vars_args(vars_process(), argv, n, 21 + VARS_ARGC);
    argv[n] = NULL;

    if (pv_job_start(&w.job, argv, w.dir) != 0) {
        fail_with(res, PJ_FAILED, "Cannot start strprint: ", strerror(errno),
                  "");
        work_end(&w);

        return res->status;
    }

    pv_sig_set(w.job.pid, 0);

    for (;;) {
        int changed = 0;
        long got;

        if (pv_sig_caught() != 0) {
            res->status = PJ_CANCELLED;
            str_copy(res->msg, sizeof res->msg, "Print stopped by a signal");
            work_end(&w);

            return PJ_CANCELLED;
        }

        while ((got = pv_job_read(&w.job, buf, sizeof buf)) > 0) {
            changed |= pv_prog_feed(&prog, buf, usz(isz(lsz(got))));
        }

        if (pv_job_done(&w.job) != 0) {
            while ((got = pv_job_read(&w.job, buf, sizeof buf)) > 0) {
                pv_prog_feed(&prog, buf, usz(isz(lsz(got))));
            }

            break;
        }

        if (tick(ctx, PJ_TYPESET, &prog, changed != 0 || first != 0) != 0) {
            res->status = PJ_CANCELLED;
            str_copy(res->title, sizeof res->title, "Print");
            str_copy(res->msg, sizeof res->msg, PJ_CANCEL_MSG);
            work_end(&w);

            return res->status;
        }

        first = 0;
    }

    status = WIFEXITED(w.job.status) ? WEXITSTATUS(w.job.status) : -1;

    /* The diagnostics are told whether or not a PDF was made. */
    memcpy(res->diag, prog.diag, usz(prog.ndiag) * sizeof res->diag[0]);
    res->ndiag = prog.ndiag;
    res->ndiag_dropped = prog.ndiag_dropped;
    res->faults_known = status == 0 || status == 3 || status == 4;

    {
        DiagList fl;
        int complete = 0;
        int fstatus = -1;

        diag_list_init(&fl);

        if (diag_file_read(errfile, &fl, &complete, &fstatus) == 0 &&
            complete != 0) {
            int cnt = fl.n < DIAG_MAX ? fl.n : DIAG_MAX;

            if (cnt > 0) {
                memcpy(res->diag, fl.v, usz(cnt) * sizeof res->diag[0]);
            }

            res->ndiag = cnt;
            res->ndiag_dropped = fl.dropped;
            res->faults_known = fstatus == 0 || fstatus == 3 || fstatus == 4;
        }

        diag_list_free(&fl);
    }

    /* Status 4 and 3 mean that the PDF was made, with diagnostics. */
    if ((status != 0 && status != 3 && status != 4) ||
        access(tmppdf, R_OK) != 0) {
        res->status = PJ_FAILED;
        str_copy(res->title, sizeof res->title, "Print");
        strprint_failure(&prog, status, res->msg, sizeof res->msg);
        work_end(&w);

        return res->status;
    }

    res->pages = prog.pages > 0 ? prog.pages : prog.known;

    if (qonly == 0 && put_pdf(tmppdf, out) != 0) {
        fail_with(res, PJ_FAILED, "Cannot write the PDF: ", strerror(errno),
                  "");
        work_end(&w);

        return res->status;
    }

    res->kept = qonly == 0;
    res->status = PJ_DONE;
    str_copy(res->title, sizeof res->title, "Print");

    if (qonly != 0) {
        res->msg[0] = '\0';
    } else if (res->pages > 0) {
        snprintf(res->msg, sizeof res->msg, "Wrote %s (%d page%s)", out,
                 res->pages, res->pages == 1 ? "" : "s");
    } else {
        snprintf(res->msg, sizeof res->msg, "Wrote %s", out);
    }

    if (pj_open_wanted(o) != 0) {
        char why[160];
        size_t at = strlen(res->msg);

        if (pj_open_pdf(o, out, why, sizeof why) != 0) {
            res->opened = 1;
            snprintf(res->msg + at, sizeof res->msg - at,
                     " and opened it to print");
        } else {
            /* The PDF is kept and the print is not a failure. */
            snprintf(res->msg + at, sizeof res->msg - at,
                     " but %s: it is saved", why);
        }
    }

    if (pj_printer_wanted(o) != 0) {
        if (have_lp == 0) {
            res->status = PJ_NOTSENT;
            snprintf(res->msg, sizeof res->msg,
                     "Wrote %s but lp was not found, so it was not sent to "
                     "%s.  Install cups-client.", out,
                     o->printer);
        } else {
            PjOpts sent = *o;
            const char *dp = doc_opt_get(part, "DP");
            int pw = 0;
            int ph = 0;

            sent.envelope = dp != NULL && sscanf(dp, "%*d,%d,%d", &pw,
                                                 &ph) == 2 &&
                            doc_envelope_points(pw, ph) > 0;
            send_pdf(&sent, lpprog, qonly != 0 ? tmppdf : out, res->pages, tick,
                     ctx, &prog, &w, res, qonly == 0);
        }
    }

    if (res->ndiag > 0 && (res->status == PJ_DONE ||
                           res->status == PJ_NOTSENT)) {
        size_t at = strlen(res->msg);
        int errors = 0;
        int warnings = 0;

        for (int i = 0; i < res->ndiag; i++) {
            if (res->diag[i].kind == DIAG_ERROR) {
                errors++;
            } else {
                warnings++;
            }
        }

        snprintf(res->msg + at, sizeof res->msg - at,
                 ".  %s%d error%s and %d warning%s.",
                 errors > 0 ? "" : "The document has ", errors,
                 errors == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s");

        if (errors > 0) {
            static const char head[] = "PRINT FAILED: ";
            size_t hn = sizeof head - 1;
            size_t mn = strlen(res->msg);

            if (mn + hn < sizeof res->msg) {
                memmove(res->msg + hn, res->msg, mn + 1);
                memcpy(res->msg, head, hn);
            }
        }
    }

    work_end(&w);

    return res->status;
}


/* Where the lines of the Printer about the check of an export begin. */
#define CHECK_TAG "@@strprint check "


/*
 * The text after "@@strprint check WORD" in what the Printer said, up to the
 * end of the line, copied to out; the result is out, or NULL when the
 * Printer said no such line.
 */
static const char *
check_line(const char *said, const char *word, char *out, size_t cap)
{
    char tag[64];
    const char *p;
    size_t n;

    snprintf(tag, sizeof tag, "%s%s", CHECK_TAG, word);
    p = strstr(said, tag);

    if (p == NULL) {
        return NULL;
    }

    p += strlen(tag);

    while (*p == ' ') {
        p++;
    }

    n = strcspn(p, "\n");

    if (n >= cap) {
        n = cap - 1;
    }

    memcpy(out, p, n);
    out[n] = '\0';

    return out;
}


/* The diagnostics of the error file of the Printer, when it is complete. */
static void
read_export_diags(const char *errfile, PjResult *res)
{
    DiagList fl;
    int complete = 0;
    int fstatus = -1;

    diag_list_init(&fl);

    if (diag_file_read(errfile, &fl, &complete, &fstatus) == 0 &&
        complete != 0) {
        int cnt = fl.n < DIAG_MAX ? fl.n : DIAG_MAX;

        if (cnt > 0) {
            memcpy(res->diag, fl.v, usz(cnt) * sizeof res->diag[0]);
        }

        res->ndiag = cnt;
        res->ndiag_dropped = fl.dropped;
        res->faults_known = 1;
    }

    diag_list_free(&fl);
}


/*
 * The note for the message about the check: what the Printer said about it,
 * or nothing when no check was asked for.
 */
static void
note_text(const char *said, int check, char *note, size_t cap)
{
    char word[400];

    note[0] = '\0';

    if (check == 0) {
        return;
    }

    if (check_line(said, "ok", word, sizeof word) != NULL) {
        str_copy(note, cap, "LaTeX accepts it");
    } else if (check_line(said, "skipped", word, sizeof word) != NULL) {
        snprintf(note, cap, "not checked: %s", word);
    } else if (check_line(said, "errors", word, sizeof word) != NULL) {
        str_copy(note, cap, "LaTeX does not accept it");
    }
}


/*
 * The message of an export that was made: "Exported FILE", how many errors
 * and warnings the document has, and a note about the check (NULL when there
 * was none).
 */
static void
export_message(PjResult *res, const char *out, const char *note)
{
    size_t at;

    snprintf(res->msg, sizeof res->msg, "Exported %s", out);
    at = strlen(res->msg);

    if (res->ndiag > 0) {
        int errors = 0;
        int warnings = 0;

        for (int i = 0; i < res->ndiag; i++) {
            if (res->diag[i].kind == DIAG_ERROR) {
                errors++;
            } else {
                warnings++;
            }
        }

        if (errors > 0) {
            snprintf(res->msg + at, sizeof res->msg - at,
                     " with %d error%s and %d warning%s", errors,
                     errors == 1 ? "" : "s", warnings,
                     warnings == 1 ? "" : "s");
        } else {
            snprintf(res->msg + at, sizeof res->msg - at,
                     " with %d warning%s", warnings,
                     warnings == 1 ? "" : "s");
        }

        at = strlen(res->msg);
    }

    if (note != NULL) {
        snprintf(res->msg + at, sizeof res->msg - at, "%s%s",
                 res->ndiag > 0 ? "; " : ", ", note);
    }
}


int
pj_export_latex(const PjExport *x, Doc *part, const char *docdir,
                PjTick tick, void *ctx, PjResult *res)
{
    Work w;
    PvProg prog;
    char strprint[PATH_MAX];
    char partdoc[1100];
    char ddir[PATH_MAX];
    char out[PATH_MAX];
    char source[PATH_MAX];
    char srcpath[PATH_MAX];
    char errfile[1200];
    char said[1024];
    char buf[1024];
    char *argv[20 + VARS_ARGC];
    char a_srcpath[] = "--source-path";
    char a_export[] = "--export-latex";
    char a_docdir[] = "--docdir";
    char a_workdir[] = "--workingdir";
    char a_source[] = "--source-name";
    char a_pictures[] = "--pictures";
    char a_keep[] = "keep";
    char a_copy[] = "copy";
    char a_positions[] = "--position-comments";
    char a_check[] = "--check-latex";
    char a_errors[] = "--errors";
    char note[480];
    size_t nsaid = 0;
    int n = 0;
    int status;
    int first = 1;
    int checking = 0;

    memset(res, 0, sizeof *res);
    memset(&w, 0, sizeof w);
    w.job.fd = -1;
    said[0] = '\0';

    if (x->out[0] == '\0') {
        fail(res, PJ_FAILED, "Export", "There is no file name");

        return res->status;
    }

    if (pv_find_prog("STRATA_STRPRINT", "strprint", strprint,
                     sizeof strprint) != 0) {
        fail(res, PJ_FAILED, "Export",
             "Export needs strprint: it was not found next to stred or in "
             "the system folders.");

        return res->status;
    }

    full_path(x->out, out, sizeof out);

    if (x->source_path[0] != '\0') {
        full_path(x->source_path, srcpath, sizeof srcpath);
    } else {
        srcpath[0] = '\0';
    }

    str_copy(ddir, sizeof ddir, docdir);
    str_copy(source, sizeof source, x->source);

    if (pv_mkdtemp(w.dir, sizeof w.dir) != 0) {
        fail_with(res, PJ_FAILED, "Cannot make a temporary directory: ",
                  strerror(errno), "");
        str_copy(res->title, sizeof res->title, "Export");

        return res->status;
    }

    pv_sig_hook();
    path_join(partdoc, sizeof partdoc, w.dir, "part.doc");
    path_join(errfile, sizeof errfile, w.dir, "errors.txt");
    pv_prog_init(&prog, 0);

    if (write_part(part, partdoc) != 0) {
        fail_with(res, PJ_FAILED, "Cannot write in ", w.dir, "");
        str_copy(res->title, sizeof res->title, "Export");
        work_end(&w);

        return res->status;
    }

    argv[n++] = strprint;
    argv[n++] = partdoc;
    argv[n++] = a_export;
    argv[n++] = out;
    argv[n++] = a_docdir;
    argv[n++] = ddir;

    if (pj_work_dir[0] != '\0') {
        argv[n++] = a_workdir;
        argv[n++] = pj_work_dir;
    }

    argv[n++] = a_srcpath;
    argv[n++] = srcpath;

    if (source[0] != '\0') {
        argv[n++] = a_source;
        argv[n++] = source;
    }

    argv[n++] = a_pictures;
    argv[n++] = x->keep_pictures != 0 ? a_keep : a_copy;

    if (x->positions != 0) {
        argv[n++] = a_positions;
    }

    if (x->check != 0) {
        argv[n++] = a_check;
    }

    argv[n++] = a_errors;
    argv[n++] = errfile;
    n = vars_args(vars_process(), argv, n, 19 + VARS_ARGC);
    argv[n] = NULL;

    if (pv_job_start(&w.job, argv, w.dir) != 0) {
        fail_with(res, PJ_FAILED, "Cannot start strprint: ", strerror(errno),
                  "");
        str_copy(res->title, sizeof res->title, "Export");
        work_end(&w);

        return res->status;
    }

    pv_sig_set(w.job.pid, 0);

    for (;;) {
        long got;
        int changed = 0;

        if (pv_sig_caught() != 0) {
            res->status = PJ_CANCELLED;
            str_copy(res->msg, sizeof res->msg, "Export stopped by a signal");
            work_end(&w);

            return PJ_CANCELLED;
        }

        while ((got = pv_job_read(&w.job, buf, sizeof buf)) > 0) {
            size_t take = usz(isz(lsz(got)));

            changed = 1;

            if (nsaid + take >= sizeof said) {
                take = sizeof said - 1 - nsaid;
            }

            memcpy(said + nsaid, buf, take);
            nsaid += take;
            said[nsaid] = '\0';
        }

        if (pv_job_done(&w.job) != 0) {
            break;
        }

        /* The file is made when the Printer starts the check. */
        if (x->check != 0 && checking == 0 &&
            check_line(said, "started", note, sizeof note) != NULL) {
            checking = 1;
            changed = 1;
        }

        if (tick != NULL) {
            if (tick(ctx, PJ_TYPESET, &prog, changed != 0 || first != 0) !=
                0) {
                str_copy(res->title, sizeof res->title, "Export");

                if (checking != 0) {
                    /* The check is stopped; the file stays. */
                    read_export_diags(errfile, res);
                    res->status = PJ_DONE;
                    export_message(res, out, "the check was stopped");
                    work_end(&w);

                    return res->status;
                }

                res->status = PJ_CANCELLED;
                str_copy(res->msg, sizeof res->msg, "Export cancelled");
                work_end(&w);

                return res->status;
            }
        } else {
            usleep(20000);
        }

        first = 0;
    }

    while (pv_job_read(&w.job, buf, sizeof buf) > 0) {
        /* The rest of the output is of no use. */
    }

    status = WIFEXITED(w.job.status) ? WEXITSTATUS(w.job.status) : -1;
    str_copy(res->title, sizeof res->title, "Export");

    if (status != 0 && status != 4) {
        DiagList fl;
        int complete = 0;
        int fstatus = -1;

        /* The reason is the first diagnostic of the error file. */
        diag_list_init(&fl);

        if (diag_file_read(errfile, &fl, &complete, &fstatus) == 0 &&
            complete != 0 && fl.n > 0) {
            str_copy(said, sizeof said, fl.v[0].msg);
        } else {
            flatten(said);
        }

        diag_list_free(&fl);
        snprintf(res->msg, sizeof res->msg, "Export failed: %s",
                 said[0] != '\0' ? said : "the Printer stopped without "
                                          "saying why");
        res->status = PJ_FAILED;
        work_end(&w);

        return res->status;
    }

    read_export_diags(errfile, res);
    res->status = PJ_DONE;
    note_text(said, x->check, note, sizeof note);
    export_message(res, out, note[0] != '\0' ? note : NULL);

    work_end(&w);

    return res->status;
}
