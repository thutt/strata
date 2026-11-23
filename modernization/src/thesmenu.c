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

/* The thesaurus panel; see thesmenu.h and docs/THESAURUS.txt. */
#include "util.h"
#include "term.h"
#include "widgets.h"
#include "thesaurus.h"
#include "thesmenu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HIST_MAX 20
#define COL_LABEL 2 /* The part of speech, 4 columns wide. */
#define COL_DASH 7
#define COL_WORDS 9
#define COL_LIMIT 78 /* A row of words ends before this column. */
#define COL_LIST 21 /* The words of the list for a word that is not found. */

/* One word of the senses as it is on the panel: row, columns and text. */
typedef struct {
    int row;
    int c0;
    int c1;
    char mark; /* The "," ";" or "." that follows the word. */
    char text[TH_HEAD];
} TWord;

typedef struct {
    Editor *e;
    int cols;
    int top; /* The screen row of the top border. */
    int height; /* Rows of the panel. */
    int page; /* The text rows of a page of senses. */
    int list; /* The names of the list for a word not found. */
    char orig[TH_HEAD]; /* The word of the document, for the case. */
    char head[TH_HEAD]; /* The word that was looked up, as it was typed. */
    int found;
    int nosyn; /* The word is in the data but has no synonym. */
    ThEntry ent;
    TWord *w;
    int nw;
    int capw;
    int nrows;
    int srow[TH_MAXSENSES]; /* The row where each sense starts. */
    int slabel[TH_MAXSENSES]; /* Its part of speech is written there. */
    int first; /* The first row shown. */
    int hl; /* The highlighted word; -1 is the heading. */
    char (*names)[TH_HEAD];
    int nnames;
    char hist[HIST_MAX][TH_HEAD];
    int nhist;
    int boxopen;
    char box[TH_HEAD];
} Panel;


/* Layout. */
static int
add_tword(Panel *p, int row, int c0, const char *text, char mark)
{
    if (p->nw == p->capw) {
        int nc = p->capw == 0 ? 64 : p->capw * 2;
        TWord *nw = realloc(p->w, usz(nc) * sizeof *nw);

        if (nw == NULL) {
            return 0;
        }

        p->w = nw;
        p->capw = nc;
    }

    TWord *t = &p->w[p->nw++];

    t->row = row;
    t->c0 = c0;
    t->c1 = c0 + slen(text);
    t->mark = mark;
    str_copy(t->text, sizeof t->text, text);

    return 1;
}


/*
 * Lay the senses out so: the part of speech in 4 columns
 * at the first of its senses, "-" at the start of every sense, the words
 * after ", " (";" or "." after the last one of a sense), and a row that
 * wraps with the words under the first one.
 */
static void
layout(Panel *p)
{
    int row = 0;
    char last_pos[8] = "";

    p->nw = 0;
    p->nrows = 0;

    for (int s = 0; s < p->ent.nsenses; s++) {
        const ThSense *sn = &p->ent.senses[s];
        int col = COL_WORDS;

        if (s > 0) {
            row++;
        }

        p->srow[s] = row;
        p->slabel[s] = strcmp(last_pos, sn->pos) != 0;
        str_copy(last_pos, sizeof last_pos, sn->pos);

        for (int i = 0; i < sn->nwords; i++) {
            int len = slen(sn->words[i]);

            if (col > COL_WORDS && col + len + 1 > COL_LIMIT) {
                row++;
                col = COL_WORDS;
            }

            char mark = ',';

            if (i + 1 == sn->nwords) {
                mark = s + 1 == p->ent.nsenses ? '.' : ';';
            }

            if (add_tword(p, row, col, sn->words[i], mark) == 0) {
                return;
            }

            col += len + 2;
        }
    }

    p->nrows = row + 1;
}


/* The index of the first word of a row, or -1. */
static int
row_first(const Panel *p, int row)
{
    for (int i = 0; i < p->nw; i++) {
        if (p->w[i].row == row) {
            return i;
        }
    }

    return -1;
}


static int
row_last(const Panel *p, int row)
{
    int r = -1;

    for (int i = 0; i < p->nw; i++) {
        if (p->w[i].row == row) {
            r = i;
        }
    }

    return r;
}


/* Scroll just far enough for the highlighted word to be on the screen. */
static void
show_hl(Panel *p)
{
    if (p->found == 0 || p->hl < 0) {
        return;
    }

    int row = p->w[p->hl].row;

    if (row < p->first) {
        p->first = row;
    } else if (row >= p->first + p->page) {
        p->first = row - p->page + 1;
    }
}


