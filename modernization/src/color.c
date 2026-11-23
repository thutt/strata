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

/* color.c - the colors of the screen (see color.h). */
#include "util.h"
#include "color.h"
#include "term.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The VGA palette, in the order of the numbers of the ANSI colors. */
static const struct {
    const char *name;
    unsigned rgb;
} vga[16] = {
    {"black", 0x000000},       {"blue", 0x0000AA},
    {"green", 0x00AA00},       {"cyan", 0x00AAAA},
    {"red", 0xAA0000},         {"magenta", 0xAA00AA},
    {"brown", 0xAA5500},       {"light-grey", 0xAAAAAA},
    {"dark-grey", 0x555555},   {"light-blue", 0x5555FF},
    {"light-green", 0x55FF55}, {"light-cyan", 0x55FFFF},
    {"light-red", 0xFF5555},   {"light-magenta", 0xFF55FF},
    {"yellow", 0xFFFF55},      {"white", 0xFFFFFF}};

/* Other names of the same colors, with no spaces, hyphens or capitals. */
static const struct {
    const char *name;
    int index;
} alias[] = {{"gray", 7},         {"lightgray", 7},   {"darkgray", 8},
             {"brightblack", 8},  {"brightblue", 9},  {"brightgreen", 10},
             {"brightcyan", 11},  {"brightred", 12},  {"brightmagenta", 13},
             {"brightyellow", 14}, {"brightwhite", 15}, {"lightyellow", 14},
             {"lightwhite", 15},  {"darkblue", 1},    {"darkgreen", 2},
             {"darkcyan", 3},     {"darkred", 4},     {"darkmagenta", 5},
             {"darkyellow", 6},   {"orange", 6}};

/* The kinds: the key of the settings, the label of the panel, the sample. */
static const struct {
    const char *key;
    const char *label;
    int attr;
    unsigned fg;
    unsigned bg;
} kinds[CK_COUNT] = {
    {"normal", "Normal text", TS_PLAIN, 0xFFFF55, 0x0000AA},
    {"bold", "Bold text", TS_PLAIN | TA_BOLD, 0xFFFFFF, 0x0000AA},
    {"italic", "Italic text", TS_PLAIN | TA_ITALIC, 0x55FF55, 0x0000AA},
    {"underline", "Underlined text", TS_PLAIN | TA_UNDER, 0xFFFF55,
     0x0000AA},
    {"marker", "Markers", TS_PLAIN | TA_MARKER, 0x55FFFF, 0x0000AA},
    {"dim", "Dim text, ruler", TS_PLAIN | TA_DIM, 0xAAAAAA, 0x0000AA},
    {"highlight", "Highlight", TS_PLAIN | TA_REVERSE, 0x000000, 0x00AAAA},
    {"print_error", "Print Error markers",
     TS_PLAIN | TA_MARKER | TA_BOLD | TA_UNDER, 0xFF5555, 0x0000AA},
    {"keycap", "Keycaps", TS_PLAIN | TA_KEY, 0x000000, 0xAAAAAA},
    {"status", "Status line", TS_STATUS, 0x000000, 0xAAAAAA},
    {"message", "Message line", TS_MESSAGE, 0x000000, 0xAAAAAA},
    {"keys", "Function key labels", TS_KEYS, 0x000000, 0xAAAAAA},
    {"menu", "Menus", TS_MENU, 0x000000, 0xAAAAAA},
    {"panel", "Panels", TS_PANEL, 0x000000, 0xAAAAAA},
    {"selected", "Selected entry", TS_SELECT, 0xFFFFFF, 0x000000},
    {"cursor", "Cursor", TS_CURSOR, 0x000000, 0xFFB000}};

static ColorSet active;
static int active_set;
static unsigned generation = 1;
static int forced;
static int depth = DEPTH_16;
static ColorPair swatch[TS_SWATCH_MAX];
static int nswatch;


const char *
color_kind_key(int kind)
{
    return kinds[kind].key;
}


const char *
color_kind_label(int kind)
{
    return kinds[kind].label;
}


int
color_kind_attr(int kind)
{
    return kinds[kind].attr;
}


unsigned
color_default_of(int kind, int bg)
{
    return bg != 0 ? kinds[kind].bg : kinds[kind].fg;
}


void
color_defaults(ColorSet *cs)
{
    for (int k = 0; k < CK_COUNT; k++) {
        cs->pair[k].fg = kinds[k].fg;
        cs->pair[k].bg = kinds[k].bg;
    }
}


unsigned
color_vga(int index)
{
    return vga[index & 15].rgb;
}


const char *
color_vga_name(int index)
{
    return vga[index & 15].name;
}


/* A name without capitals, spaces, hyphens and underscores. */
static void
squeeze(const char *s, char *out, size_t cap)
{
    size_t n = 0;

    for (; *s != '\0' && n + 1 < cap; s++) {
        if (*s != ' ' && *s != '-' && *s != '_') {
            out[n++] = chr(tolower(u8(*s)));
        }
    }

    out[n] = '\0';
}


