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
 * calctest - a table of expressions for src/calc.c (Math, Ctrl-F2): the
 * precedence, parentheses, negation, the exponent, percent, scientific
 * notation, numbers that are only added, ignored letters, a hundred digits,
 * a division by zero.  Prints "ok" or "FAIL" for each; the status is the
 * number that failed.
 */
#include "../src/calc.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *expr;
    int rc;
    const char *want; /* the result, when rc is CALC_OK */
} Case;

static const Case cases[] = {
    {"2 + 3 * 4", CALC_OK, "14"},
    {"(2 + 3) * 4", CALC_OK, "20"},
    {"2 + 3", CALC_OK, "5"},
    {"10 - 4 - 3", CALC_OK, "3"},
    {"100 / 8", CALC_OK, "12.5"},
    {"1 / 3", CALC_OK,
     "0.3333333333333333333333333333333333333333333333333333333333"
     "333333333333333333333333333333333333333333"},
    {"2 / 3", CALC_OK,
     "0.6666666666666666666666666666666666666666666666666666666666"
     "666666666666666666666666666666666666666667"},
    {"2 ^ 10", CALC_OK, "1024"},
    {"2 ^ 3 ^ 2", CALC_OK, "512"},
    {"2 ^ -2", CALC_OK, "0.25"},
    {"-2 ^ 2", CALC_OK, "-4"},
    {"(-2) ^ 2", CALC_OK, "4"},
    {"- 5 + 2", CALC_OK, "-3"},
    {"3 * -4", CALC_OK, "-12"},
    {"--5", CALC_OK, "5"},
    {"0.1 + 0.2", CALC_OK, "0.3"},
    {"1.10 + 2.20", CALC_OK, "3.3"},
    {"1,5 + 1", CALC_OK, "2.5"},
    {".5 * 4", CALC_OK, "2"},
    {"50%", CALC_OK, "0.5"},
    {"200 * 15%", CALC_OK, "30"},
    {"1.5e3", CALC_OK, "1500"},
    {"2E-2", CALC_OK, "0.02"},
    {"1e3 + 1", CALC_OK, "1001"},
    {"4e", CALC_OK, "4"},
    {"2 3", CALC_OK, "5"},
    {"2 3 * 4", CALC_OK, "14"},
    {"2 (3)", CALC_OK, "5"},
    {"Total: 12 apples and 30 pears", CALC_OK, "42"},
    {"$1,234.50", CALC_OK, "1234.5"},
    {"3 +\t4\n* 2", CALC_OK, "11"},
    {"7 * 0", CALC_OK, "0"},
    {"0.5 * 0.5", CALC_OK, "0.25"},
    {"123456789 * 987654321", CALC_OK, "121932631112635269"},
    {"99999999999999999999 + 1", CALC_OK, "100000000000000000000"},
    {"1 - 0.9", CALC_OK, "0.1"},
    {"10 ^ 100", CALC_OK, "1E+100"},
    {"2 ^ 0", CALC_OK, "1"},
    {"0 ^ 0", CALC_OK, "1"},
    {"((((1+1))))", CALC_OK, "2"},
    {"(1 + 2", CALC_OK, "3"},
    {"1 + 2)", CALC_OK, "3"},
    {"1 / 0", CALC_DIVZERO, ""},
    {"5 / (3 - 3)", CALC_DIVZERO, ""},
    {"no numbers here", CALC_EMPTY, ""},
    {"", CALC_EMPTY, ""},
    {"2 ^ 0.5", CALC_BAD, ""},
    {"3 +", CALC_BAD, ""},
    {"* 3", CALC_BAD, ""},
    {NULL, 0, NULL}};

int
main(void)
{
    int fails = 0;
    char big[160];
    char out[800];

    for (const Case *c = cases; c->expr != NULL; c++) {
        int rc = calc_eval(c->expr, strlen(c->expr), out, sizeof out);
        int good = rc == c->rc && (rc != CALC_OK || strcmp(out, c->want) == 0);

        printf("%s calc \"%s\" -> %d \"%s\"%s\n", good ? "ok  " : "FAIL",
               c->expr, rc, out, good ? "" : " (wanted a different result)");
        fails += !good;
    }

    /* A number of 100 digits is good, one of 101 digits is not. */
    memset(big, '9', 100);
    big[100] = '\0';

    {
        int rc = calc_eval(big, 100, out, sizeof out);
        int good = rc == CALC_OK && strcmp(out, big) == 0;

        printf("%s a number of 100 digits is kept\n", good ? "ok  " : "FAIL");
        fails += !good;
    }

    memset(big, '9', 101);
    big[101] = '\0';

    {
        int rc = calc_eval(big, 101, out, sizeof out);
        int good = rc == CALC_BAD;

        printf("%s a number of 101 digits is refused\n",
               good ? "ok  " : "FAIL");
        fails += !good;
    }

    {
        char e[120];

        memset(e, '9', 100);
        strcpy(e + 100, " + 1");
        int rc = calc_eval(e, strlen(e), out, sizeof out);
        int good = rc == CALC_OK && strcmp(out, "1E+100") == 0;

        printf("%s 100 nines plus 1 is 1E+100 (101 digits)\n",
               good ? "ok  " : "FAIL");
        fails += !good;
    }

    /*
     * The rounding at the 101st digit: a cut-off digit of exactly 5 rounds
     * up, one of 4 rounds down.  Half down or no rounding up would give the
     * hundred nines for the first sum.
     */
    {
        char e[120];
        int rc;
        int good;

        memset(e, '9', 100);
        strcpy(e + 100, " + 0.5");
        rc = calc_eval(e, strlen(e), out, sizeof out);
        good = rc == CALC_OK && strcmp(out, "1E+100") == 0;

        printf("%s 100 nines plus 0.5 rounds up to 1E+100\n",
               good ? "ok  " : "FAIL");
        fails += !good;

        memset(e, '9', 100);
        strcpy(e + 100, " + 0.4");
        rc = calc_eval(e, strlen(e), out, sizeof out);
        memset(big, '9', 100);
        big[100] = '\0';
        good = rc == CALC_OK && strcmp(out, big) == 0;

        printf("%s 100 nines plus 0.4 rounds down to 100 nines\n",
               good ? "ok  " : "FAIL");
        fails += !good;
    }

    printf("calctest: %s\n", fails == 0 ? "all passed" : "FAILURES");

    return fails;
}
