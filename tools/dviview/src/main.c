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
 * dviview: shows the pages of a DVI file in an X11 window.
 */
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "dviview.h"

#define ZOOM_MIN 25
#define ZOOM_MAX 800
#define ZOOM_STEP 25
#define SCROLL_STEP 60

typedef struct {
    Display *dpy;
    Window win;
    GC gc;
    XImage *img;
    char *buf;
    int rb;
    int gb;
    int bb;
    long ww;
    long wh;
    long tw;
    long th;
    Atom wm_delete;
    XFontStruct *font;
    GC bar_gc;
} Ui;

static Dvi *dvi;
static ItemList items;
static const char *file_name;
static long page = 1;
static long dpi = 100;
static long zoom = 100;
static long scroll_x;
static long scroll_y;
static int timing;
static int plain;

/* The colour of the status area of a failed print. */
#define STATUS_RED 0xc00000UL
static int show_legend = 1;
static int entering;
static char entry[16];
static int entry_len;


static double
eff_dpi(void)
{
    return dbl(dpi) * dbl(zoom) / 100.0;
}


/* The page as shown: width and height swap when the page is turned. */
static long
page_w(void)
{
    double w = ceil(items.paper_w * eff_dpi());
    double h = ceil(items.paper_h * eff_dpi());

    return round_long(items.rotate == 90 || items.rotate == 270 ? h : w);
}


static long
page_h(void)
{
    double w = ceil(items.paper_w * eff_dpi());
    double h = ceil(items.paper_h * eff_dpi());

    return round_long(items.rotate == 90 || items.rotate == 270 ? w : h);
}


static long
max_scroll(long page_px, long win_px)
{
    return page_px > win_px ? page_px - win_px : 0;
}


static long
clamp(long v, long lo, long hi)
{
    if (v > hi) {
        v = hi;
    }

    if (v < lo) {
        v = lo;
    }

    return v;
}


static void
clamp_scroll(const Ui *ui)
{
    scroll_x = clamp(scroll_x, 0, max_scroll(page_w(), ui->ww));
    scroll_y = clamp(scroll_y, 0, max_scroll(page_h(), ui->wh));
}


/*
 * The title of the window, in UTF-8, as the name of a file is: the property
 * _NET_WM_NAME has it whatever the locale is, and WM_NAME is set for the
 * window managers that do not read that one.
 */
static void
put_title(const Ui *ui, const char *title)
{
    unsigned char copy[700];
    int n = 0;

    while (title[n] != '\0' && n < 699) {
        copy[n] = (unsigned char)title[n];
        n++;
    }

    copy[n] = 0;
    XStoreName(ui->dpy, ui->win, title);
    XChangeProperty(ui->dpy, ui->win, XInternAtom(ui->dpy, "_NET_WM_NAME",
                                                  False),
                    XInternAtom(ui->dpy, "UTF8_STRING", False), 8,
                    PropModeReplace, copy, n);
}


static void
set_title(const Ui *ui)
{
    const char *base = strrchr(file_name, '/');

    base = base != NULL ? base + 1 : file_name;

    if (plain != 0) {
        char t[600];

        snprintf(t, sizeof t, "%s - page %ld/%ld - %ld%%", base, page,
                 dvi->npages, zoom);

        /* The tests read the work of the font maker from the title. */
        if (mk_status() != NULL) {
            char u[900];

            snprintf(u, sizeof u, "%s - %s", t, mk_status());
            put_title(ui, u);
        } else {
            put_title(ui, t);
        }
    } else {
        put_title(ui, base);
    }
}


/*
 * The height of the bar: one line of status and optionally one of keys.  The
 * variable DVIVIEW_PLAIN has no bar and puts the page in the title, which the
 * tests of the pictures use.
 */
static long
bar_height(const Ui *ui)
{
    if (plain != 0) {
        return 0;
    }

    long line = (ui->font->ascent + ui->font->descent) + 4;
    long lines = show_legend != 0 ? 2 : 1;

    /* A failed print has a red line of its own above the status. */
    if (dvi->failed != 0) {
        lines++;
    }

    return lines * line;
}


static void
bar_text(const Ui *ui, long x, long line, const char *t)
{
    long lh = (ui->font->ascent + ui->font->descent) + 4;

    /* The red line of a failed print is the first line of the bar. */
    if (dvi->failed != 0) {
        line++;
    }

    XDrawString(ui->dpy, ui->win, ui->bar_gc, to_int(x),
                to_int(ui->wh + line * lh + 2 + ui->font->ascent), t,
                to_int(to_long(strlen(t))));
}