/* Lookup. */
static void
clear_entry(Panel *p)
{
    if (p->found != 0) {
        th_free(&p->ent);
    }

    p->found = 0;
    p->nosyn = 0;
    p->nw = 0;
    p->nrows = 0;
    p->nnames = 0;
    p->first = 0;
    p->hl = 0;
}


static void
begin_lookup(Panel *p, const char *word)
{
    clear_entry(p);
    str_copy(p->head, sizeof p->head, word);

    int r = th_lookup(word, &p->ent);

    if (r == 1) {
        p->found = 1;
        layout(p);
        p->hl = p->nw > 0 ? 0 : -1;
    } else {
        p->nosyn = r == 2 ? 1 : 0;
        p->nnames = th_neighbours(word, 0, p->list, p->list / 2, p->names,
                                  &p->hl);
    }
}


static void
push_history(Panel *p)
{
    if (p->nhist == HIST_MAX) {
        memmove(p->hist[0], p->hist[1], sizeof p->hist[0] * (HIST_MAX - 1));
        p->nhist--;
    }

    str_copy(p->hist[p->nhist++], sizeof p->hist[0], p->head);
}


/* The highlighted word, or NULL when there is none. */
static const char *
hl_text(const Panel *p)
{
    if (p->found != 0) {
        return p->hl >= 0 ? p->w[p->hl].text : p->head;
    }

    return p->nnames > 0 ? p->names[p->hl] : NULL;
}


/* Look up another word; the word being shown goes on the history. */
static void
look_up_next(Panel *p, const char *word)
{
    char copy[TH_HEAD];

    str_copy(copy, sizeof copy, word);
    push_history(p);
    begin_lookup(p, copy);
}


/* Drawing. */
/* Is there a headword before (dir -1) or after (dir 1) the name? */
static int
more_names(const char *name, int dir)
{
    char t[1][TH_HEAD];
    int m;

    return th_neighbours(name, dir, 1, 0, t, &m) == 1 &&
           strcmp(t[0], name) != 0;
}


static void
draw_more(const Panel *p, int up, int down)
{
    int x = p->cols - 10;

    if (up == 0 && down == 0) {
        return;
    }

    term_puts(x, p->top + 1, "MORE:", TA_BOLD);

    if (up != 0 && down != 0) {
        term_put(x + 6, p->top + 1, 0x18, TA_BOLD);
        term_put(x + 7, p->top + 1, 0x19, TA_BOLD);
    } else {
        term_put(x + 6, p->top + 1, up != 0 ? 0x18 : 0x19, TA_BOLD);
    }
}


/* Text centred on a row, with a space on each side, over a border. */
static void
draw_centred(const Panel *p, int y, const char *text)
{
    int n = slen(text);
    int x = (p->cols - n - 2) / 2;

    term_puts(x, y, " ", 0);
    term_puts(x + 1, y, text, TA_BOLD);
    term_puts(x + 1 + n, y, " ", 0);
}


static void
draw_hint(const Panel *p)
{
    char hint[96];

    snprintf(hint, sizeof hint, "%s  Esc:exit  CTRL-T:look up%s",
             p->found != 0 ? "Enter:replace" : "Enter:look up",
             p->nhist > 0 ? "  Ctrl+PgUp:last word" : "");
    draw_centred(p, p->top + p->height - 1, hint);

    if (p->boxopen != 0) {
        draw_centred(p, p->top + 6, "Esc:exit  CTRL-T:look up");
    }
}


static void
draw_senses(const Panel *p)
{
    int y0 = p->top + 2;

    term_puts(COL_LABEL, p->top + 1, p->head,
              p->hl < 0 ? TA_REVERSE : TA_BOLD);
    term_puts(COL_LABEL + slen(p->head), p->top + 1, ":", TA_BOLD);

    for (int s = 0; s < p->ent.nsenses; s++) {
        int r = p->srow[s] - p->first;

        if (r >= 0 && r < p->page) {
            if (p->slabel[s] != 0) {
                int pl = slen(p->ent.senses[s].pos);

                term_puts(COL_LABEL + 4 - pl, y0 + r, p->ent.senses[s].pos,
                          TA_DIM);
            }

            term_put(COL_DASH, y0 + r, '-', TA_DIM);
        }
    }

    for (int i = 0; i < p->nw; i++) {
        const TWord *t = &p->w[i];
        int r = t->row - p->first;

        if (r < 0 || r >= p->page) {
            continue;
        }

        term_puts(t->c0, y0 + r, t->text, i == p->hl ? TA_REVERSE : 0);

        term_put(t->c1, y0 + r, u8(t->mark), 0);
    }

    draw_more(p, p->first > 0, p->first + p->page < p->nrows);
}


