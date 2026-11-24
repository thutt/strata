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

/* help.c - read the help file and break its topics into lines.  The format
 * of the file is described in help.h. */
#include "util.h"
#include "help.h"
#include "readfile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>


/* A growing piece of text. */
typedef struct {
    char *s;
    size_t len;
    size_t cap;
} Text;


static void
text_add(Text *t, const char *s, size_t n)
{
    if (t->len + n + 1 > t->cap) {
        t->cap = (t->len + n + 1) * 2;
        t->s = realloc(t->s, t->cap);
    }

    memcpy(t->s + t->len, s, n);
    t->len += n;
    t->s[t->len] = '\0';
}


static void
text_reset(Text *t)
{
    free(t->s);
    t->s = NULL;
    t->len = 0;
    t->cap = 0;
}


static char *
dup_n(const char *s, size_t n)
{
    char *r = malloc(n + 1);

    memcpy(r, s, n);
    r[n] = '\0';

    return r;
}


static void
add_string(char ***list, int *n, char *s)
{
    *list = realloc(*list, usz(*n + 1) * sizeof **list);
    (*list)[*n] = s;
    *n += 1;
}


/* Start a topic with the given title. */
static void
add_topic(Help *h, const char *title, size_t len)
{
    int i = h->n;

    h->n += 1;
    h->title = realloc(h->title, usz(h->n) * sizeof *h->title);
    h->text = realloc(h->text, usz(h->n) * sizeof *h->text);
    h->name = realloc(h->name, usz(h->n) * sizeof *h->name);
    h->nname = realloc(h->nname, usz(h->n) * sizeof *h->nname);
    h->msg = realloc(h->msg, usz(h->n) * sizeof *h->msg);
    h->nmsg = realloc(h->nmsg, usz(h->n) * sizeof *h->nmsg);
    h->title[i] = dup_n(title, len);
    h->name[i] = NULL;
    h->nname[i] = 0;
    h->msg[i] = NULL;
    h->nmsg[i] = 0;
    h->text[i] = strdup("");
}


/* The body of the topic that was last added is complete. */
static void
end_topic(Help *h, Text *body)
{
    if (h->n > 0 && body->s != NULL) {
        free(h->text[h->n - 1]);
        h->text[h->n - 1] = body->s;
        body->s = NULL;
        body->len = 0;
        body->cap = 0;
    }
}


static void
trim(const char **s, size_t *n)
{
    while (*n > 0 && (**s == ' ' || **s == '\t')) {
        *s += 1;
        *n -= 1;
    }

    while (*n > 0 && ((*s)[*n - 1] == ' ' || (*s)[*n - 1] == '\t' ||
                      (*s)[*n - 1] == '\r')) {
        *n -= 1;
    }
}


/* The help file is a few hundred kilobytes: refuse a file larger than this. */
#define HELP_FILE_MAX (16u * 1024u * 1024u)


Help *
help_load(const char *path)
{
    size_t total = 0;
    char *all = readfile_text(path, HELP_FILE_MAX, &total, NULL, 0);

    if (all == NULL) {
        return NULL;
    }

    Help *h = calloc(1, sizeof *h);
    Text body = {NULL, 0, 0};
    const char *p = all;

    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t ll = eol != NULL ? lsz(eol - p) : strlen(p);
        const char *line = p;

        p = eol != NULL ? eol + 1 : p + ll;

        if (ll > 0 && line[ll - 1] == '\r') {
            ll--;
        }

        if (ll >= 2 && line[0] == '=' && line[1] == ' ') {
            const char *t = line + 2;
            size_t tl = ll - 2;

            end_topic(h, &body);
            trim(&t, &tl);
            add_topic(h, t, tl);
        } else if (h->n == 0) {
            continue;
        } else if (ll >= 2 && line[0] == '~' && line[1] == ' ') {
            const char *t = line + 2;
            size_t tl = ll - 2;

            trim(&t, &tl);
            add_string(&h->name[h->n - 1], &h->nname[h->n - 1],
                       dup_n(t, tl));
        } else if (ll >= 2 && line[0] == '?' && line[1] == ' ') {
            const char *t = line + 2;
            size_t tl = ll - 2;

            trim(&t, &tl);
            add_string(&h->msg[h->n - 1], &h->nmsg[h->n - 1], dup_n(t, tl));
        } else if (ll >= 1 && line[0] == ';') {
            continue;
        } else {
            text_add(&body, line, ll);
            text_add(&body, "\n", 1);
        }
    }

    end_topic(h, &body);
    free(all);

    if (h->n == 0) {
        help_free(h);

        return NULL;
    }

    return h;
}


