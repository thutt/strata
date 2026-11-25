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

/* config.c - the settings of the user in ~/.strata; see config.h. */
#include "config.h"
#include "util.h"
#include "log.h"
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


void
config_defaults(Config *c)
{
    memset(c, 0, sizeof *c);
    c->file_trusted = 1;
    c->autosave = 1;
    c->autosave_seconds = 120;
    c->onscreen_help = 1;
    c->show_marks = 1;
    c->undo_levels = CONFIG_UNDO_DEFAULT;
    c->backups = CONFIG_BACKUPS_DEFAULT;
}


int
config_clamp_backups(int n)
{
    if (n < 0) {
        return 0;
    }

    return n > CONFIG_BACKUPS_MAX ? CONFIG_BACKUPS_MAX : n;
}


int
config_clamp_undo(int n)
{
    if (n < 0) {
        return 0;
    }

    return n > CONFIG_UNDO_MAX ? CONFIG_UNDO_MAX : n;
}


/* The directory: $STRATA_HOME, or .strata in the home directory. */
int
config_directory(char *out, size_t cap)
{
    const char *env = getenv("STRATA_HOME");

    if (env != NULL && env[0] != '\0') {
        snprintf(out, cap, "%s", env);

        return 0;
    }

    const char *home = getenv("HOME");

    if (home == NULL || home[0] == '\0') {
        const struct passwd *pw = getpwuid(getuid());

        home = pw != NULL ? pw->pw_dir : NULL;
    }

    if (home == NULL || home[0] == '\0') {
        return -1;
    }

    snprintf(out, cap, "%s/.strata", home);

    return 0;
}


void
config_path(char *out, int cap)
{
    char dir[PATH_MAX - 64];

    out[0] = '\0';

    if (config_directory(dir, sizeof dir) == 0) {
        snprintf(out, usz(cap), "%s/config", dir);
    }
}


/*
 * The directory must be a real directory (not a link) that belongs to the
 * user.  Its mode is set to 0700.  Returns 0 if it can be used, and 1 if it
 * is not there.  Anything else is -1.
 */
static int
dir_check(const char *dir)
{
    struct stat st;

    if (lstat(dir, &st) != 0) {
        return errno == ENOENT ? 1 : -1;
    }

    if (S_ISDIR(st.st_mode) == 0 || st.st_uid != geteuid()) {
        return -1;
    }

    if ((st.st_mode & 0777) != 0700 && chmod(dir, 0700) != 0) {
        return -1;
    }

    return 0;
}


static void
set_int(int *v, const char *s, int lo, int hi)
{
    int x = atoi(s);

    if (x >= lo && x <= hi) {
        *v = x;
    }
}


int
config_print_to(const Config *c)
{
    if (c->print_to[0] != '\0') {
        return strcmp(c->print_to, "printer") == 0 ? CONFIG_PRINT_PRINTER :
               strcmp(c->print_to, "viewer") == 0  ? CONFIG_PRINT_OPEN :
                                                     CONFIG_PRINT_FILE;
    }

    if (c->printer[0] == '\0' || strcasecmp(c->printer, "File only") == 0) {
        return CONFIG_PRINT_FILE;
    }

    return strcasecmp(c->printer, "PDF viewer") == 0 ||
                   strcasecmp(c->printer, "Open to print") == 0 ?
               CONFIG_PRINT_OPEN :
               CONFIG_PRINT_PRINTER;
}


void
config_print_queue(const Config *c, char *out, size_t cap)
{
    if (c->print_to[0] != '\0') {
        str_copy(out, cap, c->print_queue);
    } else if (config_print_to(c) == CONFIG_PRINT_PRINTER) {
        str_copy(out, cap, c->printer);
    } else {
        out[0] = '\0';
    }
}


int
config_clamp_rows(int rows, int max)
{
    int top = max > 0 && max < CONFIG_ROWS_MAX ? max : CONFIG_ROWS_MAX;

    if (top < CONFIG_ROWS_MIN) {
        top = CONFIG_ROWS_MIN;
    }

    if (rows < CONFIG_ROWS_MIN) {
        return CONFIG_ROWS_MIN;
    }

    return rows > top ? top : rows;
}


/*
 * A bad color of the file is reported once in the run log, however often
 * the file is read (a message that finds no run log is made again).
 */
