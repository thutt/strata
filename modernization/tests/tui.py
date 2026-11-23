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

"""Drive stred in a pseudo-terminal and inspect the screen (stdlib only).

usage: tui.py [--rows N --cols N] -- <program args...>  < script
Script lines (stdin):  key names or "text:..." ; "show" prints the screen;
  bytes:HEX writes bytes (an answer of a terminal); rawbytes shows the bytes
  that the program sent since the last raw or rawbytes.
  keys: ENTER TAB ESC BKSP DEL UP DOWN LEFT RIGHT HOME END PGUP PGDN INS
        F1..F12  S-F1..S-F12  C-F1..C-F12  C-a..C-z  C-SPACE CS-SPACE  A-x  S-UP ...  wait
"""
import sys as _s; _s.dont_write_bytecode = True

# Every test that uses this module must work when TMPDIR is a long path (more
# than 100 characters: the scratch folder of a deep directory tree is one).
# A check must not compare a message, a field or a note that carries the path
# of a temporary file with the whole path as text: the programs show a long
# path shortened from the front (fit_message in src/util.h), so a check looks
# for the end of the path or for the words around it.  The targets smoke-long
# and xtest-long of the Makefile run the tests with a TMPDIR of 140 characters.
import os, pty, sys, time, select, struct, fcntl, termios, re, signal

# A guard for every test that uses this module: the directory shadowbin is
# the first in PATH, and it has xdg-open, gio and open as scripts that never
# start anything, so that a test cannot start a real program that opens a PDF
# (Print, PDF viewer) on the display of the person who runs the tests.
SHADOWBIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         'shadowbin')
if SHADOWBIN not in os.environ.get('PATH', '').split(':'):
    os.environ['PATH'] = SHADOWBIN + ':' + os.environ.get('PATH', '')

# The terminal shows no colors unless a test asks for them: the programs
# then send the attributes that they always sent (reverse for the status
# lines, the menus and the selected entries), which the tests look for.
os.environ.setdefault('STRATA_COLORS', 'none')

# The programs draw ASCII look-alikes when the terminal cannot show UTF-8, and
# ask the terminal where its cursor is at the start (src/term.c).  The tests
# read the UTF-8 shapes, so they force UTF-8 and nothing is asked; a test of
# the check passes env={'STRATA_ASCII': None} and an answer (see session).
os.environ.setdefault('STRATA_ASCII', '0')
# A test runs in no multiplexer, whatever runs the tests.
os.environ.pop('TMUX', None)
os.environ.pop('STY', None)

# The steps that open a panel of a program and wait until the panel is on
# the screen: a panel may open later than a fixed wait when the machine is
# busy or the print system answers slowly, and the test must neither fail
# then nor wait longer when the panel is quick.  The Setup page of the
# Document Manager (the last entry but one of its menu) and the Print
# panels of the Editor.
OPEN_SETUP = ['END UP', 'ENTER', 'waitfor:Backups Kept']
OPEN_PRINT_FINAL = ['F12', 'text:P', 'RIGHT', 'ENTER',
                    'waitfor:Print Document']
OPEN_PRINT_DRAFT = ['F12', 'text:P', 'ENTER', 'waitfor:Draft Print']


def printer_entries(queues=('laser', 'office')):
    """The entries of the Printer field of a Print panel, in the order in
    which the space bar steps through them: File only, PDF viewer, each
    queue, the System default printer (only when there are queues) and,
    last, Search for printers..."""
    return (['File only', 'PDF viewer'] + list(queues) +
            (['System default printer'] if queues else []) +
            ['Search for printers...'])


def printer_steps(target, queues=('laser', 'office'), start='File only'):
    """The keys that step the Printer field, a choice field, from the entry
    start to the entry target: one space for each step forward, wrapping at
    the end.  The field accepts no typed name."""
    entries = printer_entries(queues)
    return ['text: '] * ((entries.index(target) - entries.index(start)) %
                         len(entries))

