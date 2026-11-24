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
 * config.h - the settings of the user, kept in the directory ~/.strata.
 *
 * The settings that the Document Manager (str, Setup) and the editor (stred,
 * Global, Options) share are in the file ~/.strata/config.  Both programs read
 * it when they start, and write it when a setting is changed.  The directory
 * can be used by its owner only (mode 0700) and so can the file (mode 0600).
 * The environment variable STRATA_HOME names another directory (the tests
 * use it).
 */
#ifndef CONFIG_H
#define CONFIG_H
#include "backup.h"
#include "color.h"
#include <limits.h>
#include <stdio.h>

typedef struct {
    char workdir[PATH_MAX]; /* the initial working directory, or "" */
    char initial_macros[PATH_MAX]; /* Setup: the macro set loaded at start */
    char initial_library[PATH_MAX]; /* Setup: the library loaded at start */
    char stred[512]; /* the editor program, or "" */
    char help[512]; /* the help file, or "" */
    int autosave; /* 1: a changed document is saved every so often */
    int autosave_seconds; /* the interval, 1 to 86400 */
    int expanded_status; /* Global, Options: Expanded Status Line? */
    int onscreen_help; /* Global, Options: Display On-Screen Help? */
    int show_marks; /* Global, Options: Display Tab and End-of-Line */
    int show_dots; /* Global, Options: Display Dots for Spaces? */
    int backups; /* Options, Setup: numbered backups kept, 0 to 99 */
    int backups_explicit; /* 1: the key backups was read or set */
    /* The old key printer: a queue, "File only", "PDF viewer" (once "Open to print") or "". */
    char printer[128];
    char print_to[16]; /* Setup, Print To: printer, file or open, or "" */
    char print_queue[128]; /* Setup, Printer Queue: "" is the default */
    /*
     * The choices of the Print panel that are about the printer, kept from
     * the last print (the keys print_last_*): the Printer field as it was
     * (a queue, File only, PDF viewer), the sides (default, one, two), the
     * order of the pages (default, yes, no) and black and white (yes, no).
     * Empty means that nothing was kept yet: Setup is the default.
     */
    char print_last_printer[128];
    char print_last_sides[8];
    char print_last_sort[8];
    char print_last_bw[4];
    char pdf_dir[PATH_MAX]; /* Setup, PDF Directory: "" is the document's */
    char pdf_viewer[PATH_MAX]; /* The program that shows a PDF, or "" */
    /* A pdf_viewer of an older file that was not a full path: not used. */
    char pdf_viewer_old[PATH_MAX];
    int file_trusted; /* 1: the file of the settings is only the user's */
    char spell_lang[32]; /* Setup, Dictionary Language: en_US, or "" */
    int latex_keep_pictures; /* Export LaTeX: 1 keeps pictures in place */
    int latex_positions; /* Export LaTeX: 1 keeps position comments */
    int latex_markers_off; /* 1: a LaTeX marker is printed as plain text */
    int x11_rows; /* Setup: rows of the X11 window, 0 for the default */
    int terminal_rows; /* Setup: most rows of the terminal, 0 for all */
    /* Setup, Terminal Characters: "" is automatic, or utf8, or ascii */
    char terminal_charset[8];
    /* Setup, Paper for New Documents: "" is Letter, or a4 */
    char default_paper[8];

    /*
     * Setup, Colors: the colors of the screen, as the text of the file:
     * colors[version][kind][0 for the foreground, 1 for the background],
     * with the version 0 for the terminal and 1 for X11.  An empty text is
     * the default color of the kind.
     */
    char colors[2][CK_COUNT][2][COLOR_TEXT_MAX];
    int undo_levels; /* Setup: operations that Undo keeps, 0 for none */
} Config;


/* The numbered backups of a saved document: the most and the default. */
#define CONFIG_BACKUPS_MAX BACKUP_MAX
#define CONFIG_BACKUPS_DEFAULT BACKUP_DEFAULT


