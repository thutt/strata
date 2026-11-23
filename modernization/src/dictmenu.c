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
 * dictmenu.c - the panel Maintain Dictionaries of the Document Manager.
 *
 * The list is the personal word list that the spell check reads.  Every
 * change reads the file again, applies the change and writes the file, so a
 * word that the Editor's Define added while the panel was open is not lost.
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "util.h"
#include "term.h"
#include "widgets.h"
#include "dictlist.h"
#include "hyphlist.h"
#include "dictmenu.h"

#define TITLE "Maintain Dictionaries"
#define PANEL_WIDTH 60
#define FIND_MAX 40

enum {
    F_DIR,
    F_FILE,
    F_COUNT
};

typedef struct {
    int hy; /* 0 the spelling list, 1 the hyphenation list */
    SpellDict d; /* the words, in order */
    int sel; /* the entry that has the cursor */
    int top; /* the first entry that is shown */
    char find[FIND_MAX]; /* the letters typed to find a word */
    char msg[160]; /* the last thing that happened */
} Panel;


static int
file_exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}


/* The operations of the list that the panel shows: spelling or hyphenation. */
static void
list_load(Panel *p)
{
    if (p->hy != 0) {
        dicthyph_load(&p->d);
    } else {
        dictlist_load(&p->d);
    }
}


static int
list_check(const Panel *p, const char *word, char *why, size_t cap)
{
    return p->hy != 0 ? hyph_check(word, why, cap)
                      : dictlist_check_word(word, why, cap);
}


static int
list_find(const Panel *p, const char *word)
{
    return p->hy != 0 ? dicthyph_find(&p->d, word)
                      : dictlist_find(&p->d, word);
}


static int
list_prefix(const Panel *p, const char *prefix)
{
    return p->hy != 0 ? dicthyph_prefix(&p->d, prefix)
                      : dictlist_prefix(&p->d, prefix);
}


/* The full path of a name typed in the panel of an import or export. */
static void
full_path(const char *dir, const char *name, char *out, size_t cap)
{
    if (name[0] == '/' || dir[0] == '\0') {
        snprintf(out, cap, "%s", name);
    } else {
        snprintf(out, cap, "%s/%s", dir, name);
    }
}


/* Read the file again: the list is what is on the disk now. */
static void
reload(Panel *p)
{
    spell_dict_free(&p->d);
    list_load(p);

    if (p->sel >= p->d.n) {
        p->sel = p->d.n > 0 ? p->d.n - 1 : 0;
    }
}


/* Put the cursor on a word that is in the list (or near where it would be). */
static void
select_word(Panel *p, const char *word)
{
    int at = list_find(p, word);

    if (at < 0) {
        at = list_prefix(p, word);
    }

    if (at >= 0) {
        p->sel = at;
    }
}


static void
paint(const Panel *p)
{
    int R = term_rows();
    int C = term_cols();
    int w = C - 2 < PANEL_WIDTH ? C - 2 : PANEL_WIDTH;
    int h = R - 4;
    int vis;
    int x;
    int y;
    char line[128];

    w_base();
    x = (C - w) / 2;
    y = (R - h) / 2;
    box(x, y, w, h, TITLE);
    vis = h - 5;
    if (p->hy != 0) {
        snprintf(line, sizeof line, "Hyphenation, your words: %d   "
                                    "Tab: Spelling", p->d.n);
    } else {
        snprintf(line, sizeof line, "Spelling, your words: %d   "
                                    "Tab: Hyphenation", p->d.n);
    }

    term_puts(x + 2, y + 1, line, 0);

    for (int i = 0; i < vis && p->top + i < p->d.n; i++) {
        int at = p->top + i;
        int attr = at == p->sel ? TS_SELECT : 0;

        if (at == p->sel) {
            term_fill(x + 1, y + 2 + i, w - 2, 1, ' ', attr);
        }

        term_puts(x + 3, y + 2 + i, p->d.w[at], attr);
    }

    if (p->d.n == 0) {
        term_puts(x + 3, y + 2, "No words yet.  Press Ins to add one.", 0);
    }

    if (p->top > 0) {
        term_put(x + w - 3, y + 2, 0x18, 0);
    }

    if (p->top + vis < p->d.n) {
        term_put(x + w - 3, y + 1 + vis, 0x19, 0);
    }

    if (p->find[0] != '\0') {
        snprintf(line, sizeof line, "Find: %s", p->find);
        term_puts(x + 2, y + h - 3, line, 0);
    } else {
        term_puts(x + 2, y + h - 3, p->msg, 0);
    }

    term_puts(x + 2, y + h - 2, "Ins Add  Del Delete  F3 Import  F4 Export  "
                                "Esc Leave",
              0);

    if (p->hy != 0 && p->d.n == 0) {
        term_puts(x + 3, y + 3, "A word has a hyphen where it may be broken: "
                                "hy-phen-a-tion.", 0);
        term_puts(x + 3, y + 4, "A word with none is never broken.", 0);
    }
    term_cursor(0, 0, 0);
    term_present();
}


