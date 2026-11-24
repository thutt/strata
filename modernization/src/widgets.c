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

/* widgets.c - shared popup menus, prompts, pager and help viewer (used by
   stred and str). */
#include "util.h"
#include "widgets.h"
#include "backup.h"
#include "layout.h"
#include "term.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <ctype.h>

Help *w_help;
void (*w_redraw)(void);

/* The place that F1 names instead of the title of the popup, or NULL. */
static const char *help_name;


void
w_set_help(const char *name)
{
    help_name = name;
}


/* The name of the place that F1 asks the help about. */
static const char *
help_place(const char *title)
{
    return help_name != NULL ? help_name : title;
}


void
w_base(void)
{
    if (w_redraw != NULL) {
        w_redraw();
    } else {
        term_clear();
    }
}


/* =============================================================== popups */
void
box(int x, int y, int w, int h, const char *title)
{
    term_style(TS_PANEL);
    term_fill(x, y, w, h, ' ', 0);
    term_box(x, y, w, h, 0, 1);

    if (title != NULL && *title != '\0') {
        int tl = slen(title);

        term_puts(x + (w - tl - 2) / 2, y, " ", 0);
        term_puts(x + (w - tl - 2) / 2 + 1, y, title, TA_BOLD);
        term_puts(x + (w - tl - 2) / 2 + 1 + tl, y, " ", 0);
    }
}


/*
 * Does the field hold the name of a file or a folder?  Such a field holds
 * UTF-8, as the file system has it, and is edited one character at a time;
 * the other fields hold the code page of the document format.
 */
static int
is_name(const WField *f)
{
    return f->kind == WF_TEXT && (f->pick == PICK_DIR || f->pick == PICK_FILE);
}


/* Accept the panel: 1 when the check allows it, else 0 with its message. */
static int
form_accept(const WField *f, int n,
            int (*check)(const WField *f, int n, char *err, int errcap),
            const char *title)
{
    if (check != NULL) {
        char err[256] = "";

        if (check(f, n, err, sizeof err) == 0) {
            popup_msg(title, err);

            return 0;
        }
    }

    return 1;
}


/*
 * The hint on the bottom border of a list: the keys that choose an entry,
 * as long as it fits the width of the box.
 */
static void
list_hint(int x, int y, int w)
{
    const char *full = " Ins or Enter selects, Esc cancels ";
    const char *shrt = " Ins selects ";

    if (w >= slen(full) + 4) {
        term_puts(x + 2, y, full, 0);
    } else if (w >= slen(shrt) + 4) {
        term_puts(x + 2, y, shrt, 0);
    }
}


/* The hint of a list of folders and files: Left goes up one folder. */
static void
list_hint_up(int x, int y, int w)
{
    const char *full = " Ins or Enter selects, Left goes up, Esc cancels ";
    const char *shrt = " Ins selects, Left goes up ";

    if (w >= slen(full) + 4) {
        term_puts(x + 2, y, full, 0);
    } else if (w >= slen(shrt) + 4) {
        term_puts(x + 2, y, shrt, 0);
    }
}


/* generic scrolling list; returns index or -1.  First-letter selection. */
int
popup_menu(const char *title, const char **items, int n, int sel)
{
    widgets_list_ins = 0;

    if (sel < 0 || sel >= n) {
        sel = 0;
    }

    int top = 0;
    char fitted[512];
    const char *full = title;

    for (;;) {
        w_base();
        int R = term_rows();
        int C = term_cols();

        /*
         * A title that is a question about a path is longer than the screen
         * when the path is long: the path is shortened from the front, so
         * that the end of the question is seen.
         */
        fit_message(full, C - 8, fitted, sizeof fitted);
        title = fitted;

        int w = swidth(title) + 4;

        for (int i = 0; i < n; i++) {
            int l = swidth(items[i]) + 4;

            if (l > w) {
                w = l;
            }
        }

        if (w > C - 2) {
            w = C - 2;
        }

        int h = n + 2;

        if (h > R - 4) {
            h = R - 4;
        }

        int vis = h - 2;

        if (sel < top) {
            top = sel;
        }

        if (sel >= top + vis) {
            top = sel - vis + 1;
        }

        int x = (C - w) / 2;
        int y = (R - h) / 2;

        box(x, y, w, h, title);
        list_hint(x, y + h - 1, w);

        /* An entry that is wider than the list shows its end, which is the
           end of a name with its extension. */
        for (int i = 0; i < vis && top + i < n; i++) {
            char entry[1200];

            name_tail(items[top + i], w - 4, entry, sizeof entry);
            term_puts(x + 2, y + 1 + i, entry,
                      (top + i == sel) ? TA_REVERSE : 0);
        }

        for (int i = 0; i < vis && top + i < n; i++) {
            if (top + i == sel) {
                char entry[1200];

                name_tail(items[top + i], w - 4, entry, sizeof entry);
                term_fill(x + 1, y + 1 + i, w - 2, 1, ' ', TA_REVERSE),
                    term_puts(x + 2, y + 1 + i, entry, TA_REVERSE);
            }
        }

        term_cursor(0, 0, 0);
        term_present();
        int k = term_getkey(-1);
        if (k == K_F(1)) {
            help_context(help_place(title));
            continue;
        }

        switch (K_BASE(k)) {
            case K_UP:
                if (sel > 0) {
                    sel--;
                }
                break;

            case K_DOWN:
                if (sel < n - 1) {
                    sel++;
                }
                break;

            case K_HOME:
                sel = 0;
                break;

            case K_END:
                sel = n - 1;
                break;

            case K_PGUP:
                sel -= vis;
                if (sel < 0) {
                    sel = 0;
                }
                break;

            case K_PGDN:
                sel += vis;
                if (sel > n - 1) {
                    sel = n - 1;
                }
                break;

            case K_INS:
                widgets_list_ins = 1;

                return sel;

            case 13:
                return sel;

            case K_ESC:
                return -1;

            case K_RESIZE:
                break;

            default:
                if (k >= 32 && k < 127) {
                    for (int i = 1; i <= n; i++) {
                        int j = (sel + i) % n;

                        if (to_upper(items[j][0]) == to_upper(k)) {
                            sel = j;

                            if (n <= 12) {
                                return j;
                            }

                            break;
                        }
                    }
                }
        }
    }
}


/* Draw the text of d in lines of at most w characters, at most rows lines. */
static void
put_wrapped(int x, int y, int w, int rows, const char *d)
{
    int row = 0;
    const char *p = d;

    while (*p != '\0' && row < rows) {
        int len = (int)strlen(p);
        int cut;

        if (len <= w) {
            cut = len;
        } else {
            cut = w;

            while (cut > 0 && p[cut] != ' ') {
                cut--;
            }

            if (cut == 0) {
                cut = w;
            }
        }

        char line[256];

        snprintf(line, sizeof line, "%.*s", cut < 255 ? cut : 255, p);
        term_puts(x, y + row, line, 0);
        row++;
        p += cut;

        while (*p == ' ') {
            p++;
        }
    }
}


