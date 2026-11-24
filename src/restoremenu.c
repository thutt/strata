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

/* restoremenu.c - the panel Restore Backup, see restoremenu.h. */
#include "util.h"
#include "backup.h"
#include "msdoc.h"
#include "restoremenu.h"
#include "widgets.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define TITLE "Restore Backup"

enum {
    F_DIR,
    F_DOC,
    F_COUNT
};


/*
 * The full path of a name that the user typed: a name that begins with a
 * slash is taken as it is, any other name is in the working directory.  A
 * name without an extension gets .doc.  Returns 0 when there is no name.
 */
static int
full_path(const char *dir, const char *name, char *out, size_t cap)
{
    if (name[0] == '\0') {
        return 0;
    }

    if (name[0] == '/' || dir[0] == '\0') {
        snprintf(out, cap, "%s", name);
    } else {
        snprintf(out, cap, "%s/%s", dir, name);
    }

    doc_name_complete(out, cap);

    return 1;
}


/* The name of the file without its directory. */
static const char *
file_part(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash != NULL ? slash + 1 : path;
}


/* The words that name a backup: "backup 02", or "special backup V2". */
static void
backup_words(const BackupInfo *b, char *out, size_t cap)
{
    if (b->old == 2) {
        snprintf(out, cap, "special backup V%d", b->special);
    } else {
        snprintf(out, cap, "backup %02d", b->n);
    }
}


/* The panel is checked before it is accepted: there must be a name. */
static int
check_panel(const WField *f, int n, char *err, int errcap)
{
    if (n < F_COUNT) {
        return 0;
    }

    if (f[F_DOC].val[0] == '\0') {
        snprintf(err, usz(errcap), "Enter the name of a document");

        return 0;
    }

    return 1;
}


/* The words of the first block of a backup, to tell it from the others. */
static void
first_words(const char *path, char *out, size_t cap)
{
    char err[160];
    Doc *d = doc_load(path, err, sizeof err);

    out[0] = '\0';

    if (d == NULL) {
        return;
    }

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == IT_BLOCK && d->items[i].blk->ncols > 0) {
            char buf[200];

            text_plain(&d->items[i].blk->cols[0].text, buf, sizeof buf);

            if (buf[0] != '\0') {
                str_copy(out, cap, buf);
                break;
            }
        }
    }

    doc_free(d);
}


/* The date and time of a backup, as the list shows them. */
static void
stamp_text(time_t t, char *out, size_t cap)
{
    struct tm tmv;

    if (localtime_r(&t, &tmv) == NULL ||
        strftime(out, cap, "%d %b %Y %H:%M", &tmv) == 0) {
        str_copy(out, cap, "?");
    }
}


/* The lines of the text of a backup for the pager: block by block. */
static char **
view_lines(const char *path, int *n)
{
    char err[160];
    Doc *d = doc_load(path, err, sizeof err);
    char **v = NULL;
    int cap = 0;

    *n = 0;

    if (d == NULL) {
        return NULL;
    }

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        const Block *b = d->items[i].blk;

        for (int c = 0; c < b->ncols; c++) {
            char buf[4096];
            const char *p;

            text_plain(&b->cols[c].text, buf, sizeof buf);
            p = buf;

            do {
                size_t len = strlen(p);

                if (len > 76) {
                    size_t brk = 76;

                    while (brk > 40 && p[brk] != ' ') {
                        brk--;
                    }

                    len = p[brk] == ' ' ? brk : 76;
                }

                if (*n == cap) {
                    cap = cap != 0 ? cap * 2 : 64;
                    v = realloc(v, usz(cap) * sizeof *v);
                }

                v[*n] = malloc(len + 1);
                memcpy(v[*n], p, len);
                v[*n][len] = '\0';
                (*n)++;
                p += len;

                while (*p == ' ') {
                    p++;
                }
            } while (*p != '\0');
        }

        if (*n == cap) {
            cap = cap != 0 ? cap * 2 : 64;
            v = realloc(v, usz(cap) * sizeof *v);
        }

        v[*n] = strdup("");
        (*n)++;
    }

    doc_free(d);

    return v;
}


/* Show the text of a backup in the pager. */
static void
view_backup(const BackupInfo *b)
{
    int n;
    char **lines = view_lines(b->path, &n);
    char title[160];

    if (lines == NULL || n == 0) {
        popup_msg(TITLE, "The backup has no text to show");
        free(lines);

        return;
    }

    if (b->old == 2) {
        snprintf(title, sizeof title, "Special backup V%d: %s", b->special,
                 file_part(b->path));
    } else {
        snprintf(title, sizeof title, "Backup %02d: %s", b->n,
                 file_part(b->path));
    }
    pager(title, lines, n);

    for (int i = 0; i < n; i++) {
        free(lines[i]);
    }

    free(lines);
}


