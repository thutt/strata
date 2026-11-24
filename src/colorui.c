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
 * colorui.c - the Colors panel of Setup in the Document Manager: a list of
 * the kinds of text with their foreground and background colors and a
 * sample of each, a palette to choose a color from, and the way back to the
 * defaults.  A change is in use at once; Ins keeps it in the settings file
 * and Esc takes it back.
 */
#include "util.h"
#include "colorui.h"
#include "color.h"
#include "config.h"
#include "log.h"
#include "term.h"
#include "widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The colors of the panel while it is open. */
typedef struct {
    Config cfg; /* The colors of both versions, as the text of the file. */
    int ver; /* The version that is edited: 0 terminal, 1 X11. */
    int row; /* The kind that is selected. */
    int bg; /* 0 for its foreground color, 1 for its background. */
    int top; /* The first kind that is shown. */
} Panel;

static void
paint_content(const Panel *p, int x, int y, int w, int vis);


/* The samples are the first entries of the table of the color module. */
#define SAMPLE_BASE 0
#define PALETTE_BASE 32
#define PALETTE_COLS 32


/* The colors of a version, with the defaults for what the text lacks. */
static void
set_of(const Panel *p, int ver, ColorSet *cs)
{
    config_colors(&p->cfg, ver, cs);
}


/* The color of one entry. */
static unsigned
entry_rgb(const Panel *p, int ver, int kind, int bg)
{
    ColorSet cs;

    set_of(p, ver, &cs);

    return bg != 0 ? cs.pair[kind].bg : cs.pair[kind].fg;
}


/* The colors in use change at once when the version that runs is edited. */
static void
apply(const Panel *p)
{
    if (p->ver == term_is_gui()) {
        ColorSet cs;

        set_of(p, p->ver, &cs);
        color_activate(&cs);
    }
}


/* The colors of the samples, in the first entries of a table. */
static void
samples(const Panel *p, ColorPair *table)
{
    ColorSet cs;

    set_of(p, p->ver, &cs);
    memset(table, 0, TS_SWATCH_MAX * sizeof table[0]);

    for (int k = 0; k < CK_COUNT; k++) {
        table[SAMPLE_BASE + k] = cs.pair[k];
    }
}


/* The colors of a palette: the cells that the user chooses from. */
static int
palette_make(int ver, int deep, ColorPair *out)
{
    int n = 0;

    /* The 16 colors of the VGA palette come first in every palette. */
    for (int i = 0; i < 16; i++) {
        out[n].bg = color_vga(i);
        out[n].fg = color_readable(out[n].bg);
        n++;
    }

    if (ver == 0 && deep != 0) {
        /* The 240 other colors of the extended palette of a terminal. */
        for (int i = 16; i < 256; i++) {
            out[n].bg = color_rgb256(i);
            out[n].fg = color_readable(out[n].bg);
            n++;
        }

        return n;
    }

    if (ver == 0) {
        return n;
    }

    /* X11: 16 greys, then 16 hues at three strengths. */
    for (int i = 0; i < 16; i++) {
        unsigned g = uns(i * 17);

        out[n].bg = (g << 16) | (g << 8) | g;
        out[n].fg = color_readable(out[n].bg);
        n++;
    }

    static const int vs[3][2] = {{140, 255}, {255, 255}, {255, 110}};

    for (int k = 0; k < 3; k++) {
        for (int h = 0; h < 16; h++) {
            int v = vs[k][0];
            int sat = vs[k][1];
            int seg = h * 6 / 16;
            int f = (h * 6 % 16) * 16;
            int pp = v * (255 - sat) / 255;
            int qq = v * (255 - sat * f / 255) / 255;
            int tt = v * (255 - sat * (255 - f) / 255) / 255;
            int c[3];

            switch (seg) {
                case 0:
                    c[0] = v;
                    c[1] = tt;
                    c[2] = pp;
                    break;

                case 1:
                    c[0] = qq;
                    c[1] = v;
                    c[2] = pp;
                    break;

                case 2:
                    c[0] = pp;
                    c[1] = v;
                    c[2] = tt;
                    break;

                case 3:
                    c[0] = pp;
                    c[1] = qq;
                    c[2] = v;
                    break;

                case 4:
                    c[0] = tt;
                    c[1] = pp;
                    c[2] = v;
                    break;

                default:
                    c[0] = v;
                    c[1] = pp;
                    c[2] = qq;
                    break;
            }

            out[n].bg = (uns(c[0]) << 16) | (uns(c[1]) << 8) | uns(c[2]);
            out[n].fg = color_readable(out[n].bg);
            n++;
        }
    }

    return n;
}


