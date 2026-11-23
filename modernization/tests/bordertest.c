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
 * bordertest - the encoding and decoding of the record CB (src/border.c):
 * the masks of the original's probes (All Double 170, All Single 85, Top
 * Single 1, Bottom Single 4, Left Single 16), the flag, and the Border
 * Layout settings applied to the columns of a table.
 */
#include "../src/border.h"
#include <stdio.h>
#include <string.h>

static int fails;

static void
check(const char *name, int cond)
{
    printf("%s %s\n", cond ? "ok  " : "FAIL", name);
    fails += !cond;
}


static BorderSet
none(void)
{
    BorderSet s = {-1, -1, -1, -1, -1, -1, -1};

    return s;
}


int
main(void)
{
    char buf[32];
    int flag;
    BorderSet s = none();
    int m;

    s.all = BW_DOUBLE;
    check("All Double on one column is 170",
          border_apply(0, &s, 0, 1) == 170);
    s.all = BW_SINGLE;
    check("All Single on one column is 85", border_apply(0, &s, 0, 1) == 85);
    s = none();
    s.top = BW_SINGLE;
    check("Top Single is 1", border_apply(0, &s, 0, 1) == 1);
    s = none();
    s.bottom = BW_SINGLE;
    check("Bottom Single is 4", border_apply(0, &s, 0, 1) == 4);
    s = none();
    s.left = BW_SINGLE;
    check("Left Single is 16", border_apply(0, &s, 0, 1) == 16);
    s = none();
    s.right = BW_WIDE;
    check("Right Wide is 192", border_apply(0, &s, 0, 1) == 192);
    check("the weights of 170 are all double",
          border_get(170, BS_TOP) == BW_DOUBLE &&
          border_get(170, BS_BOTTOM) == BW_DOUBLE &&
          border_get(170, BS_LEFT) == BW_DOUBLE &&
          border_get(170, BS_RIGHT) == BW_DOUBLE);
    check("the weights of 16 are a single left side only",
          border_get(16, BS_LEFT) == BW_SINGLE &&
          border_get(16, BS_TOP) == BW_NONE &&
          border_get(16, BS_RIGHT) == BW_NONE);
    check("border_put sets one side and keeps the others",
          border_put(85, BS_TOP, BW_WIDE) == 87 &&
          border_put(85, BS_RIGHT, BW_NONE) == 21);

    m = border_parse("170,1", &flag);
    check("CB170,1 reads as the mask 170 and the flag 1",
          m == 170 && flag == 1);
    m = border_parse("85,0", &flag);
    check("CB85,0 has the flag 0", m == 85 && flag == 0);
    m = border_parse("4", &flag);
    check("a CB with no comma has the flag 1", m == 4 && flag == 1);
    check("no text is no border", border_parse(NULL, &flag) == 0);
    border_format(170, 1, buf, sizeof buf);
    check("170 and 1 are written as 170,1", strcmp(buf, "170,1") == 0);
    border_format(170, 0, buf, sizeof buf);
    check("170 and 0 are written as 170,0 (the flag is kept)",
          strcmp(buf, "170,0") == 0);
    border_format(256 + 5, 1, buf, sizeof buf);
    check("a mask above 255 is written with its low eight bits",
          strcmp(buf, "5,1") == 0);
    m = border_parse("511,1", &flag);
    check("a mask above 255 is read with its low eight bits",
          m == 255 && flag == 1);

    s = none();
    s.all = BW_DOUBLE;
    check("All on three columns: every line is double, one between columns",
          border_apply(0, &s, 0, 3) == 170 &&
          border_apply(0, &s, 1, 3) == 170 - 32 &&
          border_apply(0, &s, 2, 3) == 170 - 32);
    s = none();
    s.outside = BW_SINGLE;
    check("Outside on three columns: the first has top, bottom and left",
          border_apply(0, &s, 0, 3) == 1 + 4 + 16);
    check("Outside: the middle one has top and bottom only",
          border_apply(0, &s, 1, 3) == 1 + 4);
    check("Outside: the last has top, bottom and right",
          border_apply(0, &s, 2, 3) == 1 + 4 + 64);
    s = none();
    s.inside = BW_SINGLE;
    check("Inside on three columns: the lines between the columns",
          border_apply(0, &s, 0, 3) == 64 &&
          border_apply(0, &s, 1, 3) == 64 &&
          border_apply(0, &s, 2, 3) == 0);
    check("Inside on one column changes nothing",
          border_apply(5, &s, 0, 1) == 5);
    s = none();
    s.all = BW_DOUBLE;
    s.top = BW_NONE;
    check("a side wins over All: All Double with Top None is 168",
          border_apply(0, &s, 0, 1) == 170 - 2);
    s = none();
    s.left = BW_DOUBLE;
    check("Left is the left of the first column only",
          border_apply(0, &s, 0, 2) == 32 && border_apply(0, &s, 1, 2) == 0);
    s = none();
    s.right = BW_DOUBLE;
    check("Right is the right of the last column only",
          border_apply(0, &s, 0, 2) == 0 && border_apply(0, &s, 1, 2) == 128);
    s = none();
    check("nothing set changes nothing", border_apply(77, &s, 0, 1) == 77);

    printf("bordertest: %s\n", fails == 0 ? "all passed" : "FAILURES");

    return fails;
}
