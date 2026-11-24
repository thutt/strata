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
 * backuptest.c - the unit tests of the numbered backups (src/backup.c) and
 * of the settings that say how many are kept (src/config.c).
 *
 * Every test works in a directory of its own under /tmp.  The files hold a
 * short text, such as "v3", so that the version of a file can be read.
 *
 *     backuptest                  the tests
 *     backuptest --measure FILE   the time of a save with 0, 1 and 99
 *                                 backups of a large document
 */
#include "../src/util.h"
#include "../src/backup.h"
#include "../src/config.h"
#include <dirent.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <utime.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;
static int checks = 0;
static char root[256];
static char dir[512];
static int test_number;

#define CHECK(c, ...)                                                          \
    do {                                                                       \
        checks++;                                                              \
        if (!(c)) {                                                            \
            printf("FAIL line %d: ", __LINE__);                                \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
            fails++;                                                           \
        }                                                                      \
    } while (0)


/* A new directory for a test; dir holds its name. */
static void
fresh(void)
{
    char tag[24];

    test_number++;
    snprintf(tag, sizeof tag, "/t%d", test_number);
    str_copy(dir, sizeof dir, root);
    str_copy(dir + strlen(dir), sizeof dir - strlen(dir), tag);
    mkdir(dir, 0755);
}


/* The path of a file of the test directory. */
static const char *
at(const char *name)
{
    static char path[4][1024];
    static int next;
    char *p = path[next++ % 4];
    size_t n = strlen(dir);

    str_copy(p, 1024, dir);
    str_copy(p + n, 1024 - n, "/");
    str_copy(p + n + 1, 1024 - n - 1, name);

    return p;
}


/* Make a folder of the test directory (and the folder .backup for a name in it). */
static void
mk(const char *name)
{
    mkdir(at(name), 0755);
}


static void
put(const char *name, const char *text)
{
    if (strncmp(name, BACKUP_FOLDER "/", strlen(BACKUP_FOLDER) + 1) == 0) {
        mk(BACKUP_FOLDER);
    }

    FILE *f = fopen(at(name), "wb");

    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}


/* Does the file hold exactly this text? */
static int
holds(const char *name, const char *text)
{
    char buf[256];
    FILE *f = fopen(at(name), "rb");

    if (f == NULL) {
        return 0;
    }

    size_t k = fread(buf, 1, sizeof buf - 1, f);

    fclose(f);
    buf[k] = '\0';

    return strcmp(buf, text) == 0;
}


static int
there(const char *name)
{
    struct stat st;

    return lstat(at(name), &st) == 0;
}


/* The number of entries of the test directory. */
static int
entries(void)
{
    DIR *d = opendir(dir);
    int n = 0;

    if (d == NULL) {
        return -1;
    }

    for (struct dirent *e = readdir(d); e != NULL; e = readdir(d)) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
            n++;
        }
    }

    closedir(d);

    return n;
}


/* The number of entries of a folder of the test directory. */
static int
count_in(const char *sub)
{
    char saved[512];
    int n;

    str_copy(saved, sizeof saved, dir);
    str_copy(dir + strlen(dir), sizeof dir - strlen(dir), "/");
    str_copy(dir + strlen(dir), sizeof dir - strlen(dir), sub);
    n = entries();
    str_copy(dir, sizeof dir, saved);

    return n;
}


/* Save the text under the name with depth backups; returns the result. */
static int
save(const char *name, const char *text, int depth, int *unchanged)
{
    char err[200];

    return backup_write(at(name), text, strlen(text), depth, unchanged, err,
                        sizeof err);
}


