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
 * build_type.h - the build type as constants of the C language.  The build
 * scripts define exactly one of STRATA_DEBUG, STRATA_RELEASE and
 * STRATA_ASAN from the environment variable STRATA_BUILD_TYPE (debug,
 * release or asan).  This is the only place that tests those macros: every
 * other file uses the constants below in ordinary conditions, for example
 *
 *     if (strata_log_detail) {
 *         log_debug("...");
 *     }
 *
 * so that the code is compiled out of a build that does not want it, yet is
 * type checked in every build.  See docs/LOGGING.txt.
 */
#ifndef BUILD_TYPE_H
#define BUILD_TYPE_H

#if defined(STRATA_DEBUG)
#define strata_debug 1
#define strata_release 0
#define strata_asan 0
#elif defined(STRATA_RELEASE)
#define strata_debug 0
#define strata_release 1
#define strata_asan 0
#elif defined(STRATA_ASAN)
#define strata_debug 0
#define strata_release 0
#define strata_asan 1
#else
#error Neither STRATA_(DEBUG|RELEASE|ASAN) defined: invalid build.
#endif

/*
 * The builds that record the detail of a run: the debug build and the asan
 * build (a build made to find faults, so it reports what it finds).  A release
 * build records only warnings, errors and fatal errors.  The record is the
 * log of the run, never the console.
 */
#define strata_log_detail (strata_debug != 0 || strata_asan != 0)

/* The name of the build type, for the log. */
#define strata_build_name \
    (strata_debug != 0 ? "debug" : (strata_release != 0 ? "release" : "asan"))

/*
 * The version of Strata.  The Makefile defines STRATA_VERSION from the name
 * of the branch that is built (scripts/strata-version); a build that does
 * not define it shows "unknown".
 */
#ifndef STRATA_VERSION
#define STRATA_VERSION "unknown"
#endif
#define strata_version STRATA_VERSION

/*
 * The version of the file format that the programs write (the number of the
 * first record of a document, *R<n>; docs/DOC-FORMAT.txt, section 0).  It is
 * the one place that holds the number; --version of every program prints it
 * and the release model lists it.
 */
#define STRATA_FILE_FORMAT 3
#define STRATA_FILE_FORMAT_TEXT "3"

#endif
