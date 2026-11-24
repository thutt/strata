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

/* term_x11.c - X11 backend for term.h (plain Xlib, core fonts; no toolkit).
 *
 * A window holds a grid of character cells drawn with a monospaced Unicode
 * ("iso10646-1") core font, so the CP437 box-drawing characters used by the
 * screens of the program render natively.  The editor and the master menu
 * are unchanged: they only see the cell-buffer API in term.h.
 *
 * Environment (all optional):
 *   STRATA_X11_FONT   full XLFD of the normal font (the bold font is derived by
 *                replacing "medium" with "bold")
 *   STRATA_X11_XFT    fontconfig family for the anti-aliased (Xft) path, e.g.
 *   "Liberation Mono";
 *                "0" forces the core-font path (default family: DejaVu Sans
 *                Mono)
 *   STRATA_X11_SIZE   Xft: pixel size of the font (default 16).  Core fonts: 13
 *   | 15 | 18 | 20 (default 15)
 *   STRATA_X11_GEOM   COLSxROWS           (default 80x50); the columns stay
 *                fixed and the user may change the rows
 *   STRATA_X11_DUMP   file; the text of the screen is written there after each
 *                redraw (used by the automated tests)
 */
#include "util.h"
#include "term.h"
#include "termbuf.h"
#include "layout.h"
#include "config.h"
#include "color.h"
#include "log.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#ifdef HAVE_XFT
#include <X11/Xft/Xft.h>
#endif
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>

static Display *dpy;
static Window win;
static GC gc;
/*
 * Everything is drawn into this off-screen picture and then copied to the
 * window, so that the window never shows a half-drawn screen.  An exposed
 * window is repaired by copying, without drawing anything again.
 */
static Pixmap pm;
static int dirty_x0 = INT_MAX;
static int dirty_y0 = INT_MAX;
static int dirty_x1 = -1;
static int dirty_y1 = -1;
static XFontStruct *fnt[2]; /* normal, bold */
/* cell width, height, font ascent */
static int cw;
static int chh;
/* The least number of rows: the tallest boxes of the Editor need 20. */
#define MIN_ROWS 20

static int fixed_cols;
static int max_rows;

/*
 * The height that the user chose is kept in the settings once it has not
 * changed for a second, and at the end of the run at the latest.  The rows
 * that are in the file, or that the window had at the start, are saved_rows.
 */
#define SAVE_DELAY_MS 1000
static int saved_rows;
static int geometry_forced; /* STRATA_X11_GEOM is set: nothing is kept */
static int pending_rows; /* 0: nothing waits to be saved */
static struct timeval pending_since;
static int asc;
static Atom wm_delete;

/*
 * The colors that were asked of the server: the color 0xRRGGBB, its pixel
 * and, for the Xft fonts, its XftColor.  There are never more than a few
 * dozen in use (the colors of the kinds of text, and the swatches of the
 * Colors panel); when the table is full the oldest entry is replaced.
 */
#define COLOR_CACHE 512
static struct {
    unsigned rgb;
    unsigned long pixel;
#ifdef HAVE_XFT
    XftColor xft;
#endif
} ccache[COLOR_CACHE];
static int ncached;
static int cache_next;

/* The colors in use when the picture was last drawn: see term_present. */
static unsigned drawn_generation;

/* where the cursor was last drawn */
static int pcx = -1;
static int pcy = -1;
static int pvis;
static int mapped;
#ifdef HAVE_XFT
static int use_xft;
/* normal, bold, italic, bold italic */
static XftFont *xf[4];
/* fallback for glyphs the mono font lacks */
static XftFont *xf_fb;
/* the small A of the name LaTeX: seven tenths of the size of the text */
static XftFont *xf_small;
static XftDraw *xd;
#endif


/* The entry of the table for the color rgb: it is asked of the server once. */
static int
color_entry(unsigned rgb)
{
    int scr = DefaultScreen(dpy);

    for (int i = 0; i < ncached; i++) {
        if (ccache[i].rgb == rgb) {
            return i;
        }
    }

    int i = ncached;

    if (ncached < COLOR_CACHE) {
        ncached++;
    } else {
        i = cache_next;
        cache_next = (cache_next + 1) % COLOR_CACHE;
    }

    XColor c;

    memset(&c, 0, sizeof c);
    c.red = u16(((rgb >> 16) & 0xFF) * 257);
    c.green = u16(((rgb >> 8) & 0xFF) * 257);
    c.blue = u16((rgb & 0xFF) * 257);
    c.flags = DoRed | DoGreen | DoBlue;
    ccache[i].rgb = rgb;

    if (XAllocColor(dpy, DefaultColormap(dpy, scr), &c) != 0) {
        ccache[i].pixel = c.pixel;
    } else {
        ccache[i].pixel = (rgb & 0x808080) != 0 ? WhitePixel(dpy, scr) :
                                                  BlackPixel(dpy, scr);
    }
#ifdef HAVE_XFT
    XRenderColor xr;

    xr.red = c.red;
    xr.green = c.green;
    xr.blue = c.blue;
    xr.alpha = 0xFFFF;

    if (use_xft != 0) {
        XftColorAllocValue(dpy, DefaultVisual(dpy, scr),
                           DefaultColormap(dpy, scr), &xr, &ccache[i].xft);
    }
#endif

    return i;
}


static unsigned long
pixel_of(unsigned rgb)
{
    return ccache[color_entry(rgb)].pixel;
}


#ifdef HAVE_XFT
static XftColor *
xft_of(unsigned rgb)
{
    return &ccache[color_entry(rgb)].xft;
}
#endif


