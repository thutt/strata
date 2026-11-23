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
 * vertest - the version of the file format.
 *
 * The first record of a file is *R<n>.  The programs read the versions 2 and
 * 3 and write 3.  The tests check the three errors for a file (the version is
 * above 3, the version is below 2 or damaged, no first record), the writer,
 * the name and the making of the special backup, and how the list of
 * backups and the restore treat a special backup.
 */
#include "../src/backup.h"
#include "../src/msdoc.h"
#include "../src/util.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks;
static int failures;


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


/* The text of a message that names a value, as one check. */
static void
check_text(const char *got, const char *want, const char *what)
{
    char line[400];

    snprintf(line, sizeof line, "%s", what);
    check(strcmp(got, want) == 0, line);

    if (strcmp(got, want) != 0) {
        printf("     got  %s\n     want %s\n", got, want);
    }
}


/* ------------------------------------------------------ the first record */
typedef struct {
    const char *what;
    const char *data;
    size_t n;
    int ok; /* 1: read, 0: refused */
    int version;
    const char *message; /* the message of a refused file */
} Case;

#define CASE(what, data, ok, version, message) \
    {what, data, sizeof data - 1, ok, version, message}

#define MSG_NOT "The file is not a document of this software."
#define MSG_OLD3 \
    "The file is version 4.  This software reads up to version 3."


static void
test_check(void)
{
    static const Case cases[] = {
        CASE("*R3 is the current version", "*R3\r\nMR1000\r\n", 1, 3, ""),
        CASE("*R2 is the older version", "*R2\r\nMR1000\r\n", 1, 2, ""),
        CASE("*R2h (a library file) is version 2", "*R2h\r\nMD\r\n", 1, 2,
             ""),
        CASE("*R3 alone at the end of the data", "*R3", 1, 3, ""),
        CASE("*R4 is too new", "*R4\r\nMR1000\r\n", 0, 0, MSG_OLD3),
        CASE("*R10 is too new", "*R10\r\n", 0, 0,
             "The file is version 10.  This software reads up to version 3."),
        CASE("*R99999999999 is too new", "*R99999999999\r\n", 0, 0,
             "The file is version 99999999999.  This software reads up to "
             "version 3."),
        CASE("*R1 is below the oldest", "*R1\r\n", 0, 0,
             "The file version is 1.  This software reads versions 2 to 3."),
        CASE("*R0 is below the oldest", "*R0\r\n", 0, 0,
             "The file version is 0.  This software reads versions 2 to 3."),
        CASE("*R-1 is damaged", "*R-1\r\n", 0, 0,
             "The file version is \"-1\".  This software reads versions 2 to "
             "3."),
        CASE("*R with no number is damaged", "*R\r\nMR1000\r\n", 0, 0,
             "The file version is \"\".  This software reads versions 2 to "
             "3."),
        CASE("*R at the end of the data is damaged", "*R", 0, 0,
             "The file version is \"\".  This software reads versions 2 to "
             "3."),
        CASE("*Rx is damaged", "*Rx\r\n", 0, 0,
             "The file version is \"x\".  This software reads versions 2 to "
             "3."),
        CASE("*R3X (a capital) is damaged", "*R3X\r\n", 0, 0,
             "The file version is \"3X\".  This software reads versions 2 to "
             "3."),
        CASE("an empty file is no document", "", 0, 0, MSG_NOT),
        CASE("R3 without the star is no document", "R3\r\n", 0, 0, MSG_NOT),
        CASE("a text file is no document", "Hello, world\r\n", 0, 0,
             MSG_NOT),
        CASE("*B1 is no document", "*B1\r\n", 0, 0, MSG_NOT),
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char err[300] = "";
        int v = -7;
        int r = docver_check((const uint8_t *)cases[i].data, cases[i].n, &v,
                             err, sizeof err);

        if (cases[i].ok != 0) {
            check(r == 0 && v == cases[i].version, cases[i].what);
        } else {
            char what[200];

            snprintf(what, sizeof what, "%s: the message", cases[i].what);
            check(r != 0, cases[i].what);
            check_text(err, cases[i].message, what);
        }
    }
}


/* -------------------------------------------------------- load and save */
static uint8_t *
bytes_of(const char *path, size_t *n)
{
    FILE *fp = fopen(path, "rb");
    uint8_t *b;
    long len;

    *n = 0;

    if (fp == NULL) {
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    b = malloc((size_t)len + 1);

    if (b != NULL && fread(b, 1, (size_t)len, fp) == (size_t)len) {
        *n = (size_t)len;
    }

    fclose(fp);

    return b;
}


static void
put_file(const char *path, const char *data, size_t n)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL || fwrite(data, 1, n, fp) != n) {
        printf("FAIL cannot write %s\n", path);
        failures++;
    }

    if (fp != NULL) {
        fclose(fp);
    }
}


#define BODY "MR1001\r\nPP\\1\\\0\r\n*B0\r\nS\r\nTtext\035\0\r\n*E\r\n"


