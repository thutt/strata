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
 * calc.c - exact decimal arithmetic for Math (Ctrl-F2), see calc.h.
 *
 * A value is a sign, the digits of a whole number (least significant first)
 * and a scale: the value is the number divided by ten to the scale.  Every
 * result is rounded to CALC_DIGITS significant digits.
 */
#include "calc.h"
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAXD 640 /* digits of a product before it is rounded */
#define MAXTOK 4096
#define MAXEXP 300 /* the power of ten of a number in scientific notation */
#define MAXPOW 999 /* the exponent of ^ */

typedef struct {
    int neg;
    int n; /* the number of digits; 0 for the value 0 */
    int scale; /* the places after the decimal point, 0 or more */
    unsigned char d[MAXD]; /* least significant first */
} Dec;


/* The kinds of token. */
enum {
    T_NUM,
    T_ADD,
    T_SUB,
    T_MUL,
    T_DIV,
    T_POW,
    T_LP,
    T_RP,
    T_PCT
};

typedef struct {
    int kind;
    Dec v;
} Tok;

typedef struct {
    Tok *t;
    int n;
    int at;
    int err;
} Parse;


static void
dec_zero(Dec *x)
{
    memset(x, 0, sizeof *x);
}


/* Remove the zeros at the left, and those at the right of the point. */
static void
dec_norm(Dec *x)
{
    while (x->n > 0 && x->d[x->n - 1] == 0) {
        x->n--;
    }

    while (x->scale > 0 && x->n > 0 && x->d[0] == 0) {
        memmove(x->d, x->d + 1, (size_t)(x->n - 1));
        x->n--;
        x->scale--;
    }

    if (x->n == 0) {
        x->neg = 0;
        x->scale = 0;
    }
}


/* Round to CALC_DIGITS significant digits (half up). */
static void
dec_round(Dec *x)
{
    int cut;

    dec_norm(x);

    if (x->n <= CALC_DIGITS) {
        return;
    }

    cut = x->n - CALC_DIGITS;

    {
        int up = x->d[cut - 1] >= 5;

        memmove(x->d, x->d + cut, (size_t)CALC_DIGITS);
        x->n = CALC_DIGITS;

        if (up != 0) {
            int i = 0;

            while (i < x->n && x->d[i] == 9) {
                x->d[i++] = 0;
            }

            if (i == x->n) {
                x->d[x->n++] = 1;
            } else {
                x->d[i]++;
            }
        }
    }

    if (x->scale >= cut) {
        x->scale -= cut;
    } else {
        /* The digits that were cut stood left of the point: zeros there. */
        int z = cut - x->scale;

        if (x->n + z > MAXD) {
            z = MAXD - x->n;
        }

        memmove(x->d + z, x->d, (size_t)x->n);
        memset(x->d, 0, (size_t)z);
        x->n += z;
        x->scale = 0;
    }

    dec_norm(x);
}


/* Multiply the digits by ten to the k (k >= 0) and raise the scale by k. */
static void
dec_shift(Dec *x, int k)
{
    if (k <= 0) {
        return;
    }

    if (x->n > 0) {
        if (x->n + k > MAXD) {
            k = MAXD - x->n;
        }

        memmove(x->d + k, x->d, (size_t)x->n);
        memset(x->d, 0, (size_t)k);
        x->n += k;
    }

    x->scale += k;
}


/* Compare the sizes (the digits and the scale are equal) of a and b. */
static int
mag_cmp(const Dec *a, const Dec *b)
{
    if (a->n != b->n) {
        return a->n < b->n ? -1 : 1;
    }

    for (int i = a->n - 1; i >= 0; i--) {
        if (a->d[i] != b->d[i]) {
            return a->d[i] < b->d[i] ? -1 : 1;
        }
    }

    return 0;
}


/* r = |a| + |b| for values with the same scale. */
static void
mag_add(Dec *r, const Dec *a, const Dec *b)
{
    int carry = 0;
    int n = a->n > b->n ? a->n : b->n;

    r->n = 0;

    for (int i = 0; i < n || carry != 0; i++) {
        int s = carry + (i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0);

        if (r->n >= MAXD) {
            break;
        }

        r->d[r->n++] = (unsigned char)(s % 10);
        carry = s / 10;
    }
}


