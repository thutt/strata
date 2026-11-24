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
 * helptest.c - the reader of the help file.
 *
 *   helptest SAMPLE.txt [strata.hlp]
 *
 * The sample is a small help file with known contents.  When a second file
 * is given (the help file that the build makes), every topic of it is laid
 * out at several widths and the links of its text are checked.
 */
#include "../src/util.h"
#include "../src/help.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;


static void
check(const char *what, int ok)
{
    printf("%s %s\n", ok != 0 ? "ok  " : "FAIL", what);

    if (ok == 0) {
        failures++;
    }
}


static int
has_line(const HelpPage *pg, const char *text)
{
    for (int i = 0; i < pg->n; i++) {
        if (strcmp(pg->line[i], text) == 0) {
            return 1;
        }
    }

    return 0;
}


/* Whether the lines, joined with a blank, hold the text. */
static int
joined(const HelpPage *pg, const char *text)
{
    char all[4096] = "";

    for (int i = 0; i < pg->n; i++) {
        strcat(all, pg->line[i]);
        strcat(all, " ");
    }

    return strstr(all, text) != NULL;
}


static int
longest(const HelpPage *pg)
{
    int w = 0;

    for (int i = 0; i < pg->n; i++) {
        if (slen(pg->line[i]) > w) {
            w = slen(pg->line[i]);
        }
    }

    return w;
}


static void
test_sample(const char *path)
{
    Help *h = help_load(path);
    HelpPage pg;

    check("the sample loads", h != NULL);

    if (h == NULL) {
        return;
    }

    check("the sample has two topics", h->n == 2);
    check("the title of a topic", strcmp(h->title[0], "Alpha") == 0);
    check("a topic is found by its title", help_find(h, "Beta") == 1);
    check("a topic is found by another name", help_find(h, "Alpha name") == 0);
    check("the case of a name is ignored", help_find(h, "BETA") == 1);
    check("a name that is not there is not found", help_find(h, "Gamma") < 0);
    check("the comment lines are not in the text",
          strstr(h->text[0], "comment") == NULL);
    check("the names are not in the text", strstr(h->text[0], "~") == NULL);

    check("a place is found by its name",
          help_find_context(h, "Menu:File:Save") == 1);
    check("a place with a field is found by the field",
          help_find_context(h, "Panel:Field") == 0);
    check("a place with an unknown field falls back to the panel",
          help_find_context(h, "Alpha:Unknown field") == 0);
    check("a place with a suffix in parentheses falls back",
          help_find_context(h, "Beta (more)") == 1);
    check("a place that is not there is not found",
          help_find_context(h, "Gamma:Delta") < 0);

    check("a message is found by its beginning",
          help_find_message(h, "Cannot do beta: the file is shut") == 1);
    check("the longest beginning of a message wins",
          help_find_message(h, "Cannot do alpha twice, sorry") == 0);
    check("a message that begins with a number is found by N",
          help_find_message(h, "3 things made") == 0);
    check("a number is needed for a pattern that begins with N",
          help_find_message(h, "things made") < 0);
    check("a message that nobody knows is not found",
          help_find_message(h, "Something else") < 0);

    check("a page is made", help_page(h, 0, 30, &pg) == 0);
    check("no line is longer than the width", longest(&pg) <= 30);

    int keys = 0;
    int links = 0;

    for (int i = 0; i < pg.nspan; i++) {
        if (pg.span[i].topic == HELP_KEY) {
            keys++;
            check("a key has a space on each side",
                  pg.line[pg.span[i].line][pg.span[i].col] == ' ' &&
                  pg.line[pg.span[i].line][pg.span[i].col +
                                           pg.span[i].len - 1] == ' ');
        } else {
            links++;
            check("a link goes to a topic", pg.span[i].topic == 1);
        }
    }

    check("two keys: Ctrl-S and F12", keys == 2);
    check("three links: two in the paragraph and one in the literal line",
          links == 3);
    check("a link to a topic that is not there is plain text",
          strstr(h->text[0], "[[No such topic]]") != NULL);

    int plain = 0;

    for (int i = 0; i < pg.n; i++) {
        if (strstr(pg.line[i], "No such topic") != NULL) {
            plain++;
        }

        check("no brace of the markup is left in a line",
              strstr(pg.line[i], "[[") == NULL &&
              strstr(pg.line[i], "]]") == NULL);
    }

    check("the text of the broken link is shown", plain == 1);
    check("a doubled brace is shown as one", joined(&pg, "brace { and } in it"));

    int bullet = 0;
    int indented = 0;

    for (int i = 0; i < pg.n; i++) {
        if (strncmp(pg.line[i], "- ", 2) == 0) {
            bullet++;
        }

        if (strncmp(pg.line[i], "  ", 2) == 0 && pg.line[i][2] != ' ') {
            indented++;
        }
    }

    check("the two items begin with a dash", bullet == 2);
    check("the first item goes on in lines that are indented",
          indented >= 1);
    check("the literal line keeps its blanks",
          has_line(&pg, "literal    line   with a link"));
    help_page_free(&pg);

    check("a page is made at a width of 30", help_page(h, 0, 30, &pg) == 0);

    int narrow = pg.n;

    help_page_free(&pg);
    check("a page is made at a width of 100", help_page(h, 0, 100, &pg) == 0);
    check("a wide page has fewer lines", pg.n < narrow);
    help_page_free(&pg);
    check("a topic that is not there gives no page",
          help_page(h, 9, 30, &pg) != 0);

    help_free(h);
}


