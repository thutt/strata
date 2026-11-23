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
 * lmtest - the preparation of the text of a LaTeX marker (src/latexmark.c).
 * The X11 Editor gives the text of a LaTeX marker to LaTeX alone, and draws
 * the picture that LaTeX makes of it when it has ink: nothing in the text is
 * refused beforehand (docs/LATEXMARKER-PICTURES.txt).  What is checked here
 * is the one step that is made before: the comments are cut, the lines are
 * joined, the ends are trimmed, and a text that is too long is refused.  What
 * the Editor draws for each kind of text is checked under Xvfb by
 * tests/xlmpic.py.  Prints "ok" or "FAIL" for each; the status is the number
 * that failed.
 */
#include "../src/latexmark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int fails;


static void
check(const char *name, int cond)
{
    printf("%s %s\n", cond != 0 ? "ok  " : "FAIL", name);
    fails += cond == 0;
}


int
main(void)
{
    char prep[LMK_MAX + 1];

    check("prepare: a comment and the end of its line are cut",
          lmk_prepare("a % b\nc", prep, sizeof prep) > 0 &&
              strcmp(prep, "a c") == 0);
    check("prepare: an escaped percent sign is kept",
          lmk_prepare("50\\% off", prep, sizeof prep) > 0 &&
              strcmp(prep, "50\\% off") == 0);
    check("prepare: lines are joined with one space, the ends trimmed",
          lmk_prepare("  \\TeX{}\n\\LaTeX{}\n", prep, sizeof prep) > 0 &&
              strcmp(prep, "\\TeX{} \\LaTeX{}") == 0);
    check("prepare: a command that is only space is kept as it is typed",
          lmk_prepare("\\vspace{3ex}\\stratalogo[3]\\vspace{3ex}", prep,
                      sizeof prep) > 0 &&
              strcmp(prep, "\\vspace{3ex}\\stratalogo[3]\\vspace{3ex}") ==
                  0);
    check("prepare: a text that does not fit is refused",
          lmk_prepare("x", prep, 1) < 0);

    {
        char big[LMK_MAX + 100];

        memset(big, 'x', sizeof big - 1);
        big[sizeof big - 1] = '\0';
        check("prepare: a text longer than the limit is refused",
              lmk_prepare(big, prep, sizeof prep) < 0);
    }

    printf("lmtest: %d failures\n", fails);

    return fails > 255 ? 255 : fails;
}
