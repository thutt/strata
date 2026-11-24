# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
# Copyright (c) 2026 Logic Magicians Software

# This program sends a function key to the X display named by $DISPLAY by
# its keycode with the XTest extension, because xdotool adds a stray Alt to
# function keys on a virtual display whose keymap has F12 at several levels.
#
#     python3 -I manual/xk.py [--hold] [shift|ctrl|alt ...] F12
#
# With --hold the program returns at once and a copy of it lets the
# modifier keys go five seconds later.

import ctypes
import os
import sys
import time

CODES = dict(("F%d" % i, 66 + i) for i in range(1, 11))
CODES["F11"] = 95
CODES["F12"] = 96
MODS = {"shift": 50, "ctrl": 37, "alt": 64}


def main(args):
    """Send the key; the last argument is the key, the others modifiers."""
    xlib = ctypes.CDLL("libX11.so.6")
    xtst = ctypes.CDLL("libXtst.so.6")
    xlib.XOpenDisplay.restype = ctypes.c_void_p
    xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
    disp = ctypes.c_void_p(xlib.XOpenDisplay(os.environ["DISPLAY"].encode()))

    def key(code, down):
        xtst.XTestFakeKeyEvent(disp, code, down, 0)
        xlib.XFlush(disp)

    hold = args[0] == "--hold"

    if hold:
        args = args[1:]

    for m in args[:-1]:
        key(MODS[m], 1)
        time.sleep(0.1)
    key(CODES[args[-1]], 1)
    time.sleep(0.15)
    key(CODES[args[-1]], 0)

    if hold and os.fork() != 0:
        # The modifier stays down for a few seconds, so that a box that
        # takes "any key" is not closed by the release of the modifier.
        return 0

    if hold:
        time.sleep(5)

    for m in reversed(args[:-1]):
        time.sleep(0.1)
        key(MODS[m], 0)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