int
color_parse(const char *text, unsigned *rgb)
{
    char sq[40];

    while (*text == ' ') {
        text++;
    }

    if (text[0] == '#') {
        if (strlen(text) != 7) {
            return -1;
        }

        for (int i = 1; i < 7; i++) {
            if (isxdigit(u8(text[i])) == 0) {
                return -1;
            }
        }

        sscanf(text + 1, "%x", rgb);

        return 0;
    }

    squeeze(text, sq, sizeof sq);

    for (int i = 0; i < 16; i++) {
        char want[40];

        squeeze(vga[i].name, want, sizeof want);

        if (strcmp(sq, want) == 0) {
            *rgb = vga[i].rgb;

            return 0;
        }
    }

    for (size_t i = 0; i < sizeof alias / sizeof alias[0]; i++) {
        if (strcmp(sq, alias[i].name) == 0) {
            *rgb = vga[alias[i].index].rgb;

            return 0;
        }
    }

    return -1;
}


void
color_text(unsigned rgb, char *out, size_t cap)
{
    for (int i = 0; i < 16; i++) {
        if (vga[i].rgb == rgb) {
            str_copy(out, cap, vga[i].name);

            return;
        }
    }

    snprintf(out, cap, "#%06x", rgb & 0xFFFFFF);
}


unsigned
color_rgb256(int index)
{
    static const unsigned level[6] = {0, 95, 135, 175, 215, 255};

    if (index < 16) {
        return vga[index & 15].rgb;
    }

    if (index < 232) {
        int i = index - 16;

        return (level[i / 36] << 16) | (level[(i / 6) % 6] << 8) |
               level[i % 6];
    }

    unsigned g = uns(8 + 10 * (index - 232));

    return (g << 16) | (g << 8) | g;
}


static unsigned
distance(unsigned a, unsigned b)
{
    int dr = iun((a >> 16) & 0xFF) - iun((b >> 16) & 0xFF);
    int dg = iun((a >> 8) & 0xFF) - iun((b >> 8) & 0xFF);
    int db = iun(a & 0xFF) - iun(b & 0xFF);

    /* The weights are those of the eye: green counts most. */
    return uns(2 * dr * dr + 4 * dg * dg + 3 * db * db);
}


int
color_nearest16(unsigned rgb)
{
    int best = 0;
    unsigned d = ~0U;

    for (int i = 0; i < 16; i++) {
        unsigned x = distance(rgb, vga[i].rgb);

        if (x < d) {
            d = x;
            best = i;
        }
    }

    return best;
}


int
color_nearest256(unsigned rgb)
{
    int best = 16;
    unsigned d = ~0U;

    for (int i = 16; i < 256; i++) {
        unsigned x = distance(rgb, color_rgb256(i));

        if (x < d) {
            d = x;
            best = i;
        }
    }

    return best;
}


/*
 * The relative luminance of a color, 0.0 to 1.0.  The brightness of a
 * channel is its square (the usual curve of a screen is a power of 2.2 to
 * 2.4), which keeps the program free of the math library and is near
 * enough for a warning.
 */
static double
luminance(unsigned rgb)
{
    double c[3];

    for (int i = 0; i < 3; i++) {
        double v = (double)((rgb >> (16 - 8 * i)) & 0xFF) / 255.0;

        c[i] = v * v;
    }

    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
}


double
color_contrast(unsigned a, unsigned b)
{
    double la = luminance(a);
    double lb = luminance(b);

    if (la < lb) {
        double t = la;

        la = lb;
        lb = t;
    }

    return (la + 0.05) / (lb + 0.05);
}


/* A color between two colors: the half of each. */
static unsigned
halfway(unsigned a, unsigned b)
{
    unsigned r = (((a >> 16) & 0xFF) + ((b >> 16) & 0xFF)) / 2;
    unsigned g = (((a >> 8) & 0xFF) + ((b >> 8) & 0xFF)) / 2;
    unsigned bl = ((a & 0xFF) + (b & 0xFF)) / 2;

    return (r << 16) | (g << 8) | bl;
}


/* The kind of text of a cell that has no style. */
static int
plain_kind(int attr)
{
    if ((attr & TA_KEY) != 0) {
        return CK_KEYCAP;
    }

    if ((attr & (TA_MARKER | TA_BOLD | TA_UNDER)) ==
        (TA_MARKER | TA_BOLD | TA_UNDER)) {
        return CK_PRINTERR;
    }

    if ((attr & TA_REVERSE) != 0) {
        return CK_HIGH;
    }

    if ((attr & TA_BOLD) != 0) {
        return CK_BOLD;
    }

    if ((attr & TA_MARKER) != 0) {
        return CK_MARKER;
    }

    if ((attr & TA_ITALIC) != 0) {
        return CK_ITALIC;
    }

    if ((attr & TA_DIM) != 0) {
        return CK_DIM;
    }

    if ((attr & TA_UNDER) != 0) {
        return CK_UNDER;
    }

    return CK_NORMAL;
}