/* Draws the status bar below the page. */
static void
draw_bar(const Ui *ui)
{
    char t[200];
    long bh = bar_height(ui);

    if (bh == 0) {
        return;
    }

    XSetForeground(ui->dpy, ui->bar_gc, 0xe0e0e0UL);
    XFillRectangle(ui->dpy, ui->win, ui->bar_gc, 0, to_int(ui->wh),
                   to_uint(ui->ww), to_uint(bh));
    XSetForeground(ui->dpy, ui->bar_gc, 0x808080UL);
    XDrawLine(ui->dpy, ui->win, ui->bar_gc, 0, to_int(ui->wh),
              to_int(ui->ww), to_int(ui->wh));
    XSetForeground(ui->dpy, ui->bar_gc, 0x000000UL);

    if (dvi->failed != 0) {
        long lh = (ui->font->ascent + ui->font->descent) + 4;

        XSetForeground(ui->dpy, ui->bar_gc, STATUS_RED);
        XFillRectangle(ui->dpy, ui->win, ui->bar_gc, 0, to_int(ui->wh),
                       to_uint(ui->ww), to_uint(lh));
        XSetForeground(ui->dpy, ui->bar_gc, 0xffffffUL);
        snprintf(t, sizeof t, "PRINT FAILED - %ld error%s - THIS PREVIEW IS "
                 "INACCURATE", dvi->failed, dvi->failed == 1 ? "" : "s");

        /* The text is drawn twice, one pixel apart, to make it bold. */
        XDrawString(ui->dpy, ui->win, ui->bar_gc, 6,
                    to_int(ui->wh + 2 + ui->font->ascent), t,
                    to_int(to_long(strlen(t))));
        XDrawString(ui->dpy, ui->win, ui->bar_gc, 7,
                    to_int(ui->wh + 2 + ui->font->ascent), t,
                    to_int(to_long(strlen(t))));
        XSetForeground(ui->dpy, ui->bar_gc, 0x000000UL);
    }

    if (entering != 0) {
        snprintf(t, sizeof t, "Go to page (1-%ld): %s_   Enter goes, Esc "
                 "cancels", dvi->npages, entry);
    } else {
        snprintf(t, sizeof t, "Page %ld of %ld   Zoom %ld%%   F1 or ? %s keys",
                 page, dvi->npages, zoom,
                 show_legend != 0 ? "hides" : "shows");
    }

    /* While a bitmap font is made (mkfont.c) the bar shows so. */
    bar_text(ui, 6, 0, t);

    if (entering == 0 && mk_status() != NULL) {
        long w = XTextWidth(ui->font, t, to_int(to_long(strlen(t))));

        bar_text(ui, 6 + w + 24, 0, mk_status());
    }

    if (show_legend != 0) {
        static const char *const legend[] = {
            "n PgDn Space: next   p PgUp: previous   Home End: first last   "
            "g: go to page   + -: zoom   0: 100%   arrows: pan   q: quit",
            "n p: page   g: go to page   + - 0: zoom   arrows: pan   q: quit",
            "n p: page   g: go to   + - 0: zoom   q: quit",
            "g: go to   q: quit"
        };
        size_t k = 0;

        while (k + 1 < sizeof legend / sizeof legend[0] &&
               XTextWidth(ui->font, legend[k],
                          to_int(to_long(strlen(legend[k])))) + 12 > ui->ww) {
            k++;
        }

        bar_text(ui, 6, 1, legend[k]);
    }
}


static void
render(Ui *ui)
{
    Canvas cv;
    View v;
    struct timespec t0;
    struct timespec t1;

    cv.buf = ui->buf;
    cv.w = ui->ww;
    cv.h = ui->wh;
    cv.rb = ui->rb;
    cv.gb = ui->gb;
    cv.bb = ui->bb;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    view_make(&v, dvi, &items, eff_dpi(), ui->ww, ui->wh, scroll_x, scroll_y);
    render_page(&cv, &items, &v);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    if (timing != 0) {
        if (strata_log_detail) {
            log_debug("dviview: page %ld has %ld items, zoom %ld%%, drawn in "
                      "%.1f ms",
                      page, items.n, zoom,
                      dbl(t1.tv_sec - t0.tv_sec) * 1000.0 +
                      dbl(t1.tv_nsec - t0.tv_nsec) / 1e6);
        }
    }

    XPutImage(ui->dpy, ui->win, ui->gc, ui->img, 0, 0, 0, 0,
              to_uint(ui->ww), to_uint(ui->wh));
    draw_bar(ui);
}