/* Ask, restore, and say what happened.  Returns 1 when the files changed. */
static int
restore_one(const char *doc, const BackupInfo *b, int depth)
{
    char stamp[48];
    char text[1100];
    char err[600] = "";
    int same = 0;
    char msg[PATH_MAX + 700];

    stamp_text(b->when, stamp, sizeof stamp);
    char words[48];

    backup_words(b, words, sizeof words);

    if (b->old == 2) {
        snprintf(text, sizeof text,
                 "Restore %s of %s over %s?  The present %s becomes backup 1 "
                 "and the backups move up by one.  The special backup stays "
                 "as it is.  If the document is open in an Editor, that "
                 "Editor keeps the old text until it loads the document "
                 "again.", words, stamp, file_part(doc), file_part(doc));
    } else {
        snprintf(text, sizeof text,
                 "Restore backup %02d of %s over %s?  The present %s becomes "
                 "backup 1 and the backups below %02d move up by one.  If "
                 "the document is open in an Editor, that Editor keeps the "
                 "old text until it loads the document again.",
                 b->n, stamp, file_part(doc), file_part(doc), b->n);
    }

    if (popup_confirm(TITLE, text) == 0) {
        return 0;
    }

    if (backup_restore(doc, b, depth, &same, err, sizeof err) != 0) {
        popup_msg(TITLE, err);

        return 0;
    }

    if (same != 0) {
        snprintf(msg, sizeof msg, "%s holds the same text as %s: nothing "
                 "was done", words, file_part(doc));
        msg[0] = (char)toupper((unsigned char)msg[0]);
        popup_msg(TITLE, msg);

        return 0;
    }

    snprintf(msg, sizeof msg, "Restored %s over %s.  The document that was "
             "there is backup 1.", words, file_part(doc));
    popup_msg(TITLE, msg);

    return 1;
}


/* The list of the backups of one document; returns when the user leaves. */
static void
list_backups(const char *doc, int depth)
{
    static BackupInfo v[2 * BACKUP_MAX];
    int sel = 0;

    for (;;) {
        int n = backup_list(doc, v, 2 * BACKUP_MAX);
        static char label[2 * BACKUP_MAX][160];
        const char *items[2 * BACKUP_MAX];
        char head[PATH_MAX + 40];

        if (n == 0) {
            snprintf(head, sizeof head, "%s has no backup", file_part(doc));
            popup_msg(TITLE, head);

            return;
        }

        for (int i = 0; i < n; i++) {
            char stamp[48];
            char words[48];

            stamp_text(v[i].when, stamp, sizeof stamp);
            first_words(v[i].path, words, sizeof words);
            if (v[i].special != 0 || v[i].old == 2) {
                snprintf(label[i], sizeof label[i],
                         "V%d  %s  %9lld bytes  %s", v[i].special, stamp,
                         v[i].size, words);
            } else {
                snprintf(label[i], sizeof label[i],
                         "%02d%s  %s  %9lld bytes  %s", v[i].n,
                         v[i].old != 0 ? " old" : "    ", stamp, v[i].size,
                         words);
            }
            items[i] = label[i];
        }

        snprintf(head, sizeof head, "Backups of %s", file_part(doc));

        int c = popup_menu(head, items, n, sel);

        if (c < 0) {
            return;
        }

        sel = c;

        for (;;) {
            static const char *acts[] = {"View the text",
                                               "Restore this backup", "Back"};
            char title[64];

            if (v[c].old == 2) {
                snprintf(title, sizeof title, "Special backup V%d",
                         v[c].special);
            } else {
                snprintf(title, sizeof title, "Backup %02d%s", v[c].n,
                         v[c].old != 0 ? " (old)" : "");
            }

            int a = popup_menu(title, acts, 3, 0);

            if (a == 0) {
                view_backup(&v[c]);
            } else if (a == 1) {
                if (restore_one(doc, &v[c], depth) != 0) {
                    sel = 0;
                }

                break;
            } else {
                break;
            }
        }
    }
}


void
restore_panel(const char *workdir, const char *first, int depth)
{
    WField f[F_COUNT];

    memset(f, 0, sizeof f);
    f[F_DIR].label = "Working Directory";
    f[F_DIR].kind = WF_TEXT;
    f[F_DIR].pick = PICK_DIR;
    str_copy(f[F_DIR].val, sizeof f[F_DIR].val, workdir);
    f[F_DOC].label = "Document Name";
    f[F_DOC].kind = WF_TEXT;
    f[F_DOC].pick = PICK_FILE;
    f[F_DOC].suffixes = ".doc .set";
    str_copy(f[F_DOC].val, sizeof f[F_DOC].val, first);

    w_set_help(TITLE);

    for (;;) {
        char path[PATH_MAX];

        if (popup_form(TITLE, f, F_COUNT, check_panel) == 0) {
            break;
        }

        full_path(f[F_DIR].val, f[F_DOC].val, path, sizeof path);
        list_backups(path, depth);
    }

    w_set_help(NULL);
}