/* The background color of normal text, which is also the page color. */
static unsigned
normal_bg(void)
{
    return color_active()->pair[CK_NORMAL].bg;
}


static XFontStruct *
load_font(const char *xlfd)
{
    return XLoadQueryFont(dpy, xlfd);
}

#ifdef HAVE_XFT
static int
open_xft(void)
{
    const char *fam = getenv("STRATA_X11_XFT");

    if (fam != NULL && strcmp(fam, "0") == 0) {
        return -1;
    }

    if (fam == NULL || *fam == '\0' || strcmp(fam, "1") == 0) {
        fam = "DejaVu Sans Mono";
    }

    int size = getenv("STRATA_X11_SIZE") != NULL ?
                   atoi(getenv("STRATA_X11_SIZE")) :
                   16;

    if (size < 8) {
        size = 16;
    }

    static const char *style[4] = {"", ":bold", ":italic", ":bold:italic"};
    char name[200];

    for (int i = 0; i < 4; i++) {
        snprintf(name, sizeof name, "%s:pixelsize=%d%s", fam, size, style[i]);
        xf[i] = XftFontOpenName(dpy, DefaultScreen(dpy), name);
    }

    if (xf[0] == NULL) {
        return -1;
    }

    XGlyphInfo gi;

    XftTextExtents8(dpy, xf[0], bytes("M"), 1, &gi);
    cw = gi.xOff;
    asc = xf[0]->ascent;
    chh = xf[0]->ascent + xf[0]->descent;

    if (cw < 4 || chh < 6) {
        return -1;
    }

    /* a style with different metrics would break the grid */
    for (int i = 1; i < 4; i++) {
        if (xf[i] != NULL) {
            XftTextExtents8(dpy, xf[i], bytes("M"), 1, &gi);

            if (gi.xOff != cw) {
                XftFontClose(dpy, xf[i]);
                xf[i] = NULL;
            }
        }
    }

    snprintf(name, sizeof name, "DejaVu Sans:pixelsize=%d", size);
    xf_fb = XftFontOpenName(dpy, DefaultScreen(dpy), name);
    snprintf(name, sizeof name, "%s:pixelsize=%d", fam,
             size * 7 / 10 < 5 ? 5 : size * 7 / 10);
    xf_small = XftFontOpenName(dpy, DefaultScreen(dpy), name);
    use_xft = 1;

    return 0;
}
#endif


static int
open_fonts(void)
{
#ifdef HAVE_XFT
    if (open_xft() == 0) {
        return 0;
    }
#endif

    const char *env = getenv("STRATA_X11_FONT");
    char normal[256];
    char bold[256];

    if (env != NULL && *env != '\0') {
        snprintf(normal, sizeof normal, "%s", env);
        snprintf(bold, sizeof bold, "%s", env);
        char *p = strstr(bold, "medium");

        if (p != NULL) {
            memcpy(p, "bold\0\0", 6);
            memmove(p + 4, p + 6, strlen(p + 6) + 1);
        }
    } else {
        int size = getenv("STRATA_X11_SIZE") != NULL ?
                       atoi(getenv("STRATA_X11_SIZE")) :
                       15;
        const char *dims;

        switch (size) {
            case 13:
                dims = "13-120-75-75-c-80";
                break;

            case 18:
                dims = "18-120-100-100-c-90";
                break;

            case 20:
                dims = "20-200-75-75-c-100";
                break;

            default:
                dims = "15-140-75-75-c-90";
                break;
        }

        snprintf(normal, sizeof normal,
                 "-misc-fixed-medium-r-normal--%s-iso10646-1", dims);
        snprintf(bold, sizeof bold, "-misc-fixed-bold-r-normal--%s-iso10646-1",
                 dims);
    }

    fnt[0] = load_font(normal);

    if (fnt[0] == NULL) {
        fnt[0] = load_font(
            "-misc-fixed-medium-r-normal--15-140-75-75-c-90-iso10646-1");
    }

    if (fnt[0] == NULL) {
        fnt[0] = load_font("fixed");
    }

    if (fnt[0] == NULL) {
        return -1;
    }

    fnt[1] = load_font(bold);

    if (fnt[1] == NULL ||
        fnt[1]->max_bounds.width != fnt[0]->max_bounds.width) {
        fnt[1] = fnt[0];
    }

    cw = fnt[0]->max_bounds.width;
    asc = fnt[0]->ascent;
    chh = fnt[0]->ascent + fnt[0]->descent;

    return 0;
}


/* The part of the picture that has changed since it was last copied. */
static void
mark_dirty(int x, int y, int w, int h)
{
    if (x < dirty_x0) {
        dirty_x0 = x;
    }

    if (y < dirty_y0) {
        dirty_y0 = y;
    }

    if (x + w > dirty_x1) {
        dirty_x1 = x + w;
    }

    if (y + h > dirty_y1) {
        dirty_y1 = y + h;
    }
}


/* Copy the whole picture to the window. */
static void
copy_all(void)
{
    XCopyArea(dpy, pm, win, gc, 0, 0, uns(tb_C * cw), uns(tb_R * chh), 0, 0);
}


