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
 * menu.h - the F12 menu bar of the editor.
 *
 * The menu is on the top two screen rows: the names of the
 * entries on the first row and, on the second, a line that says what the
 * highlighted entry does.  Choosing an entry that has sub-entries replaces
 * the names with those of the sub-entries.
 */
#ifndef MENU_H
#define MENU_H

typedef struct MenuItem {
    const char *name; /* the text on the bar */
    const char *hint; /* the line shown below the bar */
    int avail; /* 0 if the command is not available (shown dimmed) */
    const struct MenuItem *sub; /* the entries below this one, or NULL */
    int nsub;
} MenuItem;

#define MENU_MAXDEPTH 3
#define MENU_MAXITEMS 24 /* the most entries of one level */


/*
 * Run the menu.  The redraw function paints the screen under the bar.  On
 * return the path holds the index chosen at each level.  The result is the
 * number of levels in the path, or 0 if the menu was cancelled.
 */
int
menu_run(const MenuItem *top, int ntop, void (*redraw)(void),
         int path[MENU_MAXDEPTH]);
#endif
