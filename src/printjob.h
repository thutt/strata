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
 * printjob.h - F12, Print, Draft and Final: typeset a part of a document to
 * a PDF with strprint and send the PDF to a CUPS queue with lp.
 *
 * Nothing here draws on the screen.  The editor (printmenu.c), the command
 * line of stred (printcli.c) and the Document Manager (str.c, which only
 * asks for the list of queues) share it.  The work runs in child processes,
 * strprint and lp, that are read through a pipe and polled, as the preview
 * does (pvcore.h); no shell is ever started and no text of the user goes
 * through system() or popen().  See docs/PRINT-MENU.txt.
 */
#ifndef PRINTJOB_H
#define PRINTJOB_H
#include "msdoc.h"
#include "pvcore.h"
#include <limits.h>
#include <stddef.h>

#define PJ_COPIES_MAX 99
#define PJ_PAGE_MAX 99999
#define PJ_QNAME 128
#define PJ_MAXQ 64
#define PJ_FILE_ONLY "File only"
/* The Printer value that makes the PDF and opens it in the PDF viewer; the
   words of older settings files and options stay readable. */
#define PJ_PDF_VIEWER "PDF viewer"
#define PJ_PDF_VIEWER_OLD "Open to print"
/* A queue is wanted and it is the default of the system: no lp -d. */
#define PJ_SYSTEM_DEFAULT "System default printer"
/*
 * The last entry of the list of a Printer field: it does not name a queue, it
 * starts a search for printers on the network (pj_search_start).
 */
#define PJ_SEARCH "Search for printers..."

/* The choices of the sides and of the order of the pages. */
enum {
    PJ_DEFAULT, /* Leave it to the printer. */
    PJ_ONE, /* One sided. */
    PJ_TWO /* Double sided, long edge. */
};

enum {
    PJ_SORT_DEFAULT, /* Leave it to the printer. */
    PJ_SORT_YES, /* Sorted: collate=true. */
    PJ_SORT_NO /* Not sorted: collate=false. */
};

/* A part of the print that the document decides (see PjOpts). */
#define PJ_AUTO (-1)

typedef struct {
    char out[PATH_MAX]; /* The PDF to write. */
    /* A queue, or empty, "File only" or "PDF viewer". */
    char printer[PJ_QNAME];
    char viewer[PATH_MAX]; /* The program that shows a PDF, or "" for any. */
    char title[200]; /* The title of the print job (lp -t), or empty. */
    int copies; /* 1 to PJ_COPIES_MAX. */
    int sides; /* PJ_DEFAULT, PJ_ONE or PJ_TWO. */
    int sort; /* PJ_SORT_DEFAULT, PJ_SORT_YES or PJ_SORT_NO. */
    int page_from; /* The first page to print, or 0 for page 1. */
    int page_to; /* The last page to print, or 0 for the last page. */
    int draft; /* 1: strprint --draft. */
    int double_spaced; /* 1: strprint --double-spaced. */
    int black_white; /* 1: strprint --black-and-white (greys, no colours). */
    /*
     * 1 (the default): the PDF is written to out.  0: when a queue is named
     * and lp is there the PDF is made in the private temporary folder, sent,
     * and deleted with the folder; no file is written and out is not used.
     * The panels of the Editor set 0, the command line leaves 1.
     */
    int keep_pdf;
    /*
     * The parts that the print makes: 1 on (strprint --index and so on),
     * 0 off (--no-index), PJ_AUTO the document's (nothing is said to
     * strprint).  contents is a level, 1 to 99, 0 for none, or PJ_AUTO.
     */
    int index;
    int list_figures;
    int list_pictures;
    int list_tables;
    int contents;
    int error_log; /* 1: the PDF ends with an Error Log (the default). */
    /*
     * 1: the page is the size of an envelope.  lp then gets the option
     * print-scaling=none, so that the system does not scale the page to the
     * printable area, which shrinks the type and moves the corner.  Set by
     * pj_run from the document; a letter or an A4 has the system default.
     */
    int envelope;
    int banner; /* 1: a failed print has the banner (the default). */
    const char *errors_file; /* strprint --errors FILE, or NULL. */
    /*
     * The file that the part is a copy of, for the markers Filename,
     * Pathname, Filedate and Filetime; empty when the document has none.
     */
    char source_path[PATH_MAX];
} PjOpts;


