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

/* term.h - minimal terminal backend (termios + ANSI/VT100), no curses
 * dependency.  The editor talks only to this interface, so an X11 or
 * ncurses backend could replace term.c without touching anything else. */
#ifndef TERM_H
#define TERM_H
#include <stdint.h>

/* attribute bits (same values as layout.h DA_*) */
#define TA_BOLD 0x01
#define TA_UNDER 0x02
#define TA_ITALIC 0x04
#define TA_MARKER 0x08
#define TA_DIM 0x10
#define TA_REVERSE 0x20
#define TA_KEY 0x80 /* a keycap: a box around the cells */

/*
 * The style of a cell: which part of the screen it belongs to.  It is put
 * in the attribute with the bits above, and says which colors the cell gets
 * (color.h).  A cell with no style (0) gets the style that term_style set,
 * and that is none after term_clear.  In a style that is not plain TA_REVERSE
 * is the selected entry, in plain text it is the highlight, TA_DIM is
 * dimmer than the style, and TA_BOLD is the bold face only.
 */
#define TS_PLAIN 0x100 /* The text of a document, whatever term_style says. */
#define TS_STATUS 0x200 /* The status lines of the Editor. */
#define TS_MESSAGE 0x300 /* The message line. */
#define TS_KEYS 0x400 /* The labels of the function keys. */
#define TS_MENU 0x500 /* A menu bar or a popup menu. */
#define TS_MENUSEL 0x600 /* The chosen entry of a menu bar. */
#define TS_PANEL 0x700 /* A panel, its frame and its text. */
#define TS_SELECT 0x800 /* The selected entry of a panel or a list. */
#define TS_CURSOR 0x900 /* The cell of the cursor (the X11 version). */
#define TS_SWATCH 0xA00 /* A sample of colors: see TS_SWATCH_INDEX. */
#define TS_MASK 0xF00
#define TS_SWATCH_INDEX(i) ((i) << 12)
#define TS_SWATCH_OF(attr) (((attr) >> 12) & 0x3FF)
#define TS_SWATCH_MAX 512

/* key codes */
#define KM_SHIFT 0x10000
#define KM_CTRL 0x20000
#define KM_ALT 0x40000
enum {
    K_NONE = 0,
    K_RESIZE = 0x0F00,
    K_NBSP = 0x0F01, /* Ctrl-Shift-Space: a no-break space. */
    K_UP = 0x1000,
    K_DOWN,
    K_LEFT,
    K_RIGHT,
    K_HOME,
    K_END,
    K_PGUP,
    K_PGDN,
    K_INS,
    K_DEL,
    K_ESC,
    K_BTAB,
    K_BKSP,
    K_WHEEL_UP, /* Mouse wheel, X11 only: see term_getkey_mouse. */
    K_WHEEL_DOWN,
    K_CLICK, /* Button 1 pressed: the cell is in term_mouse_cell. */
    K_F1 = 0x1100 /* K_F1+n-1 for Fn */
};
#define K_F(n) (K_F1 + (n) - 1)
#define K_BASE(k) ((k) & 0xFFFF)


int
term_open(void);


/*
 * The notice that the first screen says when the terminal version chose ASCII
 * characters by itself (the terminal does not read UTF-8), once: the next
 * call gives NULL.  The X11 version has none.
 */
const char *
term_charset_notice(void);


/* 1 when no byte of 0x80 or more is written (the VT100 and ASCII sets). */
int
term_is_ascii(void);


void
term_close(void);


int
term_rows(void);


int
term_cols(void);


/* blank the back buffer */
void
term_clear(void);


void
term_put(int x, int y, uint8_t cp437, int attr); /* one CP437 cell */


void
term_putu(int x, int y, unsigned cp, int attr); /* one Unicode cell */


void
term_puts(int x, int y, const char *ascii, int attr);


void
term_fill(int x, int y, int w, int h, uint8_t cp437, int attr);


void
term_box(int x, int y, int w, int h, int attr, int dbl);


/*
 * The style of the cells that are drawn from now on until term_clear, when
 * their attribute has none: TS_PANEL for a panel, 0 for none.
 */
void
term_style(int style);


void
term_cursor(int x, int y, int visible);


void
term_present(void); /* flush changes */


/* -1 timeout -> block */
int
term_getkey(int timeout_ms);


