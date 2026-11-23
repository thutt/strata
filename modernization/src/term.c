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

/* term.c - termios/ANSI terminal backend. */
#include "util.h"
#include "term.h"
#include "layout.h" /* cp437_uni */
#include "config.h"
#include "color.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <poll.h>
#include <sys/ioctl.h>

#include "termbuf.h"
#define R tb_R
#define C tb_C
#define front tb_front
#define back tb_back
#define cx tb_cx
#define cy tb_cy
#define cvis tb_cvis

static struct termios orig_tio;
static int have_orig_tio;
static int is_open;
static int handlers_installed;

/* Sent to the terminal at exit: attributes, cursor, normal screen. */
static const char reset_seq[] = "\x1b[0m\x1b[?25h\x1b[?1049l";
static volatile sig_atomic_t resized;

/*
 * Set when the program changed the color of the cursor of the terminal (the
 * sequence "OSC 12"), so that it is put back at the end, also by a signal.
 */
static volatile sig_atomic_t cursor_colored;
static const char cursor_reset_seq[] = "\x1b]112\x07";

/* The colors in use when the screen was last drawn, and their number. */
static unsigned drawn_generation;

/*
 * The rows of the terminal as the user last made them (full_rows), whether
 * the user changed the height during this run, and the value of the key
 * terminal_rows that is in the settings (0 when it has none).  The program
 * never changes the size of the terminal.
 */
static int full_rows;
static int resize_seen;
static int kept_rows;


static void
on_winch(int s UNUSED)
{
    resized = 1;
}


/*
 * The characters of the screen: UTF-8 (the shapes of cp437_uni) or ASCII.
 *
 * ascii_mode is 1 when every cell is drawn with an ASCII look-alike of the
 * character (cp437_ascii).  It is decided once per run, when the terminal is
 * first opened: see choose_charset.  The notice is the one line that the
 * first screen says when the program chose ASCII by itself.
 */
static int ascii_mode;
static int charset_chosen;
static char charset_notice[96];

/* Bytes that were read while the program waited for the answer of the
   terminal: they are keys that the user typed, and are read later. */
#define PENDING_MAX 64
static unsigned char pending[PENDING_MAX];
static int pending_n;
static int pending_pos;
static int probing; /* 1 while probe_cell waits: pending is not read */

/* The character that is written to ask the terminal where the cursor is
   afterwards: U+2502, three bytes in UTF-8 (E2 94 82), none of them a
   control function of a terminal that reads 8 bits. */
#define PROBE_ROW 2
#define PROBE_MS 800

/*
 * The ASCII look-alike of each cell of the CP437 screen set, by the number
 * of the character (the table cp437_uni of layout.c has the Unicode ones):
 * the box and block characters, the arrows, the triangles, the accented
 * letters (the letter without the accent) and the Greek and math signs.
 * Sixteen characters to a row; the plain ASCII rows are the characters.
 */
static const char cp437_ascii[256 + 1] =
    " oO*****#o#MFdD*" /* 00 */
    "><|!PS-|^v><L-^v" /* 10 */
    " !\"#$%&'()*+,-./" /* 20 */
    "0123456789:;<=>?" /* 30 */
    "@ABCDEFGHIJKLMNO" /* 40 */
    "PQRSTUVWXYZ[\\]^_" /* 50 */
    "`abcdefghijklmno" /* 60 */
    "pqrstuvwxyz{|}~^" /* 70 */
    "CueaaaaceeeiiiAA" /* 80 */
    "EaAooouuyOUcLYPf" /* 90 */
    "aiounNao?--//!<>" /* A0 */
    "::#|++++++|+++++" /* B0 */
    "++++-++++++++=++" /* C0 */
    "+++++++++++#####" /* D0 */
    "aBGpSoutFOOd8oen" /* E0 */
    "=+><||/~o..vn2# "; /* F0 */


