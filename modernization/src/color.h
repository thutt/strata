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
 * color.h - the colors of the screen.
 *
 * Every cell of the screen has an attribute (term.h): the bits TA_BOLD and
 * the others, and a style (TS_STATUS and the others) for the lines and
 * panels of the programs.  This module determines which foreground and which
 * background color a cell gets.  There are KIND_COUNT kinds of text, each
 * with a foreground and a background color, and the user can set them in
 * the Setup of the Document Manager, separately for the terminal version
 * and for the X11 version (config.h keeps them in the file of the
 * settings).  A color is a 0xRRGGBB number; the VGA palette gives the names.
 *
 * The terminal cannot show every color: color_depth tells how many it can,
 * and color_nearest16 and color_nearest256 find the color that it can show.
 */
#ifndef COLOR_H
#define COLOR_H
#include <stddef.h>

/* The kinds of text.  The order is the order of the Colors panel. */
enum {
    CK_NORMAL,
    CK_BOLD,
    CK_ITALIC,
    CK_UNDER,
    CK_MARKER,
    CK_DIM,
    CK_HIGH,
    CK_PRINTERR,
    CK_KEYCAP,
    CK_STATUS,
    CK_MESSAGE,
    CK_KEYS,
    CK_MENU,
    CK_PANEL,
    CK_SELECT,
    CK_CURSOR,
    CK_COUNT
};

/* The number of characters of a color as it is kept: "#rrggbb" or a name. */
#define COLOR_TEXT_MAX 16

typedef struct {
    unsigned fg; /* 0xRRGGBB */
    unsigned bg;
} ColorPair;

typedef struct {
    ColorPair pair[CK_COUNT];
} ColorSet;

/* What a cell looks like: its colors, and what is drawn besides them. */
typedef struct {
    unsigned fg;
    unsigned bg;
    int bold; /* The bold face. */
    int italic; /* The italic face. */
    int under; /* The line under the text. */
    int key; /* The box of a keycap. */
    int cursor; /* The cell is the cursor. */
} CellLook;

/* How many colors a terminal shows. */
enum {
    DEPTH_NONE = 0, /* No colors: the attributes of the old terminals. */
    DEPTH_16 = 4,
    DEPTH_256 = 8,
    DEPTH_TRUE = 24
};


/* The key of the settings file for a kind ("normal"), and its name. */
const char *
color_kind_key(int kind);


const char *
color_kind_label(int kind);


/* The attribute, with its style, that draws a sample of the kind. */
int
color_kind_attr(int kind);


/* The colors of the program as it is delivered. */
void
color_defaults(ColorSet *cs);


/*
 * Reads a color: a name of the VGA palette ("light grey", "Light-Grey",
 * "yellow") or #rrggbb.  Returns 0 and sets *rgb, or -1.
 */
int
color_parse(const char *text, unsigned *rgb);


/* The text of a color for the settings file: its VGA name, or #rrggbb. */
void
color_text(unsigned rgb, char *out, size_t cap);


/* The 16 colors of the VGA palette, with the numbers 0 to 15. */
unsigned
color_vga(int index);


const char *
color_vga_name(int index);


/* The palette of 256 colors of the extended terminals. */
unsigned
color_rgb256(int index);


/* The number of the color, among 0 to 15, that is nearest to rgb. */
int
color_nearest16(unsigned rgb);


/* The number of the color, among 16 to 255, that is nearest to rgb. */
int
color_nearest256(unsigned rgb);


/* The ratio of the contrast of two colors, 1.0 to 21.0. */
double
color_contrast(unsigned a, unsigned b);


/* The ratio below which the Colors panel warns about two colors. */
#define COLOR_CONTRAST_LOW 3.0


/*
 * The look of the cell with the attribute attr (TA_ and TS_ bits of
 * term.h) with the colors of cs.
 */
void
color_look(const ColorSet *cs, int attr, CellLook *out);


/*
 * The attribute bits of an old terminal that shows no colors: bold, dim,
 * underline, reverse and the rest, as the programs have always sent them.
 */
int
color_plain_attr(int attr);


/*
 * The colors that the program shows: color_init (it is in config.c, which
 * reads the settings) takes them from the settings and from the
 * environment (see color_force_defaults), for the
 * X11 version when gui is not 0.  color_activate puts other colors in use
 * at once; color_generation changes whenever the colors in use change, so
 * that a backend knows that it must draw every cell again.
 */
void
color_init(int gui);


const ColorSet *
color_active(void);


void
color_activate(const ColorSet *cs);


unsigned
color_generation(void);


/*
 * The recovery of a user who made the screen unreadable: the program
 * starts with the colors that it is delivered with.  The command line
 * option --default-colors calls this, and so does a value in the
 * environment variable STRATA_DEFAULT_COLORS.  Call it before term_open.
 */
void
color_force_defaults(void);


int
color_defaults_forced(void);


/* How many colors the terminal shows (DEPTH_*), as color_init found. */
int
color_depth(void);


/* Sets the depth, which color_init finds (and the tests change). */
void
color_set_depth(int depth);


/*
 * The depth of a terminal from its environment: the variable
 * STRATA_COLORS (none, 16, 256, truecolor), COLORTERM, then TERM.  The
 * arguments are the values of the variables, or NULL.
 */
int
color_depth_of(const char *strata, const char *colorterm, const char *term);


/*
 * The samples of a color panel: a cell with the style TS_SWATCH and the
 * attribute bits TS_SWATCH_INDEX(i) is drawn with the colors pairs[i] (and
 * the bold face, the italic face and the line under, if the attribute has
 * them).  Setting the samples does not change the colors in use.
 */
void
color_set_swatches(const ColorPair *pairs, int n);


/* Black or white, whichever can be read on the background bg. */
unsigned
color_readable(unsigned bg);


/* The default of a color of one kind: 0 for the foreground, 1 for bg. */
unsigned
color_default_of(int kind, int bg);
#endif
