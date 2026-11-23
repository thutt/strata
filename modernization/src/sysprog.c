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

/* sysprog.c - finding the programs of the system by a fixed list. */
#include "util.h"
#include "build_type.h"
#include "sysprog.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SP_MAXDIRS 32
#define SP_MAXYEARS 64
#define SP_TEXLIVE "/usr/local/texlive"

/* The user that the kernel shows for a user that a namespace cannot map. */
#define SP_OVERFLOW_UID 65534

/* The folders of the system, in the order in which they are looked at. */
static const char *const system_dirs[] = {"/usr/bin", "/bin", "/usr/local/bin",
                                          "/usr/sbin", NULL};

/* The list that a test has set, and the list read from the variable. */
static const char *const *test_dirs;
static char env_text[PATH_MAX];
static const char *env_dirs[SP_MAXDIRS];


int
sp_state(const char *path)
{
    struct stat st;
    struct stat ds;
    char dir[PATH_MAX];
    char *slash;

    if (stat(path, &st) != 0 || S_ISREG(st.st_mode) == 0 ||
        access(path, X_OK) != 0) {
        return 0;
    }

    /* The owner is root, the user, or the user that a namespace shows for
       a file whose owner it cannot map (a sandbox shows root as nobody). */
    if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0 ||
        (st.st_uid != 0 && st.st_uid != geteuid() &&
         st.st_uid != SP_OVERFLOW_UID)) {
        return -1;
    }

    str_copy(dir, sizeof dir, path);
    slash = strrchr(dir, '/');

    if (slash == NULL) {
        return -1;
    }

    if (slash == dir) {
        str_copy(dir, sizeof dir, "/");
    } else {
        *slash = '\0';
    }

    if (stat(dir, &ds) != 0 || (ds.st_mode & S_IWOTH) != 0) {
        return -1;
    }

    return 1;
}


void
sp_dirs_set(const char *const *dirs)
{
    test_dirs = dirs;
}


/*
 * The list that a debug or asan build reads from STRATA_PROGRAM_DIRS: only
 * full paths are taken, and the variable is read again at each call.
 * Returns NULL when the variable is not to be used.
 */
static const char *const *
read_env_dirs(void)
{
    const char *v;
    size_t n = 0;
    char *p;

    if (strata_release != 0) {
        return NULL;
    }

    v = getenv("STRATA_PROGRAM_DIRS");

    if (v == NULL || v[0] == '\0') {
        return NULL;
    }

    str_copy(env_text, sizeof env_text, v);
    p = env_text;

    while (*p != '\0' && n + 1 < SP_MAXDIRS) {
        char *colon = strchr(p, ':');

        if (colon != NULL) {
            *colon = '\0';
        }

        if (p[0] == '/') {
            env_dirs[n++] = p;
        }

        if (colon == NULL) {
            break;
        }

        p = colon + 1;
    }

    env_dirs[n] = NULL;

    return env_dirs;
}


/* The list that is in use now: a test list, the variable, or the system. */
static const char *const *
current_dirs(void)
{
    const char *const *e;

    if (test_dirs != NULL) {
        return test_dirs;
    }

    e = read_env_dirs();

    return e != NULL ? e : system_dirs;
}


/* Whether the list in use is the one of the system. */
static int
system_list(void)
{
    return current_dirs() == system_dirs;
}


/* Look at one folder: 1 and the path when it holds the program. */
static int
try_dir(const char *dir, const char *name, char *out, size_t cap,
        int *unsafe)
{
    char cand[PATH_MAX];
    int st;

    path_join(cand, sizeof cand, dir, name);
    st = sp_state(cand);

    if (st > 0) {
        str_copy(out, cap, cand);

        return 1;
    }

    if (st < 0 && unsafe != NULL) {
        *unsafe = 1;
    }

    return 0;
}


/* Order the names of the year folders of TeX Live, the newest first. */
static int
by_name_desc(const void *a, const void *b)
{
    char *const *x = a;
    char *const *y = b;

    return strcmp(*y, *x);
}