static void
test_names(void)
{
    char n[PATH_MAX];
    char longname[400];

    CHECK(backup_name("/d/report.doc", 1, n, sizeof n) == 0 &&
              strcmp(n, "/d/.backup/report.bak01") == 0,
          "the name of backup 1 is %s", n);
    CHECK(backup_old_name("/d/report.doc", 1, n, sizeof n) == 0 &&
              strcmp(n, "/d/report.bak01") == 0,
          "the old name of backup 1 is %s", n);
    CHECK(backup_dir("/d/report.doc", n, sizeof n) == 0 &&
              strcmp(n, "/d/.backup") == 0,
          "the folder is %s", n);
    CHECK(backup_name("REPORT.DOC", 12, n, sizeof n) == 0 &&
              strcmp(n, ".backup/REPORT.BAK12") == 0,
          "capitals stay capitals, a name without a directory: %s", n);
    CHECK(backup_name("a.b.c.doc", 7, n, sizeof n) == 0 &&
              strcmp(n, ".backup/a.b.c.bak07") == 0,
          "a name with dots: %s", n);
    CHECK(backup_name("memo.txt", 3, n, sizeof n) == 0 &&
              strcmp(n, ".backup/memo.txt.bak03") == 0,
          "a name that is not .doc: %s", n);
    CHECK(backup_name("/report.doc", 2, n, sizeof n) == 0 &&
              strcmp(n, "/.backup/report.bak02") == 0,
          "a document in the root directory: %s", n);
    CHECK(backup_is_backup_file("/d/report.bak01") == 1 &&
              backup_is_backup_file("report.BAK12") == 1 &&
              backup_is_backup_file("/d/memo.bak") == 1 &&
              backup_is_backup_file("a.b.Bak99") == 1,
          "backup file names are recognised");
    CHECK(backup_is_backup_file("/d/.backup/anything.doc") == 1 &&
              backup_is_backup_file(".backup/x") == 1,
          "a file in the folder .backup is a backup file");
    CHECK(backup_is_backup_file("/d/report.doc") == 0 &&
              backup_is_backup_file("bak") == 0 &&
              backup_is_backup_file("/d/report.bakery") == 0 &&
              backup_is_backup_file("/d/my.backup.doc") == 0 &&
              backup_is_backup_file("/d/.backupx/a.doc") == 0 &&
              backup_is_backup_file("report.bak01.doc") == 0,
          "other names are not backup files");
    CHECK(backup_name("x.doc", 0, n, sizeof n) != 0 &&
              backup_name("x.doc", 100, n, sizeof n) != 0,
          "the numbers are 1 to 99");
    memset(longname, 'a', sizeof longname - 5);
    strcpy(longname + sizeof longname - 5, ".doc");
    CHECK(backup_name(longname, 1, n, sizeof n) != 0,
          "a name that is too long is refused, not cut");
    CHECK(backup_name("/d/report.doc", 1, n, 8) != 0,
          "a name that does not fit the room is refused");
}


static void
test_series(void)
{
    int un = 0;

    /* Three backups and five saves of changed text. */
    fresh();
    put("a.doc", "v1");

    for (int v = 2; v <= 6; v++) {
        char t[16];

        snprintf(t, sizeof t, "v%d", v);
        CHECK(save("a.doc", t, 3, &un) == 0 && un == 0, "save %s", t);
    }

    CHECK(holds("a.doc", "v6"), "the document is the last version");
    CHECK(holds(".backup/a.bak01", "v5") && holds(".backup/a.bak02", "v4") &&
              holds(".backup/a.bak03", "v3"),
          "the backups hold the three versions before, 1 the newest");
    CHECK(there(".backup/a.bak04") == 0, "the oldest are gone");
    CHECK(entries() == 2 && count_in(BACKUP_FOLDER) == 3,
          "the document and the folder; no file beside the document: %d "
          "entries, %d in the folder", entries(), count_in(BACKUP_FOLDER));

    /* One backup: the old behaviour. */
    fresh();
    put("a.doc", "v1");
    CHECK(save("a.doc", "v2", 1, NULL) == 0 && holds(".backup/a.bak01", "v1"),
          "N = 1 keeps the version before");
    CHECK(save("a.doc", "v3", 1, NULL) == 0 && holds(".backup/a.bak01", "v2") &&
              there(".backup/a.bak02") == 0,
          "N = 1 replaces the backup");

    /* None. */
    fresh();
    put("a.doc", "v1");
    CHECK(save("a.doc", "v2", 0, NULL) == 0 && holds("a.doc", "v2") &&
              entries() == 1,
          "N = 0 makes no backup");

    /* Fewer saves than backups. */
    fresh();
    put("a.doc", "v1");
    save("a.doc", "v2", 3, NULL);
    save("a.doc", "v3", 3, NULL);
    CHECK(holds(".backup/a.bak01", "v2") && holds(".backup/a.bak02", "v1") &&
              there(".backup/a.bak03") == 0,
          "two saves leave two backups");

    /* A new document has no version before it. */
    fresh();
    CHECK(save("n.doc", "v1", 3, NULL) == 0 && holds("n.doc", "v1") &&
              entries() == 1,
          "a document that is new makes no backup");
}


static void
test_gap_and_depth(void)
{
    /* Backup 2 is missing: the gap is filled, nothing is lost. */
    fresh();
    put("a.doc", "d");
    put(".backup/a.bak01", "b1");
    put(".backup/a.bak03", "b3");
    CHECK(save("a.doc", "new", 3, NULL) == 0, "save with a gap");
    CHECK(holds(".backup/a.bak01", "d") && holds(".backup/a.bak02", "b1") &&
              holds(".backup/a.bak03", "b3"),
          "the gap is filled and the older backup stays");

    /* Lowering the number removes the oldest backups at the next save. */
    fresh();
    put("a.doc", "d");
    put(".backup/a.bak01", "b1");
    put(".backup/a.bak02", "b2");
    put(".backup/a.bak03", "b3");
    put(".backup/a.bak04", "b4");
    put(".backup/a.bak05", "b5");
    CHECK(save("a.doc", "new", 2, NULL) == 0, "save with N lowered to 2");
    CHECK(holds(".backup/a.bak01", "d") && holds(".backup/a.bak02", "b1") &&
              there(".backup/a.bak03") == 0 && there(".backup/a.bak04") == 0 &&
              there(".backup/a.bak05") == 0,
          "only two backups are left");

    /* Raising the number keeps what is there. */
    CHECK(save("a.doc", "newer", 5, NULL) == 0 && holds(".backup/a.bak01", "new") &&
              holds(".backup/a.bak02", "d") && holds(".backup/a.bak03", "b1"),
          "raising N keeps the backups");

    /* N = 0 leaves the backups that exist alone. */
    CHECK(save("a.doc", "last", 0, NULL) == 0 && holds(".backup/a.bak01", "new") &&
              holds(".backup/a.bak03", "b1"),
          "N = 0 touches no backup");
}


