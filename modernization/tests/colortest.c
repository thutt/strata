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
 * Unit tests of the colors of the screen (src/color.c and the color keys of
 * src/config.c): the reading of a color, the defaults, the kind of text of a
 * cell and its colors, the fall-back to the 16 and the 256 colors, the depth
 * of a terminal, the contrast, and the keys of the settings file with their
 * bad values.  The argument is a directory in which the test may make files.
 */
#include "../src/util.h"
#include "../src/color.h"
#include "../src/config.h"
#include "../src/term.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
static int checks;


static void
check_int(const char *what, long got, long want)
{
    checks++;

    if (got != want) {
        printf("FAIL %s: got %ld, want %ld\n", what, got, want);
        failures++;
    }
}


static void
check_hex(const char *what, unsigned got, unsigned want)
{
    checks++;

    if (got != want) {
        printf("FAIL %s: got #%06x, want #%06x\n", what, got, want);
        failures++;
    }
}


static void
check_str(const char *what, const char *got, const char *want)
{
    checks++;

    if (strcmp(got, want) != 0) {
        printf("FAIL %s: got [%s], want [%s]\n", what, got, want);
        failures++;
    }
}


/* The reading of a color: the names of the VGA palette and #rrggbb. */
static void
test_parse(void)
{
    unsigned rgb = 0;

    check_int("parse: yellow", color_parse("yellow", &rgb), 0);
    check_hex("parse: yellow is bright", rgb, 0xFFFF55);
    check_int("parse: Light Grey", color_parse("Light Grey", &rgb), 0);
    check_hex("parse: Light Grey", rgb, 0xAAAAAA);
    check_int("parse: light-grey", color_parse("light-grey", &rgb), 0);
    check_hex("parse: light-grey", rgb, 0xAAAAAA);
    check_int("parse: LIGHT_CYAN", color_parse("LIGHT_CYAN", &rgb), 0);
    check_hex("parse: LIGHT_CYAN", rgb, 0x55FFFF);
    check_int("parse: gray", color_parse("gray", &rgb), 0);
    check_hex("parse: gray", rgb, 0xAAAAAA);
    check_int("parse: bright white", color_parse("bright white", &rgb), 0);
    check_hex("parse: bright white", rgb, 0xFFFFFF);
    check_int("parse: #1a2B3c", color_parse("#1a2B3c", &rgb), 0);
    check_hex("parse: #1a2B3c", rgb, 0x1A2B3C);
    check_int("parse: not a color", color_parse("bluish", &rgb), -1);
    check_int("parse: empty", color_parse("", &rgb), -1);
    check_int("parse: #12345", color_parse("#12345", &rgb), -1);
    check_int("parse: #1234567", color_parse("#1234567", &rgb), -1);
    check_int("parse: #12345g", color_parse("#12345g", &rgb), -1);
    check_int("parse: no hash", color_parse("1a2b3c", &rgb), -1);

    char text[COLOR_TEXT_MAX];

    color_text(0x0000AA, text, sizeof text);
    check_str("text: blue", text, "blue");
    color_text(0x123456, text, sizeof text);
    check_str("text: #123456", text, "#123456");

    for (int i = 0; i < 16; i++) {
        color_text(color_vga(i), text, sizeof text);
        check_int("text: a VGA name reads back", color_parse(text, &rgb), 0);
        check_hex("text: a VGA name reads back as itself", rgb, color_vga(i));
    }
}


