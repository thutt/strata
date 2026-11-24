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
 * globalmenu.h - the panels of the Global menu that change the settings of
 * the whole document: Typeface and the others reached from F12, Global.
 */
#ifndef GLOBALMENU_H
#define GLOBALMENU_H
#include "editor.h"

/* Global, Typeface: the list of twenty fonts and their names. */
void
globalmenu_typeface(Editor *e);

/*
 * Global, Format: the headline and text rulers and the spacing of the
 * whole document and of each level, as the F records of the header.
 */
void
globalmenu_format(Editor *e);

/*
 * Global, Marker: the Marker Format panel (hidden markers, note type,
 * cross-reference, date and time formats) and the display mode of every
 * marker.
 */
void
globalmenu_marker(Editor *e);

/*
 * Global, Page: the Global Page Layout and the layouts of the title page,
 * the table of contents and the other pages: margins, header and footer
 * indent and whitespace, page number tag and paper size.
 */
void
globalmenu_page(Editor *e);

/*
 * Global, Named: edit (del is 0) or delete (del is 1) a named block
 * format, the FN definitions of the header.
 */
void
globalmenu_named(Editor *e, int del);


/*
 * The paper of a document that is made new and does not come from a
 * template: the setting default_paper of Setup (Paper for New Documents).
 * Letter, the setting that is not there, changes nothing; A4 sets the paper
 * of the document, as the field Paper Size of Global, Page does.
 */
void
doc_default_paper(Doc *d);
#endif