static int
make_image(Ui *ui, long w, long h)
{
    Visual *vis = DefaultVisual(ui->dpy, DefaultScreen(ui->dpy));

    if (ui->img != NULL) {
        ui->img->data = NULL;
        XDestroyImage(ui->img);
    }

    free(ui->buf);
    ui->tw = w;
    ui->th = h;
    h = h - bar_height(ui);

    if (h < 50) {
        h = 50;
    }

    ui->ww = w;
    ui->wh = h;
    ui->buf = xcalloc(to_size(w * h), 4);
    ui->img = XCreateImage(ui->dpy, vis,
                           to_uint(DefaultDepth(ui->dpy,
                                                DefaultScreen(ui->dpy))),
                           ZPixmap, 0, ui->buf, to_uint(w), to_uint(h), 32, 0);

    if (ui->img == NULL || ui->img->bits_per_pixel != 32) {
        viewer_tell("fail", "a 32 bits per pixel display is needed");
        return -1;
    }

    ui->rb = ui->img->byte_order == LSBFirst ? 2 : 1;
    ui->gb = ui->img->byte_order == LSBFirst ? 1 : 2;
    ui->bb = ui->img->byte_order == LSBFirst ? 0 : 3;

    if (vis->red_mask != 0xff0000UL || vis->green_mask != 0xff00UL ||
        vis->blue_mask != 0xffUL) {
        viewer_tell("fail", "the pixel format of the display is not "
                    "supported: the Viewer needs 8 bits each of red, green "
                    "and blue");
        return -1;
    }

    return 0;
}


static void
load_page(void)
{
    dvi_page(dvi, page, &items);
}


/* Changes the zoom and keeps the center of the window where it was. */
static void
set_zoom(const Ui *ui, long z)
{
    double old = eff_dpi();
    double cx = (dbl(scroll_x) + dbl(ui->ww) / 2.0) / old;
    double cy = (dbl(scroll_y) + dbl(ui->wh) / 2.0) / old;

    z = clamp(z, ZOOM_MIN, ZOOM_MAX);

    if (z == zoom) {
        return;
    }

    zoom = z;
    glyph_flush();
    image_flush();
    scroll_x = round_long(cx * eff_dpi() - dbl(ui->ww) / 2.0);
    scroll_y = round_long(cy * eff_dpi() - dbl(ui->wh) / 2.0);
}


static void
goto_page(long p)
{
    p = clamp(p, 1, dvi->npages);

    if (p != page) {
        page = p;
        scroll_x = 0;
        scroll_y = 0;
        load_page();
    }
}


/* Handles a key while the number of a page is typed in the bar. */
static int
handle_entry(const Ui *ui, KeySym ks, const char *buf)
{
    if (ks == XK_Escape) {
        entering = 0;
    } else if (ks == XK_Return || ks == XK_KP_Enter) {
        entering = 0;

        if (entry_len > 0) {
            goto_page(atol(entry));
            clamp_scroll(ui);
        }
    } else if (ks == XK_BackSpace) {
        if (entry_len > 0) {
            entry_len--;
            entry[entry_len] = '\0';
        }
    } else if (buf[0] >= '0' && buf[0] <= '9' && entry_len < 8) {
        entry[entry_len] = buf[0];
        entry_len++;
        entry[entry_len] = '\0';
    }

    return 1;
}


