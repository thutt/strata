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

/* str.c - the "master menu": a stand-in for MS.EXE's Document Manager.
 *
 * This program has a host that starts the editor, the Printer and the
 * utilities.  It keeps the part that users see first: the Document
 * Manager menu and its panels (Edit / Preview / Print Document, Setup,
 * TeX Font Association).  Real work is delegated to `stred`, run as a child
 * process.  Everything that has no implementation is flagged as such, with F1
 * showing the description of it.
 *
 * Panel conventions:  Up/Down/Tab move between fields,
 * Enter accepts a field (and moves on), Ins accepts the whole panel, Esc
 * cancels, F1 is help, and Keypad + (the '+' key) at the Document Name prompt
 * lists the available files.
 */
#include "util.h"
#include "comparemenu.h"
#include "restoremenu.h"
#include "dictmenu.h"
#include "template.h"
#include "widgets.h"
#include "backup.h"
#include "colorui.h"
#include "color.h"
#include "term.h"
#include "layout.h"
#include "help.h"
#include "config.h"
#include "fonts.h"
#include "printjob.h"
#include "log.h"
#include "logrun.h"
#include "vars.h"
#include "sysprog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

#define NUM(a) isz(sizeof(a) / sizeof((a)[0]))
#define VERSION_TITLE "Strata Document Manager"
/* The name of the entry that opens a document. */
#define EDIT_ENTRY "Edit / Preview / Print Document"

static char helppath[512];
static char msed_path[512];
/* Autosave of the editor: yes or no, and the interval in seconds. */
static int autosave_yes = 1;
static int autosave_secs = 120;
static char workdir[PATH_MAX];
/* The default CUPS queue of Setup, Printer; "" for none. */
/* Where a print goes (Setup): "printer", "file" or "open"; the queue ("" is
   the default of the system); the directory of the PDF ("" is the
   document's). */
static char print_to_val[16];
static char print_queue_val[PJ_QNAME];
static char pdf_dir_val[PATH_MAX];
/* The language of the spell checker, such as en_US (Setup). */
static char spell_lang[32] = "en_US";
static char conf_file[PATH_MAX];
static char status_msg[160];
static void (*screen)(void);
/* paints whatever is "under" popups */


/* ============================================================ configuration */
/*
 * The settings of the user are in ~/.strata/config (config.c), which only
 * the user can use; the editor reads and writes the same file.
 */
static Config cfg;

/* The rows of the windows (Setup); 0 is the default of the version. */
static int x11_rows_val;
static int term_rows_val;
/* Setup: terminal_charset: auto, utf8, vt100, ascii. */
static char charset_val[8];
static char paper_val[8]; /* Setup: default_paper: "" for Letter, or a4 */
static int undo_levels_val; /* Setup: the operations that Undo keeps. */
static int backups_val; /* Setup: the numbered backups that a save keeps. */
static char macros_val[PATH_MAX]; /* Setup: the macro set loaded at start. */
static char library_val[PATH_MAX]; /* Setup: the library loaded at start. */

/* The program that shows the PDF of Print To PDF viewer; "" is evince. */
static char pdf_viewer[PATH_MAX];


static void
conf_load(void)
{
    config_load(&cfg);
    str_copy(workdir, sizeof workdir, cfg.workdir);
    str_copy(macros_val, sizeof macros_val, cfg.initial_macros);
    str_copy(library_val, sizeof library_val, cfg.initial_library);
    str_copy(msed_path, sizeof msed_path, cfg.stred);
    str_copy(helppath, sizeof helppath, cfg.help);
    autosave_yes = cfg.autosave;
    autosave_secs = cfg.autosave_seconds;
    str_copy(print_to_val, sizeof print_to_val,
             config_print_to(&cfg) == CONFIG_PRINT_PRINTER ? "printer" :
             config_print_to(&cfg) == CONFIG_PRINT_OPEN    ? "viewer" :
                                                             "file");
    config_print_queue(&cfg, print_queue_val, sizeof print_queue_val);
    str_copy(pdf_dir_val, sizeof pdf_dir_val, cfg.pdf_dir);
    str_copy(spell_lang, sizeof spell_lang,
             cfg.spell_lang[0] != '\0' ? cfg.spell_lang : "en_US");
    x11_rows_val = cfg.x11_rows;
    term_rows_val = cfg.terminal_rows;
    str_copy(charset_val, sizeof charset_val,
             cfg.terminal_charset[0] != '\0' ? cfg.terminal_charset : "auto");
    str_copy(paper_val, sizeof paper_val, cfg.default_paper);
    undo_levels_val = cfg.undo_levels;
    backups_val = cfg.backups;
    str_copy(pdf_viewer, sizeof pdf_viewer, cfg.pdf_viewer);
    config_path(conf_file, sizeof conf_file);
}


static void
default_msed(char *out, size_t cap);


static int
conf_save(void)
{
    char err[160];
    char dflt_msed[PATH_MAX];

    /* The settings of the editor are in the file too: take the current ones. */
    config_load(&cfg);
    str_copy(cfg.workdir, sizeof cfg.workdir, workdir);
    str_copy(cfg.initial_macros, sizeof cfg.initial_macros, macros_val);
    str_copy(cfg.initial_library, sizeof cfg.initial_library, library_val);
    /* A path is kept only when the user chose it: the place where stred is
       found by itself changes with every build and must not be fixed. */
    default_msed(dflt_msed, sizeof dflt_msed);
    str_copy(cfg.stred, sizeof cfg.stred,
             strcmp(msed_path, dflt_msed) == 0 ? "" : msed_path);
    str_copy(cfg.help, sizeof cfg.help, helppath);
    cfg.autosave = autosave_yes;
    cfg.autosave_seconds = autosave_secs;
    cfg.printer[0] = '\0';
    str_copy(cfg.print_to, sizeof cfg.print_to, print_to_val);
    str_copy(cfg.print_queue, sizeof cfg.print_queue, print_queue_val);
    str_copy(cfg.pdf_dir, sizeof cfg.pdf_dir, pdf_dir_val);
    str_copy(cfg.spell_lang, sizeof cfg.spell_lang,
             strcmp(spell_lang, "en_US") == 0 ? "" : spell_lang);
    cfg.x11_rows = x11_rows_val;
    cfg.terminal_rows = term_rows_val;
    str_copy(cfg.terminal_charset, sizeof cfg.terminal_charset,
             strcmp(charset_val, "auto") == 0 ? "" : charset_val);
    str_copy(cfg.default_paper, sizeof cfg.default_paper, paper_val);
    cfg.undo_levels = undo_levels_val;
    cfg.backups = backups_val;
    cfg.backups_explicit = 1;
    str_copy(cfg.pdf_viewer, sizeof cfg.pdf_viewer, pdf_viewer);
    colors_page_commit(&cfg);

    return config_save(&cfg, err, sizeof err);
}


/* ================================================================ utilities */
static int
is_dir(const char *p)
{
    struct stat st;

    return stat(p, &st) == 0 && (S_ISDIR(st.st_mode) != 0) != 0;
}


static int
exists(const char *p)
{
    return access(p, F_OK) == 0;
}


/*
 * Where stred is when the user has not said: the STRED variable, the stred
 * next to this program (stred-x11 next to str-x11), or the one found in the
 * fixed folders of the system.
 */
static void
default_msed(char *out, size_t cap)
{
    const char *env = getenv("STRED");
    char self[PATH_MAX];
    ssize_t n;

    if (env != NULL && *env != '\0') {
        str_copy(out, cap, env);

        return;
    }

    n = readlink("/proc/self/exe", self, sizeof self - 1);

    if (n > 0) {
        self[n] = 0;
        char *sl = strrchr(self, '/');

        if (sl != NULL) {
            /* The Editor that belongs to this program: the program str has
               stred beside it, and a program str-x11 has stred-x11, so that
               every version starts its own Editor. */
            char suffix[64];

            suffix[0] = '\0';

            if (strncmp(sl + 1, "str", 3) == 0) {
                str_copy(suffix, sizeof suffix, sl + 4);
            }

            snprintf(sl + 1, sizeof self - lsz(sl + 1 - self), "stred%s",
                     suffix);

            if (access(self, X_OK) == 0) {
                str_copy(out, cap, self);

                return;
            }
        }
    }

    /* Last, the fixed folders of the system (where an install puts it), and
       never PATH; the name alone is left when it is not there. */
    if (sp_find("stred", out, cap, NULL) != 0) {
        str_copy(out, cap, "stred");
    }
}


static void
find_msed(void)
{
    if (msed_path[0] != '\0' && access(msed_path, X_OK) == 0) {
        return;
    }

    default_msed(msed_path, sizeof msed_path);
}


/* run stred (or anything) as a child; inherit the terminal when
   `interactive` */


/*
 * Writes the end of a child to the log of the run and returns its exit
 * status: the status of the program, 128 when a signal ended it, 127 when
 * it could not be started.
 */
static int
child_status(const char *program, pid_t pid, int st)
{
    int status = WIFEXITED(st) ? WEXITSTATUS(st) : 128;
    long number = pid;

    if (pid < 0) {
        log_error("str: cannot start %s: fork failed: %s", program,
                  strerror(errno));
    } else if (WIFSIGNALED(st)) {
        log_error("str: %s (process %ld) ended on signal %d", program,
                  number, WTERMSIG(st));
    } else if (status != 0) {
        log_warn("str: %s (process %ld) ended with exit status %d", program,
                 number, status);
    } else {
        log_note("str: %s (process %ld) ended normally", program, number);
    }

    return status;
}


static int
run_child(const char *dir, char *const argv[], int interactive)
{
    /* X11: the child opens its own window */
    int gui = interactive != 0 && term_is_gui() != 0;

    if (interactive != 0 && gui == 0) {
        term_close();
        fflush(stdout);
    }

    pid_t p = fork();

    if (p == 0) {
        if (dir != NULL && *dir != '\0' && chdir(dir) != 0) {
            _exit(126);
        }

        if (interactive == 0) {
            if (freopen("/dev/null", "w", stderr) == NULL ||
                freopen("/dev/null", "w", stdout) == NULL) {
                _exit(126);
            }
        }

        if (strchr(argv[0], '/') != NULL) {
            execv(argv[0], argv);
        } else {
            /* A name without a slash is looked for in the fixed folders of
               the system and run by its full path; PATH is never searched. */
            char full[PATH_MAX];

            if (sp_find(argv[0], full, sizeof full, NULL) == 0) {
                execv(full, argv);
            }
        }

        _exit(127);
    }

    int st = 0;

    if (p > 0) {
        if (gui != 0) {
            while (waitpid(p, &st, WNOHANG) == 0) {
                term_idle(), usleep(30000);
            }
        } else {
            /*
             * A resize of the terminal delivers SIGWINCH to this process as
             * well, which interrupts the wait.  Wait again until the child
             * has really finished.
             */
            pid_t r = waitpid(p, &st, 0);

            while (r < 0 && errno == EINTR) {
                r = waitpid(p, &st, 0);
            }
        }
    } else {
        st = 127 << 8;
    }

    if (interactive != 0 && gui == 0) {
        term_open();
        term_set_title("str");
    }

    return child_status(argv[0], p, st);
}


static void
say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>


