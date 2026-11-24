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
 * shot: writes the root window of the X display to a binary PPM file.
 * This replaces xwd and xwdtopnm, which are not installed here.
 * The only argument is the name of the output file.
 */
#include <stdio.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>


int
main(int argc, char **argv)
{
    Display *dpy;
    Window root;
    XImage *img;
    XWindowAttributes wa;
    FILE *fp;
    int x;
    int y;

    if (argc != 2) {
        fprintf(stderr, "usage: shot OUT.ppm\n");

        return 2;
    }

    dpy = XOpenDisplay(NULL);

    if (dpy == NULL) {
        fprintf(stderr, "shot: cannot open the display\n");

        return 1;
    }

    root = DefaultRootWindow(dpy);
    XGetWindowAttributes(dpy, root, &wa);
    img = XGetImage(dpy, root, 0, 0, (unsigned)wa.width, (unsigned)wa.height,
                    AllPlanes, ZPixmap);

    if (img == NULL) {
        fprintf(stderr, "shot: XGetImage failed\n");

        return 1;
    }

    fp = fopen(argv[1], "wb");

    if (fp == NULL) {
        fprintf(stderr, "shot: cannot write %s\n", argv[1]);

        return 1;
    }

    fprintf(fp, "P6\n%d %d\n255\n", wa.width, wa.height);

    for (y = 0; y < wa.height; y++) {
        for (x = 0; x < wa.width; x++) {
            unsigned long p = XGetPixel(img, x, y);
            int c;

            for (c = 2; c >= 0; c--) {
                fputc((int)((p >> (8 * c)) & 255UL), fp);
            }
        }
    }

    fclose(fp);
    XDestroyImage(img);
    XCloseDisplay(dpy);

    return 0;
}