/* The colors that the programs are delivered with. */
static void
test_defaults(void)
{
    ColorSet cs;

    color_defaults(&cs);
    check_hex("default: normal text is yellow", cs.pair[CK_NORMAL].fg,
              0xFFFF55);
    check_hex("default: normal background is blue", cs.pair[CK_NORMAL].bg,
              0x0000AA);
    check_hex("default: bold text is bright white", cs.pair[CK_BOLD].fg,
              0xFFFFFF);
    check_hex("default: bold text on blue", cs.pair[CK_BOLD].bg, 0x0000AA);
    check_hex("default: the status line is black", cs.pair[CK_STATUS].fg,
              0x000000);
    check_hex("default: the status line on light grey", cs.pair[CK_STATUS].bg,
              0xAAAAAA);
    check_hex("default: the selected entry is white on black (fg)",
              cs.pair[CK_SELECT].fg, 0xFFFFFF);
    check_hex("default: the selected entry is white on black (bg)",
              cs.pair[CK_SELECT].bg, 0x000000);
    check_hex("default: the cursor is amber", cs.pair[CK_CURSOR].bg, 0xFFB000);

    /* Every default can be read back and no pair is unreadable. */
    for (int k = 0; k < CK_COUNT; k++) {
        if (k == CK_CURSOR) {
            continue;
        }

        checks++;

        if (color_contrast(cs.pair[k].fg, cs.pair[k].bg) < COLOR_CONTRAST_LOW) {
            printf("FAIL default: %s is hard to read\n", color_kind_key(k));
            failures++;
        }
    }
}


/* Which kind of text a cell is, and the colors that it gets. */
static void
test_look(void)
{
    ColorSet cs;
    CellLook lk;

    color_defaults(&cs);
    color_look(&cs, 0, &lk);
    check_hex("look: no attribute is normal text", lk.fg, 0xFFFF55);
    check_hex("look: no attribute, background", lk.bg, 0x0000AA);
    color_look(&cs, TA_BOLD, &lk);
    check_hex("look: bold", lk.fg, 0xFFFFFF);
    check_int("look: bold has the bold face", lk.bold, 1);
    color_look(&cs, TA_ITALIC, &lk);
    check_hex("look: italic", lk.fg, 0x55FF55);
    color_look(&cs, TA_UNDER, &lk);
    check_hex("look: underline", lk.fg, 0xFFFF55);
    check_int("look: underline has the line", lk.under, 1);
    color_look(&cs, TA_MARKER, &lk);
    check_hex("look: marker", lk.fg, 0x55FFFF);
    color_look(&cs, TA_DIM, &lk);
    check_hex("look: dim", lk.fg, 0xAAAAAA);
    color_look(&cs, TA_REVERSE, &lk);
    check_hex("look: highlight, fg", lk.fg, 0x000000);
    check_hex("look: highlight, bg", lk.bg, 0x00AAAA);
    color_look(&cs, TA_MARKER | TA_BOLD | TA_UNDER, &lk);
    check_hex("look: a Print Error marker", lk.fg, 0xFF5555);
    color_look(&cs, TA_MARKER | TA_BOLD, &lk);
    check_hex("look: a bold marker is bold", lk.fg, 0xFFFFFF);
    color_look(&cs, TA_KEY, &lk);
    check_hex("look: keycap, bg", lk.bg, 0xAAAAAA);
    check_int("look: keycap has its box", lk.key, 1);
    color_look(&cs, TS_STATUS, &lk);
    check_hex("look: status line, bg", lk.bg, 0xAAAAAA);
    color_look(&cs, TS_PANEL | TA_BOLD, &lk);
    check_hex("look: bold in a panel keeps the color of the panel", lk.fg,
              0x000000);
    check_int("look: bold in a panel is the bold face", lk.bold, 1);
    color_look(&cs, TS_PANEL | TA_REVERSE, &lk);
    check_hex("look: reverse in a panel is the selected entry, bg", lk.bg,
              0x000000);
    check_hex("look: reverse in a panel is the selected entry, fg", lk.fg,
              0xFFFFFF);
    color_look(&cs, TS_PANEL | TA_DIM, &lk);
    checks++;

    if (lk.fg == 0x000000 || lk.fg == 0xAAAAAA) {
        printf("FAIL look: dim in a panel is between the two colors\n");
        failures++;
    }

    color_look(&cs, TS_MENUSEL, &lk);
    check_hex("look: the entry of a menu bar, bg", lk.bg, 0x000000);
    color_look(&cs, TS_CURSOR, &lk);
    check_hex("look: cursor, bg", lk.bg, 0xFFB000);
    check_int("look: cursor flag", lk.cursor, 1);

    /* A color of the settings changes the kind and only the kind. */
    cs.pair[CK_ITALIC].fg = 0x123456;
    color_look(&cs, TA_ITALIC, &lk);
    check_hex("look: a changed color is used", lk.fg, 0x123456);
    color_look(&cs, TA_BOLD, &lk);
    check_hex("look: the others are not changed", lk.fg, 0xFFFFFF);

    /* The attributes of a terminal with no colors are the old ones. */
    check_int("plain: a status line is reverse",
              color_plain_attr(TS_STATUS) & TA_REVERSE, TA_REVERSE);
    check_int("plain: a panel is not reverse",
              color_plain_attr(TS_PANEL) & TA_REVERSE, 0);
    check_int("plain: the chosen entry of a menu bar is not reverse",
              color_plain_attr(TS_MENUSEL) & TA_REVERSE, 0);
    check_int("plain: bold stays bold", color_plain_attr(TA_BOLD), TA_BOLD);
    check_int("plain: a selected entry of a panel is reverse",
              color_plain_attr(TS_PANEL | TA_REVERSE) & TA_REVERSE,
              TA_REVERSE);

    /* The samples of a panel of colors. */
    ColorPair pairs[2] = {{0x010203, 0x040506}, {0x0A0B0C, 0x0D0E0F}};

    color_set_swatches(pairs, 2);
    color_look(&cs, TS_SWATCH | TS_SWATCH_INDEX(1) | TA_BOLD, &lk);
    check_hex("swatch: fg", lk.fg, 0x0A0B0C);
    check_hex("swatch: bg", lk.bg, 0x0D0E0F);
    check_int("swatch: the bold face stays", lk.bold, 1);
}