/* r = |a| - |b|, with |a| >= |b|, for values with the same scale. */
static void
mag_sub(Dec *r, const Dec *a, const Dec *b)
{
    int borrow = 0;

    r->n = a->n;

    for (int i = 0; i < a->n; i++) {
        int s = a->d[i] - borrow - (i < b->n ? b->d[i] : 0);

        borrow = s < 0;
        r->d[i] = (unsigned char)(s < 0 ? s + 10 : s);
    }
}


static void
dec_add(Dec *r, const Dec *a0, const Dec *b0, int sub)
{
    Dec a = *a0;
    Dec b = *b0;
    Dec t;

    b.neg = sub != 0 ? !b.neg : b.neg;

    if (a.scale < b.scale) {
        dec_shift(&a, b.scale - a.scale);
    } else {
        dec_shift(&b, a.scale - b.scale);
    }

    dec_zero(&t);
    t.scale = a.scale;

    if (a.neg == b.neg) {
        mag_add(&t, &a, &b);
        t.neg = a.neg;
    } else if (mag_cmp(&a, &b) >= 0) {
        mag_sub(&t, &a, &b);
        t.neg = a.neg;
    } else {
        mag_sub(&t, &b, &a);
        t.neg = b.neg;
    }

    dec_round(&t);
    *r = t;
}


static void
dec_mul(Dec *r, const Dec *a, const Dec *b)
{
    static int acc[MAXD];
    Dec t;
    int n = a->n + b->n;

    dec_zero(&t);

    if (a->n == 0 || b->n == 0) {
        *r = t;

        return;
    }

    if (n > MAXD) {
        n = MAXD;
    }

    memset(acc, 0, sizeof acc);

    for (int i = 0; i < a->n; i++) {
        for (int j = 0; j < b->n && i + j < MAXD; j++) {
            acc[i + j] += a->d[i] * b->d[j];
        }
    }

    for (int i = 0; i < n; i++) {
        if (i + 1 < MAXD) {
            acc[i + 1] += acc[i] / 10;
        }

        t.d[i] = (unsigned char)(acc[i] % 10);
    }

    t.n = n;
    t.scale = a->scale + b->scale;
    t.neg = a->neg != b->neg;
    dec_round(&t);
    *r = t;
}


/* The digits of a that remain after dropping ten to the k: shift right. */
static int
mag_ge_shifted(const Dec *rem, const Dec *b, int k)
{
    /* rem >= b * 10^k ? */
    int bn = b->n + k;

    if (rem->n != bn) {
        return rem->n > bn;
    }

    for (int i = rem->n - 1; i >= 0; i--) {
        int bd = i >= k ? b->d[i - k] : 0;

        if (rem->d[i] != bd) {
            return rem->d[i] > bd;
        }
    }

    return 1;
}


static void
mag_sub_shifted(Dec *rem, const Dec *b, int k)
{
    int borrow = 0;

    for (int i = 0; i < rem->n; i++) {
        int bd = i >= k && i - k < b->n ? b->d[i - k] : 0;
        int s = rem->d[i] - borrow - bd;

        borrow = s < 0;
        rem->d[i] = (unsigned char)(s < 0 ? s + 10 : s);
    }

    while (rem->n > 0 && rem->d[rem->n - 1] == 0) {
        rem->n--;
    }
}