static void
say(const char *fmt, ...)
{
    char full[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(full, sizeof full, fmt, ap);
    va_end(ap);
    fit_message(full, term_cols() - 2, status_msg, sizeof status_msg);
}


/* ================================================================ screens */
/* The keys of a field with choices, for the foot of the screen; it is empty
   when the field has none. */
static const char *footer_keys = "";

static void
paint_footer(void)
{
    int R = term_rows();
    int C = term_cols();

    char shown[256];
    char plain[256];

    /* On a field with choices the keys of the choice are named, as the
       panels of the Editor do. */
    snprintf(plain, sizeof plain, "F1 Help   Esc Cancel%s", footer_keys);
    fit_message(status_msg[0] != '\0' ? status_msg : plain,
                C - 2, shown, sizeof shown);
    term_fill(0, R - 1, C, 1, ' ', TS_MESSAGE);
    term_puts(1, R - 1, shown,
              TS_MESSAGE | (status_msg[0] != '\0' ? TA_BOLD : 0));
}


static void
paint_blank(void)
{
    term_clear();
    paint_footer();
    term_cursor(0, 0, 0);
}


/* A "not implemented" notice: F1 shows its description. */
static void
not_implemented(const char *what, const char *help_title)
{
    for (;;) {
        w_base();
        int C = term_cols();
        int R = term_rows();
        char l1[200];

        str_copy(l1, sizeof l1, what);
        const char *l2 = "is not implemented in this version.";
        const char *l3 = w_help != NULL ?
                             "F1: read the help text "
                             "   any other key: continue" :
                             "any key: continue";
        int w = slen(l3) + 6;

        if (slen(l1) + 6 > w) {
            w = slen(l1) + 6;
        }

        if (slen(l2) + 6 > w) {
            w = slen(l2) + 6;
        }

        if (w > C - 2) {
            w = C - 2;
        }

        int x = (C - w) / 2;
        int y = R / 2 - 3;

        box(x, y, w, 7, "Not implemented");
        term_puts(x + (w - slen(l1)) / 2, y + 2, l1, TA_BOLD);
        term_puts(x + (w - slen(l2)) / 2, y + 3, l2, 0);
        term_puts(x + (w - slen(l3)) / 2, y + 5, l3, TA_DIM);
        term_cursor(0, 0, 0);
        term_present();
        int k = term_getkey(-1);

        if ((K_BASE(k) == K_F(1) && w_help) != 0) {
            int t = help_ctx_topic(help_title);

            if (t < 0 && help_title != NULL) {
                /* "(TIFF)" suffixes and the like */
                char tmp[160];

                str_copy(tmp, sizeof tmp, help_title);
                char *p = strstr(tmp, " (");

                if (p != NULL) {
                    *p = 0;
                    t = help_ctx_topic(tmp);
                }
            }

            help_viewer(t >= 0 ? t : help_ctx_topic("Document Manager"));
            continue;
        }

        if (k != K_RESIZE && k != K_NONE) {
            return;
        }
    }
}


/* ================================================================== menus */
static void
paint_menu(const char *title, const char *bottom, const char **items, int n,
           int sel, const char *note)
{
    term_clear();
    int C = term_cols();
    int R = term_rows();
    int lw = 0;

    for (int i = 0; i < n; i++) {
        if (slen(items[i]) > lw) {
            lw = slen(items[i]);
        }
    }

    int w = lw + 16;

    if (w < slen(title) + 10) {
        w = slen(title) + 10;
    }

    if (w > C - 2) {
        w = C - 2;
    }

    int h = n + 4;
    int x = (C - w) / 2;
    int y = (R - h) / 2 - 1;

    if (y < 1) {
        y = 1;
    }

    term_style(TS_PANEL);
    term_fill(x, y, w, h, ' ', 0);
    term_box(x, y, w, h, 0, 1);
    int tl = slen(title);
    int bl = slen(bottom);

    term_puts(x + (w - tl - 2) / 2, y, " ", 0);
    term_puts(x + (w - tl - 2) / 2 + 1, y, title, TA_BOLD);
    term_puts(x + (w - tl - 2) / 2 + 1 + tl, y, " ", 0);
    term_puts(x + (w - bl - 2) / 2, y + h - 1, " ", 0);
    term_puts(x + (w - bl - 2) / 2 + 1, y + h - 1, bottom, TA_BOLD);
    term_puts(x + (w - bl - 2) / 2 + 1 + bl, y + h - 1, " ", 0);
    int ix = x + (w - lw) / 2;

    for (int i = 0; i < n; i++) {
        if (i == sel) {
            term_fill(ix - 1, y + 2 + i, lw + 2, 1, ' ', TA_REVERSE);
        }

        term_puts(ix, y + 2 + i, items[i], i == sel ? TA_REVERSE : 0);
    }

    if (note != NULL) {
        term_puts((C - slen(note)) / 2, y + h + 1, note, TA_DIM);
    }

    paint_footer();
    term_cursor(0, 0, 0);
}

static const char *cur_title;
static const char *cur_bottom;
static const char *cur_note;
static const char **cur_items;
static int cur_n;
static int cur_sel;


static void
paint_cur_menu(void)
{
    paint_menu(cur_title, cur_bottom, cur_items, cur_n, cur_sel, cur_note);
}


/* returns the chosen index; -1 on Esc (when allow_esc) */
static int
run_menu(const char *title, const char *bottom, const char **items, int n,
         const char *help_title, const char *note, int allow_esc, int *keep)
{
    int sel = keep != NULL ? *keep : 0;
    void (*prev)(void) = screen;

    cur_title = title;
    cur_bottom = bottom;
    cur_items = items;
    cur_n = n;
    cur_note = note;
    screen = paint_cur_menu;

    for (;;) {
        cur_sel = sel;
        paint_cur_menu();
        term_present();
        status_msg[0] = 0;
        int k = term_getkey(-1);
        int b = K_BASE(k);

        if (b == K_UP) {
            sel = (sel + n - 1) % n;
        } else if (b == K_DOWN) {
            sel = (sel + 1) % n;
        } else if (b == K_HOME) {
            sel = 0;
        } else if (b == K_END) {
            sel = n - 1;
        } else if (b == 13 || b == K_INS) {
            if (keep != NULL) {
                *keep = sel;
            }

            screen = prev;
            return sel;
        } else if (b == K_ESC) {
            if (allow_esc != 0) {
                screen = prev;
                return -1;
            }
        } else if (b == K_F(1)) {
            char place[200];

            snprintf(place, sizeof place, "%s:%s", help_title, items[sel]);
            help_context(place);
        } else if (k >= 32 && k < 127) {
            for (int i = 1; i <= n; i++) {
                int j = (sel + i) % n;

                if (to_upper(items[j][0]) == to_upper(k)) {
                    sel = j;
                    break;
                }
            }
        }
    }
}

/* ================================================================== forms */
typedef struct {
    const char *label;
    char val[PATH_MAX];
    int na;
    int cap;
    const char *na_help;
    /*
     * A row of two edit fields: sub1 and sub2 are the words before them,
     * the first text is val and the second one is val2.  Both are NULL for
     * a row of one field.
     */
    const char *sub1;
    const char *sub2;
    char val2[16];
    /* 1 for a line of text under a field: it has no focus and no edit. */
    int note;
    /*
     * A choice field: the entries (the text of val is one of them), and the
     * words that the list shows after each entry (or NULL).  Left and Right
     * change the entry, a letter jumps to an entry that starts with it, and
     * F2 shows the list; a name cannot be made up.
     */
    const char **choices;
    const char **marks;
    int nchoices;
    /*
     * The default of the field, for the panels that show it: def is the
     * value that Del puts back and defshow the words that are shown for it
     * (NULL shows def).  A field without a default has def NULL.  help is a
     * line about the field for the foot of the panel.  button is 1 for a
     * line that opens another panel: Enter and F2 open it.
     */
    const char *def;
    const char *defshow;
    const char *help;
    int button;
    /* The page of a panel of several pages that the field is on, from 0. */
    int page;
} Field;


static const char *const name_labels[] = {
    "Document Name:", "Initial Macro Set:", "Initial Library:",
    "Initial Working Directory:", "Working Directory:", "Editor program:",
    "Help File:",
};


/*
 * Does the field hold the name of a file or a folder?  Such a field holds
 * UTF-8, as the file system has it, and is edited one character at a time;
 * the other fields hold the code page of the document format.
 */
static int
is_name_field(const Field *f)
{
    for (int i = 0; i < NUM(name_labels); i++) {
        if (f->label != NULL && strcmp(f->label, name_labels[i]) == 0) {
            return 1;
        }
    }

    return 0;
}
typedef int (*FieldHook)(Field *f, int idx, int key);

/*
 * What a panel can ask of form(): check, when it is not NULL, tests a field
 * (why is the reason, cap its room) and returns 0 when the value is right;
 * a field is tested when the focus leaves it and all of them when Ins is
 * pressed, and a wrong value keeps the panel open with the reason in the
 * line of the foot.  del_default is 1 when Del puts the default of a field
 * back.  bottom replaces the line at the foot of the frame.
 */
typedef int (*FieldCheck)(const Field *f, int idx, char *why, size_t cap);
static FieldCheck f_check;
static int f_del_default;
static const char *f_bottom_text;
static char f_error[300]; /* The reason that a field was refused, or "". */

/*
 * A panel of several pages, as the panels of the Editor with PgUp and PgDn:
 * pages_set is the number of pages that the caller of form() asks for (1 is
 * the usual panel) and page_titles_set the title of each page (or NULL for
 * the one title).  PgUp shows the next page and PgDn the one before, and the
 * first and the last page follow each other; Ins keeps the fields of all
 * the pages and Esc leaves all of them as they were.
 */
static int f_pages_set = 1;
static const char *const *f_page_titles_set;
/*
 * A page that is not a column of fields (the colors of Setup): custom_set is
 * its number (-1 for none).  It is drawn and given the keys by the code of
 * the colors, but it is shown, left and ended as any page.
 */
static int f_custom_set = -1;
static int f_custom = -1;
static int f_pages = 1; /* The pages of the panel that is shown. */
static int f_page; /* The page that is shown. */
static const char *const *f_page_titles;
static const char *f_title;
static const char *f_bottom;
static Field *f_fields;
static int f_n;
static int f_focus;
static int f_pos;
static int f_first;
static int f_top; /* The first field that is shown, when not all fit. */
static int f_sub; /* 0 or 1: the edit field of a row of two. */


/* The text and the room of the edit field sub of a row. */
static char *
field_text(Field *f, int sub)
{
    return sub != 0 ? f->val2 : f->val;
}


static int
field_cap(const Field *f, int sub)
{
    return sub != 0 ? isz(sizeof f->val2) : f->cap;
}


/* The room that an edit field of a row of two shows. */
#define SUB_WIDTH 7


/*
 * The fields of the page that is shown: how many, the field that is the
 * pos-th of them, and the place of field idx among them.  A panel of one
 * page shows all its fields.
 */
static int
on_page(const Field *f)
{
    return f->page == f_page;
}


static int
page_count(const Field *fields, int n)
{
    int c = 0;

    for (int i = 0; i < n; i++) {
        c += on_page(&fields[i]) != 0;
    }

    return c;
}


static int
page_field(const Field *fields, int n, int pos)
{
    for (int i = 0, c = 0; i < n; i++) {
        if (on_page(&fields[i]) != 0 && c++ == pos) {
            return i;
        }
    }

    return 0;
}


static int
page_pos(const Field *fields, int n, int idx)
{
    for (int i = 0, c = 0; i < n; i++) {
        if (on_page(&fields[i]) != 0) {
            if (i == idx) {
                return c;
            }

            c++;
        }
    }

    return 0;
}


static void
paint_custom_page(void)
{
    int x;
    int y;
    int w;
    int h;
    const char *ptitle = f_page_titles != NULL ? f_page_titles[f_page] :
                                                 f_title;
    int tl = slen(ptitle);

    footer_keys = "";
    term_clear();
    colors_page_paint(&x, &y, &w, &h);
    term_style(TS_PANEL);
    term_fill(x, y, w, h, ' ', 0);
    term_box(x, y, w, h, 0, 1);
    term_puts(x + (w - tl - 2) / 2, y, " ", 0);
    term_puts(x + (w - tl - 2) / 2 + 1, y, ptitle, TA_BOLD);
    term_puts(x + (w - tl - 2) / 2 + 1 + tl, y, " ", 0);
    colors_page_content(x, y, w);
    paint_footer();
}


static void
paint_form(void)
{
    if (f_custom >= 0 && f_page == f_custom) {
        paint_custom_page();

        return;
    }

    footer_keys = f_fields[f_focus].nchoices > 0 ?
                      "   Space Next   Left Right Choose   F2 List" :
                      "";
    term_clear();
    int C = term_cols();
    int R = term_rows();
    int lw = 0;

    for (int i = 0; i < f_n; i++) {
        if (slen(f_fields[i].label) > lw) {
            lw = slen(f_fields[i].label);
        }
    }

    int w = C - 8;

    if (w > 76) {
        w = 76;
    }

    if (w < lw + 24) {
        w = lw + 24;
    }

    /* A panel that is taller than the screen shows some of its fields; a
       panel of several pages shows the fields of one page. */
    int total = page_count(f_fields, f_n);
    int vis = total;
    int fpos = page_pos(f_fields, f_n, f_focus);

    if (vis > R - 6) {
        vis = R - 6;
    }

    if (fpos < f_top) {
        f_top = fpos;
    }

    if (fpos >= f_top + vis) {
        f_top = fpos - vis + 1;
    }

    int h = vis + 4;
    int x = (C - w) / 2;
    int y = (R - h) / 2 - 1;

    if (y < 1) {
        y = 1;
    }

    term_style(TS_PANEL);
    term_fill(x, y, w, h, ' ', 0);
    term_box(x, y, w, h, 0, 1);
    const char *ptitle = f_page_titles != NULL ? f_page_titles[f_page] :
                                                 f_title;
    int tl = slen(ptitle);
    int bl = slen(f_bottom);

    term_puts(x + (w - tl - 2) / 2, y, " ", 0);
    term_puts(x + (w - tl - 2) / 2 + 1, y, ptitle, TA_BOLD);
    term_puts(x + (w - tl - 2) / 2 + 1 + tl, y, " ", 0);
    term_puts(x + (w - bl - 2) / 2, y + h - 1, " ", 0);
    term_puts(x + (w - bl - 2) / 2 + 1, y + h - 1, f_bottom, TA_BOLD);
    term_puts(x + (w - bl - 2) / 2 + 1 + bl, y + h - 1, " ", 0);
    int vx = x + lw + 4;
    int vw = w - lw - 6;

    if (f_top > 0) {
        term_put(x + w - 2, y + 2, 0x18, 0);
    }

    if (f_top + vis < total) {
        term_put(x + w - 2, y + 1 + vis, 0x19, 0);
    }

    for (int pos = f_top; pos < f_top + vis; pos++) {
        int i = page_field(f_fields, f_n, pos);
        Field *f = &f_fields[i];
        int ry = y + 2 + pos - f_top;

        if (f->note != 0) {
            /*
             * A line of text across the panel: it is not an edit field.  A
             * long path in it is shortened from the front, so that the end
             * of the path, which is what matters, is seen.
             */
            char shown[1100];

            fit_message(f->val, w - 4, shown, sizeof shown);

            /* note 1 is dim text, 2 a refusal (bold) and 3 a heading. */
            for (int k = 0; k < w - 4 && shown[k] != '\0'; k++) {
                term_put(x + 2 + k, ry, u8(shown[k]),
                         f->note == 1 ? TA_DIM : TA_BOLD);
            }

            continue;
        }

        term_puts(x + 2 + lw - slen(f->label), ry, f->label,
                  f->na != 0 ? TA_DIM : 0);

        if (f->sub1 != NULL) {
            /* A row of two edit fields: "X11: [   ]  Terminal: [   ]". */
            int ex[2];

            ex[0] = vx + slen(f->sub1) + 1;
            ex[1] = ex[0] + SUB_WIDTH + 2 + slen(f->sub2) + 1;
            term_puts(vx, ry, f->sub1, 0);
            term_puts(ex[0] + SUB_WIDTH + 2, ry, f->sub2, 0);

            for (int sub = 0; sub < 2; sub++) {
                const char *t = field_text(f, sub);
                int len = slen(t);
                int off = (i == f_focus && sub == f_sub && f_pos >= SUB_WIDTH) ?
                              f_pos - SUB_WIDTH + 1 :
                              0;

                term_fill(ex[sub], ry, SUB_WIDTH, 1, 0xFA, TA_DIM);

                for (int k = 0; k < SUB_WIDTH && off + k < len; k++) {
                    term_put(ex[sub] + k, ry, u8(t[off + k]),
                             i == f_focus && sub == f_sub && f_first != 0 ?
                                 TA_REVERSE :
                                 0);
                }

                if (i == f_focus && sub == f_sub) {
                    int pp = f_pos - off;

                    term_cursor(ex[sub] + (pp < SUB_WIDTH ? pp : SUB_WIDTH - 1),
                                ry, 1);
                }
            }

            if (f->def != NULL) {
                char dtext[160];

                snprintf(dtext, sizeof dtext, "default: %.100s",
                         f->defshow != NULL ? f->defshow : f->def);

                int dl = slen(dtext);
                int dx = ex[1] + SUB_WIDTH + 2;

                if (dx + dl <= vx + vw) {
                    term_puts(dx, ry, dtext, TA_DIM);
                }
            }

            continue;
        }

        term_fill(vx, ry, vw, 1, f->na != 0 ? ' ' : 0xFA, TA_DIM);

        /* A name is UTF-8: its cells and the cell of the place are used. */
        char cells[PATH_MAX];
        int cpos = f_pos;

        if (is_name_field(f) != 0) {
            field_cells(f->val, f_pos, cells, sizeof cells, &cpos);
        } else {
            str_copy(cells, sizeof cells, f->val);
        }

        int len = slen(cells);
        int off = (i == f_focus && cpos >= vw) ? cpos - vw + 1 : 0;

        for (int k = 0; k < vw && off + k < len; k++) {
            term_put(vx + k, ry, u8(cells[off + k]),
                     f->na != 0 ?
                         TA_DIM :
                         (i == f_focus && f_first != 0 ? TA_REVERSE : 0));
        }

        if (f->na != 0) {
            term_puts(vx + vw - 5, ry, "(n/a)", TA_DIM);
        }

        if (f->def != NULL && f->na == 0) {
            /* The default, at the right, when the value leaves room. */
            char dtext[160];

            snprintf(dtext, sizeof dtext, "default: %.100s",
                     f->defshow != NULL ? f->defshow : f->def);

            int dl = slen(dtext);

            if (len + dl + 2 <= vw) {
                term_puts(vx + vw - dl, ry, dtext, TA_DIM);
            }
        }

        if (i == f_focus) {
            int p = cpos - off;

            term_cursor(vx + (p < vw ? p : vw - 1), ry, 1);
        }
    }

    paint_footer();
}


/* move the focus to edit field sub of field i and reset the editing state */
static void
focus_sub(Field *fields, int i, int sub)
{
    f_focus = i;
    f_sub = sub;
    f_pos = slen(field_text(&fields[i], sub));
    f_first = fields[i].na == 0 && field_text(&fields[i], sub)[0] != '\0';
}


static void
focus_field(Field *fields, int i)
{
    focus_sub(fields, i, 0);
}


/* The field after (dir 1) or before (dir -1) field i that takes the focus:
   a line of text does not. */
static int
step_field(const Field *fields, int n, int i, int dir)
{
    for (int k = 0; k < n; k++) {
        i = (i + n + dir) % n;

        if (fields[i].note == 0 && on_page(&fields[i]) != 0) {
            return i;
        }
    }

    return i;
}


/* The index of the entry of a choice field that val is, or 0. */
static int
choice_index(const Field *f)
{
    for (int i = 0; i < f->nchoices; i++) {
        if (strcmp(f->val, f->choices[i]) == 0) {
            return i;
        }
    }

    return 0;
}


/* The list of the entries of a choice field; the entry that was chosen. */
static void
choice_list(Field *f)
{
    static char text[PJ_MAXQ + 4][PJ_QNAME + 40];
    const char *items[PJ_MAXQ + 4];
    int c;

    for (int i = 0; i < f->nchoices && i < PJ_MAXQ + 4; i++) {
        snprintf(text[i], sizeof text[i], "%.100s%s%.30s", f->choices[i],
                 f->marks != NULL && f->marks[i] != NULL ? "  " : "",
                 f->marks != NULL && f->marks[i] != NULL ? f->marks[i] : "");
        items[i] = text[i];
    }

    c = popup_menu(f->label, items, f->nchoices, choice_index(f));

    if (c >= 0) {
        str_copy(f->val, sizeof f->val, f->choices[c]);
    }
}


/*
 * A key in a choice field, as in the panels of the Editor: Right is the next
 * entry and Left the one before, and nothing happens at the last and the
 * first entry; Space is the next entry and goes round from the last to the
 * first; F2 and the plus show the list of the entries.  Returns 1 when the
 * key was one of them.
 */
static int
choice_key(Field *f, int k)
{
    int b = K_BASE(k);
    int i = choice_index(f);
    int n = f->nchoices;

    if (n == 0) {
        return 0;
    }

    if (b == K_LEFT) {
        if (i > 0) {
            i--;
        }
    } else if (b == K_RIGHT) {
        if (i + 1 < n) {
            i++;
        }
    } else if (k == ' ') {
        i = (i + 1) % n;
    } else if (k == '+' || k == ('+' | KM_ALT) || b == K_F(2)) {
        choice_list(f);

        return 1;
    } else {
        return 0;
    }

    str_copy(f->val, sizeof f->val, f->choices[i]);

    return 1;
}


/* The field that the next form starts on (0, the first): set by the caller. */
static int form_start;


/* Is the value of field idx right?  Else the reason is in f_error. */
static int
leave_ok(FieldCheck chk, const Field *fields, int idx)
{
    char why[256];

    if (chk == NULL || fields[idx].note != 0 || fields[idx].na != 0 ||
        chk(&fields[idx], idx, why, sizeof why) == 0) {
        return 1;
    }

    str_copy(f_error, sizeof f_error, why);

    return 0;
}


/* 1 when a list shown by a hook was left by Ins: the panel is finished. */
static int form_finish;


/*
 * Are all the fields right?  The first wrong one takes the focus and the
 * reason is in f_error; the panel stays open and nothing typed is lost.
 */
static int
accept_ok(FieldCheck chk, Field *fields, int n)
{
    for (int i = 0; i < n; i++) {
        if (leave_ok(chk, fields, i) == 0) {
            if (f_page != fields[i].page) {
                f_page = fields[i].page;
                f_top = 0;
            }

            focus_field(fields, i);

            return 0;
        }
    }

    return 1;
}


static int
form(const char *title, Field *fields, int n, const char *help_title,
     FieldHook hook)
{
    void (*prev)(void) = screen;
    FieldCheck chk = f_check;
    int del = f_del_default;

    int sv_pages = f_pages;
    int sv_page = f_page;
    int sv_custom = f_custom;
    const char *const *sv_page_titles = f_page_titles;

    f_title = title;
    f_bottom = f_bottom_text != NULL ? f_bottom_text :
                                       "Press Ins to Use Displayed Settings";
    f_pages = f_pages_set;
    f_custom = f_custom_set;
    f_custom_set = -1;
    f_page = 0;
    f_page_titles = f_page_titles_set;
    f_pages_set = 1;
    f_page_titles_set = NULL;
    f_check = NULL;
    f_del_default = 0;
    f_bottom_text = NULL;
    f_error[0] = '\0';
    f_fields = fields;
    f_n = n;
    f_top = 0;

    /* A line of text, such as a heading, does not take the focus. */
    focus_field(fields, fields[form_start].note != 0 ?
                            step_field(fields, n, form_start, 1) :
                            form_start);
    form_start = 0;
    screen = paint_form;

    for (;;) {
        /* The hook is told that nothing was pressed, so that it can update
           a line of text that depends on what is typed. */
        int on_custom = f_custom >= 0 && f_page == f_custom;

        if (hook != NULL && on_custom == 0) {
            hook(fields, f_focus, 0);
        }

        paint_form();
        term_present();
        int k = term_getkey(-1);
        int b = K_BASE(k);
        Field *f = &fields[f_focus];

        status_msg[0] = 0;
        f_error[0] = '\0';

        if (hook != NULL && on_custom == 0 && hook(fields, f_focus, k) != 0) {
            f_pos = slen(field_text(f, f_sub));
            f_first = 0;

            /* A list that the hook showed was left by Ins: the panel is
               finished as if Ins had been pressed in it. */
            if (form_finish != 0) {
                form_finish = 0;

                if (accept_ok(chk, fields, n) != 0) {
                    screen = prev;
                    footer_keys = "";
                    f_pages = sv_pages;
                    f_page = sv_page;
                    f_custom = sv_custom;
                    f_page_titles = sv_page_titles;
                    return 1;
                }
            }

            continue;
        }

        if (b == K_ESC) {
            screen = prev;
            footer_keys = "";
            f_pages = sv_pages;
            f_page = sv_page;
            f_custom = sv_custom;
            f_page_titles = sv_page_titles;
            return 0;
        }

        if (b == K_INS) {
            if (accept_ok(chk, fields, n) == 0) {
                continue;
            }

            screen = prev;
            footer_keys = "";
            f_pages = sv_pages;
            f_page = sv_page;
            f_custom = sv_custom;
            f_page_titles = sv_page_titles;
            return 1;
        }

        /*
         * PgUp shows the next page and PgDn the one before, as in the
         * panels of the Editor; the last page is followed by the first.  A
         * value that is wrong keeps the focus and the page.
         */
        if ((b == K_PGUP || b == K_PGDN) && f_pages > 1) {
            if (leave_ok(chk, fields, f_focus) != 0) {
                f_page = (f_page + (b == K_PGUP ? 1 : f_pages - 1)) % f_pages;
                f_top = 0;
                focus_field(fields,
                            step_field(fields, n, n - 1, 1));
            }

            continue;
        }

        if (on_custom != 0) {
            if (b == K_F(1)) {
                help_context("Setup:Screen Colors");
            } else {
                (void) colors_page_key(k);
            }

            continue;
        }

        if (b == K_F(1)) {
            char place[200];

            snprintf(place, sizeof place, "%s:%s", help_title,
                     fields[f_focus].label);
            help_context(place);
            continue;
        }

        if (b == K_BTAB) {
            if (f->sub1 != NULL && f_sub == 1) {
                focus_sub(fields, f_focus, 0);
            } else if (leave_ok(chk, fields, f_focus) != 0) {
                int pv = step_field(fields, n, f_focus, -1);

                focus_sub(fields, pv, fields[pv].sub1 != NULL ? 1 : 0);
            }

            continue;
        }

        if (b == K_UP) {
            if (leave_ok(chk, fields, f_focus) != 0) {
                focus_field(fields, step_field(fields, n, f_focus, -1));
            }

            continue;
        }

        if (b == 9 && f->sub1 != NULL && f_sub == 0) {
            focus_sub(fields, f_focus, 1);
            continue;
        }

        if (b == K_DOWN || b == 9) {
            if (leave_ok(chk, fields, f_focus) != 0) {
                focus_field(fields, step_field(fields, n, f_focus, 1));
            }

            continue;
        }

        if (b == 13) {
            if (f->sub1 != NULL && f_sub == 0) {
                focus_sub(fields, f_focus, 1);
                continue;
            }

            if (f_focus == step_field(fields, n, 0, -1)) {
                if (accept_ok(chk, fields, n) == 0) {
                    continue;
                }

                screen = prev;
                footer_keys = "";
                f_pages = sv_pages;
                f_page = sv_page;
                f_custom = sv_custom;
                f_page_titles = sv_page_titles;
                return 1;
            }

            if (leave_ok(chk, fields, f_focus) != 0) {
                focus_field(fields, step_field(fields, n, f_focus, 1));
            }

            continue;
        }

        int editkey = (k >= 32 && k <= 255 && (k & ~0xFF) == 0) ||
                      b == K_BKSP || b == K_DEL;

        if (f->na != 0) {
            if (editkey != 0) {
                say("Not implemented in this version: %s", f->label);

                if (f->na_help != NULL) {
                    paint_form();
                    term_present();
                    not_implemented(f->label, f->na_help);
                }
            }

            continue;
        }

        if (del != 0 && b == K_DEL && f->def != NULL) {
            /* Del puts the default back. */
            str_copy(f->val, sizeof f->val, f->def);
            f->val2[0] = '\0';
            f_pos = slen(field_text(f, f_sub));
            f_first = 0;
            continue;
        }

        if (f->choices != NULL) {
            if (choice_key(f, k) != 0) {
                f_pos = slen(f->val);
                f_first = 0;
            }

            continue;
        }

        char *val = field_text(f, f_sub);
        int cap = field_cap(f, f_sub);
        int len = slen(val);
        int name = is_name_field(f);

        if (b == K_LEFT) {
            if (f_pos > 0) {
                f_pos = name != 0 ? name_prev(val, f_pos) : f_pos - 1;
            } else if (f->sub1 != NULL && f_sub == 1) {
                focus_sub(fields, f_focus, 0);
            }

            f_first = 0;
        } else if (b == K_RIGHT) {
            if (f_pos < len) {
                f_pos = name != 0 ? name_next(val, f_pos) : f_pos + 1;
            } else if (f->sub1 != NULL && f_sub == 0) {
                focus_sub(fields, f_focus, 1);
            }

            f_first = 0;
        } else if (b == K_HOME) {
            f_pos = 0;
            f_first = 0;
        } else if (b == K_END) {
            f_pos = len;
            f_first = 0;
        } else if (b == K_BKSP) {
            if (f_first != 0) {
                val[0] = 0;
                f_pos = 0;
            } else if (f_pos > 0) {
                int from = name != 0 ? name_prev(val, f_pos) : f_pos - 1;

                memmove(val + from, val + f_pos, usz(len - f_pos + 1));
                f_pos = from;
            }

            f_first = 0;
        } else if (b == K_DEL) {
            if (f_first != 0) {
                val[0] = 0;
                f_pos = 0;
            } else if (f_pos < len) {
                int to = name != 0 ? name_next(val, f_pos) : f_pos + 1;

                memmove(val + f_pos, val + to, usz(len - to + 1));
            }

            f_first = 0;
        } else if (k >= 32 && k <= 255 && (k & ~0xFF) == 0) {
            if (f_first != 0) {
                val[0] = 0;
                f_pos = len = 0;
                f_first = 0;
            }

            /* A key of a name is put in as the UTF-8 of the character. */
            char put[4];
            int np = name != 0 ? key_utf8(k, put) : 1;

            put[0] = name != 0 ? put[0] : chr(k);

            if (len + np < cap) {
                memmove(val + f_pos + np, val + f_pos, usz(len - f_pos + 1));
                memcpy(val + f_pos, put, usz(np));
                f_pos += np;
            }
        }
    }
}


static void
field_init(Field *f, const char *label, const char *val, int na,
           const char *na_help)
{
    memset(f, 0, sizeof *f);
    f->label = label;
    f->cap = PATH_MAX;
    f->na = na;
    f->na_help = na_help;
    str_copy(f->val, sizeof f->val, val != NULL ? val : "");
}


/* ============================================================ file choosing */
/*
 * Keypad + on a field that holds the name of a file or of a directory shows a
 * list of directories and files to walk through (popup_browse).  Which kind
 * of field it is follows from its label.  A file is looked for in the
 * directory that the field "Working Directory:" of the panel holds, and
 * choosing a file in another directory changes that field.
 */
typedef struct {
    const char *label;
    int dirs_only; /* 1 for a directory */
    const char *suffixes; /* the files that are shown, "" for all */
} FileKind;

static const FileKind file_kinds[] = {
    {"Document Name:", 0, ".doc"},
    {"Initial Macro Set:", 0, ".mac"},
    {"Initial Library:", 0, ".lib"},
    {"Initial Working Directory:", 1, ""},
    {"Working Directory:", 1, ""},
    {"Editor program:", 0, ""},
    {"Help File:", 0, ".hlp"},
};


/* ============================================================== variables */
/*
 * The variables that the If markers of a document test.  They are given on
 * the panel Variables, which the field "Variables:" of the panel Edit /
 * Preview / Print Document opens, one name and one value in two fields so
 * that each can be checked.  They are kept for the whole run and passed to
 * the programs that str starts, with the special variables of str.
 */
static void
variables_summary(char *out, size_t cap)
{
    const VarList *l = vars_process();
    int n = 0;

    for (int i = 0; i < l->n; i++) {
        if (l->v[i].special == 0) {
            n++;
        }
    }

    if (n == 0) {
        str_copy(out, cap, "none; Keypad + or F2 to give some");
    } else {
        snprintf(out, cap, "%d given; Keypad + or F2 to change", n);
    }
}


/* Show the panel of the variables; Ins keeps what it holds if it is right. */
static void
edit_variables(Field *summary)
{
    static char labels[2 * VARS_MAX][24];
    Field g[2 * VARS_MAX];
    const VarList *cur = vars_process();
    const char *sv_title = f_title;
    const char *sv_bottom = f_bottom;
    Field *sv_fields = f_fields;
    int sv_n = f_n;
    int sv_focus = f_focus;
    int sv_pos = f_pos;
    int sv_first = f_first;
    int shown = 0;

    for (int i = 0; i < VARS_MAX; i++) {
        const char *name = "";
        const char *value = "";

        snprintf(labels[2 * i], sizeof labels[0], "Name %d:", i + 1);
        snprintf(labels[2 * i + 1], sizeof labels[0], "Value %d:", i + 1);

        for (int k = 0, seen = 0; k < cur->n; k++) {
            if (cur->v[k].special != 0) {
                continue;
            }

            if (seen == i) {
                name = cur->v[k].name;
                value = cur->v[k].value;
                break;
            }

            seen++;
        }

        field_init(&g[2 * i], labels[2 * i], name, 0, NULL);
        field_init(&g[2 * i + 1], labels[2 * i + 1], value, 0, NULL);
        g[2 * i].cap = VAR_LEN_MAX + 1;
        g[2 * i + 1].cap = VAR_LEN_MAX + 1;
    }

    for (;;) {
        VarList tmp;
        char err[240];
        int bad = 0;

        if (form("Variables", g, 2 * VARS_MAX, "Variables", NULL) == 0) {
            break;
        }

        memset(&tmp, 0, sizeof tmp);

        for (int i = 0; i < cur->n; i++) {
            if (cur->v[i].special != 0) {
                tmp.v[tmp.n++] = cur->v[i];
            }
        }

        for (int i = 0; i < VARS_MAX && bad == 0; i++) {
            const char *name = g[2 * i].val;
            const char *value = g[2 * i + 1].val;

            if (name[0] == '\0' && value[0] == '\0') {
                continue;
            }

            if (name[0] == '\0') {
                snprintf(err, sizeof err, "Line %d has a value but no name.",
                         i + 1);
                bad = 1;
            } else if (vars_set(&tmp, name, value, 0, err, sizeof err) != 0) {
                bad = 1;
            }
        }

        if (bad != 0) {
            log_warn("str: the variables were refused and the panel stays "
                     "open: %s", err);
            popup_msg("Variables", err);
            continue;
        }

        *vars_process() = tmp;
        shown = 1;
        break;
    }

    f_title = sv_title;
    f_bottom = sv_bottom;
    f_fields = sv_fields;
    f_n = sv_n;
    f_focus = sv_focus;
    f_pos = sv_pos;
    f_first = sv_first;

    if (shown != 0) {
        variables_summary(summary->val, sizeof summary->val);
    }
}


/* What Setup holds of the printers: the number of queues, and the default. */
static int setup_queues;
#define SETUP_QUEUES_PENDING (-2)
static char setup_default_queue[PJ_QNAME];

/* The field of Setup with the label, or -1. */
static int
find_label(const Field *f, int n, const char *label)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(f[i].label, label) == 0) {
            return i;
        }
    }

    return -1;
}


