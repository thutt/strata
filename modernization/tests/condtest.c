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
 * condtest.c - the unit tests of conditional text: the rules of the
 * variables (src/vars.c), the expression of the If marker and the
 * comparison (src/cond.c), the filtered copy of a document and the map of
 * its positions.  Usage: condtest.
 */
#include "../src/cond.h"
#include "../src/marker.h"
#include "../src/util.h"
#include "../src/vars.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int failures;


static void
check(int ok, const char *what)
{
    checks++;

    if (ok == 0) {
        failures++;
        printf("FAIL %s\n", what);
    }
}


/* ---------------------------------------------------------------- vars */
static void
test_names_and_values(void)
{
    char err[256];
    char big[300];

    check(var_check_name("COLOR", err, sizeof err) == 0, "name: letters");
    check(var_check_name("a", err, sizeof err) == 0, "name: one letter");
    check(var_check_name("Region-9_x", err, sizeof err) == 0,
          "name: digits, hyphen and underscore");
    check(var_check_name("", err, sizeof err) != 0, "name: empty");
    check(var_check_name("9lives", err, sizeof err) != 0,
          "name: starts with a digit");
    check(var_check_name("-x", err, sizeof err) != 0,
          "name: starts with a hyphen");
    check(var_check_name("a b", err, sizeof err) != 0, "name: a space");
    check(var_check_name("a.b", err, sizeof err) != 0, "name: a period");
    check(var_check_name("a=b", err, sizeof err) != 0, "name: an equals sign");
    check(var_check_name(".STRATA_X", err, sizeof err) != 0 &&
          strstr(err, "period") != NULL,
          "name: a period first is refused for a variable of the user");
    memset(big, 'a', 255);
    big[255] = '\0';
    check(var_check_name(big, err, sizeof err) == 0, "name: 255 characters");
    big[255] = 'a';
    big[256] = '\0';
    check(var_check_name(big, err, sizeof err) != 0, "name: 256 characters");

    check(var_check_special_name(".STRATA_BUILD_TYPE", err, sizeof err) == 0,
          "special: the build type");
    check(var_check_special_name(".STRATA_", err, sizeof err) != 0,
          "special: no name after the prefix");
    check(var_check_special_name("STRATA_X", err, sizeof err) != 0,
          "special: no period");
    check(var_check_special_name(".OTHER_X", err, sizeof err) != 0,
          "special: another prefix");

    check(var_check_value("", err, sizeof err) == 0, "value: empty");
    check(var_check_value("red", err, sizeof err) == 0, "value: a word");
    check(var_check_value("a b ~!#$", err, sizeof err) == 0,
          "value: printable ASCII with spaces");
    check(var_check_value("say \"hi\"", err, sizeof err) != 0 &&
          strstr(err, "quotation") != NULL, "value: a double quote");
    check(var_check_value("it's", err, sizeof err) != 0,
          "value: a single quote");
    check(var_check_value("a\nb", err, sizeof err) != 0, "value: a newline");
    check(var_check_value("caf\xc3\xa9", err, sizeof err) != 0,
          "value: UTF-8 bytes");
    check(var_check_value("\x7f", err, sizeof err) != 0, "value: DEL");
    memset(big, 'v', 255);
    big[255] = '\0';
    check(var_check_value(big, err, sizeof err) == 0, "value: 255 characters");
    big[255] = 'v';
    big[256] = '\0';
    check(var_check_value(big, err, sizeof err) != 0,
          "value: 256 characters");
}


static void
test_list(void)
{
    VarList l;
    char err[256];

    memset(&l, 0, sizeof l);
    check(vars_set(&l, "COLOR", "red", 0, err, sizeof err) == 0, "set");
    check(strcmp(vars_value(&l, "COLOR"), "red") == 0, "get");
    check(strcmp(vars_value(&l, "other"), VAR_UNDEFINED) == 0,
          "a missing variable is undefined");
    check(vars_set(&l, "COLOR", "blue", 0, err, sizeof err) == 0 &&
          strcmp(vars_value(&l, "COLOR"), "blue") == 0 && l.n == 1,
          "set again replaces");
    check(vars_set(&l, "E", "", 0, err, sizeof err) == 0 &&
          strcmp(vars_value(&l, "E"), VAR_UNDEFINED) == 0 &&
          vars_is_set(&l, "E") == 0,
          "an empty value is undefined");
    check(vars_set(&l, ".x", "1", 0, err, sizeof err) != 0,
          "a name with a period is refused for the user");
    check(vars_set(&l, ".STRATA_X", "1", 0, err, sizeof err) != 0,
          "a special name is refused for the user");
    check(vars_set(&l, "x", "1", 1, err, sizeof err) != 0,
          "an ordinary name is refused as a special variable");
    vars_set_specials(&l);
    check(strcmp(vars_value(&l, ".STRATA_BUILD_TYPE"),
                 vars_build_type()) == 0, "the build type is set");
    check(strcmp(vars_build_type(), "debug") == 0 ||
          strcmp(vars_build_type(), "release") == 0 ||
          strcmp(vars_build_type(), "asan") == 0, "the build type is known");

    vars_unset(&l, "COLOR");
    check(strcmp(vars_value(&l, "COLOR"), VAR_UNDEFINED) == 0, "unset");
    vars_unset(&l, ".STRATA_BUILD_TYPE");
    check(vars_is_set(&l, ".STRATA_BUILD_TYPE") != 0,
          "a special variable is not unset");

    memset(&l, 0, sizeof l);

    for (int i = 0; i < VARS_MAX; i++) {
        char nm[16];

        snprintf(nm, sizeof nm, "v%d", i);
        check(vars_set(&l, nm, "x", 0, err, sizeof err) == 0, "fill");
    }

    check(vars_set(&l, "more", "x", 0, err, sizeof err) != 0,
          "no more than eight variables of the user");
    check(vars_set(&l, "v0", "y", 0, err, sizeof err) == 0,
          "a full list still replaces");
}


