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
 * recoverui.c - the box that offers the readable part of a damaged document.
 */
#include "util.h"
#include "recoverui.h"
#include "msdoc.h"
#include "log.h"
#include "widgets.h"
#include <stdio.h>
#include <string.h>


void
recoverui_text(const char *path, const DocRecovery *r, char *out, size_t cap)
{
    const char *base = strrchr(path, '/') != NULL ? strrchr(path, '/') + 1 :
                                                    path;

    snprintf(out, cap, "%s.  %d block%s and %d section%s of %s can be read "
             "(%lu of %lu bytes).  Open the part that can be read?  The "
             "original file is not changed.  Y opens it, N or Esc does not.",
             r->why, r->blocks, r->blocks == 1 ? "" : "s", r->sections,
             r->sections == 1 ? "" : "s", base, (unsigned long)r->bytes_read,
             (unsigned long)r->file_size);
}


Doc *
recoverui_load(const char *path, char *err, size_t errlen, const char *title,
               int *blocks)
{
    DocRecovery r;
    char err2[160];
    Doc *d = doc_load(path, err, errlen);
    Doc *rd;
    char text[640];

    *blocks = -1;

    if (d != NULL) {
        int made = 0;

        /* A file of an older version of the file format is kept as it was:
           the save writes the current version over it. */
        if (d->file_version < STRATA_FILE_FORMAT &&
            docver_special_backup(path, &made, err, errlen) != 0) {
            doc_free(d);

            return NULL;
        }

        if (made != 0) {
            log_note("stred: the special backup of %s was made", path);
        }

        return d;
    }

    rd = doc_recover(path, &r, err2, sizeof err2);

    if (rd == NULL || r.damaged == 0) {
        doc_free(rd);

        return NULL;
    }

    recoverui_text(path, &r, text, sizeof text);

    if (popup_confirm(title, text) == 0) {
        doc_free(rd);

        return NULL;
    }

    *blocks = r.blocks;

    return rd;
}
