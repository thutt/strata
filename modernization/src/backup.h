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
 * backup.h - the safe write of a document and its numbered backups.
 *
 * A document is written to a file of its own in the same directory first.
 * Only when that file is complete are the backups moved up by one and the
 * old document kept as backup 1; last the new file is renamed over the
 * document.  At every moment a complete copy of the document is on the
 * disk, and a failing step leaves the old document and the backups as they
 * were or as they can be understood.  Backup 1 is always the newest.
 */
#ifndef BACKUP_H
#define BACKUP_H
#include <limits.h>
#include <stddef.h>
#include <time.h>

/* The folder, next to the document, that holds its backups. */
#define BACKUP_FOLDER ".backup"

/* The most numbered backups of a document, and the number a user gets. */
#define BACKUP_MAX 99
#define BACKUP_DEFAULT 3


/*
 * The folder of the backups of the document path: the folder .backup in the
 * directory of the document.  It is made when the first backup is made.
 * Returns 0, or -1 when the name does not fit out.
 */
int
backup_dir(const char *path, char *out, size_t cap);


/*
 * Make the name of backup number n (1 to BACKUP_MAX) of the document path:
 * the file is in the folder .backup, its name is the name of the document
 * without its extension .doc, then .bak and the number with two digits, such
 * as .backup/report.bak01 (the letters are capitals, report.BAK01, when the
 * extension of the document is written in capitals).  A name that does not
 * end in .doc gets .bak and the number added.  Returns 0, or -1 when the
 * name does not fit out or is longer than a file system allows.
 */
int
backup_name(const char *path, int n, char *out, size_t cap);


/*
 * Tell whether path names a backup file, which a Save must refuse to
 * overwrite: the extension of the file name is .bak or .bak followed by
 * digits (report.bak01), in capitals or lower case, or the file is in a
 * folder named .backup.  Returns 1 for a backup file, else 0.
 */
int
backup_is_backup_file(const char *path);


/* The text of the message that a refused Save gives (no file is written). */
#define BACKUP_REFUSED_MSG \
    "A backup file is not saved over: use Restore Backup or give another name"


/*
 * The name that earlier versions gave to backup n: the same file name in the
 * directory of the document itself, such as report.bak01.  The programs do
 * not make, move or remove these files; they only list them.
 */
int
backup_old_name(const char *path, int n, char *out, size_t cap);


/*
 * Write the len bytes of buf as the file path, as described above, with
 * depth numbered backups (0: none, and the old file is replaced).  When
 * depth is more than 0 and the file already holds exactly these bytes,
 * nothing is deleted, renamed or written and *unchanged is set to 1
 * (unchanged may be NULL); if the document is saved but no backup can be
 * made (a file named .backup is in the way, the folder cannot be made or
 * written, the directory cannot be written), *unchanged is set to 2, the
 * result is 0 and err says why.  Backups above depth are removed after a
 * successful write that made a backup.  In a directory that cannot be
 * written, a file that can be changed is written in place, without
 * backups.  Returns 0, or -1 with a message in err.
 */
int
backup_write(const char *path, const void *buf, size_t len, int depth,
             int *unchanged, char *err, size_t errlen);



/* One backup of a document, as backup_list gives it. */
typedef struct {
    int n; /* the number, 1 (the newest) to BACKUP_MAX */
    int old; /* 1: a file beside the document, made by an earlier version;
                2: a special backup (see special) */
    int special; /* the file format version of a special backup (the file
                    V<version>-<name> beside the document), else 0 */
    long long size; /* the bytes */
    time_t when; /* the time stamp of the file */
    char path[PATH_MAX];
} BackupInfo;


/*
 * The backups of the document path, those in the folder .backup first (the
 * newest, number 1, first), then the old ones beside the document, then the
 * special backups (V<version>-<name> beside the document, made when a file
 * of an older version of the file format was loaded; they are never removed
 * and have n 0), at most cap of them.  Returns how many.
 */
int
backup_list(const char *path, BackupInfo *v, int cap);


/*
 * Restore backup b over the document path: a swap.  The present document
 * becomes backup 1 (the backups below the one that was restored move up by
 * one, so the file of the chosen backup is not kept twice), and the text of
 * the chosen backup is the document, with its permissions and owner as far as
 * they can be kept.  A restore keeps at least one backup, whatever depth
 * is, and removes the backups above depth.  If the document holds the same
 * bytes as the backup, nothing is done and *same is 1.  The order of the
 * steps is such that a failure leaves the document and every backup whole:
 * a step that fails before the document is replaced changes nothing; one
 * that fails after leaves the old document in the file .backup/.swapNNN,
 * named in err.  Returns 0 or -1.
 */
int
backup_restore(const char *path, const BackupInfo *b, int depth, int *same,
               char *err, size_t errlen);


/*
 * The step of backup_restore (1 to 5) that fails on purpose, 0 for none:
 * for the tests of the failures.
 */
extern int backup_fault;


/*
 * The text of the box that asks before the number of backups is lowered
 * from `from` to `to` (to is less than from): which backups will be removed
 * and when, and what Yes and No do.
 */
void
backup_lowering_text(int from, int to, char *out, size_t cap);

#endif