static void
test_args(void)
{
    VarList l;
    char *argv[8];
    char err[256];
    int n;

    memset(&l, 0, sizeof l);
    vars_set(&l, "A", "1", 0, err, sizeof err);
    vars_set(&l, ".STRATA_BUILD_TYPE", "debug", 1, err, sizeof err);
    n = vars_args(&l, argv, 0, 8);
    check(n == 6 && strcmp(argv[0], "--var") == 0 &&
          strcmp(argv[1], "A") == 0 && strcmp(argv[2], "1") == 0 &&
          strcmp(argv[3], "--special") == 0 &&
          strcmp(argv[4], ".STRATA_BUILD_TYPE") == 0, "vars_args");
    n = vars_args(&l, argv, 0, 4);
    check(n == 3, "vars_args stops when the array is full");
}


static void
test_option(void)
{
    char prog[] = "prog";
    char var[] = "--var";
    char special[] = "--special";
    char other[] = "--other";
    char x[] = "X";
    char hello[] = "hello";
    char file[] = "file";
    char bad[] = ".bad";
    char one[] = "1";
    char zname[] = ".STRATA_Z";
    char plain[] = "Z";
    char seven[] = "7";
    char *a1[] = {prog, var, x, hello, file};
    char *a2[] = {prog, var, x};
    char *a3[] = {prog, var, bad, one};
    char *a4[] = {prog, special, zname, seven};
    char *a5[] = {prog, special, plain, seven};
    char *a6[] = {prog, other};
    char err[256];
    int i;

    memset(vars_process(), 0, sizeof *vars_process());
    i = 1;
    check(vars_option(5, a1, &i, err, sizeof err) == 1 && i == 3 &&
          strcmp(vars_value(vars_process(), "X"), "hello") == 0,
          "option: --var");
    i = 1;
    check(vars_option(3, a2, &i, err, sizeof err) == -1 && i == 1 &&
          strstr(err, "needs a name and a value") != NULL,
          "option: a missing value");
    i = 1;
    check(vars_option(4, a3, &i, err, sizeof err) == -1 &&
          strstr(err, "period") != NULL, "option: a name with a period");
    i = 1;
    check(vars_option(4, a4, &i, err, sizeof err) == 1 &&
          strcmp(vars_value(vars_process(), ".STRATA_Z"), "7") == 0,
          "option: --special");
    i = 1;
    check(vars_option(4, a5, &i, err, sizeof err) == -1,
          "option: --special with an ordinary name");
    i = 1;
    check(vars_option(2, a6, &i, err, sizeof err) == 0,
          "option: another option is not ours");
}


/* ----------------------------------------------------------- expression */
static void
expect_bad(const char *text, const char *code)
{
    CondExpr e;
    char c[64];
    char m[256];
    char what[300];
    int r = cond_parse(text, &e, c, sizeof c, m, sizeof m);

    snprintf(what, sizeof what, "parse '%s' is refused as %s (got %s)", text,
             code, r != 0 ? c : "ok");
    check(r != 0 && strcmp(c, code) == 0, what);
}


static void
expect_ok(const char *text, const char *name, int op, const char *lit)
{
    CondExpr e;
    char c[64];
    char m[256];
    char what[300];
    int r = cond_parse(text, &e, c, sizeof c, m, sizeof m);

    snprintf(what, sizeof what, "parse '%s'", text);
    check(r == 0 && strcmp(e.name, name) == 0 && e.op == op &&
          strcmp(e.lit, lit) == 0, what);
}


