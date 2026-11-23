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
/* dictmenu.h - the panel Maintain Dictionaries of the Document Manager. */
#ifndef DICTMENU_H
#define DICTMENU_H

/*
 * The panel of the personal word list: the words in order, Ins to add one,
 * Del to take one out, F3 and F4 to import and export a list of words, and
 * letters to find a word.  workdir is the directory that the lists of files
 * of the import and export begin with.
 */
void
dict_panel(const char *workdir);

#endif