static void
warn_color(const char *key, const char *val)
{
    static char seen[8][160];
    static int nseen;
    char line[160];

    size_t klen = strlen(key);

    /* The key and the value are cut to fit; only the text must differ. */
    if (klen > sizeof line - 2) {
        klen = sizeof line - 2;
    }

    memcpy(line, key, klen);
    line[klen] = '=';
    str_copy(line + klen + 1, sizeof line - klen - 1, val);

    for (int i = 0; i < nseen && i < 8; i++) {
        if (strcmp(seen[i], line) == 0) {
            return;
        }
    }

    /* Before the run log is there the message is lost: say it again. */
    if (nseen < 8 && log_run_active() != 0) {
        str_copy(seen[nseen], sizeof seen[nseen], line);
        nseen++;
    }

    log_warn("config: the setting %s has the value %s, which is not a "
             "color; use a color name of the VGA palette or #rrggbb "
             "(the default color is used)", key, val);
}


void
config_color_key(char *out, size_t cap, int gui, int kind, int bg)
{
    snprintf(out, cap, "%s_color_%s_%s", gui != 0 ? "x11" : "terminal",
             color_kind_key(kind), bg != 0 ? "bg" : "fg");
}


/* A setting of a color: the key is x11_color_KIND_fg and so on. */
static int
set_color(Config *c, const char *key, const char *val)
{
    for (int gui = 0; gui < 2; gui++) {
        for (int kind = 0; kind < CK_COUNT; kind++) {
            for (int bg = 0; bg < 2; bg++) {
                char name[80];
                unsigned rgb;

                config_color_key(name, sizeof name, gui, kind, bg);

                if (strcmp(key, name) != 0) {
                    continue;
                }

                char *dst = c->colors[gui][kind][bg];

                dst[0] = '\0';

                if (val[0] == '\0') {
                    return 1;
                }

                if (color_parse(val, &rgb) != 0) {
                    warn_color(key, val);

                    return 1;
                }

                color_text(rgb, dst, COLOR_TEXT_MAX);

                return 1;
            }
        }
    }

    return 0;
}


void
config_colors(const Config *c, int gui, ColorSet *cs)
{
    color_defaults(cs);

    for (int kind = 0; kind < CK_COUNT; kind++) {
        for (int bg = 0; bg < 2; bg++) {
            const char *t = c->colors[gui != 0][kind][bg];
            unsigned rgb;

            if (t[0] != '\0' && color_parse(t, &rgb) == 0) {
                if (bg != 0) {
                    cs->pair[kind].bg = rgb;
                } else {
                    cs->pair[kind].fg = rgb;
                }
            }
        }
    }
}


void
color_init(int gui)
{
    Config cfg;
    ColorSet cs;

    color_defaults(&cs);
    color_set_depth(gui != 0 ? DEPTH_TRUE :
                               color_depth_of(getenv("STRATA_COLORS"),
                                              getenv("COLORTERM"),
                                              getenv("TERM")));

    if (color_defaults_forced() == 0) {
        config_load(&cfg);
        config_colors(&cfg, gui, &cs);
    }

    color_activate(&cs);
}


/* A number of rows: a number outside the limits is moved to the limit. */
static void
set_rows(int *v, const char *s)
{
    int x = atoi(s);

    if (x > 0) {
        *v = config_clamp_rows(x, 0);
    }
}


static void
set_bool(int *v, const char *s)
{
    *v = strcmp(s, "no") != 0 && strcmp(s, "0") != 0;
}