/* Does this terminal take any color the user types? */
static int
takes_any(int ver)
{
    return ver != 0 || term_is_gui() != 0 || color_depth() >= DEPTH_TRUE;
}


/* Does the palette of the version have the 240 colors of the extended one? */
static int
has_deep(int ver)
{
    return ver != 0 || term_is_gui() != 0 || color_depth() >= DEPTH_256;
}


/* The line that says what the running program shows of the version. */
static const char *
version_note(const Panel *p)
{
    if (p->ver == term_is_gui()) {
        return p->ver != 0 ? "This is the X11 version: any color" :
                             color_depth() >= DEPTH_TRUE ? "This terminal: any "
                                                           "color" :
                             color_depth() >= DEPTH_256 ? "This terminal: 256 "
                                                          "colors" :
                             color_depth() >= DEPTH_16  ? "This terminal: 16 "
                                                          "colors" :
                                                          "This terminal has "
                                                          "no colors";
    }

    return "Not the running version: the colors are for its next start";
}


/* The palette: choose a color; returns 1 and sets *rgb, or 0 for Esc. */
static int
choose(const Panel *p, unsigned *rgb)
{
    ColorPair cells[TS_SWATCH_MAX];
    int deep = has_deep(p->ver);
    int n = palette_make(p->ver, deep, cells);
    int sel = 0;

    for (int i = 0; i < n; i++) {
        if (cells[i].bg == *rgb) {
            sel = i;
        }
    }

    for (;;) {
        ColorPair table[TS_SWATCH_MAX];
        int rows = (n + PALETTE_COLS - 1) / PALETTE_COLS;

        /* The panel is drawn first, and it sets the samples. */
        w_base();
        samples(p, table);

        for (int i = 0; i < n; i++) {
            table[PALETTE_BASE + i] = cells[i];
        }

        color_set_swatches(table, PALETTE_BASE + n);

        int C = term_cols();
        int R = term_rows();
        int w = PALETTE_COLS * 2 + 4;
        int h = rows + 6;
        int x = (C - w) / 2;
        int y = (R - h) / 2;

        box(x, y, w, h, "Choose a color");

        for (int i = 0; i < n; i++) {
            int cx = x + 2 + (i % PALETTE_COLS) * 2;
            int cy = y + 1 + i / PALETTE_COLS;
            int attr = TS_SWATCH | TS_SWATCH_INDEX(PALETTE_BASE + i);

            term_put(cx, cy, i == sel ? '[' : ' ', attr);
            term_put(cx + 1, cy, i == sel ? ']' : ' ', attr);
        }

        char text[COLOR_TEXT_MAX];
        char line[80];

        color_text(cells[sel].bg, text, sizeof text);
        snprintf(line, sizeof line, "%s  #%06x", text, cells[sel].bg);
        term_puts(x + 2, y + rows + 2, line, TA_BOLD);
        term_puts(x + 2, y + rows + 3,
                  takes_any(p->ver) != 0 ? "Arrows move   Enter chooses   "
                                           "T types a color" :
                                           "Arrows move   Enter chooses",
                  TA_DIM);
        term_cursor(0, 0, 0);
        term_present();

        int k = term_getkey(-1);
        int b = K_BASE(k);

        if (k == K_F(1)) {
            help_context("Setup:Screen Colors");
        } else if (b == K_ESC) {
            return 0;
        } else if (b == 13) {
            *rgb = cells[sel].bg;

            return 1;
        } else if (b == K_LEFT && sel > 0) {
            sel--;
        } else if (b == K_RIGHT && sel < n - 1) {
            sel++;
        } else if (b == K_UP && sel >= PALETTE_COLS) {
            sel -= PALETTE_COLS;
        } else if (b == K_DOWN && sel + PALETTE_COLS < n) {
            sel += PALETTE_COLS;
        } else if (b == K_HOME) {
            sel = 0;
        } else if (b == K_END) {
            sel = n - 1;
        } else if (b == 't' || b == 'T' || b == '#') {
            char buf[COLOR_TEXT_MAX] = "";

            if (takes_any(p->ver) == 0) {
                popup_msg("Choose a color",
                          "This terminal shows 16 or 256 colors: choose "
                          "one from the palette");
            } else if (popup_input("Choose a color",
                                   "A name of a color, or #rrggbb:", buf,
                                   sizeof buf) != 0) {
                unsigned v;

                if (color_parse(buf, &v) == 0) {
                    *rgb = v;

                    return 1;
                }

                popup_msg("Choose a color",
                          "That is not a color: write a name such as "
                          "light-cyan, or #rrggbb");
            }
        }
    }
}


