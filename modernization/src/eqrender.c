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

/* eqrender.c - equation images; see eqrender.h and EQUATIONS.txt. */
#include "util.h"
#include "eqrender.h"
#include "latexmark.h"
#include "png.h"
#include "fonts.h"
#include "sysprog.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define EQ_MAX 4096
/* The most pixels on a side of a picture that a job makes (pdftoppm -W and -H). */
#define EQ_SIDE_MAX 4000
#define EQ_TEXT 1024

/* A job that takes longer than this to typeset fails; its marker is drawn by the
   settings, in milliseconds. */
#define MARKER_MILLIS 5000


/*
 * Close every file descriptor above the three of the terminal in a job.  The
 * job must not hold the connection to the display or the other files of the
 * Editor: with it, the window of an Editor that has ended would stay on the
 * screen until the job is done.
 */
static void
close_inherited_fds(void)
{
    DIR *d = opendir("/proc/self/fd");

    if (d == NULL) {
        for (int fd = 3; fd < 1024; fd++) {
            close(fd);
        }

        return;
    }

    int dfd = dirfd(d);
    struct dirent *e;

    while ((e = readdir(d)) != NULL) {
        int fd = atoi(e->d_name);

        if (fd >= 3 && fd != dfd) {
            close(fd);
        }
    }

    closedir(d);
}


/* Leave a job directory before it is removed; failing to do so is harmless. */
static void
leave_dir(void)
{
    if (chdir("/") != 0) {
        errno = 0;
    }
}

enum {
    ST_QUEUED,
    ST_RUNNING,
    ST_READY,
    ST_FAILED
};

typedef struct {
    char key[20];
    char tex[EQ_TEXT];
    int display;
    int dpi;
    int state;
    pid_t pid;
    long started_ms; /* Monotonic clock, for the limit of a job. */
    EqImage img;
    char err[480];
    char kind; /* Display 4: E for an error of LaTeX, R for a refusal. */
    int taken; /* The error was handed out by eqr_error_take. */
} Eq;

/* The pictures and failures, each in a block of its own, so that the address
   of one stays when the list grows. */
static Eq **eqs;
static int neqs;
static int eqcap;
static int enabled;
static int pic_enabled; /* pdflatex and pdftoppm are there */
static int cfg_dpi = 100;
static unsigned cfg_fg = 0xFFFFFF;
static unsigned cfg_bg = 0x000000;
static int finished; /* a job ended since the last poll */
static char cache[PATH_MAX];
static int (*marker_head)(int pts, FILE *out);
/* The directory of the running job of a LaTeX marker: the parent makes it,
   so that it can clear it when it has to stop the job. */
static char job_dir[PATH_MAX];


static int
have_program(const char *name)
{
    char full[PATH_MAX];

    return sp_find(name, full, sizeof full, NULL) == 0;
}


const char *
eqr_cache_dir(void)
{
    if (cache[0] == '\0') {
        const char *x = getenv("XDG_CACHE_HOME");
        const char *h = getenv("HOME");
        char tmp[PATH_MAX - 8];

        if (x != NULL && x[0] == '/') {
            snprintf(tmp, sizeof tmp, "%s/strata", x);
        } else if (h != NULL) {
            snprintf(tmp, sizeof tmp, "%s/.cache/strata", h);
        } else {
            snprintf(tmp, sizeof tmp, "%s/strata-cache-%d", tmp_dir(),
                     (int)getuid());
        }

        char parent[PATH_MAX - 8];

        str_copy(parent, sizeof parent, tmp);

        char *sl = strrchr(parent, '/');

        if (sl != NULL && sl != parent) {
            *sl = '\0';
            mkdir(parent, 0700);
        }

        mkdir(tmp, 0700);
        snprintf(cache, sizeof cache, "%s/eqn", tmp);
        mkdir(cache, 0700);
    }

    return cache;
}


/* Remove the files that a job of a LaTeX marker leaves and its directory. */
static void
drop_job_dir(void)
{
    static const char *const names[] = {"eq.tex", "eq.aux", "eq.log",
                                        "eq.out", "eq.pdf", "eq.png",
                                        "eq.dep", "eq.inf", NULL};

    if (job_dir[0] == '\0') {
        return;
    }

    for (int i = 0; names[i] != NULL; i++) {
        char path[PATH_MAX + 16];

        snprintf(path, sizeof path, "%s/%s", job_dir, names[i]);
        unlink(path);
    }

    rmdir(job_dir);
    job_dir[0] = '\0';
}


static void
free_all(void)
{
    for (int i = 0; i < neqs; i++) {
        if (eqs[i]->state == ST_RUNNING && eqs[i]->pid > 0) {
            kill(-eqs[i]->pid, SIGKILL);
            waitpid(eqs[i]->pid, NULL, 0);
            drop_job_dir();
        }

        free(eqs[i]->img.rgb);
    }

    for (int i = 0; i < neqs; i++) {
        free(eqs[i]);
    }

    free(eqs);
    eqs = NULL;
    eqcap = 0;
    neqs = 0;
}


/* Stop the job that runs when the program ends, with its programs. */
static void
stop_jobs_at_exit(void)
{
    for (int i = 0; i < neqs; i++) {
        if (eqs[i]->state == ST_RUNNING && eqs[i]->pid > 0) {
            kill(-eqs[i]->pid, SIGKILL);
            waitpid(eqs[i]->pid, NULL, 0);
            eqs[i]->state = ST_FAILED;
        }
    }

    drop_job_dir();
}


void
eqr_configure(int dpi, unsigned fg, unsigned bg)
{
    static int registered;

    if (registered == 0) {
        registered = 1;
        atexit(stop_jobs_at_exit);
    }

    if (enabled != 0 && (dpi != cfg_dpi || fg != cfg_fg || bg != cfg_bg)) {
        free_all();
    }

    cfg_dpi = dpi;
    cfg_fg = fg;
    cfg_bg = bg;
    enabled = have_program("latex") != 0 && have_program("dvipng") != 0;
    pic_enabled = have_program("pdflatex") != 0 &&
                  have_program("pdftoppm") != 0;
}


int
eqr_enabled(void)
{
    return enabled;
}


int
eqr_pictures_enabled(void)
{
    return pic_enabled;
}


