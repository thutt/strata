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

/* widgets.h - popup menus, prompts, pager and help viewer shared by stred
   and str. */
#ifndef WIDGETS_H
#define WIDGETS_H
#include "help.h"

/* help file used by help_viewer (may be NULL) */
extern Help *w_help;
/* repaints the screen under a popup (NULL: blank) */
extern void (*w_redraw)(void);


/*
 * Name the place that F1 asks the help about in the popups that follow, in
 * place of their titles (for a title that more than one place uses).  NULL
 * goes back to the titles.
 */
void
w_set_help(const char *name);


void
w_base(void);


void
box(int x, int y, int w, int h, const char *title);


/* index or -1 */
int
popup_menu(const char *title, const char **items, int n, int sel);


/*
 * A list of names with a description of the chosen one in the lines below
 * it.  Returns the number of the entry that Enter chose, or -1 for Esc.
 */
int
popup_list_desc(const char *title, const char **names, const char **descs,
                int n, int sel);


/* What popup_list_desc_up returns when the key Left (or Backspace with no
   typed text) asks to go up one folder. */
#define POPUP_LIST_UP (-2)


/*
 * What popup_list_desc_up returns when a typed text is in no entry of the
 * list: the text is in widgets_list_find, and the caller may look for it in
 * the other folders.  The caller sets widgets_list_find before it shows the
 * list again, to the text that is to be shown as typed.
 */
#define POPUP_LIST_FIND (-4)
extern char widgets_list_find[48];


/* Does the text have a word that begins with the first n characters of find
   (any case)?  A word begins at the start and after a character that is
   neither a letter nor a digit. */
int
list_word_has(const char *text, const char *find, size_t n);


/*
 * The same list for folders and files: the keys Left and Backspace return
 * POPUP_LIST_UP, so that the caller shows the folder above.  The other keys
 * and the results are those of popup_list_desc.
 */
int
popup_list_desc_up(const char *title, const char **names, const char **descs,
                   int n, int sel);


/* 1 = Enter, 0 = Esc */
int
popup_input(const char *title, const char *label, char *buf, int cap);


/*
 * The same for the name of a file: the text is UTF-8, as the file system has
 * it, and a key is put in as a character, not as a byte.
 */
int
popup_name_input(const char *title, const char *label, char *buf, int cap);


/*
 * A panel of fields, as the marker panels are.  Up and Down move
 * between the fields, Ins accepts and Esc cancels.
 */
enum {
    WF_TEXT, /* a line of text */
    WF_CHOICE, /* one entry of a list, changed with Left, Right or Space */
    WF_INFO /* shown, not editable */
};

/*
 * What the Keypad + key (or Alt-+ or F2) shows for a text field: a checklist
 * of directories or of files.  A list of files is of the directory that the
 * first field of the panel holds.
 */
enum {
    PICK_NONE,
    PICK_DIR, /* the directories next to the one in the field */
    PICK_FILE, /* the files of the directory in the first field */
    PICK_LIST, /* the names in the list of the field */
    PICK_CALL /* call: a panel of its own opens (F2 or Keypad +) */
};

typedef struct WField {
    const char *label;
    int kind;
    char val[512]; /* WF_TEXT and WF_INFO */
    const char *const *choices; /* WF_CHOICE, ending with NULL */
    int sel; /* WF_CHOICE */
    int pick; /* PICK_* for a text field */
    const char *suffixes; /* PICK_FILE: suffixes such as ".doc .set", or "" */
    const char *const *list; /* PICK_LIST: the names, ending with NULL */
    /* PICK_CALL: edit the value of the field (cap bytes), arg is for it. */
    void (*call)(char *val, size_t cap, int arg);
    int arg;

    /*
     * PICK_LIST with call: the entry of the list with this text is not a
     * value; choosing it calls call and leaves the value of the field as
     * it was.  NULL when the list has no such entry.
     */
    const char *call_item;

    /*
     * WF_CHOICE only.  refresh, when it is not NULL, is called before the
     * field is drawn and after each key, so that a list that is filled in
     * later (the queues of the printers) can change the entries: it keeps
     * the entry that is chosen (by its text) and prev_sel.  call and
     * call_item work for a choice field too: Enter on the entry call_item,
     * or Ins while it is chosen, calls call (the panel stays) and the
     * field goes back to the entry that was chosen before, prev_sel.
     */
    void (*refresh)(struct WField *f);
    int prev_sel;

    /*
     * A field that is active only while another field has a given choice:
     * dep is the number of that field plus one (0 means that the field is
     * always active) and dep_sel the choice (its sel) that makes it active.
     * A field that is not active stays in its place, drawn as a text that is
     * only shown (like a WF_INFO field), and the cursor skips it.
     */
    int dep;
    int dep_sel;
    /* 1: the field is active while the other has a choice up to dep_sel. */
    int dep_le;

    /*
     * 1 for a PICK_CALL field whose list finishes the panel when it is left
     * by Ins: choosing an entry with Ins fills the field and accepts the
     * panel as if Ins had been pressed in it; Enter only fills the field.
     */
    int finish_on_ins;
} WField;


