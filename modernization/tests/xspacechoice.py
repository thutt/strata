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

"""The space bar in the X11 Editor and Document Manager: Space takes the next
value of a field that has a fixed set of values (docs/SPACE-CHOICE-AUDIT.txt).
Two panels: Print Double Sided? of the Print panel of the Editor, and Family
and Install Now of TeX Font Association of the Document Manager.  The
Editor and the Document Manager run under Xvfb, the keys come from xdotool and
the screen is the text that the program writes to $STRATA_X11_DUMP.  No printer
is used and nothing is sent anywhere."""
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


def field(s, label):
    for l in s.split('\n'):
        if label in l:
            t = l.split(label, 1)[1].lstrip(': ')
            m = re.match(r'\[([^\]]*)\]', t)
            return m.group(1) if m else re.split('\u00b7|\u2551| {3,}', t)[0].strip()
    return None


try:
    p = start([os.path.join(bindir, 'stred'), '-H', helpf, doc])
    w = win('stred')
    xdo('windowfocus', w); time.sleep(0.3)
    xdo('keydown', '96'); xdo('keyup', '96'); time.sleep(0.3)
    key(w, 'p'); key(w, 'Right')
    key(w, 'Return')
    s = wait(lambda s: 'Print Document' in s)
    for _ in range(3):
        key(w, 'Down')
    vals = [field(wait(lambda s: 'Print Double Sided?' in s),
                  'Print Double Sided?')]
    for _ in range(3):
        key(w, 'space'); time.sleep(0.4)
        vals.append(field(screen(), 'Print Double Sided?'))
    check('x11: Print Double Sided?: Space steps Printer default, Yes, No '
          'and goes round', vals == ['Printer default', 'Yes', 'No',
          'Printer default'], vals)
    key(w, 'Escape')
    p.terminate(); p.wait(5)

    p = start([os.path.join(bindir, 'str'), '-H', helpf], cwd=tmp)
    w = win('str')
    xdo('windowfocus', w); time.sleep(0.3)
    for _ in range(4):
        key(w, 'Down')
    key(w, 'Return')
    s = wait(lambda s: 'TeX Font Association' in s and 'Latin Modern' in s,
             60.0)
    for _ in range(19):
        key(w, 'Down')
    key(w, 'Return')
    s = wait(lambda s: 'Family (roman, sans or mono)' in s, 20.0)
    for _ in range(4):
        key(w, 'Down')
    fam = [field(screen(), 'Family (roman, sans or mono)')]
    for _ in range(3):
        key(w, 'space'); time.sleep(0.4)
        fam.append(field(screen(), 'Family (roman, sans or mono)'))
    check('x11: Family: Space steps roman, sans, mono and goes round',
          fam == ['roman', 'sans', 'mono', 'roman'], fam)
    for _ in range(4):
        key(w, 'Down')
    inst = [field(screen(), 'Install Now (Yes or No)')]
    for _ in range(2):
        key(w, 'space'); time.sleep(0.4)
        inst.append(field(screen(), 'Install Now (Yes or No)'))
    check('x11: Install Now: Space gives Yes and then No', inst == ['No',
          'Yes', 'No'], inst)
    key(w, 'Escape')
    p.terminate(); p.wait(5)
finally:
    for q in procs:
        if q.poll() is None:
            q.terminate()
    xvfb.terminate()
    shutil.rmtree(tmp, ignore_errors=True)
print('xspacechoice: %d failure(s)' % fails)
sys.exit(1 if fails else 0)