/* The text of an equation as latex gets it: the lines joined. */
static void
flatten(const char *tex, char *out, size_t cap)
{
    size_t n = 0;

    for (; *tex != '\0' && n + 1 < cap; tex++) {
        out[n++] = *tex == '\n' || *tex == '\r' ? ' ' : *tex;
    }

    out[n] = '\0';
}


const char *
eqr_unsafe(const char *tex)
{
    static const char *const bad[] = {
        "input",     "include",   "openin",   "openout", "read",
        "write",     "immediate", "catcode",  "csname",  "endcsname",
        "usepackage", "RequirePackage", "documentclass", "newwrite",
        "newread",   "closein",   "closeout", "special", "pdfshellescape",
        "scantokens", "endinput", "undefined", NULL};

    for (const char *p = tex; *p != '\0'; p++) {
        if (*p != '\\') {
            continue;
        }

        for (int i = 0; bad[i] != NULL; i++) {
            size_t n = strlen(bad[i]);

            if (strncmp(p + 1, bad[i], n) == 0 &&
                !((p[1 + n] >= 'a' && p[1 + n] <= 'z') ||
                  (p[1 + n] >= 'A' && p[1 + n] <= 'Z'))) {
                return "That TeX command is not allowed in an equation";
            }
        }
    }

    return NULL;
}


/*
 * The key of a picture in the cache: the display, the resolution, the colors
 * and the text.  The pictures of a LaTeX marker (display 4) have the number
 * of the rule that made them: a refusal that an earlier rule kept in the
 * cache (a block taller than two lines, a command that was never drawn) must
 * not hold a marker in the Short form after the rule has changed.
 */
static void
make_key(const char *tex, int display, int dpi, char *key)
{
    uint64_t h = 1469598103934665603ULL;
    char buf[EQ_TEXT + 64];
    int n = snprintf(buf, sizeof buf, "%d%s|%d|%06x|%06x|%s", display,
                     display == 4 ? "r2" : "", dpi, cfg_fg, cfg_bg, tex);

    for (int i = 0; i < n; i++) {
        h ^= u8(buf[i]);
        h *= 1099511628211ULL;
    }

    snprintf(key, 20, "%016llx", (unsigned long long)h);
}


static Eq *
find(const char *tex, int display, int dpi, char *flat)
{
    char key[20];

    flatten(tex, flat, EQ_TEXT);
    make_key(flat, display, dpi, key);

    for (int i = 0; i < neqs; i++) {
        if (strcmp(eqs[i]->key, key) == 0) {
            return eqs[i];
        }
    }

    return NULL;
}


static void
cache_path(const Eq *q, const char *ext, char *out, size_t cap)
{
    char dir[PATH_MAX - 64];
    int n;

    /* A directory that does not fit gives a path that names no file. */
    if (strlen(eqr_cache_dir()) >= sizeof dir) {
        out[0] = '\0';

        return;
    }

    str_copy(dir, sizeof dir, eqr_cache_dir());
    n = snprintf(out, cap, "%s/%.24s.%.8s", dir, q->key, ext);

    if (n < 0 || usz(n) >= cap) {
        out[0] = '\0';
    }
}


/*
 * Does the picture hold anything but the color of the window?  A pixel
 * counts when it differs from the window color by more than the rounding of
 * the colors.
 */
static int
has_ink(const uint8_t *rgb, int w, int h)
{
    int bg[3] = {iun((cfg_bg >> 16) & 255), iun((cfg_bg >> 8) & 255),
                 iun(cfg_bg & 255)};
    size_t n = usz(w) * usz(h);

    for (size_t i = 0; i < n; i++) {
        int d = 0;

        for (int c = 0; c < 3; c++) {
            int v = rgb[i * 3 + usz(c)] - bg[c];

            d += v < 0 ? -v : v;
        }

        if (d > 40) {
            return 1;
        }
    }

    return 0;
}


/* Read the result of a finished job, or of an earlier run. */
static int
load_result(Eq *q)
{
    char path[PATH_MAX];
    int w;
    int h;
    uint8_t *rgb;

    cache_path(q, "png", path, sizeof path);

    if (png_load(path, cfg_bg, &w, &h, &rgb) != 0) {
        /* The picture of a LaTeX marker that has no ink says nothing. */
        if (q->display == 4 && has_ink(rgb, w, h) == 0) {
            free(rgb);
            str_copy(q->err, sizeof q->err, "it makes nothing visible");
            q->kind = 'R';
            q->state = ST_FAILED;

            return 1;
        }

        q->img.w = w;
        q->img.h = h;
        q->img.rgb = rgb;
        q->img.depth = 0;
        q->state = ST_READY;
        cache_path(q, "dep", path, sizeof path);

        FILE *fp = fopen(path, "r");

        if (fp != NULL) {
            char line[32];

            if (fgets(line, sizeof line, fp) != NULL) {
                int d = png_parse_depth(line, h);

                /* A depth that is not valid is taken as zero. */
                q->img.depth = d >= 0 ? d : 0;
            }

            fclose(fp);
        }

        return 1;
    }

    cache_path(q, "err", path, sizeof path);

    FILE *fp = fopen(path, "r");

    if (fp == NULL) {
        char png[PATH_MAX];

        /* A picture file that was refused is reported with the reason. */
        cache_path(q, "png", png, sizeof png);

        if (access(png, F_OK) == 0) {
            str_copy(q->err, sizeof q->err, png_error());
            q->kind = 'R';
            q->state = ST_FAILED;

            return 1;
        }
    }

    if (fp != NULL) {
        size_t got = fread(q->err, 1, sizeof q->err - 1, fp);

        q->err[got] = '\0';
        fclose(fp);

        while (got > 0 &&
               (q->err[got - 1] == '\n' || q->err[got - 1] == '\r')) {
            q->err[--got] = '\0';
        }

        /* The file of a marker starts with the kind: E: or R:. */
        q->kind = 'R';

        if (q->display == 4 && q->err[1] == ':' &&
            (q->err[0] == 'E' || q->err[0] == 'R')) {
            q->kind = q->err[0];
            memmove(q->err, q->err + 2, strlen(q->err + 2) + 1);
        }

        if (q->err[0] == '\0') {
            str_copy(q->err, sizeof q->err, "The equation did not typeset");
        }

        q->state = ST_FAILED;

        return 1;
    }

    return 0;
}