/* Is the text of the Backups Kept field a whole number? */
static int
backups_ok(const char *s)
{
    if (s[0] == '\0') {
        return 0;
    }

    for (int i = 0; s[i] != '\0'; i++) {
        if (isdigit(u8(s[i])) == 0) {
            return 0;
        }
    }

    return strlen(s) <= 9;
}


/*
 * The last line of Setup: a few words about the field that has the focus.
 * For the Printer Queue the default queue of the system, for the PDF
 * Directory where the PDF goes, and for the PDF Viewer the program that the
 * value names, found as the Editor will find it, or why it is not used.
 */
static void
setup_note(Field *f, int n, int idx)
{
    int at = find_label(f, n, "Printer Queue:");
    int nt = -1;
    char found[PATH_MAX];
    char dir[PATH_MAX];
    char why[256];
    const char *typed;

    /* The line of text at the foot of the page that is shown. */
    for (int i = 0; i < n; i++) {
        if (f[i].page == f_page) {
            nt = i;
        }
    }

    if (nt < 0 || f[nt].note == 0) {
        return;
    }

    f[nt].val[0] = '\0';
    f[nt].note = 1;

    if (f_error[0] != '\0') {
        /* A value that was refused: the reason takes the place of the help. */
        str_copy(f[nt].val, sizeof f[nt].val, f_error);
        f[nt].note = 2;

        return;
    }

    if (idx == at && at >= 0) {
        if (setup_queues == SETUP_QUEUES_PENDING) {
            str_copy(f[nt].val, sizeof f[nt].val,
                     "The printer list is not available yet");
        } else if (setup_queues == 0) {
            str_copy(f[nt].val, sizeof f[nt].val, "No printers found");
        } else if (setup_default_queue[0] != '\0') {
            snprintf(f[nt].val, sizeof f[nt].val,
                     "The default printer of the system is %.100s",
                     setup_default_queue);
        } else {
            str_copy(f[nt].val, sizeof f[nt].val,
                     "The system has no default printer");
        }

        return;
    }

    if (idx == find_label(f, n, "PDF Directory:")) {
        typed = f[idx].val;

        if (typed[0] == '\0') {
            str_copy(f[nt].val, sizeof f[nt].val,
                     "The PDF is put in the directory of the document");
        } else if (pj_pdf_dir_check(typed, dir, sizeof dir, why,
                                    sizeof why) == 0) {
            snprintf(f[nt].val, sizeof f[nt].val, "PDF files go to %.1000s",
                     dir);
        } else {
            snprintf(f[nt].val, sizeof f[nt].val, "%.200s (the document's "
                                                  "directory is used)",
                     why);
        }

        return;
    }

    if (idx == find_label(f, n, "Backups Kept (0 to 99):")) {
        int kept = backups_ok(f[idx].val) != 0 ?
                       config_clamp_backups(atoi(f[idx].val)) :
                       0;

        if (kept == 0) {
            str_copy(f[nt].val, sizeof f[nt].val, "No backup is made");
        } else if (kept == 1) {
            str_copy(f[nt].val, sizeof f[nt].val,
                     "One backup, in the folder .backup of the document");
        } else {
            snprintf(f[nt].val, sizeof f[nt].val,
                     "%d backups, in the folder .backup of the document",
                     kept);
        }

        return;
    }

    if (idx != find_label(f, n, "PDF Viewer:")) {
        if (f[idx].help != NULL) {
            str_copy(f[nt].val, sizeof f[nt].val, f[idx].help);
        }

        return;
    }

    typed = f[idx].val;

    if (typed[0] == '\0') {
        if (cfg.pdf_viewer_old[0] != '\0') {
            /* A setting of an older file that was only a name. */
            snprintf(f[nt].val, sizeof f[nt].val,
                     "The old setting %.100s is not a full path and is not "
                     "used", cfg.pdf_viewer_old);
        } else if (pj_viewer_default(found, sizeof found) != 0) {
            snprintf(f[nt].val, sizeof f[nt].val, "Found: %.1000s", found);
        } else {
            str_copy(f[nt].val, sizeof f[nt].val,
                     "No PDF viewer found in /usr/bin, /usr/local/bin or "
                     "/bin: give one with its full path");
        }

        return;
    }

    switch (pj_viewer_check(typed, found, sizeof found, why, sizeof why)) {
    case PJ_VIEWER_OK:
        snprintf(f[nt].val, sizeof f[nt].val, "Found: %.1000s", found);
        break;

    case PJ_VIEWER_BAD_FORM:
        str_copy(f[nt].val, sizeof f[nt].val, "The full path of the program, "
                                              "such as /usr/bin/evince");
        break;

    case PJ_VIEWER_PROCESSOR:
        str_copy(f[nt].val, sizeof f[nt].val, "Not a word processor: use a "
                                              "program that shows PDF files");
        break;

    case PJ_VIEWER_UNSAFE:
        str_copy(f[nt].val, sizeof f[nt].val, "Not safe: other users can "
                                              "change this program");
        break;

    default:
        str_copy(f[nt].val, sizeof f[nt].val, "Not an executable file");
        break;
    }
}


