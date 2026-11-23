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
 * restoremenu.h - the panel Restore Backup of the Document Manager: the user
 * chooses a document, sees all its backups, newest first, with the number,
 * the date and time, the size and the first words, may read the text of one
 * of them, and restores one: the present document and the backup change
 * places (see backup_restore in backup.h).
 */
#ifndef RESTOREMENU_H
#define RESTOREMENU_H

/*
 * Runs the panel.  workdir is the working directory of the Document Manager
 * and first the name of the document that was used last (it may be empty).
 * depth is the number of backups kept (the setting Backups Kept).  Returns
 * when the user leaves the panel.
 */
void
restore_panel(const char *workdir, const char *first, int depth);

#endif
