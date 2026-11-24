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
 * spellui.c - the screens of the spell check
 * (docs/SPELL.txt): a bar of commands on the top lines, the unknown word, the
 * text round it, and the document, section and dictionary at the bottom.
 */
#include "spellui.h"
#include "config.h"
#include "spell.h"
#include "term.h"
#include "util.h"
#include "widgets.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum {
    M_BUSY, /* the checker is working */
    M_MENU, /* Accept Define Ignore Edit Guess Line Quit */
    M_GUESS, /* the list of suggestions */
    M_EDIT, /* the word is being edited */
    M_ACCEPT, /* Accept correction? Yes No */
    M_CONT, /* Continue with auto corrections? Yes No */
    M_NOALT, /* No alternatives found */
    M_NONE /* nothing is asked: the end */
};

enum {
    C_ACCEPT,
    C_DEFINE,
    C_IGNORE,
    C_EDIT,
    C_GUESS,
    C_LINE,
    C_QUIT,
    C_COUNT
};

#define CTX_CAP 4096
#define MAX_ROWS 200

/* Everything the screen shows; the painter reads it. */
typedef struct {
    int mode;
    int sel; /* the entry of the menu, of the list or of Yes No */
    SpellHit hit;
    int has_hit;
    SpellRun *run;
    char ctx[CTX_CAP];
    int hs;
    int he;
    char edit[SPELL_MAXWORD * 2 + 2];
    int epos;
    char docname[PATH_MAX];
    char lang[40];
    char dictpath[PATH_MAX];
    char program[64];
    const char *endmsg[3];
    int sugsel;
} Ui;

static Ui U;
static const char *const cmd_names[C_COUNT] = {"Accept", "Define", "Ignore",
                                               "Edit",   "Guess",  "Line",
                                               "Quit"};
static const char *const cmd_text[C_COUNT] = {
    "Accept word as correct",
    "Define word in the personal dictionary",
    "Ignore this word",
    "Edit the word",
    "List similar words",
    "Edit the line",
    "Quit the spell check"};


/* ============================================================== painting */
/* Break the text into rows of at most w cells; the starts go to rs. */
static int
wrap_rows(const char *s, int w, int *rs, int maxrows)
{
    int n = 0;
    int i = 0;
    int len = slen(s);

    while (n < maxrows) {
        int col = 0;
        int last = -1;
        int j = i;

        rs[n++] = i;

        while (j < len && s[j] != '\n' && col < w) {
            if (s[j] == ' ') {
                last = j;
            }

            j++;
            col++;
        }

        if (j >= len) {
            break;
        }

        if (s[j] == '\n') {
            i = j + 1;

            if (i > len) {
                break;
            }

            continue;
        }

        i = last > i ? last + 1 : j;
    }

    return n;
}


static void
put_text(int x, int y, int w, const char *s, int attr)
{
    for (int i = 0; s[i] != '\0' && i < w; i++) {
        term_put(x + i, y, u8(s[i]), attr);
    }
}


static void
paint_bar(int W)
{
    term_fill(0, 0, W, 2, ' ', TS_MENU);

    switch (U.mode) {
        case M_MENU: {
            int x = 0;

            for (int i = 0; i < C_COUNT; i++) {
                put_text(x, 0, W - x, cmd_names[i],
                         i == U.sel ? TS_MENUSEL : TS_MENU);
                x += slen(cmd_names[i]) + 2;
            }

            put_text(0, 1, W, cmd_text[U.sel], TS_MENU);
            break;
        }

        case M_GUESS: {
            int x = 0;
            int first = 0;

            /* The list scrolls so that the entry that is selected shows. */
            for (;;) {
                int used = 0;

                for (int i = first; i <= U.sel; i++) {
                    used += slen(U.hit.sug[i]) + 2;
                }

                if (used <= W || first >= U.sel) {
                    break;
                }

                first++;
            }

            for (int i = first; i < U.hit.nsug && x < W; i++) {
                put_text(x, 0, W - x, U.hit.sug[i],
                         i == U.sel ? TS_MENUSEL : TS_MENU);
                x += slen(U.hit.sug[i]) + 2;
            }

            put_text(0, 1, W, "Select word", TS_MENU);
            break;
        }

        case M_ACCEPT:
        case M_CONT: {
            const char *q = U.mode == M_ACCEPT ?
                                "Accept correction? " :
                                "Continue with auto corrections? ";
            int x = slen(q);

            put_text(0, 0, W, q, TS_MENU);
            put_text(x, 0, W - x, "Yes", U.sel == 0 ? TS_MENUSEL : TS_MENU);
            put_text(x + 4, 0, W - x - 4, "No", U.sel == 1 ? TS_MENUSEL : TS_MENU);
            break;
        }

        case M_NOALT:
            put_text(0, 0, W, "No alternatives found", TS_MENU);
            break;

        case M_BUSY:
            put_text(0, 1, W, "Checking the spelling (Esc quits)",
                     TS_MENU);
            break;

        case M_EDIT:
        case M_NONE:
        default:
            break;
    }
}