KEYS = {'ENTER': '\r', 'TAB': '\t', 'ESC': '\x1b', 'BKSP': '\x7f',
    'DEL': '\x1b[3~', 'UP': '\x1b[A', 'DOWN': '\x1b[B',
        'RIGHT': '\x1b[C', 'LEFT': '\x1b[D', 'HOME': '\x1b[H', 'END': '\x1b[F',
        'PGUP': '\x1b[5~', 'PGDN': '\x1b[6~',
        'INS': '\x1b[2~', 'S-INS': '\x1b[2;2~', 'S-UP': '\x1b[1;2A',
        'S-DOWN': '\x1b[1;2B', 'S-LEFT': '\x1b[1;2D', 'S-RIGHT': '\x1b[1;2C',
        'C-LEFT': '\x1b[1;5D', 'C-RIGHT': '\x1b[1;5C', 'S-HOME': '\x1b[1;2H',
        'S-END': '\x1b[1;2F', 'C-HOME': '\x1b[1;5H', 'C-END': '\x1b[1;5F',
        'S-PGUP': '\x1b[5;2~', 'S-PGDN': '\x1b[6;2~', 'BTAB': '\x1b[Z',
        'C-PGUP': '\x1b[5;5~', 'C-PGDN': '\x1b[6;5~', 'C-SPACE': '\x00',
        'CS-SPACE': '\x1b[32;6u', 'CS-SPACE-MOK': '\x1b[27;6;32~',
        'C-SPACE-U': '\x1b[32;5u', 'C-SPACE-MOK': '\x1b[27;5;32~',
        'C-HYPHEN': '\x1f', 'C-SLASH': '\x1f', 'A-SLASH': '\x1b/',
        'A-HYPHEN': '\x1b_', 'A-MINUS': '\x1b-'}
# Konsole's form of a modified F1 to F4: ESC O m P (m 2 to 8).
for _m in range(2, 9):
    for _n, _c in enumerate('PQRS'):
        KEYS['K%d-F%d' % (_m, _n + 1)] = '\x1bO%d%s' % (_m, _c)
for i in range(1, 13):
    KEYS['F%d' % i] = ('\x1bOP \x1bOQ \x1bOR \x1bOS \x1b[15~ \x1b[17~ '
                       '\x1b[18~ \x1b[19~ \x1b[20~ \x1b[21~ \x1b[23~ '
                       '\x1b[24~').split()[i-1]
for i in range(1, 13):   # Alt-F via xterm modifier 3
    code = {1:'1;3P',2:'1;3Q',3:'1;3R',4:'1;3S',5:'15;3~',6:'17;3~',7:'18;3~',
        8:'19;3~',9:'20;3~',10:'21;3~',11:'23;3~',12:'24;3~'}[i]
    KEYS['A-F%d' % i] = '\x1b[' + code
    KEYS['C-F%d' % i] = '\x1b[' + code.replace(';3', ';5')
    KEYS['S-F%d' % i] = '\x1b[' + code.replace(';3', ';2')

