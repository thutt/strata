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
 * dictlist.h - the functions behind Maintain Dictionaries.
 *
 * The personal word list of the user (the file "dictionary" in the settings
 * directory, one word on a line) is read by the spell check as a SpellDict.
 * This module is what a panel needs to change it: the rules for a word, an
 * ordered list, add and remove, and the import and export of a plain list of
 * words.  It has no screen output; dictmenu.c has the panel.
 */
#ifndef DICTLIST_H
#define DICTLIST_H

#include <stddef.h>

#include "spell.h"


/* What an import did with the lines of a file. */
typedef struct {
    int added; /* new words, now in the list */
    int duplicate; /* words that the list had already, or twice in the file */
    int refused; /* lines that are not a word */
} DictImport;


/*
 * Is the CP437 text a word that the list may hold?  A word has letters,
 * apostrophes and hyphens, begins and ends with a letter, has no space and
 * no digit and is at most SPELL_MAXWORD characters long.  Returns 1, or 0
 * with the reason in why.
 */
int
dictlist_check_word(const char *word, char *why, size_t cap);


/* Put the list in order: letters without regard to case, then as typed. */
void
dictlist_sort(SpellDict *d);


/*
 * The place of the entry that is exactly the word in a list that is in order,
 * or -1.
 */
int
dictlist_find(const SpellDict *d, const char *word);


/*
 * Does the list already stand for the word?  It does when the word is an
 * entry, or when it is an entry written in lower case with an initial
 * capital (the check treats such an entry as both).  The list must be in
 * order.
 */
int
dictlist_covers(const SpellDict *d, const char *word);


/*
 * Add a word to a list that is in order and keep it in order.  Returns 1 when
 * the word was added, 0 when the list covers it already.  The word must have
 * passed dictlist_check_word.
 */
int
dictlist_add(SpellDict *d, const char *word);


/* Take the entry at index out of the list. */
void
dictlist_remove(SpellDict *d, int index);


/*
 * The first entry of a list that is in order that begins with prefix, without
 * regard to case, or -1.
 */
int
dictlist_prefix(const SpellDict *d, const char *prefix);


/*
 * Read the file that the spell check reads and put the list in order.  A
 * missing file is an empty list.
 */
void
dictlist_load(SpellDict *d);


/* Write the list as the spell check reads it.  Returns 0 or -1 with err. */
int
dictlist_save(const SpellDict *d, char *err, size_t cap);


/*
 * Add the words of a text file (one on a line, UTF-8 or CP437 letters as the
 * checker's file has them) to a list that is in order.  The counts are in
 * res.  Returns 0, or -1 with err when the file cannot be read.  The list is
 * not written: call dictlist_save.
 */
int
dictlist_import(SpellDict *d, const char *path, DictImport *res, char *err,
                size_t cap);


/*
 * Write the list to a file, one word on a line, in order.  The file is made
 * whole before it replaces one that is there.  Returns 0 or -1 with err.
 */
int
dictlist_export(const SpellDict *d, const char *path, char *err, size_t cap);


/* ================================================== the hyphenation list */
/*
 * The same operations for the personal hyphenation list, the file
 * "hyphenation" of the settings folder (hyphlist.h).  An entry is a word with
 * a hyphen at each place where it may be broken; two entries with the same
 * letters are the same word.  dictlist_remove takes an entry out; the export
 * is dictlist_export.
 */
void
dicthyph_sort(SpellDict *d);


/* The place of the entry with the same letters as the word, or -1. */
int
dicthyph_find(const SpellDict *d, const char *word);


/* Add an entry that passed hyph_check; 0 when the list has the word. */
int
dicthyph_add(SpellDict *d, const char *word);


/* The first entry whose letters begin with prefix, or -1. */
int
dicthyph_prefix(const SpellDict *d, const char *prefix);


/* Read the file again; a missing file is an empty list. */
void
dicthyph_load(SpellDict *d);


/* Write the list to the file.  Returns 0 or -1 with err. */
int
dicthyph_save(const SpellDict *d, char *err, size_t cap);


/*
 * Add the words of a file: a hyphenation dictionary of the earlier program
 * when the name ends in .HYC (any case), else a text file with one entry on
 * a line.  The counts are in res.  Returns 0, or -1 with err.  The list is not
 * written.
 */
int
dicthyph_import(SpellDict *d, const char *path, DictImport *res, char *err,
                size_t cap);

#endif