static void
test_unchanged(void)
{
    struct stat before;
    struct stat after;
    int un = 0;
    int n;

    fresh();
    put("a.doc", "same");
    put(".backup/a.bak01", "old");
    stat(at("a.doc"), &before);
    n = entries();
    CHECK(save("a.doc", "same", 3, &un) == 0 && un == 1,
          "a save of the same content is reported as unchanged");
    stat(at("a.doc"), &after);
    CHECK(holds(".backup/a.bak01", "old") && there(".backup/a.bak02") == 0,
          "no backup is made or moved");
    CHECK(before.st_ino == after.st_ino && before.st_mtime == after.st_mtime,
          "the document is not written");
    CHECK(entries() == n, "no file is left over");

    /* With no backups kept the file is written as before. */
    CHECK(save("a.doc", "same", 0, &un) == 0 && un == 0,
          "N = 0 does not look at the content");
}


static void
test_failures(void)
{
    char err[200] = "";
    int un = 0;

    /* A Save onto a backup file is refused and writes nothing. */
    fresh();
    put("a.doc", "d");
    put("a.bak01", "old backup");
    put(".backup/a.bak02", "b2");
    err[0] = '\0';
    CHECK(backup_write(at("a.bak01"), "new", 3, 2, &un, err, sizeof err) != 0 &&
              strcmp(err, BACKUP_REFUSED_MSG) == 0,
          "a save onto a.bak01 is refused with the message: %s", err);
    CHECK(backup_write(at(".backup/a.bak02"), "new", 3, 0, &un, err,
                       sizeof err) != 0 &&
              holds("a.bak01", "old backup") && holds(".backup/a.bak02", "b2"),
          "a save onto a file in the folder .backup is refused");
    CHECK(backup_write(at("a.doc"), "d2", 2, 0, &un, err, sizeof err) == 0 &&
              holds("a.doc", "d2"),
          "a save under another name still works");

    /* A backup that cannot be removed: the document stays whole. */
    fresh();
    put("a.doc", "d");
    put(".backup/a.bak01", "b1");
    mkdir(at(".backup/a.bak02"), 0755);
    put(".backup/a.bak02/inner", "x");
    CHECK(backup_write(at("a.doc"), "new", 3, 2, &un, err, sizeof err) != 0 &&
              err[0] != '\0',
          "a step that fails gives a message: %s", err);
    CHECK(holds("a.doc", "d") && holds(".backup/a.bak01", "b1") &&
              there(".backup/a.bak02/inner") != 0,
          "the document and the backups are as they were");
    CHECK(entries() == 2 && count_in(BACKUP_FOLDER) == 2,
          "no file is left over: %d, %d", entries(), count_in(BACKUP_FOLDER));

    /* A name that is a directory. */
    fresh();
    mkdir(at("a.doc"), 0755);
    err[0] = '\0';
    CHECK(backup_write(at("a.doc"), "x", 1, 1, NULL, err, sizeof err) != 0 &&
              strstr(err, "cannot write file") != NULL,
          "a directory is not replaced: %s", err);

    if (geteuid() != 0) {
        /* A directory that cannot be written: a file that can be changed is
           written in place, without a backup. */
        fresh();
        put("a.doc", "d");
        chmod(dir, 0555);
        CHECK(save("a.doc", "new", 3, NULL) == 0 && holds("a.doc", "new") &&
                  there(".backup/a.bak01") == 0,
              "a read-only directory: saved in place");

        /* ... and a file that cannot be changed is refused. */
        chmod(at("a.doc"), 0444);
        err[0] = '\0';
        CHECK(backup_write(at("a.doc"), "x", 1, 3, NULL, err, sizeof err) !=
                      0 &&
                  holds("a.doc", "new"),
              "a read-only file in a read-only directory: refused");
        chmod(dir, 0755);
    }
}