/*
 * The mouse (X11 window only; the terminal version never reports it and
 * sends nothing to the terminal).  A mouse event is delivered only to a
 * caller that asked for it, by the mode of the call; in every other loop
 * the events are dropped, so that a menu or a panel is never moved by the
 * mouse.
 *
 *   MOUSE_OFF         every mouse event is dropped (the default);
 *   MOUSE_WHEEL_KEYS  the wheel is returned as the keys Up and Down, three
 *                     for a step, and Page Up and Page Down with Ctrl; the
 *                     clicks are dropped (the lists and the viewers);
 *   MOUSE_EDITOR      the wheel is returned as K_WHEEL_UP and K_WHEEL_DOWN
 *                     (with KM_CTRL when Ctrl is held) and button 1 as
 *                     K_CLICK; term_mouse_cell gives the cell of the click.
 *
 * term_getkey_mouse is term_getkey with a mode for this one call.  A mouse
 * event is not recorded in a macro.
 */
enum {
    MOUSE_OFF,
    MOUSE_WHEEL_KEYS,
    MOUSE_EDITOR
};


int
term_getkey_mouse(int timeout_ms, int mode);


void
term_mouse_mode(int mode);


/* The screen row and column of the last K_CLICK, counted from 0. */
void
term_mouse_cell(int *row, int *col);


/* The keyboard itself, below the queue and the recording of term_getkey. */
int
term_getkey_raw(int timeout_ms);


/*
 * Is something waiting for the program: a key that was queued by a macro, a
 * key or another event that has arrived and is not read yet?  The main loop
 * leaves out a drawing that the next key would replace, so that a display
 * that is slower than the keyboard repeat never builds a queue of keys that
 * is worked off after the key is released.  An event that is not a key may
 * make this true: the main loop then reads with term_getkey(0) and draws
 * when nothing comes.
 */
int
term_input_pending(void);


/* The same for the keyboard itself, below the queue (the two frontends). */
int
term_raw_pending(void);


/*
 * Is a KEY waiting?  Unlike term_input_pending this is not true for another
 * event (a resize, an exposure, the pointer): the steps of a glide of the
 * window stop only for a key.
 */
int
term_key_pending(void);


/* The same for the keyboard itself, below the queue (the two frontends). */
int
term_raw_key_pending(void);


/*
 * Macros.  term_inject queues keys that term_getkey returns before it reads
 * the keyboard.  term_record starts keeping the keys that term_getkey
 * returns, until Ctrl-F1 is pressed; the key itself is not kept and
 * done is called.  term_recorded gives the keys.
 */
void
term_inject(const int *keys, int n);


void
term_record(int on, void (*done)(void));


const int *
term_recorded(int *n);


void
term_beep(void);


void
term_set_title(const char *title); /* window / terminal title */


int
term_is_gui(void); /* 1 for the X11 backend */


/*
 * The modifier keys held now, as a mask of KM_SHIFT, KM_CTRL and KM_ALT.
 * Only the X11 backend sees a modifier key alone: when one is pressed or
 * released, term_getkey returns K_RESIZE so that the program can draw the
 * function key labels of the modifier.  A terminal always gives 0.
 */
int
term_modifiers(void);


/* GUI: service expose/resize events while waiting */
void
term_idle(void);


/*
 * Images (the X11 version only).  term_gui_info gives the size of a cell
 * in pixels and the colors of the text and of the window (0xRRGGBB), and
 * returns 1; the terminal returns 0 and draws no images.
 *
 * term_image puts a picture of w by h pixels (3 bytes per pixel: red,
 * green, blue) over the cells that begin at cell (cx, cy), moved by (xoff,
 * yoff) pixels.  Only the cells that hold the code TERM_IMAGE_CELL are
 * painted, so a panel that is drawn over the picture stays visible.  The
 * pictures are forgotten by term_clear, and the pixels must stay valid
 * until term_present.
 */
#define TERM_IMAGE_CELL 0xE000

/*
 * The X11 version draws the name LaTeX as TeX does: the A of the word is a
 * smaller letter that is raised, and the E is a letter that is lowered.  The
 * Editor puts these two codes for them (term_putu) when term_is_gui() says
 * that the screen can draw them; they stand for a capital A and a capital E.
 */
#define TERM_LOGO_A 0xE001
#define TERM_LOGO_E 0xE002

int
term_gui_info(int *cell_w, int *cell_h, unsigned *fg, unsigned *bg);


void
term_image(int cx, int cy, int xoff, int yoff, const uint8_t *rgb, int w,
           int h);


/*
 * The cursor drawn around a picture (the X11 version): the rectangle of w by
 * h pixels at (xoff, yoff) from cell (cx, cy).  It replaces the cursor of
 * the cell for this frame and is forgotten by term_clear.
 */
void
term_cursor_box(int cx, int cy, int xoff, int yoff, int w, int h);


/*
 * Poll hook.  While one is set, term_getkey wakes up about every 200 ms and
 * calls it; it returns 1 when the screen must be drawn again, and
 * term_getkey then returns K_RESIZE.
 */
void
term_set_idle(int (*poll)(void));
#endif
