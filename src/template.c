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
 * template.c - the list of templates and the question that chooses one.
 */
#include "template.h"
#include "util.h"
#include "config.h"
#include "widgets.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>


/* Does the name end in .doc or .set (any case)?  Gives the length of the stem. */
static int
stem_len(const char *name)
{
    size_t n = strlen(name);

    if (n > 4 && (strcasecmp(name + n - 4, ".doc") == 0 ||
                  strcasecmp(name + n - 4, ".set") == 0)) {
        return (int)n - 4;
    }

    return 0;
}


static int
by_name(const void *a, const void *b)
{
    return strcasecmp(((const Template *)a)->name, ((const Template *)b)->name);
}


/*
 * Put the folder b below the folder a in dst (capacity cap, truncating): a
 * and b with a slash between them, or b alone when a is empty.
 */
static void
join_folder(char *dst, size_t cap, const char *a, const char *b)
{
    size_t used;

    if (a[0] == '\0') {
        str_copy(dst, cap, b);

        return;
    }

    str_copy(dst, cap, a);
    used = strlen(dst);

    if (used + 1 < cap) {
        dst[used++] = '/';
        dst[used] = '\0';
        str_copy(dst + used, cap - used, b);
    }
}


/*
 * Put the templates of dir and of the folders below it in v (count of them
 * already, at most cap).  folder is the path of dir below the template place
 * (empty for the place itself); first is the number of the entries that the
 * earlier places made: a name of those hides the same name here.  At most
 * depth more folders are looked into.
 */
static int
scan_folder(const char *dir, const char *folder, int depth, int first,
            Template *v, int count, int cap)
{
    DIR *dp = opendir(dir);
    struct dirent *de;
    char *subs[TEMPLATE_FOLDERS_MAX];
    int nsub = 0;

    if (dp == NULL) {
        return count;
    }

    while ((de = readdir(dp)) != NULL && count < cap) {
        int stem = stem_len(de->d_name);
        Template t;
        int hidden = 0;
        char full[PATH_MAX];

        if (de->d_name[0] == '.') {
            continue;
        }

        path_join(full, sizeof full, dir, de->d_name);

        if (stem <= 0) {
            struct stat st;

            if (depth > 0 && nsub < TEMPLATE_FOLDERS_MAX &&
                stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
                char *copy = strdup(de->d_name);

                if (copy != NULL) {
                    subs[nsub++] = copy;
                }
            }

            continue;
        }

        if (stem >= (int)sizeof t.name) {
            continue;
        }

        memset(&t, 0, sizeof t);
        memcpy(t.name, de->d_name, (size_t)stem);
        str_copy(t.folder, sizeof t.folder, folder);
        str_copy(t.path, sizeof t.path, full);

        if (access(t.path, R_OK) != 0) {
            continue;
        }

        /* A file that does not begin as a document is not a template. */
        {
            FILE *fp = fopen(t.path, "rb");
            char magic[2] = {0, 0};

            if (fp == NULL || fread(magic, 1, 2, fp) != 2 ||
                memcmp(magic, "*R", 2) != 0) {
                if (fp != NULL) {
                    fclose(fp);
                }

                continue;
            }

            fclose(fp);
        }

        /* The same name in an earlier place hides this one. */
        for (int i = 0; i < first; i++) {
            if (strcasecmp(v[i].name, t.name) == 0) {
                hidden = 1;
                break;
            }
        }

        if (hidden != 0) {
            continue;
        }

        (void)doc_peek_description(t.path, t.desc, sizeof t.desc);
        v[count++] = t;
    }

    closedir(dp);

    for (int i = 0; i < nsub; i++) {
        char sub[PATH_MAX];
        char subfolder[TEMPLATE_FOLDER_MAX];

        path_join(sub, sizeof sub, dir, subs[i]);

        join_folder(subfolder, sizeof subfolder, folder, subs[i]);

        count = scan_folder(sub, subfolder, depth - 1, first, v, count, cap);
        free(subs[i]);
    }

    return count;
}


int
template_scan(const char (*dirs)[PATH_MAX], int n, Template *v, int cap)
{
    int count = 0;

    for (int d = 0; d < n; d++) {
        count = scan_folder(dirs[d], "", TEMPLATE_DEPTH, count, v, count, cap);
    }

    qsort(v, usz(count), sizeof v[0], by_name);

    return count;
}


int
template_list(Template *v, int cap)
{
    char dirs[4][PATH_MAX];
    int n = template_dirs(dirs, 4);

    return template_scan((const char (*)[PATH_MAX])dirs, n, v, cap);
}


/*
 * Does the folder f lie in the folder cur (or is it cur itself)?  Gives 0
 * for no, 1 for f is cur, and 2 for a folder below cur; for 2 the first name
 * of the rest of f is put in next.
 */