/* The monotonic clock in milliseconds. */
static long
now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}


/* --------------------------------------------------------------- jobs */
static int
run_program(const char *const argv[], const char *out)
{
    char *av[24];
    char prog[PATH_MAX];
    int n = 0;

    /* The program is looked for in the fixed folders of the system and run
       by its full path: PATH is never searched. */
    if (sp_find(argv[0], prog, sizeof prog, NULL) != 0) {
        return -1;
    }

    av[0] = strdup(prog);

    for (n = 1; argv[n] != NULL && n < 22; n++) {
        av[n] = strdup(argv[n]);
    }

    av[n] = NULL;

    pid_t p = fork();

    if (p < 0) {
        for (int i = 0; i < n; i++) {
            free(av[i]);
        }

        return -1;
    }

    if (p == 0) {
        int fd = open(out != NULL ? out : "/dev/null",
                      O_WRONLY | O_CREAT | O_TRUNC, 0600);

        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);
        }

        sp_child_env(av[0]);
        execv(av[0], av);
        _exit(127);
    }

    int st = 0;
    int w = waitpid(p, &st, 0);

    for (int i = 0; i < n; i++) {
        free(av[i]);
    }

    if (w < 0) {
        return -1;
    }

    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}


static void
write_text(const char *path, const char *text)
{
    FILE *fp = fopen(path, "w");

    if (fp != NULL) {
        fputs(text, fp);
        fclose(fp);
    }
}


/*
 * The pixels that pdftoppm makes of the first page of a PDF file at dpi (the
 * size of the page rounded up), as the decimal texts of the options
 * -scale-to-x and -scale-to-y.  The size comes from pdfinfo.  Returns 1, or 0
 * when pdfinfo is missing, when it fails or when its answer cannot be read.
 */
static int
page_pixels(const char *pdf, int dpi, char *wtext, size_t wcap, char *htext,
            size_t hcap)
{
    const char *const info[] = {"pdfinfo", pdf, NULL};
    char text[2048];
    int w;
    int h;

    if (run_program(info, "eq.inf") != 0) {
        return 0;
    }

    FILE *fp = fopen("eq.inf", "r");

    if (fp == NULL) {
        return 0;
    }

    size_t n = fread(text, 1, sizeof text - 1, fp);

    fclose(fp);
    text[n] = '\0';

    if (png_page_pixels(text, dpi, &w, &h) == 0) {
        return 0;
    }

    snprintf(wtext, wcap, "%d", w);
    snprintf(htext, hcap, "%d", h);

    return 1;
}


/* A color as dvipng reads it: "rgb 0.784 0.784 0.784". */
static void
color_arg(unsigned c, char *out, size_t cap)
{
    int r = isz((c >> 16) & 0xFF) * 1000 / 255;
    int g = isz((c >> 8) & 0xFF) * 1000 / 255;
    int b = isz(c & 0xFF) * 1000 / 255;

    snprintf(out, cap, "rgb %d.%03d %d.%03d %d.%03d", r / 1000, r % 1000,
             g / 1000, g % 1000, b / 1000, b % 1000);
}



/* Copy the picture that a job made to the cache under its name. */
static int
deliver_png(const char *png_tmp, const char *png)
{
    FILE *in = fopen("eq.png", "rb");
    FILE *out = fopen(png_tmp, "wb");
    char buf[8192];
    size_t k;
    int ok = in != NULL && out != NULL;

    while (ok != 0 && (k = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, k, out) != k) {
            ok = 0;
        }
    }

    if (in != NULL) {
        fclose(in);
    }

    if (out != NULL) {
        fclose(out);
    }

    return ok != 0 && rename(png_tmp, png) == 0 ? 0 : -1;
}


/*
 * The work of a picture job, in the child.  q->tex is "path|width|height|
 * scale" (the sizes in thousandths of an inch and of one).  The file is
 * linked into a private directory under a name that is safe in TeX, typeset
 * on a page of its own by pdflatex, and the page is made into a PNG by
 * pdftoppm.  PNG, JPEG, PDF and EPS files are what pdflatex takes.
 */
