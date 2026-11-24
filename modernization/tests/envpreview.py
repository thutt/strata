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

"""The orientation of an envelope in the Preview.

The Editor makes a DVI file with strprint and starts the Viewer on it (here
a fake viewer keeps the DVI and the part of the document that was printed).
A landscape sheet is a DVI that says papersize with the width above the
length; a page that is turned by an orientation marker carries the special
pdf: put @thispage with /Rotate 90.  The checks look at what the Preview
makes for every sample envelope, for an envelope chosen in the Page panel,
for a block of a document after a landscape marker (the part that is printed
has no marker of its own), and, in an X display, at the window of the real
Viewer.
"""
import sys as _s; _s.dont_write_bytecode = True
import atexit, glob, os, re, shutil, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
dviview = os.environ.get('DVIVIEW', os.path.join(build, 'x11', 'dviview'))
if not os.path.exists(stred):
    print('SKIP: build the programs first')
    sys.exit(0)
if not shutil.which('latex'):
    print('SKIP: latex is not installed')
    sys.exit(0)

tmp = tempfile.mkdtemp(prefix='envprev')
atexit.register(shutil.rmtree, tmp, True)
os.environ['STRATA_HOME'] = os.path.join(tmp, 'home')
os.makedirs(os.environ['STRATA_HOME'], mode=0o700)
os.environ['STRATA_DVIVIEW'] = os.path.join(here, 'fakeview.sh')
fails = 0
count = 0

samples = os.path.join(root, 'samples', 'envelopes')
FINAL = ['F12', 'text:P', 'text:P', 'ENTER', 'INS']
BLOCK = ['F12', 'text:P', 'text:P', 'RIGHT', 'ENTER', 'INS']
WAIT = ['wait'] * 5
PANEL = ['F12', 'text:G', 'RIGHT', 'ENTER', 'wait']
LANDSCAPE = b'T\x1336(12\r\n\x13)\x1d\x00\r\nB\r\n'
PORTRAIT = b'T\x1336(11\r\n\x13)\x1d\x00\r\nB\r\n'


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
        + str(extra)))
    fails += 0 if cond else 1


def preview(src, keys, name):
    """Run the Preview of the document src with the keys; return the DVI
    bytes and the part that was printed, as bytes."""
    global count
    count += 1
    run = os.path.join(tmp, 'run%d' % count)
    rec = os.path.join(run, 'rec')
    os.makedirs(rec)
    os.makedirs(os.path.join(run, 'tmp'))
    path = os.path.join(run, name + '.doc')
    shutil.copy(src, path)
    os.environ['PVREC'] = rec
    os.environ['TMPDIR'] = os.path.join(run, 'tmp')
    tui.session([stred, '-H', helpf, path], keys + WAIT, rows=30)
    dvi = os.path.join(rec, 'copy.dvi')
    part = os.path.join(rec, 'part.doc')
    return (open(dvi, 'rb').read() if os.path.exists(dvi) else b'',
            open(part, 'rb').read() if os.path.exists(part) else b'')


def paper(dvi):
    """The width and length in points of the papersize special."""
    m = re.search(rb'papersize=([0-9.]+)pt,([0-9.]+)pt', dvi)

    return (float(m.group(1)), float(m.group(2))) if m else (0.0, 0.0)


def rotated(dvi):
    return b'/Rotate 90' in dvi


def make(name, size, marker=b'', tail=b''):
    """A document made from the number 10 envelope with the stored size
    given (as in the DP record) and an orientation marker block in front of
    the first block."""
    data = open(os.path.join(samples, 'sample-envelope-us-ca-10.doc'),
                'rb').read()
    data = data.replace(b'DP13,9500,4125', b'DP13,' + size)
    i = data.index(b'*B0\r\nS\r\n') + len(b'*B0\r\nS\r\n')
    path = os.path.join(tmp, name + '.doc')
    open(path, 'wb').write(data[:i] + marker + data[i:])

    return path


# Every sample envelope previews as a landscape sheet, the way it prints.
for f in sorted(glob.glob(os.path.join(samples, 'sample-envelope-*.doc'))):
    name = os.path.basename(f)[len('sample-envelope-'):-4]
    dvi, part = preview(f, FINAL, name)
    w, h = paper(dvi)
    check('sample %s previews as a landscape sheet and not turned' % name,
          w > h > 0 and not rotated(dvi), (w, h, rotated(dvi)))