static void
test_parse(void)
{
    expect_ok("COLOR = red", "COLOR", COND_EQ, "red");
    expect_ok("  COLOR=red  ", "COLOR", COND_EQ, "red");
    expect_ok("n # 5", "n", COND_NE, "5");
    expect_ok("n < 5", "n", COND_LT, "5");
    expect_ok("n > 5", "n", COND_GT, "5");
    expect_ok("n <= 5", "n", COND_LE, "5");
    expect_ok("n >= 5", "n", COND_GE, "5");
    expect_ok("a-b_c = x y", "a-b_c", COND_EQ, "x y");
    expect_ok(".STRATA_BUILD_TYPE # release", ".STRATA_BUILD_TYPE", COND_NE,
              "release");
    expect_ok("region=09", "region", COND_EQ, "09");
    expect_ok("a = =b", "a", COND_EQ, "=b");

    expect_bad("", "if-empty");
    expect_bad("   ", "if-empty");
    expect_bad("9 = 1", "if-no-variable");
    expect_bad("= 1", "if-no-variable");
    expect_bad("n <> 5", "if-bad-operator");
    expect_bad("n == 5", "if-bad-operator");
    expect_bad("n != 5", "if-bad-operator");
    expect_bad("n 5", "if-bad-operator");
    expect_bad("n", "if-bad-operator");
    expect_bad("n =", "if-no-literal");
    expect_bad("n =    ", "if-no-literal");
    expect_bad("n = \"red\"", "if-bad-literal");
    expect_bad("n = it's", "if-bad-literal");
    expect_bad("n = caf\xc3\xa9", "if-bad-literal");
}


static int
cmp(const char *l, int op, const char *r)
{
    return cond_compare(l, op, r);
}


static void
test_compare(void)
{
    /* Two integers compare as numbers. */
    check(cmp("9", COND_LT, "10") == 1, "9 < 10");
    check(cmp("10", COND_GT, "9") == 1, "10 > 9");
    check(cmp("09", COND_EQ, "9") == 1, "09 = 9");
    check(cmp("0", COND_EQ, "00000") == 1, "0 = 00000");
    check(cmp("007", COND_NE, "7") == 0, "007 # 7 is false");
    check(cmp("4294967295", COND_GT, "4294967294") == 1,
          "4294967295 > 4294967294");
    check(cmp("4294967295", COND_EQ, "0004294967295") == 1,
          "leading zeros of the largest value");
    check(cmp("5", COND_LE, "5") == 1 && cmp("5", COND_GE, "5") == 1,
          "5 <= 5 and 5 >= 5");

    /* More than 32 bits is text. */
    check(cmp("4294967296", COND_GT, "9") == 0,
          "4294967296 is text: '4294967296' > '9' is false");
    check(cmp("4294967296", COND_LT, "9") == 1,
          "4294967296 is text: '4294967296' < '9'");
    check(cmp("99999999999", COND_EQ, "99999999999") == 1,
          "a long number equals itself as text");

    /* Text compares by the ASCII order, case sensitive. */
    check(cmp("Z", COND_LT, "a") == 1, "Z < a");
    check(cmp("a", COND_GT, "Z") == 1, "a > Z");
    check(cmp("abc", COND_LT, "abd") == 1, "abc < abd");
    check(cmp("ab", COND_LT, "abc") == 1, "a prefix is less");
    check(cmp("red", COND_EQ, "red") == 1, "red = red");
    check(cmp("red", COND_EQ, "Red") == 0, "red = Red is false");
    check(cmp("red", COND_NE, "Red") == 1, "red # Red");
    check(cmp("9", COND_LT, "9a") == 1, "9 < 9a as text");
    check(cmp("9a", COND_LT, "10") == 0, "9a < 10 as text is false");
    check(cmp("-5", COND_LT, "3") == 1, "a sign makes it text: '-5' < '3'");
    check(cmp("1.5", COND_LT, "1.10") == 0, "a decimal point makes it text");

    /* One side a number, the other text: both are text. */
    check(cmp(VAR_UNDEFINED, COND_GT, "10") == 1,
          "undefined > 10 as text is true");
    check(cmp(VAR_UNDEFINED, COND_LT, "10") == 0,
          "undefined < 10 is false");
    check(cmp(VAR_UNDEFINED, COND_EQ, "10") == 0, "undefined = 10 is false");
    check(cmp(VAR_UNDEFINED, COND_NE, "10") == 1, "undefined # 10 is true");
    check(cmp(VAR_UNDEFINED, COND_EQ, VAR_UNDEFINED) == 1,
          "undefined = undefined is the test for a missing variable");
    check(cmp("10", COND_LT, "abc") == 1, "10 < abc as text");
}


