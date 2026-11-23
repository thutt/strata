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
 * calc.h - the calculator of Math (Ctrl-F2).
 *
 * A numeric expression in text is calculated with exact decimal arithmetic of
 * up to 100 digits (no floating point).  The expression has numbers,
 * operators, parentheses and special characters; every other character is
 * ignored.  Where no operator stands between two numbers they are added.
 * The operators, in the order of their precedence, are parentheses, the
 * exponent ^ (the exponent is a whole number), negation -, multiplication and
 * division * /, and addition and subtraction + -.  A number may have a
 * decimal separator, the period, or the comma when it stands between digits
 * of a number that has no period; % divides the number before it by 100; e or
 * E followed by a whole number, which may have a sign, is the power of ten of
 * a number written in scientific notation.
 */
#ifndef CALC_H
#define CALC_H
#include <stddef.h>

/* The digits that a number and a result may have. */
#define CALC_DIGITS 100

enum {
    CALC_OK = 0,
    CALC_EMPTY = -1, /* there is no number in the text */
    CALC_DIVZERO = -2, /* a division by zero */
    CALC_BAD = -3 /* the expression cannot be calculated */
};


/*
 * Calculate the len bytes of text.  The result goes into out (cap bytes) as
 * digits with a minus sign and a period when it is needed, with as many
 * decimals as the result has (no zero at the end of them), and, for a result
 * of more than 100 digits before the period, as d.ddd with a power of ten
 * (1E+150).  Returns CALC_OK or one of the errors above; out is then empty.
 */
int
calc_eval(const char *text, size_t len, char *out, size_t cap);

#endif