static void
test_file_state(void)
{
    struct stat st;

    /* The permissions of the old file are kept. */
    fresh();
    put("a.doc", "d");
    chmod(at("a.doc"), 0640);
    save("a.doc", "new", 2, NULL);
    stat(at("a.doc"), &st);
    CHECK((st.st_mode & 0777) == 0640, "the mode is kept: %o",
          st.st_mode & 0777);

    /* A link to the document: the file itself is replaced. */
    fresh();
    put("real.doc", "d");
    CHECK(symlink("real.doc", at("link.doc")) == 0, "make the link");
    CHECK(save("link.doc", "new", 2, NULL) == 0 && holds("real.doc", "new") &&
              holds(".backup/real.bak01", "d"),
          "a link is followed");
    CHECK(lstat(at("link.doc"), &st) == 0 && S_ISLNK(st.st_mode) != 0,
          "the link stays a link");
}


/* The folder .backup: made at the first backup, and what goes wrong. */
static void
test_folder(void)
{
    struct stat st;
    char err[200] = "";
    int un = 0;
    mode_t old_mask = umask(022);

    fresh();
    put("a.doc", "v1");
    CHECK(save("a.doc", "v2", 2, NULL) == 0 && stat(at(".backup"), &st) == 0 &&
              S_ISDIR(st.st_mode) != 0 && (st.st_mode & 0777) == 0755,
          "the folder is made at the first backup, with the umask: %o",
          st.st_mode & 0777);
    CHECK(holds(".backup/a.bak01", "v1") && there("a.bak01") == 0,
          "the backup is in the folder, nothing is beside the document");

    /* An unchanged document makes no backup and no folder. */
    fresh();
    put("a.doc", "same");
    CHECK(save("a.doc", "same", 3, &un) == 0 && un == 1 &&
              there(BACKUP_FOLDER) == 0 && entries() == 1,
          "an unchanged document makes no folder");

    /* N = 0 makes no folder. */
    fresh();
    put("a.doc", "v1");
    CHECK(save("a.doc", "v2", 0, NULL) == 0 && there(BACKUP_FOLDER) == 0,
          "no backups kept, no folder");

    /* A file named .backup is in the way: the save works, with no backup. */
    fresh();
    put("a.doc", "v1");
    put(BACKUP_FOLDER, "i am a file");
    CHECK(backup_write(at("a.doc"), "v2", 2, 2, &un, err, sizeof err) == 0 &&
              un == 2 && strstr(err, BACKUP_FOLDER) != NULL &&
              holds("a.doc", "v2") && holds(BACKUP_FOLDER, "i am a file") &&
              entries() == 2,
          "a file named .backup: saved, no backup, the reason is told: %s",
          err);

    if (geteuid() != 0) {
        /* A folder that cannot be written. */
        fresh();
        put("a.doc", "v1");
        mk(BACKUP_FOLDER);
        chmod(at(BACKUP_FOLDER), 0555);
        err[0] = '\0';
        CHECK(backup_write(at("a.doc"), "v2", 2, 2, &un, err, sizeof err) ==
                      0 &&
                  un == 2 && strstr(err, "cannot be written") != NULL &&
                  holds("a.doc", "v2") && count_in(BACKUP_FOLDER) == 0,
              "a read-only folder: saved, no backup, the reason is told: %s",
              err);
        chmod(at(BACKUP_FOLDER), 0755);

        /* A directory that cannot be written. */
        fresh();
        put("a.doc", "v1");
        chmod(dir, 0555);
        err[0] = '\0';
        CHECK(backup_write(at("a.doc"), "v2", 2, 2, &un, err, sizeof err) ==
                      0 &&
                  un == 2 && err[0] != '\0' && holds("a.doc", "v2"),
              "a read-only directory: saved in place, the reason is told");
        chmod(dir, 0755);
    }

    /* The files that earlier versions left beside the document stay. */
    fresh();
    put("a.doc", "d");
    put("a.bak01", "old");
    CHECK(save("a.doc", "new", 2, NULL) == 0 && holds("a.bak01", "old") &&
              holds(".backup/a.bak01", "d") && entries() == 3,
          "an old backup beside the document is not moved or removed");

    umask(old_mask);
}


/* The list of the backups of a document. */
static void
test_list(void)
{
    BackupInfo v[20];
    struct utimbuf tm = {0, 0};

    fresh();
    put("a.doc", "v1");

    for (int i = 2; i <= 4; i++) {
        char t[16];

        snprintf(t, sizeof t, "v%d", i);
        save("a.doc", t, 3, NULL);
    }

    put("a.bak01", "o1");
    put("a.bak02", "o22");
    tm.actime = tm.modtime = 1000000000;
    utime(at(".backup/a.bak02"), &tm);

    int n = backup_list(at("a.doc"), v, 20);

    CHECK(n == 5, "three backups in the folder and two old ones: %d", n);
    CHECK(n == 5 && v[0].n == 1 && v[0].old == 0 && v[1].n == 2 &&
              v[2].n == 3 && v[2].old == 0 && v[3].n == 1 && v[3].old == 1 &&
              v[4].n == 2 && v[4].old == 1,
          "the folder first, newest first, then the old ones");
    CHECK(n == 5 && v[1].when == 1000000000 && v[0].size == 2 &&
              v[4].size == 3 && strstr(v[0].path, "/.backup/a.bak01") != NULL,
          "the time stamp, the size and the path of each");
    CHECK(backup_list(at("none.doc"), v, 20) == 0, "no backups: an empty list");
    CHECK(backup_list(at("a.doc"), v, 2) == 2, "no more than the room");
}