/* r = a / b, to CALC_DIGITS + 2 significant digits and then rounded. */
static int
dec_div(Dec *r, const Dec *a, const Dec *b)
{
    Dec rem;
    Dec q;
    int extra;

    if (b->n == 0) {
        return CALC_DIVZERO;
    }

    dec_zero(&q);

    if (a->n == 0) {
        *r = q;

        return CALC_OK;
    }

    /* Scale the dividend so that the whole quotient has enough digits. */
    rem = *a;
    extra = CALC_DIGITS + 2 + b->n - a->n;
    extra = extra < 0 ? 0 : extra;

    if (rem.n + extra > MAXD - 2) {
        extra = MAXD - 2 - rem.n;
    }

    dec_shift(&rem, extra);
    q.n = rem.n - b->n + 1;
    q.n = q.n < 1 ? 1 : q.n;

    for (int k = q.n - 1; k >= 0; k--) {
        int digit = 0;

        while (mag_ge_shifted(&rem, b, k)) {
            mag_sub_shifted(&rem, b, k);
            digit++;
        }

        q.d[k] = (unsigned char)digit;
    }

    q.scale = rem.scale - b->scale;
    q.neg = a->neg != b->neg;

    /* The scale of a quotient may be less than 0: whole numbers of zeros. */
    if (q.scale < 0) {
        int z = -q.scale;

        if (q.n + z > MAXD) {
            z = MAXD - q.n;
        }

        memmove(q.d + z, q.d, (size_t)q.n);
        memset(q.d, 0, (size_t)z);
        q.n += z;
        q.scale = 0;
    }

    dec_round(&q);
    *r = q;

    return CALC_OK;
}


/* Is x a whole number? Then its value is in *v (at most MAXPOW). */
static int
dec_int(const Dec *x, int *v)
{
    int n = 0;

    if (x->scale != 0 || x->n > 4) {
        return 0;
    }

    for (int i = x->n - 1; i >= 0; i--) {
        n = n * 10 + x->d[i];
    }

    *v = x->neg != 0 ? -n : n;

    return n <= MAXPOW;
}


static int
dec_pow(Dec *r, const Dec *a, const Dec *e)
{
    Dec acc;
    Dec base = *a;
    int n;
    int neg;

    if (dec_int(e, &n) == 0) {
        return CALC_BAD;
    }

    neg = n < 0;
    n = neg != 0 ? -n : n;
    dec_zero(&acc);
    acc.d[0] = 1;
    acc.n = 1;

    if (n == 0) {
        *r = acc;

        return CALC_OK;
    }

    while (n > 0) {
        if ((n & 1) != 0) {
            dec_mul(&acc, &acc, &base);
        }

        n >>= 1;

        if (n > 0) {
            dec_mul(&base, &base, &base);
        }
    }

    if (neg != 0) {
        Dec one;

        dec_zero(&one);
        one.d[0] = 1;
        one.n = 1;

        return dec_div(r, &one, &acc);
    }

    *r = acc;

    return CALC_OK;
}


/* ----------------------------------------------------------------- scan */
/* Read a number at s[*i]; returns 1 and the value, or 0 for CALC_BAD. */
static int
scan_number(const char *s, size_t len, size_t *i, Dec *v)
{
    size_t p = *i;
    int has_period = 0;
    int seen_point = 0;
    int digits = 0;
    int exp = 0;
    int sign = 1;

    dec_zero(v);

    /* A comma is a decimal separator only in a number that has no period. */
    for (size_t q = p; q < len; q++) {
        if (s[q] == '.' && q + 1 < len && s[q + 1] >= '0' && s[q + 1] <= '9') {
            has_period = 1;
        }

        if (!((s[q] >= '0' && s[q] <= '9') || s[q] == ',' || s[q] == '.')) {
            break;
        }
    }

    while (p < len) {
        unsigned char c = (unsigned char)s[p];

        if (c >= '0' && c <= '9') {
            /* The digits are kept most significant first for now. */
            if (digits > 0 || c != '0' || seen_point != 0) {
                if (v->n >= MAXD - 1) {
                    return 0;
                }

                v->d[v->n++] = (unsigned char)(c - '0');
                digits++;

                if (seen_point != 0) {
                    v->scale++;
                }
            }

            p++;
        } else if (c == ',' && has_period != 0 && digits > 0 && p + 1 < len &&
                   s[p + 1] >= '0' && s[p + 1] <= '9') {
            p++; /* a separator of thousands is left out */
        } else if ((c == '.' || (c == ',' && has_period == 0)) &&
                   seen_point == 0 && p + 1 < len && s[p + 1] >= '0' &&
                   s[p + 1] <= '9') {
            seen_point = c == '.' ? 1 : 2;
            p++;
        } else {
            break;
        }
    }

    /* The digits were collected most significant first: turn them round. */
    for (int a = 0, b = v->n - 1; a < b; a++, b--) {
        unsigned char t = v->d[a];

        v->d[a] = v->d[b];
        v->d[b] = t;
    }

    dec_norm(v);

    if (v->n > CALC_DIGITS) {
        return 0;
    }

    /* Scientific notation: e or E, a sign that may be there, and digits. */
    if (p < len && (s[p] == 'e' || s[p] == 'E')) {
        size_t q = p + 1;

        if (q < len && (s[q] == '+' || s[q] == '-')) {
            sign = s[q] == '-' ? -1 : 1;
            q++;
        }

        if (q < len && s[q] >= '0' && s[q] <= '9') {
            while (q < len && s[q] >= '0' && s[q] <= '9') {
                exp = exp * 10 + (s[q] - '0');

                if (exp > MAXEXP) {
                    return 0;
                }

                q++;
            }

            p = q;
        } else {
            sign = 1;
            exp = 0;
        }
    }

    if (exp != 0) {
        int e = sign * exp;

        if (e > 0) {
            if (v->scale >= e) {
                v->scale -= e;
            } else {
                int z = e - v->scale;

                if (v->n + z > MAXD) {
                    return 0;
                }

                memmove(v->d + z, v->d, (size_t)v->n);
                memset(v->d, 0, (size_t)z);
                v->n += z;
                v->scale = 0;
            }
        } else {
            v->scale += -e;
        }

        dec_norm(v);

        if (v->n > MAXD - 2) {
            return 0;
        }
    }

    *i = p;

    return 1;
}


