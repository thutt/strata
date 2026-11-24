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

/* fonts.c - the list of fonts for printing; see fonts.h. */
#include "util.h"
#include "config.h"
#include "fonts.h"
#include "sysprog.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FILE_NAME "fonts"

static const struct {
    const char *name;
    const char *package;
    const char *options;
    int family;
    const char *ctan;
    const char *map;
} defaults[FONT_COUNT] = {
    {"Latin Modern Sans", "lmodern", "", FF_SANS, "", ""},
    {"Latin Modern Roman", "lmodern", "", FF_ROMAN, "", ""},
    {"Latin Modern Roman", "lmodern", "", FF_ROMAN, "", ""},
    {"Times", "mathptmx", "", FF_ROMAN, "", ""},
    {"Helvetica", "helvet", "scaled=0.92", FF_SANS, "", ""},
    {"Courier", "courier", "", FF_MONO, "", ""},
    {"Palatino", "mathpazo", "", FF_ROMAN, "", ""},
    {"Bookman", "bookman", "", FF_ROMAN, "", ""},
    {"New Century Schoolbook", "newcent", "", FF_ROMAN, "", ""},
    {"Avant Garde", "avant", "", FF_SANS, "", ""},
    {"Zapf Chancery", "chancery", "", FF_ROMAN, "", ""},
    {"Charter", "charter", "", FF_ROMAN, "", ""},
    {"Utopia", "utopia", "", FF_ROMAN, "", ""},
    {"TeX Gyre Termes", "tgtermes", "", FF_ROMAN, "", ""},
    {"TeX Gyre Heros", "tgheros", "", FF_SANS, "", ""},
    {"TeX Gyre Pagella", "tgpagella", "", FF_ROMAN, "", ""},
    {"TeX Gyre Cursor", "tgcursor", "", FF_MONO, "", ""},
    {"Computer Modern Roman", "", "", FF_ROMAN, "", ""},
    {"Lato", "lato", "default", FF_SANS, "lato", "lato.map"},
    {"", "", "", FF_ROMAN, "", ""}};


void
fonts_defaults(FontEntry t[FONT_COUNT])
{
    for (int i = 0; i < FONT_COUNT; i++) {
        memset(&t[i], 0, sizeof t[i]);
        str_copy(t[i].name, sizeof t[i].name, defaults[i].name);
        str_copy(t[i].package, sizeof t[i].package, defaults[i].package);
        str_copy(t[i].options, sizeof t[i].options, defaults[i].options);
        t[i].family = defaults[i].family;
        str_copy(t[i].ctan, sizeof t[i].ctan, defaults[i].ctan);
        str_copy(t[i].map, sizeof t[i].map, defaults[i].map);
    }
}


void
fonts_labels(char labels[FONT_COUNT][FONT_LABEL])
{
    static const char *const roles[3] = {"Headline", "Text", "Header/Footer"};
    FontEntry t[FONT_COUNT];

    fonts_load(t);

    for (int i = 0; i < FONT_COUNT; i++) {
        char role[24];

        if (i < 3) {
            str_copy(role, sizeof role, roles[i]);
        } else {
            snprintf(role, sizeof role, "Font %d", i + 1);
        }

        if (t[i].name[0] != '\0') {
            snprintf(labels[i], FONT_LABEL, "%.23s (%.47s)", role,
                     t[i].name);
        } else {
            str_copy(labels[i], FONT_LABEL, role);
        }
    }
}


int
fonts_word_ok(const char *s)
{
    for (; *s != '\0'; s++) {
        int c = u8(*s);

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' ||
              c == '+' || c == '=' || c == ',')) {
            return 0;
        }
    }

    return 1;
}


static const char *const family_words[] = {"roman", "sans", "mono"};


/* Split a line at the bars into at most n fields (in place). */
static int
split_bars(char *line, char *f[], int n)
{
    int k = 0;

    f[k++] = line;

    for (char *p = line; *p != '\0' && k < n; p++) {
        if (*p == '|') {
            *p = '\0';
            f[k++] = p + 1;
        }
    }

    return k;
}


void
fonts_load(FontEntry t[FONT_COUNT])
{
    FILE *fp;
    char line[512];

    fonts_defaults(t);
    fp = config_read_file(FILE_NAME);

    if (fp == NULL) {
        return;
    }

    while (fgets(line, sizeof line, fp) != NULL) {
        char *f[7];
        int n;
        int num;
        int fam = -1;

        line[strcspn(line, "\r\n")] = '\0';
        n = split_bars(line, f, 7);

        if (n != 7) {
            continue;
        }

        num = atoi(f[0]);

        for (int i = 0; i < 3; i++) {
            if (strcmp(f[4], family_words[i]) == 0) {
                fam = i;
            }
        }

        if (num < 1 || num > FONT_COUNT || fam < 0 ||
            fonts_word_ok(f[2]) == 0 || fonts_word_ok(f[3]) == 0 ||
            fonts_word_ok(f[5]) == 0 || fonts_word_ok(f[6]) == 0) {
            continue;
        }

        str_copy(t[num - 1].name, sizeof t[num - 1].name, f[1]);
        str_copy(t[num - 1].package, sizeof t[num - 1].package, f[2]);
        str_copy(t[num - 1].options, sizeof t[num - 1].options, f[3]);
        t[num - 1].family = fam;
        str_copy(t[num - 1].ctan, sizeof t[num - 1].ctan, f[5]);
        str_copy(t[num - 1].map, sizeof t[num - 1].map, f[6]);
    }

    fclose(fp);
}