static void
paint_context(int W, int y0, int rows)
{
    int rs[MAX_ROWS];
    int n;
    int hitrow = 0;
    int first;
    int len = slen(U.ctx);

    if (U.has_hit == 0 || rows < 1) {
        return;
    }

    n = wrap_rows(U.ctx, W, rs, MAX_ROWS);

    for (int k = 0; k < n; k++) {
        int end = k + 1 < n ? rs[k + 1] : len + 1;

        if (U.hs >= rs[k] && U.hs < end) {
            hitrow = k;
        }
    }

    /* The row with the word is the second one when there is a row before. */
    first = hitrow > 0 ? hitrow - 1 : 0;

    if (hitrow - first >= rows) {
        first = hitrow - rows + 1;
    }

    for (int k = first; k < n && k - first < rows; k++) {
        int end = k + 1 < n ? rs[k + 1] : len;

        for (int i = rs[k], x = 0; i < end && x < W && U.ctx[i] != '\n';
             i++, x++) {
            term_put(x, y0 + k - first, u8(U.ctx[i]),
                     i >= U.hs && i < U.he ? TA_REVERSE : 0);
        }
    }
}


static void
paint(void)
{
    int W = term_cols();
    int H = term_rows();
    int ctxrows = H - 14 < 3 ? 3 : H - 14;
    int yinfo = 8 + ctxrows + 1;
    char ln[PATH_MAX + 40];

    term_clear();
    paint_bar(W);

    if (U.has_hit != 0) {
        put_text(0, 3, W, "Unknown word: ", 0);
        put_text(14, 3, W - 14, U.hit.word, TA_REVERSE);

        if (U.mode == M_EDIT) {
            put_text(0, 5, W, "Correct word: ", 0);
            put_text(14, 5, W - 14, U.edit, TA_REVERSE);
            term_cursor(14 + U.epos, 5, 1);
        } else if (U.mode == M_ACCEPT) {
            put_text(0, 5, W, "Correct word: ", 0);
            put_text(14, 5, W - 14, U.hit.sug[0], TA_REVERSE);
        }
    }

    if (U.mode != M_EDIT) {
        term_cursor(0, 0, 0);
    }

    term_fill(0, 7, W, 1, 0xC4, TA_DIM);
    paint_context(W, 8, ctxrows);
    term_fill(0, 8 + ctxrows, W, 1, 0xC4, TA_DIM);
    term_fill(0, yinfo, W, 4, ' ', TS_STATUS);
    snprintf(ln, sizeof ln, "Document:     %s", U.docname);
    put_text(0, yinfo, W, ln, TS_STATUS);
    snprintf(ln, sizeof ln, "Section:      %s",
             U.has_hit != 0 && U.hit.in_note != 0 ? "Body (Note)" : "Body");
    put_text(0, yinfo + 1, W, ln, TS_STATUS);
    snprintf(ln, sizeof ln, "Dictionaries: %s (%s)", U.lang, U.program);
    put_text(0, yinfo + 2, W, ln, TS_STATUS);
    snprintf(ln, sizeof ln, "              %s", U.dictpath);
    put_text(0, yinfo + 3, W, ln, TS_STATUS);

    if (U.mode == M_NONE) {
        int bw = 30;
        int bx = (W - bw) / 2;
        int lines = 0;

        for (int i = 0; i < 3; i++) {
            if (U.endmsg[i] != NULL) {
                lines++;
            }
        }

        box(bx, 9, bw, lines + 2, NULL);

        for (int i = 0, y = 10; i < 3; i++) {
            if (U.endmsg[i] != NULL) {
                put_text(bx + (bw - slen(U.endmsg[i])) / 2, y++, bw - 2,
                         U.endmsg[i], 0);
            }
        }
    }
}