void
pj_opts_init(PjOpts *o);


/*
 * 1 when the options name a queue (not empty, not "File only" and not "Open
 * to print").
 */
int
pj_printer_wanted(const PjOpts *o);


/* 1 when the options say "PDF viewer": the PDF is opened, not sent. */
int
pj_open_wanted(const PjOpts *o);


/*
 * Open the PDF in a program that shows PDF files, so that the user prints it
 * from the dialog of that program.  The program is the one named by
 * o->viewer (the setting pdf_viewer) when that is not empty; else the first
 * PDF viewer that is in a fixed directory of the system (/usr/bin,
 * /usr/local/bin, /bin: never PATH) among evince, okular, atril, zathura, qpdfview, mupdf,
 * xpdf and epdfview (each takes the file name as its only argument), and
 * only when there is a display.  A program is used only when
 * pj_viewer_check accepts it: a browser is never looked for, and no program
 * that edits a document is ever started.  The program is started without a
 * shell, detached: it is not waited for, it has no terminal and its output
 * goes nowhere; the full path of the PDF is its only argument, so that the
 * file name can never be taken for an option.  Returns 1 when it was started,
 * and 0 when not, with the reason in why (a short phrase that goes after
 * "but " in the message).
 */
int
pj_open_pdf(const PjOpts *o, const char *pdf, char *why, size_t cap);


/*
 * 1 when the setting names a word processor, which must never be started to
 * show a PDF: a base name that holds soffice, libreoffice, oowriter, oodraw,
 * lowriter, lodraw, abiword, wps, calligra or winword, in any case.  This
 * is a courtesy and not a security measure: a link or a wrapper has another
 * name.
 */
int
pj_viewer_refused(const char *setting);


/* The answers of pj_viewer_check. */
enum {
    PJ_VIEWER_OK = 0, /* A program that may be started: found has its path. */
    PJ_VIEWER_BAD_FORM, /* Not a program name or a full path. */
    PJ_VIEWER_PROCESSOR, /* A word processor. */
    PJ_VIEWER_NOT_FOUND, /* No such program. */
    PJ_VIEWER_UNSAFE /* Found, but others could have changed it. */
};


/*
 * Check the value of the setting pdf_viewer and find the program.  The value
 * is the FULL PATH of a program, and nothing else: it starts with a slash,
 * holds only letters, digits and the characters . _ + - /, no space or quote
 * or other character of a shell, and no .. component; a name without a path
 * and a relative path are refused (PJ_VIEWER_BAD_FORM): PATH is never
 * searched.  The program must be a regular file that the user may run, that
 * the user or root owns, that is not writable by its group or by others, and
 * whose directory is not writable by others (the sticky bit does not make it
 * right).  Returns PJ_VIEWER_OK with the full path in found, else the reason,
 * with a sentence in why that says it in plain words (found is empty).  An
 * empty setting is not checked here: it means the default.
 */
int
pj_viewer_check(const char *setting, char *found, size_t fcap, char *why,
                size_t wcap);


/*
 * The directories that the default viewer is looked for in: the system ones
 * (/usr/bin, /usr/local/bin, /bin) unless this sets others (a list ended by
 * NULL, which must stay valid; NULL puts the system ones back).  This is for
 * the tests of the program; no setting and no environment variable reaches
 * it.
 */
void
pj_viewer_dirs_set(const char *const *dirs);


/*
 * Find the PDF viewer that is used when the setting is empty: the first of
 * evince, okular, atril, zathura, qpdfview, mupdf, xpdf and epdfview that
 * pj_viewer_check accepts.  Returns 1 and its full path in found, or 0.
 */