static int
hook_files(Field *f, int idx, int key)
{
    const FileKind *k = NULL;

    setup_note(f, f_n, idx);

    Field *wd = NULL;
    char dir[PATH_MAX];
    char name[PATH_MAX];
    char start[PATH_MAX];

    /* The field of the variables is not typed in; its panel is opened. */
    if (strcmp(f[idx].label, "Variables:") == 0) {
        if (key == '+' || key == ('+' | KM_ALT) || K_BASE(key) == K_F(2)) {
            edit_variables(&f[idx]);

            return 1;
        }

        return (key >= 32 && key <= 255 && (key & ~0xFF) == 0) ||
               K_BASE(key) == K_BKSP || K_BASE(key) == K_DEL;
    }

    /* The keypad plus (the X11 version sends it as Alt and +), Alt-+ of a
       terminal, the plus of the keyboard and F2, as the panels of stred. */
    if (key != '+' && key != ('+' | KM_ALT) && K_BASE(key) != K_F(2)) {
        return 0;
    }

    for (size_t i = 0; i < sizeof file_kinds / sizeof file_kinds[0]; i++) {
        if (strcmp(f[idx].label, file_kinds[i].label) == 0) {
            k = &file_kinds[i];
        }
    }

    if (k == NULL) {
        return 0;
    }

    for (int i = 0; i < f_n; i++) {
        if (i != idx && strcmp(f[i].label, "Working Directory:") == 0) {
            wd = &f[i];
        }
    }

    str_copy(start, sizeof start, k->dirs_only != 0 || wd == NULL ?
                                      f[idx].val :
                                      wd->val);

    if (k->dirs_only == 0 && wd == NULL && strchr(f[idx].val, '/') != NULL) {
        char *sl;

        str_copy(start, sizeof start, f[idx].val);
        sl = strrchr(start, '/');
        *sl = '\0';
    }

    if (is_dir(start) == 0) {
        start[0] = '\0';
    }

    if (popup_browse(start, k->dirs_only, k->suffixes, dir, sizeof dir, name,
                     sizeof name) == 0) {
        return 1;
    }

    if (k->dirs_only != 0) {
        str_copy(f[idx].val, sizeof f[idx].val, dir);
    } else if (wd != NULL) {
        str_copy(wd->val, sizeof wd->val, dir);
        str_copy(f[idx].val, sizeof f[idx].val, name);
    } else {
        snprintf(f[idx].val, sizeof f[idx].val, "%.2000s/%.2000s", dir, name);
    }

    return 1;
}