/* The colors that a terminal can show. */
static void
test_fallback(void)
{
    check_int("nearest16: blue", color_nearest16(0x0000AA), 1);
    check_int("nearest16: a dark blue", color_nearest16(0x000080), 1);
    check_int("nearest16: yellow", color_nearest16(0xFFFF55), 14);
    check_int("nearest16: orange is brown", color_nearest16(0xCC6600), 6);
    check_int("nearest16: black", color_nearest16(0x101010), 0);
    check_int("nearest16: white", color_nearest16(0xF8F8F8), 15);
    check_int("nearest256: pure red", color_nearest256(0xFF0000), 196);
    check_int("nearest256: a grey", color_nearest256(0x808080), 244);
    check_hex("rgb256: the cube", color_rgb256(196), 0xFF0000);
    check_hex("rgb256: the greys", color_rgb256(244), 0x808080);
    check_hex("rgb256: the first 16 are VGA", color_rgb256(14), 0xFFFF55);

    for (int i = 16; i < 256; i++) {
        checks++;

        if (color_nearest256(color_rgb256(i)) != i) {
            printf("FAIL nearest256: color %d is not its own nearest\n", i);
            failures++;
        }
    }

    check_int("depth: nothing is known", color_depth_of(NULL, NULL, NULL),
              DEPTH_NONE);
    check_int("depth: dumb", color_depth_of(NULL, NULL, "dumb"), DEPTH_NONE);
    check_int("depth: vt100", color_depth_of(NULL, NULL, "vt100"),
              DEPTH_NONE);
    check_int("depth: xterm", color_depth_of(NULL, NULL, "xterm"), DEPTH_16);
    check_int("depth: linux", color_depth_of(NULL, NULL, "linux"), DEPTH_16);
    check_int("depth: xterm-256color",
              color_depth_of(NULL, NULL, "xterm-256color"), DEPTH_256);
    check_int("depth: COLORTERM truecolor",
              color_depth_of(NULL, "truecolor", "xterm"), DEPTH_TRUE);
    check_int("depth: COLORTERM 24bit",
              color_depth_of(NULL, "24bit", "xterm-256color"), DEPTH_TRUE);
    check_int("depth: xterm-direct", color_depth_of(NULL, NULL, "xterm-direct"),
              DEPTH_TRUE);
    check_int("depth: STRATA_COLORS none beats all",
              color_depth_of("none", "truecolor", "xterm-256color"),
              DEPTH_NONE);
    check_int("depth: STRATA_COLORS 16",
              color_depth_of("16", "truecolor", NULL), DEPTH_16);
    check_int("depth: STRATA_COLORS 256", color_depth_of("256", NULL, "dumb"),
              DEPTH_256);
    check_int("depth: STRATA_COLORS is not a word of ours",
              color_depth_of("many", NULL, "xterm"), DEPTH_16);
}