static void
test_eval(void)
{
    VarList l;
    CondExpr e;
    char c[64];
    char m[256];
    char err[256];

    memset(&l, 0, sizeof l);
    vars_set(&l, "COLOR", "red", 0, err, sizeof err);
    vars_set(&l, "n", "10", 0, err, sizeof err);
    vars_set(&l, "empty", "", 0, err, sizeof err);

    cond_parse("COLOR = red", &e, c, sizeof c, m, sizeof m);
    check(cond_eval(&l, &e) == 1, "eval: COLOR = red");
    cond_parse("COLOR # red", &e, c, sizeof c, m, sizeof m);
    check(cond_eval(&l, &e) == 0, "eval: COLOR # red");
    cond_parse("n > 9", &e, c, sizeof c, m, sizeof m);
    check(cond_eval(&l, &e) == 1, "eval: n > 9 with n = 10");
    cond_parse("missing = undefined", &e, c, sizeof c, m, sizeof m);
    check(cond_eval(&l, &e) == 1, "eval: a missing variable is undefined");
    cond_parse("empty = undefined", &e, c, sizeof c, m, sizeof m);
    check(cond_eval(&l, &e) == 1, "eval: an empty variable is undefined");
    cond_parse("missing < 10", &e, c, sizeof c, m, sizeof m);
    check(cond_eval(&l, &e) == 0, "eval: missing < 10 is false");
    cond_parse("missing # 10", &e, c, sizeof c, m, sizeof m);
    check(cond_eval(&l, &e) == 1, "eval: missing # 10 is true");
    cond_parse("x = y", &e, c, sizeof c, m, sizeof m);
    check(cond_eval(NULL, &e) == 0, "eval: no variables at all");
}


/* ------------------------------------------------------------- documents */
typedef struct {
    char *p;
    size_t n;
    size_t cap;
} Bytes;


static void
put(Bytes *b, const void *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->p = realloc(b->p, b->cap);
    }

    memcpy(b->p + b->n, s, n);
    b->n += n;
}


static void
puts_(Bytes *b, const char *s)
{
    put(b, s, strlen(s));
}


/*
 * One block of one column: the text ends with an end of line.  The text is a
 * string literal that may hold NUL bytes (the records of a marker end with
 * one), so its length is the size of the literal.
 */
static void
blk_n(Bytes *b, const char *text, size_t n)
{
    puts_(b, "T");
    put(b, text, n);
    put(b, "\x1d\x00\r\n", 4);
}


#define blk(b, text) blk_n(b, text, sizeof text - 1)


/* The text of the markers. */
#define IF(expr) "\x13" "33(1" expr "\x00\r\n\x13)"
#define ELSE_ "\x13" "34(\x13)"
#define ENDIF_ "\x13" "35(\x13)"
#define XREF(n) "\x13" "2(XN" n "\x00\r\nXT2\r\n\x13)"


static Bytes
doc_start(int structured)
{
    Bytes b = {NULL, 0, 0};

    puts_(&b, "*R2\r\nMR1001\r\nPP\\1\\");
    put(&b, "\x00\r\n", 3);
    puts_(&b, "P1C33\r\nFHA1\r\nFTT5\r\nDP0,8500,11000\r\n");
    puts_(&b, structured != 0 ? "*B1\r\n" : "*B0\r\n");

    return b;
}


static Doc *
doc_finish(Bytes *b)
{
    char err[256];
    Doc *d;

    puts_(b, "*E\r\n");
    d = doc_load_mem(bytes(b->p), b->n, err, sizeof err);

    if (d == NULL) {
        printf("FAIL the test document does not load: %s\n", err);
        failures++;
    }

    free(b->p);

    return d;
}


/* The plain text of a block, with markers shown as [type]. */
static void
flat(const Doc *d, int item, char *out, size_t cap)
{
    size_t n = 0;
    const Block *b = d->items[item].blk;

    out[0] = '\0';

    for (int c = 0; c < b->ncols; c++) {
        const Text *t = &b->cols[c].text;

        for (int i = 0; i < t->n && n + 8 < cap; i++) {
            const Tok *k = &t->v[i];

            if (k->kind == TK_CHAR) {
                out[n++] = chr(k->a);
            } else if (k->kind == TK_NL) {
                out[n++] = '|';
            } else if (k->kind == TK_MARKER) {
                char tag[16];
                size_t w = usz(snprintf(tag, sizeof tag, "[%d]",
                                        tok_mtype(k)));

                memcpy(out + n, tag, w);
                n += w;
            }
        }

        out[n] = '\0';
    }
}


/* The item of the n-th block (0 is the first), or -1. */
static int
bi(const Doc *d, int n)
{
    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == IT_BLOCK && n-- == 0) {
            return i;
        }
    }

    return -1;
}