/* One line, key=value, of the file or of an older file. */
static void
set_value(Config *c, const char *key, const char *val)
{
    if (strcmp(key, "workdir") == 0) {
        str_copy(c->workdir, sizeof c->workdir, val);
    } else if (strcmp(key, "initial_macros") == 0) {
        str_copy(c->initial_macros, sizeof c->initial_macros, val);
    } else if (strcmp(key, "initial_library") == 0) {
        str_copy(c->initial_library, sizeof c->initial_library, val);
    } else if (strcmp(key, "stred") == 0 || strcmp(key, "msed") == 0) {
        str_copy(c->stred, sizeof c->stred, val);
    } else if (strcmp(key, "help") == 0) {
        str_copy(c->help, sizeof c->help, val);
    } else if (strcmp(key, "autosave") == 0) {
        set_bool(&c->autosave, val);
    } else if (strcmp(key, "autosave_seconds") == 0) {
        set_int(&c->autosave_seconds, val, 1, 86400);
    } else if (strcmp(key, "expanded_status") == 0) {
        set_bool(&c->expanded_status, val);
    } else if (strcmp(key, "onscreen_help") == 0) {
        set_bool(&c->onscreen_help, val);
    } else if (strcmp(key, "show_marks") == 0) {
        set_bool(&c->show_marks, val);
    } else if (strcmp(key, "show_dots") == 0) {
        set_bool(&c->show_dots, val);
    } else if (strcmp(key, "backups") == 0) {
        /* A whole number; anything else keeps the number that was there. */
        int ok = val[0] != '\0';

        for (int i = 0; ok != 0 && val[i] != '\0'; i++) {
            ok = val[i] >= '0' && val[i] <= '9';
        }

        if (ok != 0 && strlen(val) <= 9) {
            c->backups = config_clamp_backups(atoi(val));
            c->backups_explicit = 1;
        }
    } else if (strcmp(key, "backup") == 0) {
        /*
         * The old yes or no is not read.  The old program wrote every key,
         * the default of backup (no) included, so a file that says no may
         * never have been a choice: it must not override the number of
         * backups that is kept now by default.  A yes was a wish for backups
         * and the default is three, so nothing is changed for it either.
         * The key backups, when it is there, is always honoured.
         */
        (void) val;
    } else if (strcmp(key, "printer") == 0) {
        str_copy(c->printer, sizeof c->printer, val);
    } else if (strcmp(key, "print_to") == 0) {
        /* The old word for the PDF viewer, open, is kept as viewer. */
        if (strcmp(val, "printer") == 0 || strcmp(val, "file") == 0 ||
            strcmp(val, "viewer") == 0) {
            str_copy(c->print_to, sizeof c->print_to, val);
        } else if (strcmp(val, "open") == 0) {
            str_copy(c->print_to, sizeof c->print_to, "viewer");
        }
    } else if (strcmp(key, "print_queue") == 0) {
        str_copy(c->print_queue, sizeof c->print_queue, val);
    } else if (strcmp(key, "print_last_printer") == 0) {
        /* A name with a control character is not taken. */
        int clean = val[0] != '\0';

        for (const char *p = val; *p != '\0'; p++) {
            clean = clean && (unsigned char)*p >= 32;
        }

        if (clean != 0) {
            str_copy(c->print_last_printer, sizeof c->print_last_printer,
                     val);
        }
    } else if (strcmp(key, "print_last_sides") == 0) {
        if (strcmp(val, "default") == 0 || strcmp(val, "one") == 0 ||
            strcmp(val, "two") == 0) {
            str_copy(c->print_last_sides, sizeof c->print_last_sides, val);
        }
    } else if (strcmp(key, "print_last_sort") == 0) {
        if (strcmp(val, "default") == 0 || strcmp(val, "yes") == 0 ||
            strcmp(val, "no") == 0) {
            str_copy(c->print_last_sort, sizeof c->print_last_sort, val);
        }
    } else if (strcmp(key, "print_last_bw") == 0) {
        if (strcmp(val, "yes") == 0 || strcmp(val, "no") == 0) {
            str_copy(c->print_last_bw, sizeof c->print_last_bw, val);
        }
    } else if (strcmp(key, "pdf_dir") == 0) {
        str_copy(c->pdf_dir, sizeof c->pdf_dir, val);
    } else if (strcmp(key, "pdf_viewer") == 0) {
        str_copy(c->pdf_viewer, sizeof c->pdf_viewer, val);
    } else if (strcmp(key, "spell_language") == 0) {
        str_copy(c->spell_lang, sizeof c->spell_lang, val);
    } else if (strcmp(key, "latex_pictures") == 0) {
        c->latex_keep_pictures = strcmp(val, "keep") == 0;
    } else if (strcmp(key, "latex_positions") == 0) {
        set_bool(&c->latex_positions, val);
    } else if (strcmp(key, "undo_levels") == 0) {
        if (isdigit(val[0] & 0xFF) != 0) {
            c->undo_levels = config_clamp_undo(atoi(val));
        }
    } else if (strcmp(key, "x11_rows") == 0) {
        set_rows(&c->x11_rows, val);
    } else if (strcmp(key, "terminal_rows") == 0) {
        set_rows(&c->terminal_rows, val);
    } else if (strcmp(key, "terminal_charset") == 0) {
        if (strcmp(val, "utf8") == 0 || strcmp(val, "vt100") == 0 ||
            strcmp(val, "ascii") == 0 || strcmp(val, "auto") == 0) {
            str_copy(c->terminal_charset, sizeof c->terminal_charset, val);
        } else {
            c->terminal_charset[0] = '\0';
        }
    } else if (strcmp(key, "default_paper") == 0) {
        if (strcmp(val, "a4") == 0) {
            str_copy(c->default_paper, sizeof c->default_paper, val);
        } else if (strcmp(val, "letter") == 0) {
            c->default_paper[0] = '\0';
        }
    } else if (set_color(c, key, val) != 0) {
        return;
    } else if (strcmp(key, "latex_markers") == 0) {
        int allowed = 1;

        set_bool(&allowed, val);
        c->latex_markers_off = allowed == 0;
    }
}