/*
 * Does the text have a word that begins with the typed text (any case)?  A
 * word begins at the start of the text and after every character that is
 * neither a letter nor a digit, so "5160" finds "label-us-5160" and
 * "avery 5160 address" finds "Avery 5160 address labels".
 */
int
list_word_has(const char *text, const char *find, size_t n)
{
    for (size_t i = 0; text[i] != '\0'; i++) {
        if ((i == 0 || isalnum((unsigned char)text[i - 1]) == 0) &&
            strncasecmp(text + i, find, n) == 0) {
            return 1;
        }
    }

    return 0;
}


/* The first entry (from the top) whose name or description has the typed
   text at the start of a word, or -1. */
static int
list_find_entry(const char **names, const char **descs, int n,
                const char *find)
{
    size_t len = strlen(find);

    for (int i = 0; i < n; i++) {
        if (list_word_has(names[i], find, len) != 0 ||
            list_word_has(descs[i], find, len) != 0) {
            return i;
        }
    }

    return -1;
}


char widgets_list_find[48];


static int
list_desc_run(const char *title, const char **names, const char **descs,
              int n, int sel, int up)
{
    int top = 0;
    const int drows = 3;
    char find[48] = "";

    widgets_list_ins = 0;

    /* A list of folders is shown again with the text that was typed: the
       caller sets the text in widgets_list_find. */
    if (up != 0) {
        str_copy(find, sizeof find, widgets_list_find);
        widgets_list_find[0] = '\0';
    }

    if (sel < 0 || sel >= n) {
        sel = 0;
    }

    for (;;) {
        w_base();
        int R = term_rows();
        int C = term_cols();
        int w = slen(title) + 4;

        for (int i = 0; i < n; i++) {
            int l = slen(names[i]) + 4;

            if (l > w) {
                w = l;
            }
        }

        if (w < 56) {
            w = 56;
        }

        if (w > C - 2) {
            w = C - 2;
        }

        int vis = n < R - 10 ? n : R - 10;

        if (vis < 1) {
            vis = 1;
        }

        int h = vis + drows + 3;
        int x = (C - w) / 2;
        int y = (R - h) / 2;

        if (sel < top) {
            top = sel;
        }

        if (sel >= top + vis) {
            top = sel - vis + 1;
        }

        box(x, y, w, h, title);

        if (find[0] != '\0' && w >= (int)strlen(find) + 14) {
            char shown[80];

            snprintf(shown, sizeof shown, " Find: %s ", find);
            term_puts(x + 2, y + h - 1, shown, 0);
        } else if (up != 0) {
            list_hint_up(x, y + h - 1, w);
        } else {
            list_hint(x, y + h - 1, w);
        }

        for (int i = 0; i < vis && top + i < n; i++) {
            if (top + i == sel) {
                term_fill(x + 1, y + 1 + i, w - 2, 1, ' ', TA_REVERSE);
            }

            term_puts(x + 2, y + 1 + i, names[top + i],
                      top + i == sel ? TA_REVERSE : 0);
        }

        term_fill(x + 1, y + 1 + vis, w - 2, 1, 0xC4, 0);
        put_wrapped(x + 2, y + 2 + vis, w - 4, drows, descs[sel]);
        term_cursor(0, 0, 0);
        term_present();

        int k = term_getkey(-1);

        if (k == K_F(1)) {
            help_context(help_place(title));
            continue;
        }

        if (K_BASE(k) != K_BKSP && (k < 32 || k >= 127)) {
            find[0] = '\0';
        }

        switch (K_BASE(k)) {
            case K_UP:
                if (sel > 0) {
                    sel--;
                }
                break;

            case K_DOWN:
                if (sel < n - 1) {
                    sel++;
                }
                break;

            case K_HOME:
                sel = 0;
                break;

            case K_END:
                sel = n - 1;
                break;

            case K_PGUP:
                sel = sel - vis < 0 ? 0 : sel - vis;
                break;

            case K_PGDN:
                sel = sel + vis > n - 1 ? n - 1 : sel + vis;
                break;

            case K_INS:
                widgets_list_ins = 1;

                return sel;

            case 13:
                return sel;

            case K_ESC:
                return -1;

            case K_LEFT:
                if (up != 0) {
                    return POPUP_LIST_UP;
                }
                break;

            case K_BKSP:
                if (find[0] != '\0') {
                    find[strlen(find) - 1] = '\0';

                    if (find[0] != '\0') {
                        int f = list_find_entry(names, descs, n, find);

                        sel = f >= 0 ? f : sel;
                    }
                } else if (up != 0) {
                    return POPUP_LIST_UP;
                }
                break;

            default:
                /* Typed text selects the first entry that has a word that
                   begins with it; a letter that finds none is ignored. */
                if (k >= 32 && k < 127 && strlen(find) < sizeof find - 1 &&
                    (find[0] != '\0' || k != ' ')) {
                    size_t len = strlen(find);
                    int f;

                    find[len] = (char)k;
                    find[len + 1] = '\0';
                    f = list_find_entry(names, descs, n, find);

                    if (f >= 0) {
                        sel = f;
                    } else if (up != 0) {
                        /* Not in this folder: the caller looks in the others,
                           and shows the list again with the text. */
                        str_copy(widgets_list_find, sizeof widgets_list_find,
                                 find);

                        return POPUP_LIST_FIND;
                    } else {
                        find[len] = '\0';
                    }
                }
                break;
        }
    }
}


int
popup_list_desc(const char *title, const char **names, const char **descs,
                int n, int sel)
{
    return list_desc_run(title, names, descs, n, sel, 0);
}


int
popup_list_desc_up(const char *title, const char **names, const char **descs,
                   int n, int sel)
{
    return list_desc_run(title, names, descs, n, sel, 1);
}


/*
 * Single-line input; returns 1 on Enter, 0 on Esc.  A name is the name of a
 * file: it is UTF-8 and is edited one character at a time.
 */