static int
count_blocks(const Doc *d)
{
    int n = 0;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == IT_BLOCK) {
            n++;
        }
    }

    return n;
}


static int
has_code(const DiagList *l, const char *code)
{
    for (int i = 0; i < l->n; i++) {
        if (strcmp(l->v[i].code, code) == 0) {
            return 1;
        }
    }

    return 0;
}


static void
test_no_conditionals(void)
{
    Bytes b = doc_start(0);
    Doc *d;
    CondMap *m = NULL;
    Doc *w;

    puts_(&b, "S\r\n");
    blk(&b, "plain text");
    d = doc_finish(&b);
    marker_set_doc(d);
    marker_prepare(d);
    w = cond_filter(d, NULL, NULL, &m);
    check(w == NULL && m == NULL, "a document without If markers is kept");
    doc_free(d);
}


static void
test_branches(void)
{
    VarList l;
    char err[256];
    char t[256];
    Bytes b;
    Doc *d;
    Doc *w;
    CondMap *m = NULL;
    DiagList dl;

    memset(&l, 0, sizeof l);
    vars_set(&l, "COLOR", "red", 0, err, sizeof err);
    b = doc_start(0);
    puts_(&b, "S\r\n");
    blk(&b, "A " IF("COLOR = red") "RED" ELSE_ "OTHER" ENDIF_ " Z");
    puts_(&b, "B\r\n");
    blk(&b, IF("COLOR = blue"));
    puts_(&b, "B\r\n");
    blk(&b, "hidden whole block");
    puts_(&b, "B\r\n");
    blk(&b, ENDIF_);
    puts_(&b, "B\r\n");
    blk(&b, "last");
    d = doc_finish(&b);
    marker_set_doc(d);
    marker_prepare(d);
    diag_list_init(&dl);
    w = cond_filter(d, &l, &dl, &m);
    check(w != NULL && m != NULL, "branches: a copy is made");
    check(w != NULL && count_blocks(w) == 2,
          "branches: the blocks of the markers and of the hidden text go");
    flat(w, bi(w, 0), t, sizeof t);
    check(strcmp(t, "A RED Z|") == 0, "branches: the true branch is kept");
    flat(w, bi(w, 1), t, sizeof t);
    check(strcmp(t, "last|") == 0, "branches: the block after is kept");
    check(dl.n == 0, "branches: no diagnostics");
    cond_map_free(m);
    doc_free(w);
    diag_list_free(&dl);
    doc_free(d);

    vars_set(&l, "COLOR", "blue", 0, err, sizeof err);
    b = doc_start(0);
    puts_(&b, "S\r\n");
    blk(&b, "A " IF("COLOR = red") "RED" ELSE_ "OTHER" ENDIF_ " Z");
    puts_(&b, "B\r\n");
    blk(&b, IF("COLOR = blue"));
    puts_(&b, "B\r\n");
    blk(&b, "shown now");
    puts_(&b, "B\r\n");
    blk(&b, ENDIF_);
    d = doc_finish(&b);
    marker_set_doc(d);
    marker_prepare(d);
    w = cond_filter(d, &l, NULL, &m);
    check(w != NULL && count_blocks(w) == 2, "else: two blocks are left");
    flat(w, bi(w, 0), t, sizeof t);
    check(strcmp(t, "A OTHER Z|") == 0, "else: the else branch is kept");
    flat(w, bi(w, 1), t, sizeof t);
    check(strcmp(t, "shown now|") == 0, "else: the chosen block is kept");
    cond_map_free(m);
    doc_free(w);
    doc_free(d);
}


static void
test_nested(void)
{
    VarList l;
    char err[256];
    char t[256];
    Bytes b;
    Doc *d;
    Doc *w;
    CondMap *m = NULL;

    memset(&l, 0, sizeof l);
    vars_set(&l, "a", "1", 0, err, sizeof err);
    vars_set(&l, "b", "2", 0, err, sizeof err);
    b = doc_start(0);
    puts_(&b, "S\r\n");
    blk(&b, "[" IF("a = 1") "A" IF("b = 3") "AB" ELSE_ "NB" ENDIF_ "a"
        ELSE_ "NA" IF("b = 2") "X" ENDIF_ ENDIF_ "]");
    d = doc_finish(&b);
    marker_set_doc(d);
    marker_prepare(d);
    w = cond_filter(d, &l, NULL, &m);
    flat(w, bi(w, 0), t, sizeof t);
    check(strcmp(t, "[ANBa]|") == 0, "nested: both levels choose");
    cond_map_free(m);
    doc_free(w);
    doc_free(d);

    vars_set(&l, "a", "9", 0, err, sizeof err);
    b = doc_start(0);
    puts_(&b, "S\r\n");
    blk(&b, "[" IF("a = 1") "A" IF("b = 3") "AB" ELSE_ "NB" ENDIF_ "a"
        ELSE_ "NA" IF("b = 2") "X" ENDIF_ ENDIF_ "]");
    d = doc_finish(&b);
    marker_set_doc(d);
    marker_prepare(d);
    w = cond_filter(d, &l, NULL, &m);
    flat(w, bi(w, 0), t, sizeof t);
    check(strcmp(t, "[NAX]|") == 0, "nested: the outer else holds an If");
    cond_map_free(m);
    doc_free(w);
    doc_free(d);
}