/* The ASCII character for the Unicode character cp of a cell. */
static char
ascii_of(unsigned cp)
{
    if (cp >= 0x20 && cp < 0x7F) {
        return (char)cp;
    }

    for (int i = 1; i < 256; i++) {
        if (cp437_uni[i] == cp) {
            return cp437_ascii[i];
        }
    }

    return cp == 0 ? ' ' : '?';
}


/* Whether the name of the locale (LC_ALL, LC_CTYPE, LANG) is UTF-8. */
static int
locale_is_utf8(void)
{
    static const char *const names[] = {"LC_ALL", "LC_CTYPE", "LANG"};
    const char *v = NULL;

    for (int i = 0; i < 3 && v == NULL; i++) {
        const char *e = getenv(names[i]);

        if (e != NULL && e[0] != '\0') {
            v = e;
        }
    }

    if (v == NULL) {
        return 0;
    }

    for (const char *p = v; *p != '\0'; p++) {
        if (strncasecmp(p, "utf-8", 5) == 0 || strncasecmp(p, "utf8", 4) == 0) {
            return 1;
        }
    }

    return 0;
}


/* Whether the terminal is a multiplexer (tmux, screen): the locale determines
   the characters from the locale, whatever the terminal behind it does. */
static int
in_multiplexer(void)
{
    const char *t = getenv("TERM");

    return getenv("TMUX") != NULL || getenv("STY") != NULL ||
           (t != NULL && (strncmp(t, "screen", 6) == 0 ||
                          strncmp(t, "tmux", 4) == 0));
}


/*
 * Put the terminal back the way it was found.  This is safe to call from a
 * signal handler, so it uses write and tcsetattr only.
 */
static void
restore_terminal(void)
{
    if (cursor_colored != 0 &&
        write(1, cursor_reset_seq, sizeof cursor_reset_seq - 1) < 0) {
        return;
    }

    if (write(1, reset_seq, sizeof reset_seq - 1) < 0) {
        return;
    }

    if (have_orig_tio != 0) {
        tcsetattr(0, TCSANOW, &orig_tio);
    }
}


/* A fatal signal: restore the terminal, then die of the same signal. */
static void
on_fatal(int s)
{
    if (is_open != 0) {
        restore_terminal();
    }

    signal(s, SIG_DFL);
    raise(s);
}


static void
at_exit(void)
{
    if (is_open != 0) {
        restore_terminal();
    }
}


static void
install_handlers(void)
{
    static const int fatal[] = {SIGHUP, SIGINT,  SIGQUIT, SIGTERM,
                                SIGSEGV, SIGBUS, SIGABRT, SIGFPE,
                                SIGILL};

    if (handlers_installed != 0) {
        return;
    }

    handlers_installed = 1;
    atexit(at_exit);

    for (size_t i = 0; i < sizeof fatal / sizeof fatal[0]; i++) {
        struct sigaction sa;

        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_fatal;
        sigemptyset(&sa.sa_mask);
        sigaction(fatal[i], &sa, NULL);
    }
}


static void
get_size(void)
{
    struct winsize ws;

    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
        R = ws.ws_row;
        C = ws.ws_col;
    } else {
        R = 25;
        C = 80;
    }

    full_rows = R;
}


static void
alloc_bufs(void)
{
    tb_resize(R, C);
}


/*
 * The code of an ANSI color for a VGA color number (0 to 15): the order of
 * the VGA palette has blue and red the other way round.
 */
static int
ansi_of(int vga)
{
    return (vga & 8) | ((vga & 1) << 2) | (vga & 2) | ((vga >> 2) & 1);
}


/*
 * Writes the codes of the foreground (bg is 0) or the background color rgb,
 * each after a semicolon, as many colors as the terminal shows.  Returns the
 * number of characters.
 */