class Screen:
    def __init__(self, rows, cols):
        self.r, self.c = rows, cols
        self.g = [[' '] * cols for _ in range(rows)]
        self.a = [[0] * cols for _ in range(rows)]
        # 1 where the cell was written dim (SGR 2); the step dim:ROW,COL.
        self.dm = [[0] * cols for _ in range(rows)]
        self.dimmed = 0
        # The colors of the cells: None, or the foreground and background
        # as the number or the (r, g, b) that the program sent.
        self.fgc = [[None] * cols for _ in range(rows)]
        self.bgc = [[None] * cols for _ in range(rows)]
        self.fg = self.bg = None
        self.x = self.y = 0; self.attr = 0; self.cursor = True
    def feed(self, data):
        # A read can end in the middle of a character or of an escape
        # sequence: what is not complete waits for the next read.
        if not hasattr(self, 'dec'):
            import codecs
            self.dec = codecs.getincrementaldecoder('utf-8')('replace')
            self.pend = ''
        s = self.pend + self.dec.decode(data); i = 0
        self.pend = ''
        last = s.rfind('\x1b')
        if last >= 0 and re.match(r'\x1b(\[[?0-9;]*[A-Za-z]|\][^\x07\x1b]*'
                                  r'(\x07|\x1b\\)|[^\[\]])', s[last:]) is None:
            self.pend = s[last:]; s = s[:last]
        while i < len(s):
            ch = s[i]
            if ch == '\x1b' and i + 1 < len(s) and s[i+1] == ']':
                # An operating system command (the title, the color of the
                # cursor): it ends with BEL or with ESC and a backslash.
                m = re.match(r'\x1b\][^\x07\x1b]*(\x07|\x1b\\)?', s[i:])
                i += m.end()
            elif ch == '\x1b' and i + 1 < len(s) and s[i+1] == '[':
                m = re.match(r'\x1b\[([?0-9;]*)([A-Za-z])', s[i:])
                if not m: i += 1; continue
                p, f = m.group(1), m.group(2); i += m.end()
                if f == 'H':
                    q = (p.split(';') + ['1', '1'])[:2]; self.y = int(q[0]
                        or 1) - 1; self.x = int(q[1] or 1) - 1
                elif f == 'J':
                    self.g = [[' '] * self.c for _ in range(self.r)]
                    self.a = [[0] * self.c for _ in range(self.r)]
                    self.dm = [[0] * self.c for _ in range(self.r)]
                    self.fgc = [[None] * self.c for _ in range(self.r)]
                    self.bgc = [[None] * self.c for _ in range(self.r)]
                elif f == 'm':
                    self.attr = 0; self.fg = self.bg = None; self.dimmed = 0
                    t = [int(v or 0) for v in p.split(';')]; k = 0
                    while k < len(t):
                        v = t[k]
                        if v == 7: self.attr |= 1
                        if v == 1: self.attr |= 2
                        if v == 2: self.dimmed = 1
                        if v in (38, 48):
                            # 38;5;N and 38;2;R;G;B: skip the numbers.
                            if k + 1 < len(t) and t[k + 1] == 5:
                                c = t[k + 2] if k + 2 < len(t) else 0
                                k += 2
                            else:
                                c = tuple(t[k + 2:k + 5]); k += 4
                            if v == 38: self.fg = c
                            else: self.bg = c
                        elif 30 <= v <= 37 or 90 <= v <= 97:
                            self.fg = v
                        elif 40 <= v <= 47 or 100 <= v <= 107:
                            self.bg = v
                        k += 1
                elif f == 'h' and p == '?25': self.cursor = True
                elif f == 'l' and p == '?25': self.cursor = False
            elif ch == '\x1b': i += 1
            else:
                if 0 <= self.y < self.r and 0 <= self.x < self.c:
                    self.g[self.y][self.x] = ch;
                    self.a[self.y][self.x] = self.attr
                    self.dm[self.y][self.x] = self.dimmed
                    self.fgc[self.y][self.x] = self.fg
                    self.bgc[self.y][self.x] = self.bg
                self.x += 1; i += 1
    def dump(self, marks=False):
        out = []
        for y in range(self.r):
            line = ''.join(self.g[y]).rstrip()
            out.append(line)
        return '\n'.join(out)
    def dump_rev(self):
        """rows with reverse-video runs shown inside \u27e6 \u27e7"""
        out = []
        for y in range(self.r):
            s = ''; inrev = False
            for x in range(self.c):
                r = self.a[y][x] & 1
                if r and not inrev: s += '\u27e6'; inrev = True
                if not r and inrev: s += '\u27e7'; inrev = False
                s += self.g[y][x]
            if inrev: s += '\u27e7'
            out.append(s.rstrip())
        return '\n'.join(out)

DECID = b'\x1b[?64;1;2;6;9;15;17;18;21;22;28c'


def drop_place(path):
    """Take the record ZP (the place of the cursor, saved with a document)
    out of a document file, so that the Editor opens it at the start again.
    Tests that save a document and open it again for the next step, and
    that do not test the place, use it."""
    with open(path, 'rb') as f:
        data = f.read()
    keep = [r for r in data.split(b'\r\n') if not r.startswith(b'ZP')]
    with open(path, 'wb') as f:
        f.write(b'\r\n'.join(keep))