/* Keep the cursor in view. */
static void
scroll(Panel *p)
{
    int vis = term_rows() - 4 - 5;

    if (vis < 1) {
        vis = 1;
    }

    if (p->sel < p->top) {
        p->top = p->sel;
    }

    if (p->sel >= p->top + vis) {
        p->top = p->sel - vis + 1;
    }

    if (p->top < 0) {
        p->top = 0;
    }
}


/* Write the list; a failure is shown and the list is read again. */
static int
save_list(Panel *p)
{
    char err[300] = "";

    if ((p->hy != 0 ? dicthyph_save(&p->d, err, sizeof err)
                    : dictlist_save(&p->d, err, sizeof err)) != 0) {
        popup_msg(TITLE, err);
        reload(p);

        return -1;
    }

    return 0;
}


static void
add_word(Panel *p)
{
    char word[SPELL_MAXWORD + 40] = "";
    char why[96];

    if (popup_input(p->hy != 0 ? "Add a hyphenated word" : "Add a word",
                    p->hy != 0 ? "Word, as hy-phen-a-tion:" : "Word:", word,
                    isz(sizeof word)) == 0 ||
        word[0] == '\0') {
        return;
    }

    if (list_check(p, word, why, sizeof why) == 0) {
        popup_msg(TITLE, why);

        return;
    }

    reload(p);

    if ((p->hy != 0 ? dicthyph_add(&p->d, word)
                    : dictlist_add(&p->d, word)) == 0) {
        select_word(p, word);
        snprintf(p->msg, sizeof p->msg, "%.60s is in the list already", word);

        return;
    }

    if (save_list(p) == 0) {
        select_word(p, word);
        snprintf(p->msg, sizeof p->msg, "Added %.60s", word);
    }
}


static void
delete_word(Panel *p)
{
    char word[SPELL_MAXWORD * 4 + 1];
    char ask[SPELL_MAXWORD * 4 + 40];
    int at;

    if (p->d.n == 0) {
        return;
    }

    str_copy(word, sizeof word, p->d.w[p->sel]);
    snprintf(ask, sizeof ask, "Delete %s from your words?", word);

    if (popup_yn(ask) == 0) {
        return;
    }

    reload(p);
    at = list_find(p, word);

    if (at < 0) {
        snprintf(p->msg, sizeof p->msg, "%.60s is not in the list", word);

        return;
    }

    dictlist_remove(&p->d, at);

    if (save_list(p) == 0) {
        if (p->sel >= p->d.n) {
            p->sel = p->d.n > 0 ? p->d.n - 1 : 0;
        }

        snprintf(p->msg, sizeof p->msg, "Deleted %.60s", word);
    }
}


/* A name is needed to import or export. */
static int
check_file(const WField *f, int n, char *err, int errcap)
{
    if (n >= F_COUNT && f[F_FILE].val[0] == '\0') {
        snprintf(err, usz(errcap), "Enter the name of the file");

        return 0;
    }

    return 1;
}


/* Ask for a file; returns 1 and its full path, or 0. */
static int
ask_file(const char *title, const char *workdir, const char *dflt, char *out,
         size_t cap)
{
    WField f[F_COUNT];

    memset(f, 0, sizeof f);
    f[F_DIR].label = "Working Directory";
    f[F_DIR].kind = WF_TEXT;
    f[F_DIR].pick = PICK_DIR;
    str_copy(f[F_DIR].val, sizeof f[F_DIR].val, workdir);
    f[F_FILE].label = "File Name";
    f[F_FILE].kind = WF_TEXT;
    f[F_FILE].pick = PICK_FILE;
    f[F_FILE].suffixes = "";
    str_copy(f[F_FILE].val, sizeof f[F_FILE].val, dflt);

    if (popup_form(title, f, F_COUNT, check_file) == 0) {
        return 0;
    }

    full_path(f[F_DIR].val, f[F_FILE].val, out, cap);

    return 1;
}