static void
read_file(Config *c, FILE *f)
{
    char ln[1024];

    while (fgets(ln, sizeof ln, f) != NULL) {
        ln[strcspn(ln, "\r\n")] = '\0';
        char *eq = strchr(ln, '=');

        if (eq != NULL) {
            *eq = '\0';
            set_value(c, ln, eq + 1);
        }
    }
}


/* The files of the older versions: ~/.config/strata/strata.conf. */
static void
read_legacy(Config *c)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char path[PATH_MAX];

    if (xdg != NULL && xdg[0] != '\0') {
        snprintf(path, sizeof path, "%s/strata/strata.conf", xdg);
    } else if (home != NULL) {
        snprintf(path, sizeof path, "%s/.config/strata/strata.conf", home);
    } else {
        return;
    }

    FILE *f = fopen(path, "r");

    if (f != NULL) {
        read_file(c, f);
        fclose(f);

        /* The files of the older versions never held the PDF viewer. */
        c->pdf_viewer[0] = '\0';
    }
}


int
config_load(Config *c)
{
    char dir[PATH_MAX - 64];
    char path[PATH_MAX];

    config_defaults(c);

    if (config_directory(dir, sizeof dir) != 0) {
        return 1;
    }

    int d = dir_check(dir);

    if (d < 0) {
        return 1;
    }

    snprintf(path, sizeof path, "%s/config", dir);

    FILE *f = d == 0 ? fopen(path, "r") : NULL;

    if (f == NULL) {
        /* Nothing yet: take what the older versions kept. */
        if (getenv("STRATA_HOME") == NULL) {
            read_legacy(c);
        }

        return 1;
    }

    /* The file must be the user's own, and nobody else's to use. */
    struct stat st;

    if (fstat(fileno(f), &st) != 0 || st.st_uid != geteuid()) {
        log_warn("config: the file of the settings belongs to another user, "
                 "so it is not read (%s)", path);
        fclose(f);

        return 1;
    }

    /* The file could have been changed by others when its group or others
       may write to it: the program that it names is not trusted then. */
    c->file_trusted = (st.st_mode & (S_IWGRP | S_IWOTH)) == 0;

    if ((st.st_mode & 0777) != 0600) {
        fchmod(fileno(f), 0600);
    }

    read_file(c, f);
    fclose(f);

    /* The PDF viewer is a full path.  A name or a relative path (a file of
       an older version) is never run: it is not used, and Setup reports so. */
    if (c->pdf_viewer[0] != '\0' && c->pdf_viewer[0] != '/') {
        log_warn("config: the setting pdf_viewer (%.100s) is not used: it "
                 "is not a full path", c->pdf_viewer);
        str_copy(c->pdf_viewer_old, sizeof c->pdf_viewer_old, c->pdf_viewer);
        c->pdf_viewer[0] = '\0';
    }

    if (c->file_trusted == 0 && c->pdf_viewer[0] != '\0') {
        log_warn("config: the setting pdf_viewer is ignored, because the "
                 "file of the settings could be changed by other users (%s)",
                 path);
        c->pdf_viewer[0] = '\0';
    }

    return 0;
}