static int
input_loop(const char *title, const char *label, char *buf, int cap, int name)
{
    int len = slen(buf);
    int pos = len;
    int first = 1;

    for (;;) {
        w_base();
        int C = term_cols();
        int R = term_rows();
        int w = C - 10 < 64 ? C - 10 : 64;

        if (w < 24) {
            w = 24;
        }

        int x = (C - w) / 2;
        int y = R / 2 - 2;

        box(x, y, w, 5, title);
        term_puts(x + 2, y + 1, label, 0);
        int fw = w - 4;

        term_fill(x + 2, y + 2, fw, 1, 0xFA, TA_DIM);
        char cells[1100];
        int cpos = pos;

        if (name != 0) {
            field_cells(buf, pos, cells, sizeof cells, &cpos);
        } else {
            str_copy(cells, sizeof cells, buf);
        }

        int clen = slen(cells);
        int off = cpos >= fw ? cpos - fw + 1 : 0;

        for (int i = 0; i < fw && off + i < clen; i++) {
            term_put(x + 2 + i, y + 2, u8(cells[off + i]),
                     first != 0 ? TA_REVERSE : 0);
        }

        term_puts(x + 2, y + 3, "Enter accepts, Esc cancels", TA_DIM);
        term_cursor(x + 2 + cpos - off, y + 2, 1);
        term_present();
        int k = term_getkey(-1);
        if (k == K_F(1)) {
            help_context(help_place(title));
            continue;
        }

        if (k == K_NBSP) {
            k = NBSP_BYTE;
        }

        switch (K_BASE(k)) {
            case 13:
                return 1;

            case K_ESC:
                return 0;

            case K_LEFT:
                if (pos > 0) {
                    pos = name != 0 ? name_prev(buf, pos) : pos - 1;
                }
                first = 0;
                break;

            case K_RIGHT:
                if (pos < len) {
                    pos = name != 0 ? name_next(buf, pos) : pos + 1;
                }
                first = 0;
                break;

            case K_HOME:
                pos = 0;
                first = 0;
                break;

            case K_END:
                pos = len;
                first = 0;
                break;

            case K_BKSP:
                if (first != 0) {
                    len = pos = 0;
                    buf[0] = 0;
                } else if (pos > 0) {
                    int from = name != 0 ? name_prev(buf, pos) : pos - 1;

                    memmove(buf + from, buf + pos, usz(len - pos + 1));
                    len -= pos - from;
                    pos = from;
                }
                first = 0;
                break;

            case K_DEL:
                if (pos < len) {
                    int to = name != 0 ? name_next(buf, pos) : pos + 1;

                    memmove(buf + pos, buf + to, usz(len - to + 1));
                    len -= to - pos;
                }
                first = 0;
                break;

            default:
                if (k >= 32 && k <= 255 && (k & ~0xFF) == 0) {
                    char put[4];
                    int np = name != 0 ? key_utf8(k, put) : 1;

                    put[0] = name != 0 ? put[0] : chr(k);

                    if (first != 0) {
                        len = pos = 0;
                        buf[0] = 0;
                        first = 0;
                    }

                    if (len + np < cap) {
                        memmove(buf + pos + np, buf + pos, usz(len - pos + 1));
                        memcpy(buf + pos, put, usz(np));
                        pos += np;
                        len += np;
                    }
                }
        }
    }
}


int
popup_input(const char *title, const char *label, char *buf, int cap)
{
    return input_loop(title, label, buf, cap, 0);
}


int
popup_name_input(const char *title, const char *label, char *buf, int cap)
{
    return input_loop(title, label, buf, cap, 1);
}


/* The most names of one kind that a checklist reads: a limit of memory. */
#define BROWSE_MAX 100000


static int
name_cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}


/* Sort file names with strcmp, for the checklists. */
static void
sort_names(char **v, int n)
{
    if (n > 1) {
        qsort(v, usz(n), sizeof v[0], name_cmp);
    }
}


/* Add a copy of a name to a list that grows; 0 when there is no room. */
static int
add_name(char ***v, int *n, int *cap, const char *name)
{
    if (*n >= BROWSE_MAX) {
        return 0;
    }

    if (*n == *cap) {
        int ncap = *cap == 0 ? 256 : *cap * 2;
        char **grown = realloc(*v, usz(ncap) * sizeof **v);

        if (grown == NULL) {
            return 0;
        }

        *v = grown;
        *cap = ncap;
    }

    (*v)[*n] = strdup(name);

    if ((*v)[*n] == NULL) {
        return 0;
    }

    (*n)++;

    return 1;
}


/* Does the name end with one of the suffixes (all names when it is "")? */
static bool
has_suffix(const char *name, const char *suffixes)
{
    if (suffixes == NULL || suffixes[0] == '\0') {
        return true;
    }

    const char *p = suffixes;

    while (*p != '\0') {
        while (*p == ' ') {
            p++;
        }

        size_t sl = 0;

        while (p[sl] != '\0' && p[sl] != ' ') {
            sl++;
        }

        size_t nl = strlen(name);

        if (sl > 0 && nl >= sl && strncasecmp(name + nl - sl, p, sl) == 0) {
            return true;
        }

        /* The suffix .bak also stands for the numbered backups, .bak01 to
           .bak99. */
        if (sl == 4 && strncasecmp(p, ".bak", 4) == 0 && nl >= 6 &&
            is_digit(name[nl - 1]) != 0 && is_digit(name[nl - 2]) != 0 &&
            strncasecmp(name + nl - 6, ".bak", 4) == 0) {
            return true;
        }

        p += sl;
    }

    return false;
}



/* The directory that a panel starts a list of files in when it has none. */
static char widgets_dir[512];


void
widgets_set_dir(const char *dir)
{
    str_copy(widgets_dir, sizeof widgets_dir, dir != NULL ? dir : "");
}


/* A directory name that fits in a title: the end of it. */
static void
short_dir(const char *dir, char *out, size_t cap)
{
    size_t n = strlen(dir);
    size_t room = cap > 60 ? 56 : cap - 4;

    if (n <= room) {
        str_copy(out, cap, dir);
    } else {
        snprintf(out, cap, "...%s", dir + n - room);
    }
}


