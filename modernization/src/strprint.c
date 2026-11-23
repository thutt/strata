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
 * Print a Strata document through LaTeX to a PDF with strprint.
 *
 *     strprint FILE.DOC [-o OUT] [--dvi] [--pdf-via-dvi] [--tex-only]
 *              [--keep] [--draft] [--double-spaced] [--black-and-white]
 *              [--recover]
 *              [--contents LEVEL] [--index] [--list-figures]
 *              [--list-pictures] [--list-tables]
 *              [--no-index] [--no-list-figures] [--no-list-pictures]
 *              [--no-list-tables]
 *              [--long-list]
 *              [--progress] [--no-error-log] [--no-banner]
 *              [--docdir DIR] [--var NAME VALUE] [--special NAME VALUE]
 *              [--source-path PATH]
 *
 * The program is separate from the editor: it links the document, the
 * markers and their evaluation, and the LaTeX writer, and nothing of the
 * user interface.  The .tex file is written in a private temporary
 * directory and pdflatex is run on it (or latex for the DVI routes, and
 * dvipdfmx for --pdf-via-dvi), twice when that is requested.  An
 * equation or a picture that TeX rejects is printed in the long form of
 * its marker (see latex.h) and the run goes on, so that a document always
 * gives a PDF.  The exit status is 0 only on success and the messages
 * go to stderr, so that the editor can start the program as a child.  What
 * is wrong with the document (an equation that is not TeX, a missing
 * picture, a character that no font has, each error of TeX) is collected
 * with its position in the document (diag.h), told on stderr, sent as
 * "@@strprint diag" lines with --progress and printed at the end of the
 * document as an Error Log, unless --no-error-log is given.  The status is
 * then 4: made, with diagnostics (3 keeps its old meaning, see below).
 * When there are errors (not only warnings) the print has FAILED: the
 * output still is made, but every page carries a banner "PRINT FAILED" in
 * the top margin (--no-banner leaves it out), each diagnostic that has a
 * place is marked there, and a DVI tells the Viewer with a special.  The
 * Error Log option does not change that.
 *
 * With --progress the program is made to be read through a pipe by the
 * editor's preview (see docs/PRINT-LATEX.txt): the output of latex goes to
 * the standard output unchanged, the program adds lines that begin with
 * "@@strprint " (the stage, the pass, the first error), the .tex file holds
 * a position mark before each block, and a DVI file that latex made in
 * spite of errors is kept (exit status 3).  --docdir names the directory
 * where the pictures of the document are looked for, when the document
 * has been copied; --workingdir names the working directory of the panels,
 * where a picture is looked for next (docs/PICTURES-PATHS.txt).  --source-path names the file that the document is a copy
 * of: the markers Filename, Pathname, Filedate and Filetime then show the name,
 * the path and the time of that file; an empty path says that the document
 * has no file yet.
 *
 * An export (--export-latex, --tex-only) with --check-latex also runs the
 * engine of a print, once, on the exported file (made again in a private
 * directory with the position comments, so that an error of TeX is told
 * with its place in the document) and says how it went with the lines
 * "@@strprint check started", "check ok", "check errors N" and "check
 * skipped REASON" on the standard output.  The file that was exported is
 * never changed by the check.
 */
#include "util.h"
#include "latex.h"
#include "diag.h"
#include "msdoc.h"
#include "marker.h"
#include "log.h"
#include "vars.h"
#include "config.h"
#include "readfile.h"
#include "sysprog.h"
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAXDIS 2000
#define MAXTRY 400
#define MAXTEXERR 100

/* What TeX said about each piece that was given up (the order of dis). */
static char dis_text[MAXDIS][96];
static const char *dis_msg[MAXDIS];

typedef struct {
    const char *doc;
    const char *out;
    int tex_only;
    int keep;
    int draft;
    int double_spaced;
    int black_white; /* 1: the colours of the print are greys. */
    int recover; /* 1: a damaged document: print the part that can be read */
    int index; /* 1: the index at the end; 0: none; LATEX_AUTO: see below. */
    int list_figures; /* 1: the list of figures (likewise). */
    int list_pictures; /* 1: the list of pictures (likewise). */
    int list_tables; /* 1: the list of tables (likewise). */
    /* The deepest level of the table of contents, 0 for none, or
       LATEX_AUTO: the document says (YCL; the settings of the lists and the
       counts of its markers say which lists). */
    int contents;
    int list;
    int dvi; /* Keep the DVI file of latex. */
    int via_dvi; /* The PDF is made by latex and dvipdfmx. */
    int progress; /* Report to the editor through the standard output. */
    int error_log; /* 1: the document ends with an Error Log (the default). */
    int banner; /* 1: a failed print has the banner on every page. */
    const char *docdir; /* Where the pictures are, or NULL. */
    const char *workdir; /* The working directory of the panels, or NULL. */
    const char *errors; /* The file for the error records, or NULL. */
    const char *export_latex; /* The .tex file to write (export), or NULL. */
    const char *source; /* The name of the document for the header, or NULL. */
    const char *source_path; /* The file that the document is a copy of. */
    int keep_pictures; /* 1: an exported file names the pictures in place. */
    int positions; /* 1: an exported file keeps the position comments. */
    int check; /* 1: an export is checked by a run of the TeX engine. */
    int latex_markers; /* 0 from the settings, 1 allowed, 2 not allowed. */
} Args;

/* 1 while the output of a TeX engine goes to the standard output. */
static int forward_output;


static void
usage(void)
{
    fputs("usage: strprint FILE.DOC [-o OUT] [--dvi | --pdf-via-dvi] "
          "[--tex-only | --export-latex FILE.tex] [--pictures copy | keep] "
          "[--position-comments] [--check-latex] [--source-name NAME] "
          "[--source-path PATH] [--keep] [--draft] "
          "[--double-spaced] [--black-and-white] [--recover] "
          "[--contents LEVEL] [--index | --no-index] "
          "[--list-figures | --no-list-figures] "
          "[--list-pictures | --no-list-pictures] "
          "[--list-tables | --no-list-tables] [--long-list] "
          "[--progress] [--docdir DIR] [--workingdir DIR] "
          "[--no-error-log] [--no-banner] [--errors FILE] "
          "[--latex-markers | --no-latex-markers]\n",
          stderr);
}


/* A log or a text of the engine is never this large: refuse a larger file. */
#define ENGINE_FILE_MAX (64u * 1024u * 1024u)


/* The whole file in memory (NUL terminated), or NULL. */
static char *
read_file(const char *path, size_t *len)
{
    return readfile_text(path, ENGINE_FILE_MAX, len, NULL, 0);
}


/* Copy n bytes of src into dst (capacity cap), always NUL terminated. */
static void
copy_n(char *dst, size_t cap, const char *src, size_t n)
{
    if (cap == 0) {
        return;
    }

    n = n < cap - 1 ? n : cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}


/*
 * Run a program of the TeX system in dir on the file name and return its
 * exit status, or -1.  A TeX engine (pdflatex, latex) is started without
 * shell escape and with the options of nonstopmode; halt says that it stops
 * at the first error.  dvipdfmx gets the DVI file and writes doc.pdf.
 */