int
fonts_save(const FontEntry t[FONT_COUNT], char *err, int errcap)
{
    char text[FONT_COUNT * 320];
    size_t n = 0;

    text[0] = '\0';

    for (int i = 0; i < FONT_COUNT; i++) {
        if (strchr(t[i].name, '|') != NULL || fonts_word_ok(t[i].package) == 0 ||
            fonts_word_ok(t[i].options) == 0 || fonts_word_ok(t[i].ctan) == 0 ||
            fonts_word_ok(t[i].map) == 0) {
            snprintf(err, usz(errcap), "Font %d has a character that cannot be "
                                       "used (only letters, digits and . _ - + "
                                       "= , are allowed)", i + 1);

            return -1;
        }

        n += usz(snprintf(text + n, sizeof text - n, "%d|%s|%s|%s|%s|%s|%s\n",
                          i + 1, t[i].name, t[i].package, t[i].options,
                          family_words[t[i].family >= 0 && t[i].family < 3 ?
                                           t[i].family :
                                           0],
                          t[i].ctan, t[i].map));
    }

    return config_write_file(FILE_NAME, text, err, errcap);
}


const char *
fonts_family_command(int family)
{
    return family == FF_SANS ? "\\sffamily" :
           family == FF_MONO ? "\\ttfamily" :
                               "\\rmfamily";
}


void
fonts_package_line(const FontEntry *e, char *out, size_t cap)
{
    if (e->package[0] == '\0') {
        out[0] = '\0';
    } else if (e->options[0] == '\0') {
        snprintf(out, cap, "\\usepackage{%s}", e->package);
    } else {
        snprintf(out, cap, "\\usepackage[%s]{%s}", e->options, e->package);
    }
}


/*
 * Run a program without a shell.  The output (standard output, up to cap
 * bytes) goes to out when it is not NULL.  Returns the exit status, or -1.
 */
static int
run(const char *const argv[], char *out, size_t cap)
{
    char *av[16];
    char prog[PATH_MAX];
    int na = 0;
    int pfd[2];
    pid_t pid;
    int st;
    size_t n = 0;

    if (out != NULL) {
        out[0] = '\0';
    }

    /* The program is looked for in the fixed folders of the system and run
       by its full path: PATH is never searched. */
    if (sp_find(argv[0], prog, sizeof prog, NULL) != 0) {
        return -1;
    }

    /* execv wants strings that can be changed. */
    av[0] = strdup(prog);

    for (na = 1; argv[na] != NULL && na < 15; na++) {
        av[na] = strdup(argv[na]);
    }

    av[na] = NULL;

    if (pipe(pfd) != 0) {
        for (int i = 0; i < na; i++) {
            free(av[i]);
        }

        return -1;
    }

    pid = fork();

    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);

        return -1;
    }

    if (pid == 0) {
        int nul = open("/dev/null", O_RDWR);

        close(pfd[0]);
        dup2(pfd[1], 1);
        dup2(nul >= 0 ? nul : 2, 2);
        dup2(nul >= 0 ? nul : 0, 0);
        sp_child_env(av[0]);
        execv(av[0], av);
        _exit(127);
    }

    close(pfd[1]);

    for (int i = 0; i < na; i++) {
        free(av[i]);
    }

    for (;;) {
        char buf[512];
        ssize_t r = read(pfd[0], buf, sizeof buf);

        if (r <= 0) {
            break;
        }

        for (ssize_t i = 0; i < r && out != NULL && n + 1 < cap; i++) {
            out[n++] = buf[i];
        }
    }

    close(pfd[0]);

    if (out != NULL) {
        out[n] = '\0';
    }

    if (waitpid(pid, &st, 0) != pid) {
        return -1;
    }

    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}


/*
 * What kpsewhich said about a package, kept for the run of the program: the
 * panel of the font list asks for every entry, and each question is a
 * program that takes a good part of a second to start and search.
 */
#define FCACHE_MAX 64

static struct {
    char package[48];
    int found; /* 1 found, 0 not found. */
} fcache[FCACHE_MAX];

static int fcache_n;


