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
 * The data layer of the thesaurus.
 *
 * It reads the MyThes files of the package mythes-en-us (th_en_US_v2.idx
 * and th_en_US_v2.dat) and knows nothing of the editor or the screen.  The
 * files are mapped read only and searched in place, so the first look-up
 * costs a few page reads.  See docs/THESAURUS.txt.
 */
#ifndef THESAURUS_H
#define THESAURUS_H

/* The size of a headword or of a word of a list, with its end byte. */
#define TH_HEAD 80

/* The words that the panel lists for a word that was not found. */
#define TH_LIST 10

/* The most senses and the most words of one lookup that are kept. */
#define TH_MAXSENSES 64

typedef struct {
    char pos[8]; /* "noun", "verb", "adj" or "adv". */
    char **words; /* CP437, without the suffixes, in the order of the file. */
    int nwords;
} ThSense;

typedef struct {
    char head[TH_HEAD]; /* The entry that was found, lower case. */
    ThSense *senses;
    int nsenses;
} ThEntry;


/*
 * Name the data to use: a directory, the .dat file or the path without
 * .idx or .dat.  An empty string means that no data is available.  NULL goes
 * back to the usual search (the variable STRATA_THESAURUS, then
 * /usr/share/mythes).  The tests use it to change the data.
 */
void
th_use(const char *path);


/* 1 when the index and the data can be read; the answer is kept. */
int
th_available(void);


/*
 * The path of the data file that is in use (empty when there is none), for
 * messages.
 */
const char *
th_path(void);


/*
 * Look the word up.  The word is CP437 and in any case.  Returns 1 when it
 * is found (under its stem, if it is not a headword itself; the senses
 * hold the synonyms only), 2 when the word is a headword of the data but
 * has no synonym (all its relations are generic or related terms; out->head
 * is the headword and there are no senses to free), 0 when it is not found
 * and -1 when there is no data.  Free the entry with th_free when 1 is
 * returned.
 */
int
th_lookup(const char *word, ThEntry *out);


void
th_free(ThEntry *e);


/*
 * The list of similar words that is shown when a word is not found: up to
 * count headwords, names[i] each, around the word.  The target is the last
 * headword that is not after the word, moved by step headwords (0 starts a
 * list; a page or a single word moves it).  The list starts mark_in
 * headwords before the target, or at the first headword, and *mark is the
 * index of the target in it.  Returns the number of names, 0 when there is
 * no data or no headword.
 */
int
th_neighbours(const char *word, int step, int count, int mark_in,
              char names[][TH_HEAD], int *mark);


/*
 * The try-th candidate stem of the word, in the order that th_lookup tries
 * them (try 0 is the word itself, in lower case).  Returns 1 and fills out,
 * or 0 when there is no such candidate.
 */
int
th_stem(const char *word, int try, char out[TH_HEAD]);


/*
 * Make a replacement have the case of the word that it replaces: all
 * capitals (two letters or more) make every letter a capital, a capital
 * first letter makes the first letter a capital, and otherwise the
 * replacement is left as it is.
 */
void
th_match_case(const char *orig, const char *repl, char *out, int cap);
#endif
