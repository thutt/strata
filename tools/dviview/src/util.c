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
 * Small helpers: checked conversions, allocation, files, kpsewhich.
 */
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

#include "dviview.h"
#include "sysprog.h"

static FILE *report_fp;


void
viewer_report_to(const char *path)
{
    if (report_fp != NULL) {
        fclose(report_fp);
    }

    report_fp = fopen(path, "w");
}


void
viewer_tell(const char *kind, const char *fmt, ...)
{
    char line[1200];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);

    if (report_fp != NULL) {
        fprintf(report_fp, "%s: %s\n", kind, line);
        fflush(report_fp);
    }

    if (strcmp(kind, "fail") == 0) {
        log_error("dviview: the viewer failed: %s", line);
    } else {
        log_warn("dviview: the viewer could not show something: %s", line);
    }
}


int
to_int(long v)
{
    if (v > INT_MAX) {
        v = INT_MAX;
    }

    if (v < INT_MIN) {
        v = INT_MIN;
    }

    return (int)v;
}


unsigned
to_uint(long v)
{
    if (v < 0) {
        v = 0;
    }

    if (v > INT_MAX) {
        v = INT_MAX;
    }

    return (unsigned)v;
}


unsigned char
to_u8(long v)
{
    if (v < 0) {
        v = 0;
    }

    if (v > 255) {
        v = 255;
    }

    return (unsigned char)v;
}


char
to_char(long v)
{
    return (char)to_u8(v);
}


double
dbl(long v)
{
    return (double)v;
}


long
round_long(double v)
{
    if (v > 1e9) {
        v = 1e9;
    }

    if (v < -1e9) {
        v = -1e9;
    }

    return lrint(v);
}


long
floor_long(double v)
{
    return round_long(floor(v));
}


long
ceil_long(double v)
{
    return round_long(ceil(v));
}


size_t
to_size(long v)
{
    if (v < 0) {
        v = 0;
    }

    return (size_t)v;
}


long
to_long(size_t v)
{
    if (v > 1000000000UL) {
        v = 1000000000UL;
    }

    return (long)v;
}


void *
xcalloc(size_t n, size_t size)
{
    void *p = calloc(n == 0 ? 1 : n, size);

    if (p == NULL) {
        log_fatal(1, "dviview: out of memory allocating %zu items of %zu "
                  "bytes", n, size);
    }

    return p;
}


void *
xrealloc(void *p, size_t size)
{
    void *q = realloc(p, size == 0 ? 1 : size);

    if (q == NULL) {
        log_fatal(1, "dviview: out of memory growing a block to %zu bytes",
                  size);
    }

    return q;
}


char *
xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xcalloc(n, 1);

    memcpy(p, s, n);

    return p;
}


unsigned char *
read_file(const char *path, size_t *len)
{
    FILE *fp = fopen(path, "rb");
    unsigned char *buf = NULL;
    size_t cap = 0;
    size_t n = 0;

    if (fp == NULL) {
        return NULL;
    }

    for (;;) {
        size_t got;

        if (n + 65536 + 1 > cap) {
            cap = cap * 2 + 65536 + 1;
            buf = xrealloc(buf, cap);
        }

        got = fread(buf + n, 1, 65536, fp);
        n += got;

        if (got == 0) {
            break;
        }
    }

    fclose(fp);
    buf[n] = 0;
    *len = n;

    return buf;
}


/* Free the strings of an argument vector. */
static void
free_argv(char **argv, int n)
{
    for (int i = 0; i < n; i++) {
        free(argv[i]);
    }
}


/*
 * Ask kpsewhich for the path of a file.  The program is started with an
 * argument vector, so that a name from a DVI file (it may have spaces and
 * any character) is one argument and no shell reads it.  A name that begins
 * with a minus sign would be an option, and is refused; so is a name with a
 * control character.  The first argument may be the option -mode=ljfour.
 */
char *
kpse_find(const char *arg1, const char *arg2, const char *arg3)
{
    char line[1024];
    const char *args[3];
    char *argv[6];
    int ac = 0;
    int fds[2];
    pid_t pid;
    FILE *fp;
    char *res = NULL;
    char prog[PATH_MAX];

    args[0] = arg1;
    args[1] = arg2;
    args[2] = arg3;
    if (sp_find("kpsewhich", prog, sizeof prog, NULL) != 0) {
        return NULL;
    }

    argv[ac++] = xstrdup(prog);

    for (int i = 0; i < 3; i++) {
        const char *s = args[i];

        if (s == NULL) {
            continue;
        }

        if (s[0] == '\0' || (s[0] == '-' && strcmp(s, "-mode=ljfour") != 0)) {
            free_argv(argv, ac);

            return NULL;
        }

        for (; *s != '\0'; s++) {
            if (*s >= 0 && *s < 32) {
                free_argv(argv, ac);

                return NULL;
            }
        }

        argv[ac++] = xstrdup(args[i]);
    }

    argv[ac] = NULL;

    if (pipe(fds) != 0) {
        free_argv(argv, ac);

        return NULL;
    }

    pid = fork();

    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        free_argv(argv, ac);

        return NULL;
    }

    if (pid == 0) {
        int nul = open("/dev/null", O_WRONLY);

        dup2(fds[1], 1);

        if (nul >= 0) {
            dup2(nul, 2);
        }

        close(fds[0]);
        close(fds[1]);
        sp_child_env(prog);
        execv(prog, argv);
        _exit(127);
    }

    close(fds[1]);
    free_argv(argv, ac);
    fp = fdopen(fds[0], "r");

    if (fp == NULL) {
        close(fds[0]);
        waitpid(pid, NULL, 0);

        return NULL;
    }

    if (fgets(line, sizeof line, fp) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] != '\0') {
            res = xstrdup(line);
        }
    }

    fclose(fp);
    waitpid(pid, NULL, 0);

    return res;
}


long
get_u(const unsigned char *p, int n)
{
    long v = 0;
    int i;

    for (i = 0; i < n; i++) {
        v = v * 256 + p[i];
    }

    return v;
}


long
get_s(const unsigned char *p, int n)
{
    long v = get_u(p, n);

    if (p[0] >= 128) {
        v -= 1L << (8 * n);
    }

    return v;
}