static void
test_contrast(void)
{
    checks++;

    if (color_contrast(0x000000, 0xFFFFFF) < 20.0) {
        printf("FAIL contrast: black on white is about 21\n");
        failures++;
    }

    checks++;

    if (color_contrast(0x808080, 0x808080) > 1.01) {
        printf("FAIL contrast: a color on itself is 1\n");
        failures++;
    }

    checks++;

    if (color_contrast(0x0000AA, 0x000080) >= COLOR_CONTRAST_LOW) {
        printf("FAIL contrast: two blues are near\n");
        failures++;
    }

    checks++;

    if (color_contrast(0xFFFF55, 0x0000AA) < COLOR_CONTRAST_LOW) {
        printf("FAIL contrast: yellow on blue is far\n");
        failures++;
    }

    check_hex("readable: on black", color_readable(0x000000), 0xFFFFFF);
    check_hex("readable: on white", color_readable(0xFFFFFF), 0x000000);
}


/* Writes a settings file in the directory and reads the colors of it. */
static void
write_config(const char *dir, const char *text)
{
    char path[1024];

    chmod(dir, 0700);
    snprintf(path, sizeof path, "%s/config", dir);

    FILE *f = fopen(path, "w");

    if (f == NULL) {
        printf("FAIL cannot write %s\n", path);
        failures++;

        return;
    }

    fputs(text, f);
    fclose(f);
    chmod(path, 0600);
}


