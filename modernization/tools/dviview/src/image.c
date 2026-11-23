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
 * Pictures: PNG through libpng, JPEG through jpeg.c, PDF and EPS through
 * ghostscript (and pdfinfo for the page boxes) rendered to a temporary PNG
 * in a private directory.  The results are cached; the PDF and EPS pictures
 * are rendered again when the zoom changes.  The temporary files are removed
 * by image_cleanup().
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <png.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "dviview.h"

#define MAX_PIXELS 16000000L
#define MAX_TMP 512

typedef struct {
    char *key;
    Image im;
    int raster;
    int mark;
} Entry;

static char *dvi_dir;
static char *tmp_dir;
static char *tmp_files[MAX_TMP];
static int ntmp;
static Entry **cache;
static double cache_bytes;
static long ncache;
static char *reported[64];
static int nreported;


void
image_set_dir(const char *dir)
{
    free(dvi_dir);
    dvi_dir = xstrdup(dir);
}


/* Removes the temporary files and the private directory. */
void
image_cleanup(void)
{
    int i;

    for (i = 0; i < ntmp; i++) {
        unlink(tmp_files[i]);
    }

    ntmp = 0;

    if (tmp_dir != NULL) {
        rmdir(tmp_dir);
        tmp_dir = NULL;
    }
}


static void
report_once(const char *what, const char *name)
{
    char line[600];
    int i;

    snprintf(line, sizeof line, "%s %s", what, name);

    for (i = 0; i < nreported; i++) {
        if (strcmp(reported[i], line) == 0) {
            return;
        }
    }

    if (nreported < 64) {
        reported[nreported++] = xstrdup(line);
    }

    viewer_tell("note", "%s: the preview shows a frame in its place; the "
                "PDF is not affected.  Check the file name in the Picture "
                "marker and that the file is in the directory of the "
                "document", line);
}


/* Makes the name of a new temporary file in the private directory. */
static char *
tmp_name(void)
{
    char path[512];

    if (tmp_dir == NULL) {
        char tmpl[512];
        const char *base = getenv("TMPDIR");

        snprintf(tmpl, sizeof tmpl, "%s/dviview-XXXXXX",
                 base != NULL && base[0] != '\0' ? base : "/tmp");

        if (mkdtemp(tmpl) == NULL) {
            return NULL;
        }

        tmp_dir = xstrdup(tmpl);
    }

    if (ntmp >= MAX_TMP) {
        return NULL;
    }

    snprintf(path, sizeof path, "%s/img%d.png", tmp_dir, ntmp);
    tmp_files[ntmp] = xstrdup(path);
    ntmp++;

    return tmp_files[ntmp - 1];
}


static int
file_exists(const char *p)
{
    struct stat st;

    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}


/* Looks the file up next to the DVI file, in the current directory, then
   with kpsewhich. */
static char *
find_file(const char *name)
{
    char path[1100];
    char *k;

    if (name[0] == '/') {
        return file_exists(name) ? xstrdup(name) : NULL;
    }

    if (dvi_dir != NULL) {
        snprintf(path, sizeof path, "%s/%s", dvi_dir, name);

        if (file_exists(path)) {
            return xstrdup(path);
        }
    }

    if (file_exists(name)) {
        /* A name that starts with a minus must not look like an option. */
        snprintf(path, sizeof path, "%s%s", name[0] == '-' ? "./" : "", name);

        return xstrdup(path);
    }

    k = kpse_find(name, NULL, NULL);

    return k;
}


typedef struct {
    char *v[32];
    int n;
} Argv;


static void
argv_add(Argv *a, const char *s)
{
    if (a->n < 30) {
        a->v[a->n++] = xstrdup(s);
        a->v[a->n] = NULL;
    }
}


static void
argv_free(Argv *a)
{
    int i;

    for (i = 0; i < a->n; i++) {
        free(a->v[i]);
    }

    a->n = 0;
}