static int
fcache_find(const char *package)
{
    for (int i = 0; i < fcache_n; i++) {
        if (strcmp(fcache[i].package, package) == 0) {
            return i;
        }
    }

    return -1;
}


static void
fcache_put(const char *package, int found)
{
    int i = fcache_find(package);

    if (i < 0) {
        if (fcache_n >= FCACHE_MAX) {
            return;
        }

        i = fcache_n++;
        str_copy(fcache[i].package, sizeof fcache[i].package, package);
    }

    fcache[i].found = found;
}


void
fonts_installed_forget(const FontEntry *e)
{
    int i = fcache_find(e->package);

    if (i >= 0) {
        fcache[i] = fcache[--fcache_n];
    }
}


/* Does the output of kpsewhich hold a line that ends in the file name? */
static int
output_has(const char *out, const char *sty)
{
    char want[100];
    size_t wl;

    snprintf(want, sizeof want, "/%.90s", sty);
    wl = strlen(want);

    for (const char *line = out; *line != '\0';) {
        size_t len = strcspn(line, "\n");

        if (len >= wl && strncmp(line + len - wl, want, wl) == 0) {
            return 1;
        }

        line += len;

        if (*line == '\n') {
            line++;
        }
    }

    return 0;
}


void
fonts_installed_prefetch(const FontEntry t[FONT_COUNT])
{
    char pkg[14][48];
    char sty[14][96];
    const char *av[16];
    char out[8192];
    int n = 0;

    /* The packages that are not known yet go to kpsewhich together, as many
       as run() takes at once. */
    for (int i = 0; i < FONT_COUNT && n < 14; i++) {
        int dup = 0;

        if (t[i].package[0] == '\0' || fcache_find(t[i].package) >= 0) {
            continue;
        }

        for (int k = 0; k < n; k++) {
            dup = dup != 0 || strcmp(pkg[k], t[i].package) == 0;
        }

        if (dup != 0) {
            continue;
        }

        str_copy(pkg[n], sizeof pkg[n], t[i].package);
        snprintf(sty[n], sizeof sty[n], "%s.sty", t[i].package);
        av[1 + n] = sty[n];
        n++;
    }

    if (n == 0) {
        return;
    }

    av[0] = "kpsewhich";
    av[1 + n] = NULL;

    if (run(av, out, sizeof out) < 0) {
        return;
    }

    for (int k = 0; k < n; k++) {
        fcache_put(pkg[k], output_has(out, sty[k]));
    }
}


int
fonts_installed(const FontEntry *e)
{
    char sty[96];
    char found[PATH_MAX];
    const char *av[3];
    int i;
    int ok;

    if (e->package[0] == '\0') {
        return 1;
    }

    i = fcache_find(e->package);

    if (i >= 0) {
        return fcache[i].found;
    }

    snprintf(sty, sizeof sty, "%s.sty", e->package);
    av[0] = "kpsewhich";
    av[1] = sty;
    av[2] = NULL;
    ok = run(av, found, sizeof found) == 0 && found[0] != '\0';
    fcache_put(e->package, ok);

    return ok;
}


/*
 * The size of a file in bytes, or -1 when it is not there (yet).
 */
static long
file_bytes(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        return -1;
    }

    return (long)st.st_size;
}


/*
 * Run a program of a step of the install without a shell and wait for it,
 * calling tick about every 50 milliseconds with the line that says what is
 * being done (the label, and the size of the file in watch when it is not
 * NULL).  When tick is NULL the wait is a short sleep.  A tick that answers
 * with a non-zero value stops the program, with everything that it started,
 * when can_stop is not 0.  Returns the exit status, -1 when the program did
 * not run, and -2 when it was stopped.
 */
