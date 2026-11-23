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
 * sptest - unit tests of sysprog.c: programs are found in the fixed list of
 * folders, never in PATH, and only when they are safe to start.
 *
 * usage: sptest
 * The test makes its folders under TMPDIR (or /tmp) and removes them.
 */
#include "../src/util.h"
#include "../src/build_type.h"
#include "../src/sysprog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
static int checks;


static void
check(int ok, const char *what)
{
    checks++;

    if (ok == 0) {
        failures++;
        printf("FAIL %s\n", what);
    } else {
        printf("ok   %s\n", what);
    }
}


/* Make a program (a script that does nothing) with the given mode. */
static void
make_prog(const char *dir, const char *name, mode_t mode)
{
    char path[PATH_MAX];
    FILE *f;

    path_join(path, sizeof path, dir, name);
    f = fopen(path, "w");

    if (f != NULL) {
        fputs("#!/bin/sh\nexit 0\n", f);
        fclose(f);
    }

    chmod(path, mode);
}


/* Remove the programs of a folder and the folder. */
static void
remove_prog(const char *dir, const char *name)
{
    char path[PATH_MAX];

    path_join(path, sizeof path, dir, name);
    unlink(path);
}


int
main(void)
{
    char root[256];
    char good[PATH_MAX];
    char poison[PATH_MAX];
    char loose[PATH_MAX];
    char found[PATH_MAX];
    char text[2 * PATH_MAX];
    char want[2 * PATH_MAX];
    const char *tmp = getenv("TMPDIR");
    const char *dirs_good[2];
    const char *dirs_poison[2];
    const char *dirs_loose[2];
    const char *dirs_empty[1];
    const char *old_path = getenv("PATH");
    char saved_path[PATH_MAX];
    int unsafe = 0;

    snprintf(root, sizeof root, "%.200s/sptest-XXXXXX",
             tmp != NULL && tmp[0] == '/' ? tmp : "/tmp");

    if (mkdtemp(root) == NULL) {
        printf("FAIL cannot make a folder under %s\n", root);

        return 1;
    }

    snprintf(good, sizeof good, "%s/good", root);
    snprintf(poison, sizeof poison, "%s/poison", root);
    snprintf(loose, sizeof loose, "%s/loose", root);
    mkdir(good, 0755);
    mkdir(poison, 0755);
    mkdir(loose, 0777);
    chmod(loose, 0777);
    str_copy(saved_path, sizeof saved_path, old_path != NULL ? old_path : "");
    dirs_good[0] = good;
    dirs_good[1] = NULL;
    dirs_poison[0] = poison;
    dirs_poison[1] = NULL;
    dirs_loose[0] = loose;
    dirs_loose[1] = NULL;
    dirs_empty[0] = NULL;
    make_prog(good, "tool", 0755);
    make_prog(poison, "tool", 0755);
    make_prog(poison, "only-in-path", 0755);

    /* A program of the list is found by its full path. */
    sp_dirs_set(dirs_good);
    snprintf(want, sizeof want, "%s/tool", good);
    check(sp_find("tool", found, sizeof found, &unsafe) == 0 &&
          strcmp(found, want) == 0 && unsafe == 0,
          "find: a program of the list is found by its full path");
    check(sp_find("nothing", found, sizeof found, &unsafe) != 0 &&
          found[0] == '\0',
          "find: a name that is not in the list is not found");

    /* A name that holds a slash is never looked up. */
    check(sp_find("good/tool", found, sizeof found, NULL) != 0 &&
          sp_find(want, found, sizeof found, NULL) != 0 &&
          sp_find("", found, sizeof found, NULL) != 0,
          "find: a name with a slash, and an empty name, are refused");

    /* PATH is not read: a program that is only in PATH is not found. */
    snprintf(text, sizeof text, "%s:%s", poison, saved_path);
    setenv("PATH", text, 1);
    check(sp_find("only-in-path", found, sizeof found, NULL) != 0,
          "path: a program that is only in PATH is not found");
    check(sp_find("tool", found, sizeof found, NULL) == 0 &&
          strcmp(found, want) == 0,
          "path: a program of the same name first in PATH is not used");
    setenv("PATH", ".", 1);
    check(sp_find("tool", found, sizeof found, NULL) == 0 &&
          strcmp(found, want) == 0,
          "path: a PATH of \".\" changes nothing");
    setenv("PATH", saved_path, 1);

    /* The list in use is the list that was set, and nothing else. */
    sp_dirs_set(dirs_empty);
    check(sp_find("tool", found, sizeof found, NULL) != 0,
          "list: an empty list finds nothing, whatever is in PATH");
    sp_dirs_set(dirs_poison);
    snprintf(want, sizeof want, "%s/tool", poison);
    check(sp_find("tool", found, sizeof found, NULL) == 0 &&
          strcmp(found, want) == 0,
          "list: the folders are looked at in the list that was set");

    /* A program that others can write is not safe to start. */
    make_prog(good, "loose-group", 0775);
    make_prog(good, "loose-other", 0757);
    sp_dirs_set(dirs_good);
    unsafe = 0;
    check(sp_find("loose-group", found, sizeof found, &unsafe) != 0 &&
          unsafe == 1,
          "safe: a program that the group can write is refused");
    unsafe = 0;
    check(sp_find("loose-other", found, sizeof found, &unsafe) != 0 &&
          unsafe == 1,
          "safe: a program that all can write is refused");
    check(sp_state("/usr/bin/env") == 1 && sp_state("/nonexistent") == 0 &&
          sp_state(good) == 0,
          "safe: a program of the system is safe, a missing file and a "
          "folder are not programs");

    /* A program in a folder that all can write is not safe either. */
    make_prog(loose, "tool", 0755);
    sp_dirs_set(dirs_loose);
    unsafe = 0;
    check(sp_find("tool", found, sizeof found, &unsafe) != 0 && unsafe == 1,
          "safe: a program in a folder that all can write is refused");

    /* A program that cannot be run is no program. */
    make_prog(good, "no-exec", 0644);
    sp_dirs_set(dirs_good);
    check(sp_find("no-exec", found, sizeof found, NULL) != 0,
          "safe: a file that cannot be run is not a program");

    /* The variable of a program names a full path, nothing else. */
    snprintf(want, sizeof want, "%s/tool", poison);
    setenv("SPTEST_PROG", want, 1);
    check(sp_find_env("SPTEST_PROG", "tool", found, sizeof found) == 0 &&
          strcmp(found, want) == 0,
          "env: a variable that names a full path is the program");
    setenv("SPTEST_PROG", "tool", 1);
    check(sp_find_env("SPTEST_PROG", "tool", found, sizeof found) != 0 &&
          found[0] == '\0',
          "env: a variable that is a name and not a full path is refused");
    setenv("SPTEST_PROG", "/nonexistent/tool", 1);
    check(sp_find_env("SPTEST_PROG", "tool", found, sizeof found) != 0,
          "env: a variable that names a file that is not there is refused");
    snprintf(want, sizeof want, "%s/no-exec", good);
    setenv("SPTEST_PROG", want, 1);
    check(sp_find_env("SPTEST_PROG", "tool", found, sizeof found) != 0,
          "env: a variable that names a file that cannot be run is refused");
    unsetenv("SPTEST_PROG");
    snprintf(want, sizeof want, "%s/tool", good);
    check(sp_find_env("SPTEST_PROG", "tool", found, sizeof found) == 0 &&
          strcmp(found, want) == 0 &&
          sp_find_env(NULL, "tool", found, sizeof found) == 0,
          "env: without the variable the list is used");

    /* The folders are told in the message. */
    sp_dirs_set(dirs_good);
    sp_tried(text, sizeof text);
    check(strcmp(text, good) == 0,
          "tried: a list that was set is told as it is");
    sp_dirs_set(NULL);
    sp_tried(text, sizeof text);
    check(strstr(text, "/usr/bin, /bin, /usr/local/bin, /usr/sbin") ==
          text && strstr(text, "texlive") != NULL,
          "tried: the system list is told, with the TeX Live folders");

    /* The variable STRATA_PROGRAM_DIRS is a hook of the tests: a release
       build never reads it. */
    setenv("STRATA_PROGRAM_DIRS", poison, 1);
    snprintf(want, sizeof want, "%s/only-in-path", poison);
    check((sp_find("only-in-path", found, sizeof found, NULL) == 0 &&
           strcmp(found, want) == 0) == (strata_release == 0),
          "hook: STRATA_PROGRAM_DIRS is read by a debug build and never by "
          "a release build");
    setenv("STRATA_PROGRAM_DIRS", "relative:also/relative", 1);
    check(sp_find("only-in-path", found, sizeof found, NULL) != 0 ||
          strata_release != 0,
          "hook: only full paths of the variable are taken");
    unsetenv("STRATA_PROGRAM_DIRS");
    check(sp_find("env", found, sizeof found, NULL) == 0 &&
          strcmp(found, "/usr/bin/env") == 0,
          "list: the system list finds a program of the system");

    /* A program of the system is found whatever PATH holds. */
    setenv("PATH", "/nonexistent", 1);
    check(sp_find("env", found, sizeof found, NULL) == 0,
          "path: the system list does not depend on PATH");
    setenv("PATH", saved_path, 1);

    /* The environment of a child. */
    setenv("LD_PRELOAD", "/tmp/evil.so", 1);
    setenv("LD_AUDIT", "/tmp/evil.so", 1);
    setenv("PATH", "/tmp/evil", 1);
    snprintf(want, sizeof want, "%s/tool", good);
    sp_child_env(want);
    snprintf(text, sizeof text, "%s:/usr/bin:/bin:/usr/local/bin", good);
    check(strcmp(getenv("PATH"), text) == 0,
          "child: PATH is the folder of the program and the system folders");
    check(getenv("LD_PRELOAD") == NULL && getenv("LD_AUDIT") == NULL,
          "child: the variables that load a library are removed");
    sp_child_env("tool");
    check(strcmp(getenv("PATH"), "/usr/bin:/bin:/usr/local/bin") == 0,
          "child: a name without a folder gives the system folders only");

    sp_dirs_set(NULL);
    remove_prog(good, "tool");
    remove_prog(good, "loose-group");
    remove_prog(good, "loose-other");
    remove_prog(good, "no-exec");
    remove_prog(poison, "tool");
    remove_prog(poison, "only-in-path");
    remove_prog(loose, "tool");
    rmdir(good);
    rmdir(poison);
    rmdir(loose);
    rmdir(root);
    printf("sptest: %d checks, %d failures\n", checks, failures);

    return failures == 0 ? 0 : 1;
}