static void
picture_main(const Eq *q)
{
    char dir[PATH_MAX];
    char png[PATH_MAX];
    char png_tmp[PATH_MAX];
    char errp[PATH_MAX];
    char depp[PATH_MAX];
    char spec[EQ_TEXT];
    char tex[1200];
    char opt[200] = "";
    char dpi[16];
    char link[32];
    char *f[5] = {NULL, NULL, NULL, NULL, NULL};
    const char *unit = "in";
    int wid;
    int hei;
    int scale;
    const char *ext;

    setpgid(0, 0);
    cache_path(q, "png", png, sizeof png);
    cache_path(q, "tmp", png_tmp, sizeof png_tmp);
    cache_path(q, "err", errp, sizeof errp);
    cache_path(q, "dep", depp, sizeof depp);
    str_copy(spec, sizeof spec, q->tex);

    char *at = spec;

    for (int i = 0; i < 5 && at != NULL; i++) {
        f[i] = at;
        at = strchr(at, '|');

        if (at != NULL) {
            *at++ = '\0';
        }
    }

    if (f[3] == NULL) {
        write_text(errp, "The picture has no file name\n");
        _exit(1);
    }

    /* The unit is one of the units of TeX, else inches. */
    if (f[4] != NULL) {
        static const char *const units[] = {"in", "cm", "mm", "pt", "bp",
                                            "pc", "dd", "cc", "sp", "em",
                                            "ex", NULL};

        for (int i = 0; units[i] != NULL; i++) {
            if (strcmp(f[4], units[i]) == 0) {
                unit = units[i];
            }
        }
    }

    wid = atoi(f[1]);
    hei = atoi(f[2]);
    scale = atoi(f[3]);
    ext = strrchr(f[0], '.');

    if (ext == NULL || (strcasecmp(ext, ".png") != 0 &&
                        strcasecmp(ext, ".jpg") != 0 &&
                        strcasecmp(ext, ".jpeg") != 0 &&
                        strcasecmp(ext, ".pdf") != 0 &&
                        strcasecmp(ext, ".eps") != 0)) {
        write_text(errp, "LaTeX cannot read that kind of picture file "
                         "(use PNG, JPEG, PDF or EPS)\n");
        _exit(1);
    }

    /* The link is made in another directory, so the name must be full. */
    char full[PATH_MAX];

    if (realpath(f[0], full) == NULL || access(full, R_OK) != 0) {
        write_text(errp, "The picture file cannot be read\n");
        _exit(1);
    }

    f[0] = full;

    tmp_path(dir, sizeof dir, "strata-eqXXXXXX");

    if (mkdtemp(dir) == NULL || chdir(dir) != 0) {
        write_text(errp, "No temporary directory for latex\n");
        _exit(1);
    }

    snprintf(link, sizeof link, "pic%s", strcasecmp(ext, ".jpeg") == 0 ?
                                             ".jpg" :
                                             ext);

    for (char *c = link; *c != '\0'; c++) {
        *c = chr(to_lower(*c));
    }

    if (symlink(f[0], link) != 0) {
        write_text(errp, "Cannot link the picture file\n");
        leave_dir();
        rmdir(dir);
        _exit(1);
    }

    wid = wid > 99000 ? 99000 : wid;
    hei = hei > 99000 ? 99000 : hei;
    scale = scale > 99000 ? 99000 : scale;

    char ow[40] = "";
    char oh[40] = "";
    char os[40] = "";

    if (wid > 0) {
        snprintf(ow, sizeof ow, "width=%d.%03d%s,", wid / 1000, wid % 1000,
                 unit);
    }

    if (hei > 0) {
        snprintf(oh, sizeof oh, "height=%d.%03d%s,", hei / 1000, hei % 1000,
                 unit);
    }

    if (wid <= 0 && hei <= 0 && scale > 0) {
        snprintf(os, sizeof os, "scale=%d.%03d,", scale / 1000, scale % 1000);
    }

    snprintf(opt, sizeof opt, "%s%s%s", ow, oh, os);
    setenv("openin_any", "p", 1);
    setenv("openout_any", "p", 1);
    snprintf(tex, sizeof tex,
             "\\documentclass[border=1pt]{standalone}\n"
             "\\usepackage{graphicx}\n"
             "\\begin{document}\n"
             "\\includegraphics[%s]{%s}\n"
             "\\end{document}\n",
             opt, link);
    write_text("eq.tex", tex);

    const char *const latex[] = {
        "pdflatex",       strcasecmp(ext, ".eps") == 0 ? "-shell-restricted" :
                                                         "-no-shell-escape",
        "-interaction=nonstopmode", "-halt-on-error", "eq.tex", NULL};
    int rc = run_program(latex, "eq.out");

    if (rc == 0) {
        snprintf(dpi, sizeof dpi, "%d", q->dpi);

        const char *const ppm[] = {
            "pdftoppm", "-png",  "-r", dpi,  "-singlefile", "-x", "0",
            "-y",       "0",     "-W", "4000", "-H",        "4000", "eq.pdf",
            "eq",       NULL};

        rc = run_program(ppm, "eq.dep");
    }

    if (rc == 0 && access("eq.png", R_OK) == 0 &&
        deliver_png(png_tmp, png) == 0) {
        write_text(depp, "0\n");
        rc = 0;
    } else {
        write_text(errp, "LaTeX could not make the picture (is the file a "
                         "valid image?)\n");
        rc = 1;
    }

    unlink(link);
    unlink("eq.tex");
    unlink("eq.aux");
    unlink("eq.log");
    unlink("eq.out");
    unlink("eq.pdf");
    unlink("eq.png");
    unlink("eq.dep");
    leave_dir();
    rmdir(dir);
    _exit(rc);
}


/* A color between two colors: t is 0 for a, 1000 for b. */
static void
mix_color(unsigned a, unsigned b, int t, char *out, size_t cap)
{
    unsigned rgb = 0;

    t = t < 0 ? 0 : (t > 1000 ? 1000 : t);

    for (int sh = 0; sh <= 16; sh += 8) {
        int ca = iun((a >> sh) & 0xFFu);
        int cb = iun((b >> sh) & 0xFFu);

        rgb |= uns(ca + (cb - ca) * t / 1000) << sh;
    }

    snprintf(out, cap, "%06X", rgb & 0xFFFFFFu);
}


/* The text of a legend as TeX gets it: the special characters escaped. */
static void
legend_escape(const char *in, char *out, size_t cap)
{
    size_t n = 0;

    for (; *in != '\0' && n + 24 < cap; in++) {
        unsigned char c = u8(*in);

        if (c < 32 || c > 126) {
            out[n++] = '?';
        } else if (strchr("#$%&_{}", c) != NULL) {
            out[n++] = '\\';
            out[n++] = chr(c);
        } else if (c == '~') {
            n += usz(snprintf(out + n, cap - n, "\\textasciitilde{}"));
        } else if (c == '^') {
            n += usz(snprintf(out + n, cap - n, "\\textasciicircum{}"));
        } else if (c == '\\') {
            n += usz(snprintf(out + n, cap - n, "\\textbackslash{}"));
        } else {
            out[n++] = chr(c);
        }
    }

    out[n] = '\0';
}


/*
 * The work of a keycap job, in the child.  q->tex is "legend|foreground|
 * background|scale|font" (tenths, tenths, thousandths and a font number of
 * the TeX Font Association list, fonts.h).  The legend is set in
 * the roman face inside a box with rounded corners, as the format describes
 * it (docs/MARKERS.txt): the box is 1.33 em high, a third of an em of it
 * below the baseline, with 0.31 em at each side of the legend and corners of
 * 0.2 em.  Foreground 0.0 is the color of the text of the editor and
 * Background 100.0 the color of its window, the values in between mix them.
 */
