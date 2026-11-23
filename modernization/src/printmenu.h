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
 * printmenu.h - the Print menu: Draft, Final and Preview.
 *
 * Draft and Final typeset the document to a PDF with strprint and, when a
 * printer is named, send it to a CUPS queue with lp (printjob.h).  Preview
 * typesets the part with LaTeX and shows the pages with the DVI viewer
 * (preview.h).  See docs/PRINT-MENU.txt.
 */
#ifndef PRINTMENU_H
#define PRINTMENU_H
#include "editor.h"
#include "config.h"
#include "printjob.h"

/*
 * The value of the Printer field of a print that follows the settings Print
 * To and Printer Queue (or the old key printer): File only, PDF viewer,
 * the queue, or the default printer of the system.  q is the list of the
 * queues that the system reports (NULL: ask for it).  When Print To is
 * Printer and the system has no queue, the value is File only and note
 * says so (else note is empty).
 */
void
printmenu_default_printer(const Config *cfg, const PjQueues *q, char *out,
                          size_t cap, char *note, size_t ncap);


/* Print, Draft (draft != 0) or Print, Final: make the PDF and print it. */
void
printmenu_print(Editor *e, int draft);


typedef enum {
    PV_DOCUMENT, /* the whole document, after a panel */
    PV_BLOCK, /* the block the cursor is in */
    PV_SECTION /* the section the cursor is in */
} PreviewKind;


void
printmenu_preview(Editor *e, PreviewKind kind);
#endif