static void
draw_names(const Panel *p)
{
    for (int i = 0; i < p->nnames; i++) {
        term_puts(COL_LIST, p->top + 1 + i, p->names[i],
                  i == p->hl ? TA_REVERSE : 0);
    }

    if (p->nnames > 0) {
        draw_more(p, more_names(p->names[0], -1),
                  more_names(p->names[p->nnames - 1], 1));
    }
}


static void
draw_box(const Panel *p)
{
    int x = 7;
    int y = p->top + 4;
    int w = p->cols - 14;
    int n = slen(p->box);
    int shown = n < w - 14 ? n : w - 14;

    term_fill(x, y, w, 3, ' ', 0);
    term_box(x, y, w, 3, 0, 1);
    term_puts(x + 2, y + 1, "Look Up:", TA_BOLD);
    term_puts(x + 11, y + 1, p->box + (n - shown), 0);
    term_puts(x + 11 + shown, y + 1, "_", 0);
}


static void
draw_panel(const Panel *p)
{
    w_base();
    box(0, p->top, p->cols, p->height,
        p->found != 0 ? "Thesaurus" :
        p->nosyn != 0 ? "The word has no synonyms -  Please choose another "
                        "word" :
                        "The word was not found -  Please choose another "
                        "word");

    if (p->found != 0) {
        draw_senses(p);
    } else {
        draw_names(p);
    }

    if (p->boxopen != 0) {
        draw_box(p);
    }

    draw_hint(p);
    term_cursor(0, 0, 0);
    term_present();
}


/* Keys. */
/* The word of row r whose first column is nearest to column c. */
static int
nearest_in_row(const Panel *p, int r, int c)
{
    int best = -1;
    int dist = 0;

    for (int i = 0; i < p->nw; i++) {
        if (p->w[i].row == r) {
            int d = p->w[i].c0 > c ? p->w[i].c0 - c : c - p->w[i].c0;

            if (best < 0 || d < dist) {
                best = i;
                dist = d;
            }
        }
    }

    return best;
}


/* The keys of the senses. */
static void
key_senses(Panel *p, int k)
{
    int b = K_BASE(k);
    int ctrl = (k & KM_CTRL) != 0;
    int row = p->hl >= 0 ? p->w[p->hl].row : -1;
    int c0 = p->hl >= 0 ? p->w[p->hl].c0 : 0;
    int to;

    if (b == K_RIGHT && p->hl + 1 < p->nw) {
        p->hl++;
    } else if (b == K_LEFT && p->hl >= 0) {
        p->hl = p->hl > 0 ? p->hl - 1 : 0;
    } else if (b == K_DOWN && row + 1 < p->nrows) {
        to = nearest_in_row(p, row + 1, c0);
        p->hl = to >= 0 ? to : p->hl;
    } else if (b == K_UP && row > 0) {
        to = nearest_in_row(p, row - 1, c0);
        p->hl = to >= 0 ? to : p->hl;
    } else if (b == K_HOME && ctrl != 0) {
        p->hl = -1;
    } else if (b == K_END && ctrl != 0) {
        for (int i = 0; i < p->nw; i++) {
            if (p->w[i].row < p->first + p->page) {
                p->hl = i;
            }
        }
    } else if (b == K_HOME && row >= 0) {
        p->hl = row_first(p, row);
    } else if (b == K_END && row >= 0) {
        p->hl = row_last(p, row);
    } else if (b == K_PGDN && ctrl == 0 && p->first + p->page < p->nrows) {
        p->first += p->page;
        p->hl = row_first(p, p->first);
    } else if (b == K_PGUP && ctrl == 0 && p->first > 0) {
        p->first = p->first > p->page ? p->first - p->page : 0;
        p->hl = row_first(p, p->first);
    }

    show_hl(p);
}