void
help_free(Help *h)
{
    if (h == NULL) {
        return;
    }

    for (int i = 0; i < h->n; i++) {
        free(h->title[i]);
        free(h->text[i]);

        for (int j = 0; j < h->nname[i]; j++) {
            free(h->name[i][j]);
        }

        for (int j = 0; j < h->nmsg[i]; j++) {
            free(h->msg[i][j]);
        }

        free(h->name[i]);
        free(h->msg[i]);
    }

    free(h->title);
    free(h->text);
    free(h->name);
    free(h->nname);
    free(h->msg);
    free(h->nmsg);
    free(h);
}


static int
same_name(const char *a, const char *b, int pass)
{
    if (pass == 0) {
        return strcmp(a, b) == 0;
    }

    return strcasecmp(a, b) == 0;
}


int
help_find(const Help *h, const char *title)
{
    if (h == NULL || title == NULL) {
        return -1;
    }

    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < h->n; i++) {
            int same = same_name(h->title[i], title, pass);

            for (int j = 0; same == 0 && j < h->nname[i]; j++) {
                same = same_name(h->name[i][j], title, pass);
            }

            if (same != 0) {
                return i;
            }
        }
    }

    return -1;
}


int
help_find_context(const Help *h, const char *name)
{
    char buf[256];

    if (h == NULL || name == NULL) {
        return -1;
    }

    str_copy(buf, sizeof buf, name);

    for (;;) {
        int t = help_find(h, buf);
        char *cut = strstr(buf, " (");

        if (t >= 0) {
            return t;
        }

        if (cut != NULL) {
            *cut = '\0';
            continue;
        }

        cut = strrchr(buf, ':');

        if (cut == NULL) {
            return -1;
        }

        *cut = '\0';
    }
}


int
help_find_message(const Help *h, const char *msg)
{
    int best = -1;
    size_t best_len = 0;

    if (h == NULL || msg == NULL) {
        return -1;
    }

    /* A message that begins with a number, such as "3 replacements". */
    const char *after_number = msg;

    while (*after_number >= '0' && *after_number <= '9') {
        after_number++;
    }

    for (int i = 0; i < h->n; i++) {
        for (int j = 0; j < h->nmsg[i]; j++) {
            const char *pat = h->msg[i][j];
            const char *text = msg;
            size_t n;

            /* A pattern that begins with "N " stands for a number and a
               blank before the rest of the pattern. */
            if (pat[0] == 'N' && pat[1] == ' ') {
                if (after_number == msg || *after_number != ' ') {
                    continue;
                }

                pat += 2;
                text = after_number + 1;
            }

            n = strlen(pat);

            if (n > best_len && strncmp(text, pat, n) == 0) {
                best = i;
                best_len = n;
            }
        }
    }

    return best;
}


/* ------------------------------------------------------------- pages */

/* A piece of a paragraph: a word, a key or a link. */
typedef struct {
    char *s;
    int len;
    int kind; /* 0 word, 1 key, 2 link */
    int topic;
    int space; /* the number of blanks before it in the source */
} Tok;


typedef struct {
    Tok *t;
    int n;
} Toks;


static void
tok_add(Toks *ts, const char *s, size_t n, int kind, int topic, int space)
{
    ts->t = realloc(ts->t, usz(ts->n + 1) * sizeof *ts->t);
    ts->t[ts->n].s = dup_n(s, n);
    ts->t[ts->n].len = isz(n);
    ts->t[ts->n].kind = kind;
    ts->t[ts->n].topic = topic;
    ts->t[ts->n].space = space;
    ts->n++;
}


static void
toks_free(Toks *ts)
{
    for (int i = 0; i < ts->n; i++) {
        free(ts->t[i].s);
    }

    free(ts->t);
    ts->t = NULL;
    ts->n = 0;
}


static int
at_link(const char *p)
{
    return p[0] == '[' && p[1] == '[' && strstr(p + 2, "]]") != NULL;
}


static int
at_key(const char *p)
{
    return p[0] == '{' && p[1] != '{' && strchr(p + 1, '}') != NULL;
}


