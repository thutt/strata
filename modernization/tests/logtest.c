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
 * logtest.c - the unit tests of the log of a run: the socket interface of
 * src/log.c and the collector of src/logrun.c.  Usage: logtest.  The
 * program makes a directory of its own under $TMPDIR (or /tmp) and removes
 * it at the end.
 */
#include "../src/log.h"
#include "../src/logrun.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* A buffer for a path that is made of a directory and a name. */
#define PATH_BUF (2 * PATH_MAX)

static int checks;
static int failures;
static char root[512];


static void
check(int ok, const char *what)
{
    checks++;

    if (ok == 0) {
        failures++;
        printf("FAIL %s\n", what);
    }
}


/* Reads the whole file into a new buffer, or returns NULL. */
static char *
slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    size_t n;

    if (f == NULL) {
        return NULL;
    }

    buf = malloc(1 << 20);

    if (buf == NULL) {
        fclose(f);

        return NULL;
    }

    n = fread(buf, 1, (1 << 20) - 1, f);
    buf[n] = '\0';
    fclose(f);

    return buf;
}


/* Counts the occurrences of text in buf. */
static int
count_of(const char *buf, const char *text)
{
    int n = 0;
    const char *p = buf;

    while ((p = strstr(p, text)) != NULL) {
        n++;
        p += strlen(text);
    }

    return n;
}


/* Removes every file of a directory and the directory. */
static void
remove_tree(const char *dir)
{
    DIR *d = opendir(dir);
    const struct dirent *e;

    if (d == NULL) {
        return;
    }

    while ((e = readdir(d)) != NULL) {
        char path[PATH_BUF];

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }

        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        unlink(path);
    }

    closedir(d);
    rmdir(dir);
}


static int
exists(const char *path)
{
    struct stat st;

    return lstat(path, &st) == 0;
}


/* Makes an empty file. */
static void
touch(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT, 0600);

    if (fd >= 0) {
        close(fd);
    }
}


/*
 * One line of the log has the form
 *     2026-11-14 21:05:33.123 ERROR logtest[1234]: file.c:12: text
 * Returns 1 when the line has it, and the level name and the text.
 */
static int
parse_line(const char *line, char *level, size_t cap, const char **text)
{
    int y;
    int mo;
    int d;
    int h;
    int mi;
    int s;
    int ms;
    int used = 0;
    char lv[8];

    if (sscanf(line, "%4d-%2d-%2d %2d:%2d:%2d.%3d %5s %n", &y, &mo, &d, &h,
               &mi, &s, &ms, lv, &used) < 8 || used == 0) {
        return 0;
    }

    snprintf(level, cap, "%s", lv);
    *text = line + used;

    return 1;
}