static void
test_config(const char *base)
{
    char dir[512];
    Config c;
    ColorSet cs;

    snprintf(dir, sizeof dir, "%s/colortest.%d", base, getpid());
    mkdir(dir, 0700);
    setenv("STRATA_HOME", dir, 1);

    config_defaults(&c);
    check_str("config: no setting, no text", c.colors[1][CK_NORMAL][0], "");
    config_colors(&c, 1, &cs);
    check_hex("config: no setting is the default", cs.pair[CK_NORMAL].fg,
              0xFFFF55);

    write_config(dir,
                 "x11_color_normal_fg=#102030\n"
                 "x11_color_normal_bg=black\n"
                 "terminal_color_normal_fg=light-red\n"
                 "terminal_color_status_bg=#ffffff\n"
                 "x11_color_bold_fg=\n"
                 "x11_color_italic_fg=not-a-color\n"
                 "x11_color_italic_bg=#12345\n"
                 "x11_color_nothing_fg=red\n"
                 "autosave=no\n");
    check_int("config: the file is read", config_load(&c), 0);
    config_colors(&c, 1, &cs);
    check_hex("config: x11 normal text", cs.pair[CK_NORMAL].fg, 0x102030);
    check_hex("config: x11 background by name", cs.pair[CK_NORMAL].bg,
              0x000000);
    check_hex("config: an empty value is the default", cs.pair[CK_BOLD].fg,
              0xFFFFFF);
    check_hex("config: a bad name is the default", cs.pair[CK_ITALIC].fg,
              0x55FF55);
    check_hex("config: a bad number is the default", cs.pair[CK_ITALIC].bg,
              0x0000AA);
    config_colors(&c, 0, &cs);
    check_hex("config: the terminal has its own colors", cs.pair[CK_NORMAL].fg,
              0xFF5555);
    check_hex("config: the terminal background is not the one of X11",
              cs.pair[CK_NORMAL].bg, 0x0000AA);
    check_hex("config: terminal status background", cs.pair[CK_STATUS].bg,
              0xFFFFFF);
    check_int("config: the other keys are read as before", c.autosave, 0);
    check_str("config: a color is kept as its name", c.colors[1][CK_NORMAL][1],
              "black");
    check_str("config: a color is kept as #rrggbb", c.colors[1][CK_NORMAL][0],
              "#102030");
    check_str("config: a bad value is not kept", c.colors[1][CK_ITALIC][0], "");

    /* Saved and read again: the same colors, and no bad value. */
    char err[200];

    check_int("config: save", config_save(&c, err, sizeof err), 0);

    Config d;

    config_load(&d);
    check_str("config: saved color 1", d.colors[1][CK_NORMAL][0], "#102030");
    check_str("config: saved color 2", d.colors[0][CK_NORMAL][0], "light-red");
    check_str("config: saved color 3", d.colors[0][CK_STATUS][1], "white");

    char path[1024];

    snprintf(path, sizeof path, "%s/config", dir);

    FILE *f = fopen(path, "r");
    char text[4096];
    size_t n = f != NULL ? fread(text, 1, sizeof text - 1, f) : 0;

    text[n] = '\0';

    if (f != NULL) {
        fclose(f);
    }

    checks++;

    if (strstr(text, "not-a-color") != NULL ||
        strstr(text, "#12345\n") != NULL) {
        printf("FAIL config: a bad value was written back\n");
        failures++;
    }

    checks++;

    if (strstr(text, "x11_color_normal_fg=#102030\n") == NULL ||
        strstr(text, "terminal_color_normal_fg=light-red\n") == NULL) {
        printf("FAIL config: the keys are x11_color_KIND_fg and "
               "terminal_color_KIND_bg: %s\n", text);
        failures++;
    }

    char key[80];

    config_color_key(key, sizeof key, 1, CK_PRINTERR, 0);
    check_str("key: x11 print error", key, "x11_color_print_error_fg");
    config_color_key(key, sizeof key, 0, CK_SELECT, 1);
    check_str("key: terminal selected", key, "terminal_color_selected_bg");

    /* Every key of every kind is read, so that none is missing. */
    char all[8192] = "";

    for (int g = 0; g < 2; g++) {
        for (int k = 0; k < CK_COUNT; k++) {
            for (int bg = 0; bg < 2; bg++) {
                char line[120];

                config_color_key(key, sizeof key, g, k, bg);
                snprintf(line, sizeof line, "%s=#0%d0%d0%d\n", key, g + 1,
                         k % 10, bg + 1);
                strcat(all, line);
            }
        }
    }

    write_config(dir, all);
    config_load(&c);

    for (int g = 0; g < 2; g++) {
        for (int k = 0; k < CK_COUNT; k++) {
            for (int bg = 0; bg < 2; bg++) {
                char want[48];

                snprintf(want, sizeof want, "#0%d0%d0%d", g + 1, k % 10,
                         bg + 1);
                check_str("config: every key is read", c.colors[g][k][bg],
                          want);
            }
        }
    }

    unlink(path);
    rmdir(dir);
}


int
main(int argc, char **argv)
{
    test_parse();
    test_defaults();
    test_look();
    test_fallback();
    test_contrast();
    test_config(argc > 1 ? argv[1] : ".");
    printf("colortest: %d checks, %d failures\n", checks, failures);

    return failures != 0 ? 1 : 0;
}