/* The BackupInfo of backup n, in the folder or beside the document. */
static BackupInfo
info_of(const char *name, int n, int old)
{
    BackupInfo v[200];
    BackupInfo none;
    int k = backup_list(at(name), v, 200);

    memset(&none, 0, sizeof none);

    for (int i = 0; i < k; i++) {
        if (v[i].n == n && v[i].old == old) {
            return v[i];
        }
    }

    return none;
}


static int
restore(const char *name, int n, int old, int depth, int *same, char *err,
        size_t errlen)
{
    BackupInfo b = info_of(name, n, old);

    return backup_restore(at(name), &b, depth, same, err, errlen);
}


/* True when some file of the folder holds exactly the text. */
static int
folder_has(const char *text)
{
    char path[1024];
    DIR *d;
    int found = 0;

    snprintf(path, sizeof path, "%s/%s", dir, BACKUP_FOLDER);
    d = opendir(path);

    if (d == NULL) {
        return 0;
    }

    for (struct dirent *e = readdir(d); e != NULL; e = readdir(d)) {
        char sub[1200];
        char name[1300];

        if (e->d_name[0] == '.' && (e->d_name[1] == '\0' ||
                                    (e->d_name[1] == '.' &&
                                     e->d_name[2] == '\0'))) {
            continue;
        }

        snprintf(sub, sizeof sub, "%s/%s", BACKUP_FOLDER, e->d_name);
        snprintf(name, sizeof name, "%s", sub);

        if (holds(name, text)) {
            found = 1;
        }
    }

    closedir(d);

    return found;
}