static int
run_prog(const char *dir, const char *prog, char *name, int halt)
{
    pid_t pid = fork();
    int status = 0;

    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        int null = open("/dev/null", O_RDWR);
        char *argv[10];
        char a_prog[PATH_MAX];
        char a_shell[] = "-no-shell-escape";
        char a_mode[] = "-interaction=nonstopmode";
        char a_halt[] = "-halt-on-error";
        char a_job[] = "-jobname=doc";
        char a_out[] = "-o";
        char a_pdf[] = "doc.pdf";
        int n = 0;

        if (chdir(dir) != 0) {
            _exit(126);
        }

        if (null >= 0) {
            dup2(null, 0);

            if (forward_output == 0) {
                dup2(null, 1);
            }

            dup2(null, 2);
        }

        if (forward_output != 0) {
            /* A long line is not broken, so a page mark is never split. */
            setenv("max_print_line", "10000", 1);
            setenv("error_line", "254", 1);
            setenv("half_error_line", "238", 1);
        }

        /* The program is looked for in the fixed folders of the system and
           started by its full path; PATH is never searched. */
        if (sp_find(prog, a_prog, sizeof a_prog, NULL) != 0) {
            _exit(127);
        }

        sp_child_env(a_prog);
        argv[n++] = a_prog;

        if (strcmp(prog, "dvipdfmx") == 0) {
            argv[n++] = a_out;
            argv[n++] = a_pdf;
        } else {
            argv[n++] = a_shell;
            argv[n++] = a_mode;

            if (halt != 0) {
                argv[n++] = a_halt;
            }

            argv[n++] = a_job;
        }

        argv[n++] = name;
        argv[n] = NULL;
        execv(a_prog, argv);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}


/*
 * Make a PDF from a PostScript picture with ghostscript (for pdflatex, which
 * cannot read PostScript when shell escape is off).  The program is started
 * without a shell and with -dSAFER; it is stopped after 120 seconds.
 * Returns 0 on success.
 */
static int
ps_to_pdf(const char *dir, const char *eps, const char *pdf)
{
    pid_t pid = fork();
    int status = 0;

    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        int null = open("/dev/null", O_RDWR);
        char out[320];
        char *argv[12];
        char a_gs[PATH_MAX];
        char a_q[] = "-q";
        char a_safer[] = "-dSAFER";
        char a_batch[] = "-dBATCH";
        char a_nopause[] = "-dNOPAUSE";
        char a_crop[] = "-dEPSCrop";
        char a_dev[] = "-sDEVICE=pdfwrite";
        char in[300];
        int n = 0;

        if (chdir(dir) != 0) {
            _exit(126);
        }

        if (null >= 0) {
            dup2(null, 0);
            dup2(null, 1);
            dup2(null, 2);
        }

        if (sp_find("gs", a_gs, sizeof a_gs, NULL) != 0) {
            _exit(127);
        }

        sp_child_env(a_gs);
        snprintf(out, sizeof out, "-sOutputFile=%s", pdf);
        str_copy(in, sizeof in, eps);
        argv[n++] = a_gs;
        argv[n++] = a_q;
        argv[n++] = a_safer;
        argv[n++] = a_batch;
        argv[n++] = a_nopause;
        argv[n++] = a_crop;
        argv[n++] = a_dev;
        argv[n++] = out;
        argv[n++] = in;
        argv[n] = NULL;
        execv(a_gs, argv);
        _exit(127);
    }

    for (int waited = 0; waited < 12000; waited++) {
        pid_t r = waitpid(pid, &status, WNOHANG);

        if (r == pid) {
            return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
        }

        if (r < 0) {
            return -1;
        }

        usleep(10000);
    }

    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);

    return -1;
}


/*
 * Run extractbb on a picture of the directory, which makes the file with the
 * size of the picture (picN.xbb) that latex needs when the DVI route reads a
 * PNG, JPEG or PDF picture and shell escape is off.  The program is started
 * without a shell; it is stopped after 30 seconds.  A picture that it cannot
 * read is not an error here: the engine then says so.
 */
static void
extract_box(const char *dir, const char *name)
{
    pid_t pid = fork();
    int status = 0;

    if (pid < 0) {
        return;
    }

    if (pid == 0) {
        int null = open("/dev/null", O_RDWR);
        char *argv[3];
        char a_prog[PATH_MAX];
        char a_name[300];

        if (chdir(dir) != 0) {
            _exit(126);
        }

        if (null >= 0) {
            dup2(null, 0);
            dup2(null, 1);
            dup2(null, 2);
        }

        if (sp_find("extractbb", a_prog, sizeof a_prog, NULL) != 0) {
            _exit(127);
        }

        sp_child_env(a_prog);
        str_copy(a_name, sizeof a_name, name);
        argv[0] = a_prog;
        argv[1] = a_name;
        argv[2] = NULL;
        execv(a_prog, argv);
        _exit(127);
    }

    for (int waited = 0; waited < 3000; waited++) {
        pid_t r = waitpid(pid, &status, WNOHANG);

        if (r == pid || r < 0) {
            return;
        }

        usleep(10000);
    }

    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
}


/* Make the size file of every PNG, JPEG and PDF link picN of the directory. */
static void
extract_boxes(const char *dir)
{
    DIR *dp = opendir(dir);
    struct dirent *e;

    if (dp == NULL) {
        return;
    }

    while ((e = readdir(dp)) != NULL) {
        const char *nm = e->d_name;
        const char *dot = strrchr(nm, '.');

        if (strncmp(nm, "pic", 3) != 0 || dot == NULL) {
            continue;
        }

        if (strcmp(dot, ".png") == 0 || strcmp(dot, ".jpg") == 0 ||
            strcmp(dot, ".pdf") == 0) {
            extract_box(dir, nm);
        }
    }

    closedir(dp);
}


static int
has_piece(const int *list, int n, int piece);


/* Give a piece up, with the message of TeX about it (or NULL). */
static void
add_piece(int *dis, int *ndis, int piece, const char *msg)
{
    if (*ndis >= MAXDIS || has_piece(dis, *ndis, piece) != 0) {
        return;
    }

    dis[*ndis] = piece;
    dis_text[*ndis][0] = '\0';

    if (msg != NULL) {
        str_copy(dis_text[*ndis], sizeof dis_text[*ndis],
                 strncmp(msg, "! ", 2) == 0 ? msg + 2 : msg);
    }

    dis_msg[*ndis] = dis_text[*ndis][0] != '\0' ? dis_text[*ndis] : NULL;
    (*ndis)++;
}


/*
 * Make picN.pdf for every link picN.eps of the directory that has none.  A
 * picture that ghostscript cannot convert is added to the list of the pieces
 * that are printed in the long form.  Returns how many were added.
 */
static int
convert_pictures(const char *dir, int *dis, int *ndis)
{
    DIR *dp = opendir(dir);
    struct dirent *e;
    int added = 0;

    if (dp == NULL) {
        return 0;
    }

    while ((e = readdir(dp)) != NULL) {
        const char *nm = e->d_name;
        size_t len = strlen(nm);
        char pdf[300];
        char path[1024];
        int piece;

        if (strncmp(nm, "pic", 3) != 0 || len < 8 || len > 40 ||
            strcmp(nm + len - 4, ".eps") != 0 || is_digit(nm[3]) == 0) {
            continue;
        }

        piece = atoi(nm + 3);
        snprintf(pdf, sizeof pdf, "pic%d.pdf", piece);
        path_join(path, sizeof path, dir, pdf);

        if (access(path, F_OK) == 0) {
            continue;
        }

        if (ps_to_pdf(dir, nm, pdf) != 0 &&
            has_piece(dis, *ndis, piece) == 0 && *ndis < MAXDIS) {
            add_piece(dis, ndis, piece,
                      "ghostscript could not convert the PostScript "
                      "picture");
            added++;
        }
    }

    closedir(dp);

    return added;
}


/* Tell whether the file at path contains the text. */
static int
file_has(const char *path, const char *text)
{
    size_t len;
    char *buf = read_file(path, &len);
    int r = buf != NULL && strstr(buf, text) != NULL;

    free(buf);

    return r;
}


/*
 * The piece of the .tex file that a TeX error at line n belongs to: the
 * number in the comment %F<number> at the end of that line or of one of
 * the three lines before it, or 0.
 */
static int
piece_at(const char *tex, int n)
{
    const char *p = tex;
    int line = 1;
    int found = 0;

    while (*p != '\0' && line <= n) {
        const char *e = strchr(p, '\n');
        const char *q;

        e = e != NULL ? e : p + strlen(p);
        q = e;

        while (q > p && is_digit(q[-1])) {
            q--;
        }

        if (line >= n - 3 && q < e && q - p >= 2 && q[-1] == 'F' &&
            q[-2] == '%') {
            found = atoi(q);
        }

        if (*e == '\0') {
            break;
        }

        p = e + 1;
        line++;
    }

    return found;
}