/* (Re)create the picture at the size of the screen, filled with the ground. */
static void
make_picture(void)
{
    int scr = DefaultScreen(dpy);

    if (pm != 0) {
        XFreePixmap(dpy, pm);
    }

    pm = XCreatePixmap(dpy, win, uns(tb_C * cw), uns(tb_R * chh),
                       uns(DefaultDepth(dpy, scr)));
    XSetForeground(dpy, gc, pixel_of(normal_bg()));
    XFillRectangle(dpy, pm, gc, 0, 0, uns(tb_C * cw), uns(tb_R * chh));
#ifdef HAVE_XFT
    if (use_xft != 0 && xd != NULL) {
        XftDrawChange(xd, pm);
    }
#endif
    dirty_x0 = INT_MAX;
    dirty_y0 = INT_MAX;
    dirty_x1 = -1;
    dirty_y1 = -1;
}


int
term_open(void)
{
    dpy = XOpenDisplay(NULL);

    if (dpy == NULL) {
        return -1;
    }

    if (open_fonts() != 0) {
        XCloseDisplay(dpy);
        dpy = NULL;
        return -1;
    }

    /* 80x50: like an old SVGA text mode */
    int scr = DefaultScreen(dpy);
    int cols = 80;
    int rows = 50;
    const char *g = getenv("STRATA_X11_GEOM");

    if (g != NULL) {
        sscanf(g, "%dx%d", &cols, &rows);
        geometry_forced = 1;
    } else {
        /* The height that the user kept: at most the rows of the screen. */
        Config cfg;
        int screen_rows = DisplayHeight(dpy, scr) / chh;

        config_load(&cfg);

        if (cfg.x11_rows != 0) {
            rows = config_clamp_rows(cfg.x11_rows, screen_rows);
        }
    }

    saved_rows = rows;

    if (cols < 20) {
        cols = 20;
    }

    if (rows < MIN_ROWS) {
        rows = MIN_ROWS;
    }

    tb_resize(rows, cols);
    color_init(1);
    drawn_generation = color_generation();
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, uns(cols * cw),
                              uns(rows * chh), 0,
                              pixel_of(color_active()->pair[CK_NORMAL].fg),
                              pixel_of(normal_bg()));
    XSelectInput(dpy, win, KeyPressMask | KeyReleaseMask | ExposureMask |
                 StructureNotifyMask | FocusChangeMask);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);
    XSizeHints sh;

    memset(&sh, 0, sizeof sh);
    sh.base_width = 0;
    sh.base_height = 0;

    /*
     * The width is fixed.  The height may be changed in whole rows, from 20
     * rows to as many as the screen holds.
     */
    fixed_cols = cols;
    max_rows = DisplayHeight(dpy, scr) / chh;

    if (max_rows < rows) {
        max_rows = rows;
    }

    sh.flags = PResizeInc | PMinSize | PMaxSize | PBaseSize;
    sh.width_inc = cw;
    sh.height_inc = chh;
    sh.min_width = cols * cw;
    sh.max_width = cols * cw;
    sh.min_height = MIN_ROWS * chh;
    sh.max_height = max_rows * chh;

    XSetWMNormalHints(dpy, win, &sh);
    char cls_name[] = "stred";
    char cls_class[] = "Strata";
    XClassHint ch = {cls_name, cls_class};

    XSetClassHint(dpy, win, &ch);
    XStoreName(dpy, win, "Strata");
    gc = XCreateGC(dpy, win, 0, NULL);
    XSetGraphicsExposures(dpy, gc, False);
    /* The window is never cleared by the server; the picture is copied. */
    XSetWindowBackgroundPixmap(dpy, win, None);
    make_picture();
#ifdef HAVE_XFT
    if (use_xft != 0) {
        xd = XftDrawCreate(dpy, pm, DefaultVisual(dpy, scr),
                           DefaultColormap(dpy, scr));
    }
#endif

    XMapWindow(dpy, win);

    for (;;) {
        XEvent e;

        XNextEvent(dpy, &e);

        if (e.type == MapNotify) {
            break;
        }

        if (e.type == Expose) {
            break;
        }
    }

    mapped = 1;

    return 0;
}


/* Milliseconds from the start of the wait to now. */
static long
waited_ms(void)
{
    struct timeval now;

    gettimeofday(&now, NULL);

    return (now.tv_sec - pending_since.tv_sec) * 1000L +
           (now.tv_usec - pending_since.tv_usec) / 1000L;
}


/* Keeps the rows in the settings.  A failure is logged and is no error. */
static void
save_rows(void)
{
    char err[200];
    int rows = pending_rows;

    pending_rows = 0;

    if (rows == 0 || rows == saved_rows || geometry_forced != 0) {
        return;
    }

    if (config_set_rows(1, rows, max_rows, err, sizeof err) != 0) {
        log_warn("term_x11: the height of the window, %d rows, was not "
                 "kept in the settings: %s", rows, err);

        return;
    }

    saved_rows = rows;
}


/* Saves the height when it has not changed for a while. */
static void
save_rows_settled(void)
{
    if (pending_rows != 0 && waited_ms() >= SAVE_DELAY_MS) {
        save_rows();
    }
}


void
term_close(void)
{
    if (dpy == NULL) {
        return;
    }

    save_rows();
#ifdef HAVE_XFT
    if (use_xft != 0) {
        XftDrawDestroy(xd);

        for (int i = 0; i < 4; i++) {
            if (xf[i] != NULL) {
                XftFontClose(dpy, xf[i]);
            }
        }

        if (xf_fb != NULL) {
            XftFontClose(dpy, xf_fb);
        }

        if (xf_small != NULL) {
            XftFontClose(dpy, xf_small);
        }

        memset(xf, 0, sizeof xf);
        xf_fb = NULL;
        xf_small = NULL;
        xd = NULL;
        use_xft = 0;
    }
#endif

    if (pm != 0) {
        XFreePixmap(dpy, pm);
        pm = 0;
    }

    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    dpy = NULL;
    mapped = 0;
#ifdef HAVE_XFT
    /* Gives back what FcInit (XftInit) made: its tables are not freed
       otherwise, and a leak check of a run of the window version says so. */
    FcFini();
#endif
}