/* Returns 1 when the window must be redrawn, -1 to quit, 0 otherwise. */
static int
handle_key(Ui *ui, XKeyEvent *ke)
{
    char buf[16];
    KeySym ks = NoSymbol;

    XLookupString(ke, buf, sizeof buf, &ks, NULL);

    if (entering != 0) {
        return handle_entry(ui, ks, buf);
    }

    switch (ks) {
    case XK_q:
    case XK_Q:
    case XK_Escape:
        return -1;
    case XK_Next:
    case XK_space:
    case XK_n:
    case XK_N:
        goto_page(page + 1);
        break;
    case XK_Prior:
    case XK_p:
    case XK_P:
        goto_page(page - 1);
        break;
    case XK_g:
    case XK_G:
        entering = 1;
        entry[0] = '\0';
        entry_len = 0;
        break;
    case XK_F1:
    case XK_question:
        show_legend = show_legend == 0;
        make_image(ui, ui->tw, ui->th);
        break;
    case XK_Home:
        goto_page(1);
        break;
    case XK_End:
        goto_page(dvi->npages);
        break;
    case XK_Up:
        scroll_y -= SCROLL_STEP;
        break;
    case XK_Down:
        scroll_y += SCROLL_STEP;
        break;
    case XK_Left:
        scroll_x -= SCROLL_STEP;
        break;
    case XK_Right:
        scroll_x += SCROLL_STEP;
        break;
    case XK_KP_Add:
    case XK_plus:
    case XK_equal:
        set_zoom(ui, zoom + ZOOM_STEP);
        break;
    case XK_KP_Subtract:
    case XK_minus:
        set_zoom(ui, zoom - ZOOM_STEP);
        break;
    case XK_KP_Multiply:
    case XK_0:
    case XK_KP_0:
        set_zoom(ui, 100);
        break;
    default:
        return 0;
    }

    clamp_scroll(ui);
    set_title(ui);

    return 1;
}


static int
parse_args(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            page = atol(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            dpi = atol(argv[++i]);
        } else if (strcmp(argv[i], "--report") == 0 && i + 1 < argc) {
            viewer_report_to(argv[++i]);
        } else if ((strcmp(argv[i], "--var") == 0 ||
                    strcmp(argv[i], "--special") == 0) && i + 2 < argc) {
            /*
             * The Viewer shows pages that are already made and has no use
             * for variables; it takes the options so that every program
             * that str starts can be given the same ones.
             */
            i += 2;
        } else if (strcmp(argv[i], "--") == 0 && i + 1 < argc &&
                   file_name == NULL) {
            /* After two minus signs comes the file, whatever it begins
               with. */
            file_name = argv[++i];
        } else if (argv[i][0] != '-' && file_name == NULL) {
            file_name = argv[i];
        } else {
            return -1;
        }
    }

    if (file_name == NULL || dpi < 10 || dpi > 2000) {
        return -1;
    }

    return 0;
}


/* Opens the window sized to the page, but no larger than the screen. */
static int
open_window(Ui *ui)
{
    int scr;
    long w;
    long h;
    XSizeHints hints;

    ui->dpy = XOpenDisplay(NULL);

    if (ui->dpy == NULL) {
        viewer_tell("fail", "cannot open the X display: DISPLAY is %s",
                    getenv("DISPLAY") != NULL ? getenv("DISPLAY") : "not set");

        return -1;
    }

    scr = DefaultScreen(ui->dpy);
    ui->font = XLoadQueryFont(ui->dpy, "9x15");

    if (ui->font == NULL) {
        ui->font = XLoadQueryFont(ui->dpy, "fixed");
    }

    if (ui->font == NULL) {
        viewer_tell("fail", "no font for the status bar: the X server has "
                    "neither 9x15 nor fixed");

        return -1;
    }

    w = clamp(page_w(), 100, DisplayWidth(ui->dpy, scr));
    h = clamp(page_h() + bar_height(ui), 100, DisplayHeight(ui->dpy, scr));
    ui->win = XCreateSimpleWindow(ui->dpy, RootWindow(ui->dpy, scr), 0, 0,
                                  to_uint(w), to_uint(h), 0,
                                  BlackPixel(ui->dpy, scr),
                                  WhitePixel(ui->dpy, scr));
    XSelectInput(ui->dpy, ui->win, KeyPressMask | ButtonPressMask |
                 ExposureMask | StructureNotifyMask);
    memset(&hints, 0, sizeof hints);
    hints.flags = PMinSize;
    hints.min_width = 100;
    hints.min_height = 100;
    XSetWMNormalHints(ui->dpy, ui->win, &hints);
    ui->wm_delete = XInternAtom(ui->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ui->dpy, ui->win, &ui->wm_delete, 1);
    ui->gc = XCreateGC(ui->dpy, ui->win, 0, NULL);
    ui->bar_gc = XCreateGC(ui->dpy, ui->win, 0, NULL);
    XSetFont(ui->dpy, ui->bar_gc, ui->font->fid);

    if (make_image(ui, w, h) != 0) {
        return -1;
    }

    set_title(ui);
    XMapWindow(ui->dpy, ui->win);

    return 0;
}