/* The keys of the list of words for a word that was not found. */
static void
key_names(Panel *p, int k)
{
    int b = K_BASE(k);
    int step = 0;
    int mark_in = p->hl;

    if (p->nnames == 0 || (k & KM_CTRL) != 0) {
        return;
    }

    if (b == K_DOWN && p->hl + 1 < p->nnames) {
        p->hl++;
    } else if (b == K_UP && p->hl > 0) {
        p->hl--;
    } else if (b == K_DOWN) {
        step = 1;
        mark_in = p->hl;
    } else if (b == K_UP) {
        step = -1;
        mark_in = p->hl;
    } else if (b == K_PGDN) {
        step = p->list;
    } else if (b == K_PGUP) {
        step = -p->list;
    }

    if (step != 0) {
        char from[TH_HEAD];

        str_copy(from, sizeof from, p->names[p->hl]);
        p->nnames = th_neighbours(from, step, p->list, mark_in, p->names,
                                  &p->hl);
    }
}


/* The keys of the Look Up box; returns 1 when the box requests a lookup. */
static int
key_box(Panel *p, int k)
{
    int b = K_BASE(k);
    int n = slen(p->box);

    if (b == K_ESC) {
        p->boxopen = 0;
    } else if (b == 13 || b == 20) {
        p->boxopen = 0;

        return n > 0;
    } else if (b == K_BKSP || b == 8) {
        if (n > 0) {
            p->box[n - 1] = '\0';
        }
    } else if (k >= 32 && k <= 255 && n + 1 < TH_HEAD) {
        p->box[n] = chr(k);
        p->box[n + 1] = '\0';
    }

    return 0;
}


/* A key that opens the Look Up box: a letter, in either panel. */
static int
opens_box(int k)
{
    return k >= 33 && k <= 255 && (k & ~0xFF) == 0 && k != 127;
}


/* The run. */
static int
run_panel(Panel *p, int from, int to)
{
    for (;;) {
        draw_panel(p);

        int k = term_getkey(-1);
        int b = K_BASE(k);

        if (p->boxopen != 0) {
            if (key_box(p, k) != 0) {
                look_up_next(p, p->box);
            }

            continue;
        }

        if (b == K_ESC) {
            return 0;
        }

        if (b == K_PGUP && (k & KM_CTRL) != 0) {
            if (p->nhist > 0) {
                char w[TH_HEAD];

                str_copy(w, sizeof w, p->hist[--p->nhist]);
                begin_lookup(p, w);
            }

            continue;
        }

        const char *cur = hl_text(p);

        if (b == 20 && cur != NULL) {
            look_up_next(p, cur);
        } else if (b == 13 && cur != NULL && p->found == 0) {
            look_up_next(p, cur);
        } else if (b == 13 && cur != NULL) {
            char out[TH_HEAD];

            th_match_case(p->orig, cur, out, sizeof out);
            ed_replace_range(p->e, from, to, bytes(out), slen(out));

            return 1;
        } else if (opens_box(k) != 0 && (k & KM_CTRL) == 0 &&
                   (k & KM_ALT) == 0) {
            p->boxopen = 1;
            p->box[0] = chr(k);
            p->box[1] = '\0';
        } else if (p->found != 0) {
            key_senses(p, k);
        } else {
            key_names(p, k);
        }
    }
}


int
thesmenu_lookup(Editor *e, int cursor_row)
{
    Panel *p;
    int from;
    int to;
    char word[TH_HEAD];

    if (th_available() == 0) {
        popup_text("Thesaurus", "The thesaurus is not installed.  Install "
                                "the package mythes-en-us; the install "
                                "script of Strata installs it.");

        return 0;
    }

    if (ed_word_at(e, &from, &to, word, sizeof word) == 0) {
        popup_msg("Thesaurus", "No word selected");

        return 0;
    }

    p = calloc(1, sizeof *p);

    if (p == NULL) {
        return 0;
    }

    p->names = calloc(TH_LIST, sizeof *p->names);

    if (p->names == NULL) {
        free(p);

        return 0;
    }

    int rows = term_rows();

    p->e = e;
    p->cols = term_cols();
    p->height = rows >= 25 ? 12 : rows / 2;
    p->page = p->height - 3;
    p->list = p->height - 2 < TH_LIST ? p->height - 2 : TH_LIST;
    p->top = cursor_row <= rows - p->height - 1 ? rows - p->height : 0;
    str_copy(p->orig, sizeof p->orig, word);

    /* The word is highlighted in the text while the panel is open. */
    Pos saved = e->cur;

    e->sel.kind = SEL_TEXT;
    e->sel.anchor = e->cur;
    e->sel.anchor.tok = from;
    e->cur.tok = to;

    begin_lookup(p, word);

    int changed = run_panel(p, from, to);

    e->sel.kind = SEL_NONE;

    if (changed == 0) {
        e->cur = saved;
    }

    clear_entry(p);
    free(p->w);
    free(p->names);
    free(p);

    return changed;
}