void
term_set_title(const char *t)
{
    if (dpy == NULL) {
        return;
    }

    XStoreName(dpy, win, t);
    XFlush(dpy);
}


int
term_is_gui(void)
{
    return 1;
}


const char *
term_charset_notice(void)
{
    return NULL;
}


int
term_is_ascii(void)
{
    return 0;
}


/* The modifier keys that are down: one bit for each of the keys. */
enum {
    HELD_SHIFT_L = 1,
    HELD_SHIFT_R = 2,
    HELD_CTRL_L = 4,
    HELD_CTRL_R = 8,
    HELD_ALT_L = 16,
    HELD_ALT_R = 32
};

static int held;


int
term_modifiers(void)
{
    int m = 0;

    if ((held & (HELD_SHIFT_L | HELD_SHIFT_R)) != 0) {
        m |= KM_SHIFT;
    }

    if ((held & (HELD_CTRL_L | HELD_CTRL_R)) != 0) {
        m |= KM_CTRL;
    }

    if ((held & (HELD_ALT_L | HELD_ALT_R)) != 0) {
        m |= KM_ALT;
    }

    return m;
}


/*
 * A modifier key went down (down is not 0) or up.  Returns 1 if that
 * changes the modifiers that are held, 0 if the key is not a modifier or
 * nothing changed.
 */
static int
modifier_event(XKeyEvent *ev, int down)
{
    KeySym ks = XLookupKeysym(ev, 0);
    int bit = 0;
    int before = term_modifiers();

    switch (ks) {
        case XK_Shift_L:
            bit = HELD_SHIFT_L;
            break;

        case XK_Shift_R:
            bit = HELD_SHIFT_R;
            break;

        case XK_Control_L:
            bit = HELD_CTRL_L;
            break;

        case XK_Control_R:
            bit = HELD_CTRL_R;
            break;

        case XK_Alt_L:
        case XK_Meta_L:
            bit = HELD_ALT_L;
            break;

        case XK_Alt_R:
        case XK_Meta_R:
            bit = HELD_ALT_R;
            break;

        default:
            return 0;
    }

    if (down != 0) {
        held |= bit;
    } else {
        held &= ~bit;
    }

    return term_modifiers() != before;
}


void
term_beep(void)
{
    if (dpy != NULL) {
        XBell(dpy, 30);
    }
}


/* ----------------------------------------------------------------- drawing */

/* The number of times the screen was presented, for a test that counts the
   drawings that a burst of keys makes. */
static unsigned long dump_draws;


static void
dump_screen(void)
{
    const char *path = getenv("STRATA_X11_DUMP");

    if (path == NULL || *path == '\0') {
        return;
    }

    dump_draws++;

    char tmp[1100];

    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");

    if (f == NULL) {
        return;
    }

    for (int y = 0; y < tb_R; y++) {
        int last = tb_C - 1;

        while (last >= 0 && tb_back[y * tb_C + last].cp == ' ') {
            last--;
        }

        for (int x = 0; x <= last; x++) {
            unsigned cp = tb_back[y * tb_C + x].cp;

            /* The cells of the logo of the name LaTeX are the letters a
               and e for a reader of the dump. */
            cp = cp == TERM_LOGO_A ? 'a' : cp == TERM_LOGO_E ? 'e' : cp;
            utf8_put(f, cp);
        }

        fputc('\n', f);
    }

    fprintf(f,
            "\x01"
            "cursor %d %d %d\n",
            tb_cx, tb_cy, tb_cvis);

    /* The size of the grid, for a test that measures the picture. */
    fprintf(f,
            "\x01"
            "grid %d %d %d %d\n",
            tb_C, tb_R, cw, chh);
    fprintf(f,
            "\x01"
            "draws %lu\n",
            dump_draws);
    fclose(f);
    rename(tmp, path);
}


/*
 * The box of a keycap around the n cells that start at x, y: a thin line
 * with rounded corners (a quarter of the height is round, as in the
 * Preview), in the color of the text.
 */
static void
draw_keycap_box(int x, int y, int n, unsigned long pixel)
{
    int x0 = x * cw + 1;
    int y0 = y * chh + 1;
    int x1 = (x + n) * cw - 2;
    int y1 = (y + 1) * chh - 2;
    int r = (y1 - y0) / 5;

    r = r < 2 ? 2 : r;
    XSetForeground(dpy, gc, pixel);
    XDrawLine(dpy, pm, gc, x0 + r, y0, x1 - r, y0);
    XDrawLine(dpy, pm, gc, x0 + r, y1, x1 - r, y1);
    XDrawLine(dpy, pm, gc, x0, y0 + r, x0, y1 - r);
    XDrawLine(dpy, pm, gc, x1, y0 + r, x1, y1 - r);
    XDrawArc(dpy, pm, gc, x0, y0, uns(2 * r), uns(2 * r), 90 * 64, 90 * 64);
    XDrawArc(dpy, pm, gc, x1 - 2 * r, y0, uns(2 * r), uns(2 * r), 0, 90 * 64);
    XDrawArc(dpy, pm, gc, x0, y1 - 2 * r, uns(2 * r), uns(2 * r), 180 * 64,
             90 * 64);
    XDrawArc(dpy, pm, gc, x1 - 2 * r, y1 - 2 * r, uns(2 * r), uns(2 * r),
             270 * 64, 90 * 64);
}