static int
folder_in(const char *f, const char *cur, char *next, size_t cap)
{
    size_t cl = strlen(cur);
    const char *rest;

    if (cl == 0) {
        rest = f;
    } else if (strncasecmp(f, cur, cl) == 0 && f[cl] == '/') {
        rest = f + cl + 1;
    } else if (strcasecmp(f, cur) == 0) {
        return 1;
    } else {
        return 0;
    }

    if (*rest == '\0') {
        return 1;
    }

    (void)str_copy_to(next, cap, rest, '/');

    return 2;
}


/* The number of templates in the folder cur and below it. */
static int
folder_count(const Template *v, int n, const char *cur, const char *name)
{
    char path[sizeof v->folder];
    char next[sizeof v->folder];
    int count = 0;

    join_folder(path, sizeof path, cur, name);

    for (int i = 0; i < n; i++) {
        if (folder_in(v[i].folder, path, next, sizeof next) != 0) {
            count++;
        }
    }

    return count;
}


/*
 * A list of the templates v (n of them), one folder at a time: the folders
 * of the current folder first, with the number of templates in each, then
 * its templates.  Enter on a folder opens it; Left or Backspace goes up one
 * folder; Esc ends.  At the top the built-in settings come first when
 * builtin is 1 and the entry for any other file comes last when another is
 * 1.  Returns -1 for Esc, the number of a built-in entry (0 or 1), the
 * number of a template plus the number of built-in entries, or -3 for the
 * entry for any other file.
 */
static int
template_navigate(const char *title, const Template *v, int n, int builtin,
                  int another)
{
    static const char *const bname[] = {"Structured", "Unstructured"};
    static const char *const bdesc[] = {
        "The built-in settings: a document with sections and headlines.",
        "The built-in settings: a document of blocks with no sections."};
    int cap = 2 * n + 8;
    const char **names = calloc(usz(cap), sizeof *names);
    const char **descs = calloc(usz(cap), sizeof *descs);
    int *what = calloc(usz(cap), sizeof *what);
    char (*fname)[sizeof v->folder + 2] = calloc(usz(cap), sizeof *fname);
    char (*fdesc)[100] = calloc(usz(cap), sizeof *fdesc);
    char cur[sizeof v->folder] = "";
    char came[sizeof v->folder] = "";
    char typed[sizeof widgets_list_find] = "";
    int target = -1;
    int result = -1;

    if (names == NULL || descs == NULL || what == NULL || fname == NULL ||
        fdesc == NULL) {
        goto out;
    }

    for (;;) {
        int rows = 0;
        int nfold = 0;
        int sel = 0;
        char ttl[sizeof v->folder + 40];
        int r;

        /* The built-in settings are at the top only. */
        if (cur[0] == '\0' && builtin != 0) {
            for (int i = 0; i < 2; i++) {
                names[rows] = bname[i];
                descs[rows] = bdesc[i];
                what[rows++] = i;
            }
        }

        /* The folders of the current folder, each once, in the order of
           the sorted names of the templates in them. */
        for (int i = 0; i < n; i++) {
            char next[sizeof v->folder];
            int dup = 0;

            if (folder_in(v[i].folder, cur, next, sizeof next) != 2) {
                continue;
            }

            for (int j = 0; j < nfold; j++) {
                char shown[sizeof v->folder + 2];

                snprintf(shown, sizeof shown, "%s/", next);

                if (strcasecmp(fname[rows - nfold + j], shown) == 0) {
                    dup = 1;
                    break;
                }
            }

            if (dup != 0) {
                continue;
            }

            snprintf(fname[rows], sizeof fname[rows], "%s/", next);
            snprintf(fdesc[rows], sizeof fdesc[rows],
                     "A subdirectory of %d templates.  Enter opens it.",
                     folder_count(v, n, cur, next));
            names[rows] = fname[rows];
            descs[rows] = fdesc[rows];
            what[rows++] = -10;
            nfold++;
        }

        /* The templates of the current folder. */
        for (int i = 0; i < n; i++) {
            char next[sizeof v->folder];

            if (folder_in(v[i].folder, cur, next, sizeof next) != 1) {
                continue;
            }

            names[rows] = v[i].name;
            descs[rows] = v[i].desc[0] != '\0' ? v[i].desc :
                                                  "(no description)";
            what[rows++] = i + (builtin != 0 ? 2 : 0);
        }

        if (cur[0] == '\0' && another != 0) {
            names[rows] = "Another file...";
            descs[rows] = "Look for a template file in the folders: any .doc "
                          "or .set file that is a document.";
            what[rows++] = -3;
        }

        /* Back from a folder, the cursor is on the folder just left. */
        for (int i = 0; i < rows; i++) {
            if (came[0] != '\0' && what[i] == -10) {
                char shown[sizeof v->folder + 2];

                snprintf(shown, sizeof shown, "%s/", came);

                if (strcasecmp(names[i], shown) == 0) {
                    sel = i;
                }
            }
        }

        came[0] = '\0';

        /* The cursor is on the template that a typed text found in another
           folder, or on the first entry that has the typed text. */
        for (int i = 0; i < rows; i++) {
            size_t tl = strlen(typed);
            int has = typed[0] != '\0' &&
                      (list_word_has(names[i], typed, tl) != 0 ||
                       list_word_has(descs[i], typed, tl) != 0);

            if (target >= 0 ? what[i] == target + (builtin != 0 ? 2 : 0) :
                              has != 0 && sel == 0) {
                sel = i;
                break;
            }
        }

        target = -1;
        str_copy(widgets_list_find, sizeof widgets_list_find, typed);
        typed[0] = '\0';

        if (cur[0] == '\0') {
            str_copy(ttl, sizeof ttl, title);
        } else {
            snprintf(ttl, sizeof ttl, "%s: %s", title, cur);
        }

        r = popup_list_desc_up(ttl, names, descs, rows, sel);

        if (r == POPUP_LIST_UP) {
            char *sl = strrchr(cur, '/');

            if (cur[0] != '\0') {
                if (sl != NULL) {
                    str_copy(came, sizeof came, sl + 1);
                    *sl = '\0';
                } else {
                    str_copy(came, sizeof came, cur);
                    cur[0] = '\0';
                }
            }

            continue;
        }

        if (r == POPUP_LIST_FIND) {
            /* The typed text is in no entry of this folder: the first
               template of any folder that has it, and that folder. */
            size_t len;

            str_copy(typed, sizeof typed, widgets_list_find);
            len = strlen(typed);

            for (int i = 0; i < n && target < 0; i++) {
                if (list_word_has(v[i].name, typed, len) != 0 ||
                    list_word_has(v[i].desc, typed, len) != 0) {
                    target = i;
                }
            }

            if (target >= 0) {
                str_copy(cur, sizeof cur, v[target].folder);
            } else {
                typed[len - 1] = '\0';
            }

            continue;
        }

        if (r < 0) {
            result = -1;
            break;
        }

        if (what[r] == -10) {
            char sub[sizeof v->folder];
            char next_cur[sizeof v->folder];

            (void)str_copy_to(sub, sizeof sub, names[r], '/');

            join_folder(next_cur, sizeof next_cur, cur, sub);
            str_copy(cur, sizeof cur, next_cur);

            continue;
        }

        result = what[r];
        break;
    }

out:
    free(names);
    free(descs);
    free(what);
    free(fname);
    free(fdesc);

    return result;
}


