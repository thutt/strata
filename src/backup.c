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

/* backup.c - the safe write of a document and its numbered backups. */
#include "backup.h"
#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>


/*
 * Split a path into its directory (without the last slash; "." when there
 * is none) and the name of the file.
 */
static void
split_path(const char *path, char *dir, size_t dcap, const char **base)
{
    const char *slash = strrchr(path, '/');

    if (slash == NULL) {
        str_copy(dir, dcap, ".");
        *base = path;
    } else if (slash == path) {
        str_copy(dir, dcap, "/");
        *base = slash + 1;
    } else {
        size_t n = lsz(slash - path);

        if (n >= dcap) {
            n = dcap - 1;
        }

        memcpy(dir, path, n);
        dir[n] = '\0';
        *base = slash + 1;
    }
}


/* The name of the file of backup n: report.bak01, or report.BAK01. */
static int
file_name_of(const char *base, int n, char *out, size_t cap)
{
    size_t len = strlen(base);
    size_t stem = len;
    const char *ext = ".bak";

    if (n < 1 || n > BACKUP_MAX) {
        return -1;
    }

    if (len >= 4 && strcasecmp(base + len - 4, ".doc") == 0) {
        stem = len - 4;
        ext = base[len - 1] == 'C' ? ".BAK" : ".bak";
    }

    char tail[8];
    int w = snprintf(tail, sizeof tail, "%s%02d", ext, n);

    /* The name of the file alone must not be longer than a file system
       allows. */
    if (w < 0 || stem + usz(w) >= cap || stem + usz(w) > 255) {
        return -1;
    }

    memcpy(out, base, stem);
    memcpy(out + stem, tail, usz(w) + 1);

    return 0;
}


int
backup_dir(const char *path, char *out, size_t cap)
{
    char dir[PATH_MAX];
    const char *base;
    int w;

    split_path(path, dir, sizeof dir, &base);

    if (strcmp(dir, ".") == 0) {
        w = snprintf(out, cap, "%s", BACKUP_FOLDER);
    } else if (strcmp(dir, "/") == 0) {
        w = snprintf(out, cap, "/%s", BACKUP_FOLDER);
    } else {
        w = snprintf(out, cap, "%s/%s", dir, BACKUP_FOLDER);
    }

    return w < 0 || usz(w) >= cap ? -1 : 0;
}


int
backup_name(const char *path, int n, char *out, size_t cap)
{
    char folder[PATH_MAX];
    char file[300];
    const char *base;
    char d[PATH_MAX];

    split_path(path, d, sizeof d, &base);

    if (file_name_of(base, n, file, sizeof file) != 0 ||
        backup_dir(path, folder, sizeof folder) != 0) {
        return -1;
    }

    int w = snprintf(out, cap, "%s/%s", folder, file);

    return w < 0 || usz(w) >= cap ? -1 : 0;
}


int
backup_old_name(const char *path, int n, char *out, size_t cap)
{
    char dir[PATH_MAX];
    char file[300];
    const char *base;
    int w;

    split_path(path, dir, sizeof dir, &base);

    if (file_name_of(base, n, file, sizeof file) != 0) {
        return -1;
    }

    if (strcmp(dir, ".") == 0 && strchr(path, '/') == NULL) {
        w = snprintf(out, cap, "%s", file);
    } else if (strcmp(dir, "/") == 0) {
        w = snprintf(out, cap, "/%s", file);
    } else {
        w = snprintf(out, cap, "%s/%s", dir, file);
    }

    return w < 0 || usz(w) >= cap ? -1 : 0;
}


/* Does a file or a link of this name exist? */
static int
exists(const char *path)
{
    struct stat st;

    return lstat(path, &st) == 0;
}


/* The step of a restore that fails on purpose in a test (backup.h). */
int backup_fault;

#define FAULT(step) (backup_fault == (step) && (errno = EIO) != 0)


/*
 * Make sure that the folder .backup next to the document is there and can be
 * used.  It is made when the first backup is made, with the mode that any
 * new folder gets (0777 and the umask of the user).  Returns 0, or -1 with
 * the reason in why: a file with that name is in the way, the folder cannot
 * be made, or it cannot be written.
 */