/*
 * Returns 1 when accepted and 0 when cancelled.  check, when it is not NULL,
 * is called before accepting; it puts a message in err and returns 0 to
 * refuse.
 */
/* 1 after popup_form_nav or popup_form returned 1 for Shift-Ins, else 0. */
extern int widgets_shift_ins;

/* 1 when the last list popup was left by Ins, 0 by Enter or Esc. */
extern int widgets_list_ins;


int
popup_form(const char *title, WField *f, int n,
           int (*check)(const WField *f, int n, char *err, int errcap));


/*
 * The same, for a panel that is one of several: PgUp returns POPUP_NEXT and
 * PgDn POPUP_PREV, so that the caller can show another panel.  Ins still
 * returns 1 and Esc 0.
 */
enum {
    POPUP_NEXT = 2,
    POPUP_PREV = 3
};

/*
 * A list of the directories and files of a directory to walk through: ..
 * goes up, a name with a slash goes into that directory, and Return on a
 * file chooses it.  start is the directory to begin with ("" for the usual
 * one, see widgets_set_dir).  With dirs_only only the directories are shown
 * and the first entry chooses the one that is shown.  suffixes limits the
 * files that are shown ("" shows all).  Returns 1 when something was
 * chosen: dir_out is then the full path of the directory that was shown and
 * name_out the file (empty for a directory); 0 when the user pressed Esc.
 */
int
popup_browse(const char *start, int dirs_only, const char *suffixes,
             char *dir_out, size_t dcap, char *name_out, size_t ncap);


/* The directory that lists of files begin with when a panel has none. */
void
widgets_set_dir(const char *dir);



int
popup_form_nav(const char *title, WField *f, int n,
               int (*check)(const WField *f, int n, char *err, int errcap),
               int paging);


/*
 * A grid of commands in columns, filled top to bottom.  Returns the index
 * chosen, or -1 on Esc.  *sel is the highlighted entry and is kept for next
 * time.
 */
int
popup_grid(const char *title, const char *const *names, int n, int rows,
           int *sel);


void
popup_msg(const char *title, const char *msg);


/*
 * The same box as popup_msg, with a second line (hint, may be NULL) in place
 * of "Press any key", drawn at once: the caller goes on, for a job that is
 * running and that it polls (the search for printers).
 */
void
popup_notice(const char *title, const char *msg, const char *hint);


/*
 * A longer text in a box, broken into lines at the spaces (up to 12 lines),
 * until a key is pressed: the messages of lp and of LaTeX.
 */
void
popup_text(const char *title, const char *text);


/*
 * A text with lines (a list of the errors of a document, say): each line is
 * broken at the spaces to the width of the screen, and the whole is shown in
 * the pager (Up, Down, PgUp, PgDn; Esc closes).  A short text without a line
 * break is shown as popup_text shows it.
 */
void
popup_long(const char *title, const char *text);


int
popup_yn(const char *msg);


/*
 * A long question in a box (broken at the spaces, up to 12 lines): Y takes
 * it as Yes and returns 1; N and Esc return 0.  Enter does not answer.
 */
int
popup_confirm(const char *title, const char *text);


void
pager(const char *title, char **lines, int n);


int
help_ctx_topic(const char *title);


void
help_viewer(int topic);


void
help_context(const char *title);


/*
 * F1 on a message: the topic for the text of the message, or for the place
 * named by title when no topic knows the message.
 */
void
help_message(const char *title, const char *msg);
#endif