/* Every topic of the real help file, at several widths. */
static void
test_file(const char *path)
{
    Help *h = help_load(path);
    static const int widths[] = {20, 40, 76, 120};

    check("the help file loads", h != NULL);

    if (h == NULL) {
        return;
    }

    check("the help file has topics", h->n > 40);
    check("it has the topic Help", help_find(h, "Help") >= 0);
    check("it has the topic Edit Screen", help_find(h, "Edit Screen") >= 0);
    check("it has the topic Document Manager",
          help_find(h, "Document Manager") >= 0);

    int bad = 0;

    for (int t = 0; t < h->n; t++) {
        for (size_t w = 0; w < sizeof widths / sizeof widths[0]; w++) {
            HelpPage pg;

            if (help_page(h, t, widths[w], &pg) != 0 || pg.n == 0) {
                bad++;
                printf("  the topic %s has no page at %d\n", h->title[t],
                       widths[w]);
            } else {
                for (int i = 0; i < pg.nspan; i++) {
                    if (pg.span[i].topic >= h->n) {
                        bad++;
                    }
                }
            }

            help_page_free(&pg);
        }

        const char *p = h->text[t];

        while ((p = strstr(p, "[[")) != NULL) {
            const char *e = strstr(p, "]]");
            char name[200];
            char *bar;

            if (e == NULL) {
                bad++;
                break;
            }

            str_copy(name, sizeof name, p + 2);
            name[e - (p + 2) < 199 ? e - (p + 2) : 199] = '\0';
            bar = strchr(name, '|');

            if (bar != NULL) {
                *bar = '\0';
            }

            /* A link may be broken over two lines of the source. */
            {
                char *to = name;

                for (const char *from = name; *from != '\0'; from++) {
                    if (*from == '\n' || *from == ' ') {
                        if (to == name || to[-1] != ' ') {
                            *to++ = ' ';
                        }
                    } else {
                        *to++ = *from;
                    }
                }

                *to = '\0';
            }

            if (help_find(h, name) < 0) {
                bad++;
                printf("  the topic %s links to %s, which is not there\n",
                       h->title[t], name);
            }

            p = e + 2;
        }
    }

    check("every topic has a page and every link goes to a topic", bad == 0);
    help_free(h);
}


int
main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: helptest SAMPLE.txt [strata.hlp]\n");

        return 2;
    }

    test_sample(argv[1]);

    if (argc > 2) {
        test_file(argv[2]);
    }

    printf("helptest: %d failure(s)\n", failures);

    return failures != 0;
}
