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
 * hyphlist.h - the personal hyphenation list.
 *
 * The list is the file "hyphenation" of the settings folder (STRATA_HOME or
 * ~/.strata): one word on a line, UTF-8, with a hyphen where the word may be
 * broken at the end of a line (hy-phen-a-tion).  A word with no hyphen is
 * never broken; an equals sign in front of it (=Jacksonville) says so in so
 * many words.  The Printer reads the file and writes it as \hyphenation.
 *
 * The functions here work on one word in code page 437 (the form that the
 * Editor and the panel keep) or on the file; they have no screen output.
 */
#ifndef HYPHLIST_H
#define HYPHLIST_H

#include <stddef.h>

/* The longest entry, in characters. */
#define HYPH_MAXWORD 65

/*
 * Is the CP437 text an entry that the list may hold?  Letters (also the
 * accented ones), hyphens between letters and an equals sign in front.  No
 * space, no digit and no apostrophe: TeX cannot break such a word.  Returns
 * 1, or 0 with the reason in why.
 */
int
hyph_check(const char *word, char *why, size_t cap);


/*
 * The order of the list: the letters without regard to case and without the
 * hyphens and the equals sign, then as typed.  Two entries with the same
 * letters (hyph_same_word) are the same word.
 */
int
hyph_compare(const char *x, const char *y);


/* Do the two entries have the same letters, without regard to case? */
int
hyph_same_word(const char *x, const char *y);


/*
 * The break places of the entry: the letters without marks go in plain
 * (capacity cap) and the number of the places is returned; at[i] is the
 * number of letters before place i.  A word with no hyphen has none.
 */
int
hyph_places(const char *word, char *plain, size_t cap, int *at, int maxat);


/*
 * The text of the file as the Printer writes it: "\hyphenation{...}\n" with
 * all the words (a never-break word with no hyphen), or NULL when the file is
 * not there, is empty or holds no usable word.  A line that is not an entry,
 * or has a character that the fonts of the Printer cannot set, is left out.
 * The result is malloc'd.
 */
char *
hyph_tex(void);


/*
 * The same from a text (the contents of a file), for the tests: skipped
 * counts the lines that were left out.
 */
char *
hyph_tex_of(const char *text, int *skipped);


/*
 * The entries of a hyphenation dictionary of the earlier program (a .HYC
 * file: pages of 512 bytes, page 0 the index; see docs/DICTIONARIES.txt) as
 * a malloc'd array of malloc'd CP437 words in lower case, with a hyphen at
 * each place of a word.  A word with no place is written with an equals sign
 * in front.  Returns NULL with a message in err when the data are damaged.
 */
char **
hyph_hyc_words(const unsigned char *data, size_t n, int *count, char *err,
               size_t cap);


/* Free what hyph_hyc_words made. */
void
hyph_free_words(char **w, int count);

#endif