static int
run_wait(const char *const argv[], FontTick tick, void *ctx,
         const char *label, const char *watch, int can_stop)
{
    char *av[16];
    char prog[PATH_MAX];
    int na;
    pid_t pid;
    int st;

    if (sp_find(argv[0], prog, sizeof prog, NULL) != 0) {
        return -1;
    }

    av[0] = strdup(prog);

    for (na = 1; argv[na] != NULL && na < 15; na++) {
        av[na] = strdup(argv[na]);
    }

    av[na] = NULL;
    pid = fork();

    if (pid < 0) {
        for (int i = 0; i < na; i++) {
            free(av[i]);
        }

        return -1;
    }

    if (pid == 0) {
        int nul = open("/dev/null", O_RDWR);

        setpgid(0, 0);

        if (nul >= 0) {
            dup2(nul, 0);
            dup2(nul, 1);
            dup2(nul, 2);
        }

        sp_child_env(av[0]);
        execv(av[0], av);
        _exit(127);
    }

    /* The child does the same, so that the group exists whoever runs
       first. */
    setpgid(pid, pid);

    for (int i = 0; i < na; i++) {
        free(av[i]);
    }

    for (;;) {
        pid_t r = waitpid(pid, &st, WNOHANG);

        if (r == pid) {
            break;
        }

        if (r < 0 && errno != EINTR) {
            return -1;
        }

        if (tick != NULL) {
            char line[200];
            long b = watch != NULL ? file_bytes(watch) : -1;

            if (b >= 0) {
                snprintf(line, sizeof line, "%s %ld.%ld MB so far", label,
                         b / 1000000, (b / 100000) % 10);
            } else {
                snprintf(line, sizeof line, "%s", label);
            }

            if (tick(ctx, line, can_stop) != 0 && can_stop != 0) {
                kill(-pid, SIGTERM);
                kill(pid, SIGTERM);

                if (waitpid(pid, &st, WNOHANG) != pid) {
                    struct timespec ts = {0, 100 * 1000 * 1000};

                    nanosleep(&ts, NULL);
                    kill(-pid, SIGKILL);
                    kill(pid, SIGKILL);
                    waitpid(pid, &st, 0);
                }

                return -2;
            }
        } else {
            struct timespec ts = {0, 20 * 1000 * 1000};

            nanosleep(&ts, NULL);
        }
    }

    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}


int
fonts_install(const FontEntry *e, char *msg, size_t cap, FontTick tick,
              void *ctx)
{
    char home[PATH_MAX];
    char url[512];
    char zip[PATH_MAX];
    char tmpl[PATH_MAX];
    char label[120];
    const char *base = getenv("STRATA_CTAN_URL");
    const char *av[8];
    char *dir;
    int rc;

    if (e->ctan[0] == '\0') {
        str_copy(msg, cap, "This font has no archive to download");

        return -1;
    }

    if (fonts_word_ok(e->ctan) == 0 || fonts_word_ok(e->map) == 0 ||
        strchr(e->ctan, '/') != NULL) {
        str_copy(msg, cap, "The name of the archive or of the map file has a "
                           "character that cannot be used");

        return -1;
    }

    av[0] = "kpsewhich";
    av[1] = "-var-value";
    av[2] = "TEXMFHOME";
    av[3] = NULL;

    if (run(av, home, sizeof home) != 0 || home[0] == '\0') {
        str_copy(msg, cap, "kpsewhich did not run (is TeX installed?)");

        return -1;
    }

    home[strcspn(home, "\r\n")] = '\0';
    snprintf(url, sizeof url, "%.300s/%s.tds.zip",
             base != NULL && base[0] != '\0' ? base :
                                               "https://mirrors.ctan.org/"
                                               "install/fonts",
             e->ctan);
    tmp_path(tmpl, sizeof tmpl, "strata-fontXXXXXX");
    dir = mkdtemp(tmpl);

    if (dir == NULL) {
        str_copy(msg, cap, "Cannot make a temporary directory");

        return -1;
    }

    snprintf(zip, sizeof zip, "%s/font.zip", dir);
    snprintf(label, sizeof label, "Downloading %.60s...", e->name);
    av[0] = "curl";
    av[1] = "-fsSL";
    av[2] = "-o";
    av[3] = zip;
    av[4] = url;
    av[5] = NULL;
    rc = run_wait(av, tick, ctx, label, zip, 1);

    if (rc == 0) {
        snprintf(label, sizeof label, "Unpacking %.60s...", e->name);
        av[0] = "unzip";
        av[1] = "-q";
        av[2] = "-o";
        av[3] = zip;
        av[4] = "-d";
        av[5] = home;
        av[6] = NULL;
        rc = run_wait(av, tick, ctx, label, NULL, 0) == 0 ? 0 : 2;
    }

    unlink(zip);
    rmdir(dir);

    if (rc == -2) {
        str_copy(msg, cap, "Installation cancelled");

        return 1;
    }

    if (rc != 0) {
        snprintf(msg, cap, rc == 2 ? "The archive could not be unpacked" :
                                     "The archive could not be downloaded "
                                     "from %.200s",
                 url);

        return -1;
    }

    av[0] = "mktexlsr";
    av[1] = home;
    av[2] = NULL;
    run_wait(av, tick, ctx, "Making the file list of TeX (mktexlsr)...", NULL,
             0);

    if (e->map[0] != '\0') {
        char arg[96];

        snprintf(arg, sizeof arg, "Map=%s", e->map);
        snprintf(label, sizeof label, "Enabling the map file %.40s "
                 "(updmap-user)...", e->map);
        av[0] = "updmap-user";
        av[1] = "--enable";
        av[2] = arg;
        av[3] = NULL;

        if (run_wait(av, tick, ctx, label, NULL, 0) != 0) {
            str_copy(msg, cap, "updmap-user failed to enable the map file");

            return -1;
        }
    }

    snprintf(msg, cap, "Installed in %.300s", home);

    return 0;
}