/* turn a panel's name + directory into an absolute path; adds .DOC */
static int
resolve_doc(const char *name, const char *dir, char *out, size_t cap)
{
    if (name[0] == '\0') {
        say("Enter a document name");
        return -1;
    }

    char d[PATH_MAX];

    if (dir[0] == '\0') {
        snprintf(d, sizeof d, ".");
    } else {
        str_copy(d, sizeof d, dir);
    }

    if (is_dir(d) == 0) {
        say("Cannot find directory %s", d);
        return -1;
    }

    char full[PATH_MAX * 2 + 8];

    if (name[0] == '/') {
        str_copy(full, sizeof full, name);
    } else {
        snprintf(full, sizeof full, "%s/%s", d, name);
    }

    doc_name_complete(full, sizeof full);

    char rp[PATH_MAX];

    if (exists(full) != 0) {
        if (realpath(full, rp) == NULL) {
            str_copy(rp, sizeof rp, full);
        }
    } else {
        char dd[PATH_MAX];

        str_copy(dd, sizeof dd, full);
        char *sl = strrchr(dd, '/');

        if (sl != NULL) {
            *sl = 0;
        }

        char rd[PATH_MAX];

        if (realpath(sl != NULL ? dd : ".", rd) == NULL) {
            say("Cannot find directory %s", dd);
            return -1;
        }

        path_join(rp, sizeof rp, rd, sl != NULL ? sl + 1 : full);
    }

    snprintf(out, cap, "%s", rp);

    return 0;
}

/* ================================================================ actions */
static char last_name[256];
static int main_sel;


static void
paint_under(void);


/* Keypad + or F2 on the field Template of the New File panel. */
static int
hook_newfile(Field *f, int idx, int key)
{
    if (strcmp(f[idx].label, "Template:") == 0 &&
        (key == '+' || key == ('+' | KM_ALT) || K_BASE(key) == K_F(2))) {
        template_pick(f[idx].val, sizeof f[idx].val, 0);

        if (widgets_list_ins != 0) {
            widgets_list_ins = 0;
            form_finish = 1;
        }

        return 1;
    }

    return 0;
}


