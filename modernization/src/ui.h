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

/* ui.h - user interface: menus, prompts, help viewer, main loop. */
#ifndef UI_H
#define UI_H
#include "editor.h"
#include "help.h"
#include <stdio.h>

typedef struct {
    Editor *ed;
    Help *help;
    char helppath[512];
    int quit;
    int pending_op; /* OP_* while a highlight is in progress, else -1 */
    int lib_pending; /* the highlight is for Lib Copy or Lib Cut */
    int pending_move; /* highlight is part of a Move */
    /* 1 while the highlighted columns, blocks or sections are being moved */
    int moving;
    int move_steps; /* how many steps of the Move have been made */
    /*
     * The windows of Edit, Window: win[0] is the top one.  nwin is 1 or 2;
     * wcur is the window of the cursor (its Editor is ed); wzoom is 1 when
     * the window of the cursor fills the screen; wsplit is the first screen
     * row of the second window.
     */
    Editor *win[2];
    int nwin;
    int wcur;
    int wzoom;
    int wsplit;
    int case_mode; /* CASE_* while the highlight is for Case (Ctrl-F6) */
    /* a file that does not exist is created unstructured */
    int new_flat;
    /* the description and author given when the file was created */
    char description[160];
    char author[160];
    /* a document that does not exist is a copy of this template */
    char template_path[1024];
    /* Autosave: on or off, and the interval in seconds (0: the default) */
    int autosave_off;
    int error_log; /* 0: stred --preview leaves the Error Log out. */
    int banner; /* 0: stred --preview leaves the banner out. */
    int autosave_seconds;
} App;


int
app_run(App *a, const char *initial_file);


/* formatted pager, no editing */
int
preview_file(App *a, const char *path);


/* formatted plain text, CP437 */
char **
render_lines(Doc *d, int width, int *n);


void
free_lines(char **l, int n);


int
export_text(Doc *d, const char *path, int width, char *err, size_t errlen);


Doc *
import_text(const char *path, int structured, char *err, size_t errlen);


/*
 * Write the menu tree of the F12 menu bar to out, one line for each level
 * (a test reads it).  A line is "menu", a tab, the path of the level (empty
 * for the first level), a tab and the names of the entries joined by bars.
 */
void
ui_dump_menus(FILE *out);


/* Print the commands that have a function key, one "name TAB key" a line. */
void
ui_print_fkeys(void);

#endif