/* Makes the directory of this test run and returns it in root. */
static int
make_root(void)
{
    const char *tmp = getenv("TMPDIR");

    snprintf(root, sizeof root, "%s/logtest-XXXXXX",
             tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");

    return mkdtemp(root) != NULL ? 0 : -1;
}


static void
test_run(const char *dir)
{
    char path[PATH_BUF];
    char link[PATH_BUF];
    char *log;
    ssize_t n;
    const char *level_text;
    int found;

    check(logrun_start(dir, 3) == 0, "the run starts");
    check(getenv(LOG_SOCKET_ENV) != NULL, "the socket name is in the "
          "environment");
    snprintf(path, sizeof path, "%s/current.log", dir);
    n = readlink(path, link, sizeof link - 1);
    check(n > 0, "current.log is a link");

    if (n > 0) {
        link[n] = '\0';
        check(strncmp(link, "run-", 4) == 0, "the link names a run log");
    }

    log_open("logtest");
    log_error("logtest: error line with %d", 1);
    log_warn("logtest: warning line with %d", 2);
    log_note("logtest: note line with %d", 3);
    log_debug("logtest: debug line with %d", 4);
    log_note("logtest: a text with a\nnewline and a\ttab and %s", "more");
    logrun_end();
    log_close();
    log = slurp(logrun_path() != NULL ? logrun_path() : path);
    check(log != NULL, "the log file can be read");

    if (log == NULL) {
        return;
    }

    check(count_of(log, "error line with 1") == 1, "the error is written");
    check(count_of(log, "warning line with 2") == 1,
          "the warning is written");
    check(count_of(log, "note line with 3") == (strata_release != 0 ? 0 : 1),
          "the note is written unless this is a release build");
    check(count_of(log, "debug line with 4") == (strata_release != 0 ? 0 : 1),
          "the debug line is written unless this is a release build");
    check(count_of(log, "newline and a tab") == (strata_release != 0 ? 0 : 1),
          "a message is one line, whatever it holds");
    check(count_of(log, "logtest.c:") >= 2, "a line names its file");
    level_text = strstr(log, "ERROR ");
    check(level_text != NULL, "the level of the error is named");

    found = 0;

    for (const char *p = log; *p != '\0';) {
        char level[8];
        const char *text = NULL;
        char line[512];
        size_t len = strcspn(p, "\n");

        if (len >= sizeof line) {
            len = sizeof line - 1;
        }

        memcpy(line, p, len);
        line[len] = '\0';

        if (len > 0) {
            check(parse_line(line, level, sizeof level, &text) != 0,
                  "every line has the time, the level and the program");

            if (text != NULL && strncmp(text, "logtest[", 8) == 0) {
                found++;
            }
        }

        p += len;

        if (*p == '\n') {
            p++;
        }
    }

    check(found >= 2, "every line names the program and its process");
    free(log);
}


/* Several programs write at once: every line is whole and in order. */
static void
test_concurrent(const char *dir)
{
    enum { CHILDREN = 4, LINES = 200 };
    pid_t pids[CHILDREN];
    char *log;
    int counts[CHILDREN] = {0};

    check(logrun_start(dir, 5) == 0, "the second run starts");

    for (int c = 0; c < CHILDREN; c++) {
        pids[c] = fork();

        if (pids[c] == 0) {
            log_open("child");

            for (int i = 0; i < LINES; i++) {
                log_error("child %d: writes line number %d of %d", c, i,
                          LINES);
            }

            _exit(0);
        }
    }

    for (int c = 0; c < CHILDREN; c++) {
        int st;

        waitpid(pids[c], &st, 0);
    }

    {
        const char *path = logrun_path();
        char saved[PATH_BUF];

        snprintf(saved, sizeof saved, "%s", path);
        logrun_end();
        log = slurp(saved);
    }

    check(log != NULL, "the log of the second run can be read");

    if (log == NULL) {
        return;
    }

    for (int c = 0; c < CHILDREN; c++) {
        int last = -1;
        int ordered = 1;

        for (int i = 0; i < LINES; i++) {
            char text[96];
            const char *at;

            snprintf(text, sizeof text, "child %d: writes line number %d of "
                     "%d\n", c, i, LINES);
            at = strstr(log, text);

            if (at != NULL) {
                counts[c]++;
                check(at[-1] == ' ', "a line is whole: it follows its "
                      "prefix");

                if (i < last) {
                    ordered = 0;
                }

                last = i;
            }
        }

        check(counts[c] == LINES, "every message of a child is written");
        check(ordered != 0, "the lines of a child are in order");
    }

    free(log);
}


static void
test_stale_and_prune(const char *dir)
{
    char path[PATH_BUF];
    char live[PATH_BUF];
    int logs = 0;
    long pid = getpid();
    DIR *d;
    const struct dirent *e;

    snprintf(path, sizeof path, "%s/run-20200101-000000-2147483646.sock", dir);
    touch(path);
    snprintf(live, sizeof live, "%s/run-20200101-000000-%ld.sock", dir, pid);
    touch(live);

    for (int i = 1; i <= 6; i++) {
        char name[PATH_BUF];

        snprintf(name, sizeof name, "%s/run-2020010%d-000000.log", dir, i);
        touch(name);
    }

    check(logrun_start(dir, 4) == 0, "a run starts beside stale files");
    check(exists(path) == 0, "the socket of a run that is gone is deleted");
    check(exists(live) != 0, "the socket of a live process is kept");
    logrun_end();
    unlink(live);
    d = opendir(dir);

    while (d != NULL && (e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);

        if (strncmp(e->d_name, "run-", 4) == 0 && n > 4 &&
            strcmp(e->d_name + n - 4, ".log") == 0) {
            logs++;
        }
    }

    if (d != NULL) {
        closedir(d);
    }

    check(logs == 4, "the newest runs are kept: keep logs remain");
    snprintf(path, sizeof path, "%s/run-20200101-000000.log", dir);
    check(exists(path) == 0, "the oldest log is deleted");
    snprintf(path, sizeof path, "%s/run-20200106-000000.log", dir);
    check(exists(path) != 0, "the newest of the old logs is kept");
}


static void
test_fatal(const char *dir)
{
    pid_t p;
    int st = 0;
    char *log;
    char saved[PATH_BUF];

    check(logrun_start(dir, 8) == 0, "a run starts for the fatal test");
    snprintf(saved, sizeof saved, "%s", logrun_path());
    p = fork();

    if (p == 0) {
        log_open("fatal");
        log_fatal(7, "fatal: the test ends the program with status %d", 7);
    }

    waitpid(p, &st, 0);
    check(WIFEXITED(st) != 0 && WEXITSTATUS(st) == 7,
          "log_fatal ends the program with its status");
    logrun_end();
    log = slurp(saved);
    check(log != NULL && strstr(log, "FATAL ") != NULL &&
          strstr(log, "the test ends the program with status 7") != NULL,
          "a fatal error is in the log");
    free(log);

    /* Without a run the message is lost and the program still ends. */
    p = fork();

    if (p == 0) {
        unsetenv(LOG_SOCKET_ENV);
        log_open("nolog");
        log_fatal(9, "fatal: the test ends a program without a run (%d)", 9);
    }

    waitpid(p, &st, 0);
    check(WIFEXITED(st) != 0 && WEXITSTATUS(st) == 9,
          "log_fatal ends a program that has no run");
}


int
main(void)
{
    char dir[1024];

    if (make_root() != 0) {
        printf("logtest: cannot make a directory under $TMPDIR\n");

        return 1;
    }

    snprintf(dir, sizeof dir, "%s/a/b/log", root);
    test_run(dir);
    test_concurrent(dir);
    test_stale_and_prune(dir);
    test_fatal(dir);
    snprintf(dir, sizeof dir, "%s/a/b/log", root);
    remove_tree(dir);
    snprintf(dir, sizeof dir, "%s/a/b", root);
    remove_tree(dir);
    snprintf(dir, sizeof dir, "%s/a", root);
    remove_tree(dir);
    remove_tree(root);
    printf("logtest: %d checks, %d failures\n", checks, failures);

    return failures != 0 ? 1 : 0;
}
