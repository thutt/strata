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
 * bordermenu.h - Border (Alt-F8): the Border Layout panel.
 */
#ifndef BORDERMENU_H
#define BORDERMENU_H
#include "editor.h"

/*
 * The panel for the columns and blocks that the highlight of the editor
 * holds.  Ins applies the fields that were changed, Shift-Ins every field.
 * The change is one operation of the document.  Returns 0 when something was
 * applied, -1 when the panel was cancelled or nothing was changed.
 */
int
bordermenu_run(Editor *e);

#endif
