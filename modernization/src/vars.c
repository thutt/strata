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

/* vars.c - the variables of the If markers; see vars.h for the rules. */
#include "vars.h"
#include "build_type.h"
#include "util.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SPECIAL_PREFIX ".STRATA_"

static VarList process_vars;


static int
is_name_char(char c)
{
    return is_alpha(c) || is_digit(c) || c == '-' || c == '_';
}


/* Check the characters after the first of a name; returns the bad one or 0. */
static char
bad_name_char(const char *s)
{
    for (; *s != '\0'; s++) {
        if (is_name_char(*s) == 0) {
            return *s;
        }
    }

    return '\0';
}


static void say(char *err, size_t errlen, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static void
say(char *err, size_t errlen, const char *fmt, ...)
{
    va_list ap;

    if (err == NULL || errlen == 0) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}


int
var_check_name(const char *name, char *err, size_t errlen)
{
    char bad;

    if (name[0] == '\0') {
        say(err, errlen, "The variable name is empty: a name needs at least "
            "one letter.");

        return -1;
    }

    if (name[0] == '.') {
        say(err, errlen, "The variable name %s begins with a period: the "
            "names that begin with a period belong to Strata and cannot be "
            "given.", name);

        return -1;
    }

    if (!is_alpha(name[0])) {
        say(err, errlen, "The variable name %s must start with a letter.",
            name);

        return -1;
    }

    bad = bad_name_char(name + 1);

    if (bad != '\0') {
        say(err, errlen, "The variable name %s may hold only letters, digits, "
            "hyphens and underscores.", name);

        return -1;
    }

    if (strlen(name) > VAR_LEN_MAX) {
        say(err, errlen, "The variable name %s is longer than 255 "
            "characters.", name);

        return -1;
    }

    return 0;
}


int
var_check_special_name(const char *name, char *err, size_t errlen)
{
    size_t pre = strlen(SPECIAL_PREFIX);

    if (strncmp(name, SPECIAL_PREFIX, pre) != 0 || name[pre] == '\0' ||
        bad_name_char(name + pre) != '\0' || strlen(name) > VAR_LEN_MAX) {
        say(err, errlen, "The special variable name %s is not .STRATA_ "
            "followed by a name of letters, digits, hyphens and "
            "underscores.", name);

        return -1;
    }

    return 0;
}


int
var_check_value(const char *value, char *err, size_t errlen)
{
    size_t n = strlen(value);

    if (n > VAR_LEN_MAX) {
        say(err, errlen, "A value may have at most 255 characters.");

        return -1;
    }

    for (size_t i = 0; i < n; i++) {
        char c = value[i];

        if (c == '"' || c == '\'') {
            say(err, errlen, "A value may not hold a quotation mark: %s.",
                value);

            return -1;
        }

        if (c < ' ' || c > '~') {
            say(err, errlen, "A value may hold only printable 7-bit ASCII "
                "characters.");

            return -1;
        }
    }

    return 0;
}


static int
find_index(const VarList *l, const char *name)
{
    for (int i = 0; i < l->n; i++) {
        if (strcmp(l->v[i].name, name) == 0) {
            return i;
        }
    }

    return -1;
}


int
vars_set(VarList *l, const char *name, const char *value, int special,
         char *err, size_t errlen)
{
    int at;
    int users = 0;
    int specials = 0;

    if (special != 0) {
        if (var_check_special_name(name, err, errlen) != 0) {
            return -1;
        }
    } else if (var_check_name(name, err, errlen) != 0) {
        return -1;
    }

    if (var_check_value(value, err, errlen) != 0) {
        return -1;
    }

    at = find_index(l, name);

    if (at < 0) {
        for (int i = 0; i < l->n; i++) {
            if (l->v[i].special != 0) {
                specials++;
            } else {
                users++;
            }
        }

        if ((special != 0 && specials >= VARS_SPECIAL_MAX) ||
            (special == 0 && users >= VARS_MAX)) {
            say(err, errlen, "No more variables fit: there are at most 8 "
                "variables of the user and 8 special ones.");

            return -1;
        }

        at = l->n++;
    }

    str_copy(l->v[at].name, sizeof l->v[at].name, name);
    str_copy(l->v[at].value, sizeof l->v[at].value, value);
    l->v[at].special = special != 0;

    return 0;
}


void
vars_unset(VarList *l, const char *name)
{
    int at = find_index(l, name);

    if (at < 0 || l->v[at].special != 0) {
        return;
    }

    for (int i = at; i + 1 < l->n; i++) {
        l->v[i] = l->v[i + 1];
    }

    l->n--;
}


const char *
vars_value(const VarList *l, const char *name)
{
    int at;

    if (l == NULL) {
        return VAR_UNDEFINED;
    }

    at = find_index(l, name);

    if (at < 0 || l->v[at].value[0] == '\0') {
        return VAR_UNDEFINED;
    }

    return l->v[at].value;
}


int
vars_is_set(const VarList *l, const char *name)
{
    int at;

    if (l == NULL) {
        return 0;
    }

    at = find_index(l, name);

    return at >= 0 && l->v[at].value[0] != '\0';
}


const char *
vars_build_type(void)
{
    return strata_build_name;
}


void
vars_set_specials(VarList *l)
{
    char err[160];

    vars_set(l, ".STRATA_BUILD_TYPE", vars_build_type(), 1, err, sizeof err);
}


int
vars_args(VarList *l, char **argv, int n, int cap)
{
    static char a_var[] = "--var";
    static char a_special[] = "--special";

    for (int i = 0; i < l->n && n + 3 < cap; i++) {
        argv[n++] = l->v[i].special != 0 ? a_special : a_var;
        argv[n++] = l->v[i].name;
        argv[n++] = l->v[i].value;
    }

    return n;
}


int
vars_option(int argc, char **argv, int *i, char *err, size_t errlen)
{
    int special;

    if (strcmp(argv[*i], "--var") == 0) {
        special = 0;
    } else if (strcmp(argv[*i], "--special") == 0) {
        special = 1;
    } else {
        return 0;
    }

    if (*i + 2 >= argc) {
        say(err, errlen, "The option %s needs a name and a value.", argv[*i]);

        return -1;
    }

    if (vars_set(&process_vars, argv[*i + 1], argv[*i + 2], special, err,
                 errlen) != 0) {
        return -1;
    }

    *i += 2;

    return 1;
}


VarList *
vars_process(void)
{
    return &process_vars;
}