static int
tokenize(const char *s, size_t len, Tok *t, int *nt)
{
    size_t i = 0;
    int n = 0;

    while (i < len && n < MAXTOK - 1) {
        unsigned char c = (unsigned char)s[i];
        int kind = -1;

        if ((c >= '0' && c <= '9') ||
            ((c == '.' || c == ',') && i + 1 < len && s[i + 1] >= '0' &&
             s[i + 1] <= '9')) {
            if (scan_number(s, len, &i, &t[n].v) == 0) {
                return CALC_BAD;
            }

            t[n++].kind = T_NUM;

            continue;
        }

        switch (c) {
            case '+':
                kind = T_ADD;
                break;

            case '-':
                kind = T_SUB;
                break;

            case '*':
                kind = T_MUL;
                break;

            case '/':
                kind = T_DIV;
                break;

            case '^':
                kind = T_POW;
                break;

            case '(':
                kind = T_LP;
                break;

            case ')':
                kind = T_RP;
                break;

            case '%':
                kind = T_PCT;
                break;

            default:
                break;
        }

        if (kind >= 0) {
            t[n].kind = kind;
            n++;
        }

        i++;
    }

    *nt = n;

    return CALC_OK;
}


/* ---------------------------------------------------------------- parse */
static int
peek(const Parse *p)
{
    return p->at < p->n ? p->t[p->at].kind : -1;
}


static void
expr(Parse *p, Dec *r);


static void
primary(Parse *p, Dec *r)
{
    int k = peek(p);

    dec_zero(r);

    if (k == T_NUM) {
        *r = p->t[p->at++].v;
    } else if (k == T_LP) {
        p->at++;
        expr(p, r);

        if (peek(p) == T_RP) {
            p->at++;
        }
    } else {
        p->err = CALC_BAD;
    }

    while (peek(p) == T_PCT) {
        p->at++;

        if (r->n > 0) {
            r->scale += 2; /* divided by 100 */
        }
    }
}


static void
unary(Parse *p, Dec *r);


/* The power: the exponent is a unary, so that 2^-1 and 2^3^2 work. */
static void
power(Parse *p, Dec *r)
{
    primary(p, r);

    if (peek(p) == T_POW) {
        Dec e;
        int rc;

        p->at++;
        unary(p, &e);
        rc = dec_pow(r, r, &e);

        if (rc != CALC_OK && p->err == CALC_OK) {
            p->err = rc;
        }
    }
}