static void
import_words(Panel *p, const char *workdir)
{
    char path[PATH_MAX];
    char err[300] = "";
    DictImport r;
    int rc;

    if (ask_file("Import Words", workdir, "", path, sizeof path) == 0) {
        return;
    }

    reload(p);
    rc = p->hy != 0 ? dicthyph_import(&p->d, path, &r, err, sizeof err)
                    : dictlist_import(&p->d, path, &r, err, sizeof err);

    if (rc != 0) {
        popup_msg(TITLE, err);

        return;
    }

    if (r.added > 0 && save_list(p) != 0) {
        return;
    }

    snprintf(p->msg, sizeof p->msg,
             "Imported: %d added, %d in the list already, %d not words",
             r.added, r.duplicate, r.refused);
    popup_msg(TITLE, p->msg);
}


static void
export_words(Panel *p, const char *workdir)
{
    char path[PATH_MAX];
    char err[300] = "";
    char ask[PATH_MAX + 40];

    if (ask_file("Export Words", workdir,
                 p->hy != 0 ? "hyphenation.txt" : "words.txt", path,
                 sizeof path) == 0) {
        return;
    }

    if (file_exists(path) != 0) {
        snprintf(ask, sizeof ask, "%s exists.  Replace it?", path);

        if (popup_yn(ask) == 0) {
            return;
        }
    }

    reload(p);

    if (dictlist_export(&p->d, path, err, sizeof err) != 0) {
        popup_msg(TITLE, err);

        return;
    }

    snprintf(p->msg, sizeof p->msg, "Wrote %d words to %.80s", p->d.n, path);
    popup_msg(TITLE, p->msg);
}


/* A letter typed to find a word: the cursor goes to the first match. */
static void
find_letter(Panel *p, int c)
{
    size_t n = strlen(p->find);
    char tried[FIND_MAX];
    int at;

    if (n + 1 >= sizeof p->find) {
        return;
    }

    str_copy(tried, sizeof tried, p->find);
    tried[n] = chr(c);
    tried[n + 1] = '\0';
    at = list_prefix(p, tried);

    if (at < 0) {
        snprintf(p->msg, sizeof p->msg, "No word begins with %s", tried);
        p->find[0] = '\0';

        return;
    }

    str_copy(p->find, sizeof p->find, tried);
    p->sel = at;
}


void
dict_panel(const char *workdir)
{
    Panel p;

    memset(&p, 0, sizeof p);
    list_load(&p);
    w_set_help(TITLE);

    for (;;) {
        int k;
        int base;

        scroll(&p);
        paint(&p);
        k = term_getkey(-1);
        base = K_BASE(k);

        if (k == K_F(1)) {
            help_context(TITLE);
            continue;
        }

        if (base == K_ESC) {
            break;
        }

        if (base == K_RESIZE) {
            continue;
        }

        p.msg[0] = '\0';

        if (base == K_UP || base == K_DOWN || base == K_PGUP ||
            base == K_PGDN || base == K_HOME || base == K_END) {
            int vis = term_rows() - 4 - 5;

            p.find[0] = '\0';

            if (base == K_UP && p.sel > 0) {
                p.sel--;
            } else if (base == K_DOWN && p.sel < p.d.n - 1) {
                p.sel++;
            } else if (base == K_PGUP) {
                p.sel = p.sel > vis ? p.sel - vis : 0;
            } else if (base == K_PGDN) {
                p.sel = p.sel + vis < p.d.n ? p.sel + vis : p.d.n - 1;
            } else if (base == K_HOME) {
                p.sel = 0;
            } else if (base == K_END && p.d.n > 0) {
                p.sel = p.d.n - 1;
            }
        } else if (k == 9) {
            /* The other list. */
            spell_dict_free(&p.d);
            p.hy = p.hy == 0;
            p.sel = 0;
            p.top = 0;
            p.find[0] = '\0';
            list_load(&p);
        } else if (base == K_INS) {
            p.find[0] = '\0';
            add_word(&p);
        } else if (base == K_DEL) {
            p.find[0] = '\0';
            delete_word(&p);
        } else if (k == K_F(3)) {
            p.find[0] = '\0';
            import_words(&p, workdir);
        } else if (k == K_F(4)) {
            p.find[0] = '\0';
            export_words(&p, workdir);
        } else if (base == K_BKSP) {
            size_t n = strlen(p.find);

            if (n > 0) {
                p.find[n - 1] = '\0';

                if (p.find[0] != '\0' && list_prefix(&p, p.find) >= 0) {
                    p.sel = list_prefix(&p, p.find);
                }
            }
        } else if (k == base && base >= 33 && base < 127) {
            find_letter(&p, base);
        }
    }

    w_set_help(NULL);
    spell_dict_free(&p.d);
}
