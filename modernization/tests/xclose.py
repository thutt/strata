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

"""The close button of the window of the X11 Editor acts as Quit.

Xvfb has no window manager, so the message that the close button sends
(WM_DELETE_WINDOW) is sent by tests/xclose.c.  An unchanged document ends the
Editor at once; a changed document opens the question that Quit prompts (Save and
quit, Discard changes and quit, Cancel): the Editor stays, Esc keeps it, and
the letter D ends it without saving.

usage: STRATA_BUILD_DIR=/tmp/x/debug python3 -I -B tests/xclose.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.environ.get('STRATA_BUILD_DIR', '')
STRED = os.environ.get('STRED_X11', os.path.join(BUILD, 'x11', 'stred'))
HELP = os.environ.get('STRATA_HELP', os.path.join(BUILD, 'strata.hlp'))
FAILS = []


def check(name, ok, detail=None):
    print(('ok   ' if ok else 'FAIL ') + name)

    if not ok:
        FAILS.append(name)

        if detail is not None:
            print('     ' + str(detail))


def alive(p, wait=0.0):
    end = time.time() + wait

    while True:
        if p.poll() is not None:
            return False

        if time.time() >= end:
            return True

        time.sleep(0.1)


def main():
    if not os.path.exists(STRED) or shutil.which('Xvfb') is None or \
            shutil.which('xdotool') is None or shutil.which('cc') is None:
        print('SKIP: the X11 build, Xvfb, xdotool and cc are needed')

        return 0

    tmp = tempfile.mkdtemp(prefix='xc')
    helper = os.path.join(tmp, 'xclose')
    subprocess.run(['cc', '-o', helper, os.path.join(HERE, 'xclose.c'),
                    '-lX11'], check=True)
    disp = ':%d' % (700 + os.getpid() % 200)
    xvfb = subprocess.Popen(['Xvfb', disp, '-screen', '0', '1024x768x24'],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    env = dict(os.environ, DISPLAY=disp, HOME=tmp, STRATA_HOME=tmp)
    time.sleep(1.5)

    def window(name):
        for _i in range(40):
            r = subprocess.run(['xdotool', 'search', '--name', name],
                               capture_output=True, text=True, env=env)

            if r.stdout.split():
                return r.stdout.split()[-1]

            time.sleep(0.25)

        return None

    def start(path):
        return subprocess.Popen([STRED, '-H', HELP, path], env=env,
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)

    try:
        path = os.path.join(tmp, 'a.doc')

        with open(path, 'wb') as f:
            f.write(b'*R3\r\nMR1000\r\nPP\\1\\\x00\r\nDP0,8500,11000\r\n*B0\r\n'
                    b'S\r\nTtext\x1d\x00\r\n*E\r\n')

        p = start(path)
        w = window('stred')
        check('the Editor opened its window', w is not None)

        if w is not None:
            subprocess.run([helper, w], env=env, check=True)
            check('an unchanged document: the close button ends the Editor',
                  alive(p, 3.0) is False)

        if p.poll() is None:
            p.kill()
            p.wait()

        before = open(path, 'rb').read()
        p = start(path)
        w = window('stred')

        if w is not None:
            subprocess.run(['xdotool', 'windowfocus', w], env=env)
            time.sleep(0.5)
            subprocess.run(['xdotool', 'type', '--delay', '60', 'zzz'],
                           env=env)
            time.sleep(0.5)
            subprocess.run([helper, w], env=env, check=True)
            check('a changed document: the close button asks and the Editor '
                  'stays', alive(p, 1.5) is True)
            subprocess.run(['xdotool', 'key', 'Escape'], env=env)
            time.sleep(0.5)
            check('Esc (Cancel) keeps the Editor', alive(p, 1.0) is True)
            subprocess.run([helper, w], env=env, check=True)
            time.sleep(0.5)
            subprocess.run(['xdotool', 'type', '--delay', '60', 'd'], env=env)
            check('the letter D (Discard changes and quit) ends the Editor',
                  alive(p, 3.0) is False)
            check('and nothing is saved', open(path, 'rb').read() == before)

        if p.poll() is None:
            p.kill()
            p.wait()
    finally:
        xvfb.terminate()
        xvfb.wait()

    print('xclose: %d failures' % len(FAILS))

    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