int
popup_browse(const char *start, int dirs_only, const char *suffixes,
             char *dir_out, size_t dcap, char *name_out, size_t ncap)
{
    char cur[PATH_MAX];
    int result = 0;

    if (start[0] != '\0') {
        str_copy(cur, sizeof cur, start);
    } else if (widgets_dir[0] != '\0') {
        str_copy(cur, sizeof cur, widgets_dir);
    } else {
        str_copy(cur, sizeof cur, ".");
    }

    {
        char real[PATH_MAX];

        if (realpath(cur, real) == NULL && realpath(".", real) == NULL) {
            popup_msg("Directory", "Cannot read the directory");

            return 0;
        }

        str_copy(cur, sizeof cur, real);
    }

    for (;;) {
        DIR *dp = opendir(cur);
        char **dirs = NULL;
        char **files = NULL;
        int nd = 0;
        int nf = 0;
        int dcapn = 0;
        int fcapn = 0;
        struct dirent *de;

        if (dp == NULL) {
            popup_msg("Directory", "Cannot read the directory");

            return 0;
        }

        /* Every entry is read and then all of them are sorted, so that the
           list starts with the first names. */
        while ((de = readdir(dp)) != NULL) {
            char full[PATH_MAX + 300];
            struct stat st;

            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
                continue;
            }

            path_join(full, sizeof full, cur, de->d_name);

            if (stat(full, &st) != 0) {
                continue;
            }

            if (S_ISDIR(st.st_mode)) {
                /* The folder of the backups is not a place to go to. */
                if (strcmp(de->d_name, BACKUP_FOLDER) != 0) {
                    add_name(&dirs, &nd, &dcapn, de->d_name);
                }
            } else if (dirs_only == 0 && has_suffix(de->d_name, suffixes)) {
                add_name(&files, &nf, &fcapn, de->d_name);
            }
        }

        closedir(dp);
        sort_names(dirs, nd);
        sort_names(files, nf);

        /* Entry 0 takes the directory (a list of directories), then the way
           up, the directories with a slash, and the files. */
        const char **items = malloc(usz(nd + nf + 3) * sizeof *items);
        char **label = calloc(usz(nd + 3), sizeof *label);
        int n = 0;
        int first = 0;

        if (items == NULL || label == NULL) {
            free(items);
            free(label);
            popup_msg("Directory", "Cannot read the directory");

            return 0;
        }

        if (dirs_only != 0) {
            items[n++] = "[Use this directory]";
            first = 1;
        }

        if (strcmp(cur, "/") != 0) {
            items[n++] = "../";
        }

        int nup = n - first;

        for (int i = 0; i < nd; i++) {
            size_t ll = strlen(dirs[i]) + 2;

            label[i] = malloc(ll);

            if (label[i] == NULL) {
                continue;
            }

            memcpy(label[i], dirs[i], ll - 2);
            label[i][ll - 2] = '/';
            label[i][ll - 1] = '\0';
            items[n++] = label[i];
        }

        for (int i = 0; i < nf; i++) {
            items[n++] = files[i];
        }

        char title[80];

        short_dir(cur, title, sizeof title);

        int c = popup_menu(title, items, n, 0);

        if (c < 0) {
            result = 0;
        } else if (dirs_only != 0 && c == 0) {
            str_copy(dir_out, dcap, cur);
            name_out[0] = '\0';
            result = 1;
        } else if (nup == 1 && c == first) {
            char *sl = strrchr(cur, '/');

            if (sl != NULL && sl != cur) {
                *sl = '\0';
            } else {
                str_copy(cur, sizeof cur, "/");
            }

            c = -2;
        } else if (c < first + nup + nd) {
            char next[PATH_MAX + 300];

            path_join(next, sizeof next, cur, dirs[c - first - nup]);

            if (realpath(next, cur) == NULL) {
                str_copy(cur, sizeof cur, next);
            }

            c = -2;
        } else {
            str_copy(dir_out, dcap, cur);
            str_copy(name_out, ncap, files[c - first - nup - nd]);
            result = 1;
        }

        for (int i = 0; i < nd; i++) {
            free(dirs[i]);
            free(label[i]);
        }

        for (int i = 0; i < nf; i++) {
            free(files[i]);
        }

        free(dirs);
        free(files);
        free(label);
        free(items);

        if (c != -2) {
            return result;
        }
    }
}


int widgets_shift_ins;

/* 1 when the last list popup was left by Ins, 0 when by Enter or Esc. */
int widgets_list_ins;


/*
 * The checklist for a text field: choose a directory or a file and put it
 * in the field.
 */
static void
form_pick(WField *f, int focus)
{
    if (f[focus].pick == PICK_CALL) {
        if (f[focus].call != NULL) {
            f[focus].call(f[focus].val, sizeof f[focus].val, f[focus].arg);
        }

        return;
    }

    if (f[focus].pick == PICK_LIST) {
        const char *items[512];
        int n = 0;

        while (f[focus].list != NULL && f[focus].list[n] != NULL && n < 512) {
            items[n] = f[focus].list[n];
            n++;
        }

        if (n == 0) {
            popup_msg("Checklist", "There is nothing to choose from");

            return;
        }

        int c = popup_menu("Entries", items, n, 0);

        if (c >= 0 && f[focus].call_item != NULL && f[focus].call != NULL &&
            strcmp(items[c], f[focus].call_item) == 0) {
            f[focus].call(f[focus].val, sizeof f[focus].val, f[focus].arg);
        } else if (c >= 0) {
            str_copy(f[focus].val, sizeof f[focus].val, items[c]);
        }

        return;
    }

    /* The directory to begin with: the one the first field of the panel
       holds, the one in the name that is typed, or the usual one. */
    char start[512] = "";
    char dir[512];
    char name[512];
    struct stat st;

    if (f[focus].pick == PICK_DIR) {
        str_copy(start, sizeof start, f[focus].val);
    } else if (focus != 0 && f[0].pick == PICK_DIR) {
        str_copy(start, sizeof start, f[0].val);
    } else if (strchr(f[focus].val, '/') != NULL) {
        char *sl;

        str_copy(start, sizeof start, f[focus].val);
        sl = strrchr(start, '/');
        *sl = '\0';

        if (start[0] == '\0') {
            str_copy(start, sizeof start, "/");
        }
    }

    if (start[0] != '\0' && (stat(start, &st) != 0 || !S_ISDIR(st.st_mode))) {
        start[0] = '\0';
    }

    if (popup_browse(start, f[focus].pick == PICK_DIR, f[focus].suffixes, dir,
                     sizeof dir, name, sizeof name) == 0) {
        return;
    }

    if (f[focus].pick == PICK_DIR) {
        str_copy(f[focus].val, sizeof f[focus].val, dir);
    } else if (focus != 0 && f[0].pick == PICK_DIR) {
        str_copy(f[0].val, sizeof f[0].val, dir);
        str_copy(f[focus].val, sizeof f[focus].val, name);
    } else {
        /* A name in the directory that is usual stays a bare name. */
        char usual[512];
        char real[PATH_MAX];

        str_copy(usual, sizeof usual, widgets_dir[0] != '\0' ? widgets_dir :
                                                                ".");

        if (realpath(usual, real) != NULL && strcmp(real, dir) == 0) {
            str_copy(f[focus].val, sizeof f[focus].val, name);
        } else {
            path_join(f[focus].val, sizeof f[focus].val, dir, name);
        }
    }
}


static int
choice_count(const WField *f);


/* Is the entry of a choice field that is chosen the one that calls? */
static int
choice_calls(const WField *f)
{
    return f->kind == WF_CHOICE && f->call_item != NULL && f->call != NULL &&
           f->choices != NULL &&
           strcmp(f->choices[f->sel], f->call_item) == 0;
}


/* Run the call of a choice field and choose the entry before it again. */
static void
choice_run_call(WField *f)
{
    f->call(f->val, sizeof f->val, f->arg);
    f->sel = f->prev_sel;
}


/*
 * Let the fields look for a new list of entries, and note the entry that is
 * chosen when it is not the one that calls (the one to come back to).
 */
static void
form_refresh(WField *f, int n)
{
    for (int i = 0; i < n; i++) {
        if (f[i].kind != WF_CHOICE) {
            continue;
        }

        if (f[i].refresh != NULL) {
            f[i].refresh(&f[i]);
        }

        if (f[i].call_item == NULL || choice_calls(&f[i]) == 0) {
            f[i].prev_sel = f[i].sel;
        }
    }
}