/*
 * The errors of a TeX log: each "! message" line with the number of the
 * line of the source in the "l.<n>" line that follows it.  The first error
 * is copied to msg.  At most max line numbers are stored; returns how many.
 */
static int
log_errors(const char *log, char *msg, size_t cap, int *lines, int max)
{
    const char *p = log;
    int n = 0;
    int first = 1;

    msg[0] = '\0';

    while (*p != '\0') {
        const char *e = strchr(p, '\n');
        size_t len = e != NULL ? lsz(e - p) : strlen(p);

        if (len >= 2 && p[0] == '!' && p[1] == ' ') {
            const char *q = e != NULL ? e + 1 : p + len;
            int k;

            if (first != 0) {
                copy_n(msg, cap, p, len);
                first = 0;
            }

            for (k = 0; k < 12 && *q != '\0'; k++) {
                const char *qe = strchr(q, '\n');

                if (q[0] == 'l' && q[1] == '.' && is_digit(q[2])) {
                    if (n < max) {
                        lines[n++] = atoi(q + 2);
                    }

                    break;
                }

                if (qe == NULL) {
                    break;
                }

                q = qe + 1;
            }
        }

        if (e == NULL) {
            break;
        }

        p = e + 1;
    }

    return n;
}


static int
has_piece(const int *list, int n, int piece)
{
    for (int i = 0; i < n; i++) {
        if (list[i] == piece) {
            return 1;
        }
    }

    return 0;
}


static int
write_file(const char *dir, const char *name, Doc *d, LatexOpts *o,
           int probe)
{
    char path[1024];
    FILE *f;
    int r;

    path_join(path, sizeof path, dir, name);
    f = fopen(path, "w");

    if (f == NULL) {
        return -1;
    }

    r = probe != 0 ? latex_probe(d, f, o) : latex_write(d, f, o);

    if (fclose(f) != 0) {
        r = -1;
    }

    return r;
}


/* Remove the files of the temporary directory and the directory. */
static void
remove_dir(const char *dir)
{
    DIR *dp = opendir(dir);
    struct dirent *e;

    if (dp != NULL) {
        while ((e = readdir(dp)) != NULL) {
            char path[1024];

            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                continue;
            }

            path_join(path, sizeof path, dir, e->d_name);
            unlink(path);
        }

        closedir(dp);
    }

    rmdir(dir);
}