/* The restore: a swap of the document and a backup. */
static void
test_restore(void)
{
    char err[400] = "";
    int same = 0;
    struct stat st;

    /* One backup kept: the document and the backup change names. */
    fresh();
    put("a.doc", "d");
    save("a.doc", "new", 1, NULL);
    CHECK(holds("a.doc", "new") && holds(".backup/a.bak01", "d"),
          "the situation before the swap");
    CHECK(restore("a.doc", 1, 0, 1, &same, err, sizeof err) == 0 && same == 0 &&
              holds("a.doc", "d") && holds(".backup/a.bak01", "new") &&
              entries() == 2 && count_in(BACKUP_FOLDER) == 1,
          "N = 1: a swap, nothing is left over: %s", err);

    /* A restore twice is the swap back. */
    CHECK(restore("a.doc", 1, 0, 1, &same, err, sizeof err) == 0 &&
              holds("a.doc", "new") && holds(".backup/a.bak01", "d"),
          "the same restore again swaps back");

    /* The backup below the first: the ones above it move up by one. */
    fresh();
    put("a.doc", "d");
    put(".backup/a.bak01", "b1");
    put(".backup/a.bak02", "b2");
    put(".backup/a.bak03", "b3");
    CHECK(restore("a.doc", 2, 0, 3, &same, err, sizeof err) == 0 &&
              holds("a.doc", "b2") && holds(".backup/a.bak01", "d") &&
              holds(".backup/a.bak02", "b1") && holds(".backup/a.bak03", "b3") &&
              count_in(BACKUP_FOLDER) == 3 && entries() == 2,
          "backup 2: the document is backup 1, backup 1 is 2, 3 stays: %s",
          err);

    /* The same text: nothing is done. */
    fresh();
    put("a.doc", "x");
    put(".backup/a.bak01", "x");
    put(".backup/a.bak02", "y");
    CHECK(restore("a.doc", 1, 0, 2, &same, err, sizeof err) == 0 && same == 1 &&
              holds(".backup/a.bak02", "y") && count_in(BACKUP_FOLDER) == 2,
          "a backup with the text of the document: nothing is done");

    /* A backup that lies beside the document. */
    fresh();
    put("a.doc", "d");
    put("a.bak01", "old1");
    put(".backup/a.bak01", "n1");
    CHECK(restore("a.doc", 1, 1, 2, &same, err, sizeof err) == 0 &&
              holds("a.doc", "old1") && holds(".backup/a.bak01", "d") &&
              holds(".backup/a.bak02", "n1") && there("a.bak01") == 0,
          "an old backup: it becomes the document and is not kept twice: %s",
          err);

    /* The mode of the document is kept. */
    fresh();
    put("a.doc", "d");
    put(".backup/a.bak01", "b1");
    chmod(at("a.doc"), 0640);
    chmod(at(".backup/a.bak01"), 0600);
    restore("a.doc", 1, 0, 1, &same, err, sizeof err);
    CHECK(stat(at("a.doc"), &st) == 0 && (st.st_mode & 0777) == 0640 &&
              holds("a.doc", "b1"),
          "the document keeps its mode: %o", st.st_mode & 0777);

    /* A number kept that is lower: the backups above it go. */
    fresh();
    put("a.doc", "d");

    for (int i = 1; i <= 5; i++) {
        char nm[32];
        char tx[16];

        snprintf(nm, sizeof nm, ".backup/a.bak%02d", i);
        snprintf(tx, sizeof tx, "b%d", i);
        put(nm, tx);
    }

    CHECK(restore("a.doc", 1, 0, 2, &same, err, sizeof err) == 0 &&
              holds("a.doc", "b1") && holds(".backup/a.bak01", "d") &&
              holds(".backup/a.bak02", "b2") && there(".backup/a.bak03") == 0 &&
              there(".backup/a.bak05") == 0,
          "a restore removes the backups above the number kept: %s", err);

    /* The number kept is 0: a restore still keeps the document. */
    fresh();
    put("a.doc", "d");
    put(".backup/a.bak01", "b1");
    CHECK(restore("a.doc", 1, 0, 0, &same, err, sizeof err) == 0 &&
              holds("a.doc", "b1") && holds(".backup/a.bak01", "d"),
          "N = 0: the swap is made all the same, no text is lost");

    /* The document is gone: the backup is copied. */
    fresh();
    put(".backup/a.bak01", "b1");
    CHECK(restore("a.doc", 1, 0, 1, &same, err, sizeof err) == 0 &&
              holds("a.doc", "b1") && holds(".backup/a.bak01", "b1"),
          "a document that is gone: the backup is copied");

    /* A failure after each step: nothing is lost. */
    for (int step = 1; step <= 5; step++) {
        fresh();
        put("a.doc", "d");
        put(".backup/a.bak01", "b1");
        put(".backup/a.bak02", "b2");
        put(".backup/a.bak03", "b3");
        backup_fault = step;
        err[0] = '\0';

        int r = restore("a.doc", 2, 0, 3, &same, err, sizeof err);

        backup_fault = 0;
        CHECK(r != 0 && err[0] != '\0', "step %d fails with a message: %s",
              step, err);

        if (step <= 3) {
            CHECK(holds("a.doc", "d") && holds(".backup/a.bak01", "b1") &&
                      holds(".backup/a.bak02", "b2") &&
                      holds(".backup/a.bak03", "b3") &&
                      count_in(BACKUP_FOLDER) == 3 && entries() == 2,
                  "step %d: the document and every backup are as they were, "
                  "no file is left over (%d in the folder)",
                  step, count_in(BACKUP_FOLDER));
        } else {
            CHECK(holds("a.doc", "b2") && folder_has("d") &&
                      folder_has("b1") &&
                      (step == 5 || folder_has("b2")) &&
                      folder_has("b3") && strstr(err, ".swap") != NULL,
                  "step %d: every text is still in a file, and the message "
                  "names the file of the old document: %s",
                  step, err);
        }
    }
}


/* The words of the box that asks before the number is lowered. */
static void
test_lowering(void)
{
    char t[600];

    backup_lowering_text(5, 2, t, sizeof t);
    CHECK(strstr(t, "Backups Kept: 5 to 2.") != NULL &&
              strstr(t, "numbered 3, 4 and 5 will be removed") != NULL &&
              strstr(t, "every folder .backup") != NULL &&
              strstr(t, "next time each document is saved with a backup") !=
                  NULL &&
              strstr(t, "restored") != NULL &&
              strstr(t, "Yes keeps the change, No keeps 5.") != NULL,
          "from 5 to 2: %s", t);
    backup_lowering_text(3, 2, t, sizeof t);
    CHECK(strstr(t, "The backup numbered 3 will be removed") != NULL &&
              strstr(t, "No keeps 3.") != NULL,
          "from 3 to 2: %s", t);
    backup_lowering_text(4, 2, t, sizeof t);
    CHECK(strstr(t, "numbered 3 and 4 will be removed") != NULL,
          "from 4 to 2: %s", t);
    backup_lowering_text(99, 2, t, sizeof t);
    CHECK(strstr(t, "numbered 3 to 99 will be removed") != NULL,
          "from 99 to 2: %s", t);
    backup_lowering_text(5, 0, t, sizeof t);
    CHECK(strstr(t, "No more backups will be made") != NULL &&
              strstr(t, "not touched") != NULL &&
              strstr(t, "removed") == NULL,
          "to 0: nothing is removed: %s", t);
}


/* Write a settings file and read it. */
static Config
settings(const char *text)
{
    Config c;
    FILE *f = fopen(at("config"), "w");

    fputs(text, f);
    fclose(f);
    chmod(at("config"), 0600);
    config_load(&c);

    return c;
}