# An envelope chosen in Global, Page, Paper Size previews as a landscape sheet.
us10 = make('portrait', b'4125,9500')
for label, steps in (('Business Envelope', 13), ('DL Envelope', 14),
                     ('C5 Envelope', 15), ('Monarch Envelope', 12)):
    plain = os.path.join(tmp, 'plain.doc')
    shutil.copy(os.path.join(root, 'templates', 'letters', 'letter-us.doc'), plain)
    dvi, part = preview(plain, PANEL + ['DOWN'] * 11 + ['F2', 'wait'] +
                        ['DOWN'] * steps + ['ENTER', 'wait', 'INS', 'wait'] +
                        FINAL, 'pick')
    w, h = paper(dvi)
    check('the %s chosen in the Page panel previews as a landscape sheet'
          % label, w > h > 0, (w, h))

# A portrait envelope with a landscape marker is turned.
dvi, part = preview(make('turned', b'4125,9500', LANDSCAPE), FINAL, 'turned')
w, h = paper(dvi)
check('a portrait envelope with a landscape marker previews turned',
      h > w > 0 and rotated(dvi), (w, h, rotated(dvi)))

# The block that follows a landscape marker is turned too: the part that is
# printed does not hold the marker, so the part says that it starts turned.
turned = make('later', b'4125,9500', LANDSCAPE)
DOWN4 = ['DOWN'] * 4
dvi, part = preview(turned, DOWN4 + BLOCK, 'later')
check('the part of a block after a landscape marker starts turned (PO2)',
      b'\r\nPO2\r\n' in part, part[:300])
check('the preview of a block after a landscape marker is turned',
      rotated(dvi), (paper(dvi), rotated(dvi)))
dvi, part = preview(turned, FINAL, 'whole')
check('the part of a whole document says nothing of the orientation',
      b'\r\nPO' not in part, part[:300])

# After a portrait marker the part is not turned, and before the landscape
# marker it is not either.
back = make('back', b'4125,9500', LANDSCAPE + PORTRAIT)
dvi, part = preview(back, DOWN4 + BLOCK, 'back')
check('the part of a block after landscape and then portrait is not turned',
      b'\r\nPO2\r\n' not in part and not rotated(dvi),
      (part[:300], rotated(dvi)))

# The window of the real Viewer in an X display.
if shutil.which('Xvfb') and shutil.which('xdotool') and \
   os.path.exists(dviview):
    r, w = os.pipe()
    xv = subprocess.Popen(['Xvfb', '-displayfd', str(w), '-screen', '0',
        '1280x1024x24'], pass_fds=(w,), stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    os.close(w)
    disp = ':' + os.read(r, 16).decode().strip()
    env = dict(os.environ)
    env['DISPLAY'] = disp

    def window(dvi_bytes, name):
        d = os.path.join(tmp, 'view-' + name)
        os.makedirs(d)
        p = os.path.join(d, 'a.dvi')
        open(p, 'wb').write(dvi_bytes)
        v = subprocess.Popen([dviview, p], cwd=d, env=env,
                             stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
        got = (0, 0)
        for _ in range(30):
            time.sleep(0.3)
            out = subprocess.run(['xdotool', 'search', '--onlyvisible',
                '--name', 'a.dvi'], env=env, capture_output=True,
                text=True).stdout.split()
            if out:
                geo = subprocess.run(['xdotool', 'getwindowgeometry',
                    out[-1]], env=env, capture_output=True,
                    text=True).stdout
                m = re.search(r'Geometry: (\d+)x(\d+)', geo)
                if m:
                    got = (int(m.group(1)), int(m.group(2)))
                    break
        v.terminate()
        v.wait()

        return got

    try:
        dvi, part = preview(os.path.join(samples,
                            'sample-envelope-us-ca-10.doc'), FINAL, 'x10')
        gw, gh = window(dvi, 'x10')
        check('the Viewer window of the number 10 envelope is wider than '
              'high', gw > gh > 0, (gw, gh))
        dvi, part = preview(make('xt', b'4125,9500', LANDSCAPE), FINAL, 'xt')
        gw, gh = window(dvi, 'xt')
        check('the Viewer window of a portrait envelope turned by a '
              'landscape marker is wider than high', gw > gh > 0, (gw, gh))
        dvi, part = preview(make('xp', b'4125,9500'), FINAL, 'xp')
        gw, gh = window(dvi, 'xp')
        check('the Viewer window of a portrait envelope is higher than wide',
              gh > gw > 0, (gw, gh))
    finally:
        xv.terminate()
        xv.wait()
else:
    print('SKIP: the Viewer window (Xvfb, xdotool or dviview is missing)')

sys.exit(1 if fails else 0)