static int
copy_file(const char *from, const char *to)
{
    size_t len;
    char *buf = read_file(from, &len);
    FILE *f;
    int ok;

    if (buf == NULL) {
        return -1;
    }

    f = fopen(to, "wb");

    if (f == NULL) {
        free(buf);

        return -1;
    }

    ok = fwrite(buf, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    free(buf);

    return ok != 0 ? 0 : -1;
}


/* Whether the name is that of a picture file that the TeX file uses. */
static int
is_picture_file(const char *name)
{
    static const char *const exts[] = {".png", ".jpg", ".pdf", ".eps"};
    size_t n = strlen(name);
    size_t i;

    if (strncmp(name, "pic", 3) != 0) {
        return 0;
    }

    for (i = 0; i < sizeof exts / sizeof exts[0]; i++) {
        size_t m = strlen(exts[i]);

        if (n > m && strcmp(name + n - m, exts[i]) == 0) {
            return 1;
        }
    }

    return 0;
}


/* Whether the two files have the same bytes. */
static int
same_file(const char *a, const char *b)
{
    size_t la = 0;
    size_t lb = 0;
    char *ba = read_file(a, &la);
    char *bb = read_file(b, &lb);
    int same = ba != NULL && bb != NULL && la == lb &&
               memcmp(ba, bb, la) == 0;

    free(ba);
    free(bb);

    return same;
}


/*
 * A DVI file names its pictures without a directory, and a viewer looks for
 * them next to the DVI file.  The pictures are copied to the directory of the
 * output.  A file of the same name that is different is left alone.
 */
static void
copy_pictures(const char *dir, const char *out)
{
    char odir[1024];
    const char *slash = strrchr(out, '/');
    DIR *dp = opendir(dir);
    struct dirent *de;

    if (dp == NULL) {
        return;
    }

    if (slash != NULL) {
        copy_n(odir, sizeof odir, out, lsz(slash - out));
    } else {
        str_copy(odir, sizeof odir, ".");
    }

    while ((de = readdir(dp)) != NULL) {
        char from[1024];
        char to[1024];

        if (is_picture_file(de->d_name) == 0) {
            continue;
        }

        path_join(from, sizeof from, dir, de->d_name);
        path_join(to, sizeof to, odir, de->d_name);

        if (access(to, F_OK) == 0 && same_file(from, to) == 0) {
            fprintf(stderr, "strprint: %s exists and differs: the picture "
                    "is not copied\n", to);
        } else if (copy_file(from, to) != 0) {
            fprintf(stderr, "strprint: cannot write %s\n", to);
        }
    }

    closedir(dp);
}


/* The name of the document without its directory and extension. */
static void
stem_of(const char *path, char *out, size_t cap)
{
    const char *base = strrchr(path, '/');
    char *dot;

    base = base != NULL ? base + 1 : path;
    str_copy(out, cap, base);
    dot = strrchr(out, '.');

    if (dot != NULL && dot != out) {
        *dot = '\0';
    }
}


/* The directory of the document ("." when it has none). */
static void
dir_of(const char *path, char *out, size_t cap)
{
    const char *sl = strrchr(path, '/');

    if (sl == NULL) {
        str_copy(out, cap, ".");
    } else if (sl == path) {
        str_copy(out, cap, "/");
    } else {
        copy_n(out, cap, path, lsz(sl - path));
    }
}


/* What to tell when a TeX engine did not run: it is probably not installed. */
static void
engine_missing(const char *engine, char *out, size_t cap)
{
    char tried[512];

    sp_tried(tried, sizeof tried);
    snprintf(out, cap, "%s did not run (is it installed? sudo apt install "
             "texlive-latex-recommended texlive-latex-extra "
             "texlive-extra-utils lmodern; programs are looked for in %s "
             "and not in PATH)", engine, tried);
}


static void
report_failure(const char *doc, const char *dir, const char *msg,
               const char *engine)
{
    char logpath[1024];
    size_t len;
    char *log;

    path_join(logpath, sizeof logpath, dir, "doc.log");
    log = read_file(logpath, &len);

    if (msg[0] != '\0') {
        fprintf(stderr, "strprint: %s: %s\n", doc, msg);
    } else if (log == NULL) {
        char said[1024];

        engine_missing(engine, said, sizeof said);
        fprintf(stderr, "strprint: %s: %s\n", doc, said);
    } else {
        fprintf(stderr, "strprint: %s: %s failed, no error line in %s\n",
                doc, engine, logpath);
    }

    free(log);
}


/* Find the pieces that TeX rejects, with runs on the equations only. */
static void
probe_pieces(const char *dir, Doc *d, LatexOpts *o, int *dis, int *ndis)
{
    for (int round = 0; round < 40; round++) {
        char path[1024];
        char logpath[1024];
        char name[] = "probe.tex";
        TexErr *errs;
        size_t len;
        char *tex;
        char *log;
        int n;
        int added = 0;

        o->disabled = dis;
        o->ndisabled = *ndis;

        if (write_file(dir, name, d, o, 1) != 0) {
            return;
        }

        run_prog(dir, "pdflatex", name, 1);
        path_join(path, sizeof path, dir, name);
        /* The job name is "doc" for every run, so the log is doc.log. */
        path_join(logpath, sizeof logpath, dir, "doc.log");
        tex = read_file(path, &len);
        log = read_file(logpath, &len);

        if (tex == NULL || log == NULL) {
            free(tex);
            free(log);

            return;
        }

        errs = malloc(200 * sizeof *errs);

        if (errs == NULL) {
            free(tex);
            free(log);

            return;
        }

        n = diag_tex_errors(log, errs, 200);

        for (int i = 0; i < n; i++) {
            int piece = piece_at(tex, errs[i].line);

            if (piece > 0 && has_piece(dis, *ndis, piece) == 0 &&
                *ndis < MAXDIS) {
                add_piece(dis, ndis, piece, errs[i].msg);
                added++;
            }
        }

        free(errs);
        free(tex);
        free(log);

        if (added == 0) {
            return;
        }
    }
}


/*
 * Tell the editor something: a line that begins with "@@strprint ".  A
 * blank line comes first, so that the line starts at the beginning of a
 * line of the output of TeX.  Nothing is written without --progress.
 */
static void
tell(const Args *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void
tell(const Args *a, const char *fmt, ...)
{
    va_list ap;

    if (a->progress == 0) {
        return;
    }

    fputs("\n@@strprint ", stdout);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fputs("\n\n", stdout);
    fflush(stdout);
}


/*
 * Tell the editor how the check of an export goes: a line "@@strprint check
 * WORD ...".  Unlike tell this is written without --progress, because the
 * editor asked for the check with --check-latex and reads these lines.
 */
static void
say_check(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void
say_check(const char *fmt, ...)
{
    va_list ap;

    fputs("\n@@strprint check ", stdout);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fputs("\n\n", stdout);
    fflush(stdout);
}


/*
 * The first error of a TeX log: the line that starts with "!" and the line
 * after it.  Both are empty when the log has no error.
 */
static void
first_error(const char *log, char *l1, char *l2, size_t cap)
{
    const char *p = log;

    l1[0] = '\0';
    l2[0] = '\0';

    while (p != NULL && *p != '\0') {
        const char *e = strchr(p, '\n');

        if (p[0] == '!' && p[1] == ' ') {
            copy_n(l1, cap, p, e != NULL ? lsz(e - p) : strlen(p));

            if (e != NULL && e[1] != '\0') {
                const char *q = e + 1;
                const char *qe = strchr(q, '\n');

                copy_n(l2, cap, q, qe != NULL ? lsz(qe - q) : strlen(q));
            }

            return;
        }

        p = e != NULL ? e + 1 : NULL;
    }
}


static int
parse_args(int argc, char **argv, Args *a)
{
    memset(a, 0, sizeof *a);
    a->error_log = 1;
    a->banner = 1;
    /* The document decides the parts that no option says anything about. */
    a->contents = LATEX_AUTO;
    a->index = LATEX_AUTO;
    a->list_figures = LATEX_AUTO;
    a->list_pictures = LATEX_AUTO;
    a->list_tables = LATEX_AUTO;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            a->out = argv[++i];
        } else if (strcmp(argv[i], "--dvi") == 0) {
            a->dvi = 1;
        } else if (strcmp(argv[i], "--pdf-via-dvi") == 0) {
            a->via_dvi = 1;
        } else if (strcmp(argv[i], "--tex-only") == 0) {
            a->tex_only = 1;
        } else if (strcmp(argv[i], "--export-latex") == 0 && i + 1 < argc) {
            a->export_latex = argv[++i];
        } else if (strcmp(argv[i], "--pictures") == 0 && i + 1 < argc) {
            i++;

            if (strcmp(argv[i], "copy") == 0) {
                a->keep_pictures = 0;
            } else if (strcmp(argv[i], "keep") == 0) {
                a->keep_pictures = 1;
            } else {
                return -1;
            }
        } else if (strcmp(argv[i], "--position-comments") == 0) {
            a->positions = 1;
        } else if (strcmp(argv[i], "--check-latex") == 0) {
            a->check = 1;
        } else if (strcmp(argv[i], "--latex-markers") == 0) {
            a->latex_markers = 1;
        } else if (strcmp(argv[i], "--no-latex-markers") == 0) {
            a->latex_markers = 2;
        } else if (strcmp(argv[i], "--source-name") == 0 && i + 1 < argc) {
            a->source = argv[++i];
        } else if (strcmp(argv[i], "--source-path") == 0 && i + 1 < argc) {
            a->source_path = argv[++i];
        } else if (strcmp(argv[i], "--keep") == 0) {
            a->keep = 1;
        } else if (strcmp(argv[i], "--draft") == 0) {
            a->draft = 1;
        } else if (strcmp(argv[i], "--double-spaced") == 0) {
            a->double_spaced = 1;
        } else if (strcmp(argv[i], "--black-and-white") == 0) {
            a->black_white = 1;
        } else if (strcmp(argv[i], "--recover") == 0) {
            a->recover = 1;
        } else if (strcmp(argv[i], "--index") == 0) {
            a->index = 1;
        } else if (strcmp(argv[i], "--list-figures") == 0) {
            a->list_figures = 1;
        } else if (strcmp(argv[i], "--list-pictures") == 0) {
            a->list_pictures = 1;
        } else if (strcmp(argv[i], "--list-tables") == 0) {
            a->list_tables = 1;
        } else if (strcmp(argv[i], "--no-index") == 0) {
            a->index = 0;
        } else if (strcmp(argv[i], "--no-list-figures") == 0) {
            a->list_figures = 0;
        } else if (strcmp(argv[i], "--no-list-pictures") == 0) {
            a->list_pictures = 0;
        } else if (strcmp(argv[i], "--no-list-tables") == 0) {
            a->list_tables = 0;
        } else if (strcmp(argv[i], "--contents") == 0 && i + 1 < argc) {
            a->contents = atoi(argv[++i]);

            if (a->contents < 0 || a->contents > 99) {
                fputs("strprint: --contents is a level from 0 to 99\n",
                      stderr);

                return -1;
            }
        } else if (strcmp(argv[i], "--long-list") == 0) {
            a->list = 1;
        } else if (strcmp(argv[i], "--progress") == 0) {
            a->progress = 1;
        } else if (strcmp(argv[i], "--no-error-log") == 0) {
            a->error_log = 0;
        } else if (strcmp(argv[i], "--error-log") == 0) {
            a->error_log = 1;
        } else if (strcmp(argv[i], "--no-banner") == 0) {
            a->banner = 0;
        } else if (strcmp(argv[i], "--banner") == 0) {
            a->banner = 1;
        } else if (strcmp(argv[i], "--errors") == 0 && i + 1 < argc) {
            a->errors = argv[++i];
        } else if (strcmp(argv[i], "--docdir") == 0 && i + 1 < argc) {
            a->docdir = argv[++i];
        } else if (strcmp(argv[i], "--workingdir") == 0 && i + 1 < argc) {
            a->workdir = argv[++i];
        } else if (strcmp(argv[i], "--var") == 0 ||
                   strcmp(argv[i], "--special") == 0) {
            char verr[240];

            if (vars_option(argc, argv, &i, verr, sizeof verr) != 1) {
                log_error("strprint: the option %s was refused and the "
                          "print does not start: %s", argv[i], verr);

                return -1;
            }
        } else if (argv[i][0] != '-' && a->doc == NULL) {
            a->doc = argv[i];
        } else {
            return -1;
        }
    }

    return a->doc != NULL && (a->dvi == 0 || a->via_dvi == 0) ? 0 : -1;
}


/*
 * Print the marker types that were printed in the long form and how many
 * pieces were given up because TeX refused them.
 */
static void
print_long_list(const LatexOpts *o, int refused)
{
    printf("refused: %d\n", refused);
    printf("longform:");

    for (int t = 0; t <= LATEX_MAXTYPE; t++) {
        if (o->longform[t] != 0) {
            printf(" %d", t);
        }
    }

    printf("\n");
}


/*
 * Tell the diagnostics: a line on stderr for each, and with --progress the
 * lines "@@strprint diag ..." for the editor.  The list is in the order of
 * the document.
 */
static void
report_diags(const Args *a, const DiagList *l)
{
    int errors;
    int warnings;

    for (int i = 0; i < l->n; i++) {
        char line[2 * DIAG_MSG + 400];
        char wire[4 * DIAG_MSG + 400];

        diag_describe(&l->v[i], line, sizeof line);
        fprintf(stderr, "strprint: %s\n", line);

        if (diag_encode(&l->v[i], wire, sizeof wire) > 0) {
            tell(a, "diag %s", wire);
        }
    }

    diag_counts(l, &errors, &warnings);

    /* The pictures that are not there are told on a line of their own, so
       that a print with gaps in it cannot be taken for a good one. */
    int missing = 0;

    for (int i = 0; i < l->n; i++) {
        if (strcmp(l->v[i].code, "picture-missing") == 0) {
            missing++;
        }
    }

    if (missing > 0) {
        fprintf(stderr, "strprint: %d picture file%s not found\n", missing,
                missing == 1 ? "" : "s");
        tell(a, "missingpictures %d", missing);
    }

    if (l->n > 0) {
        fprintf(stderr, "strprint: %s%d error%s, %d warning%s%s\n",
                errors > 0 && a->tex_only == 0 && a->export_latex == NULL ?
                "PRINT FAILED: " : "", errors,
                errors == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s",
                l->dropped > 0 ? " (more were found than are listed)" : "");
    }

    if (l->dropped > 0) {
        tell(a, "diagdropped %d", l->dropped);
    }
}


/*
 * Run the engine on the file: one pass, and another while the first one
 * asks for it (page references, a label that moved), up to three in all.
 * halt is 1 to stop at the first error of TeX.  Returns the exit status of
 * the last pass.
 */
static int
run_passes(const char *dir, const char *engine, char *name, int halt,
           const Args *a, const LatexOpts *o)
{
    char logpath[1024];
    int r = -1;

    path_join(logpath, sizeof logpath, dir, "doc.log");

    for (int pass = 1; pass <= 3; pass++) {
        int again;

        tell(a, "pass %d", pass);
        forward_output = a->progress;
        r = run_prog(dir, engine, name, halt);
        forward_output = 0;
        again = (pass == 1 && o->pagerefs != 0) ||
                file_has(logpath, "Rerun") != 0 ||
                file_has(logpath, "may have changed") != 0;

        if (r != 0 || again == 0) {
            break;
        }
    }

    return r;
}


/* The errors of the TeX log of the last run, as diagnostics of d. */
static void
collect_tex_errors(const char *dir, const char *texname, Doc *d,
                   DiagList *out)
{
    char logpath[1024];
    char texpath[1024];
    size_t len;
    char *log;
    char *tex;
    TexErr *errs = malloc(MAXTEXERR * sizeof *errs);
    int n;

    path_join(logpath, sizeof logpath, dir, "doc.log");
    path_join(texpath, sizeof texpath, dir, texname);
    log = read_file(logpath, &len);
    tex = read_file(texpath, &len);

    if (errs == NULL || log == NULL) {
        free(errs);
        free(log);
        free(tex);

        return;
    }

    n = diag_tex_errors(log, errs, MAXTEXERR);

    for (int i = 0; i < n; i++) {
        Diag *g = diag_add(out, DIAG_ERROR, "tex-error", "LaTeX reports: %s",
                           errs[i].msg);
        int item;
        int col;
        int line;

        if (g == NULL) {
            continue;
        }

        g->texline = errs[i].line;
        str_copy(g->text, sizeof g->text, errs[i].ctx);

        if (tex != NULL && errs[i].line > 0 &&
            diag_tex_position(tex, errs[i].line, &item, &col, &line) != 0) {
            g->item = item;
            g->col = col;
            g->line = line;
            diag_locate(d, g);
        }
    }

    free(errs);
    free(log);
    free(tex);
}


/* Put the diagnostics of two lists into one, in the order of the document. */
static void
merge_diags(DiagList *all, const DiagList *a, const DiagList *b)
{
    diag_list_clear(all);

    for (int i = 0; i < a->n; i++) {
        diag_push(all, &a->v[i]);
    }

    for (int i = 0; i < b->n; i++) {
        diag_push(all, &b->v[i]);
    }

    all->dropped += a->dropped + b->dropped;
    diag_sort(all);
}


/* The name of the file that the engine makes. */
static const char *
result_name(const Args *a)
{
    return a->dvi != 0 ? "doc.dvi" : "doc.pdf";
}


/*
 * Add the Error Log, the banner and the marks of a failed print to the
 * document: the file is written again with the log at its end (unless the
 * log is off), the banner on every page and the marks at the places, and
 * TeX is run on it, keeping the old result when that fails.  Returns 0 when
 * the new result is in place.
 */
static int
add_error_log(const char *dir, char *name, Doc *d, LatexOpts *o,
              const Args *a, const char *engine, const DiagList *all,
              int texerrors)
{
    char res[1024];
    char old[1100];
    int r;
    int ok;
    int errors;
    int warnings;

    diag_counts(all, &errors, &warnings);
    path_join(res, sizeof res, dir, result_name(a));
    snprintf(old, sizeof old, "%s.prev", res);
    rename(res, old);
    o->errlog = a->error_log != 0 ? all : NULL;
    o->failed = errors > 0 ? all : NULL;
    o->banner = a->banner;

    if (write_file(dir, name, d, o, 0) != 0) {
        o->errlog = NULL;
        o->failed = NULL;
        rename(old, res);

        return -1;
    }

    r = run_passes(dir, engine, name, texerrors == 0, a, o);
    o->errlog = NULL;
    o->failed = NULL;

    if (a->via_dvi != 0 && (r == 0 || texerrors != 0)) {
        char dvi[] = "doc.dvi";
        char dvipath[1024];

        path_join(dvipath, sizeof dvipath, dir, dvi);

        if (access(dvipath, R_OK) == 0 && run_prog(dir, "dvipdfmx", dvi, 0) !=
                                          0) {
            r = -1;
        }
    }

    ok = access(res, R_OK) == 0 && (r == 0 || texerrors != 0);

    if (ok != 0) {
        unlink(old);

        return 0;
    }

    unlink(res);
    rename(old, res);

    return -1;
}


/* A name made only of the characters that TeX takes as they are. */
static void
safe_name(const char *in, char *out, size_t cap)
{
    size_t n = 0;

    for (; *in != '\0' && n + 1 < cap; in++) {
        out[n++] = is_alnum(*in) != 0 || *in == '.' || *in == '-' ||
                   *in == '_' ? *in : '_';
    }

    out[n] = '\0';
}


/*
 * The name of the .tex file of an export: the name that --export-latex gives,
 * else the name of -o with a trailing .pdf or .dvi replaced by .tex (and .tex
 * added when it has none), else the name of the document with .tex.
 */
static void
export_path(const Args *a, const char *stem, char *out, size_t cap)
{
    size_t n;

    if (a->export_latex != NULL) {
        str_copy(out, cap, a->export_latex);

        return;
    }

    if (a->out == NULL) {
        snprintf(out, cap, "%s.tex", stem);

        return;
    }

    n = strlen(a->out);

    if (n > 4 && (strcmp(a->out + n - 4, ".pdf") == 0 ||
                  strcmp(a->out + n - 4, ".dvi") == 0)) {
        copy_n(out, cap, a->out, n - 4);
        str_copy(out + strlen(out), cap - strlen(out), ".tex");
    } else if (n > 4 && strcmp(a->out + n - 4, ".tex") == 0) {
        str_copy(out, cap, a->out);
    } else {
        snprintf(out, cap, "%s.tex", a->out);
    }
}


/*
 * The day for the header of an export, as YYYY-MM-DD: the day of the
 * environment variable SOURCE_DATE_EPOCH (in UTC) when it is set, so that a
 * file can be made again byte for byte, else the local day.
 */
static void
export_day(char *out, size_t cap)
{
    const char *epoch = getenv("SOURCE_DATE_EPOCH");
    time_t t = time(NULL);
    struct tm tmv;
    int utc = 0;

    if (epoch != NULL && epoch[0] != '\0') {
        char *end;
        long v = strtol(epoch, &end, 10);

        if (*end == '\0' && v >= 0) {
            t = v;
            utc = 1;
        }
    }

    if (utc != 0) {
        gmtime_r(&t, &tmv);
    } else {
        localtime_r(&t, &tmv);
    }

    if (strftime(out, cap, "%Y-%m-%d", &tmv) == 0) {
        str_copy(out, cap, "");
    }
}


/* Remove the picture files that an earlier export left in the folder. */
static void
clear_pictures(const char *dir)
{
    DIR *dp = opendir(dir);
    struct dirent *de;

    if (dp == NULL) {
        return;
    }

    while ((de = readdir(dp)) != NULL) {
        char path[1100];

        if (is_picture_file(de->d_name) == 0 || is_digit(de->d_name[3]) == 0) {
            continue;
        }

        path_join(path, sizeof path, dir, de->d_name);
        unlink(path);
    }

    closedir(dp);
}


/*
 * Tell why an export failed.  The reason is a diagnostic of the run, so that
 * whoever started the Printer reads it from the error file; nothing is
 * written to the console.
 */
static void
export_failed(DiagList *all, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));