static void
test_errors(void)
{
    VarList l;
    DiagList dl;
    char t[256];
    Bytes b;
    Doc *d;
    Doc *w;
    CondMap *m = NULL;

    memset(&l, 0, sizeof l);

    /* An Else, an Endif and a second Else without their partners. */
    b = doc_start(0);
    puts_(&b, "S\r\n");
    blk(&b, "a" ELSE_ "b" ENDIF_ "c" IF("x = undefined") "d" ELSE_ "e" ELSE_
        "f" ENDIF_ "g");
    d = doc_finish(&b);
    marker_set_doc(d);
    marker_prepare(d);
    diag_list_init(&dl);
    w = cond_filter(d, &l, &dl, &m);
    flat(w, bi(w, 0), t, sizeof t);
    check(strcmp(t, "abcdg|") == 0,
          "errors: the markers without partners are dropped");
    check(has_code(&dl, "else-unmatched") != 0, "errors: else-unmatched");
    check(has_code(&dl, "endif-unmatched") != 0, "errors: endif-unmatched");
    check(has_code(&dl, "else-doubled") != 0, "errors: else-doubled");
    check(dl.n == 3 && dl.v[0].kind == DIAG_ERROR && dl.v[0].mtype == 34 &&
          dl.v[0].line == 1 && dl.v[0].off == 1,
          "errors: the position of the first one");
    cond_map_free(m);
    doc_free(w);
    diag_list_free(&dl);
    doc_free(d);

    /* An If that is never closed holds the rest of the document. */
    b = doc_start(0);
    puts_(&b, "S\r\n");
    blk(&b, "a" IF("x = undefined") "b");
    puts_(&b, "B\r\n");
    blk(&b, "c");
    d = doc_finish(&b);
    marker_set_doc(d);
    marker_prepare(d);
    diag_list_init(&dl);
    w = cond_filter(d, &l, &dl, &m);
    check(has_code(&dl, "if-unclosed") != 0, "errors: if-unclosed");
    check(count_blocks(w) == 2, "errors: an open If keeps a true branch");
    cond_map_free(m);
    doc_free(w);
    diag_list_free(&dl);
    doc_free(d);

    /* A bad expression is an error and its branch is left out. */
    b = doc_start(0);
    puts_(&b, "S\r\n");
    blk(&b, "a" IF("n <> 5") "HIDE" ELSE_ "SHOW" ENDIF_ "z");
    d = doc_finish(&b);
    marker_set_doc(d);
    marker_prepare(d);
    diag_list_init(&dl);
    w = cond_filter(d, &l, &dl, &m);
    flat(w, bi(w, 0), t, sizeof t);
    check(strcmp(t, "aSHOWz|") == 0,
          "errors: the branch of a bad expression is false");
    check(dl.n == 1 && strcmp(dl.v[0].code, "if-bad-operator") == 0 &&
          dl.v[0].mtype == 33 && strcmp(dl.v[0].text, "n <> 5") == 0,
          "errors: the bad operator is told with its text");
    cond_map_free(m);
    doc_free(w);
    diag_list_free(&dl);
    doc_free(d);

    /* More than ten levels. */
    {
        char text[2048];
        size_t n = 0;

        text[0] = '\0';

        for (int i = 0; i < 11; i++) {
            static const char open_if[] = IF("x = undefined");

            memcpy(text + n, open_if, sizeof open_if - 1);
            n += sizeof open_if - 1;
        }

        b = doc_start(0);
        puts_(&b, "S\r\n");
        puts_(&b, "T");
        put(&b, text, n);
        puts_(&b, "deep");

        for (int i = 0; i < 11; i++) {
            puts_(&b, ENDIF_);
        }

        put(&b, "\x1d\x00\r\n", 4);
        d = doc_finish(&b);
        marker_set_doc(d);
        marker_prepare(d);
        diag_list_init(&dl);
        w = cond_filter(d, &l, &dl, &m);
        flat(w, bi(w, 0), t, sizeof t);
        check(has_code(&dl, "if-too-deep") != 0 && dl.n == 1,
              "errors: eleven levels are too deep, told once");
        check(strcmp(t, "deep|") == 0, "errors: a deep text is still kept");
        cond_map_free(m);
        doc_free(w);
        diag_list_free(&dl);
        doc_free(d);
    }
}