static int
choice_count(const WField *f)
{
    int n = 0;

    while (f->choices != NULL && f->choices[n] != NULL) {
        n++;
    }

    return n;
}


/*
 * 1 when field i is active: it depends on no field, or on the one it names
 * and that has the choice.  A field that is not active stays in its place,
 * drawn as a text that is only shown (the look of WF_INFO), and the cursor
 * skips it: a panel never gains or loses a row.
 */
static int
field_active(const WField *f, int i)
{
    return f[i].dep == 0 ||
           (f[i].dep_le != 0 ? f[f[i].dep - 1].sel <= f[i].dep_sel :
                               f[f[i].dep - 1].sel == f[i].dep_sel);
}


int
popup_form_nav(const char *title, WField *f, int n,
               int (*check)(const WField *f, int n, char *err, int errcap),
               int paging)
{
    int focus = 0;
    int pos = 0;
    int first = 1;
    int lw = 0;

    for (int i = 0; i < n; i++) {
        if (slen(f[i].label) > lw) {
            lw = slen(f[i].label);
        }
    }

    pos = slen(f[0].val);

    for (;;) {
        w_base();
        int C = term_cols();
        int R = term_rows();
        int w = C - 6 < 72 ? C - 6 : 72;
        int shown = n;

        /* Fields are a row apart, or two when the panel fits that way. */
        int step = shown * 2 + 3 <= R - 2 ? 2 : 1;
        int h = shown * step + 3;
        int x = (C - w) / 2;
        int y = (R - h) / 2;
        int vx = x + 3 + lw + 2;
        int vw = x + w - 2 - vx;
        int cx = vx;

        form_refresh(f, n);

        /* The cursor never rests on a field that is not active. */
        for (int k = 0; k < n && field_active(f, focus) == 0 && n > 1; k++) {
            focus = (focus + 1) % n;
            pos = slen(f[focus].val);
        }

        box(x, y, w, h, title);
        if (paging != 0) {
            term_puts(x + (w - 46) / 2, y + h - 1,
                      " Ins accepts, Esc cancels, PgUp/PgDn panels ", 0);
        } else {
            term_puts(x + (w - 36) / 2, y + h - 1,
                      " Ins accepts, Esc cancels ", 0);
        }

        for (int i = 0; i < n; i++) {
            int ry = y + 1 + i * step;
            int attr = i == focus ? TA_REVERSE : 0;

            term_puts(x + 3 + lw - slen(f[i].label), ry, f[i].label, TA_BOLD);
            term_puts(x + 3 + lw, ry, ":", 0);

            if (f[i].kind == WF_CHOICE && field_active(f, i) == 0) {
                /* Not active: the entry as a text, no brackets. */
                term_puts(vx, ry, f[i].choices[f[i].sel], TA_DIM);
            } else if (f[i].kind == WF_CHOICE) {
                const char *c = f[i].choices[f[i].sel];

                /* An entry that has brackets already (the value that a
                   field takes from the document) is drawn as it is. */
                if (c[0] == '[') {
                    term_puts(vx, ry, c, attr);
                } else {
                    term_puts(vx, ry, "[", 0);
                    term_puts(vx + 1, ry, c, attr);
                    term_puts(vx + 1 + slen(c), ry, "]", 0);
                }
            } else {
                /* A name is UTF-8: its cells and the cell of the place are
                   used, one for each character. */
                char cells[1100];
                int cpos = pos;

                int info = f[i].kind == WF_INFO || field_active(f, i) == 0;

                if (is_name(&f[i]) != 0 || info != 0) {
                    field_cells(f[i].val, pos, cells, sizeof cells, &cpos);
                } else {
                    str_copy(cells, sizeof cells, f[i].val);
                }

                int len = slen(cells);
                int off = i == focus && cpos >= vw ? cpos - vw + 1 : 0;

                /*
                 * A path that is only shown (a text that is not being edited,
                 * and a directory or a file) shows its end: the name.
                 */
                if (len > vw && (info != 0 ||
                                 (i != focus && f[i].pick != PICK_NONE))) {
                    off = len - vw;
                }

                term_fill(vx, ry, vw, 1, info != 0 ? ' ' : 0xFA, TA_DIM);

                for (int k = 0; k < vw && off + k < len; k++) {
                    term_put(vx + k, ry, u8(cells[off + k]),
                             i == focus && first != 0 && f[i].kind == WF_TEXT ?
                                 TA_REVERSE :
                                 f[i].kind != WF_INFO && info != 0 ?
                                     TA_DIM :
                                     0);
                }

                if (i == focus) {
                    cx = vx + cpos - off;
                }
            }
        }

        term_cursor(cx, y + 1 + focus * step,
                    f[focus].kind == WF_TEXT);
        term_present();
        int k = term_getkey(-1);

        if (k == K_F(1)) {
            char place[200];

            snprintf(place, sizeof place, "%s:%s", help_place(title),
                     f[focus].label);
            help_context(place);
            continue;
        }

        if (k == K_NBSP) {
            k = NBSP_BYTE;
        }

        WField *cur = &f[focus];
        int len = slen(cur->val);
        int move = 0;

        /* The list of a field may have arrived while the key was awaited. */
        form_refresh(f, n);

        /* Enter on the entry that calls: the call runs, the entry before is
           chosen again, and the cursor stays. */
        if (K_BASE(k) == 13 && choice_calls(cur) != 0) {
            choice_run_call(cur);
            continue;
        }

        /* Keypad + (Alt-+ or F2) lists the directories or the files. */
        if (cur->kind == WF_TEXT && cur->pick != PICK_NONE &&
            ((K_BASE(k) == '+' && (k & KM_ALT) != 0) || K_BASE(k) == K_F(2))) {
            widgets_list_ins = 0;
            form_pick(f, focus);
            pos = slen(cur->val);
            first = 0;

            /* A list that was left by Ins finishes the panel, when the
               field says so. */
            if (cur->finish_on_ins != 0 && widgets_list_ins != 0) {
                widgets_list_ins = 0;

                if (form_accept(f, n, check, title) != 0) {
                    widgets_shift_ins = 0;

                    return 1;
                }
            }

            continue;
        }

        /* Keypad + (Alt-+ or F2) on a choice shows the list to pick from. */
        if (cur->kind == WF_CHOICE &&
            ((K_BASE(k) == '+' && (k & KM_ALT) != 0) || K_BASE(k) == K_F(2))) {
            const char *items[64];
            int cn = choice_count(cur) < 64 ? choice_count(cur) : 64;

            for (int i = 0; i < cn; i++) {
                items[i] = cur->choices[i];
            }

            int c = popup_menu(cur->label, items, cn, cur->sel);

            if (c >= 0) {
                cur->sel = c;
            }

            continue;
        }

        switch (K_BASE(k)) {
            case K_INS:
                /* A call that is chosen runs first; the panel goes on. */
                {
                    int ran = 0;

                    for (int i = 0; i < n; i++) {
                        if (choice_calls(&f[i]) != 0) {
                            choice_run_call(&f[i]);
                            ran = 1;
                        }
                    }

                    if (ran != 0) {
                        break;
                    }
                }

                if (form_accept(f, n, check, title) == 0) {
                    break;
                }

                widgets_shift_ins = (k & KM_SHIFT) != 0;

                return 1;

            case K_ESC:
                return 0;

            case K_PGUP:
                if (paging != 0) {
                    return POPUP_NEXT;
                }

                break;

            case K_PGDN:
                if (paging != 0) {
                    return POPUP_PREV;
                }

                break;

            case K_UP:
            case K_BTAB:
                move = -1;
                break;

            case K_DOWN:
            case 9:
            case 13:
                move = 1;
                break;

            case K_LEFT:
                if (cur->kind == WF_CHOICE && cur->sel > 0) {
                    cur->sel--;
                } else if (cur->kind == WF_TEXT && pos > 0) {
                    pos = is_name(cur) != 0 ? name_prev(cur->val, pos) :
                                              pos - 1;
                    first = 0;
                }

                break;

            case K_RIGHT:
                if (cur->kind == WF_CHOICE &&
                    cur->sel + 1 < choice_count(cur)) {
                    cur->sel++;
                } else if (cur->kind == WF_TEXT && pos < len) {
                    pos = is_name(cur) != 0 ? name_next(cur->val, pos) :
                                              pos + 1;
                    first = 0;
                }

                break;

            case K_HOME:
                pos = 0;
                first = 0;
                break;

            case K_END:
                pos = len;
                first = 0;
                break;

            case K_BKSP:
            case K_DEL:
                if (cur->kind != WF_TEXT) {
                    break;
                }

                if (first != 0) {
                    cur->val[0] = '\0';
                    pos = 0;
                } else if (K_BASE(k) == K_BKSP && pos > 0) {
                    int from = is_name(cur) != 0 ? name_prev(cur->val, pos) :
                                                   pos - 1;

                    memmove(cur->val + from, cur->val + pos,
                            usz(len - pos + 1));
                    pos = from;
                } else if (K_BASE(k) == K_DEL && pos < len) {
                    int to = is_name(cur) != 0 ? name_next(cur->val, pos) :
                                                 pos + 1;

                    memmove(cur->val + pos, cur->val + to, usz(len - to + 1));
                }

                first = 0;
                break;

            default:
                if (cur->kind == WF_CHOICE && k == ' ') {
                    int cnt = choice_count(cur);

                    cur->sel = cnt > 0 ? (cur->sel + 1) % cnt : 0;
                } else if (cur->kind == WF_TEXT && k >= 32 && k <= 255 &&
                           (k & ~0xFF) == 0 && len < isz(sizeof cur->val) - 1) {
                    if (first != 0) {
                        cur->val[0] = '\0';
                        len = 0;
                        pos = 0;
                        first = 0;
                    }

                    /* A key of a name is put in as the UTF-8 of the
                       character. */
                    char put[4];
                    int np = is_name(cur) != 0 ? key_utf8(k, put) : 1;

                    put[0] = is_name(cur) != 0 ? put[0] : chr(k);

                    if (len + np < isz(sizeof cur->val)) {
                        memmove(cur->val + pos + np, cur->val + pos,
                                usz(len - pos + 1));
                        memcpy(cur->val + pos, put, usz(np));
                        pos += np;
                    }
                }

                break;
        }

        if (move != 0) {
            int nf = focus;

            do {
                nf += move;
            } while (nf >= 0 && nf < n &&
                     (f[nf].kind == WF_INFO || field_active(f, nf) == 0) &&
                     n > 1);

            if (nf >= 0 && nf < n) {
                focus = nf;
                pos = slen(f[focus].val);
                first = 1;
            }
        }
    }
}