/* The one rule for the number of backups: 0 to CONFIG_BACKUPS_MAX. */
int
config_clamp_backups(int n);


/* The number of operations that Undo keeps: the most and the default. */
#define CONFIG_UNDO_MAX 500
#define CONFIG_UNDO_DEFAULT 100


/* The one rule for the number of Undo levels: 0 to CONFIG_UNDO_MAX. */
int
config_clamp_undo(int n);


/* The rows of a window: the least, the most and the default of X11. */
#define CONFIG_ROWS_MIN 20
#define CONFIG_ROWS_MAX 200
#define CONFIG_X11_ROWS_DEFAULT 50


/* The settings that a user who has none gets. */
void
config_defaults(Config *c);


/*
 * Reads the settings.  Returns 0 when the file was read, and 1 when there is
 * none (c has the defaults) or when it cannot be trusted: the directory or
 * the file belongs to somebody else.
 */
int
config_load(Config *c);


/*
 * Writes the settings: the directory is made with mode 0700 and the file is
 * written to a temporary file with mode 0600 and then renamed.  Returns 0, or
 * -1 with a message in err.
 */
int
config_save(const Config *c, char *err, int errcap);


/* The path of the file. */
void
config_path(char *out, int cap);


/*
 * The one rule for a number of rows, used when it is read, when it is
 * written and by Setup: never less than CONFIG_ROWS_MIN, and never more than
 * max (the rows of the screen of X11) or, when max is 0 or more than
 * CONFIG_ROWS_MAX, never more than CONFIG_ROWS_MAX.
 */
int
config_clamp_rows(int rows, int max);


/* Where a print goes by default (Setup, Print To). */
enum {
    CONFIG_PRINT_FILE, /* The PDF only. */
    CONFIG_PRINT_OPEN, /* The PDF, opened in a viewer to print. */
    CONFIG_PRINT_PRINTER /* The PDF, sent to a print queue. */
};


/*
 * The default destination of a print: the key print_to when the file has
 * it; else the old key printer (empty or File only is the PDF only, Open to
 * print opens it, any other text is the name of a queue), so that a file of
 * an older version keeps its meaning.
 */
int
config_print_to(const Config *c);


/* The queue of the default destination: "" is the default of the system. */
void
config_print_queue(const Config *c, char *out, size_t cap);


/*
 * Keep the number of rows of a window in the settings: the key x11_rows (x11
 * is not 0) or terminal_rows.  Only that line of the file is changed; every
 * other line stays as it is.  The file is read just before it is written, so
 * that a change that the other program made is kept, and it is written
 * through a temporary file and a rename.  The number is made right with
 * config_clamp_rows (max as there), so that the file never holds a number
 * that the programs would refuse.  The default of X11, and 0, take the line
 * out.  Returns 0, or -1 with a message in err.
 */
int
config_set_rows(int x11, int rows, int max, char *err, int errcap);

/*
 * The key of a color in the file, such as x11_color_normal_fg or
 * terminal_color_status_bg: gui is 1 for X11, bg is 1 for the background.
 */
void
config_color_key(char *out, size_t cap, int gui, int kind, int bg);


/*
 * The colors of a version: cs has the colors that the settings of the
 * version give, and the default for every kind that has none.
 */
void
config_colors(const Config *c, int gui, ColorSet *cs);


/*
 * The directory of the settings of the user: $STRATA_HOME, or .strata in
 * the home directory.  Returns 0, or -1 when there is no home directory.
 */
int
config_directory(char *out, size_t cap);


/*
 * Other files of the user in the same directory (the font list is one), made
 * and checked in the same way.  config_read_file opens the file NAME for
 * reading when it is there, is the user's own and not for anybody else (its
 * mode is set to 0600), or returns NULL.  config_write_file writes the text to
 * NAME through a temporary file with mode 0600 and a rename, and makes the
 * directory with mode 0700.  Returns 0, or -1 with a message in err.
 */
FILE *
config_read_file(const char *name);


int
config_write_file(const char *name, const char *data, char *err, int errcap);
#endif
