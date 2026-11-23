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

/* readfile.c - read a whole file into memory, safely. */
#include "util.h"
#include "readfile.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>


static void
say(char *err, size_t errlen, const char *msg)
{
    if (err != NULL && errlen != 0) {
        str_copy(err, errlen, msg);
    }
}


static void *
read_regular(const char *path, size_t limit, size_t *len, char *err,
             size_t errlen)
{
    /* O_NONBLOCK keeps a named pipe from making the open wait for a writer. */
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    struct stat st;

    *len = 0;

    if (fd < 0) {
        say(err, errlen, "cannot open file");

        return NULL;
    }

    if (fstat(fd, &st) != 0) {
        close(fd);
        say(err, errlen, "cannot read file");

        return NULL;
    }

    if (!S_ISREG(st.st_mode)) {
        close(fd);
        say(err, errlen, S_ISDIR(st.st_mode) ?
            "this is a directory, not a file" :
            "this is not a regular file");

        return NULL;
    }

    size_t sz = lsz(st.st_size);

    if (sz > limit) {
        char msg[96];

        close(fd);
        snprintf(msg, sizeof msg, "file too large (the limit is %zu MB)",
                 (limit + 1048575) / 1048576);
        say(err, errlen, msg);

        return NULL;
    }

    uint8_t *b = malloc(sz + 1);

    if (b == NULL) {
        close(fd);
        say(err, errlen, "not enough memory to read the file");

        return NULL;
    }

    size_t got = 0;

    /* Read what the file holds now; a file that grows is refused. */
    while (got < sz) {
        ssize_t k = read(fd, b + got, sz - got);

        if (k < 0 && errno == EINTR) {
            continue;
        }

        if (k <= 0) {
            break;
        }

        got += lsz(k);
    }

    uint8_t extra;
    bool grew = got == sz && read(fd, &extra, 1) == 1;

    close(fd);

    if (got != sz || grew) {
        free(b);
        say(err, errlen, grew ? "file changed while it was read" :
                                "read error");

        return NULL;
    }

    b[sz] = 0;
    *len = sz;

    return b;
}


uint8_t *
readfile_all(const char *path, size_t limit, size_t *len, char *err,
             size_t errlen)
{
    return read_regular(path, limit, len, err, errlen);
}


char *
readfile_text(const char *path, size_t limit, size_t *len, char *err,
              size_t errlen)
{
    return read_regular(path, limit, len, err, errlen);
}