/* Split the text of a paragraph into words, keys and links. */
static void
tokenize(const Help *h, const char *s, Toks *ts)
{
    int space = 0;

    while (*s != '\0') {
        if (*s == ' ' || *s == '\t') {
            space++;
            s++;
        } else if (at_key(s)) {
            const char *e = strchr(s + 1, '}');
            char key[128];
            size_t n = lsz(e - (s + 1));

            if (n > sizeof key - 3) {
                n = sizeof key - 3;
            }

            key[0] = ' ';
            memcpy(key + 1, s + 1, n);
            key[n + 1] = ' ';
            tok_add(ts, key, n + 2, 1, HELP_KEY, space);
            space = 0;
            s = e + 1;
        } else if (at_link(s)) {
            const char *e = strstr(s + 2, "]]");
            char in[256];
            size_t n = lsz(e - (s + 2));
            char *bar;
            int topic;

            if (n > sizeof in - 1) {
                n = sizeof in - 1;
            }

            memcpy(in, s + 2, n);
            in[n] = '\0';
            bar = strchr(in, '|');

            if (bar != NULL) {
                *bar = '\0';
            }

            topic = help_find(h, in);

            if (bar != NULL) {
                tok_add(ts, bar + 1, strlen(bar + 1), topic >= 0 ? 2 : 0,
                        topic, space);
            } else {
                tok_add(ts, in, strlen(in), topic >= 0 ? 2 : 0, topic, space);
            }

            space = 0;
            s = e + 2;
        } else {
            Text w = {NULL, 0, 0};

            while (*s != '\0' && *s != ' ' && *s != '\t' && at_key(s) == 0 &&
                   at_link(s) == 0) {
                if (s[0] == '{' && s[1] == '{') {
                    text_add(&w, "{", 1);
                    s += 2;
                } else if (s[0] == '}' && s[1] == '}') {
                    text_add(&w, "}", 1);
                    s += 2;
                } else {
                    text_add(&w, s, 1);
                    s++;
                }
            }

            tok_add(ts, w.s, w.len, 0, -1, space);
            text_reset(&w);
            space = 0;
        }
    }
}


/* The page that is being made. */
typedef struct {
    HelpPage *pg;
    Text line;
    int col;
} Build;


static void
put_line(Build *b)
{
    HelpPage *pg = b->pg;

    pg->line = realloc(pg->line, usz(pg->n + 1) * sizeof *pg->line);
    pg->line[pg->n] = b->line.s != NULL ? b->line.s : strdup("");
    pg->n++;
    b->line.s = NULL;
    b->line.len = 0;
    b->line.cap = 0;
    b->col = 0;
}


static void
put_blank(Build *b)
{
    HelpPage *pg = b->pg;

    if (pg->n > 0 && pg->line[pg->n - 1][0] != '\0') {
        put_line(b);
    }
}


static void
pad_to(Build *b, int col)
{
    while (b->col < col) {
        text_add(&b->line, " ", 1);
        b->col++;
    }
}


/*
 * Lay out a paragraph.  first is the text before the first line, indent the
 * width of the margin of the lines that follow; wrap is 1 when the lines are
 * to be broken at the width, and 0 when they are not.
 */
static void
layout(const Help *h, Build *b, const char *first, int indent,
       const char *para, int width, int wrap)
{
    Toks ts = {NULL, 0};
    int i = 0;
    int line_start;

    tokenize(h, para, &ts);
    text_add(&b->line, first, strlen(first));
    b->col = slen(first);
    line_start = b->col;

    while (i < ts.n) {
        int j = i + 1;
        int gw = ts.t[i].len;

        while (j < ts.n && ts.t[j].space == 0) {
            gw += ts.t[j].len;
            j++;
        }

        if (b->col > line_start) {
            if (wrap != 0 && b->col + 1 + gw > width) {
                put_line(b);
                pad_to(b, indent);
                line_start = b->col;
            } else {
                pad_to(b, b->col + (wrap != 0 ? 1 : ts.t[i].space));
            }
        } else if (wrap == 0) {
            /* The blanks at the start of a line that is not broken. */
            pad_to(b, b->col + ts.t[i].space);
        }

        for (int k = i; k < j; k++) {
            if (ts.t[k].kind != 0) {
                HelpPage *pg = b->pg;

                pg->span = realloc(pg->span,
                                   usz(pg->nspan + 1) * sizeof *pg->span);
                pg->span[pg->nspan].line = pg->n;
                pg->span[pg->nspan].col = b->col;
                pg->span[pg->nspan].len = ts.t[k].len;
                pg->span[pg->nspan].topic = ts.t[k].topic;
                pg->nspan++;
            }

            text_add(&b->line, ts.t[k].s, usz(ts.t[k].len));
            b->col += ts.t[k].len;
        }

        i = j;
    }

    put_line(b);
    toks_free(&ts);
}


/* The kinds of line of a topic. */
enum {
    LINE_TEXT,
    LINE_BLANK,
    LINE_LITERAL,
    LINE_ITEM,
    LINE_MORE
};


static int
line_kind(const char *line, size_t ll, int in_item)
{
    if (ll == 0) {
        return LINE_BLANK;
    }

    if (ll >= 2 && line[0] == '|' && line[1] == ' ') {
        return LINE_LITERAL;
    }

    if (ll >= 2 && line[0] == '-' && line[1] == ' ') {
        return LINE_ITEM;
    }

    if (ll >= 2 && line[0] == ' ' && line[1] == ' ' && in_item != 0) {
        return LINE_MORE;
    }

    return LINE_TEXT;
}


