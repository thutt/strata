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

/* comparemenu.c - the panel of Compare Document Versions, see comparemenu.h. */
#include "util.h"
#include "log.h"
#include "backup.h"
#include "msdoc.h"
#include "compare.h"
#include "comparemenu.h"
#include "widgets.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TITLE "Compare Document Versions"

enum {
    F_DIR,
    F_FIRST,
    F_SECOND,
    F_REPORT,
    F_COUNT
};


static int
file_exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}


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


/*
 * The newest backup file of a document, when there is one: backup 1 (see
 * backup.h), in the folder .backup next to the document, or else the old
 * file beside it that earlier versions made.  Returns 1 and the name (the
 * folder and the file, relative to the directory of the document when the
 * name has none) in out.
 */
static int
backup_of(const char *dir, const char *name, char *out, size_t cap)
{
    char full[PATH_MAX];
    char bak[PATH_MAX];

    if (full_path(dir, name, full, sizeof full) == 0) {
        return 0;
    }

    if (backup_name(full, 1, bak, sizeof bak) == 0 && file_exists(bak) != 0) {
        return backup_name(name, 1, out, cap) == 0;
    }

    if (backup_old_name(full, 1, bak, sizeof bak) == 0 &&
        file_exists(bak) != 0) {
        return backup_old_name(name, 1, out, cap) == 0;
    }

    return 0;
}


/* The name of the report that is offered: next to the first document. */
static void
report_name(const char *first, char *out, size_t cap)
{
    size_t n = strlen(first);

    if (n >= 4 && strcasecmp(first + n - 4, ".doc") == 0) {
        n -= 4;
    }

    /* No name of a file may be longer than 255 bytes: a long name is cut
       between two characters to leave room for -diff.doc. */
    const char *sl = strrchr(first, '/');
    size_t start = sl != NULL ? strlen(first) - strlen(sl + 1) : 0;

    if (n > start + 246) {
        n = start + 246;

        while (n > start && (u8(first[n]) & 0xC0) == 0x80) {
            n--;
        }
    }

    if (n + 10 > cap) {
        out[0] = '\0';

        return;
    }

    memcpy(out, first, n);
    memcpy(out + n, "-diff.doc", 10);
}


/* The names are checked before the panel is accepted. */
static int
check_panel(const WField *f, int n, char *err, int errcap)
{
    char a[PATH_MAX];
    char b[PATH_MAX];
    char r[PATH_MAX];

    if (n < F_COUNT) {
        return 0;
    }

    if (full_path(f[F_DIR].val, f[F_FIRST].val, a, sizeof a) == 0 ||
        full_path(f[F_DIR].val, f[F_SECOND].val, b, sizeof b) == 0) {
        snprintf(err, usz(errcap), "Enter the names of the two documents");

        return 0;
    }

    if (file_exists(a) == 0) {
        snprintf(err, usz(errcap), "Cannot find %s", a);

        return 0;
    }

    if (file_exists(b) == 0) {
        snprintf(err, usz(errcap), "Cannot find %s", b);

        return 0;
    }

    if (strcmp(a, b) == 0) {
        snprintf(err, usz(errcap), "Choose two different documents");

        return 0;
    }

    if (full_path(f[F_DIR].val, f[F_REPORT].val, r, sizeof r) != 0 &&
        (strcmp(r, a) == 0 || strcmp(r, b) == 0)) {
        snprintf(err, usz(errcap),
                 "The report must not replace one of the two documents");

        return 0;
    }

    return 1;
}


static void
save_report(const CmpReport *rep, const char *path)
{
    char err[256] = "";
    char msg[PATH_MAX + 320];
    Doc *d;

    if (file_exists(path) != 0) {
        snprintf(msg, sizeof msg, "%s exists.  Replace it?", path);

        if (popup_yn(msg) == 0) {
            return;
        }
    }

    d = cmp_report_doc(rep);

    if (doc_save(d, path, err, sizeof err) != 0) {
        snprintf(msg, sizeof msg, "Cannot write %s: %s", path, err);
        popup_msg(TITLE, msg);
    } else {
        snprintf(msg, sizeof msg, "Wrote %s", path);
        popup_msg(TITLE, msg);
    }

    doc_free(d);
}


static void
compare_files(const char *pa, const char *pb, const char *report)
{
    char err[256] = "";
    char msg[PATH_MAX + 320];
    Doc *a = doc_load(pa, err, sizeof err);
    Doc *b;
    CmpReport rep;

    if (a == NULL) {
        snprintf(msg, sizeof msg, "Cannot read %s: %s", pa, err);
        popup_msg(TITLE, msg);

        return;
    }

    b = doc_load(pb, err, sizeof err);

    if (b == NULL) {
        snprintf(msg, sizeof msg, "Cannot read %s: %s", pb, err);
        popup_msg(TITLE, msg);
        doc_free(a);

        return;
    }

    cmp_compare(a, b, pa, pb, &rep);
    doc_free(a);
    doc_free(b);
    w_set_help("Comparison Report");
    pager("Comparison of two documents", rep.lines, rep.nlines);
    w_set_help(NULL);

    if (cmp_same(&rep) == 0 &&
        popup_yn("Save the comparison as a document?") != 0) {
        save_report(&rep, report);
    }

    cmp_free(&rep);
}


void
compare_panel(const char *workdir, const char *first)
{
    WField f[F_COUNT];
    char second[256] = "";
    char rname[256] = "";

    memset(f, 0, sizeof f);
    f[F_DIR].label = "Working Directory";
    f[F_DIR].kind = WF_TEXT;
    f[F_DIR].pick = PICK_DIR;
    str_copy(f[F_DIR].val, sizeof f[F_DIR].val, workdir);
    f[F_FIRST].label = "First Document";
    f[F_FIRST].kind = WF_TEXT;
    f[F_FIRST].pick = PICK_FILE;
    f[F_FIRST].suffixes = ".doc .set .bak";
    str_copy(f[F_FIRST].val, sizeof f[F_FIRST].val, first);
    f[F_SECOND].label = "Second Document";
    f[F_SECOND].kind = WF_TEXT;
    f[F_SECOND].pick = PICK_FILE;
    f[F_SECOND].suffixes = ".doc .set .bak";
    f[F_REPORT].label = "Report Document";
    f[F_REPORT].kind = WF_TEXT;

    if (first[0] != '\0') {
        if (backup_of(workdir, first, second, sizeof second) != 0) {
            str_copy(f[F_SECOND].val, sizeof f[F_SECOND].val, second);
        }

        report_name(first, rname, sizeof rname);
        str_copy(f[F_REPORT].val, sizeof f[F_REPORT].val, rname);
    }

    for (;;) {
        char pa[PATH_MAX];
        char pb[PATH_MAX];
        char pr[PATH_MAX];

        if (popup_form(TITLE, f, F_COUNT, check_panel) == 0) {
            return;
        }

        full_path(f[F_DIR].val, f[F_FIRST].val, pa, sizeof pa);
        full_path(f[F_DIR].val, f[F_SECOND].val, pb, sizeof pb);

        if (f[F_REPORT].val[0] == '\0') {
            report_name(f[F_FIRST].val, rname, sizeof rname);
            str_copy(f[F_REPORT].val, sizeof f[F_REPORT].val, rname);
        }

        full_path(f[F_DIR].val, f[F_REPORT].val, pr, sizeof pr);

        if (strcmp(pr, pa) == 0 || strcmp(pr, pb) == 0) {
            popup_msg(TITLE,
                      "The report must not replace one of the two documents");
            continue;
        }

        compare_files(pa, pb, pr);
    }
}