static void
test_settings(void)
{
    Config c;
    char err[200];
    char path[1024];
    char text[2048];

    fresh();
    chmod(dir, 0700);
    setenv("STRATA_HOME", dir, 1);

    c = settings("");
    CHECK(c.backups == 3, "the default is three backups: %d", c.backups);
    CHECK(BACKUP_DEFAULT == 3 && CONFIG_BACKUPS_DEFAULT == 3,
          "the constant of the default is three");
    /*
     * The old key backup is not read: the old program wrote every key, the
     * default (no) included, so a no may never have been chosen.  A yes was
     * a wish for backups, which the default of three keeps.
     */
    c = settings("backup=yes\n");
    CHECK(c.backups == 3, "the old yes is the default of three: %d",
          c.backups);
    c = settings("backup=no\n");
    CHECK(c.backups == 3, "the old no is not a choice: the default of three "
                          "stays: %d",
          c.backups);
    c = settings("workdir=\nstred=\nhelp=\nautosave=yes\n"
                 "autosave_seconds=120\nexpanded_status=no\n"
                 "onscreen_help=yes\nshow_marks=yes\nshow_dots=no\n"
                 "backup=no\nprinter=\n");
    CHECK(c.backups == 3 && c.autosave == 1 && c.autosave_seconds == 120,
          "a whole file of the old program keeps the defaults: %d",
          c.backups);
    c = settings("backups=0\n");
    CHECK(c.backups == 0, "a number that is 0 is a choice and stays: %d",
          c.backups);
    c = settings("backup=no\nbackups=0\n");
    CHECK(c.backups == 0, "an explicit 0 stays beside the old key: %d",
          c.backups);
    c = settings("backups=3\n");
    CHECK(c.backups == 3, "the number is read");
    c = settings("backup=no\nbackups=4\n");
    CHECK(c.backups == 4, "the number wins over the old key");
    c = settings("backups=4\nbackup=no\n");
    CHECK(c.backups == 4, "the number wins, also when it comes first");
    c = settings("backups=abc\n");
    CHECK(c.backups == 3, "text that is not a number keeps the default 3");
    c = settings("backups=500\n");
    CHECK(c.backups == 99, "a number above 99 is made 99");
    CHECK(config_clamp_backups(-3) == 0 && config_clamp_backups(100) == 99 &&
              config_clamp_backups(7) == 7,
          "the one rule for the number");

    /* The macro set and the library that the Editor loads at the start. */
    c = settings("initial_macros=/m/names.mac\ninitial_library=lib\n");
    CHECK(strcmp(c.initial_macros, "/m/names.mac") == 0 &&
              strcmp(c.initial_library, "lib") == 0,
          "initial_macros and initial_library are read: %s %s",
          c.initial_macros, c.initial_library);
    c = settings("");
    CHECK(c.initial_macros[0] == '\0' && c.initial_library[0] == '\0',
          "they are empty by default");
    c = settings("initial_macros=a.mac\ninitial_library=b.lib\n");
    config_save(&c, err, sizeof err);
    {
        FILE *g = fopen(at("config"), "r");
        size_t n = g != NULL ? fread(text, 1, sizeof text - 1, g) : 0;

        if (g != NULL) {
            fclose(g);
        }

        text[n] = '\0';
        CHECK(strstr(text, "\ninitial_macros=a.mac\n") != NULL &&
                  strstr(text, "\ninitial_library=b.lib\n") != NULL,
              "they are written");
    }

    c = settings("initial_macros=a.mac\n");
    c.initial_macros[0] = '\0';
    config_save(&c, err, sizeof err);
    {
        FILE *g = fopen(at("config"), "r");
        size_t n = g != NULL ? fread(text, 1, sizeof text - 1, g) : 0;

        if (g != NULL) {
            fclose(g);
        }

        text[n] = '\0';
        CHECK(strstr(text, "initial_macros") == NULL,
              "an empty one is written as no line");
    }

    /*
     * An old file: printer=Open to print (the words of the old choice) and
     * a PDF viewer that is only a name.  The choice is read as the viewer
     * mode, the name is not used, and the file is saved with print_to=viewer
     * and no pdf_viewer line.
     */
    c = settings("printer=Open to print\npdf_viewer=evince\nbackup=no\n");
    CHECK(config_print_to(&c) == CONFIG_PRINT_OPEN,
          "old words: the viewer mode: %d",
          config_print_to(&c));
    CHECK(c.pdf_viewer[0] == '\0', "a bare name is not used: [%s]",
          c.pdf_viewer);
    CHECK(strcmp(c.pdf_viewer_old, "evince") == 0,
          "the old name is kept for Setup to say so: [%s]", c.pdf_viewer_old);
    /* Setup, when it is accepted, sets print_to from the choice it shows. */
    str_copy(c.print_to, sizeof c.print_to, "viewer");
    c.printer[0] = '\0';
    CHECK(config_save(&c, err, sizeof err) == 0, "save: %s", err);
    {
        FILE *g = fopen(at("config"), "r");
        size_t n = g != NULL ? fread(text, 1, sizeof text - 1, g) : 0;

        if (g != NULL) {
            fclose(g);
        }

        text[n] = '\0';
        CHECK(strstr(text, "\nprint_to=viewer\n") != NULL &&
                  strstr(text, "pdf_viewer") == NULL &&
                  strstr(text, "print_to=open") == NULL &&
                  strstr(text, "\nbackup") == NULL,
              "saved as viewer, with no pdf_viewer line: %s", text);
    }

    c = settings("print_to=open\npdf_viewer=./show\n");
    CHECK(config_print_to(&c) == CONFIG_PRINT_OPEN && c.pdf_viewer[0] == '\0',
          "the older word open, and a relative path, are read the same way");

    /* The key is written, and the old one is not. */
    c = settings("backups=0\nautosave=no\n");
    c.backups_explicit = 1;
    CHECK(config_save(&c, err, sizeof err) == 0, "save: %s", err);
    str_copy(path, sizeof path, at("config"));

    FILE *f = fopen(path, "r");
    size_t k = f != NULL ? fread(text, 1, sizeof text - 1, f) : 0;

    if (f != NULL) {
        fclose(f);
    }

    text[k] = '\0';
    CHECK(strstr(text, "\nbackups=0\n") != NULL &&
              strstr(text, "\nbackup=") == NULL,
          "the new key is written, the old one is gone");

    c = settings("backups=3\n");
    config_save(&c, err, sizeof err);
    f = fopen(path, "r");
    k = f != NULL ? fread(text, 1, sizeof text - 1, f) : 0;

    if (f != NULL) {
        fclose(f);
    }

    text[k] = '\0';
    CHECK(strstr(text, "backups=") == NULL,
          "the default is saved as no line");

    c = settings("backups=1\n");
    config_save(&c, err, sizeof err);
    f = fopen(path, "r");
    k = f != NULL ? fread(text, 1, sizeof text - 1, f) : 0;

    if (f != NULL) {
        fclose(f);
    }

    text[k] = '\0';
    CHECK(strstr(text, "backups=1") != NULL,
          "one backup, not the default, is saved as a line");
}