int
pj_viewer_default(char *found, size_t cap);


/*
 * Read the text of a Copies field: only digits, 1 to PJ_COPIES_MAX.
 * Returns 0 and the number, or -1 with a message in err.
 */
int
pj_parse_copies(const char *s, int *out, char *err, size_t cap);


/*
 * Read the text of a page field: empty (0) or only digits, 1 to
 * PJ_PAGE_MAX.  Returns 0 and the number, or -1 with a message.
 */
int
pj_parse_page(const char *s, int *out, char *err, size_t cap);


/*
 * Check every value: the copies, the name of the queue (no space, slash or
 * "#", not beginning with "-"), the pages (a range is for a printer only,
 * and the first is not after the last).  Returns 0, or -1 with a message.
 */
int
pj_check(const PjOpts *o, char *err, size_t cap);


/*
 * The name of the PDF for a document: its path with the suffix .pdf (a
 * draft: -draft.pdf, so that it does not replace the final one), in the
 * directory of the document.  A document with no name gives "" .
 */
void
pj_default_out(const char *docpath, int draft, char *out, size_t cap);


/* Check that the PDF can be written: its directory is there and open. */
int
pj_check_out(const char *out, char *err, size_t cap);


#define PJ_MAXARGV 32

typedef struct {
    char *v[PJ_MAXARGV + 1]; /* Ends with NULL. */
    char buf[PJ_MAXARGV][PATH_MAX + 8];
    int n;
} PjArgv;


/* Start an empty list of arguments, and add one argument (a copy). */
void
pj_argv_init(PjArgv *a);


void
pj_argv_add(PjArgv *a, const char *s);


/*
 * The command that sends the PDF: lp -d QUEUE -n COPIES [-o sides=...]
 * [-o collate=...] [-P FROM-TO] [-t TITLE] PDF.  lp is the program to
 * run.  pages is the number of pages of the PDF when it is known (0 when
 * not): it ends a range that has no last page, and a range that starts
 * after it is refused.  Returns 0, or -1 with a message (the options are
 * checked first).
 */
int
pj_lp_argv(const PjOpts *o, const char *lp, const char *pdf, int pages,
           PjArgv *a, char *err, size_t cap);


/* The queues of "lpstat -a", for the choice list of a Printer field. */
typedef struct {
    char name[PJ_MAXQ][PJ_QNAME];
    int off[PJ_MAXQ]; /* 1: the queue does not accept jobs now. */
    int n;
    /*
     * "File only", "PDF viewer", the queues, "System default printer"
     * (only when there are queues), "Search for printers...", NULL: for
     * PICK_LIST.
     */
    const char *list[PJ_MAXQ + 5];
    int nlist; /* The entries of list, without the NULL. */
    int pending; /* 1: lpstat had not answered yet (PJ_Q_PENDING). */
} PjQueues;


/* The answer of pj_queues when lpstat has not answered yet. */
#define PJ_Q_PENDING (-2)


/*
 * Start lpstat in the background, once per run of the program, so that the
 * answer is there when a panel needs it.  A second call does nothing, except
 * that an answer older than 30 seconds is asked for again in the background
 * (a queue that was added while the program runs shows up at the next
 * opening of a panel).  The old answer stays in use until the new one is
 * there.  A debug build reads another age, in milliseconds, from
 * STRATA_LPSTAT_REFRESH_MS; a release build never does.
 */
void
pj_printers_prefetch(void);


/*
 * Forget the answers of lpstat and stop a run that is still going, so that
 * the next ask starts lpstat again.  The tests use it between cases that
 * name another lpstat; the programs never call it.
 */
void
pj_printers_reset(void);