static void
export_failed(DiagList *all, const char *fmt, ...)
{
    char msg[DIAG_MSG];
    va_list ap;
    Diag *g;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    g = diag_add(all, DIAG_ERROR, "export-failed", "%s", msg);

    if (g != NULL) {
        g->item = -1;
    }
}


static int
write_tex(const char *path, Doc *d, LatexOpts *o)
{
    FILE *f = fopen(path, "w");
    int r;

    if (f == NULL) {
        return -1;
    }

    r = latex_write(d, f, o);

    if (fclose(f) != 0) {
        r = -1;
    }

    return r;
}


/*
 * An engine can stop with "!pdfTeX error: pdflatex (file ./F): REASON" and no
 * line of the usual form: a picture that it cannot read does that.  Tell it
 * as a diagnostic of the document, with the place of the picture when the
 * file of the picture is named in the .tex file.  Returns 1 when the log
 * has such an error.
 */
static int
picture_error(const char *dir, const char *texname, Doc *d, DiagList *out)
{
    char logpath[1024];
    char texpath[1024];
    char what[DIAG_MSG];
    char path[512];
    size_t len;
    char *log;
    char *tex;
    const char *e;
    const char *end;
    const char *f;
    const char *fend;
    Diag *g;
    size_t n = 0;
    int found = 0;

    path_join(logpath, sizeof logpath, dir, "doc.log");
    path_join(texpath, sizeof texpath, dir, texname);
    log = read_file(logpath, &len);
    tex = read_file(texpath, &len);
    path[0] = '\0';

    if (log == NULL) {
        free(tex);

        return 0;
    }

    e = strstr(log, "pdfTeX error:");

    if (e != NULL) {
        found = 1;
        end = strstr(e, "==>");
        end = end != NULL ? end : e + strlen(e);

        /* The text of the error, in one line: the log breaks long lines. */
        for (const char *c = e + strlen("pdfTeX error:");
             c < end && n + 1 < sizeof what; c++) {
            if (*c == '\n') {
                if (n > 0 && what[n - 1] != ' ') {
                    what[n++] = ' ';
                }
            } else if (*c != ' ' || (n > 0 && what[n - 1] != ' ')) {
                what[n++] = *c;
            }
        }

        while (n > 0 && what[n - 1] == ' ') {
            n--;
        }

        what[n] = '\0';

        /* The reason is what follows the name of the file. */
        {
            char *colon = strstr(what, "): ");

            if (colon != NULL) {
                memmove(what, colon + 3, strlen(colon + 3) + 1);
            }
        }

        f = strstr(e, "(file ");
        fend = f != NULL ? strchr(f, ')') : NULL;

        if (f != NULL && fend != NULL && fend < end) {
            const char *s = f + strlen("(file ");

            if (s[0] == '.' && s[1] == '/') {
                s += 2;
            }

            copy_n(path, sizeof path, s, lsz(fend - s));
        }
    }

    if (found != 0) {
        g = diag_add(out, DIAG_ERROR, "picture-unreadable",
                     "LaTeX cannot read the picture %s: %s.",
                     path[0] != '\0' ? path : "of the file", what);

        if (g != NULL) {
            g->item = -1;

            /* The line of the .tex file that names the picture. */
            if (tex != NULL && path[0] != '\0') {
                char stem[512];
                char *dot;
                char *at;

                str_copy(stem, sizeof stem, path);
                dot = strrchr(stem, '.');

                if (dot != NULL) {
                    *dot = '\0';
                }

                at = strstr(tex, stem);

                if (at != NULL) {
                    int line = 1;
                    int item;
                    int col;
                    int tl;

                    for (const char *c = tex; c < at; c++) {
                        if (*c == '\n') {
                            line++;
                        }
                    }

                    g->texline = line;

                    if (diag_tex_position(tex, line, &item, &col, &tl) !=
                        0) {
                        g->item = item;
                        g->col = col;
                        g->line = tl;
                        diag_locate(d, g);
                    }
                }
            }
        }
    }

    free(log);
    free(tex);

    return found;
}


