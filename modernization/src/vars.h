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
 * vars.h - the variables that str passes to the programs and that the If
 * markers of a document test.  The rules are the owner's (see the item
 * "Conditional text that works" of docs/WORKLIST.txt):
 *
 *   - A name starts with a letter and holds letters, digits, the hyphen
 *     and the underscore.  A name that begins with a period belongs to
 *     the special variables, which only str makes: .STRATA_<name>.  The
 *     first is .STRATA_BUILD_TYPE (debug, release or asan).
 *   - A value is printable 7-bit ASCII (space to tilde) without the
 *     quotation marks, at most 255 characters.  An empty value is the same
 *     as a variable that is not set: both give the string "undefined".
 *   - A value is a string.  It is a number only when a comparison uses it
 *     as one (see cond.h).
 *
 * The variables travel from str to the Editor and from the Editor to the
 * Printer and the Viewer as two options of the command line, each with a
 * name and a value as two arguments: --var (a variable of the user) and
 * --special (a special variable; only str gives it).
 */
#ifndef VARS_H
#define VARS_H
#include <stddef.h>

/* The longest name, value and literal. */
#define VAR_LEN_MAX 255

/* The most variables of the user, and the most special variables. */
#define VARS_MAX 8
#define VARS_SPECIAL_MAX 8

/* The room that the arguments of all variables need in an argv. */
#define VARS_ARGC (3 * (VARS_MAX + VARS_SPECIAL_MAX))

/* The text that a variable that is not set gives. */
#define VAR_UNDEFINED "undefined"

typedef struct {
    char name[VAR_LEN_MAX + 1];
    char value[VAR_LEN_MAX + 1];
    int special; /* 1 for a special variable (the name begins with a period) */
} Var;

typedef struct {
    Var v[VARS_MAX + VARS_SPECIAL_MAX];
    int n;
} VarList;


/*
 * Check a name of a variable of the user.  Returns 0, or -1 with a message
 * that names the rule in err (a name that begins with a period is refused
 * here, as the special variables cannot be given).
 */
int
var_check_name(const char *name, char *err, size_t errlen);


/* Check the name of a special variable (.STRATA_ and a name). */
int
var_check_special_name(const char *name, char *err, size_t errlen);


/*
 * Check a value, or the literal of an If marker, against the rules of the
 * characters and the length.  Returns 0, or -1 with a message in err.
 */
int
var_check_value(const char *value, char *err, size_t errlen);


/*
 * Set a variable (it replaces one of the same name).  special is 0 for a
 * variable of the user and 1 for a special one.  Returns 0, or -1 with a
 * message in err when a rule is broken or the list is full.
 */
int
vars_set(VarList *l, const char *name, const char *value, int special,
         char *err, size_t errlen);


/* Forget a variable of the user by name (no error when it is not set). */
void
vars_unset(VarList *l, const char *name);


/*
 * The value of a variable, or "undefined" when it is not set or its value
 * is empty.  Never NULL.
 */
const char *
vars_value(const VarList *l, const char *name);


/* Is the variable set to something that is not empty? */
int
vars_is_set(const VarList *l, const char *name);


/* The name of the build type of this program, as .STRATA_BUILD_TYPE has it. */
const char *
vars_build_type(void);


/* Set the special variables that str makes (.STRATA_BUILD_TYPE). */
void
vars_set_specials(VarList *l);


/*
 * Add --var and --special arguments for every variable of the list to argv
 * at index n; cap is the size of argv.  Returns the new count.  The strings
 * are those of the list, which must stay alive until the program starts.
 */
int
vars_args(VarList *l, char **argv, int n, int cap);


/*
 * Read an option of the command line: when argv[*i] is --var or --special
 * and the two arguments follow, set the variable of the process list, move
 * *i to the value and return 1.  Returns 0 when argv[*i] is another option
 * and -1 with a message in err when the option is wrong (a rule is broken
 * or an argument is missing).
 */
int
vars_option(int argc, char **argv, int *i, char *err, size_t errlen);


/* The variables of this process (what the options of the command line gave). */
VarList *
vars_process(void);

#endif