/*
 * The two letters of the name LaTeX that TeX does not set on the baseline:
 * the A is smaller and raised, and the E is lowered.  The cell is the
 * cell of the letter; the background is already drawn.
 */
#ifdef HAVE_XFT
static void
draw_logo_cell(int x, int y, unsigned cp, XftFont *normal, XftColor *fgc)
{
    FcChar32 c = cp == TERM_LOGO_A ? 'A' : 'E';
    int size = normal->ascent + normal->descent;

    if (cp == TERM_LOGO_A) {
        XftFont *f = xf_small != NULL ? xf_small : normal;
        XGlyphInfo gi;
        int raise = asc / 4 < 2 ? 2 : asc / 4;

        XftTextExtents32(dpy, f, &c, 1, &gi);
        XftDrawString32(xd, fgc, f, x * cw + (cw - gi.xOff) / 2,
                        y * chh + asc - raise, &c, 1);
    } else {
        int lower = size / 5 < 2 ? 2 : size / 5;

        if (asc + lower > chh - 1) {
            lower = chh - 1 - asc;
        }

        XftDrawString32(xd, fgc, normal, x * cw, y * chh + asc + lower,
                        &c, 1);
    }
}
#endif


static void
draw_run(int x, int y, int n, int attr)
{
    mark_dirty(x * cw, y * chh, n * cw, chh);

    CellLook lk;

    color_look(color_active(), attr, &lk);
    unsigned long fgp = pixel_of(lk.fg);
    unsigned long bgp = pixel_of(lk.bg);

#ifdef HAVE_XFT
    if (use_xft != 0) {
        int fi = (lk.bold != 0 ? 1 : 0) | (lk.italic != 0 ? 2 : 0);
        XftFont *f = xf[fi] != NULL ? xf[fi] : xf[0];
        XftColor *fgc = xft_of(lk.fg);

        XftDrawRect(xd, xft_of(lk.bg), x * cw, y * chh, uns(n * cw),
                    uns(chh));

        for (int i = 0; i < n; i++) {
            FcChar32 c = tb_back[y * tb_C + x + i].cp;

            if (c == ' ' || c == 0 || c == TERM_IMAGE_CELL) {
                continue;
            }

            if (c == TERM_LOGO_A || c == TERM_LOGO_E) {
                draw_logo_cell(x + i, y, c, f, fgc);

                continue;
            }

            XftFont *ff =
                XftCharExists(dpy, f, c) != 0 ?
                    f :
                    (xf_fb != NULL && XftCharExists(dpy, xf_fb, c) != 0 ?
                         xf_fb :
                         f);

            XftDrawString32(xd, fgc, ff, (x + i) * cw, y * chh + asc, &c, 1);
        }

        if (lk.under != 0) {
            XftDrawRect(xd, fgc, x * cw, y * chh + asc + 1, uns(n * cw), 1);
        }

        if (lk.key != 0) {
            draw_keycap_box(x, y, n, fgp);
        }

        return;
    }
#endif

    XChar2b *s = malloc(usz(n > 1 ? n : 1) * sizeof *s);

    for (int i = 0; i < n; i++) {
        unsigned cp = tb_back[y * tb_C + x + i].cp;

        if (cp == TERM_IMAGE_CELL) {
            cp = ' ';
        }

        if (cp == TERM_LOGO_A || cp == TERM_LOGO_E) {
            cp = cp == TERM_LOGO_A ? 'A' : 'E';
        }

        s[i].byte1 = u8(iun(cp >> 8));
        s[i].byte2 = u8(iun(cp & 0xFF));
    }

    XSetForeground(dpy, gc, fgp);
    XSetBackground(dpy, gc, bgp);
    XSetFont(dpy, gc, fnt[lk.bold != 0 ? 1 : 0]->fid);
    XDrawImageString16(dpy, pm, gc, x * cw, y * chh + asc, s, n);

    /* The A of the name LaTeX is raised and its E is lowered: the cell is
       drawn again with the letter moved (the same font, no smaller one). */
    for (int i = 0; i < n; i++) {
        unsigned cp = tb_back[y * tb_C + x + i].cp;

        if (cp == TERM_LOGO_A || cp == TERM_LOGO_E) {
            int off = cp == TERM_LOGO_A ? -(asc / 4 < 2 ? 2 : asc / 4) :
                                          (chh - asc > 3 ? 2 : 1);

            XSetForeground(dpy, gc, bgp);
            XFillRectangle(dpy, pm, gc, (x + i) * cw, y * chh, uns(cw),
                           uns(chh));
            XSetForeground(dpy, gc, fgp);
            XDrawString16(dpy, pm, gc, (x + i) * cw, y * chh + asc + off,
                          &s[i], 1);
        }
    }

    if (lk.under != 0) {
        XDrawLine(dpy, pm, gc, x * cw, y * chh + asc + 1, (x + n) * cw - 1,
                  y * chh + asc + 1);
    }

    free(s);

    if (lk.key != 0) {
        draw_keycap_box(x, y, n, fgp);
    }
}


/* ----------------------------------------------------------------- images */
/* The pictures of the last frame, to repair the cells under one that moved. */
static TImage *last_img;
static int last_nimg;


static int
same_image(const TImage *a, const TImage *b)
{
    return a->cx == b->cx && a->cy == b->cy && a->xoff == b->xoff &&
           a->yoff == b->yoff && a->w == b->w && a->h == b->h &&
           a->rgb == b->rgb;
}