static void
keycap_main(const Eq *q)
{
    char dir[PATH_MAX];
    char png[PATH_MAX];
    char png_tmp[PATH_MAX];
    char errp[PATH_MAX];
    char depp[PATH_MAX];
    char spec[EQ_TEXT];
    char leg[200];
    char tex[1800];
    char dpi[16];
    char pkg[160];
    const char *fam = "\\rmfamily";
    FontEntry fonts[FONT_COUNT];
    char kfg[16];
    char kbg[16];
    char pbg[16];
    char *f[5] = {NULL, NULL, NULL, NULL, NULL};
    int fnt;
    int fgv;
    int bgv;
    int scl;

    setpgid(0, 0);
    cache_path(q, "png", png, sizeof png);
    cache_path(q, "tmp", png_tmp, sizeof png_tmp);
    cache_path(q, "err", errp, sizeof errp);
    cache_path(q, "dep", depp, sizeof depp);
    str_copy(spec, sizeof spec, q->tex);

    char *at = spec;

    for (int i = 0; i < 5 && at != NULL; i++) {
        f[i] = at;
        at = i < 4 ? strchr(at, '|') : NULL;

        if (at != NULL) {
            *at++ = '\0';
        }
    }

    if (f[4] == NULL) {
        write_text(errp, "The keycap has no legend\n");
        _exit(1);
    }

    fgv = atoi(f[1]);
    bgv = atoi(f[2]);
    scl = atoi(f[3]);
    scl = scl < 1 ? 1 : (scl > 10000 ? 10000 : scl);
    fnt = atoi(f[4]);
    pkg[0] = '\0';

    /* The package of the font is left out when it is not installed or when
       its name is not safe to write; the legend is then in the roman face. */
    if (fnt >= 1 && fnt <= FONT_COUNT) {
        fonts_load(fonts);

        const FontEntry *fe = &fonts[fnt - 1];

        if (fonts_word_ok(fe->package) != 0 &&
            fonts_word_ok(fe->options) != 0 && fonts_installed(fe) != 0) {
            fonts_package_line(fe, pkg, sizeof pkg);
            fam = fonts_family_command(fe->family);
        }
    }

    legend_escape(f[0], leg, sizeof leg);
    mix_color(cfg_fg, cfg_bg, fgv * 10, kfg, sizeof kfg);
    mix_color(cfg_fg, cfg_bg, bgv * 10, kbg, sizeof kbg);
    mix_color(cfg_bg, cfg_bg, 0, pbg, sizeof pbg);

    tmp_path(dir, sizeof dir, "strata-eqXXXXXX");

    if (mkdtemp(dir) == NULL || chdir(dir) != 0) {
        write_text(errp, "No temporary directory for latex\n");
        _exit(1);
    }

    setenv("openin_any", "p", 1);
    setenv("openout_any", "p", 1);
    snprintf(tex, sizeof tex,
             "\\documentclass[border=1pt]{standalone}\n"
             "\\usepackage{tikz}\n"
             "%s\n"
             "\\definecolor{kfg}{HTML}{%s}\n"
             "\\definecolor{kbg}{HTML}{%s}\n"
             "\\definecolor{pbg}{HTML}{%s}\n"
             "\\begin{document}\n"
             "%s\n"
             "\\pagecolor{pbg}\n"
             "\\newlength{\\kw}\n"
             "\\settowidth{\\kw}{%s}\n"
             "\\scalebox{%d.%03d}{\\begin{tikzpicture}[baseline=0pt]\n"
             "\\draw[draw=kfg,fill=kbg,line width=0.4pt,rounded corners=0.2em]"
             " (0,-0.33em) rectangle ({\\kw+0.62em},1em);\n"
             "\\node[anchor=base west,inner sep=0pt,text=kfg] at (0.31em,0)"
             " {%s};\n"
             "\\end{tikzpicture}}\n"
             "\\end{document}\n",
             pkg, kfg, kbg, pbg, fam, leg, scl / 1000, scl % 1000, leg);
    write_text("eq.tex", tex);

    const char *const latex[] = {"pdflatex", "-no-shell-escape",
                                 "-interaction=nonstopmode",
                                 "-halt-on-error", "eq.tex", NULL};
    int rc = run_program(latex, "eq.out");

    if (rc == 0) {
        char pw[16];
        char ph[16];

        snprintf(dpi, sizeof dpi, "%d", q->dpi);

        /*
         * The page of the keycap ends inside a pixel, almost always, and
         * pdftoppm writes that last pixel too, but in the paper color
         * (white) and not in the color of the page: a bar of one pixel at
         * the right of the key, and sometimes a line below it.  The page
         * is scaled to the whole pixels that pdftoppm makes of it (a
         * change of less than one pixel in the size), so that the color of
         * the page covers every pixel.  If the size of the page is not
         * known the picture is made as it was.
         */
        if (page_pixels("eq.pdf", q->dpi, pw, sizeof pw, ph, sizeof ph) != 0) {
            const char *const ppm[] = {
                "pdftoppm", "-png", "-scale-to-x", pw, "-scale-to-y", ph,
                "-singlefile", "-x", "0", "-y", "0", "-W", "4000", "-H",
                "4000", "eq.pdf", "eq", NULL};

            rc = run_program(ppm, "eq.dep");
        } else {
            const char *const ppm[] = {
                "pdftoppm", "-png", "-r", dpi, "-singlefile", "-x", "0",
                "-y", "0", "-W", "4000", "-H", "4000", "eq.pdf", "eq", NULL};

            rc = run_program(ppm, "eq.dep");
        }
    }

    if (rc == 0 && access("eq.png", R_OK) == 0 &&
        deliver_png(png_tmp, png) == 0) {
        char depth[24];

        /* Below the baseline: the box (0.33 em of 10 pt), half of the line,
           and the border of 1 pt, all scaled but the border. */
        snprintf(depth, sizeof depth, "%d\n",
                 (int)((3.5 * scl / 1000.0 + 1.0) * q->dpi / 72.27 + 0.5));
        write_text(depp, depth);
        rc = 0;
    } else {
        write_text(errp, "LaTeX could not make the keycap\n");
        rc = 1;
    }

    unlink("eq.tex");
    unlink("eq.aux");
    unlink("eq.log");
    unlink("eq.out");
    unlink("eq.pdf");
    unlink("eq.png");
    unlink("eq.dep");
    unlink("eq.inf");
    leave_dir();
    rmdir(dir);
    _exit(rc);
}

/* Hundredths of a point from a size that TeX wrote, such as 12.34pt. */
static int
centi_points(double pt)
{
    return (int)(pt * 100.0 + (pt < 0.0 ? -0.5 : 0.5));
}