static int
ensure_folder(const char *path, char *why, size_t wcap)
{
    char f[PATH_MAX];
    char probe[PATH_MAX];
    struct stat st;

    /* The name of a backup is the name of the document with .bak01 to
       .bak99 for the end, and no name of a file may be longer than 255
       bytes, so a long name has no backups. */
    if (backup_name(path, 1, probe, sizeof probe) != 0) {
        snprintf(why, wcap, "the name is too long for a backup");

        return -1;
    }

    if (backup_dir(path, f, sizeof f) != 0) {
        snprintf(why, wcap, "the name of the folder %s is too long",
                 BACKUP_FOLDER);

        return -1;
    }

    if (stat(f, &st) == 0) {
        if (S_ISDIR(st.st_mode) == 0) {
            snprintf(why, wcap, "a file named %s is in the way", BACKUP_FOLDER);

            return -1;
        }
    } else if (mkdir(f, 0777) != 0 && errno != EEXIST) {
        snprintf(why, wcap, "cannot make the folder %s: %s", BACKUP_FOLDER,
                 strerror(errno));

        return -1;
    }

    if (access(f, W_OK | X_OK) != 0) {
        snprintf(why, wcap, "the folder %s cannot be written", BACKUP_FOLDER);

        return -1;
    }

    return 0;
}


/* Put the text of a failure in err: what was being done and why not. */
static void
fail(char *err, size_t errlen, const char *what, int code)
{
    if (err == NULL || errlen == 0) {
        return;
    }

    str_copy(err, errlen, what);

    size_t used = strlen(err);

    if (used + 3 < errlen) {
        memcpy(err + used, ": ", 3);
        str_copy(err + used + 2, errlen - used - 2, strerror(code));
    }
}


/*
 * Make the name of the file that a write goes to first: a short name in the
 * directory of the file, made of .tmp, the number of the process and a
 * number made from the whole path.  It is short whatever the name of the
 * document is, so that a name that is as long as a file system allows can be
 * written; two paths of one process get two names.  Returns 0, or -1 when
 * the directory does not fit.
 */
static int
temp_name(char *out, size_t cap, const char *path)
{
    char dir[PATH_MAX];
    const char *base;
    char tag[40];
    uint32_t h = 2166136261u;
    int w;

    split_path(path, dir, sizeof dir, &base);

    for (const char *p = path; *p != '\0'; p++) {
        h = (h ^ u8(*p)) * 16777619u;
    }

    w = snprintf(tag, sizeof tag, ".tmp%d-%08lx", getpid(), (unsigned long)h);

    if (w < 0) {
        return -1;
    }

    const char *where = dir;
    size_t room = strlen(dir) + 1 + strlen(tag) + 1;

    /* The file is in the directory of the document: no directory when the
       path has none, the root without a second slash. */
    if (strcmp(dir, ".") == 0 && strchr(path, '/') == NULL) {
        where = "";
        room = strlen(tag) + 1;
    } else if (strcmp(dir, "/") == 0) {
        where = "";
        room = strlen(tag) + 2;
    }

    if (room > cap) {
        return -1;
    }

    memcpy(out, where, strlen(where));
    out[strlen(where)] = '\0';

    if (where[0] != '\0' || strcmp(dir, "/") == 0) {
        strcat(out, "/");
    }

    strcat(out, tag);

    return 0;
}


/* Does the file hold exactly the len bytes of buf? */
static int
same_content(const char *path, const uint8_t *buf, size_t len)
{
    FILE *f = fopen(path, "rb");
    uint8_t chunk[65536];
    size_t off = 0;
    int same = 1;

    if (f == NULL) {
        return 0;
    }

    for (;;) {
        size_t k = fread(chunk, 1, sizeof chunk, f);

        if (k == 0) {
            break;
        }

        if (k > len - off || memcmp(chunk, buf + off, k) != 0) {
            same = 0;
            break;
        }

        off += k;
    }

    if (ferror(f) != 0 || (same != 0 && off != len)) {
        same = 0;
    }

    fclose(f);

    return same;
}


