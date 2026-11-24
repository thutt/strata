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
 * spell.h - the spell check (docs/SPELL.txt).
 *
 * The program has no dictionary of its own.  The words are checked by a
 * program that speaks the ispell pipe protocol (aspell -a or hunspell -a),
 * which runs as a child process.  What this module does is the part that
 * is Strata's: what a word is (the rules are those of the format), which
 * words of a document are sent, the replacement of a word with its
 * attributes kept, the personal word list and the walk through the text of
 * a document and of the notes in it.  It has no screen output; spellui.c has
 * the panels.
 */
#ifndef SPELL_H
#define SPELL_H
#include <stddef.h>
#include <stdint.h>
#include "msdoc.h"

/* The longest word that the check takes (the limit of the format). */
#define SPELL_MAXWORD 65

/* The most suggestions that are kept for a word. */
#define SPELL_MAXSUG 12

/* ----------------------------------------------------------- word finder */
typedef struct {
    int start; /* the token of the first letter */
    int end; /* one past the token of the last character */
    int len; /* the characters of the word */
    int too_long; /* more than SPELL_MAXWORD characters */
    char text[SPELL_MAXWORD + 1]; /* the word, CP437 bytes (cut if long) */
} SpellWord;


/*
 * The first word of the tokens from to to (not included) of t.  A word is a
 * run of characters without a space, a tab, an attribute change, a marker or
 * a line end.  Punctuation at its two ends is not part of it, but it is
 * inside: don't, dont-stop and a.b are one word each.  A run that begins with
 * a digit, or has no letter, is not a word.  Returns 1 and the word, or 0.
 */
int
spell_find_word(const Text *t, int from, int to, SpellWord *w);


/* The word that holds token tok, or the one that ends just before it. */
int
spell_word_at(const Text *t, int tok, SpellWord *w);


/*
 * Replace the characters of the word w of t with the CP437 string s.  The
 * attribute tokens around the word are not touched, so the new text has the
 * attributes of the old.  Returns the number of tokens that the text grew
 * (negative when it shrank).
 */
int
spell_replace_word(Text *t, const SpellWord *w, const char *s);


/* A word with a digit inside is flagged without asking the checker. */
int
spell_has_digit(const char *s);


/* ---------------------------------------------------------- personal list */
typedef struct {
    char **w;
    int n;
    int cap;
} SpellDict;


/* Read ~/.strata/dictionary (one word on a line); a missing file is empty. */
void
spell_dict_load(SpellDict *d);


void
spell_dict_free(SpellDict *d);


/*
 * Is the word in the list?  An entry in lower case also stands for the word
 * with an initial capital; any other entry is only that word.
 */
int
spell_dict_has(const SpellDict *d, const char *word);


/* Add a word (as typed) and write the file.  Returns 0 or -1 with err. */
int
spell_dict_add(SpellDict *d, const char *word, char *err, size_t errcap);


/* The path of the file, for messages. */
void
spell_dict_path(char *out, size_t cap);

/* Put a word (CP437 bytes) at the end of the list, in memory only. */
void
spell_dict_append(SpellDict *d, const char *word);

/* Is the CP437 byte c a letter: an ASCII letter or an accented letter? */
int
spell_is_letter(int c);


/* ------------------------------------------------------ the checker child */
typedef struct SpellProc SpellProc;

/*
 * The program: $STRATA_SPELL, else aspell, else hunspell, found in PATH.
 * Returns 0 and its path, or -1.
 */
int
spell_find_program(char *path, size_t cap);


/*
 * Make the name of the Debian package that holds the dictionary of a
 * language such as en_US for the checker PROG (aspell or hunspell, with or
 * without a path): aspell-en, hunspell-en-us, hunspell-de-de.  Returns 1
 * and the name in OUT, or 0 and an empty OUT when the checker is another
 * program or the language is not of the form xx or xx_YY.
 */
int
spell_dictionary_package(const char *prog, const char *lang, char *out,
                         size_t cap);


/*
 * Start the checker for a language such as en_US.  Returns NULL, with a
 * message that says what is wrong, when none can be started.
 */
SpellProc *
spell_proc_open(const char *lang, char *err, size_t errcap);


void
spell_proc_close(SpellProc *p);


/* The language that the settings of the user name (en_US if none). */
void
spell_language(char *out, size_t cap);


/* One thing that the checker did not know, in one line of words. */
typedef struct {
    int offset; /* byte offset in the line, from 0 */
    char orig[SPELL_MAXWORD * 4 + 1]; /* what it reported, UTF-8 */
    int nsug;
    char sug[SPELL_MAXSUG][SPELL_MAXWORD * 4 + 1]; /* UTF-8 */
} SpellMiss;


