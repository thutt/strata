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
 * tplfind.c - the places of the templates, and the search for a template by
 * name.  It needs nothing of the screen, so the programs that read documents
 * and the tests can use it.
 */
#include "template.h"
#include "util.h"
#include <dirent.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>


/* The folder of the settings: STRATA_HOME, else .strata in the home. */
static int
settings_folder(char *out, size_t cap)
{
    const char *env = getenv("STRATA_HOME");
    const char *home;

    if (env != NULL && env[0] != '\0') {
        str_copy(out, cap, env);

        return 0;
    }

    home = getenv("HOME");

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


int
template_dirs(char (*dirs)[PATH_MAX], int cap)
{
    int n = 0;
    const char *env = getenv("STRATA_TEMPLATES");
    char cfg[PATH_MAX];
    char self[PATH_MAX];
    ssize_t k;

    if (env != NULL && env[0] != '\0' && n < cap) {
        str_copy(dirs[n++], PATH_MAX, env);
    }

    if (settings_folder(cfg, sizeof cfg) == 0 && n < cap) {
        path_join(dirs[n++], PATH_MAX, cfg, "templates");
    }

    k = readlink("/proc/self/exe", self, sizeof self - 20);

    if (k > 0 && n < cap) {
        char *sl;

        self[k] = '\0';
        sl = strrchr(self, '/');

        if (sl != NULL) {
            *sl = '\0';

            size_t used = (size_t)(sl - self);

            str_copy(sl, sizeof self - used, "/../share/strata/templates");
            str_copy(dirs[n++], PATH_MAX, self);
        }
    }

    return n;
}


/* Find name in dir, ignoring the case of the letters, as DOS does. */
static int
find_ci(const char *dir, const char *name, char *out, size_t cap)
{
    DIR *dp = opendir(dir != NULL && dir[0] != '\0' ? dir : ".");
    struct dirent *de;
    int found = 0;

    if (dp == NULL) {
        return 0;
    }

    while ((de = readdir(dp)) != NULL) {
        if (strcasecmp(de->d_name, name) == 0) {
            path_join(out, cap, dir != NULL && dir[0] != '\0' ? dir : ".",
                      de->d_name);
            found = 1;
            break;
        }
    }

    closedir(dp);

    return found;
}


/* Find name in dir, as it is typed or with .doc or .set added. */
static int
find_with_extension(const char *dir, const char *name, char *out, size_t cap)
{
    static const char *const ext[] = {"", ".doc", ".set"};
    char full[PATH_MAX];

    for (size_t i = 0; i < sizeof ext / sizeof ext[0]; i++) {
        snprintf(full, sizeof full, "%s%s", name, ext[i]);

        if (find_ci(dir, full, out, cap) != 0) {
            return 1;
        }
    }

    return 0;
}


int
template_find(const char *name, const char *dir, char *out, size_t cap)
{
    char dirs[4][PATH_MAX];
    int n;
    char self[PATH_MAX];
    ssize_t k;

    if (strchr(name, '/') != NULL) {
        if (access(name, R_OK) == 0) {
            str_copy(out, cap, name);

            return 1;
        }

        return 0;
    }

    /* The working directory first, then the places that the list of New
       has: the folder that STRATA_TEMPLATES names, the folder templates of
       the settings and ../share/strata/templates of the programs. */
    if (find_with_extension(dir, name, out, cap) != 0) {
        return 1;
    }

    n = template_dirs(dirs, 4);

    for (int i = 0; i < n; i++) {
        if (find_with_extension(dirs[i], name, out, cap) != 0) {
            return 1;
        }
    }

    /* The folder of the running program itself, as before. */
    k = readlink("/proc/self/exe", self, sizeof self - 1);

    if (k > 0) {
        char *sl;

        self[k] = '\0';
        sl = strrchr(self, '/');

        if (sl != NULL) {
            *sl = '\0';

            return find_with_extension(self, name, out, cap);
        }
    }

    return 0;
}
