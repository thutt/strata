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
 * png.h - a small PNG reader: no library, only what dvipng writes (and
 * the usual plain files): non-interlaced, 1 to 8 bits per channel, grey,
 * RGB, palette, grey with alpha and RGBA.  Transparent pixels are mixed
 * with the background color given.
 */
#ifndef PNG_H
#define PNG_H
#include <stddef.h>
#include <stdint.h>

/*
 * Limits.  The pictures are equations, pictures and keycaps made by dvipng
 * at screen resolution: 8 million pixels (for example 4000 by 2000) is far
 * more than any of them needs, and keeps the largest allocation (the
 * decoded data of 4 bytes per pixel and the 3 bytes per pixel result) under
 * 60 MB.  A file is read only up to PNG_MAX_FILE bytes.
 */
#define PNG_MAX_SIDE 20000u
#define PNG_MAX_PIXELS 8000000u
#define PNG_MAX_INFLATED (PNG_MAX_PIXELS * 4u + 20000u)
#define PNG_MAX_FILE (32u * 1024u * 1024u)

/*
 * Reads the file.  On success returns 1, with *w and *h the size and *rgb a
 * malloc'ed array of w*h*3 bytes (red, green, blue per pixel) that the caller
 * frees.  On failure returns 0 and *rgb is not set.
 */
int
png_load(const char *path, uint32_t background, int *w, int *h,
         uint8_t **rgb);


/*
 * The reason why the last call failed, as a sentence a user can read.  The
 * text is static and stays valid until the next call.
 */
const char *
png_error(void);


/* The same for a file that is already in memory. */
int
png_decode(const uint8_t *data, size_t len, uint32_t background, int *w,
           int *h, uint8_t **rgb);


/* The largest depth below the baseline that is taken from dvipng. */
#define PNG_MAX_DEPTH 100000


/*
 * Reads the depth that goes with a picture as plain decimal digits (white
 * space may follow).  Returns -1 for anything else (a sign, a letter, an
 * empty text, more than seven digits) and for a value above limit.
 */
int
png_parse_depth(const char *text, int limit);


/*
 * Reads the size of the page from the text that pdfinfo writes (the line
 * "Page size:   68.344 x 25.196 pts") and gives the pixels of the page at the
 * resolution dpi, the width and the height rounded UP: the size of the
 * picture that a raster program makes of the page.  The raster program
 * does not paint the last pixel, when the page ends inside it, in the color
 * of the page but in white, so the caller scales the page to these whole
 * pixels.  Returns 1 on success and 0 when the line is missing or damaged,
 * when dpi is not positive or when a side is below one pixel or above
 * PNG_MAX_SIDE.
 */
int
png_page_pixels(const char *info, int dpi, int *w, int *h);
#endif
