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
 * comparemenu.h - the panel of Compare Document Versions of the Document
 * Manager: the user chooses two documents, reads the report of the
 * differences and may save it as a document.
 */
#ifndef COMPAREMENU_H
#define COMPAREMENU_H

/*
 * Runs the panel.  workdir is the working directory of the Document Manager
 * and first is the name of the document that was used last (it may be
 * empty): it is offered as the first document, and its backup file, when
 * there is one, as the second.  Returns when the user leaves the panel.
 */
void
compare_panel(const char *workdir, const char *first);

#endif
