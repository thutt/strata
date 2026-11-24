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
 * undo.h - Undo and Redo of the changes of the document.
 *
 * The Editor keeps a copy of the document as it was after the last change
 * (the shadow).  After every key the document is compared with the shadow;
 * when it differs, the part that changed is kept as a patch: the items that
 * were replaced, and how many items replaced them.  A patch swaps the two
 * sides, so that the same patch undoes and redoes.  Nothing in the editing
 * commands has to know about it.  With the number of levels set to 0 no
 * shadow and no history are kept.
 */
#ifndef UNDO_H
#define UNDO_H
#include "editor.h"
#include "config.h"

/* The most operations that can be kept, and the number a user gets. */
#define UNDO_MAX_LEVELS CONFIG_UNDO_MAX
#define UNDO_DEFAULT_LEVELS CONFIG_UNDO_DEFAULT

/* The most memory that the history may take, in bytes. */
#define UNDO_MAX_BYTES (64u * 1024u * 1024u)


/* The one rule for a number of levels: 0 to UNDO_MAX_LEVELS. */
int
undo_clamp_levels(int n);


/* The number of levels that the Editors that start now use. */
void
undo_set_levels(int n);


int
undo_get_levels(void);


/*
 * What a key did, for the grouping of typing.  The class is 1 for a typed
 * character, 2 for a Backspace or Delete, and 0 for any other key.
 */
typedef struct {
    Pos cur; /* the cursor before the key */
    int cls;
    int ch; /* the typed character, or 0 */
} UndoKey;


/* Call before a key is handled: the shadow is made when it is needed. */
void
undo_before_key(Editor *e, UndoKey *uk, int cls, int ch);


/*
 * Call after the key was handled: a change of the document becomes a patch
 * (or joins the patch of the typing before it).  label names the operation.
 */
void
undo_after_key(Editor *e, const UndoKey *uk, const char *label);


/*
 * Undo the last operation, or do it again.  Return 0 when it was done, and
 * -1 when there was nothing to do (the message shows why).
 */
int
undo_undo(Editor *e);


int
undo_redo(Editor *e);


/* Forget the history and the shadow (a file was loaded, a new one made). */
void
undo_reset(Editor *e);


/* Free what the Editor keeps (ed_free). */
void
undo_free(Editor *e);


/*
 * For the tests: the number of operations that can be undone and redone, the
 * bytes that the history and the shadow take, and a check that the shadow
 * is the document (0 when it is).
 */
void
undo_counts(const Editor *e, int *undo, int *redo);


size_t
undo_history_bytes(const Editor *e);


size_t
undo_shadow_bytes(const Editor *e);


int
undo_verify(const Editor *e);
#endif