/* Draw and wait for a key; a resize only draws again. */
static int
key(void)
{
    for (;;) {
        paint();
        term_present();

        int k = term_getkey(-1);

        if (k != K_RESIZE && k != K_NONE) {
            return k;
        }
    }
}


static int
is_enter(int k)
{
    return K_BASE(k) == 13 || K_BASE(k) == K_INS;
}


/* ================================================================ steps */
/* Yes or No on the bar: 1 for yes. */
static int
ask_yes_no(int mode)
{
    int saved = U.mode;

    U.mode = mode;
    U.sel = 0;

    for (;;) {
        int k = key();
        int b = K_BASE(k);

        if (b == K_LEFT || b == K_RIGHT) {
            U.sel = U.sel == 0 ? 1 : 0;
        } else if (b == 'y' || b == 'Y') {
            U.mode = saved;

            return 1;
        } else if (b == 'n' || b == 'N' || b == K_ESC) {
            U.mode = saved;

            return 0;
        } else if (is_enter(k) != 0) {
            U.mode = saved;

            return U.sel == 0;
        }
    }
}


/* The list of suggestions; the word chosen, or NULL for Esc. */
static const char *
guess_list(void)
{
    if (U.hit.nsug == 0 || U.hit.no_guess != 0) {
        U.mode = M_NOALT;
        key();
        U.mode = M_MENU;

        return NULL;
    }

    U.mode = M_GUESS;
    U.sel = 0;

    for (;;) {
        int k = key();
        int b = K_BASE(k);

        if (b == K_LEFT && U.sel > 0) {
            U.sel--;
        } else if (b == K_RIGHT && U.sel + 1 < U.hit.nsug) {
            U.sel++;
        } else if (b == K_HOME) {
            U.sel = 0;
        } else if (b == K_END) {
            U.sel = U.hit.nsug - 1;
        } else if (b == K_ESC) {
            U.mode = M_MENU;

            return NULL;
        } else if (is_enter(k) != 0) {
            U.mode = M_MENU;

            return U.hit.sug[U.sel];
        }
    }
}


/* Edit the word on the line "Correct word"; 1 when it is accepted. */
static int
edit_word(char *out, size_t cap)
{
    int len;

    str_copy(U.edit, sizeof U.edit, U.hit.word);
    U.epos = slen(U.edit);
    U.mode = M_EDIT;

    for (;;) {
        int k = key();
        int b = K_BASE(k);

        len = slen(U.edit);

        if (b == K_LEFT && U.epos > 0) {
            U.epos--;
        } else if (b == K_RIGHT && U.epos < len) {
            U.epos++;
        } else if (b == K_HOME) {
            U.epos = 0;
        } else if (b == K_END) {
            U.epos = len;
        } else if (b == K_BKSP && U.epos > 0) {
            memmove(U.edit + U.epos - 1, U.edit + U.epos,
                    usz(len - U.epos + 1));
            U.epos--;
        } else if (b == K_DEL && U.epos < len) {
            memmove(U.edit + U.epos, U.edit + U.epos + 1,
                    usz(len - U.epos));
        } else if (b == K_ESC) {
            U.mode = M_MENU;

            return 0;
        } else if (is_enter(k) != 0) {
            str_copy(out, cap, U.edit);
            U.mode = M_MENU;

            return len > 0;
        } else if ((k & ~0xFF) == 0 && k >= ' ' && len + 1 < SPELL_MAXWORD * 2) {
            memmove(U.edit + U.epos + 1, U.edit + U.epos,
                    usz(len - U.epos + 1));
            U.edit[U.epos++] = chr(k);
        }
    }
}


