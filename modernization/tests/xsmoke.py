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

"""X11 frontend tests: run build/x11/{stred,str} under Xvfb, inject keys with
xdotool, and assert on the screen text the backend writes to
$STRATA_X11_DUMP."""
import sys as _s; _s.dont_write_bytecode = True
import os, re, sys, time, shutil, struct, subprocess, tempfile
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bindir = os.environ.get('XBIN', os.path.join(os.environ.get('STRATA_BUILD_DIR',
    ''), 'x11'))
sample = os.environ.get('DOCSRC', os.path.join(root, 'tests', 'samples'))
helpf = os.environ.get('STRATA_HELP', os.path.join(
    os.environ.get('STRATA_BUILD_DIR', ''), 'strata.hlp'))
for tool in ('Xvfb', 'xdotool', 'xkbcomp'):
    if not shutil.which(tool): print('SKIP: %s not '
        'installed' % tool); sys.exit(0)
if not os.path.exists(os.path.join(bindir, 'stred')): print('SKIP: run `make '
    'x11` first'); sys.exit(0)

# Xvfb picks a display number that is free (-displayfd).  A number taken
# from the process id could belong to an Xvfb that is still running, and
# then Xvfb fails to start and the tests meet the windows of that server.
dfd_r, dfd_w = os.pipe()
# The server is not reset when the last client leaves (-noreset): a reset
# would bring back the key map that one_level_function_keys changes.
xvfb = subprocess.Popen(['Xvfb', '-noreset', '-displayfd', str(dfd_w),
    '-screen', '0', '1280x800x24'], stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL, pass_fds=(dfd_w,))
os.close(dfd_w)
num = b''
while not num.endswith(b'\n'):
    chunk = os.read(dfd_r, 16)
    if not chunk:
        print('FAIL: Xvfb did not start'); sys.exit(1)
    num += chunk
os.close(dfd_r)
disp = ':' + num.decode().strip()

# The key map of Xvfb gives each of the keys F1 to F12 the symbols
# [F1, F1, F1, F1, XF86Switch_VT_1] on the type CTRL+ALT.  To type F1, xdotool
# 3.20160805 finds that F1 is on a level that needs Alt, and so it presses
# Alt_L around the key: the program is then sent Alt-F1, Alt-F12 and
# Ctrl-Alt-F12 (Ctrl-Alt-F12 is even the keysym XF86Switch_VT_12) although
# the test asked for F1, F12 and Ctrl-F12.  The program is right to take the
# Alt state of the events it gets.  Give the function keys one level, as the
# keyboard of a person who has no virtual consoles to switch has.
def one_level_function_keys(display):
    km = subprocess.run(['xkbcomp', '-xkb', display, '-'],
        capture_output=True, text=True).stdout
    km = re.sub(r'(key <FK\d\d> \{\s*)type= "CTRL\+ALT",\s*'
        r'symbols\[Group1\]= \[\s*(F\d+),[^\]]*\]',
        r'\1type= "ONE_LEVEL", symbols[Group1]= [ \2 ]', km)
    subprocess.run(['xkbcomp', '-w0', '-', display], input=km, text=True,
        capture_output=True)
one_level_function_keys(disp)
tmp = tempfile.mkdtemp(); dump = os.path.join(tmp, 'dump')
# No test may start a real program that opens a PDF: xdg-open, gio and open
# are guards (tests/shadowbin) at the front of PATH.
shadowbin = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         'shadowbin')
env = dict(os.environ, DISPLAY=disp, STRATA_X11_DUMP=dump,
    PATH=shadowbin + ':' + os.environ.get('PATH', ''),
    XDG_CONFIG_HOME=os.path.join(tmp, 'cfg'),
    STRATA_HOME=os.path.join(tmp, 'strata-home'), SHELL='/bin/true')
fails = 0; procs = []

def xdo(*a):
    return subprocess.run(['xdotool'] + list(a), env=env, capture_output=True,
        text=True).stdout.strip()
def start(args, cwd=None):
    if os.path.exists(dump): os.remove(dump)
    p = subprocess.Popen(args, env=env, cwd=cwd, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL); procs.append(p)
    wait(lambda s: len(s) > 0); return p
def flat(s):
    # The text of a screen on one line: the frames of boxes are dropped and
    # a message that a box wraps (a long path) is joined again.
    return ' '.join(re.sub('[\u2551\u2554\u2557\u255a\u255d\u2550]', ' ',
        s).split())
def screen():
    try: return open(dump, encoding='utf-8').read()
    except OSError: return ''
def wait(pred, t=6.0):
    end = time.time() + t; s = ''
    while time.time() < end:
        s = screen()
        if s and pred(s): return s
        time.sleep(0.1)
    return s
def win(name):
    for _ in range(40):
        w = xdo('search', '--name', '^%s$' % name)
        if w: return w.split()[0]
        time.sleep(0.1)
    return None
def key(w, *keys): subprocess.run(['xdotool', 'key', '--window', w, '--delay',
    '70'] + list(keys), env=env, capture_output=True)
def typ(w, s): subprocess.run(['xdotool', 'type', '--window', w, '--delay',
    '40', s], env=env, capture_output=True)
def pixel(w, x, y):
    """The color 0xRRGGBB of the pixel (x, y) of the window, as the X
    server shows it (xwd writes the window, with a header of 25 numbers)."""
    data = subprocess.run(['xwd', '-id', w, '-silent'], env=env,
        capture_output=True).stdout
    h = struct.unpack('>25I', data[:100])
    base = h[0] + h[19] * 12
    if y < 0:
        y += h[5]
    bpp = h[11] // 8
    raw = data[base + y * h[12] + x * bpp:base + y * h[12] + (x + 1) * bpp]
    v = int.from_bytes(raw, 'little' if h[7] == 0 else 'big')
    rgb = 0
    for mask in (h[14], h[15], h[16]):
        shift = (mask & -mask).bit_length() - 1
        rgb = (rgb << 8) | (((v & mask) >> shift) * 255 // (mask >> shift))
    return rgb
def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra))); fails += 0 if cond else 1

doc = os.path.join(tmp, 'a.doc'); shutil.copy(os.path.join(sample,
    'plan-title-table.doc'), doc)
