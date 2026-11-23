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

"""Check that str leaves the terminal as it found it.

Each case runs str in a pseudo terminal.  At the end the terminal must be in
cooked mode with echo on, the cursor must be visible, and the alternate
screen must have been left.  The cases are a normal quit, a quit after the
terminal was resized while the editor was running, and fatal signals.  With
colors the terminal must also have its own colors and cursor color back.
"""
import re
import sys as _s; _s.dont_write_bytecode = True
import os, pty, sys, time, select, termios, struct, fcntl, shutil, tempfile
import signal

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
strprog = os.environ.get('STR', os.path.join(os.environ.get(
    'STRATA_BUILD_DIR', ''), 'term', 'str'))
sample = os.environ.get('DOCSRC', os.path.join(root, 'tests', 'samples'))
fails = 0


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
                                                   + extra))
    fails += 0 if cond else 1


class Session:
    """A program running in a pseudo terminal, with its raw output."""

    def __init__(self, work, cfg, colors='none'):
        self.raw = b''
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ['TERM'] = 'xterm'
            os.environ['STRATA_COLORS'] = colors
            # No question about the characters (src/term.c): UTF-8 is forced.
            os.environ['STRATA_ASCII'] = '0'
            os.environ.pop('TMUX', None)
            os.environ.pop('COLORTERM', None)
            os.environ['XDG_CONFIG_HOME'] = cfg
            os.environ['STRATA_HOME'] = os.path.join(cfg, 'strata-home')
            os.chdir(work)
            os.execv(strprog, [strprog])
        self.resize(25, 80)
        self.pump(0.8)

    def resize(self, rows, cols):
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ,
                    struct.pack('HHHH', rows, cols, 0, 0))

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([self.fd], [], [], 0.05)
            if ready:
                try:
                    data = os.read(self.fd, 65536)
                except OSError:
                    return
                if not data:
                    return
                self.raw += data

    def send(self, data, seconds=0.4):
        os.write(self.fd, data)
        self.pump(seconds)

    def exited(self, timeout=15):
        """Has the program ended?  It is given time to: on a loaded machine
        it needs more than the pause after the key that ends it."""
        end = time.time() + timeout

        while True:
            done, _ = os.waitpid(self.pid, os.WNOHANG)

            if done != 0:
                return True

            if time.time() >= end:
                return False

            self.pump(0.1)

    def state(self):
        attrs = termios.tcgetattr(self.fd)
        cooked = bool(attrs[3] & termios.ECHO) and bool(attrs[3]
                                                        & termios.ICANON)
        cursor = self.raw.rfind(b'\x1b[?25h') > self.raw.rfind(b'\x1b[?25l')
        normal = self.raw.rfind(b'\x1b[?1049l') > self.raw.rfind(b'\x1b[?1049h')
        return cooked, cursor, normal

    def kill(self):
        try:
            os.kill(self.pid, signal.SIGKILL)
            os.waitpid(self.pid, 0)
        except OSError:
            pass


def restored(s):
    cooked, cursor, normal = s.state()
    return cooked and cursor and normal


def colors_restored(s):
    """The last thing sent about colors puts the terminal's own back: the
    reset of the attributes after the last color, and the reset of the color
    of the cursor after the last time that it was set."""
    sets = [m.end() for m in re.finditer(rb'\x1b\[0;[0-9;]*m', s.raw)]
    last_color = sets[-1] if sets else -1

    return (s.raw.rfind(b'\x1b[0m\x1b[?25h\x1b[?1049l') > last_color and
            s.raw.rfind(b'\x1b]112\x07') > s.raw.rfind(b'\x1b]12;'))


work = tempfile.mkdtemp()
cfg = tempfile.mkdtemp()
shutil.copy(os.path.join(sample, 'plan-title-table.doc'),
            os.path.join(work, 'a.doc'))
down = b'\x1b[B'
end = b'\x1b[F' # End goes to the last entry, Quit, whatever the menu has

# A normal quit from the Document Manager.
s = Session(work, cfg)
s.send(end)
s.send(b'\r', 1.0)
check('term: quit restores the terminal', s.exited() and restored(s),
      repr(s.state()))
s.kill()

# Edit a document, resize the window while the editor runs, close the editor
# and quit.  The wait for the editor must not end at the resize.
s = Session(work, cfg)
s.send(b'\r')
s.send(b'a')
s.send(b'\x1b[2~', 2.5)
s.resize(30, 100)
s.pump(1.5)
s.send(b'\x11', 2.0)
s.send(end)
s.send(b'\r', 1.0)
check('term: resize while the editor runs, then quit', s.exited()
      and restored(s), repr(s.state()))
s.kill()

# Fatal signals must not leave the terminal in raw mode.
for name in ('SIGTERM', 'SIGHUP', 'SIGINT'):
    s = Session(work, cfg)
    os.kill(s.pid, getattr(signal, name))
    s.pump(1.0)
    check('term: %s restores the terminal' % name, restored(s),
          repr(s.state()))
    s.kill()

# With colors the terminal is painted with a background; whatever ends the
# program, the colors of the terminal come back.
s = Session(work, cfg, '256')
check('term: with colors the screen is painted with the background',
      b'\x1b[0;38;5;227;48;5;19m' in s.raw, repr(s.raw[:200]))
s.send(end)
s.send(b'\r', 1.0)
check('term: with colors quit puts the colors of the terminal back',
      s.exited() and restored(s) and colors_restored(s), repr(s.raw[-100:]))
s.kill()
for name in ('SIGTERM', 'SIGHUP', 'SIGINT'):
    s = Session(work, cfg, '16')
    os.kill(s.pid, getattr(signal, name))
    s.pump(1.0)
    check('term: with colors %s puts the colors of the terminal back' % name,
          restored(s) and colors_restored(s), repr(s.raw[-100:]))
    s.kill()

shutil.rmtree(work)
shutil.rmtree(cfg)
print('termstate: %s' % ('all passed' if not fails else '%d FAILURES' % fails))
sys.exit(1 if fails else 0)