/*
 * Ask lpstat (STRATA_LPSTAT names it, else the fixed folders of the system)
 * for the queues.  lpstat runs once, in the background; this waits for it at
 * most 0.3 second.  Returns the number of queues, 0 when there is none, -1
 * when lpstat is not found and PJ_Q_PENDING when it has not answered yet (the
 * next call finds the answer).  q->list is always ready.
 */
int
pj_queues(PjQueues *q);


/*
 * The same, waiting at most max_ms for a run that is going (0: not at all
 * and no new run, for a panel that is open and looks again at every key).
 */
int
pj_queues_wait(PjQueues *q, long max_ms);


/*
 * The default queue of the system, from "lpstat -d" (the same tool and the
 * same waiting as pj_queues).  Returns 1 and its name in out, or 0 when the
 * system has no default, lpstat is not found or it has not answered yet.
 */
int
pj_default_queue(char *out, size_t cap);


/*
 * The search for printers: "lpinfo -v" (STRATA_LPINFO names it, else the fixed
 * folders of the system) lists the devices that CUPS can reach, the printers
 * of the network among them.  It runs in the background for at most
 * PJ_SEARCH_MS; the caller polls it and draws a message while it runs.  The
 * program never adds a queue itself: pj_search_report only says what to type.
 */
#define PJ_SEARCH_MS 10000
#define PJ_MAXDEV 32

typedef struct {
    char cls[16]; /* "network" or "direct". */
    char uri[256]; /* The device URI, as lpinfo printed it. */
    char name[96]; /* A name for the queue: letters, digits and _ only. */
    char model[128]; /* The make and model or the name, may be empty. */
    int driverless; /* 1: lpadmin can use "-m everywhere" (ipp, dnssd). */
} PjDevice;

typedef struct PjSearch PjSearch;

/* Start the search.  Returns 0, or -1 with the reason in err. */
int
pj_search_start(PjSearch **sp, char *err, size_t cap);

/* Read what lpinfo has written.  Returns 1 when the search is over. */
int
pj_search_poll(PjSearch *s);

/* Milliseconds since the search started. */
long
pj_search_elapsed(const PjSearch *s);

/*
 * The text of the result for a panel: the devices with their address and the
 * command that adds each, or that none was found, or the error text of
 * lpinfo.  Returns the number of devices (0 for none or an error).
 */
int
pj_search_report(const PjSearch *s, char *out, size_t cap);

/* Stop a search that is still going and free it. */
void
pj_search_end(PjSearch *s);

/* Parse the text of "lpinfo -v" (the tests use it).  Returns the count. */
int
pj_parse_devices(const char *text, PjDevice *dev, int max);

/*
 * Check the setting of the PDF Directory: empty (the directory of the
 * document), a full path, or a path that starts with ~/ (the home directory
 * of the user).  Returns 0 and the expanded directory in dir (empty for the
 * default) when it can be used: it exists and the user may write in it.
 * Returns 1 with a sentence in why when the text is not allowed (a relative
 * path), 2 when the directory does not exist and 3 when it cannot be
 * written; dir holds the expanded path then, except for 1.
 */
int
pj_pdf_dir_check(const char *setting, char *dir, size_t dcap, char *why,
                 size_t wcap);


/*
 * The default of the Output File: NAME.pdf (NAME-draft.pdf for a Draft)
 * for the document at docpath, in the directory of the setting pdf_dir when
 * that can be used, else in the directory of the document.  note gets a
 * sentence when the directory of the setting is not used (else it is
 * empty).
 */
void
pj_default_out_in(const char *docpath, int draft, const char *pdf_dir,
                  char *out, size_t cap, char *note, size_t ncap);


/*
 * The full path of a file name: the name itself when it begins with a slash,
 * else the name after the working directory.
 */
void
pj_full_path(const char *name, char *out, size_t cap);


/* Find lp: the file STRATA_LP names, else PATH.  Returns 0 or -1. */
int
pj_find_lp(char *out, size_t cap);


#define PJ_CANCEL_MSG "Print cancelled"

