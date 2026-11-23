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

/* termbuf.h - cell buffer shared by the terminal backends (term.c,
   term_x11.c). */
#ifndef TERMBUF_H
#define TERMBUF_H
#include <stdint.h>
/* Unicode code point + TA_* attributes */
typedef struct {
    uint16_t cp;
    uint32_t attr; /* TA_ bits, a TS_ style and a TS_SWATCH_INDEX */
} TC;
/* what is shown / what the app drew */
extern TC *tb_front;
extern TC *tb_back;
extern int tb_R;
extern int tb_C;
extern int tb_cx;
extern int tb_cy;
extern int tb_cvis;


/* The pictures of the frame being drawn (see term_image in term.h).  The
   list grows with the pictures of the frame: a screen of keycaps holds far
   more than any fixed number, and a picture that does not fit the list is
   not drawn. */
typedef struct {
    int cx;
    int cy;
    int xoff;
    int yoff;
    int w;
    int h;
    const uint8_t *rgb;
} TImage;

typedef struct {
    int valid;
    int cx;
    int cy;
    int xoff;
    int yoff;
    int w;
    int h;
} TBox;

extern TBox tb_cbox;
extern TImage *tb_img;
extern int tb_nimg;


/* Makes the next presentation draw every cell (the colors changed). */
void
tb_invalidate(void);


/* reallocate, blank back, invalidate front */
void
tb_resize(int rows, int cols);
#endif