try:
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc]);
    w = win('stred')
    s = screen()
    check('x11: editor renders the document in a window', w
        and 'Orchard Plan' in s and 'STRUCTURED EDIT' in s, s)
    check('x11: box-drawing and tab alignment',
        '\u2500\u2500\u2500\u2500' in s and any(l.startswith('Site:')
        and l.find('Heron') == 24 for l in s.split('\n')), s)
    # A modifier key alone shows its function key labels.
    for mod, lab, first in (('Alt_L', 'F2: Append', 'F12: Marker'),
            ('Control_L', 'F1: Learn', 'F6: Case F7: Prev Error'), ('Shift_L', 'F1:', None)):
        xdo('keydown', '--window', w, mod)
        s = wait(lambda s: 'F1: Help' not in s)
        last = s.split('\n')[-6:]
        check('x11: holding %s shows its function key labels' % mod,
            any(lab in l for l in last) and (first is None or
            all(any(f in l for l in last) for f in first.split(' F'))) and
            'F1: Help' not in s, s)
        xdo('keyup', '--window', w, mod)
        s = wait(lambda s: 'F1: Help' in s)
        check('x11: releasing %s shows the usual labels again' % mod,
            'F1: Help' in s and 'F12: Menu' in s, s)
    xdo('keydown', '--window', w, 'Shift_L')
    xdo('keydown', '--window', w, 'Control_L')
    s = wait(lambda s: 'F1: Learn' in s)
    check('x11: Ctrl wins over Shift', 'F1: Learn' in s, s)
    xdo('keydown', '--window', w, 'Alt_L')
    s = wait(lambda s: 'F2: Append' in s)
    check('x11: Alt wins over Ctrl and Shift', 'F2: Append' in s, s)
    for mod in ('Alt_L', 'Control_L', 'Shift_L'):
        xdo('keyup', '--window', w, mod)
    wait(lambda s: 'F1: Help' in s)
    # The key strip shows F1 to F12, six entries on each row, and no two
    # entries touch, in every mode.
    def strip_rows(s):
        rows = [l for l in s.split('\n')[:-1] if not l.startswith('\x01')]
        return rows[-2:]
    def strip_fault(s):
        top, bot = strip_rows(s)
        for row, want in ((top, [1, 3, 5, 7, 9, 11]),
                (bot, [2, 4, 6, 8, 10, 12])):
            found = [(m.start(), int(m.group(1))) for m in
                re.finditer(r'F(\d+):', row)]
            if [n for _, n in found] != want:
                return 'keys %r, wanted %r' % ([n for _, n in found], want)
            for pos, n in found[1:]:
                if row[pos - 1] != ' ':
                    return 'F%d touches the entry before it' % n
        return None
    for mod, lab in ((None, 'F1: Help'), ('Shift_L', 'F1:'),
            ('Control_L', 'F1: Learn'), ('Alt_L', 'F2: Append')):
        if mod: xdo('keydown', '--window', w, mod)
        s = wait(lambda s: lab in ''.join(strip_rows(s)))
        time.sleep(0.3); s = screen()
        check('x11: the key strip has F1 to F12 with spacing (%s)' %
            (mod or 'no modifier'), strip_fault(s) is None and
            (mod != 'Shift_L' or 'F1: Help' not in s), (strip_fault(s), s))
        # No label is cut: every entry of the mode is there in full.
        full = {None: 'Help Split Copy Cut Paste Format Move Table Search'
                      ' - - Menu',
                'Shift_L': '- - - - - - - - - - - -',
                'Control_L': 'Learn Math Lib~Copy Lib~Cut Lib~Paste Case '
                             'Prev~Error Next~Error Again Undo Redo Marker',
                'Alt_L': '- Append Join Delete Overtype Set~Attr '
                         'Go~To Border Replace - - Marker'}[mod]
        rows = strip_rows(s)
        miss = [i + 1 for i, lab in enumerate(full.split())
                if ('F%d: %s' % (i + 1, lab.replace('~', ' ')) if lab != '-'
                    else 'F%d:' % (i + 1)) not in rows[i % 2]]
        check('x11: no label of the key strip is cut (%s)' %
            (mod or 'no modifier'), miss == [], (miss, s))
        if mod: xdo('keyup', '--window', w, mod)
        wait(lambda s: 'F1: Help' in ''.join(strip_rows(s)))
    # Ctrl-F1 is Learn, not Help; F1 alone is Help.  The event of Ctrl-F1
    # carries the state ControlMask (xdotool key ctrl+F1): the events that
    # xdotool keydown --window sends do not set the state of the next key.
    key(w, 'ctrl+F1')
    s = wait(lambda s: 'Define Macro' in s or 'HELP:' in s)
    check('x11: Ctrl-F1 starts Learn: the panel Define Macro opens, not the '
        'help', 'Define Macro' in s and 'Macro Key: Alt-' in s and
        'HELP:' not in s and 'Help file not found' not in s, s)
    key(w, 'Escape'); time.sleep(0.4)
    key(w, 'Shift+F1'); time.sleep(0.5); s = screen()
    check('x11: Shift-F1 does nothing', 'HELP:' not in s and
        'Define Macro' not in s and 'F1: Help' in s, s)
    key(w, 'F1')
    s = wait(lambda s: 'HELP:' in s)
    check('x11: F1 alone opens the help', 'HELP: Edit Screen' in s and
        'Define Macro' not in s, s)
    key(w, 'Escape'); time.sleep(0.4)
    key(w, 'alt+1'); s = wait(lambda s: 'Neighbours' not in s)
    check('x11: Alt-1 collapses the outline', 'Orchard Plan' in s
        and 'Neighbours' not in s, s)
    key(w, 'alt+0'); key(w, 'alt+0'); key(w, 'F1');
    s = wait(lambda s: 'HELP:' in s)
    check('x11: F1 opens the help', 'HELP: Edit Screen' in s, s)
    # The help is a window across the foot of the screen: the document stays
    # in view above it, a link is followed with Tab and Enter, Backspace goes
    # back and Z makes the window as large as the screen.
    check('x11: the help window leaves the document in view above it',
        'Orchard Plan' in s.split('\n')[2] and 'Esc: close' in s, s)
    key(w, 'Tab'); key(w, 'Return')
    s = wait(lambda s: 'HELP: Function Keys' in s)
    check('x11: Tab and Enter follow a link in the help',
        'HELP: Function Keys' in s, s)
    key(w, 'BackSpace')
    s = wait(lambda s: 'HELP: Edit Screen' in s)
    check('x11: Backspace goes back in the help', 'HELP: Edit Screen' in s, s)
    key(w, 'z')
    s = wait(lambda s: 'HELP:' in s.split('\n')[0])
    check('x11: Z makes the help window as large as the screen',
        'HELP:' in s.split('\n')[0], s)
    key(w, 'Escape'); time.sleep(0.4)
    key(w, 'F12'); key(w, 'p'); key(w, 'F1')
    s = wait(lambda s: 'HELP:' in s)
    check('x11: F1 in the menu bar shows the topic of the entry (Print, '
        'Draft)', 'HELP: Print Draft' in s, s)
    key(w, 'Escape'); key(w, 'Escape'); key(w, 'Escape'); time.sleep(0.4)
    # F1 in the text editor of a note shows the topic of the note text, and
    # F1 on the entry Next Error of the Print menu shows its own topic.
    key(w, 'Down'); key(w, 'alt+F12'); time.sleep(0.4); key(w, 'n')
    key(w, 'Insert')
    s = wait(lambda s: 'NOTE EDIT' in s)
    key(w, 'F1')
    s = wait(lambda s: 'HELP:' in s)
    check('x11: F1 in the editor of a note shows the topic Note Text',
        'HELP: Note Text' in s, s)
    key(w, 'Escape'); time.sleep(0.4); key(w, 'Escape'); time.sleep(0.4)
    key(w, 'F12'); key(w, 'p')
    for _ in range(3):
        key(w, 'Right')
    key(w, 'F1')
    s = wait(lambda s: 'HELP:' in s)
    check('x11: F1 on the entry Next Error shows the topic Print Errors',
        'HELP: Print Errors' in s, s)
    key(w, 'Escape'); key(w, 'Escape'); key(w, 'Escape'); time.sleep(0.4)
    key(w, 'ctrl+Home'); key(w, 'F12');
    s = wait(lambda s: 'File' in s.split('\n')[0]
        and 'Quit' in s.split('\n')[0])
    check('x11: F12 menu bar', 'Global' in s.split('\n')[0]
        and 'Quit' in s.split('\n')[0] and 'Shell' not in s, s)
    # F12 again closes the menu bar.
    key(w, 'F12'); s = wait(lambda s: 'Global' not in s.split('\n')[0])
    check('x11: F12 again closes the menu bar', 'Global' not in
        s.split('\n')[0], s)
    # F10 and F11 have no command: no menu bar, no marker commands.
    key(w, 'F10'); time.sleep(0.5); s = screen()
    check('x11: F10 does nothing', 'Global' not in s.split('\n')[0] and
        'Marker Commands' not in s, s)
    key(w, 'F11'); time.sleep(0.5); s = screen()
    check('x11: F11 does nothing', 'Global' not in s.split('\n')[0] and
        'Marker Commands' not in s, s)
    key(w, 'alt+F10'); time.sleep(0.5); s = screen()
    check('x11: Alt-F10 is not the marker command now', 'Marker Commands'
        not in s, s)
    # the MARKER command: Alt-F12, and two other ways for
    # window managers that keep Alt-F12 for themselves
    key(w, 'Escape'); key(w, 'alt+F12')
    s = wait(lambda s: 'Marker Commands' in s)
    check('x11: Alt-F12 offers the marker commands', 'Marker Commands' in s
        and 'Cross-reference' in s, s)
    key(w, 'Escape'); key(w, 'ctrl+F12')
    s = wait(lambda s: 'Marker Commands' in s)
    check('x11: Ctrl-F12 offers the marker commands too', 'Marker Commands'
        in s, s)
    key(w, 'Escape'); key(w, 'F12'); key(w, 'Right'); key(w, 'Right')
    key(w, 'Return')
    s = wait(lambda s: 'Title' in s)
    check('x11: the Edit menu offers Title and Window', 'Title' in s and
        'Window' in s, s)
    key(w, 'Escape'); key(w, 'ctrl+Home')
    # Global, Format: PgUp twice reaches the third panel, Line Numbering.
    key(w, 'Escape'); key(w, 'F12'); key(w, 'Right'); key(w, 'Return')
    key(w, 'Return'); wait(lambda s: 'Global Format' in s)
    key(w, 'Prior'); key(w, 'Prior')
    s = wait(lambda s: 'line numbering' in s)
    check('x11: Global, Format, PgUp PgUp shows the Line Numbering panel',
        'Global Format (line numbering)' in s and all(f in s for f in
        ('Numbering', 'Number Every', 'Side', 'Number Size', 'Number Font',
        'Number Format', 'Distance', 'Equations')) and 'Pagewise' not in s
        and 'Off' in s and 'Tiny' in s, s)
    key(w, 'Right'); s = wait(lambda s: 'Pagewise' in s)
    check('x11: Right on Numbering shows Pagewise', 'Pagewise' in s, s)
    key(w, 'Escape'); wait(lambda s: 'Global Format' not in s)
    key(w, 'Escape'); key(w, 'ctrl+Home')
    key(w, 'Escape'); key(w, 'ctrl+a'); typ(w, 'typed in X'); key(w, 'ctrl+s');
    time.sleep(0.5)
    check('x11: typing and Ctrl-S write the file', b'typed in X' in open(doc,
        'rb').read(), screen())
    s = screen(); lines = s.split('\n')
    check('x11: window opens as 80x50 cells', len([l for l in lines[:-1]
        if not l.startswith('\x01')]) == 50 and 'STRUCTURED EDIT' in lines[0],
        '%d lines; first=%r' % (len(lines), lines[0]))
    geo = xdo('getwindowgeometry', w)
    check('x11: the window has a geometry', 'Geometry' in geo,
        geo)
    key(w, 'ctrl+q'); time.sleep(0.6)
    check('x11: Ctrl-Q quits cleanly', p.poll() is not None)

    # Ctrl-Shift-Space inserts a no-break space: a cap (EF) on the screen, the
    # character 1E in the saved document, and one key of a macro.  Ctrl-Space
    # without Shift does nothing.
    ndoc = os.path.join(tmp, 'nb.doc'); shutil.copy(os.path.join(sample,
        'plan-title-table.doc'), ndoc)
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, ndoc])
    w = win('stred')
    typ(w, 'x'); key(w, 'ctrl+shift+space'); typ(w, 'y'); time.sleep(0.4)
    s = screen()
    check('x11: Ctrl-Shift-Space draws a cap (EF) that is not a space of '
        'the text', 'x\u2229yOrchard' in s, s)
    check('x11: a plain space of the text is still drawn as a blank',
        'Orchard Plan' in s and s.count('\u2229') == 1, s)
    typ(w, 'p'); key(w, 'ctrl+space'); typ(w, 'q'); time.sleep(0.4)
    s = screen()
    check('x11: Ctrl-Space without Shift types nothing', 'pqOrchard' in
        s.replace('x\u2229y', '') or 'x\u2229ypqOrchard' in s, s)
    key(w, 'ctrl+s'); time.sleep(0.6)
    data = open(ndoc, 'rb').read()
    check('x11: Ctrl-Shift-Space is saved as the character 1E, Ctrl-Space '
        'is not saved', b'x\x1eypq\x1cOrchard' in data, repr(data[300:400]))
    key(w, 'ctrl+q'); time.sleep(0.6)
    check('x11: the editor quits after the no-break space test',
        p.poll() is not None)

    # An exposed window (moved, uncovered, mapped again) is repaired by
    # copying a finished picture, not by drawing the screen again; drawing
    # it again flickers.  The dump is rewritten only when the screen is drawn.
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc])
    w = win('stred')
    wait(lambda s: 'Orchard Plan' in s)
    time.sleep(0.5)
    before = os.stat(dump).st_mtime_ns
    xdo('windowunmap', w); time.sleep(0.3)
    xdo('windowmap', w); time.sleep(1.0)
    check('x11: an exposed window is not drawn again',
        os.stat(dump).st_mtime_ns == before, 'the screen was drawn again')
    if shutil.which('xwd'):
        shot = subprocess.run(['xwd', '-silent', '-id', w], env=env,
            capture_output=True).stdout
        hdr = struct.unpack('>25I', shot[:100])
        off = hdr[0] + hdr[19] * 12
        lit = sum(1 for i in range(off, len(shot) - 3, 4)
            if shot[i:i + 3] != b'\x00\x00\x00')
        check('x11: the window still shows the text after being exposed',
            lit > 2000, '%d lit pixels' % lit)
    key(w, 'ctrl+q'); time.sleep(0.6)

    # sections that start with SE<n>: numbered, and the keypad - and + work
    sdoc = os.path.join(tmp, 's.doc')
    shutil.copy(os.path.join(root, 'tests', 'data', 'sections_se.doc'), sdoc)
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, sdoc])
    w = win('stred')
    s = wait(lambda s: 'Purpose' in s)
    check('x11: sections that start with SE are numbered', '1 Purpose' in s
        and '2 Prerequisites' in s, s)
    key(w, 'Down'); key(w, 'Down')
    s = wait(lambda s: 'Sect: 1' in s.split('\n')[0])
    check('x11: the status line shows the section number', 'Sect: 1' in
        s.split('\n')[0], s)
    key(w, 'KP_Subtract'); s = wait(lambda s: 'Purpose body.' not in s)
    check('x11: keypad minus contracts the section', 'Purpose body.' not in s
        and '1 Purpose' in s, s)
    key(w, 'KP_Add'); s = wait(lambda s: 'Purpose body.' in s)
    check('x11: keypad plus expands the section', 'Purpose body.' in s, s)
    # Folding changes the document, so Quit prompts; discard the change.
    key(w, 'ctrl+q')
    s = wait(lambda s: 'unsaved changes' in s)
    check('x11: contracting and expanding is a change that Quit prompts about',
        'unsaved changes' in s, s)
    key(w, 'Down'); key(w, 'Return'); time.sleep(0.6)

    # a displayed equation is typeset to a picture (needs latex and dvipng)
    if shutil.which('latex') and shutil.which('dvipng'):
        env['XDG_CACHE_HOME'] = os.path.join(tmp, 'cache')
        edoc = os.path.join(tmp, 'e.doc')
        shutil.copy(os.path.join(root, 'tests', 'data', 'equation.doc'), edoc)
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, edoc])
        w = win('stred')
        s = wait(lambda s: '\ue000' in s, 30.0)
        check('x11: an equation that is on its own is typeset to a picture '
            'when it is first shown', '\ue000' in s and '[EQN' not in s, s)
        key(w, 'alt+F12')
        s = wait(lambda s: 'EQUATION EDIT' in s)
        typ(w, 'z+'); key(w, 'Insert')
        s = wait(lambda s: 'EQUATION EDIT' not in s and '[EQN' in s, 5.0)
        s = wait(lambda s: '\ue000' in s and '[EQN' not in s, 30.0)
        check('x11: the picture is made again after the equation editor',
            '\ue000' in s and '[EQN' not in s, s)
        # Ctrl-W in the equation editor: an enlarged picture, or the error
        key(w, 'alt+F12')
        s = wait(lambda s: 'EQUATION EDIT' in s)
        key(w, 'ctrl+w')
        s = wait(lambda s: 'Esc: back to the editor' in s and '\ue000' in s,
            30.0)
        check('x11: Ctrl-W in the equation editor shows an enlarged picture',
            'Esc: back to the editor' in s and '\ue000' in s, s)
        key(w, 'Escape')
        typ(w, '\\frac{1')
        key(w, 'ctrl+w')
        s = wait(lambda s: 'could not typeset' in s, 30.0)
        check('x11: Ctrl-W shows the message of latex when the equation does '
            'not typeset', 'could not typeset' in s and 'File ended' in s, s)
        key(w, 'Escape'); key(w, 'Escape')
        wait(lambda s: 'EQUATION EDIT' not in s)

        # Global, Marker, Marker Display Modes: Equation Short shows the label
        def equations(answer):
            key(w, 'F12'); key(w, 'g'); key(w, 'Right'); key(w, 'Right')
            key(w, 'Right'); key(w, 'Return')
            wait(lambda s: 'Marker Format' in s)
            key(w, 'Prior')
            wait(lambda s: 'Marker Display Modes' in s)
            for _ in range(10): key(w, 'Down')
            for _ in range(3): key(w, answer)
            key(w, 'Insert')
        equations('Left')
        s = wait(lambda s: '[EQN' in s and '\ue000' not in s, 10.0)
        check('x11: Equation in the mode Short shows the equation marker', '[EQN'
            in s and '\ue000' not in s, s)
        equations('Right')
        s = wait(lambda s: '\ue000' in s and '[EQN' not in s, 30.0)
        check('x11: Equation in the mode Inline shows the picture again',
            '\ue000' in s and '[EQN' not in s, s)
        p.terminate(); p.wait(5)
        idoc = os.path.join(tmp, 'i.doc')
        shutil.copy(os.path.join(root, 'tests', 'data',
            'equation_inline.doc'), idoc)
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, idoc])
        w = win('stred')
        s = wait(lambda s: '\ue000' in s and '[EQN' not in s, 60.0)
        check('x11: equations inside a line of text are typeset to pictures '
            'too', '\ue000' in s and '[EQN' not in s and 'The sum' in s
            and 'is positive' in s, s)
        lines = s.split('\n')
        check('x11: the picture of an inline equation gives its row room',
            any('The sum' in l for l in lines) and sum(1 for l in lines
            if '\ue000' in l) >= 4, s)
        p.terminate(); p.wait(5)
        del env['XDG_CACHE_HOME']
    else:
        print('skip: latex or dvipng is not installed')

    # a Picture marker is typeset by pdflatex and pdftoppm like an equation
    if shutil.which('pdflatex') and shutil.which('pdftoppm'):
        import re
        env['XDG_CACHE_HOME'] = os.path.join(tmp, 'cache2')
        pdoc = os.path.join(tmp, 'p.doc')
        pic = os.path.join(root, 'tests', 'data', 'picture.png')
        d0 = open(os.path.join(root, 'tests', 'data', 'equation.doc'),
            'rb').read()
        d1 = re.sub(rb'\x139\(.*?\x13\)', lambda m: b'\x1325(1' +
            pic.encode() + b'\x00\r\n\x13)', d0, count=1, flags=re.S)
        open(pdoc, 'wb').write(d1)
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, pdoc])
        w = win('stred')
        s = wait(lambda s: '\ue000' in s and '[PIC' not in s, 60.0)
        check('x11: a picture file is typeset to a picture when it is shown',
            '\ue000' in s and '[PIC' not in s, s)
        key(w, 'F12'); key(w, 'g'); key(w, 'Right'); key(w, 'Right')
        key(w, 'Right'); key(w, 'Return')
        wait(lambda s: 'Marker Format' in s)
        for _ in range(3): key(w, 'Prior')
        wait(lambda s: 'Marker Display Modes (3 of 3)' in s)
        for _ in range(4): key(w, 'Down')
        for _ in range(3): key(w, 'Left')
        key(w, 'Insert')
        s = wait(lambda s: '[PIC' in s and '\ue000' not in s, 10.0)
        check('x11: Picture in the mode Short shows the label of the picture',
            '[PIC' in s and '\ue000' not in s, s)
        key(w, 'ctrl+q')
        s = wait(lambda s: 'unsaved changes' in s)
        check('x11: changing a display mode is a change of the document',
            'unsaved changes' in s, s)
        p.terminate(); p.wait(5)
        del env['XDG_CACHE_HOME']
        # a relative file name is found from the directory stred was started in
        cwd = os.path.join(tmp, 'startdir')
        os.makedirs(os.path.join(cwd, 'sub'))
        shutil.copy(pic, os.path.join(cwd, 'sub', 'rel.png'))
        rdoc = os.path.join(tmp, 'r.doc')
        d1 = re.sub(rb'\x139\(.*?\x13\)', lambda m: b'\x1325(1sub/rel.png'
            b'\x00\r\n\x13)', d0, count=1, flags=re.S)
        open(rdoc, 'wb').write(d1)
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, rdoc], cwd=cwd)
        w = win('stred')
        s = wait(lambda s: '\ue000' in s and '[PIC' not in s, 60.0)
        check('x11: a relative picture name is found from the start directory',
            '\ue000' in s and '[PIC' not in s, s)
        p.terminate(); p.wait(5)

        # a Keycap marker is typeset to a box by pdflatex like a picture
        env['XDG_CACHE_HOME'] = os.path.join(tmp, 'cache3')
        kdoc = os.path.join(tmp, 'k.doc')
        d1 = re.sub(rb'\x139\(.*?\x13\)', lambda m: b'Press \x1337(1PrtScr'
            b'\x00\r\n\x13) now', d0, count=1, flags=re.S)
        open(kdoc, 'wb').write(d1)
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, kdoc])
        w = win('stred')
        s = wait(lambda s: '\ue000' in s and 'PrtScr' not in s, 60.0)
        check('x11: a keycap is typeset to a picture in the line of text',
            '\ue000' in s and 'Press' in s and 'now' in s, s)
        p.terminate(); p.wait(5)

        # The Scale of the Keycap Format is the scale of a keycap that has
        # none of its own: with OKS2000 the picture is wider (more cells).
        def keycap_cells(data, name):
            kp = os.path.join(tmp, name)
            open(kp, 'wb').write(data)
            kproc = start([os.path.join(bindir, 'stred'), '-H', helpf, kp])
            ks = wait(lambda s: s.count('\ue000') > 0 and 'PrtScr' not in s,
                60.0)
            kproc.terminate(); kproc.wait(5)
            return ks.count('\ue000'), ks
        n1, s1 = keycap_cells(d1, 'k1.doc')
        n2, s2 = keycap_cells(d1.replace(b'YIWN2\r\n', b'YIWN2\r\nOKS2000\r\n',
            1), 'k2.doc')
        check('x11: a keycap takes the Scale of the Keycap Format (%d, %d '
            'cells wide)' % (n1, n2), n1 > 0 and n2 > n1, s2)
        del env['XDG_CACHE_HOME']
    else:
        print('skip: pdflatex or pdftoppm is not installed')

    # The width of the window is fixed and the height may be changed, in
    # whole rows, from 20 rows to the height of the screen.
    env['STRATA_X11_GEOM'] = '80x30'
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc]);
    w = win('stred')
    def geom():
        g = dict(l.split('=') for l in xdo('getwindowgeometry', '--shell',
            w).split('\n') if '=' in l)
        return int(g['WIDTH']), int(g['HEIGHT'])
    def rows_of(s): return [l for l in s.split('\n')[:-1]
        if not l.startswith('\x01')]
    def nrows(s): return len(rows_of(s))
    s = wait(lambda s: nrows(s) == 30)
    w0, h0 = geom(); cwid = w0 // 80; chei = h0 // 30
    check('x11: the window opens at the grid of STRATA_X11_GEOM (80x30)',
        nrows(s) == 30 and w0 == 80 * cwid and h0 == 30 * chei, (w0, h0))
    xdo('windowsize', w, str(w0), str(40 * chei))
    s = wait(lambda s: nrows(s) == 40)
    lines = rows_of(s)
    check('x11: a window made taller shows more rows, with the status line '
        'at the top and the key strip at the foot', nrows(s) == 40 and
        'STRUCTURED EDIT' in lines[0] and lines[-2].startswith('F1: Help')
        and lines[-1].startswith('F2: Split') and
        sum(1 for l in lines[1:-2] if l.strip() != '') > 27, s)
    check('x11: the width stays when only the height changed',
        geom() == (w0, 40 * chei), geom())
    xdo('windowsize', w, str(w0 + 5 * cwid), str(40 * chei))
    time.sleep(0.8)
    s = screen()
    check('x11: a window made wider keeps its width and its grid',
        geom()[0] == w0 and nrows(s) == 40 and
        all(len(l) <= 80 for l in s.split('\n')), (geom(), s))
    xdo('windowsize', w, str(w0 - 10 * cwid), str(40 * chei))
    time.sleep(0.8)
    check('x11: a window made narrower keeps its width too',
        geom()[0] == w0 and nrows(screen()) == 40, geom())
    xdo('windowsize', w, str(w0), str(2 * chei))
    s = wait(lambda s: nrows(s) == 20)
    check('x11: the window is not made lower than 20 rows', nrows(s) == 20, s)
    xdo('windowsize', w, str(w0), '5000')
    time.sleep(1.0)
    s = screen()
    check('x11: the window is not made taller than the screen',
        nrows(s) * chei <= 800 and nrows(s) >= 40, nrows(s))
    xdo('windowsize', w, str(w0), str(30 * chei))
    s = wait(lambda s: nrows(s) == 30)
    check('x11: a window made as it was draws the first screen again',
        nrows(s) == 30 and rows_of(s)[-2].startswith('F1: Help'), s)
    key(w, 'ctrl+q'); time.sleep(0.5)
    del env['STRATA_X11_GEOM']

    # The height that the user gives the window is kept in the settings
    # (key x11_rows) and the next window opens at that height.
    rows_home = os.path.join(tmp, 'rows-home')
    saved_home = env['STRATA_HOME']
    env['STRATA_HOME'] = rows_home
    rows_cfg = os.path.join(rows_home, 'config')
    def rows_text():
        try: return open(rows_cfg).read()
        except OSError: return None
    os.makedirs(rows_home, mode=0o700)
    with open(rows_cfg, 'w') as f:
        f.write('backup=yes\nshow_dots=yes\n')
    os.chmod(rows_cfg, 0o600)
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc]);
    w = win('stred')
    s = wait(lambda s: nrows(s) == 50)
    w0, h0 = geom(); chei = h0 // 50
    check('x11: without x11_rows the window opens with the default 50 rows',
        nrows(s) == 50, nrows(s))
    xdo('windowsize', w, str(w0), str(34 * chei))
    s = wait(lambda s: nrows(s) == 34)
    time.sleep(1.6)
    check('x11: a height that has settled is kept in the settings at once',
        rows_text() == 'backup=yes\nshow_dots=yes\nx11_rows=34\n',
        rows_text())
    xdo('windowsize', w, str(w0), str(36 * chei))
    s = wait(lambda s: nrows(s) == 36)
    key(w, 'ctrl+q'); p.wait(5)
    check('x11: a height that has not settled is kept when the run ends',
        rows_text() == 'backup=yes\nshow_dots=yes\nx11_rows=36\n',
        rows_text())
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc]);
    w = win('stred')
    s = wait(lambda s: nrows(s) == 36)
    check('x11: the next Editor opens at the kept height', nrows(s) == 36,
        nrows(s))
    key(w, 'ctrl+q'); p.wait(5)
    before = rows_text()
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc]);
    w = win('stred')
    time.sleep(1.5)
    key(w, 'ctrl+q'); p.wait(5)
    check('x11: a run in which the height did not change writes nothing',
        rows_text() == before, rows_text())
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc]);
    w = win('stred')
    xdo('windowsize', w, str(w0), str(2 * chei))
    s = wait(lambda s: nrows(s) == 20)
    key(w, 'ctrl+q'); p.wait(5)
    check('x11: a height below 20 rows is written as 20',
        'x11_rows=20\n' in (rows_text() or ''), rows_text())
    before = rows_text()
    env['STRATA_X11_GEOM'] = '80x25'
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc]);
    w = win('stred')
    s = wait(lambda s: nrows(s) == 25)
    check('x11: STRATA_X11_GEOM wins over the kept height', nrows(s) == 25,
        nrows(s))
    xdo('windowsize', w, str(w0), str(30 * chei))
    s = wait(lambda s: nrows(s) == 30)
    time.sleep(1.6)
    key(w, 'ctrl+q'); p.wait(5)
    check('x11: a run with STRATA_X11_GEOM does not change the kept height',
        rows_text() == before, rows_text())
    del env['STRATA_X11_GEOM']
    for text, want, name in (
            ('x11_rows=5\n', 20, 'below 20 rows: 20 rows'),
            ('x11_rows=abc\n', 50, 'not a number: the default'),
            ('x11_rows=5000\n', None, 'above the screen: the screen')):
        with open(rows_cfg, 'w') as f:
            f.write(text)
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc]);
        w = win('stred')
        time.sleep(1.0)
        s = screen()
        if want is None:
            check('x11: x11_rows %s' % name, 20 <= nrows(s) <= 800 // chei
                and nrows(s) >= 40, nrows(s))
        else:
            check('x11: x11_rows %s' % name, nrows(s) == want, nrows(s))
        key(w, 'ctrl+q'); p.wait(5)
    # The file cannot be replaced: a directory has its name.
    os.remove(rows_cfg)
    os.mkdir(rows_cfg)
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc]);
    w = win('stred')
    s = wait(lambda s: nrows(s) == 50)
    xdo('windowsize', w, str(w0), str(44 * chei))
    s = wait(lambda s: nrows(s) == 44)
    time.sleep(1.6)
    key(w, 'ctrl+q'); p.wait(5)
    check('x11: a settings file that cannot be written is no failure; the '
        'editor ends with status 0 and the window stayed usable',
        p.returncode == 0 and os.path.isdir(rows_cfg) and nrows(s) == 44,
        (p.returncode, nrows(s)))
    env['STRATA_HOME'] = saved_home

    # No help file: Ctrl-F1 is still Learn, and F1 says that none was found.
    saved_help = env.pop('STRATA_HELP', None)
    # The program finds the help file of the build directory, so a copy of it
    # in another directory is the program that finds no help file.
    alone = os.path.join(tmp, 'stred-alone')
    shutil.copy(os.path.join(bindir, 'stred'), alone)
    p = start([alone, '-H', os.path.join(tmp,
        'no-such-help-file.hlp'), doc]);
    w = win('stred')
    key(w, 'ctrl+F1')
    s = wait(lambda s: 'Define Macro' in s or 'Help file not found' in s)
    check('x11: Ctrl-F1 is Learn when there is no help file, with no error',
        'Define Macro' in s and 'Help file not found' not in s, s)
    key(w, 'Escape'); time.sleep(0.4)
    key(w, 'F1')
    s = wait(lambda s: 'Help file not found' in s)
    check('x11: F1 says that no help file was found when there is none',
        'Help file not found' in s and 'Define Macro' not in s, s)
    key(w, 'Escape'); key(w, 'ctrl+q'); time.sleep(0.5)
    if saved_help is not None:
        env['STRATA_HELP'] = saved_help
    # Undo and Redo (src/undo.c): Ctrl-Z and Ctrl-F11 in the X11 version.
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc]);
    w = win('stred')
    key(w, 'h', 'e', 'l', 'l', 'o'); time.sleep(0.4); s = screen()
    check('x11: undo: typed text is shown', 'helloOrchard Plan' in s or
        'hello' in s, s)
    key(w, 'ctrl+z'); s = wait(lambda s: 'Undone: Typing' in s)
    check('x11: undo: Ctrl-Z takes the typing back and says so',
        'Undone: Typing' in s and 'hello' not in s, s)
    key(w, 'ctrl+F11'); s = wait(lambda s: 'Redone: Typing' in s)
    check('x11: undo: Ctrl-F11 does it again', 'Redone: Typing' in s and
        'hello' in s, s)
    key(w, 'ctrl+F10'); s = wait(lambda s: 'Undone: Typing' in s)
    check('x11: undo: Ctrl-F10 is Undo too', 'Undone: Typing' in s, s)
    key(w, 'ctrl+z'); s = wait(lambda s: 'Nothing to undo' in s)
    check('x11: undo: nothing more to undo', 'Nothing to undo' in s, s)
    # The same keys as in a terminal: Ctrl-/ is Undo, Alt-/ is Redo, and
    # Alt-Shift-minus (Alt-_) types a soft hyphen.
    key(w, 'alt+slash'); s = wait(lambda s: 'Redone: Typing' in s)
    check('x11: Alt-/ is Redo', 'Redone: Typing' in s and 'hello' in s, s)
    key(w, 'ctrl+slash'); s = wait(lambda s: 'Undone: Typing' in s)
    check('x11: Ctrl-/ is Undo (the same key as the byte 1F of a terminal)',
        'Undone: Typing' in s and 'hello' not in s, s)
    key(w, 'a', 'b', 'alt+shift+minus', 'c'); time.sleep(0.4); s = screen()
    check('x11: Alt-Shift-minus types a soft hyphen (the dim triangle)',
        'ab\u25bccOrchard' in s or 'ab\u25bcc' in s, s)
    for _ in range(6):
        key(w, 'ctrl+slash')
    s = wait(lambda s: 'Nothing to undo' in s)
    key(w, 'ctrl+q'); time.sleep(0.5)
    # Search and Replace options (src/search.c): the panels, the question at
    # each place and Undo, in the X11 version.
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc]);
    w = win('stred')
    key(w, 'F9'); s = wait(lambda s: 'Match Case' in s)
    check('x11: search: the panel has Match Case, Whole Word and Attribute',
        all(x in s for x in ('Search for', 'Find', 'Direction', 'Match Case',
        'Whole Word', 'Attribute')), s)
    key(w, 'Escape'); time.sleep(0.3)
    key(w, 'alt+F9'); s = wait(lambda s: 'Ask at Each' in s)
    check('x11: replace: the panel has the options and Ask at Each',
        all(x in s for x in ('Search for', 'Replace with', 'Find',
        'Match Case', 'Whole Word', 'Attribute', 'Replacement Case',
        'Ask at Each')), s)
    key(w, 'h', 'e', 'r', 'o', 'n', 'Down', 'h', 'a', 'r', 'b', 'o', 'r',
        'Down', 'Down', 'Left', 'Insert')
    s = wait(lambda s: 'R replace' in s)
    check('x11: replace: it prompts at the first place, with the keys',
        'R replace' in s and 'A all' in s and 'Q quit' in s, s)
    key(w, 'r'); s = wait(lambda s: 'harbor Hill Orchard' in s)
    check('x11: replace: R changes the place', 'harbor Hill Orchard' in s, s)
    if 'R replace' in s:
        key(w, 'q')

    s = wait(lambda s: 'replacement' in s)
    check('x11: replace: the run ends with the count', '1 replacement' in s,
        s)
    key(w, 'ctrl+z'); s = wait(lambda s: 'Undone' in s)
    check('x11: replace: Undo takes the Replace back',
        'Heron Hill Orchard' in s and 'harbor Hill Orchard' not in s, s)
    key(w, 'ctrl+q'); time.sleep(0.5)
    cwd = tmp
    p = start([os.path.join(bindir, 'str'), '-H', helpf], cwd=cwd);
    w = win('str')
    s = screen()
    check('x11: str shows the Document Manager', 'Strata Document '
        'Manager' in s and 'Press Ins to Select Action' in s, s)
    check('x11: the menu has the one Edit / Preview / Print Document entry',
        'Edit / Preview / Print Document' in s and
        s.count('Print Document') == 1 and
        all(x not in s for x in ('Preview Document', 'Check Document '
        'Spelling', 'Batch Print', 'Edit Document')), s)
    check('x11: the menu has the entry Restore Backup', 'Restore Backup' in s
        and 'Compare Document Versions' in s and 'Setup' in s, s)
    key(w, 'Down', 'Return');
    s = wait(lambda s: 'Spelling, your words' in s)
    check('x11: Maintain Dictionaries opens the panel of the word list',
        'Spelling, your words' in s and 'Ins Add' in s and
        'is not implemented in this version' not in s, s)
    key(w, 'Escape')
    key(w, 'Home', 'Return')
    s = wait(lambda s: 'Variables:' in s)
    check('x11: the Edit selection has the field Variables',
        'Variables:' in s and 'Working Directory:' in s, s)
    key(w, 'Down', 'F2')
    s = wait(lambda s: 'Name 1:' in s)
    check('x11: F2 on Variables opens the panel with a name and a value',
        'Name 1:' in s and 'Value 1:' in s, s)
    key(w, 'Escape'); key(w, 'Up'); typ(w, 'a'); key(w, 'Insert')
    ew = win('stred'); s = wait(lambda s: 'STRUCTURED EDIT' in s)
    check('x11: str opens the editor in a second window', ew is not None
        and 'Orchard Plan' in s, s)
    key(ew, 'ctrl+q'); s = wait(lambda s: 'Strata Document Manager' in s)
    check('x11: closing the editor returns to str', 'Strata Document '
        'Manager' in s, s)
    key(w, 'End', 'Return'); time.sleep(0.6)
    check('x11: Quit exits str', p.poll() is not None)

    # Compare Document Versions in the X11 version of the Document Manager.
    cdir = os.path.join(tmp, 'cmpx')
    os.makedirs(cdir, exist_ok=True)
    ca = os.path.join(cdir, 'a.doc')
    cb = os.path.join(cdir, 'b.doc')
    shutil.copy(doc, ca)
    with open(ca, 'rb') as f:
        cdata = f.read()
    with open(cb, 'wb') as f:
        f.write(cdata.replace(b'Pomologist', b'Pomologisa'))
    p = start([os.path.join(bindir, 'str'), '-H', helpf], cwd=cdir)
    w = win('str')
    s = screen()
    key(w, 'Down', 'Down', 'Return')
    s = wait(lambda s: 'Second Document' in s)
    check('x11: Compare Document Versions opens its panel with two documents',
        'Compare Document Versions' in s and 'First Document' in s and
        'Report Document' in s and 'is not implemented' not in s, s)
    key(w, 'Down'); typ(w, ca); key(w, 'Down'); typ(w, cb)
    key(w, 'Insert')
    s = wait(lambda s: 'Comparison of two documents' in s)
    check('x11: Compare Document Versions shows the words that changed',
        '[-Pomologist-]' in s and '[+Pomologisa+]' in s, s)
    key(w, 'Escape')
    s = wait(lambda s: 'Save the comparison' in s)
    check('x11: the report can be saved as a document',
        'Save the comparison as a document?' in s, s)
    key(w, 'Escape'); time.sleep(0.4)
    key(w, 'Escape')
    s = wait(lambda s: 'Strata Document Manager' in s)
    check('x11: leaving the panel returns to the menu', 'Strata Document '
        'Manager' in s and not os.path.exists(os.path.join(cdir,
        'a-diff.doc')), s)
    key(w, 'End', 'Return'); time.sleep(0.6)
    check('x11: Quit exits str after a comparison', p.poll() is not None)

    # F12, Print, Final makes a PDF and sends it with lp (a stand-in).
    if shutil.which('pdflatex') and shutil.which('pdfinfo'):
        lplog = os.path.join(tmp, 'lp.log')
        env.update(STRATA_LP=os.path.join(root, 'tests', 'fakelp.sh'),
            STRATA_LPSTAT=os.path.join(root, 'tests', 'fakelpstat.sh'),
            FAKELP_LOG=lplog)
        pdoc = os.path.join(tmp, 'pr.doc'); shutil.copy(doc, pdoc)
        pout = os.path.join(tmp, 'pr.pdf')
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, pdoc])
        w = win('stred')
        # F12 is sent as the key code 96 through XTest to the focused
        # window: a key sent by name can carry a stray Alt on some servers.
        xdo('windowfocus', w); time.sleep(0.3)
        xdo('keydown', '96'); xdo('keyup', '96'); time.sleep(0.3)
        key(w, 'p'); key(w, 'Right')
        key(w, 'Return')
        s = wait(lambda s: 'Print Document' in s)
        check('x11: F12, Print, Final shows the panel with a PDF as output',
            'Output File:' in s and 'Printer:' in s and 'pr.pdf' in s, s)
        typ(w, pout); key(w, 'Down')
        # The Printer field is a choice field: the space bar steps through
        # its entries (File only, PDF viewer, each queue, ...) until the
        # queue of the stand-in is the chosen one.
        for _step in range(8):
            s = wait(lambda s: 'Printer:' in s)
            line = [x for x in s.split('\n') if 'Printer:' in x][0]
            if 'laser' in line:
                break
            key(w, 'space')
        key(w, 'Insert')
        s = wait(lambda s: 'Sent to laser' in flat(s), 90.0)
        check('x11: Final sends the pages to the queue and says so, with no '
            'PDF kept', 'Sent to laser: 4 pages' in flat(s) and
            'No PDF was kept' in flat(s), s)
        lg = open(lplog).read() if os.path.exists(lplog) else ''
        # The sample sets Page Break Level 2: four pages, not two.  A print
        # to a queue makes no file of its own.
        check('x11: lp got the queue and no PDF is left',
            '|-d|laser|-n|1|' in lg and not os.path.exists(pout), lg)
        p.terminate(); p.wait(5)
        # PDF viewer (a block of its own): the stand-in opener of the
        # setting pdf_viewer records the call.
        oplog = os.path.join(tmp, 'open.log')
        fakesafe = os.path.join(tmp, 'fakeopen.sh')
        shutil.copy(os.path.join(root, 'tests', 'fakeopen.sh'), fakesafe)
        os.chmod(fakesafe, 0o755)
        # The panel kept the queue of the print before: forget it, so that
        # the list below starts from File only as the keys expect.
        cfgp = os.path.join(tmp, 'strata-home', 'config')
        with open(cfgp) as f:
            kept = [l for l in f.read().split('\n')
                    if not l.startswith('print_last_')]
        with open(cfgp, 'w') as f:
            f.write('\n'.join(kept) + '\n')
        with open(os.path.join(tmp, 'strata-home', 'config'), 'a') as f:
            f.write('pdf_viewer=%s\n' % fakesafe)
        os.chmod(os.path.join(tmp, 'strata-home', 'config'), 0o600)
        env.update(FAKEOPEN_LOG=oplog)
        odoc = os.path.join(tmp, 'op.doc'); shutil.copy(doc, odoc)
        oout = os.path.join(tmp, 'op.pdf')
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, odoc])
        w = win('stred')
        xdo('windowfocus', w); time.sleep(0.3)
        xdo('keydown', '96'); xdo('keyup', '96'); time.sleep(0.3)
        key(w, 'p'); key(w, 'Right')
        key(w, 'Return')
        s = wait(lambda s: 'Print Document' in s)
        typ(w, oout); key(w, 'Down'); key(w, 'F2'); key(w, 'Down')
        key(w, 'Return'); key(w, 'Insert')
        s = wait(lambda s: 'and opened it to print' in s, 90.0)
        time.sleep(0.8)
        lg = open(oplog).read() if os.path.exists(oplog) else ''
        check('x11: Final with PDF viewer makes the PDF and opens it',
            'and opened it to print' in s and
            ('fakeopen.sh|%s size=' % oout) in lg, s + lg)
        p.terminate(); p.wait(5)
        # F12, File, Export offers Export a LaTeX File and writes the file of
        # Print and Preview, with its header.
        xdoc = os.path.join(tmp, 'xl.doc'); shutil.copy(doc, xdoc)
        xtex = os.path.join(tmp, 'xl.tex')
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, xdoc])
        w = win('stred')
        xdo('windowfocus', w); time.sleep(0.3)
        xdo('keydown', '96'); xdo('keyup', '96'); time.sleep(0.3)
        key(w, 'Return')
        for _ in range(5): key(w, 'Right')
        key(w, 'Return')
        s = wait(lambda s: 'Export ASCII Text File' in s)
        check('x11: F12, File, Export shows the panel', 'Export a Screen '
            'Image' in s, s)
        key(w, 'Down'); typ(w, xtex); key(w, 'Down'); key(w, 'Right')
        key(w, 'Right')
        s = wait(lambda s: 'Export a LaTeX File' in s)
        check('x11: the Export panel offers Export a LaTeX File',
            'Export a LaTeX File' in s, s)
        key(w, 'Insert')
        s = wait(lambda s: 'Exported' in s, 60.0)
        xsrc = open(xtex, errors='replace').read() if os.path.exists(xtex) \
            else ''
        check('x11: Export a LaTeX File writes the LaTeX file with its header',
            'Exported' in s and xsrc.startswith('% LaTeX file exported by '
            'Strata.\n% Source document: xl.doc\n') and
            '\\end{document}' in xsrc, s + xsrc[:200])
        p.terminate(); p.wait(5)
        # Export a Screen Image of a document with a border draws the frame as
        # the window shows it: the rows of the file are rows of the screen.
        crl = b'\r\n'
        bdoc = os.path.join(tmp, 'xb.doc')
        with open(bdoc, 'wb') as f:
            f.write(b'*R3' + crl + b'MR1001' + crl + b'PP\\1\\\x00' + crl +
                b'FHA1' + crl + b'DP0,8500,11000' + crl + b'*B0' + crl +
                b'S' + crl + b'CB85,1' + crl + b'CL1' + crl + b'CR1' + crl +
                b'TLeft cell\x1d\x00' + crl + b'B' + crl +
                b'TSecond block\x1d\x00' + crl + b'*E' + crl)
        xtxt = os.path.join(tmp, 'xb.txt')
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, bdoc])
        w = win('stred')
        xdo('windowfocus', w); time.sleep(0.3)
        s0 = wait(lambda s: 'Left cell' in s)
        xdo('keydown', '96'); xdo('keyup', '96'); time.sleep(0.3)
        key(w, 'Return')
        for _ in range(5): key(w, 'Right')
        key(w, 'Return')
        s = wait(lambda s: 'Export ASCII Text File' in s)
        key(w, 'Down'); typ(w, xtxt); key(w, 'Insert')
        s = wait(lambda s: 'Exported' in s, 60.0)
        time.sleep(0.5)
        raw = open(xtxt, 'rb').read() if os.path.exists(xtxt) else b''
        ex = [l.decode('cp437').rstrip() for l in raw.split(b'\r\n')]
        sc = [l.replace('\u25c4', ' ').rstrip() for l in s0.split('\n')]
        k = next((i for i, l in enumerate(sc) if ex[0] != '' and l == ex[0]), -1)
        check('x11: Export a Screen Image has the frame of the border, the '
            'rows are those of the window', raw.startswith(b'\xda' + b'\xc4' *
            78 + b'\xbf\r\n\xb3Left cell') and k >= 0 and
            sc[k:k + 4] == ex[:4], (ex[:5], sc[:8]))
        p.terminate(); p.wait(5)
        # The box Check Validity is active for the LaTeX file only and, when
        # it is Yes, LaTeX is run on the export and accepts it.
        ctex = os.path.join(tmp, 'xc.tex')
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, xdoc])
        w = win('stred')
        xdo('windowfocus', w); time.sleep(0.3)
        xdo('keydown', '96'); xdo('keyup', '96'); time.sleep(0.3)
        key(w, 'Return')
        for _ in range(5): key(w, 'Right')
        key(w, 'Return')
        s = wait(lambda s: 'Export ASCII Text File' in s)
        check('x11: the Export panel has the Check Validity box inactive (in '
            'its place, no brackets) for a screen image',
            'Check Validity' in s and 'Check Validity: [' not in s, s)
        key(w, 'Down'); typ(w, ctex); key(w, 'Down'); key(w, 'Right')
        key(w, 'Right')
        s = wait(lambda s: 'Check Validity' in s)
        check('x11: the box Check Validity is shown for the LaTeX file, '
            'not checked', 'Check Validity' in s and '[No]' in s, s)
        # The document is structured: the box comes after the Scope field.
        key(w, 'Down'); key(w, 'Down'); key(w, 'Down'); key(w, 'Right')
        s = wait(lambda s: '[Yes]' in s)
        check('x11: the box Check Validity can be set to Yes', '[Yes]' in s, s)
        key(w, 'Insert')
        s = wait(lambda s: 'LaTeX accepts it' in s, 90.0)
        check('x11: a checked export runs LaTeX and says that it accepts '
            'the file', 'LaTeX accepts it' in s and os.path.exists(ctex), s)
        p.terminate(); p.wait(5)
    # The Print menu has more entries than fit in 80 columns: the entries
    # continue on a second row, every entry is whole, and the label of the
    # mode is alone at the end of the first row.
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc])
    w = win('stred')
    key(w, 'F12'); key(w, 'p')
    s = wait(lambda s: 'MENU-PRINT' in s)
    rows = s.split('\n')
    check('x11: the Print menu bar continues on a second row and the label '
        'MENU-PRINT is whole at the end of the first row',
        rows[0].rstrip().endswith('MENU-PRINT') and
        rows[0].split('MENU-PRINT')[0].rstrip().endswith('Remove Error') and
        rows[1].strip() == 'Remove Print Errors' and
        'Print document in draft quality' in rows[2], '\n'.join(rows[:4]))
    p.terminate(); p.wait(5)



    # ---- the colors of the screen ----------------------------------------
    # A setting changes the pixels of the window: the color of a cell is read
    # from the picture that the X server holds.
    if shutil.which('xwd') is not None:
        saved_home = env['STRATA_HOME']
        def colhome(text=None):
            h = os.path.join(tempfile.mkdtemp(dir=tmp), 'strata')
            os.mkdir(h, 0o700)
            if text is not None:
                open(os.path.join(h, 'config'), 'w').write(text)
                os.chmod(os.path.join(h, 'config'), 0o600)
            return h
        def str_window():
            q = start([os.path.join(bindir, 'str'), '-H', helpf])
            return q, win('str')
        # The window is 30 rows high, so that all of it is on the display.
        env['STRATA_HOME'] = colhome('x11_rows=30\n')
        p, w = str_window(); time.sleep(0.5)
        check('x11 colors: the default is yellow text on a blue background '
            '(the page is blue)', pixel(w, 10, 10) == 0x0000AA,
            hex(pixel(w, 10, 10)))
        xdo('windowfocus', w)
        key(w, 'End', 'Up', 'Return'); time.sleep(0.6)
        check('x11 colors: the message line is black on light grey',
            pixel(w, 3, -4) == 0xAAAAAA, hex(pixel(w, 3, -4)))
        p.terminate(); p.wait(5)
        env['STRATA_HOME'] = colhome('x11_rows=30\n'
            'x11_color_normal_bg=#336699\n'
            'terminal_color_normal_bg=#ff0000\n'
            'x11_color_message_bg=#aa3333\n')
        p, w = str_window(); time.sleep(0.5)
        check('x11 colors: a setting changes the pixels of the window, the '
            'setting of the terminal does not', pixel(w, 10, 10) == 0x336699,
            hex(pixel(w, 10, 10)))
        xdo('windowfocus', w)
        key(w, 'End', 'Up', 'Return'); time.sleep(0.6)
        check('x11 colors: a setting of the message line changes its pixels',
            pixel(w, 3, -4) == 0xAA3333,
            hex(pixel(w, 3, -4)))
        p.terminate(); p.wait(5)
        env['STRATA_HOME'] = colhome('x11_color_normal_bg=nonsense\n')
        p, w = str_window(); time.sleep(0.5)
        check('x11 colors: a bad value gives the default color',
            pixel(w, 10, 10) == 0x0000AA, hex(pixel(w, 10, 10)))
        p.terminate(); p.wait(5)
        # The page of colors in Setup: a color that is chosen is shown at
        # once, Esc takes it back and Ins keeps it in the file.
        for keep in (0, 1):
            env['STRATA_HOME'] = colhome()
            p, w = str_window(); time.sleep(0.3)
            xdo('windowfocus', w)
            key(w, 'End', 'Up', 'Return'); time.sleep(0.4)
            key(w, 'Prior')
            s = wait(lambda s: 'Colors of the X11 version' in s)
            check('x11 colors: PgUp shows the page of colors for the X11 '
                'version', 'Colors of the X11 version' in s and
                'Sample text' in s and 'Setup (screen colors)' in s, s)
            key(w, 'Tab', 'Right', 'Right', 'Right'); time.sleep(0.6)
            check('x11 colors: the color that is chosen is on the screen at '
                'once (red)', pixel(w, 10, 10) == 0xAA0000,
                hex(pixel(w, 10, 10)))
            if keep == 0:
                key(w, 'Escape'); time.sleep(0.6)
                check('x11 colors: Esc takes the color back',
                    pixel(w, 10, 10) == 0x0000AA, hex(pixel(w, 10, 10)))
            else:
                key(w, 'Insert'); time.sleep(0.6)
                txt = open(os.path.join(env['STRATA_HOME'], 'config')).read() \
                    if os.path.exists(os.path.join(env['STRATA_HOME'],
                    'config')) else ''
                check('x11 colors: Ins keeps the color in the file, with '
                    'the keys of X11', 'x11_color_normal_bg=red\n' in txt
                    and 'terminal_color' not in txt, txt)
            p.terminate(); p.wait(5)
        env['STRATA_HOME'] = saved_home
    # Borders: a document with no border record gets a frame at once, and
    # loses it with Undo.  The cells are read from the text that the program
    # writes, and the pixels of the rows from the picture of the window.
    if shutil.which('xwd') is not None:
        pt = os.path.join(tmp, 'pt.doc')
        shutil.copy(os.path.join(root, 'tests', 'data', 'plain_text.doc'), pt)
        def band(w, y0, y1):
            """The lit pixels (not the background of the page) of a band."""
            data = subprocess.run(['xwd', '-id', w, '-silent'], env=env,
                capture_output=True).stdout
            h = struct.unpack('>25I', data[:100])
            base = h[0] + h[19] * 12
            bpp = h[11] // 8
            n = 0
            for y in range(y0, y1):
                row = data[base + y * h[12]:base + y * h[12] + h[4] * bpp]
                for x in range(h[4]):
                    if int.from_bytes(row[x * bpp:(x + 1) * bpp],
                            'little') != 0x0000AA:
                        n += 1
            return n
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, pt]); w = win('stred')
        xdo('windowfocus', w); time.sleep(0.5)
        s0 = screen()
        rows = [l for l in s0.split('\n')]
        geo = xdo('getwindowgeometry', w)
        wh = int(re.search(r'x(\d+)', geo.split('Geometry:')[1]).group(1))
        ch = wh // max(1, len(rows) - 1)
        before = band(w, 2 * ch, 3 * ch)
        check('x11 border: no box character on a screen with no border',
            not any(c in s0 for c in '\u2554\u2550\u2557\u2551\u255a\u255d'),
            s0)
        key(w, 'alt+F8'); wait(lambda s: 'highlight column' in s)
        key(w, 'Return'); wait(lambda s: 'Border Layout' in s)
        key(w, 'Right', 'Right', 'Right', 'Insert'); time.sleep(0.8)
        s1 = wait(lambda s: '\u2554' in s)
        r1 = s1.split('\n')
        top = next((i for i, l in enumerate(r1) if '\u2554' in l), -1)
        check('x11 border: All Double draws the frame: corners and sides at '
            'the right cells', top >= 0 and r1[top].startswith('\u2554\u2550') and
            r1[top].rstrip().endswith('\u2557') and
            r1[top + 1].startswith('\u2551One line') and
            r1[top + 1].rstrip().endswith('\u2551') and
            r1[top + 2].startswith('\u255a\u2550') and
            r1[top + 2].rstrip().endswith('\u255d'), s1)
        after = band(w, top * ch, (top + 1) * ch) if top >= 0 else 0
        check('x11 border: the window shows lit pixels on the top row of the '
            'frame that the same row did not have', top >= 0 and
            after > before + 1000, '%d lit pixels, before: %d' % (after, before))
        key(w, 'ctrl+F10'); time.sleep(0.8)
        s2 = wait(lambda s: '\u2554' not in s)
        check('x11 border: Undo takes the frame away', '\u2554' not in s2 and
            '\u2551' not in s2, s2)
        p.terminate(); p.wait(5)
        # The corners and the tees of a table are joined to the lines in the
        # pixels: the edge of the cell of a junction has ink where the edge
        # of the cell next to it has ink, in the directions that have a line,
        # and none in the directions that have not.
        def image(w):
            data = subprocess.run(['xwd', '-id', w, '-silent'], env=env,
                capture_output=True).stdout
            h = struct.unpack('>25I', data[:100])
            base = h[0] + h[19] * 12
            bpp = h[11] // 8
            o = base + (h[5] // 2) * h[12] + (h[4] // 2) * bpp
            bg = int.from_bytes(data[o:o + bpp], 'little' if h[7] == 0
                else 'big')
            def lit(x, y):
                o = base + y * h[12] + x * bpp
                v = int.from_bytes(data[o:o + bpp], 'little' if h[7] == 0
                    else 'big')
                return abs(((v >> 16) & 255) - ((bg >> 16) & 255)) + abs(
                    ((v >> 8) & 255) - ((bg >> 8) & 255)) + abs(
                    (v & 255) - (bg & 255)) > 120
            return h[4], h[5], lit
        def edge(lit, cx, cy, cw, chh, side):
            x0, y0 = cx * cw, cy * chh
            if side == 'L': return {y for y in range(chh) if lit(x0, y0 + y)}
            if side == 'R': return {y for y in range(chh)
                if lit(x0 + cw - 1, y0 + y)}
            if side == 'T': return {x for x in range(cw) if lit(x0 + x, y0)}
            return {x for x in range(cw) if lit(x0 + x, y0 + chh - 1)}
        CR = b'\r\n'
        def tcell(m):
            t = b'CB%d,1' % m + CR + b'CL1' + CR + b'CR1' + CR
            return t
        for m, (tl, tee, bl) in ((85, ('\u250c', '\u251c', '\u2514')),
                (170, ('\u2554', '\u2560', '\u255a'))):
            body = b'*B0' + CR
            for r in range(3):
                body += (b'S' if r == 0 else b'B') + CR
                body += b'CW14' + CR + tcell(m) + b'T' + b'a%d' % r + \
                    b'\x1d\x00' + CR + b'CW14' + CR + tcell(m) + b'T' + \
                    b'b%d' % r + b'\x1d\x00' + CR + tcell(m) + b'T' + \
                    b'c%d' % r + b'\x1d\x00' + CR
            jdoc = os.path.join(tmp, 'j%d.doc' % m)
            open(jdoc, 'wb').write(b'*R3' + CR + b'MR1001' + CR +
                b'PP\\1\\\x00' + CR + b'FHA1' + CR + b'DP0,8500,11000' + CR +
                body + b'*E' + CR)
            p = start([os.path.join(bindir, 'stred'), '-H', helpf, jdoc])
            w = win('stred')
            s = wait(lambda s: tl in s)
            time.sleep(0.5)
            lines = s.rstrip('\n').split('\n')
            row = next((i for i, l in enumerate(lines) if l.startswith(tl)),
                -1)
            iw, ih, lit = image(w)
            geo = xdo('getwindowgeometry', w)
            wh = int(re.search(r'x(\d+)', geo.split('Geometry:')[1]).group(1))
            cw, chh = iw // 80, wh // 50  # the window has 80 by 50 cells
            joined = row >= 0
            why = []
            if joined:
                # top left corner: right and down are joined, left and up not
                a = edge(lit, 0, row, cw, chh, 'R')
                b = edge(lit, 1, row, cw, chh, 'L')
                joined &= bool(a) and a == b
                a = edge(lit, 0, row, cw, chh, 'B')
                b = edge(lit, 0, row + 1, cw, chh, 'T')
                joined &= bool(a) and a == b
                joined &= not edge(lit, 0, row, cw, chh, 'L') and \
                    not edge(lit, 0, row, cw, chh, 'T')
                # the tee below the first row: up, down and right joined
                t = row + 2
                joined &= lines[t].startswith(tee)
                a = edge(lit, 0, t, cw, chh, 'T')
                joined &= bool(a) and a == edge(lit, 0, t - 1, cw, chh, 'B')
                a = edge(lit, 0, t, cw, chh, 'B')
                joined &= bool(a) and a == edge(lit, 0, t + 1, cw, chh, 'T')
                a = edge(lit, 0, t, cw, chh, 'R')
                joined &= bool(a) and a == edge(lit, 1, t, cw, chh, 'L')
                joined &= not edge(lit, 0, t, cw, chh, 'L')
                # the right tee at the end of the first column
                a = edge(lit, 15, t, cw, chh, 'L')
                joined &= bool(a) and a == edge(lit, 14, t, cw, chh, 'R')
                a = edge(lit, 15, t, cw, chh, 'T')
                joined &= bool(a) and a == edge(lit, 15, t - 1, cw, chh, 'B')
                a = edge(lit, 15, t, cw, chh, 'B')
                joined &= bool(a) and a == edge(lit, 15, t + 1, cw, chh, 'T')
                joined &= not edge(lit, 15, t, cw, chh, 'R')
                # the bottom left corner
                u = row + 6
                joined &= lines[u].startswith(bl)
                a = edge(lit, 0, u, cw, chh, 'T')
                joined &= bool(a) and a == edge(lit, 0, u - 1, cw, chh, 'B')
                a = edge(lit, 0, u, cw, chh, 'R')
                joined &= bool(a) and a == edge(lit, 1, u, cw, chh, 'L')
                joined &= not edge(lit, 0, u, cw, chh, 'B')
            check('x11 border: the corners and the tees of a table (%s) are '
                'joined to the lines in the pixels, no gap' % tl, joined,
                (row, cw, chh, wh, iw, ih, lines[:8]))
            p.terminate(); p.wait(5)
        # The table of the showcase has only left sides: the rule between
        # its blocks is the horizontal line, and the rule meets the sides in
        # tees and crosses that join in the pixels, with no gap.
        body = b'*B0' + CR + b'S' + CR + b'Tintro\x1d\x00' + CR
        for r in range(3):
            body += (b'B' + CR + b'CW12' + CR + b'CB16,1' + CR + b'CL1' + CR +
                b'T' + b'a%d' % r + b'\x1d\x00' + CR + b'CW12' + CR +
                b'CB16,1' + CR + b'T' + b'b%d' % r + b'\x1d\x00' + CR +
                b'CB16,1' + CR + b'T' + b'c%d' % r + b'\x1d\x00' + CR)
        body += b'B' + CR + b'Tafter\x1d\x00' + CR
        jdoc = os.path.join(tmp, 'jr.doc')
        open(jdoc, 'wb').write(b'*R3' + CR + b'MR1001' + CR +
            b'PP\\1\\\x00' + CR + b'FHA1' + CR + b'DP0,8500,11000' + CR +
            body + b'*E' + CR)
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, jdoc])
        w = win('stred')
        s = wait(lambda s: '\u250c' in s)
        time.sleep(0.5)
        lines = s.rstrip('\n').split('\n')
        row = next((i for i, l in enumerate(lines) if '\u250c' in l), -1)
        iw, ih, lit = image(w)
        geo = xdo('getwindowgeometry', w)
        wh = int(re.search(r'x(\d+)', geo.split('Geometry:')[1]).group(1))
        cw, chh = iw // 80, wh // 50
        ok = row >= 0 and '\u2534' in lines[row + 6]
        if ok:
            x0 = lines[row].index('\u250c')
            x1 = lines[row].index('\u252c')
            def J(x, y, side, nx, ny, nside):
                a = edge(lit, x, y, cw, chh, side)
                return bool(a) and a == edge(lit, nx, ny, cw, chh, nside)
            # top left corner: right and down joined, left and up not
            ok &= J(x0, row, 'R', x0 + 1, row, 'L')
            ok &= J(x0, row, 'B', x0, row + 1, 'T')
            ok &= not edge(lit, x0, row, cw, chh, 'T')
            # the top tee: left, right and down joined, up not
            ok &= J(x1, row, 'L', x1 - 1, row, 'R')
            ok &= J(x1, row, 'R', x1 + 1, row, 'L')
            ok &= J(x1, row, 'B', x1, row + 1, 'T')
            ok &= not edge(lit, x1, row, cw, chh, 'T')
            # the cross between the first and the second row: all four
            t = row + 2
            ok &= lines[t][x1] == '\u253c'
            ok &= J(x1, t, 'L', x1 - 1, t, 'R') and \
                J(x1, t, 'R', x1 + 1, t, 'L')
            ok &= J(x1, t, 'T', x1, t - 1, 'B') and \
                J(x1, t, 'B', x1, t + 1, 'T')
            # the bottom tee: up, left and right joined, down not
            u = row + 6
            ok &= J(x1, u, 'T', x1, u - 1, 'B')
            ok &= J(x1, u, 'L', x1 - 1, u, 'R') and \
                J(x1, u, 'R', x1 + 1, u, 'L')
            ok &= not edge(lit, x1, u, cw, chh, 'B')
        check('x11 border: the rule between blocks of a table with sides '
            'only meets the sides in tees and a cross, joined in the '
            'pixels', ok, (row, cw, chh, lines[:10]))
        p.terminate(); p.wait(5)
        # A column with a border next to one with none: the rule between
        # the two blocks goes on to the right of the bordered column, so
        # the seam is a cross whose four arms join the cells around it.
        body = b'*B0' + CR + b'S' + CR
        for r in range(2):
            body += ((b'B' + CR if r else b'') + b'CW14' + CR + b'CB85,1' +
                CR + b'CL1' + CR + b'CR1' + CR + b'T' + b'a%d' % r +
                b'\x1d\x00' + CR + b'CW14' + CR + b'T' + b'b%d' % r +
                b'\x1d\x00' + CR + b'T' + b'c%d' % r + b'\x1d\x00' + CR)
        jdoc = os.path.join(tmp, 'js.doc')
        open(jdoc, 'wb').write(b'*R3' + CR + b'MR1001' + CR +
            b'PP\\1\\\x00' + CR + b'FHA1' + CR + b'DP0,8500,11000' + CR +
            body + b'*E' + CR)
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, jdoc])
        w = win('stred')
        s = wait(lambda s: '\u253c' in s)
        time.sleep(0.5)
        lines = s.rstrip('\n').split('\n')
        row = next((i for i, l in enumerate(lines) if '\u253c' in l), -1)
        iw, ih, lit = image(w)
        geo = xdo('getwindowgeometry', w)
        wh = int(re.search(r'x(\d+)', geo.split('Geometry:')[1]).group(1))
        cw, chh = iw // 80, wh // 50
        ok = row >= 1
        if ok:
            x1 = lines[row].index('\u253c')
            def J(x, y, side, nx, ny, nside):
                a = edge(lit, x, y, cw, chh, side)
                return bool(a) and a == edge(lit, nx, ny, cw, chh, nside)
            ok &= J(x1, row, 'L', x1 - 1, row, 'R')
            ok &= J(x1, row, 'R', x1 + 1, row, 'L')
            ok &= J(x1, row, 'T', x1, row - 1, 'B')
            ok &= J(x1, row, 'B', x1, row + 1, 'T')
        check('x11 border: the seam between a bordered column and one with no '
            'border is a cross whose four arms join in the pixels', ok,
            (row, cw, chh, lines[:8]))
        # The Editor of this block must be gone before the next block looks
        # for the window of its own Editor by name.
        p.terminate(); p.wait(5)
    # A Picture marker whose file is missing is drawn dim, with the reason:
    # in the window its text is darker than the text of a marker whose file
    # is there (the document shows both in the Short mode, so that the good
    # one is text too).
    if shutil.which('xwd') is not None:
        CR = b'\r\n'
        shutil.copy(os.path.join(root, 'tests', 'data', 'picture.png'),
            os.path.join(tmp, 'okpic.png'))
        def pmark(n):
            return (b'\x1325(1' + n + b'\x00' + CR + b'22000' + CR +
                b'\x13)')
        pbody = b'*B0' + CR + b'S' + CR
        for k, b in enumerate([b'Before\x1d', pmark(b'okpic.png') + b'\x1d',
                pmark(b'nopic.png') + b'\x1d', b'After\x1d']):
            pbody += (b'' if k == 0 else b'B' + CR) + b'T' + b + b'\x00' + CR
        pdoc = os.path.join(tmp, 'pm.doc')
        open(pdoc, 'wb').write(b'*R3' + CR + b'MR1001' + CR +
            b'PP\\1\\\x00' + CR + b'FHA1' + CR + b'OM25,2' + CR +
            b'DP0,8500,11000' + CR + pbody + b'*E' + CR)
        p = start([os.path.join(bindir, 'stred'), '-H', helpf, pdoc])
        w = win('stred')
        s = wait(lambda s: 'nopic.png' in s)
        time.sleep(0.5)
        lines = s.rstrip('\n').split('\n')
        r_ok = next((i for i, l in enumerate(lines)
            if '[PIC okpic.png]' in l), -1)
        r_no = next((i for i, l in enumerate(lines)
            if 'nopic.png: file not found]' in l), -1)
        data = subprocess.run(['xwd', '-id', w, '-silent'], env=env,
            capture_output=True).stdout
        h = struct.unpack('>25I', data[:100])
        base = h[0] + h[19] * 12
        bpp = h[11] // 8
        geo = xdo('getwindowgeometry', w)
        wh = int(re.search(r'x(\d+)', geo.split('Geometry:')[1]).group(1))
        cw, chh = h[4] // 80, wh // 50
        def brightest(row, col0, ncols):
            best = 0
            for y in range(row * chh, (row + 1) * chh):
                for x in range(col0 * cw, (col0 + ncols) * cw):
                    o = base + y * h[12] + x * bpp
                    v = int.from_bytes(data[o:o + bpp], 'little'
                        if h[7] == 0 else 'big')
                    best = max(best, ((v >> 16) & 255) + ((v >> 8) & 255) +
                        (v & 255))
            return best
        if r_ok >= 0 and r_no >= 0:
            c_ok = lines[r_ok].index('[PIC okpic')
            c_no = lines[r_no].index('[PIC nopic')
            b_ok = brightest(r_ok, c_ok, len('[PIC okpic.png]'))
            b_no = brightest(r_no, c_no, len('[PIC nopic.png: file not '
                'found]'))
        else:
            b_ok = b_no = 0
        check('x11 picture: the marker of a missing file is drawn dimmer than '
            'the marker of a file that is there', r_ok >= 0 and r_no >= 0 and
            b_ok > 0 and b_no > 0 and b_no < b_ok * 0.85,
            (r_ok, r_no, b_ok, b_no, lines[:8]))
        key(w, 'Down'); key(w, 'Down')
        s = wait(lambda s: 'Picture file not found' in s)
        check('x11 picture: the message line shows so when the cursor is on '
            'the marker', 'Picture file not found: nopic.png' in flat(s), s)
        p.terminate(); p.wait(5)
finally:
    for q in procs:
        if q.poll() is None: q.terminate()
    xvfb.terminate(); shutil.rmtree(tmp, ignore_errors=True)
print('x11 smoke: %s' % ('all passed' if not fails
    else '%d FAILURES' % fails)); sys.exit(1 if fails else 0)
