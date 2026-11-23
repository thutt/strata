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
 * include.h - the Include marker (type 16) for collaborative editing.  Only
 * one person can edit a document at a time, so a work that several authors
 * write is made of a master document and parts, each in a file of its own.
 * An Include marker in the master names a part, and the Printer puts the
 * text of the part at the place of the marker, so that one print or preview
 * of the master is the whole work made of the saved text of every part.
 *
 * The rules (docs/PRINT-LATEX.txt, the section on the Include marker):
 *
 *   - The marker is the only thing in its block.  Nothing else is allowed in
 *     that block, because the block is replaced by the part.
 *   - The name is looked for in the directory of the document that holds
 *     the marker, then in the working directory; an absolute name is taken
 *     as it is.  The name may leave out the extension .doc, and the case of
 *     the letters does not matter.
 *   - A part with no structure puts its blocks at the place of the marker.
 *     A part with sections puts them one level below the section that holds
 *     the marker, in front of the sections that it already has; for that the
 *     marker must be in the last block of its section.
 *   - The master holds the Global Format.  A part brings its text and the
 *     formats of its blocks and sections.
 *   - Parts are read when the print is made, so the print has the saved text
 *     of each.  A part may include other parts, up to eight levels deep; a
 *     part that includes itself, directly or through others, is a loop.
 *   - An include that cannot be made is an error of the document (the Error
 *     Log, "Invalid Include request") and leaves a visible notice in the
 *     print at that place.  It never stops the print.
 *   - Section numbers, counters, notes, cross-references and pages go on
 *     through the parts as one document; a cross-reference that is inside a
 *     part, or in the master, points at the same section as before.
 */
#ifndef INCLUDE_H
#define INCLUDE_H
#include "diag.h"
#include "msdoc.h"
#include <time.h>

/* The type number of the Include marker. */
#define INCLUDE_MTYPE 16

/* The deepest nest of parts that include parts. */
#define INCLUDE_DEPTH_MAX 8

/* The record of what a joined document is made of. */
typedef struct IncludeMap IncludeMap;


/*
 * Make the document that the print is made from: the master with the text of
 * the parts at the Include markers.  The master must have been prepared by
 * marker_prepare.  The result is NULL when the master has no Include marker
 * (the master itself is then written).  master_dir is the directory of the
 * master (a relative name of a part is looked for there first), or NULL;
 * the path of the master is that of the document, when it has one.
 * The errors are added to diags (when it is not NULL) with positions in the
 * master.  *map is the record for include_orig_pos and is freed with
 * include_map_free.  The parts are prepared one after the other, so the
 * caller prepares the master again (and the result) when it needs the
 * marker state.
 */
Doc *
include_expand(const Doc *master, const char *master_dir, DiagList *diags,
               IncludeMap **map);


/*
 * The position in the master of a position (item, col, line, off) in the
 * joined document: an item that comes from a part is at the Include marker
 * that brought it in.
 */
void
include_orig_pos(const IncludeMap *m, int item, int col, int line, int off,
                 int *oitem, int *ocol, int *oline, int *ooff);


/* Put the diagnostics that have positions in the joined document at their
   places in the master. */
void
include_remap_diags(const IncludeMap *m, DiagList *l, int from);


/* The master that the joined document was made from. */
const Doc *
include_orig(const IncludeMap *m);


/* How many part files were read. */
int
include_files(const IncludeMap *m);


/* The path, the size in bytes and the time of change of part file i. */
void
include_file(const IncludeMap *m, int i, const char **path, long *size,
             time_t *mtime);


/* Free the record. */
void
include_map_free(IncludeMap *m);

#endif
