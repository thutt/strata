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


int
template_scan(const char (*dirs)[PATH_MAX], int n, Template *v, int cap)
{
    int count = 0;

    for (int d = 0; d < n; d++) {
        DIR *dp = opendir(dirs[d]);
        struct dirent *de;
        int first = count;

        if (dp == NULL) {
            continue;
        }

        while ((de = readdir(dp)) != NULL && count < cap) {
            int stem = stem_len(de->d_name);
            Template t;
            int hidden = 0;

            if (stem <= 0 || stem >= (int)sizeof t.name) {
                continue;
            }

            memset(&t, 0, sizeof t);
            memcpy(t.name, de->d_name, (size_t)stem);
            path_join(t.path, sizeof t.path, dirs[d], de->d_name);

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

            /* The same name in an earlier folder hides this one. */
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


int
template_ask(const Template *v, int n, int builtin)
{
    static const char *const bname[] = {"Structured", "Unstructured"};
    static const char *const bdesc[] = {
        "The built-in settings: a document with sections and headlines.",
        "The built-in settings: a document of blocks with no sections."};
    int total = n + (builtin != 0 ? 2 : 0);
    const char **names = calloc(usz(total) + 1, sizeof *names);
    const char **descs = calloc(usz(total) + 1, sizeof *descs);
    int r;

    for (int i = 0; i < total; i++) {
        int j = i - (builtin != 0 ? 2 : 0);

        names[i] = j < 0 ? bname[i] : v[j].name;
        descs[i] = j < 0 ? bdesc[i] : v[j].desc[0] != '\0' ? v[j].desc :
                                          "(no description)";
    }

    r = popup_list_desc("Templates", names, descs, total, 0);
    free(names);
    free(descs);

    return r;
}


void
template_pick(char *val, size_t cap, int arg)
{
    static const char *const bdesc[] = {
        "The built-in settings: a document with sections and headlines.",
        "The built-in settings: a document of blocks with no sections."};
    Template *tl = calloc(TEMPLATE_MAX, sizeof *tl);
    int n = tl != NULL ? template_list(tl, TEMPLATE_MAX) : 0;
    int total = n + 3;
    const char **names = calloc(usz(total) + 1, sizeof *names);
    const char **descs = calloc(usz(total) + 1, sizeof *descs);
    int c;

    (void)arg;

    if (tl == NULL || names == NULL || descs == NULL) {
        free(tl);
        free(names);
        free(descs);

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

    names[0] = "Structured";
    names[1] = "Unstructured";
    descs[0] = bdesc[0];
    descs[1] = bdesc[1];

    for (int i = 0; i < n; i++) {
        names[i + 2] = tl[i].name;
        descs[i + 2] = tl[i].desc[0] != '\0' ? tl[i].desc :
                                                "(no description)";
    }

    names[total - 1] = "Another file...";
    descs[total - 1] = "Look for a template file in the folders: any .doc or "
                       ".set file that is a document.";
    c = popup_list_desc("Template", names, descs, total, 0);

    if (c == 0 || c == 1) {
        str_copy(val, cap, names[c]);
    } else if (c >= 2 && c < total - 1) {
        str_copy(val, cap, tl[c - 2].path);
    } else if (c == total - 1) {
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
    free(names);
    free(descs);
}
