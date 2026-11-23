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
 * border.h - the record CB of a column: the borders of its four sides.
 *
 * CB<mask>,<flag>: the mask has two bits for each side, 0 none, 1 single, 2
 * double, 3 wide: the top is bits 0 and 1, the bottom bits 2 and 3, the left
 * bits 4 and 5 and the right bits 6 and 7, so that all four sides double is
 * 170 and all four single is 85.  The flag after the comma is 1 when the
 * borders connect (Connect Borders? Yes) and 0 when they do not.
 */
#ifndef BORDER_H
#define BORDER_H
#include <stddef.h>

enum {
    BS_TOP,
    BS_BOTTOM,
    BS_LEFT,
    BS_RIGHT
};

enum {
    BW_NONE,
    BW_SINGLE,
    BW_DOUBLE,
    BW_WIDE
};


/* The weight (BW_NONE to BW_WIDE) of side s in the mask. */
int
border_get(int mask, int s);


/* The mask with side s set to the weight w. */
int
border_put(int mask, int s, int w);


/*
 * Read the text after CB: the mask, and in *flag the number after the comma
 * (1 when the text has none).  Returns the mask, 0 for no text.
 */
int
border_parse(const char *rec, int *flag);


/* Write mask and flag as the text after CB, such as 170,1. */
void
border_format(int mask, int flag, char *out, size_t cap);


/*
 * What the Border Layout panel changes: a weight (BW_NONE to BW_WIDE) for each
 * kind of border, or -1 for none.  All is every border, the inside ones too
 * when there are several columns; Outside is the four outer ones; Inside is
 * the lines between the columns; Top and Bottom are the top and the bottom of
 * every column; Left is the left of the first column and Right the right of
 * the last.  They are applied in this order: All, Outside, Inside, then the
 * four sides, so that a side wins over the others.
 */
typedef struct {
    int all;
    int outside;
    int inside;
    int top;
    int bottom;
    int left;
    int right;
} BorderSet;


/* Apply s to the mask of column col (0 is the first) of ncols columns. */
int
border_apply(int mask, const BorderSet *s, int col, int ncols);

#endif
