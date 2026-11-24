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
 * log.h - the one place where the programs log.  The rules are the
 * owner's: every program writes its log messages to the log of the run,
 * through a Unix datagram socket whose name str puts in the environment
 * variable STRATA_LOG_SOCKET; str reads the socket and appends the lines
 * to the log file of the run (see logrun.h).  No program writes a log
 * message to the standard output or to the error output, none calls the
 * system log and none writes the log file itself.  What a command line
 * tool prints because it is its job and what a program shows on its own
 * screens is not logging and does not use this module.  See
 * docs/LOGGING.txt.
 *
 * The messages are sent in the syslog format of RFC 3164 (a priority, the
 * program name and the process number, then the text); the collector adds
 * the time.  Every message names the file and the line of its call, which
 * the macros below add, so a line of the log says where it came from.  A
 * message is unique in the whole system and says what failed, with which
 * file or value, and why (tests/logmsgs.py checks this).
 *
 * A message that is only useful to a developer is written with log_debug.
 * A release build records the levels fatal, error and warning and drops
 * the others when the message is made; a debug build and an asan build
 * record all levels.
 */
#ifndef LOG_H
#define LOG_H
#include "build_type.h"

/* The levels, with the numbers of the priorities of syslog. */
#define LOG_LEVEL_FATAL 2
#define LOG_LEVEL_ERROR 3
#define LOG_LEVEL_WARN 4
#define LOG_LEVEL_NOTE 5
#define LOG_LEVEL_DEBUG 7

/* The name of the environment variable that holds the socket. */
#define LOG_SOCKET_ENV "STRATA_LOG_SOCKET"

/*
 * Names the program in what is logged and opens the connection to the
 * collector when the run has one.  Call it first in main.
 */
void log_open(const char *program);

/* Closes the connection. */
void log_close(void);

/*
 * Does this program have a run log, that is, was it started by str with a
 * collector that it could connect to?  The Editor uses it to say that the
 * details of a problem are in the log of the run.
 */
int log_run_active(void);

/* Writes one message.  Use the macros below, which add the place. */
void log_write(int level, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

#define log_error(...) \
    log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...) \
    log_write(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define log_note(...) \
    log_write(LOG_LEVEL_NOTE, __FILE__, __LINE__, __VA_ARGS__)
#define log_debug(...) \
    log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)

/*
 * Reports a condition after which the program cannot go on correctly and
 * ends it with the exit status that is the first argument.  The message is
 * written to the log of the run first.  Without a run the message is lost
 * and the program still ends with the status.
 */
void log_die(int status, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)))
    __attribute__((noreturn));

#define log_fatal(status, ...) \
    log_die(status, __FILE__, __LINE__, __VA_ARGS__)

#endif
