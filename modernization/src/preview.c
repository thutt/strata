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

/* preview.c - F12, Print, Preview through LaTeX and the DVI viewer. */
#include "util.h"
#include "preview.h"
#include "printjob.h"
#include "latex.h"
#include "perr.h"
#include "pvcore.h"
#include "term.h"
#include "ui.h"
#include "filemenu.h"
#include "widgets.h"
#include "log.h"
#include "vars.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define PANEL_W 50

typedef struct {
    char dir[1024]; /* The temporary directory, or empty. */
    PvJob job;
    pid_t viewer;
} Run;


/*
 * Stop the child programs, delete the files and the directory, put the
 * signal handlers back.  When a signal stopped the preview, the signal is
 * raised again, so that the program ends as it would have.
 */
static void
finish(Run *r)
{
    pv_sig_set(0, 0);
    pv_job_kill(&r->job);

    if (r->viewer > 0) {
        int st;

        kill(-r->viewer, SIGKILL);
        waitpid(r->viewer, &st, 0);
        r->viewer = 0;
    }

    if (r->dir[0] != '\0') {
        pv_rmtree(r->dir);
        r->dir[0] = '\0';
    }

    pv_sig_release();
}


/*
 * Draw the panel with the stage, the page and the gauge.  title is the
 * title of the box and last the text of the stage after LaTeX; extra, when
 * it is not NULL, replaces the hint on the last line.
 */
void
preview_panel(const char *title, const char *last, const PvProg *p,
              const char *extra)
{
    char line[128];
    int cols;
    int rows;
    int w;
    int x;
    int y;
    int gw;
    int fill;
    int pc = pv_prog_percent(p);

    w_base();
    cols = term_cols();
    rows = term_rows();
    w = PANEL_W < cols - 2 ? PANEL_W : cols - 2;
    x = (cols - w) / 2;
    y = rows / 2 - 4;
    box(x, y, w, 9, title);

    if (p->stage == PV_WRITING) {
        snprintf(line, sizeof line, "Writing");
    } else if (p->stage == PV_LATEX) {
        snprintf(line, sizeof line, "Running LaTeX (pass %d)", p->pass);
    } else {
        snprintf(line, sizeof line, "%s", last);
    }

    term_puts(x + 3, y + 2, line, TA_BOLD);

    if (p->stage == PV_LATEX) {
        int about;
        int total = pv_prog_total(p, &about);

        snprintf(line, sizeof line, about != 0 ? "Page %d of about %d" :
                                                 "Page %d of %d",
                 p->pages, total);
        term_puts(x + 3, y + 3, line, 0);

        if (p->pass > 1) {
            term_puts(x + 3, y + 7, "Another pass, for the references.",
                      TA_DIM);
        }
    }

    gw = w > 16 ? w - 12 : 4;
    fill = gw * pc / 100;
    term_put(x + 3, y + 5, '[', 0);

    for (int i = 0; i < gw; i++) {
        term_put(x + 4 + i, y + 5, i < fill ? 0xDB : 0xB0, 0);
    }

    term_put(x + 4 + gw, y + 5, ']', 0);
    snprintf(line, sizeof line, "%3d%%", pc);
    term_puts(x + 6 + gw, y + 5, line, 0);

    if (extra != NULL) {
        term_puts(x + 3, y + 7, extra, TA_DIM);
    } else if (p->pass <= 1) {
        term_puts(x + 3, y + 7, "Esc cancels", TA_DIM);
    }

    term_cursor(0, 0, 0);
    term_present();
}


static void
draw_panel(const PvProg *p, const char *extra)
{
    preview_panel("Preview", "Starting viewer", p, extra);
}


