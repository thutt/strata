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
#include <time.h>
#include <limits.h>
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

/* The sequence that selects the ASCII set and the one that selects the
   graphics set (DEC Special Graphics) for the characters that follow. */
#define SEQ_ASCII "\x1b(B"
#define SEQ_GRAPHICS "\x1b(0"


/*
 * Sent to the terminal at exit: the ASCII character set (a terminal that is
 * left in the graphics set shows letters as lines), attributes, cursor,
 * normal screen.
 */
static const char reset_seq[] = SEQ_ASCII "\x1b[0m\x1b[?25h\x1b[?1049l";
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
 * The characters of the screen: UTF-8 (the shapes of cp437_uni), VT100 (the
 * line characters of the DEC Special Graphics set, ESC ( 0, and ASCII for
 * the rest: no byte of 0x80 or more is ever written) or ASCII.
 *
 * charset_mode is decided once per run, when the terminal is first opened:
 * see choose_charset.  In CS_ASCII every cell is drawn with an ASCII
 * look-alike of the character (cp437_ascii).  The notice is the one line
 * that the first screen says when the program chose by itself a set that is
 * not UTF-8.
 */
enum {
    CS_UTF8,
    CS_VT100,
    CS_ASCII
};

static int charset_mode;
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
 * The VT100 form of each cell of the CP437 screen set, by the number of the
 * character: the letter of the DEC Special Graphics set that draws it, a
 * space for a character that has none (it is drawn as its ASCII look-alike),
 * or '#' for a block (a space in the reverse attribute).  The set has the
 * single lines only, so a double line is drawn as the single line of the
 * same shape.  The letters are: j k l m the corners (right bottom, right
 * top, left top, left bottom), n the cross, q the horizontal line, t u v w
 * the tees (left, right, bottom, top), x the vertical line, a the
 * checkerboard, the quote ` the diamond and the tilde the centered dot.  The
 * letters + , - . of the set are arrows in the definition of the VT100, but a
 * terminal that draws them with a font shows plain signs (observed in an
 * xterm), so the triangles and the arrows keep their ASCII look-alikes > < ^
 * v.  Sixteen characters to a row.
 */
static const char cp437_acs[256 + 1] =
    "    `  ~        " /* 00 */
    "                " /* 10 */
    "                " /* 20 */
    "                " /* 30 */
    "                " /* 40 */
    "                " /* 50 */
    "                " /* 60 */
    "                " /* 70 */
    "                " /* 80 */
    "                " /* 90 */
    "                " /* A0 */
    "aaaxuuukkuxkjjjk" /* B0 */
    "mvwtqnttmlvwtqnv" /* C0 */
    "vwwmmllnnjl#####" /* D0 */
    "                " /* E0 */
    "         ~~   # "; /* F0 */


/*
 * The form of the Unicode character cp of a cell in the VT100 set: the
 * letter of the graphics set (the cell is written between ESC ( 0 and
 * ESC ( B) with *graphic set to 1, a space with *block set to 1 for a block
 * (the cell is written in the reverse attribute), or else the ASCII
 * look-alike.
 */
static char
vt100_of(unsigned cp, int *graphic, int *block)
{
    *graphic = 0;
    *block = 0;

    if (cp >= 0x20 && cp < 0x7F) {
        return chr(iun(cp));
    }

    for (int i = 1; i < 256; i++) {
        if (cp437_uni[i] == cp) {
            char a = cp437_acs[i];

            if (a == '#') {
                *block = 1;

                return ' ';
            }

            if (a != ' ') {
                *graphic = 1;

                return a;
            }

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
 * The number of the string capability acs_chars in a compiled terminal
 * description (the order of the capabilities of the terminfo format).
 */
#define TI_ACS_CHARS 146


/* The 16-bit number at the offset o of the bytes of a description. */
static int
ti_short(const char *d, size_t o)
{
    int v = u8(d[o]) | (u8(d[o + 1]) << 8);

    return v >= 0x8000 ? v - 0x10000 : v;
}


/*
 * Whether the compiled description d (n bytes) of a terminal has the string
 * acs_chars and whether it maps the letters of the line characters to
 * themselves, as the DEC Special Graphics set does (a terminal whose table
 * maps the letters to other bytes does not take ESC ( 0).  Returns 1 or 0,
 * or -1 when the bytes are no description.
 */
static int
ti_has_acsc(const char *d, size_t n)
{
    static const char pairs[] = "jjkkllmmnnqqttuuvvwwxx";

    if (n < 12) {
        return -1;
    }

    int magic = ti_short(d, 0);
    int names = ti_short(d, 2);
    int bools = ti_short(d, 4);
    int nums = ti_short(d, 6);
    int strs = ti_short(d, 8);
    int width = magic == 0x11A ? 2 : 4;

    if ((magic != 0x11A && magic != 0x21E) || names < 0 || bools < 0 ||
        nums < 0 || strs <= TI_ACS_CHARS) {
        return magic == 0x11A || magic == 0x21E ? 0 : -1;
    }

    size_t o = 12 + usz(names) + usz(bools);

    o += o % 2;
    o += usz(nums) * usz(width);

    size_t table = o + usz(strs) * 2;

    if (table > n) {
        return -1;
    }

    int off = ti_short(d, o + TI_ACS_CHARS * 2);

    if (off < 0 || table + usz(off) >= n) {
        return 0;
    }

    const char *a = d + table + usz(off);
    size_t room = n - table - usz(off);

    for (size_t i = 0; pairs[i] != '\0'; i += 2) {
        int found = 0;

        for (size_t k = 0; k + 1 < room && a[k] != '\0'; k += 2) {
            if (a[k] == pairs[i] && a[k + 1] == pairs[i + 1]) {
                found = 1;
            }
        }

        if (found == 0) {
            return 0;
        }
    }

    return 1;
}


/*
 * Reads the description of the terminal named term from the folder dir
 * (a database in the layout of the terminfo folders: x/xterm or 78/xterm).
 * Returns the result of ti_has_acsc, or -1 when the folder has no entry.
 */
static int
ti_read_in(const char *dir, const char *term)
{
    char path[PATH_MAX];
    char buf[16384];

    for (int hexdir = 0; hexdir < 2; hexdir++) {
        int k;

        if (hexdir == 0) {
            k = snprintf(path, sizeof path, "%s/%c/%s", dir, term[0], term);
        } else {
            k = snprintf(path, sizeof path, "%s/%02x/%s", dir,
                         uns(u8(term[0])), term);
        }

        if (k < 0 || usz(k) >= sizeof path) {
            continue;
        }

        FILE *f = fopen(path, "rb");

        if (f == NULL) {
            continue;
        }

        size_t n = fread(buf, 1, sizeof buf, f);

        fclose(f);

        return ti_has_acsc(buf, n);
    }

    return -1;
}


/*
 * The names of the terminals that have the line characters of the VT100 set
 * in their descriptions, for a machine that has no terminal database at all.
 */
static int
name_has_acsc(const char *term)
{
    static const char *const names[] = {
        "xterm", "rxvt", "vt100", "vt102", "vt220", "screen", "tmux", "st",
        "alacritty", "kitty", "foot", "wezterm", "putty"};

    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        size_t n = strlen(names[i]);

        if (strncmp(term, names[i], n) == 0 &&
            (term[n] == '\0' || term[n] == '-')) {
            return 1;
        }
    }

    return 0;
}


/*
 * Whether the entry of the terminal type in the terminal database has the
 * line characters of the VT100 set (the string acs_chars with the letters of
 * the set).  The database is searched like the library of the system does
 * (TERMINFO, the folder .terminfo of the home, TERMINFO_DIRS, the folders
 * of the system); the library itself is not linked.  Returns 1 for a yes.  A
 * type that is not in the database, and a type that has no such capability,
 * give 0 (the terminal is then asked who it is); a machine with no database
 * at all uses the names of name_has_acsc.
 */
static int
term_has_acsc(void)
{
    const char *term = getenv("TERM");
    const char *ti = getenv("TERMINFO");
    const char *dirs = getenv("TERMINFO_DIRS");
    const char *home = getenv("HOME");
    char dir[PATH_MAX];
    int seen_db = 0;

    if (term == NULL || term[0] == '\0' || strchr(term, '/') != NULL) {
        return 0;
    }

    if (ti != NULL && ti[0] != '\0') {
        int r = ti_read_in(ti, term);

        if (r >= 0) {
            return r;
        }

        seen_db = 1;
    }

    if (home != NULL && home[0] != '\0') {
        snprintf(dir, sizeof dir, "%s/.terminfo", home);

        int r = ti_read_in(dir, term);

        if (r >= 0) {
            return r;
        }
    }

    if (dirs != NULL && dirs[0] != '\0') {
        const char *p = dirs;

        while (*p != '\0') {
            size_t n = strcspn(p, ":");

            if (n > 0 && n < sizeof dir) {
                memcpy(dir, p, n);
                dir[n] = '\0';

                int r = ti_read_in(dir, term);

                if (r >= 0) {
                    return r;
                }

                seen_db = 1;
            }

            p += n;

            if (*p == ':') {
                p++;
            }
        }
    }

    static const char *const system_dirs[] = {
        "/etc/terminfo", "/lib/terminfo", "/usr/share/terminfo",
        "/usr/lib/terminfo"};

    for (size_t i = 0; i < sizeof system_dirs / sizeof system_dirs[0]; i++) {
        int r = ti_read_in(system_dirs[i], term);

        if (r >= 0) {
            return r;
        }

        if (access(system_dirs[i], R_OK) == 0) {
            seen_db = 1;
        }
    }

    return seen_db == 0 ? name_has_acsc(term) : 0;
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


/* The longest wait for the answer to the question who the terminal is. */
#define DA_MS 100


/* The milliseconds from the time t0 (CLOCK_MONOTONIC) until now. */
static long
since_ms(const struct timespec *t0)
{
    struct timespec t1;

    clock_gettime(CLOCK_MONOTONIC, &t1);

    return (t1.tv_sec - t0->tv_sec) * 1000 +
           (t1.tv_nsec - t0->tv_nsec) / 1000000;
}


/*
 * Whether a device attributes answer (the first number of ESC [ ? n ; ... c)
 * names a VT100 or a later terminal of that family: 1 is the VT100 (ESC [ ?
 * 1 ; 2 c), 4 the VT132, 6 the VT102, 7 the VT131, 12 the VT125 and 62 to 69
 * the VT220 and the later models.
 */
static int
da_is_vt100(int first)
{
    return first == 1 || first == 4 || first == 6 || first == 7 ||
           first == 12 || (first >= 62 && first <= 69);
}


/*
 * Asks the terminal who it is (the device attributes query, ESC [ c) and
 * waits at most DA_MS milliseconds for the answer.  The answer is read
 * here, so that it is never taken for keys; what the user typed meanwhile is
 * kept in pending.  Returns 1 when the terminal answers that it is a VT100
 * or a later model, else 0 (also when no answer comes).
 */
static int
probe_da(void)
{
    struct timespec t0;
    int state = 0;
    int first = -1;
    int num = 0;
    unsigned char seen[32];
    int nseen = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    fputs("\x1b[c", stdout);
    fflush(stdout);
    probing = 1;

    for (int guard = 0; guard < 400; guard++) {
        long left = DA_MS - since_ms(&t0);
        int c = left > 0 ? rd(isz(lsz(left))) : -1;

        if (c < 0) {
            probing = 0;

            return 0;
        }

        if (state == 0 && c == 0x1b) {
            state = 1;
            nseen = 0;
            first = -1;
            num = 0;
        } else if (state == 1 && c == '[') {
            state = 2;
        } else if (state == 2 && c == '?') {
            state = 3;
        } else if (state == 3 && c >= '0' && c <= '9') {
            num = num * 10 + (c - '0');
        } else if (state == 3 && c == ';') {
            first = first < 0 ? num : first;
            num = 0;
        } else if (state == 3 && c == 'c') {
            first = first < 0 ? num : first;
            probing = 0;

            return da_is_vt100(first);
        } else {
            /* Not the answer: the bytes are the user's, keep them. */
            for (int i = 0; i < nseen && pending_n < PENDING_MAX; i++) {
                pending[pending_n++] = seen[i];
            }

            if (state == 0 && pending_n < PENDING_MAX) {
                pending[pending_n++] = u8(c);
            }

            state = 0;
            nseen = 0;

            continue;
        }

        if (nseen < (int)sizeof seen) {
            seen[nseen++] = u8(c);
        }
    }

    probing = 0;

    return 0;
}


/* Whether the terminal can draw the VT100 lines: the entry of its type in
   the terminal database has them, or the answer to the device attributes
   query is that of a VT100 or later. */
static int
vt100_capable(const char **how)
{
    if (term_has_acsc() != 0) {
        *how = "the terminal type has the line characters (acsc)";

        return 1;
    }

    if (probe_da() != 0) {
        *how = "the terminal answers that it is a VT100 or later";

        return 1;
    }

    *how = "the terminal type has no line characters and the terminal is "
           "not a VT100";

    return 0;
}


/* The name of a mode for the log. */
static const char *
mode_name(int mode)
{
    return mode == CS_UTF8 ? "UTF-8" : mode == CS_VT100 ? "VT100" : "ASCII";
}


/*
 * Decides once how the screen is drawn: UTF-8, VT100 or ASCII.  The order:
 * (a) the variable STRATA_ASCII (1 or 0) and the setting terminal_charset
 * (utf8, vt100, ascii); when there is no setting the screen is VT100, the
 * default.  The rest decides only when the setting is auto: (b) in a
 * multiplexer, which translates the
 * characters itself, UTF-8 when the locale is UTF-8, else VT100 when the
 * terminal type has the line characters, else ASCII; (c) in a UTF-8 locale
 * the answer of probe_cell: one column means UTF-8, and so does no answer;
 * (d) else (a locale that is not UTF-8, or a terminal that moved the cursor
 * by more than one column) VT100 when the terminal can draw it
 * (vt100_capable); (e) else ASCII.  The question of the probe is not asked
 * when the locale is not UTF-8, because the answer cannot change the
 * choice.  The font cannot be tested.  The
 * choice and its reason go to the log (as a warning when the program chose
 * a set that is not UTF-8); that choice leaves a notice for the first
 * screen.
 */
static void
choose_charset(void)
{
    const char *force = getenv("STRATA_ASCII");
    Config cfg;
    const char *how;
    const char *why = "";
    int mode;
    int automatic = 0;

    if (charset_chosen != 0) {
        return;
    }

    charset_chosen = 1;
    config_load(&cfg);

    if (force != NULL && strcmp(force, "1") == 0) {
        mode = CS_ASCII;
        how = "STRATA_ASCII=1";
    } else if (force != NULL && strcmp(force, "0") == 0) {
        mode = CS_UTF8;
        how = "STRATA_ASCII=0";
    } else if (strcmp(cfg.terminal_charset, "ascii") == 0) {
        mode = CS_ASCII;
        how = "the setting terminal_charset=ascii";
    } else if (strcmp(cfg.terminal_charset, "utf8") == 0) {
        mode = CS_UTF8;
        how = "the setting terminal_charset=utf8";
    } else if (strcmp(cfg.terminal_charset, "vt100") == 0) {
        mode = CS_VT100;
        how = "the setting terminal_charset=vt100";
    } else if (cfg.terminal_charset[0] == '\0') {
        mode = CS_VT100;
        how = "the default: there is no setting terminal_charset";
    } else if (in_multiplexer() != 0) {
        automatic = 1;

        if (locale_is_utf8() != 0) {
            mode = CS_UTF8;
            how = "multiplexer, locale is UTF-8";
        } else if (term_has_acsc() != 0) {
            mode = CS_VT100;
            how = "multiplexer, locale is not UTF-8, the terminal type has "
                  "the line characters";
        } else {
            mode = CS_ASCII;
            how = "multiplexer, locale is not UTF-8, the terminal type has "
                  "no line characters";
        }
    } else if (locale_is_utf8() == 0) {
        /* A locale that is not UTF-8 settles it: the question would only
           make a terminal that is not in UTF-8 show three odd characters. */
        automatic = 1;
        mode = vt100_capable(&why) != 0 ? CS_VT100 : CS_ASCII;
        how = "the locale is not UTF-8";
    } else {
        int col = probe_cell();

        automatic = 1;

        if (col == 2) {
            mode = CS_UTF8;
            how = "the terminal moved the cursor by one column and the "
                  "locale is UTF-8";
        } else if (col <= 0) {
            mode = CS_UTF8;
            how = "no answer from the terminal, locale is UTF-8";
        } else {
            mode = vt100_capable(&why) != 0 ? CS_VT100 : CS_ASCII;
            how = "the terminal moved the cursor by more than one column";
        }
    }

    if (automatic != 0 && mode == CS_VT100) {
        snprintf(charset_notice, sizeof charset_notice,
                 "No UTF-8 terminal: VT100 lines are drawn. See Setup, "
                 "terminal_charset.");
    } else if (automatic != 0 && mode == CS_ASCII) {
        snprintf(charset_notice, sizeof charset_notice,
                 "No UTF-8 or VT100 terminal: ASCII is drawn. See Setup, "
                 "terminal_charset.");
    }

    charset_mode = mode;

    /* A set that is not UTF-8, chosen by the program, is logged as a warning
       so that a release build records it too: it is what a person who sees
       odd characters needs to know. */
    if (automatic != 0 && mode != CS_UTF8) {
        log_warn("term: the screen is drawn in %s, not in UTF-8: %s%s%s (the "
                 "font is not checked)", mode_name(mode), how,
                 why[0] != '\0' ? "; " : "", why);
    } else {
        log_note("term: the screen is drawn in %s: %s (the font is not "
                 "checked)", mode_name(mode), how);
    }
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
    return charset_mode != CS_UTF8;
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
    fputs(SEQ_ASCII "\x1b[?1049h", stdout);
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


/*
 * Writes the bytes of a cell to o: the UTF-8 bytes of the character, or its
 * ASCII look-alike (the VT100 set has its own function, vt100_of).
 */
static void
utf8(char *o, int *n, unsigned cp)
{
    if (charset_mode != CS_UTF8) {
        o[(*n)++] = layout_ascii_of(cp);

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
    int graphics = 0; /* 1 while the terminal is in the graphics set */

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

            /* The ASCII set is on before the cursor is moved. */
            if (graphics != 0) {
                n += sprintf(out + n, SEQ_ASCII);
                graphics = 0;
            }

            n += sprintf(out + n, "\x1b[%d;%dH", y + 1, x + 1);

            for (; x < C; x++) {
                b = &back[y * C + x];
                f = &front[y * C + x];

                if (b->cp == f->cp && b->attr == f->attr) {
                    break;
                }

                int attr = iun(b->attr);
                int graphic = 0;
                char ch = ' ';

                if (charset_mode == CS_VT100) {
                    int block = 0;

                    ch = vt100_of(b->cp, &graphic, &block);
                    attr |= block != 0 ? TA_REVERSE : 0;
                }

                if (attr != cur_attr) {
                    n += cell_sequence(out + n, attr);
                    cur_attr = attr;
                }

                if (graphic != graphics) {
                    n += sprintf(out + n, graphic != 0 ? SEQ_GRAPHICS :
                                                         SEQ_ASCII);
                    graphics = graphic;
                }

                if (charset_mode == CS_VT100) {
                    out[n++] = ch;
                } else {
                    utf8(out, &n, b->cp);
                }

                *f = *b;
            }
        }
    }

    /* An update always ends in the ASCII set. */
    if (graphics != 0) {
        n += sprintf(out + n, SEQ_ASCII);
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


/* The terminal version does not use the mouse and never switches it on. */
void
term_mouse_mode(int mode)
{
    (void)mode;
}


void
term_mouse_cell(int *row, int *col)
{
    *row = 0;
    *col = 0;
}
