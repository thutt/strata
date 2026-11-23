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

/* menu.c - the F12 menu bar; see menu.h. */
#include "util.h"
#include "term.h"
#include "menu.h"
#include "widgets.h"
#include <stdio.h>
#include <string.h>

/* One open level of the menu. */
typedef struct {
    const MenuItem *items;
    int n;
    int sel;
} Level;


/*
 * Append a colon and a name to the place text.  The text is cut where the
 * buffer ends and stays terminated.  The new length is returned.
 */
static size_t
place_append(char *buf, size_t cap, size_t len, const char *name)
{
    size_t room;
    size_t n;

    if (len + 1 >= cap) {
        return len;
    }

    buf[len] = ':';
    len++;
    buf[len] = '\0';
    room = cap - len - 1;
    n = strlen(name);

    if (n > room) {
        n = room;
    }

    memcpy(buf + len, name, n);
    len += n;
    buf[len] = '\0';

    return len;
}


/* The label at the right end of the bar: MENU, MENU-GLOBAL and so on. */
static void
make_label(char *buf, size_t cap, const Level *lv, int depth)
{
    size_t len = 0;

    snprintf(buf, cap, "MENU");
    len = strlen(buf);

    for (int i = 0; i < depth && len + 2 < cap; i++) {
        const char *nm = lv[i].items[lv[i].sel].name;

        buf[len++] = '-';

        for (; *nm != '\0' && len + 1 < cap; nm++) {
            buf[len++] = (char)to_upper(*nm);
        }

        buf[len] = '\0';
    }
}


/*
 * Place the names of the entries of a level on the bar.  The names go from
 * left to right, one space apart.  The first row also holds the label of the
 * mode at its right end, so the names of that row stop before the label; a
 * name that does not fit on the rest of a row begins the next row, so that
 * every name is whole on the screen and no name is run over by the label.
 * The position of each name is stored in xs and ys, and the number of rows
 * that the names fill is returned.
 */
static int
bar_layout(const Level *cur, int cols, int label_w, int *xs, int *ys)
{
    int n = cur->n > MENU_MAXITEMS ? MENU_MAXITEMS : cur->n;
    int row = 0;
    int x = 0;

    for (int i = 0; i < n; i++) {
        int w = slen(cur->items[i].name);
        int room = row == 0 ? cols - label_w - 1 : cols;

        if (x > 0 && x + w > room) {
            row++;
            x = 0;
        }

        xs[i] = x;
        ys[i] = row;
        x += w + 1;
    }

    return row + 1;
}


static void
draw_bar(const Level *lv, int depth, const char *note)
{
    const Level *cur = &lv[depth];
    int cols = term_cols();
    char label[64];
    int xs[MENU_MAXITEMS];
    int ys[MENU_MAXITEMS];
    int n = cur->n > MENU_MAXITEMS ? MENU_MAXITEMS : cur->n;
    int rows;

    make_label(label, sizeof label, lv, depth);
    rows = bar_layout(cur, cols, slen(label), xs, ys);
    term_fill(0, 0, cols, rows + 1, ' ', TS_MENU);

    for (int i = 0; i < n; i++) {
        const MenuItem *it = &cur->items[i];
        int w = slen(it->name);
        int attr = it->avail != 0 ? TS_MENU : TS_MENU | TA_DIM;

        if (i == cur->sel) {
            term_fill(xs[i], ys[i], w, 1, ' ', TS_MENUSEL);
            attr = it->avail != 0 ? TS_MENUSEL | TA_BOLD : TS_MENUSEL | TA_DIM;
        }

        term_puts(xs[i], ys[i], it->name, attr);
    }

    term_puts(cols - slen(label), 0, label, TS_MENU);
    term_puts(0, rows, note != NULL ? note : cur->items[cur->sel].hint,
              TS_MENU);
}


/* The entry whose first letter is c, or -1. */
static int
find_letter(const Level *cur, int c)
{
    for (int i = 0; i < cur->n; i++) {
        if (to_upper(cur->items[i].name[0]) == to_upper(c)) {
            return i;
        }
    }

    return -1;
}


int
menu_run(const MenuItem *top, int ntop, void (*redraw)(void),
         int path[MENU_MAXDEPTH])
{
    Level lv[MENU_MAXDEPTH];
    int depth = 0;
    const char *note = NULL;

    lv[0].items = top;
    lv[0].n = ntop;
    lv[0].sel = 0;

    for (;;) {
        Level *cur = &lv[depth];
        int act = 0;

        redraw();
        draw_bar(lv, depth, note);
        term_present();
        note = NULL;

        int k = term_getkey(-1);
        int b = K_BASE(k);

        if (b == K_F(12) || (b == K_ESC && depth == 0)) {
            return 0;
        }

        if (k == K_F(1)) {
            /* The help of the entry that is highlighted, such as
               "Menu:File:Save". */
            char place[160];
            size_t len;

            snprintf(place, sizeof place, "Menu");
            len = strlen(place);

            for (int i = 0; i <= depth; i++) {
                const char *nm = lv[i].items[lv[i].sel].name;

                len = place_append(place, sizeof place, len, nm);
            }

            help_context(place);
            continue;
        }

        if (b == K_ESC) {
            depth--;
        } else if (b == K_LEFT) {
            cur->sel = (cur->sel + cur->n - 1) % cur->n;
        } else if (b == K_RIGHT) {
            cur->sel = (cur->sel + 1) % cur->n;
        } else if (b == K_HOME) {
            cur->sel = 0;
        } else if (b == K_END) {
            cur->sel = cur->n - 1;
        } else if (b == 13 || b == K_INS || b == K_DOWN) {
            act = 1;
        } else if (k >= 32 && k < 127) {
            int i = find_letter(cur, k);

            if (i >= 0) {
                cur->sel = i;
                act = 1;
            }
        }

        if (act == 0) {
            continue;
        }

        const MenuItem *it = &cur->items[cur->sel];

        if (it->avail == 0) {
            note = "Not available in this version.";
            continue;
        }

        if (it->sub != NULL && depth + 1 < MENU_MAXDEPTH) {
            depth++;
            lv[depth].items = it->sub;
            lv[depth].n = it->nsub;
            lv[depth].sel = 0;
            continue;
        }

        for (int i = 0; i <= depth; i++) {
            path[i] = lv[i].sel;
        }

        return depth + 1;
    }
}