/* Cells that a picture covers, as a rectangle of cells. */
static void
image_cells(const TImage *im, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = im->cx;
    *y0 = im->cy;
    *x1 = im->cx + (im->xoff + im->w + cw - 1) / cw;
    *y1 = im->cy + (im->yoff + im->h + chh - 1) / chh;
}


/* A picture that is gone or has moved: its cells must be drawn again. */
static void
forget_images(void)
{
    for (int i = 0; i < last_nimg; i++) {
        int still = 0;

        for (int j = 0; j < tb_nimg; j++) {
            if (same_image(&last_img[i], &tb_img[j]) != 0) {
                still = 1;
            }
        }

        if (still != 0) {
            continue;
        }

        int x0;
        int y0;
        int x1;
        int y1;

        image_cells(&last_img[i], &x0, &y0, &x1, &y1);

        for (int y = y0; y < y1 && y < tb_R; y++) {
            for (int x = x0; x < x1 && x < tb_C; x++) {
                if (y >= 0 && x >= 0) {
                    tb_front[y * tb_C + x].cp = 0xFFFF;
                }
            }
        }
    }
}


/* The cursor box of the last frame, to repair what it covered. */
static TBox last_box;


/* The pixels of a cursor box, with room for its two pixel line. */
static void
box_rect(const TBox *b, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = b->cx * cw + b->xoff - 3;
    *y0 = b->cy * chh + b->yoff - 3;
    *x1 = *x0 + b->w + 6;
    *y1 = *y0 + b->h + 6;
}


static void
forget_box(void)
{
    if (last_box.valid == 0 ||
        (tb_cbox.valid != 0 && memcmp(&last_box, &tb_cbox, sizeof last_box) ==
                                   0)) {
        return;
    }

    int x0;
    int y0;
    int x1;
    int y1;

    box_rect(&last_box, &x0, &y0, &x1, &y1);

    for (int y = y0 / chh; y <= y1 / chh && y < tb_R; y++) {
        for (int x = x0 / cw; x <= x1 / cw && x < tb_C; x++) {
            if (x >= 0 && y >= 0) {
                tb_front[y * tb_C + x].cp = 0xFFFF;
            }
        }
    }
}


/* Paint the pictures over the cells that are still free for them. */
static void
paint_images(void)
{
    for (int i = 0; i < tb_nimg; i++) {
        const TImage *im = &tb_img[i];
        int x0;
        int y0;
        int x1;
        int y1;

        image_cells(im, &x0, &y0, &x1, &y1);

        char *data = malloc(usz(im->w) * usz(im->h) * 4);

        if (data == NULL) {
            continue;
        }

        int lsb = ImageByteOrder(dpy) == LSBFirst;

        for (int k = 0; k < im->w * im->h; k++) {
            char *o = data + 4 * k;

            if (lsb != 0) {
                o[0] = chr(im->rgb[3 * k + 2]);
                o[1] = chr(im->rgb[3 * k + 1]);
                o[2] = chr(im->rgb[3 * k]);
                o[3] = 0;
            } else {
                o[0] = 0;
                o[1] = chr(im->rgb[3 * k]);
                o[2] = chr(im->rgb[3 * k + 1]);
                o[3] = chr(im->rgb[3 * k + 2]);
            }
        }

        XImage *xi = XCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)),
                                  uns(DefaultDepth(dpy, DefaultScreen(dpy))),
                                  ZPixmap, 0, data, uns(im->w), uns(im->h),
                                  32, 0);

        for (int y = y0; y < y1 && y < tb_R; y++) {
            for (int x = x0; x < x1 && x < tb_C; x++) {
                if (y < 0 || x < 0 ||
                    tb_back[y * tb_C + x].cp != TERM_IMAGE_CELL) {
                    continue;
                }

                int px0 = x * cw;
                int py0 = y * chh;
                int ix = px0 - (im->cx * cw + im->xoff);
                int iy = py0 - (im->cy * chh + im->yoff);
                int sx = ix < 0 ? 0 : ix;
                int sy = iy < 0 ? 0 : iy;
                int dx = px0 + (sx - ix);
                int dy = py0 + (sy - iy);
                int w = cw - (sx - ix);
                int h = chh - (sy - iy);

                if (sx + w > im->w) {
                    w = im->w - sx;
                }

                if (sy + h > im->h) {
                    h = im->h - sy;
                }

                XSetForeground(dpy, gc, pixel_of(normal_bg()));
                XFillRectangle(dpy, pm, gc, px0, py0, uns(cw), uns(chh));

                if (w > 0 && h > 0) {
                    XPutImage(dpy, pm, gc, xi, sx, sy, dx, dy, uns(w),
                              uns(h));
                }

                mark_dirty(px0, py0, cw, chh);
            }
        }

        xi->data = NULL;
        XDestroyImage(xi);
        free(data);
    }

    /* The list is kept as it is; when it cannot be copied the next frame
       repairs nothing under a moved picture. */
    TImage *kept = realloc(last_img, usz(tb_nimg > 0 ? tb_nimg : 1) *
                                         sizeof *kept);

    if (kept != NULL) {
        last_img = kept;

        if (tb_nimg > 0) {
            memcpy(last_img, tb_img, usz(tb_nimg) * sizeof *last_img);
        }

        last_nimg = tb_nimg;
    } else {
        last_nimg = 0;
    }
}


int
term_gui_info(int *cell_w, int *cell_h, unsigned *fg, unsigned *bg)
{
    if (dpy == NULL) {
        return 0;
    }

    *cell_w = cw;
    *cell_h = chh;
    *fg = color_active()->pair[CK_NORMAL].fg;
    *bg = normal_bg();

    return 1;
}


