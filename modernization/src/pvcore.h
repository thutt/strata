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
 * pvcore.h - the parts of the LaTeX preview that need no screen: reading
 * the progress of latex, estimating and correcting the number of pages, the
 * private temporary directory, finding the programs and running strprint as
 * a child that is read through a pipe.  preview.c draws the panel and runs
 * the viewer; see docs/PRINT-LATEX.txt.
 */
#ifndef PVCORE_H
#define PVCORE_H
#include "diag.h"
#include <stddef.h>
#include <sys/types.h>

/* The stages of a run, in the order in which they happen. */
typedef enum {
    PV_WRITING, /* The editor and strprint write the files. */
    PV_LATEX, /* latex is running (the pass is in PvProg.pass). */
    PV_VIEWER /* The DVI file is done, the viewer starts. */
} PvStage;

#define PV_LINE 1600 /* The longest line of the output that is looked at. */
#define PV_MSG 256

/*
 * The state of the reader of the output of strprint and latex.  Give the
 * bytes to pv_prog_feed in any pieces; a page mark that is cut between two
 * pieces is found all the same.
 */
typedef struct {
    PvStage stage;
    int pass; /* 1 for the first run of latex, 0 before it. */
    int pages; /* The pages shipped in this pass. */
    int prior; /* The estimate from the document, before latex ran. */
    int pos; /* The last position mark in thousandths, or -1. */
    int pos_pages; /* The pages shipped when that mark was printed. */
    int known; /* The pages of the last pass that ended, or 0. */
    char err1[PV_MSG]; /* The first error line of latex, or empty. */
    char err2[PV_MSG]; /* The line after it. */
    char note[PV_MSG]; /* The last line "strprint: ..." of stderr. */

    /*
     * The diagnostics that strprint told with "@@strprint diag" lines, in
     * the order of the document, and how many more it said it dropped.
     */
    Diag diag[DIAG_MAX];
    int ndiag;
    int ndiag_dropped;

    /* The state of the scanner. */
    char line[PV_LINE]; /* The line that is being read, for "@@" lines. */
    int linelen;
    int at_start; /* 1 when the next byte begins a line. */
    int skip; /* 1 in the text of a TeX warning or error. */
    int mark_state; /* 0 not in a page mark, 1 after "[", 2 in the digits. */
    int mark_at_start; /* 1 when the "[" of the mark began a line. */
    long mark_val; /* The number in the page mark that is being read. */
    long last_val; /* The number of the last page mark that was counted. */
} PvProg;


void
pv_prog_init(PvProg *p, int prior);


/*
 * Read n bytes of output.  Returns 1 when something that the panel shows
 * has changed (the stage, the pass, a page, a mark), else 0.
 */
int
pv_prog_feed(PvProg *p, const char *buf, size_t n);


/*
 * The number of pages to show as the total and whether it is only an
 * estimate (*about is 1).  It is the pages of the last pass that ended when
 * there is one; else the estimate from the document, corrected by the
 * position marks; never less than the pages already shipped.
 */
int
pv_prog_total(const PvProg *p, int *about);


/*
 * The corrected estimate: the estimate from the document, the pages shipped
 * and the position in the document (thousandths) at the last mark.
 */
int
pv_refine(int prior, int pages, int pos);


/* The gauge in percent, from 0 to 100, of the progress. */
int
pv_prog_percent(const PvProg *p);


/*
 * A private temporary directory (mode 0700) under $TMPDIR, or /tmp, whose
 * name begins with "strpv.".  Returns 0 and the path, or -1.
 */
int
pv_mkdtemp(char *path, size_t cap);


/*
 * Delete a directory and everything in it, without following links.  Only
 * a path that has a name that pv_mkdtemp makes is deleted; returns 0 when
 * the directory is gone, else -1.
 */
int
pv_rmtree(const char *path);


/*
 * Find a program: the file the environment variable names (when envname is
 * not NULL and the variable is set), else the directory of the running
 * program, else the directories of PATH.  Returns 0 and the path, or -1.
 */
int
pv_find_prog(const char *envname, const char *name, char *out, size_t cap);


/* The directory of the running program (empty when it is not known). */
void
pv_self_dir(char *out, size_t cap);


typedef struct {
    pid_t pid; /* 0 when no child runs. */
    int fd; /* The read end of the pipe, or -1. */
    int status; /* The wait status, when the child has ended. */
} PvJob;


/*
 * Start a program in a process group of its own, with the temporary
 * directory in TMPDIR, and its standard output and error in a pipe.  The
 * pipe is not blocking.  Returns 0 or -1.
 */
int
pv_job_start(PvJob *j, char *const argv[], const char *tmpdir);


/*
 * Read what the child has written: the number of bytes, 0 at the end of
 * the output, and -1 when there is nothing yet.
 */
long
pv_job_read(PvJob *j, char *buf, size_t cap);


/* 1 when the child has ended (its status is in j->status), else 0. */
int
pv_job_done(PvJob *j);


/* Stop the child and everything it started, and wait for it. */
void
pv_job_kill(PvJob *j);


/* Close the pipe of a job. */
void
pv_job_close(PvJob *j);


/*
 * Stop the program cleanly while a job runs.  pv_sig_hook takes SIGINT,
 * SIGTERM and SIGHUP: the handler kills the process groups that pv_sig_set
 * names at once and notes the signal.  The loops of the caller look at
 * pv_sig_caught (0 or the signal) about every 50 ms, clean up and call
 * pv_sig_release, which puts the old handlers back and raises the signal
 * again so that the program ends as it would have.
 */
void
pv_sig_hook(void);


void
pv_sig_set(pid_t job, pid_t viewer);


int
pv_sig_caught(void);


void
pv_sig_release(void);
#endif