static int
color_codes(char *out, int bg, unsigned rgb, int depth)
{
    if (depth >= DEPTH_TRUE) {
        return sprintf(out, ";%d;2;%u;%u;%u", bg != 0 ? 48 : 38,
                       (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }

    if (depth >= DEPTH_256) {
        return sprintf(out, ";%d;5;%d", bg != 0 ? 48 : 38,
                       color_nearest256(rgb));
    }

    int a = ansi_of(color_nearest16(rgb));
    int base = a < 8 ? (bg != 0 ? 40 : 30) : (bg != 0 ? 100 : 90);

    return sprintf(out, ";%d", base + (a & 7));
}


/*
 * Writes the sequence that sets the attributes of a cell: everything is set
 * again from the reset, so that the cell never depends on the one before.
 * A terminal with no colors gets the attributes that the programs have
 * always sent.
 */
static int
cell_sequence(char *out, int attr)
{
    int depth = color_depth();
    int n = sprintf(out, "\x1b[0");

    if (depth == DEPTH_NONE) {
        int a = color_plain_attr(attr);

        n += (a & TA_BOLD) != 0 ? sprintf(out + n, ";1") : 0;
        n += (a & TA_DIM) != 0 ? sprintf(out + n, ";2") : 0;
        n += (a & TA_ITALIC) != 0 ? sprintf(out + n, ";3") : 0;
        n += (a & TA_UNDER) != 0 ? sprintf(out + n, ";4") : 0;
        n += (a & (TA_REVERSE | TA_KEY)) != 0 ? sprintf(out + n, ";7") : 0;
        n += (a & TA_MARKER) != 0 ? sprintf(out + n, ";36") : 0;
        n += sprintf(out + n, "m");

        return n;
    }

    CellLook look;

    color_look(color_active(), attr, &look);
    n += look.bold != 0 ? sprintf(out + n, ";1") : 0;
    n += look.italic != 0 ? sprintf(out + n, ";3") : 0;
    n += look.under != 0 ? sprintf(out + n, ";4") : 0;
    n += color_codes(out + n, 0, look.fg, depth);
    n += color_codes(out + n, 1, look.bg, depth);
    n += sprintf(out + n, "m");

    return n;
}


/*
 * Sets the colors for the whole screen and clears it, so that the
 * background shows the color of normal text everywhere, not the color of
 * the terminal.  A terminal that shows no colors is cleared as always.
 * The color of the cursor is set too, where the terminal has colors.
 */
static void
clear_screen(void)
{
    char seq[160];

    drawn_generation = color_generation();
    cell_sequence(seq, TS_PLAIN);
    fputs(seq, stdout);
    fputs("\x1b[2J", stdout);

    if (color_depth() != DEPTH_NONE) {
        unsigned c = color_active()->pair[CK_CURSOR].bg;

        printf("\x1b]12;#%06x\x07", c & 0xFFFFFF);
        cursor_colored = 1;
    }
}


static int
rd(int timeout_ms);


/*
 * Writes U+2502 at a known place and asks the terminal where the cursor is
 * (the cursor position report, ESC [ row ; column R).  The answer is read
 * here, so that it is never taken for keys.  A terminal that reads UTF-8
 * moves the cursor by one column; one that reads 8-bit characters moves it
 * by three.  Returns the column of the cursor after the character (2 for
 * one column), or -1 when no answer came in time.  What the user typed
 * while the program waited is kept in pending.
 */
static int
probe_cell(void)
{
    static const char ask[] = "\x1b[2;1H\xe2\x94\x82\x1b[6n";
    int state = 0;
    int num[2] = {0, 0};
    int n = 0;
    unsigned char seen[32];
    int nseen = 0;

    fputs(ask, stdout);
    fflush(stdout);
    probing = 1;

    for (int guard = 0; guard < 400; guard++) {
        int c = rd(PROBE_MS);

        if (c < 0) {
            probing = 0;

            return -1;
        }

        if (state == 0 && c == 0x1b) {
            state = 1;
            nseen = 0;
            num[0] = num[1] = n = 0;
        } else if (state == 1 && c == '[') {
            state = 2;
        } else if (state == 2 && c >= '0' && c <= '9') {
            num[n] = num[n] * 10 + (c - '0');
        } else if (state == 2 && c == ';' && n == 0) {
            n = 1;
        } else if (state == 2 && c == 'R' && n == 1 && num[0] == PROBE_ROW) {
            probing = 0;

            return num[1];
        } else {
            /* Not the answer: the bytes are the user's, keep them. */
            for (int i = 0; i < nseen && pending_n < PENDING_MAX; i++) {
                pending[pending_n++] = seen[i];
            }

            if (state == 0 && pending_n < PENDING_MAX) {
                pending[pending_n++] = (unsigned char)c;
            }

            state = 0;
            nseen = 0;

            continue;
        }

        if (nseen < (int)sizeof seen) {
            seen[nseen++] = (unsigned char)c;
        }
    }

    probing = 0;

    return -1;
}


/*
 * Decides once whether the screen is drawn with ASCII look-alikes, from (in
 * this order) the variable STRATA_ASCII (1 or 0), the setting
 * terminal_charset (ascii, utf8), and in the automatic case from the
 * terminal itself: in a multiplexer from the locale; else from the answer
 * of probe_cell (one column: UTF-8, anything else: not), and when there is
 * no answer from the locale.  The font cannot be tested.  The choice and
 * its reason go to the log; a choice that the program made by itself leaves
 * a notice for the first screen.
 */
static void
choose_charset(void)
{
    const char *force = getenv("STRATA_ASCII");
    Config cfg;
    const char *how;
    int ascii;
    int automatic = 0;

    if (charset_chosen != 0) {
        return;
    }

    charset_chosen = 1;
    config_load(&cfg);

    if (force != NULL && strcmp(force, "1") == 0) {
        ascii = 1;
        how = "STRATA_ASCII=1";
    } else if (force != NULL && strcmp(force, "0") == 0) {
        ascii = 0;
        how = "STRATA_ASCII=0";
    } else if (strcmp(cfg.terminal_charset, "ascii") == 0) {
        ascii = 1;
        how = "the setting terminal_charset=ascii";
    } else if (strcmp(cfg.terminal_charset, "utf8") == 0) {
        ascii = 0;
        how = "the setting terminal_charset=utf8";
    } else if (in_multiplexer() != 0) {
        automatic = 1;
        ascii = locale_is_utf8() == 0;
        how = ascii != 0 ? "multiplexer, locale is not UTF-8" :
                           "multiplexer, locale is UTF-8";
    } else {
        int col = probe_cell();

        automatic = 1;

        if (col > 0) {
            ascii = col != 2;
            how = ascii != 0 ? "the terminal moved the cursor by more than "
                               "one column" :
                               "the terminal moved the cursor by one column";
        } else {
            ascii = locale_is_utf8() == 0;
            how = ascii != 0 ? "no answer from the terminal, locale is not "
                               "UTF-8" :
                               "no answer from the terminal, locale is UTF-8";
        }
    }

    if (automatic != 0 && ascii != 0) {
        snprintf(charset_notice, sizeof charset_notice,
                 "No UTF-8 terminal: ASCII is drawn. The font is not "
                 "checked. See Setup.");
    }

    ascii_mode = ascii;
    log_note("term: the screen is drawn in %s: %s (the font is not checked)",
             ascii != 0 ? "ASCII" : "UTF-8", how);
}


const char *
term_charset_notice(void)
{
    static char once[96];

    if (charset_notice[0] == '\0') {
        return NULL;
    }

    str_copy(once, sizeof once, charset_notice);
    charset_notice[0] = '\0';

    return once;
}


int
term_is_ascii(void)
{
    return ascii_mode;
}


int
term_open(void)
{
    if (isatty(0) == 0 || isatty(1) == 0) {
        return -1;
    }

    /*
     * Remember the settings only the first time.  Opening again after a
     * child program must not take a half-restored state for the saved one.
     */
    if (have_orig_tio == 0) {
        tcgetattr(0, &orig_tio);
        have_orig_tio = 1;
    }

    install_handlers();
    struct termios t = orig_tio;

    cfmakeraw(&t);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);
    get_size();

    /* The rows that the user chose are the most that the program uses. */
    Config cfg;

    config_load(&cfg);
    kept_rows = cfg.terminal_rows;

    if (kept_rows != 0 && R > kept_rows) {
        R = kept_rows;
    }

    alloc_bufs();
    color_init(0);
    /* alternate screen, enable xterm modifyOtherKeys-free basic mode */
    fputs("\x1b[?1049h", stdout);
    choose_charset();
    clear_screen();
    fputs("\x1b[H", stdout);
    fflush(stdout);
    is_open = 1;

    return 0;
}


/*
 * Keeps the height that the user gave the terminal in the settings, but only
 * when the user changed it during this run.  A failure is logged and is no
 * error.
 */
static void
save_rows(void)
{
    char err[200];

    int rows = config_clamp_rows(full_rows, 0);

    if (resize_seen == 0 || rows == kept_rows) {
        return;
    }

    if (config_set_rows(0, rows, 0, err, sizeof err) != 0) {
        log_warn("term: the height of the terminal, %d rows, was not kept "
                 "in the settings: %s", rows, err);

        return;
    }

    kept_rows = rows;
    resize_seen = 0;
}


void
term_close(void)
{
    if (is_open == 0) {
        return;
    }

    fflush(stdout);
    restore_terminal();
    is_open = 0;
    save_rows();
}


void
term_beep(void)
{
    fputc(7, stdout);
    fflush(stdout);
}


void
term_set_title(const char *t)
{
    printf("\x1b]0;%s\x07", t);
    fflush(stdout);
}


int
term_is_gui(void)
{
    return 0;
}


int
term_modifiers(void)
{
    return 0;
}


void
term_idle(void)
{}


int
term_gui_info(int *cell_w UNUSED, int *cell_h UNUSED, unsigned *fg UNUSED,
              unsigned *bg UNUSED)
{
    return 0;
}


static void
utf8(char *o, int *n, unsigned cp)
{
    if (ascii_mode != 0) {
        o[(*n)++] = ascii_of(cp);

        return;
    }

    *n += utf8_encode(cp, o + *n);
}


/*
 * Take the new size of the terminal, if it changed: the buffers are made
 * again and the next presentation draws every cell.  This is done when the
 * resize is reported to the program, so that the program lays out its next
 * frame with the new number of rows and columns.
 */
static void
apply_resize(void)
{
    if (resized != 0) {
        resized = 0;
        resize_seen = 1;
        get_size();
        alloc_bufs();
        clear_screen();
    }
}


void
term_present(void)
{
    apply_resize();

    /* Colors that changed are drawn on every cell. */
    if (drawn_generation != color_generation()) {
        drawn_generation = color_generation();
        tb_invalidate();
        clear_screen();
    }

    /* The longest sequence of a cell is about 60 characters. */
    char *out = malloc(usz(R) * usz(C) * 72 + 256);
    int n = 0;
    int cur_attr = -1;

    n += sprintf(out + n, "\x1b[?25l");

    for (int y = 0; y < R; y++) {
        int x = 0;

        while (x < C) {
            TC *b = &back[y * C + x];
            TC *f = &front[y * C + x];

            if (b->cp == f->cp && b->attr == f->attr) {
                x++;
                continue;
            }

            n += sprintf(out + n, "\x1b[%d;%dH", y + 1, x + 1);

            for (; x < C; x++) {
                b = &back[y * C + x];
                f = &front[y * C + x];

                if (b->cp == f->cp && b->attr == f->attr) {
                    break;
                }

                if (iun(b->attr) != cur_attr) {
                    n += cell_sequence(out + n, iun(b->attr));
                    cur_attr = iun(b->attr);
                }

                utf8(out, &n, b->cp);
                *f = *b;
            }
        }
    }

    if (cvis != 0) {
        n += sprintf(out + n, "\x1b[%d;%dH\x1b[?25h", cy + 1, cx + 1);
    }

    fwrite(out, 1, usz(n), stdout);
    fflush(stdout);
    free(out);
}


/* ------------------------------------------------------------------ input */
static int
rd(int timeout_ms)
{
    if (probing == 0 && pending_pos < pending_n) {
        return pending[pending_pos++];
    }

    struct pollfd p = {0, POLLIN, 0};
    int r = poll(&p, 1, timeout_ms);

    if (r <= 0) {
        return -1;
    }

    unsigned char c;
    ssize_t k = read(0, &c, 1);

    return k == 1 ? c : -1;
}


static int
mods(int m)
{
    int f = 0;

    m -= 1;

    if ((m & 1) != 0) {
        f |= KM_SHIFT;
    }

    if ((m & 2) != 0) {
        f |= KM_ALT;
    }

    if ((m & 4) != 0) {
        f |= KM_CTRL;
    }

    return f;
}


static int
cp_to_437(unsigned cp)
{
    if (cp < 0x80) {
        return iun(cp);
    }

    for (int i = 0x80; i < 256; i++) {
        if (cp437_uni[i] == cp) {
            return i;
        }
    }

    return -1;
}


static int
parse_csi(void)
{
    int num[4] = {0};
    int n = 0;
    int c;
    int private_seq = 0;

    for (;;) {
        c = rd(50);

        if (c < 0) {
            return K_ESC;
        }

        if (c >= '0' && c <= '9') {
            num[n] = num[n] * 10 + (c - '0');
        } else if (c == ';') {
            if (n < 3) {
                n++;
            }
        } else if (c >= '<' && c <= '?') {
            /* A private marker, as in the answer ESC [ ? 64 ; 1 c of a
               terminal to the question who it is: not a key. */
            private_seq = 1;
        } else {
            break;
        }
    }

    int m = (n >= 1) ? mods(num[1] != 0 ? num[1] : 1) : 0;

    /* The answers of a terminal to questions are not keys: a sequence with
       a private marker (the device attributes), and the cursor position
       report ESC [ row ; column R of a row that is not 1 (a key with a
       modifier has the 1 there: ESC [ 1 ; 5 R is Ctrl-F3). */
    if (private_seq != 0 || (c == 'R' && n >= 1 && num[0] != 1)) {
        return K_NONE;
    }

    switch (c) {
        case 'A':
            return K_UP | m;

        case 'B':
            return K_DOWN | m;

        case 'C':
            return K_RIGHT | m;

        case 'D':
            return K_LEFT | m;

        case 'H':
            return K_HOME | m;

        case 'F':
            return K_END | m;

        case 'Z':
            return K_BTAB;

        case 'u':
            /* A key in the form that some terminals send for a modified
               key: the code of the key, then the modifiers.  Only
               Ctrl-Shift-Space is known. */
            if (num[0] == 32 && m == (KM_CTRL | KM_SHIFT)) {
                return K_NBSP;
            }

            return K_NONE;

        case 'P':
            return K_F(1) | m;

        case 'Q':
            return K_F(2) | m;

        case 'R':
            return K_F(3) | m;

        case 'S':
            return K_F(4) | m;

        case '[': {
            int d = rd(50);

            if (d >= 'A' && d <= 'E') {
                return K_F(d - 'A' + 1);
            }

            return K_ESC;
        }

        case '~': {
            int base;

            switch (num[0]) {
                case 1:
                case 7:
                    base = K_HOME;
                    break;

                case 2:
                    base = K_INS;
                    break;

                case 3:
                    base = K_DEL;
                    break;

                case 4:
                case 8:
                    base = K_END;
                    break;

                case 5:
                    base = K_PGUP;
                    break;

                case 6:
                    base = K_PGDN;
                    break;

                case 11:
                case 12:
                case 13:
                case 14:
                case 15:
                    base = K_F(num[0] - 10);
                    break;

                case 17:
                case 18:
                case 19:
                case 20:
                case 21:
                    base = K_F(num[0] - 11);
                    break;

                case 23:
                case 24:
                    base = K_F(num[0] - 12);
                    break;

                case 27:
                    /* A modified key in the form "27;modifiers;code".  Only
                       Ctrl-Shift-Space is known. */
                    if (num[2] == 32 && m == (KM_CTRL | KM_SHIFT)) {
                        return K_NBSP;
                    }

                    return K_NONE;

                default:
                    return K_NONE;
            }

            return base | m;
        }

        default:
            break;
    }

    return K_NONE;
}


int
term_raw_key_pending(void)
{
    struct pollfd p = {0, POLLIN, 0};

    return poll(&p, 1, 0) > 0 ? 1 : 0;
}


int
term_raw_pending(void)
{
    struct pollfd p = {0, POLLIN, 0};

    if (resized != 0) {
        return 1;
    }

    return poll(&p, 1, 0) > 0 ? 1 : 0;
}


int
term_getkey_raw(int timeout_ms)
{
    for (;;) {
        if (resized != 0) {
            apply_resize();

            return K_RESIZE;
        }

        int c = rd(timeout_ms < 0 ? 250 : timeout_ms);

        if (c < 0) {
            if (timeout_ms < 0 && resized == 0) {
                continue;
            }

            if (resized != 0) {
                apply_resize();

                return K_RESIZE;
            }

            return K_NONE;
        }

        if (c == 0x1b) {
            int d = rd(40);

            if (d < 0) {
                return K_ESC;
            }

            if (d == '[') {
                return parse_csi();
            }

            if (d == 'O') {
                int e = rd(50);
                int fm = 0;

                /*
                 * Konsole sends Shift, Alt and Ctrl with F1 to F4 as
                 * ESC O m P (m is 2 to 8, as in ESC [ 1 ; m P): the same
                 * keys as that form, with the modifiers of m.
                 */
                if (e >= '2' && e <= '8') {
                    fm = mods(e - '0');
                    e = rd(50);

                    if (e < 'P' || e > 'S') {
                        return K_NONE;
                    }
                }

                switch (e) {
                    case 'P':
                        return K_F(1) | fm;

                    case 'Q':
                        return K_F(2) | fm;

                    case 'R':
                        return K_F(3) | fm;

                    case 'S':
                        return K_F(4) | fm;

                    case 'A':
                        return K_UP;

                    case 'B':
                        return K_DOWN;

                    case 'C':
                        return K_RIGHT;

                    case 'D':
                        return K_LEFT;

                    case 'H':
                        return K_HOME;

                    case 'F':
                        return K_END;

                    default:
                        break;
                }

                return K_NONE;
            }

            if (d == 0x1b) {
                return K_ESC;
            }

            if (d < 0x20 && d != 0x0d) {
                return (d + 0x60) | KM_ALT | KM_CTRL;
            }

            return (d == 127 ? K_BKSP : d) | KM_ALT;
        }

        /* Backspace, distinct from Ctrl-H (8) */
        if (c == 127) {
            return K_BKSP;
        }

        /*
         * A terminal that sends only the byte 00 cannot tell Ctrl-Shift-Space
         * from Ctrl-Space and Ctrl-@.  No other key of the Editor is the
         * byte 00, so it is taken for the no-break space.
         */
        if (c == 0) {
            return K_NBSP;
        }

        if (c >= 0xC0) { /* UTF-8 */
            /* Two bytes for C0-DF, three for E0-EF, four for F0 and up. */
            int need = 1;
            unsigned cp = uns(c & 0x1F);

            if (c >= 0xF0) {
                need = 3;
                cp = uns(c & 0x07);
            } else if (c >= 0xE0) {
                need = 2;
                cp = uns(c & 0x0F);
            }

            for (int i = 0; i < need; i++) {
                int d = rd(20);

                if (d < 0) {
                    return K_NONE;
                }

                cp = (cp << 6) | (d & 63);
            }

            int k = cp_to_437(cp);

            return k < 0 ? K_NONE : k;
        }

        return c;
    }
}