/*
 * The first line of the message of LaTeX for an error: the line that starts
 * with an exclamation mark in the log, without it.  Returns 1 when there
 * was one.
 */
static int
latex_first_error(const char *log, char *line, size_t cap)
{
    FILE *fp = fopen(log, "r");
    char buf[400];
    int found = 0;

    while (fp != NULL && found == 0 && fgets(buf, sizeof buf, fp) != NULL) {
        if (buf[0] == '!') {
            buf[strcspn(buf, "\r\n")] = '\0';
            str_copy(line, cap, buf + 1 + (buf[1] == ' ' ? 1 : 0));
            found = 1;
        }
    }

    if (fp != NULL) {
        fclose(fp);
    }

    return found;
}


/*
 * The text of the marker, in the body of the file for latex.  The text is
 * typeset in a box of its own, in the color of the text of the window; the
 * size of the box is told on the terminal (the line LMK: width, height and
 * depth), and the box is shipped out as a page of exactly its size, with a
 * border of one point, on the color of the window.
 */
static void
marker_body(FILE *fp, const char *text)
{
    fprintf(fp,
            "\\definecolor{lmfg}{HTML}{%06X}\n"
            "\\definecolor{lmbg}{HTML}{%06X}\n"
            "\\pagestyle{empty}\n"
            "\\setbox0\\hbox{\\color{lmfg}\\ignorespaces %s\\unskip}\n"
            "\\typeout{LMK: \\the\\wd0 \\the\\ht0 \\the\\dp0 }\n"
            "\\setbox1\\hbox{\\kern1pt\\box0\\kern1pt}\n"
            "\\setbox1\\hbox{\\color{lmbg}\\vrule width\\wd1 "
            "height\\dimexpr\\ht1+1pt\\relax depth\\dimexpr\\dp1+1pt\\relax"
            "\\kern-\\wd1 \\box1}\n"
            "\\pdfpagewidth=\\wd1 \\pdfpageheight=\\dimexpr\\ht1+\\dp1\\relax\n"
            "\\pdfhorigin=0pt \\pdfvorigin=0pt \\hoffset=0pt \\voffset=0pt\n"
            "\\shipout\\box1\n"
            "\\end{document}\n",
            cfg_fg & 0xFFFFFF, cfg_bg & 0xFFFFFF, text);
}


/*
 * The job of a LaTeX marker (display 4), in the child.  The text is "points|
 * text": the size of the text of the document and what the marker holds.  It
 * is typeset with the preamble of the Printer (marker_head), alone in a box,
 * and the page that holds the box is made a picture.  The kind of a failure
 * is the first letter of the error file: E for an error that LaTeX told, R
 * for a refusal (no width, no ink, too large).  The picture has the
 * depth of the box below the baseline, so that it stands on the line of the
 * text like an inline equation.
 */
static void
latexmark_main(const Eq *q)
{
    char dir[PATH_MAX];
    char png[PATH_MAX];
    char png_tmp[PATH_MAX];
    char errp[PATH_MAX];
    char depp[PATH_MAX];
    char spec[EQ_TEXT];
    char line[400];
    char msg[420];
    double wd = 0.0;
    double ht = 0.0;
    double dp = 0.0;
    int pts;
    int rc;
    const char *text;
    FILE *fp;

    setpgid(0, 0);
    cache_path(q, "png", png, sizeof png);
    cache_path(q, "tmp", png_tmp, sizeof png_tmp);
    cache_path(q, "err", errp, sizeof errp);
    cache_path(q, "dep", depp, sizeof depp);
    str_copy(spec, sizeof spec, q->tex);
    pts = atoi(spec);
    text = strchr(spec, '|');

    if (text == NULL || pts < 8 || pts > 20 || marker_head == NULL) {
        write_text(errp, "R:the picture cannot be made\n");
        _exit(1);
    }

    text++;
    str_copy(dir, sizeof dir, job_dir);

    if (dir[0] == '\0' || chdir(dir) != 0) {
        write_text(errp, "R:no temporary directory for latex\n");
        _exit(1);
    }

    setenv("openin_any", "p", 1);
    setenv("openout_any", "p", 1);
    fp = fopen("eq.tex", "w");

    if (fp == NULL || marker_head(pts, fp) != 0) {
        write_text(errp, "R:the file for latex cannot be written\n");
        leave_dir();
        _exit(1);
    }

    marker_body(fp, text);
    fclose(fp);

    const char *const latex[] = {"pdflatex", "-no-shell-escape",
                                 "-interaction=nonstopmode",
                                 "-halt-on-error", "eq.tex", NULL};

    rc = run_program(latex, "eq.out");

    if (rc != 0) {
        if (latex_first_error("eq.log", line, sizeof line) != 0) {
            snprintf(msg, sizeof msg, "E:%s\n", line);
        } else {
            snprintf(msg, sizeof msg, "R:latex did not typeset it\n");
        }

        write_text(errp, msg);
        rc = 1;
    } else {
        fp = fopen("eq.out", "r");

        while (fp != NULL && fgets(line, sizeof line, fp) != NULL) {
            if (strncmp(line, "LMK: ", 5) == 0) {
                sscanf(line + 5, "%lfpt %lfpt %lfpt", &wd, &ht, &dp);
            }
        }

        if (fp != NULL) {
            fclose(fp);
        }

        /*
         * The checks of the size.  A box with no width shows nothing.  The
         * height is not limited: a picture that is taller than the lines
         * around it is shown on rows of its own, as a Picture marker is.
         * The size is limited as that of a Picture marker is: the picture
         * job makes at most 4000 by 4000 pixels.
         */
        int w100 = centi_points(wd);

        if (w100 < 50) {
            write_text(errp, "R:it makes nothing visible\n");
            rc = 1;
        } else {
            char pw[16];
            char ph[16];
            char dpi[16];
            int eff = q->dpi * 10 / pts;
            int known;

            /* The picture is as large as the text of the window: the size
               of the text of the document is one line of the window. */
            snprintf(dpi, sizeof dpi, "%d", eff);
            known = page_pixels("eq.pdf", eff, pw, sizeof pw, ph, sizeof ph);

            if (known != 0 && (atoi(pw) > EQ_SIDE_MAX ||
                               atoi(ph) > EQ_SIDE_MAX)) {
                snprintf(msg, sizeof msg, "R:it is larger than %d by %d "
                                          "pixels\n", EQ_SIDE_MAX,
                         EQ_SIDE_MAX);
                write_text(errp, msg);
                rc = 1;
            } else {
                if (known != 0) {
                    const char *const ppm[] = {
                        "pdftoppm", "-png", "-scale-to-x", pw, "-scale-to-y",
                        ph, "-singlefile", "-x", "0", "-y", "0", "-W", "4000",
                        "-H", "4000", "eq.pdf", "eq", NULL};

                    rc = run_program(ppm, "eq.dep");
                } else {
                    const char *const ppm[] = {
                        "pdftoppm", "-png", "-r", dpi, "-singlefile", "-x",
                        "0", "-y", "0", "-W", "4000", "-H", "4000", "eq.pdf",
                        "eq", NULL};

                    rc = run_program(ppm, "eq.dep");
                }

                if (rc == 0 && access("eq.png", R_OK) == 0 &&
                    deliver_png(png_tmp, png) == 0) {
                    char depth[24];
                    int d100 = centi_points(dp + 1.0);

                    snprintf(depth, sizeof depth, "%d\n", d100 * eff / 7227);
                    write_text(depp, depth);
                    rc = 0;
                } else {
                    write_text(errp, "R:the picture could not be made\n");
                    rc = 1;
                }
            }
        }
    }

    unlink("eq.tex");
    unlink("eq.aux");
    unlink("eq.log");
    unlink("eq.out");
    unlink("eq.pdf");
    unlink("eq.png");
    unlink("eq.dep");
    unlink("eq.inf");
    leave_dir();
    rmdir(dir);
    _exit(rc);
}