def session(prog, steps, rows=25, cols=80, delay=0.12, status=False,
            env=None, answer=None):
    """Run prog (argv list) in a pty, execute steps, return the list of
    screen dumps ('show' / 'rev' steps).  env is a dictionary of variables
    for the program (a value of None takes the variable out); the step
    cell:ROW,COL gives the character and the colors of one cell, and the
    step raw gives what the program has sent since the last raw.  answer
    makes the pty answer the question where the cursor is (ESC [ 6 n) like a
    terminal: 'utf8' (a character moves the cursor one column), 'latin1' (a
    byte moves it one column, and the byte 0x9A is answered with the reply
    of a terminal to who are you, as an xterm in 8-bit mode does), or None
    (never).  The step rawbytes gives the bytes of the raw log, as repr."""
    # STRATA_WRAP is a command that starts the program (a test helper only:
    # tests/vgrun.py names valgrind with it); the words are split as a shell
    # would split them, and the program follows them.
    if os.environ.get('STRATA_WRAP'):
        import shlex
        prog = shlex.split(os.environ['STRATA_WRAP']) + list(prog)
    pid, fd = pty.fork()
    if pid == 0:
        os.environ['TERM'] = 'xterm'
        for k, v in (env or {}).items():
            if v is None: os.environ.pop(k, None)
            else: os.environ[k] = v
        os.execv(prog[0], prog)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    scr = Screen(rows, cols); dumps = []; rawlog = bytearray()
    def reply(d):
        if answer is None: return
        for m in re.finditer(rb'\x1b\[(\d+);(\d+)H([^\x1b]*)\x1b\[6n', d):
            body = m.group(3)
            n = len(body.decode('utf-8', 'replace')) if answer == 'utf8' \
                else len(body)
            os.write(fd, b'\x1b[%d;%dR' % (int(m.group(1)),
                int(m.group(2)) + n))
        if answer == 'latin1':
            for _ in range(d.count(b'\x9a')): os.write(fd, DECID)
    def pump(t=0.25):
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try: d = os.read(fd, 65536)
                except OSError: return False
                if not d: return False
                rawlog.extend(d)
                reply(d)
                scr.feed(d)
        return True
    pump(0.6)
    for line in steps:
        line = line.rstrip('\n')
        if not line or line.startswith('#'): continue
        if line.startswith('text:'):
            os.write(fd, line[5:].encode('utf-8')); pump(0.15)
        elif line == 'show': dumps.append(scr.dump())
        elif line == 'rev': dumps.append(scr.dump_rev())
        elif line.startswith('bytes:'):
            # Bytes that a terminal sends by itself, in hex: an answer.
            os.write(fd, bytes.fromhex(line[6:])); pump(0.3)
        elif line == 'rawbytes':
            dumps.append(repr(bytes(rawlog))); rawlog.clear()
        elif line == 'raw':
            dumps.append(rawlog.decode('utf-8', 'replace')); rawlog.clear()
        elif line.startswith('dim:'):
            row, col = [int(v) for v in line[4:].split(',')]
            dumps.append(repr(scr.dm[row][col]))
        elif line.startswith('cell:'):
            row, col = [int(v) for v in line[5:].split(',')]
            dumps.append(repr((scr.g[row][col], scr.fgc[row][col],
                               scr.bgc[row][col], scr.a[row][col])))
        elif line == 'wait': pump(0.5)
        elif line == 'clock':
            # The time now, in seconds, for a test that measures
            # how long a panel took to open.
            dumps.append(repr(time.time()))
        elif line.startswith('waitfor:'):
            # Wait until the text is on the screen, for up to 20 seconds, so
            # that a loaded machine does not make a test read a screen that
            # is not drawn yet.  When the text never comes the step ends
            # after the limit and the next 'show' reads what is there: the
            # check of the test fails, as it must.
            want = line[8:]
            end = time.time() + 20
            while want not in scr.dump() and time.time() < end:
                if not pump(0.25):
                    break
        elif line.startswith('size:'):
            # Change the size of the terminal: size:ROWS,COLS.  The screen
            # model starts empty, as the program must draw every cell.
            rows, cols = [int(v) for v in line[5:].split(',')]
            fcntl.ioctl(fd, termios.TIOCSWINSZ,
                struct.pack('HHHH', rows, cols, 0, 0))
            os.kill(pid, signal.SIGWINCH)
            scr = Screen(rows, cols); pump(0.6)
            # A loaded machine may draw the new screen later than that: wait
            # until the last row of the new size has text, for up to 20
            # seconds, then let the drawing settle.  When it never comes the
            # next 'show' reads what is there and the check of the test
            # fails, as it must.
            end = time.time() + 20
            while time.time() < end:
                last = scr.dump().split('\n')
                if len(last) >= rows and last[rows - 1].strip() != '':
                    break
                if not pump(0.25):
                    break
            pump(0.3)
        else:
            for k in line.split():
                if k in KEYS: os.write(fd, KEYS[k].encode())
                elif re.fullmatch(r'C-[a-z]', k): os.write(fd,
                    bytes([ord(k[2]) - 96]))
                elif k == 'C-^': os.write(fd, b'\x1e')
                elif re.fullmatch(r'A-.', k): os.write(fd, b'\x1b'
                    + k[2].encode())
                else: raise ValueError('unknown key ' + k)
                pump(delay)
    pump(0.3)
    st = None
    try:
        r, s_ = os.waitpid(pid, os.WNOHANG)
        if r: st = s_
    except Exception: pass
    if st is None:
        try: os.kill(pid, 15); os.waitpid(pid, 0)
        except Exception: pass
    return (dumps, st) if status else dumps

def main():
    args = sys.argv[1:]; rows, cols = 25, 80
    while args and args[0] != '--':
        if args[0] == '--rows': rows = int(args[1]); args = args[2:]
        elif args[0] == '--cols': cols = int(args[1]); args = args[2:]
        else: break
    if args and args[0] == '--': args = args[1:]
    for d in session(args, sys.stdin.readlines(), rows,
        cols): print(d); print('-' * cols)
if __name__ == '__main__': main()