/* Draws the panel and sets the samples. */
/* The rows of the kinds that the page shows (the colors fit the screen). */
static int
kinds_shown(void)
{
    int vis = term_rows() - 9;

    if (vis > CK_COUNT) {
        vis = CK_COUNT;
    }

    return vis;
}


/* What the panel and the page hold inside the frame: x, y and w are those of
   the frame and vis the number of the kinds that are shown. */
static void
paint_content(const Panel *p, int x, int y, int w, int vis)
{
    ColorSet cs;
    ColorPair table[TS_SWATCH_MAX];

    set_of(p, p->ver, &cs);
    samples(p, table);
    color_set_swatches(table, SAMPLE_BASE + CK_COUNT);
    term_puts(x + 2, y + 1, p->ver != 0 ? "Colors of the X11 version" :
                                          "Colors of the terminal version",
              TA_BOLD);
    term_puts(x + 2, y + 2, "Kind of text           Foreground  Background"
                            "  Sample",
              TA_DIM);

    for (int i = 0; i < vis; i++) {
        int k = p->top + i;
        char fg[COLOR_TEXT_MAX];
        char bg[COLOR_TEXT_MAX];
        int ry = y + 3 + i;

        color_text(cs.pair[k].fg, fg, sizeof fg);
        color_text(cs.pair[k].bg, bg, sizeof bg);
        term_puts(x + 2, ry, color_kind_label(k), k == p->row ? TA_BOLD : 0);
        term_puts(x + 25, ry, fg, k == p->row && p->bg == 0 ? TA_REVERSE : 0);
        term_puts(x + 37, ry, bg, k == p->row && p->bg != 0 ? TA_REVERSE : 0);

        /* The sample has the look of the kind and the colors being set. */
        int attr = (color_kind_attr(k) & 0xFF) | TS_SWATCH |
                   TS_SWATCH_INDEX(SAMPLE_BASE + k);

        for (int c = 0; c < 12; c++) {
            static const char *text = " Sample text";

            term_put(x + 49 + c, ry, u8(text[c]), attr);
        }
    }

    if (p->top > 0) {
        term_put(x + w - 2, y + 3, 0x18, 0);
    }

    if (p->top + vis < CK_COUNT) {
        term_put(x + w - 2, y + 2 + vis, 0x19, 0);
    }

    double ratio = color_contrast(cs.pair[p->row].fg, cs.pair[p->row].bg);
    char line[400];

    if (ratio < COLOR_CONTRAST_LOW && p->row != CK_CURSOR) {
        snprintf(line, sizeof line,
                 "Warning: the text is hard to read on this background "
                 "(%.1f to 1)",
                 ratio);
    } else {
        str_copy(line, sizeof line, version_note(p));
    }

    term_puts(x + 2, y + vis + 3, line,
              ratio < COLOR_CONTRAST_LOW && p->row != CK_CURSOR ? TA_BOLD :
                                                                  TA_DIM);
    term_puts(x + 2, y + vis + 4,
              "Space Left Right: color  F2: list  Tab: Fg/Bg  V: version",
              TA_DIM);
    term_puts(x + 2, y + vis + 5,
              "Del: default  R: all  PgUp/PgDn: page  Ins: keep  Esc: cancel",
              TA_DIM);
    term_cursor(0, 0, 0);
}


