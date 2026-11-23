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
 * xclose - send the message that a window manager sends when its close
 * button is pressed (WM_DELETE_WINDOW) to the window with the given number.
 *
 * usage: xclose WINDOW-ID
 */
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int
main(int argc, char **argv)
{
    Display *dpy;
    XEvent ev;
    Window w;

    if (argc != 2) {
        fprintf(stderr, "usage: xclose WINDOW-ID\n");

        return 2;
    }

    dpy = XOpenDisplay(NULL);

    if (dpy == NULL) {
        fprintf(stderr, "xclose: cannot open the display\n");

        return 1;
    }

    w = strtoul(argv[1], NULL, 0);
    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, w, False, NoEventMask, &ev);
    XFlush(dpy);
    XCloseDisplay(dpy);

    return 0;
}