int
config_save(const Config *c, char *err, int errcap)
{
    char dir[PATH_MAX - 64];
    char path[PATH_MAX];
    char tmp[PATH_MAX];

    if (config_directory(dir, sizeof dir) != 0) {
        snprintf(err, usz(errcap), "There is no home directory");

        return -1;
    }

    int d = dir_check(dir);

    if (d == 1 && mkdir(dir, 0700) != 0) {
        snprintf(err, usz(errcap), "Cannot make %s: %s", dir, strerror(errno));

        return -1;
    }

    if (d == 1) {
        /* The mode of mkdir is changed by the umask. */
        d = dir_check(dir);
    }

    if (d != 0) {
        snprintf(err, usz(errcap), "%s is not a directory of yours", dir);

        return -1;
    }

    snprintf(path, sizeof path, "%s/config", dir);
    snprintf(tmp, sizeof tmp, "%s/config.%d", dir, getpid());

    FILE *f = NULL;
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);

    if (fd >= 0) {
        f = fdopen(fd, "w");
    }

    if (f == NULL) {
        char msg[400];

        snprintf(msg, sizeof msg, "Cannot write %.200s: %.100s", tmp,
                 strerror(errno));
        str_copy(err, usz(errcap), msg);

        if (fd >= 0) {
            close(fd);
        }

        return -1;
    }

    fprintf(f, "workdir=%s\nstred=%s\nhelp=%s\n", c->workdir, c->stred,
            c->help);

    if (c->initial_macros[0] != '\0') {
        fprintf(f, "initial_macros=%s\n", c->initial_macros);
    }

    if (c->initial_library[0] != '\0') {
        fprintf(f, "initial_library=%s\n", c->initial_library);
    }

    fprintf(f, "autosave=%s\nautosave_seconds=%d\n",
            c->autosave != 0 ? "yes" : "no", c->autosave_seconds);
    fprintf(f, "expanded_status=%s\nonscreen_help=%s\nshow_marks=%s\n",
            c->expanded_status != 0 ? "yes" : "no",
            c->onscreen_help != 0 ? "yes" : "no",
            c->show_marks != 0 ? "yes" : "no");
    fprintf(f, "show_dots=%s\n", c->show_dots != 0 ? "yes" : "no");

    /* The default is saved as nothing, as for the other numbers. */
    if (c->backups != CONFIG_BACKUPS_DEFAULT) {
        fprintf(f, "backups=%d\n", config_clamp_backups(c->backups));
    }


    /* A file of an older version keeps its key printer until Setup is
       accepted: then the keys print_to and print_queue say it. */
    if (c->print_to[0] != '\0') {
        fprintf(f, "print_to=%s\n", c->print_to);

        if (c->print_queue[0] != '\0') {
            fprintf(f, "print_queue=%s\n", c->print_queue);
        }
    } else {
        fprintf(f, "printer=%s\n", c->printer);
    }

    if (c->print_last_printer[0] != '\0') {
        fprintf(f, "print_last_printer=%s\n", c->print_last_printer);
    }

    if (c->print_last_sides[0] != '\0') {
        fprintf(f, "print_last_sides=%s\n", c->print_last_sides);
    }

    if (c->print_last_sort[0] != '\0') {
        fprintf(f, "print_last_sort=%s\n", c->print_last_sort);
    }

    if (c->print_last_bw[0] != '\0') {
        fprintf(f, "print_last_bw=%s\n", c->print_last_bw);
    }

    if (c->pdf_dir[0] != '\0') {
        fprintf(f, "pdf_dir=%s\n", c->pdf_dir);
    }

    if (c->pdf_viewer[0] != '\0') {
        fprintf(f, "pdf_viewer=%s\n", c->pdf_viewer);
    }

    if (c->spell_lang[0] != '\0') {
        fprintf(f, "spell_language=%s\n", c->spell_lang);
    }

    if (c->latex_keep_pictures != 0) {
        fprintf(f, "latex_pictures=keep\n");
    }

    if (c->latex_positions != 0) {
        fprintf(f, "latex_positions=yes\n");
    }

    if (c->latex_markers_off != 0) {
        fprintf(f, "latex_markers=no\n");
    }

    if (c->undo_levels != CONFIG_UNDO_DEFAULT) {
        fprintf(f, "undo_levels=%d\n", config_clamp_undo(c->undo_levels));
    }

    if (c->x11_rows != 0 && c->x11_rows != CONFIG_X11_ROWS_DEFAULT) {
        fprintf(f, "x11_rows=%d\n", c->x11_rows);
    }

    if (c->terminal_rows != 0) {
        fprintf(f, "terminal_rows=%d\n", c->terminal_rows);
    }

    if (c->terminal_charset[0] != '\0') {
        fprintf(f, "terminal_charset=%s\n", c->terminal_charset);
    }

    if (c->default_paper[0] != '\0') {
        fprintf(f, "default_paper=%s\n", c->default_paper);
    }

    for (int gui = 0; gui < 2; gui++) {
        for (int kind = 0; kind < CK_COUNT; kind++) {
            for (int bg = 0; bg < 2; bg++) {
                char name[80];

                if (c->colors[gui][kind][bg][0] != '\0') {
                    config_color_key(name, sizeof name, gui, kind, bg);
                    fprintf(f, "%s=%s\n", name, c->colors[gui][kind][bg]);
                }
            }
        }
    }

    if (fclose(f) != 0 || rename(tmp, path) != 0) {
        snprintf(err, usz(errcap), "Cannot write %s: %s", path,
                 strerror(errno));
        unlink(tmp);

        return -1;
    }

    return 0;
}