int
popup_form(const char *title, WField *f, int n,
           int (*check)(const WField *f, int n, char *err, int errcap))
{
    return popup_form_nav(title, f, n, check, 0);
}


int
popup_grid(const char *title, const char *const *names, int n, int rows,
           int *sel)
{
    int cols = (n + rows - 1) / rows;
    int cw = 0;

    for (int i = 0; i < n; i++) {
        if (slen(names[i]) > cw) {
            cw = slen(names[i]);
        }
    }

    cw += 3;

    if (*sel < 0 || *sel >= n) {
        *sel = 0;
    }

    for (;;) {
        w_base();
        int C = term_cols();
        int R = term_rows();
        int w = cols * cw + 4;
        int h = rows + 4;
        int x = (C - w) / 2;
        int y = (R - h) / 2;

        box(x, y, w, h, title);
        term_puts(x + (w - 28) / 2, y + h - 1,
                  " Press Ins to Select a Command ", 0);

        for (int i = 0; i < n; i++) {
            int cx = x + 3 + (i / rows) * cw;
            int cy = y + 2 + i % rows;

            term_puts(cx, cy, names[i], i == *sel ? TA_REVERSE : 0);
        }

        term_cursor(0, 0, 0);
        term_present();
        int k = term_getkey(-1);
        if (k == K_F(1)) {
            help_context(help_place(title));
            continue;
        }

        switch (K_BASE(k)) {
            case K_INS:
            case 13:
                return *sel;

            case K_ESC:
                return -1;

            case K_UP:
                *sel = *sel > 0 ? *sel - 1 : n - 1;
                break;

            case K_DOWN:
                *sel = *sel + 1 < n ? *sel + 1 : 0;
                break;

            case K_LEFT:
                if (*sel >= rows) {
                    *sel -= rows;
                }

                break;

            case K_RIGHT:
                if (*sel + rows < n) {
                    *sel += rows;
                }

                break;

            case K_HOME:
                *sel = 0;
                break;

            case K_END:
                *sel = n - 1;
                break;

            default:
                if (k >= 32 && k < 127) {
                    /* The next entry that starts with the letter typed. */
                    for (int s = 1; s <= n; s++) {
                        int i = (*sel + s) % n;
                        const char *nm = names[i];

                        if (nm[0] == '(') {
                            nm++;
                        }

                        if (to_upper(nm[0]) == to_upper(k)) {
                            *sel = i;
                            break;
                        }
                    }
                }

                break;
        }
    }
}


void
popup_msg(const char *title, const char *msg)
{
    w_base();
    int C = term_cols();
    int R = term_rows();
    int w = slen(msg) + 6;

    if (w < 30) {
        w = 30;
    }

    if (w > C - 2) {
        w = C - 2;
    }

    int x = (C - w) / 2;
    int y = R / 2 - 2;

    char shown[512];

    /* A long path is shortened, so that the end of the message is seen. */
    fit_message(msg, w - 6, shown, sizeof shown);
    box(x, y, w, 5, title);
    term_puts(x + 3, y + 2, shown, 0);
    term_puts(x + 3, y + 3, "Press any key", TA_DIM);
    term_cursor(0, 0, 0);
    term_present();
    if (term_getkey(-1) == K_F(1)) {
        help_message(help_place(title), msg);
    }
}