int
help_page(const Help *h, int topic, int width, HelpPage *pg)
{
    Build b = {pg, {NULL, 0, 0}, 0};
    Text para = {NULL, 0, 0};
    const char *first = "";
    int indent = 0;
    const char *p;

    pg->n = 0;
    pg->line = NULL;
    pg->nspan = 0;
    pg->span = NULL;

    if (h == NULL || topic < 0 || topic >= h->n) {
        return 1;
    }

    if (width < 20) {
        width = 20;
    }

    p = h->text[topic];

    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t ll = eol != NULL ? lsz(eol - p) : strlen(p);
        const char *line = p;
        int in_item = strcmp(first, "- ") == 0 && para.len > 0;
        int kind = line_kind(line, ll, in_item);

        p = eol != NULL ? eol + 1 : p + ll;

        if (kind != LINE_MORE && para.len > 0) {
            if (kind != LINE_TEXT || in_item != 0) {
                layout(h, &b, first, indent, para.s, width, 1);
                text_reset(&para);
            }
        }

        if (kind == LINE_BLANK) {
            put_blank(&b);
        } else if (kind == LINE_LITERAL) {
            char *lit = dup_n(line + 2, ll - 2);

            layout(h, &b, "", 0, lit, width, 0);
            free(lit);
        } else {
            const char *t = line;
            size_t tl = ll;

            if (kind == LINE_ITEM) {
                t += 2;
                tl -= 2;
                first = "- ";
                indent = 2;
            } else if (kind == LINE_TEXT && para.len == 0) {
                first = "";
                indent = 0;
            }

            trim(&t, &tl);

            if (para.len > 0) {
                text_add(&para, " ", 1);
            }

            text_add(&para, t, tl);
        }

        if (eol == NULL) {
            break;
        }
    }

    if (para.len > 0) {
        layout(h, &b, first, indent, para.s, width, 1);
    }

    text_reset(&para);

    while (pg->n > 0 && pg->line[pg->n - 1][0] == '\0') {
        free(pg->line[pg->n - 1]);
        pg->n--;
    }

    return 0;
}


void
help_page_free(HelpPage *pg)
{
    for (int i = 0; i < pg->n; i++) {
        free(pg->line[i]);
    }

    free(pg->line);
    free(pg->span);
    pg->n = 0;
    pg->line = NULL;
    pg->nspan = 0;
    pg->span = NULL;
}


/* ------------------------------------------------------------- the file */

/* The help file next to the program, or in the places of an installation
   that is relative to the program: PREFIX/bin/stred finds
   PREFIX/share/strata/strata.hlp.  The directory above the program is the
   build directory, which holds term/stred and x11/stred and strata.hlp. */
static const char *
help_find_near_program(char *buf, unsigned n)
{
    static const char *const rel[] = {"/strata.hlp",
                                      "/../strata.hlp",
                                      "/../share/strata/strata.hlp",
                                      NULL};
    char self[1024];
    ssize_t k = readlink("/proc/self/exe", self, sizeof self - 1);
    char *sl;

    if (k <= 0) {
        return NULL;
    }

    self[k] = '\0';
    sl = strrchr(self, '/');

    if (sl == NULL) {
        return NULL;
    }

    *sl = '\0';

    for (int i = 0; rel[i] != NULL; i++) {
        char path[1200];

        snprintf(path, sizeof path, "%s%s", self, rel[i]);

        if (access(path, R_OK) == 0) {
            snprintf(buf, n, "%s", path);

            return buf;
        }
    }

    return NULL;
}


const char *
help_find_file(const char *hint, char *buf, unsigned n)
{
    const char *first[] = {hint, getenv("STRATA_HELP"), "strata.hlp", NULL};
    const char *last[] = {"/usr/local/share/strata/strata.hlp",
                          "/usr/share/strata/strata.hlp",
                          NULL};

    for (int i = 0; first[i] != NULL; i++) {
        if (first[i][0] != '\0' && access(first[i], R_OK) == 0) {
            snprintf(buf, n, "%s", first[i]);

            return buf;
        }
    }

    /* The installation that the program belongs to comes before any other
       installation: a copy in the home directory is not hidden by one in
       /usr/share. */
    if (help_find_near_program(buf, n) != NULL) {
        return buf;
    }

    for (int i = 0; last[i] != NULL; i++) {
        if (access(last[i], R_OK) == 0) {
            snprintf(buf, n, "%s", last[i]);

            return buf;
        }
    }

    return NULL;
}