/* Copy the file src to dst, through a file of its own that is renamed. */
static int
copy_file(const char *src, const char *dst, int *code)
{
    char tmp[PATH_MAX];
    struct stat st;
    uint8_t chunk[65536];
    int ok = 1;

    if (temp_name(tmp, sizeof tmp, dst) != 0) {
        *code = ENAMETOOLONG;

        return -1;
    }

    FILE *in = fopen(src, "rb");

    if (in == NULL) {
        *code = errno;

        return -1;
    }

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    FILE *out = fd >= 0 ? fdopen(fd, "wb") : NULL;

    if (out == NULL) {
        *code = errno;

        if (fd >= 0) {
            close(fd);
        }

        fclose(in);

        return -1;
    }

    for (;;) {
        size_t k = fread(chunk, 1, sizeof chunk, in);

        if (k == 0) {
            break;
        }

        if (fwrite(chunk, 1, k, out) != k) {
            ok = 0;
            break;
        }
    }

    ok = ok != 0 && ferror(in) == 0;

    if (ok != 0 && stat(src, &st) == 0 && fchmod(fd, st.st_mode & 07777) != 0) {
        ok = 0;
    }

    ok = ok != 0 && fsync(fd) == 0;
    ok = fclose(out) == 0 && ok != 0;
    fclose(in);

    if (ok != 0 && rename(tmp, dst) != 0) {
        ok = 0;
    }

    if (ok == 0) {
        *code = errno != 0 ? errno : EIO;
        unlink(tmp);

        return -1;
    }

    return 0;
}


/*
 * Make backup 1 of the document: a hard link to the old file, which costs
 * nothing, or a copy where the file system has no links.  The old file
 * stays in its place, so the document is whole until the new file is
 * renamed over it.
 */
static int
keep_old_as_backup_1(const char *path, const char *bak, int *code)
{
    if (link(path, bak) == 0) {
        return 0;
    }

    if (errno == EEXIST) {
        *code = errno;

        return -1;
    }

    return copy_file(path, bak, code);
}


/*
 * Move the numbered backups up by one so that number 1 is free: if all of
 * 1 to depth are there, the oldest is removed first; if one is missing, the
 * ones below it move up into the gap and nothing is removed.
 */
static int
shift_backups(const char *path, int depth, char *err, size_t errlen)
{
    char from[PATH_MAX];
    char to[PATH_MAX];
    int gap = 0;

    for (int i = 1; i <= depth && gap == 0; i++) {
        if (backup_name(path, i, from, sizeof from) != 0) {
            fail(err, errlen, "the name of the backup is too long",
                 ENAMETOOLONG);

            return -1;
        }

        if (exists(from) == 0) {
            gap = i;
        }
    }

    if (gap == 0) {
        /* Every backup is there: the oldest is dropped. */
        if (backup_name(path, depth, from, sizeof from) != 0 ||
            unlink(from) != 0) {
            fail(err, errlen, "cannot remove the oldest backup", errno);

            return -1;
        }

        gap = depth;
    }

    for (int i = gap - 1; i >= 1; i--) {
        if (backup_name(path, i, from, sizeof from) != 0 ||
            backup_name(path, i + 1, to, sizeof to) != 0 ||
            rename(from, to) != 0) {
            fail(err, errlen, "cannot move a backup", errno);

            return -1;
        }
    }

    return 0;
}


/* Remove the backups above depth, after a successful write. */
static void
remove_extra_backups(const char *path, int depth)
{
    char name[PATH_MAX];

    for (int i = depth + 1; i <= BACKUP_MAX; i++) {
        if (backup_name(path, i, name, sizeof name) == 0 && exists(name) != 0) {
            unlink(name);
        }
    }
}


/* Write the file in place: the way for a directory that cannot be written. */
static int
write_in_place(const char *path, const uint8_t *buf, size_t len, char *err,
               size_t errlen)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL) {
        snprintf(err, errlen, "cannot write file");

        return -1;
    }

    int ok = fwrite(buf, 1, len, f) == len;

    ok = fclose(f) == 0 && ok != 0;

    if (ok == 0) {
        snprintf(err, errlen, "write error");

        return -1;
    }

    return 0;
}


int
backup_is_backup_file(const char *path)
{
    const char *base = strrchr(path, '/');
    const char *dot;
    size_t n;

    if (base != NULL) {
        /* A file in a folder named .backup is a backup, whatever its name. */
        const char *q = base;

        while (q > path && q[-1] != '/') {
            q--;
        }

        if (strncmp(q, ".backup/", 8) == 0) {
            return 1;
        }

        base++;
    } else {
        base = path;
    }

    dot = strrchr(base, '.');

    if (dot == NULL) {
        return 0;
    }

    if (strncasecmp(dot, ".bak", 4) != 0) {
        return 0;
    }

    for (n = 4; dot[n] != '\0'; n++) {
        if (dot[n] < '0' || dot[n] > '9') {
            return 0;
        }
    }

    return 1;
}


