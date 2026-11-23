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

"""Edit, Window: two documents on the screen at once.

The Editor is driven in a pseudo-terminal.  Open splits the screen into two
windows, each with its status line and its text; Switch moves the cursor to the
other window; Copy in one window and Paste in the other carry text from one
document to the other; Adjust changes the size of the windows; Zoom-up makes
the window of the cursor fill the screen and Zoom-down gives the screen back;
Close closes the window of the cursor and asks about unsaved changes.

usage: STRED=path/to/stred python3 -I -B tests/windows.py
"""
import sys as _s; _s.dont_write_bytecode = True
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tui  # noqa: E402
import movesect  # noqa: E402

BUILD = os.environ.get('STRATA_BUILD_DIR', '')
STRED = os.environ.get('STRED', os.path.join(BUILD, 'term', 'stred'))
HELP = os.environ.get('STRATA_HELP', os.path.join(BUILD, 'strata.hlp'))
FAILS = []

# The menu: F12, Edit, Window, then the entry.  Zoom-down is the fifth entry.
OPEN = ['F12', 'text:E', 'text:W', 'text:O']
CLOSE = ['F12', 'text:E', 'text:W', 'text:C']
ADJUST = ['F12', 'text:E', 'text:W', 'text:A']
ZOOM_UP = ['F12', 'text:E', 'text:W', 'text:Z']
ZOOM_DOWN = ['F12', 'text:E', 'text:W', 'RIGHT', 'RIGHT', 'RIGHT', 'RIGHT',
             'ENTER']
SWITCH = ['F12', 'text:E', 'text:W', 'text:S']


def check(name, ok, detail=None):
    print(('ok   ' if ok else 'FAIL ') + name)

    if not ok:
        FAILS.append(name)

        if detail is not None:
            print('     ' + str(detail))


def status_rows(screen):
    """The screen rows that hold a status line."""
    return [i for i, ln in enumerate(screen.split('\n')) if 'Attr:' in ln]


def run(steps, rows=24):
    path = os.path.join(TMP, 'a.doc')

    with open(path, 'wb') as f:
        f.write(movesect.make(movesect.TREE))

    return tui.session([STRED, '-H', HELP, path], steps, rows=rows), path


def main():
    global TMP

    if not os.path.exists(STRED):
        print('SKIP: build the programs first')

        return 0

    TMP = tempfile.mkdtemp(prefix='win')
    out, _p = run(['show'] + OPEN + ['wait', 'show'])
    check('one window has one status line',
          len(status_rows(out[0])) == 1, status_rows(out[0]))
    two = out[1]
    rows = status_rows(two)
    check('Open: the screen has two status lines', len(rows) == 2, rows)
    check('Open: the second window is a new unstructured document',
          len(rows) == 2 and '(new)' in two.split('\n')[rows[1]] and
          'UNSTRUCTURED EDIT' in two.split('\n')[rows[1]], two)
    check('Open: the first window keeps its document',
          len(rows) == 2 and 'a.doc' in two.split('\n')[rows[0]] and
          'One' in two, two)
    check('Open: the windows share the screen about half and half',
          len(rows) == 2 and 8 <= rows[1] <= 14, rows)

    # Typing goes to the window of the cursor: the second one after Open.
    out, _p = run(OPEN + ['text:zzz', 'wait', 'show'])
    scr = out[0].split('\n')
    rows = status_rows(out[0])
    check('typing after Open goes to the second window',
          len(rows) == 2 and any('zzz' in ln for ln in scr[rows[1]:]) and
          not any('zzz' in ln for ln in scr[:rows[1]]), out[0])

    # Switch: the cursor goes to the first window.
    out, _p = run(OPEN + SWITCH + ['text:yyy', 'wait', 'show'])
    scr = out[0].split('\n')
    rows = status_rows(out[0])
    check('Switch moves the cursor to the other window',
          len(rows) == 2 and any('yyy' in ln for ln in scr[:rows[1]]) and
          not any('yyy' in ln for ln in scr[rows[1]:]), out[0])

    # Copy in one window, Paste in the other.
    steps = (OPEN + SWITCH + ['DOWN', 'F3', 'DOWN', 'DOWN', 'ENTER', 'ENTER'] +
             SWITCH + ['F5', 'wait', 'show'])
    out, _p = run(steps)
    scr = out[0].split('\n')
    rows = status_rows(out[0])
    check('Copy in the first window and Paste in the second carry the block',
          len(rows) == 2 and any('one a' in ln for ln in scr[rows[1]:]),
          out[0])

    # Zoom-up and Zoom-down.
    out, _p = run(OPEN + ZOOM_UP + ['wait', 'show'] + ZOOM_DOWN +
                  ['wait', 'show'])
    check('Zoom-up: the window of the cursor fills the screen',
          len(status_rows(out[0])) == 1 and '(new)' in out[0], out[0])
    check('Zoom-down: the screen has two windows again',
          len(status_rows(out[1])) == 2, out[1])

    # Adjust: Down moves the divider down by one row; Esc takes it back.
    out, _p = run(OPEN + ['show'] + ADJUST + ['DOWN', 'DOWN', 'ENTER', 'wait',
                                              'show'])
    before = status_rows(out[0])
    after = status_rows(out[1])
    check('Adjust: Down twice moves the second window two rows down',
          len(before) == 2 and len(after) == 2 and after[1] == before[1] + 2,
          (before, after))
    out, _p = run(OPEN + ['show'] + ADJUST + ['UP', 'ESC', 'wait', 'show'])
    check('Adjust: Esc keeps the size',
          status_rows(out[0]) == status_rows(out[1]), out)
    out, _p = run(OPEN + ['show'] + ADJUST + ['UP', 'UP', 'ENTER', 'wait',
                                              'show'])
    before = status_rows(out[0])
    after = status_rows(out[1])
    check('Adjust: Up twice moves the second window two rows up',
          len(before) == 2 and len(after) == 2 and after[1] == before[1] - 2,
          (before, after))

    # Close: an unchanged window closes at once.
    out, _p = run(OPEN + CLOSE + ['wait', 'show'])
    check('Close: an unchanged window closes and the first one is left',
          len(status_rows(out[0])) == 1 and 'a.doc' in out[0], out[0])

    # Close with changes: the question; Discard closes.
    out, _p = run(OPEN + ['text:zzz'] + CLOSE + ['wait', 'show', 'DOWN',
                                                 'ENTER', 'wait', 'show'])
    check('Close: a changed window asks what to do with the changes',
          'unsaved changes' in out[0], out[0])
    check('Close: Discard closes the window',
          len(status_rows(out[1])) == 1 and 'a.doc' in out[1], out[1])

    # Close with changes: Cancel keeps the window.
    out, _p = run(OPEN + ['text:zzz'] + CLOSE + ['DOWN', 'DOWN', 'ENTER',
                                                 'wait', 'show'])
    check('Close: Cancel keeps both windows',
          len(status_rows(out[0])) == 2, out[0])

    # The first window can close too: the second one stays.
    out, _p = run(OPEN + SWITCH + CLOSE + ['wait', 'show'])
    check('Close in the first window leaves the second one',
          len(status_rows(out[0])) == 1 and '(new)' in out[0], out[0])

    # Quit prompts about the other window as well.
    out, _p = run(OPEN + ['text:zzz'] + SWITCH + ['C-q', 'wait', 'show'])
    check('Quit prompts about the changes of the other window',
          'unsaved changes' in out[0], out[0])

    print('windows: %d failures' % len(FAILS))

    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