static void
test_load_save(void)
{
    static const char r2[] = "*R2\r\n" BODY;
    static const char r3[] = "*R3\r\n" BODY;
    static const char r2h[] = "*R2h\r\n" BODY;
    static const char r4[] = "*R4\r\n" BODY;
    char err[300] = "";
    Doc *d;
    size_t len = 0;
    uint8_t *out;

    d = doc_load_mem((const uint8_t *)r2, sizeof r2 - 1, err, sizeof err);
    check(d != NULL && d->file_version == 2, "a *R2 document is read as "
          "version 2");

    if (d != NULL) {
        out = doc_serialize(d, &len);
        check(len == sizeof r3 - 1 && memcmp(out, r3, len) == 0,
              "a version 2 document is written as *R3 and nothing else "
              "changes");
        free(out);
        doc_free(d);
    }

    d = doc_load_mem((const uint8_t *)r3, sizeof r3 - 1, err, sizeof err);
    check(d != NULL && d->file_version == 3, "a *R3 document is read as "
          "version 3");

    if (d != NULL) {
        out = doc_serialize(d, &len);
        check(len == sizeof r3 - 1 && memcmp(out, r3, len) == 0,
              "a version 3 document is written as it was read");
        free(out);
        doc_free(d);
    }

    d = doc_load_mem((const uint8_t *)r2h, sizeof r2h - 1, err, sizeof err);
    check(d != NULL && d->file_version == 2, "a *R2h file is read as "
          "version 2");

    if (d != NULL) {
        out = doc_serialize(d, &len);
        check(len > 5 && memcmp(out, "*R3h\r\n", 6) == 0,
              "a *R2h file is written as *R3h: the flag is kept");
        free(out);
        doc_free(d);
    }

    d = doc_load_mem((const uint8_t *)r4, sizeof r4 - 1, err, sizeof err);
    check(d == NULL, "a *R4 document is refused");
    check_text(err, MSG_OLD3, "the message of a *R4 document");
    doc_free(d);

    d = doc_new(0);
    out = doc_serialize(d, &len);
    check(len > 5 && memcmp(out, "*R3\r\n", 5) == 0,
          "a new document is written as *R3");
    check(d->file_version == STRATA_FILE_FORMAT, "a new document is version "
          "3");
    free(out);
    doc_free(d);

    d = doc_new(1);
    out = doc_serialize(d, &len);
    check(len > 5 && memcmp(out, "*R3\r\n", 5) == 0,
          "a new structured document is written as *R3");
    free(out);
    doc_free(d);
}


/* ----------------------------------------------------- the special backup */
static void
test_names(void)
{
    char name[PATH_MAX];

    check(docver_special_name("/d/memo.doc", 2, name, sizeof name) == 0 &&
          strcmp(name, "/d/V2-memo.doc") == 0,
          "the special backup of /d/memo.doc is /d/V2-memo.doc");
    check(docver_special_name("standard.set", 2, name, sizeof name) == 0 &&
          strcmp(name, "V2-standard.set") == 0,
          "the special backup of standard.set is V2-standard.set");
    check(docver_special_name("/d/V2-memo.doc", 2, name, sizeof name) == 0 &&
          strcmp(name, "/d/V2-V2-memo.doc") == 0,
          "a name with the prefix gets the prefix again");
    check(docver_special_name("/d/memo.doc", 5, name, 8) != 0,
          "a name that does not fit is refused");
}


static int
exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}


/* The path of a name in a directory, empty when it does not fit. */
static void
join(char *out, const char *dir, const char *name)
{
    size_t a = strlen(dir);
    size_t b = strlen(name);

    if (a + 1 + b + 1 > PATH_MAX) {
        out[0] = '\0';

        return;
    }

    memcpy(out, dir, a);
    out[a] = '/';
    memcpy(out + a + 1, name, b + 1);
}


