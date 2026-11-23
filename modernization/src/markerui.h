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
 * markerui.h - the panels for creating and editing markers, opened
 * from the MARKER key.
 */
#ifndef MARKERUI_H
#define MARKERUI_H
#include "marker.h"

/*
 * Open the panel for a marker type.  doc is the document being edited; a
 * cross-reference is checked against its sections.  data and len are the
 * payload of an
 * existing marker to edit, or an empty payload for a new one.  Returns 1
 * and a new payload (the caller frees it) when the panel is accepted, and 0
 * when it is cancelled.
 */
/*
 * Edits the nested text blocks of a note, comment or equation.  in and inlen
 * are the blocks as stored, and may be empty.  Returns 1 and the new blocks
 * (the caller frees them) when they are accepted, and 0 when cancelled.
 */
typedef int (*NestedEditor)(const char *title, const uint8_t *in, size_t inlen,
                            uint8_t **out, size_t *outlen);


/*
 * With nested set, the text of a note, comment or equation is edited with it
 * before the other fields.  Without it the text is a single line on the
 * panel, and only text that is one plain block can be edited.
 */
int
markerui_edit(const Doc *doc, const MInfo *mi, const uint8_t *data, size_t len,
              uint8_t **out, size_t *outlen, NestedEditor nested);
#endif