FILE *
config_read_file(const char *name)
{
    char dir[PATH_MAX - 64];
    char path[PATH_MAX];
    struct stat st;

    if (config_directory(dir, sizeof dir) != 0 || dir_check(dir) != 0) {
        return NULL;
    }

    snprintf(path, sizeof path, "%s/%s", dir, name);

    FILE *f = fopen(path, "r");

    if (f == NULL) {
        return NULL;
    }

    if (fstat(fileno(f), &st) != 0 || st.st_uid != geteuid() ||
        S_ISREG(st.st_mode) == 0) {
        fclose(f);

        return NULL;
    }

    if ((st.st_mode & 0777) != 0600) {
        fchmod(fileno(f), 0600);
    }

    return f;
}


/* An error message made of an action, a name and a reason. */
static void
set_error(char *err, int errcap, const char *what, const char *name,
          const char *why)
{
    char msg[400];

    snprintf(msg, sizeof msg, "%.60s %.200s%s%.100s", what, name,
             why[0] != '\0' ? ": " : "", why);
    str_copy(err, usz(errcap), msg);
}


int
config_write_file(const char *name, const char *data, char *err, int errcap)
{
    char dir[PATH_MAX - 64];
    char path[PATH_MAX];
    char tmp[PATH_MAX];

    if (config_directory(dir, sizeof dir) != 0) {
        set_error(err, errcap, "There is no home directory", "", "");

        return -1;
    }

    int d = dir_check(dir);

    if (d == 1 && mkdir(dir, 0700) != 0) {
        set_error(err, errcap, "Cannot make", dir, strerror(errno));

        return -1;
    }

    if (d == 1) {
        d = dir_check(dir);
    }

    if (d != 0) {
        set_error(err, errcap, "Not a directory of yours:", dir, "");

        return -1;
    }

    snprintf(path, sizeof path, "%s/%s", dir, name);
    snprintf(tmp, sizeof tmp, "%s/%s.%d", dir, name, (int)getpid());

    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);
    FILE *f = fd >= 0 ? fdopen(fd, "w") : NULL;

    if (f == NULL) {
        set_error(err, errcap, "Cannot write", tmp, strerror(errno));

        if (fd >= 0) {
            close(fd);
        }

        return -1;
    }

    fputs(data, f);

    if (fclose(f) != 0 || rename(tmp, path) != 0) {
        set_error(err, errcap, "Cannot write", path, strerror(errno));
        unlink(tmp);

        return -1;
    }

    return 0;
}


int
config_set_rows(int x11, int rows, int max, char *err, int errcap)
{
    const char *key = x11 != 0 ? "x11_rows" : "terminal_rows";
    size_t klen = strlen(key);
    static char data[65536];
    size_t n = 0;

    if (rows != 0) {
        rows = config_clamp_rows(rows, max);
    }

    int drop = rows == 0 || (x11 != 0 && rows == CONFIG_X11_ROWS_DEFAULT);

    data[0] = '\0';

    /* The file is read now, so that the change of the other program stays. */
    FILE *f = config_read_file("config");

    if (f != NULL) {
        char ln[1024];

        while (fgets(ln, sizeof ln, f) != NULL) {
            size_t len = strlen(ln);

            if (strncmp(ln, key, klen) == 0 && ln[klen] == '=') {
                continue;
            }

            if (n + len + 2 >= sizeof data) {
                break;
            }

            memcpy(data + n, ln, len);
            n += len;

            if (len > 0 && ln[len - 1] != '\n') {
                data[n] = '\n';
                n++;
            }
        }

        fclose(f);
    }

    data[n] = '\0';

    if (drop == 0) {
        snprintf(data + n, sizeof data - n, "%s=%d\n", key, rows);
    }

    return config_write_file("config", data, err, errcap);
}