static void
test_special_backup(const char *dir)
{
    char path[PATH_MAX];
    char bak[PATH_MAX];
    char err[300] = "";
    static const char r2[] = "*R2\r\n" BODY;
    static const char r3[] = "*R3\r\n" BODY;
    int made = -1;
    size_t n;
    uint8_t *b;

    join(path, dir, "memo.doc");
    join(bak, dir, "V2-memo.doc");
    put_file(path, r2, sizeof r2 - 1);

    check(docver_file_version(path) == 2, "the version of a *R2 file is 2");
    check(docver_special_backup(path, &made, err, sizeof err) == 0 &&
          made == 1, "the special backup of a version 2 file is made");
    b = bytes_of(bak, &n);
    check(b != NULL && n == sizeof r2 - 1 && memcmp(b, r2, n) == 0,
          "the special backup has the bytes of the original");
    free(b);

    /* An existing special backup is never written again. */
    put_file(bak, "kept", 4);
    made = -1;
    check(docver_special_backup(path, &made, err, sizeof err) == 0 &&
          made == 0, "a special backup that exists is not written again");
    b = bytes_of(bak, &n);
    check(b != NULL && n == 4 && memcmp(b, "kept", 4) == 0,
          "an existing special backup is not changed");
    free(b);
    unlink(bak);

    /* A file of the current version needs none. */
    put_file(path, r3, sizeof r3 - 1);
    made = -1;
    check(docver_special_backup(path, &made, err, sizeof err) == 0 &&
          made == 0 && exists(bak) == 0,
          "a version 3 file gets no special backup");

    /* A file that is no document gets none. */
    put_file(path, "plain text\r\n", 12);
    check(docver_special_backup(path, &made, err, sizeof err) == 0 &&
          exists(bak) == 0, "a text file gets no special backup");

    /* A template keeps its extension. */
    join(path, dir, "standard.set");
    join(bak, dir, "V2-standard.set");
    put_file(path, r2, sizeof r2 - 1);
    check(docver_special_backup(path, &made, err, sizeof err) == 0 &&
          made == 1 && exists(bak) != 0,
          "the special backup of a template is V2-standard.set");
    unlink(path);
    unlink(bak);

    /* A directory that cannot be written: the backup fails and says so. */
    {
        char sub[PATH_MAX];
        char file[PATH_MAX];
        char vfile[PATH_MAX];

        join(sub, dir, "ro");
        join(file, dir, "ro/memo.doc");
        join(vfile, dir, "ro/V2-memo.doc");
        mkdir(sub, 0755);
        put_file(file, r2, sizeof r2 - 1);
        chmod(sub, 0555);

        if (access(sub, W_OK) != 0) {
            int r = docver_special_backup(file, &made, err, sizeof err);

            check(r == -1 && exists(vfile) == 0,
                  "a special backup that cannot be written is an error");
            check(strstr(err, "special backup") != NULL &&
                  strstr(err, "V2-memo.doc") != NULL,
                  "the error names the special backup");
            b = bytes_of(file, &n);
            check(b != NULL && n == sizeof r2 - 1 && memcmp(b, r2, n) == 0,
                  "the original is not touched");
            free(b);
        } else {
            printf("skip the read-only directory test: the directory can be "
                   "written (running as root)\n");
        }

        chmod(sub, 0755);
        unlink(file);
        unlink(vfile);
        rmdir(sub);
    }
}


/* --------------------------------------------- the list and the restore */
static void
test_backup_list(const char *dir)
{
    static const char r2[] = "*R2\r\n" BODY;
    static const char r3[] = "*R3\r\n" BODY;
    char path[PATH_MAX];
    char bak[PATH_MAX];
    char err[300] = "";
    BackupInfo v[2 * BACKUP_MAX];
    int n;
    int special = -1;
    int unchanged = 0;
    int same = 0;
    size_t len;
    uint8_t *b;

    join(path, dir, "plan.doc");
    join(bak, dir, "V2-plan.doc");
    put_file(path, r2, sizeof r2 - 1);
    check(docver_special_backup(path, NULL, err, sizeof err) == 0,
          "the special backup of plan.doc is made");

    /* Save the document again and again with one numbered backup. */
    for (int i = 0; i < 4; i++) {
        char text[64];
        int w = snprintf(text, sizeof text, "*R3\r\nMR100%d\r\n*B0\r\n*E\r\n",
                         i + 1);

        put_file(path + 0, text, (size_t)w);
        check(backup_write(path, text, (size_t)w, 1, &unchanged, err,
                           sizeof err) == 0, "a save with one backup");
    }

    check(exists(bak) != 0, "the numbered backups never remove the special "
          "backup");
    n = backup_list(path, v, 2 * BACKUP_MAX);

    for (int i = 0; i < n; i++) {
        if (v[i].special != 0) {
            special = i;
        }
    }

    check(special >= 0 && v[special].n == 0 && v[special].old == 2 &&
          v[special].special == 2, "the list of backups shows the special "
          "backup as version 2");

    if (special >= 0) {
        check(backup_restore(path, &v[special], 1, &same, err, sizeof err) ==
              0 && same == 0, "the special backup is restored");
        b = bytes_of(path, &len);
        check(b != NULL && len == sizeof r2 - 1 && memcmp(b, r2, len) == 0,
              "a restored special backup is the original file, version 2");
        free(b);
        check(exists(bak) != 0, "the special backup stays after the restore");
    }

    /* A version 3 file has a backup list with no special backup. */
    unlink(bak);
    put_file(path, r3, sizeof r3 - 1);
    n = backup_list(path, v, 2 * BACKUP_MAX);
    special = 0;

    for (int i = 0; i < n; i++) {
        special += v[i].special != 0;
    }

    check(special == 0, "no special backup is listed when there is none");
}


int
main(int argc, char **argv)
{
    char dir[PATH_MAX];

    (void)argc;
    (void)argv;
    snprintf(dir, sizeof dir, "/tmp/vertest.%ld", (long)getpid());
    mkdir(dir, 0755);

    test_check();
    test_load_save();
    test_names();
    test_special_backup(dir);
    test_backup_list(dir);

    {
        char cmd[PATH_MAX + 16];

        snprintf(cmd, sizeof cmd, "rm -rf %s", dir);

        if (system(cmd) != 0) {
            printf("the directory %s was not removed\n", dir);
        }
    }

    printf("vertest: %d checks, %d failures\n", checks, failures);

    return failures != 0;
}
