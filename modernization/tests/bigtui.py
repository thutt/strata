#!/usr/bin/env python3
# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# Strata is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along
# with Strata.  If not, see <https://www.gnu.org/licenses/>.
# Copyright (c) 2026 Logic Magicians Software

"""Time the terminal Editor on a very large document, as a person uses it.

usage: bigtui.py STRED DOC NEEDLE [LIMIT_SECONDS [MIDDLE_SECTION]]

The Editor runs in a pseudo-terminal.  After each key the script waits until
the program has been quiet for a short time and reports how long that took,
so the numbers include the drawing of the screen.  A command that needs more
than the limit (a second unless given) is a failure.  The typing is done in
the middle of the document, in the section MIDDLE_SECTION (5.2 unless
given).  NEEDLE is a word that
is found only near the end of the document.  The resident memory of the
Editor is read from /proc at the end.
"""
import sys as _s; _s.dont_write_bytecode = True
import fcntl
import os
import pty
import select
import shutil
import struct
import sys
import tempfile
import termios
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import tui

QUIET = 0.08
fails = 0


class Run:
    """The Editor in a pseudo-terminal with a screen."""

    def __init__(self, prog, rows=25, cols=80):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ["TERM"] = "xterm"
            os.execv(prog[0], prog)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ,
                    struct.pack("HHHH", rows, cols, 0, 0))
        self.scr = tui.Screen(rows, cols)

    def settle(self, limit=30.0, until=None):
        """Wait until the output has been quiet for QUIET seconds and, when
        given, the function UNTIL is true for the screen; return the
        seconds spent minus the quiet time."""
        t0 = time.time()
        last = time.time()
        while time.time() - t0 < limit:
            r, _, _ = select.select([self.fd], [], [], 0.01)
            if r:
                try:
                    d = os.read(self.fd, 65536)
                except OSError:
                    break
                if not d:
                    break
                self.scr.feed(d)
                last = time.time()
            elif time.time() - last >= QUIET and (
                    until is None or until(self.scr.dump())):
                break
        return time.time() - t0 - QUIET

    def key(self, name, until=None):
        """Send a key (a name of tui.KEYS or text:...) and wait."""
        if name.startswith("text:"):
            os.write(self.fd, name[5:].encode())
        elif name in tui.KEYS:
            os.write(self.fd, tui.KEYS[name].encode())
        elif len(name) == 3 and name[:2] == "C-":
            os.write(self.fd, bytes([ord(name[2]) - 96]))
        elif len(name) == 3 and name[:2] == "A-":
            os.write(self.fd, b"\x1b" + name[2].encode())
        else:
            raise ValueError("unknown key " + name)
        return max(self.settle(30.0, until), 0.0)

    def screen(self):
        return self.scr.dump()

    def rss(self):
        """The peak and the present resident memory in kilobytes."""
        out = {}
        with open("/proc/%d/status" % self.pid) as f:
            for line in f:
                if line.startswith(("VmHWM:", "VmRSS:")):
                    out[line.split(":")[0]] = int(line.split()[1])
        return out

    def stop(self):
        try:
            os.kill(self.pid, 15)
            os.waitpid(self.pid, 0)
        except OSError:
            pass


def report(name, secs, limit):
    global fails
    print("%-44s %7.0f ms" % (name, secs * 1000))
    sys.stdout.flush()
    if secs > limit:
        print("FAIL %s took %.2f s, the limit is %.2f s" % (name, secs, limit))
        fails += 1


def check(name, cond, shown=""):
    global fails
    if not cond:
        print("FAIL %s" % name)
        if shown != "":
            print(shown)
        fails += 1


def main():
    prog, doc, needle = sys.argv[1], sys.argv[2], sys.argv[3]
    limit = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0
    middle = sys.argv[5] if len(sys.argv) > 5 else "5.2"
    work = tempfile.mkdtemp(prefix="bigtui")
    os.environ["STRATA_HOME"] = os.path.join(work, "home")
    os.makedirs(os.environ["STRATA_HOME"])
    path = os.path.join(work, "big.doc")
    shutil.copy(doc, path)

    t0 = time.time()
    r = Run([prog, path])
    r.settle(60.0, lambda s: "STRUCTURED EDIT" in s)
    report("start and first screen", time.time() - t0 - QUIET, limit)
    first = r.screen()
    check("the first screen shows the document", "STRUCTURED EDIT" in first,
          first)
    print("resident memory %s" % r.rss())

    # Scrolling.
    worst = 0.0
    total = 0.0
    for _ in range(30):
        t = r.key("PGDN")
        worst = max(worst, t)
        total += t
    report("page down (worst of 30)", worst, limit)
    print("%-44s %7.0f ms" % ("page down (mean of 30)", total / 30 * 1000))
    worst = 0.0
    for _ in range(30):
        worst = max(worst, r.key("PGUP"))
    report("page up (worst of 30)", worst, limit)
    report("Ctrl-End (end of the document)", r.key("C-END"), limit)
    check("the end of the document is drawn", r.screen().strip() != "")
    report("Ctrl-Home (start of the document)", r.key("C-HOME"), limit)
    worst = 0.0
    for _ in range(40):
        worst = max(worst, r.key("DOWN"))
    report("cursor down (worst of 40)", worst, limit)

    # Search from the start to the end.
    def found(s):
        return needle in s and "Ins accepts" not in s

    r.key("F9")
    r.key("text:" + needle)
    report("Search: from the start to the end", r.key("INS", found), limit)
    s = r.screen()
    check("Search shows the word", needle in s, s)
    report("Search again (Ctrl-F9)", r.key("C-F9"), limit)
    r.key("C-HOME")
    r.key("F9")
    r.key("C-y")
    r.key("text:zzqqxxnotthere")
    report("Search: a word that is not there", r.key("INS"), limit)

    # Go To section.
    r.key("C-HOME")
    r.key("A-F7")
    r.key("DOWN")
    r.key("DOWN")
    r.key("ENTER")
    r.key("text:3.5")
    report("Go To section 3.5", r.key("ENTER"), limit)

    # Outline.
    report("outline: level 1 (Alt-1)", r.key("A-1"), limit)
    report("outline: level 2 (Alt-2)", r.key("A-2"), limit)
    report("outline: level 3 (Alt-3)", r.key("A-3"), limit)
    report("outline: everything (Alt-9)", r.key("A-9"), limit)

    # Insert and delete in the middle.
    r.key("C-HOME")
    r.key("A-F7")
    r.key("DOWN")
    r.key("DOWN")
    r.key("ENTER")
    r.key("text:" + middle)
    r.key("ENTER")
    r.key("PGDN")
    worst = 0.0
    for ch in "type some text here":
        worst = max(worst, r.key("text:" + ch))
    report("type a character (worst of 19)", worst, limit)
    worst = 0.0
    for _ in range(19):
        worst = max(worst, r.key("BKSP"))
    report("delete a character (worst of 19)", worst, limit)
    report("Enter (split a block)", r.key("ENTER"), limit)
    report("Backspace (join them)", r.key("BKSP"), limit)

    mem = r.rss()
    print("resident memory %s" % mem)
    r.stop()
    shutil.rmtree(work, ignore_errors=True)
    if fails != 0:
        print("bigtui: %d FAILED" % fails)
        return 1
    print("bigtui: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