static void
do_edit(void)
{
    Field f[3];

    field_init(&f[0], "Document Name:", last_name, 0, NULL);
    field_init(&f[1], "Working Directory:", workdir, 0, NULL);
    field_init(&f[2], "Variables:", "", 0, NULL);
    variables_summary(f[2].val, sizeof f[2].val);

    for (;;) {
        /* Working Directory comes first, then Document Name */
        Field g[3] = {f[1], f[0], f[2]};

        /* The panel starts on Document Name, the field that is filled in. */
        form_start = 1;

        int ok = form(EDIT_ENTRY, g, 3, "Selecting a File", hook_files);

        f[0] = g[1];
        f[1] = g[0];
        f[2] = g[2];

        if (ok == 0) {
            return;
        }

        char path[PATH_MAX];

        if (resolve_doc(f[0].val, f[1].val, path, sizeof path) != 0) {
            continue;
        }

        str_copy(workdir, sizeof workdir, f[1].val);
        str_copy(last_name, sizeof last_name, f[0].val);
        int flat = 0;
        char new_description[160] = "";
        char new_author[160] = "";
        char new_template[PATH_MAX] = "";

        if (exists(path) == 0) {
            const char *it[] = {"Structured", "Unstructured"};
            Template *tl = calloc(TEMPLATE_MAX, sizeof *tl);
            int ntl = tl != NULL ? template_list(tl, TEMPLATE_MAX) : 0;
            int c = -1;
            char chosen[PATH_MAX] = "";

            /* With templates: a list of them with their descriptions, the
               built-in settings first; Esc asks for the type as before. */
            int by_ins = 0;

            if (ntl > 0) {
                c = template_ask(tl, ntl, 1);
                by_ins = c >= 0 && widgets_list_ins != 0;

                if (c >= 2) {
                    str_copy(chosen, sizeof chosen, tl[c - 2].path);
                }
            }

            free(tl);

            if (c < 0) {
                c = popup_menu("New File: document type", it, 2, 0);
                by_ins = c >= 0 && widgets_list_ins != 0;
            }

            if (c < 0) {
                continue;
            }

            if (c < 2) {
                str_copy(chosen, sizeof chosen, it[c]);
            }

            /* The New File panel has the field Template, as the Editor's
               New has: the entry chosen above, or another one (Keypad +
               or F2 list the templates and a way to any other file), and
               asks for a description and an author. */
            Field g2[3];
            char tpath[PATH_MAX];
            int accepted = 0;

            field_init(&g2[0], "Template:", chosen, 0, NULL);
            field_init(&g2[1], "Description:", "", 0, NULL);
            field_init(&g2[2], "Author:", "", 0, NULL);

            for (;;) {
                /* A list left by Ins finishes the panel at once: the type
                   is made with the description and the author left blank. */
                int shown = by_ins == 0;

                by_ins = 0;

                if (shown != 0 && form("New File", g2, 3,
                                       "Document Description",
                                       hook_newfile) == 0) {
                    break;
                }

                if (strcasecmp(g2[0].val, "Structured") == 0) {
                    flat = 0;
                    new_template[0] = '\0';
                } else if (strcasecmp(g2[0].val, "Unstructured") == 0) {
                    flat = 1;
                    new_template[0] = '\0';
                } else if (template_find(g2[0].val, workdir, tpath,
                                         sizeof tpath) != 0) {
                    flat = 0;
                    str_copy(new_template, sizeof new_template, tpath);
                } else {
                    popup_msg("Template", "Cannot find the template file");
                    continue;
                }

                str_copy(new_description, sizeof new_description, g2[1].val);
                str_copy(new_author, sizeof new_author, g2[2].val);
                accepted = 1;
                break;
            }

            if (accepted == 0) {
                continue;
            }
        }

        char opt_help[] = "-H";
        char opt_flat[] = "-u";
        char opt_templ[] = "--template";
        char opt_desc[] = "--description";
        char opt_auth[] = "--author";
        char opt_auto[] = "--autosave";
        char opt_autosecs[] = "--autosave-seconds";
        char auto_yes[] = "yes";
        char auto_no[] = "no";
        char auto_secs[16];
        char *argv[18 + VARS_ARGC];
        int n = 0;

        snprintf(auto_secs, sizeof auto_secs, "%d", autosave_secs);
        argv[n++] = msed_path;
        argv[n++] = opt_auto;
        argv[n++] = autosave_yes != 0 ? auto_yes : auto_no;
        argv[n++] = opt_autosecs;
        argv[n++] = auto_secs;

        if (helppath[0] != '\0') {
            argv[n++] = opt_help;
            argv[n++] = helppath;
        }

        if (flat != 0) {
            argv[n++] = opt_flat;
        }

        if (new_template[0] != '\0') {
            argv[n++] = opt_templ;
            argv[n++] = new_template;
        }

        if (new_description[0] != '\0') {
            argv[n++] = opt_desc;
            argv[n++] = new_description;
        }

        if (new_author[0] != '\0') {
            argv[n++] = opt_auth;
            argv[n++] = new_author;
        }

        n = vars_args(vars_process(), argv, n, 17 + VARS_ARGC);
        argv[n++] = path;
        argv[n] = NULL;
        int rc = run_child(NULL, argv, 1);

        if (rc == 127) {
            char m[PATH_MAX + 64];

            snprintf(m, sizeof m, "Cannot run the editor: %s", msed_path);
            popup_msg(EDIT_ENTRY, m);
        } else if (rc != 0) {
            char m[80];

            snprintf(m, sizeof m,
                     "The editor ended "
                     "with status %d",
                     rc);
            popup_msg(EDIT_ENTRY, m);
        }

        return;
    }
}


/* A language for the spell checker: letters, digits, _ - and @. */
static int
language_ok(const char *s)
{
    size_t n = strlen(s);

    if (n == 0 || n >= sizeof spell_lang) {
        return 0;
    }

    for (size_t i = 0; i < n; i++) {
        if (isalnum(u8(s[i])) == 0 && s[i] != '_' && s[i] != '-' &&
            s[i] != '@') {
            return 0;
        }
    }

    return 1;
}


/* The rows of a window: empty, or a whole number of at most 9 digits. */
static int
rows_ok(const char *s)
{
    if (s[0] == '\0') {
        return 1;
    }

    for (size_t i = 0; s[i] != '\0'; i++) {
        if (isdigit(u8(s[i])) == 0) {
            return 0;
        }
    }

    return strlen(s) <= 9;
}


/* The rows of a field that rows_ok accepted, made right; 0 for empty. */
static int
rows_value(const char *s)
{
    return s[0] == '\0' || atoi(s) == 0 ? 0 : config_clamp_rows(atoi(s), 0);
}


/* Is the text of the Undo Levels field a whole number? */
static int
undo_ok(const char *s)
{
    if (s[0] == '\0') {
        return 0;
    }

    for (int i = 0; s[i] != '\0'; i++) {
        if (isdigit(u8(s[i])) == 0) {
            return 0;
        }
    }

    return strlen(s) <= 9;
}


/*
 * The places of the fields of Setup.  The panel has two pages: the first,
 * the settings that most people change, with a heading for each group
 * (SU_H_*) and the line of the foot last; the second, the advanced settings
 * (SU_A_*), shown with PgUp and PgDn.
 */
enum {
    SU_H_DOCS,
    SU_WORKDIR,
    SU_BACKUPS,
    SU_AUTOSAVE,
    SU_INTERVAL,
    SU_H_PRINT,
    SU_PRINT_TO,
    SU_QUEUE,
    SU_PDF_DIR,
    SU_VIEWER,
    SU_H_WRITE,
    SU_PAPER,
    SU_LANGUAGE,
    SU_UNDO,
    SU_H_SCREEN,
    SU_ROWS,
    SU_NOTE,
    SU_A_MACRO_SET,
    SU_A_LIBRARY,
    SU_A_CHARSET,
    SU_A_EDITOR,
    SU_A_HELP,
    SU_A_TEMPLATES,
    SU_A_FILE,
    SU_A_NOTE,
    SU_COUNT
};

/* The first field of the second page. */
#define SU_PAGE2 SU_A_MACRO_SET

#define SU_NOT_NOW " (not available now)"
#define SU_DEFAULT_QUEUE "System default"
#define SU_NO_PRINTERS "No printers found"
#define SU_PENDING "Printer list not available yet"


/* The choices of Print To: Printer is kept when it is saved, even when the
   system has no queue now. */
static void
setup_print_to(Field *f, const PjQueues *q, const char *saved)
{
    static const char *pto[3];
    static char kept[40];
    int n = 0;

    if (q->n > 0) {
        pto[n++] = "Printer";
    } else if (strcmp(saved, "printer") == 0) {
        snprintf(kept, sizeof kept, "Printer%s", SU_NOT_NOW);
        pto[n++] = kept;
    }

    pto[n++] = PJ_FILE_ONLY;
    pto[n++] = PJ_PDF_VIEWER;
    f->choices = pto;
    f->nchoices = n;
    str_copy(f->val, sizeof f->val,
             strcmp(saved, "printer") == 0 ? pto[0] :
             strcmp(saved, "viewer") == 0  ? PJ_PDF_VIEWER :
                                             PJ_FILE_ONLY);
}


/* The choices of Printer Queue, from the queues of the system: the default
   first, each queue by name, and a saved queue that is not listed now. */
static void
setup_queue(Field *f, const PjQueues *q, const char *saved)
{
    static const char *list[PJ_MAXQ + 3];
    static const char *marks[PJ_MAXQ + 3];
    static char kept[PJ_QNAME + 24];
    static char dmark[PJ_QNAME + 4];
    int n = 0;
    int listed = 0;

    if (q->n == 0) {
        if (saved[0] != '\0') {
            snprintf(kept, sizeof kept, "%.100s%s", saved, SU_NOT_NOW);
            list[n++] = kept;
        } else {
            list[n++] = q->pending != 0 ? SU_PENDING : SU_NO_PRINTERS;
        }
    } else {
        snprintf(dmark, sizeof dmark, "(%s)",
                 setup_default_queue[0] != '\0' ? setup_default_queue :
                                                  "none set");
        marks[n] = dmark;
        list[n++] = SU_DEFAULT_QUEUE;

        for (int i = 0; i < q->n; i++) {
            marks[n] = strcmp(q->name[i], setup_default_queue) == 0 ?
                           "(system default)" :
                           q->off[i] != 0 ? "(not accepting jobs)" : NULL;
            list[n++] = q->name[i];
            listed = listed || strcmp(q->name[i], saved) == 0;
        }

        if (saved[0] != '\0' && listed == 0) {
            snprintf(kept, sizeof kept, "%.100s%s", saved, SU_NOT_NOW);
            marks[n] = NULL;
            list[n++] = kept;
        }
    }

    f->choices = list;
    f->marks = marks;
    f->nchoices = n;
    str_copy(f->val, sizeof f->val,
             saved[0] == '\0' ? list[0] :
             q->n == 0        ? kept :
             listed != 0      ? saved :
                                kept);
}


/* The queue that a choice of Printer Queue stands for ("" for the default
   and for no printers). */
static void
queue_of_choice(const char *val, char *out, size_t cap)
{
    size_t n = strlen(val);
    size_t m = strlen(SU_NOT_NOW);

    if (strcmp(val, SU_DEFAULT_QUEUE) == 0 ||
        strcmp(val, SU_PENDING) == 0 ||
        strcmp(val, SU_NO_PRINTERS) == 0) {
        out[0] = '\0';
    } else if (n > m && strcmp(val + n - m, SU_NOT_NOW) == 0) {
        str_copy(out, cap, val);
        out[n - m < cap ? n - m : cap - 1] = '\0';
    } else {
        str_copy(out, cap, val);
    }
}


/*
 * Is the value of a field of Setup right?  Returns 0 when it is, else 1 and
 * the reason in why.  The panel calls it when the focus leaves a field and
 * for every field when Ins is pressed.  A value that cannot be used is
 * refused; one that may be right later (a program not found, a directory
 * that is not made yet) is accepted and the line of the foot says so.
 */