int
backup_write(const char *path, const void *data, size_t len, int depth,
             int *unchanged, char *err, size_t errlen)
{
    const uint8_t *buf = data;
    char real[PATH_MAX];
    char tmp[PATH_MAX];
    char bak[PATH_MAX];
    struct stat st;
    int have = 0;

    if (unchanged != NULL) {
        *unchanged = 0;
    }

    if (backup_is_backup_file(path) != 0) {
        snprintf(err, errlen, "%s", BACKUP_REFUSED_MSG);

        return -1;
    }

    if (depth < 0) {
        depth = 0;
    }

    if (depth > BACKUP_MAX) {
        depth = BACKUP_MAX;
    }

    /* A link to a document is followed: the file itself is replaced. */
    if (realpath(path, real) == NULL) {
        str_copy(real, sizeof real, path);
    }

    if (stat(real, &st) == 0) {
        if (S_ISREG(st.st_mode) == 0) {
            snprintf(err, errlen, "cannot write file: not a regular file");

            return -1;
        }

        have = 1;
    }

    if (depth > 0 && have != 0 && same_content(real, buf, len) != 0) {
        if (unchanged != NULL) {
            *unchanged = 1;
        }

        return 0;
    }

    if (temp_name(tmp, sizeof tmp, real) != 0) {
        snprintf(err, errlen, "cannot write file");

        return -1;
    }

    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);

    if (fd < 0 && errno == EEXIST) {
        unlink(tmp);
        fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);
    }

    if (fd < 0) {
        /* A directory that cannot be written, but a file that can be. */
        if (have != 0 && access(real, W_OK) == 0) {
            int r = write_in_place(real, buf, len, err, errlen);

            if (r == 0 && depth > 0 && unchanged != NULL) {
                *unchanged = 2;
                snprintf(err, errlen, "the directory cannot be written");
            }

            return r;
        }

        snprintf(err, errlen, "cannot write file");

        return -1;
    }

    FILE *f = fdopen(fd, "wb");
    int ok = f != NULL && fwrite(buf, 1, len, f) == len;

    if (ok != 0 && have != 0) {
        /* The new file has the permissions of the old one. */
        ok = fchmod(fd, st.st_mode & 07777) == 0 || errno == EPERM;
    }

    ok = ok != 0 && fflush(f) == 0 && fsync(fd) == 0;

    if (f != NULL) {
        ok = fclose(f) == 0 && ok != 0;
    } else {
        close(fd);
    }

    if (ok == 0) {
        unlink(tmp);
        snprintf(err, errlen, "write error");

        return -1;
    }

    int made = 0;
    char why[200] = "";

    if (depth > 0 && have != 0 && ensure_folder(real, why, sizeof why) != 0) {
        /* The document is saved, with no backup, and the caller says why. */
        depth = 0;

        if (unchanged != NULL) {
            *unchanged = 2;
        }
    }

    if (depth > 0 && have != 0) {
        int code = 0;

        if (shift_backups(real, depth, err, errlen) != 0) {
            unlink(tmp);

            return -1;
        }

        if (backup_name(real, 1, bak, sizeof bak) != 0) {
            unlink(tmp);
            fail(err, errlen, "the name of the backup is too long",
                 ENAMETOOLONG);

            return -1;
        }

        if (keep_old_as_backup_1(real, bak, &code) != 0) {
            unlink(tmp);
            fail(err, errlen, "cannot make the backup file", code);

            return -1;
        }

        made = 1;
    }

    if (rename(tmp, real) != 0) {
        int code = errno;

        unlink(tmp);
        fail(err, errlen, "cannot write file", code);

        return -1;
    }

    if (made != 0) {
        remove_extra_backups(real, depth);
    }

    if (why[0] != '\0') {
        str_copy(err, errlen, why);
    }

    return 0;
}