/* The ends of a run. */
enum {
    PJ_DONE, /* The PDF is made, and sent when that was asked. */
    PJ_FAILED, /* Nothing was made: the text says why. */
    PJ_NOTSENT, /* The PDF is made but lp was not found or failed. */
    PJ_CANCELLED /* Stopped by the user; no PDF was written. */
};

typedef struct {
    int status;
    int pages; /* The pages of the PDF, or 0 when not known. */
    int sent; /* 1 when lp took the PDF. */
    int opened; /* 1 when the PDF was opened ("PDF viewer"). */
    int kept; /* 1 when a PDF file was written to the Output File. */
    char title[32]; /* The title for a popup. */
    char msg[8192]; /* What was made, or the error, in plain text. */

    /*
     * The diagnostics of the document (diag.h), in the order of the
     * document, with the items of the part that was printed; see
     * diag_remap.  ndiag_dropped counts those that did not fit.
     */
    Diag diag[DIAG_MAX];
    int ndiag;
    int ndiag_dropped;
    int faults_known; /* 1: the run reached the document (faults are real). */
} PjResult;

/* The parts of a run that the tick function is told about. */
enum {
    PJ_TYPESET, /* strprint runs. */
    PJ_SEND /* lp runs. */
};


/*
 * Called about every 50 ms during a run.  changed is 1 when the state
 * that prog holds is new.  Return 1 to cancel the run.  The function must
 * wait a little (it may wait for a key) so that the loop does not spin.
 */
typedef int (*PjTick)(void *ctx, int phase, PvProg *prog, int changed);


/*
 * Typeset part to the PDF of o and send it when o names a queue.  part is
 * written, as it is in memory, to a private temporary directory; docdir is
 * where the pictures of the document are looked for.  estimate is the
 * number of pages that the gauge expects before TeX has run.  The PDF is
 * written to its place only when it is complete.  Every child is stopped,
 * the directory is deleted and the signals are handled as the preview
 * does, on every end.  Returns res->status.
 */
int
pj_run(const PjOpts *o, Doc *part, const char *docdir, int estimate,
       PjTick tick, void *ctx, PjResult *res);


/* What an export of a LaTeX file needs besides the part of the document. */
typedef struct {
    char out[PATH_MAX]; /* The .tex file to write. */
    char source[PATH_MAX]; /* The name of the document, for the header. */
    /* The path of the document for the markers Filename and Pathname. */
    char source_path[PATH_MAX];
    int keep_pictures; /* 1: name the pictures where they are. */
    int positions; /* 1: keep the position comments. */
    int check; /* 1: run the TeX engine on the file and report its errors. */
} PjExport;


/*
 * Write part as the LaTeX file of x->out: the file that Print and Preview
 * typeset, made to be kept (the Printer option --export-latex does the
 * work, in a child process).  The pictures are copied into the folder that
 * has the name of the file and _files.  docdir is where the pictures of the
 * document are looked for.  The file is written whole or not at all.  The
 * diagnostics of the document are returned as for a print; they do not
 * stop the export.  tick may be NULL.  Returns res->status: PJ_DONE (the
 * message shows what was made and how many errors the document has),
 * PJ_FAILED or PJ_CANCELLED.
 *
 * With x->check the engine of a print is also run, once, on the file, after
 * it is written: every error of TeX is a diagnostic of the document, and
 * the message shows that LaTeX accepts the file, or that it was not checked
 * and why.  The file stays whatever the check finds.  tick is told about
 * the phase PJ_TYPESET while the check runs; when a stop is requested then, the
 * file is already there and the result is PJ_DONE, with a message that shows
 * that the check was stopped.
 */
int
pj_export_latex(const PjExport *x, Doc *part, const char *docdir,
                PjTick tick, void *ctx, PjResult *res);


/*
 * Tell the print and the export where the working directory of the panels
 * is (NULL for none): a picture whose name is not a full path is looked for
 * there after the directory of the document (strprint --workingdir).
 */
void
pj_set_workdir(const char *dir);
#endif