/*
 * Look in the folders SP_TEXLIVE/YEAR/bin/SYSTEM.  A TeX Live installation
 * that the user made is not in a folder of the system, and its engines are
 * not in /usr/bin when the system has none of its own.
 */
static int
find_texlive(const char *name, char *out, size_t cap, int *unsafe)
{
    DIR *dp = opendir(SP_TEXLIVE);
    struct dirent *e;
    char *years[SP_MAXYEARS];
    int ny = 0;
    int found = 0;

    if (dp == NULL) {
        return 0;
    }

    while ((e = readdir(dp)) != NULL && ny < SP_MAXYEARS) {
        if (e->d_name[0] >= '0' && e->d_name[0] <= '9') {
            years[ny] = strdup(e->d_name);

            if (years[ny] != NULL) {
                ny++;
            }
        }
    }

    closedir(dp);
    qsort(years, usz(ny), sizeof years[0], by_name_desc);

    for (int i = 0; i < ny && found == 0; i++) {
        char bin[1024];
        DIR *bp;

        snprintf(bin, sizeof bin, "%s/%s/bin", SP_TEXLIVE, years[i]);
        bp = opendir(bin);

        while (bp != NULL && found == 0 && (e = readdir(bp)) != NULL) {
            char arch[PATH_MAX];

            if (e->d_name[0] == '.') {
                continue;
            }

            snprintf(arch, sizeof arch, "%s/%s", bin, e->d_name);
            found = try_dir(arch, name, out, cap, unsafe);
        }

        if (bp != NULL) {
            closedir(bp);
        }
    }

    for (int i = 0; i < ny; i++) {
        free(years[i]);
    }

    return found;
}


int
sp_find(const char *name, char *out, size_t cap, int *unsafe)
{
    const char *const *dirs = current_dirs();

    if (cap > 0) {
        out[0] = '\0';
    }

    if (unsafe != NULL) {
        *unsafe = 0;
    }

    /* A name is a name: a path is never looked up and never searched. */
    if (name[0] == '\0' || strchr(name, '/') != NULL) {
        return -1;
    }

    for (int i = 0; dirs[i] != NULL; i++) {
        if (try_dir(dirs[i], name, out, cap, unsafe) != 0) {
            return 0;
        }
    }

    if (system_list() != 0 && find_texlive(name, out, cap, unsafe) != 0) {
        return 0;
    }

    return -1;
}


int
sp_find_env(const char *envname, const char *name, char *out, size_t cap)
{
    const char *env = envname != NULL ? getenv(envname) : NULL;
    struct stat st;

    if (env != NULL && env[0] != '\0') {
        if (env[0] != '/' || stat(env, &st) != 0 ||
            S_ISREG(st.st_mode) == 0 || access(env, X_OK) != 0) {
            if (cap > 0) {
                out[0] = '\0';
            }

            return -1;
        }

        str_copy(out, cap, env);

        return 0;
    }

    return sp_find(name, out, cap, NULL);
}


void
sp_tried(char *out, size_t cap)
{
    const char *const *dirs = current_dirs();
    size_t n = 0;

    out[0] = '\0';

    for (int i = 0; dirs[i] != NULL; i++) {
        int w = snprintf(out + n, cap - n, "%s%s", i > 0 ? ", " : "",
                         dirs[i]);

        if (w < 0 || usz(w) >= cap - n) {
            return;
        }

        n += usz(w);
    }

    if (system_list() != 0 && n + 1 < cap) {
        snprintf(out + n, cap - n, ", %s/YEAR/bin/SYSTEM", SP_TEXLIVE);
    }
}


void
sp_child_env(const char *prog)
{
    static const char tail[] = ":/usr/bin:/bin:/usr/local/bin";
    char path[PATH_MAX];
    char *slash;

    str_copy(path, sizeof path, prog);
    slash = strrchr(path, '/');

    if (slash != NULL && slash != path) {
        size_t n = lsz(slash - path);

        str_copy(slash, sizeof path - n, tail);
    } else {
        str_copy(path, sizeof path, tail + 1);
    }

    setenv("PATH", path, 1);
    unsetenv("LD_PRELOAD");
    unsetenv("LD_AUDIT");
}