/* What the user chose for the word; 1 to go on, 0 to quit. */
static int
handle_hit(int *changes)
{
    char err[200];
    char buf[SPELL_MAXWORD * 2 + 2];
    int hs;
    int he;

    spellrun_context(U.run, U.ctx, sizeof U.ctx, &hs, &he, 4);
    U.hs = hs;
    U.he = he;
    U.has_hit = 1;

    /* One suggestion that only differs in case is offered as it is. */
    if (U.hit.nsug == 1 && strcasecmp(U.hit.sug[0], U.hit.word) == 0) {
        if (ask_yes_no(M_ACCEPT) != 0) {
            spellrun_replace(U.run, U.hit.sug[0]);
            (*changes)++;
        }

        return 1;
    }

    U.mode = M_MENU;
    U.sel = C_ACCEPT;

    for (;;) {
        int k = key();
        int b = K_BASE(k);
        int c = -1;

        if (b == K_LEFT && U.sel > 0) {
            U.sel--;
        } else if (b == K_RIGHT && U.sel + 1 < C_COUNT) {
            U.sel++;
        } else if (is_enter(k) != 0) {
            c = U.sel;
        } else if ((k & ~0xFF) == 0 && k >= ' ') {
            for (int i = 0; i < C_COUNT; i++) {
                if (to_lower(cmd_names[i][0]) == to_lower(b)) {
                    c = i;
                }
            }
        }

        switch (c) {
            case C_ACCEPT:
                spellrun_accept(U.run);
                (*changes)++;

                return 1;

            case C_DEFINE:
                if (spellrun_define(U.run, err, sizeof err) != 0) {
                    popup_msg("Define", err);
                } else {
                    (*changes)++;
                }

                return 1;

            case C_IGNORE:
                return 1;

            case C_EDIT:
                if (edit_word(buf, sizeof buf) != 0) {
                    spellrun_replace(U.run, buf);
                    (*changes)++;

                    return 1;
                }

                break;

            case C_GUESS: {
                const char *g = guess_list();

                if (g != NULL) {
                    spellrun_replace(U.run, g);
                    (*changes)++;

                    return 1;
                }

                break;
            }

            case C_LINE: {
                char line[1024];

                spellrun_line(U.run, line, sizeof line);

                w_set_help("Spell Check:Line");
                int corrected = popup_input("Line", "Corrected line:", line,
                                            isz(sizeof line));

                w_set_help(NULL);

                if (corrected != 0) {
                    if (spellrun_set_line(U.run, line) == 0) {
                        (*changes)++;

                        return 1;
                    }

                    popup_msg("Line", "A tab or a marker cannot be changed "
                                      "here");
                }

                break;
            }

            case C_QUIT:
                return 0;

            default:
                break;
        }
    }
}


/* The text of the check: the screens, the words, the end. */
static void
run_screens(void)
{
    int changes = 0;
    int quit = 0;
    int done = 0;

    U.mode = M_BUSY;
    U.has_hit = 0;

    while (done == 0 && quit == 0) {
        int rc = spellrun_next(U.run, 40, &U.hit);

        if (rc == SR_BUSY) {
            int k;

            U.mode = M_BUSY;
            U.has_hit = 0;
            paint();
            term_present();
            k = term_getkey(0);

            if (K_BASE(k) == K_ESC) {
                quit = 1;
            }

            continue;
        }

        if (rc == SR_DONE) {
            done = 1;
        } else if (rc == SR_ERROR) {
            popup_msg("Spell Check", "The spelling checker stopped.");
            quit = 1;
        } else if (handle_hit(&changes) == 0) {
            quit = 1;
        }
    }

    /* Quit with corrections that are to be made again in the rest. */
    if (quit != 0 && spellrun_pending_autocorrect(U.run) != 0 &&
        ask_yes_no(M_CONT) != 0) {
        spellrun_finish_autocorrect(U.run);
    }

    U.mode = M_NONE;
    U.has_hit = 0;
    U.endmsg[0] = quit != 0 ? "Spell Check terminated" :
                              "Spell Check completed";
    U.endmsg[1] = changes == 0 ? "No spelling errors found" : NULL;
    U.endmsg[2] = "Press any key to exit";
    key();
}


/* ================================================================= entry */
static void
setup_ui(const char *docname)
{
    char prog[PATH_MAX];

    memset(&U, 0, sizeof U);
    str_copy(U.docname, sizeof U.docname, docname);
    spell_language(U.lang, sizeof U.lang);
    spell_dict_path(U.dictpath, sizeof U.dictpath);

    if (spell_find_program(prog, sizeof prog) == 0) {
        const char *sl = strrchr(prog, '/');

        str_copy(U.program, sizeof U.program, sl != NULL ? sl + 1 : prog);
    }
}