/* The colors as the page of Setup holds them while Setup is open. */
static Panel pg;
static ColorSet pg_before;
static int pg_open;


void
colors_page_begin(void)
{
    memset(&pg, 0, sizeof pg);
    config_load(&pg.cfg);
    pg.ver = term_is_gui();
    pg_before = *color_active();
    pg_open = 1;
}


int
colors_page_height(void)
{
    return kinds_shown() + 7;
}


void
colors_page_paint(int *x, int *y, int *w, int *h)
{
    int C = term_cols();
    int R = term_rows();
    int vis = kinds_shown();

    *w = 66 < C - 2 ? 66 : C - 2;
    *h = vis + 7;
    *x = (C - *w) / 2;
    *y = (R - *h) / 2;

    if (pg.row < pg.top) {
        pg.top = pg.row;
    }

    if (pg.row >= pg.top + vis) {
        pg.top = pg.row - vis + 1;
    }
}


void
colors_page_content(int x, int y, int w)
{
    paint_content(&pg, x, y, w, kinds_shown());
}


/* Moves the color of the selected entry in the palette: dir is 1 for the
   next and -1 for the one before; wrap goes round at the ends. */
static void
step_color(int dir, int wrap)
{
    ColorPair cells[TS_SWATCH_MAX];
    int n = palette_make(pg.ver, has_deep(pg.ver), cells);
    unsigned rgb = entry_rgb(&pg, pg.ver, pg.row, pg.bg);
    int at = -1;

    for (int i = 0; i < n; i++) {
        if (cells[i].bg == rgb) {
            at = i;
        }
    }

    if (at < 0) {
        at = dir > 0 ? -1 : n;
    }

    at += dir;

    if (at < 0 || at >= n) {
        if (wrap == 0) {
            return;
        }

        at = at < 0 ? n - 1 : 0;
    }

    color_text(cells[at].bg, pg.cfg.colors[pg.ver][pg.row][pg.bg],
               COLOR_TEXT_MAX);
    apply(&pg);
}


int
colors_page_key(int k)
{
    int b = K_BASE(k);

    if (b == K_UP && pg.row > 0) {
        pg.row--;
    } else if ((b == K_DOWN || b == 13) && pg.row < CK_COUNT - 1) {
        pg.row++;
    } else if (b == K_HOME) {
        pg.row = 0;
    } else if (b == K_END) {
        pg.row = CK_COUNT - 1;
    } else if (b == 9 || b == K_BTAB) {
        pg.bg = pg.bg == 0;
    } else if (b == K_LEFT) {
        step_color(-1, 0);
    } else if (b == K_RIGHT) {
        step_color(1, 0);
    } else if (k == ' ') {
        step_color(1, 1);
    } else if (k == '+' || k == ('+' | KM_ALT) || b == K_F(2)) {
        unsigned rgb = entry_rgb(&pg, pg.ver, pg.row, pg.bg);

        if (choose(&pg, &rgb) != 0) {
            color_text(rgb, pg.cfg.colors[pg.ver][pg.row][pg.bg],
                       COLOR_TEXT_MAX);
            apply(&pg);
        }
    } else if (b == 'v' || b == 'V') {
        pg.ver = pg.ver == 0;
    } else if (b == K_DEL || b == K_BKSP || b == 'd' || b == 'D') {
        pg.cfg.colors[pg.ver][pg.row][pg.bg][0] = '\0';
        apply(&pg);
    } else if (b == 'r' || b == 'R') {
        if (popup_yn("Use the default colors for every kind of text?") != 0) {
            memset(pg.cfg.colors[pg.ver], 0, sizeof pg.cfg.colors[pg.ver]);
            apply(&pg);
        }
    } else {
        return 0;
    }

    return 1;
}


void
colors_page_commit(Config *c)
{
    if (pg_open != 0) {
        memcpy(c->colors, pg.cfg.colors, sizeof c->colors);
    }
}


void
colors_page_end(int keep)
{
    if (pg_open != 0 && keep == 0) {
        color_activate(&pg_before);
    }

    pg_open = 0;
}