/* ----------------------------------------------------------------- list */
int
backup_list(const char *path, BackupInfo *v, int cap)
{
    char real[PATH_MAX];
    int n = 0;

    if (realpath(path, real) == NULL) {
        str_copy(real, sizeof real, path);
    }

    for (int pass = 0; pass < 2; pass++) {
        for (int i = 1; i <= BACKUP_MAX && n < cap; i++) {
            char name[PATH_MAX];
            struct stat st;
            int r = pass == 0 ? backup_name(real, i, name, sizeof name) :
                                backup_old_name(real, i, name, sizeof name);

            if (r != 0 || stat(name, &st) != 0 || S_ISREG(st.st_mode) == 0) {
                continue;
            }

            v[n].n = i;
            v[n].old = pass;
            v[n].special = 0;
            v[n].size = st.st_size;
            v[n].when = st.st_mtime;
            str_copy(v[n].path, sizeof v[n].path, name);
            n++;
        }
    }

    /* The special backups: V<version>-<name> beside the document. */
    {
        const char *slash = strrchr(real, '/');
        size_t dir = slash != NULL ? (size_t)(slash - real) + 1 : 0;

        for (int ver = 0; ver <= 99 && n < cap; ver++) {
            char name[2 * PATH_MAX + 16];
            struct stat st;
            int w = snprintf(name, sizeof name, "%.*sV%d-%s", (int)dir, real,
                             ver, real + dir);

            if (w < 0 || (size_t)w >= PATH_MAX || stat(name, &st) != 0 ||
                S_ISREG(st.st_mode) == 0) {
                continue;
            }

            v[n].n = 0;
            v[n].old = 2;
            v[n].special = ver;
            v[n].size = st.st_size;
            v[n].when = st.st_mtime;
            str_copy(v[n].path, sizeof v[n].path, name);
            n++;
        }
    }

    return n;
}


/* ------------------------------------------------------------- restore */
/* Do the two files hold the same bytes? */
static int
same_files(const char *a, const char *b)
{
    FILE *fa = fopen(a, "rb");
    FILE *fb = fopen(b, "rb");
    uint8_t ca[65536];
    uint8_t cb[65536];
    int same = fa != NULL && fb != NULL;

    while (same != 0) {
        size_t ka = fread(ca, 1, sizeof ca, fa);
        size_t kb = fread(cb, 1, sizeof cb, fb);

        if (ka != kb || memcmp(ca, cb, ka) != 0) {
            same = 0;
        } else if (ka == 0) {
            break;
        }
    }

    if (fa != NULL) {
        fclose(fa);
    }

    if (fb != NULL) {
        fclose(fb);
    }

    return same;
}


/* The name of the file in the folder that holds the old document. */
static int
swap_name(char *out, size_t cap, const char *folder)
{
    char tag[32];
    int w = snprintf(tag, sizeof tag, "/.swap%d", getpid());
    size_t len = strlen(folder);

    if (w < 0 || len + usz(w) >= cap) {
        return -1;
    }

    memcpy(out, folder, len);
    memcpy(out + len, tag, usz(w) + 1);

    return 0;
}


/* The present document becomes a file in the folder: a link, or a copy. */
static int
link_or_copy(const char *src, const char *dst, int *code)
{
    if (link(src, dst) == 0) {
        return 0;
    }

    if (errno == EEXIST) {
        *code = errno;

        return -1;
    }

    return copy_file(src, dst, code);
}


