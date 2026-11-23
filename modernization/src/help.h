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

/* help.h - the help of Strata: topics written in plain text.
 *
 * A help file is a text file made of topics.  A line that begins with "= "
 * starts a topic and gives its title.  The lines that follow, up to the next
 * topic, are of these kinds:
 *
 *   ~ NAME      Another name of the topic.  The programs ask for a topic by
 *               the title of the menu, the panel or the field that the
 *               cursor is in; a topic answers to its title and to its names.
 *   ? TEXT      A message.  When the message line shows a text that begins
 *               with TEXT, F1 shows this topic.  A TEXT that begins with "N "
 *               stands for a message that begins with a number and a blank
 *               and then has the rest of TEXT: "N replacement" fits "3
 *               replacements".
 *   ; TEXT      A comment, which is not part of the topic.
 *   | TEXT      A line that is shown as it is, not joined with its
 *               neighbours (for an example).
 *   - TEXT      An item of a list.  The lines that follow it and begin with
 *               two blanks belong to the item.
 *   (blank)     Ends a paragraph.
 *   other       Text of a paragraph.  The lines of a paragraph are joined and
 *               broken again to the width of the window.
 *
 * In the text, {Ctrl-S} is a key, which is shown as a key cap, [[Title]] is a
 * link to the topic Title and [[Title|words]] is a link that shows words.
 * A brace that is not a key is written twice, {{ and }}.
 */
#ifndef HELP_H
#define HELP_H

typedef struct {
    int n;
    char **title; /* the title of each topic */
    char **text; /* the lines of the topic, '\n' separated */
    char ***name; /* the other names of each topic */
    int *nname;
    char ***msg; /* the messages of each topic */
    int *nmsg;
} Help;


/* A span of a page: a link to a topic, or (topic is HELP_KEY) a key cap. */
enum {
    HELP_KEY = -1
};

typedef struct {
    int line;
    int col;
    int len;
    int topic;
} HelpSpan;


/* A topic broken into lines for a window of a given width. */
typedef struct {
    int n;
    char **line;
    int nspan;
    HelpSpan *span;
} HelpPage;


/* Load a help file; NULL when it cannot be read or holds no topic. */
Help *
help_load(const char *path);


void
help_free(Help *h);


/* The topic with that title or name (the case is ignored), or -1. */
int
help_find(const Help *h, const char *title);


/*
 * The topic for the place named, such as "Marker Format" or
 * "File:Save".  When there is none the name is made shorter: the part from
 * " (" on goes, then the last part after a colon, and so on; -1 when nothing
 * is left.
 */
int
help_find_context(const Help *h, const char *name);


/* The topic for the text of a message, or -1. */
int
help_find_message(const Help *h, const char *msg);


/* Break a topic into lines of at most width columns; 0 on success. */
int
help_page(const Help *h, int topic, int width, HelpPage *pg);


void
help_page_free(HelpPage *pg);


/*
 * Find the help file: the hint, $STRATA_HELP, the file strata.hlp in the
 * current directory, next to the program, in the directory above it or in
 * share/strata above it (an installation under any prefix), and then the
 * install directories /usr/local/share/strata and /usr/share/strata.
 * Returns buf, or NULL.
 */
const char *
help_find_file(const char *hint, char *buf, unsigned n);
#endif