void
term_present(void)
{
    if (dpy == NULL) {
        return;
    }

    /* The colors changed: the background is made again and every cell. */
    if (drawn_generation != color_generation()) {
        drawn_generation = color_generation();
        make_picture();
        tb_invalidate();
        forget_images();
        mark_dirty(0, 0, tb_C * cw, tb_R * chh);
    }

    forget_images();
    forget_box();

    int cvis = tb_cvis != 0 && tb_cbox.valid == 0;

    if (pvis != 0 && (pcx != tb_cx || pcy != tb_cy || cvis == 0) &&
        pcx >= 0 && pcy >= 0 && pcx < tb_C && pcy < tb_R) {
        tb_front[pcy * tb_C + pcx].cp = 0xFFFF;
    }

    for (int y = 0; y < tb_R; y++) {
        int x = 0;

        while (x < tb_C) {
            TC *b = &tb_back[y * tb_C + x];
            TC *f = &tb_front[y * tb_C + x];

            if (b->cp == f->cp && b->attr == f->attr) {
                x++;
                continue;
            }

            int a = iun(b->attr);
            int n = 0;

            while (x + n < tb_C) {
                TC *bb = &tb_back[y * tb_C + x + n];
                TC *ff = &tb_front[y * tb_C + x + n];

                if (iun(bb->attr) != a ||
                    (bb->cp == ff->cp && bb->attr == ff->attr)) {
                    break;
                }

                n++;
            }

            draw_run(x, y, n, a);

            for (int i = 0; i < n; i++) {
                tb_front[y * tb_C + x + i] = tb_back[y * tb_C + x + i];
            }

            x += n;
        }
    }

    paint_images();

    if (tb_cbox.valid != 0) {
        int x0;
        int y0;
        int x1;
        int y1;

        box_rect(&tb_cbox, &x0, &y0, &x1, &y1);
        XSetForeground(dpy, gc, pixel_of(color_active()->pair[CK_CURSOR].bg));
        XSetLineAttributes(dpy, gc, 2, LineSolid, CapButt, JoinMiter);
        XDrawRectangle(dpy, pm, gc, x0 + 1, y0 + 1, uns(x1 - x0 - 2),
                       uns(y1 - y0 - 2));
        XSetLineAttributes(dpy, gc, 0, LineSolid, CapButt, JoinMiter);
        mark_dirty(x0, y0, x1 - x0, y1 - y0);
    }

    if (cvis != 0 && tb_cx >= 0 && tb_cy >= 0 && tb_cx < tb_C &&
        tb_cy < tb_R) {
        draw_run(tb_cx, tb_cy, 1,
                 iun(tb_back[tb_cy * tb_C + tb_cx].attr & 0xFF) | TS_CURSOR);
    }

    /* Show what changed, all at once. */
    if (dirty_x1 > dirty_x0 && dirty_y1 > dirty_y0) {
        XCopyArea(dpy, pm, win, gc, dirty_x0, dirty_y0,
                  uns(dirty_x1 - dirty_x0), uns(dirty_y1 - dirty_y0), dirty_x0,
                  dirty_y0);
    }

    dirty_x0 = INT_MAX;
    dirty_y0 = INT_MAX;
    dirty_x1 = -1;
    dirty_y1 = -1;

    pcx = tb_cx;
    pcy = tb_cy;
    pvis = cvis;
    last_box = tb_cbox;
    XFlush(dpy);
    dump_screen();
}


/* ------------------------------------------------------------------- input */
static int
latin1_to_437(unsigned c)
{
    for (int i = 0x80; i < 256; i++) {
        if (cp437_uni[i] == c) {
            return i;
        }
    }

    return -1;
}


static int
map_key(XKeyEvent *ev)
{
    char buf[16];
    KeySym ks;
    int n = XLookupString(ev, buf, sizeof buf, &ks, NULL);
    int m = 0;

    if ((ev->state & ShiftMask) != 0) {
        m |= KM_SHIFT;
    }

    if ((ev->state & ControlMask) != 0) {
        m |= KM_CTRL;
    }

    if ((ev->state & Mod1Mask) != 0) {
        m |= KM_ALT;
    }

    switch (ks) {
        case XK_Up:
        case XK_KP_Up:
            return K_UP | m;

        case XK_Down:
        case XK_KP_Down:
            return K_DOWN | m;

        case XK_Left:
        case XK_KP_Left:
            return K_LEFT | m;

        case XK_Right:
        case XK_KP_Right:
            return K_RIGHT | m;

        case XK_Home:
        case XK_KP_Home:
            return K_HOME | m;

        case XK_End:
        case XK_KP_End:
            return K_END | m;

        case XK_Prior:
        case XK_KP_Prior:
            return K_PGUP | m;

        case XK_Next:
        case XK_KP_Next:
            return K_PGDN | m;

        case XK_Insert:
        case XK_KP_Insert:
            return K_INS | m;

        case XK_Delete:
        case XK_KP_Delete:
            return K_DEL | m;

        case XK_Escape:
            return K_ESC;

        case XK_BackSpace:
            return K_BKSP | (m & KM_ALT);

        case XK_ISO_Left_Tab:
            return K_BTAB;

        case XK_Tab:
            return (m & KM_SHIFT) != 0 ? K_BTAB : 9;

        case XK_Return:
        case XK_KP_Enter:
            return 13;

        /* the Keypad + - * keys (outline control) */
        case XK_KP_Add:
            return '+' | KM_ALT;

        case XK_KP_Subtract:
            return '-' | KM_ALT;

        case XK_KP_Multiply:
            return '*' | KM_ALT;

        case XK_Shift_L:
        case XK_Shift_R:
        case XK_Control_L:
        case XK_Control_R:
        case XK_Alt_L:
        case XK_Alt_R:
        case XK_Meta_L:
        case XK_Meta_R:
        case XK_Caps_Lock:
        case XK_Num_Lock:
        case XK_Scroll_Lock:
        case XK_Super_L:
        case XK_Super_R:
        case XK_ISO_Level3_Shift:
        case XK_Mode_switch:
            return K_NONE;

        default:
            break;
    }

    if (ks >= XK_F1 && ks <= XK_F12) {
        return (K_F(1) + isz(ks - XK_F1)) | m;
    }

    /* Ctrl-Shift-Space is the no-break space.  Ctrl-Space does nothing. */
    if (ks == XK_space && (m & KM_CTRL) != 0 && (m & KM_ALT) == 0) {
        if ((m & KM_SHIFT) != 0) {
            return K_NBSP;
        }

        return K_NONE;
    }

    /*
     * Ctrl-/ is Undo, the same key as in a terminal (the byte 1F).  Xlib
     * gives it for a US layout; another layout may give nothing, so the
     * keysym decides.
     */
    if (ks == XK_slash && (m & KM_CTRL) != 0 && (m & KM_ALT) == 0) {
        return 0x1F;
    }

    if (n >= 1) {
        uint8_t c = u8(buf[0]);

        if ((m & KM_ALT) != 0) {
            return (c < 0x20) ? ((c + 0x60) | KM_ALT | KM_CTRL) : (c | KM_ALT);
        }

        if (c >= 0xA0) {
            int k = latin1_to_437(c);

            return k < 0 ? K_NONE : k;
        }

        if (c == 0x7f) {
            return K_DEL;
        }

        return c;
    }

    return K_NONE;
}

