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
 * eqrender.h - typesetting equations into images in the background.  The
 * text of an Equation marker is TeX math (EQUATIONS.txt).  An image is made
 * by running latex and dvipng in a child process; finished images are kept
 * in memory and in a cache directory.  Only the X11 version asks for them.
 */
#ifndef EQRENDER_H
#define EQRENDER_H
#include <stdint.h>
#include <stdio.h>

typedef struct {
    int w; /* pixels */
    int h;
    int depth; /* pixels below the baseline, or 0 */
    uint8_t *rgb; /* w * h * 3 bytes */
} EqImage;


/*
 * Switch the renderer on: dpi is the resolution the images are made at, fg
 * and bg are the colors (0xRRGGBB) of the text and of the window.  Calling
 * it again with other values forgets the images made so far.
 */
void
eqr_configure(int dpi, unsigned fg, unsigned bg);


/*
 * The same at a resolution of your own: the equation editor shows an
 * enlarged picture.  eqr_dpi is the resolution of eqr_configure.
 */
const EqImage *
eqr_request_dpi(const char *tex, int display, int dpi);


const EqImage *
eqr_peek_dpi(const char *tex, int display, int dpi);


const char *
eqr_error_dpi(const char *tex, int display, int dpi);


int
eqr_dpi(void);


/* 1 after eqr_configure, when latex and dvipng were found. */
int
eqr_enabled(void);


/*
 * 1 after eqr_configure, when pdflatex and pdftoppm were found: then
 * pictures can be made.  A picture is asked for like an equation with
 * display 2 and the text "file|width|height|scale" (the sizes in
 * thousandths of an inch and of one, 0 when left out); the file must be
 * a PNG, JPEG, PDF or EPS file.  A keycap is asked for with display 3 and
 * the text "legend|foreground|background|scale" (tenths, tenths and
 * thousandths); its image has the depth of the box below the baseline, so
 * that it can stand in a line of text like an inline equation.
 */
int
eqr_pictures_enabled(void);


/*
 * The image of the equation, or NULL when it is not ready.  The first call
 * for an equation starts the work (one job runs at a time; the others
 * wait); later calls only look.  display is 1 for a displayed equation and
 * 0 for an inline one.
 */
const EqImage *
eqr_request(const char *tex, int display);


/* The image when it is ready; never starts any work. */
const EqImage *
eqr_peek(const char *tex, int display);


/*
 * The picture of a LaTeX marker is asked for with display 4 and the text
 * "points|text": the size of the text of the document (8 to 20) and the
 * text that the marker holds, with the comments left out (latexmark.h).  It
 * is typeset alone, with the preamble that eqr_set_marker_head gives, in
 * the color of the text of the window; its depth is the depth of its box.
 * A marker that takes more than 5 seconds is stopped.
 */
void
eqr_set_marker_head(int (*head)(int pts, FILE *out));


/*
 * For a picture that failed: E when LaTeX told an error (eqr_error has its
 * first line), R when the picture was refused for another reason (no
 * width, no ink, larger than the job makes, too slow), and NUL when it did
 * not fail or is not known.
 */
char
eqr_error_kind(const char *tex, int display, int dpi);


/*
 * The error message of a picture that failed, once: the first call after
 * the failure gives it, later calls give NULL, so that it is told to the
 * log only one time.
 */
const char *
eqr_error_take(const char *tex, int display, int dpi);


/* The message of an equation that could not be typeset, else NULL. */
const char *
eqr_error(const char *tex, int display);


/*
 * Look at the jobs.  Returns 1 when one has finished since the last call,
 * so that the screen should be drawn again.
 */
int
eqr_poll(void);


/* 1 while there is a job running or waiting. */
int
eqr_busy(void);


/*
 * Why a text is refused before it is given to latex (a command that
 * reads or writes files, or loads a package), or NULL when it is safe.  The
 * tests use it.
 */
const char *
eqr_unsafe(const char *tex);


/* The cache directory in use (created when it is first needed). */
const char *
eqr_cache_dir(void);
#endif