/* Runs a program without a shell; its output goes to out, if not NULL. */
static int
run_prog(Argv *av, char *out, size_t outsz)
{
    int fds[2];
    pid_t pid;
    int status = 0;
    size_t got = 0;

    if (pipe(fds) != 0) {
        return -1;
    }

    pid = fork();

    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);

        return -1;
    }

    if (pid == 0) {
        int nul = -1;
        int in = open("/dev/null", O_RDONLY);

        close(fds[0]);
        dup2(fds[1], 1);
        nul = open("/dev/null", O_WRONLY);

        if (nul >= 0) {
            dup2(nul, 2);
        }

        if (in >= 0) {
            dup2(in, 0);
        }

        /* A program that does not finish is stopped after a minute. */
        alarm(60);

        close(fds[1]);
        execvp(av->v[0], av->v);
        _exit(127);
    }

    close(fds[1]);

    for (;;) {
        char tmp[512];
        ssize_t n = read(fds[0], tmp, sizeof tmp);

        if (n <= 0) {
            break;
        }

        if (out != NULL && got + 1 < outsz) {
            size_t take = to_size(n);

            if (got + take + 1 > outsz) {
                take = outsz - got - 1;
            }

            memcpy(out + got, tmp, take);
            got += take;
        }
    }

    close(fds[0]);

    if (out != NULL) {
        out[got] = '\0';
    }

    waitpid(pid, &status, 0);

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}


/* The pHYs chunk of a PNG file gives the resolution; 72 when missing. */
static double
png_dpi(const unsigned char *d, size_t len)
{
    size_t pos = 8;

    while (pos + 12 <= len) {
        long clen = get_u(d + pos, 4);

        if (memcmp(d + pos + 4, "pHYs", 4) == 0 && clen >= 9 &&
            pos + 8 + 9 <= len) {
            long ppm = get_u(d + pos + 8, 4);

            if (d[pos + 16] == 1 && ppm > 0) {
                return dbl(ppm) * 0.0254;
            }

            return 72.0;
        }

        if (memcmp(d + pos + 4, "IDAT", 4) == 0) {
            break;
        }

        pos += 12 + to_size(clen);
    }

    return 72.0;
}


static int
load_png(const unsigned char *d, size_t len, Image *im)
{
    png_image pi;
    png_alloc_size_t sz;

    memset(&pi, 0, sizeof pi);
    pi.version = PNG_IMAGE_VERSION;

    if (png_image_begin_read_from_memory(&pi, d, len) == 0) {
        return -1;
    }

    pi.format = PNG_FORMAT_RGBA;

    if (to_long(pi.width) * to_long(pi.height) > 64000000L) {
        png_image_free(&pi);

        return -1;
    }

    sz = PNG_IMAGE_SIZE(pi);
    im->rgba = xcalloc(sz, 1);

    if (png_image_finish_read(&pi, NULL, im->rgba, 0, NULL) == 0) {
        free(im->rgba);
        im->rgba = NULL;
        png_image_free(&pi);

        return -1;
    }

    im->w = to_long(pi.width);
    im->h = to_long(pi.height);
    png_image_free(&pi);

    return 0;
}


static int
ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s);
    size_t b = strlen(suffix);
    size_t i;

    if (a < b) {
        return 0;
    }

    for (i = 0; i < b; i++) {
        if (tolower(to_u8(s[a - b + i])) != suffix[i]) {
            return 0;
        }
    }

    return 1;
}


/* Decodes a raster file (PNG or JPEG) by its contents, not its name. */
static int
load_raster(const char *path, Image *im)
{
    size_t len;
    unsigned char *d = read_file(path, &len);
    double dpi = 72.0;
    int rc = -1;

    if (d == NULL) {
        return -1;
    }

    if (len > 8 && memcmp(d, "\x89PNG", 4) == 0) {
        rc = load_png(d, len, im);
        dpi = png_dpi(d, len);
    } else if (len > 4 && d[0] == 0xff && d[1] == 0xd8) {
        rc = image_decode_jpeg(d, len, im, &dpi);
    }

    free(d);

    if (rc == 0) {
        if (dpi < 1.0) {
            dpi = 72.0;
        }

        im->ext[0] = 0.0;
        im->ext[1] = 0.0;
        im->ext[2] = dbl(im->w) * 72.0 / dpi;
        im->ext[3] = dbl(im->h) * 72.0 / dpi;
    }

    return rc;
}


/* Reads the box of one PDF page with pdfinfo; false when it fails. */
static int
pdf_box(const char *path, int page, int media, double *box)
{
    char out[8192];
    char pg[32];
    Argv av;
    const char *want = media ? "MediaBox:" : "CropBox:";
    const char *p;
    int rc;

    memset(&av, 0, sizeof av);
    snprintf(pg, sizeof pg, "%d", page);
    argv_add(&av, "pdfinfo");
    argv_add(&av, "-box");
    argv_add(&av, "-f");
    argv_add(&av, pg);
    argv_add(&av, "-l");
    argv_add(&av, pg);
    argv_add(&av, path);
    rc = run_prog(&av, out, sizeof out);
    argv_free(&av);

    if (rc != 0) {
        return 0;
    }

    p = strstr(out, want);

    if (p == NULL) {
        p = strstr(out, "MediaBox:");
        p = p != NULL ? p + 9 : NULL;
    } else {
        p += strlen(want);
    }

    if (p == NULL) {
        return 0;
    }

    return sscanf(p, "%lf %lf %lf %lf", &box[0], &box[1], &box[2],
                  &box[3]) == 4;
}