static int
setup_check(const Field *f, int idx, char *why, size_t cap)
{
    const char *l = f->label;

    (void) idx;

    if (strcmp(l, "Autosave Interval (Seconds):") == 0) {
        if (atoi(f->val) < 1 || atoi(f->val) > 86400) {
            str_copy(why, cap, "The autosave interval is 1 to 86400 seconds");

            return 1;
        }
    } else if (strcmp(l, "Window Rows:") == 0) {
        if (rows_ok(f->val) == 0 || rows_ok(f->val2) == 0) {
            str_copy(why, cap, "The window rows are a whole number, or empty");

            return 1;
        }
    } else if (strcmp(l, "Undo Levels (0 to 500):") == 0) {
        if (undo_ok(f->val) == 0) {
            str_copy(why, cap, "Undo Levels is a whole number from 0 to 500");

            return 1;
        }
    } else if (strcmp(l, "Backups Kept (0 to 99):") == 0) {
        if (backups_ok(f->val) == 0) {
            str_copy(why, cap, "Backups Kept is a whole number from 0 to 99");

            return 1;
        }
    } else if (strcmp(l, "PDF Directory:") == 0) {
        /* A directory that cannot be used now is saved: it may be made
           later; but a relative path is not a directory of the user. */
        char dir[PATH_MAX];
        char reason[256];

        if (pj_pdf_dir_check(f->val, dir, sizeof dir, reason,
                             sizeof reason) == 1) {
            str_copy(why, cap, "The PDF Directory is a full path or starts "
                               "with ~/");

            return 1;
        }
    } else if (strcmp(l, "PDF Viewer:") == 0) {
        /* The value is a full path of a program that may be run. */
        char found[PATH_MAX];
        char reason[256];
        int r = f->val[0] == '\0' ?
                    PJ_VIEWER_OK :
                    pj_viewer_check(f->val, found, sizeof found, reason,
                                    sizeof reason);

        if (r == PJ_VIEWER_BAD_FORM) {
            str_copy(why, cap, "The PDF viewer needs its full path, such as "
                               "/usr/bin/evince");

            return 1;
        }

        if (r == PJ_VIEWER_PROCESSOR) {
            str_copy(why, cap, "A word processor is not a PDF viewer");

            return 1;
        }

        if (r == PJ_VIEWER_UNSAFE) {
            str_copy(why, cap, "The PDF viewer or its directory can be "
                               "changed by other users: it is not used");

            return 1;
        }

        if (r != PJ_VIEWER_OK) {
            str_copy(why, cap, "The PDF viewer is not an executable file");

            return 1;
        }
    } else if (strcmp(l, "Dictionary Language:") == 0) {
        if (language_ok(f->val) == 0) {
            str_copy(why, cap, "The Dictionary Language is a code such as "
                               "en_US or de_DE");

            return 1;
        }
    } else if (strcmp(l, "Initial Working Directory:") == 0) {
        if (f->val[0] != '\0' && is_dir(f->val) == 0) {
            str_copy(why, cap, "The initial working directory does not "
                               "exist");

            return 1;
        }
    }

    return 0;
}


/* A heading of a group of fields of a panel: a line that has no focus. */
static void
field_heading(Field *f, const char *text)
{
    field_init(f, "", text, 0, NULL);
    f->note = 3;
}


/* The words and the default of a field, for the panels that show them. */
static void
field_hint(Field *f, const char *help, const char *def, const char *defshow)
{
    f->help = help;
    f->def = def;
    f->defshow = defshow;
}


/* The titles of the two pages of Setup. */
static const char *const setup_titles[] = {"Setup (settings)",
                                           "Setup (screen colors)",
                                           "Setup (advanced settings)"};


static void
do_setup(void)
{
    Field f[SU_COUNT];
    PjQueues q;
    char secs[16];
    char xrows[16] = "";
    char trows[16] = "";
    char ulev[16];
    char bkept[16];
    char dflt_backups[16];
    char dflt_msed[PATH_MAX];
    char dflt_help[512];
    char templates[PATH_MAX + 40];
    char file_line[PATH_MAX + 40];
    static const char *yes_no[] = {"Yes", "No"};
    static const char *papers[] = {"Letter", "A4"};
    static const char *charset_choices[] = {"Automatic", "UTF-8", "VT100",
                                            "ASCII"};

    pj_queues(&q);
    setup_queues = q.pending != 0 ? SETUP_QUEUES_PENDING : q.n;

    /* When the list of queues is not there yet, the default queue is not
       either: asking again would wait a second time. */
    if (q.pending != 0 ||
        pj_default_queue(setup_default_queue, sizeof setup_default_queue) ==
        0) {
        setup_default_queue[0] = '\0';
    }

    snprintf(dflt_backups, sizeof dflt_backups, "%d", CONFIG_BACKUPS_DEFAULT);
    default_msed(dflt_msed, sizeof dflt_msed);
    dflt_help[0] = '\0';

    if (help_find_file(NULL, dflt_help, sizeof dflt_help) == NULL) {
        dflt_help[0] = '\0';
    }

    snprintf(secs, sizeof secs, "%d", autosave_secs);
    snprintf(ulev, sizeof ulev, "%d", undo_levels_val);
    snprintf(bkept, sizeof bkept, "%d", backups_val);

    if (x11_rows_val != 0) {
        snprintf(xrows, sizeof xrows, "%d", x11_rows_val);
    }

    if (term_rows_val != 0) {
        snprintf(trows, sizeof trows, "%d", term_rows_val);
    }

    /* The first level: the settings that most people change. */
    field_heading(&f[SU_H_DOCS], "Documents");
    field_init(&f[SU_WORKDIR], "Initial Working Directory:", workdir, 0, NULL);
    field_hint(&f[SU_WORKDIR],
               "Where the document panels start; empty is where you started.",
               "", "start directory");
    field_init(&f[SU_BACKUPS], "Backups Kept (0 to 99):", bkept, 0, NULL);
    field_hint(&f[SU_BACKUPS], "How many numbered backups a save keeps.",
               dflt_backups, NULL);
    field_init(&f[SU_AUTOSAVE], "Autosave:", autosave_yes != 0 ? "Yes" : "No",
               0, NULL);
    f[SU_AUTOSAVE].choices = yes_no;
    f[SU_AUTOSAVE].nchoices = 2;
    field_hint(&f[SU_AUTOSAVE],
               "Whether the Editor saves a changed document every so often.",
               "Yes", NULL);
    field_init(&f[SU_INTERVAL], "Autosave Interval (Seconds):", secs, 0, NULL);
    field_hint(&f[SU_INTERVAL],
               "The seconds between two saves of Autosave, 1 to 86400.", "120",
               NULL);

    field_heading(&f[SU_H_PRINT], "Printing");
    field_init(&f[SU_PRINT_TO], "Print To:", "", 0, NULL);
    setup_print_to(&f[SU_PRINT_TO], &q, print_to_val);
    field_hint(&f[SU_PRINT_TO],
               "Where a print goes: a PDF file only, a printer, or the viewer.",
               PJ_FILE_ONLY, NULL);
    field_init(&f[SU_QUEUE], "Printer Queue:", "", 0, NULL);
    setup_queue(&f[SU_QUEUE], &q, print_queue_val);
    field_hint(&f[SU_QUEUE], NULL, q.n > 0 ? SU_DEFAULT_QUEUE : NULL, NULL);
    field_init(&f[SU_PDF_DIR], "PDF Directory:", pdf_dir_val, 0, NULL);
    field_hint(&f[SU_PDF_DIR], NULL, "", "next to the document");
    field_init(&f[SU_VIEWER], "PDF Viewer:", pdf_viewer, 0, NULL);
    field_hint(&f[SU_VIEWER], NULL, "", "a viewer in /usr/bin");

    field_heading(&f[SU_H_WRITE], "Writing");
    field_init(&f[SU_PAPER], "Paper for New Documents:",
               strcmp(paper_val, "a4") == 0 ? "A4" : "Letter", 0, NULL);
    f[SU_PAPER].choices = papers;
    f[SU_PAPER].nchoices = 2;
    field_hint(&f[SU_PAPER],
               "The paper of a new document that has no template.",
               "Letter", NULL);
    field_init(&f[SU_LANGUAGE], "Dictionary Language:", spell_lang, 0, NULL);
    field_hint(&f[SU_LANGUAGE],
               "The code of the spelling dictionary, such as en_US or de_DE.",
               "en_US", NULL);
    field_init(&f[SU_UNDO], "Undo Levels (0 to 500):", ulev, 0, NULL);
    field_hint(&f[SU_UNDO],
               "How many operations Undo keeps; 0 turns Undo off.", "100",
               NULL);

    field_heading(&f[SU_H_SCREEN], "Screen");

    /* One row, two edit fields: the rows of the X11 window and the most
       rows that the terminal version uses. */
    field_init(&f[SU_ROWS], "Window Rows:", xrows, 0, NULL);
    f[SU_ROWS].sub1 = "X11:";
    f[SU_ROWS].sub2 = "Terminal:";
    f[SU_ROWS].cap = 10;
    str_copy(f[SU_ROWS].val2, sizeof f[SU_ROWS].val2, trows);
    field_hint(&f[SU_ROWS],
               "Window rows: X11 (20 to 200) and the most of the terminal.",
               "", "50 and all");
    field_init(&f[SU_NOTE], "", "", 0, NULL);
    f[SU_NOTE].note = 1;

    /* The second page: the advanced settings. */
    field_init(&f[SU_A_MACRO_SET], "Initial Macro Set:", macros_val, 0, NULL);
    field_hint(&f[SU_A_MACRO_SET],
               "A macro file that the Editor loads at start; empty loads none.",
               "", "none");
    field_init(&f[SU_A_LIBRARY], "Initial Library:", library_val, 0, NULL);
    field_hint(&f[SU_A_LIBRARY],
               "A library file that the Editor loads at start; empty loads "
               "none.",
               "", "none");
    field_init(&f[SU_A_CHARSET], "Terminal Characters:",
               strcmp(charset_val, "utf8") == 0  ? "UTF-8" :
               strcmp(charset_val, "vt100") == 0 ? "VT100" :
               strcmp(charset_val, "ascii") == 0 ? "ASCII" :
                                                   "Automatic",
               0, NULL);
    f[SU_A_CHARSET].choices = charset_choices;
    f[SU_A_CHARSET].nchoices = 4;
    field_hint(&f[SU_A_CHARSET],
               "How the terminal draws its screen: Automatic, UTF-8, VT100 or "
               "ASCII.",
               "Automatic", NULL);
    field_init(&f[SU_A_EDITOR], "Editor program:", msed_path, 0, NULL);
    field_hint(&f[SU_A_EDITOR],
               "The Editor that this program starts; the install sets it.",
               dflt_msed, "as installed");
    field_init(&f[SU_A_HELP], "Help File:", helppath, 0, NULL);
    field_hint(&f[SU_A_HELP], "The file of the help texts.",
               dflt_help[0] != '\0' ? dflt_help : NULL, "as installed");

    {
        char dir[PATH_MAX];
        char *sl;

        str_copy(dir, sizeof dir, conf_file);
        sl = strrchr(dir, '/');

        if (sl != NULL) {
            *sl = '\0';
        }

        snprintf(templates, sizeof templates, "Templates are looked for in "
                                              "%.1000s/templates",
                 dir);
        snprintf(file_line, sizeof file_line, "The settings are in %.1000s",
                 conf_file);
    }

    field_init(&f[SU_A_TEMPLATES], "", templates, 0, NULL);
    f[SU_A_TEMPLATES].note = 1;
    field_init(&f[SU_A_FILE], "", file_line, 0, NULL);
    f[SU_A_FILE].note = 1;
    field_init(&f[SU_A_NOTE], "", "", 0, NULL);
    f[SU_A_NOTE].note = 1;

    for (int i = SU_PAGE2; i < SU_COUNT; i++) {
        f[i].page = 2;
    }

    colors_page_begin();
    f_pages_set = 3;
    f_custom_set = 1;
    f_page_titles_set = setup_titles;

    f_check = setup_check;
    f_del_default = 1;
    f_bottom_text = "Ins: Use Settings  Del: Default  PgUp/PgDn: Page  "
                    "Esc: Cancel";

    if (form("Setup", f, SU_COUNT, "Setup", hook_files) == 0) {
        colors_page_end(0);

        return;
    }

    int kept = config_clamp_backups(atoi(f[SU_BACKUPS].val));

    if (kept < backups_val) {
        /* A lower number asks first; No keeps the number that it was, and
           the rest of the settings are saved. */
        char text[600];

        backup_lowering_text(backups_val, kept, text, sizeof text);

        if (popup_confirm("Backups Kept", text) == 0) {
            kept = backups_val;
        }
    }

    str_copy(spell_lang, sizeof spell_lang, f[SU_LANGUAGE].val);
    str_copy(workdir, sizeof workdir, f[SU_WORKDIR].val);
    str_copy(macros_val, sizeof macros_val, f[SU_A_MACRO_SET].val);
    str_copy(library_val, sizeof library_val, f[SU_A_LIBRARY].val);
    str_copy(print_to_val, sizeof print_to_val,
             strncmp(f[SU_PRINT_TO].val, "Printer", 7) == 0 ? "printer" :
             strcmp(f[SU_PRINT_TO].val, PJ_PDF_VIEWER) == 0 ? "viewer" :
                                                                 "file");
    queue_of_choice(f[SU_QUEUE].val, print_queue_val, sizeof print_queue_val);
    str_copy(pdf_dir_val, sizeof pdf_dir_val, f[SU_PDF_DIR].val);
    str_copy(msed_path, sizeof msed_path, f[SU_A_EDITOR].val);
    str_copy(helppath, sizeof helppath, f[SU_A_HELP].val);
    autosave_yes = strcmp(f[SU_AUTOSAVE].val, "Yes") == 0;
    autosave_secs = atoi(f[SU_INTERVAL].val);
    undo_levels_val = config_clamp_undo(atoi(f[SU_UNDO].val));
    str_copy(charset_val, sizeof charset_val,
             strcmp(f[SU_A_CHARSET].val, "UTF-8") == 0 ? "utf8" :
             strcmp(f[SU_A_CHARSET].val, "VT100") == 0 ? "vt100" :
             strcmp(f[SU_A_CHARSET].val, "ASCII") == 0 ? "ascii" :
                                                      "auto");
    str_copy(paper_val, sizeof paper_val,
             strcmp(f[SU_PAPER].val, "A4") == 0 ? "a4" : "");
    backups_val = kept;
    x11_rows_val = rows_value(f[SU_ROWS].val);
    term_rows_val = rows_value(f[SU_ROWS].val2);
    str_copy(pdf_viewer, sizeof pdf_viewer, f[SU_VIEWER].val);

    if (w_help != NULL) {
        help_free(w_help);
        w_help = NULL;
    }

    if (helppath[0] != '\0') {
        w_help = help_load(helppath);
    }

    if (conf_save() != 0) {
        popup_msg("Setup", "Settings are in effect but could not "
                           "be saved");
    } else {
        say("Settings saved to %s", conf_file);
    }

    colors_page_end(1);
}


