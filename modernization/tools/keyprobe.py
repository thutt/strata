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

"""Find out which keys reach the Editor in which environment.

This is a developer tool for the study docs/KEYS-ENVIRONMENTS.txt.  It is
not part of the smoke tests.  It never installs anything and it keeps every
file it makes, and the home and the XDG folders of every program it starts,
inside its scratch folder.

usage:
  keyprobe.py x11 [--wm none|gnome] [--scratch DIR]
      Start a private Xvfb (with the GNOME Shell window manager when --wm is
      gnome), send each key of the list with xdotool to an xev window and
      print what the window receives: the keysym, or nothing when the key
      was taken by the window manager.
  keyprobe.py term [--terminal xterm|xfce4-terminal|tmux] [--wm none|gnome]
                   [--xrm RESOURCES] [--scratch DIR]
      The same, with the key logger of a terminal window: the bytes that the
      terminal sends to its program are printed in hexadecimal.  The option
      --xrm gives resources to xterm, for example
      'XTerm*metaSendsEscape: true' (several, separated by commas).
  keyprobe.py terminfo [--stred PROGRAM] [--scratch DIR]
      For the terminals in the list TERMS take the sequences that the
      terminfo entry says its function keys send, give each to the terminal
      version of the Editor in a pseudo terminal and print which key the
      Editor reads it as (the Editor reads the key sequences itself and does
      not look at TERM).

Needs Xvfb, xdotool and xev (x11, term), xterm, xfce4-terminal, tmux (term)
and infocmp (terminfo).  The environment variable STRED names the Editor.
"""
import sys as _s
_s.dont_write_bytecode = True
import os
import re
import signal
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# The terminal types whose function key sequences are compared with what
# the Editor reads.
TERMS = ['linux', 'xterm', 'xterm-256color', 'screen', 'tmux-256color',
         'rxvt-unicode-256color', 'vt100', 'st-256color', 'kitty',
         'alacritty', 'foot', 'konsole', 'gnome', 'xfce', 'putty']


def keylist():
    """The keys that the Editor uses, as xdotool names, in a stable order."""
    keys = []

    for n in range(1, 13):
        keys.append('F%d' % n)

    for mod in ('shift', 'ctrl', 'alt', 'ctrl+alt'):
        for n in range(1, 13):
            keys.append('%s+F%d' % (mod, n))

    for c in 'abcdefghijklmnopqrstuvwxyz':
        keys.append('ctrl+' + c)

    keys += ['ctrl+bracketleft', 'ctrl+bracketright', 'ctrl+backslash',
             'ctrl+shift+6', 'ctrl+minus', 'ctrl+space', 'ctrl+shift+space']

    for c in 'oi0123456789':
        keys.append('alt+' + c)

    keys += ['alt+plus', 'alt+minus', 'alt+asterisk', 'alt+bracketleft',
             'alt+bracketright', 'alt+less', 'alt+greater', 'alt+comma',
             'alt+period', 'alt+BackSpace', 'ctrl+BackSpace', 'alt+Tab',
             'alt+space']

    for k in ('Insert', 'Delete', 'Home', 'End', 'Prior', 'Next', 'Left',
              'Right', 'Up', 'Down'):
        keys.append(k)

        for mod in ('shift', 'ctrl'):
            keys.append(mod + '+' + k)

    keys += ['shift+Tab', 'KP_Add', 'KP_Subtract', 'KP_Multiply', 'Menu',
             'Escape']
    return keys


def run(argv, **kw):
    return subprocess.run(argv, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, **kw)