/*
 * The checker's work is done in small steps so that the keyboard is polled
 * between them.  spell_proc_ask puts one line of words (separated by a
 * space) in the queue.  spell_proc_pump moves bytes for at most timeout_ms
 * milliseconds and returns the number of answers that are complete and not
 * yet taken, or -1 when the checker died.  spell_proc_answer takes the
 * oldest complete answer: the misses it holds (at most max) are written and
 * their number is returned, or -1 when there is no answer.
 */
int
spell_proc_ask(SpellProc *p, const char *line);


int
spell_proc_pump(SpellProc *p, int timeout_ms);


int
spell_proc_answer(SpellProc *p, SpellMiss *m, int max);


/* Tell the checker that a word is right for this session. */
void
spell_proc_accept(SpellProc *p, const char *word);


/*
 * Ask about one word (CP437) and wait for the answer: 1 when the word is
 * right (m is not used), 0 and m when it is not, -1 when the checker died.
 */
int
spell_proc_check_word(SpellProc *p, const char *word, SpellMiss *m);


/*
 * The suggestions of a miss as whole words (CP437).  The checker may have
 * found only a part of the word, such as dont of dont-stop; the part is
 * replaced in the word.  word_off is the byte offset of the word in the
 * line that was asked.  Returns the number written (at most max).
 */
int
spell_whole_words(const char *word, int word_off, const SpellMiss *m,
                  char out[][SPELL_MAXWORD + 1], int max);


/* ---------------------------------------------------------- the UTF-8 form */
/* CP437 bytes to UTF-8 (the buffer has room for 3 bytes per character). */
void
spell_to_utf8(const char *cp437, char *out, size_t cap);


/* UTF-8 to CP437; a character that has no place in CP437 becomes '?'. */
void
spell_from_utf8(const char *utf8, char *out, size_t cap);


/* ------------------------------------------------------------ the walk */
/*
 * A spell check of the items lo to hi of a document, and of the notes in
 * them.  The text is taken block by block and column by column; a note
 * is checked at the place of its marker.
 */
typedef struct SpellRun SpellRun;

typedef struct {
    char word[SPELL_MAXWORD + 1]; /* CP437 */
    int nsug;
    char sug[SPELL_MAXSUG][SPELL_MAXWORD + 1]; /* CP437, whole words */
    int in_note; /* the word is in the text of a note */
    int no_guess; /* a word with a digit: the checker was not asked */
} SpellHit;

enum {
    SR_HIT, /* a word that is not known: the hit is set */
    SR_DONE, /* the end of the text */
    SR_BUSY, /* the time given is over; call again */
    SR_ERROR /* the checker died */
};


/*
 * The checker may be NULL: the words are then only compared with the
 * personal list, and every word with a digit is flagged.  This is what
 * the tests of the word finder use.
 */
SpellRun *
spellrun_new(Doc *d, int lo, int hi, SpellProc *proc, SpellDict *dict,
             int autocorrect);


void
spellrun_free(SpellRun *r);


int
spellrun_next(SpellRun *r, int budget_ms, SpellHit *hit);


/*
 * Replace the word of the last hit.  With auto correct the same word is
 * replaced when it comes again.
 */
void
spellrun_replace(SpellRun *r, const char *word);


/* The word is right in the rest of this check (Accept). */
void
spellrun_accept(SpellRun *r);


/* Put the word of the last hit in the personal list (Define). */
int
spellrun_define(SpellRun *r, char *err, size_t errcap);


/* Is there an auto correction that was not made everywhere yet? */
int
spellrun_pending_autocorrect(const SpellRun *r);


/* Make the auto corrections in the rest of the text (Quit, Yes). */
void
spellrun_finish_autocorrect(SpellRun *r);


/* The number of words that were replaced. */
int
spellrun_replaced(const SpellRun *r);


/*
 * Keep a position of the top level text (item, column, token) right while
 * words are replaced before it, as the cursor of the editor has to be.
 */
void
spellrun_track(SpellRun *r, int *item, int *col, int *tok);


/*
 * The text round the last hit as plain lines: blocks are separated by a
 * newline, a tab is 0x10 and a marker 0x04.  The word is out[*hs] to
 * out[*he - 1].  At most maxblocks blocks, from the one with the word.
 */
void
spellrun_context(const SpellRun *r, char *out, size_t cap, int *hs, int *he,
                 int maxblocks);


/*
 * The characters of the column of the last hit as a plain string, and the
 * change of it by a new string (Line).  Tabs and markers cannot be edited:
 * spellrun_set_line returns -1 when the new line differs there.
 */
void
spellrun_line(const SpellRun *r, char *out, size_t cap);


int
spellrun_set_line(SpellRun *r, const char *line);


/* "Body", or "Body (Note)" for the text of a note. */
const char *
spellrun_section(const SpellRun *r);
#endif