static void
test_positions(void)
{
    VarList l;
    Bytes b;
    Doc *d;
    Doc *w;
    CondMap *m = NULL;
    int oitem;
    int ocol;
    int oline;
    int ooff;

    memset(&l, 0, sizeof l);

    /* AB<If x = 1>CDE<Endif>FG on one line: F is the fifth token. */
    b = doc_start(0);
    puts_(&b, "S\r\n");
    blk(&b, "AB" IF("x = 1") "CDE" ENDIF_ "FG");
    d = doc_finish(&b);
    marker_set_doc(d);
    marker_prepare(d);
    w = cond_filter(d, &l, NULL, &m);
    cond_orig_pos(m, w, bi(w, 0), 0, 1, 2, &oitem, &ocol, &oline, &ooff);
    check(oitem == 1 && oline == 1 && ooff == 7,
          "position: the token after the hidden text (item, line, off)");
    cond_orig_pos(m, w, bi(w, 0), 0, 1, 0, &oitem, &ocol, &oline, &ooff);
    check(oline == 1 && ooff == 0, "position: a token before the text");
    cond_orig_pos(m, w, bi(w, 0), 0, 1, -1, &oitem, &ocol, &oline, &ooff);
    check(oline == 1 && ooff == -1, "position: the line only");
    cond_map_free(m);
    doc_free(w);
    doc_free(d);

    /* Lines: the hidden text takes an end of line away. */
    b = doc_start(0);
    puts_(&b, "S\r\n");
    put(&b, "T" "AB\x1d" "CD" IF("x = 1") "E\x1d" "F" ENDIF_ "G\x1d" "H"
        "\x1d\x00\r\n", sizeof("T" "AB\x1d" "CD" IF("x = 1") "E\x1d" "F"
        ENDIF_ "G\x1d" "H" "\x1d\x00\r\n") - 1);
    d = doc_finish(&b);
    marker_set_doc(d);
    marker_prepare(d);
    w = cond_filter(d, &l, NULL, &m);
    check(w != NULL, "lines: a copy");
    cond_orig_pos(m, w, bi(w, 0), 0, 2, 2, &oitem, &ocol, &oline, &ooff);
    check(oline == 3 && ooff == 2,
          "position: the line and the offset of G in the original");
    cond_orig_pos(m, w, bi(w, 0), 0, 3, 0, &oitem, &ocol, &oline, &ooff);
    check(oline == 4 && ooff == 0, "position: the last line is the fourth");
    cond_map_free(m);
    doc_free(w);
    doc_free(d);

    /* Items: a hidden block moves the later ones. */
    b = doc_start(0);
    puts_(&b, "S\r\n");
    blk(&b, "first");
    puts_(&b, "B\r\n");
    blk(&b, IF("x = 1"));
    puts_(&b, "B\r\n");
    blk(&b, "hidden");
    puts_(&b, "B\r\n");
    blk(&b, ENDIF_);
    puts_(&b, "B\r\n");
    blk(&b, "last");
    d = doc_finish(&b);
    marker_set_doc(d);
    marker_prepare(d);
    w = cond_filter(d, &l, NULL, &m);
    check(w != NULL && count_blocks(w) == 2, "items: two blocks are left");
    cond_orig_pos(m, w, bi(w, 1), 0, 1, 0, &oitem, &ocol, &oline,
                  &ooff);
    check(oitem == 5, "position: the item of the last block in the original");
    cond_map_free(m);
    doc_free(w);
    doc_free(d);
}


/* ---------------------------------------------------- sections and xrefs */
static int
count_type(const Doc *d, int type)
{
    int n = 0;

    for (int i = 0; i < d->nitems; i++) {
        if (d->items[i].type == type) {
            n++;
        }
    }

    return n;
}


/* The number that the first cross-reference of the document holds. */
static int
first_xn(const Doc *d, char *out, size_t cap)
{
    for (int i = 0; i < d->nitems; i++) {
        const Block *b;

        if (d->items[i].type != IT_BLOCK) {
            continue;
        }

        b = d->items[i].blk;

        for (int c = 0; c < b->ncols; c++) {
            const Text *t = &b->cols[c].text;

            for (int j = 0; j < t->n; j++) {
                if (t->v[j].kind == TK_MARKER && tok_mtype(&t->v[j]) == 2) {
                    MPayload p;
                    const char *v;

                    mp_parse(&p, marker_info(2), tok_mdata(&t->v[j]),
                             tok_mlen(&t->v[j]));
                    v = mp_get(&p, "XN");
                    snprintf(out, cap, "%s", v != NULL ? v : "");
                    mp_free(&p);

                    return 1;
                }
            }
        }
    }

    return 0;
}