class Env:
    """A private X server, optionally with a window manager, whose files all
    live in the scratch folder."""

    def __init__(self, scratch, wm):
        self.scratch = scratch
        self.wm = wm
        self.procs = []
        self.env = dict(os.environ)
        home = os.path.join(scratch, 'home')

        for d in ('home', 'config', 'cache', 'data', 'run', 'tmp'):
            os.makedirs(os.path.join(scratch, d), exist_ok=True)

        os.chmod(os.path.join(scratch, 'run'), 0o700)
        self.env.update({
            'HOME': home,
            'XDG_CONFIG_HOME': os.path.join(scratch, 'config'),
            'XDG_CACHE_HOME': os.path.join(scratch, 'cache'),
            'XDG_DATA_HOME': os.path.join(scratch, 'data'),
            'XDG_RUNTIME_DIR': os.path.join(scratch, 'run'),
            'TMPDIR': os.path.join(scratch, 'tmp'),
            'STRATA_HOME': os.path.join(scratch, 'home')})
        self.env.pop('DISPLAY', None)

    def start(self):
        r, w = os.pipe()
        p = subprocess.Popen(['Xvfb', '-displayfd', str(w), '-screen', '0',
                              '1024x768x24', '-noreset'],
                             pass_fds=(w,), env=self.env,
                             stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
        os.close(w)
        self.procs.append(p)
        num = os.read(r, 16).decode().strip()
        os.close(r)
        self.env['DISPLAY'] = ':' + num

        if self.wm == 'gnome':
            gp = subprocess.Popen(['dbus-run-session', '--', 'gnome-shell',
                                   '--x11'], env=self.env,
                                  stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL,
                                  start_new_session=True)
            self.procs.append(gp)
            end = time.time() + 40

            while time.time() < end:
                out = run(['xprop', '-root', '_NET_SUPPORTING_WM_CHECK'],
                          env=self.env).stdout.decode()

                if 'window id' in out:
                    break

                time.sleep(1)

            time.sleep(6)

    def stop(self):
        for p in reversed(self.procs):
            try:
                if p.args[0] == 'dbus-run-session':
                    os.killpg(p.pid, signal.SIGTERM)
                else:
                    p.terminate()
            except (ProcessLookupError, PermissionError):
                pass

        for p in self.procs:
            try:
                p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                p.kill()

    def key(self, name):
        run(['xdotool', 'key', '--delay', '40', name], env=self.env)


def focus(env, win):
    """Give the keyboard focus to a window.  A window manager may ignore a
    plain focus request, so the window is also activated and clicked."""
    if env.wm != 'none':
        run(['xdotool', 'windowactivate', win], env=env.env)
        run(['xdotool', 'mousemove', '60', '60', 'click', '1'], env=env.env)

    run(['xdotool', 'windowfocus', win], env=env.env)


def wait_window(env, name, seconds=15):
    end = time.time() + seconds

    while time.time() < end:
        out = run(['xdotool', 'search', '--name', name],
                  env=env.env).stdout.decode().split()

        if out:
            return out[-1]

        time.sleep(0.3)

    return None


def probe_x11(env):
    """What an X client receives: the keysyms, from xev."""
    log = os.path.join(env.scratch, 'xev.txt')
    f = open(log, 'wb')
    p = subprocess.Popen(['xev', '-name', 'keyprobe', '-geometry',
                          '300x200+10+10', '-event', 'keyboard'],
                         env=env.env, stdout=f, stderr=subprocess.STDOUT)
    env.procs.append(p)
    win = wait_window(env, 'keyprobe')
    time.sleep(1)
    result = {}

    for k in keylist():
        focus(env, win)
        before = os.path.getsize(log)
        env.key(k)
        time.sleep(0.35)
        text = open(log, 'rb').read()[before:].decode('latin1')
        syms = re.findall(r'KeyPress event.*?keysym 0x[0-9a-f]+, ([^\)]+)\)',
                          text, re.S)
        result[k] = ' '.join(syms) if syms else '(nothing)'
        env.key('Escape')
        time.sleep(0.2)

    return result


KEYLOG = r'''
import os, sys, tty, termios, select, time
out = sys.argv[1]
fd = sys.stdin.fileno()
tty.setraw(fd)
f = open(out, 'wb', buffering=0)
f.write(b'READY\n')
end = time.time() + 3000
while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        d = os.read(fd, 64)
        f.write(d.hex().encode() + b'\n')
'''


def xterm_options(xrm):
    """The -xrm options of xterm for a resource string such as
    'XTerm*metaSendsEscape: true' (several are separated by commas)."""
    opts = []

    for r in [x for x in xrm.split(',') if x != '']:
        opts += ['-xrm', r]

    return opts


def probe_term(env, terminal, xrm=''):
    """What a terminal sends to its program, as the bytes of a key logger."""
    logger = os.path.join(env.scratch, 'keylog.py')
    open(logger, 'w').write(KEYLOG)
    log = os.path.join(env.scratch, 'keys.txt')

    if os.path.exists(log):
        os.unlink(log)

    child = [sys.executable, '-I', '-B', logger, log]

    if terminal == 'tmux':
        sock = os.path.join(env.scratch, 'tmux.sock')
        cmd = ['xterm', '-name', 'keyprobe'] + xterm_options(xrm) + [
            '-e', 'tmux', '-S', sock, '-f', '/dev/null', 'new-session'
        ] + child
    elif terminal == 'xfce4-terminal':
        cmd = ['xfce4-terminal', '--disable-server', '--title', 'keyprobe',
               '-x'] + child
    else:
        cmd = ['xterm', '-name', 'keyprobe'] + xterm_options(xrm) + [
            '-e'] + child

    p = subprocess.Popen(cmd, env=env.env, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL)
    env.procs.append(p)
    win = wait_window(env, 'keyprobe') or wait_window(env, '', 5)
    end = time.time() + 20

    while time.time() < end and not os.path.exists(log):
        time.sleep(0.3)

    time.sleep(1.5)
    result = {}

    for k in keylist():
        focus(env, win)
        before = len(open(log, 'rb').read().split(b'\n'))
        env.key(k)
        time.sleep(0.35)
        lines = open(log, 'rb').read().split(b'\n')[before - 1:-1]
        result[k] = ' '.join(x.decode() for x in lines) or '(nothing)'
        env.key('Escape')
        time.sleep(0.2)

    return result


def load_tui():
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        'tui', os.path.join(ROOT, 'tests', 'tui.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# The keys that the Editor reads from terminals, by the name that the module
# tests/tui.py uses.  The F-keys are the ones the key strip shows.
def canonical_keys():
    names = []

    for n in range(1, 13):
        for pre in ('', 'S-', 'C-', 'A-'):
            names.append('%sF%d' % (pre, n))

    names += ['UP', 'DOWN', 'LEFT', 'RIGHT', 'HOME', 'END', 'PGUP', 'PGDN',
              'INS', 'DEL', 'S-UP', 'S-DOWN', 'S-LEFT', 'S-RIGHT', 'S-HOME',
              'S-END', 'S-PGUP', 'S-PGDN', 'S-INS', 'C-LEFT', 'C-RIGHT',
              'C-HOME', 'C-END', 'C-PGUP', 'C-PGDN', 'BTAB']
    return names


def editor_screen(tui, stred, doc, data):
    """The screen of the Editor after it was given the bytes."""
    steps = ['wait', 'text:' + data, 'wait', 'show']
    out = tui.session([stred, doc], steps, delay=0.15)
    return out[0] if out else ''


def terminfo_caps(term):
    """The key capabilities of a terminal type as a dictionary name -> bytes
    (the terminfo escape forms are turned into the characters)."""
    out = run(['infocmp', '-1', '-x', term]).stdout.decode('latin1')
    caps = {}

    for line in out.split('\n'):
        m = re.match(r'\s*(k[A-Za-z0-9]+)=(.*),\s*$', line)

        if m is None:
            continue

        val = m.group(2)
        val = val.replace('\\E', '\x1b').replace('\\,', ',')
        val = re.sub(r'\^(.)', lambda x: chr(ord(x.group(1)) & 0x1f), val)
        caps[m.group(1)] = val

    return caps


def probe_terminfo(stred, scratch):
    tui = load_tui()
    doc = os.path.join(scratch, 'probe.doc')
    src = os.path.join(ROOT, 'tests', 'data', 'plain_text.doc')
    open(doc, 'wb').write(open(src, 'rb').read())
    os.environ['TMPDIR'] = os.path.join(scratch, 'tmp')
    os.environ['STRATA_HOME'] = os.path.join(scratch, 'home')
    os.makedirs(os.environ['TMPDIR'], exist_ok=True)
    os.makedirs(os.environ['STRATA_HOME'], exist_ok=True)
    base = editor_screen(tui, stred, doc, '')
    canon = {}

    for name in canonical_keys():
        if name in tui.KEYS:
            canon[name] = editor_screen(tui, stred, doc, tui.KEYS[name])

    result = {}
    seen = {}

    for term in TERMS:
        caps = terminfo_caps(term)
        rows = []

        for cap in sorted(caps, key=lambda c: (len(c), c)):
            if not re.match(r'k(f[0-9]+|[A-Z]{3}[0-9]?|ich1|dch1|cuu1|cud1|'
                            r'cub1|cuf1|home|end|pp|np|cbt|bs)$', cap):
                continue

            seq = caps[cap]

            if (cap, seq) in seen:
                rows.append((cap, seq, seen[(cap, seq)]))
                continue

            scr = editor_screen(tui, stred, doc, seq)
            read = [n for n, s in canon.items() if s == scr and scr != base]

            if scr == base:
                what = 'ignored'
            elif read:
                what = '/'.join(read)
            else:
                what = 'other'

            seen[(cap, seq)] = what
            rows.append((cap, seq, what))

        result[term] = rows

    return result


def main():
    args = sys.argv[1:]

    if not args or args[0] not in ('x11', 'term', 'terminfo'):
        print(__doc__)
        return 2

    cmd = args[0]
    opt = {'--wm': 'none', '--terminal': 'xterm', '--scratch': None,
           '--xrm': '',
           '--stred': os.environ.get('STRED', '')}

    i = 1

    while i < len(args):
        if args[i] not in opt or i + 1 >= len(args):
            print(__doc__)
            return 2

        opt[args[i]] = args[i + 1]
        i += 2

    scratch = opt['--scratch'] or tempfile.mkdtemp(prefix='keyprobe-')
    os.makedirs(scratch, exist_ok=True)

    if cmd == 'terminfo':
        if opt['--stred'] == '':
            print('give --stred or set STRED')
            return 2

        res = probe_terminfo(opt['--stred'], scratch)

        for term, rows in res.items():
            print('TERM', term)

            for cap, seq, what in rows:
                shown = seq.replace('\x1b', 'ESC ')
                print('  %-8s %-18s %s' % (cap, shown, what))

        return 0

    env = Env(scratch, opt['--wm'])

    try:
        env.start()

        if cmd == 'x11':
            res = probe_x11(env)
        else:
            res = probe_term(env, opt['--terminal'], opt['--xrm'])
    finally:
        env.stop()

    for k, v in res.items():
        print('%-22s %s' % (k, v))

    return 0


if __name__ == '__main__':
    sys.exit(main())