/* The work of a job, in the child: latex, then dvipng. */
static void
job_main(const Eq *q)
{
    char dir[PATH_MAX];
    char png[PATH_MAX];
    char png_tmp[PATH_MAX];
    char errp[PATH_MAX];
    char depp[PATH_MAX];
    char tex[EQ_TEXT + 400];
    char dpi[16];
    char fg[48];
    char bg[48];

    close_inherited_fds();

    if (q->display == 2) {
        picture_main(q);
    }

    if (q->display == 3) {
        keycap_main(q);
    }

    if (q->display == 4) {
        latexmark_main(q);
    }

    setpgid(0, 0);
    cache_path(q, "png", png, sizeof png);
    cache_path(q, "tmp", png_tmp, sizeof png_tmp);
    cache_path(q, "err", errp, sizeof errp);
    cache_path(q, "dep", depp, sizeof depp);

    tmp_path(dir, sizeof dir, "strata-eqXXXXXX");

    if (mkdtemp(dir) == NULL || chdir(dir) != 0) {
        write_text(errp, "No temporary directory for latex\n");
        _exit(1);
    }

    setenv("openin_any", "p", 1);
    setenv("openout_any", "p", 1);
    snprintf(tex, sizeof tex,
             "\\documentclass[preview,border=2pt]{standalone}\n"
             "\\usepackage{amsmath,amssymb}\n"
             "\\begin{document}\n"
             "%s%s%s\n"
             "\\end{document}\n",
             q->display != 0 ? "$\\displaystyle " : "$", q->tex, "$");
    write_text("eq.tex", tex);

    const char *const latex[] = {
        "latex",         "-no-shell-escape", "-interaction=nonstopmode",
        "-halt-on-error", "eq.tex",          NULL};
    int rc = run_program(latex, "eq.out");

    if (rc != 0) {
        char line[400] = "The equation did not typeset";
        FILE *fp = fopen("eq.log", "r");
        int seen = 0;

        while (fp != NULL && fgets(tex, sizeof tex, fp) != NULL) {
            tex[strcspn(tex, "\r\n")] = '\0';

            if (seen == 0 && tex[0] == '!') {
                str_copy(line, sizeof line, tex + 1 + (tex[1] == ' ' ? 1 : 0));
                seen = 1;
            } else if (seen != 0 && seen < 4 && tex[0] != '\0') {
                size_t used = strlen(line);

                if (used + 2 < sizeof line) {
                    line[used++] = '\n';
                    str_copy(line + used, sizeof line - used, tex);
                }

                seen++;
            } else if (seen >= 4) {
                break;
            }
        }

        if (fp != NULL) {
            fclose(fp);
        }

        strcat(line, "\n");
        write_text(errp, line);
        unlink("eq.tex");
        unlink("eq.aux");
        unlink("eq.log");
        unlink("eq.out");
        unlink("eq.dvi");
        leave_dir();
        rmdir(dir);
        _exit(1);
    }

    snprintf(dpi, sizeof dpi, "%d", q->dpi);
    color_arg(cfg_fg, fg, sizeof fg);
    color_arg(cfg_bg, bg, sizeof bg);

    const char *const dvipng[] = {
        "dvipng", "-q", "-D", dpi, "-T", "tight", "-fg", fg, "-bg", bg,
        "--depth*", "-o", "eq.png", "eq.dvi", NULL};

    rc = run_program(dvipng, "eq.dep");

    if (rc == 0 && access("eq.png", R_OK) == 0) {
        FILE *fp = fopen("eq.dep", "r");
        char l[200];
        int depth = 0;

        while (fp != NULL && fgets(l, sizeof l, fp) != NULL) {
            char *d = strstr(l, "depth=");

            if (d != NULL) {
                int v = png_parse_depth(d + 6, PNG_MAX_DEPTH);

                depth = v >= 0 ? v : 0;
            }
        }

        if (fp != NULL) {
            fclose(fp);
        }

        snprintf(l, sizeof l, "%d\n", depth);
        write_text(depp, l);

        /* Copy to the cache under a temporary name, then rename. */
        FILE *in = fopen("eq.png", "rb");
        FILE *out = fopen(png_tmp, "wb");
        char buf[8192];
        size_t k;

        while (in != NULL && out != NULL &&
               (k = fread(buf, 1, sizeof buf, in)) > 0) {
            if (fwrite(buf, 1, k, out) != k) {
                break;
            }
        }

        if (in != NULL) {
            fclose(in);
        }

        if (out != NULL) {
            fclose(out);
        }

        rename(png_tmp, png);
        rc = 0;
    } else {
        write_text(errp, "dvipng did not make an image\n");
        rc = 1;
    }

    unlink("eq.tex");
    unlink("eq.aux");
    unlink("eq.log");
    unlink("eq.out");
    unlink("eq.dvi");
    unlink("eq.png");
    unlink("eq.dep");
    leave_dir();
    rmdir(dir);
    _exit(rc);
}