/* The microseconds on a clock that runs on, to time a save. */
static long long
now_us(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);

    return t.tv_sec * 1000000LL + t.tv_nsec / 1000;
}


/* The time of a save of the file, changed each time, with depth backups. */
static void
measure(const char *file)
{
    FILE *f = fopen(file, "rb");
    size_t cap = 64u << 20;
    uint8_t *buf = malloc(cap);
    size_t len = f != NULL ? fread(buf, 1, cap, f) : 0;
    char err[200];
    const int depths[3] = {0, 1, 99};

    if (f != NULL) {
        fclose(f);
    }

    fresh();
    printf("document of %zu bytes\n", len);

    for (int k = 0; k < 3; k++) {
        int saves = depths[k] == 99 ? 120 : 40;
        long long t0;
        long long per;

        backup_write(at("m.doc"), buf, len, 0, NULL, err, sizeof err);
        t0 = now_us();

        for (int i = 0; i < saves; i++) {
            buf[len - 1]++;
            backup_write(at("m.doc"), buf, len, depths[k], NULL, err,
                         sizeof err);
        }

        per = (now_us() - t0) / saves;
        printf("%2d backups: %lld.%01lld ms per save (%d saves)\n", depths[k],
               per / 1000, (per % 1000) / 100, saves);
    }

    free(buf);
}


int
main(int argc, char **argv)
{
    char tmpl[PATH_MAX];
    char *made;

    tmp_path(tmpl, sizeof tmpl, "backuptest-XXXXXX");
    made = mkdtemp(tmpl);

    if (made == NULL) {
        printf("FAIL: cannot make a directory\n");

        return 1;
    }

    str_copy(root, sizeof root, made);

    if (argc == 3 && strcmp(argv[1], "--measure") == 0) {
        measure(argv[2]);
        argc = 0;
        chmod(root, 0755);

        char c2[300];

        snprintf(c2, sizeof c2, "rm -rf '%.250s'", root);

        return system(c2) != 0;
    }

    test_names();
    test_series();
    test_gap_and_depth();
    test_unchanged();
    test_failures();
    test_file_state();
    test_folder();
    test_list();
    test_restore();
    test_lowering();
    test_settings();

    /* The directory is made by this test: remove it and what it holds. */
    chmod(root, 0755);

    char cmd[300];

    snprintf(cmd, sizeof cmd, "rm -rf '%.250s'", root);

    if (system(cmd) != 0) {
        printf("note: the directory %s was not removed\n", root);
    }

    printf("backuptest: %d checks, %d failures\n", checks, fails);

    return fails != 0 ? 1 : 0;
}
