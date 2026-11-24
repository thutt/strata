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

"""The Print panel can search for printers, and its list is kept fresh.

The Printer field of F12, Print, Final has a last entry, Search for
printers..., that runs lpinfo -v in the background (STRATA_LPINFO names it)
for at most ten seconds, polls it while a message is drawn (Esc stops it),
and shows the devices of the network with their address and the command that
adds each.  Strata never adds a queue itself.  The test names fake programs:
a lpinfo with canned lines, one that finds nothing, one that fails, one that
sleeps, and a lpstat that first reports no queue and then one; it presses
the keys of the panel and reads the screen.  Nothing is sent to a printer.

    STRED=... STRATA_HELP=... python3 -I -B tests/printsearch.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os, re, shutil, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tui

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
build = os.environ.get('STRATA_BUILD_DIR', '')
stred = os.environ.get('STRED', os.path.join(build, 'term', 'stred'))
helpf = os.environ.get('STRATA_HELP', os.path.join(build, 'strata.hlp'))
sample = os.path.join(root, 'manual', 'samples', 'chars.doc')

if not os.path.exists(stred) or not os.path.exists(sample):
    print('SKIP: build the programs first')
    sys.exit(0)

fails = 0
tmp = tempfile.mkdtemp(prefix='psrch')
doc = os.path.join(tmp, 'chars.doc')
shutil.copy(sample, doc)


def check(name, cond, extra=''):
    global fails
    print(('ok   ' if cond else 'FAIL ') + name + ('' if cond else '\n'
          + str(extra)))
    fails += 0 if cond else 1


def fake(name, body):
    path = os.path.join(tmp, name)
    with open(path, 'w') as f:
        f.write('#!/bin/sh\n' + body)
    os.chmod(path, 0o755)
    return path


DEVICES = fake('lpinfo-devices',
               'cat <<EOF\n'
               'network socket://192.168.1.201:9100\n'
               'network dnssd://HP%20LaserJet%20P2015dn%20%40%20host.'
               '_ipp._tcp.local/?uuid=1\n'
               'direct usb://HP/LaserJet%20P2015dn?serial=XYZ\n'
               'serial serial://dev/ttyS0?baud=115200\n'
               'network ipp\n'
               'EOF\n')
NOTHING = fake('lpinfo-nothing', 'exit 0\n')
BROKEN = fake('lpinfo-broken', 'echo "lpinfo: Forbidden"\nexit 1\n')
SLEEPER = fake('lpinfo-sleeper', 'exec sleep 20\n')

FINAL = ['F12', 'text:P', 'RIGHT', 'ENTER', 'waitfor:Print Document']
# DOWN goes from Output File to Printer, F2 opens the list of its entries.
LIST = FINAL + ['DOWN', 'F2', 'waitfor:PDF viewer']
# END and Enter choose the last entry in the list; Enter on the field runs it.
SEARCH = LIST + ['END', 'ENTER', 'ENTER']


def home():
    h = tempfile.mkdtemp(prefix='h', dir=tmp)
    os.chmod(h, 0o700)
    return h


def run(steps, lpinfo=None, extra_env=None, rows=50):
    env = {'STRATA_HOME': home(), 'LANG': 'en_US.UTF-8', 'LC_ALL': None,
           'STRATA_ASCII': '0', 'TMUX': None,
           'STRATA_LPSTAT': os.path.join(tmp, 'no-lpstat'),
           'STRATA_LPINFO': lpinfo or None}
    if extra_env:
        env.update(extra_env)
    return tui.session([stred, doc, '-H', helpf], steps, env=env, rows=rows)


# ---- the list has the entry --------------------------------------------------
out = run(LIST + ['show'])
check('the Printer list of the Print panel ends with Search for printers...',
      'Search for printers...' in out[0] and 'PDF viewer' in out[0], out[0])

# With queues the list holds them, the system default and, last, the search.
QUEUES = fake('lpstat-queues',
              'if [ "$1" = "-a" ]; then\n'
              '    echo "laser accepting requests since Thu Nov 20 08:00"\n'
              'fi\n')
out = run(LIST + ['show'], None, {'STRATA_LPSTAT': QUEUES})
pos = [out[0].find(w) for w in ('File only', 'PDF viewer', 'laser',
                                'System default printer',
                                'Search for printers...')]
check('with queues the list is File only, PDF viewer, the queues, System '
      'default printer and, last, Search for printers...',
      all(p >= 0 for p in pos) and pos == sorted(pos), (pos, out[0]))

# ---- the devices ---------------------------------------------------------------
out = run(SEARCH + ['waitfor:Printers found', 'show'], DEVICES)
s = out[0]
check('the search shows the network devices with their address',
      'socket://192.168.1.201:9100' in s and 'dnssd://HP%20LaserJet' in s and
      'usb://HP/LaserJet' in s, s)
check('a device of another class and a backend with no address are not '
      'shown', 'serial://dev/ttyS0' not in s and 'network ipp' not in s, s)
check('a socket device gets the command with a driver to look for',
      'sudo /usr/sbin/lpadmin -p printer_192_168_1_201 -E' in s and
      '-m DRIVER' in s and '/usr/sbin/lpinfo -m' in s, s)
check('a dnssd device gets the command with -m everywhere and its name',
      'HP_LaserJet_P2015dn' in s and '-m everywhere' in s, s)
check('the panel shows that Strata adds no queue', 'does not add a queue' in s,
      s)

# Closing the list leaves the field as it was and no queue is added.
out = run(SEARCH + ['waitfor:Printers found', 'ESC', 'waitfor:Print Document',
                    'show'], DEVICES)
check('after the list is closed the Print panel is back and the Printer '
      'field has not changed', 'Print Document' in out[0] and
      'Search for printers' not in out[0] and 'File only' in out[0], out[0])
src = ''
for fn in os.listdir(os.path.join(root, 'src')):
    if fn.endswith('.c'):
        src += open(os.path.join(root, 'src', fn), errors='replace').read()
check('no source starts lpadmin or sudo: the word only stands in the text '
      'that tells the owner what to type',
      re.search(r'(sp_find|exec|system|popen)[^;]*lpadmin', src) is None and
      re.search(r'(sp_find|exec|system|popen)[^;]*"sudo"', src) is None)

# ---- nothing found, an error, no lpinfo ------------------------------------------
out = run(SEARCH + ['waitfor:No printer was found', 'show'], NOTHING)
check('nothing found: the panel shows so and says the printer must be on and '
      'on the same network', 'No printer was found' in out[0] and
      'switched on' in out[0] and 'same network' in out[0], out[0])
out = run(SEARCH + ['waitfor:lpinfo reported', 'show'], BROKEN)
check('an error of lpinfo: its text is shown', 'Forbidden' in out[0] and
      'lpinfo reported' in out[0], out[0])
empty = os.path.join(tmp, 'emptydir')
os.makedirs(empty)
out = run(SEARCH + ['waitfor:was not found', 'show'], '',
          {'STRATA_PROGRAM_DIRS': empty})
check('no lpinfo in the system folders: the message shows so',
      'lpinfo was not found' in out[0], out[0])

# ---- the time limit and Esc ------------------------------------------------------------
out = run(SEARCH + ['waitfor:Searching for printers', 'clock', 'waitfor:No '
                    'printer was found in 10 seconds', 'clock', 'show'],
          SLEEPER)
secs = float(out[1]) - float(out[0])
check('a search that does not end is stopped after 10 seconds (%.1f s)' %
      secs, 9.5 <= secs <= 12.0 and 'in 10 seconds' in out[2], out[2])

out = run(SEARCH + ['waitfor:Searching for printers', 'clock', 'ESC',
                    'waitfor:Print Document', 'clock', 'show'], SLEEPER)
secs = float(out[1]) - float(out[0])
check('Esc stops the search at once: the panel is back %.2f s after the key '
      '(no key waits more than half a second)' % secs,
      secs < 0.5 and 'Print Document' in out[2], out[2])

# ---- the list is asked for again --------------------------------------------------------
count = os.path.join(tmp, 'count')
open(count, 'w').write('0')
LPSTAT = fake('lpstat-counting',
              'if [ "$1" = "-a" ]; then\n'
              '    n=$(cat %s); n=$((n+1)); echo $n > %s\n'
              '    if [ $n -ge 2 ]; then\n'
              '        echo "newq accepting requests since Thu Nov 20 08:00"\n'
              '    fi\n'
              'fi\n' % (count, count))
REFRESH = {'STRATA_LPSTAT': LPSTAT, 'STRATA_LPSTAT_REFRESH_MS': '0'}
out = run(LIST + ['show', 'ESC', 'ESC', 'waitfor:F1: Help'] + LIST +
          ['show'], DEVICES, REFRESH)
check('the first opening has no queue', 'newq' not in out[0] and
      'Search for printers...' in out[0], out[0])
check('a queue that appears while the program runs is in the list at the '
      'next opening, with no restart', 'newq' in out[1], out[1])
BIG = {'STRATA_LPSTAT': LPSTAT, 'STRATA_LPSTAT_REFRESH_MS': '600000'}
open(count, 'w').write('0')
out = run(LIST + ['show', 'ESC', 'ESC', 'waitfor:F1: Help'] + LIST +
          ['show'], DEVICES, BIG)
check('with a young answer the list is not asked for again',
      'newq' not in out[1], out[1])

shutil.rmtree(tmp, ignore_errors=True)
print('printsearch: %d failures' % fails)
sys.exit(1 if fails else 0)