void
popup_notice(const char *title, const char *msg, const char *hint)
{
    w_base();
    int C = term_cols();
    int R = term_rows();
    int w = slen(msg) + 6;

    if (w < 30) {
        w = 30;
    }

    if (w > C - 2) {
        w = C - 2;
    }

    int x = (C - w) / 2;
    int y = R / 2 - 2;
    char shown[512];

    fit_message(msg, w - 6, shown, sizeof shown);
    box(x, y, w, 5, title);
    term_puts(x + 3, y + 2, shown, 0);

    if (hint != NULL) {
        term_puts(x + 3, y + 3, hint, TA_DIM);
    }

    term_cursor(0, 0, 0);
    term_present();
}


void
popup_text(const char *title, const char *text)
{
    char lines[12][96];
    int nl = 0;
    int cols = term_cols();
    int rows = term_rows();
    int w = cols - 6 < 76 ? cols - 6 : 76;
    int x;
    int y;
    int h;
    const char *p = text;

    while (*p != '\0' && nl < 12) {
        int len = 0;
        int brk = -1;

        while (p[len] != '\0' && len < w - 6 && len < 90) {
            if (p[len] == ' ') {
                brk = len;
            }

            len++;
        }

        if (p[len] != '\0' && brk > 0) {
            len = brk;
        }

        memcpy(lines[nl], p, usz(len));
        lines[nl][len] = '\0';
        nl++;
        p += len;

        while (*p == ' ') {
            p++;
        }
    }

    w_base();
    h = nl + 4;
    x = (cols - w) / 2;
    y = (rows - h) / 2;
    box(x, y, w, h, title);

    for (int i = 0; i < nl; i++) {
        term_puts(x + 3, y + 2 + i, lines[i], 0);
    }

    term_puts(x + 3, y + 2 + nl, "Press any key", TA_DIM);
    term_cursor(0, 0, 0);
    term_present();
    if (term_getkey(-1) == K_F(1)) {
        help_message(help_place(title), text);
    }
}


int
popup_confirm(const char *title, const char *text)
{
    char lines[12][96];
    int nl = 0;
    int cols = term_cols();
    int rows = term_rows();
    int w = cols - 6 < 76 ? cols - 6 : 76;
    const char *p = text;

    while (*p != '\0' && nl < 12) {
        int len = 0;
        int brk = -1;

        while (p[len] != '\0' && len < w - 6 && len < 90) {
            if (p[len] == ' ') {
                brk = len;
            }

            len++;
        }

        if (p[len] != '\0' && brk > 0) {
            len = brk;
        }

        memcpy(lines[nl], p, usz(len));
        lines[nl][len] = '\0';
        nl++;
        p += len;

        while (*p == ' ') {
            p++;
        }
    }

    for (;;) {
        int h = nl + 4;
        int x = (cols - w) / 2;
        int y = (rows - h) / 2;

        w_base();
        box(x, y, w, h, title);

        for (int i = 0; i < nl; i++) {
            term_puts(x + 3, y + 2 + i, lines[i], 0);
        }

        term_puts(x + 3, y + 2 + nl, "Y yes   N no   Esc no", TA_DIM);
        term_cursor(0, 0, 0);
        term_present();

        int k = K_BASE(term_getkey(-1));

        if (k == 'y' || k == 'Y') {
            return 1;
        }

        if (k == 'n' || k == 'N' || k == K_ESC) {
            return 0;
        }
    }
}


void
popup_long(const char *title, const char *text)
{
    int C = term_cols();
    int width = C > 24 ? C - 2 : 22;
    int cap = 64;
    int n = 0;
    char **lines;
    const char *p = text;

    if (strchr(text, '\n') == NULL && strlen(text) < 600) {
        popup_text(title, text);

        return;
    }

    lines = malloc(usz(cap) * sizeof *lines);

    while (lines != NULL) {
        const char *e = strchr(p, '\n');
        size_t len = e != NULL ? lsz(e - p) : strlen(p);
        int first = 1;

        /* One line of the text, broken at the spaces; the rest is indented. */
        do {
            int room = first != 0 ? width : width - 4;
            size_t take = len;
            char *l;

            if (take > usz(room)) {
                size_t b = usz(room);

                while (b > usz(room) / 2 && p[b] != ' ') {
                    b--;
                }

                take = p[b] == ' ' && b > usz(room) / 2 ? b : usz(room);
            }

            if (n == cap) {
                char **nl;

                cap *= 2;
                nl = realloc(lines, usz(cap) * sizeof *lines);

                if (nl == NULL) {
                    break;
                }

                lines = nl;
            }

            l = malloc(take + 8);

            if (l == NULL) {
                break;
            }

            {
                size_t ind = first != 0 ? 0 : 4;

                memset(l, ' ', ind);
                memcpy(l + ind, p, take);
                l[ind + take] = '\0';
            }

            lines[n++] = l;
            p += take;
            len -= take;

            while (len > 0 && *p == ' ') {
                p++;
                len--;
            }

            first = 0;
        } while (len > 0);

        if (e == NULL) {
            break;
        }

        p = e + 1;

        if (*p == '\0') {
            break;
        }
    }

    if (n > 0) {
        pager(title, lines, n);
    }

    for (int i = 0; i < n; i++) {
        free(lines[i]);
    }

    free(lines);
}


int
popup_yn(const char *msg)
{
    const char *it[] = {"Yes", "No"};

    return popup_menu(msg, it, 2, 0) == 0;
}


/* paged text viewer */
void
pager(const char *title, char **lines, int n)
{
    int top = 0;

    for (;;) {
        int R = term_rows();
        int C = term_cols();
        int vis = R - 2;

        term_clear();
        term_fill(0, 0, C, 1, ' ', TS_STATUS);
        term_puts(1, 0, title, TS_STATUS);
        char st[48];

        snprintf(st, sizeof st, "line %d/%d", top + 1, n);
        term_puts(C - slen(st) - 1, 0, st, TS_STATUS);

        for (int i = 0; i < vis && top + i < n; i++) {
            const char *s = lines[top + i];

            for (int x = 0; s[x] != '\0' && x < C; x++) {
                term_put(x, 1 + i, u8(s[x]), 0);
            }
        }

        term_fill(0, R - 1, C, 1, ' ', TS_KEYS);
        term_puts(1, R - 1, "Up/Down/PgUp/PgDn scroll   Esc or Enter closes",
                  TS_KEYS);
        term_cursor(0, 0, 0);
        term_present();
        int k = K_BASE(term_getkey(-1));

        if (k == K_ESC || k == 13 || k == 'q' || k == 17) {
            return;
        }

        if (k == K_UP && top > 0) {
            top--;
        } else if (k == K_DOWN && top < n - 1) {
            top++;
        } else if (k == K_PGUP) {
            top -= vis - 1;

            if (top < 0) {
                top = 0;
            }
        } else if (k == K_PGDN || k == ' ') {
            top += vis - 1;

            if (top > n - 1) {
                top = n - 1;
            }
        } else if (k == K_HOME) {
            top = 0;
        } else if (k == K_END) {
            top = n > vis ? n - vis : 0;
        }
    }
}