static void
start_next(void)
{
    for (int i = 0; i < neqs; i++) {
        if (eqs[i]->state == ST_RUNNING) {
            return;
        }
    }

    for (int i = 0; i < neqs; i++) {
        if (eqs[i]->state != ST_QUEUED) {
            continue;
        }

        if (load_result(eqs[i]) != 0) {
            finished = 1;
            continue;
        }

        if (eqs[i]->display == 4) {
            tmp_path(job_dir, sizeof job_dir, "strata-lmXXXXXX");

            if (mkdtemp(job_dir) == NULL) {
                job_dir[0] = '\0';
                str_copy(eqs[i]->err, sizeof eqs[i]->err,
                         "no temporary directory for latex");
                eqs[i]->kind = 'R';
                eqs[i]->state = ST_FAILED;
                finished = 1;
                continue;
            }
        }

        pid_t p = fork();

        if (p < 0) {
            drop_job_dir();
            str_copy(eqs[i]->err, sizeof eqs[i]->err, "Cannot start latex");
            eqs[i]->state = ST_FAILED;
            finished = 1;
            continue;
        }

        if (p == 0) {
            job_main(eqs[i]);
        }

        eqs[i]->pid = p;
        eqs[i]->started_ms = now_ms();
        eqs[i]->state = ST_RUNNING;

        return;
    }
}


int
eqr_poll(void)
{
    for (int i = 0; i < neqs; i++) {
        Eq *q = eqs[i];

        if (q->state != ST_RUNNING) {
            continue;
        }

        int st;
        pid_t r = waitpid(q->pid, &st, WNOHANG);

        if (r == q->pid) {
            drop_job_dir();

            if (load_result(q) == 0) {
                str_copy(q->err, sizeof q->err,
                         "The equation did not typeset");
                q->state = ST_FAILED;
            }

            finished = 1;
        } else if (now_ms() - q->started_ms >= MARKER_MILLIS) {
            /* Every job has the same limit: 5 seconds. */
            kill(-q->pid, SIGKILL);
            waitpid(q->pid, NULL, 0);
            str_copy(q->err, sizeof q->err, "latex took more than 5 seconds");
            q->kind = 'R';
            q->state = ST_FAILED;
            drop_job_dir();
            finished = 1;
        }
    }

    start_next();

    int f = finished;

    finished = 0;

    return f;
}


int
eqr_busy(void)
{
    for (int i = 0; i < neqs; i++) {
        if (eqs[i]->state == ST_RUNNING || eqs[i]->state == ST_QUEUED) {
            return 1;
        }
    }

    return 0;
}


const EqImage *
eqr_request_dpi(const char *tex, int display, int dpi)
{
    char flat[EQ_TEXT];
    Eq *q;

    if (display >= 2 ? pic_enabled == 0 : enabled == 0) {
        return NULL;
    }

    q = find(tex, display, dpi, flat);

    if (q == NULL) {
        if (neqs == EQ_MAX) {
            return NULL;
        }

        if (neqs == eqcap) {
            int cap = eqcap == 0 ? 64 : eqcap * 2;
            Eq **grown = realloc(eqs, usz(cap) * sizeof *grown);

            if (grown == NULL) {
                return NULL;
            }

            eqs = grown;
            eqcap = cap;
        }

        q = calloc(1, sizeof *q);

        if (q == NULL) {
            return NULL;
        }

        eqs[neqs++] = q;
        str_copy(q->tex, sizeof q->tex, flat);
        q->display = display;
        q->dpi = dpi;
        make_key(flat, display, dpi, q->key);

        /* The text of a marker is checked again here, whoever asks. */
        const char *why = display >= 2 && display != 4 ? NULL :
                                                         eqr_unsafe(flat);

        q->kind = 'R';

        if (why != NULL) {
            str_copy(q->err, sizeof q->err, why);
            q->state = ST_FAILED;
        } else if (flat[strspn(flat, " ")] == '\0') {
            str_copy(q->err, sizeof q->err, "The equation is empty");
            q->state = ST_FAILED;
        } else {
            q->state = ST_QUEUED;
            start_next();
        }
    }

    return q->state == ST_READY ? &q->img : NULL;
}


const EqImage *
eqr_peek_dpi(const char *tex, int display, int dpi)
{
    char flat[EQ_TEXT];
    Eq *q = enabled != 0 || pic_enabled != 0 ? find(tex, display, dpi, flat) :
                                               NULL;

    return q != NULL && q->state == ST_READY ? &q->img : NULL;
}


const char *
eqr_error_dpi(const char *tex, int display, int dpi)
{
    char flat[EQ_TEXT];
    Eq *q = find(tex, display, dpi, flat);

    return q != NULL && q->state == ST_FAILED ? q->err : NULL;
}


char
eqr_error_kind(const char *tex, int display, int dpi)
{
    char flat[EQ_TEXT];
    Eq *q = find(tex, display, dpi, flat);

    return q != NULL && q->state == ST_FAILED ? q->kind : '\0';
}


const char *
eqr_error_take(const char *tex, int display, int dpi)
{
    char flat[EQ_TEXT];
    Eq *q = find(tex, display, dpi, flat);

    if (q == NULL || q->state != ST_FAILED || q->taken != 0) {
        return NULL;
    }

    q->taken = 1;

    return q->err;
}


void
eqr_set_marker_head(int (*head)(int pts, FILE *out))
{
    marker_head = head;
}


const EqImage *
eqr_request(const char *tex, int display)
{
    return eqr_request_dpi(tex, display, cfg_dpi);
}


const EqImage *
eqr_peek(const char *tex, int display)
{
    return eqr_peek_dpi(tex, display, cfg_dpi);
}


const char *
eqr_error(const char *tex, int display)
{
    return eqr_error_dpi(tex, display, cfg_dpi);
}


int
eqr_dpi(void)
{
    return cfg_dpi;
}