/*
 * Check an export (--check-latex): the engine of a print is run once on the
 * file, in a private directory, and every error of TeX becomes a diagnostic
 * of the document in wd.  The file is made again there by the same writer,
 * with the position comments (so that an error of TeX has its place in the
 * document) and with its pictures in a folder of the directory; that is the
 * file that was exported but for its comments.  The file of the user and its
 * folder are not touched.  The lines "@@strprint check ..." tell the editor
 * how it went; a check that cannot be made (no engine, no directory) is
 * skipped and is not an error of the document.
 */
static void
check_export(const Args *a, Doc *d, const LatexOpts *o, DiagList *wd,
             const char *folder, const int *dis, int ndis)
{
    char tmpl[1024];
    char picdir[1100];
    char texpath[1100];
    char name[] = "doc.tex";
    char said[1024];
    const char *tmp = getenv("TMPDIR");
    LatexOpts co = *o;
    Args quiet = *a;
    DiagList td;
    int local[MAXDIS];
    int nlocal = ndis < MAXDIS ? ndis : MAXDIS;
    char *dir;
    int r;

    say_check("started");
    snprintf(tmpl, sizeof tmpl, "%s/strprint.XXXXXX",
             tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");
    dir = mkdtemp(tmpl);

    if (dir == NULL) {
        say_check("skipped Cannot make a temporary directory: %s.",
                  strerror(errno));

        return;
    }

    path_join(picdir, sizeof picdir, dir, folder);
    path_join(texpath, sizeof texpath, dir, name);

    if (mkdir(picdir, 0777) != 0) {
        say_check("skipped Cannot make a folder in %s: %s.", dir,
                  strerror(errno));
        remove_dir(dir);

        return;
    }

    memcpy(local, dis, usz(nlocal) * sizeof local[0]);
    co.export_name = NULL;
    co.workdir = NULL;
    co.diags = NULL;
    co.picdir = picdir;
    co.picref = folder;
    co.posmarks = 1;
    co.marks = 0;
    co.eps_pdf = 1;
    quiet.progress = 0;

    for (int attempt = 0; attempt < 8; attempt++) {
        co.disabled = local;
        co.ndisabled = nlocal;

        if (write_tex(texpath, d, &co) != 0) {
            say_check("skipped Cannot write in %s: %s.", dir, strerror(errno));
            remove_dir(dir);

            return;
        }

        if (convert_pictures(picdir, local, &nlocal) == 0) {
            break;
        }
    }

    r = run_passes(dir, "pdflatex", name, 0, &quiet, &co);

    if (r == 127 || r == -1) {
        engine_missing("pdflatex", said, sizeof said);
        say_check("skipped %s", said);
        remove_dir(dir);

        return;
    }

    if (r == 0) {
        say_check("ok");
        remove_dir(dir);

        return;
    }

    diag_list_init(&td);
    collect_tex_errors(dir, name, d, &td);

    if (td.n == 0) {
        picture_error(dir, name, d, &td);
    }

    if (td.n == 0) {
        Diag *g = diag_add(&td, DIAG_ERROR, "check-stopped",
                           "LaTeX stopped without an error message; a "
                           "picture that it cannot read can do that.");

        if (g != NULL) {
            g->item = -1;
        }
    }

    for (int i = 0; i < td.n; i++) {
        diag_push(wd, &td.v[i]);
    }

    wd->dropped += td.dropped;
    say_check("errors %d", td.n);
    diag_list_free(&td);
    remove_dir(dir);
}


/*
 * Export the document as a LaTeX file that is made to be kept (see
 * docs/PRINT-LATEX.txt): the file of Print and Preview without the banner,
 * the marks and the Error Log, with a header comment, and with the pictures
 * copied into a folder next to it.  The pieces that TeX rejects are found
 * as in a print (one run of pdflatex when it is installed).  The result of
 * the run is 0, or 4 when the document has diagnostics, or 1.
 */
static int
export_latex(const Args *a, Doc *d, LatexOpts *o, DiagList *all, DiagList *wd,
             const char *stem)
{
    char texpath[1024];
    char newpath[1040];
    char texdir[1024];
    char tstem[256];
    char safe[256];
    char folder[300];
    char picdir[1400];
    char day[16];
    char source[256];
    const char *slash;
    int dis[MAXDIS];
    int ndis = 0;
    struct stat st;

    export_path(a, stem, texpath, sizeof texpath);
    dir_of(texpath, texdir, sizeof texdir);
    stem_of(texpath, tstem, sizeof tstem);
    safe_name(tstem, safe, sizeof safe);
    snprintf(folder, sizeof folder, "%s_files", safe);
    path_join(picdir, sizeof picdir, texdir, folder);
    snprintf(newpath, sizeof newpath, "%s.new", texpath);
    slash = strrchr(texpath, '/');
    export_day(day, sizeof day);
    stem_of(a->source != NULL ? a->source : a->doc, source, sizeof source);

    if (stat(picdir, &st) == 0 && S_ISDIR(st.st_mode) == 0) {
        export_failed(all, "%s is not a folder, so the pictures cannot be "
                      "copied there.", picdir);

        return 1;
    }

    if (mkdir(picdir, 0777) != 0 && errno != EEXIST) {
        export_failed(all, "Cannot make the folder %s for the pictures: %s.",
                      picdir, strerror(errno));

        return 1;
    }

    clear_pictures(picdir);
    o->export_name = a->source != NULL ? a->source : a->doc;
    o->export_tex = slash != NULL ? slash + 1 : texpath;
    o->export_date = day;
    o->picdir = picdir;
    o->picref = folder;
    o->copy_pictures = a->keep_pictures == 0;
    o->posmarks = a->positions;
    o->marks = 0;
    o->eps_pdf = 1;
    o->diags = wd;
    o->disabled_msg = dis_msg;

    if (write_tex(newpath, d, o) != 0) {
        export_failed(all, "Cannot write %s: %s.", texpath, strerror(errno));
        rmdir(picdir);

        return 1;
    }

    /* The pieces that TeX rejects are found as a print finds them. */
    if (o->npieces > 0) {
        LatexOpts po = *o;
        char tmpl[1024];
        const char *tmp = getenv("TMPDIR");
        char *dir;

        po.export_name = NULL;
        po.picdir = NULL;
        po.workdir = NULL;
        po.diags = NULL;
        snprintf(tmpl, sizeof tmpl, "%s/strprint.XXXXXX",
                 tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");
        dir = mkdtemp(tmpl);

        if (dir != NULL) {
            probe_pieces(dir, d, &po, dis, &ndis);
            remove_dir(dir);
        }
    }

    for (int attempt = 0; attempt < 8; attempt++) {
        o->disabled = dis;
        o->ndisabled = ndis;

        if (write_tex(newpath, d, o) != 0) {
            export_failed(all, "Cannot write %s: %s.", texpath,
                          strerror(errno));
            unlink(newpath);
            rmdir(picdir);

            return 1;
        }

        if (convert_pictures(picdir, dis, &ndis) == 0) {
            break;
        }
    }

    if (rename(newpath, texpath) != 0) {
        export_failed(all, "Cannot write %s: %s.", texpath, strerror(errno));
        unlink(newpath);

        return 1;
    }

    /* The folder is removed when no picture was copied into it. */
    rmdir(picdir);

    if (a->check != 0) {
        /*
         * The file is done: the diagnostics of the document are written
         * for the editor before the check, so that a check that is stopped
         * leaves them behind.
         */
        if (a->errors != NULL) {
            DiagList early;

            diag_list_init(&early);
            diag_list_copy(&early, wd);
            diag_sort(&early);
            diag_file_write(a->errors, &early, wd->n > 0 ? 4 : 0);
            diag_list_free(&early);
        }

        check_export(a, d, o, wd, folder, dis, ndis);
    }

    diag_sort(wd);
    report_diags(a, wd);

    if (a->list != 0) {
        print_long_list(o, 0);
    }

    diag_list_copy(all, wd);

    return wd->n > 0 ? 4 : 0;
}


/* The whole run; the diagnostics found are left in all. */
static int
run(const Args *ap, DiagList *all)
{
    Args a = *ap;
    char err[256];
    char stem[256];
    char ddir[1024];
    char tmpl[1024];
    char outpdf[1024];
    char name[] = "doc.tex";
    const char *tmp = getenv("TMPDIR");
    char *dir;
    Doc *d;
    const char *engine;
    const char *ext;
    LatexOpts o;
    int dis[MAXDIS];
    int ndis = 0;
    int status = 1;
    int texfailed = 0;
    char msg[256] = "";
    DiagList wd;
    DiagList td;

    if (a.recover != 0) {
        DocRecovery rc;

        d = doc_recover(a.doc, &rc, err, sizeof err);

        if (d != NULL && rc.damaged != 0) {
            fprintf(stderr, "strprint: %s: %s; printing the part that can be "
                    "read (%d blocks and %d sections, %lu of %lu bytes); the "
                    "file is not changed\n", a.doc, rc.why, rc.blocks,
                    rc.sections, (unsigned long)rc.bytes_read,
                    (unsigned long)rc.file_size);
        }
    } else {
        d = doc_load(a.doc, err, sizeof err);
    }

    if (d == NULL) {
        fprintf(stderr, "strprint: %s: %s\n", a.doc, err);

        return 1;
    }

    /* The markers that name the file stand for the real file, not the copy. */
    if (a.source_path != NULL) {
        marker_set_source(a.source_path);
    }

    /* The Print Error markers of the Editor are not part of the document. */
    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type != IT_BLOCK || d->items[i].blk == NULL) {
            continue;
        }

        for (int c = 0; c < d->items[i].blk->ncols; c++) {
            Text *t = &d->items[i].blk->cols[c].text;

            for (int k = t->n - 1; k >= 0; k--) {
                if (t->v[k].kind == TK_MARKER &&
                    tok_mtype(&t->v[k]) == MT_PRINT_ERROR) {
                    text_delete(t, k, 1);
                }
            }
        }
    }

    engine = a.dvi != 0 || a.via_dvi != 0 ? "latex" : "pdflatex";
    ext = a.dvi != 0 ? "dvi" : "pdf";
    stem_of(a.doc, stem, sizeof stem);
    dir_of(a.doc, ddir, sizeof ddir);
    diag_list_init(&wd);
    diag_list_init(&td);
    memset(&o, 0, sizeof o);
    o.draft = a.draft;
    o.double_spaced = a.double_spaced;
    o.contents = a.contents;
    o.black_white = a.black_white;
    o.index = a.index;
    o.list_figures = a.list_figures;
    o.list_pictures = a.list_pictures;
    o.list_tables = a.list_tables;
    o.docdir = a.docdir != NULL ? a.docdir : ddir;
    o.picwork = a.workdir;
    o.marks = a.progress;
    o.preview = a.dvi;
    o.diags = &wd;
    o.disabled_msg = dis_msg;
    o.vars = vars_process();

    /* The LaTeX markers are allowed unless the settings or the options say
       they are not (the options win). */
    if (a.latex_markers == 0) {
        Config cfg;

        config_load(&cfg);
        o.no_latex = cfg.latex_markers_off;
    } else {
        o.no_latex = a.latex_markers == 2;
    }

    if (a.tex_only != 0 || a.export_latex != NULL) {
        status = export_latex(&a, d, &o, all, &wd, stem);
        diag_list_free(&wd);
        diag_list_free(&td);
        doc_free(d);

        return status;
    }

    snprintf(tmpl, sizeof tmpl, "%s/strprint.XXXXXX",
             tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");
    dir = mkdtemp(tmpl);

    if (dir == NULL) {
        fprintf(stderr, "strprint: cannot make a temporary directory: %s\n",
                strerror(errno));

        return 1;
    }

    o.workdir = dir;
    o.eps_pdf = a.dvi == 0 && a.via_dvi == 0;
    o.posmarks = 1;
    tell(&a, "stage writing");

    /* A first file tells how many pieces TeX could refuse. */
    if (write_file(dir, name, d, &o, 0) != 0) {
        fprintf(stderr, "strprint: cannot write in %s\n", dir);
        remove_dir(dir);

        return 1;
    }

    if (o.npieces > 0) {
        probe_pieces(dir, d, &o, dis, &ndis);
    }

    for (int attempt = 0; attempt < MAXTRY; attempt++) {
        char logpath[1024];
        char texpath[1024];
        size_t len;
        int lines[16];
        int r;

        o.disabled = dis;
        o.ndisabled = ndis;

        if (write_file(dir, name, d, &o, 0) != 0) {
            fprintf(stderr, "strprint: cannot write in %s\n", dir);
            break;
        }

        /* A picture that cannot be converted is given up at once. */
        if (o.eps_pdf != 0 && convert_pictures(dir, dis, &ndis) > 0) {
            continue;
        }

        /* The DVI route needs the size of a picture in a file. */
        if (o.eps_pdf == 0) {
            extract_boxes(dir);
        }

        path_join(logpath, sizeof logpath, dir, "doc.log");
        path_join(texpath, sizeof texpath, dir, name);
        unlink(logpath);
        r = run_passes(dir, engine, name, 1, &a, &o);

        if (r == 0 && a.via_dvi != 0) {
            char dvi[] = "doc.dvi";

            if (run_prog(dir, "dvipdfmx", dvi, 0) != 0) {
                str_copy(msg, sizeof msg, "dvipdfmx failed");
                engine = "dvipdfmx";
                break;
            }
        }

        if (r == 0) {
            status = 0;
            break;
        }

        texfailed = 1;

        {
            char *log = read_file(logpath, &len);
            char *tex = read_file(texpath, &len);
            int n = 0;
            int piece = 0;

            if (log != NULL && tex != NULL) {
                n = log_errors(log, msg, sizeof msg, lines, 16);
                piece = n > 0 ? piece_at(tex, lines[0]) : 0;
            } else if (log != NULL) {
                log_errors(log, msg, sizeof msg, lines, 16);
            }

            free(log);
            free(tex);

            if (piece > 0 && has_piece(dis, ndis, piece) == 0 &&
                ndis < MAXDIS) {
                add_piece(dis, &ndis, piece, msg);
                texfailed = 0;
                continue;
            }

            /* The engine stopped without a message: a picture it cannot
               read can do that, so all the pictures are given up. */
            if (n == 0 && o.npictures > 0 && o.no_pictures == 0) {
                o.no_pictures = 1;
                msg[0] = '\0';
                texfailed = 0;
                continue;
            }
        }

        break;
    }

    /*
     * TeX failed and that was not a piece that can be given up.  The first
     * error is told to the editor, then the run is repeated without the
     * stop at the first error: what it makes (TeX writes a file in spite
     * of errors) is kept and every error of its log is a diagnostic.
     */
    if (status != 0 && texfailed != 0) {
        char logpath[1024];
        char res[1024];
        char l1[256];
        char l2[256];
        size_t len;
        char *log;

        path_join(logpath, sizeof logpath, dir, "doc.log");
        path_join(res, sizeof res, dir, result_name(&a));
        log = read_file(logpath, &len);

        if (log != NULL && a.progress != 0) {
            first_error(log, l1, l2, sizeof l1);
            tell(&a, "error %s", l1);
            tell(&a, "error2 %s", l2);
        }

        free(log);
        unlink(res);
        run_passes(dir, engine, name, 0, &a, &o);

        if (a.via_dvi != 0) {
            char dvi[] = "doc.dvi";
            char dvipath[1024];

            path_join(dvipath, sizeof dvipath, dir, dvi);

            if (access(dvipath, R_OK) == 0) {
                run_prog(dir, "dvipdfmx", dvi, 0);
            }
        }

        collect_tex_errors(dir, name, d, &td);

        if (access(res, R_OK) == 0) {
            status = a.dvi != 0 && a.progress != 0 ? 3 : 4;
        }
    }

    diag_sort(&wd);
    merge_diags(all, &wd, &td);

    if ((status == 0 || status == 3 || status == 4) && all->n > 0) {
        int errors;
        int warnings;

        diag_counts(all, &errors, &warnings);

        if ((a.error_log != 0 || errors > 0) &&
            add_error_log(dir, name, d, &o, &a, engine, all, td.n) != 0) {
            fprintf(stderr, "strprint: the Error Log, the banner and the "
                            "marks could not be added\n");
        }

        if (status == 0) {
            status = 4;
        }
    }

    if (status == 0 || status == 3 || status == 4) {
        snprintf(outpdf, sizeof outpdf, "%s.%s", stem, ext);

        if (a.out != NULL) {
            str_copy(outpdf, sizeof outpdf, a.out);
        }

        {
            char res[1024];

            path_join(res, sizeof res, dir, result_name(&a));

            if (copy_file(res, outpdf) != 0) {
                fprintf(stderr, "strprint: cannot write %s\n", outpdf);
                status = 1;
            } else if (a.dvi != 0) {
                copy_pictures(dir, outpdf);
            }
        }
    }

    report_diags(&a, all);

    if (status != 0 && status != 3 && status != 4) {
        report_failure(a.doc, dir, msg, engine);
    }

    if (a.list != 0 && status != 1) {
        print_long_list(&o, ndis);
    }

    if (a.keep != 0) {
        fprintf(stderr, "strprint: files kept in %s\n", dir);
    } else {
        remove_dir(dir);
    }

    diag_list_free(&wd);
    diag_list_free(&td);
    doc_free(d);

    return status;
}


int
main(int argc, char **argv)
{
    Args a;
    DiagList all;
    int status;

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("strprint (Strata) %s\n", strata_version);
        printf("file format %d\n", STRATA_FILE_FORMAT);

        return 0;
    }

    log_open("strprint");

    if (parse_args(argc, argv, &a) != 0) {
        usage();
        log_warn("strprint: the command line has an option or a value that "
                 "is not valid (%d arguments)", argc - 1);

        return 2;
    }

    log_note("strprint: starts, build type %s, document %s",
             strata_build_name, a.doc != NULL ? a.doc : "(none)");
    diag_list_init(&all);
    status = run(&a, &all);

    if (status != 0) {
        log_error("strprint: ends with status %d for document %s", status,
                  a.doc != NULL ? a.doc : "(none)");
    } else {
        log_note("strprint: ends normally for document %s",
                 a.doc != NULL ? a.doc : "(none)");
    }

    /* The file is written complete on every exit, success or failure. */
    if (a.errors != NULL && diag_file_write(a.errors, &all, status) != 0) {
        fprintf(stderr, "strprint: cannot write %s\n", a.errors);
        log_error("strprint: cannot write the error file %s: %s", a.errors,
                  strerror(errno));

        if (status == 0) {
            status = 1;
        }
    }

    diag_list_free(&all);

    return status;
}