/* =============================================================== help */
int
help_ctx_topic(const char *title)
{
    return help_find_context(w_help, title);
}


/* The topics that the index lists, sorted by title. */
static int
help_index_order(const Help *h, int *map)
{
    int cnt = 0;

    for (int i = 0; i < h->n; i++) {
        int at = cnt;

        if (h->title[i][0] == '\0') {
            continue;
        }

        while (at > 0 && strcasecmp(h->title[map[at - 1]], h->title[i]) > 0) {
            map[at] = map[at - 1];
            at--;
        }

        map[at] = i;
        cnt++;
    }

    return cnt;
}


/* The links of a page, as the indexes of the spans. */
static int
help_page_links(const HelpPage *pg, int *out)
{
    int n = 0;

    for (int i = 0; i < pg->nspan; i++) {
        if (pg->span[i].topic >= 0) {
            out[n++] = i;
        }
    }

    return n;
}


/*
 * The window of the help: a box across the bottom of the screen, so that the
 * document stays in view above it, and the whole screen when the user zooms.
 */
void
help_viewer(int topic)
{
    Help *h = w_help;

    if (h == NULL) {
        popup_msg("Help", "Help file not found (use --help-file or the "
                          "STRATA_HELP variable)");
        return;
    }

    if (topic < 0 || topic >= h->n) {
        topic = help_find(h, "Help");
    }

    if (topic < 0) {
        topic = 0;
    }

    int stack[64];
    int sp = 0;
    int top = 0;
    int sel = 0;
    int zoom = 0;

    for (;;) {
        int R = term_rows();
        int C = term_cols();
        int H = zoom != 0 ? R : R * 11 / 20;

        if (H < 10) {
            H = R < 10 ? R : 10;
        }

        int vis = H - 3;
        int width = C - 4;
        HelpPage pg;
        int *lk = NULL;
        int nl = 0;

        help_page(h, topic, width, &pg);
        lk = malloc(usz(pg.nspan + 1) * sizeof *lk);
        nl = help_page_links(&pg, lk);

        if (sel >= nl) {
            sel = nl - 1;
        }

        if (sel < 0) {
            sel = 0;
        }

        if (nl != 0) {
            int ln = pg.span[lk[sel]].line;

            if (ln < top) {
                top = ln;
            } else if (ln >= top + vis) {
                top = ln - vis + 1;
            }
        }

        if (top > pg.n - vis) {
            top = pg.n - vis;
        }

        if (top < 0) {
            top = 0;
        }

        char caption[160];

        snprintf(caption, sizeof caption, "HELP: %s", h->title[topic]);
        w_base();
        box(0, R - H, C, H, caption);

        for (int i = 0; i < vis && top + i < pg.n; i++) {
            const char *s = pg.line[top + i];

            for (int x = 0; s[x] != '\0' && x < width; x++) {
                term_put(2 + x, R - H + 1 + i, u8(s[x]), 0);
            }
        }

        for (int i = 0; i < pg.nspan; i++) {
            const HelpSpan *sp2 = &pg.span[i];
            int y = sp2->line - top;
            int attr = TA_BOLD | TA_UNDER;

            if (y < 0 || y >= vis) {
                continue;
            }

            if (sp2->topic == HELP_KEY) {
                attr = TA_KEY;
            } else if (nl != 0 && i == lk[sel]) {
                attr = TA_REVERSE | TA_BOLD | TA_UNDER;
            }

            for (int x = 0; x < sp2->len && sp2->col + x < width; x++) {
                term_put(2 + sp2->col + x, R - H + 1 + y,
                         u8(pg.line[sp2->line][sp2->col + x]), attr);
            }
        }

        term_puts(2, R - 2,
                  C >= 90 ?
                      "Tab: next link  Enter: follow  Backspace: back  "
                      "PgUp/PgDn  I: index  Z: zoom  Esc: close" :
                  C >= 74 ?
                      "Tab: link  Enter: follow  Backspace: back  "
                      "I: index  Z: zoom  Esc: close" :
                      "Tab Enter Backspace I Z Esc",
                  TA_DIM);
        term_cursor(0, 0, 0);
        term_present();

        int k = term_getkey(-1);
        int base = K_BASE(k);
        int done = 0;
        int link_topic = nl != 0 ? pg.span[lk[sel]].topic : -1;

        if (base == K_ESC || base == K_F(1) || base == 'q') {
            done = 1;
        } else if (base == K_UP && top > 0) {
            top--;
        } else if (base == K_DOWN) {
            top++;
        } else if (base == K_HOME) {
            top = 0;
        } else if (base == K_END) {
            top = pg.n;
        } else if (base == K_PGUP) {
            top -= vis - 1;
        } else if (base == K_PGDN || base == ' ') {
            top += vis - 1;
        } else if (base == 9 || base == K_RIGHT) {
            if (nl != 0) {
                sel = (sel + 1) % nl;
            }
        } else if (base == K_BTAB || base == K_LEFT) {
            if (nl != 0) {
                sel = (sel + nl - 1) % nl;
            }
        } else if (base == 13 && link_topic >= 0) {
            if (sp < 63) {
                stack[sp++] = topic;
            }

            topic = link_topic;
            top = 0;
            sel = 0;
        } else if (base == K_BKSP || base == 8) {
            if (sp > 0) {
                topic = stack[--sp];
                top = 0;
                sel = 0;
            } else {
                done = 1;
            }
        } else if (base == 'z' || base == 'Z') {
            zoom = zoom == 0;
        } else if (base == 'i' || base == 'I') {
            int *map = malloc(usz(h->n + 1) * sizeof *map);
            const char **items = malloc(usz(h->n + 1) * sizeof *items);
            int cnt = help_index_order(h, map);

            for (int i = 0; i < cnt; i++) {
                items[i] = h->title[map[i]];
            }

            int c = popup_menu("Help Index", items, cnt, 0);

            if (c >= 0) {
                if (sp < 63) {
                    stack[sp++] = topic;
                }

                topic = map[c];
                top = 0;
                sel = 0;
            }

            free(items);
            free(map);
        }

        free(lk);
        help_page_free(&pg);

        if (done != 0) {
            return;
        }
    }
}


void
help_context(const char *title)
{
    const char *saved = help_name;

    help_name = NULL;
    help_viewer(help_ctx_topic(title));
    help_name = saved;
}


void
help_message(const char *title, const char *msg)
{
    const char *saved = help_name;
    int t = help_find_message(w_help, msg);

    if (t < 0) {
        t = help_find_context(w_help, title);
    }

    help_name = NULL;
    help_viewer(t);
    help_name = saved;
}