/*
 * The check of the items lo to hi of d.  Returns -1 when the checker could not
 * be started (a message was shown).
 */
static int
run_check(Editor *e, Doc *d, int lo, int hi, int autocorrect,
          const char *docname)
{
    char err[400];
    char lang[40];
    SpellProc *proc;
    SpellDict dict;
    void (*saved)(void) = w_redraw;

    spell_language(lang, sizeof lang);
    proc = spell_proc_open(lang, err, sizeof err);

    if (proc == NULL) {
        popup_text("Spell", err);

        return -1;
    }

    spell_dict_load(&dict);
    setup_ui(docname);
    U.run = spellrun_new(d, lo, hi, proc, &dict, autocorrect);

    if (e != NULL) {
        spellrun_track(U.run, &e->cur.item, &e->cur.col, &e->cur.tok);
    }

    w_redraw = paint;
    run_screens();
    w_redraw = saved;
    spellrun_free(U.run);
    U.run = NULL;
    spell_dict_free(&dict);
    spell_proc_close(proc);

    if (e != NULL) {
        ed_invalidate(e);
        ed_fix_cursor(e);
    }

    return 0;
}


/* Is there a checker?  When not, say what to install. */
static int
have_checker(void)
{
    char prog[PATH_MAX];

    if (spell_find_program(prog, sizeof prog) == 0) {
        return 1;
    }

    popup_text("Spell", "No spelling checker was found.  Install the "
                        "packages hunspell and hunspell-en-us (or aspell "
                        "and aspell-en); the install script of Strata "
                        "installs them.");

    return 0;
}


static int auto_correct = 1;


void
spellui_check(Editor *e, int lo, int hi)
{
    static const char *const yn[] = {"Yes", "No", NULL};
    WField f[4];
    char prog[PATH_MAX];

    if (have_checker() == 0) {
        return;
    }

    memset(f, 0, sizeof f);
    f[0].label = "Document Name";
    f[0].kind = WF_INFO;
    str_copy(f[0].val, sizeof f[0].val,
             e->doc->path != NULL ? e->doc->path : "(not saved)");
    f[1].label = "Custom Spelling Dictionary";
    f[1].kind = WF_INFO;
    spell_dict_path(f[1].val, sizeof f[1].val);
    f[2].label = "Dictionary Language";
    f[2].kind = WF_INFO;
    spell_language(f[2].val, sizeof f[2].val);

    if (spell_find_program(prog, sizeof prog) == 0) {
        const char *sl = strrchr(prog, '/');

        snprintf(f[2].val + strlen(f[2].val),
                 sizeof f[2].val - strlen(f[2].val), " (%s)",
                 sl != NULL ? sl + 1 : prog);
    }

    f[3].label = "Auto Correct?";
    f[3].kind = WF_CHOICE;
    f[3].choices = yn;
    f[3].sel = auto_correct != 0 ? 0 : 1;

    if (popup_form("Check Document Spelling", f, 4, NULL) == 0) {
        return;
    }

    auto_correct = f[3].sel == 0;
    run_check(e, e->doc, lo, hi, auto_correct,
              e->doc->path != NULL ? e->doc->path : "(not saved)");
}