int
template_ask(const Template *v, int n, int builtin)
{
    return template_navigate("Templates", v, n, builtin, 0);
}


void
template_pick(char *val, size_t cap, int arg)
{
    Template *tl = calloc(TEMPLATE_MAX, sizeof *tl);
    int n = tl != NULL ? template_list(tl, TEMPLATE_MAX) : 0;
    int c;

    (void)arg;

    if (tl == NULL) {
        return;
    }

    /* With no template in any place the list would hold only the built-in
       settings: say where the templates are looked for. */
    if (n == 0) {
        char dirs[4][PATH_MAX];
        char msg[4 * PATH_MAX + 120];
        int nd = template_dirs(dirs, 4);

        snprintf(msg, sizeof msg,
                 "No templates found; they are looked for in:");

        for (int i = 0; i < nd; i++) {
            size_t used = strlen(msg);

            str_copy(msg + used, sizeof msg - used, i == 0 ? " " : "; ");
            used = strlen(msg);
            str_copy(msg + used, sizeof msg - used, dirs[i]);
        }

        popup_text("Templates", msg);
    }

    c = template_navigate("Template", tl, n, 1, 1);

    if (c == 0) {
        str_copy(val, cap, "Structured");
    } else if (c == 1) {
        str_copy(val, cap, "Unstructured");
    } else if (c >= 2) {
        str_copy(val, cap, tl[c - 2].path);
    } else if (c == -3) {
        char start[PATH_MAX] = "";
        char dir[PATH_MAX];
        char name[PATH_MAX];
        char *sl;

        if (strchr(val, '/') != NULL) {
            str_copy(start, sizeof start, val);
            sl = strrchr(start, '/');
            *sl = '\0';

            if (start[0] == '\0') {
                str_copy(start, sizeof start, "/");
            }
        }

        if (popup_browse(start, 0, ".doc .set", dir, sizeof dir, name,
                         sizeof name) != 0) {
            path_join(val, cap, dir, name);
        }

        /* The browser is another list: it does not finish the panel. */
        widgets_list_ins = 0;
    }

    free(tl);
}