static void
unary(Parse *p, Dec *r)
{
    int k = peek(p);

    if (k == T_SUB || k == T_ADD) {
        p->at++;
        unary(p, r);

        if (k == T_SUB && r->n > 0) {
            r->neg = !r->neg;
        }

        return;
    }

    power(p, r);
}


static void
term(Parse *p, Dec *r)
{
    unary(p, r);

    for (;;) {
        int k = peek(p);
        Dec x;
        int rc = CALC_OK;

        if (k != T_MUL && k != T_DIV) {
            break;
        }

        p->at++;
        unary(p, &x);

        if (k == T_MUL) {
            dec_mul(r, r, &x);
        } else {
            rc = dec_div(r, r, &x);
        }

        if (rc != CALC_OK && p->err == CALC_OK) {
            p->err = rc;
        }
    }
}


/* Sums: a number with no operator before it is added. */
static void
expr(Parse *p, Dec *r)
{
    term(p, r);

    for (;;) {
        int k = peek(p);
        Dec x;

        if (p->err != CALC_OK) {
            break;
        }

        if (k == T_ADD || k == T_SUB) {
            p->at++;
            term(p, &x);
            dec_add(r, r, &x, k == T_SUB);
        } else if (k == T_NUM || k == T_LP) {
            term(p, &x);
            dec_add(r, r, &x, 0);
        } else {
            break;
        }
    }
}


/* ---------------------------------------------------------------- print */
static void
format(const Dec *x0, char *out, size_t cap)
{
    Dec x = *x0;
    char buf[MAXD + 16];
    size_t k = 0;
    int ints;

    dec_norm(&x);

    if (x.n == 0) {
        snprintf(out, cap, "0");

        return;
    }

    ints = x.n - x.scale;

    if (x.neg != 0) {
        buf[k++] = '-';
    }

    if (ints > CALC_DIGITS) {
        /* d.ddd with a power of ten, without the zeros at the end. */
        int last = 0;

        while (last < x.n - 1 && x.d[last] == 0) {
            last++;
        }

        buf[k++] = (char)('0' + x.d[x.n - 1]);

        if (last < x.n - 1) {
            buf[k++] = '.';

            for (int i = x.n - 2; i >= last; i--) {
                buf[k++] = (char)('0' + x.d[i]);
            }
        }

        buf[k] = '\0';
        snprintf(out, cap, "%sE+%d", buf, ints - 1);

        return;
    }

    if (ints <= 0) {
        buf[k++] = '0';
    }

    for (int i = x.n - 1; i >= 0; i--) {
        if (i == x.scale - 1 && ints > 0) {
            buf[k++] = '.';
        } else if (i == x.n - 1 && ints <= 0) {
            buf[k++] = '.';

            for (int z = 0; z < -ints; z++) {
                buf[k++] = '0';
            }
        }

        buf[k++] = (char)('0' + x.d[i]);
    }

    buf[k] = '\0';
    snprintf(out, cap, "%s", buf);
}


int
calc_eval(const char *text, size_t len, char *out, size_t cap)
{
    static Tok t[MAXTOK];
    Parse p;
    Dec r;
    int n = 0;
    int rc;
    int nums = 0;

    if (cap > 0) {
        out[0] = '\0';
    }

    rc = tokenize(text, len, t, &n);

    if (rc != CALC_OK) {
        return rc;
    }

    for (int i = 0; i < n; i++) {
        nums += t[i].kind == T_NUM;
    }

    if (nums == 0) {
        return CALC_EMPTY;
    }

    p.t = t;
    p.n = n;
    p.at = 0;
    p.err = CALC_OK;
    expr(&p, &r);

    /* A bracket that was never opened, or an operator with nothing to
       work on, leaves tokens: that is an expression that cannot be done. */
    while (p.err == CALC_OK && p.at < p.n) {
        if (peek(&p) == T_RP) {
            p.at++;
        } else {
            p.err = CALC_BAD;
        }
    }

    if (p.err != CALC_OK) {
        return p.err;
    }

    format(&r, out, cap);

    return CALC_OK;
}
