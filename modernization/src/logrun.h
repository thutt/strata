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
 * logrun.h - the log of a run, made by str.  A run is one start of str.
 * str makes the log file, a Unix datagram socket and a collector, a child
 * process that reads the socket and appends one whole line to the log file
 * for every message that the programs send with the module log.h.  One
 * reader means that two programs that log at the same time never mix their
 * lines in the file.  The collector writes only to the log file, never to
 * the console.  See docs/LOGGING.txt.
 */
#ifndef LOGRUN_H
#define LOGRUN_H

#include <stddef.h>

/* The number of run logs that are kept in the directory of the logs. */
#define LOGRUN_KEEP 10u

/*
 * Makes the log of a run in the directory dir (and the directories above
 * it that are missing): deletes the sockets of runs that ended badly and
 * the logs beyond the newest keep - 1, makes the log file
 * run-DATE-TIME.log, the socket and the collector, points the link
 * current.log at the new file and puts the name of the socket in the
 * environment variable STRATA_LOG_SOCKET for the programs that the caller
 * starts.  Returns 0, or -1 when the run has no log (the programs then log
 * nowhere).
 */
int logrun_start(const char *dir, size_t keep);

/*
 * Ends the run: the collector writes what is still in the socket, deletes
 * the socket and ends.  Safe to call more than once.
 */
void logrun_end(void);

/* The path of the log file of this run, or NULL when the run has none. */
const char *logrun_path(void);

#endif