void
color_look(const ColorSet *cs, int attr, CellLook *out)
{
    int style = attr & TS_MASK;
    int kind;

    memset(out, 0, sizeof *out);
    out->bold = (attr & TA_BOLD) != 0;
    out->italic = (attr & TA_ITALIC) != 0;
    out->under = (attr & TA_UNDER) != 0;
    out->key = (attr & TA_KEY) != 0;

    switch (style) {
        case TS_STATUS:
            kind = CK_STATUS;
            break;

        case TS_MESSAGE:
            kind = CK_MESSAGE;
            break;

        case TS_KEYS:
            kind = CK_KEYS;
            break;

        case TS_MENU:
            kind = CK_MENU;
            break;

        case TS_MENUSEL:
        case TS_SELECT:
            kind = CK_SELECT;
            break;

        case TS_PANEL:
            kind = CK_PANEL;
            break;

        case TS_CURSOR:
            kind = CK_CURSOR;
            out->cursor = 1;
            break;

        case TS_SWATCH: {
            int i = TS_SWATCH_OF(attr);

            if (i < nswatch) {
                out->fg = swatch[i].fg;
                out->bg = swatch[i].bg;
            }

            return;
        }

        default:
            kind = plain_kind(attr);
            break;
    }

    out->fg = cs->pair[kind].fg;
    out->bg = cs->pair[kind].bg;

    /* A marker that is dim (a picture whose file is missing) is drawn in
       a colour halfway to the background of its own. */
    if (kind == CK_MARKER && (attr & TA_DIM) != 0 &&
        (style == 0 || style == TS_PLAIN)) {
        out->fg = halfway(out->fg, out->bg);
    }

    if (style != 0 && style != TS_PLAIN && style != TS_CURSOR) {
        if ((attr & TA_REVERSE) != 0 && kind != CK_SELECT) {
            out->fg = cs->pair[CK_SELECT].fg;
            out->bg = cs->pair[CK_SELECT].bg;
        }

        if ((attr & TA_KEY) != 0) {
            out->fg = cs->pair[CK_KEYCAP].fg;
            out->bg = cs->pair[CK_KEYCAP].bg;
        }

        if ((attr & TA_DIM) != 0) {
            out->fg = halfway(out->fg, out->bg);
        }
    }
}


int
color_plain_attr(int attr)
{
    int style = attr & TS_MASK;
    int bits = attr & 0xFF;

    switch (style) {
        case TS_STATUS:
        case TS_MESSAGE:
        case TS_KEYS:
        case TS_MENU:
        case TS_SELECT:
            bits |= TA_REVERSE;
            break;

        default:
            break;
    }

    return bits;
}


void
color_set_swatches(const ColorPair *pairs, int n)
{
    if (n > TS_SWATCH_MAX) {
        n = TS_SWATCH_MAX;
    }

    if (n != nswatch || memcmp(swatch, pairs, usz(n) * sizeof pairs[0]) != 0) {
        generation++;
    }

    for (int i = 0; i < n; i++) {
        swatch[i] = pairs[i];
    }

    nswatch = n;
}


unsigned
color_readable(unsigned bg)
{
    return luminance(bg) > 0.3 ? 0x000000 : 0xFFFFFF;
}


/* The text of a variable that tells how many colors a terminal shows. */
int
color_depth_of(const char *strata, const char *colorterm, const char *term)
{
    if (strata != NULL && strata[0] != '\0') {
        if (strcmp(strata, "none") == 0 || strcmp(strata, "0") == 0) {
            return DEPTH_NONE;
        }

        if (strcmp(strata, "16") == 0) {
            return DEPTH_16;
        }

        if (strcmp(strata, "256") == 0) {
            return DEPTH_256;
        }

        if (strcmp(strata, "truecolor") == 0 || strcmp(strata, "24bit") == 0) {
            return DEPTH_TRUE;
        }
    }

    if (colorterm != NULL && (strstr(colorterm, "truecolor") != NULL ||
                              strstr(colorterm, "24bit") != NULL)) {
        return DEPTH_TRUE;
    }

    if (term == NULL || term[0] == '\0' || strcmp(term, "dumb") == 0 ||
        strcmp(term, "unknown") == 0 || strncmp(term, "vt", 2) == 0) {
        return DEPTH_NONE;
    }

    if (strstr(term, "truecolor") != NULL || strstr(term, "-direct") != NULL) {
        return DEPTH_TRUE;
    }

    if (strstr(term, "256color") != NULL) {
        return DEPTH_256;
    }

    return DEPTH_16;
}


void
color_force_defaults(void)
{
    forced = 1;

    /* A program that this one starts (the Editor) uses them too. */
    setenv("STRATA_DEFAULT_COLORS", "1", 1);
}


int
color_defaults_forced(void)
{
    const char *e = getenv("STRATA_DEFAULT_COLORS");

    return forced != 0 || (e != NULL && e[0] != '\0' && strcmp(e, "0") != 0);
}


int
color_depth(void)
{
    return depth;
}


void
color_set_depth(int d)
{
    depth = d;
}


const ColorSet *
color_active(void)
{
    if (active_set == 0) {
        color_defaults(&active);
        active_set = 1;
    }

    return &active;
}


void
color_activate(const ColorSet *cs)
{
    active = *cs;
    active_set = 1;
    generation++;
}


unsigned
color_generation(void)
{
    return generation;
}