int
backup_restore(const char *path, const BackupInfo *b, int depth, int *same,
               char *err, size_t errlen)
{
    char real[PATH_MAX];
    char folder[PATH_MAX];
    char hold[PATH_MAX];
    char take[PATH_MAX];
    char bak1[PATH_MAX];
    char why[200];
    struct stat st;
    int code = 0;
    int have = 0;

    if (same != NULL) {
        *same = 0;
    }

    depth = depth < 1 ? 1 : depth > BACKUP_MAX ? BACKUP_MAX : depth;

    if (realpath(path, real) == NULL) {
        str_copy(real, sizeof real, path);
    }

    if (stat(real, &st) == 0) {
        if (S_ISREG(st.st_mode) == 0) {
            snprintf(err, errlen, "cannot restore: not a regular file");

            return -1;
        }

        have = 1;
    }

    if (have != 0 && same_files(real, b->path) != 0) {
        if (same != NULL) {
            *same = 1;
        }

        return 0;
    }

    if (ensure_folder(real, why, sizeof why) != 0) {
        snprintf(err, errlen, "cannot restore: %s", why);

        return -1;
    }

    backup_dir(real, folder, sizeof folder);
    if (swap_name(hold, sizeof hold, folder) != 0 ||
        backup_name(real, 1, bak1, sizeof bak1) != 0 ||
        temp_name(take, sizeof take, real) != 0) {
        fail(err, errlen, "the name of the backup is too long", ENAMETOOLONG);

        return -1;
    }

    /* Step 1: the present document gets a second name in the folder. */
    if (have != 0 && (FAULT(1) || link_or_copy(real, hold, &code) != 0)) {
        fail(err, errlen, "cannot keep the present document",
             code != 0 ? code : errno);

        return -1;
    }

    /*
     * Step 2: the backup is copied to a file next to the document, with the
     * permissions and the owner of the document as far as they can be kept.
     */
    if (FAULT(2) || copy_file(b->path, take, &code) != 0) {
        int e = code != 0 ? code : errno;

        if (have != 0) {
            unlink(hold);
        }

        fail(err, errlen, "cannot copy the backup", e);

        return -1;
    }

    if (have != 0) {
        if (chmod(take, st.st_mode & 07777) != 0 ||
            chown(take, st.st_uid, st.st_gid) != 0) {
            /* Not every user may do this: it is kept as far as possible. */
            errno = 0;
        }
    }

    /* Step 3: the copy replaces the document in one step. */
    if (FAULT(3) || rename(take, real) != 0) {
        int e = errno;

        unlink(take);

        if (have != 0) {
            unlink(hold);
        }

        fail(err, errlen, "cannot write the document", e);

        return -1;
    }

    if (have == 0) {
        return 0;
    }

    /*
     * From here the chosen text is the document and the old document is in
     * the file hold; every backup is still there.  Step 4: the numbered
     * backups below the chosen one move up by one (the chosen one is in the
     * document now, so its name is free); or, for a backup that lies beside
     * the document, all the numbered backups make room for number 1.
     */
    if (FAULT(4)) {
        snprintf(err, errlen, "cannot move the backups: %s; the document "
                 "that was replaced is the file %s", strerror(EIO), hold);

        return -1;
    }

    if (b->old == 0) {
        char from[PATH_MAX];
        char to[PATH_MAX];

        if (backup_name(real, b->n, from, sizeof from) == 0) {
            unlink(from);
        }

        for (int i = b->n - 1; i >= 1; i--) {
            if (backup_name(real, i, from, sizeof from) != 0 ||
                backup_name(real, i + 1, to, sizeof to) != 0 ||
                (exists(from) != 0 && rename(from, to) != 0)) {
                snprintf(err, errlen, "cannot move a backup: %s; the document "
                         "that was replaced is the file %s", strerror(errno),
                         hold);

                return -1;
            }
        }
    } else if (shift_backups(real, depth, why, sizeof why) != 0) {
        snprintf(err, errlen, "%s; the document that was replaced is the file "
                 "%s", why, hold);

        return -1;
    }

    /* Step 5: the old document is backup 1. */
    if (FAULT(5) || rename(hold, bak1) != 0) {
        snprintf(err, errlen, "cannot make backup 1: %s; the document that "
                 "was replaced is the file %s", strerror(errno), hold);

        return -1;
    }

    /* Step 6: a backup beside the document is in the document now; a
       special backup is permanent and stays. */
    if (b->old == 1) {
        unlink(b->path);
    }

    remove_extra_backups(real, depth);

    return 0;
}


/* ---------------------------------------------------------- lowering */
void
backup_lowering_text(int from, int to, char *out, size_t cap)
{
    char gone[96];
    int n = from - to;

    if (to == 0) {
        snprintf(out, cap, "Backups Kept: %d to 0.  No more backups will be "
                 "made.  The backups that exist are not touched.  Yes keeps "
                 "the change, No keeps %d.", from, from);

        return;
    }

    if (n == 1) {
        snprintf(gone, sizeof gone, "The backup numbered %d will be", from);
    } else if (n == 2) {
        snprintf(gone, sizeof gone, "The backups numbered %d and %d will be",
                 to + 1, from);
    } else if (n == 3) {
        snprintf(gone, sizeof gone, "The backups numbered %d, %d and %d will "
                 "be", to + 1, to + 2, from);
    } else {
        snprintf(gone, sizeof gone, "The backups numbered %d to %d will be",
                 to + 1, from);
    }

    snprintf(out, cap, "Backups Kept: %d to %d.  %s removed, in every folder "
             ".backup, the next time each document is saved with a backup or "
             "a backup is restored; no other time.  Yes keeps the change, No "
             "keeps %d.", from, to, gone, from);
}