/* ================================================= TeX font association */
/*
 * The list of the fonts for printing (docs/FONTS.txt): a font of a document
 * is a number, and this list says which LaTeX font it is.  Choose a font to
 * change it; a font that is not installed can be downloaded from CTAN.
 */
static void
font_label(const FontEntry *e, int n, char *out, size_t cap)
{
    snprintf(out, cap, "%2d  %-24.24s %-15.15s %s", n,
             e->name[0] != '\0' ? e->name : "(not set)",
             e->package[0] != '\0' ? e->package :
             e->name[0] != '\0'    ? "(LaTeX default)" :
                                    "",
             e->package[0] != '\0' && fonts_installed(e) == 0 ?
                 "not installed" :
                 "");
}



/*
 * The tick of the install of a font: show what is being done in a box, as
 * the search for printers does, and look at the keys.  Esc stops the
 * download.  Returns 1 for Esc.
 */
static int
install_tick(void *ctx UNUSED, const char *text, int can_stop)
{
    popup_notice("Installing font", text,
                 can_stop != 0 ? "Esc stops" : "Please wait");

    return term_getkey(50) == K_ESC ? 1 : 0;
}

static void
edit_font(FontEntry *e, int n)
{
    Field f[9];
    char title[32];
    static const char *fam[] = {"roman", "sans", "mono"};
    static const char *install_now[] = {"No", "Yes"};
    char num[16];

    snprintf(title, sizeof title, "Font %d", n);
    snprintf(num, sizeof num, "%d", n);
    field_init(&f[0], "Font Number:", num, 1, "Font Number");
    field_init(&f[1], "Name:", e->name, 0, NULL);
    field_init(&f[2], "LaTeX Package:", e->package, 0, NULL);
    field_init(&f[3], "Package Options:", e->options, 0, NULL);
    field_init(&f[4], "Family (roman, sans or mono):", fam[e->family], 0, NULL);
    field_init(&f[5], "CTAN Archive to Download:", e->ctan, 0, NULL);
    field_init(&f[6], "Map File to Enable:", e->map, 0, NULL);
    field_init(&f[7], "Installed:",
               fonts_installed(e) != 0 ? "Yes" : "No", 1, "Installed");
    field_init(&f[8], "Install Now (Yes or No):", "No", 0, NULL);
    /* Fixed sets of values: Space takes the next one, as in every choice. */
    f[4].choices = fam;
    f[4].nchoices = 3;
    f[8].choices = install_now;
    f[8].nchoices = 2;

    if (form(title, f, 9, "TeX Font Association", NULL) == 0) {
        return;
    }

    int family = strcasecmp(f[4].val, "roman") == 0 ? FF_ROMAN :
                 strcasecmp(f[4].val, "sans") == 0   ? FF_SANS :
                 strcasecmp(f[4].val, "mono") == 0   ? FF_MONO :
                                                       -1;

    if (family < 0) {
        popup_msg("Font", "The family is roman, sans or mono; not saved");

        return;
    }

    if (strchr(f[1].val, '|') != NULL || fonts_word_ok(f[2].val) == 0 ||
        fonts_word_ok(f[3].val) == 0 || fonts_word_ok(f[5].val) == 0 ||
        fonts_word_ok(f[6].val) == 0) {
        popup_msg("Font", "Only letters, digits and . _ - + = , can be used "
                          "for the package, options, archive and map file; "
                          "not saved");

        return;
    }

    str_copy(e->name, sizeof e->name, f[1].val);
    str_copy(e->package, sizeof e->package, f[2].val);
    str_copy(e->options, sizeof e->options, f[3].val);
    e->family = family;
    str_copy(e->ctan, sizeof e->ctan, f[5].val);
    str_copy(e->map, sizeof e->map, f[6].val);

    if (strcasecmp(f[8].val, "yes") == 0) {
        char msg[400];
        int rc = fonts_install(e, msg, sizeof msg, install_tick, NULL);

        if (rc < 0) {
            popup_msg("Install failed", msg);
        } else if (rc > 0) {
            say("%s", msg);
        } else {
            popup_msg("Installed", msg);
        }

        fonts_installed_forget(e);
    }
}


static void
do_fonts(void)
{
    FontEntry t[FONT_COUNT];
    static char label[FONT_COUNT][120];
    const char *items[FONT_COUNT];
    char err[200];
    int sel = 0;

    fonts_load(t);

    for (;;) {
        fonts_installed_prefetch(t);

        for (int i = 0; i < FONT_COUNT; i++) {
            font_label(&t[i], i + 1, label[i], sizeof label[i]);
            items[i] = label[i];
        }

        int c = run_menu("TeX Font Association", "Press Ins to Change a Font",
                         items, FONT_COUNT, "TeX Font Association", NULL, 1,
                         &sel);

        if (c < 0) {
            return;
        }

        edit_font(&t[c], c + 1);

        if (fonts_save(t, err, (int)sizeof err) != 0) {
            popup_msg("Fonts", err);
        }
    }
}


/*
 * Makes the log of this run, which every program that str starts writes to,
 * and writes the first line of it.  A run that cannot have a log goes on
 * without one.
 */
static void
start_run_log(void)
{
    char dir[PATH_MAX];
    char confdir[PATH_MAX - 8];

    log_open("str");

    if (config_directory(confdir, sizeof confdir) != 0) {
        return;
    }

    snprintf(dir, sizeof dir, "%s/log", confdir);

    if (logrun_start(dir, LOGRUN_KEEP) != 0) {
        return;
    }

    atexit(logrun_end);
    log_note("str: run starts, build type %s, log %s", strata_build_name,
             logrun_path());
}


/* ===================================================================== main */
int
main(int argc, char **argv)
{
    static const char *items[] = {EDIT_ENTRY,
                                  "Maintain Dictionaries",
                                  "Compare Document Versions",
                                  "Restore Backup",
                                  "TeX Font Association",
                                  "Setup",
                                  "Quit"};
    const char *hint = NULL;
    const char *file = NULL;

    conf_load();
    start_run_log();
    vars_set_specials(vars_process());
    /* The printer list is looked for in the background, once. */
    pj_printers_prefetch();

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-H") == 0 ||
             strcmp(argv[i], "--help-file") == 0) &&
            i + 1 < argc) {
            hint = argv[++i];
        } else if (strcmp(argv[i], "--default-colors") == 0) {
            color_force_defaults();
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("str (Strata) %s\n", strata_version);
            printf("file format %d\n", STRATA_FILE_FORMAT);

            return 0;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--usage") == 0) {
            puts("usage: str [-H HELPFILE] [--default-colors] [document]\n"
                 "  --version         print the version and exit\n"
                 "  Document Manager: the master menu for stred.\n"
                 "  --default-colors  use the colors that the program is "
                 "delivered with,\n"
                 "                    not those of the settings (also the "
                 "variable\n"
                 "                    STRATA_DEFAULT_COLORS)");
            return 0;
        } else if (argv[i][0] != '-') {
            file = argv[i];
        }
    }

    if (hint != NULL || helppath[0] == '\0' || access(helppath, R_OK) != 0) {
        char b[512];

        if (help_find_file(hint, b, sizeof b) != NULL) {
            str_copy(helppath, sizeof helppath, b);
        }
    }

    find_msed();

    if (workdir[0] == '\0' || is_dir(workdir) == 0) {
        if (getcwd(workdir, sizeof workdir) == NULL) {
            snprintf(workdir, sizeof workdir, ".");
        }
    }

    if (file != NULL) {
        str_copy(last_name, sizeof last_name, file);
    }

    if (term_open() != 0) {
        fprintf(stderr, "str needs an interactive terminal (or "
                        "an X display with the x11 build)\n");
        return 1;
    }

    term_set_title("str");

    if (helppath[0] != '\0') {
        w_help = help_load(helppath);
    }

    const char *charset_notice = term_charset_notice();

    if (charset_notice != NULL) {
        say("%s", charset_notice);
    }

    w_redraw = NULL;
    screen = paint_blank;

    /* popups repaint through w_redraw -> the current screen painter */
    for (;;) {
        w_redraw = paint_under;
        int c = run_menu(VERSION_TITLE, "Press Ins to Select Action", items,
                         NUM(items), "Document Manager", NULL, 0,
                         &main_sel);

        switch (c) {
            case 0:
                do_edit();
                break;

            case 1:
                dict_panel(workdir);
                break;

            case 2:
                compare_panel(workdir, last_name);
                break;

            case 3:
                restore_panel(workdir, last_name, backups_val);
                break;

            case 4:
                do_fonts();
                break;

            case 5:
                do_setup();
                break;

            case 6:
                term_close();
                if (w_help != NULL) {
                    help_free(w_help);
                }
                log_note("str: run ends, Quit was chosen from the menu, log %s",
                         logrun_path() != NULL ? logrun_path() : "(none)");
                return 0;

            default:
                break;
        }
    }
}


static void
paint_under(void)
{
    if (screen != NULL) {
        screen();
    } else {
        paint_blank();
    }
}