/* handle one pending event; returns a key, K_RESIZE, K_NONE (event
   consumed), or -2 when none pending */


static int
handle_event(void)
{
    if (XPending(dpy) == 0) {
        return -2;
    }

    XEvent e;

    XNextEvent(dpy, &e);

    switch (e.type) {
        case Expose:
            /* Moving or uncovering the window sends many of these; merge
               them and put the picture back with one copy. */
            while (XCheckTypedWindowEvent(dpy, win, Expose, &e)) {
            }

            copy_all();
            XFlush(dpy);

            return K_NONE;

        case ConfigureNotify: {
            int rows = e.xconfigure.height / chh;

            /* A change of the width is refused: ask for the width back. */
            if (e.xconfigure.width != fixed_cols * cw) {
                XResizeWindow(dpy, win, uns(fixed_cols * cw),
                              uns(e.xconfigure.height));
            }

            if (rows < MIN_ROWS) {
                rows = MIN_ROWS;
            }

            if (rows > max_rows) {
                rows = max_rows;
            }

            if (rows != tb_R) {
                tb_resize(rows, fixed_cols);
                make_picture();
                pending_rows = rows;
                gettimeofday(&pending_since, NULL);

                return K_RESIZE;
            }

            return K_NONE;
        }

        case KeyPress:
            if (modifier_event(&e.xkey, 1) != 0) {
                return K_RESIZE;
            }

            return map_key(&e.xkey);

        case KeyRelease:
            if (modifier_event(&e.xkey, 0) != 0) {
                return K_RESIZE;
            }

            return K_NONE;

        case FocusOut:
            /* The release of a key is not seen when the focus is gone. */
            if (held != 0) {
                held = 0;

                return K_RESIZE;
            }

            return K_NONE;

        case ClientMessage:
            /* window close = Ctrl-Q */
            if (lsz(e.xclient.data.l[0]) == wm_delete) {
                return 17;
            }
            return K_NONE;

        case MappingNotify:
            XRefreshKeyboardMapping(&e.xmapping);
            return K_NONE;

        default:
            break;
    }

    return K_NONE;
}


int
term_raw_pending(void)
{
    return XPending(dpy) > 0 ? 1 : 0;
}


int
term_raw_key_pending(void)
{
    XEvent ev;

    if (XCheckTypedEvent(dpy, KeyPress, &ev) == 0) {
        return 0;
    }

    XPutBackEvent(dpy, &ev);

    return 1;
}


int
term_getkey_raw(int timeout_ms)
{
    int fd = ConnectionNumber(dpy);

    for (;;) {
        int k;

        while ((k = handle_event()) != -2) {
            if (k != K_NONE) {
                return k;
            }
        }

        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(fd, &rd);
        struct timeval tv;
        struct timeval *tp = NULL;

        int early = 0;

        save_rows_settled();

        if (timeout_ms >= 0) {
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            tp = &tv;
        }

        if (pending_rows != 0) {
            /* Wake up when the height has settled. */
            long left = SAVE_DELAY_MS - waited_ms();

            if (left < 1) {
                left = 1;
            }

            if (timeout_ms < 0 || timeout_ms > left) {
                tv.tv_sec = left / 1000;
                tv.tv_usec = (left % 1000) * 1000;
                tp = &tv;
                early = 1;
            }
        }

        int r = select(fd + 1, &rd, NULL, NULL, tp);

        if (r == 0 && early != 0) {
            continue;
        }

        if (r == 0) {
            return K_NONE;
        }

        if (r < 0) {
            continue;
        }
    }
}


void
term_idle(void)
{
    if (dpy == NULL) {
        return;
    }

    while (handle_event() != -2) {
        /* drain every pending event */
    }

    save_rows_settled();
}
