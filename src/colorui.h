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

/* colorui.h - the Colors panel of Setup in the Document Manager. */
#ifndef COLORUI_H
#define COLORUI_H


/*
 * The colors of Setup are a page of the Setup panel, not a panel of their
 * own: the kinds of text with their colors for the terminal version and the
 * X11 version.  The colors are in use as they are changed; Setup keeps them
 * in the settings file when Ins is pressed (colors_page_commit and then the
 * save of Setup) and takes them back on Esc (colors_page_end(0)).
 */
#include "config.h"

/* Opens the page: the colors of the settings file are the ones to change. */
void
colors_page_begin(void);

/* The height of the frame of the page. */
int
colors_page_height(void);

/* Where the frame of the page is: x, y, w and h; scrolls to the entry. */
void
colors_page_paint(int *x, int *y, int *w, int *h);

/* Draws what the frame holds. */
void
colors_page_content(int x, int y, int w);

/* A key of the page; returns 1 when the page used it. */
int
colors_page_key(int k);

/* Puts the colors of the page into c, to be saved; nothing when closed. */
void
colors_page_commit(Config *c);

/* Closes the page; keep 0 takes the colors back that were in use before. */
void
colors_page_end(int keep);
#endif