static void
test_sections(void)
{
    VarList l;
    Bytes b;
    Doc *d;
    Doc *w;
    CondMap *m = NULL;
    DiagList dl;
    char xn[64];

    memset(&l, 0, sizeof l);

    /*
     * A title, then the sections 1 (hidden), 2 and 3.  The text of 3 points
     * at section 2.  Section 1 is left out, so section 2 becomes 1 and the
     * cross-reference has to say 1.
     */
    b = doc_start(1);
    puts_(&b, "S\r\n");
    blk(&b, "Title");
    puts_(&b, "S\r\n");
    blk(&b, "Intro " IF("x = 1"));
    puts_(&b, "+\r\n");
    puts_(&b, "SE0\r\n");
    blk(&b, "Head one");
    blk(&b, "Body one");
    puts_(&b, "+\r\n");
    puts_(&b, "SE0\r\n");
    blk(&b, "Sub of one");
    blk(&b, ENDIF_);
    puts_(&b, "-\r\n");
    puts_(&b, "SE0\r\n");
    blk(&b, "Head two");
    blk(&b, "Body two");
    puts_(&b, "SE0\r\n");
    blk(&b, "Head three");
    blk(&b, "See " XREF("2"));
    puts_(&b, "-\r\n");
    d = doc_finish(&b);
    marker_set_doc(d);
    marker_prepare(d);
    diag_list_init(&dl);
    w = cond_filter(d, &l, &dl, &m);
    check(w != NULL, "sections: a copy");

    if (w != NULL) {
        check(count_type(w, IT_SECT) == 4,
              "sections: the title, the intro, two sections remain");
        check(count_type(w, IT_PLUS) == count_type(w, IT_MINUS) &&
              count_type(w, IT_PLUS) == 1,
              "sections: one plus and one minus remain");
        check(first_xn(w, xn, sizeof xn) != 0 && strcmp(xn, "1") == 0,
              "sections: the cross-reference follows its section (2 -> 1)");
        check(dl.n == 0, "sections: no diagnostic for a clean cut");
    }

    cond_map_free(m);
    doc_free(w);
    diag_list_free(&dl);
    doc_free(d);

    /* A cross-reference to a section that is left out points nowhere. */
    b = doc_start(1);
    puts_(&b, "S\r\n");
    blk(&b, "Title");
    puts_(&b, "S\r\n");
    blk(&b, "Intro " IF("x = 1"));
    puts_(&b, "+\r\n");
    puts_(&b, "SE0\r\n");
    blk(&b, "Head one");
    blk(&b, "Body one" ENDIF_);
    puts_(&b, "SE0\r\n");
    blk(&b, "Head two");
    blk(&b, "See " XREF("1"));
    puts_(&b, "-\r\n");
    d = doc_finish(&b);
    marker_set_doc(d);
    marker_prepare(d);
    w = cond_filter(d, &l, NULL, &m);

    if (w != NULL) {
        check(first_xn(w, xn, sizeof xn) != 0 && strcmp(xn, "0.0") == 0,
              "sections: a reference to a hidden section holds 0.0");
    }

    cond_map_free(m);
    doc_free(w);
    doc_free(d);

    /* The parent is left out and its child is kept: the child moves up. */
    b = doc_start(1);
    puts_(&b, "S\r\n");
    blk(&b, "Title");
    puts_(&b, "S\r\n");
    blk(&b, "Intro " IF("x = 1"));
    puts_(&b, "+\r\n");
    puts_(&b, "SE0\r\n");
    blk(&b, "Parent head");
    blk(&b, "Parent body");
    blk(&b, ENDIF_);
    puts_(&b, "+\r\n");
    puts_(&b, "SE0\r\n");
    blk(&b, "Child head");
    blk(&b, "Child body");
    puts_(&b, "-\r\n");
    puts_(&b, "-\r\n");
    d = doc_finish(&b);
    marker_set_doc(d);
    marker_prepare(d);
    diag_list_init(&dl);
    w = cond_filter(d, &l, &dl, &m);
    check(w != NULL && count_type(w, IT_PLUS) == count_type(w, IT_MINUS),
          "sections: the structure is balanced");
    check(has_code(&dl, "if-structure") != 0,
          "sections: a section that moves up is told as a warning");
    cond_map_free(m);
    doc_free(w);
    diag_list_free(&dl);
    doc_free(d);
}


int
main(void)
{
    test_names_and_values();
    test_list();
    test_args();
    test_option();
    test_parse();
    test_compare();
    test_eval();
    test_no_conditionals();
    test_branches();
    test_nested();
    test_errors();
    test_positions();
    test_sections();
    printf("condtest: %d checks, %d failures\n", checks, failures);

    return failures != 0 ? 1 : 0;
}