/*
 * A press of a wheel button scrolls the page by the step of the arrow keys.
 * The Viewer has no key that scrolls by half a screen, so the Ctrl key
 * changes nothing here.  Returns 1 when the window must be redrawn.
 */
static int
handle_wheel(Ui *ui, const XButtonEvent *be)
{
    if (be->button == Button4) {
        scroll_y -= SCROLL_STEP;
    } else if (be->button == Button5) {
        scroll_y += SCROLL_STEP;
    } else {
        return 0;
    }

    clamp_scroll(ui);
    set_title(ui);

    return 1;
}


/* Handles one event; returns 1 to redraw, -1 to quit, 0 to do nothing. */
static int
handle_event(Ui *ui, XEvent *ev)
{
    switch (ev->type) {
    case KeyPress:
        return handle_key(ui, &ev->xkey);
    case ButtonPress:
        return handle_wheel(ui, &ev->xbutton);
    case Expose:
        return 1;
    case ConfigureNotify:
        if (ev->xconfigure.width != ui->tw ||
            ev->xconfigure.height != ui->th) {
            if (make_image(ui, ev->xconfigure.width,
                           ev->xconfigure.height) != 0) {
                return -1;
            }

            clamp_scroll(ui);

            return 1;
        }

        return 0;
    case ClientMessage:
        return ev->xclient.data.l[0] == to_long(ui->wm_delete) ? -1 : 0;
    default:
        return 0;
    }
}


/* Removes the temporary pictures when the program is stopped. */
static void
on_signal(int sig)
{
    image_cleanup();
    mk_stop();
    _exit(128 + sig);
}


int
main(int argc, char **argv)
{
    Ui ui;
    int quit = 0;
    int dirty = 1;

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("dviview (Strata) %s\n", strata_version);
        printf("file format %d\n", STRATA_FILE_FORMAT);

        return 0;
    }

    log_open("dviview");

    if (parse_args(argc, argv) != 0) {
        fprintf(stderr, "usage: dviview FILE.dvi [-p PAGE] [-d DPI] "
                "[--report FILE] [--var NAME VALUE]\n");

        return 2;
    }

    timing = getenv("DVIVIEW_TIMING") != NULL;
    plain = getenv("DVIVIEW_PLAIN") != NULL;
    atexit(image_cleanup);
    atexit(mk_stop);
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGHUP, on_signal);
    dvi = dvi_open(file_name);

    if (dvi == NULL) {
        return 1;
    }

    page = clamp(page, 1, dvi->npages);
    load_page();
    memset(&ui, 0, sizeof ui);

    if (open_window(&ui) != 0) {
        return 1;
    }

    while (!quit) {
        XEvent ev;

        /*
         * While fonts are made, wait for an event for a tenth of a second
         * at most, look at the jobs, and build the page again when a font
         * has its bitmap.  The window answers all the time.
         */
        if (mk_active()) {
            struct pollfd pf;

            pf.fd = ConnectionNumber(ui.dpy);
            pf.events = POLLIN;
            pf.revents = 0;

            if (XPending(ui.dpy) == 0) {
                XFlush(ui.dpy);
                poll(&pf, 1, 100);
            }

            if (mk_poll() > 0) {
                glyph_flush();
                load_page();
                dirty = 1;
            }

            if (!mk_active()) {
                dirty = 1;
            }

            /* The title tells the work (and that it is over). */
            set_title(&ui);

            if (XPending(ui.dpy) == 0) {
                if (dirty) {
                    render(&ui);
                    dirty = 0;
                }

                continue;
            }
        }

        /* Handle all waiting events first, then draw once. */
        do {
            int r;

            XNextEvent(ui.dpy, &ev);
            r = handle_event(&ui, &ev);

            if (r < 0) {
                quit = 1;
            } else if (r > 0) {
                dirty = 1;
            }
        } while (!quit && XPending(ui.dpy) > 0);

        if (dirty && !quit) {
            render(&ui);
            dirty = 0;
        }
    }

    XFreeGC(ui.dpy, ui.gc);
    ui.img->data = NULL;
    XDestroyImage(ui.img);
    free(ui.buf);
    XCloseDisplay(ui.dpy);

    return 0;
}