/* The BoundingBox comment of an EPS file. */
static int
eps_box(const char *path, double *box)
{
    size_t len;
    unsigned char *d = read_file(path, &len);
    size_t pos;
    int ok = 0;

    if (d == NULL) {
        return 0;
    }

    for (pos = 0; pos + 20 < len && !ok; pos++) {
        char line[160];
        size_t n = len - pos < sizeof line - 1 ? len - pos : sizeof line - 1;

        if (d[pos] != '%' || d[pos + 1] != '%' ||
            memcmp(d + pos + 2, "BoundingBox:", 12) != 0) {
            continue;
        }

        memcpy(line, d + pos + 14, n - 14);
        line[n - 14] = '\0';
        ok = sscanf(line, "%lf %lf %lf %lf", &box[0], &box[1], &box[2],
                    &box[3]) == 4;
    }

    free(d);

    return ok;
}


/* Renders a PDF page or an EPS file with ghostscript to a temporary PNG. */
static int
render_gs(const char *path, int page, int media, int is_eps, double dpi,
          const double *box, Image *im)
{
    char *out = tmp_name();
    char buf[1100];
    char *rp;
    Argv av;
    double w = box[2] - box[0];
    double h = box[3] - box[1];
    double px;
    int rc;

    if (out == NULL || w < 0.5 || h < 0.5) {
        return -1;
    }

    px = w * h * dpi * dpi / 5184.0;

    if (px > dbl(MAX_PIXELS)) {
        dpi = dpi * sqrt(dbl(MAX_PIXELS) / px);
    }

    if (dpi < 20.0) {
        dpi = 20.0;
    }

    memset(&av, 0, sizeof av);
    argv_add(&av, "gs");
    argv_add(&av, "-q");
    argv_add(&av, "-dSAFER");
    argv_add(&av, "-dBATCH");
    argv_add(&av, "-dNOPAUSE");
    argv_add(&av, "-dTextAlphaBits=4");
    argv_add(&av, "-dGraphicsAlphaBits=4");
    argv_add(&av, "-sDEVICE=pngalpha");
    snprintf(buf, sizeof buf, "-r%.2f", dpi);
    argv_add(&av, buf);

    if (is_eps) {
        argv_add(&av, "-dEPSCrop");
    } else {
        snprintf(buf, sizeof buf, "-dFirstPage=%d", page);
        argv_add(&av, buf);
        snprintf(buf, sizeof buf, "-dLastPage=%d", page);
        argv_add(&av, buf);
        argv_add(&av, media ? "-dUseMediaBox" : "-dUseCropBox");
    }

    snprintf(buf, sizeof buf, "-sOutputFile=%s", out);
    argv_add(&av, buf);
    argv_add(&av, "-f");
    rp = path[0] != '/' ? realpath(path, NULL) : NULL;
    argv_add(&av, rp != NULL ? rp : path);
    free(rp);
    rc = run_prog(&av, NULL, 0);
    argv_free(&av);

    if (rc != 0 || load_raster(out, im) != 0) {
        return -1;
    }

    im->ext[0] = box[0];
    im->ext[1] = box[1];
    im->ext[2] = box[2];
    im->ext[3] = box[3];

    return 0;
}


static Entry *
cache_find(const char *key)
{
    long i;

    for (i = 0; i < ncache; i++) {
        if (strcmp(cache[i]->key, key) == 0) {
            return cache[i];
        }
    }

    return NULL;
}


static void
entry_free(Entry *e)
{
    free(e->im.rgba);
    free(e->key);
    free(e);
}


/* The pictures kept in memory are limited; the oldest ones go first. */
static void
cache_trim(void)
{
    while (ncache > 1 && cache_bytes > 256.0 * 1024.0 * 1024.0) {
        long i;

        cache_bytes -= dbl(cache[0]->im.w) * dbl(cache[0]->im.h) * 4.0;
        entry_free(cache[0]);

        for (i = 1; i < ncache; i++) {
            cache[i - 1] = cache[i];
        }

        ncache--;
    }
}


