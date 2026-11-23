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

/* termbuf.c - the backend-independent half of term.h: a cell buffer. */
#include "util.h"
#include "term.h"
#include "termbuf.h"
#include "layout.h" /* cp437_uni */
#include <stdlib.h>

TC *tb_front;
TC *tb_back;
int tb_R = 25;
int tb_C = 80;
int tb_cx;
int tb_cy;
int tb_cvis;

/* The style of the cells that have none, set by term_style. */
static int cur_style;


void
tb_resize(int rows, int cols)
{
    tb_R = rows;
    tb_C = cols;
    free(tb_front);
    free(tb_back);
    tb_front = calloc(usz(rows) * usz(cols), sizeof(TC));
    tb_back = calloc(usz(rows) * usz(cols), sizeof(TC));

    for (int i = 0; i < rows * cols; i++) {
        tb_back[i].cp = ' ';
        tb_front[i].cp = 0xFFFF;
    }
}


void
tb_invalidate(void)
{
    for (int i = 0; i < tb_R * tb_C; i++) {
        tb_front[i].cp = 0xFFFF;
    }
}


void
term_style(int style)
{
    cur_style = style;
}


int
term_rows(void)
{
    return tb_R;
}


int
term_cols(void)
{
    return tb_C;
}


TImage *tb_img;
int tb_nimg;
static int tb_img_cap;
TBox tb_cbox;


void
term_cursor_box(int cx, int cy, int xoff, int yoff, int w, int h)
{
    tb_cbox.valid = 1;
    tb_cbox.cx = cx;
    tb_cbox.cy = cy;
    tb_cbox.xoff = xoff;
    tb_cbox.yoff = yoff;
    tb_cbox.w = w;
    tb_cbox.h = h;
}


void
term_image(int cx, int cy, int xoff, int yoff, const uint8_t *rgb, int w,
           int h)
{
    if (tb_nimg == tb_img_cap) {
        int cap = tb_img_cap == 0 ? 64 : tb_img_cap * 2;
        TImage *grown = realloc(tb_img, usz(cap) * sizeof *grown);

        if (grown == NULL) {
            return;
        }

        tb_img = grown;
        tb_img_cap = cap;
    }

    {
        TImage *im = &tb_img[tb_nimg++];

        im->cx = cx;
        im->cy = cy;
        im->xoff = xoff;
        im->yoff = yoff;
        im->w = w;
        im->h = h;
        im->rgb = rgb;
    }
}


void
term_clear(void)
{
    tb_nimg = 0;
    tb_cbox.valid = 0;
    cur_style = 0;

    for (int i = 0; i < tb_R * tb_C; i++) {
        tb_back[i].cp = ' ';
        tb_back[i].attr = 0;
    }
}


void
term_putu(int x, int y, unsigned cp, int attr)
{
    if (x < 0 || y < 0 || x >= tb_C || y >= tb_R) {
        return;
    }

    if ((attr & TS_MASK) == 0) {
        attr |= cur_style;
    }

    tb_back[y * tb_C + x].cp = u16(cp);
    tb_back[y * tb_C + x].attr = uns(attr);
}


void
term_put(int x, int y, uint8_t ch, int attr)
{
    unsigned cp = ' ';

    /* The byte FF is a no-break space in a text; it is drawn as NBSP_SHOWN. */
    if (ch == NBSP_BYTE) {
        cp = cp437_uni[NBSP_SHOWN];
    } else if (ch != '\0') {
        cp = cp437_uni[ch];
    }

    term_putu(x, y, cp, attr);
}


void
term_puts(int x, int y, const char *s, int attr)
{
    /* A UTF-8 character, as the name of a file has, is one cell. */
    while (*s != '\0') {
        size_t used;
        int cell = utf8_cell(s, &used);

        term_put(x++, y, u8(cell), attr);
        s += used;
    }
}


void
term_fill(int x, int y, int w, int h, uint8_t ch, int attr)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            term_put(x + i, y + j, ch, attr);
        }
    }
}


void
term_box(int x, int y, int w, int h, int attr, int dbl)
{
    uint8_t tl = dbl != 0 ? 0xC9 : 0xDA;
    uint8_t tr = dbl != 0 ? 0xBB : 0xBF;
    uint8_t bl = dbl != 0 ? 0xC8 : 0xC0;
    uint8_t br = dbl != 0 ? 0xBC : 0xD9;
    uint8_t hz = dbl != 0 ? 0xCD : 0xC4;
    uint8_t vt = dbl != 0 ? 0xBA : 0xB3;

    term_put(x, y, tl, attr);
    term_put(x + w - 1, y, tr, attr);
    term_put(x, y + h - 1, bl, attr);
    term_put(x + w - 1, y + h - 1, br, attr);

    for (int i = 1; i < w - 1; i++) {
        term_put(x + i, y, hz, attr);
        term_put(x + i, y + h - 1, hz, attr);
    }

    for (int j = 1; j < h - 1; j++) {
        term_put(x, y + j, vt, attr);
        term_put(x + w - 1, y + j, vt, attr);
    }
}


void
term_cursor(int x, int y, int vis)
{
    tb_cx = x;
    tb_cy = y;
    tb_cvis = vis;
}


/* ------------------------------------------------- queue and recording */
#define KEYQ_MAX 4096

static int keyq[KEYQ_MAX];
static int keyq_head;
static int keyq_n;
static int keyrec[KEYQ_MAX];
static int keyrec_n;
static int keyrec_on;
static void (*keyrec_done)(void);


void
term_inject(const int *keys, int n)
{
    for (int i = 0; i < n && keyq_n < KEYQ_MAX; i++) {
        keyq[(keyq_head + keyq_n) % KEYQ_MAX] = keys[i];
        keyq_n++;
    }
}


void
term_record(int on, void (*done)(void))
{
    keyrec_on = on;
    keyrec_done = done;
    keyrec_n = 0;
}


const int *
term_recorded(int *n)
{
    *n = keyrec_n;

    return keyrec;
}


static int (*idle_poll)(void);


void
term_set_idle(int (*poll)(void))
{
    idle_poll = poll;
}


int
term_input_pending(void)
{
    if (keyq_n > 0) {
        return 1;
    }

    return term_raw_pending();
}


int
term_key_pending(void)
{
    if (keyq_n > 0) {
        return 1;
    }

    return term_raw_key_pending();
}


int
term_getkey(int timeout_ms)
{
    int k;

    if (keyq_n > 0) {
        k = keyq[keyq_head];
        keyq_head = (keyq_head + 1) % KEYQ_MAX;
        keyq_n--;

        return k;
    }

    if (idle_poll != NULL) {
        /* A timeout of zero is a look at the keyboard: it must not wait. */
        int step = timeout_ms == 0 ? 0 : 200;

        for (;;) {
            k = term_getkey_raw(step);

            if (k != K_NONE) {
                break;
            }

            if (idle_poll() != 0) {
                return K_RESIZE;
            }

            if (timeout_ms >= 0) {
                return K_NONE;
            }
        }
    } else {
        k = term_getkey_raw(timeout_ms);
    }

    if (keyrec_on != 0 && k != K_NONE && k != K_RESIZE) {
        if (k == (K_F(1) | KM_CTRL)) {
            keyrec_on = 0;

            if (keyrec_done != NULL) {
                keyrec_done();
            }

            return K_RESIZE;
        }

        if (keyrec_n < KEYQ_MAX) {
            keyrec[keyrec_n++] = k;
        }
    }

    return k;
}