/* The directory of the document, or "." for a document with no name. */
void
preview_docdir(const Editor *e, char *out, size_t cap)
{
    const char *path = e->doc->path;
    const char *sl = path != NULL ? strrchr(path, '/') : NULL;

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
 * What the viewer told the Editor in the report file: the number of things
 * that it could not show (note) and of the failures (fail), and the text of
 * the first line.  The viewer never writes to the console, so this is the
 * way that the user learns of them.
 */
typedef struct {
    int notes;
    int fails;
    char first[160];
} ViewerReport;


/* The words that end a message of the viewer when the run has a log. */
#define LOG_POINTER "; details in the log of the run"


/* Appends the first len bytes of text to line, never beyond cap bytes. */
static void
line_put(char *line, int cap, int *n, const char *text, int len)
{
    int room = cap - *n;

    if (len > room) {
        len = room;
    }

    if (len > 0) {
        memcpy(line + *n, text, (size_t) len);
        *n += len;
    }
}


/*
 * Writes the message line for a problem of the viewer: the lead (what the
 * line said before, or nothing), a semicolon, what the viewer could not do,
 * a colon and the text that the viewer gave (the middle), then, when the run
 * has a log, the pointer to it.  Without a log the line is written whole
 * and the screen clips it.  With the pointer, a line that is too long for
 * the screen loses the end of the middle first; when the middle would be very short
 * it is left out; then the lead is shortened to Preview done; and last the
 * end of what.  The pointer is never cut.
 */
static void
viewer_message(Editor *e, const char *lead, const char *what,
               const char *middle)
{
    const char *tail = log_run_active() != 0 ? LOG_POINTER : "";
    int avail = term_cols() - 1;
    int cap = isz(sizeof e->msg) - 1;
    int nl = slen(lead);
    int nw = slen(what);
    int nm = slen(middle);
    int nt = slen(tail);
    int sep = nl > 0 ? 2 : 0;
    int room;
    int n = 0;
    char line[sizeof e->msg];

    if (nt == 0 || avail > cap) {
        avail = cap;
    }

    if (avail < nt + 1) {
        avail = nt + 1;
    }

    if (nl + sep + nw + nt > avail && nl > 12) {
        nl = 12;
    }

    if (nl + sep + nw + nt > avail) {
        nw = avail - nt - nl - sep;
        nm = 0;
    }

    room = avail - nl - sep - nw - nt - 2;

    if (nm > 0 && room < 8 && nt > 0) {
        nm = 0;
    } else if (nm > 0 && room < 1) {
        nm = 0;
    }

    line_put(line, cap, &n, lead, nl);

    if (nl > 0) {
        line_put(line, cap, &n, "; ", 2);
    }

    line_put(line, cap, &n, what, nw);

    if (nm > 0) {
        line_put(line, cap, &n, ": ", 2);

        if (nm <= room) {
            line_put(line, cap, &n, middle, nm);
        } else if (nt > 0) {
            line_put(line, cap, &n, middle, room - 3);
            line_put(line, cap, &n, "...", 3);
        } else {
            line_put(line, cap, &n, middle, room);
        }
    }

    line_put(line, cap, &n, tail, nt);
    line[n] = '\0';
    ed_msg(e, "%s", line);
}


static void
read_viewer_report(const char *path, ViewerReport *vr)
{
    char line[1400];
    FILE *f = fopen(path, "r");

    memset(vr, 0, sizeof *vr);

    if (f == NULL) {
        return;
    }

    while (fgets(line, sizeof line, f) != NULL) {
        char *text = NULL;
        size_t n = strlen(line);

        if (n > 0 && line[n - 1] == '\n') {
            line[n - 1] = '\0';
        }

        if (strncmp(line, "note: ", 6) == 0) {
            vr->notes++;
            text = line + 6;
        } else if (strncmp(line, "fail: ", 6) == 0) {
            vr->fails++;
            text = line + 6;
        }

        if (text != NULL && vr->first[0] == '\0') {
            str_copy(vr->first, sizeof vr->first, text);
        }
    }

    fclose(f);
}


/*
 * Run the viewer on the file and wait until it ends.  In a terminal the
 * terminal is given back to the shell meanwhile and taken again afterwards;
 * with the X11 terminal the editor goes on serving its window.  Returns the
 * exit status of the viewer, 127 when it could not run, or -1 when a signal
 * stopped the wait.
 */
static int
run_viewer(Run *r, const char *viewer, const char *dvi, const char *report)
{
    int gui = term_is_gui();
    int status = 0;
    pid_t pid;

    if (gui == 0) {
        term_close();
        fflush(stdout);
    }

    pid = fork();

    if (pid == 0) {
        char *vargv[5 + VARS_ARGC];
        char a_view[1024];
        char a_dvi[1100];
        char a_opt[] = "--report";
        char a_report[1200];
        int n;

        setpgid(0, 0);
        str_copy(a_view, sizeof a_view, viewer);
        str_copy(a_dvi, sizeof a_dvi, dvi);
        vargv[0] = a_view;
        vargv[1] = a_dvi;
        str_copy(a_report, sizeof a_report, report);
        vargv[2] = a_opt;
        vargv[3] = a_report;

        /*
         * The variables of the run follow the report option.  The list of
         * arguments must end after them: vars_args overwrites the place
         * where a terminator was written before, so the end of the list
         * is written at the place that it returns.
         */
        n = vars_args(vars_process(), vargv, 4, 5 + VARS_ARGC);
        vargv[n] = NULL;
        execv(a_view, vargv);
        _exit(127);
    }

    if (pid > 0) {
        setpgid(pid, pid);
        r->viewer = pid;
        pv_sig_set(0, pid);

        for (;;) {
            pid_t w = waitpid(pid, &status, WNOHANG);

            if (w == pid || (w < 0 && errno != EINTR)) {
                /* Reaped: finish has nothing to wait for. */
                r->viewer = 0;
                break;
            }

            if (pv_sig_caught() != 0) {
                status = -1;

                break;
            }

            if (gui != 0) {
                term_idle();
            }

            usleep(30000);
        }

        pv_sig_set(0, 0);
    } else {
        status = 127 << 8;
    }

    if (gui == 0) {
        term_open();
        tcflush(0, TCIFLUSH);
    }

    if (status == -1) {
        return -1;
    }

    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}


/* Tell why latex failed, in the message line. */
static void
say_failure(Editor *e, const PvProg *p, int status)
{
    if (p->err1[0] != '\0') {
        ed_msg(e, "LaTeX failed: %s %s", p->err1, p->err2);
    } else if (p->note[0] != '\0') {
        ed_msg(e, "Preview failed: %s", p->note);
    } else {
        ed_msg(e, "Preview failed (strprint ended with status %d)", status);
    }
}


/*
 * Read the faults of the run: from the error file, else from the progress
 * channel.  With faults they are returned there (for the items of the
 * document, as text for a run that has no document to mark); else they
 * become Print Error markers.  Returns how many faults, or -1 when the run
 * told nothing about the document; *errors is how many of them are errors
 * (the rest are warnings).
 */
static int
take_faults(Editor *e, const char *errfile, const PvProg *prog,
            const Doc *part, DiagList *faults, int run, int *errors)
{
    DiagList l;
    int complete = 0;
    int status = -1;
    int warnings = 0;
    int n;

    diag_list_init(&l);

    if (diag_file_read(errfile, &l, &complete, &status) != 0 ||
        complete == 0) {
        diag_list_clear(&l);
        diag_list_set(&l, prog->diag, prog->ndiag, prog->ndiag_dropped);
        status = l.n > 0 ? 4 : -1;
    }

    if (status != 0 && status != 3 && status != 4) {
        diag_list_free(&l);

        return -1;
    }

    n = l.n;
    diag_counts(&l, errors, &warnings);

    if (faults != NULL) {
        diag_remap(&l, part, e->doc);
        diag_list_free(faults);
        *faults = l;

        return n;
    }

    perr_apply(e, run, l.v, l.n, part);
    diag_list_free(&l);

    return n;
}


/*
 * The words for the message line: "no print errors", or "2 print errors",
 * with "PRINT FAILED" in front when some of them are errors.
 */
static const char *
faults_summary(int n, int errors)
{
    static char text[96];

    if (n <= 0) {
        return "no print errors";
    }

    snprintf(text, sizeof text, "%s%d print error%s (Ctrl-F8 finds the next)",
             errors > 0 ? "PRINT FAILED, " : "", n, n == 1 ? "" : "s");

    return text;
}


/* 1: the pages of a failed preview carry the banner. */
static int banner_on = 1;


void
preview_set_banner(int on)
{
    banner_on = on != 0;
}


void
preview_run(Editor *e, Doc *part, int error_log, DiagList *faults)
{
    Run r;
    PvProg prog;
    char strprint[1024];
    char viewer[1024];
    char dvi[1100];
    char partdoc[1100];
    char docdir[1024];
    char workdir[PATH_MAX];
    char buf[4096];
    char *argv[20 + VARS_ARGC];
    char errfile[1200];
    char srcpath[PATH_MAX];
    char a_srcpath[] = "--source-path";
    char a_errors[] = "--errors";
    int nfaults = 0;
    int nerrors = 0;
    char a_dvi[] = "--dvi";
    char a_o[] = "-o";
    char a_progress[] = "--progress";
    char a_docdir[] = "--docdir";
    char a_workdir[] = "--workingdir";
    char a_nolog[] = "--no-error-log";
    char a_nobanner[] = "--no-banner";
    int n = 0;
    int status = 0;
    int cancelled = 0;
    int estimate;
    int viewer_status;
    char report[1200];
    ViewerReport vr;

    memset(&r, 0, sizeof r);
    r.job.fd = -1;

    if (pv_find_prog("STRATA_STRPRINT", "strprint", strprint,
                     sizeof strprint) != 0) {
        popup_msg("Preview", "Preview needs strprint: it was not found next "
                             "to stred or in the system "
                             "folders.");

        return;
    }

    if (pv_find_prog("STRATA_DVIVIEW", "dviview", viewer, sizeof viewer) !=
        0) {
        popup_msg("Preview", "Preview needs the viewer dviview: it was not "
                             "found next to stred or in the system "
                             "folders.  STRATA_DVIVIEW "
                             "names it.");

        return;
    }

    if (getenv("STRATA_DVIVIEW") == NULL) {
        const char *display = getenv("DISPLAY");

        if (display == NULL || display[0] == '\0') {
            popup_msg("Preview", "Preview needs an X display: DISPLAY is not "
                                 "set.  Run stred under X, or use the X11 "
                                 "version.");

            return;
        }
    }

    if (pv_mkdtemp(r.dir, sizeof r.dir) != 0) {
        ed_msg(e, "Preview: cannot make a temporary directory: %s",
               strerror(errno));

        return;
    }

    pv_sig_hook();
    path_join(partdoc, sizeof partdoc, r.dir, "part.doc");
    path_join(dvi, sizeof dvi, r.dir, "preview.dvi");
    preview_docdir(e, docdir, sizeof docdir);
    str_copy(workdir, sizeof workdir, filemenu_workdir(e));
    estimate = latex_estimate_pages(part);
    pv_prog_init(&prog, estimate);
    draw_panel(&prog, NULL);

    if (write_part(part, partdoc) != 0) {
        ed_msg(e, "Preview: cannot write in %s", r.dir);
        finish(&r);

        return;
    }

    argv[n++] = strprint;
    argv[n++] = partdoc;
    argv[n++] = a_dvi;
    argv[n++] = a_o;
    argv[n++] = dvi;
    argv[n++] = a_progress;
    argv[n++] = a_docdir;
    argv[n++] = docdir;
    argv[n++] = a_workdir;
    argv[n++] = workdir;

    /* The markers Filename and Pathname are those of the real document. */
    if (e->doc->path != NULL) {
        pj_full_path(e->doc->path, srcpath, sizeof srcpath);
    } else {
        srcpath[0] = '\0';
    }

    argv[n++] = a_srcpath;
    argv[n++] = srcpath;
    path_join(errfile, sizeof errfile, r.dir, "errors.txt");
    argv[n++] = a_errors;
    argv[n++] = errfile;

    if (error_log == 0) {
        argv[n++] = a_nolog;
    }

    if (banner_on == 0) {
        argv[n++] = a_nobanner;
    }

    n = vars_args(vars_process(), argv, n, 19 + VARS_ARGC);
    argv[n] = NULL;

    if (pv_job_start(&r.job, argv, r.dir) != 0) {
        ed_msg(e, "Preview: cannot start strprint: %s", strerror(errno));
        finish(&r);

        return;
    }

    pv_sig_set(r.job.pid, 0);

    for (;;) {
        int changed = 0;
        long got;
        int k;

        if (pv_sig_caught() != 0) {
            finish(&r);

            return;
        }

        while ((got = pv_job_read(&r.job, buf, sizeof buf)) > 0) {
            changed |= pv_prog_feed(&prog, buf, usz(isz(lsz(got))));
        }

        /*
         * The end: strprint has ended (and with it whatever it started).
         * What it wrote last is still in the pipe.
         */
        if (pv_job_done(&r.job) != 0) {
            while ((got = pv_job_read(&r.job, buf, sizeof buf)) > 0) {
                pv_prog_feed(&prog, buf, usz(isz(lsz(got))));
            }

            status = r.job.status;
            break;
        }

        if (changed != 0) {
            draw_panel(&prog, NULL);
        }

        k = term_getkey(50);

        if (k == K_ESC) {
            cancelled = 1;
            break;
        }

        if (k == K_RESIZE) {
            draw_panel(&prog, NULL);
        }
    }

    if (cancelled != 0) {
        ed_msg(e, "Preview cancelled");
        finish(&r);

        return;
    }

    status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    /*
     * The errors of the document come in the file that strprint wrote (the
     * lines of the progress channel are the second choice).  They are
     * kept whether or not a file was made, but a run that did not get as
     * far as the document changes nothing.
     */
    nfaults = take_faults(e, errfile, &prog, part, faults, PERR_RUN_PREVIEW,
                          &nerrors);

    /* 3: made in spite of errors of LaTeX; 4: made, with diagnostics. */
    if ((status != 0 && status != 3 && status != 4) ||
        access(dvi, R_OK) != 0) {
        log_error("stred: the preview cannot be shown: the Printer ended "
                  "with status %d and the DVI file %s is %s", status, dvi,
                  access(dvi, R_OK) == 0 ? "there" : "missing");
        say_failure(e, &prog, status);
        finish(&r);

        return;
    }

    prog.stage = PV_VIEWER;
    draw_panel(&prog, "Close the viewer to return.");
    path_join(report, sizeof report, r.dir, "viewer.txt");
    viewer_status = run_viewer(&r, viewer, dvi, report);
    read_viewer_report(report, &vr);
    log_note("stred: the viewer ended with status %d, %d things it could not "
             "show and %d failures", viewer_status, vr.notes, vr.fails);

    if (pv_sig_caught() != 0) {
        finish(&r);

        return;
    }

    if (viewer_status == 127) {
        ed_msg(e, "Preview: cannot run the viewer %s", viewer);
    } else if (viewer_status != 0 || vr.fails > 0) {
        viewer_message(e, "", "Preview: the viewer failed",
                       vr.first[0] != '\0' ? vr.first :
                       "it ended with an error");
    } else if (prog.err1[0] != '\0') {
        ed_msg(e, "PRINT FAILED: Preview shown in spite of errors of "
               "LaTeX: %s %s",
               prog.err1, prog.err2);
    } else {
        ed_msg(e, "Preview done");
    }

    finish(&r);

    if (nfaults >= 0 && strcmp(e->msg, "Preview done") == 0) {
        ed_msg(e, "Preview done: %s", faults_summary(nfaults, nerrors));
    }

    if (vr.notes > 0 && strncmp(e->msg, "Preview done", 12) == 0) {
        char lead[160];
        char what[64];

        str_copy(lead, sizeof lead, e->msg);
        snprintf(what, sizeof what, "the viewer could not show %d thing%s",
                 vr.notes, vr.notes == 1 ? "" : "s");
        viewer_message(e, lead, what, vr.first);
    }
}