static Entry *
cache_add(const char *key, int raster)
{
    Entry *e = xcalloc(1, sizeof *e);

    cache_trim();
    cache = xrealloc(cache, to_size(ncache + 1) * sizeof *cache);
    e->key = xstrdup(key);
    e->raster = raster;
    cache[ncache++] = e;

    return e;
}


/* Loads a picture; the dpi tells how finely PDF and EPS are rendered. */
Image *
image_get(const char *name, int page, int media_box, double dpi)
{
    char key[1200];
    char *path;
    Entry *e;
    int is_pdf = ends_with(name, ".pdf");
    int is_eps = ends_with(name, ".eps") || ends_with(name, ".ps");
    long q = dpi < 36.0 ? 36 : ceil_long(dpi / 10.0) * 10;
    double box[4];
    int ok = 0;

    snprintf(key, sizeof key, "%s|%d|%d|%ld", name, page, media_box,
             is_pdf || is_eps ? q : 0L);
    e = cache_find(key);

    if (e != NULL) {
        return &e->im;
    }

    e = cache_add(key, !(is_pdf || is_eps));
    path = find_file(name);

    if (path == NULL) {
        report_once("picture file not found:", name);

        return &e->im;
    }

    if (is_pdf) {
        if (!pdf_box(path, page, media_box, box)) {
            box[0] = 0.0;
            box[1] = 0.0;
            box[2] = 612.0;
            box[3] = 792.0;
        }

        ok = render_gs(path, page, media_box, 0, dbl(q), box, &e->im) == 0;
    } else if (is_eps) {
        if (!eps_box(path, box)) {
            box[0] = 0.0;
            box[1] = 0.0;
            box[2] = 612.0;
            box[3] = 792.0;
        }

        ok = render_gs(path, 1, 0, 1, dbl(q), box, &e->im) == 0;
    } else {
        ok = load_raster(path, &e->im) == 0;
    }

    if (!ok) {
        report_once("picture cannot be decoded:", name);
    }

    e->im.ok = ok;
    cache_bytes += dbl(e->im.w) * dbl(e->im.h) * 4.0;
    free(path);

    return &e->im;
}


/* Forgets the rendered PDF and EPS pictures, as the zoom has changed. */
void
image_flush(void)
{
    long i;
    long k = 0;

    for (i = 0; i < ncache; i++) {
        if (cache[i]->raster) {
            cache[k++] = cache[i];
        } else {
            cache_bytes -= dbl(cache[i]->im.w) * dbl(cache[i]->im.h) * 4.0;
            entry_free(cache[i]);
        }
    }

    ncache = k;

    while (ntmp > 0) {
        unlink(tmp_files[--ntmp]);
        free(tmp_files[ntmp]);
    }
}


/* The extent of a picture in bp (llx, lly, urx, ury) without rendering it. */
int
image_extent(const char *name, int page, int media_box, double *ext)
{
    char key[1200];
    char *path;
    Entry *e;
    int is_pdf = ends_with(name, ".pdf");
    int is_eps = ends_with(name, ".eps") || ends_with(name, ".ps");
    double box[4];
    int ok = 0;
    int found = 0;

    if (!is_pdf && !is_eps) {
        Image *im = image_get(name, page, media_box, 72.0);

        if (im->ok) {
            memcpy(ext, im->ext, 4 * sizeof ext[0]);
        }

        return im->ok;
    }

    snprintf(key, sizeof key, "ext|%s|%d|%d", name, page, media_box);
    e = cache_find(key);

    if (e != NULL) {
        memcpy(ext, e->im.ext, 4 * sizeof ext[0]);

        return e->im.ok;
    }

    e = cache_add(key, 1);
    path = find_file(name);

    if (path == NULL) {
        report_once("picture file not found:", name);
    } else {
        found = 1;

        if (is_pdf) {
            ok = pdf_box(path, page, media_box, box);
        } else {
            ok = eps_box(path, box);
        }

        free(path);
    }

    if (!ok && found) {
        box[0] = 0.0;
        box[1] = 0.0;
        box[2] = 612.0;
        box[3] = 792.0;
        ok = 1;
    }

    e->im.ok = ok;

    if (ok) {
        memcpy(e->im.ext, box, sizeof box);
        memcpy(ext, box, sizeof box);
    }

    return ok;
}