/* ============================================================ Spell Word */
/* The panel of a word; returns the index of the suggestion chosen, or -1. */
static int
word_panel(const char *word, const char *msg, char sug[][SPELL_MAXWORD + 1],
           int n)
{
    int sel = 0;

    for (;;) {
        int W = term_cols();
        int H = term_rows();
        int w = 40;
        int h = n > 0 ? 9 : 6;
        int first = 0;
        int x;
        int y;
        const char *bottom = n > 0 ? " Press Ins to select word " :
                                     " Press any key to continue ";

        if (slen(msg) + 6 > w) {
            w = slen(msg) + 6;
        }

        if (w > W - 2) {
            w = W - 2;
        }

        x = (W - w) / 2;
        y = (H - h) / 2;
        w_base();
        box(x, y, w, h, "Spell Word");
        put_text(x + (w - slen(word)) / 2, y + 2, w - 2, word, TA_BOLD);
        put_text(x + (w - slen(msg)) / 2, y + 3, w - 2, msg, TA_BOLD);
        put_text(x + (w - slen(bottom)) / 2, y + h - 1, w, bottom, 0);

        if (n > 0) {
            int cx;

            put_text(x + (w - 22) / 2, y + 5, w - 2, "Suggested alternatives:",
                     TA_BOLD);

            for (;;) {
                int used = 0;

                for (int i = first; i <= sel; i++) {
                    used += slen(sug[i]) + 1;
                }

                if (used <= w - 3 || first >= sel) {
                    break;
                }

                first++;
            }

            cx = x + 2;

            for (int i = first; i < n && cx < x + w - 2; i++) {
                put_text(cx, y + 6, x + w - 2 - cx, sug[i],
                         i == sel ? TA_REVERSE : 0);
                cx += slen(sug[i]) + 1;
            }
        }

        term_cursor(0, 0, 0);
        term_present();

        int k = term_getkey(-1);
        int b = K_BASE(k);

        if (k == K_RESIZE || k == K_NONE) {
            continue;
        }

        if (n == 0) {
            return -1;
        }

        if (b == K_LEFT && sel > 0) {
            sel--;
        } else if (b == K_RIGHT && sel + 1 < n) {
            sel++;
        } else if (b == K_ESC) {
            return -1;
        } else if (is_enter(k) != 0) {
            return sel;
        }
    }
}


void
spellui_word(Editor *e)
{
    char err[400];
    char lang[40];
    Text *t = ed_text(e);
    SpellWord w;
    SpellProc *proc;
    SpellDict dict;
    SpellMiss m;
    char sug[SPELL_MAXSUG][SPELL_MAXWORD + 1];
    int n = 0;
    int right = 0;
    int rc = 1;

    if (have_checker() == 0) {
        return;
    }

    if (t == NULL || spell_word_at(t, e->cur.tok, &w) == 0) {
        popup_msg("Spell Word", "There is no word at the cursor");

        return;
    }

    if (w.too_long != 0) {
        char msg[80];

        snprintf(msg, sizeof msg,
                 "Word is too long - maximum length is %d characters",
                 SPELL_MAXWORD);
        word_panel(w.text, msg, sug, 0);

        return;
    }

    spell_language(lang, sizeof lang);
    proc = spell_proc_open(lang, err, sizeof err);

    if (proc == NULL) {
        popup_text("Spell", err);

        return;
    }

    spell_dict_load(&dict);

    if (spell_dict_has(&dict, w.text) != 0) {
        right = 1;
    } else if (spell_has_digit(w.text) != 0) {
        right = 0;
    } else {
        rc = spell_proc_check_word(proc, w.text, &m);

        if (rc < 0) {
            popup_msg("Spell Word", "The spelling checker stopped.");
            spell_dict_free(&dict);
            spell_proc_close(proc);

            return;
        }

        right = rc == 1;

        if (right == 0) {
            n = spell_whole_words(w.text, 0, &m, sug, SPELL_MAXSUG);
        }
    }

    spell_dict_free(&dict);
    spell_proc_close(proc);

    int pick = word_panel(w.text,
                          right != 0 ? "Spelled correctly" :
                                       "Not found in dictionary",
                          sug, n);

    if (pick >= 0) {
        Doc *d = e->doc;

        spell_replace_word(t, &w, sug[pick]);

        if (d->items[e->cur.item].blk != NULL) {
            d->items[e->cur.item].blk->dirty = 1;
        }

        doc_touch(d);
        e->cur.tok = w.start;
        ed_invalidate(e);
        ed_fix_cursor(e);
    }
}


/* ============================================================ the file */
int
spellui_file(const char *path, int autocorrect)
{
    char err[200];
    Doc *d = doc_load(path, err, sizeof err);

    if (d == NULL) {
        fprintf(stderr, "%s: %s\n", path, err);

        return 1;
    }

    if (term_open() != 0) {
        fprintf(stderr, "stred --spell needs an interactive terminal\n");
        doc_free(d);

        return 1;
    }

    w_redraw = NULL;

    if (run_check(NULL, d, 0, d->nitems - 1, autocorrect, path) == 0 &&
        d->modified != 0) {
        if (popup_yn("Save corrected document?") != 0) {
            if (doc_save(d, path, err, sizeof err) != 0) {
                popup_msg("Save", err);
            }
        }
    }

    term_close();
    doc_free(d);

    return 0;
}
